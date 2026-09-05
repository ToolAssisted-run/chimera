/* progress.hpp - one sink for "how far along is this" (see engine.h,
 * ce_progress_set). The engine's slow calls - hashing a disc, compressing a
 * greenzone, a boot fetching compiled code - report here as they go; a
 * frontend that installed a callback draws a bar, one that did not pays
 * nothing but a null check. */

#ifndef CHIMERA_PROGRESS_HPP
#define CHIMERA_PROGRESS_HPP

#include <cstdint>

namespace chimera {

/* Reports one step. total 0 means the end is not known (an activity count);
 * done and total are bytes or items, as the stage says. Cheap when nobody
 * listens. Called on whatever thread does the work, which is the caller's. */
void progress(const char *stage, uint64_t done, uint64_t total);

bool progressWanted();

} // namespace chimera

#endif
