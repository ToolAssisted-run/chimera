#!/bin/bash
# The MCP server in the Qt window (Tools > MCP Server..., docs/mcp-server.md),
# tested as the witness tests Chimera's: the window opens a project with the
# server started and tests/synth/synth-mcp.lua running, which plays the movie
# and says when it is ready; tests/synth/mcp-agent.py then plays an AI agent
# through chimera-mcp, and the window must quit by itself when it is done.
#   mcp-window.sh <chimera> <chimera-mcp> <repository> <work folder> [changes]
# With "changes" the agent may make changes (--mcp-allow-changes): it edits,
# undoes, seeks, branches, runs Lua and makes a project; without, it reads,
# and every change is refused.
qt=$1 mcp=$2 repo=$3 work=$4 allow=$5
name=gridWalker.win
dir=$work/${allow:-look}
rm -rf "$dir"
mkdir -p "$dir"
cp "$repo/tests/synth/roms/gridWalker.testrom" "$dir/"
# the movie as a project of the synthetic core, as the witness makes it
PYTHONPATH="$repo/tests/synth" python3 - "$repo/tests/synth/movies/$name.txt" "$dir/gridWalker.testrom" "$dir/$name.chimeraProject" <<'MCPPY' || exit 1
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
printf 'ready=%s/ready\ndone=%s/done\n' "$dir" "$dir" > "$dir/job.txt"
export CHIMERA_MCP_NAME="qtmcp$$" CHIMERA_JOB="$dir/job.txt" CHIMERA_CORES_DIR="$repo/build/Cores"
export QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software ALSOFT_DRIVERS=null
export CHIMERA_QT_CONFIG="$dir/settings.json" CHIMERA_QT_CACHE="$dir/cache" CHIMERA_QT_RECOVERY="$dir/recovery"
timeout -s KILL 300 "$qt" "$dir/$name.chimeraProject" "$repo/build/Cores/synth-box.chimeraCore" \
	--mcp-server ${allow:+--mcp-allow-changes} --lua "$repo/tests/synth/synth-mcp.lua" > "$dir/chimera.log" 2>&1 &
pid=$!
for i in $(seq 600); do [ -f "$dir/ready" ] && break; kill -0 $pid 2>/dev/null || break; sleep 0.2; done
rc=99
if [ -f "$dir/ready" ]; then
	timeout 180 python3 "$repo/tests/synth/mcp-agent.py" "$mcp" "$repo/tests/synth/goldens/$name.ram.bin" \
		"$repo/tests/synth/goldens/$name.vram.bin" "$repo/tests/synth/movies/$name.txt" - "window${allow:+-$allow}" > "$dir/agent.log" 2>&1
	rc=$?
fi
: > "$dir/done"
# the script ends the window once it sees the agent is done
for i in $(seq 100); do kill -0 $pid 2>/dev/null || break; sleep 0.2; done
quit=yes
if kill -0 $pid 2>/dev/null; then
	quit=no
	kill -KILL $pid 2>/dev/null
fi
wait $pid
wrc=$?
if [ $rc -eq 99 ]; then
	echo "FAIL: the window never said the movie was played (see $dir/chimera.log)"
	exit 1
fi
if [ $rc -ne 0 ]; then
	cat "$dir/agent.log"
	echo "FAIL: the agent found what is wrong above (see $dir/agent.log and $dir/chimera.log)"
	exit 1
fi
if [ $quit = no ] || [ $wrc -ne 0 ]; then
	echo "FAIL: the window did not quit by itself with code 0 (quit: $quit, code $wrc; see $dir/chimera.log)"
	exit 1
fi
if ! grep -q "^\[mcp\] the server is started" "$dir/chimera.log"; then
	echo "FAIL: the window never said its server started (see $dir/chimera.log)"
	exit 1
fi
echo "PASS: the agent's checks in the window (${allow:-look})"
