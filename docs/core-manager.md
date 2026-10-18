# The core manager

How cores reach a Chimera install: somebody puts them there.

Chimera used to be one repository that pinned fifteen core submodules, built
them all, and shipped the result as one bundle. That had a real property -
*one chimera commit pins one exact bundle* - and it stopped scaling: the
bundle got large, the release took hours, and bumping any core rebuilt the
world. So each core repository builds, packages and publishes itself, and
Chimera ships bare.

For a while Chimera also went and got them: File > Core Manager held a list of
the official cores, asked each one's repository what it had published, and
downloaded what was chosen. **It no longer does (user-decided, 2026-10-07).**
Chimera downloads nothing and reaches for nothing over the network - not a
core, not a list of cores, not what a core has published. A user downloads a
core's package from its project, or builds it, and puts it in the cores folder.
The Core Manager shows what is in that folder.

## What a core release is

Each core repository publishes to its own GitHub releases, on the same two
kinds of build the frontend already uses (see `.github/workflows/release.yml`):

* **dev** - a rolling prerelease, replaced on every green push to main. One
  link that is always newest.
* **nightly** - an immutable dated prerelease, `nightly-YYYY-MM-DD`, published
  only when main moved. Never deleted. This is the archive a movie replays
  against in five years.

One asset per CPU the core is built for: `<coreid>-<version>-<arch>.chimeraCore`,
`<arch>` being `x64` or `arm64`, the names Chimera's own release bundles use. A
package is machine code for one CPU - miniBox runs `core.wbx` directly, and
refuses one built for another CPU - but not for one OS: the same package runs on Linux and Windows. The version is the
commit the build was made from, which is what the package already stamps into
`waterbox.config` and what a movie already cites (`CoreVersion`). Releases from
before packages carried their CPU name one asset `<coreid>-<version>.chimeraCore`,
which is x64.

Most users want the x64 package, and the notes `publish-core.sh` writes say so
above the table whenever a release also has an arm64 one: take the arm64 one
only if you know you are using an arm64 device, and put only one of them in the
folder. Chimera's own release notes say the same of its bundles.

Fifteen repositories publishing identically is fifteen copies of one job that
will drift, so the logic lives once, here, in two files:

* `tools/publish-core.sh` - reads the version **out of each package**, refuses a
  hand-built one or two of different versions, checks a `core.wbx` is built for
  the CPU it is given as, names the assets, and creates or moves the release.
* `.github/workflows/publish-core.yml` - a `workflow_call` wrapper that
  downloads the gated artifacts and runs that script.

A release page ends with **What changed**: every commit since the core's last
nightly, each linked, with the Chimera issues its subject names
(`chimera#352`) linked to Chimera's issues rather than the core's own (issue
357). `tools/release-changes.py` writes it from the core's history, which the
workflow checks out whole; CI runs its `--self-test`.

A core's own workflow adds one job:

```yaml
  publish:
    needs: [ core-gate, frontend-gate ]
    if: github.event_name != 'pull_request'
    permissions: { contents: write }
    uses: ToolAssisted-run/chimera/.github/workflows/publish-core.yml@main
    with:
      core-id: gpgx
      artifact: gpgx-${{ github.sha }}
      package: gpgx.chimeraCore
```

That publishes the one package, as x64. A core built on more than one CPU
gates on each (a matrix, one leg per CPU on a runner of that CPU), uploads each
leg's package as `<artifact>-<arch>`, and names them with
`arches: x64 arm64`; the release then carries both.

plus a daily `schedule:` trigger, which is what makes a nightly. A push to main
publishes `dev`; a scheduled run publishes `nightly-YYYY-MM-DD`, and only if
main moved since the last one. Nothing publishes from a pull request, and
nothing publishes that the gates did not pass, because `needs:` is what got it
there.

