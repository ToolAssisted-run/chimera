/* compile_sessions.h - how many compile sessions a machine can run at once.
 *
 * A core that recompiles can fill its cache before a game first runs: the
 * frontend starts several sessions side by side, each compiling a share
 * (docs/compile-cache.md). How many is a question about memory, and it was
 * answered wrongly twice.
 *
 * WHAT A SESSION COSTS (measured, Windows, chimera issue 259). One session
 * compiling a PlayStation 3 game holds about 7 GB: four of them took 25.8 GB
 * of the system's commit within 80 seconds of starting and 27.9 GB at their
 * peak. Of that only 1.6 to 1.75 GB is the process's private memory, which is
 * the figure the old sizing was made from (4 GB a session "with a wide
 * margin"). The rest is the sandbox's memory: sections backed by the page
 * file, which a process's private bytes do not count and a job object's limit
 * does not see. So the figure here is 8 GB.
 *
 * WHAT A MACHINE HAS TO GIVE. On Windows every one of those bytes has to be
 * promised by the system before it is used ("commit"), and the promise is
 * limited by memory plus the page file, not by memory that happens to be
 * free. Asked for more than it has, Windows makes the page file bigger -
 * which is on the system drive whatever folder the frontend keeps its data
 * in - and when the drive is full the sessions die. That is what the report
 * was: seven sessions on a machine sized by its free memory, 18.8 GB gone
 * from C: in the course of one compile, and every session dead at the end.
 * So what a Windows machine can spare is the smaller of its free memory and
 * of what it can still promise with the page file as big as it is now.
 */
#ifndef CHIMERA_COMPILE_SESSIONS_H
#define CHIMERA_COMPILE_SESSIONS_H

#include <algorithm>
#include <cstdint>

/* What one session is taken to need unless the person says otherwise (a
 * setting of the frontend's, user-decided 2026-10-14: 8 GB is RPCS3's measured
 * need, and a machine or a core may want another figure). */
inline constexpr uint64_t kCompileSessionBytes = 8ull << 30;
/* Left for the system and for the frontend itself, never handed to sessions. */
inline constexpr uint64_t kCompileReservedBytes = 4ull << 30;
/* More than this many side by side has not been worth it. */
inline constexpr int32_t kCompileSessionsAtMost = 8;

/* What the machine says of its memory; a field is 0 when it will not say. */
struct CeHostMemory
{
	uint64_t freePhysical = 0;  /* memory nothing is using */
	uint64_t freeCommit = 0;    /* what may still be promised without the page file growing */
};

/* Read it from the system. */
CeHostMemory ceReadHostMemory();

/* What can be handed out now: the smaller of the two that are known, 0 when
 * neither is. */
inline uint64_t ceMemoryToSpare(const CeHostMemory &m)
{
	if (m.freePhysical == 0) return m.freeCommit;
	if (m.freeCommit == 0) return m.freePhysical;
	return std::min(m.freePhysical, m.freeCommit);
}

/* The greenzone's memory budget a computer can hold (issue 577): three
 * quarters of what can be spared now plus what the greenzone already holds
 * (that memory is the computer's too, but is no longer free), rounded down to
 * a whole megabyte; the rest is left for the game, the core and the system.
 * Returns that figure when `budget` is more than it, which is when to warn,
 * and 0 when the budget is fine or the computer will not say what it has. */
inline uint64_t ceGreenzoneBudgetAdvice(uint64_t budget, uint64_t toSpare, uint64_t held)
{
	if (toSpare == 0) return 0;
	const uint64_t mb = 1ull << 20;
	const uint64_t suggested = std::max<uint64_t>((toSpare + held) / 4 * 3 / mb, 64) * mb;
	return budget > suggested ? suggested : 0;
}

/* How many sessions run side by side: one per sessionBytes (0: the default,
 * kCompileSessionBytes) of what can be spared after kCompileReservedBytes,
 * never more than half the processors, never more than kCompileSessionsAtMost,
 * never fewer than one. A machine that will not say what it has (0) gets one. */
inline int32_t ceCompileSessions(int32_t processors, uint64_t toSpare, uint64_t sessionBytes = 0)
{
	if (sessionBytes == 0) sessionBytes = kCompileSessionBytes;
	const int32_t byProcessors = std::max(1, processors / 2);
	const uint64_t spare = toSpare > kCompileReservedBytes ? toSpare - kCompileReservedBytes : 0;
	const uint64_t byMemory = std::max<uint64_t>(1, spare / sessionBytes);
	return static_cast<int32_t>(std::min<uint64_t>({static_cast<uint64_t>(kCompileSessionsAtMost),
		static_cast<uint64_t>(byProcessors), byMemory}));
}

/* Whether memory ran short for sessions that were running: what could be
 * spared at the worst moment was less than half of what one more would need
 * (sessionBytes, 0 the default). Unknown (0) is not "short": nothing is said
 * of a machine that says nothing. */
inline bool ceCompileMemoryRanShort(uint64_t leastToSpare, uint64_t sessionBytes = 0)
{
	if (sessionBytes == 0) sessionBytes = kCompileSessionBytes;
	return leastToSpare != 0 && leastToSpare < sessionBytes / 2;
}

#endif
