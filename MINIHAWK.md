# miniHawk â€” Project Charter

A minimal, core-agnostic TAS frontend derived from BizHawk. Emulation cores are removed
from the tree and loaded on-the-fly as external, self-contained packages just before ROM load.

## Objectives (testable)

1. **Zero core code in the repo.** miniHawk builds and runs with no emulation core in the
   solution. Cores arrive as single-file packages (`.zip`: manifest + managed adapter DLL +
   native DLL(s)), discovered from a `Cores/` directory and loaded at ROM-load time.
2. **Published core contract.** The core-facing API (essentially `BizHawk.Emulation.Common`
   + `BizHawk.BizInvoke`) becomes a versioned, published contract. A core package must be
   buildable outside the miniHawk repo against that contract alone.
3. **Determinism witness.** The QuickerNES regression suite (31 test movies, vendored at
   `tests/suite/` from github.com/TASEmulators/quickerNES) must resync to
   success at every phase boundary, including after the core is evicted to an external
   package. Running the full suite green at every step is paramount to conserving
   correctness. Test ROMs live at `C:\Users\sergiom\Documents\TAS\roms\nes` (file names
   match the `.test` files' `Rom File` entries; SHA1-verified). In-scope counts after
   Phase 0 triage: 28 at Level A (native), 26 at Level B (full-stack).
4. **TAS-only frontend.** Kept: TAStudio, movie record/playback, savestates, rewind, frame
   advance, virtual pads, RAM Watch/Search, Hex Editor, Cheats, Lua console (core-agnostic
   APIs only), A/V dumping + screenshots, core-agnostic debug tools (trace logger, CDL,
   generic disassembler). Everything else â€” core-specific config dialogs and viewers,
   movie importers, RetroAchievements, per-system Lua libs, etc. â€” is removed.

## Pillar: reproducibility belongs to the core, not the frontend

miniHawk is NOT the keeper or enforcer of reproducibility — it REQUIRES it from
cores. A movie's reproduction contract is the pair (movie, core package): if the
selected core package is exactly the same, the movie must reproduce regardless of
miniHawk's version or the versions of any frontend library (SDL, Lua, zstd, cimgui,
...). Consequences: frontend native dependencies may be upgraded freely and are
never pinned for determinism's sake (only for build reproducibility); no frontend
feature may become something a movie depends on to sync; the witness harness proves
the frontend *delivers inputs faithfully* to the core, not that the frontend owns
determinism. (User-stated pillar, 2026-08-10.)

Package identity (same day): like roms, a core package is uniquely and solely
identified by the SHA1 of its file — that hash IS the package; name, version, git
commit, and platform variant are secondary metadata. Implemented: the loader hashes
the zip at load time (directory-form dev packages are explicitly unhashed), the
extraction cache is keyed by that SHA1, the hash is shown on load, recorded into
movie headers as CorePackageSHA1, and compared on playback with a warning on
mismatch — the exact analogue of the rom-hash check.

Elaboration (same day): the pillar bites hardest at the interface points where miniHawk
hands data to cores. Audit of those points and their standing:
- ROM data: RomGame is format-agnostic — the core receives the file's exact bytes, no
  header detection/stripping/per-system preprocessing (done in P3.2, and now
  load-bearing for this pillar, not just cleanliness).
- Archive extraction (zip → rom bytes): bit-exact by the archive format's definition.
- IPS/BPS patching: a frontend transformation of rom bytes. Tolerable only because
  those formats are deterministic by spec; the implementation must stay spec-exact and
  must never grow heuristics.
- Movie input log: the mnemonic format is effectively FROZEN — its parsing is part of
  the reproduction contract and must be spec-stable across miniHawk versions.
- Sync settings: opaque JSON round-tripped to the core's types; the frontend must
  never interpret or migrate them.
- DISC DATA IS THE OPEN VIOLATION: BizHawk.Emulation.DiscSystem parses CUE/CCD/CHD and
  synthesizes sector streams IN THE FRONTEND — a frontend version change could alter
  the byte stream a disc core sees, breaking the pillar. Resolution direction when the
  disc story is taken up: disc image interpretation must move core-side (each disc
  core package bundles its own disc layer, versioned with the core); the contract
  should hand over raw file paths/bytes only. Until then DiscSystem stays unused
  (no disc cores exist).
