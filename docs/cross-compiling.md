# Building for the other three platforms

VCV Rack ships plugins for four targets: `win-x64`, `mac-x64`, `mac-arm64` and
`lin-x64`. This module is developed on Windows, so three of the four have to come
from somewhere else.

Linux is not a decision to agonise over: it costs one more line in a build matrix
and one more file in a release, and a plugin that omits it looks unfinished for
no saving. All four are built here.

## The Apple wall, and why it shapes everything below

VCV's own [rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain)
cross-compiles all four targets from Linux, which sounds like the whole answer.
It is not, because of one line near the top of its Dockerfile:

```dockerfile
COPY MacOSX${MACOS_SDK_VERSION}.sdk.tar.* /home/build/rack-plugin-toolchain/
```

That tarball is the macOS 12.3 SDK, and their README is blunt about where it
comes from: *"You must use a Mac computer for this step."* It is extracted from
Xcode 14.0.1 with `osxcross`, and Apple's licence does not permit redistributing
it — which is why no public prebuilt image contains one, and why this is a wall
rather than an inconvenience.

The SDK is also the **first** thing the image needs, before either of the
toolchains that want nothing from Apple. Without a Mac in the room, the Windows
and Linux builds fail too, for want of a file neither of them uses.

So the work splits in two: what can be built here, and what needs a Mac somewhere.

## Windows and Linux: locally, in Docker

`tools/toolchain/Dockerfile` is VCV's toolchain with the mac target left out. It
clones their repository at a pinned commit and runs their own `dep-ubuntu` rule
rather than copying their package list here, so there is one source of truth and
the pin is what makes it reproducible.

```bash
python tools/cross_build.py image     # once, about an hour
python tools/cross_build.py win lin   # minutes
```

Packages land in `dist-cross/`. `analyze` runs cppcheck over `src/`, and `shell`
opens a prompt inside the image.

Two properties worth knowing. Because it contains no Apple SDK, **this image is
redistributable** — it can be pushed to a registry and pulled by CI, which the
full four-target image cannot. And because it builds against an
Ubuntu 16.04 sysroot, its Linux binary runs on distributions far older than the
build host, which a native `ubuntu-latest` build does not.

The container builds the working tree in place and the toolchain runs
`make clean` around each target, so a cross build removes the local `plugin.dll`
and `build/`. Run `make` again to get them back.

### One trap this uncovered

The toolchain runs `make cleandep` before every platform build, and Rack's
`dep.mk` defines that as `rm -rf dep`. DaisySP used to be vendored in `dep/`, so
the first cross build would have deleted four checked-in source files and then
failed on the missing headers — a confusing way to learn that `dep/` is Rack's
scratch space for dependencies a build can re-fetch, not a place for source that
must survive. It now lives in `daisysp-src/`, alongside `ogham-src/`.

### What it produced

All four, verified rather than assumed — a file with the right name proves
nothing:

| Package | Binary |
|---|---|
| `Keeos-2.0.0-win-x64.vcvplugin` | PE32+ DLL, x86-64, stripped |
| `Keeos-2.0.0-lin-x64.vcvplugin` | ELF 64-bit shared object, x86-64, stripped |
| `Keeos-2.0.0-mac-x64.vcvplugin` | Mach-O 64-bit, X86_64, ad-hoc signed |
| `Keeos-2.0.0-mac-arm64.vcvplugin` | Mach-O 64-bit, ARM64, ad-hoc signed |

And the properties that decide whether they will actually load, rather than
merely having compiled:

| | |
|---|---|
| Linux glibc floor | **GLIBC_2.14** — 2011 |
| Linux dependencies | `libRack.so`, `libm`, `libc` — no libstdc++ |
| macOS minimum | 10.9 on x64, 11.0 on arm64 |
| macOS links | `/tmp/Rack2/libRack.dylib`, `libc++`, `libSystem` |
| macOS signature | ad-hoc, via `rcodesign` |

