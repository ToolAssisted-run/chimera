#!/bin/sh
# Builds Chimera on Linux for a CI run - a core's tests or Chimera's own: Qt
# 6.12 from Qt's own archives (with aqt), then the engine, chimera-run, the
# window and the package contract test, installed into build/dll as
# AGENTS.md's build is. After it:
#
#   build/meson-linux/source/ui/chimera         the window; --headless runs it with no display
#   build/meson-linux/source/ui/test-packages   the contract test (CHIMERA_CORES_DIR=<folder>)
#   build/dll/chimera-run                       the engine's runner
#
#   tools/ci-chimera.sh [<qt folder>]
#
# The Qt folder defaults to $HOME/qt; one that already holds Qt 6.12 (a CI
# cache) is used as it is. Qt is downloaded here, by the CI run: Chimera
# itself downloads nothing.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
qt="${1:-$HOME/qt}"
version=6.12.0

sudo apt-get update
sudo apt-get install -y --no-install-recommends \
	meson ninja-build build-essential cmake nasm pkg-config python3 python3-venv \
	libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev \
	libxkbcommon0 libfontconfig1 libfreetype6 libdbus-1-3

if [ ! -d "$qt/$version/gcc_64" ]; then
	python3 -m venv "$qt/aqt-venv"
	"$qt/aqt-venv/bin/pip" install --quiet aqtinstall==3.3.0
	"$qt/aqt-venv/bin/aqt" install-qt linux desktop "$version" linux_gcc_64 -m qtshadertools -O "$qt"
fi
export PATH="$qt/$version/gcc_64/libexec:$qt/$version/gcc_64/bin:$PATH"
export PKG_CONFIG_PATH="$qt/$version/gcc_64/lib/pkgconfig"

cd "$root"
git submodule update --init --recursive extern
[ -f build/meson-linux/build.ninja ] || meson setup build/meson-linux --prefix "$root/build" --libdir dll -Dqt_ui=enabled
meson compile -C build/meson-linux
meson install -C build/meson-linux
