#!/usr/bin/env python3
"""Puts Chimera's Linux build in one folder that runs on its own, as
tools/stage-qt-windows.py does for Windows: the window, Chimera's libraries,
the Qt libraries, plugins and QML modules the window uses, and the few
libraries those need that a desktop may not have; Chimera's language tables
(lib/languages); the other programs in bin/; the themes Chimera ships; and an empty cores/ with a README.txt that says
what goes in it.

The folder is laid out as Qt installs itself, so a relocatable Qt finds its
plugins and QML from where its own libraries are, with no qt.conf:

    chimera   lib/   plugins/   qml/   bin/   themes/   cores/

Each program and library is told where its neighbours are ($ORIGIN, with
patchelf) and stripped of debug information, as it would ship.

    tools/stage-qt-linux.py <installed dll dir> <qt prefix> <out>

The installed dll dir is meson's install folder for the Linux build
(build/dll): the window, chimera-run, chimera-mcp, Chimera's libraries and,
when tools/fetch-ffmpeg.sh put it there, ffmpeg. Prints the folder's size and
Qt's share of it."""
import os
import platform
import shutil
import subprocess
import sys

import bundle_qml

dlls, qt, out = sys.argv[1:4]
here = os.path.dirname(os.path.abspath(__file__))

if os.path.exists(out):
    sys.exit(f"{out} exists: give a new folder")
lib, plugins, bin_ = f"{out}/lib", f"{out}/plugins", f"{out}/bin"
for d in (lib, plugins, bin_):
    os.makedirs(d)

shutil.copy2(f"{dlls}/chimera", out)
# a movie import and a compile session ask the core in a process of their own
shutil.copy2(f"{dlls}/chimera-run", bin_)
# the program an AI agent's program starts to reach the window's MCP server
shutil.copy2(f"{dlls}/chimera-mcp", bin_)
# Encode Video's, as its release ships it
for f in ("ffmpeg", "ffmpeg-LICENSE.txt"):
    if os.path.exists(f"{dlls}/{f}"):
        shutil.copy2(f"{dlls}/{f}", bin_)
# Chimera's libraries; SDL2 and OpenAL Soft are opened by name when they are there
for f in sorted(os.listdir(dlls)):
    if ".so" in f:
        shutil.copy2(f"{dlls}/{f}", lib)
# Chimera's language tables, one file per language, beside the engine that reads them
shutil.copytree(f"{dlls}/languages", f"{lib}/languages")

# Qt's plugins the window uses: drawing on X11 and off screen (--headless),
# the GL integrations X11 draws with, a key composed of two (dead keys), and
# the image files read with a plugin (ICO, JPEG, GIF; PNG and BMP need none)
wanted = {
    "platforms": ["libqxcb.so", "libqoffscreen.so"],
    "xcbglintegrations": None,
    "platforminputcontexts": ["libcomposeplatforminputcontextplugin.so"],
    "imageformats": ["libqico.so", "libqjpeg.so", "libqgif.so"],
}
for group, names in wanted.items():
    src = f"{qt}/plugins/{group}"
    if not os.path.isdir(src):
        continue
    os.makedirs(f"{plugins}/{group}", exist_ok=True)
    for n in sorted(os.listdir(src)) if names is None else names:
        if os.path.exists(f"{src}/{n}") and n.endswith(".so"):
            shutil.copy2(f"{src}/{n}", f"{plugins}/{group}")

# QML: QtQml, QtCore and the QtQuick modules the window imports, and the
# folder list Qt's own file dialogs are drawn with. Of the control styles
# only Fusion (the one used) and Basic (every style's fallback).
quick = {"Controls", "Templates", "Layouts", "Dialogs", "Window"}
styles = {"Fusion", "Basic", "impl"}
def ignore(dirpath, names):
    rel = os.path.relpath(dirpath, f"{qt}/qml")
    if rel == "QtQuick":
        return [n for n in names if os.path.isdir(os.path.join(dirpath, n)) and n not in quick]
    if rel == "QtQuick/Controls":
        return [n for n in names if os.path.isdir(os.path.join(dirpath, n)) and n not in styles]
    return [n for n in names if n.endswith(".debug")]
