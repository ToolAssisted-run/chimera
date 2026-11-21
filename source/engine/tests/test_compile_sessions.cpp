/* test_compile_sessions.cpp - how many compile sessions a machine is given.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/compile_sessions.h"
#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#ifdef __linux__
#include <sys/wait.h>
#include <unistd.h>
#endif

int main()
{
	const uint64_t GB = 1ull << 30;

	/* The report this was written for (chimera issue 259): sixteen processors
	 * and 32 GB to spare got seven sessions at 4 GB each, which asked Windows
	 * for 49 GB. At what a session really holds it gets three. */
	assert(ceCompileSessions(16, 32 * GB) == 3);

	/* Never more than half the processors, however much memory there is. */
	assert(ceCompileSessions(4, 200 * GB) == 2);
	assert(ceCompileSessions(1, 200 * GB) == 1);

	/* Never more than eight. */
	assert(ceCompileSessions(64, 500 * GB) == 8);

	/* Four gigabytes are kept back before anything is handed out, and a
	 * session is eight: 12 GB is one session, 20 GB is two. */
	assert(ceCompileSessions(16, 12 * GB) == 1);
	assert(ceCompileSessions(16, 20 * GB - 1) == 1);
	assert(ceCompileSessions(16, 20 * GB) == 2);

	/* A machine with almost nothing still gets its one session: it may fail,
	 * and then it says why. One that will not say what it has gets one too. */
	assert(ceCompileSessions(16, 1 * GB) == 1);
	assert(ceCompileSessions(16, 0) == 1);

	/* The figure is a setting (2026-10-14): 4 GB a session gives the reported
	 * machine seven, 16 GB gives it one; 0 is the default, 8 GB. */
	assert(ceCompileSessions(16, 32 * GB, 4 * GB) == 7);
	assert(ceCompileSessions(16, 32 * GB, 16 * GB) == 1);
	assert(ceCompileSessions(16, 32 * GB, 0) == 3);
	assert(ceCompileMemoryRanShort(3 * GB, 8 * GB));
	assert(!ceCompileMemoryRanShort(3 * GB, 4 * GB));

	/* The greenzone budget a computer can hold (issue 577): 20096 MB on a
	 * computer with 20 GB to spare is too much, and 15 GB is what it suggests;
	 * what the greenzone holds already counts as the computer's; a budget that
	 * fits, or a computer that says nothing, gets no advice. */
	{
		const uint64_t MB = 1ull << 20;
		assert(ceGreenzoneBudgetAdvice(20096 * MB, 20 * GB, 0) == 15 * GB);
		assert(ceGreenzoneBudgetAdvice(20096 * MB, 10 * GB, 10 * GB) == 15 * GB);
		assert(ceGreenzoneBudgetAdvice(8 * GB, 20 * GB, 0) == 0);
		assert(ceGreenzoneBudgetAdvice(15 * GB, 20 * GB, 0) == 0);
		assert(ceGreenzoneBudgetAdvice(20096 * MB, 0, 0) == 0);
		assert(ceGreenzoneBudgetAdvice(10 * GB, 1 * MB, 0) == 64 * MB);
	}

	/* What can be spared is the smaller of free memory and of what the system
	 * can still promise: a machine with 40 GB free and 9 GB of promises left
	 * has 9. A figure the system would not give is left out, not taken for 0. */
	{
		CeHostMemory m;
		m.freePhysical = 40 * GB;
		m.freeCommit = 9 * GB;
		assert(ceMemoryToSpare(m) == 9 * GB);
		m.freeCommit = 0;
		assert(ceMemoryToSpare(m) == 40 * GB);
		m.freePhysical = 0;
		assert(ceMemoryToSpare(m) == 0);
		m.freeCommit = 5 * GB;
		assert(ceMemoryToSpare(m) == 5 * GB);
	}

	/* Memory ran short when, at the worst moment, less than half a session's
	 * worth was left. Nothing known is not "short". */
	assert(ceCompileMemoryRanShort(1 * GB));
	assert(ceCompileMemoryRanShort(4 * GB - 1));
	assert(!ceCompileMemoryRanShort(4 * GB));
	assert(!ceCompileMemoryRanShort(0));

	/* And this machine says something of itself. */
	{
		const CeHostMemory m = ceReadHostMemory();
		assert(m.freePhysical != 0);
		assert(ceMemoryToSpare(m) != 0);
	}

	/* A session asks Linux to stop it before anything else when memory runs
	 * out (chimera issue 300: the kernel stopped Chimera's own window). Done
	 * in a child, which reads its score back, so this test keeps its own. */
#ifdef __linux__
	{
		const pid_t child = fork();
		assert(child >= 0);
		if (child == 0)
		{
			if (ce_compile_session_begins() != 1) _exit(2);
			FILE *f = std::fopen("/proc/self/oom_score_adj", "r");
			int score = 0;
			if (!f || std::fscanf(f, "%d", &score) != 1) _exit(3);
			std::fclose(f);
			_exit(score == 1000 ? 0 : 4);
		}
		int status = 0;
		assert(waitpid(child, &status, 0) == child);
		assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
	}
#else
	assert(ce_compile_session_begins() == 0);
#endif

	std::printf("compile_sessions: ok\n");
	return 0;
}
