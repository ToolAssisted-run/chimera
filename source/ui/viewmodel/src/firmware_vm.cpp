#include "chimera/vm/firmware_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <map>

#include "chimera/engine.h"
#include "chimera/vm/firmware.hpp"

namespace chimera::vm {

namespace fs = std::filesystem;

namespace {
std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string upper(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

std::string field(const json::Json &j, const char *name)
{
	const json::Json *v = j.find(name);
	return v != nullptr && v->kind == json::Json::Kind::String ? v->text : std::string();
}

/* a condition put in brackets when it joins others */
std::string grouped(const std::string &s)
{
	return s.find(tr("firmware.and")) != std::string::npos || s.find(tr("firmware.or")) != std::string::npos ? "(" + s + ")" : s;
}

std::string literal(const json::Json &v) { return v.kind == json::Json::Kind::String ? v.text : json::write(v); }

bool sameFolder(const std::string &a, const std::string &b)
{
	if (a.empty() || b.empty()) return false;
	std::error_code ec;
	const fs::path x = fs::weakly_canonical(a, ec), y = fs::weakly_canonical(b, ec);
	return lower(x.lexically_normal().string()) == lower(y.lexically_normal().string());
}

/* the file is in the folder or one of its sub-folders, which the Firmware
 * folder's search walks too, as Chimera's (chimera#359) */
bool underFolder(const std::string &file, const std::string &folder)
{
	if (file.empty() || folder.empty()) return false;
	std::error_code ec;
	for (fs::path at = fs::weakly_canonical(file, ec).parent_path(); !at.empty(); at = at.parent_path())
	{
		if (sameFolder(at.string(), folder)) return true;
		if (at == at.parent_path()) break;
	}
	return false;
}
} // namespace

std::string FirmwareViewModel::conditionText(const json::Json &when)
{
	if (when.kind != json::Json::Kind::Object) return {};
	if (const json::Json *setting = when.find("setting"))
	{
		if (const json::Json *is = when.find("is")) return literal(*setting) + " = " + literal(*is);
		if (const json::Json *in = when.find("in"); in != nullptr && in->kind == json::Json::Kind::Array)
		{
			std::string list;
			for (const json::Json &o : in->items) list += (list.empty() ? "" : ", ") + literal(o);
			return tr("firmware.setting_in", {literal(*setting), list});
		}
		return literal(*setting);
	}
	if (const json::Json *slot = when.find("slot"))
	{
		if (const json::Json *ext = when.find("extension")) return tr("firmware.extension_in_slot", {literal(*ext), literal(*slot)});
		return tr("firmware.file_in_slot", {literal(*slot)});
	}
	for (const char *join : {"all", "any"})
		if (const json::Json *list = when.find(join); list != nullptr && list->kind == json::Json::Kind::Array)
		{
			std::string out;
			for (const json::Json &c : list->items)
				if (const std::string t = conditionText(c); !t.empty()) out += (out.empty() ? "" : tr(join[1] == 'l' ? "firmware.and" : "firmware.or")) + grouped(t);
			return out;
		}
	if (const json::Json *no = when.find("not")) return tr("firmware.not", {grouped(conditionText(*no))});
	return {};
}

std::string FirmwareViewModel::Row::statusText() const
{
	switch (state)
	{
	case Good:
		if (!sha1.empty()) return tr("project_files.found");
		if (!path.empty() && !name.empty() && lower(fs::path(path).filename().string()) != lower(name)) return tr("firmware.found_by_size_name");
		return tr("firmware.found_by_name");
	case Unrecognised: return tr("firmware.unrecognised");
	case Custom: return tr("firmware.custom", {std::to_string(size)});
	case Unreadable: return tr("firmware.gone");
	default: return candidates >= 2 ? tr("firmware.candidates", {std::to_string(candidates)}) : tr("project_files.not_found");
	}
}

std::string FirmwareViewModel::Row::whereText() const
{
	switch (where)
	{
	case FirmwareFolder: return tr("firmware.firmware_folder");
	case Chosen: return path.empty() ? tr("project_files.chosen") : tr("firmware.chosen_at", {path});
	default: return {};
	}
}

std::string FirmwareViewModel::summaryOf(const Group &g) const
{
	if (g.rows.empty()) return tr("firmware.needs_none");
	std::vector<std::string> ids;
	size_t onHand = 0;
	bool unpinned = true;
	for (size_t i : g.rows)
	{
		const Row &r = rows_[i];
		if (std::find(ids.begin(), ids.end(), r.id) == ids.end()) ids.push_back(r.id);
		onHand += r.onHand() ? 1 : 0;
		unpinned = unpinned && r.sha1.empty();
	}
	if (ids.size() == 1 && g.rows.size() > 1)
		return tr("firmware.dumps_on_hand", {rows_[g.rows[0]].display, std::to_string(onHand), std::to_string(g.rows.size())});
	size_t satisfied = 0;
	for (const std::string &id : ids)
		satisfied += std::any_of(g.rows.begin(), g.rows.end(), [&](size_t i) { return rows_[i].id == id && rows_[i].onHand(); }) ? 1 : 0;
	const std::string files = tr("firmware.files", static_cast<int64_t>(ids.size()));
	const std::string found = ids.size() == 1 ? tr(satisfied == 1 ? "firmware.on_hand" : "firmware.not_on_hand") : tr("firmware.some_on_hand", {std::to_string(satisfied)});
	return files + " · " + found + (unpinned ? " · " + tr("firmware.unpinned") : "");
}

std::string FirmwareViewModel::keyOf(const Row &r) { return r.sha1.empty() ? r.id : r.id + "#" + upper(r.sha1); }

FirmwareViewModel::FirmwareViewModel(Dispatcher &dispatcher, CoresViewModel &cores) : ViewModel("firmware", dispatcher), cores_(cores)
{
	expose("status", "what the firmware window last had to say", status);
	expose("missing", "declarations with nothing on hand", missing);
	expose("shows", "which cores' files are listed: 0 all, 1 the emulators', 2 the game cores'", shows, true);
	expose("showAll", "every release of an id declared many times, not only the dumps on hand", showAll, true);
	expose("picked", "the line picked in the list (-1: none)", picked, true);
	expose("detail", "what the line picked says in full: what it is, what the core expects, what is on hand", detail);
	expose("canSet", "a declaration is picked (Set File...)", canSet);
	expose("canClear", "the declaration picked has a file chosen by hand (Clear)", canClear);
	expose("pickTitle", "a file picker's title for the line picked", pickTitle);
	rows("lines", "one line of the list: core, summary (the core's line), kind (row, none or more), mark (0 right, 1 used "
		"anyway, 2 gone, 3 nothing), firmware, release, expected, found, where, status, state, heading (the core and its line)", model_);
	expose("find", "Chimera's Find box: lines whose core, firmware name, id or release, file name asked for or file found "
		"holds this, any case", find, true);
	/* a core's files folded to one line ("+ PCSX2: 3 files") and opened again, as Chimera's (issue 368) */
	auto coreOf = [this](const Args &a, std::string &core, std::string &error) {
		if (a.empty()) return false;
		if (std::holds_alternative<std::string>(a[0])) return arg(a, 0, core, error);
		int64_t l = -1;
		if (!arg(a, 0, l, error)) return false;
		if (l < 0 || l >= static_cast<int64_t>(lines_.size()))
		{
			error = tr("new_project.no_line", {std::to_string(l)});
			return false;
		}
		core = groups_[lines_[static_cast<size_t>(l)].group].core;
		return true;
	};
	command("collapse", "folds a core's files to one line (a core's name, or a line of it)", {"core"},
		[this, coreOf](const Args &a, std::string &error) -> Value {
			std::string core;
			if (!coreOf(a, core, error)) return {};
			for (const Group &g : groups_)
				if (g.core == core && !g.rows.empty() && collapsed_.insert(core).second) render();
			return {};
		});
	command("expand", "opens a folded core's files again (a core's name, or a line of it)", {"core"},
		[this, coreOf](const Args &a, std::string &error) -> Value {
			std::string core;
			if (!coreOf(a, core, error)) return {};
			if (collapsed_.erase(core) > 0) render();
			return {};
		});
	command("collapseAll", "folds every core's files", {}, [this](const Args &, std::string &) -> Value {
		for (const Group &g : groups_)
			if (!g.rows.empty()) collapsed_.insert(g.core);
		render();
		return {};
	});
	command("expandAll", "opens every core's files", {}, [this](const Args &, std::string &) -> Value {
		collapsed_.clear();
		render();
		return {};
	});
	command("folderOf", "where Open Folder goes for a line: the folder of the file found for it; \"\" when the line "
		"names no file or its folder is gone", {"line"}, [this](const Args &a, std::string &error) -> Value {
			const Row *r = nullptr;
			if (!lineOf(a, r, error)) return {};
			std::error_code ec;
			const fs::path folder = r->path.empty() ? fs::path() : fs::path(r->path).parent_path();
			if (folder.empty() || !fs::is_directory(folder, ec))
			{
				status.set(r->path.empty() ? tr("firmware.no_file_for", {r->display}) : tr("firmware.folder_gone", {r->path}));
				return std::string();
			}
			return folder.string();
		});
	command("refresh", "asks every installed core again what firmware it declares, and looks for each (Rescan)", {},
		[this](const Args &, std::string &) -> Value {
			survey();
			return static_cast<int64_t>(rows_.size());
		});
	command("setFile", "a file of the person's for a line (remembered where it is, not copied; a pinned declaration's "
		"under its own hash); its size and hash decide what it is", {"line", "path"}, [this](const Args &a, std::string &error) -> Value {
			const Row *r = nullptr;
			std::string path;
			if (!lineOf(a, r, error) || !arg(a, 1, path, error)) return {};
			if (!fs::is_regular_file(path))
			{
				error = tr("new_project.not_a_file", {path});
				return {};
			}
			const Row was = *r;
			cores_.rememberFirmware(was.core, keyOf(was), path);
			survey();
			for (const Row &now : rows_)
				if (now.core == was.core && keyOf(now) == keyOf(was)) status.set(fs::path(path).filename().string() + ": " + now.statusText());
			return {};
		});
	command("clearFile", "forgets the file chosen for a line; it is looked for in the firmware folder again", {"line"},
		[this](const Args &a, std::string &error) -> Value {
			const Row *r = nullptr;
			if (!lineOf(a, r, error)) return {};
			const Row was = *r;
			cores_.rememberFirmware(was.core, keyOf(was), "");
			if (!was.sha1.empty() && cores_.chosenFirmwareFor(was.core, was.id) == was.path) cores_.rememberFirmware(was.core, was.id, "");
			survey();
			return {};
		});
	command("scanFolder", "hashes the files in a folder (and below it when asked) and remembers each that answers a "
		"declaration of an installed core, as Chimera's; a folder that answered any becomes the firmware folder when the "
		"one named is not there. Says how many", {"folder", "subfolders"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder;
			bool recurse = true;
			if (!arg(a, 0, folder, error)) return {};
			if (a.size() > 1 && !arg(a, 1, recurse, error)) return {};
			if (!fs::is_directory(folder))
			{
				error = tr("new_project.not_a_folder", {folder});
				return {};
			}
			const std::vector<FirmwareFile> index = indexFirmware({folder}, recurse, 10000);
			int64_t found = 0;
			for (const Row &r : std::vector<Row>(rows_))
			{
				json::Json decl;
				std::string err, how;
				json::parse(r.declJson, decl, err);
				if (const FirmwareFile *f = findFirmware(decl, "", index, how))
				{
					cores_.rememberFirmware(r.core, keyOf(r), f->path);
					found++;
				}
			}
			/* where the person keeps firmware, when the folder named is nowhere real (Chimera issue 293) */
			if (found > 0 && !fs::is_directory(cores_.firmwareFolder.get())) cores_.firmwareFolder.set(folder);
			survey();
			status.set(tr("firmware.found_in", found, {folder}));
			return found;
		});
	showsWatch_ = shows.changed.connect([this] { render(); });
	showAllWatch_ = showAll.changed.connect([this] { render(); });
	findWatch_ = find.changed.connect([this] { render(); });
	pickedWatch_ = picked.changed.connect([this] { describe(); });
}

bool FirmwareViewModel::lineOf(const Args &a, const Row *&out, std::string &error) const
{
	int64_t line = 0;
	if (!arg(a, 0, line, error)) return false;
	if (line < 0 || line >= static_cast<int64_t>(lines_.size()))
	{
		error = tr("new_project.no_line", {std::to_string(line)});
		return false;
	}
	if (lines_[static_cast<size_t>(line)].row < 0)
	{
		error = tr("firmware.not_a_firmware_line", {std::to_string(line)});
		return false;
	}
	out = &rows_[static_cast<size_t>(lines_[static_cast<size_t>(line)].row)];
	return true;
}

void FirmwareViewModel::survey()
{
	rows_.clear();
	groups_.clear();
	const std::string folder = cores_.firmwareFolder.get();
	/* the firmware folder's files and those chosen by hand: names and sizes, read only when a hash is asked */
	/* the folder and its sub-folders, to as many files as a scan reads, as Chimera's */
	const std::vector<FirmwareFile> index = indexFirmware(cores_.firmwareFolders(), true, 10000);
	/* one group a core, its versions together */
	std::map<std::string, std::vector<std::pair<std::string, std::string>>> byCore;
	std::vector<std::string> order;
	for (const auto &[core, package] : cores_.installed())
	{
		if (byCore.find(lower(core)) == byCore.end()) order.push_back(lower(core));
		byCore[lower(core)].emplace_back(core, package);
	}
	for (const std::string &key : order)
	{
		Group g;
		g.core = byCore[key].front().first;
		std::vector<std::string> seen;
		for (const auto &[core, package] : byCore[key])
		{
			const char *perr = nullptr;
			ce_package *pk = ce_package_open(package.c_str(), &perr);
			if (pk == nullptr) continue;
			uint64_t len = 0;
			json::Json cfg;
			std::string err;
			if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
				d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, err))
			{
				if (field(cfg, "kind") == "game") g.gameCore = true;
				if (const json::Json *list = cfg.find("firmware"); list != nullptr && list->kind == json::Json::Kind::Array)
					for (const json::Json &decl : list->items)
					{
						Row r;
						r.core = g.core;
						r.id = field(decl, "id");
						r.display = field(decl, "display").empty() ? r.id : field(decl, "display");
						r.label = field(decl, "label");
						r.sha1 = field(decl, "sha1");
						r.name = field(decl, "name");
						r.description = field(decl, "description");
						if (const json::Json *s = decl.find("size")) r.size = static_cast<int64_t>(s->number());
						if (const json::Json *when = decl.find("requiredWhen")) r.condition = conditionText(*when);
						r.declJson = json::write(decl);
						if (std::find(seen.begin(), seen.end(), r.id + "#" + lower(r.sha1)) != seen.end()) continue;
						seen.push_back(r.id + "#" + lower(r.sha1));
						/* the dump chosen for this declaration, else for its id; one gone is looked for in the folder */
						std::string path = r.sha1.empty() ? std::string() : cores_.chosenFirmwareFor(g.core, keyOf(r));
						if (path.empty()) path = cores_.chosenFirmwareFor(g.core, r.id);
						std::error_code ec;
						if (path.empty() || !fs::is_regular_file(path, ec))
						{
							std::string how;
							if (const FirmwareFile *f = findFirmware(decl, "", index, how)) path = f->path;
						}
						r.path = path;
						if (path.empty())
						{
							r.state = Missing;
							r.candidates = static_cast<int64_t>(hintCandidates(decl, index).size());
						}
						else
						{
							char hash[41] = {};
							uint64_t size = 0;
							if (ce_sha1_file(path.c_str(), hash, &size) != 1)
								r.state = Unreadable;
							else
							{
								r.actualSha1 = upper(hash);
								/* the verdict is the engine's: 0 wrong size, 1 a dump it has never seen, 2 a match */
								std::string accepted;
								for (const std::string &h : acceptedSha1s(decl)) accepted += h + "\n";
								const int32_t v = ce_firmware_state(r.size, accepted.c_str(), static_cast<int64_t>(size), r.actualSha1.c_str());
								r.state = v == 0 ? Custom : v == 1 ? Unrecognised : Good;
							}
							r.where = underFolder(path, folder) ? FirmwareFolder : Chosen;
						}
						r.gameCore = g.gameCore;
						g.rows.push_back(rows_.size());
						rows_.push_back(std::move(r));
					}
			}
			ce_package_free(pk);
		}
		groups_.push_back(std::move(g));
	}
	for (Row &r : rows_)
		for (const Group &g : groups_)
			if (g.core == r.core) r.gameCore = g.gameCore;
	/* the emulators first and the game cores after them, each by name, as every list of cores */
	std::stable_sort(groups_.begin(), groups_.end(), [](const Group &a, const Group &b) {
		if (a.gameCore != b.gameCore) return !a.gameCore;
		return lower(a.core) < lower(b.core);
	});
	int64_t none = 0;
	for (const Row &r : rows_) none += r.onHand() ? 0 : 1;
	missing.set(none);
	render();
}

