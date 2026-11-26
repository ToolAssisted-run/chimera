#include "chimera/vm/tool_windows_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/vm/json.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <sstream>

namespace chimera::vm {

using json::Json;

namespace {
Json read(const std::string &text)
{
	Json j;
	std::string error;
	if (!json::parse(text, j, error) || j.kind != Json::Kind::Object)
	{
		j = Json{};
		j.kind = Json::Kind::Object;
	}
	return j;
}

/* the tool's object in the whole, made when it is not there */
Json &toolOf(Json &all, const std::string &tool)
{
	for (auto &m : all.members)
		if (m.first == tool && m.second.kind == Json::Kind::Object) return m.second;
	Json t;
	t.kind = Json::Kind::Object;
	all.members.emplace_back(tool, t);
	return all.members.back().second;
}

void put(Json &tool, const std::string &name, Json value)
{
	for (auto &m : tool.members)
		if (m.first == name)
		{
			m.second = std::move(value);
			return;
		}
	tool.members.emplace_back(name, std::move(value));
}

bool known(const std::string &name) { return name == "save" || name == "top" || name == "float"; }
} // namespace

ToolWindowsViewModel::ToolWindowsViewModel(Dispatcher &dispatcher) : ViewModel("toolWindows", dispatcher)
{
	expose("showAsked", "a tool window asked to be shown, by the names --show takes (\"tastudio\"); \"\" when none is", showAsked, true);
	expose("exitAsked", "the program asked to close, with this exit code; -1 when it is not", exitAsked);
	command("show", "asks the window to show a tool window, by the names --show takes (Chimera's client.opentasstudio)", {"tool"},
		[this](const Args &a, std::string &error) -> Value {
			std::string tool;
			if (!arg(a, 0, tool, error)) return {};
			showAsked.set("");
			showAsked.set(tool);
			return {};
		});
	command("exit", "asks the program to close, as Chimera's client.exit, with this exit code; nobody is asked, and the "
		"project's unsaved changes are let go",
		{"code"}, [this](const Args &a, std::string &error) -> Value {
			int64_t code = 0;
			if (!a.empty() && !arg(a, 0, code, error)) return {};
			exitAsked.set(std::max<int64_t>(0, code));
			return {};
		});
	command("tell", "a line on the program's standard error, for a run nobody watches", {"text"},
		[](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			std::fprintf(stderr, "%s\n", text.c_str());
			std::fflush(stderr);
			return {};
		});
	command("screenshot", "the picture saved as Chimera's Screenshot saves it: to this file, or (\"\") named and placed as "
		"usual (Chimera's client.screenshot)", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!a.empty() && !arg(a, 0, path, error)) return {};
			if (screenshot) screenshot(path);
			return {};
		});
	command("screenshotToClipboard", "the picture to the clipboard, as Chimera's Screenshot To Clipboard", {},
		[this](const Args &, std::string &) -> Value {
			if (screenshotToClipboard) screenshotToClipboard();
			return {};
		});
	command("resizeWindow", "the main window sized again to its core's scale, as a size asked for is (Chimera's "
		"FrameBufferResized with the resize forced)", {}, [this](const Args &, std::string &) -> Value {
			if (resizeWindow) resizeWindow();
			return {};
		});
	command("windowPosition", "where the main window sits on the screen: x, a space, y", {}, [this](const Args &, std::string &) -> Value {
		const auto at = windowPosition ? windowPosition() : std::pair<int64_t, int64_t>{0, 0};
		return std::to_string(at.first) + " " + std::to_string(at.second);
	});
	expose("settings", "each tool window's options and place, by tool: save (Save Window Position), top (Stay on Top), "
		"float (Float From Main Window), x, y, w, h", settings, true);
	command("setOption", "a tool window's option set: \"save\" (Save Window Position), \"top\" (Stay on Top) or \"float\" "
		"(Float From Main Window)", {"tool", "option", "on"}, [this](const Args &a, std::string &error) -> Value {
			std::string tool, name;
			bool on = false;
			if (!arg(a, 0, tool, error) || !arg(a, 1, name, error) || !arg(a, 2, on, error)) return {};
			if (!known(name))
			{
				error = tr("tool_windows.no_option", {name});
				return {};
			}
			Json all = read(settings.get());
			Json &t = toolOf(all, tool);
			put(t, name, Json::boolean_(on));
			/* a place not to be kept is forgotten */
			if (name == "save" && !on)
			{
				std::vector<std::pair<std::string, Json>> kept;
				for (auto &m : t.members)
					if (m.first != "x" && m.first != "y" && m.first != "w" && m.first != "h") kept.push_back(m);
				t.members = kept;
			}
			settings.set(json::write(all));
			return {};
		});
	command("place", "where a tool window was left and its size, kept when Save Window Position is on", {"tool", "x", "y", "w", "h"},
		[this](const Args &a, std::string &error) -> Value {
			std::string tool;
			int64_t x = 0, y = 0, w = 0, h = 0;
			if (!arg(a, 0, tool, error) || !arg(a, 1, x, error) || !arg(a, 2, y, error) || !arg(a, 3, w, error) || !arg(a, 4, h, error)) return {};
			if (!option(tool, "save") || w <= 0 || h <= 0) return false;
			Json all = read(settings.get());
			Json &t = toolOf(all, tool);
			put(t, "x", Json::integer(x));
			put(t, "y", Json::integer(y));
			put(t, "w", Json::integer(w));
			put(t, "h", Json::integer(h));
			const std::string text = json::write(all);
			if (text != settings.get()) settings.set(text);
			return true;
		});
	command("sortFiles", "files dropped on the window (or handed over by a later start), a line each, sorted as Chimera's are: "
		"what to do, a line each in Chimera's order - \"open\" and the kind (game, watch, luaSession, "
		"lua, project) and the file, tab apart, or \"say\" and what to tell the person: several of one kind open none, and a "
		"state or cheat file does not open by dropping", {"files"}, [](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			static const char *const order[] = {"game", "state", "watch", "luaSession", "lua", "cheat", "project"};
			std::vector<std::vector<std::string>> kinds(std::size(order));
			std::istringstream in(list);
			for (std::string path; std::getline(in, path);)
			{
				if (path.empty()) continue;
				std::string lower = path;
				for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				auto ends = [&lower](const char *tail) {
					const size_t n = std::strlen(tail);
					return lower.size() >= n && lower.compare(lower.size() - n, n, tail) == 0;
				};
				/* anything else is a game, as Chimera takes an unknown file for a rom */
				const size_t kind = ends(".chimerastate") ? 1 : ends(".wch") ? 2 : ends(".luases") ? 3 : ends(".lua") ? 4
					: ends(".cht") ? 5 : ends(".chimeraproject") ? 6 : 0;
				kinds[kind].push_back(path);
			}
			std::string out;
			for (size_t k = 0; k < kinds.size(); k++)
			{
				if (kinds[k].empty()) continue;
				if (kinds[k].size() > 1)
					out += "say\t" + tr("tool_windows.dropped_many", {std::to_string(kinds[k].size())}) + "\n";
				else if (k == 1 || k == 5)
				{
					const std::string &path = kinds[k][0];
					const size_t slash = path.find_last_of("/\\");
					out += "say\t" + tr("tool_windows.not_dropped", {slash == std::string::npos ? path : path.substr(slash + 1)}) + "\n";
				}
				else
					out += std::string("open\t") + order[k] + "\t" + kinds[k][0] + "\n";
			}
			return out;
		});
	command("restoreDefaults", "a tool window's options back to Chimera's (Save Window Position on, the others off) and its place forgotten",
		{"tool"}, [this](const Args &a, std::string &error) -> Value {
			std::string tool;
			if (!arg(a, 0, tool, error)) return {};
			Json all = read(settings.get());
			std::vector<std::pair<std::string, Json>> kept;
			for (auto &m : all.members)
				if (m.first != tool) kept.push_back(m);
			all.members = kept;
			settings.set(json::write(all));
			return {};
		});
}

bool ToolWindowsViewModel::option(const std::string &tool, const std::string &name) const
{
	const Json all = read(settings.get());
	const Json *t = all.find(tool);
	const Json *v = t != nullptr ? t->find(name) : nullptr;
	if (v != nullptr && v->kind == Json::Kind::Bool) return v->boolean;
	/* Chimera's: Save Window Position on, Stay on Top and Float From Main Window off */
	return name == "save";
}

} // namespace chimera::vm
