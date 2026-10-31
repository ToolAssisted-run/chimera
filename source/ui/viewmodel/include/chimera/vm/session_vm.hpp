#pragma once
/* The machine running: open a core package with a game, play, pause, go one
 * frame at a time, hold buttons. The machine runs on a thread of its own, so
 * a slow frame never holds up the window; what it learns is posted to the
 * view's thread and shows up here as properties. */

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <optional>
#include <map>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/buttons.hpp"
#include "chimera/vm/tr.hpp"
#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/sound.hpp"
#include "chimera/vm/input_log.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/view_model.hpp"

struct ce_session;
struct ce_project;
struct ce_average_fps;

namespace chimera::vm {

/* the files given in a zip, each stored as it is */
std::vector<uint8_t> storedZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files);

/* A named frame of the movie: a place to come back to. keepState asks the
 * greenzone to keep that frame's state whatever it thins. */
struct Marker
{
	int64_t frame = 0;
	std::string text;
	bool keepState = true;
	bool operator==(const Marker &) const = default;
};

class SessionViewModel : public ViewModel
{
public:
	SessionViewModel(Dispatcher &dispatcher, InputLog &inputs);
	~SessionViewModel() override;

	Property<bool> isOpen{false};
	/* how many times a machine has been opened: a view that keeps anything
	 * of the last one knows to start again */
	Property<int64_t> opens{0};
	/* how many opens failed; the status says why (a run nobody watches ends on one) */
	Property<int64_t> openFailures{0};
	/* how many times the movie could not be played (an entry that does not read); the status says why */
	Property<int64_t> unplayableMovies{0};
	Property<bool> running{false};
	/* the frame the machine is about to run (frames 0..frame-1 have run) */
	Property<int64_t> frame{0};
	Property<double> fps{0.0};
	Property<std::string> status{tr("session.nothing_open")};
	Property<std::string> coreName;
	Property<std::string> systemName;
	/* the machine's id, as the core names it (Chimera's "Loaded core: name (id)") */
	Property<std::string> systemId;
	Property<int64_t> buttonCount{0};
	/* the gamepads in use, or why there are none */
	Property<std::string> gamepads{tr("session.no_gamepads_yet")};
	/* How fast the machine runs, as Chimera has it: a percent of its own
	 * rate (1 to 6400, in Chimera's steps), or as fast as it can
	 * (unthrottled); and its sound, on or off and its volume (0 to 100) */
	Property<int64_t> speedPercent{100};
	Property<bool> unthrottled{false};
	/* how often a frame is drawn while Turbo is held or the machine is
	 * unthrottled: 1 every frame, else one in N (2 to 10); one in three at
	 * first, as Chimera's (owner, 2026-10-24: the frame skip goes, and this
	 * is its one setting left) */
	Property<int64_t> fastDrawOneIn{3};
	/* what keeps the machine to its speed, as Chimera's Config > Speed/Skip:
	 * "clock" (its own rate, at the speed asked for), "audio" (the sound as
	 * it is heard), "vsync" (a frame each picture the window shows), or ""
	 * for nothing (as fast as it goes). Remembered */
	Property<std::string> throttle{"clock"};
	/* the window's pictures given to the screen at its refresh (Chimera's VSync Enabled; from the next start) */
	Property<bool> vsyncEnabled{true};
	/* a picture shown by the window (any thread): what the vsync throttle waits for */
	void vsyncTick();
	/* the vsync throttle is on (any thread): the window keeps drawing for it */
	bool wantsVsync() const { return throttleM_.load() == 2; }
	Property<bool> soundOn{true};
	Property<int64_t> volume{100};
	/* Chimera's fast forward runs at its alternate speed (400%), not as
	 * fast as it can; and its Speed/Skip: frames not drawn while running,
	 * 0 to 9 of every one drawn, only when behind when auto-minimized (as
	 * Chimera's, on by default); unthrottled draws one in four. Remembered */
	Property<int64_t> fastForwardPercent{400};
	/* Chimera's Sound window: the sound device ("" the system's), the buffer
	 * in milliseconds, the sound at the machine's own speed on or off, while
	 * fast forwarding on or off and its volume (a share of the volume), and
	 * frame advance muted; and the devices there are. Remembered */
	Property<std::string> soundDevice;
	Property<int64_t> soundBufferMs{100};
	Property<bool> soundNormalOn{true}, soundFastOn{true}, muteFrameAdvance{true};
	Property<int64_t> fastVolume{50};
	Property<std::string> soundDevices{"[]"};
	/* Chimera's greenzone budgets: what the history of states may keep in
	 * memory (MB, 64 at least), how close it stays behind the playhead (one
	 * frame in 1 to 32) and how many frames after a load show kept pictures
	 * (0 to 10); every project's, remembered. A project may ask for its own
	 * memory and closeness, kept in budgets.json beside its cache (a fact
	 * about this computer, not the project's file): projectBudgetMb and
	 * projectNearStride, 0 when it asks for nothing. What the machine open
	 * got: budgetMbApplied, nearStrideApplied. Each takes effect when a
	 * machine opens */
	Property<int64_t> greenzoneBudgetMb{4096}, greenzoneNearStride{4}, pictureSettleFrames{3};
	Property<int64_t> projectBudgetMb{0}, projectNearStride{0}, budgetMbApplied{0}, nearStrideApplied{0};
	/* Chimera's compile cache (docs/compile-cache.md): for a core that
	 * translates its machine's code (its package says "precompile"), what
	 * it compiled is kept between sessions in a folder for the game, named
	 * by the game's own SHA1 under compileCacheRoot; compileCacheDir is the
	 * open machine's, "" for none */
	Property<std::string> compileCacheRoot, compileCacheDir;
	static std::string compileDirFor(const std::string &root, bool compiles, const std::string &gameSha1);
	/* the volume a frame is heard at, by Chimera's rule: the volume when the
	 * sound is on; while fast forwarding a share of it, or none; none for a
	 * frame advanced by hand when that is muted */
	static int64_t frameVolume(bool soundOn, int64_t volume, bool fast, bool fastOn, int64_t fastVolume, bool advanced,
		bool muteAdvance);
	/* the size the machine's picture is shown at, at 1x, as Chimera's: its
	 * own height at the shape the machine says it is now, else its declared
	 * virtual size (a 640x400 picture declared 640x480 is shown 640x480,
	 * issue 414) */
	static void shownSize(ce_session *s, double &width, double &height);
	/* Chimera's Skip Lag Frames: a frame advance that lands after a lag
	 * frame runs on until one that is not (remembered) */
	Property<bool> skipLagFrames{false};
	/* Chimera's Display Nothing: the frames are run without being drawn, and
	 * the speed goes to the window's title (remembered) */
	Property<bool> displayNothing{false};
	/* Chimera's background input: a gamepad is heard while the window is
	 * not the one in front (remembered); windowActive says whether it is */
	Property<bool> acceptBackgroundInput{false};
	Property<bool> windowActive{true};
	/* What Chimera shows over the picture, each on or off (remembered): the
	 * speed ("60 fps", with >> fast forwarding and << rewinding), the frame
	 * counter (the frame, then the last frame with input, "(Finished)" past
	 * it), the lag counter (lag frames so far, and whether the frame just run
	 * was one), and the input of the frame just run, a mnemonic a button
	 * pressed, player by player and the console's last, as Chimera writes it */
	Property<bool> showFps{false}, showFrameCounter{false}, showLagCounter{false}, showInput{false};
	Property<std::string> fpsText{"0 fps"}, frameCounterText, inputText;
	Property<int64_t> lagCount{0};
	Property<bool> lagFrame{false};
	/* Chimera's View menu, the rest: the average speed since it was turned
	 * on or reset (counted by the engine: frames run freely over the time
	 * they took; "avg 59.9 fps", "" until something counted), shown on the
	 * speed's line; the rerecord count, 56 down; messages over the picture
	 * (the window shows each status a moment), and the status bar */
	Property<bool> showAverageFps{false}, showRerecords{false}, showMessages{true}, showStatusBar{true};
	/* Chimera's File > Screenshot > Capture OSD: a screenshot has what is drawn over the picture too */
	Property<bool> captureOsd{false};
	/* Chimera's Capture Mouse (remembered) and whether anything is bound to
	 * the pointer to capture it for: an analog control bound to WMouse X or
	 * Y (where it is) or RMouse X or Y (how far it moved), as Chimera's
	 * Controller.IsMouseBound */
	Property<bool> captureMouse{false};
	Property<bool> mouseBound{false};
	Property<std::string> averageFpsText;
	/* Chimera's Emulator menu ("Game" for a game core) and what it offers
	 * for the core open: a game core's own timer, shown at the top right 14
	 * down ("IGT 01:02.345"); the user's save data, written out; a movie
	 * made elsewhere, imported */
	Property<bool> isGameCore{false}, hasGameTimer{false}, hasSaveData{false}, hasMovieImport{false};
	/* the core's tooling groups (the engine's): a trace of what its CPU runs,
	 * and registers a debugger shows; the trace's own header */
	Property<bool> hasTrace{false}, hasRegisters{false};
	Property<std::string> traceHeader;
	/* Tracing, as Chimera's trace logger asks it: on, the core's lines of
	 * each frame run are handed to `sink` on the window's thread ("[core:
	 * trace buffer full, lines dropped]" when it overflowed); off, the core
	 * traces nothing. Kept across machines and state loads */
	using TraceSink = std::function<void(std::vector<std::string> lines)>;
	void setTracing(bool on, TraceSink sink);
	/* the registers now, by the machine (the debugger's box): name, value, bits */
	struct Register
	{
		std::string name;
		int64_t value = 0;
		int32_t bits = 32;
	};
	std::vector<Register> readRegisters(int64_t &cycles, bool &hasCycles);
	/* a register set, by its place; false when the core does not take it */
	bool writeRegister(int32_t index, int64_t value);
	/* the pictures the core draws for itself (the Surface Viewer): their
	 * names and sizes, as [{"name", "width", "height"}], and one drawn now
	 * into a picture; false when there is none */
	Property<std::string> surfaces{"[]"};
	bool renderSurface(int32_t index, Picture &into);
	Property<bool> showGameTime{false};
	Property<std::string> gameTimeText;
	/* Export Core Log (Help > Export Core Log...): what the cores write, kept in a
	 * file only when asked and never remembered (each run starts with it
	 * off; the engine keeps it, ce_core_log). The file while it is on, ""
	 * while it is off */
	Property<std::string> coreLogPath;
	/* the input of a frame as the input display writes it */
	std::string inputDisplay(int64_t frame) const;
	/* Chimera's autohold and autofire: while one is armed (its hotkey held),
	 * a button newly pressed - a key's or a pad's - toggles being held, or
	 * firing: on for autofireOn frames, off for autofireOff, lag frames not
	 * counted unless autofireSkipsLag is off. Both last until cleared, and go
	 * into the frames the machine runs as a press held does. */
	Property<std::string> autoheld, autofired;
	/* as Chimera shows them while arming: "Held: P1 A Auto-P1 B " */
	Property<std::string> stickyText{tr("session.held", {""})};
	Property<bool> stickyArmed{false};
	Property<int64_t> autofireOn{1}, autofireOff{1};
	Property<bool> autofireSkipsLag{true};
	void armAutohold(bool down);
	void armAutofire(bool down);
	/* A gamepad input caught for a binding (catchPad): waiting, and the
	 * name of the input pressed ("X1 A", "J1 POV0U"; "" while waiting) */
	Property<bool> catchingPad{false};
	Property<std::string> caughtPad;
	/* "Sound on", or why there is none */
	Property<std::string> sound{tr("session.no_sound_yet")};
	/* sample pairs sent to the speakers, and those dropped for running ahead */
	Property<int64_t> soundQueued{0};
	Property<int64_t> soundDropped{0};
	/* frames before this one were run with the inputs they have now */
	Property<int64_t> greenEnd{0};
	/* machine states the greenzone holds */
	Property<int64_t> greenzoneStates{0};
	/* how many times the movie was changed over work already done, saved
	 * with the project */
	Property<int64_t> rerecords{0};
	/* after an edit before the current frame, go back to the edit and run
	 * forward to where the machine was, with the new inputs; off until
	 * asked, as Chimera's TAStudio had it (issue 426) */
	Property<bool> autoRestore{false};
	/* TAStudio's Turbo seek, as Chimera's (off at first): on, a seek goes to
	 * its frame at once, drawing nothing on the way; off, what the greenzone
	 * holds is gone to at once and the rest is played at the machine's own
	 * speed, every frame drawn, stopping on the frame */
	Property<bool> turboSeek{false};
	/* rewind or turbo held */
	Property<bool> rewinding{false}, turbo{false};
	/* the machine's drives, for the status bar's lights, as Chimera's: a JSON array
	 * of {name, on, short, long}, the image in each drive and the one its
	 * selector is on said shortly (short) and in full (long); [] for none */
	Property<std::string> drives{"[]"};
	/* the frame the machine goes back to after edits (Chimera's green arrow), -1 for none */
	Property<int64_t> restoreFrame{-1};
	/* what the status bar says of a drive that swaps, by Chimera's rule:
	 * shortly ("1/2 Disc 1.iso | selected 2/2 Disc 2.iso"), and in full, led by the
	 * drive's name, for its tooltip; inserted -1 is an empty drive */
	static std::string driveMediaShort(const std::vector<std::string> &names, int32_t selected, int32_t inserted);
	static std::string driveMediaLong(const std::string &drive, const std::vector<std::string> &names, int32_t selected, int32_t inserted);
	/* a seek that plays: the frame it goes to (-1 when none is) and where it set off */
	Property<int64_t> seekingTo{-1};
	Property<int64_t> seekFrom{-1};
	/* TAStudio's recording mode (Chimera's "Recording mode" box and its
	 * "Toggle read-only" hotkey). Off, the machine plays the movie as it
	 * is, and past its end only autohold, autofire and the Auto-hold columns
	 * extend it; on, each frame the machine plays with the person's input
	 * becomes what is held. Off whenever a project opens, as Chimera's
	 * TAStudio starts read only. */
	Property<bool> recordingMode{false};

