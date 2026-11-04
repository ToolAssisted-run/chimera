#include "file_io.hpp"

#include <filesystem>
#include <system_error>
#include <cstdio>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
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

#if defined(_WIN32)
/* A file the engine opens is not handed to the programs Chimera starts: a
 * child started with handle inheritance on (FFmpeg, the precompile and
 * import sessions) and holding the input journal open kept it from being
 * replaced. The CRT's own way ('N' in the mode) is not certain in the
 * system msvcrt this links, so the handle is told directly. */
FILE *notInherited(FILE *f)
{
	if (f != nullptr)
	{
		const intptr_t h = _get_osfhandle(_fileno(f));
		if (h != -1) SetHandleInformation(reinterpret_cast<HANDLE>(h), HANDLE_FLAG_INHERIT, 0);
	}
	return f;
}
#endif

FILE *openRead(const char *utf8Path)
{
#if defined(_WIN32)
	return notInherited(_wfopen(widen(utf8Path).c_str(), L"rb"));
#else
	return std::fopen(utf8Path, "rb");
#endif
}

FILE *openWrite(const char *utf8Path)
{
#if defined(_WIN32)
	return notInherited(_wfopen(widen(utf8Path).c_str(), L"wb"));
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
	std::error_code ec;
	const auto asked = std::filesystem::u8path(_path);
	if (std::filesystem::is_symlink(asked, ec))
	{
		/* moving a file over a link would replace the link, not what it names */
		const auto target = std::filesystem::weakly_canonical(asked, ec);
		if (!ec) _path = target.u8string();
	}
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

bool FileWriter::commit(bool toDisk)
{
	if (_f == nullptr) return false;
	FILE *f = static_cast<FILE *>(_f);
	/* a write through file() that failed is a failure here too */
	if (std::ferror(f) != 0) _failed = true;
	if (toDisk && !_failed)
	{
#if defined(_WIN32)
		if (std::fflush(f) != 0 || _commit(_fileno(f)) != 0) _failed = true;
#else
		if (std::fflush(f) != 0 || fsync(fileno(f)) != 0) _failed = true;
#endif
	}
	const bool closed = std::fclose(f) == 0;
	_f = nullptr;
	if (_failed || !closed || !replaceFile(_tmp.c_str(), _path.c_str()))
	{
		removeFile(_tmp.c_str());
		return false;
	}
	return true;
}

bool replaceFile(const char *fromUtf8, const char *toUtf8)
{
#if defined(_WIN32)
	const std::wstring from = widen(fromUtf8), to = widen(toUtf8);
	for (int attempt = 0;; attempt++)
	{
		if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return true;
		const DWORD e = GetLastError();
		const bool held = e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_LOCK_VIOLATION;
		if (!held || attempt >= 20) return false;
		Sleep(50);
	}
#else
	return std::rename(fromUtf8, toUtf8) == 0;
#endif
}

bool removeFile(const char *utf8Path)
{
#if defined(_WIN32)
	return _wremove(widen(utf8Path).c_str()) == 0;
#else
	return std::remove(utf8Path) == 0;
#endif
}

bool seekFile(FILE *f, int64_t offset, int whence)
{
#if defined(_WIN32)
	return _fseeki64(f, offset, whence) == 0;
#else
	return fseeko(f, static_cast<off_t>(offset), whence) == 0;
#endif
}

FILE *openFile(const char *utf8Path, const char *mode)
{
#if defined(_WIN32)
	return notInherited(_wfopen(widen(utf8Path).c_str(), widen(mode).c_str()));
#else
	return std::fopen(utf8Path, mode);
#endif
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
	FileWriter out;
	return out.open(utf8Path) && out.write(data, len) && out.commit(true);
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

bool fileIdentity(const char *utf8Path, FileIdentity &out)
{
	out = FileIdentity{};
#ifdef _WIN32
	/* attributes only: a file another program holds open for writing can
	 * still be asked who it is */
	HANDLE h = CreateFileW(widen(utf8Path).c_str(), FILE_READ_ATTRIBUTES,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
		FILE_FLAG_BACKUP_SEMANTICS, nullptr);
	if (h == INVALID_HANDLE_VALUE) return false;
	BY_HANDLE_FILE_INFORMATION info;
	FILE_BASIC_INFO basic;
	const bool ok = GetFileInformationByHandle(h, &info) != 0
		&& GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) != 0;
	CloseHandle(h);
	if (!ok) return false;
	out.size = (uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
	out.volume = info.dwVolumeSerialNumber;
	out.index = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
	/* 100 ns units: the resolution NTFS keeps */
	out.modified = basic.LastWriteTime.QuadPart;
	out.changed = basic.ChangeTime.QuadPart;
#else
	struct stat st;
	if (stat(utf8Path, &st) != 0) return false;
	out.size = (uint64_t)st.st_size;
	out.volume = (uint64_t)st.st_dev;
	out.index = (uint64_t)st.st_ino;
#ifdef __APPLE__   /* the same times, under BSD's names */
	out.modified = (int64_t)st.st_mtimespec.tv_sec * 1000000000 + st.st_mtimespec.tv_nsec;
	out.changed = (int64_t)st.st_ctimespec.tv_sec * 1000000000 + st.st_ctimespec.tv_nsec;
#else
	out.modified = (int64_t)st.st_mtim.tv_sec * 1000000000 + st.st_mtim.tv_nsec;
	out.changed = (int64_t)st.st_ctim.tv_sec * 1000000000 + st.st_ctim.tv_nsec;
#endif
#endif
	return true;
}

} // namespace chimera
