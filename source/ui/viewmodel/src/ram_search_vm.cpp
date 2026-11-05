#include "chimera/vm/ram_search_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <thread>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"
#include "chimera/vm/ram_watch_vm.hpp"

namespace chimera::vm {

namespace {
/* A domain with no linear backing is read range by range. A bus is read
 * through the core, which only the machine's thread may call: the list's
 * rows, read on the window's thread, called it there and ended the program
 * (issue 456). The window's thread reads copies of the pages it asks for,
 * which the machine's thread makes and makes again after every frame; a
 * page not copied yet reads as zeros until it is. */
struct DomainReader
{
	ce_session *s = nullptr;
	int32_t index = -1;
	std::thread::id machine;
	std::function<void()> asked;
	std::mutex pagesM;
	std::map<int64_t, std::vector<uint8_t>> pages;
	std::set<int64_t> wanted;
};
constexpr int64_t kPage = 4096;
/* 4 MiB: more than the rows any list shows */
constexpr size_t kMostPages = 1024;

int64_t readDomainRange(void *user, int64_t offset, uint8_t *buf, int64_t len)
{
	auto *r = static_cast<DomainReader *>(user);
	if (std::this_thread::get_id() == r->machine) return ce_session_domain_read(r->s, r->index, offset, buf, len);
	bool missing = false;
	{
		std::lock_guard<std::mutex> lock(r->pagesM);
		for (int64_t done = 0; done < len;)
		{
			const int64_t at = offset + done, page = at / kPage, in = at % kPage, n = std::min(len - done, kPage - in);
			if (const auto it = r->pages.find(page); it != r->pages.end())
				std::memcpy(buf + done, it->second.data() + in, static_cast<size_t>(n));
			else
			{
				std::memset(buf + done, 0, static_cast<size_t>(n));
				missing = r->wanted.insert(page).second || missing;
			}
			done += n;
		}
	}
	if (missing && r->asked) r->asked();
	return len;
}

/* the machine's thread: every copy made again, and the pages asked for made; true when some were asked for */
bool copyPages(DomainReader &r)
{
	std::lock_guard<std::mutex> lock(r.pagesM);
	if (r.pages.size() + r.wanted.size() > kMostPages) r.pages.clear();
	const bool wanted = !r.wanted.empty();
	for (const int64_t page : r.wanted) r.pages[page].resize(kPage);
	r.wanted.clear();
	for (auto &[page, bytes] : r.pages)
		if (ce_session_domain_read(r.s, r.index, page * kPage, bytes.data(), kPage) < kPage) std::fill(bytes.begin(), bytes.end(), 0);
	return wanted;
}

DomainReader g_reader; /* one search at a time */

/* past this, a search starts in Fast mode, as Chimera's */
constexpr int64_t kMaxDetailedSize = 1024 * 1024;

#ifdef _WIN32
constexpr const char *kNewLine = "\r\n";
#else
constexpr const char *kNewLine = "\n";
#endif

std::string addresses(int64_t n) { return tr("ram_search.addresses", n); }

std::string utf8(const std::filesystem::path &p)
{
	const std::u8string u = p.u8string();
	return std::string(reinterpret_cast<const char *>(u.data()), u.size());
}
} // namespace

RamSearchViewModel::RamSearchViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("ramSearch", dispatcher), session_(session)
{
	expose("domains", "the machine's memory domains, comma-separated", domains);
	expose("domain", "the domain the next search is of", domain, true);
	expose("searchedDomain", "the domain the open search is of", searchedDomain);
	expose("size", "bytes a value takes: 1, 2, 4 or 8 (the open search is read again at a new size)", size, true);
	expose("bigEndian", "values are stored most significant byte first", bigEndian, true);
	expose("displays", "the ways a value of this size can be shown, a line each", displays);
	expose("display", "the way values are shown and typed, by its place in displays", display, true);
	expose("fast", "Mode: Fast keeps no change counts, Detailed does", fast, true);
	expose("checkMisaligned", "Check Mis-aligned: a value may start at any address, not only at a multiple of its size", checkMisaligned, true);
	expose("previousType", "the previous value: 0 the first, 1 the one at the last search, 2 the last frame's, 3 the one before the "
		"last change (2 and 3 in Detailed mode only)", previousType, true);
	expose("compare", "Compare To / By: 0 the previous value, 1 a specific value, 2 a specific address, 3 specific changes, "
		"4 a difference", compare, true);
	expose("specificValue", "Specific Value's box, typed as values are shown", specificValue, true);
	expose("specificAddress", "Specific Address's box, in hex", specificAddress, true);
	expose("specificChanges", "Specific Changes' box, a number of changes", specificChanges, true);
	expose("difference", "Difference of's box, typed as values are shown", difference, true);
	expose("op", "the Comparison Operator: 0 equal to, 1 greater than, 2 greater than or equal to, 3 less than, 4 less than or "
		"equal to, 5 not equal to, 6 different by", op, true);
	expose("differentBy", "Different by's box, typed as values are shown", differentBy, true);
	expose("count", "candidates left", count);
	expose("totalText", "candidates left, as Chimera writes them (\"1,024 addresses\")", totalText);
	expose("canUndo", "a search can be taken back", canUndo);
	expose("canRedo", "a search taken back can be done again", canRedo);
	expose("searching", "a search is open", searching);
	expose("status", "what the search last had to say", status);
	expose("rebootNeeded", "a setting changed that a new search is needed for", rebootNeeded);
	expose("outOfRange", "candidates past the domain's end", outOfRange);
	expose("selectedCount", "rows picked", selectedCount);
	expose("canPoke", "rows are picked and the domain can be written", canPoke);
	expose("selectionFrozen", "every row picked is frozen (Freeze Address reads Unfreeze Address)", selectionFrozen);
	expose("anyFrozen", "any address is frozen (Unfreeze All is offered)", anyFrozen);
	expose("currentFile", "the .wch file last opened or saved", currentFile);
	expose("windowOpen", "the window is open: a search starts when it opens with none open, and with each machine opened while it "
		"is, as Chimera's", windowOpen, true);
	expose("previewMode", "Preview Mode: the rows the next search would remove are coloured", previewMode, true);
	expose("autoSearch", "Auto-Search: the search is made after every frame", autoSearch, true);
	expose("autoSearchAccountForLag", "Auto-Search Account for Lag: not after a lag frame", autoSearchAccountForLag, true);
	expose("alwaysExcludeRamWatch", "Always Exclude RAM Watch List: the addresses RAM Watch has are taken out", alwaysExcludeRamWatch, true);
	expose("useUndoHistory", "Use Undo History: searches can be taken back", useUndoHistory, true);
	expose("recentFiles", ".wch files opened or saved, newest first, a line each", recentFiles, true);
	expose("hiddenColumns", "the columns not shown, by Chimera's names (Address, Value, Prev, Changes, Diff), comma-separated", hiddenColumns, true);
	expose("columnWidths", "the columns a person resized and their widths, \"Value:100\"", columnWidths, true);
	rows("results", "the candidates: address, value, previous value, changes, difference; picked, whether the next search "
		"would remove it (the preview), and whether it is past the domain's end", results_);

	auto newSearch = [this](const Args &, std::string &error) -> Value {
		const int32_t sz = static_cast<int32_t>(size.get());
		if (sz != 1 && sz != 2 && sz != 4 && sz != 8)
		{
			error = tr("ram_search.size_values");
			return {};
		}
		const std::string d = domain.get();
		const bool be = bigEndian.get(), mis = checkMisaligned.get(), detailed = !fast.get(), undo = useUndoHistory.get();
		const int32_t prev = static_cast<int32_t>(previousType.get());
		std::vector<uint64_t> exclude;
		if (alwaysExcludeRamWatch.get() && ramWatch_ != nullptr)
			for (const RamWatchViewModel::Watch &w : ramWatch_->watches())
				if (!w.separator && w.property.empty() && w.domain == d) exclude.push_back(static_cast<uint64_t>(w.address));
		session_.onMachine([this, d, sz, be, mis, detailed, prev, undo, exclude](ce_session *s) {
			startOnMachine(s, d, sz, be, mis, detailed, prev, undo, exclude);
		});
		return {};
	};
	command("newSearch", "Chimera's New Search: every address of the domain a candidate", {}, newSearch, &session_.isOpen);
	command("start", "the same as newSearch", {}, newSearch, &session_.isOpen);
	command("search", "keeps the candidates that compare as set", {}, [this](const Args &, std::string &error) -> Value {
		Params p;
		if (!params(p, error)) return {};
		session_.onMachine([this, p](ce_session *) { searchOnMachine(p); });
		previewCleared_ = true;
		return {};
	}, &searching);
	command("undo", "takes the last search or removal back", {}, [this](const Args &, std::string &) -> Value {
		onSearch([](ce_ramsearch *rs) {
			const int64_t n = ce_ramsearch_undo(rs);
			return tr("ram_search.restored", {addresses(n < 0 ? -n : n)});
		});
		previewCleared_ = true;
		return {};
	}, &canUndo);
	command("redo", "does a search taken back again", {}, [this](const Args &, std::string &) -> Value {
		onSearch([](ce_ramsearch *rs) {
			const int64_t n = ce_ramsearch_redo(rs);
			return tr("ram_search.removed", {addresses(n < 0 ? -n : n)});
		});
		previewCleared_ = true;
		return {};
	}, &canRedo);
	command("copyValueToPrev", "every candidate's previous value made its value now", {}, [this](const Args &, std::string &) -> Value {
		onSearch([](ce_ramsearch *rs) {
			ce_ramsearch_set_previous_to_current(rs);
			return std::string();
		}, true);
		return {};
	}, &searching);
	auto clearCounts = [this](const Args &, std::string &) -> Value {
		onSearch([](ce_ramsearch *rs) {
			ce_ramsearch_clear_change_counts(rs);
			return std::string();
		}, true);
		return {};
	};
	command("clearChangeCounts", "every candidate's change count back to 0", {}, clearCounts, &searching);
	command("clearUndo", "Clear Undo History: nothing can be taken back", {}, [this](const Args &, std::string &) -> Value {
		onSearch([](ce_ramsearch *rs) {
			ce_ramsearch_clear_history(rs);
			return std::string();
		}, true);
		return {};
	}, &searching);
	command("removeSelected", "the rows picked taken out of the search (it can be taken back)", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<int64_t> rows = pickedRows();
		if (rows.empty()) return {};
		onSearch([rows](ce_ramsearch *rs) {
			ce_ramsearch_remove_indices(rs, rows.data(), static_cast<int64_t>(rows.size()));
			return tr("ram_search.removed", {addresses(static_cast<int64_t>(rows.size()))});
		});
		return {};
	}, &searching);
	command("removeOutOfRange", "the candidates past the domain's end taken out", {}, [this](const Args &, std::string &) -> Value {
		onSearch([](ce_ramsearch *rs) {
			const int64_t before = ce_ramsearch_count(rs);
			ce_ramsearch_remove_out_of_range(rs);
			return tr("ram_search.removed", {addresses(before - ce_ramsearch_count(rs))});
		});
		return {};
	}, &searching);
	command("goTo", "Go to Address: the row of an address (hex) picked; its row, or -1 when it is not a candidate", {"address"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			char *end = nullptr;
			const unsigned long long at = std::strtoull(text.c_str(), &end, 16);
			if (text.empty() || end == nullptr || *end != '\0') return int64_t{-1};
			int64_t row = -1;
			{
				std::lock_guard<std::mutex> lock(rsM_);
				if (rs_ != nullptr) row = ce_ramsearch_index_of(rs_, at);
			}
			if (row < 0 || row >= rows_) return int64_t{-1};
			picked_.clear();
			pick(row, row);
			anchor_ = row;
			pickedChanged();
			return row;
		});
	command("select", "a row picked: mode \"only\" (a click), \"toggle\" (Ctrl), \"range\" (Shift, from the last clicked) or "
		"\"none\" (every row let go)", {"row", "mode"}, [this](const Args &a, std::string &error) -> Value {
			int64_t r = 0;
			std::string mode = "only";
			if (!arg(a, 0, r, error)) return {};
			if (a.size() > 1 && !arg(a, 1, mode, error)) return {};
			if (mode == "none")
			{
				picked_.clear();
				anchor_ = -1;
			}
			else if (r < 0 || r >= rows_)
			{
				error = tr("cache.no_row", {std::to_string(r)});
				return {};
			}
			else if (mode == "toggle")
			{
				if (isPicked(r)) unpick(r);
				else pick(r, r);
				anchor_ = r;
			}
			else if (mode == "range" && anchor_ >= 0 && anchor_ < rows_)
			{
				picked_.clear();
				pick(std::min(anchor_, r), std::max(anchor_, r));
			}
			else
			{
				picked_.clear();
				pick(r, r);
				anchor_ = r;
			}
			pickedChanged();
			return {};
		});
	command("selectAll", "every row picked, or none when every one already is (Chimera's Select All)", {}, [this](const Args &, std::string &) -> Value {
		const bool all = rows_ > 0 && selectedCount.get() == rows_;
		picked_.clear();
		if (!all && rows_ > 0) pick(0, rows_ - 1);
		pickedChanged();
		return {};
	});
	command("sort", "the candidates sorted by a column (Address, Value, Prev, Changes or Diff), the other way round when sorted "
		"by it already", {"column"}, [this](const Args &a, std::string &error) -> Value {
			std::string column;
			if (!arg(a, 0, column, error)) return {};
			static const char *const names[] = {"Address", "Value", "Prev", "Changes", "Diff"};
			int32_t c = -1;
			for (int32_t i = 0; i < 5; i++)
				if (column == names[i]) c = i;
			if (c < 0)
			{
				error = tr("piano_roll.no_column", {column});
				return {};
			}
			if (column != sortedBy_) sortReverse_ = false;
			const bool reverse = sortReverse_;
			const watch::Display w = way();
			const int32_t dsp = w == watch::Display::Signed ? 1 : w == watch::Display::Float ? 2 : 0;
			onSearch([c, reverse, dsp](ce_ramsearch *rs) {
				ce_ramsearch_sort(rs, c, reverse ? 1 : 0, dsp);
				return std::string();
			});
			sortedBy_ = column;
			sortReverse_ = !sortReverse_;
			return {};
		}, &searching);
	command("clearPreview", "the preview's colours taken off until the next frame", {}, [this](const Args &, std::string &) -> Value {
		previewCleared_ = true;
		if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
		return {};
	});
	command("selectedLines", "the rows picked as .wch lines (Copy, Ctrl+C)", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		const int s = static_cast<int>(size.get());
		const std::string d = searchedDomain.get();
		for (const int64_t row : pickedRows())
		{
			uint64_t at = 0;
			uint64_t cur = 0, prev = 0;
			uint32_t changes = 0;
			{
				std::lock_guard<std::mutex> lock(rsM_);
				if (rs_ == nullptr || ce_ramsearch_row64(rs_, row, &at, &cur, &prev, &changes) == 0) continue;
			}
			out += addressText(at) + "\t" + watch::sizeLetter(s) + "\t" + watch::displayLetter(way()) + "\t" + (bigEndian.get() ? "1" : "0") +
				   "\t" + d + "\t" + kNewLine;
		}
		return out;
	});
	command("addToRamWatch", "the rows picked added to RAM Watch, at the search's size, display and byte order; with Always "
		"Exclude RAM Watch List, then taken out of the search", {}, [this](const Args &, std::string &error) -> Value {
			if (ramWatch_ == nullptr)
			{
				error = tr("ram_search.no_ram_watch");
				return {};
			}
			std::string lines;
			if (const auto got = run("selectedLines", {}, error)) fromValue(*got, lines);
			if (lines.empty()) return int64_t{0};
			const auto added = ramWatch_->run("paste", {lines}, error);
			if (!added) return {};
			if (alwaysExcludeRamWatch.get())
			{
				const std::vector<uint64_t> gone = watchedAddresses();
				onSearch([gone](ce_ramsearch *rs) {
					ce_ramsearch_remove_addresses(rs, gone.data(), static_cast<int64_t>(gone.size()), 1);
					return std::string();
				});
			}
			return *added;
		});
	command("pokeInfo", "what RAM Poke shows for the rows picked, as JSON: their addresses (ten at most), domain, size, display, "
		"byte order and the first one's value", {}, [this](const Args &, std::string &) -> Value {
			json::Json out;
			out.kind = json::Json::Kind::Object;
			const std::vector<int64_t> rows = pickedRows(10);
			if (rows.empty()) return json::write(out);
			std::string list, value;
			for (size_t i = 0; i < rows.size(); i++)
			{
				uint64_t at = 0;
				uint64_t cur = 0, prev = 0;
				uint32_t changes = 0;
				{
					std::lock_guard<std::mutex> lock(rsM_);
					if (rs_ == nullptr || ce_ramsearch_row64(rs_, rows[i], &at, &cur, &prev, &changes) == 0) continue;
				}
				list += (list.empty() ? "" : ",") + addressText(at);
				if (i == 0)
				{
					value = watch::format(cur, static_cast<int>(size.get()), way());
					value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
				}
			}
			const int s = static_cast<int>(size.get());
			out.members.emplace_back("addresses", json::Json::string(list));
			out.members.emplace_back("domain", json::Json::string(searchedDomain.get()));
			out.members.emplace_back("size", json::Json::string(tr(s == 1 ? "watch.size_byte" : s == 2 ? "watch.size_word" : s == 4 ? "watch.size_dword" : "watch.size_qword")));
			out.members.emplace_back("display", json::Json::string(watch::displayName(way())));
			out.members.emplace_back("hex", json::Json::boolean_(way() == watch::Display::Hex));
			out.members.emplace_back("endian", json::Json::string(tr(bigEndian.get() ? "watch.big_endian" : "watch.little_endian")));
			out.members.emplace_back("value", json::Json::string(value));
			out.members.emplace_back("mixed", json::Json::boolean_(false));
			out.members.emplace_back("property", json::Json::boolean_(false));
			return json::write(out);
		});
	command("toggleFreeze", "the rows picked frozen at the values they have now, or, when all of them are, no longer "
		"(Chimera's Freeze Address); false with what the machine refused", {}, [this](const Args &, std::string &error) -> Value {
			const bool thaw = selectionFrozen.get();
			const int s = static_cast<int>(size.get());
			std::string refused;
			for (const int64_t row : pickedRows())
			{
				uint64_t at = 0;
				uint64_t cur = 0, prev = 0;
				uint32_t changes = 0;
				{
					std::lock_guard<std::mutex> lock(rsM_);
					if (rs_ == nullptr || ce_ramsearch_row64(rs_, row, &at, &cur, &prev, &changes) == 0) continue;
				}
				if (thaw)
				{
					session_.unfreeze(searchedDomain.get(), static_cast<int64_t>(at));
					continue;
				}
				SessionViewModel::Freeze f;
				f.domain = searchedDomain.get();
				f.address = static_cast<int64_t>(at);
				f.size = s;
				f.bigEndian = bigEndian.get();
				f.value = cur;
				std::string why;
				if (!session_.freeze(f, why))
				{
					refused = why;
					break;
				}
			}
			if (!refused.empty())
			{
				error = refused;
				return false;
			}
			return true;
		});
	command("unfreezeAll", "no address frozen any longer (Chimera's Unfreeze All)", {}, [this](const Args &, std::string &) -> Value {
		session_.unfreezeAll();
		return {};
	});
	command("pokeSelected", "a value written at every row picked, read as values are shown. False with what went wrong", {"value"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			uint64_t v = 0;
			if (!watch::parse(text, way(), v, static_cast<int>(size.get())))
			{
				error = tr("ram_search.write_failed");
				return false;
			}
			const int s = static_cast<int>(size.get());
			std::string bytes(static_cast<size_t>(s), '\0');
			for (int i = 0; i < s; i++)
				bytes[static_cast<size_t>(i)] = static_cast<char>((v >> (8 * (bigEndian.get() ? s - 1 - i : i))) & 0xFF);
			for (const int64_t row : pickedRows())
			{
				uint64_t at = 0;
				uint64_t cur = 0, prev = 0;
				uint32_t changes = 0;
				{
					std::lock_guard<std::mutex> lock(rsM_);
					if (rs_ == nullptr || ce_ramsearch_row64(rs_, row, &at, &cur, &prev, &changes) == 0) continue;
				}
				std::string why;
				if (!session_.run("writeMemory", {searchedDomain.get(), static_cast<int64_t>(at), bytes}, why))
				{
					error = tr("ram_search.write_failed");
					return false;
				}
				/* a frozen address is frozen at what was poked, as Chimera's RAM Poke does */
				if (session_.frozen(searchedDomain.get(), static_cast<int64_t>(at)))
				{
					SessionViewModel::Freeze f;
					f.domain = searchedDomain.get();
					f.address = static_cast<int64_t>(at);
					f.size = s;
					f.bigEndian = bigEndian.get();
					f.value = v;
					session_.freeze(f, why);
				}
			}
			if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
			return true;
		});
	command("open", "the addresses of a .wch file: \"open\" makes them the candidates, \"append\" adds them, \"truncate\" "
		"takes them out", {"path", "how"}, [this](const Args &a, std::string &error) -> Value {
			std::string file, how = "open";
			if (!arg(a, 0, file, error)) return {};
			if (a.size() > 1 && !arg(a, 1, how, error)) return {};
			std::ifstream in(std::filesystem::u8path(file), std::ios::binary);
			if (!in)
			{
				error = tr("new_project.cannot_read", {file});
				return {};
			}
			std::vector<uint64_t> found;
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (std::count(line.begin(), line.end(), '\t') < 4) continue;
				const std::string at = line.substr(0, line.find('\t'));
				const std::string kind = line.substr(at.size() + 1, 1);
				/* separators and game properties have no address to search */
				if (kind == "S" || kind == "p" || at.empty()) continue;
				char *end = nullptr;
				const unsigned long long v = std::strtoull(at.c_str(), &end, 16);
				if (end == nullptr || *end != '\0') continue;
				found.push_back(v);
			}
			const std::string name = utf8(std::filesystem::u8path(file).filename());
			const bool truncate = how == "truncate", append = how == "append";
			onSearch([found, truncate, append, name](ce_ramsearch *rs) {
				if (truncate)
				{
					ce_ramsearch_remove_addresses(rs, found.data(), static_cast<int64_t>(found.size()), 1);
					return tr("ram_search.removed", {addresses(static_cast<int64_t>(found.size()))});
				}
				ce_ramsearch_add_addresses(rs, found.data(), static_cast<int64_t>(found.size()), append ? 1 : 0);
				if (!append) ce_ramsearch_clear_history(rs);
				return tr("ram_search.loaded", {name});
			});
			if (!truncate) currentFile.set(file);
			addRecent(file);
			return static_cast<int64_t>(found.size());
		}, &searching);
	command("save", "every candidate written to a .wch file, at the search's size, display and byte order (the last file when "
		"none is given)", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string file = currentFile.get();
			if (!a.empty() && !arg(a, 0, file, error)) return {};
			if (file.empty())
			{
				error = tr("ram_search.no_file");
				return {};
			}
			std::string text = "SystemID " + session_.systemId.get() + kNewLine;
			const int s = static_cast<int>(size.get());
			{
				std::lock_guard<std::mutex> lock(rsM_);
				const int64_t n = rs_ != nullptr ? ce_ramsearch_count(rs_) : 0;
				for (int64_t i = 0; i < n; i++)
				{
					uint64_t at = 0;
					uint64_t cur = 0, prev = 0;
					uint32_t changes = 0;
					if (ce_ramsearch_row64(rs_, i, &at, &cur, &prev, &changes) == 0) continue;
					text += addressText(at) + "\t" + watch::sizeLetter(s) + "\t" + watch::displayLetter(way()) + "\t" +
							(bigEndian.get() ? "1" : "0") + "\t" + searchedDomain.get() + "\t" + kNewLine;
				}
			}
			text += kNewLine;
			std::ofstream out(std::filesystem::u8path(file), std::ios::binary | std::ios::trunc);
			out << text;
			const std::string name = utf8(std::filesystem::u8path(file).filename());
			if (!out)
			{
				status.set(tr("ram_search.save_failed", {name}));
				error = tr("session.could_not_write", {file});
				return {};
			}
			currentFile.set(file);
			addRecent(file);
			status.set(tr("ram_search.saved", {name}));
			return {};
		}, &searching);
	command("restoreDefaults", "RAM Search's settings back to Chimera's: the preview on, Auto-Search and lag frames as at first, "
		"RAM Watch's addresses kept, undo on, the Diff column hidden", {}, [this](const Args &, std::string &) -> Value {
			previewMode.set(true);
			autoSearchAccountForLag.set(true);
			alwaysExcludeRamWatch.set(false);
			useUndoHistory.set(true);
			hiddenColumns.set(fast.get() ? "Changes,Diff" : "Diff");
			return {};
		});

	/* the size: the ways a value can be shown, the same way kept when it still fits (as Chimera keeps it), the open search
	 * read again at the new size, its history forgotten */
	auto showWays = [this](watch::Display keep) {
		std::string lines;
		const std::vector<watch::Display> &ways = watch::displaysFor(static_cast<int>(size.get()));
		for (const watch::Display d : ways) lines += (lines.empty() ? "" : "\n") + std::string(watch::displayName(d));
		displays.set(lines);
		const auto at = std::find(ways.begin(), ways.end(), keep);
		display.set(at == ways.end() ? 0 : static_cast<int64_t>(at - ways.begin()));
	};
	showWays(watch::Display::Unsigned);
	sizeWatch_ = size.changed.connect([this, showWays] {
		showWays(lastWay_);
		lastWay_ = way();
		if (!specificAddress.get().empty()) specificAddress.set("0");
		if (!specificValue.get().empty()) specificValue.set("0");
		const int32_t sz = static_cast<int32_t>(size.get());
		if (searching.get())
			onSearch([sz](ce_ramsearch *rs) {
				ce_ramsearch_convert_to(rs, sz);
				ce_ramsearch_clear_history(rs);
				return std::string();
			});
		paramsChanged();
	});
	displayWatch_ = display.changed.connect([this] {
		/* another way: the boxes typed in the old way go back to 0, as Chimera's */
		if (way() != lastWay_)
		{
			if (!specificValue.get().empty()) specificValue.set("0");
			if (!difference.get().empty()) difference.set("0");
			if (!differentBy.get().empty()) differentBy.set("0");
		}
		lastWay_ = way();
		paramsChanged();
		if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
	});
	bigEndianWatch_ = bigEndian.changed.connect([this] {
		const bool be = bigEndian.get();
		if (searching.get())
			onSearch([be](ce_ramsearch *rs) {
				ce_ramsearch_set_big_endian(rs, be ? 1 : 0);
				return std::string();
			}, true);
	});
	previousWatch_ = previousType.changed.connect([this] {
		const int32_t t = static_cast<int32_t>(previousType.get());
		paramsChanged();
		if (searching.get())
			onSearch([t](ce_ramsearch *rs) {
				return ce_ramsearch_set_previous_type(rs, t) != 0 ? tr("ram_search.no_memory_previous")
																   : std::string();
			}, true);
	});
	/* what needs a new search to take effect */
	domainWatch_ = domain.changed.connect([this] {
		if (searching.get()) rebootNeeded.set(true);
		fastIfLarge();
	});
	fastWatch_ = fast.changed.connect([this] {
		if (searching.get()) rebootNeeded.set(true);
		std::string hidden = hiddenColumns.get();
		auto has = [&](const std::string &c) { return ("," + hidden + ",").find("," + c + ",") != std::string::npos; };
		if (fast.get())
		{
			/* Fast keeps no changes: the last frame's or last change's previous value, the changes compared, are not there */
			if (previousType.get() == 2 || previousType.get() == 3) previousType.set(1);
			if (compare.get() == 3 || compare.get() == 4) compare.set(0);
			if (!has("Changes")) hidden += std::string(hidden.empty() ? "" : ",") + "Changes";
		}
		else if (has("Changes"))
		{
			std::string kept;
			std::istringstream in(hidden);
			for (std::string c; std::getline(in, c, ',');)
				if (c != "Changes" && !c.empty()) kept += (kept.empty() ? "" : ",") + c;
			hidden = kept;
		}
		hiddenColumns.set(hidden);
	});
	misalignedWatch_ = checkMisaligned.changed.connect([this] {
		if (searching.get()) rebootNeeded.set(true);
	});
	undoWatch_ = useUndoHistory.changed.connect([this] {
		const bool on = useUndoHistory.get();
		if (searching.get())
			onSearch([on](ce_ramsearch *rs) {
				ce_ramsearch_set_undo_enabled(rs, on ? 1 : 0);
				return std::string();
			}, true);
	});
	autoWatch_ = autoSearch.changed.connect([this] { autoM_ = autoSearch.get(); });
	lagWatch_ = autoSearchAccountForLag.changed.connect([this] { lagAwareM_ = autoSearchAccountForLag.get(); });
	excludeWatch_ = alwaysExcludeRamWatch.changed.connect([this] {
		if (!alwaysExcludeRamWatch.get() || !searching.get()) return;
		const std::vector<uint64_t> gone = watchedAddresses();
		onSearch([gone](ce_ramsearch *rs) {
			ce_ramsearch_remove_addresses(rs, gone.data(), static_cast<int64_t>(gone.size()), 1);
			return std::string();
		});
	});
	for (Property<std::string> *p : {&specificValue, &specificAddress, &specificChanges, &difference, &differentBy})
		paramWatches_.push_back(p->changed.connect([this] { paramsChanged(); }));
	for (Property<int64_t> *p : {&compare, &op})
		paramWatches_.push_back(p->changed.connect([this] { paramsChanged(); }));
	paramWatches_.push_back(previewMode.changed.connect([this] {
		if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
	}));
	lastWay_ = way();
	paramsChanged();

	hooks_ = session_.addMachineHooks([this](ce_session *s) { afterFrameOnMachine(s); }, [this](ce_session *) { dropOnMachine(); });
	/* each machine opened: its domains */
	freezesWatch_ = session_.freezesChanged.connect([this] { freezesShown(); });
	opensWatch_ = session_.opens.changed.connect([this] {
		session_.onMachine([this](ce_session *s) {
			std::string names, first;
			if (s != nullptr)
				for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				{
					if (ce_session_domain_hidden(s, i) != 0) continue;
					const std::string n = ce_session_domain_name(s, i);
					if (first.empty()) first = n;
					names += (names.empty() ? "" : ",") + n;
				}
			later([this, names, first] {
				domains.set(names);
				const std::string d = "," + domain.get() + ",";
				if (domain.get().empty() || ("," + names + ",").find(d) == std::string::npos) domain.set(first);
				/* a domain of the same name may be another size on this machine, as Chimera checks at each restart;
				 * the open window searches the new machine, as Chimera's restarts its search */
				fastIfLarge([this] {
					if (windowOpen.get()) startWhileOpen();
				});
			});
		});
	});
	windowOpenWatch_ = windowOpen.changed.connect([this] {
		if (windowOpen.get() && !searching.get()) startWhileOpen();
	});
}

