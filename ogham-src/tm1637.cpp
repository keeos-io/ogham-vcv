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

#include "tm1637.h"
#include "ogham_pins.h"

using namespace daisy;

// TM1637 commands
static constexpr uint8_t CMD_DATA    = 0x40;  // Data command: auto-increment
static constexpr uint8_t CMD_ADDR    = 0xC0;  // Address command: start at 0
static constexpr uint8_t CMD_DISPLAY = 0x88;  // Display on + brightness (0-7)

// Segment bits (.GFEDCBA)
enum { SEG_A = 0x01, SEG_B = 0x02, SEG_C = 0x04, SEG_D = 0x08,
       SEG_E = 0x10, SEG_F = 0x20, SEG_G = 0x40 };

// Outer 12-segment border loop, clockwise from top-left (for the startup chase).
static const struct { uint8_t digit; uint8_t seg; } BORDER[12] = {
    {0, SEG_A}, {1, SEG_A}, {2, SEG_A}, {3, SEG_A},  // top: a, left->right
    {3, SEG_B}, {3, SEG_C},                          // right: b, c
    {3, SEG_D}, {2, SEG_D}, {1, SEG_D}, {0, SEG_D},  // bottom: d, right->left
    {0, SEG_E}, {0, SEG_F},                          // left: e, f (bottom->top)
};

void TM1637::Init(DaisySeed* seed, int clkPin, int dioPin) {
    seed_ = seed;
    brightness_ = 4;

    // CLK: push-pull output
    clkGpio_.Init(seed->GetPin(clkPin), GPIO::Mode::OUTPUT,
                  GPIO::Pull::NOPULL, GPIO::Speed::LOW);

    // DIO: open-drain output (schematic has 10k pull-up)
    dioGpio_.Init(seed->GetPin(dioPin), GPIO::Mode::OPEN_DRAIN,
                  GPIO::Pull::NOPULL, GPIO::Speed::LOW);

    WriteClk(true);
    WriteDio(true);

    Clear();
}

void TM1637::DelayUs() {
    // 50us bit timing: the original 5us was too fast for the module/wiring on the
    // mule (blank/flicker); 50us is reliable and imperceptible at 30Hz.
    System::DelayUs(50);
}

void TM1637::WriteDio(bool high) {
    dioGpio_.Write(high);
}

void TM1637::WriteClk(bool high) {
    clkGpio_.Write(high);
}

bool TM1637::ReadDio() {
    return dioGpio_.Read();
}

void TM1637::Start() {
    // DIO falls while CLK is high
    WriteDio(true);
    WriteClk(true);
    DelayUs();
    WriteDio(false);
    DelayUs();
    WriteClk(false);
    DelayUs();
}

void TM1637::Stop() {
    // DIO rises while CLK is high
    WriteClk(false);
    DelayUs();
    WriteDio(false);
    DelayUs();
    WriteClk(true);
    DelayUs();
    WriteDio(true);
    DelayUs();
}

void TM1637::WriteByte(uint8_t data) {
    // Send 8 bits LSB first
    for (int i = 0; i < 8; i++) {
        WriteClk(false);
        DelayUs();
        WriteDio(data & 0x01);
        DelayUs();
        WriteClk(true);
        DelayUs();
        data >>= 1;
    }

    // ACK: release DIO, pulse CLK, read ACK (ignored)
    WriteClk(false);
    WriteDio(true);  // Release (pull-up takes over)
    DelayUs();
    WriteClk(true);
    DelayUs();
    // We don't check ACK — TM1637 is write-only for us
    WriteClk(false);
    DelayUs();
}

void TM1637::WriteSegments(const uint8_t* segments, uint8_t length) {
    // Cache for telemetry (exact bytes that would light the display)
    for (uint8_t i = 0; i < 4; i++) lastSegs_[i] = (i < length) ? segments[i] : 0;

    // Set data command: auto-increment address
    Start();
    WriteByte(CMD_DATA);
    Stop();

    // Set address and write segment data
    Start();
    WriteByte(CMD_ADDR);
    for (uint8_t i = 0; i < length; i++) {
        WriteByte(segments[i]);
    }
    Stop();

    // Set display control: on + brightness
    Start();
    WriteByte(CMD_DISPLAY | (brightness_ & 0x07));
    Stop();
}

void TM1637::ShowChars(uint8_t chars[4]) {
    WriteSegments(chars, 4);
}

void TM1637::ShowPrefixNumber(char prefix, int number, bool dpClean) {
    if (number < 0) number = 0;
    if (number > 999) number = 999;

    uint8_t segs[4];
    segs[0] = Encode(prefix);
    segs[1] = Encode('0' + (number / 100) % 10);
    segs[2] = Encode('0' + (number / 10) % 10);
    segs[3] = Encode('0' + number % 10);

    // Suppress leading zeros in the 3-digit part
    if (number < 100) segs[1] = 0;
    if (number < 10) segs[2] = 0;

    // Far-right DP = Lo-Fi clean-center indicator; shown in every mode,
    // including the A/B param flash (matches ShowLabeled / ShowFxEdit).
    if (dpClean) segs[3] |= 0x80;

    WriteSegments(segs, 4);
}

void TM1637::SetBrightness(uint8_t level) {
    if (level > 7) level = 7;
    brightness_ = level;
}

void TM1637::Clear() {
    uint8_t segs[4] = {0, 0, 0, 0};
    WriteSegments(segs, 4);
}

void TM1637::ShowBorderSegment(int pos) {
    uint8_t segs[4] = {0, 0, 0, 0};
    if (pos >= 0 && pos < 12) segs[BORDER[pos].digit] |= BORDER[pos].seg;
    WriteSegments(segs, 4);
}

uint8_t TM1637::Encode(char c) {
    switch (c) {
        case '0': return 0x3F;
        case '1': return 0x06;
        case '2': return 0x5B;
        case '3': return 0x4F;
        case '4': return 0x66;
        case '5': return 0x6D;
        case '6': return 0x7D;
        case '7': return 0x07;
        case '8': return 0x7F;
        case '9': return 0x6F;
        case 'A': case 'a': return 0x77;
        case 'b':           return 0x7C;
        case 'C': case 'c': return 0x39;
        case 'd':           return 0x5E;
        case 'E': case 'e': return 0x79;
        case 'F': case 'f': return 0x71;
        case 'H': case 'h': return 0x76;
        case 'L': case 'l': return 0x38;
        case 'n':           return 0x54;
        case 'o':           return 0x5C;
        case 'P': case 'p': return 0x73;
        case 'q':           return 0x67;
        case 'r':           return 0x50;
        case 't':           return 0x78;
        case 'u':           return 0x1C;
        case '-':           return 0x40;
        case '_':           return 0x08;
        case ' ':           return 0x00;
        // Dot modifier: OR with 0x80
        case '.':           return 0x80;
        default:            return 0x00;
    }
}
