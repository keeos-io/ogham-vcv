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
#include "daisy_seed.h"

// CV Out on the Daisy internal DAC channel ONE (-> TL072 gain stage -> CV-out jack).
// Mode-selectable (daisy-0pq):
//   EnvOut1 - amplitude envelope follower of Out1 (DC-block -> rectify -> A/R smooth)
//   EnvOut2 - amplitude envelope follower of Out2
//   DcOut1  - raw Out1 formula sample as a DC voltage (bytebeat-as-CV/LFO)
//   DcOut2  - raw Out2 formula sample as a DC voltage
// The DC modes take the pre-Lo-Fi/FX voice sample so the CV shape is the bytebeat
// itself, and can hold slow/DC values the AC-coupled audio outs cannot.
//
// Hold (DC modes only) feeds a decimation stage before Slew, capturing on an
// odd phase of each window rather than on its degenerate zero phase;
// Slew (daisy-*) is the LAST stage before the DAC regardless of mode, so it
// shapes whichever output is selected -- an envelope follower or a held/raw
// DC voltage. Run the engine at LFO-rate (see Controls::MapKnobToRate) with
// Hold+Slew on the DC modes for anything from a classic stepped S&H voltage
// to a smoothly gliding LFO, or turn Slew up on an envelope-follower mode to
// soften its output. Both default to off (bit-exact passthrough, matching
// pre-daisy-* behaviour).
class CvOutput {
public:
    enum class Mode : uint8_t { EnvOut1 = 0, EnvOut2 = 1, DcOut1 = 2, DcOut2 = 3 };

    void Init(daisy::DacHandle* dac);
    void SetMode(Mode m) { mode_ = m; }

    // CV Out slew, shared by every mode (daisy-*): one-pole smoothing on
    // whatever's currently selected, applied asymmetrically depending on
    // whether the target is above (Rise) or below (Fall) the current output --
    // so e.g. a fast pluck-like rise can pair with a slow decaying fall.
    // 0 = off (instant).
    //
    // ONE mapping for every mode: a plain absolute time constant,
    // CV_SLEW_MIN_S..CV_SLEW_MAX_S. A given Slew setting takes the same
    // wall-clock time to move whatever the engine, the Rate knob or the hold
    // window are doing -- which is the whole point of a slew limiter and what
    // makes it dial-in-able.
    //
    // (An earlier version scaled tau to the capture interval for the DC modes,
    // to hold modulation depth constant across Rate. It did that, but it also
    // made the rise/fall time change with playback speed -- at high Rate tau
    // fell to a fraction of a millisecond and the slew stopped doing anything
    // perceptible. Depth is now handled where it belongs, by the make-up gain
    // below, which no longer requires tau to be anything in particular.)
    void SetSlewRise(uint8_t v);
    void SetSlewFall(uint8_t v);

    // Capture interval in output samples, per voice, from AudioPipeline.
    // Safe to call every main-loop iteration; only recomputes on a real change.
    void SetCaptureInterval(float samples1, float samples2);

    // CV Out hold on/off (DC modes only), applied before Slew. 0 = off (track
    // the interpolated stream every sample, i.e. today's behaviour); non-zero
    // = hold, updating only when AudioPipeline says to capture.
    //
    // Note this takes only the on/off decision: the decimation WINDOW and its
    // tick counter live in AudioPipeline, which raises an already-decimated
    // capture flag (GetCvCaptureBuffer). That's deliberate -- the capture phase
    // is anchored to the engine's t, so the pipeline is the only place that can
    // decide when a capture happens. One authority, and no phase drift.
    // (Two filters were tried before this and dropped -- a full-window boxcar
    // average and a 16-tap Gaussian in t. Both were linear low-passes on a
    // near-white sequence, so both only shrank the output.)
    void SetHold(uint8_t v);

    // Process one audio sample (called from the audio ISR, 48 kHz). out1Proc/
    // out2Proc feed the two envelope followers; raw1/raw2 are the pre-Lo-Fi
    // voices (linearly interpolated) for DC modes when Hold is off;
    // holdSamp1/holdSamp2 are what Hold captures (daisy-*) --
    // AudioPipeline::GetHoldSampleBuffer, either the fresh un-interpolated
    // formula value at the captured (offset-phase) t, since the
    // interpolated stream is still close to the previous value right when a
    // tick lands. cap1/cap2 are AudioPipeline::GetCvCaptureBuffer: true on the
    // samples Hold should latch a new value (already decimated by the window).
    void ProcessSample(float out1Proc, float out2Proc, float raw1, float raw2,
                       float holdSamp1, float holdSamp2, bool cap1, bool cap2);

    // Write the CV (called from the main loop)
    void UpdateOutput();

