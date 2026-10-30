# Chimera's window (issue 142)

Chimera's window: Qt 6.12, QML views over C++ view models. The code is in
`source/ui`; how it is built and run is in
[source/ui/README.md](../source/ui/README.md).

## What was decided

- Qt 6.12 (a long-term-support release), with the windows written in Qt
  Quick (QML) (owner, 2026-10-22).
- The logic behind each window lives in C++ "view models" with no Qt in
  them. A window only lays things out and binds to them (owner,
  2026-10-22).
- **The engine owns the movie** (owner, 2026-10-22). The window reads
  and edits a frame's input through the engine (`ce_session_movie_get`,
  `_set`, `_resize`, `_text`), which writes every line in the movie's own
  columns; the window never writes one. A frame's buttons go both ways as
  one byte a button, every one of them, and the window keeps them as a set
  of any width (vm::Buttons): a keyboard is a hundred keys and Mednafen's
  controllers are 594 buttons (issue 411: everything stopped at the 64th). Those calls may come from any
  thread while another runs the machine: the movie has a lock of its own.
  An edit is honoured before the machine next moves: the engine drops the
  states after the earliest frame edited and, when the machine is already
  past it, goes back before it and plays the movie up to where it was. As
  in Chimera's TAStudio, "where it was" is the frame the machine was on at
  the first of a run of edits, kept until the machine is moved some other
  way; going back adds no frames to the movie (one made shorter is followed
  to its end, and an undo that gives the frames back lets the machine go
  on). The window edits while the machine thread seeks, so a seek may find
  frames taken out under it: it then stops quietly, since the edit that
  took them sends the machine where it belongs next. The
  window plays and seeks with the engine's own calls
  (`ce_session_movie_advance`, `ce_session_seek`). The other ways - an
  export of the engine's line writer to the window, or a copy of the
  format in the window - were set aside. One more call
  follows from it: `ce_session_movie_at` says that a state the window
  loaded itself (a branch's) put the machine on a frame of the movie, so
  the engine counts from there instead of replaying to it. And the rule
  for which machine a project makes (core build, settings, firmware,
  files), which decides whether kept states may be loaded, is the engine's
  (`ce_project_machine_id`); frames are put in and
  taken out of the engine's movie by `ce_session_movie_insert` and
  `_remove`; and so did rerecord
  counting (`ce_session_rerecords`): an edit counts one when it drops a
  stored state, a stroke counts once, a branch loaded over the movie
  counts one, as Chimera's TAStudio counts them.
- **Recording mode follows Chimera's** (owner, 2026-10-24). A project
  opens read only: the machine plays its movie as it is, and the keys held
  reach the movie only past its end, and there only through autohold,
  autofire and the Auto-hold columns. In recording mode (TAStudio's
  "Recording mode" box, the "Toggle read-only" hotkey, Q, or a script's
  tastudio.setrecording) each frame the machine plays with the person's
  input becomes what is held, the analog controls left alone at rest and,
  when hidden columns are not edited, the hidden ones as they were. Each
  frame recorded is a "Record Frame" step of the undo history; the frames
  recorded while the machine runs are one step. An edit by hand, or an
  undo or redo, ends recording, as Chimera's TAStudio does; a script's
  edits (tastudio.applyinputchanges) leave it on.
- **Frozen addresses are for this session only** (owner, 2026-10-24:
  "Frozen values should be only used for investigation, not movie
  production. So they should only survive the session."). The engine
  keeps the list (`ce_session_freeze`, `_freeze_property`, `_unfreeze`,
  `_unfreeze_all`, `_freeze_enable`, `_frozen`) and writes each freeze
  before every frame it runs, its own replays included; nothing is saved
  in a project, a movie or a state. RAM Watch, RAM Search and the Hex
  Editor freeze and unfreeze as Chimera's do (Freeze Address, Unfreeze
  All, the Hex Editor's Space, Delete and Shift+Delete; frozen rows and
  values in Chimera's colours; a poke of a frozen address, or the Hex
  Editor's increment, freezes the new value). Toggle All Freezes switches
  them on or off, and the status bar shows Chimera's freeze icon while
  any is on. As in Chimera, they stay while the same game runs again (a
  restart, a project of it), go for another game and when the game is
  closed, and are switched off, not removed, when the machine stops.
  Chimera's question when a project starts with freezes on is here
  ("Continue playback with cheats enabled?": No switches them off, Cancel
  opens nothing). A freeze has no name: Chimera names one made in the
  Hex Editor where a game property starts after the property, which only
  its Cheats window shows, and there is no Cheats window here.
- **Lua scripts run on the machine's thread** (owner, 2026-10-24), before
  and after every frame, as Chimera's do. Before a frame the person plays,
  the OnFrameStart functions are called (what a script holds there is that
  frame's input); after it, the OnFrameEnd functions, and the scripts
  waiting for a frame go on. A seek's landing is a frame's end too. The
  window's view models are the window's: a script reaches them by waiting
  on the window (the bridge's toWindow), and the window reaches a script
  by waiting on the machine (toScripts). Neither waits for ever: while the
  machine's thread waits on the window it does the jobs other threads wait
  on it for, and while the window's thread waits on the machine it does
  the jobs the machine waits on it for; the machine's other jobs (a frame,
  a seek) wait until the script's turn is over. What only the machine
  answers is asked of it directly, without the window: memory read and
  written, its domains, a SHA-256, the frame number, and what a script
  holds for the next frame (joypad.set and its kin). Measured off-screen
  with the synthetic core running as fast as the window lets it: 172 to
  183 frames a second with a script doing nothing, reading 100 bytes a
  frame, or calling joypad.set 100 times a frame (as much apart as two
  runs of the same script), and 37 with one asking the window 100 times a
  frame (emu.getsystemid): a round trip to the window costs about 0.2 ms.
  Before, scripts ran on the window's thread after each report the window
  drew, so a script that sets the next frame's input missed frames
  whenever the machine ran faster than the window drew; a test now runs
  the machine unthrottled for 600 frames with a script that holds a
  button on every third frame and counts the frames it sees, and both
  come out exact.
- **The session journals its own movie** (owner, 2026-10-24). The engine
  writes every change to a movie log into a journal file the moment it is
  made (`ce_movie_log_journal_open`); the engine owns the movie inside the
  session, so the session has the same call for it: `ce_session_movie_journal_open` (with `_note`, `_close` and
  `_journaling`). From the first change not saved, the window keeps
  `work.journal` beside the snapshot in the project's recovery folder: the
  engine appends every edit, every frame recorded and every movie loaded
  over the old one (a branch) as it happens, and the window adds Chimera's
  records for the markers and branches (M, B and O) whenever they change. A
  crash's work is rebuilt on the newer of the snapshot and the saved
  project, with the movie, markers and branches the journal ends on, so it
  is never more than the last change old; it was up to five seconds old
  with the snapshot alone. The journal is written again from the start
  once it has grown past three times its last start (16 MB at least), as
  Chimera's is.
- **Qt for Windows is built from source** (owner, 2026-10-24) with
  Chimera's own mingw (posix threading), without the network module, as
  it is now; Qt's prebuilt Windows libraries were set aside.
- **The Windows folder is kept clean** (owner, 2026-10-24): it holds the
  program (chimera.exe) and the themes; every DLL, Qt's plugins and QML
  go in lib\, and the other programs (chimera-run, ffmpeg, the tests) in
  bin\. Windows looks for a program's DLLs only beside it, so chimera.exe
  is a small launcher (source/ui/qt/launcher.c): it adds lib\ to where
  Windows looks and to PATH (which the programs in bin\ inherit), then
  loads the window from lib\chimera-qt-app.dll. A qt.conf built into that
  DLL sends Qt to lib\ for its plugins and QML, and a program in bin\ finds
  miniBox's DLLs in ..\lib. tools/stage-qt-windows.py lays the folder out.
  chimera.exe carries the version information every program has, which
  Windows shows in the file's Properties: Chimera, the version and the
  commit it was built from (0.1 (c1abf86d)), and the licence.
- **Folders are named in lower case** (owner, 2026-10-24): every folder the
  window makes or suggests by default - cores, firmware, themes, movies
  (with backup and macros), tools, lua, reports, each system's own folder
  with its state and screenshots, and in the data folder projects,
  recovery and cache/precompiled (a project's branches in branches).
  Chimera's own window keeps its capitalised names.
- **Chimera's TAStudio prevails** (owner, 2026-10-25): where the Qt
  window looks or works differently from Chimera's TAStudio and main
  window, Chimera's way is taken. Done so far: the side as Chimera's
  (Playback, Greenzone and Branches above, Markers below, a bar between
  them; the side keeping its width; both bars remembered and put back by
  Restore Defaults), its boxes framed, Branches and Markers as Chimera's
  grids (column titles, grid lines, bold text, its colours), their icon
  buttons and tooltips (the hotkeys named), a branch dragged by its number
  into another's place, Edit Marker Frame, Turbo seek (off at first: a seek
  past the greenzone plays there at the machine's own speed, as Chimera's;
  the way back after an edit and the green arrow go the same way, so the
  window is answered on the way: issue 406),
  and the status line (what was said, a seek's progress, the frames picked,
  states held and frames copied). The main window's status bar is Chimera's:
  icons for the movie (played back or recorded), paused or playing to a
  frame, a video being made, each drive's light with the image in it (cut
  in the middle, as Chimera's), frozen addresses, the sound and the key
  priority (each a click to change), then the core's name; what the session
  says, errors too, goes over the picture as Chimera's messages.
  The piano roll is drawn as Chimera's: its font (Arial 8.25 bold) and its
  cell sizes, the titles on the roll's column colour (Frame#, and each
  button by the package's header or its letter: U, SEL), a pressed button's
  cell showing its title, the frame numbers zero-padded, grid lines between
  every row, the whole row of a frame picked tinted, the row a seek is
  going to in the current frame's colour, the arrows (the machine's frame,
  and the frame edits go back to), a marker's or a held state's corner,
  branch numbers before the frame numbers, the title under the pointer lit
  and an empty cell under it naming its column.
  The menus are Chimera's: Edit has its Selection Undo and Selection Redo
  (never enabled, as in Chimera) and nothing more (markers moving with the
  frames is in TAStudio Settings, as Chimera's; making the inputs long is
  the command line's --fill), Columns has no Show All (Restore Defaults
  shows every column), and the menu on the frames picked has Cancel Seek,
  Remove Markers only when a marker is among them, and Branch only on the
  machine's own frame.
  The roll's drags are Chimera's too: on the frame numbers with the right
  button, Shift extends the input pressed on (or the frames picked) over
  the frames passed, Ctrl and Shift put copies of it in, Ctrl puts a copy
  where the pointer is and Alt moves it there, each drag one undo step
  named as Chimera's ("Right-Click Edit: Move"); Alt and the left button on
  a marker's frame number drags the marker to another frame; and the left
  button held in the column before the frame numbers takes the machine
  along with the pointer.
  Check boxes and radio buttons are drawn as Chimera's, which leaves them to
  the system: a light box or circle with a dark edge whatever the theme
  (Fusion's own went dark with a dark theme and could not be seen). This is
  a small style of the window's own (source/ui/qt/style) over Fusion.
- **A setting may be a uint, a long, a ulong or a date and time** (Chimera
  issue 358): the whole numbers hold their C types' ranges exactly (as
  digits in the window, whose numbers are doubles), a type left out is
  inferred wider past an int's range, and a date and time is typed as
  2000-01-01 00:00:00 in UTC (or without the seconds, or a date alone) and
  kept as its seconds since 1970. A value out of range is refused, as
  Chimera's settings grid refuses it.
- **Chimera's MCP server is in the window** (Chimera issue 355; the owner
  asked on 2026-10-25 for every commit of Chimera's to be brought here): the
  same Tools > MCP Server... window, the same tools in the same words, and
  the same test - the witness's own agent (tests/synth/mcp-agent.py), which
  the build runs against this window in both of its modes. To pass it, the
  window's scripts had to be given their turns as Chimera gives them, which
  they were not: a script waiting in emu.yield goes on at the window's next
  turn whether a frame runs or not (about sixty times a second while paused
  or with nothing open), where it used to wait for a frame as
  emu.frameadvance does; and a machine opened or closed starts the running
  scripts again from their beginning, as Chimera's Lua Console restarts on
  a core change. A script that waited in emu.yield for something while
  paused never went on before.
- **Qt is patched where it faults** (owner, 2026-10-24): fixes to Qt go in
  tools/qt-patches and tools/build-qt-windows.sh applies them as it unpacks
  the source. The first: a Qt thread that ended after its data was let go
  read through a null pointer in Qt's clean-up (qthread_win.cpp). Windows
  swallows a fault at thread exit, so nothing broke, but miniBox's fault
  log said "a fault in host code" every time an idle Qt thread ended.
- **Lua's gettool and getconfig give the window's own** (owner,
  2026-10-26). gettool opens the tool by Chimera's name for it and gives
  the window's own object (`chimera.ramWatch` for "ramwatch",
  `chimera.pianoRoll` for "tastudio"; the Log Window has none, so an empty
  table), or nil when the machine open cannot use it, as Chimera's;
  getavailabletools lists the tools the machine open can use, open or not,
  as Chimera's does (its help text says "currently open", and the window
  had followed the text). getconfig gives 116 of Chimera's setting names,
  each read from the window's matching setting when it is read, and none
  can be changed through it; McpAllowChanges is left out, as scripts are
  not given the MCP server's object (an agent's script would change what
  it may do). createinstance stays out.
