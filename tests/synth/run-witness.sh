#!/bin/bash
# The synthetic witness driver - miniHawk's own smoke/correctness test.
#
# miniHawk is waterbox-only. The shipped core is the waterboxed core.wbx; the
# native C reference exists only to generate/verify the goldens (it is not a core).
# Level A: native/synth-run (the reference) records goldens; the waterboxed
#   core.wbx (via the miniBox host) must reproduce them byte-for-byte, in both
#   simple and per-frame whole-machine-savestate (rerecord) modes.
# Level B (frontend): EmuHawk (Mono + Xvfb on Linux) loads the core.wbx package
#   through miniHawk's built-in generic waterbox adapter and replays the same
#   movies; the final RAM and VRAM dumps must be byte-identical to the Level A
#   goldens. Audio is Level A-verified only (no scriptable audio tap).
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
# native/synth-run is the REFERENCE that records the goldens (it is not a core -
# just the offline ground truth). The waterboxed core.wbx must MATCH them, which
# is the equivalence proof for the only shipped flavor.
box_tester="$here/package-box/synth-run-box"
box_wbx="$here/package-box/synth.wbx"
box_host_dir="$here/../../extern/miniBox/build/meson-linux/source/host"
if [ "$level" = "both" ] || [ "$level" = "a" ]; then
	for movie in "$here"/movies/*.txt; do
		name="$(basename "$movie" .txt)"
		rom="$here/roms/${name%%.*}.testrom"
		golden="$golden_dir/$name.expected"
		"$here/native/synth-run" "$rom" "$movie" \
			--dump-ram "$work/$name.ram.bin" --dump-vram "$work/$name.vram.bin" \
			> "$work/$name.simple.out" || { report "A:nat:$name" FAIL "runner error"; continue; }
		"$here/native/synth-run" "$rom" "$movie" --rerecord > "$work/$name.rerecord.out" \
			|| { report "A:nat:$name" FAIL "runner error (rerecord)"; continue; }
		if ! cmp -s "$work/$name.simple.out" "$work/$name.rerecord.out"; then
			report "A:nat:$name" FAIL "rerecord diverges from simple"
			continue
		fi
		if [ "$record" -eq 1 ]; then
			cp "$work/$name.simple.out" "$golden"
			cp "$work/$name.ram.bin" "$golden_dir/$name.ram.bin"
			cp "$work/$name.vram.bin" "$golden_dir/$name.vram.bin"
			report "A:nat:$name" RECORDED "$(grep -o 'frames=[0-9]*' "$golden")"
		elif [ ! -f "$golden" ]; then
			report "A:nat:$name" NOGOLDEN ""
		elif cmp -s "$work/$name.simple.out" "$golden"; then
			report "A:nat:$name" PASS "$(grep -o 'frames=[0-9]*' "$golden")"
		else
			report "A:nat:$name" FAIL "metrics differ: $(diff "$golden" "$work/$name.simple.out" | tr '\n' ' ' | head -c 120)"
		fi

		# waterboxed flavor (c): synth.wbx run through the miniBox host, same
		# goldens. --rerecord here round-trips the WHOLE guest machine via the
		# waterbox host's save/load state around every frame.
		if [ -f "$box_tester" ] && [ -f "$box_wbx" ]; then
			LD_LIBRARY_PATH="$box_host_dir" "$box_tester" "$box_wbx" "$rom" "$movie" > "$work/$name.box.simple.out" 2>/dev/null \
				|| { report "A:box:$name" FAIL "runner error"; continue; }
			LD_LIBRARY_PATH="$box_host_dir" "$box_tester" "$box_wbx" "$rom" "$movie" --rerecord > "$work/$name.box.rerecord.out" 2>/dev/null \
				|| { report "A:box:$name" FAIL "runner error (rerecord)"; continue; }
			if ! cmp -s "$work/$name.box.simple.out" "$work/$name.box.rerecord.out"; then
				report "A:box:$name" FAIL "rerecord diverges from simple"
			elif cmp -s "$work/$name.box.simple.out" "$golden"; then
				report "A:box:$name" PASS "matches native goldens (waterboxed)"
			else
				report "A:box:$name" FAIL "diverges from native: $(diff "$golden" "$work/$name.box.simple.out" | tr '\n' ' ' | head -c 120)"
			fi
		else
			report "A:box:$name" SKIP "synth.wbx not built (run package-box/build-box.sh)"
		fi
	done
fi

# ---------- Level B ----------
if [ "$level" = "both" ] || [ "$level" = "b" ]; then
	emu_hawk="$repo_root/build/EmuHawk.exe"

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

	# miniHawk is waterbox-only: the frontend only accepts .wbx cores, so the box
	# flavor is the ONLY Level-B core. native/sharp are Level-A equivalence
	# references (proving synth.wbx matches the goldens), never frontend cores.
	for flavor in box; do
		package="$repo_root/build/Cores/synth-$flavor.zip"
		[ -f "$package" ] || { echo "package not found: $package (run build-package.sh)" >&2; exit 1; }
		for movie in "$here"/movies/*.txt; do
			name="$(basename "$movie" .txt)"
			rom="$here/roms/${name%%.*}.testrom"
			for mode in simple rerecord; do
				tag="$name.$flavor.$mode"
				job="$work/job.$tag.txt"
				{
					echo "movie=$movie"
					echo "outram=$work/$tag.ram.bin"
					echo "outvram=$work/$tag.vram.bin"
					echo "meta=$work/$tag.meta.txt"
					echo "mode=$mode"
				} > "$job"
				rm -f "$work/$tag.ram.bin" "$work/$tag.vram.bin" "$work/$tag.meta.txt"
				cp "$config" "$work/config.$tag.ini"
				( cd "$repo_root" && MINIHAWK_JOB="$job" timeout 300 mono "$emu_hawk" --headless \
					"--config=$work/config.$tag.ini" "--core=$package" \
					"--lua=$here/synth-replay.lua" "$rom" ) > "$work/$tag.log" 2>&1
				if [ ! -f "$work/$tag.meta.txt" ] || ! grep -q "^status=OK" "$work/$tag.meta.txt"; then
					report "B:$flavor:$name:$mode" FAIL "no OK meta (see work/$tag.log)"
					continue
				fi
				if [ "$record" -eq 1 ]; then
					report "B:$flavor:$name:$mode" PASS "(goldens recorded at level A)"
				elif cmp -s "$work/$tag.ram.bin" "$golden_dir/$name.ram.bin" \
					&& cmp -s "$work/$tag.vram.bin" "$golden_dir/$name.vram.bin"; then
					report "B:$flavor:$name:$mode" PASS "RAM+VRAM byte-identical to level A goldens"
				else
					report "B:$flavor:$name:$mode" FAIL "RAM or VRAM differs from level A goldens"
				fi
			done
		done
	done

	# --- settings -> guest channel ---
	# Same package + movie, but with initFillByte set non-zero via the core's SYNC
	# settings (injected into the config exactly as the UI/movie would). The guest
	# pre-fills RAM, so the RAM dump MUST diverge from the golden - proving the
	# built-in adapter delivered the user setting to the guest.
	if [ "$record" -eq 0 ]; then
		sname=gridWalker.win
		srom="$here/roms/${sname%%.*}.testrom"
		scfg="$work/config.settings.ini"
		python3 - "$config" "$scfg" <<'PY'
import json, sys
cfg = json.load(open(sys.argv[1]))
cfg.setdefault("CoreSyncSettings", {})["BizHawk.Emulation.Common.Waterbox.WaterboxCore"] = {"Values": {"initFillByte": 171}}
json.dump(cfg, open(sys.argv[2], "w"), indent=2)
PY
		sjob="$work/job.settings.txt"
		{
			echo "movie=$here/movies/$sname.txt"
			echo "outram=$work/settings.ram.bin"
			echo "outvram=$work/settings.vram.bin"
			echo "meta=$work/settings.meta.txt"
			echo "mode=simple"
		} > "$sjob"
		rm -f "$work/settings.ram.bin" "$work/settings.meta.txt"
		( cd "$repo_root" && MINIHAWK_JOB="$sjob" timeout 300 mono "$emu_hawk" --headless \
			"--config=$scfg" "--core=$repo_root/build/Cores/synth-box.zip" \
			"--lua=$here/synth-replay.lua" "$srom" ) > "$work/settings.log" 2>&1
		if [ ! -f "$work/settings.meta.txt" ] || ! grep -q "^status=OK" "$work/settings.meta.txt"; then
			report "S:box:initFillByte" FAIL "no OK meta (see work/settings.log)"
		elif cmp -s "$work/settings.ram.bin" "$golden_dir/$sname.ram.bin"; then
			report "S:box:initFillByte" FAIL "RAM identical to golden - setting did NOT reach the guest"
		else
			report "S:box:initFillByte" PASS "RAM diverged - user setting reached the guest"
		fi
	fi
fi

echo ""
echo "$ok ok, $failed failed"
[ "$failed" -gt 0 ] && exit 1
exit 0
