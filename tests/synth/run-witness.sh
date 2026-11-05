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
# Level B (frontend): Chimera's window, run headless, loads the core.wbx package
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
# tests read and write projects through project_file.py (a project is a zip)
export PYTHONPATH="$here${PYTHONPATH:+:$PYTHONPATH}"
repo_root="$(cd "$here/../.." && pwd)"
golden_dir="$here/goldens"
work="$here/work"
mkdir -p "$golden_dir" "$work"
# A game runs only in a project (user-decided, 2026-10-26): crun_game
# <package> <game> <movie> [chimera-run's options] makes one of the game and
# the movie with chimera-run --new-project, with any --settings and
# --firmware given, and runs it with the rest (each run under CRUN_TIMEOUT
# seconds when it is set).
crun_quote() { printf "'%s'" "$(printf '%s' "$1" | sed "s/'/'\\\\''/g")"; }
crun_game() {
	cg_pkg="$1" cg_game="$2" cg_movie="$3" cg_made=""
	shift 3
	cg_n=$#
	while [ "$cg_n" -gt 0 ]; do
		case "$1" in
			--settings) cg_made="$cg_made --settings $(crun_quote "$2")"; shift 2; cg_n=$((cg_n - 2)); continue ;;
			--settings=*) cg_made="$cg_made --settings $(crun_quote "${1#--settings=}")"; shift; cg_n=$((cg_n - 1)); continue ;;
			--firmware) cg_made="$cg_made --firmware $(crun_quote "$2")"; set -- "$@" "$1" "$2"; shift 2; cg_n=$((cg_n - 2)); continue ;;
			--firmware=*) cg_made="$cg_made --firmware $(crun_quote "${1#--firmware=}")"; set -- "$@" "$1"; shift; cg_n=$((cg_n - 1)); continue ;;
		esac
		set -- "$@" "$1"; shift; cg_n=$((cg_n - 1))
	done
	cg_dir="$(mktemp -d "$work/project.XXXXXX")"
	eval "timeout \"\${CRUN_TIMEOUT:-0}\" \"\$chimera_run\" --new-project \"\$cg_dir/game.chimeraProject\" \"\$cg_pkg\" \"\$cg_game\" \"\$cg_movie\" $cg_made" \
		> "$cg_dir/make.log" 2>&1 || { echo "chimera-run: no project made: $(tail -1 "$cg_dir/make.log")" >&2; return 2; }
	timeout "${CRUN_TIMEOUT:-0}" "$chimera_run" --project "$cg_dir/game.chimeraProject" "$cg_pkg" --files "$(dirname "$cg_game")" "$@"
}
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
			# the core's own line on its standard output (see synth_wbx.c's
			# Init) reaches the box runner's; the native runner has no Init
			sed -i '/^synth: started$/d' "$work/$name.box.simple.out" "$work/$name.box.rerecord.out"
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
# movies with NO frontend at all - no window, no display. Its dumps must match
# the Level A goldens, in both modes, and the settings channel must reach the
# guest: proof that it IS the same machine.
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
				crun_game "$epkg" "$erom" "$movie" $eextra 					--dump "RAM=$work/$etag.ram.bin" --dump "VRAM=$work/$etag.vram.bin" 					--meta "$work/$etag.meta.txt" > "$work/$etag.log" 2>&1
				if [ ! -f "$work/$etag.meta.txt" ] || ! grep -q "^status=OK" "$work/$etag.meta.txt"; then
					report "E:$ename:$emode" FAIL "no OK meta (see work/$etag.log)"
				elif cmp -s "$work/$etag.ram.bin" "$golden_dir/$ename.ram.bin" 					&& cmp -s "$work/$etag.vram.bin" "$golden_dir/$ename.vram.bin"; then
					report "E:$ename:$emode" PASS "RAM+VRAM byte-identical to level A goldens"
				else
					report "E:$ename:$emode" FAIL "RAM or VRAM differs from level A goldens"
				fi
			done
		done
		# what the game calls a control reaches the engine beside the control's
		# own name: the synthetic core calls "P1 A" Jump and nothing else anything
		lmeta="$(ls "$work"/*.engine.simple.meta.txt 2>/dev/null | head -1)"
		if [ -n "$lmeta" ] && grep -qx 'label=P1 A=Jump' "$lmeta" && [ "$(grep -c '^label=' "$lmeta")" = 1 ]; then
			report "E:labels" PASS "the core's name for a control (P1 A = Jump) is the engine's, and only for that control"
		else
			report "E:labels" FAIL "expected exactly label=P1 A=Jump in ${lmeta:-no meta}: $(grep '^label=' "$lmeta" 2>/dev/null | tr '\n' ' ')"
		fi
		# a movie's LogKey names its columns, and the engine reads them by name,
		# as the window does: the win movie with its columns written in reverse,
		# and a key saying so, must still reach the golden machine
		kname=gridWalker.win
		kmovie="$work/keyed-reversed.txt"
		python3 - "$here/movies/$kname.txt" "$kmovie" <<'KEYED'
