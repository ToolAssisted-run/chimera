/* sha1.cpp - FIPS 180-1 SHA1, straightforwardly. The ce_sha1_hex ABI entry
 * lives here too: it is the identity hash the frontend uses everywhere, so it
 * belongs with the hash, not with whichever caller happened to need it first. */

#include "sha1.hpp"
#include "language.hpp"
#include "progress.hpp"

#include "chimera/engine.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <mutex>
#include <string>
#include <vector>

#include <sys/stat.h>

#include "file_io.hpp"

/* SHA1 is in the instruction set of every x86-64 made since about 2016, and
 * this hash is on the path a person waits behind: a project pins every file it
 * carries by SHA1, so opening one with a two gigabyte disc in it hashes two
 * gigabytes before the machine starts. Measured on a 2 GB image, the plain C
 * below takes 10.7 seconds and the same digest through these instructions takes
 * 1.4 - and 98% of that 10.7 was CPU, not the disk.
 *
 * Chosen at RUNTIME, because a build has to run on the machines that do not
 * have them. Both paths produce the same forty characters, which is not an
 * assumption: test_sha1 hashes the same inputs through both and compares. */
#if defined(__x86_64__) || defined(_M_X64)
#define CE_SHA1_X86 1
#include <immintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace chimera {

static bool forceSoftware = false;

namespace {

inline uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

#if CE_SHA1_X86
/* Whether this CPU has the SHA extensions (CPUID leaf 7, EBX bit 29) and the
 * SSSE3 byte shuffle the transform below also needs (leaf 1, ECX bit 9). */
bool cpuHasSha()
{
	uint32_t a = 0, b = 0, c = 0, d = 0;
#if defined(_MSC_VER)
	int r[4];
	__cpuid(r, 1); c = (uint32_t)r[2];
	if ((c & (1u << 9)) == 0) return false;
	__cpuidex(r, 7, 0); b = (uint32_t)r[1];
#else
	if (!__get_cpuid(1, &a, &b, &c, &d)) return false;
	if ((c & (1u << 9)) == 0) return false;   // SSSE3
	if (!__get_cpuid_count(7, 0, &a, &b, &c, &d)) return false;
#endif
	return (b & (1u << 29)) != 0;             // SHA
}

/* FIPS 180-1, in six instructions the CPU already knows. The structure is the
 * one Intel published with the extensions: four message registers rotating
 * through twenty groups of four rounds, with sha1msg1/sha1msg2 building the
 * schedule a group ahead of where it is used. */
__attribute__((target("sha,sse4.1")))
void processBlocksSha(uint32_t h[5], const uint8_t *p, size_t blocks)
{
	const __m128i shuffle = _mm_set_epi64x(0x0001020304050607ULL, 0x08090a0b0c0d0e0fULL);
	__m128i abcd = _mm_shuffle_epi32(_mm_loadu_si128((const __m128i *)h), 0x1B);
	__m128i e[2];
	e[0] = _mm_set_epi32((int)h[4], 0, 0, 0);
	e[1] = _mm_setzero_si128();

	while (blocks-- != 0)
	{
		const __m128i abcdSave = abcd;
		const __m128i eSave = e[0];
		__m128i msg[4];
		int act = 0;   // which of e[] is the round's accumulator

		/* unrolled: every range test and every round-function index below is a
		 * constant once it is, which is the difference between this and the
		 * hardware sitting idle behind a branch */
#if defined(__GNUC__)
#pragma GCC unroll 20
#endif
		for (int g = 0; g < 20; g++)
		{
			const int cur = g & 3;
			if (g < 4)
			{
				msg[cur] = _mm_shuffle_epi8(
					_mm_loadu_si128((const __m128i *)(p + g * 16)), shuffle);
			}
			e[act] = g == 0 ? _mm_add_epi32(e[act], msg[cur])
			                : _mm_sha1nexte_epu32(e[act], msg[cur]);
			e[act ^ 1] = abcd;
			if (g >= 3 && g <= 18)
				msg[(cur + 1) & 3] = _mm_sha1msg2_epu32(msg[(cur + 1) & 3], msg[cur]);
			switch (g < 5 ? 0 : g < 10 ? 1 : g < 15 ? 2 : 3)
			{
				case 0: abcd = _mm_sha1rnds4_epu32(abcd, e[act], 0); break;
				case 1: abcd = _mm_sha1rnds4_epu32(abcd, e[act], 1); break;
				case 2: abcd = _mm_sha1rnds4_epu32(abcd, e[act], 2); break;
				default: abcd = _mm_sha1rnds4_epu32(abcd, e[act], 3); break;
			}
			if (g >= 1 && g <= 16)
				msg[(cur + 3) & 3] = _mm_sha1msg1_epu32(msg[(cur + 3) & 3], msg[cur]);
			if (g >= 2 && g <= 17)
				msg[(cur + 2) & 3] = _mm_xor_si128(msg[(cur + 2) & 3], msg[cur]);
			act ^= 1;
		}

		e[act] = _mm_sha1nexte_epu32(e[act], eSave);
		abcd = _mm_add_epi32(abcd, abcdSave);
		e[0] = e[act];
		p += 64;
	}

	_mm_storeu_si128((__m128i *)h, _mm_shuffle_epi32(abcd, 0x1B));
	h[4] = (uint32_t)_mm_extract_epi32(e[0], 3);
}
#endif

struct Sha1
{
	uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
	uint8_t block[64];
	uint64_t total = 0;
	size_t fill = 0;

