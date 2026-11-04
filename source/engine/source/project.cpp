/* project.cpp - the .chimeraProject: chimera's entry point and its movie in
 * one file (docs/project.md).
 *
 * A zip (owner, 2026-11-02) of project.json, the input's lines (input.input)
 * and each branch's (branches/<n>.input). Together they hold everything
 * required to reproduce the work except
 * the data bytes, which are named by SHA1: identity, the pinned core, the
 * file manifest (canonical names + SHA1 + the core-defined slot each file
 * fills), firmware pins, sync settings, and the TAS work itself - input
 * log, markers, branches. No paths are ever stored; files are resolved per
 * session from wherever the caller says they are, and saving records the
 * ACTUAL hash of every resolved file so an override leaves a truthful
 * record.
 */

#ifdef _WIN32
#define _CRT_RAND_S /* rand_s, from the system's random source */
#endif
#include "chimera/engine.h"

#include "file_io.hpp"
#include "manifest_util.hpp"
#include "conditions.hpp"
#include "project_check.hpp"
#include "sha1.hpp"
#include "thread_string.hpp"
#include "language.hpp"

#include "../../extern/cjson/cJSON.h"
#include "../../extern/miniz/miniz.h"

using chimera::lang::tr;

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <initializer_list>
#include <map>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <vector>

#ifndef _WIN32
#ifdef __APPLE__
#include <stdlib.h>   /* arc4random_buf */
#else
#include <sys/random.h>
#endif
#endif

namespace {

using namespace chimera::manifest;

thread_local chimera::ThreadString g_error;   /* never destroyed: see thread_string.hpp */

struct FileEntry
{
	std::string name;       // canonical, bare
	std::string slot;       // core-defined slot id ("support" reserved)
	std::string sha1;       // recorded (40 uppercase hex)
	std::string actualSha1; // "" while unresolved
	int32_t status = 1;     // 0 resolved+match, 1 unresolved, 2 mismatch
	/* Where these bytes were read from, on THIS machine, in THIS run. Never
	 * serialized: the project is distributable and a path is not. It exists so
	 * a frontend can remember locations in a sidecar of its own. */
	std::string sourcePath;
	/* NOT the bytes. A project's files are read where they lie, when the
	 * machine asks for them (by path, ce_session_open_project): a PS2 disc is
	 * over four gigabytes and holding one here would be four gigabytes nothing
	 * needed, on the way to a sandbox that reads it from the disk anyway. What
	 * a project knows about a file is its name, its hash and where it is. */
	uint64_t size = 0;
};

/* A cue is a few hundred bytes of text naming its tracks, so it IS read - the
 * point of not holding files is the big ones. Empty when it will not read,
 * which the callers all treat as "no references". */
std::vector<uint8_t> readSmall(const std::string &path)
{
	std::vector<uint8_t> out;
	if (!path.empty()) chimera::readFile(path.c_str(), out);
	return out;
}

struct Marker
{
	int64_t frame = 0;
	std::string text;
	bool keepState = true; // the user's keep-a-state-here choice; serialized only when false
};

struct Branch
{
	std::string name;
	int64_t frame = 0;
	std::string time; // carried verbatim (the frontend's timestamp text); "" = none
	std::string log;
	std::vector<Marker> markers;
};

const char *fail(std::string message, const char **error_out)
{
	*g_error = std::move(message);
	if (error_out != nullptr) *error_out = g_error->c_str();
	return nullptr;
}

int32_t failInt(std::string message, const char **error_out)
{
	*g_error = std::move(message);
	if (error_out != nullptr) *error_out = g_error->c_str();
	return 1;
}

std::string lowerExt(const std::string &name)
{
	size_t dot = name.find_last_of('.');
	if (dot == std::string::npos || dot + 1 == name.size()) return std::string();
	std::string ext = name.substr(dot + 1);
	for (char &c : ext)
	{
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	}
	return ext;
}

} // namespace

/* "clockStart": a second written as UTC, 2017-05-27T12:44:28Z (issue 366) */
static constexpr int64_t kClockStartLast = 7258118400; /* 2200-01-01, miniBox's latest */

static int64_t daysFromCivil(int64_t y, unsigned m, unsigned d)
{
	y -= m <= 2;
	const int64_t era = (y >= 0 ? y : y - 399) / 400;
	const unsigned yoe = static_cast<unsigned>(y - era * 400);
	const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

static bool parseUtcSecond(const char *text, int64_t &out)
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
	char z = 0, tail = 0;
	if (text == nullptr || std::strlen(text) != 20
		|| std::sscanf(text, "%4d-%2d-%2dT%2d:%2d:%2d%c%c", &y, &mo, &d, &h, &mi, &se, &z, &tail) != 7 || z != 'Z')
		return false;
	static const int monthDays[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (y < 1970 || mo < 1 || mo > 12 || d < 1 || d > monthDays[mo - 1] || h > 23 || mi > 59 || se > 59) return false;
	const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
	if (mo == 2 && d == 29 && !leap) return false;
	const int64_t seconds = daysFromCivil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + se;
	if (seconds < 0 || seconds > kClockStartLast) return false;
	out = seconds == CE_CLOCK_START_DEFAULT ? -1 : seconds;
	return true;
}

static std::string utcSecondText(int64_t seconds)
{
	int64_t days = seconds / 86400, rest = seconds % 86400;
	/* civil from days (Howard Hinnant's algorithm) */
	days += 719468;
	const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
	const unsigned doe = static_cast<unsigned>(days - era * 146097);
	const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	const unsigned mp = (5 * doy + 2) / 153;
	const unsigned d = doy - (153 * mp + 2) / 5 + 1;
	const unsigned m = mp < 10 ? mp + 3 : mp - 9;
	const int64_t y = static_cast<int64_t>(yoe) + era * 400 + (m <= 2);
	char buf[32];
	std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02lld:%02lld:%02lldZ", (long long)y, m, d,
		(long long)(rest / 3600), (long long)(rest / 60 % 60), (long long)(rest % 60));
	return buf;
}

struct ce_project
{
	/* This project, and no other. The frontend keeps everything regenerable -
	 * the state history, and where this machine last found the project's files
	 * - in a per-user cache keyed by it, so the .chimeraProject stays the one
	 * file that travels (docs/state-manager.md). It has to survive the file
	 * being renamed, moved, or synced to another machine, which rules out the
	 * path; and two attempts at the same game must not share a cache, which
	 * rules out anything derived from the contents. So it is minted once and
	 * carried. */
	std::string id;
	std::string title;
	std::string coreName, coreVersion, coreSha1;
	uint64_t rerecords = 0;
	int64_t clockStart = -1;   // the second the machine's clock starts at; -1: the sandbox's own
	cJSON *settings = nullptr; // always an object
	cJSON *firmware = nullptr; // always an array
	cJSON *coreCache = nullptr; // always an array: {name, sha1} per compiled object
	// TAStudio's own layout of the piano roll - columns, order, widths,
	// orientation, lag display - kept verbatim for the frontend that wrote it,
	// or null when it wrote none (issue #83). Not a sync setting.
	cJSON *tastudio = nullptr;
	std::string log;           // the ce_movie_log lump (LogKey + entries)
	std::vector<FileEntry> files;
	std::vector<Marker> markers;
	std::vector<Branch> branches;
	// movie metadata this format does not first-class (Author, emulator
	// version, platform facts): ordered key/value pairs, carried verbatim
	std::vector<std::pair<std::string, std::string>> headers;

	// borrowed-buffer returns
	std::string settingsOut, firmwareOut, coreCacheOut, slotsOut, tastudioOut;

	ce_project()
	{
		settings = cJSON_CreateObject();
		firmware = cJSON_CreateArray();
		coreCache = cJSON_CreateArray();
	}
	~ce_project()
	{
		cJSON_Delete(settings);
		cJSON_Delete(firmware);
		cJSON_Delete(coreCache);
		cJSON_Delete(tastudio);
	}
};

