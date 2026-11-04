#!/bin/bash
# Builds the ffmpeg a Chimera bundle ships, and puts it where the bundle
# expects it, with its licence beside it.
#
# Chimera ships ffmpeg so that Encode Video has nothing to ask a person for.
# It used to be a ready-made static build of everything ffmpeg can do, 26 MB of
# a 60 MB Windows download (issue 492). The owner chose to build our own
# (2026-11-04): FFmpeg with only what Encode Video's formats use, and what a
# script asking for raw pictures or sound needs, linked statically with x264
# and x265:
#
#   in:      NUT on a pipe (what Chimera writes): raw BGRA pictures, 16-bit sound
#   video:   libx264, libx264rgb, libx265, utvideo, ffv1, rawvideo
#   sound:   aac, flac, 16-bit PCM, and ac3, Matroska's own when a format names
#            no sound encoder
#   files:   MP4 (and MOV), Matroska, AVI, NUT, raw, WAV
#   filters: scale, setsar, crop, pad, fps, and what ffmpeg puts in by itself
#            to change a picture's or a sound's format
#   back:    the files it writes read again (H.264, H.265, FFV1, UT Video,
#            FLAC, 16-bit PCM in Matroska, MP4 or AVI): how Chimera's tests count
#            the frames of a video and check a lossless one kept every pixel
#
# A format a person types that needs anything else is refused by this ffmpeg
# with its own message. The build is GPL version 2 or later (x264 and x265 are
# GPL), and ffmpeg-LICENSE.txt says so and where every source is.
#
# Usage: ./build-ffmpeg.sh <linux|windows> <destination directory>
#
# A Linux ffmpeg is built for the CPU of the machine that builds it (x86-64
# or aarch64), a Windows one for x86-64.
#
# Every source is pinned by its SHA256 and kept under build/deps; the finished
# binary is kept there too, under a name made from this script's own hash, so
# a second run costs nothing until this script changes. Needs a C and C++
# compiler (mingw-w64 for windows), make, cmake, nasm and pkg-config.
#
# CHIMERA_FFMPEG_LINUX / CHIMERA_FFMPEG_WINDOWS name a ready binary to use
# instead, for a machine that cannot build or download. CHIMERA_FFMPEG_LICENSE
# must then name a text file with that binary's licence and where its source
# is: the bundle ships it as ffmpeg-LICENSE.txt.
set -eu
platform="${1:?usage: build-ffmpeg.sh <linux|windows> <dest dir>}"
dest="${2:?usage: build-ffmpeg.sh <linux|windows> <dest dir>}"
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="${CHIMERA_DEPS_DIR:-$root/build/deps}"
jobs="${CHIMERA_FFMPEG_JOBS:-$(nproc)}"

case "$platform" in
	linux)
		binary="ffmpeg"
		override="${CHIMERA_FFMPEG_LINUX:-}"
		;;
	windows)
		binary="ffmpeg.exe"
		override="${CHIMERA_FFMPEG_WINDOWS:-}"
		;;
	*)
		echo "build-ffmpeg: unknown platform '$platform'" >&2
		exit 2
		;;
esac

ffmpeg_version="7.1.1"
ffmpeg_url="https://ffmpeg.org/releases/ffmpeg-$ffmpeg_version.tar.xz"
ffmpeg_sha256="733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1"
# x264 has no releases: Debian's copy of its source at a fixed commit
x264_version="0.164.3108+git31e19f9"
x264_url="https://deb.debian.org/debian/pool/main/x/x264/x264_$x264_version.orig.tar.gz"
x264_sha256="41606cb8e788a7f8c4514290646d4ba5c7bc68d9e1ccd1a73f446a90546913eb"
x265_version="4.1"
x265_url="https://deb.debian.org/debian/pool/main/x/x265/x265_$x265_version.orig.tar.xz"
x265_sha256="85abb8a850000bf693947459c42a666a3ecd6de10a2ac6b3c6647554b14d3270"
# The same sources on GitHub, for when those sites do not answer or Debian has
# moved on (owner's choice, 2026-11-04): FFmpeg's own repository, a mirror of
# x264's, and MulticoreWare's own of x265, at the same commits. Archives made
# from a repository differ from the releases in a few files that say the
# version, which are put back as the releases have them (see unpack).
ffmpeg_url2="https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n$ffmpeg_version.tar.gz"
ffmpeg_sha256_2="f117507dc501f2a6c11f9241d8d0c3213846cfad91764361af37befd6b6c523d"
x264_commit="31e19f92f00c7003fa115047ce50978bc98c3a0d"
x264_url2="https://github.com/mirror/x264/archive/$x264_commit.tar.gz"
x264_sha256_2="d053c9d86988d6bc78237ca5205865c5ddf99c98ef4cd9927eec8f6d388f6dd9"
x265_url2="https://github.com/Multicorewareinc/x265/archive/refs/tags/$x265_version.tar.gz"
x265_sha256_2="631f593ee655cb9f0e440fda7b938f1d798b379834242193947b8240b021ec87"
licence_url="https://raw.githubusercontent.com/FFmpeg/FFmpeg/n$ffmpeg_version/COPYING.GPLv2"
licence_sha256="8177f97513213526df2cf6184d8ff986c675afb514d4e68a404010521b880643"

