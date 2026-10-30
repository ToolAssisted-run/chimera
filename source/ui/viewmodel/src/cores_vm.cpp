#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "chimera/engine.h"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

CoresViewModel::CoresViewModel(Dispatcher &dispatcher) : ViewModel("cores", dispatcher)
{
	expose("coresFolder", "the folder core packages are looked for in", coresFolder, true);
	expose("firmwareFolder", "the folder firmware is looked for in", firmwareFolder, true);
	expose("chosenFirmware", "the firmware files chosen by hand, every core's, as {\"core\": {\"id\": \"file\"}}", chosenFirmware, true);
	expose("count", "packages found", count);
	expose("status", "what the last look at the folder found", status);
	rows("cores", "one row a package: core, version, when it was made, file", rows_);
	command("refresh", "looks at the cores folder again", {}, [this](const Args &, std::string &) -> Value {
		scan();
		return count.get();
	});
	expose("shows", "which kinds of core the Core Manager lists: 0 all, 1 the emulators, 2 the games", shows, true);
	expose("managerHeader", "the Core Manager's two lines over the list: the folder and how many cores, and how a core arrives", managerHeader);
	expose("managerStatus", "the Core Manager's line under the list", managerStatus, true);
	rows("manager", "one row a core, as Chimera's Core Manager lists it (the emulators, then the games, each by name, through "
		"Show): core, type, systems, version (the newest, and how many more), size, gameCore", managerRows_);
	command("refreshList", "looks at the cores folder again and says how many cores are there (Refresh List)", {},
		[this](const Args &, std::string &) -> Value {
			scan();
			managerStatus.set(tr("cores.count_in", static_cast<int64_t>(managed_.size()), {coresFolder.get()}));
			return {};
		});
	command("versionsOf", "a core's versions here, newest first, as [{\"label\", \"path\"}]", {"core"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			json::Json list;
			list.kind = json::Json::Kind::Array;
			std::lock_guard<std::mutex> lock(m_);
			for (const Managed &m : managed_)
				if (m.name == name)
					for (size_t i : m.versions)
					{
						json::Json o;
						o.kind = json::Json::Kind::Object;
						const Core &c = cores_[i];
						o.members.emplace_back("label", json::Json::string(!c.datedText.empty() ? c.datedText : std::filesystem::path(c.path).stem().string()));
						o.members.emplace_back("path", json::Json::string(c.path));
						list.items.push_back(o);
					}
			return json::write(list);
		});
	command("versionDetail", "what Chimera's Core Manager says of a version: where it is, when it was built, and its terms",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			std::string built;
			{
				std::lock_guard<std::mutex> lock(m_);
				for (const Core &c : cores_)
					if (c.path == path && c.dated) built = tr("cores.built", {europeanTime(c.date)});
			}
			const std::string terms = licenceSummary(path);
			return path + (built.empty() ? "" : "\n" + built) + "\n\n" + (terms.empty() ? tr("cores.no_licence") : terms);
		});
	command("removeVersion", "deletes one version's package (only a file in the cores folder itself)", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			const std::string name = std::filesystem::path(path).filename().string();
			const std::string why = removePackage(path);
			if (why == tr("cores.not_in_folder"))
				managerStatus.set(tr("cores.not_ours", {name, path}));
			else if (!why.empty())
				managerStatus.set(tr("cores.not_removed", {why}));
			else
			{
				scan();
				managerStatus.set(tr("cores.removed", {name}));
			}
			return {};
		});
	command("removeCores", "deletes every version of the cores named (a JSON array) that is in the cores folder itself", {"cores"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			json::Json names;
			if (!json::parse(text, names, error) || names.kind != json::Json::Kind::Array) return {};
			std::vector<std::string> paths;
			{
				std::lock_guard<std::mutex> lock(m_);
				for (const json::Json &n : names.items)
					for (const Managed &m : managed_)
						if (m.name == n.text)
							for (size_t i : m.versions) paths.push_back(cores_[i].path);
			}
			int removed = 0;
			std::string kept;
			for (const std::string &p : paths)
			{
				const std::string why = removePackage(p);
				const std::string name = std::filesystem::path(p).filename().string();
				if (why.empty()) removed++;
				else kept += (kept.empty() ? "" : ", ") + (why == tr("cores.not_in_folder") ? name : name + " (" + why + ")");
			}
			scan();
			managerStatus.set(kept.empty() ? tr("cores.removed_packages", removed) : tr("cores.removed_packages_left", removed, {kept}));
			return int64_t{removed};
		});
	command("supportedSystems", "the systems the cores here run, by name, as Chimera's Show Systems lists them: "
		"[{\"name\", \"id\", \"cores\", \"installed\"}]", {}, [this](const Args &, std::string &) -> Value {
			struct Entry
			{
				std::string id, name;
				std::vector<std::string> cores;
			};
			std::vector<Entry> by;
			auto lower = [](std::string s) {
				for (char &ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
				return s;
			};
			{
				std::lock_guard<std::mutex> lock(m_);
				for (const Managed &m : managed_)
				{
					/* a core's systems, as its packages call them: the first that gives a name (not just the id back) is heard */
					std::vector<std::pair<std::string, std::string>> systems;
					for (size_t i : m.versions)
						for (const auto &[id, word] : cores_[i].systemList)
						{
							auto at = std::find_if(systems.begin(), systems.end(), [&](const auto &s) { return lower(s.first) == lower(id); });
							if (at == systems.end()) systems.emplace_back(id, word);
							else if (at->second == at->first && word != id) at->second = word;
						}
					for (const auto &[id, word] : systems)
					{
						auto e = std::find_if(by.begin(), by.end(), [&](const Entry &x) { return lower(x.id) == lower(id); });
						if (e == by.end())
						{
							by.push_back({id, id, {}});
							e = by.end() - 1;
						}
						if (std::none_of(e->cores.begin(), e->cores.end(), [&](const std::string &c) { return lower(c) == lower(m.name); })) e->cores.push_back(m.name);
						if (e->name == e->id && word != id) e->name = word;
					}
				}
			}
			for (Entry &e : by) std::sort(e.cores.begin(), e.cores.end(), [&](const std::string &x, const std::string &y) { return lower(x) < lower(y); });
			std::sort(by.begin(), by.end(), [&](const Entry &x, const Entry &y) {
				return lower(x.name) != lower(y.name) ? lower(x.name) < lower(y.name) : lower(x.id) < lower(y.id);
			});
			json::Json list;
			list.kind = json::Json::Kind::Array;
			for (const Entry &e : by)
			{
				json::Json o;
				o.kind = json::Json::Kind::Object;
				std::string cores;
				for (const std::string &c : e.cores) cores += (cores.empty() ? "" : ", ") + c;
				o.members.emplace_back("name", json::Json::string(e.name));
				o.members.emplace_back("id", json::Json::string(e.id));
				o.members.emplace_back("cores", json::Json::string(cores));
				o.members.emplace_back("installed", json::Json::boolean_(true));
				list.items.push_back(o);
			}
			return json::write(list);
		});
	showsWatch_ = shows.changed.connect([this] { managerRows_.reset.emit(); });
	/* the environment names them where a test or a portable install means it */
	if (const char *env = std::getenv("CHIMERA_CORES_DIR")) coresFolder.set(env);
	if (const char *env = std::getenv("CHIMERA_FIRMWARE_DIR")) firmwareFolder.set(env);
	/* a Windows folder that came through a file URL with its slash kept
	 * ("/C:/Users/..."), as the window's folder dialogs once wrote them and a
	 * remembered setting may still hold, is the folder without it */
	auto driveFolder = [](Property<std::string> &folder) {
		const std::string &f = folder.get();
		if (f.size() >= 3 && f[0] == '/' && std::isalpha(static_cast<unsigned char>(f[1])) && f[2] == ':' &&
			(f.size() == 3 || f[3] == '/' || f[3] == '\\'))
			folder.set(f.substr(1));
	};
	driveFolder(coresFolder);
	driveFolder(firmwareFolder);
	folderWatch_ = coresFolder.changed.connect([this, driveFolder] {
		driveFolder(coresFolder);
		scan();
	});
	firmwareWatch_ = firmwareFolder.changed.connect([this, driveFolder] {
		driveFolder(firmwareFolder);
		std::lock_guard<std::mutex> lock(m_);
		firmwareM_ = firmwareFolder.get();
	});
	firmwareM_ = firmwareFolder.get();
	chosenWatch_ = chosenFirmware.changed.connect([this] {
		json::Json all;
		std::string error;
		std::vector<std::string> files;
		if (json::parse(chosenFirmware.get(), all, error))
			for (const auto &[core, ids] : all.members)
				for (const auto &[id, file] : ids.members)
					if (file.kind == json::Json::Kind::String && !file.text.empty()) files.push_back(file.text);
		std::lock_guard<std::mutex> lock(m_);
		chosenM_ = std::move(files);
	});
	scan();
}

