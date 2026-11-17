#include "chimera/vm/paths_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string_view>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace {
/* a folder made whole and tidy, without a separator at its end ("." alone leaves one) */
std::string tidy(const fs::path &p)
{
	std::string out = p.lexically_normal().string();
	while (out.size() > 1 && (out.back() == '/' || out.back() == '\\') && !(out.size() == 3 && out[1] == ':')) out.pop_back();
	return out;
}

bool startsWithToken(const std::string &path, const char *token)
{
	const size_t n = std::char_traits<char>::length(token);
	if (path.size() < n) return false;
	for (size_t i = 0; i < n; i++)
		if (std::tolower(static_cast<unsigned char>(path[i])) != std::tolower(static_cast<unsigned char>(token[i]))) return false;
	return true;
}

/* the system's folder of recently used files (Chimera's %recent%): Windows' Recent; elsewhere the home folder */
std::string recentFolder()
{
#ifdef _WIN32
	if (const char *appData = std::getenv("APPDATA")) return tidy(fs::path(appData) / "Microsoft" / "Windows" / "Recent");
#endif
	if (const char *home = std::getenv("HOME")) return tidy(fs::path(home));
	return {};
}

/* a system's id as a folder name: what Windows refuses in a name taken out, as Chimera does */
std::string folderName(const std::string &id)
{
	std::string out;
	for (char c : id)
		if (static_cast<unsigned char>(c) >= 32 && std::string("<>:\"/\\|?*").find(c) == std::string::npos) out += c;
	return out;
}

json::Json objectOf(const std::string &text)
{
	json::Json j;
	std::string err;
	if (!json::parse(text, j, err) || j.kind != json::Json::Kind::Object)
	{
		j = json::Json();
		j.kind = json::Json::Kind::Object;
	}
	return j;
}
} // namespace

const std::vector<std::pair<std::string, std::string>> &PathsViewModel::defaults()
{
	/* Chimera's global entries, in its order, the folders named as the old
	 * window named them (owner, 2026-11-01, issue 435) */
	static const std::vector<std::pair<std::string, std::string>> list = {
		{"Base", "."},
		{"ROM", "."},
		{"Firmware", "./Firmware"},
		{"Movies", "./Movies"},
		{"Movie backups", "./Movies/backup"},
		/* where Encode Video suggests a video goes */
		{"A/V Dumps", "."},
		{"Tools", "./Tools"},
		{"Lua", "./Lua"},
		{"Themes", "./themes"},
		/* Watch and Debug Logs are under Tools when relative, as Chimera resolves them */
		{"Watch (.wch)", "."},
		{"Debug Logs", "."},
		{"Bug reports", "./Reports"},
		/* where Export Save Data saves, a folder a core in it, made as it saves
		 * (owner, 2026-11-05, issue 500) */
		{"Save data", "./SaveData"},
		{"Macros", "./Movies/Macros"},
		{"Temp Files", ""},
	};
	return list;
}

std::vector<std::pair<std::string, std::string>> PathsViewModel::systemDefaults(const std::string &system)
{
	/* a system's entries: made for a system the first time something of it is asked for */
	return {{"Base", "./" + folderName(system)}, {"ROM", "."}, {"Savestates", "./State"}, {"Screenshots", "./Screenshots"}};
}