mkdir -p "$dest" "$cache"

verify() { # <file> <sha256>
	[ -f "$1" ] || return 1
	echo "$2  $1" | sha256sum --check --status
}

fetch() { # <url> <path> <sha256> <what>
	if verify "$2" "$3"; then return 0; fi
	echo "  downloading $4"
	rm -f "$2"
	curl -sSL --fail --retry 3 --retry-delay 5 --connect-timeout 30 --max-time 1800 -o "$2" "$1" || {
		echo "build-ffmpeg: could not download $1" >&2
		echo "  Put it at $2 by hand, or point CHIMERA_FFMPEG_${platform^^} at an ffmpeg binary." >&2
		return 1
	}
	verify "$2" "$3" || {
		echo "build-ffmpeg: $4 is not what was pinned (sha256 mismatch)" >&2
		return 1
	}
}

# ---- a binary given by hand: before anything is built
if [ -n "$override" ]; then
	[ -f "$override" ] || { echo "build-ffmpeg: $override is not a file" >&2; exit 1; }
	licence="${CHIMERA_FFMPEG_LICENSE:-}"
	[ -n "$licence" ] && [ -f "$licence" ] || {
		echo "build-ffmpeg: CHIMERA_FFMPEG_LICENSE must name a text file with the licence of $override" >&2
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

# The CPU the binary is for: a Linux build is for the machine that builds it,
# a Windows one for x86-64. It is in the cached binary's name, since one cache
# can serve builds on both CPUs.
case "$platform" in
	linux)
		case "$(uname -m)" in
			x86_64) cpu=x64 ;;
			aarch64|arm64) cpu=arm64 ;;
			*) echo "build-ffmpeg: builds on x86-64 and aarch64 only, not $(uname -m)" >&2; exit 2 ;;
		esac
		;;
	windows) cpu=x64 ;;
esac

# ---- built before, by this very script --------------------------------------
key="$(sha256sum "$0" | cut -c1-16)"
built="$cache/ffmpeg-$platform-$cpu-$key"
if [ -f "$built/$binary" ] && [ -f "$built/ffmpeg-LICENSE.txt" ]; then
	cp "$built/$binary" "$dest/$binary"
	cp "$built/ffmpeg-LICENSE.txt" "$dest/ffmpeg-LICENSE.txt"
	chmod +x "$dest/$binary"
	echo "  ffmpeg $ffmpeg_version ($platform, $cpu), built before"
	exit 0
fi

# each source from where it is published, else from GitHub: which one is said
# in the licence file
ffmpeg_from="$ffmpeg_url"
fetch "$ffmpeg_url" "$cache/ffmpeg-$ffmpeg_version.tar.xz" "$ffmpeg_sha256" "FFmpeg $ffmpeg_version" ||
	{ ffmpeg_from="$ffmpeg_url2"; fetch "$ffmpeg_url2" "$cache/FFmpeg-n$ffmpeg_version.tar.gz" "$ffmpeg_sha256_2" "FFmpeg $ffmpeg_version from GitHub"; }