void RamSearchViewModel::startWhileOpen()
{
	if (!session_.isOpen.get() || domains.get().empty()) return;
	std::string ignored;
	run("newSearch", {}, ignored);
}

/* a large domain is searched fast (at start, at each new machine, and when
 * the domain changes) */
void RamSearchViewModel::fastIfLarge(std::function<void()> then)
{
	session_.onMachine([this, d = domain.get(), then = std::move(then)](ce_session *s) {
		int64_t bytes = 0;
		if (s != nullptr)
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				if (d == ce_session_domain_name(s, i)) bytes = ce_session_domain_size(s, i);
		later([this, bytes, then] {
			if (bytes >= kMaxDetailedSize && !fast.get())
			{
				fast.set(true);
				status.set(tr("ram_search.fast_mode"));
			}
			if (then) then();
		});
	});
}

RamSearchViewModel::~RamSearchViewModel()
{
	session_.removeMachineHooks(hooks_);
	/* the search goes on the machine's thread, which may be using it */
	session_.onMachineWait([this](ce_session *) { dropOnMachine(); });
}

bool RamSearchViewModel::params(Params &p, std::string &error) const
{
	p.compare = static_cast<int32_t>(compare.get());
	p.op = static_cast<int32_t>(op.get());
	p.previousType = static_cast<int32_t>(previousType.get());
	/* the engine compares raw, signed or floating point (a float, a double); every other way is raw */
	const watch::Display w = way();
	p.display = w == watch::Display::Signed ? 1 : w == watch::Display::Float || w == watch::Display::Double ? 2 : 0;
	const int sz = static_cast<int>(size.get());
	/* what is read, named by the end of its key: value, address, changes, difference, different_by. A value
	 * is read at the search's size; an address, a difference and a different-by whole as 64-bit numbers, so
	 * a signed difference below zero reaches the engine as one (issue 474) */
	auto read = [&](const std::string &text, watch::Display as, const std::string &what, uint64_t &out) {
		if (text.empty())
		{
			error = tr(("ram_search.type_" + what).c_str());
			return false;
		}
		const bool whole = as != watch::Display::Float && as != watch::Display::Double;
		const int bytes = what == "value" || what == "changes" || !whole ? sz : 8;
		if (!watch::parse(text, as, out, bytes))
		{
			error = what == "address" ? tr("ram_search.not_hex_address", {text})
				: what == "changes" ? tr("ram_search.not_changes", {text}) : tr("ram_search.not_shown_as", {text, watch::displayName(w)});
			return false;
		}
		return true;
	};
	p.value = 0;
	switch (p.compare)
	{
	case 1: if (!read(specificValue.get(), w, "value", p.value)) return false; break;
	case 2: if (!read(specificAddress.get(), watch::Display::Hex, "address", p.value)) return false; break;
	case 3: if (!read(specificChanges.get(), watch::Display::Unsigned, "changes", p.value)) return false; break;
	case 4: if (!read(difference.get(), w, "difference", p.value)) return false; break;
	default: break;
	}
	p.differentBy = 0;
	if (p.op == 6 && !read(differentBy.get(), w, "different_by", p.differentBy)) return false;
	return true;
}

