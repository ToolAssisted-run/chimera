#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "chimera/vm/json.hpp"

/* Chimera's own Lua documentation (lua_docs.inc) */
#include "lua_docs.inc"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace {
/* the most lines kept: a script printing every frame would otherwise grow without end */
constexpr size_t kMostLines = 10000;
/* what one frame may write before the rest is not shown, as Chimera's */
constexpr int kMostMessages = 100;

fs::path pathOf(const std::string &s) { return fs::u8path(s); }
std::string text(const fs::path &p)
{
	const std::u8string s = p.u8string();
	return std::string(reinterpret_cast<const char *>(s.data()), s.size());
}

std::string fileName(const std::string &path) { return text(pathOf(path).filename()); }
std::string stem(const std::string &path) { return text(pathOf(path).stem()); }

/* a path as Chimera writes it in a session: "./name.lua" beside the session
 * or under its folder, else as it is */
std::string relativeTo(const std::string &path, const std::string &session)
{
	const fs::path dir = pathOf(session).parent_path();
	const fs::path p = pathOf(path);
	std::error_code ec;
	const fs::path rel = p.lexically_relative(dir);
	if (rel.empty() || *rel.begin() == "..") return path;
#ifdef _WIN32
	fs::path windows = rel;
	return ".\\" + text(windows.make_preferred());
#else
	return "./" + text(rel);
#endif
}

fs::file_time_type writtenAt(const std::string &path)
{
	std::error_code ec;
	const auto t = fs::last_write_time(pathOf(path), ec);
	return ec ? fs::file_time_type{} : t;
}

/* what New Script copies when the Lua folder has no template of its own, as Chimera's */
const char *const kTemplate = "lua_console.template";
} // namespace

