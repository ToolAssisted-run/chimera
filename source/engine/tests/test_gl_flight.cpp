/* test_gl_flight.cpp - the GPU bridge's flight recorder says what crossed, in order.
 *
 * The recorder is read by nobody in the process that writes it: the crash module
 * reads it out of a DEAD process, through a handle, by layout alone
 * (source/crash/chimera_crash.c). So the layout is the contract, and this checks
 * it the way the module sees it - raw bytes at the address the engine hands out.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

extern "C" void ce_gl_state_loaded(int64_t to); /* the engine's own; the session calls it on every restore */

namespace {

struct Header
{
	uint32_t magic;
	uint32_t capacity;
	uint64_t written;
};

struct Entry
{
	uint32_t op;
	uint32_t detail;
};

const uint32_t kStateLoaded = 0xFFFFFF03u;

Header headerOf(void *recorder)
{
	Header h;
	std::memcpy(&h, recorder, sizeof h);
	return h;
}

Entry newestOf(void *recorder)
{
	const Header h = headerOf(recorder);
	Entry e;
	std::memcpy(&e, static_cast<const uint8_t *>(recorder) + sizeof h + ((h.written - 1) & (h.capacity - 1)) * sizeof(Entry), sizeof e);
	return e;
}

} // namespace

int main()
{
	uint32_t bytes = 12345;
	void *recorder = ce_gl_flight_recorder(&bytes);
	if (recorder == nullptr)
	{
		assert(bytes == 0);
		std::printf("test_gl_flight: this build has no GPU bridge, and says so (null, 0 bytes)\n");
		return 0;
	}

	Header h = headerOf(recorder);
	assert(h.magic == 0x4C474543u); /* "CEGL" */
	assert(h.capacity != 0 && (h.capacity & (h.capacity - 1)) == 0);
	assert(bytes == sizeof h + static_cast<uint64_t>(h.capacity) * sizeof(Entry));
	const uint64_t before = h.written;

	/* a restore is noted with or without a context: it is what a driver crash on reopen follows */
	ce_gl_state_loaded(4242);
	assert(headerOf(recorder).written == before + 1);
	Entry e = newestOf(recorder);
	assert(e.op == kStateLoaded && e.detail == 4242);

	/* nothing was borrowed, so giving the context back notes nothing */
	ce_gl_release();
	assert(headerOf(recorder).written == before + 1);

	/* the ring wraps, and the newest entry is always the last one written */
	for (uint32_t i = 0; i < h.capacity + 3; i++) ce_gl_state_loaded(i);
	h = headerOf(recorder);
	assert(h.written == before + 1 + h.capacity + 3);
	e = newestOf(recorder);
	assert(e.op == kStateLoaded && e.detail == h.capacity + 2);

	std::printf("test_gl_flight: ok (%u entries in %u bytes)\n", h.capacity, bytes);
	return 0;
}
