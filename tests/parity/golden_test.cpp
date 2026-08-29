// -----------------------------------------------------------------------------
// Ogham for VCV Rack — golden renders
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// Renders a fixed set of configurations and checks each one against a stored
// hash. This is what stops the sound changing by accident: a refactor, a
// compiler upgrade, a firmware re-sync, or a "harmless" tidy in the transcribed
// layer will all show up here as a mismatch, and nowhere else.
//
//   make -f tools/host.mk golden        build
//   build_host/golden_test              check against tests/parity/golden/
//   build_host/golden_test --write      re-record, after an intended change
//
// It drives OghamApp rather than the DSP objects directly, because that is where
// the risk is. The voices are the firmware's own code compiled unmodified, so
// their output is right by construction; the menu, the CV output modes, the
// clock tracking and V/oct are transcribed from ogham_main.cpp by hand, and are
// the part that can drift.
//
// Coverage is deliberately mechanical rather than curated: every menu field in
// turn, every FX stage in both its variants, every CV output mode, and each way
// the time base can be driven. A field nobody thought to test is the one that
// breaks.
//
// **Re-recording is not a fix.** A mismatch means the rendered audio changed. If
// that was intended, --write and say so in the commit; if it was not, the diff
// of this file is the bug report. The stored peak and RMS figures are there to
// tell those apart at a glance: a hash that moves while every level stays put is
// numerical drift, and a level that halves is something real.
//
// Determinism, which this all rests on: the app has one apparently random
// element, the CV capture phase re-rolled when the Rate knob moves. In the
// firmware it is seeded from the microsecond timer; here it is seeded from the
// engine's own position, so that a patch reloads identically. That makes even
// the rate-sweep case below reproducible.

#include "OghamApp.hpp"
#include "shim/ogham_clock.h"
#include "formulas.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int    kCore    = 48000;
constexpr double kSeconds = 3.0;
constexpr int    kBlocks  = 16;    // RMS envelope points per channel
const char*      kGoldenPath = "tests/parity/golden/renders.txt";

// How far a stored figure may move before it counts as a change in behaviour.
//
// Measured rather than guessed, and the measurement is worth keeping. g++ 14 and
// clang 19 on Windows hash every one of these renders identically. Under Linux
// glibc, four of them differ — libm's transcendentals are not bit-identical
// between implementations, and the FX chain and the lo-fi filter are full of
// them. Three of those four move by under 1e-4. The fourth, `tone-crush`, moves
// by 4.0e-4: it is the resonant band-pass at full sweep, and a resonant filter
// amplifies a last-bit difference, shifting a little energy between blocks while
// its peak and RMS stay identical to six decimal places.
//
// So 1e-3, which is 2.5x the worst drift observed and still two to three orders
// of magnitude below any real change — a wrong menu field or a changed formula
// moves a level by percent, not by a thousandth.
//
// Note what this does NOT weaken: on the machine that recorded the file, and any
// other with the same libm, the hash is exact and nothing slips past it. The
// tolerance only relaxes the check where the numbers cannot be identical anyway.
constexpr float kTolerance = 1e-3f;

// A menu setting to apply before rendering. field < 0 terminates the list.
struct Setting {
    int field = -1;
    int value = 0;
};

struct Case {
    std::string name;
    int   f1 = 0, f2 = 1;
    float potA = 0.5f, potB = 0.5f, potRate = 0.5f, potTone = 0.5f;
    bool  voctMode = false;
    float voctVolts = 0.f;
    int   clockPeriod = 0;    // core samples between clock edges, 0 = none
    int   syncPeriod  = 0;    // core samples between sync edges, 0 = none
    float rateEnd     = -1.f; // if >= 0, sweep the Rate knob here across the run
    Setting menu[4];
};

struct Result {
    uint64_t hash = 0;
    float peak[3] = {0.f, 0.f, 0.f};   // out1, out2, cv
    float rms[3]  = {0.f, 0.f, 0.f};
    float env[3][kBlocks] = {{0.f}};   // RMS per block: shape over time
    int   eoc = 0;                     // rising edges
};

// The largest fractional move between two results, over every stored figure.
// Fractional rather than absolute so a quiet channel is held to the same
// standard as a loud one.
float Deviation(const Result& a, const Result& b) {
    float worst = 0.f;
    auto cmp = [&](float x, float y) {
        const float scale = std::fmax(1e-3f, std::fmax(std::fabs(x), std::fabs(y)));
        worst = std::fmax(worst, std::fabs(x - y) / scale);
    };
    for (int i = 0; i < 3; i++) {
        cmp(a.peak[i], b.peak[i]);
        cmp(a.rms[i], b.rms[i]);
        for (int k = 0; k < kBlocks; k++) cmp(a.env[i][k], b.env[i][k]);
    }
    return worst;
}

