#include "progress.hpp"

#include "chimera/engine.h"

namespace {

ce_progress_fn g_fn = nullptr;
void *g_user = nullptr;

} // namespace

namespace chimera {

void progress(const char *stage, uint64_t done, uint64_t total)
{
	if (g_fn != nullptr) g_fn(stage, done, total, g_user);
}

bool progressWanted() { return g_fn != nullptr; }

} // namespace chimera

void ce_progress_set(ce_progress_fn fn, void *user)
{
	g_fn = fn;
	g_user = user;
}
