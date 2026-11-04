#!/bin/bash
# Makes Chimera.app from an x86-64 macOS build: the window, its libraries, Qt
# (the frameworks, plugins and QML the window uses) and ffmpeg, in one folder
# that runs on its own, on an Intel Mac or, under Rosetta 2, on an Apple silicon
# one. Like the other bundles it carries NO CORES (docs/core-manager.md): they
# go in ~/Library/Application Support/Chimera/Cores, where Chimera looks.
#
#   tools/build-app-macos.sh --dll <installed dll dir> --qt <Qt prefix> --out <Chimera.app> [--icon <Chimera.icon>]
#
#   --dll  meson's install folder for the macOS build (build/x86_64-macos/dll):
#          the window, chimera-run, chimera-mcp, Chimera's libraries and, when
#          `tools/fetch-ffmpeg.sh macos <that folder>` put it there, ffmpeg
#   --qt   the Qt 6.12 the window was built with (a prefix holding lib/, bin/macdeployqt)
#   --out  the app to make; it is replaced if it is there
#   --icon an Icon Composer icon (a Chimera.icon folder; docs/Chimera.icon is the
#          default), compiled with actool into the app's light and dark icon. It
#          needs Xcode 26 or later; with an older one, or none, the icon is drawn
#          from docs/icon.svg instead, with no dark version
#
# Layout, which the program finds its neighbours by (Contents/MacOS is the
# program's own folder, as the Linux bundle's top folder is):
#
#   Contents/MacOS/chimera  bin/  Themes/
#   Contents/Frameworks/    Chimera's libraries, languages/, and Qt's frameworks
#   Contents/PlugIns/  Contents/Resources/qml  Contents/Resources/Chimera.icns
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
dll="" qt="" out="" icon="$root/docs/Chimera.icon"
while [ $# -gt 0 ]; do
	case "$1" in
		--dll) dll="$2"; shift 2 ;;
		--qt) qt="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--icon) icon="$2"; shift 2 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ -n "$dll" ] && [ -n "$qt" ] && [ -n "$out" ] \
	|| { echo "usage: build-app-macos.sh --dll <dll dir> --qt <qt prefix> --out <Chimera.app>" >&2; exit 2; }
dll="$(cd "$dll" && pwd)"
qt="$(cd "$qt" && pwd)"
[ -x "$qt/bin/macdeployqt" ] || { echo "build-app-macos: $qt/bin/macdeployqt is not there" >&2; exit 1; }
[ -x "$dll/chimera" ] || { echo "build-app-macos: $dll/chimera is not there (meson install first)" >&2; exit 1; }
case "$(file -b "$dll/chimera")" in *x86_64*) ;; *) echo "build-app-macos: $dll/chimera is not an x86-64 program" >&2; exit 1 ;; esac

say() { printf "\n== %s\n" "$1"; }

# ---- the layout ----------------------------------------------------------------
say "staging $out"
rm -rf "$out"
app="$out/Contents"
mkdir -p "$app/MacOS/bin" "$app/Frameworks" "$app/Resources"

cp "$dll/chimera" "$app/MacOS/"
# a movie import and a compile session ask the core in a process of their own,
# and Tools > MCP Server names the program an agent's program starts
cp "$dll/chimera-run" "$dll/chimera-mcp" "$app/MacOS/bin/"
# Encode Video's, as the other bundles ship it
for f in ffmpeg ffmpeg-LICENSE.txt; do
	[ -f "$dll/$f" ] && cp "$dll/$f" "$app/MacOS/bin/"