**The version is read out of the package, never passed in.** It is what the
build stamped into `waterbox.config`, and it is what a movie cites; anything
else is a way for a release and its package to disagree. A version carrying `+local` or `-dirty` is refused
outright - a hand-built package is nobody else's build, and publishing one would
put a version nothing can reproduce into somebody's movie header.

The `dev` tag is moved by **deleting and recreating the release through the
API**, never by pushing a tag. A workflow's token may not create or update
workflow files, and pushing a tag at a commit whose `.github/workflows` differ
from the default branch's counts as exactly that. The frontend's own pipeline
learned this the hard way; the same comment is in `publish-core.sh`.

### Which cores publish

All fifteen. Ten already had a `chimera.yml` and gained the `publish` job; five
(dosbox-x, eka2l1, rpcs3, xemu, ruffle) had no CI at all and got one.

What each can prove on a public runner is bounded by content, and the five split
three ways:

| | what CI proves |
|---|---|
| **dosbox-x** | the whole gate. DOSBox-X is its own content: the machine boots to a DOS prompt with no disk, and the gate builds the hard disk, floppies, iso and cue/bin it needs as it goes. Frontend gate too. |
| **ruffle** | the whole gate, against upstream Ruffle's own test suite (`extern/ruffle/tests/tests/swfs`), which is free to distribute. |
| **eka2l1** | upstream's 194-case suite, the ARM interpreter against a golden model and dynarmic, determinism, the clock, and native == sandbox. The legs wanting a phone ROM or a game report SKIP. |
| **rpcs3** | starts, deterministic, savestates, native == sandbox over the small PPC programs in `tests/`. Firmware and disc legs report SKIP. |
| **xemu** | that both flavors build and the guest is sandbox-clean. An Xbox has no HLE bios, so nothing here executes an instruction. |

That last one is a smaller claim than the others, and it is the claim that can
be made honestly. It is also most of what actually breaks: a qemu that no longer
builds against the guest toolchain, and a package the frontend cannot read.

Every one of the five also runs **Chimera's own contract tests against the
package it just built** (`InstalledCorePackagesTests`, with `CHIMERA_CORES_DIR`
pointing at it). For eka2l1, xemu and rpcs3 that IS the frontend half - so those
three are one job rather than two, because splitting them would build the
package twice on two runners for no more proof.

Those tests open a package through the engine, which is `libchimera` - so they
dlopen a native library and go red without one. A workflow that runs them must
build Chimera's natives first. (Found by moving `build/dll` aside and watching
them fail; it would otherwise have been three first-run failures.)

rpcs3 builds its native reference in CI too - LLVM and ffmpeg for the host on
top of `rpcs3_emu` twice, the most expensive build here - because without it
`native == sandbox` is a leg nobody runs. The firmware and disc legs still
report SKIP there.

What was verified locally rather than assumed: dosbox-x's core gate (18 legs)
and frontend gate (4) both run green with nothing provisioned, and eka2l1's gate
reports 7 pass, 0 fail, 1 skip - the upstream suite's 194 cases, the CPU
difftest's 5508 programs, determinism, the clock, and native == sandbox. ruffle,
xemu and rpcs3 are built from their own scripts but their first CI run is their
first run.

## What Chimera ships

**No cores, and no list of cores.** A bundle is the frontend, its natives and
an empty `Cores/` folder. There is no roster beside the executable: a Chimera
that is not going to fetch anything has no use for a list of what could be
fetched, and a list that is not refreshed is wrong the day after it is made.
Which cores exist and where each is published is in the README, on the
project's site and on each core's own releases page - places a person reads
with a browser.

(`official-cores.json` is still in the repository. Chimera does not read it and
the bundle does not carry it; CI fetches the published packages by it, see
*Keeping the frontend honest*, and a test holds it to what the packages say.)

