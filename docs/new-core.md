# From a core request to a published core

The whole path, in the order it is walked, with a pointer to where each part
is explained. [porting-a-core.md](porting-a-core.md) is the craft - the
toolchain's refusals, the three ways to get a picture, what a package
declares. This page is the route around it: what to settle before writing
code, what to copy, what "done" means, and what is the owner's to decide.

## Read first

| Document | What it answers |
|---|---|
| [porting-a-core.md](porting-a-core.md) | The order of work and the traps that cost days |
| [gates.md](gates.md) | The eight ways a gate goes green on a broken thing |
| [waterbox-analysis.md](waterbox-analysis.md) | What the sandbox is, and why it makes a run reproducible |
| miniBox `docs/MACHINE-SPEC.md` (`extern/chimera-common-minibox`) | The machine a guest runs on: memory kinds, stacks, syscalls |
| [gpu-bridge.md](gpu-bridge.md) | A renderer on a real GPU: what it costs a savestate, and what a load loses |
| [game-cores.md](game-cores.md) | A core that is a game and not a machine: properties, the kind |
| [multi-file.md](multi-file.md), [save-data.md](save-data.md) | Projects of several files; what a machine remembers |
| [core-manager.md](core-manager.md) | What a core release is, and how a user gets one |
| [issue-intake.md](issue-intake.md) | How issues are labelled, answered and closed |
| The closest core's `docs/PLAN.md`, `docs/BUILDING.md`, `AGENTS.md` | Everything this page says, done once already |

## 1. Before any code: what a request has to answer

A request (label `core-request`) names an emulator or a machine. Answer these
on the issue before starting, from upstream's source and not from its website;
a report that says no, and why, is a finished piece of work.

- **Is it a new core at all?** A machine that a core already here can be
  taught is a machine added to that core: the X68000 is a MAME driver built on
  its own (`chimera-core-x68k`), arcade boards went into FBNeo and Flycast,
  and ares answers for twenty-nine machines from one package.
- **May it be distributed, modified?** Read the licence of the commit you
  would pin. A core is upstream's source with patches, published as a binary
  package with its licence texts; a licence that forbids modified or binary
  redistribution ends the request, and an older commit under other terms is a
  different emulator with its own answer to give. Upstream code with no
  licence at all is the owner's call, not yours.
- **Can it be one thread on one clock?** Every core is. The emulator's
  threads become its own scheduler's, time is bought with executed
  instructions, and nothing waits on the host. Upstream support for input
  movies, rollback netplay or lockstep is the best sign there is.
- **Where does the picture come from?** In order of preference
  (porting-a-core.md): the machine's own framebuffer; an OpenGL renderer drawn
  by a Mesa softpipe compiled into the core; that same renderer through the
  GPU bridge. The bridge carries OpenGL and nothing else, so an emulator that
  draws only through Vulkan, Direct3D or Metal has no path until it has a GL
  or software renderer.
- **Does it recompile?** A JIT works in the sandbox with two conditions: it
  may not use faults as a fast path (no fastmem by SIGSEGV, no write
  protection to notice self-modifying code - the sandbox cannot protect a
  page for a guest, so those become checks), and what it emits may not depend
  on the host CPU beyond the baseline the core is built for.
- **What must the user bring?** Firmware, keys, a BIOS. It is declared by the
  package and supplied by the user; nothing of it is ever committed, uploaded
  to CI or named beyond what the machine needs.
- **What can a gate run on a public runner?** Homebrew, upstream's own test
  suite, a disc the gate builds itself - or nothing, in which case CI proves
  the build and the declarations and the machine legs run where the content is.
- **How big is it?** The guest's memory layout is declared up front; a
  machine with gigabytes of address space is possible (reservations are
  committed as they are touched) and has to be planned.

What comes out is a comment on the issue: feasible or not, the route, what the
user will need, and the questions that are the owner's - a licence that is not
plainly free, a new public repository, anything Chimera itself would have to
learn. A request that is deferred stays open.

## 2. Start from the nearest core

Nobody starts a core from an empty directory. Copy the repository whose
upstream is shaped like yours, then delete what you do not need.

| Upstream is... | Look at |
|---|---|
| A small C or C++ console emulator with its own framebuffer | `gpgx`, `snes9x`, `stella`, `quickernes`, `opera` |
| A PC: disks, firmware sets, a keyboard and a mouse | `dosbox-x`, `pcem` |
| One driver out of MAME | `x68k` (sparse checkout, the driver built alone) |
| An arcade emulator: many machines, per-game switches, rom sets in zips | `fbneo` |
| Many machines behind one interface | `ares` |
| A C++ emulator with a JIT and a GL renderer, built with meson here | `flycast`, `pcsx2` |
| A large CMake emulator with a GL renderer | `dolphin`, `azahar`, `vita3k`, `ppsspp`, `rpcs3`, `eka2l1` |
| Built on QEMU | `xemu` |
| An emulator with its own guest threads to schedule | `rpcs3`, `vita3k` |
| Written in Rust | `ruffle`, `touchhle` |
| Written against the Windows API | `applewin` (a shim library stands in for it) |
| Not an emulator: a game's engine, rebuilt | `sdlpop`, `sdlpop2`, `opensamurai` and [game-cores.md](game-cores.md) |