void CoresViewModel::rememberFirmware(const std::string &coreName, const std::string &id, const std::string &path)
{
	json::Json all;
	std::string error;
	if (!json::parse(chosenFirmware.get(), all, error) || all.kind != json::Json::Kind::Object) all = json::Json(), all.kind = json::Json::Kind::Object;
	auto member = [](json::Json &obj, const std::string &name) -> json::Json & {
		for (auto &[k, v] : obj.members)
			if (k == name) return v;
		obj.members.emplace_back(name, json::Json());
		return obj.members.back().second;
	};
	json::Json &mine = member(all, coreName);
	if (mine.kind != json::Json::Kind::Object) mine = json::Json(), mine.kind = json::Json::Kind::Object;
	member(mine, id) = json::Json::string(path);
	chosenFirmware.set(json::write(all));
}

void CoresViewModel::scan()
{
	namespace fs = std::filesystem;
	std::vector<Core> found;
	const std::string folder = coresFolder.get();
	std::error_code ec;
	if (!folder.empty() && fs::is_directory(folder, ec))
		for (const auto &entry : fs::directory_iterator(folder, ec))
		{
			if (entry.path().extension() != ".chimeraCore") continue;
			const std::string path = entry.path().string();
			const char *perr = nullptr;
			ce_package *p = ce_package_open(path.c_str(), &perr);
			if (p == nullptr) continue;
			uint64_t len = 0;
			const uint8_t *d = ce_package_entry_texts(p, "waterbox.config", &len);
			json::Json cfg;
			std::string error;
			if (d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, error))
			{
				Core c;
				if (const json::Json *n = cfg.find("coreName")) c.coreName = n->text;
				if (const json::Json *v = cfg.find("version")) c.version = v->text;
				if (const json::Json *m = cfg.find("versionDate")) c.made = m->text;
				c.dated = !c.made.empty() && parseIsoTime(c.made, c.date);
				if (const json::Json *k = cfg.find("kind"))
				{
					std::string kind = k->text;
					for (char &ch : kind) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
					c.gameCore = kind == "game";
				}
				c.systems = systemsOf(cfg);
				c.systemList = systemPairs(cfg);
				c.refused = abiRefusal(cfg);
				c.path = path;
				c.size = static_cast<int64_t>(fs::file_size(entry.path(), ec));
				if (ec) c.size = 0;
				c.modified = static_cast<long long>(fs::last_write_time(entry.path(), ec).time_since_epoch().count());
				/* the files it takes, as Chimera reads them: the package's
				 * "extensions" and then each machine's */
				const auto take = [&c](const json::Json *exts) {
					if (exts == nullptr) return;
					for (const auto &[ext, system] : exts->members)
					{
						std::string e = ext;
						for (char &ch : e) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
						if (!e.empty() && e[0] == '.') e.erase(0, 1);
						if (!e.empty() && std::find(c.extensions.begin(), c.extensions.end(), e) == c.extensions.end()) c.extensions.push_back(e);
					}
				};
				take(cfg.find("extensions"));
				if (const json::Json *machines = cfg.find("machines"))
					for (const json::Json &m : machines->items) take(m.find("extensions"));
				if (!c.coreName.empty()) found.push_back(std::move(c));
			}
			ce_package_free(p);
		}
	/* by core, then newest first by the date the package says it was made
	 * (one that says none after those that do), as Chimera orders them */
	auto folded = [](std::string s) {
		for (char &ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		return s;
	};
	std::sort(found.begin(), found.end(), [&](const Core &a, const Core &b) {
		const std::string fa = folded(a.coreName), fb = folded(b.coreName);
		if (fa != fb) return fa < fb;
		if (a.coreName != b.coreName) return a.coreName < b.coreName;
		if (a.dated != b.dated) return a.dated;
		if (a.dated && a.date != b.date) return a.date > b.date;
		return a.modified > b.modified;
	});
	/* each one's line; the package's hash only where two are of one core */
	for (size_t i = 0; i < found.size(); i++)
	{
		Core &c = found[i];
		const bool shared = (i > 0 && found[i - 1].coreName == c.coreName) || (i + 1 < found.size() && found[i + 1].coreName == c.coreName);
		std::string version = c.version;
		const bool local = version.find("+local") != std::string::npos;
		if (const size_t plus = version.find('+'); plus != std::string::npos) version.erase(plus);
		const bool dirty = version.size() >= 6 && version.compare(version.size() - 6, 6, "-dirty") == 0;
		if (dirty) version.erase(version.size() - 6);
		if (version.size() > 8) version.erase(8);
		if (!version.empty() && (local || dirty)) version = tr("cores.local_version", {version});
		std::string dated = c.dated ? europeanTime(c.date) : std::string();
		if (!dated.empty() && !version.empty()) dated += "  (" + version + ")";
		else if (dated.empty()) dated = version;
		c.datedText = dated;
		std::string build;
		if (shared)
		{
			const char *perr = nullptr;
			if (ce_package *p = ce_package_open(c.path.c_str(), &perr))
			{
				const char *sha = ce_package_sha1(p);
				if (sha != nullptr && std::string(sha).size() >= 8) build = "  " + tr("cores.package_sha", {std::string(sha).substr(0, 8)});
				ce_package_free(p);
			}
		}
		c.line = c.coreName + (dated.empty() ? "" : "  " + dated) + build + "  -  " + c.systems;
		/* listed all the same, with why: a good core for another Chimera */
		if (!c.refused.empty()) c.line += "  -  " + c.refused;
	}
	const int64_t n = static_cast<int64_t>(found.size());
	/* Chimera's Core Manager: a row a core (its versions together, newest first), the emulators before the games, each by name */
	std::vector<Managed> managed;
	for (size_t i = 0; i < found.size(); i++)
	{
		if (managed.empty() || folded(managed.back().name) != folded(found[i].coreName)) managed.push_back({found[i].coreName, false, {}});
		managed.back().versions.push_back(i);
		managed.back().gameCore = managed.back().gameCore || found[i].gameCore;
	}
	std::stable_sort(managed.begin(), managed.end(), [&](const Managed &a, const Managed &b) {
		if (a.gameCore != b.gameCore) return !a.gameCore;
		return folded(a.name) < folded(b.name);
	});
	{
		std::lock_guard<std::mutex> lock(m_);
		cores_ = std::move(found);
		managed_ = std::move(managed);
	}
	count.set(n);
	status.set(folder.empty() ? tr("cores.no_folder") : tr("cores.packages_in", n, {folder}));
	rows_.reset.emit();
	sayManager();
	scanned.emit();
}

