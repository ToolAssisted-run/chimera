/* The state history: where a machine has been, along a movie's timeline.
 *
 * One structure serves everything that asks that question - seeking in
 * TAStudio, stepping a frame back, and what survives closing a project (see
 * docs/state-manager.md). It replaces a map of whole savestates, which cost
 * what the MACHINE is: on a real xemu run a state is 75 MB at frame 150 and
 * 210 MB by frame 1200, so a history of any length was gigabytes and a per
 * frame history was impossible.
 *
 * A stored frame costs what the FRAME DID instead, by asking the sandbox what
 * changed since a moment rather than since the seal (miniBox epochs). Measured
 * on that same run: 2.0 MB a frame against a 210 MB state, about a hundredfold.
 *
 * The shape that makes deltas safe is the SEGMENT: one whole state - the
 * anchor - followed by the deltas that walk forward from it, one per frame and
 * contiguous. Contiguity is the invariant the whole file rests on. A delta only
 * means anything applied to exactly the machine it was measured against, so a
 * gap in a segment would make every frame after it unreachable; keeping
 * segments contiguous by construction means any frame the history claims is a
 * frame it can actually produce.
 *
 * A host that does not offer epochs (an older libminiboxhost beside a newer
 * libchimera) is not a failure: every capture is then an anchor, every segment
 * is one frame long, and the history behaves exactly as it did before.
 */
#ifndef CHIMERA_STATE_HISTORY_HPP
#define CHIMERA_STATE_HISTORY_HPP

#include <cstdint>
#include <cstdio>
#include <functional>
#include <set>
#include <string>
#include <memory>
#include <utility>
#include <vector>

#include <atomic>
#include <mutex>

#include "host_dyn.hpp"
#include "work_thread.hpp"

namespace chimera
{

/* A spilled body read back as the bytes it was before it went out.
 *
 * What reaches the spill file is compressed when libzstd is there to do it: a
 * machine state compresses seven to a hundred and eighteen times at zstd level
 * 1, a PlayStation 2 anchor of 232.9 MB becomes 9.95 MB, and that is the
 * difference between reading a quarter of a gigabyte back off a disk and
 * reading ten megabytes of it. Every reader of the file wants the raw body in
 * order - a restore streams it into the sandbox, a save copies it, settling
 * walks its links - so this hands out exactly that, from either a raw extent or
 * a zstd frame, without ever holding the decompressed body whole.
 *
 * It re-seeks before every read of the file, because the handle it is given is
 * shared with the other readers on the same thread. */
class SpillBodyReader
{
public:
	SpillBodyReader() = default;
	~SpillBodyReader();
	SpillBodyReader(const SpillBodyReader &) = delete;
	SpillBodyReader &operator=(const SpillBodyReader &) = delete;

	/* The body occupies [at, at + length) of `f`; `packed` says it is a zstd
	 * frame rather than the raw layout. */
	bool open(std::FILE *f, uint64_t at, uint64_t length, bool packed);
	bool read(void *out, size_t n);
	bool skip(uint64_t n);
	bool readU64(uint64_t &v) { return read(&v, sizeof v); }
	/* how far into the RAW body the next read begins */
	uint64_t pos() const { return m_pos; }

private:
	bool refill();

	std::FILE *m_f = nullptr;
	uint64_t m_at = 0, m_length = 0;
	bool m_packed = false;
	uint64_t m_pos = 0;
	uint64_t m_consumed = 0;       /* of the file's extent, when packed */
	void *m_stream = nullptr;
	std::vector<uint8_t> m_in, m_out;
	size_t m_inPos = 0, m_inLen = 0, m_outPos = 0, m_outLen = 0;
};

class StateHistory
{
public:
	/* budget 0 disables and drops everything. */
	void configure(const HostApi *host, void *obj, uint64_t budgetBytes);

	/* What the spill file may weigh, or 0 for no limit.
	 *
	 * The memory budget is met by MOVING bytes to disk, so on its own it bounds
	 * only half of what a history costs. This bounds the other half, by the same
	 * rule: the oldest goes first. What that costs is replaying to reach a frame
	 * that used to be stored, which is what the whole cache is - time, never
	 * work. */
	void diskBudget(uint64_t bytes);

	/* What the stretches still in the spill file really weigh on the disk -
	 * compressed, as the writer wrote them - counted when the writer reports
	 * each one landed. The file itself is between this and twice it, plus
	 * whatever is still being written - see diskBudget. */
	uint64_t diskBytes() const { return m_spillLive; }
	void clear();