Every one of them is `https://github.com/ToolAssisted-run/chimera-core-<id>`,
and each has a `docs/PLAN.md` that says what was decided and why. Read the
closest one's whole; it is the cheapest day of the port.

## 3. The repository

`chimera-core-<id>`, branch `main`, with the shape porting-a-core.md gives and
these files, each of which a published core has:

- `README.md` - what it is, how the core reaches Chimera, what the user brings.
- `LICENSE` - this repository's own terms, and a note naming THIS core's
  upstream and what the built package contains. Write the note; do not carry
  another core's over.
- `docs/PLAN.md` - the design log: the feasibility answers, the milestones,
  every decision with its measurement, the sharp edges. Dated entries.
- `docs/BUILDING.md` - requirements, sources, every build step, the package,
  the gates, what CI does, troubleshooting. Tested from a fresh clone.
- `AGENTS.md` - the same for somebody doing it without reading prose: layout,
  the commands in order, the rules of the repository.
- `.github/workflows/chimera.yml` - the gate and the publish job (section 8).
- `waterbox/` - the adapter, both builds, `build-package.sh`, `run-gate.sh`,
  `tests/run-frontend.sh`, and the four declarations (`waterbox.config`,
  `file_slots.json`, `default_keybinds.json`, `package-licenses.json`).

Upstream is a pinned submodule, never edited in place; every change is a
numbered patch. No absolute path and no `$HOME` anywhere in a build script.

## 4. The work, and what proves each step

The order is porting-a-core.md's. What makes a step finished is a gate leg
that fails when the step is undone:

| Step | Proved by |
|---|---|
| Native reference builds headless | It runs a program to a known frame and dumps memory |
| Virtual time | Stalling the host does not move the machine; on the host clock the same leg fails |
| Guest build | `check-wbx.sh` passes; native and sandbox agree on memory, picture, audio and instruction count |
| Savestates | Rerecord (save and load around every frame) and session (another process) land where the straight run does |
| Input | A press reaches the machine through the wire the frontend uses; lag frames are frames nobody polled |
| The package | Chimera's contract tests open it; the frontend gate boots it through the real frontend |
| A GPU renderer | The renderer leaves the machine's memory identical; section 7 |

## 5. What Chimera will not learn

Chimera knows no system, no control and no core by name, reaches for nothing
over the network, and keeps its logic in the engine with the C# as interface
only. A core's needs are met by what its package declares - system names,
mnemonics, slots, firmware and its conditions, settings, media recipes,
optional exports - and porting-a-core.md lists them all.

If a core seems to need a line of its own in the frontend, the answer is a
declaration every core could use, which is a change to Chimera's design: put
it to the owner, and write it in [design-principles.md](design-principles.md)
when it is decided. It is never a special case.

## 6. Where determinism has leaked before

Each of these was a real divergence between two runs of the same movie:

- **A clock.** The host's time, a date, a tick counter, a seed drawn from
  any of them. A clock frozen at a constant is not a fix - a game waiting for
  time to pass then looks like an input bug. The machine's calendar clock is
  a setting the project pins.
- **A thread**, or anything that finishes "when it is done": background
  loaders, shader compilers, audio mixers.
- **A host address in the machine's memory.** Pointers stored where the guest
  can read them, a stack address left in a buffer, an allocator whose answers
  depend on what a tool asked earlier. Memory a state does not carry
  (`alloc_invisible`) is taken during `Init` and never later, and holds only
  what the machine can rebuild.
- **Memory nobody wrote**: an emulator that reads what `malloc` returned, or
  stack below the stack pointer.
- **Whoever is looking.** A memory domain read, a screenshot, turbo, a
  savestate being taken: none may leave the machine other than it would have
  been. The gate asks each one - drawn against undrawn, saved against not.
- **Order that is not order**: directory listings, hash-map iteration over
  pointers, a zip's member order.
- **The picture feeding the machine.** A game that reads its own frame back
  makes the renderer part of the machine; then it is declared a sync setting
  and drawn the same everywhere, or the core is not portable.

## 7. A renderer on a real GPU

Optional, last, and never the only renderer: a core must run with no GPU.
[gpu-bridge.md](gpu-bridge.md) is the whole story; the parts a new core must
do are:

- **Notice the context moved.** Every load of a state gives the renderer a
  new GL context. Keep the id of the context your objects came from in guest
  memory, compare it before touching one (`chimera_gl_context_id`), and
  rebuild from the machine's memory - before a frame's textures are looked
  up, not between that and the draw. Answer `StateLoaded`.
- **Let go of the old before making the new.** Delete every object of the
  dead context before the first object of the live one is made; a stale
  delete after that deletes the new object that was given the same name.
- **Carry what only the card holds.** A render target, a frame half drawn, a
  deinterlacer's fields are in no state. Answer `StateSaving` by copying them
  into the core's own memory - never into the console's, which a game can
  read - and put them back after the rebuild. A frame the frontend already
  read needs no copy.
- **Skip the readback, not the drawing** (`video.drawEveryFrame`) when what
  is drawn persists on the card.
- **Measure on a real card.** `chimera-run --settle-probe <frame>,<count>`
  says how many frames after a load are wrong; `--settle-probe-state` asks
  the same of a whole state, `--settle-probe-unseen` of a state taken of
  frames nobody read, and `CHIMERA_NO_STATE_SAVING=1` is the control. A
  software GL is not a driver: what passes through llvmpipe has not been
  shown on hardware.

## 8. The gates

Three, and a core is not done with fewer:

- **The core gate**, `waterbox/run-gate.sh`: native against sandbox, leg by
  leg, each saying what it compared. Read [gates.md](gates.md) first; every
  new leg is broken on purpose once and seen to fail.
- **The frontend gate**, `waterbox/tests/run-frontend.sh`: the package
  through the real frontend, headless - a boot compared with the sandbox
  reference, a project opened in TAStudio, the keybinds adopted.
- **Chimera's contract tests** against the package, which need no content:

      CHIMERA_CORES_DIR=<folder holding the package> dotnet test \
        source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj \
        -c Release --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"

Content: what a gate runs is named, never "the first file in the folder".
What may be distributed lives in the repository (`tests/`); what may not lives
outside it (`tests/roms-local`, or a path in an environment variable), its
legs say SKIP with the reason where it is absent, and a SKIP is never read as
a pass. A core proved on one real game says which; one proved on none says
that.

**Windows is the same package.** The guest is one file for both systems, and
the host under it is not: run the frontend gate's scenario on Windows before
calling a core done. What has failed only there so far was stacks - an
emulator's own coroutine stacks need `MAP_STACK`, and a fault on the page the
stack pointer is in cannot be delivered.

## 9. Publishing

A new public repository, a place in the roster and the closing of the request
are the owner's to approve. With that:

1. **The repository**, pushed to `ToolAssisted-run/chimera-core-<id>`.
2. **`.github/workflows/chimera.yml`**, copied from a core of the same
   shape: it checks Chimera out at `main` with submodules, builds miniBox and
   the guest toolchain, runs the core gate with `CORE_VERSION` set to the
   commit, runs the contract tests against the package, uploads it as
   `<id>-${{ github.sha }}`, and calls
   `ToolAssisted-run/chimera/.github/workflows/publish-core.yml@main`. Its
   header says which legs a runner cannot run. Triggers: push, pull request,
   `workflow_dispatch` with a `kind` input, and a daily schedule.
3. **The first releases.** A green push publishes the rolling `dev`; cut the
   first dated one by hand, `gh workflow run chimera.yml -f kind=nightly`.
   If the very first publish fails on an "empty index", run it again: the
   index was written a second before the asset existed.
4. **The roster and the README.** A row in `official-cores.json` - `id`,
   `name`, `systems` as id and name each, `repo`, and `"kind": "game"` for a
   game core - and a row in the README's table of cores: the core's name
   linked to its repository's main page, the systems it runs beside it.
5. **The issue.** Say what was built and which published build has it - the
   name read from the release (`gh release view dev --json name`), never
   guessed - what the user must bring, and what was not proved. Then close it.

Release notifications for a new repository use a credential that lives in the
other repositories' settings; setting it up is the owner's.

Traps on a runner are in porting-a-core.md ("Publishing it"): the package
script must build everything it needs, no absolute paths, no `$HOME`, and a
step that fails in four tenths of a second with no output failed on stdout.

## 10. Done means

- The core gate, the frontend gate and the contract tests pass here, and CI
  is green from a fresh clone.
- Every new leg has been seen to fail.
- At least one real game runs from boot to play, native and sandbox
  identical, and `docs/PLAN.md` names it - or says that none could be tried.
- A movie recorded in the frontend replays in a new process, on Linux and on
  Windows.
- `README.md`, `docs/BUILDING.md`, `AGENTS.md` and `docs/PLAN.md` describe the
  repository as it is; `package-licenses.json` names every component.
- The roster row and the README row are in, the issue says what was built and
  what was not, and nothing of the core is in Chimera's own source.