    float GetEnv() const { return envS_; }  // 0..~1, for telemetry/tuning

private:
    daisy::DacHandle* dac_ = nullptr;
    Mode  mode_ = Mode::EnvOut1;

    // Two envelope followers (Out1, Out2): each DC-block -> rectify -> attack/release,
    // then an extra one-pole smoother (envS_/env2S_) for a gentler CV.
    float dc_    = 0.0f, dc2_   = 0.0f;   // slow DC estimates (DC blockers)
    float env_   = 0.0f, env2_  = 0.0f;   // rectified A/R envelopes (0..~1)
    float envS_  = 0.0f, env2S_ = 0.0f;   // post-smoothed envelopes (output)

    // DC-mode Hold (daisy-*), one held value per voice so switching
    // DcOut1 <-> DcOut2 is instant (both always run, like the env followers
    // above). The window and its tick counter live in AudioPipeline; all this
    // keeps is whether Hold is engaged and the currently-held value.
    bool  holdOn_   = false;
    float heldRaw1_ = 0.0f, heldRaw2_ = 0.0f;  // held raw output (-1..1)

    // Shared Slew (daisy-*): ONE stage, applied in ProcessSample() to whichever
    // signal `mode_` currently selects (already scaled to the 0..1 DAC-drive
    // range), so it's the same physical filter regardless of mode -- switching
    // modes with Slew turned up glides to the new mode's value instead of
    // clicking, the same as any other change while it's engaged. Independent
    // rise/fall coefficients (daisy-*): whichever applies is picked each
    // sample by the sign of (target - slewOut_).
    // Absolute-time coefficients, shared by every mode. 1.0 = off (instant).
    float slewCoeffRise_ = 1.0f;
    float slewCoeffFall_ = 1.0f;
    // DC-mode level compensation. A one-pole on a near-uncorrelated capture
    // sequence can only shrink it toward the mean -- sd scales by
    // sqrt((1-a)/(1+a)) for the delivered pole a = exp(-K/tau). Both terms are
    // known, so this is exact for any tau; see RecomputeDcMakeup.
    float slewMakeup_ = 1.0f;
    float dcMean_     = 0.5f;   // slow centre the gain expands about
    float dcMeanCoef_ = 0.001f; // EMA coefficient, targeted at 8*tau
    uint8_t slewRiseVal_ = 0, slewFallVal_ = 0;
    float captureSamples1_ = 1.0f, captureSamples2_ = 1.0f;

    float slewOut_   = 0.0f;  // post-slew, pre-make-up
    float cvOut_     = 0.0f;  // post-make-up; UpdateOutput() just clamps + writes

    static float SlewTauSamples(uint8_t v);       // 0..99 -> tau in output samples
    static float SlewCoeffFromValue(uint8_t v);   // 0..99 -> one-pole coefficient
    void  RecomputeDcMakeup();                    // DC-mode level compensation

    // Tuning (one-pole coeffs at 48 kHz). c -> time constant ~1/(c*48000) s.
    static constexpr float DC_COEFF      = 0.0008f;  // ~6 Hz DC blocker
    static constexpr float ATTACK_COEFF  = 0.0154f;  // ~1.4 ms rise
    static constexpr float RELEASE_COEFF = 0.000615f; // ~34 ms fall
    static constexpr float ENV_SMOOTH_COEFF = 0.0020f; // ~10 ms post-smoothing (extra)
    static constexpr float ENV_GAIN      = 6.0f;     // env(0..1) -> 0..5 V (clamped)

    // Slew mapping: exponential absolute time, v=1 .. v=99. All modes.
    static constexpr float CV_SLEW_MIN_S = 0.003f;   // ~3 ms (barely perceptible)
    static constexpr float CV_SLEW_MAX_S = 5.0f;     // ~5 s (slow LFO-style glide)

    // Measured: the theoretical make-up over-corrects because captures retain
    // some correlation. 0.85 keeps median clipping at zero across all formulas.
    static constexpr float SLEW_MAKEUP_TRIM = 0.85f;
    // 6.0: with the absolute term engaged the delivered pole sits close to 1
    // (heavy smoothing of a fast signal), so there is a lot of lost amplitude
    // to restore. Past roughly 6*K of smoothing no clamp really helps -- there
    // is little modulation left to recover, and the output is legitimately a
    // small, very smooth voltage.
    static constexpr float SLEW_MAKEUP_MAX  = 6.0f;
    // The centre the make-up gain expands about. Must stay well SLOWER than the
    // slew (it is targeted at 8*tau) or it re-injects the motion the slew just
    // removed -- see RecomputeDcMakeup. These bound that in absolute terms.
    static constexpr float DC_MEAN_MIN_S = 0.5f;    // never faster than 0.5 s
    static constexpr float DC_MEAN_MAX_S = 20.0f;   // nor slower than 20 s
};
