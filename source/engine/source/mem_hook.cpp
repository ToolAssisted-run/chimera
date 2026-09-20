/* The host's side of the memory hook: the seam a memory callback crosses.
 *
 * What a debugger wants is to be told when the machine touches an address. The
 * expensive way to do that is to ask the host on every access and let the host
 * compare - which is a sandbox crossing per emulated cycle, and makes a hooked
 * run unusable (ToolAssisted-run/chimera#113 is exactly that report, against
 * BizHawk). So the comparison lives in the GUEST: the engine tells a core what
 * to watch, the core compares, and only a MATCH comes back here.
 *
 * A match arrives through the sandbox's single callback shape (six integers
 * in, one out), on the same thread, INSIDE the frame, with the machine stopped
 * exactly where the access happened. That is the whole point: a callback that
 * fired at end of frame could not read the memory the access was about.
 *
 * The answer going back is 0 for "leave the value alone", or (1 << 32) | v to
 * replace it. A replacement CHANGES THE MACHINE and is not in the movie: see
 * docs/design-principles.md.
 *
 * The sink is global, like the progress sink and the cache directory: one
 * machine runs at a time, and the `user` pointer tells the frontend which of
 * its own objects a call belongs to.
 */
#include "chimera/engine.h"

#include <cstdint>

namespace
{
ce_memhook_fn s_sink = nullptr;
void *s_user = nullptr;
uint64_t s_calls = 0;
}  // namespace

extern "C" void ce_memhook_set_sink(ce_memhook_fn fn, void *user)
{
	s_sink = fn;
	s_user = user;
}

extern "C" uint64_t ce_memhook_calls(void) { return s_calls; }

#if defined(_WIN32) && defined(__GNUC__)
#define CE_MEMHOOK_ABI __attribute__((sysv_abi))
#else
#define CE_MEMHOOK_ABI
#endif

/* op is CE_MEMHOOK_OP_FIRE; the core has nothing else to say yet. An unknown
 * op answers 0, which every caller reads as "leave the value alone" - a core
 * built against a later engine than this one is not a reason to change what
 * the machine does. */
extern "C" uintptr_t CE_MEMHOOK_ABI ce_memhook_dispatch(
	uintptr_t op, uintptr_t addr, uintptr_t value, uintptr_t flags, uintptr_t scope, uintptr_t)
{
	if (op != 1 || s_sink == nullptr) return 0;
	s_calls++;
	const int64_t answer = s_sink(
		static_cast<int32_t>(scope), static_cast<uint32_t>(addr),
		static_cast<uint32_t>(value), static_cast<uint32_t>(flags), s_user);
	if (answer < 0) return 0;
	return (static_cast<uintptr_t>(1) << 32) | static_cast<uint32_t>(answer);
}
