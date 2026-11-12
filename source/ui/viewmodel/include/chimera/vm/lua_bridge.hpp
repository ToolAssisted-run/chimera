#pragma once
/* Lua scripts over the view models. Each view model is a table under
 * `chimera`, by its name: a property is read and set as a field
 * (chimera.session.frame, chimera.pianoRoll.followPlayback = false) and a
 * command is a function (chimera.session.play(), chimera.pianoRoll.toggle(5, 0)).
 * Nothing here knows any view model: what a registry has, a script has, and
 * the MCP server would list the same names (describe()).
 *
 * Scripts run one beside another, as in Chimera's Lua Console: each is
 * started from its file (or text) as a coroutine of its own, and where it
 * yields (emu.frameadvance()) it goes on after the next frame. What it
 * registers (event.onframeend, tastudio.onbranchload, ...) is its own: called
 * as that script, and gone when it stops. A script whose code has ended goes
 * on while it has something registered or a form open, as Chimera's do. While
 * a script's code runs the working folder is the script's, as Chimera's.
 * Chimera's Lua libraries (emu, memory, mainmemory, joypad, movie, client,
 * console, gameinfo, event, bit, ...) are a layer of Lua over the view models,
 * defined at the start (lua_libraries.cpp).
 *
 * Scripts run on the machine's thread, before and after every frame, as
 * Chimera's do (owner, 2026-10-24), and reach the window's view models by
 * waiting on the window: see toScripts and toWindow. */

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

struct lua_State;

namespace chimera::vm {

class LuaBridge
{
public:
	LuaBridge();
	~LuaBridge();
	LuaBridge(const LuaBridge &) = delete;
	LuaBridge &operator=(const LuaBridge &) = delete;

	void add(ViewModel &vm);

	/* Where scripts run. toScripts runs a job on the scripts' thread (the
	 * machine's) and waits for it, at once when called there; toWindow runs
	 * one on the window's thread and waits, at once when called there; each
	 * says false when the job could not be run (the program is closing).
	 * Every call below runs on the scripts' thread, and what a script reads,
	 * sets or calls on a view model runs on the window's, unless the view
	 * model answers it on the machine's (scriptSide). What the bridge tells
	 * (onLog, onWrite, onStopped) is told on the window's thread. Unset,
	 * everything runs on the thread that calls, as the bridge's own tests
	 * use it. */
	std::function<bool(const std::function<void()> &)> toScripts, toWindow;
	/* a job for the scripts' thread, not waited for (a notice no one needs
	 * an answer to); unset, notices are called as callHook */
	std::function<void(std::function<void()>)> toScriptsLater;

	enum class State { Stopped, Running, Paused };

	/* a script from a file: read, loaded and started, run to where it first
	 * waits; its number, or 0 with what went wrong in `error` */
	int64_t start(const std::string &path, std::string &error);
	/* a script from text; its working folder `directory` ("" for the
	 * program's), its name in messages `chunkName` */
	int64_t startCode(const std::string &code, const std::string &chunkName, const std::string &directory, std::string &error);
	/* a chunk run as a script of its own; false with the message in `error` when it fails */
	bool run(const std::string &code, const std::string &chunkName, std::string &error)
	{
		return startCode(code, chunkName, "", error) != 0;
	}
	/* a script stopped: its OnExit functions called, then everything it registered gone */
	void stop(int64_t script);
	/* a paused script's code waits; its registered functions are still called, as Chimera's */
	void setPaused(int64_t script, bool paused);
	State state(int64_t script) const;
	/* every script stopped */
	void stopAll();
	/* whether any script is running or paused */
	bool running() const;
	/* what the Lua Console shows of the scripts: whether any runs, each
	 * one's state, and the stamp of the functions they registered. Read on
	 * the scripts' thread at the end of a frame and handed to the window
	 * with it, the window waits on nothing: asked from the window, each
	 * answer waited for the frame the machine was running (issue 404) */
	struct Seen
	{
		bool running = false;
		std::map<int64_t, State> states;
		std::string stamp;
	};
	Seen seen();
	/* told when a script stops, whatever stopped it (its end, its error, stop()) */
	std::function<void(int64_t)> onStopped;
	/* told, before onStopped, when a running script stops on an error, with
	 * the error (the MCP server's lua_scripts says it, issue 571) */
	std::function<void(int64_t, const std::string &)> onFailed;

	/* the console's line: an expression's values, or a statement run, as the
	 * console's own (script 0); what it gives back is printed, as Chimera's */
	void execute(const std::string &command);
	/* the MCP server's run_lua: the agent's code run to its end as a script
	 * of its own, its working folder `directory`; an expression gives its
	 * value, as on the console's line. What it returned, as a JSON array
	 * (a table an object, or a list when its keys are 1 to n), or false with
	 * what it raised in `error`. A function it registers keeps it, as a
	 * script's do; waiting for a frame is refused, as Chimera's refuses it.
	 * Code still running after `seconds` is stopped with an error, so a loop
	 * that never ends cannot hold the window (issue 571) */
	bool runForAgent(const std::string &code, const std::string &directory, std::string &results, std::string &error,
		double seconds = kAgentSeconds);
	static constexpr double kAgentSeconds = 10;

