# Porting a core

What one person learned building sixteen of them. This is the practical
companion to [design-principles.md](design-principles.md) (why Chimera is the
way it is) and [waterbox-analysis.md](waterbox-analysis.md) (what the sandbox
is): the order the work actually goes in, and the traps that cost days.

Per-core reasoning lives in each core's own `docs/PLAN.md`. Read the closest
one before starting a new port - a Symbian port and a PS2 port share more than
you would think.

[new-core.md](new-core.md) is the route this craft is used on: what a core
request has to answer before any code, which existing core to start from,
what proves each step, and the publishing checklist.

## What you are building

Two builds of the same machine, and the claim that they are the same machine:

- **The native reference** - the emulator and a harness, built for the host, no
  sandbox. It is where you debug: gdb works, printf works, the GPU works.
- **The core** - the same emulator built for the miniBox guest toolchain and
  linked into `core.wbx`, plus the package that declares it.

Everything the gate does is comparing those two. If they agree instruction for
instruction on a real game, the core is deterministic in the only sense that
matters, because the sandbox is the thing that makes it reproducible and the
reference is the thing that says the sandbox did not change the answer.

## The shape of a core repository

    extern/<upstream>        submodule, pinned, never edited in place
    patches/                 numbered series, applied by waterbox/apply-patches.sh
    waterbox/                the adapter, the builds, the gate
      machine.{h,cpp}        the emulator, its clock, and the stepping loop
      wbx-entry.cpp          the guest ABI: Init, FrameAdvance, GetVideoBgra...
      run-native.cpp         the reference harness (flags for every diagnostic)
      run-wbx.c              the host driver that runs core.wbx like the engine does
      run-gate.sh            the legs
      waterbox.config        what the frontend is told about this machine
      file_slots.json        what the project wizard asks for
      package-licenses.json  what the bundle's LICENSES.md is computed from
    docs/PLAN.md             milestones, and the reasoning behind every decision

**Patches are all-or-nothing.** A clean upstream tree gets the whole series; a
dirty one is taken to already have it. Do not try to detect them one by one -
`git apply --reverse --check` on one patch fails the moment a neighbour moves
its lines. Every patch is a build option or a hook, never a deletion.

**Regenerating one patch in the middle of a series is a trap.** `git diff` on a
file two patches touch produces a patch containing both. Snapshot the files
before your edit and `diff -u` against that instead.

**Check the submodule is really there.** `git -C extern/<upstream> status` in an
empty directory answers for the repository ABOVE it, so a clone made without
`--recursive` will happily apply the whole series to your own working tree and
say nothing. Compare `rev-parse --show-toplevel` with the path you meant.

## The order it actually goes in

0. **Skeleton and a native build.** Get upstream building headless with no
   frontend, no host devices, no window system. Usually two or three patches
   (build options) and a stub for whatever backend picks a platform at compile
   time.
1. **Virtual time.** Find the one place the emulator asks the wall what time it
   is and make it a seam. Time must be bought with executed instructions, and
   idle must jump to the next scheduled deadline. The gate leg is: stall the
   host for 300 ms and prove the machine did not notice - and prove the check
   has teeth by running the same thing on the host clock and seeing it fail.
2. **The guest build.** The emulator through the musl guest toolchain. Expect a
   week of link errors and the traps below.
3. **The machine's filesystem.** Almost every emulator wants directories on a
   host. A core has files mounted by name and nothing else. Serving the
   machine's drives out of its own memory is usually less work than teaching
   upstream about a virtual filesystem, and it makes what a game writes part of
   the savestate for free.
4. **The picture.** See below - there are three ways and they are not equal.
5. **Input and audio.** Levels, not events: the host says what is held this
   frame. Audio is pulled at the end of the frame; pulling is not passive, it
   runs the machine's own callbacks, so the reference must pull too or the two
   flavors diverge.
6. **Savestates.** Usually free if everything else was done right.
7. **The package.** Declarations, keybinds, licences.
8. **Speed, if it needs any.** Measure before believing - see below.

## The guest toolchain, and what it refuses

- **Thread-local variables need an image that declares them.** A linked image
  with thread-local variables has a TLS segment, and miniBox gives each guest
  thread its own. `check-wbx.sh` fails an image that uses `%fs` without one,
  and it is right to: a write there does not fault, it lands wherever `%fs`
  happens to point. An Intel Mac needs two more things, as macOS gives a
  program no way to set `%fs`. Compile with the guest toolchain's flags, which
  carry `-mno-tls-direct-seg-refs`, so the code always loads the thread pointer
  first. After the link, run
  `python3 <miniBox>/source/guest/wbx-tp-rewrite.py core.wbx`: it lists in the
  image where the code loads the thread pointer, and an Intel Mac rewrites
  those loads as the core loads. An Intel Mac refuses an image with
  thread-local variables and no list, or with any other use of `%fs`, such as
  hand-written assembly (Mesa's x86-64 GL dispatch: turn Mesa's x86-64
  assembly off).
- **An aarch64 package.** A core is built for aarch64 on an aarch64 machine
  (CI: ubuntu-24.04-arm), never cross-built, with miniBox's flags for that
  CPU (miniBox's meson.build, `waterbox_cpu_flags`: among them
  `-ffixed-x18` and `-ffp-contract=off`). After the link,
  `wbx-tp-rewrite.py` must run on `core.wbx`: on aarch64 it points the code's
  thread-pointer reads at the machine's slot, and the host refuses an image it
  has not rewritten ("reads its thread pointer from TPIDR_EL0"). Run it
  wherever `core.wbx` is built, a CI step that calls ninja directly included.
  The native reference build on aarch64 needs `-ffp-contract=off` too: GCC
  fuses multiply-adds there by default, and the two builds' sound then
  differ. x86-only code goes per CPU: `-msse2` and other x86 flags, a
  `config.h`'s ARCH defines, a hand-written stack switch (a coroutine or
  libco's `amd64.c`), a recompiler that only emits x86 code. A
  `waterbox.config` written by a generator needs `runsOn` in the generator.
  The x86_64 `core.wbx` should be the same file before and after; check it.
