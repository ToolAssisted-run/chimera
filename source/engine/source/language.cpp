/* language.cpp - Chimera's language tables and the ce_language_/ce_text ABI.
 * See language.hpp and docs/translations.md. */
#include "language.hpp"

#include "../../extern/cjson/cJSON.h"
#include "chimera/engine.h"
#include "file_io.hpp"
#include "host_dyn.hpp"
#include "thread_string.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <set>

namespace chimera::lang {

namespace {

std::string baseOf(const std::string &code)
{
	const size_t cut = code.find('_');
	return cut == std::string::npos ? code : code.substr(0, cut);
}

std::mutex g_lock;
std::shared_ptr<const Table> g_table;   /* the language in use; null: none read yet */
std::string g_code = "en";

std::shared_ptr<const Table> tableInUse()
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_table != nullptr) return g_table;
	}
	/* English until told otherwise, read at the first text asked for */
	auto table = std::make_shared<Table>();
	std::string error;
	readTableFile("en", *table, error);
	std::lock_guard<std::mutex> lock(g_lock);
	if (g_table == nullptr) g_table = table;
	return g_table;
}

} // namespace

const Table::Entry *Table::find(const char *key) const
{
	auto at = std::lower_bound(entries.begin(), entries.end(), key,
		[](const std::pair<std::string, Entry> &e, const char *k) { return std::strcmp(e.first.c_str(), k) < 0; });
	return at != entries.end() && at->first == key ? &at->second : nullptr;
}

bool readTable(const char *json, size_t length, Table &out, std::string &error)
{
	cJSON *root = cJSON_ParseWithLength(json, length);
	if (root == nullptr || !cJSON_IsObject(root))
	{
		cJSON_Delete(root);
		error = "not a JSON object";
		return false;
	}
	/* a core package's tables say no name: the language is Chimera's */
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
	const cJSON *english = cJSON_GetObjectItemCaseSensitive(root, "english");
	const cJSON *texts = cJSON_GetObjectItemCaseSensitive(root, "texts");
	if (!cJSON_IsObject(texts))
	{
		cJSON_Delete(root);
		error = "it needs \"texts\"";
		return false;
	}
	out.name = cJSON_IsString(name) ? name->valuestring : "";
	out.english = cJSON_IsString(english) ? english->valuestring : "";
	out.entries.clear();
	for (const cJSON *t = texts->child; t != nullptr; t = t->next)
	{
		Table::Entry entry;
		if (cJSON_IsString(t))
		{
			entry.text = t->valuestring;
		}
		else if (cJSON_IsObject(t) && t->child != nullptr)
		{
			for (const cJSON *f = t->child; f != nullptr; f = f->next)
			{
				if (!cJSON_IsString(f))
				{
					error = std::string("\"") + t->string + "\": a plural form that is not a text";
					cJSON_Delete(root);
					return false;
				}
				entry.forms.emplace_back(f->string, f->valuestring);
			}
		}
		else
		{
			error = std::string("\"") + t->string + "\" is neither a text nor plural forms";
			cJSON_Delete(root);
			return false;
		}
		out.entries.emplace_back(t->string, std::move(entry));
	}
	cJSON_Delete(root);
	std::sort(out.entries.begin(), out.entries.end(),
		[](const std::pair<std::string, Table::Entry> &a, const std::pair<std::string, Table::Entry> &b) { return a.first < b.first; });
	for (size_t i = 1; i < out.entries.size(); i++)
	{
		if (out.entries[i].first == out.entries[i - 1].first)
		{
			error = "\"" + out.entries[i].first + "\" is there twice";
			return false;
		}
	}
	return true;
}

std::string folder()
{
	/* Beside the engine. A program in a bundle's bin/ folder (chimera-run,
	 * which has the engine compiled in) finds them in lib/ beside it, as it
	 * finds the libraries: looking only beside itself, it found none, and
	 * every text it gave - a tool's answer, an error - was its key. */
	static const std::string found = [] {
		const std::string here = folderOfUs();
		for (const char *where : {"languages/", "../lib/languages/"})
		{
			std::error_code ec;
			if (std::filesystem::exists(std::filesystem::u8path(here + where + "en.json"), ec)) return here + where;
		}
		return here + "languages/";
	}();
	return found;
}

