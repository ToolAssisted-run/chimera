# AGENTS.md - Chimera

Chimera is a frontend for tool-assisted speedruns: an engine in C++
(`libchimera`), an interface in C# over it, and the miniBox sandbox every
emulator core runs in. It ships no cores. Each core is its own repository,
`ToolAssisted-run/chimera-core-<id>`, publishing one file,
`<id>.chimeraCore`, that a user puts in the `Cores` folder.

This file is the operating guide for an agent working here. The reasoning
behind every rule in it is in [docs/design-principles.md](docs/design-principles.md),
the design log.

## If you are here to build a new core

Start with **[docs/new-core.md](docs/new-core.md)**: what a core request has
to answer before any code, which existing core to copy, what proves each
step, the gates, and the publishing checklist. It sends you to the rest:

- [docs/porting-a-core.md](docs/porting-a-core.md) - the craft: the guest
  toolchain's refusals, the picture, savestates, what a package declares,
  the optional exports, what breaks on a CI runner.
- [docs/gates.md](docs/gates.md) - how a gate goes green on a broken thing.
- [docs/waterbox-analysis.md](docs/waterbox-analysis.md) and
  `extern/chimera-common-minibox/docs/MACHINE-SPEC.md` - the sandbox.
- [docs/gpu-bridge.md](docs/gpu-bridge.md) - a renderer on a real GPU.
- [docs/game-cores.md](docs/game-cores.md) - a core that is a game.
- [docs/multi-file.md](docs/multi-file.md), [docs/save-data.md](docs/save-data.md),
  [docs/core-manager.md](docs/core-manager.md), [docs/issue-intake.md](docs/issue-intake.md).
- The closest core's own `AGENTS.md`, `docs/BUILDING.md` and `docs/PLAN.md`.

The requests are the issues labelled `core-request`:
`gh issue list -R ToolAssisted-run/chimera --label core-request`.

A core is built in ITS OWN repository, a checkout beside this one. This
repository changes for a new core in exactly two places - a row in
`official-cores.json` and a row in the README's table of cores - and in
nothing under `source/`.

## Layout

- `source/engine/` - the engine: sessions, movies, the state history
  (greenzone), projects, RAM search. `tools/chimera-run.cpp` is the headless
  runner every measurement uses.
- `source/gui/` - the C# solution (`Chimera.sln`): interface only.
- `extern/chimera-common-minibox/` - miniBox, a submodule: the sandbox host,
  the guest toolchain (`source/guest`), the GPU bridge (`source/gl`).
- `tests/synth/` - a synthetic core and the witness gate that drives the
  whole product with it; `tests/ui/` - the interface tests; `tests/engine/`.
- `tools/` - bundles, publishing (`publish-core.sh`, `write-core-index.sh`),
  `fetch-cores.sh`.
- `official-cores.json` - the roster CI goes by. The frontend does not read it.
- `docs/` - one document per subject; `design-principles.md` is the log.
- `build/` - every output. Ignored by git. `build/Cores` is where a core's
  `build-package.sh -r <this checkout>` installs its package.

## Build

Linux hosts both builds. Requirements are in the README ("Building").

```sh
git submodule update --init --recursive
meson setup build/meson-linux   --prefix "$PWD/build" --libdir dll
meson setup build/meson-windows --prefix "$PWD/build" --libdir dll --cross-file extern/meson/mingw-w64.ini
meson compile -C build/meson-linux && meson install -C build/meson-linux
meson compile -C build/meson-windows && meson install -C build/meson-windows
dotnet build source/gui/Chimera.sln -c Release
```

A core repository also needs miniBox's host library and guest toolchain:

```sh
mb="$PWD/extern/chimera-common-minibox"
meson setup "$mb/build/meson-linux" "$mb" && meson compile -C "$mb/build/meson-linux"
meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true && meson compile -C "$mb/build/meson-cpp"
```

## The gates

Run them before every commit that touches what they cover, and say in the
commit what ran. Chain on the real exit code - a pipe's is its last command's.

