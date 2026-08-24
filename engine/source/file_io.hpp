/* file_io.hpp - the engine's minimal file reading.
 *
 * Read-only, whole-file, UTF-8 paths everywhere - on Windows that means
 * converting to wide characters, since the ANSI fopen would mangle non-ASCII
 * paths the C# side (which is UTF-16 native) has no trouble with.
 */

#ifndef CHIMERA_FILE_IO_HPP
#define CHIMERA_FILE_IO_HPP

#include <cstdint>
#include <vector>

namespace chimera {

/* false when the file cannot be opened or read; out is only valid on true. */
bool readFile(const char *utf8Path, std::vector<uint8_t> &out);

bool fileExists(const char *utf8Path);

bool isDirectory(const char *utf8Path);

} // namespace chimera

#endif
