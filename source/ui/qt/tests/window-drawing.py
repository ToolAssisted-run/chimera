#!/usr/bin/env python3
# What the window draws, seen in its pictures:
#
# - Encode Video's Capture OSD writes the picture as the window shows it, at
#   the size of its picture area, as Chimera's did: the window's 2x or 3x is
#   the video's, not the machine's own pixels.
# - A script's text breaks at each new line ("\n"), in the window and in the
#   video: gui.drawText's two lines and gui.text's are two rows each.
# - The Lua Console's list of scripts has lines between its rows and
#   columns, as Chimera's list had.
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

# the Lua Console: the script's row, the first thing in its list unlike the
# list's empty background, has a line under it and lines between its columns
console = os.path.join(work, "window-luaConsoleWindow.png")
if os.path.exists(console):
    w, h, rows = png(console)
    at = lambda x, y: rows[y][x * 3:x * 3 + 3]
    empty = at(10, h - 80)
    band, y = None, 60
    while y < h - 80 and band is None:
        if at(10, y) != empty:
            end = y
            while at(10, end + 1) == at(10, y):
                end += 1
            if end - y >= 14:
                band = (y, end)
            y = end
        y += 1
    check(band is not None, "the Lua Console shows the script's row")
    if band is not None:
        top, bottom = band
        colour = at(10, top)
        xs = [x for x in range(w) if at(x, top) == colour]
        left, right = min(xs), max(xs)
        line = at(10, bottom + 1)
        lined = sum(at(x, bottom + 1) == line for x in range(left, right + 1))
        check(line not in (colour, empty) and lined > (right - left) * 0.9, "a line under the row",
              "%s under a %s row on %s, %d of %d pixels" % (line.hex(), colour.hex(), empty.hex(), lined, right - left + 1))
        columns = [x for x in range(left, right + 1) if all(at(x, y) == line for y in range(top, bottom + 1))]
        check(len(columns) >= 2, "lines between the columns, in the line's colour", columns)
else:
    check(False, "the Lua Console was open and shot", out)

sys.exit(1 if failures else 0)
