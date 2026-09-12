#include "state_history.hpp"
#include "zstd_dyn.hpp"

#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#ifdef _WIN32
#include <process.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <chrono>
#include <new>

namespace chimera
{

namespace
{

/* a note is a caller's few bytes about a frame; anything claiming more is damage */
const uint64_t kMaxNote = 4096;

/* the stream shims the host's callbacks want */
struct ByteSink { StateHistory::Bytes *out; };
int32_t sinkWrite(uintptr_t ud, const void *data, uintptr_t len)
{
	auto *s = reinterpret_cast<ByteSink *>(ud);
	const auto *p = static_cast<const uint8_t *>(data);
	s->out->insert(s->out->end(), p, p + len);
	return 0;
}

struct ByteSource { const uint8_t *data; uintptr_t len, pos; };
intptr_t sourceRead(uintptr_t ud, void *out, uintptr_t len)
{
	auto *s = reinterpret_cast<ByteSource *>(ud);
	uintptr_t n = s->len - s->pos;
	if (n > len) n = len;
	if (n == 0) return -1;
	std::memcpy(out, s->data + s->pos, n);
	s->pos += n;
	return static_cast<intptr_t>(n);
}

/* The spill file is named for the process and the instance, not for the
 * directory alone. Two histories handed the same directory - a project opened
 * again before the session that had it is gone, or the same project open
 * twice - used to open the same file, and the second truncated the first's:
 * every frame the first had on disk came back as garbage. A name of its own is
 * the whole fix; what a crashed session leaves behind is a cache file like any
 * other, and the cache manager's bound on the directory takes it in time. */
static std::string spillFileName()
{
	static int seq = 0;
#ifdef _WIN32
	const long pid = static_cast<long>(_getpid());
#else
	const long pid = static_cast<long>(getpid());
#endif
	return "history-spill-" + std::to_string(pid) + "-" + std::to_string(++seq) + ".bin";
}

/* fseek and ftell take a long, which is 32 bits on Windows - and the spill file
 * is the one thing here designed to pass two gigabytes. */
#ifdef _WIN32
bool seekTo(std::FILE *f, uint64_t at) { return _fseeki64(f, static_cast<__int64>(at), SEEK_SET) == 0; }
bool seekEnd(std::FILE *f) { return _fseeki64(f, 0, SEEK_END) == 0; }
bool tellAt(std::FILE *f, uint64_t &at)
{
	const __int64 n = _ftelli64(f);
	if (n < 0) return false;
	at = static_cast<uint64_t>(n);
	return true;
}
#else
bool seekTo(std::FILE *f, uint64_t at) { return fseeko(f, static_cast<off_t>(at), SEEK_SET) == 0; }
bool seekEnd(std::FILE *f) { return fseeko(f, 0, SEEK_END) == 0; }
bool tellAt(std::FILE *f, uint64_t &at)
{
	const off_t n = ftello(f);
	if (n < 0) return false;
	at = static_cast<uint64_t>(n);
	return true;
}
#endif

bool writeAll(std::FILE *f, const void *data, size_t n)
{
	return n == 0 || std::fwrite(data, 1, n, f) == n;
}

bool readAll(std::FILE *f, void *data, size_t n)
{
	return n == 0 || std::fread(data, 1, n, f) == n;
}

bool writeU64(std::FILE *f, uint64_t v) { return writeAll(f, &v, sizeof v); }
bool readU64(std::FILE *f, uint64_t &v) { return readAll(f, &v, sizeof v); }


/* CHIMERA_HISTORY_TRACE=1 says what the history stored and what it cost. A
 * delta silently falling back to a whole state is the failure mode with no
 * symptom - everything still works, it just costs a hundred times more - so
 * there has to be a way to look. */
double nowSeconds()
{
	/* steady_clock rather than clock_gettime, which mingw does not have - this
	 * is only ever read under CHIMERA_HISTORY_TRACE, but a diagnostic that
	 * fails to link on the platform people run is worse than no diagnostic. */
	const auto t = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration<double>(t).count();
}

bool historyTrace()
{
	static const int on = [] {
		const char *e = getenv("CHIMERA_HISTORY_TRACE");
		return e != nullptr && e[0] != '\0' && e[0] != '0' ? 1 : 0;
	}();
	return on != 0;
}

} // namespace

void StateHistory::configure(const HostApi *host, void *obj, uint64_t budgetBytes)
{
	/* Before the host changes, not after: the pages being held belong to the
	 * OLD machine and only the old host can let them go. */
	finishPlan();
	m_host = host;
	m_obj = obj;
	m_budget = budgetBytes;
	/* Everything stored goes, the spill file with it: a history configured
	 * over one that had spilled kept the file open and its live count, and
	 * then held the disk budget against stretches that no longer existed. The
	 * directory is kept - the next spill opens a fresh file there. */
	clear();
}

void StateHistory::flushWrites()
{
	drainWriter();
	evictDisk();   /* and what that landed is held to the budget, like a frame's */
}

void StateHistory::helpers(bool on)
{
	m_writer.setThreaded(on);
	applyWrites();   /* setThreaded(false) finishes what was queued; take its word now */
}

void StateHistory::clear()
{
	finishPlan();
	m_segments.clear();
	m_bytes = 0;
	m_epochOpen = false;
	m_newest = -1;
	m_nearStride = 1;
	m_captureSeconds = 0;
	m_wallSeconds = 0;
	m_lastCaptureEnded = 0;
	m_capturesSinceTuned = 0;
	dropSpillFile();
}

/* CHIMERA_NO_DELTAS=1 keeps whole states, as the greenzone did before epochs.
 * It is here for the same reason CHIMERA_NO_GPU is: two things that fail
 * differently should be tellable apart on a machine that is not here, and a
 * regression in capture cost or in seek latency wants an A against its B on
 * the same build. */
static bool deltasRefused()
{
	static const int off = [] {
		const char *e = getenv("CHIMERA_NO_DELTAS");
		return e != nullptr && e[0] != '\0' && e[0] != '0' ? 1 : 0;
	}();
	return off != 0;
}

/* One layout, used by the spill file and by a saved history alike - which is
 * what lets a saved history copy a spilled segment through byte for byte
 * instead of rebuilding it:
 *
 *   anchor: length, bytes, note length, note
 *   links:  count, then per link: the frame it lands on, note length, note,
 *           length, bytes
 */
bool StateHistory::writeSegmentBodyTo(const std::function<bool(const void *, size_t)> &put, const Segment &seg)
{
	auto u64 = [&](uint64_t v) { return put(&v, sizeof v); };
	bool ok = u64(seg.anchor.size())
		&& put(seg.anchor.data(), seg.anchor.size())
		&& u64(seg.anchorNote.size())
		&& put(seg.anchorNote.data(), seg.anchorNote.size())
		&& u64(seg.links.size());
	for (const Link &l : seg.links)
	{
		if (!ok) break;
		ok = u64(static_cast<uint64_t>(l.endFrame))
			&& u64(l.note.size())
			&& put(l.note.data(), l.note.size())
			&& u64(l.bytes.size())
			&& put(l.bytes.data(), l.bytes.size());
	}
	return ok;
}

bool StateHistory::writeSegmentBody(std::FILE *f, const Segment &seg)
{
	return writeSegmentBodyTo([f](const void *d, size_t n) { return writeAll(f, d, n); }, seg);
}

namespace
{

/* CHIMERA_SPILL_RAW=1 writes spilled bodies uncompressed, for the same reason
 * CHIMERA_NO_DELTAS exists: an A against a B on one build. */
bool spillRaw()
{
	static const int raw = [] {
		const char *e = getenv("CHIMERA_SPILL_RAW");
		return e != nullptr && e[0] != '\0' && e[0] != '0' ? 1 : 0;
	}();
	return raw != 0;
}

/* Compresses as the body is serialised. A body is up to a whole machine, and
 * holding it a second time just to compress it would be exactly the memory a
 * spill exists to give back. */
struct ZstdBodySink
{
	std::FILE *f;
	const ZstdApi *z;
	void *cs = nullptr;
	std::vector<uint8_t> out;
	bool failed = false;

	ZstdBodySink(std::FILE *file, const ZstdApi *api) : f(file), z(api), out(1u << 20)
	{
		cs = z->createCStream();
		/* level 1: 3.2 GB/s on a PlayStation 2 anchor for 23x, against level 3's
		 * 2.6 GB/s for 25x - the writer has other bodies waiting */
		if (cs == nullptr || z->isError(z->initCStream(cs, 1))) failed = true;
	}
	~ZstdBodySink() { if (cs != nullptr) z->freeCStream(cs); }

