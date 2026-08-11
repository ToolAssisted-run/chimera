#!/bin/bash
# The synthetic witness driver - miniHawk's own smoke/correctness test.
#
# Level A (native): synth-run replays every movie against every rom on
#   libsynthcore directly and compares frames/RAM/video/audio metrics -
#   including full per-frame video and audio stream hashes - against goldens,
#   in both simple and per-frame-serialize (rerecord) modes.
# Level B (frontend): EmuHawk (Mono + Xvfb on Linux) loads the synth-native
#   package, replays the same movies through the real input pipeline, and the
#   final RAM and VRAM (framebuffer) dumps must be byte-identical to the same
#   goldens Level A verified. Audio is Level A-verified only (the frontend
#   exposes no scriptable audio tap).
#
# Usage:
#   ./run-witness.sh              # verify (both levels, both modes)
#   ./run-witness.sh --record     # (re)record goldens from the current build
#   ./run-witness.sh --level a    # native level only (no EmuHawk needed)
set -u

record=0
level=both
while [ $# -gt 0 ]; do
	case "$1" in
		--record) record=1 ;;
		--level) level="$2"; shift ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
	shift
done

here="$(cd "$(dirname "$0")" && pwd)"
repo_root="$(cd "$here/../.." && pwd)"
golden_dir="$here/goldens"
work="$here/work"
mkdir -p "$golden_dir" "$work"
failed=0
ok=0

# ---------- build ----------
gcc -O2 -Wall -Wextra -Werror -o "$here/native/synth-run" \
	"$here/native/synth-run.c" "$here/native/synthcore.c" || exit 1
for src in "$here"/roms/*.sasm; do
	python3 "$here/asm.py" "$src" "${src%.sasm}.testrom" > /dev/null || exit 1
done

report() { # name result detail
	printf "%-28s %-9s %s\n" "$1" "$2" "$3"
	case "$2" in PASS|RECORDED) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac
}

# ---------- Level A ----------
if [ "$level" = "both" ] || [ "$level" = "a" ]; then
	for movie in "$here"/movies/*.txt; do
		name="$(basename "$movie" .txt)"
		rom="$here/roms/${name%%.*}.testrom"
		golden="$golden_dir/$name.expected"
		"$here/native/synth-run" "$rom" "$movie" \
			--dump-ram "$work/$name.ram.bin" --dump-vram "$work/$name.vram.bin" \
			> "$work/$name.simple.out" || { report "A:$name" FAIL "runner error"; continue; }
		"$here/native/synth-run" "$rom" "$movie" --rerecord > "$work/$name.rerecord.out" \
			|| { report "A:$name" FAIL "runner error (rerecord)"; continue; }
		if ! cmp -s "$work/$name.simple.out" "$work/$name.rerecord.out"; then
			report "A:$name" FAIL "rerecord diverges from simple"
			continue
		fi
		if [ "$record" -eq 1 ]; then
			cp "$work/$name.simple.out" "$golden"
			cp "$work/$name.ram.bin" "$golden_dir/$name.ram.bin"
			cp "$work/$name.vram.bin" "$golden_dir/$name.vram.bin"
			report "A:$name" RECORDED "$(grep -o 'frames=[0-9]*' "$golden")"
		elif [ ! -f "$golden" ]; then
			report "A:$name" NOGOLDEN ""
		elif cmp -s "$work/$name.simple.out" "$golden"; then
			report "A:$name" PASS "$(grep -o 'frames=[0-9]*' "$golden")"
		else
			report "A:$name" FAIL "metrics differ: $(diff "$golden" "$work/$name.simple.out" | tr '\n' ' ' | head -c 120)"
		fi
	done
fi

# ---------- Level B ----------
if [ "$level" = "both" ] || [ "$level" = "b" ]; then
	package="$repo_root/build/Cores/synth-native.zip"
	emu_hawk="$repo_root/build/EmuHawk.exe"
	[ -f "$package" ] || { echo "package not found: $package (run build-package.sh)" >&2; exit 1; }

	export LD_LIBRARY_PATH="$repo_root/build/dll:$repo_root/build:/usr/lib/x86_64-linux-gnu"
	export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
	xvfb_pid=""
	cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
	trap cleanup EXIT
	if [ -z "${DISPLAY:-}" ]; then
		command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
		for n in 90 91 92 93 94; do
			if [ ! -e "/tmp/.X11-unix/X$n" ]; then
				Xvfb ":$n" -screen 0 640x480x24 -nolisten tcp & xvfb_pid=$!
				export DISPLAY=":$n"; break
			fi
		done
		sleep 1
	fi

	config="$work/config.ini"
	if [ ! -f "$config" ]; then
		( cd "$repo_root" && timeout 120 mono "$emu_hawk" --headless "--config=$config" \
			"--lua=$here/bootstrap-exit.lua" ) > "$work/bootstrap.log" 2>&1
		[ -f "$config" ] || { echo "config bootstrap failed (see $work/bootstrap.log)" >&2; exit 1; }
	fi
	# GDI+ display: OpenGL on the hidden display is Mesa llvmpipe software
	# rendering (wasted cores); display method cannot affect emulation
	sed -i 's/"DispMethod": [0-9]/"DispMethod": 1/' "$config"

	for movie in "$here"/movies/*.txt; do
		name="$(basename "$movie" .txt)"
		rom="$here/roms/${name%%.*}.testrom"
		for mode in simple rerecord; do
			job="$work/job.$name.$mode.txt"
			{
				echo "movie=$movie"
				echo "outram=$work/$name.b.$mode.ram.bin"
				echo "outvram=$work/$name.b.$mode.vram.bin"
				echo "meta=$work/$name.b.$mode.meta.txt"
				echo "mode=$mode"
			} > "$job"
			rm -f "$work/$name.b.$mode.ram.bin" "$work/$name.b.$mode.vram.bin" "$work/$name.b.$mode.meta.txt"
			cp "$config" "$work/config.$name.$mode.ini"
			( cd "$repo_root" && MINIHAWK_JOB="$job" timeout 300 mono "$emu_hawk" --headless \
				"--config=$work/config.$name.$mode.ini" "--core=$package" \
				"--lua=$here/synth-replay.lua" "$rom" ) > "$work/$name.b.$mode.log" 2>&1
			if [ ! -f "$work/$name.b.$mode.meta.txt" ] || ! grep -q "^status=OK" "$work/$name.b.$mode.meta.txt"; then
				report "B:$name:$mode" FAIL "no OK meta (see work/$name.b.$mode.log)"
				continue
			fi
			if [ "$record" -eq 1 ]; then
				report "B:$name:$mode" PASS "(goldens recorded at level A)"
			elif cmp -s "$work/$name.b.$mode.ram.bin" "$golden_dir/$name.ram.bin" \
				&& cmp -s "$work/$name.b.$mode.vram.bin" "$golden_dir/$name.vram.bin"; then
				report "B:$name:$mode" PASS "RAM+VRAM byte-identical to level A goldens"
			else
				report "B:$name:$mode" FAIL "RAM or VRAM differs from level A goldens"
			fi
		done
	done
fi

echo ""
echo "$ok ok, $failed failed"
[ "$failed" -gt 0 ] && exit 1
exit 0