	/* Frozen addresses, Chimera's freezes: values the engine writes before
	 * every frame, its replays included, for this session only - never
	 * saved, never in a movie (the owner, 2026-10-24: they are for looking
	 * into a game, not for making a movie). A domain's bytes, or a game
	 * property's element by name. As Chimera's, they stay while the same
	 * game runs again (a restart, a project of it) and go for another game,
	 * and they are switched off when the machine stops. */
	struct Freeze
	{
		std::string domain;
		int64_t address = 0;
		int32_t size = 1;
		bool bigEndian = false;
		uint64_t value = 0;
		/* a game property instead, by its name as a watch has it (an
		 * element's included), set from text */
		std::string property;
		std::string text;
		bool enabled = true;
	};
	const std::vector<Freeze> &freezes() const { return freezes_; }
	/* an enabled freeze writes this address of the domain */
	bool frozen(const std::string &domain, int64_t address) const;
	bool propertyFrozen(const std::string &name) const;
	/* added, or put in place of the one at the same address or element; this
	 * waits for the machine, and says why when it refuses */
	bool freeze(const Freeze &f, std::string &error);
	/* every freeze writing this address, or on this element, goes */
	void unfreeze(const std::string &domain, int64_t address);
	void unfreezeProperty(const std::string &name);
	void unfreezeAll();
	/* the freezes on: Chimera's status bar shows its icon then */
	Property<int64_t> frozenCount{0};
	/* the core stopped: the frame it stopped on
	 * (-1 while it runs), what the sandbox said, the newest state at or
	 * before it (-1: none), whether frame 0 is still held, and how many
	 * frozen addresses were switched off; asked until stoppedChoice answers */
	Property<int64_t> stoppedAt{-1};
	Property<std::string> stoppedReason;
	Property<int64_t> stoppedSafePoint{-1};
	Property<bool> stoppedCanRestart{false};
	Property<int64_t> stoppedFreezesOff{0};
	Signal<> freezesChanged;
	/* which frames the greenzone stores: 1 every frame, N one in N, 0 none
	 * (TAStudio's Greenzone box; saved with the project, 1 for one saved
	 * before it was), and the N the "every N frames" choice uses */
	Property<int64_t> greenzonePeriod{1};
	Property<int64_t> greenzoneEvery{2};
	/* how long the last seek took, in milliseconds, and how many frames it ran */
	Property<double> lastSeekMs{0.0};
	Property<int64_t> lastSeekFrames{0};
	/* the open project's file, "" while it has not been saved yet */
	Property<std::string> projectPath;
	Property<bool> hasProject{false};
	/* the open project's id (its cache folder's name), "" for none */
	Property<std::string> projectId;
	/* a project waiting for its files to be found: its files are the
	 * "projectFiles" rows, located one by one or by scanning a folder */
	Property<bool> resolvingFiles{false};
	/* Work a crash left behind: while a project
	 * has unsaved changes it is kept every five seconds in recoveryFolder,
	 * a folder per project, and let go when it is saved or closed cleanly.
	 * A project with no file yet has nowhere to be offered back, and is not
	 * kept.
	 * Found when the project opens again, it is rebuilt as a copy among the
	 * backups and offered: recoveredCopy names it ("" when nothing is
	 * offered), recoveredUpTo says when its last work was, and the open
	 * waits for the answer (recoverWork) */
	Property<std::string> recoveryFolder, recoveredCopy, recoveredUpTo;
	/* where crash notes are (docs/project.md, "Crash notes"), and, beside
	 * recovered work, what ended the session that left it and its note
	 * ("" when no note says) */
	Property<std::string> crashFolder, recoveredCrash, recoveredCrashNote;
	struct ProjectFile
	{
		std::string name, slot, sha1, actualSha1;
		/* 0 found and matching, 1 not found, 2 found but not the recorded bytes */
		int32_t status = 1;
		/* firmware the project needs, listed after its files (issue 397):
		 * its name, "firmware" as its slot, the hash it must have, if any */
		bool firmware = false;
	};
	const std::vector<ProjectFile> &projectFiles() const { return projectFiles_; }
	Property<std::string> projectTitle;
	/* the game's name: its file's, without the kind (a project's first file) */
	Property<std::string> gameName;
	/* where a project's backups go (Chimera's "Movie backups" path) */
	Property<std::string> backupFolder;
	/* projects opened, newest first, a line each (ten kept) */
	Property<std::string> recentProjects;
	/* the open project was changed since it was opened or last saved: its
	 * inputs, markers, branches or details (Chimera asks before closing it) */
	Property<bool> unsaved{false};
	/* TAStudio's settings for playing and saving: pause on the frame the
	 * movie runs out; how many frames a step of rewind goes back, and
	 * while fast forwarding; save the project by itself every so many
	 * seconds when it has changes (0 never), as a backup instead if asked;
	 * and a backup as well every time it is saved by hand */
	Property<bool> pauseAtEnd{false};
	Property<int64_t> rewindStep{1};
	Property<int64_t> rewindStepFast{4};
	Property<int64_t> autosaveSeconds{1800};
	Property<bool> autosaveAsBackup{false};
	Property<bool> backupOnSave{false};

