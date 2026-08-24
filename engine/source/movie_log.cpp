/* movie_log.cpp - the [Input] lump of a movie.
 *
 * This replaces Bk2Movie.InputLog.cs's parsing and rendering, and reproduces it
 * exactly - including its quirks, which are named where they occur. Byte
 * compatibility with movies written by the C# implementation is the contract;
 * the fixtures in tests/test_movie_log.cpp pin it.
 */

#include "chimera/engine.h"

#include <cstring>
#include <optional>
#include <string>
#include <vector>

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
};

extern "C" {

ce_movie_log *ce_movie_log_new(void) { return new ce_movie_log(); }

void ce_movie_log_free(ce_movie_log *log) { delete log; }

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
	return 0;
}

const char *ce_movie_log_last_error(ce_movie_log *log) { return log->lastError.c_str(); }

int64_t ce_movie_log_count(const ce_movie_log *log) { return static_cast<int64_t>(log->entries.size()); }

const char *ce_movie_log_entry(const ce_movie_log *log, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(log->entries.size())) return nullptr;
	return log->entries[static_cast<size_t>(index)].c_str();
}

void ce_movie_log_add(ce_movie_log *log, const char *entry) { log->entries.emplace_back(entry); }

void ce_movie_log_clear(ce_movie_log *log)
{
	log->entries.clear();
	log->logKey.reset();
	log->stateFrame.reset();
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