uint64_t fnv1a(uint64_t h, const void* data, size_t n) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
    return h;
}

Result Render(const Case& c) {
    ogham::shim::VirtualClock clock(kCore);
    clock.Install();

    ogham::OghamApp app{};
    app.Init();
    app.SetFormula1(c.f1);
    app.SetFormula2(c.f2);
    for (const Setting& s : c.menu) {
        if (s.field < 0) break;
        app.SetMenuValue(s.field, s.value);
    }

    ogham::AppInputs in;
    in.potA = c.potA; in.potB = c.potB;
    in.potRate = c.potRate; in.potTone = c.potTone;
    in.voctMode = c.voctMode;
    in.voctVolts = c.voctVolts;

    ogham::AppOutputs out;
    Result r;
    r.hash = 1469598103934665603ull;

    const int samples = (int)(kSeconds * kCore);
    const int blockLen = samples / kBlocks;
    double sum[3] = {0.0, 0.0, 0.0};
    double blockSum[3] = {0.0, 0.0, 0.0};
    bool lastEoc = false;

    for (int n = 0; n < samples; n++) {
        in.clockEdge = c.clockPeriod && (n % c.clockPeriod) == 0;
        in.syncEdge  = c.syncPeriod  && n && (n % c.syncPeriod) == 0;

        // A swept Rate knob, for the cases that want one. Linear across the run,
        // which crosses the re-roll deadband many times — the point being that
        // it must land on the same phases every time.
        if (c.rateEnd >= 0.f)
            in.potRate = c.potRate + (c.rateEnd - c.potRate) * ((float)n / samples);

        app.ProcessSample(in, out);

        const float v[3] = {out.out1, out.out2, out.env};
        for (int i = 0; i < 3; i++) {
            const float a = std::fabs(v[i]);
            if (a > r.peak[i]) r.peak[i] = a;
            sum[i] += (double)v[i] * v[i];
        }
        for (int i = 0; i < 3; i++) blockSum[i] += (double)v[i] * v[i];
        if ((n % blockLen) == blockLen - 1) {
            const int b = n / blockLen;
            if (b < kBlocks)
                for (int i = 0; i < 3; i++) {
                    r.env[i][b] = (float)std::sqrt(blockSum[i] / blockLen);
                    blockSum[i] = 0.0;
                }
        }

        if (out.eoc && !lastEoc) r.eoc++;
        lastEoc = out.eoc;

        r.hash = fnv1a(r.hash, v, sizeof(v));
        r.hash = fnv1a(r.hash, &out.eoc, sizeof(out.eoc));
        clock.Advance();
    }

    for (int i = 0; i < 3; i++) r.rms[i] = (float)std::sqrt(sum[i] / samples);
    return r;
}

// --- the cases ---------------------------------------------------------------

// One representative value per menu field, so every field is exercised without
// anyone having to remember to add a case. Indexed by field number; see the FX
// menu table in CLAUDE.md for what each one means.
const int kFieldValue[22] = {
    1,    // 0  chain on
    1,    // 1  parallel
    60,   // 2  chorus level
    1,    // 3  chorus type -> ensemble
    40,   // 4  chorus rate
    70,   // 5  chorus depth
    60,   // 6  flanger level
    1,    // 7  flanger type -> barber-pole
    30,   // 8  flanger rate
    55,   // 9  flanger feedback
    60,   // 10 phaser level
    1,    // 11 phaser type -> bi-phase
    45,   // 12 phaser rate
    60,   // 13 phaser 2nd LFO
    2,    // 14 CV out mode -> dc1
    40,   // 15 slew rise
    70,   // 16 slew fall
    4,    // 17 hold window
    35,   // 18 LPG on, medium decay
    1,    // 19 timbre route -> A
    3,    // 20 param interpolation grid
    1,    // 21 Out2 drone
};

