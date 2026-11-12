# An MCP server for Chimera (design)

Status: the design was decided by the owner on 2026-10-12 (the decisions are
listed at the end). The work is in progress; this section says what is built.

Built so far:

- One engine call opens a project (step 1 of the order of work).
- The engine's part of the server: the local pipe, the protocol, the log, and
  `chimera-mcp`, the program an agent starts, on Windows, Linux and macOS.
- In the window: Tools > MCP Server... with start, stop, the state, the tick
  box, the set-up command and the log; the title bar says "MCP server
  started" while it is.
- The tools that look: `status`, `controls`, `get_input`, `screenshot`,
  `list_memory`, `read_memory`, `registers`, `list_properties`,
  `get_property`, `list_branches`, `list_markers`.
- RAM search, the agent's own: `search_start`, `search_filter`,
  `search_results`, `search_undo`.
- `watch`: values named once and told with every answer that lands on a
  frame.
- The tools that change things, refused until the tick box is ticked: `seek`,
  `set_input`, `set_buttons`, `insert_frames`, `delete_frames`, `undo`,
  `advance_until`, `save_branch`, `load_branch`, `add_marker`,
  `write_memory`, `set_property`, `set_register`. A branch of the movie as it
  was is saved before the first of them, and again for each project opened
  while the server runs (2026-10-18: once per server start, a project opened
  later had nothing kept).
- Two command-line flags for runs nobody clicks in: `--mcp-server` and
  `--mcp-allow-changes`.
- For an agent's whole run with no clicking (issue 355): `pause`,
  `resume`, `save_project` and `close_tastudio` in the window,
  `server_info`, and `status` with the controls' names, the project's
  files, the firmware files in use and where each was found, and the speed
  measured. The tick box is remembered.
- `press_buttons`, a schedule of presses written into the input as one
  step, and `capture_series`, pictures on a schedule of the machine's own
  frames, each saying whether it is blank; `screenshot` says so too.
- Every Lua function, in the window: `lua_functions` lists them as the Lua
  Console documents them, `run_lua` runs Lua there and answers what it
  returned and printed, and `lua_script` starts or stops a script file.
- `create_project` in the window: the New Project window, never shown,
  given the agent's answers; missing firmware is listed instead of asked
  for. `list_cores` (with the kinds of file each core opens) and
  `describe_core` work in the window too.

