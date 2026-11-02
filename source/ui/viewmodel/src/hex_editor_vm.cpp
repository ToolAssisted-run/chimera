#include "chimera/vm/hex_editor_vm.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "chimera/engine.h"
#include "chimera/vm/json.hpp"
#include "chimera/vm/ram_watch_vm.hpp"
#include "chimera/vm/watch_values.hpp"

namespace chimera::vm {

namespace {
/* the open game's file, as Chimera's hex editor names it */

int32_t domainIndex(ce_session *s, const std::string &name)
{
	for (int32_t i = 0; i < ce_session_domain_count(s); i++)
		if (name == ce_session_domain_name(s, i)) return i;
	return -1;
}

/* "12 AB" or "12ab": the bytes, or false */
bool hexBytes(const std::string &text, std::vector<uint8_t> &out)
{
	std::string digits;
	for (char c : text)
		if (!std::isspace(static_cast<unsigned char>(c))) digits += c;
	if (digits.empty() || digits.size() % 2 != 0) return false;
	out.clear();
	for (size_t i = 0; i < digits.size(); i += 2)
	{
		const std::string two = digits.substr(i, 2);
		if (two.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
		out.push_back(static_cast<uint8_t>(std::strtoul(two.c_str(), nullptr, 16)));
	}
	return true;
}

std::string upperHex(uint32_t v, int digits)
{
	char buf[16];
	std::snprintf(buf, sizeof buf, "%0*X", digits, v);
	return buf;
}
} // namespace

HexEditorViewModel::HexEditorViewModel(Dispatcher &dispatcher, SessionViewModel &session)
	: ViewModel("hexEditor", dispatcher), session_(session)
{
	expose("domains", "the machine's memory domains (and \"File on Disk\" with a game open by itself), comma-separated", domains);
	expose("domain", "the domain shown", domain, true);
	expose("size", "bytes a value takes: 1, 2 or 4", size, true);
	expose("bigEndian", "values are stored most significant byte first", bigEndian, true);
	expose("pickedFrozen", "the address highlighted is frozen (Freeze Address reads Unfreeze Address)", pickedFrozen);
	expose("anyFrozen", "any address is frozen (Unfreeze All is offered)", anyFrozen);
	expose("lineCount", "lines of 16 bytes the domain has", lineCount);
	expose("topLine", "the first line shown", topLine, true);
	expose("visibleLines", "how many lines are shown", visibleLines, true);
	expose("watching", "read again after each frame (the window is open)", watching, true);
	expose("picked", "the address highlighted, -1 for none", picked, true);
	expose("secondaryCount", "addresses highlighted beside it", secondaryCount);
	expose("writable", "the domain can be written", writable);
	expose("status", "what the hex editor last had to say", status);
	expose("nibbles", "hex digits typed into the highlighted address so far", nibbles);
	expose("title", "the window's title, as Chimera's", title);
	expose("boxTitle", "the box's title: the system, the domain, its addresses, what is being typed", boxTitle);
	expose("header", "the column numbers over the values", header);
	expose("findString", "what Find Next and Find Prev look for, in hex", findString, true);
	expose("findText", "the Find window last looked for text", findText, true);
	expose("tableFile", "the .tbl file the text is shown by, \"\" for none", tableFile);
	expose("recentTables", ".tbl files read, newest first, a line each", recentTables, true);
	expose("colors", "Custom Colors as JSON (Background, Foreground, MenuBar, Freeze, Highlight, HighlightFreeze), \"{}\" for the theme's",
		colors, true);
	rows("lines", "the lines shown: address; a value and whether it changed for each place (v0, c0, ...); the text; each "
		"place's highlight (h0.., 0 none, 1 the address, 2 one beside it); each byte's character (t0..) and highlight (th0..)", lines_);

	command("goTo", "Go to Address: an address highlighted and brought into view", {"address"}, [this](const Args &a, std::string &error) -> Value {
		int64_t address = 0;
		if (!arg(a, 0, address, error)) return {};
		if (address < 0 || address >= domainSize_)
		{
			error = tr("hex.no_address", {watch::hex(address, 1)});
			return {};
		}
		secondary_.clear();
		highlight(address);
		return {};
	});
	command("poke", "writes a value (hex, of the size shown) into the domain at an address", {"address", "value"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t address = 0;
			std::string text;
			if (!arg(a, 0, address, error) || !arg(a, 1, text, error)) return {};
			const int sz = static_cast<int>(size.get());
			uint64_t value = 0;
			if (text.size() > static_cast<size_t>(sz * 2) || !watch::parse(text, watch::Display::Hex, value))
			{
				error = tr("hex.not_value_of", sz, {text});
				return {};
			}
			if (!writable.get())
			{
				error = tr("session.domain_read_only", {domain.get()});
				return {};
			}
			if (address < 0 || address + sz > domainSize_)
			{
				error = tr("hex.no_address", {watch::hex(address, 1)});
				return {};
			}
			if (!writeValue(address, static_cast<uint32_t>(value), error)) return {};
			status.set(tr("hex.wrote_at", {watch::hex(address, digits())}));
			return {};
		});
	command("find", "Find Next: the next place these bytes (hex) are, after the address highlighted", {"bytes"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			std::vector<uint8_t> want;
			if (!hexBytes(text, want))
			{
				error = tr("hex.not_bytes", {text});
				return {};
			}
			const auto got = run("findNext", {text}, error);
			return got.has_value() ? *got : Value{};
		});
	auto finder = [this](bool forward) {
		return [this, forward](const Args &a, std::string &error) -> Value {
			std::string text = findString.get();
			if (!a.empty() && !arg(a, 0, text, error)) return {};
			std::string digitsOnly;
			for (char c : text)
				if (!std::isspace(static_cast<unsigned char>(c))) digitsOnly += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			if (digitsOnly.empty()) return {};
			/* an odd digit is the high half of a last byte, as Chimera reads it */
			if (digitsOnly.size() % 2 == 1) digitsOnly += "0";
			std::vector<uint8_t> want;
			if (!hexBytes(digitsOnly, want))
			{
				error = tr("hex.not_bytes", {text});
				return {};
			}
			const int sz = static_cast<int>(size.get());
			const int64_t n = static_cast<int64_t>(want.size());
			const int64_t at = picked.get();
			/* Chimera's starting places: after the highlighted value, or before it */
			int64_t from = 0;
			if (forward) from = at < 0 || at >= domainSize_ - 1 - n ? 0 : at + sz;
			else from = at < 0 ? domainSize_ - sz - n : at - 1;
			int64_t hit = findBytes(want, forward, from);
			/* not found: the other way, as Chimera's */
			if (hit < 0) hit = findBytes(want, false, at < 0 ? domainSize_ - sz - n : at - 1);
			if (hit < 0)
			{
				status.set(tr("hex.not_found"));
				return int64_t{-1};
			}
			/* kept for Find Next only when found, as Chimera keeps it */
			findString.set(digitsOnly);
			found(hit, want.size());
			return hit;
		};
	};
	command("findNext", "Find Next (F3): the next place the bytes (hex; what was last looked for when none are given) are", {"bytes"}, finder(true));
	command("findPrev", "Find Prev (F2): the place before the highlighted address the bytes are", {"bytes"}, finder(false));
	command("textBytes", "text as the bytes Find looks for, in hex: through the .tbl file when one is read, else as ASCII", {"text"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text, out;
			if (!arg(a, 0, text, error)) return {};
			for (const char c : text)
			{
				int byte = static_cast<unsigned char>(c);
				if (!table_.empty())
				{
					byte = 0;
					for (const auto &kv : table_)
						if (kv.second == c)
						{
							byte = kv.first;
							break;
						}
				}
				out += upperHex(static_cast<uint32_t>(byte & 0xFF), 2);
			}
			return out;
		});
	command("findValues", "what the Find window starts with: the values highlighted, in hex", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		const int sz = static_cast<int>(size.get());
		for (const int64_t at : highlightedAll()) out += upperHex(valueAt(at), sz * 2);
		return out;
	});
	command("click", "a click on an address: mode \"only\", \"toggle\" (Ctrl) or \"range\" (Shift, from the address highlighted)",
		{"address", "mode"}, [this](const Args &a, std::string &error) -> Value {
			int64_t address = 0;
			std::string mode = "only";
			if (!arg(a, 0, address, error)) return {};
			if (a.size() > 1 && !arg(a, 1, mode, error)) return {};
			if (address < 0 || address >= domainSize_) return {};
			const int sz = static_cast<int>(size.get());
			if (mode == "toggle")
			{
				if (address == picked.get())
				{
					picked.set(-1);
				}
				else if (!secondary_.erase(address))
					secondary_.insert(address);
			}
			else if (mode == "range" && picked.get() >= 0)
			{
				/* Chimera's shift-click: every value from the one highlighted to this one */
				secondary_.clear();
				const int64_t h = picked.get();
				if (address < h)
					for (int64_t x = address; x < h; x += sz) secondary_.insert(x);
				else
					for (int64_t x = h + sz; x <= address; x += sz) secondary_.insert(x);
			}
			else
			{
				secondary_.clear();
				findString.set("");
				highlight(address);
			}
			nibbles.set("");
			titles();
			lines_.reset.emit();
			return {};
		});
	command("key", "a key in the values, as Chimera's: Up, Down, Left, Right, PageUp, PageDown, Tab, Home, End, Escape; with "
		"Shift the addresses passed are highlighted too", {"key", "shift"}, [this](const Args &a, std::string &error) -> Value {
			std::string key;
			bool shift = false;
			if (!arg(a, 0, key, error)) return {};
			if (a.size() > 1 && !arg(a, 1, shift, error)) return {};
			const int64_t sz = size.get(), at = picked.get(), rows = visibleLines.get();
			auto add = [this](int64_t x) {
				if (x >= 0 && x < domainSize_) secondary_.insert(x);
			};
			auto go = [&](int64_t to, auto passed) {
				if (shift) passed();
				else secondary_.clear();
				highlight(to);
			};
			if (key == "Up") go(at - 16, [&] { for (int64_t i = at - 16 + sz; i <= at; i += sz) add(i); });
			else if (key == "Down") go(at + 16, [&] { for (int64_t i = at; i < at + 16; i += sz) add(i); });
			else if (key == "Left") go(at - sz, [&] { add(at); });
			else if (key == "Right") go(at + sz, [&] { add(at); });
			else if (key == "PageUp") go(at - rows * 16, [&] { for (int64_t i = at - rows * 16 + 1; i <= at; i += sz) add(i); });
			else if (key == "PageDown") go(at + rows * 16, [&] { for (int64_t i = at + 1; i < at + rows * 16; i += sz) add(i); });
			else if (key == "Tab") { secondary_.clear(); highlight(shift ? at - 8 : at + 8); }
			else if (key == "Home") go(0, [&] { for (int64_t i = 1; i <= at; i += sz) add(i); });
			else if (key == "End") go(domainSize_ - sz, [&] { for (int64_t i = at; i < domainSize_ - sz; i += sz) add(i); });
			else if (key == "Escape")
			{
				secondary_.clear();
				picked.set(-1);
			}
			else
			{
				error = tr("hex.no_key", {key});
				return {};
			}
			titles();
			lines_.reset.emit();
			return {};
		});
	command("type", "a hex digit typed: once a value's worth is in, it is written into the address highlighted, and the next "
		"one highlighted", {"digit"}, [this](const Args &a, std::string &error) -> Value {
			std::string digit;
			if (!arg(a, 0, digit, error)) return {};
			if (digit.size() != 1 || !std::isxdigit(static_cast<unsigned char>(digit[0]))) return {};
			if (!writable.get() || picked.get() < 0) return {};
			const int sz = static_cast<int>(size.get());
			std::string typed = nibbles.get() + static_cast<char>(std::toupper(static_cast<unsigned char>(digit[0])));
			if (static_cast<int>(typed.size()) == sz * 2)
			{
				const int64_t at = picked.get();
				nibbles.set("");
				if (!writeValue(at, static_cast<uint32_t>(std::strtoul(typed.c_str(), nullptr, 16)), error)) return {};
				highlight(at + sz);
			}
			else
				nibbles.set(typed);
			titles();
			return {};
		});
	auto stepper = [this](int delta) {
		return [this, delta](const Args &, std::string &error) -> Value {
			if (!writable.get()) return {};
			const int sz = static_cast<int>(size.get());
			const uint32_t mask = sz == 4 ? 0xFFFFFFFFu : (1u << (8 * sz)) - 1;
			for (const int64_t at : highlightedAll())
			{
				const uint32_t v = (valueAt(at) + static_cast<uint32_t>(delta)) & mask;
				if (!writeValue(at, v, error)) return {};
				/* as Chimera's: a frozen value is the one stepped */
				for (const SessionViewModel::Freeze &z : session_.freezes())
					if (z.enabled && z.property.empty() && z.domain == domain.get() && z.address == at)
					{
						SessionViewModel::Freeze f = z;
						f.value = v;
						session_.freeze(f, error);
						break;
					}
			}
			return {};
		};
	};
	command("toggleFreeze", "the values highlighted frozen at what they are now, or, when the address highlighted is, every "
		"freeze on them gone (Chimera's Freeze Address, Space); false with what the machine refused", {},
		[this](const Args &, std::string &error) -> Value {
			if (!writable.get() || picked.get() < 0) return {};
			const bool thaw = placeFrozen(picked.get());
			for (const int64_t at : highlightedAll())
			{
				if (thaw)
				{
					session_.unfreeze(domain.get(), at);
					continue;
				}
				SessionViewModel::Freeze f;
				f.domain = domain.get();
				f.address = at;
				f.size = static_cast<int32_t>(size.get());
				f.bigEndian = bigEndian.get();
				f.value = valueAt(at);
				if (!session_.freeze(f, error)) return false;
			}
			return true;
		});
	command("unfreezeHighlighted", "the freezes on the values highlighted go (Delete)", {}, [this](const Args &, std::string &) -> Value {
		for (const int64_t at : highlightedAll()) session_.unfreeze(domain.get(), at);
		return {};
	});
	command("unfreezeAll", "no address frozen any longer (Chimera's Unfreeze All, Shift+Delete)", {}, [this](const Args &, std::string &) -> Value {
		session_.unfreezeAll();
		return {};
	});
	command("increment", "each value highlighted made one more (+)", {}, stepper(1));
	command("decrement", "each value highlighted made one less (-)", {}, stepper(-1));
	command("copyText", "Copy: the bytes highlighted, in hex, run together", {}, [this](const Args &, std::string &) -> Value {
		std::vector<int64_t> all = highlightedAll();
		std::sort(all.begin(), all.end());
		std::string out;
		const int sz = static_cast<int>(size.get());
		for (const int64_t at : all)
		{
			std::vector<uint8_t> b;
			if (!readNow(at, sz, b)) continue;
			for (const uint8_t x : b) out += upperHex(x, 2);
		}
		return out;
	});
	command("exportText", "Export: the values highlighted, in hex, a space between", {}, [this](const Args &, std::string &) -> Value {
		std::vector<int64_t> all = highlightedAll();
		std::sort(all.begin(), all.end());
		std::string out;
		const int sz = static_cast<int>(size.get());
		for (size_t i = 0; i < all.size(); i++) out += (i ? " " : "") + upperHex(valueAt(all[i]), sz * 2);
		return out;
	});
	command("paste", "Paste: the hex digits of a text written as bytes from the address highlighted", {"text"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text, hex;
			if (!arg(a, 0, text, error)) return {};
			for (const char c : text)
				if (std::isxdigit(static_cast<unsigned char>(c))) hex += c;
			std::vector<uint8_t> bytes;
			for (size_t i = 0; i + 1 < hex.size(); i += 2) bytes.push_back(static_cast<uint8_t>(std::strtoul(hex.substr(i, 2).c_str(), nullptr, 16)));
			const int64_t at = std::max<int64_t>(picked.get(), 0);
			if (at + static_cast<int64_t>(bytes.size()) > domainSize_) bytes.resize(static_cast<size_t>(std::max<int64_t>(0, domainSize_ - at)));
			if (bytes.empty()) return {};
			if (!writeNow(at, bytes, error)) return {};
			return {};
		});
	command("saveBinary", "every byte of the domain written to a file (Save as binary..., or Save as ROM... for File on Disk)", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			if (!arg(a, 0, file, error)) return {};
			std::vector<uint8_t> all;
			if (!readNow(0, domainSize_, all))
			{
				error = tr("new_project.cannot_read", {domain.get()});
				return {};
			}
			std::ofstream out(std::filesystem::u8path(file), std::ios::binary | std::ios::trunc);
			out.write(reinterpret_cast<const char *>(all.data()), static_cast<std::streamsize>(all.size()));
			if (!out)
			{
				error = tr("session.could_not_write", {file});
				return {};
			}
			status.set(tr("hex.saved", {file}));
			return {};
		});
	command("saveText", "the domain written to a file as text: sixteen bytes a line in hex, a space after each", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			if (!arg(a, 0, file, error)) return {};
			std::vector<uint8_t> all;
			if (!readNow(0, domainSize_, all))
			{
				error = tr("new_project.cannot_read", {domain.get()});
				return {};
			}
#ifdef _WIN32
			const char *const nl = "\r\n";
#else
			const char *const nl = "\n";
#endif
			std::string text;
			/* the last line as long as what is left, not dropped */
			for (int64_t i = 0; i * 16 < domainSize_; i++)
			{
				for (int64_t j = i * 16; j < std::min<int64_t>(i * 16 + 16, domainSize_); j++) text += upperHex(all[static_cast<size_t>(j)], 2) + " ";
				text += nl;
			}
			text += nl;
			std::ofstream out(std::filesystem::u8path(file), std::ios::binary | std::ios::trunc);
			out << text;
			if (!out)
			{
				error = tr("session.could_not_write", {file});
				return {};
			}
			status.set(tr("hex.saved", {file}));
			return {};
		});
	command("importBinary", "Import as binary: a file's bytes written into the domain from its start", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			if (!arg(a, 0, file, error)) return {};
			if (!writable.get())
			{
				error = tr("hex.cannot_poke");
				return {};
			}
			std::ifstream in(std::filesystem::u8path(file), std::ios::binary);
			if (!in)
			{
				error = tr("new_project.cannot_read", {file});
				return {};
			}
			std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			if (static_cast<int64_t>(bytes.size()) > domainSize_) bytes.resize(static_cast<size_t>(domainSize_));
			if (!writeNow(0, bytes, error)) return {};
			return static_cast<int64_t>(bytes.size());
		});
	command("loadTable", "a .tbl file read: each line \"hex=character\", the bytes it names shown as its characters", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string file;
			if (!arg(a, 0, file, error)) return {};
			std::ifstream in(std::filesystem::u8path(file), std::ios::binary);
			if (!in)
			{
				error = tr("new_project.cannot_read", {file});
				return {};
			}
			std::map<int, char> read;
			std::string line;
			while (std::getline(in, line))
			{
				if (!line.empty() && line.back() == '\r') line.pop_back();
				const size_t eq = line.find('=');
				if (eq == std::string::npos || eq + 1 >= line.size()) continue;
				read[static_cast<int>(std::strtol(line.substr(0, eq).c_str(), nullptr, 16))] = line[eq + 1];
			}
			table_ = read;
			tableFile.set(file);
			addRecent(file);
			lines_.reset.emit();
			return static_cast<int64_t>(read.size());
		});
	command("closeTable", "the .tbl file let go: the text as ASCII again", {}, [this](const Args &, std::string &) -> Value {
		table_.clear();
		tableFile.set("");
		lines_.reset.emit();
		return {};
	});
	command("addToRamWatch", "the addresses highlighted added to RAM Watch, at the size shown, in hex", {}, [this](const Args &, std::string &error) -> Value {
		if (ramWatch_ == nullptr)
		{
			error = tr("ram_search.no_ram_watch");
			return {};
		}
		std::string lines;
		const int sz = static_cast<int>(size.get());
		for (const int64_t at : highlightedAll())
			lines += watch::hex(at, digits()) + "\t" + watch::sizeLetter(sz) + "\th\t" + (bigEndian.get() ? "1" : "0") + "\t" + domain.get() + "\t\n";
		if (lines.empty()) return int64_t{0};
		const auto added = ramWatch_->run("paste", {lines}, error);
		return added.has_value() ? *added : Value{};
	});
	command("pokeInfo", "what RAM Poke shows for the addresses highlighted, as JSON (in hex, at the size shown)", {},
		[this](const Args &, std::string &) -> Value {
			json::Json out;
			out.kind = json::Json::Kind::Object;
			const std::vector<int64_t> all = highlightedAll();
			if (all.empty()) return json::write(out);
			std::string list;
			for (size_t i = 0; i < all.size() && i < 10; i++) list += (i ? "," : "") + watch::hex(all[i], digits());
			const int sz = static_cast<int>(size.get());
			out.members.emplace_back("addresses", json::Json::string(list));
			out.members.emplace_back("domain", json::Json::string(domain.get()));
			out.members.emplace_back("size", json::Json::string(tr(sz == 1 ? "watch.size_byte" : sz == 2 ? "watch.size_word" : "watch.size_dword")));
			out.members.emplace_back("display", json::Json::string(watch::displayName(watch::Display::Hex)));
			out.members.emplace_back("hex", json::Json::boolean_(true));
			out.members.emplace_back("endian", json::Json::string(tr(bigEndian.get() ? "watch.big_endian" : "watch.little_endian")));
			out.members.emplace_back("value", json::Json::string(upperHex(valueAt(all.front()), sz * 2)));
			out.members.emplace_back("mixed", json::Json::boolean_(false));
			out.members.emplace_back("property", json::Json::boolean_(false));
			return json::write(out);
		});
	command("pokeSelected", "a value (hex) written into every address highlighted. False with what went wrong", {"value"},
		[this](const Args &a, std::string &error) -> Value {
			std::string text;
			if (!arg(a, 0, text, error)) return {};
			uint64_t v = 0;
			if (!watch::parse(text, watch::Display::Hex, v))
			{
				error = tr("ram_search.write_failed");
				return false;
			}
			for (const int64_t at : highlightedAll())
				if (!writeValue(at, static_cast<uint32_t>(v), error))
				{
					error = tr("ram_search.write_failed");
					return false;
				}
			return true;
		});
	command("setColor", "a Custom Color: Background, Foreground, MenuBar, Freeze, Highlight or HighlightFreeze, as #RRGGBB", {"role", "color"},
		[this](const Args &a, std::string &error) -> Value {
			std::string role, color;
			if (!arg(a, 0, role, error) || !arg(a, 1, color, error)) return {};
			static const char *const roles[] = {"Background", "Foreground", "MenuBar", "Freeze", "Highlight", "HighlightFreeze"};
			if (std::find_if(std::begin(roles), std::end(roles), [&](const char *r) { return role == r; }) == std::end(roles))
			{
				error = tr("hex.no_colour", {role});
				return {};
			}
			json::Json all;
			std::string jerr;
			if (!json::parse(colors.get(), all, jerr) || all.kind != json::Json::Kind::Object)
			{
				all = json::Json{};
				all.kind = json::Json::Kind::Object;
			}
			bool put = false;
			for (auto &m : all.members)
				if (m.first == role)
				{
					m.second = json::Json::string(color);
					put = true;
				}
			if (!put) all.members.emplace_back(role, json::Json::string(color));
			colors.set(json::write(all));
			return {};
		});
	command("resetColors", "Custom Colors back to the theme's", {}, [this](const Args &, std::string &) -> Value {
		colors.set("{}");
		return {};
	});

	/* after each frame only while the window is open; closed, the next read
	 * compares with nothing, so it marks no byte as changed */
	hooks_ = session_.addMachineHooks(
		[this](ce_session *s) {
			if (watchingM_.load()) readOnMachine(s);
			else lastM_.clear();
		},
		[this](ce_session *) {
			lastM_.clear();
			lastFromM_ = -1;
		});
	auto moved = [this] {
		if (topLine.get() < 0) topLine.set(0);
		reread();
	};
	topWatch_ = topLine.changed.connect(moved);
	linesResetWatch_ = lines_.reset.connect([this] { linesAnnounced_ = lines_.rowCount(); });
	watchingWatch_ = watching.changed.connect([this] {
		watchingM_.store(watching.get());
		if (watching.get()) reread();
	});
	visibleWatch_ = visibleLines.changed.connect(moved);
	sizeWatch_ = size.changed.connect([this] {
		if (size.get() != 1 && size.get() != 2 && size.get() != 4) size.set(1);
		/* a new data size: the highlights beside the first go */
		secondary_.clear();
		nibbles.set("");
		titles();
		lines_.reset.emit();
		reread();
	});
	endianWatch_ = bigEndian.changed.connect([this] { lines_.reset.emit(); });
	pickedWatch_ = picked.changed.connect([this] { titles(); });
	domainWatch_ = domain.changed.connect([this] {
		{
			const auto w = canWrite_.find(domain.get());
			writable.set(w != canWrite_.end() && w->second);
			const auto it = sizes_.find(domain.get());
			domainSize_ = it == sizes_.end() ? 0 : it->second;
		}
		lineCount.set((domainSize_ + 15) / 16);
		picked.set(-1);
		secondary_.clear();
		nibbles.set("");
		topLine.set(0);
		titles();
		reread();
	});
	opensWatch_ = session_.opens.changed.connect([this] { refreshDomains(); });
	freezesWatch_ = session_.freezesChanged.connect([this] { freezesShown(); });
	pickedFrozenWatch_ = picked.changed.connect([this] { pickedFrozen.set(picked.get() >= 0 && placeFrozen(picked.get())); });
	refreshDomains();
	titles();
}

