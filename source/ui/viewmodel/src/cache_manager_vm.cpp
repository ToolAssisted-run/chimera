#include "chimera/vm/cache_manager_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <tuple>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>

#include "chimera/vm/json.hpp"
#include "chimera/vm/process_stamp.hpp"

namespace chimera::vm {

namespace
{
/* a cache item no file of which was ever written: older than any, and no date */
constexpr int64_t kNeverUsed = std::numeric_limits<int64_t>::min();
} // namespace


namespace {
/* a project's id as a folder name: one name, never "." or ".." (which would put
 * a file in the cache folder's parent), never empty */
bool plainId(const std::string &id)
{
	return !id.empty() && id != "." && id != ".." && id.find_first_of("/\\") == std::string::npos;
}
/* a folder written one way: "a/b/" and "a/./b" are "a/b" */
std::filesystem::path plainPath(const std::filesystem::path &p)
{
	std::filesystem::path n = p.lexically_normal();
	if (n.has_relative_path() && n.filename().empty()) n = n.parent_path();
	return n;
}
} // namespace

namespace fs = std::filesystem;

namespace {
const char *const kProjectCache = "Project cache";
const char *const kUnsavedWork = "Unsaved work";

std::string readText(const fs::path &file)
{
	std::ifstream in(file, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/* a whole number as written: a process's start time does not fit a double */
int64_t whole(const json::Json *v, int64_t otherwise)
{
	if (v == nullptr || v->kind != json::Json::Kind::Number) return otherwise;
	try
	{
		return static_cast<int64_t>(std::stoll(v->text));
	}
	catch (...)
	{
		return otherwise;
	}
}

/* what somebody has said about each place: locked or not, where it is not the default */
std::map<std::string, bool> readLocks(const std::string &file)
{
	std::map<std::string, bool> said;
	json::Json j;
	std::string err;
	const std::string text = readText(file);
	if (text.empty() || !json::parse(text, j, err) || j.kind != json::Json::Kind::Object) return said;
	for (const auto &[place, value] : j.members)
		if (value.kind == json::Json::Kind::Bool) said[place] = value.boolean;
	return said;
}

bool lockedByDefault(const std::string &kind) { return kind != kProjectCache; }
} // namespace

CacheManagerViewModel::CacheManagerViewModel(Dispatcher &dispatcher) : ViewModel("cacheManager", dispatcher)
{
	freeSpaceAt = [](const std::string &place) -> uint64_t {
		std::error_code ec;
		const fs::space_info s = fs::space(place, ec);
		return ec ? UINT64_MAX : static_cast<uint64_t>(s.available);
	};
	expose("cacheFolder", "where the project caches are", cacheFolder, true);
	expose("recoveryFolder", "where unsaved work is kept against a crash", recoveryFolder, true);
	expose("total", "all of it together, as it reads", total);
	expose("inUse", "the open project's cache folder name, \"\" for none", inUse, true);
	expose("autoClean", "the cache is held under its limit by itself, the oldest unlocked entries going first, at the "
		"start and when a project closes", autoClean, true);
	expose("limitGb", "what the whole cache may weigh, in gigabytes", limitGb, true);
	expose("floorGb", "how much of the disk to leave free whatever the limit says, in gigabytes", floorGb, true);
	expose("overLimit", "the line saying the cache is over its limit, and what that means; \"\" when it is not", overLimit);
	expose("status", "what the cache manager last did", status);
	rows("caches", "one row an entry: title, project, core, system, games, size (bytes), sizeText, orphan (its "
		"project is not where it was last seen), folder, open (in use), note, kind (\"Project cache\" or \"Unsaved work\"), "
		"locked (kept from the auto-clean), lastUsed", rows_);
	command("refresh", "looks at what is kept again", {}, [this](const Args &, std::string &) -> Value {
		survey();
		return static_cast<int64_t>(items_.size());
	});
	expose("header", "what the window says over the list: how many, how much, the orphans, and whether it is over its limit", header);
	expose("sortColumn", "the column the list is sorted by, as Chimera's (9 the size, 10 when last modified...)", sortColumn);
	expose("sortAscending", "the list sorted from the least up", sortAscending);
	command("sortBy", "sorts by a column, the other way round when it is the one sorted by already; size and date start "
		"largest and newest, as Chimera's", {"column"}, [this](const Args &a, std::string &error) -> Value {
			int64_t column = 0;
			if (!arg(a, 0, column, error)) return {};
			if (column == sortColumn.get()) sortAscending.set(!sortAscending.get());
			else
			{
				sortColumn.set(column);
				sortAscending.set(column != 9 && column != 10);
			}
			sortItems();
			rows_.reset.emit();
			return {};
		});
	command("folders", "the entries' places on disk, as a JSON array: \"free\" every one not in use (Select all), \"orphans\" "
		"those whose project is gone and not in use (Select all orphans)", {"which"}, [this](const Args &a, std::string &error) -> Value {
			std::string which;
			if (!arg(a, 0, which, error)) return {};
			json::Json list;
			list.kind = json::Json::Kind::Array;
			for (const Item &i : items_)
				if (!i.open && (which != "orphans" || i.orphan)) list.items.push_back(json::Json::string(i.folder));
			return json::write(list);
		});
	command("firstPick", "the row picked when none is, by its place on disk: the project open now, else the project used most "
		"recently, else the first row (\"\" for none); Greenzone budgets... works on it, and the first row - the largest, "
		"as the list starts sorted - named a project nobody was working on (Chimera issue 369)", {},
		[this](const Args &, std::string &) -> Value {
			const Item *newest = nullptr;
			for (const Item &i : items_)
			{
				if (i.kind != kProjectCache) continue;
				if (i.open) return i.folder;
				if (newest == nullptr || i.lastUsed > newest->lastUsed) newest = &i;
			}
			if (newest != nullptr) return newest->folder;
			return items_.empty() ? std::string() : items_.front().folder;
		});
	command("placesOf", "the places in the list (\"0,2\") of entries named by their places on disk (a JSON array)", {"folders"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			json::Json folders;
			if (!json::parse(text, folders, error)) return {};
			std::string list;
			for (size_t i = 0; i < items_.size(); i++)
				for (const json::Json &f : folders.items)
					if (f.text == items_[i].folder) list += (list.empty() ? "" : ",") + std::to_string(i);
			return list;
		});
	command("projectOf", "the project of the entry at a place in the list, for its budgets: {\"id\", \"title\"}; the id is "
		"\"\" for what is not a project's cache", {"row"}, [this](const Args &a, std::string &error) -> Value {
			int64_t row = 0;
			if (!arg(a, 0, row, error)) return {};
			json::Json o;
			o.kind = json::Json::Kind::Object;
			const bool project = row >= 0 && row < static_cast<int64_t>(items_.size()) && !items_[static_cast<size_t>(row)].unsavedWork;
			o.members.emplace_back("id", json::Json::string(project ? fs::path(items_[static_cast<size_t>(row)].folder).filename().string() : std::string()));
			o.members.emplace_back("title", json::Json::string(project ? items_[static_cast<size_t>(row)].title : std::string()));
			return json::write(o);
		});
	command("budgetsOf", "a project's own greenzone budgets, by its id (its cache folder's name), as Chimera keeps them "
		"beside its cache: {\"memoryMb\", \"maxNearStride\"} (0 for every project's)", {"id"}, [this](const Args &a, std::string &error) -> Value {
			std::string id;
			if (!arg(a, 0, id, error)) return {};
			if (!plainId(id))
			{
				error = tr("cache.project_id");
				return {};
			}
			json::Json b;
			std::string err;
			int64_t mb = 0, stride = 0;
			if (json::parse(readText(fs::path(cacheFolder.get()) / id / "budgets.json"), b, err) && b.kind == json::Json::Kind::Object)
			{
				if (const json::Json *v = b.find("memoryMb"); v != nullptr && v->kind == json::Json::Kind::Number) mb = static_cast<int64_t>(v->number());
				if (const json::Json *v = b.find("maxNearStride"); v != nullptr && v->kind == json::Json::Kind::Number) stride = static_cast<int64_t>(v->number());
			}
			json::Json o;
			o.kind = json::Json::Kind::Object;
			o.members.emplace_back("memoryMb", json::Json::integer(mb));
			o.members.emplace_back("maxNearStride", json::Json::integer(stride));
			return json::write(o);
		});
	command("setBudgetsOf", "a project's own greenzone budgets, by its id, kept beside its cache (0 and 0: none, every "
		"project's apply); they take effect when it next opens", {"id", "memoryMb", "nearStride"}, [this](const Args &a, std::string &error) -> Value {
			std::string id;
			int64_t mb = 0, stride = 0;
			if (!arg(a, 0, id, error) || !arg(a, 1, mb, error) || !arg(a, 2, stride, error)) return {};
			if (!plainId(id) || (mb != 0 && mb < 64) || stride < 0 || stride > 32)
			{
				error = tr("cache.budget_args");
				return {};
			}
			const fs::path dir = fs::path(cacheFolder.get()) / id;
			std::error_code ec;
			if (mb == 0 && stride == 0)
			{
				fs::remove(dir / "budgets.json", ec);
				return {};
			}
			json::Json b;
			b.kind = json::Json::Kind::Object;
			if (mb != 0) b.members.emplace_back("memoryMb", json::Json::integer(mb));
			if (stride != 0) b.members.emplace_back("maxNearStride", json::Json::integer(stride));
			fs::create_directories(dir, ec);
			std::ofstream(dir / "budgets.json", std::ios::binary | std::ios::trunc) << json::write(b);
			return {};
		});
	command("detailOf", "what the entry at a place in the list means, in full, as Chimera says it under the list", {"row"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t row = 0;
			if (!arg(a, 0, row, error)) return {};
			return rows_.cell(row, 18);
		});
	command("costs", "what removing the entries at these places (\"0,2\") costs, each different cost a line, and how much "
		"they free, for Chimera's question: {\"count\", \"size\", \"costs\"}", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			uint64_t bytes = 0;
			std::vector<std::string> costs;
			for (size_t i : picked)
			{
				bytes += items_[i].bytes;
				const std::string c = tr(items_[i].unsavedWork ? "cache.cost_unsaved" : "cache.cost_project");
				if (std::find(costs.begin(), costs.end(), c) == costs.end()) costs.push_back(c);
			}
			std::string lines;
			for (const std::string &c : costs) lines += (lines.empty() ? "" : "\n") + c;
			json::Json o;
			o.kind = json::Json::Kind::Object;
			o.members.emplace_back("count", json::Json::integer(static_cast<long long>(picked.size())));
			o.members.emplace_back("size", json::Json::string(sizeText(bytes)));
			o.members.emplace_back("costs", json::Json::string(lines));
			return json::write(o);
		});
	command("remove", "removes the entries at these places in the list (\"0,2\"), locked or not: a project cache's run "
		"replays instead of resuming; unsaved work is gone", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			std::string failed;
			const int64_t removed = removeAll(picked, failed);
			survey();
			status.set(failed.empty() ? tr("cache.removed", removed) : tr("cache.removed_left", removed, {failed}));
			if (!failed.empty()) error = tr("cache.could_not_remove", {failed});
			return removed;
		});
	command("orphans", "the places in the list of entries whose project is gone (\"1,3\")", {}, [this](const Args &, std::string &) -> Value {
		std::string list;
		for (size_t i = 0; i < items_.size(); i++)
			if (items_[i].orphan) list += (list.empty() ? "" : ",") + std::to_string(i);
		return list;
	});
	command("toggleLock", "locks the entries at these places (\"0,2\") when any of them is unlocked, else unlocks them all, "
		"as Chimera's Lock / Unlock", {"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			if (picked.empty()) return {};
			bool locking = false;
			for (size_t i : picked) locking = locking || !items_[i].locked;
			std::map<std::string, bool> said = readLocks(locksFile());
			for (size_t i : picked)
			{
				/* only what differs from its kind's default is written down */
				if (locking == lockedByDefault(items_[i].kind)) said.erase(items_[i].folder);
				else said[items_[i].folder] = locking;
			}
			json::Json book;
			book.kind = json::Json::Kind::Object;
			std::error_code ec;
			for (const auto &[place, locked] : said)
				if (fs::is_directory(place, ec)) book.members.emplace_back(place, json::Json::boolean_(locked));
			fs::create_directories(cacheFolder.get(), ec);
			std::ofstream(locksFile(), std::ios::binary | std::ios::trunc) << json::write(book);
			survey();
			status.set(tr(locking ? "cache.locked_items" : "cache.unlocked_items", static_cast<int64_t>(picked.size())));
			return locking;
		});
	command("titles", "the entries at these places (\"0,2\"), a line each: \"- title (size)\", for a question to name them",
		{"rows"}, [this](const Args &a, std::string &error) -> Value {
			std::vector<size_t> picked;
			if (!rowsOf(a, picked, error)) return {};
			std::string lines;
			for (size_t i : picked) lines += (lines.empty() ? "- " : "\n- ") + items_[i].title + " (" + sizeText(items_[i].bytes) + ")";
			return lines;
		});
	command("wouldGo", "the places in the list Clean Now would take (\"1,3\"), oldest first, to come under the limit", {},
		[this](const Args &, std::string &) -> Value {
			std::string list;
			for (size_t i : whatWouldGo(items_, limitNow(), {})) list += (list.empty() ? "" : ",") + std::to_string(i);
			return list;
		});
	command("cleanNow", "does now what the auto-clean would, whether it is on or not: the oldest unlocked entries until "
		"the cache is under its limit", {}, [this](const Args &, std::string &) -> Value {
			uint64_t all = 0;
			for (const Item &i : items_) all += i.bytes;
			const uint64_t limit = limitNow();
			const std::vector<size_t> going = whatWouldGo(items_, limit, {});
			if (going.empty())
			{
				status.set(tr(all > limit ? "cache.over_nothing_to_take" : "cache.under_limit"));
				return int64_t{0};
			}
			uint64_t freed = 0;
			for (size_t i : going) freed += items_[i].bytes;
			std::string failed;
			const int64_t removed = removeAll(going, failed);
			survey();
			status.set(tr("cache.cleaned", removed, {sizeText(freed)}) + (failed.empty() ? "" : " " + tr("cache.could_not_remove", {failed})));
			return removed;
		});
	command("runAutoClean", "the auto-clean, when it is on: the oldest unlocked entries until the cache is under its limit, "
		"never the one named (a project cache folder's name, the project just closed); says what it freed", {"spare"},
		[this](const Args &a, std::string &error) -> Value {
			std::string spare;
			if (a.size() > 0 && !arg(a, 0, spare, error)) return {};
			if (!autoClean.get()) return std::string();
			survey();
			std::vector<std::string> spared;
			if (!spare.empty()) spared.push_back((fs::path(cacheFolder.get()) / spare).string());
			const std::vector<size_t> going = whatWouldGo(items_, limitNow(), spared);
			if (going.empty()) return std::string();
			uint64_t freed = 0;
			for (size_t i : going) freed += items_[i].bytes;
			std::string failed;
			removeAll(going, failed);
			survey();
			const std::string said = tr("cache.auto_cleaned", {sizeText(freed)});
			status.set(said);
			return said;
		});
	folderWatch_ = cacheFolder.changed.connect([this] { survey(); });
	recoveryWatch_ = recoveryFolder.changed.connect([this] { survey(); });
	inUseWatch_ = inUse.changed.connect([this] { survey(); });
	/* a setting: kept at once; nothing is removed while a number is being typed */
	limitWatch_ = limitGb.changed.connect([this] {
		if (limitGb.get() < 1) limitGb.set(1);
		sayOverLimit();
	});
	floorWatch_ = floorGb.changed.connect([this] {
		if (floorGb.get() < 0) floorGb.set(0);
		sayOverLimit();
	});
	autoWatch_ = autoClean.changed.connect([this] { sayOverLimit(); });
}

std::string CacheManagerViewModel::sizeText(uint64_t bytes)
{
	/* a size as Chimera writes one: nothing for nothing, gigabytes and megabytes to a tenth, else whole kilobytes */
	if (bytes == 0) return {};
	const double mb = static_cast<double>(bytes) / 1024.0 / 1024.0;
	char text[32];
	if (mb >= 1024.0) std::snprintf(text, sizeof text, "%.1f", mb / 1024.0);
	else if (mb >= 1.0) std::snprintf(text, sizeof text, "%.1f", mb);
	else std::snprintf(text, sizeof text, "%.0f", std::floor(static_cast<double>(bytes) / 1024.0 + 0.5));
	return tr(mb >= 1024.0 ? "units.gb" : mb >= 1.0 ? "units.mb" : "units.kb", {text});
}

uint64_t CacheManagerViewModel::effectiveLimit(uint64_t limit, uint64_t floor, uint64_t cacheBytes, uint64_t freeBytes, uint64_t never)
{
	if (freeBytes >= floor) return limit;
	/* giving the floor back costs what it is short by, and the cache can only give what it holds */
	const uint64_t shortBy = floor - freeBytes;
	const uint64_t fromCache = cacheBytes > shortBy ? cacheBytes - shortBy : 0;
	/* never less than the part that is always the person's: a disk low for
	 * reasons of its own must not cost every greenzone at every start */
	const uint64_t always = std::min(never, limit);
	return std::max(always, std::min(limit, fromCache));
}

std::vector<size_t> CacheManagerViewModel::whatWouldGo(const std::vector<Item> &items, uint64_t limit, const std::vector<std::string> &spare)
{
	uint64_t total = 0;
	for (const Item &i : items) total += i.bytes;
	if (total <= limit) return {};
	/* the newest is never taken: the last thing worked on survives, and the cache never empties itself */
	size_t newest = items.size();
	for (size_t i = 0; i < items.size(); i++)
		if (items[i].bytes > 0 && (newest == items.size() || items[i].lastUsed > items[newest].lastUsed)) newest = i;
	std::vector<size_t> candidates;
	for (size_t i = 0; i < items.size(); i++)
	{
		const Item &it = items[i];
		if (it.open || it.locked || it.bytes == 0 || i == newest) continue;
		if (std::find(spare.begin(), spare.end(), it.folder) != spare.end()) continue;
		candidates.push_back(i);
	}
	/* oldest first; among the same age the biggest, so the limit costs as few removals as it can */
	std::stable_sort(candidates.begin(), candidates.end(), [&](size_t a, size_t b) {
		if (items[a].lastUsed != items[b].lastUsed) return items[a].lastUsed < items[b].lastUsed;
		return items[a].bytes > items[b].bytes;
	});
	std::vector<size_t> going;
	for (size_t i : candidates)
	{
		if (total <= limit) break;
		going.push_back(i);
		total -= items[i].bytes;
	}
	return going;
}

std::string CacheManagerViewModel::locksFile() const
{
	return cacheFolder.get().empty() ? std::string() : (fs::path(cacheFolder.get()) / "cache-locks.json").string();
}

uint64_t CacheManagerViewModel::limitNow() const
{
	uint64_t all = 0;
	for (const Item &i : items_) all += i.bytes;
	const uint64_t freeBytes = cacheFolder.get().empty() || !freeSpaceAt ? UINT64_MAX : freeSpaceAt(cacheFolder.get());
	return effectiveLimit(static_cast<uint64_t>(limitGb.get()) * unitBytes, static_cast<uint64_t>(floorGb.get()) * unitBytes, all,
		freeBytes, unitBytes);
}

void CacheManagerViewModel::sayOverLimit()
{
	uint64_t all = 0;
	for (const Item &i : items_) all += i.bytes;
	const uint64_t limit = limitNow();
	if (all <= limit)
	{
		overLimit.set("");
		header.set(headerText());
		return;
	}
	std::string line;
	const uint64_t asked = static_cast<uint64_t>(limitGb.get()) * unitBytes;
	if (limit < asked)
	{
		const uint64_t freeBytes = freeSpaceAt ? freeSpaceAt(cacheFolder.get()) : UINT64_MAX;
		line += tr(limit <= unitBytes ? "cache.held_by_disk_floor" : "cache.held_by_disk",
					  {sizeText(freeBytes), std::to_string(floorGb.get()), sizeText(limit), std::to_string(limitGb.get())}) + " ";
	}
	uint64_t going = 0;
	for (size_t i : whatWouldGo(items_, limit, {})) going += items_[i].bytes;
	line += tr("cache.over_by", {sizeText(all - limit)}) + " ";
	line += tr(going < all - limit ? "cache.cannot_meet" : autoClean.get() ? "cache.auto_clean_will" : "cache.nothing_enforcing");
	overLimit.set(line);
	header.set(headerText());
}

bool CacheManagerViewModel::rowsOf(const Args &a, std::vector<size_t> &picked, std::string &error) const
{
	std::string list;
	if (!arg(a, 0, list, error)) return false;
	std::stringstream in(list);
	for (std::string one; std::getline(in, one, ',');)
		if (!one.empty())
		{
			char *end = nullptr;
			const long i = std::strtol(one.c_str(), &end, 10);
			if (end == nullptr || *end != '\0' || i < 0 || static_cast<size_t>(i) >= items_.size())
			{
				error = tr("cache.no_row", {one});
				return false;
			}
			if (std::find(picked.begin(), picked.end(), static_cast<size_t>(i)) == picked.end()) picked.push_back(static_cast<size_t>(i));
		}
	return true;
}

int64_t CacheManagerViewModel::removeAll(const std::vector<size_t> &picked, std::string &failed)
{
	int64_t removed = 0;
	for (size_t i : picked)
	{
		std::error_code ec;
		const Item &item = items_[i];
		/* only ever a folder inside the cache folder or the recovery folder */
		const fs::path folder = item.folder;
		const fs::path root = item.unsavedWork ? fs::path(recoveryFolder.get()) : fs::path(cacheFolder.get());
		/* the same folder written another way (a slash at its end) is still it */
		if (root.empty() || plainPath(folder).parent_path() != plainPath(root))
		{
			/* said, not skipped in silence */
			failed += (failed.empty() ? "" : ", ") + tr("cache.not_in_folder", {item.title});
			continue;
		}
		/* the open project's is being written */
		if (item.open)
		{
			failed += (failed.empty() ? "" : ", ") + tr("cache.in_use_item", {item.title});
			continue;
		}
		fs::remove_all(folder, ec);
		if (ec) failed += (failed.empty() ? "" : ", ") + item.title;
		else removed++;
	}
	return removed;
}

void CacheManagerViewModel::sortItems()
{
	/* built least-first and turned round when it should not be, as Chimera's; the lock: unlocked first */
	auto lower = [](std::string s) {
		for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		return s;
	};
	const int64_t c = sortColumn.get();
	std::stable_sort(items_.begin(), items_.end(), [&](const Item &a, const Item &b) {
		switch (c)
		{
		case 0: return std::make_tuple(a.locked, a.unsavedWork, a.bytes) < std::make_tuple(b.locked, b.unsavedWork, b.bytes);
		case 1: return std::make_tuple(a.unsavedWork, a.bytes) < std::make_tuple(b.unsavedWork, b.bytes);
		case 2: return lower(a.title) < lower(b.title);
		case 3: return lower(a.system) < lower(b.system);
		case 4: return lower(a.core) < lower(b.core);
		case 5: return lower(a.games) < lower(b.games);
		case 6: return lower(fs::path(a.folder).filename().string()) < lower(fs::path(b.folder).filename().string());
		case 7: return lower(a.projectPath) < lower(b.projectPath);
		case 8: return lower(a.folder) < lower(b.folder);
		case 10: return a.lastUsed < b.lastUsed;
		default: return a.bytes < b.bytes;
		}
	});
	if (!sortAscending.get()) std::reverse(items_.begin(), items_.end());
}

std::string CacheManagerViewModel::headerText() const
{
	if (items_.empty()) return tr("cache.empty");
	uint64_t all = 0, orphaned = 0;
	size_t orphans = 0;
	for (const Item &i : items_)
	{
		all += i.bytes;
		if (i.orphan)
		{
			orphans++;
			orphaned += i.bytes;
		}
	}
	return tr("cache.header", static_cast<int64_t>(items_.size()), {sizeText(all)}) +
		(orphans == 0 ? std::string() : "\n" + tr("cache.orphans", static_cast<int64_t>(orphans), {sizeText(orphaned)})) +
		(overLimit.get().empty() ? std::string() : "\n" + overLimit.get());
}

void CacheManagerViewModel::survey()
{
	items_.clear();
	std::error_code ec;
	const std::map<std::string, bool> said = readLocks(locksFile());
	auto measure = [&](Item &item, const fs::path &dir) {
		/* the file clock's zero is not the past (libstdc++ counts today as below it): start at its least */
		fs::file_time_type newest = fs::file_time_type::min();
		for (fs::recursive_directory_iterator f(dir, ec), fend; !ec && f != fend; f.increment(ec))
			if (f->is_regular_file(ec))
			{
				item.bytes += f->file_size(ec);
				const fs::file_time_type t = f->last_write_time(ec);
				if (!ec && t > newest) newest = t;
			}
		ec.clear();
		/* a folder with no files keeps the least time: older than any used (0 is not -
		 * this clock counts today below it); not given to the clocks, which overflow on it.
		 * The least of int64_t, not the clock's own: Apple's library counts this clock in
		 * 128 bits, whose least cut to 64 bits is 0, a date */
		item.lastUsed = newest == fs::file_time_type::min() ? kNeverUsed : static_cast<int64_t>(newest.time_since_epoch().count());
		const auto lock = said.find(item.folder);
		item.locked = lock != said.end() ? lock->second : lockedByDefault(item.kind);
	};
	const fs::path root = cacheFolder.get();
	if (!root.empty() && fs::is_directory(root, ec))
		for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
		{
			if (!it->is_directory(ec)) continue;
			Item item;
			item.kind = kProjectCache;
			item.folder = it->path().string();
			item.title = tr("cache.unnamed_project");
			item.open = !inUse.get().empty() && it->path().filename().string() == inUse.get();
			/* who it belongs to, when it says */
			json::Json about;
			std::string err;
			if (json::parse(readText(it->path() / "about.json"), about, err))
			{
				if (const json::Json *t = about.find("title"); t != nullptr && !t->text.empty()) item.title = t->text;
				if (const json::Json *p = about.find("projectPath")) item.projectPath = p->text;
				if (const json::Json *c = about.find("core")) item.core = c->text;
				if (const json::Json *s = about.find("system")) item.system = s->text;
				if (const json::Json *g = about.find("games"))
					for (const json::Json &one : g->items) item.games += (item.games.empty() ? "" : ", ") + one.text;
			}
			/* as Chimera: only a project whose place is known and empty now; the open one is not missing */
			item.orphan = !item.open && !item.projectPath.empty() && !fs::exists(item.projectPath, ec);
			measure(item, it->path());
			items_.push_back(std::move(item));
		}
	/* unsaved work: a folder per project, by its id, while it has work not
	 * saved, or after a crash until that project opens again */
	const fs::path kept = recoveryFolder.get();
	ec.clear();
	if (!kept.empty() && fs::is_directory(kept, ec))
		for (fs::directory_iterator it(kept, ec), end; !ec && it != end; it.increment(ec))
		{
			/* "recovered" holds rebuilt copies when there is no backup folder: work, not a cache */
			if (!it->is_directory(ec) || it->path().filename() == "recovered") continue;
			Item item;
			item.kind = kUnsavedWork;
			item.unsavedWork = true;
			item.folder = it->path().string();
			json::Json record;
			std::string err;
			int64_t pid = 0, started = -1;
			if (json::parse(readText(it->path() / "session.json"), record, err))
			{
				if (const json::Json *p = record.find("projectPath")) item.projectPath = p->text;
				pid = whole(record.find("processId"), 0);
				started = whole(record.find("processStarted"), -1);
			}
			item.title = item.projectPath.empty() ? tr("cache.unsaved_work") : tr("cache.unsaved_work_of", {fs::path(item.projectPath).stem().string()});
			/* a window still writing it has it open */
			item.open = pid > 0 && started >= 0 && processStarted(pid) == started;
			item.orphan = !item.open && !item.projectPath.empty() && !fs::exists(item.projectPath, ec);
			measure(item, it->path());
			items_.push_back(std::move(item));
		}
	sortItems();
	uint64_t all = 0;
	for (const Item &i : items_) all += i.bytes;
	total.set(tr("cache.total", static_cast<int64_t>(items_.size()), {sizeText(all)}));
	rows_.reset.emit();
	sayOverLimit();
}

std::string CacheManagerViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"title", "project", "core", "system", "games", "size", "sizeText", "orphan", "folder",
		"open", "note", "kind", "locked", "lastUsed", "what", "projectId", "projectFile", "modified", "detail", "lockTip"};
	return column >= 0 && column < 20 ? names[column] : "";
}

