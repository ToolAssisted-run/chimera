# Chimera - Project Charter

A minimal, core-agnostic TAS frontend derived from BizHawk. Emulation cores are removed
from the tree and loaded on-the-fly as external, self-contained packages just before ROM load.

## Objectives (testable)

1. **Zero core code in the repo.** Chimera builds and runs with no emulation core in the
   solution. Cores arrive as single-file packages (`.chimeraCore`, a zip: manifest + managed adapter DLL +
   native DLL(s)), discovered from a `Cores/` directory and loaded at ROM-load time.
2. **Published core contract.** The core-facing API (essentially `Chimera.Emulation.Common`
   + `Chimera.NativeInvoke`) becomes a versioned, published contract. A core package must be
   buildable outside the Chimera repo against that contract alone.
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
   generic disassembler). Everything else - core-specific config dialogs and viewers,
   movie importers, RetroAchievements, per-system Lua libs, etc. - is removed.

## Pillar: reproducibility belongs to the core, not the frontend

Chimera is NOT the keeper or enforcer of reproducibility - it REQUIRES it from
cores. A movie's reproduction contract is the pair (movie, core package): if the
selected core package is exactly the same, the movie must reproduce regardless of
Chimera's version or the versions of any frontend library (SDL, Lua, zstd, cimgui,
...). Consequences: frontend native dependencies may be upgraded freely and are
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
- ROM data: RomGame is format-agnostic - the core receives the file's exact bytes, no
  header detection/stripping/per-system preprocessing (done in P3.2, and now
  load-bearing for this pillar, not just cleanliness).
- Archive extraction (zip -> rom bytes): bit-exact by the archive format's definition.
- IPS/BPS patching: a frontend transformation of rom bytes. Tolerable only because
  those formats are deterministic by spec; the implementation must stay spec-exact and
  must never grow heuristics.
- Movie input log: the mnemonic format is effectively FROZEN - its parsing is part of
  the reproduction contract and must be spec-stable across Chimera versions.
- Sync settings: opaque JSON round-tripped to the core's types; the frontend must
  never interpret or migrate them.
- Disc data [RESOLVED 2026-08-25]: Chimera.Emulation.DiscSystem is REMOVED. It parsed
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

## Core flavors and their reproducibility profiles

Three "flavors" of emulation core exist from Chimera's perspective (user-stated,
2026-08-11):

- **(a) Native cores** (e.g. QuickerNES): compiled per platform (Windows .dll /
  Linux .so) and run natively on the host. Their reproducibility depends on a
  good implementation of their own serialization/deserialization mechanisms.
  Windows<->Linux inter-reproducibility is desirable but not guaranteed.
  (Empirical note: the QuickerNES witness core has so far proven byte-exact
  across compilers and OSes - the 2026-08-11 Linux gate reproduced the
  Windows-recorded goldens with a Linux-gcc-built core - but that is an
  observed property of this core, not a guarantee of the flavor.)
- **(b) Pure cores** (implemented fully in C#, e.g. NesHawk in upstream
  BizHawk): must equally guarantee reproducible serialization, but are
  universally cross-compatible as long as the system runs C# and the C#
  engines are faithful (very slight chance of desync between engines). They
  require no runtime loading of precompiled native libraries.
- **(c) Waterboxed cores** (e.g. GPGX, DOSBox-X and many others in upstream
  BizHawk): use the waterbox closed system to behave as a universal Linux
  machine whose entire machine state is preserved. These offer the maximum
  level of reproducibility, but depend on the waterboxing machinery to work,
  which requires careful integration and other complications that do not
  exist in native builds.

