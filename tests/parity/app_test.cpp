// -----------------------------------------------------------------------------
// Ogham for VCV Rack — OghamApp behaviour tests
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// The DSP is the firmware's own code and is verified by rendering it. The
// application layer is not: it is transcribed by hand from ogham_main.cpp, and
// this is the only automated check that the transcription behaves.
//
// It tests the things a listening test cannot pin down — that a clock at 120 BPM
// gives exactly 1x, that the ratio knob quantises to powers of two, that pulling
// the cable holds the tempo rather than snapping back to the knob, that a volt
// is an octave. Phase 5 adds the segment-level comparison against real hardware,
// which covers the menu; this covers the parts that have no display.
//
//   make -f tools/host.mk app && build_host/app_test

#include "OghamApp.hpp"
#include "formulas.h"

#include <cmath>
#include <cstdio>

namespace {

constexpr int kCore = 48000;

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

void checkNear(double got, double want, double tol, const char* what) {
    const bool ok = std::fabs(got - want) <= tol;
    std::printf("  %-58s %s  (got %.5f, want %.5f)\n",
                what, ok ? "ok" : "FAIL", got, want);
    if (!ok) failures++;
}

// Run the app for a while, optionally injecting a clock edge every `clockPeriod`
// core samples. Returns nothing; the caller inspects the app.
void run(ogham::OghamApp& app, ogham::AppInputs in, int samples,
         int clockPeriod = 0, int syncAt = -1) {
    ogham::AppOutputs out;
    for (int n = 0; n < samples; n++) {
        in.clockEdge = clockPeriod > 0 && (n % clockPeriod) == 0;
        in.syncEdge  = (n == syncAt);
        app.ProcessSample(in, out);
    }
}

ogham::AppInputs defaults() {
    ogham::AppInputs in;
    in.potA = in.potB = in.potRate = in.potTone = 0.5f;
    return in;
}

// --- encoder helpers -------------------------------------------------------
//
// The app derives all its gesture timing from its own sample counter, so a test
// spends milliseconds by running samples. 48 core samples is 1 ms.

void runMs(ogham::OghamApp& app, ogham::AppInputs in, int ms) {
    ogham::AppOutputs out;
    for (int n = 0; n < ms * 48; n++) {
        app.ProcessSample(in, out);
        in.encDelta = 0;          // detents are consumed once
    }
}

// A turn of `detents`, delivered on one control tick, `gapMs` after the last
// one — the gap is what selects the acceleration multiplier.
void turn(ogham::OghamApp& app, ogham::AppInputs in, int detents, int gapMs = 200) {
    runMs(app, in, gapMs);
    in.encDelta = detents;
    ogham::AppOutputs out;
    for (int n = 0; n < 48; n++) { app.ProcessSample(in, out); in.encDelta = 0; }
}

void click(ogham::OghamApp& app, ogham::AppInputs in, int holdMs) {
    in.encPressed = true;
    runMs(app, in, holdMs);
    in.encPressed = false;
    runMs(app, in, 20);
}

// --- the plugin's path -----------------------------------------------------
//
// A mouse cannot supply a clean button, so the widget classifies the gesture and
// hands over the result. These drive that path, which is the one the plugin
// actually uses; the raw-button helpers above cover the path a real button takes.

void clickEvent(ogham::OghamApp& app, ogham::AppInputs in) {
    in.encClicks = 1;
    ogham::AppOutputs out;
    for (int n = 0; n < 48; n++) { app.ProcessSample(in, out); in.encClicks = 0; }
    runMs(app, in, 20);
}

void longEvent(ogham::OghamApp& app, ogham::AppInputs in) {
    in.encLongPresses = 1;
    ogham::AppOutputs out;
    for (int n = 0; n < 48; n++) { app.ProcessSample(in, out); in.encLongPresses = 0; }
    runMs(app, in, 20);
}

}  // namespace