	/* How dense the history is at each distance from the newest frame it holds,
	 * all in FRAMES - the engine does not know a core's frame rate and the
	 * caller does. Any value left at 0 keeps the current one.
	 *
	 * Editing a movie is local: the frames somebody steps through and re-records
	 * are the ones around the playhead, while the frames from ten minutes ago
	 * are jumped to rather than scrubbed through. So a frame is captured into
	 * the near band and coarsened as the playhead leaves it behind, which is a
	 * thing that can be done to a stored delta without a machine.
	 *
	 * `anchorSpacing` is the one that decides what a seek costs, since a
	 * restore walks the links of one anchor's stretch and no further. Positive,
	 * it is the spacing, exactly. Negative, it is the most a stretch may span,
	 * and the history closes one sooner once its deltas weigh as much as its
	 * anchor - see hasRoom. That is the default, capped at 600. */
	void bands(int64_t nearFrames, int64_t midFrames, int64_t midStride,
	           int64_t farStride, int64_t anchorSpacing);

	/* Where the far band goes once the budget is full, or nullptr for nowhere.
	 *
	 * The oldest stretches are the right thing to put on disk: large, rarely
	 * touched, and - if the file is lost - regenerable like everything else
	 * here. Without a directory the budget can only DROP them, which costs the
	 * frames themselves rather than the time to read them back. */
	void spillTo(const char *dir);

	/* Whether putting the far band on disk has failed since the directory was
	 * set - a full disk, almost always.
	 *
	 * The history carries on when it does: it thins in memory instead, which is
	 * correct and completely silent. From the piano roll that looks like the
	 * greenzone mysteriously going sparse, so somebody has to be told, and the
	 * engine is where the fact is. Saying it is the frontend's. */
	bool spillFailed() const { return m_spillFailed; }

	bool enabled() const { return m_budget != 0; }

	/* Whether this history's work may happen on a helper thread.
	 *
	 * Off, everything happens in line on the caller's thread, which is what the
	 * code did before any of this existed and is the reference the threaded
	 * path is tested against - the differential fuzz runs the same sequence
	 * both ways in one process and compares. CHIMERA_HELPERS=0 turns them off
	 * for the whole process whatever this says. */
	void helpers(bool on);

	/* Bytes handed to the writer whose landing has not been reported yet -
	 * diagnostics, and what a test watches to know the writer is idle. */
	uint64_t writesInFlight() const { return m_writeQueued; }

	/* Waits for everything the writer owes, so the spill FILE holds exactly
	 * what the history says it does.
	 *
	 * Nothing needs this to be correct - every reader already waits for the
	 * range it is about to read - but the file now lags the metadata by
	 * design, so anything that looks at the file itself rather than asking the
	 * history needs to say so: a test measuring its size, or a copy of the
	 * cache directory taken from underneath a running session. */
	void flushWrites();

	/* What the writer and the file cost, for the bench and for a trace: how
	 * many compactions the file has had, how many times the loop had to wait
	 * for the writer, and how long those waits took in total. A wait is the
	 * thing that turns a helper back into a stall, so it is counted rather
	 * than assumed. */
	struct Costs
	{
		uint64_t compactions = 0;
		uint64_t waits = 0;
		double waitSeconds = 0;
		/* what the writer was handed, and what reached the file */
		uint64_t spilledRaw = 0, spilledPacked = 0;
	};
	Costs costs() const { return m_costs; }

	/* What it may hold now, which is not what it was configured with if the
	 * machine has run out of memory since. */
	uint64_t budget() const { return m_budget; }
	uint64_t bytes() const { return m_bytes; }

	/* Frames the history can produce, which is every anchor plus every delta -
	 * NOT the number of stored objects, because a frame reached by walking
	 * deltas is as reachable as one with a state of its own. */
	int64_t count() const;
	/* how many stretches, each one anchor - what the spacing decided */
	int64_t anchors() const { return static_cast<int64_t>(m_segments.size()); }

	/* The greatest frame at or before `frame` that can be produced, or -1. */
	int64_t nearest(int64_t frame) const;

	/* Called immediately BEFORE an advance whose result will be captured. It
	 * decides anchor-or-delta for that frame and, for a delta, marks the epoch
	 * the sandbox will measure. Deciding here rather than after the advance is
	 * forced: an epoch has to be open before the machine moves. */
	void beforeAdvance();

