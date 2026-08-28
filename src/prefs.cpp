// -----------------------------------------------------------------------------
// Ogham for VCV Rack — settings that belong to the machine, not the patch
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------

#include "plugin.hpp"
#include "prefs.hpp"

namespace ogham {
namespace prefs {
namespace {

// Under Rack's user directory, beside the plugins folder — so it survives an
// update of the plugin, and is per installation rather than per patch.
const char* kDir  = "Keeos";
const char* kFile = "Keeos/settings.json";

// On, everywhere, including macOS — and that is a correction.
//
// This defaulted off on macOS for one revision, in answer to a report that
// dragging over the encoder changed the function instead of moving around the
// patch. The gesture in question turned out to be a two-finger trackpad swipe,
// which is a SCROLL, not a drag: the wrong event was disabled, and the reported
// one carried on doing exactly what was complained about. That is fixed where it
// actually lives, in EncoderWidget::onHoverScroll.
//
// Off on macOS is also no longer a safe default. Scroll now belongs to the view
// unless Rack's own "scroll wheel knob adjustment" is enabled, so a drag is the
// only way left to turn the encoder — and defaulting it off would leave the
// module with no turn gesture at all.
//
// The setting stays, because a trackpad and a mouse are different propositions
// and someone may still want the drag quiet. It is now a deliberate choice
// rather than a guess made at build time.
constexpr bool kDragTurnsDefault = true;

bool g_loaded = false;
bool g_dragTurns = kDragTurnsDefault;

void Load() {
    g_loaded = true;

    json_error_t err;
    json_t* root = json_load_file(asset::user(kFile).c_str(), 0, &err);
    if (!root) return;   // No file yet, or unreadable: the default stands.

    json_t* v = json_object_get(root, "encoderDragTurns");
    if (v) g_dragTurns = json_is_true(v);
    json_decref(root);
}

void Save() {
    system::createDirectories(asset::user(kDir));

    json_t* root = json_object();
    json_object_set_new(root, "encoderDragTurns", json_boolean(g_dragTurns));
    json_dump_file(root, asset::user(kFile).c_str(), JSON_INDENT(2));
    json_decref(root);
}

}  // namespace

bool DragTurnsEncoder() {
    if (!g_loaded) Load();
    return g_dragTurns;
}

void SetDragTurnsEncoder(bool on) {
    if (!g_loaded) Load();
    if (on == g_dragTurns) return;
    g_dragTurns = on;
    Save();
}

}  // namespace prefs
}  // namespace ogham
