#!/usr/bin/env python3
"""Checks a core repository's texts against its English table.

    tools/check-core-texts.py <core checkout>

A core's texts are keys of its package's tables, languages/<code>.json
(docs/translations.md). Chimera's tests check the keys its declarations hold
(waterbox.config, file_slots.json); this checks the ones its own code
returns while it runs - a load error, a suggestion's note, an import's notes,
its game's settings, its properties' descriptions, its own windows' labels
(their QML) - which only the code knows. It reads the core's own sources (not extern/, build/ or a folder of
upstream code) and says:

- each string in them that looks like a key of a family the table has (its
  first word, "error." for error.no_file) and is not in the table;
- each key of the table that neither a declaration nor the code names.

A key built while the core runs is seen when the code names its family of
two words or more ("firmware.what." + name names every firmware.what.*);
one built in a family of one word ("setting." + name) is not, so name it
whole in a comment beside the code that builds it. Exit status 0 when there
is nothing to say.
"""
import json
import os
import re
import sys

# .qml and .mjs: a core's own windows (issue 610), whose labels are keys of its tables
SOURCES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".inc", ".rs", ".go", ".java", ".py", ".sh", ".gd", ".lua", ".js", ".patch",
           ".qml", ".mjs")
SKIP_DIRS = {".git", "extern", "build", "upstream", "third_party", "node_modules", "target", "languages"}
KEY = re.compile(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+$")
# a file's name is not a key: "firmware.bin"
FILE_NAME = re.compile(r"\.(bin|rom|img|iso|cue|chd|dat|sav|srm|ram|state|json|config|cfg|ini|xml|txt|md|png|jpg|"
                       r"tga|wav|ogg|zip|7z|gz|wbx|so|dll|exe|lua|sh|py|c|cpp|h|hpp|nes|smc|sfc|gb|gbc|gba|nds|tic|"
                       r"uze|hex|ch8|adf|ipf|dsk|tap|tzx|d64|t64|crt|prg|pup|elf|self|xex|iso9660|pak|wad|lmp)$")
# a string literal of any of those languages, and a key with arguments after
# it: "error.x\x1f", "error.x\u001f%s", "error.x" TEXT_ARG
LITERAL = re.compile(r'"((?:[^"\\\n]|\\.)*)"|\'((?:[^\'\\\n]|\\.)*)\'')
COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)
# a key in JSON written inside a literal: "{\"note\": \"suggest.fill\"}"
ESCAPED = re.compile(r'\\"([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)(?=\\"|\\\\u001[def])')


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    core = sys.argv[1]
    decl = os.path.join(core, "waterbox")
    if not os.path.isdir(decl):
        decl = core
    table_path = os.path.join(decl, "languages", "en.json")
    try:
        with open(table_path, encoding="utf-8") as f:
            table = json.load(f)["texts"]
    except (OSError, ValueError, KeyError) as e:
        sys.exit("%s: %s" % (table_path, e))
    families = {k.split(".", 1)[0] for k in table}

    used = set()
    lines = []

    def key_of(s):
        for cut in ("\\x1f", "\\u001f", "\\u{1f}", "\x1f", "\\x1e", "\\u001e", "\\u{1e}", "\x1e",
                    "\\x1d", "\\u001d", "\\u{1d}", "\x1d"):
            s = s.split(cut, 1)[0]
        return s

    # the declarations: every string value that is a key
    for name in ("waterbox.config", "file_slots.json"):
        path = os.path.join(decl, name)
        if not os.path.exists(path):
            continue
        with open(path, encoding="utf-8") as f:
            text = f.read()
        for m in re.finditer(r'"((?:[^"\\]|\\.)*)"', text):
            # a key, and its arguments that are keys too (each after \u001f,
            # \u001e or \u001d)
            for part in re.split(r"\\u001[def]", m.group(1)):
                if part in table:
                    used.add(part)

    for top, dirs, files in os.walk(core):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("."))
        for name in sorted(files):
            if not name.endswith(SOURCES):
                continue
            path = os.path.join(top, name)
            try:
                with open(path, encoding="utf-8", errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            # comments: a key named there counts as named (a key the code
            # builds, written out whole beside it), and nothing there is wrong
            comments = [(m.start(), m.end()) for m in COMMENT.finditer(text)] if not name.endswith((".py", ".sh")) else \
                [(m.start(), m.end()) for m in re.finditer(r"#[^\n]*", text)]
            in_comment = lambda at: any(a <= at < b for a, b in comments)
            found = [(m.start(), m.group(1) if m.group(1) is not None else m.group(2)) for m in LITERAL.finditer(text)]
            found += [(m.start(), m.group(1)) for m in ESCAPED.finditer(text)]
            for start, s in found:
                # a family the code builds its keys in ("firmware.what." + name):
                # two words or more, so a whole kind of text is never let off
                if re.match(r"^[a-z][a-z0-9_]*(\.[a-z0-9_]+)+\.$", s):
                    used.update(k for k in table if k.startswith(s))
                    continue
                k = key_of(s)
                if not KEY.match(k) or FILE_NAME.search(k):
                    continue
                if k in table:
                    used.add(k)
                elif k.split(".", 1)[0] in families and not in_comment(start):
                    line = text.count("\n", 0, start) + 1
                    lines.append("%s:%d: \"%s\" is not a key of %s" % (os.path.relpath(path, core), line, k,
                                                                      os.path.relpath(table_path, core)))
    for k in sorted(table):
        if k not in used:
            lines.append("%s: \"%s\" is named by no declaration and no code" % (os.path.relpath(table_path, core), k))
    for l in lines:
        print(l)
    if lines:
        print("%d problem(s)" % len(lines))
        return 1
    print("%d keys, every one named, every name a key" % len(table))
    return 0


if __name__ == "__main__":
    sys.exit(main())
