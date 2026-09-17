#include "file_io.hpp"

#include <filesystem>
#include <system_error>
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

FileWriter::~FileWriter()
{
	if (_f != nullptr)
	{
		std::fclose(static_cast<FILE *>(_f));
		std::error_code ec;
		std::filesystem::remove(std::filesystem::u8path(_tmp), ec);
	}
}

bool FileWriter::open(const char *utf8Path)
{
	_path = utf8Path;
	_tmp = _path + ".writing";
	_failed = false;
	_f = openWrite(_tmp.c_str());
	return _f != nullptr;
}

bool FileWriter::write(const void *src, uint64_t len)
{
	if (_f == nullptr || _failed) return false;
	if (len != 0 && std::fwrite(src, 1, static_cast<size_t>(len), static_cast<FILE *>(_f)) != len) _failed = true;
	return !_failed;
}

bool FileWriter::commit()
{
	if (_f == nullptr) return false;
	const bool closed = std::fclose(static_cast<FILE *>(_f)) == 0;
	_f = nullptr;
	std::error_code ec;
	const auto tmp = std::filesystem::u8path(_tmp), target = std::filesystem::u8path(_path);
	if (_failed || !closed)
	{
		std::filesystem::remove(tmp, ec);
		return false;
	}
	std::filesystem::rename(tmp, target, ec);
	if (ec)
	{
		/* not every platform's rename replaces */
		std::filesystem::remove(target, ec);
		std::filesystem::rename(tmp, target, ec);
	}
	if (ec) std::filesystem::remove(tmp, ec);
	return !ec;
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

namespace chimera {

/* Size and mtime, with the same UTF-8 path handling as everything else here. */
bool fileStamp(const char *utf8Path, uint64_t *sizeOut, int64_t *mtimeOut)
{
#ifdef _WIN32
	struct _stat64 st;
	if (_wstat64(widen(utf8Path).c_str(), &st) != 0) return false;
#else
	struct stat st;
	if (stat(utf8Path, &st) != 0) return false;
#endif
	if (sizeOut != nullptr) *sizeOut = (uint64_t)st.st_size;
	if (mtimeOut != nullptr) *mtimeOut = (int64_t)st.st_mtime;
	return true;
}

} // namespace chimera
