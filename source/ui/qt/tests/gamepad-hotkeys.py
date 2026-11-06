#!/usr/bin/env python3
# A gamepad input bound to a hotkey runs it as a key does (issue 466). With
# Pause on a pad's right trigger (gamepad-hotkeys.lua), a project opened
# paused runs once the pad presses the trigger, and not before. The pad is
# a stand-in for SDL2 (stand_in_sdl.c), put in a folder of its own as
# libSDL2.so, whose trigger is held from 2.0 to 2.3 seconds after it starts.
#   gamepad-hotkeys.py <chimera> <work folder> <project> <package> <stand-in SDL> <script>
import os
import re
import shutil
import subprocess
import sys

chimera, work, project, package, sdl, script = sys.argv[1:7]
shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, "lib"))
# the library's name and the variable the system's loader reads, on a Mac as on Linux
mac = sys.platform == "darwin"
shutil.copy(sdl, os.path.join(work, "lib", "libSDL2.dylib" if mac else "libSDL2.so"))
path_var = "DYLD_LIBRARY_PATH" if mac else "LD_LIBRARY_PATH"
seconds = 6
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
           CHIMERA_QT_CONFIG=os.path.join(work, "settings.json"), CHIMERA_QT_CACHE=os.path.join(work, "cache"),
           CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"), CHIMERA_QT_HASHES=os.path.join(work, "file-hashes.txt"),
           STAND_IN_PAD_FROM="2000", STAND_IN_PAD_TO="2300")
env[path_var] = os.path.join(work, "lib") + os.pathsep + os.environ.get(path_var, "")
try:
    run = subprocess.run([chimera, project, package, "--lua", script, "--measure", str(seconds)], env=env,
                         capture_output=True, text=True, timeout=90)
except subprocess.TimeoutExpired:
    sys.exit("FAIL  the window was still running after 90 seconds")
out = run.stdout + run.stderr
frames = re.search(r"machine: (\d+) frames", out)
failures = 0


def check(ok, what):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what)
    failures += 0 if ok else 1


check(run.returncode == 0, "the window quit cleanly (%d)" % run.returncode)
n = int(frames.group(1)) if frames else -1
# about 60 a second once running: from the press (2 s after the pads start) to the end, never the whole run
check(20 < n < (seconds - 1) * 60, "the pad's trigger ran Pause: %d frames played, after the press and not before" % n)
if failures:
    print(out[-3000:])
sys.exit(1 if failures else 0)
