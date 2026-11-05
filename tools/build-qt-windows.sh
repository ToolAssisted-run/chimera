#!/bin/sh
# Builds Qt for Windows on Linux, for the Qt window (source/ui), with the
# compiler Chimera's Windows build of that CPU uses: mingw-w64's GCC for x64,
# llvm-mingw's clang for arm64 (its bin/ folder on PATH). The Linux Qt of the
# same version runs its tools during the build (moc, qsb, qmlcachegen...), so
# it is given as the host Qt.
#
#   tools/build-qt-windows.sh <host qt> <folder of source archives> <install folder> [x64|arm64]
#
# QT_JOBS caps the build's parallel jobs (default: every core).
#
# The source folder holds qtbase.tar.xz, qtshadertools.tar.xz,
# qtdeclarative.tar.xz and qtsvg.tar.xz (the "submodules" archives of the
# release). Qt is built without its network module: Chimera has no network
# code, and QML without it cannot load anything from the network. It has its
# test library, for the window's tests on Windows (CI); no program the bundle
# carries uses it, so the bundle does not carry it.
#
# Qt needs the compiler's "posix" thread model (x86_64-w64-mingw32-g++-posix):
# built with the "win32" one, which the rest of Chimera's Windows build uses,
# a Qt thread crashed in libstdc++-6.dll as it ended at exit.
set -eu
host=$1 src=$2 prefix=$3 cpu=${4:-x64}
case "$cpu" in
	x64)
		threads=${QT_MINGW_THREADS:-posix}
		work=${QT_WORK:-$src/build-mingw-$threads}
		;;
	arm64)
		# llvm-mingw has one thread model: winpthreads, with libc++
		cc=$(command -v aarch64-w64-mingw32-clang) \
			|| { echo "build-qt-windows: no aarch64-w64-mingw32-clang on PATH (llvm-mingw's bin/)" >&2; exit 1; }
		work=${QT_WORK:-$src/build-llvm-mingw-arm64}
		;;
	*) echo "build-qt-windows: the CPU is x64 or arm64, not $cpu" >&2; exit 2 ;;
esac
mkdir -p "$work"

tc="$work/mingw-w64.cmake"
if [ "$cpu" = arm64 ]; then
cat > "$tc" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)
set(CMAKE_C_COMPILER aarch64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER aarch64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER aarch64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH $(dirname "$(dirname "$cc")")/aarch64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
else
cat > "$tc" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc-$threads)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++-$threads)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF
fi

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
	-DCMAKE_TOOLCHAIN_FILE="$tc" -DQT_HOST_PATH="$host" \
	-DCMAKE_INSTALL_PREFIX="$prefix" -DCMAKE_BUILD_TYPE=Release \
	-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DQT_BUILD_BENCHMARKS=OFF \
	-DFEATURE_network=OFF -DFEATURE_dbus=OFF -DFEATURE_sql=OFF \
	-DFEATURE_printsupport=OFF -DFEATURE_widgets=OFF
cmake --build "$work/qtbase" --parallel ${QT_JOBS:-}
cmake --install "$work/qtbase"

for m in qtshadertools qtdeclarative qtsvg; do
	step "$m"
	"$prefix/bin/qt-cmake" -G Ninja -S "$work/$m-src" -B "$work/$m" \
		-DQT_HOST_PATH="$host" -DCMAKE_BUILD_TYPE=Release \
		-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF
	cmake --build "$work/$m" --parallel ${QT_JOBS:-}
	cmake --install "$work/$m"
done
step done