	/* called before every frame: what the scripts drew for the last frame
	 * goes, and the OnFrameStart functions are called (a joypad.set there is
	 * this frame's) */
	void frameStart();
	/* called after every frame: the OnFrameEnd functions (and the
	 * OnFrameStart ones, when frameStart was not called: the machine moved
	 * another way), then every running script that waits for a frame goes
	 * on, each to its next wait; one that ends with nothing registered, or
	 * fails, is stopped */
	void frameDone();
	/* a read of the controls the core reported, part way through a frame:
	 * the scripts' event.oninputpoll functions, called there and then */
	void inputPoll();
	/* an access to memory a script watches, which the core reported part way
	 * through a frame: the scripts' event.on_bus_* functions, called there and
	 * then, with its size in bytes (0 unknown) and whether the core lets its
	 * value be changed. The value the last function to give a number gave,
	 * when one did (issue 534) */
	std::optional<uint64_t> memoryAccess(int32_t kind, int32_t scope, uint64_t address, uint64_t value, int32_t size, bool replaceable);
	/* the scripts' userdata as text, kept with a branch; and a branch loaded:
	 * its userdata put back (none: left as it is), then the scripts'
	 * event.onloadstate functions called with its name (issue 534) */
	std::string userdataText();
	void branchLoaded(const std::optional<std::string> &userdata, const std::string &name);
	/* the window's turn when no frame is run (paused, or nothing open): the
	 * running scripts that wait in emu.yield go on, each to its next wait, as
	 * Chimera resumes them every pass of its loop; those waiting for a frame
	 * (emu.frameadvance) wait on. True when any of them stopped */
	bool yieldTurn();
	/* the console closing: its OnConsoleClose functions, then every script stopped */
	void consoleClosed();

	/* what print() wrote and what failed, a line each */
	std::function<void(const std::string &)> onLog;
	/* what scripts wrote as it is written (console.write writes no line end);
	 * when set, it is told instead of onLog */
	std::function<void(const std::string &)> onWrite;

	/* every view model's properties and commands with their help, as JSON:
	 * what an MCP server's tool list would be made from */
	std::string describe() const;

	/* a global function of the libraries' called with a number, if it is
	 * there (a form's click, a form closed) */
	void callHook(const char *name, int64_t arg);
	/* the same, as a notice: on the scripts' thread when it gets to it, the
	 * caller not waiting (an edit, a branch saved, a form clicked) */
	void notify(const char *name, int64_t arg);
	/* a global function of the libraries' asked about a cell (row, column):
	 * its answer as text, "" for none */
	std::string query(const char *name, int64_t row, const std::string &column);
	/* a cell a roll asks the scripts about as it is drawn: kind 0 its
	 * colour, 1 its text, 2 its icon; the answer "" for none */
	struct Cell
	{
		int kind = 0;
		int64_t row = 0;
		std::string column, answer;
	};
	/* every cell of one drawing asked at once, in one wait for the scripts'
	 * thread (a wait for each cell held the roll and the machine back, issue
	 * 476); a kind no script registered a function for is not asked */
	void query(std::vector<Cell> &cells);
	/* a global function of the libraries' called with these: its first answer
	 * as text ("true" for true, "" for none or false) */
	std::string ask(const char *name, const std::vector<Value> &args = {});

private:
	struct Script
	{
		int64_t id = 0;
		std::string directory;
		State state = State::Running;
		/* a registry reference to its coroutine; none once its code has ended */
		int thread = -2;
		/* stop() was asked while its code ran: stopped when it next waits */
		bool stopAfter = false;
		/* where it waits, it waits for a frame (emu.frameadvance), not for the window's next turn */
		bool frameWait = false;
	};

	static int index(lua_State *L);
	static int newIndex(lua_State *L);
	static int call(lua_State *L);
	static int write(lua_State *L);
	static int enter(lua_State *L);
	static int folder(lua_State *L);
	static void push(lua_State *L, const Value &v);
	static bool read(lua_State *L, int at, Value &out);
	void log(const std::string &line);
	void writeText(const std::string &text);
	static const char *luaLibraries();
	/* fn on the scripts' thread, waited for, with its answer (T{} when it could not run) */
	template <class F>
	auto onScripts(F &&fn) const -> decltype(fn());
	/* fn on the window's thread, waited for; false when it could not run */
	bool atWindow(const std::function<void()> &fn) const;
	/* the bodies of the calls above, on the scripts' thread */
	int64_t startHere(const std::string &path, std::string &error);
	int64_t startCodeHere(const std::string &code, const std::string &chunkName, const std::string &directory, std::string &error);
	void stopHere(int64_t script);
	void stopAllHere();
	void executeHere(const std::string &command);
	bool runForAgentHere(const std::string &code, const std::string &directory, std::string &results, std::string &error, double seconds);
	/* run_lua's limit while its code starts, 0 otherwise: startCodeHere gives the new script a clock */
	double agentSeconds_ = 0;
	void frameDoneHere();
	/* the running scripts go on, those waiting for a frame only when `frames` */
	bool resumeRunning(bool frames);
	std::string askHere(const char *name, const std::vector<Value> &args);
	/* a global function of the libraries' called, if it is there */
	void callHook(const char *name);
	/* the script whose code runs now: the global __chimera_script and the
	 * working folder; the one before back */
	int64_t enterScript(int64_t id);
	Script *find(int64_t id);
	/* goes on with a script's coroutine to its next wait; false when it failed */
	bool resume(int64_t id, std::string &error);
	bool keeps(int64_t id);
	void published();

	lua_State *L_;
	std::vector<ViewModel *> models_;
	std::vector<Script> scripts_;
	int64_t nextId_ = 1;
	int64_t current_ = 0;
	/* the script whose coroutine is being resumed, 0 for none */
	int64_t resuming_ = 0;
	/* the working folder before a script's was taken */
	std::string home_;
	/* what console.write left without a line end, when only onLog listens */
	std::string partial_;
	/* how many times anything was written: whether the console's line printed something itself */
	uint64_t written_ = 0;
	/* frameStart was called for the frame frameDone ends */
	bool begun_ = false;
};

} // namespace chimera::vm
