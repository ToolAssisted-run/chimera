/* stride_tuner.h - how sparsely the near band may keep frames.
 *
 * The near band keeps every frame unless keeping them costs more than
 * kCostShare of the run; then it gets a stride, and the frames in between are
 * described by the next delta instead of being stored (state_history.hpp, "how
 * much of the run the history is allowed to cost").
 *
 * The stride is capped, and the cap is a setting (user-decided, 2026-09-15).
 * Uncapped, the tuner climbed to one frame in 32 on a Ruffle project and kept
 * it: the frames right behind the playhead - the ones a person rewinds to -
 * were the sparsest in the history, because the older ones had been captured
 * before the stride rose. Measured on that project on a GTX 1060, 2500 frames:
 * no history 12-18 s, every frame 71 s, one in four 41 s, one in eight 32 s,
 * one in thirty-two 22 s. The default cap is four: a rewind near the playhead
 * replays at most three frames.
 *
 * Why the cost share is not the whole story, recorded because it misled once:
 * the share counts the time inside a capture, but a frame that is stored also
 * runs slower - the sandbox write-tracks the pages it touches - and that cost
 * lands in the emulation, not in the capture. A sparser band made the run far
 * faster while the measured share barely moved, so a rule that keeps a raise
 * only when the share falls would have undone raises that were paying.
 *
 * Apart from StateHistory so that test_stride_tuner.cpp can check it with no
 * machine and no clock.
 */
#ifndef CHIMERA_STRIDE_TUNER_H
#define CHIMERA_STRIDE_TUNER_H

#include <cstdint>

struct CeStrideTuner
{
	static constexpr double kCostShare = 0.15;
	/* A capture that is quick in absolute terms is never worth thinning for. */
	static constexpr double kWorthThinning = 0.002;
	static constexpr int64_t kDefaultMaxStride = 4;
	static constexpr int64_t kLimitMaxStride = 32;   /* past this the replay is the cost */

	int64_t stride = 1;
	int64_t maxStride = kDefaultMaxStride;

	/* The cap, clamped to what it may be; the stride follows it down at once. */
	void setMaxStride(int64_t cap)
	{
		if (cap < 1) cap = 1;
		if (cap > kLimitMaxStride) cap = kLimitMaxStride;
		maxStride = cap;
		if (stride > maxStride) stride = maxStride;
	}

	/* One decision from the means since the last one. Returns the new stride. */
	int64_t decide(double captureSeconds, double wallSeconds)
	{
		if (wallSeconds <= 0 || captureSeconds < 0) return stride;
		const double share = captureSeconds / wallSeconds;
		int64_t want = stride;
		if (share > kCostShare && captureSeconds > kWorthThinning)
		{
			want = static_cast<int64_t>(stride * (share / kCostShare) + 0.5);
		}
		else if ((share < kCostShare / 3 || captureSeconds <= kWorthThinning) && stride > 1)
		{
			want = stride - 1;
		}
		if (want < 1) want = 1;
		if (want > maxStride) want = maxStride;
		stride = want;
		return stride;
	}
};

#endif /* CHIMERA_STRIDE_TUNER_H */
