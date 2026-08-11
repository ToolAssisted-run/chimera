#!/bin/sh
# Builds the native-flavor Synth core package (synth-native.zip) and the test
# roms, and installs the package into <MiniHawkRoot>/build/Cores/.
# Prereq: the miniHawk solution has been built (contract DLLs in build/dll).
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

# native core: Linux always; Windows too when the mingw cross-compiler exists
gcc -O2 -Wall -Wextra -Werror -fvisibility=hidden -shared -fPIC \
	-Wl,-soname,libsynthcore.so \
	-o "$here/native/libsynthcore.so" "$here/native/synthcore.c"
if command -v x86_64-w64-mingw32-gcc >/dev/null; then
	x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -shared -static-libgcc \
		-o "$here/native/libsynthcore.dll" "$here/native/synthcore.c"
fi

# test roms
for src in "$here"/roms/*.sasm; do
	python3 "$here/asm.py" "$src" "${src%.sasm}.testrom"
done

# managed adapters (both flavors) + the C# level A tester
dotnet build "$here/package/MiniHawk.SynthNative.csproj" -c "$configuration" \
	-p:MiniHawkRoot="$minihawk_root" -v q --nologo /nodeReuse:false -p:UseSharedCompilation=false
dotnet build "$here/package-sharp/MiniHawk.SynthSharp.csproj" -c "$configuration" \
	-p:MiniHawkRoot="$minihawk_root" -v q --nologo /nodeReuse:false -p:UseSharedCompilation=false
dotnet build "$here/package-sharp/tester/SynthRunSharp.csproj" -c "$configuration" \
	-v q --nologo /nodeReuse:false -p:UseSharedCompilation=false

# waterboxed flavor (c): build the miniBox host + guest toolchain (musl builds in
# the meson graph), then synth.wbx, then the managed box adapter.
mb="$minihawk_root/extern/miniBox"
mbuild="$mb/build/meson-linux"
[ -f "$mbuild/build.ninja" ] || meson setup "$mbuild" "$mb"
ninja -C "$mbuild"
sh "$here/package-box/build-box.sh"
dotnet build "$here/package-box/MiniHawk.SynthBox.csproj" -c "$configuration" \
	-p:MiniHawkRoot="$minihawk_root" -v q --nologo /nodeReuse:false -p:UseSharedCompilation=false

staging="$here/package/bin/package-staging"
rm -rf "$staging"
mkdir -p "$staging"
cp "$here/package/minihawk-core.json" "$staging"
cp "$here/package/defctrl.json" "$staging"
cp "$here/package/bin/$configuration/MiniHawk.SynthNative.dll" "$staging"
cp "$here/native/libsynthcore.so" "$staging"
[ -f "$here/native/libsynthcore.dll" ] && cp "$here/native/libsynthcore.dll" "$staging"

# pure-C# flavor staging: adapter + manifest + defctrl, nothing else
staging_sharp="$here/package-sharp/bin/package-staging"
rm -rf "$staging_sharp"
mkdir -p "$staging_sharp"
cp "$here/package-sharp/minihawk-core.json" "$staging_sharp"
cp "$here/package-sharp/defctrl.json" "$staging_sharp"
cp "$here/package-sharp/bin/$configuration/MiniHawk.SynthSharp.dll" "$staging_sharp"

# waterboxed flavor staging: adapter + manifest + defctrl + the guest (synth.wbx)
# + the miniBox host (libminiboxhost, the declared native).
staging_box="$here/package-box/bin/package-staging"
rm -rf "$staging_box"
mkdir -p "$staging_box"
cp "$here/package-box/minihawk-core.json" "$staging_box"
cp "$here/package-box/defctrl.json" "$staging_box"
cp "$here/package-box/bin/$configuration/MiniHawk.SynthBox.dll" "$staging_box"
cp "$here/package-box/synth.wbx" "$staging_box"
cp "$mbuild/source/host/libminiboxhost.so" "$staging_box"
[ -f "$mbuild/source/host/libminiboxhost.dll" ] && cp "$mbuild/source/host/libminiboxhost.dll" "$staging_box"

cores_dir="$minihawk_root/build/Cores"
mkdir -p "$cores_dir"
zip_path="$cores_dir/synth-native.zip"
rm -f "$zip_path"
python3 - "$staging" "$zip_path" <<'EOF'
import os, sys, zipfile
staging, zip_path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(staging):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            z.write(full, os.path.relpath(full, staging))
EOF
zip_sharp="$cores_dir/synth-sharp.zip"
rm -f "$zip_sharp"
python3 - "$staging_sharp" "$zip_sharp" <<'PYEOF'
import os, sys, zipfile
staging, zip_path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(staging):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            z.write(full, os.path.relpath(full, staging))
PYEOF
zip_box="$cores_dir/synth-box.zip"
rm -f "$zip_box"
python3 - "$staging_box" "$zip_box" <<'PYEOF'
import os, sys, zipfile
staging, zip_path = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(staging):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            z.write(full, os.path.relpath(full, staging))
PYEOF
for cache in "$minihawk_root"/build/CoreCache/synth-native-* "$minihawk_root"/build/CoreCache/synth-sharp-* "$minihawk_root"/build/CoreCache/synth-box-*; do
	[ -d "$cache" ] && rm -rf "$cache" || true
done
echo "packaged -> $zip_path"
echo "packaged -> $zip_sharp"
echo "packaged -> $zip_box"
