// -----------------------------------------------------------------------------
// Ogham for VCV Rack — libDaisy stand-in
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// The firmware's DSP files are compiled into this plugin UNMODIFIED, straight
// out of the `ogham/` submodule. Three of them reach for libDaisy, and this
// header is the whole of what they need:
//
//   ogham_cv_output.cpp   daisy::DacHandle          (writes the CV value)
//   ogham_display.cpp     daisy::System::GetNow     (flash timeouts)
//   tm1637.cpp            daisy::GPIO, DaisySeed,   (bit-bangs the display;
//                         daisy::System::DelayUs     harmless with no-op pins)
//
// Nothing here emulates hardware. GPIO writes go nowhere, delays do not delay.
// tm1637.cpp is still compiled verbatim rather than reimplemented, because
// TM1637::WriteSegments caches the four segment bytes BEFORE clocking them out
// (`lastSegs_`, read back via GetLastSegs) — so driving the real transport into
// dead pins yields exactly the bytes the hardware display would show, with no
// second copy of the segment font to drift out of step.
//
// This header must precede the submodule's src/ on the include path, and no
// real libDaisy header may be reachable at all.

#pragma once

#include <cstdint>

#include "ogham_clock.h"

namespace daisy {

// A pin identifier. The value is never used for anything; it exists so
// DaisySeed::GetPin has something to return and GPIO::Init something to take.
struct Pin {
    int index = -1;
};

// A GPIO that isn't. Write() stores, Read() returns what was stored — enough
// for the display transport, which never checks the ACK bit it reads.
class GPIO {
public:
    enum class Mode { INPUT, OUTPUT, OPEN_DRAIN, ANALOG };
    enum class Pull { NOPULL, PULLUP, PULLDOWN };
    enum class Speed { LOW, MEDIUM, HIGH, VERY_HIGH };

    void Init(Pin pin,
              Mode  mode  = Mode::INPUT,
              Pull  pull  = Pull::NOPULL,
              Speed speed = Speed::LOW) {
        pin_ = pin;
        mode_ = mode;
        (void)pull;
        (void)speed;
    }

    void Write(bool state) { state_ = state; }
    bool Read() const { return state_; }

private:
    Pin  pin_{};
    Mode mode_ = Mode::INPUT;
    bool state_ = true;   // open-drain idle is high
};

// Opaque. TM1637 holds a pointer to one and only ever calls GetPin().
class DaisySeed {
public:
    Pin GetPin(int index) const { return Pin{index}; }
};

// The internal DAC. CvOutput writes a 12-bit value here once per control tick;
// the host reads it back with Value(). One instance per module, so nothing is
// shared between plugin instances.
class DacHandle {
public:
    enum class Channel { ONE, TWO, BOTH };

    void WriteValue(Channel channel, uint16_t value) {
        if (channel == Channel::TWO) value2_ = value;
        else                         value1_ = value;
    }

    // 0..4095, as written by CvOutput::UpdateOutput.
    uint16_t Value(Channel channel = Channel::ONE) const {
        return channel == Channel::TWO ? value2_ : value1_;
    }

    // The same value as 0..1, which is what the CV output stage wants.
    float Normalized(Channel channel = Channel::ONE) const {
        return Value(channel) * (1.0f / 4095.0f);
    }

private:
    uint16_t value1_ = 0;
    uint16_t value2_ = 0;
};

// Time. GetNow/GetUs delegate to the process-wide clock in ogham_clock.h, so
// the Rack module can drive them from engine time and the offline renderer from
// a virtual clock — the display's flash timeouts then behave correctly even in
// a faster-than-real-time render. Delays are no-ops: the only caller is the
// display transport, and there is no wire to meet timing on.
struct System {
    static uint32_t GetNow() { return ogham::shim::NowMs(); }
    static uint32_t GetUs()  { return ogham::shim::NowUs(); }
    static void Delay(uint32_t ms)   { (void)ms; }
    static void DelayUs(uint32_t us) { (void)us; }
};

}  // namespace daisy
