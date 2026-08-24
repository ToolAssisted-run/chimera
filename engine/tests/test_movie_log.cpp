/* test_movie_log.cpp - pins the input-log format, byte for byte.
 *
 * The fixtures here are what the C# implementation produced and accepted; the
 * engine must match them exactly or existing movies stop being readable.
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static ce_movie_log *parsed(const char *text)
{
	ce_movie_log *log = ce_movie_log_new();
	int32_t rc = ce_movie_log_parse(log, text, std::strlen(text));
	assert(rc == 0);
	return log;
}

int main(void)
{
	assert(ce_abi_version() == CE_ABI_VERSION);
	assert(std::strstr(ce_build_info(), "\"component\":\"chimera engine\"") != nullptr);

	{ // a movie's Input Log.txt, exactly as the frontend writes it
		ce_movie_log *log = parsed("[Input]\nLogKey:#Reset|Power|#P1 Up|Down|Left|Right|Start|Select|B|A|\n|..|.U......|\n|..|........|\n[/Input]\n");
		assert(ce_movie_log_count(log) == 2);
		assert(std::strcmp(ce_movie_log_entry(log, 0), "|..|.U......|") == 0);
		assert(std::strcmp(ce_movie_log_entry(log, 1), "|..|........|") == 0);
		assert(std::strcmp(ce_movie_log_key(log), "#Reset|Power|#P1 Up|Down|Left|Right|Start|Select|B|A|") == 0);
		assert(!ce_movie_log_has_state_frame(log));
		assert(ce_movie_log_entry(log, 2) == nullptr);
		assert(ce_movie_log_entry(log, -1) == nullptr);

		// round trip: serializing what was parsed reproduces the file
		uint64_t len = 0;
		const char *text = ce_movie_log_serialize(log, 0, &len);
		assert(std::string(text, len)
			== "[Input]\nLogKey:#Reset|Power|#P1 Up|Down|Left|Right|Start|Select|B|A|\n|..|.U......|\n|..|........|\n[/Input]\n");
		ce_movie_log_free(log);
	}

	{ // CRLF input parses the same; CRLF output on request
		ce_movie_log *log = parsed("[Input]\r\nLogKey:#A|\r\n|.|\r\n[/Input]\r\n");
		assert(ce_movie_log_count(log) == 1);
		uint64_t len = 0;
		const char *text = ce_movie_log_serialize(log, 1, &len);
		assert(std::string(text, len) == "[Input]\r\nLogKey:#A|\r\n|.|\r\n[/Input]\r\n");
		ce_movie_log_free(log);
	}

	{ // savestate input block: Frame line present
		ce_movie_log *log = parsed("|.|\n|U|\nFrame 2\n");
		assert(ce_movie_log_has_state_frame(log));
		assert(ce_movie_log_state_frame(log) == 2);
		assert(ce_movie_log_count(log) == 2);
		ce_movie_log_free(log);
	}

	{ // quirk: unparseable Frame number is a hard error
		ce_movie_log *log = ce_movie_log_new();
		const char *bad = "|.|\nFrame x\n";
		assert(ce_movie_log_parse(log, bad, std::strlen(bad)) != 0);
		assert(std::strcmp(ce_movie_log_last_error(log), "Savestate Frame number failed to parse") == 0);
		const char *twoSpaces = "Frame  5\n"; // token between the spaces is empty
		assert(ce_movie_log_parse(log, twoSpaces, std::strlen(twoSpaces)) != 0);
		ce_movie_log_free(log);
	}

	{ // int.Parse semantics: sign and surrounding whitespace pass, 2^31 fails
		ce_movie_log *log = parsed("Frame +7\n");
		assert(ce_movie_log_state_frame(log) == 7);
		const char *big = "Frame 2147483648\n";
		assert(ce_movie_log_parse(log, big, std::strlen(big)) != 0);
		const char *max = "Frame 2147483647\n";
		assert(ce_movie_log_parse(log, max, std::strlen(max)) == 0);
		assert(ce_movie_log_state_frame(log) == 2147483647);
		ce_movie_log_free(log);
	}

	{ // quirks: last LogKey wins, and Replace() eats every marker occurrence
		ce_movie_log *log = parsed("LogKey:#first|\nLogKey:#second|\n");
		assert(std::strcmp(ce_movie_log_key(log), "#second|") == 0);
		ce_movie_log *odd = parsed("LogKey:aLogKey:b\n");
		assert(std::strcmp(ce_movie_log_key(odd), "ab") == 0);
		ce_movie_log_free(odd);
		ce_movie_log_free(log);
	}

	{ // no LogKey parsed -> key is NULL, serialize still writes the line
		ce_movie_log *log = parsed("|.|\n");
		assert(ce_movie_log_key(log) == nullptr);
		uint64_t len = 0;
		const char *text = ce_movie_log_serialize(log, 0, &len);
		assert(std::string(text, len) == "[Input]\nLogKey:\n|.|\n[/Input]\n");
		ce_movie_log_set_key(log, "#K|");
		text = ce_movie_log_serialize(log, 0, &len);
		assert(std::string(text, len) == "[Input]\nLogKey:#K|\n|.|\n[/Input]\n");
		ce_movie_log_free(log);
	}

	{ // a final line without a terminator still counts
		ce_movie_log *log = parsed("|.|\n|U|");
		assert(ce_movie_log_count(log) == 2);
		ce_movie_log_free(log);
	}

	{ // divergent point, all three cases
		ce_movie_log *a = parsed("|.|\n|U|\n");
		ce_movie_log *b = parsed("|.|\n|D|\n");
		ce_movie_log *prefix = parsed("|.|\n");
		assert(ce_movie_log_divergent_point(a, b) == 1);
		assert(ce_movie_log_divergent_point(a, prefix) == 1);
		assert(ce_movie_log_divergent_point(prefix, a) == 1);
		ce_movie_log_free(prefix);
		ce_movie_log_free(b);
		ce_movie_log_free(a);
	}
	{
		ce_movie_log *a = parsed("|.|\n|U|\n");
		ce_movie_log *b = parsed("|.|\n|U|\n");
		assert(ce_movie_log_divergent_point(a, b) == -1);
		ce_movie_log_free(b);
		ce_movie_log_free(a);
	}

	{ // parse replaces previous contents entirely
		ce_movie_log *log = parsed("|.|\n|U|\nFrame 1\nLogKey:#A|\n");
		const char *second = "|D|\n";
		assert(ce_movie_log_parse(log, second, std::strlen(second)) == 0);
		assert(ce_movie_log_count(log) == 1);
		assert(!ce_movie_log_has_state_frame(log));
		assert(ce_movie_log_key(log) == nullptr);
		ce_movie_log_free(log);
	}

	std::puts("test_movie_log: all ok");
	return 0;
}
