/* test_greenzone_shape.cpp - the bands a full greenzone is thinned to.
 *
 * Measured back from the frontier, doubling in length, each aiming for the
 * same number of snapshots; the band with the most gives one up first, and a
 * band's last is never given up (user-decided, 2026-09-15; greenzone_shape.h).
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/greenzone_shape.h"

#include <cassert>
#include <cstdio>

int main()
{
	using S = CeGreenzoneShape;

	/* The defaults: 4 apart, 32 a band - the edges the user was shown. */
	assert(S::bandEnd(0, 4, 32) == 128);
	assert(S::bandEnd(1, 4, 32) == 384);
	assert(S::bandEnd(2, 4, 32) == 896);
	assert(S::bandEnd(3, 4, 32) == 1920);
	assert(S::bandEnd(4, 4, 32) == 3968);
	assert(S::bandEnd(5, 4, 32) == 8064);
	assert(S::bandEnd(6, 4, 32) == 16256);

	assert(S::bandOf(0, 4, 32) == 0);
	assert(S::bandOf(127, 4, 32) == 0);
	assert(S::bandOf(128, 4, 32) == 1);
	assert(S::bandOf(383, 4, 32) == 1);
	assert(S::bandOf(384, 4, 32) == 2);
	assert(S::bandOf(1919, 4, 32) == 3);
	assert(S::bandOf(1920, 4, 32) == 4);
	assert(S::bandOf(-5, 4, 32) == 0);

	/* Unbounded: a very long run just has more bands, and nothing overflows. */
	assert(S::bandOf(int64_t{ 1 } << 40, 4, 32) > 20);
	assert(S::bandEnd(70, 4, 32) == INT64_MAX);

	/* The near spacing and the goal scale the edges. */
	assert(S::bandEnd(0, 1, 8) == 8 && S::bandEnd(1, 1, 8) == 24);
	assert(S::bandOf(7, 1, 8) == 0 && S::bandOf(8, 1, 8) == 1);

	/* Most snapshots first; ties to the farthest; never a band's last. */
	{
		const std::vector<int> order = S::removalOrder({ 3, 5, 5, 1, 2, 0 });
		const std::vector<int> want = { 2, 1, 0, 4 };
		assert(order == want);
	}
	assert(S::removalOrder({ 1, 1, 1 }).empty());
	assert(S::removalOrder({}).empty());

	/* Level bands take turns: giving up from the farthest tied band leaves the
	 * next one tied band ahead - round robin with no cursor to keep. */
	{
		std::vector<int64_t> counts = { 4, 4, 4 };
		std::vector<int> taken;
		for (int i = 0; i < 6; i++)
		{
			const int k = S::removalOrder(counts).front();
			taken.push_back(k);
			counts[static_cast<size_t>(k)]--;
		}
		const std::vector<int> want = { 2, 1, 0, 2, 1, 0 };
		assert(taken == want);
		assert(counts == (std::vector<int64_t>{ 2, 2, 2 }));
	}

	std::puts("greenzone shape: ok");
	return 0;
}
