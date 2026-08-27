# Ogham for VCV Rack

A VCV Rack 2 plugin port of **Ogham**, the dual-voice bytebeat Eurorack module by
[Keeos](https://keeos.io). Written by the maker of the module.

Status: **in development.** Nothing is released yet.

## What this is

Ogham is a hardware Eurorack module built on the Electro-Smith Daisy Seed: a
hundred curated bytebeat functions, two independent voices on a shared time
base, a bipolar lo-fi tone macro, a three-stage modulation chain, an internal
low-pass gate, and a CV output that can be an envelope follower or a bytebeat
LFO. This plugin is that module, in Rack.

## How it relates to the firmware

The firmware repository, [`keeos-io/ogham`](https://github.com/keeos-io/ogham),
is the source of truth and this project never commits to it. It is included here
as a submodule at `ogham/`, pinned to a specific commit, and six of its nine
translation units are compiled into the plugin **unmodified**:

| Compiled verbatim | Not compiled |
|---|---|
| `formulas.cpp` | `ogham_main.cpp` — a `main()` with globals and ISRs; transcribed as `OghamApp` |
| `bytebeat_engine.cpp` | `ogham_controls.cpp` — ADC smoothing and analog corrections a plugin does not want |
| `bpm_clock.cpp` | `tm1637.cpp` — bit-bangs GPIO; replaced by a segment-buffer implementation of the same class |
| `ogham_audio_pipeline.cpp` | `diag_pots.cpp` — a hardware diagnostic |
| `ogham_cv_output.cpp` | |
| `ogham_display.cpp` | |

So the voices here are not a reimplementation of the module's voices. They are
the same code.

Everywhere the plugin deliberately differs from the module —
hardware calibration constants that become ideal values, the sample-rate
boundary, persistence — is recorded in
[`docs/firmware-differences.md`](docs/firmware-differences.md). A divergence
that is not in that file is a bug.

## Building

Requires the VCV Rack SDK. Set `RACK_DIR`, then:

```sh
git clone --recurse-submodules https://github.com/keeos-io/ogham-vcv.git
cd ogham-vcv
make
```

The host-side spike and parity harness build without Rack; see `tests/parity/`.

## Licence

MIT — see `LICENSE.txt`. Third-party components and their licences are listed in
`THIRD-PARTY.md`.

VCV Rack is GPLv3-or-later; this plugin is distributed free of charge under the
VCV Rack Non-Commercial Plugin License Exception. "VCV Rack" is a trademark of
VCV; this plugin is not affiliated with or endorsed by VCV.