import sys
names = ["P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 A", "P1 B", "P1 Select", "P1 Start"]
rows = ["|" + l.strip().strip("|")[::-1] + "|" for l in open(sys.argv[1]) if l.startswith("|")]
open(sys.argv[2], "w").write("[Input]\nLogKey:#" + "|".join(reversed(names)) + "|\n" + "\n".join(rows) + "\n[/Input]\n")
KEYED
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$kmovie" \
			--dump "RAM=$work/keyed.ram.bin" --dump "VRAM=$work/keyed.vram.bin" > "$work/keyed.log" 2>&1
		if cmp -s "$work/keyed.ram.bin" "$golden_dir/$kname.ram.bin" && cmp -s "$work/keyed.vram.bin" "$golden_dir/$kname.vram.bin"; then
			report "E:logkey" PASS "a movie's columns are read by the names its LogKey gives them, in any order"
		else
			report "E:logkey" FAIL "the reversed, keyed movie did not reach the golden machine (see work/keyed.log)"
		fi
		# the greenzone: play to the end, seek BACK mid-movie, invalidate (as an
		# input edit would), replay to the end - the dumps must not change
		for movie in "$here"/movies/*.txt; do
			gname="$(basename "$movie" .txt)"
			grom="$here/roms/${gname%%.*}.testrom"
			gtag="$gname.engine.seek"
			rm -f "$work/$gtag.ram.bin" "$work/$gtag.vram.bin"
			crun_game "$epkg" "$grom" "$movie" --seek 10 				--dump "RAM=$work/$gtag.ram.bin" --dump "VRAM=$work/$gtag.vram.bin" 				> "$work/$gtag.log" 2>&1
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
			crun_game "$epkg" "$srom" "$movie" --state-file-roundtrip 10 --save-state-file "10=$work/$stag.state" 				--meta "$work/$stag.meta.txt" --dump "RAM=$work/$stag.ram.bin" --dump "VRAM=$work/$stag.vram.bin" 				> "$work/$stag.log" 2>&1
			if cmp -s "$work/$stag.ram.bin" "$golden_dir/$sname.ram.bin" 				&& cmp -s "$work/$stag.vram.bin" "$golden_dir/$sname.vram.bin" && [ -s "$work/$stag.state" ]; then
				report "E:$sname:statefile" PASS "saved to a file and loaded back mid-movie, byte-identical"
			else
				report "E:$sname:statefile" FAIL "RAM or VRAM differs after a state-file round trip (see work/$stag.log)"
			fi
		done
		sfirst="$(ls "$here"/movies/*.txt | head -1)"; sname="$(basename "$sfirst" .txt)"; srom="$here/roms/${sname%%.*}.testrom"
		printf 'this is not a state' > "$work/not-a-state.state"
		crun_game "$epkg" "$srom" "$sfirst" --state-file "$work/not-a-state.state" > "$work/statefile-garbage.log" 2>&1; garbage_rc=$?
		head -c 300 "$work/$sname.engine.statefile.state" > "$work/cut-short.state"
		crun_game "$epkg" "$srom" "$sfirst" --state-file "$work/cut-short.state" > "$work/statefile-short.log" 2>&1; short_rc=$?
		crun_game "$epkg" "$srom" "$sfirst" --state-file "$work/$sname.engine.statefile.state" > "$work/statefile-good.log" 2>&1; good_rc=$?
		if [ "$garbage_rc" -ne 0 ] && [ "$garbage_rc" -lt 128 ] && grep -q "is not a state file" "$work/statefile-garbage.log" 			&& [ "$short_rc" -ne 0 ] && [ "$short_rc" -lt 128 ] 			&& [ "$good_rc" -eq 0 ] && grep -q "state file tag: frame 10" "$work/statefile-good.log"; then
			report "E:statefile:refusals" PASS "a whole file loads and hands its tag back; garbage and a cut-short file are refused, no crash"
		else
			report "E:statefile:refusals" FAIL "garbage rc=$garbage_rc short rc=$short_rc good rc=$good_rc (see work/statefile-*.log)"
		fi

		# A PROJECT, opened by the engine's one call for it
		# (ce_session_open_project), which chimera-run and the window both use.
		# The project's files are found beside it, its input is played, and the
		# machine must end where the reference dumps say. Then the project's own
		# settings must reach the machine, and each thing the call refuses must
		# be refused with its reason and without taking the process down: a file
		# that is not there, a file that is not the one recorded, a package that
		# is not the one the project names (allowed when the flag says so), and
		# files that do not fit the slots the package declares.
		jname=gridWalker.win
		jdir="$work/engine-project"
		rm -rf "$jdir" && mkdir -p "$jdir/ok" "$jdir/missing" "$jdir/changed" "$jdir/slots-package"
		cp "$here/roms/gridWalker.testrom" "$jdir/ok/gridWalker.testrom"
		head -c 64 "$here/roms/gridWalker.testrom" > "$jdir/changed/gridWalker.testrom"
		python3 - "$here/movies/$jname.txt" "$jdir/ok/gridWalker.testrom" "$jdir" <<'PROJPY'
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
def write(path, pin="", settings=None, firmware=None):
    doc = {
        "title": "gridWalker.win",
        "core": {"name": "Synth", "version": "", "sha1": pin},
        "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
        "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
        "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
    }
    if settings is not None: doc["settings"] = settings
    if firmware is not None: doc["firmware"] = firmware
    project_file.write(path, doc)
out = sys.argv[3]
write(out + "/ok/p.chimeraProject")
write(out + "/ok/settings.chimeraProject", settings={"initFillByte": 171})
write(out + "/ok/pinned.chimeraProject", pin="0123456789ABCDEF0123456789ABCDEF01234567")
write(out + "/ok/firmware.chimeraProject", firmware=[{"id": "bios", "sha1": sha1}])
write(out + "/missing/p.chimeraProject")
write(out + "/changed/p.chimeraProject")
PROJPY
		jrun() { # tag, then chimera-run's arguments; the exit code is left in jrc
			jtag="$1"; shift
			rm -f "$jdir/$jtag.ram.bin" "$jdir/$jtag.vram.bin"
			"$chimera_run" "$@" --dump "RAM=$jdir/$jtag.ram.bin" --dump "VRAM=$jdir/$jtag.vram.bin" > "$jdir/$jtag.log" 2>&1
			jrc=$?
		}
		jsame() { cmp -s "$jdir/$1.ram.bin" "$golden_dir/$jname.ram.bin" && cmp -s "$jdir/$1.vram.bin" "$golden_dir/$jname.vram.bin"; }
		jrefused() { [ "$jrc" -ne 0 ] && [ "$jrc" -lt 128 ] && grep -q "$2" "$jdir/$1.log"; }

		jrun plain --project "$jdir/ok/p.chimeraProject" "$epkg"
		if [ "$jrc" -eq 0 ] && jsame plain; then
			report "E:project:$jname" PASS "a project played through the engine's project call, RAM+VRAM byte-identical"
		else
			report "E:project:$jname" FAIL "rc=$jrc, or RAM or VRAM differs (see work/engine-project/plain.log)"
		fi
		jrun elsewhere --project "$jdir/missing/p.chimeraProject" "$epkg" --files "$jdir/ok"
		if [ "$jrc" -eq 0 ] && jsame elsewhere; then
			report "E:project:files-dir" PASS "a file that is not beside the project is found in a --files folder"
		else
			report "E:project:files-dir" FAIL "rc=$jrc, or RAM or VRAM differs (see work/engine-project/elsewhere.log)"
		fi
		jrun settings --project "$jdir/ok/settings.chimeraProject" "$epkg"
		if [ "$jrc" -eq 0 ] && [ -s "$jdir/settings.ram.bin" ] && ! cmp -s "$jdir/settings.ram.bin" "$golden_dir/$jname.ram.bin"; then
			report "E:project:settings" PASS "the project's own settings reach the machine"
		else
			report "E:project:settings" FAIL "rc=$jrc, or RAM matches the default dump: initFillByte never reached the machine"
		fi
		# The second the machine's clock starts at (issue 366): a project may
		# name another than the sandbox's own, and the machine reads it. A rom
		# ending in CLOCKTEST keeps the second its clock reads in the last eight
		# bytes of RAM; one project names 2030-01-02T03:04:05Z (1893553445), the
		# other names none and must read the sandbox's 2017-05-27 (1495889068).
		mkdir -p "$jdir/clock"
		{ cat "$here/roms/gridWalker.testrom"; printf 'CLOCKTEST'; } > "$jdir/clock/clock.testrom"
		python3 - "$jdir/clock/clock.testrom" "$jdir/clock" <<'CLOCKPY'
import hashlib, json, sys, project_file
sha1 = hashlib.sha1(open(sys.argv[1], "rb").read()).hexdigest().upper()
def write(path, start=None):
    doc = {
        "title": "clock",
        "core": {"name": "Synth", "version": "", "sha1": ""},
        "files": [{"name": "clock.testrom", "sha1": sha1, "slot": "rom"}],
        "inputs": {"columns": [["P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 A", "P1 B", "P1 Select", "P1 Start"]],
                   "entries": ["|........|"] * 3},
    }
    if start is not None: doc["clockStart"] = start
    project_file.write(path, doc)
write(sys.argv[2] + "/start.chimeraProject", "2030-01-02T03:04:05Z")
write(sys.argv[2] + "/default.chimeraProject")
CLOCKPY
		jsecond() { python3 -c 'import struct,sys; print(struct.unpack("<Q", open(sys.argv[1],"rb").read()[4088:4096])[0])' "$jdir/$1.ram.bin" 2>/dev/null; }
		jrun clock-start --project "$jdir/clock/start.chimeraProject" "$epkg"
		cstart="$(jsecond clock-start)"; crc=$jrc
		jrun clock-default --project "$jdir/clock/default.chimeraProject" "$epkg"
		cdefault="$(jsecond clock-default)"
		if [ "$crc" -eq 0 ] && [ "$jrc" -eq 0 ] && [ "$cstart" = "1893553445" ] && [ "$cdefault" = "1495889068" ]; then
			report "E:project:clock-start" PASS "the machine's clock starts at the second the project names, and at the sandbox's own when it names none"
		else
			report "E:project:clock-start" FAIL "the machine read ${cstart:-nothing} for 1893553445 (rc=$crc) and ${cdefault:-nothing} for 1495889068 (rc=$jrc) (see work/engine-project/clock-*.log)"
		fi

		jbad=""
		jrun missing --project "$jdir/missing/p.chimeraProject" "$epkg"
		jrefused missing "'gridWalker.testrom' was not found" || jbad="$jbad missing(rc=$jrc)"
		jrun changed --project "$jdir/changed/p.chimeraProject" "$epkg"
		jrefused changed "'gridWalker.testrom' does not match its recorded hash" || jbad="$jbad changed(rc=$jrc)"
		jrun pinned --project "$jdir/ok/pinned.chimeraProject" "$epkg"
		jrefused pinned "is not the project's pinned core" || jbad="$jbad pinned(rc=$jrc)"
		jrun allowed --project "$jdir/ok/pinned.chimeraProject" "$epkg" --allow-core-mismatch
		{ [ "$jrc" -eq 0 ] && jsame allowed; } || jbad="$jbad allowed(rc=$jrc)"
		# the package unpacked, with a declaration that has no slot called "rom"
		( cd "$jdir/slots-package" && unzip -q -o "$epkg" )
		printf '{"slots":[{"id":"cartridge","title":"Cartridge","min":1,"max":1,"formats":["testrom"]}]}\n' > "$jdir/slots-package/file_slots.json"
		jrun slots --project "$jdir/ok/p.chimeraProject" "$jdir/slots-package"
		{ [ "$jrc" -ne 0 ] && [ "$jrc" -lt 128 ] && [ ! -s "$jdir/slots.ram.bin" ]; } || jbad="$jbad slots(rc=$jrc)"
		# A slot gated on a setting the project leaves out is judged by the
		# package's default for it, as the machine will boot: the project with
		# no settings fits a "rom" slot shown only at initFillByte 0 (the
		# default), and the one that records 171 does not.
		mkdir -p "$jdir/defaults-package"
		( cd "$jdir/defaults-package" && unzip -q -o "$epkg" )
		printf '{"slots":[{"id":"rom","title":"Game","min":1,"max":1,"exposedWhen":{"setting":"initFillByte","is":0}}]}\n' > "$jdir/defaults-package/file_slots.json"
		jrun defaults --project "$jdir/ok/p.chimeraProject" "$jdir/defaults-package"
		dbad=""
		{ [ "$jrc" -eq 0 ] && jsame defaults; } || dbad="$dbad left-out(rc=$jrc)"
		jrun defaults-set --project "$jdir/ok/settings.chimeraProject" "$jdir/defaults-package"
		jrefused defaults-set "which these files make unavailable" || dbad="$dbad recorded(rc=$jrc)"
		if [ -z "$dbad" ]; then
			report "E:project:setting-defaults" PASS "a slot gated on a setting the project leaves out is judged by the package's default"
		else
			report "E:project:setting-defaults" FAIL "wrong:$dbad (see work/engine-project/defaults*.log)"
		fi

		# Firmware other than what the project pins is said and not refused
		# (the owner's choice): none given, and another file, each run to the
		# reference dumps with the warning; the pinned file runs without one.
		# The game file stands in for the firmware: its hash is the pin.
		fbad=""
		jrun fw-none --project "$jdir/ok/firmware.chimeraProject" "$epkg"
		{ [ "$jrc" -eq 0 ] && jsame fw-none && grep -q "pins the firmware 'bios'.*none was given" "$jdir/fw-none.log"; } || fbad="$fbad none(rc=$jrc)"
		jrun fw-other --project "$jdir/ok/firmware.chimeraProject" "$epkg" --firmware "bios=$jdir/changed/gridWalker.testrom"
		{ [ "$jrc" -eq 0 ] && jsame fw-other && grep -q "firmware 'bios' given is not the one the project pins" "$jdir/fw-other.log"; } || fbad="$fbad other(rc=$jrc)"
		jrun fw-right --project "$jdir/ok/firmware.chimeraProject" "$epkg" --firmware "bios=$jdir/ok/gridWalker.testrom"
		{ [ "$jrc" -eq 0 ] && jsame fw-right && ! grep -q "^\[project\]" "$jdir/fw-right.log"; } || fbad="$fbad right(rc=$jrc)"
		if [ -z "$fbad" ]; then
			report "E:project:firmware-pins" PASS "firmware other than the project's pin, or none, is said and the project still runs; the pinned file says nothing"
		else
			report "E:project:firmware-pins" FAIL "wrong:$fbad (see work/engine-project/fw-*.log)"
		fi

		if [ -z "$jbad" ]; then
			report "E:project:refusals" PASS "a missing file, a changed file, another core's package and files that fit no slot are each refused; the flag allows the package"
		else
			report "E:project:refusals" FAIL "wrong:$jbad (see work/engine-project/*.log)"
		fi

		# THE MCP SERVER (docs/mcp-server.md): the way an AI agent's program uses
		# the machine a running Chimera has open. chimera-run plays the running
		# Chimera here (--mcp-serve: it plays the movie, then serves the machine
		# as the movie left it), chimera-mcp is the program an agent starts, and
		# mcp-agent.py plays the agent. What the tools say must be what the other
		# tests already trust: memory equal to the reference dumps byte for byte,
		# the input equal to the movie file, the picture equal to the one
		# chimera-run writes for the last frame. Then, with no server running, a
		# call must say how to start one instead of hanging.
		# The pipe is given a name of this run's own, so a Chimera this user has
		# open with its server started is neither disturbed nor in the way.
		chimera_mcp="$repo_root/build/meson-linux/chimera-mcp"
		if [ ! -x "$chimera_mcp" ]; then
			report "E:mcp" SKIP "chimera-mcp not built (meson compile -C build/meson-linux)"
		else
			# Twice: as a person leaves it, where an agent may only look and every
			# change must be refused; then with changes allowed (--mcp-serve
			# changes), where what is written to the machine must read back and
			# must be said by status from then on.
			qname=gridWalker.win
			qlast=$(( $(grep -c '^|' "$here/movies/$qname.txt") - 1 ))
			CHIMERA_MCP_NAME="witness$$"; export CHIMERA_MCP_NAME
			for qallow in "" changes; do
				qdir="$work/mcp${qallow:+-$qallow}"
				rm -rf "$qdir" && mkdir -p "$qdir"
				crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/$qname.txt" \
					--screenshot "$qlast=$qdir/last.tga" --mcp-serve $qallow > "$qdir/serve.log" 2>&1 &
				qserve=$!
				qtries=0
				while [ "$qtries" -lt 100 ] && ! grep -q "MCP server started" "$qdir/serve.log" 2>/dev/null; do
					sleep 0.1; qtries=$((qtries+1))
				done
				timeout 120 python3 "$here/mcp-agent.py" "$chimera_mcp" "$golden_dir/$qname.ram.bin" "$golden_dir/$qname.vram.bin" \
					"$here/movies/$qname.txt" "$qdir/last.tga" "runner${qallow:+-$qallow}" > "$qdir/agent.log" 2>&1
				qrc=$?
				wait "$qserve"; qsrc=$?
				if [ -z "$qallow" ]; then
					if [ "$qrc" -eq 0 ] && [ "$qsrc" -eq 0 ] && grep -q "mcp: .*the agent's program is the witness 1" "$qdir/serve.log"; then
						report "E:mcp:tools" PASS "an agent reads the machine through chimera-mcp: memory, input and picture equal to the references; changes are refused"
					else
						report "E:mcp:tools" FAIL "agent rc=$qrc, chimera-run rc=$qsrc (see work/mcp/agent.log and serve.log)"
					fi
				elif [ "$qrc" -eq 0 ] && [ "$qsrc" -eq 0 ] && grep -q "mcp: .*the work as it was has been kept" "$qdir/serve.log"; then
					report "E:mcp:writes" PASS "with changes allowed, memory and a named value are written and read back, and status says so"
				else
					report "E:mcp:writes" FAIL "agent rc=$qrc, chimera-run rc=$qsrc (see work/mcp-changes/agent.log and serve.log)"
				fi
			done
			qdir="$work/mcp"
			printf '%s\n%s\n' \
				'{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{}}}' \
				'{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"status","arguments":{}}}' \
				| timeout 30 "$chimera_mcp" > "$qdir/stopped.log" 2>&1
			qrc=$?
			if [ "$qrc" -eq 0 ] && grep -q '"id":2.*not started.*Tools > MCP Server.*"isError":true' "$qdir/stopped.log"; then
				report "E:mcp:stopped" PASS "with no server running, a call says how to start one"
			else
				report "E:mcp:stopped" FAIL "rc=$qrc (see work/mcp/stopped.log)"
			fi
			unset CHIMERA_MCP_NAME

			# WITHOUT A WINDOW. Given a cores folder, chimera-mcp reaches no
			# Chimera: it opens a machine of its own. mcp-agent.py lists the cores,
			# starts a project from the game, writes the movie's input with the
			# tools and plays it - and then everything the checks above ask of a
			# running Chimera is asked of that machine, the changes too: memory
			# and picture equal to the references, edits and undos, branches,
			# seeks. It saves the project, closes, opens it again and plays it to
			# the reference memory. Reads outside the folders given and writes
			# outside the work folder must be refused.
			ldir="$work/mcp-windowless"
			rm -rf "$ldir" && mkdir -p "$ldir/cores" "$ldir/games" "$ldir/work"
			cp "$epkg" "$ldir/cores/"
			cp "$here/roms/gridWalker.testrom" "$ldir/games/"
			MCP_TEST_ROM="$ldir/games/gridWalker.testrom" MCP_TEST_WORK="$ldir/work" timeout 180 python3 "$here/mcp-agent.py" \
				"$chimera_mcp" "$golden_dir/$qname.ram.bin" "$golden_dir/$qname.vram.bin" "$here/movies/$qname.txt" "$qdir/last.tga" \
				local --cores "$ldir/cores" --work "$ldir/work" --files "$ldir/games" > "$ldir/agent.log" 2>&1
			lrc=$?
			if [ "$lrc" -eq 0 ] && [ -s "$ldir/work/made.chimeraProject" ] && [ -s "$ldir/work/made.txt" ] \
				&& [ "$(ls "$ldir/work" | wc -l)" -eq 2 ]; then
				report "E:mcp:windowless" PASS "with no window an agent opens a game, makes its movie, edits, saves and reopens it; memory and picture equal to the references"
			else
				report "E:mcp:windowless" FAIL "agent rc=$lrc (see work/mcp-windowless/agent.log)"
			fi
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
			crun_game "$badpkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				> "$work/unreadable-package.log" 2>&1 || true
			if grep -q "waterbox.config is not readable JSON ($badpkg: [0-9]* bytes, starting with \\\\xff\\\\xfe" "$work/unreadable-package.log"; then
				report "E:package:unreadable" PASS "the refusal names the package, its size and its first bytes"
			else
				report "E:package:unreadable" FAIL "$(tail -1 "$work/unreadable-package.log" | cut -c1-120)"
			fi
		else
			report "E:package:unreadable" FAIL "could not prepare the package (unzip, python3)"
		fi

		# A CORE THAT DIES IN INIT is reported as stopped, with miniBox's
		# reason, not as a file it could not load (2026-10-18). The synthetic
		# core dies in Init on a rom of the four bytes DIE!.
		printf 'DIE!' > "$work/dies-in-init.testrom"
		crun_game "$epkg" "$work/dies-in-init.testrom" "$here/movies/gridWalker.win.txt" > "$work/dies-in-init.log" 2>&1; dirc=$?
		if [ "$dirc" -ne 0 ] && [ "$dirc" -lt 128 ] && grep -q "the core stopped while starting" "$work/dies-in-init.log" \
			&& ! grep -q "could not load this file" "$work/dies-in-init.log"; then
			report "E:core-dies-in:init" PASS "a core that dies in Init is said to have stopped, with the reason: $(grep -o 'the core stopped while starting.*' "$work/dies-in-init.log" | head -1 | cut -c1-80)"
		else
			report "E:core-dies-in:init" FAIL "rc=$dirc: $(tail -1 "$work/dies-in-init.log" | cut -c1-120)"
		fi

		# A CORE'S OWN SENTENCE (docs/translations.md): a refusal the core
		# gives as a key of its package's tables, with an argument, is shown in
		# the words of the table. The synthetic core refuses a rom of the four
		# bytes NO!!.
		printf 'NO!!' > "$work/refused.testrom"
		crun_game "$epkg" "$work/refused.testrom" "$here/movies/gridWalker.win.txt" > "$work/refused.log" 2>&1; rfrc=$?
		if [ "$rfrc" -ne 0 ] && grep -q "Synth: The synthetic core refuses this game of 4 bytes." "$work/refused.log" \
			&& ! grep -q "error.refused" "$work/refused.log"; then
			report "E:core-text" PASS "a core's refusal, a key of its package's tables with an argument, reads: $(grep -o 'The synthetic core refuses.*' "$work/refused.log" | head -1)"
		else
			report "E:core-text" FAIL "rc=$rfrc: $(tail -1 "$work/refused.log" | cut -c1-120)"
		fi

		# CHIMERA-RUN SAYS WHAT IT WAS ASKED (2026-10-18): --final-screenshot is
		# the last frame's own picture, in a plain run and after a seek (it was
		# a blank buffer, or the first pass's picture); a number that is not
		# one, and --play with nothing to be the first pass of, are refused
		# rather than read as 0 and ignored.
		fsdir="$work/final-shot"; rm -rf "$fsdir"; mkdir -p "$fsdir"
		fsmovie="$here/movies/gridWalker.win.txt"; fsrom="$here/roms/gridWalker.testrom"
		fslast=$(( $(grep -c '^|' "$fsmovie") - 1 ))
		crun_game "$epkg" "$fsrom" "$fsmovie" --screenshot "$fslast=$fsdir/drawn.tga" > "$fsdir/drawn.log" 2>&1
		crun_game "$epkg" "$fsrom" "$fsmovie" --final-screenshot "$fsdir/plain.tga" > "$fsdir/plain.log" 2>&1; fsa=$?
		crun_game "$epkg" "$fsrom" "$fsmovie" --seek 30 --final-screenshot "$fsdir/seek.tga" > "$fsdir/seek.log" 2>&1; fsb=$?
		crun_game "$epkg" "$fsrom" "$fsmovie" --frames 10x > "$fsdir/number.log" 2>&1; fsc=$?
		crun_game "$epkg" "$fsrom" "$fsmovie" --play 20 > "$fsdir/play.log" 2>&1; fsd=$?
		if [ "$fsa" -eq 0 ] && [ "$fsb" -eq 0 ] && cmp -s "$fsdir/drawn.tga" "$fsdir/plain.tga" && cmp -s "$fsdir/drawn.tga" "$fsdir/seek.tga" \
			&& [ "$fsc" -ne 0 ] && [ "$fsc" -lt 128 ] && grep -q "wants a whole number, not '10x'" "$fsdir/number.log" \
			&& [ "$fsd" -ne 0 ] && [ "$fsd" -lt 128 ] && grep -q "first pass of a --seek" "$fsdir/play.log"; then
			report "E:run:final-screenshot" PASS "the final screenshot is the last frame's picture, plain and after a seek; a bad number and a lone --play are refused"
		else
			report "E:run:final-screenshot" FAIL "rc=$fsa/$fsb/$fsc/$fsd (see work/final-shot/*.log)"
		fi

		# A PACKAGE WHOSE SIZES ARE IMPOSSIBLE is refused with the number and
		# the range, and the process goes on to say so. A width of -1 sized a
		# picture buffer of 2^64 bytes and ended Chimera (engine analysis,
		# 2026-10-18).
		sizepkg="$work/impossible-package"
		rm -rf "$sizepkg"; mkdir -p "$sizepkg"
		if (cd "$sizepkg" && unzip -q "$epkg") 2>/dev/null && python3 -c "
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d['video']['width'] = -1
json.dump(d, open(p, 'w'))
" "$sizepkg/waterbox.config"; then
			crun_game "$sizepkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				> "$work/impossible-package.log" 2>&1; src=$?
			if [ "$src" -ne 0 ] && [ "$src" -lt 128 ] && grep -q "video.width is -1, which is not between 1 and 16384" "$work/impossible-package.log"; then
				report "E:package:sizes" PASS "a package declaring a width of -1 is refused with the number and the range"
			else
				report "E:package:sizes" FAIL "rc=$src: $(tail -1 "$work/impossible-package.log" | cut -c1-120)"
			fi
		else
			report "E:package:sizes" FAIL "could not prepare the package (unzip, python3)"
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
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
				--settings '{"probe":0.5}' --dump "RAM=$work/locale-c.ram.bin" > "$work/locale-c.log" 2>&1 || true
			LOCPATH="$loc" LC_ALL=de_DE.UTF-8 CHIMERA_TEST_NEEDS_COMMA_LOCALE=1 \
				"$repo_root/build/meson-linux/test_plain_numbers" > "$work/locale-unit.log" 2>&1
			unit_rc=$?
			LOCPATH="$loc" LC_ALL=de_DE.UTF-8 crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$dmovie" \
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
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$work/crashes-at-11.txt" \
			--meta "$work/crashes.meta.txt" > "$work/crashes.log" 2>&1
		crc=$?
		if [ "$crc" -ge 128 ]; then
			report "E:core-crashes" FAIL "the process died with signal $((crc - 128)) (see work/crashes.log)"
		elif grep -q "^detail=the core stopped: the core crashed: it wrote to address" "$work/crashes.meta.txt" 2>/dev/null; then
			report "E:core-crashes" PASS "a core that follows a wild pointer stops the run, and the process survives"
		else
			report "E:core-crashes" FAIL "no stopped core reported (see work/crashes.log)"
		fi

		# A CORE THAT DIES AFTER ITS FRAME (2026-10-18): in the picture's readback
		# or the input question the frame it died in is not counted - the run
		# stops with 11 frames done, not 12 - and in StateSaving, under
		# --rerecord, the state of a dead machine is not taken, so the run stops
		# instead of loading it back to life. (SPEC.md: the 0xFD/0xF7/0xFB cues.)
		for cue in "GetVideoBgra:|U.LRABsS|:--screenshot 11=$work/dies-in.tga" \
		           "InputWasRead:|UDL.ABsS|:" \
		           "StateSaving:|UD.RABsS|:--rerecord"; do
			where="${cue%%:*}"; rest="${cue#*:}"; line="${rest%%:*}"; extra="${rest#*:}"
			sed "12s/.*/$line/" "$here/movies/gridWalker.win.txt" > "$work/dies-in-$where.txt"
			rm -f "$work/dies-in-$where.meta.txt"
			# shellcheck disable=SC2086
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$work/dies-in-$where.txt" $extra \
				--meta "$work/dies-in-$where.meta.txt" > "$work/dies-in-$where.log" 2>&1
			irc=$?
			if [ "$irc" -ge 128 ]; then
				report "E:core-dies-in:$where" FAIL "the process died with signal $((irc - 128)) (see work/dies-in-$where.log)"
			elif ! grep -q "^status=ERROR" "$work/dies-in-$where.meta.txt" 2>/dev/null \
			     || ! grep -q "dying in $where on purpose" "$work/dies-in-$where.meta.txt"; then
				report "E:core-dies-in:$where" FAIL "the run did not stop with the core's reason: $(grep '^detail=\|^status=' "$work/dies-in-$where.meta.txt" 2>/dev/null | tr '\n' ' ')"
			elif [ "$where" != StateSaving ] && ! grep -q "^failedat=11$" "$work/dies-in-$where.meta.txt"; then
				report "E:core-dies-in:$where" FAIL "the frame the core died in was counted: $(grep '^failedat=' "$work/dies-in-$where.meta.txt")"
			else
				report "E:core-dies-in:$where" PASS "a core that dies in $where stops the run there, with its reason, and the process survives"
			fi
		done

		# A CORE THAT NEVER COMES BACK (2026-10-19): the session's watchdog
		# stops a call that runs past its time - 20 seconds for a frame and 2
		# minutes for starting, 1.5 seconds here - and the run reports the core
		# stopped, with the reason, instead of hanging. The synthetic core loops
		# forever in its own code: in its third frame on a rom ending in
		# SPINFRAME, and in Init on a rom of the four bytes SPIN.
		cat "$here/roms/gridWalker.testrom" > "$work/stuck-frame.testrom"
		printf 'SPINFRAME' >> "$work/stuck-frame.testrom"
		rm -f "$work/stuck-frame.meta.txt"
		CHIMERA_FRAME_TIMEOUT_MS=1500 CRUN_TIMEOUT=60 crun_game "$epkg" "$work/stuck-frame.testrom" "$here/movies/gridWalker.win.txt" \
			--meta "$work/stuck-frame.meta.txt" > "$work/stuck-frame.log" 2>&1; sfrc=$?
		printf 'SPIN' > "$work/stuck-start.testrom"
		CHIMERA_START_TIMEOUT_MS=1500 CRUN_TIMEOUT=60 crun_game "$epkg" "$work/stuck-start.testrom" "$here/movies/gridWalker.win.txt" \
			> "$work/stuck-start.log" 2>&1; ssrc=$?
		if [ "$sfrc" -eq 124 ] || [ "$ssrc" -eq 124 ]; then
			report "E:core-stuck" FAIL "a stuck core was not stopped: the run was killed after 60 s (frame rc=$sfrc, start rc=$ssrc)"
		elif ! grep -q "^failedat=2$" "$work/stuck-frame.meta.txt" 2>/dev/null \
		     || ! grep -q "did not finish a frame in 1.5 seconds" "$work/stuck-frame.meta.txt" "$work/stuck-frame.log" 2>/dev/null; then
			report "E:core-stuck" FAIL "the stuck frame was not reported as such: $(grep '^detail=\|^status=\|^failedat=' "$work/stuck-frame.meta.txt" 2>/dev/null | tr '\n' ' ')"
		elif [ "$ssrc" -eq 0 ] || ! grep -q "the core stopped while starting: the core did not finish starting in 1.5 seconds" "$work/stuck-start.log"; then
			report "E:core-stuck" FAIL "the stuck start was not reported as such: rc=$ssrc $(tail -1 "$work/stuck-start.log" | cut -c1-120)"
		else
			report "E:core-stuck" PASS "a core stuck in a frame or in starting is stopped after its time and reported, with the reason, and the process survives"
		fi

		# ...and a core that is working is not stopped, even when the work is
		# done for it by Chimera: a frame that only asks the compile cache for
		# an object it never stored, and yields - about half a second here -
		# runs past a 0.1 second limit, because each call to the cache counts
		# as the core's work (calls through the GPU bridges count the same way).
		cat "$here/roms/gridWalker.testrom" > "$work/cache-work.testrom"
		printf 'CACHEWORK' >> "$work/cache-work.testrom"
		rm -rf "$work/cache-work.cache" "$work/cache-work.meta.txt"
		CHIMERA_FRAME_TIMEOUT_MS=100 CRUN_TIMEOUT=60 crun_game "$epkg" "$work/cache-work.testrom" "$here/movies/gridWalker.win.txt" \
			--cache-dir "$work/cache-work.cache" --meta "$work/cache-work.meta.txt" > "$work/cache-work.log" 2>&1; cwrc=$?
		if [ "$cwrc" -ne 0 ] || ! grep -q "^status=OK$" "$work/cache-work.meta.txt" 2>/dev/null; then
			report "E:core-working" FAIL "a core working through the compile cache was stopped: rc=$cwrc $(grep '^detail=' "$work/cache-work.meta.txt" 2>/dev/null)"
		else
			report "E:core-working" PASS "a frame that is long but busy (calls to the compile cache) is not stopped"
		fi

		# ...and the first frame after starting, and the first after a state
		# load, have the start's time (2026-10-21, issue 332): RPCS3 analyses
		# and compiles a game in its first frame. A rom ending in SLOWFIRST
		# spends half a second or so of those frames in the core's own code; a
		# frame limit of 0.1 seconds must not stop them. The seek loads a state,
		# so there are two such frames at least.
		cat "$here/roms/gridWalker.testrom" > "$work/slow-first.testrom"
		printf 'SLOWFIRST' >> "$work/slow-first.testrom"
		rm -f "$work/slow-first.meta.txt"
		CHIMERA_FRAME_TIMEOUT_MS=100 CRUN_TIMEOUT=120 crun_game "$epkg" "$work/slow-first.testrom" "$here/movies/gridWalker.win.txt" \
			--seek 5 --meta "$work/slow-first.meta.txt" > "$work/slow-first.log" 2>&1; slrc=$?
		slow_frames=$(grep -c "synth: a long first frame" "$work/slow-first.log" 2>/dev/null)
		if [ "$slrc" -ne 0 ] || ! grep -q "^status=OK$" "$work/slow-first.meta.txt" 2>/dev/null; then
			report "E:first-frame-time" FAIL "a long first frame was stopped: rc=$slrc $(grep '^detail=' "$work/slow-first.meta.txt" 2>/dev/null)"
		elif [ "${slow_frames:-0}" -lt 2 ]; then
			report "E:first-frame-time" FAIL "expected a long first frame after starting and after the seek's load, saw ${slow_frames:-0} (see work/slow-first.log)"
		else
			report "E:first-frame-time" PASS "the first frame after starting and after a load ($slow_frames of them) has the start's time, not a frame's"
		fi

		# A FRAME STOPPED PARTWAY, AND THE MACHINE PUT BACK (chimera#330). A
		# core stopped in the middle of a frame - by the watchdog, here - may
		# have written anywhere before it stopped: the machine's RAM, its heap.
		# The window then offers to go back to the latest stored frame, and
		# chimera-run --recover does the same. A rom
		# ending in STOPONCE scribbles over the machine's RAM and leaves a block
		# of its heap written in frame 5, and loops there until it is stopped,
		# once; put back on frame 3 and played on, the machine must end on the
		# goldens, every byte the dead frame wrote gone.
		cat "$here/roms/gridWalker.testrom" > "$work/stop-once.testrom"
		printf 'STOPONCE' >> "$work/stop-once.testrom"
		rm -f "$work/stop-once.meta.txt" "$work/stop-once.ram.bin" "$work/stop-once.vram.bin"
		CHIMERA_FRAME_TIMEOUT_MS=500 CRUN_TIMEOUT=120 crun_game "$epkg" "$work/stop-once.testrom" "$here/movies/gridWalker.win.txt" \
			--recover --dump "RAM=$work/stop-once.ram.bin" --dump "VRAM=$work/stop-once.vram.bin" \
			--meta "$work/stop-once.meta.txt" > "$work/stop-once.log" 2>&1; sorc=$?
		if [ "$sorc" -ne 0 ] || ! grep -q "^status=OK$" "$work/stop-once.meta.txt" 2>/dev/null; then
			report "E:core-recovers" FAIL "the run did not go on after the stop: rc=$sorc $(grep '^detail=' "$work/stop-once.meta.txt" 2>/dev/null) $(grep 'chimera-run:' "$work/stop-once.log" | tail -1 | cut -c1-160)"
		elif ! grep -q "^recovered=1$" "$work/stop-once.meta.txt"; then
			report "E:core-recovers" FAIL "the core was not stopped and put back once: $(grep '^recovered=' "$work/stop-once.meta.txt")"
		elif ! cmp -s "$work/stop-once.ram.bin" "$golden_dir/gridWalker.win.ram.bin" \
		     || ! cmp -s "$work/stop-once.vram.bin" "$golden_dir/gridWalker.win.vram.bin"; then
			report "E:core-recovers" FAIL "put back after the stop, the machine did not end on the goldens: what the dead frame wrote survived"
		else
			report "E:core-recovers" PASS "a frame stopped after scribbling over RAM and the heap, the machine put back on the frame before it, played on: it ends on the goldens"
		fi

		# CALLS DURING A FRAME (issue 344). The synthetic core reports each INPUT
		# its machine executes, part way through the frame (its
		# SetInputPollCallback export), and chimera-run --count-input-polls counts
		# the engine's calls. gridWalker reads its pad once in each frame it
		# plays and not at all once the game is over, so the calls are exactly
		# the frames that were not lag frames - and counting them must not move
		# the machine.
		rm -f "$work/polls.meta.txt" "$work/polls.ram.bin" "$work/polls.vram.bin"
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" --count-input-polls \
			--dump "RAM=$work/polls.ram.bin" --dump "VRAM=$work/polls.vram.bin" \
			--meta "$work/polls.meta.txt" > "$work/polls.log" 2>&1
		polls=$(sed -n 's/^inputPolls=//p' "$work/polls.meta.txt" 2>/dev/null)
		lags=$(sed -n 's/^lagFrames=//p' "$work/polls.meta.txt" 2>/dev/null)
		pframes=$(sed -n 's/^frames=//p' "$work/polls.meta.txt" 2>/dev/null)
		if [ -z "$polls" ] || [ "$polls" = none ]; then
			report "E:input-polls" FAIL "the core's reads of its pad were not reported: inputPolls=${polls:-missing} (see work/polls.log)"
		elif [ "$polls" -eq 0 ] || [ "$polls" -ne $(( ${pframes:-0} - ${lags:-0} )) ]; then
			report "E:input-polls" FAIL "$polls calls in ${pframes:-?} frames with ${lags:-?} lag frames: not one for each frame the pad was read"
		elif ! cmp -s "$work/polls.ram.bin" "$golden_dir/gridWalker.win.ram.bin" \
			|| ! cmp -s "$work/polls.vram.bin" "$golden_dir/gridWalker.win.vram.bin"; then
			report "E:input-polls" FAIL "counting the calls moved the machine off the goldens"
		else
			report "E:input-polls" PASS "$polls calls part way through $pframes frames, one for each frame the core read its pad ($lags lag frames had none); the machine ends on the goldens"
		fi

		# CALLS AT MEMORY (issue 496). The synthetic core names two scopes, Code
		# and RAM, and calls the engine at each access it is asked to watch,
		# part way through the frame. gridWalker starts every frame at the
		# instruction at 0 and reads its status byte (RAM 0) twice in every
		# frame: 0 while it plays, and 1 once it is won - at the second read of
		# the frame it is won in, and at both in each frame after (the frames
		# that read no pad). chimera-run --watch-memory counts the calls and
		# sums the values given; watching must not move the machine, and a
		# read in the Code scope is refused.
		rm -f "$work/memcalls.exec.meta.txt" "$work/memcalls.read.meta.txt" "$work/memcalls.ram.bin" "$work/memcalls.vram.bin"
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" --watch-memory exec:0:0 \
			--meta "$work/memcalls.exec.meta.txt" > "$work/memcalls.exec.log" 2>&1
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" --watch-memory read:1:0 \
			--count-input-polls --dump "RAM=$work/memcalls.ram.bin" --dump "VRAM=$work/memcalls.vram.bin" \
			--meta "$work/memcalls.read.meta.txt" > "$work/memcalls.read.log" 2>&1
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" --watch-memory read:0:0 \
			--meta "$work/memcalls.refused.meta.txt" > "$work/memcalls.refused.log" 2>&1
		mframes=$(sed -n 's/^frames=//p' "$work/memcalls.read.meta.txt" 2>/dev/null)
		mlags=$(sed -n 's/^lagFrames=//p' "$work/memcalls.read.meta.txt" 2>/dev/null)
		mexec=$(sed -n 's/^memoryCalls=//p' "$work/memcalls.exec.meta.txt" 2>/dev/null)
		mread=$(sed -n 's/^memoryCalls=//p' "$work/memcalls.read.meta.txt" 2>/dev/null)
		msum=$(sed -n 's/^memorySum=//p' "$work/memcalls.read.meta.txt" 2>/dev/null)
		mscopes=$(sed -n 's/^memoryScopes=//p' "$work/memcalls.read.meta.txt" 2>/dev/null)
		if [ "$mscopes" != "Code,RAM" ] || [ -z "$mexec" ] || [ "$mexec" = none ] || [ -z "$mread" ]; then
			report "E:memory-calls" FAIL "the core's memory calls were not offered: scopes ${mscopes:-missing}, calls ${mexec:-missing}/${mread:-missing} (see work/memcalls.*.log)"
		elif [ "$mexec" -ne "${mframes:-0}" ]; then
			report "E:memory-calls" FAIL "the instruction at 0 made $mexec calls in ${mframes:-?} frames, not one a frame"
		elif [ "$mread" -ne $(( 2 * ${mframes:-0} )) ] || [ "${msum:-x}" != $(( 2 * ${mlags:-0} + 1 )) ]; then
			report "E:memory-calls" FAIL "reads of RAM 0 made $mread calls summing to ${msum:-?} in ${mframes:-?} frames with ${mlags:-?} lag frames: not two a frame, with the status the game had"
		elif ! grep -q "^status=ERROR" "$work/memcalls.refused.meta.txt" 2>/dev/null; then
			report "E:memory-calls" FAIL "a read in the Code scope was not refused (see work/memcalls.refused.log)"
		elif ! cmp -s "$work/memcalls.ram.bin" "$golden_dir/gridWalker.win.ram.bin" \
			|| ! cmp -s "$work/memcalls.vram.bin" "$golden_dir/gridWalker.win.vram.bin"; then
			report "E:memory-calls" FAIL "watching memory moved the machine off the goldens"
		else
			report "E:memory-calls" PASS "the instruction at 0 called once in each of $mframes frames, reads of RAM 0 twice ($mread, the status values summing to $msum), a read in the Code scope refused; the machine ends on the goldens"
		fi

		# THE CORE LOG (ce_core_log, Tools > Export Core Log... in the frontend):
		# asked for, it starts a file saying when and by which Chimera, notes
		# the session's core package, and keeps everything the core writes - the
		# dying core's last words included. The "corelog" request it mounts must
		# not move the machine: the same movie with the log on ends on the
		# goldens.
		clmovie="$here/movies/gridWalker.win.txt"
		rm -f "$work/core.log" "$work/core-on.ram.bin" "$work/core-on.vram.bin"
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$dmovie" \
			--core-log "$work/core.log" > "$work/core-log-dies.log" 2>&1
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$clmovie" --core-log "$work/core-on.log" \
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

		# A CORE'S OWN TOOL (ce_package_run_tool, the Emulator menu's own items):
		# the core is asked to run a tool INSTEAD of being started, with no game.
		# The synth's "join" writes its parts, read under part.0, part.1, ... in
		# the order given, into the file "joined", which goes straight to the
		# disk where it was asked; "backwards" turns each part around. A part
		# that is empty is refused in a sentence that names the person's file,
		# and a tool the core does not have is refused too; a refused run
		# leaves no file (it writes <file>.part, put in place only when done) -
		# the files and the answers both checked, so a tool that wrote nothing
		# cannot pass. A tool run is given no game, so a file the tool reads
		# may be named "rom" (user-decided, 2026-11-02): the run that joins is
		# given one, which the synth does not read.
		tdir="$work/tool"
		rm -rf "$tdir" && mkdir -p "$tdir"
		printf 'abc' > "$tdir/first.part"; printf 'de' > "$tdir/second.part"; : > "$tdir/empty.part"
		printf 'stale contents' > "$tdir/joined.bin"
		tdone="$("$chimera_run" --run-tool join "$epkg" --input part.0="$tdir/first.part" --input part.1="$tdir/second.part" \
			--input rom="$tdir/empty.part" --output joined="$tdir/joined.bin" --tool-options '{"backwards":true}' 2>"$tdir/run.err")"
		tempty="$("$chimera_run" --run-tool join "$epkg" --input part.0="$tdir/first.part" --input part.1="$tdir/empty.part" \
			--output joined="$tdir/refused.bin" 2>>"$tdir/run.err")"
		tunknown="$("$chimera_run" --run-tool shuffle "$epkg" --output joined="$tdir/unknown.bin" 2>>"$tdir/run.err")"
		if [ "$tdone" = '{"done": "joined 2 parts, 5 bytes"}' ] && [ "$(cat "$tdir/joined.bin")" = "cbaed" ] \
			&& [ "$tempty" = '{"refused": "the part empty.part is empty"}' ] && [ ! -e "$tdir/refused.bin" ] && [ ! -e "$tdir/refused.bin.part" ] \
			&& [ "$tunknown" = '{"refused": "this core has no tool shuffle"}' ]; then
			report "E:core-tool" PASS "a tool of the core's own wrote its file on the disk (5 bytes, parts in their order, turned around as asked, the old contents gone), with a file of its own named rom; an empty part and an unknown tool are refused in a sentence, and leave no file"
		else
			report "E:core-tool" FAIL "done=[$tdone] file=[$(cat "$tdir/joined.bin" 2>/dev/null)] empty=[$tempty] unknown=[$tunknown] (see work/tool/run.err)"
		fi

		# A TOOL'S PROJECT (owner, 2026-11-02): an output the package declares
		# "project" is two files to the core, <name>.json and <name>.input,
		# which the engine makes into the project's zip when the tool is done.
		# The synth's make_project writes a project of three frames pinned to
		# the package; the zip must hold project.json and input.input, its
		# lines exactly as written, and open (asked for a suggestion, which
		# reads its settings). With "broken" one line is not a frame's: the
		# engine refuses the project, says which line, and leaves no file.
		mdir2="$work/tool-project"
		rm -rf "$mdir2" && mkdir -p "$mdir2"
		mdone="$("$chimera_run" --run-tool make_project "$epkg" --output project="$mdir2/made.chimeraProject" 2>"$mdir2/run.err")"
		mbroken="$("$chimera_run" --run-tool make_project "$epkg" --output project="$mdir2/broken.chimeraProject" \
			--tool-options '{"broken":true}' 2>&1)"; mbrc=$?
		msuggest="$("$chimera_run" --project "$mdir2/made.chimeraProject" "$epkg" --suggest 2>>"$mdir2/run.err")"
		mzip="$(python3 - "$mdir2/made.chimeraProject" "$epkg" <<'TOOLPROJPY'