LuaConsoleViewModel::LuaConsoleViewModel(Dispatcher &dispatcher, LuaBridge &lua, std::function<void(const std::string &)> echo)
	: ViewModel("luaConsole", dispatcher), lua_(lua), echo_(std::move(echo))
{
	rows("output", "what scripts printed and what failed, a row a line; selected: picked for Copy", rows_);
	rows("scripts", "the scripts, a row each: name, path, state (running, paused or stopped; \"\" for a separator), "
		"separator, selected", scripts_);
	rows("functions", "the functions the scripts registered, by event and name: event, name, guid", functionRows_);
	expose("sessionPath", "the session's file, \"\" until one is opened or saved", sessionPath);
	expose("changes", "the list changed since the session was opened or saved", changes);
	expose("status", "the line under the window: the session's file name (\"* \" before it while changed), or what saving it did", status);
	expose("summary", "how many scripts, how many of them active and paused, as Chimera counts them", summary);
	expose("lines", "lines of output", lines);
	expose("running", "a script is running or paused", running);
	expose("selectedCount", "rows selected", selectedCount);
	expose("selectedScripts", "scripts selected (not separators)", selectedScripts);
	expose("anyEnabled", "a script in the list is running or paused", anyEnabled);
	expose("functionCount", "functions the scripts registered", functionCount);
	expose("lastScript", "the script Toggle Last Lua Script toggles: the last one toggled", lastScript);
	expose("outputFirst", "the first output line selected, -1 for none", outputFirst);
	expose("outputLast", "the last output line selected, -1 for none", outputLast);
	expose("disableOnLoad", "Disable Scripts on Load: a script opened is not started", disableOnLoad, true);
	expose("toggleAllIfNoneSelected", "Toggle All if None Selected: Toggle and Refresh work on every script when none is selected",
		toggleAllIfNoneSelected, true);
	expose("reloadOnChange", "Reload When Script File Changes: a running script whose file is written is started again", reloadOnChange, true);
	expose("recentSessions", "sessions opened or saved, newest first, a line each", recentSessions, true);
	expose("recentScripts", "scripts opened, newest first, a line each", recentScripts, true);
	expose("luaFolder", "the Paths' Lua folder, where New Script's template is", luaFolder, true);

	lua_.onWrite = [this](const std::string &t) { append(t); };
	lua_.onStopped = [this](int64_t id) {
		for (Entry &e : entries_)
			if (e.script == id) e.script = 0;
		refresh();
	};
	changesWatch_ = changes.changed.connect([this] {
		status.set((changes.get() ? "* " : "") + fileName(sessionPath.get()));
	});
	sessionWatch_ = sessionPath.changed.connect([this] {
		status.set((changes.get() ? "* " : "") + fileName(sessionPath.get()));
	});

	/* File */
	command("newSession", "Chimera's New Session: every script stopped and taken out of the list, the console's own "
		"registered functions and the output cleared", {}, [this](const Args &, std::string &) -> Value {
			removeAll();
			lua_.ask("__chimera_registered_clear", {Value(int64_t{0})});
			sessionPath.set("");
			changes.set(false);
			std::string e;
			run("clear", {}, e);
			listChanged();
			refreshFunctions(true);
			return {};
		});
	command("openSession", "the list read from a session file; its scripts marked to run are started (unless Disable Scripts on Load)",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string p;
			if (!arg(a, 0, p, error)) return {};
			if (!loadSession(p, error)) return {};
			return {};
		});
	command("saveSession", "the list written to the session's file", {}, [this](const Args &, std::string &error) -> Value {
		if (sessionPath.get().empty())
		{
			error = tr("lua_console.session_no_file");
			return {};
		}
		saveSession(sessionPath.get(), error);
		return {};
	});
	command("saveSessionAs", "the list written to a session file, which it is from then on", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string p;
		if (!arg(a, 0, p, error)) return {};
		saveSession(p, error);
		return {};
	});

	/* Script */
	command("newScript", "a new script at a path, a copy of the Lua folder's .template.lua (made first if it is not there), "
		"added to the list but not started; its path back, to open in an editor", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string p;
			if (!arg(a, 0, p, error)) return {};
			std::error_code ec;
			std::string body = tr(kTemplate);
			if (!luaFolder.get().empty())
			{
				const fs::path tmpl = pathOf(luaFolder.get()) / ".template.lua";
				if (!fs::exists(tmpl, ec))
				{
					fs::create_directories(pathOf(luaFolder.get()), ec);
					std::ofstream(tmpl, std::ios::binary) << tr(kTemplate);
				}
				std::ifstream in(tmpl, std::ios::binary);
				if (in)
				{
					std::stringstream ss;
					ss << in.rdbuf();
					body = ss.str();
				}
			}
			std::ofstream out(pathOf(p), std::ios::binary);
			out << body;
			if (!out)
			{
				error = tr("session.could_not_write", {p});
				return {};
			}
			out.close();
			openScript(p, false);
			return text(fs::absolute(pathOf(p), ec));
		});
	command("openScript", "a script's file added to the list and started (unless Disable Scripts on Load); one already "
		"in the list is started if it is stopped", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string p;
			if (!arg(a, 0, p, error)) return {};
			openScript(p, true);
			return {};
		});
	command("toggle", "each script selected (or every one, when none is and Toggle All if None Selected is on) started if "
		"stopped, stopped if running or paused", {}, [this](const Args &, std::string &) -> Value {
			for (const size_t i : targets())
				if (i < entries_.size()) toggle(entries_[i]);
			refresh();
			return {};
		});
	command("toggleRow", "the script on a row toggled (a double click)", {"row"}, [this](const Args &a, std::string &error) -> Value {
		int64_t r = 0;
		if (!arg(a, 0, r, error)) return {};
		if (r >= 0 && r < static_cast<int64_t>(entries_.size())) toggle(entries_[static_cast<size_t>(r)]);
		refresh();
		return {};
	});
	command("refresh", "each script toggled twice: a running one started again from its file", {}, [this](const Args &, std::string &) -> Value {
		for (const size_t i : targets())
		{
			if (i >= entries_.size()) continue;
			toggle(entries_[i]);
			if (i < entries_.size()) toggle(entries_[i]);
		}
		refresh();
		return {};
	});
	command("pause", "each script selected paused if running, run on if paused", {}, [this](const Args &, std::string &) -> Value {
		for (const size_t i : selectedRows())
		{
			const Entry &e = entries_[i];
			if (e.separator || e.script == 0) continue;
			lua_.setPaused(e.script, lua_.state(e.script) == LuaBridge::State::Running);
		}
		refresh();
		return {};
	});
	command("editPaths", "the files of the scripts selected, a line each (Edit Script opens them in an editor)", {},
		[this](const Args &, std::string &) -> Value {
			std::string out;
			for (const size_t i : selectedRows())
				if (!entries_[i].separator) out += (out.empty() ? "" : "\n") + entries_[i].path;
			return out;
		});
	command("remove", "the rows selected taken out of the list, their scripts stopped", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<size_t> rows = selectedRows();
		if (rows.empty()) return {};
		for (auto it = rows.rbegin(); it != rows.rend(); ++it)
		{
			if (*it >= entries_.size()) continue;
			const int64_t id = entries_[*it].script;
			if (id != 0) lua_.stop(id);
			if (*it < entries_.size()) entries_.erase(entries_.begin() + static_cast<long>(*it));
		}
		anchor_ = -1;
		markChanged();
		std::string e;
		run("eraseDrawings", {}, e);
		listChanged();
		return {};
	});
	command("duplicateName", "where a copy of the first script selected goes unless told: beside it, \"name (1).lua\"", {},
		[this](const Args &, std::string &) -> Value {
			for (const size_t i : selectedRows())
			{
				if (entries_[i].separator) return std::string();
				const fs::path p = pathOf(entries_[i].path);
				return text(p.parent_path() / (text(p.stem()) + " (1).lua"));
			}
			return std::string();
		});
	command("duplicate", "the first row selected copied: a script's file written to a path and added (not started); a "
		"separator, another added at the end", {"path"}, [this](const Args &a, std::string &error) -> Value {
			const std::vector<size_t> rows = selectedRows();
			if (rows.empty()) return {};
			const Entry e = entries_[rows.front()];
			if (e.separator)
			{
				entries_.push_back(Entry{"", true});
				markChanged();
				listChanged();
				return {};
			}
			std::string p;
			if (!arg(a, 0, p, error)) return {};
			std::ifstream in(pathOf(e.path), std::ios::binary);
			std::stringstream ss;
			ss << in.rdbuf();
			std::ofstream out(pathOf(p), std::ios::binary);
			out << ss.str();
			if (!in || !out)
			{
				error = tr("lua_console.cannot_copy", {e.path, p});
				return {};
			}
			out.close();
			openScript(p, false);
			return {};
		});
	command("clear", "empties the output", {}, [this](const Args &, std::string &) -> Value {
		output_.clear();
		open_ = false;
		outputAnchor_ = -1;
		outputFirst.set(-1);
		outputLast.set(-1);
		lines.set(0);
		rows_.reset.emit();
		return {};
	});
	command("insertSeparator", "a separator put before the first row selected, or at the end", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<size_t> rows = selectedRows();
		const size_t at = rows.empty() ? entries_.size() : rows.front();
		entries_.insert(entries_.begin() + static_cast<long>(at), Entry{"", true});
		markChanged();
		listChanged();
		return {};
	});
	command("moveUp", "the rows selected moved up a row", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<size_t> rows = selectedRows();
		if (rows.empty() || rows.front() == 0) return {};
		for (const size_t i : rows) std::swap(entries_[i - 1], entries_[i]);
		markChanged();
		listChanged();
		return {};
	});
	command("moveDown", "the rows selected moved down a row", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<size_t> rows = selectedRows();
		if (rows.empty() || rows.back() + 1 >= entries_.size()) return {};
		for (auto it = rows.rbegin(); it != rows.rend(); ++it) std::swap(entries_[*it], entries_[*it + 1]);
		markChanged();
		listChanged();
		return {};
	});
	command("select", "a row selected: mode \"only\" (a click), \"toggle\" (Ctrl), \"range\" (Shift, from the last "
		"clicked), or \"none\" (every row unselected)", {"row", "mode"}, [this](const Args &a, std::string &error) -> Value {
			int64_t r = 0;
			std::string mode = "only";
			if (!arg(a, 0, r, error)) return {};
			if (a.size() > 1 && !arg(a, 1, mode, error)) return {};
			const auto n = static_cast<int64_t>(entries_.size());
			if (mode == "none")
			{
				for (Entry &e : entries_) e.selected = false;
				anchor_ = -1;
			}
			else if (r < 0 || r >= n)
			{
				error = tr("cache.no_row", {std::to_string(r)});
				return {};
			}
			else if (mode == "toggle")
			{
				entries_[static_cast<size_t>(r)].selected = !entries_[static_cast<size_t>(r)].selected;
				anchor_ = r;
			}
			else if (mode == "range" && anchor_ >= 0 && anchor_ < n)
			{
				for (int64_t i = 0; i < n; i++) entries_[static_cast<size_t>(i)].selected = i >= std::min(anchor_, r) && i <= std::max(anchor_, r);
			}
			else
			{
				for (Entry &e : entries_) e.selected = false;
				entries_[static_cast<size_t>(r)].selected = true;
				anchor_ = r;
			}
			refresh();
			if (n > 0) scripts_.changed.emit(0, n - 1);
			return {};
		});
	command("selectAll", "every row selected, or none when every one already is (Chimera's Select All)", {}, [this](const Args &, std::string &) -> Value {
		const bool all = std::all_of(entries_.begin(), entries_.end(), [](const Entry &e) { return e.selected; });
		for (Entry &e : entries_) e.selected = !all;
		refresh();
		if (!entries_.empty()) scripts_.changed.emit(0, static_cast<int64_t>(entries_.size()) - 1);
		return {};
	});
	command("stopAll", "every script in the list stopped", {}, [this](const Args &, std::string &) -> Value {
		for (size_t i = 0; i < entries_.size(); i++)
			if (entries_[i].script != 0) lua_.stop(entries_[i].script);
		refresh();
		return {};
	});
	command("sort", "the list sorted by a column (\"Script\" or \"Path\"), the other way round when sorted by it already",
		{"column"}, [this](const Args &a, std::string &error) -> Value {
			std::string column;
			if (!arg(a, 0, column, error)) return {};
			if (column != sortedBy_) sortReverse_ = false;
			const bool byName = column == "Script";
			const bool reverse = sortReverse_;
			std::stable_sort(entries_.begin(), entries_.end(), [byName, reverse](const Entry &x, const Entry &y) {
				const std::string xs = byName ? stem(x.path) : x.path, ys = byName ? stem(y.path) : y.path;
				const std::string xt = byName ? x.path : stem(x.path), yt = byName ? y.path : stem(y.path);
				if (xs != ys) return reverse ? ys < xs : xs < ys;
				return xt < yt;
			});
			sortedBy_ = column;
			sortReverse_ = !sortReverse_;
			listChanged();
			return {};
		});
	command("toggleLast", "Chimera's Toggle Last Lua Script: the script last toggled toggled again; whether it runs now", {},
		[this](const Args &, std::string &) -> Value {
			for (Entry &e : entries_)
				if (!e.separator && e.path == lastScript.get())
				{
					toggle(e);
					refresh();
					break;
				}
			for (const Entry &e : entries_)
				if (!e.separator && e.path == lastScript.get()) return e.script != 0;
			return false;
		});
	command("eraseDrawings", "Erase Stale/Stuck Lua Drawing Layers: what scripts drew over the picture taken off", {},
		[this](const Args &, std::string &) -> Value {
			lua_.ask("__chimera_frame_begin");
			lua_.ask("__chimera_frame_end");
			return {};
		});
	command("checkFiles", "with Reload When Script File Changes, each running script whose file was written since it "
		"started is started again", {}, [this](const Args &, std::string &) -> Value {
			if (!reloadOnChange.get()) return {};
			for (size_t i = 0; i < entries_.size(); i++)
			{
				Entry &e = entries_[i];
				if (e.separator) continue;
				const auto now = writtenAt(e.path);
				if (now == e.written) continue;
				e.written = now;
				if (e.script != 0)
				{
					toggle(entries_[i]);
					if (i < entries_.size()) toggle(entries_[i]);
				}
			}
			refresh();
			return {};
		});
	command("closed", "the console closed: its OnConsoleClose functions called, every script stopped, the list and the "
		"output emptied, as Chimera's", {}, [this](const Args &, std::string &) -> Value {
			lua_.consoleClosed();
			entries_.clear();
			anchor_ = -1;
			sessionPath.set("");
			changes.set(false);
			std::string e;
			run("clear", {}, e);
			run("eraseDrawings", {}, e);
			listChanged();
			refreshFunctions(true);
			return {};
		});

	/* the line typed into */
	command("execute", "a line of Lua run as the console's own: an expression's values, or a statement, printed as "
		"Chimera's console prints them", {"command"}, [this](const Args &a, std::string &error) -> Value {
			std::string c;
			if (!arg(a, 0, c, error)) return {};
			if (c.find_first_not_of(" \t\r\n") == std::string::npos) return {};
			messages_ = 0;
			lua_.execute(c);
			messages_ = 0;
			history_.insert(history_.begin(), c);
			historyAt_ = -1;
			refresh();
			refreshFunctions(false);
			return {};
		});
	command("recall", "a line typed before: step 1 an older one (Up), -1 a newer one (Down); nothing when there is none", {"step"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t step = 0;
			if (!arg(a, 0, step, error)) return {};
			if (step > 0)
			{
				if (historyAt_ + 1 >= static_cast<int64_t>(history_.size())) return {};
				historyAt_++;
				return history_[static_cast<size_t>(historyAt_)];
			}
			if (historyAt_ == 0)
			{
				historyAt_ = -1;
				return std::string();
			}
			if (historyAt_ > 0)
			{
				historyAt_--;
				return history_[static_cast<size_t>(historyAt_)];
			}
			return {};
		});

	/* the output's own selection, for Copy */
	command("selectOutput", "an output line selected: mode \"only\" or \"range\" (from the last one clicked)", {"row", "mode"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t r = 0;
			std::string mode = "only";
			if (!arg(a, 0, r, error)) return {};
			if (a.size() > 1 && !arg(a, 1, mode, error)) return {};
			if (r < 0 || r >= static_cast<int64_t>(output_.size())) return {};
			if (mode == "range" && outputAnchor_ >= 0)
			{
				outputFirst.set(std::min(outputAnchor_, r));
				outputLast.set(std::max(outputAnchor_, r));
			}
			else
			{
				outputAnchor_ = r;
				outputFirst.set(r);
				outputLast.set(r);
			}
			rows_.changed.emit(0, static_cast<int64_t>(output_.size()) - 1);
			return {};
		});
	command("selectAllOutput", "every output line selected", {}, [this](const Args &, std::string &) -> Value {
		if (output_.empty()) return {};
		outputAnchor_ = 0;
		outputFirst.set(0);
		outputLast.set(static_cast<int64_t>(output_.size()) - 1);
		rows_.changed.emit(0, static_cast<int64_t>(output_.size()) - 1);
		return {};
	});
	command("copyOutput", "the output lines selected, as text", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		for (int64_t i = std::max<int64_t>(outputFirst.get(), 0); i <= outputLast.get() && i < static_cast<int64_t>(output_.size()); i++)
			out += output_[static_cast<size_t>(i)] + "\n";
		return out;
	});

	command("restoreDefaults", "the console's settings back to Chimera's: scripts start on load, Toggle works on every "
		"script when none is selected, and files are not watched", {},
		[this](const Args &, std::string &) -> Value {
			disableOnLoad.set(false);
			toggleAllIfNoneSelected.set(true);
			reloadOnChange.set(false);
			return {};
		});
	command("functionsList", "Help > Lua Functions List, as Chimera's: every function Chimera documents, in its words "
		"(return type, library, name, parameters, description, example; deprecated or not), each marked unavailable "
		"when this window has no such function, and its own besides, as [{\"returns\", \"library\", "
		"\"name\", \"params\", \"description\", \"example\", \"deprecated\", \"unavailable\"}]",
		{}, [this](const Args &, std::string &) -> Value {
			return functionsList(lua_.ask("__chimera_functions_list"));
		});

	command("functionNames", "every Lua function's name, a line each, as Chimera's client.getluafunctionslist gives them: "
		"those Chimera documents that are here (and those it documents that raise an error in every core), then the "
		"window's own", {}, [this](const Args &, std::string &) -> Value {
			json::Json docs, ours;
			std::string why, out;
			if (!json::parse(std::string_view(kChimeraLuaDocs), docs, why)) docs.kind = json::Json::Kind::Array;
			if (!json::parse(lua_.ask("__chimera_functions_list"), ours, why)) ours.kind = json::Json::Kind::Array;
			auto field = [](const json::Json &o, const char *k) {
				const json::Json *j = o.find(k);
				return j != nullptr ? j->text : std::string();
			};
			std::vector<std::string> have, documented;
			for (const json::Json &f : ours.items) have.push_back(field(f, "library") + "." + field(f, "name"));
			for (const json::Json &d : docs.items)
			{
				const std::string key = field(d, "library") + "." + field(d, "name");
				documented.push_back(key);
				const json::Json *gone = d.find("unavailable");
				if (std::find(have.begin(), have.end(), key) != have.end() || (gone != nullptr && gone->boolean)) out += field(d, "name") + "\n";
			}
			for (const json::Json &f : ours.items)
				if (std::find(documented.begin(), documented.end(), field(f, "library") + "." + field(f, "name")) == documented.end())
					out += field(f, "name") + "\n";
			return out;
		});

	/* Registered Functions */
	command("callFunction", "a registered function called, by its id", {"guid"}, [this](const Args &a, std::string &error) -> Value {
		std::string g;
		if (!arg(a, 0, g, error)) return {};
		const bool found = lua_.ask("__chimera_registered_call", {Value(g)}) == "true";
		refreshFunctions(false);
		return found;
	});
	command("removeFunction", "a registered function removed, by its id", {"guid"}, [this](const Args &a, std::string &error) -> Value {
		std::string g;
		if (!arg(a, 0, g, error)) return {};
		const bool found = lua_.ask("__chimera_registered_remove", {Value(g)}) == "true";
		refreshFunctions(false);
		refresh();
		return found;
	});
	command("removeAllFunctions", "every registered function of every script and of the console removed", {}, [this](const Args &, std::string &) -> Value {
		lua_.ask("__chimera_registered_clear");
		refreshFunctions(false);
		refresh();
		return {};
	});
}