- `chimera-mcp` is in every release bundle, where the dialog's set-up command
  points: `dll/` on Linux, `bin\` on Windows, `Contents/MacOS/bin` in the Mac
  app.

- The windowless mode: `chimera-mcp --cores <dir> --work <dir>` opens a
  machine of its own, with `list_cores`, `describe_core`, `open_game`,
  `open_project`, `save_project`, `export_movie` and `close`, and every tool
  above.

Everything in this document is built. What the windowless mode does not do
is said in its section below.

On Windows: the same test that edits a movie in the window was run against
the Windows build of Chimera, started with no window shown, and passed. The
dialog itself has not been looked at on Windows; only its Linux picture has.

## What it is

A way for an AI agent to work on the movie a person has open in Chimera: look
at the input, the picture and the memory, search RAM, and, if the person
allows it, edit the input. The person starts it from **Tools > MCP Server...**
in the running window and can stop it at any time.

MCP (Model Context Protocol) is an open standard for this. A program that
follows it (a "server") declares a list of tools, each with a name, a
description and typed parameters. An agent's own program (Claude Code, Claude
Desktop, Cursor and others) reads the list, and the model calls the tools and
gets structured answers back, pictures included.

There are two ways to use it, built in this order:

1. **In the window** (the main use). A person is working on a movie and asks
   their agent to analyse or change it. The agent works on the project that
   is open, and the person sees every step in TAStudio.
2. **Without a window.** An agent opens its own machine, with no display, to
   explore a game or check a movie. Same tools, plus opening and saving.

## How the agent reaches Chimera

An agent's program connects to an MCP server by starting it as a child
process and exchanging lines of JSON over that process's standard input and
output. A running Chimera window cannot be started by the agent, so there is a
small program in between:

```
agent's program  <-- standard input/output -->  chimera-mcp  <-- local pipe -->  Chimera window
```

- `chimera-mcp` is a small C++ program in the `bin` folder beside
  `chimera-run`. The agent's program starts it. It passes each request to the
  window and each answer back.
- The local pipe is a named pipe on Windows and a pair of FIFO files in the
  user's runtime folder on Linux, both open to the same user only. No socket
  call is used anywhere.
- When the server in the window is stopped, or no Chimera is running,
  `chimera-mcp` still lists the tools, and every call answers "Chimera's MCP
  server is not started: Tools > MCP Server... > Start". The agent's program
  therefore does not need restarting when Chimera is.

Nothing here opens a socket, listens on a port or resolves a name, so the rule
that Chimera has no network code (docs/core-manager.md, "Nothing reaches the
network") holds and `tools/check-no-network.sh` passes unchanged. MCP's other
way to connect, over HTTP, is not offered, for the same rule.

A second Chimera of the same user can serve beside the first under another
name: the environment variable `CHIMERA_MCP_NAME`, set to the same word for
that Chimera and for its `chimera-mcp`.

A user sets it up once in their agent. The dialog shows the line to copy, for
example for Claude Code:

```sh
claude mcp add chimera -- /path/to/chimera/bin/chimera-mcp
```

### After an update

Each nightly is a new folder, so the agent's set-up keeps naming the
`chimera-mcp` of the Chimera that was current when it was made. Its tools are
compiled into it: an old one does not offer the tools added since, whatever
window it reaches. An agent once spent a session working round tools it did
not know were there (issue 571). So the two programs say their builds to each
other when they connect (2026-11-12):

- `chimera-mcp` sends its build and the names of its tools in its greeting;
  the window answers with its own, and the path of its own `chimera-mcp`.
- When the builds differ, every answer the agent gets ends with a warning:
  both builds, the tools one side has and the other does not, and the
  `chimera-mcp` to point the set-up at. A `chimera-mcp` from before
  2026-11-12 says no build; the window then adds the warning itself, so even
  an old one is told.
- `server_info` reports `chimera_mcp_build` ("older" for one from before
  2026-11-12) and `chimera_mcp_up_to_date`.
- `chimera-mcp --version` prints its build without a window running.

After an update, run the set-up line again from Tools > MCP Server... (with
`claude mcp remove chimera` first for Claude Code) and restart the agent's
program.

## Tools > MCP Server...

One dialog, usable at any time while Chimera runs:

- **Start / Stop.** The server always starts stopped when Chimera starts; its
  state is never remembered. Stopping it ends any call in progress with an
  error to the agent.
- **State.** Stopped, waiting for an agent, or connected, with the name and
  version the agent's program gives.
- **Allow the agent to make changes.** A tick box, remembered and applied
  every time the server is started (user-decided, 2026-10-22, issue 355; it
  was off at every start, and an agent's run stopped at its first change
  until someone ticked it again). Unticked, the agent can only look. Ticked,
  it may edit input, move the playback position, write memory, and save and
  close the project.
- **The log.** One line per call: time, tool, arguments (shortened), result
  or error, how long it took. It can be saved to a file.
- **The set-up line** to paste into the agent, with a button to copy it.

While the server is started the main window's title says "MCP server
started" and the menu item's icon carries a green dot, so it cannot be left
on unnoticed.
Closing the dialog does not stop the server; closing Chimera does.

Chimera started with `--mcp-server` starts the server at launch, as pressing
Start does, and `--mcp-allow-changes` beside it ticks the box for that run
without remembering it. They exist for runs nobody clicks in (the tests use
them). Nothing else starts the server, and only the person's own tick is
remembered.

## What protects the person's work

- With the tick box off, every tool that changes anything is refused, and the
  refusal says which tick box allows it.
- Every edit the agent makes is an ordinary TAStudio edit: it appears in the
  piano roll and in the undo history, and can be undone.
- Before the agent's first change after the server is started, a branch of
  the movie as it was is saved, with a note saying so and the time. If the
  branch cannot be saved, nothing is changed: every change is refused until
  the server is stopped and started again. The engine holds this rule, not
  the window, so no path around the window's code skips it.
- With changes allowed, the agent can save the project to a file it names
  and close it (issue 355). Nothing asks: a file that is already there is
  not replaced unless the agent says `overwrite`, and changes that are not
  saved are lost only when it says `discard_unsaved`. No branch is saved
  before these two, since they change no input: one would itself have been
  an unsaved change.
- A memory write is not input: a movie cannot reproduce it. It is reported in
  the log and in `status` from the first write on, with the frame. It also
  lasts only until the machine goes back before it: a seek back, a branch or
  a rewind loads a state from before the write, and the movie replays
  without it. The answer of the call during which that happened says so
  ("the memory writes made on frames 1200-1260 were thrown away: the machine
  was put back on frame 1100"), once, so the agent does not take a write
  that is gone for one that did nothing (issue 571). A load onto the frame of
  a write undoes it too: that frame's state was kept when the machine
  arrived there, before the write. `freeze` holds a value through all of
  this instead.
- What the agent was told about one machine stays with that machine: when a
  project is closed and another opened (in the window or without one), the
  first write, the watches, the condition `advance_until` waits for and the
  search all start again. The sandbox puts a core's memory at the same
  address every time, so this goes by the session, not by the memory.
- An answer finished after its agent's program has gone is not sent to the
  next one to connect (every program numbers its calls from 1).

## The tools

Names and parameters are a sketch until built. "Changes" marks the tools the
tick box governs.

Looking:

| Tool | What it does |
| --- | --- |
| `status` | Core, game, frame, movie length, picture size, frame rate, whether changes are allowed, whether the core has stopped, and with `controls` the names of the controls plugged in (left out unless asked since issue 571: the call an agent makes most is shorter without them). In the window also: the project's file and whether it is saved, whether the machine is paused, the speed measured, the project's files with their paths, and each firmware file in use with its path and where it was found (the project's own, a file chosen for the core before, or the Firmware folder). |
| `controls` | The machine's buttons and axes by name, with each axis's rest value. |
| `get_input` | The movie's input over a range of frames. |
| `screenshot` | With `layers` `with_lua`, the picture as the window shows it: the game, the padding a script asked for and everything Lua draws, brought back to the game's own pixels, with the padding on each side said, so the agent can see its own overlay (issue 571; window only, the window grabs it). Otherwise the picture of the current frame as a PNG, optionally scaled down, with whether it is blank: its commonest colour and how much of it that colour covers (one colour over 99% or more is blank). `region` crops it first; `pixels` answers the colours of the pixels named, as #RRGGBB, and with `image` false no picture is sent (issue 571). |
| `list_memory` | The memory domains and address buses, with sizes. |
| `read_memory` | Bytes or typed values at an address. |
| `registers` | The CPU registers, where the core gives them; `names` reads only those, and a name the core has not is refused with the names it has. A core that names its registers' roles (`GetRegisterRoles`, issue 571) adds them: the program counter, the stack pointer and the flags, with the segment paired with each and the linear address on a segmented processor. |
| `trace_memory` | Who reads, writes or runs an address (or up to 64 from it): watches it, plays some frames of the movie's own input aside and puts the machine back, and lists each hit with its frame, value, size and - for a core that says (`GetMemoryCallbackPc`) - the instruction that made it, and every register if asked. A script watching the same place keeps its watch: the engine counts who watches what. Scripts hear none of the frames played aside (issue 571). |
| `trace_cpu` | The core's own trace of the instructions it runs over some frames played aside, filtered by text, for a core that keeps one (issue 571; it was "not planned" before). |
| `list_properties` / `get_property` | A game core's named values (docs/game-cores.md). |
| `list_branches` / `list_markers` | The project's branches and markers. |
| `server_info` | This build, the folder it runs from, and the folders for projects, cores, firmware and Lua scripts. Without a window: the build and the folders it was started with. |
| `pause` | Pauses the machine and says the frame. |
| `lua_functions` | Chimera's Lua functions as the Lua Console documents them: name, parameters, what each returns and does. By library, or by words. Window only. |
| `list_cores` / `describe_core` | The core packages in the cores folder, with the machines each is and the kinds of file it opens; and what one declares: settings, firmware, file slots. |
| `watch` | Names up to 32 memory values or properties; `status`, `seek`, `advance_until` and `load_branch` then carry their values every time. An empty list ends it. |
| `list_freezes` | The values held now (Chimera's freezes, the person's own from the Cheats window among them): each one's index, place, value, whether it is on, and its condition. |

Every address a tool takes can be a number or hex text (`"0x2E329"`), and
the answers that give an address give it in hex too (`address_hex`): a slip
between decimal and hex sent a person to the wrong place once (issue 571).

RAM search (the agent's own search, separate from the RAM Search window):

| Tool | What it does |
| --- | --- |
| `search_start` | Starts a search over a domain with a value size of 1, 2, 4 or 8 bytes, aligned or at every address (`misaligned`), over the whole domain or parts of it (`ranges`, widened to whole 8-byte blocks). Every address is a candidate; its value is remembered, and every frame the machine runs counts its changes. |
| `search_filter` | Keeps the candidates that compare as asked: the value now, the previous value, the address, the number of changes, or the difference, with a value or with each one's previous value; equal, different, greater, less, or different by so much. Values unsigned, signed or float. `previous` sets what a previous value is from then on: the last search's (the usual), the original, the last frame's, or the one before the last change. |
| `search_results` | The candidates left, lowest address first, with the value now, the previous one, the difference and the count of changes, up to a limit; where they lie (`clusters`), and the distance that most often separates one from the next (`stride`), a table's record size when they are a field of each record. |
| `search_undo` | Takes the last filter back. |
| `search_steps` | Several rounds in one call: each step goes to its frame, as `seek` does, and filters there. Changes, since it moves the playback position. |

The search is the engine's own, the one the RAM Search window uses (issue
571 asked for all of it): before each filter it catches up with what was
written or loaded since the last frame, which counts as one change, and a
seek forward over kept states loads one rather than running the frames, so
the frames it skips are not counted one by one.

The way it is used: start a search, have the game change the thing looked for
(`seek` to another frame, or the person plays), filter for "different" or
"greater", and repeat until few addresses are left. It only reads, so it
needs no permission. One search at a time; it ends when the project is
closed or the server is stopped.

Changes:

| Tool | What it does |
| --- | --- |
| `seek` | Moves the playback position to a frame, back or forward, and answers when the machine is there. With `extend`, a frame past the movie's end is reached by adding empty frames first, one step in the undo history (issue 571). |
| `try_input` | Plays some frames from where the machine is with input given in the call (input lines, or buttons and axes held), never written into the movie, reads the values asked for (after every frame if asked) and the picture, then puts the machine back exactly as it was: the movie, its length, its saved states and the playback position are not touched (issue 571). The engine runs the frames as a script's show_future does. |
| `snapshot` / `restore` | Keeps the machine as it is, memory writes and all, by an id, and puts it back on that frame later, without changing the movie: the Lua Console's memorysavestate, for an agent (issue 571). |
| `advance_until` | Plays the movie forward until a memory value or a property meets a condition (equal, greater, changed, ...) and stops on that frame. With `length`, "changed" watches that many bytes and stops when any of them changes: the frame something starts to move, with no byte known yet (issue 571). It stops at the movie's end or after `max_frames` otherwise, and says which. |
| `diff_memory` | Goes to frame_a, then to frame_b, as `seek` does, and lists each run of bytes of a range that changed between them, what it was and what it became; up to 16 MiB, the first 200 runs. It leaves the machine on frame_b. |
| `set_input` | Replaces the input from a frame on, one line a frame as `get_input` returns them, in printable ASCII. Past the movie's end it adds frames, starting at most 100000 frames past it. |
| `set_buttons` | Presses or releases buttons, and sets axes, by name over a range of frames. Controls not named keep what they had. Like `set_input` it starts at most 100000 frames past the movie's end. A movie made with an older package may not have a column for a control (its LogKey does not name it): without a window the control is added to the movie, a column on every line; with the window the call is refused and says which control. |
| `press_buttons` | Presses buttons on a schedule: each press its buttons, its frame (or so many frames from the frame the machine is on) and how long it is held. All of it is written into the input as one step; buttons not named keep what they had. |
| `capture_series` | Plays the movie and takes a picture on each of `count` frames, `every` frames (or seconds of the machine's own time) apart. Past the movie's end empty frames are added first, one step. Each picture says whether it is blank; with `folder` each is also written there as a PNG at its own size, and a file already there stops the series before it starts. |
| `insert_frames` / `delete_frames` | Changes the movie's length at a frame. |
| `undo` | Undoes the last step of the undo history, the same as Edit > Undo. |
| `save_branch` / `load_branch` | Keeps or restores a branch. |
| `add_marker` | Places a marker with a text. |
| `write_memory` / `set_property` / `set_register` | Writes to the machine. Reported as said above. |
| `freeze` / `unfreeze` | Holds a memory value (with a condition if asked: only while the value now compares so) or a property, written before every frame the machine runs, a seek's replay included, so it survives going back; or lets go of one by its index, its place, or all. These are Chimera's freezes (Cheats): never saved, never in the movie. In the window the person sees them in the Cheats window: the engine's list is read back into the window's after every call that changed it, so the window keeps them when it puts its list back on the machine (a restart). |
| `resume` | Lets the machine run, as unpausing does; past the movie's end each frame played is added to it. Window only. |
| `save_project` | Saves the project where it was saved, or at a path (a relative one is in the Movies folder; without a window, the path must be inside the work folder). A file already there is replaced only with `overwrite`. |
| `create_project` | Makes a project and opens it, as the New Project window does: the agent gives the core, the machine, the game's files (by slot, or each put where its kind of file goes), settings and firmware. The core's suggested settings for the game are applied first, as on that window's settings page. Firmware is looked for where that window looks and in a folder the agent names; when some is missing nothing is made, and the answer lists each file missing, the files that would do, and where it looked. A project with unsaved changes is replaced only with `discard_unsaved`. Window only; without a window, `open_game`. |
| `run_lua` | Runs Lua in the Lua Console, where the person sees it, and answers what it returned (a table as a list or an object) and what it printed. Every Lua function is there. It runs as a script does, in a coroutine of its own; the functions that wait for a frame are refused, as on the console's input line. Code still running after `seconds` (10 when not given, at most 300) is stopped with an error, so a loop that never ends cannot hold the window (issue 571). Window only. |
| `lua_script` | Starts a script file in the Lua Console, as File > Open Script does, or stops it; answers whether it runs and what the console printed since the agent last asked, and for a script that failed to start or stopped on an error, the error with its line (issue 571). Window only. |
| `lua_console` | What the Lua Console printed, whoever wrote it, from a numbered line on (the last 2000 kept), and the number to ask from next: the agent can read the person's scripts too (issue 571). Window only. |
| `lua_scripts` | The scripts in the Lua Console's list, the person's and the agent's: each one's file, whether it runs, is paused or is stopped, and the error it last stopped on. Window only. |
| `close_tastudio` | Closes the project and its machine. Unsaved changes are lost only with `discard_unsaved`. Window only; without a window, `close`. |

Each call that edits the input is one step in TAStudio's undo history, named
"Agent: ...", so a person can undo an agent's edit as they undo their own.
Every step that changes the movie's length says the length before and after,
and that the project now has unsaved changes (issue 571).

`advance_until` plays the movie's own input and never past its end, so it
adds no frames. With `watch` it answers "on which frame does this value first
become that" in one call instead of one call a frame.

Without a window only:

| Tool | What it does |
| --- | --- |
| `open_game` | Starts a project from a core and game files, with an empty movie. |
| `open_project` | Opens a `.chimeraProject`; its files must be the ones it records. |
| `export_movie` | Writes the input alone as a movie file, inside the work folder. |
| `close` | Closes the machine. |

## Where the code lives

- **In the engine (C++):** the protocol, the pipe, the list of tools, the log,
  and every tool that only needs the machine: memory, registers, properties,
  pictures, RAM search, conditions. This part is the same with and without a
  window.
- **In the window:** the dialog, and the tools that must go through
  TAStudio so the person sees them and can undo them: input edits, seeking,
  branches, markers. They call the same editing functions the piano roll
  itself calls.
- **How a call runs in the window.** A thread in the engine reads the pipe.
  The window's main loop asks the engine once per pass whether a call is
  waiting; the engine answers the calls it can itself and hands the others to
  the interface. So every call runs on the thread that runs the machine,
  between two frames, never beside it. If the window is busy (a dialog is
  open), the call waits, and is answered "Chimera is busy" after a time limit.
- **Nothing system-specific.** No tool names a system, a control or a core.
  Names are whatever the opened core declares.

## One place that opens a project

The steps from a project to a running machine (check that every file is found
and matches its hash, check the core against the one the project names, check
the files against the core's slots, build the list of files the core sees,
start the machine with the project's settings) were written twice: in
`chimera-run.cpp` and in the window. The server would have been a third.

They are now one engine call, `ce_session_open_project` (engine.h;
docs/project.md, "One place starts the machine"). `chimera-run` and the
window use it, the two copies are gone, and the server will use it too.

The same will be done for finding firmware in the user's Firmware folder when
the windowless mode needs it; today only the interface knows how.

## Rules of the design

- **Few round trips.** Every call costs the agent time and money. That is
  why ranges are taken in one call, why `advance_until` and `watch` exist,
  and why a picture is sent only when asked for.
- **Small answers.** Memory reads and search results have a size limit and
  say when they were cut. Pictures can be scaled down.
- **Editing the past behaves as it does by hand.** Changing input before the
  current frame drops the saved states after it, as any TAStudio edit does.
- **Errors are the engine's own words.** A core that stopped, a refused
  write, a frame that does not exist are tool errors with the engine's text.

## Without a window

An agent can also work with no Chimera running at all. Started with a cores
folder, `chimera-mcp` reaches no window: it opens a machine of its own, in its
own process.

```sh
claude mcp add chimera-headless -- /path/to/chimera/dll/chimera-mcp \
    --cores /path/to/chimera/Cores --work /path/to/a/work/folder \
    --files /path/to/my/games
