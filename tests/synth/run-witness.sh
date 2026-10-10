#!/bin/bash
# The synthetic witness driver - Chimera's own smoke/correctness test.
#
# Chimera is waterbox-only. The shipped core is the waterboxed core.wbx; the
# native C reference exists only to generate/verify the goldens (it is not a core).
# Level A: native/synth-run (the reference) records goldens; the waterboxed
#   core.wbx (via the miniBox host) must reproduce them byte-for-byte, in both
#   simple and per-frame whole-machine-savestate (rerecord) modes.
# Level E (engine): chimera-run - the engine's headless session - replays the
#   same movies with no frontend at all; its dumps must match the goldens too.
# Level B (frontend): Chimera (Mono + Xvfb on Linux) loads the core.wbx package
#   through Chimera's built-in generic waterbox adapter and replays the same
#   movies; the final RAM and VRAM dumps must be byte-identical to the Level A
#   goldens. Audio is Level A-verified only (no scriptable audio tap).
#
# Usage:
#   ./run-witness.sh              # verify (all levels, both modes)
#   ./run-witness.sh --record     # (re)record goldens from the current build
#   ./run-witness.sh --level a    # native level only (no Chimera needed)
#   ./run-witness.sh --level e    # engine level only (no Chimera needed)
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
known=0

# ---------- build ----------
gcc -O2 -Wall -Wextra -Werror -o "$here/native/synth-run" \
	"$here/native/synth-run.c" "$here/native/synthcore.c" || exit 1
for src in "$here"/roms/*.sasm; do
	python3 "$here/asm.py" "$src" "${src%.sasm}.testrom" > /dev/null || exit 1
done

report() { # name result detail
	printf "%-28s %-9s %s\n" "$1" "$2" "$3"
	# KNOWN: a failure that is tracked and does not fail the run - only ever
	# reported where a check says so, with the reason in the detail
	case "$2" in PASS|RECORDED) ok=$((ok+1)) ;; KNOWN) known=$((known+1)) ;; *) failed=$((failed+1)) ;; esac
}

# ---------- Level A ----------
# native/synth-run is the REFERENCE that records the goldens (it is not a core -
# just the offline ground truth). The waterboxed core.wbx must MATCH them, which
# is the equivalence proof for the only shipped flavor.
box_tester="$here/package-box/synth-run-box"
box_wbx="$here/package-box/synth.wbx"
box_host_dir="$here/../../extern/chimera-common-minibox/build/meson-linux/source/host"
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
	epkg="$repo_root/build/Cores/synth-box.chimeraCore"
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

		# A STATE KEPT IN A FILE (what a TAStudio branch keeps; engine.h). Saved to a
		# file and loaded straight back mid-movie, the machine must not notice: the
		# run ends byte for byte where the goldens say. Then the two ways a file can
		# be wrong - not a state at all, and a state cut short - must each be refused
		# with a reason and without taking the process down.
		for movie in "$here"/movies/*.txt; do
			sname="$(basename "$movie" .txt)"
			srom="$here/roms/${sname%%.*}.testrom"
			stag="$sname.engine.statefile"
			rm -f "$work/$stag.ram.bin" "$work/$stag.vram.bin" "$work/$stag.state"
			"$chimera_run" "$epkg" "$srom" "$movie" --state-file-roundtrip 10 --save-state-file "10=$work/$stag.state" 				--meta "$work/$stag.meta.txt" --dump "RAM=$work/$stag.ram.bin" --dump "VRAM=$work/$stag.vram.bin" 				> "$work/$stag.log" 2>&1
			if cmp -s "$work/$stag.ram.bin" "$golden_dir/$sname.ram.bin" 				&& cmp -s "$work/$stag.vram.bin" "$golden_dir/$sname.vram.bin" && [ -s "$work/$stag.state" ]; then
				report "E:$sname:statefile" PASS "saved to a file and loaded back mid-movie, byte-identical"
			else
				report "E:$sname:statefile" FAIL "RAM or VRAM differs after a state-file round trip (see work/$stag.log)"
			fi
		done
		sfirst="$(ls "$here"/movies/*.txt | head -1)"; sname="$(basename "$sfirst" .txt)"; srom="$here/roms/${sname%%.*}.testrom"
		printf 'this is not a state' > "$work/not-a-state.state"
		"$chimera_run" "$epkg" "$srom" "$sfirst" --state-file "$work/not-a-state.state" > "$work/statefile-garbage.log" 2>&1; garbage_rc=$?
		head -c 300 "$work/$sname.engine.statefile.state" > "$work/cut-short.state"
		"$chimera_run" "$epkg" "$srom" "$sfirst" --state-file "$work/cut-short.state" > "$work/statefile-short.log" 2>&1; short_rc=$?
		"$chimera_run" "$epkg" "$srom" "$sfirst" --state-file "$work/$sname.engine.statefile.state" > "$work/statefile-good.log" 2>&1; good_rc=$?
		if [ "$garbage_rc" -ne 0 ] && [ "$garbage_rc" -lt 128 ] && grep -q "is not a state file" "$work/statefile-garbage.log" 			&& [ "$short_rc" -ne 0 ] && [ "$short_rc" -lt 128 ] 			&& [ "$good_rc" -eq 0 ] && grep -q "state file tag: frame 10" "$work/statefile-good.log"; then
			report "E:statefile:refusals" PASS "a whole file loads and hands its tag back; garbage and a cut-short file are refused, no crash"
		else
			report "E:statefile:refusals" FAIL "garbage rc=$garbage_rc short rc=$short_rc good rc=$good_rc (see work/statefile-*.log)"
		fi

		# A PACKAGE WHOSE DECLARATIONS CANNOT BE READ says which file and what
		# is in it. The sentence reaches a person through a window that shows
		# one line (the project wizard's compile step), and "not readable JSON"
		# alone left a report of it with nothing to go on (2026-10-10). The
		# package here is the synthetic one unpacked, its waterbox.config
		# saved as UTF-16, which is what a text editor can do to it.
		badpkg="$work/unreadable-package"
		rm -rf "$badpkg"; mkdir -p "$badpkg"
		if (cd "$badpkg" && unzip -q "$epkg") 2>/dev/null && python3 -c "
import codecs, sys
p = sys.argv[1]
t = open(p, encoding='utf-8').read()
open(p, 'wb').write(codecs.BOM_UTF16_LE + t.encode('utf-16-le'))
" "$badpkg/waterbox.config"; then
			"$chimera_run" "$badpkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				> "$work/unreadable-package.log" 2>&1 || true
			if grep -q "waterbox.config is not readable JSON ($badpkg: [0-9]* bytes, starting with \\\\xff\\\\xfe" "$work/unreadable-package.log"; then
				report "E:package:unreadable" PASS "the refusal names the package, its size and its first bytes"
			else
				report "E:package:unreadable" FAIL "$(tail -1 "$work/unreadable-package.log" | cut -c1-120)"
			fi
		else
			report "E:package:unreadable" FAIL "could not prepare the package (unzip, python3)"
		fi

		# NUMBERS DO NOT FOLLOW THE SYSTEM'S DECIMAL SEPARATOR (issue 244). On
		# Linux the frontend's runtime puts the process in the user's locale;
		# with a decimal comma the engine could not read "0.5" in a core's
		# declarations or in settings, and would have written fractions nothing
		# reads back. A German locale is built here (no root needed) and two
		# things are run in it: the unit test's decimal-comma half, and this
		# runner taking the environment's locale as the frontend's runtime
		# does, with a fraction in its settings. The run must be the run the C
		# locale gives. Where the locale cannot be built the check says it did
		# not run.
		loc="$work/locales"
		rm -rf "$loc"; mkdir -p "$loc"
		if command -v localedef > /dev/null 2>&1 && localedef -i de_DE -f UTF-8 "$loc/de_DE.UTF-8" > /dev/null 2>&1; then
			"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--settings '{"probe":0.5}' --dump "RAM=$work/locale-c.ram.bin" > "$work/locale-c.log" 2>&1 || true
			LOCPATH="$loc" LC_ALL=de_DE.UTF-8 CHIMERA_TEST_NEEDS_COMMA_LOCALE=1 \
				"$repo_root/build/meson-linux/test_plain_numbers" > "$work/locale-unit.log" 2>&1
			unit_rc=$?
			LOCPATH="$loc" LC_ALL=de_DE.UTF-8 "$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--locale-from-environment --settings '{"probe":0.5}' --dump "RAM=$work/locale-de.ram.bin" > "$work/locale-de.log" 2>&1
			run_rc=$?
			if [ "$unit_rc" = 0 ] && [ "$run_rc" = 0 ] && grep -q "decimal separator ','" "$work/locale-de.log" \
				&& [ -s "$work/locale-c.ram.bin" ] && cmp -s "$work/locale-c.ram.bin" "$work/locale-de.ram.bin"; then
				report "E:locale:decimal-comma" PASS "with a decimal comma: fractions read and written with a dot, and the run is the C locale's"
			else
				report "E:locale:decimal-comma" FAIL "unit rc=$unit_rc run rc=$run_rc: $(tail -1 "$work/locale-de.log" | cut -c1-90) (see work/locale-*.log)"
			fi
		else
			report "E:locale:decimal-comma" KNOWN "did not run: no decimal-comma locale could be built here (localedef -i de_DE)"
		fi

		# A CORE THAT DIES: all eight buttons at once make the synth core abort on
		# cue (SPEC.md). The run must stop with the reason and the core's own last
		# words - miniBox handing control back - and the process must not crash.
		dmovie="$work/dies-at-11.txt"
		sed '12s/.*/|UDLRABsS|/' "$here/movies/gridWalker.win.txt" > "$dmovie"
		rm -f "$work/dies.meta.txt"
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$dmovie" \
			--meta "$work/dies.meta.txt" > "$work/dies.log" 2>&1
		drc=$?
		if [ "$drc" -ge 128 ]; then
			report "E:core-dies" FAIL "the process died with signal $((drc - 128)) (see work/dies.log)"
		elif ! grep -q "^status=ERROR" "$work/dies.meta.txt" 2>/dev/null; then
			report "E:core-dies" FAIL "the run did not report a stopped core (see work/dies.log)"
		elif grep -q "the core aborted" "$work/dies.meta.txt" && grep -q "stopping on purpose" "$work/dies.meta.txt"; then
			report "E:core-dies" PASS "a core that aborts stops the run with its reason, and the process survives"
		else
			report "E:core-dies" FAIL "the reason or the core's words are missing: $(grep '^detail=' "$work/dies.meta.txt")"
		fi
		# and the same death arriving as a FAULT (all but Up: a wild pointer)
		sed '12s/.*/|.DLRABsS|/' "$here/movies/gridWalker.win.txt" > "$work/crashes-at-11.txt"
		rm -f "$work/crashes.meta.txt"
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$work/crashes-at-11.txt" \
			--meta "$work/crashes.meta.txt" > "$work/crashes.log" 2>&1
		crc=$?
		if [ "$crc" -ge 128 ]; then
			report "E:core-crashes" FAIL "the process died with signal $((crc - 128)) (see work/crashes.log)"
		elif grep -q "^detail=the core stopped: the core crashed: it wrote to address" "$work/crashes.meta.txt" 2>/dev/null; then
			report "E:core-crashes" PASS "a core that follows a wild pointer stops the run, and the process survives"
		else
			report "E:core-crashes" FAIL "no stopped core reported (see work/crashes.log)"
		fi

		# THE CORE LOG (ce_core_log, Tools > Export Core Log... in the frontend):
		# asked for, it starts a file saying when and by which Chimera, notes
		# the session's core package, and keeps everything the core writes - the
		# dying core's last words included. The "corelog" request it mounts must
		# not move the machine: the same movie with the log on ends on the
		# goldens.
		clmovie="$here/movies/gridWalker.win.txt"
		rm -f "$work/core.log" "$work/core-on.ram.bin" "$work/core-on.vram.bin"
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$dmovie" \
			--core-log "$work/core.log" > "$work/core-log-dies.log" 2>&1
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$clmovie" --core-log "$work/core-on.log" \
			--dump "RAM=$work/core-on.ram.bin" --dump "VRAM=$work/core-on.vram.bin" > "$work/core-log-on.log" 2>&1
		if ! grep -q '^Chimera core log, started ' "$work/core.log" 2>/dev/null; then
			report "E:core-log" FAIL "no core log, or no header in it (see work/core-log-dies.log)"
		elif ! grep -q 'session: core package .*synth-box.chimeraCore (sha1 ' "$work/core.log"; then
			report "E:core-log" FAIL "the core log does not name the session's package"
		elif ! grep -q 'stopping on purpose' "$work/core.log"; then
			report "E:core-log" FAIL "the core log does not hold what the core said"
		elif ! cmp -s "$work/core-on.ram.bin" "$golden_dir/gridWalker.win.ram.bin" \
			|| ! cmp -s "$work/core-on.vram.bin" "$golden_dir/gridWalker.win.vram.bin"; then
			report "E:core-log" FAIL "with the core log on the machine differs from the goldens"
		else
			report "E:core-log" PASS "asked for, the log names its build and package and keeps the core's words; the machine is the goldens'"
		fi

		# A MOVIE MADE ELSEWHERE (ce_import_movie, Game > Import ... in the
		# frontend): the core is asked what the movie amounts to INSTEAD of being
		# started - the movie mounted as "movie", the files it names beside it,
		# the import options in the settings - and its answer comes back as JSON
		# for the frontend's own project creation. The synth reads its own movie
		# text, so every frame must come back as an input line, the option it
		# was given as the setting it dictates, and a game that is not at hand
		# must be refused in a sentence, not crash anything.
		imovie="$here/movies/gridWalker.win.txt"
		ianswer="$("$chimera_run" "$epkg" "$imovie" --import-movie \
			--mount gridWalker.testrom="$here/roms/gridWalker.testrom" \
			--settings '{"importRom":"gridWalker.testrom","importFill":7}' 2>"$work/import.err")"
		irefused="$("$chimera_run" "$epkg" "$imovie" --import-movie --settings '{"importRom":"gone.testrom"}' 2>>"$work/import.err")"
		iframes="$(grep -c '^|' "$imovie")"
		if printf '%s' "$ianswer" | python3 -c "
