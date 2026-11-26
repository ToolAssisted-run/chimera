#include "chimera/vm/paths_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>

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

/* a core's id as a folder name: what Windows refuses in a name taken out */
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
		{"Macros", "./Movies/Macros"},
		/* what a core writes by itself (screenshots), each core in a folder of its id; made when first written to (owner, 2026-11-26, issue 655) */
		{"Output", "./output"},
		{"Temp Files", ""},
	};
	return list;
}

PathsViewModel::PathsViewModel(Dispatcher &dispatcher) : ViewModel("paths", dispatcher)
{
	expose("base", "the global base, as set; \"\" for Chimera's \".\" (the program's own folder)", base, true);
	expose("programFolder", "the program's folder", programFolder);
	expose("changed", "the global paths changed from Chimera's, {\"Movies\": \"D:/runs\", ...}", changed, true);
	expose("recentForRoms", "the game dialog opens where the last game came from", recentForRoms, true);
	expose("lastRomFolder", "where the last game came from", lastRomFolder, true);
	rows("paths", "one row a folder: name, path (as set), folder (made absolute), isDefault", rows_);
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
	command("resolve", "where a global folder is, made absolute", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		const std::string r = resolve(name);
		if (r.empty()) error = tr("paths.no_folder", {name});
		return r;
	});
	command("outputFor", "where a core's own files go by default: the Output folder, then a folder named by the core's id "
		"(its name in lower case, dashes for spaces; never its version); the Output folder itself for \"\"; nothing is made",
		{"core"}, [this](const Args &a, std::string &error) -> Value {
			std::string core;
			if (!arg(a, 0, core, error)) return {};
			return outputFor(core);
		});
	command("absolute", "a path made absolute as Chimera does", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			return absolute(path);
		});
	command("makeRelative", "a folder picked, made relative to the base it would be under (\"./...\"), as Chimera's "
		"TryMakeRelative; outside the base, as it is", {"folder"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder;
			if (!arg(a, 0, folder, error)) return {};
			const fs::path from(globalBase());
			const fs::path at = fs::path(folder).lexically_normal();
			const fs::path rel = at.lexically_relative(from);
			if (rel.empty() || *rel.begin() == "..") return tidy(at);
			return rel == "." ? std::string(".") : "./" + rel.generic_string();
		});
	command("dialogFolder", "where a file dialog for this folder opens: the folder, or the nearest one above it that is "
		"there; for ROM, where the last game came from when recentForRoms. With make, the folder is made first, for a "
		"file about to be saved there (Save as Zip's report, issue 435)", {"name", "make"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			bool make = false;
			if (!arg(a, 0, name, error) || (a.size() > 1 && !arg(a, 1, make, error))) return {};
			if (name == "ROM" && recentForRoms.get() && !lastRomFolder.get().empty() && fs::is_directory(lastRomFolder.get())) return lastRomFolder.get();
			std::string r = resolve(name);
			if (r.empty())
			{
				error = tr("paths.no_folder", {name});
				return {};
			}
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

std::string PathsViewModel::globalBase() const
{
	/* the global base made absolute: %exe% put in, an absolute base as it is, else under the program's folder */
	std::string b = pathOf("Base");
	if (startsWithToken(b, "%exe%")) b = programFolder.get() + b.substr(5);
	if (fs::path(b).is_absolute()) return tidy(b);
	return tidy(fs::path(programFolder.get()) / b);
}

std::string PathsViewModel::absolute(const std::string &path) const
{
	/* a path made absolute, step by step */
	const std::string &p = path;
	if (p.empty()) return globalBase();
	if (p == "%recent%") return recentFolder();
	if (startsWithToken(p, "%exe%")) return tidy(programFolder.get() + p.substr(5));
	if (startsWithToken(p, "%rom%")) return tidy((lastRomFolder.get().empty() ? std::string(".") : lastRomFolder.get()) + p.substr(5));
	if (p[0] == '.')
	{
		/* "..": the folder above the base, as Chimera's Special Commands say */
		if (p.size() >= 2 && p[1] == '.') return tidy(fs::path(globalBase()) / p);
		if (p.size() == 1) return globalBase();
		return tidy(globalBase() + p.substr(1));
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
		return absolute(path == "Tools" ? tools : (fs::path(tools) / path).string());
	}
	return absolute(path);
}

std::string PathsViewModel::tempFolder() const
{
	return pathOf("Temp Files").empty() ? std::string() : resolve("Temp Files");
}

std::string PathsViewModel::outputFor(const std::string &core) const
{
	const std::string folder = resolve("Output");
	const std::string name = folderName(core);
	/* "." and ".." are folders, not names */
	if (name.empty() || name == "." || name == "..") return folder;
	return tidy(fs::path(folder) / name);
}

int64_t PathsViewModel::Rows::rowCount() const
{
	return static_cast<int64_t>(defaults().size());
}

std::string PathsViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "path", "folder", "isDefault"};
	return column >= 0 && column < 4 ? names[column] : "";
}

Value PathsViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const auto &globals = defaults();
	const std::string &name = globals[static_cast<size_t>(row)].first;
	const std::string path = vm_.pathOf(name);
	switch (column)
	{
	case 0: return name;
	case 1: return path;
	case 2: return vm_.resolve(name);
	case 3: return path == globals[static_cast<size_t>(row)].second;
	default: return {};
	}
}

} // namespace chimera::vm
