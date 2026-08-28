/* manifest_util.hpp - the rules shared by everything that names files by
 * hash: the multi-file descriptor and the project manifest. Bare names,
 * well-formed SHA1s, cue references, the percent-encoded movie-line name.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chimera {
namespace manifest {

/* bare means bare: no separators, no traversal, nothing empty */
bool bareName(const std::string &n);

/* a slot id: lowercase letters, digits, '_' or '-', nonempty */
bool validSlot(const std::string &s);

std::string upperHex(std::string s);

/* 40 uppercase hex characters */
bool validSha1(const std::string &s);

/* everything up to and including the last separator; "" for a bare path */
std::string folderOf(const std::string &path);

/* The file names a cue sheet references: FILE "name" TYPE lines (quotes
 * optional). The cue decides - a listed cue whose references are not listed
 * would let unhashed bytes reach the machine. */
std::vector<std::string> cueReferences(const std::vector<uint8_t> &bytes);

bool hasCueSuffix(const std::string &name);

/* the movie line's name encoding: '%', '=', ':' and anything outside
 * 0x21..0x7E become %XX, so names with spaces survive a space-joined line */
std::string encodeName(const std::string &name);

/* SHA1 of the bytes, as 40 uppercase hex characters */
void hashInto(const std::vector<uint8_t> &bytes, std::string &out);

} // namespace manifest
} // namespace chimera
