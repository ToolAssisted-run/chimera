#pragma once
/* A project's tool windows (issue 587): which of them are open, RAM Watch's
 * file and the Lua Console's scripts, said to the session as they change (it
 * keeps them for a project that reopens its windows), and opened again from
 * what the session gives back when such a project opens. The windows are the
 * main window's tool windows, found by their object names as --show finds
 * them; nothing here knows a core or a system. */
#include <QObject>
#include <QPointer>
#include <QQuickWindow>

#include <string>
#include <vector>

#include "chimera/vm/observable.hpp"

namespace chimera::vm {
class SessionViewModel;
class RamWatchViewModel;
class LuaConsoleViewModel;
}

namespace chimera::qt {

class ProjectWindows : public QObject
{
public:
	/* the tool windows a project reopens, by the names the layout uses */
	static const std::vector<std::string> &tools();
	/* a tool's window's object name, as its QML gives it; "" for no tool */
	static QString objectNameOf(const std::string &tool);

	ProjectWindows(QObject *mainWindow, vm::SessionViewModel &session, vm::RamWatchViewModel &ramWatch,
		vm::LuaConsoleViewModel &luaConsole);

	/* what is open now, as the layout's JSON: {"tools": [...], "ramWatchFile":
	 * "...", "luaScripts": [...]} */
	std::string now() const;
	/* opens what a layout names: its tool windows shown, RAM Watch's file
	 * opened, each script not already listed opened in the Lua Console */
	void reopen(const std::string &layout);

private:
	QQuickWindow *window(const std::string &tool) const;
	void tell();

	QPointer<QObject> main_;
	vm::SessionViewModel &session_;
	vm::RamWatchViewModel &ramWatch_;
	vm::LuaConsoleViewModel &luaConsole_;
	std::vector<vm::Connection> watches_;
	bool reopening_ = false;
};

} // namespace chimera::qt
