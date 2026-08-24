#include "file_io.hpp"

#include <cstdio>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace chimera {

namespace {

#if defined(_WIN32)
std::wstring widen(const char *utf8)
{
	int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
	std::wstring wide(n > 0 ? static_cast<size_t>(n - 1) : 0, L'\0');
	if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8, -1, &wide[0], n);
	return wide;
}
#endif

FILE *openRead(const char *utf8Path)
{
#if defined(_WIN32)
	return _wfopen(widen(utf8Path).c_str(), L"rb");
#else
	return std::fopen(utf8Path, "rb");
#endif
}

} // namespace

bool readFile(const char *utf8Path, std::vector<uint8_t> &out)
{
	FILE *f = openRead(utf8Path);
	if (f == nullptr) return false;
	out.clear();
	uint8_t chunk[1 << 16];
	size_t got;
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) != 0)
	{
		out.insert(out.end(), chunk, chunk + got);
	}
	bool ok = std::ferror(f) == 0;
	std::fclose(f);
	return ok;
}

bool fileExists(const char *utf8Path)
{
#if defined(_WIN32)
	DWORD attrs = GetFileAttributesW(widen(utf8Path).c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
#else
	struct stat st;
	return stat(utf8Path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool isDirectory(const char *utf8Path)
{
#if defined(_WIN32)
	DWORD attrs = GetFileAttributesW(widen(utf8Path).c_str());
	return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
	struct stat st;
	return stat(utf8Path, &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

} // namespace chimera
