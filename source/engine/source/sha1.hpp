/* sha1.hpp - plain portable SHA1, for identity hashes (bundles, firmware,
 * roms). Not a hot path: each file is hashed once, at load or compose. */

#ifndef CHIMERA_SHA1_HPP
#define CHIMERA_SHA1_HPP

#include <cstdint>
#include <string>

namespace chimera {

/* 40 uppercase hex characters. */
std::string sha1Hex(const uint8_t *data, uint64_t len);

/* The same digest taken by reading the file, without holding it: for a disc
 * image, the difference between four gigabytes and none. lenOut, when given,
 * receives the length that was hashed. */
bool sha1HexOfFile(const char *utf8Path, uint64_t *lenOut, std::string &out);

/* Test hook: force the portable transform, so the same bytes can be hashed both
 * ways and compared. The two paths agreeing is not something to assume. */
void sha1ForceSoftwareForTests(bool on);

} // namespace chimera

#endif
