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
p = Panel(203.2)
p.border(2.4)
p.border(H - 2.4)
p.add(text("ZVUNI", 8.0, 9.6, 4.6, FONT_BOLD, INK, anchor="start", spacing=0.14))
p.add(text("it rings", 31.5, 9.4, 2.8, FONT_MED, RED, anchor="start"))
p.add(text("drone  /  melody  /  partner", p.w - 8.0, 9.4, 2.8, FONT_MED, SOFT, anchor="end"))
p.rect(6.0, 12.0, 191.2, 25.0, SCREEN, 1.6)

# sections
for x, name in ((35.6, "PARTNER"), (101.6, "VOICE"), (167.6, "PHRASE")):
    p.add(text(name, x, 41.6, 3.2, FONT_BOLD, RED, spacing=0.18))
p.vline(68.6, 39.5, 108)
p.vline(134.6, 39.5, 108)

rowA_l, rowA_k, rowA_cv = 46.2, 52.5, 65.5
rowB_l, rowB_k, rowB_cv = 73.8, 80.0, 91.5
rowC_l, rowC_k = 97.9, 103.0
cvlab = 60.6      # small labels for the controls in the CV row

# partner
p.label("TENSION", 25.6, rowA_l, 3.2)
p.hint("unison", 7.0, 61.2, 2.2)
p.hint("fifth", 25.0, 61.2, 2.2)
p.label("RING", 55.6, rowA_l, 3.2)
p.hint("CV  amt", 25.6, cvlab + 9.0, 2.2)
p.label("DRIFT", 15.6, rowB_l, 3.2)
p.label("CLING", 35.6, rowB_l, 3.2)
p.label("RANGE", 55.6, rowB_l, 3.2)
p.hint("pull to", 35.6, rowB_cv - 1.4, 2.2)
p.hint("the drone", 35.6, rowB_cv + 1.6, 2.2)
p.hint("fold into", 55.6, rowB_cv - 1.4, 2.2)
p.hint("a window", 55.6, rowB_cv + 1.6, 2.2)
p.label("MODE", 15.6, rowC_l, 2.9)
p.label("SIDE", 29.35, rowC_l, 2.9)
p.label("FLIP", 43.1, rowC_l, 2.9)
p.label("ACCENT", 56.85, rowC_l, 2.9)
p.hint("seek", 20.85, rowC_k - 3.2, 2.2)
p.hint("cents", 21.35, rowC_k + 0.8, 2.2)
p.hint("Hz", 19.85, rowC_k + 4.8, 2.2)
p.hint("up = above", 29.35, rowC_k + 6.6, 2.2)
p.hint("gate", 43.1, rowC_k + 6.6, 2.2)
p.hint("gate", 56.85, rowC_k + 6.6, 2.2)

# voice
p.label("VOWEL", 81.6, rowA_l, 3.2)
p.label("OPEN", 101.6, rowA_l, 3.2)
p.label("TRESENE", 121.6, rowA_l, 3.2)
p.label("ENSEMBLE", 81.6, rowB_l, 3.2)
p.label("NASAL", 101.6, rowB_l, 3.2)
p.label("BREATH", 121.6, rowB_l, 3.2)
p.hint("a e i o u", 81.6, cvlab - 0.4, 2.2)
p.hint("throat", 101.6, cvlab - 0.4, 2.2)
p.hint("shake", 121.6, cvlab - 0.4, 2.2)
p.hint("1 singer", 101.6, rowB_cv - 1.4, 2.2)
p.hint("> 6 singers", 101.6, rowB_cv + 1.6, 2.2)
p.label("DRONE", 81.6, rowC_l, 2.9)
p.label("PARTNER", 101.6, rowC_l, 2.9)
p.label("PARTNER", 121.6, rowC_l, 2.9)
p.hint("level", 81.6, rowC_k + 6.6, 2.2)
p.hint("level", 101.6, rowC_k + 6.6, 2.2)
p.hint("on (gate)", 121.6, rowC_k + 6.6, 2.2)

