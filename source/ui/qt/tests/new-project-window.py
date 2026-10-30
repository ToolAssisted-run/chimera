#!/usr/bin/env python3
# New Project's window waits for the project it makes (issue 422). Create
# makes the project away from the window, since that reads and hashes every
# file; the window closed at once, so a project the engine then refused (a
# file of a format its slot does not take) was said nowhere, and nothing
# happened. Now the window stays open until the project is made, and closes
# only then. The script here makes the project through the view model, as
# the Create button does, so what is tested is what the window does when it
# is told how the making went, not the button itself.
#
# And no page listens for a view model's changes with onValueChanged: Qt's
# QQmlPropertyMap sends valueChanged only for a change made from QML, never
# for one the view model makes, so such a handler never runs. The wizard's
# line and Movie Import's closing waited on one.
#   new-project-window.py <chimera> <work folder> <package> <game> <qml folder>
import glob
import json
import os
import re
import shutil
import subprocess
import sys

chimera, work, package, game, qml = sys.argv[1:6]
shutil.rmtree(work, ignore_errors=True)
os.makedirs(os.path.join(work, "cores"))
shutil.copy(package, os.path.join(work, "cores"))
shutil.copy(game, work)
wrong = os.path.join(work, "Elite.ssd")
with open(wrong, "w") as f:
    f.write("not a game for this core")
failures = 0


def check(ok, what, got=None):
    global failures
    print(("PASS  " if ok else "FAIL  ") + what + ("" if ok or got is None else "  (" + got + ")"))
    if not ok:
        failures += 1


def run(name, file):
    """the window with New Project shown, a script making a project of the file"""
    folder = os.path.join(work, name)
    os.makedirs(folder)
    lua = os.path.join(folder, name + ".lua")
    with open(lua, "w") as f:
        f.write('chimera.newProject.chooseCore("Synth")\n')
        f.write('chimera.newProject.addFile("rom", %s)\n' % json.dumps(file))
        f.write("chimera.newProject.create()\n")
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software", ALSOFT_DRIVERS="null",
               CHIMERA_CORES_DIR=os.path.join(work, "cores"), CHIMERA_DATA_HOME=os.path.join(folder, "data"),
               CHIMERA_QT_CONFIG=os.path.join(folder, "settings.json"), CHIMERA_QT_CACHE=os.path.join(folder, "cache"),
               CHIMERA_QT_RECOVERY=os.path.join(folder, "recovery"),
               CHIMERA_QT_HASHES=os.path.join(folder, "file-hashes.txt"))
    shot = os.path.join(folder, "window.png")
    try:
        done = subprocess.run([chimera, "--show", "new-project", "--lua", lua, "--measure", "3", "--screenshot", shot],
                              env=env, capture_output=True, text=True, timeout=90)
    except subprocess.TimeoutExpired:
        return "the window was still running after 90 seconds"
    return done.stdout + done.stderr


out = run("refused", wrong)
check("screenshot: newProjectWindow saved" in out, "a project refused for its files: the window stays open to say why", out)
check("status: Nothing open" in out, "and nothing is opened", out)
out = run("made", os.path.join(work, os.path.basename(game)))
check("status: Opened" in out, "a project made: it opens", out)
check("newProjectWindow" not in out, "and the window closes", out)

waiting = []
for page in sorted(glob.glob(os.path.join(qml, "**", "*.qml"), recursive=True)):
    with open(page, encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            if re.search(r"\bonValueChanged\b", line):
                waiting.append("%s:%d" % (os.path.relpath(page, qml), number))
check(not waiting, "no page waits on onValueChanged, which a view model's change never sends", ", ".join(waiting))

sys.exit(1 if failures else 0)