	Picture screen;
	Signal<> screenChanged;

	/* the machine's buttons, and the one-letter name a movie gives each */
	const std::vector<std::string> &buttonNames() const { return buttonNames_; }
	/* the machine's analog controls, as its package declares them */
	struct Axis
	{
		std::string name, header;
		int64_t min = -32768, max = 32767, neutral = 0;
		/* its player, 0 the console's, as the core declares */
		int32_t player = 0;
	};
	const std::vector<Axis> &axes() const { return axes_; }
	/* each button's player (0 the console's), as the core declares it
	 * (ce_session_player_of), in the buttons' order */
	const std::vector<int32_t> &buttonPlayers() const { return buttonPlayers_; }
	/* a control's player by its name, 0 for the console's or one the machine lacks */
	int32_t playerOf(const std::string &name) const;
	/* the controller's name as the package declares it (Chimera's
	 * ControllerDefinition name; "Waterbox Controller" when it says none) */
	const std::string &controllerName() const { return controllerName_; }
	/* the picture of the controller the package ships (a PNG's bytes; "" for none) */
	const std::string &controllerPicture() const { return controllerPicture_; }
	/* the package the machine open runs ("" for none) */
	const std::string &packagePath() const { return packagePath_; }
	/* an input as Chimera's input display writes it: for each player in turn
	 * (1, 2... and the console's last), its buttons - the mnemonic of one
	 * pressed, a space for one not - then its analog controls, five places
	 * and a comma off neutral, six spaces at it */
	static std::string inputDisplay(const std::vector<std::string> &buttons, const std::string &mnemonics,
		const std::vector<Axis> &axes, const Buttons &pressed, const std::vector<int32_t> &values);
	/* each button's keys ("Down, J1 POV0D"), by index: as they are, as the
	 * package has them, and given again (the view's thread) */
	const std::vector<std::string> &bindingTexts() const { return bindings_.texts(); }
	/* the autofire keys of each button (the package's, and as set), and the analog controls' bindings */
	const std::vector<std::string> &defaultAutofireTexts() const { return defaultAutofire_; }
	const std::vector<std::string> &autofireTexts() const { return autofireBindings_.texts(); }
	void setAutofireTexts(std::vector<std::string> texts);
	const std::vector<AnalogBind> &defaultAnalogBinds() const { return defaultAnalog_; }
	const std::vector<AnalogBind> &analogBinds() const { return analogBinds_; }
	void setAnalogBinds(std::vector<AnalogBind> binds);
	/* how hard the pointer's movement pushes an analog control, in percent */
	void setMouseSensitivity(int64_t percent);
	/* each pad axis now, by Chimera's names, -10000..10000 (catching an analog binding) */
	std::vector<std::pair<std::string, int>> padAxesNow() const;
	const std::vector<std::string> &defaultBindingTexts() const { return defaultBindings_; }
	void setBindingTexts(std::vector<std::string> texts);
	const std::string &mnemonics() const { return mnemonics_; }
	/* what heads each button's column in TAStudio: the package's header, else its letter */
	const std::vector<std::string> &buttonHeaders() const { return buttonHeaders_; }
	Signal<> buttonsChanged;

	/* the inputs of frames first..last were changed, or frames were added
	 * at the end, by the machine (recording what was held) */
	Signal<int64_t, int64_t> inputsChanged;
	Signal<int64_t, int64_t> inputsAppended;
	/* frames the person's input wrote, in order: as each was and as it
	 * became (appended: past the end), and whether the machine was running
	 * then (Chimera's undo history keeps a run's frames as one step) */
	struct RecordedFrame
	{
		int64_t frame = 0, length = 0;
		bool appended = false, running = false;
		Buttons before, after;
		std::vector<int32_t> axesBefore, axesAfter;
	};
	Signal<std::vector<RecordedFrame>> framesRecorded;
	/* the movie changed from this frame on, so states after it may no longer hold (Lua's
	 * tastudio.ongreenzoneinvalidated): every edit, and every frame recorded */
	Signal<int64_t> greenzoneInvalidated;
	/* every frame's inputs were replaced (a project opened) */
	Signal<> inputsReset;
	/* a project opened with these markers */
	Signal<std::vector<Marker>> markersLoaded;
	/* the piano roll's hidden columns, by control name, as the project (or
	 * Chimera's defaults, for one without its own) has them */
	Signal<std::vector<std::string>> hiddenColumnsLoaded;
	/* the hidden columns now, to save with the project in Chimera's shape */
	void setHiddenColumns(const std::vector<std::string> &names);
	/* TAStudio's other settings kept in a project (members of its TAStudio
	 * part, as Chimera does: "BoolPatterns"...): each as
	 * JSON text, when a project opens; and given back, to be saved with it
	 * (a member not given is saved as the project had it) */
	Signal<std::map<std::string, std::string>> tastudioLoaded;
	void setTastudioMember(const std::string &name, const std::string &json);
	/* The piano roll's Auto-hold columns (Chimera's column "emphasis"), by
	 * name: as the project had them, and the ones now, to save with it */
	Signal<std::vector<std::string>> autoHeldColumnsLoaded;
	void setAutoHeldColumns(const std::vector<std::string> &names);
	/* What each Auto-hold column presses while the game plays, on the UI's
	 * thread: its values in turn (1 or 0 for a button, the value for an
	 * analog control), back to `loop` after the last, a lag frame not
	 * counted when skipLag; a button or an analog control by its index */
	struct ColumnPattern
	{
		bool isAxis = false;
		int32_t index = 0;
		std::vector<int32_t> values;
		bool skipLag = true;
		int64_t loop = 0;
	};
	void setColumnPatterns(std::vector<ColumnPattern> patterns);
	/* Which frames the greenzone holds a state for, of those the piano
	 * roll shows: the UI's thread says which it shows, the machine answers
	 * with every report; hasState reads the last answer (false for a frame
	 * outside it), and statesShown says when it changed */
	void watchStates(int64_t first, int64_t count);
	bool hasState(int64_t frame) const;
	Signal<> statesShown;
	/* the controls Chimera's piano roll hides until shown (the console's
	 * power and reset, keyboard keys, disk and tape controls...) */
	static bool hiddenByDefault(const std::string &name);
	/* the movie's markers, from whoever edits them: saved with the project,
	 * and the frames of those that keep a state pinned in the greenzone */
	void setMarkers(std::vector<Marker> markers);
	/* the gamepads' buttons held at the last frame, by Chimera's names (any thread) */
	std::vector<std::string> padsHeld() const
	{
		std::lock_guard<std::mutex> lock(padHeldCopyM_);
		return padHeldCopy_;
	}
	/* the run's own markers' frames: their states are kept as a marker's are, never saved */
	void setRunMarkers(std::vector<int64_t> frames);

