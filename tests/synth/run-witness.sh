#!/bin/bash
# The synthetic witness driver - miniHawk's own smoke/correctness test.
#
# miniHawk is waterbox-only. The shipped core is the waterboxed core.wbx; the
# native C reference exists only to generate/verify the goldens (it is not a core).
# Level A: native/synth-run (the reference) records goldens; the waterboxed
#   core.wbx (via the miniBox host) must reproduce them byte-for-byte, in both
#   simple and per-frame whole-machine-savestate (rerecord) modes.
# Level E (engine): chimera-run - the engine's headless session - replays the
#   same movies with no frontend at all; its dumps must match the goldens too.
# Level B (frontend): EmuHawk (Mono + Xvfb on Linux) loads the core.wbx package
#   through miniHawk's built-in generic waterbox adapter and replays the same
#   movies; the final RAM and VRAM dumps must be byte-identical to the Level A
#   goldens. Audio is Level A-verified only (no scriptable audio tap).
#
# Usage:
#   ./run-witness.sh              # verify (all levels, both modes)
#   ./run-witness.sh --record     # (re)record goldens from the current build
#   ./run-witness.sh --level a    # native level only (no EmuHawk needed)
#   ./run-witness.sh --level e    # engine level only (no EmuHawk needed)
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

# ---------- Level E ----------
# The engine's own headless runner (chimera-run): the same package, host and
# movies with NO frontend at all - no Mono, no display. Its dumps must match
# the Level A goldens, in both modes, and the settings channel must reach the
# guest. This is the migration's session (docs/engine-migration.md) proving
# it IS the same machine.
if [ "$level" = "both" ] || [ "$level" = "e" ]; then
	chimera_run="$repo_root/build/meson-linux/chimera-run"
	epkg="$repo_root/build/Cores/synth-box.zip"
	# the ABI must actually be exported - mingw once silently un-exported it
	# when a vendored library declared its own dllexports
	if [ -f "$repo_root/build/dll/libchimera.so" ] \
		&& ! nm -D "$repo_root/build/dll/libchimera.so" 2>/dev/null | grep -q ' ce_abi_version$'; then
		report "E:exports:linux" FAIL "libchimera.so does not export ce_abi_version"
	fi
	if [ -f "$repo_root/build/dll/libchimera.dll" ] \
		&& ! objdump -p "$repo_root/build/dll/libchimera.dll" 2>/dev/null | grep -q 'ce_abi_version'; then
		report "E:exports:windows" FAIL "libchimera.dll does not export ce_abi_version"
	fi

	if [ ! -x "$chimera_run" ]; then
		report "E:engine" SKIP "chimera-run not built (meson compile -C build/meson-linux)"
	elif [ ! -f "$epkg" ]; then
		report "E:engine" SKIP "package not found: $epkg (run build-package.sh)"
	elif [ "$record" -eq 1 ]; then
		report "E:engine" PASS "(goldens recorded at level A)"
	else
		for movie in "$here"/movies/*.txt; do
			ename="$(basename "$movie" .txt)"
			erom="$here/roms/${ename%%.*}.testrom"
			for emode in simple rerecord; do
				eextra=""
				[ "$emode" = "rerecord" ] && eextra="--rerecord"
				etag="$ename.engine.$emode"
				rm -f "$work/$etag.ram.bin" "$work/$etag.vram.bin" "$work/$etag.meta.txt"
				"$chimera_run" "$epkg" "$erom" "$movie" $eextra 					--dump "RAM=$work/$etag.ram.bin" --dump "VRAM=$work/$etag.vram.bin" 					--meta "$work/$etag.meta.txt" > "$work/$etag.log" 2>&1
				if [ ! -f "$work/$etag.meta.txt" ] || ! grep -q "^status=OK" "$work/$etag.meta.txt"; then
					report "E:$ename:$emode" FAIL "no OK meta (see work/$etag.log)"
				elif cmp -s "$work/$etag.ram.bin" "$golden_dir/$ename.ram.bin" 					&& cmp -s "$work/$etag.vram.bin" "$golden_dir/$ename.vram.bin"; then
					report "E:$ename:$emode" PASS "RAM+VRAM byte-identical to level A goldens"
				else
					report "E:$ename:$emode" FAIL "RAM or VRAM differs from level A goldens"
				fi
			done
		done
		# the greenzone: play to the end, seek BACK mid-movie, invalidate (as an
		# input edit would), replay to the end - the dumps must not change
		for movie in "$here"/movies/*.txt; do
			gname="$(basename "$movie" .txt)"
			grom="$here/roms/${gname%%.*}.testrom"
			gtag="$gname.engine.seek"
			rm -f "$work/$gtag.ram.bin" "$work/$gtag.vram.bin"
			"$chimera_run" "$epkg" "$grom" "$movie" --seek 10 				--dump "RAM=$work/$gtag.ram.bin" --dump "VRAM=$work/$gtag.vram.bin" 				> "$work/$gtag.log" 2>&1
			if cmp -s "$work/$gtag.ram.bin" "$golden_dir/$gname.ram.bin" 				&& cmp -s "$work/$gtag.vram.bin" "$golden_dir/$gname.vram.bin"; then
				report "E:$gname:seek" PASS "greenzone seek+replay byte-identical"
			else
				report "E:$gname:seek" FAIL "RAM or VRAM differs after seek (see work/$gtag.log)"
			fi
		done

		# the settings channel, natively: a non-default sync setting must diverge
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" 			--settings '{"initFillByte":171}' --dump "RAM=$work/engine.settings.ram.bin" 			> "$work/engine.settings.log" 2>&1
		if [ ! -f "$work/engine.settings.ram.bin" ]; then
			report "E:settings" FAIL "run failed (see work/engine.settings.log)"
		elif cmp -s "$work/engine.settings.ram.bin" "$golden_dir/gridWalker.win.ram.bin"; then
			report "E:settings" FAIL "RAM identical to golden - setting did NOT reach the guest"
		else
			report "E:settings" PASS "RAM diverged - user setting reached the guest"
		fi
	fi
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

	# --- a core is never loaded implicitly ---
	# Same movie, but NO --core. A package sitting in build/Cores is AVAILABLE, not
	# loaded: opening a core is something the user does (File > Open Core), and the
	# commandline says which one with --core. So this run must NOT produce a machine -
	# if it does, something is loading cores behind the user's back again.
	if [ "$record" -eq 0 ]; then
		dname=gridWalker.win
		djob="$work/job.discovery.txt"
		{
			echo "movie=$here/movies/$dname.txt"
			echo "outram=$work/discovery.ram.bin"
			echo "outvram=$work/discovery.vram.bin"
			echo "meta=$work/discovery.meta.txt"
			echo "mode=simple"
		} > "$djob"
		rm -f "$work/discovery.ram.bin" "$work/discovery.meta.txt"
		cp "$config" "$work/config.discovery.ini"
		( cd "$repo_root" && MINIHAWK_JOB="$djob" timeout 300 mono "$emu_hawk" --headless \
			"--config=$work/config.discovery.ini" "--lua=$here/synth-replay.lua" \
			"$here/roms/${dname%%.*}.testrom" ) > "$work/discovery.log" 2>&1
		if [ -f "$work/discovery.meta.txt" ] && grep -q "^status=OK" "$work/discovery.meta.txt"; then
			report "D:box:noImplicitCore" FAIL "the rom ran with no core opened and no --core"
		else
			report "D:box:noImplicitCore" PASS "a rom does not load without a core (see work/discovery.log)"
		fi

		# --- keybinds ship with the package ---
		# The frontend has no bindings of its own: a controller it has never seen is played however
		# the package that declared it says (the package's default_keybinds.json). Start from a
		# config that has never heard of this controller - which is what a fresh install is - and
		# the config EmuHawk writes on exit must hold the package's bindings, or the core arrives
		# unplayable.
		kname=gridWalker.win
		kcfg="$work/config.keybinds.ini"
		python3 "$here/forget-controller.py" "$config" "$kcfg" "Synth Controller"
		kjob="$work/job.keybinds.txt"
		{
			echo "movie=$here/movies/$kname.txt"
			echo "outram=$work/keybinds.ram.bin"
			echo "outvram=$work/keybinds.vram.bin"
			echo "meta=$work/keybinds.meta.txt"
			echo "mode=simple"
		} > "$kjob"
		rm -f "$work/keybinds.meta.txt"
		( cd "$repo_root" && MINIHAWK_JOB="$kjob" timeout 300 mono "$emu_hawk" --headless \
			"--config=$kcfg" "--core=$package" "--lua=$here/synth-replay.lua" \
			"$here/roms/${kname%%.*}.testrom" ) > "$work/keybinds.log" 2>&1
		if [ ! -f "$work/keybinds.meta.txt" ] || ! grep -q "^status=OK" "$work/keybinds.meta.txt"; then
			report "K:box:keybinds" FAIL "run failed (see work/keybinds.log)"
		elif python3 "$here/check-keybinds.py" "$kcfg" \
			"$here/package-box/default_keybinds.json" "Synth Controller" > "$work/keybinds.txt" 2>&1; then
			report "K:box:keybinds" PASS "$(cat "$work/keybinds.txt")"
		else
			report "K:box:keybinds" FAIL "$(head -1 "$work/keybinds.txt")"
		fi
	fi
fi

echo ""
echo "$ok ok, $failed failed"
[ "$failed" -gt 0 ] && exit 1
exit 0