Value CacheManagerViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Item &i = vm_.items_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return i.title;
	case 1: return i.projectPath;
	case 2: return i.core;
	case 3: return i.system;
	case 4: return i.games;
	case 5: return static_cast<int64_t>(i.bytes);
	case 6: return sizeText(i.bytes);
	case 7: return i.orphan;
	case 8: return i.folder;
	case 9: return i.open;
	case 10:
		if (i.unsavedWork)
			return tr(i.open ? "cache.note_unsaved_open" : i.orphan ? "cache.note_unsaved_orphan" : "cache.note_unsaved");
		return i.open ? tr("cache.note_open") : i.orphan ? tr("cache.note_orphan") : std::string();
	case 11: return i.kind;
	case 12: return i.locked;
	case 13: return i.lastUsed;
	/* Chimera's columns and the line under the list */
	case 14: return tr(i.unsavedWork ? "cache.unsaved_work" : "cache.project");
	case 15: return i.unsavedWork ? std::string() : fs::path(i.folder).filename().string();
	case 16: return !i.projectPath.empty() ? (i.orphan ? tr("cache.path_not_found", {i.projectPath}) : i.projectPath) : i.unsavedWork ? std::string() : tr("cache.never_recorded");
	case 17:
	{
		if (i.lastUsed == kNeverUsed) return std::string();
		const auto sys = std::chrono::file_clock::to_sys(fs::file_time_type(fs::file_time_type::duration(i.lastUsed)));
		const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::time_point_cast<std::chrono::system_clock::duration>(sys));
		std::tm local{};
#ifdef _WIN32
		localtime_s(&local, &t);
#else
		localtime_r(&t, &local);
#endif
		char buf[32];
		std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M", &local);
		return std::string(buf);
	}
	case 18:
	{
		std::vector<std::string> lines;
		lines.push_back(tr(i.unsavedWork ? "cache.cost_unsaved" : "cache.cost_project"));
		std::string note;
		fromValue(cell(row, 10), note);
		if (i.open) lines.push_back(tr("cache.in_use_now"));
		else if (!note.empty()) lines.push_back(note);
		lines.push_back(tr(i.locked ? "cache.locked_detail" : "cache.unlocked_detail"));
		if (i.games.find(", ") != std::string::npos) lines.push_back(tr("cache.game_files", {i.games}));
		if (!i.projectPath.empty()) lines.push_back(tr("cache.project_file", {i.projectPath}));
		lines.push_back(tr("cache.folder", {i.folder}));
		std::string out;
		for (const std::string &l : lines) out += (out.empty() ? "" : "\n") + l;
		return out;
	}
	case 19: return tr(i.locked ? "cache.locked_tip" : "cache.unlocked_tip");
	default: return {};
	}
}

} // namespace chimera::vm