extern "C" {

/* An identity for one project. It need only be unique - nothing reads meaning
 * from it - but it is the key of the project's cache, greenzone, branch states
 * and recovery folder, so two must never meet. It is 64 bits from the system's
 * random source; where that gives nothing, where this process sits in memory,
 * the clock and a count. That alone was not enough on Windows, which places a
 * DLL at one address for every process until a reboot: two Chimeras making a
 * project in the same second made the same id. It is metadata and touches no
 * machine state, so there is nothing here for determinism to mind. */
static uint64_t systemRandom64()
{
#ifdef _WIN32
	unsigned int lo = 0, hi = 0;
	if (rand_s(&lo) != 0 || rand_s(&hi) != 0) return 0;
	return (static_cast<uint64_t>(hi) << 32) | lo;
#else
	uint64_t v = 0;
#ifdef __APPLE__
	arc4random_buf(&v, sizeof v);   /* the kernel's generator; it cannot fail */
	return v;
#else
	return getrandom(&v, sizeof v, 0) == static_cast<ssize_t>(sizeof v) ? v : 0;
#endif
#endif
}

/* The name of a member `obj` has that is not one of `known`, or nullptr. A
 * key this build does not know is an error at every level, as it always was
 * at the top: inside a file, a marker, a branch or the core it was accepted
 * and then lost on the next save (engine analysis, 2026-10-18). */
static const char *unknownKey(const cJSON *obj, std::initializer_list<const char *> known)
{
	for (const cJSON *item = obj != nullptr ? obj->child : nullptr; item != nullptr; item = item->next)
	{
		bool ok = false;
		for (const char *k : known)
			if (item->string != nullptr && std::strcmp(item->string, k) == 0) { ok = true; break; }
		if (!ok) return item->string != nullptr ? item->string : "";
	}
	return nullptr;
}

static std::string mintProjectId()
{
	static uint64_t made;
	uint64_t bits = systemRandom64();
	if (bits == 0)
		bits = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&made)) << 16)
			^ (static_cast<uint64_t>(std::time(nullptr)) << 8)
			^ (++made);
	if (bits == 0) bits = 1;
	char buf[17];
	std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(bits));
	return std::string(buf);
}

ce_project *ce_project_new(void)
{
	auto *p = new ce_project();
	p->id = mintProjectId();
	return p;
}

void ce_project_free(ce_project *p) { delete p; }

/* A movie's input as the project holds it (owner, 2026-10-28 and
 * 2026-11-02): its columns in project.json, and its entries, a frame a line,
 * in a file of their own beside it (input.input, a branch's
 * branches/<n>.input):
 *
 *   project.json   "inputs": { "columns": [ ["Reset", "Power"], ["P1 Up", ...] ] }
 *   input.input    |..|........|
 *                  |.P|U.......|
 *
 * The lines are what TAStudio's Copy puts on the clipboard: nothing before
 * or after them, no quotes, no commas between them. "columns" is the key's
 * groups in order, each a list of its columns' names - the groups an entry's
 * '|' part. In memory the project keeps the text block, which every reader
 * of a movie takes; it is turned into this and back only here.
 *
 * The key is split where a '#' begins a name (a group starts) and at each
 * '|' (a name ends), and joining the groups back gives the key byte for
 * byte whatever the names hold: a machine's "P1 #" or "| @" is read from the
 * key with the machine's own names, as before (EntryLayout::buildFromKey),
 * and only looks split here. A key that does not come back the same is kept
 * whole, as "key". A movie with no key has no "inputs". */
static bool splitKey(const std::string &key, std::vector<std::vector<std::string>> &groups)
{
	groups.clear();
	size_t at = 0;
	while (at < key.size())
	{
		if (key[at] != '#') return false;
		groups.emplace_back();
		at++;
		while (at < key.size() && key[at] != '#')
		{
			const size_t bar = key.find('|', at);
			if (bar == std::string::npos) return false;
			groups.back().push_back(key.substr(at, bar - at));
			at = bar + 1;
		}
	}
	return true;
}

static std::string joinKey(const std::vector<std::vector<std::string>> &groups)
{
	std::string key;
	for (const auto &g : groups)
	{
		key += '#';
		for (const std::string &name : g) key += name + "|";
	}
	return key;
}

/* a string as JSON writes it, quoted and escaped */
static std::string jsonString(const std::string &text)
{
	cJSON *s = cJSON_CreateString(text.c_str());
	char *printed = cJSON_PrintUnformatted(s);
	std::string out = printed != nullptr ? printed : "\"\"";
	cJSON_free(printed);
	cJSON_Delete(s);
	return out;
}

/* a list one item a line, at the depth cJSON_Print indents the key it is the
 * value of (a group of columns a line reads, and diffs, as one) */
static cJSON *listOnLines(const std::vector<std::string> &items, int depth)
{
	if (items.empty()) return cJSON_CreateArray();
	std::string text = "[\n";
	for (size_t i = 0; i < items.size(); i++)
		text += std::string(static_cast<size_t>(depth) + 1, '\t') + items[i] + (i + 1 < items.size() ? ",\n" : "\n");
	text += std::string(static_cast<size_t>(depth), '\t') + "]";
	return cJSON_CreateRaw(text.c_str());
}

/* the text block as "inputs" (null when the movie names no columns) and its
 * entries as the lines of an .input file */
static cJSON *inputsOf(const std::string &lump, int depth, std::string &lines)
{
	lines.clear();
	if (lump.empty()) return nullptr;
	ce_movie_log *log = ce_movie_log_new();
	ce_movie_log_parse(log, lump.data(), lump.size());
	cJSON *out = nullptr;
	if (const char *key = ce_movie_log_key(log))
	{
		out = cJSON_CreateObject();
		std::vector<std::vector<std::string>> groups;
		if (splitKey(key, groups) && joinKey(groups) == key)
		{
			std::vector<std::string> names;
			for (const auto &g : groups)
			{
				std::string line = "[";
				for (size_t i = 0; i < g.size(); i++) line += (i ? ", " : "") + jsonString(g[i]);
				names.push_back(line + "]");
			}
			cJSON_AddItemToObject(out, "columns", listOnLines(names, depth + 1));
		}
		else cJSON_AddStringToObject(out, "key", key);
	}
	for (int64_t i = 0; i < ce_movie_log_count(log); i++) lines += std::string(ce_movie_log_entry(log, i)) + "\n";
	ce_movie_log_free(log);
	return out;
}

/* "inputs" (null for none) and the lines of its .input file (named so a
 * refusal can say which) back to the text block; false with why for a
 * project that does not hold one: the key of a sentence and its arguments */
static bool lumpOf(const cJSON *inputs, const std::string &lines, const std::string &field, const std::string &file,
	std::string &lump, std::string &why, std::vector<std::string> &args)
{
	lump.clear();
	args = { field };
	if (inputs == nullptr && lines.empty()) return true;
	lump = "[Input]\n";
	if (inputs != nullptr)
	{
		if (!cJSON_IsObject(inputs))
		{
			why = "project.inputs_not_object";
			return false;
		}
		if (const char *k = unknownKey(inputs, { "columns", "key" }))
		{
			why = "project.inputs_unknown_key";
			args.push_back(k);
			return false;
		}
		const cJSON *columns = cJSON_GetObjectItemCaseSensitive(inputs, "columns");
		const cJSON *key = cJSON_GetObjectItemCaseSensitive(inputs, "key");
		if (columns != nullptr && key != nullptr)
		{
			why = "project.inputs_columns_and_key";
			return false;
		}
		if (columns != nullptr)
		{
			std::vector<std::vector<std::string>> groups;
			if (!cJSON_IsArray(columns))
			{
				why = "project.inputs_columns_not_list";
				return false;
			}
			for (const cJSON *g = columns->child; g != nullptr; g = g->next)
			{
				if (!cJSON_IsArray(g))
				{
					why = "project.inputs_group_not_list";
					return false;
				}
				groups.emplace_back();
				for (const cJSON *n = g->child; n != nullptr; n = n->next)
				{
					if (!cJSON_IsString(n))
					{
						why = "project.inputs_name_not_string";
						return false;
					}
					groups.back().push_back(n->valuestring);
				}
			}
			lump += "LogKey:" + joinKey(groups) + "\n";
		}
		else if (key != nullptr)
		{
			if (!cJSON_IsString(key))
			{
				why = "project.inputs_key_not_string";
				return false;
			}
			lump += std::string("LogKey:") + key->valuestring + "\n";
		}
	}
	/* a line each, as Copy writes them; a line ended by "\r\n" (the file
	 * edited on Windows) is the same line */
	size_t at = 0;
	int64_t number = 0;
	while (at < lines.size())
	{
		size_t end = lines.find('\n', at);
		if (end == std::string::npos) end = lines.size();
		std::string line = lines.substr(at, end - at);
		at = end + 1;
		number++;
		if (!line.empty() && line.back() == '\r') line.pop_back();
		/* a line that is not an entry would be dropped when the movie is read */
		if (line.empty() || line[0] != '|')
		{
			why = "project.input_line_not_input";
			args = { file, std::to_string(number) };
			return false;
		}
		lump += line + "\n";
	}
	lump += "[/Input]\n";
	return true;
}