HexEditorViewModel::~HexEditorViewModel()
{
	session_.removeMachineHooks(hooks_);
	session_.onMachineWait([](ce_session *) {});
}


int HexEditorViewModel::digits() const
{
	/* Chimera's address widths: 4 digits up to 0x10000, 6 up to 0x1000000, else 8 */
	return domainSize_ <= 0x10000 ? 4 : domainSize_ <= 0x1000000 ? 6 : 8;
}

void HexEditorViewModel::refreshDomains()
{
	session_.onMachine([this](ce_session *s) {
		std::string names;
		std::map<std::string, int64_t> sizes;
		std::map<std::string, bool> canWrite;
		if (s != nullptr)
			for (int32_t i = 0; i < ce_session_domain_count(s); i++)
			{
				if (ce_session_domain_hidden(s, i) != 0) continue;
				const std::string n = ce_session_domain_name(s, i);
				names += (names.empty() ? "" : ",") + n;
				sizes[n] = ce_session_domain_size(s, i);
				canWrite[n] = ce_session_domain_writable(s, i) != 0 && (ce_session_domain_bus(s, i) >= 0 || ce_session_domain_ptr(s, i) != 0);
			}
		later([this, names, sizes, canWrite] {
			sizes_ = sizes;
			canWrite_ = canWrite;
			domains.set(names);
			if (sizes.count(domain.get()) == 0) domain.set(names.substr(0, names.find(',')));
			{
				const auto w = canWrite.find(domain.get());
				writable.set(w != canWrite.end() && w->second);
				const auto it = sizes_.find(domain.get());
				domainSize_ = it == sizes_.end() ? 0 : it->second;
			}
			lineCount.set((domainSize_ + 15) / 16);
			titles();
			reread();
		});
	});
}