import hashlib, json, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
names = sorted(z.namelist())
doc = json.loads(z.read("project.json"))
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
ok = (names == ["input.input", "project.json"]
      and z.read("input.input") == b"|U.|\n|.D|\n|UD|\n"
      and doc.get("id") and doc["title"] == "made by a tool" and doc["core"]["sha1"] == sha1
      and doc["inputs"] == {"columns": [["P1 Up", "P1 Down"]]} and doc["settings"] == {"initFillByte": 9})
print("ok" if ok else "names=%s json=%s" % (names, json.dumps(doc)[:300]))
TOOLPROJPY
)"
		if [ "$mdone" = '{"done": "made a project of 3 frames"}' ] && [ "$mzip" = ok ] \
			&& printf '%s' "$msuggest" | grep -qF '"seen": "game none, 0 bytes, fill 9"' \
			&& [ "$mbrc" -ne 0 ] && printf '%s' "$mbroken" | grep -qF "could not be made" && printf '%s' "$mbroken" | grep -qF "line 2" \
			&& [ -z "$(ls "$mdir2" | grep -v -e '^made.chimeraProject$' -e '^run.err$')" ]; then
			report "E:core-tool-project" PASS "a tool's project output, written as project.json and its input's lines, became a project zip that opens (its lines as written, pinned to the package); a project with a line that is not a frame's is refused, line named, and no file is left"
		else
			report "E:core-tool-project" FAIL "done=[$mdone] zip=[$mzip] suggest=[$(printf '%s' "$msuggest" | cut -c1-80)] broken rc=$mbrc [$(printf '%s' "$mbroken" | tail -1 | cut -c1-160)] files=[$(ls "$mdir2" | tr '\n' ' ')]"
		fi

		# THE OTHER QUESTIONS ARE ASKED OF A PROJECT TOO (user-decided,
		# 2026-10-26). The core's suggested settings (SuggestSettings): the
		# synth names the game the project gave it, its size and the fill byte
		# in the project's settings, and suggests the size modulo 256; a
		# project with no game yet is still asked (its slot is not checked
		# for a question), and a game file alone is refused.
		qdir="$work/suggest"
		rm -rf "$qdir" && mkdir -p "$qdir"
		qsize="$(wc -c < "$here/roms/gridWalker.testrom" | tr -d ' ')"
		"$chimera_run" --new-project "$qdir/game.chimeraProject" "$epkg" "$here/roms/gridWalker.testrom" \
			--settings '{"initFillByte":5}' > "$qdir/game.make.log" 2>&1
		qanswer="$("$chimera_run" --project "$qdir/game.chimeraProject" "$epkg" --files "$here/roms" --suggest 2>"$qdir/suggest.err")"
		python3 -c 'import project_file,sys; project_file.write(sys.argv[1], {"title": "none", "core": {"name": "Synth", "version": "", "sha1": ""}, "files": [], "settings": {"initFillByte": 9}})' \
			"$qdir/none.chimeraProject"
		qnone="$("$chimera_run" --project "$qdir/none.chimeraProject" "$epkg" --suggest 2>>"$qdir/suggest.err")"
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" --suggest > "$qdir/alone.log" 2>&1; qalone=$?
		# (the synth answers a fill of 9, and "seen" says what reached it)
		if printf '%s' "$qanswer" | grep -q '^{"values": {"initFillByte": 9}, "note": "The synthetic core suggests a fill of 9."' \
			&& printf '%s' "$qanswer" | grep -qF "\"seen\": \"game gridWalker.testrom, $qsize bytes, fill 5\"" \
			&& printf '%s' "$qnone" | grep -qF '"seen": "game none, 0 bytes, fill 9"' \
			&& [ "$qalone" -ne 0 ] && grep -q "a game runs only in a project" "$qdir/alone.log"; then
			report "E:suggest" PASS "the core's suggestion is asked of a project: its game ($qsize bytes) and its settings reach the core; one with no game yet is asked too; a game file alone is refused"
		else
			report "E:suggest" FAIL "answer=[$qanswer] none=[$qnone] alone rc=$qalone (see work/suggest/*)"
		fi

		# A compile session (--precompile) is a project's too: two sessions,
		# each its share of the synth's six modules into one compile cache,
		# never running the game. Each says how far it is and what it stored,
		# and between them every module is there.
		pdir="$work/precompile"
		rm -rf "$pdir" && mkdir -p "$pdir/cache"
		"$chimera_run" --new-project "$pdir/game.chimeraProject" "$epkg" "$here/roms/gridWalker.testrom" > "$pdir/game.make.log" 2>&1
		prc=""
		for pi in 0 1; do
			"$chimera_run" --project "$pdir/game.chimeraProject" "$epkg" --files "$here/roms" \
				--precompile "$pi/2" --cache-dir "$pdir/cache" > "$pdir/session$pi.log" 2>&1
			prc="$prc$?"
		done
		pstored="$(cat "$pdir"/session*.log | grep -c '^\[cache\] stored synth/module[0-5].bin [0-9A-Fa-f]\{40\}')"
		pfiles="$(find "$pdir/cache" -type f -name 'module*.bin' | wc -l | tr -d ' ')"
		if [ "$prc" = "00" ] && [ "$pstored" = 6 ] && [ "$pfiles" = 6 ] \
			&& grep -q '^Precompiled 3/6 modules' "$pdir/session0.log" && grep -q '^Precompiled 3/6 modules' "$pdir/session1.log"; then
			report "E:precompile" PASS "two compile sessions of a project each compile their 3 of 6 modules into the cache, and say so"
		else
			report "E:precompile" FAIL "rc=$prc stored=$pstored files=$pfiles (see work/precompile/session*.log)"
		fi

		# and a game file alone is no run: chimera-run takes a project
		"$chimera_run" "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" > "$work/game-alone.log" 2>&1; garc=$?
		if [ "$garc" -ne 0 ] && [ "$garc" -lt 128 ] && grep -q "a game runs only in a project" "$work/game-alone.log"; then
			report "E:run:projects-only" PASS "a game file and a movie without a project are refused, with how to make one"
		else
			report "E:run:projects-only" FAIL "rc=$garc: $(tail -1 "$work/game-alone.log" | cut -c1-120)"
		fi

		# a bus read in runs is the bus read a byte at a time (chimera#180):
		# "Bus" answers through the core's ReadBus, "Bus (peeks)" declines it
		# and the engine peeks for itself. chimera-run compares every byte of
		# each against PeekBus, from an odd start and past the end.
		busok=1
		for bus in "Bus" "Bus (peeks)"; do
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--ram-search-bench "Bus" > "$work/ranges.log" 2>&1
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--ram-search-bench "Bus (peeks)" > "$work/ranges-none.log" 2>&1
		# kept whole to eights, the largest candidate's size (issue 474):
		# 0x20001+0xff0 -> 0x20000..0x20ff8 (4088); 0x1000+0x801 -> 0x1000..0x1808 (2056);
		# 0x2fffa+64 -> 0x2fff8..0x30000 (8): 6152 bytes of 196608
		if grep -q "^ram-search-bench Bus: 3 live ranges, 6152 of 196608 bytes searched, every candidate live" "$work/ranges.log" \
			&& grep -q "^ram-search-bench Bus (peeks): no word on live ranges, all of it is searched" "$work/ranges-none.log"; then
			report "E:bus-ranges" PASS "a search of a bus that says what is live is over that alone: 6152 of 196608 bytes, each reading what the bus peeks"
		else
			report "E:bus-ranges" FAIL "$(grep -m1 -E 'live|ranges|word' "$work/ranges.log" "$work/ranges-none.log" | tail -1)"
		fi

		# a bus can be dumped (chimera-run --dump): a bus is an address space
		# and may be 4 GiB, so what is written is a fingerprint of every
		# 64 KiB of it, eight bytes each - enough to say whether two runs of
		# a core with no memory domain left the same machine - and
		# <bus>@<address>+<length> writes a span's bytes. The synth's bus is
		# three runs of 64 KiB: the file is 24 bytes, its first eight are the
		# FNV-1a of the first run's bytes as the span dump gives them, both
		# buses give the same file, and a span past the end is refused.
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--dump "Bus=$work/bus.prints" --dump "Bus (peeks)=$work/bus-peeks.prints" \
			--dump "Bus@0+10000=$work/bus.span" --dump "Bus@1001+7ff=$work/bus.odd" > "$work/busdump.log" 2>&1
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--dump "Bus@2fff8+10=$work/bus.past" > "$work/busdump-past.log" 2>&1
		pastrc=$?
		if python3 - "$work" <<'PYEOF' > "$work/busdump.check" 2>&1 && [ "$pastrc" -ne 0 ] && grep -q "that span is not inside the bus" "$work/busdump-past.log" && [ ! -e "$work/bus.past" ]
