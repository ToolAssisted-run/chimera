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

#ifndef _WIN32
#include <fcntl.h>
#ifdef __APPLE__   /* BSD's names for the stat times */
#define st_atim st_atimespec
#define st_mtim st_mtimespec
#endif
#include <sys/stat.h>
#endif

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

/* The hashes kept between sessions (owner, 2026-10-24, issue 368). A hash
 * taken after ce_sha1_keep_in is read back by the next process, which then
 * does not read the file; a file written since is read again; a line that is
 * not one costs only itself; a kept file of many more lines than files is
 * written again with the newest of each. "The next process" is this one with
 * its own hashes forgotten. */
void writeFill(const char *path, uint8_t fill)
{
	FILE *f = fopen(path, "wb");
	assert(f != nullptr);
	std::vector<uint8_t> block(1 << 20, fill);
	for (int i = 0; i < 9; i++) assert(fwrite(block.data(), 1, block.size(), f) == block.size());
	fclose(f);
}

int linesOf(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == nullptr) return -1;
	int n = 0;
	for (int c; (c = fgetc(f)) != EOF;)
		if (c == '\n') n++;
	fclose(f);
	return n;
}

void testKeptBetweenSessions()
{
	const char *kept = "test_sha1_kept.txt";
	const char *path = "test_sha1_kept.bin";
	remove(kept);
	remove(path);
	writeFill(path, 0x5A);
	char hex[41];
	uint64_t len = 0;
	ce_progress_set(&onProgress, nullptr);

	assert(ce_sha1_keep_in(kept) == 0);
	g_reports = 0;
	assert(ce_sha1_file(path, hex, &len) == 1 && g_reports > 0);
	const std::string first = hex;
	assert(linesOf(kept) == 1);

	/* the next process: the file is not read, the answer is the same */
	chimera::sha1ForgetForTests();
	assert(ce_sha1_keep_in(kept) == 1);
	g_reports = 0;
	len = 0;
	assert(ce_sha1_file(path, hex, &len) == 1);
	assert(first == hex && len == 9u << 20);
	assert(g_reports == 0);

	/* written since: read again */
	writeFill(path, 0x5B);
	chimera::sha1ForgetForTests();
	assert(ce_sha1_keep_in(kept) == 1);
	g_reports = 0;
	assert(ce_sha1_file(path, hex, &len) == 1 && first != hex && g_reports > 0);
	const std::string second = hex;

	/* broken lines: passed over */
	{
		FILE *f = fopen(kept, "ab");
		assert(f != nullptr);
		fputs("not a hash at all\n", f);
		fputs("0123 5 6 7 8 9 10 /short/hash\n", f);
		fputs(first.c_str(), f); /* a last line cut short */
		fclose(f);
	}
	chimera::sha1ForgetForTests();
	assert(ce_sha1_keep_in(kept) == 1);
	g_reports = 0;
	assert(ce_sha1_file(path, hex, &len) == 1 && second == hex && g_reports == 0);

	/* many lines of one file: written again with the newest alone */
	for (int i = 0; i < 70; i++)
	{
		writeFill(path, static_cast<uint8_t>(i));
		assert(ce_sha1_file(path, hex, &len) == 1);
	}
	const std::string last = hex;
	assert(linesOf(kept) > 70);
	chimera::sha1ForgetForTests();
	assert(ce_sha1_keep_in(kept) == 1);
	assert(linesOf(kept) == 1);
	g_reports = 0;
	assert(ce_sha1_file(path, hex, &len) == 1 && last == hex && g_reports == 0);

	assert(ce_sha1_keep_in(nullptr) == 0);
	ce_progress_set(nullptr, nullptr);
	remove(kept);
	remove(path);
	printf("kept: hashes come back in the next session, a changed file is hashed again\n");
}

#ifndef _WIN32
/* A file replaced by one of the same size that keeps the old one's date - an
 * unzip, cp -p, rsync -t, Explorer's copy and replace - is another file, and
 * its hash is its own. The cache once answered it from (size, whole-second
 * mtime). */
void testSameSizeSameDateIsHashedAgain()
{
	const char *path = "test_sha1_same_date.bin";
	auto writeAll = [&](uint8_t fill)
	{
		FILE *f = fopen(path, "r+b");
		if (f == nullptr) f = fopen(path, "wb");
		assert(f != nullptr);
		std::vector<uint8_t> block(64 << 10, fill);
		assert(fwrite(block.data(), 1, block.size(), f) == block.size());
		fclose(f);
	};
	writeAll(0x11);
	struct stat before;
	assert(stat(path, &before) == 0);
	char first[41], second[41];
	assert(ce_sha1_file(path, first, nullptr) == 1);
	writeAll(0x22); /* in place: the same file, the same size */
	const struct timespec times[2] = { before.st_atim, before.st_mtim };
	assert(utimensat(AT_FDCWD, path, times, 0) == 0);
	struct stat after;
	assert(stat(path, &after) == 0);
	assert(after.st_size == before.st_size && after.st_mtim.tv_sec == before.st_mtim.tv_sec
		&& after.st_mtim.tv_nsec == before.st_mtim.tv_nsec);
	assert(ce_sha1_file(path, second, nullptr) == 1);
	std::vector<uint8_t> expect(64 << 10, 0x22);
	assert(std::string(second) == chimera::sha1Hex(expect.data(), expect.size()));
	assert(std::string(second) != std::string(first));
	remove(path);
	printf("cache: a same-size file with the old date is hashed again\n");
}
#endif

} // namespace

int main(void)
{
	testProgressOfAFileHash();
	testKeptBetweenSessions();
#ifndef _WIN32
	testSameSizeSameDateIsHashedAgain();
#endif
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
