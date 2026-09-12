/* How much of a capture is the COPY - the part a second thread could carry?
 *
 * epochbench answers what a captured frame costs; its sink counts bytes and
 * throws them away, because the question there was the epoch machinery. This
 * asks the other half: of what a capture spends, how much is miniBox walking
 * its page tables (which happens where the machine is, and can happen nowhere
 * else) and how much is memcpy into the history's buffer (which is bytes
 * moving from one place to another and cares about no lock and no order)?
 *
 * That ratio is the whole case for and against a helper thread, so it is
 * measured rather than assumed. Both halves are timed twice over the same
 * work: once with a sink that only counts, once with a sink that appends the
 * way StateHistory's does, into a buffer reserved up front the way
 * StateHistory reserves it. The difference is the copy.
 *
 * Two captures, because they are different animals:
 *   - the DELTA, taken every frame, whose size is what the frame wrote;
 *   - the ANCHOR, taken every anchorSpacing frames (600, ten seconds), whose
 *     size is what the machine IS. Nothing measured this before, and it is the
 *     largest single stall the capture path has.
 *
 * Writes are scattered as widely as the arena allows, which is the worst case
 * on purpose - a real machine writes in clusters. Build and run with
 * tests/perf/run-storebench.sh. Not part of any gate: it answers a question
 * about cost, not about correctness.
 */
#include "minibox_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* the sink that only counts: what the walk costs with nothing carried */
static size_t g_counted;
static int sink_count(uintptr_t ud, const unsigned char *p, size_t n)
{
	(void)ud; (void)p;
	g_counted += n;
	return 0;
}

/* the sink StateHistory has: append into a vector reserved up front. Never
 * grows here, for the same reason the real one reserves - a buffer that
 * doubles copies everything it holds on the way, which would be measuring the
 * allocator rather than the capture. */
typedef struct { unsigned char *p; size_t n, cap; } buf;
static int sink_store(uintptr_t ud, const unsigned char *p, size_t n)
{
	buf *b = (buf *)ud;
	if (b->n + n > b->cap) return -1;
	memcpy(b->p + b->n, p, n);
	b->n += n;
	return 0;
}

int main(int argc, char **argv)
{
	uintptr_t mb = argc > 1 ? (uintptr_t)atoll(argv[1]) : 2048;
	size_t touch = argc > 2 ? (size_t)atoll(argv[2]) : 256;
	int frames = argc > 3 ? atoi(argv[3]) : 40;
	/* what the machine IS, which is what an anchor carries: a real one is a
	 * fraction of its arena, so this is given rather than assumed */
	uintptr_t dirtyMb = argc > 4 ? (uintptr_t)atoll(argv[4]) : mb / 8;

	uintptr_t size = mb << 20;
	mb_range a = { 0x36f00000000ull, size };
	mb_block *b = mb_block_new(a);
	if (!b) { fprintf(stderr, "block\n"); return 1; }
	mb_block_activate(b);
	mb_range r = { b->addr.start, size };
	mb_block_mmap_fixed(b, r, MB_PROT_RW, true);
	mb_block_seal(b);

	/* a machine that has been running: the part of it an anchor will carry */
	for (uintptr_t off = 0; off < (dirtyMb << 20); off += MB_PAGESIZE)
		((volatile uint8_t *)(b->addr.start + off))[0] = 1;

	/* ---- the anchor: what the machine is ----
	 *
	 * The best of a few, like restorebench: the first pass walks half a
	 * gigabyte of cold pages and measures the cache filling up rather than the
	 * work. A capture on a machine that has been running is the warm case. */
	g_counted = 0;
	mb_block_save_state(b, sink_count, 0);
	const size_t anchorBytes = g_counted;

	buf whole = { 0 };
	whole.cap = anchorBytes + MB_PAGESIZE;
	whole.p = (unsigned char *)malloc(whole.cap);
	if (!whole.p) { fprintf(stderr, "out of memory for a %.1f MB anchor\n", anchorBytes / 1048576.0); return 1; }

	double bestWalk = 1e9, bestStore = 1e9;
	for (int rep = 0; rep < 3; rep++)
	{
		g_counted = 0;
		double t0 = now();
		mb_block_save_state(b, sink_count, 0);
		double t1 = now();
		if (t1 - t0 < bestWalk) bestWalk = t1 - t0;

		whole.n = 0;
		double t2 = now();
		mb_block_save_state(b, sink_store, (uintptr_t)&whole);
		double t3 = now();
		if (t3 - t2 < bestStore) bestStore = t3 - t2;
	}

	printf("%5lu MB arena, %5lu MB dirty: anchor %8.1f MB   walk %7.3f ms   walk+store %7.3f ms"
		"   => the copy is %6.3f ms (%.0f%%)\n",
		(unsigned long)mb, (unsigned long)dirtyMb, anchorBytes / 1048576.0,
		bestWalk * 1e3, bestStore * 1e3, (bestStore - bestWalk) * 1e3,
		bestStore > 0 ? (bestStore - bestWalk) / bestStore * 100 : 0);
	free(whole.p);

	/* ---- the delta: what a frame did ----
	 *
	 * Each measurement is its own frame's worth of writes, so the two sinks
	 * see the same amount of work rather than one seeing pages the other
	 * already reported. */
	buf small = { 0 };
	small.cap = (touch + 64) * (MB_PAGESIZE + 64) + MB_PAGESIZE;
	small.p = (unsigned char *)malloc(small.cap);
	size_t spread = (size / MB_PAGESIZE) / (touch ? touch : 1);
	double walk = 0, store = 0;
	size_t deltaBytes = 0;
	for (int f = 0; f < frames; f++)
	{
		mb_block_epoch_begin(b);
		for (size_t i = 0; i < touch; i++)
		{
			uintptr_t page = ((i * spread) + (size_t)f) % (size / MB_PAGESIZE);
			((volatile uint8_t *)(b->addr.start + (page << MB_PAGESHIFT)))[0] = (uint8_t)f;
		}
		g_counted = 0;
		double d0 = now();
		mb_block_delta_save(b, true, sink_count, 0);
		double d1 = now();
		deltaBytes = g_counted;

		mb_block_epoch_begin(b);
		for (size_t i = 0; i < touch; i++)
		{
			uintptr_t page = ((i * spread) + (size_t)f) % (size / MB_PAGESIZE);
			((volatile uint8_t *)(b->addr.start + (page << MB_PAGESHIFT)))[0] = (uint8_t)(f + 1);
		}
		small.n = 0;
		double d2 = now();
		mb_block_delta_save(b, true, sink_store, (uintptr_t)&small);
		double d3 = now();

		walk += d1 - d0;
		store += d3 - d2;
	}
	printf("%5lu MB arena, %5zu pages/frame: delta  %8.1f KB   walk %7.3f ms   walk+store %7.3f ms"
		"   => the copy is %6.3f ms (%.0f%%)\n",
		(unsigned long)mb, touch, deltaBytes / 1024.0,
		walk / frames * 1e3, store / frames * 1e3, (store - walk) / frames * 1e3,
		store > 0 ? (store - walk) / store * 100 : 0);

	free(small.p);
	mb_block_free(b);
	return 0;
}
