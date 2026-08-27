# -----------------------------------------------------------------------------
# Ogham for VCV Rack — host build for the parity harness
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
#
#   source tools/env.sh
#   make -f tools/host.mk                 # build with g++
#   make -f tools/host.mk CXX=clang++     # build with clang++
#   make -f tools/host.mk check           # build both, render, compare hashes
#
# Builds the firmware's DSP sources off-target against the shim in src/shim.
# No Rack, no libDaisy. The MSVC equivalent is tools/build_host.bat.
#
# -ffp-contract=off is deliberate and load-bearing. GCC and clang default to
# `fast`, which lets the compiler fuse a*b+c into a single FMA and produce a
# DIFFERENT result from an unfused multiply-add. MSVC does not contract by
# default. Without this flag the same source built by two compilers renders
# audio that differs in the last bits, and the parity hash — the whole point of
# the harness — stops meaning anything. The Rack plugin build must carry the
# same flag for its output to be comparable.
# -----------------------------------------------------------------------------

CXX      ?= g++
BUILD    ?= build_host
FW       := ogham-src
DEP      := dep/daisysp

INCLUDES := -Isrc/shim -I$(FW) -I$(DEP) -I$(DEP)/Utility

CXXFLAGS ?= -O2 -std=c++17 -ffp-contract=off -Wall -Wno-unused-variable

# The seven firmware translation units, compiled unmodified.
FW_SRC := \
	$(FW)/formulas.cpp \
	$(FW)/bytebeat_engine.cpp \
	$(FW)/bpm_clock.cpp \
	$(FW)/ogham_audio_pipeline.cpp \
	$(FW)/ogham_cv_output.cpp \
	$(FW)/ogham_display.cpp \
	$(FW)/tm1637.cpp

DEP_SRC := \
	$(DEP)/Effects/chorus.cpp \
	$(DEP)/Effects/flanger.cpp \
	$(DEP)/Effects/phaser.cpp

SHIM_SRC := src/shim/ogham_clock.cpp

RENDER := $(BUILD)/render-$(notdir $(CXX))$(EXE_SUFFIX)
CONV   := $(BUILD)/converter_test$(EXE_SUFFIX)

.PHONY: all check compile-only converter clean

all: $(RENDER)

# The boundary converter, tested on its own with a synthetic core. No Rack and
# no firmware sources — it depends on neither.
converter: $(CONV)
	@./$(CONV)

$(CONV): tests/parity/converter_test.cpp src/RateConverter.hpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -Isrc -o $@ tests/parity/converter_test.cpp

$(RENDER): tests/parity/render.cpp $(FW_SRC) $(DEP_SRC) $(SHIM_SRC)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

# Portability check with no linking: do the firmware's own sources still compile
# untouched under this compiler?
compile-only:
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -fsyntax-only $(FW_SRC) $(DEP_SRC) $(SHIM_SRC)
	@echo "OK - all units compile unmodified under $(CXX)"

# Build with both compilers, render the same script, and compare the hashes.
# They must match: the sources are identical and FP contraction is off, so any
# difference is a compiler bug or a flag that slipped.
check:
	@$(MAKE) -f tools/host.mk CXX=g++
	@$(MAKE) -f tools/host.mk CXX=clang++
	@mkdir -p tests/parity/out
	@echo "--- g++ ---"
	@./$(BUILD)/render-g++ tests/parity/scripts/smoke.csv tests/parity/out/smoke-gcc.wav 10
	@echo "--- clang++ ---"
	@./$(BUILD)/render-clang++ tests/parity/scripts/smoke.csv tests/parity/out/smoke-clang.wav 10
	@cmp tests/parity/out/smoke-gcc.wav tests/parity/out/smoke-clang.wav \
		&& echo "PASS - g++ and clang++ render identical audio" \
		|| (echo "FAIL - renders differ" && exit 1)

clean:
	rm -rf $(BUILD)