```

The agent then calls `list_cores` and `describe_core` to see what there is,
`open_game` (a core, the game's files by slot, settings, firmware) or
`open_project`, and after that every tool of the window: it writes input,
plays, looks, searches, undoes, branches. `save_project` and `export_movie`
keep the result, and a project saved this way opens in the window like any
other.

What is different from the window:

- **Nothing asks for permission.** The machine is the agent's own, so the
  tools that change things are not refused and no branch is saved first.
- **The movie is held by the engine**, with its own undo history (the last
  100 edits). There is no TAStudio, so nothing of TAStudio's is there: no
  markers of the movie's own, no recording mode, no lag log.
- **A branch keeps the input and the frame, not the machine's state.**
  Loading one plays its input to its frame.
- **A project is made with `open_game`, not `create_project`**: it takes
  the files by slot and the firmware by path, and asks the core for no
  suggestions.
- **Firmware is given by path**, a file for each firmware id
  (`describe_core` lists them), in `open_game` and `open_project`. It does
  not look in Chimera's Firmware folder or its settings; only the window
  knows those. The project `open_game` starts pins the firmware it was given
  (its SHA-1), as the window's projects do. `open_project` given firmware
  other than the pinned one, or none, opens the project anyway and says so
  in `warnings` (2026-10-18): other firmware is another machine, and the
  movie may desync.
- **One machine at a time.** Opening another closes the one before, without
  saving it. An open that fails gives the one before back, its unsaved input
  as it was and its machine started again from frame 0 (2026-10-18: it was
  lost). `export_movie` refuses a link.
- **What the core prints goes to standard error.** The core runs in
  `chimera-mcp`'s own process, and its standard output would be the agent's
  protocol stream: a line of it there broke the agent's reading of every
  answer after it (2026-10-18). `chimera-mcp` keeps standard output for the
  protocol and sends everything else to standard error.

### Which files it may touch

An agent chooses every path it passes, so the limits are set when
`chimera-mcp` is started and cannot be changed by a call:

- core packages are read only from the `--cores` folder;
- games, projects, movies and firmware are read only from the folders named
  with `--files <dir>` (repeatable) and from the work folder;
- everything is written inside the work folder, `--work <dir>`;
- any other path is refused, also one that climbs out with `..` or through a
  link;
- a project's files are checked against the hashes it records, and a
  mismatch is an error, since there is no person to ask.

It downloads nothing. Games and firmware are the user's own files.

## How it is tested

- The shared project-opening call: the existing tests must give the same
  results as before, since `chimera-run` and the window both open every
  project through it. That includes the test that runs the whole product
  with the synthetic core (`tests/synth/`) and the interface tests.
- The server in the window, in the interface tests: start it, connect as an
  agent's program would, read input and memory and compare with what the
  window itself shows; with the tick box off an edit is refused; with it on
  an edit appears in the piano roll, a branch was saved first, and undo
  removes it; stopping the server ends the connection.
- The windowless mode: play a known movie through the tools and compare the
  memory, byte for byte, with what `chimera-run` produces for it.
- Each check is broken on purpose once and seen to fail (docs/gates.md).
- Linux and the Windows build both.

## Order of work

1. One engine call that opens a project, used by `chimera-run` and the
   window.
2. The engine part of the server: protocol, pipe, log, and the tools that
   only look. `chimera-mcp` as the program in between. Tools > MCP Server...
   with start, stop, state and log.
3. The tools that change things, the tick box, the saved branch, undo.
4. RAM search, `watch`, `advance_until`.
5. The windowless mode and its file limits.
6. Release bundles, README, a note for the website.

The owner changed the order on 2026-10-12, once the window mode was complete
and tested on both systems: `chimera-mcp` ships in the release bundles now,
and the convenience tools and the windowless mode follow.

## Not planned

- Sound, video encoding. (CPU traces were here until 2026-11-12: the owner
  had `trace_cpu` added for issue 571.)
- A disassembler: no core here offers one (Lua's `emu.disassemble` says so),
  and a core would have to declare one first.
- More than one agent connected at once, or one agent on several Chimera
  windows.
- Any connection that is not local to the machine and the user.

## What was decided (owner, 2026-10-12)

1. A program an agent starts and talks to over standard input and output is
   inside the "no network code" rule.
2. `chimera-mcp` ships in the release bundles once it is tested, not before.
3. Opening a project is one piece of C++ in the engine that `chimera-run`,
   the window and the server all use, done now and not postponed. Avoiding
   repeated code comes first.
4. Memory writes are offered, and reported from the first one on.
5. Without a window, files are limited to the folders named at start.
6. Working inside the running window is the main use: Tools > MCP Server...,
   where the server can be started, watched, logged and stopped at any time.
7. The window comes first; the windowless mode follows.
8. In the window, changes are behind a tick box that is off at first.
9. The server always starts stopped; its state is not remembered.

## What was decided (owner, 2026-10-22, issue 355)

The six requests of issue 355 are all built, and every Lua function is
offered through the server too (in the window, where Lua runs; the
windowless server has no Lua). Decision 8 changed: the tick box is
remembered between starts. The server itself still starts stopped (9).