bool readTableFile(const std::string &code, Table &out, std::string &error)
{
	std::vector<uint8_t> bytes;
	const std::string path = folder() + code + ".json";
	if (!readFile(path.c_str(), bytes))
	{
		error = path + " could not be read";
		return false;
	}
	if (!readTable(reinterpret_cast<const char *>(bytes.data()), bytes.size(), out, error))
	{
		error = path + ": " + error;
		return false;
	}
	return true;
}

const std::vector<Language> &languages()
{
	static const std::vector<Language> all = [] {
		std::vector<Language> out;
		std::error_code ec;
		for (const auto &entry : std::filesystem::directory_iterator(std::filesystem::u8path(folder()), ec))
		{
			if (entry.path().extension() != ".json") continue;
			const std::string code = entry.path().stem().u8string();
			Table table;
			std::string error;
			if (readTableFile(code, table, error) && !table.name.empty()) out.push_back({code, table.name, table.english});
		}
		std::sort(out.begin(), out.end(), [](const Language &a, const Language &b) {
			if ((a.code == "en") != (b.code == "en")) return a.code == "en";
			return a.code < b.code;
		});
		return out;
	}();
	return all;
}

bool use(const std::string &code)
{
	const auto &all = languages();
	if (std::none_of(all.begin(), all.end(), [&](const Language &l) { return l.code == code; })) return false;
	auto table = std::make_shared<Table>();
	std::string error;
	if (!readTableFile(code, *table, error)) return false;
	std::lock_guard<std::mutex> lock(g_lock);
	g_table = table;
	g_code = code;
	return true;
}

std::string inUse()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_code;
}

/* CLDR's plural rules, for whole numbers, of the languages Chimera has. A
 * language added needs its rule here. */
std::vector<std::string> pluralForms(const std::string &code)
{
	const std::string base = baseOf(code);
	if (base == "ja" || base == "zh" || base == "ko" || base == "id" || base == "vi") return {"other"};
	if (base == "ru" || base == "uk" || base == "pl") return {"one", "few", "many"};
	if (base == "cs" || base == "ro") return {"one", "few", "other"};
	if (base == "en" || base == "de" || base == "es" || base == "it" || base == "nl" || base == "sv" || base == "da"
		|| base == "nb" || base == "fi" || base == "el" || base == "hu" || base == "tr" || base == "ca"
		|| base == "fr" || base == "pt")
	{
		return {"one", "other"};
	}
	return {};
}

std::string pluralForm(const std::string &code, int64_t count)
{
	const std::string base = baseOf(code);
	const uint64_t n = count < 0 ? 0 - static_cast<uint64_t>(count) : static_cast<uint64_t>(count);
	const uint64_t n10 = n % 10, n100 = n % 100;
	const std::vector<std::string> forms = pluralForms(code);
	if (forms.size() == 1) return "other";
	if (base == "ru" || base == "uk")
	{
		if (n10 == 1 && n100 != 11) return "one";
		if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return "few";
		return "many";
	}
	if (base == "pl")
	{
		if (n == 1) return "one";
		if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14)) return "few";
		return "many";
	}
	if (base == "cs")
	{
		if (n == 1) return "one";
		if (n >= 2 && n <= 4) return "few";
		return "other";
	}
	if (base == "ro")
	{
		if (n == 1) return "one";
		if (n == 0 || (n100 >= 2 && n100 <= 19)) return "few";
		return "other";
	}
	if (base == "fr" || base == "pt") return n <= 1 ? "one" : "other";
	return n == 1 ? "one" : "other";
}

