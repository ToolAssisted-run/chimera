/* movie_log.cpp - the [Input] lump of a movie.
 *
 * This replaces Bk2Movie.InputLog.cs's parsing and rendering, and reproduces it
 * exactly - including its quirks, which are named where they occur. Byte
 * compatibility with movies written by the C# implementation is the contract;
 * the fixtures in tests/test_movie_log.cpp pin it.
 */

#include "chimera/engine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

struct MovieLog
{
	std::vector<std::string> entries;
	std::optional<std::string> logKey;
	std::optional<int32_t> stateFrame;
	std::string lastError;
	std::string scratch; // backs the pointer returned by serialize
};

/* One line per call, consuming LF, CRLF or lone CR - the same three
 * terminators C#'s TextReader.ReadLine accepts. A final line without a
 * terminator still counts. */
bool nextLine(const char *text, uint64_t len, uint64_t &pos, std::string &line)
{
	if (pos >= len) return false;
	uint64_t start = pos;
	while (pos < len && text[pos] != '\n' && text[pos] != '\r') pos++;
	line.assign(text + start, pos - start);
	if (pos < len)
	{
		if (text[pos] == '\r' && pos + 1 < len && text[pos + 1] == '\n') pos++;
		pos++;
	}
	return true;
}

/* int.Parse(string) semantics for the "Frame N" token: surrounding whitespace
 * and a leading sign are fine, anything else (including an empty token) fails,
 * and the value must fit in 32 bits. */
bool parseInt32(const std::string &s, int32_t &out)
{
	size_t i = 0, end = s.size();
	while (i < end && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) i++;
	while (end > i && (s[end - 1] == ' ' || (s[end - 1] >= '\t' && s[end - 1] <= '\r'))) end--;
	if (i >= end) return false;
	bool negative = false;
	if (s[i] == '+' || s[i] == '-')
	{
		negative = s[i] == '-';
		if (++i >= end) return false;
	}
	int64_t value = 0;
	for (; i < end; i++)
	{
		if (s[i] < '0' || s[i] > '9') return false;
		value = value * 10 + (s[i] - '0');
		if (value > (negative ? 2147483648LL : 2147483647LL)) return false;
	}
	out = static_cast<int32_t>(negative ? -value : value);
	return true;
}

bool startsWith(const std::string &s, const char *prefix)
{
	return s.compare(0, std::strlen(prefix), prefix) == 0;
}

} // namespace

struct ce_movie_log : MovieLog
{
	/* The input journal (ce_movie_log_journal_open): every change to this log,
	 * appended as one line the moment it happens. */
	std::FILE *journal = nullptr;
	std::string journalPath;
	std::chrono::steady_clock::time_point journalSynced{};

	ce_movie_log() = default;
	ce_movie_log(const ce_movie_log &) = delete;
	ce_movie_log &operator=(const ce_movie_log &) = delete;
	~ce_movie_log()
	{
		if (journal != nullptr) std::fclose(journal);   /* kept on disk: only a close says remove */
	}
};

