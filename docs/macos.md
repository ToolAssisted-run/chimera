# macOS (x86-64)

This is the state of the macOS port: what is built, what has been tested, and
what has not. The port is for the x86-64 processor. On an Apple silicon Mac the
x86-64 build runs under Rosetta 2. A native Apple silicon build is a separate
piece of work and is not done here.

## What builds and runs

The engine, `chimera-run`, `chimera-mcp` and every native library the engine
loads (Lua, SQLite, zstd, SDL2, OpenAL Soft, miniBox) build with the same
meson build as Linux, given a cross file that names the processor:

```sh
git submodule update --init --recursive
meson setup build/meson-macos-x86_64 --prefix "$PWD/build/x86_64-macos" --libdir dll \
  --cross-file extern/meson/macos-x86_64.ini -Dqt_ui=disabled
meson compile -C build/meson-macos-x86_64
meson install -C build/meson-macos-x86_64
meson test -C build/meson-macos-x86_64
```

Requirements: Xcode's command line tools, and Homebrew's meson, ninja and
cmake. The cross file works on an Intel Mac as well. On either kind of Mac the
tests run, because the result can be run (on Apple silicon through Rosetta 2).

The window (Qt) is not built yet: it needs an x86-64 Qt 6.12, and Homebrew's
on an Apple silicon Mac is arm64.

## miniBox on macOS

miniBox is the sandbox every core runs in. Two things are different on macOS.

**Memory.** Linux makes a block of the guest's memory with a memory file mapped
twice (the guest's view, whose protection miniBox manages, and the host's view,
which is always writable). macOS has no such file, so the block is one mapping
that nobody touches, and each view is a `mach_vm_remap` of it. Both views see
every write, and each has a protection of its own.

**The `gs` register.** A guest finds its context through `gs:0x18`. On Linux
nothing else uses `gs`, so miniBox points it at its own block for good. On
macOS the operating system uses `gs` for the thread's own data (`gs:0` is the
`pthread_t`, `gs:0x18` is the Mach thread port), and a program cannot read or
write the `gs` base with `rdgsbase` and `wrgsbase` (they are illegal
instructions, Rosetta 2 included). So the `gs` base is swapped every time
control crosses between the guest and the host: the guest's block while guest
code runs, the operating system's whenever host code runs. The swap is made
with the system call that libsystem itself makes to set a thread's data
pointer. It happens when a guest is entered and left, when the guest makes a
system call, when it calls back into the host, and in every signal handler.

Measured under Rosetta 2 (macOS 27, an M1): the swap works and restores
exactly, and a swap in and out costs about 530 ns. A real Intel Mac has not
been tried.

A guest that uses `%fs` for its thread-local storage (a Rust guest does)
cannot run on macOS, because `%fs` cannot be set from a program either. The C
and C++ guests miniBox's own toolchain makes use `gs` and are not affected.

A write to a protected page raises `SIGBUS` on macOS, where Linux raises
`SIGSEGV`. miniBox handles both the same way.

## The GPU bridge

OpenGL is Apple's, through CGL, a context with no window. It stops at OpenGL
4.1, so the entry points of 4.2 to 4.6 are refused, as the bridge refuses any
entry point a driver does not have. Mesa and Vulkan are not part of this port.

## What has been tested

- miniBox's unit tests: all pass.
- The engine's tests: all pass.

What has not been tried: running a core. miniBox's tests that run a guest are
written for Linux. See docs/design-principles.md for the date and numbers.