	/* A branch: another version of the movie, kept to come back to. */
	struct Branch
	{
		std::string name, time;
		int64_t frame = 0;
		/* the movie, as the engine writes it, and how many frames it has */
		std::string text;
		int64_t frames = 0;
		std::vector<Marker> markers;
		/* its state's file name in the project's cache folder, "" when it
		 * has none (it is then replayed to), and the picture it had */
		std::string state;
		std::shared_ptr<const Picture::Frame> picture;
		/* which branch this is while the window is open (not saved) */
		uint64_t id = 0;
	};
	/* a project opened with these branches */
	Signal<std::vector<Branch>> branchesLoaded;
	/* the branches to save with the project */
	void setBranches(std::vector<Branch> branches);
	/* another movie becomes the machine's, and the machine goes to `frame`:
	 * the engine keeps the states before the first frame where the two
	 * differ, and replays from there */
	void loadMovie(std::string text, int64_t frame);
	/* A branch of the movie as it is, made on the machine's thread: its
	 * frame, movie and picture, and with a project open its state kept in a
	 * file in the cache folder. done gets it on the view's thread, with why
	 * its state could not be kept ("" when it was, or there is no project). */
	void makeBranch(std::string name, std::string time, std::function<void(Branch, std::string why)> done);
	/* A branch put back: its movie, and the machine on its frame - by its
	 * state file when it has one this machine can take, else by replaying
	 * from where the two movies differ. done says which, on the view's
	 * thread, with why a state it had could not be loaded. */
	void loadBranch(Branch branch, std::function<void(bool fromState, std::string why)> done);
	/* a branch is gone, and its state file with it */
	void forgetBranch(Branch branch);

	/* where what a project can make again is kept (its branches' states), a
	 * folder per project; "" keeps nothing */
	Property<std::string> cacheFolder;

	/* Where packages and firmware are found (the cores view model's), asked
	 * on the machine's thread: a project that names no package runs the one
	 * found for its core, and every machine gets the firmware it needs. */
	using PackageFinder = std::function<std::string(const std::string &coreName, const std::string &sha1, std::string &note)>;
	using FolderFinder = std::function<std::vector<std::string>()>;
	void setFinders(PackageFinder packages, FolderFinder firmwareFolders);
	/* Chimera's --firmware <id>=<path>: files for this run, ahead of what is
	 * remembered - used by its hash where the project pins one, else as given */
	void setRunFirmware(std::map<std::string, std::string> byId);
	/* a firmware file located for a waiting project, remembered for its core
	 * as New Project remembers one (the cores view model's); on the window's thread */
	using FirmwareKeeper = std::function<void(const std::string &core, const std::string &id, const std::string &path)>;
	void setFirmwareKeeper(FirmwareKeeper keep) { keepFirmware_ = std::move(keep); }

	/* for the window: a key went down or up, by Chimera's key names */
	void key(const std::string &name, bool down);

	/* Held keys, as Chimera's hotkeys work: frame advance runs a frame, and
	 * held half a second runs on until let go; rewind goes a frame back, and
	 * held goes on back a frame a frame; fast forward runs as fast as the
	 * machine can while held. */
	void holdFrameAdvance(bool down);
	void holdRewind(bool down);
	/* The UI's thread: another view model changed the open project (its
	 * markers, branches or details); it is unsaved until saved */
	void markChanged();
	/* The UI's thread: the buttons and analog controls a key or pad held may
	 * write into the movie - the piano roll's columns shown, when hidden ones
	 * are not to be edited, as Chimera records (nullopt and axes empty: all) */
	void setRecordMask(std::optional<Buttons> shown, std::vector<bool> axes = {});
	void holdFastForward(bool down);
	/* whether a key presses any of the machine's buttons */
	bool keyIsBound(const std::string &name) const { return !bindings_.buttonsOf(name).empty(); }
	/* Chimera's Turbo, held: fast forward with nothing drawn or heard */
	void holdTurbo(bool down);
	bool turboHeld() const { return turboHeld_; }

	/* the inputs of frame `first` (and maybe later ones) were edited: the
	 * machine's states after it no longer hold */
	void edited(int64_t first);

	/* A memory domain's bytes, read on the machine's thread; this waits for
	 * it. Empty when there is no such domain or no machine. For tools and
	 * tests, not for every frame. */
	std::vector<uint8_t> readDomain(const std::string &name);

	/* On the machine's thread: the machine was changed by hand (a poke) at
	 * the frame it is on, so the stored states after it no longer hold */
	void dropStatesAfterOnMachine(int64_t frame);

	/* For other view models (RAM Search, RAM Watch, a hex editor) whose
	 * work is the machine's: run it on the machine's thread, with the open
	 * session or nullptr. */
	using MachineJob = std::function<void(ce_session *)>;
	void onMachine(MachineJob job);
	/* the same, and waits for it */
	void onMachineWait(MachineJob job);
	/* the open project, on the machine's thread (nullptr for none), waited
	 * for: what is changed in it is saved with it */
	using ProjectJob = std::function<void(ce_project *)>;
	void onProjectWait(ProjectJob job);

	/* Scripts run on the machine's thread, before and after every frame, as
	 * Chimera's do, and reach the window's view models by waiting on the
	 * window (owner, 2026-10-24). These are the two waits. Each thread, while
	 * it waits on the other, does what the other waits on it for, so that
	 * neither waits for ever: the machine's thread does the jobs a thread
	 * waits on (onMachineWait and the rest), the window's thread the jobs the
	 * machine's thread waits on.
	 * onMachineThreadWait: job on the machine's thread, waited for (at once
	 * when called there). onWindowWait: from the machine's thread, job on the
	 * window's thread, waited for (at once when called there); false, and
	 * the job not run, when the program is closing. */
	bool onMachineThread() const { return std::this_thread::get_id() == machineThread_; }
	bool onMachineThreadWait(const std::function<void()> &job);
	bool onWindowWait(const std::function<void()> &job);
	/* the scripts' turns, on the machine's thread: `before` before every
	 * frame played with the person's input (what a script holds there is that
	 * frame's), `after` after it and after the machine was moved another way
	 * (a seek lands, a state is loaded). Waits until the machine's thread has
	 * them; empty functions let go of the old ones. While turbo is held they
	 * are left out unless scriptsDuringTurbo (Customize's Run Lua scripts
	 * when turbo is held). */
	void setScriptTurns(std::function<void()> before, std::function<void()> after);
	/* The scripts' turns when no frame is run - paused, or nothing open -
	 * about sixty times a second while `wanted` says any script is there, as
	 * Chimera's loop resumes scripts every pass: `idle` (those that wait in
	 * emu.yield go on). With nothing open and not paused the turns are whole
	 * frames' (before and after), as Chimera's empty machine still runs
	 * frames. Both are called on the machine's thread. */
	void setScriptIdle(std::function<bool()> wanted, std::function<void()> idle);
	/* Chimera's client.show_future: after each frame played, frames are
	 * played ahead - the movie's input, none past its end - while `turn`,
	 * asked with how many were played so far, says false (setFutureFrames'
	 * most at the most); the last one's picture is shown and the machine is
	 * put back as it was. On the machine's thread. */
	void setFutureTurn(std::function<bool(int64_t played)> turn);
	/* the scripts' functions called at each read of the controls a core
	 * reports, part way through a frame (event.oninputpoll, issue 344): on
	 * the machine's thread, inside the frame */
	void setInputPollTurn(std::function<void()> turn);
	void scriptsDuringTurbo(bool on) { scriptsDuringTurboM_.store(on); }
	/* the machine was opened by a script (client.reboot_core, or
	 * client.openproject): the scripts go on, rather than start again as
	 * they do when a person opens one (asked once when it opens: the answer
	 * is taken) */
	bool takeScriptReboot() { return scriptRebootM_.exchange(false); }
	/* no frame runs while held: a script named on the command line starts
	 * before the machine's first frame, as Chimera's does */
	void holdFramesForScript(bool hold)
	{
		holdForScriptM_.store(hold);
		cv_.notify_all();
	}
	bool scriptRebooting() const { return scriptRebootM_.load(); }
	/* What made the project last opened, kept after it closes, for New
	 * Project to open on as Chimera's does (closing a project is most often
	 * the step before changing one setting and going again): its core and
	 * the package it ran with, its settings, and where each of its files was
	 * found, as {slot, path}. Paths are this session's knowledge: a project
	 * carries names and hashes only. The window's thread. */
	struct ProjectAnswers
	{
		std::string coreName, package, settings;
		std::vector<std::pair<std::string, std::string>> files;
	};
	const ProjectAnswers &lastAnswers() const { return lastAnswers_; }
	/* From a job on the machine's thread: opens a project made in memory
	 * and never written (New Project's), taking it over. It has no file
	 * until it is first saved, which is when it is asked where it goes. */
	void openMadeProjectOnMachine(ce_project *p, const std::string &package);
	/* frame runs after every frame (seeks' too), closing just before a
	 * machine goes (its memory with it). removeMachineHooks waits until the
	 * machine's thread has let go of them. */
	int addMachineHooks(MachineJob frame, MachineJob closing);
	/* the machine's thread, in a frame hook: whether the frame just run was a lag frame */
	bool lastFrameLagOnMachine() const { return lastLagM_; }
	void removeMachineHooks(int id);