namespace {

/* the %1..%9 and %n a text uses */
std::multiset<std::string> placeholders(const std::string &t)
{
	std::multiset<std::string> out;
	for (size_t i = 0; i + 1 < t.size(); i++)
	{
		if (t[i] == '%' && ((t[i + 1] >= '1' && t[i + 1] <= '9') || t[i + 1] == 'n')) out.insert(t.substr(i, 2));
	}
	return out;
}

/* a menu's shortcut letter: an & before a letter, not "&&" */
bool hasShortcut(const std::string &t)
{
	for (size_t i = 0; i + 1 < t.size(); i++)
	{
		if (t[i] != '&') continue;
		if (t[i + 1] == '&') { i++; continue; }
		if (t[i + 1] != ' ') return true;
	}
	return false;
}

/* the spaces and line breaks at either end */
std::string edges(const std::string &t)
{
	size_t a = 0, b = t.size();
	while (a < b && (t[a] == ' ' || t[a] == '\n')) a++;
	while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\n')) b--;
	return t.substr(0, a) + "|" + t.substr(b);
}

/* Proper nouns are never translated (owner, 2026-10-31): Chimera is always
 * Chimera, and the programs and services the texts name are written as they
 * are. A language whose grammar puts endings on names says the sentence with
 * a common noun beside the name ("v programu Chimera"); a particle in
 * another script, as Korean's, leaves the name whole. */
const char *const kProperNouns[] = {"Chimera", "TAStudio", "GitHub", "Lua", "Windows", "Linux", "macOS", "OpenGL", "Vulkan",
	"Direct3D", "SQLite", "Sublime Text", "miniBox", "ffmpeg", "Qt", "Wayland"};

/* the letter of the Latin alphabet, ASCII or U+00C0..U+024F in UTF-8, that
 * ends at `end` or starts at `at`: a name with one beside it is changed */
bool latinEndsAt(const std::string &t, size_t end)
{
	if (end == 0) return false;
	const unsigned char c = static_cast<unsigned char>(t[end - 1]);
	if (c < 0x80) return std::isalpha(c) != 0;
	/* back to the sequence's first byte, then the code point */
	size_t at = end - 1;
	while (at > 0 && (static_cast<unsigned char>(t[at]) & 0xC0) == 0x80) at--;
	const unsigned char lead = static_cast<unsigned char>(t[at]);
	if ((lead & 0xE0) != 0xC0 || end - at != 2) return false;
	const unsigned cp = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(t[at + 1]) & 0x3Fu);
	return cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7;
}

bool latinStartsAt(const std::string &t, size_t at)
{
	if (at >= t.size()) return false;
	const unsigned char c = static_cast<unsigned char>(t[at]);
	if (c < 0x80) return std::isalpha(c) != 0;
	if ((c & 0xE0) != 0xC0 || at + 1 >= t.size()) return false;
	const unsigned cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(t[at + 1]) & 0x3Fu);
	return cp >= 0xC0 && cp <= 0x24F && cp != 0xD7 && cp != 0xF7;
}

/* a file's name or a path ("*.chimeraProject", "chimera.lua"): no name in a sentence */
bool inFileName(const std::string &t, size_t at)
{
	size_t a = at, b = at;
	while (a > 0 && t[a - 1] != ' ' && t[a - 1] != '\n') a--;
	while (b < t.size() && t[b] != ' ' && t[b] != '\n') b++;
	for (size_t i = a; i < b; i++)
		if (t[i] == '/' || t[i] == '\\' || t[i] == '*' ||
			(t[i] == '.' && i + 1 < b && std::isalnum(static_cast<unsigned char>(t[i + 1])) != 0))
			return true;
	return false;
}

/* the name in the text, whole: true when it is there with no letter glued
 * to it; `glued` set when a word starts with it and goes on (an ending put
 * on it). Inside another word it is no name ("ares" in "auriculares") */
bool holdsName(const std::string &t, const std::string &name, bool &glued)
{
	bool whole = false;
	glued = false;
	for (size_t at = t.find(name); at != std::string::npos; at = t.find(name, at + 1))
	{
		if (latinEndsAt(t, at)) continue;
		if (!latinStartsAt(t, at + name.size())) whole = true;
		else if (!inFileName(t, at)) glued = true;
	}
	return whole;
}

} // namespace