done
[ -x "$app/MacOS/bin/ffmpeg" ] || echo "  (no ffmpeg in $dll: Encode Video will say so)"
# Chimera's libraries, with the language tables the engine reads beside itself
for f in "$dll"/*.dylib; do cp "$f" "$app/Frameworks/"; done
cp -R "$dll/languages" "$app/Frameworks/languages"
cp -R "$root/Themes" "$app/MacOS/Themes"
# the folders a person fills - Cores, Firmware, Movies, settings.json - are in
# ~/Library/Application Support/Chimera, not in the app, which is signed
cp "$root/tools/cores-README.txt" "$app/Resources/Cores-README.txt"

# ---- where each program finds the libraries ------------------------------------
# meson installs them under their full paths; in the app they are found along
# an rpath, and the build's own Qt path comes out of it
say "library paths"
relink() { # <file> <rpath to Frameworks from it>
	local f="$1" rp="$2" dep
	while read -r dep; do
		case "$dep" in
			"$dll"/*.dylib) install_name_tool -change "$dep" "@rpath/$(basename "$dep")" "$f" ;;
		esac
	done < <(otool -L "$f" | tail -n +2 | awk '{print $1}')
	while read -r old; do
		install_name_tool -delete_rpath "$old" "$f" 2> /dev/null || true
	done < <(otool -l "$f" | awk '/LC_RPATH/{r=1} r&&/path /{print $2; r=0}')
	install_name_tool -add_rpath "$rp" "$f"
}
relink "$app/MacOS/chimera" "@executable_path/../Frameworks"
for f in "$app"/MacOS/bin/chimera-run "$app"/MacOS/bin/chimera-mcp; do
	relink "$f" "@executable_path/../../Frameworks"
done
for f in "$app"/Frameworks/*.dylib; do
	install_name_tool -id "@rpath/$(basename "$f")" "$f"
	relink "$f" "@loader_path"
done

# ---- Info.plist and the icon ---------------------------------------------------
say "Info.plist and icon"
version="$(sed -n "s/.*version: *'\([^']*\)'.*/\1/p" "$root/meson.build" | head -1)"
commit="$(git -C "$root" rev-parse --short=12 HEAD 2> /dev/null || echo unknown)"
cat > "$app/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key><string>Chimera</string>
	<key>CFBundleDisplayName</key><string>Chimera</string>
	<key>CFBundleIdentifier</key><string>run.toolassisted.chimera</string>
	<key>CFBundleExecutable</key><string>chimera</string>
	<key>CFBundleIconFile</key><string>Chimera</string>
	<key>CFBundlePackageType</key><string>APPL</string>
	<key>CFBundleShortVersionString</key><string>${version:-0.1}</string>
	<key>CFBundleVersion</key><string>${commit}</string>
	<key>LSMinimumSystemVersion</key><string>14.4</string>
	<key>NSHighResolutionCapable</key><true/>
	<key>NSPrincipalClass</key><string>NSApplication</string>
	<key>LSApplicationCategoryType</key><string>public.app-category.games</string>
</dict>
</plist>
EOF
icons="$(mktemp -d)"
trap 'rm -rf "$icons"' EXIT
icon_name=""
if [ -d "$icon" ]; then
	# the icon as Icon Composer made it: Assets.car (light and dark) and an .icns
	# for what is older; the plist keys it wants come out of the compile
	if xcrun actool "$icon" --compile "$app/Resources" --output-format human-readable-text \
		--notices --warnings --errors --output-partial-info-plist "$icons/icon.plist" \
		--app-icon "$(basename "$icon" .icon)" --include-all-app-icons --enable-on-demand-resources NO \
		--development-region en --target-device mac --minimum-deployment-target 14.4 --platform macosx \
		> "$icons/actool.log" 2>&1 && [ -f "$app/Resources/Assets.car" ]; then
		icon_name="$(basename "$icon" .icon)"
		echo "  icon: $icon"
	else
		echo "  (actool could not compile $icon - it needs Xcode 26 or later; drawing the icon from docs/icon.svg)" >&2
		sed 's/^/    /' "$icons/actool.log" | tail -n 5 >&2
	fi
fi
if [ -z "$icon_name" ]; then
	mkdir "$icons/Chimera.iconset"
	# the mark, drawn at every size from the vector
	qlmanage -t -s 1024 -o "$icons" "$root/docs/icon.svg" > /dev/null 2>&1
	if [ -f "$icons/icon.svg.png" ]; then
		for size in 16 32 128 256 512; do
			sips -z $size $size "$icons/icon.svg.png" --out "$icons/Chimera.iconset/icon_${size}x${size}.png" > /dev/null
			sips -z $((size * 2)) $((size * 2)) "$icons/icon.svg.png" --out "$icons/Chimera.iconset/icon_${size}x${size}@2x.png" > /dev/null
		done
		iconutil -c icns "$icons/Chimera.iconset" -o "$app/Resources/Chimera.icns"
	else
		echo "  (the icon could not be drawn; the app has the generic one)"
	fi
