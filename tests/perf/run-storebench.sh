#!/bin/bash
# Builds and runs storebench (see its own header, and docs/state-manager.md).
#
# Not part of any gate: it answers a question about cost rather than about
# correctness. It is here because the table in docs/state-manager.md - how much
# of a capture is the copy, which is the whole case for a helper thread - has
# to be reproducible by somebody who doubts it.
#
# Usage:
#   ./run-storebench.sh                                  # the sweep the doc reports
#   ./run-storebench.sh <MB> <pages/frame> [frames] [dirty MB]
#
# The dirty figure is what the ANCHOR carries, and the machines worth asking
# about are the ones with big arenas: a Game Boy's whole state is smaller than
# one page of the answer.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
box="${MINIBOX_DIR:-$root/extern/chimera-common-minibox}"
lib="$box/build/meson-linux/source/host/libminiboxhost.a"
[ -f "$lib" ] || { echo "build miniBox first: meson compile -C $box/build/meson-linux" >&2; exit 1; }
out="${TMPDIR:-/tmp}/storebench"
cc -O2 -o "$out" "$here/storebench.c" \
	-I"$box/source/host" -I"$box/source/include" "$lib" -lpthread

if [ $# -gt 0 ]; then exec "$out" "$@"; fi
# a handheld, a mid-weight console, a heavy one, and a machine the size of a PS3
"$out" 64 64 40 8
"$out" 256 256 40 32
"$out" 2048 256 40 256
"$out" 2048 1024 40 1024
"$out" 8192 1024 20 1024
