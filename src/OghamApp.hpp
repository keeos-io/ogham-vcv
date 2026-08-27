// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the transcribed application layer
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// Everything the module's firmware does OUTSIDE its DSP objects. The DSP itself
// is compiled verbatim from ogham-src/; this is the part that cannot be, because
// ogham_main.cpp is a main() built on file-scope globals and interrupt handlers.
//
// It is a TRANSCRIPTION, not a rewrite. It follows ogham_main.cpp's order, keeps
// its constant names, and carries over the comments that explain why a thing is
// the way it is. That is deliberate: the defence against these two drifting
// apart is that a person can put the files side by side and follow one against
// the other, and tidying this up would take that away.
//
// Every member is per-instance. Rack runs many copies of a module in one
// process, and the firmware's globals are exactly what must not survive the
// move.
//
// See docs/firmware-differences.md for the places this deliberately differs.

#pragma once

#include <cstdint>

#include "daisy_seed.h"
#include "bytebeat_engine.h"
#include "ogham_audio_pipeline.h"
#include "ogham_cv_output.h"
#include "ogham_display.h"
#include "bpm_clock.h"
#include "tm1637.h"

namespace ogham {

// What the host hands the application each core sample. On hardware this is
// what Controls reads from the ADC and the gate pins; here it comes from Rack's
// params and ports. Neither the engine nor this class ever learns which.
struct AppInputs {
    // Knob positions, 0..1. Already ideal: no ADC smoothing, no pot end-stop
    // correction, no analog summing amp to undo.
    float potA     = 0.5f;
    float potB     = 0.5f;
    float potRate  = 0.5f;
    float potTone  = 0.5f;

    // CV inputs as a fraction of the module's +/-5 V range, so +/-1.
    float cvA      = 0.f;
    float cvB      = 0.f;

    // The Mode switch, and the shared Clk/VOct jack read as volts.
    bool  voctMode = false;
    float voctVolts = 0.f;

    // True for exactly one core sample.
    bool  syncEdge  = false;
    bool  clockEdge = false;

    // The Func encoder. `encDelta` is detents since the last control tick,
    // signed; `encPressed` is the button's current state. Acceleration and
    // long-press timing are NOT done by the host — they live in the app, so the
    // gesture curves are the module's own.
    int   encDelta   = 0;
    bool  encPressed = false;
};

struct AppOutputs {
    float out1 = 0.f;   // -1..1
    float out2 = 0.f;
    float env  = 0.f;   // 0..1, as written to the module's DAC
    bool  eoc  = false;
};

class OghamApp {
public:
    void Init();

    // One core sample at 48 kHz. Consumes the edges, runs the DSP, and every
    // 48th call runs Poll() — the module's ~1 kHz main loop.
    void ProcessSample(const AppInputs& in, AppOutputs& out);

    // State the host needs for persistence and for the display, later.
    FxChainConfig&       Fx()       { return fx_; }
    const FxChainConfig& Fx() const { return fx_; }
    void ApplyFxChain();

    BytebeatEngine&  Engine()  { return engine_; }
    const TM1637&    Display() const { return tm1637_; }

    // The four segment bytes the display would be showing, packed low digit
    // first — the same layout the module's SWD telemetry uses.
    uint32_t DisplaySegments() const;

    // Menu state. RAM-only on the module (fxField is zero-initialised, so a
    // power cycle starts at field 0); the plugin persists selOut and fxField
    // because a patch has to reopen as it was left, but never mid-edit.
    int  SelectedVoice() const { return selOut_; }
    void SetSelectedVoice(int v) { selOut_ = (v != 0) ? 1 : 0; }
    int  MenuField() const { return fxField_; }
    void SetMenuField(int f);
    bool InMenu() const { return funcMode_ == FUNC_FX; }
    bool Editing() const { return fxEditing_; }

    // The value the menu would show for a field — the display needs it, and so
    // does a right-click mirror of the menu later.
    int  MenuValue(int field) const;

    int  Formula1() const { return engine_.GetFormula1Index(); }
    int  Formula2() const { return engine_.GetFormula2Index(); }
    void SetFormula1(int i) { engine_.SetFormula1(i); }
    void SetFormula2(int i) { engine_.SetFormula2(i); }

    // True while an external clock is driving the rate, or being held after the
    // cable was pulled. The panel has no light for this, but the display and
    // the tooltips want it.
    bool ExternalClock() const { return extClockActive_; }
    bool ClockHeld() const     { return clockHeld_; }
    float Rate() const         { return engine_.GetRate(); }
    float Bpm() const          { return bpmClock_.GetBpm(); }

private:
    void PollControls(const AppInputs& in);   // the main loop, ~1 kHz
    void OnClockEdge();                       // was the EXTI ISR
    void HandleEncoder(const AppInputs& in, uint32_t nowMs);
    void UpdateDisplay(uint32_t nowMs);

    // --- the firmware's objects, unmodified ---
    BytebeatEngine   engine_;
    AudioPipeline    pipeline_;
    CvOutput         cvOutput_;
    BpmClock         bpmClock_;
    TM1637           tm1637_;
    ::Display        display_;
    daisy::DaisySeed seed_;
    daisy::DacHandle dac_;

    FxChainConfig fx_;

    // --- what were file-scope globals in ogham_main.cpp ---
    // The names are kept so the two files can be read against each other.
    uint64_t coreSample_    = 0;      // replaces System::GetUs()'s wrapping counter
    int      controlCount_  = 0;

    // Clock tracking. On hardware these are written by the EXTI ISR and read by
    // the main loop, hence volatile; here one thread owns them.
    bool     extClockActive_ = false;
    float    extClockRate_   = 1.f;
    uint64_t lastClockEdgeUs_ = 0;
    uint32_t lastClockPeriodUs_ = 0;
    uint64_t lastSeenEdgeUs_ = 0;
    uint32_t lastEdgeSeenMs_ = 0;
    uint32_t clkP0_ = 0, clkP1_ = 0, clkP2_ = 0;

    bool     clockHeld_ = false;
    float    heldRateRef_ = 0.f;
    int      lastClockRatioExp_ = 999;

    // Rate knob / CV Out capture-phase re-roll.
    float    lastRerollPot_ = -1.f;

    // A/B commit hysteresis, function-local statics in the firmware.
    int32_t  lastKnobStepA_ = -1, lastKnobStepB_ = -1;

    int      prevFormulaIdx_ = -1;

    // The Func encoder's state machine.
    enum FuncMode { FUNC_SELECT = 0, FUNC_FX = 1 };
    FuncMode funcMode_  = FUNC_SELECT;
    int      selOut_    = 0;      // voice the encoder edits in SELECT mode
    int      fxField_   = 0;
    bool     fxEditing_ = false;  // false = navigate fields, true = edit value

    bool     encWasPressed_ = false;
    uint32_t encPressStart_ = 0;
    bool     encLongFired_  = false;
    uint32_t lastEncMs_     = 0;

    uint32_t lastDisplayTime_ = 0;

    // Detents delivered since the last control tick. The host can push them on
    // any sample; they are drained once, when the gesture machine next runs.
    // Accumulating here rather than host-side is what stops a detent being
    // dropped on the 47 samples out of 48 where Poll does not fire.
    int      encPending_ = 0;

    // Cheap change detection for the setters that are not cheap.
    float    lastToneApplied_ = -1.f;
};

}  // namespace ogham
