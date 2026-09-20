/* test_state_format.cpp - keeps the savestate format number honest (issue #115).
 *
 * Three jobs:
 *
 * 1. The layout fingerprint. kStateLayout spells out every piece of framing the
 *    format number stands for, and is built from the very magic strings the
 *    writers write, so renaming one moves the digest on its own. If this test
 *    fails, the layout moved: bump kStateFormat and paste the digest printed
 *    here into kStateLayoutDigest. Forgetting to bump is the bug this catches.
 *
 * 2. The state file's header: same-build round trip (which must ALWAYS work),
 *    a wrong format number (refused, both numbers named), no format number at
 *    all (read as format 1 - what the layout was the day the number arrived),
 *    and things that are not state files.
 *
 * 3. The sandbox's magics, checked against miniBox's own sources: a sandbox that
 *    renames its format has changed it, and this build's number must move with
 *    it. Skipped when the submodule is not checked out - what it guards is a
 *    developer's edit, not a user's run.
 */

#include "chimera/engine.h"

#include "../source/state_format.hpp"
#include "../source/sha1.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace chimera;

static std::vector<uint8_t> tagBytes()
{
	/* what WaterboxCore actually writes: a lag flag and two counters */
	return std::vector<uint8_t>{ 1, 0x0d, 0xf0, 0, 0, 0x2a, 0, 0, 0 };
}

/* the header a build before format numbers wrote: magic, compressed, tag */
static std::vector<uint8_t> legacyHeader(bool compressed, const std::vector<uint8_t> &tag)
{
	std::vector<uint8_t> out(CE_STATE_FILE_MAGIC_V1, CE_STATE_FILE_MAGIC_V1 + (sizeof CE_STATE_FILE_MAGIC_V1 - 1));
	out.push_back(compressed ? 1 : 0);
	const uint32_t n = static_cast<uint32_t>(tag.size());
	out.push_back(static_cast<uint8_t>(n & 0xffu));
	out.push_back(static_cast<uint8_t>((n >> 8) & 0xffu));
	out.push_back(static_cast<uint8_t>((n >> 16) & 0xffu));
	out.push_back(static_cast<uint8_t>((n >> 24) & 0xffu));
	out.insert(out.end(), tag.begin(), tag.end());
	return out;
}

static bool sourceSays(const char *relative, const char *needle)
{
	std::string path = std::string(CE_REPO_ROOT) + "/" + relative;
	std::FILE *f = std::fopen(path.c_str(), "rb");
	if (f == nullptr) return false;
	std::string all;
	char buf[65536];
	size_t n;
	while ((n = std::fread(buf, 1, sizeof buf, f)) != 0) all.append(buf, n);
	std::fclose(f);
	return all.find(needle) != std::string::npos;
}

