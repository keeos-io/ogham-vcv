// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the transcribed application layer
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// Transcribed from ogham_main.cpp (keeos-io/ogham @ 4dbd4c5, firmware v1.16),
// which is not compiled here because it is a main() on file-scope globals.
// Section order, constant names and the comments that explain *why* are kept, so
// the two can be read side by side. See src/OghamApp.hpp.

#include "OghamApp.hpp"

#include <cmath>

#include "formulas.h"

namespace ogham {
namespace {

// ---------------------------------------------------------------------------
// Constants, as ogham_main.cpp declares them.
//
// The hardware calibration constants are NOT carried across: RATE_POT_CENTER,
// POT_FULL_SCALE, the MCP6004 fit and TIMBRE_CV_K_A/B all correct for real
// analog parts, and reproducing them here would be reproducing their errors.
// Rack's params are exact, so the centre is exactly 0.5 and the knob gains are
// exactly 1. Every one of those is a row in docs/firmware-differences.md.
// ---------------------------------------------------------------------------

constexpr int   kControlDivision = 48;      // the main loop, ~1 kHz at 48 kHz

// Rate knob. The pot calibration collapses to the ideal case.
constexpr float RATE_POT_MIN    = 0.0f;
constexpr float RATE_POT_CENTER = 0.5f;     // was 0.459, the fleet mean 12 o'clock
constexpr float RATE_POT_MAX    = 1.0f;     // was 0.955

// ~2 degrees of throw. On hardware this is also ~100x the ADC noise; here there
// is no noise, but the gesture should feel the same, so the deadband stays.
constexpr float RATE_REROLL_DEADBAND = 0.008f;

constexpr float TIMBRE_CV_DEPTH = 1.0f;

constexpr int   CLOCK_RATIO_MAX_EXP = 5;    // 2^5 = x32 up, /32 down

constexpr uint32_t EXT_CLOCK_TIMEOUT_US      = 10000000;  // 10 s
constexpr uint32_t EXT_CLOCK_TIMEOUT_PERIODS = 2;
constexpr uint32_t EXT_CLOCK_TIMEOUT_MIN_MS  = 90;
constexpr uint32_t EXT_CLOCK_TIMEOUT_MAX_MS  = 10000;
constexpr uint32_t MIN_CLOCK_PERIOD_US       = 200;       // comparator chatter

constexpr float RATE_HOLD_EXIT_EPS = 0.02f;

// Clock In -> tempo. One pulse per beat at 120 BPM is the unity reference.
constexpr float TEMPO_UNITY_BPM       = 120.0f;
constexpr int   TEMPO_PULSES_PER_BEAT = 1;
constexpr float TEMPO_UNITY_HZ        = TEMPO_UNITY_BPM * TEMPO_PULSES_PER_BEAT / 60.0f;
constexpr float CLOCK_RATE_MAX        = 64.0f;

// V/oct. The ADC fractions are gone — a volt is a volt here — but the tuning
// constant stays: VOCT_RATE_TUNE corrects the whole bank at once, because every
// melodic formula has a power-of-two period.
constexpr float SR_CORRECTION      = 28160.0f / 28154.0f;
constexpr float VOCT_BASE_HZ       = 32.70f / SR_CORRECTION;   // C1
constexpr float VOCT_NATURAL_HZ    = 8000.0f / 256.0f;         // 31.25 Hz
constexpr float VOCT_RATE_TUNE     = VOCT_BASE_HZ / VOCT_NATURAL_HZ;
constexpr float VOCT_KNOB_SPAN_OCT = 2.0f;                     // full throw = +-1 octave
constexpr float VOCT_RATE_MIN      = 1.0f / 64.0f;

// How far past the committed value the continuous position must travel before a
// new value is latched, in LSB of the 0-255 range. Must stay under 1.0 or
// integers become unreachable.
constexpr float PARAM_COMMIT_LSB = 0.75f;

// Encoder gestures.
constexpr uint32_t LONG_PRESS_MS = 600;

// Acceleration: the faster you turn, the bigger the step. A hard crank jumps
// ~8 per detent (~12 detents across the whole 100-slot range) while single or
// slow detents stay 1-for-1 for precise landing.
constexpr uint32_t ENC_FAST_MS = 25;
constexpr uint32_t ENC_MED_MS  = 50;
constexpr uint32_t ENC_SLOW_MS = 90;
constexpr int ENC_FAST_MULT = 8;
constexpr int ENC_MED_MULT  = 4;
constexpr int ENC_SLOW_MULT = 2;

// Menu navigation accelerates on its own, gentler curve. The menu is 22 fields,
// not 100 slots: at x8 a single fast detent would cross 40% of the list and
// overshooting would be the norm. x3 still slams end-to-end in ~7 detents, and
// clamping makes the ends a reliable landing spot.
constexpr int ENC_MENU_FAST_MULT = 3;
constexpr int ENC_MENU_MED_MULT  = 2;

constexpr uint32_t DISPLAY_INTERVAL_MS = 33;   // ~30 Hz

// How long after start-up the A/B value flash stays suppressed. On hardware
// this protects the boot version splash; here it stops a freshly instantiated
// module flashing a value nobody asked for.
constexpr uint32_t PARAM_FLASH_GRACE_MS = 600;

// ---------------------------------------------------------------------------
// Helpers, as ogham_main.cpp declares them.
// ---------------------------------------------------------------------------

inline float CenterNorm(float raw, float lo, float ctr, float hi) {
    float n;
    if (raw <= ctr) n = (ctr > lo) ? 0.5f * (raw - lo) / (ctr - lo) : 0.0f;
    else            n = (hi > ctr) ? 0.5f + 0.5f * (raw - ctr) / (hi - ctr) : 1.0f;
    if (n < 0.0f) n = 0.0f;
    if (n > 1.0f) n = 1.0f;
    return n;
}

inline int ClockRatioExp(float centered) {
    int e = (int)lroundf((centered - 0.5f) * (2 * CLOCK_RATIO_MAX_EXP));
    if (e >  CLOCK_RATIO_MAX_EXP) e =  CLOCK_RATIO_MAX_EXP;
    if (e < -CLOCK_RATIO_MAX_EXP) e = -CLOCK_RATIO_MAX_EXP;
    return e;
}

// Controls::MapKnobToRate, which lives in a translation unit the plugin does not
// compile. Exponential: 0 -> 1/64x, 0.5 -> 1x, 1 -> 64x. Wide enough to run the
// engine at LFO-rate speeds for CV Out.
inline float MapKnobToRate(float knob) {
    return powf(2.0f, 12.0f * knob - 6.0f);
}

inline uint32_t Median3(uint32_t a, uint32_t b, uint32_t c) {
    if (a > b) { uint32_t t = a; a = b; b = t; }
    if (b > c) { uint32_t t = b; b = c; c = t; }
    if (a > b) { uint32_t t = a; a = b; b = t; }
    return b;
}

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// The menu's field index to the config byte it edits. Returns nullptr for the
// global on/off (0) and the chain toggle (1), which are handled by name.
uint8_t* FxFieldPtr(FxChainConfig& f, int field) {
    switch (field) {
        case 2:  return &f.chorusLevel;  case 3:  return &f.chorusType;
        case 4:  return &f.chorusP1;     case 5:  return &f.chorusP2;
        case 6:  return &f.flangerLevel; case 7:  return &f.flangerType;
        case 8:  return &f.flangerP1;    case 9:  return &f.flangerP2;
        case 10: return &f.phaserLevel;  case 11: return &f.phaserType;
        case 12: return &f.phaserP1;     case 13: return &f.phaserP2;
        default: return nullptr;
    }
}

// True for the per-stage "type" sub-field (sub == 1): fields 3, 7, 11.
inline bool FxFieldIsType(int field) {
    return field >= 2 && field <= 13 && ((field - 2) % 4) == 1;
}

// Param-interp grid (q) steps through this discrete list (0 = off).
uint8_t NextQuant(uint8_t cur, int dir) {
    static const uint8_t list[] = {0, 4, 8, 16, 32, 64, 128};
    const int n = (int)(sizeof(list) / sizeof(list[0]));
    int idx = 0;
    for (int i = 0; i < n; i++) if (list[i] == cur) { idx = i; break; }
    idx += (dir > 0) ? 1 : -1;
    if (idx < 0) idx = 0;
    if (idx >= n) idx = n - 1;
    return list[idx];
}

}  // namespace

// ---------------------------------------------------------------------------

void OghamApp::Init() {
    engine_.Init();
    pipeline_.Init();
    cvOutput_.Init(&dac_);
    bpmClock_.Init();
    tm1637_.Init(&seed_, 9, 10);     // the module's CLK/DIO pins; inert here
    display_.Init(&tm1637_);

    fx_ = AudioPipeline::DefaultFxChain();
    ApplyFxChain();

    engine_.SetParamA(128);
    engine_.SetParamB(128);
    engine_.SetRate(1.0f);
    pipeline_.SetLofiMacro(0.5f);

    coreSample_ = 0;
    controlCount_ = 0;
    prevFormulaIdx_ = engine_.GetFormula1Index();
}

void OghamApp::ApplyFxChain() {
    pipeline_.SetFxChain(fx_);
    engine_.SetParamQuant(fx_.paramQuant);
    engine_.SetOut2Decoupled(fx_.out2Drone != 0);
    cvOutput_.SetMode((CvOutput::Mode)fx_.cvOutMode);
}

// The gate EXTI ISR: an external rising edge requests a hard-sync reset and
// plucks the LPG. Both only raise a flag that the audio loop consumes at the
// next sample, so the waveform restart and the gate's attack land together.
// Here the edge is found at host rate and applied before this sample's core
// steps, which is the same ordering with none of the interrupt latency.
//
// The clock EXTI ISR, likewise, but with its timing arithmetic intact.
void OghamApp::OnClockEdge() {
    const uint64_t now = coreSample_ * 1000000ull / 48000ull;

    // The firmware needs a signed difference and a wrap guard here, because
    // System::GetUs() counts 0..~17.9M and wraps; a wrapped edge read as a huge
    // gap, which showed up as a ~1/2 s tempo dropout at low rates. A 64-bit
    // sample counter cannot wrap in any session, so the guard is not
    // transcribed — one of the few places where less code is the faithful
    // translation.
    const uint64_t period = now - lastClockEdgeUs_;
    lastClockEdgeUs_ = now;

    if (period < MIN_CLOCK_PERIOD_US) return;      // refractory: comparator chatter
    if (period >= EXT_CLOCK_TIMEOUT_US) {          // first edge after a real gap:
        extClockActive_ = false;                   // re-anchor only; next edge tracks
        return;
    }
    const uint32_t per = (uint32_t)period;

    // Median-3 (seed on the first edge after a gap, else shift in).
    if (!extClockActive_) { clkP0_ = clkP1_ = clkP2_ = per; extClockActive_ = true; }
    else                  { clkP0_ = clkP1_; clkP1_ = clkP2_; clkP2_ = per; }
    const uint32_t medP = Median3(clkP0_, clkP1_, clkP2_);
    lastClockPeriodUs_ = medP;

    // Tempo: snap the rate to the measured clock (no portamento). Knob trims it.
    float target = (1000000.0f / (float)medP) / TEMPO_UNITY_HZ;
    if (target > CLOCK_RATE_MAX) target = CLOCK_RATE_MAX;
    extClockRate_ = target;
}

// ---------------------------------------------------------------------------
// The main loop, at ~1 kHz. Section order follows ogham_main.cpp.
// ---------------------------------------------------------------------------

void OghamApp::PollControls(const AppInputs& in) {
    const uint32_t nowMs = (uint32_t)(coreSample_ / 48ull);

    HandleEncoder(in, nowMs);

    // --- CV->Timbre routing: partition each channel's pot+CV into a knob part
    //     and an isolated CV part. In the alt routing modes that CV is borrowed
    //     to modulate the Timbre/Lo-Fi macro, and the channel's Param then
    //     follows the knob only.
    //
    // On hardware the split has to be recovered from an analog sum, which is why
    // the firmware multiplies the pot by a measured gain and subtracts. Here the
    // two arrive separately and exactly, so the partition is simply what it is.
    const int   timbreRoute = fx_.timbreCvRoute;
    const float knobA   = in.potA;
    const float knobB   = in.potB;
    const float cvOnlyA = in.cvA;
    const float cvOnlyB = in.cvB;
    const float combinedA = Clamp01(knobA + cvOnlyA);
    const float combinedB = Clamp01(knobB + cvOnlyB);

    const float paramASrc = Clamp01((timbreRoute == 1) ? knobA : combinedA);
    const float paramBSrc = Clamp01((timbreRoute == 2) ? knobB : combinedB);

    // --- Combined Pot+CV -> Parameters A/B, fixed 0-255, fed to BOTH voices ---
    //
    // The sub-LSB hysteresis is kept even though Rack's params carry no dither.
    // It costs nothing, and it keeps this readable against the firmware, where
    // it exists because comparing the ROUNDED value made every odd number
    // unreachable and the control moved in twos.
    // Params still track the knobs in the menu, but the flash is suppressed so
    // it cannot clobber the field you are editing. The boot grace does the same
    // for the version splash; here it stops a freshly instantiated module
    // flashing a value nobody asked for.
    const bool flashOk = (nowMs > PARAM_FLASH_GRACE_MS) && (funcMode_ != FUNC_FX);
    {
        const float scaledA = paramASrc * 255.0f;
        int32_t a = (int32_t)(scaledA + 0.5f);
        if (a < 0) a = 0;
        if (a > 255) a = 255;
        const int32_t curA = engine_.GetParamA();
        const float   dA   = scaledA - (float)curA;
        const bool    endA = (a != curA) && (a == 0 || a == 255);
        if (dA > PARAM_COMMIT_LSB || dA < -PARAM_COMMIT_LSB || endA) {
            engine_.SetParamA(a);
            if (flashOk) display_.UpdateFlashValue('A', a);
        }
        // (Re)start the flash only on physical knob movement, so CV alone can
        // never hold the display on.
        const float rawScaledA = knobA * 255.0f;
        if (lastKnobStepA_ < 0) {
            lastKnobStepA_ = (int32_t)(rawScaledA + 0.5f);   // seed, no flash
        } else {
            const float dRawA = rawScaledA - (float)lastKnobStepA_;
            if (dRawA > PARAM_COMMIT_LSB || dRawA < -PARAM_COMMIT_LSB) {
                lastKnobStepA_ = (int32_t)(rawScaledA + 0.5f);
                if (flashOk) display_.FlashParam('A', a);
            }
        }

        const float scaledB = paramBSrc * 255.0f;
        int32_t b = (int32_t)(scaledB + 0.5f);
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        const int32_t curB = engine_.GetParamB();
        const float   dB   = scaledB - (float)curB;
        const bool    endB = (b != curB) && (b == 0 || b == 255);
        if (dB > PARAM_COMMIT_LSB || dB < -PARAM_COMMIT_LSB || endB) {
            engine_.SetParamB(b);
            if (flashOk) display_.UpdateFlashValue('b', b);
        }
        const float rawScaledB = knobB * 255.0f;
        if (lastKnobStepB_ < 0) {
            lastKnobStepB_ = (int32_t)(rawScaledB + 0.5f);
        } else {
            const float dRawB = rawScaledB - (float)lastKnobStepB_;
            if (dRawB > PARAM_COMMIT_LSB || dRawB < -PARAM_COMMIT_LSB) {
                lastKnobStepB_ = (int32_t)(rawScaledB + 0.5f);
                if (flashOk) display_.FlashParam('b', b);
            }
        }
    }

    // --- Clock hold state ---
    // A live clock overrides any hold; while held, moving the Rate pot past the
    // deadband exits back to continuous Hz.
    if (extClockActive_) {
        clockHeld_ = false;
    } else if (clockHeld_) {
        const float r = in.potRate;
        if (r - heldRateRef_ > RATE_HOLD_EXIT_EPS ||
            heldRateRef_ - r > RATE_HOLD_EXIT_EPS) {
            clockHeld_ = false;
        }
    }

    // --- Rate ---
    const float rawRatePot = in.potRate;
    const float rateKnob   = CenterNorm(rawRatePot, RATE_POT_MIN,
                                        RATE_POT_CENTER, RATE_POT_MAX);
    const float knobRate   = MapKnobToRate(rateKnob);

    // Re-roll CV Out's capture phase whenever the Rate knob actually moves.
    // Which phase the Hold stage sits on decides how the capture lines up with
    // the formula's own periodicity, and that changes how VARIED the resulting
    // LFO is far more than it changes its level — with no way to pick a good one
    // in advance. Tying a re-roll to the knob makes finding one a gesture.
    //
    // Keyed off the RAW pot, not the derived rate, so it still re-rolls in
    // clocked mode where the knob quantises to x/div steps.
    if (lastRerollPot_ < 0.0f) {
        // First control pass: adopt the knob position WITHOUT rolling, so a
        // fresh instance comes back on the built-in phase and a saved patch
        // sounds the same as it was left. The first nudge randomises.
        lastRerollPot_ = rawRatePot;
    } else if (fabsf(rawRatePot - lastRerollPot_) > RATE_REROLL_DEADBAND) {
        lastRerollPot_ = rawRatePot;
        // The firmware seeds this from the microsecond timer. A plugin must
        // reload a patch identically, so the entropy is the engine's own
        // position instead — unpredictable at the moment of a knob move, and
        // reproducible when the patch is restored.
        pipeline_.RerollCvSampleOffset((uint32_t)(coreSample_ ^ engine_.GetT()));
    }

    if (engine_.GetFormula1Index() == GetReferenceIndex()) {
        // A440 reference: pin the rate to exactly 1x so the tone is a
        // dead-accurate 440 Hz, whatever the Rate knob, clock or V/oct say.
        engine_.SetPitchSync(0.0f);
        engine_.SetRate(1.0f);
    } else if (in.voctMode) {
        // V/oct drives the PLAYBACK RATE; there is no pitch hard-sync.
        //
        // A component of period P ticks played at R ticks/s sounds at R/P Hz, so
        // an exponential R tracks 1 V/oct exactly — and every partial scales
        // with it, so this is varispeed transposition with the waveform shape
        // preserved. The formula runs out normally, which the old hard-sync
        // could not do.
        //
        // Rate knob = bipolar FINE TUNE, +-12 semitones, 12 o'clock = 0.
        // Continuous rather than quantized, so Ogham can be tuned by ear.
        const float cvOct   = in.voctVolts;          // a volt is an octave
        const float knobOct = (rateKnob - 0.5f) * VOCT_KNOB_SPAN_OCT;
        float rate = VOCT_RATE_TUNE * powf(2.0f, cvOct + knobOct);
        if (rate > CLOCK_RATE_MAX) rate = CLOCK_RATE_MAX;
        if (rate < VOCT_RATE_MIN)  rate = VOCT_RATE_MIN;
        engine_.SetPitchSync(0.0f);
        engine_.SetRate(rate);
        lastClockRatioExp_ = 999;   // so returning to a clock re-flashes x1
    } else {
        // Clock In sets the playback rate. With a clock present (or held after a
        // cable pull) the Rate pot is a QUANTIZED multiply/divide of the clock
        // rate; with no clock it is the continuous 1/64x-64x knob.
        engine_.SetPitchSync(0.0f);
        if (extClockActive_ || clockHeld_) {
            const int e = extClockActive_
                ? ClockRatioExp(rateKnob)
                : (lastClockRatioExp_ == 999 ? 0 : lastClockRatioExp_);
            float rate = extClockRate_ * ldexpf(1.0f, (float)e);   // 2^e, exact
            if (rate > CLOCK_RATE_MAX) rate = CLOCK_RATE_MAX;
            engine_.SetRate(rate);
            if (extClockActive_ && e != lastClockRatioExp_) {
                display_.FlashClockRatio(e);
            }
            lastClockRatioExp_ = e;
        } else {
            engine_.SetRate(knobRate);
            lastClockRatioExp_ = 999;   // so the next clock re-flashes x1
        }
    }

    // --- Tone macro, with the CV->Timbre route added as a bidirectional offset
    //     around the knob position (self-clamps in SetLofiMacro) ---
    float timbre = in.potTone;
    if (timbreRoute == 1)      timbre += TIMBRE_CV_DEPTH * cvOnlyA;
    else if (timbreRoute == 2) timbre += TIMBRE_CV_DEPTH * cvOnlyB;
    timbre = Clamp01(timbre);
    // Mapped onto the pot scale SetLofiMacro is calibrated for — see the note in
    // OghamApp.hpp. Without this the clean centre sits 4% anticlockwise of noon.
    const float lofiPot = lofi::PotFromKnob(timbre);
    if (lofiPot != lastToneApplied_) {
        lastToneApplied_ = lofiPot;
        pipeline_.SetLofiMacro(lofiPot);   // computes coefficients: change only
    }

    // A field written from the right-click mirror: recompute here, on the audio
    // thread, rather than wherever the click happened.
    if (fxApplyPending_) {
        fxApplyPending_ = false;
        ApplyFxChain();
    }

    // --- Out2 decouple/drone. Idempotent: the engine only snapshots Out2 on the
    //     couple->decouple edge, so calling every loop is safe and also applies
    //     restored state. ---
    engine_.SetOut2Decoupled(fx_.out2Drone != 0);

    // --- CV Out. Capture interval first: the DC-mode slew coefficients are
    //     derived from it, so a Rate change has to land before the slew values
    //     are reapplied. All of these are no-ops unless something changed. ---
    cvOutput_.SetMode((CvOutput::Mode)fx_.cvOutMode);
    cvOutput_.SetCaptureInterval(pipeline_.GetCaptureSamples(),
                                 pipeline_.GetCaptureSamples2());
    cvOutput_.SetSlewRise(fx_.cvSlewRise);
    cvOutput_.SetSlewFall(fx_.cvSlewFall);
    cvOutput_.SetHold(fx_.cvHold);

    // --- External clock timeout ---
    // The firmware watches the ISR's timestamp on the monotonic millisecond
    // clock rather than on GetUs, because GetUs wraps every ~17.9 s and an edge
    // landing near the top of that cycle left extClockActive stuck true after
    // the cable was pulled. The same structure holds here.
    if (lastClockEdgeUs_ != lastSeenEdgeUs_) {
        lastSeenEdgeUs_ = lastClockEdgeUs_;
        lastEdgeSeenMs_ = nowMs;
    }
    // Adaptive timeout: ~2 clock periods, clamped. Fast clocks revert quickly on
    // unplug; slow clocks keep a wide window so they do not self-revert.
    uint32_t timeoutMs = (lastClockPeriodUs_ / 1000) * EXT_CLOCK_TIMEOUT_PERIODS;
    if (timeoutMs < EXT_CLOCK_TIMEOUT_MIN_MS) timeoutMs = EXT_CLOCK_TIMEOUT_MIN_MS;
    if (timeoutMs > EXT_CLOCK_TIMEOUT_MAX_MS) timeoutMs = EXT_CLOCK_TIMEOUT_MAX_MS;
    if (extClockActive_ && (nowMs - lastEdgeSeenMs_) > timeoutMs) {
        // Edges stopped: HOLD the last clock rate rather than reverting to the
        // knob. extClockRate stays frozen; a later edge re-anchors, a Rate move
        // exits the hold.
        extClockActive_ = false;
        clockHeld_      = true;
        heldRateRef_    = in.potRate;
    }

    // --- CV output ---
    cvOutput_.UpdateOutput();

    // --- BPM: re-estimate on a formula change only. A and B are continuous
    //     timbre controls; a sweep resets the estimator faster than it can lock.
    {
        const int idx = engine_.GetFormula1Index();
        if (idx != prevFormulaIdx_) {
            prevFormulaIdx_ = idx;
            bpmClock_.RequestEstimate();
        }
    }
    bpmClock_.Update(engine_.GetRate());

    UpdateDisplay(nowMs);
}

// ---------------------------------------------------------------------------
// The Func encoder: gestures and the menu state machine.
// ---------------------------------------------------------------------------

void OghamApp::HandleEncoder(const AppInputs& in, uint32_t nowMs) {
    const bool pressed = in.encPressed;
    bool gShort = false, gLong = false;

    // Gestures the host classified for us, and the right-click menu request,
    // all join the same path — so there is one implementation of what a click
    // and a long press mean, whoever noticed them.
    if (clickPending_ > 0) { clickPending_ = 0; gShort = true; }
    if (longPending_  > 0) { longPending_  = 0; gLong  = true; }
    if (in.menuToggle) gLong = true;

    if (pressed && !encWasPressed_) {            // press start
        encPressStart_ = nowMs;
        encLongFired_ = false;
        // Touching the encoder hands the display over at once — an A/B value
        // flash should not sit in front of the gesture that follows it.
        display_.CancelFlash();
    }
    if (pressed && !encLongFired_ &&
        (nowMs - encPressStart_ >= LONG_PRESS_MS)) {
        encLongFired_ = true;
        gLong = true;                            // long press (while held)
    }
    if (gLong) encLongFired_ = true;             // don't also fire on release
    if (!pressed && encWasPressed_ && !encLongFired_) {  // short release
        gShort = true;
    }
    encWasPressed_ = pressed;

    // Long press: enter the menu from SELECT, or leave it from anywhere,
    // including mid-edit. Short click: SELECT -> switch voice; menu -> toggle
    // navigate <-> edit on the current field.
    if (gLong) {
        if (funcMode_ == FUNC_FX) {
            funcMode_ = FUNC_SELECT;
            fxEditing_ = false;
        } else {
            funcMode_ = FUNC_FX;
            // Re-enter on the field you left, NOT field 0: with 22 fields and no
            // wrap, re-navigating from the top every time to tweak one parameter
            // is the whole cost of the menu.
            fxEditing_ = false;   // but never resume mid-edit
        }
    } else if (gShort) {
        if (funcMode_ == FUNC_SELECT) selOut_ ^= 1;
        else fxEditing_ = !fxEditing_;
    }

    // Encoder turn (acceleration scales the step by how fast you turn).
    const int enc = encPending_;
    encPending_ = 0;
    if (enc != 0) {
        display_.CancelFlash();   // the turn owns the display from here
        const uint32_t dt = nowMs - lastEncMs_;
        lastEncMs_ = nowMs;
        int step = 1;
        if (dt < ENC_FAST_MS)      step = ENC_FAST_MULT;
        else if (dt < ENC_MED_MS)  step = ENC_MED_MULT;
        else if (dt < ENC_SLOW_MS) step = ENC_SLOW_MULT;
        const int delta = enc * step;

        if (funcMode_ == FUNC_FX) {
            if (!fxEditing_) {
                // Navigate: CLAMPED at both ends — no wrap, matching the function
                // selector. Wrapping made a crank at either end silently jump to
                // the far side of the menu; clamping means the ends are findable
                // by feel.
                int mstep = 1;
                if (dt < ENC_FAST_MS)      mstep = ENC_MENU_FAST_MULT;
                else if (dt < ENC_MED_MS)  mstep = ENC_MENU_MED_MULT;
                int n = fxField_ + enc * mstep;
                if (n < 0) n = 0;
                if (n > FX_NUM_FIELDS - 1) n = FX_NUM_FIELDS - 1;
                fxField_ = n;
            } else if (fxField_ == FX_FIELD_GLOBAL) {
                fx_.enabled = (enc > 0) ? 1 : 0;    // CW = on, CCW = off
                pipeline_.SetFxChain(fx_);
            } else if (fxField_ == FX_FIELD_CHAIN) {
                fx_.parallel = (enc > 0) ? 1 : 0;   // CW = parallel, CCW = serial
                pipeline_.SetFxChain(fx_);
            } else if (fxField_ == FX_FIELD_QUANT) {
                fx_.paramQuant = NextQuant(fx_.paramQuant, enc);
                engine_.SetParamQuant(fx_.paramQuant);
            } else if (fxField_ == FX_FIELD_DRONE) {
                // Out2 decouple/drone: CW = decoupled (frozen), CCW = coupled.
                // The engine snapshots on the couple->decouple edge.
                fx_.out2Drone = (enc > 0) ? 1 : 0;
            } else if (fxField_ == FX_FIELD_CVOUT) {
                int nv = (int)fx_.cvOutMode + (enc > 0 ? 1 : -1);
                if (nv < 0) nv = 0;
                if (nv > 3) nv = 3;
                fx_.cvOutMode = (uint8_t)nv;
            } else if (fxField_ == FX_FIELD_TIMBRECV) {
                int nv = (int)fx_.timbreCvRoute + (enc > 0 ? 1 : -1);
                if (nv < 0) nv = 0;
                if (nv > 2) nv = 2;
                fx_.timbreCvRoute = (uint8_t)nv;
            } else if (fxField_ == FX_FIELD_LPG) {
                // Internal LPG, on/off and decay in one field: 0 = off, 1..99 =
                // on with that decay. SetFxChain plucks it once on the
                // 0->nonzero edge so the change is audible.
                int nv = (int)fx_.lpgDecay + delta;
                if (nv < 0) nv = 0;
                if (nv > 99) nv = 99;
                fx_.lpgDecay = (uint8_t)nv;
                pipeline_.SetFxChain(fx_);   // live preview
            } else if (fxField_ == FX_FIELD_CVSLEWRISE) {
                int nv = (int)fx_.cvSlewRise + delta;
                if (nv < 0) nv = 0;
                if (nv > 99) nv = 99;
                fx_.cvSlewRise = (uint8_t)nv;
            } else if (fxField_ == FX_FIELD_CVSLEWFALL) {
                int nv = (int)fx_.cvSlewFall + delta;
                if (nv < 0) nv = 0;
                if (nv > 99) nv = 99;
                fx_.cvSlewFall = (uint8_t)nv;
            } else if (fxField_ == FX_FIELD_CVHOLD) {
                // 0 = off (every tick) .. 8 = every 256 ticks, power-of-2 steps.
                // One detent per step, not accelerated: only nine values.
                int nv = (int)fx_.cvHold + (enc > 0 ? 1 : -1);
                if (nv < 0) nv = 0;
                if (nv > 8) nv = 8;
                fx_.cvHold = (uint8_t)nv;
            } else {
                // Type fields clamp to FX_TYPE_MAX; level and params use 0..99.
                uint8_t* p = FxFieldPtr(fx_, fxField_);
                const int maxv = FxFieldIsType(fxField_) ? FX_TYPE_MAX : 99;
                int nv = (int)(*p) + delta;
                if (nv < 0) nv = 0;
                if (nv > maxv) nv = maxv;
                *p = (uint8_t)nv;
                pipeline_.SetFxChain(fx_);   // live preview
            }
        } else if (selOut_ == 0) {
            engine_.SetFormula1(engine_.GetFormula1Index() + delta);
        } else {
            engine_.SetFormula2(engine_.GetFormula2Index() + delta);
        }
    }
}

// ---------------------------------------------------------------------------

int OghamApp::MenuValue(int field) const {
    if (field == FX_FIELD_GLOBAL)     return fx_.enabled;
    if (field == FX_FIELD_CHAIN)      return fx_.parallel;
    if (field == FX_FIELD_QUANT)      return fx_.paramQuant;
    if (field == FX_FIELD_DRONE)      return fx_.out2Drone;
    if (field == FX_FIELD_CVOUT)      return fx_.cvOutMode;
    if (field == FX_FIELD_TIMBRECV)   return fx_.timbreCvRoute;
    if (field == FX_FIELD_LPG)        return fx_.lpgDecay;
    if (field == FX_FIELD_CVSLEWRISE) return fx_.cvSlewRise;
    if (field == FX_FIELD_CVSLEWFALL) return fx_.cvSlewFall;
    if (field == FX_FIELD_CVHOLD)     return (fx_.cvHold == 0) ? 0 : (1 << fx_.cvHold);
    const uint8_t* p = FxFieldPtr(const_cast<FxChainConfig&>(fx_), field);
    return p ? *p : 0;
}

void OghamApp::SetMenuValue(int field, int value) {
    switch (field) {
        case FX_FIELD_GLOBAL:      fx_.enabled       = value ? 1 : 0; break;
        case FX_FIELD_CHAIN:       fx_.parallel      = value ? 1 : 0; break;
        case FX_FIELD_CVOUT:       fx_.cvOutMode     = (uint8_t)clampi(value, 0, 3); break;
        case FX_FIELD_CVSLEWRISE:  fx_.cvSlewRise    = (uint8_t)clampi(value, 0, 99); break;
        case FX_FIELD_CVSLEWFALL:  fx_.cvSlewFall    = (uint8_t)clampi(value, 0, 99); break;
        case FX_FIELD_CVHOLD:      fx_.cvHold        = (uint8_t)clampi(value, 0, 8); break;
        case FX_FIELD_LPG:         fx_.lpgDecay      = (uint8_t)clampi(value, 0, 99); break;
        case FX_FIELD_TIMBRECV:    fx_.timbreCvRoute = (uint8_t)clampi(value, 0, 2); break;
        case FX_FIELD_QUANT:       fx_.paramQuant    = (uint8_t)clampi(value, 0, 128); break;
        case FX_FIELD_DRONE:       fx_.out2Drone     = value ? 1 : 0; break;
        default: {
            uint8_t* p = FxFieldPtr(fx_, field);
            if (!p) return;
            const int maxv = FxFieldIsType(field) ? FX_TYPE_MAX : 99;
            *p = (uint8_t)clampi(value, 0, maxv);
        } break;
    }
    fxApplyPending_ = true;
}

void OghamApp::SetMenuField(int f) {
    if (f < 0) f = 0;
    if (f > FX_NUM_FIELDS - 1) f = FX_NUM_FIELDS - 1;
    fxField_ = f;
}

uint32_t OghamApp::DisplaySegments() const {
    const uint8_t* s = tm1637_.GetLastSegs();
    return (uint32_t)s[0] | ((uint32_t)s[1] << 8)
         | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24);
}

