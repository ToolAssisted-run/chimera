/* test_movie_text.cpp - pins Header.txt, Comments.txt and subtitle lines,
 * byte for byte against what the C# implementation wrote and accepted.
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static std::string serialized(ce_movie_header *h, int crlf)
{
	uint64_t len = 0;
	const char *text = ce_movie_header_serialize(h, crlf, &len);
	return std::string(text, len);
}

int main(void)
{
	{ // a real header, as the frontend writes it (with the closing blank line)
		const char *lump =
			"MovieVersion Chimera v1.0.0\nAuthor jaffar\nPlatform NES\nCore QuickerNesHawk\n\n";
		ce_movie_header *h = ce_movie_header_new();
		ce_movie_header_parse(h, lump, std::strlen(lump));
		assert(ce_movie_header_count(h) == 4);
		assert(std::strcmp(ce_movie_header_key_at(h, 0), "MovieVersion") == 0);
		assert(std::strcmp(ce_movie_header_value_at(h, 0), "Chimera v1.0.0") == 0);
		assert(std::strcmp(ce_movie_header_key_at(h, 3), "Core") == 0);
		assert(ce_movie_header_key_at(h, 4) == nullptr);
		assert(serialized(h, 0) == lump); // round trip
		ce_movie_header_free(h);
	}

	{ // net48 Split quirks: separator runs eaten before the value, kept after
		const char *lump = "Key   spaced  value \n Indented v\nBare\n   \nLast one\n";
		ce_movie_header *h = ce_movie_header_new();
		ce_movie_header_parse(h, lump, std::strlen(lump));
		assert(ce_movie_header_count(h) == 3); // "Bare" (no value) and blank line skipped
		assert(std::strcmp(ce_movie_header_value_at(h, 0), "spaced  value ") == 0);
		assert(std::strcmp(ce_movie_header_key_at(h, 1), "Indented") == 0);
		assert(std::strcmp(ce_movie_header_key_at(h, 2), "Last") == 0);
		ce_movie_header_free(h);
	}

	{ // quirk: first occurrence of a key wins on parse; set() overwrites
		const char *lump = "Author first\nAuthor second\n";
		ce_movie_header *h = ce_movie_header_new();
		ce_movie_header_parse(h, lump, std::strlen(lump));
		assert(ce_movie_header_count(h) == 1);
		assert(std::strcmp(ce_movie_header_value_at(h, 0), "first") == 0);
		ce_movie_header_set(h, "Author", "third");
		ce_movie_header_set(h, "Platform", "NES");
		assert(serialized(h, 0) == "Author third\nPlatform NES\n\n");
		assert(serialized(h, 1) == "Author third\r\nPlatform NES\r\n\r\n");
		ce_movie_header_free(h);
	}

	{ // comments lump: order and duplicates kept, blanks dropped
		const char *lump = "comment one\n\ncomment one\n  \ncomment two\n\n";
		ce_text_lines *lines = ce_text_lines_new();
		ce_text_lines_parse(lines, lump, std::strlen(lump));
		assert(ce_text_lines_count(lines) == 3);
		assert(std::strcmp(ce_text_lines_at(lines, 1), "comment one") == 0);
		uint64_t len = 0;
		const char *text = ce_text_lines_serialize(lines, 0, &len);
		assert(std::string(text, len) == "comment one\ncomment one\ncomment two\n\n");
		ce_text_lines_free(lines);
	}

	{ // subtitle line, the format the frontend writes: trailing space head + message
		ce_subtitle_fields fields;
		char msg[256];
		int64_t n = ce_subtitle_parse_line("subtitle 100 10 20 120 FFFFFFFF hello world", &fields, msg, sizeof msg);
		assert(n == 11);
		assert(std::strcmp(msg, "hello world") == 0);
		assert(fields.frame == 100 && fields.x == 10 && fields.y == 20 && fields.duration == 120);
		assert(fields.color == 0xFFFFFFFFu);

		char line[256];
		int64_t m = ce_subtitle_format_line(&fields, msg, line, sizeof line);
		assert(m == static_cast<int64_t>(std::strlen("subtitle 100 10 20 120 FFFFFFFF hello world")));
		assert(std::strcmp(line, "subtitle 100 10 20 120 FFFFFFFF hello world") == 0);

		// empty message: the head's trailing space stays, as C# always wrote it
		int64_t e = ce_subtitle_format_line(&fields, "", line, sizeof line);
		assert(std::strcmp(line, "subtitle 100 10 20 120 FFFFFFFF ") == 0);
		assert(e == static_cast<int64_t>(std::strlen(line)));
	}

	{ // message quirk: interior space runs survive the join, ends are trimmed
		ce_subtitle_fields fields;
		char msg[256];
		int64_t n = ce_subtitle_parse_line("subtitle 1 2 3 4 00FF00FF  a  b \t", &fields, msg, sizeof msg);
		assert(n >= 0);
		assert(std::strcmp(msg, "a  b") == 0);
		assert(fields.color == 0x00FF00FFu);
	}

	{ // the leading word was never checked; garbage lines are refused, not fatal
		ce_subtitle_fields fields;
		char msg[8];
		assert(ce_subtitle_parse_line("anything 1 2 3 4 AABBCCDD ok", &fields, msg, sizeof msg) == 2);
		assert(ce_subtitle_parse_line("subtitle x 2 3 4 AABBCCDD nope", &fields, msg, sizeof msg) == -1);
		assert(ce_subtitle_parse_line("subtitle 1 2 3 4", &fields, msg, sizeof msg) == -1);
		assert(ce_subtitle_parse_line("subtitle 1 2 3 4 -1 sign", &fields, msg, sizeof msg) == -1);
		// a too-small buffer still reports the true length
		assert(ce_subtitle_parse_line("subtitle 1 2 3 4 AABBCCDD a long message", &fields, msg, sizeof msg) == 14);
	}

	std::puts("test_movie_text: all ok");
	return 0;
}
