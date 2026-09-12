/* What does an anchor cost the thread that runs the machine, before and after?
 *
 * storebench showed that taking a whole machine is 95 to 98% memcpy: 40 to 180
 * ms for a 257 MB machine warm and cold, once every anchorSpacing frames. The
 * copy does not need the machine to stand still, only the bytes, so miniBox can
 * hold the pages and let somebody else copy them (mb_block_state_plan).
 *
 * This measures the thing that matters: what is left ON the emulation thread.
 *   old - mb_block_save_state, which is the walk and the whole copy
 *   new - state_size + state_plan here, the copy on another thread, and
 *         plan_finish here at the end
 * and, because a machine that is standing still would be cheating, the guest
 * keeps writing pages while the copy runs - every one of those is a fault that
 * copies its page before the write lands, which is the cost this trade buys.
 *
 * It also compares the two states byte for byte, because a faster anchor that
 * is not the same anchor is worth nothing.
 *
 * Build and run with tests/perf/run-planbench.sh. Not part of any gate.
 */
#include "minibox_internal.h"

#include <pthread.h>
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

typedef struct { uint8_t *p; size_t n, cap; } buf;
static int32_t buf_write(uintptr_t ud, const uint8_t *data, uintptr_t n)
{
	buf *b = (buf *)ud;
	if (b->n + n > b->cap) return -1;
	memcpy(b->p + b->n, data, n);
	b->n += n;
	return 0;
}

static volatile uint8_t *gp(mb_block *b, uintptr_t off) { return (volatile uint8_t *)(b->addr.start + off); }

/* A machine that has been running: the first `dirty` bytes of it written since
 * the seal, which is what an anchor carries. */
static mb_block *running(uintptr_t size, uintptr_t dirty)
{
	mb_range a = { 0x36f00000000ull, size };
	mb_block *b = mb_block_new(a);
	mb_block_activate(b);
	mb_range r = { a.start, size };
	mb_block_mmap_fixed(b, r, MB_PROT_RW, true);
	for (uintptr_t off = 0; off < size; off += MB_PAGESIZE) gp(b, off)[0] = 1;
	mb_block_seal(b);
	for (uintptr_t off = 0; off < dirty; off += MB_PAGESIZE) gp(b, off)[0] = (uint8_t)(off >> 12);
	return b;
}

struct filler { mb_block *b; volatile int go; double seconds; };

static void *fill_thread(void *ud)
{
	struct filler *f = (struct filler *)ud;
	while (!f->go) { }
	const double t0 = now();
	mb_block_plan_fill(f->b, 0, mb_block_plan_count(f->b));
	f->seconds = now() - t0;
	return NULL;
}

int main(int argc, char **argv)
{
	const uintptr_t mb = argc > 1 ? (uintptr_t)atoll(argv[1]) : 256;
	const uintptr_t dirtyMb = argc > 2 ? (uintptr_t)atoll(argv[2]) : mb / 2;
	const size_t writes = argc > 3 ? (size_t)atoll(argv[3]) : 4096;

	const uintptr_t size = mb << 20, dirty = dirtyMb << 20;

	/* ---- the old way: the walk and the whole copy, here ---- */
	mb_block *b = running(size, dirty);
	const size_t len = mb_block_state_size(b);
	buf old = { (uint8_t *)malloc(len), 0, len };
	const double t0 = now();
	mb_block_save_state(b, buf_write, (uintptr_t)&old);
	const double oldMs = (now() - t0) * 1e3;
	mb_block_free(b);

	/* ---- the new way: plan here, copy there, finish here ---- */
	b = running(size, dirty);
	uint8_t *dest = (uint8_t *)calloc(1, len);
	struct filler f = { b, 0, 0 };
	pthread_t t;

	const double p0 = now();
	const size_t sized = mb_block_state_size(b);
	mb_block_state_plan(b, dest);
	const double planMs = (now() - p0) * 1e3;

	pthread_create(&t, NULL, fill_thread, &f);
	f.go = 1;
	/* the guest keeps running: every page it writes that the copy has not
	 * reached yet faults, and the handler copies it first */
	const double w0 = now();
	for (size_t i = 0; i < writes; i++)
	{
		const uintptr_t off = (i * 4093u * MB_PAGESIZE) % (dirty ? dirty : size);
		gp(b, off)[0] = 0xEE;
	}
	const double writeMs = (now() - w0) * 1e3;
	pthread_join(t, NULL);

	const double f0 = now();
	mb_block_plan_finish(b);
	const double finishMs = (now() - f0) * 1e3;

	const int same = sized == len && memcmp(dest, old.p, len) == 0;

	printf("%5lu MB machine, %5lu MB dirty (anchor %6.1f MB), %5zu pages written while it filled\n",
		(unsigned long)mb, (unsigned long)dirtyMb, len / 1048576.0, writes);
	printf("    on the emulation thread: old %8.2f ms   new %8.2f ms (plan %.2f + finish %.2f)   => %.1fx\n",
		oldMs, planMs + finishMs, planMs, finishMs,
		(planMs + finishMs) > 0 ? oldMs / (planMs + finishMs) : 0.0);
	printf("    elsewhere: the copy took %8.2f ms on its own thread; the guest's writes cost %.2f ms\n",
		f.seconds * 1e3, writeMs);
	printf("    the two states are %s\n", same ? "byte for byte the same" : "DIFFERENT - the plan is wrong");

	free(dest);
	free(old.p);
	mb_block_free(b);
	return same ? 0 : 1;
}
