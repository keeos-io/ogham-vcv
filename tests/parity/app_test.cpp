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

    std::printf("\n%s\n", failures == 0 ? "PASS - the transcription behaves"
                                        : "FAIL - see above");
    return failures == 0 ? 0 : 1;
}
