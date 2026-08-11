# The Synth machine - specification (v1)

The synthetic witness console for miniHawk (see docs/design-principles.md,
"The synthetic witness"). The emulator is a stateful interpreter with the
capacity of printing to screen and emitting audio; ALL game logic, video
assets, and audio assets belong to the game, provided as a `.testrom` file.

Three implementations (native, pure C#, waterboxed) must produce, for the same
rom and input movie, bit-identical: (1) the full memory space, (2) every video
frame, (3) every audio sample. This document is the single source of truth all
three are written against; nothing outside it may influence emulation.

Everything below is exact-width integer arithmetic; there is no float anywhere.
All multi-byte values are little-endian, in roms and in serialized state.

## Machine model

- Frame rate: exactly 60 fps (frontend timing 60/1).
- Input: one pad, 8 buttons, bitmask (bit 0..7):
  Up=0x01 Down=0x02 Left=0x04 Right=0x08 A=0x10 B=0x20 Select=0x40 Start=0x80.
- Registers: R0..R7, int32. PC: byte offset into CODE, u32.
- RAM: 4096 bytes (0x000..0xFFF), all zero at power-on. This is the memory
  space the witness compares and the frontend exposes as the "RAM" domain.
- Framebuffer: 128 x 120 pixels, one byte per pixel, a palette index 0..15
  (upper 4 bits of a written value are masked off). Persists across frames.
  All zero (palette color 0) at power-on.
- Audio: one square-wave channel, 735 samples emitted per frame (44100 Hz /
  60 exactly), mono int16 (frontend duplicates to stereo). Channel state:
  phase accumulator u32, phase increment u32, volume u8 (0..255), plus jingle
  playback state (see PLAY). Sample generation, per sample:
    phase += increment
    output = vol == 0 ? 0 : (phase & 0x80000000) ? +(vol << 6) : -(vol << 6)
  TONE with freq 0 sets increment 0 AND vol 0, so silence always outputs 0
  exactly, and a nonzero volume always comes with a nonzero increment.
  increment = (freq * 4294967296) / 44100, computed in u64 then truncated to
  u32; freq is u16 Hz.
- Frame counter: u32, starts at 0, increments after each emulated frame.
- Lag flag (transient, NOT serialized state): whether an INPUT instruction
  executed during the last emulated frame. Frontends use it for lag counting;
  it has no effect on emulation.

## Per-frame execution

Each frame: the interpreter sets PC = entry point and executes instructions
until HALT or until 65536 instructions have executed (the instruction budget;
hitting it simply ends the frame - no fault state). Then 735 audio samples
are synthesized from the (possibly updated) channel state, and the frame
counter increments. The framebuffer is whatever the program has drawn so far
(it is NOT cleared between frames).

Jingle playback advances at the start of each frame, BEFORE code runs.
PLAY j (j taken mod jingleCount) makes jingle j active with note position 0
and a remaining-frames counter of 0; the channel is NOT touched at PLAY time
(the first note starts sounding on the next frame's advance). The per-frame
advance, when a jingle is active: if the remaining-frames counter is 0, the
next note is loaded - the channel is set from the note's freq and vol (TONE
semantics; a stored duration byte of 0 counts as 1) and the counter is set to
the duration - unless there is no next note, in which case the jingle
deactivates and the channel is silenced (as STOP). Then the counter
decrements, and when it reaches 0 the note position advances. The channel is
only ever written at these note boundaries: TONE/STOP executed by code
override the channel until the next boundary. PLAY replaces any active
jingle. A jingle with 0 notes deactivates on its first advance.

## Instruction set

Fixed 8-byte instructions: opcode u8, a u8, b u8, c u8, imm i32.
"Ra" = register index in field a (0..7; out-of-range indices wrap mod 8),
likewise Rb (field b) and Rc (field c). Addresses are taken mod 4096 for RAM
and mod (128*120) for the framebuffer; tile indices mod the tile count;
palette indices mod 16; jingle indices mod the jingle count. Every out-of-
range access is thus well-defined. Signed arithmetic wraps (two's complement).

| op   | mnemonic            | effect |
|------|---------------------|--------|
| 0x00 | HALT                | end this frame's execution |
| 0x01 | MOVI Ra, imm        | Ra = imm |
| 0x02 | MOV  Ra, Rb         | Ra = Rb |
| 0x03 | ADD  Ra, Rb, Rc     | Ra = Rb + Rc |
| 0x04 | SUB  Ra, Rb, Rc     | Ra = Rb - Rc |
| 0x05 | MUL  Ra, Rb, Rc     | Ra = Rb * Rc (low 32 bits) |
| 0x06 | DIV  Ra, Rb, Rc     | Ra = Rc==0 ? 0 : Rb / Rc (C truncation); INT_MIN/-1 = INT_MIN |
| 0x07 | AND  Ra, Rb, Rc     | Ra = Rb & Rc |
| 0x08 | OR   Ra, Rb, Rc     | Ra = Rb | Rc |
| 0x09 | XOR  Ra, Rb, Rc     | Ra = Rb ^ Rc |
| 0x0A | SHL  Ra, Rb, Rc     | Ra = Rb << (Rc & 31) |
| 0x0B | SHR  Ra, Rb, Rc     | Ra = (u32)Rb >> (Rc & 31) (logical) |
| 0x0C | ADDI Ra, Rb, imm    | Ra = Rb + imm |
| 0x10 | LDB  Ra, Rb         | Ra = RAM[(u32)Rb % 4096] (zero-extended byte) |
| 0x11 | STB  Ra, Rb         | RAM[(u32)Rb % 4096] = Ra & 0xFF |
| 0x12 | LDW  Ra, Rb         | Ra = little-endian i32 from RAM[(u32)Rb % 4093] (addr clamped so all 4 bytes fit: addr = (u32)Rb % 4093) |
| 0x13 | STW  Ra, Rb         | little-endian i32 Ra to RAM[(u32)Rb % 4093] |
| 0x14 | LDD  Ra, Rb         | Ra = DATA[(u32)Rb % dataSize] (zero-extended byte; 0 if no DATA section) |
| 0x20 | JMP  imm            | PC = (u32)imm % codeSize (aligned down to 8) |
| 0x21 | BEQ  Ra, Rb, imm    | if Ra == Rb: jump as JMP |
| 0x22 | BNE  Ra, Rb, imm    | if Ra != Rb: jump as JMP |
| 0x23 | BLT  Ra, Rb, imm    | if Ra <  Rb (signed): jump as JMP |
| 0x24 | BGE  Ra, Rb, imm    | if Ra >= Rb (signed): jump as JMP |
| 0x30 | INPUT Ra            | Ra = current pad bitmask (u8, zero-extended) |
| 0x31 | FRAME Ra            | Ra = frame counter (as i32) |
| 0x40 | CLEAR Ra            | fill framebuffer with palette index Ra & 15 |
| 0x41 | PIXEL Ra, Rb, Rc    | fb[((u32)Rb % 120) * 128 + (u32)Ra % 128] = Rc & 15 |
| 0x42 | RECT  Ra, Rb, Rc, imm | filled rect: x=(u32)Ra%128, y=(u32)Rb%120, size packed in imm (w = imm & 0xFF, h = (imm >> 8) & 0xFF, both clipped to the framebuffer), color Rc & 15 |
| 0x43 | TILE  Ra, Rb, Rc    | blit 8x8 tile index (u32)Rc % tileCount at x=(u32)Ra%128, y=(u32)Rb%120 (clipped); tile pixels are palette indices, index 0 is transparent |
| 0x50 | TONE  Ra, Rb        | freq = Ra & 0xFFFF, vol = Rb & 0xFF; set increment from freq (freq 0 also forces vol 0) |
| 0x51 | STOP                | vol = 0, increment = 0 (phase keeps its value) |
| 0x52 | PLAY  Ra            | start jingle (u32)Ra % jingleCount (see jingle playback above); no-op if no SND section |

Unknown opcodes are no-ops (skip to the next instruction). PC advances by 8
before the instruction executes (so JMP targets are absolute offsets and a
non-taken branch continues naturally). If PC reaches codeSize execution ends
for the frame (as HALT).

## The .testrom container

Little-endian throughout.

```
offset  size  field
0       8     magic "SYNTHROM"
8       2     format version = 1
10      2     reserved (0)
12      4     entry point (byte offset into CODE)
16      4     CODE offset   (from file start)
20      4     CODE size     (bytes; multiple of 8)
24      4     GFX  offset
28      4     GFX  size     (0 = no section)
32      4     SND  offset
36      4     SND  size     (0 = no section)
40      4     DATA offset
44      4     DATA size     (0 = no section)
48      ...   sections
```

- GFX section: 16 palette entries of 4 bytes each (R, G, B, 0xFF), then tiles:
  each tile is 64 bytes (8x8 pixels, one palette index byte per pixel,
  row-major). tileCount = (gfxSize - 64) / 64.
- SND section: jingle directory: u16 jingleCount, then per jingle: u16
  noteCount, then notes: each note is u16 freq, u8 vol, u8 frames (duration).
  Jingles are stored back-to-back in directory order.
- DATA section: arbitrary read-only bytes (level layouts etc.), reachable
  via LDD.

The rom is identified by its file SHA1 (the frontend's standard whole-file
hash; no header stripping - the Synth system honors the format-agnostic
RomGame pillar).

## Canonical state serialization

The complete machine state, in this exact order (total 20272 bytes):

```
R0..R7            8 x i32   (32 bytes)
frameCounter      u32       (4)
audio.phase       u32       (4)
audio.increment   u32       (4)
audio.volume      u8        (1)
jingle.active     u8        (0 or 1) (1)
jingle.index      u16       (2)
jingle.notePos    u16       (2)
jingle.noteFramesLeft u8    (1)
pad0              u8[3]     (3)  (always zero)
RAM               4096 bytes
framebuffer       15360 bytes (128*120)
```

PC is NOT serialized (it is dead between frames - execution always restarts
at the entry point). Loading a state must reproduce subsequent emulation
bit-exactly; the state above is the whole machine by construction.

## Video presentation

The frontend receives 128x120 as BGRA ints: for each pixel,
palette[fbByte & 15] with the GFX palette (or, with no GFX section, the
fixed fallback palette: entry 0 = black 0xFF000000, entries 1..15 =
0xFF000000 | (0x111111 * n)). The palette lookup happens at presentation
time only - the framebuffer of record is the palette-index bytes, and THAT
is what the witness compares (video goldens hash the index bytes, not BGRA).

## Audio presentation

The 735 mono int16 samples per frame are the audio of record (witness
compares/hashes these). The frontend duplicates them to stereo.

## Side-effect freedom

Per the design doc's rule for non-waterboxed cores: implementations of this
machine must be pure functions of (rom, input sequence, machine state). No
filesystem access in either direction (the rom arrives as bytes through the
frontend interface), no direct I/O, no syscalls, no clocks, no randomness,
no threads; nothing observable that a savestate save/load does not capture
and revert. The native reference implementation uses only memory allocation
(scoped to the core instance) and memory operations - keep the twins equally
pure.

## Test goals

Each test = (rom, movie, goal). Goals per the plan: win the game, lose the
game, produce a certain video output, produce a certain audio output. Win and
lose are game-defined conventions: by convention every rom writes a status
byte at RAM[0x000]: 0 = playing, 1 = won, 2 = lost.
