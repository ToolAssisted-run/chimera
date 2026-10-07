#!/bin/bash
# Asks, on WINDOWS, whether the new-project wizard's firmware page lays out its
# row of buttons - Select File..., Clear, Scan Folder... - without one covering
# the next (chimera issue #160). tests/ui/run-ui-tests.sh runs on Mono, whose
# wider default font scales every fixed position differently from Windows, so
# the overlap users saw does not show there.
#
# It compiles WizardFirmwareButtons.cs against the frontend assemblies this tree
# has just built and runs it. Run it from WSL on a Windows box:
#
#     dotnet build source/gui/Chimera.sln -c Release
#     ./tests/ui/windows/wizard-firmware-buttons.sh
#
# It prints each button's geometry, draws the row, and exits non-zero if a
# button reaches past the start of the next.
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
# (Windows' own %TEMP%: the WSL user name need not be the Windows one)
win_temp="${WIN_TEMP:-}"
if [ -z "$win_temp" ] && command -v wslpath >/dev/null; then
	win_temp="$(wslpath "$(/mnt/c/Windows/System32/cmd.exe /c echo %TEMP% 2>/dev/null | tr -d '\r')" 2>/dev/null)"
fi
[ -d "$win_temp" ] || win_temp="$(dirname "$(mktemp -u)")"
work="$win_temp/chimera-wizard-firmware-buttons"
rm -rf "$work"; mkdir -p "$work" || exit 1
cleanup() { rm -rf "$work"; }
trap cleanup EXIT

# The frontend builds as an exe whose assembly name is Chimera.Client.GUI, and
# that is the name the runtime will look for, so it is copied under it.
cp "$build/Chimera.exe" "$work/Chimera.Client.GUI.exe"
cp "$build/dll/"*.dll "$work/" 2>/dev/null
cp "$here/WizardFirmwareButtons.cs" "$work/"

# the Windows path of the same folder, for csc and for the program itself
if command -v wslpath >/dev/null; then winpath="$(wslpath -w "$work")"
else winpath="$(cd "$work" && cmd.exe /c cd 2>/dev/null | tr -d '\r')"
fi
[ -n "$winpath" ] || { echo "cannot work out the Windows path of $work" >&2; exit 1; }

"$csc" -nologo -target:exe -platform:x64 \
	-out:"$winpath\\WizardFirmwareButtons.exe" \
	-r:System.dll -r:System.Core.dll -r:System.Drawing.dll -r:System.Windows.Forms.dll \
	-r:"$winpath\\Chimera.Client.GUI.exe" -r:"$winpath\\Chimera.Client.Common.dll" \
	-r:"$winpath\\Chimera.Common.dll" -r:"$winpath\\Chimera.Emulation.Common.dll" -r:netstandard.dll \
	"$winpath\\WizardFirmwareButtons.cs" 2>&1 | tr -d '\r'
[ -f "$work/WizardFirmwareButtons.exe" ] || { echo "did not compile" >&2; exit 1; }

"$work/WizardFirmwareButtons.exe" "$winpath" "$@" 2>&1 | tr -d '\r'
status=${PIPESTATUS[0]}

out="${OUT_DIR:-$repo_root/tests/ui/shots}"
mkdir -p "$out"
cp "$work/wizard-firmware-buttons.png" "$out/" 2>/dev/null && echo "picture: $out/wizard-firmware-buttons.png"
exit "$status"