std::string CoresViewModel::packageFor(const std::string &coreName, const std::string &pinnedSha1, std::string &note) const
{
	std::vector<Core> builds;
	std::string refused;
	{
		std::lock_guard<std::mutex> lock(m_);
		for (const Core &c : cores_)
		{
			if (c.coreName != coreName) continue;
			/* one this Chimera cannot run is passed over; when it is all there is, why */
			if (!c.refused.empty())
			{
				if (refused.empty()) refused = c.refused;
				continue;
			}
			builds.push_back(c);
		}
	}
	if (builds.empty())
	{
		if (!refused.empty()) note = refused;
		return {};
	}
	/* the pinned one, when it is here: only then is its hash worth computing */
	if (!pinnedSha1.empty())
		for (const Core &c : builds)
		{
			const char *perr = nullptr;
			ce_package *p = ce_package_open(c.path.c_str(), &perr);
			if (p == nullptr) continue;
			const char *sha = ce_package_sha1(p);
			const bool match = sha != nullptr && pinnedSha1 == sha;
			ce_package_free(p);
			if (match) return c.path;
		}
	/* else the most recently installed (they are sorted newest first) */
	if (!pinnedSha1.empty()) note = tr("cores.newest_instead", {coreName});
	return builds.front().path;
}

std::string CoresViewModel::abiRefusal(const json::Json &cfg)
{
	const json::Json *declared = cfg.find("abi");
	const int64_t abi = declared != nullptr && declared->kind == json::Json::Kind::Number ? static_cast<int64_t>(declared->number()) : 1;
	if (abi > kGuestAbi)
		return tr("cores.abi_too_new", {std::to_string(abi), std::to_string(kGuestAbi)});
	if (abi < kOldestGuestAbi)
		return tr("cores.abi_too_old", {std::to_string(abi), std::to_string(kOldestGuestAbi)});
	return {};
}

