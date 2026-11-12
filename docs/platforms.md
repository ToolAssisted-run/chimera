# The six builds

Chimera is built, tested and published for two kinds of processor (CPU), x86_64
and aarch64, on three systems, Windows, Linux and macOS (owner, 2026-11-04).
This page says which build to download, how each is made, what is tested on
each, and what does not run on some of them yet. macOS has a page of its own:
[macos.md](macos.md).

## Which to download

On Windows or Linux you most likely want x86_64; take aarch64 only if you know
you are using an ARM device. On a Mac, take aarch64 for Apple silicon (M1 and
later) and x86_64 for an Intel Mac.

| Build | File | Made on | Started by the release on |
| --- | --- | --- | --- |
| Windows x86_64 | `chimera-win-x86_64-<commit>.zip` | Linux, with mingw-w64's GCC | windows-2025 |
| Windows aarch64 | `chimera-win-aarch64-<commit>.zip` | Linux, with llvm-mingw's clang | windows-11-arm |
| Linux x86_64 | `chimera-linux-x86_64-<commit>.tar.gz` | ubuntu-latest | ubuntu-latest |
| Linux aarch64 | `chimera-linux-aarch64-<commit>.tar.gz` | ubuntu-26.04-arm | ubuntu-26.04-arm |
| macOS x86_64 (Intel) | `chimera-macos-x86_64-<commit>.zip` | macos-15-intel | macos-15-intel |
| macOS aarch64 (Apple silicon) | `chimera-macos-aarch64-<commit>.zip` | macos-26 | macos-26 |

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

A core's package is machine code for one CPU. It says itself which of the six
systems it runs on, in `runsOn` in its `waterbox.config` (for example
`"runsOn": ["windows-x86_64", "linux-x86_64"]`), and Chimera runs it only
there: elsewhere the core is listed with the reason and not opened. A package
that does not say is refused. Chimera keeps no list of which core runs where
(user-decided, 2026-11-05); a core that cannot run on one of the six leaves it
out, as Ikemen GO leaves out the Intel Mac.

- **Some cores have an aarch64 package.** FreeJ2ME was the first official
  core to publish both (its Nightly 2026-11-07, issue 536). Since 2026-11-07
  these emulator cores are built and tested on an aarch64 runner too, and
  their nightlies carry both packages: 86Box (without its recompiler on
  aarch64), AppleWin, bsnes v115 (since it joined, 2026-11-12), Cadmium,
  DOSBox-X,
  FBNeo, Fuse, Gambatte, Genesis Plus GX, Mednafen, Mesen, NesHawk, Opera,
  PUAE, QuickerNES, Snes9x, Stella, TIC-80, Uzem, VICE, Virtual Jaguar,
  ZEsarUX and OpenBOR, and the game cores dsda, RSDKv3, RSDKv5 and RSDKv5U.
  Game cores are not ported otherwise (user-decided, 2026-11-07). The other
  cores publish x86_64 only. Of the third-party cores, SRB2 publishes both. An
  aarch64 build runs only the cores that have an aarch64 package.
- **An Apple silicon Mac can run the Intel build** under Rosetta 2, Apple's
  translator, with x86_64 packages (measured on an M1, [macos.md](macos.md)).
  Windows on aarch64 also runs x86_64 programs, but the x86_64 build has not been
  tried there.
- **Intel Mac (and the Intel build on Apple silicon): thread-local variables.**
  A core that has them must be built with the miniBox this build carries, which
  lists where its code reads them ([porting-a-core.md](porting-a-core.md)). One
  that is not, or whose code reads them in a way the list cannot hold (the Go
  code of Ikemen GO), is refused with the reason, and should not list
  `macos-x86_64`.
- **Apple silicon: no GuestFaultHandler.** A core that asks for the faults of
  single 4 KiB pages (RPCS3, Xenia) is refused, with the reason, on a machine
  whose memory pages are larger: every Apple silicon Mac (16 KiB), and Linux
  on one (Asahi Linux).
- **Graphics on a Mac** are Apple's OpenGL, which stops at 4.1. Mesa and
  Vulkan are not part of the Mac builds.

## How each is built

- **Linux** is built on the machine it is for, x86_64 or aarch64. Qt is built from
  source (`tools/build-qt-linux.sh`), and `tools/build-bundle.sh --platform
  linux` makes the bundle.
- **Windows** is cross-built on Linux: x86_64 with mingw-w64's GCC, aarch64 with
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
| Linux x86_64, aarch64 | every meson test (engine, view models, window), the witness at both levels | ubuntu-latest, ubuntu-26.04-arm |
| Windows x86_64, aarch64 | 57 of those 63 tests, built on Linux, packed with the bundle (`tools/export-tests.py`) and run against it (`tools/run-tests.py`) | windows-2025, windows-11-arm |
| macOS x86_64, aarch64 | every meson test built for macOS | macos-15-intel, macos-26 |

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

- Windows x86_64 and Linux x86_64: where the cores are made and tried with games,
  on graphics cards.
- Linux aarch64: on an Apple M1 under Asahi Linux (pull request 309), the SRB2
  core's aarch64 package played, and the GPU bridge had OpenGL 4.6 from Mesa;
  that was before the window moved to Qt.
- macOS: the Intel build under Rosetta 2 on an M1, with four x86_64 cores and
  the window drawn without a screen ([macos.md](macos.md)).

Not tried: Windows on aarch64 on a person's machine, the Apple silicon app, a
real Intel Mac, and the window on a Mac's screen or with its graphics card.
