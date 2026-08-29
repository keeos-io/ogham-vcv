// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the sample-rate boundary, checked by its invariants
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
//   make -f tools/host.mk boundary
//
// The golden renders all run at exactly 48 kHz, where the converter is bypassed
// entirely — so they prove nothing whatever about this. Everything here is about
// what happens at the other rates, which is what most users are on.
//
// It is a canary, deliberately. It answers "is the boundary broken" and not "is
// the resampling good", because the first question is cheap to answer exactly
// and the second needs a definition of correctness per rate that nobody has.
//
// The trick that makes it cheap: every check below is an invariant that holds at
// EVERY rate, so there is no golden per rate and no tolerance to argue about.
//
//   1. Core-step conservation. Over N host samples at rate R, the core must run
//      exactly N * 48000/R times. That is counting, not comparing. A wrong
//      ratio shows immediately; fixed-point phase drift shows as a slow
//      divergence, which is why the run is a minute and not a second.
//
//   2. Edge conservation. Every sync and clock edge must reach the core. This is
//      not guaranteed by construction: at 96 kHz the increment is exactly 0.5,
//      so advance() returns 1, 0, 1, 0 and HALF of all host samples run no core
//      steps; at 192 kHz it is three in four. An edge arriving on one of those
//      samples has nowhere to go unless something holds it.
//
//   3. Nothing is NaN or infinite, at any rate.
//
//   4. Level is within a few percent of the 48 kHz render. Resampling preserves
//      energy; it does not halve or double it. Catches gross aliasing, silence,
//      and a converter that has stopped reading.
//
// Block size is deliberately absent. Rack gives a module one hook,
// process(const ProcessArgs&), called once per sample — there is no
// processBundle — and nothing here holds per-block state, so a block boundary is
// not an event this code can observe. Sizes 16 to 2048 would all be the same
// test run four times.

#include "OghamApp.hpp"
#include "RateConverter.hpp"
#include "shim/ogham_clock.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures = 0;

void Check(bool ok, const char* what) {
    std::printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

const float kRates[] = { 44100.f, 48000.f, 88200.f, 96000.f, 192000.f };

// --- 1. core-step conservation ----------------------------------------------

void StepConservation() {
    std::printf("\nCore steps consumed over 60 s, against 60 x 48000:\n");
    const double seconds = 60.0;

    for (float rate : kRates) {
        ogham::RateConverter conv;
        conv.setHostRate(rate);

        const long hostSamples = (long)(seconds * rate);
        long steps = 0;
        for (long n = 0; n < hostSamples; n++) steps += conv.advance();

        const long expected = (long)(seconds * ogham::kCoreSampleRate);
        const long slack = 4;   // the 3 priming steps, plus one of rounding
        char label[96];
        std::snprintf(label, sizeof(label),
                      "%7.0f Hz  %ld steps, %+ld against expected",
                      rate, steps, steps - expected);
        Check(std::labs(steps - expected) <= slack, label);
    }
}

// --- 2. edge conservation ----------------------------------------------------
//
// This mirrors the loop in Ogham::process, because the host tests cannot include
// Rack. The latching it relies on lives in RateConverter, which is the real
// code: the hazard is created by a converter that can return zero steps, so the
// remedy belongs there rather than in each caller.

void EdgeConservation() {
    std::printf("\nSync edges delivered to the core, of 200 sent:\n");
    const int kEdges = 200;

    for (float rate : kRates) {
        ogham::shim::VirtualClock clock((int)rate);
        clock.Install();

        ogham::RateConverter conv;
        conv.setHostRate(rate);
        ogham::OghamApp app{};
        app.Init();

        ogham::AppInputs in;
        ogham::AppOutputs out;

        // One edge every 5 ms, so they land on every phase of the ratio.
        const int spacing = (int)(rate * 0.005f);
        const long hostSamples = (long)spacing * kEdges + spacing;
        int delivered = 0;

        for (long n = 0; n < hostSamples; n++) {
            const bool sync = (n % spacing) == 0 && n > 0 && (n / spacing) <= kEdges;
            conv.latchEdges(sync, false);

            const int steps = conv.advance();
            for (int i = 0; i < steps; i++) {
                in.syncEdge = (i == 0) && conv.takeSync();
                in.clockEdge = (i == 0) && conv.takeClock();
                if (in.syncEdge) delivered++;
                app.ProcessSample(in, out);
                const float ch[ogham::RateConverter::kChannels] = {
                    out.out1, out.out2, out.env };
                conv.push(ch, out.eoc);
            }
        }

        char label[96];
        std::snprintf(label, sizeof(label), "%7.0f Hz  %d of %d delivered",
                      rate, delivered, kEdges);
        Check(delivered == kEdges, label);
    }
}

// --- 3 and 4. finite output, and the level it should be ----------------------

struct Level {
    double rms = 0.0;
    bool   finite = true;
};

Level RenderAt(float rate) {
    ogham::shim::VirtualClock clock((int)rate);
    clock.Install();

    ogham::RateConverter conv;
    conv.setHostRate(rate);
    ogham::OghamApp app{};
    app.Init();
    app.SetFormula1(66);
    app.SetFormula2(84);
    app.SetMenuValue(2, 55);    // a chorus, so the FX chain is in the path
    app.SetMenuValue(18, 40);   // and the LPG, so there is an envelope

    ogham::AppInputs in;
    in.potTone = 0.62f;
    ogham::AppOutputs out;

    const long hostSamples = (long)(rate * 4.f);
    double sum = 0.0;
    Level lv;

    for (long n = 0; n < hostSamples; n++) {
        conv.latchEdges((n % (long)(rate / 2)) == 0 && n > 0, false);
        const int steps = conv.advance();
        for (int i = 0; i < steps; i++) {
            in.syncEdge = (i == 0) && conv.takeSync();
            in.clockEdge = false;
            app.ProcessSample(in, out);
            const float ch[ogham::RateConverter::kChannels] = {
                out.out1, out.out2, out.env };
            conv.push(ch, out.eoc);
        }
        const float v = conv.read(0);
        if (!std::isfinite(v)) lv.finite = false;
        sum += (double)v * v;
    }

    lv.rms = std::sqrt(sum / hostSamples);
    return lv;
}

void LevelAndFiniteness() {
    std::printf("\nOut 1 RMS against the 48 kHz render, and finiteness:\n");

    const Level ref = RenderAt(48000.f);
    Check(ref.finite && ref.rms > 0.01, "48000 Hz  reference renders, non-silent");

    for (float rate : kRates) {
        if (rate == 48000.f) continue;
        const Level lv = RenderAt(rate);
        const double ratio = (ref.rms > 0.0) ? lv.rms / ref.rms : 0.0;

        char label[96];
        std::snprintf(label, sizeof(label), "%7.0f Hz  rms %.4f, %.1f%% of 48 kHz",
                      rate, lv.rms, ratio * 100.0);
        // Ten percent: resampling a signal whose aliasing IS the sound moves the
        // level a little, and the point here is to catch halving, not to
        // characterise the interpolator.
        Check(lv.finite && ratio > 0.90 && ratio < 1.10, label);
    }
}

}  // namespace

int main() {
    std::printf("The 48 kHz boundary, by invariants that hold at every rate.\n");

    StepConservation();
    EdgeConservation();
    LevelAndFiniteness();

    std::printf("\n%s\n", failures == 0
        ? "PASS - the boundary holds at every rate"
        : "FAIL - see above");
    return failures == 0 ? 0 : 1;
}
