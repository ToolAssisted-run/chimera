# miniHawk

**miniHawk is a derivative fork of [BizHawk](https://github.com/TASEmulators/BizHawk).**
The frontend, the tooling, and the architecture it builds on are the original
work of the BizHawk team, and all credit for them belongs to BizHawk's
developers â€” including this fork's maintainer, who is one of BizHawk's
contributors. miniHawk removes; it does not claim.

## What it is

miniHawk is a minimal, **core-agnostic** frontend for creating tool-assisted
speedruns (TAS). It keeps BizHawk's TAS toolchain and removes everything else â€”
most importantly, every emulation core. Cores are not compiled into the
frontend: they are **external, self-contained packages** (a `.zip` with a
manifest, a managed adapter DLL, the native emulator, and any data the core
needs). Loading a core is an explicit act, exactly like loading a ROM â€” a file
prompt for the core package is the first step of opening a ROM. Each core is
maintained in its own repository, by its own authors, under its own license.

What remains of the frontend:

- movie recording/playback, savestates, rewind, frame advance
- RAM Watch / RAM Search / Hex Editor / Cheats
- Lua scripting (core-agnostic APIs) and external tools
- A/V dumping and screenshots
- virtual pads and input configuration

What is deliberately gone: all emulation cores, the game database, firmware
databases, per-system menus/dialogs/tools, and every other piece of
system-specific knowledge. The frontend knows about *emulators in general*,
never about *a particular console* â€” ROM-to-system routing, controller
defaults, cart databases, and palettes all ship inside core packages.

## Approach

- **Deferred reproducibility guarantee.** miniHawk is distributed both as
  source and as precompiled releases, and the resulting binaries may differ â€”
  between the two, and between builds on different systems. That is by design.
  The frontend is deliberately written to *tolerate variation in itself*:
  nothing about a miniHawk build â€” its version, its compiler, the versions of
  its libraries â€” is allowed to matter for whether a movie reproduces. The
  guarantee of emulation reproducibility is instead deferred to each core: a
  movie's reproduction contract is the pair *(movie, core package)*, and given
  the same core package it must sync on any miniHawk that can load it.
- **Complete rebuildability.** This repository contains no precompiled
  objects, libraries, or cores â€” not one committed binary. Everything required
  to produce a working miniHawk is present here, either directly or as pinned
  submodules, and every prerequisite (including all native libraries) is
  compiled from source as part of the ordinary build: the repository is
  self-complete. Users who would rather not build anything can download an
  official release, which ships the same components precompiled.
- **A published core contract.** `BizHawk.Emulation.Common` (+
  `BizHawk.Common`, `BizHawk.BizInvoke`) is the complete interface a core
  builds against, outside this repository. Building the solution produces the
  full core-author kit in `build/dll`, including the settings source
  generator. The package format is documented in [MINIHAWK.md](MINIHAWK.md),
  and the quickerNES package serves as the reference implementation.
- **Determinism as the gate.** Every change to this repository must pass the
  witness harness ([tests/](tests/README.md)): a regression
  suite replayed through the full frontend stack, byte-comparing emulator RAM
  against goldens, in both straight-replay and per-frame-savestate modes.
  Frame-exact reproducibility is the invariant that makes TAS work possible.
- **A reference core.** The first core package is
  [quickerNES](https://github.com/SergioMartin86/quickerNES), whose `minihawk/`
  directory contains the entire quickerNESâ†”miniHawk interface â€” this
  repository contains zero emulator-specific code.

The full engineering log â€” objectives, phase-by-phase procedure, and the sharp
edges found along the way â€” is in [MINIHAWK.md](MINIHAWK.md).

## Building

The canonical build is **Linux-hosted and meson-mediated**, and produces the
artifacts for *both* operating systems: the managed frontend is built once
(its IL is platform-neutral, running on .NET Framework on Windows and Mono on
Linux), and every native library is built twice â€” natively with gcc for
Linux, and cross-compiled with mingw-w64 (static gcc runtime) for Windows.
Clone with `--recursive`; there are no prebuilt binaries anywhere in the
repository.

```
meson setup build/meson-linux   --prefix "$(pwd)/build" --libdir dll
meson setup build/meson-windows --prefix "$(pwd)/build" --libdir dll --cross-file extern/meson/mingw-w64.ini
meson compile -C build/meson-linux            # Linux natives
meson compile -C build/meson-windows          # Windows natives (mingw cross)
meson install -C build/meson-linux
meson install -C build/meson-windows
meson compile -C build/meson-linux frontend   # the managed solution (dotnet)
```

Linux requirements: meson, ninja, cmake, gcc, mingw-w64, the .NET SDK, and
Rust via [rustup](https://rustup.rs). The .NET SDK must be **Microsoft's own
binary** (`curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0`):
distro-built SDKs — including what packages.microsoft.com serves for some
Ubuntu releases — omit the `Microsoft.NET.Sdk.WindowsDesktop` targets that
the net48/WinForms frontend needs.

On Windows, a plain `dotnet build source/BizHawk.sln -c Release` remains
self-sufficient as a development convenience (it builds the Windows natives
via Visual Studio's clang-cl/CMake/Ninja and rustup) â€” but the Linux meson
build is the one releases and CI use.

To run on Windows: `build\EmuHawk.exe`. Build a core package first (for
quickerNES, run `minihawk/build-package.ps1` from a sibling quickerNES
checkout â€” it installs `quickernes.zip` into `build\Cores\`), then
`File > Open Core...` (or `--core=<path>`) followed by the ROM.

## Contributing

Pull requests are welcome â€” from people, from people working with AI
assistants, and from AI agents alike. A contribution is judged on its merits:
it should build, pass the witness gate, and keep to the project's scope. The
one firm requirement is legal cleanliness â€” you must have the right to submit
the code under this repository's MIT license, and anything derived from or
generated out of other works must respect those works' licenses and carry the
attribution they require.

## Credits and license

This project stands entirely on **BizHawk**, created and maintained by the
BizHawk team ([TASEmulators/BizHawk](https://github.com/TASEmulators/BizHawk)) â€”
the frontend architecture, the TAS tools, the emulation service interfaces,
and years of accumulated correctness are theirs. miniHawk is a subtraction
from their work, not an addition to it.

miniHawk is provided under the MIT License, preserving the BizHawk team's
copyright â€” see [LICENSE](LICENSE), which also explains the licensing of the
native libraries built from `extern/`, the vendored test suite, and why core
packages carry their own licenses.

This repository's history begins at the point of separation from BizHawk. The
full pre-separation ancestry â€” including the BizHawk contributors' commit
history this work is built on â€” is preserved in
[BizHawk](https://github.com/TASEmulators/BizHawk) and in the
[transitional fork](https://github.com/SergioMartin86/BizHawk) this repository
was extracted from.
