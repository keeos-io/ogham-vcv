#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — vendored source integrity and upstream drift
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
# Two questions, in order of how likely they are to matter:
#
#   1. Is ogham-src/ still what tools/sync_upstream.py put there?
#      Those files are the firmware's, compiled unmodified. Editing one here
#      would be a silent fork of the module's DSP, and this is what catches it.
#
#   2. Has the firmware moved since the last sync?
#      Only answerable with a copy of the firmware to hand, so it is skipped
#      unless --against is given or upstream can be cloned with --fetch. The
#      answer that matters is whether ogham_main.cpp changed: the plugin mirrors
#      it by hand, and nothing but a person can reconcile that.
#
#   python tools/upstream_check.py                    integrity only
#   python tools/upstream_check.py --against ../ogham compare with a local clone
#   python tools/upstream_check.py --fetch            clone upstream and compare

import argparse
import hashlib
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VENDOR = os.path.join(ROOT, "ogham-src")
MANIFEST = os.path.join(ROOT, "tools", "upstream_manifest.json")
FW = "firmware/src"

# Mirrors sync_upstream.py: everything vendored is compiled; the rest is watched
# because the plugin re-creates it by hand.
WATCHED = ["ogham_main.cpp", "ogham_controls.cpp", "ogham_controls.h"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def load_manifest():
    if not os.path.isfile(MANIFEST):
        sys.exit("upstream_check: no manifest — run tools/sync_upstream.py first")
    with open(MANIFEST) as f:
        return json.load(f)


def check_integrity(manifest):
    """Are the vendored files still byte-identical to what was synced?"""
    problems = []
    for name, digest in sorted(manifest["files"].items()):
        if name in WATCHED:
            continue                      # watched but not vendored
        path = os.path.join(VENDOR, name)
        if not os.path.isfile(path):
            problems.append((name, "missing from ogham-src/"))
        elif sha256(path) != digest:
            problems.append((name, "EDITED since it was synced"))
    return problems


def check_upstream(manifest, source):
    """Has the firmware moved under us?"""
    srcdir = os.path.join(source, FW)
    if not os.path.isdir(srcdir):
        sys.exit(f"upstream_check: {source} does not look like the Ogham firmware repo")
    try:
        sha = subprocess.run(["git", "-C", source, "rev-parse", "HEAD"],
                             capture_output=True, text=True, check=True).stdout.strip()
    except subprocess.CalledProcessError:
        sha = "?"

    changed = []
    for name, digest in sorted(manifest["files"].items()):
        path = os.path.join(srcdir, name)
        if not os.path.isfile(path):
            changed.append((name, "gone from the firmware"))
        elif sha256(path) != digest:
            changed.append((name, "changed upstream"))
    return sha, changed


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--against", help="path to a firmware clone to compare with")
    ap.add_argument("--fetch", action="store_true", help="clone upstream and compare")
    args = ap.parse_args()

    manifest = load_manifest()
    recorded = manifest.get("commit", "?")

    problems = check_integrity(manifest)
    if problems:
        print("upstream_check: ogham-src/ does not match what was synced")
        for name, why in problems:
            print(f"  {name}: {why}")
        print()
        print("Those files are the firmware's, compiled unmodified. If a change is")
        print("genuinely wanted it belongs upstream, in keeos-io/ogham, not here.")
        print("To restore them: python tools/sync_upstream.py")
        return 1
    print(f"upstream_check: ogham-src/ intact, synced from {recorded[:10]}")

    source = args.against
    tmp = None
    if args.fetch and not source:
        tmp = tempfile.TemporaryDirectory()
        source = os.path.join(tmp.name, "ogham")
        url = manifest.get("url", "https://github.com/keeos-io/ogham.git")
        print(f"upstream_check: cloning {url}")
        subprocess.run(["git", "clone", "--quiet", "--no-recurse-submodules", url, source],
                       check=True)

    if not source:
        print("upstream_check: upstream not checked (pass --against or --fetch)")
        return 0

    sha, changed = check_upstream(manifest, source)
    if sha == recorded:
        print(f"upstream_check: firmware unmoved at {sha[:10]}")
        return 0

    print(f"upstream_check: firmware moved {recorded[:10]} -> {sha[:10]}")
    if not changed:
        print("                nothing the plugin depends on changed.")
        print("                accept with: python tools/sync_upstream.py")
        return 0

    for name, why in changed:
        tag = "TRANSCRIBED" if name in WATCHED else "compiled"
        print(f"  [{tag}] {name}: {why}")
    print()
    if any(n in WATCHED for n, _ in changed):
        print("A transcribed file changed. src/OghamApp.cpp mirrors ogham_main.cpp by")
        print("hand — read the diff and decide what it means before syncing.")
    print("Record any new divergence in docs/firmware-differences.md, then:")
    print("  python tools/sync_upstream.py")
    return 1


if __name__ == "__main__":
    sys.exit(main())
