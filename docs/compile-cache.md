# The compile cache

A core that recompiles its machine's code, the way RPCS3 turns PowerPC into
x86 through LLVM, pays for it at every boot: minutes for a big game, gone with
the session, because a sandboxed core's files live in memory the session owns.

The cache keeps the compiled objects on the host between sessions. It is the
one kind of file a core keeps that is not machine state, and that decides
everything about it.

## Why it can be implicit

An object is a pure function of three things: the module's bytes, the core
package that compiled it (our patches shape the generated code) and the target
CPU the core pins. A boot that loads a cached object runs the same machine as a
boot that compiles it fresh: the code's meaning is the interpreter's, and the
charges that keep the machine's clock are compiled into it either way. Only
host time changes.

So nothing about determinism, movies or projects depends on whether the cache
was warm, and no user decision is involved. That is the opposite of save data,
which is explicit and bundle-driven on purpose (docs/save-data.md): there the
file changes what the machine does. A project never records cache files; when
they are missing they are made again from the same inputs.

## The channel

miniBox's `source/cache/cache-bridge.h` is the contract. A core that keeps
such files exports `SetCacheBridge(uint64_t)` and receives the host's
dispatcher before Init, through the sandbox's single callback. Two operations
cross it, each as a pointer to an argument block in guest memory:

- `CACHE_OP_FETCH` with a name and a buffer: the host copies the file in, or
  answers 0 when it has none. A core asks with no buffer first to learn the
  size.
- `CACHE_OP_STORE` with a name and bytes: the host keeps them, written beside
  and renamed, so a reader never sees half a file.

Names are relative paths, with `/` between folders, under a directory the
host owns for this core and package. A core may build a name from what a game
image says, so the host refuses every name that could reach outside that
directory or something that is not a file: an absolute prefix, a segment of
dots (`..` climbs out), a backslash or a colon (on Windows a backslash also
separates folders, so `..\x` would climb out), a segment that ends in a dot or
a space, a control character, and the Windows device names (`CON`, `NUL`,
`COM1` and the like). A refused store answers 0, as a full disk does. RPCS3's own object
names carry the module hash, its version and the CPU, so the same firmware
library compiles once for every game.

