// -----------------------------------------------------------------------------
// Ogham for VCV Rack — settings that belong to the machine, not the patch
//
// SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
// SPDX-License-Identifier: MIT
// https://github.com/keeos-io/ogham-vcv
// -----------------------------------------------------------------------------
//
// How the encoder answers a mouse is a property of the desk it is used at — a
// trackpad, a mouse, the habits of the person driving — and not of the patch. So
// it is stored once per installation, in Rack's user directory, and opening
// somebody else's patch cannot change it.
//
// Process-wide rather than per module: every Ogham in the rack reads the same
// setting, because they are all being driven by the same hands.
//
// UI thread only. The audio thread never reads any of this.
// -----------------------------------------------------------------------------

#pragma once

namespace ogham {
namespace prefs {

// Whether dragging the encoder turns it. Defaults to true, and to false on
// macOS — see the note in prefs.cpp. Loads from disk on first call.
bool DragTurnsEncoder();

// Sets it and writes the file. A no-op if the value has not changed, so opening
// the menu does not rewrite the file.
void SetDragTurnsEncoder(bool on);

}  // namespace prefs
}  // namespace ogham