import sys
w = sys.argv[1]
prints = open(w + "/bus.prints", "rb").read()
span = open(w + "/bus.span", "rb").read()
odd = open(w + "/bus.odd", "rb").read()
assert len(prints) == 24, "the fingerprints are %d bytes, not 24" % len(prints)
assert prints == open(w + "/bus-peeks.prints", "rb").read(), "the two buses' fingerprints differ"
assert len(span) == 0x10000 and len(odd) == 0x7ff, "the spans are %d and %d bytes" % (len(span), len(odd))
h = 0xcbf29ce484222325
for b in span:
    h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
assert prints[:8] == h.to_bytes(8, "little"), "the first fingerprint is not the first 64 KiB's"
assert odd == span[0x1001:0x1001 + 0x7ff], "a span from an odd address is not those bytes"
assert len(set(span)) > 1, "the bus read as one value throughout"
PYEOF
		then
			report "E:bus-dump" PASS "a bus is dumped as a fingerprint of every 64 KiB (24 bytes for its three), a span of it as its bytes, and a span past its end is refused"
		else
			report "E:bus-dump" FAIL "$(tail -1 "$work/busdump.check" 2>/dev/null) past=[rc $pastrc: $(tail -1 "$work/busdump-past.log" 2>/dev/null)]"
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
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
		CHIMERA_HELPERS=0 crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
			CHIMERA_PICTURE_TRACE=1 crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$work/probe.none.txt" --frames 1 --record "$work/probe.row.txt" > /dev/null 2>&1
		{ grep -v '^|' "$work/probe.row.txt" | grep -v '^\[/'; row="$(grep -m1 '^|' "$work/probe.row.txt")"; i=0; while [ "$i" -lt 60 ]; do printf '%s\n' "$row"; i=$((i + 1)); done; grep '^\[/' "$work/probe.row.txt"; } > "$work/probe.idle.txt"
		probeState() { # garble frames -> the summary line
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$work/probe.idle.txt" \
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
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$work/headers.in.txt" \
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
			crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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

		# A tool reads a core's memory without running the core's own code
		# (engine analysis, 2026-10-18). The synth keeps a page no-access, as
		# RPCS3's texture cache keeps the pages it watches, and counts in its
		# Mailbox every time its fault handler is asked about the page, which
		# holds byte i = i * 7 + 3. After a run, RAM Search's measurement reads
		# the page as a search does, and the run dumps it as the memory tools
		# read: the bytes come out, and the handler never ran. Read through
		# the core's own pointer, as the tools did, it ran.
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
			--ram-search-bench Watched --dump "Watched=$work/watched.bin" --dump "Mailbox=$work/watched.mail.bin" \
			> "$work/watched.log" 2>&1
		watchedRuns="$(od -A n -t u4 -j 20 -N 4 "$work/watched.mail.bin" 2>/dev/null | tr -d ' ')"
		if python3 -c 'import sys; d = open(sys.argv[1], "rb").read(); sys.exit(0 if d == bytes((i * 7 + 3) & 255 for i in range(4096)) else 1)' "$work/watched.bin" 2>/dev/null \
			&& grep -q '^ram-search-bench Watched (4096 bytes) size 4: ' "$work/watched.log" && [ "$watchedRuns" = 0 ]; then
			report "E:tools:core-not-run" PASS "a page the core made no-access, read by RAM Search and dumped between frames, comes out whole, and the core's fault handler was not run for it"
		else
			report "E:tools:core-not-run" FAIL "the handler ran '$watchedRuns' times; $(grep -m1 -E 'ram-search-bench Watched|chimera-run:' "$work/watched.log" | cut -c1-120) (see work/watched.*)"
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
		#   Mirror, on a bus with no pointer, reads what the bus says, and can
		#   be written as the bus can (chimera#456);
		#   a 77 written to Wanderer before the first frame (Row 0) stays at
		#   0x200 - Origin - and Wanderer, once it has moved on, does not have it;
		#   the table says it is dynamic, and a fresh listing has Sometimes
		#   or not by the last row;
		#   a game's own names are exact (chimera#218): "wanderer" is another
		#   property than Wanderer, at 0x244 every frame, and "WANDERER",
		#   which the core does not have, is never there.
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" \
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
assert names["Mirror"]["writable"] is True and names["Wanderer"]["offset"] == 0x200 + 4 * last
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
			crun_game "$epkg" "$hrom" "$movie" --history-out "$work/$htag.hist" 				> "$work/$htag.save.log" 2>&1
			crun_game "$epkg" "$hrom" "$movie" --history-in "$work/$htag.hist" --seek 10 				--dump "RAM=$work/$htag.ram.bin" --dump "VRAM=$work/$htag.vram.bin" 				> "$work/$htag.log" 2>&1
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
			CHIMERA_HISTORY_TRACE=1 crun_game "$epkg" "$trom" "$movie" \
				--greenzone-map "$work/$ttag.whole.map" > "$work/$ttag.whole.log" 2>&1
			whole="$(grep -a -o '[0-9]* total' "$work/$ttag.whole.log" | tail -1 | awk '{print $1}')"
			if [ -z "$whole" ]; then
				report "E:$tname:thinned" FAIL "could not weigh the history kept whole (see work/$ttag.whole.log)"
				continue
			fi
			thin="--greenzone-bytes $((whole * 4 / 10)) --greenzone-band-goal 1"
			CHIMERA_HISTORY_TRACE=1 crun_game "$epkg" "$trom" "$movie" $thin \
				--greenzone-map "$work/$ttag.map" > "$work/$ttag.plan.log" 2>&1
			target=""
			for into in $(grep -a -o "merged the landing at [0-9]* into [0-9]*" "$work/$ttag.plan.log" | awk '{print $NF}' | sort -n -u); do
				if grep -qx "$into" "$work/$ttag.map" 2>/dev/null && [ "$into" -gt 0 ]; then target="$into"; fi
			done
			if [ -z "$target" ]; then
				report "E:$tname:thinned" FAIL "nothing it kept was ever merged into (see work/$ttag.plan.log)"
				continue
			fi
			crun_game "$epkg" "$trom" "$movie" --frames "$target" \
				--dump "RAM=$work/$ttag.ref.ram.bin" > "$work/$ttag.ref.log" 2>&1
			CHIMERA_HISTORY_TRACE=1 crun_game "$epkg" "$trom" "$movie" --seek "$target" --stop-at-seek $thin \
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
			crun_game "$epkg" "$rrom" "$movie" --record "$work/$rtag.txt" \
				--dump "RAM=$work/$rtag.ram.bin" --dump "VRAM=$work/$rtag.vram.bin" \
				> "$work/$rtag.log" 2>&1
			if [ ! -s "$work/$rtag.txt" ]; then
				report "E:$rname:record" FAIL "no movie recorded (see work/$rtag.log)"
			elif ! cmp -s "$work/$rtag.ram.bin" "$golden_dir/$rname.ram.bin" \
				|| ! cmp -s "$work/$rtag.vram.bin" "$golden_dir/$rname.vram.bin"; then
				report "E:$rname:record" FAIL "recording drove a different machine than playback"
			else
				rm -f "$work/$rtag.replay.ram.bin" "$work/$rtag.replay.vram.bin"
				crun_game "$epkg" "$rrom" "$work/$rtag.txt" \
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

		# A recording that starts as a movie whose LogKey names fewer controls
		# than the machine has (made with an older package): a control it does
		# not name joins the key when pressed, and every line gets its column,
		# so the recording replays to the same machine. Left out, the press
		# reached the machine but not the movie.
		kname=gridWalker.win
		krom="$here/roms/${kname%%.*}.testrom"
		rm -f "$work/$kname.keyed.txt" "$work/$kname.keyed.ram.bin" "$work/$kname.keyed.vram.bin"
		crun_game "$epkg" "$krom" "$here/movies/$kname.txt" --record "$work/$kname.keyed.txt" \
			--record-key "#P1 Up|P1 Down|" > "$work/$kname.keyed.log" 2>&1; krc=$?
		crun_game "$epkg" "$krom" "$work/$kname.keyed.txt" \
			--dump "RAM=$work/$kname.keyed.ram.bin" --dump "VRAM=$work/$kname.keyed.vram.bin" > "$work/$kname.keyed.replay.log" 2>&1
		if [ "$krc" -eq 0 ] && grep -q '^LogKey:#P1 Up|P1 Down|#P1 ' "$work/$kname.keyed.txt" \
			&& cmp -s "$work/$kname.keyed.ram.bin" "$golden_dir/$kname.ram.bin" \
			&& cmp -s "$work/$kname.keyed.vram.bin" "$golden_dir/$kname.vram.bin"; then
			report "E:record:key-gains" PASS "a control the recording's LogKey did not name joined it, and the recording replays to the goldens"
		else
			report "E:record:key-gains" FAIL "rc=$krc, the LogKey did not grow, or the replay differs (see work/$kname.keyed*.log)"
		fi

		# the settings channel, natively: a non-default sync setting must diverge
		crun_game "$epkg" "$here/roms/gridWalker.testrom" "$here/movies/gridWalker.win.txt" 			--settings '{"initFillByte":171}' --dump "RAM=$work/engine.settings.ram.bin" 			> "$work/engine.settings.log" 2>&1
		if [ ! -f "$work/engine.settings.ram.bin" ]; then
			report "E:settings" FAIL "run failed (see work/engine.settings.log)"
		elif cmp -s "$work/engine.settings.ram.bin" "$golden_dir/gridWalker.win.ram.bin"; then
			report "E:settings" FAIL "RAM identical to golden - setting did NOT reach the guest"
		else
			report "E:settings" PASS "RAM diverged - user setting reached the guest"
		fi

		# A PROJECT MADE FROM THE COMMAND LINE (chimera-run --new-project). A game
		# runs only in a project (owner, 2026-10-26), and this is how a game file
		# becomes one without a window. Each movie, made into a project and run
		# through it, must reach the goldens. A setting given is in the project
		# and runs as it did given to the game, the rest are at the package's
		# defaults; a firmware file is pinned by its hash; a clock start is kept;
		# the game goes in the slot that takes its kind of file; and files that
		# do not fit the package's slots, or a game of several files given whole,
		# are refused with a reason and no project written.
		mdir="$work/made-project"
		rm -rf "$mdir" && mkdir -p "$mdir"
		cp "$here/roms/gridWalker.testrom" "$mdir/"
		mmake() { # name, then --new-project's arguments after the project; the exit code is left in mrc
			mtag="$1"; shift
			rm -f "$mdir/$mtag.chimeraProject"
			"$chimera_run" --new-project "$mdir/$mtag.chimeraProject" "$@" > "$mdir/$mtag.make.log" 2>&1
			mrc=$?
		}
		mplay() { # name, then more of chimera-run's arguments
			mtag="$1"; shift
			rm -f "$mdir/$mtag.ram.bin" "$mdir/$mtag.vram.bin"
			"$chimera_run" --project "$mdir/$mtag.chimeraProject" "$epkg" "$@" \
				--dump "RAM=$mdir/$mtag.ram.bin" --dump "VRAM=$mdir/$mtag.vram.bin" > "$mdir/$mtag.log" 2>&1
		}
		mfield() { python3 -c 'import json,sys,project_file; print(json.dumps(project_file.read(sys.argv[1]).get(sys.argv[2]), sort_keys=True))' "$mdir/$1.chimeraProject" "$2" 2>/dev/null; }
		for movie in "$here"/movies/*.txt; do
			mname="$(basename "$movie" .txt)"
			mmake "$mname" "$epkg" "$mdir/${mname%%.*}.testrom" "$movie"
			mplay "$mname"
			if [ "$mrc" -eq 0 ] && cmp -s "$mdir/$mname.ram.bin" "$golden_dir/$mname.ram.bin" \
				&& cmp -s "$mdir/$mname.vram.bin" "$golden_dir/$mname.vram.bin"; then
				report "E:made-project:$mname" PASS "made into a project and run through it, RAM+VRAM byte-identical"
			else
				report "E:made-project:$mname" FAIL "rc=$mrc, or RAM or VRAM differs (see work/made-project/$mname*.log)"
			fi
		done
		mmake settings "$epkg" "$mdir/gridWalker.testrom" "$here/movies/gridWalker.win.txt" --settings '{"initFillByte":171}'
		mplay settings
		if [ "$mrc" -eq 0 ] && [ "$(mfield settings settings)" = '{"initFillByte": 171}' ] \
			&& [ "$(mfield gridWalker.win settings)" = '{"initFillByte": 0}' ] \
			&& [ -s "$mdir/settings.ram.bin" ] && cmp -s "$mdir/settings.ram.bin" "$work/engine.settings.ram.bin"; then
			report "E:made-project:settings" PASS "a setting given is the project's and runs as it did given to the game; one not given is at the package's default"
		else
			report "E:made-project:settings" FAIL "rc=$mrc, settings $(mfield settings settings) and $(mfield gridWalker.win settings), or RAM differs from the game run with it (see work/made-project/settings*.log)"
		fi
		msha="$(sha1sum "$mdir/gridWalker.testrom" | cut -c1-40 | tr a-f A-F)"
		mmake firmware "$epkg" "$mdir/gridWalker.testrom" "$here/movies/gridWalker.win.txt" --firmware "bios=$mdir/gridWalker.testrom"
		mplay firmware --firmware "bios=$mdir/gridWalker.testrom"
		if [ "$mrc" -eq 0 ] && [ "$(mfield firmware firmware)" = "[{\"id\": \"bios\", \"sha1\": \"$msha\"}]" ] \
			&& cmp -s "$mdir/firmware.ram.bin" "$golden_dir/gridWalker.win.ram.bin" && ! grep -q "^\[project\]" "$mdir/firmware.log"; then
			report "E:made-project:firmware" PASS "a firmware file is pinned by its hash, and a run given it says nothing"
		else
			report "E:made-project:firmware" FAIL "rc=$mrc, firmware $(mfield firmware firmware) for $msha (see work/made-project/firmware*.log)"
		fi
		{ cat "$here/roms/gridWalker.testrom"; printf 'CLOCKTEST'; } > "$mdir/clock.testrom"
		printf '[Input]\nLogKey:#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|\n|........|\n|........|\n|........|\n[/Input]\n' > "$mdir/three.txt"
		mmake clock "$epkg" "$mdir/clock.testrom" "$mdir/three.txt" --clock-start 1893553445
		mplay clock
		msecond="$(python3 -c 'import struct,sys; print(struct.unpack("<Q", open(sys.argv[1],"rb").read()[4088:4096])[0])' "$mdir/clock.ram.bin" 2>/dev/null)"
		if [ "$mrc" -eq 0 ] && [ "$msecond" = "1893553445" ]; then
			report "E:made-project:clock-start" PASS "the clock start given is the project's, and the machine reads it"
		else
			report "E:made-project:clock-start" FAIL "rc=$mrc, the machine read ${msecond:-nothing} for 1893553445 (see work/made-project/clock*.log)"
		fi
		# the slot: the package's own, by the game's kind of file
		mslots="$mdir/slots-package"
		mkdir -p "$mslots" && ( cd "$mslots" && unzip -q -o "$epkg" )
		printf '{"slots":[{"id":"disk","title":"Disk","min":0,"max":1,"formats":["dsk"]},{"id":"cartridge","title":"Cartridge","min":0,"max":1,"formats":["testrom"]}]}\n' > "$mslots/file_slots.json"
		mmake slot "$mslots" "$mdir/gridWalker.testrom"
		mslotrc=$mrc
		mmake wrong-slot "$mslots" --file "nosuch=$mdir/gridWalker.testrom"
		mwrongrc=$mrc
		printf '{}' > "$mdir/game.chimeraMultiFile"
		mmake whole "$epkg" "$mdir/game.chimeraMultiFile"
		if [ "$mslotrc" -eq 0 ] && [ "$(python3 -c 'import project_file,sys; print(project_file.read(sys.argv[1])["files"][0]["slot"])' "$mdir/slot.chimeraProject" 2>/dev/null)" = cartridge ] \
			&& [ "$mwrongrc" -ne 0 ] && [ "$mwrongrc" -lt 128 ] && [ ! -e "$mdir/wrong-slot.chimeraProject" ] && grep -q "do not fit the package's slots" "$mdir/wrong-slot.make.log" \
			&& [ "$mrc" -ne 0 ] && [ "$mrc" -lt 128 ] && [ ! -e "$mdir/whole.chimeraProject" ] && grep -q "a file at a time" "$mdir/whole.make.log"; then
			report "E:made-project:slots" PASS "the game goes in the slot that takes its kind; a slot the package lacks and a game given whole are refused, no project written"
		else
			report "E:made-project:slots" FAIL "slot rc=$mslotrc, wrong slot rc=$mwrongrc, whole rc=$mrc (see work/made-project/*.make.log)"
		fi
		# the machine: a package of two machines, each with its own kind of file,
		# and the game's kind picks one, as Chimera picked it for a game opened
		# alone - unless the settings given name it
		mmach="$mdir/machines-package"
		mkdir -p "$mmach" && ( cd "$mmach" && unzip -q -o "$epkg" )
		python3 - "$mmach/waterbox.config" "$mmach/file_slots.json" <<'MACHPY'
import json, sys
slots = json.load(open(sys.argv[2]))
for slot in slots["slots"]:
    slot["formats"] = slot.get("formats", []) + ["runner"]
json.dump(slots, open(sys.argv[2], "w"))
p = sys.argv[1]
cfg = json.load(open(p))
cfg["machineSetting"] = "model"
cfg.setdefault("settings", []).append({"name": "model", "type": "choice", "options": ["walker", "runner"], "default": "walker"})
cfg["machines"] = [{"id": "Synth", "when": ["walker"], "extensions": {".testrom": "Synth"}},
                   {"id": "SynthRunner", "when": ["runner"], "extensions": {".runner": "SynthRunner"}}]
json.dump(cfg, open(p, "w"))
MACHPY
		cp "$mdir/gridWalker.testrom" "$mdir/gridWalker.runner"
		mmake machine-own "$mmach" "$mdir/gridWalker.testrom"
		mown="$(mfield machine-own settings)|$(mfield machine-own headers)"
		mmake machine-other "$mmach" "$mdir/gridWalker.runner"
		mother="$(mfield machine-other settings)|$(mfield machine-other headers)"
		mmake machine-named "$mmach" "$mdir/gridWalker.runner" --settings '{"model":"walker"}'
		mnamed="$(mfield machine-named settings)|$(mfield machine-named headers)"
		if [ "$mown" = '{"initFillByte": 0, "model": "walker"}|{"Platform": "Synth"}' ] \
			&& [ "$mother" = '{"initFillByte": 0, "model": "runner"}|{"Platform": "SynthRunner"}' ] \
			&& [ "$mnamed" = '{"initFillByte": 0, "model": "walker"}|{"Platform": "Synth"}' ]; then
			report "E:made-project:machine" PASS "the game's kind of file picks the machine, and settings that name one are kept"
		else
			report "E:made-project:machine" FAIL "own: $mown; other: $mother; named: $mnamed (see work/made-project/machine-*.make.log)"
		fi
		# a slot the settings open or close: the files are checked against the
		# slots the recorded settings leave, not against none
		mgated="$mdir/gated-package"
		mkdir -p "$mgated" && ( cd "$mgated" && unzip -q -o "$epkg" )
		printf '{"slots":[{"id":"rom","title":"Game","min":1,"max":1,"formats":["testrom"],"exposedWhen":{"setting":"initFillByte","is":0}}]}\n' > "$mgated/file_slots.json"
		mmake gated-open "$mgated" "$mdir/gridWalker.testrom"
		mopenrc=$mrc
		mmake gated-shut "$mgated" "$mdir/gridWalker.testrom" --settings '{"initFillByte":7}'
		if [ "$mopenrc" -eq 0 ] && [ "$mrc" -ne 0 ] && [ "$mrc" -lt 128 ] && grep -q "which these files make unavailable" "$mdir/gated-shut.make.log"; then
			report "E:made-project:gated-slot" PASS "a slot the recorded settings open takes the game; one they close refuses it"
		else
			report "E:made-project:gated-slot" FAIL "open rc=$mopenrc, shut rc=$mrc (see work/made-project/gated-*.make.log)"
		fi
		# the movie named with --movie, which a project of no game file needs:
		# the same input as the movie after the game
		mmake movie-option "$epkg" "$mdir/gridWalker.testrom" --movie "$here/movies/gridWalker.win.txt"
		if [ "$mrc" -eq 0 ] && [ "$(mfield gridWalker.win inputs)" != null ] && [ "$(mfield movie-option inputs)" = "$(mfield gridWalker.win inputs)" ]; then
			report "E:made-project:movie-option" PASS "a movie named with --movie is the project's input, as one after the game"
		else
			report "E:made-project:movie-option" FAIL "rc=$mrc, or the input differs (see work/made-project/movie-option.make.log)"
		fi
	fi
fi

# ---------- Level B ----------
if [ "$level" = "both" ] || [ "$level" = "b" ]; then
	# Chimera's window, run headless (it needs no display): its settings, cache,
	# recovery files and hashes are the witness's own, nothing is carried over
	# from a Chimera installed on this machine, and the cores are build/Cores
	chimera_exe="$repo_root/build/meson-linux/source/ui/chimera"
	[ -x "$chimera_exe" ] || { echo "Chimera's window not built: $chimera_exe (meson setup with -Dqt_ui=enabled)" >&2; exit 1; }
	export ALSOFT_DRIVERS=null CHIMERA_CORES_DIR="$repo_root/build/Cores" CHIMERA_QT_CACHE="$work/qt-cache" \
		CHIMERA_QT_RECOVERY="$work/qt-recovery" CHIMERA_QT_HASHES="$work/qt-file-hashes.txt"
	# a project's greenzone is kept in the per-user cache; this keeps it in the witness's
	export XDG_CACHE_HOME="$work/cache-home" XDG_DATA_HOME="$work/data-home"

	# the window's own settings, empty: each run starts from a copy
	config="$work/settings.json"
	printf '{}\n' > "$config"
	# A game runs only in a project (user-decided, 2026-10-26): each run below
	# opens one, made with chimera-run --new-project beside a copy of its game.
	# bproject <name> <package> <game> [chimera-run's options] prints its path.
	bproject() {
		bpdir="$work/projects/$1"
		rm -rf "$bpdir" && mkdir -p "$bpdir" && cp "$3" "$bpdir/"
		bpgame="$bpdir/$(basename "$3")"
		bpname="$1"; bppkg="$2"; shift 3
		"$repo_root/build/meson-linux/chimera-run" --new-project "$bpdir/$bpname.chimeraProject" "$bppkg" "$bpgame" "$@" \
			> "$bpdir/make.log" 2>&1 && echo "$bpdir/$bpname.chimeraProject"
	}
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
				cp "$config" "$work/settings.$tag.json"
				proj="$(bproject "$tag" "$package" "$rom")"
				( cd "$repo_root" && CHIMERA_JOB="$job" timeout 300 "$chimera_exe" --headless \
					"--config=$work/settings.$tag.json" "--core=$package" \
					"--lua=$here/synth-replay.lua" "--project=$proj" ) > "$work/$tag.log" 2>&1
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

	# --- a package's picture size is room, not a buffer held ---
	# The size a package declares is the most its core can draw: DuckStation's
	# 16x is 12288x9216, 450 MB a buffer. The engine and the window hold room
	# for the picture the core draws, and grow when it draws more; they used to
	# take the declared size up front, for every session at any resolution.
	# The same movie runs with the synthetic package declaring 8192x8192 (256 MB
	# a buffer) and as it is: the machine and the picture are the goldens', and
	# the process's peak memory is within 128 MB of the plain run's (it was
	# 512 MB more).
	if [ "$record" -eq 0 ] && [ -x /usr/bin/time ]; then
		rname=gridWalker.win
		rrom="$here/roms/${rname%%.*}.testrom"
		roompkg="$work/picture-room"
		rm -rf "$roompkg"; mkdir -p "$roompkg"
		(cd "$roompkg" && unzip -q "$repo_root/build/Cores/synth-box.chimeraCore") && python3 -c "
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d['video']['width'] = d['video']['height'] = 8192
json.dump(d, open(p, 'w'))
" "$roompkg/waterbox.config"
		for rkind in plain roomy; do
			rpkg="$repo_root/build/Cores/synth-box.chimeraCore"
			[ "$rkind" = roomy ] && rpkg="$roompkg"
			rjob="$work/job.room-$rkind.txt"
			{
				echo "movie=$here/movies/$rname.txt"
				echo "outram=$work/room-$rkind.ram.bin"
				echo "outvram=$work/room-$rkind.vram.bin"
				echo "meta=$work/room-$rkind.meta.txt"
				echo "mode=simple"
			} > "$rjob"
			rm -f "$work/room-$rkind.ram.bin" "$work/room-$rkind.vram.bin" "$work/room-$rkind.meta.txt"
			cp "$config" "$work/settings.room-$rkind.json"
			rproj="$(bproject "room-$rkind" "$rpkg" "$rrom")"
			( cd "$repo_root" && CHIMERA_JOB="$rjob" timeout 300 /usr/bin/time -f "peak %M" -o "$work/room-$rkind.peak" "$chimera_exe" --headless \
				"--config=$work/settings.room-$rkind.json" "--core=$rpkg" \
				"--lua=$here/synth-replay.lua" "--project=$rproj" ) > "$work/room-$rkind.log" 2>&1
		done
		rplain="$(sed -n 's/^peak \([0-9]*\)$/\1/p' "$work/room-plain.peak" 2>/dev/null)"
		rroomy="$(sed -n 's/^peak \([0-9]*\)$/\1/p' "$work/room-roomy.peak" 2>/dev/null)"
		if ! grep -q "^status=OK" "$work/room-roomy.meta.txt" 2>/dev/null || [ -z "$rplain" ] || [ -z "$rroomy" ]; then
			report "B:box:picture-room" FAIL "no OK meta or no peak memory (see work/room-*.log)"
		elif ! cmp -s "$work/room-roomy.ram.bin" "$golden_dir/$rname.ram.bin" || ! cmp -s "$work/room-roomy.vram.bin" "$golden_dir/$rname.vram.bin"; then
			report "B:box:picture-room" FAIL "with 8192x8192 declared, RAM or VRAM differs from the goldens"
		elif [ $(( rroomy - rplain )) -ge 131072 ]; then
			report "B:box:picture-room" FAIL "with 8192x8192 declared the process peaked at $(( rroomy / 1024 )) MB against $(( rplain / 1024 )) MB: room for the declared size was taken"
		else
			report "B:box:picture-room" PASS "with 8192x8192 declared: the goldens' RAM and picture, and a peak of $(( rroomy / 1024 )) MB against $(( rplain / 1024 )) MB as declared"
		fi
	fi

	# --- settings -> guest channel ---
	# Same package + movie, but with initFillByte set non-zero in the project's
	# SYNC settings (a game's settings are its project's). The guest pre-fills
	# RAM, so the RAM dump MUST diverge from the golden - proving the built-in
	# adapter delivered the project's setting to the guest.
	if [ "$record" -eq 0 ]; then
		sname=gridWalker.win
		srom="$here/roms/${sname%%.*}.testrom"
		scfg="$work/settings.settings.json"
		cp "$config" "$scfg"
		sproj="$(bproject settings "$repo_root/build/Cores/synth-box.chimeraCore" "$srom" --settings '{"initFillByte":171}')"
		sjob="$work/job.settings.txt"
		{
			echo "movie=$here/movies/$sname.txt"
			echo "outram=$work/settings.ram.bin"
			echo "outvram=$work/settings.vram.bin"
			echo "meta=$work/settings.meta.txt"
			echo "mode=simple"
		} > "$sjob"
		rm -f "$work/settings.ram.bin" "$work/settings.meta.txt"
		( cd "$repo_root" && CHIMERA_JOB="$sjob" timeout 300 "$chimera_exe" --headless \
			"--config=$scfg" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--lua=$here/synth-replay.lua" "--project=$sproj" ) > "$work/settings.log" 2>&1
		if [ ! -f "$work/settings.meta.txt" ] || ! grep -q "^status=OK" "$work/settings.meta.txt"; then
			report "S:box:initFillByte" FAIL "no OK meta (see work/settings.log)"
		elif cmp -s "$work/settings.ram.bin" "$golden_dir/$sname.ram.bin"; then
			report "S:box:initFillByte" FAIL "RAM identical to golden - setting did NOT reach the guest"
		else
			report "S:box:initFillByte" PASS "RAM diverged - the project's setting reached the guest"
		fi
	fi

	# --- a real .chimeraProject through the real movie pipeline ---
	# Everything above drives input by script; this makes a project of the win
	# inputs (chimera-run --new-project) and lets MovieSession play it (the
	# project IS the movie, docs/project.md). The engine parses the entries, the
	# frontend latches them, and the dumps must match the same goldens - the
	# movie pipeline is Level B's reason to exist.
	if [ "$record" -eq 0 ]; then
		mname=gridWalker.win
		mrom="$here/roms/${mname%%.*}.testrom"
		mproj="$(bproject movie "$repo_root/build/Cores/synth-box.chimeraCore" "$mrom" "$here/movies/$mname.txt")"
		mjob="$work/job.movie.txt"
		{
			echo "outram=$work/movie.ram.bin"
			echo "outvram=$work/movie.vram.bin"
			echo "meta=$work/movie.meta.txt"
		} > "$mjob"
		rm -f "$work/movie.ram.bin" "$work/movie.vram.bin" "$work/movie.meta.txt"
		cp "$config" "$work/settings.movie.json"
		( cd "$repo_root" && CHIMERA_JOB="$mjob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.movie.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$mproj" "--lua=$here/synth-movie-dump.lua" ) > "$work/movie.log" 2>&1
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
	# guest through the project path) and the log must show the synthetic
	# core's "synth: started" (its Init) exactly once.
	if [ "$record" -eq 0 ]; then
		pname=gridWalker.win
		prom="$here/roms/${pname%%.*}.testrom"
		pdir="$work/project-leg"
		rm -rf "$pdir" && mkdir -p "$pdir"
		cp "$prom" "$pdir/gridWalker.testrom"
		python3 - "$here/movies/$pname.txt" "$pdir/gridWalker.testrom" "$pdir/$pname.chimeraProject" <<'PY'
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "settings": {"initFillByte": 171},
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
PY
		pjob="$work/job.project.txt"
		{
			echo "outram=$work/project.ram.bin"
			echo "outvram=$work/project.vram.bin"
			echo "meta=$work/project.meta.txt"
		} > "$pjob"
		rm -f "$work/project.ram.bin" "$work/project.vram.bin" "$work/project.meta.txt"
		cp "$config" "$work/settings.project.json"
		( cd "$repo_root" && CHIMERA_JOB="$pjob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.project.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$pdir/$pname.chimeraProject" "--lua=$here/synth-movie-dump.lua" ) > "$work/project.log" 2>&1
		boots="$(grep -c "^synth: started" "$work/project.log" || true)"
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

	# --- the MCP server in the real window (docs/mcp-server.md) ---
	# Chimera opens a project with --mcp-server, which is what pressing Start in
	# Tools > MCP Server... does. A script plays the movie to its end, pauses and
	# keeps the window running; mcp-agent.py then plays an AI agent through
	# chimera-mcp. What it reads must be what the references say - memory byte
	# for byte, the input line for line - and the tools only the window can run
	# (the project's markers and branches) must answer. The server's pipe has a
	# name of this run's own.
	if [ "$record" -eq 0 ]; then
		wname=gridWalker.win
		wdir="$work/mcp-window"
		wmcp="$repo_root/build/meson-linux/chimera-mcp"
		rm -rf "$wdir" && mkdir -p "$wdir"
		if [ ! -x "$wmcp" ]; then
			report "M:mcp:window" SKIP "chimera-mcp not built (meson compile -C build/meson-linux)"
		else
			cp "$here/roms/gridWalker.testrom" "$wdir/gridWalker.testrom"
			python3 - "$here/movies/$wname.txt" "$wdir/gridWalker.testrom" "$wdir/$wname.chimeraProject" <<'MCPPY'
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
MCPPY
			cp "$config" "$wdir/settings.json"
			CHIMERA_MCP_NAME="witness$$"; export CHIMERA_MCP_NAME
			# Twice: started as pressing Start does, where the agent may only look;
			# then with --mcp-allow-changes, which is the tick box, where it edits
			# the input and undoes it, inserts and deletes frames, places a marker,
			# saves and loads branches and moves the playback position - and the
			# movie must then still play to the reference memory.
			for wallow in "" changes; do
				wtag="M:mcp:window${wallow:+:$wallow}"
				wsub="$wdir/${wallow:-look}"
				mkdir -p "$wsub"
				cp "$wdir/gridWalker.testrom" "$wdir/$wname.chimeraProject" "$wsub/"
				{
					echo "ready=$wsub/ready"
					echo "done=$wsub/done"
				} > "$wsub/job.txt"
				( cd "$repo_root" && CHIMERA_JOB="$wsub/job.txt" timeout 300 "$chimera_exe" --headless --mcp-server ${wallow:+--mcp-allow-changes} \
					"--config=$wdir/config.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
					"--project=$wsub/$wname.chimeraProject" "--lua=$here/synth-mcp.lua" ) > "$wsub/chimera.log" 2>&1 &
				wpid=$!
				wtries=0
				while [ "$wtries" -lt 600 ] && [ ! -f "$wsub/ready" ] && kill -0 "$wpid" 2>/dev/null; do
					sleep 0.2; wtries=$((wtries+1))
				done
				wrc=99
				if [ -f "$wsub/ready" ]; then
					timeout 180 python3 "$here/mcp-agent.py" "$wmcp" "$golden_dir/$wname.ram.bin" "$golden_dir/$wname.vram.bin" \
						"$here/movies/$wname.txt" - "window${wallow:+-$wallow}" > "$wsub/agent.log" 2>&1
					wrc=$?
				fi
				: > "$wsub/done"
				wait "$wpid"; wexit=$?
				if [ "$wrc" -eq 0 ] && [ "$wexit" -eq 0 ] && grep -q "^\[mcp\] the server is started" "$wsub/chimera.log"; then
					if [ -z "$wallow" ]; then
						report "$wtag" PASS "an agent reads the open project through the window's server: memory, input, markers, branches; changes are refused"
					else
						report "$wtag" PASS "with changes allowed, an agent edits, undoes, seeks and branches in the window, and the movie still plays to the reference memory"
					fi
				elif [ "$wrc" -eq 99 ]; then
					report "$wtag" FAIL "Chimera never said the movie was played (see work/mcp-window/${wallow:-look}/chimera.log)"
				else
					report "$wtag" FAIL "agent rc=$wrc, Chimera rc=$wexit (see work/mcp-window/${wallow:-look}/agent.log and chimera.log)"
				fi
			done
			unset CHIMERA_MCP_NAME
		fi
	fi

	# --- a project the windowless MCP server saved, opened in the window ---
	# What an agent made with no window must be a project like any other: the
	# window opens it, plays its movie and ends with the reference memory. (The
	# game is copied beside it, where the window looks for a project's files.)
	if [ "$record" -eq 0 ]; then
		xsrc="$work/mcp-windowless/work/made.chimeraProject"
		xdir="$work/mcp-windowless/window"
		if [ ! -s "$xsrc" ]; then
			report "M:mcp:windowless:project" SKIP "the windowless check saved no project (see E:mcp:windowless)"
		else
			rm -rf "$xdir" && mkdir -p "$xdir"
			cp "$xsrc" "$xdir/made.chimeraProject"
			cp "$here/roms/gridWalker.testrom" "$xdir/"
			{
				echo "outram=$xdir/ram.bin"
				echo "outvram=$xdir/vram.bin"
				echo "meta=$xdir/meta.txt"
			} > "$xdir/job.txt"
			cp "$config" "$xdir/settings.json"
			( cd "$repo_root" && CHIMERA_JOB="$xdir/job.txt" timeout 300 "$chimera_exe" --headless \
				"--config=$xdir/config.ini" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$xdir/made.chimeraProject" "--lua=$here/synth-movie-dump.lua" ) > "$xdir/chimera.log" 2>&1
			if [ ! -f "$xdir/meta.txt" ] || ! grep -q "^status=OK" "$xdir/meta.txt"; then
				report "M:mcp:windowless:project" FAIL "the window did not open and play it (see work/mcp-windowless/window/chimera.log)"
			elif cmp -s "$xdir/ram.bin" "$golden_dir/gridWalker.win.ram.bin" && cmp -s "$xdir/vram.bin" "$golden_dir/gridWalker.win.vram.bin"; then
				report "M:mcp:windowless:project" PASS "the window opens the project the windowless server saved and plays it to the reference memory"
			else
				report "M:mcp:windowless:project" FAIL "RAM or VRAM differs from the references (see work/mcp-windowless/window/)"
			fi
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
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")][:5]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
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
			cp "$config" "$work/settings.tastudio-$cname.json"
			( cd "$repo_root" && CHIMERA_JOB="$tjob" timeout 300 "$chimera_exe" --headless \
				"--config=$work/settings.tastudio-$cname.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
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
			cp "$config" "$work/settings.greenzone-$gpass.json"
			( cd "$repo_root" && CHIMERA_JOB="$gjob" timeout 300 "$chimera_exe" --headless \
				"--config=$work/settings.greenzone-$gpass.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$gdir/g.chimeraProject" "--lua=$here/synth-greenzone-choice.lua" ) > "$gdir/$gpass.log" 2>&1
			if ! grep -q "^status=OK" "$gdir/$gpass.meta.txt" 2>/dev/null; then
				report "T:box:greenzoneChoice" FAIL "the $gpass run: $(sed -n 's/^detail=//p' "$gdir/$gpass.meta.txt" 2>/dev/null || echo "run failed") (see $gdir/$gpass.log)"
				gfailed=1
				break
			fi
			if [ "$gpass" = first ]; then
				if ! python3 - "$gdir/g.chimeraProject" <<'GZPY'
import json, sys, project_file
p = project_file.read(sys.argv[1])
# TAStudio's part is its settings object as ConfigService writes it: {"o": {...}}
t = (p.get("tastudio") or {}).get("o")
if not isinstance(t, dict) or t.get("GreenzonePeriod") != 1:
    sys.exit("TAStudio's part holds no GreenzonePeriod of 1: " + json.dumps(t)[:200])
t["GreenzonePeriod"] = 7
project_file.write(sys.argv[1], p)
GZPY
				then
					report "T:box:greenzoneChoice" FAIL "the first save wrote no Greenzone choice into the project"
					gfailed=1
					break
				fi
			fi
		done
		if [ "$gfailed" -eq 0 ]; then
			gperiod="$(python3 -c 'import project_file,sys; print(project_file.read(sys.argv[1]).get("tastudio", {}).get("o", {}).get("GreenzonePeriod"))' "$gdir/g.chimeraProject")"
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
		cp "$config" "$work/settings.properties.json"
		( cd "$repo_root" && CHIMERA_JOB="$pjob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.properties.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$pdir/p.chimeraProject" "--lua=$here/synth-game-properties.lua" ) > "$pdir/run.log" 2>&1
		if grep -q "^status=OK" "$pdir/meta.txt" 2>/dev/null; then
			report "T:box:gameProperties" PASS "every kind of property reads and sets its own bytes by name, and the game plays on what was set"
		else
			report "T:box:gameProperties" FAIL "$(sed -n 's/^detail=//p' "$pdir/meta.txt" 2>/dev/null || echo "run failed") (see $pdir/run.log)"
		fi
	fi

	# --- a function called at each read of the controls (issue 344) ---
	# The synth reports each INPUT its machine executes, part way through the
	# frame. A script registers event.oninputpoll, sees it called once in a frame
	# the game reads its pad in, reading the machine's memory from inside it,
	# and no more once it is removed. It leaves another registered, and Chimera
	# must close without an error (it once failed taking that function off the
	# core it had already closed).
	if [ "$record" -eq 0 ]; then
		idir="$work/input-polls-leg"
		rm -rf "$idir" && mkdir -p "$idir"
		cp "$tdir/gridWalker.testrom" "$idir/gridWalker.testrom"
		cp "$tdir/$tname.chimeraProject" "$idir/p.chimeraProject"
		ijob="$work/job.input-polls.txt"
		echo "meta=$idir/meta.txt" > "$ijob"
		cp "$config" "$work/settings.input-polls.json"
		( cd "$repo_root" && CHIMERA_JOB="$ijob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.input-polls.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$idir/p.chimeraProject" "--lua=$here/synth-input-polls.lua" ) > "$idir/run.log" 2>&1
		if grep -q "^status=OK" "$idir/meta.txt" 2>/dev/null && grep -qi "fatal" "$idir/run.log"; then
			report "T:box:inputPolls" FAIL "Chimera stopped with an error as it closed: $(grep -i -m1 "fatal" "$idir/run.log") (see $idir/run.log)"
		elif grep -q "^status=OK" "$idir/meta.txt" 2>/dev/null; then
			report "T:box:inputPolls" PASS "event.oninputpoll is called once in a frame the game reads its pad in, reads memory there, and stops when removed; Chimera closes cleanly with one still registered"
		else
			report "T:box:inputPolls" FAIL "$(sed -n 's/^detail=//p' "$idir/meta.txt" 2>/dev/null || echo "run failed") (see $idir/run.log)"
		fi
	fi

	# --- functions called at memory the machine runs, reads or writes (issue 496) ---
	# The synth names the scopes Code and RAM and calls back at each access a
	# script watches. A script sees the instruction at 0 called once a frame,
	# reads of the status twice with the value memory holds, every write with
	# the value already written; a scope or kind the core lacks refused; none
	# called once removed. It leaves one registered, and Chimera must close
	# without an error.
	if [ "$record" -eq 0 ]; then
		mdir="$work/memory-calls-leg"
		rm -rf "$mdir" && mkdir -p "$mdir"
		cp "$tdir/gridWalker.testrom" "$mdir/gridWalker.testrom"
		cp "$tdir/$tname.chimeraProject" "$mdir/p.chimeraProject"
		mjob="$work/job.memory-calls.txt"
		echo "meta=$mdir/meta.txt" > "$mjob"
		cp "$config" "$work/settings.memory-calls.json"
		( cd "$repo_root" && CHIMERA_JOB="$mjob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.memory-calls.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$mdir/p.chimeraProject" "--lua=$here/synth-memory-calls.lua" ) > "$mdir/run.log" 2>&1
		if grep -q "^status=OK" "$mdir/meta.txt" 2>/dev/null && grep -qi "fatal" "$mdir/run.log"; then
			report "T:box:memoryCalls" FAIL "Chimera stopped with an error as it closed: $(grep -i -m1 "fatal" "$mdir/run.log") (see $mdir/run.log)"
		elif grep -q "^status=OK" "$mdir/meta.txt" 2>/dev/null; then
			report "T:box:memoryCalls" PASS "event.on_bus_exec, on_bus_read and on_bus_write are called at the accesses watched, with the values memory holds, refused where the core cannot watch, and stop when removed; Chimera closes cleanly with one still registered"
		else
			report "T:box:memoryCalls" FAIL "$(sed -n 's/^detail=//p' "$mdir/meta.txt" 2>/dev/null || echo "run failed") (see $mdir/run.log)"
		fi
	fi

	# --- a core that stops, in the real frontend ---
	# The synth core dies on cue (SPEC.md): all eight buttons abort, all but Up
	# follow a wild pointer - the second arrives as a SIGSEGV inside a process
	# whose runtime has signal handlers of its own. Headless Chimera must end the
	# run the ordinary way with the reason and exit code 65, not die in native code, and leave the recovery journal behind.
	if [ "$record" -eq 0 ]; then
		for cue in "abort:|UDLRABsS|:the core aborted" "wild:|.DLRABsS|:the core crashed: it wrote to address"; do
			cname="${cue%%:*}"; rest="${cue#*:}"; cline="${rest%%:*}"; cwant="${rest#*:}"
			cdir="$work/stops-$cname"
			rm -rf "$cdir" && mkdir -p "$cdir"
			cp "$here/roms/gridWalker.testrom" "$cdir/gridWalker.testrom"
			sed "12s/.*/$cline/" "$here/movies/gridWalker.win.txt" > "$cdir/movie.txt"
			python3 - "$cdir/movie.txt" "$cdir/gridWalker.testrom" "$cdir/stops.chimeraProject" <<'PY'
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "id": "0123456789abcdef0123456789abc0de",
    "title": "stops",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
PY
			cjob="$work/job.stops-$cname.txt"
			{
				echo "outram=$cdir/ram.bin"
				echo "outvram=$cdir/vram.bin"
				echo "meta=$cdir/meta.txt"
			} > "$cjob"
			cp "$config" "$work/settings.stops-$cname.json"
			( cd "$repo_root" && CHIMERA_JOB="$cjob" timeout 120 "$chimera_exe" --headless \
				"--config=$work/settings.stops-$cname.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
				"--project=$cdir/stops.chimeraProject" "--lua=$here/synth-movie-dump.lua" ) > "$cdir/log.txt" 2>&1
			crc=$?
			if grep -qE "Got a SIG|SIGSEGV" "$cdir/log.txt"; then
				report "S:frontend:$cname" FAIL "the process reported a crash (see $cdir/log.txt)"
			elif [ "$crc" -ne 65 ]; then
				report "S:frontend:$cname" FAIL "the process ended with $crc, not 65 (see $cdir/log.txt)"
			elif ! grep -q "\[headless\] The core stopped: $cwant" "$cdir/log.txt"; then
				report "S:frontend:$cname" FAIL "the stopped core was not reported (see $cdir/log.txt)"
			elif ! find "$CHIMERA_QT_RECOVERY" -type d -name 0123456789abcdef0123456789abc0de 2>/dev/null | grep -q .; then
				report "S:frontend:$cname" FAIL "the recovery journal was not left behind"
			else
				report "S:frontend:$cname" PASS "headless Chimera stops with the reason, lives, and keeps the journal"
			fi
			find "$CHIMERA_QT_RECOVERY" -type d -name 0123456789abcdef0123456789abc0de -exec rm -rf {} + 2>/dev/null
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
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "id": "0123456789abcdef0123456789ab0196",
    "title": "reboot",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
PY
			rjob="$work/job.reboot-$roll.txt"
			{
				echo "outram=$rdir/ram.bin"
				echo "meta=$rdir/meta.txt"
				echo "tastudio=$roll"
				echo "rebootat=30"
			} > "$rjob"
			cp "$config" "$work/settings.reboot-$roll.json"
			( cd "$repo_root" && CHIMERA_JOB="$rjob" timeout 120 "$chimera_exe" --headless \
				"--config=$work/settings.reboot-$roll.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
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
			find "$CHIMERA_QT_RECOVERY" -type d -name 0123456789abcdef0123456789ab0196 -exec rm -rf {} + 2>/dev/null
		done

		# the core dies on cue (all eight buttons, frame 12) after a reboot at
		# frame 5: the stop asks for the work to be kept, of a session whose
		# machine has been replaced once already
		ddir="$work/reboot-dies"
		rm -rf "$ddir" && mkdir -p "$ddir"
		cp "$here/roms/gridWalker.testrom" "$ddir/gridWalker.testrom"
		sed '12s/.*/|UDLRABsS|/' "$here/movies/gridWalker.win.txt" > "$ddir/movie.txt"
		python3 - "$ddir/movie.txt" "$ddir/gridWalker.testrom" "$ddir/reboot.chimeraProject" <<'PY'
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "id": "0123456789abcdef0123456789ab0196",
    "title": "reboot",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
PY
		djob="$work/job.reboot-dies.txt"
		{
			echo "outram=$ddir/ram.bin"
			echo "meta=$ddir/meta.txt"
			echo "tastudio=0"
			echo "rebootat=5"
		} > "$djob"
		cp "$config" "$work/settings.reboot-dies.json"
		( cd "$repo_root" && CHIMERA_JOB="$djob" timeout 120 "$chimera_exe" --headless \
			"--config=$work/settings.reboot-dies.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
			"--project=$ddir/reboot.chimeraProject" "--lua=$here/synth-reboot.lua" ) > "$ddir/log.txt" 2>&1
		drc=$?
		if grep -qE "Got a SIG|SIGSEGV" "$ddir/log.txt"; then
			report "R:frontend:dies-after" FAIL "the process reported a crash (see $ddir/log.txt)"
		elif ! grep -q "\[synth-reboot\] rebooted" "$ddir/log.txt"; then
			report "R:frontend:dies-after" FAIL "the core was never rebooted (see $ddir/log.txt)"
		elif [ "$drc" -ne 65 ]; then
			report "R:frontend:dies-after" FAIL "the process ended with $drc, not 65 (see $ddir/log.txt)"
		elif ! grep -q "\[headless\] The core stopped: the core aborted" "$ddir/log.txt"; then
			report "R:frontend:dies-after" FAIL "the stopped core was not reported (see $ddir/log.txt)"
		elif ! find "$CHIMERA_QT_RECOVERY" -type d -name 0123456789abcdef0123456789ab0196 2>/dev/null | grep -q .; then
			report "R:frontend:dies-after" FAIL "the recovery journal was not left behind"
		else
			report "R:frontend:dies-after" PASS "a core that dies after a reboot is survived, and the work is kept"
		fi
		find "$CHIMERA_QT_RECOVERY" -type d -name 0123456789abcdef0123456789ab0196 -exec rm -rf {} + 2>/dev/null
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
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "title": "gridWalker.win",
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": "gridWalker.testrom", "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
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
		cp "$config" "$work/settings.encode.json"

		# ffmpeg is part of a build now, not something a person is asked to fetch
		# the first time they want a video - so the gate builds it in too (once:
		# tools/build-ffmpeg.sh keeps what it built under build/deps).
		if ! "$repo_root/tools/build-ffmpeg.sh" linux "$repo_root/build/dll" > "$work/ffmpeg.log" 2>&1; then
			report "V:box:encode" FAIL "no ffmpeg to encode with (see work/ffmpeg.log)"
		else
			( cd "$repo_root" && CHIMERA_JOB="$ejob" timeout 300 "$chimera_exe" --headless \
				"--config=$work/settings.encode.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
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
			elif [ -n "$(find "$work" "$repo_root/build" -name 'encode-restore-*.state' 2>/dev/null | head -1)" ]; then
				# where the user was is kept as a state file while the encode runs
				# (chimera issue 282: a state over 2 GB did not fit the byte array
				# it was kept in), and the file goes when the emulator is handed back
				report "V:box:encode" FAIL "the state kept to hand the emulator back was left behind: $(find "$work" "$repo_root/build" -name 'encode-restore-*.state' | head -1)"
			else
				report "V:box:encode" PASS "$eframes frames written, emulator handed back on frame $eafter from a state file, which was then removed"
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
import hashlib, json, sys, project_file
entries = [l.rstrip("\r\n") for l in open(sys.argv[1]) if l.startswith("|")]
logkey = "#P1 Up|P1 Down|P1 Left|P1 Right|P1 A|P1 B|P1 Select|P1 Start|"
sha1 = hashlib.sha1(open(sys.argv[2], "rb").read()).hexdigest().upper()
project_file.write(sys.argv[3], {
    "title": sys.argv[1],
    "core": {"name": "Synth", "version": "", "sha1": ""},
    "headers": {"MovieVersion": "Chimera Project File v1.1", "Platform": "Synth"},
    "files": [{"name": sys.argv[2].split("/")[-1], "sha1": sha1, "slot": "rom"}],
    "inputs": {"columns": [g.split("|")[:-1] for g in logkey.split("#")[1:]], "entries": entries},
})
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
		cp "$config" "$work/settings.reopen.json"
		( cd "$repo_root" && CHIMERA_JOB="$rjob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.reopen.json" "--core=$repo_root/build/Cores/synth-box.chimeraCore" \
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

	# --- a game runs only in a project ---
	# A game file named alone, with its core given, and no window to start New
	# Project in: the run must refuse it, say so, and produce no machine
	# (user-decided, 2026-10-26). If it runs, a game is being opened outside a
	# project again.
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
		cp "$config" "$work/settings.discovery.json"
		( cd "$repo_root" && CHIMERA_JOB="$djob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.discovery.json" "--core=$package" "--lua=$here/synth-replay.lua" \
			"$here/roms/${dname%%.*}.testrom" ) > "$work/discovery.log" 2>&1; drc=$?
		if [ -f "$work/discovery.meta.txt" ] && grep -q "^status=OK" "$work/discovery.meta.txt"; then
			report "D:box:projectOnly" FAIL "the game ran without a project"
		elif [ "$drc" -ne 2 ] || ! grep -q "a game opens only in a project" "$work/discovery.log"; then
			report "D:box:projectOnly" FAIL "rc=$drc, or the refusal was not said (see work/discovery.log)"
		else
			report "D:box:projectOnly" PASS "a game file alone is refused with a reason, and nothing runs"
		fi

		# (The helpers New Project asks before a project is made - suggested
		# settings, compile sessions - are chimera-run's in Chimera's window,
		# and level E asks each of them of a project: E:suggest, E:precompile.
		# A core's own tools run there too, with no project: E:core-tool.)

		# --- keybinds ship with the package ---
		# The frontend has no bindings of its own: a controller it has never seen is played however
		# the package that declared it says (the package's default_keybinds.json). The window keeps
		# only the keys a person changed, so with fresh settings the machine's bindings must be the
		# package's, or the core arrives unplayable. A script reads them (dump-controls.lua).
		kname=gridWalker.win
		kjob="$work/job.keybinds.txt"
		printf 'out=%s/keybinds.controls.json\nmeta=%s/keybinds.meta.txt\n' "$work" "$work" > "$kjob"
		rm -f "$work/keybinds.meta.txt" "$work/keybinds.controls.json"
		cp "$config" "$work/settings.keybinds.json"
		kproj="$(bproject keybinds "$package" "$here/roms/${kname%%.*}.testrom")"
		( cd "$repo_root" && CHIMERA_JOB="$kjob" timeout 300 "$chimera_exe" --headless \
			"--config=$work/settings.keybinds.json" "--core=$package" "--lua=$here/dump-controls.lua" \
			"--project=$kproj" ) > "$work/keybinds.log" 2>&1
		if [ ! -f "$work/keybinds.meta.txt" ] || ! grep -q "^status=OK" "$work/keybinds.meta.txt"; then
			report "K:box:keybinds" FAIL "run failed (see work/keybinds.log)"
		elif python3 "$here/check-keybinds.py" "$work/keybinds.controls.json" \
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
