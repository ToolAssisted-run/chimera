#!/usr/bin/env python3
# What the window draws, seen in its pictures:
#
# - Encode Video's Capture OSD writes the picture as the window shows it, at
#   the size of its picture area, as Chimera's did: the window's 2x or 3x is
#   the video's, not the machine's own pixels.
# - A script's text breaks at each new line ("\n"), in the window and in the
#   video: gui.drawText's two lines and gui.text's are two rows each.
# - The lists of the Lua Console, RAM Watch and RAM Search have lines
#   between their rows and columns, as Chimera's lists had.
#
# The window plays the project with window-drawing.lua and is shot when the
# run ends. The encode is skipped (said) without an ffmpeg.
#   window-drawing.py <chimera> <work folder> <project> <package> <script>
import os
import shutil
import struct
import subprocess
import sys
import zlib

chimera, work, project, package, script = sys.argv[1:6]
shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
failures = 0


def check(ok, what, got=None):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what + ("" if ok or got is None else "  (" + str(got) + ")"))
    if not ok:
        failures += 1


def png(path):
    """a PNG's pixels: width, height and its rows of (r, g, b) bytes, 8 bits a channel"""
    with open(path, "rb") as f:
        data = f.read()
    pos, idat = 8, b""
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        kind, body = data[pos + 4:pos + 8], data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if kind == b"IHDR":
            w, h, depth, colour = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
    if depth != 8 or colour not in (2, 6):
        raise ValueError("a PNG of depth %d, colour type %d" % (depth, colour))
    bpp = 3 if colour == 2 else 4
    raw, stride, rows, prev, i = zlib.decompress(idat), w * bpp, [], bytearray(w * bpp), 0
    for _ in range(h):
        kind, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if kind == 1:
                line[x] = (line[x] + a) & 255
            elif kind == 2:
                line[x] = (line[x] + b) & 255
            elif kind == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        rows.append(bytes(line) if bpp == 3 else bytes(line[i] for i in range(stride) if i % 4 != 3))
    return w, h, rows


def bands(rows, like):
    """runs of rows with a pixel like the colour (like(r, g, b)), each (first, last)"""
    found = []
    for y, row in enumerate(rows):
        if any(like(row[i], row[i + 1], row[i + 2]) for i in range(0, len(row), 3)):
            if found and found[-1][1] == y - 1:
                found[-1] = (found[-1][0], y)
            else:
                found.append((y, y))
    return found


# the script's colours, smoothed edges and all; the game's picture is greys, reds and a green
def MAGENTA(r, g, b): return r > 150 and b > 150 and g < 90
def CYAN(r, g, b): return g > 150 and b > 150 and r < 90


video, meta = os.path.join(work, "osd.raw"), os.path.join(work, "meta.txt")
shot = os.path.join(work, "window.png")
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
           CHIMERA_QT_CONFIG=os.path.join(work, "settings.json"), CHIMERA_QT_CACHE=os.path.join(work, "cache"),
           CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"), CHIMERA_QT_HASHES=os.path.join(work, "file-hashes.txt"),
           CHIMERA_DRAW_VIDEO=video, CHIMERA_DRAW_META=meta)
try:
    done = subprocess.run([chimera, project, package, "--play", "--lua", script, "--measure", "10", "--screenshot", shot],
                          env=env, capture_output=True, text=True, timeout=120)
    out = done.stdout + done.stderr
except subprocess.TimeoutExpired:
    out = "the window was still running after 120 seconds"
check("screenshot: saved" in out, "the window ran and was shot", out)

# the encode: three frames at the picture area's size
said = {}
if os.path.exists(meta):
    for line in open(meta):
        key, _, value = line.rstrip("\n").partition("=")
        said[key] = value
if "ffmpeg" in said.get("error", "") and not os.path.exists(video):
    print("SKIP  the encode: " + said["error"])
else:
    check(said.get("finished") == "1" and said.get("error") == "", "the encode ran to its end", said or out)
    w, h = (int(n) for n in said.get("area", "0 0").split())
    size = os.path.getsize(video) if os.path.exists(video) else 0
    check(w > 128 and h > 120 and size == w * h * 4 * 3,
          "Capture OSD wrote 3 frames of the picture area's %dx%d, not the machine's 128x120" % (w, h), "%d bytes" % size)
    if size == w * h * 4 * 3:
        with open(video, "rb") as f:
            f.seek(w * h * 4 * 2)
            frame = f.read(w * h * 4)
        rows = [b"".join(frame[(y * w + x) * 4:(y * w + x) * 4 + 3][::-1] for x in range(w)) for y in range(h)]
        check(len(bands(rows, MAGENTA)) == 2, "the video: gui.drawText's two lines are two rows", bands(rows, MAGENTA))
        check(len(bands(rows, CYAN)) == 2, "the video: gui.text's two lines are two rows", bands(rows, CYAN))

# the window's own picture
if os.path.exists(shot):
    _, _, rows = png(shot)
    check(len(bands(rows, MAGENTA)) == 2, "the window: gui.drawText's two lines are two rows", bands(rows, MAGENTA))
    check(len(bands(rows, CYAN)) == 2, "the window: gui.text's two lines are two rows", bands(rows, CYAN))

def grid(rows, w, h):
    """a list's grid: a line one pixel high and 150 or more wide, unlike the
    rows above and below it, with two or more lines of its colour standing on
    it inside its ends, a row high (20 pixels); (y, left, right, columns) or None"""
    at = lambda x, y: rows[y][x * 3:x * 3 + 3]
    for y in range(21, h - 1):
        x = 0
        while x < w:
            colour, end = at(x, y), x
            while end + 1 < w and at(end + 1, y) == colour:
                end += 1
            if end - x >= 150:
                span = range(x, end + 1)
                if (sum(at(i, y - 1) != colour for i in span) > len(span) * 0.9
                        and sum(at(i, y + 1) != colour for i in span) > len(span) * 0.9):
                    columns = [i for i in range(x + 4, end - 3) if all(at(i, up) == colour for up in range(y - 20, y))]
                    if len(columns) >= 2:
                        return y, x, end, columns
            x = end + 1
    return None


for name, what in (("luaConsoleWindow", "the Lua Console's scripts"), ("ramWatchWindow", "RAM Watch's watches"),
                   ("ramSearchWindow", "RAM Search's addresses")):
    picture = os.path.join(work, "window-%s.png" % name)
    if not os.path.exists(picture):
        check(False, what + ": the window was open and shot", out)
        continue
    w, h, rows = png(picture)
    found = grid(rows, w, h)
    check(found is not None, what + ": a line under the rows and lines between the columns", "none found")

sys.exit(1 if failures else 0)