std::vector<std::pair<std::string, std::string>> CoresViewModel::coresFor(const std::string &extension) const
{
	std::vector<std::pair<std::string, std::string>> out;
	std::lock_guard<std::mutex> lock(m_);
	/* sorted by core, newest first: the first of each core is the one */
	for (const Core &c : cores_)
	{
		if (!out.empty() && out.back().first == c.coreName) continue;
		if (!c.refused.empty()) continue;
		if (std::find(c.extensions.begin(), c.extensions.end(), extension) != c.extensions.end())
			out.emplace_back(c.coreName, c.path);
	}
	return out;
}

std::vector<std::pair<std::string, std::string>> CoresViewModel::installed() const
{
	std::vector<std::pair<std::string, std::string>> out;
	std::lock_guard<std::mutex> lock(m_);
	for (const Core &c : cores_)
		if (out.empty() || out.back().first != c.coreName) out.emplace_back(c.coreName, c.path);
	return out;
}

std::vector<std::pair<std::string, std::string>> CoresViewModel::systemPairs(const json::Json &cfg)
{
	std::vector<std::string> ids;
	if (const json::Json *machines = cfg.find("machines"))
		for (const json::Json &m : machines->items)
			if (const json::Json *id = m.find("id"); id != nullptr && !id->text.empty() && std::find(ids.begin(), ids.end(), id->text) == ids.end())
				ids.push_back(id->text);
	if (ids.empty())
		if (const json::Json *one = cfg.find("systemId"); one != nullptr && !one->text.empty()) ids.push_back(one->text);
	const json::Json *names = cfg.find("systemNames");
	std::vector<std::pair<std::string, std::string>> out;
	for (const std::string &id : ids)
	{
		const json::Json *word = names != nullptr ? names->find(id) : nullptr;
		const bool said = word != nullptr && word->text.find_first_not_of(" \t") != std::string::npos;
		out.emplace_back(id, said ? word->text : id);
	}
	return out;
}

