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

FILE *openWrite(const char *utf8Path)
{
#if defined(_WIN32)
	return _wfopen(widen(utf8Path).c_str(), L"wb");
#else
	return std::fopen(utf8Path, "wb");
#endif
}

} // namespace

FileReader::~FileReader() { close(); }

bool FileReader::open(const char *utf8Path)
{
	close();
	_f = openRead(utf8Path);
	return _f != nullptr;
}

uint64_t FileReader::read(uint8_t *dst, uint64_t max)
{
	if (_f == nullptr) return 0;
	return std::fread(dst, 1, static_cast<size_t>(max), static_cast<FILE *>(_f));
}

bool FileReader::ok() const
{
	return _f != nullptr && std::ferror(static_cast<FILE *>(_f)) == 0;
}

void FileReader::close()
{
	if (_f != nullptr) { std::fclose(static_cast<FILE *>(_f)); _f = nullptr; }
}

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

bool writeFile(const char *utf8Path, const uint8_t *data, uint64_t len)
{
	FILE *f = openWrite(utf8Path);
	if (f == nullptr) return false;
	bool ok = len == 0 || std::fwrite(data, 1, len, f) == len;
	if (std::fclose(f) != 0) ok = false;
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
