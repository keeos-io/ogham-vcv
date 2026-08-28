"""Build res/Ogham.svg for Rack from the production panel graphics.

The source is the JLCPCB fabrication artwork: 52.5 x 130.5 mm with 1 mm of bleed
around a 50.5 x 128.5 mm panel, in layers that describe how the board is MADE
rather than how it LOOKS. The finished panel is black soldermask, white
silkscreen legends, and gold where the mask opens over copper. So the Rack panel
composites exactly those three things, in that order, and drops the fabrication
layers (Edge.Cuts, Drill, and everything on the back).

Rack wants 10 HP = 50.8 mm, against the panel's 50.5. The artwork is PADDED by
0.15 mm each side rather than scaled, so nothing is stretched and every control
sits where the production panel puts it.
"""

import re
import sys

SRC = "panel_paths2.svg"          # Inkscape output, text already converted to paths
OUT = r"D:\ogham-vcv\res\Ogham.svg"

BLEED = 1.0        # mm of bleed in the source, each side
PANEL_W = 50.5     # the real panel
PANEL_H = 128.5
RACK_W = 50.8      # 10 HP
PAD = (RACK_W - PANEL_W) / 2.0

# The finished panel's colours, not the fabrication layer's.
BLACK = "#141618"     # matte black soldermask
WHITE = "#e8ece9"     # white silkscreen
GOLD = "#b08d3f"      # ENIG over exposed copper

s = open(SRC, encoding="utf8", errors="replace").read()


def layer(name):
    """Return the inner XML of a named Inkscape layer."""
    m = re.search(r'<g[^>]*inkscape:label="%s"[^>]*>' % re.escape(name), s)
    if not m:
        sys.exit("layer not found: " + name)
    start = m.end()
    depth, i = 1, start
    while depth and i < len(s):
        nxt, close = s.find("<g", i), s.find("</g>", i)
        if close == -1:
            break
        if nxt != -1 and nxt < close:
            depth += 1
            i = nxt + 2
        else:
            depth -= 1
            i = close + 4
    return s[start:i - 4]


def recolour(xml, fill):
    """Strip the fabrication colours and impose one fill."""
    xml = re.sub(r'\sstyle="[^"]*"', "", xml)
    xml = re.sub(r'\sfill="[^"]*"', "", xml)
    xml = re.sub(r'\sstroke="[^"]*"', "", xml)
    xml = re.sub(r'<g\b', '<g fill="%s"' % fill, xml, count=1)
    return '<g fill="%s">%s</g>' % (fill, xml)


gold = recolour(layer("F.Cu"), GOLD)
silk = recolour(layer("F.SilkS"), WHITE)

# Controls, in panel millimetres, measured from PanelPCB-v2's Edge.Cuts and
# identified from the legends in the production graphics. x is shifted by the
# padding; y is unchanged.
CONTROLS = [
    ("FUNC1_PARAM",   "encoder",  7.38, 45.19),   # Func encoder
    ("MODE_PARAM",    "switch",  25.28, 45.19),   # Clk / VOct
    ("RATE_PARAM",    "knob",    43.28, 45.19),   # Rate / Fine
    ("A_PARAM",       "knob",     7.38, 65.69),
    ("B_PARAM",       "knob",    25.28, 65.69),
    ("TONE_PARAM",    "knob",    43.28, 65.69),
    ("CV_A_INPUT",    "jack",     5.78, 97.27),
    ("CV_B_INPUT",    "jack",    18.78, 97.27),
    ("CLK_VOCT_INPUT","jack",    31.78, 97.27),
    ("SYNC_INPUT",    "jack",    44.78, 97.27),
    ("OUT1_OUTPUT",   "jack",     5.78, 113.77),
    ("OUT2_OUTPUT",   "jack",    18.78, 113.77),
    ("ENV_OUTPUT",    "jack",    31.78, 113.77),
    ("EOC_OUTPUT",    "jack",    44.78, 113.77),
]
DISPLAY = (12.40, 12.62, 29.5, 12.5)   # x, y, w, h in panel mm

RADII = {"knob": 4.0, "encoder": 4.0, "switch": 3.0, "jack": 3.0}

comps = []
for name, kind, x, y in CONTROLS:
    # fill="none": these mark where the components go, for helper.py and
    # panel_check.py. They must not be drawn — an unfilled circle defaults to
    # black, which put a disc under every knob and jack.
    comps.append('    <circle id="%s" cx="%.3f" cy="%.3f" r="%.2f" fill="none"/>'
                 % (name, x + PAD, y, RADII[kind]))

out = f'''<?xml version="1.0" encoding="UTF-8"?>
<!--
  Ogham for VCV Rack — panel.

  Generated from the production artwork, "Ogham Panel Graphics (JLCPCB) v2.svg",
  by tools/build_panel.py. Do not edit by hand: regenerate.

  The source is fabrication data for a 50.5 x 128.5 mm panel with 1 mm of bleed.
  Rack wants 10 HP = 50.8 mm, so the artwork is padded by {PAD:.2f} mm each side
  rather than scaled — nothing is stretched, and every control sits where the
  production panel puts it.

  All text is converted to paths. Rack draws panels with nanosvg, which has no
  text support whatsoever: a <text> element renders as nothing at all, silently.
-->
<svg xmlns="http://www.w3.org/2000/svg"
     xmlns:inkscape="http://www.inkscape.org/namespaces/inkscape"
     width="{RACK_W}mm" height="{PANEL_H}mm"
     viewBox="0 0 {RACK_W} {PANEL_H}" version="1.1">

  <g inkscape:groupmode="layer" inkscape:label="panel" id="panel">
    <rect x="0" y="0" width="{RACK_W}" height="{PANEL_H}" fill="{BLACK}"/>

    <!-- The artwork, less its bleed, centred in 10 HP. -->
    <g transform="translate({PAD - BLEED:.3f},{-BLEED:.3f})">
      {gold}
      {silk}
    </g>

    <!-- The display window: black glass, drawn under the widget. -->
    <rect x="{DISPLAY[0] + PAD:.3f}" y="{DISPLAY[1]:.3f}"
          width="{DISPLAY[2]:.3f}" height="{DISPLAY[3]:.3f}"
          rx="0.6" fill="#0a0c0d" stroke="#2a2f31" stroke-width="0.2"/>
  </g>

  <!-- Where the components go, for helper.py and tools/panel_check.py. -->
  <g inkscape:groupmode="layer" inkscape:label="components" id="components">
{chr(10).join(comps)}
  </g>
</svg>
'''

open(OUT, "w", encoding="utf8").write(out)
print("wrote", OUT)
print(f"  {RACK_W} x {PANEL_H} mm, artwork padded {PAD:.2f} mm each side")
print(f"  {len(CONTROLS)} components, display window {DISPLAY[2]} x {DISPLAY[3]} mm")