```sh
meson test -C build/meson-linux                  # the engine's unit tests
env -u DISPLAY sh tests/synth/run-witness.sh     # the witness: the product, end to end
env -u DISPLAY bash tests/ui/run-ui-tests.sh     # the interface tests
```

- `meson install -C build/meson-linux` first: the managed tests and the
  witness load `build/dll/libchimera.so`, which only the install refreshes.
- `run-ui-tests.sh` does not build. After a C# change run `dotnet build ...
  --no-incremental` first, and distrust a test count that did not move.
- After editing the synthetic core (`tests/synth/package-box/`), run
  `tests/synth/package-box/build-box.sh` and then `tests/synth/build-package.sh`,
  or the witness runs the old package.
- A change to the engine or to miniBox must also compile for Windows
  (`meson compile -C build/meson-windows`).
- The contract tests run against any package:
  `CHIMERA_CORES_DIR=<folder> dotnet test source/gui/Chimera.Tests.Client.Common/Chimera.Tests.Client.Common.csproj -c Release --filter "FullyQualifiedName~InstalledCorePackagesTests|FullyQualifiedName~MnemonicUniquenessTests"`.
- A new test is broken on purpose once and seen to fail ([docs/gates.md](docs/gates.md)).

## Rules of the repository

- **Nothing system-specific in Chimera.** No system, control or core is known
  by name in the engine or the interface; a core says what it needs in its
  package. A core that seems to need a special case needs a declaration every
  core could use - a design change, put to the owner.
- **Load-bearing logic lives in the engine.** The C# is interface.
- **No network code.** Nothing downloads, nothing phones anywhere;
  `tools/check-no-network.sh` holds it.
- **Decisions go in the design log** in the same commit as the code: a dated
  section in `docs/design-principles.md`, in prose, with what was measured,
  marked "(user-decided, YYYY-MM-DD)" when the owner decided it. The document
  for the subject is updated in that commit too.
- **Prose is ASCII.** No typographic dashes or quotes in any `.md`.
- **The README is succinct**: a sentence or two a point, detail in `docs/`.
  Its tables of cores are by core - the name linked to the repository's main
  page, the systems beside it.
- **Commits.** `type(scope): a sentence that says what is now true`, a body in
  prose with the reasoning and the measurements, and the trailers this
  project marks assisted commits with (CREDITS.md). One gated round, one
  commit. Stage explicit paths - never `git add -A`. No amending, no force
  push.
- **Report what happened.** A leg that was skipped is said to be skipped; a
  result seen only through a software GL is not said of a real card; a game
  that was not tried is not said to work.
- **Content.** No commercial game, firmware or key is committed, uploaded to
  CI or attached to an issue. Test content that may not be distributed stays
  outside the repository and its legs SKIP where it is absent.
- **Issues.** Deferred is not closed. An issue is closed when the fix is
  published, with the published build named from the release and not from
  memory, and with what was not proved said plainly.

## What is the owner's to decide

Ask before: creating or publishing a repository, changing the roster,
anything a licence does not plainly allow, a change to what Chimera is (a new
declaration, a new export, a new setting's meaning), and anything that needs
a credential.

## More than one agent in this checkout

One working tree, one index, one `build/`. To share them:

- Commit only your own paths, by name, and `git pull --rebase` before a push.
  Never stage, stash, reset or clean what you did not write.
- A core lives in its own checkout and repository, so most of a new core's
  work touches nothing here. The shared files are `official-cores.json`,
  `README.md`, `docs/` and `build/Cores`.
- `build/Cores/<id>.chimeraCore` is overwritten by that core's
  `build-package.sh`; leave other cores' packages alone.
- Heavy jobs - a full build, a gate, a run on the GPU - one at a time on one
  machine. Check what is running before starting one, and never edit a
  script while it runs.
- The witness and the interface tests write under `tests/synth/work` and
  `build/tests`; two runs at once spoil each other.