namespace {

/* ---- the input journal ----
 *
 * Why it exists: the inputs somebody has entered are the one thing a crash must
 * never take, and not every crash can be caught. A GPU driver that fast-fails, a
 * killed process, a power cut - none of them run a handler, so anything that
 * saves "on the way down" saves nothing. What survives all of them is bytes the
 * operating system already holds. So every change is written and FLUSHED as it
 * happens (a process that dies a moment later has still handed them over), and
 * synced to the disk at most once a second (a machine that loses power keeps all
 * but that second).
 *
 * The format is text, one record per line, and a journal alone rebuilds the log:
 * opening one writes the whole log first ("C", "K key", "A entry"...), then each
 * change follows. A record torn by the crash - a last line with no newline - is
 * simply not replayed. */
constexpr const char kJournalMagic[] = "CHIMERA-INPUT-JOURNAL 1";

void syncFile(std::FILE *f)
{
#ifdef _WIN32
	_commit(_fileno(f));
#else
	fsync(fileno(f));
#endif
}

/* An entry is one line of the movie file, so it cannot hold a line end; one that
 * somehow does is written with spaces rather than splitting the record. */
std::string oneLine(const char *text)
{
	std::string s = text != nullptr ? text : "";
	for (char &c : s)
	{
		if (c == '\n' || c == '\r') c = ' ';
	}
	return s;
}

/* Appends records (each already newline-terminated) and hands them to the OS. A
 * journal that cannot be written is closed rather than trusted half way: the
 * frontend's snapshot still stands, and a journal missing a change would replay
 * a timeline nobody made. */
void journalAppend(ce_movie_log *log, const std::string &records)
{
	if (log->journal == nullptr) return;
	if (std::fwrite(records.data(), 1, records.size(), log->journal) != records.size()
		|| std::fflush(log->journal) != 0)
	{
		std::fclose(log->journal);
		log->journal = nullptr;
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	if (now - log->journalSynced >= std::chrono::seconds(1))
	{
		syncFile(log->journal);
		log->journalSynced = now;
	}
}

std::string imageOf(const ce_movie_log *log, bool withMagic)
{
	std::string out;
	size_t bytes = 64;
	for (const auto &e : log->entries) bytes += e.size() + 3;
	out.reserve(bytes);
	if (withMagic) out.append(kJournalMagic).append("\n");
	out.append("C\n");
	if (log->logKey.has_value()) out.append("K ").append(oneLine(log->logKey->c_str())).append("\n");
	for (const auto &e : log->entries) out.append("A ").append(oneLine(e.c_str())).append("\n");
	return out;
}

bool readAllText(const std::string &path, std::string &out)
{
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) return false;
	char buf[1 << 16];
	size_t n;
	out.clear();
	while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
	std::fclose(f);
	return true;
}

bool parseInt64(const char *&p, int64_t &out)
{
	char *end = nullptr;
	const long long v = std::strtoll(p, &end, 10);
	if (end == p) return false;
	out = v;
	p = end;
	return true;
}

} // namespace