# phrase
p.label("HEIGHT", 147.6, rowA_l, 3.2)
p.label("SNAP", 167.6, rowA_l, 3.2)
p.label("HOLD", 187.6, rowA_l, 3.2)
p.label("YELP IN", 147.6, cvlab, 2.6)
p.label("AUTO", 167.6, cvlab, 2.6)
p.label("PARTNER TOO", 187.6, cvlab, 2.6)
p.label("GLIDE", 147.6, rowB_l, 3.2)
p.label("TO", 167.6, rowB_l, 3.2)
p.label("RESOLVE", 187.6, rowB_l, 3.2)
p.hint("octave", 176.6, rowB_k - 3.2, 2.2)
p.hint("fifth", 175.35, rowB_k + 0.8, 2.2)
p.hint("unison", 176.6, rowB_k + 4.8, 2.2)
p.hint("gate in", 187.6, rowB_k + 7.6, 2.2)
p.hint("Aksak END into RESOLVE", 167.6, rowC_l + 0.6, 2.2)
p.hint("= automatic cadences.", 167.6, rowC_l + 3.4, 2.2)
p.hint("Aksak ACCENT into ACCENT", 167.6, rowC_l + 7.0, 2.2)
p.hint("= cling on the strong beats.", 167.6, rowC_l + 9.8, 2.2)

# bottom jacks
jl, jy = 113.4, 118.4
for x, n in ((12, "DRONE"), (26.5, "MELODY"), (41, "GATE")):
    p.label(n, x, jl, 2.9)
p.rect(50.5, 110.2, 148.7, 14.6, OUTBOX)
outs = ["DRONE", "MELODY", "PARTNER", "MIX L", "MIX R", "MEL V/O", "PTR V/O", "ROUGH", "INTERVAL"]
for i, n in enumerate(outs):
    p.add(text(n, 59 + 16.4 * i, jl, 2.9, FONT_BOLD, OUTINK, spacing=0.03))
p.save("Zvuni.svg")

# ---------------------------------------------------------------------------
# Aksak, 10 HP
# ---------------------------------------------------------------------------
a = Panel(50.8)
a.border(2.4)
a.border(H - 2.4)
a.add(text("AKSAK", 25.4, 9.6, 4.2, FONT_BOLD, INK, spacing=0.14))
a.rect(4.0, 12.0, 42.8, 16.0, SCREEN, 1.4)
a.label("METER", 14, 33.6, 3.2)
a.label("MUTATE", 37, 33.6, 3.2)
a.hint("CV", 6.2, 52.4, 2.6)
a.hint("CV", 29.2, 52.4, 2.6)
a.add(text("CUSTOM GROUPS", 25.4, 61.9, 2.9, FONT_BOLD, RED, spacing=0.06))
a.hint("tap: off > 2 > 3", 25.4, 82.9, 2.4)
a.label("CLOCK", 14, 87.5, 2.9)
a.label("RESET", 37, 87.5, 2.9)
a.rect(3.5, 97.0, 43.8, 26.6, OUTBOX)
for x, y, n in ((14, 100.9, "STEP"), (37, 100.9, "ACCENT"), (14, 114.1, "LONG"), (37, 114.1, "END")):
    a.add(text(n, x, y, 2.9, FONT_BOLD, OUTINK, spacing=0.05))
a.save("Aksak.svg")

# --- Haer maker logo (res/haer_logo.py), placed clear of the jacks and rail artwork
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import haer_logo
haer_logo.add_to_file(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "Aksak.svg"), x=25.64, y=126.0, cap=3.0, bg='#F4F1EA', knock=(125.15, 127.05))
haer_logo.add_to_file(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "Zvuni.svg"), x=40.89, y=126.0, cap=3.0, bg='#F4F1EA', knock=(125.15, 127.05))