- Firmware: database contents removed; anything a core needs ships inside its package.

## Agreed decisions

- **Core package interface:** managed .NET adapter DLL implementing `IEmulator` (+ service
  interfaces) against the published contract, bundled with its native core DLL(s).
  Not a C ABI, not libretro.
- **Repo strategy:** trim this BizHawk checkout in place on a branch. Keep git history and
  upstream diffability. Rename to miniHawk once stable.
- **Reload model:** load-once per process (net48 cannot unload assemblies). Packages load
  lazily at ROM-load time; swapping a loaded core's version requires app restart.
- **Witness core:** QuickNes (backed by the TASEmulators/quickerNES submodule) â€” single
  native DLL, no waterbox, no discs, no firmware. It stays in-tree until Phase 3.

## Procedure

Each phase ends with: build green â†’ witness movie syncs â†’ commit. Never more than one
phase of unverified change.

- **Phase 0 â€” Baseline & harness.** Build as-is. Stand up the two-level witness harness
  (below), generate golden RAM dumps from unmodified BizHawk, and confirm they agree with
  the native quickerNES tester's ground truth. No product code changes.
- **Phase 1 â€” Shrink to one core, statically. [DONE 2026-08-09]** All cores except
  QuickNes removed (~312k lines deleted); frontend trimmed (per-core dialogs/tools,
  RetroAchievements, movie importers, per-system Lua libs, Libretro/MAME paths, system
  menus except NES). Core asset payloads, core submodules, and native core source trees
  removed (Assets/dll: 90 â†’ 28 files, all frontend infra + libquicknes). Kept: Waterbox
  host + waterbox/ native tree (OPEN DECISION pending), quicknes/, SDL2 + rcheevos +
  chd/bizhash/zstd/blip_buf natives (still referenced by frontend/DiscSystem), gamedb.
  Witness at phase close: 26/26 simple AND 26/26 rerecord, byte-identical goldens,
  after a from-clean rebuild.
- **Phase 2 â€” Publish the contract. [DONE 2026-08-09]** `CoreInventory`, `CoreNames`,
  and the reflection-attribute construction machinery are gone (per user decision, along
  with waterbox, the 6502 disassembler, and the trace logger â€” all recoverable from git
  history). In their place:
  - `ICoreFactory` + `CoreCreationContext` + `IRomAsset` in **BizHawk.Emulation.Common**
    â€” the published core contract. A core ships a factory: name, system IDs, core type
    (stable key for persisted settings), settings/sync-settings types, and `Create(ctx)`.
  - `CoreRegistry` (Client.Common) indexes factories by system; RomLoader's
    `MakeCoreFromRegistry` replaces the old inventory path (same preferred/forced-core
    and error semantics).
  - `CorePackageLoader` (Client.Common): at startup, `<exe>/Cores/` is scanned for
    package directories or zips containing `minihawk-core.json` (formatVersion 1:
    name, assembly, factoryTypes[]). Zips extract to `Cores/_cache/<name>` keyed by
    zip timestamp; the package dir is prepended to PATH for native DLL resolution;
    an AppDomain.AssemblyResolve hook serves package assemblies so persisted
    settings / movie sync-settings JSON ("Type, Assembly" names) round-trip.
  - QuickNES is registered through `BuiltInCoreFactories` (transitional; Phase 3
    replaces it with a real external package and deletes it).
  Witness at phase close: 26/26 simple AND 26/26 rerecord through the new loader.
