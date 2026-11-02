#!/usr/bin/env python3
"""No text in the code (docs/translations.md, owner 2026-10-28): every text a
person reads is a key in the language tables. This finds string literals in
Chimera's code that read as words - two words, or a capitalised one - and
are none of what the rules leave English:

- what a developer reads: logs (standard error, the core log, traces) and
  the output of the command line (chimera-run, the window's --measure);
- the descriptions of Lua functions, MCP tools and view-model properties;
- identifiers that are English words: hotkey names, the on-screen
  elements, key names, config names (tools/check-texts-identifiers.txt);
- tests.

  tools/check-texts.py [<chimera checkout>]

Prints each one as file:line: text, and exits 1 when there is any."""
import os
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")

WORDS = re.compile(r"[A-Za-z]{2,}[ ,.:;!?']\s*[A-Za-z]")
CAPITALISED = re.compile(r"[A-Z][a-z]{2,}(\.\.\.|:)?")
NOT_WORDS = re.compile(
    r"^(qrc:|:/|file:|https?:|[a-z]+://)"
    r"|^[\w./\\ -]*\.(png|svg|ico|json|txt|lua|qml|js|log|zip|wch|input|sublime-completions|jpg|gif|bmp|tga|mp4|mkv|wav|dll|so|exe|chimera\w*)$"
    r"|^(Ctrl|Alt|Shift|Meta)\+"
    r"|^\[[a-z][a-z-]*\] "                  # a log line: "[history] ..."
    r"|^-[a-z]"                              # a command line's arguments: "-c:v libx264 ..."
    r"|^[A-Za-z]:\\\\"                        # a Windows path
    r"|^[{\[]\\?\""                           # JSON being written
    r"|^(bold |italic )?\d+px "             # a font: "12px monospace"
    r"|^[a-z][A-Za-z0-9_]*(\.[A-Za-z0-9_]+)+$"
)

# calls whose every literal is left English: what a developer reads
LOG_CALLS = {"fprintf", "printf", "fputs", "puts", "qDebug", "qWarning", "qInfo", "qCritical", "trace",
             "coreLog", "coreLogNote", "logLine", "log", "glRefused", "static_assert", "appendLine", "QCommandLineOption", "addPositionalArgument", "setApplicationDescription",
             # --headless's report of what would have waited for a person
             "blocked",
             # the crash note's own lines, for the developers a crash is sent to
             "describeSession",
             # code, not words: QML handed to Qt, names registered with it
             "setData", "qmlRegisterType", "setApplicationName", "setExtraFileSelectors"}
# calls whose given arguments are identifiers: the theme preview draws a
# colour by its name (Themes.qml)
IDENTIFIER_ARGS = {"fill": {0}, "outline": {0}, "write": {0}, "mark": {0}, "line": {0}, "poly": {0}}
# a declaration that is a call: QCommandLineOption fillOpt(...)
DECLARED = {"QCommandLineOption"}
# calls whose second argument is a description for scripts and agents
DOC_CALLS = {"expose", "command", "rows", "exposeList", "library", "function", "picture"}

def identifiers():
    path = os.path.join(root, "tools/check-texts-identifiers.txt")
    out = set()
    for line in open(path, encoding="utf-8"):
        line = line.rstrip("\n")
        if line and not line.startswith("#"):
            out.add(line)
    return out

# a piece of a sentence joined to others: words with a space at an end
PIECE = re.compile(r"^\s+[A-Za-z]{2,}|[A-Za-z]{2,}[.:]?\s+$")

# a printf conversion: "%0*lld" holds no words
CONVERSION = re.compile(r"%[-+ #0]*[0-9*]*(\.[0-9*]+)?(ll|l|z|j|h|hh)?[a-zA-Z]")

def is_text(s, known):
    if s in known or NOT_WORDS.search(s):
        return False
    s = CONVERSION.sub("", s)
    return bool(WORDS.search(s)) or bool(CAPITALISED.fullmatch(s)) or bool(PIECE.search(s))