void RamSearchViewModel::paramsChanged()
{
	Params p;
	std::string error;
	p.ok = params(p, error);
	preview_ = p;
	{
		std::lock_guard<std::mutex> lock(paramsMutex_);
		paramsM_ = p;
	}
	if (rows_ > 0 && previewMode.get()) results_.changed.emit(0, rows_ - 1);
}

bool RamSearchViewModel::isPicked(int64_t row) const
{
	for (const auto &r : picked_)
		if (row >= r.first && row <= r.second) return true;
	return false;
}

void RamSearchViewModel::pick(int64_t first, int64_t last)
{
	picked_.emplace_back(first, last);
	std::sort(picked_.begin(), picked_.end());
	/* overlapping and touching ranges made one */
	std::vector<std::pair<int64_t, int64_t>> merged;
	for (const auto &r : picked_)
	{
		if (!merged.empty() && r.first <= merged.back().second + 1) merged.back().second = std::max(merged.back().second, r.second);
		else merged.push_back(r);
	}
	picked_ = merged;
}

void RamSearchViewModel::unpick(int64_t row)
{
	std::vector<std::pair<int64_t, int64_t>> kept;
	for (const auto &r : picked_)
	{
		if (row < r.first || row > r.second)
		{
			kept.push_back(r);
			continue;
		}
		if (r.first < row) kept.emplace_back(r.first, row - 1);
		if (row < r.second) kept.emplace_back(row + 1, r.second);
	}
	picked_ = kept;
}

