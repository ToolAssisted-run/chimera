# miniHawk witness harness

Correctness gate for the miniHawk project (see [MINIHAWK.md](../MINIHAWK.md)).
The quickerNES regression suite (`suite/*.test` + `.sol` input sequences â€” a
vendored snapshot of `tests/` from
[TASEmulators/quickerNES](https://github.com/TASEmulators/quickerNES), which
this repo no longer carries as a submodule) must pass at every phase boundary,
at two levels:

## Level A â€” native core guard

Runs the quickerNES native tester over every test and compares final-RAM
MetroHashes against `goldens/levelA-hashes.txt`. Validates that the native core
payload never drifts. Built and run under WSL:

```
# one-time setup (WSL): clone TASEmulators/quickerNES + ROMs, then
meson setup build -DenableArkanoidInputs=true   # + -Wno-unused-but-set-variable on GCC 13+
ninja -C build
```

`native/dumper.cpp` is a small companion tool (added to the same meson build)
that writes the raw 2KB low-mem instead of a hash â€” its output is the ground
truth that Level B dumps are byte-compared against (`goldens/native/`).

## Level B â€” full-stack witness (the real one)

`run-level-b.ps1` drives the actual EmuHawk build in `build/`: for each test it
writes a job file, launches EmuHawk with `--lua=replay.lua` on a **hidden
Windows desktop** (no window appears), replays the `.sol` through the frontend
input pipeline, and dumps the final 2KB `RAM` domain. Up to 8 EmuHawk instances
run concurrently, each with private config/job files. `hidden-run.ps1` is a
standalone helper for one-off hidden runs (diagnostics etc.).

Replay semantics that were required for byte-exact agreement with the native
tester (each found the hard way; see MINIHAWK.md "Phase 0 discoveries"):

- `client.reboot_core()` at script start â€” EmuHawk emulates one frame during
  ROM load before Lua gains control.
- Console Reset/Power flags in `.sol` files are parsed but **not** applied â€”
  the native tester ignores them during replay (solarJetman has a reset).
- `OpposingDirPolicy` must be `Allow` (2) in the config â€” Lua input passes
  through the SOCD filter, and the movies use simultaneous L+R/U+D.
- Arkanoid paddle input is delivered via `joypad.setfrommnemonicstr` â€”
  `joypad.setanalog` axis holds never reach the output controller in this
  build.

```
.\run-level-b.ps1                  # verify current build against goldens
.\run-level-b.ps1 -Record          # (re)record goldens
.\run-level-b.ps1 -Mode rerecord   # per-frame savestate save/load variant (IStatable)
.\run-level-b.ps1 -Filter super    # subset by regex
```

Goldens live in `goldens/levelB/`. A run passes when every dump is
byte-identical to its golden, and goldens must themselves match
`goldens/native/` (cross-validated whenever they are re-recorded).

ROMs are resolved from `suite/roms/` first, then
`C:\Users\sergiom\Documents\TAS\roms\nes`, and are SHA1-verified against each
`.test` before running.

## Witness set

31 tests total; exclusions and rationale are listed in MINIHAWK.md (mapper 5
disabled in the pinned fork, one pinned-core mapper-30 serializeState crash,
one wrong local dump, two initial-`.state` tests that are Level-A-only).
