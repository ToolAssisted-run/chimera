#!/bin/sh
# Builds the waterboxed Synth core package and the test roms, and installs the
# package into <MiniHawkRoot>/build/Cores/.
#
# miniHawk is WATERBOX-ONLY: the only shipped core package is the waterboxed
# flavor. A package is just core.wbx (fixed name) + waterbox.config; it is loaded
# by miniHawk's built-in generic WaterboxCore adapter through libminiboxhost,
# which ships with the frontend in build/dll (see the root meson.build). There is
# no per-core managed assembly, manifest, or natives list.
#
# native/ (synthcore.c + synth-run) is kept ONLY as the offline golden generator
# for the witness (built by run-witness.sh); it is not a core.
#
# Prereq: the frontend has been built (build/dll has libminiboxhost + the contract
# DLLs including the built-in WaterboxCore).
#
# Usage: ./build-package.sh [-c Configuration] [-r MiniHawkRoot]
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
configuration=Release
minihawk_root="$here/../.."
while getopts "c:r:" opt; do
	case "$opt" in
		c) configuration="$OPTARG" ;;
		r) minihawk_root="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
minihawk_root="$(cd "$minihawk_root" && pwd)"

# test roms
for src in "$here"/roms/*.sasm; do
	python3 "$here/asm.py" "$src" "${src%.sasm}.testrom"
done

# build the miniBox host + guest toolchain (musl builds in the meson graph), then
# the guest core.wbx.
mb="$minihawk_root/extern/miniBox"
mbuild="$mb/build/meson-linux"
[ -f "$mbuild/build.ninja" ] || meson setup "$mbuild" "$mb"
ninja -C "$mbuild"
sh "$here/package-box/build-box.sh"

# package = core.wbx (fixed name) + waterbox.config, nothing else.
staging="$here/package-box/bin/package-staging"
rm -rf "$staging"
mkdir -p "$staging"
cp "$here/package-box/synth.wbx" "$staging/core.wbx"
cp "$here/package-box/waterbox.config" "$staging"

cores_dir="$minihawk_root/build/Cores"
mkdir -p "$cores_dir"
zip_path="$cores_dir/synth-box.zip"
rm -f "$zip_path"
python3 - "$staging" "$zip_path" <<'PYEOF'
import os, sys, zipfile
staging, zip_path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(staging):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            z.write(full, os.path.relpath(full, staging))
PYEOF
# force re-extract on next load
for cache in "$minihawk_root"/build/CoreCache/synth-box-*; do
	[ -d "$cache" ] && rm -rf "$cache" || true
done
echo "packaged -> $zip_path"
