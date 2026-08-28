// -----------------------------------------------------------------------------
// Ogham for VCV Rack — multi-instance test
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// Rack runs many copies of a module in one process. The firmware it is
// transcribed from runs exactly one, and its state lives in file-scope globals —
// so the single largest risk in this port is that something did not become a
// member and instances quietly share it.
//
// A missed static does not crash. It bleeds: one module's Rate knob nudges
// another's, or two instances share a clock, and the symptom is intermittent and
// blamed on everything else first. So this does not test that eight instances
// run — it tests that each one produces EXACTLY what it produces alone.
//
// Method: render each configuration by itself and hash its output, then render
// all eight interleaved, sample by sample, as Rack would, and hash each again.
// Every hash must match. Anything shared shows up as a mismatch, because the
// configurations are chosen to differ in the things a shared variable would
// carry: function, parameters, rate, tone, and menu state.
//
// It also reports what eight instances cost, which is the number a Rack user
// judges a plugin by.
//
//   make -f tools/host.mk multi && build_host/multi_test

#include "OghamApp.hpp"
#include "shim/ogham_clock.h"
#include "formulas.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

constexpr int kCore = 48000;
constexpr int kInstances = 8;
constexpr double kSeconds = 4.0;

int failures = 0;

struct Setup {
    const char* name;
    int   f1, f2;
    float potA, potB, potRate, potTone;
    int   menuField, menuValue;
    bool  voctMode;
    float voctVolts;
    int   clockPeriod;      // core samples between edges, 0 = none
};

// Eight configurations that differ in everything a shared variable could carry.
const Setup kSetups[kInstances] = {
    {"melodic, free",      84,  60, 0.50f, 0.25f, 0.50f, 0.50f,  0,  1, false, 0.f,      0},
    {"rhythmic, clocked",  66,  12, 0.75f, 0.60f, 0.50f, 0.50f,  2, 70, false, 0.f, kCore/2},
    {"percussive, LPG",    44,  90, 0.30f, 0.80f, 0.62f, 0.20f, 18, 40, false, 0.f,      0},
    {"noise, crushed",     25,   7, 0.90f, 0.10f, 0.40f, 0.85f, 10, 55, false, 0.f,      0},
    {"textural, drone",     3,  81, 0.55f, 0.45f, 0.55f, 0.50f, 21,  1, false, 0.f,      0},
    {"voct, low",          88,  33, 0.20f, 0.70f, 0.50f, 0.45f, 14,  2, true, -1.5f,     0},
    {"voct, high",         88,  33, 0.20f, 0.70f, 0.50f, 0.45f, 14,  2, true,  2.0f,     0},
    {"reference tone",    100,  50, 0.50f, 0.50f, 0.90f, 0.50f, 17,  4, false, 0.f, kCore/4},
};

uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

void configure(ogham::OghamApp& app, const Setup& s) {
    app.Init();
    app.SetFormula1(s.f1);
    app.SetFormula2(s.f2);
    app.SetMenuValue(s.menuField, s.menuValue);
}

ogham::AppInputs inputsFor(const Setup& s) {
    ogham::AppInputs in;
    in.potA = s.potA; in.potB = s.potB;
    in.potRate = s.potRate; in.potTone = s.potTone;
    in.voctMode = s.voctMode;
    in.voctVolts = s.voctVolts;
    return in;
}

}  // namespace

int main() {
    const int samples = (int)(kSeconds * kCore);

    // --- each configuration alone --------------------------------------------
    std::vector<uint64_t> solo(kInstances, 0);
    std::vector<std::vector<float>> soloTrace(kInstances, std::vector<float>(samples, 0.f));
    std::vector<std::vector<float>> grpTrace(kInstances, std::vector<float>(samples, 0.f));
    for (int i = 0; i < kInstances; i++) {
        ogham::shim::VirtualClock clock(kCore);
        clock.Install();

        ogham::OghamApp app{};   // value-initialised, as the vector below is
        configure(app, kSetups[i]);
        ogham::AppInputs in = inputsFor(kSetups[i]);
        ogham::AppOutputs out;

        uint64_t h = 1469598103934665603ull;
        for (int n = 0; n < samples; n++) {
            in.clockEdge = kSetups[i].clockPeriod && (n % kSetups[i].clockPeriod) == 0;
            app.ProcessSample(in, out);
            h = fnv1a(h, &out.out1, sizeof(out.out1));
            h = fnv1a(h, &out.out2, sizeof(out.out2));
            h = fnv1a(h, &out.env,  sizeof(out.env));
            soloTrace[i][n] = out.out1 + 3.f * out.out2 + 7.f * out.env;
            clock.Advance();
        }
        solo[i] = h;
    }

    // --- all eight interleaved, as Rack would run them ------------------------
    ogham::shim::VirtualClock clock(kCore);
    clock.Install();

    std::vector<ogham::OghamApp> apps(kInstances);
    std::vector<ogham::AppInputs> ins;
    std::vector<uint64_t> together(kInstances, 1469598103934665603ull);
    for (int i = 0; i < kInstances; i++) {
        configure(apps[i], kSetups[i]);
        ins.push_back(inputsFor(kSetups[i]));
    }

    const auto t0 = std::chrono::steady_clock::now();
    ogham::AppOutputs out;
    for (int n = 0; n < samples; n++) {
        for (int i = 0; i < kInstances; i++) {
            ins[i].clockEdge = kSetups[i].clockPeriod && (n % kSetups[i].clockPeriod) == 0;
            apps[i].ProcessSample(ins[i], out);
            together[i] = fnv1a(together[i], &out.out1, sizeof(out.out1));
            together[i] = fnv1a(together[i], &out.out2, sizeof(out.out2));
            together[i] = fnv1a(together[i], &out.env,  sizeof(out.env));
            grpTrace[i][n] = out.out1 + 3.f * out.out2 + 7.f * out.env;
        }
        clock.Advance();
    }
    const double wall = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    // --- results --------------------------------------------------------------
    std::printf("Eight instances, %.0f s of audio each\n\n", kSeconds);
    for (int i = 0; i < kInstances; i++) {
        const bool ok = solo[i] == together[i];
        if (!ok) failures++;
        std::printf("  %-20s %s   alone %016llx   together %016llx\n",
                    kSetups[i].name, ok ? "ok  " : "FAIL",
                    (unsigned long long)solo[i], (unsigned long long)together[i]);
    }

    for (int i = 0; i < kInstances; i++) {
        if (solo[i] == together[i]) continue;
        int first = -1, differ = 0;
        for (int n = 0; n < samples; n++)
            if (soloTrace[i][n] != grpTrace[i][n]) { if (first < 0) first = n; differ++; }
        std::printf("      %-16s first differs at sample %d (%.4f s); %d of %d\n",
                    kSetups[i].name, first, first / (double)kCore, differ, samples);
    }

    const double realtime = (kSeconds * kInstances) / wall;
    std::printf("\n  %.2f s of wall clock for %.0f s of audio across %d instances\n",
                wall, kSeconds * kInstances, kInstances);
    std::printf("  %.0fx real time in total, %.1f%% of one core for all eight\n",
                realtime, 100.0 / realtime * kInstances);
    std::printf("  about %.2f%% of a core each\n", 100.0 / realtime);

    std::printf("\n%s\n", failures == 0
        ? "PASS - every instance renders exactly what it renders alone"
        : "FAIL - instances are sharing state");
    return failures == 0 ? 0 : 1;
}
