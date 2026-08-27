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

#include "ogham_cv_output.h"
#include <cmath>

void CvOutput::Init(daisy::DacHandle* dac) {
    dac_ = dac;
    dc_ = dc2_ = 0.0f;
    env_ = env2_ = 0.0f;
    envS_ = env2S_ = 0.0f;
    heldRaw1_ = heldRaw2_ = 0.0f;
    slewOut_ = 0.0f;
    cvOut_ = 0.0f;
    dcMean_ = 0.5f;
}

// Slew time constant in OUTPUT SAMPLES for a 0..99 value. Exponential,
// CV_SLEW_MIN_S..CV_SLEW_MAX_S; 0 = off (instant, bit-exact passthrough).
// One mapping for every CV Out mode -- a slew setting means the same number
// of milliseconds whatever the engine is doing (daisy-*).
float CvOutput::SlewTauSamples(uint8_t v) {
    if (v == 0) return 0.0f;
    const float norm  = (float)v / 99.0f;
    const float timeS = CV_SLEW_MIN_S * powf(CV_SLEW_MAX_S / CV_SLEW_MIN_S, norm);
    return timeS * 48000.0f;
}

float CvOutput::SlewCoeffFromValue(uint8_t v) {
    float tau = SlewTauSamples(v);
    if (tau <= 0.0f) return 1.0f;               // off: instant
    if (tau < 1.0f)  tau = 1.0f;
    return 1.0f - expf(-1.0f / tau);
}

void CvOutput::SetSlewRise(uint8_t v) {
    if (v == slewRiseVal_) return;              // called every main loop
    slewRiseVal_ = v;
    slewCoeffRise_ = SlewCoeffFromValue(v);
    RecomputeDcMakeup();
}

void CvOutput::SetSlewFall(uint8_t v) {
    if (v == slewFallVal_) return;
    slewFallVal_ = v;
    slewCoeffFall_ = SlewCoeffFromValue(v);
    RecomputeDcMakeup();
}

void CvOutput::SetCaptureInterval(float s1, float s2) {
    // Only recompute on a meaningful change -- the Rate knob dithers by tiny
    // amounts every ADC read and this does expf/sqrtf.
    if (fabsf(s1 - captureSamples1_) < 0.01f * captureSamples1_ &&
        fabsf(s2 - captureSamples2_) < 0.01f * captureSamples2_) return;
    captureSamples1_ = (s1 > 1.0f) ? s1 : 1.0f;
    captureSamples2_ = (s2 > 1.0f) ? s2 : 1.0f;
    RecomputeDcMakeup();
}

// The slew itself is plain absolute time and needs nothing per-mode. What the
// DC modes DO need is the level compensation, because a one-pole on a
// near-uncorrelated capture sequence can only shrink it toward the mean --
// sd scales by sqrt((1-a)/(1+a)) for pole a. Both terms of a = exp(-K/tau)
// are known, so the correction is exact for any tau; it does not have to be,
// and no longer is, a constant.
//
// Amplitude still falls away once tau greatly exceeds the capture interval,
// and no gain can invent modulation that the filter removed. That is honest:
// a 5 s slew on a 2 kHz LFO really is nearly DC. Predictable behaviour was
// worth more here than a clever mapping -- an earlier version scaled tau to
// the capture interval to hold the depth constant, which made the rise/fall
// time change with playback speed and was much harder to dial in.
void CvOutput::RecomputeDcMakeup() {
    const float K      = captureSamples1_;      // voice 1 sets the CV timebase
    const float tauR   = SlewTauSamples(slewRiseVal_);
    const float tauF   = SlewTauSamples(slewFallVal_);
    // Rise and fall generally differ; their mean is the right first-order
    // answer for a signal spending roughly equal time going up and down.
    const float tauAvg = 0.5f * (tauR + tauF);

    if (tauAvg <= 0.0f) {
        slewMakeup_ = 1.0f;
    } else {
        const float a     = expf(-K / tauAvg);  // the pole actually delivered
        const float atten = sqrtf((1.0f - a) / (1.0f + a));
        float g = (atten > 1.0e-4f) ? (SLEW_MAKEUP_TRIM / atten) : SLEW_MAKEUP_MAX;
        if (g < 1.0f) g = 1.0f;                 // never attenuate further
        if (g > SLEW_MAKEUP_MAX) g = SLEW_MAKEUP_MAX;
        slewMakeup_ = g;
    }

    // Centre the gain expands about. MUST stay well slower than the slew --
    // the output is mean + gain*(slew - mean), so anything the mean tracks is
    // added straight back at full speed. Tying it to the capture interval was
    // wrong: with Hold off that made it ~100x FASTER than the slew, which read
    // as "smooth locally, jagged overall". 8*tau, absolutely bounded.
    float meanSamples = 8.0f * tauAvg;
    if (meanSamples < DC_MEAN_MIN_S * 48000.0f) meanSamples = DC_MEAN_MIN_S * 48000.0f;
    if (meanSamples > DC_MEAN_MAX_S * 48000.0f) meanSamples = DC_MEAN_MAX_S * 48000.0f;
    dcMeanCoef_ = 1.0f / meanSamples;
}