std::vector<int64_t> RamSearchViewModel::pickedRows(size_t most) const
{
	std::vector<int64_t> out;
	for (const auto &r : picked_)
		for (int64_t i = r.first; i <= r.second && out.size() < most; i++) out.push_back(i);
	return out;
}

void RamSearchViewModel::pickedChanged()
{
	int64_t n = 0;
	for (const auto &r : picked_) n += r.second - r.first + 1;
	selectedCount.set(n);
	canPoke.set(n > 0 && domainWritable_);
	freezesShown();
}

void RamSearchViewModel::freezesShown()
{
	anyFrozen.set(session_.frozenCount.get() > 0);
	bool all = selectedCount.get() > 0 && selectedCount.get() <= 1000;
	if (all)
		for (const int64_t row : pickedRows())
		{
			uint64_t at = 0;
			uint64_t cur = 0, prev = 0;
			uint32_t changes = 0;
			{
				std::lock_guard<std::mutex> lock(rsM_);
				if (rs_ == nullptr || ce_ramsearch_row64(rs_, row, &at, &cur, &prev, &changes) == 0) continue;
			}
			if (!session_.frozen(searchedDomain.get(), static_cast<int64_t>(at)))
			{
				all = false;
				break;
			}
		}
	selectionFrozen.set(all);
	if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
}