void OghamApp::UpdateDisplay(uint32_t nowMs) {
    if (nowMs - lastDisplayTime_ < DISPLAY_INTERVAL_MS) return;
    lastDisplayTime_ = nowMs;

    display_.Update();   // time out any param flash first
    const bool clean = pipeline_.IsLofiClean();

    if (display_.IsFlashing()) {
        // On hardware this is deferred to the display tick because the write
        // blocks for ~9 ms and would otherwise throttle the main loop. Here the
        // write costs nothing, but the timing is kept: it is what the module
        // looks like.
        display_.DrawPendingFlash(clean);
    } else if (funcMode_ == FUNC_FX) {
        // Edit mode: flash the value at ~80% duty (~600 ms period) so it is
        // clear you are editing rather than navigating.
        const bool blankValue = fxEditing_ && ((nowMs % 600) >= 480);
        display_.ShowFxEdit(fxField_, MenuValue(fxField_),
                            fx_.parallel != 0, clean, blankValue);
    } else {
        const int idx = (selOut_ == 0) ? engine_.GetFormula1Index()
                                       : engine_.GetFormula2Index();
        if (idx == GetReferenceIndex()) display_.ShowVoiceRef(selOut_ + 1, clean);
        else                            display_.ShowVoice(selOut_ + 1, idx, clean);
    }
}

