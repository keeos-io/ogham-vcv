# -----------------------------------------------------------------------------
# Ogham for VCV Rack — cross-compilation image (Windows + Linux)
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
# VCV's own rack-plugin-toolchain builds all four targets, and its Dockerfile
# starts with `COPY MacOSX12.3.sdk.tar.*` — so without an Apple SDK the image
# fails before it reaches the Windows and Linux toolchains, which need nothing
# from Apple. That SDK can only be extracted on a Mac, from a specific Xcode, and
# Apple's licence forbids redistributing it, so no public image can contain one.
#
# This image is the same toolchain with the mac target made OPTIONAL: it builds
# win-x64 and lin-x64 unconditionally, which is everything reachable without a
# Mac, and adds mac-x64 and mac-arm64 if an SDK has been placed in
# tools/toolchain/sdk/. See docs/cross-compiling.md.
#
# It clones the upstream toolchain at a pinned commit and runs VCV's own
# dep-ubuntu rather than copying their dependency list here — one source of
# truth, and the pin is what makes the result reproducible.
#
#   python tools/cross_build.py image
# -----------------------------------------------------------------------------

FROM ubuntu:24.04
ENV LANG=C.UTF-8
ENV DEBIAN_FRONTEND=noninteractive

ARG JOBS
ARG TOOLCHAIN_COMMIT=4fd1318701624051cee54b385343f23d59b4845c

# Just enough to bootstrap; the real list comes from the toolchain's own rule.
RUN apt-get update && apt-get install -y --no-install-recommends \
        make sudo ca-certificates git
RUN echo "%sudo ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# The toolchains refuse to build as root, and uid 1000 keeps bind-mounted files
# owned by the invoking user on Linux hosts.
RUN useradd --non-unique --create-home --uid 1000 --gid 1000 --shell /bin/bash build \
    && usermod -aG sudo build
USER build
WORKDIR /home/build

RUN git clone https://github.com/VCVRack/rack-plugin-toolchain.git rack-plugin-toolchain \
    && cd rack-plugin-toolchain && git checkout ${TOOLCHAIN_COMMIT}
WORKDIR /home/build/rack-plugin-toolchain

# dep-ubuntu calls apt-get install without -y, so in a container it stops at
# "Do you want to continue? [Y/n]" and fails. Answering yes by configuration
# keeps their package list as the single source of truth.
RUN echo 'APT::Get::Assume-Yes "true";' | sudo tee /etc/apt/apt.conf.d/90assumeyes
RUN sudo apt-get update && make dep-ubuntu && sudo rm -rf /var/lib/apt/lists/*

# Separate layers so a failure says which toolchain failed, and a retry keeps
# whatever already succeeded.
RUN JOBS=${JOBS} make toolchain-win
# gnu.org is unreachable from this network — both the Makefile's own texinfo
# download and crosstool-ng's libiconv fetch go through it, by different routes.
# The script explains both and points them at an official GNU mirror. It sits
# after toolchain-win so that layer stays cached.
COPY --chown=build:build patch_toolchain.sh /home/build/rack-plugin-toolchain/
RUN ./patch_toolchain.sh
RUN JOBS=${JOBS} make toolchain-lin
RUN JOBS=${JOBS} make cppcheck
RUN make rack-sdk-win-x64 rack-sdk-lin-x64

# osxcross fetches binutils from gnu.org too, and its MIRROR is a plain
# assignment no environment variable can override. This edits the toolchain-mac
# recipe to rewrite it just after the clone. Placed here, below everything the
# Windows and Linux builds need, so that a macOS failure never invalidates them.
COPY --chown=build:build patch_osxcross.sh /home/build/rack-plugin-toolchain/
RUN ./patch_osxcross.sh

# macOS, if and only if an SDK has been supplied. Last, deliberately: everything
# above is reachable without a Mac, so adding an SDK later reuses those layers
# instead of rebuilding two toolchains from source.
#
# If an SDK IS present, the image is no longer redistributable — the tarball
# stays in this COPY layer even though the build deletes it afterwards.
COPY --chown=build:build sdk/ /home/build/rack-plugin-toolchain/sdk/
RUN if ls sdk/MacOSX*.sdk.tar.* >/dev/null 2>&1; then         echo "macOS SDK found; building osxcross."         && cp sdk/MacOSX*.sdk.tar.* .         && JOBS=${JOBS} make toolchain-mac         && make rack-sdk-mac-x64 rack-sdk-mac-arm64         && rm -f MacOSX*.sdk.tar.*;     else         echo "No macOS SDK in tools/toolchain/sdk/; building win-x64 and lin-x64 only.";     fi     && rm -rf sdk
