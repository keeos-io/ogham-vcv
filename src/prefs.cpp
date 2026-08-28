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

// Dragging a knob to turn it is Rack's convention on every platform, and it is
// what the module's own gesture maps onto — so it is the default. Except on
// macOS, where dragging is the reflex for moving around a patch, and a drag that
// crosses the encoder changing the function reads as a fault rather than as a
// knob being turned.
//
// A default, not a rule: a Mac driven by a mouse is a different proposition from
// one driven by a trackpad, and no build-time guess covers both.
//
// ARCH_MAC comes from Rack's arch.hpp, which plugin.hpp pulls in — it is not a
// compiler flag, so this only works because that header is included above. If it
// were not, this would quietly compile the wrong branch and look like success.
#if defined ARCH_MAC
constexpr bool kDragTurnsDefault = false;
#else
constexpr bool kDragTurnsDefault = true;
#endif

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