for m in ("QtQml", "QtCore", "QtQuick", "Qt/labs/folderlistmodel"):
    shutil.copytree(f"{qt}/qml/{m}", f"{out}/qml/{m}", ignore=ignore)
# every module the window and Qt's dialogs import is here (bundle_qml.py)
gone = bundle_qml.missing(f"{out}/qml", os.path.join(here, "..", "source", "ui", "qt"))
if gone:
    sys.exit("the bundle lacks QML modules the window imports: " + ", ".join(gone))

# the libraries everything here needs, from Qt's, read from their dynamic
# sections; and of the system's, the few a desktop may lack (X11's cursor,
# image and key helpers since Qt 6.5 asks for them), so the window starts
desktop_may_lack = {"libxcb-cursor.so.0", "libxcb-icccm.so.4", "libxcb-image.so.0", "libxcb-keysyms.so.1",
                    "libxcb-render-util.so.0", "libxcb-util.so.1", "libxkbcommon-x11.so.0"}
def needed(path):
    o = subprocess.run(["readelf", "-d", path], capture_output=True, text=True).stdout
    return [line.split("[")[1].split("]")[0] for line in o.splitlines() if "(NEEDED)" in line]
# ldconfig names each library's CPU: only the one this bundle is built for will do
machine = platform.machine()
if machine == "x86_64":
    ldconfig_cpu = "x86-64"
elif machine == "aarch64":
    ldconfig_cpu = "AArch64"
else:
    sys.exit(f"Chimera's Linux bundle is built on x86-64 and aarch64 only, not {machine}")
def system_path(name):
    o = subprocess.run(["ldconfig", "-p"], capture_output=True, text=True).stdout
    for line in o.splitlines():
        if line.strip().startswith(name + " ") and f",{ldconfig_cpu})" in line:
            return line.split("=>")[1].strip()
    return None
def elves():
    for r, _, fs in os.walk(out):
        for f in fs:
            p = os.path.join(r, f)
            with open(p, "rb") as h:
                if h.read(4) == b"\x7fELF":
                    yield p
have = set(os.listdir(lib))
todo = list(elves())
while todo:
    for n in needed(todo.pop()):
        if n in have:
            continue
        src = f"{qt}/lib/{n}" if os.path.exists(f"{qt}/lib/{n}") else (system_path(n) if n in desktop_may_lack else None)
        if src is None:
            continue
        shutil.copy2(os.path.realpath(src), f"{lib}/{n}")
        have.add(n)
        todo.append(f"{lib}/{n}")

# where each finds its neighbours, and stripped
def run(*args):
    subprocess.run(list(args), check=True)
for p in elves():
    # ffmpeg is one static program, as its release ships it
    if os.path.basename(p) == "ffmpeg":
        continue
    rel = os.path.relpath(os.path.dirname(p), out)
    to_lib = os.path.relpath(lib, os.path.dirname(p))
    if rel == "." or rel == "bin" or rel == "lib" and not os.path.basename(p).startswith("libQt6"):
        run("patchelf", "--set-rpath", "$ORIGIN" if to_lib == "." else f"$ORIGIN/{to_lib}", p)
    run("strip", "--strip-unneeded", p)

# the themes Chimera ships, in the place Chimera keeps them: beside the program
shutil.copytree(os.path.join(here, "..", "Themes"), f"{out}/themes")
# the folder the window reads core packages from, empty but for what it is for
os.makedirs(f"{out}/cores")
shutil.copy2(os.path.join(here, "cores-README.txt"), f"{out}/cores/README.txt")

def size(pred):
    return sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs if pred(os.path.join(r, f)))
total = size(lambda p: True)
qtpart = size(lambda p: os.path.basename(p).startswith("libQt6") or any(f"{os.sep}{d}{os.sep}" in p for d in ("qml", "plugins")))
print(f"{out}: {total / 1e6:.1f} MB, of which Qt {qtpart / 1e6:.1f} MB")
print("libraries:", " ".join(sorted(f for f in os.listdir(lib))))
