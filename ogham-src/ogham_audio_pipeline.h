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
#include "bytebeat_engine.h"
#include "Effects/chorus.h"
#include "Effects/flanger.h"
#include "Effects/phaser.h"
#include "Utility/delayline.h"

// FX chain config (Func long-press -> FX editor). A fixed 3-stage chain
// (Chorus -> Flanger -> Phaser); each stage has a mix (0 = bypass) and two
// flavour params. `parallel` swaps the topology (stages in parallel vs series).
// All values are 0..99 (displayed as 2 digits; mapped to engine ranges in
// AudioPipeline::SetFxChain). Shared across both voices; each voice keeps its
// own engine state. The field index order here MUST match the menu in
// ogham_main.cpp and the labels in Display::ShowFxEdit.
struct FxChainConfig {
    uint8_t enabled;    // global FX on/off (1 = on, 0 = whole chain bypassed)
    // Per stage: level (mix), type (variant 0/1), param1, param2.
    uint8_t chorusLevel,  chorusType,  chorusP1,  chorusP2;
    uint8_t flangerLevel, flangerType, flangerP1, flangerP2;
    uint8_t phaserLevel,  phaserType,  phaserP1,  phaserP2;
    uint8_t parallel;   // 0 = serial, 1 = parallel
    uint8_t paramQuant; // A/B interpolation grid step (engine setting; 0 = off)
    uint8_t out2Drone;  // Out2 decouple/drone (daisy-pcq): 1 = frozen independent voice
    uint8_t cvOutMode;  // CV-out mode (daisy-0pq): 0 = env follower, 1 = DC Out1, 2 = DC Out2
    uint8_t timbreCvRoute; // CV->Timbre routing (daisy-gtw): 0 = normal, 1 = CV A, 2 = CV B
    // Internal LPG (daisy-nmr), on/off + decay consolidated into one field
    // (daisy-*): 0 = off (bypassed), 1..99 = on (Sync in plucks it) with that
    // decay, exp-mapped 2ms..20s (see LPG_DECAY_MIN/MID/MAX_S) -- the same
    // curve as when this was two fields, just shifted so 0 means off instead
    // of a 2ms decay.
    uint8_t lpgDecay;
    // CV Out slew + hold (daisy-*): a sample-and-hold decimation stage (cvHold,
    // DC modes only, capturing on an odd PHASE of each window) followed by a
    // one-pole smoother (cvSlewRise/cvSlewFall, all CV Out modes -- the last
    // stage before the DAC regardless of mode). A very slow Rate plus slew/hold
    // gives a smoothly gliding beat-locked LFO, and the same slew can also just
    // soften an envelope-follower output. Rise and fall are independent, so a
    // fast rise can pair with a slow decaying fall. Slew is a plain absolute
    // time in every mode; the DC modes add a make-up gain (CvOutput).
    uint8_t cvSlewRise; // 0 = off (instant)
    uint8_t cvSlewFall; // same mapping, applied when the target is below the current output
    uint8_t cvHold;     // 0 = off (every tick); else a power-of-2 hold window, 2..256; DC modes only
    // Was the V/oct start offset (menu field 22), withdrawn 2026-08-11 -- the
    // silent-opening problem it addressed is documented in the manual instead,
    // where it costs no menu field and no flash. Kept as a reserved byte so the
    // persisted layout is unchanged: removing it would bump SETTINGS_VERSION and
    // wipe everyone's settings to take a feature away.
    uint8_t reserved0;
};
// FX menu fields, in menu order: [0] global on/off, [1] chain toggle,
// [2..13] = 3 stages x 4 sub-params (level/type/p1/p2), [14] = CV-out mode,
// [15] = CV Out slew rise, [16] = CV Out slew fall, [17] = CV Out hold, [18] =
// LPG (consolidated on/off + decay), [19] = CV->Timbre routing, [20] =
// param-interp grid (q), [21] = Out2 decouple/drone.
// Stage param = 2 + stage*4 + sub.
static constexpr int FX_NUM_FIELDS   = 22;
static constexpr int FX_FIELD_GLOBAL = 0;   // the global on/off
static constexpr int FX_FIELD_CHAIN  = 1;   // the serial/parallel toggle
static constexpr int FX_FIELD_CVOUT  = 14;  // CV-out mode (env / DC Out1 / DC Out2)
static constexpr int FX_FIELD_CVSLEWRISE = 15; // CV Out slew, rising (all modes)
static constexpr int FX_FIELD_CVSLEWFALL = 16; // CV Out slew, falling (all modes)
static constexpr int FX_FIELD_CVHOLD   = 17; // CV Out sample-and-hold (DC modes only)
static constexpr int FX_FIELD_LPG      = 18; // internal LPG, consolidated on/off + decay
static constexpr int FX_FIELD_TIMBRECV = 19; // CV->Timbre routing (normal / CV A / CV B)
static constexpr int FX_FIELD_QUANT  = 20;  // the A/B param-interp grid (q)
static constexpr int FX_FIELD_DRONE  = 21;  // Out2 decouple/drone toggle
static constexpr int FX_TYPE_MAX     = 1;   // 0 = clean, 1 = characterful variant