- **Phase 3 â€” Evict the witness. [DONE 2026-08-09]** The QuickNES adapter now lives in
  `minihawk-cores/quickernes/` â€” NOT part of BizHawk.sln, built by `build-package.ps1`
  against the contract DLLs from `build/dll` exactly as an out-of-repo package would be,
  and shipped as `build/Cores/quickernes.zip` (manifest + MiniHawk.QuickerNES.dll +
  libquicknes natives). `BizHawk.Emulation.Cores` is deleted from the solution entirely:
  **miniHawk contains zero core code**. Supporting changes: INESPPUViewable and the NES
  palette helpers moved into Emulation.Common (frontend PPU viewers stay core-agnostic);
  BootGod NES cart DB init moved into the package factory; virtual pad schemas are
  discovered from registered factory assemblies; per-core settings dialogs replaced by
  the generic config dialog (`QuickNesConfig` and its FCEUX-palette import are gone â€”
  revisit if missed); `emu.setrenderplanes` is a no-op pending a contract story; the
  Debug-only core-poking dev menu was deleted. Witness at phase close: 26/26 simple AND
  26/26 rerecord with the core loaded from the zip package.
  Addendum (same day): per user principle â€” miniHawk must contain NOTHING core-specific
  â€” the Phase 3 compromises were purged too: INESPPUViewable and the NES palette code
  moved from Emulation.Common into the package; the NES PPU/nametable viewer tools, the
  NES menu, and the QuickNes icon were deleted from the frontend; NesCarts.xml moved
  from gamedb into the package zip (BootGod reads from the extracted package dir);
  NES palette files moved to minihawk-cores/quickernes/palettes.
  Second addendum (deeper cleanse, same day): gamedb is GONE entirely (all per-system
  hash DBs, the Database class, DB-entry generation service). ROM->system routing is now
  purely: (1) user extension preference, (2) the extension->system map that core-package
  manifests declare ("extensions" field), (3) the platform-chooser prompt, which lists
  only systems provided by loaded packages. RomGame is format-agnostic (no header
  detection/stripping, no per-system preprocessing; hash = whole file â€” NOTE: game
  hashes recorded in old movies will no longer match, warning-only). Also removed: the
  FirmwareDatabase CONTENTS (mechanism kept, empty; packages could register records
  later), per-system display-name tables (system ID is the display name), per-system
  default path tables (path sets generated on demand per system), the frame-rate table
  (display fallback is a flat 60/50), the GameShark converter and all per-system
  cheat-code decoders, and the game-DB button in the log window.
  Phase 3 sharp-edge, confirmed in the wild: persisted configs and movie sync-settings
  embed `"Type, AssemblyName"` â€” entries written before eviction name the deleted
  `BizHawk.Emulation.Cores` assembly, and Newtonsoft's `$type` binder then throws
  (silently swallowed â†’ defaults, first caught as two sync-settings-dependent witness
  failures). Fix: the manifest's `supersedesAssemblies` list â€” the resolver serves the
  package assembly under its legacy names, keeping old configs AND pre-eviction movies
  round-tripping.
  Third addendum (Assets cleanse, same day): deleted from Assets â€” 8 orphaned native
  libs (librcheevos.dll/.so [RetroAchievements was removed], freetype.dll, libpng16.dll,
  zlib.dll [nothing P/Invokes them and no remaining native lib imports them â€” verified
  by PE import-table scan], and the MinGW runtime trio libgcc_s_seh-1/libstdc++-6/
  libwinpthread-1 [only ever needed by MinGW-built cores; every remaining native uses
  msvcrt or the UCRT]); all per-system example Lua scripts (Doom/GBA/Genesis/N64/NDS/
  NES/PCE/SNES dirs); the bsnes-gamma shader (SNES color correction). N3DSHasher.cs
  (3DS-specific) deleted from Emulation.Common. defctrl.json is GONE from the frontend:
  per-system controller defaults are now package-provided â€” a package may ship its own
  defctrl.json (same DefaultControls shape, keyed by controller-definition name);
  CoreRegistry merges them (first package wins per name) and InputManager.SyncControls
  adopts package defaults for any controller the user's config has never seen. A
  user-saved defctrl.json next to the exe (written by the controller dialog's "save
  defaults") still wins over package defaults. The quickernes package ships the NES
  Controller bindings. Natives KEPT deliberately: chd_capi (DiscSystem â€” disc story
  still an open decision), cimgui/SDL2/OpenAL32/lua54/libzstd/libbizhash/e_sqlite3
  (frontend infrastructure). blip_buf was initially kept as contract-offered audio
  resampling, then REMOVED at user request (wrapper class, natives, C source dir,
  arm64 prebuilt): nothing in the tree used it, and a core that wants it (e.g. a
  future Gambatte package) should bundle its own copy.
  Sharp edge #2, caught by the witness harness itself: the original zip-extraction
  cache (delete stale dir, extract, write stamp file) raced when 8 EmuHawk instances
  launched simultaneously against a cold cache â€” ZipFile.ExtractToDirectory threw
  "file already exists", and since extraction ran outside the per-package try/catch it
  reached the top-level exception dialog, which on the hidden desktop blocks invisibly
  until timeout. Fix: cache dirs are keyed by zip timestamp (`<name>-<ticks>`), zips
  extract to a private temp dir followed by an atomic Directory.Move â€” race losers
  discard their temp and use the winner's dir; stale caches are cleaned best-effort;
  per-zip extraction failures are caught and logged instead of killing startup.
  Fourth addendum (same day, user-directed): Assets/Shaders and the entire .cgp
  retro-shader feature removed (RetroShaderChain/Preset/Pass, RetroShader, hq2x/
  scanlines/bicubic/user chains, TargetDisplayFilter + TargetScanlineFilterIntensity +
  DispUserFilterPath config, the Scaling Filter UI group, the Bicubic final-filter
  option [legacy setting falls back to bilinear], the Bizware.Test shader demo app;
  client.get/settargetscanlineintensity are warning no-ops now). The built-in
  hand-coded presentation shaders in Bizware.Graphics remain â€” they ARE the display
  pipeline. DiscoHawk removed entirely (app project, DiscoHawkLogic, sln entry, docs),
  taking the stale upstream release packaging with it: Package.sh, the packaging bats,
  and the whole nix ecosystem (default.nix, Dist/*.nix, docs, CI workflow) â€” dead
  since they referenced gamedb/waterbox/defctrl anyway. blip_buf removed (see above).
  Fifteenth addendum (2026-08-10, user-directed — MESON, LINUX-HOSTED, DUAL-TARGET):
  the canonical build is now meson on Linux (WSL locally; Linux runners in CI),
  producing BOTH OSes' artifacts: managed IL built once via dotnet (run_target
  'frontend', Linux dir only), natives built per-target — native gcc for .so, and
  mingw-w64 cross (static gcc runtime, so no libgcc/winpthread dlls) for .dll via
  extern/meson/mingw-w64.ini + mingw-toolchain.cmake. Root meson.build: direct
  shared_library targets for bizhash/lua54/e_sqlite3/cimgui/luasocket (luasocket's
  two same-named "core" modules live in extern/meson/luasocket-*/ subdirs), nested
  upstream builds via extern/meson/nested-build.sh for zstd (its own meson), SDL2 +
  openal (cmake + toolchain file), chd_capi (cargo, --target x86_64-pc-windows-gnu
  for cross). WSL provisioned: mingw-w64 gcc 13, rustup 1.97.1 + windows-gnu target,
  and — after packages.microsoft.com's Ubuntu 24.04 feed turned out to serve a
  source-build SDK (8.0.129) WITHOUT WindowsDesktop targets, confirming the
  UbuntuMispackagedSDKCheck trap firsthand — Microsoft's own SDK binary (8.0.423)
  via dotnet-install.sh into ~/.dotnet. The
  Windows msbuild BuildNativeDeps hook is now Condition Windows_NT — a dev shim.
  Sharp edges found: mingw builds of libusb need our own config.h (msvc/config.h is
  guarded; and _TIMESPEC_DEFINED must NOT be defined — it's mingw's own timespec
  guard); meson passes builddir-relative paths to custom_target commands (absolutize
  before cd-ing tools like cargo); meson target names collide within a directory
  (same-named outputs need subdirs); WSL /tmp is volatile (log to /mnt/c); and the
  nastiest — upstream builds decorate library names per platform convention (mingw
  meson emits libzstd-1.dll + libzstd.dll.a), and a filename glob happily shipped the
  import-library ARCHIVE as "libzstd.dll", which passed everything until LoadLibrary
  returned ERROR_BAD_EXE_FORMAT and the witness went 0/26 — nested-build.sh now
  matches exact/version-infixed names and excludes .a/.d/.def. Witness after fix:
  the Windows gate ran against the Linux-cross-built dlls — the frontend stack
  (SDL2, lua54, zstd, cimgui, bizhash all mingw-built) byte-exact at 26/26 + 26/26,
  which is also the deferred-reproducibility pillar demonstrated empirically:
  an entirely different compiler family under the frontend, identical emulation.
  Fourteenth addendum (2026-08-10, user-directed): BizHawk.sln, Common.props, and
  Directory.Packages.props moved into source/ (sln project paths relativized;
  extern/ gained a thin Directory.Packages.props forwarding to source/'s, since NuGet
  discovers that file by walking up from each project). Root is now four directories
  (build/ extern/ source/ tests/) and five files (.gitignore .gitmodules LICENSE
  MINIHAWK.md README.md). STATED DIRECTION (user): the entire build system — Windows
  and Linux targets — should eventually be mediated solely by MESON; the current
  msbuild-invokes-build-natives.ps1 arrangement is a transitional shim that meson
  will absorb (meson orchestrating the dotnet build and every native recipe).
  Thirteenth addendum (2026-08-10, user-directed — NATIVES FROM SOURCE): ExternalProjects
  moved to extern/ at the repo root, and ALL prebuilt native libraries are gone from the
  tree. extern/build-natives.ps1, run automatically by the solution build
  (BuildNativeDeps target, incremental by timestamp), builds every native dependency
  from source into build/dll: libbizhash + SDL2 (in-repo recipes; clang-cl — bizhash's
  gcc target-attributes need it, and a .def now provides the exports ELF visibility
  used to), and six NEW pinned submodules under extern/ with new recipes — lua v5.4.8
  (stock, LUA_BUILD_AS_DLL), zstd v1.5.7 (cmake → libzstd.dll), cimgui 1.90.6 (pinned
  to ImGui.NET 1.90.6.1's binding version), openal-soft 1.24.3, ericsink/cb's sqlite3
  amalgamation with the canonical e_sqlite3 defines (mirrored from cb's generator),
  luasocket v3.1.0 (socket/mime core.dll against our lua54 import lib), plus chd_capi
  built via cargo (rust-toolchain.toml pins 1.97.1; user installed rustup for this).
  Toolchain: VS2022's clang-cl/CMake/Ninja (probed by path, works on GitHub
  windows-latest runners unchanged). Per the reproducibility pillar, all version pins
  are for BUILD reproducibility only — never for movie determinism, which belongs to
  the core package. Assets/ is GONE entirely: EmuHawkMono.sh moved into the EmuHawk
  project (copied to build/ by the PostBuild target), the LuaCATS API doc stubs were
  DELETED outright (pure editor documentation, annotation-only with error() guards,
  never read by the frontend, and partially stale against miniHawk's actual API;
  recoverable from history or regenerable from the [LuaMethod] attributes if ever
  wanted), and the Assets/** copy glob was removed. Root is now
  build/ extern/ source/ tests/ + eight files.
  Twelfth addendum (2026-08-10, user-directed — EXPLICIT CORE LOADING + layout): cores
  are no longer discovered. The startup Cores/ directory scan is gone; loading a core
  is an explicit act: File > Open Core... (file prompt for a package dir/zip) or the
  new --core=<path> CLI option (which the witness driver now passes). Open ROM and
  Recent ROM stay DISABLED until a core is loaded — deliberately, so core (sync)
  settings can be configured BEFORE the first rom load (config-before-load principle).
  The Config > Core Settings menu rebuilds on every package load; zip caches now live
  under <exe>/CoreCache. Layout changes in the same round: minihawk-cores/ deleted
  (author docs live in this charter + the quickerNES reference package);
  minihawk-tests/ renamed tests/; ExternalProjects/ moved under source/; root purged
  to the minimum (removed: .github incl. all CI, .vscode, .config, .editorconfig,
  .global.editorconfig.ini, .stylecop.json + the Common.props StyleCop block,
  appveyor.yml, SECURITY.md, contributing.md [README gained a Contributing section],
  global.json, sln.DotSettings, .git-blame-ignore-revs, "Building Other
  Solutions.txt").
  Eleventh addendum (2026-08-10, user-directed): miniHawk is a STANDALONE repository
  (github.com/SergioMartin86/miniHawk) with a FRESH history — commit 0 is the
  post-separation state. The BizHawk ancestry (23,722 commits) is deliberately not
  carried; it remains available in the upstream BizHawk repository and in the
  transitional SergioMartin86/BizHawk fork, which is also the recovery source for
  deferred machinery (waterbox, trace logger, disassembler, Gambatte sources for
  Phase 4). Future upstream ports are cherry-picked/rebased without shared ancestry.
  Tenth addendum (2026-08-10, user-directed — FULL SEPARATION): the QuickerNES adapter
  no longer lives in this repo at all. Everything quickerNES-specific
  (minihawk-cores/quickernes/ and the quickernes.yml workflow) moved to the quickerNES
  repository itself (github.com/SergioMartin86/quickerNES, branch minihawk-adapter,
  `minihawk/` dir): managed adapter, native bizinterface + Makefile (now building from
  that repo's own sources), manifest, bundled data, prebuilt natives, build-package.ps1
  (param -MiniHawkRoot, default sibling ../BizHawk checkout; installs quickernes.zip
  into <MiniHawkRoot>/build/Cores), and a native-build CI workflow. The dev loop:
  build miniHawk sln → run ../quickerNES/minihawk/build-package.ps1 → witness gate.
  minihawk-cores/ here is reduced to the core-author guide README. The vendored witness
  suite stays in minihawk-tests/suite/ (user decision: pinning the gate to what the
  goldens were recorded against is a feature). Gate ran against the
  quickerNES-repo-built package: 26/26 simple + 26/26 rerecord.
  Ninth addendum (same day, user-directed): References/ deleted — no committed managed
  binaries remain. The source generators are ProjectReferences with
  OutputItemType=Analyzer; NLua/ISOParser/HawkQuantizer are plain ProjectReferences
  (built transitively, not sln members); SettingsUtil is built via a
  ReferenceOutputAssembly=false reference from EmuHawk and copied into build/dll
  (ProvideCoreAuthorKit target) so the core-author kit is contract DLLs + settings
  generator in one directory — the quickernes package's Analyzer path points there.
  Sharp edge #3: solution builds UNSET Configuration/Platform for ProjectReferences
  that aren't solution members — everything above silently built as Debug inside a
  Release build until Common.props set
  ShouldUnsetParentConfigurationAndPlatform=false. Also, LibCommon.props had a
  PostBuild copying outputs into References/, which would have silently resurrected
  the folder; removed. Only native binaries (C/C++ toolchains required) remain
  committed: Assets/dll frontend infra and the package's libquicknes.
  Eighth addendum (same day, user-directed): the entire Dist/ folder deleted — release
  packaging utilities (7za/zip/unzip/upx/fart/vswhere/ILMerge/NuGet binaries), Unix
  build wrapper scripts (two already broken by the rename), upstream release stamping,
  the upstream changelog, arm64 prebuilt natives nothing shipped anymore, and the
  git-hooks commit-message linter together with the InstallGitHooks build target in
  BizHawk.Common.csproj that installed it on every build (code-checking, per user
  policy). Unix builds are plain `dotnet build BizHawk.sln` now.
  Seventh addendum (same day, user-directed): standard folder nomenclature adopted —
  `src/` is now `source/` (git mv, sln paths updated; likewise the package's `src/` →
  `source/` and `native-src/` → `native-source/`) and the build output dir `output/` is
  now `build/` (MainSlnExecutable.props, package HintPaths/build script, witness driver,
  .gitignore, launch.json, SDL2/libchd build scripts, docs). This charter's earlier
  entries were rewritten to the new names wholesale.
  Sixth addendum (same day, user-directed): ExternalToolProjects deleted entirely
  (incl. HelloWorld; the external-tools mechanism in the frontend stays), and the
  `quicknes/` directory is GONE â€” miniHawk no longer carries the quickerNES submodule.
  Consequences handled: the witness test suite (.test/.sol/.state) is now VENDORED at
  `minihawk-tests/suite/` (snapshot of upstream `tests/`; run-level-b.ps1 reads from
  there, making the Level B gate fully self-contained modulo ROMs); the native build
  recipe (bizinterface.cpp + Makefile) moved into
  `minihawk-cores/quickernes/native-source/` with a `QUICKERNES_ROOT` variable pointing
  at an external quickerNES clone; quickernes.yml clones upstream instead of using a
  submodule. The submodule working tree contained untracked files (controller.hpp and
  a vendored copy of the original blargg quickNES) â€” backed up to
  Documents/ClaudeSessions/quickernes-submodule-backup before deletion. Stale
  submodule.* entries for long-deleted submodules were purged from local .git/config.
  Fifth addendum (same day, user-directed): ExternalProjects dead weight deleted â€”
  FlatBuffers.GenOutput (no consumers), LibBizAbiAdapter + the WaterboxAdapter/
  MsHostSysVGuest code in BizInvoke (waterbox pile), TestromSuiteReportProcessor,
  BizHawk.AnalyzersTests, and BizHawk.Analyzer itself (user: no code-checking needed;
  References/BizHawk.Analyzer.dll + Common.props wiring removed). AnalyzersCommon KEPT:
  despite the name it's shared plumbing imported by the three source generators, which
  generate runtime-necessary code. ExternalToolProjects trimmed to HelloWorld + shared
  props/targets (DATParser/DBMan were gamedb tooling; AutoGenConfig/FakeTemporalAA were
  dev experiments) â€” the external-tools mechanism itself stays. Stale CI deleted
  (.gitlab-ci.yml, mame/waterbox/release workflows); ci.yml rewritten minimal
  (build sln + package); quickernes.yml and quicknes/make now install the native to
  minihawk-cores/quickernes/natives. Still present, deliberately: ExternalProjects/SDL2
  (81 MB of SDL+libusb submodules, only needed to rebuild SDL2.dll from source),
  iso-parser + libchd-rs-capi (fall with DiscSystem if the disc decision goes that way),
  NLua/LibBizHash/HawkQuantizer/SrcGens (sources of live References/Assets binaries).
  Also in this round: ExternalProjects/librcheevos (build project + rcheevos submodule)
  removed; stale waterbox/llvm-project gitlink dropped from the index; legacy example/dev
  Lua scripts deleted (ButtonCount, Input_Display, JoypadIntersection, MovieClock,
  migration_helpers, tasjudy, UnitTests â€” Lua/ keeps only socket/ and mime/, which are
  luasocket binary modules wired into package.cpath, and _docs_luacats, the Lua API type
  stubs); empty leftover directories from earlier phase deletions cleaned from disk.
- **Phase 4 â€” Harden & prove generality.** Manifest/versioning, package validation,
  error UX, core-author docs. Port a second core to prove the contract isn't
  QuickerNES-shaped.

## Witness harness (two levels, both must pass at every phase boundary)

The quickerNES test format: each `.test` is JSON naming a ROM (+ expected SHA1, optional
initial `.state`, controller types) and a `.sol` input sequence (one line per frame,
jaffar format, e.g. `|..|........|`). The native tester (`quicknes/core/source/tester.cpp`)
replays the sequence and emits a MetroHash of NES low RAM (2KB) as the verdict; cycle
types `Rerecord`/`Full` additionally do a savestate save+load around every frame, which
is exactly the TAS-critical property.

- **Level A â€” core payload guard.** Build and run the native quickerNES tester over all
  34 tests. Validates that the native DLL we package never drifts. Runs everything,
  including the two tests with initial `.state` files.
- **Level B â€” full-stack witness (the real one).** Drive EmuHawk itself: load ROM, feed
  the `.sol` inputs through the frontend input pipeline (Lua harness or generated `.bk2`
  movies), dump final 2KB RAM domain, byte-compare against golden dumps recorded from
  unmodified BizHawk in Phase 0. Also run a per-frame savestate save/load variant
  (mirroring `Rerecord` cycle type) to exercise the frontend statable path.
  `bizinterface.cpp` already supports the Arkanoid paddle types the arkanoid tests need.

Level B sharp edges, to resolve in Phase 0: input-string â†’ BizHawk controller mapping
must be validated button-by-button; the two initial-`.state` tests (microMachines,
saiyuukiWorld.lastHalf) use quickerNES-native state format and may remain Level-A-only;
BizHawk power-on state must be confirmed identical to the bare core's.

Witness-set exclusions found in Phase 0 (28 of 31 at Level A; 26 at Level B):
- `castlevania3.playaround`: mapper 5 (MMC5) is deliberately disabled in the
  BizHawk-pinned TASEmulators/quickerNES fork (poor QuickNES MMC5 support) â€” excluded.
- `novaTheSquirrel.anyPercent`: pinned core segfaults in `Core::serializeState` on
  mapper 30 (UNROM 512) before emulation starts. Pre-existing fork bug, likely affects
  stock BizHawk too â€” excluded pending separate investigation.
- `arkanoid2.arkFamicomController`: local `Arkanoid II (Japan).nes` dump SHA1 does not
  match the test's expected dump â€” excluded until the matching dump is available.
- `rcProAmII.race1` / `superOffroad.anyPercent`: upstream's quickNES-vs-quickerNES
  comparison fails, but quickerNES itself runs and hashes fine â€” IN scope (miniHawk
  Level A compares quickerNES hashes against stored goldens, not core-vs-core).
- Note: the native tester build needs `-Wno-unused-but-set-variable` appended to
  `commonCompileArgs` under GCC 13+ (applied to the WSL build copy only, not the
  submodule).

Phase 0 discoveries about frame alignment (validated by per-frame RAM comparison):
- EmuHawk emulates exactly ONE frame during ROM load, before a `--lua` script's
  first line executes. A naive Lua replay is therefore one frame late relative to a
  power-on input sequence. `replay.lua` compensates with `client.reboot_core()` at
  script start (verified `startframe=0` afterward). Remember this when miniHawk later
  aligns `.bk2` movies with tester `.sol` sequences.
- quickerNES `emulate_skip_frame` (rendering disabled, used by the native tester) was
  verified state-equivalent to `emulate_frame` â€” rendering on/off does not affect RAM.
- EmuHawk power-on RAM state is byte-identical to the bare core's (no adapter-side
  initialization differences).
- Lua `joypad.set` input passes through the SOCD (opposing-directions) filter
  (`UdlrControllerAdapter`), whose default `Priority` policy silently rewrites
  simultaneous L+R / U+D â€” which the TAS movies use heavily. The harness config sets
  `OpposingDirPolicy: 2` (Allow). Note for miniHawk: `.bk2` movie *playback* bypasses
  this filter (it taps the chain after the SOCD adapter); only Lua/user input is
  affected.
- The native tester parses but IGNORES console Reset/Power flags during replay
  (`input.reset` is dead in `advanceState`); the harness mirrors that.
- Lua `joypad.setanalog` axis sticky-holds never reach the output controller in this
  BizHawk build (axes die between `StickyHoldController` and the final controller);
  axes must be delivered via `joypad.setfrommnemonicstr`, which routes through
  `ButtonOverrideAdapter` â†’ `Controller.Overrides()`. Worth revisiting when miniHawk
  owns the input pipeline.
- Phase 0 witness status: 26/26 Level B tests PASS byte-identical to native ground
  truth in BOTH modes (simple replay AND per-frame savestate rerecord); Level A
  goldens recorded for 28/28.
- Core finding for upstream quickerNES: a full-state savestate round-trip is NOT
  lossless for `gimmick` (Sunsoft FME-7) and `superOffroad` â€” the native core itself
  perturbs state on deserialize+advance (EmuHawk mirrors it byte-exactly, so the
  frontend is faithful; the incompleteness is in core serialization). Deterministic,
  so the witness remains sound, but worth an upstream look.
- Core finding for upstream: pinned fork segfaults in `Core::serializeState` on
  mapper 30 (novaTheSquirrel) â€” see witness-set exclusions.

## Architecture facts informing the plan

- `CoreInventory` already discovers cores via reflection (`[Core]` + `[CoreConstructor]`
  attributes) and its constructor already accepts arbitrary assembly type lists â€” the
  plugin inversion is small.
- Only `BizHawk.Client.Common` references `Emulation.Cores` at the project level; the real
  coupling is ~96 frontend files using concrete core types, mostly deletable for miniHawk.
- `GenericCoreConfig` (reflection-based settings UI) already exists; per-core config
  dialogs are not needed.
- EmuHawk targets `net48`: no assembly unload, hence the load-once model.
- Determinism is sacred: anything touching emulation, input, or timing must preserve
  frame-exact reproducibility or movies desync.