	/* Chimera's encode borrows the machine: it is wound to frame `from` and
	 * plays the movie read only to `to`, both included, as fast as it goes,
	 * every frame drawn and handed to `frame` on the machine's thread (false,
	 * with why, stops it), no state kept on the way; then `finishing` (on the
	 * machine's thread: the file closed), and the machine given back on the
	 * frame it was on, its picture, running or paused as it was. `done`
	 * comes on the view's thread: 0 every frame written, 1 stopped, 2 failed,
	 * with why. Nothing else moves the machine meanwhile. */
	using BorrowFrame = std::function<bool(ce_session *, std::string &why)>;
	void borrowMachine(int64_t from, int64_t to, BorrowFrame frame, std::function<void()> finishing,
		std::function<void(int, std::string)> done);
	/* the borrowed run ends after the frame it is on */
	void stopBorrow() { borrowStopM_.store(true); }
	/* frames run since the borrowed run reached `from`, for its progress */
	int64_t borrowedFrames() const { return borrowFramesM_.load(); }

	/* Settings, for the settings view model. On the machine's thread: what
	 * the machine was opened with (a JSON object; the project's own when
	 * a project is open), and the slot map the settings' conditions read.
	 * Changing them is tried live; a core that cannot take them live is
	 * opened again with them, at frame 0, its inputs kept. The answer
	 * comes back on the view's thread: true when applied, with what
	 * happened. */
	std::string settingsOnMachine() const;
	std::string packageOnMachine() const { return package_; }
	std::string slotsOnMachine() const;
	void changeSettings(std::string json, std::function<void(bool, std::string)> done);
	/* The open project's settings and the start of its machine's clock, changed
	 * (Chimera's File > Project Settings, issue 370): they become the project's,
	 * and the machine starts again with them at frame 0, the inputs kept; a
	 * greenzone kept with the old ones is not used. When it does not start, the
	 * old ones are put back and it starts with those; done(false) then. */
	void changeProjectSettings(std::string json, int64_t clockStart, std::function<void(bool)> done);
	/* The MCP server (Tools > MCP Server..., docs/mcp-server.md). While it is
	 * started the machine's thread runs the agent's calls between frames; the
	 * engine answers what needs only the machine and hands the rest to `host`,
	 * on the window's thread, whose answer comes back through mcpAnswer.
	 * Nothing else of the agent's runs until a call is answered. */
	using McpHost = std::function<void(uint64_t id, std::string tool, std::string arguments)>;
	void setMcpHost(McpHost host) { mcpHost_ = std::move(host); }
	void mcpServing(bool on);
	void mcpAnswer(uint64_t id, std::string text, bool isError);
	/* answers with whether the machine is paused and its frame, once the jobs before are done */
	void mcpAnswerRunState(uint64_t id);
	/* create_project's answer, once the project made before it has opened:
	 * what get_status says of the window, with the core, the system, the
	 * frame and the movie's length; or that its machine did not start */
	void mcpAnswerOpened(uint64_t id);
	/* seek: the machine goes to the frame as TAStudio sends it; answered when it stops */
	void mcpSeek(uint64_t id, int64_t frame);
	/* advance_until: the condition the engine reads from the arguments; played
	 * forward until it is met, or for at most this many frames, or to the movie's end */
	void mcpPlayUntil(uint64_t id, std::string arguments, int64_t most);
	/* capture_series: each frame gone to in turn and its picture taken */
	void mcpPictures(uint64_t id, std::vector<int64_t> frames);
	/* the machine's thread: the start of the open project's clock */
	int64_t clockOnMachine() const;

private:
	using Clock = std::chrono::steady_clock;