std::vector<std::string> compare(const Table &english, const Table &other, const std::string &code, const std::vector<std::string> &names)
{
	std::vector<std::string> out;
	auto problem = [&](const std::string &key, const std::string &what) { out.push_back(code + ": " + key + ": " + what); };
	const std::vector<std::string> forms = pluralForms(code);
	std::vector<std::string> nouns(std::begin(kProperNouns), std::end(kProperNouns));
	nouns.insert(nouns.end(), names.begin(), names.end());
	if (forms.empty()) problem("(the table)", "no plural rule for this language (language.cpp)");
	for (const auto &e : english.entries)
	{
		const Table::Entry *t = other.find(e.first.c_str());
		if (t == nullptr) { problem(e.first, "missing"); continue; }
		const std::string en = e.second.forms.empty() ? e.second.text : e.second.forms.back().second;
		if (e.second.forms.empty() != t->forms.empty()) { problem(e.first, "plural forms where English has none, or none where it has"); continue; }
		std::vector<std::string> texts;
		if (t->forms.empty()) texts.push_back(t->text);
		else
		{
			std::vector<std::string> named;
			for (const auto &f : t->forms) { named.push_back(f.first); texts.push_back(f.second); }
			if (named != forms) problem(e.first, "plural forms are not the language's (" + std::to_string(forms.size()) + ", in CLDR's order)");
		}
		for (const std::string &x : texts)
		{
			std::multiset<std::string> want = placeholders(en), have = placeholders(x);
			/* one form of several may write its number as a word ("one") */
			if (!e.second.forms.empty()) { want.erase("%n"); have.erase("%n"); }
			if (want != have) problem(e.first, "placeholders differ from English: " + x);
			if (hasShortcut(en) && !hasShortcut(x)) problem(e.first, "no shortcut letter (&): " + x);
			if (edges(en) != edges(x)) problem(e.first, "spaces or line breaks at the ends differ from English");
			/* a proper noun English names is there as itself, and none has letters glued to it */
			for (const std::string &n : nouns)
			{
				bool enGlued = false, glued = false;
				const bool inEnglish = holdsName(en, n, enGlued), whole = holdsName(x, n, glued);
				if (inEnglish && !whole) problem(e.first, n + " is not there as itself: " + x);
				/* a short name starts words of other languages (Indonesian "Luapan"): only where English names it */
				else if (glued && (inEnglish || n.size() > 3)) problem(e.first, n + " has letters glued to it: " + x);
			}
		}
	}
	for (const auto &e : other.entries)
	{
		if (english.find(e.first.c_str()) == nullptr) problem(e.first, "not an English key");
	}
	return out;
}

namespace {

/* The fields of a package's declarations a person reads: each holds a key of
 * its language tables. A path is the field's place, [] any element of a list
 * and * any member of an object; control names, option values, machine
 * labels and the like are names, not texts, and stay as they are. */
const char *const kTextFields[] = {
	"waterbox.config:settings[].display", "waterbox.config:settings[].description", "waterbox.config:settings[].optionLabels.*",
	"waterbox.config:firmware[].display", "waterbox.config:firmware[].description", "waterbox.config:firmware[].label",
	"waterbox.config:presets[].label", "waterbox.config:presets[].description",
	"waterbox.config:tools[].menu", "waterbox.config:tools[].description",
	"waterbox.config:tools[].inputs[].label", "waterbox.config:tools[].outputs[].label",
	"waterbox.config:tools[].options[].display", "waterbox.config:tools[].options[].description",
	"waterbox.config:tools[].options[].optionLabels.*",
	"file_slots.json:slots[].title", "file_slots.json:slots[].help", "file_slots.json:slots[].nameHelp",
};

/* the strings at a path ("settings[].display") under a node */
void stringsAt(const cJSON *node, const std::string &path, std::vector<std::string> &out)
{
	if (path.empty())
	{
		if (cJSON_IsString(node)) out.push_back(node->valuestring);
		return;
	}
	const size_t cut = path.find_first_of(".[");
	const std::string head = path.substr(0, cut);
	std::string rest = cut == std::string::npos ? std::string() : path.substr(cut);
	if (head == "*")
	{
		if (!rest.empty() && rest[0] == '.') rest.erase(0, 1);
		for (const cJSON *c = node->child; c != nullptr; c = c->next) stringsAt(c, rest, out);
		return;
	}
	const cJSON *child = cJSON_GetObjectItemCaseSensitive(node, head.c_str());
	if (child == nullptr) return;
	if (rest.rfind("[]", 0) == 0)
	{
		rest.erase(0, 2);
		if (!rest.empty() && rest[0] == '.') rest.erase(0, 1);
		if (!cJSON_IsArray(child)) return;
		for (const cJSON *c = child->child; c != nullptr; c = c->next) stringsAt(c, rest, out);
		return;
	}
	if (!rest.empty() && rest[0] == '.') rest.erase(0, 1);
	stringsAt(child, rest, out);
}

} // namespace