// Only the on/off decision -- the window size and its tick counter live in
// AudioPipeline (see the header). Safe to call every main-loop iteration.
void CvOutput::SetHold(uint8_t v) { holdOn_ = (v != 0); }

// One envelope-follower step (DC-block -> full-wave rectify -> attack/release).
static inline void EnvStep(float in, float& dc, float& env,
                           float dcC, float atkC, float relC) {
    dc += dcC * (in - dc);
    float rect = fabsf(in - dc);
    if (rect > env) env += atkC * (rect - env);
    else            env += relC * (rect - env);
}

// One Hold step for a DC-mode voice (daisy-*). Off: track the interpolated
// stream every sample. On: latch `holdSamp` whenever the pipeline says to
// capture, and sit on it in between. Both the decimation counting and the
// choice of when to capture (an odd phase of the window, never the degenerate
// zero phase) are decided upstream, which keeps this a plain latch.
static inline void HoldStep(float raw, float holdSamp, bool capture,
                            bool holdOn, float& held) {
    if (!holdOn)      held = raw;        // off: track every sample
    else if (capture) held = holdSamp;   // on: latch, then hold until next capture
}

void CvOutput::ProcessSample(float out1Proc, float out2Proc, float raw1, float raw2,
                             float holdSamp1, float holdSamp2, bool cap1, bool cap2) {
    // Two envelope followers (kept running in every mode so switching is instant),
    // each with an extra one-pole post-smoother for a gentler CV.
    EnvStep(out1Proc, dc_,  env_,  DC_COEFF, ATTACK_COEFF, RELEASE_COEFF);
    EnvStep(out2Proc, dc2_, env2_, DC_COEFF, ATTACK_COEFF, RELEASE_COEFF);
    envS_  += ENV_SMOOTH_COEFF * (env_  - envS_);
    env2S_ += ENV_SMOOTH_COEFF * (env2_ - env2S_);

    // DC-mode Hold (kept running in every mode too, for the same reason).
    HoldStep(raw1, holdSamp1, cap1, holdOn_, heldRaw1_);
    HoldStep(raw2, holdSamp2, cap2, holdOn_, heldRaw2_);

    // Pick this sample's pre-slew target, in 0..1 DAC-drive units, for whichever
    // mode is current, then run it through the ONE shared Slew stage (daisy-*).
    // Switching modes with Slew engaged glides to the new target rather than
    // clicking -- the same behaviour as any other change while Slew is up.
    const bool dcMode = (mode_ == Mode::DcOut1 || mode_ == Mode::DcOut2);
    float target;
    if (!dcMode) {
        float env = (mode_ == Mode::EnvOut2) ? env2S_ : envS_;
        float v = env * ENV_GAIN;       // ~0..5 V envelope
        if (v < 0.0f) v = 0.0f;
        if (v > 5.0f) v = 5.0f;
        target = v / 5.0f;
    } else {
        float raw = (mode_ == Mode::DcOut2) ? heldRaw2_ : heldRaw1_;
        target = raw * 0.5f + 0.5f;     // -1..1 -> 0..1
        // Slow centre for the make-up gain to work about (daisy-*).
        dcMean_ += dcMeanCoef_ * (target - dcMean_);
    }
    // Independent rise/fall (daisy-*): pick the coefficient by direction. Both
    // are plain absolute times and shared by every mode, so a given Slew
    // setting takes the same wall-clock time to move whatever the engine or
    // the Rate knob is doing.
    float delta = target - slewOut_;
    slewOut_ += ((delta >= 0.0f) ? slewCoeffRise_ : slewCoeffFall_) * delta;

    // Make-up gain, DC modes only: restore the swing the one-pole removed,
    // expanding about the running centre so a skewed formula doesn't get
    // pushed bodily into one rail.
    if (dcMode && slewMakeup_ > 1.0f) {
        cvOut_ = dcMean_ + slewMakeup_ * (slewOut_ - dcMean_);
    } else {
        cvOut_ = slewOut_;
    }
}

void CvOutput::UpdateOutput() {
    if (!dac_) return;

    float u = cvOut_;
    if (u < 0.0f) u = 0.0f;
    if (u > 1.0f) u = 1.0f;

    uint16_t out = (uint16_t)(u * 4095.0f + 0.5f);
    if (out > 4095) out = 4095;
    dac_->WriteValue(daisy::DacHandle::Channel::ONE, out);
}