- **One thread.** The machine's scheduler is the only scheduler. Thread pools,
  lazy background workers and "spawn a thread to do the IO" all have to go or
  become synchronous.
- **No timed waits.** A futex wait with a timeout parks the only thread and
  nothing ever wakes it: miniBox prints `all threads fell asleep. states: t1=W`
  and traps. Any queue an owner drives must have a non-blocking take. This one
  cost an afternoon on a one-microsecond `pop(1)`.
- **Weak symbols resolve to zero.** A library that declares
  `pthread_mutexattr_init` weak as a build workaround gets address zero in a
  static guest link and calls it. Force them with `-Wl,-u,<name>`.
- **musl's `open()` asks the kernel for `O_CLOEXEC` with a second raw syscall**
  that no libc-level definition can shadow. If the sandbox does not implement
  it, wrap `open` and strip the flag before the syscall.
- **Syscall gaps are normal.** `closefrom`, `clock_getres`, `getcwd`, `mkdir`,
  `lstat`, `fcntl`, `sched_getaffinity`... each shows up as an ENOSYS crash the
  first time a new upstream reaches for it. Add them to a `guest-syscalls.cpp`
  as they appear.
- **Arena sizing: grow the MMAP arena, not sbrk.** musl sends every large
  allocation to mmap, so that is the one that runs out; miniBox prints
  `sbrk heap exhausted` when the heap really is the problem, and the ABSENCE of
  that message with an out-of-memory failure means mmap. The layout is
  `{sbrk, sealed, invisible, plain, mmap}` in `waterbox.config`.

## Debugging inside the sandbox

- A guest fault prints a `rip=0x36f0...`. The guest links at `0x36f00000000`;
  `addr2line -f -C -e core.wbx <rip>` names the function.
- gdb works on the host driver, but miniBox uses SIGSEGV for its own memory
  tricks: `handle SIGSEGV nostop noprint pass` first, or you will "catch" the
  sandbox working correctly.
- A guest that hangs rather than faults is usually the timed-wait trap above.
- Chimera stops a core that does not finish a frame in 20 seconds, or does
  not finish starting in 2 minutes, and shows "the core did not finish a
  frame in 20 seconds". The first frame after starting, and the first after
  every state load, has the 2 minutes too: a core may analyse or compile a
  game's code there, or rebuild what a load threw away. A core that is working is not stopped: every system
  call other than a sleep, a yield, a clock read or a futex call counts as
  work and starts the time again, and so does every call through the GPU
  bridges or to the compile cache, once it has been answered (a shader
  compile that took long is not a stuck core). What is stopped is a core
  spinning in its own code, or one only polling a clock or yielding. While you debug, `CHIMERA_FRAME_TIMEOUT_MS=0` and
  `CHIMERA_START_TIMEOUT_MS=0` turn the limits off; other values set them in
  milliseconds.
- Build the reference with the same change and reproduce there first. Most
  "sandbox bugs" are ordinary bugs that the sandbox merely made visible.

## Three ways to get a picture, in order of preference

1. **Read the machine's own memory.** If the emulated machine has a
   framebuffer the guest paints (any direct-screen-access console, most
   handhelds), read it and convert. No graphics driver, no GPU, and the
   sandbox's picture is the reference's byte for byte by construction.
2. **Compile an OpenGL into the core.** Mesa's OSMesa front end with the
   **softpipe** driver and LLVM off: plain C, no JIT, no dispatch on host CPU
   features, so every machine draws the same picture. This is what a core needs
   when the emulator composes on the GPU. It costs a few MB of package and is
   slower than a hardware path, and it is the only way a window-server-style
   renderer works in a sandbox at all.

   **The core builds its own Mesa** - Chimera carried one as a submodule and
   does not any more, for the same reason it stopped carrying cores. Copy a
   `waterbox/setup-mesa.sh` from a core that has one: they all pin mesa 24.0.9
   by SHA256 and build it into `build/mesa`. There are two flavours, and which
   you want depends on how the core draws:

   - **eka2l1's** additionally applies five small patches (no thread pointer,
     one CPU, no x86-64 dispatch stubs) that a window-server compositor needs;
   - **pcsx2's and flycast's** build it unpatched, which is what a core linking
     the GL renderer through `-Dmesa_guest_dir` uses.
3. **The GPU bridge** - the guest's GL calls leave the sandbox onto a real
   driver. Fast, and [gpu-bridge.md](gpu-bridge.md) explains it, but note what
   it costs: **a savestate made with GPU state alive is good only in the
   session that made it**. For a TAS core that is a serious constraint, not a
   detail.

Whichever you choose: **prove the renderer does not perturb the machine.** Run
the same game with the renderer and without and compare the instruction count.
If it changes, the picture is part of the machine and the core is not portable.

## Savestates

In a waterbox they are arena snapshots, so they usually work the day the core
works. Two legs prove it and both are worth having:

- **Rerecord**: save and load before EVERY frame; the run must be identical to
  one that did not. Anything of the machine living outside the arena shows up
  here.