	/* The transform without SHA instructions, which every Intel desktop from
	 * the 6th to the 10th generation lacks. Written out round by round with a
	 * 16-word schedule rolled in place, so the compiler keeps everything in
	 * registers: the loop over 80 rounds with a branch and a five-register
	 * shuffle in each ran at about 270 MB/s here, this at about 1.25 GB/s, and
	 * the digest is the same (test_sha1 checks it against the vectors). */
	void processBlock(const uint8_t *p)
	{
		uint32_t w[16];
		for (int i = 0; i < 16; i++)
		{
			w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16)
				| (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
		}
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
#define CE_W(i) (w[(i) & 15] = rol(w[((i) + 13) & 15] ^ w[((i) + 8) & 15] ^ w[((i) + 2) & 15] ^ w[(i) & 15], 1))
#define CE_R0(v, x, y, z, u, i) u += ((x & (y ^ z)) ^ z) + w[i] + 0x5A827999u + rol(v, 5); x = rol(x, 30);
#define CE_R1(v, x, y, z, u, i) u += ((x & (y ^ z)) ^ z) + CE_W(i) + 0x5A827999u + rol(v, 5); x = rol(x, 30);
#define CE_R2(v, x, y, z, u, i) u += (x ^ y ^ z) + CE_W(i) + 0x6ED9EBA1u + rol(v, 5); x = rol(x, 30);
#define CE_R3(v, x, y, z, u, i) u += (((x | y) & z) | (x & y)) + CE_W(i) + 0x8F1BBCDCu + rol(v, 5); x = rol(x, 30);
#define CE_R4(v, x, y, z, u, i) u += (x ^ y ^ z) + CE_W(i) + 0xCA62C1D6u + rol(v, 5); x = rol(x, 30);
#define CE_FIVE(R, i) R(a, b, c, d, e, i) R(e, a, b, c, d, i + 1) R(d, e, a, b, c, i + 2) R(c, d, e, a, b, i + 3) R(b, c, d, e, a, i + 4)
		CE_FIVE(CE_R0, 0) CE_FIVE(CE_R0, 5) CE_FIVE(CE_R0, 10)
		CE_R0(a, b, c, d, e, 15)
		CE_R1(e, a, b, c, d, 16) CE_R1(d, e, a, b, c, 17) CE_R1(c, d, e, a, b, 18) CE_R1(b, c, d, e, a, 19)
		CE_FIVE(CE_R2, 20) CE_FIVE(CE_R2, 25) CE_FIVE(CE_R2, 30) CE_FIVE(CE_R2, 35)
		CE_FIVE(CE_R3, 40) CE_FIVE(CE_R3, 45) CE_FIVE(CE_R3, 50) CE_FIVE(CE_R3, 55)
		CE_FIVE(CE_R4, 60) CE_FIVE(CE_R4, 65) CE_FIVE(CE_R4, 70) CE_FIVE(CE_R4, 75)
#undef CE_FIVE
#undef CE_R4
#undef CE_R3
#undef CE_R2
#undef CE_R1
#undef CE_R0
#undef CE_W
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}