import json, sys
a = json.loads(sys.stdin.read())
lines = [l for l in a['input'].split('\n') if l.startswith('|')]
assert a['settings'] == {'initFillByte': 7}, a['settings']
assert a['files'] == [{'name': 'gridWalker.testrom', 'slot': 'rom'}], a['files']
assert a['frames'] == $iframes and len(lines) == $iframes, (a['frames'], len(lines))
assert a['input'].startswith('[Input]') and a['input'].rstrip().endswith('[/Input]')
" 2>"$work/import.check" && printf '%s' "$irefused" | grep -q '"error": "the game gone.testrom is not at hand"'; then
			report "E:import-movie" PASS "the core's answer comes back whole ($iframes frames, its setting, its file), and a missing game is a sentence"
		else
			report "E:import-movie" FAIL "$(tail -1 "$work/import.check" 2>/dev/null) refused=[$irefused] (see work/import.err)"
		fi

		# a bus read in runs is the bus read a byte at a time (chimera#180):
		# "Bus" answers through the core's ReadBus, "Bus (peeks)" declines it
		# and the engine peeks for itself. chimera-run compares every byte of
		# each against PeekBus, from an odd start and past the end.
		busok=1
		for bus in "Bus" "Bus (peeks)"; do
			"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--ram-search-bench "$bus" > "$work/bus.log" 2>&1
			grep -q "^ram-search-bench $bus: runs == peeks over" "$work/bus.log" || busok=0
		done
		if [ "$busok" = 1 ]; then
			report "E:bus-read" PASS "both buses read in runs exactly as peeked, the core's ReadBus and the engine's own"
		else
			report "E:bus-read" FAIL "$(grep -m1 -E 'differs|no domain' "$work/bus.log")"
		fi

		# a bus says which of it is live (engine.h, ce_session_bus_ranges;
		# chimera#218), and a RAM search told so is over those ranges alone.
		# The synth's "Bus" calls three pieces live - out of order, at odd
		# addresses, one running off its end - and the search of it holds
		# exactly those bytes (each kept whole to four), every candidate
		# reading what the bus peeks; "Bus (peeks)" says nothing, and is
		# searched whole. Both lines are from the last bus-read run above
		# and the one before it.
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--ram-search-bench "Bus" > "$work/ranges.log" 2>&1
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--ram-search-bench "Bus (peeks)" > "$work/ranges-none.log" 2>&1
		# 0x20001+0xff0 -> 0x20000..0x20ff4 (4084); 0x1000+0x801 -> 0x1000..0x1804 (2052);
		# 0x2fffa+64 -> 0x2fff8..0x30000 (8): 6144 bytes of 196608
		if grep -q "^ram-search-bench Bus: 3 live ranges, 6144 of 196608 bytes searched, every candidate live" "$work/ranges.log" \
			&& grep -q "^ram-search-bench Bus (peeks): no word on live ranges, all of it is searched" "$work/ranges-none.log"; then
			report "E:bus-ranges" PASS "a search of a bus that says what is live is over that alone: 6144 of 196608 bytes, each reading what the bus peeks"
		else
			report "E:bus-ranges" FAIL "$(grep -m1 -E 'live|ranges|word' "$work/ranges.log" "$work/ranges-none.log" | tail -1)"
		fi

		# the pictures of frames already drawn (engine.h,
		# ce_session_greenzone_pictures; chimera#190). A renderer on the far
		# side of the GPU bridge draws wrong for a while after a state load;
		# the synth pretends to, for 5 frames, with its garbleAfterLoad
		# setting (package-box/synth_wbx.c). Put back on frame 41 - by a
		# restore and a replay, and by a restore alone - a session that keeps
		# pictures shows the one the frame had; one that does not shows what
		# the renderer made of it. After an edit the pictures past it are
		# gone, the frame redrawn so soon after a load is not kept in their
		# place, and what it shows is what the core drew. And with a renderer
		# that is never wrong, keeping pictures changes nothing that is shown.
		kept() { # garble frames, pictures spec -> the three lines
			"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--settings "{\"garbleAfterLoad\":$1}" --greenzone-pictures "$2" --kept-pictures-check 40 2>/dev/null | grep '^kept-pictures'
		}
		# The two runs that COUNT pictures pack them inline (CHIMERA_HELPERS=0):
		# the helper drops a picture when it is three behind, which is right
		# and depends on what else the machine is doing - a build running
		# beside this once left 63 of the 70.
		kept 5 0 > "$work/kept.off"
		( export CHIMERA_HELPERS=0; kept 5 64,8 ) > "$work/kept.on"
		kept 0 64,8 > "$work/kept.healthy"
		# ...and the settle counts frames SHOWN, not frames run: with a state
		# only every 50 frames, going back to frame 41 replays 40 frames nobody
		# sees, and a renderer that was not drawing through them is as wrong on
		# the frame it then draws as it would have been at once. That frame
		# still shows the picture it had, and past an edit the wrong one is
		# still not kept (the count before the replay is the count after).
		CHIMERA_HELPERS=0 "$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--settings '{"garbleAfterLoad":5}' --greenzone-period 50 --greenzone-pictures 64,8 --kept-pictures-check 40 2>/dev/null \
			| grep '^kept-pictures' > "$work/kept.far"
		farKept="$(sed -n 's/.*after an edit before frame 41: \([0-9]*\) kept before the replay, \([0-9]*\) after it.*/\1 \2/p' "$work/kept.far")"
		if [ "$(grep -c 'NOT the picture it had' "$work/kept.off")" = 2 ] \
			&& grep -q '^kept-pictures: back on frame 41, replayed and drawn: the picture it had (70 kept' "$work/kept.on" \
			&& grep -q '^kept-pictures: back on frame 41 by a restore alone: the picture it had' "$work/kept.on" \
			&& grep -q '^kept-pictures: after an edit before frame 41: 40 kept before the replay, 40 after it, and the frame shows what the core drew' "$work/kept.on" \
			&& [ "$(grep -c 'the picture it had' "$work/kept.healthy")" = 3 ] && ! grep -q 'NOT' "$work/kept.healthy" \
			&& grep -q '^kept-pictures: back on frame 41, replayed and drawn: the picture it had' "$work/kept.far" \
			&& [ -n "$farKept" ] && [ "${farKept% *}" = "${farKept#* }" ]; then
			report "E:kept-pictures" PASS "back on a frame it has drawn, a session shows the picture the frame had (70 kept); without them, what a renderer wrong after a load made of it; past an edit, what the core draws"
		else
			report "E:kept-pictures" FAIL "$(cat "$work/kept.far" "$work/kept.off" "$work/kept.on" "$work/kept.healthy" 2>/dev/null | grep -m1 -v 'xxxx' | cut -c1-120) (see work/kept.*)"
		fi

		# --settle-probe, the instrument that says how long a renderer IS wrong
		# after a load on a given machine (chimera#190: on a GTX 1060 xemu is
		# one frame behind in one game and wrong for five in another, which no
		# amount of reasoning had said). It remembers frames as first drawn,
		# goes back to the frame before them with pictures off and a state on
		# every frame, draws them again and compares. Against a renderer whose
		# answer is known - the synth's, wrong for 5 drawn frames, for 2, for
		# none - it must give that answer, and the picture trace must show the
		# load and the first frame after it.
		probe() { # garble frames -> the summary line, then the trace's two
			CHIMERA_PICTURE_TRACE=1 "$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--settings "{\"garbleAfterLoad\":$1}" --greenzone-period 1 --settle-probe 40,8 2>&1 \
				| grep -a '^settle-probe: \(the picture is wrong\|every frame\)\|^pictrace: load, on frame 40\|^pictrace: frame 41, #1 after the load: read back'
		}
		probe 5 > "$work/probe.5"
		probe 2 > "$work/probe.2"
		probe 0 > "$work/probe.0"
		# --settle-probe-state asks the same of a WHOLE state taken on the frame
		# before and loaded back, no history in it - what told a core that dies
		# on any load from one that dies on a greenzone restore (Dolphin and
		# RPCS3 on the GTX 1060). The movie is not wound back with the machine,
		# so it is given one that presses nothing: a row recorded, repeated.
		printf '[Input]\nLogKey:#\n' > "$work/probe.none.txt"
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$work/probe.none.txt" --frames 1 --record "$work/probe.row.txt" > /dev/null 2>&1
		{ grep -v '^|' "$work/probe.row.txt" | grep -v '^\[/'; row="$(grep -m1 '^|' "$work/probe.row.txt")"; i=0; while [ "$i" -lt 60 ]; do printf '%s\n' "$row"; i=$((i + 1)); done; grep '^\[/' "$work/probe.row.txt"; } > "$work/probe.idle.txt"
		probeState() { # garble frames -> the summary line
			"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$work/probe.idle.txt" \
				--settings "{\"garbleAfterLoad\":$1}" --settle-probe 20,8 --settle-probe-state 2>&1 \
				| grep -a '^settle-probe: \(the picture is wrong\|every frame\)'
		}
		probeState 5 > "$work/probe.state.5"
		probeState 0 > "$work/probe.state.0"
		# --settle-probe-unseen runs the frames before the probe's as a seek
		# runs them, no picture read: the state the load lands on is then one
		# taken of a machine nobody was looking at, which is most of a
		# greenzone, and what a core's renderer must bring back from it is what
		# it DREW and not what was last READ (Flycast's last frame, chimera#190).
		# The synth counts the pictures it hands over (its Mailbox): the same
		# probe reads 40 fewer with the option - frames 0 to 39 - and still
		# measures a renderer wrong for 5 drawn frames as exactly that.
		probeRead() { # name, then extra arguments -> pictures read in the run
			pname="$1"; shift
			"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--settings '{"garbleAfterLoad":5}' --greenzone-period 1 --settle-probe 40,8 \
				--dump "Mailbox=$work/probe.$pname.bin" "$@" > "$work/probe.$pname" 2>&1
			od -A n -t u4 -j 16 -N 4 "$work/probe.$pname.bin" 2>/dev/null | tr -d ' '
		}
		readSeen="$(probeRead seen)"
		readUnseen="$(probeRead unseen --settle-probe-unseen)"
		if grep -q '^settle-probe: the picture is wrong up to drawn frame #5 after the load, of 8 drawn' "$work/probe.5" \
			&& grep -q '^settle-probe: the picture is wrong up to drawn frame #2 after the load, of 8 drawn' "$work/probe.2" \
			&& grep -q '^settle-probe: every frame drawn after the load is the picture it was' "$work/probe.0" \
			&& grep -q '^settle-probe: the picture is wrong up to drawn frame #5 after the load, of 8 drawn' "$work/probe.state.5" \
			&& grep -q '^settle-probe: every frame drawn after the load is the picture it was' "$work/probe.state.0" \
			&& grep -q '^settle-probe: the picture is wrong up to drawn frame #5 after the load, of 8 drawn' "$work/probe.unseen" \
			&& [ -n "$readSeen" ] && [ -n "$readUnseen" ] && [ "$((readSeen - readUnseen))" = 40 ] \
			&& [ "$(grep -c '^pictrace: ' "$work/probe.5")" = 2 ] && [ "$(grep -c '^pictrace: ' "$work/probe.0")" = 2 ]; then
			report "E:settle-probe" PASS "a renderer wrong for 5 drawn frames after a load, for 2 and for none is measured as exactly that, through the greenzone and through a whole state loaded back, and from states of frames nobody read ($readUnseen pictures read against $readSeen); the picture trace names the load and the frame after it"
		else
			report "E:settle-probe" FAIL "$(cat "$work/probe.5" "$work/probe.2" "$work/probe.0" "$work/probe.state.5" "$work/probe.state.0" "$work/probe.unseen" 2>/dev/null | grep -m1 '^settle-probe' | cut -c1-120); pictures read seen '$readSeen' unseen '$readUnseen' (see work/probe.*)"
		fi

		# What heads a button's column (chimera#225). A keyboard has more keys
		# than one character tells apart, so a package may give a button a
		# header as well as its letter. The synth gives Select one, "SEL", and
		# no other button any. Asked as a window asks (chimera-run --controls:
		# a control's name, the character it writes, what heads its column):
		#   - Select is headed SEL and still writes s;
		#   - a button with no header is headed by its letter;
		#   - and a movie recorded with Select held has s in its text and SEL
		#     nowhere: a header is never written into a movie.
		printf '|........|\n|......s.|\n|......s.|\n|........|\n' > "$work/headers.in.txt"
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$work/headers.in.txt" \
			--record "$work/headers.out.txt" --controls "$work/headers.controls" > "$work/headers.log" 2>&1
		tab="$(printf '\t')"
		if grep -qx "P1 Select${tab}s${tab}SEL" "$work/headers.controls" 2>/dev/null \
			&& grep -qx "P1 Start${tab}S${tab}S" "$work/headers.controls" \
			&& [ "$(wc -l < "$work/headers.controls")" = 8 ] \
			&& [ "$(grep -c '|......s.|$' "$work/headers.out.txt")" = 2 ] \
			&& ! grep -q 'SEL' "$work/headers.out.txt"; then
			report "E:column-headers" PASS "a button the package gave a header is headed by it (Select: SEL) and still writes its one character into the movie; the others are headed by their letters"
		else
			report "E:column-headers" FAIL "$(tr '\t\n' ' ;' < "$work/headers.controls" 2>/dev/null | cut -c1-160) (see work/headers.*)"
		fi

		# StateSaving, the export a core is told with BEFORE every state the
		# engine takes of it (engine: ce_session::stateSaving; chimera#190 -
		# xemu writes the surfaces its GPU drew into the console's RAM there,
		# and without it a load loses them). The synth notes the frame it was
		# last told on in memory a state carries, and hands that out through
		# its Mailbox with the frames it has run and the times it was told:
		#   - a machine nobody stores is never told (0 times in 70 frames);
		#   - a frame is told when it is STORED and not otherwise: 71 times
		#     with a state on every frame (the anchor and 70), 15 with one in
		#     five;
		#   - back on frame 32 from the state of frame 30, the note says 30:
		#     the state holds the note of its own frame, so the core was told
		#     before the state was read and what it wrote is in the delta (told
		#     after, it would say 25);
		#   - and with CHIMERA_NO_STATE_SAVING=1, the control the measurements
		#     use, it is never told at all.
		told() { # name, then chimera-run's arguments -> "told-at frames-run times-told"
			tname="$1"; shift
			"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--dump "Mailbox=$work/told.$tname.bin" "$@" > "$work/told.$tname.log" 2>&1
			od -A n -t u4 -j 4 -N 12 "$work/told.$tname.bin" 2>/dev/null | tr -s ' ' | sed 's/^ //; s/ $//'
		}
		toldNone="$(told none)"
		toldEvery="$(told every --greenzone-period 1 --greenzone-max-stride 1)"
		toldFifth="$(told fifth --greenzone-period 5)"
		toldBack="$(told back --greenzone-period 5 --seek 32 --stop-at-seek)"
		toldOff="$(export CHIMERA_NO_STATE_SAVING=1; told off --greenzone-period 5 --seek 32 --stop-at-seek)"
		if [ "$toldNone" = "0 70 0" ] && [ "$toldEvery" = "70 70 71" ] && [ "$toldFifth" = "70 70 15" ] \
			&& [ "$toldBack" = "30 32 15" ] && [ "$toldOff" = "0 32 0" ]; then
			report "E:state-saving" PASS "a core is told before each state taken of it and for no frame that is not stored (71 times at every frame, 15 at one in five, never without a history), and a restored state holds what it wrote then"
		else
			report "E:state-saving" FAIL "told-at/frames/times: none '$toldNone' every '$toldEvery' fifth '$toldFifth' back '$toldBack' off '$toldOff' (see work/told.*)"
		fi

		# a DYNAMIC property table (engine.h; chimera#216): with its
		# movingProperties setting on, the synth lists properties that move
		# and come and go with the cursor's row, the way a Flash movie's
		# variables do on its emulator's heap (package-box/synth_wbx.c says
		# where each is). chimera-run asks for each by name after every frame
		# of a whole game, as a watch does, and what it prints is checked
		# against the rule the core states, not against a recording:
		#   Wanderer is at 0x200 + 4 * Row, every frame;
		#   Sometimes is there exactly while Row is odd;
		#   Mirror, on a bus with no pointer, reads what the bus says;
		#   a 77 written to Wanderer before the first frame (Row 0) stays at
		#   0x200 - Origin - and Wanderer, once it has moved on, does not have it;
		#   the table says it is dynamic, and a fresh listing has Sometimes
		#   or not by the last row;
		#   a game's own names are exact (chimera#218): "wanderer" is another
		#   property than Wanderer, at 0x244 every frame, and "WANDERER",
		#   which the core does not have, is never there.
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--settings '{"movingProperties":1}' --property-set Wanderer=77 \
			--property-trace Row,Steps,Wanderer,Sometimes,Mirror,Origin,wanderer,WANDERER > "$work/dynamic.out" 2>"$work/dynamic.err"
		if python3 - "$work/dynamic.out" > "$work/dynamic.check" 2>&1 <<'DYNAMIC'
import json, sys
frames, table, listed, set_line = {}, None, None, None
for line in open(sys.argv[1]):
    w = line.split()
    if line.startswith("prop set "): set_line = line.strip()
    elif line.startswith("prop table: {"): table = json.loads(line[len("prop table: "):])
    elif line.startswith("prop table: "): listed = line.strip()
    elif w and w[0] == "prop":
        frames.setdefault(int(w[1]), {})[w[2]] = None if w[3] == "gone" else (int(w[4]), int(w[6]))
assert set_line == "prop set Wanderer: done", set_line
assert len(frames) >= 60, "only %d frames traced" % len(frames)
rows = set()
for f, p in sorted(frames.items()):
    row, steps = p["Row"][1], p["Steps"][1]
    rows.add(row)
    assert p["Wanderer"] is not None and p["Wanderer"][0] == 0x200 + 4 * row, "frame %d: row %d, Wanderer %r" % (f, row, p["Wanderer"])
    assert (p["Sometimes"] is not None) == (row % 2 == 1), "frame %d: row %d, Sometimes %r" % (f, row, p["Sometimes"])
    assert p["Mirror"] == (4, (steps & 255) ^ 28), "frame %d: %d steps, Mirror %r" % (f, steps, p["Mirror"])
    assert p["Origin"] == (0x200, 77), "frame %d: Origin %r" % (f, p["Origin"])
    assert p["Wanderer"][1] == (77 if row == 0 else 0), "frame %d: row %d, Wanderer reads %d" % (f, row, p["Wanderer"][1])
    assert p["wanderer"] is not None and p["wanderer"][0] == 0x244, "frame %d: wanderer %r is not its own property" % (f, p["wanderer"])
    assert p["WANDERER"] is None, "frame %d: WANDERER, which the core does not have, read %r" % (f, p["WANDERER"])
assert len(rows) >= 4, "the cursor only visited rows %r" % sorted(rows)
last = frames[max(frames)]["Row"][1]
names = {p["name"]: p for p in table["properties"]}
assert table.get("dynamic") is True and listed == "prop table: dynamic, %d listed after the run" % (7 if last % 2 else 6), listed
assert "wanderer" in names and "Wanderer" in names and names["wanderer"]["offset"] == 0x244, sorted(names)
assert names["Sometimes"]["listed"] == (last % 2 == 1) and names["Sometimes"]["present"] == (last % 2 == 1), names["Sometimes"]
assert names["Mirror"]["writable"] is False and names["Wanderer"]["offset"] == 0x200 + 4 * last
print("%d frames over rows %s" % (len(frames), sorted(rows)))
DYNAMIC
		then
			report "E:dynamic-properties" PASS "a property that moves is followed by name, one that goes is gone, one on a bus reads the bus ($(cat "$work/dynamic.check"))"
		else
			report "E:dynamic-properties" FAIL "$(tail -1 "$work/dynamic.check") (see work/dynamic.out)"
		fi

		# the history OUTLIVES its process, which is what reopening a project
		# asks of it. One run plays the movie and keeps its history to a file; a
		# second, fresh process starts from that file, seeks back into states it
		# did not make, and replays. The dumps must still be the goldens. This is
		# the leg the old greenzone would have failed silently: it serialized
		# itself through an array that stops near 2GB, so a long history simply
		# did not save and nothing said so.
		for movie in "$here"/movies/*.txt; do
			hname="$(basename "$movie" .txt)"
			hrom="$here/roms/${hname%%.*}.testrom"
			htag="$hname.engine.history"
			rm -f "$work/$htag.ram.bin" "$work/$htag.vram.bin" "$work/$htag.hist"
			"$chimera_run" "$epkg" "$hrom" "$movie" --history-out "$work/$htag.hist" 				> "$work/$htag.save.log" 2>&1
			"$chimera_run" "$epkg" "$hrom" "$movie" --history-in "$work/$htag.hist" --seek 10 				--dump "RAM=$work/$htag.ram.bin" --dump "VRAM=$work/$htag.vram.bin" 				> "$work/$htag.log" 2>&1
			if [ ! -s "$work/$htag.hist" ]; then
				report "E:$hname:history" FAIL "nothing was written (see work/$htag.save.log)"
			elif cmp -s "$work/$htag.ram.bin" "$golden_dir/$hname.ram.bin" 				&& cmp -s "$work/$htag.vram.bin" "$golden_dir/$hname.vram.bin"; then
				report "E:$hname:history" PASS "a history kept across processes seeks to the goldens"
			else
				report "E:$hname:history" FAIL "RAM or VRAM differs after reloading the history (see work/$htag.log)"
			fi
		done

		# THINNED: a greenzone over its memory budget gives frames up toward its
		# shape (bands doubling back from the frontier), and a frame it keeps can
		# be one that other frames were COMPOSED into. Seeking to such a frame has
		# to land on the machine a straight run reaches there.
		#
		# The comparison is against a run stopped at that frame, not against the
		# goldens at the end of the movie, and that distinction is the whole leg:
		# this core's ending is decided by its inputs, so a replay from a WRONG
		# frame still finishes on the goldens and proves nothing.
		#
		# The target is chosen from the history itself: a frame the trace says a
		# landing was merged into AND that the finished history still holds. A
		# frame thinned away is reached by restoring something earlier and
		# replaying, which is correct however broken the merging is - so seeking
		# to one would prove nothing either. A run that never merged fails.
		#
		# The budget is in bytes, since this machine weighs under a megabyte, and
		# it is taken from the movie itself: 40% of what its history weighs kept
		# whole (the movies differ - one is 21 frames and 286 KB, which a fixed
		# budget never made thin), with a goal of one snapshot a band.
		for movie in "$here"/movies/*.txt; do
			tname="$(basename "$movie" .txt)"
			trom="$here/roms/${tname%%.*}.testrom"
			ttag="$tname.engine.thinned"
			rm -f "$work/$ttag.ram.bin" "$work/$ttag.ref.ram.bin" "$work/$ttag.map"
			CHIMERA_HISTORY_TRACE=1 "$chimera_run" "$epkg" "$trom" "$movie" \
				--greenzone-map "$work/$ttag.whole.map" > "$work/$ttag.whole.log" 2>&1
			whole="$(grep -a -o '[0-9]* total' "$work/$ttag.whole.log" | tail -1 | awk '{print $1}')"
			if [ -z "$whole" ]; then
				report "E:$tname:thinned" FAIL "could not weigh the history kept whole (see work/$ttag.whole.log)"
				continue
			fi
			thin="--greenzone-bytes $((whole * 4 / 10)) --greenzone-band-goal 1"
			CHIMERA_HISTORY_TRACE=1 "$chimera_run" "$epkg" "$trom" "$movie" $thin \
				--greenzone-map "$work/$ttag.map" > "$work/$ttag.plan.log" 2>&1
			target=""
			for into in $(grep -a -o "merged the landing at [0-9]* into [0-9]*" "$work/$ttag.plan.log" | awk '{print $NF}' | sort -n -u); do
				if grep -qx "$into" "$work/$ttag.map" 2>/dev/null && [ "$into" -gt 0 ]; then target="$into"; fi
			done
			if [ -z "$target" ]; then
				report "E:$tname:thinned" FAIL "nothing it kept was ever merged into (see work/$ttag.plan.log)"
				continue
			fi
			"$chimera_run" "$epkg" "$trom" "$movie" --frames "$target" \
				--dump "RAM=$work/$ttag.ref.ram.bin" > "$work/$ttag.ref.log" 2>&1
			CHIMERA_HISTORY_TRACE=1 "$chimera_run" "$epkg" "$trom" "$movie" --seek "$target" --stop-at-seek $thin \
				--dump "RAM=$work/$ttag.ram.bin" > "$work/$ttag.log" 2>&1
			if [ ! -s "$work/$ttag.ref.ram.bin" ]; then
				report "E:$tname:thinned" FAIL "the reference run wrote nothing (see work/$ttag.ref.log)"
			elif cmp -s "$work/$ttag.ram.bin" "$work/$ttag.ref.ram.bin"; then
				report "E:$tname:thinned" PASS "seeking a thinned history lands on the real frame $target, a merged one"
			else
				report "E:$tname:thinned" FAIL "RAM at frame $target differs from a straight run (see work/$ttag.log)"
			fi
		done

		# RECORD mode: playback never generates an entry, so the paths that turn
		# machine input back into movie text have no other witness. Each movie is
		# replayed as an INPUT SOURCE into a recording session, which writes its
		# own log; that recording must drive the machine to the same goldens, and
		# replaying the file it wrote must land there too - record and playback
		# agreeing on the format is the whole point.
		for movie in "$here"/movies/*.txt; do
			rname="$(basename "$movie" .txt)"
			rrom="$here/roms/${rname%%.*}.testrom"
			rtag="$rname.engine.record"
			rm -f "$work/$rtag.ram.bin" "$work/$rtag.vram.bin" "$work/$rtag.txt"
			"$chimera_run" "$epkg" "$rrom" "$movie" --record "$work/$rtag.txt" \
				--dump "RAM=$work/$rtag.ram.bin" --dump "VRAM=$work/$rtag.vram.bin" \
				> "$work/$rtag.log" 2>&1
			if [ ! -s "$work/$rtag.txt" ]; then
				report "E:$rname:record" FAIL "no movie recorded (see work/$rtag.log)"
			elif ! cmp -s "$work/$rtag.ram.bin" "$golden_dir/$rname.ram.bin" \
				|| ! cmp -s "$work/$rtag.vram.bin" "$golden_dir/$rname.vram.bin"; then
				report "E:$rname:record" FAIL "recording drove a different machine than playback"
			else
				rm -f "$work/$rtag.replay.ram.bin" "$work/$rtag.replay.vram.bin"
				"$chimera_run" "$epkg" "$rrom" "$work/$rtag.txt" \
					--dump "RAM=$work/$rtag.replay.ram.bin" --dump "VRAM=$work/$rtag.replay.vram.bin" \
					> "$work/$rtag.replay.log" 2>&1
				if cmp -s "$work/$rtag.replay.ram.bin" "$golden_dir/$rname.ram.bin" \
					&& cmp -s "$work/$rtag.replay.vram.bin" "$golden_dir/$rname.vram.bin"; then
					report "E:$rname:record" PASS "recorded log replays to the same goldens"
				else
					report "E:$rname:record" FAIL "the recorded movie replays to a different machine"
				fi
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
	emu_exe="$repo_root/build/Chimera.exe"

	export LD_LIBRARY_PATH="$repo_root/build/dll:$repo_root/build:/usr/lib/x86_64-linux-gnu"
	export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
	# The frontend keeps a project's greenzone in the per-user cache now, not
	# beside the project, so without this the witness would leave its states in
	# the real one. XDG_DATA_HOME and not CHIMERA_DATA_HOME: the latter moves the
	# WHOLE data directory, tools and all, and the encode leg then cannot find
	# ffmpeg. This moves only where per-user data is kept.
	export XDG_DATA_HOME="$work/data-home"
	xvfb_pid=""
	cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
	trap cleanup EXIT
	# A DISPLAY that is set but does not answer - an ssh session's forwarded
	# display with nothing behind it, which is what the WSL dev box has - would
	# fail every frontend leg with "Could not open display", fourteen times over.
	# Ask it first, and bring up an Xvfb if it says nothing.
	if [ -n "${DISPLAY:-}" ] && command -v xdpyinfo >/dev/null && ! timeout 5 xdpyinfo >/dev/null 2>&1; then
		echo "DISPLAY=$DISPLAY does not answer; starting an Xvfb instead" >&2
		unset DISPLAY
	fi
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
		( cd "$repo_root" && timeout 120 mono "$emu_exe" --headless "--config=$config" \
			"--lua=$here/bootstrap-exit.lua" ) > "$work/bootstrap.log" 2>&1
		[ -f "$config" ] || { echo "config bootstrap failed (see $work/bootstrap.log)" >&2; exit 1; }
	fi
	# Chimera is waterbox-only: the frontend only accepts .wbx cores, so the box
	# flavor is the ONLY Level-B core. native/sharp are Level-A equivalence
	# references (proving synth.wbx matches the goldens), never frontend cores.
	for flavor in box; do
		package="$repo_root/build/Cores/synth-$flavor.chimeraCore"
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
				( cd "$repo_root" && CHIMERA_JOB="$job" timeout 300 mono "$emu_exe" --headless \
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
# the core's own settings key, spelled exactly as the UI writes it
cfg.setdefault("CoreSettings", {})["Chimera.Emulation.Common.Waterbox.WaterboxCore"] = {"Values": {"initFillByte": 171}}
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
		( cd "$repo_root" && CHIMERA_JOB="$sjob" timeout 300 mono "$emu_exe" --headless \
			"--config=$scfg" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--lua=$here/synth-replay.lua" "$srom" ) > "$work/settings.log" 2>&1
		if [ ! -f "$work/settings.meta.txt" ] || ! grep -q "^status=OK" "$work/settings.meta.txt"; then
			report "S:box:initFillByte" FAIL "no OK meta (see work/settings.log)"
		elif cmp -s "$work/settings.ram.bin" "$golden_dir/$sname.ram.bin"; then
			report "S:box:initFillByte" FAIL "RAM identical to golden - setting did NOT reach the guest"
		else
			report "S:box:initFillByte" PASS "RAM diverged - user setting reached the guest"
		fi
	fi

	# --- a real .chimeraProject through the real movie pipeline ---
	# Everything above drives input by script; this composes an actual project
	# file from the win inputs and lets MovieSession play it (the project IS
	# the movie, docs/project.md). The engine parses the entries, the frontend
	# latches them, and the dumps must match the same goldens - the movie
	# pipeline is Level B's reason to exist.
	if [ "$record" -eq 0 ]; then
		mname=gridWalker.win
		mrom="$here/roms/${mname%%.*}.testrom"
		mbk2="$work/$mname.chimeraProject"
		python3 - "$here/movies/$mname.txt" "$mbk2" <<'PY'
import json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
json.dump({
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[2], "w"))
PY
		mjob="$work/job.movie.txt"
		{
			echo "outram=$work/movie.ram.bin"
			echo "outvram=$work/movie.vram.bin"
			echo "meta=$work/movie.meta.txt"
		} > "$mjob"
		rm -f "$work/movie.ram.bin" "$work/movie.vram.bin" "$work/movie.meta.txt"
		cp "$config" "$work/config.movie.ini"
		( cd "$repo_root" && CHIMERA_JOB="$mjob" timeout 300 mono "$emu_exe" --headless \
			"--config=$work/config.movie.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--movie=$mbk2" "--lua=$here/synth-movie-dump.lua" "$mrom" ) > "$work/movie.log" 2>&1
		if [ ! -f "$work/movie.meta.txt" ] || ! grep -q "^status=OK" "$work/movie.meta.txt"; then
			report "M:box:$mname" FAIL "no OK meta (see work/movie.log)"
		elif cmp -s "$work/movie.ram.bin" "$golden_dir/$mname.ram.bin" \
			&& cmp -s "$work/movie.vram.bin" "$golden_dir/$mname.vram.bin"; then
			report "M:box:$mname" PASS "MovieSession playback byte-identical to goldens"
		else
			report "M:box:$mname" FAIL "RAM or VRAM differs from goldens (see work/movie.log)"
		fi
	fi

	# --- the project entry point: ONE boot, project settings honored ---
	# Chimera's whole point vs its lineage: opening a project must
	# init the core EXACTLY ONCE (rom load, TAStudio open and tasproj load
	# each cost a full boot there), and the boot must run with the
	# project's OWN sync settings - not a config default. One leg pins
	# both: a project carrying initFillByte=171 plays the win movie; the
	# final RAM must DIVERGE from the golden (the setting reached the
	# guest through the project path) and the log must show exactly one
	# "[waterbox] booting" line.
	if [ "$record" -eq 0 ]; then
		pname=gridWalker.win
		prom="$here/roms/${pname%%.*}.testrom"
		pdir="$work/project-leg"
		rm -rf "$pdir" && mkdir -p "$pdir"
		cp "$prom" "$pdir/gridWalker.testrom"
		python3 - "$here/movies/$pname.txt" "$pdir/gridWalker.testrom" "$pdir/$pname.chimeraProject" <<'PY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "settings": {"initFillByte": 171},
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
PY
		pjob="$work/job.project.txt"
		{
			echo "outram=$work/project.ram.bin"
			echo "outvram=$work/project.vram.bin"
			echo "meta=$work/project.meta.txt"
		} > "$pjob"
		rm -f "$work/project.ram.bin" "$work/project.vram.bin" "$work/project.meta.txt"
		cp "$config" "$work/config.project.ini"
		( cd "$repo_root" && CHIMERA_JOB="$pjob" timeout 300 mono "$emu_exe" --headless \
			"--config=$work/config.project.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$pdir/$pname.chimeraProject" "--lua=$here/synth-movie-dump.lua" ) > "$work/project.log" 2>&1
		boots="$(grep -c "\[waterbox\] booting" "$work/project.log" || true)"
		if [ ! -f "$work/project.meta.txt" ] || ! grep -q "^status=OK" "$work/project.meta.txt"; then
			report "P:box:$pname" FAIL "no OK meta (see work/project.log)"
		elif [ "$boots" != "1" ]; then
			report "P:box:$pname" FAIL "core booted $boots times; a project open boots EXACTLY once"
		elif cmp -s "$work/project.ram.bin" "$golden_dir/$pname.ram.bin"; then
			report "P:box:$pname" FAIL "RAM matches the default golden - the project's initFillByte never reached the guest"
		else
			report "P:box:$pname" PASS "one boot, project settings live in the guest"
		fi
	fi

	# --- a script's input, while TAStudio extends the movie past its end ---
	# The piano roll open is a different frame loop, and past the end of the log
	# it is not replaying anything: each frame out there is AUTHORED as it is
	# reached. A seek turns recording off for its duration so the rows it passes
	# over are not typed over - and out past the end, where there are no rows to
	# protect, that used to throw away everything being pressed: the frames were
	# written from the autoholds alone, so a script's joypad.set reached neither
	# the movie nor the machine (issue #95).
	#
	# Three runs pin both halves of the rule. Recording asked for and a button
	# held must reach the log AND the core (the controller it is handed, and a
	# RAM that diverges from the run that held nothing); recording asked for and
	# nothing held must stay empty; and read-only play past the end must STILL
	# extend from the autoholds alone, whatever a script presses - nobody is
	# recording there.
	#
	# The project is the win movie cut to its first five frames, so the game is
	# still being played when the extension starts: gridWalker FREEZES once it
	# is won or lost (SPEC/rom), and a frozen machine ignores input, which would
	# make the RAM comparison prove nothing. Held from there, Down retraces the
	# winning path, so every extended frame is a real move.
	if [ "$record" -eq 0 ]; then
		tname=gridWalker.win
		trom="$here/roms/${tname%%.*}.testrom"
		tdir="$work/tastudio-input-leg"
		rm -rf "$tdir" && mkdir -p "$tdir"
		cp "$trom" "$tdir/gridWalker.testrom"
		python3 - "$here/movies/$tname.txt" "$tdir/gridWalker.testrom" "$tdir/$tname.chimeraProject" <<'TASPY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")][:5]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
TASPY
		textend=8
		tfailed=0
		for case in "held:1:1" "empty:1:0" "readonly:0:1"; do
			cname="${case%%:*}"; trest="${case#*:}"
			trecord="${trest%%:*}"; tpress="${trest#*:}"
			tjob="$work/job.tastudio-$cname.txt"
			{
				echo "meta=$tdir/$cname.meta.txt"
				echo "outram=$tdir/$cname.ram.bin"
				echo "extend=$textend"
				echo "button=P1 Down"
				echo "press=$tpress"
				echo "record=$trecord"
			} > "$tjob"
			rm -f "$tdir/$cname.meta.txt" "$tdir/$cname.ram.bin"
			cp "$config" "$work/config.tastudio-$cname.ini"
			( cd "$repo_root" && CHIMERA_JOB="$tjob" timeout 300 mono "$emu_exe" --headless \
				"--config=$work/config.tastudio-$cname.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$tdir/$tname.chimeraProject" "--lua=$here/synth-tastudio-input.lua" ) > "$tdir/$cname.log" 2>&1
			if [ ! -f "$tdir/$cname.meta.txt" ] || ! grep -q "^status=OK" "$tdir/$cname.meta.txt"; then
				report "T:box:$cname" FAIL "$(sed -n 's/^detail=//p' "$tdir/$cname.meta.txt" 2>/dev/null || echo "run failed") (see $tdir/$cname.log)"
				tfailed=1
			fi
		done
		if [ "$tfailed" -eq 0 ]; then
			theld="$(sed -n 's/^recorded=//p' "$tdir/held.meta.txt")"
			theldout="$(sed -n 's/^machine=//p' "$tdir/held.meta.txt")"
			tempty="$(sed -n 's/^recorded=//p' "$tdir/empty.meta.txt")"
			tro="$(sed -n 's/^recorded=//p' "$tdir/readonly.meta.txt")"
			if [ "$theld" != "$textend" ]; then
				report "T:box:luaInput" FAIL "the held button reached $theld of $textend extended frames in the log"
			elif [ "$theldout" != "$textend" ]; then
				report "T:box:luaInput" FAIL "the log holds every press but the core was handed $theldout of $textend"
			elif cmp -s "$tdir/held.ram.bin" "$tdir/empty.ram.bin"; then
				report "T:box:luaInput" FAIL "RAM identical to the run that held nothing - the press never moved the machine"
			else
				report "T:box:luaInput" PASS "a script's press authors every extended frame, and the core plays it"
			fi
			if [ "$tempty" != "0" ]; then
				report "T:box:luaQuiet" FAIL "nothing was pressed, yet $tempty extended frames hold the button"
			elif [ "$tro" != "0" ]; then
				report "T:box:luaQuiet" FAIL "read-only play past the end wrote $tro pressed frames; only the autoholds may reach it"
			else
				report "T:box:luaQuiet" PASS "an unpressed frame stays empty, and read-only extension still takes only the autoholds"
			fi
		fi
	fi

	# --- TAStudio's Greenzone choice, saved with the project (issue #158) ---
	# The choice - every frame, one in N, off - is kept in TAStudio's part of the
	# .chimeraProject (user-decided, 2026-09-28). Two runs of one project: the
	# first opens it in TAStudio and saves it, which writes TAStudio's part with
	# the choice every movie starts on (1, every frame). The file is then set to
	# one in seven, as if it had been left there, and the second run opens and
	# saves it again. A project whose choice is read on opening writes the 7
	# back; one that ignored it writes the 1 TAStudio started on - which is what
	# every build before #158 did.
	if [ "$record" -eq 0 ]; then
		gdir="$work/greenzone-leg"
		rm -rf "$gdir" && mkdir -p "$gdir"
		cp "$tdir/gridWalker.testrom" "$gdir/gridWalker.testrom"
		cp "$tdir/$tname.chimeraProject" "$gdir/g.chimeraProject"
		gfailed=0
		for gpass in first second; do
			gjob="$work/job.greenzone-$gpass.txt"
			echo "meta=$gdir/$gpass.meta.txt" > "$gjob"
			cp "$config" "$work/config.greenzone-$gpass.ini"
			( cd "$repo_root" && CHIMERA_JOB="$gjob" timeout 300 mono "$emu_exe" --headless \
				"--config=$work/config.greenzone-$gpass.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$gdir/g.chimeraProject" "--lua=$here/synth-greenzone-choice.lua" ) > "$gdir/$gpass.log" 2>&1
			if ! grep -q "^status=OK" "$gdir/$gpass.meta.txt" 2>/dev/null; then
				report "T:box:greenzoneChoice" FAIL "the $gpass run: $(sed -n 's/^detail=//p' "$gdir/$gpass.meta.txt" 2>/dev/null || echo "run failed") (see $gdir/$gpass.log)"
				gfailed=1
				break
			fi
			if [ "$gpass" = first ]; then
				if ! python3 - "$gdir/g.chimeraProject" <<'GZPY'
import json, sys
p = json.load(open(sys.argv[1]))
# TAStudio's part is its settings object as ConfigService writes it: {"o": {...}}
t = (p.get("tastudio") or {}).get("o")
if not isinstance(t, dict) or t.get("GreenzonePeriod") != 1:
    sys.exit("TAStudio's part holds no GreenzonePeriod of 1: " + json.dumps(t)[:200])
t["GreenzonePeriod"] = 7
json.dump(p, open(sys.argv[1], "w"))
GZPY
				then
					report "T:box:greenzoneChoice" FAIL "the first save wrote no Greenzone choice into the project"
					gfailed=1
					break
				fi
			fi
		done
		if [ "$gfailed" -eq 0 ]; then
			gperiod="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("tastudio", {}).get("o", {}).get("GreenzonePeriod"))' "$gdir/g.chimeraProject")"
			if [ "$gperiod" = 7 ]; then
				report "T:box:greenzoneChoice" PASS "a project left on one in seven reopens on it and saves it back"
			else
				report "T:box:greenzoneChoice" FAIL "a project left on one in seven saved back $gperiod - the choice in the file was not read on opening"
			fi
		fi
	fi

	# --- a core's game properties, by name (docs/game-cores.md) ---
	# The synth exports a property table naming places in its RAM. A script reads
	# the names back in the core's order, reads a property against the same bytes
	# through memory.*, writes one by name and watches the game draw the cursor
	# where it was put.
	if [ "$record" -eq 0 ]; then
		pdir="$work/properties-leg"
		rm -rf "$pdir" && mkdir -p "$pdir"
		cp "$tdir/gridWalker.testrom" "$pdir/gridWalker.testrom"
		cp "$tdir/$tname.chimeraProject" "$pdir/p.chimeraProject"
		pjob="$work/job.properties.txt"
		echo "meta=$pdir/meta.txt" > "$pjob"
		cp "$config" "$work/config.properties.ini"
		( cd "$repo_root" && CHIMERA_JOB="$pjob" timeout 300 mono "$emu_exe" --headless \
			"--config=$work/config.properties.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$pdir/p.chimeraProject" "--lua=$here/synth-game-properties.lua" ) > "$pdir/run.log" 2>&1
		if grep -q "^status=OK" "$pdir/meta.txt" 2>/dev/null; then
			report "T:box:gameProperties" PASS "every kind of property reads and sets its own bytes by name, and the game plays on what was set"
		else
			report "T:box:gameProperties" FAIL "$(sed -n 's/^detail=//p' "$pdir/meta.txt" 2>/dev/null || echo "run failed") (see $pdir/run.log)"
		fi
	fi

	# --- a core that stops, in the real frontend ---
	# The synth core dies on cue (SPEC.md): all eight buttons abort, all but Up
	# follow a wild pointer - the second arrives as a SIGSEGV inside a process
	# whose runtime has signal handlers of its own. Headless Chimera must end the
	# run the ordinary way with the reason and HeadlessMode.EXIT_CODE_CORE_STOPPED
	# (65), not die in native code, and leave the recovery journal behind.
	if [ "$record" -eq 0 ]; then
		for cue in "abort:|UDLRABsS|:the core aborted" "wild:|.DLRABsS|:the core crashed: it wrote to address"; do
			cname="${cue%%:*}"; rest="${cue#*:}"; cline="${rest%%:*}"; cwant="${rest#*:}"
			cdir="$work/stops-$cname"
			rm -rf "$cdir" && mkdir -p "$cdir"
			cp "$here/roms/gridWalker.testrom" "$cdir/gridWalker.testrom"
			sed "12s/.*/$cline/" "$here/movies/gridWalker.win.txt" > "$cdir/movie.txt"
			python3 - "$cdir/movie.txt" "$cdir/gridWalker.testrom" "$cdir/stops.chimeraProject" <<'PY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "id": "0123456789abcdef0123456789abc0de",
    "title": "stops",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
PY
			cjob="$work/job.stops-$cname.txt"
			{
				echo "outram=$cdir/ram.bin"
				echo "outvram=$cdir/vram.bin"
				echo "meta=$cdir/meta.txt"
			} > "$cjob"
			cp "$config" "$work/config.stops-$cname.ini"
			( cd "$repo_root" && CHIMERA_JOB="$cjob" timeout 120 mono "$emu_exe" --headless \
				"--config=$work/config.stops-$cname.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$cdir/stops.chimeraProject" "--lua=$here/synth-movie-dump.lua" ) > "$cdir/log.txt" 2>&1
			crc=$?
			if grep -qE "Native Crash|Got a SIG|SIGSEGV while executing native" "$cdir/log.txt"; then
				if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
					# On the GitHub runner, and only there, Mono's unwinder crashes on the
					# stop (docs/design-principles.md, "A crash only the CI runner has").
					# Tracked, not waited on: everywhere else this is still a failure.
					report "S:frontend:$cname" KNOWN "CI runner only: Mono's unwinder crashed on the stop (see $cdir/log.txt)"
				else
					report "S:frontend:$cname" FAIL "the runtime reported a native crash (see $cdir/log.txt)"
				fi
			elif [ "$crc" -ne 65 ]; then
				report "S:frontend:$cname" FAIL "the process ended with $crc, not 65 (see $cdir/log.txt)"
			elif ! grep -q "\[headless\] The core stopped: $cwant" "$cdir/log.txt"; then
				report "S:frontend:$cname" FAIL "the stopped core was not reported (see $cdir/log.txt)"
			elif ! ls -d "$XDG_DATA_HOME"/*/Recovery/0123456789abcdef0123456789abc0de >/dev/null 2>&1 \
				&& ! find "$XDG_DATA_HOME" -type d -name 0123456789abcdef0123456789abc0de -path "*Recovery*" | grep -q .; then
				report "S:frontend:$cname" FAIL "the recovery journal was not left behind"
			else
				report "S:frontend:$cname" PASS "headless Chimera stops with the reason, lives, and keeps the journal"
			fi
			find "$XDG_DATA_HOME" -type d -name 0123456789abcdef0123456789abc0de -path "*Recovery*" -exec rm -rf {} + 2>/dev/null
		done
	fi

	# --- Reboot Core keeps the project ---
	# A reboot inside a project is the same project on a machine booted again
	# (issue #196). It used to replace the movie with a blank one when the piano
	# roll was open, and stop and dispose it when it was closed - and the
	# recovery session went on holding the movie that was gone, so the next
	# request to keep the work safe crashed the process. Three legs: the project
	# survives a reboot with the roll closed and with it open (same movie, same
	# file, power-on, the marker set before, and the golden RAM at the end), and
	# a core that dies AFTER a reboot is still survived, which is the request to
	# keep the work that used to crash.
	if [ "$record" -eq 0 ]; then
		for roll in 0 1; do
			rdir="$work/reboot-$roll"
			rm -rf "$rdir" && mkdir -p "$rdir"
			cp "$here/roms/gridWalker.testrom" "$rdir/gridWalker.testrom"
			python3 - "$here/movies/gridWalker.win.txt" "$rdir/gridWalker.testrom" "$rdir/reboot.chimeraProject" <<'PY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "id": "0123456789abcdef0123456789ab0196",
    "title": "reboot",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
PY
			rjob="$work/job.reboot-$roll.txt"
			{
				echo "outram=$rdir/ram.bin"
				echo "meta=$rdir/meta.txt"
				echo "tastudio=$roll"
				echo "rebootat=30"
			} > "$rjob"
			cp "$config" "$work/config.reboot-$roll.ini"
			( cd "$repo_root" && CHIMERA_JOB="$rjob" timeout 120 mono "$emu_exe" --headless \
				"--config=$work/config.reboot-$roll.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$rdir/reboot.chimeraProject" "--lua=$here/synth-reboot.lua" ) > "$rdir/log.txt" 2>&1
			rlabel="closed"; [ "$roll" -eq 1 ] && rlabel="open"
			if ! grep -q '^status=OK' "$rdir/meta.txt" 2>/dev/null; then
				report "R:frontend:roll-$rlabel" FAIL "$(grep '^detail=' "$rdir/meta.txt" 2>/dev/null | cut -c8- || true) (see $rdir/log.txt)"
			elif grep -q '\[recovery\]' "$rdir/log.txt"; then
				report "R:frontend:roll-$rlabel" FAIL "the recovery session lost the movie (see $rdir/log.txt)"
			elif ! cmp -s "$rdir/ram.bin" "$golden_dir/gridWalker.win.ram.bin"; then
				report "R:frontend:roll-$rlabel" FAIL "the rebooted machine did not play the project's inputs to the golden"
			else
				report "R:frontend:roll-$rlabel" PASS "the same movie on a rebooted machine, played to the golden"
			fi
			find "$XDG_DATA_HOME" -type d -name 0123456789abcdef0123456789ab0196 -path "*Recovery*" -exec rm -rf {} + 2>/dev/null
		done

		# the core dies on cue (all eight buttons, frame 12) after a reboot at
		# frame 5: the stop asks for the work to be kept, of a session whose
		# machine has been replaced once already
		ddir="$work/reboot-dies"
		rm -rf "$ddir" && mkdir -p "$ddir"
		cp "$here/roms/gridWalker.testrom" "$ddir/gridWalker.testrom"
		sed '12s/.*/|UDLRABsS|/' "$here/movies/gridWalker.win.txt" > "$ddir/movie.txt"
		python3 - "$ddir/movie.txt" "$ddir/gridWalker.testrom" "$ddir/reboot.chimeraProject" <<'PY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "id": "0123456789abcdef0123456789ab0196",
    "title": "reboot",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
PY
		djob="$work/job.reboot-dies.txt"
		{
			echo "outram=$ddir/ram.bin"
			echo "meta=$ddir/meta.txt"
			echo "tastudio=0"
			echo "rebootat=5"
		} > "$djob"
		cp "$config" "$work/config.reboot-dies.ini"
		( cd "$repo_root" && CHIMERA_JOB="$djob" timeout 120 mono "$emu_exe" --headless \
			"--config=$work/config.reboot-dies.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$ddir/reboot.chimeraProject" "--lua=$here/synth-reboot.lua" ) > "$ddir/log.txt" 2>&1
		drc=$?
		if grep -qE "Native Crash|Got a SIG|SIGSEGV while executing native" "$ddir/log.txt"; then
			if [ "${GITHUB_ACTIONS:-}" = "true" ]; then
				report "R:frontend:dies-after" KNOWN "CI runner only: Mono's unwinder crashed on the stop (see $ddir/log.txt)"
			else
				report "R:frontend:dies-after" FAIL "the runtime reported a native crash (see $ddir/log.txt)"
			fi
		elif ! grep -q "\[synth-reboot\] rebooted" "$ddir/log.txt"; then
			report "R:frontend:dies-after" FAIL "the core was never rebooted (see $ddir/log.txt)"
		elif [ "$drc" -ne 65 ]; then
			report "R:frontend:dies-after" FAIL "the process ended with $drc, not 65 (see $ddir/log.txt)"
		elif ! grep -q "\[headless\] The core stopped: the core aborted" "$ddir/log.txt"; then
			report "R:frontend:dies-after" FAIL "the stopped core was not reported (see $ddir/log.txt)"
		elif ! find "$XDG_DATA_HOME" -type d -name 0123456789abcdef0123456789ab0196 -path "*Recovery*" | grep -q .; then
			report "R:frontend:dies-after" FAIL "the recovery journal was not left behind"
		else
			report "R:frontend:dies-after" PASS "a core that dies after a reboot is survived, and the work is kept"
		fi
		find "$XDG_DATA_HOME" -type d -name 0123456789abcdef0123456789ab0196 -path "*Recovery*" -exec rm -rf {} + 2>/dev/null
	fi

	# --- a run becomes a video ---
	# Encode Video, end to end: part of a real run reproduced into a real file
	# through the real writer. The checks are the ones that used to be a person's
	# job when this was three separate commands - is the file there, does it hold
	# EXACTLY the frames that were asked for (both ends included), and was the
	# emulator put back where it was found.
	if [ "$record" -eq 0 ]; then
		ename=gridWalker.win
		erom="$here/roms/${ename%%.*}.testrom"
		edir="$work/encode-leg"
		rm -rf "$edir" && mkdir -p "$edir"
		cp "$erom" "$edir/gridWalker.testrom"
		python3 - "$here/movies/$ename.txt" "$edir/gridWalker.testrom" "$edir/$ename.chimeraProject" <<'ENCPY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
ENCPY
		evideo="$work/encode.mkv"
		efrom=10
		eto=49
		eframes=$((eto - efrom + 1))
		ejob="$work/job.encode.txt"
		{
			echo "out=$evideo"
			echo "meta=$work/encode.meta.txt"
			echo "from=$efrom"
			echo "to=$eto"
			# lossless, so a frame that came out wrong cannot be blamed on a codec
			echo "command=-c:a pcm_s16le -c:v ffv1 -pix_fmt bgr0 -level 1 -g 1 -f matroska"
		} > "$ejob"
		rm -f "$evideo" "$work/encode.meta.txt"
		cp "$config" "$work/config.encode.ini"

		# ffmpeg is part of a build now, not something a person is asked to fetch
		# the first time they want a video - so the gate builds it in too.
		if ! "$repo_root/tools/fetch-ffmpeg.sh" linux "$repo_root/build/dll" > "$work/ffmpeg.log" 2>&1; then
			report "V:box:encode" FAIL "no ffmpeg to encode with (see work/ffmpeg.log)"
		else
			( cd "$repo_root" && CHIMERA_JOB="$ejob" timeout 300 mono "$emu_exe" --headless \
				"--config=$work/config.encode.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$edir/$ename.chimeraProject" "--lua=$here/synth-encode.lua" ) > "$work/encode.log" 2>&1
			# counted with the ffmpeg this build ships rather than a system
			# ffprobe: the gate must not depend on a tool the machine happens to
			# have, and a CI runner does not have one. The progress line ffmpeg
			# writes while decoding to nowhere ends with the frame it reached.
			ecount=""
			if [ -s "$evideo" ]; then
				ecount="$("$repo_root/build/dll/ffmpeg" -hide_banner -nostdin -i "$evideo" -f null - 2>&1 \
					| tr "\r" "\n" | sed -n "s/^frame= *\([0-9][0-9]*\).*/\1/p" | tail -1)"
			fi
			ebefore="$(sed -n "s/^before=//p" "$work/encode.meta.txt" 2>/dev/null)"
			eafter="$(sed -n "s/^after=//p" "$work/encode.meta.txt" 2>/dev/null)"
			if [ ! -f "$work/encode.meta.txt" ] || ! grep -q "^status=OK" "$work/encode.meta.txt"; then
				report "V:box:encode" FAIL "$(sed -n "s/^detail=//p" "$work/encode.meta.txt" 2>/dev/null || echo "run failed") (see work/encode.log)"
			elif [ ! -s "$evideo" ]; then
				report "V:box:encode" FAIL "no video was written (see work/encode.log)"
			elif [ "$ecount" != "$eframes" ]; then
				report "V:box:encode" FAIL "video holds ${ecount:-?} frames, asked for $eframes ($efrom to $eto inclusive)"
			elif [ "$ebefore" != "$eafter" ]; then
				report "V:box:encode" FAIL "emulator was left on frame $eafter, not the $ebefore it was borrowed from"
			else
				report "V:box:encode" PASS "$eframes frames written, emulator handed back on frame $eafter"
			fi
		fi
	fi

	# --- a second project, over a running one ---
	# The shape that used to throw: booting over a live session ran the old
	# machine's close inside the new one's load, where it took the movie the load
	# had just queued. A session has to end before the next one begins, and the
	# machine left behind has to be the SECOND project's.
	if [ "$record" -eq 0 ]; then
		rdir="$work/reopen-leg"
		rm -rf "$rdir" && mkdir -p "$rdir"
		make_project() { # <movie name> <project path>
			cp "$here/roms/${1%%.*}.testrom" "$rdir/${1%%.*}.testrom"
			python3 - "$here/movies/$1.txt" "$rdir/${1%%.*}.testrom" "$2" <<'REOPENPY'
import hashlib, json, sys
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
json.dump({
    "title": sys.argv[1],
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": sys.argv[2].split("/")[-1], "sha1": sha1, "slot": "rom"}],
    "input": "[Input]\nLogKey:" + logkey + "\n" + "\n".join(entries) + "\n[/Input]\n",
}, open(sys.argv[3], "w"))
REOPENPY
		}
		make_project gridWalker.win "$rdir/first.chimeraProject"
		make_project gridWalker.lose "$rdir/second.chimeraProject"
		firstlen="$(grep -c '^|' "$here/movies/gridWalker.win.txt")"
		secondlen="$(grep -c '^|' "$here/movies/gridWalker.lose.txt")"

		rjob="$work/job.reopen.txt"
		{
			echo "second=$rdir/second.chimeraProject"
			echo "meta=$work/reopen.meta.txt"
		} > "$rjob"
		rm -f "$work/reopen.meta.txt" "$work/reopen.ram.bin"
		cp "$config" "$work/config.reopen.ini"
		( cd "$repo_root" && CHIMERA_JOB="$rjob" timeout 300 mono "$emu_exe" --headless \
			"--config=$work/config.reopen.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$rdir/first.chimeraProject" "--lua=$here/synth-reopen.lua" ) > "$work/reopen.log" 2>&1
		got_first="$(sed -n 's/^firstlength=//p' "$work/reopen.meta.txt" 2>/dev/null)"
		got_second="$(sed -n 's/^secondlength=//p' "$work/reopen.meta.txt" 2>/dev/null)"
		if [ ! -f "$work/reopen.meta.txt" ] || ! grep -q "^status=OK" "$work/reopen.meta.txt"; then
			report "R:box:reopen" FAIL "$(sed -n 's/^detail=//p' "$work/reopen.meta.txt" 2>/dev/null || echo "run failed") (see work/reopen.log)"
		elif [ "$got_first" != "$firstlen" ]; then
			report "R:box:reopen" FAIL "the first project was $got_first frames, not $firstlen"
		elif [ "$got_second" != "$secondlen" ]; then
			report "R:box:reopen" FAIL "after reopening, the movie is $got_second frames; the second project is $secondlen"
		else
			report "R:box:reopen" PASS "a project opened over a running one, and the second is what is loaded"
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
		( cd "$repo_root" && CHIMERA_JOB="$djob" timeout 300 mono "$emu_exe" --headless \
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
		# the config Chimera writes on exit must hold the package's bindings, or the core arrives
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
		( cd "$repo_root" && CHIMERA_JOB="$kjob" timeout 300 mono "$emu_exe" --headless \
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
if [ "$known" -gt 0 ]; then echo "$ok ok, $failed failed, $known known"; else echo "$ok ok, $failed failed"; fi
[ "$failed" -gt 0 ] && exit 1
exit 0