std::string LuaConsoleViewModel::functionsList(const std::string &windowList)
{
	json::Json docs, ours;
	std::string why;
	if (!json::parse(std::string_view(kChimeraLuaDocs), docs, why)) docs.kind = json::Json::Kind::Array;
	if (!json::parse(windowList, ours, why)) ours.kind = json::Json::Kind::Array;
	auto text = [](const json::Json &o, const char *k) {
		const json::Json *j = o.find(k);
		return j != nullptr ? j->text : std::string();
	};
	auto flag = [](const json::Json &o, const char *k) {
		const json::Json *j = o.find(k);
		return j != nullptr && j->boolean;
	};
	/* what the window's libraries have, by library.name */
	std::vector<std::string> have;
	for (const json::Json &f : ours.items) have.push_back(text(f, "library") + "." + text(f, "name"));
	std::vector<std::string> documented;
	json::Json out;
	out.kind = json::Json::Kind::Array;
	auto entry = [&](const std::string &returns, const std::string &library, const std::string &name, const std::string &params,
					 const std::string &description, const std::string &example, bool deprecated, bool unavailable) {
		json::Json e;
		e.kind = json::Json::Kind::Object;
		e.members.emplace_back("returns", json::Json::string(returns));
		e.members.emplace_back("library", json::Json::string(library));
		e.members.emplace_back("name", json::Json::string(name));
		e.members.emplace_back("params", json::Json::string(params));
		e.members.emplace_back("description", json::Json::string(description));
		e.members.emplace_back("example", json::Json::string(example));
		e.members.emplace_back("deprecated", json::Json::boolean_(deprecated));
		e.members.emplace_back("unavailable", json::Json::boolean_(unavailable));
		out.items.push_back(std::move(e));
	};
	for (const json::Json &d : docs.items)
	{
		const std::string key = text(d, "library") + "." + text(d, "name");
		documented.push_back(key);
		const bool here = std::find(have.begin(), have.end(), key) != have.end();
		entry(text(d, "returns"), text(d, "library"), text(d, "name"), text(d, "params"), text(d, "description"), text(d, "example"),
			flag(d, "deprecated"), flag(d, "unavailable") || !here);
	}
	/* the window's functions with no documentation */
	for (const json::Json &f : ours.items)
	{
		const std::string key = text(f, "library") + "." + text(f, "name");
		if (std::find(documented.begin(), documented.end(), key) != documented.end()) continue;
		entry("", text(f, "library"), text(f, "name"), "(" + text(f, "params") + ")", "", "", false, false);
	}
	return json::write(out);
}