The engine sets the directory with `ce_cache_dir` before opening a session,
the frontend names it `<data home>/Cache/PrecompiledCode/<game sha1>/`
(see `CacheStore`; the data home is `%LOCALAPPDATA%\Chimera` or
`$XDG_DATA_HOME/chimera`), and
`ce_session_cache_stored` / `ce_session_cache_fetched` say what a session did.
The command-line runner takes the directory as `chimera-run --cache-dir
<folder>`. Each call to the cache counts as the core's work for the time
limit on a frame (a core compiling is not stuck; see "Debugging inside the
sandbox" in porting-a-core.md).
In a precompile session every object that crosses is announced on stdout as
`[cache] stored <name> <sha1>` or `[cache] fetched <name> <sha1>`, which is
how a frontend watching a child process learns what a game needs. An ordinary
session says nothing and hashes nothing (the hash was seconds of a boot on a
CPU without the SHA instructions); `CHIMERA_CACHE_ANNOUNCE=1` asks for the
lines in any session.

One directory per package version, because a different build of the package
generates different code and must not read the old one's objects.

It is not a configurable path and it is emphatically not inside the install.
A Chimera bundle is a zip somebody unpacks, and updating it means unpacking a
newer one: a gigabyte of a PS3 game's compiled code sitting in there would be
lost on every update, and would make the size of the install depend on what
had been played. It used to live in `<exe>/CoreCache`; an older install's is
moved out on the first run that finds it.

## Precompile sessions

Waiting for a first boot to compile is the slow way, and inside the machine's
scheduler only one thread runs at a time. A core that can fill its cache
without running exports `SetPrecompile(index, count, firmware_too)`: the
session boots, never runs, compiles every module part whose name hashes to its
index, and reports when it is done (`IsPrecompileDone`, `GetPrecompileDone`,
`GetPrecompileTotal`). The parallel form is not threads in one sandbox, where
guest threads are green threads on one host thread, but several sandboxes
side by side, each a child process of the frontend compiling its share into
the shared directory.

The progress is work to do, counted in any unit the core chooses, and the
window's bar and its "Compiling D of T modules" line are made from it. `done`
is what this session has finished; `total` is the whole game's, the same in
every session, so the shares the sessions finish add up to it. The total may
grow while a session is still finding what the game has, and never goes
down. Count something small enough that the bar moves while one big module
compiles: RPCS3 counts each file of its sweep and each part of a module it
compiles on its own (a game's main executable is hundreds of parts and
minutes of work), not the modules (issue 618: it counted the executable once
as total and once as done in each of four sessions, and a bar of modules sat at
nothing for the minutes the executable took). The window does not trust it
blindly: it adds up what the sessions say is done, takes the biggest total,
never shows a total lower than one it showed, and never shows more done than
total.

## Where it happens: a step in the wizard

For a core whose package says `"precompile": true`, creating a project has one
more step, after the firmware and before Create. It says what it is - this
core translates the console's code into the machine's, and doing it once now
is what makes the game fast enough to work on - and it lists the modules:
name, SHA1, and whether the object is on disk and still what it was. What is
already compiled is green when the step opens.

**Compile** runs the sessions: `Chimera --precompile INDEX/COUNT <package>
<files> <cache folder> [<firmware id>=<path>]...`, this same program as child
processes with no window, each compiling the modules whose names hash to its
index. A game runs only in a project (user-decided, 2026-10-26), so each
session makes one of `<files>` - the files picked so far, their slots and the
settings chosen, as the window writes them down - and opens its machine; the
firmware is given outright, as a session reads no settings file. Rows appear
as objects land, green, so the wait shows its work. The button is offered
exactly while there is compiling left to do. From a shell, `chimera-run
--project <p> <package> --precompile INDEX/COUNT --cache-dir <folder>` runs
the same session on a project.

How many run at once is bounded by memory as well as by processors, and the
engine decides it (`ce_compile_sessions`, `source/engine/source/compile_sessions.h`):
one session per 8 GB of what the machine can spare after 4 GB kept back for
the frontend and the system, never more than half the processors, never more
than eight, never fewer than one. The 8 GB is a setting (Tools >
Pre-Compiled Modules, "Memory per compile session"; user-decided
2026-10-14): a person whose games compile in less can run more sessions at
once, and one short of memory can run fewer.

A session holds about 7 GB (measured on Windows with MotorStorm, issue 259:
four sessions took 25.8 GB of the system's commit within 80 seconds of
starting, 27.9 GB at their peak). Only 1.6 to 1.75 GB of that is the
process's private memory; the rest is the sandbox's, in sections backed by
the page file. An earlier figure here - 4 GB a session - was made from the
private memory alone, which is what Task Manager's "commit size" and a job
object's limit show. Do not take the figure from maximum resident set size on
Linux either: the same session reports 83 GB there, because the sandbox's
arena is mapped and its touched pages are counted.

What a machine can spare is, on Windows, the smaller of its free memory and
of what the system can still promise with the page file at its present size
(`ce_compile_memory_to_spare`). Windows has to promise memory before a
program may use it, and when it is asked for more than it has it makes the
page file bigger. That file is on the system drive whatever folder Chimera
keeps its data in. Sized by free memory alone, a compile could take 18.8 GB
of free space from C: and then fail when the drive was full. Nothing else is
written outside the data folder: in the measured runs the system drive lost
0.17 GB, and the cache is 172 MB.

A session that dies prints nothing on its way out, so its exit code is the only
account of it there is, and the orchestrator reports it: the POSIX signals and
the Windows status values both, and an unrecognised code by number. That is worth
more than the sentence it replaces - "3 of 8 sessions failed without saying why"
was holding an 0xC0000005, which is what turned a supposed memory shortage into
the real fault, a JIT region in the rpcs3 core that wrapped onto its own code
(docs/design-principles.md, 2026-09-12). Where memory is what ran out, the run halves the sessions and goes again -
too much at once is a reason to want less, not a reason to stop. Memory ran
out when the system's code says so, when a session said the sandbox "failed
to create memory block", or when the machine had less than 4 GB to spare at
some moment while the sessions ran (the orchestrator looks every two
seconds; a session Windows could not give a page to dies of an access
violation like any crash). When one session alone still fails that way, the
wizard says so in words, and on Windows says where the disk space went. A
partial manifest is never written: a list of what a game needs with holes in it
is worse than no list.

On Linux, running out of memory is settled by the kernel, which stops the
process it judges biggest - and Chimera's own window was that process when a
compile of a 16 GB game filled a 31 GB computer (issue 300). So a session
asks to be stopped first (`ce_compile_session_begins`, oom_score_adj 1000):
the kernel then stops a session, the window sees it killed for want of
memory, and runs fewer. A session that meets a message box it cannot show
quits with code 64, and the orchestrator says so by name.

**Create stays unavailable until every listed module is green.** A project
whose game is not compiled is a project that boots into a minutes-long stall,
so it is not created at all. This is the only place the cache is filled from;
removing what it filled is `Tools > Pre-Compiled Modules...`, which lists the
games and takes them away one at a time.

What a game needs is remembered beside the objects, in the game's own
directory as `manifest.json`: the list of names and hashes the sessions
produced, and which core and package build produced them. The project records
the same list under `coreCache`, so opening it later can say whether the
compiled code is the same code. The files themselves are never in the project:
they regenerate from the game and the package.

## One directory per game (user-decided, 2026-09-17)

A game's compiled code is filed under the game's own SHA1 and nothing else.
Neither the core's name nor the package version is in the path.

This replaced one directory per core and package version. That layout kept a
rebuilt core from reading the objects an older build compiled, which is a real
hazard - different patches generate different code - but it paid for it with a
directory that never went away: five of them and 231 MB on the machine this was
decided on, one per local core build, with no way to tell which was still
wanted. The manifest now carries the core name and package version instead, and
a manifest that does not vouch for the build now running is not believed, so the
game compiles again. A rebuild is cheaper than an directory that accumulates
forever.

Not believing the manifest is not enough: another build's files keep their
names, so a compile would take them as already done and a session would hand
them to the core. A game's folder therefore holds one build's code
(`source/ui/viewmodel/src/compiled_code.cpp`). Before a session gives the folder
to the core, and before New Project compiles into it, code that another build
compiled is removed, and so are files that no manifest names a build for. A
folder a session starts is given a manifest naming the build, with no modules
(a compile lists them). New Project counts a game as compiled only when its
manifest names the build now running. The session says when it removed
another build's code.

Objects are **not** shared between games: each game keeps its own copy of
everything it needs, firmware libraries included. Measured on the deciding
machine, the shared firmware set was 12 MB against a game's own 47 MB - so the
duplication buys a directory that can be deleted whole, with nothing else
depending on what is inside it, for about a quarter again of its size.

What this guarantees is boot time, not correctness: a module a game generates
at runtime cannot be precompiled, and the core compiles it when it meets it,
as before.

## The proof

`WizardPrecompileTests` holds the step's rules: a core that compiles nothing
is never asked, a game not compiled yet cannot be created, a compiled game
lists its modules in green and can, and a module that changed underneath is
not accepted.

The rpcs3 gate holds that the native and the sandboxed compiler produce
byte-identical objects (`cache:objects`), that a warm run fetches every object
and compiles none (`cache:warm`), and that two precompile sessions store
exactly the boot set between them (`cache:precompile`); the frontend gate does
the same through the frontend's own sessions (`precompile:frontend`). One trap
worth recording: LLVM's X86 backend emits `endbr64` into JIT code whenever the
compiler binary itself was built with CET, which Ubuntu's GCC does by default,
so the native LLVM is built without it or the two flavors' objects differ.
