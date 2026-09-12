#!/bin/bash
# Builds and runs planbench (see its own header, and docs/state-manager.md).
#
# Not part of any gate: it answers a question about cost rather than about
# correctness - though it does check that the two states come out the same, and
# returns non-zero if they do not, because a faster anchor that is not the same
# anchor is worth nothing.
#
# Usage:
#   ./run-planbench.sh                                  # the sweep the doc reports
#   ./run-planbench.sh <MB> <dirty MB> <pages written while it fills>
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "$here/../.." && pwd)"
box="${MINIBOX_DIR:-$root/extern/chimera-common-minibox}"
lib="$box/build/meson-linux/source/host/libminiboxhost.a"
[ -f "$lib" ] || { echo "build miniBox first: meson compile -C $box/build/meson-linux" >&2; exit 1; }
out="${TMPDIR:-/tmp}/planbench"
cc -O2 -o "$out" "$here/planbench.c" \
	-I"$box/source/host" -I"$box/source/include" "$lib" -lpthread

if [ $# -gt 0 ]; then exec "$out" "$@"; fi

# A machine standing perfectly still, which is the best case and says what the
# holds and the walk cost on their own.
"$out" 256 128 0
# And machines that keep running while the state fills, which is the real one:
# every page the guest writes that the copy has not reached yet faults, and the
# handler copies it before the write lands.
"$out" 64 32 500
"$out" 256 128 1000
"$out" 1024 512 2000
