/* greenzone_shape.h - the shape a full greenzone is thinned to.
 *
 * The history keeps every frame it captures until its memory budget is full
 * (user-decided, 2026-09-15). Past that, it gives frames up toward a shape
 * measured back from the frontier - the newest stored frame - in bands whose
 * lengths double: with a near spacing s0 (the GreenzoneMaxNearStride setting,
 * 4 by default) and a goal of G snapshots per band (32), band k covers the
 * frames between s0*G*(2^k - 1) and s0*G*(2^(k+1) - 1) behind the frontier:
 * the last 128 frames, then to 384, 896, 1920, 3968, 8064 ... as many bands as
 * the run is long. G snapshots over each of them is 4 apart, then 8, 16, 32 ...
 *
 * G is a GOAL, not a quota: a heavy core may not have the budget for 32
 * snapshots in all. So a frame is given up by the band holding the MOST, ties
 * to the farthest - a band that aged with more than its share sheds it first,
 * and once bands are level every band shrinks in turn, the budget spread round
 * robin across them. A band's last snapshot is never given up. Frame 0 and the
 * frontier are kept on their own account and are not counted as members: counted,
 * the frontier was always the near band's last, the frame before it always went,
 * and a starved history collapsed to just those two.
 *
 * Apart from StateHistory so test_greenzone_shape.cpp can check it with no
 * machine.
 */
#ifndef CHIMERA_GREENZONE_SHAPE_H
#define CHIMERA_GREENZONE_SHAPE_H

#include <algorithm>
#include <cstdint>
#include <vector>

struct CeGreenzoneShape
{
	static constexpr int64_t kDefaultGoal = 32;

	/* Where band k ends, exclusive, in frames behind the frontier. */
	static int64_t bandEnd(int k, int64_t nearSpacing, int64_t goal)
	{
		const int64_t width = std::max<int64_t>(1, nearSpacing) * std::max<int64_t>(1, goal);
		if (k >= 62) return INT64_MAX;
		const int64_t doubling = (int64_t{ 1 } << (k + 1)) - 1;
		return width > INT64_MAX / doubling ? INT64_MAX : width * doubling;
	}

	/* The band a frame `distance` behind the frontier falls in. */
	static int bandOf(int64_t distance, int64_t nearSpacing, int64_t goal)
	{
		if (distance < 0) distance = 0;
		int k = 0;
		while (distance >= bandEnd(k, nearSpacing, goal)) k++;
		return k;
	}

	/* The bands in the order they give a frame up: most snapshots first, ties
	 * to the farthest. A band holding one (or none) never appears. */
	static std::vector<int> removalOrder(const std::vector<int64_t> &counts)
	{
		std::vector<int> order;
		for (size_t k = 0; k < counts.size(); k++)
		{
			if (counts[k] > 1) order.push_back(static_cast<int>(k));
		}
		std::sort(order.begin(), order.end(), [&](int a, int b) {
			if (counts[static_cast<size_t>(a)] != counts[static_cast<size_t>(b)])
				return counts[static_cast<size_t>(a)] > counts[static_cast<size_t>(b)];
			return a > b;
		});
		return order;
	}
};

#endif /* CHIMERA_GREENZONE_SHAPE_H */
