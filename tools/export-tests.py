#!/usr/bin/env python3
"""Packs the tests of a build made for another system (Windows, cross-built on
Linux) for a machine of that system to run with tools/run-tests.py: the list
meson keeps (meson introspect --tests), and every file those tests need, in one
folder whose layout is the repository's.

    tools/export-tests.py <meson build dir> <out folder> [--runtime <folder>]...

Taken: the tests whose program is one of the build's own (an .exe); a test run
by a Linux program (python3, bash) is left out and said. Copied: the build
folder's programs, libraries and the folders beside them (the language tables),
and every file a test names in its arguments or its folder, wherever in the
repository it is (build/Cores/synth-box.chimeraCore, a test's data). In the
list, a path inside the repository is written relative to it, from @ROOT@.

The build folder's object files and meson's own files are not copied.
--runtime names a folder of the compiler's runtime DLLs (libstdc++-6.dll and
the rest for mingw-w64's GCC, libc++.dll for llvm-mingw): every DLL the
programs import that is in none of the build's folders is copied from there
beside them, as the release's bundle carries them.
tools/run-tests.py is copied in beside the list: `python run-tests.py .`."""
import json
import os
import re
import shutil
import subprocess
import sys

args = sys.argv[1:]
runtime_dirs = []
while "--runtime" in args:
    i = args.index("--runtime")
    runtime_dirs.append(args[i + 1])
    del args[i:i + 2]
build, out = (os.path.abspath(a) for a in args[0:2])
root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if os.path.exists(out):
    sys.exit(f"{out} exists: give a new folder")
tests = json.loads(subprocess.run(["meson", "introspect", "--tests", build],
                                  capture_output=True, text=True, check=True).stdout)


def inside(path):
    p = os.path.abspath(path)
    return p == root or p.startswith(root + os.sep)


def mapped(value):
    """A path inside the repository, as the list writes it (and the file to copy)."""
    if os.path.isabs(value) and inside(value):
        return "@ROOT@/" + os.path.relpath(value, root).replace(os.sep, "/"), value
    return value, None


files = set()
listed, left = [], []
for t in tests:
    program = t["cmd"][0]
    if not program.lower().endswith(".exe"):
        left.append(f"{t['name']} ({os.path.basename(program)})")
        continue
    cmd = []
    for a in t["cmd"]:
        m, f = mapped(a)
        cmd.append(m)
        if f and os.path.exists(f) and not f.startswith(build + os.sep):
            files.add(f)
    workdir = t["workdir"] or build   # meson runs a test in the build folder unless told
    env = {}
    for k, v in (t["env"] or {}).items():
        env[k] = mapped(v)[0]
    listed.append({"name": t["name"], "cmd": cmd, "workdir": mapped(workdir)[0], "env": env,
                   "timeout": t["timeout"], "suite": t.get("suite", [])})

# the build folder, without what only the build itself uses (object files,
# meson's own, and the nested builds of zstd, SDL2 and OpenAL Soft, whose
# libraries are copied beside the programs)
skip_dirs = {"meson-info", "meson-logs", "meson-private"}
for d, dirs, fs in os.walk(build):
    dirs[:] = [x for x in dirs if x not in skip_dirs and not x.endswith((".p", "-build"))]
    for f in fs:
        if f.endswith((".obj", ".o", ".a", ".ninja", ".ninja_deps", ".ninja_log", ".d")) or f == "build.ninja":
            continue
        files.add(os.path.join(d, f))

for f in sorted(files):
    dest = os.path.join(out, os.path.relpath(f, root))
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    if os.path.isdir(f):
        shutil.copytree(f, dest, dirs_exist_ok=True)
    else:
        shutil.copy2(f, dest)

# the compiler's runtime DLLs the programs import, found by reading their
# import tables (objdump -p, as tools/stage-qt-windows.py does). The host's
# objdump reads x64 programs; an arm64 one takes llvm-mingw's.
def imports(path):
    for tool in ("aarch64-w64-mingw32-objdump", "x86_64-w64-mingw32-objdump", "objdump"):
        if shutil.which(tool):
            o = subprocess.run([tool, "-p", path], capture_output=True, text=True).stdout
            found = re.findall(r"DLL Name: (\S+)", o)
            if found:
                return found
    return []


if runtime_dirs:
    build_out = os.path.join(out, os.path.relpath(build, root))
    have = {f.lower() for d, _, fs in os.walk(out) for f in fs}
    todo = [os.path.join(d, f) for d, _, fs in os.walk(out) for f in fs if f.lower().endswith((".exe", ".dll"))]
    while todo:
        for dll in imports(todo.pop()):
            if dll.lower() in have:
                continue
            for d in runtime_dirs:
                if os.path.exists(os.path.join(d, dll)):
                    shutil.copy2(os.path.join(d, dll), build_out)
                    have.add(dll.lower())
                    todo.append(os.path.join(build_out, dll))
                    print("runtime:", dll)
                    break

# and the program that runs them, so the machine that does needs nothing else
shutil.copy2(os.path.join(root, "tools", "run-tests.py"), os.path.join(out, "run-tests.py"))
with open(os.path.join(out, "tests.json"), "w") as fh:
    json.dump({"build": "@ROOT@/" + os.path.relpath(build, root).replace(os.sep, "/"), "tests": listed}, fh, indent=1)
size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
print(f"{out}: {len(listed)} tests, {len(files)} files, {size / 1e6:.1f} MB")
if left:
    print("left out (run by a Linux program):", ", ".join(left))