PathsViewModel::PathsViewModel(Dispatcher &dispatcher) : ViewModel("paths", dispatcher)
{
	expose("base", "the global base, as set; \"\" for Chimera's \".\" (the program's own folder)", base, true);
	expose("programFolder", "the program's folder", programFolder);
	expose("changed", "the global paths changed from Chimera's, {\"Movies\": \"D:/runs\", ...}", changed, true);
	expose("systems", "the systems with folders of their own, as a JSON array", systems, true);
	expose("systemChanged", "their paths changed from Chimera's, {\"NES\": {\"Screenshots\": \"./Shots\"}}", systemChanged, true);
	expose("recentForRoms", "the game dialog opens where the last game came from", recentForRoms, true);
	expose("lastRomFolder", "where the last game came from", lastRomFolder, true);
	rows("paths", "one row a folder: system (\"Global\" or its id), name, path (as set), folder (made absolute), isDefault", rows_);
	command("setPath", "a global folder's path: absolute, \".\" for the base, %exe%, %rom% or %recent%; \"\" puts "
		"Chimera's back", {"name", "path"}, [this](const Args &a, std::string &error) -> Value {
			std::string name, path;
			if (!arg(a, 0, name, error) || !arg(a, 1, path, error)) return {};
			const auto &d = defaults();
			const auto it = std::find_if(d.begin(), d.end(), [&](const auto &e) { return e.first == name; });
			if (it == d.end())
			{
				error = tr("paths.no_folder", {name});
				return {};
			}
			if (name == "Base")
			{
				base.set(path == "." ? std::string() : path);
				return resolve(name);
			}
			json::Json all = objectOf(changed.get());
			std::erase_if(all.members, [&](const auto &m) { return m.first == name; });
			/* Chimera's own path is not a change */
			if (!path.empty() && path != it->second) all.members.emplace_back(name, json::Json::string(path));
			changed.set(json::write(all));
			return resolve(name);
		});
	command("setSystemPath", "a system's folder's path (Base, ROM, Savestates, Screenshots); \"\" puts Chimera's back",
		{"system", "name", "path"}, [this](const Args &a, std::string &error) -> Value {
			std::string system, name, path;
			if (!arg(a, 0, system, error) || !arg(a, 1, name, error) || !arg(a, 2, path, error)) return {};
			const auto d = systemDefaults(system);
			const auto it = std::find_if(d.begin(), d.end(), [&](const auto &e) { return e.first == name; });
			if (system.empty() || it == d.end())
			{
				error = system.empty() ? tr("paths.no_system") : tr("paths.no_folder", {name});
				return {};
			}
			useSystem(system);
			json::Json all = objectOf(systemChanged.get());
			json::Json mine = all.find(system) != nullptr && all.find(system)->kind == json::Json::Kind::Object ? *all.find(system) : objectOf("{}");
			std::erase_if(mine.members, [&](const auto &m) { return m.first == name; });
			if (!path.empty() && path != it->second) mine.members.emplace_back(name, json::Json::string(path));
			std::erase_if(all.members, [&](const auto &m) { return m.first == system; });
			if (!mine.members.empty()) all.members.emplace_back(system, mine);
			systemChanged.set(json::write(all));
			return resolveFor(system, name);
		});
	command("useSystem", "a system gets folders of its own", {"system"}, [this](const Args &a, std::string &error) -> Value {
		std::string system;
		if (!arg(a, 0, system, error)) return {};
		useSystem(system);
		return {};
	});
	command("resolve", "where a global folder is, made absolute", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		const std::string r = resolve(name);
		if (r.empty()) error = tr("paths.no_folder", {name});
		return r;
	});
	command("resolveFor", "where a system's folder is, made absolute (its own, else its base, else the global base)",
		{"system", "name"}, [this](const Args &a, std::string &error) -> Value {
			std::string system, name;
			if (!arg(a, 0, system, error) || !arg(a, 1, name, error)) return {};
			return resolveFor(system, name);
		});
	command("absolute", "a path made absolute as Chimera does, for a system or for none (\"\")", {"path", "system"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path, system;
			if (!arg(a, 0, path, error) || !arg(a, 1, system, error)) return {};
			return absolute(path, system);
		});
	command("makeRelative", "a folder picked, made relative to the base it would be under (\"./...\"), as Chimera's "
		"TryMakeRelative: the global base, or the system's own for a system's folder; outside it, as it is",
		{"folder", "system"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder, system;
			if (!arg(a, 0, folder, error) || !arg(a, 1, system, error)) return {};
			const fs::path from(system.empty() ? globalBase() : resolveFor(system, "Base"));
			const fs::path at = fs::path(folder).lexically_normal();
			const fs::path rel = at.lexically_relative(from);
			if (rel.empty() || *rel.begin() == "..") return tidy(at);
			return rel == "." ? std::string(".") : "./" + rel.generic_string();
		});
	command("dialogFolder", "where a file dialog for this folder opens: the folder, or the nearest one above it that is "
		"there; for ROM, where the last game came from when recentForRoms. With make, the folder is made first, for a "
		"file about to be saved there (Save as Zip's report, issue 435). With a sub, that folder inside it, its name "
		"made fit for a folder (a core's, for Export Save Data: issue 500)", {"name", "make", "sub"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name, sub;
			bool make = false;
			if (!arg(a, 0, name, error) || (a.size() > 1 && !arg(a, 1, make, error)) || (a.size() > 2 && !arg(a, 2, sub, error))) return {};
			if (name == "ROM" && recentForRoms.get() && !lastRomFolder.get().empty() && fs::is_directory(lastRomFolder.get())) return lastRomFolder.get();
			std::string r = resolve(name);
			if (r.empty())
			{
				error = tr("paths.no_folder", {name});
				return {};
			}
			for (char &c : sub)
				if (std::string_view("<>:\"/\\|?*").find(c) != std::string_view::npos || static_cast<unsigned char>(c) < 32) c = '_';
			while (!sub.empty() && (sub.back() == '.' || sub.back() == ' ')) sub.pop_back();
			if (!sub.empty()) r = (fs::path(r) / sub).string();
			std::error_code ec;
			if (make) fs::create_directories(r, ec);
			fs::path at(r);
			while (!at.empty() && !fs::is_directory(at, ec) && at.has_parent_path() && at.parent_path() != at) at = at.parent_path();
			return at.string();
		});
	command("gameChosen", "a game file picked in a dialog: its folder is where the last game came from, which the ROM "
		"dialogs open in when recentForRoms", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			std::error_code ec;
			const fs::path whole = fs::absolute(path, ec);
			lastRomFolder.set((ec ? fs::path(path) : whole).parent_path().string());
			return {};
		});
	command("resetAll", "every folder back to Chimera's", {}, [this](const Args &, std::string &) -> Value {
		changed.set("{}");
		systemChanged.set("{}");
		base.set("");
		return {};
	});
	auto refresh = [this] {
		rows_.reset.emit();
		moved.emit();
	};
	baseWatch_ = base.changed.connect(refresh);
	programWatch_ = programFolder.changed.connect(refresh);
	changedWatch_ = changed.changed.connect(refresh);
	systemsWatch_ = systems.changed.connect(refresh);
	systemChangedWatch_ = systemChanged.changed.connect(refresh);
}