std::vector<uint64_t> RamSearchViewModel::watchedAddresses() const
{
	std::vector<uint64_t> out;
	if (ramWatch_ == nullptr) return out;
	for (const RamWatchViewModel::Watch &w : ramWatch_->watches())
		if (!w.separator && w.property.empty() && w.domain == searchedDomain.get()) out.push_back(static_cast<uint64_t>(w.address));
	return out;
}

void RamSearchViewModel::dropOnMachine()
{
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ == nullptr) return;
		ce_ramsearch_destroy(rs_);
		rs_ = nullptr;
		pagedM_ = false;
	}
	later([this] {
		rows_ = 0;
		picked_.clear();
		count.set(0);
		totalText.set("0 addresses");
		canUndo.set(false);
		canRedo.set(false);
		searching.set(false);
		outOfRange.set(0);
		pickedChanged();
		status.set(tr("ram_search.machine_closed"));
		results_.reset.emit();
	});
}

void RamSearchViewModel::startOnMachine(ce_session *s, const std::string &name, int32_t sz, bool be, bool misaligned, bool detailed,
	int32_t previous, bool undo, const std::vector<uint64_t> &exclude)
{
	if (s == nullptr) return;
	int32_t index = -1;
	for (int32_t i = 0; i < ce_session_domain_count(s); i++)
		if (name == ce_session_domain_name(s, i)) index = i;
	if (index < 0)
	{
		later([this, name] { status.set(tr("ram_search.no_domain", {name})); });
		return;
	}
	const int64_t bytes = ce_session_domain_size(s, index);
	const bool writable = ce_session_domain_writable(s, index) != 0;
	const uint64_t ptr = ce_session_domain_read_ptr(s, index);
	ce_ramsearch *rs = nullptr;
	if (ptr != 0)
		rs = ce_ramsearch_create(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(ptr)), nullptr, nullptr, bytes);
	else
	{
		{
			std::lock_guard<std::mutex> lock(g_reader.pagesM);
			g_reader.s = s;
			g_reader.index = index;
			g_reader.machine = std::this_thread::get_id();
			g_reader.asked = [this] { pagesAsked(); };
			g_reader.pages.clear();
			g_reader.wanted.clear();
		}
		rs = ce_ramsearch_create(nullptr, &readDomainRange, &g_reader, bytes);
	}
	const bool paged = ptr == 0;
	/* a bus that says which of it is live is searched there alone: a heap
	 * of a gigabyte with thirty megabytes in use (chimera#218, #456) */
	if (const int32_t bus = ce_session_domain_bus(s, index); rs != nullptr && bus >= 0)
		if (const int32_t n = ce_session_bus_ranges(s, bus, nullptr, 0); n >= 0)
		{
			std::vector<int64_t> pairs(2 * static_cast<size_t>(n));
			const int32_t got = ce_session_bus_ranges(s, bus, pairs.data(), n);
			ce_ramsearch_set_ranges(rs, pairs.data(), std::min(n, got));
		}
	if (rs == nullptr || ce_ramsearch_start(rs, sz, misaligned ? 1 : 0, be ? 1 : 0, detailed ? 1 : 0) != 0)
	{
		if (rs != nullptr) ce_ramsearch_destroy(rs);
		later([this, name] { status.set(tr("ram_search.no_memory_for", {name})); });
		return;
	}
	ce_ramsearch_set_previous_type(rs, previous);
	ce_ramsearch_set_undo_enabled(rs, undo ? 1 : 0);
	/* Always Exclude RAM Watch List: a new search has nothing of it, and nothing to take back */
	if (!exclude.empty())
	{
		ce_ramsearch_remove_addresses(rs, exclude.data(), static_cast<int64_t>(exclude.size()), 0);
		ce_ramsearch_clear_history(rs);
	}
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ != nullptr) ce_ramsearch_destroy(rs_);
		rs_ = rs;
		pagedM_ = paged;
	}
	later([this, bytes, writable, name] {
		domainSize_ = bytes;
		domainWritable_ = writable;
		searchedDomain.set(name);
		searching.set(true);
		rebootNeeded.set(false);
	});
	reportOnMachine("");
}