	bool pump(ZstdApi::Buffer &in, int endOp)
	{
		for (;;)
		{
			ZstdApi::OutBuffer o{ out.data(), out.size(), 0 };
			const size_t rc = z->compressStream2(cs, &o, &in, endOp);
			if (z->isError(rc)) return false;
			if (o.pos != 0 && !writeAll(f, out.data(), o.pos)) return false;
			if (endOp == 0 ? in.pos == in.size : rc == 0) return true;
		}
	}
	bool write(const void *data, size_t n)
	{
		if (failed) return false;
		if (n == 0) return true;
		ZstdApi::Buffer in{ data, n, 0 };
		return pump(in, 0);
	}
	bool finish()
	{
		if (failed) return false;
		ZstdApi::Buffer in{ nullptr, 0, 0 };
		return pump(in, 2);
	}
};

} // namespace

bool StateHistory::writeSegmentBodyPacked(std::FILE *f, const Segment &seg, bool &packed)
{
	packed = false;
	const ZstdApi *z = spillRaw() ? nullptr : zstdApi(nullptr);
	if (z == nullptr || z->createCStream == nullptr || z->initCStream == nullptr
		|| z->freeCStream == nullptr || z->compressStream2 == nullptr)
	{
		return writeSegmentBody(f, seg);
	}
	ZstdBodySink sink(f, z);
	if (sink.failed) return writeSegmentBody(f, seg);   /* nothing written yet */
	if (!writeSegmentBodyTo([&sink](const void *d, size_t n) { return sink.write(d, n); }, seg)) return false;
	if (!sink.finish()) return false;
	packed = true;
	return true;
}

SpillBodyReader::~SpillBodyReader()
{
	if (m_stream != nullptr)
	{
		if (const ZstdApi *z = zstdApi(nullptr)) z->freeDStream(m_stream);
	}
}

bool SpillBodyReader::open(std::FILE *f, uint64_t at, uint64_t length, bool packed)
{
	m_f = f;
	m_at = at;
	m_length = length;
	m_packed = packed;
	m_pos = 0;
	if (f == nullptr) return false;
	if (!packed) return true;
	const ZstdApi *z = zstdApi(nullptr);
	if (z == nullptr) return false;
	m_stream = z->createDStream();
	if (m_stream == nullptr || z->isError(z->initDStream(m_stream))) return false;
	m_in.resize(256u << 10);
	m_out.resize(1u << 20);
	return true;
}

bool SpillBodyReader::refill()
{
	const ZstdApi *z = zstdApi(nullptr);
	if (z == nullptr) return false;
	for (;;)
	{
		if (m_inPos == m_inLen)
		{
			if (m_consumed >= m_length) return false;   /* the extent is exhausted */
			const uint64_t left = m_length - m_consumed;
			const size_t take = static_cast<size_t>(left < m_in.size() ? left : m_in.size());
			if (!seekTo(m_f, m_at + m_consumed) || !readAll(m_f, m_in.data(), take)) return false;
			m_consumed += take;
			m_inPos = 0;
			m_inLen = take;
		}
		ZstdApi::Buffer in{ m_in.data(), m_inLen, m_inPos };
		ZstdApi::OutBuffer out{ m_out.data(), m_out.size(), 0 };
		const size_t rc = z->decompressStream(m_stream, &out, &in);
		if (z->isError(rc)) return false;
		m_inPos = in.pos;
		m_outPos = 0;
		m_outLen = out.pos;
		if (m_outLen != 0) return true;
		if (rc == 0 && m_inPos == m_inLen && m_consumed >= m_length) return false;   /* the frame is over */
	}
}

bool SpillBodyReader::read(void *out, size_t n)
{
	if (n == 0) return true;
	if (!m_packed)
	{
		if (m_pos + n > m_length) return false;
		if (!seekTo(m_f, m_at + m_pos) || !readAll(m_f, out, n)) return false;
		m_pos += n;
		return true;
	}
	auto *dst = static_cast<uint8_t *>(out);
	while (n != 0)
	{
		if (m_outPos == m_outLen && !refill()) return false;
		size_t take = m_outLen - m_outPos;
		if (take > n) take = n;
		std::memcpy(dst, m_out.data() + m_outPos, take);
		m_outPos += take;
		dst += take;
		n -= take;
		m_pos += take;
	}
	return true;
}

bool SpillBodyReader::skip(uint64_t n)
{
	if (!m_packed)
	{
		if (m_pos + n > m_length) return false;
		m_pos += n;
		return true;
	}
	while (n != 0)
	{
		if (m_outPos == m_outLen && !refill()) return false;
		uint64_t take = m_outLen - m_outPos;
		if (take > n) take = n;
		m_outPos += static_cast<size_t>(take);
		n -= take;
		m_pos += take;
	}
	return true;
}

namespace
{

/* A read callback over a spilled body, for handing an anchor or a link
 * straight to the sandbox. */
struct ReaderSource
{
	SpillBodyReader *body;
	uint64_t left;
};

intptr_t readerRead(uintptr_t ud, void *out, uintptr_t len)
{
	auto *s = reinterpret_cast<ReaderSource *>(ud);
	if (len > s->left) len = static_cast<uintptr_t>(s->left);
	if (len == 0) return -1;
	if (!s->body->read(out, len)) return -1;
	s->left -= len;
	return static_cast<intptr_t>(len);
}

} // namespace

StateHistory::~StateHistory()
{
	/* the drainer is reading the machine's pages; it stops before anything else
	 * does, because what it is reading belongs to somebody who is also going */
	finishPlan();
	dropSpillFile();
}

/* Spill files a previous session left behind.
 *
 * The file is named for the process that made it, so a session that died -
 * a crash, a machine turned off - leaves its file in the project's cache with
 * nobody to remove it, and the next runs pile theirs on top. They are cache
 * files and the cache manager would take them in the end, but "in the end" is
 * after they have filled a disk, and they are gigabytes each.
 *
 * So a history sweeps the directory it is given. A file another LIVE session
 * is using refuses to go on Windows, which is the answer we want; on Linux the
 * unlink costs that session its name and nothing else - it holds the handle,
 * and its own removal simply finds nothing later. */
void StateHistory::sweepStaleSpills(const std::string &dir)
{
	if (dir.empty()) return;
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
	{
		if (ec) return;
		std::error_code one;
		if (!entry.is_regular_file(one) || one) continue;
		const std::string name = entry.path().filename().string();
		if (name.rfind("history-spill-", 0) != 0) continue;
		if (name == std::filesystem::path(m_spillPath).filename().string()) continue;   /* ours */
		std::filesystem::remove(entry.path(), one);
	}
}

void StateHistory::spillTo(const char *dir)
{
	const std::string next = dir != nullptr ? dir : "";
	if (next == m_spillDir)
	{
		sweepStaleSpills(next);
		return;
	}
	/* Whatever is out there belongs to the old directory, and the segments
	 * pointing at it are now unreadable - so they go, which costs replaying. */
	dropSpillFile();
	for (size_t i = m_segments.size(); i-- > 0; )
	{
		if (m_segments[i].spilled) forgetSegment(i);
	}
	m_spillDir = next;
	m_spillFailed = false;   /* a new directory is a fresh chance at it */
	sweepStaleSpills(next);
}

/* True when anything between the anchor and the last landing is pinned - the
 * question eviction asks before throwing a stretch away. */
static bool holdsPinned(const std::set<int64_t> &pins, int64_t from, int64_t to)
{
	const auto it = pins.lower_bound(from);
	return it != pins.end() && *it <= to;
}

/* Which stretch to give up, when one has to go.
 *
 * Two rules, and the first is not negotiable: the stretch holding the earliest
 * frames NEVER goes. Going back to a frame the greenzone no longer covers means
 * starting from the beginning and replaying to it, and that is only possible
 * while the beginning is still there. The disk budget used to drop the oldest
 * stretch in the file whichever it was - the first one included - and once it
 * had, nothing could reach the early movie at all: the piano roll would try,
 * find no state at or before the target, and seek forward forever. The newest
 * never goes either; it is where the work is.
 *
 * The second rule is what is left AFTER the budget has taken its share:
 * breadcrumbs. Dropping the oldest every time empties the far past first, so a
 * long session ends up with everything huddled around the playhead and a return
 * to the middle costs a replay from zero. Instead the stretch that goes is the
 * one whose absence widens the gap between its neighbours least - which, applied
 * repeatedly, thins the run evenly and leaves anchors spread across the whole
 * movie. Getting back to any frame then costs one anchor and a bounded replay.
 *
 * A stretch somebody pinned a frame in is not a candidate at all: a pin is a
 * promise that frame can still be reached. */
size_t StateHistory::chooseVictim(bool spilled) const
{
	size_t best = m_segments.size();
	int64_t bestGap = 0;
	for (size_t i = 1; i + 1 < m_segments.size(); i++)
	{
		const Segment &s = m_segments[i];
		if (s.spilled != spilled) continue;
		if (holdsPinned(m_pinned, s.anchorFrame, s.lastFrame())) continue;
		const int64_t gap = m_segments[i + 1].anchorFrame - m_segments[i - 1].anchorFrame;
		if (best == m_segments.size() || gap < bestGap) { best = i; bestGap = gap; }
	}
	return best;
}

/* The number given is what the FILE may weigh, because that is the number
 * somebody watching a disk fill up cares about and the only one they can check.
 *
 * What is kept reachable is half of it. A compaction copies every live byte, so
 * doing one per drop would copy the same bytes over and over; waiting until the
 * dead part is the bigger part makes it one copy per byte written, amortised,
 * and means the file sits between the live total and twice it. Half the number
 * for live is what turns that into a promise that can be read off `ls`. */
void StateHistory::diskBudget(uint64_t bytes)
{
	m_diskBudget = bytes / 2;
	evictDisk();
}

/* Drops the oldest stretches in the spill file until it is under its budget,
 * then reclaims the room they held.
 *
 * The newest is spared here as it is in memory - it is where the work is - and
 * a stretch somebody pinned a frame in is spared too, for the same reason
 * eviction spares it: a pin is a promise that frame can still be reached.
 */
void StateHistory::evictDisk()
{
	if (m_diskBudget == 0) return;
	for (;;)
	{
		bool dropped = false;
		while (m_spillLive > m_diskBudget)
		{
			const size_t victim = chooseVictim(true);
			if (victim == m_segments.size()) break;   /* nothing left that may go */
			forgetSegment(victim);
			dropped = true;
		}
		if (!dropped) return;
		/* A compaction waits for the writer first, and what that lets land
		 * counts against the budget too - so it is asked again rather than
		 * trusted to have been the end of it. Every turn drops a stretch, so
		 * this ends. */
		compactSpill();
		if (m_spillLive <= m_diskBudget) return;
	}
}

/* Moves what is still live to the front of the file, so the room the dropped
 * stretches held is actually given back. Only when the dead part is the bigger
 * part: a compaction copies every live byte, and doing it at every drop would
 * copy the same bytes over and over.
 *
 * A failure here is not an error. The file stays as it was and so do the
 * offsets in it; what is lost is the room, until the next drop asks again. */
bool StateHistory::compactSpill()
{
	if (m_spill == nullptr) return false;
	/* The cheap question first, and the wait only for an answer of yes.
	 *
	 * This is called after every drop and the answer is usually no - the dead
	 * part is not the bigger part yet - so draining here unconditionally made
	 * every spill wait for its own write within a frame or two of queuing it.
	 * That is the synchronous behaviour with a thread's overhead on top, and it
	 * measured exactly that way: a run that was 1.8x faster with the writer
	 * became 2.4x SLOWER as soon as the file had a budget. */
	/* Judge a SETTLED file.
	 *
	 * With writes in flight the counts describe a file that does not exist yet -
	 * ranges are reserved and not written, and a stretch dropped before its
	 * write lands is dead room that was never even filled. Deciding on those
	 * numbers made a pathological run compact at every opportunity instead of
	 * every third one, and a compaction copies every live byte.
	 *
	 * So a busy writer defers the question to a quieter frame. Not for ever: at
	 * three times the live set the file is worth compacting whatever is in
	 * flight, because the promise about its size is a promise. */
	{
		const uint64_t dead = m_fileBytes > m_spillLive ? m_fileBytes - m_spillLive : 0;
		if (m_writeQueued != 0 && dead < m_spillLive * 3) return false;
		if (m_spillLive != 0 && dead < m_spillLive) return false;
	}

	drainWriter();   /* it reads every live range, and then replaces the file */
	if (m_spill == nullptr) return false;
	bool anySpilled = false;
	for (const Segment &seg : m_segments) anySpilled = anySpilled || seg.spilled;
	if (m_spillLive == 0 && !anySpilled)
	{
		/* Nothing of it is wanted: the cheapest compaction there is. Reopened
		 * rather than rewound, because the writer appends where the file really
		 * ends, and a rewound file ends where it always did. */
		m_settle = Settling{};
		std::fclose(m_spill);
		m_spill = nullptr;
		if (m_spillRead != nullptr) { std::fclose(m_spillRead); m_spillRead = nullptr; }
		m_spill = std::fopen(m_spillPath.c_str(), "w+b");
		if (m_spill != nullptr) m_spillRead = std::fopen(m_spillPath.c_str(), "rb");
		m_spillBytes = 0;
		m_fileBytes = 0;
		m_writtenThrough = 0;
		return m_spill != nullptr && m_spillRead != nullptr;
	}
	/* the drain can have landed writes, which changes both counts */
	if (m_fileBytes <= m_spillLive || m_fileBytes - m_spillLive < m_spillLive) return false;   /* dead half is the smaller half */

	const std::string path = m_spillPath;
	const std::string tmp = path + ".compacting";
	std::FILE *out = std::fopen(tmp.c_str(), "w+b");
	if (out == nullptr) return false;

	/* What each stretch will be once this works - applied only if it does. The
	 * loop used to move each stretch's offset as it copied it, so a failure half
	 * way left the first half pointing into a file that was then thrown away. */
	StateHistory::Bytes buf;
	uint64_t at = 0, physAt = 0;
	std::vector<std::pair<uint64_t, uint64_t>> placed;   /* logical, physical */
	placed.reserve(m_segments.size());
	std::FILE *const in = m_spillRead != nullptr ? m_spillRead : m_spill;
	for (Segment &seg : m_segments)
	{
		if (!seg.spilled) continue;
		/* the compressed extent, as it is: a compaction never re-encodes */
		buf.resize(static_cast<size_t>(seg.physLength));
		if (!seekTo(in, seg.physAt) || !readAll(in, buf.data(), buf.size())
			|| !writeAll(out, buf.data(), buf.size()))
		{
			std::fclose(out);
			std::remove(tmp.c_str());
			return false;
		}
		placed.emplace_back(at, physAt);
		at += seg.spillLength;
		physAt += seg.physLength;
	}
	if (std::fflush(out) != 0)
	{
		std::fclose(out);
		std::remove(tmp.c_str());
		return false;
	}

	/* a settle in progress holds a reader on the handle about to close */
	m_settle = Settling{};
	std::fclose(m_spill);
	m_spill = nullptr;
	if (m_spillRead != nullptr) { std::fclose(m_spillRead); m_spillRead = nullptr; }
	if (std::rename(tmp.c_str(), path.c_str()) != 0)
	{
		/* the old file is still there and still right; reopen it and give up */
		std::fclose(out);
		std::remove(tmp.c_str());
		m_spill = std::fopen(path.c_str(), "r+b");
		m_spillRead = std::fopen(path.c_str(), "rb");
		return false;
	}
	std::fclose(out);
	m_spill = std::fopen(path.c_str(), "r+b");
	m_spillRead = std::fopen(path.c_str(), "rb");
	if (m_spill == nullptr || m_spillRead == nullptr) return false;
	{
		size_t k = 0;
		for (Segment &seg : m_segments)
		{
			if (!seg.spilled) continue;
			seg.spillAt = placed[k].first;
			seg.physAt = placed[k].second;
			k++;
		}
	}
	m_spillBytes = at;
	m_fileBytes = physAt;
	m_writtenThrough = at;   /* every live byte was just copied, here, in full */
	m_costs.compactions++;
	if (historyTrace())
	{
		fprintf(stderr, "[history] compacted the spill file to %llu bytes\n",
			(unsigned long long)m_spillBytes);
		fflush(stderr);
	}
	return true;
}

void StateHistory::dropSpillFile()
{
	/* Anything in flight is writing to the handle about to be closed. Finishing
	 * first costs a write nobody wants any more; closing first is a use after
	 * free on another thread. */
	drainWriter();
	/* whatever was being settled was being read from this file */
	m_settle = Settling{};
	if (historyTrace() && m_spill != nullptr)
	{
		/* what the file really weighs against what the history counted, and
		 * what the writer was handed against what it wrote - the whole of what
		 * compressing spilled bodies bought, in one line */
		uint64_t physical = 0;
		if (seekEnd(m_spill)) tellAt(m_spill, physical);
		/* no count of stretches: by the time a history lets its file go, clear()
		 * has usually emptied the list, and a count of nothing reads as a fact */
		fprintf(stderr, "[history] spill file at close: %llu bytes on disk for %llu logical,"
			" live weighing %llu; the writer was handed %llu and wrote %llu\n",
			(unsigned long long)physical, (unsigned long long)m_spillBytes,
			(unsigned long long)m_spillLive,
			(unsigned long long)m_costs.spilledRaw, (unsigned long long)m_costs.spilledPacked);
		fflush(stderr);
	}
	if (m_spillRead != nullptr)
	{
		std::fclose(m_spillRead);
		m_spillRead = nullptr;
	}
	if (m_spill != nullptr)
	{
		std::fclose(m_spill);
		m_spill = nullptr;
		std::remove(m_spillPath.c_str());
	}
	m_spillBytes = 0;
	m_spillLive = 0;
	m_fileBytes = 0;
	m_writtenThrough = 0;
}

void StateHistory::bands(int64_t nearFrames, int64_t midFrames, int64_t midStride,
	int64_t farStride, int64_t anchorSpacing)
{
	if (nearFrames > 0) m_nearFrames = nearFrames;
	if (midFrames > 0) m_midFrames = midFrames;
	if (midStride > 0) m_midStride = midStride;
	if (farStride > 0) m_farStride = farStride;
	if (anchorSpacing > 0) m_anchorSpacing = anchorSpacing;
	/* A band cannot be denser than the one nearer the playhead: the landings
	 * are a grid per band, and a coarser grid inside a finer one would keep
	 * asking for landings the band before it has already merged away. */
	if (m_farStride < m_midStride) m_farStride = m_midStride;
}

bool StateHistory::composeAvailable() const
{
	return m_host != nullptr && m_host->wbx_compose_delta != nullptr;
}

bool StateHistory::deltasAvailable() const
{
	return !deltasRefused() && m_host != nullptr && m_host->wbx_epoch_begin != nullptr
		&& m_host->wbx_save_delta != nullptr && m_host->wbx_load_delta != nullptr;
}

/* Links land on strictly ascending frames, so both of these are searches
 * rather than walks - a segment near the playhead is thousands of links long. */
int64_t StateHistory::Segment::nearestIn(int64_t f) const
{
	if (f < anchorFrame) return -1;
	if (f >= lastFrame()) return lastFrame();
	const auto it = std::upper_bound(links.begin(), links.end(), f,
		[](int64_t v, const Link &l) { return v < l.endFrame; });
	return it == links.begin() ? anchorFrame : (it - 1)->endFrame;
}

int64_t StateHistory::Segment::stepsTo(int64_t f) const
{
	if (f == anchorFrame) return 0;
	if (f < anchorFrame || f > lastFrame()) return -1;
	const auto it = std::lower_bound(links.begin(), links.end(), f,
		[](const Link &l, int64_t v) { return l.endFrame < v; });
	if (it == links.end() || it->endFrame != f) return -1;
	return static_cast<int64_t>(it - links.begin()) + 1;
}

int64_t StateHistory::count() const
{
	int64_t n = 0;
	for (const Segment &s : m_segments) n += 1 + static_cast<int64_t>(s.links.size());
	return n;
}

const uint8_t *StateHistory::noteFor(int64_t frame, size_t &lenOut) const
{
	lenOut = 0;
	for (const Segment &seg : m_segments)
	{
		if (seg.anchorFrame > frame) break;
		if (seg.anchorFrame == frame)
		{
			lenOut = seg.anchorNote.size();
			return seg.anchorNote.empty() ? nullptr : seg.anchorNote.data();
		}
		const int64_t steps = seg.stepsTo(frame);
		if (steps <= 0) continue;
		const Link &l = seg.links[static_cast<size_t>(steps) - 1];
		lenOut = l.note.size();
		return l.note.empty() ? nullptr : l.note.data();
	}
	return nullptr;
}

int64_t StateHistory::nearest(int64_t frame) const
{
	int64_t best = -1;
	for (const Segment &s : m_segments)
	{
		if (s.anchorFrame > frame) break;              /* ordered: nothing later helps */
		const int64_t here = s.nearestIn(frame);
		if (here > best) best = here;
	}
	return best;
}

void StateHistory::beforeAdvance()
{
	if (!enabled() || !deltasAvailable()) { m_epochOpen = false; return; }
	/* A delta continues the newest segment, and only while there is one with
	 * room. Otherwise the coming capture is an anchor and needs no epoch. */
	if (m_segments.empty()) { m_epochOpen = false; return; }
	const Segment &seg = m_segments.back();
	if (seg.lastFrame() - seg.anchorFrame >= m_anchorSpacing) { m_epochOpen = false; return; }
	/* An epoch left open by a frame the near band's stride skipped: it is
	 * measuring from the last landing and must go on doing so, or what those
	 * frames did is lost. Opening a new one here would forget it. */
	if (m_epochOpen) return;
	WbxReturn r{};
	m_host->wbx_epoch_begin(m_obj, &r);
	m_epochOpen = r.ok();
}

/* The near band's stride, from what capture is costing against what the run is.
 *
 * Moved gently and only every so often: a stride that chases one expensive
 * frame would thrash, and the thing being measured is noisy by nature - one
 * frame loads a level, the next draws a menu. */
/* An anchor is not a delta, and the stride is about deltas.
 *
 * Every capture used to go into one exponential mean, anchors included, and an
 * anchor costs what the MACHINE is where a delta costs what the frame did -
 * two orders of magnitude apart on anything heavy. Work it through with a
 * machine at 2 ms a capture on a 10 ms frame and one 121 ms anchor: the mean
 * goes to about 7.9 ms and the share to 0.49 against a ceiling of 0.15, so the
 * near band's stride trebles - and then recovers one step per thirty captures,
 * about ninety frames, by which time the next anchor is a sixth of the way
 * closer. A significant share of every heavy run was stored sparsely because of
 * a cost that had nothing to do with the frames being stored.
 *
 * Thinning the near band cannot make an anchor cheaper, either: anchors happen
 * on their own schedule (anchorSpacing), so the lever the tuner has does not
 * move the cost it was reacting to. So an anchor only resets the clock the next
 * delta measures against, and says what it cost under the trace. */
void StateHistory::noteAnchorCost()
{
	m_lastCaptureEnded = nowSeconds();
}

void StateHistory::tuneStride(double captureSeconds, double wallSeconds)
{
	if (wallSeconds <= 0 || captureSeconds < 0) return;
	const double a = 0.05;   /* the mean follows a couple of hundred frames */
	m_captureSeconds = m_captureSeconds == 0 ? captureSeconds : m_captureSeconds * (1 - a) + captureSeconds * a;
	m_wallSeconds = m_wallSeconds == 0 ? wallSeconds : m_wallSeconds * (1 - a) + wallSeconds * a;
	if (++m_capturesSinceTuned < 30) return;
	m_capturesSinceTuned = 0;
	if (m_wallSeconds <= 0) return;

	/* A capture that is quick in absolute terms is never worth thinning for,
	 * whatever share of the run it is. A machine that costs a millisecond a
	 * frame and a tenth of that to store is not a machine anybody is waiting
	 * for, and the near band's promise - every frame, where the work is - is
	 * worth more than the tenth. Only a capture measured in milliseconds can
	 * move the stride up. */
	static constexpr double kWorthThinning = 0.002;
	const double share = m_captureSeconds / m_wallSeconds;
	int64_t want = m_nearStride;
	if (share > kCostShare && m_captureSeconds > kWorthThinning)
	{
		want = static_cast<int64_t>(m_nearStride * (share / kCostShare) + 0.5);
	}
	else if ((share < kCostShare / 3 || m_captureSeconds <= kWorthThinning) && m_nearStride > 1)
	{
		want = m_nearStride - 1;
	}
	if (want < 1) want = 1;
	if (want > 32) want = 32;          /* past this the replay is the cost */
	if (want == m_nearStride) return;
	if (historyTrace())
	{
		fprintf(stderr, "[history] capture is %.0f%% of the run: the near band keeps"
			" one frame in %lld rather than one in %lld\n",
			share * 100, (long long)want, (long long)m_nearStride);
		fflush(stderr);
	}
	m_nearStride = want;
	/* what was measured describes the old stride */
	m_captureSeconds = 0;
	m_wallSeconds = 0;
}

/* A capture allocates - a whole machine for an anchor, a frame's churn for a
 * delta - and on a machine under pressure that allocation is where Chimera
 * meets the end of memory first, because the history is the biggest thing it
 * holds that it does not need.
 *
 * So running out is not fatal here: the budget halves, what that frees is given
 * back, and the capture is tried again. Repeatedly, down to a floor, because
 * one halving of a budget the machine cannot afford is unlikely to be enough.
 * A greenzone that has quietly become half as deep is a run that continues; the
 * alternative is a crash that loses the session.
 *
 * The budget is not written back to the settings. What the machine can spare
 * today is not a decision somebody made, and it should not silently become one.
 */
void StateHistory::capture(int64_t frame, const uint8_t *note, size_t noteLen)
{
	for (;;)
	{
		try
		{
			captureOnce(frame, note, noteLen);
			/* The disk budget is met on what has LANDED: a stretch costs the
			 * disk when the writer reports it, which can be any capture after
			 * it was spilled. So once a frame, whatever that frame did, what
			 * has been reported is held to the budget. Between here and the
			 * next frame the file may run ahead of it by what is still being
			 * written, which the writer's queue bounds. */
			applyWrites();
			evictDisk();
			return;
		}
		catch (const std::bad_alloc &)
		{
			/* Already as small as a history gets: the frame is simply not
			 * stored, which costs replaying to reach it and nothing else. */
			if (!halveBudget()) return;
		}
	}
}

/* Halves what the history may hold and gives back what that frees. False when
 * it is already at the floor - below which it could not hold one anchor, and
 * would be spending allocations to store nothing. */
bool StateHistory::halveBudget()
{
	if (m_budget <= kSmallestBudget) return false;
	const uint64_t was = m_budget;
	m_budget = m_budget / 2 < kSmallestBudget ? kSmallestBudget : m_budget / 2;
	fprintf(stderr, "[history] out of memory with %llu bytes held: the budget goes from"
		" %llu to %llu and the oldest of the run is given up\n",
		(unsigned long long)m_bytes, (unsigned long long)was, (unsigned long long)m_budget);
	fflush(stderr);
	evict();
	evictDisk();
	return true;
}

bool StateHistory::planAvailable() const
{
	return m_host != nullptr && m_host->wbx_state_size != nullptr
		&& m_host->wbx_state_plan != nullptr && m_host->wbx_state_fill != nullptr
		&& m_host->wbx_state_finish != nullptr && m_host->wbx_state_pages != nullptr
		&& m_drainer.threaded();
}

/* An anchor, taken as a plan: the sandbox holds the pages, the drainer copies
 * them, and this thread pays the walk and the holds.
 *
 * The segment is pushed with its buffer ALREADY the right size and already the
 * history's, because everything except the page bytes is written by the plan -
 * so the history's accounting, its bands and its budget see exactly what they
 * would have seen, at exactly the moment they would have seen it. What arrives
 * late is only the contents, and nothing may read those without finishPlan. */
bool StateHistory::captureAnchorPlanned(int64_t frame, std::vector<uint8_t> &carried)
{
	if (!planAvailable()) return false;
	const double t0 = historyTrace() ? nowSeconds() : 0.0;

	WbxReturn r{};
	m_host->wbx_state_size(m_obj, &r);
	if (!r.ok() || r.data <= 0) return false;
	const size_t size = static_cast<size_t>(r.data);

	Bytes bytes;
	try { bytes.resize(size); }
	catch (const std::bad_alloc &) { return false; }
	const double tAlloc = historyTrace() ? nowSeconds() : 0.0;

	m_host->wbx_state_plan(m_obj, bytes.data(), static_cast<uint64_t>(size), &r);
	if (!r.ok()) return false;
	/* The buffer is not zeroed (Bytes), so the state must fill it exactly: a
	 * plan that came out smaller than the size it was asked for would leave
	 * whatever the allocator handed us at the end of an anchor. It cannot
	 * happen - nothing runs between the two calls - and it is checked anyway,
	 * because the failure would be uninitialised memory inside a savestate. */
	if (static_cast<size_t>(r.data) != size)
	{
		WbxReturn fr{};
		m_host->wbx_state_finish(m_obj, &fr);
		return false;
	}
	const double tPlanned = historyTrace() ? nowSeconds() : 0.0;

	uint64_t pages = 0;
	m_host->wbx_state_pages(m_obj, &r);
	if (r.ok() && r.data > 0) pages = static_cast<uint64_t>(r.data);

	m_lastAnchorBytes = size;
	Segment seg;
	seg.anchorFrame = frame;
	seg.bytes = size;
	seg.anchor = Body::make(std::move(bytes));
	seg.anchorNote = std::move(carried);
	m_bytes += seg.bytes;
	m_segments.push_back(std::move(seg));

	m_planPending = true;
	m_planFrame = frame;
	if (pages != 0)
	{
		void *const obj = m_obj;
		const HostApi *const host = m_host;
		m_drainer.post([host, obj, pages]() {
			WbxReturn fr{};
			host->wbx_state_fill(obj, 0, pages, &fr);
		});
	}
	if (historyTrace())
	{
		fprintf(stderr, "[history] frame %lld: ANCHOR %llu bytes planned, %llu pages to fill,"
			" %.1f ms here (buffer %.1f, plan %.1f)\n",
			(long long)frame, (unsigned long long)size, (unsigned long long)pages,
			(nowSeconds() - t0) * 1000, (tAlloc - t0) * 1000, (tPlanned - tAlloc) * 1000);
		fflush(stderr);
	}
	return true;
}

void StateHistory::finishPlan()
{
	if (!m_planPending) return;
	/* the drainer first, always: it is reading the machine's pages, and
	 * everything after this may take them away */
	m_drainer.drain();
	m_planPending = false;
	m_planFrame = -1;
	if (m_host == nullptr || m_host->wbx_state_finish == nullptr) return;
	WbxReturn r{};
	m_host->wbx_state_finish(m_obj, &r);
	if (!r.ok() && historyTrace())
	{
		fprintf(stderr, "[history] the planned anchor would not finish: %s\n", r.errorMessage);
		fflush(stderr);
	}
}

void StateHistory::captureOnce(int64_t frame, const uint8_t *note, size_t noteLen)
{
	std::vector<uint8_t> carried(note, note + (note != nullptr ? noteLen : 0));
	if (!enabled()) return;
	/* what the writer has reported since the last frame, applied here, on the
	 * one thread that is allowed to change anything */
	applyWrites();
	/* and an anchor still being filled is finished before another is taken:
	 * one plan at a time, and its bytes have to be real before anything can
	 * spill or restore them */
	finishPlan();
	m_newest = frame;

	/* A capture describes the frame we now stand on. Anything at or after it is
	 * a timeline that no longer happens - which is what recording over an
	 * existing entry means - so it goes before this is stored. */
	invalidateAfter(frame - 1);

	Bytes bytes;
	ByteSink sink{ &bytes };
	WbxReturn r{};

	/* Never onto a spilled stretch. An edit far enough back truncates one that
	 * is on disk, and a delta pushed onto it would sit in memory while every
	 * restore reads the file - where the old timeline's links still are at
	 * those positions. The frames after the edit would come back as the
	 * frames before it. A fresh anchor starts a stretch of its own instead. */
	/* A frame the near band's stride skips is not stored at all: the epoch
	 * stays open and the next delta describes this frame along with it. The
	 * invalidation above has already happened, which is what matters - a
	 * timeline that no longer happens must go whether or not this frame is
	 * kept. */
	if (m_epochOpen && m_nearStride > 1 && !m_segments.empty()
		&& !m_segments.back().spilled
		&& m_segments.back().lastFrame() < frame
		&& frame - m_segments.back().lastFrame() < m_nearStride
		&& m_segments.back().lastFrame() - m_segments.back().anchorFrame < m_anchorSpacing)
	{
		m_newest = frame;
		return;
	}

	const double tCapture0 = nowSeconds();

	/* Room for the delta before it is written, not while.
	 *
	 * The sink appends each page as the sandbox hands it over, and a vector
	 * that grows by doubling copies everything it already holds each time it
	 * does - so a twenty megabyte delta was written once and copied about as
	 * much again, in pieces, on the way. The sandbox knows how many pages the
	 * frame touched before any of them are read, so the room is asked for
	 * once: an index and a page each, and a little for the header and the
	 * status list. Asking for slightly too much costs a moment's memory;
	 * asking for nothing cost a second copy of every delta. */
	if (m_epochOpen && m_host->wbx_get_epoch_page_count != nullptr)
	{
		WbxReturn pr{};
		m_host->wbx_get_epoch_page_count(m_obj, &pr);
		if (pr.ok())
		{
			const uint64_t pages = static_cast<uint64_t>(pr.data);
			if (pages > 0 && pages < (1ull << 32)) bytes.reserve(pages * (4096 + 8) + 4096);
		}
	}

	const bool wantDelta = m_epochOpen
		&& !m_segments.empty()
		&& !m_segments.back().spilled
		&& m_segments.back().lastFrame() < frame
		&& m_segments.back().lastFrame() - m_segments.back().anchorFrame < m_anchorSpacing;
	m_epochOpen = false;

	if (wantDelta)
	{
		m_host->wbx_save_delta(m_obj, true, sinkWrite, reinterpret_cast<uintptr_t>(&sink), &r);
		if (r.ok())
		{
			const uint64_t added = bytes.size();
			/* the push first, the arithmetic after: an allocation that throws
			 * between them leaves a count describing bytes nobody holds */
			m_segments.back().links.push_back(Link{ Body::make(std::move(bytes)), frame, std::move(carried) });
			m_segments.back().bytes += added;
			m_bytes += added;
			if (historyTrace())
			{
				fprintf(stderr, "[history] frame %lld: delta %llu bytes (segment %zu links, %llu total)\n",
					(long long)frame, (unsigned long long)added,
					m_segments.back().links.size(), (unsigned long long)m_bytes);
				fflush(stderr);
			}
			coarsen(frame);
			evict();
			const double now = nowSeconds();
			tuneStride(now - tCapture0, m_lastCaptureEnded > 0 ? now - m_lastCaptureEnded : 0);
			m_lastCaptureEnded = now;
			return;
		}
		bytes.clear();   /* fall through to a whole state rather than lose the frame */
	}

	/* The same for a whole machine: an anchor is the same size every time it is
	 * taken, so the one before it is the measure. */
	/* Planned if this host can: the pages are held and filled on the drainer,
	 * and what is paid here is the walk. Falls through to writing it in line
	 * when it cannot - an older host, no helper thread, or no room. */
	if (captureAnchorPlanned(frame, carried))
	{
		coarsen(frame);
		evict();
		evictDisk();
		/* An anchor does not feed the stride: see noteAnchorCost. */
		noteAnchorCost();
		return;
	}

	if (m_lastAnchorBytes != 0) bytes.reserve(m_lastAnchorBytes + (m_lastAnchorBytes >> 4));
	m_host->wbx_save_state(m_obj, sinkWrite, reinterpret_cast<uintptr_t>(&sink), &r);
	if (!r.ok()) return;   /* a missed capture only costs a longer replay later */
	m_lastAnchorBytes = bytes.size();
	const double anchorMs = historyTrace() ? (nowSeconds() - tCapture0) * 1000 : 0.0;
	Segment seg;
	seg.anchorFrame = frame;
	seg.bytes = bytes.size();
	seg.anchor = Body::make(std::move(bytes));
	seg.anchorNote = std::move(carried);
	m_bytes += seg.bytes;
	if (historyTrace())
	{
		fprintf(stderr, "[history] frame %lld: ANCHOR %llu bytes (%llu total), %.1f ms here%s\n",
			(long long)frame, (unsigned long long)seg.bytes, (unsigned long long)m_bytes, anchorMs,
			deltasAvailable() ? "" : " - this host has no epochs, every frame is an anchor");
		fflush(stderr);
	}
	m_segments.push_back(std::move(seg));
	coarsen(frame);
	evict();
	evictDisk();
	noteAnchorCost();
}

/* Drops one stretch and gives back whatever it was holding - memory, room in
 * the spill file, or neither. Every erase goes through this: the accounting bug
 * that wrapped the budget past zero was one erase that did its own arithmetic. */
void StateHistory::forgetSegment(size_t index)
{
	Segment &seg = m_segments[index];
	releaseBytes(seg.memoryBytes(), "forget");
	/* nobody will ever read this stretch again, so a write still waiting to
	 * happen should not happen */
	if (seg.writeWanted) seg.writeWanted->store(false);
	if (seg.spilled)
	{
		/* what it really weighed - nothing, if its write had not reported yet */
		if (seg.physLength > m_spillLive) m_spillLive = 0;
		else m_spillLive -= seg.physLength;
	}
	m_segments.erase(m_segments.begin() + static_cast<std::ptrdiff_t>(index));
}

void StateHistory::releaseBytes(uint64_t n, const char *where)
{
	if (n > m_bytes)
	{
		fprintf(stderr, "[history] %s gave back %llu bytes of the %llu held; "
			"the count was wrong before this\n", where,
			(unsigned long long)n, (unsigned long long)m_bytes);
		fflush(stderr);
		m_bytes = 0;
		return;
	}
	m_bytes -= n;
}

void StateHistory::invalidateAfter(int64_t frame)
{
	/* the stretch being filled may be one of the ones about to go */
	if (m_planPending && m_planFrame > frame) finishPlan();
	while (!m_segments.empty() && m_segments.back().anchorFrame > frame)
	{
		forgetSegment(m_segments.size() - 1);
	}
	if (m_segments.empty()) return;
	Segment &s = m_segments.back();
	while (s.lastFrame() > frame && !s.links.empty())
	{
		/* A spilled segment's links hold nothing here and its `bytes` describes
		 * the file, so there is nothing to give back and nothing to correct.
		 * What it can still answer shrinks, which is the point. */
		if (!s.spilled)
		{
			const uint64_t n = s.links.back().bytes.size();
			s.bytes -= n;
			releaseBytes(n, "invalidateAfter");
		}
		s.links.pop_back();
	}
}

/* ---- the bands ----
 *
 * A landing survives in a band if it sits on that band's grid - the multiples
 * of its stride. Everything else is composed into the landing after it, which
 * is a merge of two stored deltas and needs no machine.
 *
 * Only the landings that have just crossed a boundary are looked at, so this is
 * a couple of merges a frame rather than a sweep. The grid rule is what makes
 * that safe: it does not matter when a landing is examined or in what order,
 * because whether it survives depends only on where it lands.
 */
void StateHistory::pin(int64_t frame, bool isPinned)
{
	if (isPinned) m_pinned.insert(frame);
	else m_pinned.erase(frame);
}

bool StateHistory::pinned(int64_t frame) const
{
	return m_pinned.count(frame) != 0;
}

void StateHistory::unpinAll()
{
	m_pinned.clear();
}


void StateHistory::coarsen(int64_t newestFrame)
{
	if (!composeAvailable()) return;   /* an older host: keep every link */
	tidy(newestFrame - m_nearFrames, m_midStride);
	tidy(newestFrame - m_nearFrames - m_midFrames, m_farStride);
	settleSpilled(newestFrame - m_nearFrames - m_midFrames);
}

void StateHistory::tidy(int64_t frame, int64_t stride)
{
	if (stride <= 1 || frame <= 0) return;
	if (frame % stride == 0) return;   /* on the grid: this band wants it */
	if (pinned(frame)) return;         /* and somebody wants this one whatever the band says */

	for (Segment &seg : m_segments)
	{
		if (seg.spilled) continue;   /* its bytes are on disk and its band is settled */
		if (seg.lastFrame() < frame) continue;
		if (seg.anchorFrame >= frame) break;         /* ordered: nothing later holds it */
		const int64_t steps = seg.stepsTo(frame);
		if (steps <= 0) return;                      /* not a landing, or the anchor */
		const size_t i = static_cast<size_t>(steps) - 1;
		composeInto(seg, i);
		return;
	}
}

bool StateHistory::composePair(const Link &a, const Link &b, uint64_t anchorLen, Bytes &merged)
{
	/* A merge reads both links and writes their union, so it costs their
	 * combined size - and coarsening merges into a neighbour that KEEPS the
	 * span, so that neighbour accumulates and every later merge re-reads
	 * all of it. Collapsing four hundred landings that way cost four and a
	 * half seconds of pure composition on a machine whose frames overlap
	 * ninety per cent, and thirteen seconds at seventy; measured per frame,
	 * ten to thirty milliseconds spent reclaiming a few per cent.
	 *
	 * So a merge is capped at what fits in about a millisecond of memory
	 * bandwidth. It costs almost nothing: the merges it refuses are the
	 * handful of biggest ones, which are exactly the ones where the union
	 * is closest to the sum and least is reclaimed - a tenth of a per cent
	 * of the work buys back four to fourteen per cent of the memory.
	 * Leaving the landing in place only makes the band denser than asked,
	 * which is safe; the budget is what answers for the memory.
	 *
	 * The anchor is the outer bound on the same thought: a composed link
	 * that already costs what a whole machine costs is not worth composing
	 * further, because the band would be better served by the anchor it is
	 * walking from. */
	static constexpr uint64_t kMergeCap = 8u << 20;
	const uint64_t together = a.bytes.size() + b.bytes.size();
	if (together > kMergeCap) return false;
	if (anchorLen != 0 && together > anchorLen) return false;

	merged.clear();
	/* The merge of two sorted lists is at most both of them, and asking for
	 * that up front is one allocation instead of a dozen doublings with a
	 * copy each - on a delta of megabytes that is most of the write. */
	merged.reserve(a.bytes.size() + b.bytes.size());
	ByteSink sink{ &merged };
	WbxReturn r{};
	if (m_host->wbx_compose_delta_mem != nullptr)
	{
		/* Both are already contiguous here, so the host has no reason to
		 * copy them into buffers of its own to look at them. */
		m_host->wbx_compose_delta_mem(a.bytes.data(), a.bytes.size(), b.bytes.data(), b.bytes.size(),
			sinkWrite, reinterpret_cast<uintptr_t>(&sink), &r);
	}
	else
	{
		ByteSource sa{ a.bytes.data(), a.bytes.size(), 0 };
		ByteSource sb{ b.bytes.data(), b.bytes.size(), 0 };
		m_host->wbx_compose_delta(sourceRead, reinterpret_cast<uintptr_t>(&sa),
			sourceRead, reinterpret_cast<uintptr_t>(&sb),
			sinkWrite, reinterpret_cast<uintptr_t>(&sink), &r);
	}
	return r.ok();   /* a merge that will not happen costs memory, nothing else */
}

bool StateHistory::composeInto(Segment &seg, size_t i)
{
	/* composePair reads the anchor's length, and a merge reads link bodies */
	finishPlan();
	if (i + 1 >= seg.links.size()) return false;   /* the last link has nothing to merge into */
	Link &a = seg.links[i];
	Link &b = seg.links[i + 1];
	Bytes merged;
	if (!composePair(a, b, seg.anchor.size(), merged)) return false;

	const uint64_t was = a.bytes.size() + b.bytes.size();
	seg.bytes -= was;
	releaseBytes(was, "tidy");
	seg.bytes += merged.size();
	m_bytes += merged.size();
	if (historyTrace())
	{
		fprintf(stderr, "[history] merged the landing at %lld into %lld: %llu -> %zu bytes\n",
			(long long)a.endFrame, (long long)b.endFrame, (unsigned long long)was, merged.size());
		fflush(stderr);
	}
	b.bytes = Body::make(std::move(merged));
	seg.links.erase(seg.links.begin() + static_cast<std::ptrdiff_t>(i));
	return true;
}

/* ---- settling what was spilled too early ----
 *
 * The bands are kept by tidy(), which composes a landing into its neighbour as
 * the playhead moves away from it - and skips a spilled stretch, whose bytes
 * are on disk. So a stretch spilled out of the near or mid band, which a budget
 * smaller than those bands does every time, kept every frame's delta on disk
 * for good: six thousand Game Boy frames under a 64MB budget put 1567MB in the
 * file. Once the far boundary has passed such a stretch its links are read
 * back one at a time and composed down to the far grid - the same merge, under
 * the same caps - and what is left is appended to the file; what it was
 * becomes dead room and the compaction takes it back. A stretch already on the
 * far grid when it was spilled is marked settled then and never read.
 *
 * Streamed on purpose. A stretch spilled under a small budget is one that did
 * not fit in memory, so reading it back whole would be the very thing the
 * budget forbids; what is held is one accumulating link, one just read, and
 * the settled result, which is far-band sized.
 */
void StateHistory::settleSpilled(int64_t farFrame)
{
	/* a far stride of one keeps every landing, so there is nothing to settle */
	if (!composeAvailable() || m_spill == nullptr || m_farStride <= 1) return;
	/* This one runs inside a capture, so it must not simply wait for the writer
	 * - that would put the write back on the frame's critical path, which is
	 * the whole thing being removed. But it must not SKIP on a busy writer
	 * either: which stretch gets settled would then depend on when a write
	 * landed, and the history would hold different frames threaded than in
	 * line, which is exactly the difference this design promises not to have.
	 *
	 * So the choice is made from metadata, as it always was, and the file is
	 * only waited for when the stretch actually chosen is one the writer has
	 * not finished - which is the rare case of settling a stretch spilled a
	 * moment ago. In line, that wait is nothing at all. */
	applyWrites();
	std::FILE *const in = m_spillRead != nullptr ? m_spillRead : m_spill;
	Settling &st = m_settle;
	if (!st.active)
	{
		for (size_t i = 0; i + 1 < m_segments.size(); i++)
		{
			Segment &seg = m_segments[i];
			if (!seg.spilled || seg.settled || seg.lastFrame() >= farFrame) continue;
			if (seg.links.size() <= 1) { seg.settled = true; continue; }   /* nothing to compose */
			/* the head of the body: the anchor's length is the cap, the rest is stepped over */
			uint64_t noteLen = 0, count = 0;
			if (seg.spillAt + seg.spillLength > m_writtenThrough)
			{
				drainWriter();
				if (!seg.spilled) continue;   /* its write failed and it came back */
			}
			st = Settling{};
			auto reader = std::make_unique<SpillBodyReader>();
			if (!reader->open(in, seg.physAt, seg.physLength, seg.packed)
				|| !reader->readU64(st.anchorLen) || st.anchorLen > seg.spillLength
				|| !reader->skip(st.anchorLen) || !reader->readU64(noteLen) || noteLen > kMaxNote
				|| !reader->skip(noteLen) || !reader->readU64(count))
			{
				seg.settled = true;   /* unreadable: left as it is, and not asked again */
				continue;
			}
			st.reader = std::move(reader);
			st.active = true;
			st.anchorFrame = seg.anchorFrame;
			st.spillAt = seg.spillAt;
			st.spillLength = seg.spillLength;
			/* what the stretch still answers for may be less than the file
			 * holds - an edit truncated it - and the rest is not wanted back */
			st.linkCount = count < seg.links.size() ? static_cast<size_t>(count) : seg.links.size();
			break;
		}
		if (!st.active) return;
	}

	/* The stretch, as it is NOW: a compaction moves bodies and a drop removes
	 * them, so it is looked up every time and never kept. */
	const Segment *seg = settlingSegment();
	if (seg == nullptr)
	{
		m_settle = Settling{};   /* gone or moved meanwhile: nothing to finish */
		return;
	}
	/* the bytes about to be read have to BE there */
	if (seg->spillAt + seg->spillLength > m_writtenThrough)
	{
		drainWriter();
		seg = settlingSegment();
		if (seg == nullptr) { m_settle = Settling{}; return; }
	}

	/* a few links, then the rest next frame - reading one back costs what
	 * spilling it cost, and the far boundary moves one frame at a time */
	static constexpr int kLinksPerFrame = 4;
	for (int n = 0; n < kLinksPerFrame && st.linkIndex < st.linkCount; n++)
	{
		uint64_t endFrame = 0, noteLen = 0, len = 0;
		Link next;
		/* every read stays inside the body: a record that claims more is damage */
		SpillBodyReader &body = *st.reader;
		bool ok = body.pos() < seg->spillLength
			&& body.readU64(endFrame) && body.readU64(noteLen) && noteLen <= kMaxNote;
		if (ok)
		{
			next.note.resize(static_cast<size_t>(noteLen));
			ok = body.read(next.note.data(), next.note.size()) && body.readU64(len)
				&& len <= seg->spillLength - body.pos();
		}
		if (ok)
		{
			Bytes bytes(static_cast<size_t>(len));
			ok = body.read(bytes.data(), bytes.size());
			next.bytes = Body::make(std::move(bytes));
		}
		if (!ok)
		{
			/* unreadable: this stretch is left as it is, and not asked again */
			if (Segment *mine = settlingSegment()) mine->settled = true;
			m_settle = Settling{};
			return;
		}
		next.endFrame = static_cast<int64_t>(endFrame);
		st.linkIndex++;

		if (!st.hasAcc)
		{
			st.acc = std::move(next);
			st.hasAcc = true;
			continue;
		}
		Bytes merged;
		const bool keep = st.acc.endFrame % m_farStride == 0 || pinned(st.acc.endFrame)
			|| !composePair(st.acc, next, st.anchorLen, merged);
		if (keep)
		{
			/* the grid, a pin, or the caps: this landing stays */
			st.out.push_back(std::move(st.acc));
			st.acc = std::move(next);
			continue;
		}
		st.acc.bytes = Body::make(std::move(merged));
		st.acc.endFrame = next.endFrame;
		st.acc.note = std::move(next.note);
		st.merges++;
	}
	if (st.linkIndex < st.linkCount) return;
	if (st.hasAcc) st.out.push_back(std::move(st.acc));
	finishSettling();
}

StateHistory::Segment *StateHistory::settlingSegment()
{
	for (Segment &s : m_segments)
	{
		if (s.anchorFrame != m_settle.anchorFrame || !s.spilled) continue;
		/* the same stretch, in the same place, the same length: anything else
		 * with this anchor frame is another timeline's */
		if (s.spillAt != m_settle.spillAt || s.spillLength != m_settle.spillLength) return nullptr;
		return &s;
	}
	return nullptr;
}

void StateHistory::finishSettling()
{
	/* The old body is read back for its anchor, so it has to be in the file -
	 * which it nearly always is; the wait is only for one spilled a moment ago.
	 * This used to drain the writer unconditionally and then write the settled
	 * body HERE, a whole machine written on the emulation thread once per
	 * settled stretch. It is reserved here and written on the writer now, the
	 * way a spill is. */
	Segment *seg = settlingSegment();
	if (seg != nullptr && seg->spillAt + seg->spillLength > m_writtenThrough)
	{
		drainWriter();
		seg = settlingSegment();
	}
	Settling st = std::move(m_settle);
	m_settle = Settling{};
	/* gone meanwhile - dropped, moved, re-recorded over - or nothing was
	 * merged: either way the file is right as it is */
	if (seg == nullptr) return;
	seg->settled = true;
	if (st.merges == 0) return;

	/* an edit may have shortened the stretch while this worked: keep only what
	 * it still answers for. Composition never moves a landing, so the settled
	 * frames are a subset of the ones it had. */
	const int64_t last = seg->lastFrame();
	while (!st.out.empty() && st.out.back().endFrame > last) st.out.pop_back();

	/* the anchor comes across from the old body; it is one machine, which is
	 * what capturing it held in memory in the first place */
	Segment work;
	work.anchorFrame = seg->anchorFrame;
	work.anchorNote = seg->anchorNote;
	{
		std::FILE *const in = m_spillRead != nullptr ? m_spillRead : m_spill;
		SpillBodyReader body;
		uint64_t anchorLen = 0;
		Bytes anchorBody(static_cast<size_t>(st.anchorLen));
		if (!body.open(in, seg->physAt, seg->physLength, seg->packed) || !body.readU64(anchorLen)
			|| anchorLen != st.anchorLen || !body.read(anchorBody.data(), anchorBody.size()))
		{
			return;
		}
		work.anchor = Body::make(std::move(anchorBody));
	}
	work.bytes = st.anchorLen;
	for (Link &l : st.out) work.bytes += l.bytes.size();
	work.links = std::move(st.out);

	const uint64_t at = m_spillBytes;
	const uint64_t length = segmentBodyLength(work);
	const uint64_t was = seg->spillLength;

	PendingWrite job;
	job.at = at;
	job.length = length;
	job.anchorFrame = seg->anchorFrame;
	job.wanted = std::make_shared<std::atomic<bool>>(true);
	seg->writeWanted = job.wanted;
	job.body.anchorFrame = work.anchorFrame;
	job.body.anchor = work.anchor;
	job.body.anchorNote = work.anchorNote;
	job.body.links = work.links;

	m_spillBytes = at + length;
	m_writeQueued += length;
	/* the old body stops costing the disk now; the settled one costs it when
	 * the writer says it landed */
	m_spillLive = seg->physLength > m_spillLive ? 0 : m_spillLive - seg->physLength;
	seg->spillAt = at;
	seg->spillLength = length;
	seg->physAt = 0;
	seg->physLength = 0;
	seg->packed = false;
	seg->bytes = work.bytes;
	/* the landings it now has: metadata only, as a spilled stretch keeps them */
	seg->links.clear();
	for (Link &l : work.links)
	{
		seg->links.push_back(Link{ Body{}, l.endFrame, l.note });
	}
	postWrite(std::move(job));

	if (historyTrace())
	{
		fprintf(stderr, "[history] settled frames %lld-%lld on disk: %llu -> %llu bytes, %d merges"
			" (%llu live, file %llu)\n",
			(long long)seg->anchorFrame, (long long)last, (unsigned long long)was,
			(unsigned long long)seg->spillLength, st.merges,
			(unsigned long long)m_spillLive, (unsigned long long)m_spillBytes);
		fflush(stderr);
	}
	/* the old body is dead room now; take it back when it is the bigger half */
	if (m_fileBytes > m_spillLive && m_fileBytes - m_spillLive >= m_spillLive) compactSpill();
}

/* ---- spilling ----
 *
 * One file, appended to. A segment that is dropped or re-recorded over leaves
 * its space behind unreclaimed, which is the right trade for a cache: the file
 * is thrown away wholesale when the history is, and the alternative is
 * bookkeeping that buys nothing a user would notice.
 */
/* What a body of this shape weighs in the file.
 *
 * It must agree with writeSegmentBody to the byte, because this is what
 * reserves the range the write will land in - and the next reservation starts
 * where this one ends. test_state_history pins the two together by writing a
 * body and comparing. */
uint64_t StateHistory::segmentBodyLength(const Segment &seg)
{
	uint64_t n = sizeof(uint64_t) + seg.anchor.size()
		+ sizeof(uint64_t) + seg.anchorNote.size()
		+ sizeof(uint64_t);
	for (const Link &l : seg.links)
	{
		n += sizeof(uint64_t) * 3 + l.note.size() + l.bytes.size();
	}
	return n;
}

/* Finishes every write in flight, then applies what they said.
 *
 * Everything that READS the spill file calls this first. The writer owns that
 * handle while it has work, so the rule is not about the bytes - the ranges
 * never overlap - but about the FILE itself, whose position and buffer are one
 * object with one owner. */
void StateHistory::drainWriter()
{
	if (m_writer.pending() != 0)
	{
		const double t0 = nowSeconds();
		m_costs.waits++;
		m_writer.drain();
		m_costs.waitSeconds += nowSeconds() - t0;
	}
	applyWrites();
}

/* What the writer reported, applied HERE, on the thread that owns the history.
 *
 * A write that succeeded needs nothing done: its metadata was settled when it
 * was queued. A write that FAILED is undone - the stretch gets its bytes back
 * and stops being spilled - which leaves the history exactly where a
 * synchronous spill returning false left it, and lets evict() fall through to
 * thinning as it always did. */
void StateHistory::applyWrites()
{
	std::vector<PendingWrite> done;
	{
		std::lock_guard<std::mutex> lock(m_writtenLock);
		done.swap(m_written);
	}
	for (PendingWrite &w : done)
	{
		m_writeQueued = w.length > m_writeQueued ? 0 : m_writeQueued - w.length;
		/* Ranges are reserved in order and written in order, so the end of the
		 * one just reported is the point up to which the file is whole. A
		 * failed write leaves dead room rather than a hole, so it moves this
		 * along too. */
		if (!w.skipped && w.at + w.length > m_writtenThrough) m_writtenThrough = w.at + w.length;
		for (Segment &s : m_segments)
		{
			if (s.writeWanted != w.wanted) continue;
			s.writeWanted.reset();
			/* where the bytes really are: nothing but a reader looks at these */
			if (w.ok && !w.skipped)
			{
				s.physAt = w.physAt;
				s.physLength = w.physLength;
				s.packed = w.packed;
				/* The disk budget counts what the file really holds (user-decided,
				 * 2026-09-13), so this is the moment a stretch starts to cost it -
				 * and only a stretch still here: one dropped before its report
				 * never cost anything. */
				m_spillLive += w.physLength;
			}
			break;
		}
		if (w.ok && !w.skipped)
		{
			m_costs.spilledRaw += w.length;
			m_costs.spilledPacked += w.physLength;
			/* the bytes are in the file whether or not their stretch still is */
			if (w.physAt + w.physLength > m_fileBytes) m_fileBytes = w.physAt + w.physLength;
		}
		if (w.ok) continue;

		for (Segment &s : m_segments)
		{
			/* the same stretch, in the same place: anything else with this
			 * anchor frame belongs to a timeline an edit has since ended */
			if (s.anchorFrame != w.anchorFrame || !s.spilled) continue;
			if (s.spillAt != w.at || s.spillLength != w.length) continue;
			s.spilled = false;
			s.anchor = w.body.anchor;
			/* an edit may have shortened it meanwhile; give back only the
			 * bodies of the links it still answers for */
			for (size_t i = 0; i < s.links.size() && i < w.body.links.size(); i++)
			{
				s.links[i].bytes = w.body.links[i].bytes;
			}
			s.bytes = s.anchor.size();
			for (const Link &l : s.links) s.bytes += l.bytes.size();
			m_bytes += s.bytes;
			break;
		}
		if (!m_spillFailed)
		{
			m_spillFailed = true;
			fprintf(stderr, "[history] a spill to %s did not land - the far band will be dropped"
				" instead (the disk is full, or the directory has gone)\n", m_spillDir.c_str());
			fflush(stderr);
		}
	}
}

/* The writer's half of a spill or a settle: compress the body and append it
 * where the file really ends, then say where that was.
 *
 * Appended rather than written at the reserved offset, because a compressed
 * body is a fraction of the range reserved for it: written in place it would
 * leave a hole the size of the difference after every one, which NTFS fills
 * with zeros. The reservation stays what every decision is made from; this is
 * only where the bytes go. */
/* CHIMERA_SPILL_COLD=1: after each body lands, put it on the disk for real and
 * ask the kernel to forget its cached pages, so the next read of it is a read
 * of a disk rather than a memcpy of the page cache.
 *
 * For measuring, and only that. A stretch spilled a moment ago otherwise sits
 * in the page cache and reads back at memory speed, which hides exactly the
 * cost that compressing bodies exists to remove - and there is no dropping the
 * cache system-wide without root. Evicting one's own file needs no privilege.
 * Linux only; elsewhere it does nothing. */
static void forgetCachedPages(std::FILE *f)
{
	static const int cold = [] {
		const char *e = getenv("CHIMERA_SPILL_COLD");
		return e != nullptr && e[0] != '\0' && e[0] != '0' ? 1 : 0;
	}();
	if (cold == 0) return;
#ifndef _WIN32
	const int fd = fileno(f);
	fdatasync(fd);
	posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
#else
	(void)f;
#endif
}

void StateHistory::postWrite(PendingWrite &&job)
{
	std::FILE *const f = m_spill;
	m_writer.post([this, f, job = std::move(job)]() mutable {
		if (job.wanted->load())
		{
			uint64_t start = 0, end = 0;
			job.ok = seekEnd(f) && tellAt(f, start)
				&& writeSegmentBodyPacked(f, job.body, job.packed)
				&& std::fflush(f) == 0 && tellAt(f, end);
			job.physAt = start;
			job.physLength = job.ok ? end - start : 0;
			if (job.ok) forgetCachedPages(f);
		}
		else
		{
			/* dropped while it waited: those bytes will never be read, so the
			 * cheapest correct thing is not to write them */
			job.skipped = true;
			job.ok = true;
		}
		/* the bodies go here, on the writer, rather than on the thread that
		 * runs the machine */
		job.body = Segment{};
		std::lock_guard<std::mutex> lock(m_writtenLock);
		m_written.push_back(std::move(job));
	});
}

uint64_t StateHistory::writeQueueCap() const
{
	uint64_t cap = m_budget / 2;
	if (cap < (4ull << 20)) cap = 4ull << 20;
	if (cap > (64ull << 20)) cap = 64ull << 20;
	return cap;
}

bool StateHistory::spill(Segment &seg)
{
	if (seg.spilled || m_spillDir.empty()) return false;
	/* the bytes are about to go to a file, so they have to be there */
	finishPlan();

	/* Let the writer catch up before handing it more than it can hold. See
	 * writeQueueCap: a queue nobody bounds is memory the budget cannot see and
	 * a file full of ranges that were dropped before they were written. */
	if (m_writeQueued > writeQueueCap()) drainWriter();

	if (m_spill == nullptr)
	{
		m_spillPath = m_spillDir + "/" + spillFileName();
		m_spill = std::fopen(m_spillPath.c_str(), "w+b");
		if (m_spill == nullptr) return false;
		/* the loop's own view of the same file: its own position, so a read
		 * here and a write there cannot move each other (see m_spillRead) */
		m_spillRead = std::fopen(m_spillPath.c_str(), "rb");
		if (m_spillRead == nullptr) { std::fclose(m_spill); m_spill = nullptr; return false; }
		m_spillBytes = 0;
	}

	/* The range is reserved here, and the bytes go to the writer. Everything
	 * the history knows about this stretch is therefore true immediately - the
	 * budget's arithmetic is what it always was, and so are the decisions that
	 * follow it - and the only thing that happens later is the write. */
	const uint64_t at = m_spillBytes;
	const uint64_t length = segmentBodyLength(seg);

	PendingWrite job;
	job.at = at;
	job.length = length;
	job.anchorFrame = seg.anchorFrame;
	job.wanted = std::make_shared<std::atomic<bool>>(true);
	seg.writeWanted = job.wanted;
	job.body.anchorFrame = seg.anchorFrame;
	job.body.anchor = seg.anchor;          /* shared: a pointer, not a machine */
	job.body.anchorNote = seg.anchorNote;
	job.body.links = seg.links;            /* shared bodies, copied notes */

	m_spillBytes = at + length;
	/* Before the flag, not after: once it is set the segment costs no memory by
	 * definition, and taking its bytes off the count afterwards takes nothing. */
	releaseBytes(seg.memoryBytes(), "spill");
	seg.spilled = true;
	/* on the far grid already if the far boundary has passed it - tidy() did
	 * that as it went - and then settleSpilled() has nothing to read back */
	seg.settled = m_newest >= 0 && seg.lastFrame() < m_newest - m_nearFrames - m_midFrames;
	seg.spillAt = at;
	seg.spillLength = length;
	/* Not counted against the disk yet: what it will weigh there is decided by
	 * the compressor, and it is counted when the writer says it landed. */
	m_writeQueued += length;

	/* What it held is now the file's; give the memory back for real. The notes
	 * stay: they are metadata, like the landings, and answering what was stored
	 * with a frame must not touch a disk. */
	seg.anchor.reset();
	for (Link &l : seg.links)
	{
		l.bytes.reset();
	}

	postWrite(std::move(job));

	if (historyTrace())
	{
		fprintf(stderr, "[history] spilled frames %lld-%lld: %llu bytes to disk"
			" (%llu in memory, %llu on disk, file %llu)\n",
			(long long)seg.anchorFrame, (long long)seg.lastFrame(),
			(unsigned long long)seg.bytes, (unsigned long long)m_bytes,
			(unsigned long long)m_spillLive, (unsigned long long)m_spillBytes);
		fflush(stderr);
	}
	return true;
}

bool StateHistory::restoreSpilled(const Segment &seg, int64_t steps, std::string &error)
{
	/* The writer owns that handle while it has work, so a read waits - but only
	 * for a stretch it has not finished. The barrier the design calls "help
	 * finish" costs what the write costs, which is what the synchronous path
	 * paid at the moment of spilling; paying it for a stretch already on disk
	 * would be paying it for nothing. */
	if (seg.spillAt + seg.spillLength > m_writtenThrough) drainWriter();
	std::FILE *const in = m_spillRead != nullptr ? m_spillRead : m_spill;
	SpillBodyReader body;
	uint64_t anchorLen = 0, linkCount = 0, noteLen = 0;
	if (in == nullptr || !body.open(in, seg.physAt, seg.physLength, seg.packed) || !body.readU64(anchorLen))
	{
		error = "the spilled state history could not be read";
		return false;
	}

	WbxReturn r{};
	ReaderSource anchor{ &body, anchorLen };
	m_host->wbx_load_state(m_obj, readerRead, reinterpret_cast<uintptr_t>(&anchor), &r);
	if (!r.ok()) { error = r.errorMessage; return false; }
	/* the sandbox may stop reading before the end - skip whatever it left, and
	 * the anchor's note, which is already in memory */
	if (!body.skip(anchor.left) || !body.readU64(noteLen) || !body.skip(noteLen) || !body.readU64(linkCount))
	{
		error = "the spilled state history could not be read";
		return false;
	}
	for (int64_t i = 0; i < steps; i++)
	{
		uint64_t endFrame = 0, len = 0;
		if (!body.readU64(endFrame) || !body.readU64(noteLen) || !body.skip(noteLen) || !body.readU64(len))
		{
			error = "the spilled state history could not be read";
			return false;
		}
		ReaderSource link{ &body, len };
		m_host->wbx_load_delta(m_obj, readerRead, reinterpret_cast<uintptr_t>(&link), &r);
		if (!r.ok()) { error = r.errorMessage; return false; }
		if (!body.skip(link.left))
		{
			error = "the spilled state history could not be read";
			return false;
		}
	}
	return true;
}

/* Under budget pressure the history thins from the FAR end of the oldest
 * segment: dropping a trailing delta costs precision back there and orphans
 * nothing, because nothing chains through the end of a chain. The first
 * segment's anchor is never dropped - it is what keeps every frame reachable
 * at all - and neither is the newest segment's, which is where the work is. */
void StateHistory::evict()
{
	while (m_bytes > m_budget)
	{
		/* First choice: put the oldest stretch on disk, oldest to newest. It
		 * costs reading it back rather than replaying to it, and the far end of
		 * the history is where that trade is obviously right. The newest is
		 * never spilled - it is where the work is. */
		bool moved = false;
		for (size_t i = 0; i + 1 < m_segments.size(); i++)
		{
			if (m_segments[i].spilled) continue;
			if (spill(m_segments[i]))
			{
				/* right here, not after the whole memory pass: the file is over
				 * its budget from the moment the write lands, and a limit that
				 * is only true once the loop finishes is a limit somebody
				 * watching a disk fill up cannot read off `ls` */
				evictDisk();
				moved = true;
				break;
			}
			/* Asked to spill, and could not - a full disk, near enough always.
			 * The thinning below carries on, so this costs frames rather than
			 * the session, but it is not something to keep to ourselves. */
			if (!m_spillDir.empty() && !m_spillFailed)
			{
				m_spillFailed = true;
				fprintf(stderr, "[history] could not spill to %s - the far band will be dropped instead"
					" (the disk is full, or the directory has gone)\n", m_spillDir.c_str());
				fflush(stderr);
			}
			break;   /* nowhere to spill: everything after this fails the same way */
		}
		if (moved) continue;

		/* Thin the oldest stretch that still holds anything, and NEVER the
		 * newest - it is where the playhead is, and its last link is the frame
		 * that was captured a moment ago.
		 *
		 * Taking it was a loop: capture a delta, evict it again because the
		 * budget was already unmeetable, then have nothing to chain to next
		 * frame and write a whole anchor instead, spill that, and go round. Six
		 * thousand Game Boy frames under a budget too small for them made two
		 * thousand seven hundred anchors and five gigabytes of spill file. The
		 * drop-a-whole-stretch path below has always spared the newest; this one
		 * did not, and it is the one that runs first. */
		Segment *victim = nullptr;
		for (size_t i = 0; i + 1 < m_segments.size(); i++)
		{
			Segment &s = m_segments[i];
			if (s.links.empty() || s.spilled) continue;
			if (pinned(s.links.back().endFrame)) continue;   /* somebody wants that one */
			victim = &s;
			break;
		}
		if (victim != nullptr)
		{
			uint64_t n = victim->links.back().bytes.size();
			victim->bytes -= n;
			releaseBytes(n, "evict");
			victim->links.pop_back();
			continue;
		}
		/* Nothing left to thin: give a whole stretch up, chosen to keep what
		 * remains spread over the run (chooseVictim), and stop when there is
		 * nothing that may go. A spilled segment costs nothing in memory, so
		 * dropping one would not help - and a stretch somebody pinned a frame in
		 * is spilled, never dropped: if it could not be spilled it stays, and
		 * the budget is missed rather than the promise. */
		const size_t drop = chooseVictim(false);
		if (drop == m_segments.size()) return;
		forgetSegment(drop);
	}
}

/* A restore that fails part way is the worst thing this file can do quietly.
 *
 * The chain is an anchor and the deltas after it, applied to the live machine.
 * Fail on the fourth of thirty and the machine is not frame N, and it is not
 * the frame it was on before either - it is a machine that never existed, and
 * the session carries on with it: frames are captured from it, a movie is
 * recorded against it, and the desync surfaces somewhere else entirely. The
 * old code returned false and left it exactly there.
 *
 * So a failure is CONTAINED. The anchor is loaded again - it was read once
 * already, so this is the one step most likely to work - and the machine is
 * then a machine that did exist, at the anchor's frame, which `landedOn`
 * reports so the caller's idea of where it is can follow. The stretch that
 * failed is dropped, because whatever is wrong with it will be wrong the next
 * time somebody walks it; what that costs is replaying, which is what the
 * history is allowed to cost. If even the anchor will not load, nothing here
 * can help and `landedOn` stays -1: the caller must reload the machine.
 */
bool StateHistory::restoreFailed(const Segment *seg, std::string &error, int64_t *landedOn)
{
	const int64_t anchorFrame = seg->anchorFrame;
	bool consistent = false;
	if (!seg->spilled)
	{
		WbxReturn r{};
		ByteSource anchor{ seg->anchor.data(), seg->anchor.size(), 0 };
		m_host->wbx_load_state(m_obj, sourceRead, reinterpret_cast<uintptr_t>(&anchor), &r);
		consistent = r.ok();
	}
	else
	{
		std::string ignored;
		consistent = restoreSpilled(*seg, 0, ignored);
	}
	fprintf(stderr, "[history] the stored frames from %lld could not be walked (%s); "
		"the machine is %s and those frames are given up\n",
		(long long)anchorFrame, error.c_str(),
		consistent ? "back on that anchor" : "NOT to be trusted - reload it");
	fflush(stderr);
	for (size_t i = 0; i < m_segments.size(); i++)
	{
		if (m_segments[i].anchorFrame == anchorFrame) { forgetSegment(i); break; }
	}
	if (landedOn != nullptr) *landedOn = consistent ? anchorFrame : -1;
	m_epochOpen = false;
	return false;
}

bool StateHistory::restore(int64_t frame, std::string &error, int64_t *landedOn)
{
	/* a half-filled anchor is not a state; and a load replaces the machine the
	 * plan is holding pages of */
	finishPlan();
	if (landedOn != nullptr) *landedOn = -1;
	const Segment *seg = nullptr;
	int64_t steps = -1;
	for (const Segment &s : m_segments)
	{
		const int64_t n = s.stepsTo(frame);
		if (n >= 0) { seg = &s; steps = n; break; }
	}
	if (seg == nullptr)
	{
		error = "no stored state at that frame";
		return false;   /* nothing was touched: the machine is where it was */
	}

	const double t0 = historyTrace() ? nowSeconds() : 0.0;
	if (seg->spilled)
	{
		if (!restoreSpilled(*seg, steps, error)) return restoreFailed(seg, error, landedOn);
		if (historyTrace())
		{
			fprintf(stderr, "[history] restore %lld: from disk, anchor %lld + %lld deltas, %.0f ms\n",
				(long long)frame, (long long)seg->anchorFrame, (long long)steps,
				(nowSeconds() - t0) * 1000);
		}
		m_epochOpen = false;
		return true;
	}

	WbxReturn r{};
	ByteSource anchor{ seg->anchor.data(), seg->anchor.size(), 0 };
	m_host->wbx_load_state(m_obj, sourceRead, reinterpret_cast<uintptr_t>(&anchor), &r);
	if (!r.ok())
	{
		/* the anchor itself: nothing was applied on top of it, so the machine is
		 * whatever the failed load left - which only the caller can repair */
		error = r.errorMessage;
		return restoreFailed(seg, error, landedOn);
	}
	const double t1 = historyTrace() ? nowSeconds() : 0.0;
	for (int64_t i = 0; i < steps; i++)
	{
		const Body &d = seg->links[static_cast<size_t>(i)].bytes;
		ByteSource src{ d.data(), d.size(), 0 };
		m_host->wbx_load_delta(m_obj, sourceRead, reinterpret_cast<uintptr_t>(&src), &r);
		if (!r.ok())
		{
			error = r.errorMessage;
			return restoreFailed(seg, error, landedOn);
		}
	}
	if (historyTrace())
	{
		const int64_t chain = steps;
		const double t2 = nowSeconds();
		fprintf(stderr,
			"[history] restore %lld: anchor %lld (%.1f MB) %.0f ms + %lld deltas %.0f ms = %.0f ms\n",
			(long long)frame, (long long)seg->anchorFrame, seg->anchor.size() / 1048576.0,
			(t1 - t0) * 1000, (long long)chain, (t2 - t1) * 1000, (t2 - t0) * 1000);
	}
	/* whatever epoch was marked described the machine we have just left */
	m_epochOpen = false;
	return true;
}

/* ---- persistence ----
 *
 * One file, written and read a segment at a time. Nothing here assembles the
 * history in memory: the old greenzone did, through a managed array that stops
 * near 2GB, and a long run's history therefore failed to save and said nothing.
 */
namespace
{

const char kMagic[] = "ChimeraHistory3";

/* Versions this build can no longer read. A history from one of these is not
 * damage and not the user's doing: it is a cache written by an older build, and
 * the contract for losing a cache is that it costs recomputation and never
 * work. So it is treated exactly as a history of another machine is - dropped,
 * quietly, and rebuilt by playing. 1 had a stride of one frame per link, from
 * before links could span more than a frame; 2 had no room for the caller's
 * note. */
const char *const kSuperseded[] = { "ChimeraHistory1", "ChimeraHistory2" };

/* A note is a frontend's lag flag and its counters - a few bytes, ridden along
 * because keeping them in a table of the caller's own would mean mirroring
 * every eviction this class does. It is not a place to keep things, and a file
 * claiming otherwise is damaged. */

} // namespace

/* `n` bytes of a spilled body into a file, through a small buffer. A saved
 * history is always the raw layout, whatever the spill file holds. */
static bool copyFromReader(SpillBodyReader &in, std::FILE *out, uint64_t n)
{
	std::vector<uint8_t> chunk(256 * 1024);
	while (n != 0)
	{
		const size_t take = static_cast<size_t>(n < chunk.size() ? n : chunk.size());
		if (!in.read(chunk.data(), take) || !writeAll(out, chunk.data(), take)) return false;
		n -= take;
	}
	return true;
}

bool StateHistory::copySpilledBody(std::FILE *spill, std::FILE *out, const Segment &seg)
{
	SpillBodyReader body;
	if (!body.open(spill, seg.physAt, seg.physLength, seg.packed)) return false;
	uint64_t anchorLen = 0, noteLen = 0, count = 0;
	if (!body.readU64(anchorLen) || anchorLen > seg.spillLength) return false;
	if (!writeU64(out, anchorLen) || !copyFromReader(body, out, anchorLen)) return false;
	if (!body.readU64(noteLen) || noteLen > kMaxNote) return false;
	if (!writeU64(out, noteLen) || !copyFromReader(body, out, noteLen)) return false;
	if (!body.readU64(count)) return false;
	if (count > seg.links.size()) count = seg.links.size();
	if (!writeU64(out, count)) return false;
	for (uint64_t k = 0; k < count; k++)
	{
		uint64_t endFrame = 0, len = 0;
		if (!body.readU64(endFrame) || !body.readU64(noteLen) || noteLen > kMaxNote) return false;
		if (!writeU64(out, endFrame) || !writeU64(out, noteLen) || !copyFromReader(body, out, noteLen)) return false;
		if (!body.readU64(len) || len > seg.spillLength) return false;
		if (!writeU64(out, len) || !copyFromReader(body, out, len)) return false;
	}
	return true;
}

/* The whole of writing a history, with nothing of the history in it but the
 * segments handed over. That is what lets it happen on the writer: the bodies
 * are shared and immutable, the metadata is a copy taken the moment the save
 * was asked for, and the spill file belongs to the thread doing the writing. */
bool StateHistory::writeHistoryFile(std::FILE *spill, const char *path, const std::string &id,
	const std::vector<Segment> &segments, std::string &error)
{
	std::FILE *f = std::fopen(path, "wb");
	if (f == nullptr)
	{
		error = std::string("could not write the state history: ") + std::strerror(errno);
		return false;
	}
	bool ok = writeAll(f, kMagic, sizeof kMagic - 1)
		&& writeU64(f, id.size())
		&& writeAll(f, id.data(), id.size())
		&& writeU64(f, segments.size());
	for (const Segment &seg : segments)
	{
		if (!ok) break;
		ok = writeU64(f, static_cast<uint64_t>(seg.anchorFrame));
		if (!ok) break;
		if (seg.spilled)
		{
			/* A spilled segment is already in exactly this shape, minus the
			 * frame just written, so it is copied rather than rebuilt - which
			 * keeps the promise that nothing here is assembled in memory. Link
			 * by link, and only as many as the stretch still answers for: an
			 * edit that truncated it left the old timeline's links in the file,
			 * and copying the body whole put them in the saved history, where
			 * a reopened project offered frames the movie no longer had. */
			ok = copySpilledBody(spill, f, seg);
			continue;
		}
		ok = writeSegmentBody(f, seg);
	}
	if (std::fclose(f) != 0) ok = false;
	if (!ok)
	{
		error = "the state history could not be written in full";
		std::remove(path);   /* half a history is worse than none */
		return false;
	}
	return true;
}

bool StateHistory::saveTo(const char *path, const char *machineId, std::string &error)
{
	finishPlan();
	drainWriter();   /* spilled bodies are copied from the file, so it must be whole */
	return writeHistoryFile(m_spillRead != nullptr ? m_spillRead : m_spill, path,
		machineId != nullptr ? machineId : "", m_segments, error);
}

/* The same save, handed to the writer.
 *
 * WHY. Saving a project writes the whole history, and the budgets it is written
 * against are four gigabytes in memory and ten on disk - so this is up to
 * fourteen gigabytes, on the thread that runs the machine, measured at ten
 * seconds to a Linux disk and over a minute to NTFS. TAStudio's autosave fires
 * it every thirty minutes without being asked.
 *
 * WHAT MAKES IT SAFE. The metadata is copied here, now - so the file describes
 * the history as it was at the moment of asking, which is what a save means -
 * and the bodies are shared and immutable, so what the history does next
 * (coarsening, evicting, an edit) cannot touch them. Spilled stretches are read
 * from the spill file by the same thread that writes it, in queue order, and
 * the one thing that could move them underneath - a compaction - waits for the
 * writer like every other reader.
 *
 * False means it was not started; true means it was queued and `saveDone` will
 * say what happened. */
bool StateHistory::saveToLater(const char *path, const char *machineId, std::string &error)
{
	finishPlan();   /* the snapshot below shares the anchor's bytes */
	if (path == nullptr || path[0] == '\0')
	{
		error = "no path to save the state history to";
		return false;
	}
	if (!m_writer.threaded())
	{
		/* No writer: it happens here, and the ANSWER is recorded all the same.
		 * saveWait has one contract whichever way the save went, or a caller
		 * that checks it would read "the save failed" from a save that worked
		 * simply because the helpers were off. */
		const bool ok = saveTo(path, machineId, error);
		std::lock_guard<std::mutex> lock(m_savedLock);
		m_saved.asked = true;
		m_saved.pending = false;
		m_saved.ok = ok;
		m_saved.error = ok ? std::string() : error;
		return ok;
	}

	/* One at a time: the second would be describing a history the first has
	 * already been told about, and a save nobody waited for is not worth
	 * queueing twice. */
	saveWait(error);

	{
		std::lock_guard<std::mutex> lock(m_savedLock);
		m_saved = SaveResult{};
		m_saved.asked = true;
		m_saved.pending = true;
	}
	const std::string id = machineId != nullptr ? machineId : "";
	const std::string where = path;
	std::vector<Segment> snapshot = m_segments;   /* shared bodies; copied metadata */
	std::FILE *const spill = m_spill;
	m_writer.post([this, spill, where, id, snapshot = std::move(snapshot)]() mutable {
		std::string why;
		const bool ok = writeHistoryFile(spill, where.c_str(), id, snapshot, why);
		std::lock_guard<std::mutex> lock(m_savedLock);
		m_saved.pending = false;
		m_saved.ok = ok;
		m_saved.error = std::move(why);
	});
	return true;
}

bool StateHistory::savePending() const
{
	std::lock_guard<std::mutex> lock(m_savedLock);
	return m_saved.pending;
}

bool StateHistory::saveWait(std::string &error)
{
	m_writer.drain();
	applyWrites();
	std::lock_guard<std::mutex> lock(m_savedLock);
	/* Nothing was ever asked for, so there is nothing that failed. A caller
	 * closing a project calls this whether or not it saved. */
	if (!m_saved.asked) return true;
	if (!m_saved.ok && !m_saved.error.empty()) error = m_saved.error;
	return m_saved.ok;
}

bool StateHistory::loadFrom(const char *path, const char *machineId, std::string &error)
{
	clear();
	std::FILE *f = std::fopen(path, "rb");
	if (f == nullptr) return true;   /* no history yet is not a failure */

	auto give_up = [&](std::string why) {
		std::fclose(f);
		clear();
		error = std::move(why);
		return false;
	};

	char magic[sizeof kMagic - 1];
	if (!readAll(f, magic, sizeof magic)) return give_up("that is not a state history");
	if (std::memcmp(magic, kMagic, sizeof magic) != 0)
	{
		for (const char *old : kSuperseded)
		{
			if (std::memcmp(magic, old, sizeof magic) != 0) continue;
			std::fclose(f);
			clear();
			return true;
		}
		return give_up("that is not a state history");
	}
	uint64_t idLen = 0;
	if (!readU64(f, idLen) || idLen > (1u << 20)) return give_up("the state history is damaged");
	std::string id(static_cast<size_t>(idLen), '\0');
	if (!readAll(f, id.data(), id.size())) return give_up("the state history is damaged");
	if (id != (machineId != nullptr ? machineId : ""))
	{
		/* Not damage, and not an error the user did anything about: these states
		 * belong to a machine with other settings, files or core. */
		std::fclose(f);
		clear();
		return true;
	}

	uint64_t segCount = 0;
	if (!readU64(f, segCount)) return give_up("the state history is damaged");
	for (uint64_t i = 0; i < segCount; i++)
	{
		Segment seg;
		uint64_t anchorFrame = 0, anchorLen = 0, deltaCount = 0, noteLen = 0;
		if (!readU64(f, anchorFrame) || !readU64(f, anchorLen)) return give_up("the state history is damaged");
		seg.anchorFrame = static_cast<int64_t>(anchorFrame);
		Bytes anchorBody(static_cast<size_t>(anchorLen));
		if (!readAll(f, anchorBody.data(), anchorBody.size())) return give_up("the state history is damaged");
		seg.anchor = Body::make(std::move(anchorBody));
		if (!readU64(f, noteLen) || noteLen > kMaxNote) return give_up("the state history is damaged");
		seg.anchorNote.resize(static_cast<size_t>(noteLen));
		if (!readAll(f, seg.anchorNote.data(), seg.anchorNote.size())) return give_up("the state history is damaged");
		if (!readU64(f, deltaCount)) return give_up("the state history is damaged");
		seg.bytes = anchorLen;
		int64_t landed = seg.anchorFrame;
		for (uint64_t d = 0; d < deltaCount; d++)
		{
			uint64_t endFrame = 0, len = 0;
			if (!readU64(f, endFrame) || !readU64(f, noteLen) || noteLen > kMaxNote)
			{
				return give_up("the state history is damaged");
			}
			std::vector<uint8_t> note(static_cast<size_t>(noteLen));
			if (!readAll(f, note.data(), note.size())) return give_up("the state history is damaged");
			if (!readU64(f, len)) return give_up("the state history is damaged");
			/* the spans have to tile: a file whose links go backwards or stand
			 * still would offer frames it cannot walk to */
			if (static_cast<int64_t>(endFrame) <= landed) return give_up("the state history is damaged");
			landed = static_cast<int64_t>(endFrame);
			Bytes delta(static_cast<size_t>(len));
			if (!readAll(f, delta.data(), delta.size())) return give_up("the state history is damaged");
			seg.bytes += len;
			seg.links.push_back(Link{ Body::make(std::move(delta)), landed, std::move(note) });
		}
		m_bytes += seg.bytes;
		m_segments.push_back(std::move(seg));
		/* Per segment, not at the end: a history can be larger than the budget
		 * - that is what spilling is for - and holding all of it at once while
		 * deciding what to keep would be the very thing this design removed. */
		if (enabled()) evict();
	}
	std::fclose(f);
	if (enabled()) evict();
	return true;
}

} // namespace chimera
