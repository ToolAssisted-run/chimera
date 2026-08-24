/* zstd_dyn.hpp - libzstd, loaded at runtime.
 *
 * The build already ships libzstd beside libchimera (build/dll on both
 * targets); loading it at runtime from this library's own directory keeps the
 * engine free of per-platform link plumbing, the same way the C# side always
 * P/Invoked it. Absence is a reported error, not a crash.
 */

#ifndef CHIMERA_ZSTD_DYN_HPP
#define CHIMERA_ZSTD_DYN_HPP

#include <cstddef>

namespace chimera {

struct ZstdApi
{
	size_t (*compressBound)(size_t srcSize);
	size_t (*compress)(void *dst, size_t dstCap, const void *src, size_t srcSize, int level);
	unsigned (*isError)(size_t code);
	void *(*createDStream)(void);
	size_t (*initDStream)(void *zds);
	size_t (*freeDStream)(void *zds);
	struct Buffer { const void *ptr; size_t size; size_t pos; };
	struct OutBuffer { void *ptr; size_t size; size_t pos; };
	size_t (*decompressStream)(void *zds, OutBuffer *output, Buffer *input);
};

/* The loaded API, or nullptr with *error set to why. Loads once, then cached. */
const ZstdApi *zstdApi(const char **error);

} // namespace chimera

#endif
