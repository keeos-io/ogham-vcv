// -----------------------------------------------------------------------------
// Ogham — a dual-voice bytebeat synthesizer for Eurorack
//
// Author:     Steven Collins, 2026, Keeos.io
// Copyright:  (c) 2026 Steven Collins
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
//
// This file is part of the Ogham firmware. See LICENSE-firmware.txt at the
// repository root for the full licence text.
// https://github.com/keeos-io/ogham
// -----------------------------------------------------------------------------

#include "bpm_clock.h"
#include <cmath>
#include <cstring>

// Compact in-place radix-2 DIT FFT for N complex values.
// data[0..2N-1] = interleaved [re0, im0, re1, im1, ...]
static void fft_inplace(float* data, int N) {
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < N; i++) {
        int bit = N >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            float tr = data[2 * i];
            data[2 * i] = data[2 * j];
            data[2 * j] = tr;
            float ti = data[2 * i + 1];
            data[2 * i + 1] = data[2 * j + 1];
            data[2 * j + 1] = ti;
        }
    }

    // Butterfly stages
    for (int len = 2; len <= N; len *= 2) {
        float angle = -6.2831853f / (float)len;
        float wR = cosf(angle);
        float wI = sinf(angle);
        for (int i = 0; i < N; i += len) {
            float cR = 1.0f, cI = 0.0f;
            for (int j = 0; j < len / 2; j++) {
                int u = 2 * (i + j);
                int v = 2 * (i + j + len / 2);
                float tR = cR * data[v] - cI * data[v + 1];
                float tI = cR * data[v + 1] + cI * data[v];
                data[v] = data[u] - tR;
                data[v + 1] = data[u + 1] - tI;
                data[u] += tR;
                data[u + 1] += tI;
                float nR = cR * wR - cI * wI;
                float nI = cR * wI + cI * wR;
                cR = nR;
                cI = nI;
            }
        }
    }
}

static float MedianOf(const float* src, int n);

void BpmClock::Init() {
    memset(audioBuffer_, 0, sizeof(audioBuffer_));
    audioWritePos_ = 0;
    frameWritePos_ = 0;
    decimCounter_ = 0;
    frameReady_ = false;

    // Periodic Hann, built once. Periodic (divide by FFT_SIZE) rather than
    // symmetric (FFT_SIZE - 1) because these frames overlap.
    for (int i = 0; i < FFT_SIZE; i++) {
        window_[i] = 0.5f * (1.0f - cosf(6.2831853f * (float)i / (float)FFT_SIZE));
    }

    memset(curMag_, 0, sizeof(curMag_));
    memset(prevMag_, 0, sizeof(prevMag_));
    hasPrevMag_ = false;

    memset(fluxBuffer_, 0, sizeof(fluxBuffer_));
    fluxWritePos_ = 0;

    estimatePending_ = false;
    freshSamples_ = 0;
    lastRunSamples_ = 0;

    bpmHistCount_ = 0;
    lastPeak_ = 0.0f;

    baseBpm_ = 0.0f;
    bpm_ = 0.0f;
    locked_ = false;
    confidence_ = 0.0f;

    period_ = 0;
    clockCounter_ = 0;
    clockHigh_ = false;
}

void BpmClock::RequestEstimate() {
    locked_ = false;
    estimatePending_ = true;
    freshSamples_ = 0;
    lastRunSamples_ = 0;
    bpmHistCount_ = 0;
    lastPeak_ = 0.0f;
    confidence_ = 0.0f;
}

void BpmClock::ProcessSample(float sample) {
    // Store in audio ring buffer
    audioBuffer_[audioWritePos_] = sample;
    audioWritePos_ = (audioWritePos_ + 1) & (AUDIO_BUF_SIZE - 1);

    // Decimation to 100 Hz
    decimCounter_++;
    if (decimCounter_ >= DECIM_FACTOR) {
        decimCounter_ = 0;
        frameWritePos_ = audioWritePos_;  // Snapshot position for main loop
        frameReady_ = true;
        if (estimatePending_) {
            freshSamples_++;
        }
    }

    // Clock generator
    uint32_t p = period_;
    if (p > 0) {
        clockCounter_++;
        if (clockHigh_) {
            if (clockCounter_ >= PULSE_WIDTH) {
                clockHigh_ = false;
            }
        }
        if (clockCounter_ >= p) {
            clockCounter_ = 0;
            clockHigh_ = true;
        }
    } else {
        clockHigh_ = false;
    }
}

