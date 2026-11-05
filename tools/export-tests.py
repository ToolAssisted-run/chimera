#!/usr/bin/env python3
"""Packs the tests of a build made for another system (Windows, cross-built on
Linux) for a machine of that system to run with tools/run-tests.py: the list
meson keeps (meson introspect --tests), and every file those tests need, in one
folder whose layout is the repository's.

    tools/export-tests.py <meson build dir> <out folder> [--runtime <folder>]... [--bundle <folder>]
        [--font <file>]...

Taken: the tests whose program is one of the build's own (an .exe), and those
a Python script runs, which the machine's own Python runs there (with every
module of the repository the script imports); a test run by another Linux
program (bash) is left out and said. Copied: the build
folder's programs, libraries and the folders beside them (the language tables),
and every file a test names in its arguments or its folder, wherever in the
repository it is (build/Cores/synth-box.chimeraCore, a test's data). In the
list, a path inside the repository is written relative to it, from @ROOT@.

The build folder's object files and meson's own files are not copied.
--runtime names a folder of the compiler's runtime DLLs (libstdc++-6.dll and
the rest for mingw-w64's GCC, libc++.dll for llvm-mingw): every DLL the
programs import that is in none of the build's folders is copied from there
beside them, as the release's bundle carries them.
--bundle names the bundle tools/build-bundle.sh staged from this build (the
window, with its Qt): it is copied too, a test given the build's chimera.exe
is given the bundle's instead (the build's cannot start: its Qt is not beside
it), and every test finds Qt's libraries, plugins and QML modules in the
bundle's lib folder, as the window does. Qt's warnings go to the test's output
(on Windows they go to a debugger otherwise).
--font names a font file the tests draw their text with, copied into the
pack's fonts folder. On Windows, Qt's offscreen platform, which the window's
tests use, reads fonts from one folder only, and Qt ships none; pointed at
Windows' own, or at all of DejaVu's, it drew in whichever it fell back to
(Qt's default, Helvetica, is in neither): a narrow one, a serif. Given only the
fonts the Linux tests draw with (DejaVu Sans and DejaVu Sans Mono), the
window's tests measure the same text on both.
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
bundle = None
if "--bundle" in args:
    i = args.index("--bundle")
    bundle = os.path.abspath(args[i + 1])
    del args[i:i + 2]
fonts = []
while "--font" in args:
    i = args.index("--font")
    fonts.append(args[i + 1])
    del args[i:i + 2]
build, out = (os.path.abspath(a) for a in args[0:2])
root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
if os.path.exists(out):
    sys.exit(f"{out} exists: give a new folder")
if bundle and not bundle.startswith(root + os.sep):
    sys.exit(f"{bundle}: give a bundle inside the repository (build/...)")
# the bundle's programs, by name, which stand in for the build's
bundle_programs = {f.lower(): os.path.join(bundle, f) for f in os.listdir(bundle)
                   if f.lower().endswith(".exe")} if bundle else {}
tests = json.loads(subprocess.run(["meson", "introspect", "--tests", build],
                                  capture_output=True, text=True, check=True).stdout)
# what the build makes only for its tests (the extra synthetic packages the
# view models' test is given, the window's test project): meson test makes
# each test's own before it runs, and the tests of a cross build are not run
# here, so they are made now
depends = {d for t in tests for d in t.get("depends", [])}
targets = json.loads(subprocess.run(["meson", "introspect", "--targets", build],
                                    capture_output=True, text=True, check=True).stdout)
outputs = [os.path.relpath(f, build) for t in targets if t["id"] in depends for f in t["filename"]]
subprocess.run(["ninja", "-C", build, "meson-test-prereq"] + outputs, check=True, stdout=subprocess.DEVNULL)


def inside(path):
    p = os.path.abspath(path)
    return p == root or p.startswith(root + os.sep)


def mapped(value):
    """A path inside the repository, as the list writes it (and the file to copy)."""
    if os.path.isabs(value) and inside(value):
        return "@ROOT@/" + os.path.relpath(value, root).replace(os.sep, "/"), value
    return value, None


def imported(script, seen):
    """The repository's modules a Python script imports (by a plain import of
    a name that is a .py file in the repository's source or tests), and theirs."""
    with open(script, encoding="utf-8") as fh:
        names = set(re.findall(r"^\s*(?:import|from)\s+(\w+)", fh.read(), re.M))
    for top in ("source", "tests"):
        for d, dirs, fs in os.walk(os.path.join(root, top)):
            for name in names:
                f = os.path.join(d, name + ".py")
                if name + ".py" in fs and f not in seen:
                    seen.add(f)
                    imported(f, seen)
    return seen


files = set()
listed, left = [], []
for t in tests:
    program = t["cmd"][0]
    # a script the machine's own Python runs: "@PYTHON@" in the list
    python = (os.path.basename(program).lower().startswith("python") and len(t["cmd"]) > 1
              and t["cmd"][1].endswith(".py"))
    if not program.lower().endswith(".exe") and not python:
        left.append(f"{t['name']} ({os.path.basename(program)})")
        continue
    if python:
        files.update(imported(t["cmd"][1], set()))
    cmd = ["@PYTHON@"] if python else []
    for a in t["cmd"][1:] if python else t["cmd"]:
        # meson names a program of the build relative to the build folder
        in_build = os.path.abspath(os.path.join(build, a)).startswith(build + os.sep)
        if bundle and in_build and os.path.basename(a).lower() in bundle_programs:
            a = bundle_programs[os.path.basename(a).lower()]
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
if bundle:
    files.add(bundle)

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
plan = {"build": "@ROOT@/" + os.path.relpath(build, root).replace(os.sep, "/"), "tests": listed}
if bundle:
    # Qt where the window has it: its libraries on PATH after the build's,
    # its plugins and QML modules where Qt looks
    lib = mapped(os.path.join(bundle, "lib"))[0]
    plan["path"] = [lib]
    plan["env"] = {"QT_PLUGIN_PATH": lib, "QML_IMPORT_PATH": lib + "/qml", "QT_FORCE_STDERR_LOGGING": "1"}
if fonts:
    os.makedirs(os.path.join(out, "fonts"))
    for f in fonts:
        shutil.copy2(f, os.path.join(out, "fonts"))
    plan.setdefault("env", {})["QT_QPA_FONTDIR"] = "@ROOT@/fonts"
with open(os.path.join(out, "tests.json"), "w") as fh:
    json.dump(plan, fh, indent=1)
size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(out) for f in fs)
print(f"{out}: {len(listed)} tests, {len(files)} files, {size / 1e6:.1f} MB")
if left:
    print("left out (run by a Linux program):", ", ".join(left))
