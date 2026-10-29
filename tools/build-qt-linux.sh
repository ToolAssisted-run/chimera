#!/bin/sh
# Builds Qt for Linux, for the CPU it runs on (x86-64 or aarch64), for the
# window Chimera ships (source/ui), as tools/build-qt-windows.sh builds the
# Windows one: the same four modules, without Qt's network module - Chimera
# has no network code, and QML
# without it cannot load anything from the network. Qt's prebuilt Linux
# libraries (aqt) have it, so they serve only to build and test.
#
#   tools/build-qt-linux.sh <folder of source archives> <install folder>
#
# The source folder holds qtbase.tar.xz, qtshadertools.tar.xz,
# qtdeclarative.tar.xz and qtsvg.tar.xz (tools/fetch-qt-source.sh). Qt is
# relocatable: installed beside Chimera (lib/, plugins/, qml/ under one
# folder) it finds its plugins and QML from where its own libraries are.
# It draws on X11 (xcb), and off screen for --headless; Wayland desktops run
# it through XWayland.
set -eu
src=$1 prefix=$2
work=${QT_WORK:-$src/build-linux}
mkdir -p "$work"

for m in qtbase qtshadertools qtdeclarative qtsvg; do
	if [ ! -d "$work/$m-src" ]; then
		mkdir -p "$work/$m-src"
		tar -xJf "$src/$m.tar.xz" -C "$work/$m-src" --strip-components=1
		# Chimera's fixes to Qt, applied once as the source is unpacked
		for p in "$(dirname "$0")"/qt-patches/"$m"-*.patch; do
			[ -e "$p" ] && patch -d "$work/$m-src" -p1 < "$p"
		done
	fi
done

step() { echo "=== $1 $(date -u +%H:%M:%S)"; }

step qtbase
cmake -G Ninja -S "$work/qtbase-src" -B "$work/qtbase" \
	-DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release \
	-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_BENCHMARKS=OFF \
	-DFEATURE_network=OFF -DFEATURE_dbus=OFF -DFEATURE_sql=OFF \
	-DFEATURE_printsupport=OFF -DFEATURE_widgets=OFF -DFEATURE_testlib=OFF \
	-DFEATURE_icu=OFF -DFEATURE_glib=OFF \
	-DFEATURE_xcb=ON -DFEATURE_xkbcommon=ON -DFEATURE_fontconfig=ON \
	-DFEATURE_relocatable=ON
cmake --build "$work/qtbase" --parallel
cmake --install "$work/qtbase"

for m in qtshadertools qtdeclarative qtsvg; do
	step "$m"
	"$prefix/bin/qt-cmake" -G Ninja -S "$work/$m-src" -B "$work/$m" \
		-DCMAKE_BUILD_TYPE=Release -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
	cmake --build "$work/$m" --parallel
	cmake --install "$work/$m"
done
step done