std::vector<Case> BuildCases() {
    std::vector<Case> v;

    // Baselines: one per character of the bank, at default settings. If these
    // move, something central moved.
    v.push_back({"base-default"});
    v.push_back({"base-textural",   3,  17});
    v.push_back({"base-noise",     25,  31});
    v.push_back({"base-percussive",44,  52});
    v.push_back({"base-rhythmic",  66,  71});
    v.push_back({"base-melodic",   84,  90});
    v.push_back({"base-reference", 100, 100});

    // Every menu field in turn, on a common base.
    for (int f = 0; f < 22; f++) {
        Case c;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "field-%02d", f);
        c.name = buf;
        c.f1 = 60; c.f2 = 84;
        c.menu[0] = {f, kFieldValue[f]};
        v.push_back(c);
    }

    // Each FX stage, both variants, with the stage actually audible. The menu
    // sweep above sets one field at a time; these set a whole stage, which is
    // the state a patch is really in.
    struct Stage { const char* name; int base; int rate; int flavour; };
    const Stage stages[3] = {
        {"chorus",  2, 40, 70},
        {"flanger", 6, 30, 55},
        {"phaser", 10, 45, 60},
    };
    for (const Stage& s : stages) {
        for (int type = 0; type < 2; type++) {
            Case c;
            c.name = std::string("fx-") + s.name + (type ? "-variant" : "-clean");
            c.f1 = 12; c.f2 = 66;
            c.menu[0] = {s.base + 0, 65};        // level
            c.menu[1] = {s.base + 1, type};      // type
            c.menu[2] = {s.base + 2, s.rate};
            c.menu[3] = {s.base + 3, s.flavour};
            v.push_back(c);
        }
    }

    // All four CV output modes, on a voice with a strong envelope.
    const char* cvMode[4] = {"env1", "env2", "dc1", "dc2"};
    for (int m = 0; m < 4; m++) {
        Case c;
        c.name = std::string("cv-") + cvMode[m];
        c.f1 = 44; c.f2 = 60;
        c.menu[0] = {14, m};
        c.menu[1] = {17, 3};     // a hold window, so the DC modes have shape
        v.push_back(c);
    }

    // The three ways the time base can be driven.
    { Case c; c.name = "time-free";     c.f1 = 66; c.potRate = 0.62f; v.push_back(c); }
    { Case c; c.name = "time-clocked";  c.f1 = 66; c.clockPeriod = kCore / 2; v.push_back(c); }
    { Case c; c.name = "time-synced";   c.f1 = 44; c.syncPeriod  = kCore / 4; v.push_back(c); }
    { Case c; c.name = "time-voct-low"; c.f1 = 88; c.voctMode = true; c.voctVolts = -1.5f; v.push_back(c); }
    { Case c; c.name = "time-voct-high";c.f1 = 88; c.voctMode = true; c.voctVolts =  2.0f; v.push_back(c); }

    // The tone macro at both extremes and at its clean centre.
    { Case c; c.name = "tone-fold";  c.f1 = 60; c.potTone = 0.0f; v.push_back(c); }
    { Case c; c.name = "tone-clean"; c.f1 = 60; c.potTone = 0.5f; v.push_back(c); }
    { Case c; c.name = "tone-crush"; c.f1 = 60; c.potTone = 1.0f; v.push_back(c); }

    // A moving Rate knob, which re-rolls the CV capture phase as it crosses the
    // deadband. Here to prove that stays reproducible.
    { Case c; c.name = "rate-sweep"; c.f1 = 66; c.potRate = 0.30f; c.rateEnd = 0.80f;
      c.menu[0] = {14, 2}; v.push_back(c); }

    // Both parameters at their extremes, which is where a formula is most likely
    // to do something degenerate.
    { Case c; c.name = "params-min"; c.f1 = 33; c.potA = 0.f; c.potB = 0.f; v.push_back(c); }
    { Case c; c.name = "params-max"; c.f1 = 33; c.potA = 1.f; c.potB = 1.f; v.push_back(c); }

    // Everything at once: three variant stages, the LPG, a drone and a clock.
    { Case c; c.name = "kitchen-sink"; c.f1 = 66; c.f2 = 12;
      c.clockPeriod = kCore / 2; c.potTone = 0.72f;
      c.menu[0] = {2, 55}; c.menu[1] = {3, 1}; c.menu[2] = {18, 30}; c.menu[3] = {21, 1};
      v.push_back(c); }

    return v;
}

// --- the golden file ---------------------------------------------------------

bool ReadGoldens(std::map<std::string, Result>& out) {
    std::ifstream f(kGoldenPath);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream in(line);
        std::string name, hash;
        Result r;
        in >> name >> hash >> r.eoc;
        for (int i = 0; i < 3; i++) in >> r.peak[i] >> r.rms[i];
        for (int i = 0; i < 3; i++)
            for (int k = 0; k < kBlocks; k++) in >> r.env[i][k];
        if (name.empty() || hash.empty() || !in) continue;
        r.hash = std::strtoull(hash.c_str(), nullptr, 16);
        out[name] = r;
    }
    return true;
}

