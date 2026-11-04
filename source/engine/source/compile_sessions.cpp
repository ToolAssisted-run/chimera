/* compile_sessions.cpp - what the machine says of its memory, and the C face
 * of compile_sessions.h (which says what is counted and why). */

#include "compile_sessions.h"

#include "chimera/engine.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <cstring>
#ifdef __APPLE__
#include <mach/mach.h>
#endif
#endif

CeHostMemory ceReadHostMemory()
{
	CeHostMemory m;
#ifdef _WIN32
	MEMORYSTATUSEX status;
	status.dwLength = sizeof status;
	if (GlobalMemoryStatusEx(&status))
	{
		m.freePhysical = status.ullAvailPhys;
		/* "page file" in this structure's names means commit: what the system
		 * may still promise, with the page file at its present size */
		m.freeCommit = status.ullAvailPageFile;
	}
#elif defined(__APPLE__)
	/* macOS, like Linux, promises more than it has, so there is one figure:
	 * the pages free, plus those it would give a new program without
	 * swapping - inactive, speculative and purgeable. */
	vm_statistics64_data_t vm;
	mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
	vm_size_t page = 0;
	if (host_page_size(mach_host_self(), &page) == KERN_SUCCESS
	    && host_statistics64(mach_host_self(), HOST_VM_INFO64, reinterpret_cast<host_info64_t>(&vm), &count) == KERN_SUCCESS)
		m.freePhysical = (static_cast<uint64_t>(vm.free_count) + vm.inactive_count + vm.speculative_count
		                  + vm.purgeable_count) * page;
#else
	/* Linux promises more than it has, so there is one figure: what the
	 * kernel reckons a new program could have without swapping. */
	if (FILE *f = std::fopen("/proc/meminfo", "r"))
	{
		char line[256];
		while (std::fgets(line, sizeof line, f) != nullptr)
		{
			unsigned long long kb = 0;
			if (std::sscanf(line, "MemAvailable: %llu kB", &kb) == 1)
			{
				m.freePhysical = static_cast<uint64_t>(kb) * 1024;
				break;
			}
		}
		std::fclose(f);
	}
#endif
	return m;
}

extern "C" uint64_t ce_compile_memory_to_spare(void)
{
	return ceMemoryToSpare(ceReadHostMemory());
}

extern "C" int32_t ce_compile_sessions(int32_t processors, uint64_t to_spare, uint64_t session_bytes)
{
	return ceCompileSessions(processors, to_spare, session_bytes);
}

extern "C" uint64_t ce_compile_session_bytes(void)
{
	return kCompileSessionBytes;
}

extern "C" int32_t ce_compile_memory_ran_short(uint64_t least_to_spare, uint64_t session_bytes)
{
	return ceCompileMemoryRanShort(least_to_spare, session_bytes) ? 1 : 0;
}

extern "C" int32_t ce_compile_session_begins(void)
{
#ifdef __linux__
	// Raising one's own score needs no privilege; 1000 is "first".
	FILE *f = std::fopen("/proc/self/oom_score_adj", "w");
	if (!f) return 0;
	const bool ok = std::fputs("1000", f) >= 0;
	return std::fclose(f) == 0 && ok ? 1 : 0;
#else
	return 0;
#endif
}
