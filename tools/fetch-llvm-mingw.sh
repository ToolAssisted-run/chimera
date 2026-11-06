#!/bin/sh
# Fetches llvm-mingw, the compiler Chimera's Windows on arm64 build uses
# (clang and libc++ for aarch64-w64-mingw32; mingw-w64's GCC has no arm64
# Windows target), and unpacks it into <folder>. Prints its bin/ folder, which
# goes on PATH for the build (extern/meson/mingw-w64-aarch64.ini names its
# programs without a folder).
#
#   tools/fetch-llvm-mingw.sh <folder>
#
# The release is pinned, and the archive checked by its SHA256 (GitHub's own
# digest of the release asset). It is the build of llvm-mingw for the Linux
# machine it runs on, x86-64 or arm64: the arm64 Windows build can be made on
# either (CI makes it on an arm64 runner, where its test core is built too).
set -eu
dest="${1:?usage: fetch-llvm-mingw.sh <folder>}"
release=20261020
case "$(uname -m)" in
	x86_64)
		name="llvm-mingw-$release-ucrt-ubuntu-22.04-x86_64"
		sha256=55064b60b991a1e1e8090b8c2f84e82b22cfe5903d12c159d65064cf5e8ae335
		;;
	aarch64|arm64)
		name="llvm-mingw-$release-ucrt-ubuntu-22.04-aarch64"
		sha256=ca16bb0f4f485876496fa46ad8d353c525eba57d0cf9e9e49ec1738fe4810151
		;;
	*) echo "fetch-llvm-mingw: llvm-mingw runs on x86-64 and arm64 Linux, not $(uname -m)" >&2; exit 2 ;;
esac
url="https://github.com/mstorsjo/llvm-mingw/releases/download/$release/$name.tar.xz"

mkdir -p "$dest"
if [ ! -x "$dest/$name/bin/aarch64-w64-mingw32-clang" ]; then
	archive="$dest/$name.tar.xz"
	if ! echo "$sha256  $archive" | sha256sum --check --status 2>/dev/null; then
		curl -sSL --fail --retry 3 --retry-delay 5 -o "$archive" "$url"
		echo "$sha256  $archive" | sha256sum --check --status \
			|| { echo "fetch-llvm-mingw: $archive is not the pinned release (sha256 mismatch)" >&2; exit 1; }
	fi
	tar -xJf "$archive" -C "$dest"
fi
echo "$(cd "$dest/$name/bin" && pwd)"