/* The project's parts, as its zip holds them (owner, 2026-11-02): project.json,
 * the main input's lines (input.input) and each branch's (branches/<n>.input,
 * numbered from 1 in the branches' order). */
struct Parts
{
	std::string json, input;
	std::map<int, std::string> branches;
};

/* a sentence from its key and up to two arguments */
static std::string trArgs(const std::string &key, const std::vector<std::string> &args)
{
	if (args.empty()) return tr(key.c_str());
	if (args.size() == 1) return tr(key.c_str(), {args[0]});
	return tr(key.c_str(), {args[0], args[1]});
}

/* a branch's .input file, by its number, or -1 for a name that is not one */
static int branchInputNumber(const std::string &name)
{
	static const std::string head = "branches/", tail = ".input";
	if (name.size() <= head.size() + tail.size() || name.compare(0, head.size(), head) != 0
		|| name.compare(name.size() - tail.size(), tail.size(), tail) != 0)
		return -1;
	const std::string digits = name.substr(head.size(), name.size() - head.size() - tail.size());
	if (digits.empty() || digits.size() > 6 || digits[0] == '0') return -1;
	for (char c : digits)
		if (c < '0' || c > '9') return -1;
	return std::stoi(digits);
}

/* The parts of a project file; false with why for one that is not a project.
 * The form before 2026-11-02, one JSON file, is refused with a sentence of
 * its own: this Chimera keeps no reader for it (owner, 2026-11-02). */
static bool partsOf(const std::vector<uint8_t> &raw, Parts &parts, std::string &why)
{
	size_t first = raw.size() >= 3 && raw[0] == 0xEF && raw[1] == 0xBB && raw[2] == 0xBF ? 3 : 0;
	while (first < raw.size() && std::isspace(raw[first])) first++;
	if (first < raw.size() && raw[first] == '{')
	{
		why = tr("project.old_form");
		return false;
	}
	mz_zip_archive zip{};
	if (mz_zip_reader_init_mem(&zip, raw.data(), raw.size(), 0) == MZ_FALSE)
	{
		why = tr("project.not_zip");
		return false;
	}
	bool haveJson = false;
	const mz_uint count = mz_zip_reader_get_num_files(&zip);
	for (mz_uint i = 0; i < count && why.empty(); i++)
	{
		if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
		char nameBuf[512] = {};
		mz_zip_reader_get_filename(&zip, i, nameBuf, sizeof nameBuf);
		const std::string name = nameBuf;
		std::string *into = nullptr;
		if (name == "project.json")
		{
			into = &parts.json;
			haveJson = true;
		}
		else if (name == "input.input") into = &parts.input;
		else if (const int n = branchInputNumber(name); n > 0) into = &parts.branches[n];
		else
		{
			why = tr("project.unknown_entry", {name});
			break;
		}
		/* a part of a gigabyte or more is no project's */
		mz_zip_archive_file_stat stat{};
		size_t len = 0;
		void *data = mz_zip_reader_file_stat(&zip, i, &stat) == MZ_TRUE && stat.m_uncomp_size < (uint64_t{1} << 30)
			? mz_zip_reader_extract_to_heap(&zip, i, &len, 0) : nullptr;
		if (data == nullptr)
		{
			why = tr("project.entry_unreadable", {name});
			break;
		}
		into->assign(static_cast<const char *>(data), len);
		mz_free(data);
	}
	mz_zip_reader_end(&zip);
	if (why.empty() && !haveJson) why = tr("project.no_json");
	return why.empty();
}

/* A project from its parts. pathForId is where the project is, for the id of
 * one that has none; with none given it gets a new one. */
