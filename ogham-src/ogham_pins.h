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
// Ogham Eurorack Module — Pin Assignments
// Source of truth: KiCad schematic v0.4 (ERC clean)
// See hardware/ogham/ERC_manual_review.md for full pin table

namespace ogham {

// --- Encoder (directly connected, active-low with pull-ups) ---
constexpr int ENC_A  = 0;   // GPIO0, Seed pin 1
constexpr int ENC_B  = 1;   // GPIO1, Seed pin 2
constexpr int ENC_SW = 2;   // GPIO2, Seed pin 3

// --- Gate / Clock (active-high after transistor/comparator inversion) ---
constexpr int GATE_IN  = 5;  // GPIO5, Seed pin 6  — via MMBT3904 NPN
constexpr int CLK_IN   = 6;  // GPIO6, Seed pin 7  — via LM393 comparator
constexpr int GATE_OUT = 7;  // GPIO7, Seed pin 8  — via 74AHCT1G125 buffer

// --- TM1637 Display ---
constexpr int TM1637_CLK = 9;   // GPIO9,  Seed pin 10
constexpr int TM1637_DIO = 10;  // GPIO10, Seed pin 11 — open-drain, 10k pull-up

// --- ADC Channels (D15-D21 = ADC0-ADC6) ---
constexpr int POT_A     = 15;  // ADC0, Seed pin 22
constexpr int POT_B     = 16;  // ADC1, Seed pin 23
constexpr int POT_RATE  = 17;  // ADC2, Seed pin 24
constexpr int POT_LEVEL = 18;  // ADC3, Seed pin 25
constexpr int CV_A      = 19;  // ADC4, Seed pin 26 — via MCP6004 (inverted)
constexpr int CV_B      = 20;  // ADC5, Seed pin 27 — via MCP6004 (inverted)
constexpr int VOCT_ADC  = 21;  // ADC6, Seed pin 28 (A6/D21) — V/oct tap on the Clock jack
constexpr int NUM_ADC_CHANNELS = 7;

// --- Mode toggle: Clock (tempo) <-> V/oct (pitch) ---
// Dailywell SPDT: common (pin 2) -> GND, throw (pin 1) -> this GPIO w/ pull-up.
constexpr int MODE_SW   = 8;   // GPIO8, Seed pin 9 (D8 / PG11)

// --- DAC Output ---
constexpr int CV_OUT_DAC = 23;  // DAC_OUT1 / PA4, Seed pin 30

}  // namespace ogham