- **Session**: save in one process, load in another, and land where the
  uninterrupted run did. This is what a movie asks of a core, and it is the leg
  a core holding a graphics context fails.

Things that break them: host pointers stored in guest memory (some emulators do
this deliberately - it is fine inside the arena, but it makes raw memory differ
between flavors), GPU objects, and anything the core caches across `Init`.

A core that draws on a real card needs a third comparison, and the two above
do not make it: **a run with a rewind in it against a straight run, on the
card**. Play a movie straight; play it again with a state taken every few
hundred frames and none loaded; play it a third time with one rewind
(`chimera-run ... --gpu --greenzone 32768 --greenzone-period 300
--rewind-loop <frame>,1`). All three must end with the same machine. Compare
a memory domain (`--dump <domain>=<file>`), or, for a core that has only a
bus, the fingerprints `--dump <bus>=<file>` writes: eight bytes for every
64 KiB of the address space, so two files differ exactly where the machines
do; `--dump '<bus>@<hex address>+<hex length>=<file>'` then gives the bytes
of one place to read.

What that comparison found in two cores (2026-10-13), each a thing to look
for in a new one:

- **The card's answer in the machine's memory.** An emulator that asks the
  real card how many pixels were visible (an occlusion query) and writes the
  answer when it arrives has a machine that depends on the card and on the
  moment. Give the made-up answer, or wait for the real one at a fixed place.
- **The host's random numbers.** In the sandbox `getrandom` is a fixed
  sequence, but C++'s `std::random_device` does not ask it: built for x86 it
  uses the processor's own instruction (RDRAND), which nothing can stand in
  front of. Two runs of one movie then differ. Give the emulator a sequence
  of its own, kept in guest memory, and check that no object of the guest
  build refers to `std::random_device`.
- **Turns handed out by the renderer's rebuild.** After a load the renderer
  is made again; if that waits through the emulator's own scheduler, the
  machine's threads run beside it and the rewound run is no longer the
  straight one.

**A guest that reads its dead stack runs differently on Windows unless the
package says so** (issue 555). On Windows the sandbox learns that the guest
wrote a page by letting the write fault, and Windows puts the fault's
record, about 2 KB of it, in the guest's stack just below where it is
running. Linux puts nothing there. Code reads that memory when it reads a
local it never set, a frame's padding, or, above all, when a conservative
garbage collector scans its stack for pointers: a saved register left there
can keep an object alive in one run and not in another. Where the faults
fall depends on the greenzone's settings, so the same movie plays differently
on Windows with different settings. A core whose guest does this declares
`"readsDeadStack": true` at the top of `waterbox.config`. On Windows its
writes are then found by comparing pages instead, so nothing is written into
its stack. That costs speed while the greenzone captures frames: Mesen
marked this way fell from 644 to 140 frames a second capturing every frame
on a GTX 1060 PC. Declare it only for a guest that needs it. To find out:
play one movie on Windows with the greenzone off and with it capturing
every frame (`chimera-run ... --greenzone-period 1`), and compare the
domains or the final states (`--final-state`).

## The gate

One command, and every leg says what it compared.

**Read [gates.md](gates.md) before writing a leg.** It is the seven ways a gate
goes green on a broken thing, each one taken from a mistake this project
actually shipped, and the rule they all come down to: a leg that has never been
seen to fail is a leg that has not been tested. Breaking the thing on purpose
and watching the leg go red takes minutes and is the highest-value act in
testing.

Advice earned the hard way:

- **Name the test content.** A leg that takes "the first `.rar` in the folder"
  changes meaning the day the user adds a game, and three legs go red for
  reasons that have nothing to do with the commit. Name the game, fall back to
  a glob, and print which one ran.
- **A game that does not work is a finding about that game**, not a broken
  gate. Keep the gate on content that works and record the rest in PLAN.md.
- **Compare the machine, not just the picture.** Instruction counts catch
  divergence long before pixels do.
- **Upstream's own test suite is a leg.** So is its differential CPU harness if
  it has one: it tests the interpreter you actually ship.

## What the package declares