static ce_project *parseProject(const Parts &parts, const char *pathForId, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	cJSON *root = cJSON_ParseWithLength(parts.json.data(), parts.json.size());
	if (root == nullptr)
	{
		fail(tr("project.not_json"), error_out);
		return nullptr;
	}
	if (!cJSON_IsObject(root))
	{
		cJSON_Delete(root);
		fail(tr("project.not_object"), error_out);
		return nullptr;
	}

	auto reject = [&](std::string message) -> ce_project *
	{
		cJSON_Delete(root);
		fail(std::move(message), error_out);
		return nullptr;
	};

	/* the format is strict: a key this build does not know is an error, not
	 * something to drop silently on the next save */
	static const char *known[] = { "id", "title", "core", "rerecords", "clockStart",
		"files", "settings", "firmware", "coreCache", "inputs", "markers", "branches", "headers", "tastudio" };
	for (cJSON *item = root->child; item != nullptr; item = item->next)
	{
		bool ok = false;
		for (const char *k : known)
		{
			if (item->string != nullptr && std::strcmp(item->string, k) == 0) { ok = true; break; }
		}
		if (!ok) return reject(tr("project.unknown_key", {item->string != nullptr ? item->string : ""}));
	}

	auto *p = new ce_project();
	auto rejectP = [&](std::string message) -> ce_project *
	{
		delete p;
		return reject(std::move(message));
	};

	cJSON *j;
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "id")) != nullptr)
	{
		if (!cJSON_IsString(j)) return rejectP(tr("project.must_be_string", {"id"}));
		p->id = j->valuestring;
	}
	/* A project written before ids, or by hand, has none. It gets one made
	 * from where the file is, the same at every open until a save writes it
	 * in: a random one each time hid the work of a session that crashed
	 * before its first save, which is looked for by id (engine analysis,
	 * 2026-10-18). Where the path cannot be made whole, a random one. */
	if (p->id.empty() && (pathForId == nullptr || pathForId[0] == '\0')) p->id = mintProjectId();
	if (p->id.empty())
	{
		std::error_code ec;
		const std::string whole = std::filesystem::weakly_canonical(std::filesystem::u8path(pathForId), ec).u8string();
		p->id = !ec && !whole.empty()
			? chimera::sha1Hex(reinterpret_cast<const uint8_t *>(whole.data()), whole.size()).substr(0, 16)
			: mintProjectId();
		for (char &c : p->id) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "title")) != nullptr)
	{
		if (!cJSON_IsString(j)) return rejectP(tr("project.not_string", {"title"}));
		p->title = j->valuestring;
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "core")) != nullptr)
	{
		if (!cJSON_IsObject(j)) return rejectP(tr("project.field_not_object", {"core"}));
		if (const char *k = unknownKey(j, { "name", "version", "sha1" })) return rejectP(tr("project.unknown_key_in", {k, "\"core\""}));
		cJSON *name = cJSON_GetObjectItemCaseSensitive(j, "name");
		cJSON *version = cJSON_GetObjectItemCaseSensitive(j, "version");
		cJSON *sha1 = cJSON_GetObjectItemCaseSensitive(j, "sha1");
		if (!cJSON_IsString(name) || !cJSON_IsString(version) || !cJSON_IsString(sha1))
		{
			return rejectP(tr("project.core_fields"));
		}
		p->coreName = name->valuestring;
		p->coreVersion = version->valuestring;
		p->coreSha1 = upperHex(sha1->valuestring);
		if (!p->coreSha1.empty() && !validSha1(p->coreSha1))
		{
			return rejectP(tr("project.core_sha1"));
		}
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "rerecords")) != nullptr)
	{
		if (!cJSON_IsNumber(j) || j->valuedouble < 0) return rejectP(tr("project.rerecords"));
		p->rerecords = static_cast<uint64_t>(j->valuedouble);
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "clockStart")) != nullptr)
	{
		if (!cJSON_IsString(j) || !parseUtcSecond(j->valuestring, p->clockStart))
			return rejectP(tr("project.clock_start"));
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "files")) != nullptr)
	{
		if (!cJSON_IsArray(j)) return rejectP(tr("project.not_array", {"files"}));
		for (cJSON *item = j->child; item != nullptr; item = item->next)
		{
			if (const char *k = unknownKey(item, { "name", "sha1", "slot" })) return rejectP(tr("project.unknown_key_in", {k, tr("project.a_file")}));
			cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
			cJSON *sha1 = cJSON_GetObjectItemCaseSensitive(item, "sha1");
			cJSON *slot = cJSON_GetObjectItemCaseSensitive(item, "slot");
			if (!cJSON_IsString(name) || !cJSON_IsString(sha1) || !cJSON_IsString(slot))
			{
				return rejectP(tr("project.file_fields"));
			}
			FileEntry e;
			e.name = name->valuestring;
			e.sha1 = upperHex(sha1->valuestring);
			e.slot = slot->valuestring;
			if (!bareName(e.name)) return rejectP(tr("project.not_bare", {e.name}));
			if (!validSha1(e.sha1)) return rejectP(tr("project.file_sha1", {e.name}));
			if (!validSlot(e.slot)) return rejectP(tr("project.file_slot", {e.name, e.slot}));
			for (const FileEntry &prior : p->files)
			{
				if (prior.name == e.name) return rejectP(tr("project.listed_twice", {e.name}));
			}
			p->files.push_back(std::move(e));
		}
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "settings")) != nullptr)
	{
		if (!cJSON_IsObject(j)) return rejectP(tr("project.field_not_object", {"settings"}));
		cJSON_Delete(p->settings);
		p->settings = cJSON_Duplicate(j, 1);
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "firmware")) != nullptr)
	{
		if (!cJSON_IsArray(j)) return rejectP(tr("project.not_array", {"firmware"}));
		cJSON_Delete(p->firmware);
		p->firmware = cJSON_Duplicate(j, 1);
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "coreCache")) != nullptr)
	{
		if (!cJSON_IsArray(j)) return rejectP(tr("project.not_array", {"coreCache"}));
		cJSON_Delete(p->coreCache);
		p->coreCache = cJSON_Duplicate(j, 1);
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "tastudio")) != nullptr)
	{
		if (!cJSON_IsObject(j)) return rejectP(tr("project.field_not_object", {"tastudio"}));
		p->tastudio = cJSON_Duplicate(j, 1);
	}
	std::vector<std::string> args;
	{
		std::string why;
		if (!lumpOf(cJSON_GetObjectItemCaseSensitive(root, "inputs"), parts.input, "\"inputs\"", "input.input", p->log, why, args))
			return rejectP(trArgs(why, args));
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "markers")) != nullptr)
	{
		if (!cJSON_IsArray(j)) return rejectP(tr("project.not_array", {"markers"}));
		for (cJSON *item = j->child; item != nullptr; item = item->next)
		{
			if (const char *k = unknownKey(item, { "frame", "text", "keepState" })) return rejectP(tr("project.unknown_key_in", {k, tr("project.a_marker")}));
			cJSON *frame = cJSON_GetObjectItemCaseSensitive(item, "frame");
			cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
			cJSON *keep = cJSON_GetObjectItemCaseSensitive(item, "keepState");
			if (!cJSON_IsNumber(frame) || !cJSON_IsString(text))
			{
				return rejectP(tr("project.marker_fields"));
			}
			Marker m;
			m.frame = static_cast<int64_t>(frame->valuedouble);
			m.text = text->valuestring;
			if (keep != nullptr)
			{
				if (!cJSON_IsBool(keep)) return rejectP(tr("project.marker_keep_state"));
				m.keepState = cJSON_IsTrue(keep);
			}
			p->markers.push_back(std::move(m));
		}
		/* kept sorted by frame; a hand-edited file gets the order restored
		 * (stable, so equal frames keep their relative order) */
		for (size_t i = 1; i < p->markers.size(); i++)
		{
			Marker m = std::move(p->markers[i]);
			size_t at = i;
			while (at > 0 && p->markers[at - 1].frame > m.frame)
			{
				p->markers[at] = std::move(p->markers[at - 1]);
				at--;
			}
			p->markers[at] = std::move(m);
		}
	}
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "branches")) != nullptr)
	{
		if (!cJSON_IsArray(j)) return rejectP(tr("project.not_array", {"branches"}));
		for (cJSON *item = j->child; item != nullptr; item = item->next)
		{
			if (const char *k = unknownKey(item, { "name", "frame", "inputs", "time", "markers" })) return rejectP(tr("project.unknown_key_in", {k, tr("project.a_branch")}));
			cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
			cJSON *frame = cJSON_GetObjectItemCaseSensitive(item, "frame");
			cJSON *inputs = cJSON_GetObjectItemCaseSensitive(item, "inputs");
			cJSON *time = cJSON_GetObjectItemCaseSensitive(item, "time");
			cJSON *markers = cJSON_GetObjectItemCaseSensitive(item, "markers");
			if (!cJSON_IsString(name) || !cJSON_IsNumber(frame))
			{
				return rejectP(tr("project.branch_fields"));
			}
			Branch b;
			b.name = name->valuestring;
			b.frame = static_cast<int64_t>(frame->valuedouble);
			const int number = static_cast<int>(p->branches.size()) + 1;
			const auto lines = parts.branches.find(number);
			if (std::string why; !lumpOf(inputs, lines != parts.branches.end() ? lines->second : std::string(), tr("project.branch_inputs"),
					"branches/" + std::to_string(number) + ".input", b.log, why, args))
				return rejectP(trArgs(why, args));
			if (time != nullptr)
			{
				if (!cJSON_IsString(time)) return rejectP(tr("project.branch_time"));
				b.time = time->valuestring;
			}
			if (markers != nullptr)
			{
				if (!cJSON_IsArray(markers)) return rejectP(tr("project.branch_markers"));
				for (cJSON *m = markers->child; m != nullptr; m = m->next)
				{
					if (const char *k = unknownKey(m, { "frame", "text", "keepState" })) return rejectP(tr("project.unknown_key_in", {k, tr("project.a_branch_marker")}));
					cJSON *mFrame = cJSON_GetObjectItemCaseSensitive(m, "frame");
					cJSON *mText = cJSON_GetObjectItemCaseSensitive(m, "text");
					cJSON *mKeep = cJSON_GetObjectItemCaseSensitive(m, "keepState");
					if (!cJSON_IsNumber(mFrame) || !cJSON_IsString(mText))
					{
						return rejectP(tr("project.branch_marker_fields"));
					}
					Marker mk;
					mk.frame = static_cast<int64_t>(mFrame->valuedouble);
					mk.text = mText->valuestring;
					/* as for the project's own markers: written only when false */
					if (mKeep != nullptr)
					{
						if (!cJSON_IsBool(mKeep)) return rejectP(tr("project.branch_marker_keep_state"));
						mk.keepState = cJSON_IsTrue(mKeep);
					}
					b.markers.push_back(std::move(mk));
				}
			}
			p->branches.push_back(std::move(b));
		}
	}
	for (const auto &[number, lines] : parts.branches)
		if (number > static_cast<int>(p->branches.size()))
			return rejectP(tr("project.unknown_entry", {"branches/" + std::to_string(number) + ".input"}));
	if ((j = cJSON_GetObjectItemCaseSensitive(root, "headers")) != nullptr)
	{
		if (!cJSON_IsObject(j)) return rejectP(tr("project.field_not_object", {"headers"}));
		for (cJSON *item = j->child; item != nullptr; item = item->next)
		{
			if (!cJSON_IsString(item)) return rejectP(tr("project.header_string"));
			p->headers.emplace_back(item->string != nullptr ? item->string : "", item->valuestring);
		}
	}

	cJSON_Delete(root);
	return p;
}

static ce_project *openProject(const char *path, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	std::vector<uint8_t> raw;
	if (!chimera::readFile(path, raw))
	{
		fail(tr("new_project.cannot_read", {path}), error_out);
		return nullptr;
	}
	Parts parts;
	std::string why;
	if (!partsOf(raw, parts, why))
	{
		fail(why, error_out);
		return nullptr;
	}
	return parseProject(parts, path, error_out);
}

ce_project *ce_project_open(const char *path, const char **error_out)
{
	/* a project is somebody's file: nothing in it may end the process */
	try
	{
		return openProject(path, error_out);
	}
	catch (const std::bad_alloc &)
	{
		*g_error = tr("project.no_memory");
		if (error_out != nullptr) *error_out = g_error->c_str();
		return nullptr;
	}
}

