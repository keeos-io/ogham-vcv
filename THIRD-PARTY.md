# Third-party components

## Ogham firmware

<https://github.com/keeos-io/ogham> — MIT, © 2026 Steven Collins.

Included as a git submodule at `ogham/`, pinned to a specific commit. Six
translation units are compiled into this plugin unmodified; see the README for
which, and `docs/firmware-differences.md` for every place the plugin's behaviour
departs from the module's.

## DaisySP

<https://github.com/electro-smith/DaisySP> — MIT, © 2020 Electrosmith, Corp.

The FX chain uses `Chorus`, `Flanger`, `Phaser` and `DelayLine`. These are
vendored under `dep/daisysp/` rather than pulled through the firmware's own
submodule, so that a recursive clone of this repository does not also fetch
libDaisy, which the plugin has no use for.

## VCV Rack API

<https://github.com/VCVRack/Rack> — GPLv3-or-later, © VCV.

Linked against, not copied. This plugin is distributed free of charge under the
VCV Rack Non-Commercial Plugin License Exception, which permits a free plugin to
carry any licence. No non-API Rack source is copied into this project.

The VCV Component Library graphics (CC BY-NC 4.0) are **not** used: every
component on the panel is original artwork matching the hardware module.