	/* Whole blocks are hashed WHERE THEY LIE. The obvious loop copies every
	 * one through the 64-byte staging block first, which for a two gigabyte
	 * disc image is two gigabytes of memcpy nothing needed - the staging block
	 * is only for the ragged ends. */
	void processBlocks(const uint8_t *p, size_t blocks)
	{
#if CE_SHA1_X86
		static const bool hasSha = cpuHasSha();
		if (hasSha && !forceSoftware) { processBlocksSha(h, p, blocks); return; }
#endif
		for (size_t i = 0; i < blocks; i++) processBlock(p + i * 64);
	}

	void update(const uint8_t *data, uint64_t len)
	{
		total += len;
		if (fill != 0)
		{
			size_t take = 64 - fill;
			if (take > len) take = static_cast<size_t>(len);
			std::memcpy(block + fill, data, take);
			fill += take;
			data += take;
			len -= take;
			if (fill == 64) { processBlocks(block, 1); fill = 0; }
		}
		if (len >= 64)
		{
			const size_t blocks = static_cast<size_t>(len / 64);
			processBlocks(data, blocks);
			data += blocks * 64;
			len -= static_cast<uint64_t>(blocks) * 64;
		}
		if (len != 0)
		{
			std::memcpy(block + fill, data, static_cast<size_t>(len));
			fill += static_cast<size_t>(len);
		}
	}

	void finish(uint8_t out[20])
	{
		uint64_t bits = total * 8;
		uint8_t pad = 0x80;
		update(&pad, 1);
		uint8_t zero = 0;
		while (fill != 56) update(&zero, 1);
		uint8_t lenBytes[8];
		for (int i = 0; i < 8; i++) lenBytes[i] = static_cast<uint8_t>(bits >> (56 - i * 8));
		update(lenBytes, 8);
		for (int i = 0; i < 5; i++)
		{
			out[i * 4] = static_cast<uint8_t>(h[i] >> 24);
			out[i * 4 + 1] = static_cast<uint8_t>(h[i] >> 16);
			out[i * 4 + 2] = static_cast<uint8_t>(h[i] >> 8);
			out[i * 4 + 3] = static_cast<uint8_t>(h[i]);
		}
	}
};

} // namespace

/* The same digest, without the file in memory.
 *
 * A disc image is gigabytes and its hash is 40 characters; reading it whole to
 * produce them is a copy nothing needed. Returns false only when the file will
 * not open or will not read - a caller cannot tell a wrong hash from a missing
 * one by looking at the string, so it is told. */
/* WHAT WAS HASHED ALREADY, keyed by the file's path, size and mtime.
 *
 * A project hashes every file it carries when it is opened, and the same file
 * is routinely hashed more than once in a session - the wizard weighs it while
 * a project is being made, and opening that project weighs it again. Two
 * gigabytes twice is a wait for an answer already known.
 *
 * Keyed on the path AND the file's identity (fileIdentity): a file edited or
 * replaced under a running Chimera has a new change time, or is another file,
 * and gets hashed again. Size and the whole-second mtime were not enough: a
 * copy, an unzip or an rsync keeps the source's date, and two dumps of one game
 * are often the same size. It is a cache of work, not a decision - a wrong
 * answer here would be a movie pinned to the wrong bytes, so it only ever skips
 * work that would have produced the same forty characters.
 *
 * It lived as long as the process, and nothing was written to disk: a stale
 * entry surviving a restart is a bug nobody could see. The owner chose to keep
 * it between sessions (2026-10-24, issue 368): the first rescan of a Firmware
 * folder in every session read every disc image in it again. The frontend
 * names a file (ce_sha1_keep_in); every hash taken of a file whose identity
 * the system gives in full is appended to it, and the next session reads it
 * back. The key is the same whole identity, so a file edited, replaced, copied
 * over or moved in is another file and is hashed again. A file whose system
 * gives no change time or no file index (some network and FAT volumes) is
 * kept for the session only: without them two different files can look the
 * same.
 *
 * The file is text, a line a hash, appended as hashes are taken:
 *   <sha1> <length> <size> <modified> <changed> <volume> <index> <path>
 * A line that does not read as one is passed over, so a write cut short costs
 * that entry and nothing else. Only the newest line of a path is taken, and a
 * file holding many more lines than paths is written again with those alone. */
