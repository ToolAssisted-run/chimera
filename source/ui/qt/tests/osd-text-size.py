#!/usr/bin/env python3
# The on-screen text's size while it does not grow with the window (owner,
# 2026-11-04, issue 488): Config > Display's OSD text size. The window opens
# the project paused, so its picture is the same every time, and is shot with
# the frame counter and without it: the pixels that differ are the counter's.
# At 36 pixels they stand about three times as tall as at 12.
#   osd-text-size.py <chimera> <work folder> <project> <package> <script>
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


def shot(size, counter):
    """the window shot with the counter shown or not, at a size; None when it did not run"""
    name = "osd-%d-%d" % (size, counter)
    folder = os.path.join(work, name)
    os.makedirs(folder)
    path = os.path.join(folder, "window.png")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
               CHIMERA_QT_CONFIG=os.path.join(folder, "settings.json"), CHIMERA_QT_CACHE=os.path.join(folder, "cache"),
               CHIMERA_QT_RECOVERY=os.path.join(folder, "recovery"), CHIMERA_QT_HASHES=os.path.join(folder, "file-hashes.txt"),
               OSD_SIZE=str(size), OSD_COUNTER=str(counter))
    try:
        done = subprocess.run([chimera, project, package, "--lua", script, "--measure", "2", "--screenshot", path],
                              env=env, capture_output=True, text=True, timeout=120)
        out = done.stdout + done.stderr
    except subprocess.TimeoutExpired:
        out = "the window was still running after 120 seconds"
    if "screenshot: saved" not in out or not os.path.exists(path):
        check(False, "the window ran and was shot (size %d)" % size, out[-400:])
        return None
    return png(path)


def counter_height(size):
    """how many rows the counter's pixels span at a size: the rows of the
    window's top half where the shots with and without it differ (the
    messages at the bottom come and go as they please)"""
    a, b = shot(size, 1), shot(size, 0)
    if a is None or b is None or a[:2] != b[:2]:
        return 0
    rows = [y for y in range(a[1] // 2) if a[2][y] != b[2][y]]
    return rows[-1] - rows[0] + 1 if rows else 0


small, large = counter_height(12), counter_height(36)
check(small > 0, "at 12 pixels the frame counter is drawn", small)
check(small > 0 and large >= small * 2.5, "at 36 pixels it stands about three times as tall as at 12 (%d rows against %d)"
      % (large, small), "%d rows against %d" % (large, small))
sys.exit(1 if failures else 0)
