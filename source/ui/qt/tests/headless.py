#!/usr/bin/env python3
# Chimera's --headless, taken by the window that replaces it, for runs nobody
# watches such as the cores' tests: it runs with no display at all; a
# script's client.exit leaves without asking, its unsaved changes let go and
# said; a core that stops, or a movie that cannot be played, ends the run
# with 65 and says why; a project whose
# files are not found ends it with 64 and names them, and one that cannot
# open says why; a file picker ends it
# with 64; work a killed run left is said, and the project opens as saved.
#   headless.py <chimera> <work folder> <package> <game>
import hashlib
import json
import os
import shutil
import signal
import subprocess
import sys
import time

chimera, work, package, game = sys.argv[1:5]
here = os.path.dirname(os.path.abspath(__file__))
shutil.rmtree(work, ignore_errors=True)
os.makedirs(work)
env = dict(os.environ, ALSOFT_DRIVERS="null", CHIMERA_QT_CONFIG=os.path.join(work, "settings.json"),
           CHIMERA_QT_CACHE=os.path.join(work, "cache"), CHIMERA_QT_RECOVERY=os.path.join(work, "recovery"),
           CHIMERA_QT_HASHES=os.path.join(work, "file-hashes.txt"))
# no display, and Qt not told where to draw: --headless must do without
for name in ("DISPLAY", "WAYLAND_DISPLAY", "QT_QPA_PLATFORM", "QT_QUICK_BACKEND"):
    env.pop(name, None)
failures = 0


def check(ok, what, got=None):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what + ("" if ok or got is None else "  (" + got + ")"))
    if not ok:
        failures += 1


def project(name, rom_tail=b""):
    """a project of its own, its game copied in beside it"""
    folder = os.path.join(work, name)
    os.makedirs(folder)
    rom = os.path.join(folder, os.path.basename(game))
    with open(game, "rb") as src, open(rom, "wb") as dst:
        dst.write(src.read() + rom_tail)
    out = os.path.join(folder, name + ".chimeraProject")
    subprocess.run([sys.executable, os.path.join(here, "game-project.py"), package, rom, out], check=True)
    return out, rom


def script(name, text):
    path = os.path.join(work, name + ".lua")
    with open(path, "w") as f:
        f.write(text)
    return path