	void submit(std::function<void()> job);
	void loop();
	std::string movieTextOnMachine() const;
	/* the movie replaced by this one; where the two part, or -1 */
	int64_t replaceMovieOnMachine(const std::string &text);
	/* the open project's folder in the cache, and its branch states' folder
	 * in that; empty when there is no project or no cache */
	std::filesystem::path projectCacheOnMachine() const;
	std::filesystem::path branchDirOnMachine() const;
	/* A project's session starts: the greenzone the last one kept is
	 * loaded when that session ended cleanly (not on a restart, whose
	 * movie may have been edited since the save), and the session is
	 * marked as running until it ends. */
	void startProjectSessionOnMachine(bool loadHistory);
	/* the greenzone kept with the project as it is saved, when it may
	 * outlive the session: written in the background */
	void keepHistoryOnMachine();
	/* a project just saved under a name of its own, with an id of its own:
	 * what its old id's cache held that a save does not write again comes
	 * along, and the work kept against a crash under the old id goes */
	void becameAnotherOnMachine(const std::filesystem::path &cacheWas, const std::filesystem::path &recoveryWas);
	/* who the project's cache belongs to, beside it ("about.json", as
	 * Chimera's project cache writes it: title, the project's path, core,
	 * system, games), so the Cache Manager can name it */
	void rememberAboutOnMachine(const std::string &projectPath);
	/* which machine this is, as a state of it needs: core, settings, files */
	std::string machineIdOnMachine();
	/* the state files no branch of the project has any more go */
	void tidyBranchStatesOnMachine();
	/* a project opened from another path than its cache last knew it at:
	 * the greenzone and the branches' states kept there go */
	void forgetElsewhereCacheOnMachine(const std::string &path);
	static void loadMovieOnMachine(ce_session *s, const std::string &text);
	void openProjectOnMachine(const std::string &path, const std::string &package, bool keepInputs,
		ce_project *reuse = nullptr);
	/* the rest of opening a project, once its files are found; extraFlags
	 * lets a file that is not the recorded one be opened anyway */
	void bootProjectOnMachine(ce_project *p, const std::string &path, const std::string &packageGiven, bool keepInputs,
		ce_project *reuse, const std::string &kept, uint64_t keptRerecords, uint32_t extraFlags);
	/* a project waiting for its files to be found (Chimera's resolution
	 * window): what opening it was asked with */
	struct PendingOpen
	{
		ce_project *project = nullptr;
		std::string path, package;
		bool keepInputs = false;
		ce_project *reuse = nullptr;
		std::string kept;
		uint64_t keptRerecords = 0;
		/* the work a crash left, opened instead: unsaved once open */
		bool recovered = false;
	};
	/* the files looked for, then the rest of opening (or the project waits for its files) */
	void findFilesAndBootOnMachine(PendingOpen open);
	/* the project waits for files to be located (Locate Project Files), saying why */
	void waitForFilesOnMachine(PendingOpen open, const std::string &said);
	bool recoveredM_ = false;
	std::string recoveryFolderM_, backupFolderM_, crashFolderM_;
	std::filesystem::path recoveryDirOf(ce_project *p) const;
	/* the work kept for a crash, written now (quietly) or let go */
	void keepRecoveryOnMachine();
	void dropRecoveryOnMachine(const std::filesystem::path &dir);
	/* a crash's work for this project, rebuilt as a copy among the backups;
	 * the copy's path and the time of its last work, or false for none */
	bool recoveredCopyOnMachine(ce_project *p, const std::string &projectPath, std::string &copy, std::string &upTo,
		std::string &crash, std::string &crashNote);
	Clock::time_point recoveryDue_{};
	/* the journal beside the snapshot (work.journal, as Chimera's): the
	 * engine writes every change to the movie into it as it is made, and the
	 * markers and branches follow as records of their own (Chimera's M, B
	 * and O). Opened, or written again from the start, by journalOnMachine;
	 * what the records last said, so that only a change is written. */
	void journalOnMachine();
	void journalMarksOnMachine();
	Clock::time_point journalRetryDue_{};
	uintmax_t journalImageBytes_ = 0;
	std::string journaledMarkers_, journaledOrder_;
	std::map<std::string, std::string> journaledBranches_;
	Connection recoveryWatch_, backupWatch_, crashWatch_;
	PendingOpen pendingOpenM_;
	void publishProjectFilesOnMachine();
	/* where this machine found a project's files, kept in its cache folder
	 * (local-paths.json, as Chimera keeps them): read
	 * as hints, each checked by its hash; written once they are found */
	void applyLocalPathsOnMachine(ce_project *p);
	void rememberLocalPathsOnMachine();
	std::filesystem::path cacheDirOf(ce_project *p) const;
	/* the project's file, first in the recent projects */
	void addRecent(const std::string &path);
	/* a backup: written there, the project's own file and greenzone left as they are */
	/* true when written */
	bool saveProjectOnMachine(const std::string &path, bool backup = false, bool quiet = false);
	/* the UI's thread: the project saved to its own file (or the one
	 * given) and, to back it up, a backup named for it and the time */
	void saveTo(const std::string &path);
	std::string backupPathFor(const std::string &projectFile) const;
	/* the changes counted since the project was opened; the count a save
	 * covered, once it is written */
	uint64_t changeGen_ = 0;
	void savedAt(uint64_t gen);
	void projectSwitched();
	void autosave();
	std::atomic<bool> unsavedM_{false}, pauseAtEndM_{false};
	/* the buttons recording may change (Chimera's "allow editing hidden
	 * columns" off): all, or only these */
	std::atomic<bool> recordAllM_{true};
	SharedButtons recordShownM_;
	std::vector<bool> recordAxesM_;
	std::atomic<bool> recordingModeM_{false};
	std::vector<int32_t> loggedAxes_;
	/* a frame's buttons as the engine reads and writes them, a byte each */
	std::vector<uint8_t> statesBuf_;
	Connection recordingWatch_;
	std::atomic<int64_t> rewindStepM_{1}, rewindStepFastM_{4}, autosaveMsM_{1800000};
	Clock::time_point autosaveDue_{};
	/* the last frame run was not the movie's end (pausing there is once) */
	bool doPauseM_ = false;
	Connection pauseWatch_, rewindWatch_, rewindFastWatch_, autosaveWatch_;
	/* Chimera's state history check: from frame 0, at each frame the
	 * history holds, the machine saved, put back from the history and saved
	 * again, the two compared; the first that differ said, and written to
	 * `keepIn` when one is given */
	void checkHistoryOnMachine(const std::string &keepIn);
	void freeProjectOnMachine();
	/* what every open shares, once the session exists; `withOpen` runs on
	 * the window just before it says the machine is open, in the same turn */
	void adoptOnMachine(ce_session *s, const std::string &package, const std::string &label, bool inputsReplaced,
		std::function<void()> withOpen = {});
	void closeOnMachine();
	/* a frame run as the window runs it: drawn unless turbo or Display
	 * Nothing says not; its lag (1), or not (0), or below 0 when it failed */
	int32_t advanceOnMachine();
	/* the frame a seek that plays stops on, -1 for none */
	int64_t pauseOnFrameM_ = -1;
	/* the MCP server: started, a call handed to the window and not answered,
	 * and the machine moving for one (where it set off, where it goes, how) */
	std::atomic<bool> mcpOnM_{false};
	bool mcpBusyM_ = false;
	McpHost mcpHost_;
	struct McpMove
	{
		uint64_t id = 0;
		int64_t start = 0, target = 0;
		enum Kind { Seek, Until, Pictures } kind = Seek;
		std::deque<int64_t> pictures;
	};
	std::optional<McpMove> mcpMoveM_;
	/* playing for advance_until with Turbo seek on: as fast as it goes */
	bool mcpFastM_ = false;
	/* the frames a second last measured, for the status tool */
	double lastFpsM_ = 0;
	void mcpOnMachine();
	/* the moving call's state: true when it is answered (or there is none) */
	bool mcpMoveOnMachine();
	void mcpAnswerOnMachine(uint64_t id, const std::string &text, bool isError);
	std::string mcpStatusOnMachine() const;
	/* TAStudio's seek: what greenzone holds at once, the rest played unless Turbo seek */
	void seekAsAskedOnMachine(int64_t target);
	/* the frame a seek that plays (Turbo seek off) goes to (-1 none): its
	 * frames are the movie's, as Chimera's TAStudio plays them with
	 * recording off, and none of them takes what the person holds */
	int64_t playedSeekToM_ = -1;
	bool playedSeekOnMachine() const { return runningM_ && pauseOnFrameM_ >= 0 && pauseOnFrameM_ == playedSeekToM_; }
	/* The way back after an edit (issue 406), gone as a seek asked for goes
	 * so the machine's thread answers the window on the way: the target, no
	 * further than the movie's end. restoreToM_ is where it goes while it is
	 * under way (-1 none); restoredOnMachine is its landing, after which the
	 * edits that follow keep its frame. */
	void restoreOnMachine(int64_t target);
	int64_t restoreToM_ = -1;
	bool restoringOnMachine() const
	{
		return restoreToM_ >= 0 && ((runningM_ && pauseOnFrameM_ == restoreToM_) || turboSeekToM_ == restoreToM_);
	}
	void restoredOnMachine();
	/* the drives as last told to the window */
	std::string drivesSaidM_ = "[]";
	int64_t restoreSaidM_ = -1;
	void drivesOnMachine();
	std::atomic<bool> turboSeekM_{false};
	Connection turboSeekWatch_;
	/* one frame: `live` takes the keys and pads held and plays the sound
	 * (a replay does neither); `render` makes the picture. The lag result. */
	/* runs one frame; grow false: a frame past the movie's end is not
	 * played (the window took it out since the machine was sent there) */
	int32_t stepOnMachine(bool live, bool render, bool grow = true);
	/* puts the machine on a frame; past the movie's end the movie grows to
	 * it, unless grow is false (going back after an edit): then it stops at
	 * the end */
	void seekOnMachine(int64_t target, bool grow = true);
	void report(int64_t changedFirst, int64_t changedLast, int64_t appendedFrom, int64_t appendedTo, double fpsNow);
	void tick();

	InputLog &inputs_;
	std::vector<std::string> buttonNames_;
	std::vector<int32_t> buttonPlayers_;
	std::vector<Axis> axes_;
	std::string controllerName_, controllerPicture_, packagePath_;
	std::vector<ProjectFile> projectFiles_;
	class ProjectFileRows : public RowModel
	{
	public:
		explicit ProjectFileRows(SessionViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.projectFiles_.size()); }
		int32_t columnCount() const override { return 6; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		SessionViewModel &vm_;
	};
	ProjectFileRows projectFileRows_{*this};
	std::vector<std::string> defaultBindings_;
	std::string mnemonics_;
	std::vector<std::string> buttonHeaders_;
	Bindings bindings_;
	Bindings autofireBindings_;
	std::vector<std::string> defaultAutofire_;
	std::vector<AnalogBind> analogBinds_, defaultAnalog_;
	/* the keys held, for the machine's thread */
	std::mutex keysHeldM_;
	std::vector<std::string> keysHeld_;
	std::map<std::string, bool> keysDown_;

