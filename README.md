# Ogham for VCV Rack

A VCV Rack 2 plugin port of **Ogham**, the dual-voice bytebeat Eurorack module by
[Keeos](https://keeos.io). Written by the maker of the module.

## What this is

Ogham is a hardware Eurorack module built on the Electro-Smith Daisy Seed: a
hundred curated bytebeat functions, two independent voices on a shared time
base, a bipolar lo-fi tone macro, a three-stage modulation chain, an internal
low-pass gate, and a CV output that can be an envelope follower or a bytebeat
LFO. This plugin is that module, in Rack.

## Install

The plugin is in the [VCV Library](https://library.vcvrack.com/Keeos/Ogham), so
Rack's own browser is the easiest route: subscribe, and Rack keeps it updated.

For anyone who prefers not to, every release carries a package for each
platform:

**[Download from Releases](https://github.com/keeos-io/ogham-vcv/releases)** —
`win-x64`, `mac-arm64`, `mac-x64`, `lin-x64`.

Put the `.vcvplugin` file **directly** into Rack's plugin folder, not in a
subfolder, and restart Rack — it unpacks the file itself on startup. **Help →
Open user folder** in Rack finds the folder; the plugins live in
`plugins-<platform>/` beside it.

These packages are ad-hoc signed rather than notarised, so macOS may refuse the
first launch. Right-click → Open, or:

```sh
xattr -dr com.apple.quarantine ~/Library/Application\ Support/Rack2/plugins-mac-arm64/Keeos
```

## Using it

The panel is the module's, at hardware size and hardware layout, so anything
written about the module applies here. The
[Ogham manual](https://keeos.io/docs/ogham-manual/)
([PDF](https://keeos.io/docs/ogham-manual.pdf)) is the full account; this is what
a Rack user needs.

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
| **CV Out** | Envelope follower of Out 1, or a bytebeat LFO — see menu field 14 |
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
and nothing reads it out. That is the only difference a player will meet; the
rest are listed in the next section.

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
make -f tools/host.mk tests          # converter, app, multi-instance, goldens
make -f tools/host.mk compile-only   # just: do the firmware sources still compile?

build_host/render-g++ tests/parity/scripts/smoke.csv out.wav 10
```

### Golden renders

`tests/parity/golden/renders.txt` holds 51 configurations — every menu field in
turn, every FX stage in both variants, every CV output mode, and each way the
time base can be driven — rendered for three seconds and reduced to a hash, peak
and RMS levels, and a 16-point RMS envelope per channel. This is what notices the
sound changing.

```sh
make -f tools/host.mk golden        # check
build_host/golden_test --write      # re-record, after an INTENDED change
```

The hash is exact; the levels are compared with a 1e-3 tolerance. Both are
needed, because libm is not bit-identical between platforms: the same sources
under Linux glibc render four of these cases differently in their last bits, the
worst by 4e-4, while peak and RMS stay identical to six decimal places. A hash
that moves while every level holds is reported as drift rather than failed. On
the machine that recorded the file the hash is still exact, so nothing slips
past.

Re-recording is not a way to fix a failure. If the audio changed on purpose,
`--write` and say so in the commit; if it did not, the diff is the bug report.

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