LuaConsoleViewModel::~LuaConsoleViewModel()
{
	lua_.onWrite = nullptr;
	lua_.onStopped = nullptr;
}

void LuaConsoleViewModel::openScript(const std::string &path, bool start)
{
	std::error_code ec;
	const std::string abs = text(fs::absolute(pathOf(path), ec).lexically_normal());
	addRecent(recentScripts, abs);
	for (Entry &e : entries_)
		if (!e.separator && e.path == abs)
		{
			if (e.script == 0 && start && !disableOnLoad.get()) toggle(e);
			refresh();
			return;
		}
	Entry e;
	e.path = abs;
	e.written = writtenAt(abs);
	entries_.push_back(e);
	markChanged();
	listChanged();
	if (start && !disableOnLoad.get()) enable(entries_.back());
	refresh();
}

void LuaConsoleViewModel::enable(Entry &entry)
{
	const std::string path = entry.path;
	std::string error;
	messages_ = 0;
	const int64_t id = lua_.start(path, error);
	if (id == 0 && !error.empty()) line(error);
	const bool alive = id != 0 && lua_.state(id) != LuaBridge::State::Stopped;
	/* the entry found again: the script may have changed the list as it started */
	for (Entry &e : entries_)
		if (!e.separator && e.path == path)
		{
			e.script = alive ? id : 0;
			e.written = writtenAt(path);
		}
}

