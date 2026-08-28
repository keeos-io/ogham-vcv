#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — cross-compile for the platforms this machine is not
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
#   python tools/cross_build.py image     build the toolchain image (once, slow)
#   python tools/cross_build.py win lin   build those targets
#   python tools/cross_build.py mac       both macOS targets, if the image has them
#   python tools/cross_build.py all       everything the image can build
#   python tools/cross_build.py analyze   cppcheck over src/
#   python tools/cross_build.py shell     a prompt inside the image
#
# Packages land in dist-cross/.
#
# The macOS targets exist only if an SDK was supplied when the image was built —
# see tools/toolchain/sdk/README.md. Two images are therefore possible, and the
# tag says which one you have: :win-lin, or :all when it can build all four.
#
# The container builds a staged copy, never the working tree, so a cross build
# leaves the local plugin.dll and build/ alone. That is not politeness: the .d
# files a Windows build leaves in build/ name paths like D:/VCV/..., and GNU make
# inside the container reads that colon as a target separator and stops with
# "multiple target patterns" before any rule — `make clean` included — can run.
# -----------------------------------------------------------------------------

import argparse
import glob
import os
import shutil
import stat
import subprocess
import sys
import time

IMAGE_ALL = "ogham-toolchain:all"          # has osxcross; built with an SDK
IMAGE_WINLIN = "ogham-toolchain:win-lin"   # everything reachable without a Mac

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "dist-cross")
STAGE = os.path.join(OUT, "src")
SDK_DIR = os.path.join(ROOT, "tools", "toolchain", "sdk")

# What the container never needs to see. build/ and dist/ matter most: they hold
# this machine's Windows artifacts.
STAGE_IGNORE = shutil.ignore_patterns(
    ".git", ".beads", "build", "build_host", "dist", "dist-cross",
    "__pycache__", "*.dll", "*.so", "*.dylib")

TARGETS = {
    "win": "plugin-build-win-x64",
    "lin": "plugin-build-lin-x64",
    "mac-x64": "plugin-build-mac-x64",
    "mac-arm64": "plugin-build-mac-arm64",
}
MAC_TARGETS = ["mac-x64", "mac-arm64"]


def docker(*args, **kw):
    return subprocess.run(["docker"] + list(args), **kw)


def require_docker():
    if not shutil.which("docker"):
        sys.exit("cross_build: docker is not on PATH.\n"
                 "  Install Docker Desktop, or see docs/cross-compiling.md for "
                 "the CI route, which needs nothing installed locally.")
    if docker("info", stdout=subprocess.DEVNULL,
              stderr=subprocess.DEVNULL).returncode != 0:
        sys.exit("cross_build: docker is installed but the daemon is not "
                 "responding. Start Docker Desktop and try again.")


def sdk_present():
    return bool(glob.glob(os.path.join(SDK_DIR, "MacOSX*.sdk.tar.*")))


def image_exists(image):
    return docker("image", "inspect", image, stdout=subprocess.DEVNULL,
                  stderr=subprocess.DEVNULL).returncode == 0


def pick_image():
    """Whichever image exists, preferring the one that can build all four."""
    for image in (IMAGE_ALL, IMAGE_WINLIN):
        if image_exists(image):
            return image
    return None


def build_image(jobs):
    mac = sdk_present()
    image = IMAGE_ALL if mac else IMAGE_WINLIN
    print("Building %s with JOBS=%d." % (image, jobs))
    if mac:
        print("macOS SDK found: all four targets.")
    else:
        print("No macOS SDK in tools/toolchain/sdk/, so this image will build\n"
              "win-x64 and lin-x64. Not a failure — see\n"
              "tools/toolchain/sdk/README.md to add the two macOS targets.")
    print("Compiling cross-toolchains from source takes about an hour, once.\n")

    rc = docker("build",
                "-f", os.path.join(ROOT, "tools", "toolchain", "Dockerfile"),
                "--build-arg", "JOBS=%d" % jobs,
                "-t", image,
                "--progress=plain",
                os.path.join(ROOT, "tools", "toolchain")).returncode
    if rc != 0:
        sys.exit("cross_build: image build failed (exit %d)" % rc)
    print("\nBuilt %s." % image)


def stage_sources():
    # A clean copy for the container to build. A few MB of source and SVG, and
    # worth it twice over: the working tree keeps its Windows build, and the
    # container never sees a .d file written by it.
    def drop_readonly(func, path, _exc):
        os.chmod(path, stat.S_IWRITE)
        func(path)

    if os.path.isdir(STAGE):
        shutil.rmtree(STAGE, onerror=drop_readonly)
    shutil.copytree(ROOT, STAGE, ignore=STAGE_IGNORE)
    return STAGE


def run_in_image(image, command, source=None):
    os.makedirs(OUT, exist_ok=True)
    return docker(
        "run", "--rm",
        "--volume", "%s:/home/build/plugin-src" % (source or ROOT),
        "--volume", "%s:/home/build/rack-plugin-toolchain/plugin-build" % OUT,
        "--env", "PLUGIN_DIR=/home/build/plugin-src",
        image, "/bin/bash", "-c", command).returncode


def main():
    ap = argparse.ArgumentParser(add_help=True)
    ap.add_argument("targets", nargs="*", default=[],
                    help="image | win | lin | mac | mac-x64 | mac-arm64 | "
                         "all | analyze | shell")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()
    todo = list(args.targets) or ["all"]

    require_docker()

    if "image" in todo:
        build_image(args.jobs)
        todo = [t for t in todo if t != "image"]
        if not todo:
            return 0

    image = pick_image()
    if image is None:
        sys.exit("cross_build: no toolchain image yet.\n"
                 "  Run: python tools/cross_build.py image")
    has_mac = image == IMAGE_ALL

    if "shell" in todo:
        return run_in_image(image, "exec bash -i")
    if "analyze" in todo:
        return run_in_image(image, "make plugin-analyze")

    if "all" in todo:
        todo = ["win", "lin"] + (MAC_TARGETS if has_mac else [])
    if "mac" in todo:
        todo = [t for t in todo if t != "mac"] + MAC_TARGETS

    unknown = [t for t in todo if t not in TARGETS]
    if unknown:
        sys.exit("cross_build: unknown target(s): %s" % ", ".join(unknown))

    if not has_mac and [t for t in todo if t in MAC_TARGETS]:
        sys.exit("cross_build: %s cannot build for macOS.\n"
                 "  It was built without an SDK. Put MacOSX12.3.sdk.tar.xz in\n"
                 "  tools/toolchain/sdk/ and run `cross_build.py image` again:\n"
                 "  the Windows and Linux layers are cached, so it is quick.\n"
                 "  See tools/toolchain/sdk/README.md." % image)

    print("Using %s" % image)
    source = stage_sources()
    failed = []
    for t in todo:
        print("\n=== %s ===" % t, flush=True)
        t0 = time.time()
        rc = run_in_image(image, "make %s -j%d" % (TARGETS[t], args.jobs),
                          source)
        if rc != 0:
            failed.append(t)
            print("  FAILED (exit %d)" % rc)
        else:
            print("  built in %.0f s" % (time.time() - t0))

    print()
    shutil.rmtree(STAGE, ignore_errors=True)
    if os.path.isdir(OUT):
        for f in sorted(os.listdir(OUT)):
            path = os.path.join(OUT, f)
            if os.path.isfile(path):
                print("  dist-cross/%s  %.2f MB" % (f, os.path.getsize(path) / 1e6))
    if failed:
        print("\nFAILED: %s" % ", ".join(failed))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
