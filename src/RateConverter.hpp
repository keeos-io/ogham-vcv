// -----------------------------------------------------------------------------
// Ogham for VCV Rack — the 48 kHz boundary
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// The firmware's DSP is written against a hard-coded 48 kHz — twenty-one
// constants across five files, including the tempo estimator's 100 Hz flux rate
// and every filter and envelope coefficient. Rack runs at whatever rate the user
// picked. Rather than edit the firmware (which this project does not do), the
// core runs at exactly 48 kHz and the rate change happens here.
//
// Per Rack sample:
//   1. advance the accumulator by 48000 / hostRate
//   2. run the core zero, one or more times, pushing each result
//   3. read the outputs back, interpolated to the host's timeline
//
// At 44.1 kHz that is usually one core step and sometimes two; at 96 kHz it
// alternates one and zero. At exactly 48 kHz the whole thing is bypassed: one
// core step per host sample, passed through untouched, bit-identical to the
// module and with no latency at all.
//
// Interpolation is a 4-point Catmull-Rom (Hermite) on the continuous outputs.
// The signal is an 8-bit stream at an 8 kHz tick rate whose aliasing IS the
// sound, so there is nothing to be gained by band-limiting it on the way out —
// what matters is not adding anything of our own. The window costs up to two
// core samples of delay, about 42 us, off the 48 kHz path only.
//
// Gates are never interpolated: EOC holds its last core value. A 10 ms pulse is
// 480 core samples, so no edge can be missed at any host rate.

#pragma once

#include <cstdint>

namespace ogham {

// The rate the firmware's DSP is written for, and the only rate it ever runs at.
static constexpr double kCoreSampleRate = 48000.0;

class RateConverter {
public:
    // How many continuous signals are carried across the boundary.
    // Out 1, Out 2 and ENV; gates are handled separately.
    static constexpr int kChannels = 3;

    void setHostRate(float hostRate) {
        if (hostRate <= 0.f) hostRate = (float)kCoreSampleRate;
        if (hostRate == hostRate_) return;
        hostRate_ = hostRate;
        // 32.32 fixed point, the same shape as the engine's own accumulator.
        const double ratio = kCoreSampleRate / (double)hostRate;
        increment_ = (uint64_t)(ratio * 4294967296.0 + 0.5);
        bypass_ = (increment_ == (1ull << 32));
        reset();
    }

    float hostRate() const { return hostRate_; }
    bool  bypassed() const { return bypass_; }

    // Latency in host samples, for Rack's latency reporting. Zero on the
    // bypass path; two core samples otherwise.
    float latency() const {
        return bypass_ ? 0.f : 2.f * (float)(hostRate_ / kCoreSampleRate);
    }

    void reset() {
        phase_ = 0;
        for (int c = 0; c < kChannels; c++)
            for (int i = 0; i < 4; i++) history_[c][i] = 0.f;
        gate_ = false;
        primed_ = false;
        pendSync_ = false;
        pendClock_ = false;
    }

    // Input edges, held until there is a core step to receive them.
    //
    // This is the converter's problem to solve rather than the caller's, because
    // the converter is what creates it: advance() can legitimately return zero,
    // and an edge arriving on such a host sample would otherwise be dropped
    // where it stands. It is not a rare corner. At 96 kHz the increment is
    // exactly 0.5, so the steps run 1, 0, 1, 0 and HALF of all host samples take
    // no core step; at 192 kHz it is three in four. Sync suffers most, being a
    // single-sample event that resets the waveform.
    //
    // Latch on every host sample; take on the first core step of the next sample
    // that runs one. An edge is therefore delayed by at most one host sample and
    // never lost, and two edges falling inside one core step collapse into one,
    // which is right — the core cannot reset twice in a step it runs once.
    void latchEdges(bool sync, bool clock) {
        pendSync_  = pendSync_  || sync;
        pendClock_ = pendClock_ || clock;
    }

    bool takeSync()  { const bool v = pendSync_;  pendSync_  = false; return v; }
    bool takeClock() { const bool v = pendClock_; pendClock_ = false; return v; }

    // Advance one host sample and report how many core steps to run.
    //
    // Call this, run the core that many times pushing each result with push(),
    // then read the outputs with read(). Priming the interpolation window costs
    // three extra core steps on the very first sample, once.
    int advance() {
        if (bypass_) return 1;

        int steps = 0;
        if (!primed_) {
            primed_ = true;
            steps += 3;             // fill the window before the first read
        }
        phase_ += increment_;
        while (phase_ >= (1ull << 32)) {
            phase_ -= (1ull << 32);
            steps++;
        }
        return steps;
    }

    // Push one core sample of every continuous channel, plus the gate state.
    void push(const float* channels, bool gate) {
        if (bypass_) {
            for (int c = 0; c < kChannels; c++) history_[c][2] = channels[c];
            gate_ = gate;
            return;
        }
        for (int c = 0; c < kChannels; c++) {
            history_[c][0] = history_[c][1];
            history_[c][1] = history_[c][2];
            history_[c][2] = history_[c][3];
            history_[c][3] = channels[c];
        }
        gate_ = gate;   // held, never interpolated
    }

    // The value of one channel at the host's current position.
    float read(int channel) const {
        const float* h = history_[channel];
        if (bypass_) return h[2];
        return hermite(h[0], h[1], h[2], h[3], frac());
    }

    bool gate() const { return gate_; }

private:
    // Position between h[1] and h[2]; h[3] is the one-sample lookahead the
    // window needs.
    float frac() const {
        return (float)((double)phase_ / 4294967296.0);
    }

    // Catmull-Rom. At t = 0 this returns p1 exactly, which is what makes the
    // 48 kHz case degenerate cleanly even before the bypass short-circuits it.
    static float hermite(float p0, float p1, float p2, float p3, float t) {
        const float a = 0.5f * (-p0 + 3.f * p1 - 3.f * p2 + p3);
        const float b = 0.5f * (2.f * p0 - 5.f * p1 + 4.f * p2 - p3);
        const float c = 0.5f * (-p0 + p2);
        return ((a * t + b) * t + c) * t + p1;
    }

    float    hostRate_  = 0.f;
    uint64_t increment_ = 1ull << 32;
    uint64_t phase_     = 0;
    bool     bypass_    = true;
    bool     primed_    = false;
    bool     pendSync_  = false;
    bool     pendClock_ = false;
    bool     gate_      = false;
    float    history_[kChannels][4] = {};
};

}  // namespace ogham