	/* Called immediately after that advance, with the frame now standing at.
	 *
	 * `note` is the caller's own bookkeeping for this frame, stored with it and
	 * handed back by noteFor. The engine never looks inside it. It exists
	 * because a frontend has side-band state a savestate does not carry - a lag
	 * flag, a lag count, its own frame number - and keeping that in a table of
	 * the caller's own would mean mirroring every invalidation, eviction,
	 * coarsening and spill this class does. Riding along is the only way it
	 * stays true. */
	void capture(int64_t frame, const uint8_t *note = nullptr, size_t noteLen = 0);

	/* What was stored with `frame`, or nullptr. Borrowed, and invalidated by the
	 * next capture. */
	const uint8_t *noteFor(int64_t frame, size_t &lenOut) const;

	/* Drops everything after `frame`. An input edit at a frame makes every
	 * later state a lie, while the state AT it still holds. */
	void invalidateAfter(int64_t frame);

	/* Frames to keep reachable whatever the bands would otherwise do: a landing
	 * that is pinned is never merged away, and a stretch holding one is spilled
	 * rather than dropped.
	 *
	 * What deserves pinning is the caller's business - a marker somebody wants
	 * to jump to instantly, and nothing the engine could work out for itself.
	 * Pinning a frame the history does not hold is not an error; it takes
	 * effect if that frame is ever stored. */
	void pin(int64_t frame, bool pinned);
	bool pinned(int64_t frame) const;
	void unpinAll();

	/* Puts the machine back to `frame`, which must be one nearest() offered.
	 * Restores that frame's segment anchor and walks its deltas forward.
	 * False with `error` set. */
	/* Puts the machine on `frame`. On failure the machine is left on a frame it
	 * CAN be trusted on rather than half way through a chain, and `landedOn`
	 * (when given) says which - see the definition. */
	bool restore(int64_t frame, std::string &error, int64_t *landedOn = nullptr);

	/* Writes the history to a file, and reads one back.
	 *
	 * Streamed, one segment at a time, straight to and from the file: no part of
	 * this may be assembled in memory first. That is not an optimisation, it is
	 * the bug this design exists to remove - the greenzone used to serialize
	 * itself through a managed byte[], which stops near 2GB, and a history of
	 * any real length silently failed to save and took the work with it.
	 *
	 * `machineId` describes the machine the states belong to - the caller's
	 * business, since it is the caller that knows about cores and settings and
	 * files. Loading a history that names a different machine refuses and leaves
	 * the history empty: a state is the memory of one exact machine, and the
	 * sandbox only checks the core binary. An absent or unreadable file is that
	 * same empty answer, because losing this costs recomputation and never work.
	 * False with `error` set. */
	bool saveTo(const char *path, const char *machineId, std::string &error);
	bool loadFrom(const char *path, const char *machineId, std::string &error);

