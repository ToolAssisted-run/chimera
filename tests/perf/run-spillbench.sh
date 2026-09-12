#!/bin/bash
# Builds and runs spillbench (see its own header, and docs/state-manager.md).
#
# Not part of any gate: it answers a question about cost rather than about
# correctness. It is here because the claim that moving the writer off the
# capture path is worth having - and the shapes of machine where it is NOT -
# have to be reproducible by somebody who doubts them.
#
# Usage:
#   ./run-spillbench.sh                    # the sweep the doc reports
#   ./run-spillbench.sh <machine MB> <frames> <budget MB> <dir> <KB/frame> <anchor spacing> <disk budget x>
#
# The last argument is the file's budget as a multiple of the memory budget;
# 0 leaves the file unbounded, which takes compaction out of the picture.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="${CHIMERA_BUILD:-$root/build/meson-linux}"
bench="$build/spillbench"
if [ ! -x "$bench" ]; then
	meson compile -C "$build" spillbench >/dev/null || {
		echo "build it first: meson compile -C $build spillbench" >&2; exit 1; }
fi
work="${TMPDIR:-/tmp}/spillbench-work"

if [ $# -gt 0 ]; then exec "$bench" "$@"; fi

# The realistic shape first: a budget many times a machine state, which is what
# the defaults are (four gigabytes against a machine of a few hundred
# megabytes), and a run long enough to fill it.
"$bench" 8 2000 128 "$work" 512 60 2
"$bench" 32 1500 256 "$work" 1024 60 2
# A mid-weight machine whose frames are big enough to fill the budget between
# anchors, which is where the writer has most to take.
"$bench" 32 400 8 "$work" 2048 60 8
"$bench" 16 400 6 "$work" 4096 80 8
# The pathological shape: a budget SMALLER than one machine state, so the
# history thrashes and the spill file is rewritten constantly. Kept because it
# is where two regressions were found - a barrier taken before deciding not to
# compact, and compaction deciding on a file that had not been written yet.
"$bench" 8 600 4 "$work" 512 40 8
# And the shape where this phase does NOTHING, on purpose: a big machine whose
# frames are small, where the whole of the worst frame is the anchor being
# taken and no writer can help until anchors are taken differently (phase 3).
"$bench" 64 400 16 "$work" 256 30 8
