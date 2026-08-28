#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — extract the macOS SDK, on a Mac
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
# Produces MacOSX12.3.sdk.tar.xz, the one file a Windows or Linux machine cannot
# obtain for itself, and which lets it cross-compile the plugin for macOS.
#
#   ./extract_sdk.sh ~/Downloads/Xcode_14.0.1.xip
#   ./extract_sdk.sh /Applications/Xcode.app        # if already expanded
#
# Xcode is only unpacked, never run, so it does not need to be a version this Mac
# can launch. Nothing is installed and nothing needs sudo.
# -----------------------------------------------------------------------------

set -euo pipefail

WANT_SDK="12.3"
WORK="${TMPDIR:-/tmp}/ogham-sdk"
OUT_NAME="MacOSX${WANT_SDK}.sdk.tar.xz"

die() { printf '\nextract_sdk: %s\n' "$*" >&2; exit 1; }
say() { printf '\n== %s\n' "$*"; }

[ "$(uname -s)" = "Darwin" ] || die "this has to run on a Mac; that is the whole point of it."
[ $# -eq 1 ] || die "usage: $0 <Xcode_14.0.1.xip | Xcode.app>"
SRC="$1"
[ -e "$SRC" ] || die "not found: $SRC"

# --- space ------------------------------------------------------------------
# The .xip is about 11 GB and expands to roughly 20 GB of Xcode.app.
avail_gb=$(df -g "${TMPDIR:-/tmp}" | awk 'NR==2 {print $4}')
if [ "${avail_gb:-0}" -lt 40 ] && [ "${SRC##*.}" = "xip" ]; then
    die "about ${avail_gb} GB free where temporary files go, and expanding Xcode needs ~40 GB.
  Free some space, or point TMPDIR at a bigger volume."
fi

mkdir -p "$WORK"
cd "$WORK"

# --- Xcode ------------------------------------------------------------------
if [ -d "$SRC" ]; then
    XCODE="$SRC"
    say "Using $XCODE"
else
    XCODE="$WORK/Xcode.app"
    if [ -d "$XCODE" ]; then
        say "Already expanded at $XCODE"
    else
        say "Expanding $(basename "$SRC"). Ten to twenty minutes, and it looks idle throughout."
        xip --expand "$SRC"
        [ -d "$XCODE" ] || die "expansion did not produce Xcode.app in $WORK"
    fi
fi

# Check the version BEFORE spending time on the extraction: the toolchain pins
# DARWIN_VERSION 21.4, which is SDK 12.3, and a different Xcode carries a
# different SDK that its compiler triples will not match.
sdk_dir="$XCODE/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs"
[ -d "$sdk_dir" ] || die "no SDKs inside $XCODE — is that really an Xcode.app?"
if [ ! -e "$sdk_dir/MacOSX${WANT_SDK}.sdk" ]; then
    printf '\nextract_sdk: this Xcode does not carry the macOS %s SDK.\n' "$WANT_SDK" >&2
    printf '  It has: %s\n' "$(ls "$sdk_dir" | tr '\n' ' ')" >&2
    die "download Xcode 14.0.1 specifically:
  https://developer.apple.com/services-account/download?path=/Developer_Tools/Xcode_14.0.1/Xcode_14.0.1.xip"
fi
say "Found the macOS ${WANT_SDK} SDK."

# --- osxcross's packaging script --------------------------------------------
if [ ! -d "$WORK/osxcross" ]; then
    say "Fetching osxcross (for its packaging script only; nothing is built)"
    git clone --depth 1 https://github.com/tpoechtrager/osxcross.git "$WORK/osxcross"
fi

command -v xz >/dev/null 2>&1 || die "xz is missing. Install it with: brew install xz"

say "Packaging the SDK. A few minutes."
cd "$WORK/osxcross/tools"
XCODEDIR="$XCODE" ./gen_sdk_package.sh

# --- result -----------------------------------------------------------------
found="$(ls -1 MacOSX*.sdk.tar.* 2>/dev/null | head -1 || true)"
[ -n "$found" ] || die "gen_sdk_package.sh produced no tarball."

dest="$HOME/Desktop/$OUT_NAME"
[ -d "$HOME/Desktop" ] || dest="$HOME/$OUT_NAME"
mv "$found" "$dest"

size_mb=$(( $(stat -f%z "$dest") / 1000000 ))
[ "$size_mb" -gt 20 ] || die "the tarball is only ${size_mb} MB, which is too small to be the SDK."

cat <<EOF

Done. ${size_mb} MB at:

  $dest

Copy it to the build machine, into the plugin repo at:

  tools/toolchain/sdk/$(basename "$dest")

then rebuild the image, which reuses the Windows and Linux layers:

  python tools/cross_build.py image
  python tools/cross_build.py all

The scratch copy of Xcode is still in $WORK and is roughly 20 GB.
Delete it whenever you like:

  rm -rf "$WORK"

Do not commit the tarball. It is Apple's, redistributing it is not permitted,
and the repository is set up to keep it out.
EOF
