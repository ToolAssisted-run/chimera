#!/usr/bin/env python3
"""Proper nouns are never translated (owner, 2026-10-31): Chimera is always
Chimera, in every language, as are the names of other programs and
services. A language whose grammar puts endings on names says the sentence
with a common noun beside the name instead ("v programu Chimera", "TAStudio-
asetukset"). This checks Chimera's language tables (docs/translations.md):

- every name an English text holds is in that text in every language, as
  it is written, with no letter of the Latin alphabet glued to either side
  of it (Korean's and Japanese's particles, in their own scripts, are their
  grammar and leave the name whole);
- no text glues Latin letters to a name, whatever its English says.

  tools/check-proper-nouns.py [<chimera checkout>]

Prints each one as language key: text, and exits 1 when there is any."""
import json
import os
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

NAMES = ["Chimera", "TAStudio", "GitHub", "Lua", "Windows", "Linux", "macOS", "OpenGL", "Vulkan", "Direct3D", "SQLite",
         "Sublime Text", "miniBox", "ffmpeg", "Qt", "Wayland"]
LATIN = "A-Za-zÀ-ɏ"


def whole(name):
    return re.compile("(?<![" + LATIN + "])" + re.escape(name) + "(?![" + LATIN + "])")


def glued(name):
    return re.compile("[" + LATIN + "]" + re.escape(name) + "|" + re.escape(name) + "[" + LATIN + "]")


def texts(path):
    """a table's texts, a key's plural forms as one text"""
    with open(path, encoding="utf-8") as f:
        table = json.load(f)["texts"]
    return {k: " | ".join(v.values()) if isinstance(v, dict) else v for k, v in table.items()}


folder = os.path.join(root, "languages")
english = texts(os.path.join(folder, "en.json"))
# a name glued inside a longer word of English's own (Chimera in chimeraProject) is no name
names_in = {k: [n for n in NAMES if whole(n).search(v)] for k, v in english.items()}
bad = 0
for name in sorted(os.listdir(folder)):
    if not name.endswith(".json") or name == "en.json":
        continue
    code = name[:-5]
    for key, text in sorted(texts(os.path.join(folder, name)).items()):
        for n in names_in.get(key, []):
            if not whole(n).search(text):
                print("%s %s: %s is not there as itself: %s" % (code, key, n, text.replace("\n", " ")))
                bad += 1
        for n in NAMES:
            # file names and identifiers are not names in a sentence (chimeraProject, Lua's lua)
            if glued(n).search(re.sub(r"\S*[./*]\S*", "", text)):
                print("%s %s: %s has letters glued to it: %s" % (code, key, n, text.replace("\n", " ")))
                bad += 1
print("%d proper nouns changed" % bad, file=sys.stderr)
sys.exit(1 if bad else 0)