void HexEditorViewModel::reread()
{
	{
		std::lock_guard<std::mutex> lock(wantM_);
		wantDomain_ = domain.get();
		wantFrom_ = topLine.get() * 16;
		wantBytes_ = std::max<int64_t>(0, std::min(visibleLines.get() * 16, domainSize_ - wantFrom_));
	}
	session_.onMachine([this](ce_session *s) { readOnMachine(s); });
}

void HexEditorViewModel::readOnMachine(ce_session *s)
{
	if (s == nullptr) return;
	std::string name;
	int64_t from = 0, bytes = 0;
	{
		std::lock_guard<std::mutex> lock(wantM_);
		name = wantDomain_;
		from = wantFrom_;
		bytes = wantBytes_;
	}
	const int32_t i = domainIndex(s, name);
	if (i < 0 || bytes <= 0) return;
	std::vector<uint8_t> now(static_cast<size_t>(bytes));
	if (ce_session_domain_read(s, i, from, now.data(), bytes) != bytes) return;
	/* what changed since the same bytes were last read */
	std::vector<uint8_t> changed(now.size(), 0);
	if (name == lastDomainM_ && from == lastFromM_ && lastM_.size() == now.size())
		for (size_t k = 0; k < now.size(); k++) changed[k] = now[k] != lastM_[k] ? 1 : 0;
	lastM_ = now;
	lastFromM_ = from;
	lastDomainM_ = name;
	{
		std::lock_guard<std::mutex> lock(outM_);
		out_ = std::move(now);
		outChanged_ = std::move(changed);
		outFrom_ = from;
	}
	if (!postedM_.exchange(true))
		later([this] {
			postedM_ = false;
			{
				std::lock_guard<std::mutex> lock(outM_);
				shown_ = out_;
				changedShown_ = outChanged_;
				shownFrom_ = outFrom_;
			}
			const int64_t before = linesAnnounced_;
			linesAnnounced_ = lines_.rowCount();
			lines_.refreshed(before);
		});
}

