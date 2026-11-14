/* A project's tool windows and their files (issue 587): what ProjectWindows
 * says is open follows the tool windows shown, RAM Watch's file and the Lua
 * Console's scripts; and a layout the session gives back when a project opens
 * shows its windows again and opens its scripts, one already listed not twice.
 * Stand-in windows by the main window's tool windows' names, over the view
 * models the program uses.
 *   test-project-windows <work folder> */
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTest>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "chimera/vm/input_log.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "../project_windows.hpp"

namespace fs = std::filesystem;
using namespace chimera;

namespace {

int failures = 0, checks = 0;
#define CHECK(c) \
	do { \
		checks++; \
		if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } \
	} while (0)

/* the view models' work, into Qt's event loop, as the window has it */
class QtDispatcher : public vm::Dispatcher
{
public:
	explicit QtDispatcher(QObject *context) : context_(context) {}
	void post(std::function<void()> fn) override { QMetaObject::invokeMethod(context_, std::move(fn), Qt::QueuedConnection); }

private:
	QObject *context_;
};

/* a layout's member, as written */
std::string member(const std::string &layout, const char *name)
{
	vm::json::Json l;
	std::string error;
	if (!vm::json::parse(layout, l, error)) return "?";
	const vm::json::Json *m = l.find(name);
	return m != nullptr ? vm::json::write(*m) : "?";
}

/* the file names of a layout's scripts, a comma between: the Lua Console
 * keeps a path in its own form (on Windows its separators may not be the
 * ones a test wrote), so the names are compared */
std::string scriptNames(const std::string &layout)
{
	vm::json::Json l;
	std::string error, out;
	if (!vm::json::parse(layout, l, error)) return "?";
	if (const vm::json::Json *s = l.find("luaScripts"))
		for (const vm::json::Json &p : s->items) out += (out.empty() ? "" : ",") + fs::path(p.text).filename().string();
	return out;
}

} // namespace

int main(int argc, char **argv)
{
	QGuiApplication app(argc, argv);
	if (argc < 2)
	{
		std::fprintf(stderr, "usage: test-project-windows <work folder>\n");
		return 2;
	}
	const fs::path work = argv[1];
	std::error_code ec;
	fs::remove_all(work, ec);
	fs::create_directories(work);
	auto write = [&](const char *name, const std::string &text) {
		std::ofstream(work / name, std::ios::binary) << text;
		return (work / name).string();
	};
	const std::string one = write("one.lua", "print('one ran')\n"), two = write("two.lua", "print('two ran')\n");
	const std::string watches = write("kept.wch", "");

	QtDispatcher d(&app);
	vm::InputLog inputs;
	vm::SessionViewModel session(d, inputs);
	vm::RamWatchViewModel ramWatch(d, session);
	vm::LuaBridge lua;
	vm::LuaConsoleViewModel console(d, lua);
	console.disableOnLoad.set(true);

	/* the main window's tool windows, by their names, hidden at first */
	QObject mainWindow;
	std::map<std::string, std::unique_ptr<QQuickWindow>> windows;
	for (const std::string &tool : qt::ProjectWindows::tools())
	{
		auto w = std::make_unique<QQuickWindow>();
		w->setObjectName(qt::ProjectWindows::objectNameOf(tool));
		static_cast<QObject *>(w.get())->setParent(&mainWindow);
		windows[tool] = std::move(w);
	}
	qt::ProjectWindows projectWindows(&mainWindow, session, ramWatch, console);
	std::string error;

	/* nothing open */
	CHECK(member(projectWindows.now(), "tools") == "[]");
	CHECK(member(projectWindows.now(), "luaScripts") == "[]");
	/* RAM Watch and the Lua Console shown, a watch file opened, a script listed */
	windows["ramWatch"]->show();
	windows["luaConsole"]->show();
	CHECK(member(projectWindows.now(), "tools") == R"(["ramWatch", "luaConsole"])");
	CHECK(ramWatch.run("open", {watches, false}, error).has_value());
	CHECK(member(projectWindows.now(), "ramWatchFile") == vm::json::quote(watches));
	CHECK(console.run("openScript", {one}, error).has_value());
	CHECK(QTest::qWaitFor([&] { return scriptNames(projectWindows.now()) == "one.lua"; }, 2000));
	std::printf("open: %s\n", projectWindows.now().c_str());

	/* everything hidden; the session gives back a layout as a project opens:
	 * the Hex Editor and the Lua Console shown, RAM Watch not; the script
	 * already listed is not listed again, the other is opened */
	for (auto &w : windows) w.second->hide();
	const std::string layout = R"({"tools": ["hexEditor", "luaConsole"], "ramWatchFile": "", "luaScripts": [)" + vm::json::quote(one) + ", " +
							   vm::json::quote(two) + "]}";
	session.windowsKept.set(layout);
	CHECK(windows["hexEditor"]->isVisible() && windows["luaConsole"]->isVisible() && !windows["ramWatch"]->isVisible());
	CHECK(QTest::qWaitFor([&] { return scriptNames(projectWindows.now()) == "one.lua,two.lua"; }, 2000));
	std::printf("reopened: %s\n", projectWindows.now().c_str());

	std::printf("%d checks, %d failed\n", checks, failures);
	return failures == 0 ? 0 : 1;
}
