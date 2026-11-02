"""A project for a game, for the window's tests. A game runs only in a
project (owner, 2026-10-26), so a test that plays one makes one, as the New
Project window would, with the game copied beside it:

  game-project.py <package> <game> <out.chimeraProject>

The package's core by name, the game in the slot that takes its kind of file
(else the first); the settings and firmware are the package's own defaults.
"""
import hashlib
import json
import os
import shutil
import sys
import zipfile

# a project is a zip (owner, 2026-11-02): written as the witness writes one
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "..", "tests", "synth"))
import project_file  # noqa: E402

package, game, out = sys.argv[1:4]
with zipfile.ZipFile(package) as z:
    cfg = json.loads(z.read("waterbox.config"))
    slots = json.loads(z.read("file_slots.json")).get("slots", []) if "file_slots.json" in z.namelist() else []
kind = os.path.splitext(game)[1][1:].lower()
slot = next((s["id"] for s in slots if kind in [f.lower() for f in s.get("formats", [])]),
            slots[0]["id"] if slots else "rom")
folder = os.path.dirname(os.path.abspath(out))
os.makedirs(folder, exist_ok=True)
beside = os.path.join(folder, os.path.basename(game))
if os.path.abspath(game) != beside:
    shutil.copyfile(game, beside)
with open(beside, "rb") as f:
    sha1 = hashlib.sha1(f.read()).hexdigest().upper()
project_file.write(out, {
    "title": os.path.splitext(os.path.basename(game))[0],
    "core": {"name": cfg["coreName"], "version": cfg.get("version", ""), "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": cfg.get("systemId", "")},
    "files": [{"name": os.path.basename(game), "sha1": sha1, "slot": slot}],
})
