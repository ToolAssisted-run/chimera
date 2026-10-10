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
# A Linux bundle is built on the CPU it is for, so its ffmpeg is the build
# machine's CPU: x86-64 or aarch64. A Windows bundle is x64 wherever it is built.
#
# Everything is pinned and checked twice: the archive by its own SHA256, and the
# binary inside it by the SHA256 the frontend used to demand before it would run
# it. Downloads are cached under build/deps, so this costs nothing after the
# first build.
#
# CHIMERA_FFMPEG_LINUX / CHIMERA_FFMPEG_WINDOWS name a local binary to use
# instead, for a machine that cannot reach the network. CHIMERA_FFMPEG_LICENSE
# must then name a text file with that binary's licence and where its source
# is: the bundle ships it as ffmpeg-LICENSE.txt, since an ffmpeg is usually
# under the GPL and may not be handed on without them.
set -eu

platform="${1:?usage: fetch-ffmpeg.sh <linux|windows> <dest dir>}"
dest="${2:?usage: fetch-ffmpeg.sh <linux|windows> <dest dir>}"

root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${CHIMERA_DEPS_DIR:-$root/build/deps}"

case "$(uname -m)" in
	x86_64|amd64) cpu=x86_64 ;;
	aarch64|arm64) cpu=aarch64 ;;
	*) echo "fetch-ffmpeg: builds on x86-64 and aarch64 only, not $(uname -m)" >&2; exit 2 ;;
esac

# The same static ffmpeg 4.4.1 the frontend used to fetch on its own: half the
# size of a current all-inclusive build, and it still has every encoder the
# canned formats name (utvideo, ffv1 and aac are ffmpeg's own; x264, xvid, vpx,
# theora, vorbis and lame are configured in). member is where the binary sits
# inside the archive.
tasemulators="https://github.com/TASEmulators/ffmpeg-binaries/raw/master"
case "$platform" in
	linux)
		case "$cpu" in
			x86_64)
				archive="ffmpeg-4.4.1-static-linux-x64.7z"
				url="$tasemulators/$archive"
				archive_sha256="a1d47ae867c4211da47a2560905f62d02722b5a601e91ab9c95d574adfc00b06"
				member="ffmpeg"
				binary_sha256="3ea58083710f63bf920b16c7d5d24ae081e7d731f57a656fed11af0410d4eb48"
				;;
			aarch64)
				# The same release by the same builder, for arm64: the x64 binary
				# above is byte for byte the ffmpeg in his amd64 tarball of 4.4.1,
				# and this one is configured with the same encoders.
				archive="ffmpeg-4.4.1-arm64-static.tar.xz"
				url="https://johnvansickle.com/ffmpeg/old-releases/$archive"
				archive_sha256="af7cf5dc9e42f5baf0d6af6f1b34fbf77571929c4d211eda332673bd7ada1471"
				member="ffmpeg-4.4.1-arm64-static/ffmpeg"
				binary_sha256="97772dfc79fa8c483288de50c0a78904a1b79c94fa8a6b883677040e5bfc9b23"
				;;
		esac
		binary="ffmpeg"
		override="${CHIMERA_FFMPEG_LINUX:-}"
		;;
	windows)
		archive="ffmpeg-4.4.1-static-windows-x64.7z"
		url="$tasemulators/$archive"
		archive_sha256="9cdd3cd7b3263e881647b60c1188f4adc197788347b3d7ff62d535f2d6878849"
		member="ffmpeg.exe"
		binary_sha256="8436760af8f81c95eff92d854a7684e6d3cedb872888420359fc45c8eb2664ac"
		binary="ffmpeg.exe"
		override="${CHIMERA_FFMPEG_WINDOWS:-}"
		;;
	*)
		echo "fetch-ffmpeg: unknown platform '$platform'" >&2
		exit 2
		;;
esac