void RamSearchViewModel::searchOnMachine(const Params &p)
{
	int64_t removed = 0;
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ == nullptr) return;
		removed = ce_ramsearch_search64(rs_, p.compare, p.op, p.display, p.value, p.differentBy, p.previousType);
	}
	reportOnMachine(removed < 0 ? tr("ram_search.no_memory") : tr("ram_search.removed", {addresses(removed)}));
}

void RamSearchViewModel::onSearch(std::function<std::string(ce_ramsearch *)> job, bool keepPicks)
{
	session_.onMachine([this, job, keepPicks](ce_session *) {
		std::string said;
		{
			std::lock_guard<std::mutex> lock(rsM_);
			if (rs_ == nullptr) return;
			said = job(rs_);
		}
		reportOnMachine(said, keepPicks);
	});
}

void RamSearchViewModel::pagesAsked()
{
	/* once, until the machine's thread has made them; then the rows are read again */
	if (pagesPosted_.exchange(true)) return;
	session_.onMachine([this](ce_session *s) {
		pagesPosted_ = false;
		pagesOnMachine(s);
		if (!refreshPosted_.exchange(true))
			later([this] {
				refreshPosted_ = false;
				if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
			});
	});
}

void RamSearchViewModel::pagesOnMachine(ce_session *s)
{
	std::lock_guard<std::mutex> lock(rsM_);
	if (rs_ != nullptr && pagedM_ && s != nullptr && s == g_reader.s) copyPages(g_reader);
}

