/* host_machine.cpp - what the computer Chimera runs on says of itself, for a
 * bug report (Help > Report an Issue, chimera issue 277): the processor's name
 * and the memory installed. */

#include "chimera/engine.h"

#include <cstring>
#include <string>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#endif
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif

namespace
{
/* The processor's own name: the 48 bytes of its brand string (cpuid leaves
 * 0x80000002 to 0x80000004), with the spaces some makers pad it with taken
 * off. "" on a processor that has none, or is not an x86. */
std::string readCpuName()
{
#if defined(__x86_64__) || defined(__i386__)
	unsigned int a = 0, b = 0, c = 0, d = 0;
	if (__get_cpuid(0x80000000u, &a, &b, &c, &d) == 0 || a < 0x80000004u) return "";
	char brand[49] = {};
	for (unsigned int leaf = 0; leaf < 3; leaf++)
	{
		__get_cpuid(0x80000002u + leaf, &a, &b, &c, &d);
		std::memcpy(brand + leaf * 16 + 0, &a, 4);
		std::memcpy(brand + leaf * 16 + 4, &b, 4);
		std::memcpy(brand + leaf * 16 + 8, &c, 4);
		std::memcpy(brand + leaf * 16 + 12, &d, 4);
	}
	std::string name(brand);
#elif defined(__APPLE__)
	/* not an x86, but macOS names its processor all the same ("Apple M1") */
	char brand[128] = {};
	size_t size = sizeof brand - 1;
	if (sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) != 0) return "";
	std::string name(brand);
#endif
#if defined(__x86_64__) || defined(__i386__) || defined(__APPLE__)
	const size_t first = name.find_first_not_of(' ');
	if (first == std::string::npos) return "";
	const size_t last = name.find_last_not_of(' ');
	return name.substr(first, last - first + 1);
#else
	return "";
#endif
}
}  // namespace

const char *ce_host_cpu_name(void)
{
	static const std::string name = readCpuName();
	return name.c_str();
}

uint64_t ce_host_memory_total(void)
{
#ifdef _WIN32
	MEMORYSTATUSEX status;
	status.dwLength = sizeof status;
	return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
#elif defined(__APPLE__)
	uint64_t total = 0;
	size_t size = sizeof total;
	return sysctlbyname("hw.memsize", &total, &size, nullptr, 0) == 0 ? total : 0;
#else
	uint64_t total = 0;
	if (FILE *f = std::fopen("/proc/meminfo", "r"))
	{
		char line[256];
		while (std::fgets(line, sizeof line, f) != nullptr)
		{
			unsigned long long kb = 0;
			if (std::sscanf(line, "MemTotal: %llu kB", &kb) == 1)
			{
				total = static_cast<uint64_t>(kb) * 1024;
				break;
			}
		}
		std::fclose(f);
	}
	return total;
#endif
}