bool HexEditorViewModel::readNow(int64_t from, int64_t count, std::vector<uint8_t> &out)
{
	out.clear();
	if (from < 0 || count < 0 || from + count > domainSize_) return false;
	bool ok = false;
	const std::string name = domain.get();
	session_.onMachineWait([&](ce_session *s) {
		if (s == nullptr) return;
		const int32_t i = domainIndex(s, name);
		if (i < 0) return;
		out.resize(static_cast<size_t>(count));
		ok = count == 0 || ce_session_domain_read(s, i, from, out.data(), count) == count;
	});
	return ok;
}

bool HexEditorViewModel::writeNow(int64_t at, const std::vector<uint8_t> &bytes, std::string &error)
{
	if (at < 0 || at + static_cast<int64_t>(bytes.size()) > domainSize_)
	{
		error = tr("hex.no_address", {watch::hex(at, 1)});
		return false;
	}
	/* every write goes through here: Lua and agents as well as the window */
	if (!writable.get())
	{
		error = tr("session.domain_read_only", {domain.get()});
		return false;
	}
	const std::string name = domain.get();
	std::string why;
	session_.onMachineWait([&](ce_session *s) {
		if (s == nullptr)
		{
			why = tr("session.no_machine_running");
			return;
		}
		const int32_t i = domainIndex(s, name);
		const int64_t n = static_cast<int64_t>(bytes.size());
		if (i < 0 || ce_session_domain_write(s, i, at, bytes.data(), n) != n)
		{
			why = tr("hex.not_writable", {name});
			return;
		}
		/* the states after this frame were made without it */
		session_.dropStatesAfterOnMachine(ce_session_frame(s));
		readOnMachine(s);
	});
	if (!why.empty())
	{
		error = why;
		status.set(why);
		return false;
	}
	return true;
}