- **A game runs only in a project** (owner, 2026-10-26: "Both GUI and
  command line should provide a project file, if a game needs loading.
  This unifies the path to execution."). Nothing opens a game alone any
  more: the command line takes a project (and its core package when it is
  not installed; two files that are not a project and a package are
  refused); a game file named alone, or handed over by a second start in
  single instance mode, starts New Project from it, as a file dropped on
  the window does; Lua's client.openrom is gone, as it is from Chimera
  (client.openproject opens a project).
  The Batch Runner, which ran game files, is gone (Chimera never shows
  its menu item). Stop Movie, which let the project go and ran its game
  alone, starts a new project of the same core, files, settings and
  firmware with an empty movie, written nowhere until its first save. The
  machine's held keys go into the movie only in TAStudio's recording mode,
  as in a project they always did. The tests play their games in projects
  they write (qt/tests/game-project.py; gameProject in the view models'
  tests). The same rule came to Chimera and to chimera-run, and to every
  core's tests. What the window asks a core before a project is made is
  asked of one too: the suggested settings, a compile session and a movie
  import each make a project of the files picked so far (in their slots)
  and the settings chosen so far (suggestion.cpp, askingProject), and
  chimera-run is given it with --project; a file a movie import offers as
  firmware is mounted beside the movie by name, since only the core's
  answer can say which firmware it is.
- **The settings file is beside the program** (owner, 2026-10-29, issue
  403): settings.json next to the program, so each Chimera folder keeps
  its own settings, as the old window's config.ini did. When the
  program's folder cannot be written (Program Files, a system install),
  the file is in the system's folder for the person's settings instead.
  --config names another file. Nothing is carried over from an earlier
  file or program (owner, 2026-10-27); Config > Load Config From... reads
  a settings file of this window's, wherever it is.
- **The work stays private** (owner, 2026-10-24): no public branch, CI or
  preview build until the owner says otherwise.
- **macOS is a target** (owner, 2026-10-24): the code is kept portable to
  it, and a macOS CI runner is added when the branch goes public (there
  is no Mac here; cores also need miniBox on macOS).
- Slint was considered and set aside for now. It is the closest fit to
  this pattern, but it has no docking, fewer desktop controls, and its
  licence is GPLv3 or an attribution licence, against Chimera's MIT.

## What the window does

Each window binds to view models through one generic adapter that knows
none of them. Lua scripts reach the same view models by the same names;
the list of names (`--describe`) is what an MCP server's tools would be
made from.

The main window is Chimera's: its menus are Chimera's, in its order
(File, System, View, Config, Tools, Emulator, Help), with System,
Emulator and the status bar there only while a machine runs, and the
debugger, surface viewer and trace logger in the Emulator menu, as tools
the core offers. As in Chimera, the way in is a project: File has New
Chimera Project and Open Project, and no way to open a game alone, and
there is no window to change a machine's settings once it runs (they are
chosen when the project is made). With nothing open the window shows
Chimera's idle eye: its mark, very faint and colourless, blinking now and
then, its eye following the pointer across the window (a view model makes
the picture, only when it would change). Files dropped on the window open
by their kind, in Chimera's order: a game starts a new project from it, a
watch list opens in RAM Watch, a Lua session or script in the Lua Console,
a project opens; several of one kind open none, and a state or cheat file
does not open by dropping, both said over the picture. A game named on the
command line or handed over by a second start starts a new project too; a
project handed over opens with the package named after it. The command
line is Chimera's: --project and --core (or a project and its package
named), --config, --firmware <id>=<path>, --fullscreen, --chromeless,
--lua and --luaconsole, --mmf, --userdata, --mcp-server and
--mcp-allow-changes, and --version; --config naming Chimera's own settings
file is refused rather than written over (Load Config From... carries it
over). Chimera's --headless, --audiosync and --dump-* are not taken yet.
When the core stops, the window asks what now as Chimera's does (The core
stopped): back to the latest safe point, run again from frame 0, save the
inputs and close, or close without saving with the work kept against a
crash for the next open. Capture OSD puts the counters and messages over
the picture into screenshots, as Chimera's does. Config > Speed/Skip has
Chimera's three throttles, one at a time or none: the clock (the
machine's rate, at the speed asked for), the sound as it is heard (the
machine waits while the sound queued is ahead), and vsync (a frame for
each picture the window shows); VSync Enabled sets the window to the
screen's refresh from the next start (Qt sets that when the window is
made).

Each window of Chimera's is a window here too, under
Chimera's title: Controller Config and Configure Hotkeys are two windows
(they were one, with tabs), and TAStudio's Metadata menu opens Header
Info, Edit Comments and Edit Subtitles each in its own window (they were
one Project Details window).

