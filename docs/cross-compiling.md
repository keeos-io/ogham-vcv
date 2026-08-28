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

## macOS: on a Mac, without owning one

Three routes, and the first is the one that matters most.

**The VCV Library builds it for you.** Submission is a source repository and a
manifest; VCV's build farm compiles all four platforms itself. For a free plugin
distributed through the Library, no Mac binary ever has to be produced here.

**GitHub Actions runs real Macs.** `macos-13` is x64 and `macos-14` is arm64,
both free for public repositories, both with Xcode already installed. No SDK
extraction, no `osxcross`, no Apple account — the plugin simply compiles natively
with the matching Rack SDK. This is what `.github/workflows/build.yml` does, and
it is how the Mac build gets *verified* here rather than assumed.

**Borrow a Mac once.** Extract `MacOSX12.3.sdk.tar.xz` per VCV's README, drop it
into a clone of their toolchain, and `make docker-build` gives all four targets
locally and forever. Worth doing if a Mac is ever within reach for an afternoon;
not worth buying one for.

## Versions

| | |
|---|---|
| Rack SDK | 2.6.6 |
| Toolchain commit | `4fd1318701624051cee54b385343f23d59b4845c` |
| macOS SDK (upstream, not used here) | 12.3, Darwin 21.4 |

The Rack SDK version is set by the toolchain's own Makefile at that commit, so
the pin fixes both together.
