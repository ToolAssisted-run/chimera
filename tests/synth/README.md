# The synthetic witness

miniHawk's own, core-agnostic correctness apparatus (see "The synthetic
witness" in [design-principles.md](../../docs/design-principles.md)). Three
core flavors - native, pure C#, waterboxed - will implement the same tiny
console, and for every test movie ALL of the memory space, video output, and
audio output must match bit-exactly across the three.

Everything here is a conformance-test fixture for the published core contract:
nothing is part of BizHawk.sln, and nothing is quickerNES- or system-specific.

## Running

```
./run-witness.sh              # both levels, both modes (~10 s)
./run-witness.sh --level a    # native only (no EmuHawk/Xvfb needed, <1 s)
./run-witness.sh --record     # (re)record goldens
```

## Layout

- `SPEC.md` - the Synth machine specification (v1). The single source of
  truth; all three implementations are written against it alone.
- `native/synthcore.c` - the native reference implementation (flavor a).
- `asm.py` - assembler: `.sasm` text -> `.testrom`.
- `roms/` - the test games (game logic, video assets, and audio assets live
  in the roms; the emulator is a bare stateful interpreter).
- `package/` - the managed adapter + manifest for the native flavor;
  `build-package.sh` builds natives (Linux .so always, Windows .dll when
  mingw-w64 is present), assembles the roms, builds the adapter against
  `build/dll`, and installs `build/Cores/synth-native.zip`.

## Status

- [x] Machine spec v1
- [x] Native core (libsynthcore) + assembler + gridWalker rom
- [x] Native-flavor package; verified through EmuHawk on Mono end to end
      (rom routing via manifest extensions, factory creation, input pipeline,
      RAM domain, savestates, lag counting)
- [x] Witness driver (`run-witness.sh`): four goal movies (win / lose /
      video output / audio output), Level A native metrics incl. full
      per-frame video+audio stream hashes with a rerecord self-check, Level B
      through EmuHawk with RAM+VRAM byte-compare against the same goldens,
      both modes. ~10 s wall clock - THE miniHawk smoke test (supersedes the
      old quickerNES --quick subset).
- [ ] Additional test roms (dedicated video-goal and audio-goal roms)
- [ ] Pure-C# twin (flavor b) - blocked on the C# core integration story
- [ ] Waterboxed twin (flavor c) - blocked on the waterboxing machinery

## Provenance

All code, art, and sound here are original to this repository (the game logic
derives from the author's own JaffarPlus TestEmulator/GridWalker, MIT): the
tiles are elementary geometry, the palette is generic RGB values, and the
audio is square-wave frequency ramps - nothing is taken from any existing
game, emulator, or other copyrighted source.
