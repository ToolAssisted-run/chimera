# The six builds

Chimera is built, tested and published for two kinds of processor (CPU), x64
and arm64, on three systems, Windows, Linux and macOS (owner, 2026-11-04).
This page says which build to download, how each is made, what is tested on
each, and what does not run on some of them yet. macOS has a page of its own:
[macos.md](macos.md).

## Which to download

On Windows or Linux you most likely want x64; take arm64 only if you know you
are using an arm64 device. On a Mac, take arm64 for Apple silicon (M1 and
later) and x64 for an Intel Mac.

| Build | File | Made on | Started by the release on |
| --- | --- | --- | --- |
| Windows x64 | `chimera-win-x64-<commit>.zip` | Linux, with mingw-w64's GCC | windows-2025 |
| Windows arm64 | `chimera-win-arm64-<commit>.zip` | Linux, with llvm-mingw's clang | windows-11-arm |
| Linux x64 | `chimera-linux-x64-<commit>.tar.gz` | ubuntu-latest | ubuntu-latest |
| Linux arm64 | `chimera-linux-arm64-<commit>.tar.gz` | ubuntu-26.04-arm | ubuntu-26.04-arm |
| macOS x64 (Intel) | `chimera-macos-x64-<commit>.zip` | macos-15-intel | macos-15-intel |
| macOS arm64 (Apple silicon) | `chimera-macos-arm64-<commit>.zip` | macos-26 | macos-26 |

The names in the last two columns are GitHub's machines (runners). The release
builds all six and publishes them together, or none. Each is started once
before it is published (`--version`, which must name the commit). A Windows
build cannot start on the Linux machine that makes it, so it is started on a
Windows machine of its CPU.

Each carries the window, its Qt, the helper programs (`chimera-run`,
`chimera-mcp`) and the ffmpeg Encode Video uses, all for its CPU. The Mac
builds are apps (`Chimera.app`), signed by no developer, so macOS asks before
opening one the first time; see [macos.md](macos.md).

## Cores on each

A core's package is machine code for one CPU, and runs on every system of that
CPU: one x64 package runs on Windows, Linux and an Intel Mac, one arm64 package
on the three arm64 builds.

- **Most cores have no arm64 package yet.** On 2026-11-05 every official core
  published x64 only; of the third-party cores, SRB2 published both. An arm64
  build runs only the cores that have an arm64 package.
- **An Apple silicon Mac can run the Intel build** under Rosetta 2, Apple's
  translator, with x64 packages (measured on an M1, [macos.md](macos.md)).
  Windows on arm64 also runs x64 programs, but the x64 build has not been
  tried there.
- **Intel Mac (and the Intel build on Apple silicon): thread-local variables.**
  A core that has them is refused, with the reason, until it is built again
  with the miniBox this build carries; 17 of the 30 cores tried have them.
- **Apple silicon: no GuestFaultHandler.** A core that asks for the faults of
  single 4 KiB pages (RPCS3, Xenia) is refused, with the reason, on a machine
  whose memory pages are larger: every Apple silicon Mac (16 KiB), and Linux
  on one (Asahi Linux).
- **Graphics on a Mac** are Apple's OpenGL, which stops at 4.1. Mesa and
  Vulkan are not part of the Mac builds.

## How each is built

- **Linux** is built on the machine it is for, x64 or arm64. Qt is built from
  source (`tools/build-qt-linux.sh`), and `tools/build-bundle.sh --platform
  linux` makes the bundle.
- **Windows** is cross-built on Linux: x64 with mingw-w64's GCC, arm64 with
  llvm-mingw (clang and libc++, a fixed release checked by its SHA256:
  `tools/fetch-llvm-mingw.sh`). Qt is built from source with the same compiler
  (`tools/build-qt-windows.sh <host qt> <qt source> <out> x64|arm64`), and
  `tools/build-bundle.sh --platform windows --cpu x64|arm64` makes the bundle.
- **macOS** is built on the kind of Mac it is for, with the cross file that
  names the CPU (`extern/meson/macos-x86_64.ini`, `macos-aarch64.ini`), and
  `tools/build-app-macos.sh` makes the app.
- **ffmpeg** is built for each by `tools/build-ffmpeg.sh`
  ([encode-video.md](encode-video.md)).

## What is tested, and where

The tests run on GitHub's machines with the synthetic test core
(`tests/synth`), built for each CPU. No GitHub machine has a graphics card, so
the GPU bridge and the real cores are not run there.

| Build | Tests | On |
| --- | --- | --- |
| Linux x64, arm64 | every meson test (engine, view models, window), the witness at both levels | ubuntu-latest, ubuntu-26.04-arm |
| Windows x64, arm64 | 57 of those 63 tests, built on Linux, packed with the bundle (`tools/export-tests.py`) and run against it (`tools/run-tests.py`) | windows-2025, windows-11-arm |
| macOS x64, arm64 | every meson test built for macOS | macos-15-intel, macos-26 |

Windows has no test of the desktop's own file chooser (Linux's), of a gamepad
through a stand-in SDL (a Linux library) or of the MCP server in the window
(a shell script runs it), and the two tests that check the code's texts run
on Linux, as they give the same answer everywhere. On Windows the window's
tests use the bundle's own Qt, so they test what a person downloads. They
draw with no screen, in the fonts the Linux tests use (DejaVu), which the
test pack carries. The witness, which drives the whole program with the
synthetic core, runs on Linux only.

A pull request that changes how the bundles are made also builds all six as
a trial, without publishing them.

## Tried on a person's machine

- Windows x64 and Linux x64: where the cores are made and tried with games,
  on graphics cards.
- Linux arm64: on an Apple M1 under Asahi Linux (pull request 309), the SRB2
  core's arm64 package played, and the GPU bridge had OpenGL 4.6 from Mesa;
  that was before the window moved to Qt.
- macOS: the Intel build under Rosetta 2 on an M1, with four x64 cores and
  the window drawn without a screen ([macos.md](macos.md)).

Not tried: Windows on arm64 on a person's machine, the Apple silicon app, a
real Intel Mac, and the window on a Mac's screen or with its graphics card.
