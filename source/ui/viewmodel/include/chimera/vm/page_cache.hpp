#pragma once
/* Copies of the pages of a memory domain that a list reads on the window's
 * thread, made on the machine's thread, which alone may ask the core (RAM
 * Search on a domain with no pointer: a bus, a paged memory). A page asked
 * for and not copied yet reads as zeros until the next copy.
 *
 * Every copy is made again after each frame, so only the pages a list still
 * reads are kept: one no row has read for about a second of frames is let
 * go, or a list scrolled far away kept a thousand pages, four megabytes read
 * from the core after every frame (issue 568). Pages side by side are read
 * from the core in one go. */

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace chimera::vm {

class PageCache
{
public:
	static constexpr int64_t kPage = 4096;
	/* 4 MiB: more than the rows any list shows */
	static constexpr size_t kMostPages = 1024;
	/* the copies made again after a page was last read before it is let go: a second's frames */
	static constexpr uint64_t kKeptCopies = 60;
	/* the core's read: bytes from an offset, how many it read */
	using Read = std::function<int64_t(int64_t offset, uint8_t *buf, int64_t len)>;

	/* the window's thread: the bytes copied, zeros where a page is not; true when a page was asked for */
	bool read(int64_t offset, uint8_t *buf, int64_t len)
	{
		std::lock_guard<std::mutex> lock(m_);
		bool missing = false;
		for (int64_t done = 0; done < len;)
		{
			const int64_t at = offset + done, page = at / kPage, in = at % kPage, n = std::min(len - done, kPage - in);
			if (const auto it = pages_.find(page); it != pages_.end())
			{
				std::memcpy(buf + done, it->second.bytes.data() + in, static_cast<size_t>(n));
				it->second.lastRead = copies_;
			}
			else
			{
				std::memset(buf + done, 0, static_cast<size_t>(n));
				missing = wanted_.insert(page).second || missing;
			}
			done += n;
		}
		return missing;
	}

	/* the machine's thread: the pages still read copied again, those asked for made; true when some were asked for */
	bool copy(const Read &read)
	{
		std::lock_guard<std::mutex> lock(m_);
		copies_++;
		std::erase_if(pages_, [this](const auto &p) { return copies_ - p.second.lastRead > kKeptCopies; });
		if (pages_.size() + wanted_.size() > kMostPages) pages_.clear();
		const bool asked = !wanted_.empty();
		for (const int64_t page : wanted_)
		{
			Page &p = pages_[page];
			p.bytes.resize(kPage);
			p.lastRead = copies_;
		}
		wanted_.clear();
		std::vector<uint8_t> run;
		for (auto it = pages_.begin(); it != pages_.end();)
		{
			auto end = it;
			int64_t n = 0;
			while (end != pages_.end() && end->first == it->first + n) ++end, ++n;
			run.resize(static_cast<size_t>(n * kPage));
			if (n > 1 && read(it->first * kPage, run.data(), n * kPage) == n * kPage)
			{
				for (int64_t i = 0; it != end; ++it, ++i)
					std::memcpy(it->second.bytes.data(), run.data() + i * kPage, static_cast<size_t>(kPage));
				continue;
			}
			/* one page, or a run the core would not read whole (one past the domain's end): a page at a time */
			for (; it != end; ++it)
				if (read(it->first * kPage, it->second.bytes.data(), kPage) < kPage) std::fill(it->second.bytes.begin(), it->second.bytes.end(), 0);
		}
		return asked;
	}

	void clear()
	{
		std::lock_guard<std::mutex> lock(m_);
		pages_.clear();
		wanted_.clear();
	}

	size_t kept() const
	{
		std::lock_guard<std::mutex> lock(m_);
		return pages_.size();
	}

private:
	struct Page
	{
		std::vector<uint8_t> bytes;
		uint64_t lastRead = 0;
	};
	mutable std::mutex m_;
	std::map<int64_t, Page> pages_;
	std::set<int64_t> wanted_;
	uint64_t copies_ = 0;
};

} // namespace chimera::vm