	/* The same save, on the writer, so that saving a project does not stop the
	 * machine for as long as the history is big.
	 *
	 * With the default budgets that is up to fourteen gigabytes - four in
	 * memory and ten on disk - which measured ten seconds to a Linux disk and
	 * over a minute to NTFS, on the thread that runs the emulator, fired every
	 * thirty minutes by TAStudio's autosave without anybody asking.
	 *
	 * What the file describes is the history at the moment this was called: the
	 * metadata is copied now and the bodies are shared and immutable, so
	 * whatever the run does next cannot change what is being written. True when
	 * it was queued (or, with helpers off, done). `saveWait` is the barrier,
	 * and closing a project is where it belongs. */
	bool saveToLater(const char *path, const char *machineId, std::string &error);
	bool savePending() const;
	/* Waits for a queued save and says whether it worked. False with `error`
	 * set on a failure; false and `error` untouched when there was none. */
	bool saveWait(std::string &error);

private:
	/* One step along a segment: the bytes that walk the machine from wherever
	 * the link before it landed to `endFrame`.
	 *
	 * A link spans a single frame when it is captured, and more than one once
	 * the history has thinned it - two adjacent links compose into one that
	 * spans both (docs/state-manager.md). That is why a link carries the frame
	 * it lands ON rather than being counted from the anchor: the stride is no
	 * longer a constant, and a frame in the middle of a span is not a frame
	 * this segment can produce.
	 *
	 * The contiguity invariant survives in the units it actually holds in: the
	 * spans tile the segment without gaps, so whatever the strides, every frame
	 * the history OFFERS is one it can walk to exactly. */
	/* Bytes that are not zeroed before they are written over.
	 *
	 * A body is always filled immediately - by the sandbox writing a state into
	 * it, or by a read from the spill file - so value-initialising it first is
	 * pure waste, and on the scale this deals in it is not a small waste: a
	 * PlayStation 2 anchor is 232 MB, and `resize` spent 106 to 155 MILLISECONDS
	 * zeroing and first-touching it on the thread that runs the machine, against
	 * 11 ms for the work it was preparing. That one memset was most of what a
	 * planned anchor cost, and it is the reason this allocator exists.
	 *
	 * Whoever fills the bytes pays for touching them, which for a planned anchor
	 * is the drainer rather than the emulation thread. */
	template <class T>
	struct NoInit
	{
		using value_type = T;
		NoInit() = default;
		template <class U> NoInit(const NoInit<U> &) noexcept {}
		T *allocate(size_t n) { return static_cast<T *>(::operator new(n * sizeof(T))); }
		void deallocate(T *p, size_t) noexcept { ::operator delete(p); }
		template <class U> void construct(U *) noexcept { /* deliberately nothing */ }
		template <class U, class... Args> void construct(U *p, Args &&...args)
		{
			::new (static_cast<void *>(p)) U(std::forward<Args>(args)...);
		}
		template <class U> bool operator==(const NoInit<U> &) const noexcept { return true; }
		template <class U> bool operator!=(const NoInit<U> &) const noexcept { return false; }
	};

public:
	/* What a state, a delta and a saved history are made of. */
	using Bytes = std::vector<uint8_t, NoInit<uint8_t>>;

private:
	/* A body of bytes - an anchor or a delta - shared and never changed once it
	 * is built.
	 *
	 * It is shared because a helper thread may be reading one while the loop
	 * decides to drop it: an edit arrives, a stretch is coarsened, a budget
	 * evicts. Holding the bytes by reference means the structure can let go
	 * whenever it likes and the bytes survive exactly as long as somebody is
	 * looking at them. It is IMMUTABLE because two readers are only safe
	 * without a lock if there is no writer - so a merge produces a new body
	 * rather than editing one, which is what composition did anyway.
	 *
	 * Empty and absent are the same answer here: a spilled stretch's bodies are
	 * in the file, and asking one for its size gets 0 either way, which is what
	 * every caller already expected of an emptied vector. */
	struct Body
	{
		std::shared_ptr<const Bytes> p;

		size_t size() const { return p ? p->size() : 0; }
		const uint8_t *data() const { return p ? p->data() : nullptr; }
		bool empty() const { return size() == 0; }
		void reset() { p.reset(); }

		static Body make(Bytes &&v)
		{
			return Body{ std::make_shared<const Bytes>(std::move(v)) };
		}
	};

	struct Link
	{
		Body bytes;
		int64_t endFrame = 0;
		std::vector<uint8_t> note;   /* the caller's, for the frame this lands on */	};

	struct Segment
	{
		int64_t anchorFrame = 0;
		Body anchor;                   /* a whole machine, unless spilled */
		std::vector<uint8_t> anchorNote;
		std::vector<Link> links;
		uint64_t bytes = 0;

		/* Spilled: the bytes are in the spill file at spillAt, and `anchor` and
		 * the links' bytes are empty. What frames it holds stays in memory -
		 * that is metadata, it is small, and answering "can you reach frame N"
		 * must not touch a disk. */
		bool spilled = false;
		/* Set while this stretch's write is still with the writer, and cleared
		 * when it lands. Dropping the stretch first clears it, which tells the
		 * job that nobody wants those bytes any more: writing them would be
		 * megabytes of I/O into a range that will never be read, and it would
		 * make the file look full of dead room and bring on a compaction that
		 * copies every live byte for nothing. */
		std::shared_ptr<std::atomic<bool>> writeWanted;
		/* Spilled AND on the far band's grid already, or past helping: nothing
		 * settleSpilled() would do to it. Set when a stretch is spilled after
		 * the far boundary has passed it, or once it has been rewritten. */
		bool settled = false;

		/* What this segment costs in MEMORY, which is what the budget is about
		 * and is not the same as how big it is. A spilled segment is still as
		 * big as it ever was - `bytes` describes the file - and costs nothing.
		 * Every adjustment of m_bytes goes through this, because the one that
		 * did not underflowed it: a spilled segment dropped by invalidateAfter
		 * gave its bytes back a second time, m_bytes wrapped past zero, and
		 * `m_bytes > m_budget` was true forever after - so the history spilled a
		 * segment every frame for the rest of the session. */
		uint64_t memoryBytes() const { return spilled ? 0 : bytes; }
		uint64_t spillAt = 0;
		uint64_t spillLength = 0;
		/* Where the bytes REALLY are, which is not where the two above say.
		 *
		 * spillAt and spillLength are LOGICAL: the uncompressed body's length,
		 * reserved the moment the stretch is spilled. Every decision the history
		 * makes about the file - what is live, what is dead, when to compact,
		 * what the disk budget drops - is made from those, and they have to come
		 * out the same threaded as in line, which a size nobody knows until the
		 * compressor has run could not. The writer compresses the body, appends
		 * it wherever the file really ends, and says where when it reports:
		 * that is these, and only readers look at them. */
		uint64_t physAt = 0, physLength = 0;
		bool packed = false;