struct CacheKey
{
	std::string path;
	FileIdentity id;
	bool operator<(const CacheKey &o) const
	{
		if (path != o.path) return path < o.path;
		return id < o.id;
	}
};

std::mutex g_cacheLock;
std::map<CacheKey, std::pair<std::string, uint64_t>> g_cache;

static std::mutex g_keepLock; /* the file the hashes are kept in, and writes to it */
static std::string g_keepPath;

static bool keepable(const FileIdentity &id) { return id.changed != 0 && id.index != 0; }

static std::string keepLine(const CacheKey &key, const std::string &hex, uint64_t len)
{
	if (key.path.empty() || key.path.find_first_of("\r\n") != std::string::npos) return {};
	char head[200];
	std::snprintf(head, sizeof head, "%s %llu %llu %lld %lld %llu %llu ", hex.c_str(),
		(unsigned long long)len, (unsigned long long)key.id.size, (long long)key.id.modified,
		(long long)key.id.changed, (unsigned long long)key.id.volume, (unsigned long long)key.id.index);
	return head + key.path + "\n";
}

/* one line of the kept file; false for anything that is not a whole one */
static bool readKeepLine(const std::string &line, CacheKey &key, std::string &hex, uint64_t &len)
{
	size_t at = 0;
	std::string fields[7];
	for (std::string &f : fields)
	{
		const size_t sp = line.find(' ', at);
		if (sp == std::string::npos || sp == at) return false;
		f = line.substr(at, sp - at);
		at = sp + 1;
	}
	if (at >= line.size()) return false;
	if (fields[0].size() != 40 || fields[0].find_first_not_of("0123456789ABCDEF") != std::string::npos) return false;
	auto number = [](const std::string &s, bool sign, uint64_t &out) {
		if (s.empty()) return false;
		size_t i = sign && s[0] == '-' ? 1 : 0;
		if (i == s.size() || s.find_first_not_of("0123456789", i) != std::string::npos || s.size() > 20) return false;
		out = std::strtoull(s.c_str() + i, nullptr, 10);
		if (i == 1) out = (uint64_t)(-(int64_t)out);
		return true;
	};
	uint64_t v[6];
	for (int i = 0; i < 6; i++)
		if (!number(fields[i + 1], i == 2 || i == 3, v[i])) return false;
	hex = fields[0];
	len = v[0];
	key.id.size = v[1];
	key.id.modified = (int64_t)v[2];
	key.id.changed = (int64_t)v[3];
	key.id.volume = v[4];
	key.id.index = v[5];
	key.path = line.substr(at);
	return keepable(key.id);
}

static void keepHash(const CacheKey &key, const std::string &hex, uint64_t len)
{
	if (!keepable(key.id)) return;
	const std::string line = keepLine(key, hex, len);
	if (line.empty()) return;
	std::lock_guard<std::mutex> keep(g_keepLock);
	if (g_keepPath.empty()) return;
	FILE *f = openFile(g_keepPath.c_str(), "ab");
	if (f == nullptr) return;
	std::fwrite(line.data(), 1, line.size(), f);
	std::fclose(f);
}

