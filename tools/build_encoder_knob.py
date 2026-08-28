#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — the gold encoder cap
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
# Recolours Rack's RoundBigBlackKnob_bg into the module's gold encoder cap.
#
# The background frame rather than the knob proper, deliberately: the knob frame
# carries an indicator line, and an endless encoder has no position for one to
# point at. What the background does have is a ring of light-catching facets, so
# it still shows the cap turning.
#
# The result is a derivative of a VCV Component Library graphic, which is
# CC BY-NC 4.0 — fine for a plugin distributed free of charge, with the credit in
# THIRD-PARTY.md. It is one of the things that would have to be redrawn from
# scratch before this could ever be sold outside the VCV Library.
#
#   python tools/build_encoder_knob.py

import os
import re
import sys

RACK_DIR = os.environ.get("RACK_DIR", "")
SRC_CANDIDATES = [
    os.path.join(RACK_DIR, "..", "..", "Rack", "res", "ComponentLibrary",
                 "RoundBigBlackKnob_bg.svg"),
    r"D:\VCV\Rack\res\ComponentLibrary\RoundBigBlackKnob_bg.svg",
]
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "res", "components", "OghamEncoder.svg")

# Grey to gold. The rim keeps its strong light-to-dark sweep, which is what makes
# it read as turned metal rather than a flat disc; the body sits a little darker
# than the rim so the facets stay visible against it.
RECOLOUR = {
    "#B0ACAE": "#E6C888",   # rim highlight
    "#000000": "#2E220B",   # rim shadow — bronze, not black
    "#232223": "#A8813A",   # body, lit
    "#1F1E1F": "#7C5C1F",   # body, shaded
    "#FFFFFF": "#FFF0CC",   # the facets: specular on gold, not white
}


def main():
    src = next((p for p in SRC_CANDIDATES if os.path.isfile(p)), None)
    if not src:
        sys.exit("build_encoder_knob: RoundBigBlackKnob_bg.svg not found; "
                 "set RACK_DIR or edit SRC_CANDIDATES")

    s = open(src, encoding="utf8").read()
    for old, new in RECOLOUR.items():
        s = re.sub(re.escape(old), new, s, flags=re.IGNORECASE)

    # Two of the facets carry no fill at all, only opacity, so they default to
    # black — which on a gold cap reads as a hole rather than a shadow. Give them
    # a dark bronze so the whole thing stays one metal.
    s = re.sub(r'<path\s+opacity="0.5"', '<path fill="#3A2B0E" opacity="0.5"', s)

    credit = ("<!-- Derived from VCV Rack's RoundBigBlackKnob_bg.svg, "
              "(c) VCV, CC BY-NC 4.0. Recoloured for Ogham by "
              "tools/build_encoder_knob.py — do not edit by hand. See "
              "THIRD-PARTY.md. -->\n")
    s = s.replace("<svg", credit + "<svg", 1)

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    open(OUT, "w", encoding="utf8").write(s)
    print("wrote", OUT)
    for old, new in RECOLOUR.items():
        print("  %s -> %s" % (old, new))
    return 0


if __name__ == "__main__":
    sys.exit(main())
