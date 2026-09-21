#!/bin/bash
# Runs tests/engine/windows/refused-open.c against the engine DLL this tree has
# just cross-built, ON WINDOWS - which is the only place the defect it looks for
# exists (chimera#123; see the .c for what it asks and why).
#
# From WSL on a Windows box, which is how this project is developed:
#
#     meson compile -C build/meson-mingw
#     ./tests/engine/windows/refused-open.sh
#
# It never touches an installed Chimera: everything is copied into a scratch
# folder of its own under the Windows temp directory, and removed afterwards.
# With build/Cores/quickernes.chimeraCore present it also drives a real machine
# refused at Init; without it that half says SKIP and why.
#
# CI cannot run this (the runners are Linux). See docs/gates.md, mode G, for
# who owns running it.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/../../.." && pwd)"
mingw="$repo_root/build/meson-mingw"
[ -f "$mingw/libchimera.dll" ] || {
	echo "no $mingw/libchimera.dll - run: meson compile -C build/meson-mingw" >&2; exit 1; }

cc="${MINGW_CC:-x86_64-w64-mingw32-gcc}"
command -v "$cc" >/dev/null || { echo "no $cc on PATH (set MINGW_CC=...)" >&2; exit 1; }

win_temp="${WIN_TEMP:-/mnt/c/Users/$USER/AppData/Local/Temp}"
[ -d "$win_temp" ] || win_temp="$(dirname "$(mktemp -u)")"
work="$win_temp/chimera-refused-open"
rm -rf "$work"; mkdir -p "$work" || exit 1
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

# beside the DLLs, because that is how Windows finds them
cp "$mingw/libchimera.dll" "$mingw/libminiboxhost.dll" "$work/"
[ -f "$mingw/libzstd.dll" ] && cp "$mingw/libzstd.dll" "$work/"
"$cc" -O1 -Wall -Wextra -o "$work/refused-open.exe" "$here/refused-open.c" || exit 1

# a core and something it will not take, when this tree has them
core="$repo_root/build/Cores/quickernes.chimeraCore"
args=""
if [ -f "$core" ]; then
	head -c 4096 /dev/urandom > "$work/not-a-rom.nes"
	cp "$core" "$work/core.chimeraCore"
	args="core.chimeraCore not-a-rom.nes"
fi

# A fault in host code writes minibox-diag.log beside the process; there must
# be none, and the program must return 0.
(cd "$work" && ./refused-open.exe $args) > "$work/out.txt" 2>&1
status=$?
tr -d '\r' < "$work/out.txt"
if [ -f "$work/minibox-diag.log" ]; then
	echo "FAIL a refused open wrote a fault report:" >&2
	sed -n '1,20p' "$work/minibox-diag.log" >&2
	status=1
fi
exit "$status"
