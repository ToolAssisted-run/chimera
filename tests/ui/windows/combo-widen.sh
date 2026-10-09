#!/bin/bash
# Asks, on WINDOWS, whether a combo box under the Dark theme is drawn right
# after the window it is anchored in has been widened (chimera#230: maximised,
# the New Project window showed a second button beside every box).
# tests/ui/run-ui-tests.sh runs on Mono, which paints a flat combo box its own
# way; what a user sees is decided by the Windows toolkit.
#
# It compiles ComboWiden.cs against the frontend assemblies this tree has
# just built and runs it. See that file for what it checks.
#
# Run it from WSL on a Windows box (which is how this project is developed):
#
#     dotnet build source/gui/Chimera.sln
#     ./tests/ui/windows/combo-widen.sh
#
# or run it natively on Windows with csc.exe on PATH. It writes a picture of
# the two boxes and exits non-zero if they differ. With "unthemed-control" as
# its argument it runs the control instead: a flat box the theme engine never
# saw, which must show the fault.
#
# It never touches an installed Chimera: everything is copied into a scratch
# folder of its own, which is removed afterwards.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/../../.." && pwd)"
build="$repo_root/build"
[ -f "$build/Chimera.exe" ] || { echo "no build/Chimera.exe - run: dotnet build source/gui/Chimera.sln" >&2; exit 1; }

# WSL starts a Windows program through an interop handler, and a machine with
# Mono installed has another handler for .exe files in front of it ("run-
# detectors: unable to find an interpreter"). /init starts one when asked by
# name, whichever handler is registered - and takes what follows the program
# as its whole argument list, the program's own name first.
winrun() {
	if [ -x /init ]; then /init "$1" "$@"; else "$@"; fi
}

csc="${CSC:-/mnt/c/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe}"
[ -x "$csc" ] || { echo "no csc.exe at $csc (set CSC=... to point at one)" >&2; exit 1; }

# a folder Windows can reach, because the program being compiled is a Windows one
# (Windows' own %TEMP%: the WSL user name need not be the Windows one)
win_temp="${WIN_TEMP:-}"
if [ -z "$win_temp" ] && command -v wslpath >/dev/null; then
	win_temp="$(wslpath "$(cd /mnt/c && winrun /mnt/c/Windows/System32/cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')" 2>/dev/null)"
fi
[ -d "$win_temp" ] || win_temp="$(dirname "$(mktemp -u)")"
work="$win_temp/chimera-combo-widen"
rm -rf "$work"; mkdir -p "$work" || exit 1
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

# The frontend builds as an exe whose assembly name is Chimera.Client.GUI, and
# that is the name the runtime will look for, so it is copied under it.
cp "$build/Chimera.exe" "$work/Chimera.Client.GUI.exe"
cp "$build/dll/"*.dll "$work/" 2>/dev/null
cp "$here/ComboWiden.cs" "$work/"

# the Windows path of the same folder, for csc and for the program itself
if command -v wslpath >/dev/null; then winpath="$(wslpath -w "$work")"
else winpath="$(cd "$work" && cmd.exe /c cd 2>/dev/null | tr -d '\r')"
fi
[ -n "$winpath" ] || { echo "cannot work out the Windows path of $work" >&2; exit 1; }

winrun "$csc" -nologo -target:exe -platform:x64 \
	-out:"$winpath\\ComboWiden.exe" \
	-r:System.dll -r:System.Core.dll -r:System.Drawing.dll -r:System.Windows.Forms.dll \
	-r:"$winpath\\Chimera.Client.GUI.exe" -r:"$winpath\\Chimera.Client.Common.dll" \
	"$winpath\\ComboWiden.cs" 2>&1 | tr -d '\r'
[ -f "$work/ComboWiden.exe" ] || { echo "did not compile" >&2; exit 1; }

winrun "$work/ComboWiden.exe" "$winpath\\combo-widen.png" ${1:+"$1"} 2>&1 | tr -d '\r'
status=${PIPESTATUS[0]}

out="${OUT_DIR:-$repo_root/tests/ui/shots}"
mkdir -p "$out"
cp "$work/combo-widen.png" "$out/" 2>/dev/null && echo "picture: $out/combo-widen.png"
exit "$status"
