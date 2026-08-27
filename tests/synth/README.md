# The synthetic witness

Chimera's own, core-agnostic correctness apparatus (see "The synthetic
witness" in [design-principles.md](../../docs/design-principles.md)). Three
core flavors - native, pure C#, waterboxed - will implement the same tiny
console, and for every test movie ALL of the memory space, video output, and
audio output must match bit-exactly across the three.

Everything here is a conformance-test fixture for the published core contract:
nothing is part of Chimera.sln, and nothing is quickerNES- or system-specific.

## Running

```
./run-witness.sh              # both levels, both modes (~10 s)
./run-witness.sh --level a    # native only (no Chimera/Xvfb needed, <1 s)
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
  `build/dll`, and installs `build/Cores/synth-native.chimeraCore`.

## Status

- [x] Machine spec v1
- [x] Native core (libsynthcore) + assembler + gridWalker rom
- [x] Native-flavor package; verified through Chimera on Mono end to end
      (rom routing via manifest extensions, factory creation, input pipeline,
      RAM domain, savestates, lag counting)
- [x] Witness driver (`run-witness.sh`): four goal movies (win / lose /
      video output / audio output), Level A native metrics incl. full
      per-frame video+audio stream hashes with a rerecord self-check, Level B
      through Chimera with RAM+VRAM byte-compare against the same goldens,
      both modes. ~20 s wall clock - THE Chimera smoke test (supersedes the
      old quickerNES --quick subset).
- [x] Pure-C# twin (flavor b): SynthMachine.cs, written from SPEC.md alone
      (no shared code with the native flavor), shipped as synth-sharp.chimeraCore -
      a manifest with an EMPTY natives list; nothing loaded at runtime. Its
      Level A tester (synth-run-sharp, under Mono) and its frontend package
      both verify against the NATIVE-recorded goldens: bit-identical RAM,
      video stream, and audio stream on every movie, first try - the
      contract needed no changes for a pure-C# core.
- [ ] Additional test roms (dedicated video-goal and audio-goal roms)
- [ ] Waterboxed twin (flavor c) - blocked on the waterboxing machinery

## Provenance

All code, art, and sound here are original to this repository (the game logic
derives from the author's own JaffarPlus TestEmulator/GridWalker, MIT): the
tiles are elementary geometry, the palette is generic RGB values, and the
audio is square-wave frequency ramps - nothing is taken from any existing
game, emulator, or other copyrighted source.
