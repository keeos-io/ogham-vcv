#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — the same mirror workaround, for osxcross
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# -----------------------------------------------------------------------------
#
# Third instance of one problem: gnu.org is unreachable from this network.
#
# osxcross fetches binutils itself, and build_binutils.sh carries
# MIRROR="https://ftp.gnu.org/gnu" as a plain assignment — not a default, so an
# environment variable cannot override it. Everything else osxcross needs comes
# from GitHub, which is why LLVM, clang, osxcross proper and compiler-rt all
# build before this one fails, 25 minutes in.
#
# The sed has to run against a checkout that does not exist yet: the toolchain
# clones osxcross inside the toolchain-mac recipe. So this edits the RECIPE,
# inserting the rewrite immediately after the clone and before anything uses it.
#
# Kept separate from patch_toolchain.sh purely for Docker's layer cache: this
# runs after the Windows and Linux toolchains, so fixing the macOS build never
# costs a rebuild of the two that already work.
# -----------------------------------------------------------------------------

set -euo pipefail

MIRROR="https://mirrors.kernel.org/gnu"

python3 - "$MIRROR" <<'PY'
import sys

mirror = sys.argv[1]
path = "Makefile"
s = open(path).read()

anchor = "\tcd osxcross && git checkout 4372d5560307c649af5dbbfa20b39199c9ef48be\n"
if anchor not in s:
    sys.exit("patch_osxcross: could not find the osxcross checkout line; "
             "upstream has changed and this patch needs revisiting.")

# Every script, not just build_binutils.sh: if another one grows a gnu.org URL
# it fails the same way, half an hour in.
rewrite = ("\tfind osxcross -name '*.sh' -exec sed -i "
           + "'s|https://ftp.gnu.org/gnu|" + mirror + "|g' {} +\n")
s = s.replace(anchor, anchor + rewrite, 1)

open(path, "w").write(s)
print("patch_osxcross: osxcross scripts will be pointed at " + mirror)
PY

sed -n '/git checkout 4372/,+1p' Makefile