void LuaConsoleViewModel::toggle(Entry &e)
{
	if (e.separator) return;
	lastScript.set(e.path);
	if (e.script != 0)
		lua_.stop(e.script);
	else
		enable(e);
}

void LuaConsoleViewModel::removeAll()
{
	for (size_t i = 0; i < entries_.size(); i++)
		if (entries_[i].script != 0) lua_.stop(entries_[i].script);
	entries_.clear();
	anchor_ = -1;
}

bool LuaConsoleViewModel::loadSession(const std::string &path, std::string &error)
{
	std::ifstream in(pathOf(path), std::ios::binary);
	if (!in)
	{
		error = tr("new_project.cannot_read", {path});
		return false;
	}
	removeAll();
	std::error_code ec;
	const fs::path dir = fs::absolute(pathOf(path), ec).parent_path();
	std::vector<bool> marked;
	std::string l;
	while (std::getline(in, l))
	{
		if (!l.empty() && l.back() == '\r') l.pop_back();
		if (l.rfind("---", 0) == 0)
		{
			entries_.push_back(Entry{"", true});
			marked.push_back(false);
			continue;
		}
		if (l.size() < 3) continue;
		fs::path p = pathOf(l.substr(2));
		if (!p.is_absolute()) p = dir / p;
		Entry e;
		e.path = text(p.lexically_normal());
		e.written = writtenAt(e.path);
		entries_.push_back(e);
		marked.push_back(l[0] == '1');
	}
	sessionPath.set(text(fs::absolute(pathOf(path), ec)));
	listChanged();
	for (size_t i = 0; i < entries_.size() && i < marked.size(); i++)
	{
		if (entries_[i].separator) continue;
		if (marked[i] && !disableOnLoad.get()) enable(entries_[i]);
		addRecent(recentScripts, entries_[i].path);
	}
	changes.set(false);
	addRecent(recentSessions, sessionPath.get());
	/* the output starts empty with the session, as Chimera's does */
	std::string e;
	run("clear", {}, e);
	refresh();
	return true;
}

