/* The view models with no window: what a script, the MCP server or a test
 * sees. No Qt is linked here; the dispatcher is drained by hand.
 *
 *   test_vm [<synth-box package> <synth test rom> [<synth movies dir> <synth goldens dir>]]
 *
 * Without the package and rom the machine tests are skipped (exit 77 when
 * nothing else ran). */

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <clocale>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <mutex>
#include <regex>
#include <set>
#include <string>
#include <thread>
#include <tuple>

#ifndef _WIN32
#include <dlfcn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#endif

#include "chimera/vm/bindings.hpp"
#include "chimera/vm/config_names.hpp"
#include "chimera/vm/config_store.hpp"
#include "chimera/vm/controls_vm.hpp"
#include "chimera/vm/hotkeys_vm.hpp"
#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/hex_editor_vm.hpp"
#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/project_details_vm.hpp"
#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/help_vm.hpp"
#include "chimera/vm/issue_report.hpp"

/* the engine's miniz, to read a report's zip back */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../../../extern/miniz/miniz.h"
#include "chimera/vm/media_maker_vm.hpp"
#include "chimera/vm/cache_manager_vm.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/tool_windows_vm.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/process_stamp.hpp"
#include "chimera/vm/crash_notes.hpp"
#include "chimera/vm/customize_vm.hpp"
#include "chimera/vm/messages_vm.hpp"
#include "chimera/vm/display_vm.hpp"
#include "chimera/vm/paths_vm.hpp"
#include "chimera/vm/firmware_vm.hpp"
#include "chimera/vm/config_files_vm.hpp"
#include "chimera/vm/data_folder_vm.hpp"
#include "chimera/vm/precompiled_vm.hpp"
#include "chimera/vm/trace_logger_vm.hpp"
#include "chimera/vm/idle_eye.hpp"
#include "chimera/vm/encoder_vm.hpp"
#include "chimera/vm/host_input_vm.hpp"
#include "chimera/vm/game_vm.hpp"
#include "chimera/vm/forms_vm.hpp"
#include "chimera/vm/comm_vm.hpp"
#include "chimera/vm/sql_vm.hpp"
#ifndef _WIN32
#include <sys/mman.h>
#endif
#include "chimera/vm/nut_muxer.hpp"
#include "chimera/vm/subprocess.hpp"
#include "chimera/vm/surface_viewer_vm.hpp"
#include "chimera/vm/macro_tool_vm.hpp"
#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/watch_values.hpp"
#include "chimera/vm/sound.hpp"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/themes_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/engine.h"

using namespace chimera::vm;

static int failures = 0, checks = 0;
/* Every test's session seeks at once (Turbo seek on), as the checks wait
 * for the frame; Turbo seek off, which plays to the frame, has a test of its own */
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)
/* a test's folder taken away; on Windows a file still open (a machine not
 * closed, a handle kept) is not removed, and that is a failure, not the end
 * of the program */
#define removeTree(folder) \
	do { \
		std::error_code removeEc_; \
		std::filesystem::remove_all(folder, removeEc_); \
		checks++; \
		if (removeEc_) { std::fprintf(stderr, "FAIL %s:%d: %s not removed: %s\n", __FILE__, __LINE__, std::filesystem::path(folder).string().c_str(), removeEc_.message().c_str()); failures++; } \
	} while (0)
/* a text compared, what it was said when it is not */
#define CHECK_TEXT(got, want) \
	do { \
		checks++; \
		const std::string got_ = (got), want_ = (want); \
		if (got_ != want_) { std::fprintf(stderr, "FAIL %s:%d: %s was \"%s\", not \"%s\"\n", __FILE__, __LINE__, #got, got_.c_str(), want_.c_str()); failures++; } \
	} while (0)

namespace {

class ManualDispatcher : public Dispatcher
{
public:
	void post(std::function<void()> fn) override
	{
		std::lock_guard<std::mutex> lock(m_);
		q_.push_back(std::move(fn));
	}
	int pump()
	{
		std::deque<std::function<void()>> now;
		{
			std::lock_guard<std::mutex> lock(m_);
			now.swap(q_);
		}
		for (auto &fn : now) fn();
		return static_cast<int>(now.size());
	}
	/* drains until cond holds or the time runs out */
	template <class F>
	bool until(F cond, double seconds)
	{
		const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
		while (std::chrono::steady_clock::now() < end)
		{
			pump();
			if (cond()) return true;
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		pump();
		return cond();
	}

private:
	std::mutex m_;
	std::deque<std::function<void()>> q_;
};

/* a script written to a file of its own, for the console to open, as
 * Chimera's opens scripts; the folder goes when the tests end */
std::filesystem::path scriptFolder()
{
	static const std::filesystem::path dir = std::filesystem::temp_directory_path() /
		("chimera-test-scripts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(dir);
	return dir;
}
std::string scriptFile(const std::string &code, const std::string &name)
{
	const std::filesystem::path p = scriptFolder() / (name + ".lua");
	std::ofstream(p, std::ios::binary) << code;
	return p.string();
}

/* A game runs only in a project (owner, 2026-10-26), so a test that plays a
 * game makes one, as the New Project window would: the package's core, the
 * game copied beside the project in the slot that takes its kind of file
 * (else the first), every setting that applies at the package's default.
 * Written to `project`, or to a folder of its own when none is named. */
std::filesystem::path gameProject(const std::string &package, const std::string &game, std::filesystem::path project = {},
	const std::string &title = {})
{
	namespace fs = std::filesystem;
	static int made = 0;
	if (project.empty())
		project = scriptFolder() / ("game-" + std::to_string(++made)) / (fs::path(game).stem().string() + ".chimeraProject");
	fs::create_directories(project.parent_path());
	const fs::path beside = project.parent_path() / fs::path(game).filename();
	std::error_code ec;
	if (!fs::equivalent(beside, game, ec)) fs::copy_file(game, beside, fs::copy_options::overwrite_existing);
	json::Json cfg, fileSlots;
	std::string jerr, sha;
	const char *perr = nullptr;
	if (ce_package *pk = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(pk, "waterbox.config", &len))
			json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, jerr);
		if (const uint8_t *d = ce_package_entry(pk, "file_slots.json", &len))
			json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), fileSlots, jerr);
		sha = ce_package_sha1(pk) != nullptr ? ce_package_sha1(pk) : "";
		ce_package_free(pk);
	}
	std::string ext = fs::path(game).extension().string();
	if (!ext.empty()) ext.erase(0, 1);
	for (char &ch : ext) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	std::string slotId = "rom";
	if (const json::Json *list = fileSlots.find("slots"); list != nullptr && !list->items.empty())
	{
		if (const json::Json *id = list->items[0].find("id")) slotId = id->text;
		for (const json::Json &slot : list->items)
		{
			bool takes = false;
			if (const json::Json *formats = slot.find("formats"))
				for (const json::Json &f : formats->items)
				{
					std::string e = f.text;
					for (char &ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					takes = takes || e == ext;
				}
			if (takes)
			{
				if (const json::Json *id = slot.find("id")) slotId = id->text;
				break;
			}
		}
	}
	ce_project *p = ce_project_new();
	const json::Json *coreName = cfg.find("coreName"), *version = cfg.find("version");
	ce_project_set_core(p, coreName != nullptr ? coreName->text.c_str() : "", version != nullptr ? version->text.c_str() : "", sha.c_str());
	if (const json::Json *system = cfg.find("systemId")) ce_project_header_set(p, "Platform", system->text.c_str());
	const char *error = nullptr;
	ce_project_file_add(p, beside.filename().string().c_str(), slotId.c_str(), beside.string().c_str(), &error);
	json::Json slotMap, files, recorded, none;
	slotMap.kind = recorded.kind = none.kind = json::Json::Kind::Object;
	files.kind = json::Json::Kind::Array;
	files.items.push_back(json::Json::string(beside.filename().string()));
	slotMap.members.emplace_back(slotId, files);
	if (const json::Json *decl = cfg.find("settings"))
	{
		const json::Json *machineSetting = cfg.find("machineSetting");
		for (size_t i : SettingsViewModel::applicable(decl->items, json::write(slotMap), none, machineSetting != nullptr ? machineSetting->text : ""))
		{
			const json::Json *name = decl->items[i].find("name"), *v = decl->items[i].find("default");
			if (name != nullptr && v != nullptr) recorded.members.emplace_back(name->text, *v);
		}
	}
	ce_project_set_settings_text(p, json::write(recorded).c_str(), &error);
	ce_project_set_title(p, title.empty() ? fs::path(game).stem().string().c_str() : title.c_str());
	ce_project_save(p, project.string().c_str(), &error);
	ce_project_free(p);
	return project;
}
/* ...and opened in the session */
std::filesystem::path openGame(SessionViewModel &session, const std::string &package, const std::string &game, std::string &error,
	std::filesystem::path project = {}, const std::string &title = {})
{
	project = gameProject(package, game, project, title);
	session.run("openProject", {project.string(), package}, error);
	return project;
}

class Sample : public ViewModel
{
public:
	explicit Sample(Dispatcher &d) : ViewModel("sample", d)
	{
		expose("count", "a number anyone may set", count, true);
		expose("label", "a text only the view model sets", label);
		command("add", "adds to count", {"amount"}, [this](const Args &a, std::string &error) -> Value {
			int64_t n = 0;
			if (!arg(a, 0, n, error)) return {};
			count.set(count.get() + n);
			return count.get();
		}, &armed);
	}
	Property<int64_t> count{0};
	Property<std::string> label{"x"};
	Property<bool> armed{true};
};

void signalsAndProperties()
{
	Property<int64_t> p{1};
	int seen = 0;
	Connection c = p.changed.connect([&] { seen++; });
	p.set(1);
	CHECK(seen == 0);
	p.set(2);
	CHECK(seen == 1);
	c.disconnect();
	p.set(3);
	CHECK(seen == 1);

	/* a slot that disconnects another during the same emit: the other is not called */
	Signal<> s;
	int first = 0, second = 0;
	Connection c2;
	Connection c1 = s.connect([&] { first++; c2.disconnect(); });
	c2 = s.connect([&] { second++; });
	s.emit();
	CHECK(first == 1 && second == 0);

	/* a connection that outlives its signal ends quietly */
	Connection late;
	{
		Signal<int> gone;
		late = gone.connect([](int) {});
	}
	late.disconnect();
}

void registry()
{
	ManualDispatcher d;
	Sample vm(d);
	std::string error;
	CHECK(vm.get("count") && std::get<int64_t>(*vm.get("count")) == 0);
	CHECK(vm.set("count", int64_t{5}, error) && vm.count.get() == 5);
	CHECK(vm.set("count", 7.0, error) && vm.count.get() == 7); /* a script's whole real number */
	CHECK(!vm.set("count", std::string("many"), error) && error.find("cannot be") != std::string::npos);
	CHECK(!vm.set("label", std::string("y"), error) && error.find("cannot be set") != std::string::npos);
	CHECK(!vm.set("nothing", int64_t{1}, error) && error.find("no property") != std::string::npos);
	auto r = vm.run("add", {int64_t{3}}, error);
	CHECK(r && std::get<int64_t>(*r) == 10);
	CHECK(!vm.run("add", {}, error) && error.find("missing") != std::string::npos);
	vm.armed.set(false);
	CHECK(!vm.run("add", {int64_t{1}}, error) && error.find("cannot run now") != std::string::npos);
	CHECK(!vm.run("subtract", {}, error) && error.find("no command") != std::string::npos);
	CHECK(vm.properties().size() == 2 && vm.commands().size() == 1);
}

/* A script reaches a view model only through its registry */
void lua()
{
	ManualDispatcher d;
	Sample vm(d);
	LuaBridge lua;
	std::vector<std::string> lines;
	lua.onLog = [&](const std::string &l) { lines.push_back(l); };
	lua.add(vm);
	std::string error;
	CHECK(lua.run("print(chimera.sample.count, chimera.sample.label)", "t1", error));
	CHECK(lines.size() == 1 && lines[0] == "0\tx");
	CHECK(lua.run("chimera.sample.count = 4; print(chimera.sample.add(3))", "t2", error));
	CHECK(vm.count.get() == 7 && lines.back() == "7");
	/* the view model's own refusals come back as Lua errors, with its words */
	CHECK(!lua.run("chimera.sample.label = 'y'", "t3", error) && error.find("cannot be set") != std::string::npos);
	CHECK(!lua.run("chimera.sample.add('lots')", "t4", error) && error.find("argument 1 cannot be") != std::string::npos);
	CHECK(!lua.run("return chimera.sample.nothing", "t5", error) && error.find("no property or command nothing") != std::string::npos);
	CHECK(lua.run("local ok, e = pcall(chimera.sample.add); print(ok, e)", "t6", error));
	CHECK(lines.back().rfind("False\t", 0) == 0 && lines.back().find("missing") != std::string::npos);
	/* onFrame functions run on every frame; one that fails is said each time and kept, as Chimera's */
	CHECK(lua.run("n = 0; chimera.onFrame(function() n = n + 1 end); chimera.onFrame(function() error('boom') end)", "t7", error));
	lua.frameDone();
	lua.frameDone();
	CHECK(lua.run("print(n)", "t8", error) && lines.back() == "2");
	int failed = 0, said = 0;
	for (const auto &l : lines)
	{
		failed += l.find("boom") != std::string::npos;
		said += l == "error running function attached by the event OnFrameEnd";
	}
	CHECK(failed == 2 && said == 2);
	/* what an MCP server would list: the same names, from the same registry */
	const std::string d1 = lua.describe();
	CHECK(d1.find("\"name\": \"sample\"") != std::string::npos && d1.find("\"name\": \"add\"") != std::string::npos &&
		d1.find("\"params\": [\"amount\"]") != std::string::npos && d1.find("\"settable\": false") != std::string::npos);
}

/* The console, as Chimera's: scripts opened into a list and started, each
 * toggled, paused, moved and taken out; what they print; the line typed into;
 * the list kept as a session file and read back; what a script registers its
 * own, called after each frame and gone when it stops */
std::string readFile(const std::filesystem::path &p);

void luaConsole()
{
	namespace fs = std::filesystem;
	ManualDispatcher d;
	Sample vm(d);
	LuaBridge lua;
	lua.add(vm);
	LuaConsoleViewModel console(d, lua);
	lua.add(console);
	std::string error;
	RowModel &out = *console.rowLists()[0].model;
	RowModel &list = *console.rowLists()[1].model;
	RowModel &fns = *console.rowLists()[2].model;
	auto cell = [](RowModel &m, int64_t r, int32_t c) {
		std::string t;
		fromValue(m.cell(r, c), t);
		return t;
	};
	auto lastLine = [&] { return cell(out, out.rowCount() - 1, 0); };
	auto lineBack = [&](int64_t n) { return cell(out, out.rowCount() - 1 - n, 0); };
	auto run = [&](const char *name, Args args = {}) { return console.run(name, args, error); };

	/* a script that ends at once with nothing registered stops at once */
	const std::string once = scriptFile("print(chimera.sample.add(2) * 10)", "once");
	run("openScript", {once});
	CHECK(lastLine() == "20" && list.rowCount() == 1 && cell(list, 0, 0) == "once" && cell(list, 0, 1) == once);
	CHECK(cell(list, 0, 2) == "stopped" && console.summary.get() == "1 script (0 active, 0 paused)" && console.changes.get());
	/* a failing one says why */
	run("openScript", {scriptFile("chimera.sample.nothing()", "bad")});
	CHECK(lastLine().find("no property or command nothing") != std::string::npos && cell(list, 1, 2) == "stopped");
	/* one waiting for frames runs on; one that registers runs on after its code ends */
	const std::string hooks = scriptFile("seen = 0\nevent.onframeend(function() seen = seen + 1 end, 'counter')\n"
		"event.onexit(function() print('bye', seen) end)", "hooks");
	run("openScript", {scriptFile("n = 0\nwhile true do n = n + 1 emu.frameadvance() end", "loop")});
	run("openScript", {hooks});
	CHECK(cell(list, 2, 2) == "running" && cell(list, 3, 2) == "running" && console.summary.get() == "4 scripts (2 active, 0 paused)");
	console.frameDone();
	console.frameDone();
	run("execute", {std::string("n, seen")});
	CHECK(lastLine() == "3\t2" && console.running.get() && console.anyEnabled.get());
	/* the window's part of a frame, and the list it shows, ask the scripts'
	 * thread nothing: what they show comes with the frame (issue 404: in
	 * the window each question waited for the frame the machine was running) */
	{
		int asked = 0;
		lua.toScripts = [&asked](const std::function<void()> &job) {
			asked++;
			job();
			return true;
		};
		const LuaBridge::Seen seen = lua.seen();
		CHECK(asked == 1 && seen.running && seen.states.size() == 2);
		asked = 0;
		console.afterFrame(seen);
		CHECK(cell(list, 2, 2) == "running" && cell(list, 3, 2) == "running" && console.running.get());
		CHECK(asked == 0);
		lua.toScripts = nullptr;
	}
	/* paused: its code waits; what it registered is still called, as Chimera's */
	run("select", {int64_t{2}, std::string("only")});
	CHECK(console.selectedCount.get() == 1 && console.selectedScripts.get() == 1 && list.cell(2, 4) == Value(true));
	run("pause");
	CHECK(cell(list, 2, 2) == "paused" && console.summary.get() == "4 scripts (2 active, 1 paused)");
	console.frameDone();
	run("execute", {std::string("n, seen")});
	CHECK(lastLine() == "3\t3");
	run("pause");
	CHECK(cell(list, 2, 2) == "running");
	/* the functions registered, by event then name, each with its id */
	CHECK(console.functionCount.get() == 2 && fns.rowCount() == 2 && cell(fns, 0, 0) == "OnExit" && cell(fns, 0, 1) == "Anonymous");
	CHECK(cell(fns, 1, 0) == "OnFrameEnd" && cell(fns, 1, 1) == "counter" && cell(fns, 1, 2).size() == 36 && cell(fns, 1, 2)[8] == '-');
	CHECK(run("callFunction", {cell(fns, 1, 2)}) == Value(true));
	run("execute", {std::string("seen")});
	CHECK(lastLine() == "4");
	/* toggled off: its OnExit functions called, then everything of it gone */
	run("select", {int64_t{3}, std::string("only")});
	run("toggle");
	CHECK(lastLine() == "bye\t4" && cell(list, 3, 2) == "stopped" && console.functionCount.get() == 0 && console.lastScript.get() == hooks);
	/* Toggle Last Lua Script starts it again, and stops it again */
	CHECK(run("toggleLast") == Value(true) && console.functionCount.get() == 2);
	CHECK(run("toggleLast") == Value(false) && console.functionCount.get() == 0);
	/* a script's own unregistered by name and by id */
	run("execute", {std::string("a = event.onframeend(function() end, 'one') b = event.onframestart(function() end, 'two')")});
	CHECK(console.functionCount.get() == 2);
	run("execute", {std::string("event.unregisterbyname('one'), event.unregisterbyname('one'), event.unregisterbyid(b)")});
	CHECK(lastLine() == "True\tFalse\tTrue" && console.functionCount.get() == 0);
	/* moved, a separator put before the selected row */
	run("select", {int64_t{0}, std::string("only")});
	run("moveDown");
	CHECK(cell(list, 0, 0) == "bad" && cell(list, 1, 0) == "once" && list.cell(1, 4) == Value(true));
	run("insertSeparator");
	CHECK(list.rowCount() == 5 && list.cell(1, 3) == Value(true) && cell(list, 1, 2).empty() && cell(list, 2, 0) == "once");
	CHECK(console.summary.get() == "4 scripts (1 active, 0 paused)");
	/* the session written as Chimera writes it */
	const fs::path session = scriptFolder() / "test.luases";
	CHECK(run("saveSessionAs", {session.string()}).has_value() && !console.changes.get() && console.status.get() == "test.luases saved.");
	{
		std::ifstream in(session, std::ios::binary);
		std::stringstream ss;
		ss << in.rdbuf();
#ifdef _WIN32
		CHECK(ss.str() == "0 .\\bad.lua\r\n---\r\n0 .\\once.lua\r\n1 .\\loop.lua\r\n0 .\\hooks.lua\r\n");
#else
		CHECK(ss.str() == "0 ./bad.lua\n---\n0 ./once.lua\n1 ./loop.lua\n0 ./hooks.lua\n");
#endif
	}
	/* a new session: every script stopped, the list empty */
	run("newSession");
	CHECK(list.rowCount() == 0 && !console.running.get() && console.lines.get() == 0 && console.sessionPath.get().empty());
	/* read back: the scripts marked to run started, the output empty, as Chimera's */
	run("openSession", {session.string()});
	CHECK(list.rowCount() == 5 && cell(list, 3, 0) == "loop" && cell(list, 3, 2) == "running" && cell(list, 0, 2) == "stopped");
	CHECK(console.lines.get() == 0 && console.status.get() == "test.luases" && !console.changes.get());
	CHECK(console.recentSessions.get() == session.string() && console.recentScripts.get().find(hooks) != std::string::npos);
	/* the typed line: values tab between, a table shallowly a key a line, (no return) when nothing came back */
	run("execute", {std::string("1 + 1, 'x'")});
	CHECK(lastLine() == "2\tx");
	run("execute", {std::string("{10, x = 'y'}")});
	CHECK(lineBack(2) == "1: 10" && lineBack(1) == "\"x\": \"y\"" && lastLine().empty());
	run("execute", {std::string("local z = 1")});
	CHECK(lastLine() == "(no return)");
	run("execute", {std::string("print('hi')")});
	CHECK(lastLine() == "hi");
	run("execute", {std::string("emu.frameadvance()")});
	CHECK(lastLine() == "emu.frameadvance() can not be called from the console");
	run("execute", {std::string("syntax error here")});
	CHECK(lastLine().find("input") != std::string::npos);
	/* Up and Down through the lines typed */
	CHECK(run("recall", {int64_t{1}}) == Value(std::string("syntax error here")));
	CHECK(run("recall", {int64_t{1}}) == Value(std::string("emu.frameadvance()")));
	CHECK(run("recall", {int64_t{-1}}) == Value(std::string("syntax error here")));
	CHECK(run("recall", {int64_t{-1}}) == Value(std::string()));
	/* console.write writes no line end */
	run("execute", {std::string("console.write('a') console.write('b') console.log('c')")});
	CHECK(lastLine() == "abc");
	/* past 100 writes at once, the rest is not shown */
	run("execute", {std::string("for i = 1, 150 do print(i) end")});
	CHECK(lineBack(2) == "100" && lastLine() == "Flood warning! Message cap reached, suppressing output.");
	/* the output's lines picked and copied */
	const int64_t last = out.rowCount() - 1;
	run("selectOutput", {last - 2, std::string("only")});
	run("selectOutput", {last, std::string("range")});
	CHECK(run("copyOutput") == Value(std::string("100\n\nFlood warning! Message cap reached, suppressing output.\n")));
	CHECK(out.cell(last - 1, 1) == Value(true) && out.cell(last - 3, 1) == Value(false));
	/* sorted by name, then the other way round */
	run("sort", {std::string("Script")});
	CHECK(cell(list, 1, 0) == "bad" && cell(list, 4, 0) == "once");
	run("sort", {std::string("Script")});
	CHECK(cell(list, 0, 0) == "once" && cell(list, 3, 0) == "bad");
	/* a new script from the template, made in the Lua folder; a copy beside the first */
	console.luaFolder.set((scriptFolder() / "Lua").string());
	const fs::path fresh = scriptFolder() / "fresh.lua";
	CHECK(run("newScript", {fresh.string()}) == Value(fresh.string()));
	CHECK(fs::exists(scriptFolder() / "Lua" / ".template.lua") && fs::file_size(fresh) == fs::file_size(scriptFolder() / "Lua" / ".template.lua"));
	CHECK(list.rowCount() == 6 && cell(list, 5, 0) == "fresh" && cell(list, 5, 2) == "stopped");
	run("select", {int64_t{5}, std::string("only")});
	CHECK(run("duplicateName") == Value((scriptFolder() / "fresh (1).lua").string()));
	run("duplicate", {(scriptFolder() / "fresh (1).lua").string()});
	CHECK(list.rowCount() == 7 && cell(list, 6, 0) == "fresh (1)" && fs::exists(scriptFolder() / "fresh (1).lua"));
	/* taken out: stopped and gone */
	int64_t loopRow = -1;
	for (int64_t i = 0; i < list.rowCount(); i++)
		if (cell(list, i, 0) == "loop") loopRow = i;
	run("select", {loopRow, std::string("only")});
	run("remove");
	CHECK(list.rowCount() == 6 && !console.running.get() && console.changes.get());
	/* a script's code runs in its own folder, as Chimera's: dofile and io.open find files beside it; the program's folder is back after */
	const fs::path before = fs::current_path();
	std::ofstream(scriptFolder() / "helper.lua", std::ios::binary) << "helped = 41 + 1";
	run("openScript", {scriptFile("dofile('helper.lua') print('helped', helped) local f = io.open('beside.txt', 'w') f:write('here') f:close()", "folder")});
	CHECK(lastLine() == "helped\t42" && fs::exists(scriptFolder() / "beside.txt") && fs::current_path() == before);
	/* closed: its OnConsoleClose functions called, every script stopped, the list and output emptied */
	run("openScript", {hooks});
	run("execute", {std::string("closing = false event.onconsoleclose(function() closing = true end)")});
	run("closed");
	CHECK(list.rowCount() == 0 && console.lines.get() == 0 && !console.running.get() && console.sessionPath.get().empty());
	run("execute", {std::string("closing")});
	CHECK(lastLine() == "True");
	/* Restore Defaults: Chimera's settings back */
	console.disableOnLoad.set(true);
	console.toggleAllIfNoneSelected.set(false);
	console.reloadOnChange.set(true);
	run("restoreDefaults");
	CHECK(!console.disableOnLoad.get() && console.toggleAllIfNoneSelected.get() && !console.reloadOnChange.get());
	/* Help > Lua Functions List: its documentation's words, the window's
	 * functions found in it, those the window lacks marked unavailable,
	 * deprecated ones marked, undocumented ones besides */
	{
		json::Json listed;
		std::string why;
		CHECK(json::parse(std::get<std::string>(*run("functionsList")), listed, why));
		auto find = [&](const std::string &library, const std::string &name) -> const json::Json * {
			for (const json::Json &e : listed.items)
				if (e.find("library")->text == library && e.find("name")->text == name) return &e;
			return nullptr;
		};
		const json::Json *onframeend = find("event", "onframeend");
		CHECK(onframeend != nullptr && onframeend->find("returns")->text == "string" &&
			onframeend->find("params")->text == "(function luaf, [string name = nil])" &&
			onframeend->find("description")->text.rfind("Calls the given lua function at the end of each frame", 0) == 0 &&
			!onframeend->find("unavailable")->boolean);
		const json::Json *range = find("memory", "readbyterange");
		CHECK(range != nullptr && range->find("deprecated")->boolean && range->find("returns")->text == "zero-based table");
		/* a canvas's functions, which no global table holds, are listed as here */
		const json::Json *canvas = find("LuaCanvas", "DrawText");
		CHECK(canvas != nullptr && !canvas->find("unavailable")->boolean);
		/* one with no documentation is listed too */
		const std::string mixed = LuaConsoleViewModel::functionsList(R"x([{"library": "extra", "name": "thing", "params": "a, b"}])x");
		const bool own = mixed.find(R"x("library":"extra","name":"thing","params":"(a, b)")x") != std::string::npos ||
						 mixed.find(R"x("library": "extra", "name": "thing", "params": "(a, b)")x") != std::string::npos;
		CHECK(own);
		/* Register To Text Editors: Chimera's completions for Sublime Text 2, written into its Lua package */
		const std::string sublime = LuaConsoleViewModel::sublimeCompletions();
		CHECK(sublime.rfind(R"x({"scope":"source.lua - string","completions":[{"trigger":")x", 0) == 0);
		CHECK(sublime.find(R"x({"trigger":"joypad.setanalog","contents":"joypad.setanalog(${1:controls},${2:[controller]})"})x") != std::string::npos);
		namespace fs = std::filesystem;
		const fs::path appdata = fs::temp_directory_path() / ("chimera-test-sublime-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		console.appDataFolder.set(appdata.string());
		std::string error;
		/* a fresh install has no Lua package folder: it is made (Chimera 8850b65) */
		CHECK(!fs::exists(appdata / "Sublime Text 2" / "Packages" / "Lua"));
		CHECK(console.run("registerSublime", {}, error).has_value());
		CHECK(readFile(appdata / "Sublime Text 2" / "Packages" / "Lua" / "chimera.lua.sublime-completions") == sublime);
		fs::remove_all(appdata);
	}
}

/* Every tool window's Settings items, as Chimera's: Save Window Position on
 * and the others off at first; the place kept only while Save Window
 * Position is on; Restore Defaults forgets the tool's own */
void toolWindows()
{
	ManualDispatcher d;
	ToolWindowsViewModel tw(d);
	std::string error;
	CHECK(tw.option("luaConsole", "save") && !tw.option("luaConsole", "top") && !tw.option("luaConsole", "float"));
	CHECK(tw.run("place", {std::string("luaConsole"), int64_t{10}, int64_t{20}, int64_t{700}, int64_t{400}}, error) == Value(true));
	CHECK(tw.settings.get() == "{\"luaConsole\": {\"x\": 10, \"y\": 20, \"w\": 700, \"h\": 400}}");
	CHECK(tw.run("setOption", {std::string("luaConsole"), std::string("top"), true}, error).has_value() && tw.option("luaConsole", "top"));
	CHECK(tw.run("setOption", {std::string("tastudio"), std::string("float"), true}, error).has_value() && tw.option("tastudio", "float"));
	/* Save Window Position off: the place is forgotten, and no longer kept */
	tw.run("setOption", {std::string("luaConsole"), std::string("save"), false}, error);
	CHECK(tw.settings.get() == "{\"luaConsole\": {\"top\": true, \"save\": false}, \"tastudio\": {\"float\": true}}");
	CHECK(tw.run("place", {std::string("luaConsole"), int64_t{1}, int64_t{2}, int64_t{3}, int64_t{4}}, error) == Value(false));
	CHECK(!tw.run("setOption", {std::string("luaConsole"), std::string("wide"), true}, error) && error.find("no option wide") != std::string::npos);
	/* files dropped, sorted as Chimera's: one of a kind opens, in its order (the game first, the project last);
	 * several of a kind open none; a state or cheat file is said not to open by dropping */
	CHECK(tw.run("sortFiles", {std::string("/a/run.chimeraProject\n/a/s.LUA\n/a/game.nes\n/a/w.wch\n/a/x.luases\n")}, error) ==
		Value(std::string("open\tgame\t/a/game.nes\nopen\twatch\t/a/w.wch\nopen\tluaSession\t/a/x.luases\nopen\tlua\t/a/s.LUA\n"
						  "open\tproject\t/a/run.chimeraProject\n")));
	CHECK(tw.run("sortFiles", {std::string("/a/1.lua\n/a/2.lua\n/a/q.chimerastate")}, error) ==
		Value(std::string("say\tq.chimerastate: Chimera does not open this kind of file by dropping it\n"
						  "say\t2 files of one kind were dropped; drop them one at a time\n")));
	/* Restore Defaults: the tool's own options and place gone, the others' kept */
	tw.run("restoreDefaults", {std::string("luaConsole")}, error);
	CHECK(tw.settings.get() == "{\"tastudio\": {\"float\": true}}" && tw.option("luaConsole", "save") && !tw.option("luaConsole", "top"));
}

/* Firmware is found by its hash when one is pinned (the project's first),
 * else by its name, else by the one file its size and name hints point at;
 * and only what the settings call for is asked for at all */
void firmware()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-fw-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "sub");
	auto write = [&](const fs::path &p, const std::string &bytes) { std::ofstream(p, std::ios::binary) << bytes; };
	write(dir / "anything.rom", "the pinned bios");
	write(dir / "sub" / "BIOS.BIN", "named");
	write(dir / "NDS_Bios7i.bin", std::string(16, 'a'));
	write(dir / "custom.rom", "a custom bios");
	char sha[41], custom[41];
	ce_sha1_hex(reinterpret_cast<const uint8_t *>("the pinned bios"), 15, sha);
	ce_sha1_hex(reinterpret_cast<const uint8_t *>("a custom bios"), 13, custom);
	const auto index = indexFirmware({dir.string()});
	CHECK(index.size() == 4);
	auto decl = [](const std::string &text) {
		json::Json j;
		std::string e;
		json::parse(text, j, e);
		return j;
	};
	std::string how;
	const FirmwareFile *f = findFirmware(decl(std::string(R"({"id":"a","sha1":")") + sha + "\"}"), "", index, how);
	CHECK(f != nullptr && f->name == "anything.rom" && how == "by its hash");
	f = findFirmware(decl(std::string(R"({"id":"a","sha1":")") + sha + "\"}"), custom, index, how);
	CHECK(f != nullptr && f->name == "custom.rom" && how == "by the hash the project pins");
	CHECK(findFirmware(decl(R"({"id":"a","sha1":"0000000000000000000000000000000000000000","name":"BIOS.BIN"})"), "", index, how) == nullptr);
	f = findFirmware(decl(R"({"id":"b","name":"bios.bin"})"), "", index, how);
	CHECK(f != nullptr && f->name == "BIOS.BIN" && how == "by its name");
	f = findFirmware(decl(R"({"id":"c","size":16,"nameHints":["7i"]})"), "", index, how);
	CHECK(f != nullptr && f->name == "NDS_Bios7i.bin" && how == "by its size and name");
	write(dir / "other_7i.bin", std::string(16, 'b'));
	CHECK(findFirmware(decl(R"({"id":"c","size":16,"nameHints":["7i"]})"), "", indexFirmware({dir.string()}), how) == nullptr);
	/* two different dumps the hints point at: both candidates, for the person to choose (issue 349) */
	CHECK(hintCandidates(decl(R"({"id":"c","size":16,"nameHints":["7i"]})"), indexFirmware({dir.string()})).size() == 2);
	CHECK(hintCandidates(decl(R"({"id":"c","size":16,"nameHints":["7i"],"sha1":"00"})"), indexFirmware({dir.string()})).empty());
	/* a name and a size declared: the file of that name and that size, not
	 * another machine's of the same name found first (issue 379: the PC-88's
	 * font.rom taken for the PC-98's FONT.ROM) */
	{
		const fs::path fonts = dir.string() + "-fonts";
		fs::create_directories(fonts / "pc88");
		fs::create_directories(fonts / "pc98");
		write(fonts / "pc88" / "font.rom", std::string(2048, 'x'));
		write(fonts / "pc98" / "font.rom", std::string(4096, 'y'));
		const auto both = indexFirmware({(fonts / "pc88").string(), (fonts / "pc98").string()});
		f = findFirmware(decl(R"({"id":"FONT.ROM","name":"FONT.ROM","size":4096,"nameHints":["font"]})"), "", both, how);
		CHECK(f != nullptr && f->size == 4096 && how == "by its name");
		/* only the other machine's: not found, for the person to choose */
		CHECK(findFirmware(decl(R"({"id":"FONT.ROM","name":"FONT.ROM","size":4096})"), "", indexFirmware({(fonts / "pc88").string()}), how) == nullptr);
		std::error_code ec;
		fs::remove_all(fonts, ec);
	}
	{
		/* said in the Firmware window as Chimera says it */
		FirmwareViewModel::Row row;
		row.candidates = 3;
		CHECK(row.statusText() == "3 candidates - choose one with Set...");
		row.candidates = 1;
		CHECK(row.statusText() == "not found");
	}
	/* the engine says what is needed: here only with Load BIOS on */
	const json::Json fwDecl = decl(R"([{"id":"b","display":"The BIOS","name":"bios.bin","requiredWhen":{"setting":"loadBios","is":true}}])");
	const json::Json setDecl = decl(R"([{"name":"loadBios","type":"bool","default":false}])");
	json::Json none, on, noPins;
	none.kind = json::Json::Kind::Object;
	json::parse(R"({"loadBios":true})", on, how);
	CHECK(neededFirmware(fwDecl, setDecl, "{}", none, noPins, index).empty());
	const auto needs = neededFirmware(fwDecl, setDecl, "{}", on, noPins, index);
	CHECK(needs.size() == 1 && needs[0].id == "b" && needs[0].path.find("BIOS.BIN") != std::string::npos && needs[0].sha1.size() == 40);
	CHECK(needs[0].required);
	/* entries that share an id, one for each version a setting picks: the
	 * one the setting's value calls for, its default when nothing is chosen
	 * (issue 390), never another with the same id */
	{
		const json::Json versions = decl(R"([{"id":"player","display":"Player 32","sha1":"AAAA","requiredWhen":{"setting":"build","is":"32"}},
			{"id":"player","display":"Player 24","sha1":"BBBB","requiredWhen":{"setting":"build","is":"24"}}])");
		const json::Json buildDecl = decl(R"([{"name":"build","type":"enum","options":["32","24"],"default":"32"}])");
		json::Json old;
		json::parse(R"({"build":"24"})", old, how);
		const auto standard = neededFirmware(versions, buildDecl, "{}", none, noPins, index);
		const auto older = neededFirmware(versions, buildDecl, "{}", old, noPins, index);
		CHECK(standard.size() == 1 && standard[0].declIndex == 0 && standard[0].display == "Player 32");
		CHECK(older.size() == 1 && older[0].declIndex == 1 && older[0].display == "Player 24");
	}
	/* one the package says the core starts without, not found: listed, and
	 * not required, so an open does not refuse for it (as Chimera's) */
	{
		const auto optional = neededFirmware(decl(R"([{"id":"eeprom","name":"eeprom.bin","size":256,"required":false}])"), setDecl, "{}", none, noPins, index);
		CHECK(optional.size() == 1 && optional[0].path.empty() && !optional[0].required);
	}
	removeTree(dir);
}

/* Numbers as JSON writes them (review, 2026-10-26): its grammar, not only
 * its characters; a dot whatever the locale; no NaN; a whole number too big
 * for 64 bits read without ending the program */
void jsonNumbers()
{
	auto parses = [](const std::string &text) {
		json::Json j;
		std::string e;
		return json::parse(text, j, e);
	};
	CHECK(parses("[0, -1, 1.5, 2e3, -0.25E-2]"));
	CHECK(!parses("[-]") && !parses("[1-2]") && !parses("[01]") && !parses("[1.]") && !parses("[.5]") && !parses("[1e]") && !parses("[+1]"));
	CHECK(json::Json::number(0.1).text == "0.1" && json::Json::number(0.5).number() == 0.5);
	CHECK(json::Json::number(std::nan("")).kind == json::Json::Kind::Null);
	CHECK(json::Json::number(HUGE_VAL).kind == json::Json::Kind::Null);
	/* under a decimal comma, where the system has one */
	const std::string was = std::setlocale(LC_NUMERIC, nullptr);
	if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr || std::setlocale(LC_NUMERIC, "de_DE") != nullptr)
	{
		json::Json j;
		std::string e;
		CHECK(json::Json::number(0.5).text == "0.5");
		CHECK(json::parse("2.25", j, e) && j.number() == 2.25);
		std::setlocale(LC_NUMERIC, was.c_str());
	}
	else
		std::printf("SKIP jsonNumbers: no decimal-comma locale here\n");
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-json-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	const std::string file = (dir / "settings.json").string();
	std::ofstream(file) << R"({"sample.count": 99999999999999999999})";
	{
		ManualDispatcher d;
		ConfigStore store(file);
		Sample vm(d);
		store.keep(vm, "count");
		CHECK(store.problem().empty());
	}
	removeTree(dir);
}

/* What the window remembers: a property kept by its name comes back in the
 * next run; a file that does not read is set aside, not written over */
void configStore()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-config-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const std::string file = (dir / "settings.json").string();
	ManualDispatcher d;
	/* a switch read before anything is made: what the file says, else the default */
	CHECK(ConfigStore::switchIn(file, "customize.singleInstance", true) && !ConfigStore::switchIn(file, "customize.singleInstance", false));
	{
		ConfigStore store(file);
		Sample vm(d);
		store.keep(vm, "count");
		vm.count.set(42);
		CHECK(store.problem().empty() && fs::exists(file));
	}
	{
		std::ofstream(file + ".switches") << R"({"customize.singleInstance": true, "customize.gameCubeAdapter": false, "count": 3})";
		CHECK(ConfigStore::switchIn(file + ".switches", "customize.singleInstance", false));
		CHECK(!ConfigStore::switchIn(file + ".switches", "customize.gameCubeAdapter", true));
		/* not a switch: the default */
		CHECK(ConfigStore::switchIn(file + ".switches", "count", true) && !ConfigStore::switchIn(file + ".switches", "missing", false));
	}
	{
		ConfigStore store(file);
		Sample vm(d);
		store.keep(vm, "count");
		CHECK(vm.count.get() == 42);
		/* Chimera's Save Config As and Load Config From, through the config menu's view model */
		ConfigFilesViewModel menu(d, store);
		std::string error;
		const std::string copy = (dir / "copies" / "mine.json").string();
		CHECK(menu.run("saveAs", {copy}, error).has_value() && menu.status.get() == "Settings saved as " + copy);
		vm.count.set(7);
		CHECK(menu.run("loadFrom", {copy}, error) == Value(int64_t{1}) && vm.count.get() == 42);
		/* and so kept as this window's own */
		{
			std::ifstream in(file, std::ios::binary);
			const std::string kept((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			CHECK(kept.find("\"sample.count\": 42") != std::string::npos);
		}
		/* the file changed by someone else: Load Config reads it again */
		std::ofstream(file, std::ios::binary | std::ios::trunc) << R"({"sample.count": 5})";
		CHECK(menu.run("load", {}, error) == Value(int64_t{1}) && vm.count.get() == 5);
		std::ofstream(dir / "bad.json") << "[1, 2]";
		CHECK(!menu.run("loadFrom", {(dir / "bad.json").string()}, error) && error.rfind((dir / "bad.json").string() + " is not a settings file", 0) == 0);
		CHECK(!menu.run("loadFrom", {(dir / "none.json").string()}, error) && error == "cannot read " + (dir / "none.json").string());
		CHECK_TEXT(store.problem(), "");
		CHECK(menu.run("save", {}, error) == Value(file) && menu.status.get() == "Settings saved in " + file);
	}
	std::ofstream(file, std::ios::binary | std::ios::trunc) << "{ not json";
	{
		ConfigStore store(file);
		CHECK(!store.problem().empty() && fs::exists(file + ".unreadable") && !fs::exists(file));
	}
	removeTree(dir);
}

/* the names client.getconfig gives the window's settings: each once, and
 * Lua's list a line each */
void configNamesList()
{
	std::vector<std::string> names;
	for (const ConfigName &s : configNames()) names.push_back(s.name);
	std::sort(names.begin(), names.end());
	CHECK(std::adjacent_find(names.begin(), names.end()) == names.end() && names.size() == 119);
	const std::string forLua = configNamesText();
	CHECK(std::count(forLua.begin(), forLua.end(), '\n') == 119);
}

void bindings()
{
	const std::string json = R"({
		"_comment": ["x"],
		"AllTrollers": {
			"Other Pad": { "P1 Up": "W" },
			"Synth Controller": { "P1 Up": "Up, J1 POV0U", "P1 A": "X", "P1 B": "Z, X", "P2 A": "Q" }
		}
	})";
	Bindings b;
	std::string error;
	CHECK(b.parse(json, {"P1 Up", "P1 A", "P1 B"}, error));
	CHECK(b.buttonsOf("Up").size() == 1 && b.buttonsOf("Up")[0] == 0);
	CHECK(b.buttonsOf("J1 POV0U").size() == 1);
	CHECK(b.buttonsOf("X").size() == 2); /* X holds A and B */
	CHECK(b.buttonsOf("W").empty());      /* the section that names fewer buttons is not taken */
	CHECK(b.buttonsOf("Q").empty());      /* a button the machine does not have */
	CHECK(!b.parse("{\"a\": [1, 2", {"P1 Up"}, error) && !error.empty());
	/* the autofire keys are their own group; the buttons are only AllTrollers' */
	const std::string groups = R"({
		"AllTrollers": { "Synth Controller": { "P1 A": "X" } },
		"AllTrollersAutoFire": { "Synth Controller": { "P1 A": "Q" } },
		"AllTrollersAnalog": { "Synth Controller": {
			"P1 Dial": { "Value": "X1 LeftThumbX Axis", "Mult": -0.5, "Deadzone": 0.2, "ButtonBindPositive": "L", "ButtonBindNegative": null } } }
	})";
	/* machines of one package with the same button names: the machine's own
	 * controller's section, as Chimera's, not the first that names as many */
	const std::string twoMachines = R"({
		"AllTrollers": { "Atari 7800 Controls": { "P1 Up": "Up", "Pause": "F3, X1 RightShoulder" },
		                 "Amstrad GX4000 Controls": { "P1 Up": "Up", "Pause": "F3, X1 Start" } },
		"AllTrollersAnalog": { "Atari 7800 Controls": { "P1 Dial": { "Value": "X1 LeftThumbX Axis" } },
		                       "Amstrad GX4000 Controls": { "P1 Dial": { "Value": "X1 RightThumbX Axis" } } }
	})";
	Bindings own;
	CHECK(own.parse(twoMachines, {"P1 Up", "Pause"}, "AllTrollers", error, "Amstrad GX4000 Controls") &&
		  own.buttonsOf("X1 Start").size() == 1 && own.buttonsOf("X1 RightShoulder").empty());
	CHECK(parseAnalogBinds(twoMachines, {"P1 Dial"}, error, "Amstrad GX4000 Controls")[0].value == "X1 RightThumbX Axis");
	/* a controller the file has no section for: the one that names most of its buttons */
	CHECK(own.parse(twoMachines, {"P1 Up", "Pause"}, "AllTrollers", error, "Another Controller") && own.buttonsOf("X1 RightShoulder").size() == 1);
	Bindings fire;
	CHECK(b.parse(groups, {"P1 A"}, error) && b.buttonsOf("X").size() == 1 && b.buttonsOf("Q").empty());
	CHECK(fire.parse(groups, {"P1 A"}, "AllTrollersAutoFire", error) && fire.buttonsOf("Q").size() == 1 && fire.buttonsOf("X").empty());
	/* the analog bindings, by the machine's analog controls; one the file does not name is unbound */
	const std::vector<AnalogBind> analog = parseAnalogBinds(groups, {"P1 Dial", "P1 Wheel"}, error);
	CHECK(analog.size() == 2 && analog[0].value == "X1 LeftThumbX Axis" && analog[0].mult == -0.5 && analog[0].deadzone == 0.2 &&
		  analog[0].positive == "L" && analog[0].negative.empty() && analog[1].empty() && analog[1].mult == 1.0 && analog[1].deadzone == 0.1);
	AnalogBind back;
	CHECK(analogBindFrom(analogBindJson(analog[0]), back) && back.value == analog[0].value && back.mult == -0.5 && back.positive == "L");
	/* Chimera's arithmetic: a key to an end, the axis past the dead zone and scaled back up, around the rest value, kept in range */
	bool active = false;
	AnalogBind dial;
	dial.mult = 1.0;
	dial.deadzone = 0.0;
	CHECK(analogValue(dial, 0, true, false, 0, 255, 128, active) == 255 && active);
	CHECK(analogValue(dial, 0, false, true, 0, 255, 128, active) == 0 && active);
	CHECK(analogValue(dial, 5000, false, false, 0, 255, 128, active) == 192 && active);
	dial.deadzone = 0.1;
	CHECK(analogValue(dial, 900, false, false, -100, 100, 0, active) == 0 && !active);
	CHECK(analogValue(dial, -5000, false, false, -100, 100, 0, active) == -44 && active);
}

/* Chimera's Key Priority, for a key that is a hotkey and a button: both
 * act, the button wins, or the hotkey wins - each giving way only for a
 * key the other side has; and its hotkey's cycle */
void keyPriority()
{
	auto route = [](int64_t p, bool hotkey, bool control) {
		const HotkeysViewModel::Route r = HotkeysViewModel::route(p, hotkey, control);
		return std::string(r.hotkey ? "H" : "-") + (r.control ? "C" : "-");
	};
	/* both: each side gets it */
	CHECK(route(0, true, true) == "HC" && route(0, true, false) == "HC" && route(0, false, true) == "HC");
	/* buttons first: a button's key is not a hotkey's; any other key still is */
	CHECK(route(1, true, true) == "-C" && route(1, true, false) == "HC" && route(1, false, true) == "-C");
	/* hotkeys first: a hotkey's key is not a button's; any other key still is */
	CHECK(route(2, true, true) == "H-" && route(2, false, true) == "HC" && route(2, true, false) == "H-");
	ManualDispatcher d;
	HotkeysViewModel hotkeys(d);
	hotkeys.add("General", "Pause", "Pause", [](bool) {});
	CHECK(hotkeys.boundTo("Pause") && !hotkeys.boundTo("X"));
	std::string error;
	CHECK(hotkeys.run("cycleKeyPriority", {}, error) == Value(int64_t{1}) && hotkeys.status.get() == "Key priority set to Input over Hotkey");
	CHECK(hotkeys.run("cycleKeyPriority", {}, error) == Value(int64_t{2}) && hotkeys.status.get() == "Key priority set to Hotkey over Input");
	CHECK(hotkeys.run("cycleKeyPriority", {}, error) == Value(int64_t{0}) && hotkeys.status.get() == "Key priority set to Both Hotkey and Input");
	hotkeys.noMixedPriority.set(true);
	hotkeys.keyPriority.set(2);
	CHECK(hotkeys.run("cycleKeyPriority", {}, error) == Value(int64_t{1}));
}

/* Customize's Merge L+R modifier keys: when it changes, Chimera's rewrite
 * of the keys bound turns each side's name into the kind's, or the kind's
 * into the left's, in every key of a list and part of a combination, for
 * the hotkeys and every core's controls remembered */
void modifierRewrite()
{
	const auto merge = CustomizeViewModel::modifierKeys(true), separate = CustomizeViewModel::modifierKeys(false);
	CHECK(replaceKeyNames("LeftShift", merge) == "Shift");
	/* both sides one: once; the spaces as they were */
	CHECK(replaceKeyNames("Shift, LeftShift, RightShift, Z", merge) == "Shift, Z");
	CHECK(replaceKeyNames("LeftCtrl+S,RightAlt+X1 A", merge) == "Ctrl+S,Alt+X1 A");
	CHECK(replaceKeyNames("Shift+F1", separate) == "LeftShift+F1");
	CHECK(replaceKeyNames("Ctrl+Alt+Shift+X, Z", separate) == "LeftCtrl+LeftAlt+LeftShift+X, Z");
	CHECK(replaceKeyNames("LeftWin, J1 B1", merge) == "LeftWin, J1 B1");
	CHECK(replaceKeyNames("", merge).empty());
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	CustomizeViewModel customize(d, session);
	CHECK(customize.mergeModifiers.get() && !customize.alternateLayouts.get());
	std::string error;
	const Value names = customize.run("modifierKeys", {Value(false)}, error).value_or(Value());
	CHECK(error.empty() && names == Value(std::string(R"({"Alt": "LeftAlt", "Ctrl": "LeftCtrl", "Shift": "LeftShift"})")));
	/* the hotkeys: what differs from Chimera's is remembered */
	HotkeysViewModel hotkeys(d);
	hotkeys.add("Save States", "Save State 1", "Shift+F1", [](bool) {});
	hotkeys.add("General", "Pause", "Pause", [](bool) {});
	hotkeys.run("replaceKeys", {names}, error);
	CHECK(error.empty() && hotkeys.saved.get() == R"({"Save State 1": "LeftShift+F1"})");
	CHECK(hotkeys.boundTo("LeftShift+F1") && !hotkeys.boundTo("Shift+F1"));
	const Value merging = customize.run("modifierKeys", {Value(true)}, error).value_or(Value());
	hotkeys.run("replaceKeys", {merging}, error);
	CHECK(error.empty() && hotkeys.saved.get() == "{}" && hotkeys.boundTo("Shift+F1"));
	/* the ROM hotkeys became the project's (owner, 2026-10-26): a binding
	 * saved under an old name is the new hotkey's */
	{
		HotkeysViewModel renamed(d);
		renamed.add("General", "Open Project", "Ctrl+O", [](bool) {});
		renamed.add("General", "Load Last Project", "", [](bool) {});
		renamed.saved.set(R"({"Open ROM": "Ctrl+P", "Load Last ROM": "F11"})");
		CHECK(renamed.boundTo("Ctrl+P") && !renamed.boundTo("Ctrl+O") && renamed.boundTo("F11"));
	}
	/* the window's copy opened, a key pressed, the copy let go: the rows
	 * change in place, not made again, which cost the window over a
	 * second each time (issue 388); an action added makes them again */
	{
		RowModel *rows = nullptr;
		for (const auto &r : hotkeys.rowLists())
			if (r.name == "hotkeys") rows = r.model;
		CHECK(rows != nullptr && rows->rowCount() == 2);
		int resets = 0, changes = 0;
		auto resetWatch = rows->reset.connect([&] { resets++; });
		auto changeWatch = rows->changed.connect([&](int64_t first, int64_t last) { changes += first == 0 && last == 1; });
		hotkeys.run("edit", {}, error);
		hotkeys.run("press", {std::string("Pause"), std::string("P")}, error);
		hotkeys.run("cancel", {}, error);
		CHECK(error.empty() && resets == 0 && changes == 3);
		hotkeys.add("General", "Rewind", "Shift+R", [](bool) {});
		CHECK(resets == 1 && rows->rowCount() == 3);
	}
	/* every core's controls remembered, with no machine open */
	ControlsViewModel controls(d, session);
	controls.saved.set(R"({"Synth":{"P1 A":"LeftShift, Z"},"Other":{"P1 B":"RightCtrl+X"}})");
	controls.savedAutofire.set(R"({"Synth":{"P1 A":"RightShift"}})");
	controls.savedAnalog.set(R"({"Synth":{"P1 Stick X":{"Value":"","Mult":1.0,"Deadzone":0.1,"ButtonBindPositive":"RightAlt","ButtonBindNegative":"Left"}}})");
	controls.run("replaceKeys", {merging}, error);
	CHECK(error.empty());
	CHECK(controls.saved.get() == R"({"Synth": {"P1 A": "Shift, Z"}, "Other": {"P1 B": "Ctrl+X"}})");
	CHECK(controls.savedAutofire.get() == R"({"Synth": {"P1 A": "Shift"}})");
	CHECK(controls.savedAnalog.get().find(R"("ButtonBindPositive": "Alt", "ButtonBindNegative": "Left")") != std::string::npos);
	controls.run("replaceKeys", {Value(std::string("[1]"))}, error);
	CHECK(!error.empty());
}

/* Chimera's Messages window: each thing over the picture at a corner and a
 * distance, Chimera's places at first; the four colours; how long a
 * message stays; a remembered value missing a place gains it */
void messagesConfig()
{
	ManualDispatcher d;
	MessagesViewModel messages(d);
	std::string error;
	auto at = [&](const std::string &name) {
		json::Json all;
		std::string e;
		json::parse(messages.positions.get(), all, e);
		const json::Json *p = all.find(name);
		if (p == nullptr) return std::string("none");
		return p->find("x")->text + "," + p->find("y")->text + "," + p->find("anchor")->text;
	};
	CHECK(at("Fps") == "0,0,0" && at("Lag Counter") == "0,42,0" && at("Messages") == "0,0,2" && at("Autohold") == "0,0,1" &&
		at("Game Time") == "0,14,1" && at("Watches") == "0,70,0");
	CHECK(messages.run("setPosition", {std::string("Fps"), int64_t{4}, int64_t{6}, int64_t{3}}, error).has_value() && at("Fps") == "4,6,3");
	CHECK(!messages.run("setPosition", {std::string("Clock"), int64_t{0}, int64_t{0}, int64_t{0}}, error) &&
		error == "nothing called Clock is drawn over the picture");
	CHECK(!messages.run("setPosition", {std::string("Fps"), int64_t{0}, int64_t{0}, int64_t{4}}, error));
	CHECK(messages.run("setColor", {std::string("alert"), std::string("#00ff00")}, error).has_value() && messages.alertColor.get() == "#FF00FF00");
	CHECK(!messages.run("setColor", {std::string("alert"), std::string("green")}, error) && error == "GREEN is not a colour: #AARRGGBB or #RRGGBB");
	CHECK(!messages.run("setColor", {std::string("border"), std::string("#FF000000")}, error));
	messages.messageSeconds.set(30);
	CHECK(messages.messageSeconds.get() == 10);
	CHECK(messages.run("resetDefaults", {}, error).has_value() && at("Fps") == "0,0,0" && messages.alertColor.get() == "#FFFF0000" &&
		messages.messageSeconds.get() == 2);
	/* remembered before Game Time was a place: the rest kept, Game Time at Chimera's */
	messages.positions.set(R"({"Fps": {"x": 9, "y": 9, "anchor": 1}})");
	CHECK(at("Fps") == "9,9,1" && at("Game Time") == "0,14,1");
}

/* Chimera's Display window: the picture's shape, bars or stretched, whole
 * steps, crop - worked out for a 128x120 picture the machine shows at 4:3 */
void displayFit()
{
	auto near = [](double a, double b) { return std::fabs(a - b) < 0.01; };
	auto same = [&](const DisplayFit::Rect &r, double x, double y, double w, double h) {
		return near(r.x, x) && near(r.y, y) && near(r.w, w) && near(r.h, h);
	};
	const double m160 = 160, m120 = 120;
	/* Chimera's prescaling: the user's whole step, then with smoothing the
	 * largest whole steps that fit where the picture is drawn */
	{
		DisplayFit p;
		p.aspectMode = 0;
		int x = 0, y = 0;
		p.prescaleSteps(256, 224, m160, m120, 800, 700, true, 8192, x, y);
		CHECK(x == 1 && y == 1);
		p.prescale = 2;
		p.prescaleSteps(256, 224, m160, m120, 800, 700, true, 8192, x, y);
		CHECK(x == 2 && y == 2);
		/* 800 x 700 holds 256 x 224 three times (768 x 672, letterboxed): 2 then 1 more */
		p.prescale = 1;
		p.autoPrescale = true;
		p.prescaleSteps(256, 224, m160, m120, 800, 700, true, 8192, x, y);
		CHECK(x == 3 && y == 3);
		/* not without smoothing */
		p.prescaleSteps(256, 224, m160, m120, 800, 700, false, 8192, x, y);
		CHECK(x == 1 && y == 1);
		/* stretched: each side its own */
		p.letterbox = false;
		p.prescaleSteps(256, 224, m160, m120, 1100, 500, true, 8192, x, y);
		CHECK(x == 4 && y == 2);
		/* a step that would pass the largest side is taken back */
		p.letterbox = true;
		p.autoPrescale = false;
		p.prescale = 16;
		p.prescaleSteps(1024, 512, m160, m120, 800, 700, true, 8192, x, y);
		CHECK(x == 8 && y == 8);
		/* the crop is not grown into: the part shown is what fits */
		p.prescale = 1;
		p.autoPrescale = true;
		p.cropLeft = 64;
		p.cropRight = 64;
		p.prescaleSteps(256, 224, m160, m120, 800, 700, true, 8192, x, y);
		CHECK(x == 3 && y == 3);
	}
	DisplayFit f;
	double w = 0, h = 0;
	/* the machine's shape: 160x120 at 1x, kept with bars */
	f.naturalSize(128, 120, m160, m120, w, h);
	CHECK(near(w, 160) && near(h, 120));
	CHECK(same(f.place(128, 120, m160, m120, 400, 300), 0, 0, 400, 300));
	CHECK(same(f.place(128, 120, m160, m120, 500, 300), 50, 0, 400, 300));
	/* whole steps only: twice, not two and a half */
	f.integerScale = true;
	CHECK(same(f.place(128, 120, m160, m120, 500, 300), 90, 30, 320, 240));
	/* smaller than 1x: shrunk freely */
	CHECK(same(f.place(128, 120, m160, m120, 80, 60), 0, 0, 80, 60));
	f.integerScale = false;
	/* stretched to the area; by whole steps of each side when asked */
	f.letterbox = false;
	CHECK(same(f.place(128, 120, m160, m120, 500, 300), 0, 0, 500, 300));
	f.integerScale = true;
	CHECK(same(f.place(128, 120, m160, m120, 500, 300), 10, 30, 480, 240));
	f.integerScale = false;
	f.letterbox = true;
	/* raw pixels, a size, a ratio */
	f.aspectMode = 0;
	f.naturalSize(128, 120, m160, m120, w, h);
	CHECK(near(w, 128) && near(h, 120));
	f.aspectMode = 2;
	f.customWidth = 256;
	f.customHeight = 224;
	f.naturalSize(128, 120, m160, m120, w, h);
	CHECK(near(w, 256) && near(h, 224));
	f.aspectMode = 3;
	f.ratioX = 16;
	f.ratioY = 9;
	f.naturalSize(128, 120, m160, m120, w, h);
	CHECK(near(w, 120 * 16.0 / 9.0) && near(h, 120));
	/* the size the machine says, not only its shape: a 640x400 picture
	 * declared 640x480 is 640x480 at 1x, as Chimera's (issue 414) */
	f.aspectMode = 1;
	f.naturalSize(640, 400, 640, 480, w, h);
	CHECK(near(w, 640) && near(h, 480));
	/* a crop: those pixels gone, the shape of what is left */
	f.aspectMode = 1;
	f.cropLeft = 16;
	f.cropTop = 16;
	const DisplayFit::Rect from = f.source(128, 120);
	CHECK(same(from, 16, 16, 112, 104));
	f.naturalSize(128, 120, m160, m120, w, h);
	CHECK(near(w, 140) && near(h, 104));
	/* a crop that would leave nothing leaves the picture whole */
	f.cropRight = 120;
	CHECK(same(f.source(128, 120), 0, 0, 128, 120));
	/* Chimera's extra paddings: the game's around the picture in its pixels,
	 * the surface scripts draw on that much larger; the client's free room in
	 * the window's pixels around it all */
	{
		DisplayFit p;
		p.aspectMode = 0;
		p.padLeft = 40;
		p.padRight = 60;
		p.padBottom = 20;
		p.surfaceSize(128, 120, m160, m120, w, h);
		CHECK(near(w, 228) && near(h, 140));
		CHECK(same(p.surface(128, 120, m160, m120, 456, 280), 0, 0, 456, 280));
		CHECK(same(p.place(128, 120, m160, m120, 456, 280), 80, 0, 256, 240));
		p.clientLeft = 10;
		p.clientTop = 6;
		p.clientRight = 10;
		p.clientBottom = 14;
		CHECK(same(p.surface(128, 120, m160, m120, 476, 300), 10, 6, 456, 280));
		CHECK(same(p.place(128, 120, m160, m120, 476, 300), 90, 6, 256, 240));
		/* a crop with them: the surface starts where the crop does */
		p.cropLeft = 8;
		p.surfaceSize(128, 120, m160, m120, w, h);
		CHECK(near(w, 220) && near(h, 140));
	}
	/* the view model's values, kept in range */
	ManualDispatcher d;
	DisplayViewModel display(d);
	/* what is drawn over the picture grows with the window at first, and a
	 * script or the Display window can turn that off (issue 401) */
	{
		std::string error;
		CHECK(display.get("osdFollowsWindow") && *display.get("osdFollowsWindow") == Value(true));
		CHECK(display.set("osdFollowsWindow", false, error) && !display.osdFollowsWindow.get());
		display.osdFollowsWindow.set(true);
	}
	display.aspectMode.set(9);
	display.customWidth.set(-5);
	CHECK(display.fit().aspectMode == 3 && display.fit().customWidth == 0 && display.fit().letterbox);
	{
		std::string error;
		CHECK(display.run("setPadding", {std::string("game"), int64_t{1}, int64_t{2}, int64_t{3}, int64_t{4}}, error).has_value());
		CHECK(display.run("setPadding", {std::string("client"), int64_t{5}, int64_t{6}, int64_t{7}, int64_t{8}}, error).has_value());
		CHECK(display.fit().padLeft == 1 && display.fit().padBottom == 4 && display.fit().clientLeft == 5 && display.fit().clientBottom == 8);
		CHECK(!display.run("setPadding", {std::string("emu"), int64_t{1}, int64_t{2}, int64_t{3}, int64_t{4}}, error));
	}
	/* Window Size, as Chimera's: each system's scale, 2 at first, 1 to 10, kept; the window follows the picture */
	{
		std::string error;
		CHECK(display.resizeWithPicture.get() && display.scaleFor("NES") == 2);
		CHECK(display.run("setScaleFor", {std::string("NES"), int64_t{3}}, error) == Value(int64_t{3}));
		CHECK(display.run("setScaleFor", {std::string("PSX"), int64_t{14}}, error) == Value(int64_t{10}));
		CHECK(display.scaleFor("NES") == 3 && display.scaleFor("PSX") == 10 && display.scaleFor("GB") == 2);
		CHECK(display.run("setScaleFor", {std::string("NES"), int64_t{0}}, error) == Value(int64_t{1}) && display.scaleFor("PSX") == 10);
	}
	/* the window's chrome starts as Chimera's: windowed with its menu, caption and thick frame; full
	 * screen bare, the pointer hidden when still; double click full screen; titles not static */
	CHECK(display.menuWindowed.get() && display.captionWindowed.get() && display.frameWindowed.get() == 2 &&
		  !display.menuFullscreen.get() && !display.statusBarFullscreen.get() && display.autohideMouseFullscreen.get() &&
		  display.allowDoubleClickFullscreen.get() && !display.stayOnTop.get() && !display.staticWindowTitles.get() &&
		  display.titleSuffix.get().empty());
	std::string error;
	CHECK(display.set("frameWindowed", Value(int64_t{0}), error) && display.frameWindowed.get() == 0);
}

/* Chimera's Paths window: folders relative to the base (the program's own
 * at first), changed ones remembered, a dialog opening in the nearest
 * folder that is there, the last game's folder for ROMs */
void pathsConfig()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-paths-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "program" / "lua");
	ManualDispatcher d;
	PathsViewModel paths(d);
	std::string error;
	int moved = 0;
	Connection watch = paths.moved.connect([&] { moved++; });
	paths.programFolder.set((dir / "program").string());
	CHECK(paths.resolve("Movie backups") == (dir / "program" / "movies" / "backup").string());
	CHECK(paths.resolve("ROM") == (dir / "program").string());
	CHECK(paths.resolve("Nothing").empty());
	/* Watch and Debug Logs under Tools, as Chimera resolves them */
	CHECK(paths.resolve("Tools") == (dir / "program" / "tools").string() && paths.resolve("Watch (.wch)") == (dir / "program" / "tools").string() &&
		paths.resolve("Debug Logs") == (dir / "program" / "tools").string());
	/* a base of the person's */
	paths.base.set((dir / "elsewhere").string());
	CHECK(paths.resolve("Lua") == (dir / "elsewhere" / "lua").string());
	paths.base.set("");
	/* a path changed: absolute, or relative to the base; Chimera's again is no change */
	std::string got;
	CHECK(fromValue(paths.run("setPath", {std::string("Movies"), (dir / "runs").string()}, error).value_or(Value()), got) &&
		got == (dir / "runs").string());
	CHECK(paths.changed.get() == "{\"Movies\": " + json::quote((dir / "runs").string()) + "}");
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("./Scripts")}, error).has_value() &&
		paths.resolve("Lua") == (dir / "program" / "Scripts").string());
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("./lua")}, error).has_value() && paths.changed.get().find("Lua") == std::string::npos);
	CHECK(!paths.run("setPath", {std::string("Saves"), std::string(".")}, error) && error == "no folder called Saves");
	CHECK(moved >= 5);
	/* the rows: as set, made absolute, whether Chimera's */
	RowModel *rows = paths.rowLists()[0].model;
	bool isDefault = true;
	std::string name, folder;
	for (int64_t i = 0; i < rows->rowCount(); i++)
	{
		fromValue(rows->cell(i, 1), name);
		if (name != "Movies") continue;
		fromValue(rows->cell(i, 3), folder);
		fromValue(rows->cell(i, 4), isDefault);
	}
	CHECK(folder == (dir / "runs").string() && !isDefault);
	/* Chimera's global list, in its order, the base first and temporary files last */
	fromValue(rows->cell(0, 1), name);
	CHECK(name == "Base" && rows->rowCount() == 14);
	fromValue(rows->cell(13, 1), name);
	CHECK(name == "Temp Files" && paths.resolve("Temp Files") == (dir / "program").string());
	/* Chimera's words in a path: the program's folder, the last game's, and anything else is the program's folder */
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("%exe%/Scripts")}, error).has_value() &&
		paths.resolve("Lua") == (dir / "program" / "Scripts").string());
	paths.lastRomFolder.set((dir / "games").string());
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("%rom%/lua")}, error).has_value() &&
		paths.resolve("Lua") == (dir / "games" / "lua").string());
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("Scripts")}, error).has_value() && paths.resolve("Lua") == (dir / "program").string());
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("%recent%")}, error).has_value() && !paths.resolve("Lua").empty());
	/* "..": the folder above the base, as Chimera's Special Commands say */
	CHECK(paths.run("setPath", {std::string("Lua"), std::string("../lua")}, error).has_value() && paths.resolve("Lua") == (dir / "lua").string());
	paths.run("setPath", {std::string("Lua"), std::string("")}, error);
	/* the base is a row too */
	CHECK(paths.run("setPath", {std::string("Base"), (dir / "elsewhere").string()}, error).has_value() &&
		paths.resolve("Movies") == (dir / "runs").string() && paths.resolve("Lua") == (dir / "elsewhere" / "lua").string());
	paths.run("setPath", {std::string("Base"), std::string(".")}, error);
	CHECK(paths.base.get().empty());
	/* a system's own folders, made when a game for it is opened: under its base, which is under the global one */
	paths.useSystem("NES");
	CHECK(paths.systems.get() == "[\"NES\"]" && rows->rowCount() == 18);
	CHECK(paths.resolveFor("NES", "Screenshots") == (dir / "program" / "nes" / "screenshots").string() &&
		paths.resolveFor("NES", "ROM") == (dir / "program" / "nes").string() &&
		paths.resolveFor("NES", "Savestates") == (dir / "program" / "nes" / "state").string());
	/* a kind it has not falls back to its base */
	CHECK(paths.resolveFor("NES", "Cheats") == (dir / "program" / "nes").string());
	CHECK(fromValue(paths.run("setSystemPath", {std::string("NES"), std::string("Screenshots"), std::string("./Shots")}, error).value_or(Value()), got) &&
		got == (dir / "program" / "nes" / "Shots").string());
	CHECK(paths.run("setSystemPath", {std::string("NES"), std::string("Base"), (dir / "nes").string()}, error).has_value() &&
		paths.resolveFor("NES", "Screenshots") == (dir / "nes" / "Shots").string());
	CHECK(!paths.run("setSystemPath", {std::string("NES"), std::string("Movies"), std::string(".")}, error) && error == "no folder called Movies");
	/* a folder picked, made relative to the base it is under, as Chimera's */
	CHECK(fromValue(paths.run("makeRelative", {(dir / "nes" / "pics").string(), std::string("NES")}, error).value_or(Value()), got) && got == "./pics");
	CHECK(fromValue(paths.run("makeRelative", {(dir / "program" / "Lua").string(), std::string("")}, error).value_or(Value()), got) && got == "./Lua");
	CHECK(fromValue(paths.run("makeRelative", {(dir / "other").string(), std::string("")}, error).value_or(Value()), got) && got == (dir / "other").string());
	paths.run("setSystemPath", {std::string("NES"), std::string("Base"), std::string("")}, error);
	paths.run("setSystemPath", {std::string("NES"), std::string("Screenshots"), std::string("")}, error);
	CHECK(paths.systemChanged.get() == "{}");
	/* a dialog opens in the folder when it is there, else the nearest above it; nothing is made */
	CHECK(fromValue(paths.run("dialogFolder", {std::string("Lua")}, error).value_or(Value()), got) && got == (dir / "program" / "lua").string());
	CHECK(fromValue(paths.run("dialogFolder", {std::string("Macros")}, error).value_or(Value()), got) && got == (dir / "program").string());
	CHECK(!fs::exists(dir / "program" / "movies"));
	/* ROMs: where the last game came from, while it is there and asked for */
	paths.lastRomFolder.set((dir / "program" / "lua").string());
	CHECK(fromValue(paths.run("dialogFolder", {std::string("ROM")}, error).value_or(Value()), got) && got == (dir / "program" / "lua").string());
	paths.recentForRoms.set(false);
	CHECK(fromValue(paths.run("dialogFolder", {std::string("ROM")}, error).value_or(Value()), got) && got == (dir / "program").string());
	/* a game picked in a dialog is where the last game came from (issue 402) */
	CHECK(paths.run("gameChosen", {(dir / "games" / "a game.nes").string()}, error).has_value() &&
		paths.lastRomFolder.get() == (dir / "games").string());
	/* Temp Files: nothing while it is not set (the system's own folder), the folder when it is */
	CHECK(paths.tempFolder().empty());
	CHECK(paths.run("setPath", {std::string("Temp Files"), std::string("./tmp")}, error).has_value() &&
		paths.tempFolder() == (dir / "program" / "tmp").string());
	CHECK(paths.run("resetAll", {}, error).has_value() && paths.changed.get() == "{}" && paths.resolve("Movies") == (dir / "program" / "movies").string());
	CHECK(paths.tempFolder().empty());
	removeTree(dir);
}

/* Chimera's Data Directory: a change recorded, carried out at the next
 * start, the data moved with it when asked; a folder holding other things
 * gets a Chimera folder of its own; into or out of itself is refused; a
 * name already in the new place stops the move and leaves the old whole */
void dataFolder()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-data-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "data" / "projects" / "AB12");
	fs::create_directories(dir / "data" / "recovery" / "CD34");
	std::ofstream(dir / "data" / "projects" / "AB12" / "history.bin") << std::string(1000, 'h');
	std::ofstream(dir / "data" / "recovery" / "CD34" / "session.json") << "{}";
	fs::create_directories(dir / "docs");
	std::ofstream(dir / "docs" / "letter.txt") << "not Chimera's";
	ManualDispatcher d;
	DataFolderViewModel data(d);
	std::string error, said;
	data.standard.set((dir / "standard").string());
	data.current.set((dir / "data").string());
	CHECK(data_folder::holdsData((dir / "data").string()) && !data_folder::holdsData((dir / "docs").string()));
	CHECK(data_folder::targetFor((dir / "docs").string()) == (dir / "docs" / "chimera").string());
	CHECK(data_folder::targetFor((dir / "fresh").string()) == (dir / "fresh").string());
	/* what the window says of it, as Chimera writes sizes, and what a folder picked would be before anything is asked */
	CHECK(data_folder::bytesText(1000) == "1 KB" && data_folder::bytesText(3 * (int64_t{1} << 20)) == "3.0 MB" &&
		  data_folder::bytesText(int64_t{3} << 29) == "1.5 GB");
	CHECK(data.run("refresh", {}, error).has_value() && data.figures.get().rfind("1 KB in use here, ", 0) == 0 &&
		  data.figures.get().find(" free on its drive") != std::string::npos);
	{
		json::Json what;
		std::string text;
		CHECK(fromValue(data.run("inspect", {(dir / "docs").string()}, error).value_or(Value()), text) && json::parse(text, what, error) &&
			  what.find("target")->text == (dir / "docs" / "chimera").string() && what.find("choice")->text == "empty" && what.find("size")->text == "1 KB");
		CHECK(fromValue(data.run("inspect", {(dir / "data").string()}, error).value_or(Value()), text) && text.find("\"choice\": \"same\"") != std::string::npos);
		fs::create_directories(dir / "other" / "projects");
		CHECK(fromValue(data.run("inspect", {(dir / "other").string()}, error).value_or(Value()), text) && text.find("\"choice\": \"holdsData\"") != std::string::npos);
		CHECK(fromValue(data.run("inspect", {(dir / "data" / "inner").string()}, error).value_or(Value()), text) && text.find("\"choice\": \"nested\"") != std::string::npos);
	}
	/* into or out of itself, and where it already is */
	CHECK(!data.run("request", {(dir / "data" / "inner").string(), true}, error) && error.find("cannot move into or out of itself") != std::string::npos);
	CHECK(fromValue(data.run("request", {(dir / "data").string(), true}, error).value_or(Value()), said) && said == "same" && data.pending.get().empty());
	/* recorded only: nothing moves until the next start */
	CHECK(fromValue(data.run("request", {(dir / "fresh").string(), true}, error).value_or(Value()), said) && said == "empty");
	CHECK(data.pending.get() == (dir / "fresh").string() && data.pendingMove.get() && fs::exists(dir / "data" / "projects" / "AB12" / "history.bin"));
	CHECK(data.run("withdraw", {}, error).has_value() && data.pending.get().empty());
	CHECK(data.applyPending().empty() && data.inUse() == (dir / "data").string());
	/* asked again, and the start carries it out */
	data.run("request", {(dir / "fresh").string(), true}, error);
	CHECK(data.applyPending().empty() && data.inUse() == (dir / "fresh").string() && data.pending.get().empty());
	CHECK(fs::file_size(dir / "fresh" / "projects" / "AB12" / "history.bin") == 1000 && fs::exists(dir / "fresh" / "recovery" / "CD34" / "session.json"));
	CHECK(!fs::exists(dir / "data"));
	/* a folder holding other things: a Chimera folder inside it, without moving the data */
	CHECK(fromValue(data.run("request", {(dir / "docs").string(), false}, error).value_or(Value()), said) && said == "empty");
	CHECK(data.pending.get() == (dir / "docs" / "chimera").string() && !data.pendingMove.get());
	CHECK(data.applyPending().empty() && data.inUse() == (dir / "docs" / "chimera").string() && fs::exists(dir / "fresh" / "projects"));
	/* back, moving: a name already there stops it, and the old is whole */
	fs::create_directories(dir / "docs" / "chimera" / "projects");
	data.run("request", {(dir / "fresh").string(), true}, error);
	data.current.set((dir / "fresh").string());
	data.pending.set((dir / "docs" / "chimera").string());
	data.pendingMove.set(true);
	said = data.applyPending();
	CHECK(said.rfind("The data folder was not moved (", 0) == 0 && said.find("is already there") != std::string::npos);
	CHECK(data.inUse() == (dir / "fresh").string() && fs::file_size(dir / "fresh" / "projects" / "AB12" / "history.bin") == 1000 &&
		fs::exists(dir / "fresh" / "recovery" / "CD34"));
	/* the system's place */
	CHECK(fromValue(data.run("request", {std::string(), false}, error).value_or(Value()), said) && data.pending.get() == (dir / "standard").string());
	CHECK(data.applyPending().empty() && data.current.get().empty() && data.inUse() == (dir / "standard").string());
	/* CHIMERA_DATA_HOME decides, as Chimera's: in use, nothing asked for here, a change waiting not carried out */
	data.pending.set((dir / "fresh").string());
	data.environment.set((dir / "portable").string());
	CHECK(data.inUse() == (dir / "portable").string() && data.applyPending().empty() && data.pending.get() == (dir / "fresh").string());
	CHECK(!data.run("request", {(dir / "elsewhere").string(), true}, error) && error.find("CHIMERA_DATA_HOME") != std::string::npos);
	removeTree(dir);
}

/* Chimera's compile cache: a folder a game, by its SHA1, for a core that
 * compiles; the Pre-Compiled Modules window lists them, named by their
 * manifest when there is one, and removes them but the open machine's */
void precompiledModules()
{
	namespace fs = std::filesystem;
	CHECK(SessionViewModel::compileDirFor("/data/code", true, "ab12cd") == (fs::path("/data/code") / "AB12CD").string());
	CHECK(SessionViewModel::compileDirFor("/data/code", false, "ab12cd").empty());
	CHECK(SessionViewModel::compileDirFor("/data/code", true, "").empty() && SessionViewModel::compileDirFor("", true, "ab").empty());
	CHECK(SessionViewModel::compileDirFor("/data/code", true, "../x") == (fs::path("/data/code") / "___X").string());
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-code-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "AAAA" / "obj");
	fs::create_directories(dir / "BBBB");
	fs::create_directories(dir / "CCCC");
	std::ofstream(dir / "AAAA" / "obj" / "one.o") << std::string(100, 'o');
	std::ofstream(dir / "AAAA" / "manifest.json") << R"({"rom": "game.iso", "core": "RPCS3", "coreVersion": "0123456789abcdef", "compiled": "2026-10-20T10:00:00Z", "files": [{"name": "obj/one.o"}, {"name": "obj/two.o"}]})";
	std::ofstream(dir / "BBBB" / "partial.o") << "half";
	std::ofstream(dir / "CCCC" / "manifest.json") << R"({"rom": "other.iso", "core": "RPCS3", "compiled": "2026-10-22T10:00:00Z", "files": [{"name": "x.o"}]})";
	std::ofstream(dir / "CCCC" / "x.o") << "x";
	ManualDispatcher d;
	PrecompiledViewModel code(d);
	code.root.set(dir.string());
	code.inUse.set((dir / "CCCC").string());
	CHECK(code.games().size() == 3);
	/* newest first; a folder nothing names last, under its hash */
	CHECK(code.games()[0].name == "other.iso" && code.games()[0].complete && code.games()[0].open);
	CHECK(code.games()[1].name == "game.iso" && code.games()[1].modules == 2 && !code.games()[1].complete && code.games()[1].bytes > 100);
	CHECK(code.games()[2].name == "BBBB" && code.games()[2].unknown);
	/* Chimera's columns and words */
	{
		RowModel &rows = *code.rowLists()[0].model;
		auto text = [&](int64_t r, int32_t c) {
			std::string t;
			fromValue(rows.cell(r, c), t);
			return t;
		};
		CHECK(text(1, 10) == "RPCS3 0123456789ab" && text(2, 10) == "not recorded" && text(1, 11).size() == 16 && text(2, 11).empty());
		CHECK(text(1, 12) == "2" && text(2, 12).empty() && text(1, 14) == "error" && text(2, 14) == "grey" && text(0, 14) == "normal");
		CHECK(code.header.get().rfind("3 games, ", 0) == 0 &&
			  code.header.get().find(". Removing one means its game is compiled again the next time a project needs it, which costs minutes and never work.") != std::string::npos);
		CHECK(PrecompiledViewModel::sizeText(1536) == "2 KB" && PrecompiledViewModel::sizeText(3ull << 30) == "3.00 GB" &&
			  PrecompiledViewModel::sizeText(5ull << 20) == "5.0 MB");
		std::string error;
		CHECK(code.run("sizeOf", {std::string("1,2")}, error) == Value(PrecompiledViewModel::sizeText(code.games()[1].bytes + code.games()[2].bytes)));
	}
	std::string error;
	CHECK(!code.run("remove", {std::string("0,2")}, error) && error == "could not remove: other.iso (in use)");
	CHECK(!fs::exists(dir / "BBBB") && fs::exists(dir / "CCCC") && code.games().size() == 2);
	CHECK(code.total.get().find(" in 2 games") != std::string::npos);
	removeTree(dir);
}

void pianoRollWithoutAMachine()
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	/* the movie is the machine's: with none open there is none to fill or edit */
	auto n = roll.run("fill", {int64_t{1000000}}, error);
	CHECK(n && std::get<int64_t>(*n) == 0);
	CHECK(roll.frameCount.get() == 0);
	CHECK(roll.rowLists()[0].name == "frames" && roll.rowLists()[0].model->rowCount() == 0);
	CHECK(!roll.run("toggle", {int64_t{5}, int64_t{0}}, error) && error == "no such frame or button");
	/* the first row in view stays inside the movie */
	roll.visibleFrames.set(40);
	CHECK(roll.set("topFrame", int64_t{5000000}, error) && roll.topFrame.get() == 0);
}

/* SDL's virtual joystick stands in for a real pad: it is held, and the
 * names Chimera's bindings use come out */
bool gamepads()
{
#ifdef _WIN32
	return false;
#else
	Gamepads pads;
	if (!pads.available())
	{
		std::printf("SKIP the gamepad tests: %s\n", pads.problem().c_str());
		return false;
	}
	void *sdl = dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD);
	if (sdl == nullptr) sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
	using Attach = int (*)(int, int, int, int);
	using SetButton = int (*)(void *, int, uint8_t);
	using SetHat = int (*)(void *, int, uint8_t);
	using SetAxis = int (*)(void *, int, int16_t);
	using Open = void *(*)(int);
	auto attach = sdl ? reinterpret_cast<Attach>(dlsym(sdl, "SDL_JoystickAttachVirtual")) : nullptr;
	auto setButton = sdl ? reinterpret_cast<SetButton>(dlsym(sdl, "SDL_JoystickSetVirtualButton")) : nullptr;
	auto setHat = sdl ? reinterpret_cast<SetHat>(dlsym(sdl, "SDL_JoystickSetVirtualHat")) : nullptr;
	auto setAxis = sdl ? reinterpret_cast<SetAxis>(dlsym(sdl, "SDL_JoystickSetVirtualAxis")) : nullptr;
	auto open = sdl ? reinterpret_cast<Open>(dlsym(sdl, "SDL_JoystickOpen")) : nullptr;
	if (!attach || !setButton || !setHat || !setAxis || !open)
	{
		std::printf("SKIP the gamepad tests: this SDL2 has no virtual joysticks\n");
		return false;
	}
	std::vector<std::string> held;
	pads.poll(held);
	const size_t before = pads.names().size();
	/* a plain joystick (type 0, not a game controller): 2 axes, 4 buttons, 1 hat */
	const int index = attach(0, 2, 4, 1);
	CHECK(index >= 0);
	pads.poll(held);
	CHECK(pads.names().size() == before + 1);
	void *joy = open(index); /* a second handle to the same device, to move it */
	CHECK(joy != nullptr);
	if (joy == nullptr) return true;
	const std::string j = "J" + std::to_string(index + 1) + " ";
	setButton(joy, 2, 1);
	setHat(joy, 0, 1 | 2); /* up and right */
	setAxis(joy, 1, -30000);
	pads.poll(held);
	auto has = [&](const std::string &n) { return std::find(held.begin(), held.end(), n) != held.end(); };
	CHECK(has(j + "B3"));
	CHECK(has(j + "POV0U") && has(j + "POV0R") && !has(j + "POV0D"));
	CHECK(has(j + "Y-") && !has(j + "Y+") && !has(j + "X+"));
	setButton(joy, 2, 0);
	setHat(joy, 0, 0);
	setAxis(joy, 1, 0);
	pads.poll(held);
	CHECK(!has(j + "B3") && !has(j + "POV0U") && !has(j + "Y-"));
	return true;
#endif
}

/* A gamepad input caught for a binding: the next one pressed, a game open
 * or not, and not one held already */
bool padCatch()
{
#ifdef _WIN32
	return false;
#else
	void *sdl = dlopen("libSDL2.so", RTLD_NOW | RTLD_NOLOAD);
	if (sdl == nullptr) sdl = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_NOLOAD);
	using Attach = int (*)(int, int, int, int);
	using SetButton = int (*)(void *, int, uint8_t);
	using Open = void *(*)(int);
	auto attach = sdl ? reinterpret_cast<Attach>(dlsym(sdl, "SDL_JoystickAttachVirtual")) : nullptr;
	auto setButton = sdl ? reinterpret_cast<SetButton>(dlsym(sdl, "SDL_JoystickSetVirtualButton")) : nullptr;
	auto open = sdl ? reinterpret_cast<Open>(dlsym(sdl, "SDL_JoystickOpen")) : nullptr;
	if (!attach || !setButton || !open)
	{
		std::printf("SKIP catching a gamepad input: no SDL2 with virtual joysticks\n");
		return false;
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	const int index = attach(0, 0, 4, 0);
	void *joy = index >= 0 ? open(index) : nullptr;
	CHECK(joy != nullptr);
	if (joy == nullptr) return true;
	const std::string j = "J" + std::to_string(index + 1) + " ";
	/* a button held when the catch starts is not the one pressed */
	setButton(joy, 0, 1);
	CHECK(session.run("catchPad", {}, error).has_value() && session.catchingPad.get() && session.caughtPad.get().empty());
	CHECK(!d.until([&] { return !session.caughtPad.get().empty(); }, 1));
	setButton(joy, 3, 1);
	CHECK(d.until([&] { return !session.caughtPad.get().empty(); }, 10));
	CHECK(session.caughtPad.get() == j + "B4" && !session.catchingPad.get());
	setButton(joy, 3, 0);
	/* stopped, nothing is caught */
	CHECK(session.run("catchPad", {}, error).has_value() && session.run("stopCatchPad", {}, error).has_value());
	setButton(joy, 2, 1);
	CHECK(!d.until([&] { return !session.caughtPad.get().empty(); }, 1) && !session.catchingPad.get());
	setButton(joy, 0, 0);
	setButton(joy, 2, 0);
	return true;
#endif
}

/* How fast the machine runs, and its sound, as Chimera has them: its
 * speeds a step at a time, the volume in tens; then the machine itself at a
 * quarter of its speed, at four times it, and as fast as it can */
bool speedAndSound(const char *package, const char *rom)
{
	std::vector<int16_t> out;
	const int16_t samples[] = {1000, -1000, 32767, -32768};
	Sound::scale(samples, 2, 50, out);
	CHECK(out == std::vector<int16_t>({500, -500, 16383, -16384}));
	Sound::scale(samples, 2, 0, out);
	CHECK(out == std::vector<int16_t>({0, 0, 0, 0}));
	Sound::scale(samples, 2, 150, out);
	CHECK(out == std::vector<int16_t>({1000, -1000, 32767, -32768}));

	/* Chimera's Sound window: the volume a frame is heard at */
	CHECK(SessionViewModel::frameVolume(true, 80, false, true, 50, false, true) == 80);
	CHECK(SessionViewModel::frameVolume(true, 80, true, true, 50, false, true) == 40);
	CHECK(SessionViewModel::frameVolume(true, 80, true, false, 50, false, true) == 0);
	CHECK(SessionViewModel::frameVolume(true, 80, false, true, 50, true, true) == 0);
	CHECK(SessionViewModel::frameVolume(true, 80, false, true, 50, true, false) == 80);
	CHECK(SessionViewModel::frameVolume(false, 80, false, true, 50, false, true) == 0);
	/* the buffer: a frame's sound (735 pairs at 60 a second) dropped once a
	 * buffer's worth waits; OpenAL's device that plays to nowhere */
	{
		std::vector<int16_t> frame(735 * 2, 100);
		Sound small("", 50);
		if (small.available())
		{
			for (int i = 0; i < 20; i++) small.play(frame.data(), 735);
			std::printf("sound buffer 50 ms: %lld queued, %lld dropped\n", static_cast<long long>(small.queued()),
				static_cast<long long>(small.dropped()));
			CHECK(small.queued() <= 2205 && small.queued() >= 1470 && small.queued() + small.dropped() == 20 * 735);
			Sound large("", 1000);
			for (int i = 0; i < 20; i++) large.play(frame.data(), 735);
			CHECK(large.queued() == 20 * 735 && large.dropped() == 0);
			CHECK(!small.devices().empty());
			Sound none("no such sound device");
			CHECK(!none.available() && none.problem() == "the sound device no such sound device would not open");
		}
		else
			std::printf("SKIP the sound buffer: %s\n", small.problem().c_str());
	}

	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	auto step = [&](const char *command) {
		session.run(command, {}, error);
		return session.speedPercent.get();
	};
	CHECK(step("speedUp") == 150 && step("speedUp") == 200 && step("speedDown") == 150 && step("speedReset") == 100);
	CHECK(session.status.get() == "Speed: 100%");
	session.speedPercent.set(1);
	CHECK(step("speedDown") == 1 && step("speedUp") == 3);
	session.speedPercent.set(6400);
	CHECK(step("speedUp") == 6400 && step("speedDown") == 3200);
	session.volume.set(95);
	session.run("volumeUp", {}, error);
	CHECK(session.volume.get() == 100 && session.status.get() == "Volume 100");
	for (int i = 0; i < 12; i++) session.run("volumeDown", {}, error);
	CHECK(session.volume.get() == 0);
	session.run("toggleSound", {}, error);
	CHECK(!session.soundOn.get() && session.status.get() == "Sound off");
	session.run("toggleThrottle", {}, error);
	CHECK(session.unthrottled.get() && session.status.get() == "Unthrottled");
	session.run("toggleThrottle", {}, error);

	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	/* frames run in a stretch of time, at a speed (the machine runs at 60 a second) */
	auto framesIn = [&](double seconds) {
		const int64_t from = session.frame.get();
		session.run("play", {}, error);
		d.until([] { return false; }, seconds);
		session.run("pause", {}, error);
		d.until([&] { return !session.running.get(); }, 5);
		return session.frame.get() - from;
	};
	session.speedPercent.set(25);
	const int64_t quarter = framesIn(1.0);
	session.speedPercent.set(400);
	const int64_t four = framesIn(1.0);
	session.speedPercent.set(100);
	session.unthrottled.set(true);
	const int64_t free = framesIn(0.5);
	session.unthrottled.set(false);
	std::printf("speed: %lld frames a second at 25%%, %lld at 400%%, %lld in half a second unthrottled\n",
		static_cast<long long>(quarter), static_cast<long long>(four), static_cast<long long>(free));
	CHECK(quarter >= 8 && quarter <= 25);
	CHECK(four >= 120 && four <= 260);
	CHECK(free > 260);
	/* the average speed: frames run freely over their time, counted from
	 * when it is turned on; time paused and frames advanced by hand are not */
	{
		auto average = [&] {
			double v = -1;
			std::sscanf(session.averageFpsText.get().c_str(), "avg %lf fps", &v);
			return v;
		};
		session.showAverageFps.set(true);
		CHECK(session.averageFpsText.get().empty());
		for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
		d.until([] { return false; }, 0.3);
		CHECK(session.averageFpsText.get().empty());
		framesIn(1.0);
		const double first = average();
		d.until([] { return false; }, 1.0);
		framesIn(0.5);
		const double second = average();
		std::printf("average: %.1f fps, %.1f after a second paused\n", first, second);
		CHECK(first > 50 && first < 70 && second > 50 && second < 70);
		session.run("resetAverageFps", {}, error);
		CHECK(session.averageFpsText.get().empty());
		session.speedPercent.set(400);
		framesIn(0.5);
		session.speedPercent.set(100);
		CHECK(average() > 150);
		session.showAverageFps.set(false);
	}
	/* Turbo held: as fast as it goes, one frame in three drawn (fastDrawOneIn),
	 * the speed marked >>>>; let go, every frame is drawn again */
	auto serial = [&] {
		Picture::Frame f;
		session.screen.takeIfNewer(UINT64_MAX, f);
		return f.serial;
	};
	{
		session.showFps.set(true);
		session.run("play", {}, error);
		CHECK(d.until([&] { return session.running.get(); }, 5));
		session.holdTurbo(true);
		d.until([] { return false; }, 0.2);
		const uint64_t drawn = serial();
		const int64_t from = session.frame.get();
		d.until([] { return false; }, 0.5);
		const int64_t ran = session.frame.get() - from;
		const int64_t shown = static_cast<int64_t>(serial() - drawn);
		std::printf("turbo: %lld frames in half a second, %lld drawn\n", static_cast<long long>(ran), static_cast<long long>(shown));
		CHECK(ran > 60 && std::llabs(shown * 3 - ran) <= 6);
		CHECK(session.fpsText.get().find(" >>>>") != std::string::npos);
		session.holdTurbo(false);
		CHECK(d.until([&] { return serial() != drawn; }, 5));
		CHECK(d.until([&] { return session.fpsText.get().find(">>") == std::string::npos; }, 5));
		session.run("pause", {}, error);
		CHECK(d.until([&] { return !session.running.get(); }, 5));
	}
	auto advanced = [&](int64_t by) {
		const int64_t from = session.frame.get();
		session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == from + by; }, 20) && (d.until([] { return false; }, 0.05), session.frame.get() == from + by);
	};
	/* Speed/Skip: frames drawn and frames run, while running */
	{
		auto ran = [&](double seconds) {
			const int64_t f0 = session.frame.get();
			const uint64_t s0 = serial();
			session.run("play", {}, error);
			d.until([] { return false; }, seconds);
			session.run("pause", {}, error);
			d.until([&] { return !session.running.get(); }, 5);
			d.until([] { return false; }, 0.1);
			return std::make_pair(session.frame.get() - f0, static_cast<int64_t>(serial() - s0));
		};
		/* no frame skip (owner, 2026-10-24, as Chimera's is gone): every frame
		 * run is drawn, at the machine's speed and unthrottled alike */
		CHECK(!session.run("setFrameSkip", {int64_t{2}}, error).has_value() && !session.get("frameSkip").has_value());
		auto [frames, drawn] = ran(0.5);
		std::printf("running: %lld frames, %lld drawn\n", static_cast<long long>(frames), static_cast<long long>(drawn));
		CHECK(frames > 20 && drawn == frames);
		/* unthrottled: one in three, as Turbo; every frame when asked */
		session.unthrottled.set(true);
		std::tie(frames, drawn) = ran(0.3);
		std::printf("unthrottled: %lld frames, %lld drawn\n", static_cast<long long>(frames), static_cast<long long>(drawn));
		CHECK(frames > 200 && std::llabs(drawn * 3 - frames) <= 6);
		session.fastDrawOneIn.set(1);
		std::tie(frames, drawn) = ran(0.3);
		CHECK(frames > 200 && drawn == frames);
		session.fastDrawOneIn.set(30);
		CHECK(session.fastDrawOneIn.get() == 10);
		session.fastDrawOneIn.set(3);
		session.unthrottled.set(false);
		/* fast forward at its own speed, 400%, not as fast as it goes, every frame drawn */
		session.run("play", {}, error);
		const uint64_t drawnBefore = serial();
		session.holdFastForward(true);
		const int64_t from = session.frame.get();
		d.until([] { return false; }, 1.0);
		const int64_t fast = session.frame.get() - from;
		session.holdFastForward(false);
		session.run("pause", {}, error);
		CHECK(d.until([&] { return !session.running.get(); }, 5));
		/* counted once the last frame's picture has arrived: read at once, a
		 * slow machine's count was one short (CI, e47945e) */
		d.until([] { return false; }, 0.1);
		const int64_t fastDrawn = static_cast<int64_t>(serial() - drawnBefore);
		CHECK(fastDrawn >= fast);
		std::printf("fast forward: %lld frames in a second\n", static_cast<long long>(fast));
		CHECK(fast >= 180 && fast <= 280);
	}
	/* Display Nothing: the frame runs and is not drawn */
	{
		session.displayNothing.set(true);
		const uint64_t was = serial();
		CHECK(advanced(1) && serial() == was);
		session.displayNothing.set(false);
		CHECK(advanced(1) && d.until([&] { return serial() != was; }, 5));
	}
	/* Skip Lag Frames: a frame advance stops on a frame that read the pad;
	 * once the game is lost (walked into a hazard) no frame reads it, and the
	 * advance runs on past them, 600 at most */
	{
		session.skipLagFrames.set(true);
		CHECK(advanced(1));
		session.key("Down", true);
		for (int i = 0; i < 4; i++) CHECK(advanced(1));
		session.key("Down", false);
		session.key("Right", true);
		for (int i = 0; i < 2; i++) CHECK(advanced(1));
		session.key("Right", false);
		CHECK(session.readDomain("RAM").at(0) == 2);
		CHECK(advanced(601));
		session.skipLagFrames.set(false);
		CHECK(advanced(1));
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Chimera's counters over the picture: the frame counter, the lag counter,
 * the input of the frame just run and the speed, worked out as Chimera
 * writes them */
bool overlay(const char *package, const char *rom)
{
	{
		/* players in turn, the console's last; buttons, then analog controls */
		const std::vector<std::string> buttons = {"Power", "P1 Up", "P2 A", "P1 B", "P10 C"};
		std::vector<SessionViewModel::Axis> axes(3);
		axes[0].name = "P2 Dial";
		axes[1].name = "Slider";
		axes[2].name = "P1 Paddle";
		axes[2].neutral = 128;
		const uint64_t mask = 1 | 4 | 8;
		CHECK(SessionViewModel::inputDisplay(buttons, "PUABC", axes, Buttons::first64(mask), {5, 0, 128}) == " B      A    5, P      ");
		CHECK(SessionViewModel::inputDisplay(buttons, "PUABC", axes, Buttons::first64(0), {-1234, 7, 0}) == "      0, -1234,      7,");
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	session.showFrameCounter.set(true);
	session.showInput.set(true);
	session.showLagCounter.set(true);
	session.showFps.set(true);
	CHECK(session.frameCounterText.get() == "0/0" && session.inputText.get().empty() && session.lagCount.get() == 0);
	/* three frames with Down held, then one with nothing */
	session.key("Down", true);
	for (int i = 0; i < 3; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	session.key("Down", false);
	std::printf("overlay: [%s] [%s] lags %lld mnemonics [%s]\n", session.frameCounterText.get().c_str(), session.inputText.get().c_str(),
		static_cast<long long>(session.lagCount.get()), session.mnemonics().c_str());
	CHECK(session.frameCounterText.get() == "3/2 (Finished)");
	CHECK(session.inputText.get() == std::string(" ") + session.mnemonics()[1] + "      ");
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 4; }, 20));
	CHECK(session.inputText.get() == "        " && session.frameCounterText.get() == "4/2 (Finished)");
	/* the lag frames are the machine's, counted up to the frame */
	int64_t lags = 0;
	for (int64_t f = 0; f < 4; f++) lags += inputs.lagAt(f) ? 1 : 0;
	CHECK(session.lagCount.get() == lags && session.lagFrame.get() == inputs.lagAt(3));
	/* lag frames the counter did not hear of while it was off are counted
	 * when it is on again (this game reads its input every frame: the
	 * flags are set here), and a frame run adds its own */
	session.showLagCounter.set(false);
	inputs.setLag(1, true);
	inputs.setLag(3, true);
	session.showLagCounter.set(true);
	CHECK(lags == 0 && session.lagCount.get() == 2);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 20));
	CHECK(session.lagCount.get() == 2 + (inputs.lagAt(0) ? 1 : 0) + (inputs.lagAt(2) ? 1 : 0) + (inputs.lagAt(4) ? 1 : 0));
	CHECK(session.frameCounterText.get() == "5/2 (Finished)");
	CHECK(session.fpsText.get().size() > 4 && session.fpsText.get().substr(session.fpsText.get().size() - 4) == " fps");
	/* a frame with input past the last makes it the last; back before it, not finished */
	session.run("seek", {int64_t{1}}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	CHECK(session.frameCounterText.get() == "1/2" && session.inputText.get() == std::string(" ") + session.mnemonics()[1] + "      ");
	CHECK(session.lagCount.get() == (inputs.lagAt(0) ? 1 : 0));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

std::string readFile(const std::filesystem::path &p);

/* The piano roll's columns shown and hidden, as Chimera's TAStudio keeps
 * them: its default-hidden controls, and the project's own list in
 * Chimera's shape, written where the project had none and only marked
 * shown or hidden where it had its own */
bool pianoRollColumns(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	using json::Json;
	CHECK(SessionViewModel::hiddenByDefault("Power") && SessionViewModel::hiddenByDefault("Reset") &&
		SessionViewModel::hiddenByDefault("Key A") && SessionViewModel::hiddenByDefault("Eject Disk") &&
		SessionViewModel::hiddenByDefault("Play Tape") && SessionViewModel::hiddenByDefault("Tilt X") &&
		!SessionViewModel::hiddenByDefault("P1 Up") && !SessionViewModel::hiddenByDefault("P1 Keyboard"));
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-columns-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	const fs::path fresh = dir / "fresh.chimeraProject", own = dir / "own.chimeraProject";
	auto tastudioOf = [](const fs::path &path) {
		const char *err = nullptr;
		ce_project *p = ce_project_open(path.string().c_str(), &err);
		std::string t;
		if (p != nullptr)
		{
			uint64_t len = 0;
			const char *text = ce_project_tastudio_text(p, &len);
			if (text != nullptr) t.assign(text, static_cast<size_t>(len));
			ce_project_free(p);
		}
		return t;
	};
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		PianoRollViewModel roll(d, inputs, session);
		std::string error;
		openGame(session, package, (dir / "walker.testrom").string(), error);
		CHECK(d.until([&] { return session.isOpen.get() && roll.frames().columnCount() == 8; }, 60));
		CHECK(roll.hiddenColumns.get().empty());
		CHECK(roll.run("hideColumn", {std::string("P1 B")}, error).has_value() && roll.hiddenColumns.get() == "P1 B");
		CHECK(roll.frames().columnHidden(5) && !roll.frames().columnHidden(4));
		RowModel *columns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "columns") columns = r.model;
		bool shown = true;
		CHECK(columns != nullptr && columns->rowCount() == 8);
		if (columns != nullptr) fromValue(columns->cell(5, 2), shown);
		CHECK(!shown);
		CHECK(session.run("saveProject", {fresh.string()}, error).has_value());
		CHECK(d.until([&] { return session.projectPath.get() == fresh.string(); }, 20));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	/* written in Chimera's shape: one roll, the cursor and frame columns, then the controls */
	{
		Json t;
		std::string jerr;
		CHECK(json::parse(tastudioOf(fresh), t, jerr));
		const Json *o = t.find("o");
		const Json *rolls = o != nullptr ? o->find("Columns") : nullptr;
		CHECK(rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 10);
		if (rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 10)
		{
			const Json &cs = rolls->items[0];
			CHECK(cs.items[0].find("Name")->text == "CursorColumn" && cs.items[1].find("Text")->text == "Frame#");
			CHECK(cs.items[2].find("Name")->text == "P1 Up" && cs.items[2].find("Text")->text == "U" && cs.items[2].find("Visible")->boolean);
			CHECK(cs.items[7].find("Name")->text == "P1 B" && !cs.items[7].find("Visible")->boolean);
		}
	}
	/* a project with Chimera's own columns (widths and all): its hidden ones
	 * read, and only shown or hidden changed when saved */
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(fresh.string().c_str(), &err);
		CHECK(p != nullptr);
		if (p == nullptr) return true;
		const std::string chimeraOwn = "{\"o\": {\"$type\": \"an old type name\", \"HorizontalOrientation\": false, \"Columns\": [[{\"VerticalWidth\": 12, \"HorizontalHeight\": 12, \"Left\": 0, \"Right\": 14, "
			"\"Name\": \"CursorColumn\", \"Text\": \"\", \"Visible\": true, \"Emphasis\": false, \"Rotatable\": false}, "
			"{\"VerticalWidth\": 15, \"HorizontalHeight\": 10, \"Left\": 69, \"Right\": 87, \"Name\": \"P1 Up\", \"Text\": \"U\", "
			"\"Visible\": false, \"Emphasis\": true, \"Rotatable\": false}, "
			"{\"VerticalWidth\": 15, \"HorizontalHeight\": 10, \"Left\": 87, \"Right\": 105, \"Name\": \"P1 Down\", \"Text\": \"D\", "
			"\"Visible\": true, \"Emphasis\": false, \"Rotatable\": false}]], \"GreenzonePeriod\": 1}}";
		CHECK(ce_project_set_tastudio_text(p, chimeraOwn.c_str(), &err) == 0);
		CHECK(ce_project_save(p, own.string().c_str(), &err) == 0);
		ce_project_free(p);
	}
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		PianoRollViewModel roll(d, inputs, session);
		std::string error;
		session.run("openProject", {own.string(), std::string(package)}, error);
		/* the project said open too: saving waits on it */
		CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get() && roll.hiddenColumns.get() == "P1 Up"; }, 60));
		CHECK(roll.run("showColumn", {std::string("P1 Up")}, error).has_value());
		CHECK(roll.run("hideColumn", {std::string("P1 Down")}, error).has_value());
		/* TAStudio's Rotate, kept in the project as Chimera keeps it */
		CHECK(!roll.horizontal.get());
		roll.horizontal.set(true);
		CHECK(session.run("saveProject", {}, error).has_value());
		CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	{
		Json t;
		std::string jerr;
		CHECK(json::parse(tastudioOf(own), t, jerr));
		const Json *o = t.find("o");
		const Json *rolls = o != nullptr ? o->find("Columns") : nullptr;
		CHECK(rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 3);
		if (rolls != nullptr && rolls->items.size() == 1 && rolls->items[0].items.size() == 3)
		{
			const Json &up = rolls->items[0].items[1], &down = rolls->items[0].items[2];
			CHECK(up.find("Visible")->boolean && up.find("Emphasis")->boolean && up.find("VerticalWidth")->number() == 15);
			CHECK(!down.find("Visible")->boolean && down.find("Right")->number() == 105);
		}
		const Json *across = o != nullptr ? o->find("HorizontalOrientation") : nullptr;
		CHECK(across != nullptr && across->kind == Json::Kind::Bool && across->boolean);
		/* the old type name it carried is not written back */
		CHECK(o != nullptr && o->find("$type") == nullptr);
	}
	{
		/* opened again: turned, as it was saved */
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		PianoRollViewModel roll(d, inputs, session);
		std::string error;
		session.run("openProject", {own.string(), std::string(package)}, error);
		CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get() && roll.horizontal.get(); }, 60));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	removeTree(dir);
	return true;
}

/* Chimera's autohold and autofire: armed by their hotkey, a button pressed
 * changes over; held or fired, it goes into the frames run */
bool autoholdAndAutofire(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get() && session.buttonNames().size() == 8; }, 60));
	/* the key the package gives P1 A, and Down's */
	std::string keyA = session.defaultBindingTexts()[4];
	keyA = keyA.substr(0, keyA.find(','));
	auto press = [&](const std::string &k) {
		session.key(k, true);
		session.key(k, false);
	};
	auto advance = [&](int n) {
		const int64_t to = session.frame.get() + n;
		for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == to; }, 20);
	};
	/* not armed, a press is only a press */
	press(keyA);
	CHECK(session.autoheld.get().empty());
	session.armAutohold(true);
	CHECK(session.stickyArmed.get());
	press(keyA);
	session.armAutohold(false);
	CHECK(session.autoheld.get() == "P1 A" && !session.stickyArmed.get() && session.stickyText.get() == "Held: P1 A ");
	CHECK(advance(2));
	CHECK((inputs.at(0).low() >> 4 & 1) && (inputs.at(1).low() >> 4 & 1));
	/* autofire: on a frame, off a frame */
	session.armAutofire(true);
	press("Down");
	session.armAutofire(false);
	CHECK(session.autofired.get() == "P1 Down" && session.stickyText.get() == "Held: P1 A Auto-P1 Down ");
	CHECK(advance(4));
	CHECK((inputs.at(2).low() >> 1 & 1) && !(inputs.at(3).low() >> 1 & 1) && (inputs.at(4).low() >> 1 & 1) && !(inputs.at(5).low() >> 1 & 1));
	CHECK((inputs.at(5).low() >> 4 & 1));
	/* on two, off one */
	session.autofireOn.set(2);
	session.run("toggleAutofire", {std::string("P1 Down")}, error);
	session.run("toggleAutofire", {std::string("P1 Down")}, error);
	CHECK(advance(3));
	CHECK((inputs.at(6).low() >> 1 & 1) && (inputs.at(7).low() >> 1 & 1) && !(inputs.at(8).low() >> 1 & 1));
	/* armed again, the held one lets go */
	session.armAutohold(true);
	press(keyA);
	session.armAutohold(false);
	CHECK(session.autoheld.get().empty());
	CHECK(session.run("clearAutohold", {}, error).has_value() && session.autofired.get().empty() && session.status.get() == "Autohold keys cleared");
	CHECK(advance(1));
	CHECK(inputs.at(9).low() == 0);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* A project's backup, the greenzone cleared, and the state history check,
 * as Chimera's TAStudio has them */
bool backupAndHistory(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-backup-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	session.backupFolder.set((dir / "backup").string());
	CHECK(!session.run("saveBackup", {}, error));
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	session.key("Down", true);
	for (int i = 0; i < 30; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 30; }, 20));
	session.key("Down", false);
	/* the backup: named for the project, the date and the time; the project's own file left as it is */
	const std::string before = readFile(project);
	const auto made = session.run("saveBackup", {}, error);
	std::string backup;
	CHECK(made.has_value() && fromValue(*made, backup));
	CHECK(d.until([&] { return session.status.get() == "Backup saved: " + backup; }, 20));
	CHECK(std::regex_match(fs::path(backup).filename().string(), std::regex("walker\\.\\d{4}-\\d\\d-\\d\\d \\d\\d\\.\\d\\d\\.\\d\\d\\.chimeraProject")));
	CHECK(fs::exists(backup) && fs::path(backup).parent_path() == dir / "backup");
	CHECK(readFile(project) == before && session.projectPath.get() == project.string());
	/* the backup is the project as it stood: its 30 frames */
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(backup.c_str(), &err);
		uint64_t len = 0;
		const char *log = p != nullptr ? ce_project_log_text(p, &len) : nullptr;
		int64_t lines = 0;
		for (uint64_t i = 0; log != nullptr && i < len; i++) lines += log[i] == '|' && (i == 0 || log[i - 1] == '\n') ? 1 : 0;
		CHECK(lines == 30);
		if (p != nullptr) ce_project_free(p);
	}
	/* the state history check: every state puts the machine back exactly */
	CHECK(session.run("checkStateHistory", {(dir / "integrity").string()}, error).has_value());
	CHECK(d.until([&] { return session.status.get().rfind("The state history check passed", 0) == 0; }, 60));
	std::printf("history: %s\n", session.status.get().c_str());
	CHECK(!fs::exists(dir / "integrity"));
	/* the greenzone cleared: the first frame's state kept, the machine at frame 0 */
	CHECK(session.greenzoneStates.get() > 1);
	CHECK(session.run("clearGreenzone", {}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 0 && session.greenzoneStates.get() <= 1; }, 20));
	CHECK(session.run("checkStateHistory", {}, error).has_value());
	CHECK(d.until([&] { return session.status.get() == "The state history holds nothing to check yet"; }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Chimera's Trace Logger and the Debugger's registers, with the synthetic
 * core's trace (a line an instruction: what ran, a tab, the registers) and
 * its eight registers and frame counter */
bool traceAndRegisters(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-trace-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	TraceLoggerViewModel trace(d, session);
	RegistersViewModel debugger(d, session);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasTrace.get() && session.hasRegisters.get(); }, 60));
	CHECK(session.traceHeader.get() == "Where: opcode a b c immediate\tthe registers");
	auto advance = [&](int64_t n) {
		const int64_t to = session.frame.get() + n;
		for (int64_t i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == to; }, 20) && (d.until([] { return false; }, 0.1), true);
	};
	/* nothing traced while nobody logs */
	CHECK(advance(2) && trace.lines().empty());
	trace.logging.set(true);
	CHECK(trace.run("logToFile", {(dir / "trace.log").string()}, error).has_value());
	CHECK(advance(1));
	CHECK(d.until([&] { return !trace.lines().empty(); }, 10));
	const size_t oneFrame = trace.lines().size();
	std::printf("trace: %zu lines a frame; first: %s | %s\n", oneFrame, trace.lines().front().disassembly.c_str(),
		trace.lines().front().registers.c_str());
	CHECK(oneFrame > 10 && std::regex_match(trace.lines().front().disassembly, std::regex("[0-9A-F]{4}: [0-9A-F]{2} \\d \\d \\d [0-9A-F]{8}")));
	CHECK(trace.lines().front().registers.rfind("R0:", 0) == 0 && trace.lines().front().registers.find("R7:") != std::string::npos);
	CHECK(trace.linesToFile.get() == static_cast<int64_t>(oneFrame));
	/* a bounded tail: the latest kept */
	trace.capacity.set(100);
	CHECK(advance(1) && d.until([&] { return trace.lines().size() == 100; }, 10));
	CHECK(trace.linesToFile.get() > 100);
	CHECK(trace.run("stopFile", {}, error).has_value() && trace.file.get().empty());
	{
		std::ifstream in(dir / "trace.log");
		int64_t n = 0;
		for (std::string line; std::getline(in, line);) n++;
		CHECK(n == trace.linesToFile.get());
	}
	/* stopped: the core traces no more */
	trace.logging.set(false);
	CHECK(trace.run("clear", {}, error).has_value() && trace.lines().empty());
	CHECK(advance(2) && trace.lines().empty());
	/* the registers: read, and R0 set by hand (hex as text, or a number) */
	CHECK(debugger.run("refresh", {}, error) == Value(int64_t{9}));
	CHECK(debugger.registers()[0].name == "R0" && debugger.registers()[8].name == "Frame" && debugger.registers()[8].value == session.frame.get());
	CHECK(debugger.run("setRegister", {int64_t{0}, std::string("0x1F")}, error).has_value() && debugger.registers()[0].value == 31);
	CHECK(debugger.run("setRegister", {int64_t{0}, int64_t{7}}, error).has_value() && debugger.registers()[0].value == 7);
	std::string hex;
	CHECK(fromValue(debugger.rowLists()[0].model->cell(0, 2), hex) && hex == "00000007");
	CHECK(!debugger.run("setRegister", {int64_t{0}, std::string("seven")}, error) && error == "seven is not a number");
	CHECK(!debugger.run("setRegister", {int64_t{20}, int64_t{1}}, error));
	/* the Surface Viewer: the synthetic core's RAM drawn as 64x64 greys */
	{
		SurfaceViewerViewModel viewer(d, session);
		CHECK(session.surfaces.get() == R"([{"name": "RAM", "width": 64, "height": 64}])");
		viewer.watching.set(true);
		CHECK(viewer.status.get() == "RAM - 64x64");
		Picture::Frame f;
		CHECK(viewer.surface.takeIfNewer(UINT64_MAX, f) && f.width == 64 && f.height == 64);
		const std::vector<uint8_t> ram = session.readDomain("RAM");
		bool same = ram.size() >= 4096 && f.pixels.size() == 4096;
		for (size_t i = 0; same && i < 4096; i++) same = f.pixels[i] == (0xFF000000u | ram[i] * 0x010101u);
		CHECK(same);
		/* drawn again as the machine moves */
		const uint64_t was = f.serial;
		CHECK(advance(1) && viewer.surface.takeIfNewer(was, f));
		viewer.picked.set(3);
		CHECK(viewer.status.get() == "No surface 3");
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Chimera's Macro Tool: the entire movie and zones of it, a zone's own
 * columns, Overlay and Replace, saved as Chimera's .chimeraMacro and loaded
 * back, placed at a frame */
bool macroTool(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-macros-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	MacroToolViewModel tool(d, roll, session);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	auto advance = [&](int64_t n) {
		const int64_t to = session.frame.get() + n;
		for (int64_t i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == to && roll.frameCount.get() >= to; }, 20);
	};
	session.key("Down", true);
	CHECK(advance(3));
	session.key("Down", false);
	CHECK(advance(3));
	CHECK(tool.zones().size() == 1 && tool.zones()[0].name == "Entire Movie" && tool.zones()[0].length == 6);
	/* a zone of frames 0 to 2, carrying P1 Down alone (a group for the console, none, and one for P1, as Chimera writes it) */
	CHECK(tool.run("setZone", {int64_t{0}, int64_t{2}}, error) == Value(int64_t{1}) && tool.zones()[1].name == "Zone 1" && tool.unsavedCount.get() == 1);
	CHECK(!tool.run("setZone", {int64_t{0}, int64_t{9}}, error) && error == "Start and end frames must be inside the movie.");
	CHECK(!tool.run("setColumns", {int64_t{1}, std::string("Sideways")}, error) && error == "Sideways is not one of the movie's columns");
	CHECK(tool.run("setColumns", {int64_t{1}, std::string("P1 Down")}, error).has_value());
	const fs::path zoneFile = dir / "down.chimeraMacro";
	CHECK(tool.run("saveAs", {int64_t{1}, zoneFile.string()}, error).has_value() && tool.unsavedCount.get() == 0);
	std::vector<std::string> lines;
	{
		std::ifstream in(zoneFile);
		for (std::string line; std::getline(in, line);) lines.push_back(line);
	}
	CHECK(lines.size() == 7 && lines[0] == "P1 Down" && lines[3] == "False,True" && lines[4] == "||D|" && lines[6] == "||D|");
	/* the entire movie: the movie's own lines */
	const fs::path wholeFile = dir / "whole.chimeraMacro";
	CHECK(tool.run("saveAs", {int64_t{0}, wholeFile.string()}, error).has_value());
	{
		std::ifstream in(wholeFile);
		std::vector<std::string> w;
		for (std::string line; std::getline(in, line);) w.push_back(line);
		CHECK(w.size() == 10 && w[4] == inputs.lines(0, 0)[0] && w[9] == inputs.lines(5, 5)[0]);
	}
	/* placed at frame 4, over the frames there: Down from 4 to 6 (the movie grows by one) */
	CHECK(tool.run("place", {int64_t{1}, int64_t{4}}, error).has_value());
	CHECK(d.until([&] { return roll.frameCount.get() == 7; }, 10));
	CHECK(inputs.lines(4, 6)[0] == inputs.lines(0, 0)[0] && inputs.lines(4, 6)[2] == inputs.lines(0, 0)[0]);
	/* loaded back, put in before frame 0 rather than over it */
	CHECK(tool.run("load", {zoneFile.string()}, error) == Value(int64_t{2}) && tool.zones()[2].name == "down" && tool.zones()[2].length == 3);
	CHECK(tool.run("setReplace", {int64_t{2}, false}, error).has_value());
	CHECK(tool.run("place", {int64_t{2}, int64_t{0}}, error).has_value());
	CHECK(d.until([&] { return roll.frameCount.get() == 10; }, 10));
	CHECK(tool.zones()[0].length == 10);
	/* saved and loaded macros are Chimera's recent ones, shared with TAStudio's Recent Macros */
	CHECK(roll.recentMacros.get() == zoneFile.string() + "\n" + wholeFile.string());
	/* a macro of columns the movie lacks is refused, as Chimera's says */
	{
		std::ofstream(dir / "other.chimeraMacro") << "P2 Turbo\nOther\n3\nFalse,True\n|T|\n";
		CHECK(!tool.run("load", {(dir / "other.chimeraMacro").string()}, error) &&
			error.rfind("The selected macro is not compatible with the current emulator core.", 0) == 0);
	}
	CHECK(!tool.run("remove", {int64_t{0}}, error) && tool.run("remove", {int64_t{2}}, error).has_value() && tool.zones().size() == 2);
	/* review, 2026-10-26: a zone set after a removal is not named as one
	 * still there; the zone picked stays picked when one before it goes */
	CHECK(tool.run("setZone", {int64_t{0}, int64_t{1}}, error) == Value(int64_t{2}) && tool.zones()[2].name == "Zone 2");
	CHECK(tool.run("setZone", {int64_t{2}, int64_t{3}}, error) == Value(int64_t{3}) && tool.zones()[3].name == "Zone 3");
	tool.picked.set(3);
	CHECK(tool.run("remove", {int64_t{2}}, error).has_value() && tool.picked.get() == 2 && tool.zones()[2].name == "Zone 3");
	CHECK(tool.run("setZone", {int64_t{0}, int64_t{2}}, error) == Value(int64_t{3}) && tool.zones()[3].name == "Zone 4");
	/* another project opened: its movie's zones, not these */
	const int64_t opensBefore = session.opens.get();
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.opens.get() > opensBefore && session.isOpen.get(); }, 60));
	CHECK(tool.zones().size() == 1 && tool.zones()[0].whole);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Chimera's Lua libraries over the view models: a script as Chimera would
 * write it - memory read and written in each size and order, the domains,
 * joypad.set held for the next frame, emu.frameadvance waiting for a frame
 * and going on after it, movie.getinput by full and by controller names,
 * registers, bit - and stopped, and Toggle Last Lua Script */
bool luaLibraries(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	RegistersViewModel debugger(d, session);
	GuiOverlayViewModel guiOverlay(d);
	HostInputViewModel hostInput(d, session);
	GamePropertiesViewModel gameProperties(d, session);
	FormsViewModel forms(d);
	CommViewModel comm(d, [] { return std::string("PNG bytes"); });
	SqlViewModel sql(d);
	LuaBridge lua;
	LuaConsoleViewModel console(d, lua);
	lua.add(session);
	lua.add(roll);
	lua.add(debugger);
	lua.add(console);
	lua.add(guiOverlay);
	lua.add(hostInput);
	lua.add(gameProperties);
	lua.add(forms);
	lua.add(comm);
	lua.add(sql);
	Connection formClicks = forms.clicked.connect([&lua](int64_t h) { lua.callHook("__chimera_forms_click", h); });
	Connection formCloses = forms.closed.connect([&lua](int64_t h) { lua.callHook("__chimera_forms_closed", h); });
	/* the person holds Z and the left and right buttons, the pointer at 12,34 */
	hostInput.key("Z", true);
	hostInput.key("X", true);
	hostInput.key("X", false);
	hostInput.mouseX.set(12);
	hostInput.mouseY.set(34);
	hostInput.mouseButtons.set(HostInputViewModel::kLeft | HostInputViewModel::kRight);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	/* System > "Loaded core: name (id)": the machine's id as the core names it */
	CHECK(session.systemId.get() == "Synth" && session.systemName.get() == "Synthetic Witness");
	auto said = [&](const std::string &start) {
		std::string found;
		RowModel *out = console.rowLists()[0].model;
		for (int64_t i = 0; i < out->rowCount(); i++)
		{
			std::string line;
			fromValue(out->cell(i, 0), line);
			/* console.log puts tabs between its values, as Chimera's */
			std::replace(line.begin(), line.end(), '\t', ' ');
			if (line.rfind(start, 0) == 0) found = line;
		}
		return found;
	};
	/* a database in the test's temporary folder: Lua's os.tmpname names one in a Windows drive's root */
	const std::string sqlDb = (std::filesystem::temp_directory_path() / ("chimera-test-sql-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".db")).generic_string();
	console.run("openScript", {scriptFile(std::regex_replace(std::string(R"LUA(
console.log("start", emu.framecount(), emu.getsystemid() ~= "")
mainmemory.write_u8(0x10, 0xAB)
console.log("u8", mainmemory.read_u8(0x10), mainmemory.read_s8(0x10), mainmemory.readbyte(0x10))
mainmemory.write_u16_be(0x20, 0x1234)
mainmemory.write_s32_le(0x30, -2)
console.log("sized", mainmemory.read_u16_be(0x20), mainmemory.read_u16_le(0x20), mainmemory.read_s32_le(0x30), mainmemory.read_u32_le(0x30), mainmemory.read_u24_be(0x20))
local r = mainmemory.readbyterange(0x20, 2)
local a = mainmemory.read_bytes_as_array(0x20, 2)
console.log("tables", r[0], r[1], a[1], a[2], memory.getmemorydomainlist()[0] == mainmemory.getname(), memory.getmemorydomainsize() > 0)
mainmemory.writefloat(0x40, 1.5, false)
console.log("float", mainmemory.readfloat(0x40, false))
local id = memorysavestate.savecorestate()
mainmemory.write_u8(0x10, 0x55)
memorysavestate.loadcorestate(id)
console.log("state", type(id), mainmemory.read_u8(0x10), pcall(memorysavestate.loadcorestate, "nothing"))
memorysavestate.removestate(id)
local held, mouse = input.get(), input.getmouse()
console.log("input", held["Z"], held["WMouse L"], held["WMouse R"], held["X"] == nil, held["WMouse M"] == nil, mouse.X, mouse.Y, mouse.Left, mouse.Middle, mouse.Right, input.get_pressed_axes()["WMouse Y"])
local names = game.list()
game.set("Steps", 0x01020304)
console.log("game", #names, names[1], names[4], game.describe("Steps").type, game.describe("Steps").size, mainmemory.read_u8(4), mainmemory.read_u8(7), game.get("Steps"), game.get("Nope") == nil, game.describe("Nope") == nil, type(game.describe("Status").label), game.set("Nope", 1))
game.set("Steps", 0)
local form = forms.newform(200, 100, "Test", function() userdata.set("closed", true) end)
local button = forms.button(form, "Go", function() userdata.set("clicked", (userdata.get("clicked") or 0) + 1) end, 1, 2)
local box = forms.textbox(form, "abc")
local tick = forms.checkbox(form, "Tick")
local pick = forms.dropdown(form, {"b", "a"})
local pic = forms.pictureBox(form, 0, 0, 50, 50)
forms.clear(pic, "white")
forms.drawLine(pic, 0, 0, 10, 10, "red")
forms.refresh(pic)
forms.setproperty(button, "Width", 90)
console.log("forms", form > 0, forms.gettext(box), forms.gettext(pick), forms.getproperty(button, "Left"), forms.getproperty(button, "Width"), forms.ischecked(tick), forms.button(12345, "x"), forms.createcolor(1, 2, 3, 4))
local shared = "chimera-test-" .. tostring(os.time()) .. "-" .. tostring(math.random(1, 1000000))
comm.mmfSetFilename(shared .. "-shot")
mainmemory.write_bytes_as_array(0x60, {9, 8, 7})
comm.mmfCopyFromMemory(shared .. "-ram", 0x60, 3)
mainmemory.write_bytes_as_array(0x70, {0, 0, 0})
comm.mmfCopyToMemory(shared .. "-ram", 0x70, 3)
console.log("comm", comm.mmfWrite(shared, "hello"), comm.mmfRead(shared, 5), comm.mmfWriteBytes(shared .. "-b", {1, 2, 3}), comm.mmfReadBytes(shared .. "-b", 3)[2],
	mainmemory.read_u8(0x71), comm.mmfScreenshot(), comm.mmfRead(comm.mmfGetFilename(), 9), pcall(comm.mmfRead, shared .. "-none", 4))
console.log("sharedname", shared)
local db = __CHIMERA_TEST_DB__
console.log("sqlbefore", SQL.writecommand("create table t (x integer)"), SQL.readcommand("select 1"))
local created, opened = SQL.createdatabase(db), SQL.opendatabase(db)
local made = SQL.writecommand("create table rewards (ID integer primary key, action varchar(20), score real)")
SQL.writecommand("insert into rewards (action, score) values ('jump', 1.5)")
SQL.writecommand("insert into rewards (action, score) values ('run', null)")
local rows = SQL.readcommand("select * from rewards")
console.log("sql", created, opened, made, rows["ID 0"], rows["action 1"], rows["score 0"], rows["score 1"], SQL.readcommand("select * from rewards where ID = 9"), SQL.writecommand("nonsense"))
os.remove(db) os.remove(db .. "-wal") os.remove(db .. "-shm")
userdata.set("lives", 3)
userdata.set("name", "walker")
console.log("userdata", userdata.get("lives"), userdata.containskey("name"), #userdata.get_keys(), userdata.remove("lives"), userdata.get("lives"), userdata.remove("lives"), select(2, pcall(userdata.set, "t", {})):find("Invalid type for userdata") ~= nil)
joypad.set({["P1 Down"] = true})
gui.drawRectangle(1, 2, 3, 4, 0xFFFF0000)
gui.text(5, 6, 'say "hi"', "yellow", "topright")
gui.addmessage("from the script")
emu.frameadvance()
local f = emu.framecount() - 1
console.log("after", emu.framecount(), movie.getinput(f)["P1 Down"], movie.getinput(f, 1)["Down"], movie.getinput(f)["P1 Up"])
console.log("regs", emu.getregister("Frame") == emu.framecount(), bit.band(0xF0, 0x3C), bit.lshift(1, 4), bit.check(5, 2))
emu.setregister("R1", 99)
console.log("set", emu.getregister("R1"))
emu.frameadvance()
console.log("again", emu.framecount())
joypad.setfrommnemonicstr("00007\t|...R....|")
mainmemory.write_bytes_as_array(0x50, {0x61, 0x62, 0x63})
console.log("hash", mainmemory.hash_region(0x50, 3), memory.hash_region(0x50, 0, "RAM"), pcall(mainmemory.hash_region, -1, 2))
emu.frameadvance()
console.log("entry", movie.getinput(emu.framecount() - 1)["P1 Right"], movie.getinput(emu.framecount() - 1)["P1 Up"], (select(2, pcall(joypad.setaxis, "Stick", 5))):find("no analog control") ~= nil)
while true do emu.frameadvance() end
)LUA"), std::regex("__CHIMERA_TEST_DB__"), "\"" + sqlDb + "\""), "libraries")}, error);
	CHECK(console.running.get() && console.summary.get() == "1 script (1 active, 0 paused)");
	CHECK_TEXT(said("start "), "start 0 True");
	CHECK_TEXT(said("u8 "), "u8 171 -85 171");
	CHECK_TEXT(said("sized "), "sized 4660 13330 -2 4294967294 1192960");
	CHECK_TEXT(said("tables "), "tables 18 52 18 52 True True");
	CHECK_TEXT(said("float "), "float 1.5");
	CHECK_TEXT(said("state "), "state string 171 False no state nothing in memory");
	CHECK_TEXT(said("input "), "input True True True True True 12 34 True False True 34");
	CHECK_TEXT(said("game "), "game 17 Status Steps u32 4 4 1 16909060 True True string False");
	CHECK_TEXT(said("forms "), "forms True abc a 1 90 False 0 67174915");
	CHECK_TEXT(said("sqlbefore "), "sqlbefore Database not open. Database not open.");
	CHECK_TEXT(said("sql "), "sql Database Created Successfully Database Opened Successfully Command ran successfully 1 run 1.5 nil No rows found near \"nonsense\": syntax error");
	CHECK(said("comm ").rfind("comm 5 hello 3 3 8 9 PNG bytes False ", 0) == 0 && said("comm ").find("there is no memory-mapped file") != std::string::npos);
	/* the shared memory made, let go (the script runs again below: that run's too, at the end) */
	auto unshare = [&] {
		if (const std::string name = said("sharedname "); name.size() > 11)
			for (const char *suffix : {"", "-b", "-ram", "-shot"})
			{
#ifndef _WIN32
				shm_unlink(("/" + name.substr(11) + suffix).c_str());
#else
				/* Windows lets a mapping go with its last handle */
				(void)suffix;
#endif
			}
	};
	unshare();
	{
		/* the window's: the button clicked twice, the check box ticked, the form closed */
		RowModel &controls = *forms.rowLists()[1].model;
		int64_t button = 0, tick = 0, pic = 0;
		std::string shapes;
		for (int64_t i = 0; i < controls.rowCount(); i++)
		{
			std::string kind;
			int64_t h = 0;
			fromValue(controls.cell(i, 2), kind);
			fromValue(controls.cell(i, 0), h);
			if (kind == "button") button = h;
			if (kind == "checkbox") tick = h;
			if (kind == "picturebox")
			{
				pic = h;
				fromValue(controls.cell(i, 15), shapes);
			}
		}
		CHECK(button > 0 && tick > 0 && pic > 0 && shapes.find("\"k\":\"line\"") != std::string::npos);
		forms.run("click", {button}, error);
		forms.run("click", {button}, error);
		forms.run("setChecked", {tick, true}, error);
		forms.run("closed", {forms.rowLists()[0].model->cell(0, 0)}, error);
		CHECK(forms.rowLists()[0].model->rowCount() == 0 && controls.rowCount() == 0);
		/* Chimera's --userdata pairs, read as Chimera reads them */
		{
			std::string why2;
			CHECK(lua.run("__chimera_userdata_pair('n', '-1234') __chimera_userdata_pair('f', '12.5') __chimera_userdata_pair('b', 'true') "
						  "__chimera_userdata_pair('big', '9999999999') __chimera_userdata_pair('s', 'walker') "
						  "console.log('pairs', math.type(userdata.get('n')), userdata.get('n'), userdata.get('f'), type(userdata.get('b')), "
						  "math.type(userdata.get('big')), userdata.get('s'))", "pairs", why2));
			CHECK_TEXT(said("pairs "), "pairs integer -1234 12.5 boolean float walker");
		}
		/* the script's functions ran: two clicks, and the form's close */
		std::string why;
		CHECK(lua.run("console.log('callbacks', userdata.get('clicked'), userdata.get('closed'))", "check", why));
		CHECK_TEXT(said("callbacks "), "callbacks 2 True");
	}
	CHECK(said("game.set: ").find("Nope") != std::string::npos);
	CHECK_TEXT(said("userdata "), "userdata 3 True 2 True nil False True");
	CHECK(session.readDomain("RAM").at(0x10) == 0xAB);
	/* the drawings of the frame to come, for the window to draw over the picture */
	{
		const std::string shapes = guiOverlay.shapes.get();
		for (const char *part : {"\"k\":\"rect\"", "\"x\":1", "\"h\":4", "\"line\":\"#FFFF0000\"", "\"k\":\"screentext\"",
				 "\"text\":\"say \\u0022hi\\u0022\"", "\"anchor\":\"topright\""})
			CHECK(shapes.find(part) != std::string::npos);
		CHECK(shapes.front() == '[' && guiOverlay.message.get() == "from the script");
	}
	/* waiting for a frame: nothing more yet */
	CHECK(said("after ").empty());
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	console.frameDone();
	CHECK_TEXT(said("after "), "after 1 True True False");
	/* a new frame, nothing drawn on it */
	CHECK(guiOverlay.shapes.get() == "[]");
	CHECK_TEXT(said("regs "), "regs True 48 16 True");
	CHECK_TEXT(said("set "), "set 99");
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 2; }, 20));
	console.frameDone();
	CHECK_TEXT(said("again "), "again 2");
	CHECK(said("hash ").rfind("hash BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD "
							  "E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855 False", 0) == 0);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	console.frameDone();
	CHECK_TEXT(said("entry "), "entry True False True");
	/* stopped (Toggle, on every script when none is selected); Toggle Last Lua Script runs it again, and stops it again */
	CHECK(console.running.get() && console.run("toggle", {}, error).has_value() && !console.running.get());
	CHECK(console.run("toggleLast", {}, error) == Value(true) && console.run("toggleLast", {}, error) == Value(false));
	/* a script error is said, and the script let go */
	console.run("openScript", {scriptFile("emu.frameadvance()\nerror('on purpose')", "fails")}, error);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 4; }, 20));
	console.frameDone();
	CHECK(said("script stopped: ").find("on purpose") != std::string::npos && !console.running.get());
	/* the review's fixes (2026-10-26), each as Chimera's library answers: the
	 * system's id, 0 for a register not there, 32-bit bit operations and a
	 * shift modulo 32, false for a file that is no project, nothing past the
	 * movie, a byte table from [0]; a fraction written as its whole part, a
	 * form's onClose run when its script closes it, no waiting inside an
	 * event callback, and joypad.set taking 1 for pressed */
	console.run("openScript", {scriptFile(R"LUA(
console.log("review", emu.getsystemid(), emu.getregister("NOPE"), bit.bor(-1, 0), bit.lshift(1, 32), client.openproject("/no/such.chimeraProject"), movie.getinput(1000000000) == nil, bizstring.decode({[0] = 65, [1] = 66}))
mainmemory.write_u8(0x50, 3.5)
local closed = false
local f = forms.newform(100, 100, "t", function() closed = true end)
forms.destroy(f)
local inCallback
event.onframeend(function() if inCallback == nil then inCallback = pcall(emu.frameadvance) end end)
joypad.set({A = 1}, 1)
emu.frameadvance()
console.log("review2", mainmemory.read_u8(0x50), closed, inCallback, movie.getinput(emu.framecount() - 1, 1)["A"])
)LUA", "review")}, error);
	/* run to its first wait as it opens: no frame has passed yet */
	CHECK_TEXT(said("review "), "review Synth 0 4294967295 1 False True AB");
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 20));
	console.frameDone();
	CHECK_TEXT(said("review2 "), "review2 3 True False True");
	unshare();
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Chimera's greenzone budgets: every project's, and a project's own kept
 * beside its cache, each taking effect when the machine opens */
bool greenzoneBudgets(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-budgets-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	session.cacheFolder.set((dir / "cache").string());
	session.greenzoneBudgetMb.set(10);
	CHECK(session.greenzoneBudgetMb.get() == 64);
	session.greenzoneBudgetMb.set(512);
	session.greenzoneNearStride.set(40);
	CHECK(session.greenzoneNearStride.get() == 32);
	session.greenzoneNearStride.set(3);
	session.compileCacheRoot.set((dir / "code").string());
	CHECK(!session.run("setProjectBudgets", {int64_t{256}, int64_t{2}}, error));
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get() && session.budgetMbApplied.get() == 512 && session.nearStrideApplied.get() == 3; }, 60));
	/* a core that compiles nothing keeps no compiled code */
	CHECK(session.compileCacheDir.get().empty() && (ce_cache_dir_get() == nullptr || *ce_cache_dir_get() == '\0') && !fs::exists(dir / "code"));
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	const fs::path budgets = dir / "cache" / session.projectId.get() / "budgets.json";
	CHECK(!session.run("setProjectBudgets", {int64_t{10}, int64_t{2}}, error));
	CHECK(session.run("setProjectBudgets", {int64_t{256}, int64_t{2}}, error).has_value());
	CHECK(session.projectBudgetMb.get() == 256 && fs::exists(budgets));
	CHECK(readFile(budgets) == R"({"memoryMb": 256, "maxNearStride": 2})");
	auto reopen = [&] {
		const int64_t was = session.opens.get();
		session.run("openProject", {project.string(), std::string(package)}, error);
		return d.until([&] { return session.opens.get() > was && session.isOpen.get(); }, 30) && (d.until([] { return false; }, 0.1), true);
	};
	/* the project's own, once it opens again */
	CHECK(reopen() && session.budgetMbApplied.get() == 256 && session.nearStrideApplied.get() == 2 && session.projectBudgetMb.get() == 256);
	/* none: the file goes, every project's apply */
	CHECK(session.run("setProjectBudgets", {int64_t{0}, int64_t{0}}, error).has_value() && !fs::exists(budgets));
	CHECK(reopen() && session.budgetMbApplied.get() == 512 && session.nearStrideApplied.get() == 3 && session.projectBudgetMb.get() == 0);
	/* the closeness alone: the memory stays every project's */
	CHECK(session.run("setProjectBudgets", {int64_t{0}, int64_t{8}}, error).has_value());
	CHECK(reopen() && session.budgetMbApplied.get() == 512 && session.nearStrideApplied.get() == 8);
	/* Chimera's Stop Movie under TAStudio: a new project of the same game and
	 * settings with an empty movie, written nowhere until its first save (a
	 * game never runs alone: owner, 2026-10-26) */
	{
		for (int i = 0; i < 4; i++) session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return inputs.size() >= 4; }, 20));
		const int64_t was = session.opens.get();
		CHECK(session.run("stopMovie", {}, error).has_value());
		CHECK(d.until([&] { return session.opens.get() > was && session.isOpen.get() && session.hasProject.get(); }, 30));
		CHECK(session.projectPath.get().empty() && inputs.size() == 0 && session.gameName.get() == "walker");
		CHECK(session.projectTitle.get() == "walker" && session.coreName.get() == "Synth");
		CHECK(!session.run("saveProject", {}, error) && error.find("has not been saved yet") != std::string::npos);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Chimera's Customize window: a machine opened runs unless the person
 * paused the last one (or "start paused" holds the first); a menu open and
 * the window going behind another pause it when asked, and run it again;
 * scripts while turbo is held only when asked */
bool customizeOptions(const char *package, const char *rom)
{
	std::string error;
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		CustomizeViewModel customize(d, session);
		auto openOne = [&] {
			const int64_t was = session.opens.get();
			openGame(session, package, std::string(rom), error);
			return d.until([&] { return session.opens.get() > was && session.isOpen.get(); }, 60);
		};
		auto settled = [&](bool running) {
			d.until([] { return false; }, 0.3);
			return session.running.get() == running;
		};
		/* the first runs; paused, the next waits; played, the next runs */
		CHECK(openOne() && settled(true));
		session.run("pause", {}, error);
		CHECK(settled(false));
		CHECK(openOne() && settled(false));
		session.run("play", {}, error);
		CHECK(settled(true));
		CHECK(openOne() && settled(true));
		/* nothing pauses it but the person: no menu (the window has no such
		 * call any more) and not the window going behind another (owner,
		 * 2026-10-24) */
		CHECK(!customize.run("menuOpened", {}, error).has_value() && !customize.get("runInBackground").has_value() &&
			!customize.get("pauseWhenMenuActivated").has_value());
		session.windowActive.set(false);
		CHECK(settled(true));
		session.windowActive.set(true);
		CHECK(settled(true));
		/* paused by the person while behind: in front again, it stays paused */
		session.windowActive.set(false);
		session.run("pause", {}, error);
		CHECK(settled(false));
		session.windowActive.set(true);
		CHECK(settled(false));
		/* scripts while turbo is held */
		CHECK(customize.luaRunsNow());
		session.holdTurbo(true);
		CHECK(customize.luaRunsNow());
		customize.luaDuringTurbo.set(false);
		CHECK(!customize.luaRunsNow());
		session.holdTurbo(false);
		CHECK(customize.luaRunsNow());
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	/* "start paused": the first machine waits */
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		CustomizeViewModel customize(d, session);
		customize.startPaused.set(true);
		openGame(session, package, std::string(rom), error);
		CHECK(d.until([&] { return session.isOpen.get(); }, 60));
		d.until([] { return false; }, 0.3);
		CHECK(!session.running.get());
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	return true;
}

/* Work a crash left behind, as Chimera keeps it: a
 * snapshot every five seconds while there are unsaved changes and a journal
 * the engine writes every change to the movie into as it is made, the
 * markers and branches beside it, all gone when the project is saved or let
 * go; one whose writer no longer runs is rebuilt as a copy among the backups
 * and offered when the project opens again, its answer opening that work
 * unsaved or the project as saved */
bool crashRecovery(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-recovery-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	session.backupFolder.set((dir / "backup").string());
	session.recoveryFolder.set((dir / "recovery").string());
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	session.recordingMode.set(true);
	session.key("Down", true);
	for (int i = 0; i < 5; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 20));
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && !session.unsaved.get(); }, 20));
	const fs::path kept = dir / "recovery" / session.projectId.get();
	CHECK(!session.projectId.get().empty());
	auto advance = [&](int64_t to) {
		while (session.frame.get() < to)
		{
			session.run("frameAdvance", {}, error);
			const int64_t was = session.frame.get();
			d.until([&] { return session.frame.get() > was; }, 20);
		}
		return session.frame.get() == to;
	};
	auto lines = [](const fs::path &file) {
		const char *err = nullptr;
		ce_project *p = ce_project_open(file.string().c_str(), &err);
		uint64_t len = 0;
		const char *log = p != nullptr ? ce_project_log_text(p, &len) : nullptr;
		int64_t n = 0;
		for (uint64_t i = 0; log != nullptr && i < len; i++) n += log[i] == '|' && (i == 0 || log[i - 1] == '\n') ? 1 : 0;
		if (p != nullptr) ce_project_free(p);
		return n;
	};
	auto markers = [](const fs::path &file) {
		const char *err = nullptr;
		ce_project *p = ce_project_open(file.string().c_str(), &err);
		std::string all;
		for (int32_t i = 0; p != nullptr && i < ce_project_marker_count(p); i++)
			all += std::to_string(ce_project_marker_frame(p, i)) + ":" + ce_project_marker_text(p, i) + ";";
		if (p != nullptr) ce_project_free(p);
		return all;
	};
	/* changes not saved: kept at once, and who kept them said */
	CHECK(advance(20) && session.unsaved.get());
	CHECK(d.until([&] { return fs::exists(kept / "snapshot.chimeraProject") && fs::exists(kept / "session.json") &&
		fs::exists(kept / "work.journal"); }, 10));
	CHECK(readFile(kept / "session.json").find("\"processId\": " + std::to_string(currentProcessId())) != std::string::npos);
	/* the first at the first change, then every five seconds with what came since */
	CHECK(d.until([&] { return lines(kept / "snapshot.chimeraProject") == 20; }, 10));
#ifndef _WIN32
	/* the record of who keeps it is put in place whole each time, as the
	 * snapshot is: another file, never the one there cut short and written
	 * again (a copy taken meanwhile found it empty) */
	auto fileId = [](const fs::path &f) {
		struct stat st {};
		return ::stat(f.c_str(), &st) == 0 ? static_cast<uint64_t>(st.st_ino) : uint64_t{0};
	};
	const uint64_t recordBefore = fileId(kept / "session.json");
#endif
	CHECK(advance(25));
	CHECK(d.until([&] { return lines(kept / "snapshot.chimeraProject") == 25; }, 10));
#ifndef _WIN32
	CHECK(recordBefore != 0 && d.until([&] { return fileId(kept / "session.json") != recordBefore; }, 10));
#endif
	/* saved: let go */
	session.run("saveProject", {}, error);
	CHECK(d.until([&] { return !session.unsaved.get(); }, 20));
	CHECK(d.until([&] { return !fs::exists(kept); }, 10));
	/* more work, and a crash: the folder as it stood, put back after a clean
	 * close took it, its writer a process that has ended. What was done after
	 * the last snapshot - two frames and a marker - is in the journal */
	CHECK(advance(30));
	CHECK(d.until([&] { return lines(kept / "snapshot.chimeraProject") == 30; }, 10));
	CHECK(advance(32) && roll.run("addMarker", {int64_t{31}, std::string("late")}, error).has_value());
	d.until([] { return false; }, 0.2);
	CHECK(lines(kept / "snapshot.chimeraProject") == 30);
	auto crashed = [&](const fs::path &aside) {
		fs::remove_all(aside);
		fs::copy(kept, aside, fs::copy_options::recursive);
		session.run("close", {}, error);
		const bool closed = d.until([&] { return !session.isOpen.get(); }, 20);
		const bool letGo = !fs::exists(kept);
		fs::copy(aside, kept, fs::copy_options::recursive);
		std::string record = readFile(kept / "session.json");
		const std::string own = "\"processId\": " + std::to_string(currentProcessId());
		const size_t at = record.find(own);
		if (at == std::string::npos) return false;
		record.replace(at, own.size(), "\"processId\": 2147483000");
		std::ofstream(kept / "session.json", std::ios::binary | std::ios::trunc) << record;
		return closed && letGo;
	};
	CHECK(crashed(dir / "aside"));
	/* the project opened again: the work rebuilt among the backups, offered, the open waiting */
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return !session.recoveredCopy.get().empty(); }, 20));
	const fs::path copy = session.recoveredCopy.get();
	CHECK(!session.isOpen.get() && !session.hasProject.get() && !fs::exists(kept));
	CHECK(copy.parent_path() == dir / "backup" && fs::exists(copy) && lines(copy) == 32);
	CHECK(markers(copy) == "31:late;");
	CHECK(std::regex_match(copy.filename().string(), std::regex("walker\\.recovered \\d{4}-\\d\\d-\\d\\d \\d\\d\\.\\d\\d\\.\\d\\d\\.chimeraProject")));
	CHECK(std::regex_match(session.recoveredUpTo.get(), std::regex("\\d\\d:\\d\\d:\\d\\d")));
	{
		/* the copy is named for that time */
		std::string dotted = session.recoveredUpTo.get();
		std::replace(dotted.begin(), dotted.end(), ':', '.');
		CHECK(copy.filename().string().find(" " + dotted + ".chimeraProject") != std::string::npos);
	}
	CHECK(lines(project) == 25);
	/* yes: that work, under the project's own name, unsaved; saving writes it there */
	CHECK(session.run("recoverWork", {true}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 30));
	CHECK(session.recoveredCopy.get().empty() && session.projectPath.get() == project.string());
	CHECK(d.until([&] { return roll.frameCount.get() == 32 && session.unsaved.get(); }, 20));
	CHECK(roll.markerCount.get() == 1);
	CHECK(session.status.get() == "The recovered work is open, not saved yet: saving writes it to " + project.string());
	/* it is kept against the next crash at once */
	CHECK(d.until([&] { return fs::exists(kept / "snapshot.chimeraProject"); }, 10));
	session.run("saveProject", {}, error);
	CHECK(d.until([&] { return !session.unsaved.get() && !fs::exists(kept); }, 20));
	CHECK(lines(project) == 32 && markers(project) == "31:late;" && fs::exists(copy));
	CHECK(!session.run("recoverWork", {true}, error) && error == "no recovered work is waiting");
	/* no: the project as it was saved, the copy left where it is */
	session.run("seek", {int64_t{32}}, error);
	CHECK(d.until([&] { return session.frame.get() == 32; }, 20));
	CHECK(advance(35));
	CHECK(d.until([&] { return lines(kept / "snapshot.chimeraProject") == 35; }, 10));
	CHECK(crashed(dir / "aside"));
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return !session.recoveredCopy.get().empty(); }, 20));
	const fs::path second = session.recoveredCopy.get();
	CHECK(second != copy && fs::exists(second) && lines(second) == 35);
	CHECK(session.run("recoverWork", {false}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 30));
	CHECK(d.until([&] { return roll.frameCount.get() == 32; }, 20));
	CHECK(!session.unsaved.get() && fs::exists(second) && !fs::exists(kept));
	/* a folder whose writer still runs is another window's work: not offered, not taken */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::copy(dir / "aside", kept, fs::copy_options::recursive);
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 30));
	CHECK(session.recoveredCopy.get().empty() && fs::exists(kept / "snapshot.chimeraProject"));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* kept before the project was last saved: nothing in it the project lacks, so it goes */
	fs::remove_all(kept);
	fs::copy(dir / "aside", kept, fs::copy_options::recursive);
	{
		std::string record = readFile(kept / "session.json");
		const std::string own = "\"processId\": " + std::to_string(currentProcessId());
		const size_t at = record.find(own);
		CHECK(at != std::string::npos);
		if (at != std::string::npos) record.replace(at, own.size(), "\"processId\": 2147483000");
		std::ofstream(kept / "session.json", std::ios::binary | std::ios::trunc) << record;
	}
	fs::last_write_time(kept / "snapshot.chimeraProject", fs::last_write_time(project) - std::chrono::hours(1));
	fs::last_write_time(kept / "work.journal", fs::last_write_time(project) - std::chrono::hours(1));
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 30));
	CHECK(session.recoveredCopy.get().empty() && !fs::exists(kept));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a folder with no snapshot is the remains of a clean end: it goes */
	fs::create_directories(kept);
	std::ofstream(kept / "session.json") << "{}";
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 30));
	CHECK(session.recoveredCopy.get().empty() && !fs::exists(kept));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* TAStudio's settings for playing, saving, copying, markers and branches,
 * and the project's changes not saved yet: marked by every kind of edit,
 * cleared by a save, saved by the autosave when it comes round */
bool tastudioSettings(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-tastudio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	ProjectDetailsViewModel details(d, session);
	std::string error;
	session.backupFolder.set((dir / "backup").string());
	auto backups = [&] {
		int64_t n = 0;
		std::error_code ec;
		for (fs::directory_iterator it(dir / "backup", ec), end; !ec && it != end; it.increment(ec)) n++;
		return n;
	};
	auto saved = [&] {
		session.run("saveProject", {}, error);
		return d.until([&] { return !session.unsaved.get(); }, 20);
	};
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	CHECK(!session.unsaved.get());
	/* frames run past the movie's end are a change */
	session.key("Down", true);
	for (int i = 0; i < 19; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 20; }, 20));
	session.key("Down", false);
	CHECK(d.until([&] { return session.unsaved.get(); }, 5));
	CHECK(saved());
	/* an edit; and a save by hand writes a backup as well when asked */
	roll.run("paint", {int64_t{2}, int64_t{3}, int64_t{4}, true}, error);
	CHECK(session.unsaved.get());
	session.backupOnSave.set(true);
	CHECK(saved());
	CHECK(d.until([&] { return backups() == 1; }, 20));
	session.backupOnSave.set(false);
	/* a marker keeps its frame's state, or not; one named again keeps what it kept */
	RowModel *markerRows = nullptr;
	for (const auto &r : roll.rowLists())
		if (r.name == "markers") markerRows = r.model;
	CHECK(markerRows != nullptr);
	if (markerRows == nullptr) return false;
	auto keeps = [&](int64_t frame) {
		for (int64_t i = 0; i < markerRows->rowCount(); i++)
		{
			int64_t f = -1;
			bool k = false;
			if (fromValue(markerRows->cell(i, 0), f) && f == frame && fromValue(markerRows->cell(i, 2), k)) return k ? 1 : 0;
		}
		return -1;
	};
	roll.markersKeepStates.set(false);
	CHECK(roll.run("addMarker", {int64_t{5}, std::string("five")}, error).has_value() && keeps(5) == 0);
	CHECK(session.unsaved.get());
	roll.markersKeepStates.set(true);
	CHECK(roll.run("addMarker", {int64_t{5}, std::string("named again")}, error).has_value() && keeps(5) == 0);
	CHECK(roll.run("addMarker", {int64_t{6}, std::string("six")}, error).has_value() && keeps(6) == 1);
	/* TAStudio's Set Markers and Remove Markers, on the frames picked */
	CHECK(roll.run("addMarkers", {int64_t{6}, int64_t{8}}, error) == Value(int64_t{3}) && roll.markerCount.get() == 4 && keeps(7) == 1 && keeps(8) == 1);
	CHECK(roll.run("removeMarkers", {int64_t{6}, int64_t{7}}, error) == Value(int64_t{2}) && keeps(6) == -1 && keeps(7) == -1);
	CHECK(keeps(5) == 0 && keeps(8) == 1 && roll.run("removeMarkers", {int64_t{8}, int64_t{9}}, error) == Value(int64_t{1}) && roll.markerCount.get() == 1);
	CHECK(!roll.run("addMarkers", {int64_t{3}, int64_t{2}}, error).has_value());
	roll.run("addMarker", {int64_t{6}, std::string("six")}, error);
	/* the run's own markers, as Chimera's: Run end on the last frame, Last
	 * input on the last press, following each edit; not removable */
	{
		const int64_t end = inputs.size() - 1, was = roll.lastInputFrame.get();
		RowModel &frames = *roll.rowLists()[0].model;
		CHECK(roll.runEndFrame.get() == end && was <= end && !frames.rowNote(0).size());
		CHECK((frames.rowFlags(end) & PianoRollViewModel::kPermanent) != 0 && (frames.rowFlags(0) & PianoRollViewModel::kPermanent) != 0);
		/* two empty frames put in after the end: the run ends there, its input does not */
		roll.run("insertFrames", {end + 1, int64_t{2}}, error);
		CHECK(roll.runEndFrame.get() == end + 2 && roll.lastInputFrame.get() == was);
		roll.run("toggle", {end + 2, int64_t{0}}, error);
		CHECK(roll.lastInputFrame.get() == end + 2);
		roll.run("toggle", {end + 2, int64_t{0}}, error);
		CHECK(roll.lastInputFrame.get() == was);
		roll.run("undo", {}, error);
		roll.run("undo", {}, error);
		roll.run("undo", {}, error);
		CHECK(roll.runEndFrame.get() == end && inputs.size() == end + 1);
		CHECK(!roll.run("removeMarker", {end}, error) && error.find("the run's own") != std::string::npos);
		CHECK(!roll.run("removeMarker", {int64_t{0}}, error));
		CHECK(std::get<int64_t>(*roll.run("nextMarker", {end - 1}, error)) == end);
		/* a marker of the person's may share their frame, beside them */
		roll.run("addMarker", {end, std::string("mine")}, error);
		CHECK(roll.markerCount.get() == 3 && frames.rowNote(end) == "mine");
		CHECK(roll.run("removeMarker", {end}, error).has_value() && roll.markerCount.get() == 2);
	}
	/* Copy: a line a frame, as the movie has them; with their numbers when asked */
	CHECK(roll.run("copyFrames", {int64_t{2}, int64_t{3}}, error).has_value());
	const std::string plain = roll.clipboardText.get();
	const std::vector<std::string> lines = inputs.lines(2, 3);
	CHECK(lines.size() == 2 && plain == lines[0] + "\n" + lines[1] + "\n" && plain[0] == '|');
	roll.copyFrameNumbers.set(true);
	roll.run("copyFrames", {int64_t{2}, int64_t{3}}, error);
	const std::string numbered = roll.clipboardText.get();
	CHECK(numbered == "00002 " + lines[0] + "\n00003 " + lines[1] + "\n");
	roll.copyFrameNumbers.set(false);
	/* pasted from text: a line that is not a frame's input pastes nothing */
	const uint64_t at10 = roll.frameBits(10).low(), at11 = roll.frameBits(11).low(), at2 = roll.frameBits(2).low(), at3 = roll.frameBits(3).low();
	CHECK(at2 != at10);
	CHECK(!roll.run("pasteText", {int64_t{10}, std::string("12 nothing\n"), false}, error).has_value());
	CHECK(roll.frameBits(10).low() == at10 && roll.frameBits(11).low() == at11);
	/* lines copied with their frame numbers paste as the frames they were, as Chimera's now does */
	CHECK(PianoRollViewModel::withoutFrameNumber("00002 |..U.....|") == "|..U.....|" &&
		PianoRollViewModel::withoutFrameNumber("  17   |A|") == "|A|" && PianoRollViewModel::withoutFrameNumber("17|A|") == "|A|" &&
		PianoRollViewModel::withoutFrameNumber("00002\t|A|") == "|A|" && PianoRollViewModel::withoutFrameNumber("    |A|") == "|A|" &&
		PianoRollViewModel::withoutFrameNumber("x 2 |A|") == "x 2 |A|" && PianoRollViewModel::withoutFrameNumber("|..U.....|") == "|..U.....|");
	error.clear();
	CHECK(roll.run("pasteText", {int64_t{10}, numbered, false}, error).has_value() && error.empty());
	CHECK(roll.frameBits(10).low() == at2 && roll.frameBits(11).low() == at3);
	roll.run("undo", {}, error);
	CHECK(roll.frameBits(10).low() == at10 && roll.frameBits(11).low() == at11);
	error.clear();
	CHECK(roll.run("pasteText", {int64_t{10}, plain, false}, error).has_value() && error.empty());
	CHECK(roll.frameBits(10).low() == at2 && roll.frameBits(11).low() == at3 && inputs.size() == 20);
	/* Windows line ends, put in */
	CHECK(roll.run("pasteText", {int64_t{0}, lines[0] + "\r\n" + lines[1] + "\r\n", true}, error).has_value());
	CHECK(inputs.size() == 22 && roll.frameBits(0).low() == at2 && roll.frameBits(1).low() == at3);
	/* following the current frame, the four ways TAStudio scrolls to it */
	{
		auto seekTo = [&](int64_t f) {
			session.run("seek", {f}, error);
			return d.until([&] { return session.frame.get() == f && roll.currentFrame.get() == f; }, 20);
		};
		roll.visibleFrames.set(5);
		roll.topFrame.set(0);
		CHECK(seekTo(12) && roll.topFrame.get() == 8);
		CHECK(seekTo(3) && roll.topFrame.get() == 3);
		CHECK(seekTo(5) && roll.topFrame.get() == 3);
		roll.followScroll.set("top");
		CHECK(seekTo(20) && roll.topFrame.get() == 20);
		roll.followScroll.set("center");
		CHECK(seekTo(10) && roll.topFrame.get() == 8);
		CHECK(seekTo(11) && roll.topFrame.get() == 8);
		roll.followAlways.set(true);
		CHECK(seekTo(12) && roll.topFrame.get() == 10);
		roll.followAlways.set(false);
		roll.followScroll.set("bottom");
		CHECK(seekTo(20) && roll.topFrame.get() == 16);
		roll.followScroll.set("near");
		roll.visibleFrames.set(0);
	}
	/* Chimera's marker list colours: the marker the machine is at or past,
	 * and a marker's frame in the greenzone's or the lag's colour once run */
	{
		auto zone = [&](int64_t frame) {
			for (int64_t i = 0; i < markerRows->rowCount(); i++)
			{
				int64_t f = -1;
				bool permanent = true;
				std::string z;
				if (fromValue(markerRows->cell(i, 0), f) && f == frame && fromValue(markerRows->cell(i, 3), permanent) && !permanent &&
					fromValue(markerRows->cell(i, 4), z))
					return z;
			}
			return std::string("?");
		};
		auto seekTo = [&](int64_t f) {
			session.run("seek", {f}, error);
			return d.until([&] { return session.frame.get() == f && roll.currentFrame.get() == f; }, 20);
		};
		CHECK(roll.lastInputFrame.get() > 8 && roll.markerCount.get() == 2);
		/* an edit before them, the machine left where it is: their frames are not run as they stand */
		session.autoRestore.set(false);
		roll.run("toggle", {int64_t{3}, int64_t{0}}, error);
		CHECK(d.until([&] { return roll.greenEnd.get() <= 4; }, 20));
		CHECK(zone(5) == "" && zone(6) == "");
		int markerChanges = 0;
		Connection markerWatch = markerRows->changed.connect([&](int64_t, int64_t) { markerChanges++; });
		CHECK(seekTo(8) && roll.greenEnd.get() >= 8 && markerChanges > 0);
		CHECK(zone(5) == "green" && zone(6) == "green");
		CHECK(roll.currentMarker.get() == 6);
		CHECK(seekTo(5) && roll.currentMarker.get() == 5);
		CHECK(seekTo(4) && roll.currentMarker.get() == 0);
		/* a lag frame's marker, in the lag's colour (this game reads its
		 * input every frame: the flag is set here) */
		inputs.setLag(6, true);
		CHECK(zone(6) == "lag" && zone(5) == "green");
		inputs.setLag(6, false);
		roll.run("toggle", {int64_t{3}, int64_t{0}}, error);
		session.autoRestore.set(true);
		CHECK(roll.markerCount.get() == 2);
	}
	/* pause at the movie's end: once, and played on from there it goes on */
	session.pauseAtEnd.set(true);
	session.run("seek", {int64_t{15}}, error);
	CHECK(d.until([&] { return session.frame.get() == 15; }, 20));
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 22 && !session.running.get(); }, 20));
	d.until([] { return false; }, 0.3);
	CHECK(session.frame.get() == 22 && !session.running.get());
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() > 26; }, 20));
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	session.pauseAtEnd.set(false);
	/* rewind goes back its step, and its fast step while fast forwarding */
	session.run("seek", {int64_t{20}}, error);
	CHECK(d.until([&] { return session.frame.get() == 20; }, 20));
	session.rewindStep.set(3);
	session.holdRewind(true);
	CHECK(d.until([&] { return session.frame.get() == 17; }, 20));
	session.holdRewind(false);
	session.rewindStepFast.set(5);
	session.holdFastForward(true);
	session.holdRewind(true);
	CHECK(d.until([&] { return session.frame.get() == 12; }, 20));
	session.holdRewind(false);
	session.holdFastForward(false);
	d.until([] { return false; }, 0.2);
	CHECK(session.frame.get() == 12);
	session.rewindStep.set(1);
	session.rewindStepFast.set(4);
	/* a branch made is a change; the old way of loading one only goes to its frame */
	CHECK(saved());
	CHECK(branches.run("create", {}, error).has_value());
	CHECK(d.until([&] { return branches.made.get() == 1; }, 20));
	CHECK(session.unsaved.get());
	CHECK(saved());
	roll.run("toggle", {int64_t{1}, int64_t{4}}, error);
	const std::string edited = inputs.text();
	session.run("seek", {int64_t{3}}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	branches.oldControlScheme.set(true);
	CHECK(branches.run("load", {int64_t{0}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 12; }, 20));
	CHECK(inputs.text() == edited);
	branches.oldControlScheme.set(false);
	/* a column hidden is a change, as Chimera flags it */
	CHECK(saved());
	CHECK(roll.run("hideColumn", {session.buttonNames()[7]}, error).has_value() && session.unsaved.get());
	roll.run("showColumn", {session.buttonNames()[7]}, error);
	/* the project's details are a change */
	CHECK(saved());
	CHECK(details.run("setComments", {std::string("autosaved")}, error).has_value() && session.unsaved.get());
	/* the autosave: the project saved by itself */
	session.autosaveSeconds.set(1);
	CHECK(d.until([&] { return !session.unsaved.get(); }, 20));
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(project.string().c_str(), &err);
		CHECK(p != nullptr && std::string(ce_project_description(p) != nullptr ? ce_project_description(p) : "") == "autosaved");
		if (p != nullptr) ce_project_free(p);
	}
	/* as a backup: one written, and the project still unsaved */
	session.autosaveAsBackup.set(true);
	const int64_t before = backups();
	roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
	CHECK(d.until([&] { return backups() > before; }, 20));
	CHECK(session.unsaved.get());
	session.autosaveSeconds.set(0);
	session.autosaveAsBackup.set(false);
	/* a column's Auto-hold while the game plays: held, fired, or its own
	 * pattern, as "Choose pattern" says; kept in the project as Chimera's
	 * column emphasis */
	{
		const std::string a = session.buttonNames()[4];
		session.autofireSkipsLag.set(false);
		auto recorded = [&](int n) {
			const int64_t end = inputs.size();
			session.run("seek", {end}, error);
			d.until([&] { return session.frame.get() == end; }, 20);
			for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
			d.until([&] { return session.frame.get() == end + n; }, 20);
			std::string bits;
			for (int64_t f = end; f < end + n; f++) bits += (inputs.at(f).low() >> 4 & 1) ? '1' : '0';
			return bits;
		};
		CHECK(roll.run("toggleAutoHold", {a}, error) == Value(true));
		CHECK(recorded(4) == "1111");
		roll.patternKind.set("autofire");
		CHECK(recorded(4) == "1010");
		roll.patternKind.set("custom");
		CHECK(roll.run("setPattern", {a, std::string("On x2, Off x1"), int64_t{0}, false}, error).has_value());
		CHECK(recorded(6) == "110110");
		roll.patternKind.set("hold");
		CHECK(roll.run("toggleAutoHold", {a}, error) == Value(false));
		CHECK(recorded(3) == "000");
		CHECK(roll.run("toggleAutoHold", {a}, error) == Value(true));
		session.autofireSkipsLag.set(true);
	}
	/* a control's pattern is kept in the project, as Chimera keeps it */
	CHECK(roll.run("setPattern", {session.buttonNames()[4], std::string("On x3, Off x2"), int64_t{0}, false}, error).has_value());
	roll.lagFramesToHide.set(2);
	CHECK(session.unsaved.get() && saved());
	{
		const char *err = nullptr;
		ce_project *p = ce_project_open(project.string().c_str(), &err);
		uint64_t len = 0;
		const char *t = p != nullptr ? ce_project_tastudio_text(p, &len) : nullptr;
		const std::string tastudio = t != nullptr ? std::string(t, static_cast<size_t>(len)) : "";
		CHECK(tastudio.find("\"LagFramesToHide\":2") != std::string::npos && tastudio.find("\"HideWasLagFrames\":false") != std::string::npos);
		CHECK(tastudio.find("\"BoolPatterns\":[") != std::string::npos &&
			tastudio.find("{\"_index\":0,\"SkipsLag\":false,\"Pattern\":[true,true,true,false,false],\"Loop\":0}") != std::string::npos);
		if (p != nullptr) ce_project_free(p);
	}
	/* closed, nothing is unsaved; and a game alone hides no lag frames */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(!session.unsaved.get() && roll.lagFramesToHide.get() == 0);
	/* opened again, the pattern is the project's */
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 60));
	{
		RowModel *patterns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "patterns") patterns = r.model;
		std::string runs;
		bool skips = true;
		CHECK(patterns != nullptr && d.until([&] { return fromValue(patterns->cell(4, 1), runs) && runs == "On x3, Off x2"; }, 20));
		CHECK(patterns != nullptr && fromValue(patterns->cell(4, 3), skips) && !skips);
		CHECK(patterns != nullptr && fromValue(patterns->cell(3, 1), runs) && runs == "On x1, Off x1");
		CHECK(roll.lagFramesToHide.get() == 2);
		/* and the Auto-hold column is one again */
		RowModel *columns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "columns") columns = r.model;
		bool held = false;
		CHECK(columns != nullptr && fromValue(columns->cell(4, 3), held) && held);
		CHECK(columns != nullptr && fromValue(columns->cell(3, 3), held) && !held);
		/* the machine started again: still the project's */
		const int64_t opened = session.opens.get();
		session.run("restart", {}, error);
		CHECK(d.until([&] { return session.opens.get() > opened && session.isOpen.get(); }, 60));
		d.until([] { return false; }, 0.2);
		CHECK(patterns != nullptr && fromValue(patterns->cell(4, 1), runs) && runs == "On x3, Off x2");
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Chimera's Cache Manager, for the project caches: each named by its
 * about.json, its size, a project gone picked out, and removed */
bool cacheManager(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-cache-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "cache");
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	session.cacheFolder.set((dir / "cache").string());
	std::string error;
	const fs::path project = dir / "walker.chimeraProject";
	openGame(session, package, (dir / "walker.testrom").string(), error, project, "Walker");
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
	session.run("saveProject", {}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
	/* while it is open, its cache is in use and stays */
	{
		const std::string id = session.projectId.get();
		CHECK(id.size() >= 8 && fs::is_directory(dir / "cache" / id));
		CacheManagerViewModel open(d);
		open.inUse.set(id);
		open.cacheFolder.set((dir / "cache").string());
		RowModel *r = open.rowLists()[0].model;
		bool inUse = false;
		CHECK(r->rowCount() == 1 && fromValue(r->cell(0, 9), inUse) && inUse);
		CHECK(!open.run("remove", {std::string("0")}, error) && error == "could not remove: Walker (in use)" && fs::is_directory(dir / "cache" / id));
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get() && session.projectId.get().empty(); }, 20));
	/* a cache that never said whose it is: named so, and not an orphan */
	fs::create_directories(dir / "cache" / "00UNNAMED");
	/* another project's cache, its project gone */
	fs::create_directories(dir / "cache" / "00DEADBEEF");
	{
		std::ofstream(dir / "cache" / "00DEADBEEF" / "about.json") << R"({"title": "Gone", "projectPath": "/no/such/gone.chimeraProject", "core": "Synth"})";
		std::ofstream(dir / "cache" / "00DEADBEEF" / "history.bin") << std::string(100, 'z');
	}
	CacheManagerViewModel caches(d);
	caches.cacheFolder.set((dir / "cache").string());
	RowModel *rows = caches.rowLists()[0].model;
	auto text = [&](int64_t row, int32_t col) {
		std::string t;
		fromValue(rows->cell(row, col), t);
		return t;
	};
	auto flag = [&](int64_t row, int32_t col) {
		bool b = false;
		fromValue(rows->cell(row, col), b);
		return b;
	};
	CHECK(rows->rowCount() == 3);
	int64_t walker = -1, gone = -1, unnamed = -1;
	for (int64_t i = 0; i < rows->rowCount(); i++)
	{
		if (text(i, 0) == "Walker") walker = i;
		if (text(i, 0) == "Gone") gone = i;
		if (text(i, 0) == "(a project that never said its name)") unnamed = i;
	}
	CHECK(walker >= 0 && gone >= 0 && unnamed >= 0);
	if (walker < 0 || gone < 0 || unnamed < 0) return false;
	CHECK(!flag(unnamed, 7) && text(gone, 10) == "The project this belongs to is not where it was last seen.");
	CHECK(text(walker, 0) == "Walker" && text(walker, 1) == fs::absolute(project).string() && text(walker, 2) == "Synth" &&
		text(walker, 4) == "walker.testrom" && !flag(walker, 7));
	CHECK(text(gone, 0) == "Gone" && flag(gone, 7));
	std::string orphans;
	CHECK(fromValue(caches.run("orphans", {}, error).value_or(Value()), orphans) && orphans == std::to_string(gone));
	/* the row picked at first: the project used last, not the largest (Chimera issue 369); the open one when one is */
	{
		const auto old = fs::file_time_type::clock::now() - std::chrono::hours(48);
		for (const char *name : {"00DEADBEEF", "00UNNAMED"})
			for (const auto &e : fs::recursive_directory_iterator(dir / "cache" / name)) fs::last_write_time(e.path(), old);
		/* a folder's own time too, where the system lets it be set (Windows' runtime here does not) */
		std::error_code timeEc;
		fs::last_write_time(dir / "cache" / "00DEADBEEF", old, timeEc);
		fs::last_write_time(dir / "cache" / "00UNNAMED", old, timeEc);
		caches.run("refresh", {}, error);
		std::string first;
		CHECK(fromValue(caches.run("firstPick", {}, error).value_or(Value()), first) && fs::path(first).filename().string() != "00DEADBEEF" &&
			fs::path(first).filename().string() != "00UNNAMED" && !first.empty());
		caches.inUse.set("00DEADBEEF");
		caches.run("refresh", {}, error);
		CHECK(fromValue(caches.run("firstPick", {}, error).value_or(Value()), first) && fs::path(first).filename().string() == "00DEADBEEF");
		caches.inUse.set("");
		caches.run("refresh", {}, error);
	}
	CHECK(caches.total.get().find(" in 3 items") != std::string::npos);
	/* what Chimera's window says over the list */
	CHECK(caches.header.get().rfind("3 cached items, ", 0) == 0 &&
		  caches.header.get().find("Everything here can be removed: what it costs is time, never work.\n1 belongs to a project that is no longer where it was (") != std::string::npos);
	/* Chimera's columns: what, the project's id, its file (not found), and what the row means in full */
	CHECK(text(gone, 14) == "Project" && text(gone, 15) == "00DEADBEEF" && text(gone, 16) == "/no/such/gone.chimeraProject   (not found)" &&
		  text(unnamed, 16) == "(never recorded)" && text(gone, 17).size() == 16);
	/* a folder with nothing in it was never used: no date, and older than any */
	CHECK(text(unnamed, 17).empty());
	{
		std::string detail;
		CHECK(fromValue(caches.run("detailOf", {gone}, error).value_or(Value()), detail) &&
			  detail == "The run replays instead of resuming, and its files are asked for once more.\nThe project this belongs to is not where it was last seen.\n"
						"Unlocked: the auto-clean may take this one, oldest first, when the cache is over its limit.\nProject file: /no/such/gone.chimeraProject\nCache: " +
							(dir / "cache" / "00DEADBEEF").string());
		/* ticked by their places on disk, which survive a sort */
		std::string folders, places, costs;
		CHECK(fromValue(caches.run("folders", {std::string("orphans")}, error).value_or(Value()), folders) &&
			  folders == "[" + json::quote((dir / "cache" / "00DEADBEEF").string()) + "]");
		CHECK(fromValue(caches.run("placesOf", {folders}, error).value_or(Value()), places) && places == std::to_string(gone));
		CHECK(fromValue(caches.run("costs", {places}, error).value_or(Value()), costs) &&
			  costs == "{\"count\": 1, \"size\": \"0 KB\", \"costs\": \"The run replays instead of resuming, and its files are asked for once more.\"}");
		/* sorted by a heading: by name least first, again the other way; size and date start largest */
		caches.run("sortBy", {int64_t{2}}, error);
		CHECK(caches.sortColumn.get() == 2 && caches.sortAscending.get() && text(0, 0) == "(a project that never said its name)");
		caches.run("sortBy", {int64_t{2}}, error);
		CHECK(!caches.sortAscending.get() && text(0, 0) == "Walker");
		caches.run("sortBy", {int64_t{9}}, error);
		CHECK(caches.sortColumn.get() == 9 && !caches.sortAscending.get());
		/* a project row's own greenzone budgets, kept beside its cache, as Chimera's Greenzone budgets... offers them */
		int64_t goneNow = -1;
		for (int64_t i = 0; i < rows->rowCount(); i++)
			if (text(i, 0) == "Gone") goneNow = i;
		CHECK(caches.run("projectOf", {goneNow}, error) == Value(std::string("{\"id\": \"00DEADBEEF\", \"title\": \"Gone\"}")));
		CHECK(caches.run("budgetsOf", {std::string("00DEADBEEF")}, error) == Value(std::string("{\"memoryMb\": 0, \"maxNearStride\": 0}")));
		CHECK(caches.run("setBudgetsOf", {std::string("00DEADBEEF"), int64_t{512}, int64_t{2}}, error).has_value() &&
			  caches.run("budgetsOf", {std::string("00DEADBEEF")}, error) == Value(std::string("{\"memoryMb\": 512, \"maxNearStride\": 2}")));
		CHECK(caches.run("setBudgetsOf", {std::string("00DEADBEEF"), int64_t{0}, int64_t{0}}, error).has_value() &&
			  !fs::exists(dir / "cache" / "00DEADBEEF" / "budgets.json"));
		CHECK(!caches.run("setBudgetsOf", {std::string("../x"), int64_t{512}, int64_t{2}}, error));
		for (int64_t i = 0; i < rows->rowCount(); i++)
		{
			if (text(i, 0) == "Walker") walker = i;
			if (text(i, 0) == "Gone") gone = i;
			if (text(i, 0) == "(a project that never said its name)") unnamed = i;
		}
	}
	CHECK(caches.run("remove", {orphans}, error) == Value(int64_t{1}));
	CHECK(rows->rowCount() == 2 && !fs::exists(dir / "cache" / "00DEADBEEF"));
	CHECK(!caches.run("remove", {std::string("7")}, error) && error == "no row 7");
	/* sizes as Chimera writes them */
	CHECK(CacheManagerViewModel::sizeText(1536) == "2 KB" && CacheManagerViewModel::sizeText(12) == "0 KB" && CacheManagerViewModel::sizeText(0).empty() &&
		  CacheManagerViewModel::sizeText(3 * 1024 * 1024 / 2) == "1.5 MB" && CacheManagerViewModel::sizeText(5ull << 30) == "5.0 GB");

	/* the limit that applies, as Chimera's: the setting, unless the disk is
	 * short of its floor; then what gives the floor back, never below a gigabyte */
	{
		const uint64_t gb = 1024ull * 1024ull * 1024ull;
		CHECK(CacheManagerViewModel::effectiveLimit(100 * gb, 20 * gb, 50 * gb, 30 * gb) == 100 * gb);
		CHECK(CacheManagerViewModel::effectiveLimit(100 * gb, 20 * gb, 10 * gb, 15 * gb) == 5 * gb);
		CHECK(CacheManagerViewModel::effectiveLimit(100 * gb, 20 * gb, 2 * gb, 10 * gb) == gb);
		CHECK(CacheManagerViewModel::effectiveLimit(gb / 2, 20 * gb, 2 * gb, 10 * gb) == gb / 2);
	}
	/* locks, the limit and the auto-clean, counted in hundreds of bytes: the
	 * oldest unlocked go first, never the newest, the spared or what is in
	 * use; unsaved work starts locked */
	{
		const fs::path root = dir / "limits", cache = root / "cache", kept = root / "recovery";
		auto entry = [&](const fs::path &folder, size_t bytes, int hoursAgo) {
			fs::create_directories(folder);
			std::ofstream(folder / "data.bin", std::ios::binary) << std::string(bytes, 'x');
			fs::last_write_time(folder / "data.bin", fs::file_time_type::clock::now() - std::chrono::hours(hoursAgo));
		};
		entry(cache / "C1", 200, 30);
		std::ofstream(cache / "C1" / "about.json") << R"({"title": "One"})";
		fs::last_write_time(cache / "C1" / "about.json", fs::file_time_type::clock::now() - std::chrono::hours(30));
		entry(cache / "C2", 150, 20);
		std::ofstream(cache / "C2" / "about.json") << R"({"title": "Two"})";
		fs::last_write_time(cache / "C2" / "about.json", fs::file_time_type::clock::now() - std::chrono::hours(20));
		entry(cache / "C3", 100, 1);
		entry(kept / "R1", 200, 40);
		std::ofstream(kept / "R1" / "session.json") << R"({"processId": 2147483000, "processStarted": 5, "projectPath": "/no/such/gone2.chimeraProject"})";
		fs::last_write_time(kept / "R1" / "session.json", fs::file_time_type::clock::now() - std::chrono::hours(40));
		CacheManagerViewModel limits(d);
		limits.unitBytes = 100;
		limits.freeSpaceAt = [](const std::string &) { return UINT64_MAX; };
		limits.autoClean.set(false);
		limits.limitGb.set(6);
		limits.floorGb.set(0);
		limits.cacheFolder.set(cache.string());
		limits.recoveryFolder.set(kept.string());
		RowModel *lr = limits.rowLists()[0].model;
		auto rowOf = [&](const std::string &title) {
			for (int64_t i = 0; i < lr->rowCount(); i++)
			{
				std::string t;
				fromValue(lr->cell(i, 0), t);
				if (t == title) return std::to_string(i);
			}
			return std::string("-1");
		};
		auto cellText = [&](const std::string &row, int32_t col) {
			std::string t;
			fromValue(lr->cell(std::stoll(row), col), t);
			return t;
		};
		auto cellFlag = [&](const std::string &row, int32_t col) {
			bool b = false;
			fromValue(lr->cell(std::stoll(row), col), b);
			return b;
		};
		auto wouldGo = [&] {
			std::string list;
			fromValue(limits.run("wouldGo", {}, error).value_or(Value()), list);
			return list;
		};
		CHECK(lr->rowCount() == 4);
		const std::string unsaved = rowOf("Unsaved work: gone2");
		CHECK(unsaved != "-1" && cellText(unsaved, 11) == "Unsaved work" && cellFlag(unsaved, 12) && cellFlag(unsaved, 7));
		CHECK(cellText(rowOf("One"), 11) == "Project cache" && !cellFlag(rowOf("One"), 12));
		/* 776 bytes (One 216, Two 166, Three 100, the work 294) against 600: the oldest unlocked
		 * goes, One, and that is enough; the newest and the locked work stay */
		CHECK(wouldGo() == rowOf("One"));
		CHECK(limits.overLimit.get() == "That is 0 KB over. Nothing is enforcing it while the auto-clean is off; Clean Now applies it once.");
		/* One locked: only Two may go, and that is not enough */
		CHECK(limits.run("toggleLock", {rowOf("One")}, error) == Value(true));
		/* the folder as JSON writes it (a Windows path's backslashes doubled) */
		CHECK(cellFlag(rowOf("One"), 12) && readFile(cache / "cache-locks.json").find(json::write(json::Json::string((cache / "C1").string()))) != std::string::npos);
		CHECK(wouldGo() == rowOf("Two"));
		CHECK(limits.overLimit.get().find("cannot be met without unlocking something") != std::string::npos);
		/* unlocked again: back to its kind's default, written down nowhere */
		CHECK(limits.run("toggleLock", {rowOf("One")}, error) == Value(false));
		CHECK(!cellFlag(rowOf("One"), 12) && readFile(cache / "cache-locks.json") == "{}");
		/* the auto-clean does nothing while it is off */
		CHECK(limits.run("runAutoClean", {std::string("C1")}, error) == Value(std::string()));
		CHECK(lr->rowCount() == 4);
		/* the unsaved work unlocked; on, sparing the project just closed (One): the old work goes,
		 * the oldest there is, and then the cache is under */
		CHECK(limits.run("toggleLock", {unsaved}, error) == Value(false));
		limits.autoClean.set(true);
		CHECK(limits.run("runAutoClean", {std::string("C1")}, error) == Value(std::string("Cache auto-clean freed 0 KB")));
		CHECK(!fs::exists(kept / "R1") && fs::exists(cache / "C1") && fs::exists(cache / "C2") && fs::exists(cache / "C3"));
		CHECK(lr->rowCount() == 3 && limits.overLimit.get().empty());
		/* a lock whose place is gone is dropped from the book the next time it is written */
		CHECK(limits.run("toggleLock", {rowOf("One")}, error) == Value(true));
		CHECK(readFile(cache / "cache-locks.json").find("R1") == std::string::npos);
		/* the disk short of its floor sets the limit, and says so */
		limits.floorGb.set(50);
		limits.freeSpaceAt = [](const std::string &) { return uint64_t{4000}; };
		limits.limitGb.set(2);
		CHECK(limits.overLimit.get().rfind("The disk has 4 KB free and is to be left 50 GB free, so the cache is being held to 0 KB "
			"rather than to the 2 GB asked for (it is never held to less than that because of the disk)", 0) == 0);
		/* Clean Now applies the limit whatever the auto-clean says: 482 bytes against 100, but One
		 * and Two locked and Three the newest */
		limits.autoClean.set(false);
		CHECK(limits.run("toggleLock", {rowOf("One") + "," + rowOf("Two")}, error) == Value(true));
		CHECK(limits.run("cleanNow", {}, error) == Value(int64_t{0}));
		CHECK(limits.status.get() == "Over the limit, and nothing left may be taken: what remains is locked, in use, or the run last worked on.");
		CHECK(limits.run("toggleLock", {rowOf("One")}, error) == Value(false));
		CHECK(limits.run("cleanNow", {}, error) == Value(int64_t{1}));
		CHECK(!fs::exists(cache / "C1") && fs::exists(cache / "C2") && fs::exists(cache / "C3"));
		CHECK(limits.status.get() == "Removed 1 of the oldest unlocked items, freeing 0 KB.");
		/* unsaved work a window is still writing is in use: never removed */
		entry(kept / "R2", 10, 1);
		std::ofstream(kept / "R2" / "session.json") << "{\"processId\": " << currentProcessId() << ", \"processStarted\": " << processStarted(currentProcessId()) << "}";
		limits.run("refresh", {}, error);
		CHECK(cellFlag(rowOf("Unsaved work"), 9));
		CHECK(!limits.run("remove", {rowOf("Unsaved work")}, error) && error == "could not remove: Unsaved work (in use)" && fs::exists(kept / "R2"));
		std::string named;
		CHECK(fromValue(limits.run("titles", {rowOf("Unsaved work")}, error).value_or(Value()), named) && named.rfind("- Unsaved work (", 0) == 0);
	}
	removeTree(dir);
	return true;
}

/* Chimera's Reproducible Media Maker: a folder packed into one file whose
 * SHA-1 is the same however the folder's files were copied; a core's
 * recipe used for a folder it was written for */
bool mediaMaker()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-media-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const fs::path game = dir / "My Game";
	fs::create_directories(game / "data");
	{
		std::ofstream(game / "readme.txt") << "hello";
		std::ofstream(game / "data" / "level.bin") << std::string(3000, 'x');
	}
	ManualDispatcher d;
	const std::string discRecipe = R"({"id": "disc", "label": "test disc", "format": "iso9660", "when": {"rootFile": "BOOT.BIN"},
		"systemArea": [{"at": 0, "u32be": 1}, {"at": 12, "u32be": "lastSector"}]})";
	MediaMakerViewModel maker(d, [&] { return std::vector<MediaMakerViewModel::Recipe>{{"Synth", "test disc", discRecipe}}; });
	std::string error;
	/* the file beside the folder, named after it, in step with the shape */
	CHECK(maker.run("setFolder", {game.string() + "/"}, error).has_value() && maker.folder.get() == game.string());
	CHECK(maker.output.get() == (dir / "My Game.zip").string());
	maker.format.set(2);
	CHECK(maker.output.get() == (dir / "My Game.img").string() && maker.formatHelp.get().rfind("A 1.44 MB floppy", 0) == 0);
	maker.format.set(0);
	auto make = [&](const fs::path &to) {
		maker.output.set(to.string());
		if (!maker.run("make", {}, error)) return false;
		return d.until([&] { return !maker.running.get(); }, 60);
	};
	CHECK(make(dir / "one.zip") && maker.result.get().rfind("Written: " + (dir / "one.zip").string() + "\nSHA1 ", 0) == 0);
	const std::string first = maker.sha1.get();
	char sha[41] = {};
	uint64_t length = 0;
	CHECK(first.size() == 40 && ce_sha1_file((dir / "one.zip").string().c_str(), sha, &length) == 1 && first == sha);
	CHECK(maker.progress.get() == 1.0);
	/* the files' times are not part of it */
	fs::last_write_time(game / "readme.txt", fs::file_time_type::clock::now() - std::chrono::hours(48));
	CHECK(make(dir / "two.zip") && maker.sha1.get() == first);
	/* a disc: no core's recipe for this folder; one once it has the file the recipe looks for */
	maker.format.set(1);
	CHECK(maker.recognised.get().empty());
	{
		std::ofstream(game / "BOOT.BIN") << "boot";
	}
	maker.run("setFolder", {game.string()}, error);
	CHECK(maker.recognised.get() == "This folder is a test disc: the image is written the way Synth reads it.");
	CHECK(make(dir / "disc.iso") && maker.result.get().rfind("Written: ", 0) == 0 && maker.sha1.get().size() == 40);
	/* the size as Chimera's writes it, its thousands marked */
	CHECK(std::regex_search(maker.result.get(), std::regex(R"(  \([0-9]{1,3}(,[0-9]{3})+ bytes\)$)")));
	/* the recipe's own bytes: the same folder packed with no core's recipe differs */
	{
		MediaMakerViewModel plain(d, {});
		plain.run("setFolder", {game.string()}, error);
		plain.format.set(1);
		plain.output.set((dir / "plain.iso").string());
		CHECK(plain.recognised.get().empty() && plain.run("make", {}, error).has_value());
		CHECK(d.until([&] { return !plain.running.get(); }, 60) && plain.sha1.get().size() == 40 && plain.sha1.get() != maker.sha1.get());
	}
	/* a zip is not what the recipe writes */
	maker.format.set(0);
	CHECK(maker.recognised.get().empty());
	CHECK(!maker.run("setFolder", {}, error));
	removeTree(dir);
	return true;
}

/* Chimera's Help menu: the build and the core in a bug report's words, a
 * report's first lines filled in from the session, and a zip of it all */
bool helpMenu(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-help-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	SettingsViewModel settings(d, session);
	HelpViewModel help(d, session, settings);
	std::string error, text;
	/* named as Chimera names a build: its commit and that commit's time on the European clock */
	CHECK(std::regex_match(help.buildName.get(), std::regex(R"(Commit [0-9a-f]{9} \(\d{4}-\d{2}-\d{2} \d{2}:\d{2} CES?T\))")));
	CHECK(help.commit.get().size() == 9 && help.buildName.get().find(help.commit.get()) == 7 &&
		help.commitUrl.get().rfind("https://github.com/ToolAssisted-run/chimera/commit/" + help.commit.get(), 0) == 0);
	/* nothing open: the build alone, and the report says what was not running */
	CHECK(fromValue(help.run("versionReport", {}, error).value_or(Value()), text) && text == "**Chimera build:** " + help.buildName.get());
	CHECK(fromValue(help.run("makeReport", {}, error).value_or(Value()), text));
	CHECK(text.find("**Core and version:** <!-- no core was running -->") != std::string::npos &&
		text.find("**Game:** <!-- no project was open -->") != std::string::npos &&
		text.find("**Renderer and non-default settings:** <!-- no core was running -->") != std::string::npos &&
		text.find("**OS and GPU:** <!-- type your operating system here -->, <!-- type your graphics card here -->") != std::string::npos);
	CHECK(text.find("**CPU and memory:** ") != std::string::npos && text.find(" threads)") != std::string::npos &&
		text.find(" GB of memory") != std::string::npos);
	/* nothing to attach: said, as Chimera's window says it */
	if (help.reportFiles.get().empty())
		CHECK(help.status.get() == "No saved project, crash note or log was found. The zip will hold the text.");
	/* the newest crash note goes with a report, its dump beside it; an older one does not */
	{
		const fs::path crashes = dir / "Crashes";
		fs::create_directories(crashes);
		for (const char *stem : {"2026-10-26 10.00.00 pid1", "2026-10-27 10.00.00 pid2"})
		{
			std::ofstream(crashes / (std::string(stem) + ".txt")) << "Chimera crash note\nprocess=1\nkind=access violation\n";
			std::ofstream(crashes / (std::string(stem) + ".dmp")) << "MDMP";
		}
		help.crashFolder.set(crashes.string());
		CHECK(fromValue(help.run("makeReport", {}, error).value_or(Value()), text));
		const std::string files = help.reportFiles.get();
		CHECK(files.find("2026-10-27 10.00.00 pid2.txt") != std::string::npos && files.find("2026-10-27 10.00.00 pid2.dmp") != std::string::npos &&
			files.find("pid1") == std::string::npos);
		help.crashFolder.set("");
	}
	/* a project open: its core, its game, its settings off their defaults */
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get() && settings.shown.get() > 0; }, 60));
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveProject", {project.string()}, error).has_value());
	/* the path is said a moment before the project is: both, before the report */
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	CHECK(fromValue(help.run("versionReport", {}, error).value_or(Value()), text) &&
		text.rfind("**Chimera build:** " + help.buildName.get() + "\n**Core and version:** Synth ", 0) == 0);
	CHECK(settings.run("set", {std::string("initFillByte"), int64_t{7}}, error).has_value());
	help.osName.set("Some System (x86_64)");
	/* the card the window draws on, when no core drew on one (issue 427) */
	help.windowGpu.set("Some Card 1060");
	CHECK(fromValue(help.run("makeReport", {}, error).value_or(Value()), text));
	CHECK(text.find("**OS and GPU:** Some System (x86_64), Some Card 1060\n") != std::string::npos);
	{
		issue_report::Facts gl;
		gl.os = "Some System (x86_64)";
		gl.glRenderer = "GeForce GTX 1060/PCIe/SSE2";
		gl.glVersion = "4.6.0 NVIDIA 581.57";
		gl.windowGpu = "Some Card 1060";
		CHECK(issue_report::firstLines(gl).find("**OS and GPU:** Some System (x86_64), GeForce GTX 1060/PCIe/SSE2, OpenGL 4.6.0 NVIDIA 581.57\n") !=
			std::string::npos);
	}
	std::printf("report:\n%s\n", text.c_str());
	CHECK(text.find("**Game:** rom: walker.testrom") != std::string::npos);
	CHECK(text.find("\n- Initial RAM Fill: 7 (default: 0)") != std::string::npos);
	CHECK(text.find("**OS and GPU:** Some System (x86_64), ") != std::string::npos);
	CHECK(help.reportFiles.get().rfind(project.string(), 0) == 0);
	/* in one zip: the report, the picture given, the files */
	const fs::path picture = dir / "shot.png";
	{
		std::ofstream out(picture, std::ios::binary);
		out << "\x89PNG not really";
	}
	const fs::path zip = dir / "report.zip";
	/* the entries of a zip, and one's text */
	auto entries = [](const fs::path &path) {
		std::map<std::string, std::string> out;
		mz_zip_archive z{};
		if (!mz_zip_reader_init_file(&z, path.string().c_str(), 0)) return out;
		for (mz_uint i = 0; i < mz_zip_reader_get_num_files(&z); i++)
		{
			char name[512];
			mz_zip_reader_get_filename(&z, i, name, sizeof name);
			size_t size = 0;
			void *data = mz_zip_reader_extract_to_heap(&z, i, &size, 0);
			out[name] = data != nullptr ? std::string(static_cast<const char *>(data), size) : std::string();
			mz_free(data);
		}
		mz_zip_reader_end(&z);
		return out;
	};
	CHECK(help.run("saveReport", {zip.string(), picture.string()}, error).has_value());
	{
		const auto in = entries(zip);
		CHECK(in.count("report.md") == 1 && in.at("report.md") == text + "\n" && in.count("game.png") == 1 && in.at("game.png") == "\x89PNG not really");
		CHECK(in.count("walker.chimeraProject") == 1 && in.at("walker.chimeraProject") == readFile(project));
	}
	/* as Chimera says it: the name and the size */
	CHECK(std::regex_match(help.status.get(), std::regex(R"(Saved report\.zip \([0-9]+\.[0-9] MB\)\.)")));
	/* the text and the files deflated, as Chimera's; the picture stored */
	{
		mz_zip_archive z{};
		CHECK(mz_zip_reader_init_file(&z, zip.string().c_str(), 0));
		auto method = [&](const char *name) {
			const int i = mz_zip_reader_locate_file(&z, name, nullptr, 0);
			mz_zip_archive_file_stat st{};
			return i >= 0 && mz_zip_reader_file_stat(&z, static_cast<mz_uint>(i), &st) ? static_cast<int>(st.m_method) : -1;
		};
		CHECK(method("report.md") == MZ_DEFLATED && method("walker.chimeraProject") == MZ_DEFLATED && method("game.png") == 0);
		mz_zip_reader_end(&z);
	}
	/* only the files ticked; one that cannot be read said by name */
	{
		const fs::path some = dir / "some.zip";
		const std::string list = help.reportFiles.get();
		help.reportFiles.set(list + "\n" + (dir / "gone.dmp").string());
		CHECK(help.run("saveReport", {some.string(), std::string(), (dir / "gone.dmp").string()}, error).has_value());
		const auto in = entries(some);
		CHECK(in.size() == 1 && in.count("report.md") == 1);
		CHECK(help.status.get().find(" Could not read: gone.dmp.") != std::string::npos);
		help.reportFiles.set(list);
		/* a zip that cannot be written: said, and nothing left behind */
		CHECK(!help.run("saveReport", {(dir / "no such folder" / "x.zip").string(), std::string()}, error) &&
			help.status.get().rfind("The zip was not saved: ", 0) == 0 && error == help.status.get());
	}
	/* Export Core Log: kept from a restart on, listed with a report's files, off with the file kept */
	{
		const fs::path log = dir / "core.log";
		std::string name;
		CHECK(fromValue(session.run("coreLogName", {}, error).value_or(Value()), name) && name.rfind("Synth core log ", 0) == 0 &&
			name.size() > 4 && name.compare(name.size() - 4, 4, ".txt") == 0);
		CHECK(session.run("startCoreLog", {log.string()}, error).has_value() && session.coreLogPath.get() == log.string());
		const int64_t opened = session.opens.get();
		session.run("restart", {}, error);
		CHECK(d.until([&] { return session.opens.get() > opened && session.isOpen.get(); }, 60));
		int64_t size = 0;
		CHECK(fromValue(session.run("coreLogSize", {}, error).value_or(Value()), size) && size > 0);
		CHECK(readFile(log).find("session: core package") != std::string::npos);
		help.run("makeReport", {}, error);
		CHECK(help.reportFiles.get().find(log.string()) != std::string::npos);
		CHECK(session.run("stopCoreLog", {}, error).has_value() && session.coreLogPath.get().empty());
		CHECK(std::string(ce_core_log_path()).empty() && fs::exists(log));
		CHECK(session.status.get() == "The core log is off. It is in " + log.string() + ": attach that file to your issue.");
		CHECK(!session.run("startCoreLog", {(dir / "no such folder" / "x.log").string()}, error) &&
			error.rfind("The core log could not be started: ", 0) == 0);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Chimera's Emulator menu (Game for a game core) and what it offers for the
 * core open: a game's own timer over the picture, the save data written out
 * (one file as it is, more in a zip), a movie import */
bool coreMenu(const char *package, const char *rom, const char *slotsPackage, const char *gamePackage)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-coremenu-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	CHECK(session.hasSaveData.get() && session.hasGameTimer.get() && !session.isGameCore.get() && session.movieImportMenu.get().empty());
	/* the save files, and written out */
	std::string names;
	CHECK(fromValue(session.run("saveDataFiles", {}, error).value_or(Value()), names) && names == "card/slot1.sav\nnotes.txt");
	const fs::path zip = dir / "save.zip";
	CHECK(session.run("exportSaveData", {zip.string()}, error) == Value(int64_t{2}));
	const std::string bytes = readFile(zip);
	CHECK(bytes.size() > 22 && bytes.compare(0, 4, std::string("PK\x03\x04", 4)) == 0);
	CHECK(bytes.find("card/slot1.sav") != std::string::npos && bytes.find("gridWalker was here\n") != std::string::npos);
	/* the end record says two files */
	CHECK(bytes.size() > 22 && static_cast<uint8_t>(bytes[bytes.size() - 22 + 8]) == 2 && static_cast<uint8_t>(bytes[bytes.size() - 22 + 10]) == 2 &&
		bytes.compare(bytes.size() - 22, 4, std::string("PK\x05\x06", 4)) == 0);
	CHECK(session.status.get() == "Save data written: " + zip.string());
	/* the game's own timer, when shown */
	CHECK(session.gameTimeText.get().empty());
	session.showGameTime.set(true);
	session.key("Down", true);
	for (int i = 0; i < 20; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 20; }, 20));
	session.key("Down", false);
	d.until([] { return false; }, 0.2);
	std::printf("game time: %s\n", session.gameTimeText.get().c_str());
	CHECK(std::regex_match(session.gameTimeText.get(), std::regex("IGT \\d\\d:\\d\\d\\.\\d\\d\\d")));
	session.showGameTime.set(false);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a core that imports movies; a game core */
	openGame(session, slotsPackage, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	CHECK(session.movieImportMenu.get() == "Import Synth Movie..." && !session.isGameCore.get());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	openGame(session, gamePackage, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60) && session.isGameCore.get());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* The piano roll's Columns menu as Chimera's: the console's columns at the
 * top, a group a player, the keyboard keys thirty a group (hidden until
 * shown, as Chimera's piano roll has them); a group shown or hidden at once */
bool columnsMenu(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	openGame(session, package, std::string(rom), error);
	/* the columns, and then the ones hidden at first, which come a moment after */
	CHECK(d.until([&] { return session.isOpen.get() && roll.frames().columnCount() == 47 && !roll.hiddenColumns.get().empty(); }, 60));
	RowModel *groups = nullptr, *columns = nullptr;
	for (const auto &r : roll.rowLists())
	{
		if (r.name == "columnGroups") groups = r.model;
		if (r.name == "columns") columns = r.model;
	}
	CHECK(groups != nullptr && columns != nullptr);
	if (groups == nullptr || columns == nullptr) return false;
	auto text = [](RowModel *m, int64_t row, int32_t col) {
		std::string t;
		fromValue(m->cell(row, col), t);
		return t;
	};
	auto flag = [](RowModel *m, int64_t row, int32_t col) {
		bool b = false;
		fromValue(m->cell(row, col), b);
		return b;
	};
	auto columnRow = [&](const std::string &name) {
		for (int64_t i = 0; i < columns->rowCount(); i++)
			if (text(columns, i, 0) == name) return i;
		return int64_t{-1};
	};
	CHECK(groups->rowCount() == 4);
	CHECK(text(groups, 0, 0) == "Keys (A - 3)" && text(groups, 1, 0) == "Keys (4 - 9)" && text(groups, 2, 0) == "Player 1" &&
		text(groups, 3, 0) == "Player 2");
	CHECK(text(groups, 0, 1) == "Keys" && text(groups, 1, 1) == "Keys" && text(groups, 3, 1) == "Player 2");
	CHECK(text(columns, columnRow("P1 Up"), 4) == "Player 1" && text(columns, columnRow("Power"), 4).empty() &&
		text(columns, columnRow("Key A"), 4) == "Keys (A - 3)" && text(columns, columnRow("Key 9"), 4) == "Keys (4 - 9)");
	/* the keys and Power hidden at first, as Chimera's piano roll hides them */
	CHECK(!flag(groups, 0, 2) && flag(groups, 2, 2) && !flag(columns, columnRow("Power"), 2));
	CHECK(roll.run("showGroup", {std::string("Keys"), true}, error).has_value() && flag(groups, 0, 2) && flag(groups, 1, 2));
	CHECK(flag(columns, columnRow("Key 5"), 2));
	CHECK(roll.run("showGroup", {std::string("Player 2"), false}, error).has_value() && !flag(groups, 3, 2) &&
		!flag(columns, columnRow("P2 B"), 2) && flag(columns, columnRow("P1 A"), 2));
	CHECK(!roll.run("showGroup", {std::string("Player 7"), true}, error) && error == "no group Player 7");
	/* input rolls, as Chimera's TAStudio has them: a new one shows the columns hidden in any roll, put where
	 * the one used was (which moves along one); each keeps its own columns; the one used is the menus' */
	{
		const RowModel &frames = roll.frames();
		auto hiddenIn = [&](int64_t view, const std::string &name) {
			for (int32_t c = 0; c < frames.columnCount(); c++)
				if (frames.columnName(c) == name) return frames.columnHiddenIn(view, c);
			return false;
		};
		CHECK(roll.rollCount.get() == 1 && roll.activeRoll.get() == 0);
		CHECK(roll.run("newRoll", {}, error) == Value(int64_t{0}) && roll.rollCount.get() == 2 && roll.activeRoll.get() == 1);
		/* Player 2 and Power were hidden: the new one shows them alone */
		CHECK(!hiddenIn(0, "P2 B") && !hiddenIn(0, "Power") && hiddenIn(0, "P1 A") && hiddenIn(0, "Key A"));
		CHECK(hiddenIn(1, "P2 B") && !hiddenIn(1, "P1 A") && !hiddenIn(1, "Key A"));
		/* the new one used: its columns in the menus, its own to hide */
		CHECK(roll.run("setActiveRoll", {int64_t{0}}, error).has_value() && flag(columns, columnRow("P2 B"), 2) &&
			!flag(columns, columnRow("P1 A"), 2));
		CHECK(roll.run("hideColumn", {std::string("P2 B")}, error).has_value() && hiddenIn(0, "P2 B") && !hiddenIn(1, "P1 A"));
		CHECK(!roll.run("setActiveRoll", {int64_t{2}}, error) && error == "no input roll 2");
		/* taken away: the other is the one used */
		CHECK(roll.run("deleteRoll", {}, error).has_value() && roll.rollCount.get() == 1 && roll.activeRoll.get() == 0);
		CHECK(hiddenIn(0, "P2 B") && !hiddenIn(0, "P1 A") && !hiddenIn(0, "Key A") && flag(columns, columnRow("P1 A"), 2));
		CHECK(roll.run("deleteRoll", {}, error).has_value() && roll.rollCount.get() == 1);
	}
	/* Config > Controllers chooses TAStudio's columns (issue 391): a box a control, in the window's copy
	 * until Save; a tab's at once; Cancel lets them go */
	{
		ControlsViewModel controls(d, session, &roll);
		RowModel *buttons = nullptr;
		for (const auto &r : controls.rowLists())
			if (r.name == "buttons") buttons = r.model;
		CHECK(buttons != nullptr && buttons->columnName(6) == "column");
		if (buttons == nullptr) return false;
		/* the package declares its players, Power player 2's whatever its name says: the
		 * session, the roll's columns and the Controllers window's tabs go by it */
		CHECK(session.playerOf("Power") == 2 && session.playerOf("P1 Up") == 1 && session.playerOf("P2 B") == 2 &&
			session.playerOf("Key A") == 0);
		{
			const RowModel &frames = roll.frames();
			int32_t power = -1;
			for (int32_t c = 0; c < frames.columnCount(); c++)
				if (frames.columnName(c) == "Power") power = c;
			CHECK(power >= 0 && frames.columnPlayer(power) == 2 && frames.columnPlayer(0) == 1);
		}
		CHECK(controls.groupOf("Power") == "Player 2" && controls.groupOf("Key A") == "Console");
		auto buttonRow = [&](const std::string &name) {
			for (int64_t i = 0; i < buttons->rowCount(); i++)
				if (text(buttons, i, 0) == name) return i;
			return int64_t{-1};
		};
		CHECK(!flag(buttons, buttonRow("P2 B"), 6) && flag(buttons, buttonRow("P1 A"), 6));
		CHECK(controls.run("edit", {}, error).has_value() && controls.columnsRoll.get() == 0);
		CHECK(controls.run("showColumn", {std::string("P1 A"), false}, error).has_value());
		CHECK(controls.run("showColumns", {std::string("buttons"), std::string("Player 2"), true}, error).has_value());
		/* the copy changed, TAStudio not yet */
		CHECK(!flag(buttons, buttonRow("P1 A"), 6) && flag(buttons, buttonRow("P2 B"), 6) && flag(buttons, buttonRow("P2 A"), 6));
		CHECK(flag(columns, columnRow("P1 A"), 2) && !flag(columns, columnRow("P2 B"), 2));
		CHECK(controls.run("cancel", {}, error).has_value());
		CHECK(flag(buttons, buttonRow("P1 A"), 6) && !flag(buttons, buttonRow("P2 B"), 6));
		/* again, and saved */
		controls.run("edit", {}, error);
		controls.run("showColumn", {std::string("P1 A"), false}, error);
		controls.run("showColumns", {std::string("buttons"), std::string("Player 2"), true}, error);
		CHECK(controls.run("save", {}, error).has_value() && error.empty());
		CHECK(!flag(columns, columnRow("P1 A"), 2) && flag(columns, columnRow("P2 B"), 2) && flag(columns, columnRow("P2 A"), 2));
		CHECK(!flag(buttons, buttonRow("P1 A"), 6) && flag(buttons, buttonRow("P2 B"), 6));
		/* hidden in TAStudio, shown here */
		CHECK(roll.run("hideColumn", {std::string("P2 B")}, error).has_value() && !flag(buttons, buttonRow("P2 B"), 6));
		CHECK(!controls.run("showColumn", {std::string("P9 Z"), true}, error) && error == "no control P9 Z");
		CHECK(!controls.run("showColumns", {std::string("keys"), std::string(), true}, error) &&
			error == "no kind keys: buttons or analog");
		/* with several input rolls, the window says which it chooses for */
		roll.run("newRoll", {}, error);
		CHECK(controls.run("edit", {}, error).has_value() && controls.columnsRoll.get() == 2);
		controls.run("cancel", {}, error);
		roll.run("deleteRoll", {}, error);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* The project's details as Chimera's TAStudio edits them - its header,
 * comments and subtitles - kept with the project, and the subtitles of the
 * frame on screen worked out */
bool projectDetails(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-details-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	const fs::path project = dir / "walker.chimeraProject";
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		ProjectDetailsViewModel details(d, session);
		std::string error;
		CHECK(!details.run("setComments", {std::string("none")}, error) && error == "no project is open");
		openGame(session, package, (dir / "walker.testrom").string(), error, project, "the corridor");
		CHECK(d.until([&] { return session.isOpen.get(); }, 60));
		CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
		CHECK(details.run("reload", {}, error).has_value() && details.gameName.get() == "the corridor" && details.core.get() == "Synth");
		CHECK(details.run("setHeader", {std::string("a TASer"), std::string("1.0"), std::string("SYNTH-01"), std::string("Walker"), false}, error).has_value());
		CHECK(details.author.get() == "a TASer" && details.gameName.get() == "Walker" && details.defaultAuthor.get() == "default user");
		CHECK(details.run("setComments", {std::string("first\n\n  \nsecond\r\n")}, error).has_value() && details.comments.get() == "first\nsecond");
		CHECK(details.run("addSubtitle", {int64_t{30}, int64_t{4}, int64_t{8}, int64_t{60}, std::string("#FF0000"), std::string("later")}, error).has_value());
		CHECK(details.run("addSubtitle", {int64_t{0}, int64_t{1}, int64_t{2}, int64_t{10}, std::string("#80FFFFFF"), std::string("at once")}, error).has_value());
		CHECK(!details.run("addSubtitle", {int64_t{0}, int64_t{1}, int64_t{2}, int64_t{0}, std::string("#FFFFFF"), std::string("never")}, error));
		CHECK(!details.run("addSubtitle", {int64_t{0}, int64_t{1}, int64_t{2}, int64_t{5}, std::string("red"), std::string("x")}, error) &&
			error == "red is not a colour: #RRGGBB or #AARRGGBB");
		/* the one on a frame, for the main window's Add Subtitle to edit */
		CHECK(details.run("subtitleOn", {int64_t{30}}, error) ==
			  Value(std::string("{\"index\": 1, \"x\": 4, \"y\": 8, \"duration\": 60, \"color\": \"#FFFF0000\", \"message\": \"later\"}")));
		CHECK(details.run("subtitleOn", {int64_t{31}}, error) == Value(std::string()));
		/* kept by frame */
		RowModel &subs = *details.rowLists()[0].model;
		auto cellText = [&](int64_t r, int32_t c) {
			std::string t;
			const Value v = subs.cell(r, c);
			if (!fromValue(v, t))
			{
				int64_t n = 0;
				fromValue(v, n);
				t = std::to_string(n);
			}
			return t;
		};
		CHECK(details.count.get() == 2 && cellText(0, 5) == "at once" && cellText(1, 5) == "later" && cellText(0, 4) == "#80FFFFFF" && cellText(1, 4) == "#FFFF0000");
		/* on screen at frame 0: the first, in its colour (shown opaque) */
		CHECK(details.onScreen.get() == "[{\"text\": \"at once\", \"x\": 1, \"y\": 2, \"color\": \"#FFFFFFFF\"}]");
		CHECK(details.run("editSubtitle", {int64_t{1}, int64_t{0}, int64_t{4}, int64_t{8}, int64_t{60}, std::string("#00FF00"), std::string("now too")}, error).has_value());
		CHECK(details.onScreen.get().find("now too") != std::string::npos);
		details.showSubtitles.set(false);
		CHECK(details.onScreen.get() == "[]");
		details.showSubtitles.set(true);
		CHECK(details.run("removeSubtitle", {int64_t{0}}, error).has_value() && details.count.get() == 1);
		CHECK(session.run("saveProject", {}, error).has_value());
		CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	const std::string text = readFile(project);
	CHECK(text.find("a TASer") != std::string::npos && text.find("SYNTH-01") != std::string::npos && text.find("second") != std::string::npos);
	{
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		ProjectDetailsViewModel details(d, session);
		std::string error;
		session.run("openProject", {project.string(), std::string(package)}, error);
		CHECK(d.until([&] { return session.isOpen.get() && details.core.get() == "Synth"; }, 60));
		CHECK(details.author.get() == "a TASer" && details.emulatorVersion.get() == "1.0" && details.boardName.get() == "SYNTH-01");
		CHECK(details.gameName.get() == "Walker" && details.comments.get() == "first\nsecond" && details.count.get() == 1);
		CHECK(details.subtitles()[0].message == "now too" && details.subtitles()[0].color == 0xFF00FF00u && details.subtitles()[0].frame == 0);
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	removeTree(dir);
	return true;
}

std::string readFile(const std::filesystem::path &p);

bool machine(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;

	openGame(session, package, std::string(rom), error);
	CHECK(error.empty());
	if (!d.until([&] { return session.isOpen.get() || session.status.get().rfind("Could not", 0) == 0; }, 60))
	{
		std::fprintf(stderr, "FAIL: the machine did not open: %s\n", session.status.get().c_str());
		failures++;
		return true;
	}
	CHECK(session.isOpen.get());
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	CHECK(session.buttonCount.get() == 8);
	CHECK(roll.rowLists()[0].model->columnCount() == 8);
	CHECK(roll.rowLists()[0].model->columnName(4) == "P1 A");

	/* X is bound to P1 A (button 4): held over one frame advance, it is recorded */
	int64_t changedRows = 0, appendedRows = 0;
	Connection w1 = roll.rowLists()[0].model->changed.connect([&](int64_t a, int64_t b) { changedRows += b - a + 1; });
	Connection w2 = roll.rowLists()[0].model->appended.connect([&](int64_t a, int64_t b) { appendedRows += b - a + 1; });
	session.key("X", true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 10));
	session.key("X", false);
	CHECK(inputs.size() == 1 && inputs.at(0).low() == (uint64_t{1} << 4));
	CHECK(appendedRows == 1);
	CHECK(roll.currentFrame.get() == 1);

	/* a picture came out of it */
	Picture::Frame f;
	CHECK(session.screen.takeIfNewer(0, f) && f.width > 0 && f.height > 0 && f.pixels.size() == size_t(f.width) * f.height);
	/* opaque, whatever the core left in the fourth byte: a desktop that takes
	 * it as see-through showed through the picture (issue 393) */
	CHECK(std::all_of(f.pixels.begin(), f.pixels.end(), [](uint32_t p) { return p >> 24 == 0xFF; }));
	{
		Picture picture;
		const uint32_t left[4] = {0x00102030u, 0x7F405060u, 0xFF708090u, 0x01A0B0C0u};
		picture.publish(left, 2, 2, 2, 2);
		const std::vector<uint32_t> opaque = {0xFF102030u, 0xFF405060u, 0xFF708090u, 0xFFA0B0C0u};
		Picture::Frame shown;
		CHECK(picture.takeIfNewer(0, shown) && shown.pixels == opaque);
	}
	/* and it saves as chimera-run writes one: a TGA, rows bottom-up */
	{
		namespace fs = std::filesystem;
		const fs::path shot = fs::temp_directory_path() / ("chimera-test-shot-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".tga");
		CHECK(session.run("savePicture", {shot.string()}, error).has_value());
		const std::string tga = readFile(shot);
		CHECK(tga.size() == 18 + size_t(f.width) * f.height * 4 && tga[2] == 2 && static_cast<uint8_t>(tga[16]) == 32);
		/* every row where a TGA keeps it: the picture's last row first */
		std::string rows;
		for (int32_t y = f.height - 1; y >= 0; y--)
			rows.append(reinterpret_cast<const char *>(&f.pixels[size_t(y) * f.width]), size_t(f.width) * 4);
		CHECK(tga.size() > 18 && tga.compare(18, std::string::npos, rows) == 0);
		fs::remove(shot);
	}

	/* a press on the cell flips it; a drag down the column gives the next cells the same */
	auto held = roll.run("press", {int64_t{0}, int64_t{4}}, error);
	CHECK(held && std::get<bool>(*held) == false && inputs.at(0).low() == 0);
	roll.run("fill", {int64_t{10}}, error);
	roll.run("press", {int64_t{2}, int64_t{7}}, error);
	roll.run("drag", {int64_t{3}, int64_t{7}}, error);
	roll.run("drag", {int64_t{4}, int64_t{7}}, error);
	CHECK(inputs.at(2).low() == (uint64_t{1} << 7) && inputs.at(3).low() == (uint64_t{1} << 7) && inputs.at(4).low() == (uint64_t{1} << 7));
	CHECK(changedRows >= 4);

	/* the press and its drag are one edit: one undo takes them all back,
	 * one redo does them again; an undone fill shortens the inputs again */
	CHECK(roll.canUndo.get() && !roll.canRedo.get());
	roll.run("undo", {}, error);
	CHECK(inputs.at(2).low() == 0 && inputs.at(3).low() == 0 && inputs.at(4).low() == 0);
	CHECK(roll.canRedo.get());
	roll.run("redo", {}, error);
	CHECK(inputs.at(2).low() == (uint64_t{1} << 7) && inputs.at(3).low() == (uint64_t{1} << 7) && inputs.at(4).low() == (uint64_t{1} << 7));
	CHECK(!roll.canRedo.get());
	roll.run("fill", {int64_t{25}}, error);
	CHECK(inputs.size() == 25);
	roll.run("undo", {}, error);
	CHECK(inputs.size() == 10 && roll.frameCount.get() == 10);
	roll.run("redo", {}, error);
	CHECK(inputs.size() == 25);
	roll.run("undo", {}, error);
	/* a new edit after an undo forgets what could be redone */
	roll.run("toggle", {int64_t{5}, int64_t{0}}, error);
	CHECK(!roll.canRedo.get());
	roll.run("undo", {}, error);
	CHECK(inputs.at(5).low() == 0 && inputs.size() == 10);

	/* playing runs at the machine's own rate and plays what the log holds */
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() >= 30; }, 10));
	/* and its sound reaches the speakers (OpenAL's null device, in the test) */
	if (session.sound.get() == "Sound on") CHECK(session.soundQueued.get() > 0);
	else std::printf("SKIP the sound check: %s\n", session.sound.get().c_str());
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	const int64_t stopped = session.frame.get();
	d.until([] { return false; }, 0.2);
	CHECK(session.frame.get() == stopped);
	CHECK(inputs.at(3).low() == (uint64_t{1} << 7)); /* played, not overwritten */
	CHECK(inputs.size() >= stopped);

	/* seeking: back to a frame and forward again draws the same picture,
	 * and a seek near a stored state runs only the last frame */
	auto pictureNow = [&] {
		Picture::Frame p;
		session.screen.takeIfNewer(0, p);
		return p.pixels;
	};
	auto seek = [&](int64_t f) {
		std::string e;
		session.run("seek", {f}, e);
		const bool there = d.until([&] { return session.frame.get() == f; }, 20);
		d.until([] { return false; }, 0.05); /* let the last report land */
		return there;
	};
	const std::vector<uint32_t> atStop = pictureNow();
	CHECK(session.greenEnd.get() == stopped);
	CHECK(session.greenzoneStates.get() > 0);
	CHECK(seek(10));
	CHECK(session.frame.get() == 10 && session.lastSeekFrames.get() == 1);
	CHECK(session.greenEnd.get() == stopped); /* going back changes no input */
	CHECK(seek(stopped));
	CHECK(pictureNow() == atStop);

	/* an edit before the current frame: the states after it go, the
	 * machine goes back to it and runs on to where it was (autoRestore) */
	session.autoRestore.set(true);
	const auto right = std::find(session.buttonNames().begin(), session.buttonNames().end(), "P1 Right") - session.buttonNames().begin();
	CHECK(right < 8);
	roll.run("paint", {int64_t{5}, int64_t{25}, static_cast<int64_t>(right), true}, error);
	CHECK(d.until([&] { return session.frame.get() == stopped && session.greenEnd.get() == stopped && session.lastSeekFrames.get() > 0; }, 20));
	d.until([] { return false; }, 0.1);
	const std::vector<uint32_t> walked = pictureNow();
	CHECK(walked != atStop); /* the walker walked: the edit reached the machine */
	/* and taking the edit back gives the first picture again */
	roll.run("paint", {int64_t{5}, int64_t{25}, static_cast<int64_t>(right), false}, error);
	CHECK(d.until([&] { return session.frame.get() == stopped && session.greenEnd.get() == stopped; }, 20));
	d.until([] { return false; }, 0.1);
	CHECK(pictureNow() == atStop);
	/* without autoRestore the machine stays at the edit */
	session.autoRestore.set(false);
	roll.run("toggle", {int64_t{3}, static_cast<int64_t>(right)}, error);
	CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
	CHECK(session.greenEnd.get() == 3);
	roll.run("toggle", {int64_t{3}, static_cast<int64_t>(right)}, error);
	session.autoRestore.set(true);

	/* The engine honours an edit behind the machine whoever asks next: a
	 * seek forward past it, or one frame - with no piano roll to send the
	 * machine back. What it gives equals a whole replay from frame 0. The
	 * edits span several frames: on some the game reads no input. */
	{
		const int64_t at = session.frame.get();
		std::vector<Buttons> kept(20);
		inputs.read(0, kept);
		auto flip = [&](int64_t first, int64_t last) {
			for (int64_t f = first; f <= last; f++) inputs.set(f, Buttons::first64(inputs.at(f).low() ^ (uint64_t{1} << right))); /* straight into the engine's movie */
		};
		auto replayed = [&](int64_t to) {
			std::string e;
			session.run("seek", {int64_t{0}}, e);
			d.until([&] { return session.frame.get() == 0; }, 20);
			session.run("seek", {to}, e);
			d.until([&] { return session.frame.get() == to; }, 20);
			return session.readDomain("RAM");
		};
		flip(2, 9);
		session.run("seek", {at + 5}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 5; }, 20));
		const std::vector<uint8_t> forward = session.readDomain("RAM");
		CHECK(replayed(at + 5) == forward);
		/* and one frame: the same frames back as they were, which matter */
		flip(2, 9);
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 6; }, 20));
		const std::vector<uint8_t> stepped = session.readDomain("RAM");
		CHECK(replayed(at + 6) == stepped);
		for (int64_t f = 0; f < 20; f++) inputs.set(f, kept[static_cast<size_t>(f)]);
		inputs.resize(at);
		session.run("seek", {at}, error);
		CHECK(d.until([&] { return session.frame.get() == at; }, 20));
	}

	/* A long way back after an edit goes as a seek asked for goes, never in
	 * one run that keeps the window waiting (issue 406): a request the window
	 * waits on is answered on the way. With Turbo seek in pieces; without it
	 * played at the machine's speed, every frame drawn, the movie's frames
	 * played as they are even in recording mode, and stopped where it was */
	{
		const int64_t at = session.frame.get();
		const int64_t far = 200000;
		auto waitedMs = [&] {
			const auto began = std::chrono::steady_clock::now();
			std::string e;
			session.run("frameNow", {}, e);
			return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
		};
		/* the longest the window waited in the second and a half after an
		 * edit, while the way back went on; then that it got there */
		auto watchWayBack = [&](int64_t to, double &worst) {
			worst = 0;
			const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(1500);
			while (std::chrono::steady_clock::now() < end)
			{
				worst = std::max(worst, waitedMs());
				d.until([] { return false; }, 0.02);
			}
			return d.until([&] { return session.frame.get() == to && !session.running.get() && session.seekingTo.get() < 0; }, 60);
		};
		session.greenzonePeriod.set(0);
		roll.run("fill", {far}, error);
		session.run("seek", {far}, error);
		CHECK(d.until([&] { return session.frame.get() == far; }, 60));
		double worst = 0;
		roll.run("toggle", {int64_t{3}, static_cast<int64_t>(right)}, error);
		const auto began = std::chrono::steady_clock::now();
		CHECK(watchWayBack(far, worst));
		const double took = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
		std::printf("turbo way back: %.0f ms, the window waited %.0f ms at most\n", took, worst);
		CHECK(took > 1000 && worst < 500);
		/* the green arrow kept for the edit after it */
		CHECK(session.restoreFrame.get() == far);
		/* played, at the machine's speed (here as fast as it goes) */
		roll.run("paint", {int64_t{100}, int64_t{199}, static_cast<int64_t>(right), true}, error);
		CHECK(d.until([&] { return session.frame.get() == far && !session.running.get(); }, 60));
		const int64_t near = 20000;
		session.run("seek", {near}, error);
		CHECK(d.until([&] { return session.frame.get() == near; }, 60));
		session.turboSeek.set(false);
		session.unthrottled.set(true);
		roll.run("toggle", {int64_t{3}, static_cast<int64_t>(right)}, error);
		CHECK(watchWayBack(near, worst));
		std::printf("played way back: the window waited %.0f ms at most\n", worst);
		CHECK(worst < 500 && session.restoreFrame.get() == near);
		/* a seek that plays takes the movie's frames as they are, recording
		 * mode or not (an edit turns recording off, as Chimera's TAStudio
		 * does, so it is turned on after the last edit) */
		session.autoRestore.set(false);
		roll.run("toggle", {int64_t{60}, static_cast<int64_t>(right)}, error);
		CHECK(d.until([&] { return session.frame.get() == 60 && !session.running.get(); }, 60));
		roll.run("toggle", {int64_t{60}, static_cast<int64_t>(right)}, error);
		session.autoRestore.set(true);
		session.recordingMode.set(true);
		session.run("seek", {int64_t{300}}, error);
		CHECK(d.until([&] { return session.frame.get() == 300 && !session.running.get(); }, 30));
		bool kept = session.recordingMode.get();
		for (int64_t f = 100; f <= 199; f++) kept = kept && (inputs.at(f).low() >> right & 1) != 0;
		CHECK(kept && (inputs.at(250).low() >> right & 1) == 0);
		session.recordingMode.set(false);
		session.unthrottled.set(false);
		session.turboSeek.set(true);
		session.greenzonePeriod.set(1);
		for (int64_t f = 100; f <= 199; f++) inputs.set(f, Buttons::first64(inputs.at(f).low() & ~(uint64_t{1} << right)));
		inputs.resize(at);
		session.run("seek", {at}, error);
		CHECK(d.until([&] { return session.frame.get() == at; }, 60));
	}

	/* restart: frame 0 again, the inputs kept */
	const int64_t logged = inputs.size();
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 0 && session.isOpen.get(); }, 30));
	CHECK(inputs.size() == logged);

	/* commands a view could not reach while closed */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 10));
	CHECK(!session.run("play", {}, error) && error.find("cannot run now") != std::string::npos);
	return true;
}

/* RAM Search: after "not equal to the value at the start", the candidates are
 * exactly the addresses whose byte differs between two reads of RAM */
bool ramSearch(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	RamSearchViewModel search(d, session);
	RamWatchViewModel watches(d, session);
	search.setRamWatch(&watches);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get() && search.domains.get().find("RAM") != std::string::npos; }, 60));
	session.recordingMode.set(true);
	search.set("domain", std::string("RAM"), error);
	search.set("size", int64_t{1}, error);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 10));
	const std::vector<uint8_t> before = session.readDomain("RAM");
	CHECK(!before.empty());
	CHECK(search.run("newSearch", {}, error).has_value());
	CHECK(d.until([&] { return search.searching.get() && search.count.get() == static_cast<int64_t>(before.size()); }, 20));
	CHECK(search.searchedDomain.get() == "RAM" && search.status.get().empty() && !search.rebootNeeded.get());
	{
		/* as Chimera writes the count */
		std::string expect = std::to_string(before.size());
		for (int at = static_cast<int>(expect.size()) - 3; at > 0; at -= 3) expect.insert(static_cast<size_t>(at), ",");
		CHECK(search.totalText.get() == expect + " addresses");
	}

	/* the walker walks down for ten frames */
	session.key("Down", true);
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 11; }, 20));
	session.key("Down", false);
	const std::vector<uint8_t> after = session.readDomain("RAM");
	std::vector<std::string> changed;
	const int digits = watch::addressDigits(static_cast<int64_t>(before.size()) - 1);
	for (size_t a = 0; a < before.size() && a < after.size(); a++)
		if (before[a] != after[a]) changed.push_back(watch::hex(static_cast<int64_t>(a), digits));
	CHECK(!changed.empty());
	search.set("compare", int64_t{0}, error); /* the previous value */
	search.set("op", int64_t{5}, error);      /* not equal */
	RowModel &rows = *search.rowLists()[0].model;
	/* the preview: the rows the search would remove are marked before it is made */
	d.until([] { return false; }, 0.1);
	int64_t marked = 0;
	for (int64_t r = 0; r < rows.rowCount(); r++) marked += rows.cell(r, 6) == Value(true);
	CHECK(marked == static_cast<int64_t>(before.size() - changed.size()));
	search.run("search", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(changed.size()); }, 20));
	CHECK(search.status.get() == std::to_string(before.size() - changed.size()) + " addresses removed");
	std::vector<std::string> listed;
	for (int64_t r = 0; r < rows.rowCount(); r++)
	{
		std::string a;
		fromValue(rows.cell(r, 0), a);
		listed.push_back(a);
	}
	std::sort(listed.begin(), listed.end());
	CHECK(listed == changed);
	/* the value column is the byte now, shown each way a byte can be, as Chimera names them */
	CHECK(search.displays.get() == "Unsigned\nSigned\nHex\nBinary");
	std::string address, shownValue;
	fromValue(rows.cell(0, 0), address);
	const uint8_t byte = after[std::stoul(address, nullptr, 16)];
	fromValue(rows.cell(0, 1), shownValue);
	CHECK(shownValue == std::to_string(byte));
	search.set("display", int64_t{2}, error);
	fromValue(rows.cell(0, 1), shownValue);
	CHECK(shownValue == watch::format(byte, 1, watch::Display::Hex));
	/* a specific value typed as shown: in hex, the candidates holding it are kept */
	const int64_t before2 = search.count.get();
	search.set("compare", int64_t{1}, error);
	search.set("op", int64_t{0}, error);
	search.set("specificValue", watch::format(byte, 1, watch::Display::Hex), error);
	CHECK(search.run("search", {}, error).has_value());
	CHECK(d.until([&] { return search.count.get() < before2 || search.status.get() == "0 addresses removed"; }, 20));
	d.until([] { return false; }, 0.1);
	for (int64_t r = 0; r < rows.rowCount(); r++)
	{
		fromValue(rows.cell(r, 1), shownValue);
		CHECK(shownValue == watch::format(byte, 1, watch::Display::Hex));
	}
	search.set("specificValue", std::string("0x10"), error);
	CHECK(!search.run("search", {}, error) && error == "\"0x10\" is not a value shown as Hex");
	search.set("specificValue", std::string(""), error);
	CHECK(!search.run("search", {}, error) && error == "Type a value to compare with");
	/* taken back, and done again */
	search.set("compare", int64_t{0}, error);
	search.set("op", int64_t{5}, error);
	CHECK(search.canUndo.get());
	search.run("undo", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(changed.size()); }, 20));
	search.run("undo", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(before.size()); }, 20));
	CHECK(search.status.get() == std::to_string(before.size() - changed.size()) + " addresses restored");
	CHECK(search.canRedo.get());
	search.run("redo", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(changed.size()); }, 20));
	/* picked, a range of them; the first removed */
	search.run("select", {int64_t{0}, std::string("only")}, error);
	CHECK(search.selectedCount.get() == 1 && rows.cell(0, 5) == Value(true) && search.canPoke.get());
	if (rows.rowCount() > 2)
	{
		search.run("select", {int64_t{2}, std::string("range")}, error);
		CHECK(search.selectedCount.get() == 3 && rows.cell(1, 5) == Value(true));
		search.run("select", {int64_t{1}, std::string("toggle")}, error);
		CHECK(search.selectedCount.get() == 2 && rows.cell(1, 5) == Value(false));
	}
	search.run("selectAll", {}, error);
	CHECK(search.selectedCount.get() == search.count.get());
	search.run("selectAll", {}, error);
	CHECK(search.selectedCount.get() == 0);
	/* Go to Address picks its row */
	std::string second;
	fromValue(rows.cell(1, 0), second);
	CHECK(search.run("goTo", {second}, error) == Value(int64_t{1}) && search.selectedCount.get() == 1 && rows.cell(1, 5) == Value(true));
	CHECK(search.run("goTo", {std::string("FFFFFF")}, error) == Value(int64_t{-1}));
	/* added to RAM Watch at the search's size and display */
	CHECK(search.run("addToRamWatch", {}, error) == Value(int64_t{1}) && watches.count.get() == 1);
	CHECK(watches.watches()[0].address == std::stoll(second, nullptr, 16) && watches.watches()[0].display == watch::Display::Hex);
	/* poked: the byte written */
	CHECK(search.run("pokeSelected", {std::string("7E")}, error) == Value(true) &&
		session.readDomain("RAM")[static_cast<size_t>(std::stoll(second, nullptr, 16))] == 0x7E);
	/* removed: taken out, and taken back */
	const int64_t left = search.count.get();
	search.run("removeSelected", {}, error);
	CHECK(d.until([&] { return search.count.get() == left - 1; }, 20) && search.status.get() == "1 address removed" && search.selectedCount.get() == 0);
	search.run("undo", {}, error);
	CHECK(d.until([&] { return search.count.get() == left; }, 20));
	/* sorted by address, then the other way round */
	search.run("sort", {std::string("Address")}, error);
	d.until([] { return false; }, 0.1);
	std::string first, last;
	fromValue(rows.cell(0, 0), first);
	fromValue(rows.cell(rows.rowCount() - 1, 0), last);
	CHECK(first <= last);
	search.run("sort", {std::string("Address")}, error);
	d.until([] { return false; }, 0.1);
	fromValue(rows.cell(0, 0), first);
	CHECK(first == last);
	/* saved as a .wch file and read back: the same candidates */
	const std::filesystem::path file = std::filesystem::temp_directory_path() /
		("chimera-test-search-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".wch");
	CHECK(search.run("save", {file.string()}, error).has_value() && search.status.get() == file.filename().string() + " saved");
	search.run("newSearch", {}, error);
	CHECK(d.until([&] { return search.count.get() == static_cast<int64_t>(before.size()); }, 20));
	CHECK(search.run("open", {file.string(), std::string("open")}, error) == Value(left));
	CHECK(d.until([&] { return search.count.get() == left; }, 20) && search.status.get() == file.filename().string() + " loaded");
	/* truncated by the same file: nothing left */
	search.run("open", {file.string(), std::string("truncate")}, error);
	CHECK(d.until([&] { return search.count.get() == 0; }, 20));
	CHECK(search.recentFiles.get() == file.string());
	std::error_code ec;
	std::filesystem::remove(file, ec);
	/* four bytes: seven ways, the same way kept where it fits, else the first */
	search.set("display", int64_t{3}, error);
	search.set("size", int64_t{4}, error);
	CHECK(search.displays.get().find("Float") != std::string::npos && search.display.get() == 3);
	search.set("display", int64_t{6}, error);
	search.set("size", int64_t{2}, error);
	CHECK(search.display.get() == 0);
	/* Fast mode: no change counts (the column goes), and a new search is needed */
	search.set("previousType", int64_t{2}, error);
	search.set("fast", true, error);
	CHECK(search.rebootNeeded.get() && search.previousType.get() == 1 && search.hiddenColumns.get() == "Diff,Changes");
	search.set("fast", false, error);
	CHECK(search.hiddenColumns.get() == "Diff");
	/* the machine closing ends the search: its memory goes with it */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !search.searching.get() && search.count.get() == 0; }, 20));
	return true;
}

/* Which settings apply: a setting naming machines belongs to them alone,
 * the machine being what the package's machineSetting says */
/* Chimera's setting types of issue 358: uint, long and ulong hold their C types' ranges exactly, a type left
 * out is inferred wider past an int's range, and a datetime is typed as Chimera
 * shows it, in UTC, and kept as its seconds since 1970 */
void settingTypes()
{
	json::Json decl;
	std::string error;
	CHECK(json::parse(R"([
		{"name": "u", "type": "uint", "default": 0},
		{"name": "l", "type": "long", "default": 0},
		{"name": "ul", "type": "ulong", "default": 0},
		{"name": "bounded", "type": "ulong", "default": 1, "min": 1, "max": 18446744073709551614},
		{"name": "clock", "type": "datetime", "default": 946684800},
		{"name": "after", "type": "datetime", "default": 946684800, "min": 946684800},
		{"name": "big", "default": 5000000000},
		{"name": "huge", "default": 18446744073709551615},
		{"name": "small", "default": 8}
	])", decl, error));
	SettingsForm form;
	form.declare(decl.items, "");
	json::Json none;
	none.kind = json::Json::Kind::Object;
	form.setValues(none);
	form.evaluate();
	auto written = [&](const std::string &name) {
		const json::Json *v = form.values().find(name);
		return v != nullptr ? json::write(*v) : std::string("none");
	};
	/* each whole-number type holds what its C type holds, exactly */
	CHECK(form.set("u", int64_t{4294967295}, error) && written("u") == "4294967295");
	CHECK(!form.set("u", int64_t{-1}, error) && error == "u goes from 0 to 4294967295");
	CHECK(!form.set("u", std::string("5000000000"), error));
	CHECK(form.set("l", std::string("-9223372036854775808"), error) && written("l") == "-9223372036854775808");
	CHECK(form.set("ul", std::string("18446744073709551615"), error) && written("ul") == "18446744073709551615");
	CHECK(!form.set("ul", int64_t{-5}, error) && !form.set("ul", std::string("12x"), error) && error == "ul is a whole number");
	/* what the window is given: the digits, every one of them */
	CHECK(SettingsForm::valueOf(*form.declaration("ul"), form.values()) == Value(std::string("18446744073709551615")));
	/* declared bounds kept exactly at 64 bits */
	CHECK(!form.set("bounded", std::string("18446744073709551615"), error) && error == "bounded goes from 1 to 18446744073709551614");
	CHECK(form.set("bounded", std::string("18446744073709551614"), error) && written("bounded") == "18446744073709551614");
	/* a default past an int's range is a wider type */
	CHECK(SettingsForm::typeOf(*form.declaration("big")) == "long" && SettingsForm::typeOf(*form.declaration("huge")) == "ulong");
	CHECK(SettingsForm::typeOf(*form.declaration("small")) == "int");
	/* a date and time: shown as Chimera shows it, typed in its three forms, kept as seconds */
	CHECK(SettingsForm::valueOf(*form.declaration("clock"), form.values()) == Value(std::string("2000-01-01 00:00:00")));
	CHECK(form.set("clock", std::string("2000-01-01"), error) && written("clock") == "946684800");
	CHECK(form.set("clock", std::string("2100-01-01 00:00"), error) && written("clock") == "4102444800");
	CHECK(form.set("clock", std::string("1969-12-31 00:00:00"), error) && written("clock") == "-86400");
	CHECK(form.set("clock", std::string("2010-06-15 12:30:00"), error) && written("clock") == "1276605000");
	CHECK(!form.set("clock", std::string("15/06/2010"), error) && error == "clock is a date and time, written as 2000-01-01 00:00:00 (UTC)");
	CHECK(!form.set("clock", std::string("2001-02-29"), error));
	CHECK(SettingsForm::dateText(253402300799) == "9999-12-31 23:59:59" && SettingsForm::dateText(-62135596800) == "0001-01-01 00:00:00");
	CHECK(!form.set("after", std::string("1990-01-01"), error) && error == "after goes from 2000-01-01 00:00:00 to 9999-12-31 23:59:59");
	/* the values reach the core with every digit */
	CHECK(json::write(form.recorded()).find("\"ul\": 18446744073709551615") != std::string::npos);
}

void settingsByMachine()
{
	json::Json decl;
	std::string error;
	CHECK(json::parse(R"([
		{"name": "machine", "type": "enum", "options": ["pc", "pc98"], "default": "pc"},
		{"name": "everywhere", "type": "bool", "default": false},
		{"name": "sound", "type": "enum", "options": ["sb", "none"], "default": "sb", "when": ["pc"]},
		{"name": "fm", "type": "bool", "default": true, "when": ["pc98"]}
	])", decl, error));
	json::Json values;
	values.kind = json::Json::Kind::Object;
	auto names = [&](const std::vector<size_t> &shown) {
		std::string out;
		for (size_t i : shown) out += decl.items[i].find("name")->text + " ";
		return out;
	};
	CHECK(names(SettingsViewModel::applicable(decl.items, "{}", values, "machine")) == "machine everywhere sound ");
	json::Json pc98;
	CHECK(json::parse(R"({"machine": "pc98"})", pc98, error));
	CHECK(names(SettingsViewModel::applicable(decl.items, "{}", pc98, "machine")) == "machine everywhere fm ");
	/* a package with no machineSetting shows them all */
	CHECK(names(SettingsViewModel::applicable(decl.items, "{}", values, "")) == "machine everywhere sound fm ");
}

/* Settings: the synthetic core declares one, the byte its RAM starts filled
 * with. Set and applied, the machine starts again with it, and its RAM says so */
bool settings(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	SettingsViewModel settings(d, session);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get() && settings.shown.get() == 1; }, 60));
	RowModel &rows = *settings.rowLists()[0].model;
	std::string name, type;
	fromValue(rows.cell(0, SettingsViewModel::kName), name);
	fromValue(rows.cell(0, SettingsViewModel::kType), type);
	CHECK(name == "initFillByte" && type == "int");
	int64_t value = -1;
	fromValue(rows.cell(0, SettingsViewModel::kValue), value);
	CHECK(value == 0);
	auto filled = [&](uint8_t b) {
		const std::vector<uint8_t> ram = session.readDomain("RAM");
		return std::count(ram.begin(), ram.end(), b);
	};
	const auto zeros = filled(0);
	CHECK(zeros > 1000); /* most of the 4 KiB is never written by the game */

	/* refused: out of range, not declared, not a number */
	CHECK(!settings.run("set", {std::string("initFillByte"), int64_t{256}}, error) && error.find("goes from 0 to 255") != std::string::npos);
	CHECK(!settings.run("set", {std::string("nothing"), int64_t{1}}, error) && error.find("no setting nothing") != std::string::npos);
	CHECK(!settings.run("set", {std::string("initFillByte"), std::string("lots")}, error));

	/* set, discarded, set again, applied; a value set updates its row in
	 * place, so a list keeps where it was scrolled to (issue 417) */
	int resets = 0;
	Connection resetSeen = rows.reset.connect([&] { resets++; });
	CHECK(settings.run("set", {std::string("initFillByte"), int64_t{171}}, error).has_value());
	CHECK(resets == 0);
	resetSeen.disconnect();
	CHECK(settings.pending.get());
	bool edited = false;
	fromValue(rows.cell(0, SettingsViewModel::kEdited), edited);
	CHECK(edited);
	settings.run("discard", {}, error);
	CHECK(!settings.pending.get());
	settings.run("set", {std::string("initFillByte"), int64_t{171}}, error);
	const int64_t opensBefore = session.opens.get();
	/* the answer comes after the machine has opened again, and the reload's own
	 * word may follow it at once: heard as it is said, not looked for after */
	bool startedAgain = false;
	Connection said = settings.status.changed.connect([&] {
		if (settings.status.get().find("started again") != std::string::npos) startedAgain = true;
	});
	settings.run("apply", {}, error);
	CHECK(d.until([&] { return session.opens.get() > opensBefore && !settings.pending.get() && startedAgain; }, 60));
	said.disconnect();
	CHECK(filled(0xAB) >= zeros); /* the machine started with its RAM filled with 171 */
	fromValue(rows.cell(0, SettingsViewModel::kValue), value);
	CHECK(value == 171);
	/* and the restart (File > Restart) keeps it */
	const int64_t opensAgain = session.opens.get();
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() > opensAgain; }, 60));
	CHECK(filled(0xAB) >= zeros);
	/* another game starts from the package's own settings */
	const int64_t opensThird = session.opens.get();
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.opens.get() > opensThird; }, 60));
	CHECK(filled(0) == zeros);
	return true;
}

std::string readFile(const std::filesystem::path &p)
{
	std::ifstream in(p, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

/* A project: the synthetic game's winning movie, played to its end, leaves
 * the machine's memory exactly as the witness's recorded run does; saved and
 * opened again, it holds the same inputs, and an edit survives the trip. */
bool project(const char *package, const char *rom, const std::string &movies, const std::string &goldens)
{
	namespace fs = std::filesystem;
	const std::string movie = readFile(movies + "/gridWalker.win.txt");
	const std::string ram = readFile(goldens + "/gridWalker.win.ram.bin"), vram = readFile(goldens + "/gridWalker.win.vram.bin");
	if (movie.empty() || ram.empty() || vram.empty())
	{
		std::printf("SKIP the project tests: the witness's movie or goldens are missing\n");
		return false;
	}
	std::vector<std::string> entries;
	std::istringstream lines(movie);
	for (std::string l; std::getline(lines, l);)
	{
		if (!l.empty() && l.back() == '\r') l.pop_back();
		if (!l.empty() && l[0] == '|') entries.push_back(l);
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-vm-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "gridWalker.testrom");
	char sha[41];
	const std::string romBytes = readFile(rom);
	ce_sha1_hex(reinterpret_cast<const uint8_t *>(romBytes.data()), romBytes.size(), sha);
	std::string inputsJson = "{\"columns\": [[\"P1 Up\", \"P1 Down\", \"P1 Left\", \"P1 Right\", \"P1 A\", \"P1 B\", \"P1 Select\", \"P1 Start\"]], "
		"\"entries\": [";
	for (size_t i = 0; i < entries.size(); i++) inputsJson += (i ? ", \"" : "\"") + entries[i] + "\"";
	inputsJson += "]}";
	{
		std::ofstream out(dir / "p.chimeraProject", std::ios::binary);
		out << "{\"title\": \"gridWalker.win\", \"core\": {\"name\": \"Synth\", \"version\": \"\", \"sha1\": \"\"}, "
			<< "\"headers\": {\"MovieVersion\": \"Chimera Project File v1.1\", \"Platform\": \"Synth\"}, "
			<< "\"files\": [{\"name\": \"gridWalker.testrom\", \"sha1\": \"" << sha << "\", \"slot\": \"rom\"}], "
			<< "\"inputs\": " << inputsJson << "}";
	}

	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	std::string error;
	auto openIt = [&](const fs::path &p) {
		std::string e;
		const int64_t was = session.opens.get();
		session.run("openProject", {(p).string(), std::string(package)}, e);
		return d.until([&] { return session.opens.get() > was && session.projectPath.get() == p.string(); }, 60);
	};
	CHECK(openIt(dir / "p.chimeraProject"));
	if (!session.isOpen.get())
	{
		std::fprintf(stderr, "FAIL: the project did not open: %s\n", session.status.get().c_str());
		failures++;
		return true;
	}
	CHECK(session.projectTitle.get() == "gridWalker.win");
	CHECK(session.recentProjects.get() == (dir / "p.chimeraProject").string());
	CHECK(roll.frameCount.get() == static_cast<int64_t>(entries.size()));
	CHECK(inputs.at(0).low() == 0 && inputs.at(1).low() == (uint64_t{1} << 1)); /* "|.D......|" is P1 Down */

	/* played to its end: the witness's memory, byte for byte */
	const int64_t end = static_cast<int64_t>(entries.size());
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
	CHECK(session.readDomain("VRAM") == std::vector<uint8_t>(vram.begin(), vram.end()));
	/* the comparison can fail: without its Down presses the movie ends elsewhere
	 * (it does not notice a shift of one frame: this movie ends the same way) */
	roll.run("paint", {int64_t{0}, end - 1, int64_t{1}, false}, error);
	CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
	CHECK(session.readDomain("RAM") != std::vector<uint8_t>(ram.begin(), ram.end()));
	CHECK(openIt(dir / "p.chimeraProject"));

	/* saved and opened again: the same inputs, and the same input text */
	session.run("saveProject", {(dir / "saved.chimeraProject").string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == (dir / "saved.chimeraProject").string(); }, 20));
	std::vector<Buttons> before(entries.size()), after(entries.size());
	inputs.read(0, before);
	CHECK(openIt(dir / "saved.chimeraProject"));
	inputs.read(0, after);
	CHECK(before == after && inputs.size() == end);
	CHECK(readFile(dir / "saved.chimeraProject").find("|.D......|") != std::string::npos);

	/* File > Restart on a project: frame 0 again, the movie as edited */
	{
		const uint64_t was = inputs.at(40).low();
		roll.run("toggle", {int64_t{40}, int64_t{5}}, error);
		const int64_t opensBefore = session.opens.get();
		session.run("restart", {}, error);
		CHECK(d.until([&] { return session.opens.get() > opensBefore; }, 30));
		CHECK(session.isOpen.get() && session.frame.get() == 0 && session.projectPath.get() == (dir / "saved.chimeraProject").string());
		CHECK(inputs.at(40).low() == (was ^ (uint64_t{1} << 5)));
		roll.run("toggle", {int64_t{40}, int64_t{5}}, error);
	}

	/* markers survive the trip too */
	roll.run("addMarker", {int64_t{30}, std::string("the corner")}, error);
	roll.run("addMarker", {int64_t{12}, std::string("turn")}, error);
	CHECK(roll.markerCount.get() == 2);
	CHECK((roll.rowLists()[0].model->rowFlags(12) & PianoRollViewModel::kMarker) != 0);
	CHECK(roll.rowLists()[0].model->rowNote(30) == "the corner");
	/* the run's own markers count too: Last input when it comes between, Run start before */
	const int64_t lastIn = roll.lastInputFrame.get();
	CHECK(roll.runEndFrame.get() == inputs.size() - 1);
	CHECK(std::get<int64_t>(*roll.run("nextMarker", {int64_t{12}}, error)) == (lastIn > 12 && lastIn < 30 ? lastIn : 30));
	CHECK(std::get<int64_t>(*roll.run("previousMarker", {int64_t{12}}, error)) == (lastIn > 0 && lastIn < 12 ? lastIn : 0));
	CHECK(!roll.run("removeMarker", {int64_t{13}}, error) && error == "no marker on frame 13");
	const std::string beforeMarkers = readFile(dir / "saved.chimeraProject");
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return readFile(dir / "saved.chimeraProject") != beforeMarkers; }, 20));
	CHECK(openIt(dir / "p.chimeraProject"));
	CHECK(roll.markerCount.get() == 0); /* the other project has none */
	/* the newest first, each once */
	CHECK(session.recentProjects.get() == (dir / "p.chimeraProject").string() + "\n" + (dir / "saved.chimeraProject").string());
	CHECK(openIt(dir / "saved.chimeraProject"));
	CHECK(roll.markerCount.get() == 2 && roll.rowLists()[0].model->rowNote(12) == "turn");
	RowModel &marks = *roll.rowLists()[1].model;
	int64_t firstFrame = -1;
	fromValue(marks.cell(0, 0), firstFrame);
	/* the run's own three first, then the person's; and none of the three written in the project */
	std::string firstText;
	fromValue(marks.cell(0, 1), firstText);
	CHECK(marks.rowCount() == 5 && firstFrame == 0 && firstText == "Run start");
	{
		const std::string saved = readFile(dir / "saved.chimeraProject");
		CHECK(saved.find("Run start") == std::string::npos && saved.find("Last input") == std::string::npos && saved.find("Run end") == std::string::npos);
	}
	roll.run("removeMarker", {int64_t{12}}, error);
	CHECK(roll.markerCount.get() == 1);

	/* an edit survives the trip: P1 A on frame 10 */
	const std::string unedited = readFile(dir / "saved.chimeraProject");
	roll.run("toggle", {int64_t{10}, int64_t{4}}, error);
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return readFile(dir / "saved.chimeraProject") != unedited; }, 20));
	CHECK(openIt(dir / "p.chimeraProject"));
	CHECK(inputs.at(10) == before[10]);
	CHECK(openIt(dir / "saved.chimeraProject"));
	CHECK(inputs.at(10) == (before[10] | Buttons::first64(uint64_t{1} << 4)));
	/* branches: the winning movie kept at its end, the movie spoiled, the
	 * branch loaded - and the machine is where the winning movie leaves it.
	 * With a project open a branch keeps its state in the cache folder, so
	 * loading it is a state load, not a replay. */
	const fs::path cache = dir / "cache";
	session.cacheFolder.set(cache.string());
	auto filesNamed = [&](const char *extension) {
		std::vector<fs::path> found;
		std::error_code ec;
		for (const auto &e : fs::recursive_directory_iterator(cache, ec))
			if (e.path().extension() == extension) found.push_back(e.path());
		return found;
	};
	auto shown = [&] {
		Picture::Frame f;
		session.screen.takeIfNewer(~uint64_t{0}, f);
		return f.pixels;
	};
	CHECK(openIt(dir / "p.chimeraProject"));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	const std::vector<uint32_t> winPicture = shown();
	CHECK(branches.run("create", {std::string("win")}, error).has_value() && branches.count.get() == 1);
	CHECK(d.until([&] { return branches.made.get() == 1; }, 30));
	CHECK(filesNamed(".state").size() == 1 && filesNamed(".note").size() == 1);
	roll.run("addMarker", {int64_t{20}, std::string("in the branch")}, error);
	/* removed, its state file is kept while Undo may put it back */
	branches.run("remove", {int64_t{0}}, error);
	CHECK(branches.count.get() == 0 && branches.undoText.get() == "Undo Branch Removal");
	d.until([] { return false; }, 0.2);
	CHECK(filesNamed(".state").size() == 1);
	CHECK(branches.run("undo", {}, error).has_value() && branches.count.get() == 1 && branches.status.get() == "Branch Removal canceled");
	branches.run("remove", {int64_t{0}}, error);
	CHECK(branches.count.get() == 0);
	branches.run("create", {std::string("win")}, error);
	CHECK(d.until([&] { return branches.made.get() == 2; }, 30));
	/* a branch's own state file, by the name the list gives it */
	auto stateOf = [&](int64_t i) {
		std::string stem;
		fromValue(branches.rowLists()[0].model->cell(i, 4), stem);
		return stem;
	};
	CHECK(!stateOf(0).empty() && !filesNamed(".state").empty());
	const fs::path stateDir = filesNamed(".state").at(0).parent_path();
	CHECK(fs::exists(stateDir / (stateOf(0) + ".state")) && branches.status.get().find("its state kept") != std::string::npos);
	roll.run("removeMarker", {int64_t{20}}, error);
	auto spoil = [&] {
		roll.run("paint", {int64_t{0}, end - 1, int64_t{1}, false}, error);
		CHECK(d.until([&] { return session.frame.get() == end && session.greenEnd.get() == end; }, 30));
		d.until([] { return false; }, 0.1);
		CHECK(session.readDomain("RAM") != std::vector<uint8_t>(ram.begin(), ram.end()));
		CHECK(shown() != winPicture);
	};
	auto loadIt = [&](bool fromState) {
		const int64_t was = branches.loads.get();
		CHECK(branches.run("load", {int64_t{0}}, error).has_value());
		CHECK(d.until([&] { return branches.loads.get() > was && session.frame.get() == end; }, 30));
		d.until([] { return false; }, 0.1);
		CHECK(branches.loadedFromState.get() == fromState);
		/* a state load replays nothing: the states past where the movies
		 * part are not there again, as a replay would make them */
		CHECK(fromState ? session.greenEnd.get() < end : session.greenEnd.get() == end);
		CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
		CHECK(session.readDomain("VRAM") == std::vector<uint8_t>(vram.begin(), vram.end()));
		CHECK(shown() == winPicture);
	};
	spoil();
	loadIt(true);
	CHECK(roll.frameCount.get() == end && roll.markerCount.get() == 1 && roll.rowLists()[0].model->rowNote(20) == "in the branch");
	/* loaded from another frame, and played on: the session counts from
	 * the branch's frame */
	session.run("seek", {int64_t{5}}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 30));
	loadIt(true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == end + 1; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.frame.get() == end + 1 && inputs.size() == end + 1);
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	CHECK(!branches.run("load", {int64_t{3}}, error) && error == "no branch 3");
	/* a person loading another branch over changes the current one does not
	 * hold: it is loaded, and the message after it says so (owner,
	 * 2026-10-28, issue 395: no question) */
	{
		auto loaded = [&](int64_t was) { return d.until([&] { return branches.loads.get() > was; }, 30); };
		const std::string warning = "did not hold the changes made since it was loaded or updated (Undo Branch Load brings them back)";
		auto said = [&](const std::string &what) { return branches.status.get().find(what) != std::string::npos; };
		CHECK(branches.run("create", {std::string("second")}, error).has_value());
		CHECK(d.until([&] { return branches.made.get() == 3; }, 30) && branches.current.get() == 1);
		/* the current branch again, then nothing changed since: nothing said */
		int64_t was = branches.loads.get();
		CHECK(branches.run("loadWithWarning", {int64_t{1}}, error).has_value() && loaded(was) && !said(warning));
		was = branches.loads.get();
		CHECK(branches.run("loadWithWarning", {int64_t{0}}, error).has_value() && loaded(was) && branches.current.get() == 0 && !said(warning));
		was = branches.loads.get();
		CHECK(branches.run("loadWithWarning", {int64_t{1}}, error).has_value() && loaded(was) && branches.current.get() == 1);
		/* an input changed: loaded all the same, branch 2 left as it was, and said */
		roll.run("toggle", {int64_t{3}, int64_t{0}}, error);
		was = branches.loads.get();
		CHECK(branches.run("loadWithWarning", {int64_t{0}}, error).has_value() && loaded(was) && branches.current.get() == 0);
		CHECK(said("; branch 2 " + warning) && branches.made.get() == 3);
		/* a marker of the person's counts too */
		roll.run("addMarker", {int64_t{7}, std::string("only here")}, error);
		was = branches.loads.get();
		CHECK(branches.run("loadWithWarning", {int64_t{1}}, error).has_value() && loaded(was) && said("; branch 1 " + warning));
		/* a script's load says nothing of it */
		roll.run("toggle", {int64_t{4}, int64_t{0}}, error);
		was = branches.loads.get();
		CHECK(branches.run("load", {int64_t{0}}, error).has_value() && loaded(was) && !said(warning));
		/* a removal taken back: the branch loaded is still the one it was,
		 * though it moved down and back up (review, 2026-10-26) */
		was = branches.loads.get();
		CHECK(branches.run("load", {int64_t{1}}, error).has_value() && loaded(was) && branches.current.get() == 1);
		branches.run("remove", {int64_t{0}}, error);
		CHECK(branches.current.get() == 0);
		CHECK(branches.run("undo", {}, error).has_value() && branches.count.get() == 2 && branches.current.get() == 1);
		was = branches.loads.get();
		CHECK(branches.run("load", {int64_t{0}}, error).has_value() && loaded(was) && branches.current.get() == 0);
		branches.run("remove", {int64_t{1}}, error);
		CHECK(branches.count.get() == 1 && branches.current.get() == 0);
		session.run("seek", {end}, error);
		CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	}
	/* a state that does not fit is not loaded, and the branch is replayed
	 * to: a note naming another machine, then a state cut short */
	const fs::path stateFile = stateDir / (stateOf(0) + ".state"), noteFile = stateDir / (stateOf(0) + ".note");
	const std::string note = readFile(noteFile), state = readFile(stateFile);
	{
		std::string other = note;
		const size_t at = other.find("machine ");
		CHECK(at != std::string::npos);
		other.replace(at + 8, 16, "0123456789abcdef");
		std::ofstream(noteFile, std::ios::binary | std::ios::trunc) << other;
	}
	spoil();
	loadIt(false);
	std::ofstream(noteFile, std::ios::binary | std::ios::trunc) << note;
	std::ofstream(stateFile, std::ios::binary | std::ios::trunc) << state.substr(0, state.size() / 2);
	spoil();
	loadIt(false);
	CHECK(branches.status.get().find("its state could not be loaded") != std::string::npos);
	std::ofstream(stateFile, std::ios::binary | std::ios::trunc) << state;
	spoil();
	loadIt(true);
	/* Kept with the project, and found again by it. Saved under another name
	 * the project is a project of its own (owner, 2026-10-29): an id of its
	 * own, and a folder of its own that its branches' states come into, while
	 * the first file's folder keeps its own. What no branch has is cleared
	 * away from a project's folder when it opens, and from no other. */
	const fs::path firstFolder = stateFile.parent_path().parent_path();
	const std::string firstHistory = readFile(firstFolder / "history.bin");
	session.run("saveProject", {(dir / "branched.chimeraProject").string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == (dir / "branched.chimeraProject").string(); }, 20));
	json::Json branchedFile;
	std::string branchedWhy;
	CHECK(json::parse(readFile(dir / "branched.chimeraProject"), branchedFile, branchedWhy));
	const json::Json *idField = branchedFile.find("id");
	const std::string ownId = idField != nullptr ? idField->text : std::string();
	CHECK(!ownId.empty() && ownId != firstFolder.filename().string());
	const fs::path ownStates = cache / ownId / "branches";
	CHECK(readFile(ownStates / stateFile.filename()) == state && readFile(ownStates / noteFile.filename()) == note);
	/* the greenzone this save keeps goes to the new folder: the first file,
	 * opened again, finds the history its own save wrote */
	CHECK(d.until([&] { return fs::exists(cache / ownId / "history.bin"); }, 20));
	CHECK(readFile(firstFolder / "history.bin") == firstHistory);
	{
		std::ofstream(ownStates / "0123456789abcdef.state") << "left behind";
		std::ofstream(ownStates / "0123456789abcdef.note") << "left behind";
	}
	CHECK(openIt(dir / "branched.chimeraProject"));
	CHECK(branches.count.get() == 1);
	auto filesIn = [&](const fs::path &folder, const char *extension) {
		std::vector<fs::path> found;
		for (const fs::path &f : filesNamed(extension))
			if (f.parent_path() == folder) found.push_back(f);
		return found;
	};
	CHECK(filesIn(ownStates, ".state") == std::vector<fs::path>{ownStates / stateFile.filename()});
	CHECK(filesIn(ownStates, ".note") == std::vector<fs::path>{ownStates / noteFile.filename()});
	CHECK(fs::exists(stateFile) && fs::exists(noteFile));
	std::string branchName;
	int64_t branchFrames = 0;
	fromValue(branches.rowLists()[0].model->cell(0, 0), branchName);
	fromValue(branches.rowLists()[0].model->cell(0, 3), branchFrames);
	CHECK(branchName == "win" && branchFrames == end);
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	spoil();
	loadIt(true);

	/* The greenzone is kept with the project: saved, it is there when the
	 * project opens again, and going to the end runs from its states
	 * rather than from the start. */
	const fs::path projectCache = cache / ownId, history = projectCache / "history.bin";
	const std::string branched = (dir / "branched.chimeraProject").string();
	/* played through from the start, so it holds the frames near the end */
	session.run("seek", {int64_t{0}}, error);
	CHECK(d.until([&] { return session.frame.get() == 0; }, 30));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved " + branched, 0) == 0; }, 20));
	CHECK(openIt(branched));
	/* the machine says it is open a moment before the project's session marks itself running */
	CHECK(d.until([&] { return fs::exists(history) && fs::exists(projectCache / "session.running"); }, 10));
	CHECK(d.until([&] { return session.greenEnd.get() > 0; }, 10));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.lastSeekFrames.get() < end / 2);
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));
	/* a session that did not end keeps nothing it wrote: the mark it
	 * leaves while running is found, its greenzone is not loaded and goes */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(!fs::exists(projectCache / "session.running"));
	std::ofstream(projectCache / "session.running") << "a session that never ended";
	CHECK(openIt(branched));
	d.until([] { return false; }, 0.2);
	CHECK(session.greenEnd.get() == 0 && !fs::exists(history));
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	d.until([] { return false; }, 0.1);
	CHECK(session.lastSeekFrames.get() == end);
	CHECK(session.readDomain("RAM") == std::vector<uint8_t>(ram.begin(), ram.end()));

	/* Opened from another path - a file copied by hand, a moved one, a
	 * backup - the project keeps its id, but what its cache holds was made
	 * for the other file: the greenzone and the branches' states go, and the
	 * budgets stay. A cache that never said where its project was is left
	 * alone (user-decided, 2026-10-15). */
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved " + branched, 0) == 0; }, 20));
	CHECK(d.until([&] { return fs::exists(history); }, 20));
	CHECK(!filesIn(ownStates, ".state").empty());
	const std::string copied = (dir / "copied.chimeraProject").string();
	fs::copy_file(branched, copied, fs::copy_options::overwrite_existing);
	std::ofstream(projectCache / "budgets.json") << "{\"memoryMb\": 256}";
	CHECK(openIt(copied));
	d.until([] { return false; }, 0.2);
	CHECK(session.projectId.get() == ownId);
	CHECK(!fs::exists(history) && filesIn(ownStates, ".state").empty() && session.greenEnd.get() == 0);
	CHECK(fs::exists(projectCache / "budgets.json"));
	CHECK(readFile(projectCache / "about.json").find("copied.chimeraProject") != std::string::npos);
	session.run("seek", {end}, error);
	CHECK(d.until([&] { return session.frame.get() == end; }, 30));
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved " + copied, 0) == 0; }, 20));
	CHECK(d.until([&] { return fs::exists(history); }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove(projectCache / "about.json");
	CHECK(openIt(branched));
	CHECK(d.until([&] { return session.greenEnd.get() > 0; }, 10));
	fs::remove(projectCache / "budgets.json");

	/* opened with no package given: the installed core is found for it, and
	 * a cores folder without it says which core is missing */
	{
		const fs::path cores = dir / "Cores";
		fs::create_directories(cores);
		fs::copy_file(package, cores / "synth-box.chimeraCore");
		CoresViewModel installed(d);
		installed.set("coresFolder", (cores / "none").string(), error);
		session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
			[&installed] { return installed.firmwareFolders(); });
		session.run("openProject", {(dir / "p.chimeraProject").string()}, error);
		CHECK(d.until([&] { return session.status.get().find("No Synth core is installed") != std::string::npos; }, 20));
		installed.set("coresFolder", cores.string(), error);
		CHECK(installed.count.get() == 1);
		std::string coreName;
		fromValue(installed.rowLists()[0].model->cell(0, 0), coreName);
		CHECK(coreName == "Synth");
		const int64_t was = session.opens.get();
		session.run("openProject", {(dir / "p.chimeraProject").string()}, error);
		CHECK(d.until([&] { return session.opens.get() > was; }, 30));
		CHECK(session.coreName.get() == "Synth" && roll.frameCount.get() == end);
		session.setFinders({}, {});
		d.until([] { return false; }, 0.05);
	}

	/* a project whose file is not beside it waits, its files listed, as
	 * Chimera's resolution window has them: a wrong file is told apart by
	 * its hash, a folder scanned finds the right one under another name,
	 * and the next time it is found where it was, without asking */
	{
		const std::string project = (dir / "p.chimeraProject").string();
		/* the machine closed first: Windows moves no file one has open */
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		fs::create_directories(dir / "elsewhere");
		fs::rename(dir / "gridWalker.testrom", dir / "elsewhere" / "renamed.bin");
		/* another file: the game with a byte more, which the core still loads */
		{
			std::ofstream out(dir / "elsewhere" / "wrong.bin", std::ios::binary);
			out << readFile(dir / "elsewhere" / "renamed.bin") << '\0';
		}
		session.cacheFolder.set((dir / "cache").string());
		session.run("openProject", {project, std::string(package)}, error);
		CHECK(d.until([&] { return session.resolvingFiles.get() && session.projectFiles().size() == 1; }, 20));
		CHECK(!session.isOpen.get() && session.projectFiles()[0].name == "gridWalker.testrom" && session.projectFiles()[0].status == 1);
		CHECK(!session.run("openWithFiles", {}, error) && error == "a file is still not found");
		CHECK(session.run("locateProjectFile", {int64_t{0}, (dir / "elsewhere" / "wrong.bin").string()}, error).has_value());
		CHECK(d.until([&] { return session.projectFiles()[0].status == 2; }, 20));
		CHECK(session.run("scanForProjectFiles", {(dir / "elsewhere").string(), true}, error) == Value(int64_t{1}));
		CHECK(d.until([&] { return session.projectFiles()[0].status == 0; }, 20));
		const int64_t opened = session.opens.get();
		CHECK(session.run("openWithFiles", {}, error).has_value() && !session.resolvingFiles.get());
		CHECK(d.until([&] { return session.opens.get() > opened && session.isOpen.get(); }, 30));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		/* found where it was: no asking */
		const int64_t again = session.opens.get();
		session.run("openProject", {project, std::string(package)}, error);
		CHECK(d.until([&] { return session.opens.get() > again && session.isOpen.get(); }, 30));
		CHECK(!session.resolvingFiles.get());
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		/* gone altogether: it waits, and is left unopened when asked */
		fs::remove(dir / "elsewhere" / "renamed.bin");
		session.run("openProject", {project, std::string(package)}, error);
		CHECK(d.until([&] { return session.resolvingFiles.get(); }, 20));
		CHECK(session.run("cancelOpen", {}, error).has_value());
		CHECK(d.until([&] { return session.status.get() == "The project was not opened" && session.projectFiles().empty(); }, 20));
		CHECK(!session.isOpen.get() && !session.resolvingFiles.get());
		/* a file that is not the recorded one opens when asked to anyway (Chimera's Open Anyway) */
		session.run("openProject", {project, std::string(package)}, error);
		CHECK(d.until([&] { return session.resolvingFiles.get(); }, 20));
		session.run("locateProjectFile", {int64_t{0}, (dir / "elsewhere" / "wrong.bin").string()}, error);
		CHECK(d.until([&] { return session.projectFiles()[0].status == 2; }, 20));
		const int64_t anyway = session.opens.get();
		CHECK(session.run("openWithFiles", {}, error).has_value());
		CHECK(d.until([&] { return session.opens.get() > anyway && session.isOpen.get(); }, 30));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	removeTree(dir);
	return true;
}

/* A game made a project, as the New Project window makes one: reopened, it
 * has the same movie, its core, its machine and every setting that applies */
bool newProject(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-new-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	const fs::path project = dir / "walker.chimeraProject";
	openGame(session, package, (dir / "walker.testrom").string(), error, project, "down the corridor");
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	session.key("Down", true);
	for (int i = 0; i < 5; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 5; }, 20));
	session.key("Down", false);
	std::vector<Buttons> made(5);
	inputs.read(0, made);
	session.run("saveProject", {}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
	const std::string text = readFile(project);
	CHECK(text.find("\"Platform\"") != std::string::npos && text.find("initFillByte") != std::string::npos &&
		text.find("walker.testrom") != std::string::npos && text.find("Synth") != std::string::npos);
	/* opened again in a new session (one machine at a time: the sandbox
	 * puts each at the same address) */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	InputLog inputs2;
	SessionViewModel session2(d, inputs2);
	session2.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session2.isOpen.get() && session2.projectPath.get() == project.string(); }, 60));
	std::vector<Buttons> back(5);
	inputs2.read(0, back);
	CHECK(session2.projectTitle.get() == "down the corridor" && inputs2.size() == 5 && back == made);
	CHECK(made[0].any()); /* Down was held */
	/* closed before its folder goes: Windows removes no file a machine has open */
	session2.run("close", {}, error);
	CHECK(d.until([&] { return !session2.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Themes: built in and from the folder, one chosen; files that do not read
 * are named, colours that are not colours left out */
void themes()
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-themes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	std::ofstream(dir / "Sea.chimeraTheme.json") << R"({"name": "Sea", "author": "A", "description": "waves\nand more", "dark": true,
		"colors": {"WindowBackground": "#102030", "WindowText": "#E0E0E0", "Selection": "#3060A0", "Bogus": "blue"}})";
	std::ofstream(dir / "Broken.chimeraTheme.json") << R"({"colors": {}})";
	std::ofstream(dir / "Light.chimeraTheme.json") << R"({"name": "Light", "colors": {"WindowBackground": "#000000"}})";
	std::ofstream(dir / "notes.json") << "{}";
	ManualDispatcher d;
	ThemesViewModel t(d);
	std::string error;
	CHECK(t.addBuiltIn(R"({"name": "Light", "dark": false, "colors": {"WindowBackground": "system:Control", "WindowText": "#000000"}})", error));
	CHECK(t.addBuiltIn(R"({"name": "Dark", "dark": true, "colors": {"WindowBackground": "#202020"}})", error));
	CHECK(!t.addBuiltIn(R"({"name": "", "colors": {}})", error) && error == "it has no name");
	t.folder.set(dir.string());
	/* the built-ins, then the folder's in name order; a file's theme by a
	 * built-in's name stays the built-in */
	CHECK(t.count.get() == 3);
	RowModel &rows = *t.rowLists()[0].model;
	std::string name, desc;
	fromValue(rows.cell(2, 0), name);
	fromValue(rows.cell(2, 2), desc);
	CHECK(name == "Sea" && desc == "waves and more");
	/* Chimera's line, and why a file would not load */
	CHECK(t.status.get() == "3 themes; files are read from " + dir.string() + ". 1 file would not load - why?" &&
		  t.failures.get() == "Broken.chimeraTheme.json: it has no name");
	/* Dark at first, as Chimera's default */
	CHECK(t.current.get() == "Dark" && t.dark.get() && t.colors.get().find("#202020") != std::string::npos);
	CHECK(t.run("choose", {std::string("Sea")}, error).has_value());
	CHECK(t.colors.get().find("#102030") != std::string::npos && t.colors.get().find("Bogus") == std::string::npos && t.dark.get());
	t.run("choose", {std::string("Light")}, error);
	CHECK(t.colors.get().find("system:Control") != std::string::npos && t.colors.get().find("#000000") != std::string::npos && !t.dark.get());
	CHECK(!t.run("choose", {std::string("Nope")}, error) && error == "no theme called Nope");
	/* the files after the built-ins by their themes' names, not their file names */
	std::ofstream(dir / "zz.chimeraTheme.json") << R"({"name": "Aqua", "colors": {"WindowBackground": "#00FFFF"}})";
	t.run("refresh", {}, error);
	CHECK(t.names.get() == "[\"Light\", \"Dark\", \"Aqua\", \"Sea\"]");
	/* a theme that is not on, for a preview, and what it says of itself */
	CHECK(t.run("colorsOf", {std::string("Sea")}, error) == Value(std::string("{\"WindowBackground\": \"#102030\", \"WindowText\": \"#E0E0E0\", \"Selection\": \"#3060A0\"}")));
	CHECK(t.run("about", {std::string("Sea")}, error) ==
		  Value(std::string("{\"name\": \"Sea\", \"about\": \"waves / and more\", \"author\": \"A\", \"origin\": \"Sea.chimeraTheme.json\", \"builtIn\": false, \"dark\": true}")));
	/* Write a Copy to Edit: a file of its own, never over one there, read back as a theme */
	std::string made;
	CHECK(fromValue(t.run("writeCopy", {std::string("Sea")}, error).value_or(Value()), made) && made == "Sea copy");
	CHECK(fs::exists(dir / "Sea copy.chimeraTheme.json") && t.status.get().rfind("Written to ", 0) == 0);
	CHECK(fromValue(t.run("writeCopy", {std::string("Sea")}, error).value_or(Value()), made) && made == "Sea copy 2");
	CHECK(t.run("colorsOf", {std::string("Sea copy 2")}, error) == t.run("colorsOf", {std::string("Sea")}, error));
	/* a built-in's, with the desktop's colours the window painted */
	CHECK(fromValue(t.run("writeCopy", {std::string("Light"), std::string(R"({"WindowBackground": "#F0F0F0", "WindowText": "#000000"})")}, error).value_or(Value()), made) &&
		  made == "Light copy" && t.run("colorsOf", {std::string("Light copy")}, error) == Value(std::string("{\"WindowBackground\": \"#F0F0F0\", \"WindowText\": \"#000000\"}")));
	removeTree(dir);
}

/* Values written as Chimera's RAM Watch writes them (the whole range was
 * compared once; these few keep it so) */
void watchValues()
{
	using watch::Display;
	using watch::format;
	CHECK(format(0x80, 1, Display::Unsigned) == "128" && format(0x80, 1, Display::Signed) == "-128");
	CHECK(format(0x0A, 1, Display::Hex) == "0A" && format(0xA5, 1, Display::Binary) == "1010 0101");
	CHECK(format(0xABCD, 2, Display::Binary) == "1010 1011 1100 1101" && format(0xFFFF, 2, Display::Signed) == "-1");
	CHECK(format(0x0008, 2, Display::Fixed12_4) == "0.5000" && format(0xFFF8, 2, Display::Fixed12_4) == "-0.5000");
	CHECK(format(0x0000, 2, Display::Fixed12_4) == "0.0000" && format(0x1234, 2, Display::Hex) == "1234");
	CHECK(format(0xFFFFF000, 4, Display::Fixed20_12) == "-1" && format(0x00000800, 4, Display::Fixed20_12) == "0.5");
	CHECK(format(1, 4, Display::Fixed16_16) == "0.000015" && format(0xFFFFFFFF, 4, Display::Fixed16_16) == "-0.000015");
	CHECK(format(0x00000080, 4, Display::Fixed16_16) == "0.001953");
	CHECK(format(0x3DCCCCCD, 4, Display::Float) == "0.1" && format(0x4CBEBC20, 4, Display::Float) == "100000000");
	CHECK(format(0x4E6E6B28, 4, Display::Float) == "1E+09" && format(0x3727C5AC, 4, Display::Float) == "1E-05");
	CHECK(format(0x38D1B717, 4, Display::Float) == "0.0001" && format(0x80000000, 4, Display::Float) == "-0");
	CHECK(format(0x7F800000, 4, Display::Float) == "Infinity" && format(0x7FC00000, 4, Display::Float) == "NaN");
	CHECK(format(0x7F7FFFFF, 4, Display::Float) == "3.4028235E+38" && format(0x00000001, 4, Display::Float) == "1E-45");
	CHECK(format(0x12345678, 4, Display::Hex) == "12345678" && format(0x80000000, 4, Display::Signed) == "-2147483648");
	CHECK(watch::addressDigits(0xFF) == 2 && watch::addressDigits(0x100) == 4 && watch::addressDigits(0x10000) == 6);
	CHECK(watch::fits(2, Display::Fixed12_4) && !watch::fits(1, Display::Float) && !watch::fits(4, Display::Fixed12_4));
	/* and typed back, as Chimera's value boxes read them */
	uint32_t v = 0;
	CHECK(watch::parse("FF", Display::Hex, v) && v == 0xFF && watch::parse(" 7fffffff ", Display::Hex, v) && v == 0x7FFFFFFF);
	CHECK(!watch::parse("0x10", Display::Hex, v) && !watch::parse("123456789", Display::Hex, v) && !watch::parse("-1", Display::Hex, v));
	CHECK(watch::parse("-1", Display::Signed, v) && v == 0xFFFFFFFF && !watch::parse("2147483648", Display::Signed, v));
	CHECK(watch::parse("4294967295", Display::Unsigned, v) && v == 0xFFFFFFFF && !watch::parse("4294967296", Display::Unsigned, v));
	CHECK(watch::parse("10100101", Display::Binary, v) && v == 0xA5 && !watch::parse("1010 0101", Display::Binary, v));
	CHECK(watch::parse("0.5", Display::Fixed12_4, v) && v == 8 && watch::parse("1.99", Display::Fixed12_4, v) && v == 31);
	CHECK(watch::parse("-0.5", Display::Fixed20_12, v) && v == 0xFFFFF800 && watch::parse("1", Display::Fixed16_16, v) && v == 0x10000);
	CHECK(watch::parse("0.1", Display::Float, v) && v == 0x3DCCCCCD && !watch::parse("1e40", Display::Float, v));
	CHECK(!watch::parse("", Display::Unsigned, v) && !watch::parse("abc", Display::Unsigned, v) && watch::parse(" 12 ", Display::Unsigned, v) && v == 12);
}

/* RAM Watch: values read after every frame, the previous value and the
 * changes counted three ways, and Chimera's .wch files both ways */
bool ramWatch(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-watch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	RamWatchViewModel watches(d, session);
	std::string error;
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get() && !watches.domains.get().empty(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	const std::string firstDomain = watches.domains.get().substr(0, watches.domains.get().find(','));
	CHECK(("," + watches.domains.get() + ",").find(",RAM,") != std::string::npos);
	/* a byte the game changes from one frame to the next */
	session.key("Down", true);
	int64_t frame = 0;
	auto step = [&] {
		session.run("frameAdvance", {}, error);
		frame++;
		CHECK(d.until([&] { return session.frame.get() == frame; }, 20));
	};
	step();
	std::vector<uint8_t> before = session.readDomain("RAM");
	step();
	std::vector<uint8_t> now = session.readDomain("RAM");
	const int64_t ramSize = static_cast<int64_t>(now.size());
	int64_t at = -1;
	for (int64_t i = 0; i + 4 <= ramSize && at < 0; i++)
		if (before[static_cast<size_t>(i)] != now[static_cast<size_t>(i)]) at = i;
	CHECK(at >= 0);
	if (at < 0) return false;
	const int64_t a2 = at & ~int64_t{1}, a4 = at & ~int64_t{3};
	CHECK(watches.run("add", {std::string("RAM"), at, int64_t{1}, int64_t{0}, false, std::string("changing byte")}, error).has_value());
	watches.run("add", {std::string("RAM"), a2, int64_t{2}, int64_t{2}, false, std::string("word, little end first")}, error);
	watches.run("add", {std::string("RAM"), a2, int64_t{2}, int64_t{2}, true, std::string("word, big end first")}, error);
	watches.run("add", {std::string("RAM"), a4, int64_t{4}, int64_t{3}, false, std::string("bits")}, error);
	watches.run("addSeparator", {std::string("below")}, error);
	watches.run("add", {std::string("RAM"), ramSize - 1, int64_t{2}, int64_t{0}, false, std::string("past the end")}, error);
	watches.run("add", {std::string("RAM"), a4, int64_t{4}, int64_t{7}, false, std::string("as a float")}, error);
	CHECK(!watches.run("add", {std::string("RAM"), at, int64_t{1}, int64_t{7}, false, std::string("")}, error));
	CHECK(!watches.run("add", {std::string("RAM"), at, int64_t{3}, int64_t{0}, false, std::string("")}, error));
	CHECK(watches.count.get() == 7 && watches.changed.get());
	auto text = [&](int64_t row, int32_t column) {
		std::string t;
		fromValue(watches.rowLists()[0].model->cell(row, column), t);
		return t;
	};
	auto number = [&](int64_t row, int32_t column) {
		int64_t n = -1;
		fromValue(watches.rowLists()[0].model->cell(row, column), n);
		return n;
	};
	auto shownAs = [&](const std::vector<uint8_t> &r) {
		const size_t i = static_cast<size_t>(at), w = static_cast<size_t>(a2), q = static_cast<size_t>(a4);
		const uint32_t little = r[w] | (r[w + 1] << 8), big = (r[w] << 8) | r[w + 1];
		const uint32_t dword = r[q] | (r[q + 1] << 8) | (r[q + 2] << 16) | (static_cast<uint32_t>(r[q + 3]) << 24);
		return text(0, 1) == std::to_string(r[i]) && text(1, 1) == watch::format(little, 2, watch::Display::Hex) &&
			   text(2, 1) == watch::format(big, 2, watch::Display::Hex) && text(3, 1) == watch::format(dword, 4, watch::Display::Binary) &&
			   text(6, 1) == watch::format(dword, 4, watch::Display::Float);
	};
	CHECK(d.until([&] { return shownAs(now); }, 10));
	CHECK(text(5, 1) == "-" && text(5, 4) == "-" && text(4, 6) == "below" && text(4, 1).empty());
	CHECK(text(0, 0) == watch::hex(at, watch::addressDigits(ramSize - 1)) && text(0, 5) == "RAM" && text(1, 8) == "Hex");
	CHECK(text(0, 2) == text(0, 1) && number(0, 3) == 0);
	/* the last frame's value as the previous one: every frame that moves it is a change */
	int64_t changes = 0;
	uint8_t value = now[static_cast<size_t>(at)], previous = value;
	for (int k = 0; k < 6; k++)
	{
		step();
		now = session.readDomain("RAM");
		previous = value;
		value = now[static_cast<size_t>(at)];
		if (value != previous) changes++;
		CHECK(d.until([&] { return shownAs(now) && text(0, 2) == std::to_string(previous) && number(0, 3) == changes; }, 10));
	}
	const int64_t diff = static_cast<int64_t>(value) - previous;
	CHECK(text(0, 4) == (diff > 0 ? "+" : "") + std::to_string(diff));
	/* the value before the last change */
	watches.set("previousType", int64_t{3}, error);
	uint8_t beforeChange = previous;
	for (int k = 0; k < 6; k++)
	{
		step();
		now = session.readDomain("RAM");
		const uint8_t read = now[static_cast<size_t>(at)];
		if (read != value)
		{
			beforeChange = value;
			value = read;
			changes++;
		}
		CHECK(d.until([&] { return shownAs(now) && text(0, 2) == std::to_string(beforeChange) && number(0, 3) == changes; }, 10));
	}
	/* the first value seen: nothing moves it, nothing is counted */
	watches.set("previousType", int64_t{0}, error);
	const std::string kept = text(0, 2);
	for (int k = 0; k < 3; k++)
	{
		step();
		now = session.readDomain("RAM");
		CHECK(d.until([&] { return shownAs(now) && text(0, 2) == kept && number(0, 3) == changes; }, 10));
	}
	watches.run("clearChangeCounts", {}, error);
	CHECK(number(0, 3) == 0);

	/* saved as Chimera saves it */
	watches.run("setOnScreen", {int64_t{0}, true}, error);
	watches.run("setOnScreen", {int64_t{6}, true}, error);
	CHECK(!watches.run("setOnScreen", {int64_t{4}, true}, error));
	/* shown over the picture, "note: value", in the list's order */
	CHECK(d.until([&] {
		return watches.onScreenText.get() == "changing byte: " + text(0, 1) + "\nas a float: " + text(6, 1);
	}, 10));
	watches.set("showOnScreen", false, error);
	CHECK(watches.onScreenText.get().empty());
	watches.set("showOnScreen", true, error);
	CHECK(watches.onScreenText.get().rfind("changing byte: ", 0) == 0);
	step();
	now = session.readDomain("RAM");
	CHECK(d.until([&] { return watches.onScreenText.get().rfind("changing byte: " + std::to_string(now[static_cast<size_t>(at)]) + "\n", 0) == 0; }, 10));
	const fs::path file = dir / "w.wch";
	CHECK(watches.run("save", {file.string()}, error).has_value() && !watches.changed.get() && watches.path.get() == file.string());
	const std::string saved = readFile(file);
#ifdef _WIN32
	const std::string nl = "\r\n";
#else
	const std::string nl = "\n";
#endif
	const std::string digits = watch::hex(at, watch::addressDigits(ramSize));
	CHECK(saved.rfind("SystemID Synth" + nl + digits + "\tb\tu\t0\tRAM\tchanging byte" + nl, 0) == 0);
	CHECK(saved.find(nl + "0\tS\t_\t1\t\tbelow" + nl) != std::string::npos);
	CHECK(saved.find(watch::hex(a2, watch::addressDigits(ramSize)) + "\tw\th\t1\tRAM\tword, big end first" + nl) != std::string::npos);
	CHECK(saved.size() > 2 * nl.size() && saved.substr(saved.size() - (nl.size() + 12 + nl.size())) == "OnScreen\t0,6" + nl + nl);
	/* and read back */
	watches.run("newList", {}, error);
	CHECK(watches.count.get() == 0 && watches.path.get().empty());
	CHECK(watches.run("open", {file.string()}, error).has_value());
	CHECK(watches.count.get() == 7 && !watches.changed.get());
	CHECK(text(0, 6) == "changing byte" && number(0, 7) == 1 && text(2, 8) == "Hex" && text(4, 6) == "below");
	bool on0 = false, on1 = true, on6 = false;
	fromValue(watches.rowLists()[0].model->cell(0, 9), on0);
	fromValue(watches.rowLists()[0].model->cell(1, 9), on1);
	fromValue(watches.rowLists()[0].model->cell(6, 9), on6);
	CHECK(on0 && !on1 && on6);
	CHECK(d.until([&] { return shownAs(now); }, 10));
	/* opened with another: appended, and the list has changed */
	CHECK(watches.run("open", {file.string(), true}, error).has_value() && watches.count.get() == 14 && watches.changed.get());
	/* another emulator's file (a count first, a place before each line, no
	 * domain), a domain this machine lacks, and a game property, which is
	 * kept as it was */
	{
		std::ofstream(dir / "other.wch", std::ios::binary) << "2\r\n00001\t0010\tw\th\t1\tfrom elsewhere\r\n00002\t0020\tb\tu\t0\tsecond\r\n";
		CHECK(watches.run("open", {(dir / "other.wch").string()}, error).has_value() && watches.count.get() == 2);
		CHECK(text(0, 0) == watch::hex(0x10, watch::addressDigits(ramSize - 1)) && text(0, 5) == firstDomain && text(0, 6) == "from elsewhere");
		CHECK(number(0, 7) == 2 && text(0, 8) == "Hex" && text(1, 6) == "second");
		std::ofstream(dir / "odd.wch", std::ios::binary)
			<< "SystemID Synth\n0010\tb\tu\t0\tNowhere\tlost domain\n0\tp\t_\t0\t\tplayer x\nOnScreen\t1\n";
		CHECK(watches.run("open", {(dir / "odd.wch").string()}, error).has_value() && watches.count.get() == 2);
		/* the game has no "player x": it reads as nothing, and is saved as it was */
		CHECK(text(0, 5) == firstDomain && text(1, 6) == "player x" && text(1, 8) == "Game property" && text(1, 1) == "-");
		CHECK(watches.run("save", {(dir / "odd-saved.wch").string()}, error).has_value());
		CHECK(readFile(dir / "odd-saved.wch").find(nl + "0\tp\t_\t0\t\tplayer x" + nl) != std::string::npos);
		CHECK(readFile(dir / "odd-saved.wch").find("OnScreen\t1" + nl) != std::string::npos);
	}
	CHECK(!watches.run("open", {(dir / "missing.wch").string()}, error) && error.find("cannot read") != std::string::npos);
	/* Chimera's RAM Watch works on the rows picked: split, edit, duplicate,
	 * poke, copy and paste, move, purge, sort; the warning for addresses past
	 * the end; the status line */
	{
		auto addr = [&](int64_t n) { return watch::hex(n, watch::addressDigits(ramSize - 1)); };
		auto picked = [&](int64_t row) { return watches.rowLists()[0].model->cell(row, 11) == Value(true); };
#ifdef _WIN32
		const std::string nl = "\r\n";
#else
		const std::string nl = "\n";
#endif
		watches.run("newList", {}, error);
		CHECK(watches.status.get().empty() && watches.countText.get() == "0 watches");
		watches.run("add", {std::string("RAM"), int64_t{0x40}, int64_t{4}, int64_t{2}, true, std::string("dword")}, error);
		watches.run("add", {std::string("RAM"), int64_t{0x50}, int64_t{1}, int64_t{0}, false, std::string("byte")}, error);
		watches.run("addSeparator", {std::string("sep")}, error);
		watches.run("add", {std::string("RAM"), ramSize + 4, int64_t{2}, int64_t{0}, false, std::string("past")}, error);
		CHECK(watches.countText.get() == "3 watches" && watches.invalidCount.get() == 1 && watches.rowLists()[0].model->cell(3, 12) == Value(true));
		/* a double word in hex split into two words, the lower address first when big end first */
		watches.run("select", {int64_t{0}, std::string("only")}, error);
		CHECK(watches.selectedCount.get() == 1 && watches.canSplit.get() && watches.canPoke.get() && picked(0) && !picked(1));
		CHECK(watches.run("splitSelected", {}, error) == Value(true));
		CHECK(text(0, 0) == addr(0x40) && text(1, 0) == addr(0x42) && number(0, 7) == 2 && number(1, 7) == 2 && text(1, 10) == "2hB");
		/* a byte in unsigned cannot be */
		watches.run("select", {int64_t{2}, std::string("only")}, error);
		CHECK(!watches.canSplit.get());
		/* edited: a word, unsigned, at another address, with its note */
		CHECK(watches.run("editSelected", {int64_t{2}, int64_t{0}, int64_t{0}, std::string("51"), std::string("word now")}, error).has_value());
		CHECK(text(2, 0) == addr(0x51) && number(2, 7) == 2 && text(2, 6) == "word now" && text(2, 10) == "2uL");
		/* duplicated, changed as the editor says, after the last row picked */
		watches.run("duplicateSelected", {int64_t{1}, int64_t{2}, int64_t{1}, std::string(""), std::string("copy")}, error);
		CHECK(watches.count.get() == 6 && text(3, 6) == "copy" && text(3, 10) == "1hB" && text(3, 0) == addr(0x51) && text(2, 6) == "word now");
		/* poked into memory, as the display reads it; text it cannot read refused */
		watches.run("select", {int64_t{3}, std::string("only")}, error);
		const std::string info = std::get<std::string>(*watches.run("pokeInfo", {}, error));
		CHECK(info.find("\"addresses\": \"" + addr(0x51) + "\"") != std::string::npos && info.find("\"size\": \"Byte\"") != std::string::npos &&
			info.find("\"display\": \"Hex\"") != std::string::npos && info.find("\"mixed\": false") != std::string::npos);
		CHECK(watches.run("pokeSelected", {std::string("A5")}, error) == Value(true) && session.readDomain("RAM")[0x51] == 0xA5);
		CHECK(!watches.run("pokeSelected", {std::string("XYZ")}, error) && error == "An error occurred when writing Value.");
		/* copied as .wch lines and pasted back at the end */
		watches.run("select", {int64_t{0}, std::string("only")}, error);
		watches.run("select", {int64_t{1}, std::string("range")}, error);
		const std::string copied = std::get<std::string>(*watches.run("copySelected", {}, error));
		CHECK(copied == addr(0x40) + "\tw\th\t1\tRAM\tdword" + nl + addr(0x42) + "\tw\th\t1\tRAM\tdword" + nl);
		CHECK(watches.run("paste", {copied}, error) == Value(int64_t{2}) && watches.count.get() == 8 && text(6, 0) == addr(0x40) && text(7, 0) == addr(0x42));
		/* moved to the top, the bottom, up a row, kept picked */
		watches.run("select", {int64_t{5}, std::string("only")}, error);
		watches.run("moveSelected", {std::string("top")}, error);
		CHECK(text(0, 6) == "past" && picked(0));
		watches.run("moveSelected", {std::string("bottom")}, error);
		CHECK(text(7, 6) == "past" && picked(7));
		watches.run("moveSelected", {std::string("up")}, error);
		CHECK(text(6, 6) == "past" && picked(6));
		/* the same domain, address and size twice: the later goes */
		CHECK(watches.run("purgeDuplicates", {}, error) == Value(true) && watches.count.get() == 6);
		/* the warning clicked: the ones past the end go */
		CHECK(watches.invalidCount.get() == 1);
		watches.run("removeInvalid", {}, error);
		CHECK(watches.count.get() == 5 && watches.invalidCount.get() == 0);
		/* sorted between separators by address (then size), then the other way round */
		watches.run("sort", {std::string("Address")}, error);
		CHECK(text(2, 6) == "copy" && text(3, 6) == "word now" && text(4, 6) == "sep");
		watches.run("sort", {std::string("Address")}, error);
		CHECK(text(0, 6) == "word now" && text(1, 6) == "copy" && text(3, 0) == addr(0x40));
		/* the editor starts from what is picked: one, or several that differ */
		watches.run("select", {int64_t{0}, std::string("only")}, error);
		const std::string one = std::get<std::string>(*watches.run("editInfo", {std::string("edit")}, error));
		CHECK(one.find("\"size\": 2") != std::string::npos && one.find("\"many\": false") != std::string::npos &&
			one.find("\"note\": \"word now\"") != std::string::npos && one.find("\"bigEndian\": 0") != std::string::npos);
		watches.run("selectAll", {}, error);
		const std::string many = std::get<std::string>(*watches.run("editInfo", {std::string("edit")}, error));
		CHECK(many.find("\"many\": true") != std::string::npos && many.find("\"bigEndian\": 2") != std::string::npos &&
			many.find("\"sizesDiffer\": true") != std::string::npos);
		/* a separator's text */
		watches.run("select", {int64_t{4}, std::string("only")}, error);
		watches.run("editSeparators", {std::string("renamed")}, error);
		CHECK(text(4, 6) == "renamed" && watches.rowLists()[0].model->cell(4, 13) == Value(true));
		/* the status line, as Chimera's: saved, then changed */
		const std::filesystem::path pickedFile = dir / "picked.wch";
		watches.run("save", {pickedFile.string()}, error);
		CHECK(watches.status.get() == "picked.wch saved." && watches.recentFiles.get().rfind(pickedFile.string(), 0) == 0);
		watches.run("insertSeparator", {}, error);
		CHECK(watches.status.get() == "picked.wch *" && text(4, 6).empty() && text(5, 6) == "renamed");
	}
	/* the game's own properties, by name: read by the engine as text, named
	 * values by their names, changes counted, saved as Chimera saves them */
	watches.run("newList", {}, error);
	watches.set("previousType", int64_t{2}, error);
	CHECK(watches.run("addProperty", {std::string("Steps")}, error).has_value());
	CHECK(watches.run("addProperty", {std::string("Status")}, error).has_value());
	CHECK(!watches.run("addProperty", {std::string("Nope")}, error) && error == "the game has no property Nope");
	auto stepsNow = [&] {
		const std::vector<uint8_t> r = session.readDomain("RAM");
		return std::to_string(r[4] | (r[5] << 8) | (r[6] << 16) | (static_cast<uint32_t>(r[7]) << 24));
	};
	CHECK(d.until([&] { return text(0, 1) == stepsNow() && text(1, 1) == "Playing"; }, 10));
	CHECK(text(0, 0) == watch::hex(4, watch::addressDigits(ramSize - 1)) && text(0, 5) == "RAM" && text(0, 8) == "Game property");
	/* a few frames each way: every frame Steps reads differently is a change */
	int64_t expectChanges = number(0, 3);
	std::string last = stepsNow();
	for (const char *key : {"Up", "Up", "Left", "Left", "Right", "Right", "Down", "Down"})
	{
		session.key(key, true);
		step();
		session.key(key, false);
		const std::string nowSteps = stepsNow();
		if (nowSteps != last) expectChanges++;
		last = nowSteps;
		CHECK(d.until([&] { return text(0, 1) == nowSteps && number(0, 3) == expectChanges; }, 10));
	}
	CHECK(expectChanges > 0);
	CHECK(!watches.run("setDisplay", {int64_t{0}, int64_t{2}}, error));
	watches.run("setOnScreen", {int64_t{1}, true}, error);
	CHECK(watches.onScreenText.get() == "Status: Playing");
	CHECK(watches.run("save", {(dir / "props.wch").string()}, error).has_value());
	CHECK(readFile(dir / "props.wch").find(nl + watch::hex(4, watch::addressDigits(ramSize)) + "\tp\tu\t0\tRAM\tSteps" + nl) != std::string::npos);
	watches.run("newList", {}, error);
	CHECK(watches.run("open", {(dir / "props.wch").string()}, error).has_value() && watches.count.get() == 2);
	CHECK(d.until([&] { return text(0, 1) == stepsNow() && text(1, 1) == "Playing"; }, 10));
	session.key("Down", false);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* The hex editor: the lines on screen read after every frame, shown by
 * size and byte order, the values that changed marked; a poke written into
 * the machine; bytes found */
bool hexEditor(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	HexEditorViewModel hex(d, session);
	/* its window open: read again after each frame */
	hex.watching.set(true);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get() && !hex.domains.get().empty(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	hex.set("domain", std::string("RAM"), error);
	std::vector<uint8_t> ram = session.readDomain("RAM");
	const int64_t size = static_cast<int64_t>(ram.size());
	CHECK(size >= 256 && hex.lineCount.get() == (size + 15) / 16 && hex.writable.get());
	hex.set("visibleLines", int64_t{8}, error);
	RowModel &lines = *hex.rowLists()[0].model;
	auto text = [&](int64_t row, int32_t column) {
		std::string t;
		fromValue(lines.cell(row, column), t);
		return t;
	};
	auto flag = [&](int64_t row, int32_t column) {
		bool b = false;
		fromValue(lines.cell(row, column), b);
		return b;
	};
	const int digits = watch::addressDigits(size - 1);
	auto byteText = [](uint8_t b) { return watch::format(b, 1, watch::Display::Hex); };
	auto showsRam = [&](int64_t top) {
		if (lines.rowCount() != 8) return false;
		for (int64_t r = 0; r < 8; r++)
		{
			const int64_t address = (top + r) * 16;
			if (text(r, 0) != watch::hex(address, digits)) return false;
			for (int k = 0; k < 16; k++)
				if (text(r, 1 + 2 * k) != byteText(ram[static_cast<size_t>(address + k)])) return false;
		}
		return true;
	};
	CHECK(d.until([&] { ram = session.readDomain("RAM"); return showsRam(0); }, 10));
	/* the text the bytes spell */
	{
		std::string spelled;
		for (int k = 0; k < 16; k++) spelled += ram[static_cast<size_t>(k)] >= 0x20 && ram[static_cast<size_t>(k)] < 0x7F ? static_cast<char>(ram[static_cast<size_t>(k)]) : '.';
		CHECK(text(0, 33) == spelled);
	}
	/* two bytes a value, each end first */
	hex.set("size", int64_t{2}, error);
	CHECK(d.until([&] { return text(0, 1) == watch::format(ram[0] | (ram[1] << 8), 2, watch::Display::Hex); }, 10));
	CHECK(text(0, 3) == watch::format(ram[2] | (ram[3] << 8), 2, watch::Display::Hex) && text(0, 17).empty());
	hex.set("bigEndian", true, error);
	CHECK(d.until([&] { return text(0, 1) == watch::format((ram[0] << 8) | ram[1], 2, watch::Display::Hex); }, 10));
	hex.set("bigEndian", false, error);
	hex.set("size", int64_t{1}, error);
	/* scrolled */
	hex.set("topLine", int64_t{3}, error);
	CHECK(d.until([&] { return showsRam(3); }, 10));
	/* a frame: the bytes it changed are marked, and no others */
	const std::vector<uint8_t> before = session.readDomain("RAM");
	session.key("Down", true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	const std::vector<uint8_t> atOne = session.readDomain("RAM");
	int64_t moved = -1;
	for (int64_t i = 0; i < size && moved < 0; i++)
		if (before[static_cast<size_t>(i)] != atOne[static_cast<size_t>(i)]) moved = i;
	CHECK(moved >= 0);
	if (moved < 0) return false;
	hex.run("goTo", {moved}, error);
	CHECK(hex.picked.get() == moved && moved / 16 >= hex.topLine.get() && moved / 16 < hex.topLine.get() + 8);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 2; }, 20));
	session.key("Down", false);
	const std::vector<uint8_t> atTwo = session.readDomain("RAM");
	ram = atTwo;
	CHECK(d.until([&] { return showsRam(hex.topLine.get()); }, 10));
	int64_t marked = 0;
	bool marksRight = true;
	for (int64_t r = 0; r < 8; r++)
		for (int k = 0; k < 16; k++)
		{
			const size_t a = static_cast<size_t>((hex.topLine.get() + r) * 16 + k);
			const bool differs = atOne[a] != atTwo[a];
			marksRight = marksRight && flag(r, 2 + 2 * k) == differs;
			marked += differs ? 1 : 0;
		}
	CHECK(marksRight && marked > 0);
	/* a poke: written into the machine, shown, and the states after this
	 * frame dropped */
	CHECK(hex.run("poke", {moved, std::string("AB")}, error).has_value());
	CHECK(d.until([&] { return session.readDomain("RAM")[static_cast<size_t>(moved)] == 0xAB; }, 10));
	ram = session.readDomain("RAM");
	CHECK(d.until([&] { return showsRam(hex.topLine.get()); }, 10));
	CHECK(session.greenEnd.get() <= session.frame.get());
	hex.set("size", int64_t{2}, error);
	hex.set("bigEndian", true, error);
	const int64_t at = (moved + 2) & ~int64_t{1};
	CHECK(hex.run("poke", {at, std::string("1234")}, error).has_value());
	CHECK(d.until([&] { ram = session.readDomain("RAM"); return ram[static_cast<size_t>(at)] == 0x12 && ram[static_cast<size_t>(at + 1)] == 0x34; }, 10));
	CHECK(!hex.run("poke", {at, std::string("12345")}, error) && !hex.run("poke", {at, std::string("zz")}, error));
	CHECK(!hex.run("poke", {size, std::string("00")}, error));
	hex.set("size", int64_t{1}, error);
	hex.set("bigEndian", false, error);
	/* found: the bytes poked, from the start */
	hex.set("picked", int64_t{-1}, error);
	CHECK(hex.run("find", {std::string("12 34")}, error).has_value());
	CHECK(d.until([&] { return hex.status.get().rfind("Found at", 0) == 0; }, 20));
	{
		/* the first place they are in the domain */
		int64_t expected = -1;
		for (int64_t i = 0; i + 1 < size && expected < 0; i++)
			if (ram[static_cast<size_t>(i)] == 0x12 && ram[static_cast<size_t>(i + 1)] == 0x34) expected = i;
		CHECK(hex.picked.get() == expected && hex.picked.get() / 16 >= hex.topLine.get() && hex.picked.get() / 16 < hex.topLine.get() + 8);
	}
	CHECK(!hex.run("find", {std::string("1")}, error) && !hex.run("find", {std::string("xy")}, error));
	/* as Chimera's: a click highlights an address, Shift every value to another, the title and the box say so */
	auto kind = [&](int64_t address) {
		int64_t k = -1;
		const int64_t row = address / 16 - hex.topLine.get();
		fromValue(lines.cell(row, 34 + static_cast<int32_t>(address % 16)), k);
		return k;
	};
	hex.run("goTo", {int64_t{0x20}}, error);
	hex.run("click", {int64_t{0x22}, std::string("only")}, error);
	hex.run("click", {int64_t{0x25}, std::string("range")}, error);
	CHECK(hex.picked.get() == 0x22 && hex.secondaryCount.get() == 3 && kind(0x22) == 1 && kind(0x24) == 2 && kind(0x26) == 0);
	CHECK(hex.title.get() == "Hex Editor - Editing Address 0x" + watch::hex(0x22, digits == 4 ? 4 : digits) + " (Selected 0x4)");
	CHECK(hex.boxTitle.get().find(" RAM  -  0x") != std::string::npos && hex.header.get().rfind("0  1  2", 0) == 0);
	/* Ctrl toggles one beside it; Escape lets them all go */
	hex.run("click", {int64_t{0x24}, std::string("toggle")}, error);
	CHECK(hex.secondaryCount.get() == 2 && kind(0x24) == 0);
	hex.run("key", {std::string("Escape")}, error);
	CHECK(hex.picked.get() == -1 && hex.secondaryCount.get() == 0 && hex.title.get() == "Hex Editor");
	/* typed: two digits write a byte and move on, as Chimera's */
	hex.run("click", {int64_t{0x30}, std::string("only")}, error);
	hex.run("type", {std::string("c")}, error);
	CHECK(hex.nibbles.get() == "C" && hex.boxTitle.get().find("Typing: (C)") != std::string::npos);
	hex.run("type", {std::string("5")}, error);
	CHECK(session.readDomain("RAM")[0x30] == 0xC5 && hex.picked.get() == 0x31 && hex.nibbles.get().empty());
	/* the keys move as Chimera's: Right a value, Down a line, Shift-Right takes the one left */
	hex.run("key", {std::string("Down")}, error);
	CHECK(hex.picked.get() == 0x41);
	hex.run("key", {std::string("Right"), true}, error);
	CHECK(hex.picked.get() == 0x42 && hex.secondaryCount.get() == 1 && kind(0x41) == 2);
	hex.run("key", {std::string("Home")}, error);
	CHECK(hex.picked.get() == 0 && hex.secondaryCount.get() == 0);
	/* increment and decrement */
	hex.run("click", {int64_t{0x30}, std::string("only")}, error);
	hex.run("increment", {}, error);
	CHECK(session.readDomain("RAM")[0x30] == 0xC6);
	hex.run("decrement", {}, error);
	hex.run("decrement", {}, error);
	CHECK(session.readDomain("RAM")[0x30] == 0xC4);
	/* copied as bytes run together, exported as values with a space, pasted from the address highlighted */
	hex.run("click", {int64_t{0x31}, std::string("range")}, error);
	CHECK(hex.run("copyText", {}, error) == Value(std::string("C4") + watch::format(session.readDomain("RAM")[0x31], 1, watch::Display::Hex)));
	CHECK(hex.run("exportText", {}, error) == Value(std::string("C4 ") + watch::format(session.readDomain("RAM")[0x31], 1, watch::Display::Hex)));
	hex.run("click", {int64_t{0x50}, std::string("only")}, error);
	hex.run("paste", {std::string("de ad-be:ef")}, error);
	ram = session.readDomain("RAM");
	CHECK(ram[0x50] == 0xDE && ram[0x51] == 0xAD && ram[0x52] == 0xBE && ram[0x53] == 0xEF);
	/* found again: "DE AD" from the start; before it there is none, so none is found, as Chimera's */
	hex.run("key", {std::string("Home")}, error);
	CHECK(hex.run("findNext", {std::string("DEAD")}, error) == Value(int64_t{0x50}) && hex.findString.get() == "DEAD" && hex.secondaryCount.get() == 1);
	CHECK(hex.run("findPrev", {}, error) == Value(int64_t{-1}) && hex.status.get() == "Not found");
	CHECK(hex.run("textBytes", {std::string("AZ")}, error) == Value(std::string("415A")));
	/* a .tbl file: its characters shown, and looked for */
	const std::filesystem::path dir = std::filesystem::temp_directory_path() /
		("chimera-test-hex-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(dir);
	std::ofstream(dir / "game.tbl", std::ios::binary) << "DE=X\nAD=Y\n";
	CHECK(hex.run("loadTable", {(dir / "game.tbl").string()}, error) == Value(int64_t{2}) && hex.recentTables.get() == (dir / "game.tbl").string());
	CHECK(hex.run("textBytes", {std::string("XY")}, error) == Value(std::string("DEAD")));
	hex.run("goTo", {int64_t{0x50}}, error);
	CHECK(d.until([&] { return text(0x50 / 16 - hex.topLine.get(), 50) == "X" && text(0x50 / 16 - hex.topLine.get(), 52) == "?"; }, 10));
	hex.run("closeTable", {}, error);
	CHECK(hex.tableFile.get().empty() && d.until([&] { return text(0x50 / 16 - hex.topLine.get(), 50) == "."; }, 10));
	/* saved as binary and as text; a file imported over the domain's start */
	CHECK(hex.run("saveBinary", {(dir / "ram.bin").string()}, error).has_value());
	CHECK(std::filesystem::file_size(dir / "ram.bin") == static_cast<uintmax_t>(size));
	CHECK(hex.run("saveText", {(dir / "ram.txt").string()}, error).has_value());
	{
		std::ifstream in(dir / "ram.txt", std::ios::binary);
		std::string first;
		std::getline(in, first);
		CHECK(first.rfind(watch::format(ram[0], 1, watch::Display::Hex) + " ", 0) == 0 && first.size() >= 48);
	}
	std::ofstream(dir / "in.bin", std::ios::binary) << std::string("\x11\x22\x33", 3);
	CHECK(hex.run("importBinary", {(dir / "in.bin").string()}, error) == Value(int64_t{3}));
	ram = session.readDomain("RAM");
	CHECK(ram[0] == 0x11 && ram[1] == 0x22 && ram[2] == 0x33);
	/* Custom Colors kept as JSON; Reset to Default */
	hex.run("setColor", {std::string("Highlight"), std::string("#112233")}, error);
	CHECK(hex.colors.get() == "{\"Highlight\": \"#112233\"}");
	CHECK(!hex.run("setColor", {std::string("Nope"), std::string("#000000")}, error));
	hex.run("resetColors", {}, error);
	CHECK(hex.colors.get() == "{}");
	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* The piano roll's editing: frames put in, taken out, cloned, cleared,
 * copied and pasted, cut off - each one step to undo - and markers that
 * move with the frames when bound to them */
bool pianoRollEditing(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
	/* frame f holds button f % 8 */
	for (int64_t f = 0; f < 10; f++) roll.run("paint", {f, f, f % 8, true}, error);
	auto masks = [&] {
		std::vector<Buttons> read(static_cast<size_t>(inputs.size()));
		inputs.read(0, read);
		std::vector<uint64_t> m;
		for (const Buttons &b : read) m.push_back(b.low());
		return m;
	};
	auto bit = [](int64_t b) { return uint64_t{1} << b; };
	const std::vector<uint64_t> start = masks();
	CHECK(start.size() == 10 && start[3] == bit(3) && start[9] == bit(1));
	/* the frames in view whose state the greenzone holds; those an edit
	 * took out of it, still known as run; a frame put in, never run */
	{
		using P = PianoRollViewModel;
		auto flags = [&](int64_t f) { return roll.frames().rowFlags(f); };
		roll.visibleFrames.set(12);
		roll.topFrame.set(0);
		CHECK(d.until([&] { return session.hasState(9); }, 20));
		CHECK((flags(5) & P::kGreen) && (flags(5) & P::kStated) && (flags(5) & P::kRan));
		/* the machine done with the edits before (each ran it back to frame
		 * 10): an edit it has not got to yet would be run past by them, and
		 * the states after it made again */
		session.onMachineWait([](ce_session *) {});
		session.autoRestore.set(false);
		roll.run("toggle", {int64_t{3}, int64_t{6}}, error);
		CHECK(d.until([&] { return session.frame.get() == 3 && !session.hasState(5); }, 20));
		CHECK(!(flags(5) & P::kGreen) && !(flags(5) & P::kStated) && (flags(5) & P::kRan));
		roll.run("insertFrames", {int64_t{4}, int64_t{1}}, error);
		CHECK(!(flags(4) & P::kRan) && (flags(6) & P::kRan));
		roll.run("undo", {}, error);
		roll.run("undo", {}, error);
		CHECK(masks() == start);
		session.autoRestore.set(true);
		session.run("seek", {int64_t{10}}, error);
		CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
		roll.visibleFrames.set(0);
	}
	/* lag frames hidden, as Chimera's TAStudio hides them: of a run, the
	 * first N; the rest of a longer run shown; with "hide was lag frames",
	 * the ones an edit took out of the greenzone too */
	{
		std::vector<bool> lags;
		for (int64_t f = 0; f < 10; f++) lags.push_back(inputs.lagAt(f));
		for (int64_t f = 0; f < 10; f++) inputs.setLag(f, f == 2 || f == 3 || f == 4 || f == 7);
		RowModel &rows = roll.frames();
		CHECK(rows.nextShown(1) == 2 && rows.nextShown(-1) == 0);
		roll.lagFramesToHide.set(2);
		CHECK(roll.frameHidden(2) && roll.frameHidden(3) && !roll.frameHidden(4) && roll.frameHidden(7) && !roll.frameHidden(8));
		CHECK(rows.nextShown(1) == 4 && rows.nextShown(4) == 5 && rows.nextShown(6) == 8 && rows.nextShown(-1) == 0);
		CHECK(roll.shownFrom(8, -2) == 5 && roll.shownFrom(0, 3) == 5);
		roll.lagFramesToHide.set(3);
		CHECK(rows.nextShown(1) == 5);
		/* out of the greenzone, a frame that was lag is shown unless asked */
		session.autoRestore.set(false);
		roll.run("toggle", {int64_t{6}, int64_t{6}}, error);
		CHECK(d.until([&] { return session.frame.get() == 6 && session.greenEnd.get() == 6; }, 20));
		inputs.setLag(7, true);
		CHECK(!roll.frameHidden(7));
		roll.hideWasLagFrames.set(true);
		CHECK(roll.frameHidden(7));
		roll.hideWasLagFrames.set(false);
		roll.lagFramesToHide.set(0);
		CHECK(!roll.frameHidden(2) && rows.nextShown(1) == 2);
		roll.run("undo", {}, error);
		CHECK(masks() == start);
		session.autoRestore.set(true);
		for (int64_t f = 0; f < 10; f++) inputs.setLag(f, lags[static_cast<size_t>(f)]);
		session.run("seek", {int64_t{10}}, error);
		CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
	}
	auto undoTo = [&](const std::vector<uint64_t> &want) {
		roll.run("undo", {}, error);
		return masks() == want;
	};
	/* put in */
	CHECK(roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error).has_value());
	std::vector<uint64_t> want = start;
	want.insert(want.begin() + 3, 2, 0);
	CHECK(masks() == want && roll.frameCount.get() == 12);
	CHECK(undoTo(start) && roll.frameCount.get() == 10);
	roll.run("redo", {}, error);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* taken out */
	CHECK(roll.run("deleteFrames", {int64_t{2}, int64_t{4}}, error).has_value());
	want = start;
	want.erase(want.begin() + 2, want.begin() + 5);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cloned after themselves */
	CHECK(roll.run("cloneFrames", {int64_t{1}, int64_t{2}}, error).has_value());
	want = start;
	want.insert(want.begin() + 3, {start[1], start[2]});
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cleared */
	CHECK(roll.run("clearFrames", {int64_t{5}, int64_t{6}}, error).has_value());
	want = start;
	want[5] = want[6] = 0;
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* copied, pasted over, pasted in */
	CHECK(roll.run("copyFrames", {int64_t{0}, int64_t{2}}, error).has_value() && roll.clipboardFrames.get() == 3);
	CHECK(roll.run("pasteFrames", {int64_t{8}}, error).has_value());
	want = start;
	want[8] = start[0];
	want[9] = start[1];
	want.push_back(start[2]);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	CHECK(roll.run("pasteInsertFrames", {int64_t{5}}, error).has_value());
	want = start;
	want.insert(want.begin() + 5, {start[0], start[1], start[2]});
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cut: kept to paste, and taken out */
	CHECK(roll.run("cutFrames", {int64_t{1}, int64_t{2}}, error).has_value() && roll.clipboardFrames.get() == 2);
	want = start;
	want.erase(want.begin() + 1, want.begin() + 3);
	CHECK(masks() == want);
	CHECK(undoTo(start));
	/* cut off */
	CHECK(roll.run("truncate", {int64_t{6}}, error).has_value() && roll.frameCount.get() == 6);
	CHECK(undoTo(start) && roll.frameCount.get() == 10);
	/* refused */
	CHECK(!roll.run("deleteFrames", {int64_t{8}, int64_t{12}}, error) && error == "the frames go from 0 to 9");
	CHECK(!roll.run("insertFrames", {int64_t{11}, int64_t{1}}, error));
	/* markers stay on their frames, unless bound to the frames */
	roll.run("addMarker", {int64_t{5}, std::string("five")}, error);
	roll.run("addMarker", {int64_t{8}, std::string("eight")}, error);
	auto markerAt = [&](int64_t f) { return roll.rowLists()[0].model->rowNote(f); };
	roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error);
	CHECK(markerAt(5) == "five" && markerAt(8) == "eight");
	roll.run("undo", {}, error);
	roll.set("bindMarkers", true, error);
	roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error);
	CHECK(markerAt(7) == "five" && markerAt(10) == "eight" && markerAt(5).empty() && roll.markerCount.get() == 2);
	roll.run("deleteFrames", {int64_t{6}, int64_t{7}}, error);
	CHECK(markerAt(8) == "eight" && roll.markerCount.get() == 1);
	roll.run("undo", {}, error);
	CHECK(markerAt(7) == "five" && markerAt(10) == "eight");
	roll.run("undo", {}, error);
	CHECK(markerAt(5) == "five" && markerAt(8) == "eight" && masks() == start);
	/* the machine follows: the edits before it were honoured */
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30));
	/* going back after an edit adds no frames, however soon the machine gets
	 * there: it follows a shorter movie to its end, and goes on to where the
	 * first edit found it once an undo gives the frames back */
	CHECK(roll.run("deleteFrames", {int64_t{2}, int64_t{4}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 7; }, 30) && masks().size() == 7);
	CHECK(undoTo(start));
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30) && masks() == start);
	CHECK(roll.run("truncate", {int64_t{6}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 6; }, 30) && masks().size() == 6);
	CHECK(undoTo(start));
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30) && masks() == start);
	/* and no seek cut short by an edit said it could not go somewhere */
	CHECK(session.status.get().find("Could not go") == std::string::npos);
	/* the undo history, step by step, by Chimera's names */
	{
		RowModel *history = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "history") history = r.model;
		CHECK(history != nullptr);
		if (history == nullptr) return false;
		auto stepName = [&](int64_t i) {
			std::string n;
			fromValue(history->cell(i, 0), n);
			return n;
		};
		auto done = [&](int64_t i) {
			bool b = false;
			fromValue(history->cell(i, 1), b);
			return b;
		};
		CHECK(roll.run("clearHistory", {}, error).has_value() && history->rowCount() == 0);
		roll.run("paint", {int64_t{2}, int64_t{2}, int64_t{4}, true}, error);
		roll.run("insertFrames", {int64_t{3}, int64_t{2}}, error);
		roll.run("deleteFrames", {int64_t{3}, int64_t{4}}, error);
		roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
		CHECK(history->rowCount() == 4);
		CHECK(stepName(0) == "Set P1 A(On): 2" && stepName(1) == "Insert 2 frames: 3" && stepName(2) == "Delete 2 frames from 3-4" &&
			stepName(3) == "Toggle P1 A: 2");
		CHECK(roll.run("undoTo", {int64_t{1}}, error).has_value() && done(1) && !done(2) && !done(3) && history->rowCount() == 4);
		CHECK(stepName(2) == "Delete 2 frames from 3-4" && stepName(3) == "Toggle P1 A: 2");
		CHECK(roll.run("redoTo", {int64_t{2}}, error).has_value() && done(2) && !done(3));
		CHECK(roll.run("undoTo", {int64_t{-1}}, error).has_value() && !done(0) && masks() == start);
		CHECK(roll.run("redoTo", {int64_t{3}}, error).has_value() && done(3));
		CHECK(roll.run("clearHistoryTo", {int64_t{1}}, error).has_value() && history->rowCount() == 2 && stepName(0) == "Delete 2 frames from 3-4");
		roll.maxUndoSteps.set(1);
		CHECK(history->rowCount() == 1 && stepName(0) == "Toggle P1 A: 2");
		roll.run("undo", {}, error);
		roll.maxUndoSteps.set(1000);
		/* what is left done stays, the base the tests below start from */
		roll.run("paint", {int64_t{2}, int64_t{2}, int64_t{4}, false}, error);
		CHECK(masks() == start);
	}
	/* editing the shown columns alone (TAStudio's "allow editing hidden
	 * columns" off): the hidden column's presses stay on their frames */
	{
		const uint64_t h = bit(2);
		CHECK(roll.run("hideColumn", {session.buttonNames()[2]}, error).has_value());
		roll.editHiddenColumns.set(false);
		CHECK(roll.run("insertFrames", {int64_t{1}, int64_t{2}}, error).has_value());
		std::vector<uint64_t> want(12, 0);
		for (size_t f = 0; f < 10; f++) want[f] |= start[f] & h;
		want[0] |= start[0] & ~h;
		for (size_t f = 1; f < 10; f++) want[f + 2] |= start[f] & ~h;
		CHECK(masks() == want && roll.frameCount.get() == 12);
		CHECK(undoTo(start) && roll.frameCount.get() == 10);
		CHECK(roll.run("deleteFrames", {int64_t{3}, int64_t{4}}, error).has_value());
		want.assign(10, 0);
		for (size_t f = 0; f < 10; f++) want[f] = (start[f] & h) | (f < 3 ? start[f] & ~h : f + 2 < 10 ? start[f + 2] & ~h : 0);
		CHECK(masks() == want && roll.frameCount.get() == 10);
		CHECK(undoTo(start));
		CHECK(roll.run("clearFrames", {int64_t{1}, int64_t{3}}, error).has_value());
		want = start;
		for (size_t f = 1; f <= 3; f++) want[f] &= h;
		CHECK(masks() == want);
		CHECK(undoTo(start));
		/* cut off: the shown columns cleared, the empty frames at the end
		 * taken off; a hidden press keeps its frame */
		CHECK(roll.run("truncate", {int64_t{5}}, error).has_value());
		CHECK(masks() == std::vector<uint64_t>(start.begin(), start.begin() + 5));
		CHECK(undoTo(start) && roll.frameCount.get() == 10);
		CHECK(roll.run("truncate", {int64_t{1}}, error).has_value());
		CHECK(masks() == (std::vector<uint64_t>{start[0], 0, h}));
		CHECK(undoTo(start));
		/* recorded: a hidden button held does not go into the movie */
		{
			session.recordingMode.set(true);
			size_t down = 0;
			while (down < session.buttonNames().size() && session.buttonNames()[down].find("Down") == std::string::npos) down++;
			CHECK(down < session.buttonNames().size() && down != 2);
			roll.run("hideColumn", {session.buttonNames()[down]}, error);
			session.run("seek", {int64_t{10}}, error);
			CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
			session.key("Down", true);
			session.run("frameAdvance", {}, error);
			CHECK(d.until([&] { return session.frame.get() == 11; }, 20));
			session.key("Down", false);
			CHECK(inputs.size() == 11 && masks()[10] == 0);
			roll.run("showColumn", {session.buttonNames()[down]}, error);
			session.key("Down", true);
			session.run("frameAdvance", {}, error);
			CHECK(d.until([&] { return session.frame.get() == 12; }, 20));
			session.key("Down", false);
			CHECK(inputs.size() == 12 && masks()[11] == bit(static_cast<int64_t>(down)));
			roll.run("truncate", {int64_t{10}}, error);
			CHECK(masks() == start);
		}
		/* hidden columns edited again when it is back on */
		roll.editHiddenColumns.set(true);
		CHECK(roll.run("clearFrames", {int64_t{2}, int64_t{2}}, error).has_value() && masks()[2] == 0);
		CHECK(undoTo(start));
		roll.run("showAllColumns", {}, error);
	}
	/* a column painted with its pattern: with Alt held, or as patternMode
	 * says; each control's own pattern */
	{
		RowModel *patterns = nullptr;
		for (const auto &r : roll.rowLists())
			if (r.name == "patterns") patterns = r.model;
		CHECK(patterns != nullptr && patterns->rowCount() == static_cast<int64_t>(session.buttonNames().size() + session.axes().size()));
		if (patterns == nullptr) return false;
		auto text = [&](int64_t row, int32_t col) {
			std::string t;
			fromValue(patterns->cell(row, col), t);
			return t;
		};
		const std::string five = session.buttonNames()[5];
		CHECK(text(5, 0) == five && text(5, 1) == "On x1, Off x1");
		/* the settings window's pattern editor: a button's values are off and on,
		 * an analog control's its range, an inserted run at its rest */
		{
			auto number = [&](int64_t row, int32_t col) {
				int64_t n = -12345;
				fromValue(patterns->cell(row, col), n);
				return n;
			};
			CHECK(number(5, 5) == 0 && number(5, 6) == 1 && number(5, 7) == 0);
			if (!session.axes().empty())
			{
				const int64_t a = static_cast<int64_t>(session.buttonNames().size());
				const SessionViewModel::Axis &ax = session.axes()[0];
				CHECK(number(a, 5) == ax.min && number(a, 6) == ax.max && number(a, 7) == ax.neutral);
			}
		}
		auto columnFive = [&](int64_t from, int64_t to) {
			std::string bits;
			for (int64_t f = from; f <= to; f++) bits += (masks()[static_cast<size_t>(f)] >> 5 & 1) ? '1' : '0';
			return bits;
		};
		auto history = [&] {
			for (const auto &r : roll.rowLists())
				if (r.name == "history")
				{
					std::string n;
					fromValue(r.model->cell(r.model->rowCount() - 1, 0), n);
					return n;
				}
			return std::string();
		};
		/* Chimera's default: on a frame, off the next */
		CHECK(roll.run("press", {int64_t{0}, int64_t{5}, true}, error).has_value());
		roll.run("drag", {int64_t{3}, int64_t{5}}, error);
		CHECK(columnFive(0, 3) == "1010" && history() == "Paint Bool " + five + " from frame 0");
		CHECK(undoTo(start));
		/* its own: on two, off one, back to the second */
		CHECK(roll.run("setPattern", {five, std::string("On x2, Off x1"), int64_t{1}, true}, error).has_value());
		CHECK(text(5, 1) == "On x2, Off x1");
		roll.run("press", {int64_t{0}, int64_t{5}, true}, error);
		roll.run("drag", {int64_t{6}, int64_t{5}}, error);
		CHECK(columnFive(0, 6) == "1101010");
		CHECK(undoTo(start));
		/* begun on a held cell, it lets go */
		roll.run("press", {int64_t{5}, int64_t{5}, true}, error);
		roll.run("drag", {int64_t{7}, int64_t{5}}, error);
		CHECK(columnFive(4, 8) == "00000");
		CHECK(undoTo(start));
		/* always: without Alt too */
		roll.patternMode.set("always");
		roll.run("press", {int64_t{0}, int64_t{5}}, error);
		roll.run("drag", {int64_t{2}, int64_t{5}}, error);
		CHECK(columnFive(0, 2) == "110");
		CHECK(undoTo(start));
		/* autofire: on the Auto-hold columns only */
		roll.patternMode.set("autofire");
		roll.run("press", {int64_t{0}, int64_t{5}}, error);
		roll.run("drag", {int64_t{1}, int64_t{5}}, error);
		roll.run("drag", {int64_t{2}, int64_t{5}}, error);
		CHECK(columnFive(0, 2) == "111");
		CHECK(undoTo(start));
		CHECK(roll.run("toggleAutoHold", {five}, error) == Value(true));
		roll.run("press", {int64_t{0}, int64_t{5}}, error);
		roll.run("drag", {int64_t{2}, int64_t{5}}, error);
		CHECK(columnFive(0, 2) == "110");
		CHECK(undoTo(start));
		CHECK(roll.run("toggleAutoHold", {five}, error) == Value(false));
		roll.patternMode.set("never");
		/* what is not a pattern */
		CHECK(!roll.run("setPattern", {five, std::string("Maybe x2")}, error) && error == "\"Maybe\" is not On or Off");
		CHECK(!roll.run("setPattern", {five, std::string("On"), int64_t{3}}, error));
		CHECK(!roll.run("setPattern", {std::string("Nothing"), std::string("On")}, error));
		CHECK(roll.run("setPattern", {five, std::string("On x1, Off x1"), int64_t{0}, true}, error).has_value());
	}
	/* macros: frames saved as Chimera's TAStudio saves them, and placed */
	{
		namespace fs = std::filesystem;
		const fs::path dir = fs::temp_directory_path() / ("chimera-test-macro-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
		fs::create_directories(dir);
		const std::string file = (dir / "three.chimeraMacro").string();
		CHECK(roll.run("saveMacro", {int64_t{2}, int64_t{4}, file}, error).has_value());
		std::vector<std::string> lines;
		{
			std::ifstream in(file);
			for (std::string line; std::getline(in, line);) lines.push_back(line);
		}
		const std::vector<std::string> movieLines = inputs.lines(2, 4);
		CHECK(lines.size() == 7 && lines[0].rfind(session.buttonNames()[0] + "|", 0) == 0 && lines[0].find('#') == std::string::npos &&
			lines[0].back() != '|' && lines[1] == session.controllerName() && lines[3] == "False,True");
		CHECK(lines.size() == 7 && lines[4] == movieLines[0] && lines[6] == movieLines[2]);
		CHECK(roll.run("placeMacro", {int64_t{6}, file}, error).has_value());
		want = start;
		for (size_t i = 0; i < 3; i++) want[6 + i] = start[2 + i];
		CHECK(masks() == want && roll.recentMacros.get() == file);
		CHECK(undoTo(start));
		/* one column, pressed over what is there, put in before the frames */
		const std::string one = (dir / "one.chimeraMacro").string();
		{
			std::ofstream out(one, std::ios::binary);
			out << session.buttonNames()[4] << "\r\nSome Pad\r\n2\r\nTrue,False\r\n|A|\r\n|.|\r\n";
		}
		CHECK(roll.run("placeMacro", {int64_t{3}, one}, error).has_value());
		want = start;
		want.insert(want.begin() + 3, {bit(4), 0});
		CHECK(masks() == want);
		CHECK(undoTo(start));
		CHECK(roll.recentMacros.get() == one + "\n" + file);
		/* a column the movie does not have */
		const std::string other = (dir / "other.chimeraMacro").string();
		{
			std::ofstream out(other, std::ios::binary);
			out << "P9 Laser\nLaser Gun\n10\nFalse,True\n|L|\n";
		}
		CHECK(!roll.run("placeMacro", {int64_t{0}, other}, error) &&
			error == "The selected macro is not compatible with the current core.\nMacro controller: Laser Gun\nMacro controller groups (\"players\" + 1) count: 10");
		CHECK(masks() == start);
		removeTree(dir);
	}
	/* cloned a number of times, as one edit */
	CHECK(roll.run("cloneFrames", {int64_t{1}, int64_t{2}, int64_t{3}}, error).has_value() && roll.frameCount.get() == 16);
	want = start;
	for (int i = 0; i < 3; i++) want.insert(want.begin() + 3, {start[1], start[2]});
	CHECK(masks() == want);
	CHECK(undoTo(start));
	CHECK(!roll.run("cloneFrames", {int64_t{1}, int64_t{2}, int64_t{0}}, error) && error == "a number of times from 1 to 100000");
	/* the frames copied picked again, where they were */
	CHECK(roll.run("copyFrames", {int64_t{4}, int64_t{6}}, error).has_value());
	roll.run("select", {int64_t{-1}, int64_t{-1}}, error);
	CHECK(roll.run("reselectClipboard", {}, error).has_value() && roll.selectFirst.get() == 4 && roll.selectLast.get() == 6);
	/* with the machine left at an edit, back to where the edits began
	 * (Chimera's green arrow) */
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30));
	session.autoRestore.set(false);
	CHECK(roll.run("clearFrames", {int64_t{2}, int64_t{2}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 2; }, 30));
	CHECK(session.run("seekToRestore", {}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 10; }, 30));
	CHECK(undoTo(start));
	session.autoRestore.set(true);
	/* the review's fixes (2026-10-26): a row past the movie, empty, as
	 * Chimera's TAStudio has */
	CHECK(roll.frames().rowCount() == inputs.size() + 1 && roll.frames().cell(inputs.size(), 0) == Value(false));
	/* a marker set is a step of the history: undone it goes, redone it is back */
	error.clear();
	roll.run("addMarker", {int64_t{5}, std::string("here")}, error);
	CHECK(error.empty());
	CHECK(roll.run("markerAt", {int64_t{5}}, error) == Value(std::string("here")));
	roll.run("undo", {}, error);
	CHECK(roll.run("markerAt", {int64_t{5}}, error) != Value(std::string("here")));
	roll.run("redo", {}, error);
	CHECK(roll.run("markerAt", {int64_t{5}}, error) == Value(std::string("here")));
	/* a run repeated more times than a number holds is refused, not the program's end */
	roll.run("setPattern", {session.buttonNames()[0], std::string("On x99999999999999999999"), int64_t{0}, false}, error);
	CHECK(error.find("too many") != std::string::npos);
	/* another project opened: the last one's steps and selection are not its */
	roll.run("select", {int64_t{1}, int64_t{2}}, error);
	CHECK(roll.canUndo.get() && roll.selectFirst.get() == 1);
	const int64_t opensBefore = session.opens.get();
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.opens.get() > opensBefore && session.isOpen.get(); }, 60));
	CHECK(!roll.canUndo.get() && !roll.canRedo.get() && roll.selectFirst.get() == -1);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Analog controls in the piano roll: a column each, its value shown; a
 * press picks a cell to edit, a drag paints its value, each one step */
bool pianoRollAxes(const char *slotsPackage, const char *rom)
{
	std::error_code ec;
	if (std::filesystem::file_size(slotsPackage, ec) == 0 || ec)
	{
		std::printf("SKIP analog controls: no slots package was made\n");
		return false;
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	ControlsViewModel controls(d, session);
	std::string error;
	openGame(session, slotsPackage, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	for (int i = 0; i < 7; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 7; }, 20));
	RowModel &rows = roll.frames();
	CHECK(session.axes().size() == 1 && session.axes()[0].min == -100 && session.axes()[0].max == 100);
	CHECK(rows.columnCount() == 9 && rows.columnName(8) == "P1 Dial" && rows.columnShortName(8) == "Dl" && rows.columnIsNumber(8) && !rows.columnIsNumber(7));
	auto dial = [&](int64_t f) {
		int64_t v = -999;
		fromValue(rows.cell(f, 8), v);
		return v;
	};
	CHECK(dial(2) == 0);
	/* picked */
	CHECK(roll.run("press", {int64_t{2}, int64_t{8}}, error).has_value());
	CHECK(roll.pickedFrame.get() == 2 && roll.pickedAxis.get() == 0 && roll.axisName.get() == "P1 Dial" && roll.axisMin.get() == -100 && roll.axisMax.get() == 100 && roll.axisValue.get() == 0);
	/* set, and refused out of range */
	CHECK(roll.run("setAxis", {int64_t{2}, int64_t{0}, int64_t{50}}, error).has_value() && dial(2) == 50 && roll.axisValue.get() == 50);
	CHECK(!roll.run("setAxis", {int64_t{2}, int64_t{0}, int64_t{101}}, error) && error == "P1 Dial goes from -100 to 100");
	/* Chimera's analog keys on the picked cell: by one, by ten, to the ends, kept in range */
	auto nudge = [&](const char *how) {
		roll.run("nudgeAxis", {std::string(how)}, error);
		return dial(2);
	};
	CHECK(nudge("up") == 51 && nudge("down10") == 41 && nudge("up10") == 51 && nudge("down") == 50);
	CHECK(nudge("max") == 100 && nudge("up") == 100 && nudge("min") == -100 && nudge("down10") == -100);
	CHECK(roll.run("setAxis", {int64_t{2}, int64_t{0}, int64_t{50}}, error).has_value() && dial(2) == 50);
	/* a drag paints the pressed cell's value, one step */
	roll.run("setAxis", {int64_t{3}, int64_t{0}, int64_t{77}}, error);
	roll.run("press", {int64_t{3}, int64_t{8}}, error);
	roll.run("drag", {int64_t{4}, int64_t{8}}, error);
	roll.run("drag", {int64_t{5}, int64_t{8}}, error);
	CHECK(dial(3) == 77 && dial(4) == 77 && dial(5) == 77 && dial(6) == 0);
	roll.run("undo", {}, error);
	CHECK(dial(3) == 77 && dial(4) == 0 && dial(5) == 0);
	roll.run("undo", {}, error);
	CHECK(dial(3) == 0 && dial(2) == 50);
	/* a button pressed on the same frame keeps the dial */
	roll.run("press", {int64_t{2}, int64_t{4}}, error);
	CHECK(dial(2) == 50 && (inputs.at(2).low() >> 4 & 1) != 0);
	/* frames taken out and put back keep their dials */
	roll.run("deleteFrames", {int64_t{1}, int64_t{1}}, error);
	CHECK(dial(1) == 50);
	roll.run("undo", {}, error);
	CHECK(dial(2) == 50 && dial(1) == 0);
	/* the movie carries the dial */
	CHECK(inputs.text().find("50") != std::string::npos);
	/* an analog column's Auto-hold holds the value given (chimera issue
	 * 298), or its most when none was given */
	{
		auto recorded = [&](int n) {
			const int64_t end = inputs.size();
			session.run("seek", {end}, error);
			d.until([&] { return session.frame.get() == end; }, 20);
			for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
			d.until([&] { return session.frame.get() == end + n; }, 20);
			std::string values;
			for (int64_t f = end; f < end + n; f++) values += std::to_string(dial(f)) + " ";
			return values;
		};
		/* what Chimera asks first: the value to hold, offering the frame's */
		std::string question;
		CHECK(fromValue(roll.run("autoHoldQuestion", {std::string("P1 Dial")}, error).value_or(Value()), question) &&
			question == "Hold P1 Dial at (from -100 to 100):" && roll.autoHoldMin.get() == -100 && roll.autoHoldMax.get() == 100);
		CHECK(fromValue(roll.run("autoHoldQuestion", {std::string("P1 A")}, error).value_or(Value()), question) && question.empty());
		CHECK(!roll.run("toggleAutoHold", {std::string("P1 Dial"), int64_t{120}}, error) && error == "P1 Dial goes from -100 to 100");
		CHECK(roll.run("toggleAutoHold", {std::string("P1 Dial"), int64_t{-40}}, error) == Value(true));
		CHECK(recorded(2) == "-40 -40 ");
		roll.patternKind.set("autofire");
		CHECK(recorded(4) == "-40 0 -40 0 ");
		roll.patternKind.set("hold");
		CHECK(roll.run("toggleAutoHold", {std::string("P1 Dial")}, error) == Value(false));
		CHECK(recorded(1) == "0 ");
		CHECK(roll.run("toggleAutoHold", {std::string("P1 Dial")}, error) == Value(true));
		CHECK(recorded(1) == "-40 ");
		roll.run("toggleAutoHold", {std::string("P1 Dial")}, error);
	}
	/* the dial bound in Controller Config, as Chimera does: a key for
	 * either end, the sensitivity, the pointer past a dead zone */
	{
		auto held = [&](const std::vector<std::string> &keys) {
			const int64_t end = inputs.size();
			session.run("seek", {end}, error);
			d.until([&] { return session.frame.get() == end; }, 20);
			/* an edit by hand ends recording, as Chimera's does */
			session.recordingMode.set(true);
			for (const std::string &k : keys) session.key(k, true);
			session.run("frameAdvance", {}, error);
			d.until([&] { return session.frame.get() == end + 1; }, 20);
			for (const std::string &k : keys) session.key(k, false);
			return dial(end);
		};
		RowModel &analog = *controls.rowLists()[2].model;
		CHECK(analog.rowCount() == 1 && analog.columnName(0) == "axis" && controls.axisGroups.get() == "[\"Player 1\"]");
		CHECK(controls.run("press", {std::string("positive"), int64_t{0}, std::string("L")}, error).has_value());
		controls.run("press", {std::string("negative"), int64_t{0}, std::string("H")}, error);
		controls.run("setAnalog", {int64_t{0}, std::string("mult"), 0.5}, error);
		std::string positive;
		double mult = 0;
		fromValue(analog.cell(0, 5), positive);
		fromValue(analog.cell(0, 3), mult);
		CHECK(positive == "L" && mult == 0.5 && session.analogBinds()[0].positive.empty());
		controls.run("save", {}, error);
		CHECK(session.analogBinds()[0].positive == "L" && controls.savedAnalog.get().find("\"P1 Dial\"") != std::string::npos);
		/* half the way to an end; both ends at once are no push, and the dial is left as the movie has it */
		CHECK(held({"L"}) == 50 && held({"H"}) == -50 && held({"L", "H"}) == 0 && held({}) == 0);
		/* the pointer, half way to the right, past a dead zone of a tenth: (0.5 - 0.1) / 0.9 of the way */
		controls.run("setAnalog", {int64_t{0}, std::string("value"), std::string("WMouse X")}, error);
		controls.run("setAnalog", {int64_t{0}, std::string("mult"), 1.0}, error);
		controls.run("setAnalog", {int64_t{0}, std::string("deadzone"), 0.1}, error);
		controls.run("save", {}, error);
		session.run("pointer", {int64_t{5000}, int64_t{0}}, error);
		CHECK(held({}) == 44);
		/* inside the dead zone it is left alone */
		session.run("pointer", {int64_t{800}, int64_t{0}}, error);
		CHECK(held({}) == 0);
		/* Flip Axis: the other way */
		controls.run("flip", {int64_t{0}}, error);
		controls.run("save", {}, error);
		session.run("pointer", {int64_t{5000}, int64_t{0}}, error);
		CHECK(held({}) == -44);
		/* kept in the control's range */
		controls.run("setAnalog", {int64_t{0}, std::string("mult"), -2.0}, error);
		controls.run("save", {}, error);
		session.run("pointer", {int64_t{10000}, int64_t{0}}, error);
		CHECK(held({}) == -100);
		/* bound to the pointer: Capture Mouse has something to capture for */
		CHECK(session.mouseBound.get());
		/* the pointer's axis caught again: how far it moves (RMouse), then where it is again, as Chimera's */
		CHECK(controls.run("caught", {int64_t{0}, std::string("WMouse X")}, error) == Value(std::string("RMouse X")));
		CHECK(controls.run("caught", {int64_t{0}, std::string("WMouse X")}, error) == Value(std::string("WMouse X")));
		CHECK(controls.run("caught", {int64_t{0}, std::string("WMouse X")}, error) == Value(std::string("RMouse X")));
		controls.run("setAnalog", {int64_t{0}, std::string("mult"), 1.0}, error);
		controls.run("setAnalog", {int64_t{0}, std::string("deadzone"), 0.0}, error);
		controls.run("save", {}, error);
		/* how far it moved since the last frame, read once, as Chimera's: 120
		 * pixels push it all the way at 100% (RelativeMouseSensitivity), and
		 * no further; the next frame has moved nothing */
		auto moved = [&](int64_t first, int64_t second) {
			const int64_t end = inputs.size();
			session.run("seek", {end}, error);
			d.until([&] { return session.frame.get() == end; }, 20);
			session.recordingMode.set(true);
			session.run("pointerMoved", {first, int64_t{0}}, error);
			session.run("pointerMoved", {second, int64_t{0}}, error);
			session.run("frameAdvance", {}, error);
			d.until([&] { return session.frame.get() == end + 1; }, 20);
			return dial(end);
		};
		CHECK(moved(36, 24) == 50);
		CHECK(moved(3000, 2000) == 100);
		CHECK(moved(-30, 0) == -25);
		/* the sensitivity: Controller Config's copy, kept by Save */
		CHECK(controls.run("edit", {}, error).has_value() && controls.mouseSensitivityCopy.get() == 100);
		controls.mouseSensitivityCopy.set(200);
		CHECK(controls.mouseSensitivity.get() == 100 && moved(30, 0) == 25);
		controls.run("save", {}, error);
		CHECK(controls.mouseSensitivity.get() == 200 && moved(30, 0) == 50);
		controls.mouseSensitivity.set(100);
		CHECK(held({}) == 0);
		controls.run("caught", {int64_t{0}, std::string("WMouse X")}, error);
		controls.run("setAnalog", {int64_t{0}, std::string("mult"), -2.0}, error);
		controls.run("setAnalog", {int64_t{0}, std::string("deadzone"), 0.1}, error);
		controls.run("save", {}, error);
		CHECK(!controls.run("setAnalog", {int64_t{0}, std::string("speed"), 1.0}, error) && error == "no field speed: value, mult or deadzone");
		CHECK(!controls.run("unbind", {int64_t{1}}, error) && error == "no analog control 1");
		/* Unbind!: the pointer no longer moves it, the keys still do */
		controls.run("unbind", {int64_t{0}}, error);
		controls.run("save", {}, error);
		CHECK(held({}) == 0 && held({"H"}) == 100);
		/* the package's again: nothing remembered */
		controls.run("loadDefaults", {}, error);
		controls.run("save", {}, error);
		CHECK(controls.savedAnalog.get() == "{}" && session.analogBinds()[0].positive.empty());
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Controls rebound and remembered per core; hotkeys run, rebound and let
 * go; frame advance and rewind held; branch slots */
bool controlsAndHotkeys(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	ControlsViewModel controls(d, session);
	HotkeysViewModel hotkeys(d);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get() && controls.core.get() == "Synth"; }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	RowModel &buttons = *controls.rowLists()[0].model;
	CHECK(buttons.columnName(3) == "keys" && buttons.columnName(4) == "packageKeys" && buttons.columnName(5) == "changed");
	auto text = [&](int64_t row, int32_t column) {
		std::string t;
		fromValue(buttons.cell(row, column), t);
		return t;
	};
	int64_t down = -1, a = -1;
	for (int64_t r = 0; r < buttons.rowCount(); r++)
	{
		if (text(r, 0) == "P1 Down") down = r;
		if (text(r, 0) == "P1 A") a = r;
	}
	CHECK(down >= 0 && a >= 0);
	if (down < 0 || a < 0) return false;
	/* Chimera's tabs and title: one player, so one tab */
	CHECK(text(down, 2) == "Player 1" && controls.buttonGroups.get() == "[\"Player 1\"]");
	CHECK(controls.groupOf("P1 Start") == "Player 1" && controls.groupOf("Power") == "Console");
	CHECK(controls.title.get() == session.controllerName() + " Configuration");
	const std::string packageKeys = text(down, 4);
	CHECK(text(down, 3) == packageKeys && packageKeys.find("Down") != std::string::npos);
	int64_t frame = 0;
	auto step = [&] {
		session.run("frameAdvance", {}, error);
		frame++;
		CHECK(d.until([&] { return session.frame.get() == frame; }, 20));
	};
	/* rebound on the window's copy: the machine has the old keys until Save */
	CHECK(controls.run("bind", {std::string("normal"), down, std::string("K")}, error).has_value());
	bool changed = false;
	fromValue(buttons.cell(down, 5), changed);
	CHECK(controls.editing.get() && text(down, 3) == "K" && changed && session.bindingTexts()[static_cast<size_t>(down)] == packageKeys);
	CHECK(controls.run("save", {}, error).has_value() && !controls.editing.get());
	/* K holds Down, and Down holds nothing */
	session.key("K", true);
	step();
	session.key("K", false);
	session.key("Down", true);
	step();
	session.key("Down", false);
	CHECK(inputs.at(0).low() == (uint64_t{1} << down) && inputs.at(1).low() == 0);
	/* what a key holds now, for the right click and the double click on the picture */
	CHECK(controls.run("hasBinding", {std::string("K")}, error) == Value(true) &&
		  controls.run("hasBinding", {std::string("WMouse R")}, error) == Value(false));
	{
		json::Json all;
		CHECK(json::parse(controls.saved.get(), all, error) && all.find("Synth") != nullptr &&
			  all.find("Synth")->find("P1 Down") != nullptr && all.find("Synth")->find("P1 Down")->text == "K");
	}
	/* remembered when the machine starts again */
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() == 2; }, 60));
	session.recordingMode.set(true);
	CHECK(session.bindingTexts()[static_cast<size_t>(down)] == "K");
	frame = session.frame.get();
	/* keys pressed in a box, as Chimera's: with Auto Tab one replaces the keys and the next box takes the next */
	auto press = [&](const char *key) {
		return controls.run("press", {std::string("normal"), down, std::string(key)}, error).value_or(Value());
	};
	CHECK(controls.autoTab.get() && press("J") == Value(true) && text(down, 3) == "J");
	/* without it, one more each, never twice, and the box keeps the focus */
	controls.autoTab.set(false);
	CHECK(press("K") == Value(false) && text(down, 3) == "J,K");
	press("K");
	CHECK(text(down, 3) == "J,K");
	/* never bound by a key: Alt+F4, and what only the arrow's menu gives */
	press("Alt+F4");
	press("WMouse L");
	CHECK(text(down, 3) == "J,K");
	CHECK(controls.run("special", {std::string("normal"), down, std::string("WMouse L")}, error).has_value() && text(down, 3) == "J,K,WMouse L");
	CHECK(!controls.run("special", {std::string("normal"), down, std::string("Q")}, error) && error == "Q is not a special binding");
	/* Escape empties the box */
	press("Escape");
	CHECK(text(down, 3).empty());
	controls.autoTab.set(true);
	/* Cancel lets the copy go */
	controls.run("cancel", {}, error);
	CHECK(!controls.editing.get() && text(down, 3) == "K" && session.bindingTexts()[static_cast<size_t>(down)] == "K");
	/* Load Defaults, then Save: the package's keys, nothing remembered */
	controls.run("loadDefaults", {}, error);
	CHECK(text(down, 3) == packageKeys && session.bindingTexts()[static_cast<size_t>(down)] == "K");
	controls.run("save", {}, error);
	CHECK(session.bindingTexts()[static_cast<size_t>(down)] == packageKeys && controls.saved.get() == "{}");
	/* Clear, then Save: no keys, remembered as none */
	controls.run("clear", {}, error);
	controls.run("save", {}, error);
	CHECK(text(down, 3).empty() && session.bindingTexts()[static_cast<size_t>(down)].empty() &&
		  controls.saved.get().find("\"P1 Down\": \"\"") != std::string::npos);
	controls.run("loadDefaults", {}, error);
	controls.run("save", {}, error);
	CHECK(controls.saved.get() == "{}");
	CHECK(!controls.run("bind", {std::string("normal"), int64_t{99}, std::string("K")}, error) && error == "no button 99");
	CHECK(!controls.run("bind", {std::string("analog"), down, std::string("K")}, error) &&
		  error == "no table analog: normal, autofire, positive or negative");
	/* an autofire key: its button pressed on the frame it goes down and every other frame after (on 1, off 1) */
	{
		RowModel &autofire = *controls.rowLists()[1].model;
		CHECK(controls.run("bind", {std::string("autofire"), a, std::string("Q")}, error).has_value());
		std::string q;
		fromValue(autofire.cell(a, 3), q);
		CHECK(q == "Q");
		controls.run("save", {}, error);
		CHECK(controls.savedAutofire.get().find("\"P1 A\": \"Q\"") != std::string::npos && session.autofireTexts()[static_cast<size_t>(a)] == "Q");
		const int64_t first = frame;
		session.key("Q", true);
		for (int i = 0; i < 5; i++) step();
		session.key("Q", false);
		step();
		std::string pattern;
		for (int64_t f = first; f < first + 6; f++) pattern += (inputs.at(f).low() >> a & 1) != 0 ? "1" : "0";
		CHECK(pattern == "101010");
		controls.run("loadDefaults", {}, error);
		controls.run("save", {}, error);
		CHECK(controls.savedAutofire.get() == "{}");
	}
	/* hotkeys: run, repeat ignored, let go, rebound, back */
	int downs = 0, ups = 0;
	hotkeys.add("General", "Frame Advance", "F", [&](bool isDown) { (isDown ? downs : ups)++; });
	hotkeys.add("General", "Rewind", "Shift+R", [&](bool) {});
	CHECK(hotkeys.key("F", true) && downs == 1);
	CHECK(!hotkeys.key("F", true) && downs == 1);
	CHECK(hotkeys.key("F", false) && ups == 1);
	CHECK(!hotkeys.key("R", true) && !hotkeys.key("Shift+F", true));
	/* every action's keys, for the menus to show */
	CHECK(hotkeys.bound.get() == "{\"Frame Advance\": \"F\", \"Rewind\": \"Shift+R\"}");
	/* Configure Hotkeys works on a copy: the keys change when it is saved */
	CHECK(hotkeys.run("bind", {std::string("Frame Advance"), std::string("G")}, error).has_value());
	CHECK(hotkeys.editing.get() && hotkeys.bound.get() == "{\"Frame Advance\": \"F\", \"Rewind\": \"Shift+R\"}");
	CHECK(hotkeys.run("save", {}, error).has_value() && !hotkeys.editing.get());
	CHECK(hotkeys.bound.get() == "{\"Frame Advance\": \"G\", \"Rewind\": \"Shift+R\"}");
	/* the window opened again starts from the keys as they are, not Chimera's */
	{
		hotkeys.run("edit", {}, error);
		std::string k;
		fromValue(hotkeys.rowLists()[0].model->cell(0, 2), k);
		CHECK(k == "G");
		hotkeys.run("cancel", {}, error);
	}
	CHECK(!hotkeys.key("F", true) && hotkeys.key("G", true) && downs == 2);
	hotkeys.key("G", false);
	CHECK(hotkeys.saved.get().find("\"G\"") != std::string::npos);
	/* a key let go after its action was rebound still lets the action go */
	hotkeys.key("G", true);
	hotkeys.run("defaults", {std::string("General")}, error);
	hotkeys.run("save", {}, error);
	CHECK(hotkeys.key("G", false) && ups == 3);
	CHECK(hotkeys.saved.get() == "{}" && hotkeys.key("F", true));
	hotkeys.key("F", false);
	CHECK(!hotkeys.run("bind", {std::string("Fly"), std::string("Z")}, error) && error == "no action Fly");
	/* listed in Chimera's order, whatever the order they were added in; the tabs as Chimera's */
	hotkeys.add("TAStudio", "Add Branch", "Alt+Insert", [](bool) {});
	hotkeys.add("General", "Pause", "Pause", [](bool) {});
	{
		RowModel &list = *hotkeys.rowLists()[0].model;
		std::string names;
		for (int64_t r = 0; r < list.rowCount(); r++)
		{
			std::string n;
			fromValue(list.cell(r, 1), n);
			names += n + "|";
		}
		CHECK(names == "Frame Advance|Rewind|Pause|Add Branch|" && hotkeys.groups.get() == "[\"General\", \"TAStudio\"]");
		auto keysOf = [&](int64_t r) {
			std::string k;
			fromValue(list.cell(r, 2), k);
			return k;
		};
		/* keys pressed in a box: with Auto Tab one replaces the keys and the next box takes the next */
		CHECK(hotkeys.run("press", {std::string("Pause"), std::string("Ctrl+K")}, error) == Value(true) && keysOf(2) == "Ctrl+K");
		hotkeys.autoTab.set(false);
		CHECK(hotkeys.run("press", {std::string("Pause"), std::string("J")}, error) == Value(false) && keysOf(2) == "Ctrl+K,J");
		hotkeys.run("press", {std::string("Pause"), std::string("Escape")}, error);
		CHECK(keysOf(2).empty());
		hotkeys.autoTab.set(true);
		/* a tab cleared, then the copy let go */
		hotkeys.run("clear", {std::string("General")}, error);
		CHECK(keysOf(0).empty() && keysOf(1).empty() && keysOf(3) == "Alt+Insert");
		hotkeys.run("cancel", {}, error);
		CHECK(keysOf(0) == "F" && keysOf(2) == "Pause" && hotkeys.saved.get() == "{}");
		/* Find: a name in any case, its tab; and what is typed so far suggests names */
		CHECK(hotkeys.run("find", {std::string("add BRANCH ")}, error) == Value(std::string("{\"action\": \"Add Branch\", \"group\": \"TAStudio\"}")));
		CHECK(hotkeys.run("find", {std::string("Fly")}, error) == Value(std::string()));
		CHECK(hotkeys.run("suggest", {std::string("r")}, error) == Value(std::string("[{\"action\": \"Rewind\", \"label\": \"Rewind\"}]")));
	}
	const int before = downs;
	hotkeys.run("run", {std::string("Frame Advance")}, error);
	CHECK(downs == before + 1);

	/* frame advance held: a frame, then after half a second it runs on until let go */
	const int64_t start = session.frame.get();
	session.holdFrameAdvance(true);
	CHECK(d.until([&] { return session.frame.get() == start + 1; }, 10));
	d.until([] { return false; }, 0.25);
	CHECK(session.frame.get() == start + 1 && !session.running.get());
	CHECK(d.until([&] { return session.running.get() && session.frame.get() > start + 3; }, 5));
	session.holdFrameAdvance(false);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	d.until([] { return false; }, 0.2);
	const int64_t stopped = session.frame.get();
	d.until([] { return false; }, 0.3);
	CHECK(session.frame.get() == stopped);
	/* rewind held: a frame back at once, then on back */
	session.holdRewind(true);
	CHECK(d.until([&] { return session.frame.get() == stopped - 1; }, 10));
	CHECK(d.until([&] { return session.frame.get() < stopped - 3; }, 5));
	session.holdRewind(false);
	d.until([] { return false; }, 0.2);
	const int64_t back = session.frame.get();
	d.until([] { return false; }, 0.3);
	CHECK(session.frame.get() == back && back >= 0);
	/* fast forward: faster than the machine's own rate while held */
	session.run("play", {}, error);
	session.holdFastForward(true);
	const int64_t fastFrom = session.frame.get();
	d.until([] { return false; }, 0.5);
	session.holdFastForward(false);
	session.run("pause", {}, error);
	CHECK(session.frame.get() - fastFrom > 45);

	/* branch slots: saved into, over, and a new one past the last */
	CHECK(branches.run("saveSlot", {int64_t{0}}, error).has_value() && branches.count.get() == 1);
	CHECK(d.until([&] { return branches.made.get() == 1; }, 20));
	/* its picture, shown at no side longer than the setting, never grown */
	{
		Picture::Frame shown;
		CHECK(branches.run("preview", {int64_t{0}}, error) == Value(true) && branches.previewing.get() == 0);
		CHECK(branches.preview.takeIfNewer(0, shown) && shown.width > 0);
		const double h = shown.height, w = shown.height * shown.aspect();
		const int64_t most = std::max(branches.previewWidth.get(), branches.previewHeight.get());
		CHECK(most == std::min<int64_t>(320, static_cast<int64_t>(std::max(w, h) + 0.5)));
		branches.previewSide.set(100);
		branches.run("preview", {int64_t{0}}, error);
		CHECK(std::max(branches.previewWidth.get(), branches.previewHeight.get()) == 100);
		CHECK(std::abs(static_cast<double>(branches.previewWidth.get()) / branches.previewHeight.get() - w / h) < 0.02);
		branches.previewSide.set(10);
		CHECK(branches.previewSide.get() == 64);
		branches.previewSide.set(320);
		CHECK(branches.run("preview", {int64_t{-1}}, error) == Value(false) && branches.previewing.get() == -1);
	}
	session.run("frameAdvance", {}, error);
	const int64_t later = session.frame.get() + 1;
	CHECK(d.until([&] { return session.frame.get() == later; }, 20));
	CHECK(branches.run("saveSlot", {int64_t{0}}, error).has_value() && branches.count.get() == 1);
	CHECK(d.until([&] { return branches.made.get() == 2; }, 20));
	int64_t slotFrame = -1;
	std::string slotName;
	fromValue(branches.rowLists()[0].model->cell(0, 1), slotFrame);
	fromValue(branches.rowLists()[0].model->cell(0, 0), slotName);
	CHECK(slotFrame == later && slotName == "Branch 1");
	CHECK(branches.run("saveSlot", {int64_t{5}}, error).has_value() && branches.count.get() == 2);
	CHECK(d.until([&] { return branches.made.get() == 3; }, 20));
	/* Chimera's branch undo: an update taken back puts the old one back */
	auto branchFrame = [&](int64_t i) {
		int64_t f = -1;
		fromValue(branches.rowLists()[0].model->cell(i, 1), f);
		return f;
	};
	auto branchName = [&](int64_t i) {
		std::string n;
		fromValue(branches.rowLists()[0].model->cell(i, 0), n);
		return n;
	};
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == later + 1; }, 20));
	CHECK(branches.run("saveSlot", {int64_t{0}}, error).has_value() && branches.undoText.get() == "Undo Branch Update");
	CHECK(d.until([&] { return branches.made.get() == 4 && branchFrame(0) == later + 1; }, 20));
	CHECK(branches.run("undo", {}, error).has_value() && branchFrame(0) == later && branches.status.get() == "Branch Update canceled");
	CHECK(!branches.run("undo", {}, error) && error == "nothing to undo");
	/* a text edit, and a removal, taken back */
	CHECK(branches.run("rename", {int64_t{0}, std::string("renamed")}, error).has_value() && branchName(0) == "renamed");
	CHECK(branches.run("undo", {}, error).has_value() && branchName(0) == "Branch 1");
	CHECK(branches.run("remove", {int64_t{1}}, error).has_value() && branches.count.get() == 1);
	CHECK(branches.run("undo", {}, error).has_value() && branches.count.get() == 2 && branchName(1) == "Branch 2");
	/* Jump To: the machine on the branch's frame, the movie left as it is */
	const int64_t length = inputs.size();
	CHECK(branches.run("jumpTo", {int64_t{0}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == later; }, 20) && inputs.size() == length);
	/* a load taken back: the run as it was before it */
	session.run("seek", {int64_t{2}}, error);
	CHECK(d.until([&] { return session.frame.get() == 2; }, 20));
	roll.run("paint", {int64_t{1}, int64_t{1}, int64_t{6}, true}, error);
	const std::string beforeLoad = inputs.text();
	const int64_t loads = branches.loads.get();
	CHECK(branches.run("load", {int64_t{0}}, error).has_value() && branches.undoText.get() == "Undo Branch Load");
	CHECK(d.until([&] { return branches.loads.get() == loads + 1 && session.frame.get() == later; }, 20));
	CHECK(inputs.text() != beforeLoad);
	CHECK(branches.run("undo", {}, error).has_value());
	CHECK(d.until([&] { return branches.status.get() == "Branch Load canceled"; }, 20));
	CHECK(session.frame.get() == 2 && inputs.text() == beforeLoad);
	/* the hotkeys' Delete Branch takes the one picked */
	branches.selected.set(1);
	CHECK(branches.run("remove", {branches.selected.get()}, error).has_value() && branches.count.get() == 1 && branches.selected.get() == 0);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* The Greenzone box: read from TAStudio's part of the project, given to
 * the engine, saved back without disturbing the rest of that part */
bool greenzoneChoice(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-gzbox-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	const fs::path project = dir / "g.chimeraProject";
	session.run("saveProject", {project.string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a part made here holds the window's settings and no type name */
	{
		json::Json p;
		CHECK(json::parse(readFile(project), p, error));
		const json::Json *o = p.find("tastudio") != nullptr ? p.find("tastudio")->find("o") : nullptr;
		CHECK(o != nullptr && o->find("$type") == nullptr && o->find("GreenzonePeriod") != nullptr &&
			  o->find("GreenzonePeriod")->number() == 1);
	}
	/* TAStudio's part as Chimera writes it, with a choice of 5 and another setting */
	{
		json::Json p;
		CHECK(json::parse(readFile(project), p, error));
		json::Json o, tastudio;
		o.kind = tastudio.kind = json::Json::Kind::Object;
		o.members.emplace_back("HorizontalOrientation", json::Json::boolean_(true));
		o.members.emplace_back("GreenzonePeriod", json::Json::integer(5));
		tastudio.members.emplace_back("o", o);
		/* in place of the part the save wrote */
		bool replaced = false;
		for (auto &[k, v] : p.members)
			if (k == "tastudio")
			{
				v = tastudio;
				replaced = true;
			}
		if (!replaced) p.members.emplace_back("tastudio", tastudio);
		std::ofstream(project, std::ios::binary | std::ios::trunc) << json::write(p);
	}
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.greenzonePeriod.get() == 5; }, 60));
	auto advance = [&](int n) {
		const int64_t to = session.frame.get() + n;
		for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == to; }, 30));
		d.until([] { return false; }, 0.1);
	};
	/* one in five: twenty frames store few states; every frame: many more */
	const int64_t s0 = session.greenzoneStates.get();
	advance(20);
	const int64_t fifths = session.greenzoneStates.get() - s0;
	session.greenzonePeriod.set(1);
	advance(20);
	const int64_t every = session.greenzoneStates.get() - s0 - fifths;
	CHECK(fifths <= 6 && every >= 15);
	/* none: nothing more is stored */
	session.greenzonePeriod.set(0);
	const int64_t before = session.greenzoneStates.get();
	advance(10);
	CHECK(session.greenzoneStates.get() == before);
	/* saved into TAStudio's part, the rest of it as it was */
	session.run("saveProject", {std::string()}, error);
	CHECK(d.until([&] { return session.status.get().rfind("Saved", 0) == 0; }, 20));
	{
		json::Json p;
		CHECK(json::parse(readFile(project), p, error));
		const json::Json *o = p.find("tastudio") != nullptr ? p.find("tastudio")->find("o") : nullptr;
		CHECK(o != nullptr && o->find("GreenzonePeriod") != nullptr && o->find("GreenzonePeriod")->number() == 0 &&
			  o->find("HorizontalOrientation") != nullptr && o->find("HorizontalOrientation")->boolean);
	}
	/* cycled: every frame, every N, none */
	session.run("cycleGreenzone", {}, error);
	CHECK(session.greenzonePeriod.get() == 1);
	session.greenzoneEvery.set(4);
	session.run("cycleGreenzone", {}, error);
	CHECK(session.greenzonePeriod.get() == 4);
	session.run("cycleGreenzone", {}, error);
	CHECK(session.greenzonePeriod.get() == 0);
	/* a game alone starts on every frame */
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get() && session.greenzonePeriod.get() == 1; }, 60));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* Rerecords, counted as Chimera's TAStudio counts them: an edit over frames
 * the machine has stored states past counts one, a stroke counts once
 * however many cells it crosses, frames added at the end count none, a
 * branch loaded over the movie counts one; the count is kept with the
 * project and over a restart. */
/* TAStudio's right button held and the wheel turned: a frame a notch; the
 * first notch of a hold from the frame the machine is on, the next on from
 * where the last was going (reported 2026-10-28: a new hold went on from an
 * old hold's frame) */
bool wheelSeek(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 30; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 30; }, 20));
	/* a hold: three notches up, then one down before the machine is there */
	CHECK(session.run("wheelSeek", {int64_t{3}, true}, error) == Value(int64_t{27}));
	CHECK(session.run("wheelSeek", {int64_t{-1}, false}, error) == Value(int64_t{28}));
	CHECK(d.until([&] { return session.frame.get() == 28; }, 20));
	/* the machine moved another way, then a new hold */
	session.run("seek", {int64_t{15}}, error);
	CHECK(d.until([&] { return session.frame.get() == 15; }, 20));
	CHECK(session.run("wheelSeek", {int64_t{1}, true}, error) == Value(int64_t{14}));
	CHECK(d.until([&] { return session.frame.get() == 14; }, 20));
	CHECK(session.run("wheelSeek", {int64_t{20}, false}, error) == Value(int64_t{0}));
	CHECK(d.until([&] { return session.frame.get() == 0; }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

bool rerecordCount(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-rerecords-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	std::string error;
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 10; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 10; }, 20));
	auto settle = [&] { d.until([] { return false; }, 0.2); };
	settle();
	CHECK(session.rerecords.get() == 0);
	/* an edit over played frames */
	roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
	CHECK(d.until([&] { return session.rerecords.get() == 1 && session.frame.get() == 10; }, 20));
	/* a stroke: pressed on one cell, dragged over three more */
	roll.run("press", {int64_t{3}, int64_t{5}}, error);
	for (int64_t f = 4; f <= 6; f++)
	{
		settle();
		roll.run("drag", {f, int64_t{5}}, error);
	}
	settle();
	CHECK(session.rerecords.get() == 2);
	/* frames added at the end redo nothing */
	roll.run("fill", {int64_t{20}}, error);
	roll.run("paint", {int64_t{15}, int64_t{18}, int64_t{1}, true}, error);
	settle();
	CHECK(session.rerecords.get() == 2);
	/* undoing an edit over played frames is one */
	roll.run("undo", {}, error);
	roll.run("undo", {}, error);
	roll.run("undo", {}, error);
	settle();
	CHECK(session.rerecords.get() == 3);
	/* a branch loaded over the movie */
	CHECK(branches.run("create", {std::string("b")}, error).has_value());
	CHECK(d.until([&] { return branches.made.get() == 1; }, 20));
	roll.run("toggle", {int64_t{1}, int64_t{0}}, error);
	CHECK(d.until([&] { return session.rerecords.get() == 4 && session.frame.get() == 10; }, 20));
	branches.run("load", {int64_t{0}}, error);
	CHECK(d.until([&] { return branches.loads.get() == 1; }, 20));
	settle();
	CHECK(session.rerecords.get() == 5);
	/* kept over a restart, and with the project */
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() == 2; }, 60));
	settle();
	CHECK(session.rerecords.get() == 5);
	const fs::path project = dir / "walker.chimeraProject";
	session.run("saveProject", {project.string()}, error);
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	{
		const std::string text = readFile(project);
		const size_t at = text.find("\"rerecords\"");
		CHECK(at != std::string::npos && std::atoi(text.c_str() + text.find(':', at) + 1) == 5);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == project.string(); }, 60));
	settle();
	CHECK(session.rerecords.get() == 5);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* TAStudio's recording mode, as Chimera's: a project opens read only; the
 * keys held then reach the movie only past its end, and only through
 * autohold; recording replaces each frame played with what is held, a
 * Record Frame step of the undo history each, the frames of a run one
 * step; undo or an edit by hand ends recording, a script's edit does not */
bool recordingMode(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-recording-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	/* the project is known once the machine says it is open, and recording
	 * turned on then stays on (review, 2026-10-26: the project's arrival,
	 * posted after, turned it off again) */
	bool projectAtOpen = false;
	{
		Connection atOpen = session.isOpen.changed.connect([&] {
			if (!session.isOpen.get()) return;
			projectAtOpen = session.hasProject.get();
			session.recordingMode.set(true);
		});
		openGame(session, package, (dir / "walker.testrom").string(), error);
		CHECK(d.until([&] { return session.isOpen.get() && session.buttonNames().size() == 8; }, 60));
		d.until([] { return false; }, 0.3);
	}
	CHECK(projectAtOpen && session.recordingMode.get());
	session.recordingMode.set(false);
	const fs::path project = dir / "walker.chimeraProject";
	CHECK(session.run("saveProject", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == project.string() && session.hasProject.get(); }, 20));
	CHECK(!session.recordingMode.get());
	const std::vector<std::string> &names = session.buttonNames();
	const auto downAt = std::find(names.begin(), names.end(), "P1 Down");
	CHECK(downAt != names.end() && names[4] == "P1 A");
	if (downAt == names.end()) return false;
	const uint64_t down = uint64_t{1} << (downAt - names.begin()), a = uint64_t{1} << 4;
	auto settle = [&] { d.until([] { return false; }, 0.2); };
	auto advance = [&](int n) {
		const int64_t to = session.frame.get() + n;
		for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == to; }, 20);
	};
	auto seek = [&](int64_t f) {
		session.run("seek", {f}, error);
		return d.until([&] { return session.frame.get() == f && !session.running.get(); }, 20);
	};
	RowModel *history = nullptr;
	for (const auto &r : roll.rowLists())
		if (r.name == "history") history = r.model;
	CHECK(history != nullptr);
	if (history == nullptr) return false;
	auto stepName = [&](int64_t i) {
		std::string n;
		fromValue(history->cell(i, 0), n);
		return n;
	};
	/* read only: Down held, the movie grows by empty frames */
	session.key("Down", true);
	CHECK(advance(5));
	settle();
	CHECK(inputs.size() == 5);
	for (int64_t f = 0; f < 5; f++) CHECK(inputs.at(f).low() == 0);
	/* past the end autohold still reaches it */
	session.run("toggleAutohold", {std::string("P1 A")}, error);
	CHECK(advance(2));
	settle();
	CHECK(inputs.size() == 7 && inputs.at(5).low() == a && inputs.at(6).low() == a);
	session.run("toggleAutohold", {std::string("P1 A")}, error);
	/* within the movie the frames are played as they are */
	roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
	CHECK(seek(0));
	CHECK(advance(4));
	settle();
	CHECK(inputs.at(1).low() == 0 && inputs.at(2).low() == a && inputs.at(3).low() == 0);
	/* recording: a frame played becomes what is held, Down; frame 2's A goes */
	CHECK(seek(2));
	CHECK(roll.run("clearHistory", {}, error).has_value() && history->rowCount() == 0);
	session.run("toggleRecording", {}, error);
	CHECK(session.recordingMode.get());
	CHECK(advance(2));
	settle();
	CHECK(inputs.at(2).low() == down && inputs.at(3).low() == down);
	CHECK(history->rowCount() == 2 && stepName(0) == "Record Frame: 2" && stepName(1) == "Record Frame: 3");
	/* the frames a run records are one step, over the movie and past its end */
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() >= 12; }, 10));
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	settle();
	CHECK(inputs.size() >= 12 && inputs.at(5).low() == down && inputs.at(11).low() == down);
	CHECK(history->rowCount() == 3 && stepName(2) == "Record Frame: 4");
	/* undone at once: frames 4 to 6 as they were, the movie its 7 frames; and recording is over */
	roll.run("undo", {}, error);
	settle();
	CHECK(inputs.size() == 7 && inputs.at(4).low() == 0 && inputs.at(5).low() == a && inputs.at(6).low() == a && inputs.at(3).low() == down);
	CHECK(!session.recordingMode.get());
	/* a script's edit leaves recording on, an edit by hand ends it */
	session.run("toggleRecording", {}, error);
	roll.scriptEditing.set(true);
	roll.run("toggle", {int64_t{1}, int64_t{4}}, error);
	roll.scriptEditing.set(false);
	CHECK(session.recordingMode.get() && inputs.at(1).low() == a);
	roll.run("toggle", {int64_t{1}, int64_t{4}}, error);
	CHECK(!session.recordingMode.get() && inputs.at(1).low() == 0);
	/* a project opened is read only */
	session.run("toggleRecording", {}, error);
	session.key("Down", false);
	session.run("saveProject", {}, error);
	CHECK(d.until([&] { return !session.unsaved.get(); }, 20));
	CHECK(session.recordingMode.get());
	const int64_t opens = session.opens.get();
	session.run("openProject", {project.string(), std::string(package)}, error);
	CHECK(d.until([&] { return session.opens.get() > opens && session.isOpen.get() && session.projectPath.get() == project.string(); }, 60));
	CHECK(d.until([&] { return !session.recordingMode.get(); }, 20));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* the roll's thick lines between players' columns (reported 2026-10-28:
 * they fell at fixed places): a player's group starts at its first column
 * shown and ends after its last; the console's columns are a group too;
 * hidden columns are passed over. Each column's player is the core's to say
 * (ce_session_player_of); here they are given. */
void playerEdges()
{
	using P = PianoRollViewModel;
	/* QuickerNES's pad: ABsSUDLR, one player */
	std::vector<bool> edges = P::playerEdges({1, 1, 1, 1, 1, 1, 1, 1}, {});
	CHECK(edges == std::vector<bool>({true, false, false, false, false, false, false, false, true}));
	/* Reset, Power, P1 A, P1 B, P2 A, P2 B, then an axis of player 1's */
	const std::vector<int32_t> two{0, 0, 1, 1, 2, 2, 1};
	edges = P::playerEdges(two, {});
	CHECK(edges == std::vector<bool>({true, false, true, false, true, false, true, true}));
	/* Power and P1 B hidden */
	edges = P::playerEdges(two, {true, false, true, false, true, true, true});
	CHECK(edges == std::vector<bool>({true, false, true, false, true, false, true, true}));
	/* P1 A and P1 B hidden: the console meets player 2 */
	edges = P::playerEdges(two, {true, true, false, false, true, true, true});
	CHECK(edges == std::vector<bool>({true, false, false, false, true, false, true, true}));
	CHECK(P::playerEdges({}, {}) == std::vector<bool>({false}));
}

/* Frozen addresses, as Chimera's freezes and as the owner decided
 * (2026-10-24): the engine writes them before every frame, its replays
 * included, for this session only; they stay for the same game run again,
 * go for another game and when the game is closed, and are switched off
 * (not removed) when the machine stops */
/* The frame numbers' colour (owner, 2026-10-24): Set Colors' Frame# Column on
 * a frame the greenzone holds, lag or not; the wash on the current frame and on
 * a frame not run as it stands */
void frameColumnColours()
{
	CHECK(std::string(PianoRollViewModel::frameColumnRole(false, true, false)) == "TasGreenZone");
	CHECK(std::string(PianoRollViewModel::frameColumnRole(false, true, true)) == "TasLagZone");
	CHECK(std::string(PianoRollViewModel::frameColumnRole(true, true, false)) == "TasFrameColumnWash");
	CHECK(std::string(PianoRollViewModel::frameColumnRole(false, false, true)) == "TasFrameColumnWash");
}

/* Scripts run on the machine's thread, before and after every frame, as
 * Chimera's do, and reach the view models by waiting on the window (owner,
 * 2026-10-24): with the machine running as fast as it goes, a script's
 * OnFrameEnd function sees every frame, one after another, and what it holds
 * in OnFrameStart is that frame's input; its reads of memory and of the
 * window's view models answer on the way */
/* The scripts' turns replaced while one of them waits on the window, as the
 * window does when it closes while a script asks it for something: the
 * replacement waits until that turn is over. It was done from inside the
 * wait, and the turn running was freed under it (a crash at the window's
 * exit, 2026-10-27). What the turn finds after its wait is kept outside it,
 * in these, so a turn freed early reads none of its own */
static std::atomic<bool> turnArmed{false}, turnEntered{false}, turnAlive{false}, turnFinished{false};
static std::weak_ptr<int> turnOwnPart;

bool turnsReplacedWhileOneRuns(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	auto part = std::make_shared<int>(1);
	turnOwnPart = part;
	session.setScriptTurns({}, [&session, part = std::move(part)] {
		if (!turnArmed.exchange(false)) return;
		turnEntered = true;
		session.onWindowWait([] {});
		turnAlive = !turnOwnPart.expired();
		turnFinished = true;
	});
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	turnArmed = true;
	session.run("play", {}, error);
	/* the window not answering yet: the turn waits on it */
	const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(30);
	while (!turnEntered && std::chrono::steady_clock::now() < end) std::this_thread::sleep_for(std::chrono::milliseconds(2));
	CHECK(turnEntered.load());
	/* another thread replaces the turns, and is not let through while the turn waits */
	std::atomic<bool> replaced{false};
	std::thread other([&] {
		session.setScriptTurns({}, {});
		replaced = true;
	});
	std::this_thread::sleep_for(std::chrono::milliseconds(300));
	CHECK(!replaced.load());
	/* the window answers: the turn ends whole, then the turns are replaced */
	CHECK(d.until([&] { return replaced.load(); }, 30));
	other.join();
	CHECK(turnFinished.load() && turnAlive.load());
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	return true;
}

bool scriptsOnMachine(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	LuaBridge lua;
	lua.add(session);
	std::vector<std::string> lines;
	lua.onLog = [&](const std::string &l) { lines.push_back(l); };
	lua.toScripts = [&session](const std::function<void()> &job) { return session.onMachineThreadWait(job); };
	lua.toWindow = [&session](const std::function<void()> &job) { return session.onWindowWait(job); };
	session.setScriptTurns([&lua] { lua.frameStart(); }, [&lua] { lua.frameDone(); });
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	CHECK(lua.run(R"(
seen, gaps, bytes, system = {}, 0, 0, ""
event.onframestart(function() joypad.set({["P1 A"] = emu.framecount() % 3 == 0}) end)
event.onframeend(function()
	local f = emu.framecount()
	if #seen > 0 and f ~= seen[#seen] + 1 then gaps = gaps + 1 end
	seen[#seen + 1] = f
	bytes = bytes + memory.readbyte(f % 64, "RAM")
	system = emu.getsystemid()
end)
)", "every-frame", error));
	session.unthrottled.set(true);
	const int64_t from = session.frame.get();
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() >= from + 600; }, 60));
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 5));
	d.until([] { return false; }, 0.2);
	const int64_t to = session.frame.get();
	CHECK(to - from >= 600);
	/* every frame, in order */
	CHECK(lua.run("print(#seen, seen[1], seen[#seen], gaps, system ~= '', bytes >= 0)", "count", error));
	CHECK(!lines.empty() && lines.back() == std::to_string(to - from) + "\t" + std::to_string(from + 1) + "\t" + std::to_string(to) +
		"\t0\tTrue\tTrue");
	/* and each frame's input what the script held for it */
	int64_t wrong = 0;
	for (int64_t f = from; f < to; f++)
	{
		const auto input = session.run("movieInput", {f}, error);
		std::string text;
		if (input) fromValue(*input, text);
		const bool pressed = text.find("P1 A\n") != std::string::npos;
		wrong += pressed != (f % 3 == 0);
	}
	CHECK(wrong == 0);
	/* turbo held: the scripts are left out, unless Customize says to run them then */
	auto seen = [&] {
		lines.clear();
		lua.run("print(#seen)", "n", error);
		return lines.empty() ? int64_t{-1} : static_cast<int64_t>(std::stoll(lines.back()));
	};
	auto run = [&](int64_t frames) {
		const int64_t until = session.frame.get() + frames;
		session.run("play", {}, error);
		const bool ran = d.until([&] { return session.frame.get() >= until; }, 60);
		session.run("pause", {}, error);
		return ran && d.until([&] { return !session.running.get(); }, 5);
	};
	const int64_t had = seen();
	session.scriptsDuringTurbo(false);
	session.holdTurbo(true);
	CHECK(run(100));
	CHECK(seen() == had);
	session.scriptsDuringTurbo(true);
	const int64_t at = session.frame.get();
	CHECK(run(100));
	CHECK(seen() == had + session.frame.get() - at);
	session.holdTurbo(false);
	session.setScriptTurns({}, {});
	return true;
}

bool freezes(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-freezes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	fs::copy_file(rom, dir / "other.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	RamWatchViewModel watches(d, session);
	RamSearchViewModel search(d, session);
	HexEditorViewModel hex(d, session);
	std::string error;
	auto advance = [&](int n) {
		const int64_t to = session.frame.get() + n;
		for (int i = 0; i < n; i++) session.run("frameAdvance", {}, error);
		return d.until([&] { return session.frame.get() == to; }, 20);
	};
	auto settle = [&] { d.until([] { return false; }, 0.2); };
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	/* TAStudio's recording mode: what is held becomes the frame */
	session.recordingMode.set(true);
	/* a byte the game never writes */
	const int64_t at = 4000;
	session.key("Down", true);
	CHECK(advance(10));
	const uint8_t untouched = session.readDomain("RAM").at(at);
	CHECK(untouched != 0x5A);
	CHECK(session.run("freeze", {std::string("RAM"), at, int64_t{1}, false, int64_t{0x5A}}, error).has_value());
	CHECK(session.frozenCount.get() == 1 && session.frozen("RAM", at) && !session.frozen("RAM", at + 1));
	CHECK(advance(3));
	CHECK(session.readDomain("RAM").at(at) == 0x5A);
	/* a replay inside the engine writes it too: an edit before the frame the machine is on */
	CHECK(session.run("writeMemory", {std::string("RAM"), at, std::string(1, static_cast<char>(untouched))}, error).has_value());
	CHECK(session.readDomain("RAM").at(at) == untouched);
	roll.run("toggle", {int64_t{2}, int64_t{4}}, error);
	settle();
	CHECK(d.until([&] { return session.frame.get() == 13; }, 20));
	CHECK(session.readDomain("RAM").at(at) == 0x5A);
	/* refused where the machine cannot write */
	CHECK(!session.run("freeze", {std::string("NoSuchDomain"), at, int64_t{1}, false, int64_t{1}}, error).has_value());
	CHECK(error == "no memory domain NoSuchDomain");
	CHECK(session.freezes().size() == 1);
	/* the tool windows bind to whether anything is frozen (Unfreeze All shown) by name */
	CHECK(d.until([&] { return watches.get("anyFrozen") == Value(true) && search.get("anyFrozen") == Value(true) &&
		hex.get("anyFrozen") == Value(true); }, 5));
	CHECK(watches.get("selectionFrozen").has_value() && search.get("selectionFrozen").has_value() && hex.get("pickedFrozen").has_value());
	/* the same game run again keeps it: written from its first frame */
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() == 2 && session.frame.get() == 0; }, 60));
	settle();
	CHECK(session.frozenCount.get() == 1);
	CHECK(advance(1));
	CHECK(session.readDomain("RAM").at(at) == 0x5A);
	/* Toggle All Freezes: off and said so, the game's own byte back on a fresh start */
	session.run("toggleAllFreezes", {}, error);
	CHECK(session.frozenCount.get() == 0 && session.freezes().size() == 1 && session.status.get() == "Freezes toggled (off)");
	session.run("restart", {}, error);
	CHECK(d.until([&] { return session.opens.get() == 3 && session.frame.get() == 0; }, 60));
	CHECK(advance(1));
	CHECK(session.readDomain("RAM").at(at) == untouched);
	session.run("toggleAllFreezes", {}, error);
	CHECK(session.frozenCount.get() == 1 && session.status.get() == "Freezes toggled (on)");
	/* the machine stops: the freeze is switched off, not removed */
	session.key("Down", false);
	session.recordingMode.set(true);
	session.run("scriptHold", {std::string("P1 Up\nP1 Down\nP1 Left\nP1 Right\nP1 A\nP1 B\nP1 Start\nP1 Select")}, error);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.status.get().find("The machine stopped") == 0; }, 20));
	settle();
	CHECK(session.frozenCount.get() == 0 && session.freezes().size() == 1 && !session.freezes().front().enabled);
	/* and asked what now, as Chimera's The core stopped: the frame, why, the
	 * latest safe point, frame 0, the freeze switched off (review, 2026-10-26) */
	{
		CHECK(d.until([&] { return session.stoppedAt.get() >= 0; }, 5));
		const int64_t at = session.stoppedAt.get(), safe = session.stoppedSafePoint.get();
		CHECK(at == session.frame.get() && !session.stoppedReason.get().empty() && safe >= 0 && safe <= at &&
			session.stoppedCanRestart.get() && session.stoppedFreezesOff.get() == 1);
		/* back to the safe point: the question gone, the machine there and running again */
		CHECK(session.run("stoppedChoice", {std::string("back")}, error).has_value() && session.stoppedAt.get() == -1);
		CHECK(d.until([&] { return session.frame.get() == safe; }, 20));
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == safe + 1; }, 20) && session.stoppedAt.get() == -1);
		CHECK(!session.run("stoppedChoice", {std::string("sideways")}, error) && error == "no choice sideways");
	}
	/* another game: gone */
	session.run("toggleAllFreezes", {}, error);
	CHECK(session.frozenCount.get() == 1);
	openGame(session, package, (dir / "other.testrom").string(), error);
	CHECK(d.until([&] { return session.opens.get() == 4 && session.isOpen.get(); }, 60));
	CHECK(d.until([&] { return session.freezes().empty() && session.frozenCount.get() == 0; }, 20));
	/* and closing the game lets them go */
	CHECK(session.run("freeze", {std::string("RAM"), at, int64_t{1}, false, int64_t{0x11}}, error).has_value());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(session.freezes().empty() && session.frozenCount.get() == 0);
	removeTree(dir);
	return true;
}

/* New Project, as Chimera's window makes one: a core, its machine, files
 * in the slots it declares, its settings, its firmware; made, then opened */
std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

/* Chimera's compile step (its wizard's last page), for a core that compiles
 * a game's code before a project is made, as RPCS3's: the synthetic core's
 * compile session (six modules, a share a session) run by the headless
 * runner, side by side; the modules the game's manifest lists, Create
 * refused until they are all there, a module gone said */
/* The MCP server's Lua tools (issue 355), as Chimera's: run_lua's code runs to
 * its end, what it returned given back as JSON and what it printed with it,
 * waiting for a frame refused; lua_script starts a file as Open Script does,
 * and stops it out of the list. And the turns scripts are given as Chimera's:
 * emu.yield goes on at the window's turn with no frame run, emu.frameadvance
 * only after a frame; a machine opened or closed starts the running scripts
 * again from their beginning. */
void luaForAgent()
{
	ManualDispatcher d;
	Sample vm(d);
	LuaBridge lua;
	lua.add(vm);
	LuaConsoleViewModel console(d, lua);
	lua.add(console);
	console.luaFolder.set(scriptFolder().string());
	std::string results, error;
	/* an expression gives its values, as on the console's line, and the code is shown */
	CHECK(console.runForAgent("1 + 1, 'x'", results, error) && results == "[2, \"x\"]");
	CHECK(console.takeOutputForAgent() == "[agent] 1 + 1, 'x'\n" && console.takeOutputForAgent().empty());
	/* statements give what they return; a table with keys 1 to n is a list, what it printed comes with it */
	CHECK(console.runForAgent("print('hello from the agent') return {1, 2, x = nil}, 1 + 1", results, error) && results == "[[1, 2], 2]");
	CHECK(console.takeOutputForAgent() == "[agent] print('hello from the agent') return {1, 2, x = nil}, 1 + 1\nhello from the agent\n");
	/* else an object by its keys' names; a whole number said as one, a function as such */
	CHECK(console.runForAgent("return {b = 2.0, a = {[3] = true, z = 2.5}}, print, nil", results, error) &&
		results == "[{\"a\": {\"3\": true, \"z\": 2.5}, \"b\": 2}, \"(a function)\", null]");
	/* tables within tables followed eight deep */
	CHECK(console.runForAgent("local t = {} local c = t for i = 1, 10 do c.n = {} c = c.n end return t", results, error) &&
		results == "[{\"n\": {\"n\": {\"n\": {\"n\": {\"n\": {\"n\": {\"n\": {\"n\": \"(a table, too deep to show)\"}}}}}}}}]");
	/* an error is the agent's, and shown */
	console.takeOutputForAgent();
	CHECK(!console.runForAgent("emu.nope()", results, error) && error.find("nope") != std::string::npos);
	CHECK(console.takeOutputForAgent() == "[agent] emu.nope()\n" + error + "\n");
	CHECK(!console.runForAgent("syntax error here", results, error) && error.find("input:1:") != std::string::npos);
	/* waiting for a frame is refused, as Chimera's refuses it, and nothing is left waiting */
	CHECK(!console.runForAgent("emu.frameadvance()", results, error) && error.find("emu.frameadvance() is not allowed during any callbacks") != std::string::npos);
	CHECK(!console.runForAgent("emu.yield()", results, error) && error.find("emu.yield() is not allowed") != std::string::npos);
	CHECK(!console.runForAgent("coroutine.yield()", results, error) && error.find("lua_script") != std::string::npos && !lua.running());
	/* a function it registers keeps it, and runs on each frame */
	CHECK(console.runForAgent("agentSeen = 0 event.onframeend(function() agentSeen = agentSeen + 1 end)", results, error) && results == "[]");
	CHECK(lua.running());
	console.frameDone();
	CHECK(console.runForAgent("agentSeen", results, error) && results == "[1]");
	/* lua_script: a file started as Open Script starts it, in the list; stopped, taken out */
	const std::string waits = scriptFile("frames = 0\nprint('the script started')\n"
		"event.onframeend(function() end)\n"
		"while true do frames = frames + 1 emu.frameadvance() end", "agent-frames");
	const std::string yields = scriptFile("yielded = 0\nwhile true do yielded = yielded + 1 emu.yield() end", "agent-yields");
	console.takeOutputForAgent();
	CHECK(console.scriptForAgent(waits, false) && console.takeOutputForAgent() == "the script started\n");
	CHECK(console.scriptForAgent(yields, false));
	RowModel &list = *console.rowLists()[1].model;
	CHECK(list.rowCount() == 2);
	/* the window's turn with no frame: the one in emu.yield goes on, the one waiting for a frame does not */
	CHECK(console.runForAgent("frames, yielded", results, error) && results == "[1, 1]");
	lua.yieldTurn();
	lua.yieldTurn();
	CHECK(console.runForAgent("frames, yielded", results, error) && results == "[1, 3]");
	/* a frame: both */
	console.frameDone();
	CHECK(console.runForAgent("frames, yielded, agentSeen", results, error) && results == "[2, 4, 2]");
	/* a machine opened or closed: every script stopped, those that ran started again; the agent's code goes */
	console.restartScripts();
	CHECK(console.runForAgent("frames, yielded, agentSeen", results, error) && results == "[1, 1, 2]");
	console.frameDone();
	CHECK(console.runForAgent("frames, agentSeen", results, error) && results == "[2, 2]");
	CHECK(list.rowCount() == 2 && console.scriptForAgent(waits, false));
	CHECK(!console.scriptForAgent(waits, true) && list.rowCount() == 1);
	CHECK(!console.scriptForAgent(scriptFile("print('done at once')", "agent-once"), false) && list.rowCount() == 2);
	CHECK(console.takeOutputForAgent().find("done at once") != std::string::npos);
	console.scriptForAgent(yields, true);
	console.scriptForAgent((scriptFolder() / "agent-once.lua").string(), true);
	CHECK(list.rowCount() == 0 && !lua.running());
}

/* The MCP server's create_project (issue 355): the agent's answers taken
 * through the New Project form as Chimera's wizard takes them, the project
 * made and opened; what keeps it from being made said in Chimera's words,
 * firmware not found listed with the files that would do */
bool createForAgent(const char *package, const char *rom, const char *hashedPackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(hashedPackage, ec) == 0 || ec)
	{
		std::printf("SKIP create_project: no package with a hashed firmware was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-agent-project-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::create_directories(dir / "Firmware");
	fs::create_directories(dir / "Elsewhere");
	fs::copy_file(package, dir / "Cores" / "synth-box.chimeraCore");
	fs::copy_file(hashedPackage, dir / "Cores" / "synth-hashed.chimeraCore");
	const std::string game = (dir / "walker.testrom").string();
	fs::copy_file(rom, game);
	/* the file the hashed package's firmware pins, under another name */
	std::ofstream(dir / "Elsewhere" / "the-logo.bin", std::ios::binary) << "the original logo";
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel agent(d, session, installed);
	auto ask = [&](NewProjectViewModel::AgentRequest request) {
		std::optional<NewProjectViewModel::AgentOutcome> got;
		agent.completeForAgent(request, [&](NewProjectViewModel::AgentOutcome o) { got = std::move(o); });
		d.until([&] { return got.has_value(); }, 20);
		return got.value_or(NewProjectViewModel::AgentOutcome{false, "no answer", {}, {}});
	};
	NewProjectViewModel::AgentRequest r;
	r.core = "No Such Core";
	r.games = {game};
	CHECK(ask(r).refusal == "There is no core called 'No Such Core' in the cores folder. list_cores names them.");
	r.core = "Synth";
	r.system = "Nope";
	const auto notThat = ask(r);
	CHECK(!notThat.made && notThat.refusal.rfind("Synth is a ", 0) == 0 && notThat.refusal.find(", not a 'Nope'.") != std::string::npos);
	r.system.clear();
	r.games = {(dir / "nothing.testrom").string()};
	CHECK(ask(r).refusal == "There is no file at '" + (dir / "nothing.testrom").string() + "'.");
	r.games.clear();
	r.files = {{"nope", game}};
	CHECK(ask(r).refusal.rfind("Synth has no slot 'nope'. Its slots: ", 0) == 0);
	r.files.clear();
	r.games = {game};
	json::parse("{\"NoSuchSetting\": 1}", r.settings, error);
	CHECK(ask(r).refusal == "Synth has no setting called 'NoSuchSetting'. describe_core lists its settings.");
	r.settings = json::Json();
	/* a file the slot does not take: refused when the project is made, and said so, not called made (issue 422) */
	std::ofstream(dir / "notes.txt") << "not a game";
	r.games.clear();
	r.files = {{"rom", (dir / "notes.txt").string()}};
	const auto wrongKind = ask(r);
	CHECK(!wrongKind.made && wrongKind.refusal == "file 'notes.txt' is not a format slot 'rom' accepts");
	r.files.clear();
	r.games = {game};
	/* by its package's file name; the firmware it pins found nowhere: listed, with where it was looked for */
	r.core = "synth-hashed.chimeraCore";
	const auto missing = ask(r);
	CHECK(!missing.made && missing.refusal == "Firmware the project needs was not found: The logo.");
	CHECK(json::write(missing.missing) ==
		"[{\"id\": \"logo\", \"name\": \"The logo\", \"accepted\": [{\"file\": \"logo.bin\", \"size\": null, \"sha1\": \"CE31CFD83A86A850B5B110A3AC05B634F186C1F1\"}]}]");
	CHECK(json::write(missing.searched).find(json::quote((dir / "Firmware").string())) != std::string::npos);
	/* found in the folder the agent names: made and opened */
	r.firmwareFolder = (dir / "Elsewhere").string();
	const auto made = ask(r);
	CHECK(made.made && made.refusal.empty());
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get() && session.coreName.get() == "Synth Hashed"; }, 30));
	/* another, over it */
	r = {};
	r.core = "synth";
	r.games = {game};
	CHECK(ask(r).made);
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get() && session.coreName.get() == "Synth"; }, 30));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir, ec);
	return true;
}

bool compileStep(const char *compilePackage, const char *rom)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const char *runner = std::getenv("CHIMERA_RUN");
	if (runner == nullptr || !fs::exists(runner, ec) || fs::file_size(compilePackage, ec) == 0)
	{
		std::printf("SKIP the compile step: no runner (CHIMERA_RUN) or no compiling package\n");
		return true;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-compile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::copy_file(compilePackage, dir / "Cores" / "synth-compile.chimeraCore");
	const std::string game = (dir / "walker.testrom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel wizard(d, session, installed);
	wizard.compileRunner.set(runner);
	wizard.compileRoot.set((dir / "compiled").string());
	CHECK(wizard.run("chooseCore", {std::string("Synth Compile")}, error).has_value());
	CHECK(!wizard.compileAsked.get());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value());
	CHECK(wizard.compileAsked.get() && !wizard.compileReady.get() &&
		wizard.compileStatus.get() == "This game's code is not compiled yet: Compile does it.");
	const fs::path made = dir / "compiled.chimeraProject";
	CHECK(!wizard.run("create", {made.string()}, error) && error == "Compile this game's code first.");
	/* compiled: every session's share, the game's manifest */
	CHECK(wizard.run("compile", {}, error).has_value() && wizard.compiling.get());
	CHECK(d.until([&] { return !wizard.compiling.get(); }, 60));
	RowModel *modules = nullptr;
	for (const auto &r : wizard.rowLists())
		if (r.name == "compiled") modules = r.model;
	CHECK(modules != nullptr && modules->rowCount() == 6);
	CHECK(wizard.compileReady.get() && wizard.compileStatus.get() == "All 6 modules are compiled." && wizard.compileDone.get() == 6 &&
		wizard.compileTotal.get() == 6);
	char sha[41] = {};
	ce_sha1_file(game.c_str(), sha, nullptr);
	const fs::path folder = dir / "compiled" / sha;
	const std::string manifest = readFile(folder / "manifest.json");
	CHECK(manifest.find("\"rom\": \"walker.testrom\"") != std::string::npos && manifest.find("synth/module5.bin") != std::string::npos &&
		manifest.find("\"core\": \"Synth Compile\"") != std::string::npos);
	/* a module gone: said, and Create waits again */
	fs::remove(folder / "synth" / "module3.bin");
	CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value());
	CHECK(!wizard.compileReady.get() && wizard.compileStatus.get() == "5 of 6 modules are there: Compile does the rest.");
	CHECK(wizard.run("compile", {}, error).has_value());
	CHECK(d.until([&] { return !wizard.compiling.get(); }, 60) && wizard.compileReady.get());
	CHECK(wizard.run("create", {made.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == made.string(); }, 60));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* code another build of the core compiled is not this build's (issues
	 * 361 and 383): New Project compiles it again, from nothing, and a
	 * session removes it before the core can read it */
	{
		const std::string thisBuild = readFile(folder / "manifest.json");
		auto olderBuild = [&] {
			std::string m = thisBuild;
			const std::string key = "\"coreVersion\": \"";
			const size_t at = m.find(key) + key.size();
			m.replace(at, m.find('"', at) - at, "older");
			std::ofstream(folder / "manifest.json", std::ios::binary) << m;
			std::ofstream(folder / "synth" / "stale.bin") << "an older build's code";
		};
		olderBuild();
		CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
		CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value());
		CHECK(!wizard.compileReady.get() && wizard.compileStatus.get() ==
			"This game's code was compiled by Synth Compile older, not by this build of the core: Compile does it again.");
		CHECK(wizard.run("compile", {}, error).has_value());
		CHECK(d.until([&] { return !wizard.compiling.get(); }, 60) && wizard.compileReady.get());
		CHECK(!fs::exists(folder / "synth" / "stale.bin") && readFile(folder / "manifest.json").find("\"coreVersion\": \"older\"") == std::string::npos);
		/* a session: the older build's code goes when the project opens */
		olderBuild();
		session.compileCacheRoot.set((dir / "compiled").string());
		session.run("openProject", {made.string(), (dir / "Cores" / "synth-compile.chimeraCore").string()}, error);
		CHECK(d.until([&] { return session.isOpen.get(); }, 60));
		CHECK(!fs::exists(folder / "synth" / "stale.bin") && !fs::exists(folder / "synth" / "module3.bin"));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		/* files no manifest names a build for go too; this build's stay */
		fs::remove(folder / "manifest.json");
		std::ofstream(folder / "unnamed.bin") << "whose?";
		session.run("openProject", {made.string(), (dir / "Cores" / "synth-compile.chimeraCore").string()}, error);
		CHECK(d.until([&] { return session.isOpen.get(); }, 60));
		CHECK(!fs::exists(folder / "unnamed.bin") && fs::exists(folder / "manifest.json"));
		std::ofstream(folder / "kept.bin") << "this build's";
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		session.run("openProject", {made.string(), (dir / "Cores" / "synth-compile.chimeraCore").string()}, error);
		CHECK(d.until([&] { return session.isOpen.get(); }, 60) && fs::exists(folder / "kept.bin"));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
#ifndef _WIN32
	/* Sessions killed for want of memory: half as many, and again, until they
	 * get through, as Chimera's; one alone killed says so in Chimera's words.
	 * A stand-in session: killed (137) unless it runs alone, or always. */
	{
		const fs::path log = dir / "sessions.log", script = dir / "session.sh";
		std::ofstream(script) << "#!/bin/sh\n"
			"spec=; out=; while [ $# -gt 0 ]; do case \"$1\" in --precompile) spec=$2; shift;; --cache-dir) out=$2; shift;; esac; shift; done\n"
			"echo \"$spec\" >> '" << log.string() << "'\n"
			"[ -n \"$ALWAYS\" ] && exit 137\n"
			"[ \"${spec#*/}\" != 1 ] && exit 137\n"
			"mkdir -p \"$out/synth\"\n"
			"for i in 0 1 2 3 4 5; do printf 'm%s' $i > \"$out/synth/module$i.bin\"; "
			"echo \"[cache] stored synth/module$i.bin $(sha1sum \"$out/synth/module$i.bin\" | cut -d' ' -f1)\"; done\n";
		fs::permissions(script, fs::perms::owner_all);
		const fs::path other = dir / "other.testrom";
		{
			std::ifstream in(rom, std::ios::binary);
			std::ofstream out(other, std::ios::binary);
			out << in.rdbuf() << "another game";
		}
		wizard.compileRunner.set(script.string());
		/* counted at 1 GB a session, so a machine of any size runs more than one at first */
		wizard.compileSessionGigabytes.set(1);
		CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
		CHECK(wizard.run("addFile", {std::string("rom"), other.string()}, error).has_value());
		CHECK(wizard.compileAsked.get() && !wizard.compileReady.get());
		CHECK(wizard.run("compile", {}, error).has_value());
		CHECK(d.until([&] { return !wizard.compiling.get(); }, 60));
		const std::string specs = readFile(log);
		CHECK(wizard.compileReady.get() && specs.find("0/1\n") != std::string::npos);
		const int firstCount = std::atoi(specs.substr(specs.find('/') + 1).c_str());
		CHECK(firstCount == 1 || specs.find("0/" + std::to_string(std::max(1, firstCount / 2)) + "\n") != std::string::npos);
		std::printf("compile sessions tried: %s\n", std::regex_replace(specs, std::regex("\n"), " ").c_str());
		/* killed even alone: the person told what ran out */
		setenv("ALWAYS", "1", 1);
		fs::remove_all(dir / "compiled", ec);
		CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
		CHECK(wizard.run("addFile", {std::string("rom"), other.string()}, error).has_value());
		CHECK(wizard.run("compile", {}, error).has_value());
		CHECK(d.until([&] { return !wizard.compiling.get(); }, 60));
		unsetenv("ALWAYS");
		CHECK(!wizard.compileReady.get() && wizard.compileStatus.get().rfind("The computer ran out of memory while compiling. A compile session "
			"needs about 1 GB", 0) == 0);
	}
#endif
	fs::remove_all(dir, ec);
	return true;
}

bool newProjectWizard(const char *package, const char *rom, const char *slotsPackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(slotsPackage, ec) == 0 || ec)
	{
		std::printf("SKIP New Project: no slots package was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-wizard-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::create_directories(dir / "Firmware");
	fs::copy_file(package, dir / "Cores" / "synth-box.chimeraCore");
	fs::copy_file(slotsPackage, dir / "Cores" / "synth-slots.chimeraCore");
	const std::string game = (dir / "walker.testrom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	/* the installed cores' media recipes, as the Media Maker is given them */
	{
		const auto recipes = installed.mediaRecipes();
		CHECK(recipes.size() == 1 && recipes[0].label == "synthetic disc" && recipes[0].json.find("\"SYNTH.BIN\"") != std::string::npos);
	}
	NewProjectViewModel wizard(d, session, installed);
	ProjectDetailsViewModel details(d, session);
	auto cell = [&](const char *rowsName, int64_t row, int32_t column) {
		for (const auto &r : wizard.rowLists())
			if (r.name == rowsName) return r.model->cell(row, column);
		return Value{};
	};
	auto text = [&](const char *rowsName, int64_t row, int32_t column) {
		std::string t;
		fromValue(cell(rowsName, row, column), t);
		return t;
	};
	auto rowCount = [&](const char *rowsName) {
		for (const auto &r : wizard.rowLists())
			if (r.name == rowsName) return r.model->rowCount();
		return int64_t{-1};
	};
	auto settingNames = [&] {
		std::string out;
		for (int64_t r = 0; r < rowCount("settings"); r++) out += text("settings", r, 0) + " ";
		return out;
	};
	CHECK(wizard.coreNames.get() == "Synth\nSynth Slots");
	/* Chimera's list: a line a package, its version, its systems; only
	 * emulators here, so asking for games lists them still */
	CHECK(std::regex_match(wizard.coreLines.get(),
		std::regex("Synth  [0-9a-f]{8}( local)?  -  Synthetic Witness\nSynth Slots  [0-9a-f]{8}( local)?  -  Synthetic Witness, SynthB")));
	CHECK(wizard.coreLine.get() == -1 && wizard.kind.get() == "emulators");
	CHECK(wizard.run("chooseKind", {std::string("games")}, error).has_value() && wizard.coreLines.get().find("Synth Slots") != std::string::npos);
	CHECK(wizard.run("chooseKind", {std::string("emulators")}, error).has_value());
	CHECK(wizard.problems.get() == "Choose a core" && !wizard.ready.get());
	CHECK(!wizard.run("addFile", {std::string("rom"), game}, error) && error == "choose a core first");

	/* a package of one kind of file: the game */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	CHECK(rowCount("slots") == 1 && text("slots", 0, 0) == "rom" && text("slots", 0, 4) == ".testrom" && text("slots", 0, 9) == "*.testrom");
	/* the package put in the place of another while the program runs, as a
	 * newer one is (issue 423): the wizard looks at the folder again and
	 * starts over with it, where it kept the one gone */
	{
		const int64_t line = wizard.coreLine.get();
		fs::rename(dir / "Cores" / "synth-box.chimeraCore", dir / "Cores" / "synth-box-newer.chimeraCore");
		CHECK(wizard.run("refresh", {}, error).has_value());
		CHECK(wizard.core.get() == "Synth" && wizard.coreLine.get() == line && installed.count.get() == 2);
		error.clear();
		CHECK(wizard.run("chooseCoreLine", {line}, error).has_value() && error.empty());
		fs::rename(dir / "Cores" / "synth-box-newer.chimeraCore", dir / "Cores" / "synth-box.chimeraCore");
		CHECK(wizard.run("refresh", {}, error).has_value() && wizard.core.get() == "Synth" && wizard.coreLine.get() == line);
		CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	}
	CHECK(wizard.problems.get() == "Game: a file is needed" && wizard.machines.get().empty());
	/* the wizard's pages, in Chimera's words: the files page asked, what keeps it from going on */
	CHECK(wizard.filesAsked.get() && wizard.filesComplaint.get() == "Game: needs exactly one, has 0" && text("slots", 0, 10) == "exactly one");
	CHECK(!wizard.firmwareAsked.get() && wizard.firmwareMissing.get().empty());
	std::ofstream(dir / "notes.txt") << "not a game";
	/* a file of another kind is said, not refused, as Chimera's; more than the slot takes is said at Next */
	CHECK(wizard.run("addFile", {std::string("rom"), (dir / "notes.txt").string()}, error).has_value() &&
		wizard.status.get() == "notes.txt is not one of the formats this slot expects (.testrom)");
	/* and refused when the project is made, the engine's rule: the wizard is told why and that it was
	 * for its files, and stays open until then (issue 422: the window closed and nothing was said) */
	const int64_t madeBefore = wizard.made.get();
	{
		const fs::path refused = dir / "refused.chimeraProject";
		CHECK(wizard.ready.get() && wizard.run("create", {refused.string()}, error).has_value() && wizard.making.get());
		CHECK(!wizard.run("create", {refused.string()}, error) && error == "Making the project...");
		CHECK(d.until([&] { return !wizard.making.get(); }, 60));
		CHECK(wizard.filesRefused.get() && wizard.made.get() == madeBefore && !fs::exists(refused));
		CHECK(wizard.status.get() == "Not made: file 'notes.txt' is not a format slot 'rom' accepts");
	}
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && wizard.filesComplaint.get() == "Game: takes exactly one, has 2");
	CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
	CHECK(wizard.ready.get() && wizard.problems.get().empty() && wizard.status.get().empty());
	CHECK(wizard.filesComplaint.get().empty());
	/* the same file again: nothing to do */
	{
		int64_t n = 0;
		CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && fromValue(cell("slots", 0, 8), n) && n == 1);
	}
	CHECK(text("slots", 0, 7) == "walker.testrom");
	CHECK(settingNames() == "initFillByte ");
	/* a core that declares no renderer draws one way */
	CHECK(wizard.renderers.get() == "Software (Native)" && wizard.renderer.get() == 0);
	CHECK(!wizard.run("set", {std::string("initFillByte"), int64_t{300}}, error) && error == "initFillByte goes from 0 to 255");
	CHECK(wizard.run("set", {std::string("initFillByte"), int64_t{7}}, error).has_value());
	wizard.set("title", std::string("down the corridor"), error);
	const fs::path plain = dir / "walker.chimeraProject";
	CHECK(wizard.run("create", {plain.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == plain.string(); }, 60));
	CHECK(wizard.made.get() == madeBefore + 1 && !wizard.making.get() && !wizard.filesRefused.get());
	{
		const std::string project = readFile(plain);
		CHECK(std::regex_search(project, std::regex("\"initFillByte\"\\s*:\\s*7[^0-9]")));
		CHECK(project.find("down the corridor") != std::string::npos && project.find("walker.testrom") != std::string::npos);
		CHECK(project.find("\"Platform\"") != std::string::npos);
	}
	CHECK(session.projectTitle.get() == "down the corridor" && session.coreName.get() == "Synth");
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* when the machine's clock starts (issue 366): the sandbox's own at first, written as Chimera's
	 * row writes it; a time that is not one refused at Create; another reaching the machine */
	CHECK(wizard.clockStart.get() == "2017-05-27 12:44:28" && NewProjectViewModel::parseClockText("2017-05-27 12:44:28") == 1495889068);
	CHECK(NewProjectViewModel::parseClockText(" 2030-01-02 03:04:05 ") == 1893553445 && NewProjectViewModel::clockText(1893553445) == "2030-01-02 03:04:05");
	CHECK(NewProjectViewModel::parseClockText("2200-01-01 00:00:00") == 7258118400 && !NewProjectViewModel::parseClockText("2200-01-01 00:00:01"));
	CHECK(!NewProjectViewModel::parseClockText("2023-02-29 00:00:00") && NewProjectViewModel::parseClockText("2024-02-29 23:59:59") == 1709251199);
	CHECK(!NewProjectViewModel::parseClockText("1969-12-31 23:59:59") && !NewProjectViewModel::parseClockText("2030-01-02T03:04:05"));
	{
		const std::string bytes = readFile(game);
		std::ofstream(dir / "clock.testrom", std::ios::binary) << bytes << "CLOCKTEST";
		CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
		CHECK(wizard.run("addFile", {std::string("rom"), (dir / "clock.testrom").string()}, error).has_value());
		wizard.clockStart.set("tomorrow");
		const fs::path clocked = dir / "clock.chimeraProject";
		CHECK(!wizard.run("create", {clocked.string()}, error) &&
			error == "the machine's clock start is not a time written as 2017-05-27 12:44:28, between 1970 and 2200 (UTC)");
		wizard.clockStart.set("2030-01-02 03:04:05");
		CHECK(wizard.run("create", {clocked.string()}, error).has_value());
		CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == clocked.string(); }, 60));
		CHECK(std::regex_search(readFile(clocked), std::regex("\"clockStart\"\\s*:\\s*\"2030-01-02T03:04:05Z\"")));
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
		const std::vector<uint8_t> ram = session.readDomain("RAM");
		uint64_t seconds = 0;
		if (ram.size() >= 4096) std::memcpy(&seconds, ram.data() + 4088, 8);
		/* the clock has gone on a frame's worth at most from where it started */
		CHECK(seconds >= 1893553445 && seconds <= 1893553446);
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		wizard.clockStart.set("2017-05-27 12:44:28");
		CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
		CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value());
	}
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));

	/* two boards: each its own kinds of file, settings and firmware */
	CHECK(wizard.run("chooseCore", {std::string("Synth Slots")}, error).has_value());
	CHECK(wizard.machines.get() == "Board A\nBoard B" && wizard.machine.get() == 0);
	bool exposed = false;
	/* presets: the ones for this board, written as typed in, the board and
	 * a name no setting has left out and named */
	CHECK(rowCount("presets") == 1 && text("presets", 0, 1) == "Full fill");
	CHECK(wizard.run("applyPreset", {std::string("full")}, error).has_value());
	CHECK(wizard.status.get() == "Applied \"Full fill\": 1 setting; this machine has no setting named nope." && wizard.machine.get() == 0);
	/* still board A: its presets, its cartridge slot */
	CHECK(rowCount("presets") == 1);
	fromValue(cell("slots", 0, 6), exposed);
	CHECK(exposed);
	{
		int64_t v = -1;
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "initFillByte") fromValue(cell("settings", r, 3), v);
		CHECK(v == 255);
	}
	CHECK(!wizard.run("applyPreset", {std::string("fast")}, error) && error == "no preset fast for this machine");
	CHECK(wizard.problems.get() == "One of these is needed: Cartridge, Disks");
	CHECK(wizard.filesComplaint.get() == "at least one file is needed across: Cartridge, Disks");
	fromValue(cell("slots", 0, 6), exposed);
	CHECK(exposed && rowCount("firmware") == 0);
	CHECK(settingNames() == "initFillByte port ");
	/* the renderer is asked beside the machine, named for what it is; a
	 * hardware one says what it costs */
	CHECK(wizard.renderers.get() == "Software (Native)\nHardware (OpenGL)" && wizard.renderer.get() == 0 && wizard.rendererCaveat.get().empty());
	CHECK(wizard.run("chooseRenderer", {int64_t{1}}, error).has_value() && wizard.renderer.get() == 1);
	CHECK(wizard.rendererCaveat.get().rfind("Hardware renderers use your PC's GPU: fast, but the picture is drawn outside the savestate", 0) == 0);
	CHECK(!wizard.run("chooseRenderer", {int64_t{2}}, error) && error == "no renderer 2");
	CHECK(wizard.run("chooseRenderer", {int64_t{0}}, error).has_value() && wizard.rendererCaveat.get().empty());
	/* a board narrows what a setting offers and starts it on (board A: the
	 * mouse by default; board B: no mouse, and nothing by default) */
	auto portCell = [&](int32_t column) {
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "port") return text("settings", r, column);
		return std::string("no port");
	};
	CHECK(portCell(3) == "mouse" && portCell(4) == "pad\nmouse\nnone");
	/* the disk slot takes the kinds of disk this board claims */
	CHECK(text("slots", 1, 4) == ".dsk" && text("slots", 1, 9) == "*.dsk");
	CHECK(wizard.run("chooseMachine", {int64_t{1}}, error).has_value() && wizard.machine.get() == 1);
	CHECK(text("slots", 1, 4) == ".cue, .img");
	std::ofstream(dir / "disk3.dsk") << "board A's kind of disk";
	CHECK(wizard.run("addFile", {std::string("disk"), (dir / "disk3.dsk").string()}, error).has_value() &&
		wizard.status.get() == "disk3.dsk is not one of the formats this slot expects (.cue, .img)");
	CHECK(wizard.run("removeFile", {std::string("disk"), int64_t{0}}, error).has_value());
	fromValue(cell("slots", 0, 6), exposed);
	CHECK(!exposed && settingNames() == "initFillByte diskSpeed port ");
	CHECK(portCell(3) == "none" && portCell(4) == "pad\nnone");
	CHECK(!wizard.run("set", {std::string("port"), std::string("mouse")}, error) && error == "mouse is not one of port's options");
	CHECK(rowCount("presets") == 2 && wizard.run("applyPreset", {std::string("fast")}, error).has_value());
	{
		int64_t v = -1;
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "diskSpeed") fromValue(cell("settings", r, 3), v);
		CHECK(v == 4);
	}
	CHECK(!wizard.run("addFile", {std::string("rom"), game}, error) && error == "Cartridge does not go with the files or the machine chosen");
	CHECK(rowCount("firmware") == 1 && text("firmware", 0, 1) == "Board B BIOS");
	CHECK(wizard.problems.get() == "One of these is needed: Disks\nBoard B BIOS: not found in the firmware folder; choose its file");
	/* the firmware page: asked, its columns and what is missing in Chimera's words */
	CHECK(wizard.firmwareAsked.get() && wizard.firmwareMissing.get() == "Board B BIOS");
	/* the fast disks' own BIOS, of the two entries named bios (issue 390) */
	CHECK(text("firmware", 0, 5) == "Board B BIOS  (fast disks)" && text("firmware", 0, 6) == "boardbfast.bin" && text("firmware", 0, 8) == "not found");
	/* a cue whose track is not beside it is said at once; names a slot asks for */
	std::ofstream(dir / "disk1.cue") << "FILE \"disk1.bin\" BINARY\n  TRACK 01 MODE1/2048\n    INDEX 01 00:00:00\n";
	CHECK(!wizard.run("addFile", {std::string("disk"), (dir / "disk1.cue").string()}, error) && error == "disk1.cue references disk1.bin, which is not next to it - a cue's files are added together");
	std::ofstream(dir / "disk1.bin") << "the first disk's track";
	CHECK(wizard.run("addFile", {std::string("disk"), (dir / "disk1.cue").string()}, error).has_value());
	std::ofstream(dir / "game.img") << "a disk by another name";
	CHECK(!wizard.run("addFile", {std::string("disk"), (dir / "game.img").string()}, error) && error == "game.img: A disk is called disk, a number and .cue, .img or .dsk.");
	std::ofstream(dir / "disk2.img") << "the second disk";
	CHECK(wizard.run("addFile", {std::string("disk"), (dir / "disk2.img").string()}, error).has_value());
	CHECK(wizard.run("moveFile", {std::string("disk"), int64_t{1}, int64_t{0}}, error).has_value() && text("slots", 1, 7) == "disk2.img\ndisk1.cue");
	CHECK(wizard.run("set", {std::string("diskSpeed"), int64_t{3}}, error).has_value());
	CHECK(rowCount("firmware") == 1 && text("firmware", 0, 5) == "Board B BIOS" && text("firmware", 0, 6) == "boardb.bin");
	CHECK(!wizard.run("set", {std::string("board"), std::string("a")}, error));
	/* the core is asked about the game with what is chosen so far: the slot
	 * map, the other files and a cue's tracks, the settings */
	CHECK(wizard.run("moveFile", {std::string("disk"), int64_t{1}, int64_t{0}}, error).has_value());
	CHECK(wizard.suggestion.get().find(" It sees 2 disks and the track of disk1.cue, at disk speed 3.") != std::string::npos);
	CHECK(wizard.run("moveFile", {std::string("disk"), int64_t{1}, int64_t{0}}, error).has_value() && text("slots", 1, 7) == "disk2.img\ndisk1.cue");
	/* the firmware found by scanning a folder: by the name it declares,
	 * below the folder only when asked; a folder that has it becomes the
	 * firmware folder when the one set is not there */
	fs::create_directories(dir / "scan" / "deeper");
	std::ofstream(dir / "scan" / "deeper" / "boardb.bin") << "a board B BIOS, by its name";
	installed.set("firmwareFolder", (dir / "nowhere").string(), error);
	CHECK(wizard.run("scanFolder", {(dir / "scan").string(), false}, error).has_value());
	CHECK(wizard.status.get() == "scanned 0 files; nothing new matched" && !wizard.ready.get());
	CHECK(wizard.run("scanFolder", {(dir / "scan").string(), true}, error).has_value());
	CHECK(wizard.status.get() == "scanned 1 file; 1 requirement resolved" && wizard.ready.get());
	CHECK(wizard.firmwareMissing.get().empty() && text("firmware", 0, 8) == "found: boardb.bin");
	CHECK(text("firmware", 0, 2) == (dir / "scan" / "deeper" / "boardb.bin").string() && text("firmware", 0, 3) == "chosen");
	CHECK(installed.firmwareFolder.get() == (dir / "scan").string());
	/* Clear takes it away until the page is shown again, when what is found is found again */
	CHECK(wizard.run("clearFirmware", {std::string("bios")}, error).has_value() && wizard.firmwareMissing.get() == "Board B BIOS" &&
		text("firmware", 0, 8) == "not found" && !wizard.ready.get());
	CHECK(wizard.run("firmwarePage", {}, error).has_value() && wizard.firmwareMissing.get().empty());
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	/* the folder scanned is looked in from now on, as Chimera's is: what
	 * was chosen there, forgotten, is found there again */
	const std::string scanned = (dir / "scan" / "deeper" / "boardb.bin").string();
	wizard.run("forgetFirmware", {std::string("bios")}, error);
	CHECK(wizard.ready.get() && text("firmware", 0, 2) == scanned && text("firmware", 0, 3) != "chosen");
	/* the firmware: chosen by hand, forgotten, then found in the folder */
	std::ofstream(dir / "mybios.bin") << "a board B BIOS";
	CHECK(wizard.run("chooseFirmware", {std::string("bios"), (dir / "mybios.bin").string()}, error).has_value());
	CHECK(text("firmware", 0, 2) == (dir / "mybios.bin").string() && text("firmware", 0, 3) == "chosen" && wizard.ready.get());
	wizard.run("forgetFirmware", {std::string("bios")}, error);
	CHECK(text("firmware", 0, 2) == scanned);
	wizard.run("chooseFirmware", {std::string("bios"), (dir / "mybios.bin").string()}, error);
	const fs::path boardB = dir / "boardb.chimeraProject";
	CHECK(wizard.run("create", {boardB.string(), false}, error).has_value());
	CHECK(d.until([&] { return wizard.status.get() == "Made " + boardB.string(); }, 30));
	{
		const std::string project = readFile(boardB);
		char sha[41];
		uint64_t len = 0;
		ce_sha1_file((dir / "mybios.bin").string().c_str(), sha, &len);
		CHECK(project.find("SynthB") != std::string::npos && std::regex_search(project, std::regex("\"diskSpeed\"\\s*:\\s*3[^0-9]")));
		CHECK(std::regex_search(project, std::regex("\"board\"\\s*:\\s*\"b\"")));
		CHECK(project.find(sha) != std::string::npos || project.find(lower(sha)) != std::string::npos);
		const size_t d2 = project.find("disk2.img"), d1 = project.find("disk1.cue"), track = project.find("disk1.bin");
		CHECK(d2 != std::string::npos && d1 != std::string::npos && track != std::string::npos && d2 < d1);
		CHECK(project.find("\"support\"") != std::string::npos);
	}
	CHECK(!session.isOpen.get());
	/* board A with its cartridge: made and opened, and it plays; the core
	 * suggests a setting for the game, which is applied, and taken back
	 * when the game is taken out */
	CHECK(wizard.run("chooseMachine", {int64_t{0}}, error).has_value());
	auto fillNow = [&] {
		int64_t v = -1;
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == "initFillByte") fromValue(cell("settings", r, 3), v);
		return v;
	};
	CHECK(wizard.run("removeFile", {std::string("disk"), int64_t{1}}, error).has_value());
	CHECK(wizard.run("removeFile", {std::string("disk"), int64_t{0}}, error).has_value());
	CHECK(wizard.suggestion.get().empty());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && wizard.ready.get());
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nApplied below: Initial RAM Fill." && fillNow() == 9);
	/* the game's own setting comes after the package's; one by a package
	 * setting's name is not the game's to declare */
	auto settingRow = [&](const std::string &name) {
		for (int64_t r = 0; r < rowCount("settings"); r++)
			if (text("settings", r, 0) == name) return r;
		return int64_t{-1};
	};
	auto firmwareRow = [&](const std::string &id) {
		for (int64_t r = 0; r < rowCount("firmware"); r++)
			if (text("firmware", r, 0) == id) return r;
		return int64_t{-1};
	};
	CHECK(settingRow("gameLogo") == rowCount("settings") - 1 && text("settings", settingRow("gameLogo"), 1) == "Game Logo");
	CHECK(settingNames().find("initFillByte") == settingNames().rfind("initFillByte") && text("settings", settingRow("initFillByte"), 1) == "Initial RAM Fill");
	CHECK(fillNow() == 9);
	/* its own firmware, needed with its setting on; one by a package
	 * firmware's id is not the game's */
	CHECK(firmwareRow("gamelogo") < 0 && firmwareRow("bios") < 0);
	CHECK(wizard.run("set", {std::string("gameLogo"), std::string("on")}, error).has_value());
	CHECK(firmwareRow("gamelogo") >= 0 && text("firmware", firmwareRow("gamelogo"), 1) == "Game logo" && firmwareRow("bios") < 0);
	CHECK(!wizard.ready.get() && wizard.problems.get().find("Game logo") != std::string::npos);
	/* another game takes them away */
	CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
	CHECK(wizard.suggestion.get().empty() && fillNow() == 0);
	CHECK(settingRow("gameLogo") < 0 && firmwareRow("gamelogo") < 0);
	/* the same game again: its setting as it was left */
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && fillNow() == 9);
	CHECK(settingRow("gameLogo") >= 0 && firmwareRow("gamelogo") >= 0 && !wizard.ready.get());
	/* with its logo on, the game starts only with the logo given: the open
	 * asks the core again what the game needs */
	std::ofstream(dir / "mylogo.bin") << "a game's logo";
	CHECK(wizard.run("chooseFirmware", {std::string("gamelogo"), (dir / "mylogo.bin").string()}, error).has_value() && wizard.ready.get());
	/* made as Chimera makes one: opened, and written nowhere until its first
	 * save, which says where it goes */
	const fs::path boardA = dir / "boarda.chimeraProject";
	CHECK(wizard.run("create", {}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 60));
	CHECK(session.projectPath.get().empty() && session.coreName.get() == "Synth Slots");
	CHECK(wizard.status.get() == "Made a new project: it is written when it is first saved");
	/* a new project starts with the default author, as Chimera's do */
	CHECK(d.until([&] { return details.author.get() == "default user"; }, 20));
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 20));
	CHECK(!session.run("saveProject", {}, error) && error == "this project has not been saved yet: say where it goes");
	/* Save Project As proposes the game's name in the folder given (the
	 * Movies folder), and once saved the project's own file (issue 399) */
	auto suggested = [&] {
		std::string at;
		fromValue(session.run("suggestedProjectFile", {(dir / "movies").string()}, error).value_or(Value()), at);
		return at;
	};
	CHECK(suggested() == (dir / "movies" / "walker.chimeraProject").string());
	CHECK(session.run("saveProject", {boardA.string()}, error).has_value());
	CHECK(d.until([&] { return session.projectPath.get() == boardA.string(); }, 20));
	CHECK(suggested() == boardA.string());
	CHECK(std::regex_search(readFile(boardA), std::regex("\"gameLogo\"\\s*:\\s*\"on\"")));
	/* the renderer, asked apart, is recorded like any setting */
	CHECK(std::regex_search(readFile(boardA), std::regex("\"renderer\"\\s*:\\s*\"software\"")));
	CHECK(session.recentProjects.get().rfind(boardA.string(), 0) == 0);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a software renderer asks the engine for no GPU, and a hardware one for
	 * the GPU bridge, as Chimera asks (a Ruffle project failed without it) */
	CHECK(ce_gl_requested() == 0);
	const fs::path boardHw = dir / "boardhw.chimeraProject";
	std::ofstream(boardHw, std::ios::binary) << std::regex_replace(readFile(boardA), std::regex("\"renderer\"(\\s*:\\s*)\"software\""), "\"renderer\"$1\"opengl-hw\"");
	CHECK(session.run("openProject", {boardHw.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == boardHw.string(); }, 60));
	CHECK(ce_gl_requested() == 1);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	ce_gl_request(0);
	/* File > Project Settings (Chimera issue 370): the open project's settings
	 * and clock changed; the machine starts again with them at frame 0, and
	 * the project, unsaved, writes them */
	{
		SettingsViewModel projectSettings(d, session);
		CHECK(session.run("openProject", {boardA.string()}, error).has_value());
		CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == boardA.string() && projectSettings.shown.get() > 0; }, 60));
		RowModel *rows = nullptr;
		for (const auto &r : projectSettings.rowLists())
			if (r.name == "settings") rows = r.model;
		CHECK(rows != nullptr);
		auto rowOf = [&](const std::string &name) {
			for (int64_t i = 0; rows != nullptr && i < rows->rowCount(); i++)
			{
				const Value cell = rows->cell(i, SettingsViewModel::kName);
				if (const std::string *n = std::get_if<std::string>(&cell); n != nullptr && *n == name) return i;
			}
			return int64_t{-1};
		};
		/* the renderer is there; the machine, which goes with the files, is not */
		CHECK(rowOf("renderer") >= 0 && rowOf("initFillByte") >= 0 && rowOf("board") < 0);
		CHECK(!projectSettings.changed.get() && !projectSettings.canRestart.get() && projectSettings.restartNote.get() == "Nothing is changed yet.");
		/* the value it has, typed back, is no change */
		const Value fill = rows->cell(rowOf("initFillByte"), SettingsViewModel::kValue);
		CHECK(projectSettings.run("set", {std::string("initFillByte"), fill}, error).has_value() && !projectSettings.changed.get());
		CHECK(projectSettings.run("set", {std::string("initFillByte"), int64_t{3}}, error).has_value() && projectSettings.canRestart.get());
		projectSettings.clockStart.set("not a date");
		CHECK(!projectSettings.canRestart.get() && projectSettings.restartNote.get().rfind("The clock's start reads", 0) == 0);
		projectSettings.clockStart.set("2001-02-03 04:05:06");
		CHECK(projectSettings.canRestart.get());
		for (int i = 0; i < 3; i++) session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == 3; }, 20));
		CHECK(projectSettings.run("restartWith", {}, error).has_value());
		CHECK(d.until([&] { return session.isOpen.get() && session.frame.get() == 0 && !projectSettings.changed.get() &&
			projectSettings.clockStart.get() == "2001-02-03 04:05:06"; }, 60));
		CHECK(session.unsaved.get() && std::get<int64_t>(rows->cell(rowOf("initFillByte"), SettingsViewModel::kValue)) == 3);
		const fs::path boardSet = dir / "boardset.chimeraProject";
		CHECK(session.run("saveProject", {boardSet.string()}, error).has_value());
		CHECK(d.until([&] { return session.projectPath.get() == boardSet.string() && !session.unsaved.get(); }, 20));
		CHECK(std::regex_search(readFile(boardSet), std::regex("\"initFillByte\"\\s*:\\s*3")));
		CHECK(std::regex_search(readFile(boardSet), std::regex("\"clockStart\"\\s*:\\s*\"2001-02-03T04:05:06Z\"")));
		/* board A the last project opened again, for what follows */
		CHECK(session.run("openProject", {boardA.string()}, error).has_value());
		CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == boardA.string(); }, 60));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	}
	/* opened again on that project's answers, whatever the form held since:
	 * its core, machine, files and settings - its own, so the suggestion is
	 * not put over them - and its firmware found where it was chosen */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	CHECK(wizard.run("openOnLastProject", {}, error).has_value());
	CHECK(wizard.core.get() == "Synth Slots" && wizard.machine.get() == 0 && rowCount("slots") >= 1);
	CHECK(text("slots", 0, 7) == "walker.testrom" && fillNow() == 9);
	CHECK(settingRow("gameLogo") >= 0 && text("settings", settingRow("gameLogo"), 3) == "on");
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nThis project's own settings are kept.");
	CHECK(firmwareRow("gamelogo") >= 0 && text("firmware", firmwareRow("gamelogo"), 2) == (dir / "mylogo.bin").string() && wizard.ready.get());
	/* another game: what the core suggested for the project's game comes out
	 * where it still sits - not where the person moved it - and that game's
	 * own setting goes with it (Chimera issue 214) */
	const std::string nofill = (dir / "nofill.testrom").string();
	fs::copy_file(game, nofill);
	std::ofstream(nofill, std::ios::binary | std::ios::app) << "NOFILL";
	auto otherGame = [&] {
		return wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value() &&
			wizard.run("addFile", {std::string("rom"), nofill}, error).has_value();
	};
	CHECK(wizard.run("set", {std::string("initFillByte"), int64_t{5}}, error).has_value());
	CHECK(otherGame() && fillNow() == 5);
	CHECK(wizard.run("openOnLastProject", {}, error).has_value() && fillNow() == 9);
	CHECK(otherGame() && fillNow() == 0);
	CHECK(wizard.suggestion.get() == "The synthetic core suggests nothing for this game.");
	CHECK(settingRow("gameLogo") < 0 && firmwareRow("gamelogo") < 0);
	/* the project's game back is a game like any other now: its suggestion
	 * is applied, and its own setting starts from its default */
	CHECK(wizard.run("removeFile", {std::string("rom"), int64_t{0}}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && fillNow() == 9);
	CHECK(settingRow("gameLogo") >= 0 && text("settings", settingRow("gameLogo"), 3) == "off");
	/* started from a file, as a file dropped on Chimera's window: the first
	 * core that takes its kind, the file in its slot - in place of the one
	 * there when the slot holds no more */
	CHECK(wizard.run("startFrom", {game}, error).has_value() && wizard.core.get() == "Synth" && text("slots", 0, 7) == "walker.testrom");
	CHECK(wizard.run("startFrom", {nofill}, error).has_value() && wizard.core.get() == "Synth" && text("slots", 0, 7) == "nofill.testrom");
	CHECK(!wizard.run("startFrom", {(dir / "notes.txt").string()}, error) && error == "no installed core takes .txt files");
	/* a core the last project was made with opens on its answers first, but
	 * another game's files are not this one's (issue 398): only the game */
	CHECK(wizard.run("startFrom", {(dir / "disk3.dsk").string()}, error).has_value());
	CHECK(wizard.core.get() == "Synth Slots" && wizard.machine.get() == 0 && text("slots", 0, 7) == "" && text("slots", 1, 7) == "disk3.dsk");
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9. It sees 1 disk, at disk speed 1.\nApplied below: Initial RAM Fill.");
	/* one of the last project's own files is the same game again, and brings
	 * back the rest of its files: a board A project of the game and a disk,
	 * then its disk dropped */
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && wizard.ready.get());
	CHECK(wizard.run("create", {}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.hasProject.get(); }, 60));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(wizard.run("startFrom", {(dir / "disk3.dsk").string()}, error).has_value());
	CHECK(wizard.core.get() == "Synth Slots" && text("slots", 0, 7) == "walker.testrom" && text("slots", 1, 7) == "disk3.dsk");
	std::ofstream(dir / "disk4.dsk") << "another board A game";
	CHECK(wizard.run("startFrom", {(dir / "disk4.dsk").string()}, error).has_value());
	CHECK(text("slots", 0, 7) == "" && text("slots", 1, 7) == "disk4.dsk");
	/* nothing to make until everything is there */
	CHECK(wizard.run("chooseCore", {std::string("Synth")}, error).has_value());
	CHECK(!wizard.run("create", {(dir / "never.chimeraProject").string()}, error) && error == "not yet: Game: a file is needed");
	removeTree(dir);
	return true;
}

/* A firmware with a hash names one exact file. Another file is refused,
 * except by a game core, which takes a file of the person's own and pins
 * that file's hash in the project */
bool customFirmware(const char *rom, const char *hashedPackage, const char *gamePackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(hashedPackage, ec) == 0 || ec || fs::file_size(gamePackage, ec) == 0 || ec)
	{
		std::printf("SKIP custom firmware: the packages with a hashed firmware were not made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-logo-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::create_directories(dir / "Firmware");
	fs::copy_file(hashedPackage, dir / "Cores" / "synth-hashed.chimeraCore");
	fs::copy_file(gamePackage, dir / "Cores" / "synth-game.chimeraCore");
	fs::copy_file(gamePackage, dir / "Cores" / "synth-game-again.chimeraCore");
	const std::string game = (dir / "walker.testrom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel wizard(d, session, installed);
	auto text = [&](const char *rowsName, int64_t row, int32_t column) {
		std::string t;
		for (const auto &r : wizard.rowLists())
			if (r.name == rowsName) fromValue(r.model->cell(row, column), t);
		return t;
	};
	const std::string original = "CE31CFD83A86A850B5B110A3AC05B634F186C1F1";
	const fs::path mine = dir / "mylogo.bin", theirs = dir / "logo.bin";
	std::ofstream(mine, std::ios::binary) << "a logo of my own";
	std::ofstream(theirs, std::ios::binary) << "the original logo";
	char sha[41];
	uint64_t len = 0;
	CHECK(ce_sha1_file(mine.string().c_str(), sha, &len) == 1);
	const std::string mySha = sha;
	CHECK(ce_sha1_file(theirs.string().c_str(), sha, &len) == 1 && lower(sha) == lower(original));
	CHECK(wizard.coreNames.get() == "Synth Game\nSynth Hashed");
	/* game cores and emulators apart; two packages of one core told apart
	 * by the package's hash; the date in Chimera's clock */
	CHECK(std::regex_match(wizard.coreLines.get(), std::regex("Synth Hashed  [0-9a-f]{8}( local)?  -  Synthetic Witness")));
	CHECK(wizard.run("chooseKind", {std::string("games")}, error).has_value());
	CHECK(std::regex_match(wizard.coreLines.get(), std::regex("(Synth Game  2026-10-18 18:35 CEST  \\([0-9a-f]{8}( local)?\\)  \\[package [0-9A-F]{8}\\]  -  Synthetic Witness\n?){2}")));
	CHECK(wizard.run("chooseCoreLine", {int64_t{1}}, error).has_value() && wizard.core.get() == "Synth Game" && wizard.coreLine.get() == 1);
	/* a core chosen by name shows its kind */
	CHECK(wizard.run("chooseCore", {std::string("Synth Hashed")}, error).has_value() && wizard.kind.get() == "emulators" && wizard.coreLine.get() == 0);
	CHECK(wizard.run("chooseCore", {std::string("Synth Game")}, error).has_value() && wizard.kind.get() == "games" && wizard.coreLine.get() >= 0);
	CHECK(wizard.systems.get() == "Synthetic Witness");

	/* not a game: only the file the package names */
	CHECK(wizard.run("chooseCore", {std::string("Synth Hashed")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && !wizard.ready.get());
	CHECK(!wizard.run("chooseFirmware", {std::string("logo"), mine.string()}, error) &&
		error == "mylogo.bin is not this file: its hash is " + mySha + ", the requirement is " + original);
	CHECK(!wizard.ready.get());
	CHECK(wizard.run("chooseFirmware", {std::string("logo"), theirs.string()}, error).has_value());
	CHECK(wizard.ready.get() && text("firmware", 0, 3) == "chosen" && wizard.status.get().empty());

	/* a game: a file of the person's own, its hash pinned */
	CHECK(wizard.run("chooseCore", {std::string("Synth Game")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && !wizard.ready.get());
	CHECK(wizard.run("chooseFirmware", {std::string("logo"), mine.string()}, error).has_value());
	CHECK(wizard.status.get() == "mylogo.bin is a file of your own, not the original logo: the project pins its hash (" + mySha + ")");
	CHECK(wizard.ready.get() && text("firmware", 0, 2) == mine.string() && text("firmware", 0, 3) == "your own");
	const fs::path made = dir / "mylogo.chimeraProject";
	CHECK(wizard.run("create", {made.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == made.string(); }, 60));
	{
		const std::string project = readFile(made);
		CHECK(project.find(mySha) != std::string::npos || project.find(lower(mySha)) != std::string::npos);
		CHECK(project.find(original) == std::string::npos && project.find(lower(original)) == std::string::npos);
	}
	/* Config > Firmware: each installed core's declared firmware, and what is on hand */
	{
		FirmwareViewModel survey(d, installed);
		std::string e;
		CHECK(survey.run("refresh", {}, e).has_value());
		RowModel &lines = *survey.rowLists()[0].model;
		auto lineOfCore = [&](const std::string &core) {
			for (int64_t r = 0; r < lines.rowCount(); r++)
			{
				std::string c, kind;
				fromValue(lines.cell(r, 0), c);
				fromValue(lines.cell(r, 2), kind);
				if (c == core && kind == "row") return r;
			}
			return int64_t{-1};
		};
		auto logoItem = [&]() -> const FirmwareViewModel::Row * {
			for (const auto &r : survey.items())
				if (r.core == "Synth Hashed" && r.id == "logo") return &r;
			return nullptr;
		};
		const int64_t logoLine = lineOfCore("Synth Hashed");
		CHECK(logoLine >= 0 && logoItem() != nullptr);
		if (logoLine >= 0 && logoItem() != nullptr)
		{
			/* the game core's file of the person's own: on hand, a dump the core has never seen */
			bool gameLogo = false;
			for (const auto &r : survey.items())
				gameLogo = gameLogo || (r.core == "Synth Game" && r.state == FirmwareViewModel::Unrecognised && r.path == mine.string());
			CHECK(gameLogo);
			/* the emulator's: a file whose hash is another is not a dump it knows; the original is; gone is said */
			CHECK(survey.run("setFile", {logoLine, mine.string()}, e).has_value() && logoItem()->state == FirmwareViewModel::Unrecognised);
			CHECK(survey.status.get() == "mylogo.bin: right size, a dump the core has never seen - used anyway");
			CHECK(survey.run("setFile", {logoLine, theirs.string()}, e).has_value() && logoItem()->state == FirmwareViewModel::Good);
			const fs::path moved = dir / "moved.bin";
			fs::copy_file(theirs, moved);
			CHECK(survey.run("setFile", {logoLine, moved.string()}, e).has_value());
			fs::remove(moved);
			CHECK(survey.run("refresh", {}, e).has_value() && logoItem()->state == FirmwareViewModel::Unreadable);
			/* cleared: looked for in the firmware folder, where it is not */
			CHECK(survey.run("clearFile", {logoLine}, e).has_value() && logoItem()->state == FirmwareViewModel::Missing && survey.missing.get() >= 1);
			/* a folder scanned: the original found by its hash, under any name, for every core that declares it */
			fs::create_directories(dir / "dumps" / "deep");
			fs::copy_file(theirs, dir / "dumps" / "deep" / "renamed.rom");
			CHECK(survey.run("scanFolder", {(dir / "dumps").string(), false}, e) == Value(int64_t{0}));
			CHECK(survey.run("scanFolder", {(dir / "dumps").string(), true}, e) == Value(int64_t{2}));
			CHECK(logoItem()->state == FirmwareViewModel::Good && logoItem()->path == (dir / "dumps" / "deep" / "renamed.rom").string());
			CHECK(!survey.run("setFile", {int64_t{99}, theirs.string()}, e) && e == "no line 99");
			/* put back as the test had it: nothing chosen for the emulator, nor the original for the game */
			installed.rememberFirmware("Synth Hashed", "logo", "");
			installed.rememberFirmware("Synth Hashed", "logo#" + original, "");
			installed.rememberFirmware("Synth Game", "logo#" + original, "");
		}
	}
	/* where it was chosen is remembered: that is how the project found it */
	CHECK(installed.chosenFirmware.get().find("\"Synth Game\"") != std::string::npos &&
		installed.chosenFirmware.get().find("mylogo.bin") != std::string::npos);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* firmware not found at an open is asked for in Locate Project Files,
	 * as the project's files are (issue 397): a file of another hash is
	 * refused, the right one is taken and remembered for the core; a folder
	 * scanned finds it too */
	session.setFirmwareKeeper([&installed](const std::string &core, const std::string &id, const std::string &path) {
		installed.rememberFirmware(core, id, path);
	});
	const fs::path away = dir / "elsewhere" / "given.bin";
	fs::create_directories(away.parent_path());
	fs::rename(mine, away);
	installed.rememberFirmware("Synth Game", "logo", "");
	{
		RowModel *files = nullptr;
		for (const auto &r : session.rowLists())
			if (r.name == "projectFiles") files = r.model;
		CHECK(files != nullptr);
		if (files == nullptr) return false;
		auto cell = [&](int64_t row, int32_t column) {
			std::string t;
			fromValue(files->cell(row, column), t);
			return t;
		};
		const int64_t opensBefore = session.opens.get();
		session.run("openProject", {made.string()}, error);
		CHECK(d.until([&] { return session.resolvingFiles.get() && files->rowCount() == 2; }, 60) && !session.isOpen.get());
		CHECK(session.status.get() == "The firmware this project needs was not found: locate it, or scan a folder");
		CHECK(cell(0, 3) == "found, hash matches");
		CHECK(cell(1, 1) == "firmware" && cell(1, 3) == "not found" && lower(cell(1, 4)) == lower(mySha));
		CHECK(!session.run("openWithFiles", {}, error) && error == "a file is still not found");
		CHECK(!session.run("locateProjectFile", {int64_t{1}, theirs.string()}, error) &&
			lower(error) == lower("logo.bin is not the firmware the project needs: its hash is " + original + ", the project needs " + mySha));
		CHECK(session.run("locateProjectFile", {int64_t{1}, away.string()}, error).has_value() && error.empty());
		CHECK(d.until([&] { return cell(1, 3) == "found, hash matches"; }, 20));
		CHECK(installed.chosenFirmwareFor("Synth Game", "logo") == away.string());
		CHECK(session.run("openWithFiles", {}, error).has_value());
		CHECK(d.until([&] { return session.opens.get() > opensBefore && session.isOpen.get(); }, 60));
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		/* remembered: the next open asks nothing */
		const int64_t opensThen = session.opens.get();
		session.run("openProject", {made.string()}, error);
		CHECK(d.until([&] { return session.opens.get() > opensThen && session.isOpen.get(); }, 60) && !session.resolvingFiles.get());
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		/* forgotten, and found by a folder scanned */
		installed.rememberFirmware("Synth Game", "logo", "");
		session.run("openProject", {made.string()}, error);
		CHECK(d.until([&] { return session.resolvingFiles.get() && files->rowCount() == 2; }, 60));
		CHECK(session.run("scanForProjectFiles", {(dir / "elsewhere").string(), true}, error) == Value(int64_t{1}));
		CHECK(d.until([&] { return cell(1, 3) == "found, hash matches"; }, 20));
		CHECK(session.run("cancelOpen", {}, error).has_value());
		CHECK(d.until([&] { return !session.resolvingFiles.get() && session.status.get() == "The project was not opened"; }, 20));
		installed.rememberFirmware("Synth Game", "logo", "");
	}
	/* Chimera's --firmware: a file given for this run, found nowhere else, is
	 * used where its hash is the one the project pins (review, 2026-10-26) */
	{
		const int64_t opensBefore = session.opens.get();
		session.setRunFirmware({{"logo", away.string()}});
		session.run("openProject", {made.string()}, error);
		CHECK(d.until([&] { return session.opens.get() > opensBefore && session.isOpen.get(); }, 60));
		session.setRunFirmware({});
		session.run("close", {}, error);
		CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
		fs::rename(away, mine);
	}
	/* a folder scanned finds a file by the hash the package gives, whatever
	 * it is called */
	CHECK(wizard.run("chooseCore", {std::string("Synth Hashed")}, error).has_value());
	CHECK(wizard.run("addFile", {std::string("rom"), game}, error).has_value() && !wizard.ready.get());
	fs::create_directories(dir / "scan" / "deeper");
	fs::copy_file(theirs, dir / "scan" / "deeper" / "renamed.bin");
	std::ofstream(dir / "scan" / "logo.bin") << "a logo by the right name and another hash";
	CHECK(wizard.run("scanFolder", {(dir / "scan").string()}, error).has_value());
	CHECK(wizard.status.get() == "scanned 2 files; 1 requirement resolved" && wizard.ready.get());
	CHECK(text("firmware", 0, 2) == (dir / "scan" / "deeper" / "renamed.bin").string());
	removeTree(dir);
	return true;
}

/* A movie made elsewhere: the core says what it amounts to, the New Project
 * form takes its answer, and the project made has the movie's inputs */
bool movieImport(const char *slotsPackage, const char *rom, const std::string &movies)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(slotsPackage, ec) == 0 || ec || !fs::exists(movies + "/gridWalker.win.txt"))
	{
		std::printf("SKIP movie import: no slots package or no movie\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-import-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::copy_file(slotsPackage, dir / "Cores" / "synth-slots.chimeraCore");
	fs::copy_file(rom, dir / "gridWalker.testrom");
	fs::copy_file(movies + "/gridWalker.win.txt", dir / "win.txt");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", (dir / "Cores").string(), error);
	installed.set("firmwareFolder", (dir / "Firmware").string(), error);
	session.setFinders([&installed](const std::string &n, const std::string &h, std::string &note) { return installed.packageFor(n, h, note); },
		[&installed] { return installed.firmwareFolders(); });
	NewProjectViewModel wizard(d, session, installed);
	CHECK(wizard.run("chooseCore", {std::string("Synth Slots")}, error).has_value());
	CHECK(wizard.importMenu.get() == "Import Synth Movie..." && wizard.importMovieFilter.get() == "*.txt" && wizard.importMovieLabel.get() == "A synthetic movie");
	RowModel *files = nullptr;
	for (const auto &r : wizard.rowLists())
		if (r.name == "importFiles") files = r.model;
	CHECK(files != nullptr && files->rowCount() == 1);
	std::string t;
	fromValue(files->cell(0, 0), t);
	CHECK(t == "importRom");
	fromValue(files->cell(0, 2), t);
	CHECK(t == "*.testrom");
	/* what is missing, in Chimera's words */
	CHECK(!wizard.run("importRun", {}, error) && error == "Choose the a synthetic movie to import." && wizard.importFailure.get() == error);
	CHECK(wizard.run("importSetMovie", {(dir / "win.txt").string()}, error).has_value());
	CHECK(!wizard.run("importRun", {}, error) && error == "Choose the The game.");
	CHECK(wizard.run("importAddFile", {std::string("importRom"), (dir / "gridWalker.testrom").string()}, error).has_value());
	/* a single input gives its file up for another of the same name, as Chimera's */
	{
		fs::create_directories(dir / "again");
		fs::copy_file(dir / "gridWalker.testrom", dir / "again" / "GRIDWALKER.testrom", fs::copy_options::overwrite_existing);
		CHECK(wizard.run("importAddFile", {std::string("importRom"), (dir / "again" / "GRIDWALKER.testrom").string()}, error).has_value());
		CHECK(wizard.run("importAddFile", {std::string("importRom"), (dir / "gridWalker.testrom").string()}, error).has_value());
	}
	fromValue(files->cell(0, 5), t);
	CHECK(t == "gridWalker.testrom");
	wizard.run("importSetOption", {std::string("importFill"), true}, error);
	/* the form's value before: the movie's answer replaces it */
	CHECK(wizard.run("set", {std::string("initFillByte"), int64_t{5}}, error).has_value());
	CHECK(wizard.run("importRun", {}, error).has_value());
	/* the answer in the form: its file in the slot, its setting, its notes */
	int64_t frames = 0;
	{
		std::istringstream lines(readFile(dir / "win.txt"));
		for (std::string l; std::getline(lines, l);) frames += !l.empty() && l[0] == '|' ? 1 : 0;
	}
	CHECK(wizard.status.get() == "Imported win.txt (" + std::to_string(frames) + " frames)");
	/* taken: Chimera's message of what the core noted */
	CHECK(wizard.importsTaken.get() == 1 && wizard.importFailure.get().empty());
	std::printf("import summary: %s\n", wizard.importSummary.get().c_str());
	CHECK(std::regex_match(wizard.importSummary.get(), std::regex("Synth Slots read a [^\n]* movie of " + std::to_string(frames) +
		" frames, and notes:\n\n- imported by the synth core")));
	CHECK(wizard.imported.get() && wizard.importNotes.get() == "imported by the synth core" && wizard.title.get() == "win");
	/* the movie's settings are the project's own: the core's note is shown,
	 * and what it would set is not put over them */
	CHECK(wizard.suggestion.get() == "The synthetic core suggests a fill of 9.\nThis project's own settings are kept.");
	RowModel *slots = nullptr, *settings = nullptr;
	for (const auto &r : wizard.rowLists())
	{
		if (r.name == "slots") slots = r.model;
		if (r.name == "settings") settings = r.model;
	}
	fromValue(slots->cell(0, 7), t);
	CHECK(t == "gridWalker.testrom" && wizard.ready.get());
	int64_t fill = -1;
	for (int64_t r = 0; r < settings->rowCount(); r++)
	{
		std::string n;
		fromValue(settings->cell(r, 0), n);
		if (n == "initFillByte") fromValue(settings->cell(r, 3), fill);
	}
	/* what the synthetic importer dictates for a ticked option (sent as true,
	 * as Chimera sends it, which it reads as no number) */
	CHECK(fill == 0);
	/* made: the project has the movie's inputs, and opens on them */
	const fs::path project = dir / "win.chimeraProject";
	CHECK(wizard.run("create", {project.string()}, error).has_value());
	CHECK(d.until([&] { return session.isOpen.get() && session.projectPath.get() == project.string(); }, 60));
	CHECK(inputs.size() == frames);
	/* forgotten: the next project starts empty */
	wizard.run("importForget", {}, error);
	CHECK(!wizard.imported.get());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	removeTree(dir);
	return true;
}

/* The cores that take a kind of file (New Project's start from a dropped
 * file asks it); a game is never opened by itself, only in a project (owner,
 * 2026-10-26) */
bool openByKind(const char *package, const char *rom, const char *again)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(again, ec) == 0 || ec)
	{
		std::printf("SKIP choosing between cores: no second package was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-kind-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	const fs::path cores = dir / "Cores";
	fs::create_directories(cores);
	fs::copy_file(package, cores / "synth-box.chimeraCore");
	/* the case of the name does not matter */
	const std::string game = (dir / "Walker.TestRom").string();
	fs::copy_file(rom, game);
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	CoresViewModel installed(d);
	std::string error;
	installed.set("coresFolder", cores.string(), error);
	const auto takes = installed.coresFor("testrom");
	CHECK(takes.size() == 1 && takes[0].first == "Synth" && installed.coresFor("md").empty());
	CHECK(installed.rowLists()[0].model->columnName(3) == "takes");
	std::string column;
	fromValue(installed.rowLists()[0].model->cell(0, 3), column);
	CHECK(column == ".testrom");
	/* two do: both are listed, the first for New Project's start */
	fs::copy_file(again, cores / "synth-again.chimeraCore");
	installed.run("refresh", {}, error);
	const auto both = installed.coresFor("testrom");
	CHECK(both.size() == 2 && both[0].first == "Synth" && both[1].first == "Synth Again");
	/* and the session has nothing that opens a game alone */
	for (const char *gone : {"open", "openGame", "chooseCore", "saveAsProject"})
	{
		error.clear();
		CHECK(!session.run(gone, {game}, error) && error == std::string("session has no command ") + gone);
	}
	CHECK(!session.isOpen.get());
	removeTree(dir);
	return true;
}

} // namespace

/* A crash note, as the crash module writes one (docs/project.md, "Crash
 * notes"): what failed, where and in whose code; the note of the process
 * that left recovered work, not an earlier one given the same id; only the
 * newest kept, their dumps with them. Arming is Windows' alone. */
void crashNotes()
{
	namespace fs = std::filesystem;
	const std::string driverFastFail = "Chimera crash note\n"
		"time=2026-09-13 13:38:03\n"
		"process=4242\n"
		"code=0xc0000409\n"
		"kind=fast fail (the process ended itself; no handler runs)\n"
		"fastfail=7 FATAL_APP_EXIT\n"
		"address=0x7ffb1108eb9d\n"
		"module=nvoglv64.dll\n"
		"offset=0x108eb9d\n"
		"fatal=1\n"
		"frame=21\n"
		"\n"
		"session:\n"
		"  project=C:\\TAS\\nss102.chimeraProject\n"
		"  system=Flash\n"
		"\n"
		"stack (faulting thread):\n"
		"  #00 nvoglv64.dll+0x108eb9d\n"
		"  #01 KERNEL32.DLL+0x1259d  BaseThreadInitThunk+0x1d\n"
		"\n"
		"dump=written\n";
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-crash-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	auto write = [&](const std::string &stem, const std::string &text, bool dump) {
		const fs::path note = dir / (stem + ".txt");
		std::ofstream(note, std::ios::binary) << text;
		if (dump) std::ofstream(dir / (stem + ".dmp"), std::ios::binary) << "MDMP";
		return note.string();
	};
	const std::string path = write("2026-09-13 13.38.03 pid4242", driverFastFail, true);
	crash::Note note;
	CHECK(crash::parse(driverFastFail, path, note));
	CHECK(note.processId == 4242 && note.time == "2026-09-13 13:38:03");
	CHECK(note.summary() == "fast fail in nvoglv64.dll+0x108eb9d (the NVIDIA graphics driver) at frame 21");
	CHECK(note.session.find("project=C:\\TAS\\nss102.chimeraProject") != std::string::npos);
	CHECK(note.stack.rfind("#00 nvoglv64.dll+0x108eb9d", 0) == 0);
	/* a field after the stack is still a field */
	CHECK(note.field("dump") == "written" && note.dump == (dir / "2026-09-13 13.38.03 pid4242.dmp").string());
	/* no frame, a module nobody is named for, CR LF, no dump */
	CHECK(crash::parse("Chimera crash note\r\nprocess=7\r\ncode=0xc0000005\r\nkind=access violation\r\nmodule=chimera.dll\r\noffset=0x10\r\nframe=0\r\n",
			  "x.txt", note) &&
		note.summary() == "access violation in chimera.dll+0x10" && note.time.empty() && note.dump.empty());
	CHECK(!crash::parse("time=2026-09-13 13:38:03\nprocess=1\n", "x.txt", note) && !crash::parse("", "x.txt", note));
	/* the process's own note, not one an earlier process with the same id left */
	CHECK(crash::noteFor(4242, "2026-09-13 13:30:00", dir.string()).has_value());
	CHECK(!crash::noteFor(4242, "2026-09-13 13:40:00", dir.string()).has_value());
	CHECK(!crash::noteFor(4243, "2026-09-13 13:30:00", dir.string()).has_value());
	CHECK(!crash::noteFor(4242, "2026-09-13 13:30:00", (dir / "none").string()).has_value());
	CHECK(!crash::noteFor(4242, "", dir.string()).has_value());
	/* the newest kept, their dumps with them; what is not a note left alone */
	for (int day = 10; day <= 14; day++)
		write("2026-09-" + std::to_string(day) + " 12.00.00 pid" + std::to_string(day), driverFastFail, true);
	std::ofstream(dir / "readme.md") << "not a note";
	CHECK(crash::notesIn(dir.string()).size() == 6 && crash::notesIn(dir.string()).front().path == (dir / "2026-09-14 12.00.00 pid14.txt").string());
	crash::prune(dir.string(), 2);
	std::vector<std::string> left;
	for (const auto &e : fs::directory_iterator(dir)) left.push_back(e.path().filename().string());
	std::sort(left.begin(), left.end());
	CHECK((left == std::vector<std::string>{"2026-09-13 13.38.03 pid4242.dmp", "2026-09-13 13.38.03 pid4242.txt",
		"2026-09-14 12.00.00 pid14.dmp", "2026-09-14 12.00.00 pid14.txt", "readme.md"}));
	CHECK(crash::whoseModule("atio6axx.dll").empty() && crash::whoseModule("amdxx64.dll") == "the AMD graphics driver" &&
		crash::whoseModule("ig9icd64.dll") == "the Intel graphics driver" && crash::whoseModule("opengl32.dll") == "the graphics system");
#ifndef _WIN32
	/* arming is Windows' alone: elsewhere it says why, and the rest does nothing */
	CHECK(crash::arm(dir.string(), dir.string()) == "crash notes are written by Windows Error Reporting" && !crash::armed());
	crash::noteFrame(5);
	crash::describeSession("no project open");
#endif
	removeTree(dir);
}

/* Chimera's clock for a core's date (chimera issue 331) */
void europeanClock()
{
	auto at = [](const char *iso) {
		int64_t t = 0;
		return parseIsoTime(iso, t) ? europeanTime(t) : std::string("unread");
	};
	CHECK(at("2026-10-18T16:35:00Z") == "2026-10-18 18:35 CEST");
	CHECK(at("2026-01-15T12:00:00Z") == "2026-01-15 13:00 CET");
	CHECK(at("2026-03-29T00:59:00Z") == "2026-03-29 01:59 CET" && at("2026-03-29T01:00:00Z") == "2026-03-29 03:00 CEST");
	CHECK(at("2026-10-25T00:59:00Z") == "2026-10-25 02:59 CEST" && at("2026-10-25T01:00:00Z") == "2026-10-25 02:00 CET");
	CHECK(at("2026-11-30T23:30:00Z") == "2026-12-01 00:30 CET");
	CHECK(at("2026-10-18T12:35:00-04:00") == "2026-10-18 18:35 CEST" && at("2026-10-18 16:35") == "2026-10-18 18:35 CEST");
	CHECK(at("2026-10-18T16:35:00.123Z") == "2026-10-18 18:35 CEST" && at("yesterday") == "unread" && at("2026-13-01T00:00Z") == "unread");
}

/* Chimera's throttles: the clock keeps the machine to its rate; with none
 * it goes as fast as it can; the vsync throttle moves it a frame for each
 * picture the window shows, and not at all without them */
bool throttles(const char *package, const char *rom)
{
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	std::string error;
	openGame(session, package, std::string(rom), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	CHECK(session.throttle.get() == "clock");
	session.run("play", {}, error);
	auto ranIn = [&](double seconds) {
		const int64_t was = session.frame.get();
		d.until([] { return false; }, seconds);
		return session.frame.get() - was;
	};
	const int64_t clock = ranIn(0.5);
	session.throttle.set("");
	CHECK(session.status.get() == "Throttled:(none)");
	const int64_t none = ranIn(0.5);
	CHECK(clock >= 20 && clock <= 40 && none > clock * 3);
	session.throttle.set("vsync");
	ranIn(0.1);
	CHECK(ranIn(0.3) <= 1);
	const int64_t was = session.frame.get();
	for (int i = 0; i < 10; i++)
	{
		session.vsyncTick();
		d.until([] { return false; }, 0.03);
	}
	d.until([&] { return session.frame.get() - was >= 10; }, 2);
	CHECK(session.frame.get() - was >= 9 && session.frame.get() - was <= 11);
	/* the audio throttle: the sound as it is heard, whatever speed the clock was asked for
	 * (OpenAL's device that plays to nowhere still plays in time) */
	session.speedPercent.set(400);
	session.throttle.set("audio");
	ranIn(0.3);
	const int64_t heard = ranIn(0.5);
	session.throttle.set("clock");
	ranIn(0.1);
	const int64_t fast = ranIn(0.5);
	session.speedPercent.set(100);
	CHECK(heard >= 20 && heard <= 40 && fast > heard * 2);
	/* nothing else: a throttle by any other name is the clock */
	session.throttle.set("sometimes");
	CHECK(session.throttle.get() == "clock");
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return true;
}

/* Lua's tastudio library on a project: queued edits applied together,
 * markers by text and the run's own among them, a branch's text and input,
 * the lag log, and what is not here said so */
/* The Lua functions Chimera has that the window lacked: bizstring
 * (UTF-16 positions, encodings by their usual names), bit's 64-bit
 * swap, the lag counter and flag set, the movie's header, comments,
 * subtitles, lines and rerecord counting, the game's database status, and
 * the client's window and tool calls */
bool luaParity(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-lua-parity-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	ProjectDetailsViewModel details(d, session);
	DisplayViewModel display(d);
	ToolWindowsViewModel tools(d);
	GuiOverlayViewModel overlay(d);
	FormsViewModel forms(d);
	PathsViewModel paths(d);
	RegistersViewModel debugger(d, session);
	MessagesViewModel messages(d);
	paths.programFolder.set(dir.string());
	LuaBridge lua;
	LuaConsoleViewModel console(d, lua);
	for (ViewModel *vm : std::initializer_list<ViewModel *>{&session, &roll, &details, &display, &tools, &overlay, &forms, &paths, &debugger,
			 &console, &messages})
		lua.add(*vm);
	std::string shot = "(none)";
	tools.screenshot = [&shot](const std::string &path) { shot = path; };
	tools.windowPosition = [] { return std::pair<int64_t, int64_t>{-7, 42}; };
	int resized = 0;
	tools.resizeWindow = [&resized] { resized++; };
	std::string askedName = "(none)", askedFolder;
	std::vector<std::string> askedFilters;
	forms.openFile = [&](const std::string &name, const std::string &folder, const std::vector<std::string> &filters) {
		askedName = name;
		askedFolder = folder;
		askedFilters = filters;
		return std::string("picked.lua");
	};
	/* forms.openfile: a script's filter text as the dialog's filters */
	CHECK((FormsViewModel::nameFilters("Images (*.bmp;*.png)|*.bmp;*.png|All Files (*.*)|*.*") ==
		std::vector<std::string>{"Images (*.bmp *.png)", "All Files (*)"}));
	CHECK((FormsViewModel::nameFilters("Lua scripts|*.lua") == std::vector<std::string>{"Lua scripts (*.lua)"}));
	CHECK((FormsViewModel::nameFilters("") == std::vector<std::string>{"All Files (*)"}));
	std::string results, error;
	auto lua1 = [&](const std::string &code) {
		results.clear();
		if (!console.runForAgent(code, results, error)) return "error: " + error;
		return results;
	};
	/* forms.openfile's defaults, as Chimera's: the Lua folder, every file;
	 * the folder of the file named; and what was picked comes back */
	CHECK(lua1("forms.openfile()") == "[\"picked.lua\"]");
	CHECK(askedName.empty() && askedFolder == paths.resolve("Lua") && !askedFolder.empty());
	CHECK((askedFilters == std::vector<std::string>{"All Files (*)"}));
	lua1("forms.openfile('/some/where/a.lua', nil, 'Lua|*.lua')");
	CHECK(askedName == "/some/where/a.lua" && askedFolder == "/some/where");
	CHECK((askedFilters == std::vector<std::string>{"Lua (*.lua)"}));
	lua1("forms.openfile('a.lua', '/there')");
	CHECK(askedName == "a.lua" && askedFolder == "/there");
	lua1("forms.openfile('a.lua')");
	CHECK(askedFolder.empty());
	forms.openFile = nullptr;
	CHECK(lua1("forms.openfile()") == "[\"\"]");
	/* bizstring, before any machine */
	CHECK(lua1("bizstring.hex(10), bizstring.hex(255), bizstring.hex(-1)") == "[\"0A\", \"FF\", \"FFFFFFFFFFFFFFFF\"]");
	CHECK(lua1("bizstring.binary(5), bizstring.binary(0), bizstring.octal(8), bizstring.octal(7)") == "[\"101\", \"\", \"10\", \"07\"]");
	CHECK(lua1("bizstring.pad_start('7', 3, '0'), bizstring.pad_end('ab', 4, ''), bizstring.trim('  x y \\t'), bizstring.trim('')") ==
		"[\"007\", \"ab  \", \"x y\", null]");
	CHECK(lua1("bizstring.replace('a.b.c', '.', '-'), bizstring.toupper('abc\u{e9}'), bizstring.tolower('\u{c0}B')") ==
		"[\"a-b-c\", \"ABC\u00C9\", \"\u00E0b\"]" || lua1("bizstring.toupper('abc\u{e9}')") == "[\"ABC\xC3\x89\"]");
	/* positions in UTF-16 units: こんにちは is five */
	CHECK(lua1("bizstring.substring('\u{3053}\u{3093}\u{306B}\u{3061}\u{306F}', 1, 2), bizstring.remove('hello', 1, 3)") ==
		"[\"\xE3\x82\x93\xE3\x81\xAB\", \"ho\"]");
	CHECK(lua1("bizstring.contains('hello', 'ell'), bizstring.startswith('hello', 'he'), bizstring.endswith('hello', 'lo'), bizstring.endswith('', '')") ==
		"[true, true, true, false]");
	CHECK(lua1("bizstring.split('a,,b,c', ','), bizstring.split('a b', '  '), bizstring.fold('abcdefg', 3, '|')") ==
		"[[\"a\", \"b\", \"c\"], [\"a b\"], \"abc|def|g\"]");
	CHECK(lua1("bizstring.encode('\u{e9}'), bizstring.encode('A', 'utf-16'), bizstring.encode('\u{3042}', 'shift_jis')") == "[[195, 169], [65, 0], [130, 160]]");
	CHECK(lua1("bizstring.decode({0x82, 0xA0}, 'shift_jis') == '\u{3042}', bizstring.decode({104, 105})") == "[true, \"hi\"]");
	CHECK(lua1("bizstring.substring('abc', 2, 5)").rfind("error: ", 0) == 0);
	CHECK(lua1("bizstring.encode('x', 'no-such-encoding')").find("not a supported encoding") != std::string::npos);
	CHECK(lua1("bit.byteswap_64(0x0102030405060708) == 0x0807060504030201") == "[true]");
	/* code ending in a bracket (the closing brackets around it once closed early) */
	CHECK(lua1("({5, 6})[2]") == "[6]");
	/* the client's: the engine's name, the functions, the window */
	CHECK(lua1("client.get_lua_engine(), client.gettargetscanlineintensity()") == "[\"Lua\", 0]");
	CHECK(lua1("string.find(client.getluafunctionslist(), '\\ngetluafunctionslist\\n') ~= nil") == "[true]");
	CHECK(lua1("client.xpos(), client.ypos()") == "[-7, 42]");
	/* the tools the machine open can use, open or not, as Chimera's: with
	 * nothing open, those that need no machine; gettool opens one and gives
	 * its object, any case, and nil for a tool that cannot be used now or a
	 * name that is no tool */
	CHECK(lua1("client.getavailabletools()[0], client.getavailabletools()[1], client.getavailabletools()[2]") == "[\"luaconsole\", \"logwindow\", null]");
	CHECK(lua1("client.gettool('RamWatch') == nil, client.gettool('nosuchtool') == nil, client.gettool('LuaConsole') == chimera.luaConsole") == "[true, true, true]");
	CHECK(tools.showAsked.get() == "lua");
	CHECK(lua1("type(client.gettool('logwindow'))") == "[\"table\"]" && tools.showAsked.get() == "logWindow");
	/* client.getconfig: Chimera's names, read from the window's settings as
	 * they are when read; a choice by its name, a colour as Chimera's number;
	 * the places over the picture; read-only; nil for what is not here */
	session.speedPercent.set(100);
	session.throttle.set("clock");
	CHECK(lua1("local c = client.getconfig() return c.SpeedPercent, c.ClockThrottle, c.VSyncThrottle, c.DispManagerAR, c.MessagesColor, c.AlertMessageColor") ==
		"[100, true, false, \"System\", -1, -65536]");
	CHECK(lua1("cfg = client.getconfig()") == "[]");
	session.speedPercent.set(150);
	session.throttle.set("vsync");
	display.aspectMode.set(3);
	CHECK(lua1("cfg.SpeedPercent, cfg.ClockThrottle, cfg.VSyncThrottle, cfg.DispManagerAR, cfg.Movies.GreenzoneBudgetMb") == "[150, false, true, \"CustomRatio\", 4096]");
	messages.positions.set("{\"Fps\": {\"x\": 5, \"y\": 7, \"anchor\": 3}}");
	CHECK(lua1("cfg.Fps.X, cfg.Fps.Y, cfg.Fps.Anchor") == "[5, 7, \"BottomRight\"]");
	display.integerScale.set(true);
	CHECK(lua1("cfg.DispFixScaleInteger, cfg.DispFullscreenHacks, cfg.JmdCompression == nil, cfg.Theme == nil") == "[true, false, true, true]");
	CHECK(lua1("cfg.SpeedPercent = 5").find("SpeedPercent can be read here, not changed") != std::string::npos && session.speedPercent.get() == 150);
	CHECK(lua1("local n = 0 for k, v in pairs(cfg) do if k == 'SpeedPercent' and v == 150 then n = n + 1 end end return n") == "[1]");
	CHECK(lua1("client.createinstance == nil") == "[true]");
	session.speedPercent.set(100);
	session.throttle.set("clock");
	display.aspectMode.set(1);
	display.integerScale.set(false);
	CHECK(lua1("client.screenshot('x.png') client.screenshot()") == "[]" && shot.empty());
	CHECK(lua1("client.bufferwidth(), client.bufferheight()") == "[256, 192]");
	/* gui's two surfaces, as Chimera's: named last, else use_surface's; cleared one at a time */
	{
		auto count = [](const std::string &text, const std::string &what) {
			size_t n = 0;
			for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1)) n++;
			return n;
		};
		CHECK(lua1("gui.drawBox(1, 2, 3, 4, nil, nil, 'client') gui.use_surface('native') gui.drawPixel(5, 6) "
			"gui.use_surface('emu') gui.drawLine(0, 0, 1, 1)") == "[]");
		CHECK(count(overlay.shapes.get(), "\"surface\":\"client\"") == 2 && count(overlay.shapes.get(), "\"k\":\"line\"") == 1);
		CHECK(lua1("gui.clearGraphics('client')") == "[]");
		CHECK(count(overlay.shapes.get(), "client") == 0 && count(overlay.shapes.get(), "\"k\":\"line\"") == 1);
		CHECK(lua1("gui.drawPixel(1, 1, nil, 'screen')").find("screen is not the name of a display surface") != std::string::npos);
		CHECK(lua1("gui.DrawNew('client') gui.drawPixel(7, 7) gui.DrawNew('emucore', false) gui.drawPixel(8, 8)") == "[]");
		CHECK(count(overlay.shapes.get(), "\"surface\":\"client\"") == 1 && count(overlay.shapes.get(), "\"k\":\"line\"") == 1);
		console.takeOutputForAgent();
		CHECK(lua1("gui.use_surface()") == "[]" && console.takeOutputForAgent().find("Surface name cannot be nil") != std::string::npos);
		CHECK(lua1("gui.clearGraphics() gui.clearGraphics('client')") == "[]" && overlay.shapes.get() == "[]");
	}
	/* gui.createcanvas: a form of its own, "LuaCanvas", with one picture box the size asked for */
	CHECK(lua1("canvas = gui.createcanvas(120, 60) canvas.Clear(0xFF102030) canvas.DrawText(5, 6, 'hi', nil, nil, 10, 'Arial', 'bold', 'center', 'bottom') "
		"canvas.Refresh() return canvas.GetMouseX(), canvas.GetMouseY()") == "[0, 0]");
	{
		RowModel &formRows = *forms.rowLists()[0].model;
		RowModel &controls = *forms.rowLists()[1].model;
		std::string title, kind, shapes;
		int64_t w = 0, h = 0;
		fromValue(formRows.cell(formRows.rowCount() - 1, 3), title);
		fromValue(controls.cell(controls.rowCount() - 1, 2), kind);
		fromValue(controls.cell(controls.rowCount() - 1, 6), w);
		fromValue(controls.cell(controls.rowCount() - 1, 7), h);
		fromValue(controls.cell(controls.rowCount() - 1, 15), shapes);
		CHECK(title == "LuaCanvas" && kind == "picturebox" && w == 120 && h == 60);
		/* its text as Chimera's picture box writes it: black, on the half-black box, the font and alignment named */
		CHECK(shapes.find("\"text\":\"hi\"") != std::string::npos && shapes.find("\"color\":\"#FF000000\"") != std::string::npos &&
			shapes.find("\"back\":\"#80000000\"") != std::string::npos && shapes.find("\"family\":\"Arial\"") != std::string::npos &&
			shapes.find("\"halign\":\"center\"") != std::string::npos && shapes.find("outline") == std::string::npos);
		CHECK(lua1("canvas.SetTitle('Mine') canvas.save_image_to_disk('canvas.png')") == "[]");
		std::string saveTo;
		fromValue(formRows.cell(formRows.rowCount() - 1, 3), title);
		fromValue(controls.cell(controls.rowCount() - 1, 19), saveTo);
		CHECK(title == "Mine" && saveTo.size() > 11 && saveTo.substr(saveTo.size() - 11) == "/canvas.png" && saveTo.find("screenshots") != std::string::npos);
	}
	/* a game open */
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	session.run("setLag", {int64_t{0}, int64_t{0}}, error);
	CHECK(lua1("gameinfo.getstatus(), gameinfo.isstatusbad()") == "[\"NotInDatabase\", false]");
	/* what the game calls its controls, beside their declared names (the roll's title tooltip) */
	{
		std::string labels;
		CHECK(fromValue(session.run("controlLabels", {}, error).value_or(Value()), labels) && labels == "P1 A\tJump\n");
	}
	/* a machine open: the tools that need one, the debugger's and the trace
	 * logger's as the core has registers and a trace */
	{
		std::string expected = "tastudio luaconsole ramwatch ramsearch hexeditor ";
		if (session.hasTrace.get()) expected += "tracelogger ";
		if (session.hasRegisters.get()) expected += "genericdebugger ";
		expected += "macroinputtool ";
		if (session.surfaces.get() != "[]") expected += "surfaceviewer ";
		expected += "logwindow ";
		CHECK(lua1("local t, out = client.getavailabletools(), '' for i = 0, #t do out = out .. t[i] .. ' ' end return out") == "[\"" + expected + "\"]");
		CHECK(lua1("client.gettool('TAStudio') == chimera.pianoRoll") == "[true]" && tools.showAsked.get() == "tastudio");
	}
	/* the game as Chimera names and hashes it */
	{
		char hex[41] = {};
		ce_sha1_file((dir / "walker.testrom").string().c_str(), hex, nullptr);
		const std::string named = lua1("gameinfo.getromname(), gameinfo.getromhash(), gameinfo.indatabase()");
		CHECK(named == "[\"walker\", \"" + std::string(hex, 40) + "\", false]");
		if (named != "[\"walker\", \"" + std::string(hex, 40) + "\", false]") std::fprintf(stderr, "gameinfo said %s\n", named.c_str());
	}
	const std::string size = lua1("client.bufferwidth(), client.bufferheight()");
	CHECK(size != "[256, 192]" && size.rfind("[", 0) == 0 && size.find("error") == std::string::npos);
	CHECK(lua1("client.getwindowsize()") == "[2]");
	CHECK(lua1("client.setwindowsize(3) return client.getwindowsize()") == "[3]" && resized == 1);
	CHECK(lua1("client.setwindowsize(11) return client.getwindowsize()") == "[3]" && resized == 1);
	session.holdTurbo(true);
	CHECK(lua1("client.isturbo(), client.is_rewinding(), client.isseeking()") == "[true, false, false]");
	session.holdTurbo(false);
	/* a few frames, then the game as a project */
	for (int i = 0; i < 6; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 6; }, 20));
	CHECK(session.run("saveProject", {(dir / "walker.chimeraProject").string()}, error).has_value());
	CHECK(d.until([&] { return session.hasProject.get() && !session.projectPath.get().empty(); }, 20));
	details.run("reload", {}, error);
	details.run("setComments", {std::string("first\n\nthird")}, error);
	details.run("addSubtitle", {int64_t{2}, int64_t{10}, int64_t{20}, int64_t{30}, std::string("#FFFFFFFF"), std::string("hello")}, error);
	/* the lag count: set, and the frames after add to it */
	CHECK(lua1("emu.setlagcount(50) return emu.lagcount()") == "[50]");
	CHECK(lua1("emu.setislagged(true) return emu.islagged(), emu.lagcount()") == "[true, 51]");
	CHECK(lua1("emu.setislagged(false) return emu.islagged(), emu.lagcount()") == "[false, 50]");
	/* the movie */
	CHECK(lua1("movie.startsfromsavestate(), movie.getheader().Platform ~= nil") == "[false, true]");
	/* a blank line is no comment, as Chimera reads them */
	CHECK(lua1("local c = movie.getcomments() return c[0], c[1], c[2]") == "[\"first\", \"third\", null]");
	CHECK(lua1("local s = movie.getsubtitles() return s[0] ~= nil and string.find(s[0], 'hello') ~= nil, s[1]") == "[true, null]");
	CHECK(lua1("movie.getinputasmnemonic(0), movie.getinputasmnemonic(100000)").rfind("[\"|", 0) == 0);
	CHECK(lua1("movie.getinputasmnemonic(100000)") == "[\"\"]");
	CHECK(lua1("movie.getrerecordcounting()") == "[true]");
	CHECK(lua1("movie.setrerecordcount(77) return movie.getrerecordcount()") == "[77]");
	CHECK(lua1("movie.setrerecordcounting(false) return movie.getrerecordcounting()") == "[false]");
	/* not counting: an edit over work done adds none once the machine takes it (it goes back and on) */
	auto editOverWorkDone = [&] {
		const int64_t at = session.frame.get();
		roll.run("toggle", {int64_t{1}, int64_t{0}}, error);
		session.run("frameAdvance", {}, error);
		d.until([&] { return session.frame.get() == at + 1; }, 20);
		d.until([] { return false; }, 0.3);
	};
	editOverWorkDone();
	CHECK(lua1("movie.getrerecordcount()") == "[77]");
	/* counting again: it adds one */
	CHECK(lua1("movie.setrerecordcounting(true)") == "[]");
	editOverWorkDone();
	CHECK(lua1("movie.getrerecordcount()") == "[78]");
	/* show_future: after a frame, frames played ahead until the function says enough, the machine put back */
	session.setFutureTurn([&lua](int64_t played) { return lua.ask("__chimera_future", {Value(played)}) == "true"; });
	CHECK(lua1("seen = {} future = client.show_future(function(n) seen[#seen + 1] = n return n == 3 end, 10) return #future") == "[36]");
	{
		const int64_t at = session.frame.get();
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 1; }, 20));
		CHECK(lua1("emu.framecount(), table.concat(seen, ',')") == "[" + std::to_string(at + 1) + ", \"0,1,2,3\"]");
		/* the machine's own frame counter: put back, not three ahead */
		const std::string machineFrame = lua1("emu.getregister('Frame')");
		const std::string ahead = lua1("memory.hash_region(0, 256)");
		/* the same frame played without it: the same machine (each run_lua is a
		 * script of its own, so its function goes as the console's Remove All takes it) */
		CHECK(console.run("removeAllFunctions", {}, error).has_value() && lua1("seen = {} return #seen") == "[0]");
		session.run("seek", {at}, error);
		CHECK(d.until([&] { return session.frame.get() == at; }, 20));
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 1; }, 20));
		CHECK(lua1("memory.hash_region(0, 256)") == ahead && lua1("#seen") == "[0]");
		CHECK(lua1("emu.getregister('Frame')") == machineFrame && machineFrame.find("error") == std::string::npos);
		/* the most: the function asked once more when it is reached, as Chimera's */
		CHECK(lua1("seen = {} future = client.show_future(function(n) seen[#seen + 1] = n return false end, 2)") == "[]");
		session.run("frameAdvance", {}, error);
		CHECK(d.until([&] { return session.frame.get() == at + 2; }, 20));
		CHECK(lua1("table.concat(seen, ','), client.show_future(print, 0)") == "[\"0,1,2\", \"00000000-0000-0000-0000-000000000000\"]");
		CHECK(console.run("removeAllFunctions", {}, error).has_value());
	}
	/* sleep: the scripts' thread waits */
	const auto before = std::chrono::steady_clock::now();
	CHECK(lua1("client.sleep(30) client.exactsleep(20)") == "[]");
	CHECK(std::chrono::steady_clock::now() - before >= std::chrono::milliseconds(50));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	CHECK(lua1("gameinfo.getstatus(), gameinfo.getromname(), gameinfo.getromhash(), gameinfo.indatabase()") == "[\"GoodDump\", \"Null\", \"\", true]");
	fs::remove_all(dir, ec);
	return true;
}

/* Turbo seek, as Chimera's: a long way gone in pieces, so Cancel Seek (a
 * pause) stops it part way on the frame it reached; left alone it lands on
 * its frame */
bool turboSeekCancel(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-turbo-seek-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	CHECK(session.run("saveProject", {(dir / "walker.chimeraProject").string()}, error).has_value());
	CHECK(d.until([&] { return session.hasProject.get(); }, 20));
	/* a movie long enough to take a while, and nothing kept to go back to but the start */
	const int64_t length = 400000;
	CHECK(roll.run("fill", {length}, error).has_value());
	CHECK(d.until([&] { return roll.frameCount.get() >= length; }, 20));
	session.greenzonePeriod.set(0);
	session.turboSeek.set(true);
	/* left alone: on its frame */
	CHECK(session.run("seek", {int64_t{5000}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 5000 && session.seekingTo.get() == -1; }, 60));
	/* stopped part way: where it reached, the seek over */
	CHECK(session.run("seek", {length - 1}, error).has_value());
	CHECK(d.until([&] { return session.seekingTo.get() == length - 1 && session.frame.get() > 5000; }, 20));
	CHECK(session.run("pause", {}, error).has_value());
	CHECK(d.until([&] { return session.seekingTo.get() == -1; }, 20));
	d.until([] { return false; }, 0.5);
	const int64_t stoppedAt = session.frame.get();
	std::printf("turbo seek stopped at frame %lld of %lld\n", static_cast<long long>(stoppedAt), static_cast<long long>(length - 1));
	CHECK(stoppedAt > 5000 && stoppedAt < length - 1);
	d.until([] { return false; }, 0.5);
	CHECK(session.frame.get() == stoppedAt);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir, ec);
	return true;
}

bool luaTastudio(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	LuaBridge lua;
	LuaConsoleViewModel console(d, lua);
	lua.add(session);
	lua.add(roll);
	lua.add(branches);
	lua.add(console);
	Connection zoneCalls = session.greenzoneInvalidated.connect([&lua](int64_t f) { lua.callHook("__chimera_greenzone_invalidated", f); });
	Connection branchLoads = branches.branchLoaded.connect([&lua](int64_t i) { lua.callHook("__chimera_branch_loaded", i); });
	Connection branchSaves = branches.branchSaved.connect([&lua](int64_t i) { lua.callHook("__chimera_branch_saved", i); });
	Connection branchRemoves = branches.branchRemoved.connect([&lua](int64_t i) { lua.callHook("__chimera_branch_removed", i); });
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-tastudio-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	std::string error;
	session.cacheFolder.set((dir / "cache").string());
	auto said = [&](const std::string &start) {
		std::string found;
		RowModel *out = console.rowLists()[0].model;
		for (int64_t i = 0; i < out->rowCount(); i++)
		{
			std::string line;
			fromValue(out->cell(i, 0), line);
			/* console.log puts tabs between its values, as Chimera's */
			std::replace(line.begin(), line.end(), '\t', ' ');
			if (line.rfind(start, 0) == 0) found = line;
		}
		return found;
	};
	/* nothing open: TAStudio is not engaged */
	console.run("openScript", {scriptFile("console.log('alone', tastudio.engaged(), tastudio.islag(0))", "alone")}, error);
	CHECK(d.until([&] { return !said("alone ").empty(); }, 10) && said("alone ") == "alone False nil");
	console.run("stopAll", {}, error);
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 6; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 6; }, 30));
	CHECK(session.run("saveProject", {(dir / "walker.chimeraProject").string()}, error).has_value());
	CHECK(d.until([&] { return session.hasProject.get() && !session.projectPath.get().empty(); }, 20));
	console.run("openScript", {scriptFile(R"(
console.log("engaged", tastudio.engaged(), tastudio.islag(1000))
local seen = {}
tastudio.ongreenzoneinvalidated(function(f) seen[#seen + 1] = "zone " .. f end)
tastudio.onbranchsave(function(i) seen[#seen + 1] = "save " .. i end)
tastudio.onbranchload(function(i) seen[#seen + 1] = "load " .. i end)
tastudio.onbranchremove(function(i) seen[#seen + 1] = "remove " .. i end)
function seenSoFar() return table.concat(seen, ", ") end
tastudio.submitinputchange(2, "P1 A", true)
tastudio.submitinsertframes(1, 2)
tastudio.submitinsertframes(100, 2)
tastudio.applyinputchanges()
console.log("edits", movie.getinput(4)["P1 A"], movie.getinput(2)["P1 A"], movie.length())
tastudio.setmarker(3, "here")
console.log("markers", tastudio.getmarker(3), tastudio.getmarker(0), tastudio.getmarker(5), tastudio.find_marker_on_or_before(5), #tastudio.get_frames_with_markers())
tastudio.setbranchtext("first try")
chimera.branches.create()
local b = tastudio.getbranches()
console.log("branches", b[0].Text, b[0].Frame, tastudio.get_branch_index_by_id(b[0].Id), tastudio.getbranchinput(b[0].Id, 4)["P1 A"], tastudio.getbranchinput(b[0].Id, 3)["P1 A"])
tastudio.setbranchtext("renamed", 0)
console.log("renamed", tastudio.getbranches()[0].Text)
tastudio.setlag(0, true)
local wasLag = tastudio.islag(0)
tastudio.setlag(0, nil)
console.log("lag", wasLag, tastudio.islag(0))
console.log("misc", tastudio.getrecording(), tastudio.getselection()[0] == nil)
tastudio.addcolumn("xp", "XP", 40)
tastudio.onqueryitemtext(function(row, col) if col == "xp" then return row * 10 end end)
tastudio.onqueryitembg(function(row, col) if col == "P1 A" then return 0xFF102030 end end)
tastudio.onqueryitemicon(function(row, col) if col == "P1 B" and row == 1 then return "/icons/b.png" end end)
tastudio.clearIconCache()
)", "tastudio")}, error);
	CHECK(console.running.get());
	CHECK_TEXT(said("engaged "), "engaged True nil");
	CHECK_TEXT(said("edits "), "edits True False 8");
	CHECK_TEXT(said("markers "), "markers here Run start nil 4 4");
	CHECK_TEXT(said("branches "), "branches first try 6 0 True False");
	CHECK_TEXT(said("renamed "), "renamed renamed");
	CHECK_TEXT(said("lag "), "lag True nil");
	CHECK_TEXT(said("misc "), "misc False True");
	/* the roll's drawing asks the script about cells; its own column listed */
	CHECK(roll.scriptQueries.get() && roll.scriptColumns.get().find("\"name\": \"xp\"") != std::string::npos);
	CHECK(lua.query("__chimera_query_text", 7, "xp") == "70" && lua.query("__chimera_query_text", 7, "P1 A").empty());
	CHECK(lua.query("__chimera_query_bg", 3, "P1 A") == "#FF102030" && lua.query("__chimera_query_bg", 3, "xp").empty());
	CHECK(lua.query("__chimera_query_icon", 1, "P1 B") == "/icons/b.png" && lua.query("__chimera_query_icon", 2, "P1 B").empty());
	CHECK(roll.iconEpoch.get() == 1);
	{
		/* the callbacks: the edits, the branch made once its state is kept, loaded, removed */
		CHECK(d.until([&] { return branches.made.get() >= 1; }, 30));
		std::string why;
		branches.run("load", {int64_t{0}}, error);
		CHECK(d.until([&] { return branches.loads.get() >= 1; }, 30));
		branches.run("remove", {int64_t{0}}, error);
		CHECK(lua.run("console.log('seen', seenSoFar())", "seen", why));
		const std::string seen = said("seen ");
		CHECK(seen.rfind("seen zone 2, zone 1", 0) == 0 && seen.find("save 0") != std::string::npos);
		CHECK(seen.find("load 0") != std::string::npos && seen.find("remove 0") != std::string::npos);
	}
	/* the script stopped: what it registered goes with it */
	lua.stopAll();
	CHECK(!roll.scriptQueries.get() && roll.scriptColumns.get() == "[]" && lua.query("__chimera_query_text", 7, "xp").empty());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	std::error_code ec;
	fs::remove_all(dir, ec);
	return true;
}

/* Chimera's Encode Video: frames 5 to 14 of a project's run, both
 * included, written by the ffmpeg the build ships; the file holds exactly
 * those, counted by that ffmpeg; the machine is handed back on the frame it
 * was borrowed from, paused as it was; stopping early leaves a file that
 * plays. Skipped (said) without an ffmpeg. */
bool encodeVideo(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	session.turboSeek.set(true);
	EncoderViewModel encoder(d, session, ".");
	const std::string ffmpeg = encoder.ffmpegPath();
	if (ffmpeg.empty())
	{
		std::printf("SKIP encodeVideo: no ffmpeg (CHIMERA_FFMPEG)\n");
		return true;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-encode-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	std::string error;
	session.cacheFolder.set((dir / "cache").string());
	/* no project: no encode */
	encoder.fromFrame.set(5);
	encoder.toFrame.set(14);
	encoder.output.set((dir / "run.mkv").string());
	CHECK(!encoder.run("start", {}, error) && error.find("Open a project first") != std::string::npos);
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	for (int i = 0; i < 30; i++) session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 30; }, 30));
	CHECK(session.run("saveProject", {(dir / "walker.chimeraProject").string()}, error).has_value());
	CHECK(d.until([&] { return session.hasProject.get() && !session.projectPath.get().empty(); }, 20));
	/* the box follows the command: the preset's name, the container's extension */
	encoder.run("usePreset", {int64_t{5}}, error);
	CHECK(encoder.preset.get() == "encoder.preset_444" && encoder.output.get() == (dir / "run.mkv").string());
	encoder.commandLine.set("-c:v ffv1 -c:a pcm_s16le -f matroska");
	CHECK(encoder.preset.get() == "encoder.preset_own");
	session.run("seek", {int64_t{20}}, error);
	CHECK(d.until([&] { return session.frame.get() == 20; }, 30));
	const int64_t rerecords = session.rerecords.get();
	error.clear();
	CHECK(encoder.run("start", {}, error).has_value() && error.empty());
	CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
	CHECK(encoder.phase.get() == "done" && encoder.framesDone.get() == 10);
	std::printf("encode: %s\n", encoder.status.get().c_str());
	/* handed back where it was, paused, the run untouched */
	CHECK(session.frame.get() == 20 && !session.running.get() && session.rerecords.get() == rerecords && inputs.size() == 30);
	/* the frames in the file, counted by the ffmpeg the build ships */
	auto framesIn = [&](const fs::path &file) {
		Subprocess count;
		std::string why;
		if (!count.start(ffmpeg, {"-hide_banner", "-i", file.string(), "-map", "0:v:0", "-f", "null", "-"}, why)) return int64_t{-1};
		count.closeInput();
		count.wait(-1);
		const std::string tail = count.errorTail();
		const size_t at = tail.rfind("frame=");
		return at == std::string::npos ? int64_t{-1} : static_cast<int64_t>(std::atoll(tail.c_str() + at + 6));
	};
	CHECK(framesIn(dir / "run.mkv") == 10);
	/* the same without syncing to the sound: a frame each time */
	encoder.audioSync.set(false);
	encoder.output.set((dir / "nosync.mkv").string());
	encoder.fromFrame.set(0);
	encoder.toFrame.set(29);
	CHECK(encoder.run("start", {}, error).has_value());
	CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
	CHECK(encoder.phase.get() == "done" && framesIn(dir / "nosync.mkv") == 30);
	/* a size of its own, padded */
	encoder.resize.set(true);
	encoder.width.set(320);
	encoder.height.set(200);
	encoder.pad.set(true);
	encoder.output.set((dir / "sized.mkv").string());
	CHECK(encoder.run("start", {}, error).has_value());
	CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
	CHECK(encoder.phase.get() == "done" && framesIn(dir / "sized.mkv") == 30);
	/* Capture OSD takes Capture Lua with it, and Capture Lua off takes the OSD, as Chimera's boxes */
	encoder.resize.set(false);
	encoder.captureOsd.set(true);
	CHECK(encoder.captureLua.get());
	encoder.captureLua.set(false);
	CHECK(!encoder.captureOsd.get());
	/* Capture Lua: each frame is handed to the window, at its own size, to have
	 * what is drawn over it put in; scripts have their turns as it plays */
	{
		LuaBridge lua;
		lua.add(session);
		lua.toScripts = [&session](const std::function<void()> &job) { return session.onMachineThreadWait(job); };
		lua.toWindow = [&session](const std::function<void()> &job) { return session.onWindowWait(job); };
		session.setScriptTurns([&lua] { lua.frameStart(); }, [&lua] { lua.frameDone(); });
		CHECK(lua.run("ended = 0 event.onframeend(function() ended = ended + 1 end)", "count", error));
		int composed = 0, withOsd = 0, elsewhere = 0, w = 0, h = 0;
		double shownW = 0, shownH = 0;
		const auto window = std::this_thread::get_id();
		encoder.compose = [&](std::vector<uint32_t> &pixels, int32_t &pw, int32_t &ph, double sw, double sh, bool osd) {
			composed++;
			withOsd += osd;
			elsewhere += std::this_thread::get_id() != window;
			shownW = sw;
			shownH = sh;
			/* the OSD's picture is the window's, at its picture area's size: here three times the shown size */
			if (osd)
			{
				pw = static_cast<int32_t>(sw) * 3;
				ph = static_cast<int32_t>(sh) * 3;
				pixels.assign(static_cast<size_t>(pw) * ph, 0xFF000000u);
			}
			w = pw;
			h = ph;
			pixels[0] = 0xFF123456u;
		};
		encoder.captureLua.set(true);
		encoder.audioSync.set(false);
		encoder.commandLine.set("-an -f rawvideo -pix_fmt bgra");
		encoder.output.set((dir / "lua.raw").string());
		encoder.fromFrame.set(0);
		encoder.toFrame.set(9);
		CHECK(encoder.run("start", {}, error).has_value());
		CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
		CHECK(encoder.phase.get() == "done" && composed == 10 && withOsd == 0 && elsewhere == 0 && w > 0 && h > 0);
		/* in every frame of the file */
		const std::string raw = readFile(dir / "lua.raw");
		bool marked = raw.size() == static_cast<size_t>(w) * h * 4 * 10;
		for (size_t f = 0; marked && f < 10; f++)
			marked = std::memcmp(raw.data() + f * static_cast<size_t>(w) * h * 4, "\x56\x34\x12\xFF", 4) == 0;
		CHECK(marked);
		std::vector<std::string> said;
		lua.onLog = [&](const std::string &l) { said.push_back(l); };
		/* the wind to the first marker lands (a seek's landing is a frame's end), then ten frames,
		 * then the frame given back lands too */
		CHECK(lua.run("print(ended)", "ended", error) && !said.empty() && said.back() == "12");
		/* Capture OSD: the picture as the window shows it, at the size of
		 * its picture area, as Chimera's; the machine's 128x120 picture is
		 * shown 256x240 */
		const int32_t machineW = w, machineH = h;
		encoder.captureOsd.set(true);
		encoder.output.set((dir / "osd.raw").string());
		CHECK(encoder.run("start", {}, error).has_value());
		CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
		CHECK(encoder.phase.get() == "done" && composed == 20 && withOsd == 10);
		CHECK(machineW == 128 && machineH == 120 && shownW == 256 && shownH == 240 && w == 768 && h == 720);
		const std::string osdRaw = readFile(dir / "osd.raw");
		CHECK(osdRaw.size() == static_cast<size_t>(w) * h * 4 * 10 && std::memcmp(osdRaw.data(), "\x56\x34\x12\xFF", 4) == 0);
		session.setScriptTurns({}, {});
		encoder.compose = nullptr;
		encoder.captureLua.set(false);
		encoder.commandLine.set("-c:v ffv1 -c:a pcm_s16le -f matroska");
		encoder.audioSync.set(true);
		encoder.fromFrame.set(0);
		encoder.toFrame.set(29);
	}
	/* stopped before its first frame: given back, and no file (one opens with its first frame) */
	encoder.resize.set(false);
	encoder.output.set((dir / "stopped.mkv").string());
	CHECK(encoder.run("start", {}, error).has_value());
	encoder.run("stop", {}, error);
	CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
	CHECK(encoder.phase.get() == "stopped" && session.frame.get() == 20 && !fs::exists(dir / "stopped.mkv"));
	/* an ffmpeg that refuses the command says why */
	encoder.commandLine.set("-c:v no-such-codec -f matroska");
	encoder.output.set((dir / "bad.mkv").string());
	CHECK(encoder.run("start", {}, error).has_value());
	CHECK(d.until([&] { return encoder.phase.get() != "encoding"; }, 120));
	CHECK(encoder.phase.get() == "failed" && encoder.status.get().find("ffmpeg") != std::string::npos && session.frame.get() == 20);
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	/* a picture shown in another shape than its pixels' (a PC-98's 640x400
	 * shown 4:3) is written in that shape, and the YouTube presets make it
	 * that shape in square pixels; the lossless presets give back every
	 * pixel (issue 375) */
	{
		CHECK((EncoderViewModel::pixelShape(640, 400, 640, 480) == std::pair<int64_t, int64_t>{5, 6}));
		CHECK((EncoderViewModel::pixelShape(320, 240, 320, 240) == std::pair<int64_t, int64_t>{1, 1}));
		CHECK((EncoderViewModel::pixelShape(320, 240, 0, 0) == std::pair<int64_t, int64_t>{1, 1}));
		/* a dithered picture: black and white, red and blue, a ramp */
		const int32_t w = 640, h = 400;
		std::vector<uint32_t> picture(static_cast<size_t>(w) * h);
		for (int32_t y = 0; y < h; y++)
			for (int32_t x = 0; x < w; x++)
				picture[static_cast<size_t>(y) * w + x] = 0xFF000000u |
					(x < 200 ? ((x + y) % 2 ? 0xFFFFFFu : 0) : x < 400 ? ((x + y) % 2 ? 0xFF0000u : 0x0000FFu)
								: static_cast<uint32_t>((x * 255 / w) << 16 | (y * 255 / h) << 8 | 128));
		auto encode = [&](const std::string &command, const fs::path &out) {
			Subprocess ff;
			std::string why;
			std::vector<std::string> args = {"-y", "-f", "nut", "-i", "-"};
			for (const std::string &word : splitCommandLine(command)) args.push_back(word);
			args.push_back(out.string());
			if (!ff.start(ffmpeg, args, why)) return false;
			{
				NutMuxer mux(w, h, 60, 1, 44100, 2, [&ff](const uint8_t *data, size_t size) { return ff.write(data, size); }, 5, 6);
				const std::vector<int16_t> quiet(735 * 2, 0);
				for (int i = 0; i < 3; i++) mux.writeVideo(picture.data(), picture.size()), mux.writeAudio(quiet.data(), quiet.size());
				mux.finish();
			}
			ff.closeInput();
			return ff.wait(-1) == 0;
		};
		/* what ffmpeg says of a file, every line */
		auto described = [&](const fs::path &file) {
			Subprocess probe;
			std::string why, said;
			probe.onLine = [&said](const std::string &line) { said += line + "\n"; };
			if (!probe.start(ffmpeg, {"-hide_banner", "-i", file.string()}, why)) return std::string();
			probe.closeInput();
			probe.wait(-1);
			return said;
		};
		CHECK(encode("-c:v utvideo -f matroska", dir / "shape.mkv") && described(dir / "shape.mkv").find("640x400, SAR 5:6 DAR 4:3") != std::string::npos);
		const auto &presets = EncoderViewModel::presets();
		CHECK(encode(presets[2].command, dir / "youtube.mp4") && described(dir / "youtube.mp4").find("2880x2160 [SAR 1:1 DAR 4:3]") != std::string::npos);
		for (const auto &p : presets)
		{
			if (std::string(p.key).find("_rgb") == std::string::npos && std::string(p.key) != "encoder.preset_ut_video") continue;
			const fs::path out = dir / "lossless.mkv", raw = dir / "lossless.raw";
			std::error_code gone;
			fs::remove(raw, gone);
			Subprocess back;
			std::string why;
			const bool written = encode(p.command, out) && back.start(ffmpeg, {"-y", "-i", out.string(), "-vframes", "1", "-f", "rawvideo",
				"-pix_fmt", "bgra", raw.string()}, why) && (back.closeInput(), back.wait(-1) == 0);
			const std::string bytes = readFile(raw);
			bool same = written && bytes.size() == picture.size() * 4;
			for (size_t i = 0; same && i < picture.size(); i++)
				same = (reinterpret_cast<const uint32_t *>(bytes.data())[i] | 0xFF000000u) == picture[i];
			std::printf("%s: %s\n", p.key, same ? "every pixel kept" : "changed");
			CHECK(same && described(out).find("Audio: flac") != std::string::npos);
		}
	}
	std::error_code ec;
	fs::remove_all(dir, ec);
	return true;
}

/* the NUT stream's own rules: the magic, the headers' start codes, and the
 * picture and sound interleaved by time */
void nutMuxer()
{
	std::vector<uint8_t> out;
	NutMuxer m(2, 1, 60, 1, 44100, 2, [&](const uint8_t *p, size_t n) {
		out.insert(out.end(), p, p + n);
		return true;
	});
	CHECK(out.size() > 25 && std::memcmp(out.data(), "nut/multimedia container", 25) == 0);
	const uint32_t px[2] = {0xFF0000FFu, 0xFF00FF00u};
	std::vector<int16_t> sound(735 * 2, 7);
	CHECK(m.writeVideo(px, 2) && m.writeAudio(sound.data(), sound.size()) && m.writeVideo(px, 2) && m.finish());
	/* the syncpoint start code begins each frame: two pictures, a sound, two ends */
	size_t syncpoints = 0;
	const uint8_t sp[8] = {0x4E, 0x4B, 0xE4, 0xAD, 0xEE, 0xCA, 0x45, 0x69};
	for (size_t i = 0; i + 8 <= out.size(); i++)
		if (std::memcmp(out.data() + i, sp, 8) == 0) syncpoints++;
	CHECK(syncpoints == 5);
	CHECK(splitCommandLine("-vf \"scale=1:2\" -f  mp4") == std::vector<std::string>({"-vf", "scale=1:2", "-f", "mp4"}));
	CHECK(EncoderViewModel::extensionOf("-c:v ffv1 -f matroska") == "mkv" && EncoderViewModel::extensionOf("-f mp4") == "mp4");
	/* how far it is, in Chimera's words: counts with their thousands marked, the time left once a speed is known */
	CHECK(EncoderViewModel::progressText(1234, 5000, 59.6) == "frame 1,234 / 5,000   60 fps   about 1m 3s remaining");
	CHECK(EncoderViewModel::progressText(5, 10, 0) == "frame 5 / 10   0 fps");
	CHECK(EncoderViewModel::progressText(100, 222100, 60) == "frame 100 / 222,100   60 fps   about 1h 1m remaining");
	CHECK(EncoderViewModel::progressText(9, 10, 2) == "frame 9 / 10   2 fps   about 0s remaining");
}

/* Chimera's idle eye: nothing drawn without the mark; with it, the faint
 * mark, its eye sliding towards the pointer, a blink now and then, and no
 * new picture while nothing changes */
void idleEye()
{
	ManualDispatcher d;
	IdleEyeViewModel eye(d);
	/* a day of the year with no snow, whatever today is */
	int day = 100;
	eye.dayOfYear = [&day] { return day; };
	CHECK(!eye.tick(0) && eye.drawn.get() == 0);
	/* a slab left of the eye's box, and a block inside it */
	std::vector<uint32_t> art(160 * 160, 0);
	for (int y = 70; y < 90; y++)
		for (int x = 0; x < 160; x++)
			if ((x >= 10 && x < 30) || (x >= 70 && x < 90)) art[static_cast<size_t>(y) * 160 + x] = 0xFFFFFFFFu;
	eye.setMark(160, art);
	CHECK(eye.tick(0) && eye.drawn.get() == 1);
	/* where the eye's block is lit, across the screen */
	auto eyeAt = [&] {
		Picture::Frame f;
		eye.eye.takeIfNewer(0, f);
		double sum = 0, weight = 0;
		for (int y = 0; y < f.height; y++)
			for (int x = 200; x < 300 && x < f.width; x++)
			{
				const double level = f.pixels[static_cast<size_t>(y) * f.width + x] & 0xFF;
				sum += level * x;
				weight += level;
			}
		return weight > 0 ? sum / weight : -1.0;
	};
	Picture::Frame first;
	CHECK(eye.eye.takeIfNewer(0, first) && first.width == 480 && first.height == 360);
	/* faint: no pixel brighter than a tenth and a bit */
	uint32_t brightest = 0;
	for (uint32_t p : first.pixels) brightest = std::max(brightest, p & 0xFF);
	CHECK(brightest > 0 && brightest <= 30);
	const double looking = eyeAt();
	for (int i = 0; i < 40; i++) eye.tick(1);
	CHECK(looking > 0 && eyeAt() > looking + 10);
	/* settled and still: far fewer pictures than ticks */
	const int64_t before = eye.drawn.get();
	for (int i = 0; i < 60; i++) eye.tick(1);
	CHECK(eye.drawn.get() - before < 30);
	/* a blink within the first seconds */
	float lowest = 1.0f;
	for (int i = 0; i < 600; i++)
	{
		eye.tick(1);
		lowest = std::min(lowest, eye.lid());
	}
	CHECK(lowest < 0.2f);
	/* Chimera's Snowy null core: the week of Christmas at first, days 354 to 360 */
	CHECK(eye.snowWhen.get() == 2 && !eye.snowing());
	day = 354;
	CHECK(eye.snowing());
	day = 361;
	CHECK(!eye.snowing());
	eye.snowWhen.set(1);
	/* 256 x 192, grey, at once and then every tenth turn */
	const int64_t drawnBefore = eye.drawn.get();
	CHECK(eye.tick(0));
	Picture::Frame snow;
	CHECK(eye.eye.takeIfNewer(0, snow) && snow.width == 256 && snow.height == 192);
	int news = 0;
	for (int i = 0; i < 30; i++) news += eye.tick(0) ? 1 : 0;
	CHECK(news == 3 && eye.drawn.get() == drawnBefore + 4);
	auto stats = [&] {
		Picture::Frame f;
		eye.eye.takeIfNewer(0, f);
		bool grey = true;
		double sum = 0;
		uint32_t top = 0;
		for (uint32_t p : f.pixels)
		{
			const uint32_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
			grey = grey && r == g && g == b;
			sum += b;
			top = std::max(top, b);
		}
		return std::make_tuple(grey, f.pixels.empty() ? 0.0 : sum / f.pixels.size(), top);
	};
	const auto [grey, mean, top] = stats();
	/* bias -2: each byte raised to 100, nearly all black */
	CHECK(grey && mean < 5 && top > 0);
	/* bias 0 and half the brightness: an even spread up to half white */
	eye.snowBias.set(0.0);
	eye.snowIntensity.set(0.5);
	for (int i = 0; i < 10; i++) eye.tick(0);
	const auto [grey2, mean2, top2] = stats();
	CHECK(grey2 && mean2 > 55 && mean2 < 72 && top2 <= 128 && top2 >= 120);
	/* never: the eye again */
	eye.snowWhen.set(0);
	CHECK(eye.tick(0));
	Picture::Frame back;
	CHECK(eye.eye.takeIfNewer(0, back) && back.width == 480);
}

/* Chimera's Firmware window: a group a core (the emulators first), a line a
 * declaration in Chimera's words, Show, many releases of one id shown only
 * when on hand, what a line says in full, Set File (a pinned dump kept
 * under its own hash), Clear, and Scan Folder (a folder that answers
 * becomes the firmware folder when the one named is not there) */
bool firmwareWindow(const char *package, const char *slotsPackage, const char *biosPackage, const char *gamePackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(slotsPackage, ec) == 0 || ec || fs::file_size(biosPackage, ec) == 0 || ec)
	{
		std::printf("SKIP the Firmware window: no slots or bios package was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-firmware-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::create_directories(dir / "Firmware");
	fs::copy_file(package, dir / "Cores" / "synth-box.chimeraCore");
	fs::copy_file(slotsPackage, dir / "Cores" / "synth-slots.chimeraCore");
	fs::copy_file(biosPackage, dir / "Cores" / "synth-bios.chimeraCore");
	fs::copy_file(gamePackage, dir / "Cores" / "synth-game.chimeraCore");
	ManualDispatcher d;
	CoresViewModel cores(d);
	std::string error;
	cores.set("coresFolder", (dir / "Cores").string(), error);
	cores.set("firmwareFolder", (dir / "Firmware").string(), error);
	/* a Windows folder that came through a file URL with its slash kept, as
	 * the folder dialogs once wrote one and a setting may still hold */
	{
		CoresViewModel fromUrl(d);
		fromUrl.set("coresFolder", std::string("/C:/Users/someone/chimera/Cores"), error);
		fromUrl.set("firmwareFolder", std::string("/D:\\Firmware"), error);
		CHECK(fromUrl.coresFolder.get() == "C:/Users/someone/chimera/Cores" && fromUrl.firmwareFolder.get() == "D:\\Firmware");
		fromUrl.set("coresFolder", std::string("/home/someone/Cores"), error);
		CHECK(fromUrl.coresFolder.get() == "/home/someone/Cores");
	}
	FirmwareViewModel fw(d, cores);
	RowModel &lines = *fw.rowLists()[0].model;
	auto text = [&](int64_t r, int32_t c) {
		std::string t;
		fromValue(lines.cell(r, c), t);
		return t;
	};
	auto mark = [&](int64_t r) {
		int64_t m = -1;
		fromValue(lines.cell(r, 3), m);
		return m;
	};
	auto lineOf = [&](const std::string &firmware, const std::string &release) {
		for (int64_t r = 0; r < lines.rowCount(); r++)
			if (text(r, 4) == firmware && (release.empty() || text(r, 5) == release)) return r;
		return int64_t{-1};
	};
	auto coresListed = [&] {
		std::string out;
		for (int64_t r = 0; r < lines.rowCount(); r++)
			if (out.empty() || out.substr(out.rfind('|', out.size() - 2) + 1) != text(r, 0) + "|") out += text(r, 0) + "|";
		return out;
	};
	CHECK(fw.run("refresh", {}, error).has_value());
	/* the emulators first, each by name, then the game cores */
	CHECK(coresListed() == "Synth|Synth Bios|Synth Slots|Synth Game|");
	CHECK(text(0, 2) == "none" && text(0, 4) == "(needs no firmware)" && text(0, 1) == "needs no firmware");
	/* ten releases of one firmware: none on hand, so one line says so */
	const int64_t more = lineOf("… 10 more releases of Synth BIOS not on hand; tick \"Show every release\" to see them", "");
	CHECK(more >= 0 && text(more, 2) == "more" && text(more, 0) == "Synth Bios" &&
		  text(more, 1) == "Synth BIOS · 0 of 10 dumps on hand · any one satisfies a project that chooses it");
	CHECK(text(more, 11) == "Synth Bios  ·  " + text(more, 1));
	fw.showAll.set(true);
	CHECK(lineOf("Synth BIOS", "v01") >= 0 && lineOf("Synth BIOS", "v10") >= 0 && text(lineOf("Synth BIOS", "v03"), 6) == "AE71FDD6" &&
		  text(lineOf("Synth BIOS", "v03"), 9) == "not found" && mark(lineOf("Synth BIOS", "v03")) == 3);
	fw.showAll.set(false);
	/* a release in the firmware folder: shown, found by its hash */
	std::ofstream(dir / "Firmware" / "rel03.bin") << "bios rel 03";
	fw.run("refresh", {}, error);
	{
		const int64_t v3 = lineOf("Synth BIOS", "v03");
		CHECK(v3 >= 0 && lineOf("Synth BIOS", "v04") < 0 && text(v3, 9) == "found, hash matches" && text(v3, 8) == "Firmware folder" &&
			  text(v3, 7) == "rel03.bin" && mark(v3) == 0 && text(v3, 1).find("1 of 10 dumps on hand") != std::string::npos);
		CHECK(lineOf("… 9 more releases of Synth BIOS not on hand; tick \"Show every release\" to see them", "") >= 0);
	}
	/* Chimera's Find box, folding and Open Folder (issue 368) */
	{
		fw.set("find", std::string("REL03"), error);
		CHECK(coresListed() == "Synth Bios|" && lineOf("Synth BIOS", "v03") >= 0 && lines.rowCount() == 1);
		fw.set("find", std::string("board b"), error);
		CHECK(coresListed() == "Synth Slots|" && lineOf("Board B BIOS", "") >= 0);
		fw.set("find", std::string("nothing like this"), error);
		CHECK(lines.rowCount() == 0);
		fw.set("find", std::string(""), error);
		CHECK(coresListed() == "Synth|Synth Bios|Synth Slots|Synth Game|");
		/* a core folded to one line, and opened again by a line of it */
		CHECK(fw.run("collapse", {std::string("Synth Bios")}, error).has_value());
		const int64_t folded = lineOf("+ Synth Bios: 10 files", "double-click or + to show");
		CHECK(folded >= 0 && text(folded, 2) == "folded" && lineOf("Synth BIOS", "v03") < 0);
		CHECK(fw.run("expand", {folded}, error).has_value() && lineOf("Synth BIOS", "v03") >= 0);
		/* all of them, a core with no firmware left as it is */
		CHECK(fw.run("collapseAll", {}, error).has_value());
		{
			std::string folds;
			for (int64_t r = 0; r < lines.rowCount(); r++) folds += text(r, 4) + "|";
			CHECK(folds == "(needs no firmware)|+ Synth Bios: 10 files|+ Synth Slots: 2 files|+ Synth Game: 1 file|");
		}
		CHECK(fw.run("expandAll", {}, error).has_value() && lineOf("Board B BIOS", "") >= 0);
		/* Open Folder: the folder of the file found; none for a line with no file */
		std::string folder;
		CHECK(fromValue(fw.run("folderOf", {lineOf("Synth BIOS", "v03")}, error).value_or(Value()), folder) &&
			fs::equivalent(folder, dir / "Firmware"));
		CHECK(fromValue(fw.run("folderOf", {lineOf("Board B BIOS", "")}, error).value_or(Value()), folder) && folder.empty() &&
			fw.status.get() == "No file was found for Board B BIOS.");
	}
	/* one in a sub-folder of the firmware folder: found, and in the Firmware folder, as Chimera's (chimera#359) */
	fs::create_directories(dir / "Firmware" / "Synth" / "Bios");
	std::ofstream(dir / "Firmware" / "Synth" / "Bios" / "rel05.bin") << "bios rel 05";
	fw.run("refresh", {}, error);
	{
		const int64_t v5 = lineOf("Synth BIOS", "v05");
		CHECK(v5 >= 0 && text(v5, 9) == "found, hash matches" && text(v5, 8) == "Firmware folder" && text(v5, 7) == "rel05.bin");
	}
	fs::remove_all(dir / "Firmware" / "Synth");
	fw.run("refresh", {}, error);
	/* a declaration with no hash, needed for one board: what it says in
	 * full; the other entry of its id is a line of its own, by its label */
	const int64_t boardB = lineOf("Board B BIOS", "");
	CHECK(boardB >= 0 && text(boardB, 5) == "when board = b and not diskSpeed = 4" && text(boardB, 6) == "(any)" && text(boardB, 9) == "not found");
	CHECK(lineOf("Board B BIOS", "fast disks") >= 0);
	fw.picked.set(boardB);
	CHECK(fw.detail.get() == "Board B BIOS, 0 bytes\na file called boardb.bin, any bytes · needed when board = b and not diskSpeed = 4\nnothing yet" &&
		  fw.canSet.get() && !fw.canClear.get() && fw.pickTitle.get() == "Synth Slots: Board B BIOS");
	CHECK(!fw.run("setFile", {lineOf("Board B BIOS", ""), (dir / "none.bin").string()}, error) && error == (dir / "none.bin").string() + " is not a file");
	CHECK(!fw.run("setFile", {int64_t{0}, (dir / "none.bin").string()}, error) && error == "line 0 is not a firmware file");
	/* a file of the person's: chosen, found by size and name; Clear forgets it */
	std::ofstream(dir / "mine.bin") << "my board B bios";
	CHECK(fw.run("setFile", {lineOf("Board B BIOS", ""), (dir / "mine.bin").string()}, error).has_value());
	{
		const int64_t l = lineOf("Board B BIOS", "");
		CHECK(text(l, 8) == "chosen" && text(l, 9) == "found by size and name" && fw.picked.get() == l && fw.canClear.get() &&
			  fw.detail.get().find((dir / "mine.bin").string() + " is ") != std::string::npos);
		CHECK(fw.run("clearFile", {l}, error).has_value() && text(lineOf("Board B BIOS", ""), 9) == "not found");
	}
	/* a pinned release chosen: kept under its own hash; another size, another hash, a file gone */
	fw.showAll.set(true);
	fs::create_directories(dir / "x");
	std::ofstream(dir / "x" / "my05.bin") << "bios rel 05";
	CHECK(fw.run("setFile", {lineOf("Synth BIOS", "v05"), (dir / "x" / "my05.bin").string()}, error).has_value() &&
		  text(lineOf("Synth BIOS", "v05"), 9) == "found, hash matches" && text(lineOf("Synth BIOS", "v05"), 8) == "chosen");
	CHECK(cores.chosenFirmwareFor("Synth Bios", "bios#10558C2BC8A1F4DBAE702864F80B790774E0C89E") == (dir / "x" / "my05.bin").string() &&
		  cores.chosenFirmwareFor("Synth Bios", "bios").empty());
	std::ofstream(dir / "x" / "big.bin") << "a bios of another size";
	fw.run("setFile", {lineOf("Synth BIOS", "v06"), (dir / "x" / "big.bin").string()}, error);
	CHECK(text(lineOf("Synth BIOS", "v06"), 9) == "not the 11 bytes the core expects - used anyway" && mark(lineOf("Synth BIOS", "v06")) == 1);
	std::ofstream(dir / "x" / "odd.bin") << "bios rel ??";
	fw.run("setFile", {lineOf("Synth BIOS", "v07"), (dir / "x" / "odd.bin").string()}, error);
	CHECK(text(lineOf("Synth BIOS", "v07"), 9) == "right size, a dump the core has never seen - used anyway");
	fs::remove(dir / "x" / "odd.bin");
	fw.run("refresh", {}, error);
	CHECK(text(lineOf("Synth BIOS", "v07"), 9) == "chosen file is gone" && mark(lineOf("Synth BIOS", "v07")) == 2);
	/* Show: the game cores' files, or the emulators' */
	fw.shows.set(2);
	CHECK(coresListed() == "Synth Game|");
	fw.shows.set(1);
	CHECK(coresListed() == "Synth|Synth Bios|Synth Slots|");
	fw.shows.set(0);
	/* Scan Folder: what it answers is remembered; the folder becomes the firmware folder when the one named is not there */
	fs::create_directories(dir / "scan" / "deeper");
	std::ofstream(dir / "scan" / "deeper" / "boardb.bin") << "a board B BIOS, by its name";
	cores.set("firmwareFolder", (dir / "nowhere").string(), error);
	int64_t found = 0;
	CHECK(fromValue(fw.run("scanFolder", {(dir / "scan").string(), true}, error).value_or(Value()), found) && found == 1 &&
		  fw.status.get() == "Found 1 firmware file in " + (dir / "scan").string());
	CHECK(text(lineOf("Board B BIOS", ""), 9) == "found by name" && cores.firmwareFolder.get() == (dir / "scan").string());
	fs::remove_all(dir, ec);
	return true;
}

/* Chimera's Core Manager: a row a core (its versions together), the
 * emulators before the games, Show, a core's versions newest first and
 * what each says, one version or whole cores removed (only files in the
 * cores folder), and the systems they add up to */
bool coreManager(const char *package, const char *slotsPackage, const char *gamePackage)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(slotsPackage, ec) == 0 || ec || fs::file_size(gamePackage, ec) == 0 || ec)
	{
		std::printf("SKIP the Core Manager: no slots or game package was made\n");
		return false;
	}
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-coremanager-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir / "Cores");
	fs::copy_file(package, dir / "Cores" / "synth-box.chimeraCore");
	fs::copy_file(package, dir / "Cores" / "synth-box-older.chimeraCore");
	fs::last_write_time(dir / "Cores" / "synth-box-older.chimeraCore", fs::last_write_time(dir / "Cores" / "synth-box.chimeraCore") - std::chrono::hours(1));
	fs::copy_file(slotsPackage, dir / "Cores" / "synth-slots.chimeraCore");
	fs::copy_file(gamePackage, dir / "Cores" / "synth-game.chimeraCore");
	ManualDispatcher d;
	CoresViewModel cores(d);
	std::string error;
	cores.set("coresFolder", (dir / "Cores").string(), error);
	RowModel *manager = nullptr;
	for (const auto &r : cores.rowLists())
		if (r.name == "manager") manager = r.model;
	CHECK(manager != nullptr);
	if (manager == nullptr) return false;
	auto text = [&](int64_t r, int32_t c) {
		std::string t;
		fromValue(manager->cell(r, c), t);
		return t;
	};
	auto listed = [&] {
		std::string out;
		for (int64_t r = 0; r < manager->rowCount(); r++) out += text(r, 0) + "|";
		return out;
	};
	/* a row a core, the emulators first, each by name; then the games */
	CHECK(listed() == "Synth|Synth Slots|Synth Game|");
	CHECK(text(0, 1) == "Emulator" && text(2, 1) == "Game" && text(0, 2) == "Synthetic Witness" && text(1, 2).find(", ") != std::string::npos);
	CHECK(text(0, 3).find("  (+1 more)") != std::string::npos && text(0, 4).find(" KB") != std::string::npos);
	CHECK(cores.managerHeader.get() == "Cores folder: " + (dir / "Cores").string() + "   (3 cores)\n"
		"Chimera downloads nothing. Get a core's package (.chimeraCore) from its project, or build it, and put it in this folder.");
	/* Show: the games, or the emulators */
	cores.shows.set(2);
	CHECK(listed() == "Synth Game|");
	cores.shows.set(1);
	CHECK(listed() == "Synth|Synth Slots|");
	cores.shows.set(0);
	/* a core's versions, newest first, and what one says */
	json::Json versions;
	std::string got;
	CHECK(fromValue(cores.run("versionsOf", {std::string("Synth")}, error).value_or(Value()), got) && json::parse(got, versions, error) &&
		  versions.items.size() == 2 && versions.items[0].find("path")->text == (dir / "Cores" / "synth-box.chimeraCore").string());
	CHECK(fromValue(cores.run("versionDetail", {(dir / "Cores" / "synth-box.chimeraCore").string()}, error).value_or(Value()), got) &&
		  got.rfind((dir / "Cores" / "synth-box.chimeraCore").string(), 0) == 0 && got.find("This package states no licence.") != std::string::npos);
	/* one version removed; one not in the cores folder is not the manager's */
	fs::copy_file(package, dir / "elsewhere.chimeraCore");
	cores.run("removeVersion", {(dir / "elsewhere.chimeraCore").string()}, error);
	CHECK(cores.managerStatus.get() == "elsewhere.chimeraCore is not in the cores folder, so it is not the manager's to delete. It is at " +
		  (dir / "elsewhere.chimeraCore").string() + "." && fs::exists(dir / "elsewhere.chimeraCore"));
	cores.run("removeVersion", {(dir / "Cores" / "synth-box-older.chimeraCore").string()}, error);
	CHECK(cores.managerStatus.get() == "Removed synth-box-older.chimeraCore." && !fs::exists(dir / "Cores" / "synth-box-older.chimeraCore") &&
		  text(0, 3).find("(+") == std::string::npos);
	/* the systems they add up to, by name, each with the cores that run it */
	CHECK(fromValue(cores.run("supportedSystems", {}, error).value_or(Value()), got) &&
		  got.find("{\"name\": \"Synthetic Witness\", \"id\": \"Synth\", \"cores\": \"Synth, Synth Game, Synth Slots\", \"installed\": true}") != std::string::npos);
	/* whole cores removed */
	CHECK(cores.run("removeCores", {std::string("[\"Synth Slots\", \"Synth Game\"]")}, error) == Value(int64_t{2}) &&
		  cores.managerStatus.get() == "Removed 2 packages." && listed() == "Synth|");
	cores.run("refreshList", {}, error);
	CHECK(cores.managerStatus.get() == "1 core in " + (dir / "Cores").string() + ".");
	fs::remove_all(dir, ec);
	return true;
}

/* a core that declares no virtual size has square pixels: its picture is shown
 * at its own shape, not at the shape of the largest picture it has room for
 * (a Flash game was drawn too wide); one that declares one is shown at that */
/* A controller wider than the 64 buttons one number carries (issue 411:
 * Ruffle's keyboard is 103 buttons, Mednafen's controllers 594). A key bound
 * to a button past the 64th is recorded into the movie, the piano roll shows
 * its cell and edits it, the movie's line has it, and a script's hold and
 * autohold reach it, as for any other button. All of it stopped at the 64th. */
bool wideButtons(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(package, ec) == 0 || ec)
	{
		std::printf("SKIP the wide controller: no package with more than 64 buttons\n");
		return true;
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	/* edits before the current frame run the machine back to where it was */
	session.autoRestore.set(true);
	session.turboSeek.set(true);
	PianoRollViewModel roll(d, inputs, session);
	std::string error;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-wide-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	session.cacheFolder.set((dir / "cache").string());
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	const std::vector<std::string> names = session.buttonNames();
	CHECK(names.size() == 88);
	const size_t wide = 80;
	auto pressedIn = [&](int64_t frame) {
		std::string out;
		fromValue(session.run("movieInput", {frame}, error).value_or(Value()), out);
		return out;
	};
	/* a key bound to it, held over a frame recorded */
	std::vector<std::string> texts = session.bindingTexts();
	texts.resize(names.size());
	texts[wide] = "F9";
	session.setBindingTexts(texts);
	session.recordingMode.set(true);
	session.key("F9", true);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 1; }, 10));
	session.key("F9", false);
	CHECK(inputs.size() == 1 && inputs.at(0).test(wide) && !inputs.at(0).test(wide - 1));
	CHECK(pressedIn(0) == names[wide] + "\n");
	/* the roll shows the cell and flips it, a step to undo */
	CHECK(roll.frames().cell(0, static_cast<int32_t>(wide)) == Value(true));
	roll.run("toggle", {int64_t{0}, static_cast<int64_t>(wide)}, error);
	CHECK(!inputs.at(0).test(wide) && pressedIn(0).empty());
	roll.run("undo", {}, error);
	CHECK(inputs.at(0).test(wide));
	/* a script's hold and autohold reach the buttons past it too (recording
	 * again: an edit turns it off) */
	session.recordingMode.set(true);
	session.run("scriptHold", {names[wide + 1]}, error);
	session.run("toggleAutohold", {names[wide + 2]}, error);
	session.run("frameAdvance", {}, error);
	CHECK(d.until([&] { return session.frame.get() == 2; }, 10));
	CHECK(inputs.at(1).test(wide + 1) && inputs.at(1).test(wide + 2) && !inputs.at(1).test(wide));
	std::string played;
	fromValue(session.run("playedButtons", {}, error).value_or(Value()), played);
	CHECK(played == names[wide + 1] + "\n" + names[wide + 2] + "\n");
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir, ec);
	return true;
}

/* While a game plays, the window's lists change in place and are not made
 * again (issue 404): the run's end marker moving on, the step the frames
 * played join growing, the whole movie's macro zone growing with it, and
 * the Hex Editor's bytes read again. Each was told as a reset on every
 * frame, and a view builds every row it shows anew on a reset: the window
 * drew 13 to 20 frames a second while the machine ran 60. The Hex Editor
 * reads only while its window is open. */
bool listsWhilePlaying(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	MacroToolViewModel macros(d, roll, session);
	HexEditorViewModel hex(d, session);
	std::string error;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-lists-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	session.cacheFolder.set((dir / "cache").string());
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	auto named = [](ViewModel &vm, const std::string &name) -> RowModel * {
		for (const auto &r : vm.rowLists())
			if (r.name == name) return r.model;
		return nullptr;
	};
	RowModel *const lists[] = {named(roll, "history"), named(roll, "markers"), named(macros, "zones"), named(hex, "lines")};
	const char *const names[] = {"the undo history", "the markers", "the macro zones", "the Hex Editor's lines"};
	int resets[4] = {}, changes[4] = {};
	std::vector<Connection> watches;
	for (int i = 0; i < 4; i++)
	{
		CHECK(lists[i] != nullptr);
		if (lists[i] == nullptr) return true;
		watches.push_back(lists[i]->reset.connect([&resets, i] { resets[i]++; }));
		watches.push_back(lists[i]->changed.connect([&changes, i](int64_t, int64_t) { changes[i]++; }));
	}
	/* the window closed: the Hex Editor reads nothing as frames run (once
	 * the read a game's opening asks for is done) */
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() >= 5; }, 20));
	resets[3] = changes[3] = 0;
	CHECK(d.until([&] { return session.frame.get() >= 15; }, 20));
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 10));
	CHECK(changes[3] == 0 && resets[3] == 0);
	/* open: read after each frame; and a first stretch played, whose
	 * first frame starts a step of its own (as many steps as before no more) */
	hex.watching.set(true);
	session.run("play", {}, error);
	CHECK(d.until([&] { return session.frame.get() >= 15; }, 20));
	for (int i = 0; i < 4; i++) resets[i] = changes[i] = 0;
	const int64_t from = session.frame.get();
	CHECK(d.until([&] { return session.frame.get() >= from + 30; }, 20));
	session.run("pause", {}, error);
	CHECK(d.until([&] { return !session.running.get(); }, 10));
	for (int i = 0; i < 4; i++)
	{
		if (resets[i] != 0 || changes[i] == 0) std::printf("FAIL %s: %d resets, %d changes in 30 frames\n", names[i], resets[i], changes[i]);
		CHECK(resets[i] == 0 && changes[i] > 0);
	}
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir, ec);
	return true;
}

/* A core that mixes its sound at 48000 Hz is heard at 44100 (issue 372):
 * each frame hands the speakers 44100/48000 of what the core made, so the
 * sound keeps its pitch and nothing runs ahead to be dropped. The window
 * had played every sample as if at 44100, a semitone and a half low. */
bool soundRate(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	if (fs::file_size(package, ec) == 0 || ec)
	{
		std::printf("SKIP the sound rate: no package that mixes at 48000 Hz\n");
		return true;
	}
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	std::string error;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-rate-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	session.cacheFolder.set((dir / "cache").string());
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	if (session.sound.get() == "Sound on")
	{
		const int64_t f0 = session.frame.get(), q0 = session.soundQueued.get();
		session.run("play", {}, error);
		CHECK(d.until([&] { return session.frame.get() >= f0 + 120; }, 20));
		session.run("pause", {}, error);
		CHECK(d.until([&] { return !session.running.get(); }, 5));
		d.until([] { return false; }, 0.2);
		const int64_t frames = session.frame.get() - f0, pairs = session.soundQueued.get() - q0;
		const double each = frames > 0 ? static_cast<double>(pairs) / static_cast<double>(frames) : 0.0;
		std::printf("a 48000 Hz core: %.1f sample pairs a frame reach the speakers (the core makes 735)\n", each);
		CHECK(std::fabs(each - 735.0 * 44100.0 / 48000.0) < 4.0);
	}
	else
		std::printf("SKIP the sound rate: %s\n", session.sound.get().c_str());
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	fs::remove_all(dir, ec);
	return true;
}

bool squarePixels(const char *declared, const char *square, const char *rom)
{
	const int before = failures;
	std::FILE *f = std::fopen(square, "rb");
	if (f == nullptr || std::fgetc(f) == EOF)
	{
		if (f != nullptr) std::fclose(f);
		std::printf("square pixels: SKIPPED (no package)\n");
		return true;
	}
	std::fclose(f);
	auto shapeOf = [&](const char *package) {
		ManualDispatcher d;
		InputLog inputs;
		SessionViewModel session(d, inputs);
		session.turboSeek.set(true);
		std::string error;
		openGame(session, package, std::string(rom), error);
		d.until([&] { return session.isOpen.get(); }, 60);
		session.run("frameAdvance", {}, error);
		Picture::Frame frame;
		d.until([&] { return session.screen.takeIfNewer(0, frame) && frame.width > 0; }, 20);
		session.run("close", {}, error);
		d.until([&] { return !session.isOpen.get(); }, 20);
		return frame;
	};
	const Picture::Frame squareFrame = shapeOf(square), declaredFrame = shapeOf(declared);
	std::printf("square pixels: %d wide, shown %gx%g; declared 256x240: %d wide, shown %gx%g\n", squareFrame.width,
		squareFrame.shownWidth, squareFrame.shownHeight, declaredFrame.width, declaredFrame.shownWidth, declaredFrame.shownHeight);
	/* drawn 128x120 in room for 320x120: shown 128x120, not 320:120 */
	CHECK(squareFrame.width == 128 && squareFrame.shownWidth == 128 && squareFrame.shownHeight == 120);
	/* a declared size is the size it is shown at, whatever its picture's (issue 414) */
	CHECK(declaredFrame.shownWidth == 256 && declaredFrame.shownHeight == 240);
	return failures == before;
}

/* TAStudio's side as Chimera's (2026-10-25): Turbo seek off, Chimera's first
 * choice, plays a seek past the greenzone at the machine's own speed and stops
 * on the frame, while one inside it is at once; on, every seek is at once. A
 * marker moves to another frame, not onto one that has a marker, and the run's
 * own do not move; a branch dragged by its number changes places with another,
 * the branch loaded staying the one loaded; a frame's zone colours the lists */
bool tastudioPanel(const char *package, const char *rom)
{
	namespace fs = std::filesystem;
	const int before = failures;
	const fs::path dir = fs::temp_directory_path() / ("chimera-test-panel-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	fs::create_directories(dir);
	fs::copy_file(rom, dir / "walker.testrom");
	ManualDispatcher d;
	InputLog inputs;
	SessionViewModel session(d, inputs);
	PianoRollViewModel roll(d, inputs, session);
	BranchesViewModel branches(d, inputs, session, roll);
	std::string error;
	CHECK(!session.turboSeek.get());
	/* off until asked, as Chimera's TAStudio had it (issue 426) */
	CHECK(!session.autoRestore.get());
	openGame(session, package, (dir / "walker.testrom").string(), error);
	CHECK(d.until([&] { return session.isOpen.get(); }, 60));
	const fs::path project = dir / "panel.chimeraProject";
	session.run("saveProject", {project.string()}, error);
	CHECK(d.until([&] { return session.hasProject.get() && session.projectPath.get() == project.string(); }, 20));
	CHECK(roll.run("fill", {int64_t{150}}, error).has_value());
	auto seconds = [](std::chrono::steady_clock::time_point since) {
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - since).count();
	};
	/* past the greenzone: played, about a second for sixty frames */
	auto started = std::chrono::steady_clock::now();
	CHECK(session.run("seek", {int64_t{60}}, error).has_value());
	CHECK(d.until([&] { return session.seekingTo.get() == 60 && session.running.get(); }, 5));
	CHECK(d.until([&] { return session.frame.get() == 60 && !session.running.get() && session.seekingTo.get() == -1; }, 20));
	const double played = seconds(started);
	/* inside it: at once, with nothing played */
	started = std::chrono::steady_clock::now();
	CHECK(session.run("seek", {int64_t{20}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 20 && !session.running.get(); }, 20));
	const double inside = seconds(started);
	CHECK(session.seekingTo.get() == -1);
	/* Turbo seek on: past the greenzone at once too */
	session.turboSeek.set(true);
	started = std::chrono::steady_clock::now();
	CHECK(session.run("seek", {int64_t{140}}, error).has_value());
	CHECK(d.until([&] { return session.frame.get() == 140 && !session.running.get(); }, 20));
	const double turbo = seconds(started);
	std::printf("panel: a seek of 60 frames played in %.2f s, one inside the greenzone %.2f s, 80 frames with Turbo seek %.2f s\n",
		played, inside, turbo);
	CHECK(played > 0.6 && inside < 0.5 && turbo < 0.5);
	/* the columns headed as Chimera's: the package's header for a button, else its letter */
	RowModel *frames = nullptr;
	for (const auto &r : roll.rowLists())
		if (r.name == "frames") frames = r.model;
	CHECK(frames != nullptr);
	auto headerOf = [&](const std::string &button) {
		for (int32_t c = 0; frames != nullptr && c < frames->columnCount(); c++)
			if (frames->columnName(c) == button) return frames->columnShortName(c);
		return std::string("?");
	};
	CHECK(headerOf("P1 Up") == "U" && headerOf("P1 Select") == "SEL" && headerOf("P1 Start") == "S");
	/* a frame's zone: run (green or lag) or not yet */
	std::string zone;
	CHECK(fromValue(roll.run("zoneOf", {int64_t{10}}, error).value_or(Value()), zone) && (zone == "green" || zone == "lag"));
	CHECK(fromValue(roll.run("zoneOf", {int64_t{149}}, error).value_or(Value()), zone) && zone.empty());
	/* markers: moved, not onto another, the run's own not at all */
	RowModel *markers = nullptr;
	for (const auto &r : roll.rowLists())
		if (r.name == "markers") markers = r.model;
	CHECK(markers != nullptr);
	auto markerAt = [&](int64_t frame) {
		for (int64_t i = 0; markers != nullptr && i < markers->rowCount(); i++)
		{
			int64_t f = -1;
			if (fromValue(markers->cell(i, 0), f) && f == frame) return true;
		}
		return false;
	};
	CHECK(roll.run("addMarker", {int64_t{5}, std::string("five")}, error).has_value());
	CHECK(roll.run("addMarker", {int64_t{9}, std::string("nine")}, error).has_value());
	CHECK(roll.run("moveMarker", {int64_t{5}, int64_t{7}}, error).has_value() && markerAt(7) && !markerAt(5));
	CHECK(!roll.run("moveMarker", {int64_t{7}, int64_t{9}}, error) && error == "frame 9 has a marker already" && markerAt(7));
	CHECK(!roll.run("moveMarker", {int64_t{0}, int64_t{3}}, error) && error == "frame 0's marker is the run's own: it cannot be moved");
	/* Chimera's right-button drags on the frame numbers, each one undo step: frames
	 * 30, 31, 32 hold buttons 0, 1, 2, and 33 and on nothing */
	CHECK(frames != nullptr);
	auto held = [&](int64_t frame) {
		int mask = 0;
		for (int32_t b = 0; b < 3; b++)
		{
			bool on = false;
			if (frames != nullptr && fromValue(frames->cell(frame, b), on) && on) mask |= 1 << b;
		}
		return mask;
	};
	for (int64_t b = 0; b < 3; b++) CHECK(roll.run("paint", {30 + b, 30 + b, b, true}, error).has_value());
	CHECK(held(30) == 1 && held(31) == 2 && held(32) == 4 && held(33) == 0);
	RowModel *history = nullptr;
	for (const auto &r : roll.rowLists())
		if (r.name == "history") history = r.model;
	CHECK(history != nullptr);
	const int64_t steps = history != nullptr ? history->rowCount() : 0;
	auto lastStep = [&] {
		std::string name;
		if (history != nullptr && history->rowCount() > 0) fromValue(history->cell(history->rowCount() - 1, 0), name);
		return name;
	};
	auto drag = [&](int64_t from, bool ctrl, bool shift, bool alt, std::vector<int64_t> to) {
		bool on = false;
		fromValue(roll.run("rightDragStart", {from, ctrl, shift, alt}, error).value_or(Value()), on);
		for (int64_t t : to) roll.run("rightDragTo", {t}, error);
		roll.run("rightDragEnd", {}, error);
		return on;
	};
	/* Shift: the input pressed on extended over the frames passed */
	CHECK(drag(30, false, true, false, {31, 32, 33}));
	CHECK(held(30) == 1 && held(31) == 1 && held(32) == 1 && held(33) == 1 && held(34) == 0);
	CHECK(history->rowCount() == steps + 1 && lastStep() == "Right-Click Edit: Extend Input");
	CHECK(roll.run("undo", {}, error).has_value() && held(31) == 2 && held(32) == 4 && held(33) == 0);
	/* Ctrl: a copy where the pointer is, the copy before it taken back as the drag goes on */
	CHECK(drag(30, true, false, false, {40, 41}));
	CHECK(held(30) == 1 && held(40) == 0 && held(41) == 1 && lastStep() == "Right-Click Edit: Copy");
	CHECK(roll.run("undo", {}, error).has_value() && held(41) == 0);
	/* Alt: moved, the frames between taking its place */
	CHECK(drag(32, false, false, true, {31, 30}));
	CHECK(held(30) == 4 && held(31) == 1 && held(32) == 2 && lastStep() == "Right-Click Edit: Move");
	CHECK(roll.run("undo", {}, error).has_value() && held(30) == 1 && held(31) == 2 && held(32) == 4);
	/* Ctrl and Shift: copies put in after it, as many as frames passed, fewer when the drag comes back */
	const int64_t length = roll.frameCount.get();
	CHECK(drag(30, true, true, false, {31, 32, 31}));
	CHECK(roll.frameCount.get() == length + 1 && held(30) == 1 && held(31) == 1 && held(32) == 2 && held(33) == 4);
	CHECK(lastStep() == "Right-Click Edit: Extend Input, Insert");
	CHECK(roll.run("undo", {}, error).has_value() && roll.frameCount.get() == length && held(31) == 2);
	/* no key held: no edit, and the menu opens instead */
	CHECK(!drag(30, false, false, false, {31}) && held(31) == 2);
	/* branches: dragged by the number into another's place */
	CHECK(branches.run("create", {std::string("first")}, error).has_value());
	CHECK(branches.run("create", {std::string("second")}, error).has_value());
	CHECK(d.until([&] { return branches.count.get() == 2; }, 10));
	const int64_t loaded = branches.current.get();
	CHECK(loaded == 1);
	CHECK(branches.run("swap", {int64_t{0}, int64_t{1}}, error).has_value());
	std::string list;
	CHECK(fromValue(branches.run("list", {}, error).value_or(Value()), list));
	CHECK(list.find("\tsecond\n") < list.find("\tfirst\n") && branches.current.get() == 0 && branches.selected.get() == 1);
	CHECK(!branches.run("swap", {int64_t{0}, int64_t{5}}, error));
	session.run("close", {}, error);
	CHECK(d.until([&] { return !session.isOpen.get(); }, 20));
	return failures == before;
}

/* what the status bar says beside a drive that swaps */
void driveMediaText()
{
	const std::vector<std::string> two{"Disc 1.iso", "Disc 2.iso"};
	CHECK(SessionViewModel::driveMediaShort(two, 0, 0) == "1/2 Disc 1.iso");
	CHECK(SessionViewModel::driveMediaShort(two, 1, 0) == "1/2 Disc 1.iso | selected 2/2 Disc 2.iso");
	CHECK(SessionViewModel::driveMediaLong("CD-ROM", two, 1, 0) == "CD-ROM - in the drive: 1/2 Disc 1.iso; selected, not inserted: 2/2 Disc 2.iso");
	CHECK(SessionViewModel::driveMediaShort(two, 1, -1) == "empty | selected 2/2 Disc 2.iso");
	CHECK(SessionViewModel::driveMediaLong("CD", two, 1, -1).rfind("CD - the drive is empty", 0) == 0);
	/* long names cut in the middle: four disks of one game still read as four */
	const std::vector<std::string> hayate{"Zikusousakan Hayate (1994)(Altacia)(Disk 1 of 4)(Disk A).fdi",
		"Zikusousakan Hayate (1994)(Altacia)(Disk 2 of 4)(Disk B).fdi", "Zikusousakan Hayate (1994)(Altacia)(Disk 3 of 4)(Disk C).fdi",
		"Zikusousakan Hayate (1994)(Altacia)(Disk 4 of 4)(Disk D).fdi"};
	std::set<std::string> shown;
	for (int32_t i = 0; i < 4; i++)
	{
		const std::string text = SessionViewModel::driveMediaShort(hayate, i, i);
		CHECK(text.size() <= 28 + 4);
		CHECK(text.size() >= 12 && text.compare(text.size() - 12, 12, std::string("(Disk ") + static_cast<char>('A' + i) + ").fdi") == 0);
		shown.insert(text);
	}
	CHECK(shown.size() == 4);
	CHECK(SessionViewModel::driveMediaLong("Floppy Disk", hayate, 1, 1).find(hayate[1]) != std::string::npos);
	/* a path is its file's name, and an index outside the list is not a name */
	CHECK(SessionViewModel::driveMediaShort({"/mnt/discs/game.iso"}, 0, 0) == "1/1 game.iso");
	CHECK(SessionViewModel::driveMediaShort({"a"}, 7, 7) == "?");
}

int main(int argc, char **argv)
{
	signalsAndProperties();
	registry();
	bindings();
	lua();
	frameColumnColours();
	playerEdges();
	driveMediaText();
	luaConsole();
	toolWindows();
	jsonNumbers();
	configStore();
	configNamesList();
	firmware();
	settingsByMachine();
	settingTypes();
	luaForAgent();
	keyPriority();
	modifierRewrite();
	messagesConfig();
	displayFit();
	pathsConfig();
	dataFolder();
	precompiledModules();
	pianoRollWithoutAMachine();
	watchValues();
	themes();
	europeanClock();
	crashNotes();
	idleEye();
	nutMuxer();
	const bool ranPads = gamepads();
	if (ranPads) padCatch();
	bool ranMachine = false;
	if (argc >= 3)
	{
		FILE *p = std::fopen(argv[1], "rb"), *r = std::fopen(argv[2], "rb");
		if (p != nullptr && r != nullptr)
		{
			ranMachine = machine(argv[1], argv[2]);
			ranMachine = ramSearch(argv[1], argv[2]) && ranMachine;
			ranMachine = settings(argv[1], argv[2]) && ranMachine;
			ranMachine = newProject(argv[1], argv[2]) && ranMachine;
			ranMachine = rerecordCount(argv[1], argv[2]) && ranMachine;
			ranMachine = wheelSeek(argv[1], argv[2]) && ranMachine;
			ranMachine = recordingMode(argv[1], argv[2]) && ranMachine;
			ranMachine = freezes(argv[1], argv[2]) && ranMachine;
			ranMachine = scriptsOnMachine(argv[1], argv[2]) && ranMachine;
			ranMachine = turnsReplacedWhileOneRuns(argv[1], argv[2]) && ranMachine;
			ranMachine = ramWatch(argv[1], argv[2]) && ranMachine;
			ranMachine = hexEditor(argv[1], argv[2]) && ranMachine;
			ranMachine = pianoRollEditing(argv[1], argv[2]) && ranMachine;
			ranMachine = controlsAndHotkeys(argv[1], argv[2]) && ranMachine;
			ranMachine = greenzoneChoice(argv[1], argv[2]) && ranMachine;
			ranMachine = tastudioPanel(argv[1], argv[2]) && ranMachine;
			ranMachine = speedAndSound(argv[1], argv[2]) && ranMachine;
			ranMachine = overlay(argv[1], argv[2]) && ranMachine;
			ranMachine = projectDetails(argv[1], argv[2]) && ranMachine;
			ranMachine = pianoRollColumns(argv[1], argv[2]) && ranMachine;
			ranMachine = backupAndHistory(argv[1], argv[2]) && ranMachine;
			ranMachine = crashRecovery(argv[1], argv[2]) && ranMachine;
			ranMachine = customizeOptions(argv[1], argv[2]) && ranMachine;
			ranMachine = greenzoneBudgets(argv[1], argv[2]) && ranMachine;
			ranMachine = traceAndRegisters(argv[1], argv[2]) && ranMachine;
			ranMachine = macroTool(argv[1], argv[2]) && ranMachine;
			ranMachine = luaLibraries(argv[1], argv[2]) && ranMachine;
			ranMachine = luaParity(argv[1], argv[2]) && ranMachine;
			ranMachine = turboSeekCancel(argv[1], argv[2]) && ranMachine;
			ranMachine = encodeVideo(argv[1], argv[2]) && ranMachine;
			ranMachine = luaTastudio(argv[1], argv[2]) && ranMachine;
			ranMachine = throttles(argv[1], argv[2]) && ranMachine;
			ranMachine = autoholdAndAutofire(argv[1], argv[2]) && ranMachine;
			ranMachine = helpMenu(argv[1], argv[2]) && ranMachine;
			ranMachine = mediaMaker() && ranMachine;
			ranMachine = cacheManager(argv[1], argv[2]) && ranMachine;
			ranMachine = tastudioSettings(argv[1], argv[2]) && ranMachine;
			if (argc >= 5) ranMachine = project(argv[1], argv[2], argv[3], argv[4]) && ranMachine;
			else std::printf("SKIP the project tests: no movies and goldens folders given\n");
			if (argc >= 6) ranMachine = openByKind(argv[1], argv[2], argv[5]) && ranMachine;
			else std::printf("SKIP choosing between cores: no second package given\n");
			if (argc >= 7) ranMachine = newProjectWizard(argv[1], argv[2], argv[6]) && ranMachine;
			if (argc >= 7) ranMachine = pianoRollAxes(argv[6], argv[2]) && ranMachine;
			if (argc >= 7) ranMachine = movieImport(argv[6], argv[2], argv[3]) && ranMachine;
			else std::printf("SKIP New Project: no slots package given\n");
			if (argc >= 9) ranMachine = customFirmware(argv[2], argv[7], argv[8]) && ranMachine;
			else std::printf("SKIP custom firmware: no packages with a hashed firmware given\n");
			if (argc >= 10) ranMachine = columnsMenu(argv[9], argv[2]) && ranMachine;
			else std::printf("SKIP the Columns menu: no package with players and keys given\n");
			if (argc >= 11) ranMachine = coreMenu(argv[1], argv[2], argv[6], argv[10]) && ranMachine;
			else std::printf("SKIP the Emulator menu: no packages that import movies and are games given\n");
			if (argc >= 12) ranMachine = firmwareWindow(argv[1], argv[6], argv[11], argv[8]) && ranMachine;
			else std::printf("SKIP the Firmware window: no package with many releases of one firmware given\n");
			if (argc >= 9) ranMachine = createForAgent(argv[1], argv[2], argv[7]) && ranMachine;
			if (argc >= 13) ranMachine = compileStep(argv[12], argv[2]) && ranMachine;
			else std::printf("SKIP the compile step: no compiling package given\n");
			if (argc >= 14) ranMachine = squarePixels(argv[1], argv[13], argv[2]) && ranMachine;
			if (argc >= 15) ranMachine = soundRate(argv[14], argv[2]) && ranMachine;
			if (argc >= 16) ranMachine = wideButtons(argv[15], argv[2]) && ranMachine;
			ranMachine = listsWhilePlaying(argv[1], argv[2]) && ranMachine;
			if (argc >= 9) ranMachine = coreManager(argv[1], argv[6], argv[8]) && ranMachine;
		}
		else
			std::printf("SKIP the machine tests: %s or %s is missing\n", argv[1], argv[2]);
		if (p) std::fclose(p);
		if (r) std::fclose(r);
	}
	else
		std::printf("SKIP the machine tests: no package and rom given\n");
	std::error_code scriptsGone;
	std::filesystem::remove_all(scriptFolder(), scriptsGone);
	std::printf("%d checks, %d failed%s%s\n", checks, failures, ranMachine ? "" : " (machine tests skipped)",
		ranPads ? "" : " (gamepad tests skipped)");
	return failures == 0 ? 0 : 1;
}
