#!/bin/bash
# Asks, on WINDOWS, whether the folder picker really carries the "Include
# sub-folders" tick box and hands back what the person left it at - the one
# question tests/ui/run-ui-tests.sh cannot answer, because it runs on Mono
# under Xvfb and the box is a control added to the Windows shell's own common
# item dialog through IFileDialogCustomize.
#
# It compiles FolderPickerCheckBox.cs against the frontend assemblies this tree
# has just built and runs it. See that file for what it checks.
#
# Run it from WSL on a Windows box (which is how this project is developed):
#
#     dotnet build source/gui/Chimera.sln -c Release
#     ./tests/ui/windows/folder-picker-checkbox.sh
#
# or run it natively on Windows with csc.exe on PATH. It writes a picture of
# the dialog and exits non-zero if any check failed.
#
# Pass --by-hand to drive the dialogs yourself. That is the check nothing
# automated replaces: that the box LOOKS like part of the dialog, sits where
# the eye expects it, and can be clicked with a mouse.
#
# It never touches an installed Chimera: everything is copied into a scratch
# folder of its own, which is removed afterwards.
set -u

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/../../.." && pwd)"
build="$repo_root/build"
[ -f "$build/Chimera.exe" ] || { echo "no build/Chimera.exe - run: dotnet build source/gui/Chimera.sln -c Release" >&2; exit 1; }

csc="${CSC:-/mnt/c/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe}"
[ -x "$csc" ] || { echo "no csc.exe at $csc (set CSC=... to point at one)" >&2; exit 1; }

# a folder Windows can reach, because the program being compiled is a Windows one
win_temp="${WIN_TEMP:-/mnt/c/Users/$USER/AppData/Local/Temp}"
[ -d "$win_temp" ] || win_temp="$(dirname "$(mktemp -u)")"
work="$win_temp/chimera-folder-picker-checkbox"
rm -rf "$work"; mkdir -p "$work" || exit 1
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

# The frontend builds as an exe whose assembly name is Chimera.Client.GUI, and
# that is the name the runtime will look for, so it is copied under it.
cp "$build/Chimera.exe" "$work/Chimera.Client.GUI.exe"
cp "$build/dll/"*.dll "$work/" 2>/dev/null
cp "$here/FolderPickerCheckBox.cs" "$work/"

# the Windows path of the same folder, for csc and for the program itself
if command -v wslpath >/dev/null; then winpath="$(wslpath -w "$work")"
else winpath="$(cd "$work" && cmd.exe /c cd 2>/dev/null | tr -d '\r')"
fi
[ -n "$winpath" ] || { echo "cannot work out the Windows path of $work" >&2; exit 1; }

"$csc" -nologo -target:exe -platform:x64 \
	-out:"$winpath\\FolderPickerCheckBox.exe" \
	-r:System.dll -r:System.Core.dll -r:System.Drawing.dll -r:System.Windows.Forms.dll \
	-r:"$winpath\\Chimera.Client.GUI.exe" -r:"$winpath\\Chimera.Client.Common.dll" \
	-r:"$winpath\\Chimera.Common.dll" \
	"$winpath\\FolderPickerCheckBox.cs" 2>&1 | tr -d '\r'
[ -f "$work/FolderPickerCheckBox.exe" ] || { echo "did not compile" >&2; exit 1; }

"$work/FolderPickerCheckBox.exe" "$winpath" "$@" 2>&1 | tr -d '\r'
status=${PIPESTATUS[0]}

out="${OUT_DIR:-$repo_root/tests/ui/shots}"
mkdir -p "$out"
cp "$work/folder-picker-checkbox.png" "$out/" 2>/dev/null && echo "picture: $out/folder-picker-checkbox.png"
exit "$status"