// Audio pipeline: takes the two engine voices and applies the same lo-fi tone
// macro (Pot 4) to each, then writes them to Out1 / Out2. The lo-fi coefficients
// are shared (one knob), but each output keeps its own filter state so the two
// independent voices are processed identically and without cross-talk.
//
// Signal flow per voice: engine -> lo-fi macro -> FX chain (post-Lo-Fi) ->
//                        LPG -> out.
// The FX stage treats Out1/Out2 as a stereo pair (per-voice engines).
class AudioPipeline {
public:
    void Init();

    // Process a block of audio (called from the audio ISR).
    // out: 2 output channels (Out1 = L, Out2 = R), size samples each.
    void Process(BytebeatEngine& engine, float** out, size_t size);

    // Bipolar lo-fi macro from a 0..1 pot reading.
    //   center (12 o'clock) = clean; CCW = LPF -> drive/wavefold/sat;
    //   CW = HPF -> sample-rate reduction/sat.
    void SetLofiMacro(float pot);

    // True when the lo-fi macro is in its clean center deadzone (no processing).
    bool IsLofiClean() const { return lofiClean_; }

    // Apply an FX-chain config: stores the wet/dry mixes + topology and maps
    // the 0..99 flavour params onto both voices' engines. Call on any edit.
    void SetFxChain(const FxChainConfig& c);

    // The module's default FX chain (used before settings are loaded).
    static FxChainConfig DefaultFxChain();

    // --- Internal LPG (daisy-nmr) ---
    // Pluck the low-pass gate: sharp attack, then the configured exponential
    // decay. Called from the Sync-in EXTI ISR -- it only raises a flag, which
    // Process() consumes at the start of the next SAMPLE (not the next block),
    // so the pluck lands within one sample of the gate edge.
    void LpgTrigger() { lpgTrigPending_ = true; }

    // Pre-lo-fi clean voice buffers (feed CV / BPM analysis). Out1 -> BPM + CV;
    // Out2 -> CV DC-out mode (daisy-0pq). Both are raw formula output (pre Lo-Fi/FX).
    const float* GetCleanBuffer() const { return cleanBuffer_; }
    const float* GetCleanBuffer2() const { return cleanBuffer2_; }

    // Per-sample "CV Out Hold should capture now" flags (daisy-*), mirroring
    // the clean buffers above. ALREADY DECIMATED -- true only where t hits the
    // offset phase of the window. The window lives in this class because that
    // phase is anchored to the engine's t; CvOutput just consumes the flag, so
    // there is one authority on capture timing. Buffered per-sample because
    // BytebeatEngine's tick state is only valid for the LAST sample of a
    // Process() block by the time the audio callback's own loop runs over it.
    const bool* GetCvCaptureBuffer() const { return cvCaptureBuffer_; }
    const bool* GetCvCaptureBuffer2() const { return cvCaptureBuffer2_; }

    // Per-sample value for CV Out's Hold stage to capture, -1..1 (daisy-*).
    // NOT the clean buffers above -- those are linearly interpolated toward
    // the next t, so at the instant a tick lands they're still near the
    // PREVIOUS value. This carries either:
    //   - the engine's raw (un-interpolated) value at t, or
    // the engine's fresh un-interpolated value at the captured t (the capture
    // phase is offset, so this is not the degenerate t == 0 phase).
    const float* GetHoldSampleBuffer() const { return holdSampleBuffer_; }
    const float* GetHoldSampleBuffer2() const { return holdSampleBuffer2_; }

    // Pick a new random ODD capture phase for CV Out's Hold stage (daisy-*).
    // Call when the Rate knob moves: it then stays put until the knob moves
    // again, so nudging Rate re-rolls the LFO's character even if you land back
    // on the same frequency.
    //
    // This selects WHICH PHASE of each hold window the capture lands on. It is
    // NOT a look-ahead, so it costs no latency at any value and the CV stays
    // aligned with the audio. (It was a look-ahead once, which put the CV up to
    // a whole window ahead of the audio -- 2 s at the slowest Rate.)
    //
    // Which phase you sit on decides how the capture lines up with the
    // formula's own periodicity, and that changes how VARIED the CV is far more
    // than it changes its level, with no way to pick well in advance -- hence a
    // re-roll gesture rather than a tuned constant.
    //
    // Odd: a phase's factors of two divide the reachable output values
    // (256 >> v2) and phase 0 is the degenerate case that collapses the capture
    // to a constant. Measured over random formula/A/B/phase draws, odd gives a
    // flat CV 3.1% of the time (vs 27% for the phase-0 bug this replaced);
    // curating to the best-measuring phases only reached 2.75%, not worth a
    // table when a dull roll is one nudge from being replaced.
    //
    // `entropy` should be unpredictable at the moment of the call (the
    // microsecond timer); it is mixed into the generator state.
    void RerollCvSampleOffset(uint32_t entropy);

