#!/bin/bash
# Puts an ffmpeg binary where a Chimera bundle expects one.
#
# Chimera ships ffmpeg. It used to ask the person to download it the first time
# they encoded anything - a dialog, a URL, a checksum and a version pin, all of
# it standing between someone and a video file, and all of it capable of
# failing on a machine that has no business reaching the internet. The binary is
# part of the build now, so the frontend has nothing to ask.
#
# Usage: ./fetch-ffmpeg.sh <linux|windows> <destination directory>
#
# Everything is pinned and checked twice: the archive by its own SHA256, and the
# binary inside it by the SHA256 the frontend used to demand before it would run
# it. Downloads are cached under build/deps, so this costs nothing after the
# first build.
#
# CHIMERA_FFMPEG_LINUX / CHIMERA_FFMPEG_WINDOWS name a local binary to use
# instead, for a machine that cannot reach the network.
set -eu

platform="${1:?usage: fetch-ffmpeg.sh <linux|windows> <dest dir>}"
dest="${2:?usage: fetch-ffmpeg.sh <linux|windows> <dest dir>}"

root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${CHIMERA_DEPS_DIR:-$root/build/deps}"

# The same static ffmpeg 4.4.1 the frontend used to fetch on its own: half the
# size of a current all-inclusive build, and it still has every encoder the
# canned formats name (utvideo, ffv1 and aac are ffmpeg's own; x264, xvid, vpx,
# theora, vorbis and lame are configured in).
case "$platform" in
	linux)
		archive="ffmpeg-4.4.1-static-linux-x64.7z"
		archive_sha256="a1d47ae867c4211da47a2560905f62d02722b5a601e91ab9c95d574adfc00b06"
		binary_sha256="3ea58083710f63bf920b16c7d5d24ae081e7d731f57a656fed11af0410d4eb48"
		binary="ffmpeg"
		override="${CHIMERA_FFMPEG_LINUX:-}"
		;;
	windows)
		archive="ffmpeg-4.4.1-static-windows-x64.7z"
		archive_sha256="9cdd3cd7b3263e881647b60c1188f4adc197788347b3d7ff62d535f2d6878849"
		binary_sha256="8436760af8f81c95eff92d854a7684e6d3cedb872888420359fc45c8eb2664ac"
		binary="ffmpeg.exe"
		override="${CHIMERA_FFMPEG_WINDOWS:-}"
		;;
	*)
		echo "fetch-ffmpeg: unknown platform '$platform'" >&2
		exit 2
		;;
esac

base="https://github.com/TASEmulators/ffmpeg-binaries/raw/master"

# The build is GPL v3 (--enable-gpl --enable-version3), so the licence has to
# travel with the binary. Taken from FFmpeg's own tree at the exact tag.
licence_url="https://raw.githubusercontent.com/FFmpeg/FFmpeg/n4.4.1/COPYING.GPLv3"
licence_sha256="8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903"

# The 7-Zip that unpacks it, if the machine has none. A static binary in a
# tar.xz, so bootstrapping it needs only tar - which means a build machine needs
# nothing installed for any of this.
sevenzip_archive="7z2301-linux-x64.tar.xz"
sevenzip_url="https://www.7-zip.org/a/$sevenzip_archive"
sevenzip_sha256="23babcab045b78016e443f862363e4ab63c77d75bc715c0b3463f6134cbcf318"

mkdir -p "$dest" "$cache"

verify() { # <file> <sha256>
	[ -f "$1" ] || return 1
	echo "$2  $1" | sha256sum --check --status
}

fetch() { # <url> <path> <sha256> <what>
	if verify "$2" "$3"; then return 0; fi
	echo "  downloading $4"
	rm -f "$2"
	curl -sSL --fail --max-time 1800 -o "$2" "$1" || {
		echo "fetch-ffmpeg: could not download $1" >&2
		echo "  Put it at $2 by hand, or point CHIMERA_FFMPEG_${platform^^} at an ffmpeg binary." >&2
		return 1
	}
	verify "$2" "$3" || {
		echo "fetch-ffmpeg: $4 is not what was pinned (sha256 mismatch)" >&2
		return 1
	}
}

# ---- the unpacker -----------------------------------------------------------
sevenzip=""
for candidate in 7zz 7z 7za; do
	if command -v "$candidate" > /dev/null 2>&1; then sevenzip="$candidate"; break; fi
done
if [ -z "$sevenzip" ]; then
	sevenzip="$cache/7zz"
	if [ ! -x "$sevenzip" ]; then
		fetch "$sevenzip_url" "$cache/$sevenzip_archive" "$sevenzip_sha256" "7-Zip"
		tar -xJf "$cache/$sevenzip_archive" -C "$cache" 7zz
		chmod +x "$sevenzip"
	fi
fi

# ---- the binary -------------------------------------------------------------
if [ -n "$override" ]; then
	[ -f "$override" ] || { echo "fetch-ffmpeg: $override is not a file" >&2; exit 1; }
	cp "$override" "$dest/$binary"
	chmod +x "$dest/$binary"
	echo "  ffmpeg from $override"
	exit 0
fi

fetch "$base/$archive" "$cache/$archive" "$archive_sha256" "$archive"
fetch "$licence_url" "$cache/COPYING.GPLv3" "$licence_sha256" "the GPL v3 text"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
"$sevenzip" x -y "-o$tmp" "$cache/$archive" > /dev/null

[ -f "$tmp/$binary" ] || { echo "fetch-ffmpeg: $archive did not contain $binary" >&2; exit 1; }
verify "$tmp/$binary" "$binary_sha256" || {
	echo "fetch-ffmpeg: the ffmpeg inside $archive is not the pinned build" >&2
	exit 1
}

cp "$tmp/$binary" "$dest/$binary"
chmod +x "$dest/$binary"

{
	printf "ffmpeg 4.4.1-static (%s)\n\n" "$platform"
	printf "Shipped as a separate program, which Chimera runs and talks to over a pipe.\n"
	printf "It is not linked into Chimera and is not modified. Taken unaltered from:\n"
	printf "  %s/%s\n" "$base" "$archive"
	printf "  sha256 of the archive: %s\n" "$archive_sha256"
	printf "  sha256 of this binary: %s\n\n" "$binary_sha256"
	printf "Built by John Van Sickle <https://johnvansickle.com/ffmpeg/> with\n"
	printf "%s, so its terms are the GNU GPL version 3.\n\n" "--enable-gpl --enable-version3"
	printf "Corresponding source, the exact release this was built from:\n"
	printf "  https://github.com/FFmpeg/FFmpeg/tree/n4.4.1\n\n"
	printf "Its licence follows, from that same release.\n\n"
	cat "$cache/COPYING.GPLv3"
} > "$dest/ffmpeg-LICENSE.txt"

echo "  ffmpeg 4.4.1-static ($platform)"