void RamSearchViewModel::afterFrameOnMachine(ce_session *s)
{
	Params p;
	{
		std::lock_guard<std::mutex> lock(paramsMutex_);
		p = paramsM_;
	}
	bool searched = false;
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ == nullptr) return;
		ce_ramsearch_update(rs_, p.previousType);
	}
	/* the copies of a bus's pages the window reads, made fresh */
	pagesOnMachine(s);
	/* Auto-Search: the search made after every frame, but not after a lag frame when told so */
	if (autoM_.load() && p.ok && !(lagAwareM_.load() && session_.lastFrameLagOnMachine()))
	{
		searchOnMachine(p);
		searched = true;
	}
	/* the values moved: the rows in view are read again, at most once a tick */
	if (!searched && !refreshPosted_.exchange(true))
		later([this] {
			refreshPosted_ = false;
			previewCleared_ = false;
			if (rows_ > 0) results_.changed.emit(0, rows_ - 1);
		});
}

void RamSearchViewModel::reportOnMachine(const std::string &text, bool keepPicks)
{
	int64_t n = 0, bad = 0;
	bool u = false, r = false;
	{
		std::lock_guard<std::mutex> lock(rsM_);
		if (rs_ != nullptr)
		{
			n = ce_ramsearch_count(rs_);
			u = ce_ramsearch_can_undo(rs_) != 0;
			r = ce_ramsearch_can_redo(rs_) != 0;
			bad = ce_ramsearch_out_of_range_count(rs_);
		}
	}
	later([this, n, u, r, bad, text, keepPicks] {
		const bool sameRows = n == rows_;
		rows_ = n;
		count.set(n);
		totalText.set(tr("ram_search.addresses_counted", n, {watch::thousands(n)}));
		canUndo.set(u);
		canRedo.set(r);
		outOfRange.set(bad);
		if (!text.empty() || !keepPicks) status.set(text);
		if (!keepPicks)
		{
			picked_.clear();
			anchor_ = -1;
		}
		pickedChanged();
		if (keepPicks && sameRows)
		{
			if (n > 0) results_.changed.emit(0, n - 1);
		}
		else
			results_.reset.emit();
	});
}

