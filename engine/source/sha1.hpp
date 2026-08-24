/* sha1.hpp - plain portable SHA1, for identity hashes (bundles, firmware,
 * roms). Not a hot path: each file is hashed once, at load or compose. */

#ifndef CHIMERA_SHA1_HPP
#define CHIMERA_SHA1_HPP

#include <cstdint>
#include <string>

namespace chimera {

/* 40 uppercase hex characters. */
std::string sha1Hex(const uint8_t *data, uint64_t len);

} // namespace chimera

#endif
