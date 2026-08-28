#!/usr/bin/env bash
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — network workarounds for the toolchain build
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# -----------------------------------------------------------------------------
#
# Runs inside the image, against the pinned upstream toolchain checkout. This is
# the only place that checkout is modified, and both changes are for one reason:
# gnu.org is unreachable from this network. ftp.gnu.org and ftpmirror.gnu.org
# both fail to connect, from the Docker container and from the Windows host
# alike, while mirrors.kernel.org serves the identical files.
#
# Two separate fetches are affected, because they go through different machinery.
#
#   1. The Linux toolchain rule downloads texinfo from ftp.gnu.org with wget,
#      directly in the Makefile. Repointed. The rule verifies a SHA-256 on the
#      next line, so a mirror serving anything else fails the build rather than
#      passing quietly.
#
#   2. crosstool-ng fetches its own components, and most of them come from
#      sourceware.org and gmplib.org — which is why the Windows toolchain built
#      without complaint. libiconv is the exception: it is GNU-only, so
#      `CT_Mirrors GNU libiconv` yields ftpmirror.gnu.org and nothing else, and
#      the build dies there. crosstool-ng has a mirror facility for exactly this,
#      composing URLs as ${CT_MIRROR_BASE_URL}/${package}/${file} — which with
#      this base is the working kernel.org URL, verbatim.
#
#      CT_FORCE_MIRROR is deliberately NOT set: the mirror is tried first and
#      anything not on it falls back to its usual home, so the non-GNU packages
#      are unaffected.
# -----------------------------------------------------------------------------

set -euo pipefail

MIRROR="https://mirrors.kernel.org/gnu"

python3 - "$MIRROR" <<'PY'
import sys

mirror = sys.argv[1]
path = "Makefile"
s = open(path).read()

# 1. texinfo, fetched by the Makefile itself.
old_url = "https://ftp.gnu.org/gnu/"
if old_url not in s:
    sys.exit("patch_toolchain: expected %s in the Makefile; upstream has "
             "changed and this patch needs revisiting." % old_url)
s = s.replace(old_url, mirror + "/", 1)

# 2. crosstool-ng's own component fetches. Two plain echo lines rather than one
#    printf: the recipe then holds no backslash escapes for make, the shell and
#    this script to disagree about — the first attempt used printf with \n and
#    put a real newline into the recipe, which make rejected as a missing
#    separator. The sample config may carry "# CT_USE_MIRROR is not set";
#    appending after it wins, both when the file is sourced and when kconfig
#    reads it.
anchor = "\tct-ng x86_64-ubuntu16.04-linux-gnu\n"
if anchor not in s:
    sys.exit("patch_toolchain: could not find the ct-ng sample line; upstream "
             "has changed and this patch needs revisiting.")
inject = (anchor
          + "\techo CT_USE_MIRROR=y >> .config\n"
          + "\techo CT_MIRROR_BASE_URL='\"" + mirror + "\"' >> .config\n")
s = s.replace(anchor, inject, 1)

open(path, "w").write(s)
print("patch_toolchain: texinfo and crosstool-ng both pointed at " + mirror)
PY

# Show the result. Each command must be on its own line, or make will say
# "missing separator" and this will have failed silently in the wrong direction.
grep -n "mirrors.kernel.org\|CT_USE_MIRROR\|CT_MIRROR_BASE_URL" Makefile
