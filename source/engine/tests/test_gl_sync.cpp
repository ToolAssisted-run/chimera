/* test_gl_sync.cpp - a GL sync object is named to the guest, never handed to it.
 *
 * The bridge keeps the driver's GLsync and gives the guest a name for it. The
 * name carries the slot AND a generation, because a saved state outlives the
 * session that made it: reloaded, it still holds the names it had then, and the
 * slots those names point at have since been reused. The generation is what
 * stops such a name addressing a stranger's fence.
 *
 * This checks the encoding on its own, with no driver and no context - the same
 * way test_gl_flight checks the recorder's layout rather than its behaviour.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/gl_sync_names.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>

int main()
{
	/* A name is never zero: zero is what a failed glFenceSync returns, and the
	 * guest must not be able to confuse "no fence" with "the first fence". */
	assert(ce_gl_sync_name_make(0, 1) != 0);
	assert(ce_gl_sync_name_make(0, 0) != 0);

	/* Slot and generation survive the round trip, at the edges too. */
	for (uint64_t slot : { (uint64_t)0, (uint64_t)1, (uint64_t)2, (uint64_t)4095, (uint64_t)0xfffffffeULL })
	{
		for (uint32_t gen : { 1u, 2u, 7u, 0xffffffffu })
		{
			const uint64_t name = ce_gl_sync_name_make(slot, gen);
			assert(ce_gl_sync_name_slot(name) == slot);
			assert(ce_gl_sync_name_generation(name) == gen);
		}
	}

	/* Two lives of one slot are different names, and the older one does not
	 * resolve to the newer one's generation. This is the property that makes a
	 * name from a reloaded state safe to reject. */
	const uint64_t first = ce_gl_sync_name_make(3, 1);
	const uint64_t second = ce_gl_sync_name_make(3, 2);
	assert(first != second);
	assert(ce_gl_sync_name_slot(first) == ce_gl_sync_name_slot(second));
	assert(ce_gl_sync_name_generation(first) != ce_gl_sync_name_generation(second));

	/* Zero names no slot at all, so a guest that hands back a null sync is told
	 * "not mine" rather than being served slot 0. */
	assert(ce_gl_sync_name_slot(0) == UINT64_MAX);

	/* A name whose low half is zero but whose high half is not - which is what
	 * a truncated or half-overwritten name looks like - still names no slot. */
	assert(ce_gl_sync_name_slot((uint64_t)9 << 32) == UINT64_MAX);

	std::printf("gl sync names ok\n");
	return 0;
}