std::vector<std::string> packageProblems(const std::function<bool(const std::string &, std::string &)> &entry)
{
	std::vector<std::string> lines;
	auto table = [&](const std::string &code, Table &out) {
		std::string bytes, error;
		const std::string name = "languages/" + code + ".json";
		if (!entry(name, bytes)) { lines.push_back(name + " is not in the package"); return false; }
		if (!readTable(bytes.data(), bytes.size(), out, error)) { lines.push_back(name + ": " + error); return false; }
		return true;
	};
	Table english;
	if (!table("en", english)) return lines;
	/* the core's own name is a proper noun too */
	std::vector<std::string> names;
	if (std::string config; entry("waterbox.config", config))
		if (cJSON *root = cJSON_ParseWithLength(config.data(), config.size()))
		{
			if (const cJSON *n = cJSON_GetObjectItemCaseSensitive(root, "coreName"); cJSON_IsString(n) && n->valuestring[0] != '\0')
				names.emplace_back(n->valuestring);
			cJSON_Delete(root);
		}
	for (const Language &l : languages())
	{
		Table other;
		if (l.code == "en" || !table(l.code, other)) continue;
		for (const std::string &line : compare(english, other, l.code, names)) lines.push_back(line);
	}
	/* the declarations' texts: keys, never words written in */
	for (const char *field : kTextFields)
	{
		const std::string spec(field);
		const size_t colon = spec.find(':');
		const std::string file = spec.substr(0, colon);
		std::string bytes;
		if (!entry(file, bytes)) continue;
		cJSON *root = cJSON_ParseWithLength(bytes.data(), bytes.size());
		if (root == nullptr) continue;
		std::vector<std::string> values;
		stringsAt(root, spec.substr(colon + 1), values);
		cJSON_Delete(root);
		for (const std::string &v : values)
		{
			const std::string key = v.substr(0, v.find('\x1f'));
			const Table::Entry *e = english.find(key.c_str());
			if (e == nullptr)
			{
				lines.push_back(file + ": " + spec.substr(colon + 1) + " is not a key of languages/en.json: \"" + key + "\"");
				continue;
			}
			/* each %1..%9 its text uses is given */
			const size_t given = static_cast<size_t>(std::count(v.begin(), v.end(), '\x1f'));
			for (const std::string &p : placeholders(e->forms.empty() ? e->text : e->forms.back().second))
				if (p[1] != 'n' && static_cast<size_t>(p[1] - '0') > given)
				{
					lines.push_back(file + ": " + spec.substr(colon + 1) + " \"" + key + "\" uses " + p + " and is given "
						+ std::to_string(given) + " argument(s)");
					break;
				}
		}
	}
	return lines;
}

std::string fill(const std::string &pattern, const std::vector<std::string> &args)
{
	std::string out;
	out.reserve(pattern.size());
	for (size_t i = 0; i < pattern.size(); i++)
	{
		const char c = pattern[i];
		if (c == '%' && i + 1 < pattern.size() && pattern[i + 1] >= '1' && pattern[i + 1] <= '9')
		{
			const size_t index = static_cast<size_t>(pattern[i + 1] - '1');
			if (index < args.size())
			{
				out += args[index];
				i++;
				continue;
			}
		}
		out += c;
	}
	return out;
}

std::string tr(const char *key, std::initializer_list<std::string> args)
{
	return tr(key, -1, args);
}

namespace {

/* an entry's text for a count, its %n replaced; below zero: the "other" form */
std::string patternFor(const Table::Entry &entry, const std::string &code, int64_t count)
{
	if (entry.forms.empty()) return entry.text;
	const std::string form = count < 0 ? std::string("other") : pluralForm(code, count);
	std::string pattern = entry.forms.back().second;
	for (const auto &f : entry.forms)
	{
		if (f.first == form) { pattern = f.second; break; }
	}
	const std::string number = std::to_string(count < 0 ? 0 : count);
	for (size_t at = pattern.find("%n"); at != std::string::npos; at = pattern.find("%n", at + number.size()))
	{
		pattern.replace(at, 2, number);
	}
	return pattern;
}

} // namespace

std::string tr(const char *key, int64_t count, std::initializer_list<std::string> args)
{
	const std::shared_ptr<const Table> table = tableInUse();
	const Table::Entry *entry = table->find(key);
	if (entry == nullptr) return key;
	return fill(patternFor(*entry, inUse(), count), std::vector<std::string>(args));
}

void PackageTexts::read(const std::function<bool(const std::string &, std::string &)> &entry)
{
	std::lock_guard<std::mutex> hold(lock_);
	raw_.clear();
	code_.clear();
	englishRead_ = false;
	others_.clear();
	for (const Language &l : languages())
	{
		std::string bytes;
		if (entry("languages/" + l.code + ".json", bytes)) raw_[l.code] = std::move(bytes);
	}
}