int32_t ce_project_save(ce_project *p, const char *path, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;

	/* cue closure at save: every file a RESOLVED cue references must be
	 * listed, or unhashed bytes would reach the machine on the next open */
	for (const FileEntry &e : p->files)
	{
		if (!hasCueSuffix(e.name) || e.status == 1) continue;
		for (const std::string &ref : cueReferences(readSmall(e.sourcePath)))
		{
			bool listed = false;
			for (const FileEntry &other : p->files)
			{
				if (other.name == ref) { listed = true; break; }
			}
			if (!listed)
			{
				return failInt(tr("project.unlisted_reference", {e.name, ref}), error_out);
			}
		}
	}

	cJSON *root = cJSON_CreateObject();
	cJSON_AddStringToObject(root, "id", p->id.c_str());
	cJSON_AddStringToObject(root, "title", p->title.c_str());
	cJSON *core = cJSON_AddObjectToObject(root, "core");
	cJSON_AddStringToObject(core, "name", p->coreName.c_str());
	cJSON_AddStringToObject(core, "version", p->coreVersion.c_str());
	cJSON_AddStringToObject(core, "sha1", p->coreSha1.c_str());
	cJSON_AddNumberToObject(root, "rerecords", static_cast<double>(p->rerecords));
	if (p->clockStart >= 0) cJSON_AddStringToObject(root, "clockStart", utcSecondText(p->clockStart).c_str());
	cJSON *files = cJSON_AddArrayToObject(root, "files");
	for (const FileEntry &e : p->files)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", e.name.c_str());
		/* a resolved file's ACTUAL hash: proceeding past a mismatch is
		 * allowed, but the project then records what actually ran */
		cJSON_AddStringToObject(item, "sha1", e.status == 1 ? e.sha1.c_str() : e.actualSha1.c_str());
		cJSON_AddStringToObject(item, "slot", e.slot.c_str());
		cJSON_AddItemToArray(files, item);
	}
	cJSON_AddItemToObject(root, "settings", cJSON_Duplicate(p->settings, 1));
	cJSON_AddItemToObject(root, "firmware", cJSON_Duplicate(p->firmware, 1));
	/* What the core compiled for this game, by name and hash: regenerable from
	 * the same inputs, recorded so a later run can be told it is running the
	 * same compiled code (docs/compile-cache.md). */
	cJSON_AddItemToObject(root, "coreCache", cJSON_Duplicate(p->coreCache, 1));
	/* only when there is one: a project that never met TAStudio stays exactly
	 * what an older build can open */
	if (p->tastudio != nullptr) cJSON_AddItemToObject(root, "tastudio", cJSON_Duplicate(p->tastudio, 1));
	Parts parts;
	if (cJSON *inputs = inputsOf(p->log, 1, parts.input)) cJSON_AddItemToObject(root, "inputs", inputs);
	cJSON *markers = cJSON_AddArrayToObject(root, "markers");
	for (const Marker &m : p->markers)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddNumberToObject(item, "frame", static_cast<double>(m.frame));
		cJSON_AddStringToObject(item, "text", m.text.c_str());
		if (!m.keepState) cJSON_AddBoolToObject(item, "keepState", 0);
		cJSON_AddItemToArray(markers, item);
	}
	cJSON *branches = cJSON_AddArrayToObject(root, "branches");
	for (const Branch &b : p->branches)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", b.name.c_str());
		cJSON_AddNumberToObject(item, "frame", static_cast<double>(b.frame));
		if (!b.time.empty()) cJSON_AddStringToObject(item, "time", b.time.c_str());
		/* a branch always has its .input file, if only an empty one */
		std::string &lines = parts.branches[static_cast<int>(parts.branches.size()) + 1];
		if (cJSON *inputs = inputsOf(b.log, 3, lines)) cJSON_AddItemToObject(item, "inputs", inputs);
		if (!b.markers.empty())
		{
			cJSON *bm = cJSON_AddArrayToObject(item, "markers");
			for (const Marker &m : b.markers)
			{
				cJSON *mi = cJSON_CreateObject();
				cJSON_AddNumberToObject(mi, "frame", static_cast<double>(m.frame));
				cJSON_AddStringToObject(mi, "text", m.text.c_str());
				if (!m.keepState) cJSON_AddBoolToObject(mi, "keepState", 0);
				cJSON_AddItemToArray(bm, mi);
			}
		}
		cJSON_AddItemToArray(branches, item);
	}
	if (!p->headers.empty())
	{
		cJSON *hdrs = cJSON_AddObjectToObject(root, "headers");
		for (const auto &kv : p->headers)
		{
			cJSON_AddStringToObject(hdrs, kv.first.c_str(), kv.second.c_str());
		}
	}

	char *text = cJSON_Print(root);
	cJSON_Delete(root);
	if (text == nullptr) return failInt(tr("project.not_serialized"), error_out);
	parts.json = text;
	cJSON_free(text);
	parts.json += "\n";

	/* the zip: project.json, input.input, then the branches' in their order */
	mz_zip_archive zip{};
	bool zipped = mz_zip_writer_init_heap(&zip, 0, 0) == MZ_TRUE;
	auto add = [&](const std::string &name, const std::string &bytes) {
		zipped = zipped && mz_zip_writer_add_mem(&zip, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_LEVEL) == MZ_TRUE;
	};
	add("project.json", parts.json);
	add("input.input", parts.input);
	for (const auto &[number, lines] : parts.branches) add("branches/" + std::to_string(number) + ".input", lines);
	void *buf = nullptr;
	size_t size = 0;
	zipped = zipped && mz_zip_writer_finalize_heap_archive(&zip, &buf, &size) == MZ_TRUE;
	std::vector<uint8_t> out;
	if (zipped) out.assign(static_cast<uint8_t *>(buf), static_cast<uint8_t *>(buf) + size);
	mz_free(buf);
	mz_zip_writer_end(&zip);
	if (!zipped) return failInt(tr("project.not_serialized"), error_out);

	if (!chimera::writeFile(path, out.data(), out.size()))
	{
		return failInt(tr("session.could_not_write", {path}), error_out);
	}
	return 0;
}


int32_t ce_project_save_as(ce_project *p, const char *path, const char **error_out)
{
	const std::string was = p->id;
	p->id = mintProjectId();
	if (ce_project_save(p, path, error_out) == 0) return 0;
	p->id = was;
	return 1;
}

/* ---- identity ---- */

const char *ce_project_id(const ce_project *p) { return p->id.c_str(); }
const char *ce_project_title(const ce_project *p) { return p->title.c_str(); }
void ce_project_set_title(ce_project *p, const char *title) { p->title = title != nullptr ? title : ""; }

const char *ce_project_core_name(const ce_project *p) { return p->coreName.c_str(); }
const char *ce_project_core_version(const ce_project *p) { return p->coreVersion.c_str(); }
const char *ce_project_core_sha1(const ce_project *p) { return p->coreSha1.c_str(); }
void ce_project_set_core(ce_project *p, const char *name, const char *version, const char *sha1)
{
	p->coreName = name != nullptr ? name : "";
	p->coreVersion = version != nullptr ? version : "";
	p->coreSha1 = upperHex(sha1 != nullptr ? sha1 : "");
}

uint64_t ce_project_rerecords(const ce_project *p) { return p->rerecords; }
void ce_project_set_rerecords(ce_project *p, uint64_t count) { p->rerecords = count; }

int64_t ce_project_clock_start(const ce_project *p) { return p->clockStart; }

int32_t ce_project_set_clock_start(ce_project *p, int64_t unix_seconds)
{
	if (unix_seconds > kClockStartLast) return 1;
	p->clockStart = unix_seconds < 0 || unix_seconds == CE_CLOCK_START_DEFAULT ? -1 : unix_seconds;
	return 0;
}

/* ---- lumps ---- */

const char *ce_project_settings_text(ce_project *p, uint64_t *len_out)
{
	char *text = cJSON_PrintUnformatted(p->settings);
	p->settingsOut = text != nullptr ? text : "{}";
	if (text != nullptr) cJSON_free(text);
	if (len_out != nullptr) *len_out = p->settingsOut.size();
	return p->settingsOut.c_str();
}

int32_t ce_project_set_settings_text(ce_project *p, const char *json, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	cJSON *parsed = cJSON_Parse(json != nullptr ? json : "");
	if (parsed == nullptr || !cJSON_IsObject(parsed))
	{
		cJSON_Delete(parsed);
		return failInt(tr("project.must_be", {"settings", tr("project.json_object")}), error_out);
	}
	cJSON_Delete(p->settings);
	p->settings = parsed;
	return 0;
}

const char *ce_project_core_cache_text(ce_project *p, uint64_t *len_out)
{
	char *text = cJSON_PrintUnformatted(p->coreCache);
	p->coreCacheOut = text != nullptr ? text : "[]";
	if (text != nullptr) cJSON_free(text);
	if (len_out != nullptr) *len_out = p->coreCacheOut.size();
	return p->coreCacheOut.c_str();
}

int32_t ce_project_set_core_cache_text(ce_project *p, const char *json, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	cJSON *parsed = cJSON_Parse(json != nullptr ? json : "");
	if (parsed == nullptr || !cJSON_IsArray(parsed))
	{
		cJSON_Delete(parsed);
		return failInt(tr("project.must_be", {"coreCache", tr("project.json_array")}), error_out);
	}
	cJSON_Delete(p->coreCache);
	p->coreCache = parsed;
	return 0;
}

