#!/usr/bin/env python3
# Writes a copy of a miniHawk config with every binding for one controller removed - the state a
# fresh install is in, which is when a package's shipped bindings are supposed to fill in.
#
# Usage: forget-controller.py <source config> <output config> <controller name>
import json
import sys

cfg = json.load(open(sys.argv[1]))
name = sys.argv[3]
for section in ("AllTrollers", "AllTrollersAutoFire", "AllTrollersAnalog", "AllTrollersFeedbacks"):
    cfg.get(section, {}).pop(name, None)
json.dump(cfg, open(sys.argv[2], "w"), indent=2)