The manager is **File > Core Manager**. When there is no core at all - a fresh
install - Chimera says so once, in a sentence that names the cores folder, and
offers to open the manager. That is the one moment where it cannot do anything
useful without help, so it is the one moment worth interrupting; once a core
exists it never asks again. It is never said to a headless run, nor when a core
package, a rom or a project was named on the command line.

## Nothing reaches the network

Not the Core Manager, and not anything else. Chimera has no code that opens a
socket, makes a request or resolves a name:

* the Core Manager's feed, installer and roster are gone, with the release
  index reader and the cache of what cores had published;
* the Lua `comm` library keeps memory-mapped files, which are local, and has
  lost `comm.socketServer*`, `comm.http*` and `comm.ws_*`, with the
  `--socket-ip`, `--socket-port`, `--socket-udp`, `--url-get` and `--url-post`
  flags that configured them;
* LuaSocket (`socket/core`, `mime/core`) is no longer built or shipped.

What is left that names an address is a link: the About box and the Help menu
hand a URL to the system's browser, and that is the browser's request, made by
the person who clicked. `tools/check-no-network.sh` holds the line - it fails
the build's gate if a network API is named anywhere in the frontend's or the
engine's sources.

The cores' own publish jobs still write a `releases.json` index
(`tools/write-core-index.sh`). Nothing in this Chimera reads it; builds from
before this change do, which is the reason it is still written.

## Where cores live

    <the cores folder>/        one folder; a package is a file in it

The cores folder is `Cores/` beside the executable. It comes with the bundle,
empty, and a package dropped into it is found. A user who wants it somewhere
else - one set of cores shared by any number of unpacked Chimeras, or a faster
disk - names another folder in File > Core Manager (**Change folder...**), kept
as `CoresFolder` in the config - written the moment it is chosen, so it is
remembered next time however the session ends: an absolute path, or one
relative to the executable. Empty means the default, and choosing the default keeps nothing, so
a bundle that is moved keeps finding the `Cores/` that moved with it.
(`CoresFolder` the class resolves it; a setting that is no path at all is the
default too.)

It is **one** folder, the named one *instead of* the default. The config's
`CorePackagePaths` can still list further directories to scan after it; that
only ever adds, has no window, and the manager removes nothing from them.

**The data directory's `Cores` is not searched.** That is where versions that
downloaded cores put them (`%LOCALAPPDATA%\Chimera\Cores` on Windows,
`~/.local/share/chimera/Cores` elsewhere, or under a moved data directory).
Somebody updating from one would otherwise open Chimera to find every core
apparently gone, so the manager says when packages are still there and how
many, and **Change folder...** opens on that folder, so making it the cores
folder is choosing it. Nothing is moved or copied for them.

### One file per version, and no version is ever replaced

A published package is named `<coreid>-<version>-<arch>.chimeraCore`. Two versions of
one core are two files sitting side by side, and nothing Chimera does removes
an older one: an old build is the only way to replay a movie recorded on it,
and Chimera cannot fetch it again. Versions go only when the user removes them.