		int64_t lastFrame() const { return links.empty() ? anchorFrame : links.back().endFrame; }
		/* what walking every link costs in bytes, next to what loading the anchor does */
		uint64_t linkBytes() const { return bytes - anchor.size(); }

		/* The greatest frame this segment can produce at or before `f`, or -1.
		 * Not the same as being inside the segment: with strides above one,
		 * most frames between two links are not stored anywhere. */
		int64_t nearestIn(int64_t f) const;

		/* How many links to apply to land exactly on `f`, or -1 when `f` is
		 * not one of this segment's frames. */
		int64_t stepsTo(int64_t f) const;
	};

	bool deltasAvailable() const;
	/* Whether the newest stretch takes another delta, or the next capture is an anchor. */
	bool hasRoom(const Segment &seg) const;
public:
	/* The least a stretch's links must weigh before their weight may close it
	 * (see hasRoom). 64 MB unless a test needs a machine small enough to see it. */
	void anchorWalkFloor(uint64_t bytes) { m_anchorWalkFloor = bytes; }
private:
	bool composeAvailable() const;
	void evict();

	/* ---- the writer -------------------------------------------------------
	 *
	 * Spilling a stretch is an fwrite of everything it holds - 7 ms to a Linux
	 * disk and 57 to NTFS for ten megabytes, measured - and it happens inside
	 * evict(), which happens inside a capture, which happens inside a frame. It
	 * has nothing to do with the machine: it is bytes going to a file.
	 *
	 * So it goes to a helper, and the metadata is settled HERE, at once: the
	 * stretch is marked spilled, its offset and length are reserved, and its
	 * bytes are handed to the job. The budget therefore behaves exactly as it
	 * did - the arithmetic is unchanged and so are the decisions that follow
	 * from it - and the only thing that is late is the write.
	 *
	 * What that costs is one rule, kept by drainWriter(): anything that READS
	 * the file waits for the writer first. Restores, saves, settling and
	 * compaction all do. A capture does not, and captures are what a frame
	 * has to be quick for.
	 *
	 * A write that FAILS is discovered late, so it is undone late: the
	 * completion hands the bytes back, the stretch stops being spilled and its
	 * memory is counted again - which leaves the history exactly where the
	 * synchronous path left it when a spill returned false. */
	struct PendingWrite
	{
		uint64_t at = 0, length = 0;          /* logical: what was reserved */
		uint64_t physAt = 0, physLength = 0;  /* physical: where it landed */
		bool packed = false;
		int64_t anchorFrame = -1;
		Segment body;       /* bodies are shared; the notes are small */
		std::shared_ptr<std::atomic<bool>> wanted;
		bool ok = false;
		bool skipped = false;   /* the stretch was dropped before this ran */
	};
	/* Hands a reserved body to the writer, which appends it compressed. */
	void postWrite(PendingWrite &&job);

	/* How far the writer may fall behind before a spill waits for it.
	 *
	 * Unbounded, the loop queues faster than a disk can take: a run measured 29
	 * MB of writes in flight, which is 29 MB of bodies held in memory the budget
	 * does not know about, and - because a stretch can be dropped while its
	 * write is still queued - 29 MB of file that is dead on arrival. That trebled
	 * the number of compactions, and a compaction copies every live byte. Half
	 * the memory budget, and never less than four megabytes nor more than
	 * sixty-four. */
	uint64_t writeQueueCap() const;
	/* Everything the file may weigh from a body of this shape. Must agree with
	 * writeSegmentBody to the byte, because the offset it reserves is where the
	 * next one starts; test_state_history pins the two together. */
	static uint64_t segmentBodyLength(const Segment &seg);
	/* Finishes every write in flight and applies what they said. Called by
	 * everything that reads the spill file, and by anything that closes it. */
	void drainWriter();
	/* Applies the completions that have landed, on this thread. */
	void applyWrites();

