# Changelog

All notable changes to Ogham for VCV Rack.

The version's major number tracks Rack's, as the plugin library requires, so the
first release is 2.0.0 rather than 1.0.0.

## Unreleased

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
- Patches save the full FX chain, both functions and the menu position, the way
  the module persists them to flash.
- Runs at any host sample rate. The DSP runs at 48 kHz exactly as on hardware and
  is resampled at the boundary, so the sound does not change with the host
  setting; at 48 kHz the conversion is bypassed entirely.
- Right-click for the reference tone, a factory reset, and the CV output modes.

### Notes

- The DSP is the module's own source, compiled unmodified — seven of the
  firmware's translation units are vendored and built as they are. Everywhere
  the plugin has to differ is listed in `docs/firmware-differences.md`.
- Patches are not interchangeable with the hardware. The module stores its
  settings in its own flash format and nothing reads it out.
