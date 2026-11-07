#!/bin/bash
# Assembles a ready-to-run Chimera bundle: the window, its libraries and the
# licences.
#
# It carries NO CORES, and Chimera downloads none (docs/core-manager.md). They
# are other repositories that build, package and publish themselves; a user
# downloads a core's package from its project, or builds it, and puts it in the
# bundle's cores/ folder, which ships empty but for a README.txt saying so.
#
# What is lost with them is "one chimera commit pins one exact bundle". What
# replaces it: a movie cites the exact core package that recorded it, by
# version and SHA-1, which is what somebody replaying it has to find.
#
# Usage: tools/build-bundle.sh --platform windows|linux --out <dir> --qt <qt>
#                              --qt-source <folder> [--host-qt <qt>]
#
#   --qt         the Qt 6.12 the window is built with for that platform,
#                built without its network module (tools/build-qt-linux.sh,
#                tools/build-qt-windows.sh)
#   --qt-source  Qt's source archives (tools/fetch-qt-source.sh): the
#                licence texts Qt ships under go in the bundle from them
#   --host-qt    Windows only: a Linux Qt of the same version, whose tools
#                (moc, rcc) run during the build
#   --cpu        Windows only: x64 (the default, mingw-w64's GCC) or arm64
#                (llvm-mingw's clang, its bin/ on PATH); --qt is that CPU's Qt
set -eu

root="$(cd "$(dirname "$0")/.." && pwd)"
platform="" out="" qt="" qt_source="" host_qt="" cpu="x64"
while [ $# -gt 0 ]; do
	case "$1" in
		--platform) platform="$2"; shift 2 ;;
		--out) out="$2"; shift 2 ;;
		--qt) qt="$2"; shift 2 ;;
		--qt-source) qt_source="$2"; shift 2 ;;
		--host-qt) host_qt="$2"; shift 2 ;;
		--cpu) cpu="$2"; shift 2 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ -n "$platform" ] && [ -n "$out" ] && [ -n "$qt" ] && [ -n "$qt_source" ] \
	|| { echo "usage: build-bundle.sh --platform windows|linux --out <dir> --qt <qt> --qt-source <folder> [--host-qt <qt>]" >&2; exit 2; }
case "$platform" in
	linux) ;;
	windows)
		[ -n "$host_qt" ] || { echo "--host-qt is needed for a Windows build" >&2; exit 2; }
		case "$cpu" in x64|arm64) ;; *) echo "--cpu is x64 or arm64, not $cpu" >&2; exit 2 ;; esac
		;;
	*) echo "platform must be windows or linux" >&2; exit 2 ;;
esac

say() { printf "\n== %s\n" "$1"; }

# Every core package builds its guest against a miniBox guest kit; default it to
# the submodule the frontend itself is built against, or a stale sibling clone
# silently produces a core.wbx built against a different ABI.
export MINIBOX_DIR="${MINIBOX_DIR:-$root/extern/chimera-common-minibox}"

rm -rf "$out"
if [ "$platform" = linux ]; then
	# a build of its own, against the Qt that ships (the everyday build uses another)
	native_dir="$root/build/meson-linux-release"
	prefix="$root/build/release-linux"
	export PATH="$qt/libexec:$qt/bin:$PATH" PKG_CONFIG_PATH="$qt/lib/pkgconfig"
	say "the engine and the window (linux)"
	[ -f "$native_dir/build.ninja" ] || meson setup "$native_dir" --prefix "$prefix" --libdir dll -Dqt_ui=enabled
	meson compile -C "$native_dir"
	meson install -C "$native_dir"
	# ffmpeg is part of the bundle, not something the frontend asks a person to
	# go and find the first time they encode a video; built here, with only
	# what Encode Video uses (issue 492)
	say "ffmpeg"
	"$root/tools/build-ffmpeg.sh" linux "$prefix/dll"
	say "staging into $out"
	python3 "$root/tools/stage-qt-linux.py" "$prefix/dll" "$qt" "$out"
	program="chimera"
	suffix=""
else
	# each CPU's own build folder and cross file (the x64 ones keep their names)
	if [ "$cpu" = arm64 ]; then
		native_dir="$root/build/meson-windows-qt-arm64" cross="mingw-w64-qt-aarch64.ini" tag="-arm64"
	else
		native_dir="$root/build/meson-windows-qt" cross="mingw-w64-qt.ini" tag=""
	fi
	export PATH="$host_qt/libexec:$host_qt/bin:$PATH"
	say "the engine and the window (windows $cpu)"
	if [ ! -f "$native_dir/build.ninja" ]; then
		# pkg-config is pointed at the Windows Qt by a second cross file
		mkdir -p "$root/build"
		printf "[properties]\npkg_config_libdir = ['%s/lib/pkgconfig']\n" "$qt" > "$root/build/qt-windows$tag-pkgconfig.ini"
		meson setup "$native_dir" --prefix "$root/build/qt-windows$tag" --libdir dll \
			--cross-file "$root/extern/meson/$cross" --cross-file "$root/build/qt-windows$tag-pkgconfig.ini" -Dqt_ui=enabled
	fi
	meson compile -C "$native_dir"
	say "ffmpeg"
	"$root/tools/build-ffmpeg.sh" windows "$root/build/ffmpeg-windows$tag" "$cpu"
	say "staging into $out"
	python3 "$root/tools/stage-qt-windows.py" --cpu "$cpu" "$native_dir" "$native_dir" "$qt" "$out" "$root/build/ffmpeg-windows$tag/ffmpeg.exe"
	cp "$root/build/ffmpeg-windows$tag/ffmpeg-LICENSE.txt" "$out/bin/"
	program="chimera.exe"
	suffix=".exe"
