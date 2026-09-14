/* gl_sync_names.h - the algebra behind naming a GL sync object to the guest.
 *
 * The GPU bridge may never hand the guest a pointer into our address space (see
 * the note at the top of gl_bridge.cpp), so a driver's GLsync is kept here and
 * the guest is given a NAME for it instead. A name is a slot index and a
 * generation: the slot says where the driver's pointer is kept, the generation
 * says which life of that slot it belongs to.
 *
 * The generation is the whole point. Slots are reused, and a state saved in one
 * session and reloaded in another still carries the names it had then. Without
 * a generation such a name would silently address whatever fence now occupies
 * that slot; with one it fails to resolve, and the caller treats it as a fence
 * that has already passed - which is the truth about GPU work whose process has
 * exited.
 *
 * The encoding lives here, apart from the GL types, so it can be checked on its
 * own (test_gl_sync.cpp) without a driver or a context.
 */
#ifndef CHIMERA_GL_SYNC_NAMES_H
#define CHIMERA_GL_SYNC_NAMES_H

#include <stdint.h>

/* Generation in the high half, slot+1 in the low, so a name is never zero -
 * zero is what a failed glFenceSync returns, and must not name anything. */
static inline uint64_t ce_gl_sync_name_make(uint64_t slot, uint32_t generation)
{
	return ((uint64_t)generation << 32) | (uint64_t)(slot + 1);
}

/* The slot a name refers to, or a value >= count when it refers to none. */
static inline uint64_t ce_gl_sync_name_slot(uint64_t name)
{
	const uint64_t low = name & 0xffffffffu;
	return low == 0 ? UINT64_MAX : low - 1;
}

static inline uint32_t ce_gl_sync_name_generation(uint64_t name)
{
	return (uint32_t)(name >> 32);
}

#endif /* CHIMERA_GL_SYNC_NAMES_H */
