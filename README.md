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

## Using it

The panel is the module's, at hardware size and hardware layout, so anything
written about the module applies here. The
[module manual](https://github.com/keeos-io/ogham) is the full account; this is
what a Rack user needs.

### The panel

| Control | Does |
|---|---|
| **Func** encoder | Selects the function for the current voice; press and hold for the menu |
| **A**, **B** | The two live parameters, shared by both voices |
| **Rate** | Time rate, 1× at noon, 1/64× to 64× — slow enough for the CV output to act as an LFO |
| **Tone** | Bipolar lo-fi macro. Noon is clean: anticlockwise filters and folds, clockwise crushes and resonates |
| **Clk / VOct** | What the shared jack means — an external clock, or pitch |
| **CV A**, **CV B** | Summed into A and B |
| **Sync** | Resets time to zero, and plucks the low-pass gate when it is on |
| **Out 1**, **Out 2** | The two voices |
| **ENV** | Envelope follower of Out 1, or a bytebeat LFO — see menu field 14 |
| **EOC** | Gate, derived from the rhythm of voice 1 |

### Driving the encoder with a mouse

The module has a shaft you turn and press. A mouse has neither, so the gestures
map like this:

| Gesture | Does |
|---|---|
| Drag | Turns it — changes the function, or the menu field or value |
| Click | Switches the selected voice, or enters and commits a menu edit |
| Hold ~0.6 s | Opens the menu, and leaves it from anywhere |
| Scroll | Turns it, **only** if Rack's *Scroll wheel knob adjustment* preference is on |

Scroll is off by default because that is Rack's own default, and because scroll
otherwise belongs to the view — on a trackpad, a two-finger swipe is a scroll,
and it should move around the patch rather than change the function.

Right-click for the whole function bank by name and category, the FX menu,
and **Drag turns the encoder**, which is stored per installation rather than in
the patch.

### The display

`X-NN` is voice and function. In the menu, `T x. NN` is effect, field and value.
The far-right dot means the tone macro is at its clean centre.

### What is not the same as the hardware

Patches do not transfer: the module keeps its settings in its own flash format
and nothing reads it out. Everything else that differs is listed in
[`docs/firmware-differences.md`](docs/firmware-differences.md).

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

### The other three platforms

Rack ships plugins for `win-x64`, `mac-x64`, `mac-arm64` and `lin-x64`, and all
four are built here.

```sh
python tools/cross_build.py image     # once, about an hour
python tools/cross_build.py win lin   # packages into dist-cross/
```

That image is VCV's plugin toolchain with the macOS target removed, because the
macOS SDK can only be extracted on a Mac and Apple does not permit
redistributing it — and their Dockerfile needs it before it reaches the two
toolchains that have no use for it. The two Mac builds are covered instead by
GitHub Actions, whose runners are Macs, and by the VCV Library, which compiles
all four platforms itself at submission.

[`docs/cross-compiling.md`](docs/cross-compiling.md) has the detail.

## Licence

MIT — see `LICENSE.txt`. Third-party components and their licences are listed in
`THIRD-PARTY.md`.

VCV Rack is GPLv3-or-later; this plugin is distributed free of charge under the
VCV Rack Non-Commercial Plugin License Exception. "VCV Rack" is a trademark of
VCV; this plugin is not affiliated with or endorsed by VCV.
