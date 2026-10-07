/* thread_string.hpp - a per-thread string the engine hands out as a C pointer,
 * and never destroys.
 *
 * Every "last error" the C API reports is one of these: the engine writes the
 * sentence into a per-thread string and returns `c_str()`, so two threads
 * opening two things at once cannot clobber each other's story.
 *
 * They must not be DESTROYED, and that is what this type is for.
 *
 * A `thread_local std::string` in a DLL built with mingw-w64 has its destructor
 * run TWICE on the thread that ends the process: once through the C runtime's
 * teardown and once more through the TLS callback the loader makes at
 * DLL_PROCESS_DETACH. A string still holding a heap buffer is therefore freed
 * twice, and the second free corrupts the CRT heap - not where it happens, but
 * later, wherever the heap next coalesces that block: a write in ntdll's free
 * path, minutes of log away from anything that names us. It cost chimera#123 a
 * user's five crashed games and two sessions to find, because a string that is
 * EMPTY has no buffer and the double free is harmless - so the bug only appears
 * on a path that reports a long error, which is exactly the path nobody
 * exercises until something goes wrong for a user. (Linux runs the destructor
 * once, so nothing there can see it; chimera-run links the engine statically
 * into an exe, where there is no DLL detach, so it cannot see it either. Only
 * the frontend can.)
 *
 * The pointer below has a trivial destructor, so no destructor is registered
 * for it at all, and the string it owns lives until the process ends. The cost
 * is one small string per thread that ever asked the engine for an error, never
 * given back - deliberately. What is bought is that the pointer handed to the
 * caller stays valid for as long as the process does, which is what a C API
 * that returns a `const char *` should mean anyway.
 *
 * The rule, for anything new: nothing with a non-trivial destructor may be
 * `thread_local` in the engine.
 */
#pragma once

#include <string>
#include <utility>

namespace chimera
{

class ThreadString
{
public:
	/* Constructed on first use per thread; never destroyed (see above). */
	std::string &operator*() const { return *value; }
	std::string *operator->() const { return value; }

private:
	std::string *value = new std::string();
};

} // namespace chimera