const char *ce_project_tastudio_text(ce_project *p, uint64_t *len_out)
{
	char *text = p->tastudio != nullptr ? cJSON_PrintUnformatted(p->tastudio) : nullptr;
	p->tastudioOut = text != nullptr ? text : "";
	if (text != nullptr) cJSON_free(text);
	if (len_out != nullptr) *len_out = p->tastudioOut.size();
	return p->tastudioOut.c_str();
}

int32_t ce_project_set_tastudio_text(ce_project *p, const char *json, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	if (json == nullptr || json[0] == '\0')
	{
		cJSON_Delete(p->tastudio);
		p->tastudio = nullptr;
		return 0;
	}
	cJSON *parsed = cJSON_Parse(json);
	if (parsed == nullptr || !cJSON_IsObject(parsed))
	{
		cJSON_Delete(parsed);
		return failInt(tr("project.must_be", {"tastudio", tr("project.json_object")}), error_out);
	}
	cJSON_Delete(p->tastudio);
	p->tastudio = parsed;
	return 0;
}

const char *ce_project_firmware_text(ce_project *p, uint64_t *len_out)
{
	char *text = cJSON_PrintUnformatted(p->firmware);
	p->firmwareOut = text != nullptr ? text : "[]";
	if (text != nullptr) cJSON_free(text);
	if (len_out != nullptr) *len_out = p->firmwareOut.size();
	return p->firmwareOut.c_str();
}

int32_t ce_project_set_firmware_text(ce_project *p, const char *json, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	cJSON *parsed = cJSON_Parse(json != nullptr ? json : "");
	if (parsed == nullptr || !cJSON_IsArray(parsed))
	{
		cJSON_Delete(parsed);
		return failInt(tr("project.must_be", {"firmware", tr("project.json_array")}), error_out);
	}
	cJSON_Delete(p->firmware);
	p->firmware = parsed;
	return 0;
}

const char *ce_project_log_text(const ce_project *p, uint64_t *len_out)
{
	if (len_out != nullptr) *len_out = p->log.size();
	return p->log.c_str();
}

void ce_project_set_log_text(ce_project *p, const char *text, uint64_t len)
{
	if (text == nullptr) p->log.clear();
	else p->log.assign(text, static_cast<size_t>(len));
}

/* ---- markers ---- */

int32_t ce_project_marker_count(const ce_project *p)
{
	return static_cast<int32_t>(p->markers.size());
}

int64_t ce_project_marker_frame(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_marker_count(p)) return -1;
	return p->markers[index].frame;
}

const char *ce_project_marker_text(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_marker_count(p)) return nullptr;
	return p->markers[index].text.c_str();
}

int32_t ce_project_marker_add(ce_project *p, int64_t frame, const char *text, int32_t keep_state)
{
	Marker m;
	m.frame = frame;
	m.text = text != nullptr ? text : "";
	m.keepState = keep_state != 0;
	size_t at = p->markers.size();
	while (at > 0 && p->markers[at - 1].frame > frame) at--;
	p->markers.insert(p->markers.begin() + at, std::move(m));
	return static_cast<int32_t>(at);
}

int32_t ce_project_marker_keep_state(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_marker_count(p)) return 1;
	return p->markers[index].keepState ? 1 : 0;
}

void ce_project_marker_remove(ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_marker_count(p)) return;
	p->markers.erase(p->markers.begin() + index);
}

/* ---- branches ---- */

int32_t ce_project_branch_count(const ce_project *p)
{
	return static_cast<int32_t>(p->branches.size());
}

const char *ce_project_branch_name(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_count(p)) return nullptr;
	return p->branches[index].name.c_str();
}

int64_t ce_project_branch_frame(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_count(p)) return -1;
	return p->branches[index].frame;
}

const char *ce_project_branch_log_text(const ce_project *p, int32_t index, uint64_t *len_out)
{
	if (len_out != nullptr) *len_out = 0;
	if (index < 0 || index >= ce_project_branch_count(p)) return nullptr;
	if (len_out != nullptr) *len_out = p->branches[index].log.size();
	return p->branches[index].log.c_str();
}

const char *ce_project_branch_time(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_count(p)) return nullptr;
	return p->branches[index].time.c_str();
}

void ce_project_branch_add(ce_project *p, const char *name, int64_t frame, const char *time, const char *log_text, uint64_t len)
{
	Branch b;
	b.name = name != nullptr ? name : "";
	b.frame = frame;
	b.time = time != nullptr ? time : "";
	if (log_text != nullptr) b.log.assign(log_text, static_cast<size_t>(len));
	p->branches.push_back(std::move(b));
}

void ce_project_branch_remove(ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_count(p)) return;
	p->branches.erase(p->branches.begin() + index);
}

int32_t ce_project_branch_marker_count(const ce_project *p, int32_t branch)
{
	if (branch < 0 || branch >= ce_project_branch_count(p)) return 0;
	return static_cast<int32_t>(p->branches[branch].markers.size());
}

int64_t ce_project_branch_marker_frame(const ce_project *p, int32_t branch, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_marker_count(p, branch)) return -1;
	return p->branches[branch].markers[index].frame;
}

const char *ce_project_branch_marker_text(const ce_project *p, int32_t branch, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_marker_count(p, branch)) return nullptr;
	return p->branches[branch].markers[index].text.c_str();
}

void ce_project_branch_marker_add(ce_project *p, int32_t branch, int64_t frame, const char *text, int32_t keep_state)
{
	if (branch < 0 || branch >= ce_project_branch_count(p)) return;
	Marker m;
	m.frame = frame;
	m.text = text != nullptr ? text : "";
	m.keepState = keep_state != 0;
	p->branches[branch].markers.push_back(std::move(m));
}

int32_t ce_project_branch_marker_keep_state(const ce_project *p, int32_t branch, int32_t index)
{
	if (index < 0 || index >= ce_project_branch_marker_count(p, branch)) return 1;
	return p->branches[branch].markers[index].keepState ? 1 : 0;
}

/* ---- the headers map: ordered, verbatim ---- */

int32_t ce_project_header_count(const ce_project *p)
{
	return static_cast<int32_t>(p->headers.size());
}

const char *ce_project_header_key_at(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_header_count(p)) return nullptr;
	return p->headers[index].first.c_str();
}

const char *ce_project_header_value_at(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_header_count(p)) return nullptr;
	return p->headers[index].second.c_str();
}

const char *ce_project_header_get(const ce_project *p, const char *key)
{
	if (key == nullptr) return nullptr;
	for (const auto &kv : p->headers)
	{
		if (kv.first == key) return kv.second.c_str();
	}
	return nullptr;
}

/* value NULL removes the key; a new key appends, keeping order */
void ce_project_header_set(ce_project *p, const char *key, const char *value)
{
	if (key == nullptr) return;
	for (size_t i = 0; i < p->headers.size(); i++)
	{
		if (p->headers[i].first != key) continue;
		if (value == nullptr) p->headers.erase(p->headers.begin() + static_cast<long>(i));
		else p->headers[i].second = value;
		return;
	}
	if (value != nullptr) p->headers.emplace_back(key, value);
}

/* ---- files ---- */

const char *ce_cue_references(const char *cue_bytes, uint64_t cue_len, uint64_t *len_out)
{
	static thread_local chimera::ThreadString g_cueRefs;   /* never destroyed: see thread_string.hpp */
	cJSON *arr = cJSON_CreateArray();
	if (cue_bytes != nullptr && cue_len > 0)
	{
		std::vector<uint8_t> bytes(cue_bytes, cue_bytes + cue_len);
		for (const std::string &ref : cueReferences(bytes))
		{
			cJSON_AddItemToArray(arr, cJSON_CreateString(ref.c_str()));
		}
	}
	char *printed = cJSON_PrintUnformatted(arr);
	*g_cueRefs = printed != nullptr ? printed : "[]";
	cJSON_free(printed);
	cJSON_Delete(arr);
	if (len_out != nullptr) *len_out = g_cueRefs->size();
	return g_cueRefs->c_str();
}