bool LuaConsoleViewModel::saveSession(const std::string &path, std::string &error)
{
	std::error_code ec;
	const std::string abs = text(fs::absolute(pathOf(path), ec));
#ifdef _WIN32
	const char *const nl = "\r\n";
#else
	const char *const nl = "\n";
#endif
	std::string body;
	for (const Entry &e : entries_)
	{
		if (e.separator)
			body += std::string("---") + nl;
		else
			body += std::string(e.script != 0 ? "1" : "0") + " " + relativeTo(e.path, abs) + nl;
	}
	std::ofstream out(pathOf(abs), std::ios::binary);
	out << body;
	if (!out)
	{
		error = tr("ram_search.save_failed", {fileName(abs)});
		status.set(tr("lua_console.session_not_saved", {fileName(abs)}));
		return false;
	}
	sessionPath.set(abs);
	changes.set(false);
	addRecent(recentSessions, abs);
	status.set(tr("ram_watch.saved", {fileName(abs)}));
	return true;
}

std::vector<size_t> LuaConsoleViewModel::selectedRows() const
{
	std::vector<size_t> out;
	for (size_t i = 0; i < entries_.size(); i++)
		if (entries_[i].selected) out.push_back(i);
	return out;
}

std::vector<size_t> LuaConsoleViewModel::targets() const
{
	std::vector<size_t> out;
	bool any = false;
	for (size_t i = 0; i < entries_.size(); i++)
		if (entries_[i].selected && !entries_[i].separator) any = true;
	for (size_t i = 0; i < entries_.size(); i++)
		if (!entries_[i].separator && (entries_[i].selected || (!any && toggleAllIfNoneSelected.get()))) out.push_back(i);
	return out;
}