Side by side in the folder, they also RUN side by side: any number of builds of
one core can be loaded in a session as long as their packages are different
bytes (issue #63). A project boots the exact build it pins whenever that build
is in the folder; otherwise - a bare rom, a movie without a pin, a pin nobody
has - the build that runs is the one last opened with File > Open Core, and
failing that the newest.

The file's name is only a NAME. A package's identity is the SHA1 of its bytes,
which is what discovery, the extract cache and the movie header all use; a
package renamed, or kept as the `.zip` older releases called it, is the same
package.

A package put in the folder while Chimera is running is found without a
restart: discovery is separate from loading, the manager's **Refresh List**
rescans, and so does opening the manager at all.

## What a package has to be

Chimera no longer verifies a download, because it makes none. The bytes arrive
however the user got them; what says they came from the right place is the
user's browser and the core's releases page (GitHub publishes a SHA-256 beside
each asset). What Chimera checks is what it always checked of a file handed to
it, when the folder is scanned:

1. discovery can read it as a core package at all - one that cannot is listed
   where packages are opened, with the reason, rather than silently missing;
2. its guest ABI is one this Chimera runs - refused there rather than at the
   moment somebody tries to emulate with it;
3. it stamps a version, which is what a movie cites.

A project names its core by version and SHA1. If no package of that core is in
the folder, opening the project says which core it runs on and which folder to
put it in.

## The window

**File > Core Manager.** Two lines at the top: the cores folder and how many
cores are in it, and that Chimera downloads nothing - get a package from its
project, or build it, and put it in this folder. Then one list of cores on the
left, one core's versions on the right.

The list is the packages in the folder, one row per core, the emulators and
then the game cores, each by name; **Show:** narrows it to one kind. There is
no row for a core that is not there.

| column | what it is |
|---|---|
| Core | its name |
| Type | an emulator or a game (docs/game-cores.md) |
| Systems | the systems it runs, by the names the package gives them |
| Version | the newest version here, by its date and commit, and how many more |
| Size | the newest version's file, measured on disk |

Along the bottom, about the folder first and then what is in it:

* **Open cores folder** - shows it in the system's file browser, creating it if
  a folder somebody named is not there yet.
* **Change folder...** - a folder picker, opening on the cores folder; what is
  in the chosen folder is listed at once and can be used straight away, and
  the choice is remembered. A core already loaded stays loaded until Chimera
  is restarted.
* **Refresh List** - rescans, for a package copied in while the window is open.
* **Remove** - deletes **every version** of each ticked core, behind a
  confirmation that says what it costs: a movie recorded on one of those exact
  builds needs it to replay, and Chimera cannot fetch it again. Every row has a
  tick box, and **Select all** above the list says how many are ticked.
* **Show Systems...** - every system these cores run, and which core runs each
  (#172).

The right-hand panel acts on the row *selected* rather than ticked: every
version of that core that is here, newest first, with the file's path, when it
was built and the package's licence terms; **Remove version** deletes just that
one.

Remove deletes files and only files, and only ones in the cores folder itself.
A package found in a further search directory is somebody's own arrangement,
and an unpacked package - a folder, which is how somebody keeps their own build
- is never emptied by this window; both are named in what the window says it
left alone.

The columns have to add up to less than the list is wide or the last one is
reachable only by scrolling sideways.

### How a version reads

Per version, **the date and the short commit**, because those are the two
things somebody comparing two builds actually needs. A core's version IS the
commit it was built from, so eight characters of it is the same identifier the
rest of the frontend shows.

A package built by hand rather than published carries `+local` (and `-dirty`
where the tree was not clean) in its stamped version. That is worth knowing -
a local build is nobody else's build, so a movie made on it is replayable only
by whoever made it - and not worth spelling out in full every time the package
is named. So it reads as one trailing word: a published core is `4ed35321`, a
hand-built one is `12d65377 local`. This applies wherever a core is named,
including the project wizard's core picker.

**A version is listed with its date wherever its package states one** (issue
#67, user-decided 2026-09-17). Two commits say which versions they are and
nothing about which is newer. So a version reads `2026-09-17 10:30 CEST  (4ed35321)`,
the versions of one core are offered newest first, and the newest is the one a
picker opens on. The minute was added on 2026-09-29: a core often has several
versions in one day.

**Every date is on one clock** (issue 331, user-decided 2026-10-20): Central
European time as Zurich keeps it, CET (UTC+1) in winter and CEST (UTC+2) in
summer, written with the zone. That holds for a core's version and for
Chimera's own build line ("Commit 9dbd35bac (2026-10-20 10:58 CEST)"). Release
titles say it the same way: `tools/publish-core.sh` names a core's nightly
`Nightly 2026-10-19 (8c11acc3, made 2026-10-18 18:35 CEST)`, with the date
the build ran first and the commit's time after it, and its notes give the
line Chimera lists. The rule lives in `source/ui/viewmodel/src/european_clock.cpp`
and, for the release scripts, `tools/european-time.py`. Both work it out
themselves rather than ask the computer's time zones, so every machine writes
the same text.

The date is the package's own: the build script stamps `versionDate` beside
`version` in the packaged `waterbox.config`. It is the COMMIT's date, in UTC,
and never the build's - a package is a pure function of its commit, and a build
time would make the same commit produce two different packages. A package from
before the stamp has no date, and nothing else is asked: there used to be a
lookup in what the manager had last heard from the cores' repositories, and
there is no such record now. It is listed by its commit alone, after the dated
ones, rather than with a guess - the file's own time is when it was copied
here, which is not the question.

## What replaces "one commit pins one bundle"

**Movies name their core exactly.** A movie header carries `CoreVersion` and
`CorePackageSHA1`, and a project pins the same. Each core's nightly releases
are dated and never deleted, so the package a movie names stays where a person
can download it. Finding it is theirs to do; Chimera says which core and which
folder.

## Guarding the guest ABI

While the frontend pinned the cores, a core could never meet a Chimera that did
not understand it. Decoupled, it can.

So a package declares the guest ABI it was built against, `abi` in
`waterbox.config`, and the frontend declares the range it accepts
(`GuestAbi.Current` and `GuestAbi.MinimumSupported`). A package outside that
range is listed and refused with a reason - *built for a newer Chimera* -
rather than crashing somewhere inside the sandbox. A package with no `abi` at
all is ABI 1: everything published before this field existed.

The core declares it by hand, next to `systemId` and `memoryLayoutMiB`, rather
than having the build stamp it. A stamp would have to read the number out of
whichever Chimera checkout happened to be packaging, which is a way to be
confidently wrong; and the number changes about as often as a core's memory
layout does - when it does change, that core needs real work anyway.

Bump the ABI when the guest contract changes in a way an existing `core.wbx`
cannot satisfy - a new required export, a changed signature, a changed meaning.
Adding an OPTIONAL export (the tooling groups work this way) is not a bump: a
core that lacks it is detected and does without.

## Keeping the frontend honest

Nothing in Chimera's own CI builds a real core any more, so a frontend change
could break the generic waterbox adapter for every core at once and nothing
here would notice - it would surface the first time somebody tried one.

The `Published cores` workflow (`.github/workflows/cores.yml`) closes that.
`tools/fetch-cores.sh` downloads what each core last published into
`build/Cores`, and `test-packages` runs against the real packages:

* every package can be read;
* every package's guest ABI is one this build runs;
* every package's machines, settings and controller agree with each other -
  the real test of whether Chimera still understands what the cores are
  saying;
* every package's default keybinds name only buttons its controller declares,
  and no two of a controller's buttons share a mnemonic, so a controller that
  grows a button is covered the day it does;
* every package stamps a version, without which it cannot be cited by a movie.

No rom, no firmware, no emulation, no core build: this is the CONTRACT, and it
takes seconds. **Whether the emulation is still right is each core
repository's own gate**, which checks out Chimera's main and replays real
movies against it - the same check from the other side, run by the repository
that has the content to run it.

It is not part of CI (owner, 2026-10-29): Chimera is independent of every
core, so a core that has not published, or has published a package this
check refuses, never keeps CI, and the releases that follow CI, from
passing. The workflow runs on every push to main, daily and by hand, and
what it finds is for the cores' repositories to fix.

A core that has published nothing is skipped rather than failing the job. Note
what that means when NOTHING is fetched, because it bit us: every test is
skipped and **the job goes green having checked nothing at all**. For its
first day this job reported success on eight skips, and the moment the first
real packages arrived it failed instantly - reading a package is the engine's
job now (`ce_package_open`) and the job built only the tests, so
all fifteen came back *got null pointer from dlopen*. The packages were fine;
the job had never had a working engine and had never needed one to pass.

Two things came out of that, and both are load-bearing:

* the job **builds libchimera** like every other job that touches a package;
* it **fails on an empty fetch**. Every core publishes, so nothing fetched
  means the fetch broke, not that the cores are young.

The general lesson is worth keeping: *a skip is not a pass.* A suite whose
fixtures are fetched at run time can report success for having no fixtures, and
that failure mode is invisible in a green tick - it looks exactly like working.

Both halves read the same directory, so `tools/fetch-cores.sh` is also the
quickest way to get a working set of cores into a fresh checkout by hand.
`CHIMERA_CORES_DIR` points the tests somewhere else where a job needs it.

## Licences

This used to be a build-time question. The bundle carried every core, and
`tools/bundle-licenses.py` computed one `LICENSES.md` from what they declared -
which is how the release notes came to say, correctly, that the whole
distribution was non-commercial.

A bare bundle is not. `LICENSES.md` now states the frontend's own terms and says
plainly that **adding a core changes them**: several cores (Genesis Plus GX,
Opera, Snes9x) forbid commercial use and that binds whatever they are installed
into, while others are GPL and require their corresponding source to stay
identifiable.

So the terms travel with the package. Every package carries
`licenses/licenses.json`, put there at package time; `CoreLicence` reads it and
the manager shows what a core in the folder demands - commercial use first when it
is forbidden, because that is the part that binds everything around it - rather
than leaving somebody to open the zip.

`bundle-licenses.py` still reads `Cores/`, because a bundle assembled WITH
packages (a developer's, a downstream packager's) must still state their terms.

## What the frontend no longer carries

`extern/cores/*` is gone: out of the index, out of `.gitmodules`, and off the
disk. The checkouts were **moved**, not deleted - each became a standalone
repository under `~/chimera-cores/<name>`, keeping every local commit, which
mattered because all fifteen were carrying unpushed work at the time.

Moving a submodule checkout out of its superproject is not a `mv`. Its `.git` is
a FILE pointing into `<super>/.git/modules/`, its config carries a `core.worktree`
pointing back, and every nested submodule has the same problem one level deeper -
so `tools/detach-core-checkout.sh` moves the git directory in beside the tree,
strips `core.worktree` as text (git chdirs to it before it will do anything, so
it cannot unset the line that is wrong), and repoints the nested `.git` files.
There were 243 of those across the sixteen; dolphin alone has 38, and they nest
their `modules/` directories, so a submodule at `a/b` inside one at `a` lives at
`.git/modules/a/modules/b`. Deriving each new path from the old one is what makes
that rule the repository's problem rather than the script's.

Chimera's own checkout went from about 26 GB to 4.9 GB. The moved repositories
still work from where they landed: a core's `build-package.sh` looks for
`../chimera` and then `$HOME/chimera`, so it still finds the frontend. Verified
by running dosbox-x's gate from its new home.

With `extern/cores` went:

* `build_core` and `--skip-cores` from `tools/build-bundle.sh`, and the core
  hashes from `BUILD.txt`;
* the core pin file, the core build cache and the PS3 core's LLVM cache from
  `release.yml` - which is most of what made a release take hours;
* the "archive every distinct core package" step. The
  [`cores`](https://github.com/ToolAssisted-run/chimera/releases/tag/cores)
  release stays, because movies recorded before the split cite packages in it,
  and it no longer grows: each core archives its own nightlies now;
* `submodules: recursive` from both workflows' checkouts. They check out
  `extern` explicitly, which is what the frontend is actually built from;
* and, once extern/ was flat, the `mesa-guest` submodule. Chimera never used
  it - only the cores that render through OSMesa did, reaching into this
  checkout for it. pcsx2 and flycast now fetch and build their own, pinned by
  SHA256 to the same mesa 24.0.9, so what they link is what they always linked.

A bare Linux bundle is 117 MB, nearly all of it ffmpeg and the native
libraries.
