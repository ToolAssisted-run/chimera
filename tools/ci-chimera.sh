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
	meson ninja-build build-essential cmake pkg-config python3 python3-venv \
	libgl1-mesa-dev libegl-dev libx11-dev libxext-dev libasound2-dev \
	libxkbcommon0 libfontconfig1 libfreetype6 libdbus-1-3

# Qt's archives name the CPU twice: the host the download is for, and the
# build it holds (whose folder name drops the linux_)
case "$(uname -m)" in
	x86_64) qt_host=linux qt_arch=linux_gcc_64 ;;
	aarch64) qt_host=linux_arm64 qt_arch=linux_gcc_arm64 ;;
	*) echo "ci-chimera.sh: Chimera builds on x86-64 and aarch64 only, not $(uname -m)" >&2; exit 1 ;;
esac
qt_dir="$qt/$version/${qt_arch#linux_}"
if [ ! -d "$qt_dir" ]; then
	python3 -m venv "$qt/aqt-venv"
	"$qt/aqt-venv/bin/pip" install --quiet aqtinstall==3.3.0
	"$qt/aqt-venv/bin/aqt" install-qt "$qt_host" desktop "$version" "$qt_arch" -m qtshadertools -O "$qt"
	# aqt points Qt's .pc files at where it was installed only for the host
	# "linux": an arm64 one still names the folder Qt was built in
	sed -i "s|^prefix=/home/qt/work/install\$|prefix=$qt_dir|" "$qt_dir"/lib/pkgconfig/*.pc
fi
export PATH="$qt_dir/libexec:$qt_dir/bin:$PATH"
export PKG_CONFIG_PATH="$qt_dir/lib/pkgconfig"

cd "$root"
git submodule update --init --recursive extern
[ -f build/meson-linux/build.ninja ] || meson setup build/meson-linux --prefix "$root/build" --libdir dll -Dqt_ui=enabled
meson compile -C build/meson-linux
meson install -C build/meson-linux