void LuaConsoleViewModel::markChanged()
{
	changes.set(true);
}

void LuaConsoleViewModel::listChanged()
{
	scripts_.reset.emit();
	refresh();
}

void LuaConsoleViewModel::refresh()
{
	refresh(lua_.seen());
}

void LuaConsoleViewModel::refresh(const LuaBridge::Seen &seen)
{
	seen_ = seen;
	int64_t total = 0, active = 0, paused = 0, selected = 0, selectedFiles = 0;
	for (const Entry &e : entries_)
	{
		selected += e.selected;
		if (e.separator) continue;
		total++;
		selectedFiles += e.selected;
		if (e.script == 0) continue;
		active++;
		const auto state = seen_.states.find(e.script);
		paused += state != seen_.states.end() && state->second == LuaBridge::State::Paused;
	}
	std::string s = tr("lua_console.scripts", total);
	if (total != 0) s += " " + tr("lua_console.scripts_state", {std::to_string(active), std::to_string(paused)});
	if (s != summary.get())
	{
		summary.set(s);
		/* a script started, stopped or paused: its row's state and colour */
		if (!entries_.empty()) scripts_.changed.emit(0, static_cast<int64_t>(entries_.size()) - 1);
	}
	selectedCount.set(selected);
	selectedScripts.set(selectedFiles);
	anyEnabled.set(active != 0);
	running.set(seen_.running);
	refreshFunctions(false, seen_.stamp);
}

void LuaConsoleViewModel::refreshFunctions(bool always)
{
	refreshFunctions(always, lua_.ask("__chimera_registered_stamp"));
}

void LuaConsoleViewModel::refreshFunctions(bool always, const std::string &stamp)
{
	if (!always && stamp == functionsStamp_) return;
	functionsStamp_ = stamp;
	functions_.clear();
	const std::string list = lua_.ask("__chimera_registered_list");
	std::istringstream in(list);
	std::string l;
	while (std::getline(in, l))
	{
		Function f;
		std::istringstream cells(l);
		std::getline(cells, f.event, '\t');
		std::getline(cells, f.name, '\t');
		std::getline(cells, f.guid, '\t');
		if (!f.guid.empty()) functions_.push_back(f);
	}
	/* by event, then name, as Chimera's window lists them */
	std::stable_sort(functions_.begin(), functions_.end(), [](const Function &a, const Function &b) {
		return a.event != b.event ? a.event < b.event : a.name < b.name;
	});
	functionCount.set(static_cast<int64_t>(functions_.size()));
	functionRows_.reset.emit();
}

void LuaConsoleViewModel::addRecent(Property<std::string> &list, const std::string &path)
{
	/* newest first, each once, ten at most */
	std::string out = path;
	std::istringstream in(list.get());
	std::string l;
	int kept = 1;
	while (std::getline(in, l) && kept < 10)
		if (!l.empty() && l != path)
		{
			out += "\n" + l;
			kept++;
		}
	if (out != list.get()) list.set(out);
}

void LuaConsoleViewModel::frameDone()
{
	lua_.frameDone();
	afterFrame(lua_.seen());
}

void LuaConsoleViewModel::afterFrame(const LuaBridge::Seen &seen)
{
	messages_ = 0;
	refresh(seen);
}

void LuaConsoleViewModel::line(const std::string &text) { append(text + "\n"); }

