/* engine.cpp - ABI version and provenance.
 *
 * Provenance follows miniBox's convention exactly (see its diag.c): the values
 * arrive as compile-time defines from meson, are a function of the inputs only,
 * and end up shown by the frontend and cited by movies.
 */

#include "chimera/engine.h"

extern "C" uint32_t ce_abi_version(void) { return CE_ABI_VERSION; }

#ifndef CE_BUILD_COMMIT
#define CE_BUILD_COMMIT "unknown"
#endif
#ifndef CE_BUILD_COMPILER
#define CE_BUILD_COMPILER "unknown"
#endif
#ifndef CE_BUILD_OS
#define CE_BUILD_OS "unknown"
#endif

#if defined(_WIN32)
#define CE_TARGET_OS "windows"
#else
#define CE_TARGET_OS "linux"
#endif
#if defined(__aarch64__)
#define CE_TARGET_CPU "aarch64"
#elif defined(__x86_64__)
#define CE_TARGET_CPU "x86_64"
#else
#error "Chimera builds for x86-64 and aarch64 only"
#endif

extern "C" const char *ce_build_info(void)
{
	return "{\"component\":\"chimera engine\""
	       ",\"commit\":\"" CE_BUILD_COMMIT "\""
	       ",\"compiler\":\"" CE_BUILD_COMPILER "\""
	       ",\"builtOn\":\"" CE_BUILD_OS "\""
	       ",\"target\":\"" CE_TARGET_OS "-" CE_TARGET_CPU "\""
	       "}";
}
