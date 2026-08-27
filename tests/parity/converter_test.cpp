// -----------------------------------------------------------------------------
// Ogham for VCV Rack — RateConverter tests
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// The boundary is the only new DSP in the plugin, so it gets tested on its own,
// with a synthetic core, before it is trusted with audio. It has no Rack
// dependency, which is what makes this possible.
//
//   make -f tools/host.mk converter && build_host/converter_test

#include "RateConverter.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

void checkNear(double got, double want, double tol, const char* what) {
    const bool ok = std::fabs(got - want) <= tol;
    std::printf("  %-58s %s  (got %.6f, want %.6f)\n",
                what, ok ? "ok" : "FAIL", got, want);
    if (!ok) failures++;
}

// A core that emits a known ramp, one sample per step.
//
// The step is a negative power of two, which matters more than it looks. An
// unscaled ramp reaches 48000 over a host second, where a float's spacing is
// ~0.004 — larger than the interpolation error being measured, so the test
// would be reading its own rounding rather than the converter's behaviour. Even
// at 1e-3 the products are inexact and consecutive differences wobble by an ulp.
// At 1/1024 every value is exact up to n = 2^23, so a slope error is the
// converter's and nobody else's.
struct RampCore {
    static constexpr double kStep = 1.0 / 1024.0;
    double n = 0.0;
    float next() { const float v = (float)(n * kStep); n += 1.0; return v; }
};

}  // namespace

int main() {
    using ogham::RateConverter;

    // -----------------------------------------------------------------------
    std::printf("48 kHz — the bypass path\n");
    {
        RateConverter c;
        c.setHostRate(48000.f);
        check(c.bypassed(), "bypassed at exactly 48 kHz");
        checkNear(c.latency(), 0.0, 0.0, "zero latency");

        RampCore core;
        bool everyStepIsOne = true;
        bool passthroughExact = true;
        for (int i = 0; i < 1000; i++) {
            const int steps = c.advance();
            if (steps != 1) everyStepIsOne = false;
            float v = 0.f;
            for (int s = 0; s < steps; s++) {
                v = core.next();
                const float ch[3] = { v, -v, 0.5f };
                c.push(ch, (i % 7) == 0);
            }
            if (c.read(0) != v) passthroughExact = false;
        }
        check(everyStepIsOne, "exactly one core step per host sample");
        check(passthroughExact, "output is the core sample, bit for bit");
    }

    // -----------------------------------------------------------------------
    // The core must run at 48 kHz whatever the host does: over a second of host
    // samples, the number of core steps must be 48000, give or take the one
    // sample the accumulator can be mid-way through.
    // -----------------------------------------------------------------------
    const float rates[] = { 44100.f, 48000.f, 88200.f, 96000.f, 192000.f };
    for (float hostRate : rates) {
        std::printf("%.0f Hz\n", hostRate);
        RateConverter c;
        c.setHostRate(hostRate);

        RampCore core;
        long totalSteps = 0;
        int  primingSteps = 0;
        int  maxStepsInOneSample = 0;
        const int hostSamples = (int)hostRate;   // one second

        std::vector<float> out;
        out.reserve(hostSamples);

        for (int i = 0; i < hostSamples; i++) {
            const int steps = c.advance();
            // The first sample also primes the interpolation window, once.
            if (i == 0) primingSteps = steps;
            else if (steps > maxStepsInOneSample) maxStepsInOneSample = steps;
            totalSteps += steps;
            for (int s = 0; s < steps; s++) {
                const float v = core.next();
                const float ch[3] = { v, -v, 0.25f };
                c.push(ch, false);
            }
            out.push_back(c.read(0));
        }

        // 3 of these are the one-off priming of the interpolation window.
        const long expected = 48000 + (c.bypassed() ? 0 : 3);
        checkNear((double)totalSteps, (double)expected, 1.0,
                  "core runs at 48 kHz over one host second");
        check(maxStepsInOneSample <= (hostRate < 48000.f ? 2 : 1),
              "steady state runs no more core steps than the ratio needs");
        // Priming is 3 core steps, plus whatever the first host sample's own
        // accumulator crossing asks for — one when the host runs at or below
        // 48 kHz, none above it, and the bypass path primes nothing.
        const int wantPriming = c.bypassed() ? 1 : (hostRate <= 48000.f ? 4 : 3);
        check(primingSteps == wantPriming,
              "the window is primed once, on the first sample");

        // A ramp through the interpolator must come out a ramp: same slope,
        // monotonic, no ripple. This is what proves the window is aligned — a
        // misordered history shows up here as a sawtooth.
        double maxSlopeError = 0.0, maxAbs = 0.0;
        bool monotonic = true;
        const double wantSlope = RampCore::kStep * 48000.0 / hostRate;
        for (size_t i = 21; i + 1 < out.size(); i++) {
            const double slope = out[i + 1] - out[i];
            const double err = std::fabs(slope - wantSlope);
            if (err > maxSlopeError) maxSlopeError = err;
            if (std::fabs(out[i]) > maxAbs) maxAbs = std::fabs(out[i]);
            if (out[i + 1] < out[i]) monotonic = false;
        }
        check(monotonic, "a ramp stays monotonic");

        // Catmull-Rom reproduces a straight line exactly in exact arithmetic,
        // so a misaligned history shows up here as ripple. In float it cannot
        // do better than a few ulp: the coefficients are differences of
        // same-magnitude terms, which cancels hard. The two rates with
        // non-binary ratios land around 0.6 ulp of the ramp's magnitude; the
        // ones whose ratio is an exact power of two are exact. Four ulp is a
        // tolerance that still catches a genuine misalignment, which would be
        // orders of magnitude larger.
        const double tol = 4.0 * std::numeric_limits<float>::epsilon() * maxAbs;
        checkNear(maxSlopeError, 0.0, tol, "a ramp keeps its slope");
    }

    // -----------------------------------------------------------------------
    std::printf("gates\n");
    {
        RateConverter c;
        c.setHostRate(96000.f);   // half the host samples run no core step
        const float ch[3] = { 0.f, 0.f, 0.f };
        c.advance();
        c.push(ch, true);
        bool heldAcrossIdleSamples = true;
        for (int i = 0; i < 8; i++) {
            const int steps = c.advance();
            for (int s = 0; s < steps; s++) c.push(ch, false);
            if (steps == 0 && !c.gate()) heldAcrossIdleSamples = false;
            if (steps > 0) break;
        }
        c.setHostRate(44100.f);
        c.advance();
        c.push(ch, true);
        check(c.gate(), "a gate set by the last core step reads back high");
        check(heldAcrossIdleSamples, "a gate holds through host samples that run no core step");
    }

    std::printf("\n%s\n", failures == 0 ? "PASS - converter behaves"
                                        : "FAIL - see above");
    return failures == 0 ? 0 : 1;
}