	/* the machine's thread; everything below it is that thread's alone,
	 * except what is atomic or behind pendingM_ */
	std::thread thread_;
	std::mutex m_;
	std::condition_variable cv_;
	/* the machine's jobs in order; `waited` for one a thread waits on, which
	 * the machine's thread also does while it waits on the window, unless it
	 * is `betweenTurns`: one that replaces the scripts' turns waits until the
	 * script that waits on the window is done (one replaced while it ran was
	 * freed under it, and the scripts went while the machine was in one) */
	struct Job
	{
		bool waited;
		std::function<void()> run;
		bool betweenTurns = false;
	};
	std::deque<Job> jobs_;
	/* behind m_ too: the jobs the machine's thread waits on the window for */
	struct WindowJob
	{
		std::shared_ptr<bool> done;
		std::function<void()> run;
	};
	std::deque<WindowJob> windowJobs_;
	std::thread::id windowThread_ = std::this_thread::get_id(), machineThread_;
	/* the window's jobs, done on its thread (from the dispatcher, or while it waits) */
	void serveWindowJobs();
	/* job on the machine's thread, waited for */
	void waitOnMachine(const std::function<void()> &job, bool betweenTurns = false);
	std::function<void()> scriptsBeforeM_, scriptsAfterM_;
	std::function<bool()> scriptsWantedM_;
	std::function<bool(int64_t)> futureTurnM_;
	/* event.oninputpoll's: asked of the core while a script has one (it costs the core a call each read) */
	std::function<void()> inputPollTurnM_;
	std::atomic<bool> inputPollsWantedM_{false};
	static void inputPollCalled(void *ctx);
	void applyInputPollsOnMachine();
	/* a turbo seek going in pieces: where to (-1 none), and the next piece's size in frames */
	static constexpr int64_t kTurboPieceFrames = 60;
	int64_t turboSeekToM_ = -1, turboPiece_ = kTurboPieceFrames;
	/* where it started from and when, for what the seek is said to have run */
	int64_t turboFrom_ = 0;
	/* the game's file (a project's first), and its SHA-1 once asked */
	std::string romFileM_, romSha1_, romSha1For_;
	Clock::time_point turboBegan_{};
	void turboSeekPieceOnMachine();
	void endTurboSeekOnMachine();
	std::atomic<int64_t> futureMaxM_{0};
	void showFutureOnMachine();
	/* the machine's last picture, to the window */
	void publishPictureOnMachine();
	std::function<void()> scriptsIdleM_;
	Clock::time_point idleDue_{};
	std::atomic<bool> scriptsDuringTurboM_{true};
	std::atomic<bool> scriptRebootM_{false}, holdForScriptM_{false};
	/* the buttons the core was handed on the frame just run */
	SharedButtons playedM_;
	/* lent to an encode, from the asking until it is given back */
	std::atomic<bool> borrowedM_{false};
	bool scriptsNowOnMachine() const { return !turboM_.load() || scriptsDuringTurboM_.load(); }
	/* a turn of the scripts' runs now (one is not started inside another:
	 * a script's own call can move the machine) */
	bool inScriptTurnM_ = false;
	void scriptTurnOnMachine(const std::function<void()> &turn);

	bool quit_ = false;

	ce_session *s_ = nullptr;
	struct Hooks
	{
		int id;
		MachineJob frame, closing;
	};
	std::vector<Hooks> hooksM_;
	std::atomic<int> nextHook_{1};
	void freeSessionOnMachine();
	ce_project *projectM_ = nullptr;
	std::string projectPathM_;
	std::vector<int32_t> axesBuf_;
	std::vector<Marker> markersM_;
	std::vector<int64_t> runMarkersM_;
	std::atomic<bool> borrowStopM_{false};
	/* Lua's memorysavestate: the core's own state, kept in memory by an id,
	 * with the frame it was on */
	struct MemoryState
	{
		std::vector<uint8_t> bytes;
		int64_t frame = 0;
	};
	std::map<std::string, MemoryState> memoryStatesM_;
	uint64_t memoryStateCount_ = 0;
	std::atomic<int64_t> borrowFramesM_{0};
	int64_t borrowCount_ = 0;
	PackageFinder findPackageM_;
	FolderFinder firmwareFoldersM_;
	std::map<std::string, std::string> runFirmwareM_;
	/* Close without saving after the core stopped: the work kept against a
	 * crash stays, to be offered at the next open */
	bool keepWorkM_ = false;
	struct Firmware
	{
		std::vector<std::string> ids;
		std::vector<std::vector<uint8_t>> data;
		std::string pins; /* {"id","sha1"} of what was found, as a project records it */
	};
	/* firmware a project needs that was not found when it opened: what it
	 * must be (the project's pin, else the package's hash; a game core takes
	 * a file of the person's own where the project pins none), and the file
	 * located for it (issue 397) */
	struct MissingFirmware
	{
		std::string core, id, display, pinned, declared;
		json::Json decl;
		bool gameCore = false;
		std::string path, sha1;
	};
	/* the firmware a machine needs, found: the package's and, for a package
	 * whose games have their own ("gameFirmware"), what the core says this
	 * game needs - asked again at every open, with the game (`game`, a path)
	 * and its other files ({name, path}), as Chimera asks. False with why
	 * when some is missing, and what is missing in `missing` */
	bool firmwareOnMachine(const std::string &package, const std::string &slots, const std::string &settings,
		const std::string &pins, const std::string &game, const std::vector<std::pair<std::string, std::string>> &files,
		Firmware &out, std::string &why, std::vector<MissingFirmware> *missing = nullptr);
	/* the waiting project's missing firmware, and the files located for it
	 * by id (used as given at the next try, let go when the open ends) */
	std::vector<MissingFirmware> missingFirmwareM_;
	std::map<std::string, std::string> locatedFirmwareM_;
	/* a file for a missing firmware: "" when it is taken, else why not */
	std::string locateFirmwareOnMachine(MissingFirmware &m, const std::string &path);
	FirmwareKeeper keepFirmware_;
	std::vector<Branch> branchesM_;
	std::string cacheFolderM_;
	Connection cacheFolderWatch_;
	std::string packageSha1M_;
	/* the mark that a project's session is running, removed when it ends */
	std::filesystem::path runningMarkM_;
	/* the machine died in this session: what it stored is not kept */
	bool diedM_ = false;

