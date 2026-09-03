// The host's side of the compile-cache bridge (miniBox source/cache/
// cache-bridge.h): a directory of files a core names, kept between sessions.
// The core promises the bytes are a pure function of its inputs; this keeps
// them. Names are relative paths; an absolute prefix or a '..' segment is
// refused. A store writes beside and renames, so a reader never sees half.
#include "chimera/engine.h"
#include "cache-bridge.h"
#include "sha1.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <string>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define CE_MKDIR(p) _mkdir(p)
#else
#define CE_MKDIR(p) mkdir(p, 0777)
#endif

namespace
{
std::string s_dir;
uint64_t s_fetched, s_stored;

/* the relative name the caller gave, once it has been found acceptable */
std::string s_rel;

std::string safe_path(const char *name, uint64_t len)
{
	if (!name || !len || len > 1024 || name[0] == '/' || name[0] == '\\') return {};
	std::string rel(name, static_cast<size_t>(len));
	if (rel.find(':') != std::string::npos) return {};
	size_t start = 0;
	while (start <= rel.size())
	{
		size_t end = rel.find('/', start);
		if (end == std::string::npos) end = rel.size();
		std::string seg = rel.substr(start, end - start);
		if (seg.empty() || seg == "..") return {};
		start = end + 1;
	}
	s_rel = rel;
	return s_dir + "/" + rel;
}

void make_dirs(const std::string &full)
{
	for (size_t i = s_dir.size() + 1; i < full.size(); i++)
	{
		if (full[i] == '/') CE_MKDIR(full.substr(0, i).c_str());
	}
}
}  // namespace

extern "C" void ce_cache_dir(const char *dir)
{
	s_dir = dir ? dir : "";
	while (!s_dir.empty() && (s_dir.back() == '/' || s_dir.back() == '\\')) s_dir.pop_back();
	// the whole chain: the caller names a directory by core and package
	for (size_t i = 1; i < s_dir.size(); i++)
	{
		if (s_dir[i] == '/' || s_dir[i] == '\\') CE_MKDIR(s_dir.substr(0, i).c_str());
	}
	if (!s_dir.empty()) CE_MKDIR(s_dir.c_str());
}

extern "C" const char *ce_cache_dir_get(void)
{
	return s_dir.c_str();
}

#if defined(_WIN32) && defined(__GNUC__)
#define CE_BRIDGE_ABI __attribute__((sysv_abi))
#else
#define CE_BRIDGE_ABI
#endif

// The work, in the host's own ABI: a sysv-ABI function may carry no C++
// unwind data under mingw (".seh_handlerdata used outside of .seh_proc"),
// and std::string has destructors.
static __attribute__((noinline)) uintptr_t dispatch_impl(uintptr_t op, uintptr_t a, uintptr_t b)
{
	if (s_dir.empty()) return 0;
	switch (op)
	{
	case CACHE_OP_FETCH:
	{
		auto *args = reinterpret_cast<CacheFetchArgs *>(a);
		const std::string full = safe_path(reinterpret_cast<const char *>(static_cast<uintptr_t>(args->name)), args->name_len);
		if (full.empty()) return 0;
		const std::string rel = s_rel;
		FILE *f = fopen(full.c_str(), "rb");
		if (!f) return 0;
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		if (size <= 0) { fclose(f); return 0; }
		if (args->dst && args->cap >= static_cast<uint64_t>(size))
		{
			fseek(f, 0, SEEK_SET);
			if (fread(reinterpret_cast<void *>(static_cast<uintptr_t>(args->dst)), 1, static_cast<size_t>(size), f) != static_cast<size_t>(size)) { fclose(f); return 0; }
			s_fetched++;
			/* one line per object, for whoever is watching this process fill a
			 * cache (the wizard's precompile step reads exactly these) */
			printf("[cache] fetched %s %s\n", rel.c_str(),
				chimera::sha1Hex(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(args->dst)), static_cast<uint64_t>(size)).c_str());
			fflush(stdout);
		}
		fclose(f);
		return static_cast<uintptr_t>(size);
	}
	case CACHE_OP_PROGRESS:
	{
		/* said by the guest as it works, because it cannot be asked meanwhile */
		static uintptr_t lastDone = SIZE_MAX, lastTotal = SIZE_MAX;
		if (a != lastDone || b != lastTotal)
		{
			lastDone = a;
			lastTotal = b;
			printf("Precompiled %llu/%llu modules\n", (unsigned long long)a, (unsigned long long)b);
			fflush(stdout);
		}
		return 1;
	}
	case CACHE_OP_STORE:
	{
		auto *args = reinterpret_cast<CacheStoreArgs *>(a);
		const std::string full = safe_path(reinterpret_cast<const char *>(static_cast<uintptr_t>(args->name)), args->name_len);
		if (full.empty() || !args->size) return 0;
		const std::string rel = s_rel;
		make_dirs(full);
		const std::string tmp = full + ".part";
		FILE *f = fopen(tmp.c_str(), "wb");
		if (!f) return 0;
		bool ok = fwrite(reinterpret_cast<const void *>(static_cast<uintptr_t>(args->data)), 1, static_cast<size_t>(args->size), f) == static_cast<size_t>(args->size);
		ok = fclose(f) == 0 && ok;
		if (!ok) { remove(tmp.c_str()); return 0; }
#ifdef _WIN32
		remove(full.c_str());
#endif
		if (rename(tmp.c_str(), full.c_str()) != 0) { remove(tmp.c_str()); return 0; }
		s_stored++;
		printf("[cache] stored %s %s\n", rel.c_str(),
			chimera::sha1Hex(reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(args->data)), args->size).c_str());
		fflush(stdout);
		return 1;
	}
	}
	return 0;
}

// entered from guest code: sysv on every host, the sandbox's callback shape
extern "C" uintptr_t CE_BRIDGE_ABI ce_cache_dispatch(uintptr_t op, uintptr_t a, uintptr_t b, uintptr_t, uintptr_t, uintptr_t)
{
	return dispatch_impl(op, a, b);
}

extern "C" uint64_t ce_cache_host_stored(void) { return s_stored; }
extern "C" uint64_t ce_cache_host_fetched(void) { return s_fetched; }
