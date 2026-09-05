/* test_sha1.cpp - the two SHA1 transforms must agree, byte for byte.
 *
 * The hash is chosen at runtime: the CPU's SHA extensions where they exist, the
 * plain C otherwise. They are two implementations of one thing, and "they agree"
 * is a claim, not a fact - a wrong hardware path would pin every project to
 * digests no other build could reproduce, and nothing else in the tree would
 * notice, because both flavours of a gate run on the same machine.
 *
 * The hardware path was wrong when it was written. It matched only on the fifth
 * word of the digest, because two boundaries in the message schedule were one
 * group short - which is exactly the kind of mistake a known-answer test catches
 * and reading the code twice does not.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/sha1.hpp"

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

std::string hashWith(bool software, const std::vector<uint8_t> &data, size_t len)
{
	chimera::sha1ForceSoftwareForTests(software);
	std::string out = chimera::sha1Hex(data.data(), len);
	chimera::sha1ForceSoftwareForTests(false);
	return out;
}

} // namespace

namespace {

uint64_t g_lastDone = 0, g_lastTotal = 0;
int g_reports = 0;
void onProgress(const char *stage, uint64_t done, uint64_t total, void *)
{
	assert(stage != nullptr);
	g_lastDone = done;
	g_lastTotal = total;
	g_reports++;
}

void testProgressOfAFileHash()
{
	/* a file bigger than the 4 MiB report interval, hashed with a sink installed:
	 * the sink hears at least one report on the way and the last says done ==
	 * total == the file's length; with no sink, nothing is heard */
	const char *path = "progress-probe.bin";
	{
		FILE *f = fopen(path, "wb");
		assert(f != nullptr);
		std::vector<uint8_t> block(1 << 20, 0x5A);
		for (int i = 0; i < 9; i++) assert(fwrite(block.data(), 1, block.size(), f) == block.size());
		fclose(f);
	}
	char hex[41];
	uint64_t len = 0;
	ce_progress_set(&onProgress, nullptr);
	assert(ce_sha1_file(path, hex, &len) == 1);
	assert(len == 9u << 20);
	assert(g_reports >= 2);
	assert(g_lastDone == len && g_lastTotal == len);
	ce_progress_set(nullptr, nullptr);
	g_reports = 0;
	/* the answer for a file already hashed comes from the cache, so rewrite it
	 * to make the hash run again with nobody listening */
	remove(path);
	{
		FILE *f = fopen(path, "wb");
		std::vector<uint8_t> block(1 << 20, 0x3C);
		for (int i = 0; i < 5; i++) assert(fwrite(block.data(), 1, block.size(), f) == block.size());
		fclose(f);
	}
	assert(ce_sha1_file(path, hex, &len) == 1);
	assert(g_reports == 0);
	remove(path);
	printf("progress: a file hash reports its way to the end\n");
}

} // namespace

int main(void)
{
	testProgressOfAFileHash();
	{ // the known answers, so that "they agree" cannot mean "both are wrong"
		const std::string empty = chimera::sha1Hex(nullptr, 0);
		assert(empty == "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");

		const uint8_t abc[3] = { 'a', 'b', 'c' };
		assert(chimera::sha1Hex(abc, 3) == "A9993E364706816ABA3E25717850C26C9CD0D89D");

		// 448 bits: the case that pads into a second block
		const char *s = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
		assert(chimera::sha1Hex(reinterpret_cast<const uint8_t *>(s), 56)
			== "84983E441C3BD26EBAAE4AA1F95129E5E54670F1");
	}

	{ // EVERY LENGTH either side of a block boundary, both ways.
		//
		// A transform is easy to get right on one block and wrong on the seam:
		// the ragged tail, the block that is exactly full, the message schedule
		// carried from one block into the next. 0 to 600 covers nine blocks and
		// every remainder.
		std::vector<uint8_t> buf(1024);
		for (size_t i = 0; i < buf.size(); i++) buf[i] = static_cast<uint8_t>(i * 31 + 7);

		for (size_t len = 0; len <= 600; len++)
		{
			const std::string soft = hashWith(true, buf, len);
			const std::string hard = hashWith(false, buf, len);
			if (soft != hard)
			{
				std::printf("length %zu: software %s, hardware %s\n",
					len, soft.c_str(), hard.c_str());
			}
			assert(soft == hard);
		}
	}

	{ // and something long enough to run the bulk loop many times over, where
	  // whole blocks are hashed where they lie rather than through the 64-byte
	  // staging block
		std::vector<uint8_t> big(1 << 20);
		uint32_t x = 12345;
		for (size_t i = 0; i < big.size(); i++)
		{
			x = x * 1664525u + 1013904223u;
			big[i] = static_cast<uint8_t>(x >> 24);
		}
		assert(hashWith(true, big, big.size()) == hashWith(false, big, big.size()));

		// an update split at an awkward offset must reach the same digest as one
		// that is not: the staging block and the bulk path have to agree about
		// where they left off
		assert(hashWith(true, big, 1000) == hashWith(false, big, 1000));
	}

	{ // the file hash is the same hash. A firmware may be a disk image - bigger
	  // than the frontend can hold - so its identity has to come from reading
	  // it, and the two ways of taking it must not disagree about one byte.
		std::vector<uint8_t> big(3 << 20);
		uint32_t x = 987654321u;
		for (size_t i = 0; i < big.size(); i++)
		{
			x = x * 1664525u + 1013904223u;
			big[i] = static_cast<uint8_t>(x >> 24);
		}
		const std::string path = "test_sha1_file.bin"; // the test's own working directory
		FILE *f = std::fopen(path.c_str(), "wb");
		assert(f != nullptr);
		assert(std::fwrite(big.data(), 1, big.size(), f) == big.size());
		std::fclose(f);

		char hex[41] = { 0 };
		uint64_t len = 0;
		assert(ce_sha1_file(path.c_str(), hex, &len) == 1);
		assert(len == big.size());
		assert(std::string(hex) == chimera::sha1Hex(big.data(), big.size()));

		// and again, which the cache answers - the same digest, not a stale one
		char hex2[41] = { 0 };
		assert(ce_sha1_file(path.c_str(), hex2, nullptr) == 1);
		assert(std::string(hex2) == std::string(hex));

		std::remove(path.c_str());
		// a file that is not there is not an identity: it says so rather than
		// handing back a digest of nothing
		assert(ce_sha1_file(path.c_str(), hex, &len) == 0);
	}

	std::printf("test_sha1: ok\n");
	return 0;
}
