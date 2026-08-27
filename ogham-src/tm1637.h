// -----------------------------------------------------------------------------
// Ogham — a dual-voice bytebeat synthesizer for Eurorack
//
// Author:     Steven Collins, 2026, Keeos.io
// Copyright:  (c) 2026 Steven Collins
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
//
// This file is part of the Ogham firmware. See LICENSE-firmware.txt at the
// repository root for the full licence text.
// https://github.com/keeos-io/ogham
// -----------------------------------------------------------------------------

#pragma once
#include "daisy_seed.h"

class TM1637 {
public:
    // Initialize with CLK and DIO GPIO pin numbers
    void Init(daisy::DaisySeed* seed, int clkPin, int dioPin);

    // Display 4 raw characters (using segment encoding)
    // chars[0] is leftmost digit
    void ShowChars(uint8_t chars[4]);

    // Display a prefix character + 3-digit number (e.g., 'A' + 238)
    void ShowPrefixNumber(char prefix, int number, bool dpClean = false);

    // Set brightness (0-7)
    void SetBrightness(uint8_t level);

    // Clear display
    void Clear();

    // Encode a character to 7-segment pattern
    static uint8_t Encode(char c);

    // Last 4 segment bytes written (for the PC monitor; reflects exactly what
    // would be on the display even when no module is physically wired).
    const uint8_t* GetLastSegs() const { return lastSegs_; }

    // Boot splash helper: light a single segment on the outer 12-segment border
    // (pos 0-11, clockwise from top-left). Used by the startup chase.
    void ShowBorderSegment(int pos);

private:
    daisy::DaisySeed* seed_ = nullptr;
    daisy::GPIO clkGpio_;
    daisy::GPIO dioGpio_;
    uint8_t brightness_ = 4;
    uint8_t lastSegs_[4] = {0, 0, 0, 0};

    void Start();
    void Stop();
    void WriteByte(uint8_t data);
    void WriteSegments(const uint8_t* segments, uint8_t length);

    void DelayUs();
    void SetDioOutput();
    void SetDioInput();
    void WriteDio(bool high);
    void WriteClk(bool high);
    bool ReadDio();
};