int main() {
    // -----------------------------------------------------------------------
    std::printf("Rate knob\n");
    {
        struct { float pot; double want; const char* what; } cases[] = {
            { 0.5f,  1.0,        "12 o'clock is exactly 1x" },
            { 0.0f,  1.0 / 64.0, "fully anticlockwise is 1/64x" },
            { 1.0f,  64.0,       "fully clockwise is 64x" },
            { 0.75f, 8.0,        "three quarters is 8x" },
        };
        for (auto& c : cases) {
            ogham::OghamApp app;
            app.Init();
            ogham::AppInputs in = defaults();
            in.potRate = c.pot;
            run(app, in, 96);
            checkNear(app.Rate(), c.want, c.want * 1e-5, c.what);
        }
    }

    // -----------------------------------------------------------------------
    // TEMPO_UNITY_HZ is 2 Hz: one pulse per beat at 120 BPM. A clock at that
    // rate must give exactly 1x, and double it must give 2x.
    // -----------------------------------------------------------------------
    std::printf("Clock In\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        run(app, in, kCore * 2, kCore / 2);       // 2 Hz = 120 BPM
        check(app.ExternalClock(), "a clock at 120 BPM is detected");
        checkNear(app.Rate(), 1.0, 0.01, "and gives 1x");
    }
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        run(app, in, kCore * 2, kCore / 4);       // 4 Hz = 240 BPM
        checkNear(app.Rate(), 2.0, 0.01, "a clock at 240 BPM gives 2x");
    }
    {
        // With a clock present the Rate knob is a quantised multiply/divide.
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        in.potRate = 1.0f;                        // hard clockwise = x32
        run(app, in, kCore * 2, kCore / 2);
        checkNear(app.Rate(), 32.0, 0.5, "the knob hard clockwise is x32 of the clock");

        ogham::OghamApp app2;
        app2.Init();
        ogham::AppInputs in2 = defaults();
        in2.potRate = 0.0f;                       // hard anticlockwise = /32
        run(app2, in2, kCore * 2, kCore / 2);
        checkNear(app2.Rate(), 1.0 / 32.0, 0.01, "and hard anticlockwise is /32");
    }
    {
        // Pull the cable: the tempo is HELD, not snapped back to the knob.
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        in.potRate = 0.5f;
        run(app, in, kCore * 2, kCore / 4);       // 4 Hz -> 2x
        const double clocked = app.Rate();
        run(app, in, kCore * 2);                  // ... and stop
        check(!app.ExternalClock(), "the clock times out when edges stop");
        check(app.ClockHeld(), "and the rate is held rather than reverted");
        checkNear(app.Rate(), clocked, 1e-6, "the held rate is the clocked rate");

        // Moving the Rate knob past the deadband exits the hold.
        in.potRate = 0.75f;
        run(app, in, 480);
        check(!app.ClockHeld(), "moving Rate exits the hold");
        checkNear(app.Rate(), 8.0, 0.01, "and the knob takes over");
    }

    // -----------------------------------------------------------------------
    std::printf("V/oct\n");
    {
        // VOCT_RATE_TUNE puts 0 V at C1. A volt is an octave, exactly.
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        in.voctMode = true;
        in.voctVolts = 0.f;
        run(app, in, 96);
        const double base = app.Rate();
        checkNear(base, 1.04618, 1e-4, "0 V is the tuned base rate");

        in.voctVolts = 1.f;
        run(app, in, 96);
        checkNear(app.Rate(), base * 2.0, base * 1e-4, "+1 V doubles the rate");

        in.voctVolts = -2.f;
        run(app, in, 96);
        checkNear(app.Rate(), base / 4.0, base * 1e-4, "-2 V quarters it");

        // The Rate knob becomes a bipolar fine tune, +-12 semitones.
        in.voctVolts = 0.f;
        in.potRate = 1.0f;
        run(app, in, 96);
        checkNear(app.Rate(), base * 2.0, base * 1e-4, "the knob hard clockwise is +1 octave");
    }

    // -----------------------------------------------------------------------
    std::printf("A440 reference\n");
    {
        ogham::OghamApp app;
        app.Init();
        app.SetFormula1(GetReferenceIndex());
        ogham::AppInputs in = defaults();
        in.potRate = 0.9f;                        // knob says otherwise
        run(app, in, kCore / 4, kCore / 2);       // so does a clock
        checkNear(app.Rate(), 1.0, 1e-6,
                  "the reference slot pins the rate to 1x regardless");
    }

    // -----------------------------------------------------------------------
    std::printf("Sync\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        run(app, in, 4800);
        const uint32_t before = app.Engine().GetT();
        run(app, in, 480, 0, 0);                  // sync on the first sample
        const uint32_t after = app.Engine().GetT();
        check(before > 0, "t advances");
        check(after < before, "a sync edge restarts the waveform");
    }

    // -----------------------------------------------------------------------
    std::printf("Parameters\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        in.potA = 1.0f;
        in.potB = 0.0f;
        run(app, in, 96);
        checkNear(app.Engine().GetParamA(), 255, 0, "A at full scale is 255");
        checkNear(app.Engine().GetParamB(), 0, 0, "B at zero is 0");

        // CV sums with the knob and clamps.
        in.potA = 0.5f;
        in.cvA  = 0.5f;                           // +2.5 V
        run(app, in, 96);
        checkNear(app.Engine().GetParamA(), 255, 0, "knob + CV clamps at 255");

        in.cvA = -0.25f;
        run(app, in, 96);
        checkNear(app.Engine().GetParamA(), 64, 1, "and subtracts below the knob");
    }

    // -----------------------------------------------------------------------
    std::printf("Encoder gestures\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();

        check(!app.InMenu(), "starts in function select");
        check(app.SelectedVoice() == 0, "with voice 1 selected");

        click(app, in, 50);                     // short
        check(app.SelectedVoice() == 1, "a short click switches voice");
        click(app, in, 50);
        check(app.SelectedVoice() == 0, "and switches back");

        click(app, in, 700);                    // long, past LONG_PRESS_MS
        check(app.InMenu(), "a long press enters the menu");
        check(!app.Editing(), "in navigate mode");

        click(app, in, 50);
        check(app.Editing(), "a short click enters edit");
        click(app, in, 50);
        check(!app.Editing(), "and commits back to navigate");

        click(app, in, 700);
        check(!app.InMenu(), "a long press leaves the menu");
    }

    // -----------------------------------------------------------------------
    std::printf("Menu navigation\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        click(app, in, 700);

        turn(app, in, +1);
        check(app.MenuField() == 1, "one detent moves one field");
        turn(app, in, +4);
        check(app.MenuField() == 5, "four more moves four");

        // No wrap: a hard crank clamps at the end rather than jumping to the far
        // side of the list.
        for (int i = 0; i < 10; i++) turn(app, in, +5);
        check(app.MenuField() == FX_NUM_FIELDS - 1, "cranking clamps at the last field");
        for (int i = 0; i < 20; i++) turn(app, in, -5);
        check(app.MenuField() == 0, "and at the first — no wrap either way");

        // Re-entry lands on the field you left, not field 0.
        turn(app, in, +7);
        const int left = app.MenuField();
        click(app, in, 700);                    // leave
        click(app, in, 700);                    // and come back
        check(app.MenuField() == left, "re-entry returns to the field you left");
        check(!app.Editing(), "and never resumes mid-edit");
    }

    // -----------------------------------------------------------------------
    std::printf("Menu editing\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        click(app, in, 700);                    // into the menu, field 0

        // Field 0 is the global on/off: CW is on, CCW is off, whatever the
        // detent count.
        click(app, in, 50);                     // edit
        turn(app, in, -1);
        check(app.MenuValue(0) == 0, "global off, turning anticlockwise");
        turn(app, in, +1);
        check(app.MenuValue(0) == 1, "and on, clockwise");
        click(app, in, 50);                     // commit

        // Field 2 is the chorus level, 0..99, accelerated like a param.
        turn(app, in, +2);
        check(app.MenuField() == 2, "navigated to the chorus level");
        // The module boots with a gentle chorus already on, so the level starts
        // at 45 rather than 0 — DefaultFxChain, not something this test sets.
        const int chorusAtBoot = app.MenuValue(2);
        check(chorusAtBoot == 45, "the chorus level boots at its default 45");
        click(app, in, 50);
        turn(app, in, +10);
        check(app.MenuValue(2) == chorusAtBoot + 10,
              "ten detents is ten counts at a slow turn");
        for (int i = 0; i < 12; i++) turn(app, in, +10);
        check(app.MenuValue(2) == 99, "and it clamps at 99");
        click(app, in, 50);

        // The type sub-field clamps to FX_TYPE_MAX rather than 99.
        turn(app, in, +1);
        check(app.MenuField() == 3, "navigated to the chorus type");
        click(app, in, 50);
        turn(app, in, +5);
        check(app.MenuValue(3) == FX_TYPE_MAX, "a type field clamps to the variant");
        click(app, in, 50);

        // Hold is a power-of-two window shown as the tick count, one detent per
        // step whatever the turn speed.
        ogham::OghamApp app2;
        app2.Init();
        ogham::AppInputs in2 = defaults();
        click(app2, in2, 700);
        for (int i = 0; i < FX_FIELD_CVHOLD; i++) turn(app2, in2, +1);
        check(app2.MenuField() == FX_FIELD_CVHOLD, "navigated to CV Out hold");
        click(app2, in2, 50);
        turn(app2, in2, +1);
        check(app2.MenuValue(FX_FIELD_CVHOLD) == 2, "one step is a window of 2");
        turn(app2, in2, +1);
        check(app2.MenuValue(FX_FIELD_CVHOLD) == 4, "then 4 — powers of two");
        for (int i = 0; i < 10; i++) turn(app2, in2, +1);
        check(app2.MenuValue(FX_FIELD_CVHOLD) == 256, "clamping at 256");
    }

    // -----------------------------------------------------------------------
    // The whole gesture vocabulary, in order, on the path the plugin uses:
    //
    //   drag             change the function
    //   click            select the other voice
    //   hold             enter the menu
    //   drag             change the menu item
    //   click            edit that item
    //   drag             change its value
    //   click            back to item select
    //   hold             back to function select
    // -----------------------------------------------------------------------
    std::printf("The gesture vocabulary\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();

        // Read the defaults rather than assuming them: the engine boots Out 2
        // on function 1, not 0, "so Out2 differs from Out1".
        const int f2AtBoot = app.Formula2();
        check(f2AtBoot == 1, "voice 2 boots on a different function to voice 1");

        turn(app, in, +5);
        check(app.Formula1() == 5, "a turn changes the function");
        check(app.Formula2() == f2AtBoot, "and only the selected voice's");

        clickEvent(app, in);
        check(app.SelectedVoice() == 1, "a click selects the other voice");
        turn(app, in, +3);
        check(app.Formula2() == f2AtBoot + 3, "which the turn then changes");
        check(app.Formula1() == 5, "leaving the first alone");

        longEvent(app, in);
        check(app.InMenu(), "a hold enters the menu");
        check(!app.Editing(), "in item select");

        turn(app, in, +4);
        check(app.MenuField() == 4, "a turn changes the menu item");

        clickEvent(app, in);
        check(app.Editing(), "a click edits that item");
        const int before = app.MenuValue(4);
        turn(app, in, +6);
        check(app.MenuValue(4) == before + 6, "a turn changes its value");
        check(app.Editing(), "and stays in edit while it does");

        turn(app, in, +2);
        check(app.MenuValue(4) == before + 8, "turning again keeps changing it");
        check(app.MenuField() == 4, "without wandering off the item");

        clickEvent(app, in);
        check(!app.Editing(), "a click returns to item select");
        check(app.InMenu(), "still in the menu");
        turn(app, in, -1);
        check(app.MenuField() == 3, "where a turn moves items again");
        check(app.MenuValue(4) == before + 8, "and the value it left is untouched");

        longEvent(app, in);
        check(!app.InMenu(), "a hold returns to function select");
        turn(app, in, +1);
        check(app.Formula2() == f2AtBoot + 4, "where a turn is a function again");
    }

    // -----------------------------------------------------------------------
    std::printf("Restoring the mode directly\n");
    {
        // SetMenuMode is how a restored patch arrives in the right mode. The
        // gesture path goes through the long press, above.
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();

        app.SetMenuMode(true);
        check(app.InMenu(), "restoring puts the encoder in the menu");
        check(!app.Editing(), "never arriving mid-edit");

        // The symptom that started this: enter edit, then turn, and keep
        // turning. Every detent must land on the value, and none of them may
        // fall back out of edit.
        //
        // On a field with a range, not field 0 — that one is the global on/off,
        // which is already on, so turning it clockwise is a no-op and would
        // prove nothing.
        turn(app, in, +2);
        const int field = app.MenuField();
        check(field == 2, "navigated to a field with a range");
        click(app, in, 50);
        check(app.Editing(), "a click enters edit");
        const int before = app.MenuValue(field);
        turn(app, in, +1);
        turn(app, in, +1);
        turn(app, in, +1);
        check(app.Editing(), "turning does not fall out of edit");
        check(app.MenuValue(field) != before, "and the value follows the turns");

        app.SetMenuMode(false);
        check(!app.InMenu(), "and takes it back to function select");
        check(!app.Editing(), "never leaving edit hanging");
    }

    // -----------------------------------------------------------------------
    std::printf("Display\n");
    {
        ogham::OghamApp app;
        app.Init();
        ogham::AppInputs in = defaults();
        runMs(app, in, 100);
        const uint32_t voiceView = app.DisplaySegments();
        check(voiceView != 0, "the display shows something in function select");

        click(app, in, 700);
        runMs(app, in, 100);
        check(app.DisplaySegments() != voiceView, "and something else in the menu");

        // Selecting the A440 reference changes what the voice view reads.
        click(app, in, 700);
        app.SetFormula1(GetReferenceIndex());
        runMs(app, in, 100);
        check(app.DisplaySegments() != voiceView, "and something else again for AA");
    }

    std::printf("\n%s\n", failures == 0 ? "PASS - the transcription behaves"
                                        : "FAIL - see above");
    return failures == 0 ? 0 : 1;
}
