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
is the source of truth and this project never commits to it. Seven of its nine
translation units are compiled into the plugin **unmodified**, as byte-identical
copies in [`ogham-src/`](ogham-src/README.md):

| Compiled verbatim | Not compiled |
|---|---|
| `formulas.cpp` | `ogham_main.cpp` — a `main()` with globals and ISRs; transcribed as `OghamApp` |
| `bytebeat_engine.cpp` | `ogham_controls.cpp` — ADC smoothing and analog corrections a plugin does not want |
| `bpm_clock.cpp` | |
| `ogham_audio_pipeline.cpp` | `diag_pots.cpp` — a hardware diagnostic |
| `ogham_cv_output.cpp` | |
| `ogham_display.cpp` | |
| `tm1637.cpp` — against dead pins | |

So the voices here are not a reimplementation of the module's voices. They are
the same code.

Everywhere the plugin deliberately differs from the module —
hardware calibration constants that become ideal values, the sample-rate
boundary, persistence — is recorded in
[`docs/firmware-differences.md`](docs/firmware-differences.md). A divergence
that is not in that file is a bug.

## Building

```sh
git clone https://github.com/keeos-io/ogham-vcv.git
cd ogham-vcv
source tools/env.sh     # MinGW toolchain on PATH, RACK_DIR at the Rack SDK
make                    # or `make install` to put it in Rack's plugin folder
```

No submodules: the firmware's sources are vendored, so the clone is one repo and
nothing else. `python tools/upstream_check.py` verifies they are byte-identical
to what was synced.

`tools/env.sh` is written for this machine; override `RACK_DIR` or `PATH`
yourself elsewhere. The toolchain is MinGW-w64 GCC 14.2.0, `x86_64-w64-mingw32`,
POSIX threads on the MSVCRT runtime — the same profile Rack itself is built
with.

### The parity harness

The firmware's DSP sources build and run without Rack at all:

```sh
make -f tools/host.mk                # build the offline renderer with g++
make -f tools/host.mk CXX=clang++    # or with clang
make -f tools/host.mk check          # build both, render, compare byte for byte
make -f tools/host.mk compile-only   # just: do the firmware sources still compile?

build_host/render-g++ tests/parity/scripts/smoke.csv out.wav 10
```

The renderer drives the module's DSP from a scripted CSV and writes a 4-channel
WAV — Out 1, Out 2, ENV, EOC — plus a hash of the audio, so a parity check is
one line. Builds that feed a comparison carry `-ffp-contract=off`; see the
build-determinism note in `docs/firmware-differences.md` for why that matters
and what bit-exactness can and cannot be claimed across compilers.

## Licence

MIT — see `LICENSE.txt`. Third-party components and their licences are listed in
`THIRD-PARTY.md`.

VCV Rack is GPLv3-or-later; this plugin is distributed free of charge under the
VCV Rack Non-Commercial Plugin License Exception. "VCV Rack" is a trademark of
VCV; this plugin is not affiliated with or endorsed by VCV.