extern "C" {

ce_movie_log *ce_movie_log_new(void) { return new ce_movie_log(); }

void ce_movie_log_free(ce_movie_log *log) { delete log; }

int32_t ce_movie_log_journal_open(ce_movie_log *log, const char *path)
{
	if (log == nullptr || path == nullptr || path[0] == '\0') return 1;
	const std::string target = path;
	const std::string fresh = target + ".new";

	/* The whole log goes to a fresh file first and only then replaces the old
	 * journal: a crash half way through rewriting must still leave one journal
	 * that rebuilds everything. */
	std::FILE *f = std::fopen(fresh.c_str(), "wb");
	if (f == nullptr) return 1;
	const std::string image = imageOf(log, true);
	const bool written = std::fwrite(image.data(), 1, image.size(), f) == image.size() && std::fflush(f) == 0;
	if (written) syncFile(f);
	std::fclose(f);
	if (!written)
	{
		std::remove(fresh.c_str());
		return 1;
	}

	if (log->journal != nullptr)
	{
		std::fclose(log->journal);
		log->journal = nullptr;
	}
	std::error_code ec;
	std::filesystem::rename(fresh, target, ec);
	if (ec)
	{
		/* not every platform's rename replaces; the replay reads the ".new" file
		 * when the journal itself is missing, so this window is covered too */
		std::filesystem::remove(target, ec);
		std::filesystem::rename(fresh, target, ec);
		if (ec) return 1;
	}
	log->journal = std::fopen(target.c_str(), "ab");
	if (log->journal == nullptr) return 1;
	log->journalPath = target;
	log->journalSynced = std::chrono::steady_clock::now();
	return 0;
}

void ce_movie_log_journal_close(ce_movie_log *log, int32_t remove)
{
	if (log == nullptr) return;
	if (log->journal != nullptr)
	{
		std::fflush(log->journal);
		syncFile(log->journal);
		std::fclose(log->journal);
		log->journal = nullptr;
	}
	if (remove != 0 && !log->journalPath.empty())
	{
		std::remove(log->journalPath.c_str());
		std::remove((log->journalPath + ".new").c_str());
	}
	log->journalPath.clear();
}

int32_t ce_movie_log_journaling(const ce_movie_log *log)
{
	return log != nullptr && log->journal != nullptr ? 1 : 0;
}

int64_t ce_movie_log_journal_replay(ce_movie_log *log, const char *path)
{
	if (log == nullptr || path == nullptr) return -1;
	log->lastError.clear();
	std::string text;
	if (!readAllText(path, text) && !readAllText(std::string(path) + ".new", text))
	{
		log->lastError = "the input journal could not be read";
		return -1;
	}
	const size_t magicLen = std::strlen(kJournalMagic);
	if (text.compare(0, magicLen, kJournalMagic) != 0 || text.size() <= magicLen || text[magicLen] != '\n')
	{
		log->lastError = "that is not an input journal";
		return -1;
	}

	log->entries.clear();
	log->logKey.reset();
	log->stateFrame.reset();
	int64_t applied = 0;
	size_t pos = magicLen + 1;
	while (pos < text.size())
	{
		const size_t eol = text.find('\n', pos);
		if (eol == std::string::npos) break;   /* torn by the crash: never finished, never replayed */
		const std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;
		if (line.empty()) continue;

		const char op = line[0];
		const char *p = line.c_str() + 1;
		auto rest = [&]() { return std::string(*p == ' ' ? p + 1 : p); };
		const int64_t size = static_cast<int64_t>(log->entries.size());
		int64_t a = 0, b = 0;
		bool ok = true;
		switch (op)
		{
		case 'C':
			log->entries.clear();
			log->logKey.reset();
			break;
		case 'K':
			if (*p == '\0') log->logKey.reset();
			else log->logKey = rest();
			break;
		case 'A':
			log->entries.push_back(rest());
			break;
		case 'T':
			ok = parseInt64(p, a);
			if (ok && a < size) log->entries.resize(static_cast<size_t>(a < 0 ? 0 : a));
			break;
		case 'S':
			ok = parseInt64(p, a);
			if (ok && a >= 0 && a < size) log->entries[static_cast<size_t>(a)] = rest();
			break;
		case 'I':
			ok = parseInt64(p, a);
			if (ok && a >= 0 && a <= size) log->entries.insert(log->entries.begin() + static_cast<ptrdiff_t>(a), rest());
			break;
		case 'R':
			ok = parseInt64(p, a) && parseInt64(p, b);
			if (ok && a >= 0 && b > 0 && a < size)
			{
				if (b > size - a) b = size - a;
				log->entries.erase(log->entries.begin() + static_cast<ptrdiff_t>(a),
					log->entries.begin() + static_cast<ptrdiff_t>(a + b));
			}
			break;
		default:
			ok = false;
			break;
		}
		if (!ok)
		{
			/* everything before this record is what was entered; nothing after
			 * a record that cannot be read can be placed with any confidence */
			log->lastError = "the input journal has a record that cannot be read";
			break;
		}
		applied++;
	}
	return applied;
}

int32_t ce_movie_log_parse(ce_movie_log *log, const char *text, uint64_t len)
{
	log->lastError.clear();
	log->entries.clear();
	log->logKey.reset();
	log->stateFrame.reset();

	uint64_t pos = 0;
	std::string line;
	while (nextLine(text, len, pos, line))
	{
		if (!line.empty() && line[0] == '|')
		{
			log->entries.push_back(line);
		}
		else if (startsWith(line, "Frame "))
		{
			/* the token is whatever sits between the first space and the next
			 * (so "Frame  5" fails on the empty token, as it always has) */
			auto second = line.find(' ', 6);
			int32_t frame;
			if (!parseInt32(line.substr(6, second == std::string::npos ? second : second - 6), frame))
			{
				log->lastError = "Savestate Frame number failed to parse";
				journalAppend(log, imageOf(log, false));
				return 1;
			}
			log->stateFrame = frame;
		}
		else if (startsWith(line, "LogKey:"))
		{
			/* quirk: the C# used string.Replace, so EVERY occurrence of the
			 * marker vanishes from the line, not just the leading one */
			std::string key = line;
			for (auto at = key.find("LogKey:"); at != std::string::npos; at = key.find("LogKey:", at))
			{
				key.erase(at, 7);
			}
			log->logKey = key;
		}
	}
	journalAppend(log, imageOf(log, false));
	return 0;
}

const char *ce_movie_log_last_error(ce_movie_log *log) { return log->lastError.c_str(); }

int64_t ce_movie_log_count(const ce_movie_log *log) { return static_cast<int64_t>(log->entries.size()); }

const char *ce_movie_log_entry(const ce_movie_log *log, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(log->entries.size())) return nullptr;
	return log->entries[static_cast<size_t>(index)].c_str();
}