void BpmClock::Update(float rate) {
    // Process new spectral frame if ready
    if (frameReady_) {
        frameReady_ = false;
        ProcessFrame();
    }

    // Progressive estimation: try every 0.5s of fresh data until locked
    if (estimatePending_ && !locked_) {
        int fresh = freshSamples_;
        if (fresh >= ESTIMATE_INTERVAL &&
            fresh >= lastRunSamples_ + ESTIMATE_INTERVAL) {
            lastRunSamples_ = fresh;
            RunEstimate(rate);
        }
        if (fresh >= FLUX_BUF_SIZE && !locked_) {
            // Out of data. Adopt the median of whatever was gathered rather
            // than emitting no clock at all — the confidence value says how
            // little this is worth. The old code locked on the first estimate,
            // so it always produced a clock when any estimate succeeded; not
            // regressing that matters more here than holding out for agreement.
            if (!TryLock(1) && bpmHistCount_ > 0) {
                baseBpm_ = MedianOf(bpmHist_, bpmHistCount_);
                confidence_ = 0.0f;
                locked_ = true;
            }
            if (locked_) {
                ApplyBpm(rate);
            }
            estimatePending_ = false;
        }
    }

    // Scale BPM and clock period to current rate
    if (baseBpm_ > 0.0f) {
        ApplyBpm(rate);
    }
}

void BpmClock::ApplyBpm(float rate) {
    bpm_ = baseBpm_ * rate;
    if (bpm_ <= 0.0f) {
        period_ = 0;
        return;
    }
    float periodF = 48000.0f * 60.0f / bpm_;
    period_ = (uint32_t)(periodF + 0.5f);
}

void BpmClock::ProcessFrame() {
    int wp = frameWritePos_;

    // Fill FFT buffer: windowed real samples as complex (imag = 0)
    for (int i = 0; i < FFT_SIZE; i++) {
        int idx = (wp - FFT_SIZE + i + AUDIO_BUF_SIZE) & (AUDIO_BUF_SIZE - 1);
        fftBuffer_[2 * i] = audioBuffer_[idx] * window_[i];
        fftBuffer_[2 * i + 1] = 0.0f;
    }

    // Complex FFT in-place
    fft_inplace(fftBuffer_, FFT_SIZE);

    // Compute magnitude squared for bins 1..NUM_MAG_BINS
    for (int k = 0; k < NUM_MAG_BINS; k++) {
        int bin = k + 1;
        float re = fftBuffer_[2 * bin];
        float im = fftBuffer_[2 * bin + 1];
        curMag_[k] = re * re + im * im;
    }

    // Compute spectral flux: sum of positive magnitude increases
    if (hasPrevMag_) {
        float flux = 0.0f;
        for (int i = 0; i < NUM_MAG_BINS; i++) {
            float diff = curMag_[i] - prevMag_[i];
            if (diff > 0.0f) {
                flux += diff;
            }
        }

        fluxBuffer_[fluxWritePos_] = flux;
        fluxWritePos_++;
        if (fluxWritePos_ >= FLUX_BUF_SIZE) {
            fluxWritePos_ = 0;
        }
    } else {
        hasPrevMag_ = true;
    }

    memcpy(prevMag_, curMag_, sizeof(curMag_));
}

// Insertion-sorted median of a short array. Copies, so the caller's order is
// preserved.
static float MedianOf(const float* src, int n) {
    float t[8];
    if (n > 8) n = 8;
    for (int i = 0; i < n; i++) t[i] = src[i];
    for (int i = 1; i < n; i++) {
        float v = t[i];
        int j = i - 1;
        while (j >= 0 && t[j] > v) { t[j + 1] = t[j]; j--; }
        t[j + 1] = v;
    }
    return (n & 1) ? t[n / 2] : 0.5f * (t[n / 2 - 1] + t[n / 2]);
}

void BpmClock::PushEstimate(float bpm, float peak) {
    if (bpmHistCount_ < BPM_HIST) {
        bpmHist_[bpmHistCount_++] = bpm;
    } else {
        for (int i = 1; i < BPM_HIST; i++) bpmHist_[i - 1] = bpmHist_[i];
        bpmHist_[BPM_HIST - 1] = bpm;
    }
    lastPeak_ = peak;
}