    // Capture interval in 48 kHz output samples, per voice (daisy-*). CvOutput
    // scales its DC-mode slew against this instead of against absolute seconds,
    // which is what makes the LFO depth independent of the Rate knob. Refreshed
    // every block from the engine's phase increment.
    float GetCaptureSamples()  const { return cvCaptureSamples_; }
    float GetCaptureSamples2() const { return cvCaptureSamples2_; }

private:
    static constexpr size_t MAX_BLOCK_SIZE = 256;
    float cleanBuffer_[MAX_BLOCK_SIZE] = {};
    float cleanBuffer2_[MAX_BLOCK_SIZE] = {};
    bool  cvCaptureBuffer_[MAX_BLOCK_SIZE] = {};
    bool  cvCaptureBuffer2_[MAX_BLOCK_SIZE] = {};
    float holdSampleBuffer_[MAX_BLOCK_SIZE] = {};
    float holdSampleBuffer2_[MAX_BLOCK_SIZE] = {};
    // Current ODD capture phase, re-rolled by RerollCvSampleOffset() whenever
    // the Rate knob moves. Starts at a known-good fixed value so behaviour is
    // deterministic until the user first touches the knob.
    int32_t  cvSampleOffset_ = 7;
    uint32_t cvRngState_ = 0x9E3779B9u;   // xorshift32; entropy mixed in per roll
    // CV Out Hold window, owned here because the capture phase is anchored to
    // the engine's t. 1 = off (capture every tick), else the 2..256 window.
    // Always a power of two so the phase test is a mask, not a division.
    int   cvHoldTicks_  = 1;
    float cvCaptureSamples_ = 1.0f, cvCaptureSamples2_ = 1.0f;
    // Last captured hold-source value per voice, carried across the samples
    // between captures (and across blocks).
    float holdSample1_ = 0.0f, holdSample2_ = 0.0f;

    float Wavefold(float x);

    // Per-output lo-fi filter state (one set per voice; identical processing)
    struct LofiState {
        float lpState   = 0.0f;
        float lpState2  = 0.0f;
        float hpLpState = 0.0f;
        float srrHeld   = 0.0f;
        int   srrCounter = 0;
        float svfLp     = 0.0f;  // state-variable filter low-pass state
        float svfBp     = 0.0f;  // state-variable filter band-pass state
        float lpgLp1    = 0.0f;  // LPG 2-pole low-pass, stage 1
        float lpgLp2    = 0.0f;  // LPG 2-pole low-pass, stage 2
    };
    float ProcessLofi(float v, LofiState& s);

    // Post-Lo-Fi FX chain (stereo pair: l=Out1, r=Out2). Runs all three stages
    // (serial or parallel) using chorusN_/flangerN_/phaserN_ + the stored mixes.
    void ProcessFx(float& l, float& r);

    LofiState lofi1_;  // Out1
    LofiState lofi2_;  // Out2

    // FX chain state. Mixes are 0..1 (from the 0..99 config); topology flag.
    float chorusMixF_  = 0.0f;
    float flangerMixF_ = 0.0f;
    float phaserMixF_  = 0.0f;
    bool  fxParallel_  = false;

    bool fxEnabled_ = true; // global FX on/off

    // Per-stage variant (0 = clean DaisySP, 1 = characterful variant).
    int chorusType_  = 0;   // 1 = Ensemble (Juno-style)
    int flangerType_ = 0;   // 1 = Barber-pole (infinite sweep)
    int phaserType_  = 0;   // 1 = Bi-phase (dual LFO)

    // Per-voice FX engines (one set each so Out1/Out2 stay independent).
    // Chorus uses up to 3 detuned voices for the Ensemble variant; clean uses [0].
    daisysp::ChorusEngine chorus1_[3];   // Out1
    daisysp::ChorusEngine chorus2_[3];   // Out2
    daisysp::Flanger      flanger1_;
    daisysp::Flanger      flanger2_;
    daisysp::Phaser       phaser1_;
    daisysp::Phaser       phaser2_;