fi

# No list of cores goes in: Chimera downloads nothing and knows of no core it
# has not been given (docs/core-manager.md). cores/ ships empty, and what a user
# puts in it is what there is.

say "licences"
# What the bundle may be used for, and where each part's terms and source are:
# Chimera's own (MIT), Qt's (LGPL 3, its source and licence texts named), and
# ffmpeg's (a separate GPL program).
python3 "$root/tools/bundle-licenses.py" "$out" --chimera-root "$root" --qt-source "$qt_source"

say "build stamp"
# Every "it failed" report is only as useful as knowing WHICH build failed. This
# names the exact commits and the exact files' hashes.
{
	# a Linux bundle is for the CPU it was built on; a Windows one for --cpu
	if [ "$platform" = linux ]; then
		case "$(uname -m)" in aarch64|arm64) bundle_cpu=aarch64 ;; *) bundle_cpu=x86_64 ;; esac
	else
		case "$cpu" in arm64) bundle_cpu=aarch64 ;; *) bundle_cpu=x86_64 ;; esac
	fi
	printf "Chimera %s bundle (%s)\n" "$platform" "$bundle_cpu"
	printf "chimera:   %s %s\n" \
		"$(git -C "$root" rev-parse HEAD)" \
		"$(git -C "$root" log -1 --format=%s | cut -c1-60)"
	printf "\nguest kit:\n"
	git -C "$root" submodule status extern/chimera-common-minibox 2>/dev/null | sed 's/^/  /'
	printf "\nfiles (sha1):\n"
	( cd "$out" && sha1sum "$program" | sed 's/^/  /' )
	printf "\ncores: none, and Chimera downloads none. Get a core's package from its\n"
	printf "project and put it in cores/; a movie names the exact package that recorded it.\n"
} > "$out/BUILD.txt"
cat "$out/BUILD.txt"

say "verifying"
# A truncated library is indistinguishable from a bug until you check.
bad=0
for f in "$out/$program" "$out"/lib/* "$out"/bin/*; do
	[ -d "$f" ] || [ -s "$f" ] || { echo "EMPTY: $f" >&2; bad=1; }
done
[ -s "$out/LICENSES.md" ] || { echo "MISSING: LICENSES.md" >&2; bad=1; }
[ -s "$out/bin/chimera-run$suffix" ] || { echo "MISSING: bin/chimera-run$suffix" >&2; bad=1; }
[ -s "$out/bin/chimera-mcp$suffix" ] || { echo "MISSING: bin/chimera-mcp$suffix (Tools > MCP Server... names it)" >&2; bad=1; }
[ -s "$out/cores/README.txt" ] || { echo "MISSING: cores/README.txt (the folder a user puts core packages in)" >&2; bad=1; }
shipped=$(find "$root/themes" -name '*.chimeraTheme.json' | wc -l)
bundled=$(find "$out/themes" -name '*.chimeraTheme.json' | wc -l)
[ "$shipped" -gt 0 ] && [ "$bundled" -eq "$shipped" ] || { echo "MISSING: themes/ has $bundled of the $shipped themes in the repository" >&2; bad=1; }
shipped=$(find "$root/languages" -name '*.json' | wc -l)
bundled=$(find "$out/lib/languages" -name '*.json' | wc -l)
[ "$shipped" -gt 0 ] && [ "$bundled" -eq "$shipped" ] || { echo "MISSING: lib/languages has $bundled of the $shipped language tables in the repository" >&2; bad=1; }
# bin/chimera-run reads Chimera's texts from lib/languages: what it says is a
# sentence, not the text's key (a tool's answer was "import.done" when it did not)
if [ "$platform" = linux ]; then
	said=$("$out/bin/chimera-run" --run-tool none "$out/no-such.chimeraCore" 2>&1 || true)
	case "$said" in *engine.not_a_package*) echo "UNTRANSLATED: bin/chimera-run says \"$said\"" >&2; bad=1 ;; esac
fi
# libxkbcommon-x11 works only with the libxkbcommon of its own version: one
# without the other crashed the window at start on newer systems (#533)
if [ "$platform" = linux ] && [ -e "$out/lib/libxkbcommon-x11.so.0" ] && [ ! -e "$out/lib/libxkbcommon.so.0" ]; then
	echo "MISSING: lib/libxkbcommon.so.0 (lib/libxkbcommon-x11.so.0 needs the one of its own version)" >&2; bad=1
fi
# every folder Chimera puts in the bundle is named in lower case (the owner,
# 2026-11-07); Qt's QML modules under qml/ keep the names Qt imports them by
for f in "$out"/*/; do
	case "$(basename "$f")" in *[A-Z]*) echo "UPPER CASE: $(basename "$f")/ (the bundle's folders are lower case)" >&2; bad=1 ;; esac
done
[ ! -e "$out/official-cores.json" ] || { echo "PRESENT: official-cores.json (the bundle carries no list of cores)" >&2; bad=1; }
# Chimera reaches for nothing over the network: Qt's network module is not built
if find "$out" -iname '*Qt6Network*' | grep -q .; then echo "PRESENT: Qt's network module" >&2; bad=1; fi
[ "$bad" -eq 0 ] || { echo "  the bundle is NOT clean" >&2; exit 1; }
printf "  %s in %s\n" "$(du -sh "$out" | cut -f1)" "$out"