uint32_t HexEditorViewModel::valueAt(int64_t address)
{
	const int sz = static_cast<int>(size.get());
	std::vector<uint8_t> b;
	if (!readNow(address, sz, b)) return 0;
	uint32_t v = 0;
	for (int k = 0; k < sz; k++) v |= static_cast<uint32_t>(b[static_cast<size_t>(k)]) << (bigEndian.get() ? (sz - 1 - k) * 8 : k * 8);
	return v;
}

bool HexEditorViewModel::writeValue(int64_t address, uint32_t value, std::string &error)
{
	const int sz = static_cast<int>(size.get());
	std::vector<uint8_t> bytes(static_cast<size_t>(sz));
	for (int k = 0; k < sz; k++) bytes[static_cast<size_t>(k)] = static_cast<uint8_t>(value >> (bigEndian.get() ? (sz - 1 - k) * 8 : k * 8));
	return writeNow(address, bytes, error);
}

bool HexEditorViewModel::placeFrozen(int64_t address) const
{
	const int sz = static_cast<int>(size.get());
	for (int k = 0; k < sz; k++)
		if (session_.frozen(domain.get(), address + k)) return true;
	return false;
}

void HexEditorViewModel::freezesShown()
{
	anyFrozen.set(session_.frozenCount.get() > 0);
	pickedFrozen.set(picked.get() >= 0 && placeFrozen(picked.get()));
	lines_.reset.emit();
}

