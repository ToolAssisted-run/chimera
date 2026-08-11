#!/bin/sh
# Builds synth.wbx (the waterboxed flavor (c) Synth core) and synth-run-box (its
# Level A tester) using the miniBox toolchain + host from extern/miniBox.
#
# Prereq: the miniBox guest toolchain is bootstrapped and the host is built:
#   extern/miniBox/toolchain/build-toolchain.sh
#   meson setup extern/miniBox/build && ninja -C extern/miniBox/build
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
mb="$(cd "$here/../../../extern/miniBox" && pwd)"
sysroot="$mb/toolchain/sysroot"
musl_gcc="$sysroot/bin/musl-gcc"
[ -x "$musl_gcc" ] || { echo "miniBox toolchain not built; run extern/miniBox/toolchain/build-toolchain.sh" >&2; exit 1; }

cflags="-fvisibility=hidden -I$mb/toolchain/emulibc -Wall -mcmodel=large \
	-mstack-protector-guard=global -fno-pic -fno-pie -fcf-protection=none -O2 -DNDEBUG -std=c11"
emulibc_o="$mb/toolchain/emulibc/obj/release/emulibc.c.o"
if [ ! -f "$emulibc_o" ]; then
	mkdir -p "$mb/toolchain/emulibc/obj/release"
	$musl_gcc -c $cflags -o "$emulibc_o" "$mb/toolchain/emulibc/emulibc.c"
fi

# guest: the machine (synthcore.c, identical to flavors a/b) + the wbx wrapper
$musl_gcc -c $cflags -o "$here/synthcore.o" "$here/../native/synthcore.c"
$musl_gcc -c $cflags -o "$here/synth_wbx.o" "$here/synth_wbx.c"
$musl_gcc -o "$here/synth.wbx" $cflags -static -no-pie -Wl,--eh-frame-hdr,-O2 \
	-T "$mb/toolchain/linkscript.T" "$here/synth_wbx.o" "$here/synthcore.o" "$emulibc_o" -lgcc
echo "built synth.wbx"

# tester: links the miniBox host
host_lib="$mb/build/runtime-c/libminiboxhost.so"
[ -f "$host_lib" ] || { echo "miniBox host not built; run: meson setup $mb/build && ninja -C $mb/build" >&2; exit 1; }
gcc -O2 -g -Wall -I"$mb/runtime-c/include" -o "$here/synth-run-box" "$here/synth-run-box.c" \
	"$host_lib" -Wl,-rpath,"$mb/build/runtime-c"
echo "built synth-run-box"
