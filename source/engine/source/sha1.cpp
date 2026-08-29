/* sha1.cpp - FIPS 180-1 SHA1, straightforwardly. The ce_sha1_hex ABI entry
 * lives here too: it is the identity hash the frontend uses everywhere, so it
 * belongs with the hash, not with whichever caller happened to need it first. */

#include "sha1.hpp"

#include "chimera/engine.h"

#include <cstring>
#include <vector>

#include "file_io.hpp"

namespace chimera {

namespace {

inline uint32_t rol(uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

struct Sha1
{
	uint32_t h[5] = { 0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u };
	uint8_t block[64];
	uint64_t total = 0;
	size_t fill = 0;

	void processBlock(const uint8_t *p)
	{
		uint32_t w[80];
		for (int i = 0; i < 16; i++)
		{
			w[i] = (uint32_t(p[i * 4]) << 24) | (uint32_t(p[i * 4 + 1]) << 16)
				| (uint32_t(p[i * 4 + 2]) << 8) | uint32_t(p[i * 4 + 3]);
		}
		for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
		uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
		for (int i = 0; i < 80; i++)
		{
			uint32_t f, k;
			if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999u; }
			else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1u; }
			else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
			else { f = b ^ c ^ d; k = 0xCA62C1D6u; }
			uint32_t t = rol(a, 5) + f + e + k + w[i];
			e = d; d = c; c = rol(b, 30); b = a; a = t;
		}
		h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
	}

	void update(const uint8_t *data, uint64_t len)
	{
		total += len;
		while (len != 0)
		{
			size_t take = 64 - fill;
			if (take > len) take = static_cast<size_t>(len);
			std::memcpy(block + fill, data, take);
			fill += take;
			data += take;
			len -= take;
			if (fill == 64)
			{
				processBlock(block);
				fill = 0;
			}
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
bool sha1HexOfFile(const char *utf8Path, uint64_t *lenOut, std::string &out)
{
	FileReader reader;
	if (!reader.open(utf8Path)) return false;
	Sha1 sha;
	std::vector<uint8_t> chunk(1 << 20);
	uint64_t total = 0;
	for (;;)
	{
		uint64_t got = reader.read(chunk.data(), chunk.size());
		if (got == 0) break;
		sha.update(chunk.data(), got);
		total += got;
	}
	if (!reader.ok()) return false;
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
	return true;
}

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
