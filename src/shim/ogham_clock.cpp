// -----------------------------------------------------------------------------
// Ogham for VCV Rack — process-wide millisecond clock
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------

#include "ogham_clock.h"

#include <atomic>
#include <chrono>

namespace ogham {
namespace shim {
namespace {

uint64_t SteadyMicros() {
    using namespace std::chrono;
    static const steady_clock::time_point origin = steady_clock::now();
    return static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now() - origin).count());
}

std::atomic<MicroClock> g_source{&SteadyMicros};

// The VirtualClock currently installed, if any. One at a time is all an offline
// render needs, and the audio thread never touches it.
VirtualClock* g_virtual = nullptr;

uint64_t VirtualMicros() { return g_virtual ? g_virtual->Micros() : 0; }

}  // namespace

void SetClockSource(MicroClock source) {
    g_source.store(source ? source : &SteadyMicros, std::memory_order_relaxed);
}

uint32_t NowUs() {
    return static_cast<uint32_t>(g_source.load(std::memory_order_relaxed)());
}

uint32_t NowMs() {
    return static_cast<uint32_t>(g_source.load(std::memory_order_relaxed)() / 1000ull);
}

void VirtualClock::Install() {
    g_virtual = this;
    SetClockSource(&VirtualMicros);
}

}  // namespace shim
}  // namespace ogham
