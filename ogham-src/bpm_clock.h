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

#pragma once
#include <cstdint>

class BpmClock {
public:
    void Init();

    // Called per sample from audio ISR — buffer audio + clock tick
    void ProcessSample(float sample);

    // Call when formula, A, or B changes. Starts a new estimation cycle.
    void RequestEstimate();

    // Call from main loop every iteration. Runs FFT frames + estimation + rate scaling.
    void Update(float rate);

    // For gate output (read from ISR or main loop)
    bool GetClockState() const { return clockHigh_; }

    // Estimate and how much to trust it. Confidence is 0..1: the height of the
    // autocorrelation peak times the proportion of recent estimates that agreed
    // with the one adopted. Zero until locked.
    float GetBpm() const { return bpm_; }
    float GetBaseBpm() const { return baseBpm_; }
    bool  IsLocked() const { return locked_; }
    float GetConfidence() const { return confidence_; }

private:
    // FFT configuration.
    //
    // FFT_SIZE must be >= DECIM_FACTOR or the analysis misses audio: frames are
    // taken every DECIM_FACTOR samples and each covers only the FFT_SIZE samples
    // behind the boundary. At 256/480 that left a 224-sample hole in every 480,
    // so 47% of the signal never reached the onset detector — and which onsets
    // fell in the hole depended on the formula's phase against the decimation
    // grid, making it unstable as well as lossy. 512 covers 480 with a little
    // overlap.
    static constexpr int FFT_SIZE = 512;
    static constexpr int AUDIO_BUF_SIZE = 4096;  // Power of 2 for masking
    static constexpr int NUM_MAG_BINS = FFT_SIZE / 2 - 1;

    // Decimation to 100 Hz (every 480 samples at 48kHz)
    static constexpr uint32_t DECIM_FACTOR = 480;

    // Spectral flux buffer (4 seconds at 100 Hz)
    static constexpr int FLUX_BUF_SIZE = 400;

    // Clock pulse width
    static constexpr uint32_t PULSE_WIDTH = 480;  // 10ms at 48kHz

    // Autocorrelation limits. MIN_LAG 30 = 300ms = 200 BPM; nothing faster is a
    // beat, and allowing shorter lags let noise win the first-peak test.
    static constexpr int MIN_LAG = 30;
    static constexpr int MAX_LAGS = FLUX_BUF_SIZE / 2;
    static constexpr int ESTIMATE_INTERVAL = 50;  // Retry every 0.5s of fresh data

    // Estimates are collected and only adopted once several agree.
    static constexpr int BPM_HIST = 5;
    static constexpr int LOCK_AGREE = 3;
    static constexpr float AGREE_TOL = 0.03f;   // +-3% counts as the same tempo

    // Peak acceptance: absolute floor, and a fraction of the strongest peak in
    // the correlation so the bar rises with how periodic the signal actually is.
    static constexpr float PEAK_FLOOR = 0.2f;
    static constexpr float PEAK_FRAC = 0.4f;

    // Perceptual centre for choosing the metrical level.
    static constexpr float TEMPO_CENTRE = 120.0f;
    static constexpr float TEMPO_MIN = 40.0f;
    static constexpr float TEMPO_MAX = 200.0f;

    // Audio ring buffer (ISR writes, main reads).
    //
    // The main loop reads the FFT_SIZE samples behind frameWritePos_ while the
    // ISR keeps writing, so the margin before the ISR laps the region being read
    // is (AUDIO_BUF_SIZE - FFT_SIZE) samples — 74ms at 4096/512. The main loop
    // blocks for ~9ms on each TM1637 write, so the old 1024-sample buffer left
    // only 16ms and could tear a frame.
    float audioBuffer_[AUDIO_BUF_SIZE];
    volatile int audioWritePos_;
    volatile int frameWritePos_;  // Snapshot at decimation boundary

    // Decimation
    uint32_t decimCounter_;
    volatile bool frameReady_;

    // Analysis window, built once (periodic Hann, the correct one for overlap
    // analysis). Computing this with cosf per sample per frame cost 25,600
    // transcendental calls a second for a constant.
    float window_[FFT_SIZE];

    // FFT working buffer: FFT_SIZE complex values, interleaved [re, im, ...]
    float fftBuffer_[FFT_SIZE * 2];

    // Magnitude spectra for spectral flux
    float curMag_[NUM_MAG_BINS];
    float prevMag_[NUM_MAG_BINS];
    bool hasPrevMag_;

    // Spectral flux ring buffer
    float fluxBuffer_[FLUX_BUF_SIZE];
    int fluxWritePos_;

    // Flux unwrapped into linear order, mean already removed, so the
    // correlation's inner loop is a plain dot product. Indexing the ring
    // directly cost two hardware divides per iteration — about 113,000 UDIV per
    // estimate for work whose arithmetic is one multiply-accumulate.
    float fluxLin_[FLUX_BUF_SIZE];
    float corr_[MAX_LAGS];

    // Estimation state
    bool estimatePending_;
    volatile int freshSamples_;
    int lastRunSamples_;

    // Recent estimates, so a lock is earned by agreement rather than won by
    // whichever estimate happened to come first.
    float bpmHist_[BPM_HIST];
    int   bpmHistCount_;
    float lastPeak_;

    // BPM result
    float baseBpm_;
    float bpm_;
    bool locked_;
    float confidence_;

    // Clock generator (free-running)
    volatile uint32_t period_;
    uint32_t clockCounter_;
    bool clockHigh_;

    void ProcessFrame();
    void RunEstimate(float rate);
    void PushEstimate(float bpm, float peak);
    bool TryLock(int minAgree);
    void ApplyBpm(float rate);
};
