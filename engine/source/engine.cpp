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

extern "C" const char *ce_build_info(void)
{
	return "{\"component\":\"chimera engine\""
	       ",\"commit\":\"" CE_BUILD_COMMIT "\""
	       ",\"compiler\":\"" CE_BUILD_COMPILER "\""
	       ",\"builtOn\":\"" CE_BUILD_OS "\""
#if defined(_WIN32)
	       ",\"target\":\"windows-x86_64\""
#else
	       ",\"target\":\"linux-x86_64\""
#endif
	       "}";
}