// ---------------------------------------------------------------------------
// The audio callback, one sample at a time.
// ---------------------------------------------------------------------------

void OghamApp::ProcessSample(const AppInputs& in, AppOutputs& out) {
    encPending_   += in.encDelta;
    clickPending_ += in.encClicks;
    longPending_  += in.encLongPresses;

    if (in.syncEdge) {
        engine_.SyncReset();
        pipeline_.LpgTrigger();
    }
    if (in.clockEdge) {
        OnClockEdge();
    }

    if (controlCount_ == 0) PollControls(in);
    if (++controlCount_ >= kControlDivision) controlCount_ = 0;

    float l = 0.f, r = 0.f;
    float* outPtr[2] = { &l, &r };
    pipeline_.Process(engine_, outPtr, 1);

    const float* clean     = pipeline_.GetCleanBuffer();
    const float* clean2    = pipeline_.GetCleanBuffer2();
    const bool*  cap1      = pipeline_.GetCvCaptureBuffer();
    const bool*  cap2      = pipeline_.GetCvCaptureBuffer2();
    const float* holdSamp1 = pipeline_.GetHoldSampleBuffer();
    const float* holdSamp2 = pipeline_.GetHoldSampleBuffer2();

    // The envelope follower and the BPM estimator read the FULL-scale audio, so
    // CV Out keeps its range. On hardware the jacks are attenuated after this
    // point by AUDIO_OUT_LEVEL; here they are not — that constant compensates an
    // over-gained analog stage and is not part of the sound.
    cvOutput_.ProcessSample(l, r, clean[0], clean2[0],
                            holdSamp1[0], holdSamp2[0], cap1[0], cap2[0]);
    bpmClock_.ProcessSample(clean[0]);

    out.out1 = l;
    out.out2 = r;
    out.env  = dac_.Normalized();
    out.eoc  = bpmClock_.GetClockState();

    coreSample_++;
}

}  // namespace ogham