void FirmwareViewModel::render()
{
	/* the line picked stays picked when it is still listed */
	const long long pickedRow = picked.get() >= 0 && picked.get() < static_cast<int64_t>(lines_.size()) ? lines_[static_cast<size_t>(picked.get())].row : -1;
	const Row was = pickedRow >= 0 && pickedRow < static_cast<long long>(rows_.size()) ? rows_[static_cast<size_t>(pickedRow)] : Row();
	lines_.clear();
	for (size_t gi = 0; gi < groups_.size(); gi++)
	{
		const Group &g = groups_[gi];
		if ((shows.get() == 1 && g.gameCore) || (shows.get() == 2 && !g.gameCore)) continue;
		/* through the Find box: a core that matches nothing is not listed */
		std::vector<size_t> shown;
		for (size_t i : g.rows)
			if (matches(g, rows_[i], find.get())) shown.push_back(i);
		if (!find.get().empty() && shown.empty() && !matches(g, Row(), find.get())) continue;
		if (g.rows.empty())
		{
			lines_.push_back({gi, -1, tr("firmware.needs_none_line"), false, true});
			continue;
		}
		if (collapsed_.count(g.core) > 0)
		{
			lines_.push_back({gi, -1, "+ " + g.core + ": " + tr("firmware.files", static_cast<int64_t>(shown.size())), true});
			continue;
		}
		/* an id at a time, in the order the core declares them */
		std::vector<std::string> ids;
		for (size_t i : shown)
			if (std::find(ids.begin(), ids.end(), rows_[i].id) == ids.end()) ids.push_back(rows_[i].id);
		for (const std::string &id : ids)
		{
			std::vector<size_t> rows;
			for (size_t i : shown)
				if (rows_[i].id == id) rows.push_back(i);
			const bool collapse = !showAll.get() && rows.size() > kCollapseAbove;
			size_t hidden = 0;
			for (size_t i : rows)
			{
				if (collapse && !rows_[i].onHand() && rows_[i].state != Unreadable)
				{
					hidden++;
					continue;
				}
				lines_.push_back({gi, static_cast<long long>(i), {}});
			}
			if (hidden > 0)
				lines_.push_back({gi, -1, tr("firmware.more_releases", {std::to_string(hidden), rows_[rows[0]].display})});
		}
	}
	model_.reset.emit();
	int64_t again = -1;
	for (size_t l = 0; l < lines_.size() && pickedRow >= 0; l++)
		if (lines_[l].row >= 0 && rows_[static_cast<size_t>(lines_[l].row)].core == was.core && keyOf(rows_[static_cast<size_t>(lines_[l].row)]) == keyOf(was) &&
			rows_[static_cast<size_t>(lines_[l].row)].label == was.label)
			again = static_cast<int64_t>(l);
	if (picked.get() != again) picked.set(again);
	else describe();
}