The glibc figure is the point of the Ubuntu 16.04 sysroot: the binary runs on
distributions far older than the machine that built it, which a native
`ubuntu-latest` build would not. `/tmp/Rack2/libRack.dylib` is not a mistake —
it is the install name Rack itself resolves at load time, written by
`install_name_tool` in Rack's own `dist` rule.

## macOS

Three routes. Which one matters depends on whether a Mac is to hand.

### If a Mac is available: extract the SDK once

This is the one with lasting value. Half an hour on a Mac, mostly waiting for a
download, and afterwards *this* machine builds all four targets from one command,
permanently and offline.

[`mac-sdk-guide.md`](mac-sdk-guide.md) is the step-by-step, with the two
checkpoints that matter — the second of which catches the wrong Xcode before an
hour is spent on it. `tools/toolchain/sdk/extract_sdk.sh` does the same work in
one command.

The short version, on the Mac:

```bash
# Xcode 14.0.1 specifically, from developer.apple.com — a free Apple ID is enough.
# It is only unpacked, never run, so it need not support the host macOS version.
xip --expand Xcode_14.0.1.xip

git clone https://github.com/tpoechtrager/osxcross.git
cd osxcross/tools
XCODEDIR=/path/to/Xcode.app ./gen_sdk_package.sh    # writes MacOSX12.3.sdk.tar.xz
```

Bring `MacOSX12.3.sdk.tar.xz` back, drop it in `tools/toolchain/sdk/`, and
rebuild the image:

```bash
python tools/cross_build.py image   # Windows and Linux layers are cached
python tools/cross_build.py all     # all four
```

The version is not negotiable: the toolchain pins `DARWIN_VERSION = 21.4`, which
is macOS 12.3, and the compiler triples it builds are named after it.

The image is then tagged `:all` rather than `:win-lin`, and **it must not be
pushed to a public registry** — the SDK stays in its `COPY` layer even though the
build deletes it afterwards.

### While on the Mac, run it there

Worth doing regardless, because it is the part CI cannot do: no runner can say
whether the panel looks right, the encoder gestures feel right, or the display
reads correctly on a Retina screen.

```bash
curl -fLO https://vcvrack.com/downloads/Rack-SDK-2.6.6-mac-arm64.zip   # or mac-x64
unzip Rack-SDK-2.6.6-mac-arm64.zip
export RACK_DIR=$PWD/Rack-SDK
brew install zstd                    # `make dist` needs it
make -j$(sysctl -n hw.ncpu) install  # into ~/Library/Application Support/Rack2/
```

And one measurement worth taking while there, which settles an open question in
`firmware-differences.md` — whether the DSP is bit-identical across platforms or
merely close:

```bash
make -f tools/host.mk tests
build_host/render-g++ tests/parity/scripts/smoke.csv /tmp/mac.wav 10
```

The renderer prints a hash of the audio. If it matches the Windows hash, the port
is bit-exact across two compilers, two libms and two architectures, which is a
stronger claim than has been made so far. If it does not, the size of the
difference is the interesting number, and worth recording either way.

### If no Mac is available

**The VCV Library builds it for you.** Submission is a source repository and a
manifest; VCV's build farm compiles all four platforms itself. For a free plugin
distributed through the Library, no Mac binary ever has to be produced here.

**GitHub Actions runs real Macs.** `macos-13` is x64 and `macos-14` is arm64,
both free for public repositories, both with Xcode already installed — so the
plugin compiles natively with no SDK extraction and no Apple account. That is
what `.github/workflows/build.yml` does, and it is how the Mac build gets
verified on every push rather than assumed.

## Versions

| | |
|---|---|
| Rack SDK | 2.6.6 |
| Toolchain commit | `4fd1318701624051cee54b385343f23d59b4845c` |
| macOS SDK (upstream, not used here) | 12.3, Darwin 21.4 |

The Rack SDK version is set by the toolchain's own Makefile at that commit, so
the pin fixes both together.