// Adopt the median of the collected estimates if at least minAgree of them sit
// within AGREE_TOL of it. Locking on the first successful estimate meant a
// single bad one stuck permanently, because nothing ever revisited it.
bool BpmClock::TryLock(int minAgree) {
    if (bpmHistCount_ < minAgree || bpmHistCount_ < 1) return false;

    float med = MedianOf(bpmHist_, bpmHistCount_);
    if (med <= 0.0f) return false;

    int agree = 0;
    for (int i = 0; i < bpmHistCount_; i++) {
        float d = bpmHist_[i] - med;
        if (d < 0.0f) d = -d;
        if (d <= AGREE_TOL * med) agree++;
    }
    if (agree < minAgree) return false;

    float q = lastPeak_;
    if (q < 0.0f) q = 0.0f;
    if (q > 1.0f) q = 1.0f;

    baseBpm_ = med;
    confidence_ = q * ((float)agree / (float)bpmHistCount_);
    locked_ = true;
    return true;
}

void BpmClock::RunEstimate(float rate) {
    int bufLen = freshSamples_;
    if (bufLen > FLUX_BUF_SIZE) bufLen = FLUX_BUF_SIZE;

    int maxLag = bufLen / 2;
    if (maxLag > MAX_LAGS) maxLag = MAX_LAGS;
    if (maxLag < MIN_LAG + 2) return;

    // Unwrap the ring into linear order once. Doing this here removes two
    // hardware divides from every iteration of the correlation below.
    for (int i = 0; i < bufLen; i++) {
        int idx = fluxWritePos_ - bufLen + i;
        if (idx < 0) idx += FLUX_BUF_SIZE;
        fluxLin_[i] = fluxBuffer_[idx];
    }

    float mean = 0.0f;
    for (int i = 0; i < bufLen; i++) mean += fluxLin_[i];
    mean /= (float)bufLen;

    // Remove the mean in place so the correlation is a plain dot product.
    float variance = 0.0f;
    for (int i = 0; i < bufLen; i++) {
        fluxLin_[i] -= mean;
        variance += fluxLin_[i] * fluxLin_[i];
    }
    variance /= (float)bufLen;
    if (variance < 1e-6f) return;

    const int numLags = maxLag - MIN_LAG + 1;
    float peak = 0.0f;

    for (int li = 0; li < numLags; li++) {
        int lag = MIN_LAG + li;
        int count = bufLen - lag;
        const float* a = fluxLin_;
        const float* b = fluxLin_ + lag;
        float sum = 0.0f;
        for (int i = 0; i < count; i++) sum += a[i] * b[i];
        float c = sum / ((float)count * variance);
        corr_[li] = c;
        if (c > peak) peak = c;
    }

    // Accept the first local maximum that is both above an absolute floor and a
    // decent fraction of the strongest peak present, so the bar rises with how
    // periodic the signal is instead of sitting at a fixed 0.2 where noise can
    // clear it.
    float threshold = PEAK_FRAC * peak;
    if (threshold < PEAK_FLOOR) threshold = PEAK_FLOOR;

    int bestLag = 0;
    float bestCorr = 0.0f;
    for (int li = 1; li < numLags - 1; li++) {
        if (corr_[li] > corr_[li - 1] && corr_[li] >= corr_[li + 1] &&
            corr_[li] > threshold) {
            bestLag = MIN_LAG + li;
            bestCorr = corr_[li];
            break;
        }
    }
    if (bestLag == 0) return;

    // Convert lag to BPM, normalize to 1x rate
    float beatPeriodSec = (float)bestLag / 100.0f;
    float rawBpm = 60.0f / beatPeriodSec;
    if (rate > 0.0f) rawBpm /= rate;

    // Choose the metrical level nearest a typical tempo rather than folding
    // blindly into the range, which accepted 41 and 199 BPM as readily as 120.
    // Comparing max(r, 1/r) orders candidates the same way |log(r)| would, and
    // costs no logarithm.
    static const float kOctaves[5] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
    float best = 0.0f;
    float bestScore = 1e30f;
    for (int i = 0; i < 5; i++) {
        float c = rawBpm * kOctaves[i];
        if (c < TEMPO_MIN || c > TEMPO_MAX) continue;
        float r = c / TEMPO_CENTRE;
        float s = (r > 1.0f) ? r : (1.0f / r);
        if (s < bestScore) { bestScore = s; best = c; }
    }
    if (best <= 0.0f) {
        // Nothing landed in range (very fast or very slow source): fall back to
        // the plain fold so an estimate still exists.
        best = rawBpm;
        while (best > TEMPO_MAX) best *= 0.5f;
        while (best < TEMPO_MIN) best *= 2.0f;
    }

    PushEstimate(best, bestCorr);

    if (TryLock(LOCK_AGREE)) {
        estimatePending_ = false;
        ApplyBpm(rate);
    }
}
