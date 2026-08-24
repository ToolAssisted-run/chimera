/* movie_text.cpp - the movie's text lumps: Header.txt, Comments.txt, and
 * subtitle lines.
 *
 * Replaces the parsing in BasicMovieInfo.cs and the rendering in
 * Bk2Header/Bk2Movie.HeaderApi/SubtitleList, byte for byte - quirks are named
 * where they are reproduced. One deliberate stabilisation: header order on
 * write is insertion order, where the old Dictionary-backed writer left it
 * unspecified.
 */

#include "chimera/engine.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

/* One line per call: LF, CRLF or lone CR, a terminatorless tail still counts.
 * (Same helper as movie_log.cpp; worth sharing once a third file needs it.) */
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

/* string.IsNullOrWhiteSpace, for the ASCII whitespace movies actually contain.
 * (C# also treats exotic Unicode spaces as blank; a comment line made only of
 * those would now be kept instead of dropped. Nothing writes such lines.) */
bool isBlank(const std::string &s)
{
	for (char c : s)
	{
		if (c != ' ' && (c < '\t' || c > '\r')) return false;
	}
	return true;
}

/* int.Parse(string): surrounding whitespace and a leading sign allowed. */
bool parseInt32(const char *s, size_t n, int32_t &out)
{
	size_t i = 0;
	while (i < n && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) i++;
	while (n > i && (s[n - 1] == ' ' || (s[n - 1] >= '\t' && s[n - 1] <= '\r'))) n--;
	if (i >= n) return false;
	bool negative = false;
	if (s[i] == '+' || s[i] == '-')
	{
		negative = s[i] == '-';
		if (++i >= n) return false;
	}
	int64_t value = 0;
	for (; i < n; i++)
	{
		if (s[i] < '0' || s[i] > '9') return false;
		value = value * 10 + (s[i] - '0');
		if (value > (negative ? 2147483648LL : 2147483647LL)) return false;
	}
	out = static_cast<int32_t>(negative ? -value : value);
	return true;
}

/* uint.Parse(s, NumberStyles.HexNumber): whitespace allowed, sign not. */
bool parseHex32(const char *s, size_t n, uint32_t &out)
{
	size_t i = 0;
	while (i < n && (s[i] == ' ' || (s[i] >= '\t' && s[i] <= '\r'))) i++;
	while (n > i && (s[n - 1] == ' ' || (s[n - 1] >= '\t' && s[n - 1] <= '\r'))) n--;
	if (i >= n) return false;
	uint64_t value = 0;
	for (; i < n; i++)
	{
		int digit;
		if (s[i] >= '0' && s[i] <= '9') digit = s[i] - '0';
		else if (s[i] >= 'a' && s[i] <= 'f') digit = s[i] - 'a' + 10;
		else if (s[i] >= 'A' && s[i] <= 'F') digit = s[i] - 'A' + 10;
		else return false;
		value = value * 16 + digit;
		if (value > 0xFFFFFFFFULL) return false;
	}
	out = static_cast<uint32_t>(value);
	return true;
}

} // namespace

/* ---- ce_movie_header ---- */

struct ce_movie_header
{
	std::vector<std::pair<std::string, std::string>> pairs;
	std::string scratch;
};

extern "C" {

ce_movie_header *ce_movie_header_new(void) { return new ce_movie_header(); }

void ce_movie_header_free(ce_movie_header *header) { delete header; }

void ce_movie_header_parse(ce_movie_header *header, const char *text, uint64_t len)
{
	header->pairs.clear();
	uint64_t pos = 0;
	std::string line;
	while (nextLine(text, len, pos, line))
	{
		if (isBlank(line)) continue;
		/* net48 Split(' ', 2, RemoveEmptyEntries): leading separator runs are
		 * eaten, the key is the first space-free token, and the value is the
		 * REST of the line from the next non-space character - its own
		 * trailing spaces kept. A line with no second token has no value and
		 * is skipped, as it always was. */
		size_t keyStart = line.find_first_not_of(' ');
		if (keyStart == std::string::npos) continue;
		size_t keyEnd = line.find(' ', keyStart);
		if (keyEnd == std::string::npos) continue;
		size_t valueStart = line.find_first_not_of(' ', keyEnd);
		if (valueStart == std::string::npos) continue;
		std::string key = line.substr(keyStart, keyEnd - keyStart);
		/* quirk: first occurrence of a key wins */
		bool seen = false;
		for (const auto &kv : header->pairs)
		{
			if (kv.first == key) { seen = true; break; }
		}
		if (!seen) header->pairs.emplace_back(std::move(key), line.substr(valueStart));
	}
}

int64_t ce_movie_header_count(const ce_movie_header *header)
{
	return static_cast<int64_t>(header->pairs.size());
}

const char *ce_movie_header_key_at(const ce_movie_header *header, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(header->pairs.size())) return nullptr;
	return header->pairs[static_cast<size_t>(index)].first.c_str();
}

const char *ce_movie_header_value_at(const ce_movie_header *header, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(header->pairs.size())) return nullptr;
	return header->pairs[static_cast<size_t>(index)].second.c_str();
}