	void pinMarkersOnMachine();
	/* frames run since the machine opened: the engine counts only the
	 * frames of a movie it plays, and this plays frames by hand */
	int64_t frameM_ = 0;
	int64_t greenEndM_ = 0;
	/* the frame just run was a lag frame (the machine's thread) */
	bool lastLagM_ = false;
	std::atomic<int64_t> speedM_{100}, volumeM_{100}, fastSpeedM_{400}, fastDrawM_{3};
	std::atomic<bool> soundNormalOnM_{true}, soundFastOnM_{true}, muteAdvanceM_{true}, steppingM_{false};
	std::atomic<bool> recordingM_{false};
	std::atomic<int64_t> fastVolumeM_{50}, budgetMbM_{4096}, nearStrideM_{4}, pictureSettleM_{3};
	/* a project's own budgets, from budgets.json in its cache folder: 0 for none */
	void readBudgetsOnMachine(ce_project *p, int64_t &memoryMb, int64_t &nearStride) const;
	Connection budgetWatch_, strideWatch_, settleWatch_;
	std::string soundDeviceM_, compileRootM_;
	bool traceOnM_ = false;
	TraceSink traceSink_;
	/* the lines the core traced since the last drain, to the sink */
	void drainTraceOnMachine();
	/* the compile cache folder for the session about to open: the engine's
	 * (ce_cache_dir) is the whole process's, so it is set before each open */
	void compileCacheOnMachine(const std::string &package, const std::string &gamePath, const std::string &knownSha1);
	Connection compileRootWatch_;
	int64_t soundBufferMsM_ = 100;
	/* the sound opened again on the device and buffer asked for; the devices said */
	void reopenSoundOnMachine();
	Connection soundDeviceWatch_, soundBufferWatch_, soundNormalWatch_, soundFastWatch_, muteAdvanceWatch_, fastVolumeWatch_;
	Connection fastSpeedWatch_, fastDrawWatch_;
	/* frames run fast since the last one drawn (fastDrawOneIn) */
	int64_t fastUndrawn_ = 0;
	std::atomic<bool> unthrottledM_{false}, soundOnM_{true}, turboM_{false}, skipLagM_{false}, displayNothingM_{false},
		padsHeardM_{true};
	bool turboHeld_ = false;
	Connection skipLagWatch_, displayNothingWatch_, backgroundWatch_, activeWatch_;
	Connection speedWatch_, unthrottledWatch_, soundOnWatch_, volumeWatch_, throttleWatch_;
	/* 0 clock, 1 audio, 2 vsync, 3 none */
	std::atomic<int> throttleM_{0};
	std::atomic<uint64_t> vsyncTicks_{0};
	uint64_t vsyncSeen_ = 0;
	/* Where an edit sends the machine back to, as Chimera's TAStudio does: the
	 * frame it was on at the first edit, kept through the edits that follow
	 * until the machine is moved some other way (-1: none). movesM_ counts
	 * the machine's moves; restoredMovesM_ and restoredAtM_ say how many there
	 * had been, and where it was, when the last edit had moved it. */
	int64_t restoreFrameM_ = -1, restoredAtM_ = -1;
	/* where the last wheel seek was going (wheelSeek) */
	int64_t wheelTarget_ = -1;
	uint64_t movesM_ = 0, restoredMovesM_ = 0;
	/* edits the window has made (counted on its thread as each is sent), and
	 * the count the edit job now running was sent at: more since means a
	 * later edit's job is on its way */
	std::atomic<uint64_t> editsSent_{0};
	ProjectAnswers lastAnswers_;
	/* the hidden columns and the piano roll's columns, in Chimera's shape
	 * (written when the project had none of its own), for the machine's thread */
	std::vector<std::string> hiddenColumnsM_;
	std::map<std::string, std::string> tastudioMembersM_;
	std::vector<std::string> autoHeldColumnsM_;
	/* the Auto-hold columns' patterns, and where each is */
	std::vector<ColumnPattern> columnPatternsM_;
	/* the frames the piano roll shows, and the states among them */
	std::atomic<int64_t> watchFirstM_{0}, watchCountM_{0};
	int64_t pendingStatedFrom_ = 0;
	std::vector<uint8_t> pendingStated_;
	bool statedFresh_ = false;
	int64_t statedFrom_ = 0;
	std::vector<uint8_t> stated_;
	std::vector<size_t> columnAtM_;
	std::string freshColumnsM_;
	/* the defaults, sent once a machine's controls are known */
	void loadDefaultColumns();
	/* what the counters over the picture last worked out, so a frame run
	 * costs a frame's work: the last frame with input (for a movie of this
	 * length), and the lag frames counted up to a frame */
	void updateOverlay(int64_t frame, int64_t changedFrom);
	int64_t lastInputFor_ = -1, lastInput_ = 0, lagsTo_ = 0, lags_ = 0;
	/* what a script's emu.setlagcount added to the count, from which frame */
	int64_t lagOffset_ = 0, lagOffsetFrom_ = INT64_MAX;
	bool fastHeld_ = false, rewindHeld_ = false;
	std::vector<Connection> overlayWatches_;
	uint64_t editRunningM_ = 0;
	bool runningM_ = false;
	/* what a held key is doing (the machine's thread) */
	enum class Hold { None, Advance, Rewind };
	Hold holdM_ = Hold::None;
	Clock::time_point holdSince_{};
	bool holdRunning_ = false, fastM_ = false;
	Clock::time_point due_{};
	Clock::duration period_ = std::chrono::microseconds(16667);
	int64_t fpsFrames_ = 0;
	Clock::time_point fpsSince_{};
	std::string package_;
	/* gamepads are read on this thread, just before each frame */
	std::unique_ptr<Gamepads> padsM_;
	/* catching a pad input: when to look next, and what was held already */
	bool catchingM_ = false;
	/* catching an axis: where each was, and how far each has moved since */
	bool catchingAxisM_ = false;
	std::vector<std::pair<std::string, int>> catchAxesFromM_;
	std::map<std::string, int> catchMovedM_;
	/* the pointer over the picture, -10000..10000 each way (WMouse X, WMouse Y) */
	std::atomic<int> pointerXM_{0}, pointerYM_{0};
	/* how far the pointer moved since the last frame read it (Chimera's RMouse X and Y) */
	std::atomic<int> movedXM_{0}, movedYM_{0};
	/* how hard that movement pushes, in percent */
	std::atomic<int> mouseSensitivityM_{100};
	Clock::time_point catchDue_{};
	std::vector<std::string> catchHeldM_;
	void catchPollOnMachine();
	Bindings bindingsM_;
	Bindings autofireBindingsM_;
	std::vector<AnalogBind> analogBindsM_;
	std::vector<Axis> axesM_;
	std::vector<std::string> buttonNamesM_;
	Buttons autofireWasM_;
	/* the frame each button's autofire key went down, by its place */
	std::vector<int64_t> autofireStartM_;
	std::vector<std::pair<std::string, int>> padAxes_, padAxesCopy_;
	/* the keys and pad inputs held this frame */
	std::vector<std::string> keysThisFrameM_;
	Connection periodWatch_;
	std::atomic<int64_t> periodM_{1};
	std::vector<std::string> padHeld_;
	mutable std::mutex padHeldCopyM_;
	std::vector<std::string> padHeldCopy_;
	size_t padCount_ = 0;
	std::unique_ptr<Sound> soundM_;

	SharedButtons held_;
	/* buttons a script holds for the next frame run (Lua's joypad.set), let go after it */
	SharedButtons scriptHeld_;
	/* what a script let go for the next frame (joypad.set with false) */
	SharedButtons scriptReleased_;
	/* a script's analog values: for the next frame (joypad.setaxis), and held
	 * until let go (joypad.setanalog), by the control's place */
	std::mutex scriptAxesM_;
	std::map<size_t, int32_t> scriptAxesOnce_, scriptAxesHeld_;
	/* the sticky buttons, a bit each, and which arming is on (1 hold, 2 fire) */
	SharedButtons autoholdMask_, autofireMask_;
	std::atomic<int> armed_{0};
	std::atomic<int64_t> fireOnM_{1}, fireOffM_{1};
	std::atomic<bool> fireSkipsLagM_{true};
	/* each firing button's place in its pattern, and the pads' buttons held
	 * at the last frame (a pad's press arms a toggle too) - the machine's */
	std::vector<int64_t> fireAtM_;
	Buttons padWasM_;
	void toggleSticky(const Buttons &pressed);
	void stickyChanged();
	std::vector<Connection> stickyWatches_;

	/* what the next tick on the view's thread reports */
	std::mutex pendingM_;
	bool tickPosted_ = false;
	int64_t pendingFrame_ = 0;
	double pendingFps_ = -1;
	/* the average speed: the engine's count, what the loop's last pass did */
	ce_average_fps *averageM_ = nullptr;
	Clock::time_point passAt_{};
	bool pausedSincePassM_ = true;
	void passOnMachine(int64_t frames, bool stepping, bool goingBack);
	double pendingAverage_ = 0;
	std::atomic<bool> averageResetM_{false};
	/* the game's timer, read with each report when shown */
	std::atomic<bool> showGameTimeM_{false};
	bool gameTimerM_ = false;
	/* the game's timer after each frame the machine ran (kNoGameTime where it
	 * has not been since), forgotten past an edit as the stored states are:
	 * what the project's GameTime headers are written from */
	static constexpr int64_t kNoGameTime = INT64_MIN;
	std::vector<int64_t> gameTimeLogM_;
	void forgetGameTimeAfterOnMachine(int64_t frame);
	void gameTimeHeadersOnMachine(ce_project *p, int64_t length);
	int64_t pendingGameMs_ = -1;
	int64_t pendingQueued_ = 0, pendingDropped_ = 0;
	int64_t pendingGreenEnd_ = 0, pendingStates_ = 0, pendingRerecords_ = 0;
	int64_t changedFirst_ = -1, changedLast_ = -1;
	int64_t appendedFirst_ = -1, appendedLast_ = -1;
	std::vector<RecordedFrame> recordedM_;
	/* the freezes: the window's list, and the machine's copy with the game they belong to */
	std::vector<Freeze> freezes_, freezesM_;
	std::string freezeGameM_;
	void freezesUpdated();
	void syncFreezes();
	/* the engine given the list again; those it refuses are left out */
	std::vector<Freeze> syncFreezesOnMachine(const std::vector<Freeze> &list);
	int32_t addFreezeOnMachine(const Freeze &f);
	/* a new machine: the freezes stay for the same game, else go */
	void carryFreezesOnMachine(const std::string &game);
};

} // namespace chimera::vm
