#!/usr/bin/env python3
"""Draws the icons Chimera owns, replacing art inherited from BizHawk that
depicted copyrighted Nintendo game objects (a plumber sprite, the fire and
ice flowers, the question block).

    python3 tools/gen-icons.py [--preview <file.png>]

What it writes, into source/Chimera.Client.GUI/images:

  TAStudio.png / TAStudio.ico   the Chimera eye in TAStudio's own colours
  Freeze.png   / Freeze.ico     a snowflake (freeze an address; also cheats)
  Unfreeze.png                  the same flake, thawed
  RetroQuestion.png             a plain question mark (unrecognised rom)
  commandWindow.ico             a console window (the log window's icon; the
                                art it replaces carried BizHawk's hawk)
  HomeBrew.png                  a beaker (the homebrew rom status). The art it
                                replaces was a cauldron from the CC-BY
                                FatCow Farm-Fresh set - licensed, not game
                                art, but this way the icon is ours

The eye is drawn from the same 8x8 cell grid as images/chimera.png, so the
mark stays the mark and only the palette changes. Everything else is drawn
at 8x and downsampled, which is what gives the small sizes their soft edges
(the neighbouring toolbar icons look the same way).
"""
import argparse
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGES = os.path.join(HERE, "..", "source", "Chimera.Client.GUI", "images")

# the brand palette, read off images/chimera.png
TEAL = (45, 179, 166)
PURPLE = (138, 99, 232)
SLATE = (74, 85, 104)
AMBER = (246, 168, 33)
IRIS = (59, 130, 246)

# TAStudio's eye: the same mark in the tool's own colours. A triadic shift of
# the brand hues - unmistakably the same eye, never mistaken for the app icon
# at toolbar size.
T_RING_A = (206, 74, 168)   # was teal
T_RING_B = (232, 116, 63)   # was purple
T_RING_C = (86, 196, 118)   # was amber
T_SLATE = (74, 85, 104)     # unchanged: the neutral quarter
T_IRIS = (236, 84, 122)     # was blue

def draw_eye(size, source="chimera.png"):
    """The Chimera mark itself, recoloured.

    Reads the mark and substitutes each brand colour for TAStudio's, keeping
    every pixel's shading and alpha - so the shape, the ring gaps and the
    pupil are the artwork's, not an approximation of it. The 16px icon comes
    from the hand-tuned ChimeraSmall.png rather than a downsample of the big
    one, because at that size every pixel is a decision someone made.
    """
    src = Image.open(os.path.join(os.path.normpath(IMAGES), source)).convert("RGBA")
    swap = {TEAL: T_RING_A, PURPLE: T_RING_B, AMBER: T_RING_C, SLATE: T_SLATE, IRIS: T_IRIS}
    brands = list(swap)
    out = Image.new("RGBA", src.size, (0, 0, 0, 0))
    sp, op = src.load(), out.load()
    for y in range(src.height):
        for x in range(src.width):
            r, g, b, a = sp[x, y]
            if a == 0:
                continue
            # nearest brand colour decides the substitution; the pixel's own
            # brightness against that colour carries the anti-aliasing over
            near = min(brands, key=lambda c: (c[0] - r) ** 2 + (c[1] - g) ** 2 + (c[2] - b) ** 2)
            tr, tg, tb = swap[near]
            scale = (sum((r, g, b)) + 1) / (sum(near) + 1)
            op[x, y] = (min(255, int(tr * scale)), min(255, int(tg * scale)),
                        min(255, int(tb * scale)), a)
    return out if out.size == (size, size) else out.resize((size, size), Image.LANCZOS)


