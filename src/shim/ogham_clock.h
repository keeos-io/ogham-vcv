// -----------------------------------------------------------------------------
// Ogham for VCV Rack — process-wide millisecond clock
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// ogham_display.cpp calls System::GetNow() for its flash timeouts. That is a
// free function with no instance to hang off, which is correct: it asks for
// wall-clock milliseconds, and every module instance agrees on the time.
//
// It is routed through a hook so the caller can decide what "now" means:
//
//   Rack module      engine frames / sample rate — advances with audio, so the
//                    display behaves the same whether or not the host is
//                    running faster than real time.
//   Offline renderer a virtual clock stepped by the harness, so a 60-second
//                    render that takes two seconds still shows each flash for
//                    its 2.5 seconds of *rendered* time.
//   Default          the system's steady clock, for anything that just wants
//                    the obvious behaviour.
//
// Set the source once at startup. It is process-wide and read from the audio
// thread, so the setter is not for use while audio is running.

#pragma once

#include <cstdint>

namespace ogham {
namespace shim {

// Microseconds since an arbitrary origin. Milliseconds are derived from it, so
// a host only has to provide one function.
using MicroClock = uint64_t (*)();

// Install a clock source. Passing nullptr restores the default steady clock.
void SetClockSource(MicroClock source);

uint32_t NowMs();
uint32_t NowUs();

// A monotonic clock the caller advances by hand, for offline rendering.
// Advance() is expected to be called once per rendered sample.
class VirtualClock {
public:
    explicit VirtualClock(double sampleRate) : usPerSample_(1e6 / sampleRate) {}

    void Advance(uint32_t samples = 1) { us_ += usPerSample_ * samples; }
    uint64_t Micros() const { return static_cast<uint64_t>(us_); }

    // Install this instance as the process-wide clock source.
    void Install();

private:
    double us_ = 0.0;
    double usPerSample_ = 0.0;
};

}  // namespace shim
}  // namespace ogham