bool FirmwareViewModel::matches(const Group &g, const Row &r, const std::string &text) const
{
	const size_t a = text.find_first_not_of(' '), b = text.find_last_not_of(' ');
	if (a == std::string::npos) return true;
	auto lower = [](std::string s) {
		for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	};
	const std::string want = lower(text.substr(a, b - a + 1));
	auto has = [&](const std::string &s) { return !s.empty() && lower(s).find(want) != std::string::npos; };
	return has(g.core) || has(r.display) || has(r.id) || has(r.label) || has(r.name) ||
		(!r.path.empty() && has(fs::path(r.path).filename().string()));
}

void FirmwareViewModel::describe()
{
	const int64_t l = picked.get();
	if (l < 0 || l >= static_cast<int64_t>(lines_.size()) || lines_[static_cast<size_t>(l)].row < 0)
	{
		detail.set(groups_.empty() ? tr("firmware.no_cores") : "");
		canSet.set(false);
		canClear.set(false);
		pickTitle.set("");
		return;
	}
	const Row &r = rows_[static_cast<size_t>(lines_[static_cast<size_t>(l)].row)];
	canSet.set(true);
	canClear.set(r.where == Chosen);
	pickTitle.set(r.core + ": " + r.display + (r.label.empty() ? std::string() : " - " + r.label));
	const std::string what = !r.description.empty() ? r.description : tr("firmware.bytes", {r.display, std::to_string(r.size)});
	const std::string expected = r.sha1.empty() ? (r.name.empty() ? tr("firmware.any_file") : tr("firmware.file_called", {r.name}))
												: tr("firmware.expects", {upper(r.sha1)});
	const std::string when = r.condition.empty() ? std::string() : " · " + tr("firmware.needed_when", {r.condition});
	const std::string actual = r.path.empty() ? tr("firmware.nothing_yet")
		: r.actualSha1.empty() ? tr("firmware.unreadable", {r.path})
							   : tr("firmware.path_is", {r.path, r.actualSha1});
	detail.set(what + "\n" + expected + when + "\n" + actual);
}