Sha1FileResult sha1HexOfFileEx(const char *utf8Path, uint64_t *lenOut, std::string &out, const Sha1Tick &tick, bool cacheOnly)
{
	CacheKey key;
	bool stamped = utf8Path != nullptr
		&& fileIdentity(utf8Path, key.id);
	if (stamped)
	{
		key.path = utf8Path;
		std::lock_guard<std::mutex> lock(g_cacheLock);
		auto it = g_cache.find(key);
		if (it != g_cache.end())
		{
			out = it->second.first;
			if (lenOut != nullptr) *lenOut = it->second.second;
			return Sha1FileResult::Done;
		}
	}
	if (cacheOnly) return Sha1FileResult::Unknown;

	FileReader reader;
	if (!reader.open(utf8Path)) return Sha1FileResult::Unreadable;
	Sha1 sha;
	std::vector<uint8_t> chunk(1 << 20);
	uint64_t total = 0;
	/* the stage names the file: a project with three discs hashes them one
	 * after another, and a person wants to know which one this is */
	std::string stage;
	{
		const char *slash = utf8Path;
		for (const char *c = utf8Path; *c != '\0'; c++)
		{
			if (*c == '/' || *c == '\\') slash = c + 1;
		}
		stage = chimera::lang::tr("engine.progress_hashing", {slash});
	}
	const uint64_t expected = stamped ? key.id.size : 0;
	uint64_t sinceReport = 0;
	for (;;)
	{
		uint64_t got = reader.read(chunk.data(), chunk.size());
		if (got == 0) break;
		sha.update(chunk.data(), got);
		total += got;
		sinceReport += got;
		if (sinceReport >= (4u << 20))
		{
			sinceReport = 0;
			progress(stage.c_str(), total, expected);
		}
		/* asked after every chunk, so a stop is honoured within a megabyte */
		if (tick && tick(total, expected)) return Sha1FileResult::Stopped;
	}
	if (expected != 0) progress(stage.c_str(), total, expected);
	if (tick) tick(total, expected != 0 ? expected : total); /* the last step, whole */
	if (!reader.ok()) return Sha1FileResult::Unreadable;
	uint8_t digest[20];
	sha.finish(digest);
	static const char *hex = "0123456789ABCDEF";
	out.assign(40, '0');
	for (int i = 0; i < 20; i++)
	{
		out[i * 2] = hex[digest[i] >> 4];
		out[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	if (lenOut != nullptr) *lenOut = total;
	/* kept only when the file is still the one that was read: one written to
	 * while it was being hashed has a hash of neither version */
	FileIdentity after;
	if (stamped && fileIdentity(utf8Path, after) && after == key.id)
	{
		{
			std::lock_guard<std::mutex> lock(g_cacheLock);
			g_cache[key] = { out, total };
		}
		keepHash(key, out, total);
	}
	return Sha1FileResult::Done;
}

bool sha1HexOfFile(const char *utf8Path, uint64_t *lenOut, std::string &out)
{
	return sha1HexOfFileEx(utf8Path, lenOut, out, nullptr, false) == Sha1FileResult::Done;
}

int32_t sha1KeepIn(const char *utf8Path)
{
	std::lock_guard<std::mutex> keep(g_keepLock);
	g_keepPath = utf8Path != nullptr ? utf8Path : "";
	if (g_keepPath.empty()) return 0;
	std::vector<uint8_t> data;
	if (!readFile(g_keepPath.c_str(), data)) return 0; /* none yet */
	/* the newest line of each path */
	std::map<std::string, std::pair<CacheKey, std::pair<std::string, uint64_t>>> newest;
	size_t lines = 0, start = 0;
	const std::string text(data.begin(), data.end());
	while (start < text.size())
	{
		size_t end = text.find('\n', start);
		if (end == std::string::npos) end = text.size(); /* a last line cut short reads as no line */
		CacheKey key;
		std::string hex;
		uint64_t len = 0;
		if (end < text.size() && readKeepLine(text.substr(start, end - start), key, hex, len))
		{
			lines++;
			const std::string path = key.path;
			newest[path] = { std::move(key), { std::move(hex), len } };
		}
		start = end + 1;
	}
	{
		std::lock_guard<std::mutex> lock(g_cacheLock);
		for (const auto &[path, entry] : newest) g_cache[entry.first] = entry.second;
	}
	if (lines > 2 * newest.size() + 64)
	{
		std::string again;
		for (const auto &[path, entry] : newest) again += keepLine(entry.first, entry.second.first, entry.second.second);
		writeFile(g_keepPath.c_str(), reinterpret_cast<const uint8_t *>(again.data()), again.size());
	}
	return static_cast<int32_t>(newest.size());
}

void sha1ForgetForTests()
{
	std::lock_guard<std::mutex> lock(g_cacheLock);
	g_cache.clear();
}

Sha1Stream::Sha1Stream() : impl(new Sha1()) {}
Sha1Stream::~Sha1Stream() { delete static_cast<Sha1 *>(impl); }

void Sha1Stream::update(const uint8_t *data, uint64_t len)
{
	static_cast<Sha1 *>(impl)->update(data, len);
}

std::string Sha1Stream::finishHex()
{
	uint8_t digest[20];
	static_cast<Sha1 *>(impl)->finish(digest);
	static const char *hex = "0123456789ABCDEF";
	std::string out(40, '0');
	for (int i = 0; i < 20; i++)
	{
		out[i * 2] = hex[digest[i] >> 4];
		out[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	return out;
}

void sha1ForceSoftwareForTests(bool on) { forceSoftware = on; }

std::string sha1Hex(const uint8_t *data, uint64_t len)
{
	Sha1 sha;
	sha.update(data, len);
	uint8_t digest[20];
	sha.finish(digest);
	static const char *hex = "0123456789ABCDEF";
	std::string out(40, '0');
	for (int i = 0; i < 20; i++)
	{
		out[i * 2] = hex[digest[i] >> 4];
		out[i * 2 + 1] = hex[digest[i] & 0xF];
	}
	return out;
}

} // namespace chimera

extern "C" void ce_sha1_hex(const uint8_t *data, uint64_t len, char *out41)
{
	std::string hex = chimera::sha1Hex(data, len);
	std::memcpy(out41, hex.data(), 40);
	out41[40] = '\0';
}

extern "C" int ce_sha1_file_known(const char *utf8Path, char *out41, uint64_t *lenOut)
{
	std::string hex;
	if (chimera::sha1HexOfFileEx(utf8Path, lenOut, hex, nullptr, true) != chimera::Sha1FileResult::Done) return 0;
	if (out41 != nullptr)
	{
		std::memcpy(out41, hex.data(), 40);
		out41[40] = '\0';
	}
	return 1;
}

extern "C" int ce_sha1_file_progress(const char *utf8Path, char *out41, uint64_t *lenOut, ce_hash_progress_fn fn, void *user)
{
	std::string hex;
	chimera::Sha1Tick tick;
	if (fn != nullptr) tick = [fn, user](uint64_t done, uint64_t total) { return fn(done, total, user) != 0; };
	switch (chimera::sha1HexOfFileEx(utf8Path, lenOut, hex, tick, false))
	{
	case chimera::Sha1FileResult::Done:
		std::memcpy(out41, hex.data(), 40);
		out41[40] = '\0';
		return 1;
	case chimera::Sha1FileResult::Stopped: return -1;
	default: return 0;
	}
}

extern "C" int32_t ce_sha1_keep_in(const char *utf8Path)
{
	try
	{
		return chimera::sha1KeepIn(utf8Path);
	}
	catch (const std::bad_alloc &)
	{
		return 0;
	}
}

extern "C" int ce_sha1_file(const char *utf8Path, char *out41, uint64_t *lenOut)
{
	std::string hex;
	if (!chimera::sha1HexOfFile(utf8Path, lenOut, hex)) return 0;
	std::memcpy(out41, hex.data(), 40);
	out41[40] = '\0';
	return 1;
}