def run(proj, lua, seconds=120, extra_env=None):
    try:
        return subprocess.run([chimera, "--headless", "--project", proj, "--core", package, "--lua", lua],
                              env=dict(env, **(extra_env or {})), timeout=seconds, capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return None


def said(r):
    return "" if r is None else (r.stdout + r.stderr)[-400:]


def digest(path):
    with open(path, "rb") as f:
        return hashlib.sha1(f.read()).hexdigest()


# a run with no display: the script runs on the machine and ends the run
proj, _ = project("plain")
out = os.path.join(work, "plain.txt")
r = run(proj, script("plain", f"""
for i = 1, 30 do emu.frameadvance() end
local f = io.open([[{out}]], "w")
f:write(emu.getsystemid(), " ", emu.framecount(), " ", tostring(tastudio.engaged()))
f:close()
client.exit()
"""))
check(r is not None and r.returncode == 0 and os.path.exists(out), "with no display, a script runs the project and ends the run", said(r))
if os.path.exists(out):
    with open(out) as f:
        words = f.read().split()
    check(len(words) == 3 and int(words[1]) >= 30 and words[2] == "true", "the script saw the machine run, and TAStudio has the project",
          " ".join(words))

# client.exit with unsaved changes: nobody asked, the project on disk as it was
proj, _ = project("unsaved")
before = digest(proj)
r = run(proj, script("unsaved", """
tastudio.setrecording(true)
for i = 1, 20 do joypad.set({["P1 A"] = true}); emu.frameadvance() end
client.exit()
"""))
check(r is not None and r.returncode == 0 and "closing unattended with unsaved changes" in r.stderr and digest(proj) == before,
      "client.exit leaves without asking, the changes let go and said, the project unchanged", said(r))

# a core that stops: 65, and why
proj, _ = project("stops", b"STOPONCE")
r = run(proj, script("stops", "for i = 1, 60 do emu.frameadvance() end\nclient.exit()\n"), extra_env={"CHIMERA_FRAME_TIMEOUT_MS": "500"})
check(r is not None and r.returncode == 65 and "[headless] The core stopped:" in r.stderr, "a core that stops ends the run with 65, said", said(r))

# a movie with an entry that does not read: the machine stops on it; 65, and why
proj, _ = project("unplayable")
with open(proj) as f:
    doc = json.load(f)
doc["inputs"] = {"columns": [[]], "entries": ["|zz|", "|zz|"]}
with open(proj, "w") as f:
    json.dump(doc, f)
r = run(proj, script("unplayable", "emu.frameadvance()\nclient.exit()\n"), seconds=60)
check(r is not None and r.returncode == 65 and "The movie could not be played: unparseable movie entry at frame 0" in r.stderr,
      "a movie that cannot be played ends the run with 65, saying why", said(r))

# a project whose game is not found: 64, the file named
proj, rom = project("missing")
os.remove(rom)
r = run(proj, script("missing", "client.exit()\n"))
check(r is not None and r.returncode == 64 and "caption: Cannot open the project" in r.stderr and os.path.basename(rom) in r.stderr,
      "a project whose files are not found ends the run with 64, naming them", said(r))

# a project whose core is not installed: 64, and why
proj, _ = project("nocore")
empty = os.path.join(work, "no-cores")
os.makedirs(empty)
try:
    r = subprocess.run([chimera, "--headless", "--project", proj, "--lua", script("nocore", "client.exit()\n")],
                       env=dict(env, CHIMERA_CORES_DIR=empty), timeout=120, capture_output=True, text=True)
except subprocess.TimeoutExpired:
    r = None
check(r is not None and r.returncode == 64 and "caption: Cannot open the project" in r.stderr and "core is installed" in r.stderr,
      "a project that cannot open ends the run with 64, saying why", said(r))

# a file the engine does not read as a project (here a key no project has): 64, and why. It
# used to be said only over the picture for a moment, and the run waited for ever (issue 422)
proj, _ = project("unread")
with open(proj) as f:
    unread = json.load(f)
unread["notAProjectKey"] = 1
with open(proj, "w") as f:
    json.dump(unread, f)
r = run(proj, script("unread", "client.exit()\n"), seconds=60)
check(r is not None and r.returncode == 64 and "caption: Cannot open the project" in r.stderr and "notAProjectKey" in r.stderr,
      "a file that is no project ends the run with 64, saying why", said(r))

# a script's file picker: nobody to pick, 64
proj, _ = project("picker")
r = run(proj, script("picker", "forms.openfile('x.txt')\nclient.exit()\n"))
check(r is not None and r.returncode == 64 and "caption: Open" in r.stderr, "a file picker ends the run with 64", said(r))

# work a killed run left: said, and the project opens as it was saved
proj, _ = project("killed")
lua = script("killed", """
tastudio.setrecording(true)
for i = 1, 20 do joypad.set({["P1 A"] = true}); emu.frameadvance() end
while true do emu.frameadvance() end
""")
p = subprocess.Popen([chimera, "--headless", "--project", proj, "--core", package, "--lua", lua], env=env,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(8)
p.send_signal(signal.SIGKILL)
p.wait()
out = os.path.join(work, "killed.txt")
r = run(proj, script("after", f"""
local f = io.open([[{out}]], "w")
f:write(tostring(movie.length()))
f:close()
client.exit()
"""))
check(r is not None and r.returncode == 0 and "[recovery]" in r.stderr and os.path.exists(out),
      "work a killed run left is said, and the run goes on with the project as saved", said(r))

print("%s: %d failed" % ("FAIL" if failures else "PASS", failures))
sys.exit(1 if failures else 0)
