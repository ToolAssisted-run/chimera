/* gl_buffer_pool.h - which recycled buffer names the GPU bridge may hand out.
 *
 * Twelve cores' renderers create and delete buffers by the thousand a frame, and
 * glGenBuffers alone cost 1.74 ms of one (see bufferPool() in gl_bridge.cpp). So
 * a guest's delete is kept rather than sent, and its name served to the next
 * gen. That is only sound for a name a real delete would have freed, and this
 * is the bookkeeping that decides it - apart from the GL calls, so it can be
 * checked without a driver or a context (test_gl_buffer_pool.cpp).
 *
 * The rule a pool must never break: one name, one owner. GL ignores a delete of
 * a name that is already deleted, so the pool must too. It once pushed such a
 * name a second time, two later gens were served the same buffer, and the
 * second owner's glBufferData shrank the first owner's storage - every upload
 * after that refused with GL_INVALID_VALUE. A double delete is legal GL and is
 * ordinary after a restore, when a renderer lets go of handles whose buffers
 * the frames before the restore had already deleted.
 */
#ifndef CHIMERA_GL_BUFFER_POOL_H
#define CHIMERA_GL_BUFFER_POOL_H

#include <cstddef>
#include <cstdint>
#include <vector>

struct CeGlBufferPool
{
	static const size_t kMax = 4096;

	enum class Delete
	{
		Ignore, /* already deleted: GL does nothing, and neither do we */
		Pool,   /* kept, to be served to a later gen */
		Real,   /* sent to the driver */
	};

	/* A name the driver just generated: ours from now on. */
	void made(uint32_t name) { flag(ours, name, true); }

	/* A name for the next gen, or 0 when the pool has none to give. */
	uint32_t take()
	{
		if (distrusted || pool.empty()) return 0;
		const uint32_t name = pool.back();
		pool.pop_back();
		flag(pooled, name, false);
		return name;
	}

	/* What a guest delete of `name` should do. Pool and Ignore are settled here;
	 * Real leaves the name to the caller's glDeleteBuffers and forgets it. */
	Delete deleted(uint32_t name)
	{
		if (is(pooled, name)) return Delete::Ignore;
		if (is(ours, name) && !is(mapped, name) && !is(immutable, name) && !distrusted && pool.size() < kMax)
		{
			pool.push_back(name);
			flag(pooled, name, true);
			return Delete::Pool;
		}
		flag(mapped, name, false);
		flag(immutable, name, false);
		flag(ours, name, false);
		return Delete::Real;
	}

	void noteMapped(uint32_t name, bool on) { flag(mapped, name, on); }
	void noteImmutable(uint32_t name, bool on) { flag(immutable, name, on); }
	bool isOurs(uint32_t name) const { return is(ours, name); }
	bool isPooled(uint32_t name) const { return is(pooled, name); }
	bool isMapped(uint32_t name) const { return is(mapped, name); }
	bool isImmutable(uint32_t name) const { return is(immutable, name); }
	bool isDistrusted() const { return distrusted; }

	/* The pool stops for good; the names it held are handed back for the caller
	 * to glDeleteBuffers, and are no longer ours. */
	std::vector<uint32_t> distrust()
	{
		distrusted = true;
		std::vector<uint32_t> held;
		held.swap(pool);
		for (uint32_t name : held)
		{
			flag(pooled, name, false);
			flag(ours, name, false);
		}
		return held;
	}

	size_t held() const { return pool.size(); }

private:
	std::vector<uint32_t> pool;
	std::vector<bool> ours, pooled, mapped, immutable;
	bool distrusted = false;

	static bool is(const std::vector<bool> &v, uint32_t name) { return name != 0 && name < v.size() && v[name]; }
	static void flag(std::vector<bool> &v, uint32_t name, bool on)
	{
		if (name == 0) return;
		if (v.size() <= name)
		{
			if (!on) return;
			v.resize((size_t)name + 1024, false);
		}
		v[name] = on;
	}
};

#endif /* CHIMERA_GL_BUFFER_POOL_H */