std::string CoresViewModel::licenceSummary(const std::string &packagePath)
{
	const char *perr = nullptr;
	ce_package *p = ce_package_open(packagePath.c_str(), &perr);
	if (p == nullptr) return {};
	uint64_t len = 0;
	json::Json lic;
	std::string err;
	const uint8_t *d = ce_package_entry(p, "licenses/licenses.json", &len);
	const bool read = d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), lic, err);
	ce_package_free(p);
	if (!read) return {};
	/* a core's licence in short: commercial use, the terms, the note, the parts by name */
	auto text = [&](const char *name) {
		const json::Json *v = lic.find(name);
		return v != nullptr && v->kind == json::Json::Kind::String ? v->text : std::string();
	};
	std::vector<std::string> lines;
	std::string commercial = text("commercialUse");
	for (char &ch : commercial) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	if (commercial == "prohibited") lines.push_back(tr("cores.non_commercial"));
	if (!text("effectiveTerms").empty()) lines.push_back(text("effectiveTerms"));
	if (!text("note").empty()) lines.push_back(text("note"));
	std::string named;
	if (const json::Json *parts = lic.find("components"); parts != nullptr && parts->kind == json::Json::Kind::Array)
		for (const json::Json &c : parts->items)
			if (const json::Json *n = c.find("name"); n != nullptr && !n->text.empty())
			{
				const json::Json *l = c.find("license");
				named += (named.empty() ? "" : ", ") + n->text + " (" + (l != nullptr ? l->text : std::string()) + ")";
			}
	if (!named.empty()) lines.push_back(named);
	std::string out;
	for (const std::string &l : lines) out += (out.empty() ? "" : "\n") + l;
	return out;
}