	/* capture(), once. capture() itself is the loop that answers an allocation
	 * failure by making the budget smaller and asking again. */
	void captureOnce(int64_t frame, const uint8_t *note, size_t noteLen);
	bool halveBudget();

	/* Under this a history cannot hold one anchor of anything, so halving past
	 * it would spend allocations to store nothing. Not zero, which the engine
	 * reads as "no history at all" - a machine short of memory still wants the
	 * frames it can afford. */
	static constexpr uint64_t kSmallestBudget = 16ull * 1024 * 1024;

	/* Holds the spill file under its budget by dropping the oldest stretches in
	 * it, and reclaims what they held: the file is a queue - appended newest,
	 * dropped oldest - so what is dead is always a prefix, and moving the live
	 * suffix to the front is all a compaction is. Done when the dead half is the
	 * bigger half, which makes it O(1) copies per byte over a session. */
	void evictDisk();
	bool compactSpill();

	/* Which stretch to give up: never the first (the movie's beginning has to
	 * stay reachable) nor the newest nor one holding a pin, and among the rest
	 * the one whose absence widens the gap between its neighbours least. See
	 * its definition. m_segments.size() when nothing may go. */
	size_t chooseVictim(bool spilled) const;

	/* m_bytes -= n, and says so rather than wrapping if n is somehow more than
	 * there is. Clamping keeps a mistake to one wrong number instead of a budget
	 * that can never be met again. */
	void releaseBytes(uint64_t n, const char *where);

	/* Drops the stretch at `index`, giving back whatever it held. */
	void forgetSegment(size_t index);

	/* Moves one segment out to the spill file, freeing what it held in memory.
	 * False when there is nowhere to put it or the write failed, which is not
	 * an error - the budget then falls back to dropping frames. */
	static bool writeSegmentBody(std::FILE *f, const Segment &seg);
	/* The same layout, handed to any sink - the file for a raw body, the
	 * compressor for a packed one. */
	static bool writeSegmentBodyTo(const std::function<bool(const void *, size_t)> &put, const Segment &seg);
	/* The body as a zstd frame at the file's current position, or raw when
	 * there is no libzstd or CHIMERA_SPILL_RAW=1 asks for it. `packed` says
	 * which it was. */
	static bool writeSegmentBodyPacked(std::FILE *f, const Segment &seg, bool &packed);
	/* A spilled stretch into a saved history: the body, link by link, only
	 * as far as the stretch still answers (an edit may have truncated it).
	 * Static, and handed the spill file, because the writer does this too. */
	static bool copySpilledBody(std::FILE *spill, const std::function<bool(const void *, size_t)> &put,
		const Segment &seg);
	/* The whole of writing a history file, with nothing of `this` in it. */
	static bool writeHistoryFile(std::FILE *spill, const char *path, const std::string &id,
		const std::vector<Segment> &segments, std::string &error);
	bool spill(Segment &seg);
	void dropSpillFile();
	/* Removes spill files in `dir` that are not this history's: what sessions
	 * that died left behind. See its definition. */
	void sweepStaleSpills(const std::string &dir);

	/* Restores from a segment that is on disk, reading only as far along it as
	 * the target frame needs. Nothing about the segment is assembled in memory:
	 * the anchor and each link go straight from the file into the sandbox. */
	bool restoreSpilled(const Segment &seg, int64_t steps, std::string &error);

	/* Puts the machine somewhere trustworthy after a chain would not walk, and
	 * gives up the stretch that would not walk. Always returns false. */
	bool restoreFailed(const Segment *seg, std::string &error, int64_t *landedOn);

	/* Thins the bands the newest frame has just pushed a landing out of. Runs
	 * after every capture and does at most one merge per boundary, because the
	 * playhead moves one frame at a time and so exactly one landing crosses
	 * each boundary per frame. That is what keeps this off the critical path:
	 * coarsening follows DISTANCE, not the budget, so it is a little work every
	 * frame rather than a stall when the budget fills. */
	void coarsen(int64_t newestFrame);

	/* Watches what a capture costs against what the run costs and moves the
	 * near band's stride to keep the first a bounded share of the second. */
	void tuneStride(double captureSeconds, double wallSeconds);
	/* What an anchor does to the stride, which is nothing but reset the clock -
	 * see its definition for the arithmetic that says why. */
	void noteAnchorCost();

	/* Drops the landing at `frame` if the band it has fallen into does not want
	 * one there, by composing its link into the one after it. The landings a
	 * band keeps are the multiples of its stride, which makes this idempotent
	 * and the result independent of the order frames arrive in. */
	void tidy(int64_t frame, int64_t stride);

