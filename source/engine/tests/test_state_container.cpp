/* test_state_container.cpp - pins the zip-of-lumps container.
 *
 * Round trips through the engine, plus the naming and version quirks the C#
 * loader always had. Cross-implementation compatibility (a file written by
 * System.IO.Compression + streaming zstd) is pinned on the C# side, where
 * the old machinery still exists to fabricate such a file.
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::vector<uint8_t> bytes(const char *s)
{
	return std::vector<uint8_t>(s, s + std::strlen(s));
}

int main(void)
{
	{ // write, read back: plain and zstd lumps, all levels
		for (int32_t level : { 0, 1, 5, 9 })
		{
			ce_state_writer *w = ce_state_writer_new(level, "2.11 (chimera test)");
			auto header = bytes("MovieVersion Chimera v1.0.0\n\n");
			auto core = std::vector<uint8_t>(100000, 0xA5);
			core[77] = 3; // not purely repetitive
			assert(ce_state_writer_put_lump(w, "Header", "txt", 0, header.data(), header.size()) == 0);
			assert(ce_state_writer_put_lump(w, "Core", "bin", 1, core.data(), core.size()) == 0);
			assert(ce_state_writer_put_lump(w, "GreenZone", nullptr, 1, core.data(), 512) == 0);
			uint64_t zipLen = 0;
			const uint8_t *zip = ce_state_writer_finish(w, &zipLen);
			assert(zip != nullptr);
			if (level > 0) assert(zipLen < core.size()); // the state actually compressed

			const char *error = nullptr;
			ce_state_reader *r = ce_state_reader_open(zip, zipLen, 0, &error);
			assert(r != nullptr && error == nullptr);
			assert(ce_state_reader_version(r) == 3);

			uint64_t len = 0;
			const uint8_t *lump = ce_state_reader_lump(r, "Header", "txt", &len);
			assert(lump != nullptr && len == header.size() && std::memcmp(lump, header.data(), len) == 0);
			lump = ce_state_reader_lump(r, "Core", "bin", &len);
			assert(lump != nullptr && len == core.size() && std::memcmp(lump, core.data(), len) == 0);
			lump = ce_state_reader_lump(r, "GreenZone", nullptr, &len);
			assert(lump != nullptr && len == 512);
			lump = ce_state_reader_lump(r, "ChimeraVersion", "txt", &len);
			assert(lump != nullptr && std::string(lump, lump + len) == "2.11 (chimera test)\n");
			assert(ce_state_reader_lump(r, "Absent", "bin", &len) == nullptr);
			assert(ce_state_reader_last_error(r)[0] == '\0'); // absent, not broken
			ce_state_reader_free(r);
			ce_state_writer_free(w);
		}
	}

	{ // lookups are by name-before-the-first-dot, exactly as the C# resolved them
		ce_state_writer *w = ce_state_writer_new(1, "v");
		auto data = bytes("|..|\n");
		assert(ce_state_writer_put_lump(w, "Input Log", "txt", 0, data.data(), data.size()) == 0);
		assert(ce_state_writer_put_lump(w, "Branches/CoreData0", "bin", 1, data.data(), data.size()) == 0);
		uint64_t zipLen = 0;
		const uint8_t *zip = ce_state_writer_finish(w, &zipLen);
		ce_state_reader *r = ce_state_reader_open(zip, zipLen, 0, nullptr);
		assert(r != nullptr);
		uint64_t len = 0;
		assert(ce_state_reader_lump(r, "Input Log", "txt", &len) != nullptr);
		assert(ce_state_reader_lump(r, "Branches/CoreData0", "bin", &len) != nullptr);
		ce_state_reader_free(r);
		ce_state_writer_free(w);
	}

	{ // not a zip: the quiet NULL the old loader returned
		const char *error = reinterpret_cast<const char *>(1); // poison, must be cleared
		auto junk = bytes("MZ this is not a zip at all");
		assert(ce_state_reader_open(junk.data(), junk.size(), 0, &error) == nullptr);
		assert(error == nullptr);
	}

	{ // a zip with no version lump: NULL for states, version 1.0.0 for movies
		ce_state_writer *w = ce_state_writer_new(1, "v");
		// steal a valid archive, then strip is impossible here - instead build
		// a minimal zip by writing only through miniz via a fresh writer and
		// renaming trick is unavailable; so emulate with a raw stored zip:
		ce_state_writer_free(w);
		// [local header] "X.txt" stored, empty; [central dir]; [eocd] - minimal, handmade
		static const uint8_t rawZip[] = {
			0x50,0x4B,0x03,0x04, 20,0, 0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 5,0, 0,0,
			'X','.','t','x','t',
			0x50,0x4B,0x01,0x02, 20,0,20,0, 0,0, 0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
			5,0, 0,0, 0,0, 0,0, 0,0, 0,0,0,0, 0,0,0,0,
			'X','.','t','x','t',
			0x50,0x4B,0x05,0x06, 0,0, 0,0, 1,0, 1,0, 51,0,0,0, 35,0,0,0, 0,0,
		};
		assert(ce_state_reader_open(rawZip, sizeof rawZip, 0, nullptr) == nullptr);
		ce_state_reader *movie = ce_state_reader_open(rawZip, sizeof rawZip, 1, nullptr);
		assert(movie != nullptr);
		assert(ce_state_reader_version(movie) == 0);
		ce_state_reader_free(movie);
	}

	{ // duplicate lump names are corruption, reported not swallowed
		ce_state_writer *w = ce_state_writer_new(1, "v");
		auto data = bytes("x");
		// same KEY through different extensions: "Core.bin" and "Core.txt"
		assert(ce_state_writer_put_lump(w, "Core", "bin", 0, data.data(), data.size()) == 0);
		assert(ce_state_writer_put_lump(w, "Core", "txt", 0, data.data(), data.size()) == 0);
		uint64_t zipLen = 0;
		const uint8_t *zip = ce_state_writer_finish(w, &zipLen);
		const char *error = nullptr;
		assert(ce_state_reader_open(zip, zipLen, 0, &error) == nullptr);
		assert(error != nullptr && std::strstr(error, "Duplicate file") != nullptr);
		ce_state_writer_free(w);
	}

	std::puts("test_state_container: all ok");
	return 0;
}
