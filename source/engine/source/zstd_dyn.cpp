#include "zstd_dyn.hpp"

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
std::string g_error;

void *openBesideUs(const char *name)
{
#if defined(_WIN32)
	/* the directory this DLL sits in, so we find OUR zstd, not a stray one */
	HMODULE self = nullptr;
	wchar_t path[MAX_PATH];
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&openBesideUs), &self)
		&& GetModuleFileNameW(self, path, MAX_PATH) != 0)
	{
		std::wstring dir(path);
		auto slash = dir.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			std::wstring full = dir.substr(0, slash + 1);
			for (const char *c = name; *c != '\0'; c++) full.push_back(static_cast<wchar_t>(*c));
			if (HMODULE h = LoadLibraryW(full.c_str())) return h;
		}
	}
	return LoadLibraryA(name);
#else
	/* RTLD_DEEPBIND: the library's own symbols win over the process's global
	 * ones when IT looks a symbol up. Mesa links the distro's libzstd, and SDL
	 * loads Mesa with RTLD_GLOBAL, so by the time a savestate is written the
	 * process already holds a libzstd of another version in its global scope.
	 * Without this flag the bundled libzstd's internal calls (ZSTD_compress to
	 * ZSTD_createCCtx, ZSTD_freeCCtx...) resolved to THAT one, a context made
	 * by one version was torn down by the other, and the first big greenzone
	 * write died in free() - a DOSBox-X project's save, a PS2 project's exit. */
	Dl_info info;
	if (dladdr(reinterpret_cast<void *>(&openBesideUs), &info) != 0 && info.dli_fname != nullptr)
	{
		std::string dir(info.dli_fname);
		auto slash = dir.find_last_of('/');
		if (slash != std::string::npos)
		{
			std::string full = dir.substr(0, slash + 1) + name;
			if (void *h = dlopen(full.c_str(), RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND)) return h;
		}
	}
	return dlopen(name, RTLD_NOW | RTLD_LOCAL | RTLD_DEEPBIND);
#endif
}

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
		void *lib = openBesideUs("libzstd.dll");
#else
		void *lib = openBesideUs("libzstd.so");
#endif
		if (lib == nullptr)
		{
			g_error = "libzstd not found beside libchimera";
		}
		else
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
			g_ok = g_api.compressBound != nullptr && g_api.compress != nullptr && g_api.isError != nullptr
				&& g_api.createDStream != nullptr && g_api.initDStream != nullptr
				&& g_api.freeDStream != nullptr && g_api.decompressStream != nullptr;
			if (!g_ok) g_error = "libzstd is missing expected symbols";
		}
	}
}

const ZstdApi *zstdApi(const char **error)
{
	/* a magic static: the load runs exactly once, and concurrent callers wait */
	static const bool loaded = (loadOnce(), true);
	(void)loaded;
	if (!g_ok)
	{
		if (error != nullptr) *error = g_error.c_str();
		return nullptr;
	}
	return &g_api;
}

} // namespace chimera