- **`waterbox.config`** - the machine: name, system id, video (buffer capacity
  and the live size the core reports per frame), audio, vsync as a rational,
  the memory layout, the button list, settings, firmware. A package with no
  vsync (or a zero in it) runs at 60/1. A picture larger than the declared
  buffer is cropped to it, and said once on stderr: declare the largest the
  core can draw. It is room, not a buffer held (2026-10-19): the engine and
  the window keep as much as the largest picture the core has drawn, so a
  core with an internal resolution (DuckStation's 16x, 12288x9216) costs a
  session at 1x nothing for it. That needs `GetVideoWidth`/`GetVideoHeight`:
  without them the declared size is the picture. The engine refuses a package whose sizes are outside what it
  can hold (2026-10-18): width and height 1 to 16384 (the virtual size too),
  `audio.samplesPerFrame` 0 to 1048576, `audio.channels` 1 or 2.
- **The sound's rate is the chip's.** `audio.rate` is the rate the core mixes
  at, and Chimera converts it to the 44100 Hz it plays and encodes at (the
  engine's `ce_session_sound`; `ce_session_audio` stays the core's own). In a package of several machines, one whose
  chip runs at another rate states its own: `"audioRate": 64000` in its entry
  of `machines` (issue 226). Check it by counting: the samples a run produces
  over its frames must be the rate over the frame rate.
- **Every text a person reads is a key** of the package's language tables,
  `waterbox/languages/<code>.json` copied into the package as
  `languages/<code>.json`, one for every language Chimera has (owner,
  2026-10-28). That covers the declarations' names, descriptions and labels
  and what the code returns: the load error, a suggestion's note, an
  import's notes and refusal, the game's settings, its properties'
  descriptions. A key's arguments follow it after a 0x1F byte. How, and what
  stays a name: [translations.md](translations.md), "A core's texts".
  `tools/check-core-texts.py <core checkout>` checks the code's keys
  against the English table.
- **Descriptions are written in plain language** (issue 241). A user reads
  the `description` of a setting, a firmware entry and a machine preset, and
  the `help` of a file slot, in Chimera's windows. Write short sentences,
  use no project term without saying what it means, and say the same fact
  the same way every time: "It is part of the machine, so a movie needs the
  same value." or "It changes the picture only.".
- **What things are called is yours to say.** Chimera keeps no table of
  systems or of controls; a package that says nothing is shown by its ids.
  - `"systemNames": { "PSV": "PlayStation Vita" }` at the top - the name of
    every system id the package answers to (its own and each machine's).
    The same names go in the core's row of `official-cores.json`, which
    Chimera does not read: it is what CI and the documentation go by.
  - `"mnemonics": { "Select": "s" }` in each input declaration (the
    package's, and each machine's) - the one character a button writes into
    a movie's text and heads its input column with. Keyed by the button's
    whole name, or by its name without the player ("P2 Up" is found under
    "Up"). One printable ASCII character, never `.` or `|`. Declare one for
    every button: a button left out gets the rule's guess (the first letter
    of its last word), which is how two columns of one pad come to share a
    letter. An entry is read by position, so a letter may change without
    harming a movie made before.
  - `"headers": { "Keyboard CAPS SHIFT": "CS" }` beside the mnemonics, for a
    machine whose buttons one character cannot tell apart - a keyboard. It
    is what heads the button's column in TAStudio and fills its pressed
    cells; the movie's text still carries the mnemonic and nothing else.
    Keyed as a mnemonic is. One to eight characters of printable ASCII, no
    space at either end, and not the header of another column of the same
    player. A button given none is headed by its letter, so a pad needs
    none of this.
  - `"header": "LX"` on each axis - its column's header, five characters at
    most.
  - `"players": [ ["P1 Up", "P1 A"], ["P2 Up", "P2 A"] ]` in each input
    declaration - whose each control is: the first list is player 1's
    controls, the next player 2's, and any control in no list is the
    console's (owner, 2026-10-28). It decides the `|` groups of a movie
    entry (the console's first, then each player's), the thick lines
    between players in TAStudio's piano roll, and the tabs of Config >
    Controllers. Axes are named here as buttons are. A package that
    declares none keeps the old rule: the `P<n> ` in front of a name, else
    the console. Declaring it differently from the names changes the groups
    new movies are written with; a movie made before is still read by the
    columns its own header names.
  - `"tastudioColumns": ["Key ESC", "Key RETURN", "Reset"]` in an input
    declaration (the package's, and each machine's) - the controls
    TAStudio's piano roll shows when a project has no piano roll of its own
    yet; every other button and axis starts hidden, and a person shows it
    from the Columns menu (owner, 2026-11-07, issue 544). Without it,
    Chimera's rule decides: it hides power, reset, disk and tape controls
    and every control whose name starts with `Key `, which on a computer
    hides nearly every key a game uses. A project's saved piano roll wins
    over both. The contract tests refuse a name that is not one of that
    input's buttons or axes.
  - `"picture": "pictures/pad.png"` in an input declaration (the package's,
    and each machine's) - a PNG of the controller inside the package, shown
    beside the bindings in Config > Controls (issue 338). Draw it yourself or
    take one that is free to redistribute, and name its file in a component
    of `package-licenses.json` with its licence and source; the contract
    tests refuse a picture that is missing, not a PNG, or unlicensed.
  - `"media": [ ... ]` - what a disc of yours needs that a plain image does
    not have, for Tools > Media Maker: `{ "id", "label", "format":
    "iso9660", "when": { "rootFile": "PS3_DISC.SFB" }, "systemArea": [ {
    "at": 0, "u32be": 1 }, { "at": 12, "u32be": "lastSector" } ] }`. The
    recipe is offered when the folder has the root file; the engine writes
    the patches into the image's system area.
- **`runsOn` is required.** In the top level of the packaged `waterbox.config`,
  declare a nonempty array of host systems, for example
  `"runsOn": ["windows-x86_64", "linux-x86_64"]`. The only accepted names are
  `windows-x86_64`, `windows-aarch64`, `linux-x86_64`, `linux-aarch64`,
  `macos-x86_64` and `macos-aarch64`. Chimera refuses a package when the array
  is missing, empty, or contains an unknown name, and opens it only on a host
  it lists. The Linux container used to build a package is not necessarily the
  host it runs on, and the package's filename does not declare its systems.
  List only what the core has been built for and is meant to run on: one
  `waterbox.config` can serve both CPUs' packages, so it names both CPUs if
  the core is built for both. Leave a system out when the core cannot run
  there (Ikemen GO has no `macos-x86_64`: its Go code reads thread-local
  variables in a way an Intel Mac cannot rewrite). If the config is generated,
  add `runsOn` to the generator, and check the `waterbox.config` inside the
  finished `.chimeraCore` archive. `publish-core.sh` refuses a package that
  lists no system for its own CPU.
- **`version` and `versionDate`** are not yours to write: `build-package.sh`
  stamps both into the packaged `waterbox.config` - the commit, and the date of
  that commit in UTC (never the build's date, which would make one commit two
  packages). The frontend shows them together wherever a version is listed.
- **`file_slots.json`** - what the wizard asks for. Think about what the
  project IS: usually the game, with the machine's ROM as firmware, because one
  ROM serves every game.
- **Firmware** is declared by the core, resolved and remembered by the
  frontend, and mounted in the guest under its ID - `fopen("sym.rom")`. A
  firmware needed only in some cases carries `requiredWhen`
  (`{"slot": "game", "extension": "blz"}`, a setting's value, `any`/`all`/`not`).
  Optional firmware does not exist; if it is not always needed, spell the
  condition.
  An entry that pins no `sha1` (a dump every console writes differently) can
  list `nameHints`, pieces of a file name that mark it (`["7i"]`, `["nand"]`):
  when its `name` finds nothing, the one file of its `size` whose name holds a
  hint answers it, and with several the person chooses (issue 349).
- **`package-licenses.json` is not optional.** Every component, its terms, its
  source link, and its licence text travel with the package; the bundle's
  LICENSES.md is computed from them, and a package that declares nothing stops
  the build. If you compile someone's code in - a whole Mesa, say - declare it.
- **A setting's `type`** decides what the settings grid lets a person enter
  and what the core reads from the `settings` file (issue 358):
  - `bool`, `string`, `float`, and `enum` (one of its `options`).
  - The whole numbers hold what their C types hold: `int` from -2147483648
    to 2147483647, `uint` from 0 to 4294967295, `long` and `ulong` 64 bits
    with and without a sign. `min` and `max` narrow any of them. Every
    digit reaches the core, past the 2^53 a JSON reader that turns numbers
    into doubles keeps. Read them with `wbx_setting_long`, and a `ulong`
    with `wbx_setting_ulong`.
  - `datetime` is a date and time in UTC. The grid shows it, and takes it
    typed, as `2000-01-01 00:00:00`. The core reads the number of seconds
    since 1970-01-01 00:00:00, a 64-bit whole number, so 2038 is no limit.
    Its `default`, `min` and `max` are written as that number. A core that
    already reads a clock as seconds in an `int` setting can change the
    type to `datetime` and nothing else: the values stay the same.
  - With no `type`, it is inferred: `enum` when there are `options`,
    otherwise from the default (a whole number past an `int` is a `long`).
    The package tests refuse a type Chimera does not know, which used to be
    taken for text.
- Settings that shape the machine are part of the reproduction contract. There
  is no such thing as a cosmetic setting here.
- Two places to keep in step: the vsync rate in `waterbox.config` and whatever
  the guest returns from `GetVsyncNumerator`/`Denominator`.

## Optional tooling the frontend will use if you export it

Probed once after `Init`; absent exports simply mean the tool is not offered.

- **Calls during a frame: each read of the controls** (issue 344) -
  `void SetInputPollCallback(uint64_t fn)`. The engine hands over a function
  in one of miniBox's callback slots before `Init`; keep it in an
  `ECL_INVISIBLE` variable (it is the same slot address in every session and
  no savestate carries it) and call it - as
  `((uintptr_t (*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,
  uintptr_t))fn)(0, 0, 0, 0, 0, 0)` - each time the machine reads its
  controls, where the core already notes the read for lag frames. A Lua
  script's `event.oninputpoll` then runs there, part way through the frame;
  a core without the export refuses it. The call costs nothing when no
  script listens: the engine returns at once. The synthetic core
  (`tests/synth/package-box/synth_wbx.c`) is the example.
- **Calls during a frame: memory the machine runs, reads or writes** (issues
  496 and 534) - four exports, all of them or none, and a fifth that is
  optional.
  - `const char *GetMemoryCallbackScopes(void)` names the address spaces the
    core can watch, its "scopes" (a CPU's bus, a kind of memory), a line each,
    its main one first.
  - `void SetMemoryCallback(uint64_t fn)` is handed a function in callback
    slot 4; keep it as the input poll's is (`ECL_INVISIBLE`).
  - `int32_t AddMemoryWatch(int32_t kind, int32_t scope, uint64_t address)`
    and `void RemoveMemoryWatch(...)`, with the same arguments, start and
    stop watching an address of a scope (`UINT64_MAX`: every address) for a
    kind of access: 0 an instruction about to run, 1 a read, 2 a write. Add
    gives 1 when the core watches it, and 0 when it cannot (a kind or scope
    it lacks, or more places than it keeps); a script that asks for it then
    gets an error. The engine adds a place once however many scripts watch
    it, and removes it when the last one stops. Keep what is watched in
    `ECL_INVISIBLE` memory too, so a load leaves it as it is.
  - `int32_t GetMemoryCallbackReplaceable(void)`, optional: the kinds of
    access whose value a script may change, bit `1 << kind` each (2 reads, 4
    writes, 6 both). Without it, or with 0, the core only reports.
  - `const uint64_t *GetMemoryCallbackPc(void)`, optional (issue 571): the
    address of a `uint64_t` in the core's memory that holds, at each call,
    the address of the instruction making the access (for an instruction,
    its own), in the address space the core's traces and registers use.
    Keep it `ECL_INVISIBLE`. The MCP server's `trace_memory` then says which
    instruction wrote a value; registers read during the call may already
    be past it. The synthetic core sets it in its step hook.

  At each watched access, call `fn(kind, scope, address, value, size,
  replacement)` through the same six-argument signature as the input poll:
  - **address**: the place the program used, in the scope's own numbering,
    as the bus sees it after any mirroring the core does. A watch on an
    address matches an access whose first byte is there; a wider access over
    it does not.
  - **value**: the number read or about to be written, as the CPU holds it
    (not as its bytes lie in memory), 0 for an instruction.
  - **size**: the access's width in bytes (1, 2, 4 or 8), 0 when the core
    does not know it. An older core passes 0.
  - **replacement**: 0 when this access's value cannot be changed (always for
    an instruction, and for a kind the core did not name in
    `GetMemoryCallbackReplaceable`). Otherwise the address of a `uint64_t` in
    the core's memory holding the value; after the call the machine goes on
    with what it holds, which a script may have changed. The engine cuts a
    new value to `size` bytes.
  - **when**: an instruction before it runs, with every register as it is
    then (the program counter and flags too, where an interpreter keeps them
    in locals: write them back first). A read once its value is fetched and
    before the CPU uses it; a write before memory changes. A core that offers
    no replacement may report a write after memory changed.
  - **order**: calls come in the order the machine makes the accesses. Say in
    the core's notes what is not covered: another processor, a DMA copy, an
    instruction fetch.

  A Lua script's `event.on_bus_exec`, `on_bus_exec_any`, `on_bus_read` and
  `on_bus_write` run there, part way through the frame, given the address,
  the value and flags made from the kind and size; a core without the
  exports refuses them. A function there may read and write memory, but
  cannot advance, save or load a state, or close the core: Chimera refuses
  that with an error. Look for a watched place only while something is
  watched, since every access pays for the look. A core whose CPU recompiles
  the game's code can watch only what its recompiler lets it reach; offering
  reads and writes without instructions is fine. The synthetic core is the
  example (`tests/synth/package-box/synth_wbx.c`: scopes Code and RAM, sizes,
  reads and writes changeable).
- **Registers** - `int32_t GetRegisterCount(void)`, `const char
  *GetRegisterName(int32_t)` and `int64_t GetRegisterValue(int32_t)`, with
  `GetRegisterBits` and `SetRegisterValue` optional: the debugger's register
  box, Lua's `emu.getregister` and the MCP server's `registers`. And
  optionally (issue 571) `const char *GetRegisterRoles(void)`, a line each,
  `role register [segment [base]]`: which register is the program counter
  (`pc`), the stack pointer (`sp`) and the flags (`flags`), by the names
  `GetRegisterName` gives; on a segmented processor the segment register
  paired with it, and a register holding that segment's linear base, so the
  linear address is base plus value without Chimera knowing the processor's
  rule (give the base as a register the core works out, real mode or not).
  `pc EIP CS CSBASE` is a line of a PC-98's. An agent and a script then ask
  for "pc" on any core that says. The synthetic core says `pc PC`.
- **Memory domains** - a pointer and a size, published once. The memory tools
  read a domain through a second view of the core's memory that never faults,
  so a core that watches pages by protecting them (`GuestFaultHandler`, as
  RPCS3's texture cache does) is not told of a tool's read; a tool's write
  goes through the core's own pointer, and is. If the machine's
  memory is not a block (paged, per-chunk, or not allocated until the game
  runs), export a **bus** instead: `GetBusCount/Name/Size/Writable`,
  `PeekBus`, `PokeBus`, resolved per access. Cache one page of translation,
  and export `ReadBus` as well: `const uint8_t *ReadBus(int32_t bus, int64_t
  addr, int32_t len)` fills up to 64 KiB at once and answers a pointer to it,
  in an `ECL_INVISIBLE` buffer so what a tool read is no part of a savestate.
  RAM Search reads the whole bus, and without it every byte is a call into the
  sandbox: EKA2L1's 64 MB bus took 3 s a search that way and 0.2 s with it
  (chimera#180). A core without it still works; the engine peeks for it.
  The memory tools and scripts list a bus as a domain, after the domains
  (chimera#456): RAM Search, RAM Watch, the Hex Editor, freezes and Lua's
  memory library read it through `ReadBus` or `PeekBus` and write it through
  `PokeBus`. A bus with the name of one of your domains is left out of that
  list.
  **A bus that is mostly nothing says what is live**: a bus's size is read
  once, so one over a heap arena is a gigabyte whatever is mapped in it, and a
  RAM search pays for every byte. Export `const int64_t *GetBusRanges(int32_t
  bus)` - (address, length) pairs in an `ECL_INVISIBLE` buffer, ended by a pair
  of length 0, null for a bus you have no word on - and a search covers those
  ranges alone, as they were when it started (chimera#218). Reads and pokes
  are unchanged: an address outside them still reads as you read it.
- **A domain that is only a mailbox** - somewhere you copy a value a game
  property points at, because it has no one place in the machine - is marked
  with `int32_t GetMemoryDomainHidden(int32_t index)`. Properties read and
  write through it; no memory tool lists it. Keep its bytes in memory that is
  in no state, like every answer to a tool. The same export hides **a block a
  bus already shows**: ares publishes the Nintendo 64's memory as blocks in
  its own storage order, for its tests, and as buses in the console's order,
  under the same names; listing both put RDRAM in the lists twice, with the
  two disagreeing on every value wider than a byte (chimera#288). Hide the
  copy a person should not search, and list the buses in the order a person
  wants them, the main memory first: a tool opens on the first one.
- **Which stack a tool's call runs on.** Every export the host calls - a
  frame, and everything asked between frames - runs on the guest's one main
  stack, from its top (miniBox, docs/MACHINE-SPEC.md: both guest stacks are
  invisible, in no savestate, and never cleared). So what a call leaves below
  the stack pointer is there for the next call to find, and it is NOT what was
  there when a state was saved. A machine must never depend on it. If the code
  you wrap reads stack it did not write - a binary blob can - give every
  host-called export a stack of your own in invisible memory, or the run in
  which a tool asked something is not the run in which it did not.
- **Drives** - `GetDriveCount/Name/Light`: one entry per medium the PROJECT
  put in the machine, lit on a frame it was read or written. Report none rather
  than a light that can never come on. **If a drive swaps between images, say
  what it holds**: `GetDriveMediaCount/Name/Selected/Inserted`, all four. The
  status bar then shows which image is in the drive and, when it is another
  one, which the selector is on. SELECTED and INSERTED are separate because
  machines keep them separate - a changer whose Next moves a selector and whose
  Swap inserts is, in between, reading one disc and pointing at another - and
  Inserted is -1 for an empty drive. A core whose swap is one step returns the
  same index for both. Keep both in guest memory like any other machine state:
  a rewind has to take them back.
- **`StateLoaded()`** - told after every load of the machine: a savestate, a
  branch file, a greenzone restore (anchor and deltas applied), with the
  machine stopped and before it runs again. For a core that keeps something
  DERIVED from guest memory in memory a state does not carry (`alloc_invisible`)
  and has to throw it away when what it was derived from is replaced. xemu's
  translated-code cache is the case: 215 MB of every state was TCG output
  that any restore can regenerate, so the buffer is invisible and this export
  flushes it. Nothing a state needs may live there - only what the machine can
  rebuild from its own memory - or a state loaded in another session runs on
  a cache of the wrong machine.
- **`StateSaving()`** - the other end: told before every state the engine
  takes of the machine - a greenzone capture, a savestate, a branch's state
  file - with the machine stopped between frames, and not for a frame the
  history decides not to store. A core that dies in it gets no state taken:
  the capture stores nothing and the save fails with the core's reason. For a core part of whose machine is somewhere
  a state does not reach until it is brought back. xemu is the case: its GPU
  draws into surfaces that live in the driver, and writes one into the
  console's RAM only when the processor reads it, so a state taken without
  this held the RAM of a machine whose screen was elsewhere and a load lost
  what had been drawn (chimera#190). What the export writes is in the state
  that follows, and in the delta. It may draw on the GPU bridge to do it; the
  engine gives the context back afterwards as it does after a frame. It must
  leave the machine one the guest cannot tell apart from the one it would
  have been untold - whether a state is taken is the frontend's business,
  and a movie has to play the same with a greenzone and without.
  `CHIMERA_NO_STATE_SAVING=1` leaves every core untold, for measuring.
- **`SuggestSettings()`** - what the core would choose for a game before one
  boots, declared with `"suggestSettings": true` in `waterbox.config` so the
  frontend never loads a core that has nothing to say. It is called INSTEAD of
  `Init`, on exactly the mounts a run would have, of the project the New
  Project window has made so far (`ce_project_suggest_settings`;
  `chimera-run --project <p> <package> --suggest` asks it from a shell), and
  returns a JSON object: `{"values": {setting: value, ...}, "note": "..."}`.
  The new-project wizard asks when it reaches the settings page, puts the
  values into the settings the way a preset is applied, and shows the note
  above the grid - so the note must say where the values come from, or that
  the game was not found. The RPCS3 core answers from the RPCS3 wiki's per-game
  recommendations it carries; its note names the page, the compatibility
  status and the licence. Read the files, answer, return: nothing that boots,
  and nothing a later `Init` would find changed. **What is mounted when you
  are asked**: the game, the other files picked for the project under their
  names, the slot map (`slots`), the track files of every `.cue` among them,
  and the settings chosen so far - the machine among them, in a package of
  several. So a disc can be read: the Mednafen core reads a Saturn disc's
  region to say which BIOS it needs. Firmware is not mounted then (the
  wizard asks before it is chosen).
- **`RunTool()`** - the core's own tools, declared under `"tools"` in
  `waterbox.config` (docs/project.md, "A core's own tools"): each is an item
  of the window's Tools > Core-Specific Tools, offered whether a project is
  open or not, a job whose result is files. It is called
  INSTEAD of `Init`, with no game, in a process of its own
  (`ce_package_run_tool`; `chimera-run --run-tool <id> <package> --input
  <name>=<path> --output <name>=<path>` runs one from a shell). The file
  `tool` says which tool and which options, and the name each picked file
  had; each input is mounted under its name, and each output is a writable
  file the core opens and writes, which goes straight to the disk. No game
  is mounted, not even an empty `rom`: name the files anything but `tool`
  and `settings`. Return
  `{"done": "<key>"}` or `{"refused": "<key>"}`, keys of the package's tables
  with their arguments after 0x1F. Close what you write before returning.
  The Eden core's Decompress NSZ is one; the DSDA core's demo import, which
  writes a project, is another. An output declared `"project": true` is
  written as two files, `<name>.json` (a project's `project.json`, its
  `inputs` holding `columns` only) and `<name>.input` (one frame's input a
  line), which Chimera makes into the project's zip (docs/project.md).
- **`GetGameSettings()`** - the settings THIS GAME has beyond the package's
  declaration, for a core whose games carry their own (an arcade game's dip
  switches differ game to game, so they cannot be declared once per package).
  A JSON array of declarations in `waterbox.config`'s own setting format,
  read once after `Init`; the settings grid of a loaded project shows them
  after the package's. The same array returned from `SuggestSettings` under
  `"settings"` puts them in the new-project wizard before any machine runs,
  and a different game takes them away again. Their values are ordinary
  settings: mounted for `Init`, recorded in the project, cited by a movie. A
  declaration may not reuse a package setting's name - it is dropped. The
  FBNeo core names its switches `dip.<group>`.
- **The firmware THIS GAME needs** - for a core of thousands of games whose
  other files depend on the game (an arcade game's BIOS set, its parent's set,
  the sets of the devices its board has), which cannot be declared once per
  package. Declare `"gameFirmware": true` in `waterbox.config` beside
  `"suggestSettings": true`, and return the declarations from
  `SuggestSettings` under `"firmware"`: an array in `waterbox.config`'s own
  firmware format (`id`, `display`, `description`, `name`, and `requiredWhen`
  where an entry is needed with some settings only - a BIOS set that the
  game's own file already holds for the default BIOS and not for another).
  The new-project wizard asks for them on its firmware page with the
  package's own, and a load asks the core again before it builds the
  machine, so a game opened with no project is completed from the firmware
  the person has already given. A file is mounted under its id, so make the
  id the name the emulator looks for. Three rules: the answer must not depend
  on whether the firmware is already mounted (the wizard asks before it is,
  a load asks after); an id the package declares is the package's; and
  firmware is read into memory, so a disc image is a slot's file, never
  firmware. The MAME core is the example (`mame_game_firmware`).
- **`GetButtonLabel(index)` / `GetAxisLabel(index)`** - what THIS GAME calls
  a control: "Weak Punch" for the control the package declares as "P1 Button
  1". The declared name stays the control's identity - a movie's column, a
  key binding and a script go by it, so it cannot change from game to game -
  and the label is shown beside it: in the hover text of the control's
  TAStudio column and after its name in the key configuration. Asked once
  after `Init`, with the control's index in the declaration; return `""` for
  a control the game has no word of its own for. Leave the player out
  ("Attack", not "P1 Attack"): the name beside it already says which
  player's.
- Registers, trace, core-rendered surfaces, save-data export, turbo
  (`SetRenderingEnabled`). **Turbo means "skip what is pure OUTPUT", not "skip
  the renderer".** If the export can only be implemented by skipping drawing -
  which is the case for a renderer whose picture lives on a GPU outside the
  sandbox, because what it draws persists there and a frame skipped is a
  picture lost - then declare `video.drawEveryFrame` and let it skip the
  readback alone. docs/gpu-bridge.md has the measurements.

## Publishing it

Chimera ships no cores, builds none and downloads none: a core is a repository
that publishes itself, and a user downloads its package and puts it in the
cores folder (docs/core-manager.md). Joining the official set is therefore four
things:

1. Push the core repository (`ToolAssisted-run/chimera-core-<name>`, branch
   `main`).
2. Give it a `chimera.yml` that gates the package on a public runner and
   uploads it as an artifact named `<name>-${{ github.sha }}`. **This is the
   part to think about**, and it is a content problem before it is a CI
   problem: a gate needing a BIOS or a commercial rom cannot run there, so what
   CI proves has to be designed around what may be distributed. The published
   cores split three ways, and it is worth checking which one you are in before
   assuming the worst:

   - **the machine is its own content** - DOSBox-X boots to a DOS prompt with
     no disk and its gate builds the disks it needs, so the whole gate runs;
   - **upstream ships a test suite** - Ruffle's own `tests/tests/swfs` are
     vendored and free, so the whole gate runs against those;
   - **nothing may be distributed** - an Xbox has no HLE bios, so xemu's CI
     proves both flavors build and the guest is sandbox-clean, and nothing
     more. That is a small claim, honestly made, and still catches most of what
     breaks.

   Whichever it is, run **Chimera's contract tests against the package** with
   `CHIMERA_CORES_DIR` pointing at it: readable, ABI supported, becomes a
   factory, binds only declared buttons, stamps a version. It costs seconds and
   needs no content at all.
3. Add the `publish` job and a daily `schedule:` trigger, which is three lines
   calling `ToolAssisted-run/chimera/.github/workflows/publish-core.yml@main` -
   see any wired core, or docs/core-manager.md.
4. A row in Chimera's `official-cores.json` (which CI fetches published
   packages by; the frontend does not read it) and in the README's table of
   cores, which is where a user finds the core: its name linked to the
   repository's main page, and the systems it runs beside it.

**The publish job runs `./waterbox/build-package.sh -r <chimera>` in a fresh
recursive checkout, and nothing else.** Two consequences, both of which have
cost a red release:

- **Everything the package needs, that script must build.** A dependency you
  built by hand on your machine a month ago does not exist on a runner. The PS3
  core's LLVM was like this: the package failed in four tenths of a second with
  "Can't find LLVM libraries", on every release, for weeks.
- **No absolute paths anywhere.** A guest toolchain file with
  `-I/home/<you>/chimera-core-<name>/waterbox` in it works on your machine -
  from whichever clone that path names, not the one being built - and nowhere
  else. Use `${CMAKE_CURRENT_LIST_DIR}` and friends. Grep for your home
  directory before pushing.
- **Do not guess at `$HOME`.** A build step that falls back to
  `$HOME/chimera/extern/...` for the guest toolchain works on the machine
  that wrote the fallback and nowhere else; on a runner `$HOME` is not the
  checkout. Pass the miniBox path down from `build-package.sh` and stop with a
  message if the toolchain is not there.
- **The bundle sends each core's stdout to `/dev/null`.** cmake and autoconf
  write their errors there, so a build that fails for a stdout reason fails
  SILENTLY - a step that takes four tenths of a second and prints nothing is
  this, every time. Reproduce with
  `env HOME=/tmp/empty ./waterbox/build-package.sh -r <chimera> > /dev/null`.
- If a dependency takes hours (an LLVM, a Mesa), give it a cache key of its own
  in `.github/workflows/release.yml`. The broad core cache is keyed on every
  core's pin, so bumping any core throws it away.
