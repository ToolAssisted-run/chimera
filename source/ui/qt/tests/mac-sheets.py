#!/usr/bin/env python3
# Every modal window is a sheet on a Mac (owner, 2026-11-07, issue 541):
# macOS keeps an application-modal window over other apps' windows while
# Chimera is the active app, so on a Mac each is window-modal instead, which
# Qt shows as a sheet on the window it belongs to. Windows and Linux keep
# them application-modal. This reads every QML file's modality.
#   mac-sheets.py <folder of the window's QML>
import os
import re
import sys

folder = sys.argv[1]
wanted = 'modality: Qt.platform.os === "osx" ? Qt.WindowModal : Qt.ApplicationModal'
bad, seen = [], 0
for name in sorted(os.listdir(folder)):
    if not name.endswith(".qml"):
        continue
    with open(os.path.join(folder, name), encoding="utf-8") as f:
        for number, line in enumerate(f, 1):
            if re.match(r"\s*modality\s*:", line):
                seen += 1
                if wanted not in line:
                    bad.append("%s:%d: %s" % (name, number, line.strip()))
for b in bad:
    print("FAIL  application-modal on a Mac: " + b)
print("%d modal windows, %d not a sheet on a Mac" % (seen, len(bad)))
sys.exit(1 if bad or seen == 0 else 0)