	/* The union of two links, if the caps allow: together they must fit the
	 * merge cap and the anchor they walk from. True when `merged` holds it. */
	bool composePair(const Link &a, const Link &b, uint64_t anchorLen, Bytes &merged);

	/* Composes the link at `i` into the one after it, if the caps allow. True
	 * when it did. */
	bool composeInto(Segment &seg, size_t i);

	/* A stretch that was spilled before the far band reached it keeps the
	 * density it had when it went - which used to be forever. Once the far
	 * boundary has passed a spilled stretch, this reads its links back one at
	 * a time, composes them down to the far grid, and writes what is left to
	 * the end of the file; the old body becomes dead room the compaction
	 * reclaims. Streamed, because a stretch spilled under a small budget is by
	 * definition one that does not fit in memory: what is held at once is the
	 * link being accumulated, the link just read, and the settled result,
	 * which is far-band sized. A few links per call, so it is a little work
	 * every frame rather than a stall. */
	void settleSpilled(int64_t farFrame);
	void finishSettling();
	/* The stretch being settled, as it stands now, or nullptr if it has moved,
	 * been rewritten, been dropped or been replaced by another with the same
	 * anchor frame. */
	Segment *settlingSegment();

	const HostApi *m_host = nullptr;
	void *m_obj = nullptr;
	uint64_t m_budget = 0;
	uint64_t m_bytes = 0;
	std::vector<Segment> m_segments;   /* ordered by anchorFrame, never overlapping */
	bool m_epochOpen = false;          /* an epoch is marked and a delta is wanted */

	/* Defaults for 60 frames a second, and conservative on purpose: they are
	 * what a core gets before anyone has measured it. See the band table in
	 * docs/state-manager.md for what they cost on a heavy one. */
	int64_t m_nearFrames = 120;        /* 2 s of every frame */
	int64_t m_midFrames = 1800;        /* then 30 s of one in a few */
	int64_t m_midStride = 3;
	int64_t m_farStride = 1200;        /* and beyond that, one in 20 s - which,
	                                    * being wider than a segment, collapses
	                                    * an old segment to its anchor */
	int64_t m_anchorSpacing = 600;     /* a new anchor every 10 s at the most */
	bool m_anchorByBytes = true;       /* and sooner when the links outweigh it */
	uint64_t m_anchorWalkFloor = 64ull << 20;   /* - and are worth the trouble */


	std::set<int64_t> m_pinned;

	std::string m_spillDir;
	int64_t m_newest = -1;             /* the last frame captured: where the bands are measured from */

	/* The spill file is open TWICE, and which handle a thread may touch is the
	 * whole of the rule that makes the writer safe.
	 *
	 * A stdio FILE is one position. Two threads using the same one - even with
	 * glibc locking every call, even reading a range nobody is writing - can
	 * interleave a seek with a seek: the loop seeks to 0 to read the oldest
	 * stretch, the writer seeks to the end to append, and the loop's read comes
	 * back from the end. That is exactly the bug this pair of handles removes,
	 * and it showed up as a restore returning somebody else's frames about one
	 * run in three.
	 *
	 * m_spill belongs to the WRITER (and to the loop only while the writer is
	 * idle or off); m_spillRead belongs to the LOOP. Separate handles are
	 * separate positions, so a read and a write of different ranges no longer
	 * have anything to share. */
	std::FILE *m_spillRead = nullptr;

	/* ---- an anchor taken while the machine runs -------------------------
	 *
	 * A whole machine is 95 to 98% memcpy - 40 to 180 ms for a 257 MB machine,
	 * 150 to 700 for a gigabyte one - and it happens once every anchorSpacing
	 * frames, which is a freeze on a clock. The copy does not need the machine
	 * to stand still, only the bytes, so the sandbox holds the pages and this
	 * fills them on the drainer (wbx_state_plan). What stays on this thread is
	 * the walk and one mprotect per run of pages: 3 to 14 ms instead.
	 *
	 * The anchor's bytes are NOT there until the fill is finished, so anything
	 * that reads them - a restore, a spill, a save - calls finishPlan first,
	 * and so does the next anchor. The machine may run throughout; a write to a
	 * page nobody has copied yet is copied by the fault handler. */
	bool planAvailable() const;
	/* Takes the anchor as a plan. False when this host cannot, and the caller
	 * falls back to writing the state in line. */
	bool captureAnchorPlanned(int64_t frame, std::vector<uint8_t> &carried);
	/* Finishes an anchor that is still being filled. Cheap when there is none. */
	void finishPlan();

