/* The core log (ce_core_log): what the cores say, kept in a file the frontend
 * chose, and only while it asks. miniBox does the copying - every write a
 * guest makes to stdout or stderr (wbx_set_output_file) - because that is the
 * one place all of a core's output passes; a GUI process on Windows has no
 * stderr, so without a copy it reaches nobody. This side starts the file,
 * notes the sessions that open while it is on, and tells them to mount the
 * "corelog" request. */
#include "core_log.hpp"

#include "chimera/engine.h"
#include "host_dyn.hpp"
#include "thread_string.hpp"

#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace chimera {

namespace {

std::mutex g_mutex;
std::string g_path;
std::string g_error;

FILE *openUtf8(const std::string &path, bool append)
{
#if defined(_WIN32)
	int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
	if (n <= 0) return nullptr;
	std::wstring wide(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wide[0], n);
	return _wfopen(wide.c_str(), append ? L"ab" : L"wb");
#else
	return std::fopen(path.c_str(), append ? "ab" : "wb");
#endif
}

std::string utcNow()
{
	const std::time_t now = std::time(nullptr);
	std::tm tm{};
#if defined(_WIN32)
	gmtime_s(&tm, &now);
#else
	gmtime_r(&now, &tm);
#endif
	char buf[32];
	std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S UTC", &tm);
	return buf;
}

/* a line appended to the file, under the lock */
void appendLine(const std::string &path, const std::string &line)
{
	if (FILE *f = openUtf8(path, true))
	{
		std::fputs(("--- " + utcNow() + " " + line + "\n").c_str(), f);
		std::fclose(f);
	}
}

} // namespace

bool coreLogOn()
{
	std::lock_guard lock(g_mutex);
	return !g_path.empty();
}

void coreLogNote(const std::string &line)
{
	std::lock_guard lock(g_mutex);
	if (!g_path.empty()) appendLine(g_path, line);
}

} // namespace chimera

extern "C" {

int32_t ce_core_log(const char *path)
{
	using namespace chimera;
	const char *hostError = nullptr;
	const HostApi *host = hostApi(&hostError);
	std::lock_guard lock(g_mutex);
	g_error.clear();
	if (path == nullptr || path[0] == '\0')
	{
		if (!g_path.empty())
		{
			appendLine(g_path, "the core log was turned off");
			WbxReturn r{};
			if (host != nullptr && host->wbx_set_output_file != nullptr) host->wbx_set_output_file(nullptr, &r);
		}
		g_path.clear();
		return 1;
	}
	if (host == nullptr)
	{
		g_error = hostError != nullptr ? hostError : "libminiboxhost could not be loaded";
		return 0;
	}
	if (host->wbx_set_output_file == nullptr)
	{
		g_error = "this libminiboxhost cannot keep a core log (it has no wbx_set_output_file)";
		return 0;
	}
	FILE *f = openUtf8(path, false);
	if (f == nullptr)
	{
		g_error = std::string("cannot write ") + path;
		return 0;
	}
	std::fputs(("Chimera core log, started " + utcNow() + "\nChimera build: " + ce_build_info() + "\n").c_str(), f);
	std::fclose(f);
	WbxReturn r{};
	host->wbx_set_output_file(path, &r);
	if (!r.ok())
	{
		g_error = r.errorMessage;
		return 0;
	}
	g_path = path;
	return 1;
}

/* per-thread copies handed out as C strings: never destroyed, see
 * thread_string.hpp */
static thread_local chimera::ThreadString g_pathCopy;
static thread_local chimera::ThreadString g_errorCopy;

const char *ce_core_log_path(void)
{
	std::lock_guard lock(chimera::g_mutex);
	*g_pathCopy = chimera::g_path;
	return g_pathCopy->c_str();
}

const char *ce_core_log_error(void)
{
	std::lock_guard lock(chimera::g_mutex);
	*g_errorCopy = chimera::g_error;
	return g_errorCopy->c_str();
}

}
