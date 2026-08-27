#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — vendor the firmware's sources
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
# The plugin compiles seven of the Ogham firmware's translation units unmodified.
# They live in ogham-src/ as byte-identical copies, and this is what puts them
# there.
#
# Why copies rather than a submodule: keeos-io/ogham has submodules of its own —
# libDaisy and DaisySP — and libDaisy in turn has googletest, CMSIS-DSP, CMSIS_5
# and ST's STM32H7xx CMSIS drivers. A `git clone --recurse-submodules` of this
# repository therefore descends three levels into ARM and ST vendor trees and
# fails outright on the ST one. The VCV Library builds plugins from a clone, so a
# fatal clone is not survivable. Vendoring makes this repository self-contained
# and the clone trivial.
#
# The firmware is still the source of truth and is never modified. What changes
# is that the copy is explicit and hash-checked instead of a git pointer:
# tools/upstream_check.py verifies ogham-src/ against the recorded hashes, so a
# local edit to a vendored file is caught, and re-running this tool against a
# newer firmware shows exactly what moved.
#
#   python tools/sync_upstream.py                     clone upstream, sync, record
#   python tools/sync_upstream.py --from ../ogham     use a local clone instead
#   python tools/sync_upstream.py --ref v1.17         a tag, branch or commit
#   python tools/sync_upstream.py --dry-run           report, change nothing
#
# Syncing is a deliberate act. Read the diff, decide what it means for the
# transcribed application layer, and update docs/firmware-differences.md before
# committing the result.

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENDOR = os.path.join(ROOT, "ogham-src")
MANIFEST = os.path.join(ROOT, "tools", "upstream_manifest.json")

UPSTREAM_URL = "https://github.com/keeos-io/ogham.git"
FW = "firmware/src"

# Compiled into the plugin, and therefore copied.
COMPILED = [
    "formulas", "bytebeat_engine", "bpm_clock",
    "ogham_audio_pipeline", "ogham_cv_output", "ogham_display", "tm1637",
]
COMPILED_FILES = [f"{n}{ext}" for n in COMPILED for ext in (".cpp", ".h")]

# Headers with no translation unit of their own that the above still include.
# tm1637.cpp reaches for the pin table even though every pin it names is inert
# here — it is compiled verbatim, so it gets the header verbatim too.
COMPILED_FILES += ["ogham_pins.h"]

# Not copied — not compiled — but hashed, because the plugin mirrors them by
# hand and a change upstream needs a human to look at it.
WATCHED = [
    "ogham_main.cpp",       # transcribed as src/OghamApp.cpp
    "ogham_controls.cpp",   # informs the control mapping
    "ogham_controls.h",
]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def run(args, **kw):
    return subprocess.run(args, check=True, capture_output=True, text=True, **kw)


def resolve_source(from_path, ref, tmpdir):
    """Return (path to a firmware checkout, commit sha)."""
    if from_path:
        src = os.path.abspath(from_path)
        if not os.path.isdir(os.path.join(src, FW)):
            sys.exit(f"sync: {src} does not look like the Ogham firmware repo")
        if ref:
            run(["git", "-C", src, "checkout", "--quiet", ref])
        sha = run(["git", "-C", src, "rev-parse", "HEAD"]).stdout.strip()
        return src, sha

    dest = os.path.join(tmpdir, "ogham")
    print(f"sync: cloning {UPSTREAM_URL}")
    # No recursion, deliberately: the nested vendor trees are what made a
    # submodule unworkable, and none of them is needed to read seven files.
    run(["git", "clone", "--quiet", "--no-recurse-submodules", UPSTREAM_URL, dest])
    if ref:
        run(["git", "-C", dest, "checkout", "--quiet", ref])
    sha = run(["git", "-C", dest, "rev-parse", "HEAD"]).stdout.strip()
    return dest, sha


def main():
    ap = argparse.ArgumentParser(description="Vendor the Ogham firmware's sources.")
    ap.add_argument("--from", dest="from_path", help="path to a firmware clone")
    ap.add_argument("--ref", help="tag, branch or commit to sync from")
    ap.add_argument("--dry-run", action="store_true", help="report, change nothing")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmpdir:
        src, sha = resolve_source(args.from_path, args.ref, tmpdir)
        srcdir = os.path.join(src, FW)

        recorded = {}
        if os.path.isfile(MANIFEST):
            with open(MANIFEST) as f:
                recorded = json.load(f)
        old_files = recorded.get("files", {})

        state = {"commit": sha, "url": UPSTREAM_URL, "files": {}}
        changed, added = [], []

        for name in COMPILED_FILES + WATCHED:
            path = os.path.join(srcdir, name)
            if not os.path.isfile(path):
                sys.exit(f"sync: {name} is missing from the firmware — its layout may have changed")
            digest = sha256(path)
            state["files"][name] = digest
            if name not in old_files:
                added.append(name)
            elif old_files[name] != digest:
                changed.append(name)

        print(f"sync: upstream at {sha[:10]}")
        for name in added:
            print(f"  + {name}")
        for name in changed:
            kind = "TRANSCRIBED" if name in WATCHED else "compiled"
            print(f"  ~ {name}   [{kind}]")
        if not added and not changed:
            print("  no change")

        if args.dry_run:
            print("sync: dry run, nothing written")
            return 0

        os.makedirs(VENDOR, exist_ok=True)
        for name in COMPILED_FILES:
            shutil.copyfile(os.path.join(srcdir, name), os.path.join(VENDOR, name))

        with open(MANIFEST, "w") as f:
            json.dump(state, f, indent=2, sort_keys=True)
            f.write("\n")

        print(f"sync: {len(COMPILED_FILES)} files vendored into ogham-src/")
        if any(n in changed for n in WATCHED):
            print()
            print("A watched file changed. src/OghamApp.cpp mirrors ogham_main.cpp by")
            print("hand: read the diff, decide what it means, and record any new")
            print("divergence in docs/firmware-differences.md before committing.")
        return 0


if __name__ == "__main__":
    sys.exit(main())