void WriteGoldens(const std::vector<Case>& cases,
                  const std::vector<Result>& results) {
    std::ofstream f(kGoldenPath);
    f << "# Ogham for VCV Rack - golden renders. Generated by "
         "build_host/golden_test --write.\n"
         "#\n"
         "# One line per configuration, rendered for " << kSeconds
      << " seconds at " << kCore << " Hz through\n"
         "# OghamApp. The hash is exact and the levels are compared with a\n"
         "# tolerance, because libm is not bit-identical between platforms: a\n"
         "# hash that moves while the levels hold is the same audio rounded\n"
         "# differently, and that is reported rather than failed.\n"
         "#\n"
         "# Do not edit by hand, and do not re-record to make a failure go away.\n"
         "#\n"
         "# name  hash  eoc  peak/rms for out1, out2, cv, then a "
      << kBlocks << "-point RMS envelope each\n";
    for (size_t i = 0; i < cases.size(); i++) {
        const Result& r = results[i];
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%-26s %016llx %4d ",
                      cases[i].name.c_str(), (unsigned long long)r.hash, r.eoc);
        f << buf;
        for (int k = 0; k < 3; k++) {
            std::snprintf(buf, sizeof(buf), " %.6f %.6f", r.peak[k], r.rms[k]);
            f << buf;
        }
        for (int k = 0; k < 3; k++)
            for (int b = 0; b < kBlocks; b++) {
                std::snprintf(buf, sizeof(buf), " %.6f", r.env[k][b]);
                f << buf;
            }
        f << "\n";
    }
}

}  // namespace

int main(int argc, char** argv) {
    const bool write = (argc > 1 && std::strcmp(argv[1], "--write") == 0);

    const std::vector<Case> cases = BuildCases();
    std::vector<Result> results;
    results.reserve(cases.size());
    for (const Case& c : cases) results.push_back(Render(c));

    if (write) {
        WriteGoldens(cases, results);
        std::printf("Wrote %zu golden renders to %s\n",
                    cases.size(), kGoldenPath);
        return 0;
    }

    std::map<std::string, Result> golden;
    if (!ReadGoldens(golden)) {
        std::printf("golden_test: %s not found.\n"
                    "  Run from the repository root, or record it with:\n"
                    "    build_host/golden_test --write\n", kGoldenPath);
        return 2;
    }

    int failed = 0, missing = 0, drifted = 0;
    float worstDrift = 0.f;

    for (size_t i = 0; i < cases.size(); i++) {
        const std::string& name = cases[i].name;
        auto it = golden.find(name);
        if (it == golden.end()) {
            std::printf("  NEW   %-26s not in the golden file\n", name.c_str());
            missing++;
            continue;
        }

        const Result& was = it->second;
        const Result& now = results[i];
        if (was.hash == now.hash) continue;

        // The hash moved. Whether that matters is a question about the levels:
        // a different libm rounds the same audio differently, and that must not
        // fail a build. Anything that moves a level is a change in behaviour.
        const float dev = Deviation(was, now);
        if (dev <= kTolerance && was.eoc == now.eoc) {
            drifted++;
            worstDrift = std::fmax(worstDrift, dev);
            continue;
        }

        failed++;
        std::printf("  FAIL  %-26s worst deviation %.3g (tolerance %.0e)\n",
                    name.c_str(), dev, kTolerance);
        const char* ch[3] = {"out1", "out2", "cv  "};
        for (int k = 0; k < 3; k++) {
            if (std::fabs(was.peak[k] - now.peak[k]) <= kTolerance &&
                std::fabs(was.rms[k] - now.rms[k]) <= kTolerance) continue;
            std::printf("          %s  peak %.6f -> %.6f   rms %.6f -> %.6f\n",
                        ch[k], was.peak[k], now.peak[k], was.rms[k], now.rms[k]);
        }
        if (was.eoc != now.eoc)
            std::printf("          eoc   %d -> %d\n", was.eoc, now.eoc);
    }

    for (const auto& kv : golden) {
        bool found = false;
        for (const Case& c : cases) if (c.name == kv.first) { found = true; break; }
        if (!found) {
            std::printf("  GONE  %-26s in the golden file, no such case\n",
                        kv.first.c_str());
            missing++;
        }
    }

    std::printf("\n%zu renders, %.0f s of audio each\n", cases.size(), kSeconds);
    if (drifted)
        std::printf("%d render%s hashed differently but held every level within "
                    "%.0e (worst %.3g)\n"
                    "  — the same audio, rounded differently by another libm\n",
                    drifted, drifted == 1 ? "" : "s", kTolerance, worstDrift);
    if (failed == 0 && missing == 0) {
        std::printf("PASS - every render matches its golden\n");
        return 0;
    }
    if (failed)
        std::printf("FAIL - %d render%s changed. If that was intended, "
                    "re-record with --write and say so in the commit.\n",
                    failed, failed == 1 ? "" : "s");
    if (missing)
        std::printf("FAIL - %d case%s added or removed; re-record with --write\n",
                    missing, missing == 1 ? "" : "s");
    return 1;
}
