#!/usr/bin/env python3
"""Draws Config > Language's menu icon into source/ui/qt/menu-icons: a globe
(Language.png), in the blue of the Firmware icon beside it. Drawn at 8x and
scaled down, as tools/gen-icons.py draws.

    python3 tools/draw-language-icon.py [<folder>]
"""
import os
import sys
from PIL import Image, ImageDraw
here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "..", "source", "ui", "qt", "menu-icons")
K = 8
def px(v): return int(round(v * K))

def icon():
    im = Image.new("RGBA", (16 * K, 16 * K), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    edge = (35, 75, 135, 255)
    line = (220, 236, 252, 255)
    c, r = 8.0, 7.3
    d.ellipse([px(c - r), px(c - r), px(c + r), px(c + r)], fill=(75, 135, 205, 255))
    # a meridian curving each side of the middle, and the equator: at sixteen
    # pixels more lines than these make a net, not a globe
    d.ellipse([px(c - 3.2), px(c - r), px(c + 3.2), px(c + r)], outline=line, width=K)
    d.line([px(c - r), px(c), px(c + r), px(c)], fill=line, width=K)
    d.ellipse([px(c - r), px(c - r), px(c + r), px(c + r)], outline=edge, width=K)
    return im.resize((16, 16), Image.LANCZOS)

icon().save(out + "/Language.png")
