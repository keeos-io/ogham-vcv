# -----------------------------------------------------------------------------
# Ogham for VCV Rack
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
#
#   source tools/env.sh && make
#
# Compiles the plugin plus seven of the Ogham firmware's own translation units,
# taken unmodified from the `ogham/` submodule. See README.md for which, and
# docs/firmware-differences.md for every place the plugin departs from the
# module.
# -----------------------------------------------------------------------------

RACK_DIR ?= ../Rack-SDK

# arch.mk asks $(CC) -dumpmachine to work out the target. Make's built-in
# default for CC is `cc`, which a WinLibs/MinGW toolchain does not ship — it has
# gcc and g++ and nothing aliased. Override the built-in default only: anything
# set in the environment, on the command line or by a cross-compile setup still
# wins.
ifeq ($(origin CC),default)
CC := gcc
endif
ifeq ($(origin CXX),default)
CXX := g++
endif

FW  := ogham-src

# Vendored DaisySP. NOT in dep/, deliberately: Rack's dep.mk owns that name and
# `make cleandep` is `rm -rf dep`, which the plugin toolchain runs before every
# platform build. dep/ is scratch space for dependencies a build downloads and
# can recreate; these four files are checked-in source and have to survive.
#
# Under thirdparty/ because the Rack library's static analysis excludes that
# name automatically, which is the right outcome: this is unmodified
# third-party source and its warnings are not ours to fix.
DEP := thirdparty/daisysp

# src/shim first: it is where `daisy_seed.h` is found, and no real libDaisy may
# be reachable. src/Ogham.cpp fails to compile if a different one wins.
FLAGS += -Isrc -Isrc/shim -I$(FW) -I$(DEP) -I$(DEP)/Utility

# Parity flags, and not optional. Rack's compile.mk builds plugins with
# -O3 -funsafe-math-optimizations, which reassociates the DSP's arithmetic and
# changes about two thirds of the rendered samples — inaudible at -118 dBFS, but
# it costs the null test against the offline reference render, which is the main
# way this port is verified. -ffp-contract=off likewise: GCC and clang default
# to fusing a*b+c into an FMA, and only agree with each other when it is off.
#
# EXTRA_FLAGS is appended last by compile.mk, so these win. Applied to the whole
# plugin rather than just the firmware sources: our own code wants the same
# determinism, and the measured cost is about 3% on a workload under 1% of a
# core. See the build-determinism note in docs/firmware-differences.md.
EXTRA_FLAGS += -ffp-contract=off -fno-unsafe-math-optimizations

# Plugin sources
SOURCES += $(wildcard src/*.cpp)
SOURCES += src/shim/ogham_clock.cpp

# The Ogham firmware, compiled verbatim from the submodule.
SOURCES += $(FW)/formulas.cpp
SOURCES += $(FW)/bytebeat_engine.cpp
SOURCES += $(FW)/bpm_clock.cpp
SOURCES += $(FW)/ogham_audio_pipeline.cpp
SOURCES += $(FW)/ogham_cv_output.cpp
SOURCES += $(FW)/ogham_display.cpp
SOURCES += $(FW)/tm1637.cpp

# DaisySP subset, vendored (MIT — see THIRD-PARTY.md).
SOURCES += $(DEP)/Effects/chorus.cpp
SOURCES += $(DEP)/Effects/flanger.cpp
SOURCES += $(DEP)/Effects/phaser.cpp

DISTRIBUTABLES += res
DISTRIBUTABLES += LICENSE.txt THIRD-PARTY.md README.md

include $(RACK_DIR)/plugin.mk

# Verify the submodule has not moved under a file the plugin compiles or
# transcribes. Not part of `all`: it is a prompt to go and look, not a build
# error, so CI runs it explicitly.
.PHONY: upstream-check
upstream-check:
	python tools/upstream_check.py
