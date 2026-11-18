/* sha1.hpp - plain portable SHA1, for identity hashes (bundles, firmware,
 * roms). Not a hot path: each file is hashed once, at load or compose. */

#ifndef CHIMERA_SHA1_HPP
#define CHIMERA_SHA1_HPP

#include <cstdint>
#include <functional>
#include <string>

namespace chimera {

/* 40 uppercase hex characters. */
std::string sha1Hex(const uint8_t *data, uint64_t len);

/* The same digest taken by reading the file, without holding it: for a disc
 * image, the difference between four gigabytes and none. lenOut, when given,
 * receives the length that was hashed. */
bool sha1HexOfFile(const char *utf8Path, uint64_t *lenOut, std::string &out);

/* The same, for a caller that shows how far it is and may stop it. tick gets
 * the bytes read so far and the file's length (0 when it is not known) after
 * every megabyte, on the calling thread, and answers true to stop. cacheOnly
 * answers only from what is already known, and never reads. */
enum class Sha1FileResult { Done, Unreadable, Stopped, Unknown };
using Sha1Tick = std::function<bool(uint64_t done, uint64_t total)>;
Sha1FileResult sha1HexOfFileEx(const char *utf8Path, uint64_t *lenOut, std::string &out, const Sha1Tick &tick, bool cacheOnly);

/* The same digest taken as the bytes go PAST, for something being written. A
 * reproducible image is hashed on the way out: hashing it afterwards means
 * reading twenty-five gigabytes back off the disk to learn what we just had in
 * our hands. */
class Sha1Stream
{
public:
	Sha1Stream();
	~Sha1Stream();
	Sha1Stream(const Sha1Stream &) = delete;
	Sha1Stream &operator=(const Sha1Stream &) = delete;

	void update(const uint8_t *data, uint64_t len);

	/* 40 uppercase hex characters. Once only: the state is finished. */
	std::string finishHex();

private:
	void *impl;
};

/* Test hook: force the portable transform, so the same bytes can be hashed both
 * ways and compared. The two paths agreeing is not something to assume. */
void sha1ForceSoftwareForTests(bool on);

/* ce_sha1_keep_in; the number of hashes read back */
int32_t sha1KeepIn(const char *utf8Path);

/* the hashes this process holds forgotten, as a new process starts: what the
 * kept file brings back is then all there is */
void sha1ForgetForTests();

} // namespace chimera

#endif