std::vector<int64_t> HexEditorViewModel::highlightedAll() const
{
	std::vector<int64_t> out;
	if (picked.get() >= 0) out.push_back(picked.get());
	for (const int64_t s : secondary_)
		if (s != picked.get()) out.push_back(s);
	return out;
}

void HexEditorViewModel::highlight(int64_t address)
{
	const int64_t sz = size.get();
	if (address < 0) address = 0;
	if (address >= domainSize_) address = std::max<int64_t>(0, domainSize_ - sz);
	/* brought into view at the bottom, as Chimera's */
	const int64_t line = address / 16;
	if (line < topLine.get() || line >= topLine.get() + visibleLines.get())
		topLine.set(std::max<int64_t>(0, line - visibleLines.get() + 1));
	picked.set(address);
	nibbles.set("");
	titles();
	lines_.reset.emit();
}

void HexEditorViewModel::found(int64_t at, size_t length)
{
	/* the bytes found past the first value highlighted beside it, as Chimera's */
	secondary_.clear();
	const int64_t sz = size.get();
	const int64_t more = static_cast<int64_t>(length * 2) / (sz * 2) - 1;
	for (int64_t i = 0; i < more; i += sz) secondary_.insert(at + sz + i);
	highlight(at);
	status.set(tr("hex.found_at", {watch::hex(at, digits())}));
}