std::string PackageTexts::text(const std::string &message) const
{
	std::lock_guard<std::mutex> hold(lock_);
	return textLocked(message);
}

std::string PackageTexts::textLocked(const std::string &message) const
{
	if (raw_.empty()) return message;
	auto readOne = [this](const std::string &code, Table &out) {
		out = Table();
		auto it = raw_.find(code);
		std::string error;
		if (it != raw_.end()) readTable(it->second.data(), it->second.size(), out, error);
	};
	const std::string code = inUse();
	if (code_ != code)
	{
		code_ = code;
		readOne(code, table_);
	}
	if (!englishRead_)
	{
		englishRead_ = true;
		readOne("en", english_);
	}
	/* several texts, each after a 0x1D, are each a line of their own */
	if (message.find('\x1d') != std::string::npos)
	{
		std::string out;
		for (size_t at = 0;;)
		{
			const size_t cut = message.find('\x1d', at);
			const std::string one = message.substr(at, cut == std::string::npos ? std::string::npos : cut - at);
			if (!one.empty()) out += (out.empty() ? "" : "\n") + textLocked(one);
			if (cut == std::string::npos) break;
			at = cut + 1;
		}
		return out;
	}
	/* a reason meant to sit inside another sentence separates its own
	 * arguments with 0x1E; shown on its own it is a text like any other */
	const bool inner = message.find('\x1f') == std::string::npos && message.find('\x1e') != std::string::npos;
	return resolveLocked(message, inner ? '\x1e' : '\x1f', code);
}

std::string PackageTexts::resolveLocked(const std::string &message, char separator, const std::string &code) const
{
	std::vector<std::string> parts;
	for (size_t at = 0;;)
	{
		const size_t cut = message.find(separator, at);
		parts.push_back(message.substr(at, cut == std::string::npos ? std::string::npos : cut - at));
		if (cut == std::string::npos) break;
		at = cut + 1;
	}
	const std::string key = parts.front();
	parts.erase(parts.begin());
	const Table::Entry *entry = table_.find(key.c_str());
	std::string language = code;
	if (entry == nullptr)
	{
		entry = english_.find(key.c_str());
		language = "en";
	}
	/* A package without the language in use, or without English either, is
	 * not refused: any table it has will do (owner, 2026-10-29). */
	for (const Language &l : languages())
	{
		if (entry != nullptr) break;
		if (l.code == code || l.code == "en") continue;
		auto raw = raw_.find(l.code);
		if (raw == raw_.end()) continue;
		auto [other, fresh] = others_.try_emplace(l.code);
		if (fresh)
		{
			std::string error;
			readTable(raw->second.data(), raw->second.size(), other->second, error);
		}
		entry = other->second.find(key.c_str());
		language = l.code;
	}
	if (entry == nullptr) return message;
	/* an argument that is a key itself, with its own arguments after 0x1E
	 * (an emulator's reason put inside the core's sentence), is its text */
	if (separator == '\x1f')
		for (std::string &part : parts) part = resolveLocked(part, '\x1e', code);
	int64_t count = -1;
	if (!entry->forms.empty() && !parts.empty())
	{
		char *end = nullptr;
		const long long n = std::strtoll(parts.front().c_str(), &end, 10);
		if (end != parts.front().c_str() && *end == '\0' && n >= 0) count = n;
	}
	return fill(patternFor(*entry, language, count), parts);
}