    // Variant DSP helpers (branch on the per-stage type).
    float ProcessChorus (float in, daisysp::ChorusEngine eng[3]);
    float ProcessFlanger(float in, daisysp::Flanger& clean,
                         daisysp::DelayLine<float, 2048>& bp, float bpPhase);
    float ProcessPhaser (float in, daisysp::Phaser& ph, float biLfo);

    float phaserScale_ = 0.25f;  // 1/poles, normalizes the parallel-pole sum

    // --- Flanger Barber-pole variant state (one delay line + phasor per voice) ---
    daisysp::DelayLine<float, 2048> bpFl1_;
    daisysp::DelayLine<float, 2048> bpFl2_;
    float bpPhase_   = 0.0f;   // 0..1 sweep phasor (shared; voice2 offset by 0.5)
    float bpInc_     = 0.0f;   // per-sample phase increment (sign = direction)
    float bpFeedback_= 0.0f;   // 0..~0.9

    // --- Phaser Bi-phase variant state (2nd LFO modulating the centre freq) ---
    float biLfoPhase_ = 0.0f;  // 0..1 second-LFO phasor
    float biLfoInc_   = 0.0f;  // per-sample increment
    float biCentre_   = 600.0f;// base allpass centre freq (Hz)
    float biDepth_    = 0.0f;   // centre-freq sweep depth (Hz)

    // Shared lo-fi coefficients (set by SetLofiMacro from Pot 4)
    int   srrFactor_ = 1;     // sample-and-hold length; 1 = clean
    float hpCoeff_   = 0.0f;  // one-pole HP; 0 = bypass (unused since CW = SRR/dist)
    float satAmt_    = 0.0f;  // soft-saturation blend (CW)
    float distAmt_   = 0.0f;  // overdrive/distortion blend (CW); 0 = bypass
    float lpCoeff_   = 1.0f;  // 2-pole LP; 1 = open/no filtering
    float driveGain_ = 1.0f;  // pre-fold drive
    float foldMix_   = 0.0f;  // wavefold blend; 0 = bypass
    // Resonant band-pass sweep (CW side; SVF coeffs set from g_lofiConfig)
    float bpF_       = 0.0f;  // SVF frequency coeff = 2*sin(pi*fc/fs)
    float bpDamp_    = 1.0f;  // SVF damping = 1/Q
    float bpMix_     = 0.0f;  // wet/dry blend; 0 = bypass
    bool  lofiClean_ = true;  // true in the clean center deadzone

    // --- Internal LPG (daisy-nmr): vactrol-style low-pass gate ---------------
    // One shared envelope (single trigger source, single decay setting) drives
    // BOTH a VCA and a 2-pole low-pass per voice, so quiet is also dark -- the
    // vactrol behaviour that makes an LPG sound like a struck body rather than
    // a plain fade. Filter state is per-voice (LofiState.lpgLp1/2); the
    // envelope is shared so Out1/Out2 are plucked in lockstep.
    void  LpgUpdateEnvelope();          // advance the shared envelope one sample
    float ProcessLpg(float v, LofiState& s, float coeff);
    float LpgCoeff(float env) const;    // envelope -> LP coefficient (LUT + lerp)

    bool  lpgEnabled_ = false;
    volatile bool lpgTrigPending_ = false;  // set by the Sync EXTI ISR
    bool  lpgAttacking_ = false;   // true during the (short) attack ramp
    float lpgEnv_       = 0.0f;    // 0..1 shared envelope
    float lpgAttackInc_ = 1.0f;    // per-sample linear attack step
    float lpgDecayCoef_ = 0.0f;    // per-sample exponential decay multiplier

    // env -> one-pole LP coefficient, exponential in cutoff. 65 entries (env in
    // 1/64 steps) + linear interpolation keeps an expf/sinf out of the sample
    // loop; the mapping is smooth enough that 64 steps are inaudible.
    static constexpr int LPG_LUT_N = 65;
    float lpgCoefLut_[LPG_LUT_N] = {};
};

// Live-tunable Lo-Fi config (RAM, written by the monitor over SWD via mww).
// magic lets the monitor confirm it located the struct.
struct LofiConfig {
    uint32_t magic;            // 'LOFI' = 0x4C4F4649
    float bpCutoffStartHz;     // band-pass cutoff just past center
    float bpCutoffEndHz;       // band-pass cutoff at full CW
    float bpQStart;            // resonance/Q at center (low = wide)
    float bpQEnd;              // resonance/Q at full CW (high = narrow/resonant)
    float bpMixMax;            // max wet blend at full CW (0..1)
    float sweepCurve;          // shapes cutoff+mix vs throw: 1=linear, <1 front-loads
};
extern LofiConfig g_lofiConfig;