void RamSearchViewModel::addRecent(const std::string &file)
{
	/* newest first, each once, eight at most, as Chimera keeps searches */
	std::string out = file;
	std::istringstream in(recentFiles.get());
	std::string l;
	int kept = 1;
	while (std::getline(in, l) && kept < 8)
		if (!l.empty() && l != file)
		{
			out += "\n" + l;
			kept++;
		}
	if (out != recentFiles.get()) recentFiles.set(out);
}

watch::Display RamSearchViewModel::way() const
{
	const std::vector<watch::Display> &ways = watch::displaysFor(static_cast<int>(size.get()));
	const int64_t i = display.get();
	return i >= 0 && i < static_cast<int64_t>(ways.size()) ? ways[static_cast<size_t>(i)] : watch::Display::Unsigned;
}

Value RamSearchViewModel::shown(uint64_t raw) const
{
	return watch::format(raw, static_cast<int>(size.get()), way());
}

std::string RamSearchViewModel::addressText(uint64_t address) const
{
	return watch::hex(static_cast<int64_t>(address), watch::addressDigits(domainSize_ > 0 ? domainSize_ - 1 : 0xFFFFFF));
}

std::string RamSearchViewModel::Results::columnName(int32_t column) const
{
	static const char *const names[] = {"Address", "Value", "Previous", "Changes", "Diff", "selected", "preview", "invalid", "frozen"};
	return column >= 0 && column < 9 ? names[column] : "";
}

Value RamSearchViewModel::Results::cell(int64_t row, int32_t column) const
{
	if (column == 5) return vm_.isPicked(row);
	uint64_t address = 0, current = 0, previous = 0;
	uint32_t changes = 0;
	bool removed = false;
	{
		std::lock_guard<std::mutex> lock(vm_.rsM_);
		if (vm_.rs_ == nullptr || ce_ramsearch_row64(vm_.rs_, row, &address, &current, &previous, &changes) == 0) return {};
		if (column == 6 && vm_.previewMode.get() && !vm_.previewCleared_ && vm_.preview_.ok)
		{
			const Params &p = vm_.preview_;
			removed = ce_ramsearch_would_remove64(vm_.rs_, row, p.compare, p.op, p.display, p.value, p.differentBy) != 0;
		}
	}
	switch (column)
	{
	case 0: return vm_.addressText(address);
	case 1: return vm_.shown(current);
	case 2: return vm_.shown(previous);
	case 3: return static_cast<int64_t>(changes);
	case 4:
	{
		const int64_t d = static_cast<int64_t>(current - previous);
		return d > 0 ? "+" + std::to_string(d) : std::to_string(d);
	}
	case 6: return removed;
	/* Chimera's rule: the address past the domain's end */
	case 7: return vm_.domainSize_ > 0 && static_cast<int64_t>(address) >= vm_.domainSize_;
	case 8: return vm_.session_.frozen(vm_.searchedDomain.get(), static_cast<int64_t>(address));
	default: return {};
	}
}

} // namespace chimera::vm
