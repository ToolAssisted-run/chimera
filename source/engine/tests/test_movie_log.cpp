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

	{ // the editing ops TAStudio leans on: set, insert, remove_range, assign
		ce_movie_log *log = parsed("|A|\n|B|\n|C|\n|D|\n");
		ce_movie_log_set(log, 1, "|X|");
		assert(std::strcmp(ce_movie_log_entry(log, 1), "|X|") == 0);
		ce_movie_log_set(log, 99, "|nope|"); // out of range: ignored
		assert(ce_movie_log_count(log) == 4);
		ce_movie_log_insert(log, 0, "|first|");
		ce_movie_log_insert(log, 5, "|last|"); // == count: append
		assert(ce_movie_log_count(log) == 6);
		assert(std::strcmp(ce_movie_log_entry(log, 0), "|first|") == 0);
		assert(std::strcmp(ce_movie_log_entry(log, 5), "|last|") == 0);
		ce_movie_log_remove_range(log, 1, 2); // drops |A|,|X|
		assert(ce_movie_log_count(log) == 4);
		assert(std::strcmp(ce_movie_log_entry(log, 1), "|C|") == 0);
		ce_movie_log_remove_range(log, 2, 100); // clamped
		assert(ce_movie_log_count(log) == 2);
		ce_movie_log_truncate(log, 1);
		assert(ce_movie_log_count(log) == 1);

		ce_movie_log *copy = ce_movie_log_new();
		ce_movie_log_set_key(log, "#K|");
		ce_movie_log_assign(copy, log);
		assert(ce_movie_log_count(copy) == 1);
		assert(std::strcmp(ce_movie_log_key(copy), "#K|") == 0);
		assert(ce_movie_log_divergent_point(copy, log) == -1);
		ce_movie_log_free(copy);
		ce_movie_log_free(log);
	}

	{ // The input journal: every change survives the process, and a journal alone
	  // rebuilds the log. Whatever kills the process - a GPU driver fast-failing, a
	  // kill, a power cut - the inputs entered up to that moment are on disk.
		const char *path = "work-movie-journal.txt";
		const char *fresh = "work-movie-journal.txt.new";
		std::remove(path);
		std::remove(fresh);
		auto same = [](ce_movie_log *a, ce_movie_log *b) {
			if (ce_movie_log_count(a) != ce_movie_log_count(b)) return false;
			for (int64_t i = 0; i < ce_movie_log_count(a); i++)
			{
				if (std::strcmp(ce_movie_log_entry(a, i), ce_movie_log_entry(b, i)) != 0) return false;
			}
			const char *ka = ce_movie_log_key(a), *kb = ce_movie_log_key(b);
			return (ka == nullptr) == (kb == nullptr) && (ka == nullptr || std::strcmp(ka, kb) == 0);
		};
		auto replayed = [&](const char *from, int64_t *records = nullptr) {
			ce_movie_log *back = ce_movie_log_new();
			const int64_t n = ce_movie_log_journal_replay(back, from);
			if (records != nullptr) *records = n;
			return back;
		};

		ce_movie_log *log = parsed("[Input]\nLogKey:#P1 A|\n|.|\n|A|\n[/Input]\n");
		assert(!ce_movie_log_journaling(log));
		assert(ce_movie_log_journal_open(log, path) == 0);
		assert(ce_movie_log_journaling(log));

		// every kind of change, each on disk the moment it returns
		ce_movie_log_add(log, "|A|");
		ce_movie_log_set(log, 0, "|B|");
		ce_movie_log_insert(log, 1, "|C|");
		ce_movie_log_remove_range(log, 2, 1);
		ce_movie_log_add(log, "|D|");
		ce_movie_log_truncate(log, 3);
		ce_movie_log_set_key(log, "#P1 B|");
		ce_movie_log_insert(log, 3, "|E|");     // an insert at the end is an append
		ce_movie_log_remove_range(log, -1, 2);  // clamped: removes entry 0
		ce_movie_log_set(log, 99, "|nope|");    // out of range: no change, nothing journaled
		ce_movie_log_insert(log, 99, "|nope|");
		{
			int64_t records = 0;
			ce_movie_log *back = replayed(path, &records);
			assert(records > 0);
			assert(same(back, log));
			ce_movie_log_free(back);
		}

		// a record the crash cut short - no line end - is not replayed
		{
			std::FILE *f = std::fopen(path, "ab");
			std::fputs("A |torn", f);
			std::fclose(f);
			ce_movie_log *back = replayed(path);
			assert(same(back, log));
			ce_movie_log_free(back);
		}

		// reopening rewrites the image (how the journal is kept short) and loses nothing
		assert(ce_movie_log_journal_open(log, path) == 0);
		ce_movie_log_add(log, "|F|");
		{
			ce_movie_log *back = replayed(path);
			assert(same(back, log));
			ce_movie_log_free(back);
		}

		// a parse and an assign replace the whole log, and the journal says so
		assert(ce_movie_log_parse(log, "[Input]\nLogKey:#X|\n|1|\n|2|\n[/Input]\n", 34) == 0);
		{
			ce_movie_log *back = replayed(path);
			assert(same(back, log));
			ce_movie_log_free(back);
		}
		ce_movie_log *other = parsed("[Input]\nLogKey:#Y|\n|9|\n[/Input]\n");
		ce_movie_log_assign(log, other);
		ce_movie_log_clear(other);
		ce_movie_log_add(log, "|10|");
		{
			ce_movie_log *back = replayed(path);
			assert(same(back, log));
			assert(ce_movie_log_count(back) == 2);
			ce_movie_log_free(back);
		}
		ce_movie_log_free(other);

		// a clear is a change like any other
		ce_movie_log_clear(log);
		ce_movie_log_add(log, "|after clear|");
		{
			ce_movie_log *back = replayed(path);
			assert(same(back, log));
			assert(ce_movie_log_key(back) == nullptr);
			ce_movie_log_free(back);
		}

		// the crash itself: the log is gone without a close, and the file is not
		ce_movie_log *kept = ce_movie_log_new();
		ce_movie_log_assign(kept, log);
		ce_movie_log_free(log);
		{
			ce_movie_log *back = replayed(path);
			assert(same(back, kept));
			ce_movie_log_free(back);
		}

		// a journal caught between its rewrite and its rename is read from the fresh file
		std::rename(path, fresh);
		{
			ce_movie_log *back = replayed(path);
			assert(same(back, kept));
			ce_movie_log_free(back);
		}
		std::remove(fresh);

		// nothing to rebuild from, or something that is not a journal
		{
			int64_t records = 0;
			ce_movie_log *back = replayed(path, &records);
			assert(records == -1);
			assert(std::strlen(ce_movie_log_last_error(back)) > 0);
			ce_movie_log_free(back);
			std::FILE *f = std::fopen(path, "wb");
			std::fputs("[Input]\n|1|\n", f);
			std::fclose(f);
			back = replayed(path, &records);
			assert(records == -1);
			ce_movie_log_free(back);
		}

		// a session that ended cleanly leaves nothing behind
		ce_movie_log *clean = parsed("[Input]\nLogKey:#P1 A|\n|.|\n[/Input]\n");
		assert(ce_movie_log_journal_open(clean, path) == 0);
		ce_movie_log_add(clean, "|A|");
		ce_movie_log_journal_close(clean, 1);
		assert(!ce_movie_log_journaling(clean));
		assert(std::fopen(path, "rb") == nullptr);
		ce_movie_log_add(clean, "|not journaled|");   // closed: changes go nowhere, and nothing breaks
		ce_movie_log_free(clean);
		ce_movie_log_free(kept);
	}

	std::puts("test_movie_log: all ok");
	return 0;
}
