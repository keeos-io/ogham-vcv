# Third-party components

## Ogham firmware

<https://github.com/keeos-io/ogham> — MIT, © 2026 Steven Collins.

Seven translation units are compiled into this plugin unmodified, vendored as
byte-identical copies in `ogham-src/` and verified by sha256 against
`tools/upstream_manifest.json`. See `ogham-src/README.md` for why they are copies
rather than a submodule, and `docs/firmware-differences.md` for every place the
plugin's behaviour departs from the module's.

## DaisySP

<https://github.com/electro-smith/DaisySP> — MIT, © 2020 Electrosmith, Corp.

The FX chain uses `Chorus`, `Flanger`, `Phaser` and `DelayLine`, vendored under
`dep/daisysp/`. Only those four; the rest of DaisySP is not used.

## VCV Rack API

<https://github.com/VCVRack/Rack> — GPLv3-or-later, © VCV.

Linked against, not copied. This plugin is distributed free of charge under the
VCV Rack Non-Commercial Plugin License Exception, which permits a free plugin to
carry any licence. No non-API Rack source is copied into this project.

The VCV Component Library graphics (CC BY-NC 4.0) are **not** used: every
component on the panel is original artwork matching the hardware module.