int64_t HexEditorViewModel::findBytes(const std::vector<uint8_t> &want, bool forward, int64_t from)
{
	const int64_t n = static_cast<int64_t>(want.size());
	if (n == 0 || domainSize_ < n) return -1;
	/* read in chunks overlapping by the pattern's length less one */
	constexpr int64_t kChunk = 1 << 20;
	if (forward)
	{
		for (int64_t at = std::max<int64_t>(0, from); at + n <= domainSize_ - 0;)
		{
			const int64_t len = std::min<int64_t>(kChunk, domainSize_ - at);
			std::vector<uint8_t> chunk;
			if (!readNow(at, len, chunk)) return -1;
			const auto hit = std::search(chunk.begin(), chunk.end(), want.begin(), want.end());
			/* to the domain's very end: Chimera stops one short, and so never
			 * finds bytes that are the domain's last */
			if (hit != chunk.end()) return at + (hit - chunk.begin());
			if (len < n || at + len >= domainSize_) break;
			at += len - n + 1;
		}
		return -1;
	}
	for (int64_t end = std::min(from, domainSize_ - n); end >= 0;)
	{
		const int64_t start = std::max<int64_t>(0, end - kChunk + 1);
		std::vector<uint8_t> chunk;
		if (!readNow(start, end - start + n, chunk)) return -1;
		const auto hit = std::find_end(chunk.begin(), chunk.end(), want.begin(), want.end());
		if (hit != chunk.end()) return start + (hit - chunk.begin());
		if (start == 0) break;
		end = start - 1;
	}
	return -1;
}