fi
# the plist keys: the name of the icon, when it is a compiled one
if [ -n "$icon_name" ]; then
	/usr/libexec/PlistBuddy -c "Add :CFBundleIconName string $icon_name" "$app/Info.plist"
fi

# ---- Qt ------------------------------------------------------------------------
say "Qt (macdeployqt)"
"$qt/bin/macdeployqt" "$out" -qmldir="$root/source/ui/qt" -no-codesign -appstore-compliant -verbose=1 2>&1 | grep -v -e "^$" -e libmimerapi | tail -n 15 || true
# Qt's SQL drivers come with its QML modules and need database libraries a Mac
# need not have; the window uses no Qt SQL
rm -rf "$app/PlugIns/sqldrivers"
# Qt's frameworks are one build for both processors; only the half this app is
# is kept
say "keeping the x86-64 half of Qt"
before="$(du -sk "$out" | cut -f1)"
while IFS= read -r f; do
	if lipo -info "$f" 2> /dev/null | grep -q "Architectures in the fat file"; then
		lipo -thin x86_64 "$f" -output "$f.thin" && mv "$f.thin" "$f"
	fi
done < <(find "$app" -type f \( -name "*.dylib" -o -perm -u+x -o -path "*.framework/Versions/*/*" \) ! -name "*.plist" ! -name "*.sh" ! -name "*.py" ! -name "*.json" ! -name "*.qml" ! -name "*.txt" 2> /dev/null)
echo "  $((before / 1024)) MiB -> $(($(du -sk "$out" | cut -f1) / 1024)) MiB"

# ---- the licences and the stamp -------------------------------------------------
say "licences and build stamp"
{
	printf "Chimera macOS (x86-64) app\n"
	printf "chimera:   %s %s\n" "$(git -C "$root" rev-parse HEAD)" "$(git -C "$root" log -1 --format=%s | cut -c1-60)"
	printf "\nguest kit:\n"
	git -C "$root" submodule status extern/chimera-common-minibox 2> /dev/null | sed 's/^/  /'
	printf "\nQt: %s (Qt's own download, one build for both processors; the x86-64 half kept)\n" "$(basename "$(dirname "$qt")")"
	printf "\ncores: none, and Chimera downloads none. Get a core's package from its\n"
	printf "project and put it in ~/Library/Application Support/Chimera/Cores; a movie names the\n"
	printf "exact package that recorded it.\n"
} > "$app/Resources/BUILD.txt"
cp "$root/LICENSE" "$app/Resources/LICENSE.txt"

# ---- signing and checking --------------------------------------------------------
# ad hoc: no developer identity, so the app is not notarised; it runs on the
# machine it was made on (or after a right-click, Open, elsewhere)
say "signing (ad hoc)"
codesign --force --deep --sign - "$out" 2>&1 | tail -n 3

say "checking"
bad=0
# nothing may still name this machine's build or Qt folders
while IFS= read -r f; do
	if otool -L "$f" 2> /dev/null | tail -n +2 | grep -q -E "$dll|$qt"; then echo "STILL LINKED TO THE BUILD: $f" >&2; bad=1; fi
	if otool -l "$f" 2> /dev/null | grep -A2 LC_RPATH | grep -q -E "$dll|$qt"; then echo "STILL AN RPATH TO THE BUILD: $f" >&2; bad=1; fi
done < <(find "$app" -type f \( -name "*.dylib" -o -perm -u+x \) -exec sh -c 'file -b "$1" | grep -q Mach-O && echo "$1"' _ {} \;)
[ -s "$app/MacOS/bin/chimera-run" ] || { echo "MISSING: bin/chimera-run" >&2; bad=1; }
[ -s "$app/Frameworks/libchimera.dylib" ] || { echo "MISSING: libchimera.dylib" >&2; bad=1; }
[ -d "$app/Frameworks/languages" ] || { echo "MISSING: languages" >&2; bad=1; }
[ -d "$app/PlugIns/platforms" ] || { echo "MISSING: Qt's platform plugin" >&2; bad=1; }
[ "$bad" -eq 0 ] || exit 1
du -sh "$out" | sed 's/^/size: /'
echo "$out"