std::string FirmwareViewModel::Lines::columnName(int32_t column) const
{
	static const char *const names[] = {"core", "summary", "kind", "mark", "firmware", "release", "expected", "found", "where", "status", "state",
		"heading"};
	return column >= 0 && column < 12 ? names[column] : "";
}

Value FirmwareViewModel::Lines::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Line &line = vm_.lines_[static_cast<size_t>(row)];
	const Group &g = vm_.groups_[line.group];
	if (column == 0) return g.core;
	if (column == 1) return vm_.summaryOf(g);
	if (column == 11) return g.core + "  \u00b7  " + vm_.summaryOf(g);
	if (line.row < 0)
	{
		switch (column)
		{
		case 2: return std::string(line.folded ? "folded" : line.none ? "none" : "more");
		case 3: return int64_t{3};
		case 4: return line.text;
		case 5: return line.folded ? tr("firmware.unfold_hint") : std::string();
		case 10: return int64_t{Missing};
		default: return std::string();
		}
	}
	const Row &r = vm_.rows_[static_cast<size_t>(line.row)];
	switch (column)
	{
	case 2: return std::string("row");
	case 3: return int64_t{r.state == Good ? 0 : r.state == Unrecognised || r.state == Custom ? 1 : r.state == Unreadable ? 2 : 3};
	case 4: return r.display;
	case 5: return !r.label.empty() ? r.label : r.condition.empty() ? std::string() : tr("firmware.when", {r.condition});
	case 6: return r.sha1.empty() ? tr("firmware.any") : upper(r.sha1).substr(0, 8);
	case 7: return r.path.empty() ? std::string() : fs::path(r.path).filename().string();
	case 8: return r.where == Chosen ? tr("project_files.chosen") : r.whereText();
	case 9: return r.statusText();
	case 10: return static_cast<int64_t>(r.state);
	default: return {};
	}
}

} // namespace chimera::vm