def literals(text):
    """(line, value, calls) for each string literal: calls is the chain of
    enclosing calls, innermost last, as (name, argument index)"""
    out, stack, i, line, n = [], [], 0, 1, len(text)
    last_word, word_before = "", ""
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
            continue
        if c == "#" and (i == 0 or text[i - 1] == "\n"):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "'" and i + 2 < n and (text[i + 2] == "'" or text[i + 1] == "\\"):
            j = text.find("'", i + 2 if text[i + 1] == "\\" else i + 1)
            i = j + 1 if j > 0 else i + 1
            continue
        if c == "R" and text.startswith('R"', i):
            m = re.match(r'R"([^(]*)\(', text[i:])
            if m:
                end = text.find(")" + m.group(1) + '"', i)
                if end > 0:
                    line += text.count("\n", i, end)
                    i = end + len(m.group(1)) + 2
                    continue
        if c == '"':
            j, buf = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    buf.append(text[j:j + 2])
                    j += 2
                    continue
                if text[j] == "\n":
                    break
                buf.append(text[j])
                j += 1
            out.append((line, "".join(buf), [(s[0], s[1]) for s in stack]))
            i = j + 1
            last_word = ""
            continue
        if c.isalpha() or c == "_":
            m = re.match(r"[A-Za-z_][A-Za-z0-9_]*", text[i:])
            word_before, last_word = last_word, m.group(0)
            i += len(last_word)
            continue
        if c in "([{":
            name = word_before if word_before in DECLARED else last_word
            stack.append([name if c == "(" else "", 0, c])
        elif c in ")]}":
            if stack:
                stack.pop()
        elif c == "," and stack:
            stack[-1][1] += 1
        if not c.isspace():
            last_word = word_before = ""
        i += 1
    return out

def left_english(calls):
    for name, _ in calls:
        if name in LOG_CALLS:
            return True
    if calls and calls[-1][0] in DOC_CALLS and calls[-1][1] == 1:
        return True
    if calls and calls[-1][1] in IDENTIFIER_ARGS.get(calls[-1][0], ()):
        return True
    return False

def scan(path, qml, known):
    found = []
    text = open(path, encoding="utf-8", errors="replace").read()
    for line, s, calls in literals(text):
        if qml and calls and calls[-1][0] == "qsTrId":
            continue
        if left_english(calls) or not is_text(s, known):
            continue
        found.append((line, s))
    return found

# left English as a whole: the MCP server (an agent's interface), the config
# file's names, the command line's own output
WHOLE_FILES_LEFT = {
    "source/engine/source/mcp.cpp", "source/engine/source/mcp_local.cpp",
    "source/ui/viewmodel/src/config_names.cpp",
    # --headless: a run nobody watches says on standard error what stopped it
    "source/ui/qt/headless_run.cpp",
    # the language tables' own reader: its errors are about a table that may not read
    "source/engine/source/language.cpp",
    # the state files' layout, described for test_state_format
    "source/engine/source/state_format.hpp",
    # the names keys have in bindings, as Chimera writes them
    "source/ui/qt/key_names.cpp",
    # a bug report's first lines, in the words of Chimera's issue form on GitHub
    "source/ui/viewmodel/src/issue_report.cpp",
}

def files():
    for folder in ("source/ui/qt", "source/ui/viewmodel/src", "source/ui/viewmodel/include/chimera/vm", "source/engine/source"):
        base = os.path.join(root, folder)
        for f in sorted(os.listdir(base)):
            if f.endswith((".qml", ".cpp", ".hpp", ".h")):
                yield os.path.join(folder, f), f.endswith(".qml")

def main():
    known = identifiers()
    total = 0
    for rel, qml in files():
        if rel in WHOLE_FILES_LEFT:
            continue
        for n, s in scan(os.path.join(root, rel), qml, known):
            print("%s:%d: %s" % (rel, n, s[:100]))
            total += 1
    print("%d texts in the code" % total, file=sys.stderr)
    return 1 if total else 0

sys.exit(main())
