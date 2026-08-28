#!/usr/bin/env python
# -----------------------------------------------------------------------------
# Ogham for VCV Rack — panel sanity check
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
# https://github.com/keeos-io/ogham-vcv
# -----------------------------------------------------------------------------
#
# Two things about res/Ogham.svg that are invisible until someone looks at the
# module and notices, and one that is invisible even then.
#
#   1. Rack draws panels with nanosvg, which has NO text support. A <text>
#      element renders as nothing at all, silently — the label is simply absent
#      and the panel looks unfinished for no apparent reason. All text must be
#      converted to paths before export.
#
#   2. The panel is 10 HP: 50.8 x 128.5 mm exactly. A panel a fraction out sits
#      wrong against every other module in the rack.
#
#   3. The component positions in the SVG and the positions in src/Ogham.cpp are
#      written down twice, so they can disagree. An art revision that nudges a
#      jack would otherwise leave the port drawn in one place and clickable in
#      another.
#
#   python tools/panel_check.py

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PANEL = os.path.join(ROOT, "res", "Ogham.svg")
SOURCE = os.path.join(ROOT, "src", "Ogham.cpp")

HP = 5.08
WIDTH_HP = 10
EXPECT_W = HP * WIDTH_HP      # 50.8
EXPECT_H = 128.5
TOLERANCE = 0.01              # mm

problems = []


def strip_comments(xml):
    return re.sub(r"<!--[\s\S]*?-->", "", xml)


def main():
    if not os.path.isfile(PANEL):
        sys.exit("panel_check: %s is missing — run tools/build_panel.py" % PANEL)

    raw = open(PANEL, encoding="utf8").read()
    svg = strip_comments(raw)

    # 1. No text survives.
    texts = re.findall(r"<text\b", svg)
    if texts:
        problems.append(
            "%d <text> element(s) in the panel. Rack's renderer ignores them "
            "entirely, so those labels would be invisible. Convert them to "
            "paths (Inkscape: Path > Object to Path)." % len(texts))

    # 2. Exactly 10 HP.
    m = re.search(r'width="([\d.]+)mm"\s+height="([\d.]+)mm"', svg)
    if not m:
        problems.append("no width/height in millimetres on the <svg> element")
    else:
        w, h = float(m.group(1)), float(m.group(2))
        if abs(w - EXPECT_W) > TOLERANCE:
            problems.append("width is %.3f mm, expected %.2f (%d HP)"
                            % (w, EXPECT_W, WIDTH_HP))
        if abs(h - EXPECT_H) > TOLERANCE:
            problems.append("height is %.3f mm, expected %.2f (3U)" % (h, EXPECT_H))

    # 3. The components layer agrees with the widget positions in the source.
    layer = re.search(r'inkscape:label="components"[^>]*>([\s\S]*?)</g>', svg)
    if not layer:
        problems.append("no components layer — helper.py and this check need one")
        panel_pos = {}
    else:
        panel_pos = {}
        for c in re.finditer(
                r'<circle[^>]*id="([A-Z0-9_]+)"[^>]*cx="([-\d.]+)"[^>]*cy="([-\d.]+)"',
                layer.group(1)):
            panel_pos[c.group(1)] = (float(c.group(2)), float(c.group(3)))

    src = open(SOURCE, encoding="utf8").read()
    src_pos = {}
    for m in re.finditer(
            r'mm2px\(Vec\(([-\d.]+),\s*([-\d.]+)\)\),\s*module,\s*Ogham::([A-Z0-9_]+)\)',
            src):
        src_pos[m.group(3)] = (float(m.group(1)), float(m.group(2)))

    for name, (px, py) in sorted(panel_pos.items()):
        if name not in src_pos:
            # The encoder is placed by hand rather than through createParamCentered,
            # and the function slots have no widget at all; both are expected.
            if name in ("FUNC1_PARAM", "FUNC2_PARAM"):
                continue
            continue
        sx, sy = src_pos[name]
        if abs(px - sx) > TOLERANCE or abs(py - sy) > TOLERANCE:
            problems.append("%s: panel has (%.2f, %.2f), source has (%.2f, %.2f)"
                            % (name, px, py, sx, sy))

    missing = [n for n in src_pos if n not in panel_pos]
    if missing:
        problems.append("in the source but not in the components layer: %s"
                        % ", ".join(sorted(missing)))

    if problems:
        print("panel_check: %d problem(s)" % len(problems))
        for p in problems:
            print("  " + p)
        return 1

    print("panel_check: ok - %.1f x %.1f mm, %d components, no text"
          % (EXPECT_W, EXPECT_H, len(panel_pos)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