void HexEditorViewModel::titles()
{
	const int sz = static_cast<int>(size.get());
	secondaryCount.set(static_cast<int64_t>(secondary_.size()));
	if (picked.get() < 0)
		title.set("Hex Editor");
	else
	{
		std::string t = tr("hex.title_editing", {upperHex(static_cast<uint32_t>(picked.get()), digits())});
		if (!secondary_.empty())
		{
			const size_t n = secondary_.size() + (secondary_.count(picked.get()) ? 0 : 1);
			t += " " + tr("hex.title_selected", {upperHex(static_cast<uint32_t>(n), 1)});
		}
		title.set(t);
	}
	std::string count = upperHex(static_cast<uint32_t>(domainSize_ / sz), 8);
	count.erase(0, std::min(count.find_first_not_of('0'), count.size()));
	std::string box = tr(writable.get() ? "hex.box" : "hex.box_read_only", {session_.systemId.get(), domain.get(), count});
	if (!nibbles.get().empty()) box += "  " + tr("hex.typing", {nibbles.get()});
	boxTitle.set(box);
	header.set(sz == 1 ? "0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F" : sz == 2 ? "0    2    4    6    8    A    C    E" : "0        4        8        C");
}

int HexEditorViewModel::highlightKind(int64_t address) const
{
	if (address == picked.get()) return 1;
	return secondary_.count(address) != 0 ? 2 : 0;
}

char HexEditorViewModel::charOf(uint8_t b) const
{
	if (!table_.empty())
	{
		const auto it = table_.find(b);
		return it != table_.end() ? it->second : '?';
	}
	return b >= 0x20 && b < 0x7F ? static_cast<char>(b) : '.';
}

void HexEditorViewModel::addRecent(const std::string &file)
{
	/* newest first, each once, eight at most, as Chimera keeps tables */
	std::string out = file;
	std::istringstream in(recentTables.get());
	std::string l;
	int kept = 1;
	while (std::getline(in, l) && kept < 8)
		if (!l.empty() && l != file)
		{
			out += "\n" + l;
			kept++;
		}
	if (out != recentTables.get()) recentTables.set(out);
}

int64_t HexEditorViewModel::Lines::rowCount() const
{
	const int64_t left = vm_.lineCount.get() - vm_.topLine.get();
	return std::max<int64_t>(0, std::min(vm_.visibleLines.get(), left));
}

std::string HexEditorViewModel::Lines::columnName(int32_t column) const
{
	if (column == 0) return "address";
	if (column == 33) return "text";
	if (column >= 34 && column < 50) return "h" + std::to_string(column - 34);
	if (column >= 50 && column < 66) return "t" + std::to_string(column - 50);
	if (column >= 66 && column < 82) return "th" + std::to_string(column - 66);
	if (column >= 82 && column < 98) return "f" + std::to_string(column - 82);
	if (column < 0 || column > 33) return "";
	const int k = (column - 1) / 2;
	return ((column - 1) % 2 == 0 ? "v" : "c") + std::to_string(k);
}

Value HexEditorViewModel::Lines::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const int64_t address = (vm_.topLine.get() + row) * 16;
	const int64_t at = address - vm_.shownFrom_;
	const int sz = static_cast<int>(vm_.size.get());
	auto byteAt = [&](int64_t offset, uint8_t &b) {
		const int64_t idx = at + offset;
		if (idx < 0 || idx >= static_cast<int64_t>(vm_.shown_.size())) return false;
		b = vm_.shown_[static_cast<size_t>(idx)];
		return true;
	};
	if (column == 0) return watch::hex(address, vm_.digits());
	if (column == 33)
	{
		std::string text;
		for (int k = 0; k < 16; k++)
		{
			uint8_t b = 0;
			if (!byteAt(k, b)) break;
			text += vm_.charOf(b);
		}
		return text;
	}
	if (column >= 34 && column < 50)
	{
		const int place = column - 34;
		return static_cast<int64_t>(place < 16 / sz ? vm_.highlightKind(address + place * sz) : 0);
	}
	if (column >= 50 && column < 66)
	{
		uint8_t b = 0;
		return byteAt(column - 50, b) ? std::string(1, vm_.charOf(b)) : std::string();
	}
	if (column >= 66 && column < 82)
	{
		const int k = column - 66;
		return static_cast<int64_t>(vm_.highlightKind(address + (k / sz) * sz));
	}
	if (column >= 82 && column < 98)
	{
		const int place = column - 82;
		return place < 16 / sz && vm_.placeFrozen(address + place * sz);
	}
	if (column < 1 || column > 32) return {};
	const int place = (column - 1) / 2;
	if (place >= 16 / sz) return (column - 1) % 2 == 0 ? Value(std::string()) : Value(false);
	if ((column - 1) % 2 == 1)
	{
		bool any = false;
		for (int k = 0; k < sz; k++)
		{
			const int64_t idx = at + place * sz + k;
			if (idx >= 0 && idx < static_cast<int64_t>(vm_.changedShown_.size())) any = any || vm_.changedShown_[static_cast<size_t>(idx)] != 0;
		}
		return any;
	}
	uint32_t value = 0;
	for (int k = 0; k < sz; k++)
	{
		uint8_t b = 0;
		if (!byteAt(place * sz + k, b)) return std::string();
		const int shift = vm_.bigEndian.get() ? (sz - 1 - k) * 8 : k * 8;
		value |= static_cast<uint32_t>(b) << shift;
	}
	return watch::format(value, sz, watch::Display::Hex);
}

} // namespace chimera::vm