void LuaConsoleViewModel::restartScripts()
{
	std::vector<std::string> ran;
	for (const Entry &e : entries_)
		if (!e.separator && e.script != 0) ran.push_back(e.path);
	if (ran.empty() && !lua_.running()) return;
	/* the agent's code and the console's own go too, as Chimera's start again empty */
	lua_.stopAll();
	for (const std::string &path : ran)
		for (Entry &e : entries_)
			if (!e.separator && e.path == path && e.script == 0)
			{
				enable(e);
				break;
			}
	refresh();
}

std::string LuaConsoleViewModel::takeOutputForAgent()
{
	std::string out;
	out.swap(forAgent_);
	return out;
}

bool LuaConsoleViewModel::runForAgent(const std::string &code, std::string &results, std::string &error)
{
	line("[agent] " + code);
	const bool ran = lua_.runForAgent(code, luaFolder.get(), results, error);
	if (!ran) line(error);
	messages_ = 0;
	refresh();
	return ran;
}

bool LuaConsoleViewModel::scriptForAgent(const std::string &path, bool stop)
{
	std::error_code ec;
	const std::string abs = text(fs::absolute(pathOf(path), ec).lexically_normal());
	if (!stop)
		openScript(abs, true);
	else
	{
		for (size_t i = 0; i < entries_.size(); i++)
			if (!entries_[i].separator && entries_[i].path == abs)
			{
				if (entries_[i].script != 0) lua_.stop(entries_[i].script);
				/* found again: stopping it may have changed the list */
				for (size_t j = 0; j < entries_.size(); j++)
					if (!entries_[j].separator && entries_[j].path == abs)
					{
						entries_.erase(entries_.begin() + static_cast<long>(j));
						break;
					}
				anchor_ = -1;
				markChanged();
				listChanged();
				refresh();
				break;
			}
	}
	for (const Entry &e : entries_)
		if (!e.separator && e.path == abs) return e.script != 0;
	return false;
}

void LuaConsoleViewModel::append(const std::string &written)
{
	/* the agent's copy, before the window's cap */
	constexpr size_t kMostForAgent = 64 * 1024;
	forAgent_ += written;
	if (forAgent_.size() > kMostForAgent) forAgent_.erase(0, forAgent_.size() - kMostForAgent);
	if (++messages_ > kMostMessages) return;
	std::string t = written;
	if (messages_ == kMostMessages) t += "\n" + tr("lua_console.flood") + "\n";
	const auto before = static_cast<int64_t>(output_.size());
	bool grewLast = false;
	size_t at = 0;
	while (true)
	{
		const size_t nl = t.find('\n', at);
		const bool ended = nl != std::string::npos;
		const std::string piece = t.substr(at, ended ? nl - at : std::string::npos);
		if (open_)
		{
			output_.back() += piece;
			grewLast = true;
		}
		else if (ended || !piece.empty())
			output_.push_back(piece);
		open_ = !ended && (open_ || !piece.empty());
		if (ended && echo_ && !output_.empty()) echo_(output_.back());
		if (!ended) break;
		at = nl + 1;
	}
	if (output_.size() > kMostLines)
	{
		output_.erase(output_.begin(), output_.begin() + static_cast<long>(output_.size() - kMostLines));
		lines.set(static_cast<int64_t>(output_.size()));
		rows_.reset.emit();
		return;
	}
	lines.set(static_cast<int64_t>(output_.size()));
	if (grewLast && before > 0) rows_.changed.emit(before - 1, before - 1);
	if (static_cast<int64_t>(output_.size()) > before) rows_.appended.emit(before, static_cast<int64_t>(output_.size()) - 1);
}

std::string LuaConsoleViewModel::Scripts::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "path", "state", "separator", "selected"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value LuaConsoleViewModel::Scripts::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Entry &e = vm_.entries_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return e.separator ? std::string() : stem(e.path);
	case 1: return e.separator ? std::string() : e.path;
	case 2:
		if (e.separator) return std::string();
		if (e.script == 0) return std::string("stopped");
		{
			const auto state = vm_.seen_.states.find(e.script);
			return std::string(state != vm_.seen_.states.end() && state->second == LuaBridge::State::Paused ? "paused" : "running");
		}
	case 3: return e.separator;
	case 4: return e.selected;
	default: return {};
	}
}

Value LuaConsoleViewModel::Output::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	if (column == 1) return row >= vm_.outputFirst.get() && row <= vm_.outputLast.get();
	return vm_.output_[static_cast<size_t>(row)];
}

std::string LuaConsoleViewModel::Functions::columnName(int32_t column) const
{
	static const char *const names[] = {"event", "name", "guid"};
	return column >= 0 && column < 3 ? names[column] : "";
}

Value LuaConsoleViewModel::Functions::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Function &f = vm_.functions_[static_cast<size_t>(row)];
	return column == 0 ? f.event : column == 1 ? f.name : f.guid;
}

} // namespace chimera::vm