std::vector<const CoresViewModel::Managed *> CoresViewModel::shownManaged() const
{
	std::vector<const Managed *> out;
	for (const Managed &m : managed_)
		if (shows.get() == 0 || (shows.get() == 1 && !m.gameCore) || (shows.get() == 2 && m.gameCore)) out.push_back(&m);
	return out;
}

void CoresViewModel::sayManager()
{
	size_t n = 0;
	{
		std::lock_guard<std::mutex> lock(m_);
		n = managed_.size();
	}
	managerHeader.set(tr("cores.manager_header", static_cast<int64_t>(n), {coresFolder.get()}));
	if (n == 0) managerStatus.set(tr("cores.none_yet"));
	managerRows_.reset.emit();
}

std::string CoresViewModel::removePackage(const std::string &path)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	const fs::path file(path), folder(coresFolder.get());
	/* only a file in the cores folder itself: a package elsewhere is somebody's arrangement */
	if (fs::weakly_canonical(file.parent_path(), ec) != fs::weakly_canonical(folder, ec)) return tr("cores.not_in_folder");
	/* a folder, which is how somebody keeps their own build, is not emptied here */
	if (fs::is_directory(file, ec)) return tr("cores.a_folder");
	if (!fs::remove(file, ec) || ec) return ec ? ec.message() : tr("cores.would_not_go");
	return {};
}

std::string CoresViewModel::systemsOf(const json::Json &cfg)
{
	std::vector<std::string> ids;
	if (const json::Json *machines = cfg.find("machines"))
		for (const json::Json &m : machines->items)
			if (const json::Json *id = m.find("id"); id != nullptr && !id->text.empty() && std::find(ids.begin(), ids.end(), id->text) == ids.end())
				ids.push_back(id->text);
	if (ids.empty())
		if (const json::Json *one = cfg.find("systemId"); one != nullptr && !one->text.empty()) ids.push_back(one->text);
	const json::Json *names = cfg.find("systemNames");
	std::string out;
	for (const std::string &id : ids)
	{
		const json::Json *word = names != nullptr ? names->find(id) : nullptr;
		const bool said = word != nullptr && word->text.find_first_not_of(" \t") != std::string::npos;
		out += (out.empty() ? "" : ", ") + (said ? word->text : id);
	}
	return out;
}

std::vector<CoresViewModel::MediaRecipe> CoresViewModel::mediaRecipes() const
{
	std::vector<MediaRecipe> out;
	for (const Package &p : packages())
	{
		const char *perr = nullptr;
		ce_package *pk = ce_package_open(p.path.c_str(), &perr);
		if (pk == nullptr) continue;
		uint64_t len = 0;
		json::Json cfg;
		std::string err;
		if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
			d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, err))
			if (const json::Json *media = cfg.find("media"); media != nullptr && media->kind == json::Json::Kind::Array)
				for (const json::Json &r : media->items)
				{
					const json::Json *label = r.find("label");
					out.push_back({p.name, label != nullptr ? label->text : "disc", json::write(r)});
				}
		ce_package_free(pk);
	}
	return out;
}

std::vector<CoresViewModel::Package> CoresViewModel::packages() const
{
	std::vector<Package> out;
	std::lock_guard<std::mutex> lock(m_);
	for (const Core &c : cores_) out.push_back({c.coreName, c.line, c.path, c.gameCore});
	return out;
}

std::string CoresViewModel::chosenFirmwareFor(const std::string &coreName, const std::string &id) const
{
	json::Json all;
	std::string error;
	if (!json::parse(chosenFirmware.get(), all, error)) return {};
	const json::Json *mine = all.find(coreName);
	const json::Json *file = mine != nullptr ? mine->find(id) : nullptr;
	return file != nullptr && file->kind == json::Json::Kind::String ? file->text : std::string();
}

