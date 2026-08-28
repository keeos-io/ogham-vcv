#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — cross-compile for Windows and Linux
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
#   python tools/cross_build.py image     build the toolchain image (once, slow)
#   python tools/cross_build.py win lin   build those targets
#   python tools/cross_build.py analyze   cppcheck over src/
#   python tools/cross_build.py shell     a prompt inside the image
#
# Packages land in dist-cross/. The macOS targets are not here and cannot be:
# see docs/cross-compiling.md.
#
# Note that the container builds the working tree in place, and the toolchain
# runs `make clean` around each target — so a cross build removes the local
# plugin.dll and build/. Run `make` again to get them back.
# -----------------------------------------------------------------------------

import argparse
import os
import shutil
import subprocess
import sys
import time

IMAGE = "ogham-toolchain:win-lin"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "dist-cross")

TARGETS = {
    "win": "plugin-build-win-x64",
    "lin": "plugin-build-lin-x64",
}


def docker(*args, **kw):
    return subprocess.run(["docker"] + list(args), **kw)


def have_docker():
    if not shutil.which("docker"):
        sys.exit("cross_build: docker is not on PATH.\n"
                 "  Install Docker Desktop, or see docs/cross-compiling.md for "
                 "the CI route that needs nothing installed locally.")
    if docker("info", stdout=subprocess.DEVNULL,
              stderr=subprocess.DEVNULL).returncode != 0:
        sys.exit("cross_build: docker is installed but the daemon is not "
                 "responding. Start Docker Desktop and try again.")


def have_image():
    return docker("image", "inspect", IMAGE, stdout=subprocess.DEVNULL,
                  stderr=subprocess.DEVNULL).returncode == 0


def build_image(jobs):
    print("Building %s with JOBS=%d.\n"
          "This compiles two cross-toolchains from source and takes about an "
          "hour; it is needed once.\n" % (IMAGE, jobs))
    rc = docker("build",
                "-f", os.path.join(ROOT, "tools", "toolchain", "Dockerfile"),
                "--build-arg", "JOBS=%d" % jobs,
                "-t", IMAGE,
                "--progress=plain",
                os.path.join(ROOT, "tools", "toolchain")).returncode
    if rc != 0:
        sys.exit("cross_build: image build failed (exit %d)" % rc)
    print("\nImage built.")


def run_in_image(command):
    os.makedirs(OUT, exist_ok=True)
    return docker(
        "run", "--rm",
        "--volume", "%s:/home/build/plugin-src" % ROOT,
        "--volume", "%s:/home/build/rack-plugin-toolchain/plugin-build" % OUT,
        "--env", "PLUGIN_DIR=/home/build/plugin-src",
        IMAGE, "/bin/bash", "-c", command).returncode


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("targets", nargs="*", default=[],
                    help="image | win | lin | all | analyze | shell")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    todo = args.targets or ["all"]

    have_docker()

    if "image" in todo:
        build_image(args.jobs)
        todo = [t for t in todo if t != "image"]
        if not todo:
            return 0

    if not have_image():
        sys.exit("cross_build: %s does not exist yet.\n"
                 "  Run: python tools/cross_build.py image" % IMAGE)

    if "shell" in todo:
        return run_in_image("exec bash -i")

    if "analyze" in todo:
        return run_in_image("make plugin-analyze")

    if "all" in todo:
        todo = ["win", "lin"]

    unknown = [t for t in todo if t not in TARGETS]
    if unknown:
        sys.exit("cross_build: unknown target(s): %s" % ", ".join(unknown))

    failed = []
    for t in todo:
        print("\n=== %s ===" % t, flush=True)
        t0 = time.time()
        rc = run_in_image("make %s -j%d" % (TARGETS[t], args.jobs))
        if rc != 0:
            failed.append(t)
            print("  FAILED (exit %d)" % rc)
        else:
            print("  built in %.0f s" % (time.time() - t0))

    print()
    if os.path.isdir(OUT):
        for f in sorted(os.listdir(OUT)):
            p = os.path.join(OUT, f)
            print("  dist-cross/%s  %.2f MB" % (f, os.path.getsize(p) / 1e6))
    if failed:
        print("\nFAILED: %s" % ", ".join(failed))
        return 1
    print("\nThe local plugin.dll and build/ were cleaned by the toolchain; "
          "run `make` to rebuild them.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
