#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <tuple>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"

namespace chimera::vm {

namespace {

#ifdef _WIN32
constexpr const char *kNewLine = "\r\n";
#else
constexpr const char *kNewLine = "\n";
#endif
constexpr const char *kOnScreen = "OnScreen\t";

std::string trimLineEnds(std::string s)
{
	while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) s.pop_back();
	while (!s.empty() && (s.front() == '\r' || s.front() == '\n')) s.erase(0, 1);
	return s;
}

/* value minus previous, as Chimera writes a difference: +5, -3, 0 */
std::string difference(const RamWatchViewModel::Watch &w)
{
	const int64_t d = static_cast<int64_t>(w.value - w.previous);
	return d > 0 ? "+" + std::to_string(d) : std::to_string(d);
}

uint64_t readValue(ce_session *s, int32_t index, int64_t address, int size, bool bigEndian, bool &ok)
{
	uint8_t b[8] = {};
	ok = ce_session_domain_read(s, index, address, b, size) == size;
	uint64_t v = 0;
	for (int i = 0; i < size; i++)
	{
		const int at = bigEndian ? i : size - 1 - i;
		v = (v << 8) | b[at];
	}
	return v;
}

} // namespace

RamWatchViewModel::RamWatchViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("ramWatch", dispatcher), session_(session)
{
	expose("count", "how many watches the list has", count);
	expose("domains", "the machine's memory domains, comma-separated", domains);
	expose("previousType", "what the previous value is: 0 the first one seen, 2 the last frame's, 3 the one before the last change",
		previousType, true);
	expose("path", "the .wch file the list was opened from or saved to", path);
	expose("changed", "the list changed since it was opened or saved", changed);
	expose("status", "what RAM Watch last had to say", status);
	expose("showOnScreen", "the watches marked for it are shown over the picture", showOnScreen, true);
	expose("onScreenText", "what is shown over the picture: \"note: value\", a line each", onScreenText);
	rows("watches", "one row a watch: address, value, previous value, changes, difference, domain, note, size, how it is shown, on screen", rows_);

	command("add", "watches an address: domain, address, size (1, 2, 4 or 8), display (0 unsigned, 1 signed, 2 hex, 3 binary, "
		"4 fixed point 12.4, 5 fixed point 20.12, 6 fixed point 16.16, 7 float, 8 double), big endian, note; its place in the list",
		{"domain", "address", "size", "display", "bigEndian", "note"},
		[this](const Args &a, std::string &error) -> Value {
			Watch w;
			int64_t size = 1, display = 0;
			if (!arg(a, 0, w.domain, error) || !arg(a, 1, w.address, error) || !arg(a, 2, size, error) || !arg(a, 3, display, error))
				return {};
			if (a.size() > 4 && !arg(a, 4, w.bigEndian, error)) return {};
			if (a.size() > 5 && !arg(a, 5, w.note, error)) return {};
			if (size != 1 && size != 2 && size != 4 && size != 8)
			{
				error = tr("ram_watch.size_values");
				return {};
			}
			w.size = static_cast<int>(size);
			w.display = static_cast<watch::Display>(display);
			if (display < 0 || display > 8 || !watch::fits(w.size, w.display))
			{
				error = tr("ram_watch.cannot_show_size", size);
				return {};
			}
			if (w.address < 0)
			{
				error = tr("ram_watch.address_negative");
				return {};
			}
			sizeFromDomain(w);
			watches_.push_back(w);
			listChanged(true);
			return static_cast<int64_t>(watches_.size() - 1);
		});
	command("addProperty", "watches a game core's property by its name (\"Score\", \"Row[2]\")", {"name"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			bool found = false;
			session_.onMachineWait([&](ce_session *s) {
				uint32_t element = 0;
				found = s != nullptr && ce_session_property_find(s, name.c_str(), &element) >= 0;
			});
			if (!found)
			{
				error = tr("ram_watch.no_property", {name});
				return {};
			}
			Watch w;
			w.property = name;
			w.note = name;
			watches_.push_back(w);
			listChanged(true);
			return static_cast<int64_t>(watches_.size() - 1);
		});
	command("addSeparator", "a line with only a note, to group watches", {"note"},
		[this](const Args &a, std::string &error) -> Value {
			Watch w;
			w.separator = true;
			if (!a.empty() && !arg(a, 0, w.note, error)) return {};
			watches_.push_back(w);
			listChanged(true);
			return static_cast<int64_t>(watches_.size() - 1);
		});
	command("remove", "takes a watch off the list", {"index"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		if (!index(a, 0, i, error)) return {};
		watches_.erase(watches_.begin() + static_cast<long>(i));
		listChanged(true);
		return {};
	});
	command("setDisplay", "how a watch's value is shown (as add)", {"index", "display"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		int64_t display = 0;
		if (!index(a, 0, i, error) || !arg(a, 1, display, error)) return {};
		if (display < 0 || display > 8 || watches_[i].separator || !watches_[i].property.empty() || !watch::fits(watches_[i].size, static_cast<watch::Display>(display)))
		{
			error = tr("ram_watch.cannot_show");
			return {};
		}
		watches_[i].display = static_cast<watch::Display>(display);
		listChanged(true);
		return {};
	});
	command("setOnScreen", "whether a watch is shown over the picture", {"index", "on"}, [this](const Args &a, std::string &error) -> Value {
		size_t i = 0;
		bool on = false;
		if (!index(a, 0, i, error) || !arg(a, 1, on, error)) return {};
		if (watches_[i].separator)
		{
			error = tr("ram_watch.separator_no_value");
			return {};
		}
		watches_[i].onScreen = on;
		listChanged(true);
		return {};
	});
	command("clearChangeCounts", "every watch's changes back to 0", {}, [this](const Args &, std::string &) -> Value {
		for (Watch &w : watches_) w.changes = 0;
		listChanged(false);
		return {};
	});
	command("newList", "an empty list, in no file", {}, [this](const Args &, std::string &) -> Value {
		watches_.clear();
		path.set("");
		changed.set(false);
		listChanged(false);
		return {};
	});
	command("open", "the watches of a .wch file; with append, added to these", {"path", "append"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			bool append = false;
			if (!arg(a, 0, file, error)) return {};
			if (a.size() > 1 && !arg(a, 1, append, error)) return {};
			if (!load(file, append, error)) return {};
			return count.get();
		});
	command("save", "the list to a .wch file (the one it came from when no path is given)", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			if (!a.empty() && !arg(a, 0, file, error)) return {};
			if (file.empty()) file = path.get();
			if (file.empty())
			{
				error = tr("ram_watch.no_file");
				return {};
			}
			if (!save(file, error)) return {};
			return {};
		});


	/* Chimera's RAM Watch works on the rows picked */
	expose("countText", "how many watches, as Chimera counts them (\"3 watches\"; separators not)", countText);
	expose("selectedCount", "rows picked", selectedCount);
	expose("selectedWatches", "watches picked (not separators)", selectedWatches);
	expose("canSplit", "every watch picked can be split (a word or double word shown in hex or binary)", canSplit);
	expose("canPoke", "every watch picked is in memory that can be written", canPoke);
	expose("selectionFrozen", "every watch picked is frozen (Freeze Address reads Unfreeze Address)", selectionFrozen);
	expose("anyFrozen", "any address is frozen (Unfreeze All is offered)", anyFrozen);
	expose("invalidCount", "watches whose address is past their domain's end", invalidCount);
	expose("defaultDomain", "the domain a new watch starts in (Watches > Default Domain)", defaultDomain, true);
	expose("doubleClickToPoke", "On Double-Clicking a Watch: poke it, else edit it", doubleClickToPoke, true);
	expose("recentFiles", ".wch files opened or saved, newest first, a line each", recentFiles, true);
	expose("hiddenColumns", "the columns not shown, by Chimera's names (OSD, Address, Value, Prev, Changes, Diff, Type, Domain, "
		"Notes), comma-separated", hiddenColumns, true);
	command("select", "a row picked: mode \"only\" (a click), \"toggle\" (Ctrl), \"range\" (Shift, from the last clicked) or "
		"\"none\" (every row let go)", {"row", "mode"}, [this](const Args &a, std::string &error) -> Value {
			int64_t r = 0;
			std::string mode = "only";
			if (!arg(a, 0, r, error)) return {};
			if (a.size() > 1 && !arg(a, 1, mode, error)) return {};
			const auto n = static_cast<int64_t>(watches_.size());
			if (mode == "none")
			{
				for (Watch &w : watches_) w.selected = false;
				anchor_ = -1;
			}
			else if (r < 0 || r >= n)
			{
				error = tr("ram_watch.no_watch", {std::to_string(r)});
				return {};
			}
			else if (mode == "toggle")
			{
				watches_[static_cast<size_t>(r)].selected = !watches_[static_cast<size_t>(r)].selected;
				anchor_ = r;
			}
			else if (mode == "range" && anchor_ >= 0 && anchor_ < n)
			{
				for (int64_t i = 0; i < n; i++) watches_[static_cast<size_t>(i)].selected = i >= std::min(anchor_, r) && i <= std::max(anchor_, r);
			}
			else
			{
				for (Watch &w : watches_) w.selected = false;
				watches_[static_cast<size_t>(r)].selected = true;
				anchor_ = r;
			}
			selectionChanged();
			if (n > 0) rows_.changed.emit(0, n - 1);
			return {};
		});
	command("selectAll", "every row picked, or none when every one already is (Chimera's Select All)", {}, [this](const Args &, std::string &) -> Value {
		const bool all = std::all_of(watches_.begin(), watches_.end(), [](const Watch &w) { return w.selected; });
		for (Watch &w : watches_) w.selected = !all;
		selectionChanged();
		if (!watches_.empty()) rows_.changed.emit(0, static_cast<int64_t>(watches_.size()) - 1);
		return {};
	});
	command("removeSelected", "the rows picked taken off the list", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<size_t> rows = selectedRows();
		if (rows.empty()) return {};
		for (auto it = rows.rbegin(); it != rows.rend(); ++it) watches_.erase(watches_.begin() + static_cast<long>(*it));
		anchor_ = -1;
		listChanged(true);
		return {};
	});
	command("moveSelected", "the rows picked moved \"up\" or \"down\" a row, or to the \"top\" or \"bottom\", kept picked", {"where"},
		[this](const Args &a, std::string &error) -> Value {
			std::string where;
			if (!arg(a, 0, where, error)) return {};
			const std::vector<size_t> rows = selectedRows();
			if (rows.empty()) return {};
			if (where == "up")
			{
				if (rows.front() == 0) return {};
				for (const size_t i : rows) std::swap(watches_[i - 1], watches_[i]);
			}
			else if (where == "down")
			{
				if (rows.back() + 1 >= watches_.size()) return {};
				for (auto it = rows.rbegin(); it != rows.rend(); ++it) std::swap(watches_[*it], watches_[*it + 1]);
			}
			else if (where == "top" || where == "bottom")
			{
				std::vector<Watch> picked, rest;
				for (Watch &w : watches_) (w.selected ? picked : rest).push_back(w);
				watches_ = where == "top" ? picked : rest;
				watches_.insert(watches_.end(), (where == "top" ? rest : picked).begin(), (where == "top" ? rest : picked).end());
			}
			else
			{
				error = tr("ram_watch.move_values");
				return {};
			}
			listChanged(true);
			return {};
		});
	command("insertSeparator", "a separator put before the first row picked, or at the end", {}, [this](const Args &, std::string &) -> Value {
		const std::vector<size_t> rows = selectedRows();
		Watch sep;
		sep.separator = true;
		watches_.insert(watches_.begin() + static_cast<long>(rows.empty() ? watches_.size() : rows.front()), sep);
		listChanged(true);
		return {};
	});
	command("editSeparators", "the separators picked given a text", {"text"}, [this](const Args &a, std::string &error) -> Value {
		std::string text;
		if (!arg(a, 0, text, error)) return {};
		for (Watch &w : watches_)
			if (w.selected && w.separator) w.note = text;
		listChanged(true);
		return {};
	});
	command("splitSelected", "each watch picked split in two halves, as Chimera's Split Watch: a double word into two words, a "
		"word into two bytes, the first half the one at the lower address when big end first", {}, [this](const Args &, std::string &) -> Value {
			if (!canSplit.get()) return false;
			const std::vector<size_t> rows = selectedRows();
			for (auto it = rows.rbegin(); it != rows.rend(); ++it)
			{
				const Watch ab = watches_[*it];
				if (ab.separator) continue;
				Watch lo = ab, hi = ab;
				lo.size = hi.size = ab.size / 2;
				hi.address = ab.address + lo.size;
				lo.changes = hi.changes = 0;
				lo.readable = hi.readable = lo.seen = hi.seen = false;
				watches_[*it] = ab.bigEndian ? lo : hi;
				watches_.insert(watches_.begin() + static_cast<long>(*it) + 1, ab.bigEndian ? hi : lo);
			}
			listChanged(true);
			return true;
		});
	command("editInfo", "what the Watch editor starts from, as JSON: the watches picked (mode \"edit\" or \"duplicate\"), or a "
		"new one in the default domain (\"new\"); bigEndian 2 when the watches picked differ", {"mode"},
		[this](const Args &a, std::string &error) -> Value {
			std::string mode = "new";
			if (!a.empty() && !arg(a, 0, mode, error)) return {};
			json::Json out;
			out.kind = json::Json::Kind::Object;
			std::vector<const Watch *> picked;
			for (const Watch &w : watches_)
				if (w.selected && !w.separator) picked.push_back(&w);
			if (mode == "new" || picked.empty())
			{
				out.members.emplace_back("domain", json::Json::string(defaultDomain.get()));
				out.members.emplace_back("address", json::Json::string("00000000"));
				out.members.emplace_back("size", json::Json::integer(1));
				out.members.emplace_back("display", json::Json::integer(0));
				/* every machine's memory is little end first here, as in Chimera */
				out.members.emplace_back("bigEndian", json::Json::integer(0));
				out.members.emplace_back("note", json::Json::string(""));
				out.members.emplace_back("many", json::Json::boolean_(false));
				out.members.emplace_back("sizesDiffer", json::Json::boolean_(false));
				out.members.emplace_back("property", json::Json::boolean_(false));
				return json::write(out);
			}
			const Watch &first = *picked.front();
			std::string addresses;
			bool property = false, sizesDiffer = false, endiansDiffer = false;
			for (const Watch *w : picked)
			{
				const int digits = watch::addressDigits(w->domainSize != 0 ? w->domainSize - 1 : 0xFE);
				addresses += (addresses.empty() ? "" : ",") + watch::hex(w->address, digits);
				property = property || !w->property.empty();
				sizesDiffer = sizesDiffer || w->size != first.size;
				endiansDiffer = endiansDiffer || w->bigEndian != first.bigEndian;
			}
			out.members.emplace_back("domain", json::Json::string(first.domain));
			out.members.emplace_back("address", json::Json::string(addresses));
			out.members.emplace_back("size", json::Json::integer(first.size));
			out.members.emplace_back("display", json::Json::integer(static_cast<long long>(first.display)));
			out.members.emplace_back("bigEndian", json::Json::integer(endiansDiffer ? 2 : first.bigEndian ? 1 : 0));
			out.members.emplace_back("note", json::Json::string(picked.size() == 1 ? first.note : std::string()));
			out.members.emplace_back("many", json::Json::boolean_(picked.size() > 1));
			out.members.emplace_back("sizesDiffer", json::Json::boolean_(sizesDiffer));
			out.members.emplace_back("property", json::Json::boolean_(property));
			return json::write(out);
		});
	/* the editor's answer applied to watches, as Chimera's editor applies it */
	auto edited = [](Watch &w, int64_t size, int64_t display, int64_t bigEndian, int64_t address, bool one, const std::string &note) {
		w.size = static_cast<int>(size);
		if (watch::fits(w.size, static_cast<watch::Display>(display))) w.display = static_cast<watch::Display>(display);
		else if (!watch::fits(w.size, w.display)) w.display = watch::Display::Unsigned;
		if (bigEndian != 2) w.bigEndian = bigEndian == 1;
		if (one)
		{
			w.note = note;
			if (address >= 0) w.address = address;
		}
		w.changes = 0;
		w.readable = w.seen = false;
	};
	auto editorArgs = [](const Args &a, int64_t &size, int64_t &display, int64_t &bigEndian, int64_t &address, std::string &note,
						  std::string &error) {
		std::string at;
		if (!arg(a, 0, size, error) || !arg(a, 1, display, error) || !arg(a, 2, bigEndian, error) || !arg(a, 3, at, error) ||
			!arg(a, 4, note, error))
			return false;
		if (size != 1 && size != 2 && size != 4 && size != 8)
		{
			error = tr("ram_watch.size_values");
			return false;
		}
		address = -1;
		if (!at.empty() && at.find(',') == std::string::npos)
		{
			char *end = nullptr;
			const long long v = std::strtoll(at.c_str(), &end, 16);
			if (end == nullptr || *end != '\0' || v < 0)
			{
				error = tr("ram_search.not_hex_address", {at});
				return false;
			}
			address = v;
		}
		return true;
	};
	command("displays", "the ways a value of a size can be shown, in Chimera's order, as [{\"display\", \"name\"}]", {"size"},
		[](const Args &a, std::string &error) -> Value {
			int64_t size = 1;
			if (!arg(a, 0, size, error)) return {};
			json::Json out;
			out.kind = json::Json::Kind::Array;
			for (const watch::Display d : watch::displaysFor(static_cast<int>(size)))
			{
				json::Json one;
				one.kind = json::Json::Kind::Object;
				one.members.emplace_back("display", json::Json::integer(static_cast<long long>(d)));
				one.members.emplace_back("name", json::Json::string(watch::displayName(d)));
				out.items.push_back(one);
			}
			return json::write(out);
		});
	command("editSelected", "the watches picked changed as Chimera's Edit Watch changes them: size and display for all, big end "
		"first unless 2 (the picks differed and the box was left so), and the address (hex) and note for a single one",
		{"size", "display", "bigEndian", "address", "note"}, [this, edited, editorArgs](const Args &a, std::string &error) -> Value {
			int64_t size = 1, display = 0, bigEndian = 2, address = -1;
			std::string note;
			if (!editorArgs(a, size, display, bigEndian, address, note, error)) return {};
			if (selectedWatches.get() == 0) return {};
			const bool one = selectedWatches.get() == 1;
			for (Watch &w : watches_)
				if (w.selected && !w.separator && w.property.empty()) edited(w, size, display, bigEndian, address, one, note);
			listChanged(true);
			return {};
		});
	command("duplicateSelected", "copies of the watches picked, changed as the editor says (as editSelected), put after the last "
		"row picked", {"size", "display", "bigEndian", "address", "note"}, [this, edited, editorArgs](const Args &a, std::string &error) -> Value {
			int64_t size = 1, display = 0, bigEndian = 2, address = -1;
			std::string note;
			if (!editorArgs(a, size, display, bigEndian, address, note, error)) return {};
			const std::vector<size_t> rows = selectedRows();
			if (rows.empty()) return {};
			std::vector<Watch> copies;
			for (const size_t i : rows)
			{
				if (watches_[i].separator || !watches_[i].property.empty()) continue;
				Watch c = watches_[i];
				c.selected = false;
				c.onScreen = false;
				edited(c, size, display, bigEndian, address, selectedWatches.get() == 1, note);
				copies.push_back(c);
			}
			watches_.insert(watches_.begin() + static_cast<long>(rows.back()) + 1, copies.begin(), copies.end());
			listChanged(true);
			return {};
		});
	command("newWatch", "a watch added as Chimera's New Watch makes one: domain, address in hex, size, display, big end first, note",
		{"domain", "address", "size", "display", "bigEndian", "note"}, [this](const Args &a, std::string &error) -> Value {
			std::string domain, at, note;
			int64_t size = 1, display = 0;
			bool bigEndian = false;
			if (!arg(a, 0, domain, error) || !arg(a, 1, at, error) || !arg(a, 2, size, error) || !arg(a, 3, display, error) ||
				!arg(a, 4, bigEndian, error) || !arg(a, 5, note, error))
				return {};
			char *end = nullptr;
			const long long address = std::strtoll(at.c_str(), &end, 16);
			if (at.empty() || end == nullptr || *end != '\0' || address < 0)
			{
				error = tr("ram_search.not_hex_address", {at});
				return {};
			}
			const auto added = run("add", {domain, int64_t{address}, size, display, bigEndian, note}, error);
			return added.has_value() ? *added : Value{};
		});
	command("pokeInfo", "what RAM Poke shows for the watches picked, as JSON: their addresses (ten at most), domain, size, "
		"display, byte order and value; mixed when they differ in size, display or byte order (Chimera refuses those)", {},
		[this](const Args &, std::string &) -> Value {
			std::vector<const Watch *> picked;
			for (const Watch &w : watches_)
				if (w.selected && !w.separator) picked.push_back(&w);
			json::Json out;
			out.kind = json::Json::Kind::Object;
			if (picked.empty()) return json::write(out);
			const Watch &first = *picked.front();
			std::string addresses;
			std::vector<std::string> seen;
			bool mixed = false, property = false;
			for (size_t i = 0; i < picked.size(); i++)
			{
				const Watch &w = *picked[i];
				property = property || !w.property.empty();
				mixed = mixed || w.size != first.size || w.display != first.display || w.bigEndian != first.bigEndian;
				if (i >= 10) continue;
				const std::string at = watch::hex(w.address, watch::addressDigits(w.domainSize != 0 ? w.domainSize - 1 : 0xFE));
				if (std::find(seen.begin(), seen.end(), at) != seen.end()) continue;
				seen.push_back(at);
				addresses += (addresses.empty() ? "" : ",") + at;
			}
			std::string value = first.property.empty() ? (first.readable ? watch::format(first.value, first.size, first.display) : "")
													   : first.valueText;
			if (first.property.empty()) value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
			out.members.emplace_back("addresses", json::Json::string(addresses));
			out.members.emplace_back("domain", json::Json::string(first.domain));
			out.members.emplace_back("size", json::Json::string(tr(first.size == 1 ? "watch.size_byte" : first.size == 2 ? "watch.size_word" : first.size == 4 ? "watch.size_dword" : "watch.size_qword")));
			out.members.emplace_back("display", json::Json::string(watch::displayName(first.display)));
			out.members.emplace_back("hex", json::Json::boolean_(first.display == watch::Display::Hex));
			out.members.emplace_back("endian", json::Json::string(tr(first.bigEndian ? "watch.big_endian" : "watch.little_endian")));
			out.members.emplace_back("value", json::Json::string(value));
			out.members.emplace_back("mixed", json::Json::boolean_(mixed && !property));
			out.members.emplace_back("property", json::Json::boolean_(property));
			out.members.emplace_back("name", json::Json::string(picked.size() == 1 ? first.note : tr("ram_watch.properties", static_cast<int64_t>(picked.size()))));
			return json::write(out);
		});
	command("pokeSelected", "a value written into every watch picked, read as each one's display reads text (hex without 0x for "
		"hexadecimal); a game property takes the text the engine reads. False with what went wrong", {"value"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			std::string refused;
			bool ok = true;
			for (const Watch &w : watches_)
			{
				if (!w.selected || w.separator) continue;
				if (!w.property.empty())
				{
					std::string why;
					session_.onMachineWait([&](ce_session *s) {
						uint32_t element = 0;
						const int32_t index = s != nullptr ? ce_session_property_find(s, w.property.c_str(), &element) : -1;
						if (index < 0) why = tr("ram_watch.no_property", {w.property});
						else if (ce_session_property_set_text(s, index, element, text.c_str()) != 0) why = ce_session_last_error(s);
					});
					if (!why.empty()) refused += (refused.empty() ? "" : "\n") + w.property + ": " + why;
					/* a frozen property is frozen at what was poked, as Chimera's RAM Poke does */
					else if (session_.propertyFrozen(w.property))
					{
						SessionViewModel::Freeze f;
						f.property = w.property;
						f.text = text;
						session_.freeze(f, why);
					}
					continue;
				}
				std::string why;
				if (!poke(w, text, why)) ok = false;
			}
			if (!refused.empty())
			{
				error = refused;
				return false;
			}
			if (!ok)
			{
				error = tr("ram_search.write_failed");
				return false;
			}
			return true;
		});
	command("toggleFreeze", "the watches picked frozen at the values they have now, or, when all of them are, no longer "
		"(Chimera's Freeze Address); false with what the machine refused", {}, [this](const Args &, std::string &error) -> Value {
			const bool thaw = selectionFrozen.get();
			std::string refused;
			for (const Watch &w : watches_)
			{
				if (!w.selected || w.separator) continue;
				if (thaw)
				{
					if (!w.property.empty()) session_.unfreezeProperty(w.property);
					else session_.unfreeze(w.domain, w.address);
					continue;
				}
				if (!w.readable) continue;
				SessionViewModel::Freeze f;
				if (!w.property.empty())
				{
					f.property = w.property;
					f.text = w.valueText;
				}
				else
				{
					f.domain = w.domain;
					f.address = w.address;
					f.size = w.size;
					f.bigEndian = w.bigEndian;
					f.value = w.value;
				}
				std::string why;
				if (!session_.freeze(f, why)) refused += (refused.empty() ? "" : "\n") + why;
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
	command("copySelected", "the rows picked as .wch lines, for the clipboard (Ctrl+C)", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		for (const Watch &w : watches_)
			if (w.selected) out += lineOf(w) + kNewLine;
		return out;
	});
	command("paste", "watches from .wch lines (the clipboard, Ctrl+V) added at the end", {"text"}, [this](const Args &a, std::string &error) -> Value {
		std::string text;
		if (!arg(a, 0, text, error)) return {};
		std::istringstream in(text);
		const std::vector<Watch> read = readList(in);
		if (read.empty()) return int64_t{0};
		watches_.insert(watches_.end(), read.begin(), read.end());
		listChanged(true);
		return static_cast<int64_t>(read.size());
	});
	command("removeInvalid", "the watches past their domain's end taken off (Chimera's warning, clicked)", {}, [this](const Args &, std::string &) -> Value {
		watches_.erase(std::remove_if(watches_.begin(), watches_.end(), [this](const Watch &w) { return invalid(w); }), watches_.end());
		listChanged(true);
		return {};
	});
	command("purgeDuplicates", "each watch taken off whose domain, address and size an earlier one has (Chimera's Purge Duplicate "
		"Watches; byte order and display not looked at)", {}, [this](const Args &, std::string &) -> Value {
			std::vector<std::tuple<int, std::string, int64_t>> seen;
			std::vector<Watch> kept;
			for (const Watch &w : watches_)
			{
				if (!w.separator && w.property.empty())
				{
					const auto key = std::make_tuple(w.size, w.domain, w.address);
					if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
					seen.push_back(key);
				}
				kept.push_back(w);
			}
			const bool removed = kept.size() != watches_.size();
			watches_ = kept;
			listChanged(removed);
			return removed;
		});
	command("sort", "each stretch between separators sorted by a column, the other way round when sorted by it already, as "
		"Chimera's: OSD, Address, Value, Prev, Changes, Diff, Type, Domain or Notes", {"column"}, [this](const Args &a, std::string &error) -> Value {
			std::string column;
			if (!arg(a, 0, column, error)) return {};
			if (column != sortedBy_) sortReverse_ = false;
			/* a value as its display orders it; a long double holds a 64-bit one exactly */
			auto signedOf = [](const Watch &w, uint64_t v) -> long double {
				if (w.display != watch::Display::Signed) return static_cast<long double>(v);
				const int bits = 8 * w.size;
				if (bits >= 64) return static_cast<long double>(static_cast<int64_t>(v));
				if (bits == 32) return static_cast<long double>(static_cast<int32_t>(v));
				return static_cast<long double>((static_cast<int64_t>(v) ^ (int64_t{1} << (bits - 1))) - (int64_t{1} << (bits - 1)));
			};
			auto byAddress = [](const Watch &x, const Watch &y) { return x.address != y.address ? x.address < y.address : x.size < y.size; };
			std::function<bool(const Watch &, const Watch &)> less;
			if (column == "Address")
				less = [](const Watch &x, const Watch &y) {
					if (x.address != y.address) return x.address < y.address;
					return x.domain != y.domain ? x.domain < y.domain : x.size < y.size;
				};
			else if (column == "Value")
				less = [&](const Watch &x, const Watch &y) { const long double a1 = signedOf(x, x.value), b1 = signedOf(y, y.value); return a1 != b1 ? a1 < b1 : byAddress(x, y); };
			else if (column == "Prev")
				less = [&](const Watch &x, const Watch &y) { return x.previous != y.previous ? x.previous < y.previous : byAddress(x, y); };
			else if (column == "Changes")
				less = [&](const Watch &x, const Watch &y) { return x.changes != y.changes ? x.changes < y.changes : byAddress(x, y); };
			else if (column == "Diff")
				less = [&](const Watch &x, const Watch &y) { const std::string dx = difference(x), dy = difference(y); return dx != dy ? dx < dy : byAddress(x, y); };
			else if (column == "Type")
				less = [](const Watch &x, const Watch &y) {
					if (x.size != y.size) return x.size < y.size;
					return x.display != y.display ? x.display < y.display : x.bigEndian < y.bigEndian;
				};
			else if (column == "Domain")
				less = [&](const Watch &x, const Watch &y) { return x.domain != y.domain ? x.domain < y.domain : byAddress(x, y); };
			else if (column == "Notes")
				less = [&](const Watch &x, const Watch &y) {
					std::string nx = x.note, ny = y.note;
					for (char &c : nx) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					for (char &c : ny) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
					return nx != ny ? nx < ny : byAddress(x, y);
				};
			else if (column == "OSD")
				/* the ones on the screen first */
				less = [&](const Watch &x, const Watch &y) { return x.onScreen != y.onScreen ? x.onScreen : byAddress(x, y); };
			else
			{
				error = tr("piano_roll.no_column", {column});
				return {};
			}
			const bool reverse = sortReverse_;
			size_t from = 0;
			for (size_t i = 0; i <= watches_.size(); i++)
			{
				if (i < watches_.size() && !watches_[i].separator) continue;
				std::stable_sort(watches_.begin() + static_cast<long>(from), watches_.begin() + static_cast<long>(i), less);
				if (reverse) std::reverse(watches_.begin() + static_cast<long>(from), watches_.begin() + static_cast<long>(i));
				from = i + 1;
			}
			sortedBy_ = column;
			sortReverse_ = !sortReverse_;
			listChanged(true);
			return {};
		});

	hooks_ = session_.addMachineHooks([this](ce_session *s) { readOnMachine(s, false); },
		[this](ce_session *) {
			for (Watch &w : watchesM_) w.readable = false;
		});
	showWatch_ = showOnScreen.changed.connect([this] { refreshOnScreen(); });
	opensWatch_ = session_.opens.changed.connect([this] { refreshDomains(); });
	freezesWatch_ = session_.freezesChanged.connect([this] {
		anyFrozen.set(session_.frozenCount.get() > 0);
		selectionChanged();
		if (!watches_.empty()) rows_.changed.emit(0, static_cast<int64_t>(watches_.size()) - 1);
	});
	refreshDomains();
	previousTypeWatch_ = previousType.changed.connect([this] {
		const int t = static_cast<int>(previousType.get());
		session_.onMachine([this, t](ce_session *) { previousTypeM_ = t; });
	});
}

void RamWatchViewModel::refreshDomains()
{
	session_.onMachine([this](ce_session *s) {
		std::string names, system;
		std::map<std::string, int64_t> sizes;
		std::map<std::string, bool> writable;
		if (s != nullptr)
		{
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
			{
				const std::string n = ce_session_domain_name(s, i);
				sizes[n] = ce_session_domain_size(s, i);
				writable[n] = ce_session_domain_writable(s, i) != 0;
				if (ce_session_domain_hidden(s, i) != 0) continue;
				names += (names.empty() ? "" : ",") + n;
			}
			if (const char *id = ce_session_system_id(s)) system = id;
		}
		later([this, names, system, sizes, writable] {
			domainSizes_ = sizes;
			domainWritable_ = writable;
			/* Watches > Default Domain: the first, until one is chosen that this machine has */
			/* by whole name: "RAM" is not among "VRAM,WRAM" */
			if (("," + names + ",").find("," + defaultDomain.get() + ",") == std::string::npos || defaultDomain.get().empty())
				defaultDomain.set(names.substr(0, names.find(',')));
			for (Watch &w : watches_) sizeFromDomain(w);
			domains.set(names);
			systemId_ = system;
			if (!watches_.empty()) rows_.changed.emit(0, static_cast<int64_t>(watches_.size()) - 1);
			selectionChanged();
		});
		/* another machine: each watch starts again from its first value there,
		 * as a refresh of the domains resets the previous values and the
		 * change counts */
		for (Watch &w : watches_)
		{
			w.readable = false;
			w.seen = false;
			w.changes = 0;
		}
		readOnMachine(s, true);
	});
}

RamWatchViewModel::~RamWatchViewModel()
{
	session_.removeMachineHooks(hooks_);
	session_.onMachineWait([](ce_session *) {});
}

void RamWatchViewModel::readProperty(ce_session *s, Watch &w, bool fresh)
{
	uint32_t element = 0;
	const int32_t index = ce_session_property_find(s, w.property.c_str(), &element);
	bool ok = index >= 0;
	std::string now;
	if (ok)
	{
		char buf[1024];
		const int32_t n = ce_session_property_text(s, index, element, 1, buf, sizeof buf);
		ok = n >= 0;
		if (ok) now.assign(buf, static_cast<size_t>(std::min<int32_t>(n, sizeof buf - 1)));
		/* where it is now, and in which domain, as the engine's table says */
		const int64_t offset = ce_session_property_offset(s, index, element);
		if (offset >= 0) w.address = offset;
		/* a property of a dynamic table that is not there now */
		if (offset < 0 && ce_session_property_dynamic(s) != 0 && now.empty()) now = tr("ram_watch.not_there");
		/* the table is read on a first read only: a property keeps its domain */
		json::Json table;
		std::string jerr;
		if (const char *t = (fresh || !w.readable) ? ce_session_property_table(s) : nullptr; t != nullptr && json::parse(t, table, jerr))
			if (const json::Json *list = table.find("properties"); list != nullptr && static_cast<size_t>(index) < list->items.size())
			{
				const json::Json &p = list->items[static_cast<size_t>(index)];
				if (const json::Json *dm = p.find("domain")) w.domain = dm->text;
				if (const json::Json *en = p.find("endian")) w.bigEndian = en->text == "big";
			}
		if (w.domainSize == 0)
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				if (w.domain == ce_session_domain_name(s, i)) w.domainSize = ce_session_domain_size(s, i);
	}
	const bool was = w.readable;
	w.readable = ok;
	if (!ok) return;
	if (!was)
	{
		w.valueText = now;
		if (!w.seen) w.previousText = now;
		w.seen = true;
		return;
	}
	if (fresh) return;
	switch (previousTypeM_)
	{
	case 0: w.valueText = now; break;
	case 3:
		if (now != w.valueText)
		{
			w.previousText = w.valueText;
			w.valueText = now;
			w.changes++;
		}
		break;
	default:
		w.previousText = w.valueText;
		w.valueText = now;
		if (w.valueText != w.previousText) w.changes++;
		break;
	}
}

void RamWatchViewModel::refreshOnScreen()
{
	std::string lines;
	if (showOnScreen.get())
		for (const Watch &w : watches_)
		{
			if (!w.onScreen || w.separator) continue;
			const std::string value = !w.property.empty() ? (w.readable ? w.valueText : std::string("-"))
									: w.readable ? watch::format(w.value, w.size, w.display) : std::string("-");
			lines += (lines.empty() ? "" : "\n") + w.note + ": " + value;
		}
	onScreenText.set(lines);
}

void RamWatchViewModel::sizeFromDomain(Watch &w) const
{
	const auto found = domainSizes_.find(w.domain);
	if (found != domainSizes_.end()) w.domainSize = found->second;
}

bool RamWatchViewModel::index(const Args &a, size_t at, size_t &out, std::string &error) const
{
	int64_t i = 0;
	if (!arg(a, at, i, error)) return false;
	if (i < 0 || i >= static_cast<int64_t>(watches_.size()))
	{
		error = tr("ram_watch.no_watch", {std::to_string(i)});
		return false;
	}
	out = static_cast<size_t>(i);
	return true;
}

void RamWatchViewModel::listChanged(bool edit)
{
	count.set(static_cast<int64_t>(watches_.size()));
	if (edit) changed.set(true);
	const uint64_t serial = ++serial_;
	session_.onMachine([this, list = watches_, serial](ce_session *s) {
		watchesM_ = list;
		serialM_ = serial;
		readOnMachine(s, true);
	});
	rows_.reset.emit();
	refreshOnScreen();
	int64_t n = 0;
	for (const Watch &w : watches_) n += !w.separator;
	countText.set(tr("ram_watch.count", n));
	selectionChanged();
	showFileState(false);
}

void RamWatchViewModel::readOnMachine(ce_session *s, bool fresh)
{
	if (s == nullptr) return;
	std::map<std::string, int32_t> byName;
	for (int32_t i = 0; i < ce_session_domain_count(s); i++) byName[ce_session_domain_name(s, i)] = i;
	for (Watch &w : watchesM_)
	{
		if (w.separator) continue;
		if (!w.property.empty())
		{
			readProperty(s, w, fresh);
			continue;
		}
		const auto found = byName.find(w.domain);
		bool ok = false;
		uint64_t now = 0;
		if (found != byName.end())
		{
			w.domainSize = ce_session_domain_size(s, found->second);
			if (w.address + w.size <= w.domainSize) now = readValue(s, found->second, w.address, w.size, w.bigEndian, ok);
		}
		const bool was = w.readable;
		w.readable = ok;
		if (!ok) continue;
		/* read for the first time, or again on a machine opened since: its
		 * value, and a watch never seen before starts with it as the previous
		 * one; nothing counts as a change */
		if (!was)
		{
			w.value = now;
			if (!w.seen) w.previous = now;
			w.seen = true;
			continue;
		}
		/* the list changed, the machine did not move */
		if (fresh) continue;
		switch (previousTypeM_)
		{
		case 0: /* the first value seen stays the previous one */
			w.value = now;
			break;
		case 3: /* the value before the last change */
			if (now != w.value)
			{
				w.previous = w.value;
				w.value = now;
				w.changes++;
			}
			break;
		default: /* the last frame's */
			w.previous = w.value;
			w.value = now;
			if (w.value != w.previous) w.changes++;
			break;
		}
	}
	{
		std::lock_guard<std::mutex> lock(valuesM_);
		valuesOut_ = watchesM_;
		valuesSerial_ = serialM_;
	}
	if (!postedM_.exchange(true))
		later([this] {
			postedM_ = false;
			std::vector<Watch> read;
			uint64_t serial = 0;
			{
				std::lock_guard<std::mutex> lock(valuesM_);
				read = valuesOut_;
				serial = valuesSerial_;
			}
			if (serial != serial_ || read.size() != watches_.size()) return;
			for (size_t i = 0; i < read.size(); i++)
			{
				watches_[i].value = read[i].value;
				watches_[i].previous = read[i].previous;
				watches_[i].changes = read[i].changes;
				watches_[i].readable = read[i].readable;
				watches_[i].seen = read[i].seen;
				watches_[i].valueText = read[i].valueText;
				watches_[i].previousText = read[i].previousText;
				if (!read[i].property.empty())
				{
					watches_[i].address = read[i].address;
					watches_[i].domain = read[i].domain;
					watches_[i].bigEndian = read[i].bigEndian;
				}
				watches_[i].domainSize = read[i].domainSize;
			}
			if (!watches_.empty()) rows_.changed.emit(0, static_cast<int64_t>(watches_.size()) - 1);
			refreshOnScreen();
			selectionChanged();
		});
}

/* Chimera's .wch: a SystemID line, a line a watch - address in hex, size
 * letter, display letter, 1 for big endian, domain, note, split by tabs -
 * and an OnScreen line naming by place the watches shown over the picture.
 * Files from other emulators have a count first, a place number before
 * each line and no domain. */
std::vector<RamWatchViewModel::Watch> RamWatchViewModel::readList(std::istream &in) const
{
	std::vector<std::string> names;
	{
		std::string list = domains.get(), one;
		std::istringstream parts(list);
		while (std::getline(parts, one, ',')) names.push_back(one);
	}
	std::vector<Watch> read;
	std::vector<int> placeOf; /* each line of the file: the watch it gave, or -1 */
	std::string onScreen, line;
	bool ours = true;
	while (std::getline(in, line))
	{
		if (!line.empty() && line.back() == '\r') line.pop_back();
		if (line.size() < 5)
		{
			ours = false;
			continue;
		}
		if (line.rfind("Domain", 0) == 0) ours = true;
		if (line.rfind("SystemID", 0) == 0) continue;
		if (line.rfind(kOnScreen, 0) == 0)
		{
			onScreen = line.substr(std::char_traits<char>::length(kOnScreen));
			continue;
		}
		if (std::count(line.begin(), line.end(), '\t') != 5) continue;
		placeOf.push_back(-1);
		if (!ours) line = line.substr(line.find('\t') + 1);
		std::vector<std::string> f;
		{
			std::string rest = line;
			const size_t fields = ours ? 5 : 4;
			for (size_t k = 0; k < fields; k++)
			{
				const size_t tab = rest.find('\t');
				if (tab == std::string::npos) break;
				f.push_back(rest.substr(0, tab));
				rest = rest.substr(tab + 1);
			}
			f.push_back(rest);
		}
		if (f.size() < (ours ? 6u : 5u) || f[1].empty() || f[2].empty() || f[3].empty()) continue;
		Watch w;
		char *end = nullptr;
		w.address = std::strtoll(f[0].c_str(), &end, 16);
		if (f[0].empty() || end == nullptr || *end != '\0') continue;
		const int size = watch::sizeFromLetter(f[1][0]);
		if (f[1][0] == 'p' && ours)
		{
			/* a game property, known by its name (the note) */
			Watch p;
			p.propertyLine = line;
			p.property = p.note = trimLineEnds(f[5]);
			p.address = w.address;
			p.domain = f[4];
			p.bigEndian = !f[3].empty() && f[3][0] != '0';
			placeOf.back() = static_cast<int>(read.size());
			read.push_back(p);
			continue;
		}
		if (size < 0) continue;
		if (f[3][0] < '0' || f[3][0] > '9') continue;
		w.bigEndian = f[3][0] != '0';
		if (size == 0)
		{
			w.separator = true;
			w.note = trimLineEnds(ours ? f[5] : f[4]);
		}
		else
		{
			w.size = size;
			if (!watch::displayFromLetter(f[2][0], w.display) || !watch::fits(w.size, w.display)) w.display = watch::Display::Unsigned;
			w.domain = ours ? f[4] : std::string();
			/* a domain this machine does not have: its main memory, as Chimera does */
			if (!names.empty() && std::find(names.begin(), names.end(), w.domain) == names.end()) w.domain = names.front();
			w.note = trimLineEnds(ours ? f[5] : f[4]);
		}
		sizeFromDomain(w);
		placeOf.back() = static_cast<int>(read.size());
		read.push_back(w);
	}
	/* a place names a line of the file */
	std::istringstream places(onScreen);
	for (std::string p; std::getline(places, p, ',');)
	{
		const size_t at = p.find_first_not_of(' ');
		if (at == std::string::npos) continue;
		char *end = nullptr;
		const long i = std::strtol(p.c_str() + at, &end, 10);
		if (i >= 0 && i < static_cast<long>(placeOf.size()) && placeOf[static_cast<size_t>(i)] >= 0 &&
			!read[static_cast<size_t>(placeOf[static_cast<size_t>(i)])].separator)
			read[static_cast<size_t>(placeOf[static_cast<size_t>(i)])].onScreen = true;
	}
	return read;
}

bool RamWatchViewModel::load(const std::string &file, bool append, std::string &error)
{
	std::ifstream in(file, std::ios::binary);
	if (!in)
	{
		error = tr("new_project.cannot_read", {file});
		return false;
	}
	const std::vector<Watch> read = readList(in);
	if (!append) watches_.clear();
	watches_.insert(watches_.end(), read.begin(), read.end());
	path.set(file);
	changed.set(append);
	listChanged(false);
	addRecent(file);
	return true;
}

bool RamWatchViewModel::save(const std::string &file, std::string &error)
{
	std::string text = "SystemID " + systemId_ + kNewLine;
	std::string onScreen;
	for (size_t i = 0; i < watches_.size(); i++)
	{
		text += lineOf(watches_[i]) + kNewLine;
		if (watches_[i].onScreen) onScreen += (onScreen.empty() ? "" : ",") + std::to_string(i);
	}
	if (!onScreen.empty()) text += kOnScreen + onScreen + kNewLine;
	text += kNewLine;
	/* written beside it and moved over it: a failed save leaves the old file */
	const std::filesystem::path writing = std::filesystem::path(file).concat(".writing");
	{
		std::ofstream out(writing, std::ios::binary | std::ios::trunc);
		out << text;
		if (!out)
		{
			error = tr("session.could_not_write", {file});
			return false;
		}
	}
	std::error_code ec;
	std::filesystem::rename(writing, file, ec);
	if (ec)
	{
		std::filesystem::remove(writing, ec);
		error = tr("session.could_not_write", {file});
		return false;
	}
	path.set(file);
	changed.set(false);
	addRecent(file);
	showFileState(true);
	return true;
}

std::string RamWatchViewModel::lineOf(const Watch &w) const
{
	if (!w.property.empty())
		/* as Chimera writes one: where it is now when a machine says so,
		 * else the line the file had */
		return w.domainSize != 0 || w.propertyLine.empty()
			? watch::hex(w.address, watch::addressDigits(w.domainSize != 0 ? w.domainSize : 0xFE)) + "\tp\tu\t" +
				  (w.bigEndian ? "1" : "0") + "\t" + w.domain + "\t" + w.property
			: w.propertyLine;
	if (w.separator) return std::string("0\tS\t_\t1\t\t") + trimLineEnds(w.note);
	const int digits = watch::addressDigits(w.domainSize != 0 ? w.domainSize : 0xFE);
	return watch::hex(w.address, digits) + "\t" + watch::sizeLetter(w.size) + "\t" + watch::displayLetter(w.display) + "\t" +
		   (w.bigEndian ? "1" : "0") + "\t" + w.domain + "\t" + trimLineEnds(w.note);
}

std::vector<size_t> RamWatchViewModel::selectedRows() const
{
	std::vector<size_t> out;
	for (size_t i = 0; i < watches_.size(); i++)
		if (watches_[i].selected) out.push_back(i);
	return out;
}

bool RamWatchViewModel::invalid(const Watch &w) const
{
	/* Chimera's rule: the address is past the domain's end */
	return !w.separator && w.property.empty() && w.domainSize != 0 && w.address >= w.domainSize;
}

void RamWatchViewModel::selectionChanged()
{
	int64_t picked = 0, pickedWatches = 0, bad = 0;
	bool split = true, writable = true;
	for (const Watch &w : watches_)
	{
		bad += invalid(w);
		if (!w.selected) continue;
		picked++;
		if (w.separator) continue;
		pickedWatches++;
		/* Chimera splits a word or double word shown in hex or binary into halves */
		split = split && w.property.empty() && (w.size == 2 || w.size == 4) &&
				(w.display == watch::Display::Hex || w.display == watch::Display::Binary);
		const auto found = domainWritable_.find(w.domain);
		writable = writable && found != domainWritable_.end() && found->second;
	}
	selectedCount.set(picked);
	selectedWatches.set(pickedWatches);
	canSplit.set(pickedWatches > 0 && split);
	canPoke.set(pickedWatches > 0 && writable);
	selectionFrozen.set(pickedWatches > 0 && std::all_of(watches_.begin(), watches_.end(), [this](const Watch &w) {
		return !w.selected || w.separator || frozen(w);
	}));
	invalidCount.set(bad);
}

void RamWatchViewModel::showFileState(bool saved)
{
	const std::u8string u = std::filesystem::u8path(path.get()).filename().u8string();
	const std::string name(reinterpret_cast<const char *>(u.data()), u.size());
	if (name.empty()) status.set("");
	else status.set(saved ? tr("ram_watch.saved", {name}) : name + (changed.get() ? " *" : ""));
}

void RamWatchViewModel::addRecent(const std::string &file)
{
	/* newest first, each once, ten at most */
	std::string out = file;
	std::istringstream in(recentFiles.get());
	std::string l;
	int kept = 1;
	while (std::getline(in, l) && kept < 10)
		if (!l.empty() && l != file)
		{
			out += "\n" + l;
			kept++;
		}
	if (out != recentFiles.get()) recentFiles.set(out);
}

bool RamWatchViewModel::poke(const Watch &w, const std::string &text, std::string &error)
{
	uint64_t v = 0;
	if (!watch::parse(text, w.display, v, w.size))
	{
		error = tr("ram_watch.not_a_value", {text, watch::displayName(w.display)});
		return false;
	}
	std::string bytes(static_cast<size_t>(w.size), '\0');
	for (int i = 0; i < w.size; i++)
	{
		const int shift = 8 * (w.bigEndian ? w.size - 1 - i : i);
		bytes[static_cast<size_t>(i)] = static_cast<char>((v >> shift) & 0xFF);
	}
	if (!session_.run("writeMemory", {w.domain, w.address, bytes}, error).has_value()) return false;
	/* a frozen address is frozen at what was poked, as Chimera's RAM Poke does */
	if (session_.frozen(w.domain, w.address))
		for (const SessionViewModel::Freeze &z : session_.freezes())
			if (z.enabled && z.property.empty() && z.domain == w.domain && z.address == w.address && z.size == w.size)
			{
				SessionViewModel::Freeze f = z;
				f.value = v;
				f.bigEndian = w.bigEndian;
				std::string ignored;
				session_.freeze(f, ignored);
				break;
			}
	return true;
}

bool RamWatchViewModel::frozen(const Watch &w) const
{
	if (w.separator) return false;
	return !w.property.empty() ? session_.propertyFrozen(w.property) : session_.frozen(w.domain, w.address);
}

std::string RamWatchViewModel::Rows::columnName(int32_t column) const
{
	static const char *const names[] = {"address", "value", "previous", "changes", "diff", "domain", "note", "size", "shownAs", "onScreen",
		"type", "selected", "invalid", "separator", "frozen"};
	return column >= 0 && column < 15 ? names[column] : "";
}

Value RamWatchViewModel::Rows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Watch &w = vm_.watches_[static_cast<size_t>(row)];
	/* picked, and past its domain's end, whatever it is */
	if (column == 11) return w.selected;
	if (column == 12) return vm_.invalid(w);
	if (column == 13) return w.separator;
	if (column == 14) return vm_.frozen(w);
	if (w.separator) return column == 6 ? Value(w.note) : column == 9 ? Value(false) : Value(std::string());
	/* Chimera's Type column: the size, the display's letter, the byte order's (2hB: a word, hex, big end first) */
	if (column == 10)
	{
		if (!w.property.empty()) return tr("ram_watch.game_property");
		static const char *const letters[] = {"u", "s", "h", "b", "F", "F2", "F6", "f"};
		const int d = static_cast<int>(w.display);
		return std::to_string(w.size) + (d >= 0 && d < 8 ? letters[d] : "") + (w.bigEndian ? "B" : "L");
	}
	if (!w.property.empty())
		switch (column)
		{
		case 0: return w.domainSize != 0 ? watch::hex(w.address, watch::addressDigits(w.domainSize - 1)) : std::string("-");
		case 1: return w.readable ? w.valueText : std::string("-");
		case 2: return w.readable ? w.previousText : std::string("-");
		case 3: return w.changes;
		case 5: return w.domain;
		case 6: return w.note;
		case 7: return int64_t{0};
		case 8: return tr("ram_watch.game_property");
		case 9: return w.onScreen;
		default: return std::string();
		}
	switch (column)
	{
	case 0: return watch::hex(w.address, watch::addressDigits(w.domainSize != 0 ? w.domainSize - 1 : 0xFE));
	case 1: return w.readable ? watch::format(w.value, w.size, w.display) : std::string("-");
	case 2: return w.readable ? watch::format(w.previous, w.size, w.display) : std::string("-");
	case 3: return w.changes;
	case 4: return w.readable ? difference(w) : std::string("-");
	case 5: return w.domain;
	case 6: return w.note;
	case 7: return static_cast<int64_t>(w.size);
	case 8: return std::string(watch::displayName(w.display));
	case 9: return w.onScreen;
	default: return {};
	}
}

} // namespace chimera::vm
