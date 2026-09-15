/* test_gl_buffer_pool.cpp - a recycled buffer name has one owner at a time.
 *
 * The GPU bridge keeps a guest's deleted buffer names and serves them to later
 * gens (gl_buffer_pool.h). A guest may delete a name twice - legal GL, and
 * ordinary after a restore, when a renderer's restored handles name buffers the
 * frames before the restore had already deleted. The pool once pushed such a
 * name twice and served one buffer to two owners; the second owner's
 * glBufferData shrank the first one's storage, and every upload after that was
 * refused (a Ruffle rewind came back without its background).
 *
 * The bookkeeping on its own, with no driver and no context - the same way
 * test_gl_sync checks its names.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/gl_buffer_pool.h"

#include <cassert>
#include <cstdio>

int main()
{
	using D = CeGlBufferPool::Delete;

	/* A second delete of a pooled name does nothing, and the name is served once. */
	{
		CeGlBufferPool pool;
		pool.made(5);
		assert(pool.deleted(5) == D::Pool);
		assert(pool.deleted(5) == D::Ignore);
		assert(pool.deleted(5) == D::Ignore);
		assert(pool.held() == 1);
		assert(pool.take() == 5);
		assert(pool.take() == 0);
		/* served again, it is a new life: its delete pools once more */
		assert(pool.deleted(5) == D::Pool);
		assert(pool.take() == 5);
	}

	/* Names the pool never made go to the driver, and so do mapped and immutable ones. */
	{
		CeGlBufferPool pool;
		assert(pool.deleted(9) == D::Real);
		pool.made(6);
		pool.noteMapped(6, true);
		assert(pool.deleted(6) == D::Real);
		assert(!pool.isOurs(6) && !pool.isMapped(6));
		pool.made(7);
		pool.noteImmutable(7, true);
		assert(pool.deleted(7) == D::Real);
		assert(!pool.isOurs(7) && !pool.isImmutable(7));
		/* an unmapped buffer is recyclable again */
		pool.made(8);
		pool.noteMapped(8, true);
		pool.noteMapped(8, false);
		assert(pool.deleted(8) == D::Pool);
		assert(pool.take() == 8);
	}

	/* Zero is never a name. */
	{
		CeGlBufferPool pool;
		pool.made(0);
		assert(pool.deleted(0) == D::Real);
		assert(pool.take() == 0);
	}

	/* A full pool lets the delete through. */
	{
		CeGlBufferPool pool;
		for (uint32_t n = 1; n <= CeGlBufferPool::kMax + 1; n++) pool.made(n);
		for (uint32_t n = 1; n <= CeGlBufferPool::kMax; n++) assert(pool.deleted(n) == D::Pool);
		assert(pool.deleted(CeGlBufferPool::kMax + 1) == D::Real);
		assert(pool.held() == CeGlBufferPool::kMax);
	}

	/* Distrusted, it gives back what it holds, serves nothing, and every delete is real. */
	{
		CeGlBufferPool pool;
		pool.made(3);
		pool.made(4);
		assert(pool.deleted(3) == D::Pool);
		const std::vector<uint32_t> held = pool.distrust();
		assert(held.size() == 1 && held[0] == 3);
		assert(!pool.isPooled(3) && !pool.isOurs(3));
		assert(pool.take() == 0);
		assert(pool.deleted(4) == D::Real);
		/* the name handed back was deleted by the caller: another delete reaches the driver, which ignores it */
		assert(pool.deleted(3) == D::Real);
	}

	std::puts("gl buffer pool: ok");
	return 0;
}