x264_from="$x264_url"
fetch "$x264_url" "$cache/x264_$x264_version.orig.tar.gz" "$x264_sha256" "x264 $x264_version" ||
	{ x264_from="$x264_url2"; fetch "$x264_url2" "$cache/x264-$x264_commit.tar.gz" "$x264_sha256_2" "x264 $x264_version from GitHub"; }
x265_from="$x265_url"
fetch "$x265_url" "$cache/x265_$x265_version.orig.tar.xz" "$x265_sha256" "x265 $x265_version" ||
	{ x265_from="$x265_url2"; fetch "$x265_url2" "$cache/x265-$x265_version.tar.gz" "$x265_sha256_2" "x265 $x265_version from GitHub"; }
fetch "$licence_url" "$cache/COPYING.GPLv2" "$licence_sha256" "the GPL v2 text"

work="$(mktemp -d "$cache/ffmpeg-build-$platform.XXXX")"
[ -n "${CHIMERA_FFMPEG_KEEP:-}" ] || trap 'rm -rf "$work"' EXIT
prefix="$work/prefix"
mkdir -p "$prefix"
# Unpacked under the release's folder names. A copy from GitHub gets back the
# files the releases have and a repository's archive does not, so that it
# builds the very same program: FFmpeg's VERSION, x264's version.sh as
# Debian's copy prints the version (there is no .git to ask), and x265's
# x265Version.txt as its release has it (the repository's still says 4.0).
if [ "$ffmpeg_from" = "$ffmpeg_url" ]; then
	tar -xJf "$cache/ffmpeg-$ffmpeg_version.tar.xz" -C "$work"
else
	tar -xzf "$cache/FFmpeg-n$ffmpeg_version.tar.gz" -C "$work"
	mv "$work/FFmpeg-n$ffmpeg_version" "$work/ffmpeg-$ffmpeg_version"
	echo "$ffmpeg_version" > "$work/ffmpeg-$ffmpeg_version/VERSION"
fi
if [ "$x264_from" = "$x264_url" ]; then
	tar -xzf "$cache/x264_$x264_version.orig.tar.gz" -C "$work"
else
	tar -xzf "$cache/x264-$x264_commit.tar.gz" -C "$work"
	mv "$work/x264-$x264_commit" "$work/x264-$x264_version"
	printf '#!/bin/sh\necho %s\necho %s\n' "'#define X264_VERSION \" r3108 31e19f9\"'" \
		"'#define X264_POINTVER \"0.164.3108 31e19f9\"'" > "$work/x264-$x264_version/version.sh"
fi
if [ "$x265_from" = "$x265_url" ]; then
	tar -xJf "$cache/x265_$x265_version.orig.tar.xz" -C "$work"
else
	tar -xzf "$cache/x265-$x265_version.tar.gz" -C "$work"
	mv "$work/x265-$x265_version" "$work/x265_$x265_version"
	printf '#Attribute: \t     Values\nrepositorychangeset: 1d117be\nreleasetagdistance: 1\nreleasetag: %s\n' "$x265_version" \
		> "$work/x265_$x265_version/x265Version.txt"
fi

if [ "$platform" = windows ]; then
	host=x86_64-w64-mingw32
	x264_cross=(--host=$host --cross-prefix=$host-)
	x265_cross=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64 -DCMAKE_C_COMPILER=$host-gcc
		-DCMAKE_CXX_COMPILER=$host-g++ -DCMAKE_RC_COMPILER=$host-windres -DCMAKE_ASM_NASM_COMPILER=nasm)
	ffmpeg_cross=(--arch=x86_64 --target-os=mingw32 --cross-prefix=$host- --pkg-config=pkg-config)
	strip=$host-strip
else
	x264_cross=()
	x265_cross=()
	ffmpeg_cross=()
	strip=strip
fi

say() { echo "  $*"; }

say "x264 $x264_version ($platform)"
(
	cd "$work/x264-$x264_version"
	./configure --prefix="$prefix" "${x264_cross[@]}" --enable-static --disable-cli --bit-depth=8 \
		--disable-opencl --disable-lavf --disable-swscale --disable-ffms --disable-gpac --disable-lsmash
	make -j"$jobs"
	make install
) > "$work/x264.log" 2>&1 || { tail -30 "$work/x264.log" >&2; exit 1; }