Relation to the package contract as it stands: the current contract and the
reference package are flavor (a) (managed adapter + native library). Flavor (b)
is already expressible - a package whose manifest declares no natives. Flavor
(c) would require reintroducing the waterbox host machinery, which was removed
in Phase 2 (recoverable from the transitional fork's history) - a deliberate
open question for whenever a waterboxed core is wanted.

**Side-effect freedom (non-waterboxed cores; user-stated, 2026-08-11).** For
waterboxed cores the sandbox enforces isolation; flavors (a) and (b) must
uphold it by discipline: a core must contain NO side effects. Like the
reproducibility pillar, this rule CANNOT be enforced by Chimera - it is the
core developer's responsibility. Chimera states the requirement in the
contract documentation and trusts core packages to honor it; there is no
sandbox, no auditing machinery, and none will be added for it. Concretely: no
writing to the filesystem; no reading directly from the filesystem (ALL data -
roms, firmware, databases, assets - must arrive through the BizHawk interface);
no direct I/O of any kind (with rendering-only channels such as OpenGL as the
one permissible exception); no syscalls; nothing that causes a change in the
host system that cannot be captured and reverted by a savestate save/load.
The test is exactly that: if any observable effect of running the core is not
round-tripped by save/load state, the core is in violation. (Plain memory
allocation within the core's own lifetime is fine - it is torn down with the
core and carries no cross-run state.)
RESOLVED (same day): the QuickerNES adapter's one violation - the BootGod
cart DB reading NesCarts.xml from its package directory - was reduced to a
compiled-in PAL/Dendy hash list (PalHashList.cs, 370 SHA1s of PRG+CHR,
extracted once from the database with first-entry-wins semantics preserved
for dual-region dumps). The only behavioral use of the database was
rejecting PAL carts (the core is NTSC-only); the good-dump/name metadata it
also produced had no remaining consumers and is gone. The QuickerNES core
now reads nothing from the filesystem at runtime.

## Agreed decisions

- **Core package interface:** managed .NET adapter DLL implementing `IEmulator` (+ service
  interfaces) against the published contract, bundled with its native core DLL(s).
  Not a C ABI, not libretro.
- **Repo strategy:** trim this BizHawk checkout in place on a branch. Keep git history and
  upstream diffability. Rename to Chimera once stable.
- **Reload model:** load-once per process (net48 cannot unload assemblies). Packages load
  lazily at ROM-load time; swapping a loaded core's version requires app restart.
- **Witness core:** QuickNes (backed by the TASEmulators/quickerNES submodule) - single
  native DLL, no waterbox, no discs, no firmware. It stays in-tree until Phase 3.

## Procedure

Each phase ends with: build green -> witness movie syncs -> commit. Never more than one
phase of unverified change.

- **Phase 0 - Baseline & harness.** Build as-is. Stand up the two-level witness harness
  (below), generate golden RAM dumps from unmodified BizHawk, and confirm they agree with
  the native quickerNES tester's ground truth. No product code changes.
- **Phase 1 - Shrink to one core, statically. [DONE 2026-08-09]** All cores except
  QuickNes removed (~312k lines deleted); frontend trimmed (per-core dialogs/tools,
  RetroAchievements, movie importers, per-system Lua libs, Libretro/MAME paths, system
  menus except NES). Core asset payloads, core submodules, and native core source trees
  removed (Assets/dll: 90 -> 28 files, all frontend infra + libquicknes). Kept: Waterbox
  host + waterbox/ native tree (OPEN DECISION pending), quicknes/, SDL2 + rcheevos +
  chd/chimerahash/zstd/blip_buf natives (still referenced by frontend/DiscSystem), gamedb.
  Witness at phase close: 26/26 simple AND 26/26 rerecord, byte-identical goldens,
  after a from-clean rebuild.
- **Phase 2 - Publish the contract. [DONE 2026-08-09]** `CoreInventory`, `CoreNames`,
  and the reflection-attribute construction machinery are gone (per user decision, along
  with waterbox, the 6502 disassembler, and the trace logger - all recoverable from git
  history). In their place:
 - `ICoreFactory` + `CoreCreationContext` + `IRomAsset` in **Chimera.Emulation.Common**
   - the published core contract. A core ships a factory: name, system IDs, core type
    (stable key for persisted settings), settings/sync-settings types, and `Create(ctx)`.
 - `CoreRegistry` (Client.Common) indexes factories by system; RomLoader's
    `MakeCoreFromRegistry` replaces the old inventory path (same preferred/forced-core
    and error semantics).
 - `CorePackageLoader` (Client.Common): at startup, `<exe>/Cores/` is scanned for
    package directories or zips containing `chimera-core.json` (formatVersion 1:
    name, assembly, factoryTypes[]). Zips extract to `Cores/_cache/<name>` keyed by
    zip timestamp; the package dir is prepended to PATH for native DLL resolution;
    an AppDomain.AssemblyResolve hook serves package assemblies so persisted
    settings / movie sync-settings JSON ("Type, Assembly" names) round-trip.
 - QuickNES is registered through `BuiltInCoreFactories` (transitional; Phase 3
    replaces it with a real external package and deletes it).
  Witness at phase close: 26/26 simple AND 26/26 rerecord through the new loader.
- **Phase 3 - Evict the witness. [DONE 2026-08-09]** The QuickNES adapter now lives in
  `chimera-cores/quickernes/` - NOT part of Chimera.sln, built by `build-package.ps1`
  against the contract DLLs from `build/dll` exactly as an out-of-repo package would be,
  and shipped as `build/Cores/quickernes.chimeraCore` (manifest + Chimera.QuickerNES.dll +
  libquicknes natives). `Chimera.Emulation.Cores` is deleted from the solution entirely:
  **Chimera contains zero core code**. Supporting changes: INESPPUViewable and the NES
  palette helpers moved into Emulation.Common (frontend PPU viewers stay core-agnostic);
  BootGod NES cart DB init moved into the package factory; virtual pad schemas are
  discovered from registered factory assemblies; per-core settings dialogs replaced by
  the generic config dialog (`QuickNesConfig` and its FCEUX-palette import are gone - 
  revisit if missed); `emu.setrenderplanes` is a no-op pending a contract story; the
  Debug-only core-poking dev menu was deleted. Witness at phase close: 26/26 simple AND
  26/26 rerecord with the core loaded from the zip package.
  Addendum (same day): per user principle - Chimera must contain NOTHING core-specific
 - the Phase 3 compromises were purged too: INESPPUViewable and the NES palette code
  moved from Emulation.Common into the package; the NES PPU/nametable viewer tools, the
  NES menu, and the QuickNes icon were deleted from the frontend; NesCarts.xml moved
  from gamedb into the package zip (BootGod reads from the extracted package dir);
  NES palette files moved to chimera-cores/quickernes/palettes.
  Second addendum (deeper cleanse, same day): gamedb is GONE entirely (all per-system
  hash DBs, the Database class, DB-entry generation service). ROM->system routing is now
  purely: (1) user extension preference, (2) the extension->system map that core-package
  manifests declare ("extensions" field), (3) the platform-chooser prompt, which lists
  only systems provided by loaded packages. RomGame is format-agnostic (no header
  detection/stripping, no per-system preprocessing; hash = whole file - NOTE: game
  hashes recorded in old movies will no longer match, warning-only). Also removed: the
  FirmwareDatabase CONTENTS (mechanism kept, empty; packages could register records
  later), per-system display-name tables (system ID is the display name), per-system
  default path tables (path sets generated on demand per system), the frame-rate table
  (display fallback is a flat 60/50), the GameShark converter and all per-system
  cheat-code decoders, and the game-DB button in the log window.
  Phase 3 sharp-edge, confirmed in the wild: persisted configs and movie sync-settings
  embed `"Type, AssemblyName"` - entries written before eviction name the deleted
  `Chimera.Emulation.Cores` assembly, and Newtonsoft's `$type` binder then throws
  (silently swallowed -> defaults, first caught as two sync-settings-dependent witness
  failures). Fix: the manifest's `supersedesAssemblies` list - the resolver serves the
  package assembly under its legacy names, keeping old configs AND pre-eviction movies
  round-tripping.
  Third addendum (Assets cleanse, same day): deleted from Assets - 8 orphaned native
  libs (librcheevos.dll/.so [RetroAchievements was removed], freetype.dll, libpng16.dll,
  zlib.dll [nothing P/Invokes them and no remaining native lib imports them - verified
  by PE import-table scan], and the MinGW runtime trio libgcc_s_seh-1/libstdc++-6/
  libwinpthread-1 [only ever needed by MinGW-built cores; every remaining native uses
  msvcrt or the UCRT]); all per-system example Lua scripts (Doom/GBA/Genesis/N64/NDS/
  NES/PCE/SNES dirs); the bsnes-gamma shader (SNES color correction). N3DSHasher.cs
  (3DS-specific) deleted from Emulation.Common. defctrl.json is GONE from the frontend:
  per-system controller defaults are now package-provided - a package may ship its own
  defctrl.json (same DefaultControls shape, keyed by controller-definition name);
  CoreRegistry merges them (first package wins per name) and InputManager.SyncControls
  adopts package defaults for any controller the user's config has never seen. A
  user-saved defctrl.json next to the exe (written by the controller dialog's "save
  defaults") still wins over package defaults. The quickernes package ships the NES
  Controller bindings. [Superseded 2026-08-13, see "Key bindings arrive with the
  core": the packages lost their file in the waterbox-only redesign below, and the
  exe-side defctrl.json - along with the dialog's Save Defaults - is now gone too.] Natives KEPT deliberately: chd_capi (DiscSystem - disc story
  still an open decision), cimgui/SDL2/OpenAL32/lua54/libzstd/libchimerahash/e_sqlite3
  (frontend infrastructure). blip_buf was initially kept as contract-offered audio
  resampling, then REMOVED at user request (wrapper class, natives, C source dir,
  arm64 prebuilt): nothing in the tree used it, and a core that wants it (e.g. a
  future Gambatte package) should bundle its own copy.
  Sharp edge #2, caught by the witness harness itself: the original zip-extraction
  cache (delete stale dir, extract, write stamp file) raced when 8 Chimera instances
  launched simultaneously against a cold cache - ZipFile.ExtractToDirectory threw
  "file already exists", and since extraction ran outside the per-package try/catch it
  reached the top-level exception dialog, which on the hidden desktop blocks invisibly
  until timeout. Fix: cache dirs are keyed by zip timestamp (`<name>-<ticks>`), zips
  extract to a private temp dir followed by an atomic Directory.Move - race losers
  discard their temp and use the winner's dir; stale caches are cleaned best-effort;
  per-zip extraction failures are caught and logged instead of killing startup.
  Fourth addendum (same day, user-directed): Assets/Shaders and the entire .cgp
  retro-shader feature removed (RetroShaderChain/Preset/Pass, RetroShader, hq2x/
  scanlines/bicubic/user chains, TargetDisplayFilter + TargetScanlineFilterIntensity +
  DispUserFilterPath config, the Scaling Filter UI group, the Bicubic final-filter
  option [legacy setting falls back to bilinear], the Chimeraware.Test shader demo app;
  client.get/settargetscanlineintensity are warning no-ops now). The built-in
  hand-coded presentation shaders in Chimeraware.Graphics remain - they ARE the display
  pipeline. DiscoHawk removed entirely (app project, DiscoHawkLogic, sln entry, docs),
  taking the stale upstream release packaging with it: Package.sh, the packaging bats,
  and the whole nix ecosystem (default.nix, Dist/*.nix, docs, CI workflow) - dead
  since they referenced gamedb/waterbox/defctrl anyway. blip_buf removed (see above).
  Eighteenth addendum (2026-08-11, user-directed - TOTAL QUICKERNES EVICTION):
  absolutely nothing from QuickerNES remains in this repository; anything
  important moved to the quickerNES repo's chimera/ dir. Concretely: the
  entire witness apparatus - vendored suite (reversing the earlier vendoring
  decision), Level A+B goldens, replay.lua, bootstrap.lua, both run-level-b
  drivers, hidden-run.ps1, native/dumper.cpp, tests/README - moved to
  quickerNES chimera/tests/ (drivers grew --chimera-root/-ChimeraRoot,
  defaulting to a sibling Chimera or BizHawk checkout); stale QuickNesConfig
  Compile Update items purged from the Chimera csproj. The quickerNES witness
  remains the commit gate, run from its new home, until the synthetic witness
  (see "The synthetic witness" section) replaces it: three synthetic cores
  (one per flavor of the core taxonomy) sharing exact emulation logic, video,
  and audio; a stateful-interpreter emulator whose game logic and assets live
  in .testrom files; tests that win, lose, and reproduce specific video and
  audio outputs, byte-compared across all three cores - starting with the
  native flavor. In the same round the native flavor LANDED in full (see the
  synthetic-witness section status) and run-witness.sh became THE smoke test,
  retiring the short-lived --quick subset from the quickerNES drivers; the
  side-effect-freedom rule for non-waterboxed cores was stated (see the
  core-flavors section; core-dev responsibility, not enforced by Chimera)
  and QuickerNES's NesCarts.xml read - its one violation - was reduced to a
  compiled-in PAL hash list in the adapter. Also fixed: `meson compile
  frontend` no longer blocks ~15 minutes on lingering msbuild node-reuse
  workers (/nodeReuse:false -p:UseSharedCompilation=false). Gate at this
  round: 26/26 simple + 26/26 rerecord, run from the quickerNES repo against
  this tree with the PAL-list package; synthetic witness 12/12.
  Seventeenth addendum (2026-08-11, user-directed - LINUX GATE + HEADLESS + layout;
  the sixteenth - an exec-bit fix and charter note - was authored on the Windows box
  and is pending its push): the witness gate now runs on Linux. tests/run-level-b.sh
  (+ bootstrap.lua) is a faithful port of run-level-b.ps1 - Chimera under Mono on a
  private Xvfb display replacing the hidden Windows desktop, same job protocol and
  the same goldens. First-ever Mono run of this frontend; 26/26 simple + 26/26
  rerecord byte-identical to the Windows-recorded goldens - the reproducibility
  pillar demonstrated across an entirely different OS, runtime (Mono vs .NET
  Framework), and display stack. Sharp edge: the package loader's preload-then-
  dedupe native contract silently assumed Windows LoadLibrary basename-dedupe
  semantics; Linux dlopen only dedupes via DT_SONAME, so libquicknes.so must be
  linked -Wl,-soname,libquicknes.so (fixed in the quickerNES adapter's Makefile,
  which also tracks the upstream cpu.cpp split and jaffarCommon macro rename;
  build-package.sh there is the pwsh-free package builder). The failure mode was
  the invisible-modal-dialog trap again, now closed for good by --headless: a new
  CLI option for unattended runs - every modal dialog funnels through six choke
  points in DialogControllerWinFormsExtensions and in --headless mode logs its
  caption/text to the console and exits with code 64 instead of blocking;
  warning-only dialogs in Program (config version/corrupt config/display fallback/
  superuser) log and continue; fatal run-loop exceptions log and exit nonzero
  (previously they could exit 0 after an invisible dialog). Both witness drivers
  pass --headless, and both gained --quick/-Quick: a six-test smoke subset (~1 min)
  for CI and dev loops - explicitly NOT a substitute for the full commit gate.
  Layout/docs in the same round: CHIMERA.md moved to docs/design-principles.md
  (references updated); LICENSE separates copyright (BizHawk team = inherited
  code, Sergio Martin 2026 = Chimera modifications and new work); accidentally
  committed build output purged (root libchd_capi.so and 490 files under
  extern/libchd-rs-capi/chd-build/, now gitignored); all prose files converted to
  pure ASCII after a committed double-encoding accident (em-dashes had become
  mojibake; ASCII-only prose renders identically under every viewer encoding -
  keep it that way). HISTORY POLICY CHANGE (user, 2026-08-11): the amend-commit-0
  model ends here; from now on each gated round lands as a new commit.
  Fifteenth addendum (2026-08-10, user-directed - MESON, LINUX-HOSTED, DUAL-TARGET):
  the canonical build is now meson on Linux (WSL locally; Linux runners in CI),
  producing BOTH OSes' artifacts: managed IL built once via dotnet (run_target
  'frontend', Linux dir only), natives built per-target - native gcc for .so, and
  mingw-w64 cross (static gcc runtime, so no libgcc/winpthread dlls) for .dll via
  extern/meson/mingw-w64.ini + mingw-toolchain.cmake. Root meson.build: direct
  shared_library targets for chimerahash/lua54/e_sqlite3/cimgui/luasocket (luasocket's
  two same-named "core" modules live in extern/meson/luasocket-*/ subdirs), nested
  upstream builds via extern/meson/nested-build.sh for zstd (its own meson), SDL2 +
  openal (cmake + toolchain file), chd_capi (cargo, --target x86_64-pc-windows-gnu
  for cross). WSL provisioned: mingw-w64 gcc 13, rustup 1.97.1 + windows-gnu target,
  and - after packages.microsoft.com's Ubuntu 24.04 feed turned out to serve a
  source-build SDK (8.0.129) WITHOUT WindowsDesktop targets, confirming the
  UbuntuMispackagedSDKCheck trap firsthand - Microsoft's own SDK binary (8.0.423)
  via dotnet-install.sh into ~/.dotnet. The
  Windows msbuild BuildNativeDeps hook is now Condition Windows_NT - a dev shim.
  Sharp edges found: mingw builds of libusb need our own config.h (msvc/config.h is
  guarded; and _TIMESPEC_DEFINED must NOT be defined - it's mingw's own timespec
  guard); meson passes builddir-relative paths to custom_target commands (absolutize
  before cd-ing tools like cargo); meson target names collide within a directory
  (same-named outputs need subdirs); WSL /tmp is volatile (log to /mnt/c); and the
  nastiest - upstream builds decorate library names per platform convention (mingw
  meson emits libzstd-1.dll + libzstd.dll.a), and a filename glob happily shipped the
  import-library ARCHIVE as "libzstd.dll", which passed everything until LoadLibrary
  returned ERROR_BAD_EXE_FORMAT and the witness went 0/26 - nested-build.sh now
  matches exact/version-infixed names and excludes .a/.d/.def. Witness after fix:
  the Windows gate ran against the Linux-cross-built dlls - the frontend stack
  (SDL2, lua54, zstd, cimgui, chimerahash all mingw-built) byte-exact at 26/26 + 26/26,
  which is also the deferred-reproducibility pillar demonstrated empirically:
  an entirely different compiler family under the frontend, identical emulation.
  Fourteenth addendum (2026-08-10, user-directed): Chimera.sln, Common.props, and
  Directory.Packages.props moved into source/ (sln project paths relativized;
  extern/ gained a thin Directory.Packages.props forwarding to source/'s, since NuGet
  discovers that file by walking up from each project). Root is now four directories
  (build/ extern/ source/ tests/) and five files (.gitignore .gitmodules LICENSE
  CHIMERA.md README.md). STATED DIRECTION (user): the entire build system - Windows
  and Linux targets - should eventually be mediated solely by MESON; the current
  msbuild-invokes-build-natives.ps1 arrangement is a transitional shim that meson
  will absorb (meson orchestrating the dotnet build and every native recipe).
  Thirteenth addendum (2026-08-10, user-directed - NATIVES FROM SOURCE): ExternalProjects
  moved to extern/ at the repo root, and ALL prebuilt native libraries are gone from the
  tree. extern/build-natives.ps1, run automatically by the solution build
  (BuildNativeDeps target, incremental by timestamp), builds every native dependency
  from source into build/dll: libchimerahash + SDL2 (in-repo recipes; clang-cl - chimerahash's
  gcc target-attributes need it, and a .def now provides the exports ELF visibility
  used to), and six NEW pinned submodules under extern/ with new recipes - lua v5.4.8
  (stock, LUA_BUILD_AS_DLL), zstd v1.5.7 (cmake -> libzstd.dll), cimgui 1.90.6 (pinned
  to ImGui.NET 1.90.6.1's binding version), openal-soft 1.24.3, ericsink/cb's sqlite3
  amalgamation with the canonical e_sqlite3 defines (mirrored from cb's generator),
  luasocket v3.1.0 (socket/mime core.dll against our lua54 import lib), plus chd_capi
  built via cargo (rust-toolchain.toml pins 1.97.1; user installed rustup for this).
  Toolchain: VS2022's clang-cl/CMake/Ninja (probed by path, works on GitHub
  windows-latest runners unchanged). Per the reproducibility pillar, all version pins
  are for BUILD reproducibility only - never for movie determinism, which belongs to
  the core package. Assets/ is GONE entirely: ChimeraMono.sh moved into the Chimera
  project (copied to build/ by the PostBuild target), the LuaCATS API doc stubs were
  DELETED outright (pure editor documentation, annotation-only with error() guards,
  never read by the frontend, and partially stale against Chimera's actual API;
  recoverable from history or regenerable from the [LuaMethod] attributes if ever
  wanted), and the Assets/** copy glob was removed. Root is now
  build/ extern/ source/ tests/ + eight files.
  Twelfth addendum (2026-08-10, user-directed - EXPLICIT CORE LOADING + layout): cores
  are no longer discovered. The startup Cores/ directory scan is gone; loading a core
  is an explicit act: File > Open Core... (file prompt for a package dir/zip) or the
  new --core=<path> CLI option (which the witness driver now passes). Open ROM and
  Recent ROM stay DISABLED until a core is loaded - deliberately, so core (sync)
  settings can be configured BEFORE the first rom load (config-before-load principle).
  The Config > Core Settings menu rebuilds on every package load; zip caches now live
  under <exe>/CoreCache. Layout changes in the same round: chimera-cores/ deleted
  (author docs live in this charter + the quickerNES reference package);
  chimera-tests/ renamed tests/; ExternalProjects/ moved under source/; root purged
  to the minimum (removed: .github incl. all CI, .vscode, .config, .editorconfig,
  .global.editorconfig.ini, .stylecop.json + the Common.props StyleCop block,
  appveyor.yml, SECURITY.md, contributing.md [README gained a Contributing section],
  global.json, sln.DotSettings, .git-blame-ignore-revs, "Building Other
  Solutions.txt").
  Eleventh addendum (2026-08-10, user-directed): Chimera is a STANDALONE repository
  (github.com/SergioMartin86/Chimera) with a FRESH history - commit 0 is the
  post-separation state. The BizHawk ancestry (23,722 commits) is deliberately not
  carried; it remains available in the upstream BizHawk repository and in the
  transitional SergioMartin86/BizHawk fork, which is also the recovery source for
  deferred machinery (waterbox, trace logger, disassembler, Gambatte sources for
  Phase 4). Future upstream ports are cherry-picked/rebased without shared ancestry.
  Tenth addendum (2026-08-10, user-directed - FULL SEPARATION): the QuickerNES adapter
  no longer lives in this repo at all. Everything quickerNES-specific
  (chimera-cores/quickernes/ and the quickernes.yml workflow) moved to the quickerNES
  repository itself (github.com/SergioMartin86/quickerNES, branch chimera-adapter,
  `chimera/` dir): managed adapter, native chimerainterface + Makefile (now building from
  that repo's own sources), manifest, bundled data, prebuilt natives, build-package.ps1
  (param -ChimeraRoot, default sibling ../BizHawk checkout; installs quickernes.chimeraCore
  into <ChimeraRoot>/build/Cores), and a native-build CI workflow. The dev loop:
  build Chimera sln -> run ../quickerNES/chimera/build-package.ps1 -> witness gate.
  chimera-cores/ here is reduced to the core-author guide README. The vendored witness
  suite stays in chimera-tests/suite/ (user decision: pinning the gate to what the
  goldens were recorded against is a feature). Gate ran against the
  quickerNES-repo-built package: 26/26 simple + 26/26 rerecord.
  Ninth addendum (same day, user-directed): References/ deleted - no committed managed
  binaries remain. The source generators are ProjectReferences with
  OutputItemType=Analyzer; NLua/ISOParser/HawkQuantizer are plain ProjectReferences
  (built transitively, not sln members); SettingsUtil is built via a
  ReferenceOutputAssembly=false reference from Chimera and copied into build/dll
  (ProvideCoreAuthorKit target) so the core-author kit is contract DLLs + settings
  generator in one directory - the quickernes package's Analyzer path points there.
  Sharp edge #3: solution builds UNSET Configuration/Platform for ProjectReferences
  that aren't solution members - everything above silently built as Debug inside a
  Release build until Common.props set
  ShouldUnsetParentConfigurationAndPlatform=false. Also, LibCommon.props had a
  PostBuild copying outputs into References/, which would have silently resurrected
  the folder; removed. Only native binaries (C/C++ toolchains required) remain
  committed: Assets/dll frontend infra and the package's libquicknes.
  Eighth addendum (same day, user-directed): the entire Dist/ folder deleted - release
  packaging utilities (7za/zip/unzip/upx/fart/vswhere/ILMerge/NuGet binaries), Unix
  build wrapper scripts (two already broken by the rename), upstream release stamping,
  the upstream changelog, arm64 prebuilt natives nothing shipped anymore, and the
  git-hooks commit-message linter together with the InstallGitHooks build target in
  Chimera.Common.csproj that installed it on every build (code-checking, per user
  policy). Unix builds are plain `dotnet build Chimera.sln` now.
  Seventh addendum (same day, user-directed): standard folder nomenclature adopted - 
  `src/` is now `source/` (git mv, sln paths updated; likewise the package's `src/` ->
  `source/` and `native-src/` -> `native-source/`) and the build output dir `output/` is
  now `build/` (MainSlnExecutable.props, package HintPaths/build script, witness driver,
  .gitignore, launch.json, SDL2/libchd build scripts, docs). This charter's earlier
  entries were rewritten to the new names wholesale.
  Sixth addendum (same day, user-directed): ExternalToolProjects deleted entirely
  (incl. HelloWorld; the external-tools mechanism in the frontend stays), and the
  `quicknes/` directory is GONE - Chimera no longer carries the quickerNES submodule.
  Consequences handled: the witness test suite (.test/.sol/.state) is now VENDORED at
  `chimera-tests/suite/` (snapshot of upstream `tests/`; run-level-b.ps1 reads from
  there, making the Level B gate fully self-contained modulo ROMs); the native build
  recipe (chimerainterface.cpp + Makefile) moved into
  `chimera-cores/quickernes/native-source/` with a `QUICKERNES_ROOT` variable pointing
  at an external quickerNES clone; quickernes.yml clones upstream instead of using a
  submodule. The submodule working tree contained untracked files (controller.hpp and
  a vendored copy of the original blargg quickNES) - backed up to
  Documents/ClaudeSessions/quickernes-submodule-backup before deletion. Stale
  submodule.* entries for long-deleted submodules were purged from local .git/config.
  Fifth addendum (same day, user-directed): ExternalProjects dead weight deleted - 
  FlatBuffers.GenOutput (no consumers), LibBizAbiAdapter + the WaterboxAdapter/
  MsHostSysVGuest code in NativeInvoke (waterbox pile), TestromSuiteReportProcessor,
  Chimera.AnalyzersTests, and Chimera.Analyzer itself (user: no code-checking needed;
  References/Chimera.Analyzer.dll + Common.props wiring removed). AnalyzersCommon KEPT:
  despite the name it's shared plumbing imported by the three source generators, which
  generate runtime-necessary code. ExternalToolProjects trimmed to HelloWorld + shared
  props/targets (DATParser/DBMan were gamedb tooling; AutoGenConfig/FakeTemporalAA were
  dev experiments) - the external-tools mechanism itself stays. Stale CI deleted
  (.gitlab-ci.yml, mame/waterbox/release workflows); ci.yml rewritten minimal
  (build sln + package); quickernes.yml and quicknes/make now install the native to
  chimera-cores/quickernes/natives. Still present, deliberately: ExternalProjects/SDL2
  (81 MB of SDL+libusb submodules, only needed to rebuild SDL2.dll from source),
  iso-parser + libchd-rs-capi (fall with DiscSystem if the disc decision goes that way),
  NLua/LibChimeraHash/HawkQuantizer/SrcGens (sources of live References/Assets binaries).
  Also in this round: ExternalProjects/librcheevos (build project + rcheevos submodule)
  removed; stale waterbox/llvm-project gitlink dropped from the index; legacy example/dev
  Lua scripts deleted (ButtonCount, Input_Display, JoypadIntersection, MovieClock,
  migration_helpers, tasjudy, UnitTests - Lua/ keeps only socket/ and mime/, which are
  luasocket binary modules wired into package.cpath, and _docs_luacats, the Lua API type
  stubs); empty leftover directories from earlier phase deletions cleaned from disk.
- **Phase 4 - Harden & prove generality.** Manifest/versioning, package validation,
  error UX, core-author docs. Port a second core to prove the contract isn't
  QuickerNES-shaped.

## THE WATERBOX-ONLY REDESIGN (user-decided, 2026-08-11)

After full analysis of BizHawk's waterbox (docs/waterbox-analysis.md), a
fundamental design change: Chimera allows ONLY waterboxed cores, making
every core reproducible and portable BY FORCE rather than by discipline.
Decisions, all user-confirmed:

1. **Waterbox-only, no exceptions.** The package format knows only guest
   images. Flavors (a) native and (b) pure-C# are no longer expressible as
   packages; the taxonomy above remains as analysis, and the side-effect
   rule's "cannot be enforced" caveat inverts - the sandbox IS the
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
3. **One generic adapter, no per-core managed code.** A core package is a
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
   the managed host layer, and the waterbox's own conformance tests -
   independently testable without Chimera. Plan of record for the runtime:
   a C/C++ port replacing the imported Rust reference (drops the
   nightly-Rust requirement; validated differentially against it).

Migration order (each step gated by the synthetic witness, plus the
quickerNES suite until its core is migrated):
  (i) import + de-nightly the waterbox host runtime; meson dual-target;
      restore the Windows SysV ABI adapter (MsHostSysVGuest) from the
      transitional fork;
  (ii) define guest ABI v1 + package format v2 (data-driven controller/
      settings declarations); implement the generic adapter;
  (iii) stand up the toolchain repo; build synth.wbx from synthcore.c; the
      waterboxed synth becomes the SHIPPED synth package, while the native
      and pure-C# implementations remain as external ground-truth testers
      (Level A golden generators - an even cleaner witness split);
  (iv) migrate quickerNES to a .wbx guest; re-gate the full suite;
  (v) retire the flavor-(a)/(b) loading paths (manifest v1, natives lists,
      package-assembly resolution) from Chimera.

## The synthetic witness (planned successor to the quickerNES gate)

Plan (user-stated, 2026-08-11), building on the core-flavor taxonomy above:

1. **Three synthetic cores, one per flavor** - native, pure C#, and waterboxed -
   with exactly the same emulation logic, video output, and sound output.
2. **A synthetic game with non-trivial rules** and an input set.
3. **The test plan**: for a series of input movies, ALL of the memory space, the
   video output, and the sound output must match across all three cores.
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

Sequencing: the machinery for waterboxing and for C# core integration is not
yet defined, so work STARTS WITH THE NATIVE CORE ONLY; the pure-C# and
waterboxed twins follow when their integration stories exist. Until the
synthetic witness is complete enough to be the gate, the quickerNES witness
(now in the quickerNES repository) remains the commit gate.

Status (2026-08-11): the native flavor is fully implemented and passing -
SPEC.md v1; libsynthcore (the reference interpreter); the .sasm assembler;
the gridWalker rom (walls/hazards/goal/step budget, per-move beeps, win/lose
jingles); the synth-native package (adapter + manifest routing .testrom,
both OS natives); and run-witness.sh, the two-level driver: Level A replays
the four goal movies (win / lose / video output / audio output) natively and
checks final RAM plus FULL per-frame video and audio stream hashes against
goldens, with a serialize-every-frame rerecord self-check; Level B replays
the same movies through Chimera (Mono + Xvfb) and byte-compares the final
RAM and VRAM domains against the same goldens, both modes. ~10 seconds wall
clock, and it is THE Chimera smoke test (user decision, same day): the
quickerNES --quick smoke subset is retired; quickerNES's suite is the full
determinism gate only. Audio is Level A-verified only for now (the frontend
has no scriptable audio tap - an acceptable gap while the frontend audio
path stays core-agnostic).

Flavor (b) LANDED (same day): SynthMachine.cs is a from-spec pure-C#
reimplementation sharing no code with the native flavor, shipped as
synth-sharp.chimeraCore - a package whose manifest declares an empty natives list;
nothing is loaded at runtime, upholding side-effect freedom trivially. The
C# core integration story turned out to already exist: the contract and
package loader handle a natives-free package unchanged. Cross-flavor
equivalence is proven by the witness: the C# Level A tester and the
frontend-loaded package both reproduce the NATIVE-recorded goldens
bit-exactly - RAM, full per-frame video stream, full audio stream, all
four goal movies, both replay modes (24/24). Goldens are only ever
recorded from the native reference; other flavors must match, never
re-record.

Flavor (c) LANDED (2026-08-11): synth.wbx is the SAME synthcore.c
compiled for the miniBox waterbox sandbox (see the miniBox repository at
extern/chimera-common-minibox - a from-scratch C/C++ port of BizHawk's waterbox host,
GCC-built guest toolchain, machine spec, all meson) and wrapped in the
waterbox ABI (tests/synth/package-box/). Its whole machine state lives in
guest memory, so the waterbox host savestates it AUTOMATICALLY - no
explicit serialize/deserialize, the defining property of the flavor. The
Level A tester runs it through the miniBox host and reproduces the
native-recorded goldens byte-exactly on all four movies, in both plain
mode AND whole-machine-rerecord mode (the waterbox host round-tripping the
entire guest around every frame). run-witness.sh now checks all THREE
flavors at Level A. The core-flavor taxonomy is thus demonstrated end to
end: three independent implementations (native C, pure C#, waterboxed C),
one frozen spec, bit-identical memory + video + audio. This also validates
the waterbox-only redesign's foundation - the miniBox host is real,
GCC-buildable (no clang/nightly-Rust), and reproduces emulation exactly.

Placement note: the synthetic cores are conformance-test fixtures for the
published contract, not product cores - they live under `tests/` and are built
by the test harness against `build/dll`, never as members of the solution. The
"zero core code in the repo" objective refers to the product: Chimera builds
and ships with no core in the solution; a test fixture proving the contract
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

## Architecture facts informing the plan

- `CoreInventory` already discovers cores via reflection (`[Core]` + `[CoreConstructor]`
  attributes) and its constructor already accepts arbitrary assembly type lists - the
  plugin inversion is small.
- Only `Chimera.Client.Common` references `Emulation.Cores` at the project level; the real
  coupling is ~96 frontend files using concrete core types, mostly deletable for Chimera.
- `GenericCoreConfig` (reflection-based settings UI) already exists; per-core config
  dialogs are not needed.
- Chimera targets `net48`: no assembly unload, hence the load-once model.
- Determinism is sacred: anything touching emulation, input, or timing must preserve
  frame-exact reproducibility or movies desync.

## The waterbox guest ABI, as built (2026-08-12)

The contract a core package implements. Everything below is the *whole* surface:
a package is `core.wbx` + `waterbox.config`, and the one generic adapter in
`Chimera.Emulation.Common` drives it.

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
A missing export is reported by the host as address 0, and the adapter simply
does not register the corresponding service, which greys the tool out. See
`WaterboxCore.Tooling.cs`.

**Settings.** `waterbox.config`'s `settings` block holds package defaults; the
adapter merges the user's sync settings over them and mounts the result as JSON
for the guest to read during `Init`. They shape the machine, so changing one
reboots the core. Note the known wrinkle: sync settings are keyed by adapter
*type*, so every waterbox core currently shares one config key.

## Core packages arrive by themselves (2026-08-13)

Objective 1 said packages are "discovered from a `Cores/` directory"; until now
they were not - a package had to be named on the commandline (`--core`) or picked
from File > Open Core on every launch. `CorePackageDiscovery` closes that: at
startup, `Cores/` beside the executable (plus anything in
`Config.CorePackagePaths`) is scanned, and every readable package found is
loaded.

**Discovery reads, it does not load.** It opens a zip (or directory), reads
`waterbox.config` or `chimera-core.json` for a name, systems and extensions,
and hashes the file - nothing more. That matters because loading is irreversible
in-process: it pins native modules and, for adapter packages, an assembly. The
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

**Ordering trap, found by testing.** The scan must run in the MainForm
*constructor*, not `MainForm_Load`: the commandline rom load happens in the
constructor, so a scan in Load is minutes too late in program terms and the user
gets a platform-picker for a rom the frontend could have opened. The witness has
a `D:box:autodiscovery` case (a rom load with no `--core`) so this cannot regress
silently.

## Testing the interface (2026-08-13)

The frontend's windows are now split so that almost everything about them can be
checked without a person:

- **Logic** lives in `Chimera.Client.Common` and is tested in
  `Chimera.Tests.Client.Common` - no display, no emulator, no core. The states a
  window displays belong here, not in the window.
- **Wiring** is tested in `Chimera.Tests.Client.GUI`, which constructs real
  forms and drives them (tick a row, press a button) under Xvfb. Forms must be
  shown for handles to exist, or selection and click handling silently do
  nothing.
- **Appearance** cannot be asserted, so it is rendered instead: `UiScreenshots`
  writes PNGs when `CHIMERA_UI_SHOTS` is set, CI uploads them every run, and a
  person looks.

`tests/ui/run-ui-tests.sh` runs all of it (`--shots` for the pictures). New tool
windows should follow the same split: if a question about a window can be
answered without looking at it, the answer belongs in a class that a test can
call.

## Core settings are declared, not coded (2026-08-13)

`waterbox.config`'s `settings` was a bag of defaults with no types and no
documentation; the settings dialog rendered it as an uneditable dictionary. It is
now a list of DECLARATIONS - name, display name, description, type, default,
options or range, and whether the setting is sync.

**The declaration is the UI.** There is no per-core settings dialog to write, and
never can be: one adapter serves every package. So `WaterboxSettingsBase`
implements `ICustomTypeDescriptor` and synthesizes a `PropertyDescriptor` per
declaration - which is exactly what WinForms' PropertyGrid asks for names, types,
descriptions, defaults and dropdown values. Every word in that dialog came from
the core. Adding a setting to a core is a `waterbox.config` edit and nothing else.

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
config has never seen (`InputManager.SyncControls`), and the controller config's
Defaults button falls back to them. A user's own bindings always win, and the
first package to claim a controller name keeps it - so installing a second NES
core cannot rebind the pad someone is already playing with.

**A broken bindings file costs the bindings, not the core.** `PackageKeybinds.Read`
reports and ignores unreadable JSON: an optional convenience file must never stop
a core from loading.

Gated at both ends: the synthetic witness starts Chimera from a config that has
never heard of the synth controller and requires the package's bindings to be in
the config it writes out (`K:box:keybinds`), and each core repo's frontend gate
does the same for its own package.

## Sweeping out what the bundle left behind (2026-08-13)

With the cores outside the frontend, a lot of BizHawk's furniture had nothing
behind it any more. Removed, on the user's instruction to take out anything no
longer relevant:

- **Basic Bot**, entirely. It is a brute-force input searcher, and searching is
  jaffarPlus's job - that integration is the plan, not a second bot in here.
  Its three `IMainFormForTools` members (`LoadQuickSave`, `Throttle`,
  `Unthrottle`) went with it, each marked "only referenced from BasicBot", and
  `MainForm.Throttle`/`Unthrottle` had no other caller either.
- **Hotkeys for consoles this frontend cannot run**: the SNES, GB and NDS groups
  (layer toggles, screen rotation) and the RetroAchievements group. None had a
  handler - they were tabs in the Hotkeys dialog binding keys to nothing. The
  dialog's hack to hide the RA tab went too.
- **Config members nothing reads**: the ten RetroAchievements settings,
  `GbAsSgb`, `LibretroCore`, `SelectedProfile` (and the `ClientProfile` enum),
  `FirstBoot`, and `N64UseCircularAnalogConstraint` - a knob named for a console
  that cannot exist here, gating an axis constraint no package can declare.
  `TargetZoomFactors` lost its seed values for eight systems that are not here;
  it still remembers what the user sets.
- **The onboarding silhouette** in the status bar, which offered a setup wizard
  that no longer exists and answered a click with "All done!".
- **Dead UI**: an `A7800Hawk` menu item that was never even added to a menu, the
  `RomStatusPicker` dialog nothing opened, and five image files no code named.

Kept deliberately: the Code-Data Logger tool (it greys itself out for a core
without the service, and a future guest ABI group could back it), the "None"
placeholder in External Tools (an empty submenu never opens, so it cannot
repopulate itself - the same trap Preferred Cores fell into), and
`PreferredPlatformsForExtensions`, which the platform chooser still uses for
ambiguous extensions.

### Second pass: tools and per-console corners (2026-08-13)

- **Code-Data Logger**, entirely. It needs `ICodeDataLogger`, and the guest ABI has
  no group that could back it, so it greyed itself out for every core that can
  exist here - along with its drag-and-drop `.cdl` handling and its slot in the
  file-load ordering. If a "which addresses were code" group ever joins the ABI,
  the tool comes back with it.
- **Per-console corners in generic tools**: TAStudio's Doom column-hiding and its
  N64 C-button mnemonic prefixing; the hex editor's Arcade/N3DS exception around
  the rom domain, and its N64 matrix viewer (menu item, handler and the nested
  dialog class). Each was a branch on a system ID no package can claim.
- **Libretro, entirely** (user, same day). It started as the path plumbing -
  `RetroSaveRamAbsolutePath` / `RetroSystemAbsolutePath` and the two
  `ICoreFileProvider` members that handed a libretro core its own directories -
  and then took the rest: the `OpenAdvanced_Libretro` and
  `OpenAdvanced_LibretroNoGame` load types with their `IOpenAdvancedLibretro`
  interface, `RomLoader.LoadRom`'s `launchLibretroCore` parameter (whose one
  caller passed null), the file filter for picking a libretro `.so`/`.dll`, and
  the `Libretro` system ID. A libretro core is a foreign plugin ABI loaded from a
  shared library; Chimera's cores are waterbox packages with a sandbox and a
  reproduction contract, which is the opposite trade, so there was never going to
  be a road back.
- The **AVI writer** is now hidden on non-Windows rather than removed: it is
  Microsoft's AVIFIL32, so on Linux the only thing offering it achieved was a
  `DllNotFoundException` after the user picked a codec. It still works on Windows,
  where Chimera also runs.

Checked and kept: the seven other video writers (all registered, all working - the
per-codec dialogs are theirs, not leftovers), and the Cheats tool, which needs
only `IMemoryDomains` and therefore works with any core package.

### Third pass: MAME, and the core-motivated exceptions (2026-08-13)

The user's instruction was "remove all mame and all core-motivated
exceptions-to-the-rule", and the second half is the interesting one: a
core-agnostic frontend should have no code that exists because of one particular
core.

- **MAME**: the `OpenAdvanced_MAME` load type, the three `RomStatus` values only
  it produced (`Imperfect`, `Unimplemented`, `NotWorking`) and the status-bar
  cases and multi-disk exemption built around them, its icon, and the
  `IsDiscForXML` hack that made a `.chd` not-a-disc "due to MAME wanting CHDs as
  hard drives (bad design, I know!)" - the comment was BizHawk's own.
- **The controller artwork**: a table mapping 26 controller-definition names to
  pictures of consoles, with an "Uberhack" for the C64 keyboard and a special
  case for the ZX Spectrum. A package declares buttons, not artwork, so the
  frontend cannot know what any pad looks like; the picture column is collapsed
  and the images are gone.
- **Firmware, the whole subsystem**: the manager, the (already empty) database,
  the config dialog with its hardcoded table of console names, the missing-
  firmware retry path, the `Firmware` path entry, the movie header's firmware
  hash, and `ICoreFileProvider` itself - which after the libretro removal existed
  only to hand cores their BIOS files. Note this overrides an earlier note in
  `FirmwareDatabase.cs` saying the mechanism would stay for a future
  package-registers-firmware story: with no ABI channel for it, nothing could
  ever resolve, so the config dialog opened an empty window. When packages need
  BIOS files they will mount them like any other data, and the design will be
  made then rather than inherited.
- Smaller: `CGBNotSupportedException` (a Game Boy Color error in a frontend with
  no Game Boy core), and 37 image resources plus 35 image files left orphaned by
  this and the previous passes.

### The menus say what they mean (2026-08-13)

- **Emulation → System.** The menu holds Pause, Reboot Core, the dump status and
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
  reassign what you opened. `CoreChoices` is down to `MakeDefault`.
- **Tools lost Debugger, Trace Logger and Surface Viewer**: they are core-provided,
  and the Emulator menu is where the running core's tools belong. Listing them
  twice implied they were the frontend's.
- **Virtual Pad is gone entirely.** Its schemas come from core factory assemblies
  (BizHawk shipped one per console); the generic waterbox adapter has none, so the
  window could only ever be empty. With it went the eight "Analog" hotkeys that
  nudged values inside it, and `vpads_schemata` from Emulation.Common.

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
(`ICorePersistentData`, guest `GetPersistent*`/`PutPersistent`) plus `.gameBundle`
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
Xvfb box this was measured on, a TURBO seek is slower than a plain one - the
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

**The turbo-seek anomaly was Xvfb's.** Measured on the Windows box with the real
GPU, driving `Chimera.exe --headless` through interop with the same project and
Lua kicker: a plain Game Boy seek is 3.5 ms a frame and a turbo one 3.1, against
2.7 for the machine alone. The several milliseconds a present and a message pump
cost with no frame drawn were the software GL under Xvfb, and are not a Windows
problem. Two more phases in the loop trace - movie and sound - showed where the
rest of the client's time goes there: the movie's frame handling is the
greenzone capture itself (0.22 ms, the history's share of a Game Boy frame), and
the message pump at sixty services a second is the piano roll repainting, which
is what the person is watching.

**A DISPLAY that does not answer is asked first.** Both gate scripts start an
Xvfb only when DISPLAY is unset, and the dev box has DISPLAY set to an ssh
session's forwarded display with nothing behind it, so every frontend leg failed
with "Could not open display" - fourteen legs and a hundred and two window
tests, all saying the same thing. The scripts probe the display with xdpyinfo
now and bring up their own when it does not answer. The rule stands that a
caller's working display is used rather than a second one; what changed is that
"set" is not taken for "working".

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
worthless.** `chimera-run` compiles the engine's sources straight in
(`meson.build`: `engine_src + gl_bridge_src`); it does not load
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

## A window's title is asked for, not assigned (user-reported, 2026-09-11)

Reported from use: the Cache Manager's "Greenzone budgets..." button threw
`InvalidOperationException` the moment it was clicked, from `FormBase`'s own
`Text` setter, before the dialog had drawn anything.

`FormBase` refuses `Text = ...` on purpose. A title is not a string a window
owns, it is an answer the window has to be able to give again: the "static
window titles" setting (Config > Customization, for anyone recording their
screen) reaches in and asks every open window for its title a second time, and a
window that assigned its title once during construction has nothing left to ask.
So the title is a property - `WindowTitle` for the honest one, `WindowTitleStatic`
for the one safe to show a stranger - and `UpdateWindowTitle` is the only thing
that writes to `Text`. The setter throws rather than quietly ignoring the write,
because a window silently keeping the wrong title is the failure the whole
mechanism exists to prevent.

**This class of mistake is easy to make and invisible until somebody opens the
window.** It compiles: `Text` is an ordinary inherited property. It reads right:
the same constructor assigns `Text` to a dozen child controls a few lines below,
where it is perfectly correct, so the one bare `Text =` looks like all the
others. And nothing exercises it - a dialog reached from one button in one
window is not on any path a test or a session walks by accident. This one had
been in the tree since the budgets dialog was written.

The sweep found 21 `FormBase` subclasses and exactly one offender, so this was
not a pattern that had spread. The guard against the next one is
`FormTitleContractTests.EveryWindowGetsItsTitleFromWindowTitle`: it reflects over
the frontend assembly for every concrete `FormBase` subclass (base classes that
only exist to be derived from, such as `ToolFormBase`, are excluded by being the
base of another) and fails naming any that declares neither override. A window
that declares neither has nowhere to have put its title except `Text`, so the
check catches the bug without having to construct windows that need an emulator
and a live session. Its limit is worth knowing: a window that overrides
correctly and ALSO assigns `Text` somewhere later still gets through, so the
per-form tests that open a dialog and read its title back stay worth writing.

## A movie's log has a shape, and only the machine can describe it
(user-reported, 2026-09-11)

Reported from use (issue #54): open a project, seek so some frames are green,
select a range and press Delete. A "Failed to draw input roll" box appears, the
project closes, and the frontend then falls over painting the roll it no longer
has a movie for. Not core-specific: seen on DOSBox-X, reproduced on PCSX2.

What is thrown, from inside the paint, is
`ArgumentOutOfRangeException: Length cannot be less than zero` out of
`MovieController.SetFromMnemonic` - it looked for the comma that ends an axis
field, found none, and asked for a substring of negative length. The entry it
was reading had no axis fields at all. That entry was the one Delete had just
written: Clear is the only editing path that generates an entry from
`DefaultValueController` rather than from the frame's own state, and
`DefaultValueController` was built once, lazily, and kept for the movie's life.

The order that poisons it is the order a project opens in. The file says which
core to boot and with what, so the file is READ FIRST; until that boot the
frontend's emulator is the null one, whose controller has no controls and -
because `InputManager.SyncControls` builds a mnemonics cache for every emulator
it syncs, the null one included - is perfectly willing to generate entries. The
read touches `DefaultValueController` (finding where the run's input stops), and
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
definition and the log key they were built from and rebuild when either changes
(`MovieBase.DefaultValueController`, `TasMovie.DisplayValue`), and `TasMovie.Attach`
- which runs after the session has the machine's controller - works out where
the input stops a second time, because the pass done during the read could not
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
That is the thin-C# rule applied to something it would have been very tempting
to write in C#: every one of the channels above is a correctness question, and
correctness lives in the engine where the tests are.

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
controls the machine does not currently have (`MnemonicMap`,
`KeyNotFoundException` - a project recorded with a second controller and
reopened without it): `ControllerDefinition.MnemonicFor` answers from the cache
and falls back to the system's lookup for a name the machine lacks, and every
reader of the cache goes through it. A project's game record took the package's
FIRST system (`RomLoader`, `factory.SystemIds[0]`), so an ares NES project said
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

- every TAStudio edit, recording included, already did (`TasMovie.InvalidateAfter`);
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
runs otherwise is one rule, `CoreChoices.PickBuild`, used by every place that
turns a core name into a machine - project boot, the cache's machine identity,
a bare rom, a movie's forced core, the firmware in use: the pinned build if it
is there, else the one the user chose, else the most recently installed. The
user decided the middle term: opening a package with File > Open Core, or
installing a version in the core manager, is choosing it
(`Config.DefaultCoreBuilds`); a project loading the build it pins is not, and
records nothing. The "not the project's core build" prompt now only appears
when the pinned build is genuinely not installed.

Two smaller truths came with it. "Which package made this emulator" was
answered per adapter ASSEMBLY, and every miniBox package shares one, so it
named whichever package registered last; the registry now remembers the
factory behind each emulator, and the movie header and firmware record read
that. And an adapter package - a .NET assembly rather than a miniBox guest -
still cannot be loaded twice, because a second assembly of the same identity
would silently be the first one's types; those stay one build per name.

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
the spot and runs no handler, managed or native. A design that saves the work
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
disk. One trap met on the way: a static field of type `MainForm` on `Program`
made every start fail, because a field's type is loaded with its class, which
is before Chimera's assembly resolver is installed; the handlers hold framework
delegates instead.

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
its own root, `<data>/Recovery/<id>`, and its own kind (`CacheKind.Recovery`,
"Unsaved work"). The lock book already started every kind but greenzones locked;
unsaved work joins them for the opposite reason from the small caches - it is
the one entry whose loss is work rather than time. It is in use while any
session that owns it is running, and a folder where an earlier build left it is
moved on first use.

One slip worth a line: a hand-typed separator went into the source as a raw
control character, six times over. It compiled and ran; it is now an escape.

## A crash no handler sees still says what it was (user-decided, 2026-09-13)

The crash that started the recovery work was a graphics driver's fast fail:
`__fastfail` ends the process without running one exception handler, one
finally or one managed event. The work survives that (the journal), but nothing
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
   too (`TasMovie.SaveWithoutGreenzone`).
4. Close without saving, after asking. The recovery journal is ended without its
   clean-up, so the next open finds the work and offers it back.

A headless run has nobody to ask: it prints the reason, keeps the recovery
journal, and ends the ordinary way with exit code 65
(`HeadlessMode.EXIT_CODE_CORE_STOPPED`).

The stop reaches the frontend as a STATE (`ICoreStops.CoreStopped`, set by the
frame advance that did not run), and MainForm raises `CoreStoppedException` from
its own frame. Thrown from the emulator's frame advance, just back from the
engine, the same exception crashed Mono in its unwinder in most runs - and did
so with the previous host and a stop the engine only pretended, so it was never
the sandbox - while thrown one frame further up it never did.

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

## A crash only the CI runner has is tracked, not waited on (user-decided, 2026-09-15)

The two witness legs that make the synth core stop on purpose (`S:frontend:abort`,
`S:frontend:wild`) pass here every time and crash Mono on the GitHub runner every
time. The runner's log shows the core stopping correctly; the crash is Mono's own,
while CoreStoppedException is thrown. Symbolized with Ubuntu's `mono-runtime-dbg`
(fetched with `apt-get download` and unpacked, no install needed), the frames are
`mono_amd64_throw_exception` -> `mono_handle_exception_internal` ->
`unwinder_unwind_frame`, at `mini-exceptions.c:662`: `*lmf = (*lmf)->previous_lmf`,
with the LMF - Mono's chain of managed-to-native transitions - pointing at
0xdab80. The chain is already corrupt when the exception walks it.

What it is not: the compiler (natives and guest built with the runner's gcc 13.3
here - the guest came out byte-identical), the CPU count (pinned to four cores),
Mono's version or the OS (identical), or a one-off (a rerun failed the same way).
160 local runs, none crashed. The same unwinder crash was met while the feature
was being written, with a faked stop and the previous miniBox host, which is
why the throw moved to MainForm's own frame; on the runner that is not enough.

Releases were not held for it. On the runner, and only there (GITHUB_ACTIONS),
those two legs report KNOWN when Mono crashes, which does not fail the run;
anything else wrong with them still fails, and everywhere else a native crash
still fails. On Windows the frontend runs on .NET Framework and is not affected.
A Linux user whose core dies can still see the crash this feature was meant to
replace. Finding the cause is owed.

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