int32_t ce_project_file_add(ce_project *p, const char *name, const char *slot, const char *source_path, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	FileEntry e;
	e.name = name != nullptr ? name : "";
	e.slot = slot != nullptr ? slot : "";
	if (!bareName(e.name)) return failInt(tr("project.not_bare", {e.name}), error_out);
	if (!validSlot(e.slot)) return failInt(tr("project.file_slot", {e.name, e.slot}), error_out);
	for (const FileEntry &prior : p->files)
	{
		if (prior.name == e.name) return failInt(tr("project.already_listed", {e.name}), error_out);
	}
	if (source_path == nullptr || !chimera::sha1HexOfFile(source_path, &e.size, e.actualSha1))
	{
		return failInt(tr("project.cannot_read_from", {e.name, source_path != nullptr ? source_path : tr("engine.no_path")}), error_out);
	}
	e.sha1 = e.actualSha1;
	e.status = 0;
	e.sourcePath = source_path;
	std::string folder = folderOf(source_path);
	std::string cueName = e.name;
	bool isCue = hasCueSuffix(e.name);
	std::vector<std::string> refs = isCue ? cueReferences(readSmall(source_path)) : std::vector<std::string>();
	size_t before = p->files.size();
	p->files.push_back(std::move(e));

	/* a cue's referenced files join the project automatically, from the
	 * cue's own folder - closure holds from the moment of creation. Any
	 * failure removes the cue and everything it brought: all or nothing. */
	auto failCue = [&](std::string message) -> int32_t
	{
		p->files.resize(before);
		return failInt(std::move(message), error_out);
	};
	for (const std::string &ref : refs)
	{
		bool listed = false;
		for (const FileEntry &other : p->files)
		{
			if (other.name == ref) { listed = true; break; }
		}
		if (listed) continue;
		if (!bareName(ref))
		{
			return failCue(tr("project.cue_not_bare", {cueName, ref}));
		}
		FileEntry s;
		s.name = ref;
		s.slot = "support";
		if (!chimera::sha1HexOfFile((folder + ref).c_str(), &s.size, s.actualSha1))
		{
			return failCue(tr("project.cue_not_beside", {cueName, ref}));
		}
		s.sha1 = s.actualSha1;
		s.status = 0;
		s.sourcePath = folder + ref;
		p->files.push_back(std::move(s));
	}
	return 0;
}

void ce_project_file_remove(ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return;
	p->files.erase(p->files.begin() + index);
}

int32_t ce_project_file_count(const ce_project *p)
{
	return static_cast<int32_t>(p->files.size());
}

const char *ce_project_file_name(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return nullptr;
	return p->files[index].name.c_str();
}

const char *ce_project_file_slot(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return nullptr;
	return p->files[index].slot.c_str();
}

const char *ce_project_file_sha1(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return nullptr;
	return p->files[index].sha1.c_str();
}

const char *ce_project_file_actual_sha1(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return nullptr;
	return p->files[index].actualSha1.c_str();
}

int32_t ce_project_file_status(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return 1;
	return p->files[index].status;
}

const char *ce_project_file_source_path(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return nullptr;
	return p->files[index].sourcePath.c_str();
}

uint64_t ce_project_file_size(const ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return 0;
	const FileEntry &e = p->files[index];
	return e.status == 1 ? 0 : e.size;
}

int32_t ce_project_file_resolve(ce_project *p, int32_t index, const char *path, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;
	if (index < 0 || index >= ce_project_file_count(p))
	{
		return failInt(tr("project.no_file_entry"), error_out);
	}
	FileEntry &e = p->files[index];
	if (path == nullptr || !chimera::sha1HexOfFile(path, &e.size, e.actualSha1))
	{
		return failInt(tr("project.cannot_read_from", {e.name, path != nullptr ? path : tr("engine.no_path")}), error_out);
	}
	e.status = e.actualSha1 == e.sha1 ? 0 : 2;
	e.sourcePath = path;
	return 0;
}

void ce_project_file_unresolve(ce_project *p, int32_t index)
{
	if (index < 0 || index >= ce_project_file_count(p)) return;
	FileEntry &e = p->files[index];
	e.size = 0;
	e.actualSha1.clear();
	e.sourcePath.clear();
	e.status = 1;
}

int32_t ce_project_resolve_dir(ce_project *p, const char *dir)
{
	if (dir == nullptr) return 0;
	std::string base = dir;
	if (!base.empty() && base.back() != '/' && base.back() != '\\') base += '/';
	int32_t resolved = 0;
	for (FileEntry &e : p->files)
	{
		if (e.status != 1) continue;
		const std::string path = base + e.name;
		if (!chimera::sha1HexOfFile(path.c_str(), &e.size, e.actualSha1)) continue;
		e.status = e.actualSha1 == e.sha1 ? 0 : 2;
		e.sourcePath = path;
		resolved++;
	}
	return resolved;
}

int32_t ce_project_files_ok(const ce_project *p)
{
	for (const FileEntry &e : p->files)
	{
		if (e.status != 0) return 0;
	}
	return 1;
}

int32_t ce_project_game_file(const ce_project *p)
{
	for (size_t i = 0; i < p->files.size(); i++)
	{
		if (p->files[i].slot != "support") return static_cast<int32_t>(i);
	}
	return -1;
}

/* ---- validation against a core's file_slots.json ---- */

} // extern "C"

std::vector<std::string> ceFirmwarePinMismatches(const ce_project *p,
	const char *const *ids, const uint8_t *const *data, const uint64_t *lens, int32_t count)
{
	std::vector<std::string> out;
	if (p == nullptr || !cJSON_IsArray(p->firmware)) return out;
	const cJSON *pin = nullptr;
	cJSON_ArrayForEach(pin, p->firmware)
	{
		const cJSON *id = cJSON_GetObjectItemCaseSensitive(pin, "id");
		const cJSON *sha1 = cJSON_GetObjectItemCaseSensitive(pin, "sha1");
		if (!cJSON_IsString(id) || !cJSON_IsString(sha1)) continue;
		int32_t given = -1;
		for (int32_t i = 0; i < count && given < 0; i++)
		{
			if (ids[i] != nullptr && std::strcmp(ids[i], id->valuestring) == 0) given = i;
		}
		const std::string pinned = chimera::manifest::upperHex(sha1->valuestring);
		if (given < 0)
		{
			out.push_back(tr("project.firmware_missing", {id->valuestring, pinned}));
			continue;
		}
		const std::string actual = data[given] != nullptr ? chimera::sha1Hex(data[given], lens[given]) : std::string();
		if (chimera::manifest::upperHex(actual) != pinned)
		{
			out.push_back(tr("project.firmware_other", {id->valuestring, pinned, chimera::manifest::upperHex(actual)}));
		}
	}
	return out;
}