void ce_movie_header_set(ce_movie_header *header, const char *key, const char *value)
{
	for (auto &kv : header->pairs)
	{
		if (kv.first == key)
		{
			kv.second = value;
			return;
		}
	}
	header->pairs.emplace_back(key, value);
}

const char *ce_movie_header_serialize(ce_movie_header *header, int32_t crlf, uint64_t *len_out)
{
	const char *eol = crlf ? "\r\n" : "\n";
	std::string &out = header->scratch;
	out.clear();
	for (const auto &kv : header->pairs)
	{
		out.append(kv.first).append(1, ' ').append(kv.second).append(eol);
	}
	out.append(eol); // the closing blank line every text lump carries
	if (len_out != nullptr) *len_out = out.size();
	return out.c_str();
}

} // extern "C"

/* ---- ce_text_lines ---- */

struct ce_text_lines
{
	std::vector<std::string> lines;
	std::string scratch;
};

extern "C" {

ce_text_lines *ce_text_lines_new(void) { return new ce_text_lines(); }

void ce_text_lines_free(ce_text_lines *lines) { delete lines; }

void ce_text_lines_parse(ce_text_lines *lines, const char *text, uint64_t len)
{
	lines->lines.clear();
	uint64_t pos = 0;
	std::string line;
	while (nextLine(text, len, pos, line))
	{
		if (!isBlank(line)) lines->lines.push_back(line);
	}
}

int64_t ce_text_lines_count(const ce_text_lines *lines)
{
	return static_cast<int64_t>(lines->lines.size());
}

const char *ce_text_lines_at(const ce_text_lines *lines, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(lines->lines.size())) return nullptr;
	return lines->lines[static_cast<size_t>(index)].c_str();
}

void ce_text_lines_add(ce_text_lines *lines, const char *line) { lines->lines.emplace_back(line); }

const char *ce_text_lines_serialize(ce_text_lines *lines, int32_t crlf, uint64_t *len_out)
{
	const char *eol = crlf ? "\r\n" : "\n";
	std::string &out = lines->scratch;
	out.clear();
	for (const auto &line : lines->lines) out.append(line).append(eol);
	out.append(eol);
	if (len_out != nullptr) *len_out = out.size();
	return out.c_str();
}

/* ---- subtitle lines ---- */

int64_t ce_subtitle_parse_line(
	const char *line, ce_subtitle_fields *fields, char *message_buf, uint64_t cap)
{
	/* Split(' ') with empties kept: fields at part indices 1-5 (part 0, the
	 * word "subtitle", was never actually checked), message = parts 6+ joined
	 * by single spaces then trimmed - which preserves interior runs, because
	 * a run of N spaces becomes N-1 empty parts joined by spaces again. */
	std::vector<std::pair<const char *, size_t>> parts;
	const char *p = line;
	for (;;)
	{
		const char *sep = std::strchr(p, ' ');
		if (sep == nullptr)
		{
			parts.emplace_back(p, std::strlen(p));
			break;
		}
		parts.emplace_back(p, static_cast<size_t>(sep - p));
		p = sep + 1;
	}
	if (parts.size() < 6) return -1;
	ce_subtitle_fields parsed;
	if (!parseInt32(parts[1].first, parts[1].second, parsed.frame)
		|| !parseInt32(parts[2].first, parts[2].second, parsed.x)
		|| !parseInt32(parts[3].first, parts[3].second, parsed.y)
		|| !parseInt32(parts[4].first, parts[4].second, parsed.duration)
		|| !parseHex32(parts[5].first, parts[5].second, parsed.color))
	{
		return -1;
	}
	std::string message;
	for (size_t i = 6; i < parts.size(); i++)
	{
		message.append(parts[i].first, parts[i].second).append(1, ' ');
	}
	while (!message.empty()
		&& (message.back() == ' ' || (message.back() >= '\t' && message.back() <= '\r')))
	{
		message.pop_back();
	}
	size_t lead = 0;
	while (lead < message.size()
		&& (message[lead] == ' ' || (message[lead] >= '\t' && message[lead] <= '\r')))
	{
		lead++;
	}
	message.erase(0, lead);

	*fields = parsed;
	if (message.size() + 1 <= cap)
	{
		std::memcpy(message_buf, message.data(), message.size());
		message_buf[message.size()] = '\0';
	}
	return static_cast<int64_t>(message.size());
}

int64_t ce_subtitle_format_line(
	const ce_subtitle_fields *fields, const char *message, char *buf, uint64_t cap)
{
	char head[96];
	int headLen = std::snprintf(
		head, sizeof head, "subtitle %" PRId32 " %" PRId32 " %" PRId32 " %" PRId32 " %08" PRIX32 " ",
		fields->frame, fields->x, fields->y, fields->duration, fields->color);
	uint64_t total = static_cast<uint64_t>(headLen) + std::strlen(message);
	if (total + 1 <= cap)
	{
		std::memcpy(buf, head, static_cast<size_t>(headLen));
		std::strcpy(buf + headLen, message);
	}
	return static_cast<int64_t>(total);
}

} // extern "C"
