#include "work_thread.hpp"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace chimera
{

bool WorkThread::allowed()
{
	static const int on = [] {
		const char *e = getenv("CHIMERA_HELPERS");
		/* absent means on: the helpers are the normal way to run. Present and
		 * false means off, and off must reach every helper in the process, or
		 * a differential test would be comparing two threaded paths. */
		return e != nullptr && (e[0] == '0' || e[0] == '\0') ? 0 : 1;
	}();
	return on != 0;
}

WorkThread::WorkThread(const char *name) : m_name(name != nullptr ? name : "helper")
{
	m_threaded = allowed();
}

WorkThread::~WorkThread()
{
	stop();
}

void WorkThread::start()
{
	/* Called with the lock held. A thread that will not start is not an error:
	 * the helper simply stays synchronous, which is slower and correct. */
	if (m_thread.joinable() || !m_threaded) return;
	try
	{
		m_thread = std::thread([this] { run(); });
	}
	catch (const std::exception &)
	{
		m_threaded = false;
		fprintf(stderr, "[helpers] %s could not start a thread; its work happens in line\n", m_name);
		fflush(stderr);
	}
}

void WorkThread::post(std::function<void()> job)
{
	if (!job) return;
	{
		std::unique_lock<std::mutex> lock(m_lock);
		if (m_threaded && !m_stopping)
		{
			start();
			if (m_thread.joinable())
			{
				m_jobs.push_back(std::move(job));
				lock.unlock();
				m_wake.notify_one();
				return;
			}
		}
	}
	/* No helper: here and now, on the caller's thread, which is the whole of
	 * the synchronous path. */
	job();
}

void WorkThread::run()
{
	for (;;)
	{
		std::function<void()> job;
		{
			std::unique_lock<std::mutex> lock(m_lock);
			m_wake.wait(lock, [this] { return m_stopping || !m_jobs.empty(); });
			if (m_jobs.empty())
			{
				if (m_stopping) return;
				continue;
			}
			job = std::move(m_jobs.front());
			m_jobs.pop_front();
			m_running = true;
		}
		job();
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_running = false;
		}
		m_idle.notify_all();
	}
}

void WorkThread::drain()
{
	std::unique_lock<std::mutex> lock(m_lock);
	if (!m_thread.joinable()) return;
	m_idle.wait(lock, [this] { return m_jobs.empty() && !m_running; });
}

size_t WorkThread::pending() const
{
	std::lock_guard<std::mutex> lock(m_lock);
	return m_jobs.size() + (m_running ? 1 : 0);
}

void WorkThread::setThreaded(bool on)
{
	if (!on)
	{
		/* Finish what was promised before going quiet: a job in the queue holds
		 * bytes somebody is owed. */
		stop();
		m_threaded = false;
		return;
	}
	std::lock_guard<std::mutex> lock(m_lock);
	m_threaded = allowed();   /* never against the process-wide switch */
}

void WorkThread::stop()
{
	std::thread mine;
	{
		std::lock_guard<std::mutex> lock(m_lock);
		if (!m_thread.joinable()) { m_stopping = false; return; }
		m_stopping = true;
		mine = std::move(m_thread);
	}
	m_wake.notify_all();
	/* Everything queued still runs: a job holds bytes somebody is owed, and
	 * abandoning one at shutdown would leave a half written file behind. */
	mine.join();
	std::lock_guard<std::mutex> lock(m_lock);
	m_stopping = false;
	m_jobs.clear();
}

} // namespace chimera
