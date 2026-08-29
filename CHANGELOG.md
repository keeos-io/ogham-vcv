# Changelog

All notable changes to Ogham for VCV Rack.

The version's major number tracks Rack's, as the plugin library requires, so the
first release is 2.0.0 rather than 1.0.0.

## 2.0.0 — 2026-08-29

### Added

- **Ogham**, a port of the Keeos Ogham Eurorack module. Two bytebeat voices from
  a bank of 100 functions plus an A440 reference, a lo-fi tone macro, a
  three-stage modulation chain, an internal low-pass gate, and CV, gate and
  envelope outputs.
- The panel is the module's own production artwork, at hardware size and
  hardware layout: 10 HP, with the controls where the panel drills them.
- The encoder takes the module's gestures. Drag or scroll to change function,
  click to swap voices, hold to open the FX menu, then click to edit a field and
  hold to leave.
- Scrolling over the encoder turns it only when Rack's **Scroll wheel knob
  adjustment** preference is on, which is Rack's own default-off setting for
  settling this conflict. Otherwise the scroll belongs to the view, so a mouse
  wheel — or a two-finger trackpad swipe, which is the same event — moves around
  the patch as it does over any other module.
- **Drag turns the encoder** is a right-click option, on by default. Stored per
  installation in `Rack2/Keeos/settings.json` rather than in the patch: how the
  encoder answers a mouse belongs to the desk it is used at, so opening someone
  else's patch cannot change it.
- Patches save the full FX chain, both functions and the menu position, the way
  the module persists them to flash.
- Runs at any host sample rate. The DSP runs at 48 kHz exactly as on hardware and
  is resampled at the boundary, so the sound does not change with the host
  setting; at 48 kHz the conversion is bypassed entirely.
- Right-click for the reference tone, a factory reset, and the CV output modes.

### Verified

- **51 golden renders** — every menu field, every FX stage in both variants,
  every CV output mode and every way the time base can be driven — checked on
  every push, so a change to the rendered audio cannot pass unnoticed.
- **The 48 kHz boundary** at 44.1, 48, 88.2, 96 and 192 kHz, by invariants that
  hold at every rate: core-step conservation over a minute, edge conservation,
  finiteness, and level against the 48 kHz render.
- **Eight instances at once**, each rendering exactly what it renders alone, at
  about 0.35 % of a core each.
- Windows, macOS (Intel and Apple Silicon) and Linux, built and the binaries
  checked as genuine rather than assumed.

### Notes

- The DSP is the module's own source, compiled unmodified — seven of the
  firmware's translation units are vendored and built as they are. Everywhere
  the plugin has to differ is listed in `docs/firmware-differences.md`.
- Patches are not interchangeable with the hardware. The module stores its
  settings in its own flash format and nothing reads it out.
