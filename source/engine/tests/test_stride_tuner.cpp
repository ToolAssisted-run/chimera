/* test_stride_tuner.cpp - the near band thins, but never past its cap.
 *
 * The greenzone's near band gets a stride when storing frames costs too much of
 * the run (stride_tuner.h). Uncapped it reached one frame in 32 on a Ruffle
 * project and the frames right behind the playhead were the sparsest in the
 * history (user-reported, 2026-09-15); the cap is a setting, four by default.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/stride_tuner.h"

#include <cassert>
#include <cstdio>

int main()
{
	const double frame = 0.1;

	/* A dear capture thins the band - up to the default cap and no further. */
	{
		CeStrideTuner t;
		assert(t.maxStride == CeStrideTuner::kDefaultMaxStride);
		assert(t.decide(0.03, frame) == 2);          /* 30%: one in two */
		assert(t.decide(0.06, 2 * frame) == 4);      /* still 30%: would be four */
		assert(t.decide(0.12, 4 * frame) == 4);      /* would be eight: held at the cap */
		for (int i = 0; i < 50; i++) assert(t.decide(0.4, frame) <= 4);
	}

	/* The cap is a setting, clamped to 1..32; lowering it pulls the stride down. */
	{
		CeStrideTuner t;
		t.setMaxStride(16);
		assert(t.decide(0.09, frame) == 6);          /* 90%: six */
		assert(t.decide(0.54, 6 * frame) == 16);     /* would be 36: sixteen */
		t.setMaxStride(3);
		assert(t.stride == 3);
		t.setMaxStride(0);
		assert(t.maxStride == 1 && t.stride == 1);
		t.setMaxStride(1000);
		assert(t.maxStride == CeStrideTuner::kLimitMaxStride);
	}

	/* A cap of one is every frame, whatever the cost. */
	{
		CeStrideTuner t;
		t.setMaxStride(1);
		assert(t.decide(0.9, 1.0) == 1);
	}

	/* A quick capture is never thinned for, whatever its share. */
	{
		CeStrideTuner t;
		assert(t.decide(0.001, 0.002) == 1);
	}

	/* A cheap run brings the stride back down one step at a time. */
	{
		CeStrideTuner t;
		t.stride = 4;
		assert(t.decide(0.0005, frame) == 3);
		assert(t.decide(0.0005, frame) == 2);
		assert(t.decide(0.0005, frame) == 1);
		assert(t.decide(0.0005, frame) == 1);
	}

	/* Nothing measured, nothing decided. */
	{
		CeStrideTuner t;
		t.stride = 2;
		assert(t.decide(0.1, 0.0) == 2);
	}

	std::puts("stride tuner: ok");
	return 0;
}
