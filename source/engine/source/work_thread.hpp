/* One helper thread, and the rules that make it safe to have one.
 *
 * WHY THIS EXISTS. The machine runs on one core because a movie that replays
 * is a machine whose every step is the same step. The greenzone is not the
 * machine - it is a cache of where the machine has been - and most of what it
 * costs is not thinking about the machine at all: copying bytes, writing them
 * to a file, composing two deltas into one. That work can happen elsewhere.
 * See docs/state-manager.md, "What a second thread could take, and what it
 * could never".
 *
 * WHAT IT IS ALLOWED TO BE. Deliberately the smallest thing that works: a
 * FIFO of jobs and a thread to run them. It is not a thread pool, not a work
 * stealer and not a scheduler, because the property that matters here is not
 * throughput but being able to say what is happening at any moment - the
 * history is the part of Chimera whose failures are silent, late, and land
 * somewhere else entirely.
 *
 * THE THREE RULES, which the callers depend on:
 *
 * 1. A job owns everything it touches. It is handed bytes and a destination
 *    and hands back bytes or a file range. It cannot walk the history, cannot
 *    ask what frame is newest and cannot free anything the loop can see.
 *    Nothing here enforces that - it is a rule about what is put in a job -
 *    but every use in this tree keeps it, and a use that did not would be the
 *    bug this file exists to prevent.
 *
 * 2. FIFO, one at a time. Jobs on one WorkThread run in the order they were
 *    posted, which is what lets an append-ordered file have a single writer
 *    and no lock.
 *
 * 3. It is droppable. `threaded()` false - by configuration, by a thread that
 *    would not start, or in a test - makes post() run the job right here, and
 *    then everything happens in line exactly as it did before any of this
 *    existed. That is not a fallback nobody exercises: it is the reference
 *    implementation the threaded path is tested against.
 */
#ifndef CHIMERA_WORK_THREAD_HPP
#define CHIMERA_WORK_THREAD_HPP

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace chimera
{

class WorkThread
{
public:
	/* `name` is for diagnostics only. The thread is started on the first post,
	 * so a history that never spills never makes one. */
	explicit WorkThread(const char *name);
	~WorkThread();

	WorkThread(const WorkThread &) = delete;
	WorkThread &operator=(const WorkThread &) = delete;

	/* Runs `job` on the helper, or here and now when there is no helper. */
	void post(std::function<void()> job);

	/* Blocks until everything posted before this call has finished.
	 *
	 * This is the "help finish" of the design, from the caller's side: it is
	 * bounded by work already in progress, and the worst case - one thread
	 * doing all of it - is the behaviour of the code that existed before. */
	void drain();

	/* Stops the thread, after finishing what is queued. */
	void stop();

	/* Jobs posted and not yet finished, the one running included. */
	size_t pending() const;

	/* Whether posts actually go somewhere else. False in a test, on a machine
	 * that would not give us a thread, and when CHIMERA_HELPERS=0. */
	bool threaded() const { return m_threaded; }

	/* Turns this one synchronous (after draining what it has). A history does
	 * this when a test asks for the reference behaviour. */
	void setThreaded(bool on);

	/* The switch every helper in the engine obeys: CHIMERA_HELPERS=0 turns
	 * them all off, which is how the gates and the differential fuzz run the
	 * synchronous path against the threaded one on the same build. */
	static bool allowed();

private:
	void start();
	void run();

	const char *m_name;
	mutable std::mutex m_lock;
	std::condition_variable m_wake;     /* the thread waits on work */
	std::condition_variable m_idle;     /* drain() waits on quiet */
	std::deque<std::function<void()>> m_jobs;
	std::thread m_thread;
	bool m_threaded = false;
	bool m_running = false;             /* a job is in the hand right now */
	bool m_stopping = false;
};

} // namespace chimera

#endif