std::vector<std::string> PathsViewModel::systemList() const
{
	std::vector<std::string> out;
	json::Json list;
	std::string err;
	if (json::parse(systems.get(), list, err) && list.kind == json::Json::Kind::Array)
		for (const json::Json &s : list.items)
			if (s.kind == json::Json::Kind::String && !s.text.empty() && std::find(out.begin(), out.end(), s.text) == out.end()) out.push_back(s.text);
	/* the "by System" tab lists them by name, as Chimera's */
	std::sort(out.begin(), out.end());
	return out;
}

void PathsViewModel::useSystem(const std::string &system)
{
	if (system.empty()) return;
	std::vector<std::string> now = systemList();
	if (std::find(now.begin(), now.end(), system) != now.end()) return;
	now.push_back(system);
	std::sort(now.begin(), now.end());
	json::Json list;
	list.kind = json::Json::Kind::Array;
	for (const std::string &s : now) list.items.push_back(json::Json::string(s));
	systems.set(json::write(list));
}

std::string PathsViewModel::pathOf(const std::string &name) const
{
	if (name == "Base") return base.get().empty() ? std::string(".") : base.get();
	const auto &d = defaults();
	const auto it = std::find_if(d.begin(), d.end(), [&](const auto &e) { return e.first == name; });
	if (it == d.end()) return {};
	const json::Json all = objectOf(changed.get());
	if (const json::Json *v = all.find(name); v != nullptr && v->kind == json::Json::Kind::String && !v->text.empty()) return v->text;
	return it->second;
}

std::string PathsViewModel::pathOf(const std::string &system, const std::string &name) const
{
	if (system.empty() || system == "Global") return pathOf(name);
	const auto d = systemDefaults(system);
	const auto it = std::find_if(d.begin(), d.end(), [&](const auto &e) { return e.first == name; });
	if (it == d.end()) return {};
	const json::Json all = objectOf(systemChanged.get());
	if (const json::Json *mine = all.find(system))
		if (const json::Json *v = mine->find(name); v != nullptr && v->kind == json::Json::Kind::String && !v->text.empty()) return v->text;
	return it->second;
}

