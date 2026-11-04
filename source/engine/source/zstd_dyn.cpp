#include "zstd_dyn.hpp"
#include "language.hpp"
#include "host_dyn.hpp"

#include <mutex>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace chimera {

namespace {

ZstdApi g_api;
bool g_ok = false;
bool g_tried = false; /* the library opened (whether or not it had everything) */
std::string g_error;
std::mutex g_loadLock;

void *sym(void *lib, const char *name)
{
#if defined(_WIN32)
	return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
	return dlsym(lib, name);
#endif
}

} // namespace

void loadOnce()
{
	{
#if defined(_WIN32)
		void *lib = openLibraryBesideUs("libzstd.dll", false, g_error);
#elif defined(__APPLE__)
		void *lib = openLibraryBesideUs("libzstd.dylib", true, g_error);
#else
		void *lib = openLibraryBesideUs("libzstd.so", true, g_error);
#endif
		g_tried = lib != nullptr;
		if (lib != nullptr)
		{
			g_api.compressBound = reinterpret_cast<size_t (*)(size_t)>(sym(lib, "ZSTD_compressBound"));
			g_api.compress = reinterpret_cast<size_t (*)(void *, size_t, const void *, size_t, int)>(sym(lib, "ZSTD_compress"));
			g_api.isError = reinterpret_cast<unsigned (*)(size_t)>(sym(lib, "ZSTD_isError"));
			g_api.createDStream = reinterpret_cast<void *(*)()>(sym(lib, "ZSTD_createDStream"));
			g_api.initDStream = reinterpret_cast<size_t (*)(void *)>(sym(lib, "ZSTD_initDStream"));
			g_api.freeDStream = reinterpret_cast<size_t (*)(void *)>(sym(lib, "ZSTD_freeDStream"));
			g_api.decompressStream = reinterpret_cast<size_t (*)(void *, ZstdApi::OutBuffer *, ZstdApi::Buffer *)>(sym(lib, "ZSTD_decompressStream"));
			g_api.createCStream = reinterpret_cast<void *(*)()>(sym(lib, "ZSTD_createCStream"));
			g_api.initCStream = reinterpret_cast<size_t (*)(void *, int)>(sym(lib, "ZSTD_initCStream"));
			g_api.freeCStream = reinterpret_cast<size_t (*)(void *)>(sym(lib, "ZSTD_freeCStream"));
			g_api.compressStream2 = reinterpret_cast<size_t (*)(void *, ZstdApi::OutBuffer *, ZstdApi::Buffer *, int)>(sym(lib, "ZSTD_compressStream2"));
			g_api.cctxSetParameter = reinterpret_cast<size_t (*)(void *, int, int)>(sym(lib, "ZSTD_CCtx_setParameter"));
			g_ok = g_api.compressBound != nullptr && g_api.compress != nullptr && g_api.isError != nullptr
				&& g_api.createDStream != nullptr && g_api.initDStream != nullptr
				&& g_api.freeDStream != nullptr && g_api.decompressStream != nullptr;
			if (!g_ok) g_error = chimera::lang::tr("zstd.missing_symbols");
			/* streaming compression is all four or none: a caller checks one */
			if (g_api.createCStream == nullptr || g_api.initCStream == nullptr
				|| g_api.freeCStream == nullptr || g_api.compressStream2 == nullptr)
			{
				g_api.createCStream = nullptr;
				g_api.initCStream = nullptr;
				g_api.freeCStream = nullptr;
				g_api.compressStream2 = nullptr;
			}
		}
	}
}

const ZstdApi *zstdApi(const char **error)
{
	/* loaded until it loads, as hostApi: a library a scanner held the first
	 * time is tried again by the next caller */
	std::lock_guard<std::mutex> lock(g_loadLock);
	static int64_t lastTry = 0;
	if (!g_tried && retryLibraryLoad(lastTry)) loadOnce();
	if (!g_ok)
	{
		if (error != nullptr) *error = g_error.c_str();
		return nullptr;
	}
	return &g_api;
}

} // namespace chimera
