# Chimera - Project Charter

A minimal, core-agnostic TAS frontend derived from BizHawk. Emulation cores are not
in the tree: each is a package, loaded by the engine into the miniBox sandbox when a
project opens.

## Objectives (testable)

1. **Zero core code in the repo.** Chimera builds and runs with no emulation core in
   it. Cores arrive as single-file packages (`.chimeraCore`, a zip: `core.wbx` +
   `waterbox.config` + data), found in a `cores/` folder.
2. **Published core contract.** What a package declares, and the guest ABI miniBox
   runs it on, are a versioned, published contract. A core package is built outside
   the Chimera repo against that contract alone.
3. **Determinism witness.** The synthetic witness (tests/synth) proves, at every
   change, that Chimera delivers inputs to a core faithfully, and each core's own
   gate proves its emulation against a native reference.
4. **TAS-only frontend.** Kept: TAStudio, movie record/playback, savestates, rewind,
   frame advance, virtual pads, RAM Watch/Search, Hex Editor, Cheats, the Lua console
   (core-agnostic APIs only), A/V dumping and screenshots, core-agnostic debug tools.
   Everything else - core-specific config dialogs and viewers, per-system Lua
   libraries - is left out.

## Pillar: reproducibility belongs to the core, not the frontend

Chimera is NOT the keeper or enforcer of reproducibility - it REQUIRES it from
cores. A movie's reproduction contract is the pair (movie, core package): if the
selected core package is exactly the same, the movie must reproduce regardless of
Chimera's version or the versions of any frontend library (SDL, Lua, zstd,
Qt, ...). Consequences: frontend native dependencies may be upgraded freely and are
never pinned for determinism's sake (only for build reproducibility); no frontend
feature may become something a movie depends on to sync; the witness harness proves
the frontend *delivers inputs faithfully* to the core, not that the frontend owns
determinism. (User-stated pillar, 2026-08-10.)

Package identity (same day): like roms, a core package is uniquely and solely
identified by the SHA1 of its file - that hash IS the package; name, version, git
commit, and platform variant are secondary metadata. Implemented: the loader hashes
the zip at load time (directory-form dev packages are explicitly unhashed), the
extraction cache is keyed by that SHA1, the hash is shown on load, recorded into
movie headers as CorePackageSHA1, and compared on playback with a warning on
mismatch - the exact analogue of the rom-hash check.

Elaboration (same day): the pillar bites hardest at the interface points where Chimera
hands data to cores. Audit of those points and their standing:
- ROM data: format-agnostic - the core receives the file's exact bytes, no
  header detection/stripping/per-system preprocessing.
- Archive extraction (zip -> rom bytes): bit-exact by the archive format's definition.
- IPS/BPS patching: a frontend transformation of rom bytes. Tolerable only because
  those formats are deterministic by spec; the implementation must stay spec-exact and
  must never grow heuristics.
- Movie input log: the mnemonic format is effectively FROZEN - its parsing is part of
  the reproduction contract and must be spec-stable across Chimera versions.
- Sync settings: opaque JSON round-tripped to the core's types; the frontend must
  never interpret or migrate them.
- Disc data [RESOLVED 2026-08-25]: the frontend's disc layer is REMOVED. It parsed
  CUE/CCD/etc and synthesized sector streams in the frontend, so a frontend version
  change could alter the byte stream a disc core sees - and it silently claimed
  extensions (.iso and friends) that core packages declare, feeding the factory a
  disc object no waterbox core reads. Now disc images are roms like any other file:
  the raw bytes go to whichever package declared the extension, and all format
  interpretation happens core-side, versioned with the core (the PSP package parses
  ISO/CSO/CHD in-guest). A future CD-based core package bundles its own disc layer;
  the frontend's only disc job, if one ever appears, is gathering the files of a
  multi-file set - never transforming bytes on the emulation path.
- Firmware: database contents removed; anything a core needs ships inside its package.

## Side-effect freedom

A core must contain NO side effects: no writing to the filesystem; no reading
directly from it (ALL data - roms, firmware, databases, assets - arrives through
Chimera); no direct I/O of any kind (with rendering-only channels such as OpenGL as
the one permissible exception); nothing that causes a change in the host system
that cannot be captured and reverted by a savestate save/load. The test is exactly
that: if any observable effect of running the core is not round-tripped by
save/load state, the core is in violation. The sandbox enforces it (see below).

## Agreed decisions

- **Core package interface:** a waterboxed guest (`core.wbx`) and its declarations
  (`waterbox.config`), run by the engine in miniBox. Not libretro.
- **Repo strategy:** a standalone repository derived from BizHawk, with a fresh
  history.
- **Load model:** a project names its core package by SHA1; the engine opens that
  package when the project opens.

## THE WATERBOX-ONLY REDESIGN (user-decided, 2026-08-11)

After full analysis of BizHawk's waterbox (docs/waterbox-analysis.md), a
fundamental design change: Chimera allows ONLY waterboxed cores, making
every core reproducible and portable BY FORCE rather than by discipline.
Decisions, all user-confirmed:

1. **Waterbox-only, no exceptions.** The package format knows only guest
   images. A core built for the host is no longer expressible as a package,
   and the side-effect rule needs no discipline - the sandbox IS the
   enforcement.
2. **The waterbox host lives in Chimera**, compiled and shipped with
   Chimera's OS-dependent artifacts (meson dual-target, like every other
   native). This amends the reproducibility pillar the same way the frozen
   movie-mnemonic format does: the WATERBOX MACHINE IS A FROZEN, VERSIONED
   SPECIFICATION - address layout, syscall semantics, the time constant,
   scheduling order, callback-slot mechanics, everything the guest can
   observe - and Chimera ships a spec-exact implementation. Movies record
   the machine-spec version; the reproduction contract becomes
   (movie + core package + machine-spec version), where implementations of
   a given spec version are interchangeable by definition. The Synth
   SPEC.md/twins exercise is the working proof that spec-first bit-exact
   interchangeability is achievable.
3. **One generic adapter, no per-core frontend code.** A core package is a
   single platform-neutral zip: manifest + core.wbx + data files
   (controller definitions, settings schemas, system ids, extensions -
   all DATA, interpreted by Chimera's one universal adapter over a
   standardized guest export ABI). Identical zip and identical package
   SHA1 on every OS; movies therefore carry the same CorePackageSHA1
   cross-platform. (BizHawk's Nyma layer - one data-driven adapter serving
   every mednafen core - is the scale precedent.)
4. **Everything waterbox lives in ONE external repository: miniBox**
   (github.com/ToolAssisted-run/chimera-common-minibox), consumed as a submodule at
   extern/chimera-common-minibox like every other from-source dependency. It carries all
   sides: the runtime (sandbox host - Chimera's meson builds it into the
   frontend's OS-dependent artifacts), the guest toolchain (the core-author
   kit: musl fork, emulibc, libco, libcxx sysroot, linkscript, common.mak),
   and the waterbox's own conformance tests -
   independently testable without Chimera. Plan of record for the runtime:
   a C/C++ port replacing the imported Rust reference (drops the
   nightly-Rust requirement; validated differentially against it).

Migration order (each step gated by the synthetic witness, plus the
quickerNES suite until its core is migrated):
  (i) import + de-nightly the waterbox host runtime; meson dual-target;
      a Windows adapter for the guest's SysV calling convention;
  (ii) define guest ABI v1 + package format v2 (data-driven controller/
      settings declarations); implement the generic adapter;
  (iii) stand up the toolchain repo; build synth.wbx from synthcore.c; the
      waterboxed synth becomes the SHIPPED synth package, while the native
      implementation remains as the external ground-truth tester (the
      Level A golden generator - an even cleaner witness split);
  (iv) migrate quickerNES to a .wbx guest; re-gate the full suite;
  (v) retire the native loading paths (manifest v1, natives lists) from
      Chimera.

## The synthetic witness (planned successor to the quickerNES gate)

Plan (user-stated, 2026-08-11):

1. **Two synthetic cores** - a native reference and a waterboxed one - with
   exactly the same emulation logic, video output, and sound output.
2. **A synthetic game with non-trivial rules** and an input set.
3. **The test plan**: for a series of input movies, ALL of the memory space, the
   video output, and the sound output must match across both cores.
4. **Each test achieves a different goal**: win the game, lose the game, produce
   a certain video output, produce a certain audio output.

Architecture (user-stated): the emulator is a **stateful interpreter** with the
capacity of printing to screen and emitting audio - nothing more. ALL game
logic, video assets, and audio assets belong to the game, which is provided as
a **`.testrom` file**; there are multiple test roms. (Consequence: the
interpreter is a miniature console, the roms are its games, and the rom-routing
path of the frontend - manifest `extensions` mapping `.testrom` to the
synthetic system - is exercised exactly like a real system's.)

Derivation: the emulator/game logic derives from the JaffarPlus test emulator
and its GridWalker test game (a cursor on a bounded W x H grid with clamped
U/D/L/R moves; goal-cell rules; 2-byte core state), extended with video and
audio output, and with the logic moved out of the emulator into the rom
program per the interpreter architecture.

Sequencing: the machinery for waterboxing was not yet defined, so work
STARTED WITH THE NATIVE CORE ONLY; the waterboxed twin followed. Until the
synthetic witness is complete enough to be the gate, the quickerNES witness
(now in the quickerNES repository) remains the commit gate.

Status (2026-08-11): the native flavor is fully implemented and passing -
SPEC.md v1; libsynthcore (the reference interpreter); the .sasm assembler;
the gridWalker rom (walls/hazards/goal/step budget, per-move beeps, win/lose
jingles); a native package routing .testrom; and run-witness.sh, the two-level driver: Level A replays
the four goal movies (win / lose / video output / audio output) natively and
checks final RAM plus FULL per-frame video and audio stream hashes against
goldens, with a serialize-every-frame rerecord self-check; Level B replays
the same movies through Chimera and byte-compares the final
RAM and VRAM domains against the same goldens, both modes. ~10 seconds wall
clock, and it is THE Chimera smoke test (user decision, same day): the
quickerNES --quick smoke subset is retired; quickerNES's suite is the full
determinism gate only. Audio is Level A-verified only for now (the frontend
has no scriptable audio tap - an acceptable gap while the frontend audio
path stays core-agnostic).

The waterboxed core LANDED (2026-08-11): synth.wbx is the SAME synthcore.c
compiled for the miniBox waterbox sandbox (see the miniBox repository at
extern/chimera-common-minibox - a from-scratch C/C++ port of BizHawk's waterbox host,
GCC-built guest toolchain, machine spec, all meson) and wrapped in the
waterbox ABI (tests/synth/package-box/). Its whole machine state lives in
guest memory, so the waterbox host savestates it AUTOMATICALLY - no
explicit serialize/deserialize, the defining property of a waterboxed core. The
Level A tester runs it through the miniBox host and reproduces the
native-recorded goldens byte-exactly on all four movies, in both plain
mode AND whole-machine-rerecord mode (the waterbox host round-tripping the
entire guest around every frame). run-witness.sh checks both at Level A:
two implementations (native and waterboxed), one frozen spec, bit-identical
memory + video + audio. This also validates
the waterbox-only redesign's foundation - the miniBox host is real,
GCC-buildable (no clang/nightly-Rust), and reproduces emulation exactly.

Placement note: the synthetic cores are conformance-test fixtures for the
published contract, not product cores - they live under `tests/` and are built
by the test harness against `build/dll`, never as part of Chimera. The
"zero core code in the repo" objective refers to the product: Chimera builds
and ships with no core in it; a test fixture proving the contract
works is test machinery, same as the witness drivers.

## Witness harness (two levels, both must pass at every phase boundary)

NOTE (2026-08-11): this section describes the quickerNES witness, which -
together with the vendored suite, goldens, and drivers - now lives in the
quickerNES repository (`chimera/tests/`), per the rule that absolutely
nothing quickerNES-specific remains in this repository. It is still the
commit gate (run it from there, `--chimera-root` pointing here) until the
synthetic witness above replaces it. The section is kept as the harness's
design record.

The quickerNES test format: each `.test` is JSON naming a ROM (+ expected SHA1, optional
initial `.state`, controller types) and a `.sol` input sequence (one line per frame,
jaffar format, e.g. `|..|........|`). The native tester (`quicknes/core/source/tester.cpp`)
replays the sequence and emits a MetroHash of NES low RAM (2KB) as the verdict; cycle
types `Rerecord`/`Full` additionally do a savestate save+load around every frame, which
is exactly the TAS-critical property.

- **Level A - core payload guard.** Build and run the native quickerNES tester over all
  34 tests. Validates that the native DLL we package never drifts. Runs everything,
  including the two tests with initial `.state` files.
- **Level B - full-stack witness (the real one).** Drive Chimera itself: load ROM, feed
  the `.sol` inputs through the frontend input pipeline (Lua harness or generated `.bk2`
  movies), dump final 2KB RAM domain, byte-compare against golden dumps recorded from
  unmodified BizHawk in Phase 0. Also run a per-frame savestate save/load variant
  (mirroring `Rerecord` cycle type) to exercise the frontend statable path.
  `chimerainterface.cpp` already supports the Arkanoid paddle types the arkanoid tests need.

Level B sharp edges, to resolve in Phase 0: input-string -> BizHawk controller mapping
must be validated button-by-button; the two initial-`.state` tests (microMachines,
saiyuukiWorld.lastHalf) use quickerNES-native state format and may remain Level-A-only;
BizHawk power-on state must be confirmed identical to the bare core's.

Witness-set exclusions found in Phase 0 (28 of 31 at Level A; 26 at Level B):
- `castlevania3.playaround`: mapper 5 (MMC5) is deliberately disabled in the
  BizHawk-pinned TASEmulators/quickerNES fork (poor QuickNES MMC5 support) - excluded.
- `novaTheSquirrel.anyPercent`: pinned core segfaults in `Core::serializeState` on
  mapper 30 (UNROM 512) before emulation starts. Pre-existing fork bug, likely affects
  stock BizHawk too - excluded pending separate investigation.
- `arkanoid2.arkFamicomController`: local `Arkanoid II (Japan).nes` dump SHA1 does not
  match the test's expected dump - excluded until the matching dump is available.
- `rcProAmII.race1` / `superOffroad.anyPercent`: upstream's quickNES-vs-quickerNES
  comparison fails, but quickerNES itself runs and hashes fine - IN scope (Chimera
  Level A compares quickerNES hashes against stored goldens, not core-vs-core).
- Note: the native tester build needs `-Wno-unused-but-set-variable` appended to
  `commonCompileArgs` under GCC 13+ (applied to the WSL build copy only, not the
  submodule).

Phase 0 discoveries about frame alignment (validated by per-frame RAM comparison):
- Chimera emulates exactly ONE frame during ROM load, before a `--lua` script's
  first line executes. A naive Lua replay is therefore one frame late relative to a
  power-on input sequence. `replay.lua` compensates with `client.reboot_core()` at
  script start (verified `startframe=0` afterward). Remember this when Chimera later
  aligns `.bk2` movies with tester `.sol` sequences.
- quickerNES `emulate_skip_frame` (rendering disabled, used by the native tester) was
  verified state-equivalent to `emulate_frame` - rendering on/off does not affect RAM.
- Chimera power-on RAM state is byte-identical to the bare core's (no adapter-side
  initialization differences).
- Lua `joypad.set` input passes through the SOCD (opposing-directions) filter
  (`UdlrControllerAdapter`), whose default `Priority` policy silently rewrites
  simultaneous L+R / U+D - which the TAS movies use heavily. The harness config sets
  `OpposingDirPolicy: 2` (Allow). Note for Chimera: `.bk2` movie *playback* bypasses
  this filter (it taps the chain after the SOCD adapter); only Lua/user input is
  affected.
- The native tester parses but IGNORES console Reset/Power flags during replay
  (`input.reset` is dead in `advanceState`); the harness mirrors that.
- Lua `joypad.setanalog` axis sticky-holds never reach the output controller in this
  BizHawk build (axes die between `StickyHoldController` and the final controller);
  axes must be delivered via `joypad.setfrommnemonicstr`, which routes through
  `ButtonOverrideAdapter` -> `Controller.Overrides()`. Worth revisiting when Chimera
  owns the input pipeline.
- Phase 0 witness status: 26/26 Level B tests PASS byte-identical to native ground
  truth in BOTH modes (simple replay AND per-frame savestate rerecord); Level A
  goldens recorded for 28/28.
- Core finding for upstream quickerNES: a full-state savestate round-trip is NOT
  lossless for `gimmick` (Sunsoft FME-7) and `superOffroad` - the native core itself
  perturbs state on deserialize+advance (Chimera mirrors it byte-exactly, so the
  frontend is faithful; the incompleteness is in core serialization). Deterministic,
  so the witness remains sound, but worth an upstream look.
- Core finding for upstream: pinned fork segfaults in `Core::serializeState` on
  mapper 30 (novaTheSquirrel) - see witness-set exclusions.

## The waterbox guest ABI, as built (2026-08-12)

The contract a core package implements. Everything below is the *whole* surface:
a package is `core.wbx` + `waterbox.config`, and the engine drives it.

**Required exports.** `Init()` (reads the mounted rom and the mounted
`settings` JSON, returns 1 on success), `FrameAdvance(uint64 buttons)`, and the
video/audio getters named by `waterbox.config`. Button *i* of the config's
`input.buttons` is bit *i* of the mask - 64 buttons, which is what a four-player
console with peripherals actually needs.

**Runtime self-description.** Memory domains are queried after `Init`
(`GetMemoryDomain{Count,Name,Ptr,Size,Writable}`), never declared statically:
their count and size depend on the cartridge and on user settings.

**Turbo.** `SetRenderingEnabled(int on)`, optional: while off the core produces
no picture and is otherwise exactly the machine it would have been. See "Turbo:
a frame nobody looks at does not have to be drawn" below.

**Analog controls.** `waterbox.config`'s `input.axes` declares them; the adapter
pushes each value with `SetAxis(index, value)` immediately before the frame it
belongs to. A package that declares axes must export it.

**Optional tooling groups** (surfaces, registers, buses, trace) - the core
renders and formats its own tooling, so the frontend needs no system knowledge.
A missing export is reported by the host as address 0, and the tool is greyed
out.

**Settings.** `waterbox.config`'s `settings` block holds package defaults; the
adapter merges the user's sync settings over them and mounts the result as JSON
for the guest to read during `Init`. They shape the machine, so changing one
reboots the core.

## Core packages arrive by themselves (2026-08-13)

Objective 1 said packages are "discovered from a `Cores/` directory"; until now
they were not - a package had to be named on the commandline (`--core`) or picked
from File > Open Core on every launch. Discovery closes that: at startup, the
cores folder beside the program is scanned, and every readable package found is
loaded.

**Discovery reads, it does not load.** It opens a zip (or directory), reads
`waterbox.config` or `chimera-core.json` for a name, systems and extensions,
and hashes the file - nothing more. That matters because loading is irreversible
in-process: it pins native modules. The
frontend has to be able to *describe* a package it will never load - a broken
one, a duplicate - and it can only do that if describing is cheap and separate.

**No enable/disable switch, deliberately.** The first cut of the window had a
checkbox per package. It was removed the same day, on the user's challenge, and
the reasoning is worth keeping: loading a package costs a JSON parse (the
`core.wbx` is untouched until a rom loads), arbitration between two cores for one
system is what Preferred Cores is for, and a package that fails to load is caught
and reported rather than fatal. So the switch bought nothing - while costing a
config list, an enable/disable API, and two extra states whose only job was to
explain that unticking a loaded core cannot unload it. The way to not load a core
is to not put it in `Cores/`. The window is a report, not a control panel.

## Core settings are declared, not coded (2026-08-13)

`waterbox.config`'s `settings` was a bag of defaults with no types and no
documentation; the settings dialog rendered it as an uneditable dictionary. It is
now a list of DECLARATIONS - name, display name, description, type, default,
options or range, and whether the setting is sync.

**The declaration is the UI.** There is no per-core settings dialog to write, and
never can be: one adapter serves every package. So the settings form is built
from the declarations - names, types, descriptions, defaults and dropdown
values. Every word in that form came from the core. Adding a setting to a core is a `waterbox.config` edit and nothing else.

**Sync vs non-sync is the frontend's question, not the core's.** The guest gets
one flat settings object and reads the keys it knows. The `sync` flag decides
what the FRONTEND does: sync settings are recorded in movie headers and reboot
the core when changed, because they shape the machine.

**Non-sync settings must actually apply.** A "non-sync" setting that only took
effect at Init would be a sync setting wearing a disguise, so there is a fifth
optional guest ABI group: `GetSettingsCapacity` / `GetSettingsBuffer` /
`PutSettings(len)`. The host writes fresh JSON into the guest's own buffer and
calls it; `PutSettings` then returns `None` instead of `RebootCore`. A core
without the group gets a reboot, which is heavier but honest - the setting still
applies. The buffer must live in `ECL_INVISIBLE` memory: settings are not machine
state, and a savestate that captured them would restore an old value and make two
identical runs diverge.

**Storage did not change.** Values are still a flat name -> value map in the
config file and movie headers, so movies and configs written before this still
load.

## A second core for a system (2026-08-13)

QuickerNesHawk - a C++ transliteration of BizHawk's NesHawk - joined quickerNES
as a NES core package, and being the second package to claim a system turned up
three gaps that one core per system had hidden.

**Nothing could choose between them.** `--core` LOADS a package; it does not say
which core a rom opens with. That was decided by `Config.PreferredCores`, which
nothing in the UI ever wrote, so a `.nes` file opened with whichever package
happened to register first - alphabetical accident. The Emulator menu now has a
`Core` submenu listing every core registered for the running system, checked on
the current one; picking another records the preference and reboots. With one
core it is a single checked entry that answers "what am I running", which the
status bar already said - but the menu is where a user goes to change it.

**A script (or a gate) could not ask what it was talking to.** `emu.getsystemid()`
answers "NES", which is now ambiguous. `emu.getcorename()` answers the question
that matters. This is not a nicety: the first version of QuickerNesHawk's
frontend gate silently measured *quickerNES* and passed, because both cores are
accurate enough to agree on the test rom's RAM for 300 frames. A gate that cannot
name its subject is not a gate.

**A package may know its own frame rate.** NesHawk's region (NTSC/PAL/Dendy) is a
user setting, and it changes both the frame rate and the samples per frame -
neither of which a static `waterbox.config` can express. Two more optional guest
exports resolve after `Init` and win over the config when present:
`GetVsyncNumerator`/`GetVsyncDenominator`, and `GetAudioSampleCount`, which is
also what a blip-style resampler needs (it does not produce the same count every
frame). When a core exports the count, `audio.samplesPerFrame` becomes the buffer
CAPACITY rather than the per-frame number.

**A package says what rate it mixes at** (2026-09-04). The frontend's sound path
is built around 44100 Hz and the lineage's cores all produced it, so nothing ever
asked. A PS2's SPU2, a GameCube's DSP and an Xbox's APU mix at 48 kHz, and played
as 44.1 they came out a semitone and a half low with their audio outrunning the
video of every encode (issue #37). `audio.rate` declares the rate (44100 when
absent) and the adapter resamples to 44100 once per frame, on the way to the
sound output and the encoder alike. Presentation only: the guest's bytes, which
the gates hash, are what they were.

The wrinkle noted in the ABI section is now visible rather than theoretical:
sync settings are keyed by adapter *type*, so both NES packages share one config
key and one settings blob. Whichever core is loaded reads the keys it knows and
ignores the rest, so nothing breaks today, but two cores with a same-named
setting of different meaning would collide. Keying by package SHA1 (the identity
the reproduction contract already uses) is the fix when it matters.

## Key bindings arrive with the core (2026-08-13)

BizHawk ships one `defctrl.json` listing the default bindings for every console it
knows about. That only works while the frontend knows every console; Chimera's
cores arrive from outside it, so a monolithic file here would either list consoles
the frontend has no business knowing about, or be empty. It was empty: the
mechanism to read package bindings existed, but the last packages that shipped any
lost their files in the waterbox-only redesign, so a fresh install had no bindings
for any core and no way to get them except binding everything by hand.

**A package that declares a controller declares how it is played.** Each package
ships `default_keybinds.json` beside its `waterbox.config` - the same shape as
BizHawk's file, scoped to the controllers that package declares - and the values
are transcribed from BizHawk's own defaults, so someone arriving from BizHawk
finds the keys where they left them. The frontend ships none of its own.

**And has nowhere to put any.** The exe-side `defctrl.json` went with it:
`Config.ControlDefaultPath`, the seeding of a fresh config from that file, and the
controller dialog's "Save Defaults" item are all gone. Keeping a second source of
defaults would have meant two answers to "what are the defaults for this pad", one
of them a file the frontend can no longer fill in for a core it has never seen.
Your own bindings live in your config, where they always did; the defaults live
with the core that knows what the controller is.

**And "Preferred Cores" went with it** (user, same day). Config > Preferred Cores
had been a dead menu item since the CoreInventory removal - no handler, no
contents, kept only as the anchor the Core Settings submenu is inserted after. I
populated it; the user's call was to delete it instead, and the reasoning
generalises past this menu: a per-system matrix of cores to prefer is a shape that
only makes sense when the frontend ships every core it can name. Here a package
either is installed or is not, and the question is never "which of the cores you
bundle" but "this rom opens with which of the ones I installed".

So the only thing left is that answer, renamed to what it is:
`Config.DefaultCores` - the core a system's roms open with, written only when
someone picks one in Emulator > Core, never pre-populated, and absent entirely for
a system with one core. `RomLoader` reads it and otherwise takes whichever core
registered first.

They are DEFAULTS in the strict sense: they fill in for a controller the user's
config has never seen, and the controller config's
Defaults button falls back to them. A user's own bindings always win, and the
first package to claim a controller name keeps it - so installing a second NES
core cannot rebind the pad someone is already playing with.

**A broken bindings file costs the bindings, not the core.** Chimera
reports and ignores unreadable JSON: an optional convenience file must never stop
a core from loading.

Gated at both ends: the synthetic witness starts Chimera from a config that has
never heard of the synth controller and requires the package's bindings to be in
the config it writes out (`K:box:keybinds`), and each core repo's frontend gate
does the same for its own package.

## The menus say what they mean (2026-08-13)

- **Emulation -> System.** The menu holds Pause, Reboot Core, the dump status and
  what is loaded: that is the machine, not "emulation" as an activity.
- **Soft Reset and Hard Reset are gone**, menu items and hotkeys both. They only
  ever clicked a controller button named "Reset" or "Power" - buttons a package
  declares like any other, and bindable in the controller config. A frontend
  command that presses a button on your behalf is a leftover from when the
  frontend knew what a console had.
- **Emulator > Core is gone** (user, and rightly - I should not have built it).
  A core is not a setting to pick from a list, it is the machine; you choose it by
  opening its package. So File > Open Core now DOES choose: every system the
  package claims will open its roms with the cores it brought. Startup discovery
  deliberately does not, so what is sitting in `Cores/` can never silently
  reassign what you opened.
- **Tools lost Debugger, Trace Logger and Surface Viewer**: they are core-provided,
  and the Emulator menu is where the running core's tools belong. Listing them
  twice implied they were the frontend's.
- **Virtual Pad is gone entirely.** Its layouts were per console (BizHawk shipped
  one for each); a package declares none, so the window could only ever be
  empty. With it went the eight "Analog" hotkeys that nudged values inside it.

## Firmware is declared by the core, not known by the frontend (2026-08-13)

The frontend deleted its firmware manager along with the rest of the bundle
furniture: a table of every BIOS for every console, in a program that does not
know what cores exist. But some machines genuinely need a file the core cannot
ship - the Famicom Disk System's RAM adapter boots from an 8 KiB rom, and no disk
image will run without it - so the channel came back the other way up, and the
user decided its shape.

- **The package declares what it wants.** `waterbox.config` gains a `firmware`
  list: an id, a display name, a sentence about what breaks without it, the exact
  size, and the SHA1s of the dumps known to be right. Nothing about this lives in
  the frontend, which cannot name a single firmware file on its own.
- **The user provides it once, and it is remembered** under `<core name>/<id>` -
  keyed by core rather than by package hash, so rebuilding a package does not make
  you find your BIOS again.
- **`Emulator > Firmware`**, greyed out when no loaded package expects anything.
  It is the one item in that menu reachable with nothing loaded, because a rom
  that needs a BIOS cannot be loaded until the BIOS is there; opening it after the
  fact would be a door that only unlocks from the inside.
- **A wrong file is refused, an unknown one is not.** Wrong size means the wrong
  file and the core never sees it. A right-sized file whose hash is not on the
  list is used anyway and flagged: a good dump the declaration has never seen is
  likelier than a frontend that should refuse to run.
- **Missing means a sentence, not a stack trace.** `MissingFirmwareException` is
  its own type for exactly this: it is the one load failure the user can fix, and
  the message names the window that fixes it.
- **Config > Firmware surveys every installed core** (2026-09-05, issue #30). Until
  it, firmware was only ever asked about from inside: the Emulator menu's window
  needs a loaded package, the wizard asks per project, and a downloaded project
  that wanted a bios could only say "put it in the Firmware folder". The survey
  reads each package's `waterbox.config` in the Cores folder when the window
  opens, lists one row per declaration entry grouped by core - PCSX2's
  seventy-three bios releases are seventy-three rows, collapsed to the ones on
  hand - and answers each from the Firmware folder and from every path a person
  ever chose, by hash. Nothing is kept: the rows go with the window and a
  deleted package is gone on the next open (the frontend keeps no list of
  firmware, and cannot). What a person chose for it stays in config.ini, keyed
  by the core's name, for the day the package is put back. A chosen file is
  remembered where it lives, under `<core>/<id>#<sha1>` so several releases of
  one id can all be known, and never copied - firmware can be a PlayStation 3
  update or an Xbox disk image, and a second copy is a second thing to drift.
  The wizard and Open Project index every remembered dump of the core, so a
  file located in the survey satisfies them wherever it sits.
- **Delivery is a mount.** The file is handed to the guest as another mounted
  file, under the declared id, next to the rom and the settings - so the guest
  reads it with the same call it reads the rom with, and nothing in the ABI is
  special-cased for firmware.

What is deliberately NOT here: any suggestion that the frontend knows what a
firmware file means. It checks size and hash because the declaration told it what
to check, and hands over bytes. Whether they are the right bytes for the machine
is the core's business.

## Storing progress: cleared to the ground (user-decided, 2026-08-25)

Two designs for "what a machine keeps" have now been removed, and the ground is
deliberately bare while a third is designed.

The first was BizHawk's: a SaveRAM file per rom, an autosave timer, a per-system
save directory, and `StartsFromSaveRam` putting a copy of the save inside the
movie. It was replaced on 2026-08-14 by a core-declared persistent-data channel
(guest `GetPersistent*`/`PutPersistent`) plus `.gameBundle`
catalogues that named a rom and its attachments, with a movie citing the bundle
it was recorded against.

That second design is now gone too, at the user's decision: the frontend must not
be the thing that decides when progress reaches the disc. Removed with it: the
persistent-data channel and its guest ABI group, bundles (the format, the engine's
`ce_bundle_*` ABI, the loader path, the compose/write-back UI), the automatic
write-back on rom close, the `Bundle`/`BundleID` movie header keys, and the gates
that witnessed them. The cores lost their `GetPersistent*` exports and the test
runners their `--saveram-in`/`--saveram-out` flags.

What holds until the replacement lands: **a rom boots clean, every time, and
nothing is written beside it.** A machine's progress lives only in savestates,
which are explicit and belong to the user. Nothing in the frontend knows what an
SRAM is - and this time nothing knows what a save is at all.

## Save data: the core keeps it, the user takes it out (user-decided, 2026-08-25)

The third design, specified in full in docs/save-data.md. The short form: a
core with save data keeps it INSIDE the guest machine (in-guest storage is
savestate- and rewind-correct by construction, and the sealed baseline plus
page-dirty tracking makes even a 2 GB disk image cost only its dirtied pages
per state - the proven BizHawk/DOSBox recipe). Getting progress OUT is one
user action: `Emulator > Export Save Data...`, enabled whenever the core
exports the savedata group - the sixth optional guest ABI group
(`GetSaveDataFileCount/Name/Size/Buffer`) - with no change detection and no
automation; the frontend writes the core's own `(path, bytes)` enumeration as
a zip without interpreting a byte. Getting it back IN is a game input: the
exported files return through a multi-file game descriptor (a future design
the user owns), mounted hash-bound and cited by the movie header like any rom
or firmware. Cores whose save data is plain machine memory (NES SRAM) export
nothing and need nothing. First tenant: PPSSPP's RAM memstick.

## Wide input: a controller is as wide as it declares (2026-08-25)

The packed uint64 button mask was an accident of the first cores, not a
design: a DOS machine's keyboard is 101 keys before its mouse and joysticks
exist. The movie format never had the limit - an entry carries one mnemonic
column per declared button, any count - so only the transport was narrow, and
the fix is additive:

- The ENGINE keeps button state as a byte per declared button. The entry
  layer (movie_entry) parses and generates wide; `1ull << index` is gone.
- `ce_session_set_button(index, pressed)` mirrors set_axis: per frame, before
  the advance, values persist until changed. The effective state at an
  advance is the packed mask's low 64 OR'd with these - either path alone is
  exact, and record mode writes the effective state, exactly what the
  machine received.
- The GUEST side stays core-agnostic: a wide core exports
  `SetButton(index, state)` next to SetAxis, and the session delivers only
  CHANGES across the boundary. A config declaring more than 64 buttons
  refuses to open without the export - a controller that silently drops keys
  is worse than one that refuses to load. Loadstate (and greenzone seeks)
  reset the delta tracker to resend everything, because the guest's input
  latches are guest memory and the restore just rewrote them - the same
  lesson the trace flag taught.
- The FRONTEND packs the low 64 exactly as before for narrow cores; a wide
  core's adapter drives every button through set_button and passes mask 0.

First tenant: chimera-core-dosbox-x. Witnessed by the widened
test_movie_entry (a 101-button round trip across the 64 boundary) and by
every existing gate staying green - the packed path's behaviour is pinned
unchanged.

## Opening a core is something you do (2026-08-14)

Packages sitting in `Cores/` used to load themselves at startup. That made the
folder the decision-maker, and it showed: with nothing open at all, the Emulator
menu already had a Firmware item in it, because the descriptors were already
loaded.

- **`File > Open Core...` is the package list** (was "Core Packages..."), with an
  Open button. Discovery scans the search directories and LISTS what it finds;
  opening one is what loads it. Rows read *available*, *loaded* or an error - a
  package that will not parse still appears, because "why is my core not here"
  needs an answer that is not the console.
- **The Emulator menu is empty until a core is loaded**, and disabled when empty.
  Everything in it comes from a core: with a package open it holds that package's
  Firmware (declared by the descriptor, so it is known before any rom); with a
  machine running it also holds Settings, the core's tools, and what the machine
  keeps.
- **The command line still runs a rom in one step.** Naming a rom is an
  instruction to run it, so if no `--core` was given and nothing is open, the
  available packages are loaded to route it. Nothing like that happens in the
  GUI, where the point is that the choice is yours and visible.

## A refusal is a sentence, not a stack trace (2026-08-14)

Opening an FDS image with no BIOS produced a dialog with an exception and a
stack trace in it. Everything needed to say something useful existed - the core
knew precisely what was wrong - but its words went to stdout and died there,
because the only thing crossing the sandbox was "Init returned 0".

- **The core explains itself.** Optional guest export `GetLoadError`: after a
  failed Init the host reads the reason and shows it. The frontend cannot write
  that sentence - it does not know what a disk image is - so it repeats the
  core's.
- **`CoreLoadException`** is the type for a failure the user can act on
  (`MissingFirmwareException` now derives from it). RomLoader shows those as a
  plain message. Anything else still gets its stack trace: that is a bug report,
  not a configuration problem.
- **The same rule for the frontend's own refusals**: "no loaded core can run a
  NES game" now names the window that fixes it instead of arriving as an
  InvalidOperationException.
- **The command line is strict too** (user): naming a rom with no core open and
  no `--core` says so and stops. Choosing a core is never implicit, on either
  side of the GUI.

## The firmware window shows both hashes (2026-08-14)

What a person does in that window is compare what the core expects with what
they have, so the window does that comparison for them and shows its working:

- a **mark per row** - green tick (matches a dump the core names), amber warning
  (right size, hash the core has never seen; used anyway), red cross (wrong size
  or unreadable; refused), empty circle (nothing provided yet);
- **Expected SHA1** and **Actual SHA1** columns, first eight characters - enough
  to see a difference at a glance;
- the **full hashes** for the selected row underneath, because eight characters
  are enough to spot a mismatch but not enough to trust a match.

The hash of a provided file is computed even when the file will be refused: "you
gave me a 4KB file" is much less useful than showing what it actually is.

## What a package IS: reproducible bytes, versioned by a commit (2026-08-14)

A package's SHA1 was the core's whole identity, and it changed on every rebuild -
the zip stores mtimes, so the same sources produced a different "machine" every
time. A movie recorded on Tuesday warned against Wednesday's rebuild of the same
commit. Two changes, and they need each other.

**Deterministic packaging.** Fixed timestamp (1980-01-01), fixed permissions,
sorted entries, pinned compression level. The guest ELF was already reproducible;
only the container was not. `build-package.sh` packs a second time and compares
before it will publish, because this is exactly the kind of promise that rots
without anyone noticing.

**The commit is the version** (user-decided). The automated build that publishes
an artifact passes `CORE_VERSION=<commit>`, which is stamped into the packaged
`waterbox.config` - the repo's copy carries no version, since a file under
version control has no business holding a number that changes with every commit.
A package built by hand stamps `<commit>+local` (or `-dirty`), so it can never be
mistaken for a published one, and CI publishes the zip only from a job whose
gates passed.

**Both, in a movie.** `CoreVersion` is the commit - meaningful, lookup-able, what
a person acts on. `CorePackageSHA1` is the exact bytes - which BUILD of that
commit ran. When the version matches but the hash does not, the frontend says so
in those words ("same core version, but a different build of it"), because that
is the ordinary case of someone building the core themselves.

**Identity is relative to a toolchain**, and that is honest rather than
unfortunate: the same sources through a different gcc are different code and
could in principle emulate differently. Rather than pin a vendored toolchain, the
package SAYS which one it used (user-decided): `build.json` records the source
(origin, commit, dirty), the toolchain (gcc, libstdc++, binutils, musl, target),
the OS it was built on, and the exact compile and link flags. A hash that differs
is then a question with its answer inside the file.

Everything in that record is a function of the INPUTS. No timestamps, no
hostname, no absolute paths - the linkscript is written as a placeholder, because
where a machine keeps the guest kit is not something a rebuilder needs. Anything
time- or machine-dependent would make two builds of one commit differ, which is
the property all of this exists to keep.

## The whole pipeline accounts for itself (2026-08-14)

A run is reproducible only if every binary in the path can say where it came
from. Three could not, in different ways, and a movie recorded none of them.

- **The core package** carries `build.json`: source (origin, commit, dirty),
  toolchain (gcc, libstdc++, binutils, musl, target), the OS it was built on, and
  the exact compile and link flags.
- **The waterbox host** answers `wbx_build_info()` with its own commit, compiler,
  OS and target - compiled in as defines, since two build systems compile those
  sources. The sandbox is meant to change no emulation; that is a claim to be
  checkable, not asserted.
- **The frontend** already stamped its commit (`GIT <branch>#<hash>`), and now
  says so on the console at startup, where a log keeps it.
- **Firmware** is recorded per movie as `<id>=<sha1>` pairs in a canonical order.
  A disk system with a different BIOS is a different machine, so a movie without
  this was never reproducible; the ordering is fixed so replay reports a
  difference only when the machine really differs.

A movie header therefore carries: the frontend version, the core name, its
version (the commit) and package SHA1, the waterbox host's build, the firmware
hashes, and - when the game came from one - the bundle and its content id. Each
mismatch reports itself in its own words, because "something differs" is not
actionable and "your BIOS is not the one this was recorded with" is.

The discipline that makes all of it hold: **everything recorded is a function of
the inputs**. Not a timestamp, not a hostname, not a path. The moment provenance
starts describing the moment rather than the ingredients, two builds of one
commit stop matching and every hash in the chain becomes noise.

## A long wait says what it is waiting for (user-asked, 2026-09-05)

Opening, creating and saving a project could take a long time with nothing on
screen - a PlayStation 3 boot fetching its compiled code, a four-gigabyte disc
being hashed, a greenzone of hundreds of megabytes going through zstd - and a
frozen window is a window a person kills. The work stays on the UI thread, as
it must (the emulator and the engine are not shared across threads), so the
answer is not a worker but a report: the engine gains one progress sink
(`ce_progress_set`), and its slow calls report into it as they go - hashing a
file says the file's name and bytes of its length, the state container says
bytes compressed or read, a boot says its stage, the cache bridge counts the
objects it fetches. The frontend's own steps report into the same stream
(`EngineProgress`), and a small `ProgressDialog` listens while it is up, draws
a bar when the stage has a length and a clock when it does not, pumps the
message loop from those reports, and disables its owner so a click during the
wait cannot start a second operation inside the first. The engine test hashes a
nine-megabyte file with a sink installed and checks the last report reaches the
end, and that nothing is heard when no sink is set.

## Turbo: a frame nobody looks at does not have to be drawn (user-asked, 2026-08-29)

Fast-forwarding to the interesting part of a run spends most of its time drawing
pictures that go past faster than a person can see. The frontend already asked
for those frames with `render: false`, but that only skipped the memcpy out of
the sandbox - the core drew every one of them anyway. Turbo is the other half:
an optional guest export, `SetRenderingEnabled(int on)`, that tells the core
itself to stop.

**The contract is one sentence: while off, the core produces no picture and is
otherwise exactly the machine it would have been.** Nothing else is allowed to
change - not a byte of memory, not a sample of audio, not the lag count. A turbo
that got this wrong would corrupt a TAS silently, which is the one failure this
project cannot ship.

**Every core's gate proves it, the same way.** `run-gate.sh` grew a turbo leg:
run the frames twice, once drawing them all and once with drawing off for the
first half and back on for the second, then compare everything except the
whole-run video hash - memory domains, audio, lag count, and *the pictures of
that second half*. That last part is what makes it a real test rather than a
formality: a core whose skip disturbed the machine would still be caught by a
different picture even when every byte of RAM agreed.

Two cores need a one-frame settle window before that comparison starts, and both
earn it by measurement rather than assertion. A PS2 field is woven with the
field before it, so the first frame drawn after a gap has nothing to weave with;
DOSBox-X redraws a row only when it differs from the last row it drew, so the
frame that resumes is the one that rebuilds the surface. Both converge on the
very next frame, and both runs skip the same one, so nothing else is excused.

**What can be skipped is decided per core, by where the picture stops being the
machine's business.** The answer is different every time and the gate is what
settles it:

| core | what turbo skips | share of a frame |
|---|---|---|
| DOSBox-X | the copy out of the finished surface | 33% |
| snes9x | `IPPU.RenderThisFrame`, upstream's own, which still ORs the sprite range/time-over flags into `PPU.RangeTimeOver` | 24% |
| stella | `TIA::renderPixel` and the line clone. The collision latches are updated elsewhere; the frame-layout detector still gets its colours, because that IS the machine | 14% |
| quickerNES | the PPU's pixel output (upstream's own render-off path, which keeps sprite-zero hit exact through a mini offscreen buffer) | 8% |
| NesHawk | the pixel pipeline, which is the only thing that writes `xbuf`; the sprite-zero hit is decided elsewhere | 8% |
| gpgx | `remap_line` - the VDP's output step. Everything the 68000 notices (sprite overflow and collision, the pattern cache, the sprite parse) happens before it | 6% |
| opera | the VDLP's scanline renderer - the 3DO's display processor, which only reads what the cel engine already wrote into VRAM | 5% |
| PPSSPP | the framebuffer readback and its conversion, and nothing else | 3% |
| PCSX2 | the display stage - the PCRTC merge and the deinterlace. Under the OpenGL renderer that is also where the per-frame readback lives | 0% (software renderer) |
| flycast | the whole rasterisation, refsw and OpenGL alike. A render-to-TEXTURE pass is never skipped: that one is written back into video memory and the game reads it | not measurable here |

The last column is the share of a frame that turned out to be drawing, measured
by running each core's own gate content twice with half the frames undrawn. It
is not a promise about any particular game: flycast's rasteriser runs only when
a program submits a display list, and the gate's test programs submit one, so
that row waits for a real disc. PCSX2's zero is the software renderer, where the
display stage is a reference into GS local memory; the hardware renderer, where
that stage is GL work and a readback, is unmeasured on a box with no GPU.

**The 3D consoles gain the least, and the reason is worth stating.** A PSP or a
PS2 draws into memory the game can read back - VRAM is a memory domain, the
texture cache samples from it, the CPU can DMA out of it. So on those machines
the drawing IS the machine, and skipping it is not a fast-forward but a
different emulation. What turbo can take there is the display stage on top. The
Dreamcast is the exception in the other direction: Flycast's rasteriser writes
nothing back into video memory unless the pass is a render-to-texture, so the
whole of it can go. DOSBox-X was tried the deep way too - upstream has its own
`render.disablerender` switch - and the gate refused it: conventional memory and
physical RAM both moved, because the RENDER layer is wired into the VGA's event
scheduling. That is a measurement, and it is why DOS gets the thin cut.

**Where the flag lives matters.** It is the frontend's policy for the moment,
not part of the machine, so it goes in `ECL_INVISIBLE` memory - the same rule
the live-settings buffer follows, and for the same reason: a state saved while
fast-forwarding must not put the machine back into turbo when it is loaded to be
looked at. Where a core has its own knob inside the savestate (NesHawk's PPU,
snes9x's `IPPU`, PPSSPP's driver), the frame re-asserts it from the invisible
flag rather than trusting what a loaded state left behind, and the engine
forgets what it last sent after every state load.

**The engine sends deltas.** `ce_session_frame_advance`'s `render` argument now
reaches the core, but only when it changes - the seek path advances thousands of
frames and one pointless guest call each would be the wrong kind of thrift.
`chimera-run` has always replayed movies with `render: 0`, so the witness gate
now runs every Level E movie with drawing switched off end to end and still
matches the goldens byte for byte.

**In the frontend, a seek draws only its destination.** Held Turbo keeps the
throttle's one-in-four frames, because the person is watching to see where they
are. A turbo SEEK has a destination and draws it; the frames on the way are
nobody's business, so none of them are drawn - which is where the whole feature
was aimed.

## A button is where it says it is, and something on the machine says so (user-asked, 2026-08-30)

Two reports said PlayStation 2 buttons landed in the wrong places: pressing
Circle gave Triangle, pressing L1 gave R1. That is the kind of claim nothing in
the gates could answer, because every layer between a TAStudio column and the
emulated pad is keyed by the SAME NAMES. A swap in one of them looks correct
from both ends, and the two flavours agree with each other whatever they do.

**So ask the machine.** `tests/own/padtest.elf` is a PlayStation 2 controller
tester (by jbit, free to distribute) that draws every button of a DualShock 2
with the pressure the console is reading. Hold the button the package declares
as "Circle" and exactly one readout may move: the circle. The new `pad:mapping`
gate leg does that for all sixteen, and all sixteen were already right - through
the core, and again through a project and the engine's own movie decode. The
wire was never wrong.

**What WAS wrong was the label.** TAStudio heads each column with the control's
MNEMONIC - the single character a movie log is written with - and the base table
gives L2 the 'L' it gives Left, and R2 the 'R' it gives Right. Two pairs of PS2
columns read identically, and a person clicking one of them has no way to know
which. The Analog button had no entry at all and came out as '!'. PS2 now
overrides those three, and a test holds every shipped controller to the rule
that no two of its controls may share a mnemonic - which is a property of the
movie format as much as of the window, since an ambiguous log cannot be read by
eye either.

Changing a mnemonic changes the character a NEW log is written with. Old logs
still load: an entry is decoded by position and any character but '.' means
pressed, so nothing recorded stops replaying.

**The honest remainder.** Circle-gives-Triangle was not reproduced, here or
anywhere. What was found is real and in the same neighbourhood, and it is not
proof that it was the whole of what someone saw.

## A PlayStation 2 runs programs, not only discs (user-asked, 2026-08-30)

PCSX2 has always been able to start from an executable - it is how homebrew and
every test program in the world ships - and this core could not, because it
handed every file it was given to the disc path. It now looks at the file: an
ELF is run directly with an empty tray (upstream's `elf_override`, which also
forces the fast boot it needs), anything else is a disc image.

**By the first four bytes, not the extension.** Upstream decides on the ".elf"
suffix, which is right for a file picker and wrong here: a project names its
files whatever their author called them and the slot map carries that name into
the guest verbatim. What makes a file a program is that it is an ELF.

**An .irx is refused, and says why.** IOP modules are ELF files too, so the
magic alone would boot one as if it were an EE program and watch it fail
strangely. They carry Sony's own `e_type` of 0xFF80, which makes them
recognisable in the header, and the refusal says what an .irx is for instead of
reporting a crash.

One sharp edge paid for on the way: the sandbox's file system is flat, so
`hostRoot` is always empty, and upstream's `host:` resolver leaves the path
empty when it is - which made the machine unable to read the very ELF it had
just been told to boot. In a flat file system a name IS its path.

## A guard page is not a write barrier on Windows (2026-09-10)

Seeking through the history gave back a machine the plain run never had. Only
on Windows, only with ares, and only through the greenzone: saving and loading a
full state every frame was exact, so the fault was in the page-level dirty
tracking rather than in the state format. Sixty-eight bytes of Game Boy work RAM
were wrong after seeking two hundred frames back over a four hundred frame run,
and further back than about four hundred frames the guest jumped to address
zero.

**What the sandbox was doing.** miniBox tracks writes by protecting memory: a
page that matches the baseline is read-only, the first write faults, the handler
copies the pre-image and lets the write through. Windows cannot do that for the
page the stack pointer is in - it delivers an exception by pushing a context
record onto the faulting thread's own stack, so the kernel's write fails too and
the process dies with no handler having run. ares hands every emulated component
a coroutine stack out of malloc, so its stacks looked like ordinary memory and it
died on its first frame. The fix at the time was to protect every clean page
with the guard bit instead, because the kernel clears that bit BEFORE it raises,
which makes the fault deliverable wherever the stack is.

**Why that was wrong.** A guard bit can also be cleared with NO exception
delivered at all. Instrumenting the arena found a few hundred pages an epoch
that miniBox had told Windows to protect (`VirtualProtect` returned success, and
querying straight afterwards confirmed the bit was set) and that later read back
as plain PAGE_READWRITE with no handler ever having run for them. Every write to
such a page after that is invisible, and the page still says clean - so the
frame's delta does not carry it and the baseline it will be reverted to is the
wrong one. A read-only page cannot fail that way: nothing but the sandbox itself
can make it writable.

**So the contract changed instead: a guest says where its stacks are.** Ordinary
memory is read-only-when-clean on Windows exactly as it is on Linux, and MAP_STACK
is how a guest asks for a stack. ares' libco asks for its coroutine stacks with
mmap now (patch 0015 in that core) instead of taking them from malloc. A stack
page on Windows is then never protected at all, and nothing reports its writes.

**What a stack did is READ, not watched.** The first version of this called every
stack page written whether or not it was, which is correct and costs everything:
ares gives a Game Boy nineteen coroutines a stack of their own, 2.6MB of them,
and all of it went into every delta. Seven hundred frames of history came to
268MB where Linux made 110MB, and capturing a frame cost 74% of the run against
Linux's 20%. Almost none of those pages had changed - almost none of a coroutine
stack is ever touched.

Nothing there can report a write to a stack, but anything can read one. Each
stack page keeps a shadow of what it held when the last frame was described, and
a delta asks memcmp which of them moved; "differs from the sealed image", which
is what a savestate needs, is the same question against the baseline snapshot.
Both are exact, and both are stricter than a fault bit, which stays set when a
page is written and then put back. The same seven hundred frames now cost 107MB -
SMALLER than Linux's 110MB, for exactly that reason - and 32% of the run against
Linux's 19%. What is left is one 2.6MB comparison a frame, so the remaining
difference is set by how much stack a core asks for, not by how big its machine
is.

**Two smaller things fell out of the same measurement.** An epoch marked a stack
page eagerly and then dropped it from the set it re-examines, so a stack was
captured by one frame and never looked at again; and `VirtualQuery` was being
read as if `RegionSize` were the length from the address asked about, which it is
not - it counts from the region's own base, so the walk stepped over the pages
after it.

**What this is not.** Nobody has identified what clears those guard bits. The
exception-dispatch theory - the kernel scribbling its context record onto pages
below the stack pointer - was tested by marking every page near a faulting `rsp`
and does not explain them; not one of the lost pages had ever been near one. It
does not matter for the fix, because the fix is to stop depending on the bit, but
it is not a solved mystery and should not be written up as one.

## A seek costs what changed, not what the machine could be (user-asked, 2026-09-10)

The ask was a deep look at the state history with speed as the only first-class
goal: forward capture, going back to an older frame, and the rerecord that
follows an edit. Memory and disk were welcome but second. Everything below was
measured before it was touched, on ares - Super Mario 64 (an 18 ms frame, some
300 pages written each) and a Game Boy (a 3 ms frame, some 70) - with the two
benches in `tests/perf` for the parts a core would only blur.

**Going back was proportional to the arena.** Restoring a frame is one anchor
loaded and the deltas since it applied, and applying a delta ended by
re-protecting every page there is: half a million lookups and syscalls on the
ares arena, 2.3 ms for a delta of sixteen pages, paid once per link. A restore
of 34 links took 59 ms on the N64 and 54 on the Game Boy, whose deltas are a
quarter the size - the same number, because the cost was not in the delta. The
anchor load walked the same half million pages to find the few that differed.
Both are proportional to the change now: a delta refreshes only the pages in
its two lists whose protection actually moved, and the load compares the
machine's packed status and dirty maps against the state's a word at a time,
skipping eight untouched pages per two loads. The same restores are 3 ms and
2 ms; on the bench a 2 GB seek of 32 links went from 78 ms to 1.7.

**Forward capture was paying a fault for what it already knew.** A page the
machine writes every frame - the framebuffer, the audio ring, the CPU's own
registers - faulted every frame, and was re-protected every frame so that it
could fault again. On the N64 that was two thirds of what a captured frame
cost. A page written a few frames in a row now goes HOT: it stays writable, and
what it did is found by comparing it with a copy taken when the epoch opened.
Four kilobytes copied and compared is a fraction of a microsecond; a fault is
several. A hot page that stops changing cools after a few frames and is held
like any other. The comparison is exact where a fault is not, so a page written
with the bytes it already held stays out of the delta - the Game Boy's frames
came out a third smaller, 700 of them 85 MB where they were 110. The bench's
300 pages written every frame went from 1.75 ms to 0.06. On the real cores the
history's share of the run fell from 9% to 5% (N64) and 14% to 11% (Game Boy);
what is left is the pages a frame writes for the first time in a while, and
those are what the fault is for.

The invariant is the whole design: **a hot page's shadow is the page as the
epoch opened.** Opening an epoch copies every hot page, because nothing in the
sandbox can know whether the guest ran since the last delta was saved, and the
two operations that rewrite pages from outside the guest - a state loaded, a
delta applied - refresh the copy or cool the page as they go. It was tempting to
refresh the shadow only when a delta was saved and skip the copy at open; that
is wrong the first time the engine takes an anchor instead of a delta, because
the frame between ran with no epoch and the shadow is a frame stale, and a page
written back to its older bytes would then be left out of a delta whose start
did not hold them. The copy costs a tenth of a millisecond and the reasoning
costs nothing to keep.

**The client was spending as much on a seek's frame as the core did.** A
TAStudio seek runs the main loop once per frame, and every frame refreshed the
piano roll, presented the picture and pumped the message queue - 5.9 ms a frame
on the Game Boy, of which 3.0 was the machine. The headless mode already served
the host on a wall-clock cadence instead of per frame, for exactly this reason;
a seek now does the same: sixty times a second the window is live, shows where
the seek has got to and takes a click to stop it, and the frames between are
emulated and nothing else. The destination frame is always drawn and shown.
`CHIMERA_LOOP_TRACE=1` prints where each frame's time goes, phase by phase,
because this was found by measuring and the next such thing will be too.

Two sharp edges. The frames a seek passes through get the tools' fast update
whether or not it is a turbo seek, so a Lua script that counts frames during a
seek needs "Run Lua during turbo" now as it already did during one. And on the
software-drawn box this was measured on, a TURBO seek is slower than a plain one - the
present and the message pump cost several milliseconds each there with no frame
drawn, which reads like software GL and a progress bar repainting - and that is
not understood; it is bounded to the sixty services a second now, and the GPU
box has not been measured.

**A bug was found on the way and is fixed:** applying a delta copied a page's
baseline aside AFTER overwriting it, so a page this process had never written -
one from a history file, applied to a machine that had not reached that frame
itself - kept the delta's bytes as its sealed image, and every later return to a
frame where the page was clean put those bytes back. In-process it could not
happen, because every page in a delta had faulted on the recording run and had
its copy already; a reopened project is exactly the case it could.

## What was spilled early is settled, and the seek loop measured where it runs (2026-09-10)

The three loose ends of the round above, taken in turn.

**Density on disk is a temporary condition now.** A stretch spilled before the
far band reached it kept every frame's delta on disk for good, because
`coarsen()` skipped anything spilled - 1567MB of file for six thousand Game Boy
frames under a 64MB budget, the state-manager doc's own example. Once the far
boundary passes a spilled stretch it is now settled: its links are read back
one at a time, composed down to the far grid under the same caps `tidy()` uses,
and what is left is appended to the file; the old body is dead room and the
compaction takes it back. Four links a call, so it is a little work every frame.
The first version read the whole stretch into a working copy and refused any
stretch bigger than the budget - which is every stretch that was spilled under a
small budget, the only ones that need it. Streaming is the point, not an
optimisation: what is held is one accumulating link, one just read, and the
result, which is far-band sized. On a Game Boy under an 8MB budget with tight
bands, each 61-frame stretch went from 9.5MB on disk to 1.9MB, and the file's
live bytes from 112MB to 46MB.

**The turbo-seek anomaly was the software drawing's.** Measured on the Windows
box with the real GPU, headless, with the same project and Lua kicker: a plain
Game Boy seek is 3.5 ms a frame and a turbo one 3.1, against 2.7 for the machine
alone. The several milliseconds a present and a message pump cost with no frame
drawn were the software GL, and are not a Windows problem. Two more phases in the loop trace - movie and sound - showed where the
rest of the client's time goes there: the movie's frame handling is the
greenzone capture itself (0.22 ms, the history's share of a Game Boy frame), and
the message pump at sixty services a second is the piano roll repainting, which
is what the person is watching.

**Not taken: the anchor's two bytes a page.** An anchor carries a status byte and
a dirty byte for every page of the arena - 1.1MB on ares, some 21MB on rpcs3 -
and packing them would be a savestate format change. The stream layout is fixed
in miniBox's machine spec, anchors are one in six hundred frames, and the load
already skips the untouched pages a word at a time, so the saving is a few
per cent of memory on the biggest machines against a format every saved state
and history file would have to keep reading. Left as it is, and written down so
that it is not rediscovered as an easy win.

## A test that cannot fail is not a test (user-asked, 2026-09-10)

Asked to look for bugs and latent runtime failures across the state machinery,
rather than only in the round just written. The method is worth keeping, because
the bugs it found were not ones reading the code had suggested.

**Two randomized differential tests, each against a model.** miniBox's drives
the page tracker with random writes, maps, unmaps, protections and zeroings and
asks whether an anchor plus its deltas reproduces the machine byte for byte,
seeking backwards and continuing from where it lands. The engine's does the same
to the history with random frames, restores, edits, pins, budgets, spills and
save/load round trips. Deterministic per seed, so a failure names its seed and
step and can be re-run.

**Then every fix was checked by putting the bug back.** Eight mutations, one per
fix: the test that was supposed to catch it had to fail, and the tree without it
had to pass. Three of them did NOT fail, and that was the most useful part of
the exercise - it showed two of the things being "fixed" were not reachable:
- The protection run left unclosed when a delta apply fails only ever makes
  pages LESS permissive, and the next epoch re-protects them anyway. Kept for
  the single exit, but it is not a bug fix and is not written up as one.
- The first version of the truncated-load test wrote through an epoch, and
  `epoch_begin` re-protects everything mapped writable, so it healed the damage
  before the test could see it. The exposure is a write BETWEEN the failed load
  and the next epoch, which is exactly what a refused restore leaves the session
  free to do. Written that way, it fails without the fix.

The fuzz's own first failure was a bug in the fuzz: it modelled an input edit by
changing the machine BEFORE opening the epoch, so the change sat in the epoch's
baseline and never entered the delta. A real edit diverges the next frame's
state, inside the epoch. Worth saying because the shape recurs - a model that
does not do what the thing it models does will report the difference as the
subject's fault.

**What the tests are not.** They use a machine of sixty four cells and a merge
of their own, so they say nothing about miniBox's composition or about a real
core. That is what the synthetic witness, the ares gate and the rerecord suites
are for, and all of them were run.

## Going back has to be possible even where the greenzone gave up (user-reported, 2026-09-10)

Reported from use: a rewind to a part of the movie the greenzone no longer
covered did nothing at all, and something crashed at some point.

The cause was one line of policy. The disk budget dropped the oldest stretch in
the spill file whichever one it was, so the stretch holding frame zero went like
any other; and with nothing stored at or before the target, the piano roll's
GoToFrame loaded nothing and then unpaused to seek forward from a frame already
past the one asked for. It cannot arrive. From the outside that is a rewind that
does nothing, with no message.

Three things follow, and the order matters.

**The beginning of the run is not a cache entry.** Everything else the history
holds is an optimisation - lose it and you replay - but frame zero is what makes
replaying possible at all. It is never evicted now, in memory or on disk. The
encode path had already written the invariant down ("Frame zero always has a
state") and would have thrown; the piano roll trusted it silently.

**What survives should be spread, not recent.** Keeping the newest and dropping
the oldest is the obvious policy and it is wrong for a TAS: it empties the far
past first, so the further back you want to go the less there is to go on, until
the middle of a long movie costs a replay from zero. The stretch given up is now
the one whose absence widens the gap between its neighbours least. Applied
repeatedly that thins the whole run evenly and leaves anchors across it - the
breadcrumbs the report asked for - so any frame costs one anchor load and a
bounded replay. It is the same thought the bands already apply to landings,
applied to whole stretches.

**A seek that cannot arrive is not started.** The guard above should make it
unreachable, but "unreachable because of an invariant elsewhere" is how this got
shipped in the first place, so the piano roll now says what is wrong and stays
where it is.

What is NOT done, and is the honest limit: if a project's history is lost or
belongs to another machine, the frontend still has no way to rebuild the machine
from power-on and replay - it starts from wherever the emulator is. Frame zero
being kept means that case does not arise while a session is running, but a
project reopened with no usable history has only the frames it captures from
there. Rebooting the core and replaying the movie is the missing piece.

## A getenv is not free when a renderer makes fifty thousand calls a frame (user-reported, 2026-09-10)

Reported from use: Flash under Ruffle is slow in Chimera and not in vanilla
Ruffle. Measured rather than guessed, and it was two things of about the same
size.

**The GPU bridge asked the environment a question on every crossing.**
`ce_gl_dispatch` called `getenv("CHIMERA_GL_CHECK")` per GL call, on the
reasoning - written down in the code - that a getenv is nothing beside a GL
call. Ruffle's wgpu backend makes SIX THOUSAND GL calls in an average frame and
fifty thousand in a heavy one, so the walk through the environment was being
paid fifty thousand times a frame to answer a question whose answer cannot
change while the process runs. Read once, as everything else in this codebase
that reads an environment variable already does, the machine's own frame went
from 58.8 ms to 32.9.

The lesson is not "getenv is slow". It is that "nothing beside X" is an estimate
of a ratio, and a ratio is only an argument when the count is known. The count
here is now printed by `CHIMERA_GL_TRACE`, per frame, precisely so the next such
claim can be checked rather than believed.

**And the greenzone was capturing twenty megabytes a frame** because the near
band kept every frame whatever it cost. That half is in docs/state-manager.md.

Together: 112 ms a frame to 48, which is nine frames a second to twenty-one.
What remains is the six thousand crossings themselves - each one a call out of
the sandbox - and that is a batching problem, not a constant-factor one.

## A machine's own settings were indexed against the wrong list (user-reported, 2026-09-11)

Reported from use: the Neo Geo shows no settings at all, and it used to.

The settings were there. They are declared in the package, scoped to the machine
with `when`, and `machines.json` and `waterbox.config` both carry all seven of
the Neo Geo's. What went wrong was arithmetic.

The engine answers "which settings are exposed" with a NAME and an INDEX, and
the index counts into the declaration it was handed - the package's own settings
list. The wizard then read that index out of `SettingsFor(machine)`, which is a
different list: the machine's, with the settings belonging to other machines
dropped and everything after them renumbered. A name check caught the mismatch
and threw the entry away, so nothing was ever displayed wrongly - it was simply
not displayed. Every setting a machine narrows disappeared: the Neo Geo's DIP
switches, the Game Boy's Fast Boot, the Super Famicom's PPU revisions, all of
them, on every multi-machine core.

`SettingsByDeclarationIndexFor` answers in the package's own index space - one
slot per package setting, null where the machine does not have it - and the
wizard reads that. The test pins the shape rather than the symptom, because the
symptom (nothing shown) is indistinguishable from "this machine has no
settings", which is exactly why this survived.

**Worth saying about the Neo Geo in particular**, since that is where it was
found: those seven settings ARE its DIP switches - they go straight into
REG_DIPSW - but an **AES never reads that register**. Traced over 900 frames of
two commercial games, it is read zero times, because the AES is a home console
and has no DIP switches on it. The machine that reads them is the MVS, the
arcade board, and the ares core does not offer it yet for a reason recorded in
that core's own PLAN.md: ares has no uPD4990A, so the MVS BIOS waits on a
real-time clock pulse that never comes. The switches are exposed now because
they are declared and a frontend must not silently eat a declaration; what they
do is the core's business and the core says so.

## The GL bridge can say WHICH calls, not just how many (2026-09-11)

`CHIMERA_GL_TRACE` has printed a per-frame call count since the getenv round,
and that count is what made "six thousand calls a frame" a sentence anybody
could say. It is also what made it a sentence nobody could act on. Ruffle's
frame on New Star Soccer is fifty thousand calls; the next question - which of
them, and costing what - had no instrument.

Two now:

- **`CHIMERA_GL_TIME=1`** adds "N ms in the driver" to the per-frame line. The
  clock read either side is real, about 20ns a call, so a fifty-thousand-call
  frame pays a millisecond to be measured; that is worth knowing when reading
  the number, and it is why this is off by default.
- **`CHIMERA_GL_PROFILE=1`** counts calls and nanoseconds per opcode and dumps
  the table every 300 frames and again at exit. The names are deliberately not
  carried in the process: opcodes are the master list's order (miniBox
  `source/gl/gl-entry-points.txt`, first entry is 100) and resolving them is a
  job for whoever reads the dump, not for the hot path.

The exit dump is not belt and braces. `ce_gl_release` marks the frame boundary
and a host that is not drawing never calls it, so a run with rendering off
collected the whole profile and printed none of it.

What they found on a GTX 1060 is in the Ruffle core's own PLAN.md, and the
shape of it is worth repeating here because it is general: **forty thousand GL
calls cost 1.5 ms between them, and six calls cost 11.** The expensive ones
were `glGetSynciv` and `glGenBuffers` - waiting for the GPU, and allocating
fresh resources every frame. A call count is a measure of chattiness and
chattiness is not cost.

And a correction to the round before: the same feature measured 6 ms a frame
under llvmpipe and 1.5 ms on the real GPU. A software rasteriser's readback is
a copy out of memory the CPU has just written; a real driver's is a transfer it
has already had to wait for. Numbers taken on Mesa in WSL do not transfer to a
GPU, and this codebase has a whole memory note saying so - which did not stop
it happening.

## A deleted buffer's name is kept (2026-09-11)

The profile above said Ruffle's frame spends 1.74 ms in `glGenBuffers`. A
hundred and thirty-nine buffers created, filled with `glBufferData`, and deleted
every frame - the counts pair exactly - at twelve microseconds a call for an
entry point whose whole job is to reserve a name.

Reserving a name does not cost twelve microseconds. What costs that is the
driver draining deferred deletes: the buffers being freed are ones the GPU is
still reading, so the delete is held, and the next reservation queues behind it.
The churn is paid for twice.

So the bridge keeps the name instead of giving it back, and answers the next
request from that list. Three things make it safe rather than clever:

- **A recycled buffer is re-specified before use.** Every one of those hundred
  and thirty-nine is followed by a `glBufferData`, which replaces its size and
  contents outright and orphans whatever the GPU still held. Immutable storage
  would not allow that, but a guest across this bridge cannot have
  `ARB_buffer_storage` - it hands out a host pointer - so the mutable path is
  the only path here.
- **Only names the bridge handed out are pooled.** A guest deleting something it
  never generated has a bug, and passing that through to the driver is how the
  bug stays visible.
- **The list is bounded.** Past the cap a delete is a real delete, so a guest
  that frees far more than it allocates cannot turn this into a leak.

Measured on a GTX 1060, frames differenced to remove startup: 18.12 ms a frame
to **16.97**, and the driver's share 13.50 to 11.36. `glGenBuffers` leaves the
profile entirely. `CHIMERA_GL_NO_POOL=1` turns it off, which is how the A/B was
taken and how the next person can retake it.

**The oracle for a change like this is the picture, not the clock.** Three
frames apiece of Ruffle and of Dolphin, captured on the real GPU with the pool
on and off: byte-identical, all six. A GL object pool that changes a pixel is
not an optimisation, and nothing else in this codebase can tell you that it did.

### Deferring texture deletes: tried, removed, then actually measured

Textures churn the same way - a hundred created and deleted a frame, and
`glGenTextures` is 1.13 ms while the `glTexStorage2D` behind it is sixty
NANOseconds. They cannot be pooled: `glTexStorage2D` is immutable, so a recycled
texture must be handed back for exactly the shape it already has, and the shape
is not known until the call after the one that has to choose a name. Doing it
properly means translating texture names throughout the bridge.

The cheap version - hold the deletes a few frames so the GPU has finished before
the driver is told - was written and removed. It is 0.5 ms a frame WORSE:
17.74 and 17.59 against 17.22 and 17.19 for buffer pooling alone.

**Those are not the numbers this section first carried, and the first ones were
worthless.** `chimera-run` has the engine linked straight in (`meson.build`:
the engine's static library, linked whole); it does not load
`libchimera.dll`. Every A/B taken by copying the DLL beside it and leaving the
executable alone measured the same binary twice, and reported the difference
between two runs of it as the difference between two builds. Copy BOTH, and
md5sum both, every time - the note about staging a fresh directory per
experiment was already in this codebase's memory and it was still not enough,
because it did not say which files.

## The fence waiting is the GPU, and the GPU is busy (2026-09-11)

After the buffer pool, the largest single item in Ruffle's frame was 6.7 ms in
`glGetSynciv`. An average cannot say what that is: every call costing a hundred
microseconds is overhead, and one call in fifty costing five milliseconds is a
BLOCK. So the profile grew a longest-call and an over-50us count, and then
`CHIMERA_GL_WHY=N`, which prints the last sixteen opcodes whenever a call
blocks - because a profile says which call waited and never what the wait was
for.

It is unambiguous. In steady state, 2851 of 3428 blocking events were
`glGetSynciv` and every one of them came immediately after
`DrawElementsInstancedBaseVertexBaseInstance`: about eighteen a frame, 267
microseconds each, the guest issuing a draw and then waiting for the GPU. The
rest is the readback, once a frame, as a chain that is exactly what it looks
like - `ReadPixels`, `FenceSync`, `Flush`, `GetSynciv`, `ClientWaitSync`,
`GetBufferSubData`, each one blocking, about 2 ms together.

**The waits are real.** `CHIMERA_GL_GPUTIME=1` brackets each frame with
`glQueryCounter(GL_TIMESTAMP)` and reads the pair a frame later: the GPU's own
span is the same order as the whole frame. And the frame tracks GPU work -
Ruffle's `quality` from high to low is 17.07 ms a frame to 14.43, a 15% cut for
nothing but less multisampling. The CPU is waiting on a GPU that has the work.

So the bridge cannot take this one. It is wgpu's OpenGL backend synchronising
because it has no persistent buffer mapping, and it cannot have one here: a
persistent map hands back a host pointer, and a sandboxed guest cannot read host
memory. That is the same root cause as the buffer churn the pool now absorbs,
and the rest of it lives in the guest. Named, measured, and not pretended away.

## A seek does not draw, and that is a promise about the PICTURE too (user-reported, 2026-09-11)

Reported from use: a PlayStation 2 project's picture degrades as you re-record,
without desyncing, and only playing the movie from the beginning puts it right.

Four measurements, on a GTX 1060, took it apart. Rewinding once, five times and
twenty times all produced **exactly the same** difference from a straight run -
83,663 bytes - so it is not cumulative. Saving and reloading the state before
every one of 3000 frames produced no difference at all, so it is not the
savestate. A straight run with no rewind anywhere, composing only its final
frame, produced **the same 83,663 bytes**, so it is not the rewind. And the
software renderer did the same thing, so it is not the GPU.

What it is: **a seek replays with drawing off, and one renderer's display stage
carries state from frame to frame.** Chimera turns rendering off for the frames
a seek passes through - nobody is looking at them - and PCSX2's turbo patch
implements that by returning from `GSRenderer::VSync` before `Merge`. Merge is
not only composition: it decrements a scanmask countdown, advances the
deinterlace phase, and leaves the device holding the frame a blend deinterlacer
will want next. Skip it for fifteen hundred frames and the one frame that IS
composed comes from state that never saw them.

The first fix was a warm-up: draw the last five frames before a seek's
destination, because drawing the last frame only is 7.29% of the picture wrong,
the last two 3.87%, and the last five exact. **It was the wrong fix, and the way
it was wrong is worth keeping.** Marvel vs Capcom 2 redraws its whole screen
sixty times a second, so a few frames of drawing really does rebuild everything
that matters. A title screen does not. Flycast on Re-Volt, seeking to a title
screen painted once around frame 1350 and then left alone: drawing the last 1,
2, 4, 8, 30, 60 or 120 frames all give the same picture, and it is the SEGA
licence screen, a whole screen earlier. Only 300 - far enough back to include
the frame that painted it - is right. What the renderer draws lives on the far
side of the bridge and STAYS there; a screen painted by exactly one frame is
lost if that frame is skipped, and no number of later frames repaints it,
because the game has nothing more to say. **There is no number to declare.**

So the core is simply never told to stop drawing (`video.drawEveryFrame`, read
by the engine at session open), and `render == 0` comes to mean only that the
host does not copy the picture out. That is affordable because the drawing was
never the expensive part: over 1500 frames on a GTX 1060, PCSX2 costs 8.28s in
turbo, 8.30s drawing without reading back, and 9.75s doing both. The readback -
a 1.2 MB `glReadPixels` across the bridge - is the whole saving, and turbo keeps
it.

The general rule this is an instance of: **"nobody is looking at this frame" is
a statement about the OUTPUT, not a licence to skip the renderer's own
bookkeeping.** Ruffle gets it right by construction - its rendering-off path
skips the readback and still runs `Player::render`, which is where its caches and
its `Event.RENDER` broadcast live - and Dolphin gets it right because its XFB
always decodes from the machine's own memory. Both measured at 0.00% either way.
PCSX2 and Flycast skipped the whole stage, and this is what that cost - and
they are the only two bridged cores that did.

It also says something about what a hardware renderer's picture is allowed to
be. The greenzone's contract has always been about the MACHINE, and this did not
break it: EE RAM was byte-identical through every one of these runs. But a
person watching TAStudio judges by the picture, and a picture that depends on
which frames happened to be drawn is a picture that cannot be trusted to mean
anything. Costing 0.75 ms a frame, there was no reason to leave it.

## A movie's log has a shape, and only the machine can describe it
(user-reported, 2026-09-11)

Reported from use (issue #54): open a project, seek so some frames are green,
select a range and press Delete. A "Failed to draw input roll" box appears, the
project closes, and the frontend then falls over painting the roll it no longer
has a movie for. Not core-specific: seen on DOSBox-X, reproduced on PCSX2.

What failed, from inside the paint, was the reading of an entry: it looked for
the comma that ends an axis field, found none, and asked for a substring of
negative length. The entry it
was reading had no axis fields at all. That entry was the one Delete had just
written: Clear is the only editing path that generates an entry from the
movie's empty entry rather than from the frame's own state, and that empty
entry was built once, lazily, and kept for the movie's life.

The order that poisons it is the order a project opens in. The file says which
core to boot and with what, so the file is READ FIRST; until that boot the
frontend's emulator is the null one, whose controller has no controls and is
perfectly willing to generate entries. The read touches the empty entry (finding
where the run's input stops), and
that first touch fixed the writer's idea of an entry as "115 controls, none of
them axes" for the rest of the session, while every reader used the machine's
own controller, which knows this movie's four mouse axes.

The corroborating measurement was on screen the whole time: the reporter's
"Last input" marker sat on frame 844, the last frame of the movie, when the run's
input stops at 744. That marker is found by comparing entries against an empty
one, and an "empty" generated from the null emulator's controller (131 characters,
no commas) matches no real entry (151 characters, four axis fields), so the search
runs off the end.

**The rule: a controller used to generate or parse a movie's entries is not a
thing to cache across the boot that defines it.** Both caches now remember the
definition and the log key they were built from and rebuild when either changes,
and attaching the movie to the machine - which happens after the session has
the machine's controller - works out where the input stops a second time, because the pass done during the read could not
have been right. What makes this class of bug expensive is that the two halves
fail asymmetrically: writing a wrongly-shaped entry is silent, and the cost is
paid later by a reader in the middle of a paint, where there is nothing sensible
to do with an exception.

## A black screenshot is not evidence until the same frame is drawn beside it (2026-09-11)

A report of "the picture is pitch black" cost two false positives in one
afternoon, both of them the same mistake. A frontend screenshot taken at frame
702 of a Marvel vs Capcom 2 project came back an unbroken 0.00% lit; so did one
at frame 600. Neither was a bug. Frame 600 of that game is black under any
input at all, and frame 702 is black under THAT PROJECT'S input and lit under a
blank movie - the project holds a start press the blank movie does not, and the
game is a screen further on because of it.

So the ground truth for a picture is the same frame of the same movie, drawn by
`chimera-run` beside it, and the input is half of "the same movie". This is
cheap to get - `--screenshot <frame>=<path>` on a run that is otherwise undrawn
costs one composed frame - and the alternative is chasing the frontend for an
hour over a game that was showing a loading screen.

The other half of the discipline is to A/B the suspect rather than reason about
it. The suspect there was `video.drawEveryFrame`, added hours earlier, and the
A/B did not need a rebuild of anything: the installed core package is a zip, so
it was repacked with the key set to `false` and run beside the real one through
the same frontend. Same picture, so not the flag. A package-level switch is
often the cheapest bisect available, and it tests the shipped artefact rather
than a build of it.

## The crash text was already on disk, and it named the core (2026-09-12)

Reported from use: a New Star Soccer project "crashes if you clear the greenzone
and come back to frame 1", on the OLD Ruffle core, on a real GTX 1060. No crash
text came with it, and the first job was not a theory but that text. It was
already on the reporter's own disk - `minibox-diag.log` in their bundle, two
faults two minutes apart, both:

	[veh] unhandled fault: addr=ffffffffffffffd0 access=read rip=0000036f0012eb07

Both halves of that line are guest addresses, so `objdump -d
--start-address=<rip>` on the packaged `core.wbx` names the instruction with no
debugger and nothing asked of anybody: `mov %fs:(%rax),%rax` at
`_x86_64_get_dispatch+7`, with `%rax` holding -0x30 from the instruction before
it. The faulting address IS `%fs_base + (-0x30)` for a base of zero, so the
report carried its own diagnosis - the guest's thread pointer was gone.

It also settled WHICH core, which the reporter had not. That symbol exists only
in the NEW Ruffle package. The old one has no `PT_TLS` segment and not one
`%fs:` reference in its entire text, and cannot fail this way; the new one
gained both by compiling Mesa in for its software renderer, whose glapi keeps
its dispatch table in a thread local.

The cause was then measured rather than reasoned about, with five lines on the
Windows box: `wrfsbase`, then arithmetic with no fault, no syscall and no yield.
The base survived `SwitchToThread`, was gone after `Sleep(1)`, and was gone
after 47 ms and 16 million iterations of plain computation - one scheduler
quantum. Windows does not keep a user-mode FS base across a context switch.
miniBox's repair, and this repository's own account of the PCSX2 version of the
same crash, had both assumed the loss happened AT a fault; that assumption is
why the repair ran on the way out of one and could not work. It is now taken on
the way IN, and runs that died inside twenty frames complete.

What the headless runners were worth here: `chimera-run` on the OLD core, on the
same GPU, survived `--seek 1`, `--seek 0 --stop-at-seek` and `--rewind-loop 1,2`,
and the frontend survived 600 frames of back-jumps and then 800 frames with the
greenzone thrown away three times and replayed from power-on after each. The
same runner on the NEW core died in twenty frames with no greenzone involved at
all. The crash reported as the greenzone's was never the greenzone's, and no
time was spent reading the state history.

Worth carrying forward separately: the project stores `renderer` as `wgpu-hw`,
a value the new package no longer declares. The guest's `cfg.choice` falls back
to its default, so that project runs the SOFTWARE renderer on the new core -
the very thing that was dying - while the frontend's `-hw` suffix test still
sees a hardware name and asks for a GPU bridge. A setting value a package has
stopped declaring is a silently different machine, and nothing says so.

One trap that nearly became a second bug report. Driving "Clear Greenzone" from
Lua the obvious way produced a `NullReferenceException` inside
`emu.frameadvance()` that looked exactly like a frontend bug on the clear path.
It was the binding. Every TAStudio Lua method that moves the emulator brackets
it with `IsUpdateSupressed`, and without that the frontend resumes the very
script that is still running: the coroutine is no longer suspended and
`CurrentFile` is null. A new binding that skips the bracket manufactures a crash
in the code it was written to exonerate.

## The Windows bundle broke on a file nobody had touched (2026-09-12)

Release started failing on a docs-only commit. The step was "Build the Windows
bundle", and the error was not ours:

	during RTL pass: pro_and_epilogue
	gl_bridge.cpp: In function '(static initializers for gl_bridge.cpp)':
	internal compiler error: in choose_baseaddr, at config/i386/i386.cc:7119

A compiler crash, in a file that had not changed, inside a function nobody
wrote. The first guess - that the runner had resolved a different mingw variant
than this box, posix against win32 - was wrong twice over. The log's own
`update-alternatives` lines pick win32, and `apt policy` said this machine
already had the runner's exact package, 13.2.0-6ubuntu1+26.1. Same compiler,
same flags, and that is worth stating plainly: it turned a CI-only mystery into
a local one. The ICE reproduced here in seconds, and every candidate fix could
then be measured rather than pushed and waited on.

What the compiler could not emit is the function C++ synthesises to construct a
file's namespace-scope objects. At -O1, where it compiles, that function is
thirty-nine instructions which realign the stack and call `atexit` three times,
once per container; at -O2 and above `choose_baseaddr` cannot find a base
register for that frame. The three containers were the object audit's lives and
the buffer pool's two vectors.

The cheap fix - build this one file at -O1 - was rejected. `ce_gl_dispatch` is
the bridge's hot path, every opcode the guest sends crosses it, and
de-optimising a whole translation unit to route around a bug in its
initialisers pays for the workaround in the wrong place. The fix deletes the
function the compiler cannot emit instead: the three containers became
function-local statics behind accessors, constructed on first use, so the file
has no static initialiser at all. `nm` confirms it - no `_GLOBAL__sub_I` - and
the file compiles clean at both -O2 and -O3.

The constraint left behind is the part to remember. `gl_bridge.cpp` must not
gain a container, a string, or anything else needing dynamic construction at
namespace scope, or the Windows Release breaks again the same way - in a
compiler-synthesised function that no diff will ever point at. The comment on
`auditLives()` says so where somebody about to add one would read it.

## A release can be skipped without anything failing (2026-09-12)

Found while watching the fix above go green. CI passed on the commit, and no
Release came out of it. Nothing had failed: CI's run was CANCELLED, and Release
follows `workflow_run` with `conclusion == 'success'`, so it skipped. A skipped
run is not a red mark anywhere - the commit simply had no release, and only
someone looking for one would notice.

The cause is that CI's concurrency group was keyed on workflow and ref alone,
with `cancel-in-progress: true`. A push to main and the nightly schedule are
the same workflow on the same ref, so they shared a group and the second to
start killed the first. They are only ever in the same window because GitHub
runs these crons very late: CI asks for 04:00 and started at 08:16, Release
asks for 05:00, and the core repos ask for 04:00 and start around 08:30 to
08:50. The push that morning landed at 08:10, straight into the nightly's real
window rather than its declared one. The same thing cancelled the Ruffle core
gate twenty-one minutes in, after it had built its Mesa and its core, taking
the publish with it.

The fix is to put `github.event_name` in the key, so a schedule and a push no
longer share a group while a push still supersedes an older push - which is the
only thing `cancel-in-progress` is wanted for here. What makes this worth a
section is not the one-line fix but the failure mode: a pipeline that reports
success on every job it ran, and quietly produced nothing. The three Release
failures before it were loud and got attention within the day; this one had
been possible the whole time and would have gone unnoticed indefinitely.

The same group shape is still in the core repos (ares, dolphin, dosbox-x,
eka2l1, flycast, gpgx, opera, pcsx2, ppsspp, rpcs3, snes9x, stella, xemu,
quickernes); Ruffle's is fixed here alongside Chimera's.

## An unimplemented syscall is not a refusal (2026-09-12)

The Ruffle core gate had been failing on a runner for two days, and it lied
about why in three different ways. Every sandbox leg failed, which looked like
the missing guest Mesa build. With Mesa built it still failed, which looked like
the gate being too slow for its 150 minute budget - software rendering really
does cost more than the bridge, and the gate really had gone from 26 minutes to
58. Then the same commit gated GREEN, which looked like a cache race between
concurrent runs. None of it was true, and none of it could be reproduced on a
workstation even running CI's exact command.

The gate ran every leg with stderr sent to /dev/null, so what it actually said
was never once printed. A smoke step that runs ONE trace test with stderr left
alone - added to the core gate, ten minutes in, before the gate is allowed to
spend a runner - printed it immediately:

	miniBox: unimplemented syscall 203 (1, 80, 36f02c3d0e0)

203 is sched_setaffinity. miniBox answers sched_getaffinity (204) deliberately
and honestly with one CPU, so that a guest's thread pools stay deterministic,
and it had never implemented setting the mask back. Mesa does both during thread
setup, so from the day a core linked Mesa into its guest the core could die on
any frame - SIGILL, exit 132, intermittent because it depends on whether Mesa's
threading path runs at all.

The principle worth keeping is the one in the title. A syscall the host does not
implement is not an error the guest can handle and carry on from: the guest is
killed where it stands. Every syscall a linked library might reach for is
therefore a liveness question, not a completeness one, and "we do not support
that" has to be said with a return value. sched_setaffinity is now taken and
ignored, which is the honest answer - guest threads are green threads on one
host thread, so there is nothing to pin, and the mask read back is the same one
CPU either way.

Two things made this cost days rather than minutes, and both are now fixed. The
gate swallowed the evidence, and a timeout CANCELS a job rather than failing it,
so the "upload the work dir on failure" step never ran and nothing survived to
read. A canary that fails fast produces an artifact; a gate that runs to the
timeout produces nothing at all.

## A folder has no hash, so it has to become a file (user-decided, 2026-09-12)

A project stores names and SHA1s and never paths. That is the oldest promise in
this frontend and the reason a movie travels: anyone holding files whose hashes
match can replay it, wherever those files sit on their disk. miniBox goes
further and binds every mounted file to the savestate by SHA-256.

A game that arrived as a dumped FOLDER cannot be named that way, because a
directory has no hash. A PlayStation 3 disc is a tree; so is a floppy's worth of
DOS. The natural answer - let a core open a folder - breaks the promise rather
than extending it: there is nothing to name, nothing to bind a state to, and
nothing a second person can be asked to reproduce. So the folder has to become
one file first, and the interesting requirement is not that it becomes A file
but that it becomes the SAME file on anybody's machine. The precedent the user
cited (TASEmulators/iso_maker, which drives xorriso) does exactly this.

Reproducibility here is the whole design, and it is a matter of naming every
channel through which the host can leak into the bytes and closing each one
deliberately:

- Timestamps. Fixed dates everywhere - a constant DOS date in the zip, a
  constant date in the FAT12 directory - never the file's mtime and never now.
- Mode bits. A constant external-attributes word in the zip, so a dump copied
  off a FAT drive and one copied off ext4 pack identically. The ISO carries no
  Rock Ridge, which is the other way to close the same channel: the format is
  then unable to record a mode at all, including the DIRECTORY modes that are
  easy to forget because nobody ever looks at them. That is a deliberate
  difference from the xorriso precedent, which does write Rock Ridge and has to
  be told what modes to put in it.
- Order. Entries are sorted byte-wise, never by locale, so a machine with a
  different collation does not produce a different image.
- Identifiers the format invites you to invent. FAT12's volume serial is the
  clearest case: DOS wrote the format time there, which is a timestamp wearing
  a different hat. It is zero.
- Symlinks. Collected with symlink_status and skipped, so what is written
  describes the tree rather than wherever the tree points.

Three shapes, chosen because they are the three things a core actually wants.
Stored zip, so a core can seek inside it and read what it would have read
loose, at the same size on disk. ISO 9660 with Joliet, because the disc cores
want a disc and Joliet keeps long names - built as two directory trees over one
node tree, and refusing any single file of 4 GiB or more by name rather than
truncating it, since the format cannot describe one. FAT12, a 1.44 MB floppy
with deterministic 8.3 names, refusing what does not fit rather than quietly
leaving it out.

The packing is the engine's (ce_media_make, and the hashing happens on the way
out through a streaming SHA1) and the window only chooses, shows and cancels.
That is the thin-window rule applied to something it would have been very
tempting to write in the window: every one of the channels above is a
correctness question, and correctness lives in the engine where the tests are.

Refusing is a feature in all three. An empty folder, a file too big for the
format, a floppy that will not hold the tree - each is an error with a sentence,
and a cancel leaves nothing behind rather than half an image that hashes to
something.

## The image was valid and the console refused it anyway (user-reported, 2026-09-12)

The first real use of the tool was Ultra Street Fighter IV, and the image it
produced would not boot: "RPCS3: boot failed: Invalid file or folder". The image
was a correct ISO 9660 volume with a correct Joliet tree, and the EBOOT was
where it should be. See the commit for the mechanism - sectors 0 to 15 are
reserved and meaningless to ISO 9660, and a PlayStation 3 disc keeps its
encryption region table there, which rpcs3 treats as mandatory.

Two things are worth keeping from it. The first is that "reserved" in a
container format is where the platform puts what it needs, so writing a valid
volume is not the same as writing a disc that platform will accept, and the
table is only written for an image that looks like a PS3 disc (PS3_DISC.SFB at
the root) because it would mean nothing anywhere else.

The second is about evidence, and it is the same lesson as the syscall above.
This was nearly called fixed three times on no evidence at all: the first A/B
ran both images without firmware, so both stopped before rpcs3's loader was
reached; the second had a synthetic EBOOT that is not an executable, so the
fixed image failed later for its own unrelated reason and printed the same
sentence. Only --log-trace SYS,ISO showed what differed. A generic error message
that appears for several distinct causes will happily confirm whatever you
already believe, and an A/B whose two legs stop before the code under test is
not a test.

## The exit code was the evidence all along (user-reported, 2026-09-12)

With the image booting, the same game failed the next step: "The compile stopped:
3 of 8 sessions failed without saying why". The frontend precompiles a PS3 game's
modules in several headless sessions at once, and three of eight died.

The first thing this needs recording for is a wrong turn taken confidently. The
orchestrator budgeted memory per session and spent all of what the machine
reported free, so "too many sessions for the memory" was an easy story, and a
measurement seemed to support it - 8.32 GB peak for one session. That figure was
maximum resident set size on Linux, which counts the sandbox's arena as the guest
touches it, and it is not what the machine has to find. On Windows, where this
was reported, one session peaks at 1.75 GB of commit and a 3.0 GB working set,
and eight of them together held 8.7 GB while the machine never dropped below 79
GB free of 127.5. The memory story was not just unproven, it was false.

What settled it was the thing the orchestrator had been throwing away. A session
that dies prints nothing on its way out, so its exit code is the only account of
it there is - and "without saying why" was never a limit of what could be known.
The code was 0xC0000005: an access violation. Not a kill, a crash. Windows Error
Reporting had the same thing with more detail (three BEX64 entries, same fault
offset at three different module bases, so one deterministic crash site, in
"unknown" module because a sandboxed guest is mapped memory and not a loaded
image). Every exit code now names itself, the POSIX signals and the Windows
status values both, and an unrecognised one at least reports its number. That is
the principle worth keeping: a report that says "no reason given" while holding
a reason is a bug in the report, and the fix is not more guessing.

The crash was reproduced exactly - twice on Windows, twice on Linux, one session
at a time with 93 GB free - once the condition was understood: it needs a WARM
cache. A cold session compiles its share and exits 0; a session that FETCHES a
game's objects walks into it. That is why three of eight died for the user and
one of eight for this workstation: it depends on which sessions find their work
already done, not on which modules they own.

The fault itself is in the rpcs3 core and is written up there (docs/PLAN.md, risk
2). In short: a JIT memory manager whose allocator returns `block + (pos %
c_max_size)` will, once its pointer passes the end of the region, hand out
addresses that alias code already loaded - and the next relocation writes into
somebody else's instructions. The core had lowered that bound to save arena
space; this game's main module is 87.9 MB of generated code, and a stray 4-byte
relocation turned a nop into `add %bl,(%rdi)`, a store to the first byte of the
emulated machine's execution table. The bound is back and the wrap is refused
with a sentence.

Two things in the frontend came out of it and stay. The per-session figure is now
4 GB of what is spare after 4 GB kept for the system, from the Windows
measurement rather than from an inflated one. And where every dead session died
for want of memory - and only then, which after all this means almost never - the
run halves the sessions and goes again, because too much at once is a reason to
want less rather than a reason to stop.

The last lesson is about instruments. This was found by making the machine say
where its regions are (the core prints its `vm`, `exec` and JIT bases when a
precompile starts) and by making the sandbox print the faulting instruction's
bytes and registers, not just its address (miniBox 5436126). The byte at the
faulting rip was a nop, which is impossible for a write fault, and that single
impossibility is what said the code had been modified underneath. A day of
debugging bought that line; it now prints on both platforms.

## The greenzone may use a second core; the machine never will (user-decided, 2026-09-12)

Asked for directly: emulation is single-core on purpose and must stay that way,
but state management - deltas, anchors, coarsening, spilling, prefetching - is
demanding work on the critical path that a helper thread could take. Is it
worth it?

The full arithmetic and the design are in docs/state-manager.md, "What a second
thread could take, and what it could never". What belongs here is the boundary
and the three findings that decide it.

**The boundary.** The machine runs on one core because a movie that replays is
a machine whose every step is the same step. The greenzone is not the machine -
it is a cache of where the machine has been, and it has been allowed to differ
between machines since the day it was tuned: `tuneStride` decides what to keep
from what the clock says. So a helper thread here cannot change a movie, and
the rule that keeps it that way is one line - the helper reads guest memory
through miniBox's always-RW mirror and never writes it. Every mutation of the
machine stays on the emulation thread.

**The first finding is negative, and it is the one that matters.** A capture is
about a sixth copy and five sixths page-table work: one `mprotect` per run of
pages written last frame, and the guest's own stores trapping. The second is
not work done ON the emulation thread, it IS the emulation thread. Measured
with a new bench (`tests/perf/storebench.c`), a 1 MB delta at 256 pages a frame
is 0.04 ms of walk and 0.32 ms of copy, inside a captured frame costing 1.86.
Against the real-run figures already in this log - the history is 5% of an N64
run, 11% of a Game Boy one - the offloadable share of a whole session is one to
three per cent. **Threading the steady-state capture for throughput is not
worth a thread, and anybody who proposes it for that reason has the wrong
reason.**

**The second finding is that the prize is the stalls, not the mean.** An anchor
is a whole machine copied at once: 40 to 180 ms on a 257 MB machine and 150 to
700 ms on a gigabyte one, and it happens every 600 frames - once every ten
seconds - forever. A spill is an `fwrite` plus `fflush` of a whole stretch, 7 ms
to ext4 and 57 ms to NTFS, and `evict()` is a loop that can do several in one
frame once the budget is full. Coarsening is 0.16 to 0.72 ms a frame and is
capped at 8 MB of input precisely because it is on the critical path. What a
person notices is a freeze, and all three of these are bytes moving between
buffers and files while the machine waits for no reason.

**The third finding came out of measuring rather than of the question.**
Anchors do not only stall, they thin the greenzone: `tuneStride` averages every
capture together, so one 121 ms anchor drags the mean far enough over
`kCostShare` to treble the near band's stride, which then recovers one step per
thirty captures. A significant share of frames on a heavy core are captured
sparsely because of a cost that has nothing to do with those frames. That is
fixable on its own - tune the stride on deltas, measure anchors apart - and is
worth doing whether or not any of the rest is.

The decision is to build it in that order: the I/O first (no guest memory, no
miniBox change), then coarsening, and only then the copy-on-write anchor, which
is where miniBox's deliberate single-threadedness - "a plain array + no lock is
sufficient", says tripguard.c - has to be faced honestly. The anchor is the
large prize and the real decision; the first two phases are worth having on
their own terms.

And it is threadS, not a thread (user-decided, 2026-09-12). The work divides by
what it touches and by whether anything waits for it, which is the distinction
that actually matters: **owed work** - the writer, the capture drainer, the
tidier - has already been promised, is counted against the budget, and a
barrier waits for it; **speculative work** - the prefetch that keeps a composed
prefix behind the playhead, the compression of cold deltas - is promised to
nobody and is killed where it stands by any edit, seek or memory pressure. A
speculative thread that can make owed work wait has stopped being speculative,
so it works from a snapshot, publishes with one atomic swap, and is cancelled
rather than joined. The drainer is the one role that wants a POOL rather than a
thread, because copying a quarter of a gigabyte is bound by the memory bus and
one thread does not saturate it. Everything is bounded by what the machine can
spare and every role is droppable: with the helpers off, the work happens in
line exactly as it does today, which is what makes the threaded path
verifiable at all - the synchronous path is the reference implementation.

The concurrency rule is the part that had to be designed rather than coded
into existence (user-asked, 2026-09-12). Somebody stepping back, jumping,
scrubbing, editing an input and switching a branch several times a second is
what TAS work IS, so the model must be one in which interference cannot make an
unstable state rather than one in which it happens not to. Three facts carry
it. There is no user thread - the run loop calls DoEvents itself, so every
click and hotkey is dispatched on the loop thread, between frames, in order.
Only the loop owns anything: the machine, the segments, the byte count, the
spill metadata. And helpers do not own, they CONVERT - handed an immutable
buffer, they hand back a buffer or a file range, and the loop applies the
result at one point per frame.

What follows from those is that there is no lock on the history to wait behind,
no cycle to deadlock in, and no window where a reader can want bytes that a
writer has not finished: a spill is not a spill until its completion is
applied, so the memory copy stays until the file is good. Link bodies become
reference-counted and immutable, which deletes the use-after-free that an edit
during a helper's work would otherwise be. Every piece of work carries a
generation, and anything that makes a timeline untrue bumps it, so interference
turns work in flight into garbage automatically. And a barrier is "help
finish", not "wait" - the loop joins the work rather than blocking on it, so
the worst case a user action can meet is the speed of the code that exists
today.

None of it lands on a green unit suite. The user set the bar and it is written
down beside the design: long stress runs with the budget full, seeking
backwards at every distance, re-recording back and forth with changed inputs,
cores whose machine includes disk-backed files, every core rather than the two
that are easy to drive, and the performance claim measured before and after as
a per-frame MAXIMUM rather than a mean. A history bug is silent, late and lands
somewhere else; a threaded one would be all three and unreproducible as well.

One thing is deliberately not on the list. The page faults and the
re-protection are eighty per cent of a captured frame and no thread can ever
take them. The lever that reaches those is granularity - a 2 MB huge page
faults once where 512 pages fault 512 times - at the cost of deltas 512 times
coarser, which is the trade this whole design was built to escape. It is a
different project, and mixing the two would be a good way to lose both.

## The greenzone got its threads, and the machine kept its core (2026-09-12)

The design above is now three quarters built, and what it measured is worth
recording beside what it predicted, because one prediction was wrong in an
instructive way and four defects turned up that only the bar the user set could
have found.

**What exists.** The project save and the spill writes happen on a writer
thread; an anchor is taken as a plan, its pages held read-only by miniBox and
copied on a drainer thread while the machine runs on. Coarsening is still on
the loop and is now deliberately staying there (below). Everything is droppable
- `CHIMERA_HELPERS=0`, or a thread that will not start, and the whole of it
happens in line exactly as before, which is the reference the threaded path is
tested against rather than a fallback nobody exercises.

**What it bought.** On the emulation thread, an anchor of a 256 MB machine fell
from 84.7 ms to 9.6, and of a gigabyte machine from 521 ms to 33 - eight to
fifteen times less - with the resulting state byte for byte the state the old
path would have written. A captured frame's 99th percentile improved 1.7x to
12x depending on the shape of the machine, and its worst frame up to 7.2x. A
project save of a 122 MB history went from 51 ms of freeze to returning at
once. The prediction that the steady-state MEAN would barely move was right: it
moved 1.1x to 1.5x, and anybody doing this for throughput would still have the
wrong reason.

**The measurement that made phase 3 possible** was not about copying at all. A
copy-on-write anchor has to hold every page it is about to copy, and if that
cost what the copy costs the idea is dead; it does not, because mprotect works
on RANGES. Holding 256 MB read-only measured 0.76 ms in one call and 1.08 ms in
megabyte runs, against 40 to 180 ms to copy it. One micro-bench, five minutes,
and it decided the whole design.

**The prediction that was wrong.** The design said a spill should not count as
spilled until its write landed, so that a reader could never want bytes that
were not there. Built that way, the budget would be met later than it is asked
to be - and eviction is driven by that byte count, so the history would hold
different frames threaded than in line. It is the other way round: the metadata
settles at once, exactly as before, and what pays for it is that every READER
of the file waits for the writer, and only for the range it needs. That is what
makes a step-for-step comparison between the two modes possible, and that
comparison is now a test.

**Phase 2 is not deferred, it is declined.** Moving coarsening off the loop
costs that same property - a merge that lands a frame late leaves a landing the
synchronous path had already merged away - and it buys 0.16 to 0.72 ms a frame
of a cost that is not what anybody feels. Trading the one property that makes a
threaded design checkable for a fraction of a millisecond is a bad trade, and
saying so is more useful than leaving it on a list.

**The measurement that nearly ended it, and the one line that saved it.** On a
synthetic block the copy-on-write anchor did what it promised. On a real
PlayStation 2 - a 232 MB state, anchors every twenty frames - it did nothing at
all: 117 to 141 ms on the emulation thread against 113 to 129 for the old path.
Splitting the trace line in two said why in two minutes. The plan cost eleven
milliseconds, exactly as the bench said; `std::vector::resize` was spending a
hundred and thirty zeroing the quarter-gigabyte buffer it was about to be
written over. A body is now allocated by an allocator whose `construct` does
nothing, so whoever fills the bytes pays for touching them - for an anchor, the
drainer - and the same machine's anchor costs 12 to 17 ms. The lesson is not
about allocators: a bench can measure a mechanism perfectly and the system not
at all, because the thing it does once outside its loop is the thing that costs.

That figure was itself half the story (2026-09-13). It was the frame the anchor
was taken on; the frame AFTER it waited 103 to 145 ms for the drainer, because
every capture began by finishing the plan, whether it needed the anchor's bytes
or not. A trace line for the wait found it, and the wait now happens only where
the bytes are read. The same lesson a third time: the number measured was the
one the trace printed, and the cost had moved to a line nobody had written yet.

And it is the same lesson twice in one day, from opposite ends. The rpcs3 crash
above was found by printing the bytes at the faulting instruction instead of
just its address; this was found by printing the buffer's time beside the
plan's. Both had been one missing number away the whole time, and in both cases
the missing number cost a day before it cost two minutes. What transfers is not
the fix, it is that an instrument which reports the thing you are working on
and nothing beside it will confirm whatever you already believe.

**What reaches the disk is compressed, and the decisions did not notice.** Real
machine states compress seven to a hundred and eighteen times at zstd level 1 -
a PlayStation 2 anchor from 232.9 MB to 9.95 MB - so the writer compresses each
spilled body as it writes it. The design constraint was the same one as
everywhere else in this work: the history decides with LOGICAL sizes, reserved
at once, and only the readers learn where the compressed bytes physically
landed. The same PS2 history then weighed 79.9 MB on disk instead of 2.5 GB,
and a cold restore of a spilled anchor fell from 103 ms to 59. As first built it
bought no depth, because the disk budget still counted logical bytes; counting
physical ones would make disk eviction follow the writer's timing, and that
trade was put to the user rather than made. The answer was to count compressed
bytes (user-decided, 2026-09-13), and with the same 1024 MB budget a PS2 run
that had been throwing away all but two anchors kept everything it ever spilled
in 79.7 MB, and served a seek back from disk that the raw run had to replay.
What it cost is written down beside it: threaded and in line still decide the
same frames step for step, but may drop from disk at different moments, so the
differential test now compares the disk once, after a flush. The measurement that found this
had to be taken twice, because the first one landed on the tiny boot anchor and
read a page cache rather than a disk, and both mistakes flattered the old path.

**Four defects, and what each says.** A reader and the writer shared one FILE
handle, and a FILE is one position: the loop seeking to the oldest stretch and
the writer seeking to the end interleaved, and restores came back with somebody
else's frames seven runs in twenty. Two handles fixed it, and the lesson is
that "different ranges" is not the same as "different files". The save barrier
called into a session that was already freed, which a four-thousand-frame soak
found by surviving every seek and dying on the way out - a barrier belongs to
the thing that knows it is still alive. And twice a barrier was taken before
deciding whether it was needed, or a decision was made on numbers describing a
file that had not been written yet: both turned a win into a loss, and neither
was visible as anything but a slower run.

Every one of those four came from repetition and from real cores, not from a
green suite: the FILE-position bug passed thirteen runs in twenty. That is the
whole argument for the bar.

## The open ideas, measured rather than argued (2026-09-13)

Three ideas were left over when the greenzone got its threads, and each one was
settled by a run on a real machine rather than by how good it sounded.

**A saved history is compressed, because the spill file already proved it.** A
project's history is the greenzone written out, made of the same mostly-empty
machines, and it had stayed raw. `ChimeraHistory4` is the magic followed by
exactly the old layout as one zstd stream - nothing else about the format moved,
so nothing is held twice to save or load it and the old version is still read.
A PlayStation 2 history went from 1,845 MB to 60 MB, and the save got faster, not
slower, because level 1 compresses faster than the disk writes.

**Reverse deltas stay out, and the reason is where the work would happen.** They
were removed once for costing a page copy in the fault handler on every frame,
and the question was whether hot pages - which already keep the page as the
frame found it - had made that copy free. They had not: on the PlayStation 2 two
thirds of what a frame writes lands on pages that are not hot, 2.2 MB a frame
past boot and 168 MB in the worst one. That copy cannot move to a helper. The
bytes it needs exist only until the guest's write goes through, so it is paid on
the machine's own thread or not at all - and a design whose whole premise is that
the machine's thread only runs the machine does not buy a faster gesture with
it. The number that would reopen this is a core where most written pages are hot.

**The anchor spacing is chosen by what a seek would cost, not by a table of
cores.** A stretch closes once its links weigh as much as its anchor, so a seek
costs about two anchor loads on any machine. The first version was wrong about
light machines in a way only a real run could show: a NES, a Genesis and a SNES
all took an anchor every thirty-one frames, because their anchors are smaller
than thirty frames of pages, and there a seek was already a millisecond. So the
weight only counts past 64 MB of links - about twenty milliseconds of walking -
and those cores went back to one anchor per run. The PlayStation 2 went from
99 / 103 / 135 ms seeks to 63 / 44 / 53, for about 650 MB more held in memory.
A positive spacing from a caller is still obeyed exactly. What transfers is the
floor: a rule derived from cost ratios needs an absolute threshold too, or it
optimises a cost nobody was paying.

## Opening TAStudio from a script freed the script (2026-09-13)

`client.opentasstudio()` on a bare rom crashed in `--headless`, on main, for
everybody. With no project open TAStudio starts one, starting a movie reboots the
core, and a reboot restarts the Lua console - which closed the Lua state the
calling script was still running on. The call returned into freed memory, and
the fault was a write through `L->top` holding the bytes of the string
"DrawFini": a Lua method name, sitting where the state used to be. The other
calls that can reboot (`openrom`, `openproject`, `reboot_core`) already told the
console first, so it re-injected its dependencies instead of closing the state;
`opentasstudio` never did, because opening a window was not supposed to boot
anything.

Fixing it exposed a second failure on the same gesture, on Genesis only. gpgx
declares its Mega Drive and its Mega CD both as the GEN system, the package
listed GEN twice, the registry put the core under GEN twice, and the reboot -
which forces the core by name - found "more than one matching element". A
system is listed once now, in the package and again in the registry. Both were
found only by running the gesture on real roms of two different cores: the first
fix made the NES pass and the Genesis still fail.

## A shadow a frame behind is not always a frame behind (issue #64, 2026-09-13)

Reported from outside: ares crashed on Windows running NES games, with faults at
addresses like `0x3`, `0xf5` and the start of the `ares::Famicom::cpu` object -
jumps into data. None of it reproduced from a plain run, a savestate round trip
every frame, thirty minutes of play, pressed Reset buttons, the frontend itself,
the reporter's own nightly, or rebooting the core in one process. What did was
the user's suggestion: rewind many times, at irregular patterns. A seeded Lua
stress script - record, seek back one frame or a hundred or to the start, seek
forward, throw the greenzone away, resume - found it on Windows only, identically
on the reporter's build and the current one: in nine seeks as ares throwing
`Thread::Enter()::ThreadNotFound`, and after some 1470 seeks in the reporter's
exact shape, an execute fault at `rip=0x2` and a read through `r15=0x3` inside
`ares::Famicom::CPU::step` with `rcx` holding `&ares::Famicom::cpu`. Both are a
coroutine's stack from one moment beside a heap from another.

The cause was in miniBox. A Windows stack cannot report its own writes (the
kernel pushes the exception record onto that very stack), so a stack page is
tracked by COMPARING it with a shadow. The shadow was rewritten only when a delta
was saved, and the reasoning that this was safe - "a stale shadow is a frame
behind; carrying a page too many is waste, never wrong" - fails whenever the
stale bytes happen to EQUAL the new ones, which determinism makes common. Two
paths were measured, each with a test that fails on the Windows build without
the fix: a load or a delta apply (seek back, replay, and the stack is rewritten
into exactly the old timeline's bytes), and a frame captured as an anchor rather
than a delta (the shadow still describes the frame before it). The first fix
covered only the first path; seed 31 then survived 3000 steps and seed 57 still
died at ~1470 seeks. The rule that closed both is the one hot pages already had:
when an epoch opens, every stack page's shadow is what it holds, so the
comparison at the end of the frame is its end against its start whatever
happened in between (miniBox, `stack_shadows_describe_now`). Seeds 31, 57, 73
and 91 then each survived 3000 irregular actions on Windows - 1992, 2121, 1989
and 2033 seeks.

Three things worth keeping. An argument of the form "the stale value can only
make us do extra work" has to survive the case where the stale value EQUALS the
new one, and a deterministic machine is exactly where that case is common. A
harness that does one kind of thing at a time - a single seek, a round trip
every frame, a long plain run - passes a bug that needs a SEQUENCE: clear, go to
zero, go to zero again, replay, come back, step back eight frames. And a fix that
makes the known reproduction pass is not finished until a different seed has run
past where the old one died; the first version of this one looked complete for
an hour.

The fault report changed with it, because the crash that proved the fix partial
printed `code:` and nothing more: reading instruction bytes at `rip=0x2` from
inside the handler was a second fault. It now prints every general-purpose
register first and reads code only where it is readable.

The stress found four more on the way, none of them this, each fixed after it.
After seventeen to thirty-four core reboots in one process on Windows the sandbox
could not create its memory block again ("failed to create memory block"): a
freed block released its mirror - a view of the block's section - with
VirtualFree, which cannot release a view and failed silently, so every reboot
leaked a whole arena; it is now unmapped as a view, and the reboot-heavy stress
survives 156 reboots. TAStudio could not open a movie whose log key names
controls the machine does not currently have (a project recorded with a second
controller and reopened without it): a mnemonic is answered from the cache, else
from the system's lookup for a name the machine lacks, and every reader of the
cache goes through that. A project's game record took the package's FIRST
system, so an ares NES project said
N64 wherever that was read; it now takes the system the created emulator reports.
And headless mode waited forever on the prompt to locate a project's missing
file; it now names the missing files and exits 64, like every other dialog a
headless run cannot answer.

## A save in the background still stopped the run (nss102, 2026-09-13)

A Ruffle project of 8039 frames froze "for about ten seconds, every so often,
worse as the movie grew". Saving a project already happens off the machine's
thread: the history is snapshotted and written as one job on the history writer
(`saveToLater`). Measured headless on Windows with the writer's waits traced,
the save wrote 4.35 GB in 46 s - and the frame about five seconds after it
started took 42.4 s. That frame spilled: a Ruffle stretch is about 300 MB, the
writer's queue is capped at 64 MB, and a spill over the cap waits for the
writer to catch up - which, with a save in the queue, is the whole save. The
autosave fires every thirty minutes, and the history it writes grows with the
run until the budgets are full, which is the "worse as the movie grew".

While a save is pending the history now neither spills nor compacts (both would
wait for the writer); the memory budget is kept by thinning, which is what a
history with nowhere to spill has always done, and a refused spill is not
reported as a full disk. The same run on Windows with the fix: the save still
wrote its 4.35 GB, no frame took a second or more, the slowest after the save
took 0.29 s - an ordinary spill frame - and the 7000 frames finished in 238 s
rather than 287. A test asks the same question without a disk: 200 captures
made while a save is pending, none of which may queue a write; it fails without
the fix.

The general lesson is the one the helper threads were built on and this broke
quietly: moving a job off the critical path is only half of it. Anything that
WAITS for that helper - a queue cap, a drain before a read - puts the job back
on the path, and the biggest job the helper ever gets decides how long.

The same measurement found the other half of the report, which is not fixed:
a capture drops every stored frame after it (`captureOnce` calls
`invalidateAfter(frame - 1)`), so going back and playing forward over unchanged
input throws away the greenzone ahead, and returning to the end replays all of
it - 300 frames in 10 s, 1000 in 31 s, 3000 in 99 s. Every TAStudio edit already
invalidates explicitly; the engine's own record path relies on the capture
doing it. Whether a replay may keep what is ahead is the user's decision.

## A replay changes nothing (user-decided, 2026-09-13)

The rule the user set: a non-modifying replay does not affect the greenzone.

It did. Every capture dropped every stored frame after it (`captureOnce` called
`invalidateAfter(frame - 1)`), on the reasoning that a capture describes the
frame we stand on and anything after it is a timeline that no longer happens.
That is true of recording over an entry and false of playing one back. On
nss102 a seek back followed by play threw the greenzone ahead away, and the way
back to the end was emulated frame by frame: 300 frames in 10 s, 1000 in 31 s,
3000 in 99 s - the "stall" a user sees on every click forward after looking
back, and longer the further back they looked.

Now a capture of a frame the history already reaches past stores nothing and
drops nothing, and lets the epoch go so the next delta is measured from where
the machine stands. What changes the timeline says so itself, before the frame
is played:

- every TAStudio edit, recording included, already did;
- a branch loads through the same call, at the point where the logs diverge;
- loading a savestate into a project now also counts a log that is merely
  longer or shorter as diverging where the shorter one ends;
- the engine's own movie (`ce_session_movie_advance`) invalidates after the
  current frame whenever the input is not the log's - recording, or past the
  log's end - and `ce_session_movie_load` invalidates after the first entry
  the old and new logs disagree on.

What a replay does not do is fill in what the bands thinned: the history is
append-only, so frames the far band gave up stay given up until the playhead
passes the end again. Making a branch mid-history no longer stores its frame
either - and no longer drops everything after it, which is what that capture
used to do. A branch carries its own state, so nothing reaches for it.

Measured on nss102 on Windows with the change, the same script as before: back
1, 2, 5, 10, 30, 100, 300, 1000 and 3000 frames and return to the end each time.
Every return took a second or less and at most 17 frames of emulation - the
stride back to the stored frame before the end - where the returns from 300,
1000 and 3000 frames back had taken 10, 31 and 99 s.

## Two versions of a core are one core to its firmware (issue #60, 2026-09-13)

Installing a second xemu beside the first listed its firmware twice in Config >
Firmware, with nothing to tell the two sets apart. The survey made one group per
package found on disk. Firmware is remembered by core name, never by package, so
the two groups were always the same files - the second was only noise. The
survey now makes one group per core: every declaration any installed version
makes, once each by id and dump, so a firmware only the newer version asks for
is still asked for. That is the reporter's own preference - more firmware
rather than fewer - and it needs no version shown, because nothing about a
chosen file depends on which version asked.

## A cache is checked against the build that runs (issue #63, 2026-09-13)

An autosaved xemu project, reopened, could not load its branches: every attempt
threw "memory block load failed", and miniBox's diagnostics said "this state was
made by another machine". The cache beside a project (greenzone, branch states)
is only used when the machine identity it records matches the project's - and
that identity took the core's hash from the project's PIN. A project opened on
another build of its core - accepted at the "not the project's core build"
prompt, or simply the version that registered first when two are installed,
which is what the same reporter had (issue #60) - still pinned the old hash
until saved, so the old build's cache passed the check and its branch states
reached a sandbox that rightly refused them.

The identity now names the build that runs when it is known, so a cache made by
another build is set aside with the note every other mismatch gets, and the
greenzone starts empty. And a branch whose state is refused anyway no longer
throws out of TAStudio: its input is already loaded, so the refused state is let
go and the branch's frame is reached by replay, with a message saying why.

Still open: with two builds of one core installed, the registry keeps whichever
registered first, so a project cannot choose the build it pins without the other
being removed. The prompt says the builds differ; it cannot yet offer the right
one.

## Several builds of one core, and the one a project pins (user-decided, 2026-09-13)

Issue #63's still-open half, as the user put it: several versions of a core may be
installed and selectable, as long as they are not the exact same package. The
registry used to keep one factory per core name and silently drop the next, so
a project pinned to an older xemu opened on whichever xemu this session had
loaded first - and its saved states were refused.

A core package is now registered beside another of the same name whenever the
two are different bytes, and a project boots the exact build it pins whenever
that build is installed, loading it next to the others if need be. Which build
runs otherwise is one rule, used by every place that
turns a core name into a machine - project boot, the cache's machine identity,
a bare rom, a movie's forced core, the firmware in use: the pinned build if it
is there, else the one the user chose, else the most recently installed. The
user decided the middle term: opening a package with File > Open Core, or
installing a version in the core manager, is choosing it; a project loading the build it pins is not, and
records nothing. The "not the project's core build" prompt now only appears
when the pinned build is genuinely not installed.

One smaller truth came with it: "which package made this machine" is
remembered for each machine, and the movie header and firmware record read
that.

Proved end to end headless: with a byte-different copy of the synth core loaded
first (and so chosen), a project pinned to the original booted once, played its
movie to an OK dump, and never asked; the same project pinned to a build that is
not installed stopped at the prompt (exit 64). The chosen build in the config
was the copy, untouched by the project's own load.

## A crash takes nothing that was entered (user-decided, 2026-09-13)

The user asked for crashes not to be fatal, and above all for the work in
progress - the inputs - never to be lost, even when a crash cannot be survived.

The crash that prompted it could not have been caught. nss102 died inside the
NVIDIA OpenGL driver with 0xc0000409, a fast-fail: Windows ends the process on
the spot and runs no handler. A design that saves the work
"on the way down" saves nothing in exactly the crashes that matter most, and a
killed process or a power cut is no kinder. So the inputs never wait for a
crash: they are on disk before anything can go wrong.

The input log is the engine's (`ce_movie_log`), so the engine journals it. A
journal opened on a log first writes the whole log, then appends one line per
change - add, set, insert, remove, truncate, clear, key, or a whole image for a
parse or an assign - and hands each line to the operating system with a flush
the moment it is made, syncing to the disk at most once a second. Bytes the OS
holds outlive the process whatever kills it; a power cut costs at most that
second. The journal alone rebuilds the log; a line the crash cut short is not
replayed; rewriting the journal goes through a fresh file and a rename, so a
crash mid-rewrite still leaves a whole one.

The frontend keeps the rest (`ProjectRecovery`, per project, beside its cache):
a session file naming the process, a snapshot of the whole project - markers,
branches, settings, written through the backup path, so no greenzone and no
change to what counts as saved - rewritten every few seconds while there is
unsaved work, and the journal restarted with each snapshot to keep it short.
A branch load replaces the movie's log object, and the journal follows it.
Closing the project removes it all.

Opening a project whose recovery files outlived the process that wrote them
(the process is gone, or is another process with that id) rebuilds the work
before anything else happens: the newer of the snapshot and the saved project
file is the base, the journal's inputs replace its log, and the result is
written into the backups folder as `<name>.recovered <time>.chimeraProject` -
never over the project. Then it asks: yes swaps that content in, unsaved, and a
save writes it to the project; no opens the project as last saved and the copy
stays. A headless run never asks; it says where the copy is.

What can be caught is survived. An error on the UI thread, or in a frame step
(the core, the movie, a tool), keeps the work (a snapshot on the spot), pauses
emulation and says so - with the work's state in the message - and the session
goes on; a burst of them is told a few times and then only on the status line,
so recovering cannot become an endless run of dialogs. An error on another
thread ends the process whatever a handler does, so that handler only keeps the
work. Headless runs still fail loudly, so a gate cannot pass by swallowing one.

Not done, on purpose: no native last-chance handler writes anything. Code that
runs inside a process whose memory may be corrupt is the least trustworthy code
there is, and there is nothing left for it to save - the inputs are already on
disk.

## A GPU core's word that its states survive is not taken (2026-09-13)

Reopening nss102 (Ruffle, `opengl-hw`) crashed Chimera inside the NVIDIA OpenGL
driver - `nvoglv64.dll`, 0xc0000409, a fast-fail - after a long load and a black
screen. It reproduced headless on the same GTX 1060 from a copy of the project
and its saved greenzone, at the same fault offset, on the first frames after
restoring frame 21 from the history the previous session saved. The build from
before today's replay change ran the same steps to frame 300.

The difference was not a bug introduced but one uncovered. Ruffle declares
`video.gpuStatesSurviveTheContext`, so its greenzone was saved and loaded across
sessions - but until a replay stopped discarding the frames ahead, the first
capture of every session threw away everything loaded after frame 0, so a Ruffle
state from another process was never actually run. Keeping the greenzone
(user-decided, the same day) made the frontend restore one, and the renderer's
objects did not come back with it: the state's own GL audit saw nothing
orphaned, and the driver still aborted the process.

So the claim is no longer taken on trust: a machine a GPU drew keeps its states
for its own session, whatever the core declares, exactly as GPU cores without
the claim always have. Rewind, branches and the greenzone within a session are
unchanged; across a restart they are recomputed by replay. The declaration is
still written into the project, because it is what the core said. Making it
true is Ruffle's to do, and a core that proves it - a restored state drawn to
the right picture in a fresh process - can have it honored again.

## Markers and branches are journaled too, and unsaved work is locked (user-decided, 2026-09-13)

Two follow-ups the user set for recovery. First, the markers and branches are
journaled as they change, like the inputs, instead of reaching the disk only in
the snapshot every few seconds. Second, the journal is a cache manager object,
locked by default.

The journal stays one file and stays the engine's. The engine carries the
frontend's own records without interpreting them: an image of every marker and
branch goes into the same fresh file as the input log whenever the journal is
rewritten, so no crash can separate them, and each later change is appended
with the same flush and the same once-a-second sync as an input. Three records
say everything: `M` the markers a person placed (the run's own are derived),
`B` one branch whole, keyed by its Uuid, and `O` the branch order - the list IS
those ids, so a removal needs no record of its own. A branch's input log is in
its record, but only when that branch changes. Changes are found by comparing
cheap signatures on every pass of the main loop rather than by events, because
a marker's message and a branch's text are plain properties anybody can set.
Recovery replays the inputs, then takes the markers, branches and order the
records end on; a journal with no such records leaves the base project's.

Making it a cache entry uncovered a real hole. The recovery folder lived inside
the project's cache, and a project's cache entry is the whole folder - so the
auto-clean, which takes unlocked greenzones oldest first, or anyone removing a
greenzone by hand, would have deleted unrecovered work with it. Recovery now has
its own root, `<data>/Recovery/<id>`, and its own kind ("Unsaved work"). The lock book already started every kind but greenzones locked;
unsaved work joins them for the opposite reason from the small caches - it is
the one entry whose loss is work rather than time. It is in use while any
session that owns it is running, and a folder where an earlier build left it is
moved on first use.

One slip worth a line: a hand-typed separator went into the source as a raw
control character, six times over. It compiled and ran; it is now an escape.

## A crash no handler sees still says what it was (user-decided, 2026-09-13)

The crash that started the recovery work was a graphics driver's fast fail:
`__fastfail` ends the process without running one exception handler or one
cleanup. The work survives that (the journal), but nothing
said why it had been needed - the Windows event log named a module and an
offset, and the minidump `LocalDumps` happens to be set to keep was found by
chance, outside anything Chimera knows about.

What was ruled out: a handler in the process (never runs, and would be code in a
process that has just declared itself corrupt), and a watcher process attached
as a debugger (it would see every first-chance fault, and miniBox takes page
faults by design - a debugger in the loop makes the sandbox crawl).

What runs is Windows Error Reporting, in its own process, and WER has a place
for exactly this: a runtime exception helper module, registered by the process
(`WerRegisterRuntimeExceptionModule`) and allowed by a per-user registry value,
so no administrator. It is loaded into WerFault.exe with a handle to the dead
process. Proven on the test box before anything was built on it: a C crasher
that fast-fails (`int 0x29`) and one that dereferences null both left a note
and a minidump, and the stack walk resolved through WerFault's own dbghelp.

So `chimera_crash.dll` (source/crash, built by meson for Windows only) writes a
note and a minidump into `<data>/Crashes`. It reads a fixed block in Chimera's
memory - the folder, the frame, lines about the session - through the process
handle, clamps everything it reads, and claims nothing, so Windows' own record
of the crash is unchanged. The note is plain text and flushed as it goes, so a
dump that fails or stalls cannot take the note with it. The block is never
freed: it is read after the process is gone. Registration failing costs the
note, never the start.

A note is matched to a recovery folder by process id AND time (ids are reused),
and the reopen prompt says what ended the session. The minidumps are for a
debugger, not for a person; the one line in the prompt is for the person.

The first question a driver crash raises is what the renderer had just asked of
it, and a stack inside nvoglv64.dll without symbols does not say. So the GPU
bridge keeps a flight recorder (docs/gpu-bridge.md, "The flight recorder") and
the note carries its last calls: always on, because the crash that needs it is
never the run somebody switched a trace on for, and read from outside, because
nothing inside runs after a fast fail.

## A GPU core's word is withdrawn again, after it bricked a project (user-decided, 2026-09-14)

35be468 honored `video.gpuStatesSurviveTheContext` again the morning after it was
withdrawn, on measurements that were all true: a Ruffle state reloaded in a
fresh process, one restored twenty-one frames deep, and one restored 7768 frames
deep out of a 1.07 GB history, each drawing the right picture. By the evening
the person could not open their project at all.

The project's history had been written by a session that went on to die of
guest heap corruption (musl's footer check in `__bin_chunk`). Opening the
project, TAStudio went to its remembered frame, 8915, restored the nearest state,
8890, from that history, and the first draw after it killed the process inside
the NVIDIA driver: generated driver code indexing a table with a garbage count
that came from the restored guest. Every open, the same frame. It reproduced on
the same GTX 1060 from an isolated copy of the build and a copy of the cache:
with `history.bin` the process dies in the same driver function with the same
register fingerprint; without it the project opens and draws. A clean session's
history, reopened in a new process, restored and landed without incident. So
what 35be468 measured is still true, and it could not have seen this: it never
restored a state from a session that was already going wrong.

That asymmetry decides it. A greenzone that starts empty costs a replay; a
history that crashes on open costs the project until someone knows which file to
rename. And the hardware path does not reproduce itself run to run - two
identical runs to frame 60 differ in about a megabyte of guest heap - so a state
carried into another process is not the machine that process would have made.

So a machine a GPU drew keeps its states for its own session again, exactly as
the section above had it. A history already on disk from the day the claim was
honored is not loaded when the project opens - it opens cold and says why - and
the next save removes it. Honoring the claim again needs the missing half first:
a hardware path that reproduces itself, and a way to tell a history from a
session that ended in a crash.

## A greenzone is trusted when the session that wrote it finished (user-decided, 2026-09-16)

The 2026-09-14 withdrawal above is narrowed, not reversed. It refused every
GPU-drawn greenzone because one bricked a project - but the asymmetry it
identified was never the renderer. The history that bricked nss102 was written
by a session that went on to die of guest heap corruption; the same entry
records that a clean session's history, reopened in a new process, restored and
landed without incident.

Measured on the real project, 2026-09-16, GTX 1060, the released Ruffle
54c45bae replaying the project's own 8916-frame input log: a 626 MB greenzone
written by one process and reloaded by another loaded in 2.4 s, rebuilt the GL
context, and drew frame 8915 PIXEL FOR PIXEL identical to the straight run - with
a control at frame 8815 differing by 76,129 bytes, so the comparison could tell
frames apart. No guest died in any run, and no minibox-diag.log was written. Five
synthetic fresh-process reloads with 66 rewinds between them behaved the same.

So the gate is the shutdown, not the renderer. `ProjectRecovery` already knows:
`End(clean: true)` deletes the recovery folder, so "no folder" is a session that
finished. `LastSessionEndedCleanly` says so explicitly rather than leaving
callers to read a missing `Leftover` as clean - which it is not, because
`FindUnfinishedIn` also returns none when another Chimera has the project open
and when a folder holds no work at all.

What still does not travel is a BRANCH's machine: it rides inside the project
file, which people hand to each other and open on other PCs, and a state a GPU
drew is only good where that GPU is. That stays refused for a GPU-drawn machine,
and is now refused after an unclean shutdown as well.

The cost of being wrong is unchanged and still asymmetric - a greenzone that
starts empty costs a replay; a history that crashes on open costs the project -
which is why the gate refuses on doubt rather than on evidence of harm.

## A greenzone lives in memory, and the disk is for saving (user-decided, 2026-09-15)

A greenzone used to spill: once it outgrew its memory budget, the oldest
stretches were written into the project's cache directory, up to a disk budget
of ten gigabytes (2026-09-10), and a helper thread did the writing so the run
never waited on it (2026-09-12). It extended a greenzone past what the machine
could hold. It also meant that a person working in TAStudio was writing
gigabytes to their drive, continuously, for as long as they worked - and a
single session on nss102 left spill files of four and five gigabytes in its
cache. That is a cost paid by the SSD, in wear, for a convenience nobody sees.

So Chimera keeps a greenzone in memory only. It no longer gives the history a
directory to spill into, so a history over its budget thins its far band and
then drops the oldest of it - which costs replaying, never work. The disk is
written when a project is saved, and that is the moment somebody asked for it.
The disk budget is gone from the settings and from the per-project budgets, and
TAStudio's note about a greenzone that could not reach the disk went with it,
since it can no longer happen.

The engine keeps the capability. Spilling is tested there, `chimera-run --spill`
and the benchmarks still use it, and it costs Chimera nothing it does not turn on.
What changed is the frontend's choice, not what the engine can do.

Two cleanups belong to the decision. Spill files an earlier build left behind are
deleted when their project is next opened: the engine only ever swept them when
it was given a spill directory, so nothing else would remove them. And a
`config.ini` or per-project budget file that still names a disk budget loads
without complaint; the key is ignored and the next save drops it.

Not changed, and deliberately left for a separate decision: crash recovery still
writes while a project has unsaved work (a snapshot of the project every few
seconds, and every input change to a journal), because that is the promise that
entered work survives a crash; and TAStudio's autosave is a project save on a
timer, and writes the history like any other save.

## A core that stops is a question, not a crash (user-decided, 2026-09-15)

A core's machine dies in ways that are no fault of Chimera: it aborts (a Rust
panic, a failed allocation, an assertion - issue #43 met one as "unimplemented
syscall 200"), it halts on finding its own heap corrupt, it follows a wild
pointer, it exits, it deadlocks, it asks for something the sandbox does not
provide. Each of those used to take the whole frontend with it.

miniBox now hands control back instead (its "A guest that dies"): the call into
the guest returns, the machine is marked dead with a one-line reason - the core's
own last words when it wrote any - and it runs nothing until a state is loaded,
which revives it. Only a fault in host code, which cannot be trusted, stays fatal.

The engine reports it as an error on the frame (`ce_session_guest_death`, and -1
from a frame advance, a movie advance or a seek) and records nothing for a frame
that never happened. The frontend raises `CoreStoppedException`, pauses, keeps the
work, and asks what to do next. Every answer keeps the inputs, branches and
markers:

1. Go back to the latest safe point - the newest greenzone frame at or before the
   one the machine died on, named by number. A state load, so the machine lives
   again.
2. Restart the machine and run again from frame 0 - the greenzone cleared back to
   its first frame and the machine put back on it. Not a reboot: a reboot reloads
   the project from disk, and would lose what was not saved.
3. Save the project's inputs without the greenzone, and close. A history from a
   run whose machine died is not one to trust; the file an earlier save wrote goes
   too.
4. Close without saving, after asking. The recovery journal is ended without its
   clean-up, so the next open finds the work and offers it back.

A headless run has nobody to ask: it prints the reason, keeps the recovery
journal, and ends the ordinary way with exit code 65.

The stop reaches the frontend as a STATE, set by the frame advance that did not
run, and the window asks its question from there.

The synth core dies on cue for the tests (all eight buttons: an abort; all but
Up: a wild pointer), and the witness checks both, twice: chimera-run stops with
the reason, and headless Chimera exits 65 with it, without a native crash, with
the journal left behind.

## A restore is a new context to a GPU core (user-decided, 2026-09-15)

Issue #43 (xemu, FlatOut 2: "crashes at some point, while seeking or autosaving")
was not a missing syscall and not the greenzone. The reporter's project, on the
reporter's build and on the current one, died on the same step of a seeded
TAStudio stress every time, three minutes in: an insert behind the playhead, a
restore to frame 272, a replay. xemu's renderer asserted
(`glCheckFramebufferStatus == GL_FRAMEBUFFER_COMPLETE`, surface.c) and aborted.

The restored state said texture 56 was a 512x512 depth buffer. It had been
something else since: the frames after 272 reallocated it. A whole-machine state
brings back a renderer's IDEA of its GL objects - the surface cache, which name
holds what - but not the objects, which live in the driver and stay as the
future left them. Same session, same names, so nothing was ever dead
(`CHIMERA_GL_STATEAUDIT`: 0 deleted, 0 reissued) and nothing told the renderer;
the context id, the one signal a bridged core rebuilds on, only moved per
session. docs/gpu-bridge.md said "rewind and branches within a session are
untouched - the objects are still there". The objects were; what they held was
not. Upstream xemu has the protocol for its own savestates - download the
surfaces into VRAM before saving, flush the caches after loading - and a
sandbox snapshot runs neither.

So every state load moves the id (`ce_gl_state_loaded`), and a GPU core treats
a restore exactly as it treats a reopen: it builds its objects again from
emulated memory. No core changed; every one of them (dolphin, flycast, pcsx2,
rpcs3, ruffle, xemu) already rebuilds on a moved id, and the user chose one rule
for all of them over a per-core flush. `CHIMERA_GL_KEEP_OBJECTS_ON_LOAD` puts
the old behaviour back for A/B.

Measured on the GTX 1060: the stress that died at step 13 on both builds ran its
whole 20 minutes with the id moving - 212 steps, no stop.

Making every core rebuild on every rewind exposed a bridge bug the rare reopen
had hidden. The buffer pool keeps a deleted buffer's name for reuse rather than
deleting it, assuming the guest will respecify it with `glBufferData`. Dolphin's
stream buffers are `glBufferStorage` - immutable, persistently mapped - and its
rebuild deletes them. A recycled name came back still mapped or with its storage
fixed, the new buffer's storage and map failed, and Dolphin's vertex loader
wrote to address 0. The rule: recycling a name must behave like a real delete,
or it is a different operation. A buffer that is mapped or immutable is never
pooled, and a target the pool cannot identify turns it off. It took a trace
(`CHIMERA_GL_POOL_TRACE`) to find the last gap - the texel buffer on
`GL_TEXTURE_BUFFER` - after two fixes reasoned from the code had each missed it.

## A branch is its input, and a stack's leftovers are not the machine (2026-09-15)

Issue #80: branches in a reopened project either would not load or replayed to
their frame. Both messages came from TAStudio.LoadBranch, and they were two
different problems.

"This branch has no saved state beside the project; it cannot be loaded." A
branch's input lives in the project; its state and picture are cache beside it,
and a cache that was lost or set aside leaves the input alone. That is still the
branch. It now loads the input and replays to the branch's frame, the way a
refused state already did (ReplayToBranchFrame, which also tells a Lua
onbranchload watcher). Checked headless on a PPSSPP project with a branch at 60
and no state: the branch loads, the run reaches 60, the callback fires.

"This branch's saved state was made by a different machine; replaying to its
frame instead." That is the right answer when the core build changed. It was
also the answer on Windows when nothing had changed at all. miniBox names a
machine by a hash of its sealed baseline, and a state from another hash is
refused. On Windows a stack page written before the seal keeps a snapshot -
nothing reports a stack's writes there, so they are found by comparing - and
the hash took that snapshot's bytes. What sits on a stack at seal is whatever
the boot left below the stack pointer. For PPSSPP that was timer readings,
different in every process, so every state was refused in the next session.

Proved with `chimera-run` and PPSSPP on Beta Bloc, one package throughout: a
state saved in one process loaded in a second on Linux and was refused in a
second on Windows. Dumping both sealed baselines (miniBox `MB_SEAL_DUMP`) found
341,677 pages, 2 different, both stack snapshots. The hash now takes such a page
by its tag, as it always has on Linux, so Linux hashes and caches are unchanged.
After it a Windows state loads in a new process and continues exactly: RAM and
VRAM after the load match a straight run to the same frame. miniBox's
`test_stack_leftovers_are_not_identity` fails against the old hash on the
cross-built Windows run and passes against the new one.

## A recycled buffer name has one owner, and a second delete frees nothing (2026-09-15)

After the #43 change a Ruffle project's picture broke on its first rewind. With
the rebuild working, a same-session rewind still came back with its background
missing, and `CHIMERA_GL_CHECK` showed a run of glBufferSubData refused with
GL_INVALID_VALUE after every rebuild. With `CHIMERA_GL_NO_POOL` the same rewind
was pixel-exact. The pool was handing one buffer to two owners.

The bridge keeps a guest's deleted buffer names and serves them to later gens
(bufferPool()). It pooled any delete of a name it had made, and a name already
in the pool still counted as made - so deleting it again pushed it a second
time, and two gens were served the same buffer. The second owner's glBufferData
shrank the first owner's storage, and every upload after that was refused.
Deleting a deleted name is legal GL, and the driver ignores it. After a restore
it is ordinary: the restored renderer lets go of handles whose buffers the
frames after the restored one had already deleted.

A name in the pool is now marked, and a delete of a marked name does nothing,
as the driver would. The bookkeeping moved into gl_buffer_pool.h, apart from the
GL calls, and `test_gl_buffer_pool` checks it without a driver: a double delete
serves the name once, and mapped, immutable, foreign and overflow deletes still
reach the driver. On the GTX 1060 a Ruffle project rewound three times to frame
300 and replayed to 600 draws the same 220,400 pixels as a straight run, with no
GL error, pool on.

The Ruffle core had its own half of this: old handles dropped lazily after the
rebuild deleted numbers the new backend had been handed. That is fixed in the
core (its gl-map.cpp keeps the new backend off the old numbers), because only
the guest knows which generation a name belongs to.

## The greenzone stays close to the playhead, and how close is a setting (user-decided, 2026-09-15)

Reported while editing a Ruffle project: the greenzone near the last input got
sparser and sparser as the run went on. The near band's stride tuner thins the
frames behind the playhead when storing every one costs more than 15% of the
run, and nothing held it back below one frame in 32. A Ruffle frame is dear to
store, so it climbed there within a couple of thousand frames and stayed - and
because the frames further back had been captured before it climbed, the history
was densest where nobody was working and sparsest where somebody was.

The trade is real, and measured on the user's project on a GTX 1060 over 2500
frames: every frame 71 s, one in 4 41 s, one in 8 32 s, one in 32 22 s, no
history 12-18 s. A first attempt that kept a sparser band only when the measured
capture share fell was wrong about the cost - a stored frame also slows the
emulation itself, which that share never sees - and ran 2.6 times slower; it was
thrown away before it shipped.

The user chose a cap of one in four, as a setting beside the memory budget: for
every project, and per project in the project's cache like the budget, because
how dense a greenzone can afford to be is a fact about the machine the work is
done on, not about the movie. The tuner still thins when a frame is dear, but a
rewind right behind the playhead replays at most three frames. The measurements
and the misleading share are in docs/state-manager.md, "How close the near band
stays".

## A greenzone keeps everything until its budget is full, then thins to a doubling shape (user-decided, 2026-09-15)

Reported on New Star Soccer: a greenzone given 16 GB used 3 GB, and rewinds of a
few thousand frames replayed for a long time. The history was thinning by
distance whatever the budget - one frame in 1200 beyond about 2000 frames back -
and, once a budget did fill, deleting from the oldest end without regard to
spacing.

The user's rule replaced both: discard nothing until the budget is full; then
keep a near band at most 4 frames apart and bands behind it whose spacing
doubles - 8, 16, 32, 64 ... - as many as the run is long. Two refinements came in
review. Measured back from the frontier (the newest stored frame), because
thinning cannot be undone and a band centred on the playhead would thin the work
somebody scrolled away from. And 32 snapshots a band is a goal, not a quota -
"heavier cores won't even have the budget for 32 entries in total" - so the band
holding the most gives a frame up first, the budget is spread round robin across
the bands, and a band's last snapshot is never removed.

Two things the first cut got wrong, both caught by the unit test before any
real core ran it. The merge caps (8 MB, and never bigger than the anchor) existed
for distance coarsening, and applied to budget thinning they left a single long
stretch stuck over its budget, because a frame in the middle of a stretch can
only go by composition; thinning now ignores them and bounds its own work per
call. And a history that spills to disk must keep the stretch being written
whole, or settling on disk finds merged links it cannot compose.

A third, found the same way: frame 0 and the frontier had been counted as their
bands' members, so the frontier was always the near band's "last" snapshot and
the frame before it always went; under a starved budget nothing aged into a
farther band and the history collapsed to those two frames. They are kept on
their own account now, and the tail stays exponential however small the budget.

Frame 0, pins and the frontier are never given up; if nothing may go the budget
is missed rather than a band emptied. The design is docs/state-manager.md, "The
policy: everything until the budget is full, then a doubling shape";
greenzone_shape.h and test_greenzone_shape hold the bands.

## A save records the core that ran (user-reported, 2026-09-16)

Opening a project on a different core build already does the right things: it
warns, and it starts the greenzone empty because the cached states were made by
another machine. What it did not do was write the new build down. The project
was saved with the pin it was created with - name, version and package hash of a
core nobody was running - so the next open warned again, and the one after that.

A save now records the core that ran, when it can be named exactly: the pin is a
package, so only a loaded package may replace it, and the name, the version and
the package SHA1 travel together (a name from one build beside a hash from
another would describe a machine that never existed). A movie with no core
headers and an emulator that came from no package - a test's fake - still leaves
the pin alone, which is what keeps a project openable at all.

The Chimera version was already rewritten by every save, with the version that
created the project kept as `OriginalEmuVersion`; the core now follows the same
rule.

## A game's compiled code is filed under the game (user-decided, 2026-09-17)

Compiled code used to live in one directory per core and per package version:
`CompiledCode/<core>/<build>/`, with a per-game manifest inside it. That layout
bought one real thing - a rebuilt core could not read the objects an older build
compiled, because they were filed somewhere else - and paid for it with
directories that never went away. On the machine this was decided on there were
five of them, 231 MB, one per local core build, and nothing said which was still
wanted. One of the five had no manifest at all: 39 MB no tool could name and no
person could find.

A game's code is now filed under the game's own SHA1 and nothing else:
`Cache/PrecompiledCode/<game sha1>/`. Neither the core's name nor the package
version is in the path, so a game has ONE directory however often the core is
rebuilt. What the old layout guaranteed by separation is now guaranteed by
evidence: the manifest records which core and which package build produced the
objects, and a manifest that cannot vouch for the build now running is not
believed - the game compiles again. A rebuild costs minutes; a directory that
accumulates forever costs them every time, and silently.

Games share no objects. Each keeps its own copy of everything it needs,
firmware libraries included, even where two games would compile the same
library. Measured before deciding: the shared firmware set was 12 MB against a
game's own 47 MB. So the duplication is about a quarter again of a game's size,
and what it buys is that a row in a window is one directory - removable whole,
with nothing else depending on what is inside it, and no question of whether
some other game still needs a file before it can go.

That is what makes the removal safe to offer per game. `Tools > Pre-Compiled
Modules...` (it was under Config for its first day; the user moved it beside the
other managers, below Reproducible Media Maker, 2026-09-17) lists what is there - the game, the core that compiled it, when,
how many modules, what it weighs - and takes one away at a time. It replaced
"Clear Compiled Code", which could only empty everything for a whole core, and
it is the only place the store is emptied; filling it is still the wizard's,
where a project is not created until its game is compiled. Compiled code left
the Cache Manager with it: two views of one store, at two granularities, meant
the coarser one described a layout that no longer exists.

A directory nothing can name is still listed. A compile that stopped half way
leaves objects and no manifest, and that is precisely the case worth showing:
unnameable leftovers are the ones nobody would otherwise find. What the previous
layout left is offered the same way, as one row that says what it is, so the
231 MB is something a person can see and reclaim rather than bytes that simply
stay.

## The game is hashed when it is picked, not when it is needed (user-decided, 2026-09-17)

The wizard's compile step looks a game up by its hash to say what is already
compiled, and looking it up is the first thing it does. It used to hash the game
right there, on the UI thread, while building the page: a silent freeze with no
progress and no cancel, at the moment somebody arrived at the step. A disc image
is gigabytes - the game this was found on is 5.1 GB, and one in the same folder
is 19 GB.

Worse, it was the second hashing of the same bytes. Create hashes every file the
project takes, through the engine, behind a progress dialog that says which file
it is on; the compile step had its own private `SHA1.Create()` and its own idea
of what a file's identity was.

The game is now hashed when it is PICKED, through the engine's hasher, with the
wait shown where a wait is expected - somebody has just chosen a file. The
engine answers repeat calls from a cache keyed by path, size and mtime, so
Create's hashing of the same file costs a lookup, and there is one notion of a
file's identity rather than two. It is an optimisation and never a duty: a pick
that fails to hash is a pick the compile step hashes itself, exactly as before.

## A restore is a new context, unless the core says it is not (user-decided, 2026-09-17)

Every state load moves the bridge's context id, so every bridged renderer builds
its GL objects again (issue #43, 2026-09-15). That was made universal on
purpose: a restore puts back a renderer's IDEA of its objects but not the
objects, which stayed as the frames after the restored one left them. xemu
attached a texture the driver had since given to something else and asserted;
FlatOut 2 died at step 13 on every build without this, and ran 212 steps with
it.

For RPCS3 the same rebuild is the damage rather than the repair. Measured on the
GTX 1060 with Prince of Persia (BLUS30214): one rewind with the rebuild leaves
the picture 100% near-black, ONE colour, while the machine rewinds and plays on
correctly; the same rewind with the rebuild skipped reproduces a straight run
BYTE FOR BYTE. Drawing 1, 5, 30 or 100 frames before the capture changes
nothing, so this is not the warm-up problem PCSX2 had. Three rewinds and the RSX
thread aborts inside the texture cache the rebuild emptied, taking all 26
threads with it.

So the frontend was asking one question where there are two. A cross-session
REOPEN really is a new context: the names in the state belong to a dead one, and
every core must rebuild. A same-session LOAD is not: the context is alive and the
objects are valid, and whether rebuilding is right depends on where the core
keeps its picture. xemu keeps a cache that goes stale; RPCS3 keeps the picture in
the render targets and texture cache a rebuild discards.

A core answers for itself: `video.rebuildOnStateLoad`, and ABSENT MEANS TRUE, so
a package that never mentions it behaves exactly as before - only RPCS3, which
declares false, is changed. A reopen still mints a new id at `ce_gl_start`, so
the rebuild patch 0021 exists for still happens where it is needed.

It is parsed and applied in the ENGINE, beside `video.drawEveryFrame`, for that
declaration's own reason: it is a property of the core, and every host that opens
the package - the frontend, chimera-run, a gate - needs it to hold without being
told separately. A first attempt routed it through the frontend and was wrong
twice over: chimera-run would not have honoured it, and two authorities writing
one global race.

What was NOT settled: why the flip produces black. The window is cleared to
opaque black exactly when `image_to_flip` is null, and the CPU-upload fallback
that should prevent that fires during normal forward play while the picture is
correct. Five source-level explanations were checked and all were wrong; the
bisection that would have answered it could not be completed. The declaration
fixes the symptom and the crash; the mechanism is still open.

RESOLVED the same day. The cause was found by bisecting RPCS3's teardown with a
runtime-selectable skip per step: `gl::driver_state` caches what is bound so a
redundant call can be skipped, and the rebuild never reset it. Teardown frees
the stream buffers' texture views, the driver hands the just-freed names
straight back, and `bind_texture` finds the name "already bound" and skips the
bind - while deleting the old texture had unbound it, so every vertex fetch read
nothing. A full rebuild minus ONLY the stream-buffer reset drew a picture (the
new names differed, so the cache missed). Fixed in the core (rpcs3 patch 0024,
`driver_state::reset()` between teardown and setup), with patch 0025 for the
texture cache's `clear()` sentinel that caused the abort. RPCS3 therefore no
longer declares `video.rebuildOnStateLoad` and rebuilds on a load like every
other core; the declaration stays in the engine, default true, for a core that
genuinely needs it. Verified: rebuild active, five rewinds, 12.34% near-black
and 44,375 colours against a straight-run control of 12.34% / 44,384.

## RAM Search has no size it refuses (user-decided, 2026-09-17)

Issue #89: DOSBox-X's 1997 machine exposes 95 MiB of extended memory and 96 MiB
of physical RAM, and RAM Search greyed both out - in fast mode as well as
detailed - behind a 64 MiB constant whose own comment said it would crash
beyond that. The user's ruling was general: "There should be no limits for RAM
search".

The limit was not arbitrary, it was the cost of the design. The inherited
search held one heap object per candidate address, in an array of references,
and copied that array whole for every search and every undo level. A hundred
million objects is gigabytes before the first comparison, and nothing about it
could be tuned: the shape was the problem. So the search moved into the engine
(`ram_search.cpp`, `ce_ramsearch_*`), which is also where a
thing that decides rather than shows belongs; the window keeps only the
settings the tool edits.

The candidate set has two shapes and the switch between them is nobody's
business but its own. While most of the domain is still a candidate it is
DENSE: a bit per slot, and "previous" is a byte image of the domain - so a first
search costs the domain again plus an eighth, however many candidates there are.
Once a search has cut it to a thirty-second of the slots it turns SPARSE:
parallel arrays, a few bytes per survivor, the only shape that can hold an order
other than by address or an address that was added from a file. Rows of a dense
set are found by a running count per 4096 slots, so the list view can ask for
row ninety million as cheaply as row nine.

Memory comes through the domain's pointer when it has one - every waterbox
domain has - and through a read function in 1 MiB pieces when it has not (a bus
resolved by the guest's mapper, a test's byte array). The search is independent
of any session, which is what lets `test_ram_search.cpp` hold it to an oracle
with no machine: a model that keeps one record per address, exactly as the
frontend used to, runs a random script of searches, polls, removals, sorts,
conversions and undos beside it, and every row must agree after every step,
through both shapes and both ways of reading. Four deliberate breakages of the
implementation were each caught.

Measured, 256 MiB searched by bytes (four times the old refusal): 0.2 s to
start, 0.6 s for the first search, 864 MB peak including the domain itself and
one undo level. The only limit left is the host's memory, and running out is
reported in the tool's message line with the search left as it was.

Sharp edges, and what changed on the way:

- "Last change" moves one candidate's previous at a time, which a byte image
  cannot hold when candidates overlap (misaligned, wider than a byte). That one
  combination is always sparse, and costs what sparse costs.
- Undo is a real undo now: the state BEFORE a search or a removal is kept, five
  deep, so the first search can be taken back (it could not before). A dense
  level costs the image again; if there is no memory for a level the history is
  dropped rather than the search refused.
- Sorting by value, previous or difference sorts by what is SHOWN (signed and
  float order), where it used to sort the raw bits; and a "difference" search in
  signed display accepts a negative difference, which the raw comparison could
  never match.
- Domains of 1 MiB and more still START in fast mode, because detailed polls
  every candidate every frame. That is a default; the user can switch back.
- The list view counts rows in an int and "select all" walks them: a search of
  more than 2^31 candidates lists its first 2^31, and selecting all of a
  hundred million rows is as slow as it always was. Both are the control's, not
  the search's.

## A version is listed with its date, newest first (user-decided, 2026-09-17)

Issue #67: with several builds of one core installed, the New Project picker
listed commits, and a commit does not say which of two is newer. The user's
ruling: the date is shown whenever a core version is listed, and versions are
offered latest first with the latest as the default.

Where the date comes from was the only real question, because a package has
none: it is a byte-reproducible zip and every time inside it is 1980. Three
sources were on the table - the feed the core manager already fetches, a note
written at install time, or the package itself - and the user chose the
package. It is the only one that is true offline, for a core copied in by hand,
and for a local build; the other two know only what was downloaded through the
core manager after the change. So every core's `build-package.sh` stamps
`versionDate` beside `version`: the commit's date in UTC, not the build's, so
the same commit still makes the same package.

Packages already published have no stamp, so the frontend falls back to the
core manager's feed cache, matched by commit and read from disk only. Nothing
is fetched to paint a list. A version with no date from either is listed by its
commit alone; the file's modification time was rejected as a fallback because
it answers "when was this copied here", and showing it would look like an
answer to the question that was asked.

Ordering is one rule: cores stay where they
were, the versions of one core go newest first, undated ones after the dated.
The wizard sorts its package list with it once, which is what makes "latest is
the default" true everywhere at no further cost - the picker opens on the first
line, and guessing a core from a dropped file and seeding from a previous
project both take the first package that matches. The core manager's version
selector had the same defect in another shape: a build that was installed but
not published came after every published one however new it was. It is one
list by date now.

Found on the way: the UI test gate took its verdict from the pipeline that
filters the runner's output, which is the grep's status - "there was output".
A run with five failing tests printed `failed: 5`, exited 0 and never named
them. It now reads the runner's status and the failed count, and prints the
tail on failure; a planted failing test turns it red and removing it turns it
green. What the five were is not known: they appeared in one run straight after
a partial rebuild and in none of the six runs since.

## The data directory can be moved, and the next start is what moves it (user-decided, 2026-09-17)

Issue #52: everything Chimera keeps per user is under `%LOCALAPPDATA%\Chimera`,
which is the system drive, and a greenzone or a PS3 game's compiled code is
gigabytes. The only way to put it elsewhere was `CHIMERA_DATA_HOME`, which
nobody who needs this will find.

Two things were the user's to decide. WHAT moves: the regenerable caches alone
(keeping recovery journals and crash notes on the system drive, where a missing
external disk cannot take unsaved work with it), the caches and the core store,
or everything - and the answer was everything, the same thing the variable has
always done. And what becomes of the data already there: the user is asked, and
may move it or start empty and leave it.

So there is one setting, `DataDirectory`, and one resolver. The core store used
to work the same location out for itself and remember it; it asks the resolver
now, every time, because a second resolver is a second
place to teach and a remembered answer is one that goes on pointing at the old
directory. The environment variable still wins: a portable install or a test
that sets it means it, and the window says so rather than offering a choice it
would not honour.

The window does not move anything. A running session holds a state history and
a recovery journal open, the package list holds paths into the core store, and
Windows holds the crash folder's path; moving the directory under that is moving
files in use, and every one of those holders would need its own way of letting
go and picking up again. Instead the change is RECORDED (`DataDirectoryPending`)
and carried out by the next start, after the config is read and before anything
else exists. None of those problems has to be solved, because at that moment
none of them has happened yet. Crash capture is the one thing armed earlier - a
crash while reading the config should leave a note too - so it is armed at the
default place and repointed once the directory is known; the helper reads the
folder out of the process's memory at the moment of the crash, so repointing is
rewriting it.

A move is a rename where that works and otherwise a copy that is checked before
any original is deleted. Until the last entry is in the new place the old place
is whole; a failure or a full disk puts back what was renamed, removes what was
copied, and leaves the setting alone. The pending change is cleared and the
config saved BEFORE the user is told anything, so a session that dies cannot
find the change still waiting and try to move a directory that has gone.

Three refusals, each because the alternative loses data. A folder that already
has other things in it is not taken over: the data gets a `Chimera` directory
inside it, so "everything in the data directory is Chimera's" stays true and a
later move can take all of it. A folder that already holds Chimera data is
adopted as it is and never merged into. And a directory is not moved into or out
of itself.

A data directory that cannot be reached at startup - the drive is not plugged in
today - is a warning and a session on the default location, with the setting
KEPT. Forgetting it would orphan the data on the drive and start a second set
growing on the system drive, which is the problem the setting was for.

Proved without a window (`DataDirectoryTests`: a second volume is stood in for
by a rename that refuses, a failure half way by one that refuses the second
time) and through the real frontend, headless: a config with a pending move
starts, the directory is moved, the setting follows and the pending flag is gone;
a config pointing at a path that cannot exist warns, runs on the default and
keeps its setting.

## The status bar says which disc is in the drive (user-decided, 2026-09-17)

A machine with four floppies or two discs swaps between them through its own
inputs, and nothing on screen said which was selected or whether it had gone
in. The user asked for both, "for all cores where swapping is an option".

They are two facts, not one, and that is the reason the feature is worth more
than a counter. DOSBox-X's changer is a selector that Previous/Next move and a
Swap input that puts the selected image in: after a Next the machine is still
reading the OLD disc, and a person who cannot see the selector cannot tell one
that moved from one that did not (issue #47 was exactly that, found by reading a
log line). A Sega CD's tray can also simply be empty. So the guest reports, per
drive, its list of images, where the selector stands, and which image is in -
or -1 for none. When the two agree the bar says it once, `2/4 ..(Disk B).fdi`;
when they do not, `1/2 Disc 1 | selected 2/2 Disc 2`; an open tray reads
`empty | selected ...`.

It rides the existing drive-light group rather than being a new tool: the drives
are already enumerated there, per medium the PROJECT put in, and the label that
held a light now holds a light and a name. The names are copied out once at
load; the two indices are asked of the running machine as the bar is painted,
because they are whole-machine state and a rewind takes them back. The engine
holds what the guest says to the list it declared, so an index can never name
an image the frontend does not have.

Long names are cut in the MIDDLE. The disks of one game share a long beginning
and differ at the end - "... (Disk 1 of 4)(Disk A).fdi" - so cutting the tail
would leave four labels that read the same; the tooltip has the whole name.

Which cores: DOSBox-X (floppy and CD, selector plus swap) and Genesis Plus GX
(Sega CD, one-step swap, tray can be empty). Opera declares Previous/Next Disc
on the wire but upstream has no swap entry point yet, so it has nothing to show
and shows nothing. DOSBox-X had no floppy drive in the list at all, and its
"Hard Disk" light came on for floppies too - one flag for every FAT image. The
FAT driver now tells a floppy from a hard disk, so the floppy drive has a light
of its own and the hard disk's means the hard disk. Neither lights while a
guest OS booted from the image does its own disk I/O through the BIOS; that was
already true of the hard disk and is left as it is.

Proved in the sandbox harness with the gate's two discs: untouched reads
selected 1 / inserted 1; a Next alone reads selected 2 / inserted 1; Next with
Swap reads 2 / 2; and the Next-alone answer is the same with a save and load
around every frame. Not yet seen in the real status bar on a running project.

## The game is hashed when the file form is left, and the files can be ordered (user, 2026-09-17)

Two corrections to the New Project wizard's file form, both from using it.

"The game is hashed when it is picked" (above, same day) was the wrong moment.
It read a multi-gigabyte disc the instant it was dropped, before the person had
finished choosing; and which file IS the game is "the first of its slot", which
adding, removing and reordering all change, so a hash taken at pick time could
be of a file that was no longer the game by the time it was used. The purpose
stands - the compile step must open on an answer, not on a stall - and it is
served by hashing at Next: the list is settled, and the file is read once.

And the order of a slot's files could not be changed. It is not a convenience:
the first file is what is in the drive at boot and the rest follow in swap
order, and a PC-98 game can want its second disk booted. The order was whatever
order the files happened to be added in, and the only remedy was to remove
everything and start again. A slot that takes more than one file now has Up and
Down beside its list; they follow the selection, the moved file stays selected
so a button can be pressed again, and a slot that takes one file has neither.

## A branch's state is a file the engine writes (user-decided, 2026-09-17)

Issue #84, and then the user's own machine: creating a TAStudio branch in a PS3
project threw `OverflowException`. A branch kept a whole copy of the machine in
a byte array, and an array cannot be more than 2 GiB. Oblivion copies gigabytes
of disc data onto the console's hard disk, the disk is part of the machine, and
its state is 4.31 GiB AFTER zstd. The greenzone never had the problem: the
engine holds its states natively.

Three answers were offered - degrade an oversized branch to "input only, replay
on load" (the path a GPU project's branch already takes after a reopen), that
now and the proper fix later, or the proper fix - and the user chose the proper
fix: the engine holds branch states.

"Holds" became "writes to a file", for three reasons found on the way. The
sandbox saves and loads a state through a write and a read callback, so the
engine can stream the machine through zstd straight into a file and back, and
the state never exists whole anywhere - not in the frontend, and not in the
engine either, where five 4 GiB branches would otherwise sit in RAM. A file is
also already persistent: there is no second step at project save in which
gigabytes are copied into the cache zip (whose lumps had the same 2 GiB problem
waiting). And the thing the frontend holds shrinks to a NAME, which is what a thin
window wants it to be.

So: `ce_session_state_save_file` / `_load_file` in the engine, with a small
caller-owned tag riding along (the frontend's frame and lag counters, which are
not the machine's). The file is written beside its name and moved into place, so
a failed save leaves what was there. A branch holds no state of its own any
more; it names a file in `Projects/<id>/Branches/`. WHICH file is
which branch's is recorded in the cache zip where the state itself used to be,
under the same rules as before - not for a GPU-drawn machine, not after a
session that did not end cleanly, not when the cache is another machine's - so
every reason a branch could lose its state and replay instead still applies,
unchanged. A cache written before this still holds the old state lumps; they are
not read, and those branches replay to their frame once.

Files that no branch names are removed: when a project is loaded (a branch that
was never saved, a cache that was set aside) and as branches are made, updated
and removed, sparing the one TAStudio's undo may bring back. "Undo Branch Load"
used to clone the whole machine on EVERY branch load; when the last state written
was over 512 MB that backup now keeps its input alone and the undo replays to it,
because a second multi-gigabyte write per load, for an undo rarely used, is the
wrong trade.

Proved: the synthetic gate saves to a file and loads straight back mid-movie in
all four movies and ends byte-identical to the goldens, and refuses a garbage
file and a truncated one without taking the process down (57/57). On the GTX
1060 with Oblivion: a state saved at frame 2400 is 4.31 GiB; saved and loaded
back at frame 2000, the console's 256 MiB of main memory 200 frames later is
identical to a run that never did it. Cost, measured roughly: about half a
minute per save or load of that size. Not yet done: clicking the button in
TAStudio on Windows.

## The disk floor never costs the whole cache, and it is on screen (user-decided, 2026-09-17)

Issue #88 was titled "Cache value not saved, all cache deleted on every boot",
and neither half was what it looked like. The value was saved. It was being
overridden by a second rule the window never showed - leave the disk 20 GB
free - and on the reporter's machine, 14 GB free of 237, that rule worked out a
limit of ZERO: the shortfall was 6 GB, the whole cache was 94 MB, and "the cache
gives back what the floor is short by" clamps at nothing. So every start emptied
it, greenzones and unpacked cores alike (they had locked the cores to save them),
and the window explained itself with "held to  rather than to the number beside
it" - a blank, because the size formatter shows nothing for zero.

The floor is right in its own case: it stops Chimera filling a disk. It was
wrong here because it did not ask whether the cache was the problem. Four
changes, all the user's: the floor never takes the cache below a gigabyte (or
the user's own limit, where smaller); it is a box of its own beside the limit,
saved like it, with zero for off; the message names the rule and the figures and
never prints a blank; and it points at Config > Data Directory (issue #52), which
is what a person with a full system drive actually needs.

A discontinuous alternative was considered and dropped: "a cache that could not
reach the floor even if emptied is left alone". It would leave a 5.9 GB cache
untouched and cut a 6.1 GB one to a gigabyte. The guaranteed minimum alone is
continuous and says the same thing where it matters.

The size formatter still shows nothing for zero - that is deliberate for list
cells and a test pins it - so the blank was fixed in the sentence that had it.
The tests that pinned the floor were written in bytes, where a one-gigabyte
guarantee swallows every case; they are at the sizes the rule is about now.

## What a PS3 state weighs, and why compressing it is not the answer (measured, 2026-09-17)

The user expected compression to help a great deal with PS3 states, and asked
for it to be built and measured, with a progress window for the wait. It was
already built - a branch's state file goes through zstd level 1 - so what was
missing was the measurement. Oblivion at frame 2400, GTX 1060 machine, NVMe,
one save and one load of the same state:

| how | file | save | load |
|---|---|---|---|
| raw (`CHIMERA_STATE_RAW=1`) | 5.45 GiB | 11.6 s | 3.9 s |
| zstd level 1 (the default) | 4.31 GiB, 1.27x | 11.1 s | 2.7 s |
| zstd level 3 | 4.25 GiB, 1.28x | 18.3 s | 2.7 s |

Level 1 is free - the save is no slower than raw and the load is faster, there
being less to read - and it barely helps; level 3 buys nothing for seven
seconds. Every other machine measured compresses 8 to 118 times. This one does
not because of WHAT it is: the game's caching thread copies 41 files, 4.30 GiB,
from the disc to the console's hard disk (its texture, voice and mesh archives),
and that disk is the machine's memory filesystem. Four fifths of the state is a
second copy of files that are in the ISO, already compressed by their authors.

So the lever for PS3 is not a better compressor. It is not carrying the disc
twice: a hard-disk file whose bytes are the disc's could be held as a reference
to the disc file the project already has, and would then cost a state nothing -
which would shrink a branch, its save and load, AND every greenzone anchor by
the same four gigabytes. That is a change to the core's memory filesystem and is
not done here; it is recorded because the measurement is what found it.

Done the next day, in the core (rpcs3 76b992c, "not carrying the disc twice"):
a file the game writes whose bytes are provably a stretch of a disc file is
held as a reference to that stretch. The same state, raw, went from 5.85 GB to
1.23 GB, its save from 12.7 s to 1.7 s and its load from 4.1 s to 1.0 s, with
the machine byte-identical to the control through the whole movie and across
state-file round trips. Compression of PS3 states is now a question about the
remaining 1.2 GB, which is the machine proper (its 256 MB main memory, the
SPUs, the RSX's local memory and the emulator's own tables).

What was built: the engine reports progress through a save (against what the
last state of the machine weighed - the end of a save is not known until it is
reached) and a load (against the file's length), `ce_session_state_file_bytes`
says what the last one weighed raw and stored, and the two environment switches
above exist for measuring. The progress window now has a delay: TAStudio's
branch save and load open one only if the work has lasted 400 ms, so the same
button that takes milliseconds on an NES never flashes a window, and the half
minute it takes on a PS3 no longer looks like a hang.

## The greenzone is packed in memory, one stretch behind the machine (user-asked, 2026-09-18)

With the disc out of the state, the user asked for compression in memory too.
A stretch of the history that has closed - the next anchor has been taken - is
only ever read or shortened again, so its bodies are held as zstd frames; the
newest stretch, the one being written to, stays raw. The packing is a helper's,
and its result is taken at one moment only, when the next stretch closes, so
the budget's arithmetic is identical threaded and in line - the property the
history's differential fuzz pins and the reason "phase 2" was never built. On
Oblivion (PS3, 8 GB budget) a stretch went from 2.45 GB to 0.26-0.44 GB, the
frames held from 312 to 817, and a rewind to frame 1200 that used to land on
frame 434 and replay 766 frames lands on 1198 and decodes for a second. The
whole account, with the measurements, is in docs/state-manager.md, "A closed
stretch is packed in memory". Done the same day, at the user's word: a history
file that carries the bodies as they are held (`ChimeraHistory5`), so a save of
packed stretches is a copy and a load takes them as they are: the same PS3
history, 2.57 GB, saved in 4.7 s instead of 18.5 and loaded in 1.0 s instead
of 23.1.

## A script's input reaches the frames TAStudio invents (issue #95, 2026-09-19)

Reported against a PS2 project: a Lua script opened TAStudio, asked for
recording mode and pressed a button every frame for a hundred frames, and the
movie log held the press on none of them - nor did the machine's own pad
readout. Reproduced on Linux with the synth core in seconds, and the press
turned out not to be lost where the report pointed.

With TAStudio actually recording, `joypad.set` reaches the log and the core on
every frame: it lands on `ActiveController` through the override adapter, which
is where a physical press lands too, and record mode writes whatever is there.
What loses it is the state the script was really in. TAStudio owns playback, so
a script drives it with `tastudio.setplayback(target)` - and a seek turns
recording OFF for its duration (`GoToFrame`), so that the rows it passes over
are replayed rather than typed over. That rule is right, and it is kept.

Past the END of the log there are no rows to protect. A seek out there cannot
replay anything: every frame has to be AUTHORED as it is reached, which
TAStudio does itself before each frame - and it was writing them from the
autohold/autofire state alone. So the extension threw away everything actually
being pressed, by a hand on the pad as much as by a script, and because that
same row is then read back to feed the core, the machine saw nothing pressed
either. Both halves of the
report, one cause.

The extension now takes its input from what record mode would have written
whenever the person asked to record and a seek is what took it away. Read-only play past the
end is untouched: with nobody recording, the autoholds remain the only way to
hold a button out there, which is what that mode has always meant. Witnessed by
three runs of a new Level-B leg (`T:box:luaInput`, `T:box:luaQuiet`): a held
button must reach every extended row, the controller the core is handed, and
the machine's RAM; an unpressed run must leave those rows empty; and a
read-only run must leave them empty however hard a script presses.

Two traps worth writing down for the next unattended TAStudio script. The piano
roll pauses again on its own the moment a seek ends, so `client.unpause()`
belongs INSIDE the loop, with `emu.yield()` and not `emu.frameadvance()` - a
script parked in frameadvance is never resumed to notice it was paused, and
what looks like a hang is the run loop spinning beside it. And the order of the
two calls matters: `setrecording` before `setplayback` is the shape that used
to lose the input, because the seek is what suspends recording.

Found in the same round: every tastudio Lua method that asked for the tool
without calling `Engaged()` first handed the script a raw
NullReferenceException, because `Tools.TAStudio` is a get-or-CREATE - the call
built a window and died inside it on the project that was not there.
`getrecording`, `setrecording`, `togglerecording`, `setbranchtext` and
`get_branch_index_by_id` now answer the way the rest of the library already
did: false, nil, or nothing done.

## A gate that has never failed has not been tested (2026-09-20)

Seven distinct instances of a gate passing while the thing it vouched for was
broken were found in one day, across three repositories. Written up as
[gates.md](gates.md), because seven in a day is a class of mistake and not
seven accidents.

The worst of them, by how long it lasted and how much it covered: the PCSX2
core ran EVERY PlayStation 2 game with an empty per-title database - no clamp
modes, no round modes, no game fixes, none of the corrections upstream keeps
for hundreds of titles - and the gate was green throughout, because no leg ever
asked whether the database had anything in it. The user-visible symptom was two
separate bug reports about Final Fantasy X.

The cheapest of them to have prevented: miniBox's thunk pool ran out at 32 and
answered 0 for the rest, which `mb_host_proc_addr` reports exactly the way it
reports a symbol that is not in the ELF. Seventeen of quickerNES's exports read
as "this core does not have that feature" while sitting in the binary. One line
on stderr when the pool runs out would have made it obvious on the first run.

The rule the seven come down to, and the one worth keeping when the detail has
faded: **a leg that has never been seen to fail is a leg that has not been
tested.** Break the thing, watch the leg go red, revert, and say in the commit
that you did. It costs minutes and it converts a belief into a fact.

The corollary, learned the same week at the cost of a shipped regression:
"fixed by construction" is not fixed. A paletted-texture fix went in on 2026-09-19
with its own commit body saying no disc on hand used the format, so the fix
"waits for a game of sprites to prove it". A game of sprites arrived the next
day and the fix was itself the bug.

## The colours are data, and Light is the ones we had (user-decided, 2026-09-20)

Issue #112: the bright white is a strain on the eyes, and the reporter, who
does not write code, offered to contribute palettes. The decision was: do it
everywhere rather than on a subset of windows, ship at least Light and Dark,
and Light must be what Chimera looked like before themes.

So a theme is one colour for every role the window paints with, about a
hundred of them, and nothing else: a JSON object of role name to `#RRGGBB`,
with Light and Dark built in as the same kind of file. Light says
`"system:ControlText"` and the like, so it is the desktop's own palette rather
than a guess at it. Config > Theme > Write a Copy to Edit hands a contributor a
complete file to start from. docs/theming.md says how themes are read, written
and applied.

## What a PS3 install actually costs, and which half a wizard step can buy (measured, 2026-09-20)

Sergio asked for the PS3 `.pkg` install to stop crushing the greenzone and host
RAM, and proposed the shape of the answer himself: make the install a step in
the wizard, the way precompilation is, and cache its outcome so a player never
repeats it. Measuring it first moved the target, because the premise is half
wrong and the half that is right is somewhere else.

Two costs were separated on purpose. The PACKAGE install is deterministic,
happens once per boot and produces a known tree. The GAME'S OWN data install is
the game copying its archives to the console's hard disk, driven by emulated
input, several gigabytes. Measured on this box (the sandboxed core through
`run-wbx`, `CHIMERA_STATE_RAW=1`, every figure a byte count the core printed):

| game | pre-seal install into memfs | state, raw |
|---|---|---|
| Super Stardust HD, its 305.2 MB package as the game | firmware 194.8 MB, then 500.0 MB | 118.0 MB at frame 5 |
| the same, plus its 248.6 MB update package | 482.1 MB (the update replaces files) | 112.5 MB at frame 5 |
| echochrome, its 107.6 MB package as the game | firmware 194.8 MB, then 302.6 MB | 853.1 MB at frame 5 |
| Resident Evil 5 Gold, 14.4 GB Redump disc + key | firmware 194.8 MB, then 210.6 MB (the disc's own INSDIR package) | 1.141 GB at frame 400, 1.144 GB at frame 1300 |

**A package install costs a state nothing.** Half a gigabyte of memory files
and a 118 MB state; adding a second package of a quarter of a gigabyte left the
state at 112 MB; and echochrome, with a THIRD of Super Stardust's memory files,
saves a state seven times the size, because what makes a state big is how far
into the console's own memory the game has got and not what is on its disk.
That is not a coincidence and not a compressor: everything
these installs write lands BEFORE `Emu.Init` seals the machine, and a miniBox
state is the pages that are dirty since the seal (`mb_block_state_size`), so the
whole installed tree is baseline and no savestate carries a byte of it. The
question "does installed content need to be in the state at all" already has the
answer "it is not in the state", for everything the console installs on the
machine's behalf - the firmware's `/dev_flash`, a project's `.pkg` files, a
digital title's own package, and the packages a disc carries in `INSDIR`.

**The game's own install is the whole of the state cost**, because it happens
after the seal. That is the four gigabytes the disc-mirror work removed for
Oblivion the previous week (5.85 GB of state to 1.23 GB), and it is what made
Resident Evil 5's process grow from 4.9 GB to 10.9 GB while its progress bar
filled. Where the mirror cannot reach - a Redump disc, whose bytes the ISO layer
decrypts, so what the game writes never equals what the disc holds - it is still
there in full. A PS3 state with a game's install in it is larger than the
ENTIRE greenzone budget (4 GB), so the greenzone of such a game holds no frames
at all; without it, a PS3 state is about 1.14 GB and flat, which is the machine
proper. So of the two costs, the game's own install dominates the greenzone
absolutely and the package install contributes exactly zero.

**What the package install does cost is the boot.** Two timed boots of Super
Stardust HD with a warm compile cache, seconds from exec:

| phase | run 1 | run 2 |
|---|---|---|
| firmware installed (194.8 MB of `/dev_flash` out of PS3UPDAT.PUP) | 91 | 83 |
| the game's package installed (305.2 MB, 61 entries) | 96 | 88 |
| the project's package installed (248.6 MB, 53 entries) | 97 | 90 |
| booted | 101 | 94 |

So the package installs are five and two seconds, and **the firmware install is
eighty-five of a ninety-four second boot** - paid on every open of the project,
and by every one of the eight precompile sessions, for a file that is the same
on every PS3 project on the machine. If a wizard step is to cache an install,
that is the install worth caching; the packages are a rounding error beside it.
(Worth a profile before anyone builds around the number: with the log raised,
most of the eighty-five seconds is rpcs3's TAR reader failing to parse a header
once per 512-byte block across the padding, which may be cheaper to fix than to
cache.)

### What a cached install would have to be

The mechanism already exists and is not new machinery: a memfs MIRROR is a
writable file with no bytes of its own, reading through to a stretch of a
read-only host-backed file, materialising itself the moment anything writes to
it in a way the source does not describe. Today one is only ever INFERRED, from
a game copying a disc file. The same node, DECLARED, is a cached install: the
wizard step runs the installer once, writes the resulting tree into one packed
file beside the compile cache, and every later session reconstitutes the tree as
mirrors of that pack instead of decrypting anything. The boot loses eighty-five
seconds and the guest arena loses the tree's size, because the bytes stay on the
host's disk and are read as the machine asks for them, exactly as a mounted disc
is. The step belongs BEFORE precompile, not after: for a digital title the
modules to precompile are the ones the install created, and the eight sessions
should start from the pack rather than each build it again.

Three things about it are not ours to decide, because they touch what a movie
means.

The first is that a cached tree becomes part of the boot path, so a movie
depends on it. That is only safe if the pack is a verifiable function of what
the project already cites - the PUP, the packages in slot order, the disc - so
the project records the pack's hash, a regenerated pack that hashes differently
is a REFUSAL rather than a warning, and the reproduction contract stays the pair
it is. A pack nobody can check would make a movie depend on a blob.

The second is that cold and warm are not the same machine at the seal. Whether
the installer ran or the pack was mirrored, the console reads identical bytes,
but the emulator's own heap does not hold the same thing, so the sealed baseline
differs and a state does not cross between them. The clean answer is the one
precompile already uses: make the step mandatory before Create, so a project has
one flavour and never the other - which is to say a PS3 project could not be
created without doing the install first.

The third is the room it takes. A pack is the installed tree: 195 MB for the
firmware, and a digital title's own size on top - gigabytes for a big one. The
firmware pack is the same for every PS3 project and should be shared, which is
the opposite of what was decided for compiled code (one directory per game, no
sharing) and for the opposite reason: there the shared part was 12 MB against a
game's 47, here it is 195 MB against nothing at all for a disc game. And unlike
compiled code it has to be INSIDE the Cache Manager's budget rather than beside
it - `Tools > Pre-Compiled Modules...` is the only way compiled code is ever
removed, and `CacheKind` has no member for it, which this document's cache
chapter already describes as though it did.

Not built here. It is recorded because the measurement is what found it, and
because the measurement says the wizard step buys a boot rather than a
greenzone.

## The fault handler gives up its pointers, and never reports twice (chimera#127, 2026-09-21)

A PS3 core died in the main menu of a game. Everything Chimera is built to do
about that worked: the guest death was caught, the dialog came up naming the
frame and offering the four ways out, the inputs were safe, the process was
alive. Ninety-two seconds later the process was gone, and the crash note said
`stack overflow` at an address in `msvcrt.dll`, with a stack of nothing but
ntdll and `libminiboxhost.dll` repeating the same four frames fifty times over.

Two faults, and the note named neither.

`minibox-diag.log` had the shape, once it is recognised: one line reporting a
perfectly ordinary fault in host code - a write the CLR raises and handles
every day - and then fifteen hundred identical lines, all naming one
instruction inside `libminiboxhost.dll` reading one address. Disassembling a
mingw build of the same miniBox commit at the offset the crash note gives
settles what that instruction is: the first load in `say_region`, reading
`g_layout` - the pointer the fault handler uses to say which region of the
guest an address landed in. It sits immediately after the `mb_diag` that
printed the last line in the log, and after a null check that passed. So the
handler read the layout, faulted, and Windows called the handler again FOR ITS
OWN FAULT. A vectored exception handler has no depth limit and no second
chance; it ends when the stack runs out.

`g_layout` points inside the `mb_host`, and `mb_host_destroy` freed the host
without taking it back - the same mistake that had already been found and fixed
one field over, for `mb_guest_ctx`, whose comment two lines above says exactly
why: a host that is gone must not be what the fault handlers read. On Linux a
freed chunk usually still reads like itself and the report is merely wrong. On
Windows it is really gone.

The general rule the second fault leaves behind, which is the part worth
keeping: **handling a fault may nest; REPORTING one may not.** The guest's own
fault handler runs guest code, which can trip a clean page and fault again, and
that must be served. But the report walks the layout, the block list, the bytes
at `rip` and the guest's stack, and any of those can be the thing that is
wrong - so a fault arriving while a report is running is now said once, in one
sentence, and passed straight on. Saying more is exactly what just failed.

Fixed in miniBox (`fix(tripguard): a destroyed machine is not what the fault
handler reads`), with both legs watched failing first: a destroyed host gives
its layout back (asserted at both ends, because a NULL that was never set would
pass on its own), and a Windows-only child that points the layout at an address
that is not there and counts what the handler managed to write - two lines
guarded, 659 unguarded, which is the user's log reproduced.

## Guest code keeps nothing below the stack pointer (user-decided, 2026-09-21)

A SysV x86-64 leaf function may keep live values in the 128 bytes below `%rsp`
- the red zone - because nothing on Linux writes there behind its back: a
signal handler runs on its own stack. Windows delivers an exception onto the
interrupted thread's own stack, which during guest execution IS the guest's
stack, and in a guest process something in that delivery rewrites the bytes
below `%rsp - 0x28` with their content as of the previous exception's return.
A guest leaf interrupted by a dirty-page fault then reads back stale spills.

That is not a hypothetical. It is why Flycast's `cpu=jit` produced a wrong
Dreamcast on Windows whenever the greenzone was on: the CHD decompressor
spilled its two limits below `%rsp`, a dirty-page fault landed in the middle
of the routine, the reload came back as zeros, the hunk decode failed, and the
emulated machine parted from its Linux twin at frame 137 and never came back.
It was found by restoring those 128 bytes from inside the fault handler, which
made the machine byte-identical to Linux, and by replaying the Windows bytes
into the Linux guest, which made Linux diverge the same way.

**The decision: every guest is built with no red zone.** Not per core - in the
shared toolchain, in the `*cc1` entry of the musl-gcc specs that every core's
guest compile passes through, in musl's own flags and libstdc++'s, and in the
Rust guest's target spec, which is where LLVM is told. And it is enforced
rather than remembered: `check-wbx.sh`, the guest-image check every core
already runs, counts memory operands with a negative displacement from `%rsp`
and requires zero, with a named allowlist that currently holds one symbol.

The reason to enforce it rather than trust the flag is the shape of the bug.
It does not announce itself: no crash, no diagnostic, no wrong pixel - a
savestate that quietly stops matching, hundreds of frames after the fault that
caused it, on one host only. A core added next year that finds its own way
around the specs would reintroduce it silently, and the only thing standing in
the way is a check that runs on the image rather than on the build recipe.

Cost, measured before the decision was asked for: 1000 frames of the same game
three times each, means 0.01 s apart on 55.6 s, inside either run's spread. A
94 MB guest image grew 92 KB.

What is deliberately still open: WHICH component performs the rewrite, and
what enables it. Plain Windows processes keep the bytes, so do sandbox blocks
with `%rsp` inside them, so does a thread given real TEB stack bounds, and so
does one carrying the guest's FS base across the exception. The flag removes
the exposure; it does not explain it. miniBox `docs/RED-ZONE.md` holds the
measurements and names the two saved instrument patches for whoever resumes.

## A pointer names a fraction of the screen, not a number in a plane (user-decided, 2026-09-22)

An absolute input - a mouse pointer, a light gun, a touch panel - has to name a
place on a picture whose size the declaration cannot know. `waterbox.config`
fixes an axis range a priori, a limitation carried over from BizHawk, and DOS
and PCem change video mode whenever they like. The user's framing was that the
range "should always be the guest's screen resolution"; what they chose, after
the survey below, was to keep a normalised wire and **fix the unit**.

**The survey changed the problem.** The value was already resolution
independent: the pointer is turned into guest pixels, divided by the LIVE
picture's width and height, and expanded into the declared range. The unit was
simply arbitrary - "1/2560ths of the screen" for DOSBox-X - and, worse,
**the core divided by a different number than the config declared**:
`dosbox-driver.cpp` used 800 against a declared 2560, so pointing at the middle
of the window put the DOS cursor 1.6x past the right edge. Measured on that
build with the position held, every value across the whole declared range
answered the middle of the screen: the axis did not reach the machine at all.

**The rule, for DOSBox-X and PCem:** an absolute position is declared
`0..65535` neutral `32768`, and the core converts it against **its own live
picture size**, never a constant and never a number the frontend supplied.
`pixel = axis * screen / 65536` - 65535 is the largest value, 65536 the number
of steps. (Measured: dividing by 65535 instead changes no reading a gate can
take, because the clamp absorbs it; 65536 is a matter of not depending on the
clamp.)

**It is a spelling, not a new idea, and the tree is deliberately not uniform.**
Every core with an absolute axis already normalises and scales in the core -
opera, pcsx2 and flycast take a signed `-32768..32767` and immediately add
32768 and divide; Ruffle carries `0..8191` and multiplies by its live stage;
snes9x declares gun coordinates in SNES pixels and clamps Y against the live
`PPU.ScreenHeight`. None of those is wrong, so **the user scoped this to
DOSBox-X and PCem and left the rest alone**. `0..65535` is the preferred
spelling for new work; it is not a migration anyone owes.

**Three consequences worth writing down.**

*The digits problem dissolved.* `65535` is exactly five characters, which is
what the entry format already pads an axis field to
(`movie_entry.cpp:139-141`). Column width, the
typed-digit limit and the OSD's six-character field all come out unchanged, and
**the engine needed no change at all** - it never reads `min` or `max`, only
`neutral`.

*An absolute position must be asserted every frame, not when it changes.* A
fraction is not a fixed pixel: the same 32768 is pixel 160 in a 320-wide mode
and 320 in a 640-wide one. DOSBox-X's `Mouse_AfterNewVideoMode` also resets the
cursor to the middle of the new range on every mode set, so a position that
only wrote itself on a change was overwritten and never recovered. An axis
driven by an explicit relative speed is exempt, or a movie steering relatively
gets dragged back to the neutral - which is now mid-screen.

*The wire must be differenced in pixels, and this is the one that hides.* Where
a core also derives relative movement from the position (DOSBox-X's issue #61
rule, and PCem, which has no absolute device at all), one wire unit is about a
hundredth of a pixel at 640 wide. Differencing the wire makes every delta a
hundredfold too large **while the absolute cursor still lands in exactly the
right place** - so no test of position can see it. It needs its own check: that
the same movement asked for by position and by speed reaches the machine as the
same number. Both cores now have one, and in both the negative control fired
where the position check stayed green.

**An axis is as wide as it declares, in the frontend too.** TAStudio's axis
drag moved the value by one per four host pixels and its nudge hotkeys by a
literal 1 and 10 - right for a 0..255 paddle, useless at 0..65535, where
crossing the picture would take a quarter of a million pixels of mouse travel.
Both are now a fraction of the range. The custom autofire pattern's value box
had `+/-10000` fixed in the designer and would have silently kept its own value
rather than refusing one it could not hold.

**PCem gained the axis it never had, with a limit stated rather than hidden.**
Its entire mouse list is two serial, two PS/2 and two machine-integrated mice,
every one relative, with no VMware backdoor or tablet anywhere in the tree. So
a position is turned into the movement that would reach it: open-loop
targeting, which drifts if the guest accelerates or warps the cursor and cannot
be measured from outside. A mouse also reports in bytes, so a screen-wide jump
cannot fit in one packet and PCem's devices drop what is over the edge - the
remainder is now kept and delivered over the next polls, and the negative
control for that showed a 639-pixel jump arriving as 127.

## A core may suggest settings for a game, and the wizard applies them where they can be read (user-decided, 2026-09-23)

The RPCS3 wiki keeps a table of recommended settings per game, and the RPCS3
core carries a snapshot of it (with its source, date and licence). The user's
decision: **those recommendations are core settings like any other**, applied
automatically when the new-project wizard reaches its settings page - where
the provenance and the compatibility level are shown too - and **present even
when the game is not in the wiki**, so a person can still experiment with them.

**Every setting the wiki recommends became a declared setting.** 43 of the
wiki's 59 setting names map onto something the core can run with; the other 16
are refused per game with a reason (a Vulkan-only option, the host's audio
device, a camera, a network the sandbox never has). Each display name is the
wiki's own, so a recommendation on a page and a row in the grid read the same,
and each enum's options are rpcs3's own spellings, handed to rpcs3's own
parser - a spelling it does not know is refused, not guessed. **Every default is
what the core ran with before the setting existed**, including the settings the
core pins for determinism (the frame limiter, the vblank rate, the PPU thread
count): a project records only the values somebody chose, and an untouched one
resolves to the package default at load, so a new default would silently move
the machine of every existing project. The pinned ones say in their
description that a change is an experiment, and one a movie must keep.

**The lookup runs in the core, before anything boots.** Which game a file is
takes reading the file the way the core reads it - a package's content id, a
disc image's or a disc archive's PARAM.SFO, PS3_DISC.SFB when the data is
encrypted - and that knowledge belongs to the core, not to the frontend or the
engine. So a core declares `"suggestSettings": true` and exports
`SuggestSettings`; `ce_suggest_settings` mounts the files exactly as a run
would and calls it **instead of** `Init`. The wizard asks through a child
process (`Chimera --suggest-settings package game`), as the precompile step
does, because the core has to be loaded to answer and one that falls over
while it looks must not take the wizard with it.

**What arrives is values, the way a preset is.** They land in the grid, where
they can be read and changed like any other, and nothing about a suggestion is
remembered by the project - it pins the values. Three rules keep that honest:
the lookup happens once per core and game, so going Back and Next does not undo
an edit; choosing a different game first takes back what the previous game's
suggestion set, because game A's recommendation is not game B's; and a project
the wizard was seeded from keeps its own settings - the note is shown, the
values are not applied over them.

**The note says where the values came from, or that nothing was found.** It is
the same sentence the core prints at boot: the compatibility list's status and
date, what the wiki recommends, what the core cannot express, and the page and
licence - or that the title is not in the list or the wiki at that snapshot.
Recognised titles went from 1300 with an applicable recommendation to 2300
once every expressible setting was declared.

**The answer string obeys the engine's `thread_local` rule, and a Windows leg
now proves it.** The first build kept the answer in a `thread_local
std::string`, and on Windows every `--suggest-settings` child died at exit
with an access violation after printing a correct answer - the mingw double
destroy of chimera#123, which the wizard would have read as a failed lookup.
It is a `ThreadString` now. `tests/engine/windows/refused-open.sh` asks for a
suggestion too, and the negative control is recorded because the first two
versions of the check could not fail: a core that answers `""` owns no heap
buffer, and a call made on a worker thread is destroyed once, when the worker
ends. The detector is a long answer (the RPCS3 core's sentence) asked for on
the thread that ends the process - with the old string back it writes a fault
report; with the fix it passes.

## A project remembers its Greenzone choice (user-decided, 2026-09-28)

Issue #158: TAStudio's Greenzone box - every frame, one in N, off - reopened
every project on "every frame", whatever it had been left on. That was
deliberate (74d511d): somebody who turned the greenzone down for a cutscene
would not be left wondering next session why nothing turns green. The user
decided the other way, and decided where: **in the `.chimeraProject`**.

**It lives in TAStudio's part of the file**, the `tastudio` object that already
carries the piano roll's columns and lag display (issue #83), as
`GreenzonePeriod`: 1 every frame, N one in N, 0 off. The
engine keeps that object without reading it, so the project format itself did
not change and no older build refuses the file. A project saved before this
has no such field and opens on 1, which is what it always opened on.

This is not where a greenzone's memory budget lives, and the difference is
deliberate: a budget is a fact about the machine the work is done on, and is
kept beside the greenzone in the cache so a project handed to somebody with
half the memory does not arrive carrying it. The
choice of how often to store a frame is part of how the run is being worked on,
and travels with it.

**The box follows the movie on every refresh**, all three levels and N: a
project reopened on one in seven shows "Every [7] frames", without that 7
becoming the default N in TAStudio's settings.

The witness leg `T:box:greenzoneChoice` opens a project in the real frontend,
sets the file to one in seven between two runs, and checks that the second save
writes the 7 back. With the line that applies the choice on opening taken out,
it saves back 1 and fails.

## Game cores are cores, listed apart, with properties by name (user-decided, 2026-09-28)

An open or reconstructed game can be a core of its own: the same engine,
sandbox, projects, movies and TAStudio, with the user's copy of the game's
files as its firmware. SDLPoP (Prince of Persia) is the first, and it is built
from the original DOS data files: a port that redistributes resources of its
own does not use them. The spec is docs/game-cores.md; what the user decided
is what follows.

**A game core says so, and every list of cores shows it apart.**
`waterbox.config` and the roster carry `"kind": "game"`; absence is an
emulator, which is every core before this one, and a kind this build does not
know is listed with the emulators rather than refused. The Core Manager, the
Cache Manager, the new-project wizard's picker and the firmware window list
the emulators first, then a grey divider row, then the game cores. The
Core Manager keeps its "External cores" divide inside each half. The platform
chooser for an unknown file offers no game core at all: a game core plays its
own game and nothing else.

**Properties are a labelled domain, not a new kind of thing to watch.** The
alternative - a property API the tools would each learn - was declined for the
one that costs the tools nothing: the core keeps the properties in a block of
its own memory, exposes it as a `Game State` domain, and describes it with a
property table. RAM Watch, RAM Search, the Hex Editor, freezes and `memory.*`
all work on properties as they are; the table only gives them names. Raw
memory is exposed beside it where the game has it in one piece.

**Every type a game holds, not the ones the first game used** (user-decided,
the same day, reversing a narrowing that followed RAM Watch's 32 bits): 8- to
64-bit integers, f32 and f64, bools, strings in four encodings, raw bytes,
arrays with a stride (a field of an array of structures), big-endian values
and bit fields. Where a tool cannot hold one, the tool grows - a property
watch the engine reads, freezes kept as the engine's text - rather than the
table shrinking.

**The type system is the engine's.** Reading, writing, showing and parsing a
property decide what bytes a poke or a freeze writes, so they live in
libchimera (`ce_session_property_*`, source/engine/source/game_properties.cpp)
under the thin-window rule, tested by the engine's own test; the frontend lists,
shows and forwards, and a solver linking the engine gets the same
properties. The engine checks the table, not trusts it: a property on a domain
the core does not have (or one with no memory of its own), past the end of
one, of a type it does not read, with a name already taken, or a bit field
that does not fit is left out and said; the rest of the table still works. A
number that does not fit a property's width is refused, not wrapped - except
that -1 sets every bit of a u64, the only way a signed 64-bit caller can.

**A name is the identity.** Watches, freezes and scripts keep a property's
name, never its offset, so a core may reorder its block between versions. A
watch on the address a property starts at takes its name wherever it was made,
unless somebody wrote a note of their own, and a freeze takes its watch's.

**A step is as long as the game makes it, and the engine asks.** A game core's
movie row is one step of the game's logic, and Prince of Persia's last 1/12 s
walking, 1/10 s fighting and 1/60 s on the title. The engine read a core's rate
once, after Init, which played every step at the first one's length (the title
five times too slowly); it now asks again after every shown frame, which the
frontend's throttle and sound already read every loop. Seeks do not ask - no
one paces an unshown frame. A machine that changes its refresh with its video
mode (the X68000's rate export) is live the same way. `chimera-run --rates`
logs it: the SDLPoP route reads 12/s with 10 then 15 at each room change, and
274 steps of 12 with the re-read taken out.

**`game.*` says and carries on; it does not throw.** An unknown name reads as nil and sets nothing, with
the reason in the console, as `memory.*` treats a domain it does not know.

The witness leg `T:box:gameProperties` drives the whole path through the synth
core, whose table names gridWalker's RAM and one of every other kind over RAM
the rom leaves alone: the names in the core's order, a property read against
the same bytes through `memory.*`, every type's bytes (a u64 of all ones, the
least s64, an f64, cut and padded text, utf16le, bytes, big-endian, two
interleaved arrays, two bit fields in one byte), and the cursor moved by
`game.set` drawn where it was put. With the table emptied it fails at the
first check.

## A game's own clock, to the millisecond (user-asked, 2026-09-29)

A game core may name one property as the game's timer (`"gameTimer"` in its
table; docs/game-cores.md): the milliseconds the game itself has counted, as
its speedrunners count them. Prince of Persia's is TASVideos' GameTimer, the
one quickerSDLPoP prints as `IGT m:ss.mmm`: the clock loses one tick of 1/12 s
per tick of play and rolls a minute over after 719, so the time is the ticks
between the clock the game started with and the clock now, times 1000/12. The
arithmetic is the game's and lives in the core; the
engine checks that the name is one whole-number property, reads it, and owns
the only formatting, `mm:ss.mmm` (user: "for humans to see"), so a solver
linking the engine writes the same text the screen shows.

It is shown on the screen (Game > Display Game Time, on by default; see the
next section) and saved in the project as three headers - the
number, the text and the frame - **at the end of the movie only, and never
stale**. The frontend records the time each frame the machine shows and
forgets it after an edit, as it forgets lag; the headers are written when the
time at frame == InputLogLength is known and removed when it is not. A
project opened with the headers seeds the end frame's time from them, so an
open and a save without playing keep it, and an edit before the end drops it.
Stale was the trap to avoid: a header that outlives an edit says a time the
movie no longer makes, and a reader parsing projects has no way to tell.

SDLPoP2's clock starts only with the first story scene after level 4, as the
game's does, so on its own it counts nothing in levels 1 to 4 - the game's
rule, not a gap. The core's IGT From Level 1 setting (user-asked, on by
default) adds the ticks of play before then, so the time runs from the very
beginning of level 1; the frontend sees only the number either way.

## A watch is drawn on the screen only when it is ticked (user-decided, 2026-09-29)

RAM Watch's "Display Watches On Screen" drew every watch in the list, so it
was off by default and a list of any length made it useless. Each watch now
has an On Screen box, the list's first column, off for a new watch (user:
"Disabled by default"); the screen draws the ticked ones, one under the other
in the list's order. The switch stays as the master (user: "Needs to be on by
default"), and was renamed `DisplayWatchesOnScreen`: every existing config had
saved the old switch's default, off, which would have kept the boxes dead on
every machine that ever ran Chimera. A config that had it on loses nothing it
can see - it drew everything before, and draws the ticked ones now.

The ticks live in the watch file, as one line after the watches -
`OnScreen<TAB>0,3`, the places of the ticked ones in the file. A watch's own
line keeps its five tabs, because an older build reads only lines with
exactly five and would misread a sixth field into the notes; the new line has
one, so an older build skips it. The places count the file's lines, not the
watches loaded: a property watch the running core no longer has gives no
watch, and must not move the other ticks up by one (a test fails if it
does). A clipboard copy carries no tick - a paste is a new watch.

With the switch on by default, a watch file loaded at start-up (Recent >
Auto-load) opens the RAM Watch window. It used to open hidden when the switch
was on, the screen being where every watch was; now the screen shows only the
ticked ones, and the window is where the rest are.

## A game core's menu is "Game", and holds the game's own options (user-decided, 2026-09-29)

The core's menu was "Emulator" always, never named after the system, so it
stays where the user left it. A game core has no emulator to name: while one
runs, the same menu is called Game, in the same place - the title follows the
kind of core, not the game, so it still does not move from game to game.

Display Game Time moved there from View. View is for what every core has
(frames, lag, input); the timer is the game's, and only a game whose core
names one (the two Prince of Persia cores) shows the item, rather than every
core showing it greyed. The frontend asks the core whether it has a timer and
never which game it is, so a third game core with a timer gets the item
without a change here. The hotkey and the message position stay where the
other displays' are.

## The lists of cores filter by kind; they draw no dividers (user-decided, 2026-09-29)

Game cores were set apart in every list by a divider row - a grey line reading
"Game cores", with "External cores" and "External game cores" beside it in the
Core Manager. The user found it did not look
right: a divider row reads as a disabled entry, and has to be kept from being
ticked, selected, sorted across and counted.

Tabs, one per kind, were weighed and not taken. The windows act on everything
at once - the Core Manager's Check for updates and Download latest, the Cache
Manager's one budget and Clean Now - so a tab would split what is one list
with one set of actions, and the wizard's core box, a drop-down, cannot hold
tabs at all. Each list is now one list with a Type column and Show: All /
Emulators / Games above it (Kind: Emulator / Game in the wizard, above the
core). The column says on every row what the divider said once; the filter
narrows the view without splitting the actions. A ticked row the filter hides
is unticked, so no button acts on something out of view. The Core Manager's
external cores went the same way: their Source says "(added by hand)".

The Cache Manager has no distinction at all (user: not useful there): what a
cached thing costs to lose does not depend on the kind of core that made it.

## A game core's firmware may be a file of the project's own (user-decided, 2026-09-29)

A game core's firmware is the game's own data. A modified PRINCE.DAT, another
release's file, a translation: each is a legitimate thing to play, so the user
asked that a game core take any file, with the project pinning the SHA-1 of the
file it actually uses. That model was nearly in place already. Projects pin
files by hash, and the frontend hands any readable file to a core, with a
warning. Three pieces were missing:

- **The wizard.** Select File took only the declared hash. For a game core it
  now takes any file, says it is the project's own ("your own: ..."), and pins
  that file's hash. The file is remembered under its own hash, never under the
  original's: under the original's key it would stand in for the original in
  every other project. An emulator core's firmware still takes only the exact
  file.
- **The mount.** Reopening already matched each pin by the project's own hash.
  The mount then asked `GetPath` with the package's declaration, which prefers
  the dump remembered under the DECLARED hash. So a person who had ever
  pointed at the original booted the project on it, silently. That is a
  different machine, and a movie made on the custom file desyncs.
  The project's pins, set when a project boots and cleared when it closes,
  make the pinned file win for that core. What is described and
  recorded is then what is mounted. It was proven both ways, headlessly: a
  Prince of Persia 2 project pinning a modified PRINCE.DAT, with the original
  remembered, got the original's level before the change and its own after.
- **The cores.** Each checked every file against the original's hash and
  refused anything else. They now take whatever they are given. They still
  name a missing file, and still refuse a build known not to work:
  - SDLPoP2: the 1993 floppy's PRINCE.EXE, whose tables it would read from
    the wrong places;
  - OpenSamurai: the floppy's older AdLib driver.

The declarations keep the originals' hashes: they are how the wizard and the
Firmware folder recognise the original, and how "your own" is told from it.

## A core's log is kept only when asked for (user-decided, 2026-10-01)

Tools > Export Core Log... keeps everything a core writes to stdout and
stderr in a file the user chooses. Before this, a core's log reached nobody
on Windows: a GUI process has no stderr. The only way to get RPCS3's log was
a firmware entry named `logtrace`, which the GUI cannot add (#165's reporter
was left stuck on the template line that asked for it).

It is on only because somebody asked, and nothing remembers that they did:
every run starts with it off. Turning it on reboots the core (with consent),
because RPCS3 keeps its full log only from boot; using the item again turns
it off and says where the file is. The work is in the engine and miniBox,
not here: `ce_core_log` starts the file with the time and the engine build,
notes each session's core package and sha1, and has miniBox copy every
guest stdout/stderr write into it, flushed per write so a crash leaves it
whole. A session opened while it is on gets an empty `corelog` file mounted.
RPCS3 checks for it with `stat`, which allocates nothing in the guest, and
then sends its log, at the levels it already keeps, to stderr too.

**The machine does not move.** With the log on, the synth witness ends on
its goldens, and RPCS3's RAM after 150 frames of Rayman Legends is
byte-identical. Trace channels are not raised: a fuller log would change
RPCS3.log, which lives in the console's memory.

## A movie made elsewhere is imported through the wizard (user-decided, 2026-10-01)

The DSDA core reads Doom demos (.lmp). Its author's contract, written for
the user and relayed by another session, asked for four things: an item in
the Game menu; NOT an isolated tool that writes .chimeraProject files,
because the format may change and a separate writer would drift; a dialog
that takes the demo, the IWAD, the PWADs and patches in order, and the
flags; and the user choosing where the project goes, with nothing left to
fix by hand.

So the core only suggests. `ce_import_movie` runs the core's
`ImportMovie` export on a session that is never started (the same path
as `SuggestSettings`), in a child process so a core that falls over
while reading takes nothing with it. The answer seeds the New Project
wizard: the machine, the settings, the files in the core's load order,
and the firmware picked in the dialog, which the wizard takes as if it
had been picked on its own page. The user checks it and presses Create, as for any
project; the input log is then set, the user picks the path, and the
project opens. Its first open fills in the headers only a running machine
knows and saves once, since a file that is not fresh never gets them.

Nothing about Doom is in the frontend. The dialog is drawn from the
core's `movieImport` declaration; a refusal is the core's own sentence.

**No release setting (user-decided, 2026-10-01).** The first cut asked a
Version beside the System - the IWAD release, from a `versionSetting` the
core narrowed per machine. It could only offer the releases the core had
hashes for, one for most games, so the box sat greyed with nothing to
choose. Removed: the IWAD the person gives is the release, and the
compatibility level is how it plays. The import hands that IWAD to the
wizard whatever its hash, and a game core takes it as the project's own.

## Chimera knows no system: the cores say what theirs are called (user-decided, 2026-10-05)

Chimera carried three tables about the machines it does not emulate:
`VSystemID` (the ids), `SystemNames` (89 names, most for systems no core
here runs) and `MnemonicLookup` (the letter of every button and the header
of every axis, per system). A new core meant an edit to Chimera, and a
name was right only if somebody remembered to add it. The user's words:
Chimera should be completely agnostic to whatever systems are run with it;
the names should be provided by the cores themselves.

Four decisions, all the user's:

- **The roster row carries the names** of a core not yet installed:
  `official-cores.json` is format 2, `"systems": [ { "id", "name" } ]`.
  Format 1 and a bare id are still read. A test holds the roster to what
  the installed packages say.
- **A package that declares nothing gets the generic rule only** - no
  legacy table kept for old packages. The system reads as its id; a button's
  letter is the first usable character of its last word; an axis header is
  its initials. Published packages read that way until they are rebuilt.
- **Everything moved to the cores**, including the names no core used.
- **The PlayStation 3 disc layout left the engine** in the same round. The
  ISO writer detected `PS3_DISC.SFB` and wrote two fields into the system
  area; that is now a `media` recipe RPCS3 declares, and the engine writes
  whatever patches a recipe lists. The image is the same file, byte for
  byte, as the engine made before.

What a package declares is in docs/porting-a-core.md: `systemNames`,
`mnemonics` in each input declaration, `header` on each axis, `media`.
The engine resolves a control's letter (whole name, then the name without
its player, then the rule) and the frontend asks it - the window holds no table
and no rule of its own; a definition with no session behind it asks the
engine for the rule's answer.

Movies are not touched by any of it: an entry is read by position, and any
character but `.` is "pressed". That is also why the letters could be
corrected while they moved. Moving them found that seven keys had `.` or
`|` for a letter (a pressed `.` reads back as not pressed; `|` parts an
entry's groups), that the kana keys of one keyboard had letters outside
ASCII, which the engine's byte-wise entry cannot carry as one character,
and - once the uniqueness test read each MACHINE's controller rather than
only the package's - that 44 pairs of controls on one pad shared a letter
(the four C buttons of one pad were all `C`; L2 was the `L` of Left). All
are fixed in the cores' declarations. Every other letter is what it was.

Left where it is, and why:

- `GreenzoneSurvivesReload` still names cores in the frontend. It is a list
  of EVIDENCE, kept here on purpose (2026-09-14: a core's own declaration
  was not enough). Moving it is a different decision.

## Opposite directions are sent as pressed (user-decided, 2026-10-05)

The controller dialog had a U+D/L+R setting - Priority (the default),
Forbid, Allow - inherited from BizHawk, and an adapter in the input chain
that enforced it. It found its pairs by NAME: any button with "Up" in it
was held against the same name with "Down", and so for Left and Right. It
was the last place the frontend guessed something about a core's controls
from their spelling, and the guess was wrong wherever the words were not
directions: "Mouse Left" and "Mouse Right" could not be held together,
"Left Trigger" was held against the pad's Right.

The user's words: the setting is legacy, we no longer need it. It is gone
with its adapter, its enum and its config key (an old config that still
carries `OpposingDirPolicy` is read; the key is ignored). What is pressed
is what the machine gets, as Allow did. A machine whose pad cannot press
both ways is the core's to model.

The earlier note in this log about `joypad.set` and the SOCD filter is
history: there is no filter to pass through now.

## Reboot Core keeps the project (user-decided, 2026-10-06)

Reboot Core was a rom reload like any other, inherited whole. A rom load
ends by restarting the tools, and TAStudio's restart stops its movie and
starts another: the most recent project file if this was it, a blank one
otherwise. So a reboot inside a project asked whether to save, and then
replaced the project - a 70-frame run became an empty
`default.chimeraProject`. With the piano roll closed the movie was saved,
stopped and disposed instead.

The recovery session knew none of this. It went on holding the movie that
had been replaced, whose machine was gone, and the next request to keep
the work safe - a caught error, a core that stopped - read that machine's
GPU description from the engine with a session that no longer existed. The
process died (issue #196: Export Core Log offers a reboot, and a click in
the piano roll then raised the error that asked).

The user's words: reboot core should keep the project. So it does:

- The SAME movie goes on to the new machine - inputs, markers, branches,
  undo history, unsaved edits - queued before the load exactly as a
  project boot queues it, so the machine boots once, with the project's
  core and settings, and nothing is asked about saving.
- The machine is at power-on, paused there when the piano roll is open.
- The greenzone does not survive: the engine held it for the machine that
  is gone. The saved greenzone is not read back either - it is the states
  of the SAVED inputs, and the movie in hand may have been edited since.
  Branch states are files beside the project and are still there.
- Between the two machines the movie answers "nothing stored" itself
  (`MachineIsGoing`) rather than ask the old machine's history: a piano
  roll repainting in the middle of the reboot asks exactly that.
- A disposed core answers "" for its GPU description instead of handing
  the engine a null session.

Proved by three witness legs (the roll closed, the roll open, and a core
that dies after a reboot), each of which fails on the old path, and by a
unit test of the movie changing machines.

## A branch's state follows the greenzone's rule (user-decided, 2026-10-06)

Reopening an xemu or Flycast project and loading a branch said "This branch
has no saved state beside the project; replaying to its frame instead", and
replayed from power-on - every branch, every time (issue #186). The same
project had its greenzone.

Two rules had drifted apart. A branch's state was left out of the cache for
ANY machine a GPU drew, from when that state rode inside the project file:
a file people hand to each other, and a state a GPU drew is only good where
that GPU is. Since the PS3's four-gigabyte states (2026-09-17) a branch's
state is a file of its own in the cache beside the project, exactly as the
greenzone is - and the greenzone has been kept since 2026-09-16 for the
cores whose GPU-drawn states are known, by evidence, to reload in another
process, after a clean close.

The user's words: branches should follow the greenzone rule. They do: which
file is a branch's state is recorded, and read back, exactly when the
greenzone may outlive the session - a machine no GPU drew, or a core on the
evidence list; never from a session whose machine died; never read from a
session that did not finish. A core's own claim that its states survive
still decides nothing.

Measured before it was changed, on Flycast drawn through the GPU bridge:
one process played the 240p test suite to frame 700 and saved a state; a
second process loaded it and played 200 frames, and its picture was the
first process's frame 899 pixel for pixel (the control, frames 700 and 899
of one run, differs by 140 pixels). The frame right after the load draws
nothing, as after any load on these cores; TAStudio shows the branch's own
stored picture there.

What it does not change: a cache an older build wrote for a GPU-drawn
machine names no state files, so those branches replay once more and keep
their state from the next save. A state the engine refuses still falls back
to the replay, with the reason shown.

## The About box is six lines, and versions are copied from the Help menu (user-decided, 2026-10-07)

Issue #193 is the user's own specification of the About box, and it is
exhaustive: the logo, small and centred; the name; the build, as its commit
(a link to that commit) with the commit's date and time; a link to the
repository; a link to toolAssisted.run; OK. The tagline, the credits link,
the BizHawk line, the build configuration and the button that copied the
commit hash are gone. Credit is where it was and still is: CREDITS.md.

The build's time is the commit's, in UTC, read from git when the frontend
is built (`GIT_COMMITTIME`, beside the day `GIT_SHORTDATE` the version-skew
warning compares). Never the build's wall clock: a build must reproduce.

Issue #188 asked for the running core's version to be copyable, next to
Chimera's in the About box. The About box now has no room for it by
decision, and the button it would have sat beside is one of the things
removed. So the copying moved: Help > Copy Version Info puts two lines on
the clipboard, in the words the bug report template asks for them -

    **Chimera build:** Commit c7c06d7d5 (2026-10-06 16:55 UTC)
    **Core and version:** RPCS3 2026-10-07 06:35  (1924ad8b)

- the second only while a core is running, written as the Core Manager
writes it. Where it went is the implementer's choice, not the user's: the
user decided what the About box holds, and this is what followed from it.
The text is built in `VersionReport` (tested); the menu item only asks the
running core which package it is.

## A property table can be dynamic: a Flash movie's variables, by name (user-decided, 2026-10-07)

Issue #216 asked how to watch a value in a Flash game: a movie has no RAM,
and the Ruffle core published no memory at all. Two things were decided, in
this order.

First the bytes (the user: "the quickest solution to expose the data"): the
core's heap is a bus, `Heap`. Every tool that reads memory reads it, and a
number a movie keeps can be searched for and poked. That is enough to work
with and awkward to live with - a number is a double, three copies of it are
on the heap of which one is the variable, and the variable moves.

Then the names. The user proposed a tool for Ruffle alone: a dialog of every
variable, its type and its address, and a way to add one to the watch list.
What was built is that dialog as the one Chimera already had - RAM Watch's
Add Game Properties - by letting a core's property table be DYNAMIC, and the
user agreed to it ("yes, build it that way, AS1/2 first"). The reasons it is
not a Ruffle tool: Chimera holds no system-specific code; the engine already
reads, shows and parses every type a variable can have, which a byte watch
cannot (RAM Watch has no 8-byte watch, and a movie's numbers are doubles);
and Lua, freezes, the OSD and watch files then work on a variable with
nothing written for them.

What "dynamic" had to mean, since a variable is not a place:

- a property is known by its NAME, and where it is gets asked before every
  read and write (`GetGameProperty`); its index in the engine's table never
  changes, so a watch holds on through a move and through an absence;
- the list is the core's answer when asked (when the dialog opens, on
  Refresh, on `game.list()`), never kept up behind the user's back;
- a property can live on a bus, since a heap is one;
- and the rule that carries the weight: ANSWERING MUST NOT CHANGE THE
  MACHINE. Opening a dialog is not input. The core's walk allocates nothing,
  calls nothing of the movie's, and writes its answer outside every state;
  the gate proves it by counting allocator calls (0) and comparing the heap
  before and after (identical), with a control that allocates 24 bytes and
  is caught on both counts.

Decided by the user in the same message: RAM Search is NOT changed. "It's
meant for byte-based operations - searching over game properties isn't
useful and a pain to implement." It searches the Heap bus as bytes like any
other memory.

Scope, stated so nobody assumes more: ActionScript 1 and 2 only - a movie in
ActionScript 3 lists nothing yet. Numbers, booleans and strings held by
clips, by objects and arrays reachable from them, and by `_global`; a clip's
`_x`, `_y` and `_currentframe`. Strings and the clip values are read-only (a
string is shared and never changed in place; a clip's position has caches
behind it). A property with a getter has no place and is not listed. A write
is a poke: the bytes change and the movie's code is not told.

## LICENSE is the MIT text alone; the rest is in NOTICE (user-decided, 2026-10-07)

`LICENSE` opened with three paragraphs - that Chimera is a fork of BizHawk,
whose copyright is whose, and what in the tree is not under the MIT terms -
and only then the MIT License. Nothing in it was wrong, and GitHub read the
file as "Other": a licence it could not name. That is the first thing anyone
checking the project's terms sees, a person or a program (it came up while
looking at what signing the Windows binaries would need: a free signing
service for open source asks for an OSI-approved licence, and "Other" is not
an answer to that).

So the file is split, and not one word of the terms changes. `LICENSE` is
the MIT License with its two copyright lines. `NOTICE` is the paragraphs
that stood in front of it, as they were, ending with where the terms are.
A bundle carries both (`licenses/chimera-LICENSE`, `licenses/chimera-NOTICE`).

miniBox's LICENSE had the same shape and read as "Other" too, and was split
the same way the same day (miniBox dcb1f22): LICENSE the MIT text with its
three copyright lines, NOTICE the summary of whose work each part is. No
core package ships that file, so no package changes; and Chimera's pin of
miniBox is not moved for it.

## Chimera reaches for nothing over the network (user-decided, 2026-10-07)

The user's words: "Chimera should download nothing from the internet (not
even the core list or their metadata, not even information from github).
Chimera should have no internet reaching logic at all anywhere. The new use
case is: users must download or build cores themselves and place them in the
cores folder." And, an hour later, of the scripting functions that had first
been left alone: "do remove all internet reaching logic from Chimera, even
the Lua script functions."

This reverses the Core Manager as it was designed (docs/core-manager.md has
the model that replaces it). What went:

- the Core Manager's feed, installer, release-index reader and the cache of
  what cores had published; its Check for updates, Download latest, Install
  and Add external core; the development-build channel;
- the roster. `official-cores.json` is not shipped and not read. Asked whether
  a static list should stay in the app, the user chose none: a list that is
  never refreshed is wrong the day after it is made, and where cores are is
  something a person reads with a browser;
- from Lua: `comm.socketServer*`, `comm.http*`, `comm.ws_*`, and the
  `--socket-ip`, `--socket-port`, `--socket-udp`, `--url-get`, `--url-post`
  flags. `comm.mmf*` stays: a memory-mapped file is on this machine;
- LuaSocket, from the build and the bundle (the submodule is gone).

What a click on a link does - the About box, Help - is not Chimera reaching
anywhere: it hands an address to the system's browser.

Where cores go, in the user's words: "use Cores/, already coming empty on the
distribution. The user may define a different Cores/ folder, if they wanted
to." So there is ONE cores folder, `Cores/` beside the executable unless the
config's `CoresFolder` names another, set from the Core Manager. The data
directory's `Cores`, where downloads used to land, is no longer searched.

Three things here are the implementer's, not the user's, and are the kind of
thing to reverse if they are wrong:

- **Somebody updating is told where their cores are.** Every existing install
  has its cores in the data directory, which this build does not search. The
  Core Manager says how many packages are there. Nothing is moved for them,
  and nothing is searched silently: the alternative was a Chimera that opens
  after an update with every core apparently gone. (It first had a button of
  its own, "Use that folder". The user, having tried it the same day: no
  button; Change folder... does that - so the picker opens on that folder
  while packages are there.)
- **Remove still deletes, and only files.** Removing was never fetching, so
  it stays, with a confirmation that now says Chimera cannot get the build
  back. It deletes package FILES in the cores folder; an unpacked package (a
  folder, which is how somebody keeps their own build) and a package in a
  further search directory are named and left alone.
- **A tripwire, not a proof.** `tools/check-no-network.sh` fails CI if the
  frontend's or the engine's sources name a network API, or if the native
  build names a network library. It reads names in source, so it catches the
  ordinary way such code comes back and nothing cleverer.

Left as they are, and worth knowing:

- The cores still publish a `releases.json` index beside their packages. This
  Chimera does not read it; the Chimera people already have does, so it is
  still written.
- CI still fetches the published packages (`tools/fetch-cores.sh`) to check
  the frontend against them. That is the build's network, not Chimera's.
- A Lua script can still load a native module of its own from the Lua folder.
  Chimera ships none.
- A package from before packages stated their date is listed without one:
  the lookup was in the record of what cores had published, and there is no
  such record now.

The window's words are the user's, from trying the staged build that day:
**Refresh List** (it was "Look again"), **Show Systems...** (it was
"Systems..."), and the folder chosen with Change folder... "is remembered for
the next time" - it is written to the config when it is chosen, not when
Chimera closes.

## What a Flash core asked of the property tables (user-decided, 2026-10-08; chimera#218)

The second core to list a game's variables by name (Flash Player itself, under
the sandbox) came back with seven requests. Each was put to the user; three
were taken, and one was a question with an answer.

- **A dynamic table's names are exact** (decided: "exact case for dynamic
  tables"). The engine found every property by its lower-cased name. That is
  right for a fixed table, whose names are a core author's labels, and wrong
  for a game's own: ActionScript 3 tells `score` from `Score`, so one of the
  two was dropped as "named twice" and a lookup in the wrong case read the
  other. `CeGameProperties::key()` is the one place that decides: lower-cased
  for a fixed table, as written for a dynamic one. The picker's ticks were
  keyed without case too, and are not now. A core whose game folds case
  answers for whatever spelling it is asked.
- **A bus says which of it is live** (decided: "yes, add live ranges"). A
  bus's size is read once, so a heap arena is a gigabyte whatever is mapped,
  and RAM Search's first passes - a bit and a byte per address - cost 1.1 GiB
  and a read of all of it per pass. `GetBusRanges` in the core,
  `ce_session_bus_ranges` in the engine, `ce_ramsearch_set_ranges` on a
  search. The search lays the live ranges end to end and works on that
  compacted domain exactly as it did on a whole one; addresses are the real
  ones wherever they leave or enter. Three rules fell out: ranges are kept
  whole to four bytes, so an aligned candidate stays aligned; no candidate
  spans two ranges; and the ranges are those of the moment the search started
  - memory mapped later is not part of a search already begun, the way memory
  that did not exist could not have held the value being looked for. RAM
  Search stays what it is: bytes at addresses (it was not taught about
  properties, 2026-10-07).
- **A domain can be hidden** (decided: "do the hidden domain only", of four
  requests about values with no place of their own). A string the game keeps
  in pieces has to be copied where the engine can read it, and that copy
  showed in the Hex Editor and RAM Search as if it were memory. The core marks
  the domain (`GetMemoryDomainHidden`); the frontend's domain list finds it by
  name and never lists it. Not taken: a value carried inline in the table, a
  kind for null and for an object reference, a write by name
  (`SetGameProperty`), and listing by group. Each is a change to what a
  property IS - today, a typed place - and none was needed for the tools to
  work; they stay open on the issue.
- **Which stack a between-frame export runs on** was a question. The answer
  is miniBox's and was already written down: every host-called export runs on
  the guest's one main stack, which is invisible - in no savestate, never
  cleared. So a machine that reads stack it did not write is not reproducible
  across a state load whether or not a tool ever asked it anything, and a core
  wrapping such code gives its exports a stack of their own, as that core
  does. docs/porting-a-core.md says so now. Whether the host should switch
  stacks for every core instead was not decided, and nothing changed there.

The synthetic core carries all three (a hidden Mailbox domain, `wanderer`
beside `Wanderer`, three live pieces of its bus), so the witness holds them:
legs E:dynamic-properties, E:bus-ranges and the game-properties script. The
search itself is held to its oracle - the same random script, over the live
third of a domain, against a model that keeps a record only where a whole
candidate is live.

## Which emulator a core builds is in the README (user-decided, 2026-10-08; chimera#192)

Asked for by a user who could not tell whether a fix upstream had reached the
core he runs. The README has a second table now - core, emulator source, the
commit it is pinned at, that commit's date, and the nearest release where the
core's checkout knows one. It is written by `tools/core-versions.py` from the
cores' own repositories (the submodule their `package-licenses.json` names
first; a roster row may name another, as quickerNES's does), not by hand, and
it says the day it was written: a core moves its pin without telling Chimera,
and the core's repository is what is true today. Nothing in Chimera reads it.

## The pictures of frames already drawn are kept, and shown after a load (user-decided, 2026-10-08; chimera#190)

A renderer on the far side of the GPU bridge cannot be rewound with the
machine. After a state load it rebuilds its textures and buffers as the game
touches them, and until it has, the picture read back is wrong - on the
reporter's machine, pieces of the frame from before the load, for a moment,
every time an input was changed (chimera#187). Nothing in the machine is
wrong, and nothing can make the driver hurry. The request was to keep a
compressed picture with the greenzone and show that; the user said yes, for
every core a GPU draws for.

What was built, in the engine and nowhere else:

- **A session keeps the picture of every frame it shows**, by frame,
  compressed, in a budget of its own (an eighth of the greenzone's, between 32
  and 256 MiB; least recently used first out). It is on by itself for a
  session a GPU draws for, from the moment its greenzone is enabled;
  `ce_session_greenzone_pictures` chooses otherwise. The frontend was not
  touched: a frame advance hands back the picture it always did, and for a
  while after a load that picture is the kept one.
- **For the first `settle` frames shown after a load, a frame that has a
  kept picture shows it** in place of what was read back. The settle is 3
  frames, and a setting (the greenzone window of the cache manager, 0 to 10;
  0 is never): the first build said 60, and the user's answer on seeing it
  was "2~3 frames at most, configurable ... anything above that is extremely
  long, and prevents a user from observing the results of their changes"
  (user-decided, 2026-10-08). While the settle lasts, what the core has just
  drawn is not what is on the screen, and that is the thing somebody editing
  a movie is looking for. Frames shown, not
  frames run: the first build counted every frame, so a seek that replayed
  more than 60 unseen ones made the very first picture drawn after the load a
  trusted one - shown, and kept - on a core that does not draw through a
  seek, which is exactly when it is wrong (found the same day, answering a
  question about the rule; the witness has that case now). A restore onto such a frame
  shows it at once - a load draws nothing by itself, which is why a seek used
  to leave the old picture on the screen. Past the settle the picture read
  back is shown, and replaces the kept one: a renderer can be late the first
  time too (a shader still compiling), and the later picture is the better.
- **A picture drawn within the settle is never kept.** It may be the wrong
  one, and a wrong picture kept would be shown from then on.
- **An edit forgets the pictures past it** (`ce_session_greenzone_invalidate`).
  They were drawn for input that is no longer the movie's.
- **Packing is on a helper thread** (work_thread.hpp, by its rules). A frame
  pays for a copy - 0.1 ms at 640x480, 0.8 ms at 1920x1080, measured - and
  the helper packs it; three pictures behind, it is given no more. Pictures
  above 16 MiB raw are not kept at all.

What it cannot do, and the issue was told so: **draw a frame nobody has
seen.** After an edit the frames past it are new. They have no picture, they
are drawn inside the settle of the load that the edit caused, and what they
show is what the core draws - wrong for a moment on a card that does that.
Stepping back and forward over frames already seen is what this makes clean.

Pictures are in no savestate, history file or movie, and take no part in
what the machine does: a session with none shows what the core draws, as it
always did.

Proved on the synthetic core, which pretends to be a renderer that is wrong
for five frames after every load (its `garbleAfterLoad` setting; witness leg
E:kept-pictures): without pictures the frame shows the garbage, with them the
picture it had, by replay and by restore alone; past an edit, what the core
drew, and nothing kept. On a real bridge session (Flycast, through llvmpipe)
it switches itself on and changes nothing that is shown. NOT proved: the
effect on the card and the core it was asked for (xemu on an NVIDIA card),
and whether 3 frames is enough for it to recover there - llvmpipe never
draws wrong after a load, so this machine cannot show it.

### What a real card shows (measured 2026-10-08)

That last sentence was wrong about the machine: its Windows side has a GTX
1060, and the user pointed at it ("can you not use my 1060"). Measured there
with xemu, the core of the report, two ways: the headless runner with
`--settle-probe` and a state on every frame, so that a load lands on the
frame before the ones compared; and the published frontend with TAStudio
driven by a script, every screenshot compared with the picture the same frame
had when nothing had been loaded.

- **Soulcalibur II, its demonstration fight (a new picture every frame).**
  The first frame drawn after a load shows the picture of the frame the load
  landed on - the previous frame's, bit for bit, not garbage. From the second
  frame on every picture is identical to the first pass.
- **Prince of Persia: The Sands of Time, its pause screen.** Five frames
  after a load are wrong - first what RAM last held at that address
  (leftovers of the boot logo), then black - and the sixth is right, bit for
  bit.
- **After a seek** (50 and 110 frames run unseen between the load and the
  first frame shown) the first picture shown is right in both games. xemu has
  no way to be told not to draw, so its renderer recovers by frames RUN,
  seen or not.
- **TAStudio, window at 3 (the default), frames already seen:** going back a
  frame at a time shows the right picture every time (7 of 7). With the
  window at 0 the same steps show the previous frame's picture whenever the
  load landed exactly one frame before (3 of 3), and the right one when two
  frames ran (3 of 3).
- **An input changed one frame behind the cursor:** the frame after the edit
  has no kept picture and shows the one of the frame before it. The change is
  not on the screen until one more frame is run.

Why, in xemu: what the GPU drew is not in the state. Under TCG a surface is
written into the machine's RAM only when the processor reads it; upstream
writes them all out before its own savestates (`pre_savevm`), and a sandbox
snapshot runs no such hook (the same gap as chimera#43). A load throws the GL
objects away and what was drawn and not yet read goes with them: the buffer
about to be flipped in the first game, a chain of them in the second. With no
surface to scan out, the picture handed over is the VGA view of RAM, and that
is taken at the START of the frame.

Two things this says about what was built (the user's answer follows in the
next section):

- 3 frames covers the first game and not the second.
- The rule keeps a picture read back past the settle, in place of the one
  the frame had. In the second game frames 4 and 5 after a load are past a
  settle of 3 and still wrong, so the wrong picture replaces the right one
  and is shown from then on. A settle long enough for that game is longer
  than the user allowed; the remedy that needs no window is for the state to
  hold what the GPU drew.

The instruments stay: `chimera-run --settle-probe` (witness leg
E:settle-probe holds it to a renderer whose answer is known) and
`CHIMERA_PICTURE_TRACE=1`, which prints the picture's sum at every load and
for the eight frames after it - two frames with one sum are one picture.

## A core is told before a state is taken of it (user-decided, 2026-10-08; chimera#190)

Put the three ways out - fix it in the core, lengthen the window, leave it -
the user chose: "fix it in the xemu core". A window hides a wrong picture
behind an old one; the wrong picture was there because the state was not the
whole machine.

**The engine** has a second optional export beside `StateLoaded`:
`StateSaving`, called before every state it takes - a greenzone capture, a
savestate, a branch's state file - and not for a frame the history decides
not to store. The history calls it at the point where a frame is certain to
be stored, before the delta or the anchor is read, so what the core writes is
in that state; the context it may have borrowed from the GPU bridge is given
back as after a frame. `CHIMERA_NO_STATE_SAVING=1` leaves cores untold, which
is the control every measurement below used.

**xemu** answers it by writing the surfaces its GPU has drawn into the
console's RAM (its patch 0019; the same thing upstream does before its own
savestates). A load already rebuilt every GL object from RAM; now RAM has
them.

Measured, same instruments as above:

- Boot animation through llvmpipe: the first frame after a load was black;
  now all six compared are exact. Untold, black again.
- Soulcalibur II on the GTX 1060: the first frame after a load is exact
  (untold: 90% of its pixels differ). Through the published frontend with
  TAStudio and the kept pictures switched off (window 0), every picture is
  right - going back a frame at a time, and the frame after an input changed
  behind the cursor, which showed the frame before it.
- Prince of Persia on the GTX 1060: untold, five frames of leftovers and
  black; told, frames 1, 3 and 5 on are exact and frames 2 and 4 differ
  faintly - most of their pixels by one level, half a percent by more than 8,
  none by more than 22. A render target the load threw away comes back as a
  texture read from RAM, and that is not sampled quite as the surface was;
  upstream's own loadvm does the same. Nobody could see it; it is written
  here because "exact" would be untrue.
- The machine does not notice. 300 frames told at every frame, at every
  seventh and only at the end leave the same 64 MiB of RAM and the same
  picture, byte for byte; untold, 9.6 MiB of that RAM is different - what the
  GPU drew, missing.
- It costs a capture about 5.6 ms on that card (640x480, colour and depth).
  The history weighs what a capture costs against a frame, so a run does not
  slow down - 2500 frames in 68 s told, 70 to 84 s untold, 52 s with no
  history - and stores a state every 4 frames of the 3D part where it stored
  one every 1 or 2.

What this leaves of the kept pictures: for xemu, nothing to hide. They stay
for every core a GPU draws for, with the 3-frame window; what the other cores
do after a load on that card is the next section.

Witness leg E:state-saving holds the engine to it on the synthetic core,
which notes the frame it was last told on in memory a state carries: never
told without a history, 71 times with a state on every frame and 15 with one
in five, and back on frame 32 from the state of frame 30 the note says 30 -
told before the state was read. xemu's gate has gl:picture-after-load, with
the untold run as its control.

### The other cores a GPU draws for, on the same card (measured 2026-10-08)

Asked for by the user once xemu was fixed. The published build of each core
(its Development build of that day), the headless runner on the GTX 1060,
`--settle-probe <frame>,12` with `--greenzone-period N` and the frame a
multiple of N, so the load lands on the frame before the twelve compared
(the trace says where it landed, and it did every time). Each scene was
picked from a sheet of screenshots, and the probe now says how many of the
frames it compared differ from the one before - a verdict over a still
screen is not one.

| Core | Scene | The frames drawn after a load |
| --- | --- | --- |
| Azahar | Cars 2, its 3D menu (moves every frame) | exact from the first |
| Ruffle | Super Doggy, its animated title (every frame) | exact from the first |
| Vita3K | Geometry Wars 3, its title with glow (every frame) | exact from the first |
| Flycast | Re-Volt, the demo race | 1 an older picture, 2 and 3 black, exact from the 4th |
| PCSX2 | Street Fighter EX3, the 3D intro | 1 garbage, 2 to 4 off in 6 to 8% of pixels, exact from the 5th |
| Dolphin | Pro Rally 2002, the intro film | 1 exact, 2 and 3 black, exact from the 4th |
| Dolphin | Pro Rally 2002, the demo race | **the core dies** on the first frame after the load |
| RPCS3 | Dead or Alive 5 Last Round, a fight | 1 black; from 2 on the fighters are drawn black, and still are 12 frames later |

Flycast and PCSX2 give the same numbers through a whole state loaded back
(`--settle-probe-state`) as through the greenzone, to the hundredth of a
percent, so it is their renderers and not the history.

Three of these are not about a picture being late:

- **Dolphin dies after any load in that 3D scene** - through the greenzone at
  two different frames, and through a whole state saved and loaded at the
  same frame. "VertexManager: Buffer not large enough for all vertices!
  (2551574084 > 16777216)", then a jump into data. Not in the film that
  precedes it.
- **RPCS3 dies on a greenzone restore** of that fight with a state every 1700
  frames - a read one byte past its 4 GiB of guest memory - by the probe and
  by `--rewind-loop` alike, and survives a whole state loaded at the same
  frame. Whether a denser history does it too was not tried.
- **RPCS3's picture does not come back**: something the game draws once and
  keeps on the GPU is lost with the load, as xemu's surfaces were, and
  nothing redraws it.

For the 3-frame window: Flycast and Dolphin's film fit inside it; PCSX2's
fourth frame is past it, differs a little, and is kept.

### The two that died (user: "fix the dolphin crash, then the rpcs3 ones", 2026-10-08)

**Dolphin** was the core's own: the rebuild of its GL objects after a load
freed every vertex loader and left the machine's vertex format groups
pointing at them, so the first primitive ran a freed one. Its patch 0026
(core 5866b6b) sets the pointers to null and marks them for refresh; the race
survives both kinds of load and the twelve frames after each are exact.

**RPCS3's was not RPCS3's.** The restored machine was wrong, and neither
check said so: `CHIMERA_HISTORY_VERIFY` finishes every planned anchor at once,
which is the one thing that was not happening, and with it on the restore
was exact. Left to run as it does for a person, `--greenzone-check` found
1768 bytes in four pages of RSX memory stale - the anchor of frame 3400
exact, the delta from it to 5100 four pages short. The history plans an
anchor as a frame ends and opens the next frame's epoch while a helper
copies the pages; the plan's hold took those pages off the list the epoch
makes its holds from without marking them held, so once copied they were
owed to nobody, and the first thing that worked a protection out again -
RPCS3 giving a range of its video memory the protection it already had -
mapped them writable. What was written there was in no delta (miniBox
109bcaa, with a test that fails on the old code in both orders). It is the
sandbox's and so every core's; a guest that never re-protects its own memory
could still meet it when a plan is finished in the middle of an epoch. With
the default greenzone, a state a frame, the same restore did not kill the
machine here; whether that machine was exact was not checked before the fix.

`CHIMERA_PLAN_VERIFY=1` is what would have said it at once and stays: the
machine is copied whole just before each plan, and when the plan has been
filled the two are compared page by page.

**RPCS3's picture after a load** - the fighters drawn black - was the third,
and was what the GPU drew not being in the state, as it had been for xemu:
45 render targets in that fight, many drawn once long before, thrown away
with the rest by the rebuild. Asked where the copy should be made, the user
chose capture time (user-decided, 2026-10-08), and RPCS3 answers
`StateSaving` now (its patch 0050): each target's pixels are read into a
block of the core's own memory that the state carries, and after a load the
surface store is kept and every surface is given a new image and its pixels
back. It did not need the RSX thread, which was the proposal - reading an
image changes nothing the renderer believes - and it does not write into the
console's memory, which would have been less code: a game can read that, and
would have read different bytes depending on whether a state had been taken.

On the card: 45% of the picture's pixels differed after a load for as long
as was measured, and under 1% do now, on shadow edges, where it cannot be
seen; eight rewinds of 300 frames through the greenzone end on a picture
that is bit for bit a straight run's; told and untold leave the same 256 MiB
of main memory. The price is the largest any core pays for this: in that
fight some 26 MB of surfaces change every frame, so a stored state is about
25 MB larger and a run with the default greenzone takes 475 s where it took
411. Multisampled targets are not carried.

### The ones wrong for a few frames (user: "now fix the flycast and pcsx2 wrong frames", 2026-10-09)

**Flycast: three frames, two causes, none left** (flycast patch 0020). The
first frame after a load showed the picture of the moment the load was made
FROM, and the next two were black.

The black ones were an order of events. A frame is parsed, which finds its
textures in the texture cache, and then drawn from them; the check that
rebuilds the renderer after a load sat at the head of the drawing, so the
rebuild fell between the two and freed the textures the frame was about to
be drawn from. A game drawing thirty times a second kept that frame up for
two. The check runs before the parse.

The first one was something that is not in the machine: the picture of a
frame the game draws nothing in is the last frame the renderer drew, kept in
a framebuffer of the old context, and the read asked nothing about the
context. Flycast answers `StateSaving` with a copy of that frame in its own
memory, and the rebuild puts it back.

**A frame that has been read needs no copy**, and this is the part worth
keeping for the next core: the picture the frontend took is already in the
core's own buffer, a state carries that, and after a load a read that finds
no framebuffer leaves it standing. So the copy is made only of frames nobody
looked at - what a seek that still draws leaves in a greenzone - and a state
taken while somebody plays is exactly the size it was (the same 4059 states,
byte for byte the same size, told and untold). The measurement had hidden
this at first: a probe that reads every frame cannot tell a core that copies
from one that does not, and the control passed. Hence `chimera-run
--settle-probe-unseen`: the frames before the probe's are run as a seek runs
them, no picture read, so the load lands on a state of a machine nobody was
looking at. Witness leg E:settle-probe counts the pictures the synth hands
over, 38 against 78.

On the card, twelve frames after a load, each bit for bit the picture it
was: through the greenzone, a whole state, onto a drawn frame and an undrawn
one, at 1x and 3x, from states read and unread; untold, the first frame from
an unread state is wrong. Eight rewinds end on the picture and the three
memories of a run that never took a state. Not carried: the result of a
render-to-texture pass, which this core keeps on the card; a game that draws
one once and goes on using it would lose it until it drew it again.

**PCSX2: four frames, and what a scaled-up renderer cannot give back**
(pcsx2 `waterbox/gs-shadow.cpp`, its patch 0026). The first frame after a
load was an empty one and the three after it were off in 6 to 8% of their
pixels - in three games, a still screen included. A render target is a
texture on the card, and the GS's own memory under a frame buffer holds
nothing, because the hardware renderer writes it there only when a game
reads it; the rebuild after a load threw the texture cache away and made
targets again from that memory. Gone with it: the frame drawn and not yet
shown, the depth not yet cleared, and the four fields the deinterlacer keeps
- which is why it took four frames, and why a picture that does not move was
wrong too.

PCSX2 answers `StateSaving` the way RPCS3 does. Every target of the texture
cache, colour and depth, and the deinterlacer's textures are read into a
block of the core's own memory; after a load the cache's targets are kept,
every texture of the old context is let go before the device is opened
again, and each target gets a new one with its pixels. Upstream has a switch
that does the first half its own way - "read targets when closing", which
writes them into the GS's memory - and it was not used, for the reason
RPCS3's Write Color Buffers was not: a game can read that memory, the
renderer falls back on it when a target goes, and a run that took a state
would hold other bytes there than one that did not.

On the card, at the machine's own resolution: twelve frames after a load,
each bit for bit the picture it was, in all three games, through the
greenzone and by a whole state, from states read and unread; untold, the
first four are wrong, as they were. Eight rewinds end on the picture and the
EE RAM of a run that never took a state. The price: 8.7 MB of textures in
that scene, nearly all changing every frame, so a stored state is that much
larger and the history keeps a third as many (1136 against 3701 over 3700
frames); 40 s where it took 35.

**Above the machine's own resolution it is not exact, and cannot cheaply
be.** Drawn larger than the PS2 drew, a sprite's edge samples a texel past
what its texture was loaded with - upstream's upscaling lines - and what is
there is whatever the recycled texture held before: the history of a pool
hundreds of textures deep. A new context's textures held nothing. After a
load 0.12% of the picture differs at 2x and 0.03% at 3x, on its edge rows,
for as long as the scene lasts; before, at 3x, it was 0.34% once the four
bad frames had passed, which nobody had measured. Carrying the pool of spare
targets as well was tried, and moved those pixels without removing them. A
picture that depends on what a driver's texture last held is not one a state
can be asked to reproduce; the machine is the same either way.

**Dolphin: two black fields, and a batch of geometry nobody had missed**
(dolphin patch 0027). In a film the second and third frames after a load were
black. The EFB - the frame being drawn, colour and depth - is two textures
on the card; a game showing thirty pictures a second draws into it during one
field and copies it out to the XFB, which IS in the machine's memory, during
the next; a state taken between the two held nothing of it, and the rebuild
made the EFB again empty. Dolphin answers `StateSaving` with a copy of both
textures in its own memory and the rebuild puts them back.

It draws first whatever the vertex manager has queued. That was the second
finding: a state can be taken mid-batch, and the rebuild had been abandoning
the batch, because the buffer it was written into goes with the context.
Drawn before the copy, it is in the EFB a state carries. Whether drawing it
early is something the machine can see was the question, and the measurement
says no: a state on every frame leaves the picture, the system RAM and the
ARAM of 7900 frames of a race the same told and untold.

On the card the film is exact for twelve frames after a load, at 1x and 2x,
and black for two with the core left untold. A stored state is about 2 MB
larger in a 3D scene. Two things are still not right after a load in that
scene, and both are the texture cache's copies of the EFB and the XFB, which
are textures on the card as well: a few pixels in a thousand off by a few
levels in the first drawn frame, and above 1x one frame shown at the
machine's own size. Carrying those copies is the next step if it is wanted.

**It was wanted (user-decided, 2026-10-09), and there was a third thing under
it** (dolphin patch 0028). The texture cache's copies of the EFB and the XFB
are carried like the EFB: read into the same block before a state, each
named by its cache entry; after a load the entries stay, every GL object the
cache holds is dropped before the rebuild makes anything, and each kept
entry gets a new texture and its pixels. A copy of the XFB is carried only
above 1x - at 1x the picture is the machine's own memory and nobody looks at
the copy. The race is now exact for twelve frames after a load at 1x and at
2x, where at 2x its first frame had been the machine's own 640x448; untold,
both are back.

The third thing is not a picture. Dolphin defers writing a copy into the
machine's memory until the game says its drawing is done, and until then the
bytes are in a staging texture on the card. A state taken in between held a
machine whose memory was still to be written, and the rebuild began by
flushing - reading a staging texture of a context that was gone and writing
what came back into the machine. Nothing here was seen to go wrong by it; it
was found reading the code that had to be changed. The bytes are carried
too, and written when they would have been. Turning the deferral off would
have been two lines, and was not done: it moves when those bytes land in the
machine's memory, and every movie made so far was made with it on.

That is the general shape of all five cores now, worth saying once: **what
is on the card and in no state is found by asking what a rebuild makes again
from the machine's memory and whether that is the same thing.** A texture
decoded from memory is; a render target, a frame half drawn, a copy at a
higher resolution, a deinterlacer's fields, bytes waiting in a staging
buffer are not - and the last of those is the machine itself.

## Two strings a renderer holds at once are two strings (user-decided, 2026-10-09)

Found by the session building a new core, on its first run through the GPU
bridge: the renderer logged its GL vendor, renderer and version as the same
three characters. `glGetString` answers with a pointer into the driver, which
a guest cannot read, so the bridge copies the answer to the guest's side -
into one buffer per call, in the wrapper miniBox generates for every core.
OpenGL promises those strings are static, and a renderer that asks for three
and reads them afterwards read the last one three times. It was not new:
Flycast had been logging its version as its vendor on the GTX 1060, and
Dolphin keeps the three pointers and chooses driver workarounds by them.

miniBox bd8daaf gives every (name, index) a place of its own. What that
changes is which workarounds a renderer picks on a real card, in every
bridged core, and each would deserve its pictures measured again; asked, the
owner chose the fix in miniBox alone, each core taking it at its next
build, over measuring them one by one first. Two were looked at anyway
because they were at hand: Flycast, exact after a load as before, and
Dolphin - the one known to act on the strings - whose film is exact after a
load and whose 3D race gives the same picture and the same system RAM at
frame 7899 as the build before. xemu and Ruffle keep their generated wrapper
in their repositories and have it when somebody generates it again.

## A column may be headed by more than its letter (user-decided, 2026-10-09; chimera#225)

Asked for: mnemonics of more than one character, or ones a person picks, since
a keyboard's columns cannot be told apart by one. Of three ways - longer
headers only, longer mnemonics in the movie's text, headers a person edits -
the owner chose the first.

A mnemonic is one byte because an entry is walked a byte at a time and read
by position; widening it changes the movie's text, the format this log calls
frozen, in the engine and in every tool that reads a movie. What was wrong is
narrower than that: what a person READS at the top of a column. So a package
may now declare, beside `"mnemonics"`, `"headers"` - by the button's whole
name or its name without the player, as a mnemonic is found - and the engine
answers a button's header as the declared one or else its letter
(`ce_session_button_header_of`, and `ce_control_button_header` for the rule
alone). One to eight characters of printable ASCII with no space at either
end; anything else is not taken and the letter heads the column as before.

TAStudio heads the column with it and fills a pressed cell with it, so a cell
reads as its column does. Nothing else changed: the movie's text, the input
display over the picture and a pasted entry all carry the one character.

Measured with the synthetic core, which gives Select the header "SEL": asked
as a window asks (`chimera-run --controls`), Select answers `s` and `SEL` and
Start `S` and `S`, and a movie recorded with Select held has `s` in its text
and `SEL` nowhere. The contract tests refuse a header the engine would drop, a
header for a button that is not there, and one that two columns of a player
share.

## Plain language in commits, documents and replies (user-decided, 2026-10-10; chimera#239)

A community member said the commit messages and documents were painful to
read, and quoted "published with thirteen green legs" as an example nobody
outside the project could understand.

The owner decided: plain language everywhere.

- A commit subject says what changed in ordinary words. Its body is short:
  what changed, why, and how it was tested.
- Project terms are not used without saying what they mean. Prefer the plain
  word: "test" for "leg", "test script" for "gate", "passes" for "is green",
  "the upstream version a core builds" for "the pin".
- Documents are rewritten in this style as they are edited. Older sections
  stay as they are until somebody touches them.

The rules are in AGENTS.md, which is what an agent or a contributor reads
first.

## When the core stops, frozen addresses are switched off (user-decided, 2026-10-10; chimera#235, chimera#234)

Reported with the Flash core: a memory search with "Greater Than" on every
frame, then "Freeze Address" on what was left. The core stopped, and after
that none of the choices in the "core stopped" window worked - going back to
a safe frame and restarting from frame 0 both stopped again at once.

What happened: in the Flash core the searchable memory is the core's whole
heap, so the search had kept the renderer's own counters (they grow every
frame) and the freeze held them still. That stops the core. And a frozen
address is written on every frame, so it was written again right after each
recovery.

Decided: when the core stops while addresses are frozen, Chimera switches
every freeze off before anything is restored, and the window says how many
were switched off. They are not deleted; the user can freeze them again.
Freezing the emulator's own memory can still stop a core - that part is
expected, and is not prevented. A warning before freezing in such memory
was considered and not taken.

## The branch screenshot is 320 pixels, and the size is a setting (user-decided, 2026-10-10; chimera#237)

The picture shown when the mouse is over a branch's number was limited to
128 pixels on its longer side on 2026-10-02 (issue 183), because a full-size
Xbox picture covered the game. A user found 128 too small to compare with
the game.

Decided: the default is 320 pixels, and TAStudio's settings window has
"Branch screenshot size (pixels)" on its Misc page (64 to 4096). A larger
screenshot is shrunk to that size; a smaller one is shown as it is. The
branch's text is no longer printed under the picture - it is already in the
branch list.

## A Movies folder exists from the first start (user-decided, 2026-10-10; chimera#233)

A fresh install had no folder for projects, so it was not clear where they
go, and older projects could not be copied in before one had been saved.

Decided: Chimera creates the Movies folder at start if it is missing. It is
the folder Config > Paths calls "Movies", which is beside the program unless
the user changed it. "Open Project" starts in the folder of the project
opened last, and in the Movies folder when no project has been opened yet.
"Save As" already started there. No files are moved.

## A hole left by a branch load is filled when it is played through (user-decided, 2026-10-10; chimera#238)

Reported: after loading a branch, no greenzone states were stored between
the last frame the two branches share and the frame the branch was made at,
however often those frames were played. It also survived a restart.

Cause: loading a branch cuts the stored states at the last shared frame and
puts the machine at the branch's own frame. The frames in between were never
run, so nothing is stored for them. When they were played later, the engine
saw that a state of a later frame existed and treated them as a replay of
frames it already had, so it stored nothing. Editing a frame removed the
later states, which is why that worked around it.

Decided (of two fixes): the simple one. The engine remembers the hole when
a branch is loaded. When play enters the hole, the states stored beyond it
are dropped and storing continues from there, as after an edit. The frames
beyond the hole have to be played again to be stored again; nothing in the
movie changes. The exact fix - keeping the states beyond the hole and
joining the two parts - means inserting into the middle of the state
history, which only grows at its end today, and was not taken.

Limits, stated plainly:
- The hole is remembered in memory only. A project that was closed with a
  hole and opened again does not know about it; loading the branch again,
  or editing a frame in the hole, gets it filled.
- Gaps the memory or disk budget made by dropping old states are not holes
  in this sense and are left alone.

Tested with a new unit test (it fails without the fix, with the greenzone
set to every frame and to every 7 frames) and the existing randomized
history test, which loads branches.

## Azahar: internal resolution, with states that carry the renderer's surfaces (user-decided, 2026-10-10, issue 223)

On 2026-10-09 Azahar's internal resolution was measured and left out: at 2x
a savestate loaded around every frame gave a different run than no load.
The reporter said upscaling matters to them, and the owner decided to do
the work now.

Cause: after a load the core rebuilds Azahar's OpenGL renderer, and the
rebuilt renderer fills its surface cache from the console's memory. At 1x
that memory holds every surface exactly. Above 1x it only holds scaled-down
copies; the full-size pictures were textures on the graphics card and in no
state.

Decided: above 1x the core answers the engine's `StateSaving` call (added
for issue 190) by copying every surface of the renderer's cache into its
own memory, and gives them back to the renderer it builds after a load.
Nothing in the engine or the frontend changed; this is the existing export
used by one more core. All surfaces are kept, not only the upscaled ones,
because which surfaces the cache holds decides what it does next. At 1x
nothing changes. The details are in the core's docs/PLAN.md.

It is declared as a setting of the machine: the VRAM differs between
resolutions, so a movie wants the resolution it was made with.

Measured:
- Mesa llvmpipe, four games at 2x, 3x and 4x: a load around every frame,
  and a state opened in a new process, give the run that never loaded.
  Without the `StateSaving` call the run differs in the two games whose
  first frames show it (Dark Witch from frame 90, Cars 2 from frame 1650).
- GTX 1060, Cars 2 in a race, 2x and 4x: FCRAM, VRAM and the picture are
  the same after a saved state, after that state is opened in a new process,
  and after a load around every frame. At 2x twenty greenzone rewinds also
  end the same, and the twelve frames after a greenzone load are each the
  picture they were. Without the call the VRAM and the picture differ.
- Cost on that card at 2x: about 12 ms for each state taken in a 3D scene,
  and a saved state of 202 MB (238 MB at 4x).

Not tested: 3x on a real card, other drivers, and TAStudio itself.

## A setting has one declaration on every machine (user-decided, 2026-10-10)

Found through a pull request to the PCSX2 core (chimera-core-pcsx2 pull
request 1). That package declares the Boot ROM setting twice, once for the
console and once for the arcade boards, and told the two apart only by
`exposedWhen`. `exposedWhen` decides what a person is shown. What applies to
a machine is decided by a setting's `when` list, and neither declaration had
one. So both applied to every machine, and the later default won: a
PlayStation 2 project that named no Boot ROM got the arcade board's default,
was asked for no bios, and could not start. This had been so since the
arcade boards were added on 2026-09-20. Projects that record their Boot ROM
explicitly were not affected, which is why nobody reported it.

The core was fixed by giving each declaration its `when` list. The owner
asked for a test in Chimera as well, so that no package can do this unseen:

- `InstalledCorePackagesTests.ASettingHasOneDeclarationOnEveryMachine` runs
  on every installed package and fails when a machine is left with two
  declarations of one setting name.
- `TwoDeclarationsOfOneSettingAreToldApartByTheirMachines` shows, without
  any package, that the check finds the mistake and accepts the fix.

Measured: the check fails on the PCSX2 package published before the fix
("Development build 5b6a3ba5", naming `bios` on all four machines) and passes
on the fixed one ("Development build 5eeb211a"). All 30 published packages
pass. In the real frontend a PlayStation 2 disc opened with no Boot ROM
setting stops with PCSX2's "requires a BIOS" message before the fix and
boots after it; that check now lives in the PCSX2 repository's frontend test.

Known and not changed here: the engine builds its own default settings
without narrowing by machine at all (`session.cpp`), so the settings a guest
receives can still hold both declarations of a name; the guest reads the
first. The frontend sends every effective value, so this did not cause the
bug above, but a headless run that leaves a machine-scoped setting unset
gets the first declaration's default whatever the machine is.

## miniBox: four pull requests merged, and the frontend's own signals on Linux (user-decided, 2026-10-10)

Four pull requests from two contributors were waiting on miniBox. The owner
decided each pair was to be reviewed, tested and merged if clean. They are
merged (miniBox f890632) and Chimera's copy of miniBox now points there.

What came in:

- **7 and 8 (Randomno), for guests that opt into machine spec v3.** A fixed
  `mmap` over pages that are already allocated hands them back zero-filled,
  as Linux does. A thread that exits while every other thread is in a timed
  wait no longer ends the machine: the clock moves to the earliest deadline.
  For today's cores (all v2) nothing changes except one message: a machine
  whose remaining threads all wait for ever is now reported as the deadlock
  it is.
- **9 (P-AS), Linux.** A fault inside a guest's own fault handler is served,
  as it already was on Windows. Before, the kernel ended the process with no
  message.
- **10 (P-AS), Linux.** Signal handlers that were installed before miniBox
  started (the frontend's runtime's) are wrapped: when one lands on the
  thread that is running a guest, it runs with the host's thread pointer and
  on the alternate stack, not on the guest's.

Why 10 matters here: a frontend's runtime may stop threads with a signal,
and one can land while a thread runs guest code. Forced to, the frontend
running the PCSX2 core died with a segmentation fault in every run on the
old miniBox, and runs with the new one, its memory identical to the native
reference. The contributor met it unforced.

One thing was added to 10 before merging: the wrapper read the thread
pointer with `rdfsbase` on every signal, on every thread. On a host that
does not let programs use that instruction it would have been an illegal
instruction at the runtime's first signal. It now runs only where the probe
says the instruction exists, like the fault handler beside it. This machine
has the instruction, so no test here can show that fault.

Measured, with all four merged on top of main:
- miniBox's own tests: 18 of 18 on Linux; on Windows the 12 unit tests and
  the guest runner (standard, v3, probe off) pass. Each pull request's new
  test fails without its fix.
- Core test scripts against the merged host library: PCSX2 36 pass, 0 fail;
  Vita3K 60, 0; RPCS3 33, 1. Vita3K and RPCS3 are the cores with their own
  fault handler. RPCS3's one failure compares two native runs that do not
  use miniBox; it is the known problem with the disc on this machine.
- Chimera with the new miniBox: engine unit tests 21 of 21, the end-to-end
  test 73 of 73, interface tests 119 / 59 / 543 / 290 with none failed, and
  the engine compiles for Windows.

A fifth pull request, a port of miniBox to aarch64, was answered and not
merged: it stays a fork until there is an end-to-end proof - a working
aarch64 build of Chimera, a core compiled for aarch64, and a tool-assisted
run shown on a real aarch64 machine. Then it can come in as a separate
distribution of Chimera for that architecture, with its own cores.

Every core changes when it is next rebuilt against this miniBox. None was
rebuilt for this; the published packages keep working with the new host
library, which is what the three core test scripts above ran.

## A package that cannot be read says which file and what is in it (2026-10-10)

A user reported that the project wizard's compile step for an RPCS3 game
stopped with "waterbox.config is not readable JSON". That sentence is the
engine's, said by a compile session (a child copy of the frontend) when the
declarations file of the package it opened did not parse.

It could not be reproduced. The published RPCS3 packages hold valid JSON, and
on Windows with the published builds a compile session started without the
error for a disc game, for a package game with its licence, and six at once
on an empty data folder. Whatever the reporter's copy of the file holds, the
message gave nothing to work with.

So the message now adds where the file was read from, how many bytes it has
and its first sixteen bytes (text as text, anything else as \xNN), for
example: "waterbox.config is not readable JSON (C:/.../rpcs3-...: 2782
bytes, starting with \xff\xfe{\x00...". One finding along the way: an
EMPTY declarations file is reported differently ("package has no
waterbox.config"), so the reporter's file is not empty.

Tested by a new end-to-end check (`E:package:unreadable`): the synthetic
package unpacked, with its declarations saved as UTF-16. It fails without
the change and passes with it.

## Numbers in files do not follow the system's decimal separator (2026-10-10, issue 244)

This is the cause of the report in the section above. It came back as issue
244 with the missing detail: the reporter runs Linux, in a locale that
writes a decimal comma.

On Linux the frontend puts the whole process in the user's locale. The engine reads and writes JSON with a small C library that
uses the C functions `strtod` and `sprintf`, and those follow the process's
locale. With a decimal comma, `strtod` stops at the dot of "0.0", so the
RPCS3 package's declarations, which hold exactly one such number, did not
parse. The DOSBox-X and PCem packages hold "0.5" and would have failed the
same way. Writing was affected too: a fraction in a project's settings
would have been written as "0,5", which nothing reads back. Windows was not
affected, because there the C library stays in the plain C locale.

What changed:
- The JSON library is built with its own switch for this (`ENABLE_LOCALES`):
  it asks the locale for the decimal separator and translates, so JSON is
  read and written with a dot whatever the locale.
- The property table of a game core used the same C functions for the text
  of a fraction. It now uses conversions that have no locale
  (`plain_numbers.hpp`), so 1.5 is typed and shown as 1.5 everywhere.
- The headless runner has `--locale-from-environment`, which does what
  the frontend does at start, so a test can put the engine in the same position.

Measured:
- In the real frontend on Linux under a German locale, built in a scratch
  folder with `localedef`: an RPCS3 compile session stops with "waterbox.
  config is not readable JSON" before the change and compiles after it. In
  the C locale it always compiled, which is why this machine never showed
  it.
- New unit test `plain_numbers` and end-to-end check
  `E:locale:decimal-comma` (it builds the German locale itself). With the
  switch taken out again, both fail under that locale.
- Unit tests 22 of 22, end-to-end test 75 of 75, interface tests
  119 / 59 / 543 / 290 with none failed, Windows cross-compile.

Limits: a locale whose decimal separator is more than one byte is still not
handled by the JSON library (it looks at the first byte only). A project
file that was already written with "0,5" under the old build cannot be read
and has to be corrected by hand.

## RAM Search: Ctrl+A in a value box, and actions on a huge selection (2026-10-10, issue 224)

Reported as a freeze. The reporter found the trigger and the code confirms
it. Ctrl+A is the shortcut of RAM Search's "Select All" menu item, and a
menu takes its shortcut before the control that has the keyboard sees the
key. So Ctrl+A typed in the "Specific Value" box selected every address in
the list, not the text of the box. A new search lists every address of the
domain, so that was millions of rows. The next action on the selection then
ran over all of them: a double-click adds every selected address to RAM
Watch, Ctrl+C copies every one. The window stopped answering.

What changed:
- Ctrl+A in a text box selects the text of that box.
- Add to RAM Watch, Poke, Freeze, Copy and Open in the Hex Editor ask first
  when more than 1000 addresses are selected, and say how many.
- Select All asks first when the list holds more than a million addresses,
  because the list keeps one entry for every selected row.
- The context menu no longer looks at every selected row each time it opens
  when many are selected.

Tested with three new tests of the two questions (`SelectionGuardTests`);
one was broken on purpose and failed. Not tested: the keys pressed in a real
RAM Search window.

Still open in issue 224: the reporter lost about 1000 frames of input when
the frozen program was closed, which the input journal should have kept.
That part is not explained.

## Core texts are written in plain language (user-decided, 2026-10-10, issue 241)

A user writing a tutorial found the descriptions of settings and of the
kinds of file a project takes hard to read. The owner asked for every
core's text to be rewritten, all 30 public cores, one by one. This was done
on 2026-10-10: each core repository has one commit titled "settings and
file slots are described in plain language (chimera#241)".

The rule for a description a user reads in Chimera (a setting, a firmware
entry, a machine preset, a file slot):
- short sentences, and few colons and semicolons;
- no project term without saying what it means;
- every fact of the old text is kept;
- the same fact is said the same way everywhere, for example "It is part
  of the machine, so a movie needs the same value." and "It changes the
  picture only.".

How each core was checked: a script compared the declarations with the
previous commit with every description blanked out (they had to be
identical), the core's generator was run again where there is one, and
Chimera's package tests were run on the published package with the new
declarations put in (15 of 15 for every core). No core was rebuilt by hand
and no core's own test script was run, because only text changed.

## No achievement services (user-decided, 2026-10-10, issue 242)

RetroAchievements support was asked for and declined, for two reasons the
owner gave. Chimera has no network code at all, by decision (2026-10-07):
nothing is uploaded or downloaded. And Chimera is a tool for tool-assisted
runs only, not for real-time or casual play; achievements earned with
tools would be of doubtful value to the service that awards them.

## Help > Report an Issue (user-decided, 2026-10-10, issue 243)

Asked for: a wizard that gathers everything a bug report needs into a zip.
Decided: a simpler form. One menu item, Help > Report an Issue, opens a
window with three things, each with a button that copies it:
1. the address of the report form on GitHub, and a button that hands it to
   the system's browser;
2. the first five lines of the bug report form, filled in from the session:
   the Chimera build, the running core and its version, the operating
   system, the graphics driver, the open project's files with the kind each
   was given as, and the running core's settings that are not at their
   default;
3. the files the form asks for that exist on this computer: the saved
   project, the newest crash note with the memory dump beside it, and the
   sandbox's log (`minibox-diag.log`).

Why not a wizard: the text is the part people got wrong or left out, and a
window with the text ready to copy gives it in one step.

The zip (user-decided later the same day). The first version left the zip
out, because it has to be attached by hand anyway. The owner then asked for
it: a button, "Save as Zip...", under the list of files. The list has a tick
box on every row, all ticked at first, so a person can leave out what they
do not want to share. The zip holds:
- `report.md`, the text of part 2, always;
- `game.png`, the game's picture as it was when the window opened, when a
  game is running;
- every ticked file under its own name: the saved project, the newest crash
  note and its memory dump, the sandbox's log, and the core's log while
  Tools > Export Core Log... is on.
A file that cannot be read is left out and named under the list. A zip
larger than the 25 MB GitHub takes is said to be too large. It is still sent nowhere: the person
attaches it to the report.

What it does not do:
- It sends nothing. The link is opened by the system's browser, the same
  way Help > "Open toolAssisted.run in Browser" does it.
- It takes no pictures of Chimera's windows or of the screen. The only
  picture is the game's own, and only when its row is ticked.
- It names the graphics card: the one a core drew on through OpenGL in
  this session, in its driver's words, else the one Chimera's window
  draws on (owner, 2026-10-31, issue 427: see below). When neither is
  known the line carries a note asking the person to type it.
- It lists file names and slots, not folders. The list of files to attach
  shows full paths, and that list is not part of the text to paste.
- No setting is known by name: the list is every setting whose value
  differs from what the core declares as its default.

The text only formats what the session already knows, so it is interface
code. Its labels are the ones in `.github/ISSUE_TEMPLATE/bug_report.md`; when
that form changes, the text changes with it.

Tested: 9 tests of the text, the settings comparison, the file list and
the zip, 3 of the window and a screenshot in both themes. The settings comparison and the tick boxes were
each broken on purpose and their tests failed. Interface tests after a full
rebuild: 119 / 59 / 552 / 302 with none failed. The same 12 tests pass on
Windows, run there from a copy of the test build. Not tested: the buttons
that use the clipboard, the browser and the save dialog, by hand.

## SRB2 joins the roster, the first core published outside the organisation (user-decided, 2026-10-10)

The owner asked for the Sonic Robo Blast 2 game core to be added:
https://github.com/P-AS/chimera-core-srb2. It is written and published by
P-AS in their own repository, not under ToolAssisted-run.

What changed here:
- `official-cores.json` has a row for it (`"repo": "P-AS/chimera-core-srb2"`,
  `"kind": "game"`), and the README lists it with the game cores and says
  who publishes it.
- That repository publishes two packages in each release, one for x64 and
  one for arm64 (`srb2-<version>-x64.chimeraCore`, `...-arm64.chimeraCore`).
  This Chimera runs x64 guests only, so `tools/fetch-cores.sh` now leaves a
  package whose name ends in `-arm64` out of the folder it fills. Two
  packages of one core in a folder would list the core twice.

What follows from a row that points outside the organisation:
- Chimera's CI downloads that core's newest dated build on every run and
  checks it with the package tests. A broken build published there fails
  Chimera's CI until it is fixed there or the row is taken out.
- Release notifications are a setting of the core's own repository. Only
  its owner can add it, so it was asked for in an issue on that repository.

Tested: the published build `Nightly 2026-10-10` of the core, fetched with
`fetch-cores.sh --kind nightly --core srb2` (the x64 package only arrives):
package tests and roster tests, 19 of 19. The package tests also pass with
both packages in the folder. Not tested here: starting the game. The core's
own CI runs its tests on the game's data and Chimera's package tests before
it publishes.

## A machine may state its own sound rate (user-decided, 2026-10-10, issue 226)

Reported: sound problems in Time Crisis 4 and Tekken 5 Dark Resurrection,
both on the Namco System 256 boards of the PCSX2 core. The reporter gave
two videos and no description. Neither game is on the test machine; Super
Dragon Ball Z, another System 256 game, is.

What was measured, with the core run on its own for 600 frames at 59.94
frames a second (48000 samples a second would be 480480 samples):
- System 256: 637952 samples, 63,700 a second.
- Super System 256: 717760 samples, 71,700 a second.

Cause. Those boards run every clock faster than a PlayStation 2, by 4/3 and
by 3/2, and the sound chip counts its samples on that clock: 49.152 MHz
over 768 is 64000, 55.296 MHz over 768 is 72000. The arcade fork the board
code comes from says the same (its console sample rate is the clock over
768). The game agrees: started with a coin, Super Dragon Ball Z programs
voice pitches 0x0C00 and 0x0480, which are 48000 Hz and 18000 Hz on a chip
at 64000 and would be 36000 Hz and 13500 Hz on one at 48000. So the samples
were right. What was wrong is that a package could state one sound rate,
48000, for all its machines. Chimera played a third to a half too many
samples at the console's rate: too low in pitch, and more sound than the
frames have time for, so parts were dropped.

Decided: a machine may state its own rate. `machines[].audioRate`, in Hz,
is optional; a machine that says nothing has the package's `audio.rate`.
The other choice put to the owner was to convert the sound to 48000 inside
the core, which needs no change here but converts the sound twice.

What changed here: a machine's rate is read from its declaration, and the
adapter builds its converter from that rate. The engine does not use the rate and is unchanged. A new
package test refuses a rate no sound chip has (below 8 kHz or above 192
kHz), for the package and for each machine.

Tested: two new tests of the declaration and two of the package check; the
rule was broken on purpose and its test failed. Interface tests after a
full rebuild: 119 / 61 / 554 / 302 with none failed. Package tests on the
30 published packages, dev and dated builds: 17 of 17. With the PCSX2
core's new declarations, Chimera reads 48000 for the console and System
246, 64000 for System 256 and 72000 for Super 256. Not tested: listening to
a System 256 game in the interface, and the two games of the report.

## The folder a core package is unpacked into has a short name (user-decided, 2026-10-11)

Found while testing issue 236: on Windows the xemu package would not load
("Could not find a part of the path") when the data folder's path was long.
Windows refuses a path of 260 characters or more. A package was unpacked
into `UnpackedCores/<id>-<commit>-<sha1>`, 86 characters for a core with a
four-letter name, and into the same name plus 17 more while it was being
unpacked. The xemu package holds a licence file with a name of 76
characters. With the default data folder that left room for a Windows user
name of about 24 characters; a data folder moved to a longer path failed.

What changed:
- The folder is `UnpackedCores/<id>-<sha1>`: the commit in a published
  package's file name is left out, 41 characters fewer. The hash of the
  file already names the folder exactly.
- The folder used while unpacking is `UnpackedCores/tmp-<12 characters>`,
  never longer than the one it becomes.
- The folder holds a small file, `unpacked-from.txt`, with the package
  file's name. The cleanup of older contents of the same file used to go by
  the folder's name and now goes by that file. A folder with the old long
  name is removed the first time its package is loaded again.
- When unpacking fails, the half-written folder is removed.

Not chosen: asking Windows for long paths. The engine and the sandbox open
the package's files with ordinary calls, which have the same limit.

Tested: 3 new tests (`UnpackedPackageFolderTests`); with the rule switched
off all 3 fail. On Windows, with a data folder path long enough to fail:
the published build 2385e5ea refuses the xemu package with the message
above, and the same build with the fixed library loads it. Interface tests
after a full rebuild: 119 / 61 / 557 / 302 with none failed (557 was 554).

Tested: 7 new tests of the text, the settings comparison and the file list
(`IssueReportTests`), 2 of the window (`ReportIssueFormTests`) and a
screenshot in both themes. The settings comparison was broken on purpose
and its test failed. Interface tests after a full rebuild: 119 / 59 / 550 /
296 with none failed (550 was 543, 296 was 293). Not tested: the window on
Windows, and the buttons that use the clipboard and the browser.

## A Vulkan bridge (user-decided, 2026-10-10, issue 171)

A core may now draw on the machine's graphics card through Vulkan, as two
cores do through OpenGL. The request that brought it was an Xbox 360 core:
its emulator (Xenia) draws with Vulkan or Direct3D 12 and has no OpenGL and
no software renderer, so without a bridge it could show nothing. The owner:
"Developing a Vulkan bridge for Chimera is something I wanted us to do
anyway."

What changed in what Chimera is:

- **One more export a core may have: `SetVulkanBridge`.** It is offered
  under exactly the conditions `SetGpuBridge` is - the project chose a
  renderer whose value ends in `-hw`, this build has a bridge, the machine
  has a driver - and a session that handed it over says, as an OpenGL one
  does, that a GPU drew. No setting, no declaration and no name is new: the
  `-hw` convention already says "this renderer wants the card" without
  saying which API.
- **Nothing system-specific.** The engine does not know which core asks or
  why. The bridge itself, both halves and its list of commands, is
  miniBox's; the engine compiles miniBox's host half and calls three of its
  functions.
- **No network, no new dependency.** The host half links against nothing
  and finds the Vulkan loader at run time.

What it costs is what the OpenGL bridge costs, said in docs/gpu-bridge.md:
the device is outside the sandbox, so it is outside the savestate and
different on every machine. The Vulkan bridge is stricter about the first -
no driver handle is ever in guest memory, and every load is a new context -
so a core using it has to be able to make its renderer again from what a
state holds, and has to be able to prove it.

Measured, and what was not, is in docs/gpu-bridge.md ("The Vulkan bridge").
In short: on Linux with a software Vulkan the engine hands the bridge over,
a core's picture arrives byte-identical to that core's own reference, and a
movie replays with a state loaded before every frame. Engine unit tests 22
of 22, the Windows build compiles, `check-no-network.sh` is clean. The same
day it ran on Windows on a GeForce GTX 1060, which the session names on
standard error once the core has made its device. Not run: the interface.

## Xenia, the Xbox 360 core, joins the official cores (user-decided, 2026-10-11, issue 171)

The owner asked for it: "add it", after the core's repository was made
public the same day.

What was decided:

- `official-cores.json` has a row for the core `xenia`, system id `X360`,
  named "Xbox 360" as the core's own package names it, and the README's
  table of emulation cores lists it after xemu.
- It is the first core that draws through the Vulkan bridge (the section
  above). A Chimera older than that bridge cannot run it with a picture.

What a user should know, said in the core's README and in the issue:

- One game has been tried, Mirror's Edge. It runs from power-on into the
  first level, with picture, sound, controllers and savestates.
- It is slow: a third to a half of real time at the default CPU Speed
  (500). The setting's description says 2000 is the closest to a real
  console and why 500 is the default.
- The core's CI has no game, so on a public runner only the build, Xenia's
  instruction tests and Chimera's package tests run. The tests with the
  game are run on the developer's machine before each push.

What was measured: the published build "Development build 1f4b3f46", the
same package as "Nightly 2026-10-11", passed the core's test script with
the game present (16 tests, none failed, none skipped), and Chimera's
roster and package tests named in the commit that added the row.
Chimera's window, which the section above says was not run, has since run
the package on Windows on the GeForce GTX 1060, driven by a script: 420
frames, the project saved, and the saved project opened again. Nobody has
yet used it by hand.

## Vulkan in the cores whose emulators have it (user-decided, 2026-10-11, issue 248)

Users asked for Vulkan as a renderer in eight cores whose emulators have a
Vulkan renderer of their own: Vita3K, RPCS3, Flycast, xemu, PCSX2, Dolphin,
DuckStation and PPSSPP. The owner: "Implement Vulkan support for all the
cores that support it, as listed by the other users."

What was decided:

- Each of the eight gets `vulkan-hw` as another value of its Renderer
  setting, beside what it has. A core's default does not change.
- They are done one at a time. Each is published and reported on the issue
  when it is done, and the issue stays open until all eight are.
- Nothing in Chimera changes for it. The engine already hands a core the
  Vulkan bridge when it exports `SetVulkanBridge`, and a core that exports
  both that and `SetGpuBridge` is handed both.

The first is Vita3K, published as "Development build c4560f31". What its renderer
needed, which the other seven will need too, is in docs/gpu-bridge.md ("A
renderer that also has OpenGL"), with what was measured on the GeForce GTX
1060.

What is not proven: Chimera's window has not run a core with the setting on
`vulkan-hw` - only `chimera-run` has.

## Zeebx, the Zeebo core, joins the official cores (user-decided, 2026-10-12, issue 240)

The owner said "publish" once the core ran two games the same natively and
in the sandbox. The Zeebo is the console TecToy sold in Brazil and Mexico
from 2009; the core is built on the emulator Zeebx.

What was decided:

- `official-cores.json` has a row for the core `zeebx`, system id `ZEEBO`,
  named "Zeebo" as the core's own package names it, and the README's table
  of emulation cores lists it after Opera.
- Nothing in Chimera changed for it. The core draws its picture in software
  and uses only what every core may: controllers with sticks, a game file,
  an optional save data file, settings, and the game's memory as one bus.

What a user should know, said in the core's README and in the issue:

- Two games have been tried, Quake II and Um Jogo de Ovos. Both run from
  power-on to their menus with picture, sound, controllers and savestates,
  on Linux and on Windows. Quake II was played into its first level.
- Quake II's level runs at about real time on a fast computer: the 3D
  picture is drawn in software on one processor thread.
- The console's system font and a MIDI sound bank, which Zeebx can use, are
  not wired up; neither could be tested with the two games at hand.
- The core's CI has no game, so on a public runner only the build, Zeebx's
  own 608 unit tests and Chimera's package tests run. The tests with the
  games are run on the developer's machine before each push.

One thing was found on the way that is the emulator's own: its sound mixer
added the playing sounds in an order that differs from one run to the next,
so the same input did not always make exactly the same sound. The core
carries a two-line patch for it. It has not been reported to Zeebx's authors.

What was measured: the published build "Development build 0d06be4e", built
on a CI runner, passed the core's test script here with both games present
(10 tests, none failed, none skipped) - the sandboxed core printing the
same clock, instruction count, picture, sound and memory digests as the
native build, a minute of Quake II included - and Chimera's roster and
package tests named in the commit that added the row. Chimera's window ran
an earlier build of the core on Windows, driven by a script (600 frames,
the project saved). Nobody has yet used it by hand.

## Making a mapping copies nothing for memory nothing has written (2026-10-12, issue 248)

Through the Vulkan bridge a mapping is a buffer of the core's own, and
making one copied what the memory holds into it. A renderer makes and maps
its buffers before it draws, and does so again after every savestate load,
because a load is a new Vulkan device. For those buffers the copy fetched
nothing: the memory had just been allocated.

What changed (miniBox c1ee3e6): the copy is left out for memory the core has
flushed nothing into and that was allocated since the last time a queue was
given work. Nobody can have written such memory, so it holds nothing to
fetch. Memory the device may have written, or the core has flushed into, is
copied as before.

Why: the xemu core's Vulkan renderer maps 176 MiB of buffers when it starts.
On the GeForce GTX 1060 the copy was 184 ms of every load (59 s over 321
loads) and is 18 ms now. The other cores gain less, because their buffers
were made smaller for this very reason.

How it is held: the bridge's own test maps a buffer the device wrote,
scribbles over it without flushing, unmaps it and maps it again, and the new
mapping must show what the device wrote. A build of the bridge that takes
all memory for unwritten fails that test, on lavapipe and on the GTX 1060.
The engine's tests, the witness and the interface tests pass with it.

## What a game calls a control, and the files a game needs (user-decided, 2026-10-12, issue 164)

Two things a core of thousands of games knows only once it has the game,
both decided by the owner while the MAME arcade core was being made.

**A control keeps its name and gains a label.** The package declares its
controls by what they are ("P1 Button 1"), and that name is the control's
identity: a movie's column, a key binding and a script all go by it, so it
cannot be different from one game to the next. The game has its own word
for it ("Weak Punch", "Jump"). A core may now say that word: two optional
exports, `GetButtonLabel(index)` and `GetAxisLabel(index)`, asked once after
`Init`; the engine hands them out (`ce_session_button_label`,
`ce_session_axis_label`). The interface shows the label beside the name and
never instead of it: in the hover text of the control's TAStudio column
("P1 Button 1 (Weak Punch)") and after the name in the key configuration. It
is not written into a movie or a project. Not done: the label of an analog
control in the key configuration (its TAStudio hover text has it).

**The files a game needs beside its own are firmware.** An arcade project
was one rom set plus whichever other sets the game reads from. The owner's
decision: the project's file is the game's rom set, and the other sets -
the board's BIOS set, a clone's parent set, the sets of the board's devices
- are firmware, asked for on the wizard's firmware page and remembered by
the firmware store, so a BIOS set given once serves every game of that
board. A package cannot declare them (the MAME core has 36,721 machines),
so the core lists them for the game in hand: a package that declares
`"gameFirmware": true` returns a `"firmware"` array from `SuggestSettings`,
in `waterbox.config`'s own firmware format. The wizard merges it with the
package's declarations and the engine's decision tree decides as before; at
load the core factory asks the core again before it resolves firmware
(docs/project.md, "The firmware decision tree").

Three things that had to hold, and how:

- The answer must be the same before the firmware is given and after. The
  MAME core decides "this game needs that set" by looking for the set's
  files in the game's own file alone, not in what happens to be mounted.
- A requirement may depend on a setting the user changes after the core
  answered: a Neo Geo set that carries the default BIOS needs `neogeo.zip`
  only with another BIOS. So an entry may carry `requiredWhen`, and the
  decision tree re-reads it whenever the firmware page is built.
- Firmware is read into memory, and a disc image may be gigabytes. Disc
  images (CHD) stay files of the project, in a slot.

What was measured: the engine's tests 22 of 22; the witness 76 of 76, one
of them new (the synthetic core calls one control "Jump" and the engine
must say so for that control and no other); the interface tests with 0
failed, five of them new (the game's firmware joins the firmware page,
follows the setting it depends on, leaves with the game, is ignored for a
package that does not declare it; a label is shown beside a control's
name). Each new test was seen to fail with the code it covers broken. With
the MAME core, in Chimera's window: a game opened with its device
set taken from the firmware store runs as the headless runner runs it, and
with no such firmware known the project is not opened and the set is named.
Nothing of this was tried on Windows.

## A project is opened in one place, in the engine (user-decided, 2026-10-12)

The steps from a project to a running machine were written twice: in
`chimera-run.cpp` and in the window. A third
program that opens projects was about to be written (the MCP server,
docs/mcp-server.md), and the owner's decision was that the logic is brought
into one piece of C++ that every program uses, now and not later: "avoiding
code repetition is paramount in Chimera".

What was decided:

- One engine call, `ce_session_open_project`, takes a project whose files
  the caller has found and does the rest: it refuses a file that was not
  found or does not match its hash, refuses a package that is not the build
  the project names, checks the files against the slots the package
  declares, gives the core its files, and starts the machine with the
  project's settings (docs/project.md, "One place starts the machine").
- Finding the files stays with the caller, because that is where the two
  programs really differ: the window asks a person and remembers paths, the
  runner looks in the folders it was given.
- A caller with a person in front of it may pass two flags, for a file that
  differs and for a package that differs, after the person was shown the
  difference and went on. The window passes both; `chimera-run` passes the
  second only for `--allow-core-mismatch`.
- "Which file is the game" is one engine answer too, `ce_project_game_file`.
  The window used its own loop for it.

What changes for a user: the window now checks a project's files against the
core's slots when it opens a project, which only `chimera-run` and the New
Project window did before. A project that names a slot its core does not have
is refused with the engine's sentence. `chimera-run --project` no longer
takes `--suggest` or `--import-movie`, which were never documented with it
and which no test script of any core used.

Not done here: the New Project window still builds its own, smaller list of
files (the slot map and each file by name) for the steps that run before a
project exists - the core's suggestion and the compile step.

What was measured, on Linux: the engine's tests 22 of 22; the test of the
whole product with the synthetic core 80 of 80, four of them new (a project
played through `chimera-run --project` ends with the reference memory; a file
is found in a `--files` folder; the project's settings reach the machine; a
missing file, a changed file, another core's package and files that fit no
slot are each refused); the interface tests 0 failed of 1,045 (24 skipped).
The new checks were seen to fail with the call broken on purpose: all four
with the game path, the hash check and the settings removed, and the settings
check alone with only the settings removed. With a real core that is several
machines (Genesis Plus GX, a free game, 300 frames): the runner on the
project and on the bare game end with the same memory, the window opening the
same project ends with the same memory as the runner after one boot, and a
project naming the wrong slot is refused by both with the same sentence. On
Windows: the engine compiles, and the runner plays the synthetic project to
the reference memory and refuses the changed file. The window was not tried
on Windows.

## An MCP server for agents, started from the window (user-decided, 2026-10-12)

A user suggested that an AI agent should be able to use Chimera directly,
through an MCP server (a program that offers an agent a list of named tools).
The owner decided it is wanted and how. The design is docs/mcp-server.md;
nothing of the server is built at the time of this section.

What was decided:

- The main use is a person working on a movie in the window who asks their
  agent to look at it or change it. The server is started, watched, logged
  and stopped from Tools > MCP Server..., at any time.
- It always starts stopped, and that is never remembered.
- In the window the agent may only look, until the person ticks a box that
  allows changes. The box is off at first every time.
- A program an agent starts and talks to over standard input and output is
  inside the rule that Chimera has no network code. No socket is used; the
  HTTP form of MCP is not offered.
- The window comes first. A mode with no window, where an agent opens its
  own machine, follows; there the files it may read and write are limited to
  folders named when it is started.
- Writing to the machine's memory is offered, and is reported from the first
  write on, because a movie cannot reproduce it.
- It ships in the release bundles once it is tested, not before.
- Opening a project had to become one piece of engine code first (the
  section above).

Why these: the person's work is what is at risk, so the server is off until
asked for, read-only until allowed, and every change is an ordinary TAStudio
edit that can be undone, with a branch saved before the first one.

## The MCP server's engine part: a pipe, a protocol, and the tools that look (2026-10-12)

The first part of the MCP server (docs/mcp-server.md) is in the engine. What
was chosen while building it, and why:

- **The agent's program starts `chimera-mcp`, and `chimera-mcp` reaches the
  running Chimera through a local pipe.** MCP's local form has the agent's
  program start the server as a child process. A window that is already open
  cannot be started, so a small program stands in between and passes each
  call on. It also answers the introduction and lists the tools by itself, so
  an agent's program does not need restarting when Chimera is.
- **The pipe is a named pipe on Windows and two FIFO files on Linux**, not a
  Unix socket: no socket call is used at all, and `tools/check-no-network.sh`
  passes unchanged. The Windows pipe refuses other machines and is given a
  security descriptor naming this user only, because the default one lets
  every user of the machine read a pipe. The Linux folder must be this
  user's and closed to others, or the server does not start.
- **A call runs on the thread that runs the machine.** A thread in the engine
  only reads and writes the pipe. The frontend asks once per pass of its loop
  whether a call waits (`ce_mcp_pending`) and has the engine run it
  (`ce_mcp_service`); a call only the frontend can run is handed to it.
- **A call that waited too long is not run.** If Chimera does not take a call
  up within 30 seconds (a dialog is open), the agent is told it was not run;
  Chimera drops any call older than 25 seconds when it gets to it, so a call
  the agent was told had failed is never run afterwards.
- **A kept connection that has gone stale is reopened once**, without the
  agent seeing an error, when the request could not be sent. Nothing was run,
  so sending it again is safe.

What was measured. A new unit test (`test_mcp`: the pipe, the protocol, a
call travelling to a server and back, a second server and a second client
refused, reconnection) passes on Linux, 40 runs in a row, and its Windows
build passes when run through WSL. One fault was found that way: on Windows
the pipe did not listen again until the next wait after a client left, and a
client arriving in between was told another agent was connected. The engine's
tests are 23 of 23. The test of the whole product with the synthetic core is
82 of 82, two of them new: `chimera-run --mcp-serve` plays the running
Chimera, `tests/synth/mcp-agent.py` plays the agent through `chimera-mcp`,
and memory must equal the reference dumps byte for byte, the input must equal
the movie file, and the picture must equal the one `chimera-run` writes for
the last frame; with no server running, a call must say how to start one.
With the memory read, the picture, the input and the "not started" sentence
broken on purpose, the checks failed. The same path by hand on Windows
(`chimera-run.exe`, `chimera-mcp.exe`, the same script) gave the same
answers. The interface is unchanged and its tests were not run again.

## Tools > MCP Server..., and what the window does for an agent (2026-10-12)

The window's part of the MCP server (docs/mcp-server.md). What was chosen
while building it, and why:

- **The dialog shows the engine's server and holds nothing itself.** The
  state, the name of the agent's program, whether changes are allowed and the
  log are all asked of the engine a few times a second. So the dialog can be
  closed and opened again, and two views can never disagree.
- **It does not block the main window.** A person works on their movie with
  it open. Closing it does not stop the server; closing Chimera does, so an
  agent still connected is told the server is gone instead of waiting.
- **The title bar says "MCP server started"** for as long as it is, and the
  menu item is ticked. A server that can change a movie must not be left on
  without the person seeing it.
- **The main loop asks once per pass** whether a call waits, after it has
  handled the window's own messages. Nothing is done when none waits. While a
  blocking dialog is open the loop does not run, so calls wait, and the
  30-second rule of the engine part answers them.
- **The tools the window runs are in one class with no window in it**
  (`McpHostTools`), given the open movie. So they are tested without a
  window, and the window's own part is a dozen lines.
- **A marker says whether a person placed it.** Every movie has three of its
  own (the run's start, its last input, its end); an agent asked to "look at
  my markers" needs to tell them apart.
- **`--mcp-server` on the command line starts the server at launch.** It is
  what lets the test of the whole product open a project in the real window
  and talk to it with nobody to press Start. It is not remembered, and no
  setting can start the server. This flag is new and was not one of the
  owner's nine decisions; it is here for him to confirm or remove.

What was measured, on Linux. The interface tests: 0 failed of 1,056 (24
skipped), ten of them new - five for the dialog (it starts and stops the real
server, the tick box reaches the engine and is off at every start, the log is
saved as shown, the set-up command names the program beside the engine, and
the real `chimera-mcp` connects, is named in the dialog and has its calls in
the log) and five for the window's tools. The test of the whole product: 83
of 83, one new - Chimera opens a project with `--mcp-server` in the real
window, and what an agent reads through `chimera-mcp` is the reference memory
byte for byte, the movie's input line for line, a picture, and the project's
markers and branches. With the code broken on purpose (the tick box not
following the engine, the agent's name left out, branches numbered from 0,
the machine not handed to the engine) exactly the new tests failed: three
interface tests and the window check. A picture of the dialog was looked at.
The engine was not
rebuilt for this part. Nothing of the window was tried on Windows.

## An agent may change the movie, when a person allows it (2026-10-12)

The tools of the MCP server that change something (docs/mcp-server.md): the
playback position, the input, the movie's length, markers, branches, and the
machine's memory. What was chosen while building them, and why:

- **Refused until allowed, and the engine does the refusing.** Each tool is
  marked in the engine's list as one that changes something or one that does
  not. The check is made before a call is handed to anybody, so a tool added
  later cannot forget it.
- **The work is kept first, and that is the engine's rule too.** Before the
  first change after a start the engine hands the frontend a call of its own,
  `keep_work`, and runs the agent's change only after a good answer. The
  window answers by saving a branch. If it cannot (TAStudio is not open), the
  change is refused, and so is every later one until the server is started
  again. This was met for real: the first unattended run had no TAStudio
  open, and nothing was changed.
- **Edits go through the calls TAStudio's own piano roll uses**
  (`SetBoolStates`, `PokeFrame`, `InsertEmptyFrame`, `RemoveFrames`, the undo
  history's batches). Nothing of what an edit means is written again: the
  saved states after the edit are dropped, the piano roll redraws and the
  step can be undone, because that is what those calls already do.
- **One call is one undo step**, named "Agent: ...". An agent that sets
  three buttons over forty frames has made one change, and a person undoes
  it with one Ctrl+Z.
- **`set_input` checks every line before writing any.** A line must have the
  columns of this movie's lines. One bad line in a hundred changes nothing.
- **`seek` answers when the machine has arrived.** A seek takes many passes
  of the main loop. The call is held open, nothing else of the agent's runs
  meanwhile, and the answer says the frame reached - or, if a person stopped
  it, where it stopped.
- **`undo` undoes the last step whoever made it.** It is the same history a
  person uses, and an agent that could only undo "its own" steps would need a
  second history beside the first.
- **A write to memory is not hidden and not undone.** It is offered because
  testing a guess needs it. `status` says from the first write on that memory
  was written and on which frame; the saved states are not touched, exactly
  as when a person pokes memory from the Hex Editor.
- **`--mcp-allow-changes`** on the command line is the tick box for a run
  nobody clicks in. Like `--mcp-server` it is new, exists for the tests, and
  is for the owner to confirm or remove.

What was measured. On Linux: the engine's tests 23 of 23 (`test_mcp` now also
covers the refusal while changes are not allowed, the work being kept before
the first change, and nothing being changed when it cannot be kept; 10 runs
in a row). The interface tests: 0 failed of 1,066 (24 skipped), ten of them
new, for the window's tools with a movie and a stand-in for the window. The
test of the whole product: 85 of 85, two new. In one, `chimera-run` serves
with changes allowed and memory, a 16-bit value in the other byte order and a
game's named value are written and read back, the refusals refuse, and
`status` says memory was written and on which frame. In the other the real
window is opened with TAStudio and `--mcp-allow-changes`, and an agent sets
buttons and undoes it, writes input lines past the movie's end and undoes
it, inserts and deletes frames, places a marker, saves a second branch (the
first having been saved for it before its first change), seeks back to frame
10 and forward to the end - where the machine must hold the reference memory
byte for byte, which it only can if every edit was taken back exactly - and
loads a branch. The two checks that existed already now also require every
change to be refused while changes are not allowed. With the code broken on
purpose (the work not kept first, a write one byte off, undo doing nothing,
a range of frames cut to one) exactly the new tests failed: `test_mcp`, two
interface tests and the two new checks. On Windows: the engine compiles,
`test_mcp.exe` passes, and the write checks pass against `chimera-run.exe`
and `chimera-mcp.exe`. The window was not tried on Windows.

One thing learnt about this machine: a test that fails an assertion took
over a minute to end, because WSL hands every core dump to a program of its
own. `test_mcp` now asks for no core dump.

Added the same day: the window was then tried on Windows after all. The
build was copied to a folder of its own and Chimera started there with
`--headless --mcp-server --mcp-allow-changes`, a project and the same script,
and the agent's whole test of changes in the window (`mcp-agent.py`,
"window-changes") passed through `chimera-mcp.exe`: the edits, the undos, the
branches, the seeks, and the reference memory at the end. What is still not
known on Windows is how the dialog looks; nobody has opened it there.

## The agent's own RAM search (2026-10-12)

Four tools of the MCP server (docs/mcp-server.md) put the engine's RAM search
in an agent's hands: start, filter, results, undo. What was chosen:

- **It is the engine's search, not a new one.** The tools call `ce_ramsearch_*`,
  the same code the RAM Search window runs, so a domain of any size is
  searched at the cost the window pays.
- **It is the agent's, not the window's.** A person's RAM Search window and
  an agent's search do not see each other's candidates. An agent narrowing
  four thousand addresses must not empty the list a person is reading.
- **A filter compares with the value remembered and then remembers the value
  now** (the window's "previous value: last search"). That is the one rule an
  agent has to keep in mind, and it needs no call once a frame.
- **Bytes, not properties.** RAM Search is byte-based by the owner's earlier
  decision (2026-10-07); a game's named values have their own tools.
- **The search holds a pointer into the machine's memory**, so before every
  use the engine checks that the open machine still has that domain at that
  address and of that size, and drops the search when it does not. It also
  ends with the server.

What was measured. The engine's tests 23 of 23. The test of the whole product
85 of 85: the two checks that drive `chimera-run` through `chimera-mcp` now
also search - the addresses equal to a value must be exactly those of the
reference dump, lowest first; nothing having changed, "different from
before" must leave none and undo must bring them back; a 16-bit search must
list the right addresses; and, with changes allowed, after one byte is
written "different from before" must leave that byte alone. With "equal"
made to mean "different" and the addresses listed one too high, both checks
failed. The interface is unchanged; its tests were run again against the new
engine library and none failed (1,066). On Windows the engine compiles,
`test_mcp.exe` passes and the same checks pass against the Windows programs.

## The MCP server ships with the window mode (user-decided, 2026-10-12)

The owner's earlier decision was that `chimera-mcp` goes into the release
bundles once it is tested. With the window mode complete - an agent looks at
the open project, searches RAM and, when allowed, changes it - he decided to
ship that now, and to build the two convenience tools (`watch`,
`advance_until`) and the windowless mode after. He also confirmed the two
command-line flags, `--mcp-server` and `--mcp-allow-changes`, as documented
flags.

What changed: `tools/build-bundle.sh` copies `chimera-mcp` (`.exe` on
Windows) into the bundle's `dll/` folder, beside the engine's library, and
fails the bundle when it is missing. That folder is where Tools > MCP
Server... says the program is. The README has a short section.

What was measured: both bundles were built from the existing build
(`--skip-natives`) and passed the script's own check. The Linux bundle's
`chimera-mcp` lists its tools and answers a call with "not started". The
Windows bundle was copied to a folder of its own with the synthetic core
added, Chimera started there with no window shown, and the agent's test
of changes in the window passed through that bundle's `dll\chimera-mcp.exe`.
The Windows program needs no library that is not part of Windows. No engine
or interface code changed, so the other tests were not run again.

Not known: whether a second program in the Windows zip changes what
antivirus software says of it (an earlier false alarm is on record,
2026-10-07). Nobody has opened the dialog on Windows.

## Two tools that save an agent its round trips: watch and advance_until (2026-10-12)

Every call an agent makes costs it time and money. Finding "the frame on
which the score first goes up" by seeking a frame and reading memory, again
and again, is two calls a frame. Two tools of the MCP server
(docs/mcp-server.md) make it one call:

- **`watch`** names up to 32 values, each a place in memory or a game core's
  property. From then on every answer that lands the machine on a frame -
  `status`, `seek`, `advance_until`, `load_branch` - carries their values.
  The engine reads them when the answer is given, so the frontend's answer
  to a seek needs no knowledge of what is watched: it hands the engine the
  machine with its answer (`ce_mcp_answer` takes it now).
- **`advance_until`** plays the movie forward and stops on the first frame
  where a value meets a condition: equal, not equal, greater, less, or
  "changed" since the start. The window does the playing, because the
  playback position is its own; what the condition means is the engine's
  (`ce_mcp_condition_set`, `ce_mcp_condition_met`), asked after every frame.
  It is the same seek TAStudio makes, stopped early.
- **It plays the movie's own input and never past its end.** Playing past
  the end adds frames to the movie; a tool that only waits for something to
  happen must not lengthen it. It answers what stopped it: the condition, the
  movie's end, the limit of frames, or a person.
- **A value that cannot be read does not spoil the rest.** A watch on an
  address that does not exist carries a sentence in place of its value, and
  the other watches are still told.
- **`advance_until` counts as a change**, like `seek`: it moves the playback
  position, so it needs the tick box. `watch` only reads.

What was measured, on Linux. The engine's tests 23 of 23. The interface
tests: 0 failed of 1,067 (24 skipped), one new, for what `advance_until`
asks of the window and what it answers for each way of stopping. The test of
the whole product: 85 of 85, with the four checks that use the server made
longer. All four set watches (a property, a memory value, and one that
cannot be read), and require `status` to carry them and an empty list to end
them. The one that edits in the real window also requires a seek's answer to
carry the watched value; `advance_until` to stop on the first frame where the
step counter is above 3, and the counter to be 3 or less one frame earlier;
"changed" to stop when a property first differs; the frame limit and the
movie's end each to stop it and be named; and a value that cannot be read, or
is not a number, to be refused. With the code broken on purpose (a seek
carrying no watches, "changed" never met, every watched number one too high,
the window never asking the engine, the limit called the movie's end) the new
interface test and all four checks failed. On Windows the engine compiles and
`test_mcp.exe` passes; the tools themselves were not run there this time.

## What an edit means is decided once, in the engine (2026-10-12)

The windowless mode of the MCP server needs the same editing tools as the
window, with no TAStudio to run them. Rather than write them a second time,
their meaning moved into the engine before either holder of the movie is
asked (the owner's rule of the same day: no logic written twice).

- For `set_input`, `set_buttons`, `insert_frames` and `delete_frames` the
  engine now checks the arguments against the movie and the machine's
  controls, and hands on one of three plain steps: these lines from this
  frame, this many frames inserted here, this many deleted there. It also
  gives the step its name in the undo history ("Agent: set controls, frames
  5 to 7").
- `set_buttons` is worked out into the lines it amounts to: each frame's line
  is read, the named buttons and axes are set, and the line is written again.
  The window receives it as `set_input`.
- The window's side shrank to applying a step through TAStudio's own calls.
  It no longer knows which buttons exist or what a line looks like.

Found on the way, and corrected the same day: a movie line may or may not
carry the empty first group of a machine with no console buttons
(`||........|` or `|........|`). It was first written here that the engine
writes it and the interface leaves it out. That was wrong. Both write it: a
project made in the window with a PlayStation 2 core shows it, and the
interface's code writes every group. The short form is what movies written
by hand have, the test movies of this repository among them, and the window
keeps a line as it was loaded until the frame is edited. Both forms are read
alike, so the check of a line counts the characters between the bars and not
the bars.

What was measured: the engine's tests 23 of 23; the interface tests 0 failed
of 1,065 (24 skipped; four tests of the window's own button and line
checking were removed with that code, and two for applying a step added);
the test of the whole product 85 of 85, the check that edits in the real
window now also requiring an unknown button, an edit with nothing named and
an insertion past the movie's end to be refused and to change nothing. Those
three were not yet broken on purpose; the next round does it. The engine
compiles for Windows.

## The MCP server without a window (2026-10-12)

The last part of the MCP server (docs/mcp-server.md): `chimera-mcp --cores
<dir> --work <dir>` opens a machine of its own for an agent, with no Chimera
running. What was chosen while building it, and why:

- **The same program, the same tools.** There is one `chimera-mcp`. With no
  arguments it reaches the running window; with a cores folder it serves a
  machine in its own process. The engine's tools (memory, picture, RAM
  search, watches) are the very same code, and the editing tools arrive as
  the steps the engine makes of them (the section "What an edit means").
- **What a window would do is done by one class over the engine**
  (`mcp_local.cpp`): open and save a project, hold the movie and its undo
  history, move the playback position. It uses the engine's own movie
  (`ce_session_movie_load`, `ce_session_seek`), so going back is through the
  saved states and an edit drops the ones after it, as in the window.
- **A project is opened by the one engine call** made for it earlier
  (`ce_session_open_project`), the third program to use it.
- **Nothing is asked.** In the window a person allows changes and their work
  is kept first. Here the machine is the agent's own.
- **The file limits are set when the program is started** and a call cannot
  widen them: packages from `--cores`, reads from `--files` and `--work`,
  writes inside `--work`. A path is worked out to where it really leads
  before it is compared, so `..` and links do not get out.
- **Firmware is given by path.** Looking in the Firmware folder, and the
  rules of which file counts as which firmware, are the interface's. Writing
  them a second time was the thing to avoid, so the windowless mode takes a
  file for each id, as `chimera-run --firmware` does.
- **The two holders of a movie answer alike, and one script holds them to
  it.** TAStudio's side is the window's and the windowless side is the engine's; the answers'
  shapes are written in both. `tests/synth/mcp-agent.py` asks the same
  questions of both and expects the same answers, which is what keeps them
  from drifting.

Found on the way: lines written by `set_buttons` carried "!" where the
button's letter belongs, because the engine took the letters from a
recording that a playing session never starts. In the window nobody saw it:
TAStudio reads any character as "pressed" and writes the line again itself.
It now takes the machine's own letters.

Not done: the windowless mode does not look in Chimera's Firmware folder; a
branch there keeps no state of the machine; nothing of it was run with a
core that draws on a graphics card.

What was measured. On Linux: the engine's tests 23 of 23 (`test_mcp` also
checks that the windowless tools are listed only where there is no window).
The interface tests 0 failed of 1,065 (24 skipped), against the new engine
library; the interface itself did not change. The test of the whole product
87 of 87, two new. In the first, `chimera-mcp` with a cores folder is given
the same script as the window, in a mode of its own: it lists the cores,
refuses a game outside the folders it was given, starts a project, writes
the movie's 70 lines with `set_input` and plays them - memory equal to the
reference dumps and the picture equal to the one `chimera-run` writes - then
everything the window's check does (edits, undo, branches, seeks,
`advance_until`, watches, RAM search, memory writes), then saves, refuses to
write outside the work folder by a plain path and by one that climbs out,
exports the movie, closes, opens the saved project and plays it to the
reference memory. In the second, the real window opens the project the
windowless mode saved and plays it to the reference memory. With the code
broken on purpose (any path readable, the landing frame not drawn, undo
restoring nothing, and the three edit refusals of the section before) the
windowless check and the window's change check failed, on exactly those
points. On Windows the engine compiles, and `chimera-mcp.exe` with a cores
folder was driven by hand through the same steps: a read outside refused,
the movie written and played to the reference memory, an edit and its undo,
`advance_until`, a save, a write outside refused, and the saved project
reopened and played to the reference memory.

## chimera-mcp stays a program of its own (user-decided, 2026-10-12)

The owner asked whether `chimera-mcp` could be dropped and an agent reach the
open Chimera directly. What was looked at:

- An agent's program can reach an MCP server in two ways only: start a
  program and talk on its standard input and output, or connect over HTTP.
  None can be told to open a named pipe. So with a listener that is not HTTP
  - and Chimera's is a local pipe - the agent's program has to launch
  something that relays to it.
- That something could be `chimera-mcp`, or Chimera's own program started
  with a flag and no window, or a tool already on the system (PowerShell,
  `socat`) with the window speaking MCP on the pipe itself. A listener over
  HTTP would need nothing launched, and is network code.

He decided to keep `chimera-mcp` as it is: it starts fastest, it says how to
start the server when Chimera is not running, and the pipe stays the only
way in. HTTP was not chosen.

## Cadmium (CHIP-8) and Fuse (ZX Spectrum) join the official cores (user-decided, 2026-10-12, issues 253 and 229)

The owner said "commit and publish all you've done so far" once both cores
were built and tested locally.

What was decided:

- `official-cores.json` has a row for the core `cadmium`, system id `CHIP8`
  ("CHIP-8"), and a row for the core `fuse`, system id `ZXS` ("ZX
  Spectrum"). The README's table of emulation cores lists Cadmium after
  Stella and Fuse before the X68000.
- Fuse has the same system id as the ZX Spectrum of the ares core, on
  purpose: they are two cores of one system, and a project says which core
  it was made with.
- The Fuse core carries the eighteen machine ROMs Fuse itself is distributed
  with, taken from Fuse's source at build time (the owner: "package the
  ROMs"), so twelve of its sixteen machines start with no file from the
  user. The other four (the three Pentagon models and the Scorpion) ask
  for their ROMs as firmware.
- Cadmium's source includes a few historic CHIP-8 programs; the owner said
  that is not a problem.
- Nothing in Chimera changed for either core.

What a user should know, said in each core's README:

- Cadmium runs CHIP-8 and its relatives (SUPER-CHIP, XO-CHIP, MegaChip and
  others, twenty-one variants) and picks the variant from the program unless
  a setting names one.
- Fuse runs sixteen Spectrum models. Tapes, snapshots and disk images load;
  Fuse's add-on hardware (Interface 1, DivIDE, the mouse and others) is not
  offered yet, and a project takes one media file.

What was measured: both cores' first CI runs passed and published
"Development build 23000d99" (Cadmium: 12 tests, none failed, none skipped -
all of its test programs come with its sources) and "Development build
3e9981e5" (Fuse: 14 passed, 2 skipped for want of the ROMs and the game no
runner may have). Each published package was downloaded and run here beside
the local build through Chimera's headless runner: the same picture after
1,500 frames of a CHIP-8 game, and the same memory and picture after 3,000
frames of Manic Miner. Chimera's roster, package and control-letter tests
pass with both packages installed (22 tests). On Windows, earlier local
builds of both ran the same as on Linux in the headless runner; the
published packages were not run there.

## The MCP server's menu entry has an icon, and a dot for "started" (user-decided, 2026-10-12)

The owner asked for an icon on Tools > MCP Server..., as every other entry
has, "some AI-like". It is two four-pointed sparkles, drawn in MenuIcons like
the other drawn icons, flat and mid-toned so it shows on a white menu and on
a dark one (MenuIconTests measures it on every theme).

The entry used to be ticked while the server is started. A menu draws the
tick where the image is, so an entry cannot have both (the rule at the top of
MenuIcons). The entry now shows the same icon with a green dot while the
server is started, and is not ticked. The title bar still says "MCP server
started".

Measured: the interface tests, 0 failed of 1,065 with the screenshots on;
the sheet of menu icons was looked at in the light and the dark theme.

## Vulkan is the default where the emulator's own default is Vulkan (user-decided, 2026-10-12, issue 248)

The owner, once the eight cores had their Vulkan renderers: "set Vulkan as
default for the cores that manifestly prefer it (Xenia, for example?), keep
OpenGL for those who are indifferent or manifest no preference."

"Manifestly prefer" was read from each emulator's own source: what its
Renderer setting defaults to, with nothing chosen.

| Core | The emulator's own default | The core's default |
|---|---|---|
| Vita3K | Vulkan (`backend-renderer`) | `vulkan-hw`, was `opengl-hw` |
| RPCS3 | Vulkan, in a build that has it | `vulkan-hw`, was `opengl-hw` |
| Xenia | Vulkan or Direct3D 12; it has no OpenGL | `vulkan-hw`, as before |
| xemu | OpenGL | `opengl-hw`, unchanged |
| Dolphin | OpenGL, listed first outside macOS | `opengl-hw`, unchanged |
| Flycast | OpenGL, in a build without Direct3D | `software`, unchanged |
| PCSX2, DuckStation, PPSSPP | chosen by the computer | `software`, unchanged |

A core whose default is its software renderer keeps it. That default is a
different decision: a software renderer draws the same picture on every
computer, and the owner's sentence is about which graphics API a core that
already draws on the card asks for.

What a Vulkan default costs: a project made with it needs a graphics driver
with Vulkan and a Chimera that has the Vulkan bridge. Without either the
project does not start, and the core's message says to choose `opengl-hw`.
The core's own tests that ran the default before now name `opengl-hw`, so
they still test the OpenGL renderer.

RPCS3 was switched only after a second game was tried: Dengeki Bunko Fighting
Climax Ignition drew nothing under Vulkan, the owner chose "fix it first,
then switch", and the fault was the renderer's (chimera-core-rpcs3 9fe00b0).

The new-project window shows `vulkan-hw` as "Hardware (Vulkan)", beside
"Hardware (OpenGL)"; it used to show the raw value.

Measured: with default settings, Chimera's headless runner gives each of the
two cores its Vulkan renderer (Mesa's software Vulkan); Chimera's package
tests pass on both packages (18 tests). Vita3K's Vulkan renderer was measured
on a GeForce GTX 1060 when it was added; RPCS3's is in that core's
docs/PLAN.md. Neither core's whole test script was run for this change.

## A core is asked about a game with the game's files, not the one file that names it (2026-10-12)

A core may be asked about a game before any machine exists (its
`SuggestSettings` answer): which settings it suggests, which settings the
game has of its own, and which firmware this game needs. The engine has
always been able to ask with every file mounted as a run would have them
(`ce_suggest_settings`). The interface asked with the game file alone, and
with no settings.

That was enough while a game was one file - a PS3 disc folder's entry, an
arcade rom set. It is not enough for a disc: a `.cue` is a list of track
files, and a core shown the `.cue` without them cannot read the disc. The
Mednafen core has to read a Saturn disc's region to say which of two BIOS
files the console starts with. Asked about the `.cue` alone it answered
"Error opening file ... (Track 01).bin", no firmware was asked for, and the
project failed to start for lack of a BIOS nobody had been asked for. A
package of several machines also could not know which machine the file was
for.

So the question now carries what a run would have:

- **The new-project window** writes down the files picked so far, the slot
  each fills, and the settings chosen so far, and the helper process that
  loads the core mounts them (`--suggest-settings <package> <game> <that
  file>`).
- **A load** (which asks again, so that a game opened with no project is
  completed the same way) mounts the project's files and its slot map, or
  the extra files of a game opened without a project.
- **Both** add the track files of every `.cue` among them, from beside the
  cue, under the names the cue gives them. A project already lists them as
  its support files; a wizard that has not made its project yet does not.

Firmware is still not mounted when the core is asked: the wizard asks before
any is chosen, and an answer must not depend on it (the rule the MAME core's
change set).

Nothing changes for a core whose game is one file: it is asked as before,
with the settings added.

Measured: the Mednafen core's window test opens a Saturn project of
Virtua Fighter Remix with the BIOS the core named, taken from Chimera's
firmware; before the change the same project stopped with "Error opening
file saturn_na_eu.bin". Unit tests: four for the file list (a cue's tracks
come with it, nothing is listed twice or invented, a second disc's tracks
come too, the wizard's document becomes the slot map and the files) and one
for the wizard's document (picked files, slot map, settings); each was seen
to fail with its behaviour taken out.

## MAME joins the official cores, as the arcade core (user-decided, 2026-10-12, issue 164)

The owner asked for a MAME arcade core ("start 164"), decided its four open
questions the same day, and then said "commit and publish all you've done so
far" and "for the new cores: add the discord hook, update Chimera's
README.md with the new cores, and anything else we typically do when adding
a new core".

What was decided:

- `official-cores.json` has a row for the core `mame`, system id `ARCADE`
  ("Arcade" - the owner: "Arcade is fine"). The README's table of emulation
  cores lists MAME after FBNeo.
- The core is every arcade machine of MAME: 36,721 machines from 1,766
  driver files, in one package of 79 MB. Chimera's other arcade cores
  (FBNeo, and the arcade boards of Flycast, DuckStation, PCSX2 and Dolphin)
  stay: a project says which core it was made with.
- A game's controls keep MAME's general names ("P1 Button 1") as their
  identity, and the game's own word for each ("Weak Punch") is shown beside
  it. A game's other rom sets (its BIOS set, a device's set) are asked for
  as the project's firmware. Both needed additions to Chimera, which are in
  the section "What a game calls a control, and the files a game needs"
  above.
- Issue 164 stays open: its reporters wait for a fix in MAME that the
  version this core builds does not have yet.

What was measured: the core's first complete CI run passed and published
"Development build cdbaff4b" in 2 hours 11 minutes on a public runner (it
builds MAME twice, for the host and for the sandbox). Its test script there:
23 passed, 0 failed, 5 skipped (the tests that need rom sets no runner may
have); the window test: 2 passed, 2 skipped for the same reason; Chimera's
package tests: 18 passed. The published package was downloaded and run here
beside the local build through Chimera's headless runner: the same picture
after 600 frames of Pong, and the same picture and memory after 1,200 frames
of Shinobi. The games that were played with the local build are listed in
the core's docs/PLAN.md. The published package was not run on Windows; an
earlier local build was.

## A file with no extension is a format a slot can declare (2026-10-13, issue 250)

An Xbox Live Arcade game is one file with a long name of letters and digits
and no extension. The Xenia core's game slot declared `formats: ["iso"]`, so
the New Project window said such a file was "not one of the .iso this slot
expects", and the engine refused it when the project was made: a slot that
names its formats takes nothing else.

A slot's `formats` may hold the empty string, which is a file with no
extension. The engine needed no change - it compares a file's extension,
which is empty for such a name, with each declared format - but nothing said
so, no test held it, and the window showed the empty format as a lone dot.
Now docs/project.md says it, the engine's project test holds both sides of
it (accepted where declared, refused where not), and the window shows it as
"no extension" in the slot's heading and in its note about an unexpected
file. The file picker is asked for `*.`, which is how Windows spells it.

This is a declaration every core can use, not something about one console.

Measured: the engine's tests (23 pass), the interface tests (1,072, none
failed; one test of remembered firmware paths failed once in three runs and
passed alone and in the other two - it shares a static with tests that run
beside it, and is not touched here), the end-to-end test with the synthetic
core rebuilt (87 pass).

## Mednafen joins the official cores: the Saturn, the ST-V, the Virtual Boy, the PC-FX and the Lynx (user-decided, 2026-10-13)

The owner set the goal that every system BizHawk supports is supported in
Chimera too, with the Mednafen core as the first step (2026-10-12), held its
publishing back while it was finished, and on 2026-10-13 said "publish the
mednafen core -- the usual: add to readme, add to tools/ page, add discord
hook, etc".

What was decided:

- `official-cores.json` has a row for the core `mednafen` with five systems:
  `SAT` (Sega Saturn), `VB` (Virtual Boy), `PCFX` (PC-FX), `LYNX` (Atari
  Lynx) and `STV` (Sega ST-V, the Saturn's hardware as an arcade board). The
  README's table of emulation cores lists Mednafen after Flycast.
- The core is those systems of Mednafen 1.32.1 and no others: Chimera has
  other cores for the rest of what Mednafen emulates.
- Only the first player's port starts with a pad in it; every other port
  starts empty (the owner, the same day). A second player's pad is a
  setting.
- The core needs the change in the section "A core is asked about a game
  with the game's files" above: an older Chimera never asks for the Saturn's
  BIOS, because the core cannot read a disc from its cue sheet alone.

What was measured: the core's first CI run passed and published
"Development build ad1dccee" in 17 minutes. No BIOS and no game may be put
on a runner, so its test script there ran the tests of the Virtual Boy with
a program the tests assemble: 15 passed, 0 failed, 73 skipped; the window
test: 2 passed, 5 skipped; Chimera's package tests: 18 passed. Before the
push, with a game of each system and Chimera's engine, the same script gave
88 passed, 0 failed, 0 skipped and the window test 7 passed. The published
package (built with GCC 13) was downloaded and run here beside the local
build (GCC 14) through Chimera's headless runner, with a movie of a game of
each of the five machines: the last picture, the save files and, where the
machine's RAM is a memory domain, the RAM are the same. The games that were
played are listed in the core's README and docs/PLAN.md. The published
package was not run on Windows; the local build was, headless for all five
machines and in Chimera's window for four (not the ST-V).

## An average speed beside the frame counter (user-decided, 2026-10-13, issue 265)

**Asked for:** a figure for comparing settings. The frame counter on screen
answers "how fast right now" and moves too much to read: on a heavy core it
swings between single digits and full speed.

**Decided:** View > Display Average FPS, off by default, drawn under the
frame counter ("avg 59.9 fps"), with View > Reset Average FPS beside it.

- **What it is.** Frames emulated over the time they took, since the last
  reset. It starts again when the option is turned on, when a game is
  loaded or started again, and when Reset Average FPS is chosen.
- **What counts.** Free running, fast-forward and turbo included. A pass of
  the main loop made while paused, while frames are advanced by hand, or
  while going back adds neither its frames nor its time: an hour on pause
  must not turn 60 into 3 (a commenter on the issue asked for exactly that),
  and stepping a frame at a time is the speed of a finger.
- **Where it lives.** The count and the rule for what counts are the
  engine's (`ce_average_fps_*` in engine.h, `average_fps.h`). The interface
  tells it of each pass of its loop and shows the number. No hotkey.

**Tested.** The engine's unit test (`test_average_fps.cpp`: 60 then 30
frames a second average 40; a pause, hand-stepping and going back change
nothing; a reset starts again), seen to fail with the pause rule taken out.
A test that the window's calls reach the engine with each flag
in its place. And looked at once in the real window on a private display,
with the synthetic core: "60 fps" over "avg 59.9 fps".

**Not measured:** how it reads on a slow core; nothing here was run on one
with it on.

## A Vulkan command answers to every name it has (2026-10-13, issue 268)

**Reported:** Tekken 6 in the RPCS3 core, with the Vulkan renderer, ended
the program at frame 67: "access violation, execute 0x0".

**Found.** RPCS3 asks the device by name for `vkCmdSetEvent2KHR`. A command
that came through an extension and later joined a version of Vulkan has two
names, and `vkCmdSetEvent2KHR` and `vkCmdSetEvent2` are one command. The
Vulkan bridge offered the extension (`VK_KHR_synchronization2`) and knew the
command by the newer name only: the core was answered with nothing and
called it. Forty-two names of twelve offered extensions were like this.

With that alone mended the game did not crash and did not go on either. The
bridge's half in Chimera asked the driver for `vkCmdSetEvent2`, and a Mesa
driver answers that name only to a program that asked for Vulkan 1.3; RPCS3
asks for 1.2. Chimera had nothing to call, refused the call, and the
renderer waited for ever for an event nobody had set.

**Decided:** an extension the bridge offers is offered whole.

- In a core, every name the Vulkan registry gives a command the bridge
  carries answers, with the same function. This adds no command to the
  bridge, so a core built with it still runs on a Chimera built before it.
- In Chimera, the bridge asks the driver for a command under each of its
  names in turn.

The change is in miniBox (`source/vk`, commit 2b86a8a); a core gets its half
by being built again.

**Measured.** Tekken 6, RPCS3 with the Vulkan renderer:

- On Linux with Mesa's software driver (llvmpipe 25.2.8): the published core
  ends at the call to address 0; a core with the first half only spins for as
  long as it is left (ten minutes), asking `vkGetEventStatus`; with both
  halves 300 frames run.
- On a GeForce GTX 1060 (Windows, Vulkan 1.4.312): the published core ends
  as reported, at the same place. The rebuilt core runs 6,000 frames - the
  logos, the menu, into a fight of the Scenario Campaign - and draws the same
  picture at frames 3,000 and 5,999 with a Chimera built before this change
  and with one built after it. That card's driver answers both names to any
  program, so there the new core is enough.

**Tested.** miniBox's test of the bridge asks for six commands by their
extension's name and sets an event with one of them, on an instance that
asked for Vulkan 1.0. It fails at the first with the old table of names and
at the second with the old host, and a seventh on-purpose broken build keeps
the second failure. On llvmpipe only: the new steps were not run on a real
card.

**Not done:** four commands of offered extensions are still not carried
(`vkCmdSetPatchControlPointsEXT`, `vkCmdSetLogicOpEXT`, `vkCmdSetLineStipple`,
`vkCmdPushDescriptorSetWithTemplate`). No core calls them. Carrying them
makes the bridge's list longer, which a core built with it then requires of
Chimera.

## A compile session is counted at what it really holds (user-decided, 2026-10-13, issue 259)

**Reported:** compiling a PlayStation 3 game fails, with every session dead
at the end, on several builds and cores; and free space on C: falls from
18.9 GB to 100 MB while it runs, whichever drive the data folder is on.

**Asked of the owner, and decided:** measure and fix - find what grows, keep
temporary files in the data folder, run fewer sessions when the machine is
short, and say "not enough" in plain words.

**Measured** (Windows, 20 processors, 127 GB, no page file; MotorStorm, an
empty cache):

- One session: the process's private memory peaks at 1.75 GB, its working
  set at 7.0 GB. The system's commit rose by 6.6 GB within a minute of its
  start.
- Four sessions at once, as the wizard runs them: commit rose by 25.8 GB in
  80 seconds and by 27.9 GB at the peak. Private memory, all four: 6.3 to
  7 GB. Working sets: 15.2 GB.
- The system drive lost 0.17 GB in each run, and the cache written was
  171 MB. No temporary file is made outside the data folder.

So what grows is not a file of Chimera's. A session holds about 7 GB, and
5 GB of it is the sandbox's memory, which lives in sections backed by the
page file and is not in a process's private bytes. The wizard counted a
session at 4 GB (from a private-memory figure of 1.75 GB) and counted them
against free memory. Windows has to promise memory before it is used, the
promise is limited by memory plus page file, and when it runs out Windows
makes the page file bigger - on the system drive. Seven sessions asked for
about 49 GB.

**Changed.**

- The count is the engine's (`compile_sessions.h`): one session per 8 GB of
  what can be spared after 4 GB kept back, at most half the processors, at
  most eight. What can be spared is, on Windows, the smaller of free memory
  and of what can still be promised with the page file as it is.
- While sessions run the wizard looks at that figure every two seconds. If
  they die and it was ever under 4 GB, or a session said the sandbox could
  not get its memory, the run goes again with half as many - before, only
  two exit codes counted, and a session Windows refuses a page dies of an
  access violation.
- When one session alone fails for want of memory the wizard says so, and
  says that Windows' page file on the system drive is where the space went.

The rule and the reading of the machine's memory were in the interface;
they are in the engine now, and the interface asks.

**Tested.** The engine's `test_compile_sessions.cpp` (the reported machine
gets three sessions where it got seven; the limits; the smaller of the two
figures; this machine says something of itself), seen to fail with 4 GB put
back. Three tests of the interface: it asks the engine, what counts as want
of memory, and the message.

**Not measured, and it matters:** the page file growing. This machine has
none, so the cause is read from the commit figures and from how Windows
works, not seen. And the reporter's sessions may have another reason to die
as well: their first crash boxes were access violations, which this explains
only if memory was short.

**Open, for the owner:** 8 GB is what a PlayStation 3 session holds, and it
is a number about one core written into Chimera. The core should say it in
its package (beside `"precompile": true`), which is a new declaration.

**Then a setting (user-decided, 2026-10-14).** Asked whether a core should
say in its package how much a compile session needs, the owner chose a
setting instead, 8 GB by default: Tools > Pre-Compiled Modules, "Memory per
compile session (GB)", 1 to 256, remembered. The wizard hands it to the
compile run, which hands it to the engine (`ce_compile_sessions` and
`ce_compile_memory_ran_short` take it; 0 is the engine's default), and the
out-of-memory message names the figure in use and where to change it. No
package declaration was added. Tested: the engine test (4 GB gives the
reported machine seven sessions, 16 GB one), and an interface test that the
window shows the setting and writes it back - seen to fail with the write
taken out. Engine tests 25 passed; end-to-end test script 88 passed;
interface tests 1,077 run, 0 failed, 23 skipped.

## Five cores join the official cores: melonDS, PUAE, Virtual Jaguar, TIC-80 and Uzem (user-decided, 2026-10-14)

These are the next steps toward the owner's goal that every system BizHawk
supports is supported in Chimera too (2026-10-12). Each core was made fresh
from its emulator's latest source; BizHawk's ports were read for ideas only
and nothing was taken from them (the owner's instruction for these cores).
The owner held publishing back while they were finished ("keep this local")
and on 2026-10-14 said to publish all five.

What was decided:

- `official-cores.json` has five new rows:
  - `melonds` (melonDS 1.1): `NDS` (Nintendo DS) and `DSI` (Nintendo DSi);
  - `puae` (libretro-uae): `AMIGA` (Commodore Amiga);
  - `virtualjaguar` (virtualjaguar-libretro 3.9.0): `JAG` (Atari Jaguar)
    and `JAGCD` (Atari Jaguar CD). The package carries Atari's boot ROMs and
    CD BIOSes, which upstream's source embeds (the owner, 2026-10-14);
  - `tic80` (TIC-80): `TIC80`, the TIC-80 fantasy computer. Carts written as
    text (`.lua` and the like, a feature upstream sells in TIC-80 PRO) are
    not loaded, only `.tic` and `.png` carts; Ruby is left out, because its
    build needs a tool neither this machine nor the CI runner has;
  - `uzem` (the Uzebox repository's emulator): `UZE` (Uzebox). Not there:
    the SD card, the SPI RAM, the Uzenet adapter, and turning the picture for
    games made for a vertical arcade screen.
- The README's table of emulation cores lists melonDS after Azahar, Virtual
  Jaguar after Stella, TIC-80 and Uzem after Cadmium, and PUAE after
  AppleWin.
- Each repository posts its releases to the project's Discord channel, as
  the other cores' do.

What was measured before the push, on this machine:

- melonDS: its test script 45 passed, 0 failed, 0 skipped (with the owner's
  BIOS, firmware and games), the window test 6 passed; the same projects on
  Windows and Linux end the same.
- PUAE: test script 27 passed, the window test 4 passed (one earlier window
  run ended with a fault inside the sandbox's own code while a game played;
  it did not come back in later runs and is not explained); Windows and
  Linux the same.
- Virtual Jaguar: test script 31 passed, window test 3 passed; Windows and
  Linux the same. The games tried are free homebrew and upstream's test
  images; no commercial game.
- TIC-80: test script 39 passed, window test 3 passed; Windows and Linux the
  same. No cart from the tic80.com site was tried.
- Uzem: test script 75 passed, window test 3 passed. Thirteen of the Uzebox
  repository's programs give upstream uzem's pictures frame for frame and
  its sound sample for sample. Six projects (the test program, its mouse
  twin and four games) end with the same memory on Windows and Linux.
- Chimera's package tests pass with each package (18 tests each). Every
  default key binding of the five uses a name Chimera sends to the bindings
  (the names after `DistinctKeyNameOverrides`, and Shift, Ctrl and Alt as
  one key each).

What was measured after the push: each core's first CI run passed and
published its development build: melonDS "Development build f322067f" (test
script 20 passed, 0 failed, 25 skipped, because no BIOS and no game may go
on a runner; window test 2 passed, 4 skipped), PUAE "85a942c6" (22 passed, 6
skipped; window 2 passed, 2 skipped), Virtual Jaguar "65df7f90" (28 passed, 4
skipped; window 3 passed), TIC-80 "22714826" (40 passed; window 3 passed)
and Uzem "12a74bee" (75 passed; window 3 passed). Chimera's package tests
passed with each (18). The runs took 13 to 24 minutes.
Each published package was downloaded through `tools/fetch-cores.sh` with
the new rows, and Chimera's roster, package and mnemonic tests passed with
the five (22). Each core's own test script was then run here with the
published core in place of the local build, with the owner's BIOSes,
firmware and games: melonDS 45 passed, PUAE 28, Virtual Jaguar 32, TIC-80 40
and Uzem 75, none failed and none skipped.

## The processor and the memory in a bug report (2026-10-14, issue 277)

Asked for: Help > Report an Issue should give the computer's processor and
memory, which are what a report of slowness is first measured against, and
which people had to look up and type.

Done: the text it fills in has a sixth line, "CPU and memory", for example
`Intel(R) Core(TM) Ultra 7 265KF (20 threads), 47.0 GB of memory`. The
engine reads the two facts (`ce_host_cpu_name`, `ce_host_memory_total`, in
`host_machine.cpp`): the name is the processor's own (the brand string the
`cpuid` instruction gives), and the memory is what Windows
(`GlobalMemoryStatusEx`) or Linux (`/proc/meminfo`) says is installed. The
thread count is the system's. What is not known is left as a note in the form's
style, as the other lines do. The bug report form on GitHub has the line
too, so the text is still pasted over the form's first lines.

Tested: the engine's `test_host_machine.cpp` holds the name to the one Linux
gives in `/proc/cpuinfo` and the memory to more than can be handed out; it
was seen to fail with the name read from the wrong place and with the free
memory read instead of the installed. The interface's report text tests
cover the line filled in and left as a note.

## PUAE runs the Amiga CD32 and the Commodore CDTV (user-decided, 2026-10-14)

The owner asked for the CD32 and the CDTV in the PUAE core, then said to
publish it and update the roster. `official-cores.json` gives the `puae` row
two more systems, `CD32` (Amiga CD32) and `CDTV` (Commodore CDTV), and the
README's table names them beside the Amiga. Nothing in Chimera changed: the
core declares the two machines, their discs, firmware and save memory in its
package.

The core runs UAE's drive and CD audio threads one at a time against the
emulated clock, so a run is the same every time and a savestate holds them.
What was measured is in the core's design log (chimera-core-puae
docs/PLAN.md): with the owner's ROMs, Worms boots from its CD32 disc, both
machines' CD players play an audio CD the tests write, native and sandbox
give the same results with savestates, and Windows gives Linux's. Turrican
II does not start on the CDTV (it restarts after the start-up screen, in a
build with upstream's own threads too).

## An encode keeps its way back in a state file (2026-10-15, issue 282)

Encode Video refused to start on an RPCS3 project with an overflow. Before an
encode borrows the emulator it keeps where the user was, to give it back at
the end; that went through one buffer in memory, which stops at 2 GB. The reporter's state was
2.1 GB.

The way back is now a state file, written and read by the engine exactly as
a branch's state is (#84), at a path in the project's cache folder, outside `Branches/`, whose
tidying deletes any file no branch names. The file is deleted when the
emulator is handed back. If it cannot be written, the encode does not start
and says why.

Not changed: two other places still copy a whole state into a byte array,
Lua's future-frame callback and TAStudio's greenzone integrity check. Neither
is on a path a person takes to make a video.

Tested: the end-to-end test's encode now also checks that no such file is
left behind; it was seen to fail with the delete taken out. End-to-end test
script 88 passed; interface tests 1,077 run, 0 failed, 23 skipped. A state
over 2 GB was not made in a test; the file path is the one branches have
used with 4.31 GiB PlayStation 3 states since #84.

## Vulkan commands in crash notes (2026-10-15, issues 276 and 278)

Two reports crashed inside a Vulkan driver - AMD's (amdvlk64.dll) under
Xenia, Intel's (igvk64.dll) under Dolphin - while standalone Xenia and
standalone Dolphin ran on the same computers. The crash notes named the
driver and an offset; the GPU bridge's flight recorder held only OpenGL
calls, so nothing said what Chimera had just handed the driver.

Decided: Vulkan commands go into the same flight recorder rather than a
second one, so the crash block's layout and the module's reading of it are
unchanged. miniBox's Vulkan host gained a hook called with each opcode
before the driver (`chimera_vk_host_set_call_hook`, miniBox 9b67f67); the
engine's `ce_vk_flight_note` writes it tagged with 0x40000000; the crash
module names tagged entries from miniBox's Vulkan list, generated at build
time like the GL names.

Tested: `test_gl_flight` writes a Vulkan entry directly and through the
host's dispatcher and reads it back tagged; it was seen to fail with the tag
left out. Engine tests 26 passed; end-to-end test script 88 passed;
miniBox's tests 19 passed; the Windows build compiles the module with the
table. Not seen: a real crash note on Windows with Vulkan lines in it.

## A fault taken in host code reaches the guest properly (2026-10-15, issue 280)

Max Payne 3 (Xenia) ended the process at frame 1,247, at its "Press Start"
screen, on Windows, and on Linux too once the Vulkan renderer was on. The
crash was in miniBox's interop blob, entering the host for `mprotect` with
no context: the call came from Xenia's fault handler (it unprotects a
watched page), and that handler had been called for a fault that HOST code
took - the Vulkan bridge writing a command's output into a page Xenia was
watching. miniBox called the guest's handler directly, as if the faulting
thread were inside the guest; it was not, so the handler's first system call
found no context.

miniBox 1f751ee enters the handler through the guest-entry thunk when the
faulting instruction is host code, and directly, as before, when it is guest
code. Its test (RunHostWriteFault: the host writes clock_gettime's result
into a protected page) died of SIGSEGV without the change. Every core with a
GuestFaultHandler (Xenia, and the others that watch memory) takes this path
when a host write meets a watched page.

Tested: miniBox's tests 19 passed; engine tests 26 passed; end-to-end test
script 88 passed; Xenia's test script 27 passed; the Windows build compiles.
Max Payne 3 on the GTX 1060 runs past the old crash into its opening scene,
and in the Linux sandbox with Vulkan its run ends on the same line as with
no renderer. Only disc 1, and only the opening, was played.

## A save cannot destroy a project, and files open under any Windows user name (2026-10-15)

A review of the engine's own code found that several kinds of file could be
lost or never written at all.

- A project was saved by emptying its file and writing it again. A save that
  failed part way - a full disk, the process dying in the GPU driver, a power
  cut - left an empty or cut-short project, and the last good copy was gone.
  Now every whole-file write (`writeFile`: the project, multi-file
  descriptors) goes to `<file>.writing` beside the target, is flushed to the
  disk, and is moved over the old file in one step (`MoveFileExW` with
  replace on Windows, tried again for up to a second while a scanner or the
  indexer holds the file; `rename` elsewhere). A link is followed, so the file
  it names is replaced and not the link. See docs/project.md, "Saving".
- On Windows the C library reads a narrow path in the ANSI code page, but
  the engine is handed UTF-8. The input journal, the saved greenzone
  (history.bin and its spill file), the compile cache, the media maker and
  chimera-run opened their files that way, so for a Windows user whose name
  is not plain ASCII (the data folder is in the user's profile) there was no
  journal to recover from, no saved greenzone and no compile cache, and
  nothing said so. They now go through `openFile`, `removeFile` and
  `replaceFile` in file_io, which widen the path first. The spill file's
  compaction could never finish on Windows either: it renamed over an
  existing file with the C rename, and while the new file was still open.
- The compile cache's names come from the core, which may build them from
  what a game image says. Only `/` was taken as a folder separator, so on
  Windows a name like `..\..\x` reached outside the cache and a store could
  replace any file the user can write. Backslashes, colons, segments of dots,
  segments ending in a dot or a space, control characters and the Windows
  device names are now refused (docs/compile-cache.md). RPCS3, the one core
  that uses the cache, names its objects with `/` and none of these.
- The media maker walked a folder with a loop that throws when a folder
  inside cannot be opened (a drive's "System Volume Information", a folder
  with other users' access rules), and nothing between it and the C API
  caught it, so Chimera ended. It now stops with "cannot read every folder in
  ...". Its zip writer also went back to fill in each file's CRC through a
  32-bit seek, which fails or writes in the wrong place past 2 GiB on
  Windows; it now uses `seekFile`, a 64-bit seek.

Tested: a new engine test (test_utf8_paths) makes a journal, compile-cache
objects, a media image and a project under a folder named with an e-acute
and a u-umlaut, and checks the cache's name rules and the unreadable folder.
It passes on Linux, and on Windows (built by hand, run on this PC); with the
journal's old fopen put back it failed on Windows at the journal, with the
old name check it failed at `..\..\escaped`, and with the old folder loop it
ended in std::terminate. test_project gained a save made to fail after 64 KiB
(a file-size limit): with the old write the first save was lost, now it is
kept. Engine tests 27 passed; end-to-end test script 88 passed; interface
tests 1,054 passed; the Windows build compiles. Not run on Windows: the
saved greenzone and its spill compaction.

## A failure inside the greenzone costs that piece of work, not the session (2026-10-15)

The same review found ways a single failure in the engine's background work
ended Chimera, or quietly put a wrong machine in the greenzone.

- An exception that leaves a helper thread's job ended the process
  (`std::terminate`). The jobs pack and write whole machines - a PS3 anchor
  wants about a gigabyte - so running out of memory there was a crash. The
  helper now catches anything a job lets out and says so on stderr, and each
  job keeps what it hands back consistent when it fails (the fourth rule in
  work_thread.hpp).
- A spill write that failed with the file already open (a full disk) gave
  its stretch back with no bodies: the writer threw them away before saying
  the write had failed. Restoring a frame of that stretch then put a wrong
  machine in place, with no error. The bodies are now freed only when the
  write landed.
- The out-of-memory recovery in a capture halves the budget by thinning,
  which allocates too; a second failure there left the C API and ended the
  process. Now that frame is simply not stored.
- A restore that falls back to its anchor (a delta that will not apply, a
  spill that cannot be read) loads a whole machine, but it skipped what every
  other load does: the inputs were not sent again and the GL and Vulkan
  bridges were not told, so the next frames could play other inputs than the
  movie's and draw from replaced GPU objects. Both paths now share one step.
- Turning the greenzone back on from "Off" stored the machine under the
  session's own frame number, which under the frontend is wherever the last
  restore went; it now uses the frame the last capture reported.
- The MCP tools that change input read "buttons" and "axes" as objects from
  names to values; given a list (`["P1 A"]`), they looked up a name that was
  not there and the program serving the call died. A list is now refused
  with the shape it should have.

Tested: test_work_thread (new) ended in std::terminate without the catch;
test_state_history's new capped-spill test restored a wrong machine with the
old writer and the right one now; the end-to-end test script's MCP agent
sends the list, and the old chimera-mcp died of SIGSEGV on it. The fallback
restore and the greenzone switch are not driven by any test: neither can be
reached without a session, and no engine test opens one.

## A project opened from another path does not use the cached greenzone (user-decided, 2026-10-15)

A project's cache is keyed by its id, and a copy keeps its original's id:
Save As, Save Backup, a recovered copy and a file copied by hand all shared
one cache. Opening one could load another copy's state history, lag log and
branch states under its own inputs (branch states were even matched to
branches by their place in the list), and could delete the other copy's
branch states as unused.

The owner chose the simple rule over checking the cache against the inputs:
the cache already records the path the project was last opened or saved
from (about.json, until now only a label), and a project opened from any
other path - copied or moved - has its cached greenzone, lag log and branch
states discarded, and starts with an empty greenzone. The budgets stay;
they are the person's choice for the project. A cache that never recorded a
path is left alone. Save As records the new path when it saves, so the
session that goes on in the new file keeps its greenzone; the original,
opened again later, starts empty. Opening a backup empties the main
project's cache in the same way, which costs replaying and never work.

Tested: a new test (AProjectOpenedFromElsewhereLosesItsCachedGreenzone) keeps
the greenzone for the same path, discards history, lag log and branch states
for another, keeps the budgets, and leaves an unlabelled cache alone; with
the path check made to always match, it failed.

## A recycled GL buffer is unbound, but may still be one a vertex array points at (user-decided, 2026-10-15)

The GL bridge keeps deleted buffers to serve the next glGenBuffers (a
renderer's generations once cost 1.74 ms of a frame). Since a99045b a
recycled buffer leaves every target it is still bound to, as GL's own
delete would have it, and storage or maps made through the EXT and named
calls stop it being recycled.

What it does not do: send a buffer to the driver for real when a vertex
array, a buffer texture or an indexed binding still holds it. In GL that
holder keeps drawing the old contents; a recycled buffer shows it the next
owner's data. Treating every such buffer as held made 1,127 of Ruffle's
1,648 deletes real ones, giving back most of what the pool saves, for a
pattern - drawing through a vertex array after deleting its buffer - that is
legal but rare and that no core is known to use. The owner chose speed. A
core that does it would draw wrong geometry; the cure then is that rule, or
tracking which vertex arrays point at which buffers.

## Small faults from the code audit, the low ones (2026-10-15)

The audit of 2026-10-15 also listed faults it judged minor: none was seen by
a user. Those fixed in this round, and what each now does:

- **Agents.** The MCP server's notes about a machine (the first memory
  write, the watches, the `advance_until` condition, the search) belong to
  the session they were made in, and start again with the next one. They
  used to be checked by the memory's address, and the sandbox gives a core's
  memory the same address every time. An answer finished after its agent's
  program left is not sent to the next program; a Windows write to a reader
  that never reads gives up after five seconds, as on Linux. A client on
  Linux looks for the server in each folder a server may have chosen.
- **The input journal** is synced to the disk by a thread of its own, at
  most once a second and never more than a second after a change. The sync
  used to wait for the next change, so the last one before a pause was not
  on the disk, and it ran on TAStudio's thread. Files the engine opens on
  Windows are no longer inherited by the programs Chimera starts; a child
  holding the journal open kept it from being replaced, and journaling then
  stopped. Now it carries on in the journal as it was.
- **Identities.** A project's id comes from the system's random source. The
  old one used the library's address, which Windows keeps for every process
  until a reboot, so two Chimeras making a project in the same second made
  the same id. The file-hash cache keys on the full-resolution change time
  and the file's identity, so a copy that keeps the original's date is
  hashed again.
- **Packages.** A package that names no frame rate runs at 60/1 (0/0 was a
  frame rate the frontend divided by). A picture wider than the package
  declares is cropped row by row instead of sheared. A file named
  `.chimeraCore` that is not a package is listed with the reason. A
  property past 2 GiB on a bus is read-only, since the bus takes 32-bit
  addresses. A control called `P<n> ...` with n over 255 belongs to no
  player.
- **Numbers.** A float written to a 64-bit property is checked against the
  exact range (-2^63 itself was refused before, and values near 2^64 were
  converted with undefined results); an axis of 2147483648 is refused rather
  than read as its negative; `0x-1` and `0xinf` are not numbers.
- **Disc and floppy images.** Names that shorten alike each get a name no
  other file in the folder has. Only images with such names change. The
  media maker's last error and hash are kept per thread: the window asks its
  questions on one thread while a pack runs on another.
- **Files a game is made of.** A cue sheet saved with a byte-order mark, or
  with `file` in small letters, names its first track too. A multi-file
  descriptor hashes its files from the disk and reads one only when asked,
  and chimera-run gives the engine their paths, as it does for one file.
- **RAM search.** Running out of memory during a filter leaves the search
  as it was, as the window says. A smaller shape that cannot be had is
  simply not taken.
- **The GL bridge.** A failed bind of its context is said once and the
  core's GL calls are not run, instead of going to the frontend's context.
  Fences a load left behind are deleted once they have signalled. A readback
  over 1 GiB is not guarded, and the readback buffer goes with the context.
- **The compile cache** announces and hashes objects only in a precompile
  session, where the wizard reads the lines.

Each was reproduced by a test first where one could be written, and each
new test failed with the old code. Left for the owner: a FAT12 boot sector
that runs nothing, the ISO path-table order and the auto frame skip (each
changes what the user gets), the guest-visible `sysinfo` unit, and asking
the Vulkan bridge for device functions.

## The rest of the audit's small faults: history, the runner, the build (2026-10-15)

The second half of the low findings, after 64d228b:

- **The state history.** A packed body is read through a buffer no larger
  than the body and not cleared first (a small link used to zero a megabyte).
  A reader of a shared file seeks only when the file is not where it left it:
  the Windows C library throws its buffer away on every seek, which cost a
  system call and a 4 KB read for each small field of a history being opened
  (not measured on Windows). The disk budget no longer drops stretches whose
  write has not landed, which freed nothing it counted. A compaction that
  renamed its file applies the new offsets at once, and forgets the spilled
  stretches if the file cannot be opened again. A stretch whose anchor is
  bigger than the memory budget is not settled, as docs/state-manager.md
  says. A background save that failed is reported by the next one.
- **The machine.** A null sound buffer is silence; a save-data read is held
  to what was asked; the GL context is given back after the core's
  StateLoaded in the buffer and file loads. The three load paths still call
  StateLoaded at different points relative to the GL and Vulkan notices;
  making them agree needs the GPU cores tried, and is not done.
- **chimera-run.** A property set and a loaded state are part of the
  history's anchor and of the `--rerecord` state (the set value was lost on
  every frame of a `--rerecord` run); `--state` with `--history-in`, and a
  greenzone check over two different frames, are refused; every output's
  write is checked.
- **The build.** The engine is compiled once, into a static library linked
  whole into libchimera, the tools and the tests: a change to session.cpp
  compiles one file, not 29. The exported functions are the same on both
  systems. The engine's and the sandbox's commits are read at every build
  (vcs_tag) and say "-dirty" for a tree with uncommitted changes; the bridge
  generators depend on gl.h and vk.xml; the nested zstd, SDL2 and OpenAL
  builds are asked at every build.
- **Scripts.** The no-network check reads the sandbox's host side too; an
  ffmpeg given by hand needs its licence, and is taken before 7-Zip is
  looked for; a dev release that could not be deleted stops the publish.
- **The interface.** An agent's lines are applied with one invalidation; a
  movie line's axis values are written and read in no culture; the MCP
  dialog keeps the last 5000 lines.

Left for the owner, as before: the FAT12 boot code, the ISO path-table
order, the auto frame skip, `sysinfo`'s unit and the Vulkan device
functions; and TAStudio does not yet show a failed background save.


## Gambatte joins the official cores: the Game Boy, the Game Boy Color and Game Boy Link (user-decided, 2026-10-15)

The owner said to publish every session's pending work, new cores with
their repository, roster row, website row and release notifications. The
Gambatte core is public as ToolAssisted-run/chimera-core-gambatte. It
builds Gambatte-Speedrun (pokemon-speedrunning/gambatte-core) unchanged,
with no patches. BizHawk's source was read for ideas only; nothing was
taken from it, not even a test file (the owner's rule, 2026-10-15).

- official-cores.json has the row gambatte: GB Game Boy, GBC Game Boy
  Color, SGB Super Game Boy, GBL Game Boy Link.
- The README's table of cores lists it after melonDS.
- Game Boy Link is a machine of two, three or four Game Boys joined by link
  cables, each with its own game and saved data, as in BizHawk.
- The boot ROMs are firmware the user brings (`dmg_boot.bin`,
  `cgb_boot.bin`, `agb_boot.bin`, `sgb2_boot.bin`); playing without one is a
  setting.

Measured before publishing, built against miniBox dac416f: the core's own
test script 55 passed, none failed or skipped (with the owner's boot ROMs and
games; on CI the tests that need them are skipped); the window tests 6
passed; Chimera's package and mnemonic tests 18 passed. One test plays
Pokemon Gold from power-on to a save with inputs of the repository's own,
then trades a Pokemon each way between two linked games. The first CI run
passed (43 passed, 8 skipped for want of the boot ROMs and games; window
tests 4 passed, 2 skipped; package tests 18 passed) and published
"Development build 9a9cd75f". Not tried: a link battle or any other link
game, and a Super Game Boy cannot be linked.



## VICE and MAME Home Systems join the official cores; ares adds the 64DD and the Sufami Turbo, Genesis Plus GX the Pico (user-decided, 2026-10-15)

The owner said to publish every session's pending work, new cores with
their repository, roster row, website row and release notifications. Two
new cores are public, and two published ones gained systems. All four are
part of the aim of covering every system BizHawk runs; nothing was taken
from BizHawk.

- **VICE** (ToolAssisted-run/chimera-core-vice): the Commodore 64, built
  from VICE's x64sc emulator through the vice-libretro port. The package
  carries VICE's own C64 and 1541 ROMs, as the owner decided.
- **MAME Home Systems** (ToolAssisted-run/chimera-core-mamehome): the home
  machines of MAME in one package, a Machine setting choosing among 25 -
  Atari 7800 (A78), Intellivision (INTV), Vectrex (VEC), Odyssey 2 /
  Videopac (O2), Channel F (ChannelF), Amstrad CPC with the CPC+ and GX4000
  (AmstradCPC) and TI-83 (TI83). The user brings each machine's own ROMs as
  firmware; the GX4000, CPC464+ and CPC6128+ need none. A CPC's disks can be
  swapped while it runs, and Export Save Data hands out the disks it wrote.
- **ares** adds the Nintendo 64DD (N64DD, a machine of its own: the drive's
  IPL ROM is the user's firmware and the disk is the game; a 64DD game saves
  on its disk, so Export Save Data hands out the written disk) and the
  Sufami Turbo (the base cartridge with one or two linked minicarts, run as
  a Super Famicom cartridge with no id of its own). Its Nintendo 64
  cartridge saves now export in the order ares writes them, and the Save
  data slot is used; both were wrong before.
- **Genesis Plus GX** adds the Sega Pico (PICO): the pen as two axes, its
  button, and the storyware pages.
- official-cores.json has the rows vice and mamehome and the new systems of
  ares and gpgx; the README's table of cores lists VICE after PUAE and MAME
  Home Systems after MAME.

Measured before publishing, built against miniBox dac416f with the owner's
firmware and games: VICE's test script 21 passed; MAME Home Systems' 77
passed and its window tests 4; ares's 97 passed with nothing skipped (the
64DD with SimCity 64, the Sufami Turbo with two SD Gundam Generation
minicarts, Super Mario 64's EEPROM out and back in); Genesis Plus GX's 30
passed; Chimera's package and mnemonic tests 18 passed for each. The first
CI runs passed with what needs no licensed content (VICE 18 passed, 3
skipped; MAME Home Systems 18 passed, 8 skipped; ares and gpgx as before)
and published "Development build 4755a656" (VICE), "3956272c" (MAME Home
Systems), "32c17a0e" (ares) and "6dab3e2c" (Genesis Plus GX).
tools/fetch-cores.sh fetches all four with the new rows, and the package
tests pass on what it fetched. Not tried: the Sufami Turbo's linked games
past their first screen, and a 64DD game other than SimCity 64.

## The owner's answers on the audit's open questions (user-decided, 2026-10-15)

Seven questions the audit's small faults left open were put to the owner one
at a time. What was decided, and what was done:

- **Floppy images run a program when booted.** The boot sector the media
  maker writes now holds our own few instructions: print that the floppy
  holds files and no system, wait for a key, start the boot again (int 19h,
  as a DOS data floppy does). Proved by booting a made floppy in the
  DOSBox-X core with chimera-run: the message is on the screen at frame 1499.
  Only floppies made from now on differ.
- **ISO path tables follow the standard**: by level, by the parent's number,
  by identifier, each table by its own identifiers. xorriso reads a made
  image with no complaint. Only images with folders at one level under
  different parents differ.
- **Frame skip works, and is off by default.** The throttle reset its own
  measurement on every pass, so auto frame skip never skipped; it now resets
  only when the rate changes. The default is no skipping, and a config
  written before, which carries the old default of 4 that did nothing, is
  set to 0 once (Config.NeverSkipOnce, recorded in FrameSkipSettled), so
  nobody sees a change they did not ask for.
- **sysinfo's unit by guest kit revision.** The sandbox wrote mem_unit four
  bytes early. Cores built with the guest kit from now on export
  `__wbx_kit_revision` (from emulibc.c, which every core links) and get the
  corrected answer; cores built before keep the old one, so their movies
  replay (miniBox MACHINE-SPEC, "Guest kit revisions").
- **vkGetDeviceProcAddr answers NULL for a command the device lacks.** The
  guest now asks the host whether the driver gave the command under any of
  its names (a new bridge op). The bridge's test compares, for seven
  commands with two names, what the driver has with what the bridge says.
- **The engine reads a movie by its LogKey's names**, as the window does
  (EntryLayout::buildFromKey): reordered columns are read as what they are,
  a column the machine lacks is skipped and written as '.', a control the
  key lacks is released or neutral. A movie with no key is read by the
  machine's order, as before. chimera-run and the MCP server without a
  window read movies this way now; the witness replays the win movie with
  its columns reversed and a key saying so, to the golden machine.
- **A document left on a branch.** The measurement of what a PS3 package
  install costs (2026-09-20) was put on main at its own date, and two local
  branches that were already merged or now carried were deleted.


## A block that a bus already shows is hidden from the memory tools (2026-10-15)

Issue 288: in an ares Nintendo 64 project, RAM Search, RAM Watch and the Hex
Editor listed RDRAM twice, and the two copies disagreed on every value wider
than a byte. ares publishes the console's memory twice: as blocks, which are
its own storage with each 32-bit word's bytes in the computer's order (the
core's tests digest them), and as buses in the console's order, mostly under
the same names. Nothing new was needed in Chimera: the core marks the blocks
hidden with the export a game core already uses for its mailboxes
(`GetMemoryDomainHidden`, issue 218), lists its buses with RDRAM first, and
exports `ReadBus` (chimera-core-ares 1597345). The porting guide and the
comment on `ce_session_domain_hidden` now name this second use. Measured on
Super Mario 64: a search over the 8 MB DCache bus went from 193 ms to 56 ms.

## A package's default bindings name the controls exactly (2026-10-16)

Issue 284: the Dolphin core's default bindings said "A" and "Main Stick X"
while its controls are "P1 A" and "P1 Main Stick X". Chimera binds a control
only by its exact name, so none of the 75 defaults ever applied, and the
contract test that should have caught it compared names with the player
prefix taken off, and buttons only. It now compares exact names, axes
included (`KeybindsOnlyNameControlsTheControllerHas`), and fails on the
package from before the fix (39 names that are no control in its nightly of
2026-10-15). It judges each core's newest package only
(by the package's versionDate): Chimera keeps a
core's older versions for the projects made with them, and an older version
cannot be fixed - two dated Dolphin packages from September in a
developer's build/Cores failed it. The core's own
check (`check-keybinds.py`, chimera-core-dolphin e8bd847) holds the same
from its side; a scan of the other 44 cores' bindings found no other.

## A firmware scan names the Firmware folder when there is none (user-decided, 2026-10-16)

Issue 293: Config > Paths names a Firmware folder, `./Firmware` beside
Chimera, that nothing creates. People keep their dumps elsewhere and point
Scan Folder at them, which remembers each file it matches but left the
folder entry alone. So Scan Folder opened wherever Windows had last been,
and Config > Paths named a folder that was not there.

Decided by the owner: when a scan (Config > Firmware, or the wizard's
firmware page) finds firmware and the folder Config > Paths names does not
exist, the scanned folder becomes the Firmware folder. A folder that
exists is the person's choice and is never replaced. Not chosen: opening
the picker on the last scanned folder while Paths stays as it is, or
creating the default folder at start, which would leave the picker
pointing at an empty folder.

The wizard reports
the folder through `firmwareFoundIn` when the folder holds a file one of
its requirements asks for, chosen already or not. Tests:
`AScannedFolderBecomesTheFirmwareFolderOnlyWhileThatIsNotThere` (fails with
the "folder exists" check removed) and
`AScanThatFindsFirmwareSaysWhereAndOneThatDoesNotSaysNothing` (fails with
the wizard's report removed). A file a scan found below the Firmware
folder is remembered by its own path, as before. (The Firmware folder
itself was then read without its sub-folders; since issue 359 it is read
with them.)

## A choice's values have labels, and the wizard lists the Boot ROMs on hand first (user-decided, 2026-10-16)

Issue 293: a PlayStation 2 project picks its BIOS with the Boot ROM setting,
whose seventy values are file names such as `ps2-0230a-20080220`. Nothing
said that the `a` means USA, and only the next page, firmware, said whether
the file was here. Files are found by their SHA-1, so their names never
mattered, but the list gave no way to know that either.

Decided by the owner, of three choices (on-hand first and labels; on-hand
first only; explain only):

- **A new optional package field, `optionLabels`**: an enum setting's
  values may each have words to be shown by. It is display only; the
  stored value, the project and the movie keep the value. Older Chimera
  builds ignore the field, since unknown fields of a package are ignored.
  The settings grid's converter shows the label and maps a label back to
  its value; nothing stores a setting through the converter.
- **On-hand values first, marked "(found)"**, on the wizard's settings page.
  The wizard asks the engine's firmware decision tree
  (`ce_firmware_evaluate`) once per value of every setting a firmware
  condition names, with the other settings and the chosen files as they
  stand. The files every value calls for are set aside; a value whose own
  files are all on hand (the Firmware folder, or a path remembered for the
  core) is found. So the rule knows no core and no setting by name, and a
  setting that decides no file is left exactly as declared. Config > Core
  Settings shows the labels but no marks: it has no firmware to look at.

Not done: moving the Boot ROM choice onto the firmware page, as the
reporter suggested. The setting stays where every setting is.

Tests: `WizardFirmwareChoicesTests` (three). Each failed with the ordering
removed, and all three with the labels ignored.

## The Retro Engine cores: one a decompiled engine, Mania's logic compiled in (user-decided, 2026-10-17)

Issue 295 asked for the RSDKv3, RSDKv4 and RSDKv5 decompilations (Sonic CD;
Sonic 1 and 2's 2013 remasters; Sonic Mania). The owner chose three game
cores, one per engine, rather than one v5U core playing all four games: the
three code bases share global names, so one sandbox cannot hold them, and
each older engine is the one its games' data files were made for.

- **Licence.** The decompilations allow copying, changing and distributing,
  for public or private use, but not for profit, with credit, with no game
  asset, with the licence beside modified source, and with the downloadable
  content off by default in any build given out. Each core is built with
  `RSDK_AUTOBUILD`, declares `commercialUse: no` and carries the texts. For
  Mania the owner asked for a Plus Content setting: allowed by the licence
  "so long as it is set to off by default", and it is (a patch makes the two
  places the engine forced the flag off read the setting).
- **The game's data is firmware.** Each data file is the project's firmware,
  by the hash of the copy checked (Android for v3 and v4, the owner's for
  Mania); any other file may take its place, as for every game core.
- **v4 needs an OpenGL.** The remasters' menus, 3D title and touch controls
  are drawn only through the engine's OpenGL 1.x renderer, so the core
  compiles in Mesa's softpipe behind OSMesa, as the touchHLE core does.
- **v5 has no test game.** Its game is a program (the Sonic Mania
  decompilation, compiled in), not scripts in the data, so CI proves the
  build, the declarations, the refusals and the contract tests; everything
  that plays runs where the data file is. The v3 and v4 cores test on games
  written for them from scratch, in CI.

Measured on the real games: Sonic CD 3600 frames, Sonic 1 and 2 4100 each,
Mania 5200 (its opening video included), native = sandbox, a save and a
load before every frame and a state moved to another process all landing on
the straight run; the packages in Chimera on Windows matched Linux byte for
byte. Nothing in Chimera's own source changed: a roster row for each core,
and the README's game-core table.

## Auto-hold on an analog axis asks what value to hold (user-decided, 2026-10-17)

Issue 298: TAStudio's Auto-hold held an analog axis at its maximum, always
(32767 on a PlayStation 2 stick, 127 on a Nintendo 64 one), and there was no
way to hold another value except typing it into every frame.

Decided by the owner, of four choices (ask with the current value filled in;
hold the current frame's value without asking; a value per axis in TAStudio's
settings; leave it and explain the custom-pattern way):

- Turning Auto-hold on for an axis's column asks for the value, filled in
  with the axis's value on the current frame. A value outside the axis's
  range, or not a whole number, is refused with a message and Auto-hold
  stays off; Cancel leaves it off.
- The answer is kept in the project, with TAStudio's other per-project
  settings (`AxisHoldValues`), so a project reopened
  holds the same value. An axis with no answer, or with one its range no
  longer allows, holds the maximum as before; a project saved before this
  has no answers.
- With the custom pattern selected, the pattern says what the axis takes,
  so nothing is asked. Autofire on an axis uses the same value as Auto-hold.

This is interface only: the held values it feeds are the window's.

Tests: `TAStudioAutoHoldValueTests` (five): the chosen value is held, a value
out of range holds the maximum, only a value of the axis is taken from what
was typed, the choices survive a save and load, and an older project has
none. With the chosen value ignored (the maximum always held, as before),
the first of them failed. The prompt itself is not driven by a test.


## FreeJ2ME-Plus joins the official cores: a Java VM inside the sandbox (user-decided, 2026-10-17)

Issue 297 asked for Java phone games (J2ME, and DoJa/i-appli), with two
emulators in mind, FreeJ2ME-Plus and openDoJa. Both are written in Java,
and an earlier plan (2026-09-19, for KEmulator) had concluded that a core
could not host a Java VM. That was before miniBox's machine spec 3 gave a
guest timed waits on the sandbox's own clock.

`chimera-core-freej2me` runs FreeJ2ME-Plus on OpenJDK 21's HotSpot built as
Zero (its interpreter only, no generated machine code), linked statically
into `core.wbx` with the JDK's native libraries. Every file the VM opens is
served from guest memory; the runtime's files are built into the core
image, so no savestate carries them; the VM starts during Init, before the
seal. Sound goes through a Java Sound mixer of the core's own. A save and a
load around every frame, a state moved to another process, two runs, the
headless runner and the window on Linux and on Windows all land on the same
machine, on the core's test program and on five commercial games.

Decided by the owner on the way: the licence basis (HotSpot is GPL-2.0-only
without the Classpath Exception, FreeJ2ME-Plus GPL-3.0-or-later; the core
keeps the java launcher's boundary - its C calls Java through JNI, and
nothing in Java calls back), and publishing the repository. Found on the
way and fixed in miniBox (c0aad51): host values left in the vector
registers at every entry into the guest.

Issue 297 is closed without openDoJa (user-decided, 2026-10-17): an
openDoJa core is a new request, in an issue of its own. No DoJa game has
been tried yet. Nothing in Chimera's own source changed: a roster row and
the README's table of cores.

## A compile session is the first thing Linux stops (2026-10-17)

Issue 300: compiling Ratchet & Clank: Tools of Destruction (a 16 GB
package) on a 31 GB Linux computer, the kernel stopped Chimera itself a
minute in. Two causes. The RPCS3 core installed a package into memory once
per session, so each held the whole game; that is fixed in the core (an
installed file is kept as a reference to the package, chimera-core-rpcs3,
the same day). And when memory runs out, Linux stops the process it judges
the biggest, which was the window rather than a session, so the
orchestrator's answer to memory running out - halve the sessions and go
again - never got to run.

A session now asks the kernel to stop it before anything else
(`ce_compile_session_begins`: oom_score_adj 1000, which needs no privilege).
On Windows there is no such choice: the system refuses the memory, and that
case was already handled (issue 259). Exit code 64, which a headless session
gives when it meets a message box it cannot show, is reported by name
("stopped at a message it could not show"); issue 296 saw it as "exit code
64".

Measured on Linux, one Tekken 5 session: 6.4 GB proportional, 5.6 GB of it
the sandbox's, and the system's free memory fell by about the same. The
engine's 8 GB a session stands.

Tests: `test_compile_sessions` checks the score in a child process (it
failed with the write left out), and `ADeadSessionAlwaysSaysHowItDied`
checks the wording for 64. Engine tests 30/30, interface tests 1,076 passed,
the witness 89/0; the Windows build compiles.

## Vulkan's instance and device are made on a thread of their own, on Windows (2026-10-18)

**The reports.** Issues 278, 306, 307 and 308: Chimera closed when a Vulkan
core started (Dolphin, RPCS3's pre-compile, xemu) on two machines with
Intel's legacy driver, 31.0.101.2111 and .2112. Every crash note is the same:
the driver raises a C++ exception inside `vkCreateInstance` and nothing
catches it. The published nightly's own `libchimera.dll` named the caller,
`vkb_special_vkCreateInstance`.

**What was decided.** On Windows the bridge creates the instance and the
device on a thread made for the call and waits for it (miniBox 5ed4270). A
core calls in on Chimera's window thread, a single-threaded COM apartment;
standalone Dolphin, which starts on the same driver, creates its instance on
a thread of its own. Memory was ruled out from the minidumps (gigabytes of
commit to spare) and so was an old loader (one machine has 1.4.309). Whether
the thread is the cause is not proven: there is no Intel card here. A
driver's exception cannot be caught by code from Chimera's compiler, so if
it is not the thread, OpenGL stays the way on those machines.
docs/gpu-bridge.md has the measurements.

**And the compile window.** A session that died of an uncaught C++
exception (exit code 0xE06D7363) was reported as "exit code -529697949", or,
when the machine's memory looked low while sessions ran, as running out of
memory alone (issue 307). The code now has words, and a memory verdict also
says how the sessions ended when that was a crash.

## A core's floating-point settings are its own, the same on every system (user-decided, 2026-10-18)

**What was found.** The processor has two small settings that decide how
floating-point arithmetic rounds: the x87 control word (for the old
floating-point unit: precision and rounding) and MXCSR (the same for SSE,
plus what happens with very small numbers). Nothing at the sandbox's
boundary set them, so a core ran with whatever the calling thread had.
Windows starts its threads at 0x27F (53-bit precision), so the interface on
Windows ran every core at 53-bit x87 precision; chimera-run, every test
script and the interface on Linux ran them at 0x37F (64-bit). The C library
inside the sandbox (musl) assumes 64-bit: at 0x27F it printed 2.7 rounded
to a whole number as 2, and read 94% of two million decimal numbers back as
a slightly different value. A core that prints or reads decimal numbers
could therefore behave differently in the interface on Windows than in
every test. The other way round, a core that changed MXCSR (PCSX2 sets
round-toward-zero for the emulated CPU) left it changed on Chimera's thread.

**What was decided.** The two settings are part of the machine, for every
core now, old and new. The owner chose this over an opt-in per core: on
Linux nothing changes, and on Windows a movie recorded in the interface
with a core that prints or reads decimal numbers may replay differently.
A core starts with 0x37F and 0x1F80. They are loaded on every way into the
core (its exports, its start-up code, the return from a system call or from
a call to Chimera such as a GPU call) and Chimera's are put back on every
way out. What a core sets stays with the core and is saved in savestates;
a state from before loads with the starting values. miniBox 1c10a81;
the rules are in miniBox's MACHINE-SPEC, section 9.

**Measured.** On the GTX 1060, PCSX2 with OpenGL for 2401 frames took 8.1
to 8.8 s before and 8.3 to 8.6 s after (three runs each), with the same
picture at frame 2400 and the same recorded file. Each call from a core to
Chimera costs about 20 ns more. For Ruffle, the core that makes the most of
them, that is about 0.1 ms in an average frame (6,000 GL calls) and 1 ms in
the heaviest (50,000), against a frame of about 48 ms. miniBox's tests now
check the settings at start-up, in an export, after a system call, inside
and after a call to the host, and across a savestate; each check was seen
to fail with its part of the change removed. Run on Linux and on Windows.

**A compiler trap found on the way.** Chimera builds miniBox with -O3. At
that level GCC removed the call into the core's start-up code: the call
went through a constant address handed to a function it inlined, and a
constant that is not a function's address counts as unreachable to it.
miniBox's own build optimises less and passed every test; PCSX2 under
chimera-run crashed. The address now goes through an empty asm statement,
which the compiler cannot see through.

**Not checked:** whether any existing movie made in the interface on
Windows replays differently.

## A core's fault handler runs on its own thread's stack on Linux (user-decided, 2026-10-18)

**What was found.** Some cores protect parts of their own memory and want to
hear when it is touched: an emulator's texture cache watches the emulated
console's memory that way. Such a core exports a fault handler, and miniBox
calls it when a protected page is touched. On Linux that call was made from
inside the operating system's signal handler, which runs on a separate stack,
one per thread of Chimera. RPCS3's handler can wait for its renderer, and
waiting means switching to another of the core's threads; when that thread
touched a protected page too, its signal was delivered onto the same separate
stack, over the first handler that was still waiting there. The miniBox
analysis of 2026-10-18 proved the overwrite with a test program; the outcome
is Chimera crashing, or a core going on with wrong values. Windows was never
affected: its handler already runs on the stack of the thread that faulted.

**What was decided.** The owner chose to run the core's handler on the stack
of the thread that faulted, as Windows does, over refusing the thread switch.
The signal handler no longer calls it. It points the thread at a small piece
of code in miniBox (a trampoline) and returns; the trampoline saves every
register and the vector registers, calls the core's handler on the thread's
own stack, puts everything back and runs the faulting instruction again. A
handler that does not claim the fault leaves a mark, and the retry is then
an ordinary crash of the core, as before. miniBox 2a4107b; the rules are in
MACHINE-SPEC, "Guest fault forwarding".

**Tested.** miniBox's thread test has three new checks:
- a handler that switches to another thread, which faults and runs the
  handler too (this crashed the test program before the change);
- every register, the flags and the vector registers, AVX included, are
  what they were after a handler that changed them;
- a fault the handler does not claim ends the core and not Chimera.
Each was seen to fail with its part of the trampoline removed. They pass on
Linux and on Windows. With real cores on Linux, against the previous
miniBox: RPCS3 running Rayman Legends on its GL renderer called the
handler 1015 times in 400 frames, twice with the same result and the same
as before; Xenia running Mirror's Edge through chimera-run called it 776
times in 300 frames, with the same memory and picture as before.

**Not checked:** Vita3K, the third core with a handler (its build is not on
this machine). RPCS3's own test script passed (41 passed, 2 skipped), but
its GL test served no faults: a `||` and `&&` mix-up in it lets two empty
counts pass. To be fixed in the RPCS3 repository.

## Open files and the random-number stream are part of a savestate (user-decided, 2026-10-18)

**What was found.** A core reads its game files through the sandbox's file
descriptors, each with its own position. Those positions, which descriptor
had which file open, and the position of the stream that answers a core's
requests for random bytes (getrandom) were kept by miniBox outside the
machine, so a savestate did not carry them. Set aside as too complex on
2026-10-15. The analysis of 2026-10-18 found a concrete case: PCSX2's disc
swap closes one disc image and opens the next, which gets the same
descriptor number, so going back to before the swap read the second disc
where the first was expected. A core that reads a file without seeking
carried on from wherever the abandoned run had left it.

**What was decided.** The owner chose to put them in states and deltas.
After the thread set comes a record of every open descriptor (its number,
the name of the file it has open, its position) and the random stream's
position. A load puts all of it back; a state or delta written before has no
record and loads leaving them as they are, as before. miniBox c0c2ebc; the
layout is in MACHINE-SPEC, section 6.

**Tested.** miniBox's conformance test, on Linux and on Windows:
- a descriptor closed after a save and opened on a second disc reads the
  first disc, from where it was, after the load;
- the random stream goes on from where it was;
- a delta applied to the state it was measured from lands on the machine
  after a disc swap, descriptor and stream included;
- a state written before this change still loads, leaving the open files
  as they are.
Each check was seen to fail with its part removed. Chimera's engine tests
(30 passed), the witness (89 passed) and the PCSX2 core's test script (44
passed, 6 skipped for content not on this machine) pass with it.

**Not checked:** a real disc swap going back over the swap in Chimera's
interface; the test above does it inside miniBox.

## Memory a core frees is given back on Linux, and untouched memory is not written on either system (user-decided, 2026-10-18)

**What was found.** When a core frees memory (munmap, or MADV_DONTNEED, which
is how its allocator gives memory back), miniBox has to make those pages
read as zeros. It did that by writing zeros over every page, through the
second view of the core's memory that the host uses. That allocated real
memory for pages the core had never touched, and kept it: RPCS3 frees 4 GiB
of address space it reserved and never used, at every boot. Nothing was
ever handed back to the operating system either.

**What was decided.** The owner asked for memory to be given back on Linux
(by punching a hole in the file behind the core's memory, which reads back
as zeros) and on Windows (by decommitting it). Windows cannot do it: a page
committed in a view of a section stays committed. Measured on the GTX 1060
box, VirtualFree refuses to decommit it (error 87); MEM_RESET keeps the
bytes; DiscardVirtualMemory frees them but documents what reads back as
undefined, which a deterministic machine cannot build on. So:
- on both systems, a page that is clean and was zero when the core started
  is zero already and is not written at all;
- on Linux, a freed run of 16 pages or more goes back to the operating
  system;
- a shorter run, and any run on Windows, is written over as before.
miniBox 1d2be2e.

**Measured.** RPCS3 booting Bejeweled 3 for 60 frames, two runs each: the
most memory it held went from 6.05 GB to 1.81 GB, and the run from 8.1 to
9.0 seconds to 6.3, with the same frames.

**Tested.** A new unit test frees 1 MiB that was written (the memory behind
it went from 1024 KiB to 0) and 1 MiB that was not, in pieces too short to
release (nothing was written), and reads zeros back through both views.
Each part was seen to fail with it removed. Chimera's engine tests (30
passed), the witness (89 passed), the RPCS3 core's test script (41 passed, 2
skipped) and PCSX2's (44 passed, 6 skipped) pass with it.

**Not done:** giving memory back on Windows, for the reason above. The
owner may want DiscardVirtualMemory followed by writing the zeros, which
frees nothing that stays in use, or another way; it is left open.

## Savestate format 2: older builds refuse today's states by name (2026-10-18)

**What was found.** The engine analysis of 2026-10-18 found that the two
records miniBox added to every savestate that day (the floating-point
settings and the open files) were shipped without a new savestate format
number. This build reads states without them, but an older Chimera cannot
read states with them, and since the number stayed 1 it did not refuse them:
it failed half way through, after the core's memory had been replaced, and
played on with a mix of two machines. A state naming a file that is not
mounted (a ROM opened directly and renamed since) failed the same way in
this build.

**What was done.**
- The engine writes savestate format 2 and reads formats 1 and 2: a change
  that only adds to a state raises the format written and keeps the older
  ones readable (kOldestReadableStateFormat). An older build refuses a
  format-2 state by name before it touches its machine. A greenzone history
  of format 1 is kept. The interface's note at project open only speaks of
  formats this build cannot read.
- miniBox (2ad689f): a state naming a file that is not mounted loads leaving
  the open files as they are, as before today; and a load that fails after it
  began replacing memory marks the machine dead, with the reason, until a
  state loads, as MACHINE-SPEC always said it should.
docs/state-manager.md, "The savestate format number", has the rules.

**Tested.** New checks, each seen to fail with its part removed: every
format from the oldest readable to the current one loads and the ones around
them are refused; a format-1 history is kept; in miniBox, a state naming an
unmounted file loads, a state cut short in its thread set leaves the machine
dead until a good one loads, and a refused one does not. Engine tests (30
passed), the witness (89), the interface tests (none failed), the Windows
build, miniBox on Linux and Windows, and the PCSX2 core's test script (44
passed, 6 skipped) pass.

## A broken core ends its machine, not Chimera, in seven more ways (user-decided, 2026-10-18)

**What was found.** The miniBox analysis of 2026-10-18 listed the ways a
core with a bug still took Chimera down with it, instead of ending only its
own machine (which Chimera survives, keeping the project and the greenzone):
a crash in the core's start-up code or while it is sealed; a call through a
null or wild function pointer, or to a host callback that was never
registered; a system call given a pointer to memory the core had freed; on
Linux, the system refusing a memory protection change; a crash report that
read memory it could not; a too-small signal stack left by a frontend's
runtime; and on Windows, memory a core gave up that stayed accessible.

**What was decided.** The owner asked for all of them, as one round with a
test each. Each is now the core's death with a reason, or an EFAULT answer
as Linux would give, and Chimera carries on (miniBox cf163d2; MACHINE-SPEC,
"A broken guest ends the machine, not the host").

**Not done.** On Windows a core whose stack overflows into its guard page
still ends Chimera: Windows builds the record of the fault on that same
stack, and its own dispatcher and the handler need more room than a guard
page leaves. The crash note (chimera_crash.dll) still records it. Checking
GL pointers is left for the engine's GL round, which changes the same code.

**Tested.** A test for each way, each seen to fail with its part removed:
two cores that die while starting and while sealing; calls through a null
pointer, to a wild address and to an unregistered callback; twelve system
calls handed freed memory; a null call from a core that keeps its own thread
pointer; a refused protection change; a 16 KiB signal stack replaced; and on
Windows, no access over a partly committed range. miniBox's tests pass on
Linux and Windows, and so do Chimera's engine tests (30), the witness (89),
the PCSX2 core's test script (44 passed, 6 skipped) and RPCS3's (41 passed,
2 skipped).

## A core that dies after its frame, or while a state is taken, is noticed (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18: the engine checked
whether the core was still alive right after its frame, and nowhere else.
The core runs more code after the frame (the picture's readback, which is GPU
work on several cores, the audio, the question whether input was read) and
before every state (StateSaving, which nine GPU cores implement with
readbacks). A core that died there went unnoticed: the frame was counted,
recorded in the movie and stored in the greenzone, and a branch or a quicksave
kept the dead machine (a later load brought it back to life). A greenzone
restore whose anchor would not load either left the machine a mix of two, and
the session played on.

**What was decided.** The core's death is checked after everything it runs
for a frame and after StateSaving: such a frame is not counted, recorded or
stored, and such a state is not taken. A greenzone restore whose anchor will
not load goes back onto the nearest earlier anchor that does (the owner's
choice); only when none does is the machine treated as dead until a state
loads or the project is opened again. Packed greenzone bodies, archive lumps
and branch state files carry zstd checksums now, so damage fails to load
instead of loading a wrong machine, and a branch state file is on the disk
before it takes its name.

**Tested.** Three new witness legs make the synthetic core die in its
picture readback, its input question and StateSaving: the run stops with the
core's reason and 11 frames done rather than 12, and under save-and-load
around every frame no dead state is loaded back to life. Two greenzone tests
cover the earlier anchor and a capture the core died in. Each was seen to fail
with its part removed. Six one-byte changes to a PCSX2 state file that only a
checksum can see (as the zstd tool shows) are all refused now. Engine tests
(30), witness (92), interface tests and the PCSX2 core's test script (44
passed, 6 skipped) pass.

## The GPU bridge refuses missing functions and memory the core does not have (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18 found three ways a core's
GL calls could end Chimera or make a machine depend on something outside it.
A GL function the graphics driver does not provide was called through a null
pointer (the core is given a wrapper for every name, whatever the driver has).
A readback with a pack row length smaller than the picture's width made the
driver write past the engine's buffer into its heap. And a readback whose rows
leave gaps (a row length larger than the width, or rows padded to the pack
alignment) wrote an earlier readback's leftovers into the core's memory
between the rows; xemu reads surfaces straight into console RAM before every
state, so the machine depended on what had been read before it. The miniBox
analysis of the same day had also found that GL calls follow pointers the
core hands them without asking whether the core has that memory.

**What was decided.** The owner chose to fix every confirmed finding of the
analysis, one tested change each. A function the driver lacks answers 0, as a
call the driver refused, in every case, generated or hand-written, and the log
says which, once. The readback buffer covers everything GL writes, and only
GL's rows are copied back. Before a buffer upload or a readback, the engine
asks miniBox whether the core's range is allocated with the access needed (a
new export, wbx_guest_range_ok); a pointer into memory the core gave back is a
refused call. Other GL calls that take a pointer are not checked yet; the
detail is in [gpu-bridge.md](gpu-bridge.md).

**Tested.** Four new GL tests (on Mesa's software GL): overlapping rows with
nothing written past the buffer, gaps between rows keeping the core's bytes,
a nulled generated and hand-written function returning 0, and uploads and
readbacks refused when miniBox says no. Each was seen to fail with its part
removed. Engine tests (30), witness (92), interface tests, miniBox's tests
and the PCSX2 core's test script (44 passed, 6 skipped) pass, and the Windows
build compiles. PCSX2 with its OpenGL renderer on a GTX 1060 gives the same
picture at frame 2400 and the same record as the build before.

## The greenzone no longer stores the old timeline after an edit and a capture with no frame run (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18: an edit behind the
machine stops the greenzone (the state history) from storing what the machine
plays next, because that is the timeline the edit replaced, until a restore or
a load puts the machine back at or before the edit. A capture on the frame
just after the edit also ended it, on the belief that a capture always follows
a frame, and so a seek back. Three captures follow no frame: turning the
greenzone back on, resuming a capture period, and making a branch. With the
machine on frame N and an edit at N - 1, any of them stored the old timeline's
machine as frame N, and a later seek through it showed a machine the movie's
inputs do not give. The same mistake was made after a load, where the history
assumed a frame had run before the capture.

**What was decided.** The owner chose to fix every confirmed finding of the
analysis. Only a restore or a load ends the stop now; they always did, so the
capture's own ending is gone. After a load, the history knows whether a frame
ran before the capture (the frontend and the engine say so before every frame)
and judges where the machine stood when the edit came from that.

**Tested.** A new history test makes each of the three captures after an edit
one frame behind the machine, and a load followed by an edit with and without a
frame run. The randomized history test now makes such captures, and edits one
frame behind the machine a third of the time. With the old ending put back,
both tests fail (the random one with "a machine from another timeline"), and
so they do with the old guess after a load. Engine tests (30), witness (92),
interface tests and the Windows build pass.

## A project that leaves a setting out is checked with the package's default for it (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18: when the engine opens a
project, it checks the project's files against the slots the core declares,
and a slot may be shown only for some value of a setting (the Genesis Plus GX
core shows its Mega Drive cartridge slot only when the machine setting says
Genesis). That check read the project's own settings and nothing else. The
window records every setting it shows, so its projects passed; a project made
by an AI agent through the MCP server (whose settings are optional) or by hand
could leave the setting out, and was refused although the machine would have
started with the package's default.

**What was decided.** The owner chose to fix every confirmed finding of the
analysis. The engine's project opening checks the slots against the settings
the machine starts with: the package's defaults, with the project's settings
(or the caller's) on top. ce_project_validate itself is unchanged; the window
calls it with every setting recorded.

**Tested.** A new witness test gives the synthetic core a slot shown only at
its default setting: the project with no settings opens and plays to the
reference dumps, and the one that records another value is refused. With the
defaults left out of the check, the first is refused again. Engine tests (30),
witness (93), interface tests and the Windows build pass.

## A control a movie's LogKey does not name joins it (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18: a movie's first line,
the LogKey, names its columns, and since 2026-10-15 the engine reads and
writes lines by those names. A movie made with an older package can lack a
column for a control the machine has now. When such a control was pressed
while recording, the machine was given the press and the line left it out, so
the recording replayed differently; an AI agent's set_buttons on it was
answered as done and left it out too. chimera-run's recordings carried no
LogKey at all, so they would be misread once the package gained a control.

**What was decided.** The owner chose to extend the key: the control joins
the LogKey as a group of its own at the end, and every line gains that
group's empty column, in recordings and in the movie the MCP server holds
when there is no window. A movie's key that changes this way does not cost
its greenzone: lines are compared as the input they give the machine. The
window keeps a movie's columns in TAStudio, so there the agent's call is
refused and names the control; extending the window's movie is not done. A
recording, and a project an agent starts, now always name their columns:
chimera-run writes the movie with its LogKey. Lines an agent's edit writes
follow the held movie's own columns, not the machine's order.

**Tested.** A new witness test records a movie that starts with a two-control
LogKey: the key gains the control the movie presses and the recording replays
to the reference dumps. The agent script opens a project whose movie names two
controls, sets P1 A, and finds the key and every line grown, then undoes it.
An engine test checks the key text a layout is written by. Each failed with
its part removed. The window's refusal is not exercised by a test. Engine
tests (30), witness (94, among them the window opening a project the
windowless server saved), interface tests and the Windows build pass.

## Firmware other than a project's pins is said without a window (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18: a project pins the
firmware it was made with (its SHA-1), and the window checks the pins when
it opens the project and asks a person before going on with other files. The
engine's own project opening never compared them, so chimera-run --project
and the MCP server without a window ran a project on any firmware without a
word, and a project an AI agent started with open_game pinned nothing at all.
Either can desync a movie.

**What was decided.** The owner chose a warning, not a refusal. The engine's
project opening compares the firmware given with the pins and says, for each
pin, that none was given or that the file is another; chimera-run writes it
to stderr and the MCP server adds it to open_project's answer as
`warnings`. open_game pins the firmware it was given, as the window does.

**Tested.** A new witness test opens a project with a pin: with no firmware
and with another file it runs to the reference dumps and says so; with the
pinned file it says nothing. The agent script checks that open_game pins the
firmware it was given and that open_project on another file warns and on the
pinned one does not. Each failed with its part removed. Engine tests (30),
witness (95), interface tests and the Windows build pass.

## Somebody else's file cannot end Chimera (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18 found two ways a file
from outside ended the process instead of failing the call. A zstd-packed lump
in a movie or state archive (files people share) unpacked without a ceiling,
so 2 MB of file could ask for 64 GB; and no C++ exception was caught where the
frontend calls the engine, so a package that declared a picture width of -1
(the buffer it sized was 2^64 bytes), or running out of memory while reading a
file, aborted Chimera.

**What was decided.** The owner chose to fix every confirmed finding of the
analysis.
- A lump may be at most half the machine's memory, and at least 1 GiB; one
  that says or turns out larger is refused with the sizes. A PS3 machine's
  state, the largest there is, is 4.3 GiB.
- A package's picture and sound sizes are checked against ranges every core
  fits (the largest today is 6144 x 4608 and 65536 samples a frame) and a
  package outside them is refused with the number and the range.
- Opening a machine or a project, loading a state or a saved history, and
  reading a project, a movie, an archive or a package now catch what is
  thrown and fail the call with a sentence. A machine started part way is not
  given back.

**Tested.** A new engine test reads a stored and a zstd lump under a lowered
ceiling and both are refused with the ceiling named. A new witness test runs a
package declaring a width of -1: it is refused with the number and the range.
With the check removed, the new guard refuses it instead ("could not be
started"); with both removed, Chimera aborts, as the analysis found. Running out
of memory part way is not tested. Engine tests (30), witness (96),
interface tests and the Windows build pass.

## The MCP server: an edit far past the end is refused, and a core's printing stays out of the agent's stream (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18 found two ways the MCP
server without a window failed an AI agent. set_input and set_buttons took a
frame number up to two billion past the movie's end, and the frames between
were added one empty line at a time until the process was killed for want of
memory. And the core runs in chimera-mcp's own process, so a line it printed
on its standard output went into the agent's protocol stream, and the agent
could not read the answers after it.

**What was decided.** The owner chose to fix every confirmed finding of the
analysis. An edit may start at most 100,000 frames past the movie's end, the
most insert_frames adds at once, and the refusal says where the end is.
chimera-mcp keeps standard output for the protocol and sends everything else
printed there, the core's lines among them, to standard error.

**Tested.** The synthetic core now prints one line on its standard output when
it starts, and the agent script, which reads every answer as JSON, runs
through without a window as before; with the redirect removed it fails on the
core's line. The agent script asks for an edit two billion frames past the end
and is refused; with the bound removed it ran until the test's time limit.
The box runner's comparison with the native build leaves the core's line out.
Engine tests (30), witness (96), interface tests and the Windows build pass.

## A disc or zip image that fails leaves the one already there (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18: the media maker's three
writers (zip, ISO 9660 disc, FAT12 floppy) opened the output path itself and
truncated it at the start, so a run that failed or was cancelled deleted the
image that was already there; and none checked the final close, so an image
whose last bytes did not reach the disk (a full disk) was reported as made.

**What was decided.** The owner chose to fix every confirmed finding of the
analysis. The writers write through the engine's file writer: under a
temporary name beside the image, moved over it only when the file closed
without an error.

**Tested.** A new media test puts an image in place and, for each format,
cancels a run over it and, on Linux, runs it under a file size limit that
plays a full disk: the run fails and says so, the image is as it was, and no
temporary file is left; a run that works then replaces it. With the ISO
writer deleting the image on failure again, the test fails. Engine tests (30),
witness (96), interface tests and the Windows build pass.

## RAM search on big memories: undo within a budget, a detailed frame walks only what changed, a bus is read in runs (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18 measured three costs of
RAM search on large memories. Undo kept five whole copies of the search, made
before the oldest was dropped: in detailed mode a level is about six times the
domain, so the history could reach 36.8 times it, and on Linux running out is
the process killed rather than the fallback that was planned. Detailed mode
compared every candidate every frame, about 1.3 ms a MiB a frame, though most
memory does not change. And on a domain reached through a bus (no pointer, or
live ranges), a sparse search read one candidate per call, each a call through
the frontend into the core; a search that keeps the previous values read the
whole domain twice.

**What was decided.** The owner chose a byte budget for undo: 1 GiB for undo
and redo together, the oldest level dropped first and before the copy is
made, and a search larger than the whole budget not kept at all. A detailed
frame reads the domain once and walks only the 4 KiB blocks whose bytes
changed. Sparse candidates are read in runs (one read for candidates with
less than 4 KiB between them, up to 1 MiB), and a dense search on a bus reads
the domain once and takes the previous values from that read.

**Tested.** Three new engine tests: five searches keep two levels under a
200 KiB budget and none under 10 KiB; a detailed frame with no change walks no
candidate, and one with a change walks one block and counts it (also for a
candidate across two blocks); a sparse search of 100 close candidates on a bus
makes at most two reads, where it made 200. Each failed with its part removed.
The randomized test that checks every operation against a model, through a
pointer and a bus, every size, both modes and live ranges, passes. A detailed
frame of a 64 MiB domain in which a little changes took 89 ms and takes 23 ms.
Engine tests (30), witness (96), interface tests and the Windows build pass.

## miniBox: seven small gaps closed from the analysis of 2026-10-18 (user-decided, 2026-10-18)

**What was found.** The miniBox analysis of 2026-10-18 left a list the owner
asked to fix after the larger items. Seven of them are small:
- A page a background state still owed was made readable whatever the guest
  had asked for since. A page the guest made inaccessible meanwhile read
  without the fault it was waiting for (RPCS3's texture cache works that
  way), depending on how far the background copy had got.
- rbx, rbp and r12-r15 reached the guest holding the host's pointers, which
  a guest saves into memory a state carries.
- The x86 direction flag could come back from a guest set, and host code
  would then copy memory backwards.
- musl's stub for cancellable system calls passed the fourth argument where
  the host does not look (a pread lost its offset once a core linked
  pthread_cancel).
- rt_sigprocmask and ioctl answered "done" and wrote nothing, so a core read
  whatever was on its stack.
- mprotect, munmap and madvise refused a length that was not a whole number
  of pages, which Linux and miniBox's own mmap round up.
- The Vulkan bridge never gave out a destroyed object's handle slot again,
  so the table grew with every object until the next load.

**What was decided.** Each is fixed as described: a hold takes away writing
only; those registers read zero when a guest export is entered; the
direction flag is cleared when control comes back from the guest and when a
system call arrives; the stub passes the fourth argument in rcx (cores rebuilt
with this guest kit get it); the answers are written (an empty signal mask, a
zero window size, zero termios, nothing waiting); lengths are rounded up;
destroyed objects' slots are reused.

**Tested.** New miniBox tests for each, every one seen to fail with its part
removed: a held page made inaccessible reads as inaccessible; a guest export
finds those registers zero; the direction flag is clear after a guest returns
with it set; a cancellable pread keeps its offset; the answers are written;
unaligned lengths are accepted; 1000 semaphores made and destroyed use 36
handle slots, 1038 without the fix. miniBox tests, Chimera's engine tests (30),
witness (96), interface tests and the Windows build pass; the PCSX2 core's
test script passes 43 with 6 skipped and fails ports:columns, which read the
first line of a recording that chimera-run had begun writing with an
[Input] header (fixed in the next change).

## The engine's low findings, first round: the greenzone, loads, the session (user-decided, 2026-10-18)

**What was found.** The engine analysis of 2026-10-18 rated about fifty
findings low; the owner chose to fix them after the mediums, skipping what is
not worth it. This round takes the greenzone's, the loads' and the session's:
- the budget left out each stored frame's bookkeeping (6 to 18% over for a
  light core); a capture period of 600 or more made every other stored frame
  an anchor; the history file was overwritten in place, so a save cut short
  lost the last good one;
- running out of memory during a background save halved the budget to the
  floor; just after an anchor was planned, it left miniBox filling a freed
  buffer; in miniBox's write callbacks it was thrown through miniBox's C code;
  and the state-file write told miniBox a failed write had worked;
- a load the sandbox refused before touching memory was handled as a load
  (the greenzone forgot an edit, every GPU core rebuilt); two paths that
  replace the movie's input kept the old timeline's kept pictures; a project
  file changed on disk after it was found was mounted unchecked; recording on
  a dead machine cut the movie before noticing;
- a core dying in Init was reported as "could not load this file"; a core
  lacking one of four memory-domain exports opened and crashed on the first
  domain call; several index arguments reached the core unchecked; the
  in-memory save buffer grew by doubling and kept its peak.

**What was decided.** Each is fixed in the plain way the finding names (the
details are in docs/state-manager.md and the code's comments). Settling a
stretch already on disk now waits for a background save, as a spill always
did: counting the bookkeeping made a small budget spill, and so settle, often
enough to meet a save. Also: chimera-run's recordings are bare lines again
when their columns are the machine's own order, as five core test scripts
read the first line of one; the [Input] header and LogKey are written when
the columns are another order (38a88e0 wrote them always, which broke the
PCSX2 core's ports:columns test).

**Tested.** A history test for a period of 600 (one anchor over 6000 frames;
six with the old measure) and a witness test for a core dying in Init (the
reason is said; "could not load this file" with the fix removed). The other
fixes have no new test: they are on paths that need memory to run out, a
second process, or a file changed between two calls. Engine tests (30),
witness (97), interface tests, the Windows build and the PCSX2 core's test
script (44 passed, 6 skipped, ports:columns among them) pass.

## The engine's low findings, second round: the MCP server, RAM search, projects (user-decided, 2026-10-18)

**What was found.** More of the engine analysis's low findings:
- the MCP server: a line given to set_input could hold a newline or other
  control bytes, played as a pressed button and splitting the line when the
  movie was saved; insert_frames without a window inserted one line at a
  time; an open that failed lost the open project's unsaved input; export_movie
  wrote through a link out of the work folder; a watch past a bus's end read
  0; a reply could hold invalid UTF-8 (a long property text cut mid-character,
  or a game's bytes), which an agent's library refuses; every property read
  listed a dynamic table afresh; every core lookup read and hashed every
  package; the person's work was kept once per server start, not per project;
  on Linux a client arriving before the server noticed the last one leave got
  that one's answers, and a pipe that could not be opened again made the
  server's thread spin;
- RAM search: switching the byte order left a sparse set's values in the old
  order; a start that ran out of memory left the search half switched; a
  removal that ran out turned undo off for good; converting the size of a
  dense list sorted high-to-low reset its order; a property took a bus's
  domain over the domain of the same name;
- projects: a project with no id got a new random one at every open, which
  hid the work of a session that crashed before its first save; unknown keys
  inside files, markers, branches and the core were dropped on save; the
  property table's pointer was documented as lasting the session.

**What was decided.** Each is fixed as its finding names: lines in printable
ASCII; one bulk insert (ce_movie_log_insert_n); the open project set aside
and given back on failure; links refused; bus addresses bounded; replies made
valid UTF-8 (each bad byte becomes U+FFFD) and property texts read whole; no
listing before a read; cores found by file name first and hashed by file
identity; work kept again per machine; a greeting starts a connection and the
client waits for Chimera's own; the pipe made again or waited on. RAM search
turns sparse values round, restores its switches and the undo flag, and keeps
the order. A project with no id gets one made from its path until a save
writes it in; unknown nested keys are refused like top-level ones (every key
any build has written is known); the header says how long the table lasts.

**Tested.** New engine tests: UTF-8 replies, the byte-order switch on a
sparse set, an id-less project's id; new agent checks: a line with a newline,
an export through a link, a failed open, a watch past a bus's end. Each failed
with its part removed. The pipe changes, the bulk insert's speed, the core
lookup and the per-machine keep have no new test. Engine tests (30), witness
(97), interface tests and the Windows build pass.

## The engine's low findings, third round: images, chimera-run, the GPU bridge (user-decided, 2026-10-18)

**What was found.** More low findings of the engine analysis:
- images: a short name given to resolve a clash could be a later file's own
  name, so a floppy or an ISO's primary tree held two entries of one name; the
  floppy's fit check summed in 32 bits, and a file of terabytes wrapped round,
  passed and was read past the image buffer; the collector left out a file it
  could not look at without a word, and in the Windows build packed nothing
  from a network share;
- chimera-run: --final-screenshot wrote the buffer as it stood (blank in a
  plain run, the first pass's picture after a seek); a number that was not one
  read as 0; --edit-from, --stop-at-seek and --play did nothing without the
  option they belong to; the bus dumps called the core once a byte through a
  32-bit address; a domain dump wrote zeros where the core handed nothing over;
- the GPU bridge: fence names repeated across processes, so a name in a state
  from another could be a live fence; the buffer pool's flags grew to any name
  a core passed (512 MiB for one); three more readbacks into the core's memory
  were not guarded; compile-cache stores shared one temporary name across
  processes and were not flushed before the rename.

**What was decided.** Each is fixed as its finding names: every natural name
is reserved before counters are given; the fit is summed in 64 bits; a file
that cannot be looked at stops the image, named, and names are worked out from
the path alone; the last frame is drawn for the final screenshot (run again,
drawn, after a seek); numbers are read whole; the lone options are refused;
buses are read in runs; a short domain dump is said (stderr and the meta file,
and still written, since core test scripts compare such files today); fence
generations start at this process's context id; only the pool's own names are
flagged; glGetNamedBufferSubData, glReadnPixels and glGetTextureSubImage go
through the guard; a cache store has a name of its own and is on the disk
before it is renamed.

Not done here, and why: files a file manager leaves in a folder (Thumbs.db,
.DS_Store) still change an image's hash - skipping them changes images, so it
is the owner's to decide; Windows still gives cores a compatibility GL profile
and Linux a core one - changing that changes every GL core on Windows, also
the owner's; a domain read through a raw pointer can still run a core's fault
handler (RPCS3), which needs a read from miniBox that never enters the guest;
the journal's sync on Windows wants a measurement first; the session's extra
copies at open, a timeout for a guest call and two opens at once are larger
changes, left for their own round.

**Tested.** New tests: image names (a floppy and an ISO folder where a later
file's own name is the counter an earlier one would get), the final
screenshot (the last frame's picture in a plain run and after a seek, in two
runs) with a bad number and a lone --play, the pool's flags, and the three
readbacks refused like the others when miniBox says no. Each failed with its
part removed. The fit check, the collector, the bus dumps, the fence names and
the cache store have no new test. The engine's 30 unit tests pass, the
witness 98 passed and 0 failed, the interface tests 1107 with none failed
(23 skipped, as before), the Windows build compiles, and the PCSX2 core's
test script 44 passed, 0 failed, 6 skipped (the skips need discs that are not
here).

## Tools read a core's memory without running the core's code; the machine hash stays as it is (2026-10-18)

**What was found.** The memory tools - RAM Search, the hex editor, RAM Watch,
game properties, the MCP server's reads and searches, chimera-run's dumps -
read a domain through the core's own pointer, from a thread that is not the
core's, between frames. A core can make pages of its memory no-access to learn
when they are touched (RPCS3's texture cache does, for pictures the GPU drew),
and miniBox then runs the core's fault handler for the read. RPCS3's handler
writes the picture back into memory, so a movie could play differently with
RAM Search open. Measured with a miniBox test before the fix: a read from
outside the guest ran the guest's handler once.

**What was decided.** miniBox gives the engine a read view
(`wbx_guest_read_view`): a pointer to the same bytes through its second view
of the core's memory, which never faults and so never runs the core's code. A
page the core gave back reads as zeros there, as it does in the core's own
view. On Windows, a block over 4 GiB backs pages only when the core first maps
them; a page with nothing behind it gets memory in that second view alone and
reads zero. The engine reads every domain through it
(`ce_session_domain_read`, the new `ce_session_domain_read_ptr`, game
properties, the MCP server's search), and the interface reads its pointer
domains through it too (RAM Search's reads). Writes still go through the core's own pointer, where the state
history sees them, and a core that watches its memory hears of them as it
should.

The machine hash (what a state is checked against before it loads) names each
page that held data at the seal by a tag, not by its bytes, so two boots that
leave different bytes in the same pages are taken for the same machine. The
analysis proposed hashing the bytes. Measured with six cores, each booted
twice in two processes: on Linux the bytes were the same every time (up to
195 MB of such pages, up to 112 ms to hash them), but on Windows Ruffle's two
boots differed in 33 pages. The differing words are Windows host addresses:
Windows handles a fault on the faulting thread's own stack, which is the
core's, and the core later copies what was left there into its heap (the same
cause found on 2026-09-16). Hashing the bytes would refuse those states in
every new process on Windows, as issue #80 did. So the hash stays as it is,
the measurement is in miniBox's MACHINE-SPEC, and miniBox's seal dump
(`MB_SEAL_DUMP`) now writes those pages' bytes too, so two seals can be
compared byte for byte. Whether to do more - clear what Windows leaves on a
core's stack, or hash the bytes on Linux only - is the owner's to decide.

**Tested.** New tests, each seen to fail with the fix taken out: miniBox's
threads test reads a page its guest made no-access through the read view
(the bytes are there, the guest's handler does not run) and, as the control,
directly (it runs once); miniBox's block tests read a no-access page through
the view, and on Windows a lazy block's unbacked pages, and a refused commit
gives no view. The witness test E:tools:core-not-run gives the synthetic core
a no-access page and a fault handler that counts: RAM Search's measurement and
a dump read the page whole, and the count stays 0 (1 with the engine reading
through the core's pointer). An interface test holds a pointer domain to
reading through its read view and writing through the core's pointer. The
game properties' and the MCP server's reads have no test of their own; they
use the same read as the dump. miniBox's 22 tests pass (and its 6 expected
failures fail, as before), its block and threads tests pass on Windows too;
the engine's 30 unit tests pass, the witness 99 passed and 0 failed, the
interface tests 1108 with none failed (23 skipped, as before), the Windows
build compiles, and the PCSX2 core's test script 44 passed, 0 failed, 6
skipped.

## Opening a session keeps no copies of its files, and two threads may open at once (2026-10-18)

**What was found.** Opening a session copied the core out of its package, all
of the core's own files at once, the firmware and the game's bytes out of the
caller's buffers, and only then handed each to miniBox, which makes its own
copy. Reading a package entry held it twice for a moment as well. And two
switches that steer an open - "ask the core a question instead of starting
it" and "this is a precompile session" - were shared by the whole process: a
question asked on one thread while another thread opened a session turned
that session into a question with no frame export, and its first frame would
have called a null pointer. Nothing does that today (the interface asks on
the same thread, the wizard in a child process).

**What was decided.** miniBox reads each file whole during the call that
mounts it and never again, so the open hands it the bytes where they already
are: the package's entry, the caller's buffer. The core's own files are
mounted one at a time straight after the core, while the package is open,
which puts them before the game; a core cannot see the order files were
mounted in. A package entry is extracted straight into its buffer. The two
switches are per thread, and engine.h says which calls are the whole
process's (the GPU request and the compile cache folder).

Measured with chimera-run: the synthetic core opened with a 206 MB file as
firmware peaked at 829 MB before and 628 MB after, one copy of the file. A
PS3 session with its 206 MB firmware peaked at 1.69 GB before and after, and
a PS2 session at 1.16 GB: their peak comes later, while the machine runs, so
for them the open is smaller but the peak is not.

**Tested.** No new test: the copies left show only as memory, measured above,
and the race needs two opens interleaved. The engine's 30 unit tests pass,
the witness 99 passed and 0 failed, the interface tests 1108 with none failed
(23 skipped, as before), the Windows build compiles, and the PCSX2 core's
test script 44 passed, 0 failed, 6 skipped.

## A state's pages are found through the dirty map (2026-10-18)

**What was found.** Taking an anchor (a whole state the history keeps) walked
miniBox's page records - 40 bytes a page - four times: once to size the
state, once more because the plan asked the size again, once to count the
pages the plan holds and once to hold them. The PS3 core's block is 5.2
million pages.

**What was decided.** The pages a state carries are found through the dirty
map, a byte a page read eight at a time, and a page record is read only
where the map says dirty (invisible pages are dirty too, so it still asks).
The plan is given the room it may use and says how much it took, instead of
being asked its size first. Measured on a PS3-sized block (20 GiB, 128 MiB
written since the seal), the best of five: sizing the state 23 ms before and
3 ms after, planning it 51 ms before and 8 ms after, so an anchor's walking
went from about 97 ms to 11 ms.

Not done: on Windows each frame compares every stack page with its copy
twice, once at the end of the frame and once at the start of the next. The
second could be skipped when nothing ran in between, saving one compare of
the stack pages a frame, but a copy that is wrongly called current is how
issue #64's mixed restores happened; it stays.

**Tested.** A new miniBox test plans a state with a byte less room than it
needs (refused, nothing written or held) and with exactly the room (the
bytes save writes), with invisible pages written; it fails with the room
check taken out, and with invisible pages counted. miniBox's 22 tests pass (6
expected failures, as before) and the state tests pass on Windows; the
engine's 30 unit tests pass, the witness 99 passed and 0 failed, the
interface tests 1108 with none failed (23 skipped), the Windows build
compiles, and the PCSX2 core's test script 44 passed, 0 failed, 6 skipped.
## A core declares whether its greenzone reloads; no core is named (user-decided, 2026-10-18)

**What was decided.** The interface kept a GPU-drawn project's greenzone and
branch states across sessions only for seven cores named in its code, which
the rule that no core
is known by name forbids. The owner chose a new package declaration over
trusting `gpuStatesSurviveTheContext` (which Ruffle declared while bricking a
project) or keeping the list: `video.greenzoneSurvivesReload`, declared by a
core only on evidence measured in its own repository. Each save writes what the
running core declares into the project (header `GreenzoneSurvivesReload`), and
the project reads it when it is opened, before its core is booted. The
evidence that put the seven on the list moved into their packages, beside the
declaration:

| Core | Game, 1000 frames | Frame after the reload |
| --- | --- | --- |
| Ruffle | the 8916-frame nss102 project, 626 MB greenzone | frame 8915 identical |
| Dolphin | Pro Rally 2002, 192 MB greenzone | 0 of 286,720 pixels differ |
| xemu | Prince of Persia, 241 MB greenzone ending at 300 | 0 of 307,200 differ |
| PCSX2 | Gran Turismo 4, 158.7 MB | 0 of 286,720 differ |
| flycast | Re-Volt, 21.7 MB | 0 of 307,200 differ |
| PPSSPP | Ridge Racer | 0 of 130,560 differ |
| RPCS3 | a PopCap title, 100 MB | 0 of 921,600 differ |

All measured 2026-09-16, with a greenzone saved by one process and loaded by
another, and a control frame that really differs (the code comment that held
the details is in this file's git history before this change).

**What it costs.** A project saved before this change has no header, so the
first time it is opened its greenzone starts empty; its next save writes the
header and later opens keep it. A core that has not declared it (Xenia,
DuckStation, Vita3K, Azahar and the rest) keeps clearing as before.

**Also decided.** `MovieActionPaint`, an undo step nothing used, is deleted: had
it been used, undoing a painted axis would have restored 0 or 1 instead of the
axis value.

**Tested.** A new test: a GPU-drawn project of a core named Ruffle that does not
declare it keeps nothing, and one of a core of any name that does is recorded
and keeps its greenzone; it failed when every GPU core was let keep its
greenzone. The format tests that used the name Ruffle now use the declaration
(34 pass), and the package parser reads the new key.

## RAM Search compares "the same as before" eight bytes at a time (2026-10-18)

**What was found.** The commonest RAM Search step - keep what is equal (or
not equal) to its previous value, over a whole domain - called the general
comparison once for every candidate: read it, read its previous value, and
go through every kind of search to find this one.

**What was decided.** For whole-number values, each candidate on bytes of
its own, the memory is compared with the previous values eight bytes at a
time, and a candidate is looked at alone only where those eight bytes
differ. A whole number is the same exactly when its bytes are, whatever the
byte order or sign; a float is equal within a margin and keeps the general
path, as do candidates that overlap. Measured with chimera-run's RAM Search
measurement on the PS2's 32 MB main memory, best of three: a byte search
91 ms before and 26 ms after, 2-byte 57 ms and 24 ms, 4-byte 39 ms and 23 ms
(the rest is taking the new previous values).

**Tested.** The RAM Search model test (a random script of searches checked
against a simple model of every candidate) covers it: with the eight-byte
step's lane arithmetic broken it fails at the first such search. The
engine's 30 unit tests pass, the witness 99 passed and 0 failed, the
interface tests 1108 with none failed (23 skipped), and the Windows build
compiles.

## Guest kit revision 2: Linux's file answers, and virtual time that cannot run backwards or stand still (2026-10-18)

**What was found.** miniBox's file calls answered a descriptor that is not
open with ENOENT or EINVAL where Linux says EBADF, seeking a stream with
EINVAL where Linux says ESPIPE, and refused a seek past a file's end, which
Linux allows (a read there is end of file). In virtual time (machine spec
v3, used by FreeJ2ME), a sleep or timeout too large for the clock wrapped
round and ran the clock backwards, a thread spinning on a wait that had
already timed out never let another thread run, and a thread that only
called sched_yield, alone with others waiting on timeouts, spun with the
clock standing still.

**What was decided.** All of it changes what a core sees, so it goes only to
cores built with the new guest kit, revision 2 (the kit's emulibc declares
the revision; a core built before keeps the answers its movies were made
with, as revision 1 did for sysinfo). From revision 2: EBADF, ESPIPE and
seeks past the end as on Linux (a write there fills the gap with zeros, and
a position past the end survives a state load); huge times saturate at the
clock's largest value; an expired wait gives way as a clock read does; and a
lone sched_yield while others wait with timeouts costs one tick. miniBox's
MACHINE-SPEC lists revision 2.

**Tested.** miniBox's conformance guests, built once with the new kit and
once with none, check every answer both ways: the file answers in the v2
guest, and in the v3 guest a lone yield, an expired wait and a huge sleep.
With the revision ignored (old answers for all) the new-kit runs fail; with
revision 2's answers given to all, the no-kit runs fail. They pass on Linux
and on Windows. miniBox's 23 tests pass (6 expected failures, as before);
the synthetic core was rebuilt with the new kit, and the engine's 30 unit
tests pass, the witness 99 passed and 0 failed, the interface tests 1114
with none failed (23 skipped), the Windows build compiles, and the PCSX2
core's test script 44 passed, 0 failed, 6 skipped.

## The Vulkan bridge refuses an unknown handle where the driver follows it (2026-10-18)

**What was found.** Some handle fields in Vulkan structures count only
sometimes - a descriptor's image view, a pipeline's layout, the memory bound
to an image - and when they do not, a program may leave anything in them. The
bridge passed a handle it did not know in such a field to the driver as "no
object" and let the call go ahead, also where the field did count. A handle
from before a load, or one never made, then reached the driver as a null
object, and the driver followed it.

**What was decided.** Where the driver follows such a field, an unknown
handle refuses the call, as it does everywhere else: a pipeline's layout
(unless the pipeline is a library), the memory bound to an image (the bridge
offers no swapchain, the one case it is ignored), and a descriptor's image
view in a write of any type but a sampler. Fields the driver may ignore keep
the soft reading. The rules sit in a table in the generator, beside the
other rules about such fields.

**Tested.** The bridge's test makes three wrong calls through the bridge (a
pipeline with a layout, and a descriptor write with an image view, that the
bridge never made, beside the two wrong calls it made before) and expects
exactly those refused. With the rule table empty the test process dies in
the driver (lavapipe), which is the bug; with it, it passes. On Windows
against the GTX 1060's driver the calls were refused and nothing died, but
the test as a whole fails there at an older check (step 116: the driver
answers both names of all seven paired commands, and the bridge answers
the two names of the first pair, vkCmdBeginRendering, differently); that failure is the same with
these files taken back out and with the miniBox of the 16 October build,
so it predates this change, and it is left for its own look. miniBox's 23
tests pass (6 expected failures); the engine's 30 unit tests pass, the
witness 99 passed and 0 failed, the interface tests 1114 with none failed
(23 skipped), the Windows build compiles, and the PCSX2 core's test script
44 passed, 0 failed, 6 skipped, its five Vulkan tests among them.

## The Vulkan bridge answers every name of a command with one function (2026-10-18)

**What was found.** The bridge's own test failed on Windows against the GTX
1060 at step 116, before this day's changes too: vkCmdBeginRendering and
vkCmdBeginRenderingKHR are both in the bridge's list of commands, each with
its own number, and the guest gave a different function for each name.
NVIDIA's driver answers both names; lavapipe, which the Linux test uses,
answers neither in that test, so Linux never showed it. Any core worked
either way, since both functions reach the same command.

**What was decided.** Every name of a command answers with the function of
its first entry in the list, as a driver answers. Only the guest half
changes (the host half is generated byte for byte the same), so a core gets
it when it is rebuilt.

**Tested.** The bridge's test passes on Windows against the GTX 1060 (all
seven paired commands present through the bridge as through the driver, and
the checks after step 116, which never ran there before, pass) and on
Linux. miniBox's 23 tests pass (6 expected failures); the engine builds for
Linux and Windows and its 30 unit tests pass. The witness, interface tests
and PCSX2 were not rerun: the engine's code is the same.

## GPU timing in the Vulkan bridge: a switch, measured, off (2026-10-18)

**What was found.** A Vulkan core can ask how far the GPU has got (a fence,
an event, a semaphore, query results, a wait with a short timeout), and the
answer depends on the GPU's speed, so a core could take a different path
from one run to the next. The owner asked for the cost to be measured
before deciding (miniBox decision 6).

**What was decided.** The bridge has a switch, CHIMERA_VK_SETTLED=1, that
waits for the whole device before each such question and turns a timed
wait into a look; it is off. Measured on the GTX 1060 through chimera-run,
Vulkan renderer, switch off and on, twice each: PCSX2 with a game disc, 900
frames, made 896 such waits and spent 0.13 s more in the bridge's host half
(4.0 s either way overall); RPCS3 with its test program, 600 frames, made
1198 and took 58.0 s instead of 57.5 s; Dolphin with its test program asked
nothing. Two runs of PCSX2 and of RPCS3 ended with the same memory with the
switch off as with it on, so on these programs GPU timing did not reach the
machine. Whether to turn it on for everyone is the owner's to decide;
timestamps in query results stay the GPU's own times either way.

**Tested.** The bridge's own test passes with the switch on; the switch is
off, so nothing else changes. miniBox's 23 tests pass (6 expected failures);
the engine's 30 unit tests pass, the witness 99 passed and 0 failed, the
interface tests 1114 with none failed (23 skipped), the Windows build
compiles, and the PCSX2 core's test script 44 passed, 0 failed, 6 skipped.

## Windows gives back what a big core frees: lazy blocks made of pieces (user-decided, 2026-10-18)

**What was found.** On Windows a core's memory is a section with two views
(the core's, and a mirror the host reads through). A page of a view of a
section cannot be given back on its own: VirtualFree refuses to decommit it
(error 87, measured), and resetting it keeps its bytes. So in a block over
4 GiB, which is backed only as the core uses it, everything the core had
ever used stayed taken until the core was closed, however much it freed.
The owner chose a redesign of big blocks over leaving it.

**What was decided.** A block over 4 GiB is made of 16 MiB pieces, each its
own section, and both views are built of those sections on reserved address
space (Windows' placeholders, Windows 10 1803 and later). When every page of
a piece is free, the piece is unmapped from both views and its section
closed, which gives its memory back; when the core maps there again, the
piece is made again, empty. A piece is kept when a page in it is compared
every frame (a hot page), is invisible, or when a memory tool's read view
points into it. A page with nothing behind it is read as zeros by states,
anchors and deltas, which used to commit it first, and would have made the
piece again a frame later. Windows changes protection, and commits memory, one view at a time, so
both are split at the pieces' edges (the first try committed across them,
and the PS3 core's first allocation was refused: error 487). Without placeholders, or
with MB_NO_PIECES set, the block is one section as before. Linux is
unchanged: it already punches holes in its memory file.

**Tested.** A new miniBox test on Windows maps four pieces, writes them,
seals, and frees the second whole: both views of it are no longer committed
and its pages are uncommitted, its neighbours keep their bytes, a saved state
reads it as zeros and leaves it given back, half a freed piece is only
zeroed, a piece under a read view is kept, and the second piece mapped again
is backed, empty, and writable; and a mapping across four pieces is backed at
once. It fails with pieces never given back (the old behaviour), with a
saved state committing a page before reading it, and with commits not split
at the pieces' edges. On Windows, RPCS3 with its test program and firmware,
600 frames rewound five times to frame 100 on the GTX 1060, gave 86 pieces
(1.4 GB) back to Windows, had no commit refused, and ended with the same main
memory as with the block in one section (MB_NO_PIECES=1). miniBox's 23
tests pass (6 expected failures) and its unit and guest tests pass on
Windows; the engine's 30 unit tests pass, the witness 99 passed and 0
failed, the interface tests 1114 with none failed (23 skipped), the Windows
build compiles, and the PCSX2 core's test script 44 passed, 0 failed, 6
skipped.

## Two small GPU bridge fixes: the GL profiler sees the buffer pool, and a GL string is not cut at 4 KiB (2026-10-18)

**What was found.** The buffer pool (recycled GL buffer names) answered
glGenBuffers and glDeleteBuffers before CHIMERA_GL_PROFILE and
CHIMERA_GL_TIME measured anything, so the pool's own driver calls were
missing from the profile and from the driver's share of a frame. And the
guest's wrapper for glGetString and glGetStringi kept the answer in 4096
bytes: a desktop driver's extension list is longer (NVIDIA's runs past 10
KB), and the extensions after the cut were simply not there - Flycast, for
one, looks for its anisotropic filtering extension in that list.

**What was decided.** The pool's two calls are timed like every other call
when either switch is on. The wrapper's buffer is 64 KiB; the host already
writes no more than the guest gives it room for. A core gets the longer list
when it is rebuilt with this miniBox. Which GL profile Windows and Linux
hand a core (compatibility and core) is a separate question, the owner's.

**Tested.** With CHIMERA_GL_PROFILE on Dolphin's test program, the previous
code's profile has no row for glGenBuffers (74 rows) and the new one has it
(75 rows, 9 calls). A new miniBox test keeps a 12 KB extension list whole in
the string keeper behind the wrapper; the wrapper's own buffer has no test,
and the generated wrapper was checked to carry 64 KiB for both calls.
miniBox's 23 tests pass (6 expected failures); the engine's 30 unit tests
pass, the witness 99 passed and 0 failed, the interface tests 1114 with none
failed (23 skipped), the Windows build compiles, and the PCSX2 core's test
script 44 passed, 0 failed, 6 skipped.

## The input journal's flush on Windows: measured, left as it is (2026-10-18)

**What was found.** The analysis of 2026-10-18 reasoned that on Windows the
journal's once-a-second flush to the disk (_commit, which takes the C
runtime's lock on the file) could hold up the thread writing inputs behind a
slow disk, during a greenzone save for example, and asked for a measurement
before any change.

**What was measured.** A small program on this PC's Windows (through the
engine's C API) kept a journal open and added an input line about 64 times a
second for 10 seconds, timing each add, once on a quiet disk and twice while
another thread wrote 8 MiB blocks to the same disk with a flush every 64 MiB.
About 640 adds a run: the median add took 34 to 37 us; the worst took 2.2 ms
on the quiet disk and 1.1 and 3.6 ms under the writing; none took over 5 ms.

**What was decided.** Nothing changes: the flush does not hold the writer up
by anything a person could see. A slower disk than this PC's was not tried.

## Vulkan answers about the GPU's progress are settled for everyone (user-decided, 2026-10-19)

The switch measured on 2026-10-18 (under 1% on the GTX 1060, no difference
seen in two runs) is now on by default: a question about how far the GPU has
got waits for the device first, so a core's path cannot depend on the GPU's
speed. The owner chose it over leaving it as an option. CHIMERA_VK_SETTLED=0
turns it off. Recorded GPU movies were already marked as not reproducible;
one that went down a timing-dependent path may now go down the other.

**Tested.** miniBox's 23 tests pass (6 expected failures; the bridge's test
with the answers settled); the engine's 30 unit tests pass, the witness 99
passed and 0 failed, the interface tests 1114 with none failed (23 skipped),
the Windows build compiles, and the PCSX2 core's test script 44 passed, 0
failed, 6 skipped, its five Vulkan tests among them (its later tests ran
with an engine that also had the next change, the watchdog, built in).

## A core that does not come back is stopped: 20 seconds a frame, 2 minutes to start (user-decided, 2026-10-19)

**What was wrong.** A core that looped forever inside a frame, or while
starting, froze Chimera: nothing could end the call from outside (engine
analysis, 2026-10-18). The owner chose the limits: 20 seconds for a frame and
2 minutes for starting.

**What changed.**

- miniBox can stop a guest call from another thread (sandbox spec, "Stopping
  a guest that does not come back"). A guest running its own code is stopped
  at once: on Linux by a signal, on Windows by suspending the thread and
  sending it out of the call. A guest inside a system call is stopped at its
  next one. A guest inside host code that never returns (a driver call that
  waits for ever) is not stopped. The call ends as a crash ends it: the
  machine is dead and the session reports the reason.
- Each session has a watchdog thread, armed around a frame and around
  starting. Past the limit it asks miniBox for the stop every quarter second
  until the call comes back. The user sees "the core stopped: the core did
  not finish a frame in 20 seconds", or "the core stopped while starting: the
  core did not finish starting in 120 seconds".
- A core that is working is not stopped. miniBox counts the guest's system
  calls, leaving out the ones a waiting or polling loop makes (yield, sleep,
  clock reads, futex, gettid, getpid), and the watchdog starts the time again
  whenever the count moves. A use of the compile cache counts too. What is
  stopped is a core spinning in its own code, or one that only waits, yields
  or reads the clock.
- `CHIMERA_FRAME_TIMEOUT_MS` and `CHIMERA_START_TIMEOUT_MS` set other limits in
  milliseconds; 0 turns a limit off. A session that only compiles a game's
  code ahead of time has no frame limit: its frames are long by design.
- Not timed: the other calls into a core (a state save or load, the optional
  exports). They are short in every core measured.

**Why the count of system calls.** A first version timed the frame alone.
RPCS3 running Bejeweled 3 with the LLVM code compiler (`ppu_decoder` llvm)
and no compile cache spends 98 seconds in its first two frames compiling the
game's code. It was stopped at 20 seconds. With the count it is not: 600
frames in 116 seconds, status OK.

**Tested.**

- miniBox's new test `conformance-stop`: a guest spinning in its own code and
  one spinning on `sched_yield` are each stopped from a second thread (on
  Linux the second with the stop's signal blocked, so the check at a system
  call is what stops it), a stop asked for and cancelled ends nothing, and
  the count moves by 1000 for 1000 `getppid` calls and not at all for 1000
  yields. It passes on Linux and on Windows. Broken on purpose (the check at
  a system call off, `getppid` counted as waiting) it failed: the count check
  failed and the second spin was never stopped.
- The witness has a new test, "core-stuck", with both limits set to 1.5
  seconds: the synthetic core's ROM "SPIN" never finishes starting and one
  ending "SPINFRAME" never finishes frame 3. Both are stopped with the right
  message, and the run ends instead of hanging. The Windows build of the
  command-line runner stops both the same way. With the watchdog turned off
  on purpose the witness failed it: "a stuck core was not stopped: the run
  was killed after 60 s".
- The rest: miniBox's 24 tests pass (6 expected failures); the engine's 30
  unit tests pass; the witness 100 passed and 0 failed; the interface tests
  1114 with none failed (23 skipped); the Windows build compiles; the PCSX2
  core's test script 44 passed, 0 failed, 6 skipped.

**Still to do.** A single GPU driver call longer than the limit (a very long
shader compile) is not stopped while it runs, but the core is stopped as soon
as it returns. The calls through the GPU bridges do not count as work yet.
## Issues 311, 312, 313 and 314: a clipped choice, overlapping counters, a save name and dead gamepad bindings (2026-10-19)

**What was found and changed.**
- #311: on Linux the Greenzone box's "Off" choice was below the box: the
  system font placed the buttons lower than designed, and the box kept its
  designed height. The box now grows to hold its last choice.
- #312: with the speed, the average speed and the frame counter all shown,
  the average was drawn one line under the speed, which is the frame
  counter's line. The average now follows the speed on its own line.
- #313 (the ares core): the save data slot took `.fla` but not `.flash`,
  though the core's own Export Save Data names a Flash save `save.flash`, as
  ares does. It takes `.flash` and `.eeprom` now.
- #314: the ares package bound the Nintendo 64 stick to `X1 LStickX`, which
  Chimera never sends; a controller's stick arrives as `X1 LeftThumbX Axis`.
  A check over every core found the same mistake in RPCS3 (`X1 LStickX`,
  `J1 RotationZ Axis`) and stick clicks bound to `X1 LeftStick` in Dolphin,
  DuckStation and PCSX2 (the name is `X1 LeftThumb`). Each core's bindings
  are fixed in its own repository, and a new contract test,
  `KeybindsNameGamepadInputsChimeraSends`, refuses a gamepad name SDL2 does
  not send; the key-name checks had looked only at the keyboard.

**Tested.** Three new tests, each seen to fail first: the Greenzone box at
fonts from 8.25 to 14 points ("Off" ended at 83 pixels in an 80-pixel box); the speed, its average and the frame counter drawn without
overlap (they overlapped at y=14 before); the contract test, which found
69 dead names in the published Dolphin, PCSX2 and RPCS3 packages and none
once their bindings were fixed. Engine tests (30), witness (99) and the
interface tests (119, 63, 594 and 341, none failed) pass.

## Calls through the GPU bridges count as a core's work (2026-10-19)

**What was wrong.** The time limit on a frame (two entries above, "A core
that does not come back is stopped") counted only the core's own system
calls as work. A call through a GPU bridge is not a system call: Chimera runs
it for the core. A driver call longer than 20
seconds (a very long shader compile) could not be stopped while it ran, but
the stop asked for meanwhile ended the core as soon as the call came back.

**What changed.**

- Chimera counts its own work for the core: one more after each call through
  the GL or Vulkan bridge, and each call to the compile cache, once it has
  been answered. miniBox adds that count to the core's
  (`wbx_count_work_from`, sandbox spec), so the watchdog sees it.
- miniBox carries out a stop only if the core has not worked since it was
  asked for. A driver call that took long and came back drops the stop, and
  the watchdog asks again if the core gets stuck after all.
- A stop asked on the core's own thread, from inside a host callback, waits
  for the core's next system call. On Windows it used to suspend the thread
  that asked, which would never have come back.
- The message shows a limit under a second, or with a fraction, with
  decimals ("1.5 seconds", not "1 seconds").
- `chimera-run --cache-dir <folder>` keeps a compile cache, as the frontend
  does, so a run from the command line can use one.

The cost on the GL path is one load and one store per call, on a counter the
core's thread alone writes.

**Tested.**

- miniBox's `conformance-stop` has two more checks, on Linux and on Windows:
  a stop asked from inside a host callback ends the guest at its next yield,
  and the same stop with the frontend's work counted after it is dropped and
  the call ends by itself. With the drop turned off on purpose the second
  check failed.
- The witness has a new test, "core-working": the synthetic core's ROM
  ending "CACHEWORK" spends its third frame only asking the compile cache
  for an object it never stored and yielding, about half a second here, and
  it is not stopped under a 0.1 second limit. With the engine's count not
  handed to miniBox on purpose, the witness failed it: "the core did not
  finish a frame in 0.1 seconds". The Windows build of chimera-run runs the
  same frame (6.6 seconds there) without a stop, and still stops the stuck
  one.
- The count in the GL dispatcher is a thin wrapper around it, not a
  destructor: a sysv function may carry no C++ unwind data under mingw, and
  the first version did not build for Windows.
- The rest: miniBox's 24 tests pass (6 expected failures); the engine's 30
  unit tests pass; the witness 101 passed and 0 failed; the interface tests
  1114 with none failed (23 skipped); the Windows build compiles; the PCSX2
  core's test script, as committed, 44 passed, 0 failed, 6 skipped.

## The same OpenGL on Windows as on Linux: the core profile, the newest version (user-decided, 2026-10-19)

**What was different.** Linux's GPU bridge context is EGL's, which is the
core profile: the old fixed-function OpenGL is not there. Windows made its
context with `wglCreateContext`, which is the compatibility profile: the old
calls worked. A renderer could call something old and work on Windows only,
and a test on Linux would not show it. The owner chose the core profile on
both.

**What changed.**

- Windows makes a context of the old kind first (WGL hands out the call that
  makes any other kind only while one is current), then asks
  `wglCreateContextAttribsARB` for a core-profile context and uses it. A
  driver without that call keeps the old kind.
- Both systems ask for the newest version first, 4.6 down to 3.3. Asked for
  3.3, NVIDIA's driver on Windows made exactly a 3.3 context, where its
  compatibility context had been 4.6, and a renderer that looks for 4.x
  would have taken an older path. Mesa answers with its newest anyway.
- 3.3 is required on both: a Windows driver of the old kind that offers 3.0
  to 3.2 is refused, as Linux's request already was.
- The line on standard error, and the description a core can ask for, say
  which profile was made: "4.6.0 NVIDIA 581.42 on NVIDIA GeForce GTX 1060
  6GB/PCIe/SSE2, core profile".

**Measured on the GTX 1060.** Each of the eight cores that draw through the
GL bridge ran the same blank movie with the old engine (compatibility 4.6)
and the new one (core 4.6), and the pictures were compared byte for byte:
Ruffle (Super Doggy, frames 600 and 1400), Azahar (Cars 2, 1200 and 2390),
Flycast (Re-Volt, 3600 and 4190), PCSX2 (Street Fighter EX3, 2400 and 3590),
DuckStation (NHL Blades of Steel 2000, 1200 and 2390), Dolphin (Pro Rally
2002, 3600 and 7490), RPCS3 (Bejeweled 3, 600 and 890) and xemu (Soulcalibur
II, 1500 and 2390). All sixteen pairs are identical, every one a real picture,
and the run times are the same within a few seconds. With every GL call
checked (`CHIMERA_GL_CHECK=1`, 600 to 1200 frames each) the old context
raised no error in any core; the new one raised one, in Flycast:
`glGetString(GL_EXTENSIONS)`, which the core profile does not answer. Linux
has always answered it that way, and Flycast draws the same.

**Tested.** The engine's 30 unit tests pass; the witness 101 passed and 0
failed; the interface tests 1117 with none failed (23 skipped; the new test
of the packages' default bindings first failed on this checkout's own
Dolphin, PCSX2 and RPCS3 packages, built before their bindings were fixed,
and passed once they were replaced with the published ones); the Windows
build compiles; the PCSX2 core's test script 44 passed, 0 failed, 6
skipped; on Linux the context is still Mesa's "4.5 (Core Profile)".

## A fresh GL context with every load that rebuilds (user-decided, 2026-10-19)

**What was wrong.** A load moved the GL context id, so a core rebuilt its
objects, but the old objects stayed in the driver: nothing in the restored
machine names the ones the frames after the restored frame made. They were
freed only when the session ended.

**What changed.** The owner chose a fresh context, as the Vulkan bridge
already has:

- A load that moves the id also makes a new context (made as the first
  one was, then made current), destroys the old one with everything in it,
  and lets the core rebuild in the new one. Not while a second session
  holds the context, and not for a core that declares it keeps its objects.
  If no new context can be made, the old one is kept, as before.
- A new context hands out names from 1 again, and a restored state can still
  hold names of the old one. The bridge keeps the names the core holds, by
  kind, from the calls that make and delete objects (`gl_names.h`), and takes
  every one of them again in the new context, empty, before the core runs:
  a stale name names nothing, and no new object gets one. The driver hands
  out the lowest free names; one that did not would be said once.
- `CHIMERA_GL_STALE=1` lists the first uses of a name held from before the
  fresh context, each with the calls before it. `CHIMERA_GL_SAME_CONTEXT_ON_LOAD=1`
  keeps the old behaviour, for A/B. The crash notes name the renewal.

**What it found.** Dolphin's rebuild leaned on the old context in six
places (a use of freed memory among them); the owner chose to fix Dolphin
before this was published. Dolphin's patch 0031 (dolphin repository,
docs/PLAN.md) has them. Before the fix, one rewind of Pro Rally 2002 left
129 KB of System RAM different from a straight run; after it, nothing.

**Measured on the GTX 1060.** Each of the nine cores that draw through the GL
bridge ran straight to a frame and again with 8 rewinds, with this engine
and with the one before it, and the final picture and the main memory were
compared with the straight run's:

| core | scene | picture | memory | GPU memory peak, before / fresh |
|---|---|---|---|---|
| Ruffle | Super Doggy, 1500 | same | (the heap moves with any rebuild) | 4845 / 4837 MiB |
| Azahar | Cars 2, 2400 | same | FCRAM, VRAM same | 4858 / 4856 |
| Flycast | Re-Volt, 4200 | same | System RAM, VRAM same | 4799 / 4799 |
| PCSX2 | Street Fighter EX3, 3600 | same | EE RAM, IOP RAM same | 5011 / 4955 |
| DuckStation | NHL Blades of Steel 2000, 2400 | same | Main RAM same | 4796 / 4796 |
| RPCS3 | Bejeweled 3, 900 | same | MainRAM same | 4969 / 4917 |
| xemu | Soulcalibur II, 2400 | same | (differs the same with both engines) | 4901 / 4887 |
| Vita3K | Geometry Wars 3, 1500 | same | (16 bytes differ with both engines) | 4822 / 4822 |
| Dolphin | Pro Rally 2002, 7800 | same | System RAM, ARAM same | 4932 / 4810 |

xemu's memory differs from a straight run with either engine because its
StateSaving writes the GPU's surfaces into the console's memory before a
state (xemu patch 0019); Vita3K's 16 bytes are the same with both. Neither
comes from this change. The table's fresh runs were made with a build whose
names report was narrower; the build committed repeated all nine with the
same pictures and memory (its memory peaks are not comparable: the card's
use by other programs had moved by then).

**Tested.** The engine's GL session test has a new part: textures made, one
deleted, a load; the old textures are gone, every new texture gets a name
the core does not hold (the one it deleted comes back), a stale delete
leaves the new ones alone, and with two sessions holding the context a load
keeps it. It passes through Mesa here and on the GTX 1060 (WGL). Broken on
purpose - no names taken again, then no new context - it failed both times,
at the line that checks each. The engine's 30 unit tests pass; the witness
101 passed and 0 failed; the interface tests 1117 with none failed (23
skipped); the Windows build compiles; the PCSX2 core's test script 44
passed, 0 failed, 6 skipped. Dolphin's gate passes with a fresh context
(25 ok, 9 skipped).

## Mapped GL buffers are the core's own memory (user-decided, 2026-10-19)

**What was wrong.** When a core maps a GL buffer, the driver answers with a
pointer to memory of its own, and the bridge handed that pointer to the
core. The core then wrote outside its sandbox: the bytes were in no saved
state, and since a load that rebuilds makes a fresh context, a pointer kept
across one pointed at nothing.

**What changed.** The owner chose to give the core a buffer of its own (a
"shadow") for every mapping it flushes or unmaps, and to leave a persistent
mapping that is never flushed (a coherent one, or one only read) on the
driver's pointer, because its writes reach the GPU with no call to copy
them at:

- miniBox's generated map wrappers allocate the shadow and hand it in; the
  unmap wrappers free the one Chimera hands back, keeping one for the next
  map of a like size. A whole-buffer map asks for the buffer's size first (a
  new bridge question, `GL_OP_BUFFER_SIZE`).
- Chimera fills the shadow from the driver when the core may read it or the
  unmap copies all of it back, copies flushed ranges on each flush and the
  whole range on the unmap (unless the core flushes range by range or only
  reads), and gives it back. A map written and flushed range by range is
  not filled: reading a mapping a driver made for writing is slow.
- After a load that keeps the context (RPCS3, which keeps its objects, or a
  second session holding the context), a mapping still open that is not
  persistent belongs to the frames the load threw away. It is closed at the
  next GL call, with nothing copied and nothing handed back to free: the
  restored core does not know it.
- Either side works with the other's old build: an old core hands in no
  shadow and gets the driver's pointer; a new core on an old Chimera gets
  the driver's pointer and frees its shadow. So the cores can be rebuilt one
  by one after this.

**Measured.** With `CHIMERA_GL_SHADOW_TRACE=1`, of the cores rebuilt so far
only RPCS3 maps anything it unmaps: its transient buffer, 3.6 MB, written
and invalidated, about 256 maps in 900 frames of Bejeweled 3. Dolphin and
Ruffle map only persistent coherent buffers on the GTX 1060 and shadow
nothing. On the GTX 1060, RPCS3 to frame 900 gave the same picture and the
same MainRAM as a straight run with the engine before this, in each of:
the new core on the new engine, the same with 8 rewinds to frame 600, the
new core on the old engine, and the old core on the new engine. Through
Mesa on Linux the same held with 8 rewinds to frame 300, and no load found
a mapping open.

The time to frame 900 on the GTX 1060 was 20, 21, 20 and 20 seconds before
this. With a new shadow allocated for every map it was 22, 22 and 21: each
3.6 MB of new memory had every page faulted in again. With the wrapper
keeping one freed shadow for the next map it was 20, 20 and 20, and 8
rewinds took 228 seconds against 234 and 236.

**Tested.** The engine's GL session test has a new part: a mapped range is
filled and written back; a map that lets its bytes go, or is flushed range
by range, is not filled; only flushed bytes reach the buffer; a
whole-buffer map is sized by Chimera; a core without shadows gets the
driver's pointer; after a load that keeps the context an open mapping is
closed with nothing given back and the next map works, while a persistent
one stays open and its flushes still arrive. It passes through Mesa and on
the GTX 1060. Broken on purpose five times (no fill, no copy back, no close
after the load, persistent shadows closed too, and a range flushed by range
filled), it failed each time at the line that checks it. The engine's 30
unit tests pass; the witness 101 passed and 0 failed; the interface tests
1117 with none failed (23 skipped); the Windows build compiles; miniBox's
tests 24 passed (6 expected failures); the PCSX2 core's test script 44
passed, 0 failed, 6 skipped.

**Published.** Seven of the nine cores that draw through the GL bridge
map buffers through it; Flycast and xemu map none. The seven were built
again with the new wrappers and published: Azahar "Development build
178781c3", Dolphin "Development build fa1af0ea", DuckStation "Development
build 96951ade", PCSX2 "Development build b09de11b", RPCS3 "Development
build 5daf1654", Ruffle "Development build 28baebf0" and Vita3K
"Development build 016c046c". Each package records that it was built with
miniBox dca40dc. Ruffle keeps its generated wrappers in its repository, so
they were made again by hand. A core built before this gets the driver's
pointer, as before.

Each published package was run on the GeForce GTX 1060 with this engine,
with the same inputs to the same frame as a run of the build before it on
the engine before this change:

- RPCS3 (Bejeweled 3, 900 frames) shadows its 3.6 MB vertex buffer. The
  last picture and the console's main memory are the same as before, and
  also after 8 rewinds.
- Azahar (Cars 2, 2,400 frames), DuckStation (NHL Blades of Steel 2000,
  2,400), PCSX2 (Street Fighter EX3, 3,600) and Dolphin (Pro Rally 2002,
  7,800) shadow no map on this card. The last picture and the console's
  memory are the same as before, and also after 4 rewinds (Dolphin was
  run without rewinds).
- Vita3K (Geometry Wars 3, 1,500 frames) shadows no map, so its new
  wrappers only pass each call on. The last picture is the same, and 104
  bytes of the console's memory differ from the build before. The new
  build repeats those bytes exactly, on either engine. The build before
  was made with a miniBox 33 commits older, so the difference is not
  pinned on this change; it was not looked into further.
- Ruffle (a Flash game, 1,500 frames) shadows no map on this card. The
  last picture is the same, and also after 4 rewinds. Its heap could not
  be compared: two runs of the same old build already differ in 4,749
  bytes of it.

Not tested: an AMD or Intel card, where the cores may map buffers in
other ways.

## Issue 317: every core's default bindings, checked (2026-10-19)

Issue 314 found a Nintendo 64 stick that did nothing because its default
binding named a gamepad input Chimera does not send. Issue 317 asked for the
same look at every core's other inputs. The default bindings of all 49
published packages (the dev builds of 2026-10-19) were read and checked:

- Every key, mouse button, gamepad button and axis a binding names is one
  Chimera sends. None was found wrong: the 16 cores with misspelt keys
  were fixed on 2026-10-14, and issue 314 fixed the gamepads.
- Every binding is for a control the core declares. None was found wrong.
- Every axis is bound to an axis. None was found wrong.
- No host input presses two controls by mistake. Three cores did:
  - MAME home bound the keyboard's X to player 2's button 2 on the Atari
    7800, Intellivision, Vectrex, Channel F and GX4000, beside player 1's,
    and on the CPCs to the joystick's button 2: the test that keeps only a
    pad's bindings was "starts with J or X", which the X key passes. The
    French CPC's number row was bound by the first character of each key's
    name ("E Acute 2 ~" to the E key), and the CPC6128's keypad Enter
    shared the PC's Enter with Return. Fixed in its generator (mamehome
    639c216).
  - EKA2L1 bound the phone's # to the 3 key, which is also its 3. # is on
    Equals now and * on Minus as well (eka2l1 b6e158f).
- A pointing device that moves can also press. One could not: Virtual
  Jaguar's light gun aimed with the host mouse and its mouse moved with
  it, but neither had a button bound (virtualjaguar 0f955bc).

Some shared keys are on purpose and stay. Where two devices can be plugged
into one port (PCSX2's pad and light gun, DuckStation's mouse and light gun,
Dolphin's GameCube pad and Wii Remote), they share keys, as only the plugged
one reads them. DOSBox-X and the X68000 put the joystick and the cursor keys
on the same keys, so a game reading either works. DOSBox-X and PCem leave the
mouse's middle and right buttons unbound: the middle button is the Capture
Mouse hotkey, and a right button that is bound takes the right click from
Chimera's menu.

The first three checks are now one more contract test,
`KeybindsNameKeysAndMouseInputsChimeraSends`, beside
`KeybindsNameGamepadInputsChimeraSends`: a key is known by the name Input
gives it, so the test cannot drift from
what is sent. It also refuses a panel that binds LeftShift without Shift,
since a default setup sends only Shift. It was a script in four core
repositories before.

Tested: the new test failed on a copy of the TIC-80 package with three
faults put in (a key called Return, LeftShift without Shift, an axis on the
Z key) and named all three; it passes on the 49 published packages and on
them with the three fixed cores' bindings. The other two checks are a
survey, not a test: a binding shared on purpose cannot be told from one
shared by mistake without reading the core.

## Chimera ships sixty themes, and Config > Theme is a window that shows them (user-decided, 2026-10-19)

The owner was given sixty colour themes, made by Nelson/Geferulf, and asked
for three things: put them in the repository and in the release, make their
folder the place Chimera reads themes from, and let a theme be seen before it
is chosen, which meant turning the Config > Theme menu into a window.

What changed:

- **A `Themes` folder** in the repository holds the sixty files. The build
  copies them into `build/Themes`, and `tools/build-bundle.sh` copies them
  from the repository into `themes` beside the program in the release; the
  bundle check fails if any is missing. They ship under Chimera's licence,
  with `"author": "Nelson/Geferulf"` in every file and a line in CREDITS.md
  (owner's answers).
- **Where themes are read from** is a path in Config > Paths, "Themes", which
  by default is `themes` beside the program, like cores, Lua and Tools (the
  owner chose the name). The folder used before, `Themes` in the data
  directory, is still read after it so nobody loses a theme, but nothing is
  written there any more. A name in both folders is the Themes path's; the
  other file is listed as not loaded. Changing the path in Config > Paths
  re-reads the themes at once.
- **Config > Theme is a window** (`ThemeChooserForm`): the themes on the left,
  Light and Dark first and then by name; a drawing of Chimera in the theme
  picked on the right (`ThemePreview`); the description, author and file under
  it; and Open Themes Folder, Write a Copy to Edit and Reload Themes as
  buttons. Picking only changes the drawing; OK turns the theme on. Pointing at
  the drawing names the colour role and its value. Write a Copy to Edit now
  copies the theme picked rather than the one on, and never writes over a file
  that is there ("Dark copy 2" when "Dark copy" exists); before, it
  overwrote one silently.

The drawing is drawn rather than made of real controls because real controls
take their colours from the theme that is on: the theme engine walks them, and
much of the frontend asks for the current theme while painting.

**The shipped themes are taken as given (owner's decision).** 57 of the 60
have text under the 3:1 contrast floor that `ThemeContrastTests` holds themes
to: 129 of the pairs are disabled text or disabled glyphs, and of the other
243, 73 are under 2:1 (Halloweentown's green text on a row under the pointer
is 1.5:1). Offered the choice of nudging the colours, exempting disabled text,
or shipping as given, the owner chose to ship as given. So the contrast test
now measures only the built-in themes, and `ShippedThemesTests` holds that
every shipped file loads, names its author and has a plain ASCII file name.

Changed from the files as given, colours untouched (checked by the import
script): each file named after its theme in ASCII without spaces (Gyudon,
Tankoban; "Srping Leaves" became SpringLeaves; Evergreen and Frankenberry
lacked the `.chimeraTheme.json` ending); raw line breaks inside four
descriptions written as JSON escapes; and four descriptions that quoted song
lyrics or a translated poem at length cut to a few words (Electric Eye,
Plumbers, Skellington, Dim Sum), which the owner chose over keeping them.

Tested:

- Interface tests: 598 logic tests and 348 window tests, none failed or
  skipped. New: `EveryShippedThemeLoads`, `ThePathsDefaultIsTheFolderBesideTheProgram`,
  `TheChosenFolderIsReadAndTheFormerOneAfterIt`, and six window tests of the
  dialog, among them `TheDrawingIsInThePickedThemesColours`, which reads 24
  surfaces of the drawing off the screen for each of the 62 themes.
- Each was seen to fail when broken: a theme file with a misspelt role made
  `EveryShippedThemeLoads` fail naming it, and painting the drawing's status
  bar in its text colour made the drawing test fail for every theme.
- The dialog photographed under all 62 themes passed the screenshot checks
  (no toolkit beige, a chosen row readable).
- On Windows the dialog was opened and all 15 surfaces checked were in the picked theme's
  colours for Aizome, Hackerman, Win98 and Bubblegum Pop, with the window round
  it Dark. It listed 63 themes: the 62 and a "Dark copy" from the owner's old
  data-directory folder, which is the old folder being read as intended.
- The witness: 101 ok, 0 failed. A Linux bundle built from this tree carries
  the 60 files in `Themes`.

Not tried: clicking through the window by hand in the Windows build. The
harness opens it, picks and accepts in code.

## A package's picture size is room, not a buffer held (2026-10-19)

The owner decided DuckStation offers internal resolutions up to 16x (#316's
round; the DuckStation core's plan has the decision). Its package must then
declare a picture of 12288x9216, and the engine and the window each allocated
the declared size up front: 450 MB a buffer, in every DuckStation session at
any resolution. The engine filled its buffer when the session began, so the
memory was really used.

Now both keep as much as the largest picture the core has drawn, and grow
when it draws a larger one: the engine before it copies a frame, and before
it puts up a kept picture after a load (room for the largest one the kept
pictures hold, 16 MiB); the window when it copies a frame and whenever a
caller asks for the buffer, which a load can resize without a copy. A
session that has gone keeps reporting the size of its last picture. The
package's size is still the most a core may draw, and a larger picture is
still cropped to it.

Tested: the synthetic core now reports its live size (128x120, the size it
declares, so nothing else changes), and the witness's B:box:picture-room
replays a movie with its package declaring 8192x8192: the goldens' RAM and
picture, and a process peak of 286 MB against 285 MB as declared. The same
two runs on the published build before the change (fce46b88) peaked at 480
MB against 225 MB, which the leg's limit of 128 MB more refuses.

## A control's name may hold a LogKey's separators (2026-10-19)

A movie's LogKey names its columns: "#" starts a group of them and "|" ends
a name. Since 2026-10-15 the engine reads a movie's lines by those names, as
the window always did, and both split the key at every "#". Three cores have
names that hold a separator: Virtual Jaguar's keypads ("P1 #" to "P8 #"),
PUAE's "Key #", and MAME home's "# >", "0 #", "3 #" and "| @". So "P2 #" was
read as a name "P2 " and a group too many, and every column after it as
another: Virtual Jaguar's test of a project played in the headless runner
has failed every day since 2026-10-16, and a movie of those cores pressing
those keys played back with the presses on other columns.

A key is now read with the machine's own names first: at each place the
longest of them that is there whole, followed by "|" or the key's end, is one
name; otherwise "#" starts a group and a name runs to the next "|". The engine
(EntryLayout::buildFromKey) and the window read it the same way. Nothing a key is written as changes.

Tested: test_movie_entry's keyNamesMayHoldSeparators (three groups from a key
with "P1 #", "| @" and "# >", each press on its own control, a machine's own
key read back as itself) and the window's own test of its reading both fail
on the old reader and pass. Engine tests
30/30, witness 102 ok and 0 failed, interface tests 119/63/599/348 with none
failed, the Windows build compiles.

## Four more Vulkan commands, and descriptor update templates (user-decided, 2026-10-19)

The Vulkan bridge offered three extensions whose commands it did not have:
`vkCmdSetPatchControlPointsEXT` and `vkCmdSetLogicOpEXT`
(`VK_EXT_extended_dynamic_state2`), `vkCmdSetLineStipple`
(`VK_EXT_line_rasterization`) and `vkCmdPushDescriptorSetWithTemplate`
(`VK_KHR_push_descriptor`). A renderer that saw the extension offered and
asked for one got nothing. No core is known to have failed for it. The
owner decided to add all four now, knowing that a core built afterwards
needs a Chimera at least as new (miniBox 9ebf68b).

The last one needs descriptor update templates, which the bridge had
retired: a template's data holds image views, samplers, buffers and buffer
views at offsets only the template says, and the driver was handed the
guest's handles and killed the host. They are back, written by hand: the
host keeps each template's entries and, for each use, copies the data and
translates every handle in it, as it does for `vkUpdateDescriptorSets`. A
template entry it cannot translate (an acceleration structure) is refused
when the template is made.

Tested on llvmpipe by miniBox's Vulkan test: the pushed triangle is drawn
again through a template whose buffer sits eight bytes into the data, and the
picture through the bridge is the direct run's; the bridge answers the six
new commands with two names as the driver does. With the bridge as it was,
the test fails on both. The same test passes on the GeForce GTX 1060 (the
Windows runner, NVIDIA's Vulkan 1.4 driver: the same pictures, and every
one of the thirteen commands with two names present on both sides). No
core calls these commands yet.

## Encode Video has presets that write the command (user-decided, 2026-10-20)

Issue 323 asked for ready-made encoding commands to pick from. The Encode
Video window had only its command box: a list of the A/V writer's formats
had sat there once and was taken out, because the command box answers the
question better, and the list left behind was called by nothing. The owner chose presets that fill the box: a Preset box above
the command offers nine commands by name - the default, YouTube at 2160
lines (landscape or portrait, smooth or sharp scaling) and four lossless
ones from the issue - and picking one writes its command into the box. The
box stays the choice: an edit makes the Preset box say "Your own command",
and typing a preset's command shows that preset. The presets are the
window's own; the dead list is gone.

Tested: the window tests (a new one) pick a preset, see
its command and extension, edit it into "your own" and type the default
back; with picks ignored the test fails. All the window's tests pass (348
and 1 skipped). Each preset's command encoded
a one-second test picture with Ubuntu's ffmpeg 6.1.1 (the YouTube ones to
2880x2160 and 2160x1620 from 320x240); whether an encoder (libx265,
utvideo) is in another ffmpeg is that ffmpeg's.


## Godot and OpenBOR join the official cores (user-decided, 2026-10-20)

Issue 322 asked for games made with the Godot Engine, issue 325 for
OpenBOR, the engine of 2D beat 'em ups. The owner decided to build both
and, once each passed its tests, to publish them.

`chimera-core-godot` is Godot 4.7.2's engine for exported games, built for
the sandbox with a Chimera module in it: the clock and the date are the
frames', the files the game writes live in the core's memory, the window is
a software OpenGL context (Mesa's llvmpipe, linked into the core and made to
generate the same machine code on every computer: one CPU, 128-bit vectors),
and the picture is read back from it. A game's compiled scripts load only in
the Godot version that exported it, so the owner chose a core per version:
the 4.6, 4.5 and 4.4 cores (`chimera-core-godot46`, `-godot45`,
`-godot44`) are the same code with another Godot pinned, each its own
package and roster row, and a core refuses a game of another version,
naming the core to use. The 4.4 core also takes games of 4.0 to 4.3. Games
that need native extensions or another scripting runtime cannot run in the
sandbox.

`chimera-core-openbor` is OpenBOR's SDL port with SDL 2, run on a stack of
its own inside the core: each picture OpenBOR presents ends a frame. A loop
that waits by reading the clock over and over (OpenBOR's fades do) ends the
frame after 4096 reads, since the clock only moves when the game waits. Two
patches to OpenBOR: players 2 to 4 get keys by default (upstream gives them
none), and the core is told when the game has loaded and why OpenBOR stops,
so a game that cannot load is refused when it is opened.

Neither core has been tried on a commercial or published game. Each is
tested on games anyone may have, in CI too: Godot's "Dodge the Creeps" demo
(MIT) and a probe game; for OpenBOR, a small game its repository's script
makes. The native build and the sandbox agree on every picture, the sound
and what the game saw, and a save and load before every frame changes
nothing. Nothing in Chimera's own source changed: two roster rows and the
README's table of cores.


## The BBC Micro joins MAME Home Systems (user-decided, 2026-10-20)

Issue 329 asked for the BBC Micro. The owner decided to add it to the MAME
Home Systems core instead of building a core of its own: MAME emulates it in
software, step by step with its 6502, and that core already runs MAME's
other home computers. The Model A, Model B, B+ and Master 128 are offered,
with their discs, tapes and keyboard; the Acorn ROMs are firmware the user
supplies, as MAME needs them. No Acorn ROMs were at hand, and the owner
decided to publish without booting one; the issue is closed on that build,
saying so. One row of the roster changes (a system added to
`mamehome`) and the README's table of cores.


## One clock for a build's and a core version's date (user-decided, 2026-10-20)

Issue 331: Chimera listed an xemu core as "2026-10-18 12:35 (8c11acc3)",
and its release was "Nightly 2026-10-19". Both were right. The first is when
the commit was made (issue 67), in the computer's own time zone (the
reporter's was UTC-4: the commit was made at 16:35 UTC). The second is the
day the daily build ran. The report's build line, meanwhile, was in UTC.

The owner decided that every date Chimera shows for a build or a core
version is in Central European time as Zurich keeps it: CET in winter, CEST
in summer, with the zone written after it. The core version above now reads
"2026-10-18 18:35 CEST  (8c11acc3)", and the build line reads "Commit
9dbd35bac (2026-10-20 10:58 CEST)". New release titles carry the commit and
its time on the same clock, "Nightly 2026-10-19 (8c11acc3, made 2026-10-18
18:35 CEST)", and a core's release notes give the exact line Chimera lists.
Release names already published stay as they are.

The rule (the last Sundays of March and October, at 01:00 UTC) is worked out
in `european_clock.cpp` and `tools/european-time.py` rather than read from the
computer's time zones, so two computers write the same text for one moment.
The script agreed with the system's Europe/Zurich zone on 200,000 random
moments from 2001 to 2031. Times of the user's own actions, such as when
unsaved work was last kept or a cache entry last used, stay in the
computer's local time: they are not compared with anything published.


## The first frame after starting or a state load has 2 minutes (user-decided, 2026-10-21)

Issue 332: Oblivion on RPCS3 stopped in its first frame with "the core did
not finish a frame in 20 seconds". RPCS3 analyses and compiles a game's code
in its first frame. The frame limit (2026-10-19) starts again whenever the
core shows it is working, and storing a compiled piece of code counts, but
on a GTX 1060 machine the gaps between two stored pieces reached 12 seconds,
and the analysis before the first one is longer still. The first frame died
40 seconds in, with nothing stored. Chimera then loaded a state, the frame
after it was stopped the same way, and the reporter saw one such message
after another.

The owner decided that the first frame after starting, and the first frame
after every state load, has the start's limit, 2 minutes, instead of a
frame's 20 seconds. The time still starts again whenever the core shows it
is working. A frame after that has 20 seconds as before. A load includes a
seek or a rewind through the stored states, a state file, and the load
Chimera makes after a core stopped.

Tested:

- The witness has a new test, "first-frame-time". The synthetic core, on a
  ROM ending "SLOWFIRST", spends about 0.4 seconds in its own code in the
  first frame after starting and in the first after every load. With a frame
  limit of 0.1 seconds the run plays the movie, seeks back through a stored
  state and ends with status OK, after two such frames. Before the change
  it stopped in frame 0: "the core did not finish a frame in 0.1 seconds".
  The test of a core stuck in its third frame still stops it at the frame
  limit.
- Oblivion in the Windows build of the interface on the GTX 1060, with the
  reporter's core (cc813e47), disc and firmware, and an empty compile cache:
  nothing was stopped. The 600 frames the test asked for took about 18
  minutes, 16 of them in the core while it compiled (201 pieces stored).
  With the engine before this change the same run stopped 40 seconds in.


## Open4DBoxing joins the official cores: 4D Sports Boxing (user-decided, 2026-10-21)

The owner asked for a core of Open4DBoxing, a reconstruction of 4D Sports
Boxing (Distinctive Software, DOS, 1991), and then for it to be published.
The game's four programs, one for each video card (MCGA, EGA, CGA and
Tandy), are recompiled whole from their machine code into C, on a model of
the PC they expect: the timer chip, the BIOS, DOS, the sound cards, and an
MT-32 through Munt.

`chimera-core-open4dboxing` is built the way the OpenSamurai core is: the
game runs on a stack of its own inside the core, and each video frame it
passes ends a step - 70.086 Hz for the MCGA program, 59.92 Hz for the
others; which program runs is a setting. The user's two floppy images are
firmware, read in memory and never written; the files the game writes live
in the core's memory, so a savestate carries them. Nothing reads the host's
clock: the game's clocks go by the instructions it runs, and the date and
time it starts at is a setting. The controls are the mouse (its buttons,
and its place as two axes in the picture's pixels) and the 55 keys the game
reads. Properties - each boxer's health, stamina and endurance, whether a
boxer is down, the punches thrown and landed, the name the manual's question
asks for - are given for the MCGA program, whose data the reconstruction
maps.

Tested on the owner's floppies, which CI does not have: native and sandbox
agree on every step of six runs (the title; an exhibition bout from the
title into the ring; the EGA, CGA and Tandy titles; the MT-32), a save and
a load before every step change nothing, and a new process finishes each
run from a state half way through. Every check new to this core was seen to
fail when what it checks was broken. Package checks: 22 passed, here and in
the core's CI; the first published build is "Development build 039785ef".

The frontend gate ran the same day, on that build's nightly package and
Chimera's own bundles of this commit (e5dd821f): the exhibition's 31,200
frames end on the native reference's memory through `chimera-run` (a build
of the commit before; the bundles carry none), in Chimera's window
(headless), in a new Chimera playing the project the first
one saved, and in the Windows build playing that saved project; the
property library, the core's refusal of a start date that is not a date
and the package's keybinds reach the frontend, and nothing is written in
Chimera's folder or its default data home. 10 checks passed, each seen to
fail on a break of its own. The Windows build ran under Wine 10.0 on Linux,
not on a Windows machine, and two things it needed there are Chimera's to
know. On a machine with more than 255 CPUs (jaffanator2 has 256) Wine tells
Windows programs there are none - the count wraps in a byte - and Chimera
stopped before its first window; a preloaded `sysconf` that says 8 got past
it. And headless under Wine, Chimera's first window
does not bring up Wine's desktop, so the OpenGL probe failed ("no driver
could be loaded") until something had brought the desktop up first.

Found on the way: on a host with GCC 15 (Ubuntu 26.04), miniBox's C++
guest toolchain does not build. GCC 15 compiles C as C23, where the guest
musl's `char *basename();` declares a function of no arguments, and
libbacktrace stops there. GitHub's `ubuntu-latest` was still 24.04 on
2026-10-20; the core's CI names `ubuntu-24.04` so that it stays there until
miniBox builds with GCC 15.

Nothing in Chimera's own source changed: a roster row and the README's
table of cores.


## Going back after a core stopped partway is tested (2026-10-21)

Issue 330: Halo on xemu was stopped at frame 3 with "the core did not finish
a frame in 20 seconds", and after Chimera put the machine back on a stored
frame the core crashed inside its own `malloc`. The stop had a plain cause in
the core: at boot the Xbox BIOS sets the GPU's clock far ahead, and xemu
stepped the GPU's timer alarm forward one 32-bit epoch per firing, 220
million firings in frame 3, about ten seconds of host time on a GTX 1060
machine and more than twenty on the reporters'. The frame watchdog, new on
2026-10-19, saw no sign of life in that time. The xemu core now moves the
alarm to the current epoch at once (its patch 0021), and the boot frame takes
a moment.

The crash after going back was not reproduced, so the path itself is now
tested. `chimera-run --recover` answers a core that stops partway through a
frame the way the frontend's "go back to the latest safe point" does: it
restores the newest stored frame before the one that died and plays on, once.
The witness test "core-recovers" stops the synthetic core in frame 5, after it
has scribbled over its RAM and left a block of its heap written (a ROM ending
STOPONCE), goes back to frame 3 and plays the movie to the end. The machine
ends on the goldens, RAM and VRAM. With the synthetic core changed on purpose
to keep one byte of the dead frame's writes after going back, the RAM differed
and the test failed.


## On Linux a stop of a guest with its own thread pointer ends cleanly (2026-10-21)

While issue 330 was measured on Linux, a stop of xemu by the frame watchdog
crashed in the C library's `realloc` instead of ending the frame; on Windows
the same stop ended cleanly. xemu keeps its own `%fs` (it declares thread
locals), and the stop arrives by a signal, usually while the guest runs. The
stop's handler read its own thread's variable through `%fs` before putting
the host's back, which miniBox's fault handler never does. miniBox 207c5d1
puts the host's `%fs` back first, and the guest's on every way back to the
guest. Its test `foreign-signal` now stops a guest with thread locals while
it spins: the call ends with the stop's reason, and the host's thread locals
and allocator are its own afterwards. Without the change that test never
ended. xemu stopped by a one-second frame limit on Linux now reports "did not
finish a frame in 1 second", where it crashed.


## Lua events no core can run are marked, and Copy gives Lua (2026-10-21)

Nine Lua events can never run in Chimera: `event.on_bus_exec`,
`on_bus_exec_any`, `on_bus_read`, `on_bus_write`, `oninputpoll` and the old
names `onmemoryexecute`, `onmemoryexecuteany`, `onmemoryread` and
`onmemorywrite`. Each would run a Lua function in the middle of a frame, and a
core runs in a sandbox that Chimera calls once a frame and that cannot stop
part way through it. Since issue 113 they raise an error rather than hand back
an id that never fires, but the Lua Functions list (Lua Console, Help) still
described them as working (issue 341). They are kept so that scripts written
for other emulators load and can guard the call with `pcall`. Now:

- the list shows them as "[Not available]", from a new mark in the
  interface, and each description begins by saying why;
- their examples show the call guarded with `pcall`;
- the error a script gets from `pcall` says why, and the Lua Console prints
  the reason once, followed by where it came from;
- `event.availableScopes` and `event.can_use_callback_params` say that no
  Chimera core has these events.

Copy in the Lua Functions list wrote the function as `//library.name(...)`,
a C comment that Lua reads as an error; it now writes `--library.name(...)`,
a Lua comment (issue 343).

Tests: one checks that exactly these nine are marked
and described as not available, that a script guarding `on_bus_read` with
`pcall` reads the reason and goes on, and that an unguarded one logs the
reason once on the first line; another checks the copied text. Each failed
once on purpose: with `on_bus_read` unmarked, with the old message, and with
`//` put back.


## Config > Firmware keeps its place on a rescan (2026-10-21)

Scan Folder, Rescan and Set File in Config > Firmware build the list again,
and it went back to its first row, so after a scan the person scrolled down
again to see whether the file they wanted was found (issue 348). The window
now notes the row drawn halfway down the list and how far down it is drawn,
and after the rebuild brings the same row back to the same place (the row at
the same position when that row is gone). Halfway rather than the top row:
a row scrolled under the column headings could still answer a hit test
there, which put the list two rows off. The list can only be scrolled by
bringing a row into view, so the row put at the top is corrected until the
noted row is back within a row of its place.

Test: `ARescanKeepsTheListWhereItWasScrolled` scrolls 80 rows to row 50,
rescans, and finds the same row at the same height; with a row removed
further up, the same row (now one index earlier) is still at that height.
It passes on Linux and on Windows, and failed with the scroll-back call
removed.


## A themed list draws its own grid lines (2026-10-21)

Under every theme but Light, the Lua Functions list lost the lines between
its rows on each row the pointer crossed, until the window was opened again
(issue 339). Such a list is drawn by Chimera: each cell is filled edge to
edge. Its grid lines were the Windows control's own, drawn only when the whole
list is painted, so a row repainted alone was left without them. While a theme
draws a list, its grid lines are now drawn with each cell along the cell's
bottom and right edges, in the theme's `GridLines` colour (the colour the theme
preview already showed), and the control's own lines are off; they come back
when the theme gives the list back under Light. The four lists with grid lines
get this: Lua Functions, the Lua registered functions, the archive chooser and
the debugger's breakpoints. The empty space below the last row no longer has
lines.

Test: `AHoveredCellKeepsItsGridLines` draws a cell in the hovered state the
way a hover repaint does, and finds the grid colour on its bottom and right
edges, the control's own lines off, and back on under Light. With the lines
not drawn it failed (Dark: the edges were the row colour). On Windows (a
dark list captured with PrintWindow) the lines show on every
row in the grid colour; before the change they were the control's white. The
hover itself was not driven there: it needs the real pointer, and a repaint
of single rows asked for by the program kept the old lines too, so the old
loss was not reproduced on this machine.


## The wizard's Game step fits the window however wide it was (2026-10-21)

In the New Project wizard, the boxes on the Game step (one per file the core
takes) did not reach across a wide window, and narrowing the window shrank
them until they and their Add and Remove buttons disappeared (issue 333). Each
box is laid out for a panel 544 pixels wide and anchored to both sides, which
keeps the gap to the panel's right edge it has when it is added. The step is
built when the core is chosen; on Windows the panel already has the window's
width then, even though the step is not yet shown, so a window widened before
Next gave every box a wide gap, and the gap stayed as the window narrowed.
Each box now takes the panel's width when it is made, leaving the 24-pixel
gap it was designed with.

Test: `TheGameStepFitsAnyWidthTheWindowHad` builds the step again in a window
1100 pixels wide, finds every box reaching to within 60 pixels of the edge,
then narrows the window to its smallest and finds every box, list and button
inside it. Without the change it failed (the box stopped 650 pixels short).
The hidden step is laid out only when it is shown, so the test builds it
while it is in view. On Windows the wizard tests hang when run from a copy of
the test folder (an older wizard test too), so this one was not run there;
a picture of that run showed the first box across the full width.


## Ikemen GO joins the official cores (user-decided, 2026-10-21)

Issue 324 asked for Ikemen GO, the open-source fighting game engine that runs
M.U.G.E.N characters, stages and screenpacks. The owner first deferred it,
then asked for a trial, the core, background videos, and a game of their
own to work with every one of its music files and sounds, before publishing.

`chimera-core-ikemen` is Ikemen GO 1.0.0, which is written in Go, built as a
C archive with a Go 1.27.3 of the core's own: a few patches to Go make a Go
program run in the sandbox (its system calls, its threads' stacks, and its
clock, which is the frames'). It draws with its OpenGL 3.3 renderer on a
software OpenGL inside the core (Mesa's llvmpipe, made to generate the same
code on every computer); its background videos play through FFmpeg, decoded
on each frame; every file call goes to the game's zip and the files the game
writes, kept in the core's memory. The round, its time, the random seed and
each fighter's life, power, position and gauges are game properties, most of
them writable. (It was listed as a game core here at first; it is an
emulator core, see below.) A game made for an older Ikemen GO
(0.97 to 0.99) plays too: the core recognizes the older release's own
scripts in the game's folder and uses its v1.0 ones instead, as Ikemen GO's
upgrade advice says, swaps the older Boss Rush, Score Challenge, Time
Challenge and VS 100 Kumite modules for their current versions, and turns
the old save/config.json into the config.ini v1.0 reads (the owner chose
this over a separate core for v0.99). Ikemen GO's MP3 decoder is patched to
look past junk at a file's end, and music in a format Ikemen GO has no
decoder for (Musepack) plays through the core's FFmpeg; Ikemen GO's rule of
not playing sounds under 128 bytes stays (owner).

Tested on the screenpack Ikemen GO 1.0.0 shipped with (Kung Fu Man), on
probes of the core's own, on a game made from them for v0.99.0, and on the
owner's KOF Special, a fan game made for v0.99 that cannot be shared and
that CI therefore skips: Arcade, VS Mode, Training, Survival, Boss Rush and
Watch Mode, native and sandbox identical, a savestate in a fight the same as
the straight run, and all its 329 music files and 13,447 sounds as ffmpeg
decodes them. The core's test script: 53 passed, none skipped, on a computer
with KOF Special; on CI 48 passed and the KOF Special test skipped. The
frontend test 6 ok; package checks 22 passed; Chimera on Windows draws KOF
Special's frames 1600 and 4000 as Linux does (its sound was not compared).
The first published build is "Development build ccaaa382 (made 2026-10-21
15:38 CEST)". Not there: netplay, the Vulkan renderer, the GPU; a 1280x720
game runs at about 7 to 10 frames a second on a fast computer, a 640x480 one
at about 29.

Nothing in Chimera's own source changed: a roster row and the README's
table of cores.


## Bug report zips go to a Bug reports folder (user-decided, 2026-10-21)

The Save as Zip... button of Help > Report an Issue... opened on the desktop every time, and the person
browsed to Chimera's folder each time (issue 340). Config > Paths now has a
Bug reports entry, by default a Reports folder in Chimera's base folder, as
Screenshots is; Save as Zip opens there, making the folder when it is not
there yet, and falls back to the desktop when it cannot be made. The owner
chose this over opening in Chimera's folder and remembering the last folder
used, and over remembering only. A configuration saved before gets the entry
when it is read (missing global entries are added from the defaults).

Test: `SaveAsZipOpensInTheBugReportsFolder` finds the default path to be
Reports in the base folder, and the folder made and used; with the folder
ignored it failed.


## A package may ship a picture of its controller (user-decided, 2026-10-21)

Config > Controls listed a core's buttons with no idea what the controller
looks like, and a person matching an unfamiliar pad looked it up on the web
(issue 338). The window always had a picture column, kept closed because
the frontend knows no controller's looks. The owner chose to let a package
ship one, over drawing a schematic from declared button positions and over
tooltips only.

An input declaration (the package's, and each machine's) may name a PNG in
the package: `"picture": "pictures/pad.png"`. The waterbox core offers its
file when it is there and
inside the package (a name that leaves the package is no picture), and the
window opens the column and shows it, scaled to fit; without one, or with a
file that is not a picture, the column stays closed. The picture's licence
is declared like any component's, and the owner's condition holds: only
pictures that are free to redistribute, or drawn for Chimera.

Tests: `ControllerPictureTests` (the file is found only inside the package;
the window reads what the core offers, and nothing from a file that is not a
picture), which failed with the inside-the-package check removed; and the
contract test `APictureOfTheControllerIsAPngThePackageLicenses`, which on
made-up packages failed for a missing picture and for one the licence list
does not name, and passed for one that is present and named. No published
package declares a picture yet.


## Ikemen GO is an emulator core, as OpenBOR is (user-decided, 2026-10-21)

Ikemen GO was listed with the game cores in the README and marked
`"kind": "game"` in `official-cores.json`. The owner corrected it: like
OpenBOR, it is an engine that plays many games made for it (and M.U.G.E.N
characters and stages), so it is an emulator core. The README lists it with
the emulator cores, beside OpenBOR, and the roster no longer marks it a game.
Its package still declares `"kind": "game"` in its own `waterbox.config`,
which is what Chimera's windows go by; that is the core repository's to
change.


## A core may call back part way through a frame, at each read of its controls (user-decided, 2026-10-21)

Every core runs in a sandbox that Chimera calls once a frame, so a Lua
function could not run in the middle of a frame, and `event.oninputpoll`
refused on every core (issue 341). Issue 344 asked for a mode that allows
such calls. The owner chose to start with the reads of the controls, core by
core, and to keep memory callbacks (`on_bus_*`) refused: they need a hook in
each emulated CPU, which the recompiling cores cannot offer, and they slow
emulation down.

The way back out of the frame already existed: miniBox's callback slots,
through which the GPU bridges and the code cache are reached. A core that
exports `SetInputPollCallback` is handed the engine's function in slot 3 and
calls it at each read of its controls. The engine keeps which session is
running a frame on this thread, and calls the frontend's function
(`ce_session_set_input_poll_callback`) from inside the frame; the call counts
as the core's work for the frame watchdog. The waterbox core gives such a
core input callbacks, switched on in the
engine only while a script has one registered; any other core still refuses
`event.oninputpoll`, now saying it does not report its reads. A Lua function
called there may read memory, and must not advance, save or load.

The synthetic core reports each INPUT its machine executes. Tests: the
witness's `E:input-polls` (`chimera-run --count-input-polls` counts one call
for each frame that was not a lag frame, and the machine ends on the goldens)
and `T:box:inputPolls` (a script in the real window registers
`event.oninputpoll`, sees one call in a frame the game reads its pad in,
reads memory from inside it, and no call once it is removed).

## Default bindings use a joystick's first hat, and controls are listed in groups and rows (user-decided, 2026-10-21)

Chimera numbers a joystick's hats from 0 ("J1 POV0U" is the first hat's up).
33 cores bound their directions to POV1, a second hat that a stick with one
does not have, and none to POV0, so a joystick that is not a game controller
moved nothing until it was bound by hand (issue 336). Every core now binds
POV0. The console cores that had no game controller defaults got them, by
place: the bottom face button is where a Super NES's B is. Computers, phones
and touch screens keep the keyboard and the mouse. Only bindings changed:
nothing a movie records did. A package test, KeybindsNameAJoysticksFirstHat,
fails on a package that binds a later hat without the first. Chimera's own
tests run against the cores' nightly builds, and 33 of those bound POV1 until
each core's next nightly, so the test waited until they were all published
(2026-10-22: none of the 56 nightlies binds a later hat) and went in then.

The owner also decided how controls are listed (issue 337). A pad's controls
of one kind sit together: Dolphin's Triforce and Flycast's arcade boards list
Coin beside Start, and MAME home no longer interleaves the Amstrad CPC's
joystick with its keyboard. The Genesis pad keeps its own A B C Start X Y Z
Mode. A computer's keyboard is listed as it sits on a desk: the top row
first, left to right, then the keys above the arrows, the arrows and the
number pad. Godot's four cores and Ruffle take a whole PC keyboard and were
put in rows too, on the owner's word. A project made before keeps playing:
a movie's columns are read by their names (EntryLayout::buildFromKey), which
Uzem's projects from before the change showed. A core that turns a held key
into an event in its table's order (DOSBox-X, TIC-80, Godot, Ruffle) keeps
that order and maps the listed number to it, so two keys pressed on one frame
still reach the game as they did.

## Firmware with no hash is found by its size and a name hint (user-decided, 2026-10-22)

Some firmware has no hash a core can pin, because every console writes its own:
the DS firmware, the DSi's BIOS, firmware and NAND. Scan Folder and the
Firmware folder found such a file only under the exact name the core declares
(bios7i.bin, nand.bin), and people's files are named otherwise
(NDS_Bios7i.bin, DSi_Nand_USA.bin), so none was found (issue 349). The owner
chose: a firmware entry may list `nameHints`, pieces of a file name that mark
it. When the declared name finds nothing, a file of the declared size (any
size when none is declared) whose name holds one of the hints, compared
without case, answers the entry if it is the only one. One dump in two places
counts once: candidates are counted by hash. With two or more, nothing is
guessed, and Config > Firmware says "N candidates - choose one with Set...".
A file found this way says "found by size and name". An entry with a hash is
still found by its hash and nothing else. The hints sit in the package, so a
core decides what marks its files; melonDS declares them for its unpinned
entries.

## A core's release page says what changed since its last nightly (2026-10-22)

Every nightly's page said the same thing but its hashes, so finding what a
nightly changed meant reading the core's history by hand (issue 357). Now each
release, nightly and development build alike, lists the commits since the
core's last nightly, each linked to the commit. The cores' commit subjects
name Chimera's issues as `chimera#352`, which GitHub does not link on another
repository's page, and a bare `#352` there would be the core's own issue 352;
so those are linked to Chimera's issues and a bare `#N` is left for GitHub.
Long lists stop at 50 with a link to the rest. It is written by
`tools/release-changes.py`, called from `tools/publish-core.sh`, which every
core's publishing job runs, so all cores have it from their next release
without a change of their own. Measured: on the X68000 core's history it lists
the two commits since nightly-2026-10-15, with chimera#352, #351 and #336
linked; outside a checkout it adds nothing.

## Settings may be wider whole numbers, and a date and time (2026-10-22)

A setting could be a `bool`, an `int`, a `float`, a `string` or an `enum`.
A core that wanted a number from 1 to 4294967295 had no type for it (issue
358), and the owner asked for that and more. Four types were added, each
because cores need it:

- `uint`, `long` and `ulong`, the ranges of their C types. Seeds are the
  common case: nine cores stop a random seed at 2147483647 because an `int`
  does.
- `datetime`, a date and time in UTC. Sixteen cores declare a clock's start
  as an `int` of seconds since 1970, which ends in 2038, and four more as
  text in the form 2000-01-01 00:00:00 that each core parses itself. A
  `datetime` is kept, recorded and handed to the core as those seconds, a
  64-bit number. The grid shows and takes the text. So a core whose `int`
  is seconds changes only its declaration, and its projects keep their
  values.

Numbers past 2^53 needed a change in the engine. Its JSON library (cJSON,
vendored in `extern/cjson`) keeps every number as a double, which holds a
whole number exactly only up to 2^53. It also printed one of 10^15 or more
as `1e+15`, which a core's `strtol` reads as 1. Settings pass through it on
their way to the core and to the project file, so a `ulong` came back
rounded. Now a whole number of 10^15 or more keeps the digits it was read
with, and they are written back as they came. Copies keep them, and a new
value replaces them. A condition on a setting (`"is"`) compares those digits,
so two values one apart past 2^53 are told apart. Below 10^15 nothing
changed. miniBox's guest kit gained `wbx_setting_ulong`, because
`wbx_setting_long` stops at the top of a long.

The settings grid edits each type as itself: a `uint` row refuses -1, and a
`datetime` row opens a calendar. A value from a file is clamped to its
type's range and then to `min` and `max`, which are now decimals so that a
`ulong`'s bounds are exact. A type Chimera does not know used to be taken
for text without a word. Now a package test, EverySettingTypeIsOneChimeraKnows,
refuses it.

Tested: the engine's tests (a 64-bit number, -2^63, 2^53+1 and 10^15 come
back as written; a project keeps 18446744073709551615; a condition tells
9007199254740993 from 9007199254740992), miniBox's settings test, seven
tests of the types in the settings grid, and the package tests. Each new
test failed with its fix taken out. No core uses the new types yet, so none
has been tried with a game.

## An agent's whole run without clicking: save, close, pause, run (user-decided, 2026-10-22)

An agent making projects and screenshots for many games drove everything
around the MCP server by clicking through the interface: the New Project
wizard, System > Pause, the Save As dialog, the question on closing
TAStudio. Those clicks were where its runs broke (issue 355). The owner
decided that all six requests are built, and that every Lua function is
offered through the server as well. This is the first part.

- `pause` and `resume`, as System > Pause and unpausing. `resume` is a
  change: TAStudio adds every frame played past the movie's end.
- `save_project` and `close_tastudio`. Nothing asks: a file that is
  already there is kept unless the agent gives `overwrite`, and unsaved
  changes are lost only with `discard_unsaved`. The windowless server's
  `save_project` follows the same rule. No branch is saved before either:
  they change no input, and a branch saved first was itself an unsaved
  change, so a project closed first thing could never close.
- `server_info`: the build, and the folders for projects, cores, firmware
  and Lua. The agent ran several nightly builds side by side and could not
  tell which folder a project would be saved in.
- `status` also gives the controls' names, the project's files with their
  paths, the speed measured, and each firmware file in use with its path
  and where it was found. A file chosen once for a core comes before the
  Firmware folder, which is how the agent's Saturn ran a BIOS from an old
  folder unseen.
- The tick box "Allow the agent to make changes" is remembered, which
  reverses the decision of 2026-10-12 that it is off at every start. The
  server itself still starts stopped.

A tool now belongs to the window, to the windowless server, or to both,
and is listed only where it exists. Tested: engine tests 30 of 30 (the
lists of both kinds of server), 8 tests of the new tools in the window, 2
of the remembered tick, the firmware store's tests saying where each file
was found, and the end-to-end test: in the real window an agent saves,
refuses to replace another file and then replaces it, runs and pauses the
machine, and is refused a close with unsaved changes before closing with
`discard_unsaved`. Each test failed with its fix taken out.

## Buttons and pictures on the machine's own schedule (2026-10-22)

The second part of issue 355. An agent pressed Start with `set_buttons`
and took pictures with `screenshot`, one call at a time, so its schedule
ran on its own clock: presses meant for 10 s and 20 s landed at 14 s and
23 s, and games never left their title screens. Four runs saved only black
or one-colour pictures, which nobody saw until afterwards.

- `press_buttons` takes the whole schedule in one call. The engine writes
  it into the movie's input as one step of the undo history: each press on
  its frames, buttons not named as they were. It is the input, so it plays
  the same way every time. Its lines are built by the same code as
  `set_buttons`' now.
- `capture_series` plays to each frame of a schedule, counted in frames or
  in seconds of the machine's own time, and takes a picture there. The
  engine plans the frames and takes the pictures; the window only plays to
  each frame, one seek at a time, so each picture is of a frame that was
  drawn. Past the movie's end empty frames are added first, as one step.
  Pictures can also be written to a folder at their own size. A file that
  is already there stops the series before anything is played, since
  nothing asks.
- Every picture, and `screenshot`'s, says whether it is blank: its
  commonest colour and how much of it that colour covers, blank at 99% or
  more.

Tested: 1 host test (frames added past the end, in one step), and the
end-to-end test in the window and without one: presses on their frames
and nothing else, `after` counted from the frame the machine is on, three
pictures on frames 5, 15 and 25, half a second as 30 frames, files written
at the picture's size and not replaced, frames added past the end and taken
back by one undo, and the commonest colour of a picture checked against
its pixels. Each failed with its fix taken out.

## An agent makes a project as the New Project window does (2026-10-22)

The third part of issue 355. An agent made each project by clicking
through the New Project window: choosing a core from a list by its place,
removing the last project's game, finding buttons that moved with each
core's pages. Every run it skipped was firmware it found out about only by
reading the firmware page.

`create_project` runs that window's own code, without showing the window.
The agent's answers go in the way a reopened project's do, and the pages
are passed as a person who changed nothing else would leave them: the
core's suggested settings for the game are applied on the settings page,
then the agent's own settings over them; the firmware page looks where it
always looks (the Firmware folder, and files chosen for the core before),
plus a folder the agent names. The project is then made by the same code
as the window's Create, and opened as one made by hand is, and the
firmware it found is remembered as the window remembers it. A second copy
of the wizard's rules was the other way, and it would have drifted from
the window the first time either changed.

What the window would ask, the tool answers instead. Missing firmware makes
no project: the answer lists each file missing, the files that would do
(name, size, hash), and the folders looked in. A core that compiles a
game's code before its first run is refused when that has not been done,
since the compiling is the window's last page; the next project of that
game finds the compiled code. A project open with unsaved changes is
replaced only when the agent says those may go, and only once the new one
is made.

`list_cores` and `describe_core` work in the window too, over Chimera's own
cores folders, and `list_cores` says which kinds of file each core opens.
The windowless server keeps `open_game`, which takes firmware by path and
asks the core for nothing.

The synthetic test core's package now declares its file slot, as every
real core's does: without one the New Project window cannot make a project
of it.

Tested: 4 tests of the window made for an agent (the project it makes, a
missing Boot ROM listed with the folder looked in, a machine chosen by its
name, what the core does not have named), 4 of the tool's arguments and
answers, and the end-to-end test in the real window: the cores listed with
their kinds of file, a project of the synthetic core made, opened on
frame 0 with an empty movie, and closed. Each failed with its fix taken
out. Not tried: a core that suggests settings for a game, or that compiles
a game's code first; no test core does either.

## Every Lua function through the MCP server (user-decided, 2026-10-22)

The owner asked, with issue 355, that every Lua function be available
through the MCP server. Three tools do it, in the window, where Lua runs:
`lua_functions` lists the functions as the Lua Console documents them,
`run_lua` runs Lua there and answers what it returned and printed, and
`lua_script` starts or stops a script file as File > Open Script does. A
list of functions kept by hand beside the libraries would have drifted
from them; these read the libraries themselves, so a function added
tomorrow is offered with nothing changed here.

`run_lua` runs the agent's Lua as a script runs: in a coroutine of its own,
with the functions that wait for a frame refused, and what it returned read
back. Lua's own `load`, called inside the coroutine, decides whether the
text is an expression; it answers a failure instead of raising one.

`run_lua` can do anything a script can, so it is refused until changes are
allowed; `lua_functions` only reads. The code the agent runs is shown in
the Lua Console's output as `[agent] ...`. The windowless server has no
Lua: its machine runs in chimera-mcp's own process, which has no Lua
Console.

Tested: 3 tests of the tools (the list by library and by words, results
and prints and errors, a script started and stopped), and the end-to-end
test in the real window with the bundled Lua: the functions listed, values
returned and printed, a statement and then a call that reads what it set,
an error and emu.frameadvance refused with Chimera running on, and a
script file started and stopped. Each failed with its fix taken
out.

## The Firmware folder is searched with its sub-folders (2026-10-24)

The Firmware folder named in Config > Paths was searched only at its top,
while Scan Folder searched a folder and its sub-folders. Firmware sorted into
folders a company and a system (Firmware > Sony > PlayStation) was found by
Scan Folder and not by the window opening on the same folder (issue 359).
Every search of the Firmware folder now walks its sub-folders as Scan Folder
does, to the same 10,000 files at most: the index the Firmware window, the
New Project wizard and a project's opening answer from
the lookup by a declared name, and the window's "in the Firmware
folder" for a file found below it. Test:
`TheFirmwareFolderIsSearchedWithItsSubFolders` (fails with the walk kept to
the top). How deep a search goes, as a setting, is one of issue 368's
requests, put to the owner.

## Sublime Text registration writes Chimera's own file (user-decided, 2026-10-24)

Lua Console > Settings > Register To Text Editors > Sublime Text 2 wrote
Chimera's Lua functions into Sublime's Lua package folder without making the
folder. Sublime makes that folder only when the package is first changed, so
on a fresh install the click failed with an unhandled error. The file was
also still named `bizhawk.lua.sublime-completions`. The owner chose to fix it
rather than remove the menu: the folder is made when it is missing, the file
is now `chimera.lua.sublime-completions`, and a write that still fails (no
permission) says so in a message. A file of the old name is left alone: it
may be BizHawk's own. Tests: `LuaAutocompleteInstallerTests` (the first fails
with the folder no longer made).

## The piano roll's frame numbers take the "Frame# Column" colours (user-decided, 2026-10-24)

TAStudio's Set Colors has "Frame# Column" and "Frame# Column (Lag)". They
coloured only the Markers list's frame column; the piano roll's frame column
laid a light wash over each row's colour, so the two colours seemed to do
nothing there. The owner chose to have the piano roll use them too: a frame
the greenzone holds shows its frame number in the "Frame# Column" colour, or
"Frame# Column (Lag)" for a lag frame. The current frame, a marker's frame
and a frame not run as it stands keep what they had: the marker colours, and
the wash over the row's colour. The theme preview draws its sample piano roll
the same way. Tests: `TAStudioFrameColumnColourTests` (the first fails with
the wash kept everywhere).

## Loading a branch offers to update the current one first (user-decided, 2026-10-24)

A branch changes only on Add or Update; Save Project saves the movie and the
branches as they are, and loading another branch replaces the movie. A person
who drew inputs after loading a branch, saved the project and loaded another
branch found the inputs in neither (issue 360), having expected Save Project
to update the branch they were on. The owner chose to leave Save Project as
it is and ask at the load instead: when another branch is loaded by a person
(the Load button, the menus, a double click, a hotkey) and the movie holds
inputs or markers of the person's that the current branch does not, TAStudio
asks whether to update the current branch first: Yes updates it and loads, No
loads without updating (Undo Branch Load still brings the changes back),
Cancel loads nothing. Loading the current branch again does not ask: that is
how a person goes back to it. A Lua script's or an MCP agent's load does not
ask either. Whether the movie differs is the movie's question (the input log
through the engine's `ce_movie_log_divergent_point`, then the person's
markers; where the machine is does not count). Tests: two of the movie's
answer (they fail with the answer always "no change") and one of when it
asks (it fails when loading the current branch again asks too). The question itself, and the update it makes, are not
driven by a test.

## File hashes are kept between sessions (user-decided, 2026-10-24)

Chimera knows a file by its SHA-1, so every Firmware folder rescan, New
Project wizard and project opening hashes the files it looks at. The engine
kept those hashes only until Chimera closed, on purpose ("a stale entry
surviving a restart is a bug nobody could see"), so the first rescan of each
session read every disc image in the Firmware folder again, which is what
made Rescan slow (issue 368). The owner chose to keep them on disk.
`ce_sha1_keep_in` names the file (Chimera's is `file-hashes.txt` in its data
directory) and reads its hashes back; every hash taken after it is appended,
a line a hash. The key is the one the session cache already used, the path
and the file's whole identity: size, modified and changed times to the
system's resolution, volume and file index. A file edited, replaced, copied
over or moved in is another file and is hashed again. A file whose system
gives no change time or no file index (some network and FAT volumes) is kept
for the session only. A line that does not read as one is passed over; a
file of many more lines than files is written again with the newest of each.
Test: test_sha1's kept-between-sessions part (fails when the kept hashes are
not read back). Not measured: how much faster a real Firmware folder's first
rescan is.

## The Firmware window finds, folds and opens folders (user-decided, 2026-10-24)

Config > Firmware lists every file every installed core asks for, and the
list is long (issue 368). Of the five requests, the owner chose three for
the window, and keeping file hashes between sessions (above) for Rescan's
speed. The window now has:
- a Find box above the list. A row stays when its core, the firmware's name,
  id or release, the file name a core asks for, or the file found for it
  contains what is typed, in any case; a core with nothing that matches is
  not listed;
- folding a core's files to one line, "+ PCSX2: 3 files", which names the
  core because not every toolkit draws the group headings.
  A double click or the + key opens it, the - key folds it, and the
  right-click menu folds or opens one core or all of them. Folding lasts
  while the window is open;
- Open Folder on the right-click menu of a row with a file found: the
  file's folder, the file selected where the system can (Explorer), as
  Encode Video's Open Folder does.
Not done, by the owner's choice: a sub-folder depth setting (the Firmware
folder is searched at any depth since issue 359) and rescanning one system.
Tests: `FindNarrowsTheListToWhatMatches`, `ACoreFoldsToOneLineAndOpensAgain`,
`OpenFolderShowsTheFoundFileInItsFolder` (each fails with its feature taken
out), and `TheWindowInEveryTheme`, the window's first picture test, which
caught the Find box drawn over its own label.

## A state carries the GL names its machine holds (user-decided, 2026-10-24)

After a load the GPU bridge gives the guest a fresh context and takes every
GL name the guest still holds again, empty, before the guest runs, so no new
object can get a number an old call could still reach. The engine knew those
names only by watching the guest make them in this process. A state taken by
another process, which is what opening a project again loads, held names the
new process never saw: Dolphin's rebuild deleted a vertex array by its old
number, the array another vertex format had just been given, and Mario Kart
Arcade GP's grey screen drew black (issue 367, fixed in Dolphin by patch 0032).
Any GL core whose rebuild deletes by old numbers could meet the same. The
owner chose the general fix in miniBox: the generated GL wrappers of the calls
that hand out names keep the highest name of each kind in the guest's own
memory, so every state carries it, and tell the engine where it is
(`GL_OP_NAME_MARKS`, miniBox 96762cf). After a load the engine keeps every
number up to those marks from new objects (`reserveNames`, gl_bridge.cpp),
besides the names it saw made. A core gets this when it is built again against
miniBox 96762cf or later. Tests: test_gl_session's "state another process
took" part (a new texture is 21 and new vertex arrays 7 and 8 when the state
held up to 20 and 6; fails with the marks ignored), and a Dolphin core built
with the new generator and the old stale delete put back passes the
gl:load-in-new-process leg.


## A project chooses when the machine's clock starts (user-decided, 2026-10-24)

Every machine's clock started at the same second, 2017-05-27 12:44:28 UTC,
whatever the core: miniBox answers the time from a constant, so a movie
replays exactly. A game that takes its luck from the date and time then
deals the same luck every time; Bubble Suite in FreeJ2ME-Plus always lays
out the same board (issue 366). The owner chose one setting for every core,
kept by the sandbox, over a FreeJ2ME setting. A project may now name the
second the clock starts at: the New Project wizard asks for it under the
settings ("The machine's clock starts at", UTC, the sandbox's own unless
changed), the project keeps it as `clockStart` only when it is not the
default (so a project at the default still opens in an older Chimera, and
one with another start is refused there rather than run at the wrong time),
and the engine tells the sandbox before the core first runs
(`wbx_set_clock_start`, miniBox; `ce_project_clock_start`). It is part of
the machine: cached states of one start are not used for another. Absolute
times a guest sleeps or waits until are on the same scale. A core whose
clock is its own rather than the sandbox's does not follow it yet (Ruffle's
virtual date, an emulated real-time clock set from elsewhere). Tests: the
witness's E:project:clock-start (a synthetic rom ending in CLOCKTEST keeps
the second its clock reads; the project naming 2030-01-02T03:04:05Z reads
1893553445 and one naming none reads 1495889068), test_project's clock part
(the text read and written, the default left out, a bad time refused),
miniBox's conformance-v3 (the guest reads the start it was given) and
WizardClockStartTests (the wizard's box writes the project's start and
refuses a time it cannot read). Each failed once with its part taken out.

## Three options removed from Customize (user-decided, 2026-10-24)

Config > Customize lost three options that came from BizHawk. "Skip waterbox
integrity checks" set a flag that nothing in Chimera read, so ticking it did
nothing; it went, with its note and an older note on the same tab ("Will
prevent many Out Of Memory crashes...") whose checkbox was already gone. "Run
in background" and "Pause when menu activated" went together, by the owner's
choice ("we'll re add them if a user needs them"). Asked what Chimera then
does, the owner chose that nothing pauses the game: not opening a menu of the
main window, its right-click menu or a tool window, and not the window losing
focus. Before, a menu paused the game by default, and unticking "Run in
background" alone did nothing unless "Pause when menu activated" was ticked
too. Tests: GuiOptionsTests (the three are not in the window; no checkbox on a
tab lands on another control; the window in every theme), each failed once
with a checkbox put back.

## Frame skip is gone; Turbo's drawing rate is a setting (user-decided, 2026-10-24)

Chimera's Frame Skip (Config > Speed/Skip: Skip 0, Skip 1..9 and
Auto-minimize skipping, from BizHawk) is gone, with Lua's client.frameskip and
emu.minimizeframeskip. The owner's words: "a legacy configuration that used to
make sense for old NES games. The systems we handle no longer can separate
their rendering from their work." The throttle's automatic skip controller went
with it; it was also what moved the throttle's timetable after a slow frame
(issue 346), and nothing moves it now.

One part stays, as a setting. Running unthrottled - Turbo held, or Toggle
Throttle - drew one frame in three, by the frame skip's own counter. A frame not
drawn still saves copying the picture back from the graphics card on GPU cores,
so the owner chose to keep it and make it configurable, one setting for both:
Config > Speed/Skip > Turbo/Unthrottled Draws, Every Frame or 1 in 2 to 1 in 10,
1 in 3 at first (Config.UnthrottledDrawOneIn). Fast Forward was never part of
this: it runs at the alternate speed on the clock and draws every frame. (The
first question put to the owner said otherwise, and was asked again.)

Tests: ThrottleTests (unthrottled draws one frame in the number chosen, throttled
running draws every frame, a slow frame leaves the timetable alone); the first
failed with the unthrottled rule taken out.

## An input line is read from its first bar (2026-10-24)

TAStudio's Copy writes each frame's line after its frame number when "Include
frame numbers when copying input" is on ("00002 |..U.....|"), and Paste read
the digits as buttons held. Every reader of a line now reads it from its first
'|': whitespace and the digits 0-9 in front
are dropped, and anything else in front is left for the reader to refuse. So a
numbered line, one whose number is followed by a tab or by nothing, and an
indented one paste as the line - and so does such a line given to Lua's
joypad.setfrommnemonicstr. Paste puts the lines one after another from the
first frame picked, whatever the numbers say; a line that cannot be read stops
the paste and leaves the last Copy's record (Reselect Clipboard) as it was. Cut
writes the frame numbers as Copy does. Found in review of the first fix
(630186d), which only handled a number followed by spaces, in TAStudio alone.
Test: MnemonicLineTests (failed with the reader's change taken out).

## A core stuck in its own fault handler is stopped on Windows too (2026-10-24)

On Windows the frame watchdog (the session's timer that stops a core whose
frame runs past 20 seconds) never stopped Vanquish on RPCS3's Vulkan
renderer: Chimera hung for 40 minutes, where Linux stopped the same run
after 20 seconds (issue 363). The core was not working. It looped inside
its own fault handler, the function a core gives miniBox to hear about
writes to memory it watches. On Windows miniBox calls that function from
inside its own exception handler, and a stop from another thread was
refused while any fault was being handled. Linux runs the core's handler
after its own has returned, so the stop landed there.

miniBox 7216594 counts the calls into the core's handler, and stops a
thread that is running the core's code in there: it sends it to a trap that
leaves every handler at once, as a crash inside the handler already did. A
death inside the handler also left the count of faults being handled one
too high, which kept every later stop from landing; it is put back now.

Test: a new check in miniBox's threads test (a guest looping in its own
fault handler, stopped from another thread twice, then run again after a
state load) failed on Windows with the previous miniBox ("not stopped after
200 asks") and passes with this one; it passes on Linux. miniBox's CI runs
it on its Windows runner from now on.

## An open project's settings can be changed (user-decided, 2026-10-25)

Issue 370: trying a renderer or an anti-aliasing mode on an existing run
meant a new project for each, because Chimera refused every change of
settings once a project was open. docs/project.md already described the
change ("Editing"); nothing offered it.

File > Project Settings opens a window of the machine's settings: the ones
the New Project wizard shows for the project's files, plus the renderer
(the wizard asks it on its first page) and the start of the machine's clock.
The machine itself (the System box of a core with several) is not there,
because it decides which files the project takes. Restart with these asks
once more, then the new settings become the project's (what a boot reads)
and the movie's (what a save writes), and the machine restarts at frame 0
through the same path as Reboot Core. The inputs are kept; the greenzone
goes with the old machine, and a greenzone saved with the old settings is
not used for the new ones (the settings are part of the machine's identity
the cache is checked against). When the machine does not start with the
new settings, the old ones are put back and it starts with those again.

Not done: a setting changed at a chosen frame, kept in the movie. That needs
every core to apply settings while it runs, a declaration no core has.

Tests: ProjectSettingsFormTests (the renderer is shown and the machine is
not; the project's files decide what is shown; the game's own settings are
last; a restart is offered only for a change, the clock's start included)
and a test that changed settings are the next boot's and the next save's.
Each failed with its change taken out.

## A game runs only in a project (user-decided, 2026-10-26)

The owner: "Both GUI and command line should provide a project file, if a
game needs loading. This unifies the path to execution. Remove all code
enabling the open rom without project pathway." Asked whether that covers
chimera-run, the headless runner every core's tests use: "Yes, everywhere.
Everyone using Chimera needs to be able to create a project file that
provides the full settings and files required for execution."

Chimera's menus already open only projects, but paths that open a game file
alone were left over: a game named on the command line or handed over by a
second start in single instance mode, Lua's client.openrom, a movie opened
with no game running (which asks for a game file), the Batch Runner (hidden,
but still there), and chimera-run's `<package> <game> <movie>` form, which 50
of the 61 cores' tests call. What only those paths used goes with them: the
default core per system (DefaultCores) and the core settings kept for games
opened alone (CoreSettings). The owner also decided, setting by setting, to
remove the movie options that act only on movies without a project (what
happens at a movie's end, backup movies, VBA-style state loads) and the
compression level of Chimera's cache file.

The order keeps every published thing working between steps: chimera-run
first learns to make a project (this section's change), then Chimera's
interface loses the leftover paths, then each core's tests move to projects
one repository at a time, and only then does chimera-run's game-file form go.

`chimera-run --new-project <out.chimeraProject> <package> [<game> [<movie>]]`
makes a project and runs nothing. The core is the package's, pinned by its
hash; a game goes in the slot that takes its kind of file (`--file
<slot>=<path>` names the slot, once a file); the settings are every one the
package shows for those files, at the value `--settings` gives or else the
package's default, which is what the engine runs a game with (a machine's own
narrower defaults, which New Project offers, are given with `--settings`); a
`--firmware` file is pinned by its hash; `--clock-start` and `--title` are
kept; the movie is the project's input (named after the game, or with
`--movie <movie>` for a core that takes no game file, such as a game core
whose game is its firmware). Files that do not fit the package's
slots are refused and no project is written. A game of several files is given
a file at a time, as a project holds them, not as a `.chimeraMultiFile`
descriptor.

Tests: the witness's engine level makes each of its movies into a project and
runs it, to the same RAM and VRAM as the goldens, and checks a setting given
(the same RAM as the game run with that setting), the default recorded for one
not given, a firmware pin, a clock start, the slot chosen by the kind of file,
and the two refusals: 78 passed, 0 failed. With each part of the maker broken
in turn, its test failed (8 failed). The Windows build made the same project
from Windows paths.

Added the same day, for the cores whose tests run a movie with no game file:
`--movie`. Its test makes a project with the movie named that way and
compares its input with the project made from the movie after the game: 81
passed, 0 failed; with the option read and dropped, that test failed (80
passed, 1 failed).

## Chimera's interface opens a game only in a project (user-decided, 2026-10-26)

The second step of "A game runs only in a project" (above): Chimera's own
interface loses every way to run a game outside a project.

- A game file named on the command line, handed over by a second start in
  single instance mode, or dropped on the window starts New Project from it.
  A project file named the same way opens. With no window (`--headless`) a
  game file alone is refused with a reason and the process ends with code 2.
- Gone: the Open ROM dialog (reached only from a movie opened with no game
  running), `--movie`, the recent games and recent movies lists with the
  movie auto-load, Lua's `client.openrom` (`client.openproject` stays), and
  the Batch Runner, whose menu item was never shown.
- Gone with them, as only games outside a project used them: the core
  remembered per system (`DefaultCores`; a project names its core, and the
  loader already takes it from the project) and the core settings kept in
  Chimera's settings file for such games (`CoreSettings` for the cores; the
  idle screen's own settings stay).
- The movie options the owner removed setting by setting: what happens at a
  movie's end (TAStudio, which every project has open, always skipped it),
  backup movies and VBA-style state loads (both acted only on a state loaded
  into a movie without a project, a path with no caller left), and the
  compression level of the project's cache file, which is now always 2, its
  old default.
- The taskbar's jump list (Windows) lists projects, not games.

One game still runs outside a project: a compile session (`--precompile`),
which Chimera starts in a process of its own to compile a game's code before
the project is made, and which shows nothing and ends when it is done. It
moves to a project with the last step, when chimera-run's game-file form goes.

Tests: the witness's frontend runs that opened a game file now open a project
made with `chimera-run --new-project` (the replays, the picture-room run, the
settings run, which puts its setting in the project instead of Chimera's
settings file, the movie run and the keybinds run), and the run that named a
game alone now checks that it is refused: 115 passed, 0 failed. With the
refusal taken out, that check failed. Interface tests: 1215 passed, 0 failed
(four tests of the per-system core went with it; the pinned shape of the
movie settings lost the four options).

## chimera-run and the questions before a project take only projects (user-decided, 2026-10-26)

The last step of "A game runs only in a project" (above). Every core's tests
now make a project with `chimera-run --new-project`, so the rest of the ways
to run a game without one are gone.

- chimera-run takes a project or nothing. `chimera-run <package> <game>
  <movie>` is refused with a sentence that says how to make one, and so is a
  `.chimeraMultiFile` descriptor given as the game (a game of several files
  is a project of several files). `--settings` belongs to `--new-project`
  only: a project holds its settings.
- The engine no longer offers a call that opens a machine from files alone:
  `ce_session_open` is internal, and `ce_session_open_project` is the one
  way to a machine. `ce_suggest_settings` and `ce_import_movie` became
  `ce_project_suggest_settings` and `ce_project_import_movie`.
- The three questions asked of a core before its project is final are asked
  of a project made from the choices so far: the suggested settings
  (`--suggest`), a compile session (`--precompile <i>/<n>`, new in
  chimera-run) and a movie import (`--import-movie <movie>`, with `--mount`
  for files that have no slot). A question is not refused for a slot the
  person has not filled yet; a run still is.
- Movie import runs on a project too (user-decided, 2026-10-26; the
  recommendation was to keep the movie and its files, since the core's
  answer is what says where each file goes). The files whose import input
  names a slot go in the project, the import options are its settings, and
  a file the core may take as firmware is mounted beside the movie under its
  name, as only the answer can say which firmware it is.
- Chimera's own helper processes (`--suggest-settings`, `--import-movie` and
  `--precompile`, which the New Project window and the import dialog start)
  each make that project from what the window wrote down. A compile session
  is now one of those helpers: it opens the project's machine, compiles its
  share and ends, with no window and no settings file, so the window no
  longer saves the chosen firmware into its settings before it starts the
  sessions, and no longer tells them its settings file. The last ways to a
  machine without a project went with it: the loader's game-file path, the
  platform and archive choosers, and `--precompile-slots`.
- The synthetic core gained a compile session (six modules, a share a
  session) and a `SuggestSettings` answer that names the game it was given
  and the fill byte its settings hold, so both are tested without a
  commercial game.

Tests: the witness's engine level runs on made projects (55 runs moved
through one helper), and its new tests check that the suggestion and a
compile session are asked of a project (6 of 6 modules from two sessions),
that the import is asked of one with a file mounted by name, and that a game
file alone is refused: 84 passed, 0 failed. With the question's slot
exemption, the compile request and the refusal's words each broken, those
four failed. The frontend level checks Chimera's three helper processes
against the synthetic core: 30 passed, 0 failed; with the project builder
dropping the settings and the compile session losing its request, that
check failed for all three. The whole witness: 122 passed, 0 failed.
Interface tests 1,212, 0 failed; engine unit tests 30, 0 failed; the
Windows build compiles. The hand-run GPU reopen
check and the Windows refused-open check were moved to projects and compile;
neither was run here. `tests/perf/seekbench.cpp` was moved too, but it did
not compile before this change (it calls two state-history functions that
no longer exist), and still does not.

## The project hotkeys are named for projects (user-decided, 2026-10-26)

A game runs only in a project, so the hotkeys Open ROM, Close ROM and Load
Last ROM are now Open Project (Ctrl+O), Close Project (Ctrl+W) and Load
Last Project, with the menu and the picture's right-click menu saying the
same. The window that is to replace Chimera's takes the same names. A
binding a person saved under an old name is read as the new one when the
settings load, and the old name is dropped, so nobody loses a key they
chose.

Tests: a new hotkey test feeds the three old names and checks that each
binding (an unbound one too) comes back under its new name; with one
renaming taken out it failed. Interface tests 1,213, 0 failed.

## The window's review before it replaces Chimera's (user-decided, 2026-10-26)

A last review of the Qt window's view models, read beside Chimera's own
code, before the window takes Chimera's place. The owner asked for every
finding to be fixed, each with a test seen to fail once; three were the
owner's to decide:

- **The window is named "Chimera"** (user-decided): its settings and
  caches are kept under Chimera's name, and its single instance is found
  by Chimera's own mutex and pipe names. What it kept under its old name
  stays there; nothing is moved.
- **Open Project, Close Project, Load Last Project** (user-decided): the
  hotkeys Chimera called Open ROM, Close ROM and Load Last ROM are named
  for what they open now, in Chimera and in the window. A binding saved
  under an old name is read as the new one.
- **Movie import runs on a project** too (recorded in the section above).

What was wrong, and is fixed:

- A project's open told the window the machine was open, and only in a
  later step which project it was. Anything done in between met no project,
  and recording turned on in that moment was turned off by the project's
  arrival (it starts every project read only). The project is now known in
  the same step, just before the machine is said to be open. This was seen
  as tests that failed only while the machine was busy with other work.
- TAStudio: the last project's undo history and selection stayed after
  another opened; a run count too long for a number ended the program
  ("On x99999999999999999999"); a marker set was not a step of the undo
  history; the row past the movie's end, which Chimera's shows empty, was
  missing; painting and filling had no limit on how many frames.
- Branches: taking back a removal left the "current branch" mark on the
  wrong branch. Macro Tool: zones outlived the project they were made in,
  and a zone set after a removal could take the name of one still there.
- Lua, as Chimera's library answers: booleans print as True and
  False, numbers as Chimera writes them; joypad.set takes 1 and 0, and false
  lets a button go even when a key holds it; emu.getsystemid gives the
  system's id; a register that is not there reads 0; the bit operations
  work on 32 bits with shifts modulo 32; client.openproject says false for
  a file that is no project; movie.getinput gives nothing past the end;
  emu.frameadvance in an event callback is an error, not a hang.
- Numbers: the settings file's reader took text such as "1-2" for a
  number, and wrote and read numbers by the system's locale, so a decimal
  comma broke them; a whole number too big for 64 bits ended the program;
  NaN and infinity were written as text no JSON reader takes.
- Pre-Compiled Modules / Cache Manager: a cache folder with nothing in it
  counted as the most recently used one. The cause was the file clock,
  which counts today as a negative number, so "0" for "never" was the
  future. It now keeps the least time.
- Windows: the program declares UTF-8 as its code page (chimera.manifest),
  and the trace logger opens its file by its wide name, so a path with
  letters outside the system's code page opens. Not yet tried on Windows.
- The hex editor's File on Disk mode, which could not write what it
  showed, is gone; the RAM search and watch follow the domain and the
  machine; the network check covers the window's sources too.

Tests: 2,749 view-model checks and the window's own tests, 39 of 39
passed. With the fixes undone, the new checks failed (the JSON grammar,
the hotkey names, the project known at open, the empty last row, marker
undo, the run count, the history after another project, the zones, the
Lua lines, the empty cache folder, the branch mark); with the number fixes
undone the test program ended on the oversized numbers, the bug itself.

## The window takes Chimera's command line, drops and stopped core (2026-10-26)

The rest of the review's findings, where the window did less than Chimera:

- **Dropped files open by their kind**, as Chimera's do: a game starts New
  Project, a watch list opens in RAM Watch, a Lua session or script in the
  Lua Console, a project opens. Several files of one kind open none, and a
  state or cheat file does not open by dropping; both are said. Before, any
  file dropped started New Project, a project file too. The sorting is in
  the view model (sortFiles), so it is tested without a window.
- **Chimera's command line**: --project, --core, --config, --firmware,
  --fullscreen, --chromeless, --luaconsole and --version, beside the
  window's own (a project and its package named). Three files, or a second
  file that is not a package, are refused. --config naming Chimera's own
  settings file is refused: the window would have set it aside as
  unreadable or written over it. --firmware gives a file for this run, used
  by its hash where the project pins one, else as given, as Chimera's.
  --headless, --audiosync and --dump-* are not taken yet.
- **A second start hands over** the project and the package named after
  it; the package was dropped before. As in Chimera, the first file opens
  and the window is not raised.
- **The core stopped** is asked as Chimera asks it: the frame, what the
  sandbox said, the frozen addresses switched off, and four ways on - back
  to the latest safe point, run again from frame 0, save the inputs and
  close, or close without saving with the work kept against a crash. The
  window only said it in the status line before.
- A test of it found that opening another game left the last game's
  freezes in the window's list (the machine's were cleared, but the count
  that decides whether to tell the window was taken after clearing). It had
  passed only because the dead machine had dropped the freeze already.
- Found to match Chimera already, and left: "(ported)" on every core's
  tooltip (every Chimera core is a ported one), and the single-instance
  hand-over opening only the first file.

Tests: 40 of 40 passed, 2,760 view-model checks, and a new command-line
test (9 checks). Breaking the drop sorting, the run's firmware, the
--config refusal, the --show message, the stopped state and the freeze
fix each failed its check.

## The release opens no console, ships a cores folder, and Export Core Log is in Help (user-decided, 2026-10-27)

The owner tried the window's Windows release and asked for three changes.

- **No console window in a release.** chimera-qt.exe was a console program,
  so a black console window opened beside the window. A release is now a
  window program (meson's win_subsystem, from the build's debug option); a
  debug build keeps its console. Started from a console, a release still
  writes there (--version, --help, what it refuses): it joins the console it
  was started from, as Chimera does, and output sent to a file or a pipe
  stays where it was sent. The programs it starts (chimera-run, ffmpeg)
  were already started without a window of their own.
- **A cores folder.** The folder tools/stage-qt-windows.py makes now has an
  empty cores/ (the window's default place for core packages) with a
  README.txt that says what goes in it and where to get it, since Chimera
  ships no core and downloads none.
- **Export Core Log moved to Help**, beside Report an Issue, which asks for
  that log; the divider over it in Tools went with it. It has an icon now, a
  log page with Chimera's bug, drawn by tools/draw-core-log-icon.py, and the
  tick that showed the log was kept became a green dot on the icon, as MCP
  Server's icon shows its server running. A row with a tick has no icon in
  Chimera's menus. The README, the bug report form and docs/issue-intake.md
  name the new place.

Tested: 40 of 40 tests on Linux. The window test fails on a missing icon:
naming the new one wrongly failed it. The Windows program is now marked a
window program; --version and a refusal still print through a pipe. Not
tried: a release started from an open cmd window (the check was not run
here), and the icon with the log on, which no test shows.

## Scroll bars always seen; the clock start is one of the settings (user-decided, 2026-10-27)

- **Scroll bars.** Fusion's scroll bars show only while they are used and
  fade away after, so a long list of settings in New Project did not say it
  scrolled. The window's style (source/ui/qt/style) now draws every scroll
  bar, with its groove, all the time there is more to scroll to, in every
  window. Scroll views keep their content clear of their bars, which Fusion
  draws over the content's edge. Text that wraps never scrolls sideways: its
  width, taken before the vertical bar took its room, had shown a sideways
  bar for a few pixels. Configure Hotkeys now shows that its grid scrolls
  sideways, which it always did.
- **The machine's clock start** was a row of its own under the settings in
  New Project and Project Settings, seen whatever was scrolled. It is now
  the last row of the settings list, laid out as the others and scrolled
  with them. It is the project's, offered for every core, and no core has a
  row of its own outside the list.

Tested: 40 of 40 tests. A page drawn with the style (30 settings, the clock
row, wrapped text) and the Theme and Configure Hotkeys windows were
captured without the pointer over them: the bars are there, and seen in the
Dark theme. Not seen: AppleWin's own settings, which need its package and a
game.

## The program is chimera, on Windows and Linux (user-decided, 2026-10-27)

The window's program was chimera-qt (chimera-qt.exe on Windows). It is now
chimera and chimera.exe. What it says on the command line starts with
"chimera:". The library the Windows program loads from lib\ keeps its
name, chimera-qt-app.dll: no one starts it, and the launcher names it.

Tested: 40 of 40 tests on Linux, which run the program by its new name;
the Windows build makes chimera.exe, staged and started (--version).

## The window takes --headless, for runs nobody watches (user-decided, 2026-10-27)

The cores' tests run Chimera with no one watching: 55 of their workflows
start it with --headless. It does this:

- **No window is shown**, and it draws off screen (Qt's offscreen
  platform, chosen before Qt starts), so it needs no display: no display
  server on Linux, nothing on the desktop on Windows. The offscreen platform ships in
  the Windows folder for it.
- **What would wait for a person ends the run**, said on stderr in
  Chimera's words: any dialog (exit 64, its caption and text); a project
  whose files were not found (64, the files named); a script's file picker
  (64); New Project from a game (64). The window's style makes every
  dialog, so one hook in the style (style/Dialog.qml) covers them all.
- **The core stopped** ends the run with 65, the reason said and the work
  kept against a crash, as Chimera's.
- **Work a crash left** is said ("[recovery] ...") and the project opens as
  it was last saved; the recovered copy stays.
- It takes nothing handed over by another start, hands nothing over, and
  puts nothing in the taskbar's recent list.

Two differences from Chimera's found on the way, now as Chimera's in every
run: a script's client.exit leaves without asking, its unsaved changes let
go and said ("[tastudio] closing unattended with unsaved changes; the
project on disk is unchanged"); and client.invisibleemulation runs frames
undrawn for that run only, where Display Nothing is the person's
remembered setting.

The compile step RPCS3's test starts with Chimera's --precompile is the
window's chimera-run already (chimera-run --precompile), so the window has
no option for it.

Tested: a new test (headless, 7 checks): a project run with no display and
Qt told nothing, a script's unattended exit, a core stopped (65), files not
found (64), a file picker (64), and work a killed run left. Breaking the
offscreen start failed 6; breaking the other five behaviours together
failed 6. 41 of 41 tests. On Windows, a headless run of a project with a
script, from WSL, showed no window and ended 0.

## The window is Chimera's, and the cores' tests run on it (user-decided, 2026-10-27)

The owner decided this window is Chimera, published at once as the dev
build and the nightly, with every core's tests run on it (it gained
--headless for them).

- **What the window keeps of its own:** 71 images and the two built-in
  themes (`source/ui/qt/images`, `source/ui/qt/themes`), and the Lua
  functions' descriptions as a file (`lua_docs.inc`), edited by hand.
- **The package contract tests** are a C++ program, `test-packages
  [<folder>]` (18 checks on packages, and the rules on packages made up
  here), reading packages through the engine and the window's own readers.
  On the 44 packages installed here all pass; six broken copies of the
  synthetic package each fail the check meant for them.
- **A guest ABI check**, which the window lacked: a package built for a
  guest ABI this Chimera does not run is listed with why and not opened,
  in Chimera's words ("Update Chimera." or "Update the core.").
- **client.invisibleemulation** is not there (the cores' scripts call it
  inside pcall): the window draws every frame.
- **The witness's level B** runs the window --headless. Its keybinding check
  reads the bindings the machine has (controls.effective), since the window
  keeps only the keys a person changed. The helper modes (suggested
  settings, precompiling, movie import) are chimera-run's, which level E
  asks of a project (E:suggest, E:precompile, E:import-movie).
- **CI** builds the window with `tools/ci-chimera.sh` (Qt 6.12 from Qt's
  archives with aqt, cached); every core's CI does the same.
- **The release** builds Qt itself for both systems, without its network
  module, from Qt's source archives pinned by hash
  (`tools/fetch-qt-source.sh`, `tools/build-qt-linux.sh`,
  `tools/build-qt-windows.sh`; cached once built). The Windows bundle is
  `chimera.exe` with `lib\` and `bin\`; the Linux one is laid out as Qt
  installs itself (`chimera`, `lib/`, `plugins/`, `qml/`, `bin/`), so a
  relocatable Qt finds its parts with no settings file. Both carry
  `themes/`, an empty `cores/` with a README.txt, and Qt's LGPL texts with
  where its source is.
- **What the witness's level B found**, run on the window for the first
  time, each now fixed:
  - `client.reboot_core()` reboots at once: the script's next line sees
    frame 0. It used to queue the reboot, and the scripts were started
    again when the machine opened.
  - `client.openproject()` opens at once. Chimera's then never resumed the
    script that opened it; the window's goes on with the new project, as
    after a reboot (a person opening a project still starts the scripts
    again).
  - `joypad.getwithmovie()` is what the core was handed on the frame just
    run (the movie's frame, which the engine plays), kept by the machine as
    it plays it; it was the keys held.
  - A script's `emu.framecount()`, `movie.length()` and
    `client.encodingvideo()` read the machine's own state at the moment
    asked. They read the window's copies, which follow a moment later, and
    a script that asked just after a reboot, an open or an encode read the
    old value.
  - `game.describe().label` is the value as shown, always (a u64 as its
    digits, bytes in hexadecimal); it was there only for named values.
  - `event.oninputpoll` calls the function at each read of the controls a
    core reports (issue 344), and is refused with Chimera's words by any
    other core; the window refused it for every core.
  - A script's wait through an encode goes on on the frame the encode gives
    back, as on the frame a seek lands on.
  - A headless run given a game file alone ends with 2 and Chimera's
    words; a headless exit asked before the window's loop has started is
    no longer lost.
  - The witness reads the window's recovery folder, and the synthetic
    core's own "synth: started" for a boot.
  - A script named on the command line starts before the machine's first
    frame, as Chimera's: the machine waits for it. It ran three to five
    frames first.
  - `memorysavestate.loadcorestate()` no longer pauses the machine (a script
    loading one each frame waited for ever).
  - SIGTERM and Ctrl+C end the program again: SDL, started for gamepads,
    took them and turned them into a quit event nobody read, so a run could
    not be stopped by its time limit. SDL is told to leave them alone.
- **What the cores' own tests found**, run on the window:
  - A saved project carries the game's own timer at the end of the movie
    (the headers GameTimeMs, GameTime and GameTimeFrame, docs/game-cores.md): only when the machine has played to the end
    since the last edit before it, or the project said so when it was
    opened, and otherwise none. The window wrote none (the tests of rsdkv3,
    rsdkv4, rsdkv5 and SDLPoP).
  - `chimera.controls.effective()` lists every button with its keys, ""
    for none, as Chimera's settings did; of autofire only the buttons that
    have keys. It had left out a button the package ships with no key
    (rsdkv5's player 3 and 4 Y and Z, SDLPoP's Restart Game).
  - The controller is the one of the machine the machine setting chose,
    as Chimera's was. The window took the first machine with the session's
    system id, and MAME's Amstrad machines all have one: a GX4000 project
    had the CPC464's controls. The engine now says which machine it chose
    (`ce_session_machine_index`).
  - The keys a package ships are read from the section named for the
    machine's controller, as Chimera read them, and only when there is
    none from the section that names most of its buttons. The window
    always took the second way, so of two machines with the same button
    names the first won: the GX4000's Pause had the Atari 7800's keys.
  - A movie the machine cannot play (an entry that does not read) ends a
    headless run with 65 and why, as a core that stops does. The window
    stopped the machine and said so in the status bar, and a run nobody
    watches waited for ever (mamehome's test, on a project made with no
    panel).
- **By the owner's rule that the engine plays the movie** (2026-10-22):
  past the end of a project's movie and not recording, a script's
  `joypad.set` does not reach the core, so what the core plays is always
  what the movie holds. A script that drives the machine records
  (`tastudio.setrecording(true)`), as the witness's replay does.

## Chimera describes only itself (user-decided, 2026-10-27)

The owner asked that the code and the documents describe Chimera as it is,
with nothing left of the program it grew from. What that changed beyond
words in comments and documents:

- **No settings import.** The window no longer reads an earlier program's
  settings file, at its first start or through Load Config From (owner's
  choice over keeping that file's key names); a person sets their settings
  again. Its test went with it.
- **Script-visible names.** `client.get_lua_engine()` returns "Lua".
  `client.getconfig()` calls three settings `StayOnTop`,
  `MouseCaptureForcesTopmost` and `StaticWindowTitleOverride`. The Lua
  Functions List gives types in Lua's words (integer, number, boolean,
  table, function, any, color).
- **Saved projects.** TAStudio's part of a project no longer carries a type
  name; one an older project has is dropped when it is saved again.
- **Two libraries nothing used** are no longer built or shipped: the ImGui
  bindings (`extern/cimgui`) and a hashing library (`extern/LibChimeraHash`).
- **Documents** say what the window does: docs/theming.md was rewritten
  for how themes are read now (one folder, no `basedOn`, a missing colour
  is the window's own), and two documents about code that is gone were
  removed.

Tested: meson test, the end-to-end test and the Windows cross-build on the
change; a test of the saved project's TAStudio part (no type name, an old
one dropped), and the Lua Functions List's types, updated.

## NOTICE says what still comes from BizHawk (user-decided, 2026-10-27)

NOTICE said that the BizHawk team's frontend and its libraries remain in
Chimera. They do not: the code has been written anew. The owner asked for
that statement to go. What Chimera does keep from BizHawk's MIT-licensed work
is named instead - the Lua functions' names and descriptions (`lua_docs.inc`
holds BizHawk's own words for them), the format of a movie's input, and the
names of keys and controls - and the MIT License requires the BizHawk team's
copyright notice to stay with those parts, so LICENSE keeps that line,
limited to them. The vendored test suite NOTICE also named is gone from the
tree, and so is its line. CREDITS.md, the README and the bundle's
LICENSES.md say the same.

## The window leaves crash notes again (user-asked, 2026-10-27)

The window had no crash notes: nothing registered the crash module
(`chimera_crash.dll`) with Windows Error Reporting, so a graphics driver's
fast fail ended Chimera and left only the recovered work. It is registered
again at start, as docs/project.md ("Crash notes") describes, from a module of
the window's own (`crash_notes.cpp`): the block the module reads (the notes
folder, the frame each step, the GPU bridge's flight recorder, the project's
lines when it opens) is kept in the window's memory; the newest 20 notes are
kept; reopening a project whose work was recovered says what ended that
session; and Help > Report an Issue attaches the newest note and its dump.

Measured on Windows (the test PC): a headless run of a synthetic project
loaded a Lua module that fast-fails, as a driver does, after eight seconds of
recording. Windows ran the module, which wrote the note (the fast fail in that
module at frame 458, the project's lines, the stack) and a minidump; opening
the project again offered the recovered work and named the crash. Not tried:
a real driver's crash, and the flight recorder's lines (the synthetic core
draws without the GPU).

## Qt's own dialogs are drawn inside their window (2026-10-27, issue 389)

On Linux, Qt draws the file, folder, colour, font and message dialogs itself.
Since Qt 6.10 it puts each in a window of its own. A person on WSL found that
the file dialog's Open and Cancel did nothing, and that parts of it stayed
white in the dark theme. The owner chose to fix both in Qt's dialog rather
than write one of Chimera's own.

- Chimera's QML imports `ChimeraDialogs` (`source/ui/qt/dialogs`) in place
  of `QtQuick.Dialogs`. It is Qt's five dialogs with `popupType: Popup.Item`:
  each is drawn inside the window that opens it, as Qt drew them up to 6.9.
  Windows' dialogs are the system's own, and this does not change them.
- Qt chooses its dialogs' look by the style's name and knew Chimera's only
  as ChimeraStyle, so it gave them its basic look. In that look a picked line
  is white text on a near-white tint. The window now tells Qt that the style
  is Fusion's (`setExtraFileSelectors`), so the dialogs get Fusion's look and
  a picked line is drawn in the theme's selection colours.
- The white parts were the palette's "light" colour, which no theme set. A
  theme now sets it to its field colour. Qt's Fusion dialogs draw the names
  and icons of the places beside the list (Home, Desktop...) with no colour,
  so black, which was unreadable on a dark field. They now take the button
  text whenever something in the window takes the focus, as the dialog's
  list does when it opens.

Measured: a test program opened the file dialog with the dark theme off
screen and on WSLg's Wayland and clicked Cancel. In Qt's own window and inside
the window, the click closed the dialog in both places. A click sent by a
program does not pass through the compositor, so the reporter's failure was
not reproduced here. That Open and Cancel now answer on WSL has not been
seen with a real mouse.

The Linux bundle had a second fault: it left out `Qt.labs.folderlistmodel`,
which Qt's file and folder dialogs are drawn with, so in a Linux bundle
no file dialog opened at all ("module Qt.labs.folderlistmodel is not
installed"). Both bundles now carry it, and `tools/bundle_qml.py` makes
staging stop when a module that Chimera's QML or Qt's dialogs import is
missing. Measured: the check named that module on the bundle as it was, and a
file dialog loaded with only the new bundle's QML opened and closed.

## Compiled code belongs to the core build that made it (user-decided, 2026-10-27, issues 361, 383, 377)

RPCS3 games that hung or crashed (Sonic Unleashed, Oblivion) started once
the reporter moved a month-old compiled-code folder away and compiled again.
The design says a manifest that names another core build is not believed, but
the window never compared it with the build running. New Project counted the
game as compiled, and a session handed the old files to the core. Old files
keep their names, so even a new compile would have taken them as already
done.

The owner chose that the code belongs to the build that made it. Before a
session gives a game's folder to the core, and before New Project compiles into
it, code another build compiled is removed, and so are files no manifest names
a build for. New Project counts a game as compiled only for the build running.
Tools > Pre-Compiled Modules still lists each game's code with the build that
made it and removes it by hand.

Measured: the view-model test compiles a game with the synthetic compiling
core, marks its manifest as an older build's and adds a stray file. New
Project then said the code was compiled by the older build and compiled it
again, and the stray file was gone. A session that opened the project removed
the older build's files, and also files with no manifest, and kept this
build's. With the check turned off, the four checks failed. Not tried with
RPCS3 or with the reporter's games.

## Encode Video's presets keep the picture's shape and every pixel (2026-10-27, issue 375)

A report found three faults in Encode Video's presets. Each file was 16:10
where the game is shown 4:3: a PC-98's 640x400 picture was written with
square pixels. Two "Lossless" presets went through YUV, which changes some
colours by a level. And the lossless presets wrote AAC sound, which is not
lossless.

- The stream Chimera hands ffmpeg now carries the shape of a pixel, worked
  out from the picture's size and the size the machine shows it at. Every
  file plays in the machine's shape. The YouTube presets scale to that shape
  with square pixels.
- The lossless presets write FLAC sound. H.265 is RGB, as H.264 RGB and UT
  Video are. The H.264 preset that stays in YUV is named for what it is:
  "H.264 4:4:4 (YUV, nearly lossless)".
- The default MP4 is unchanged. Its colour is stored at half resolution
  (4:2:0), so a red and blue dither one pixel apart becomes purple. 4:4:4
  keeps it but does not play in many players. The owner kept the default
  as it is (user-decided, 2026-10-28): it plays everywhere, and whoever
  needs a dither kept picks the 4:4:4 or a lossless preset.

Measured with the ffmpeg the bundle ships (4.4.1), on a picture with
dithers and a ramp:
- The three RGB presets kept every pixel. H.265 in YUV had kept half the red
  and blue dither's pixels.
- The default MP4 lost the red and blue dither (mean error 88 levels).
  4:4:4 brought it to 1.8, and a lower CRF did not help.
- The encode test now checks the shape (640x400 written as SAR 5:6, DAR
  4:3; the YouTube sharp preset 2880x2160 at 4:3) and that each lossless
  preset gives back every pixel with FLAC sound. Each check was seen to fail
  without the change.

## Translations: the machinery only (user-decided, 2026-10-27, issue 376)

Issue 376 asks for the window in other languages. The owner chose to set up
the machinery only, with no language yet. The window loads, at start, a
translation built into the program for the first of the system's languages
that has one, and stays in English otherwise.
`tools/update-translations.sh` writes a language's file from the window's own
words (1425 of them). docs/translations.md says how a language is added.

What is left out on purpose: the view models' words, which are C++ without
Qt; a setting to choose the language; and Qt's translation tools in the
release's own Qt build. The first language needs those tools added there.

Measured: `test-translations` builds a French test word and loads it for a
Canadian French system, and nothing for a German one. It was seen to fail
with the loader turned off.

## A core's sound is converted to 44100 Hz again (2026-10-27, issue 372)

Investigating crackle in MAME's Killer Instinct found that the Qt window had
lost the sound-rate conversion of 2026-09-04 (issue 37). The old window's
adapter resampled a core that mixes at another rate to 44100 Hz, the rate
Chimera plays and encodes at. The Qt port read every core's sound as if it
were 44100. Fourteen of the installed cores declare another rate: 48000
(MAME, Dolphin, PCSX2, Xenia and others), 32768 (Gambatte), 32728 (melonDS,
Azahar) and 15734 (Uzebox). Their sound played a semitone and a half low (for
48000). The sound output dropped a whole frame's sound each time it ran a
buffer ahead, which is a click. Their encodes' sound ran 8.8% longer than
the picture.

The conversion now lives in the engine: `ce_session_sound` gives each
frame's sound at 44100 Hz, through a windowed-sinc resampler per session
(`source/engine/source/resampler.cpp`). Consecutive frames join without a
seam, and a seek or a state loaded starts it afresh. A loud sample is held at
the edge, never wrapped round. `ce_session_audio` stays the core's own sound,
so the sound the gates hash is unchanged. The window's sound output and
Encode Video both use the new call.

Measured: the engine test takes two seconds at 48000 Hz to 88186 samples at
44100 (the filter holds back 14). A 1000 Hz tone stays at 1000.2 Hz at the
same level, and 32768 Hz and 15734 Hz go up the same way. In the view-model
test a copy of the synthetic core that says 48000 Hz hands the speakers
675.2 sample pairs a frame (735 x 44100 / 48000 = 675.3). Without the
conversion it handed them all 735. Not tried with MAME and Killer Instinct:
whether this was the crackle the issue hears is for the reporter to say.

## TAStudio's columns are chosen in Config > Controllers (user-decided, 2026-10-28, issue 391)

A report found TAStudio's Columns menu hard to use on systems with many
buttons: a long menu, a submenu a player and the keyboard keys thirty a
submenu, one tick at a time, each tick closing the menu. It asked for the
menu to go and for the choice to be made in Config > Controllers, where
every control already has a row. The owner agreed.

- Config > Controllers has a box at the left of each button (Normal
  Controls) and each analog control (Analog Controls): ticked, the control
  is a column in TAStudio. Under each tab, "Show all" and "Hide all" do the
  tab's at once. The boxes are part of the window's copy, like the
  bindings: Save keeps them and Cancel lets them go. Saving a change marks
  the project changed, as hiding a column did.
- With several input rolls, the boxes are for the roll in use, and the
  window says which ("Columns in TAStudio's input roll #2").
- TAStudio's Columns menu is gone. Settings > Columns opens Config >
  Controllers over TAStudio, and so does Show columns in a column's menu.
  Hide column stays in that menu.
- The piano roll view model keeps its own commands for columns
  (showColumn, hideColumn, showGroup); only the menu that used them went.

Tested with the view model test: a box changed in the copy leaves TAStudio
as it was until Save, Cancel restores the boxes, a tab's Show all reaches
every column of that player, a column hidden in TAStudio shows unticked,
and the window names the roll when there are two. The test was seen to
fail with Save not handing the columns over. Both windows were opened with
the strict QML check on two test packages, one with two players and the
keyboard keys and one with an analog control, and pictured; not tried by
hand with a mouse.

## Missing firmware is asked for when a project opens (2026-10-28, issue 397)

A project whose firmware was not in the firmware folder, nor where this
machine last had it, did not open: the window said "Firmware missing (put
it in the firmware folder)" and stopped. The owner asked for the files to
be asked for instead, as New Project asks for them.

- The project now waits in Locate Project Files, the window that already
  asks for a project's own files when they are not found. The firmware is
  listed after the files, named as the package names it, with "firmware"
  in the Slot column. Locate... and Scan Folder... work on it as on a file;
  Open stays off until every required firmware is found. Optional firmware
  is not asked for: the core starts without it, as before.
- A located file must have the hash the project pins, else the hash the
  package declares; a game core takes a file of the person's own when the
  project pins none, as New Project allows. Another file is refused, with
  both hashes said. A folder scanned is looked through as the firmware
  folder is (by hash, else by the package's name and size).
- The file is remembered for the core, in the same list New Project keeps,
  so the next open of that project, or another one needing it, finds it.
- When both files and firmware are missing, the files are asked for first
  and the firmware after, since which firmware is needed is worked out
  once the files are known.

Tested with the view model test on a game core's project whose firmware
was moved: the project waits with the firmware listed, a file of another
hash is refused with both hashes, the right one is taken, remembered, and
the project opens; the next open asks nothing; a folder scanned finds it;
Cancel leaves the project closed. The test was seen to fail with the old
behaviour (the open refused). Not tried by hand in the window.

## The core says which player each control belongs to (user-decided, 2026-10-28)

A report found the piano roll's vertical lines thicker at places that had
nothing to do with players. The roll drew no player lines at all: every
column line was one unit wide, and on a screen scaled to 150% a one-unit
line falls on one or two screen pixels depending on where it lands, so
some looked thick. The owner asked for real thick lines between players,
and for the core, not Chimera, to say which controls are whose: "at the
end of the day, it's each core who decides what inputs belong to each
player or are console inputs."

- A package may declare `players` in its input declaration: a list of each
  player's controls; anything not listed is the console's. A package that
  declares none keeps the rule every movie was written with (the "P<n> "
  in front of a name). `ce_session_player_of` answers for any control.
- The same answer makes a movie entry's `|` groups, the roll's thick lines
  and the Controllers window's tabs, so they cannot disagree. The roll
  draws a thick line at the first input column, wherever one column's
  player differs from the one shown before it (hidden columns passed
  over), and after the last. The shading of every second player's columns
  follows it too.
- Every grid line is one screen pixel at any scale; the thick ones are two
  units wide.
- No core declares `players` yet, so nothing changes for any of them until
  one does; each core's next release can add it.

Tested: the engine test builds an entry layout from a declaration that
disagrees with the names (its groups and key follow the declaration; with
none, the rule); the view model tests check the edge rule and, with a test
package that gives "Power" to player 2, that the session, the roll's
columns and the Controllers tabs follow the declaration. Each was seen to
fail with its part broken. The roll's look was not checked on a scaled
screen.

## Seven languages, and a language setting (user-decided, 2026-10-28, issue 376)

The owner chose to ship languages now rather than wait for translators:
German, Spanish, French, Italian, Portuguese (written as in Brazil, used
for any Portuguese system), Simplified Chinese and Japanese, written in one
go and to be corrected by people who speak them. Each covers all 1163
different texts of the window.

- Config > Language chooses one, and the window's words change at once
  (Qt's engine retranslates every qsTr). The choice is remembered. At the
  first open, with nothing remembered, the window takes the first of the
  system's languages it has, else English, and remembers that: the owner
  asked for a setting whose first value is the system's language.
- The Language menu shows its English name beside a translated one, so a
  language picked by mistake can be undone by someone who cannot read it.
- Each translation was checked by a script before it went in: the same
  %1/%2, one shortcut letter where the English has one (in brackets after
  the word for Chinese and Japanese), the spaces and line breaks at either
  end kept, nothing missing. Qt's lrelease compiles each with every text
  finished.
- The release build takes lrelease from aqt's Qt (which the Windows build
  already uses for moc and rcc) for both platforms; the Qt that ships is
  still built without Qt's tools. The translations' resource is compiled
  without zstd, as the window's others are, for the Windows Qt.
- One QML line compared a view model's English text with a translated word
  (Project Settings' clock note); it compares with the English now.

Tested: test-translations (the seven built in, a Spanish and a Japanese
word, the first open's choice for French, Chinese, Brazilian, Taiwanese and
Korean systems, a remembered choice, English back), seen to fail with the
language list broken. The window was opened with no settings on a German
and a Japanese system: it chose and remembered each, and TAStudio was
pictured in German. The Japanese picture showed boxes, as this machine has
no Japanese font. A long German label in TAStudio's side panel is cut off.
The Linux release build's lrelease was not run here (no shipping Qt on
this machine); the next Release run is its proof.

## A project's input is its columns and its entries (user-decided, 2026-10-28)

The project file held its input as the movie's text block in one string:
"[Input]\nLogKey:#Reset|Power|#P1 Up|...\n|..|.U......|\n...[/Input]\n". The
owner found that sloppy and asked for the columns and the entries to be
entries of their own.

- `inputs` is an object: `columns`, a list of the movie's groups (the
  console's first, then each player's), each a list of its columns' names;
  and `entries`, one string a frame. A branch has its own `inputs`. Each
  list is written one item a line, so a file reads, and diffs, a frame a
  line.
- `columns` is a list of groups rather than one flat list because an
  entry's `|` groups are part of what it means: a flat list would lose
  where each group starts.
- Inside, the engine still keeps the text block every reader of a movie
  takes; the file is turned into it and back only where the project is
  read and written, so nothing else changed. Joining the groups back gives
  the key byte for byte, whatever the names hold; a name with its own `#`
  or `|` only looks split in the file. A key that cannot be split is kept
  as `key`.
- The old `input` string is not read: the owner wants no mechanism that
  keeps old forms working in Chimera ("I want Chimera's code to be extremely
  clean"), so a project saved before 2026-10-28 is refused, naming the
  `input` key. A file with an entry that is not a frame's input is refused
  and says why. A build from before cannot open a project saved by this
  one.

Tested: the engine's project test saves a project and a branch, checks the
file holds `inputs` written a group and an entry a line, opens it to the
same text block (for a key with "P1 \"#" in it and one with "| @" and
"# >"), refuses a file in the old form and five broken ones. It was seen to
fail with the input left out of the file.

## The Godot 4.7 core is named for its version (user-decided, 2026-10-28)

The owner renamed the core "Godot" to "Godot 4.7", its repository from
chimera-core-godot to chimera-core-godot47 and its package to
godot47.chimeraCore, so that its name says its version as its siblings' do
(Godot 4.6, 4.5, 4.4). The roster and the README follow.

A project names its core by name, so a project made with "Godot" finds no
core called that once the old package is gone. Chimera keeps no mechanism
for old names (owner: "I don't want any legacy supporting mechanisms in
chimera"); such a project is made again with the Godot 4.7 core, or its
core name edited by hand.

## Loading a branch asks nothing; the message warns (user-decided, 2026-10-28, issue 395)

The question of 2026-10-24 ("Update the current branch?" before loading
another branch over changes the current one does not hold) came up on almost
every load, since branches differ from the movie most of the time. Two
people asked for it to go (issues 360 and 395). The owner chose to remove it
and warn instead: the load goes ahead, and when the movie held inputs or
markers of the person's that the branch left does not, the message after the
load says so - "branch 2 did not hold the changes made since it was loaded or
updated (Undo Branch Load brings them back)". A script's or an agent's load
says nothing of it, and loading the current branch again warns of nothing.

Tested: the view model test loads the current branch again, an unchanged
movie, a changed input and a marker of the person's, and a script's load,
and checks what the message says each time; it was seen to fail with the
warning left out.


## Every text is a key in a language table, one file per language (user-decided, 2026-10-28)

The owner set four rules: Chimera and every core keep all their texts as
keys resolved from a language table; no text is written into the code;
every language Chimera has gives every key; and the same holds for the
cores' settings. The tables are separate files, one per language
("one json per language"), for Chimera and for each core alike. Their
answers to the questions this raised:

- Keys are names (`common.file`), and English is one more table, so the
  window's 1,246 texts that Qt keyed by their English words moved to names.
- What counts is what a person reads: the window, its messages and errors,
  what a core declares and the errors it gives. Developer logs, file-format
  names and tests do not; nor the descriptions of Lua functions, MCP tools
  and view-model properties (they describe English names), nor
  chimera-run's own output, nor control names (identifiers that movies,
  bindings and scripts use), nor names that are data (game titles, MAME's
  machines, the community's patch and cheat names).
- Each core package carries its own tables; Chimera has its own.
- More languages: as many as can be written with confidence. Twenty-six
  are planned; Arabic, Hebrew and Persian wait until the window can mirror
  its layout.

How: `languages/<code>.json` holds a language's name in itself and in
English and its texts; a text that depends on a number has one form per
CLDR plural category. The build copies the tables beside the engine
(`lib/languages/` in a bundle), and the engine reads them at run time.
The window asks with `qsTrId("key")`, answered by a Qt translator that reads
the engine's table in use, so Qt still redraws every text the moment the
language changes; the engine and the view models ask through `ce_text`. Qt's
own translation files (`.ts`, `lrelease`) are gone, and with them the
release build's need for Qt's translation tools.

This first step moved the window's texts (all seven translations kept, each
text matched by its English words) and the reading of the tables. The view
models' and the engine's texts, the cores and the new languages follow.

Tested: `engine_language` checks every table against English (keys,
`%1`..`%9`, plural forms, shortcut letters, spaces at the ends), and was seen
to fail with a German text removed and another's `%1` dropped; it also
checks the plural rules of all 26 languages. `translations` checks the
language a German, French, Chinese, Brazilian, British and Swahili system
starts in and what `qsTrId` answers. The window opened on a German system
with no settings showed its menus in German.

## A core's texts are keys of its own tables, with arguments after a 0x1F (2026-10-28)

Following the owner's rules of the same day (the section above), the view
models' and the engine's texts moved to keys too, about 2,550 in Chimera's
English table, and `tools/check-texts.py` (the `texts` test) refuses words
written into the code. Logic that read an English text now reads a flag:
the data folder's "holds data" choice, a firmware line that names none, the
project wizard's refused clock.

A core package's texts follow the same rule, in tables the package carries
(`languages/<code>.json`). The declarations hold keys, and so does what the
core returns while it runs: the load error, the answers of
`SuggestSettings` and `ImportMovie`, `GetGameSettings`, and its properties'
descriptions. Decided here, within the owner's rules:

- No new export and no new declaration: the strings a core already returns
  may be keys, and the engine resolves every string that is one. A core
  that has not moved yet still shows its words.
- A text with arguments is its key, then each argument after a 0x1F byte.
  The cores write sentences with a file's name or a number in them, and
  SyndicatFX alone has 438 firmware descriptions that differ only by a file
  name: one key with `%1` is one text for a translator, not 438. A key with
  plural forms takes its count first; an argument that is a key is its
  text, for an emulator's reason inside the core's sentence, and such a
  reason's own arguments are separated by 0x1E (a zip reader's "a damaged
  entry 'x'" inside "the save data is not a zip: ..."). A report the core
  composes from many sentences (MAME's account of a game's files) is
  several texts after 0x1D bytes, shown a line each, rather than one key for
  every combination.
- An emulator's own words stay its own, and so do those of a core's
  stand-ins for what a sandbox lacks (no network, no disc drive, one
  graphics context): they answer the emulator in its terms, and it reports
  them as its reasons, inside the core's sentence or alone.
- The engine keeps a session's package tables after the package is closed
  (`PackageTexts`), and resolves at the moment it is asked, so a language
  changed while a game runs changes the game's settings and properties too.
- In a JSON answer only the key strings are replaced; the rest is kept byte
  for byte. Parsing and printing it again would have rounded a 64-bit
  number in a suggested setting.
- What stays a name: control names, option values, memory domain, bus,
  register, surface and drive names, a property's name, group and value
  names (scripts set a property by its value's name), and data a core reads
  from its emulator or a database.

Tested: `engine_language`'s package part (keys with arguments, plural forms,
a key inside, a member name and a 64-bit number left alone, a missing
language shown in English, a declaration field that is no key or lacks an
argument) was seen to fail with the arguments not filled, with the
argument count not checked, and with member names resolved. The synthetic
core's texts are keys now; `synth_texts` (tools/check-core-texts.py) was
seen to fail on a misspelt key, and the witness's new `E:core-text` reads
"Synth: The synthetic core refuses this game of 4 bytes." through
chimera-run. Witness 122/0 in a fresh work folder.

Found on the way, not caused by this and not fixed: in a work folder kept
from an earlier witness run, `M:mcp:window:changes` fails. The agent saves
a copy of the project under the same project id, both share one cache
folder, and the next run's window restores the copy's state history for a
movie with other input; afterwards no state is found, not even frame 0.


## Chimera and every core in 26 languages (user-decided, 2026-10-28)

The owner asked for as many languages as could be done with confidence, and
for every text of Chimera and of every core in each. There are now 26:
English and German, Spanish, French, Italian, Japanese, Brazilian
Portuguese, Simplified Chinese, Korean, Traditional Chinese, Russian,
Ukrainian, Polish, Czech, Dutch, Swedish, Danish, Norwegian Bokmal,
Finnish, Turkish, Hungarian, Romanian, Greek, Indonesian, Vietnamese and
Catalan. Chimera's table has 2551 texts; the 56 cores' tables hold about
6200 more between them.

- The texts were translated by an AI model (Claude Haiku, as the owner
  chose), one language at a time, from the English in chunks of about 20000
  characters, with the rules of docs/translations.md as the instructions,
  and each language's existing words (German Film, French Image) kept where
  Chimera already had a table. No speaker of the languages has read them;
  corrections are welcome and are an edit of one file.
- Every chunk was checked against its English by the rules the engine
  holds a table to (the same keys, the same %1 to %9, the language's plural
  forms in CLDR's order, a shortcut letter, the spaces at either end).
  Plural forms a language does not have were dropped, invisible characters
  (soft hyphens) removed, and what failed went to a second, smaller round
  of translation; a handful of texts were written by hand. Turkish writes a
  percent sign before the number (%50), which the engine reads as a
  placeholder: those percentages are written in words.
- Names stay as they are in every language: game titles, consoles and
  emulators, file names, setting identifiers, control names.

## A project saved under another name gets an id of its own (user-decided, 2026-10-29)

A project's cache folder (its state history, its branches' states) is named
by the project's id, and Save As kept the id. The two files then shared one
folder: the next save of either wrote its history there, and opening the
other loaded states made for other input. The witness showed it: in a work
folder kept from an earlier run, `M:mcp:window:changes` failed with "the
seek was interrupted" on every seek back. The agent's `save_project` to
another name had written a history holding one state, at frame 70, into the
folder the next run's project opened from, and with no state at frame 0
nothing before frame 70 could be reached. Each failing run then saved the
same history again.

The owner chose this over two others: the engine keeping, with each history,
the movie it was made for and dropping the states past the first frame whose
input differs (it would also cover a file copied by hand, at the cost of a
new history format), or both. So: `ce_project_save_as` gives the project a
new id and writes it into the file, and keeps the old id when the save
fails. The window uses it when a project is saved to a path other than its
own (not for a backup, which is the same project kept). The branches' states
are linked into the new folder (copied where a link cannot be made), the
budgets copied, the history written there by the save itself, the running
mark moved, and the work kept against a crash under the old id dropped,
since it is in the new file now. The first file's folder is left as it was.

A file copied by hand, or a backup, still keeps its original's id. The
2026-10-15 rule (a project opened from another path drops its cached
greenzone) would cover those, and the Qt window does not have it yet
(docs/qt-interface.md).

Tested: engine_project checks the new id, written in, the first file's id
kept, and a failed save keeping the id; view models checks a save under
another name: an id of its own, the branch's state and note in the new
folder, the first file's history unchanged, and the session's recovery
journal under the new id. Each was broken once and seen to fail (the first
file's history was overwritten, as in the witness). meson test 47/47;
witness 122/0 twice in the same work folder; the Windows build compiles.

## A core's texts come from any table it has (user-decided, 2026-10-29)

The owner: a core that lacks the language in use must not fail; it shows
English, or whatever language it does have. The engine already showed a
core's English when the core had no table for the language in use, or the
table lacked a key, and showed a key as it is when no table had it. Now,
when English lacks it too, the first table the package has that holds the
key gives it, in the order Chimera lists its languages. A table that does
not read is an empty one. Nothing refuses a package for its languages; only
the test of installed packages (test-packages) holds a package to every
language, as the 2026-10-28 rules ask.

Tested: engine_language reads a package with only a German table, a Spanish
one lacking a key and an Italian one that is not JSON: with French in use
the German text, with Spanish the Spanish and, for the missing key, the
German, with Italian the German. Broken once (the step left out) and seen
to fail.

## The window drops a cache made for a project at another path (2026-10-29)

The owner's 2026-10-15 rule, which the Qt window did not have: a project
opened from another path than the one its cache last recorded (about.json's
projectPath) - a file copied by hand, a moved one, a backup, a recovered
copy, all of which keep the id - has the cache's state history and branch
states removed and starts with an empty greenzone. The budgets stay, and a
cache that never recorded a path is left alone. With Save As giving the copy
an id of its own (above), this covers the copies that keep one. Paths are
the same when they name one file, as the system matches names and links.

Tested: view models saves a project, copies the file, and opens the copy:
no history, no branch states, an empty greenzone, the budgets kept and the
copy's path recorded; the copy's own history, saved, is then kept for the
first file once the cache's path is taken away. With the path check made to
always match, it failed. meson test 47/47, witness 122/0, the Windows build
compiles.

## The way back after an edit goes as a seek does (2026-10-29)

Issue 406: with the greenzone off, an edit far behind the machine left the
window not responding for as long as the machine took to get back. After an
edit, TAStudio sends the machine back before the edit and on to the frame it
was on (auto-restore), or to the edit. The window did that as one blind
replay on the machine's thread, and anything of the window's that waits on
that thread (many requests do) waited until the replay ended. Measured on
the synthetic core: a 400,000-frame way back took 8 seconds and the window
answered nothing for 8.3 seconds.

The old TAStudio went back as any seek goes, and so does this window's own
seek: what the greenzone holds at once, and the rest played at the
machine's speed with every frame drawn (Turbo seek off, the default) or as
fast as it goes in pieces of about a tenth of a second (Turbo seek on). The
way back after an edit, and the green arrow back to where the edits began,
now go the same way. The machine is paused first, as the old TAStudio's
"restore makes no sense without pausing" did, and between frames or pieces
its thread answers the window. An edit made while it goes keeps the frame
it goes back to; one that ends the movie before it stops it at the end.

The frames of a seek that plays are the movie's, as they were in the old
TAStudio, which turned recording off while it seeked: here a seek that
played in recording mode wrote what the person held (usually nothing) over
every frame it passed.

Tested: view models fills 200,000 frames with the greenzone off and edits
frame 3. With Turbo seek the way back took 4 seconds and a request the
window waits on was answered within 61 ms throughout (4,014 ms with the
old way back); played, within 1 ms. A seek in recording mode played through
marked frames and left them marked (they were cleared with the frames
played as live input).

## The settings file is beside the program (user-decided, 2026-10-29)

Issue 403: the window kept its settings in the system's folder for the
person's settings, so every Chimera folder on a computer shared one set,
and a setting wanted for one build (to replay an old movie, say) changed
it for all. The old window kept config.ini beside its program. The owner
chose the same for this window: settings.json beside the program, and,
when that folder cannot be written (Program Files, a system-wide install
on Linux), the person's own folder as before. --config still names any
file. Nothing is read from where the settings were kept before: a person
who wants them back loads that file with Config > Load Config From...,
which takes it as it is.

Tested: command line runs a copy of the program with no settings file
named: a setting made by a script is written beside it; with its folder
made read-only, in the person's folder. With the program's folder never
used, the first check failed.

## What is drawn over the picture grows with the window (user-decided, 2026-10-29)

Issue 401: the counters, messages and watches over the picture were 12
pixels whatever the window's size, as the old window drew them, and hard to
read in a large one. The owner chose a Display setting, Scale OSD text with
the window, on at first: the text and its distances from the corners grow
with the picture area, 12 pixels being a 480-pixel-tall area's (the size of
a doubled 240-line picture), and never shrink below 12. A script's gui.text
is drawn the same way, so it stays the counters' size; Encode Video's
Capture OSD is drawn at the machine's own size and is unchanged. The text
is a key in all 26 language tables.

Tested in a scratch run of the window: the scale was 1.32 in the default
window (632 pixels tall), 1 at 120, 1.5 at 720, and 1 again with the
setting off. The window test loads the Display window under the strict
QML check.

## A key a hotkey takes is not a menu's (2026-10-29)

Issue 405: in the new window, pressing a hotkey opened menus. Qt's menu bar
takes Alt and a letter as the menu with that letter whatever else the key
does, and on Windows it takes the keyboard to the menus when Alt is let go
even after another key was pressed with it (Windows itself does not), after
which plain keys open menus. Its letters were also underlined all the time.
The old window gave the keys to the hotkeys first and passed only what they
did not take to the menus, and Windows hid the letters until Alt.

Now, in the main window and TAStudio, a combination a hotkey or a control
takes is kept from Qt's shortcuts and menu letters and comes to them as a
key; Alt+V with nothing bound to it still opens the View menu. A key
pressed while Alt is held, or Alt that is itself bound, makes the menu bar
forget it was waiting for Alt to be let go (it forgets at an event of the
kind sent for it). The menus' letters are drawn from Alt pressed until a
click, the focus leaving the menus, or Alt let go with no menu taken; the
window's style draws them so (style/MenuBarItem.qml and MenuItem.qml, Qt's
Fusion ones with that change), and the letters work either way. The key
filter moved out of main.cpp into key_filter.cpp to be tested.

Tested: key filter, in the window's own style with Qt's test keys: Alt+V
opens the View menu while nothing takes it; with a hotkey bound to it the
hotkey runs once and the menu stays shut (it opened with the override taken
out); the View item is drawn "View" until Alt is pressed, "&View" after,
and plain again after a click or Alt let go (each failed when broken).
Qt's menu bar takes the keyboard on Alt let go only on Windows, so the
forgetting was not tried: not tried on Windows.

## Chimera's CI depends on no core (user-decided, 2026-10-29)

CI had a job that fetched every core's published package and ran the package
contract tests (test-packages) against them. After the language pass, some
cores' nightlies had not been rebuilt yet, and one (srb2) is published from a
repository Chimera does not control, whose pull request with the tables was
not merged. The job failed, CI failed with it, and Release, which publishes
only a commit whose CI passed, published no development build or nightly
for any of Chimera's own fixes. The owner: Chimera must be modular and
independent of core packages, and never blocked by them.

The job is now a workflow of its own, Published cores (cores.yml), run on
every push to main, daily and by hand. It checks the same contract and says
what it finds, but CI no longer includes it, so Release does not wait on it.
Nothing else in CI uses a core: the end-to-end tests use the synthetic core
built from this repository.

Tested: both workflow files read as YAML; CI's jobs are the two witness
jobs. The workflows themselves run on push.

## Every button, not the first 64 (2026-10-29)

Issue 411: in the new window, a Ruffle game's arrow keys and Space did
nothing in recording mode. Ruffle's controller is a keyboard of 103 buttons
and the arrows are the 83rd to 86th. The engine's calls for one frame of the
movie (ce_session_movie_get and _set) and its line decoder gave and took the
buttons as one 64-bit number, and the window kept what is held, autohold,
autofire, a script's hold, the piano roll's rows and the undo history the
same way. Every button past the 64th was dropped, and _set wrote it as let
go, so recording also cleared those columns in a movie made elsewhere. 29 of
the 57 packages have more than 64 buttons: computers' keyboards, and
consoles with several players (DuckStation 204, PCSX2 191, MAME 491,
Mednafen 594). The engine itself always played any number (its movie lines
have a column a button, and ce_session_set_button hands a core the ones past
64).

The frame calls and the decoder now take one byte a button, every one of
them (the decoder's wide twin became the only one, as nothing should use a
narrower form), and the window carries buttons as a set of any width,
vm::Buttons, shared between its threads behind a lock. Show Future, the one
place the window runs frames itself, hands the buttons past 64 to the core
one by one and lets them go after. The synthetic test core gained a
SetButton export (it keeps the values and reads none), which the engine
asks of a package with more than 64 buttons.

Tested: view models opens a package of 88 buttons (the synthetic core's 8
and 80 keys): a key bound to the 81st is recorded into the movie, the movie
says it is pressed, the piano roll shows its cell and flips it (a step that
undo takes back), and a script's hold and autohold of the 82nd and 83rd are
recorded and handed to the core. With the frames read back as the first 64
buttons, 5 of those checks failed. Every other check passed unchanged on the
8-button synthetic core.

## A history of another machine leaves the greenzone as it was (2026-10-29)

Found while testing issue 411. When the window opens a project it loads the
state history kept in the project's cache, which the engine takes only when
it was made by the same machine (core build, settings, files). The loader
emptied the greenzone first and then checked whose history it was, so a
history it refused - another core build's, say, after a core update - left
the session with no state at all, not even the one taken of the first frame
when the project opened. Every seek back then failed ("the seek was
interrupted"): the end-to-end test's MCP agent hit it once the synthetic core
was rebuilt, against a cache an earlier run had left behind.

The states held stay now until a file is taken: one of another machine, one
in a format no longer read, one that is not a history, or no file at all
leaves them as they are. A file taken and then found damaged part way still
empties the history; the session then takes the machine's state again, so
it always has a frame to go back to.

Tested: engine_state_history keeps a captured first frame through loading
another machine's history, an old-format one and a file that is not a
history (it failed with the loader emptying first); the earlier check that
a history in an older format is dropped without the list now finds the
states held kept. The MCP agent run against the cache that broke it: every
check passed (before: every seek back answered "interrupted").

## The Windows program says what it is (user-decided, 2026-10-29)

Windows Defender removed chimera.exe from the published Windows build of
development build c1abf86d as soon as the zip was unpacked on the owner's
PC, calling it Program:Win32/Contebrew.A!ml. The "!ml" means a guess of
Defender's machine learning, not a known signature. chimera.exe is the small
launcher that loads the window from lib\ (docs/qt-interface.md).

What was measured on that PC, with Defender running: the published
chimera.exe is removed even when copied alone into an empty folder; the same
launcher built here (it differs in 4 bytes, the time stamp and the checksum)
and the one in the 2026-10-28 nightly are not. The verdict belongs to that
one file, not to how the launcher is made, so no change to it can be shown
to help.

The owner chose to give chimera.exe the version information every program
carries (Windows shows it in the file's Properties): Chimera, the version and
the commit it was built from, and the licence; the next build replaces the
flagged file anyway. Loading the window's DLL through a manifest instead of
by the launcher's own code was not done.

Tested: Windows reads FileDescription Chimera, FileVersion and
ProductVersion "0.1 (c1abf86d)", the copyright and licence line and
OriginalFilename chimera.exe from the new file. Defender left the new file in
place, as it left the unchanged one, which shows nothing about the flag.

## TAStudio keeps up with the game (2026-10-29, issue 404)

The piano roll looked like it ran at 10 frames a second while the game
played. Measured on Linux with the window's own timing (--measure, which now
also times TAStudio's window, see below): the machine ran 60 frames a second
but the window drew 13 to 20, and its thread was busy most of the time. Most
of that was lists being built again from nothing: four of them were told on
every frame that everything had changed (a reset), and a list told so makes
every row it shows anew. In 8 seconds of play:

- the undo history, 474 times: each frame played past the end of the movie
  joins the step before, so the history had as many steps as before;
- TAStudio's markers, 330 times: the run's end marker moves on with each
  frame added;
- Macro Input's zones, 301 times: the zone of the whole movie grows with it;
- the Hex Editor's lines, 293 times: it read memory after every frame,
  with its window closed too.

Now a list read again with as many rows as before says its rows changed in
place (RowModel::refreshed), Macro Input changes only the whole movie's row,
and the Hex Editor reads after each frame only while its window is open (its
window sets watching, as the Surface Viewer's does).

On Windows the roll's own drawing then showed: 7.7 ms a frame against 2.3 on
Linux. Two parts were most of it. Each cell asked where its column starts,
and that adds up the widths of every column before it, so a frame cost rows
times columns squared (for a controller of 594 buttons, far worse); the
places are now worked out once a drawing. And the grid's thin lines, drawn as
lines, took 3 ms; they are filled one screen pixel wide now, which looks the
same (checked at 125% on Windows, the line under the titles drawn last so the
first row does not cover it).

Found on the way: the window gave a script's cell questions
(tastudio.onqueryitembg, onqueryitemtext, onqueryitemicon) to the roll it
found when it started, but TAStudio's window makes its rolls later, so no
roll ever asked and a script's colours and texts never showed. Every roll
asks the one script runner now, a roll made later too. --measure looked for
the roll the same way and counted no paints; it now finds it in what the
windows draw and also times TAStudio's window.

Tested:
- view models: while a game plays 30 frames past the end of its movie, none
  of the four lists is reset and each changes in place; the Hex Editor reads
  nothing with its window closed. With the old resets put back all five
  checks failed (30 resets in 30 frames each).
- tastudio queries (new): a script's questions are asked by the roll in the
  real window; before the fix the roll asked 0 times.
- Linux, off-screen: both windows draw a frame every 16.6 ms (median), with
  the synthetic core and with FreeJ2ME (before: 50 to 75 ms).
- Windows, the GTX 1060, FreeJ2ME playing a new project's empty movie, as the
  report: the main window drew 618 of 1,042 frames (286 of 1,037 before) and
  TAStudio's window a frame every 17.0 ms (median), its roll in 0.97 ms. Some
  frames still take two or three refreshes there (95th percentile 50 ms),
  with this core on Windows only: the synthetic core draws 809 of 811, and
  muting the sound changed nothing. That is not explained yet.

## The window waits on nothing after a frame (2026-10-29, issue 404)

After the lists and the roll's drawing were fixed (above), the window on
Windows still drew only 618 of 1,042 frames of FreeJ2ME, while the
synthetic core drew nearly all. Linux has no such stall, so its cause was
found on Windows itself: a thread in the window that paused the window's
own thread every 2 ms and read where it was (Windows' stack unwinding, a
temporary build, not kept).

In those samples (taken with the screen locked, which stops the drawing but
not the rest) the window's thread spent about 42% of its time waiting for
the machine's. After every frame the Lua Console's view model asked the scripts, which
live on the machine's thread, whether any ran, each one's state and the
stamp of their registered functions, and its list asked each row's state
again as it was drawn. Every question waited for the frame the machine was
running by then, which for FreeJ2ME (a Java machine in the sandbox) is long.
Now the machine's thread reads all three at the end of the frame, where it
costs nothing (LuaBridge::seen), and hands them to the window with the
frame; the list shows the states from what was seen. Asked by a person
(starting or pausing a script), the console still asks and waits, once.

The next samples showed the window's script memory being cleaned up more
than it needed: TAStudio's branch and marker lists ask a colour for each
cell, and each answer read the roll's colours and the theme's from JSON
text again. They are read once now, when they change.

Tested: view models gains a check that the window's part of a frame, and
the list it shows, ask the scripts' thread nothing (with the old asking put
back it counted the questions and failed). On Windows, GTX 1060, FreeJ2ME
playing a new project's empty movie: 1,339 of 1,344 frames drawn (618 of
1,042 before), both windows a frame every 16.6 ms (median). In 10 seconds of
samples 6 pauses remain, of 15 to 37 ms, QML's script memory clean-up; the
95th percentile of the window's frame times is 33 ms, a frame now and then
waiting one more refresh as the machine's 60.07 frames a second drift
against the screen's.

## Files and folders are picked with the desktop's own chooser on Linux (user-decided, 2026-10-30)

A person reported Linux's file dialog as unusable: Qt's own, drawn inside
the window since issue 389. It is not the desktop's chooser, which many
people prefer. A path cannot be typed into it. It has no way to show hidden
files. Its folder dialog picks the folder highlighted in the list instead of
the one shown. It lists no bookmarks, no drives and not even the root. They
asked for the desktop's chooser through the XDG desktop portal, as PR 275
did for the Windows Forms frontend, with a dialog of Chimera's own where
there is no portal, and that dialog to fix the same points.

- `ChimeraDialogs`' `FileDialog` and `FolderDialog` are now Chimera's own
  objects with the properties of Qt's that the windows use, so no window
  changed. On Windows and macOS they still open Qt's dialog, which is the
  system's.
- On Linux they ask the portal through libdbus, loaded when first needed,
  since Chimera's Qt is built without D-Bus (`portal_file_chooser.cpp`). The
  rules PR 275 found are kept: 2 within 300 ms is a chooser that never came
  up, a later 2 is Cancel, the portal leaving the bus is no chooser, and the
  windows take no input instead of being disabled. Under Wayland the chooser
  is given no parent window.
- Where there is no portal, Chimera's own chooser (`BuiltinChooser.qml`)
  replaces Qt's: a Location box to type a path into, Show hidden files, the
  root, home, the person's folders, GTK's and KDE's bookmarks and the mounted
  drives beside the list, and a folder dialog that picks the folder it
  shows. Issue 389 chose to fix Qt's dialog rather than write one of
  Chimera's own. Qt's dialog has no way in for the points above, so this
  goes the other way; the owner confirmed it when merging (2026-10-30, PR
  420).
- The theme test set Fusion's style before the application was made. With
  KDE's Qt platform theme in use (`QT_QPA_PLATFORMTHEME=kde`, which a KDE
  desktop sets and other desktops can too), that theme then set Breeze's
  style in its place, so the test drew Breeze's controls, and its Lua form
  check failed whatever the change. It now sets the style after the
  application, as the window does.

Measured: the new file chooser test passes both halves, with Chimera's
chooser (no bus) and through a stand-in portal on a private bus (52 and 54
checks). With the input filter not installed, the two checks of a key
pressed while the chooser is open failed. Against the portal of the desktop
it was built on (niri on Wayland, whose portal setup sends file choosing to
KDE's chooser), a request was taken and its request object was at the path
Chimera listens on.

By hand, on niri on Wayland: the window was started on a private D-Bus
session bus, whose desktop portal (xdg-desktop-portal 1.22.1) sent file
choosing to one chooser alone, as PR 275 was tested. KDE's (6.8.2), GNOME's
(51.0, with Nautilus 51.1), GTK's (1.15.3, with no pause), COSMIC's (1.10.0)
and LXQt's (1.4.0) all worked, and so did Chimera's own, shown for a portal
with no file chooser. Not tried: Plasma, GNOME and COSMIC under their own
desktops, an X11 session, Windows and macOS.

## Include sub-folders is a box in the folder dialog (2026-10-29)

New Project's firmware page and Project Files had an "Include sub-folders"
box beside their Scan Folder button, and Config > Firmware always looked in
sub-folders. A contributor asked for such options to be in the file picker
itself, on Windows and on Linux, as PR 275 did for the Windows Forms
frontend, which the owner merged. Nothing from the owner says otherwise:
issue 368's sub-folder depth setting, which the owner chose not to add, is
about how deep a scan goes, not where its box is.

- `FileDialog` and `FolderDialog` take `checkBoxText` and `checkBoxChecked`.
  The three windows' folder dialogs carry "Include sub-folders", ticked to
  begin with; the boxes beside the buttons are gone.
- Through the portal, the box is a choice with no options and is read back
  from the answer. Chimera's own chooser draws it in its bottom row.
- On Windows, Qt's dialog takes no controls of a caller's. A dialog with a
  box is shown by Chimera as Windows' own common item dialog with the box
  added (`IFileDialogCustomize`); a dialog without one is still Qt's.

Measured: the file chooser test's new checks pass (59 with Chimera's
chooser, 66 through the stand-in portal). With the choice not sent, the six
checks of the request's choices failed; with Chimera's chooser not handing
its box back, the check of the dialog's box failed. meson test 50/50, witness
121 ok. The Windows dialog code was built with mingw-w64 and linked against
ole32, shell32 and uuid; run under Wine, the folder dialog came up and
stayed open. Windows itself, and the dialog's box under Wine, were not tried.

## Config > Language has an icon (user-decided, 2026-10-30)

The owner asked for an icon on Config > Language, as the other entries have.
It is a globe in the blue of the Firmware icon beside it: a disc with one
curved meridian each side of the middle and the equator. A first try with
three parallels and a straight meridian looked like a net at 16 pixels.
tools/draw-language-icon.py draws it at 8x and scales it down, as the other
drawn icons are made. It was looked at beside its neighbours on a light and
a dark menu.

## A project that does not open says why in a box (2026-10-30, issue 422)

Opening a project that could not be opened looked like nothing happening.
Chimera's Windows Forms window showed a "Cannot open the project" box with
the reason. The new window only put the reason over the picture for the
messages' time (a few seconds), cut off at the left when it was longer than
the window, and not at all with View > Display Messages off. The report shows
New Project's Create closing the wizard and nothing more, the same picture;
why that build (ca75e07c, older than the MAME core package it ran) did not
open the project was not found here.
On Windows a headless run of such a project also waited for ever: a file
the engine refused before the machine was asked to open it was not counted
as a failed open.

Now every failed open is counted, that refused file included, and the
window shows the reason in a box titled "Cannot open the project", in
every language. A headless run ends with 64 and the reason, as for the
other failed opens.

Measured: the headless test opens a file with a key no project has. It
ends with 64 and names the key; with the count switched off it waited until
the test's 60 seconds ran out. A project saved by the Windows Forms build
in the old form (the "input" key, refused since 2026-10-28) now shows the
box with 'unknown project key "input"'.

## The machine's own shape is the size it declares (2026-10-30, issue 414)

With the picture's shape set to the machine's own (the default), the new
window kept the picture's height and worked out its width from the shape
the machine declares. Chimera's Windows Forms window showed the declared
size itself, its "virtual size". The two differ when a picture is not as
tall as its declared size: NP2kai's PC-98 picture is 640x400 and the core
declares 640x480, so the new window showed it at 533x400 at 1x where
Chimera's showed 640x480.

Now each picture carries the size it is shown at, worked out as Chimera's
was: the picture's own height at the shape the core reports while it runs
(the optional display aspect exports), else the size the package or the
machine declares (which is the picture's own size when nothing is
declared). The machine's own shape uses that size. A ratio, a custom size
and the raw pixels are as before. A branch's note still keeps only the
shape, which is all its preview needs.

Measured: the view model tests check a 640x400 picture declared 640x480
is 640x480 at 1x, and that the synthetic core's 128x120 picture declared
256x240 is shown at 256x240 (it was 128x120); with the change undone both
fail. NP2kai itself was not run here: its PC-98 firmware is not on this
machine.

## Settings are shown in Chimera's grid (2026-10-30, issue 417)

New Project's settings page and Project Settings showed each setting as a
bold name, its control beside it and its description under it. Only four
or five settings fitted without scrolling, the controls did not line up
with the names when the window was made wider, and setting a value
scrolled the list back to the top. Chimera's Windows Forms window showed
them in a property grid: the name and the value side by side, one line a
setting, and the description of the chosen row in a box under the grid.

The list is that grid again: the names take the left 45% of its width and
the values the rest, so they stay lined up at any width; a row is chosen by
a click or by its control taking the focus, the first one at the start;
the box under the grid shows the chosen row's name and description, five
lines tall and scrolled when longer, as Chimera's. The machine's clock is
the grid's last row, as before. Setting a value no longer rebuilds the
list: when the number of rows stays the same, the rows are updated in
place, so the list keeps where it was scrolled to.

Measured: the view model test checks that a value set updates the row in
place (it failed with the old rebuild put back). The grid was looked at in
Project Settings with the VICE core; New Project's page uses the same list
and was not looked at.

## Controllers says what its boxes do (2026-10-30, issue 418)

The boxes at the left of each control in Config > Controllers make it a
column in TAStudio, but the window said so only in a tooltip and in a
label beside Show all and Hide all under the list ("Columns in
TAStudio:"), which was not read as being about the boxes. The label now
says what a tick does, "A ticked box makes the control a column in
TAStudio." (with several input rolls, the roll's number), and it sits
above the list with Show all and Hide all, in all 26 languages; the two
old texts are gone.

Checked: the window pictured with the VICE core; the translation and
text tests pass. No test of its own: nothing but where a label sits and
what it says changed.

## The Wayland text-input warning is muted (user-decided, 2026-10-30)

On Wayland the terminal showed, over and over, "qt.qpa.wayland.textinput:
virtual void QtWaylandClient::QWaylandTextInputv3::disableSurface(wl_surface*)
Trying to disable 0x... but 0x0 is focused". It comes from Qt's own Wayland
text-input code: a window gives up text input it never held. It changes
nothing in Chimera and cannot be fixed from here.

The window now turns off that one logging category (`qt.qpa.wayland.textinput`
warnings) before it starts Qt, unless the person set `QT_LOGGING_RULES`
themselves, in which case their rules are used as they are. Other Qt messages
are untouched.

Tested: the window builds and installs, and starts with `--headless`. The
message itself was not seen on a Wayland desktop from this change; that is
for the person who reported it to confirm.

## New Project waits for the project it makes (2026-10-30, issue 422)

With the build that shows a box for a project that does not open, the
reporter's Create still did nothing. The cause came before the open:

- Create closed New Project's window at once, and the project was then made
  away from it, since making it reads and hashes every file. The engine
  refuses a project with a file that is not one of the formats its slot
  declares. That reason went to the line of a window that was already closed,
  and nothing else happened. The reporter's file was a BBC Micro disk (.ssd),
  picked with "All files" because the MAME home package's slot did not list
  it (fixed in that core's own repository).
- The wizard's line never showed what its view model said. The page listened
  with onValueChanged, which Qt's QQmlPropertyMap sends only for a change made
  from QML, never for one the view model makes. Movie Import waited for its
  import the same way, so its window stayed open after an import as if
  nothing had happened. Encode Video's line was never cleared.

Now Create works as Chimera's Windows Forms wizard did. That wizard made the
project before it closed, behind a progress window, and went back to the
files page with the reason when the project was refused. The window now stays
open, its buttons off, until the project is made. It closes once the project
is made. A project refused for its files shows the files page with the reason
on the wizard's line; any other refusal is said on the page shown. A script's
or an agent's create_project is told the same: refused, with the reason,
where before it was told the project was made. The pages now listen for each
property's own change.

Measured:

- The view models' test makes a project with a file of another format. The
  wizard is told "Not made: file 'notes.txt' is not a format slot 'rom'
  accepts" and that it was for the files, and the agent is refused with the
  same reason. With the reason dropped on purpose, the three new checks fail.
- A new window test runs the window with New Project shown and a script
  making the project through the view model, as the Create button does. A
  refused project leaves the window open and opens nothing; a made one closes
  it and opens. It also checks that no page uses onValueChanged. With the old
  pages, the window stays open after a project is made, and both pages are
  named.
- Movie Import was run in the window with the synthetic core. Its window now
  closes and the core's notes are shown; with the old page it stayed open.

Not tried: the reporter's game, Windows, and pressing the Create button
itself (the tests tell the window how the making went; they do not click).

## Capture OSD writes the picture as the window shows it (user-decided, 2026-10-30)

The owner reported that Encode Video's Current size no longer said the size
of the video written, nor followed the window's 2x or 3x. Two things were
wrong. The "now" beside Current size showed the size the picture is shown
at (the declared size since issue 414: 320x240 for a 320x200 picture),
while the video was written at the machine's own pixels (320x200). And
Capture OSD drew the counters at the machine's own size, where the old
window wrote the picture area itself, at its size: its offscreen render
took the size of the window's picture control, so a window at 4x gave a
4x video with the counters sharp.

The owner chose the old window's way: with Capture OSD the video is the
frame as the picture area shows it (shape, bars, crop, filter), at that
area's size in square pixels, with what scripts draw on both surfaces and
the counters and messages as the window lays them out. Capture Lua alone
still writes the machine's own pixels with what scripts draw. The "now"
label and Current size say the size that will be written. The picture
item draws a frame as it shows it for the encoder, from the same fit the
window uses (DisplayFit), and the shown size is worked out once, in the
session (SessionViewModel::shownSize).

Two more of the old window's ways came back with it. A script's text
breaks at each new line, a line a row from the text's left, the text put
at the point as a whole, as the old GDI+ drawing did: gui.drawText,
gui.pixelText, gui.text and a form's drawString, in the window and in a
video. The Lua Console's list has lines between its rows and columns in
the theme's grid colour, as the old list (an input roll) drew them.

Tested: a new window test, "window drawing", plays the synthetic core's
project with a script that draws two lines of text both ways and encodes
three frames with Capture OSD: the video is the picture area's 512x480 (the
machine's picture is 128x120), the text is two rows in the video and in
the window, and the Lua Console's row has a line under it and two between
its columns. With the window's drawing at the machine's size, the text on
one line, or the grid taken out, five checks fail (the video's text is
then not looked at), and the video's two fail when only its text is
undone. The view models' encode
test hands back a picture three times the shown size and finds every frame
at that size; it fails when the size is ignored.

## RAM Watch and RAM Search have their grid again (user-decided, 2026-10-30)

The old RAM Watch and RAM Search lists were the same list control as the
Lua Console's, with lines between rows and columns; the new ones had none.
The owner asked for them after the Lua Console's came back. Both now draw a
line under every row and at the left of every column but the first, in the
theme's grid colour (RollGridLines), as the Lua Console's list does.

Tested: the "window drawing" test opens RAM Watch with three watches and
RAM Search after a new search, and finds in each window's picture a line
one pixel high under a row with two or more lines a row high standing on
it, as in the Lua Console's. With the three grids taken out, the three
checks fail.

## New Project looks at the cores folder each time it opens (2026-10-30, issue 423)

A person put a newer package of a core in the cores folder, in the place
of the old one, while the program ran. New Project still listed the old
package, read when the program started, so choosing it said the package
"cannot be read", and a project made with it would not open ("is not a
core package"). The cores folder was looked at again only by the Cores
window, and New Project relisted only when the number of packages
changed, which a swap leaves as it was.

New Project now looks at the folder each time it opens, as the Cores
window does, and relists after every look, whatever it found. A core
chosen whose package is gone, with another package of that core there,
starts over with that one.

Tested: the view models' wizard test renames the chosen package while the
wizard is open; after a refresh the same core and line are chosen and
choosing the line again reads the package. With the folder not looked at
again, choosing the line fails; with the core not started over, the
chosen line is lost (3 checks fail). Also seen on Windows: with the
package swapped while the program ran, choosing the core said
"...chimeraCore cannot be read" (build 36c2bffe).

The issue's other half, every word of the menu bar gone after the window
was closed, was not reproduced: the same steps on a GTX 1060, in English
and in Russian, left the menu bar's words in place.

## The crash record is put in place whole (2026-10-30)

The record of which window keeps a project's unsaved work against a crash
(session.json, beside the snapshot) was emptied and written again in place
at every snapshot, every five seconds while work is unsaved. The snapshot
beside it was already written to another file and put in place. A crash
in the moment between found the record empty, and a copy of the folder
taken then (the view models' test takes one) did too: CI failed twice that
way. The record is now written the way the snapshot is.

Tested: the view models' crash test checks that the record is another
file after the next snapshot (on Linux, by its inode); written in place
again, that check fails. The test's own copy of the record no longer
aborts the run when the process is not found in it: that is a check that
fails.

## A button let go is told where the pointer was (2026-10-30, issue 424)

Ruffle's Red Ball 2 did not take the first click: the reporter's project
holds the left button at one place from frame 262 to 286, and frame 287,
where it is let go, has the pointer at 0,0, back at its place the frame
after. Qt's PointHandler empties its point when the last button is let go
(no id, at 0,0, measured with Qt 6.12), and the window passed that on as
the pointer's place until the hover handler said where it was. A Flash
button let go away from itself is no click.

The pointer over the picture is now watched by one QML part,
PicturePointer.qml: a button let go is told where the pointer last was,
and a pointer that leaves stays where it was last seen.

Tested: a new test, "picture pointer", drives that part with Qt's test
module: a press and a release at 50,60 are told at 50,60 with no button,
and nothing is told at 0,0. With Qt's point passed on as it was, the three
checks of a release fail; the test passed 30 times on a loaded machine.
Red Ball 2 itself was not run here (the game is the reporter's).


## Every file dropped on a slot is added (2026-10-31, issue 425)

Several files dropped together on a slot of New Project's files page added
only the first. Each file added makes the view model build its list of
slots again, and the window then takes away the part that shows a slot and
makes a new one. The loop over the dropped files ran inside the old part,
so it stopped with an error after the first file ("Cannot call method of
undefined"). The loop now runs in the window itself, which stays, and the
file dialog's Add... goes through the same loop.

The rule for the window's QML: a loop that changes a list must not run
inside one of that list's rows.

Tested: a new test, "new project drop", opens the New Project window over
the program's own view model with the synthetic core and drops three files
on its slot with Qt's test module; all three are in the slot, in order.
With the loop back in the slot, one file is added and the check fails.
Not tried by hand on Windows.

## Auto-restore is off until asked (2026-10-31, issue 426)

TAStudio's "Auto-restore last position" was ticked in a new installation.
Chimera's TAStudio had it off until the person ticked it; the new window
had it on from its first version (b2e1b39d), with no decision behind it.
It is off again by default. A setting already saved keeps its value.

Tested: the view models' TAStudio test checks that a new session has it
off; with the default on, that check fails. Seven older view-model tests
were written with it on (an edit before the current frame was expected to
run the machine back to where it was); they now turn it on themselves.

## Report an Issue names the graphics card (user-decided, 2026-10-31, issue 427)

Help > Report an Issue left the graphics card for the person to type,
always. The old window's report named the driver when a core had drawn on
the card; that was lost when the window was rebuilt in Qt. And a report
made with a core that draws on no card (NP2kai, the reporter's) named
none, even under the old rule.

The owner decided the card is always named when it can be:
- a core that drew through OpenGL in this session: its card and driver in
  the driver's own words, "<renderer>, OpenGL <version>" (two new engine
  calls, ce_gl_renderer and ce_gl_version, give the driver's strings as
  they are: the description the engine already had is translated, and the
  report is written in the form's English);
- otherwise the card Chimera's window draws on, as Qt names it (its
  rendering layer, QRhi, which Qt gives with its private headers; the
  Linux build now asks for those, as the Windows build already did).

On a computer with two cards, the window's may not be the one a core
would pick, and a Vulkan core's card is not asked: the window's is
named for it.

Tested: the engine's GL session test checks that the driver's strings
are there while a context is (with Mesa on this machine); the view
models' report test checks that the window's card is on the line, that a
core's OpenGL driver comes before it, and that with no card known the
note is. With the strings not kept, or the report not using the card,
the checks fail. On Windows 11 with a GTX 1060
and no core running, the report's line read "Windows 11 Version 23H2
(x86_64), NVIDIA GeForce GTX 1060 6GB". Not tried: a report on Windows
after an OpenGL core ran.