std::vector<std::string> CoresViewModel::firmwareFolders() const
{
	std::lock_guard<std::mutex> lock(m_);
	std::vector<std::string> out;
	if (!firmwareM_.empty()) out.push_back(firmwareM_);
	out.insert(out.end(), chosenM_.begin(), chosenM_.end());
	return out;
}

int64_t CoresViewModel::ManagerRows::rowCount() const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	return static_cast<int64_t>(vm_.shownManaged().size());
}

std::string CoresViewModel::ManagerRows::columnName(int32_t column) const
{
	static const char *const names[] = {"core", "type", "systems", "version", "size", "gameCore"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value CoresViewModel::ManagerRows::cell(int64_t row, int32_t column) const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	const std::vector<const Managed *> shown = vm_.shownManaged();
	if (row < 0 || row >= static_cast<int64_t>(shown.size())) return {};
	const Managed &m = *shown[static_cast<size_t>(row)];
	const Core &newest = vm_.cores_[m.versions.front()];
	switch (column)
	{
	case 0: return m.name;
	case 1: return tr(m.gameCore ? "cores.kind_game" : "cores.kind_emulator");
	case 2:
	{
		/* the systems of all its versions, each by the first word a package gives it */
		std::vector<std::pair<std::string, std::string>> systems;
		for (size_t i : m.versions)
			for (const auto &[id, word] : vm_.cores_[i].systemList)
			{
				auto at = std::find_if(systems.begin(), systems.end(), [&](const auto &s) { return s.first == id; });
				if (at == systems.end()) systems.emplace_back(id, word);
				else if (at->second == at->first) at->second = word;
			}
		std::string out;
		for (const auto &s : systems) out += (out.empty() ? "" : ", ") + s.second;
		return out;
	}
	case 3:
	{
		/* the newest version, and how many more there are */
		size_t dated = 0;
		for (size_t i : m.versions) dated += vm_.cores_[i].datedText.empty() ? 0 : 1;
		if (dated == 0) return m.versions.size() == 1 ? std::string() : tr("cores.versions", static_cast<int64_t>(m.versions.size()));
		std::string first;
		for (size_t i : m.versions)
			if (!vm_.cores_[i].datedText.empty())
			{
				first = vm_.cores_[i].datedText;
				break;
			}
		return dated == 1 ? first : first + "  " + tr("cores.more", {std::to_string(dated - 1)});
	}
	case 4:
	{
		/* to a tenth of a megabyte; under one, in kilobytes */
		if (newest.size <= 0) return std::string();
		char buf[32];
		const double mb = static_cast<double>(newest.size) / 1024.0 / 1024.0;
		if (mb < 1.0) std::snprintf(buf, sizeof buf, "%.0f", static_cast<double>(newest.size) / 1024.0);
		else std::snprintf(buf, sizeof buf, "%.1f", mb);
		return tr(mb < 1.0 ? "units.kb" : "units.mb", {buf});
	}
	case 5: return m.gameCore;
	default: return {};
	}
}

int64_t CoresViewModel::Rows::rowCount() const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	return static_cast<int64_t>(vm_.cores_.size());
}

std::string CoresViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"core", "version", "made", "takes", "file"};
	return column >= 0 && column < 5 ? names[column] : "";
}

Value CoresViewModel::Rows::cell(int64_t row, int32_t column) const
{
	std::lock_guard<std::mutex> lock(vm_.m_);
	if (row < 0 || row >= static_cast<int64_t>(vm_.cores_.size())) return {};
	const Core &c = vm_.cores_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return c.coreName;
	case 1: return c.version.substr(0, 12);
	case 2: return c.dated ? europeanTime(c.date) : c.made;
	case 3:
	{
		std::string takes;
		for (const std::string &e : c.extensions) takes += (takes.empty() ? "." : " .") + e;
		return takes;
	}
	case 4: return std::filesystem::path(c.path).filename().string();
	default: return {};
	}
}

} // namespace chimera::vm