std::string PathsViewModel::globalBase() const
{
	/* the global base made absolute: %exe% put in, an absolute base as it is, else under the program's folder */
	std::string b = pathOf("Base");
	if (startsWithToken(b, "%exe%")) b = programFolder.get() + b.substr(5);
	if (fs::path(b).is_absolute()) return tidy(b);
	return tidy(fs::path(programFolder.get()) / b);
}

std::string PathsViewModel::absolute(const std::string &path, const std::string &forSystem) const
{
	/* a path made absolute, step by step */
	const std::string system = forSystem == "Global" ? std::string() : forSystem;
	std::string p = path;
	if (p.empty()) return globalBase();
	if (p == "%recent%") return recentFolder();
	if (startsWithToken(p, "%exe%")) return tidy(programFolder.get() + p.substr(5));
	if (startsWithToken(p, "%rom%")) return tidy((lastRomFolder.get().empty() ? std::string(".") : lastRomFolder.get()) + p.substr(5));
	if (p[0] == '.')
	{
		/* "..": the folder above the base, as Chimera's Special Commands say (Chimera's own code
		 * makes it the base itself on Windows, which drops the dot it leaves at a folder's end) */
		if (p.size() >= 2 && p[1] == '.')
			return tidy(fs::path(system.empty() ? globalBase() : resolveFor(system, "Base")) / p);
		/* a system's: from its own base, which starts from the global one */
		if (!system.empty()) p = pathOf(system, "Base") + p.substr(1);
		if (p.size() == 1) return globalBase();
		if (p[0] == '.') p = globalBase() + p.substr(1);
		return tidy(p);
	}
	/* anything else that is not absolute is the program's folder, as Chimera's */
	return fs::path(p).is_absolute() ? tidy(p) : tidy(programFolder.get());
}

std::string PathsViewModel::resolve(const std::string &name) const
{
	const std::string path = pathOf(name);
	if (name != "Base" && path.empty() && name != "Temp Files") return {};
	if (name == "Base") return globalBase();
	/* the tools' path: a relative Watch or Debug Logs folder is under Tools ("Tools" alone being Tools) */
	if ((name == "Watch (.wch)" || name == "Debug Logs") && !fs::path(path).is_absolute() && (path.empty() || path[0] != '%'))
	{
		const std::string tools = pathOf("Tools");
		return absolute(path == "Tools" ? tools : (fs::path(tools) / path).string(), "");
	}
	return absolute(path, "");
}

std::string PathsViewModel::tempFolder() const
{
	return pathOf("Temp Files").empty() ? std::string() : resolve("Temp Files");
}

std::string PathsViewModel::resolveFor(const std::string &system, const std::string &name) const
{
	if (system.empty() || system == "Global") return resolve(name);
	/* EntryWithFallback: the system's own kind, else its base, else the global base. The system's
	 * base is from the global one, and is not put in front of it again (which would give
	 * <base>/NES/NES) */
	const std::string path = pathOf(system, name);
	if (path.empty() || name == "Base") return absolute(pathOf(system, "Base"), "");
	return absolute(path, system);
}

int64_t PathsViewModel::Rows::rowCount() const
{
	return static_cast<int64_t>(defaults().size() + vm_.systemList().size() * 4);
}

std::string PathsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"system", "name", "path", "folder", "isDefault"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value PathsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const auto &globals = defaults();
	std::string system = "Global", name, fallback;
	if (static_cast<size_t>(row) < globals.size())
	{
		name = globals[static_cast<size_t>(row)].first;
		fallback = globals[static_cast<size_t>(row)].second;
	}
	else
	{
		const size_t at = static_cast<size_t>(row) - globals.size();
		system = vm_.systemList()[at / 4];
		const auto d = systemDefaults(system);
		name = d[at % 4].first;
		fallback = d[at % 4].second;
	}
	const std::string path = vm_.pathOf(system, name);
	switch (column)
	{
	case 0: return system;
	case 1: return name;
	case 2: return path;
	case 3: return system == "Global" ? vm_.resolve(name) : vm_.resolveFor(system, name);
	case 4: return path == fallback;
	default: return {};
	}
}

} // namespace chimera::vm
