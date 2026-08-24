#!/usr/bin/env python3
# Asserts that the bindings a core package ships became the frontend's defaults for its controller.
#
# Usage: check-keybinds.py <config written by Chimera> <package default_keybinds.json> <controller>
import json
import sys

cfg = json.load(open(sys.argv[1]))
shipped = json.load(open(sys.argv[2]))
name = sys.argv[3]

want = shipped["AllTrollers"][name]
got = cfg.get("AllTrollers", {}).get(name)
if got is None:
    sys.exit(f"the frontend has no bindings at all for '{name}'")
differing = {k: (v, got.get(k)) for k, v in want.items() if got.get(k) != v}
if differing:
    sys.exit(f"bindings differ from the package's: {differing}")

wantAF = shipped.get("AllTrollersAutoFire", {}).get(name)
if wantAF is not None:
    gotAF = cfg.get("AllTrollersAutoFire", {}).get(name)
    if gotAF != wantAF:
        sys.exit(f"autofire bindings differ from the package's: shipped {wantAF}, config {gotAF}")
print(f"{name}: {len(want)} bindings adopted from the package")