namespace {

void appendUtf8(std::string &out, uint32_t c)
{
	if (c < 0x80) out += char(c);
	else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
	else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
	else
	{
		out += char(0xF0 | (c >> 18));
		out += char(0x80 | ((c >> 12) & 0x3F));
		out += char(0x80 | ((c >> 6) & 0x3F));
		out += char(0x80 | (c & 0x3F));
	}
}

/* a JSON string's contents, its escapes undone; false for a broken one */
bool unescape(const std::string &json, size_t from, size_t to, std::string &out)
{
	out.clear();
	auto hex4 = [&json](size_t at, uint32_t &v) {
		v = 0;
		for (size_t k = at; k < at + 4; k++)
		{
			const char c = json[k];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= uint32_t(c - '0');
			else if (c >= 'a' && c <= 'f') v |= uint32_t(c - 'a' + 10);
			else if (c >= 'A' && c <= 'F') v |= uint32_t(c - 'A' + 10);
			else return false;
		}
		return true;
	};
	for (size_t i = from; i < to; i++)
	{
		if (json[i] != '\\') { out += json[i]; continue; }
		if (++i >= to) return false;
		switch (json[i])
		{
		case 'b': out += '\b'; break;
		case 'f': out += '\f'; break;
		case 'n': out += '\n'; break;
		case 'r': out += '\r'; break;
		case 't': out += '\t'; break;
		case 'u':
		{
			uint32_t c = 0;
			if (i + 4 >= to) return false;
			if (!hex4(i + 1, c)) return false;
			i += 4;
			if (c >= 0xD800 && c < 0xDC00 && i + 6 < to && json[i + 1] == '\\' && json[i + 2] == 'u')
			{
				uint32_t low = 0;
				if (hex4(i + 3, low) && low >= 0xDC00 && low < 0xE000)
				{
					c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
					i += 6;
				}
			}
			appendUtf8(out, c);
			break;
		}
		default: out += json[i]; break;   /* \" \\ \/ */
		}
	}
	return true;
}

std::string escape(const std::string &text)
{
	std::string out = "\"";
	for (const char c : text)
	{
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if (c == '\n') out += "\\n";
		else if (c == '\t') out += "\\t";
		else if (static_cast<unsigned char>(c) < 0x20)
		{
			char code[8];
			std::snprintf(code, sizeof code, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
			out += code;
		}
		else out += c;
	}
	return out + "\"";
}

} // namespace

std::string PackageTexts::texts(const std::string &json) const
{
	size_t first = json.find_first_not_of(" \t\r\n");
	if (first == std::string::npos || (json[first] != '{' && json[first] != '['))
		return text(json);
	std::lock_guard<std::mutex> hold(lock_);
	if (raw_.empty()) return json;
	std::string out;
	out.reserve(json.size());
	size_t copied = 0;
	std::string value;
	for (size_t i = first; i < json.size(); i++)
	{
		if (json[i] != '"') continue;
		const size_t open = i;
		for (i++; i < json.size() && json[i] != '"'; i++)
			if (json[i] == '\\') i++;
		if (i >= json.size()) break;   /* not JSON after all: what was done stands */
		const size_t close = i;
		/* a member's name is not a text */
		size_t next = json.find_first_not_of(" \t\r\n", close + 1);
		if (next != std::string::npos && json[next] == ':') continue;
		if (!unescape(json, open + 1, close, value)) continue;
		const std::string now = textLocked(value);
		if (now == value) continue;
		out.append(json, copied, open - copied);
		out += escape(now);
		copied = close + 1;
	}
	out.append(json, copied, std::string::npos);
	return out;
}

} // namespace chimera::lang

/* ---- the ABI (engine.h) ---- */

namespace {
thread_local chimera::ThreadString g_text;   /* never destroyed: see thread_string.hpp */
thread_local chimera::ThreadString g_code;   /* likewise */
}

int32_t ce_language_count(void)
{
	return static_cast<int32_t>(chimera::lang::languages().size());
}

const char *ce_language_code(int32_t index)
{
	const auto &all = chimera::lang::languages();
	return index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)].code.c_str() : "";
}

const char *ce_language_name(int32_t index)
{
	const auto &all = chimera::lang::languages();
	return index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)].name.c_str() : "";
}

const char *ce_language_english_name(int32_t index)
{
	const auto &all = chimera::lang::languages();
	return index >= 0 && static_cast<size_t>(index) < all.size() ? all[static_cast<size_t>(index)].english.c_str() : "";
}

int32_t ce_language_use(const char *code)
{
	return code != nullptr && chimera::lang::use(code) ? 1 : 0;
}

const char *ce_language_in_use(void)
{
	*g_code = chimera::lang::inUse();
	return g_code->c_str();
}

const char *ce_text(const char *key, int64_t count, const char *const *args, int32_t arg_count)
{
	if (key == nullptr) key = "";
	std::vector<std::string> filled;
	for (int32_t i = 0; args != nullptr && i < arg_count && i < 9; i++) filled.emplace_back(args[i] != nullptr ? args[i] : "");
	/* the pattern first, then the arguments: an argument's own %1 is not filled */
	const std::string pattern = chimera::lang::tr(key, count);
	*g_text = chimera::lang::fill(pattern, filled);
	return g_text->c_str();
}
