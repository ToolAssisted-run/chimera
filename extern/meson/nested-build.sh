#!/bin/sh
# Drives the native deps whose upstreams use their own build system, as a
# single meson custom_target command:
#   nested-build.sh meson <srcdir> <builddir> <output> <cross-arg|''> [extra -D args...]
#   nested-build.sh cmake <srcdir> <builddir> <output> <toolchain|''> [extra -D args...]
# <output> is the file meson expects this target to produce; the built library
# is copied there.
set -e
kind="$1"; src="$2"; bld="$3"; out="$4"; aux="$5"
shift 5 || true
# meson passes builddir-relative paths; tools below cd elsewhere, so absolutize
# (by hand: macOS's realpath has no -m, and neither path need exist yet)
abspath() { case "$1" in /*) echo "$1" ;; *) echo "$PWD/$1" ;; esac; }
bld="$(abspath "$bld")"
out="$(abspath "$out")"

case "$kind" in
meson)
	if [ ! -f "$bld/build.ninja" ]; then
		if [ -n "$aux" ]; then
			meson setup "$bld" "$src" --buildtype release "$aux" "$@"
		else
			meson setup "$bld" "$src" --buildtype release "$@"
		fi
	fi
	meson compile -C "$bld"
	# prefer the exact name; else version-infixed variants (mingw: libzstd-1.dll;
	# linux: libzstd.so.1.5.7). Never import libs or defs.
	base="$(basename "$out")"
	stem="$(basename "$out" | sed 's/\.[^.]*$//')"
	# macOS: libzstd.1.5.7.dylib, with libzstd.dylib a link to it (-L: a link
	# to a file counts as the file)
	lib="$(find -L "$bld" -maxdepth 2 -type f \
		\( -name "$base" -o -name "$stem-*.dll" -o -name "$stem.so.*" -o -name "$stem-*.so.*" -o -name "$stem.*.dylib" \) \
		! -name '*.a' ! -name '*.d' ! -name '*.def' | head -1)"
	;;
cmake)
	if [ ! -f "$bld/CMakeCache.txt" ]; then
		if [ -n "$aux" ]; then
			cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_TOOLCHAIN_FILE=$aux" "$@"
		else
			cmake -S "$src" -B "$bld" -G Ninja -DCMAKE_BUILD_TYPE=Release "$@"
		fi
	fi
	cmake --build "$bld"
	base="$(basename "$out")"
	lib="$(find -L "$bld" -name "$base" -type f | head -1)"
	if [ -z "$lib" ]; then
		# versioned .so (e.g. libopenal.so.1.24.3) or lib-prefixed variant
		stem="$(basename "$out" | sed 's/\.[^.]*$//')"
		lib="$(find "$bld" \( -name "$stem.so.*" -o -name "$stem.*" \) -type f | head -1)"
	fi
	;;
*)
	echo "unknown kind: $kind" >&2; exit 1 ;;
esac

if [ -z "$lib" ] || [ ! -f "$lib" ]; then
	echo "nested-build: no library found for $out in $bld" >&2
	exit 1
fi
# only when it changed: the target is asked at every build (meson.build)
cmp -s "$lib" "$out" 2>/dev/null || cp "$lib" "$out"
