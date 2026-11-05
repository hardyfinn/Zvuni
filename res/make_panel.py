#!/usr/bin/env python3
# Draws res/Zvuni.svg and res/Aksak.svg (white panels). VCV Rack can't draw
# SVG <text>, so every label is turned into outline paths with fontTools.
# Positions are in mm and must match src/Zvuni.cpp and src/Aksak.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

H = 128.5
PAPER = "#F4F1EA"
INK = "#1F2328"
SOFT = "#6E6A62"
RED = "#B3242B"       # embroidery red
OUTBOX = "#2A2D33"
OUTINK = "#F4F1EA"
SCREEN = "#15161A"

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = x - width * scale / 2 if anchor == "middle" else (x - width * scale if anchor == "end" else x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


class Panel:
    def __init__(self, w):
        self.w = w
        self.parts = [f'<rect width="{w}" height="{H}" fill="{PAPER}"/>']

    def add(self, s):
        self.parts.append(s)

    def border(self, y):
        """A row of small embroidery diamonds."""
        x = 3.0
        while x < self.w - 3.0:
            self.add(f'<path d="M{x:.2f} {y - 0.9:.2f} L{x + 0.9:.2f} {y:.2f} L{x:.2f} {y + 0.9:.2f} '
                     f'L{x - 0.9:.2f} {y:.2f} Z" fill="{RED}"/>')
            x += 3.2

    def vline(self, x, y1, y2):
        self.add(f'<line x1="{x}" y1="{y1}" x2="{x}" y2="{y2}" stroke="#CFC8B8" stroke-width="0.35" '
                 f'stroke-dasharray="0.9 0.9"/>')

    def rect(self, x, y, w, h, fill, rx=1.2):
        self.add(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"/>')

    def label(self, s, x, y, size=2.3, fill=INK, font=FONT_BOLD):
        self.add(text(s, x, y, size, font, fill, spacing=0.06))

    def hint(self, s, x, y, size=1.55, fill=SOFT):
        self.add(text(s, x, y, size, FONT_MED, fill))

    def save(self, name):
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{self.w}mm" height="{H}mm" '
               f'viewBox="0 0 {self.w} {H}">\n' + "\n".join(self.parts) + "\n</svg>\n")
        with open(os.path.join(HERE, name), "w") as fh:
            fh.write(svg)
        print("wrote", name)


# ---------------------------------------------------------------------------
# Zvuni, 32 HP
# ---------------------------------------------------------------------------
p = Panel(162.56)
p.border(2.4)
p.border(H - 2.4)
p.add(text("ZVUNI", 8.0, 9.6, 4.6, FONT_BOLD, INK, anchor="start", spacing=0.14))
p.add(text("it rings", 31.5, 9.4, 2.2, FONT_MED, RED, anchor="start"))
p.add(text("drone  /  melody  /  partner", p.w - 8.0, 9.4, 2.2, FONT_MED, SOFT, anchor="end"))
p.rect(6.0, 12.0, 150.56, 25.0, SCREEN, 1.6)

# sections
for x, name in ((27, "PARTNER"), (80, "VOICE"), (133, "PHRASE")):
    p.add(text(name, x, 41.4, 2.6, FONT_BOLD, RED, spacing=0.18))
p.vline(53.3, 39.5, 106)
p.vline(106.6, 39.5, 106)

rowA_l, rowA_k, rowA_cv = 45.6, 52.5, 65.5
rowB_l, rowB_k, rowB_cv = 72.0, 78.5, 90.0
rowC_l, rowC_k = 95.8, 101.0
cvlab = 60.6      # small labels for the controls in the CV row

# partner
p.label("TENSION", 19, rowA_l)
p.hint("unison", 9.0, 59.4)
p.hint("fifth", 29.4, 59.4)
p.label("RING", 43, rowA_l)
p.hint("CV  amt", 19, cvlab + 9.0, 1.4)
p.label("DRIFT", 11, rowB_l)
p.label("CLING", 27, rowB_l)
p.label("RANGE", 43, rowB_l)
p.hint("pull to", 27, rowB_cv - 1.2)
p.hint("the drone", 27, rowB_cv + 1.0)
p.hint("fold into", 43, rowB_cv - 1.2)
p.hint("a window", 43, rowB_cv + 1.0)
p.label("MODE", 11, rowC_l, 2.0)
p.label("SIDE", 22, rowC_l, 2.0)
p.label("FLIP", 33, rowC_l, 2.0)
p.label("ACCENT", 44, rowC_l, 2.0)
p.hint("seek", 15.2, rowC_k - 3.0, 1.4)
p.hint("cents", 15.6, rowC_k + 0.5, 1.4)
p.hint("Hz", 14.4, rowC_k + 4.0, 1.4)
p.hint("up = above", 22, rowC_k + 5.8, 1.4)
p.hint("gate", 33, rowC_k + 5.8, 1.4)
p.hint("gate", 44, rowC_k + 5.8, 1.4)

# voice
p.label("VOWEL", 64, rowA_l)
p.label("OPEN", 80, rowA_l)
p.label("TRESENE", 96, rowA_l)
p.label("ENSEMBLE", 64, rowB_l)
p.label("NASAL", 80, rowB_l)
p.label("BREATH", 96, rowB_l)
p.hint("a e i o u", 64, cvlab - 0.4, 1.4)
p.hint("throat", 80, cvlab - 0.4, 1.4)
p.hint("shake", 96, cvlab - 0.4, 1.4)
p.hint("1 singer", 80, rowB_cv - 1.2)
p.hint("> 6 singers", 80, rowB_cv + 1.0)
p.label("DRONE", 64, rowC_l, 2.0)
p.label("PARTNER", 80, rowC_l, 2.0)
p.label("PARTNER", 96, rowC_l, 2.0)
p.hint("level", 64, rowC_k + 5.8, 1.4)
p.hint("level", 80, rowC_k + 5.8, 1.4)
p.hint("on (gate)", 96, rowC_k + 5.8, 1.4)

# phrase
p.label("HEIGHT", 117, rowA_l)
p.label("SNAP", 133, rowA_l)
p.label("HOLD", 149, rowA_l)
p.label("YELP IN", 117, cvlab, 1.7)
p.label("AUTO", 133, cvlab, 1.7)
p.label("PARTNER TOO", 149, cvlab, 1.7)
p.label("GLIDE", 117, rowB_l)
p.label("TO", 133, rowB_l)
p.label("RESOLVE", 149, rowB_l)
p.hint("octave", 137.6, rowB_k - 3.0, 1.4)
p.hint("fifth", 137.0, rowB_k + 0.5, 1.4)
p.hint("unison", 137.8, rowB_k + 4.0, 1.4)
p.hint("gate in", 149, rowB_k + 6.4, 1.4)
p.hint("Aksak END into RESOLVE", 133, rowC_l + 1.0, 1.5)
p.hint("= automatic cadences.", 133, rowC_l + 3.1, 1.5)
p.hint("Aksak ACCENT into ACCENT", 133, rowC_l + 6.0, 1.5)
p.hint("= cling on the strong beats.", 133, rowC_l + 8.1, 1.5)

# bottom jacks
jl = 111.6
for x, n in ((11, "DRONE"), (24, "MELODY"), (37, "GATE")):
    p.label(n, x, jl, 1.9)
p.hint("V/oct  (poly)", 17.5, 124.6, 1.45)
p.rect(49.5, 108.0, 108.5, 14.8, OUTBOX)
outs = ["DRONE", "MELODY", "PARTNER", "MIX L", "MIX R", "MEL V/O", "PTR V/O", "ROUGH", "INTERVAL"]
for i, n in enumerate(outs):
    p.add(text(n, 56 + 12 * i, jl, 1.75, FONT_BOLD, OUTINK, spacing=0.04))
p.save("Zvuni.svg")

# ---------------------------------------------------------------------------
# Aksak, 10 HP
# ---------------------------------------------------------------------------
a = Panel(50.8)
a.border(2.4)
a.border(H - 2.4)
a.add(text("AKSAK", 25.4, 9.6, 4.2, FONT_BOLD, INK, spacing=0.14))
a.rect(4.0, 12.0, 42.8, 16.0, SCREEN, 1.4)
a.label("METER", 14, 33.0)
a.label("MUTATE", 37, 33.0)
a.hint("CV", 14, 57.3)
a.hint("CV", 37, 57.3)
a.add(text("CUSTOM GROUPS", 25.4, 61.6, 2.0, FONT_BOLD, RED, spacing=0.12))
a.hint("tap: off > 2 > 3", 25.4, 81.6, 1.45)
a.label("CLOCK", 14, 86.6, 2.0)
a.label("RESET", 37, 86.6, 2.0)
a.rect(3.5, 98.0, 43.8, 24.6, OUTBOX)
for x, y, n in ((14, 101.8, "STEP"), (37, 101.8, "ACCENT"), (14, 113.0, "LONG"), (37, 113.0, "END")):
    a.add(text(n, x, y, 1.9, FONT_BOLD, OUTINK, spacing=0.05))
a.save("Aksak.svg")