say "x265 $x265_version ($platform)"
(
	cmake -S "$work/x265_$x265_version/source" -B "$work/x265-build" -G "Unix Makefiles" "${x265_cross[@]}" \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
		-DENABLE_SHARED=OFF -DENABLE_CLI=OFF -DENABLE_LIBNUMA=OFF -DSTATIC_LINK_CRT=ON
	make -C "$work/x265-build" -j"$jobs"
	make -C "$work/x265-build" install
) > "$work/x265.log" 2>&1 || { tail -30 "$work/x265.log" >&2; exit 1; }
# x265 is C++: a static ffmpeg links its runtime itself
sed -i 's/^Libs.private:.*/Libs.private: -lstdc++ -lm/' "$prefix/lib/pkgconfig/x265.pc"

say "FFmpeg $ffmpeg_version ($platform)"
configuration=(
	# never installed: a prefix that names no folder of this machine, since the
	# binary carries its configuration; x264 and x265 are found by pkg-config
	--prefix=/usr/local "${ffmpeg_cross[@]}" --pkg-config-flags=--static --extra-ldexeflags=-static
	--enable-gpl --enable-libx264 --enable-libx265
	--disable-everything --disable-autodetect --disable-doc --disable-debug --disable-network
	--disable-ffplay --disable-ffprobe --disable-avdevice --disable-postproc --disable-shared --enable-static
	--enable-protocol=file,pipe,fd
	--enable-demuxer=nut,matroska,mov,avi
	--enable-decoder=rawvideo,pcm_s16le,ffv1,utvideo,flac,h264,hevc
	--enable-encoder=libx264,libx264rgb,libx265,utvideo,ffv1,rawvideo,wrapped_avframe,aac,ac3,flac,pcm_s16le
	--enable-muxer=mp4,mov,matroska,avi,nut,rawvideo,wav,null
	--enable-filter=scale,setsar,crop,pad,fps,format,aformat,aresample,null,anull,copy,acopy
)
(
	cd "$work/ffmpeg-$ffmpeg_version"
	PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" PKG_CONFIG_PATH="$prefix/lib/pkgconfig" ./configure "${configuration[@]}"
	make -j"$jobs"
) > "$work/ffmpeg.log" 2>&1 || { tail -40 "$work/ffmpeg.log" >&2; exit 1; }
"$strip" "$work/ffmpeg-$ffmpeg_version/$binary"

mkdir -p "$built"
cp "$work/ffmpeg-$ffmpeg_version/$binary" "$built/$binary"
{
	printf "ffmpeg (%s, %s), built for Chimera by tools/build-ffmpeg.sh from these sources:\n\n" "$platform" "$cpu"
	printf "  FFmpeg %s  %s\n" "$ffmpeg_version" "$ffmpeg_from"
	printf "  x264 %s  %s\n" "$x264_version" "$x264_from"
	printf "  x265 %s  %s\n\n" "$x265_version" "$x265_from"
	printf "Each is pinned by its SHA256 in build-ffmpeg.sh; the same sources are also at\n"
	printf "  %s\n  %s\n  %s\n  %s\n  %s\n  %s\n\n" "$ffmpeg_url" "$ffmpeg_url2" "$x264_url" "$x264_url2" "$x265_url" "$x265_url2"
	printf "Shipped as a separate program, which Chimera runs and talks to over a pipe.\n"
	printf "It is not linked into Chimera. With x264 and x265 in it, the whole is under the\n"
	printf "GNU General Public License, version 2 or (at your option) any later version,\n"
	printf "whose text follows. sha256 of this binary: %s\n\n" "$(sha256sum "$built/$binary" | cut -d' ' -f1)"
	printf "FFmpeg's configuration:\n  %s\n\n" "${configuration[*]}"
	cat "$cache/COPYING.GPLv2"
} > "$built/ffmpeg-LICENSE.txt"
cp "$built/$binary" "$dest/$binary"
cp "$built/ffmpeg-LICENSE.txt" "$dest/ffmpeg-LICENSE.txt"
chmod +x "$dest/$binary"
echo "  ffmpeg $ffmpeg_version ($platform, $cpu): $(du -h "$dest/$binary" | cut -f1)"
