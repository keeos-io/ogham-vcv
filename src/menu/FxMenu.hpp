// -----------------------------------------------------------------------------
// Ogham for VCV Rack — right-click menus
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// The panel is the module's, and on the module the only way to a function is to
// turn the encoder past it, and the only way to a setting is to walk 22 fields
// four digits at a time. That is what a 10 HP panel affords. A screen affords
// more, and withholding it would be pretending to a constraint we do not have.
//
// So: the same state, reachable a second way. The function browser shows all 101
// slots by category and by name — something the module cannot do at all — and
// the menu mirror shows every field with its value at once. Neither adds a
// setting the module does not have.

#pragma once

#include "plugin.hpp"
#include "OghamApp.hpp"
#include "formulas.h"

#include <string>
#include <vector>

namespace ogham {

// The bank is grouped by character, twenty of each, in slot order.
struct Category { const char* name; int first, last; };
static const Category kCategories[] = {
    {"Textural",   0,  19},
    {"Noise",     20,  39},
    {"Percussive",40,  59},
    {"Rhythmic",  60,  79},
    {"Melodic",   80,  99},
};

// The 22 menu fields, in the order the encoder walks them. `options` names the
// values of a field that has a small fixed set; a null list means it is a
// number, and `max` bounds it.
struct FieldSpec {
    const char* name;
    const char* detail;
    std::vector<std::string> options;   // empty = numeric
    int max;
};

inline const std::vector<FieldSpec>& FieldSpecs() {
    static const std::vector<FieldSpec> specs = {
        {"FX chain",          "Global bypass",                  {"Off", "On"}, 1},
        {"Chain topology",    "How the three stages are wired", {"Serial", "Parallel"}, 1},

        {"Chorus level",      "0 skips the stage entirely",     {}, 99},
        {"Chorus type",       "Clean, or three detuned voices", {"Clean", "Ensemble"}, 1},
        {"Chorus rate",       "",                               {}, 99},
        {"Chorus depth",      "",                               {}, 99},

        {"Flanger level",     "0 skips the stage entirely",     {}, 99},
        {"Flanger type",      "Clean, or an endless sweep",     {"Clean", "Barber-pole"}, 1},
        {"Flanger rate",      "50 stops a barber-pole sweep",   {}, 99},
        {"Flanger feedback",  "",                               {}, 99},

        {"Phaser level",      "0 skips the stage entirely",     {}, 99},
        {"Phaser type",       "Four poles, or eight and a 2nd LFO", {"Clean", "Bi-phase"}, 1},
        {"Phaser rate",       "",                               {}, 99},
        {"Phaser 2nd LFO",    "Ratio, in bi-phase",             {}, 99},

        {"ENV Out mode",      "Envelope follower, or the raw bytebeat as a voltage",
                              {"Envelope of Out 1", "Envelope of Out 2",
                               "Out 1 as DC", "Out 2 as DC"}, 3},
        {"ENV slew rise",     "Off to about 5 s",               {}, 99},
        {"ENV slew fall",     "Independent of the rise",        {}, 99},
        {"ENV hold",          "Sample and hold, DC modes only",
                              {"Off", "2 ticks", "4", "8", "16", "32", "64", "128", "256"}, 8},
        {"LPG decay",         "0 is off; 1-99 is 2 ms to 20 s", {}, 99},
        {"CV to Tone",        "Borrow a CV input to modulate the Tone macro",
                              {"Off", "CV A", "CV B"}, 2},
        {"Parameter smoothing", "Crossfade A and B across a grid",
                              {"Off", "4", "8", "16", "32", "64", "128"}, 128},
        {"Out 2 drone",       "Freeze Out 2 as an independent voice",
                              {"Coupled", "Frozen"}, 1},
    };
    return specs;
}

// The parameter grid is a list, not a range: its stored value is the step size.
inline const std::vector<uint8_t>& QuantSteps() {
    static const std::vector<uint8_t> v = {0, 4, 8, 16, 32, 64, 128};
    return v;
}

inline std::string FunctionLabelFor(int index) {
    const FormulaInfo* f = GetFormulaAt(index);
    const std::string name = (f && f->name) ? f->name : "?";
    if (index == GetReferenceIndex()) return "AA  " + name;
    return string::f("F%02d  %s", index, name.c_str());
}

}  // namespace ogham
