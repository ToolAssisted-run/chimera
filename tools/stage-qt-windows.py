#!/usr/bin/env python3
"""Puts the Qt window's Windows build in one folder that runs on its own: the
program, Chimera's DLLs, the QML modules the window imports, every DLL those
import (from Qt or the compiler), found by reading their import tables, and
the themes Chimera ships.
Everything is stripped of debug information, as it would ship.

The folder holds the program (chimera.exe, a launcher), the themes and an
empty cores/ with a README.txt that says what goes in it;
every DLL, Qt's plugins and QML are in lib/ (Chimera's language tables in
lib/languages), and the other programs
(chimera-run, chimera-mcp, ffmpeg) in bin/. The tests are not in it (issue
492): nothing in a release runs them.

    tools/stage-qt-windows.py [--cpu x64|arm64] <meson build dir> <installed dll dir> <windows qt> <out> [ffmpeg.exe]

The CPU (x64 when none is given) says whose tools read and strip the files,
and where the compiler's runtime DLLs are: mingw-w64's GCC for x64,
llvm-mingw's clang for arm64 (its bin/ on PATH).

Prints the folder's size and Qt's share of it."""
import os
import shutil
import subprocess
import sys

import bundle_qml

args = sys.argv[1:]
cpu = "x64"
if args[:1] == ["--cpu"]:
    cpu, args = args[1], args[2:]
build, dlls, qt, out = args[0:4]
ffmpeg = args[4] if len(args) > 4 else ""
if cpu == "x64":
    tool = "x86_64-w64-mingw32-"
    runtime = ["/usr/lib/gcc/x86_64-w64-mingw32/13-posix", "/usr/x86_64-w64-mingw32/lib"]
elif cpu == "arm64":
    tool = "aarch64-w64-mingw32-"
    cc = shutil.which("aarch64-w64-mingw32-clang")
    if cc is None:
        sys.exit("no aarch64-w64-mingw32-clang on PATH (llvm-mingw's bin/)")
    runtime = [os.path.join(os.path.dirname(os.path.dirname(cc)), "aarch64-w64-mingw32", "bin")]
else:
    sys.exit(f"the CPU is x64 or arm64, not {cpu}")

if os.path.exists(out):
    sys.exit(f"{out} exists: give a new folder")
lib, bin = f"{out}/lib", f"{out}/bin"
os.makedirs(lib)
os.makedirs(bin)
shutil.copy2(f"{build}/source/ui/chimera.exe", out)
shutil.copy2(f"{build}/source/ui/chimera-qt-app.dll", lib)
# a movie import asks the core in a process of its own: chimera-run
shutil.copy2(f"{build}/chimera-run.exe", bin)
# the program an AI agent's program starts to reach the window's MCP server
# (Tools > MCP Server...); it needs no DLL of the folder's
shutil.copy2(f"{build}/chimera-mcp.exe", bin)
# Encode Video's
if ffmpeg:
    shutil.copy2(ffmpeg, bin)
for d in ("libchimera.dll", "libminiboxhost.dll", "libzstd.dll", "lua54.dll", "SDL3.dll", "OpenAL32.dll",
          "chimera_crash.dll", "e_sqlite3.dll"):
    shutil.copy2(f"{dlls}/{d}", lib)
# Chimera's language tables, one file per language, beside the engine that reads them
shutil.copytree(f"{build}/languages", f"{lib}/languages")
os.makedirs(f"{lib}/platforms")
shutil.copy2(f"{qt}/plugins/platforms/qwindows.dll", f"{lib}/platforms")
# --headless draws off screen, for a run nobody watches (a core's tests)
shutil.copy2(f"{qt}/plugins/platforms/qoffscreen.dll", f"{lib}/platforms")
# the image files Qt reads with a plugin: ICO (a menu's icon), and JPEG and GIF as
# well as ICO for a script's gui.drawImage and TAStudio's cell icons, which read
# what Chimera's read (PNG and BMP need none)
os.makedirs(f"{lib}/imageformats")
for p in ("qico.dll", "qjpeg.dll", "qgif.dll"):
    shutil.copy2(f"{qt}/plugins/imageformats/{p}", f"{lib}/imageformats")
# the themes Chimera ships, in the place Chimera keeps them: beside the program
here = os.path.dirname(os.path.abspath(__file__))
shutil.copytree(os.path.join(here, "..", "themes"), f"{out}/themes")
# the folder the window reads core packages from, empty but for what it is for:
# Chimera ships no core and downloads none (docs/core-manager.md); the words are
# the Linux folder's too (cores-README.txt), with Windows' line ends
os.makedirs(f"{out}/cores")
with open(os.path.join(here, "cores-README.txt")) as src, open(f"{out}/cores/README.txt", "w", newline="\r\n") as dst:
    dst.write(src.read())

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
    shutil.copytree(f"{qt}/qml/{m}", f"{lib}/qml/{m}", ignore=ignore)
# every module the window and Qt's dialogs import is here (bundle_qml.py)
gone = bundle_qml.missing(f"{lib}/qml", os.path.join(here, "..", "source", "ui", "qt"))
if gone:
    sys.exit("the bundle lacks QML modules the window imports: " + ", ".join(gone))

def imports(path):
    o = subprocess.run([tool + "objdump", "-p", path], capture_output=True, text=True, check=True).stdout
    return [line.split()[-1] for line in o.splitlines() if "DLL Name:" in line]

have = {f.lower() for f in os.listdir(lib)}
todo = [os.path.join(r, f) for r, _, fs in os.walk(out) for f in fs if f.lower().endswith((".dll", ".exe"))]
while todo:
    for dll in imports(todo.pop()):
        if dll.lower() in have:
            continue
        for d in [f"{qt}/bin"] + runtime:
            if os.path.exists(f"{d}/{dll}"):
                shutil.copy2(f"{d}/{dll}", lib)
                have.add(dll.lower())
                todo.append(os.path.join(lib, dll))
                break

for r, _, fs in os.walk(out):
    for f in fs:
        # ffmpeg is copied as its own release ships it
        if f.lower().endswith((".dll", ".exe")) and f.lower() != "ffmpeg.exe":
            subprocess.run([tool + "strip", "--strip-unneeded", os.path.join(r, f)], check=True)

def size(pred):
    return sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(out) for f in fs if pred(os.path.join(r, f)))
total = size(lambda p: True)
qtpart = size(lambda p: os.path.basename(p).startswith("Qt6") or any(f"{os.sep}{d}{os.sep}" in p for d in ("qml", "platforms", "imageformats")))
print(f"{out}: {total / 1e6:.1f} MB, of which Qt {qtpart / 1e6:.1f} MB")
print("DLLs:", " ".join(sorted(f for f in os.listdir(lib) if f.lower().endswith(".dll"))))
