#!/usr/bin/env python3
# Chimera's command line (review, 2026-10-26): --version; a project named
# with --project and its package with --core, opened and run; what is not a
# project and its package refused; --config, and the settings file beside
# the program without it; --chromeless and --luaconsole; and a --show name
# no window has, said.
#   command-line.py <chimera> <work folder> <project> <package>
import os
import shutil
import subprocess
import sys

qt, work, project, package = sys.argv[1:5]
shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
           CHIMERA_QT_CONFIG=os.path.join(work, "settings.json"), CHIMERA_QT_CACHE=os.path.join(work, "cache"),
           CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"), CHIMERA_QT_HASHES=os.path.join(work, "file-hashes.txt"))
env.pop("DISPLAY", None)
failures = 0


def run(*args, seconds=60):
    try:
        return subprocess.run([qt] + list(args), env=env, timeout=seconds, capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return None


def check(ok, what, got=None):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what + ("" if ok or got is None else "  (" + got + ")"))
    if not ok:
        failures += 1


r = run("--version")
check(r is not None and r.returncode == 0 and r.stdout.startswith("Commit ") and len(r.stdout.strip()) == len("Commit ") + 9,
      "--version prints the commit, as Chimera's", r and r.stdout.strip())
# and needs no display: a release is checked with none
bare = {k: v for k, v in env.items() if k not in ("QT_QPA_PLATFORM", "QT_QUICK_BACKEND", "DISPLAY", "WAYLAND_DISPLAY")}
try:
    r = subprocess.run([qt, "--version"], env=bare, timeout=60, capture_output=True, text=True)
except subprocess.TimeoutExpired:
    r = None
check(r is not None and r.returncode == 0 and r.stdout.startswith("Commit "), "--version needs no display", r and (r.stdout + r.stderr)[-200:])

r = run("--project", project, "--core", package, "--play", "--measure", "1")
check(r is not None and r.returncode == 0 and "status: Opened gridWalker" in r.stdout,
      "--project and --core open the project and run it", r and (r.stdout + r.stderr)[-300:])

for refused, what in [([project, project, package], "three files"), ([project, os.path.join(work, "notes.txt")], "a package that is not one"),
                      ([os.path.join(work, "game.nes"), package], "a game and a package")]:
    r = run(*refused, "--measure", "1")
    check(r is not None and r.returncode == 2 and "a game opens only in a project" in r.stderr, "refused: " + what, r and r.stderr.strip())

# the file named is the one read: one that does not read is set aside, as the window's own always is
own = os.path.join(work, "own.json")
with open(own, "w") as f:
    f.write("{ not json")
r = run("--config", own, "--measure", "0.5")
check(r is not None and r.returncode == 0 and os.path.exists(own + ".unreadable"), "--config names the settings file the window reads",
      r and r.stderr.strip())

# with no file named, the settings are beside the program, so each Chimera
# folder keeps its own (owner, 2026-10-29, issue 403); a folder that cannot
# be written leaves them in the system's folder for the person's settings
program = os.path.join(work, "program")
os.makedirs(program)
copy = os.path.join(program, os.path.basename(qt))
shutil.copy2(qt, copy)
setting = os.path.join(work, "set-a-hotkey.lua")
with open(setting, "w") as f:
    f.write("chimera.hotkeys.saved = '{\"Pause\":\"F7\"}'\n")
own = dict(env, XDG_CONFIG_HOME=os.path.join(work, "xdg"),
           LD_LIBRARY_PATH=os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(qt)))))
own.pop("CHIMERA_QT_CONFIG")


def settings_in(folder):
    for root, _, files in os.walk(folder):
        if "settings.json" in files:
            with open(os.path.join(root, "settings.json")) as f:
                return f.read()
    return ""


def run_copy():
    try:
        return subprocess.run([copy, "--lua", setting, "--measure", "0.5"], env=own, timeout=60, capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return None


if sys.platform == "darwin":
    # on a Mac they are in the data folder, ~/Library/Application Support/Chimera
    # (CHIMERA_DATA_HOME says another), and never beside the program: it is in a
    # signed app (owner, 2026-11-03)
    data = os.path.join(work, "data")
    own["CHIMERA_DATA_HOME"] = data
    r = run_copy()
    check(r is not None and r.returncode == 0 and "F7" in settings_in(data) and settings_in(program) == "",
          "the settings file is in the data folder, not beside the program", r and r.stderr.strip()[-300:])
else:
    r = run_copy()
    check(r is not None and r.returncode == 0 and "F7" in settings_in(program) and settings_in(own["XDG_CONFIG_HOME"]) == "",
          "the settings file is beside the program", r and r.stderr.strip()[-300:])
    os.remove(os.path.join(program, "settings.json"))
    os.chmod(program, 0o555)
    r = run_copy()
    os.chmod(program, 0o755)
    check(r is not None and r.returncode == 0 and settings_in(program) == "" and "F7" in settings_in(own["XDG_CONFIG_HOME"]),
          "a program folder that cannot be written keeps them in the person's own", r and r.stderr.strip()[-300:])

shot = os.path.join(work, "chromeless.png")
r = run("--chromeless", "--luaconsole", "--measure", "0.5", "--screenshot", shot)
check(r is not None and r.returncode == 0 and "screenshot: luaConsoleWindow saved" in r.stdout,
      "--luaconsole opens the Lua Console", r and r.stdout.strip()[-300:])

r = run("--show", "no-such-window", "--measure", "0.5")
check(r is not None and r.returncode == 0 and "--show no-such-window: no tool window has that name" in r.stderr,
      "a --show name no window has is said", r and r.stderr.strip())

print("%s: %d failed" % ("FAIL" if failures else "PASS", failures))
sys.exit(1 if failures else 0)