# The build is GPL v3 (--enable-gpl --enable-version3), so the licence has to
# travel with the binary. Taken from FFmpeg's own tree at the exact tag.
licence_url="https://raw.githubusercontent.com/FFmpeg/FFmpeg/n4.4.1/COPYING.GPLv3"
licence_sha256="8ceb4b9ee5adedde47b31e975c1d90c73ad27b6b165a1dcd80c7c545eb65b903"

# The 7-Zip that unpacks a .7z, if the machine has none, for the machine's CPU.
# A binary in a tar.xz, so bootstrapping it needs only tar - which means a build
# machine needs nothing installed for any of this.
case "$cpu" in
	x86_64)
		sevenzip_archive="7z2301-linux-x64.tar.xz"
		sevenzip_sha256="23babcab045b78016e443f862363e4ab63c77d75bc715c0b3463f6134cbcf318"
		;;
	aarch64)
		sevenzip_archive="7z2301-linux-arm64.tar.xz"
		sevenzip_sha256="34e938fc4ba8ca6a835239733d9c1542ad8442cc037f43ca143a119bdf322b63"
		;;
esac
sevenzip_url="https://www.7-zip.org/a/$sevenzip_archive"

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

# ---- a binary given by hand: before anything is downloaded, since it is for
# a machine that cannot download (the 7-Zip bootstrap below would fail first)
if [ -n "$override" ]; then
	[ -f "$override" ] || { echo "fetch-ffmpeg: $override is not a file" >&2; exit 1; }
	licence="${CHIMERA_FFMPEG_LICENSE:-}"
	[ -n "$licence" ] && [ -f "$licence" ] || {
		echo "fetch-ffmpeg: CHIMERA_FFMPEG_LICENSE must name a text file with the licence of $override" >&2
		echo "  and where its source is; the bundle cannot ship the binary without them." >&2
		exit 1
	}
	cp "$override" "$dest/$binary"
	chmod +x "$dest/$binary"
	{
		printf "ffmpeg (%s), given by whoever built this bundle - not the build Chimera pins.\n\n" "$platform"
		printf "Shipped as a separate program, which Chimera runs and talks to over a pipe.\n"
		printf "It is not linked into Chimera. sha256 of this binary: %s\n\n" "$(sha256sum "$override" | cut -d' ' -f1)"
		printf "Its licence, and where its source is, as the builder gave them:\n\n"
		cat "$licence"
	} > "$dest/ffmpeg-LICENSE.txt"
	echo "  ffmpeg from $override"
	exit 0
fi

# ---- the unpacker -----------------------------------------------------------
# only for a .7z; a tar.xz is tar's
sevenzip=""
case "$archive" in *.7z)
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
	;;
esac

# ---- the binary -------------------------------------------------------------

fetch "$url" "$cache/$archive" "$archive_sha256" "$archive"
fetch "$licence_url" "$cache/COPYING.GPLv3" "$licence_sha256" "the GPL v3 text"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
case "$archive" in
	*.7z) "$sevenzip" x -y "-o$tmp" "$cache/$archive" > /dev/null ;;
	*.tar.xz) tar -xJf "$cache/$archive" -C "$tmp" "$member" ;;
	*) echo "fetch-ffmpeg: no way to unpack $archive" >&2; exit 1 ;;
esac

[ -f "$tmp/$member" ] || { echo "fetch-ffmpeg: $archive did not contain $member" >&2; exit 1; }
verify "$tmp/$member" "$binary_sha256" || {
	echo "fetch-ffmpeg: the ffmpeg inside $archive is not the pinned build" >&2
	exit 1
}

cp "$tmp/$member" "$dest/$binary"
chmod +x "$dest/$binary"

{
	# the CPU by the name Chimera's release bundles give it
	printf "ffmpeg 4.4.1-static (%s, %s)\n\n" "$platform" "$([ "$platform" = windows ] || [ "$cpu" = x86_64 ] && echo x64 || echo arm64)"
	printf "Shipped as a separate program, which Chimera runs and talks to over a pipe.\n"
	printf "It is not linked into Chimera and is not modified. Taken unaltered from:\n"
	printf "  %s\n" "$url"
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
