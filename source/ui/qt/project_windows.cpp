#include "project_windows.hpp"

#include <algorithm>

#include "chimera/vm/json.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/rows.hpp"
#include "chimera/vm/session_vm.hpp"

namespace chimera::qt {

namespace {

/* the Lua Console's list of scripts, by its registry name */
vm::RowModel *scriptRows(vm::LuaConsoleViewModel &console)
{
	for (const auto &r : console.rowLists())
		if (r.name == "scripts") return r.model;
	return nullptr;
}

/* the paths of the scripts listed, separators left out (columns: name, path,
 * state, separator, selected) */
std::vector<std::string> scriptPaths(vm::LuaConsoleViewModel &console)
{
	std::vector<std::string> out;
	vm::RowModel *rows = scriptRows(console);
	if (rows == nullptr) return out;
	for (int64_t r = 0; r < rows->rowCount(); r++)
	{
		bool separator = false;
		std::string path;
		vm::fromValue(rows->cell(r, 3), separator);
		vm::fromValue(rows->cell(r, 1), path);
		if (!separator && !path.empty()) out.push_back(path);
	}
	return out;
}

/* the same file, whichever separators a path was written with: on Windows
 * the Lua Console keeps a path in its own form, which may not be the one a
 * layout or a person wrote */
bool samePath(std::string a, std::string b)
{
	std::replace(a.begin(), a.end(), '\\', '/');
	std::replace(b.begin(), b.end(), '\\', '/');
	return a == b;
}

} // namespace

const std::vector<std::string> &ProjectWindows::tools()
{
	/* the Tools menu's windows every core has */
	static const std::vector<std::string> names{"tastudio", "ramWatch", "ramSearch", "luaConsole", "hexEditor", "macroTool"};
	return names;
}

ProjectWindows::ProjectWindows(QObject *mainWindow, vm::SessionViewModel &session, vm::RamWatchViewModel &ramWatch,
	vm::LuaConsoleViewModel &luaConsole)
	: main_(mainWindow), session_(session), ramWatch_(ramWatch), luaConsole_(luaConsole)
{
	for (const std::string &tool : tools())
		if (QQuickWindow *w = window(tool)) connect(w, &QWindow::visibleChanged, this, [this] { tell(); });
	watches_.push_back(ramWatch_.path.changed.connect([this] { tell(); }));
	if (vm::RowModel *rows = scriptRows(luaConsole_))
	{
		watches_.push_back(rows->reset.connect([this] { tell(); }));
		watches_.push_back(rows->changed.connect([this](int64_t, int64_t) { tell(); }));
		watches_.push_back(rows->appended.connect([this](int64_t, int64_t) { tell(); }));
	}
	/* a project that reopens its windows has just opened */
	watches_.push_back(session_.windowsKept.changed.connect([this] {
		const std::string layout = session_.windowsKept.get();
		if (!layout.empty()) reopen(layout);
	}));
	tell();
}

QString ProjectWindows::objectNameOf(const std::string &tool)
{
	static const std::vector<std::pair<std::string, QString>> objectNames{{"tastudio", QStringLiteral("tastudioWindow")},
		{"ramWatch", QStringLiteral("ramWatchWindow")}, {"ramSearch", QStringLiteral("ramSearchWindow")},
		{"luaConsole", QStringLiteral("luaConsoleWindow")}, {"hexEditor", QStringLiteral("hexEditorWindow")},
		{"macroTool", QStringLiteral("macroToolWindow")}};
	for (const auto &[name, objectName] : objectNames)
		if (name == tool) return objectName;
	return {};
}

QQuickWindow *ProjectWindows::window(const std::string &tool) const
{
	const QString name = objectNameOf(tool);
	return main_ != nullptr && !name.isEmpty() ? main_->findChild<QQuickWindow *>(name) : nullptr;
}

std::string ProjectWindows::now() const
{
	namespace json = vm::json;
	json::Json open;
	open.kind = json::Json::Kind::Array;
	for (const std::string &tool : tools())
		if (QQuickWindow *w = window(tool); w != nullptr && w->isVisible()) open.items.push_back(json::Json::string(tool));
	json::Json scripts;
	scripts.kind = json::Json::Kind::Array;
	for (const std::string &path : scriptPaths(luaConsole_)) scripts.items.push_back(json::Json::string(path));
	json::Json layout;
	layout.kind = json::Json::Kind::Object;
	layout.members.emplace_back("tools", std::move(open));
	layout.members.emplace_back("ramWatchFile", json::Json::string(ramWatch_.path.get()));
	layout.members.emplace_back("luaScripts", std::move(scripts));
	return json::write(layout);
}

void ProjectWindows::reopen(const std::string &layout)
{
	namespace json = vm::json;
	json::Json l;
	std::string error;
	if (!json::parse(layout, l, error) || l.kind != json::Json::Kind::Object) return;
	/* what opening says back is not news to the session */
	reopening_ = true;
	if (const json::Json *open = l.find("tools"))
		for (const json::Json &t : open->items)
			if (QQuickWindow *w = window(t.text))
			{
				w->show();
				w->raise();
			}
	if (const json::Json *file = l.find("ramWatchFile"); file != nullptr && !file->text.empty() && file->text != ramWatch_.path.get())
		ramWatch_.run("open", {file->text, false}, error);
	if (const json::Json *scripts = l.find("luaScripts"))
	{
		const std::vector<std::string> listed = scriptPaths(luaConsole_);
		for (const json::Json &s : scripts->items)
			if (!s.text.empty() && std::none_of(listed.begin(), listed.end(), [&](const std::string &l) { return samePath(l, s.text); }))
				luaConsole_.run("openScript", {s.text}, error);
	}
	reopening_ = false;
	tell();
}

void ProjectWindows::tell()
{
	if (reopening_) return;
	std::string error;
	session_.run("windowsNow", {now()}, error);
}

} // namespace chimera::qt