def draw_flake(size, thawed=False):
    """A six-armed snowflake: freeze, and its thawed twin."""
    S = 256
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    import math

    # thawed keeps every arm and loses its colour: the pair has to read as
    # one state and its opposite, not as a flake and a broken flake
    # the thawed flake keeps a slate-blue cast rather than going pale grey:
    # a light toolbar swallows pale grey at 16px
    body = (104, 120, 138) if thawed else (72, 178, 236)
    core = (150, 165, 182) if thawed else (206, 236, 252)
    cx, cy = S / 2, S / 2 - (S * 0.03 if thawed else 0)
    arm = S * 0.44 * (0.88 if thawed else 1.0)
    width = int(S * 0.045)

    for i in range(6):
        ang = math.radians(i * 60 + 90)
        ex, ey = cx + math.cos(ang) * arm, cy + math.sin(ang) * arm
        d.line([cx, cy, ex, ey], fill=body + (255,), width=width)
        for frac, blen in ((0.50, 0.30), (0.80, 0.20)):
            bx, by = cx + math.cos(ang) * arm * frac, cy + math.sin(ang) * arm * frac
            for side in (-1, 1):
                bang = ang + side * math.radians(45)
                d.line([bx, by,
                        bx + math.cos(bang) * arm * blen,
                        by + math.sin(bang) * arm * blen],
                       fill=body + (255,), width=max(2, int(width * 0.75)))

    r = S * 0.07
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=core + (255,))

    if thawed:
        # one drop coming off it: frozen, and no longer frozen
        drop = (72, 178, 236, 255)
        dx, dy = cx + arm * 0.70, cy + arm * 0.95
        dr = S * 0.085
        d.ellipse([dx - dr, dy - dr * 0.8, dx + dr, dy + dr * 1.2], fill=drop)
        d.polygon([(dx, dy - dr * 2.2), (dx - dr * 0.8, dy + dr * 0.1),
                   (dx + dr * 0.8, dy + dr * 0.1)], fill=drop)

    return img.resize((size, size), Image.LANCZOS)


def draw_question(size):
    """A plain question mark, in the amber the status bar used before."""
    S = 256
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body = (246, 168, 33, 255)
    edge = (120, 74, 6, 255)

    def glyph(dx, dy, fill, w):
        # one stroke: the hook sweeps over the top and comes down into the
        # stem, then the dot below it
        d.arc([70 + dx, 30 + dy, 190 + dx, 150 + dy], start=160, end=10, fill=fill, width=w)
        d.line([185 + dx, 108 + dy, 132 + dx, 168 + dy], fill=fill, width=w)
        d.line([132 + dx, 160 + dy, 132 + dx, 182 + dy], fill=fill, width=w)
        r = 20
        d.ellipse([132 - r + dx, 212 - r + dy, 132 + r + dx, 212 + r + dy], fill=fill)

    # a dark edge first, then the body over it: legible on light and dark alike
    for ox, oy in ((-7, 0), (7, 0), (0, -7), (0, 7), (-5, -5), (5, 5), (-5, 5), (5, -5)):
        glyph(ox, oy, edge, 42)
    glyph(0, 0, body, 30)
    return img.resize((size, size), Image.LANCZOS)


def draw_console(size):
    """A console window with a prompt: what the log window actually shows."""
    S = 256
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    frame = SLATE + (255,)
    face = (24, 28, 34, 255)
    bar = (52, 60, 74, 255)
    text = TEAL + (255,)
    caret = AMBER + (255,)

    box = [18, 40, S - 18, S - 40]
    d.rounded_rectangle(box, radius=18, fill=face, outline=frame, width=10)
    # title bar
    d.rounded_rectangle([box[0] + 5, box[1] + 5, box[2] - 5, box[1] + 46],
                        radius=14, fill=bar)
    d.rectangle([box[0] + 5, box[1] + 32, box[2] - 5, box[1] + 46], fill=bar)
    for i, cx in enumerate((box[0] + 28, box[0] + 54, box[0] + 80)):
        d.ellipse([cx - 7, box[1] + 18, cx + 7, box[1] + 32],
                  fill=(AMBER if i == 0 else (TEAL if i == 1 else PURPLE)) + (255,))

    # the prompt: a chevron, a line of output, and a caret
    y = box[1] + 84
    d.line([box[0] + 30, y, box[0] + 58, y + 22], fill=text, width=12)
    d.line([box[0] + 58, y + 22, box[0] + 30, y + 44], fill=text, width=12)
    d.rectangle([box[0] + 74, y + 8, box[2] - 34, y + 30], fill=text)
    d.rectangle([box[0] + 30, y + 74, box[0] + 150, y + 94], fill=(120, 132, 148, 255))
    d.rectangle([box[0] + 30, y + 118, box[0] + 62, y + 140], fill=caret)

    return img.resize((size, size), Image.LANCZOS)