void ce_movie_log_add(ce_movie_log *log, const char *entry)
{
	log->entries.emplace_back(entry);
	if (log->journal != nullptr) journalAppend(log, "A " + oneLine(entry) + "\n");
}

void ce_movie_log_truncate(ce_movie_log *log, int64_t count)
{
	if (count < 0) count = 0;
	if (count < static_cast<int64_t>(log->entries.size()))
	{
		log->entries.resize(static_cast<size_t>(count));
		if (log->journal != nullptr) journalAppend(log, "T " + std::to_string(count) + "\n");
	}
}

void ce_movie_log_set(ce_movie_log *log, int64_t index, const char *entry)
{
	if (index < 0 || index >= static_cast<int64_t>(log->entries.size())) return;
	log->entries[static_cast<size_t>(index)] = entry;
	if (log->journal != nullptr) journalAppend(log, "S " + std::to_string(index) + " " + oneLine(entry) + "\n");
}

void ce_movie_log_insert(ce_movie_log *log, int64_t index, const char *entry)
{
	if (index < 0 || index > static_cast<int64_t>(log->entries.size())) return;
	log->entries.insert(log->entries.begin() + static_cast<ptrdiff_t>(index), entry);
	if (log->journal != nullptr) journalAppend(log, "I " + std::to_string(index) + " " + oneLine(entry) + "\n");
}

void ce_movie_log_remove_range(ce_movie_log *log, int64_t index, int64_t count)
{
	int64_t size = static_cast<int64_t>(log->entries.size());
	if (index < 0) { count += index; index = 0; }
	if (index >= size || count <= 0) return;
	if (count > size - index) count = size - index;
	log->entries.erase(
		log->entries.begin() + static_cast<ptrdiff_t>(index),
		log->entries.begin() + static_cast<ptrdiff_t>(index + count));
	if (log->journal != nullptr) journalAppend(log, "R " + std::to_string(index) + " " + std::to_string(count) + "\n");
}

void ce_movie_log_assign(ce_movie_log *dst, const ce_movie_log *src)
{
	dst->entries = src->entries;
	dst->logKey = src->logKey;
	if (dst->journal != nullptr) journalAppend(dst, imageOf(dst, false));
}

void ce_movie_log_clear(ce_movie_log *log)
{
	log->entries.clear();
	log->logKey.reset();
	log->stateFrame.reset();
	if (log->journal != nullptr) journalAppend(log, "C\n");
}

int32_t ce_movie_log_has_state_frame(const ce_movie_log *log) { return log->stateFrame.has_value() ? 1 : 0; }

int32_t ce_movie_log_state_frame(const ce_movie_log *log) { return log->stateFrame.value_or(0); }

const char *ce_movie_log_key(const ce_movie_log *log)
{
	return log->logKey.has_value() ? log->logKey->c_str() : nullptr;
}

void ce_movie_log_set_key(ce_movie_log *log, const char *key)
{
	if (key == nullptr) log->logKey.reset();
	else log->logKey = key;
	if (log->journal != nullptr) journalAppend(log, key == nullptr ? std::string("K\n") : "K " + oneLine(key) + "\n");
}

int64_t ce_movie_log_divergent_point(const ce_movie_log *a, const ce_movie_log *b)
{
	size_t max = a->entries.size() < b->entries.size() ? a->entries.size() : b->entries.size();
	for (size_t i = 0; i < max; i++)
	{
		if (a->entries[i] != b->entries[i]) return static_cast<int64_t>(i);
	}
	if (a->entries.size() != b->entries.size()) return static_cast<int64_t>(max);
	return -1;
}

const char *ce_movie_log_serialize(ce_movie_log *log, int32_t crlf, uint64_t *len_out)
{
	const char *eol = crlf ? "\r\n" : "\n";
	std::string &out = log->scratch;
	out.clear();
	out.append("[Input]").append(eol);
	out.append("LogKey:").append(log->logKey.value_or("")).append(eol);
	for (const auto &entry : log->entries) out.append(entry).append(eol);
	out.append("[/Input]").append(eol);
	if (len_out != nullptr) *len_out = out.size();
	return out.c_str();
}

} // extern "C"