extern "C" {

int32_t ce_project_validate(const ce_project *p, const char *slots_json, uint64_t slots_len, const char **error_out)
{
	return ceProjectValidate(p, slots_json, slots_len, nullptr, error_out);
}

int32_t ceProjectValidate(const ce_project *p, const char *slots_json, uint64_t slots_len,
	const cJSON *settings, const char **error_out)
{
	if (settings == nullptr) settings = p->settings;
	if (error_out != nullptr) *error_out = nullptr;
	if (slots_json == nullptr) return failInt(tr("project.no_slots"), error_out);

	cJSON *root = cJSON_ParseWithLength(slots_json, static_cast<size_t>(slots_len));
	if (root == nullptr) return failInt(tr("project.slots_not_json"), error_out);
	cJSON *slots = cJSON_GetObjectItemCaseSensitive(root, "slots");
	if (!cJSON_IsArray(slots))
	{
		cJSON_Delete(root);
		return failInt(tr("project.slots_no_array"), error_out);
	}

	struct Decl
	{
		std::string id;
		int32_t min = 0, max = -1;
		std::vector<std::string> formats;
	};
	std::vector<Decl> decls;
	for (cJSON *item = slots->child; item != nullptr; item = item->next)
	{
		cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
		if (!cJSON_IsString(id))
		{
			cJSON_Delete(root);
			return failInt(tr("project.slot_id"), error_out);
		}
		Decl d;
		d.id = id->valuestring;
		cJSON *mn = cJSON_GetObjectItemCaseSensitive(item, "min");
		cJSON *mx = cJSON_GetObjectItemCaseSensitive(item, "max");
		if (cJSON_IsNumber(mn)) d.min = static_cast<int32_t>(mn->valuedouble);
		if (cJSON_IsNumber(mx)) d.max = static_cast<int32_t>(mx->valuedouble);
		cJSON *formats = cJSON_GetObjectItemCaseSensitive(item, "formats");
		if (cJSON_IsArray(formats))
		{
			for (cJSON *f = formats->child; f != nullptr; f = f->next)
			{
				if (cJSON_IsString(f)) d.formats.push_back(f->valuestring);
			}
		}
		decls.push_back(std::move(d));
	}

	auto failV = [&](std::string message) -> int32_t
	{
		cJSON_Delete(root);
		return failInt(std::move(message), error_out);
	};

	/* every file fills a declared slot (support is the engine's own) */
	for (const FileEntry &e : p->files)
	{
		if (e.slot == "support") continue;
		const Decl *found = nullptr;
		for (const Decl &d : decls)
		{
			if (d.id == e.slot) { found = &d; break; }
		}
		if (found == nullptr) return failV(tr("project.undeclared_slot", {e.name, e.slot}));
		if (!found->formats.empty())
		{
			std::string ext = lowerExt(e.name);
			bool ok = false;
			for (const std::string &f : found->formats)
			{
				if (f == ext) { ok = true; break; }
			}
			if (!ok) return failV(tr("project.wrong_format", {e.name, e.slot}));
		}
	}

	/* a slot may be gated by the OTHER slots' contents (exposedWhen, the
	 * same condition language as firmware): files sitting in a slot the
	 * manifest itself makes unavailable are structurally invalid - a
	 * Famicom disk rules out a cartridge and vice versa. Exposure is
	 * decided FIRST because an unexposed slot's minimum does not bind:
	 * "cart min 1 unless fds" and "fds min 1 unless cart" together mean
	 * exactly one of either. */
	std::set<std::string> unexposed;
	{
		cJSON *slotMap = cJSON_CreateObject();
		for (const FileEntry &e : p->files)
		{
			if (e.slot == "support") continue;
			cJSON *arr = cJSON_GetObjectItemCaseSensitive(slotMap, e.slot.c_str());
			if (arr == nullptr) arr = cJSON_AddArrayToObject(slotMap, e.slot.c_str());
			cJSON_AddItemToArray(arr, cJSON_CreateString(e.name.c_str()));
		}
		for (cJSON *item = slots->child; item != nullptr; item = item->next)
		{
			cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "id");
			cJSON *when = cJSON_GetObjectItemCaseSensitive(item, "exposedWhen");
			if (!cJSON_IsString(id) || when == nullptr) continue;
			/* the settings the machine runs with: a slot gated on the machine
			 * setting (gpgx's per-machine cartridges) is exposed by what the
			 * project records, or by the package's default where it records
			 * nothing, not evaluated blind */
			if (ceEvalCondition(when, slotMap, settings)) continue;
			unexposed.insert(id->valuestring);
			for (const FileEntry &e : p->files)
			{
				if (e.slot != id->valuestring) continue;
				std::string bad = e.name;
				std::string slotName = id->valuestring;
				cJSON_Delete(slotMap);
				return failV(tr("project.slot_unavailable", {bad, slotName}));
			}
		}
		cJSON_Delete(slotMap);
	}

	/* cardinality per declared slot; an unexposed slot's minimum does not
	 * bind (it cannot be filled) */
	for (const Decl &d : decls)
	{
		int32_t count = 0;
		for (const FileEntry &e : p->files)
		{
			if (e.slot == d.id) count++;
		}
		if (count < d.min && unexposed.find(d.id) == unexposed.end())
		{
			return failV(tr("project.slot_needs", d.min, {d.id, std::to_string(count)}));
		}
		if (d.max >= 0 && count > d.max)
		{
			return failV(tr("project.slot_takes", d.max, {d.id, std::to_string(count)}));
		}
	}

	/* the declaration's cross-slot requirement: at least one file across
	 * each listed group */
	cJSON *groups = cJSON_GetObjectItemCaseSensitive(root, "atLeastOneOf");
	if (cJSON_IsArray(groups))
	{
		for (cJSON *group = groups->child; group != nullptr; group = group->next)
		{
			if (!cJSON_IsArray(group)) continue;
			int32_t count = 0;
			std::string names;
			for (cJSON *id = group->child; id != nullptr; id = id->next)
			{
				if (!cJSON_IsString(id)) continue;
				if (!names.empty()) names += ", ";
				names += id->valuestring;
				for (const FileEntry &e : p->files)
				{
					if (e.slot == id->valuestring) count++;
				}
			}
			if (count == 0) return failV(tr("new_project.one_across", {names}));
		}
	}

	cJSON_Delete(root);
	return 0;
}

const char *ce_project_slots_text(ce_project *p, uint64_t *len_out)
{
	cJSON *root = cJSON_CreateObject();
	for (const FileEntry &e : p->files)
	{
		if (e.slot == "support") continue;
		cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, e.slot.c_str());
		if (arr == nullptr) arr = cJSON_AddArrayToObject(root, e.slot.c_str());
		cJSON_AddItemToArray(arr, cJSON_CreateString(e.name.c_str()));
	}
	char *text = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	p->slotsOut = text != nullptr ? text : "{}";
	if (text != nullptr) cJSON_free(text);
	if (len_out != nullptr) *len_out = p->slotsOut.size();
	return p->slotsOut.c_str();
}

} // extern "C"

namespace
{
/* keys sorted, no whitespace: the same settings in any order are the same machine */
cJSON *canonicalJson(const cJSON *item)
{
	if (cJSON_IsObject(item))
	{
		std::vector<const cJSON *> members;
		for (const cJSON *c = item->child; c != nullptr; c = c->next) members.push_back(c);
		std::stable_sort(members.begin(), members.end(), [](const cJSON *a, const cJSON *b) {
			return std::strcmp(a->string != nullptr ? a->string : "", b->string != nullptr ? b->string : "") < 0;
		});
		cJSON *out = cJSON_CreateObject();
		for (const cJSON *m : members) cJSON_AddItemToObject(out, m->string != nullptr ? m->string : "", canonicalJson(m));
		return out;
	}
	if (cJSON_IsArray(item))
	{
		cJSON *out = cJSON_CreateArray();
		for (const cJSON *c = item->child; c != nullptr; c = c->next) cJSON_AddItemToArray(out, canonicalJson(c));
		return out;
	}
	return cJSON_Duplicate(item, 1);
}

std::string canonicalJsonText(const char *text)
{
	cJSON *parsed = cJSON_Parse(text != nullptr ? text : "");
	if (parsed == nullptr)
	{
		std::string t = text != nullptr ? text : "";
		while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
		size_t lead = 0;
		while (lead < t.size() && std::isspace(static_cast<unsigned char>(t[lead]))) lead++;
		return t.substr(lead);
	}
	cJSON *sorted = canonicalJson(parsed);
	char *printed = cJSON_PrintUnformatted(sorted);
	std::string out = printed != nullptr ? printed : "";
	cJSON_free(printed);
	cJSON_Delete(sorted);
	cJSON_Delete(parsed);
	return out;
}

std::string upperHex(const char *s)
{
	std::string out = s != nullptr ? s : "";
	for (char &c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return out;
}
} // namespace

extern "C" void ce_project_machine_id(ce_project *p, const char *running_core_sha1, char *out41)
{
	std::string id = "core=" + upperHex(running_core_sha1 != nullptr && *running_core_sha1 != '\0' ? running_core_sha1 : ce_project_core_sha1(p)) + "\n";
	id += "settings=" + canonicalJsonText(ce_project_settings_text(p, nullptr)) + "\n";
	id += "firmware=" + canonicalJsonText(ce_project_firmware_text(p, nullptr)) + "\n";
	/* another start for the machine's clock is another machine (issue 366); the
	 * sandbox's own adds nothing, so a cache made before the choice still matches */
	if (const int64_t clock = ce_project_clock_start(p); clock >= 0 && clock != CE_CLOCK_START_DEFAULT)
		id += "clock=" + std::to_string(clock) + "\n";
	for (int32_t i = 0; i < ce_project_file_count(p); i++)
	{
		const char *slot = ce_project_file_slot(p, i);
		id += "file=" + std::string(slot != nullptr ? slot : "") + ":" + upperHex(ce_project_file_sha1(p, i)) + "\n";
	}
	ce_sha1_hex(reinterpret_cast<const uint8_t *>(id.data()), id.size(), out41);
}

/* (project_check.hpp) */
int32_t ceProjectPack(const std::string &json, const std::string &input, const char *path, std::string &why)
{
	Parts parts;
	parts.json = json;
	parts.input = input;
	const char *error = nullptr;
	ce_project *p = parseProject(parts, nullptr, &error);
	if (p == nullptr)
	{
		why = error != nullptr ? error : "";
		return 1;
	}
	const int32_t rc = ce_project_save(p, path, &error);
	if (rc != 0) why = error != nullptr ? error : "";
	ce_project_free(p);
	return rc;
}