int main(void)
{
	{ // 1. the fingerprint: the layout this build's number stands for
		const std::string digest = sha1Hex(
			reinterpret_cast<const uint8_t *>(kStateLayout), sizeof kStateLayout - 1);
		if (digest != kStateLayoutDigest)
		{
			std::fprintf(stderr,
				"\nThe savestate layout changed and the format number did not.\n\n"
				"  kStateFormat is %u\n"
				"  kStateLayoutDigest says %s\n"
				"  the layout now hashes to %s\n\n"
				"A state written under the old layout cannot be read under the new one, and\n"
				"nothing else tells the two apart. In source/engine/source/state_format.hpp:\n"
				"  - bump kStateFormat\n"
				"  - set kStateLayoutDigest to the hash above\n"
				"See docs/state-manager.md, \"The savestate format number\".\n\n",
				static_cast<unsigned>(kStateFormat), kStateLayoutDigest, digest.c_str());
			assert(!"the savestate layout moved without the format number");
		}
		/* the number is the ABI's too: the frontend records it in projects */
		assert(ce_state_format() == kStateFormat);
		assert(ce_state_writer_id() != nullptr && std::strlen(ce_state_writer_id()) != 0);
	}

	{ // 2a. same build, round trip - this must never fail
		for (bool compressed : { false, true })
		{
			StateFileHeader h;
			h.compressed = compressed;
			h.writer = stateWriterId();
			h.tag = tagBytes();
			std::vector<uint8_t> file;
			writeStateFileHeader(h, file);
			const size_t headerLen = file.size();
			const std::vector<uint8_t> body(4096, 0x5a); // the machine's bytes, whatever they are
			file.insert(file.end(), body.begin(), body.end());

			StateFileHeader back;
			std::string why;
			assert(readStateFileHeader(file.data(), file.size(), back, why) == 0);
			assert(why.empty());
			assert(back.format == kStateFormat);
			assert(back.writer == stateWriterId());
			assert(back.compressed == compressed);
			assert(back.tag == tagBytes());
			assert(back.length == headerLen); // and the machine's bytes start exactly there
		}
	}

	{ // 2b. a state of another format: refused, with both numbers named
		StateFileHeader h;
		h.format = kStateFormat + 7;
		h.writer = "Chimera commit deadbeef1234";
		h.tag = tagBytes();
		std::vector<uint8_t> file;
		writeStateFileHeader(h, file);
		file.resize(file.size() + 64, 0);

		StateFileHeader back;
		std::string why;
		assert(readStateFileHeader(file.data(), file.size(), back, why) == 1);
		assert(back.format == kStateFormat + 7);
		/* the sentence a person reads has to carry both numbers and both builds */
		assert(why.find(std::to_string(kStateFormat + 7)) != std::string::npos);
		assert(why.find(std::to_string(kStateFormat)) != std::string::npos);
		assert(why.find("Chimera commit deadbeef1234") != std::string::npos);
		assert(why.find(stateWriterId()) != std::string::npos);
		assert(why.find("not touched") != std::string::npos);
		assert(why[0] == 'T'); // a sentence a person reads, shown as it stands
	}

	{ // 2c. a state with NO format number: read as 1, the layout it was written under
		StateFileHeader back;
		std::string why;
		auto file = legacyHeader(true, tagBytes());
		const size_t headerLen = file.size();
		file.resize(file.size() + 32, 0xcc);
		const int rc = readStateFileHeader(file.data(), file.size(), back, why);
		assert(back.format == 1);
		assert(back.writer.empty());
		assert(back.compressed);
		assert(back.tag == tagBytes());
		assert(back.length == headerLen);
		if (kStateFormat == 1)
		{
			/* today: nothing in the wild is refused */
			assert(rc == 0 && why.empty());
		}
		else
		{
			/* after the first bump: refused, and it says the writer did not say */
			assert(rc == 1);
			assert(why.find("from before states said which") != std::string::npos);
		}
	}

	{ // 2d. not a state file at all, and one cut short
		StateFileHeader back;
		std::string why;
		const uint8_t junk[] = { 'P', 'K', 3, 4, 0, 0, 0, 0, 9, 9, 9, 9 };
		assert(readStateFileHeader(junk, sizeof junk, back, why) == 2);
		assert(why.find("not a state file") != std::string::npos);

		assert(readStateFileHeader(nullptr, 0, back, why) == 2);
		assert(readStateFileHeader(junk, 3, back, why) == 2);

		/* a real header with its tag chopped off is short, never "another format" */
		StateFileHeader h;
		h.writer = stateWriterId();
		h.tag = std::vector<uint8_t>(64, 7);
		std::vector<uint8_t> file;
		writeStateFileHeader(h, file);
		file.resize(file.size() - 10);
		assert(readStateFileHeader(file.data(), file.size(), back, why) == 2);
		assert(why.find("cut short") != std::string::npos);

		/* a tag length no tag could have is damage, not a format disagreement */
		std::vector<uint8_t> silly;
		writeStateFileHeader(h, silly);
		const size_t tagLenAt = silly.size() - h.tag.size() - 4;
		silly[tagLenAt + 3] = 0xff; // ~4 GB of "tag"
		assert(readStateFileHeader(silly.data(), silly.size(), back, why) == 2);
	}

	{ // 2e. the history id carries the format, so a history of another one is a cold cache
		const std::string suffix = historyFormatSuffix();
		assert(suffix == "#f" + std::to_string(kStateFormat));
		assert(suffix.find('#') == 0);
	}

	{ // 3. the date heuristic (ce_version_skew): a guess, and never a refusal
		/* far apart, either way round, and it names both sides and what to do */
		const char *msg = ce_version_skew("2026-09-20", "commit aaaaaaaaa", "2026-08-01", "commit bbbbbbbbb", "quickerNES");
		assert(msg != nullptr);
		const std::string m(msg);
		assert(m.find("2026-09-20") != std::string::npos);
		assert(m.find("2026-08-01") != std::string::npos);
		assert(m.find("commit aaaaaaaaa") != std::string::npos);
		assert(m.find("commit bbbbbbbbb") != std::string::npos);
		assert(m.find("quickerNES") != std::string::npos);
		assert(m.find("50 days apart") != std::string::npos);
		assert(m.find("Core Manager") != std::string::npos);
		assert(ce_version_skew("2026-08-01", "a", "2026-09-20", "b", "quickerNES") != nullptr);

		/* the same day, and anything inside the window, says nothing */
		assert(ce_version_skew("2026-09-20", "a", "2026-09-20", "b", "c") == nullptr);
		assert(ce_version_skew("2026-09-20", "a", "2026-09-07", "b", "c") == nullptr); // 13 days
		assert(ce_version_skew("2026-09-20", "a", "2026-09-06", "b", "c") != nullptr); // 14
		/* across a month and a year boundary, counted in days not in text */
		assert(ce_version_skew("2027-01-03", "a", "2026-12-25", "b", "c") == nullptr); // 9
		assert(ce_version_skew("2026-03-01", "a", "2026-02-14", "b", "c") != nullptr); // 15
		assert(ce_version_skew("2024-03-01", "a", "2024-02-16", "b", "c") != nullptr); // leap year: 14

		/* a side that does not say a date says nothing at all - never a guess */
		assert(ce_version_skew("unknown", "a", "2026-08-01", "b", "c") == nullptr);
		assert(ce_version_skew("2026-09-20", "a", "", "b", "c") == nullptr);
		assert(ce_version_skew(nullptr, nullptr, nullptr, nullptr, nullptr) == nullptr);
		assert(ce_version_skew("2026-9-20", "a", "2026-08-01", "b", "c") == nullptr);
		assert(ce_version_skew("2026-13-01", "a", "2026-08-01", "b", "c") == nullptr);
		assert(ce_version_skew("20260920", "a", "2026-08-01", "b", "c") == nullptr);

		/* no core name is still a sentence */
		const char *nameless = ce_version_skew("2026-09-20", "a", "2026-08-01", "b", "");
		assert(nameless != nullptr && std::string(nameless).find("the core package") != std::string::npos);
	}

	{ // 4. the sandbox still frames a state the way the layout says it does
		const char *host = "extern/chimera-common-minibox/source/host/host.c";
		const char *block = "extern/chimera-common-minibox/source/host/memblock.c";
		if (sourceSays(host, "static const char SAVE_START"))
		{
			assert(sourceSays(host, "\"" CE_SANDBOX_SAVE_MAGIC "\""));
			assert(sourceSays(host, "\"" CE_SANDBOX_DELTA_MAGIC "\""));
			assert(sourceSays(block, "\"" CE_SANDBOX_BLOCK_DELTA_MAGIC "\""));
			assert(sourceSays(block, "\"" CE_SANDBOX_BLOCK_MAGIC "\""));
		}
		else
		{
			std::fprintf(stderr, "miniBox sources not in the tree: sandbox magics not checked\n");
		}
	}

	std::printf("test_state_format: ok\n");
	return 0;
}
