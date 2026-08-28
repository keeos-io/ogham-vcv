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
# This image is the same toolchain with the mac target left out: it builds
# win-x64 and lin-x64, which is everything reachable without a Mac. See
# docs/cross-compiling.md for how the two macOS builds are covered instead.
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
RUN JOBS=${JOBS} make toolchain-lin
RUN JOBS=${JOBS} make cppcheck
RUN make rack-sdk-win-x64 rack-sdk-lin-x64