The main window shows the picture alone. TAStudio is a window of its own,
as in Chimera: it opens when a project opens, at the main window's right
side with their tops level (moved left as far as needed to stay on the
screen), and closes with the project. Closing it closes the project, as
Chimera's TAStudio did: unsaved changes are asked about first, and Cancel
keeps both open (issue 400). Its menus are Chimera's TAStudio
menus (Edit, Metadata, Settings, Columns, Window) with "<< Hide" at their
right; hidden, the main window shows "Show TAStudio >>" at the right of
its menus. Beside the roll are Chimera's boxes: Playback (previous
marker, a frame back, pause or run, a frame on, next marker, follow
cursor, auto-restore), Greenzone (every frame, every N frames, off),
Branches and Markers, each with a few buttons and a right-click menu for
the rest. Every movie has Chimera's three markers of its own: Run start
on frame 0, Last input on the last frame anything is pressed on, Run end
on the movie's last frame. They follow every edit and every frame
recorded (worked out as Chimera does: after an edit, walking back from
the end no further than the edit unless it could have erased the
answer), are never saved, cannot be moved, renamed or removed, count
for next and previous marker, keep their states as a marker does, and
are coloured apart from the person's in the roll and the list. TAStudio
Settings' Rotate input roll turns the roll on its side as Chimera's
does: the frames run across with their numbers turned, the buttons down
the left by name, the scroll bar along the bottom; it is kept in the
project, as Chimera keeps it. A right
click on the frames picked opens Chimera's menu for
them: set markers (more than 50 asks first), a marker with text, remove
markers, deselect, select between markers, clear greenzone, copy, paste,
paste insert, cut, clear, delete, insert, clone, truncate, and branch. Keys pressed in TAStudio reach the machine and the hotkeys, as
Chimera's TAStudio passes them on. Move with Main Window (on at first, as
in Chimera) brings TAStudio along when the main window is dragged (not with Shift
held, as Chimera's).

| View model | What it does | Window |
|---|---|---|
| session | open, save and make projects (a game runs only in one; one that does not open says why in Chimera's "Cannot open the project" box, issue 422), recent projects, play, pause, frame advance, restart, seek, keys and gamepads, sound, the greenzone (kept with the project in its cache folder and loaded when the project opens, unless the last session did not end; TAStudio's Greenzone box - every frame, one in N, none - saved in the project's TAStudio part as Chimera saves it), settings applied live or by restarting; the project's changes not saved yet (a star in the title, and Chimera's "Save Chimera Project?" before it is closed, another opened or the program quit); TAStudio's playing and saving settings (pause at the movie's end, the rewind steps, the autosave and its backups, a backup with every save by hand); the View menu's displays: the average speed since it was turned on (the engine's count, on the speed's line), the rerecord count, messages over the picture a moment each, the status bar shown or not, the window sized 1x to 10x the picture as Chimera's Window Size does (each system's scale, 2 at first, remembered; the window following the picture's size unless Static Size is ticked; a scale the screen cannot hold said "(larger than this screen)" and not offered, a smaller one taken while the window would not fit; Larger and Smaller Window change the system's scale and say "Screensize set to Nx"), a project's files not found beside it nor where this machine last had them (local-paths.json in its cache, checked by hash) asked for in Chimera's Locate Project Files, laid out as it is (File, Slot and Status, a missing file in the warning colour and a mismatch in the error colour, what the file picked is under them, a dialog of the main window, Enter opens and Escape cancels): located one by one (a file that cannot be read said in Chimera's message box) or found by scanning a folder, with Include sub-folders a box in the folder dialog (by hash, then by name; "the scan resolved 1 file" as Chimera says it), a file that is not the recorded one opened only when asked (Open Anyway), or the project left unopened; firmware the project needs and that was not found is asked for in the same window, after the files, with "firmware" as its slot (issue 397): a located file must have the hash the project pins (else the package's; a game core takes a file of the person's own where the project pins none), a folder scanned is looked through as the firmware folder is, and the file is remembered for the core as New Project remembers one, so the next open asks nothing, and a log window of every message the window has shown (Chimera's shows its console instead); Chimera's Emulator menu (Game for a game core): a game's own timer over the picture, the save data written out (one file as it is, more in a zip), a movie import opening Chimera's import form for the core, then New Project on its answers; Chimera's File > Screenshot (the Screenshot key's file, Screenshot As to a PNG or a JPEG named where asked, the picture or the window to the clipboard); Chimera's Save Config (the settings are written as they change, so it says where), Save Config As (a copy), Load Config (the file read again) and Load Config From (a file's settings set, and kept as the window's own; Chimera's own config.ini is read too, as at the first start); Chimera's greenzone budgets (from the Cache Manager): what the history of states may keep in memory (4 GB at first; it was a fixed 1 GB here before), how close it stays behind the playhead (one frame in 4) and how many frames after a load show kept pictures (3), every project's and remembered, and a project's own memory and closeness in budgets.json beside its cache (Chimera's file and names), each taking effect when a machine opens; Chimera's Sound window: the sound device (the ones OpenAL lists, or the system's own), the buffer in milliseconds (100 at first; a frame's sound further ahead than that is dropped), the sound at the machine's own speed and while fast forwarding each on or off, the fast-forward volume as a share (50%), frame advance muted, each remembered (only the OpenAL output method is here, as it is Chimera's only one); the window laid out as Chimera's (the two volumes as upright sliders down the left, Normal's moved by hand turning it on or off, RW & FF greyed while Normal is off, the buffer from 30 to 250 milliseconds), a copy kept by OK; Chimera's Speed/Skip: fast forward at its alternate speed (400%, remembered; it was unthrottled here before), no frame skip (its 0 to 9 menu, auto-minimize and Lua's frameskip calls are gone, as from Chimera: owner, 2026-10-24): the machine's speed and fast forward draw every frame, and Turbo and unthrottled draw one frame in N, Speed/Skip's "Turbo/Unthrottled Draws" (every frame, or 1 in 2 to 1 in 10; 1 in 3 at first, as Chimera's) - Turbo drew nothing here before; unsaved work kept against a crash as Chimera keeps it (a snapshot of the project every five seconds while it has changes, and a journal the engine writes every change to the movie into as it is made, with the markers and branches beside it, in a Recovery folder beside the caches, gone when it is saved or closed), and a crash's work rebuilt as a copy among the backups and offered when the project next opens ("Recover unsaved work?": yes opens it unsaved under the project's name), a folder whose writer still runs left alone as another window's | main window, TAStudio Settings, Log Window |
| pianoRoll | a row a frame and a column a button; press, drag, paint, fill; frames picked (on the frame numbers, as in TAStudio, with the thin column before them moving the machine), put in, taken out, cloned, cleared, copied, cut and pasted (over or in) through the system's clipboard as the movie's lines (with their frame numbers when asked; a line that is not a frame's input pastes nothing), cut off, with TAStudio's keys; analog controls a column each with their values, a cell picked to edit and a drag painting its value; markers that can move with the frames, and keep their frame's state or not by default; undo and redo (a stroke is one step), and the undo history by Chimera's step names (undone or redone to a step, forgotten up to one, the most kept chosen); follows the current frame the four ways TAStudio scrolls to it (into view, to the top, the bottom or the middle; every frame or only when it is out of view) and scrolls the wheel's frames a notch; edits that leave the hidden columns alone when asked (Chimera's "allow editing hidden columns" off: clear, paste, put in, take out, cut off and recording change only the columns shown); a column painted with its pattern (Alt held, or always, or on the Auto-hold columns only, as TAStudio's Paint Patterns say), each control's own pattern kept in the project as Chimera keeps it (runs of values, the one it goes back to, lag frames passed over), the columns shown chosen in Config > Controllers, a box beside each control and a tab's at once (issue 391: TAStudio's Columns menu was too long with many buttons; Settings > Columns and the column menu's Show columns open it; a column shown or hidden marks the project changed), a column's menu on its name (Auto-hold, hide, show columns) with the Auto-hold columns marked: while the game plays an Auto-hold column's button is held, fired or follows its own pattern as "Choose pattern" says, and an analog one holds the value asked for when it was turned on (chimera issue 298), all kept in the project as Chimera keeps them; rows coloured as Chimera colours them (the current frame, the greenzone and its lag frames apart where a state is held, the frames an edit took out of it, every second player's columns striped), states and markers told by colour or by an icon, the roll's own colours over the theme's and its own font (TAStudio's Set Colors and Set Font); lag frames hidden as Chimera hides them (of a run, the first N; frames that were lag before an edit too, when asked; kept in the project); macros saved from the frames picked and placed at them, as .chimeraMacro files Chimera reads and writes (only the macro's columns written; pressed over, or put in before, when the file says so), and the recent ones; TAStudio Settings laid out as Chimera's form (Appearance, Autosave, Paint Patterns, Input Roll, Misc, in Chimera's words and number ranges), read when it opens and written only by Apply, with Chimera's warning for markers bound to input while hidden columns are not edited; its pattern editor as Chimera's (a control, its runs "0: On (x2)" and Loop to, Value and Count, Insert, Delete, Account for lag frames); Set Colors as Chimera's Edit TAStudio Colors (its twelve colours on swatches, OK, Cancel, Defaults the theme's; the two Frame# Column colours drawn on the frame numbers of frames the greenzone holds, lag or not, the current frame and frames not run as they stand keeping the wash, as Chimera's roll now does (owner, 2026-10-24), and Analog Edit Mode fills the analog cell being edited); Chimera's several input rolls: the column menu's New input roll puts another beside the one used (showing the columns hidden in any roll, when there are some, else the used roll's), each roll with columns of its own and as wide as they are (the last taking the rest; stacked when turned), the one clicked the one used (Config > Controllers says which roll its boxes are for), Delete input roll, Hide column and Show columns as Chimera's names them, and the rolls scrolling together unless Sync scroll across all input rolls is off; the project keeps the first roll's columns, and a project opened has one roll, as Chimera's; Undo History laid out as Chimera's (ID and Undo Step, the last done in the greenzone's input colour and those taken back in the lag colour, TAStudio's own colours; double-click undoes to a step; the right button's Undo To Selection, Redo To Selection, Clear History To Selection; Undo, Redo, Auto Scroll, Max:, Clear) | TAStudio (a C++ item draws the roll) |
| branches | other versions of the movie, kept in the project, each with its state in a file in the project's cache folder and the picture it had; loading one loads that state, or replays from where the two movies differ when the state is missing or is another machine's; loaded by a double click, or only gone to with the old control scheme (as Chimera does when not recording); a person loading another branch (the Load button, the menu, a double click, a hotkey) while the movie holds inputs or markers of their own the current branch (the one last loaded, made or updated) does not is told so in the message after the load, which Undo Branch Load undoes, and asked nothing; a script's load says nothing of it (owner, 2026-10-28, issue 395); a branch's picture shown while the pointer is over it, no side longer than the branch screenshot size | TAStudio's Branches box, TAStudio Settings |
| ramSearch | the engine's RAM Search, updated after every frame, laid out as Chimera's: its menus (File, Options, Search, Settings, Columns), toolbar and right-click menu; Compare To / By with a box each (previous value, specific value, address, changes, difference) and the Comparison Operator in Chimera's order; Detailed or Fast (a domain of 1 MiB or more starts Fast, and Fast hides the change counts), Check Mis-aligned, the domain, size, byte order and display (a new size reads the search again), Define Previous Value; the preview colours the rows the next search would remove; Auto-Search after every frame, not after a lag frame when told so; undo and redo, Copy Value to Prev, Clear Change Counts, Remove Selected, Go to Address, Add to RAM Watch, RAM Poke (the same window as RAM Watch's), View in Hex Editor, sorting by any column; a search kept in, read from, added from and truncated by .wch files; the warning for addresses past the end and the button for a setting that needs a new search; Chimera's messages and its question before acting on more than 1,000 rows; Freeze Address and Unfreeze All (the frozen addresses, below) | RAM Search |
| ramWatch | addresses kept an eye on, read after every frame, shown unsigned, signed, in hex, in binary, as fixed point or as a float exactly as Chimera writes them; the previous value three ways, changes counted; Chimera's .wch files both ways (other emulators' too); a game core's properties watched by name, read and written as text by the engine; the marked ones shown over the picture where Chimera shows them. The window is laid out as Chimera's: its menus (Files, Watches, Settings, Columns), toolbar and right-click menu; rows picked one, several or a range at a time; New, Edit and Duplicate Watch in Chimera's editor (several watches edited at once, the byte order left as it is when they differ); Split Watch (a word or double word in hex or binary into halves), RAM Poke, Edit Separator, Add Game Properties, Purge Duplicate Watches; copy and paste as .wch lines; moves up, down, to the top and bottom; Default Domain; a click on a heading sorts the stretch between separators as Chimera does; Define Previous Value, On Double-Clicking a Watch, which columns show, recent files, all remembered; the warning for addresses past the end of their domain, clicked to take them off; Chimera's status line; Freeze Address and Unfreeze All (the frozen addresses, below). Set Read and Write Breakpoint are not here, as in Chimera no core has memory callbacks | RAM Watch |
| settings | the package's declared settings, those that apply, edits applied live or by restarting; the package's presets for the machine, written in as if typed (never the machine's own setting; a name the machine lacks named); shown, here and in New Project, in Chimera's settings grid: the name and the value side by side, a line between rows, the chosen row's description in a box under the grid, and a value set keeps the list where it was scrolled to (issue 417) | none: Chimera has no window for a machine's settings once it runs (they are chosen in the New Project window); scripts reach this one |
| luaConsole | Chimera's Lua Console, laid out as Chimera's: a list of scripts with lines between its rows and columns in the theme's grid colour (RollGridLines), as Chimera's list draws them, each started and stopped (toggled), paused, moved, duplicated, edited in the person's editor or taken out, with separators between them; the count of scripts above the list; sessions (.luases) written and read as Chimera writes them, the paths beside the session's file relative to it; New Script from the Lua folder's .template.lua; recent sessions and scripts; Chimera's settings (Disable Scripts on Load, Toggle All if None Selected, Reload When Script File Changes, Duplicate to Stdout); the output, at most 100 writes a frame as Chimera's, its lines picked and copied; the line typed into, an expression's values printed and Up and Down through the lines typed before; Active Registered Functions (call, remove, remove all) and the Lua Functions List (each library's functions with their parameters). Closing it stops every script, as Chimera's | Lua Console |
| mcp | Chimera's MCP server (docs/mcp-server.md), in Tools > MCP Server... laid out as Chimera's: started and stopped, the tick box that lets the agent change the project (remembered; --mcp-allow-changes for one run), the command that sets Claude Code up with the program it starts (bin/chimera-mcp, which needs no DLL of the folder's), the log of every call and Save Log...; the title bar and the menu's icon say when it is started, and --mcp-server starts it from the command line. The engine answers what needs only the machine; the window answers the rest as Chimera does, in its words: the work kept as a branch before the first change, server_info, pause and resume, save_project, close_tastudio, the markers and branches listed, set_input, insert_frames, delete_frames, undo, add_marker, save_branch and load_branch (each change made by TAStudio's own calls, so it is in the piano roll and a step of its undo history), seek, advance_until, capture_series, create_project (the agent's answers through a New Project form of its own, never shown, as Chimera's wizard takes them; what is missing said, firmware not found listed with the files that would do), and Lua: lua_functions (as the Lua Functions List lists them), run_lua (shown in the Lua Console and run there as a script of its own; what it returned given back as JSON, waiting for a frame refused) and lua_script (a file started as Open Script starts one, or stopped and taken out) | MCP Server |
| toolWindows | what Chimera puts at the end of every tool window's Settings menu (in a Window menu where the tool has one, as TAStudio does): Save Window Position (on at first: the window opens where it was left, at its size; TAStudio keeps only its size, as it opens beside the main window), Stay on Top (Windows only, as Chimera's), Float From Main Window, Restore Defaults (the tool's own settings too); File > Close where the tool has a File menu. The Lua Console and TAStudio have them; the other tool windows get them as each takes Chimera's menus | every tool window |
| newProject | a project made before any machine runs, as Chimera's New Project window makes one: the core among the installed packages - emulators or game cores (remembered), a line a package with its version dated in Chimera's clock (issue 331), the package's hash where two are of one core, and its systems -, its machine, its renderer (asked beside the machine and named for what it is, with what a hardware one costs, as Chimera asks it), files in the slots its package declares (kinds - narrowed to those the chosen machine claims, as Chimera does -, how many, the ones a choice rules out, at least one of a group, names a slot asks for, a cue's tracks checked at once), the settings that apply (with the options and default the chosen machine narrows a setting to, its `settingOverrides`, as Chimera has them), when the machine's clock starts (the last row of the settings list, scrolled with them like any other, owner 2026-10-27; Chimera's row sat under the list: UTC, written as 2017-05-27 12:44:28, the sandbox's own at first, from 1970 to 2200; kept in the project as clockStart only when it is another, given to the machine before it first runs, and part of which machine a project makes, so states of one start are not used for another: owner, 2026-10-24, issue 366), the firmware found by the engine's rules or chosen by hand (a file whose hash is not the declared one is refused, except by a game core, whose project then pins that file's own hash; a file chosen by hand is remembered, as Chimera remembers it, so the project finds it when it opens; Scan Folder looks through a folder, with its subfolders or not, for what is still missing - by hash where the package gives one, else by the declared name - and that folder becomes the firmware folder when the one set is not there, as in Chimera (issue 293)); a movie made elsewhere imported (the core's movieImport: its answer, asked in a process of its own through chimera-run, fills the form, and the project takes the movie's inputs); the settings a core suggests for the game (asked the same way, once a game, with the files, slot map and settings chosen so far, taken back for another game, never over an import's), and what the game has of its own: settings after the package's and, for a package that says `gameFirmware`, firmware after the package's (one by a package setting's name or a package firmware's id is dropped); a project's open asks the core again what its game needs, as Chimera does; what is missing said a line at a time; made and opened, written nowhere until its first save, which asks where it goes (as Chimera does); Create waits, its buttons off, until the project is made, as Chimera's waited behind its progress window: the window then closes, and a project the engine refuses (a file of a format its slot does not take) is said on the wizard's line with the files page shown, as Chimera's went back to it (issue 422); opened on the last project's answers - its core, machine, settings and the files still where they were found, the firmware where this core last had it - with the core still asked about the game but its values not put over the project's own (an import is treated the same); another game then takes back the settings that still hold what the core suggested for the project's game (Chimera issue 214), and a game's own setting values go when another game comes; a file dropped on the main window starts it, as in Chimera: the first core that takes its kind (on the last project's answers when they are that core's, its files too only when the file dropped is one of them, so another game does not get the last game's disks: issue 398), the file in the slot that takes it - in place of what a full slot held, where Chimera adds it beside and complains. Laid out as Chimera's wizard, a page at a time with its headings, < Back, Next > (Create on the last) and Cancel beside a line in the error colour: the kind, core, system (a box that is not a choice for one) and renderer with what a hardware one costs and Chimera's issues link; the files, a box a slot titled with how many it takes in Chimera's words (exactly one, 1 to 8), its formats, a list that takes dropped files, Add..., Remove and the swap-order arrows, a slot only settings open and the machine closes left off, one the files close greyed and said unavailable; the settings, the machine's presets above them (Applied "name": n settings, as Chimera says) and what the core suggests; the firmware, Chimera's four columns green or red, Select File..., Clear (until the page is shown again) and Scan Folder..., whose folder dialog carries Include sub-folders. A page goes on only when Chimera's would ("Disks: needs at least 1, has 0", "X is required and not provided"); the files page is skipped for a package with no slots and the firmware page when nothing is called for. A file of another format is added and said, not refused, and more files than a slot takes are said at Next, as in Chimera. Movie import is Chimera's separate form (the movie, each file the import reads with Up and Down for one that takes several, its options, Import and Cancel, its refusals in Chimera's words), then what the core noted in Chimera's message, then the wizard on its answers, whose Create asks where the project is saved. Chimera's compile step, its last page, for a package that says "precompile" (RPCS3): the game's code compiled before the project is made, by compile sessions of the headless runner side by side (chimera-run --project <the files and settings so far> --precompile I/N, as many as the engine says the machine can spare), each a share; what they store is the game's manifest.json in its folder under the compiled code's root, named by the game's SHA1, listed with each module's state, a progress bar, Compile and Stop; Create (and Next) wait, in Chimera's words, until every module is there, and a module gone is said. Tested with the synthetic core's own compile session (six modules); not tried with RPCS3 and a game. As Chimera's: the sessions halved and run again when memory runs out, with Chimera's words when one alone runs out, and the sessions' memory setting in Pre-Compiled Modules | New Project |
| hexEditor | Chimera's Hex Editor, laid out as Chimera's: its menus (File, Edit, Options, Settings) and right-click menu; a box titled with the system, the domain, its addresses and what is being typed; the column numbers, the addresses, the values 1, 2 or 4 bytes at a time in either byte order, and the text they spell (as ASCII, or through a .tbl file); only the lines on screen read, after every frame while the window is open. One address highlighted and others beside it (Ctrl, Shift, a drag), the keys moving as Chimera's (arrows, Page Up and Down, Home, End, Tab, Escape, with Shift taking in what they pass); hex digits typed written in once a value's worth is in; Increment and Decrement; Copy (the bytes), Export (the values), Paste; Find (hex or text) with Find Next and Find Prev, as Chimera looks; Go to Address, Add to RAM Watch, RAM Poke; Save as binary, Save as text, Import as binary; File on Disk (the game opened by itself, edited in memory and saved back); Custom Colors, remembered with the domain, size, byte order and recent tables; Freeze Address, Unfreeze Address and Unfreeze All (the frozen addresses, below) | Hex Editor |
| controls | Chimera's Controller Config: the buttons' keys and gamepad inputs (Normal Controls), the keys that fire a button while held (Autofire Controls: pressed on the frame the key goes down and then as the autofire pattern says, counted from that frame), and what moves each analog control (Analog Controls: a gamepad axis caught by wiggling it - 20000 of movement, as Chimera's - or the pointer over the picture, Bind!, Unbind!, Flip Axis, sensitivity -200% to 200%, dead zone 0% to 50%, and a key for either end, worked out as Chimera's latch from physical); the package's at first, from its default_keybinds.json groups, the section named for the machine's controller as Chimera reads it (else the one that names most of its buttons). Laid out as Chimera's: the three kinds in tabs (Analog only when the controller has analog controls; Feedbacks left out: the engine has no rumble), a tab a player inside each when there is more than one ("Player N" for "PN ...", else "Console"), a box and its name down columns, the box taking the next key or gamepad input pressed while it has the focus (Escape empties it; Auto Tab: the key replaces the box's keys and the next box takes the next, off it is one more), its arrow giving Escape and the pointer's buttons (which hold what they are bound to while pressed over the picture), the package's picture of the controller beside them (its "picture", the machine's own when the package is more than one), the tips, Auto Tab, Misc... (Load Defaults, Clear), Save and Cancel; the window's title is the controller's name and "Configuration", and it works on a copy, kept by Save. Which controls TAStudio shows as columns is chosen here too, in the same copy (issue 391): a box at each button's and analog control's left, and above them the sentence "A ticked box makes the control a column in TAStudio." with Show all and Hide all for the tab shown (issue 418); with several input rolls, the boxes are for the one used, and the sentence names it. What differs from the package is remembered per core. The pointer as a relative axis, as Chimera's: catching the pointer's axis where it is already bound goes from where it is (WMouse X/Y) to how far it moves (RMouse X/Y) and back, the movement added up and read once a live frame (a frame replayed leaves it), scaled as Chimera scales it: at 100%, 120 pixels in a frame push the control all the way and more push it no further (the movement went in unscaled before 2026-10-26, about 83 times too weak). The percentage is Chimera's relative mouse sensitivity, 1% to 1000%, a box at the foot of the Analog tab (Chimera has it only in its settings file; owner, 2026-10-26), part of the window's copy kept by Save, and carried over from Chimera's settings. Chimera's Capture Mouse (its hotkey, the middle button by default, routed as keys are by Key Priority; "Nothing bound to mouse, not capturing cursor" when no analog control is bound to the pointer; "Mouse cursor captured, press <key> to uncapture" for seven seconds; remembered): while the window is active the pointer is hidden over the picture and kept on it, put back inside when it leaves (Qt has no clip as Chimera's Cursor.Clip), so its movement stops at the picture's edges where Chimera's raw mouse goes on. A game's own names for its controls (the core's labels) show where Chimera shows them, in the tooltip over a TAStudio column's title ("P1 A (Jump)"); Chimera's Controller Config does not show them either | Controller Config |
| help | Chimera's Help menu: the site in the browser, the build and the core open in a bug report's words (Copy Version Info), a report's first lines filled in from the session (build, core and version, system, processor and memory, the project's files, the settings off their defaults) with the files worth attaching and a zip of them all and the game's picture (Report an Issue, laid out as Chimera's form and a dialog of the main window: the link with Open in Browser and Copy Link, the text with Copy Text, the files each ticked, the game as it was when the window opened first, Save as Zip... deflated as Chimera's and said in its words, the sandbox's log looked for beside the program first; nothing is sent, and the core log is listed while it is kept; Chimera's newest crash note is not, as the window writes none), Export Core Log, and About. Export Core Log is Chimera's Tools > Export Core Log, moved beside Report an Issue (owner, 2026-10-27) and drawn as a log page with a bug, with a green dot while the log is kept, as MCP Server's icon has while its server runs (tools/draw-core-log-icon.py); the session's: the engine keeps what the cores write in a file only when asked, never remembered, a restart offered so it covers the core from its start | Help menu, Report an Issue |
| mediaMaker | Chimera's Reproducible Media Maker, laid out as its form and a dialog of the main window: a folder packed into one file the same on anybody's machine (a stored zip, an ISO 9660 + Joliet disc, a FAT12 floppy), named beside the folder, how far it has got (Stopping... when cancelled; closing the window cancels), its SHA1 and size said as Chimera's (1,474,560 bytes); an installed core's disc recipe used for a folder it is for (the engine packs: ce_media_make) | Reproducible Media Maker |
| customize | Chimera's Customize window, for what the window has: start paused (the first machine waits; after that a machine opened runs when the last was running, as Chimera's pause is the person's), start full screen, background input and skipping lag frames (the session's), never ask to save, scripts during turbo, the key priority cycle without "both", Enable Context Menu; laid out as Chimera's (General with Startup Options, Advanced; Chimera's words), the boxes a copy kept by OK and let go by Cancel, and remembered. "From controller only" is shown ticked and greyed: here the keyboard is only ever the window's in front. Keys are named as Chimera names them, by their place on the keyboard as the key there on a US keyboard, whatever the layout makes it (a French keyboard's A key, where a US keyboard has Q, is "Q"; Shift+1 is Shift and Number1): the scan code on Windows, the XKB key code of the evdev rules on Linux (X11 and Wayland), the virtual key code on macOS, from tables (key_codes.inc); a key no table names, and a key event no keyboard sent, by Qt's key. Handle alternate keyboard layouts names a key on Windows by what the layout makes it, the keypad and the modifier keys still by place (Chimera's names both Alt keys Windows keys there; here they stay Alt), and does nothing elsewhere, as Chimera's. Merge L+R modifier keys (on at first) names both Shift keys "Shift", else "LeftShift" and "RightShift", likewise Ctrl and Alt, with a combination written in Chimera's order from the sides held ("LeftCtrl+LeftShift+S"); changing it asks Chimera's question, and Yes rewrites the keys bound as Chimera's does, in the hotkeys and in every core's controls remembered and the open machine's (Chimera's rewrite missed every key after the first of a list, for the space after the comma; here each is rewritten, and a key there twice kept once). A core never configured keeps its package's keys, which name both ("Shift, LeftShift"). Single instance mode, on Windows only as Chimera's (greyed elsewhere; read when the program starts): a second start finds the first's named mutex, hands the files it was given to the first through a named pipe (Chimera's payload, each argument's UTF-8 in hex joined by '|'; the pipe refuses other machines) and ends; the first opens the first file as the window opens one, asking about the open project's changes first. The mutex and pipe names are the window's own, so that it and Chimera run side by side never hand files to each other. Enable Wii U/Switch GameCube Adapter Support, as Chimera's, decides whether SDL reads USB devices itself (off, SDL_HIDAPI_DISABLE_LIBUSB is set before SDL starts; read when the program starts). Gone, as from Chimera (owner, 2026-10-24): Skip waterbox integrity checks (a flag nothing read), and Run in background and Pause when menu activated - nothing pauses the machine but the person, whatever window is in front or menu open | Customize |
| messages | Chimera's Messages window: where each of the nine things over the picture goes (the speed, frame counter, lag counter, input, watches, messages, rerecords, autohold, game time), as a corner and a distance from it, Chimera's places at first; the four colours (a colour left at Chimera's default is the theme's, as Chimera's OSDManager decides); how long a message stays; messages stacked (the newest at their corner) or only the newest; Restore Defaults; remembered. Laid out as Chimera's: Message Type (each with where it is) and Message Colors (a swatch to click and its hex) down the left, Position on the right (a small screen to click or drag the mark on, 244 by 180 as Chimera's, a corner to count from at each of its corners, x and y); changes show at once and Cancel, or closing, puts back what was there. The previous frame's input colour is kept but nothing here draws that input yet | Configure On Screen Messages |
| display | Chimera's Display Configuration, laid out as Chimera's (Scaling & Filtering, Misc, Window; the boxes a copy kept by OK and let go by Cancel): the picture's shape (its raw pixels, the machine's own, a size or a ratio; the machine's own is the size the package declares, as Chimera's virtual size, or the picture's height at the shape a core reports while it runs, so a 640x400 picture declared 640x480 is 640x480 at 1x, issue 414; the choices greyed unless the ratio is kept, as Chimera's), kept with bars or stretched, grown by whole steps only or freely, its edges cropped, drawn sharp or smoothed (bilinear), Defaults; Display Features (Full, Minimal - drawn as Full, as Chimera's -, Absolute Zero), titles kept static and the words every title ends with (Chimera's title: the speed while nothing is drawn, the game and its system, the project's file marked when changed, then those words); windowed, the caption's buttons, the menu, the status bar, the frame (none, thin or thick: thin is drawn as thick, Qt has no thin frame for a main window), the place and size kept, stay on top (Windows only, as Chimera's window shows it); full screen, the menu, the status bar, the pointer hidden after two seconds still; double-click full screen; all remembered. The fitting is worked out in the view model and tested; the window draws the rectangle it gets. User Prescale (1 to 16) and Auto Prescale as Chimera's: the picture grown pixel by pixel by whole steps before it is drawn, and with Bilinear first grown by the largest whole steps that fit where it is drawn (each side its own, the crop not counted), so that smoothing blurs only the last, smaller step; a side never past 8192 pixels, the steps taken back as Chimera's are; Defaults turns Auto Prescale on, as Chimera's. Scale OSD text with system scale (on at first, as Chimera's): off, what is drawn over the picture (the counters, the messages, the watches) and its distances from the corners keep their size at 100% on a scaled screen. Scale OSD text with the window (on at first; owner, 2026-10-29, issue 401; Chimera's stays the same size): that text, a script's gui.text with it, and their distances from the corners grow with the window, 12 pixels being a 480-pixel-tall picture area's, never smaller; Encode Video's Capture OSD writes them as the window shows them. The snowy null core as Chimera's (Misc): with nothing open, white noise 256 x 192 instead of Chimera's mark, always, never, or the week of Christmas (December 20th to 26th, days 354 to 360 of the year, Chimera's default); its brightness, bias (each grey a random byte raised to 10^-bias) and rate (60 / the slider's value a second), all remembered. Enable Windows Fullscreen Hacks with Chimera's note (Windows only): a border kept round the window in full screen (Qt's own way, its Windows window's setHasBorderInFullScreen), so that Windows does not take the screen for the window alone; Chimera puts a 1-pixel border round its picture for the same reason; Mouse Capture Forces Stay on Top as Chimera's (shown on Windows only) | Display |
| paths | Chimera's Path Configuration: its global folders, in its order (Base, ROM, Firmware, Movies, Movie backups, A/V Dumps - where Encode Video suggests a video goes -, Tools, Lua, Themes, Watch, Debug Logs, Bug reports, Macros, Temp Files), and for each system a game was opened for its own Base, ROM, Savestates and Screenshots (screenshots go there, as Chimera's). A path is absolute, or starts with "." for its base (a system's base starts from the global one, which starts from the program's folder), ".." for the folder above it, or %exe%, %rom% or %recent%; anything else is the program's folder, as Chimera does; a relative Watch or Debug Logs folder is under Tools. Changed ones are remembered; the backups, the firmware folder (both ways with the Cores window), screenshots and themes follow them, and the file dialogs open in their folder, or the nearest one above it that is there (nothing is made): New Project's game files in ROM's (where the last game picked there came from, while Always use recent path for ROMs is on) and its firmware in Firmware's (issue 402). The Movies folder is made at the start and whenever the paths change, as Chimera makes it, and Save Project As proposes the game's name there until the project has a file of its own, which it proposes after (issue 399). Temp Files, when set, is the program's temporary folder (TMPDIR, TMP, TEMP and TEMPDIR, as Chimera sets them), and emptied it is the system's own again. Laid out as Chimera's: Always use recent path for ROMs (the ROM rows greyed while it is on), Special Commands, Global and by System tabs (a system picked; it opens on the game's system), a row a folder with its browse button (a folder picked is made relative to its base when under it), Save, Defaults, OK and Cancel over a copy. Two differences from Chimera's code, on purpose: ".." is the folder above the base (Chimera's makes it the base itself, the trailing dot dropped by Windows), and a system's base is resolved from the global base (Chimera puts the system's base in front of it again when it falls back to it, giving <base>/NES/NES) | Paths |
| firmware | Chimera's Firmware window: every installed core's declared firmware, asked of the packages when the window opens, one group a core (its versions together, the emulators before the game cores) with Chimera's line for it, a line a declaration (an id declared for many releases is a line each, and more than eight show only those on hand until "Show every release" is ticked), each with Chimera's mark, words and colour - found and its hash matches, found by name, or by size and name; the right size but a dump the core has never seen; not the size the core expects; a chosen file that is gone; not found - where it was found (the Firmware folder, or chosen), and what the line picked says in full (what it is, what the core expects, when it is needed, what is on hand and its hash). Show: All, Emulators, Games (remembered). Chimera's Find box: a line stays when its core, the firmware's name, id or release, the file name asked for or the file found holds the text, any case; a core's files fold to one line ("+ Synth Bios: 10 files"), opened by a double click or +, folded by -, and the right-click menu folds or opens one core or all; Open Folder shows the folder of the file found for the line picked; the hashes of the files read are kept in file-hashes.txt in the data folder, so a rescan reads only new or changed files (owner, 2026-10-24, issue 368). Set File... (or a double click) remembers a file where it is, a pinned dump under its own hash as Chimera keys it; Clear forgets a chosen one; Scan Folder... hashes a folder, and below it while the folder dialog's Include sub-folders is ticked (it starts ticked), and remembers every declaration it answers, and a folder that answered any becomes the firmware folder when the one named is not there (Chimera issue 293); Rescan. A requirement with no hash whose size and name hints point at several files says so, as Chimera's ("2 candidates - choose one with Set..."), and the list keeps its scroll place across a rescan | Firmware |
| dataFolder | Chimera's Data Directory: where everything the window keeps for the person lives (each project's cache, the unsaved work kept against a crash), how much is there and free on its drive (as Chimera writes sizes), and a way to send it elsewhere: recorded here, done at the next start before any of it is open (a rename where it can be, else a copy checked before the old is deleted; a failure leaves the old whole). Laid out as Chimera's: what it holds, where it is, the figures, the change waiting, Change..., Use Default, Keep Current, Open Folder, Close; picking a folder asks Chimera's questions (use one that already holds data as it is, or move what is here, with a warning when it may not fit). CHIMERA_DATA_HOME decides it when set, as Chimera's (the window says so and changes nothing) | Data Directory |
| precompiled | Chimera's compile cache and its Pre-Compiled Modules window: for a core whose package says "precompile" (RPCS3), the engine is given a folder for the game, named by its SHA1 under Cache/PrecompiledCode in the data folder (before, the window gave none, so such a core compiled at every boot); the window lists those folders, named, dated and counted by their manifest.json when there is one, and removes the ticked ones, never the open machine's. Laid out as Chimera's: how many games and how much, and what removing costs, at the top; a row a game with a box to tick (Game, Compiled by, When in local time, Modules, Size in Chimera's words; a game nothing can name greyed, one missing pieces in the error colour); what the row picked means under it; Remove (with what it frees, asked first), Open folder and Close. and Chimera's Memory per compile session (GB), 1 to 256, 8 at first, remembered: as many compile sessions run side by side as the computer can give that much each | Pre-Compiled Modules |
| traceLogger, debugger | Chimera's Trace Logger: the core's own line for each instruction its CPU runs (what ran, then the machine around it after a tab), drained each frame through the engine, the latest 10,000 kept (100 to a million), what is to be kept written to a file as it comes, the visible lines saved; the core traces only while logging, and stops when the window closes. The Debugger's register box: the core's registers, read as the machine moves (ten times a second at most) and set by hand (hex), with the cycles run when the core counts them. The Surface Viewer: the pictures a core draws for itself, picked by name, 1x to 8x, drawn again as the machine moves while the window is open. The synthetic core now traces, shows R0-R7 and its frame counter, and draws its RAM as a 64x64 picture, so all three are tested. Not here, as in Chimera no core offers them (a core runs a whole frame without giving control back): a disassembler, breakpoints and memory callbacks. The Trace Logger's window is laid out as Chimera's: the File menu (Save Visible Lines...), the toolbar with Start Logging and Log to File... as buttons that stay down, Clear and Keep, the Disassembly and Registers columns, and Chimera's status line (the core's header, the lines buffered, those written to the file); its dialogs open in Debug Logs | Trace Logger, Debugger, Surface Viewer |
| macroTool | Chimera's Macro Tool (Macro Input): the entire movie and zones of it (a start and an end frame, or a .chimeraMacro loaded), each named, with Overlay and Replace and the columns it carries; placed at a frame (one undo step), saved as a .chimeraMacro Chimera reads - a zone of some columns written as Chimera writes one (grouped by player, axes before buttons) - and zones not saved asked about on closing. The piano roll's Save Selection to Macro and Place Macro share its code. The window is laid out as Chimera's: the File menu (Save Selected As..., Load Macro..., Recent, shared with TAStudio's Recent Macros), the start and end with Set Macro, the zones with the entire movie first, the name, frame, Place Zone, Replace and Overlay, and the controls ticked along the bottom placed as Chimera places them; asked on closing, Yes saves each macro not saved | Macro Input |
| encoder | Chimera's File > Encode Video, as Chimera's own design has it (its docs/encode-video.md): a video is an encode of a run, made in one act. The window asks for everything at once: the file, the ffmpeg command (typed, or one of Chimera's nine presets; the box names the preset the command is, and the file takes the extension of the container the command names), the two markers to encode between, both included (it opens on Run start to Run end), a size of its own (padded or stretched, nearest pixel; otherwise written in the shape the machine shows, issue 375) and whether to sync to the sound (on at first, as Chimera's). Start Encode keeps where the machine is as a state file the engine writes in the project's cache folder, winds the run to the first marker, plays it read only as fast as it goes, keeping no state, pipes every frame and its sound to ffmpeg as a NUT stream (Chimera's muxer, ported), and gives the machine back on its frame, paused or running as it was. A video already at that name is not replaced without asking; progress, frames a second, time left and the sound's levels are shown in Chimera's words (frame 1,234 / 5,000   60 fps   about 1m 3s remaining); what the window refuses (a start the encoder turns down, a video kept rather than replaced, a folder or video gone) is said in the status line in the error colour, as Chimera's, and with no project the menu says Chimera's "Open a project first"; Stop and closing the window end it; a picture whose size changes starts a new file. The ffmpeg is the one the bundle ships in its bin folder (the build's in build/dll). Scripts have client.encodevideo and client.encodingvideo, as Chimera's. A project's run of frames 5 to 14 was encoded and counted by that ffmpeg: 10 frames, both ends included, the machine back on its frame. Chimera's File > AVI/WAV, which the window had copied from the BizHawk lineage, is gone, as it is from Chimera. Capture OSD and Capture Lua as Chimera's (OSD takes Lua with it, and Lua off takes the OSD): each frame is handed to the window. Capture Lua paints over it, at the machine's own size, what scripts draw with the gui library. Capture OSD writes the frame as the picture area shows it, at that area's size (its shape, bars, crop and filter; the window's 2x or 3x is the video's), with what scripts draw on both surfaces and the counters, messages, subtitles and watches laid out as the window lays them out, as Chimera's wrote it (owner, 2026-10-30); its pixels are square. The "now" beside Current size is the size written: the picture area's with Capture OSD, else the machine's own pixels. Scripts have their turns around every frame encoded, as each is a frame played, and the counters follow the machine. Checked with the real window: a script's box and text, the frame counter (13 on the encode's fourth frame from 10) and the messages were in the video. | Encode Video |
| Lua libraries | Chimera's Lua libraries as a layer of Lua over the view models (lua_libraries.cpp); Settings > Register To Text Editors as Chimera's (Sublime Text 2, said detected or installed as Chimera finds it, its Lua package given Chimera's completions; the file is chimera.lua.sublime-completions where Chimera's is still named bizhawk.lua.sublime-completions, and a missing package folder is said rather than failing); Help > Lua Functions List as Chimera's (Return, Library, Name, Parameters, Description in Chimera's own words and types, kept in lua_docs.inc; those the window lacks marked [Not available], deprecated ones [Deprecated]; sorted by a column's name, filtered by name or description, Copy puts the call and its example on the clipboard), with Chimera's names, arguments and table numbering (from 0 where Chimera's are): console, bit (byteswap_64 too), bizstring (Chimera's string methods: positions, lengths and widths counted in UTF-16 units, encode and decode by the encodings' usual names, such as shift_jis, through Windows' converters or iconv), emu (frameadvance and yield, framecount, lagcount, islagged, setlagcount and setislagged, the system and core, registers read and set, cycles, the frame rate limit), memory and mainmemory (domains, bytes, ranges as tables, every size, sign and byte order, floats, hash_region's SHA-256 as Chimera gives it; the default domain the system bus when there is one), joypad (get, set for the next frame, setaxis for an analog value on the next frame, setanalog for one held until let go, setfrommnemonicstr for a movie line's buttons and values), movie (loaded, file, length, mode, rerecords read, set and whether they are counted, input of a frame by full or controller names and as its movie line, the header, comments and subtitles, save, play from start, stop), client (pause and play, speed, reboot, open and close, sound, messages, autohold, turbo, seeking and rewinding, the picture's size, the window's scale and place, screenshots to a file or the clipboard with Capture OSD, the tools the machine can use and their windows opened, encoding paused, sleep and exactsleep, the functions list, the picture area's size, where a point of the machine's picture is shown and the bars beside it, the game's and the client's extra padding, and show_future: after each frame, frames played ahead with the movie's input until the script's function says enough, their picture shown and the machine put back), gameinfo (a game's database status: Chimera has no database, so "NotInDatabase"), event (onframestart before each frame and onframeend after it, on the machine's thread; onexit; onconsoleclose; unregisterbyid and unregisterbyname on the script's own; the events part way through a frame refused with Chimera's words, so that a script can guard them with pcall), memorysavestate (the core's own state kept in memory by an id, and loaded back on its frame), userdata (values kept for the session, shared by every script, as Chimera's, in memory; Chimera's --userdata k1:v1;k2:v2 fills it at the start, read as Chimera reads them), SQL (a SQLite database made, opened, written and read, every value keyed "column row" and every answer in Chimera's words; the SQLite Chimera builds), comm (data shared with other programs on this machine through named memory-mapped files - a Windows file mapping's name, on Linux /dev/shm/<name> - written, read, copied to and from memory, and the picture as a PNG to the file --mmf or mmfSetFilename names; no network, as Chimera's), forms (a script's own windows, "Lua Dialog" when unnamed, over the main window and in the platform's own colours whatever the theme, as Chimera leaves its Lua forms unthemed: forms with buttons, labels, text boxes, check boxes, dropdowns sorted as Chimera's, and picture boxes drawn on as gui draws, shown on refresh; clicks and a form closed by the person call the script's functions; text, place, size and the common properties read and set; openfile shows the window's file dialog and waits for it, as Chimera's: in the folder asked, else the named file's, else the Lua folder, with Chimera's filter text ("Images (*.bmp;*.png)|*.bmp;*.png") read as the dialog's filters, and "" back when cancelled), tastudio (engaged when a project is open; the edit queue - buttons, analog values, frames put in, taken out and cleared - applied together, each edit its own undo step where Chimera makes them one; the lag log read and set; whether a frame has a state; clearing the greenzone; going to a frame or a marker by its text; the frames picked; markers set, read, found and removed, the run's own among them; branches listed with an id, frame and text, renamed, the next one's text given, loaded, and a frame of one's input read; recording mode read, set and toggled, as Chimera's, a script's own edits leaving it on; functions called when the greenzone is invalidated (every edit and frame recorded, with its first frame) and when a branch is loaded, made or updated, or removed (with its place); a script's own columns (addcolumn) and the colour and text it gives any cell (onqueryitembg, onqueryitemtext; the first answer winning), asked while the roll is drawn - the window's thread waits on the machine's then, between two frames - and gone when the script stops; and an icon from an image file for any cell (onqueryitemicon), each file read once until clearIconCache), game (a game core's properties by name, in the core's order: read and set by the engine's rules, an array by its own name as a table, bytes as a table of byte values, and described; a name the core lacks said in the console, as Chimera's), input (the keys held whatever they are bound to, the gamepads' buttons, the pointer over the picture in the machine's pixels with its buttons and wheel; the gamepads' sticks are not followed), gui (in the pixels of the surface Chimera's scripts draw on: the machine's picture with its crop taken off and the game padding put around it, so (0, 0) is the corner of what is shown, as Chimera's; rectangles, boxes, ellipses, lines, axes, pixels, polygons, Bezier curves, pie slices, text - its font, style and alignment as Chimera's, its background an outline around the letters - and in the window's from a corner, images and parts of them read from files - a relative path from the script's folder, as Chimera runs a script in its folder -, the default colours, messages; drawn over the picture by the window, scaled with it, and gone when the next frame begins, as Chimera's; createcanvas, a window of the script's own drawn on with Chimera's LuaCanvas functions and saved as a picture); each script runs as a coroutine of its own: where it waits for a frame (emu.frameadvance) it goes on after the next, and where it yields (emu.yield) it goes on at the window's next turn, a frame run or not (about sixty times a second while paused or with nothing open), as Chimera's loop resumes it; a machine opened or closed starts the running scripts again from their beginning, as Chimera's Lua Console restarts on a core change; what a script registers is its own (the event functions, tastudio's, its forms), called as that script and gone when it stops, and a script whose code has ended runs on while it has any, as Chimera's; a script's code runs in its own folder, so its dofile, require and io.open find files beside it; console.log and print put tabs between values and write a table a key a line, as Chimera's; Chimera's Toggle Last Lua Script. gui's client surface too (named last in each drawing function, or by gui.use_surface; the picture area's own pixels), and an image drawn with cache false read again once its file has changed (so it shows what the file holds now, as Chimera's reading it at every call does); what is drawn on the client surface is in an encoded video only with Capture OSD. A text drawn (gui.drawText, gui.pixelText, gui.text, a form's drawString) breaks at each new line, a line a row from the text's left, the text as a whole put at the point, as Chimera's. getavailabletools lists the tools the machine open can use, open or not, as Chimera's; gettool opens one by that name and gives the window's own object for it (chimera.ramWatch, chimera.pianoRoll for TAStudio); getconfig gives Chimera's setting names read from the window's settings when read, the choices by their names in Chimera's lists, colours as Chimera's numbers, the places over the picture as {X, Y, Anchor}, and Movies' greenzone settings, none of them changed through it (a name the window has no setting for is nil). Not here: client.createinstance; sizes are in the window's own units, which are the screen's pixels unless the system scales the window. | Lua Console |
| cacheManager | Chimera's Cache Manager for the project caches and the unsaved work kept against a crash: each named as its project says (about.json, which the session writes beside the cache when a project is saved or opened), its size, the projects no longer where they were last seen picked out, what is in use kept, the ticked ones removed after a question naming them; Chimera's limit (100 GB, and 20 GB of the disk left free, never below 1 GB for the disk's sake) held by the auto-clean at the start and when a project closes, the oldest unlocked first, never the newest or the project just closed; Lock / Unlock (project caches start unlocked, unsaved work locked; cache-locks.json in the cache folder), Clean Now, and the line saying why the cache is over its limit Laid out as Chimera does: what is cached, the orphans and whether it is over its limit at the top, Select all (how many ticked and how much) and the limit over the list, a row an entry with a box to tick (an entry in use is greyed and cannot be) and Chimera's padlock (red and open: the auto-clean may take it; green and shut: it may not), What, Name, System, Core, Game, Project id, Project file, Cache location, Size, Last modified (a heading click sorts; size and date start largest and newest), what the row picked means in full, the line of what was done, and Remove Ticked and Clean Now (each asked first with what it costs and frees), Lock / Unlock, Select all orphans, Greenzone budgets..., Open Folder and Close; sizes written as Chimera writes them. Open Folder has a place of its own, as Chimera's now does. With nothing picked by hand, the row picked is the project open now, else the project used most recently, as Chimera picks it (issue 369; it picked the first row, the largest, and Greenzone budgets... then named a project nobody was working on). Greenzone budgets... opens Chimera's window for every project's budgets and, when the row picked is a project's cache, that project's own (kept beside its cache), OK keeping them | Cache Manager |
| contextMenu (main window) | Chimera's right-click menu on the picture: Restart Movie, Stop Movie, Backup Movie, Stop Movie without Saving, Edit Subtitles, Add Subtitle (Chimera's Subtitle Maker, on the frame the machine is on, editing the subtitle there if there is one), Edit Comments, Save Movie, Save Movie As... while a project is open; Config while full screen; Screenshot and Close Project while a game runs; Show or Hide Menu while full screen or the menu bar is hidden. Off with Customize's Enable Context Menu; not opened when the right button holds one of the machine's buttons, unless Shift is held; the keyboard's menu key opens it too. A double click on the picture goes full screen and back, as Chimera's (Display's Allow Double-Click Fullscreen, not when the left button holds a button, unless Shift). Chimera's own Restart, Stop and Save items do nothing (no handler is attached to them); here they do what their hotkeys do. The pointer's buttons over the picture now hold what they are bound to | main window |
| projectDetails | the open project's header (author, emulator version, board name, game name; the core shown, not edited, since its package pin decides it), its comments, and its subtitles - a line from a frame, for a number of frames, at a place, in a colour - kept with the project, the frame's subtitles worked out for the window to draw; a new project starts with the default author, as in Chimera | Header Info, Edit Comments, Edit Subtitles (three windows, as Chimera's TAStudio Metadata menu opens them) |
| hotkeys | the window's actions on keys, with Chimera's names, keys and order: frame advance (held half a second it runs on), rewind (held it goes on back), pause, fast forward, toggle throttle, screenshot (a PNG named and numbered as Chimera names one), full screen, open, close, load last, reboot, volume up and down, faster, slower and its own speed (Chimera's steps, 1% to 6400%), sound on or off, exit, the tool windows, RAM Search's six, branches (add, slots F1-F10 and Shift+F1-F10), seek to a frame, follow, auto-restore, greenzone, undo and redo, markers (to the previous or next, set, delete, select between), select all, and the frame edits (clear, delete, insert, insert a number, clone, clone a number of times, paste insert), show the current frame, select it, seek to the frame picked, back to where the edits began (Chimera's green arrow), pick the copied frames again, the screen or the window to the clipboard, hide the menu, stay on top, TAStudio shown again; changed in Configure Hotkeys and remembered. That window is laid out as Chimera's: Find (a name in any case, the names that start with what is typed suggested), a tab a group in Chimera's order with the actions in Chimera's order, a name and its box down columns (the box as Controller Config's, taking combinations such as Ctrl+S: a key is bound when it is let go, so Ctrl alone can be), Chimera's note at the end of the TAStudio tab, the tips, Auto Tab, Misc... (Restore Defaults, Restore Current Tab Defaults, Clear All, Clear Current Tab), Save and Cancel; it works on a copy, kept by Save; a key that is a hotkey and a button does both, as in Chimera. A key a hotkey or a control takes is theirs and not the menus' (issue 405): Alt+V bound to a hotkey runs it and does not open the View menu (unbound, it opens it), and Alt let go after another key, or Alt that is itself bound, does not take the keyboard to the menus, as Windows and the old window had it; the letters that open menus are underlined only from Alt pressed until the menus are left, as the old window showed them. A menu's action is run by its hotkey, and the menu shows the keys bound to it (a relay hands the window what is the window's: a menu, a dialog, a window). the counters over the picture (speed, frame counter, lag counter, input display: where and as Chimera draws them, each on or off and remembered). Turbo (held: as fast as it goes, nothing drawn or heard, the speed marked >>>>), the window a size larger or smaller (1x to 10x), Frame Inch (paused a frame, running a pause), messages on or off, Skip Lag Frames (a frame advance that lands after a lag frame runs on to one that is not, 600 at most; remembered), Display Nothing (frames run undrawn, the picture and counters black, the speed in the title; remembered), background input (a gamepad heard while the window is behind others; remembered; the keyboard is only ever the window's in front). Chimera's Key Priority for a key that is a hotkey and a button (both act, the button wins or the hotkey wins, each giving way only for a key the other side has, as Chimera's input manager decides), from the Emulation menu, the status bar (a click is the next one) or its hotkey; remembered. Toggle read-only turns TAStudio's recording mode on or off (with no project, Chimera's "No movie active"). Toggle All Freezes switches every freeze on or off, said as Chimera says it. Chimera's Movie group as TAStudio answers it: Play Movie (open a project), Record Movie (New Project), Stop Movie (the project let go and its game opened alone, a new empty movie), Play from beginning (the project read again from its file), Save Movie (the project saved). Toggle Turbo Seek (Shift+S) and Cancel Seek, as TAStudio's box and row menu | Configure Hotkeys |
| themes | Chimera's colour themes: its built-in Light (the desktop's own colours) and Dark, and every theme file in the themes folder (the built-in two first, then the files by their themes' names); one chosen (Dark at first, as Chimera's default) and remembered; the window's palette and the piano roll's TAStudio colours follow it. The Theme window is laid out as Chimera does: the list (Theme, Look; a theme's description as its tooltip), a drawing of Chimera in the theme picked - menus, tool strip, a form with its fields, coloured words and marks, a level meter, an analog range box, a hex editor, a list, a piano roll with every kind of row, the status bar - where pointing at a part names its colour and value, the theme's name ("in use"), its lines and who made it, how many themes there are and what would not load ("why?" lists them), Open Themes Folder, Write a Copy to Edit (a file of its own, the desktop's colours painted, never over a file that is there), Reload Themes, OK and Cancel. Picking only shows a theme; OK or a double click puts it on | Theme |
| menus | Chimera's menu icons beside each entry (by name under menu/ in ui.qrc, those drawn for Chimera kept as PNGs in source/ui/qt/menu-icons, with RAM Watch's icon read from its .ico and Config > Language's globe drawn by tools/draw-language-icon.py), and none on what can be ticked, on labels, or on Exit, by Chimera's rule; TAStudio's branch and marker menus with theirs; a row without an icon keeps its text in line with those that have one (MenuEntry.qml); every menu's dividers in the theme's MenuSeparator colour (MenuDivider.qml; Fusion's own are near black and cannot be seen on a dark menu), and the three Chimera has that were missing (before Speed/Skip, and around Tools' managers; the one over Export Core Log is gone, as that entry is now in Help, owner 2026-10-27) | the menus |
| scroll bars | seen all the time there is more to scroll to, with their groove drawn, as a desktop's (owner, 2026-10-27; Fusion's own show only while used and fade away): the window's style (style/ScrollBar.qml), so every list, table and text in every window has them; a scroll view keeps its content clear of its bars (style/ScrollView.qml), and text that wraps never scrolls sideways | every window |
| project settings | Chimera's File > Project Settings (issue 370, owner 2026-10-25): the open project's settings changed after it was made. The settings the New Project window shows for the project's files, the renderer among them, the game's own after them, and when the machine's clock starts (UTC); not the machine, which goes with the project's files. Restart with these is offered only when what the machine is made from changed (each setting at its value or default, so the default typed back is no change; the clock counts), asks once more, and then the project takes the settings and the clock (unsaved, so a save writes them) and the machine starts again at frame 0, the inputs kept, as Reboot Core in a project; a greenzone kept with the old settings is not used. When the machine does not start with them, the old ones are put back and it starts with those, and the window says so. Not here (as in Chimera): a setting changed at a frame inside the movie | File > Project Settings |
| cores | the installed core packages and the files each takes, the cores and firmware folders; a project's core is found here, and its firmware by the engine's rules. The Core Manager is laid out as Chimera's: the folder and how a core arrives, Select all (how many ticked) and Show (All, Emulators, Games; remembered), a row a core with a box to tick (Core, Type, Systems, Version - the newest and how many more -, Size), the versions of the core picked newest first with Remove version and what the version says (where it is, when it was built, its terms from the package's licenses.json, or that it states none), the line under the list, and Open cores folder, Change folder..., Refresh List, Remove (every version of the cores ticked, asked first with what it costs), Show Systems... (Chimera's Supported Systems: a filter, each system by name with its id and the cores that run it) and Close. Only files in the cores folder itself are deleted. Not here: the note about a folder earlier Chimera versions downloaded cores to (the window never downloaded any) | Core Manager |

The tests drive the view models with no window: 1,029 checks on the
synthetic core (and on four packages made from it at build time: one under
another name, one of two machines with declared slots and a firmware, and
two whose firmware declares the hash of its file, one of them a game core). They include a project whose movie ends with the machine's
memory byte for byte as the witness recorded it. One more test loads the
whole window off-screen and runs it for a second, and fails on any QML
warning (a binding to a name no view model has reads as nothing and only
warns: Unfreeze All was never shown in RAM Search, RAM Watch and the Hex
Editor for that reason until 2026-10-24). Every new check was
broken on purpose once and seen to fail.

## What was measured

- A project saved with the greenzone every 5 frames opens with that choice
  and its columns; saved again with another choice, every other field it
  had is kept.
- A real DOSBox-X project made with the New Project window from a script
  (a configuration file whose autoexec prints a line and the DOS version)
  boots and runs it, at the machine's own 70 frames a second (Linux,
  off-screen).

Linux, headless, Qt's software drawing:

- **Full speed while the piano roll scrolls.** Snes9x, Genesis Plus GX
  and the synthetic core ran at their own rate (60 frames a second) while
  the piano roll scrolled through 1,000,000 frames. Each redraw took
  0.24 to 0.52 ms.
- **Running without the picture:** about 3,850 frames a second (Genesis
  Plus GX) and 3,950 (Snes9x).
- **Seeking:**
  - jumping back 1,500 frames, to a stored state: 9 to 10 ms;
  - an edit 2,900 frames back, replayed to where the machine was: 0.7 s.

Windows (the GTX 1060, through WSL, screen locked):

- **Tests and speed:**
  - the view-model tests pass natively;
  - Qt chose Direct3D 11 on the GTX 1060;
  - the machine held 59.9 frames a second while the piano roll scrolled.
- **Download size:** 69 MB with debug information stripped, of which Qt
  is 57 MB.
- **The window on screen (issue 404, 2026-10-29):** FreeJ2ME playing a
  new project's empty movie past its end, as a person recording does, with
  TAStudio open. Before: the main window drew 286 of 1,037 frames (a
  window frame every 58 ms). After: 1,339 of 1,344, both windows a frame
  every 16.6 ms (median), the roll drawn in 1.0 ms (7.7 ms before). What
  is left are a few pauses of QML's script memory clean-up, 15 to 37 ms,
  about one every two seconds. Windows lets no Direct3D window present
  frames while the screen is locked, so these runs need it unlocked.

## What went wrong, and what was done

- **Qt's threading variant on Windows.** Qt built for Windows with the
  compiler's "win32" threading variant (the one the rest of Chimera's
  Windows build uses) crashed in `libstdc++-6.dll` as a Qt thread ended at
  exit. Built with the "posix" variant, as Qt supports, the crash is gone.
  The Qt window's Windows build uses its own cross file
  (`extern/meson/mingw-w64-qt.ini`).
- **OPEN: the exit hang.** On Windows, a program that has shown a Qt
  Quick window does not finish exiting; it stays listed, holding memory,
  and cannot be stopped. A minimal Qt Quick program built the same way
  does too, while one with no window exits cleanly. To be retried with
  the screen unlocked, and then against Qt's own prebuilt Windows
  libraries.
- **zstd in resources.** The Linux Qt's resource compiler packs with
  zstd, which the Windows Qt (built without it) cannot unpack:
  `--no-zstd`.
- **Network code.** The Linux Qt downloaded from Qt has its network
  module inside QML; the Windows one built here leaves it out, since
  Chimera has no network code. A shipped Linux build would need the same.
- **Caches in Chimera's folder.** Qt keeps caches under the application's
  name. Named "Chimera", the window once wrote two cache files into the
  real Chimera's data folder, and was given a name of its own. As the
  window that replaces Chimera's it is named "Chimera" again (owner,
  2026-10-26), its settings and caches under that name, and its single
  instance found by Chimera's own mutex and pipe names, so the two never
  run as two single instances at once. What it kept under its old name is
  left where it is, not moved.
- **Frame numbers.** The engine counts frames only for a movie it plays
  itself, and a seek away and back within one report changes no frame
  number. The view models count their own frames, and scripts have their
  turn after every frame and after every seek, on the machine's thread.
- **Lua and C++ destructors.** Lua reports errors by jumping out of the C
  function, past C++ destructors, so the bridge raises its errors only
  after its C++ objects have ended.
- **RAM Watch's floats and previous values.** A float is written as the
  shortest text that reads back as the same float (checked over 200,000
  values), so two different floats never look alike. And with "before the
  last change" chosen, the previous value is the one before the change.
- **Column names are not Qt's.** A table column called "display" took
  every cell's text, because Qt calls a cell's text "display". The adapter
  no longer lets a column name shadow one of Qt's.
- **The piano roll was put in the main window.** At first the roll sat beside the picture in the main window, with
  TAStudio's menus in the main menu bar. In Chimera, TAStudio is a window
  of its own; the owner pointed this out on 2026-10-23. The roll, its
  menus, the side boxes and TAStudio's own windows (Header Info, Edit
  Comments, Edit Subtitles, Undo History, TAStudio Settings) are now in a TAStudio window, and the
  separate Branches window is gone: the branches are TAStudio's Branches
  box, as in Chimera.
- **Copies of a project shared a cache folder** (fixed 2026-10-29). The
  cache folder is by id, and a copy kept its original's id, so opening one
  could load the other's state history. A project saved under another name
  now gets an id of its own; a file copied by hand, a moved one or a backup
  still keeps the id, and opened from another path than the one its cache
  recorded, it drops the cache's history and branch states (the 2026-10-15
  rule, now in the window too).

- **A project's columns before its buttons** (2026-10-26). A project
  with no piano roll columns saved (every one New Project makes) was
  given its default columns before the window had the machine's buttons:
  nothing was hidden, and its first save wrote a roll of the cursor and
  frame columns alone, so the Auto-hold columns and the keyboard keys
  hidden at first were lost when it next opened. Games opened alone came
  first through another path and hid it; the columns now wait for the
  machine's buttons.

## What the window does not do

Taken from Chimera's menus, dialogs and tools on 2026-10-23, and ticked off
as each lands here. Parts that wait on a question to the owner say so.

- TAStudio: nothing. Cancel Seek stops any seek part way: one that plays
  (Turbo seek off) between frames, a turbo seek between the pieces it goes
  in (about a tenth of a second each). (Chimera's Selection Undo and Redo
  are always greyed out and do nothing, so there is nothing of theirs to
  bring.)
- Tools: nothing. (The debugger has no disassembly or breakpoints for
  these cores in Chimera either: the engine has no call for them.)
- Customize and Display: nothing (2026-10-25: keys named by place and
  the two key options, single instance mode, the GameCube adapter,
  prescaling, OSD scaling, the snowy null core, the fullscreen hacks).
- Lua: client.createinstance, which built objects of the program by their
  class names, is not here (owner, 2026-10-26); gettool and getconfig give
  the window's own objects (see "What was decided").
- Projects: nothing. Chimera's Cache Manager also lists unpacked cores;
  the Qt window opens a core's package without unpacking it, so it has
  none to list. Compiled code is in Pre-Compiled Modules in both.
- Nothing for jump lists: on Windows each project or game opened goes on
  the taskbar's Recent list (not seen working
  on the test PC, whose Windows has recently opened items switched off).

## Found in the engine on the way

- **A picture's shape when a core declares none** (2026-10-24). The engine
  answered the largest picture's size as the virtual size, so a Flash game
  (Ruffle declares none: its pixels are square) was shown too wide. Chimera's
  window uses the picture as it is now; ce_session_virtual_width and
  ce_session_virtual_height now say that too. Only the Qt window reads them.
- A state put back from the greenzone onto a machine far ahead of it can
  keep pages the state does not have (seen 2026-10-23, synthetic core:
  frame 0 put back from frame 30, then saved after a frame, carried two
  pages more than the history's own frame 1). Put back from a machine on
  the history's own line, it does not. Nothing desynced in that test, but
  a state put back should arguably be the whole machine; Chimera's own
  state history check would report it as bad data. The window's check
  puts frame 0 back twice before it walks, and says so.
- Chimera's run_lua (the MCP server, issue 355) puts the agent's code in a
  Lua long string, `[[` code `]]`, choosing more `=` only when the code
  holds `]]`; code that ends in `]` (`return t[1]`) closes the string one
  bracket early and fails to compile. The Qt window puts a line break
  before the closing brackets (seen 2026-10-25; Chimera's is not changed).
- Chimera's TAStudio, and the engine's line reader, read a pasted line by
  place: a line copied with "include frame numbers when copying input"
  ("00002 |..U.....|") pasted its digits as buttons held (seen
  2026-10-23). Both now drop a frame number before the '|' (chimera
  630186d, owner 2026-10-24), and the window still refuses any other
  line that does not start with '|'.


## Yours to decide

Nothing open: the last three questions were answered on 2026-10-24 (see
"What was decided").
