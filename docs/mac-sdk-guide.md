# Extracting the macOS SDK, step by step

**What this achieves:** one file, `MacOSX12.3.sdk.tar.xz`, which lets the Windows
machine cross-compile this plugin for both macOS targets. Afterwards, one command
there builds all four platforms, offline, permanently.

**Why it has to be done on a Mac:** the SDK ships inside Xcode, Apple's licence
does not permit redistributing it, and no public toolchain image can contain one.
It is the only part of the build that cannot be automated from Windows.

**Before starting**

| | |
|---|---|
| Time | About 45 minutes, most of it waiting |
| Disk | ~40 GB free (an 11 GB download that expands to ~20 GB) |
| Account | An Apple ID. A free one — no paid developer membership |
| Result | One file of a few tens of MB |

Nothing is installed, nothing needs `sudo`, and Xcode is never launched — only
unpacked. It does not have to be a version this Mac could run.

> **Prefer to skip the manual steps?** `tools/toolchain/sdk/extract_sdk.sh` does
> parts 2 and 3 in one command, with the same checks. This guide is the long way
> round, for when you want to see what is happening.

---

## Part 1 — Download Xcode 14.0.1

### Step 1. Sign in to Apple's developer downloads

Open <https://developer.apple.com/download/all/> and sign in with your Apple ID.
A free account has access to old Xcode releases; you do not need to pay for
anything.

### Step 2. Download Xcode 14.0.1

Search the list for **Xcode 14.0.1** and download `Xcode_14.0.1.xip` (about
11 GB). Direct link:

```
https://developer.apple.com/services-account/download?path=/Developer_Tools/Xcode_14.0.1/Xcode_14.0.1.xip
```

**It has to be 14.0.1.** That release carries the macOS **12.3** SDK, and the
toolchain pins `DARWIN_VERSION = 21.4` — the cross-compilers it builds are named
after that version. A newer Xcode carries a newer SDK and will not match.

---

## Part 2 — Unpack it

### Step 3. Expand the archive

```bash
cd ~/Downloads
xip --expand Xcode_14.0.1.xip
```

Ten to twenty minutes, and it prints nothing at all while it works — it has not
hung. This produces `Xcode.app` in `~/Downloads`.

Expand it from the command line rather than double-clicking it in Finder, and
leave it where it is. It is a disposable copy, not an installation, and it must
not replace whatever Xcode this Mac already has.

### Step 4. Checkpoint: confirm the SDK is in there

```bash
ls ~/Downloads/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs
```

You should see `MacOSX12.3.sdk`, possibly alongside a couple of others.

**If `MacOSX12.3.sdk` is not listed, stop here.** The download was a different
Xcode release, and everything after this point would produce an SDK the toolchain
cannot use. Go back to step 2.

---

## Part 3 — Package the SDK

### Step 5. Check for `xz`

```bash
command -v xz || brew install xz
```

The packaging script compresses with `xz`, which macOS does not always ship. If
you do not have Homebrew, `git clone` will also prompt to install Apple's command
line tools the first time you run it — accept that; it is small and unrelated to
the Xcode above.

### Step 6. Fetch osxcross

```bash
cd ~/Downloads
git clone https://github.com/tpoechtrager/osxcross.git
```

Only one script from this repository is used. Nothing is compiled and nothing is
installed.

### Step 7. Build the tarball

```bash
cd ~/Downloads/osxcross/tools
XCODEDIR=~/Downloads/Xcode.app ./gen_sdk_package.sh
```

A few minutes. It copies the SDK out of the app bundle and compresses it.

### Step 8. Checkpoint: confirm the result

```bash
ls -lh ~/Downloads/osxcross/tools/MacOSX12.3.sdk.tar.xz
tar -tf ~/Downloads/osxcross/tools/MacOSX12.3.sdk.tar.xz | head -3
```

Expect a file of a few tens of megabytes, listing paths beginning
`MacOSX12.3.sdk/`. Anything under about 20 MB means the packaging failed and the
file should not be trusted.

---

## Part 4 — Take it back to the build machine

### Step 9. Copy the file across

AirDrop, a USB stick, a network share, `scp` — whatever is convenient. It is the
only thing that needs to make the trip.

### Step 10. Put it where the build looks

On the Windows machine, into the plugin repository:

```
D:\ogham-vcv\tools\toolchain\sdk\MacOSX12.3.sdk.tar.xz
```

Do not commit it. It is Apple's, and `.gitignore` already keeps it out.

### Step 11. Rebuild the image and build everything

```bash
python tools/cross_build.py image   # Windows and Linux layers are cached
python tools/cross_build.py all     # all four targets
```

The image is retagged `ogham-toolchain:all`. It must never be pushed to a public
registry from then on: the SDK stays inside its `COPY` layer even though the
build deletes it afterwards.

---

## Part 5 — Tidy up

```bash
rm -rf ~/Downloads/Xcode.app          # about 20 GB
rm -f  ~/Downloads/Xcode_14.0.1.xip   # about 11 GB
rm -rf ~/Downloads/osxcross
```

Keep a copy of `MacOSX12.3.sdk.tar.xz` somewhere safe. Doing this again means
downloading Xcode again.

---

## While you are on the Mac

Two things worth doing, since the machine is already in front of you. Neither is
required for the SDK.

### Run the plugin natively

This is the part no CI runner can do. A runner proves the code compiles; only a
person can say whether the panel looks right, the encoder gestures feel right,
and the display reads properly on a Retina screen.

```bash
git clone https://github.com/keeos-io/ogham-vcv.git
cd ogham-vcv

uname -m    # arm64 -> mac-arm64 below; x86_64 -> mac-x64

curl -fLO https://vcvrack.com/downloads/Rack-SDK-2.6.6-mac-arm64.zip
unzip -q Rack-SDK-2.6.6-mac-arm64.zip
export RACK_DIR=$PWD/Rack-SDK

brew install zstd                      # `make dist` needs it
make -j$(sysctl -n hw.ncpu) install    # into ~/Library/Application Support/Rack2/
```

### Settle whether the DSP is bit-exact across platforms

An open question in `firmware-differences.md`: bit-exactness has only ever been
claimed *within* a toolchain family, never across one.

```bash
make -f tools/host.mk tests
build_host/render-g++ tests/parity/scripts/smoke.csv /tmp/mac.wav 10
```

The renderer prints a hash of the rendered audio. If it matches the hash the same
script produces on Windows, the port is bit-identical across two compilers, two C
libraries and two architectures — a materially stronger claim than the one
currently on record. If it differs, the size of the difference is the interesting
number. Either way it is worth writing down.
