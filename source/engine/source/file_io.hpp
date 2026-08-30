/* file_io.hpp - the engine's minimal file access.
 *
 * Whole-file, UTF-8 paths everywhere - on Windows that means
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

/* A file read a piece at a time, for the ones too big to want whole: a PS2
 * disc image is over four gigabytes, and nothing the engine does with one
 * needs it in memory. Same UTF-8 path handling as the whole-file calls. */
class FileReader
{
public:
	FileReader() = default;
	~FileReader();
	FileReader(const FileReader &) = delete;
	FileReader &operator=(const FileReader &) = delete;

	bool open(const char *utf8Path);
	/* bytes read, 0 at end of file (or on error - ask ok()) */
	uint64_t read(uint8_t *dst, uint64_t max);
	bool ok() const;
	void close();

private:
	void *_f = nullptr;   /* FILE*, kept opaque so the header stays clean */
};


bool fileExists(const char *utf8Path);

/* A file's size and last-modified time, for deciding whether a remembered
 * answer about it still applies. false when the file cannot be stat'd. */
bool fileStamp(const char *utf8Path, uint64_t *sizeOut, int64_t *mtimeOut);

/* whole-file write (multifile descriptors); false on any failure */
bool writeFile(const char *utf8Path, const uint8_t *data, uint64_t len);

bool isDirectory(const char *utf8Path);

} // namespace chimera

#endif