def draw_beaker(size):
    """A flask with something brewing in it: a rom somebody made themselves.

    Kept to a silhouette - at 16px a glass outline turns to mush, so the
    shape carries it and the contents supply the colour.
    """
    S = 256
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    glass = (222, 230, 240, 255)
    edge = (58, 68, 84, 255)
    brew = TEAL + (255,)
    bubble = AMBER + (255,)

    neck_l, neck_r, neck_top, shoulder = 96, 160, 44, 116
    base_l, base_r, base_y = 34, 222, 214
    flask = [(neck_l, neck_top), (neck_r, neck_top), (neck_r, shoulder),
             (base_r, base_y), (base_l, base_y), (neck_l, shoulder)]

    d.polygon(flask, fill=glass, outline=edge)
    for i in range(len(flask)):
        d.line([flask[i], flask[(i + 1) % len(flask)]], fill=edge, width=14)
    # the lip, so the neck does not read as a stem
    d.line([(neck_l - 16, neck_top + 4), (neck_r + 16, neck_top + 4)], fill=edge, width=16)

    # the brew: a level line, then everything below it
    level = 150
    d.polygon([(neck_l + 6 + (level - shoulder) * 0.0, level),
               (neck_r - 6, level),
               (base_r - 12, base_y - 10), (base_l + 12, base_y - 10)], fill=brew)
    span = (base_r - base_l) * (level - shoulder) / (base_y - shoulder) / 2
    d.polygon([(128 - span - 20, level), (128 + span + 20, level),
               (base_r - 12, base_y - 10), (base_l + 12, base_y - 10)], fill=brew)

    # one bubble inside it - a second one outside the glass just reads as a
    # stray pixel once this is 16 across
    d.ellipse([114, 112, 146, 144], fill=bubble)

    return img.resize((size, size), Image.LANCZOS)


def save_ico(img, path, sizes=(16, 32, 48, 64)):
    img.save(path, format="ICO", sizes=[(s, s) for s in sizes])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", help="also write a scaled-up sheet here")
    args = ap.parse_args()

    out = os.path.normpath(IMAGES)
    if not os.path.isdir(out):
        sys.exit(f"no images directory at {out}")

    tas = draw_eye(16, source="ChimeraSmall.png")
    tas.save(os.path.join(out, "TAStudio.png"))
    save_ico(draw_eye(64), os.path.join(out, "TAStudio.ico"), sizes=(16, 32, 48, 64))

    freeze = draw_flake(16)
    freeze.save(os.path.join(out, "Freeze.png"))
    save_ico(draw_flake(64), os.path.join(out, "Freeze.ico"))

    unfreeze = draw_flake(16, thawed=True)
    unfreeze.save(os.path.join(out, "Unfreeze.png"))

    question = draw_question(16)
    question.save(os.path.join(out, "RetroQuestion.png"))

    homebrew = draw_beaker(16)
    homebrew.save(os.path.join(out, "HomeBrew.png"))

    console = draw_console(32)
    save_ico(draw_console(64), os.path.join(out, "commandWindow.ico"),
             sizes=(16, 24, 32, 48, 64))

    print("wrote TAStudio.png/.ico, Freeze.png/.ico, Unfreeze.png,"
          " RetroQuestion.png, commandWindow.ico, HomeBrew.png")

    if args.preview:
        tiles = [("TAStudio", tas), ("Freeze", freeze), ("Unfreeze", unfreeze),
                 ("RetroQuestion", question), ("commandWindow", console),
                 ("HomeBrew", homebrew)]
        tw = 96
        sheet = Image.new("RGBA", (len(tiles) * tw, tw + 30), (32, 32, 32, 255))
        d = ImageDraw.Draw(sheet)
        for i, (name, im) in enumerate(tiles):
            big = im.resize((80, 80), Image.NEAREST)
            sheet.paste(big, (i * tw + 8, 8), big)
            d.text((i * tw + 8, 92), name, fill=(230, 230, 230, 255))
        sheet.convert("RGB").save(args.preview)
        print("preview ->", args.preview)


if __name__ == "__main__":
    main()
