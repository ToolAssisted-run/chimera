#include "chimera/vm/process_stamp.hpp"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <libproc.h>
#include <sys/proc_info.h>
#include <unistd.h>
#else
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#endif

namespace chimera::vm {

#ifdef _WIN32
int64_t currentProcessId() { return static_cast<int64_t>(GetCurrentProcessId()); }

int64_t processStarted(int64_t pid)
{
	if (pid <= 0) return -1;
	HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
	if (h == nullptr) return -1;
	int64_t started = -1;
	DWORD code = 0;
	FILETIME created{}, exited{}, kernel{}, user{};
	if (GetExitCodeProcess(h, &code) && code == STILL_ACTIVE && GetProcessTimes(h, &created, &exited, &kernel, &user))
		started = static_cast<int64_t>((static_cast<uint64_t>(created.dwHighDateTime) << 32) | created.dwLowDateTime);
	CloseHandle(h);
	return started;
}
#elif defined(__APPLE__)
int64_t currentProcessId() { return static_cast<int64_t>(getpid()); }

/* macOS has no /proc: the kernel's own record of the process, with when it started
 * (to the microsecond, so a pid taken by another program is told apart) */
int64_t processStarted(int64_t pid)
{
	if (pid <= 0) return -1;
	struct proc_bsdinfo info = {};
	if (proc_pidinfo(static_cast<int>(pid), PROC_PIDTBSDINFO, 0, &info, sizeof info) != static_cast<int>(sizeof info)) return -1;
	/* a process that has ended but not been collected is not running (SZOMB, in the kernel's own header) */
	const uint32_t zombie = 5;
	if (info.pbi_status == zombie) return -1;
	return static_cast<int64_t>(info.pbi_start_tvsec) * 1000000 + static_cast<int64_t>(info.pbi_start_tvusec);
}
#else
int64_t currentProcessId() { return static_cast<int64_t>(getpid()); }

int64_t processStarted(int64_t pid)
{
	if (pid <= 0) return -1;
	std::ifstream in("/proc/" + std::to_string(pid) + "/stat");
	std::string line;
	if (!std::getline(in, line)) return -1;
	/* the name, in brackets, may hold spaces: the fields after it are counted
	 * from its last bracket; the start time is the 22nd field, the 20th after */
	const size_t close = line.rfind(')');
	if (close == std::string::npos) return -1;
	std::istringstream rest(line.substr(close + 1));
	std::string field;
	for (int i = 0; i < 20; i++)
		if (!(rest >> field)) return -1;
	/* a process that has ended but not been collected is not running */
	std::istringstream state(line.substr(close + 1));
	std::string s;
	state >> s;
	if (s == "Z" || s == "X") return -1;
	try
	{
		return static_cast<int64_t>(std::stoll(field));
	}
	catch (...)
	{
		return -1;
	}
}
#endif

} // namespace chimera::vm