	bool m_planPending = false;

	/* how long the drainer took to fill the last plan, in microseconds, for the trace */

	std::atomic<int64_t> m_fillMicros{ 0 };
	int64_t m_planFrame = -1;          /* the anchor being filled */
	WorkThread m_drainer{ "history drainer" };

	WorkThread m_writer{ "history writer" };
	std::mutex m_writtenLock;          /* only ever held by a writer job and by applyWrites */
	std::vector<PendingWrite> m_written;
	uint64_t m_writeQueued = 0;        /* bytes handed to the writer and not yet reported */
	uint64_t m_writtenThrough = 0;     /* the file is whole up to here (see applyWrites) */
	Costs m_costs;

	/* A queued save, and what became of it. */
	struct SaveResult
	{
		bool asked = false;    /* a save was requested at all */
		bool pending = false;
		bool ok = false;
		std::string error;
	};
	mutable std::mutex m_savedLock;
	SaveResult m_saved;

	/* ---- how much of the run the history is allowed to cost ----
	 *
	 * The near band used to keep EVERY frame, whatever a frame cost to keep.
	 * On a light machine that is right: a Game Boy's frame is 300KB and taking
	 * it is a fraction of a millisecond against three milliseconds of
	 * emulation. On a heavy one it is not: Ruffle rewrites twenty megabytes a
	 * frame, and storing that took as long again as running the frame - the
	 * greenzone doubled the cost of playing, which is exactly the complaint
	 * that prompted this.
	 *
	 * So the history measures what it costs and spends a bounded share of the
	 * run. When capture is taking more than kCostShare of wall time, the near
	 * band gets a STRIDE: frames between landings are not stored, and the epoch
	 * simply stays open across them, so the next delta describes all of them at
	 * once. Reaching a frame that was not stored costs replaying at most a
	 * stride of frames - which the bands already do further back, and which is
	 * cheap precisely because the machine is fast to run.
	 *
	 * It is a ratio rather than a byte count because the trade is between two
	 * costs, not between bytes and anything: a delta worth 20MB is dear on a
	 * core whose frame is three milliseconds and cheap on one whose frame is a
	 * tenth of a second. */
	static constexpr double kCostShare = 0.15;
	int64_t m_nearStride = 1;
	double m_captureSeconds = 0;       /* exponential means, in seconds */
	double m_wallSeconds = 0;
	double m_lastCaptureEnded = 0;
	int64_t m_capturesSinceTuned = 0;
	uint64_t m_lastAnchorBytes = 0;    /* what to ask for before taking the next one */

	/* the stretch being settled, if any (see settleSpilled) */
	struct Settling
	{
		bool active = false;
		/* Which stretch this is. Not the anchor frame alone: an edit can end a
		 * timeline and a new stretch can be spilled with the same anchor frame,
		 * and reading the old body's offsets out of the new one would settle it
		 * into nonsense. Where it lies in the file says which stretch it IS -
		 * and a compaction that moves it mid-settle simply ends this attempt,
		 * which costs the work and is asked again later. */
		int64_t anchorFrame = -1;
		uint64_t spillAt = 0, spillLength = 0;
		uint64_t anchorLen = 0;        /* the cap on a composed link */
		/* the old body, being read a few links a frame; reset by anything that
		 * replaces the file under it */
		std::unique_ptr<SpillBodyReader> reader;
		size_t linkIndex = 0, linkCount = 0;
		bool hasAcc = false;
		Link acc;                      /* the landing being composed into */
		std::vector<Link> out;         /* the settled landings so far */
		int merges = 0;                /* none at the end means nothing to rewrite */
	};
	Settling m_settle;
	std::FILE *m_spill = nullptr;      /* one file, appended to, holes and all */
	std::string m_spillPath;           /* its name: the process's and this instance's */
	uint64_t m_spillBytes = 0;    /* how long the file is LOGICALLY, dead room and all */
	uint64_t m_spillLive = 0;     /* what the live stretches in it really weigh, as reported */
	uint64_t m_fileBytes = 0;     /* how long the file really is, as reported */
	uint64_t m_diskBudget = 0;    /* what may stay LIVE: half the file's budget.
	                               * 0: no limit, which is what it was for a year */
	bool m_spillFailed = false;

public:
	~StateHistory();
	StateHistory() = default;
	StateHistory(const StateHistory &) = delete;
	StateHistory &operator=(const StateHistory &) = delete;
};

} // namespace chimera

#endif
