/* ram_search.hpp - RAM Search over a memory domain of any size.
 *
 * The candidate set has two shapes. While most of the domain is still a
 * candidate it is DENSE: one bit per slot, and "previous" is a byte image of
 * the domain - a byte per byte however many candidates there are, so a first
 * search over hundreds of megabytes costs what the memory itself costs. Once a
 * search has cut it down it turns SPARSE: parallel arrays, a few bytes per
 * survivor, which is also the only shape that can hold an order other than by
 * address. The switch is an implementation detail; every operation means the
 * same in both, and test_ram_search.cpp holds both to one oracle.
 *
 * Memory comes through a pointer when the domain has one, and through a
 * callback in chunks when it has not (a bus, a test's byte array).
 *
 * A domain may be mostly nothing: a bus over a gigabyte of address space with
 * thirty megabytes mapped (chimera#218). Told which ranges are live, a search
 * is over those alone - its images and its reads cost what the live part
 * costs. Inside, the live ranges are laid end to end and everything works on
 * that compacted domain as it always has; addresses are the real ones again
 * wherever they leave or enter (rows, indexOf, the address lists, a search by
 * address). A candidate never spans two ranges.
 */
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

typedef int64_t (*CeRamSearchReadFn)(void *user, int64_t offset, uint8_t *buf, int64_t len);

class CeRamSearch
{
public:
	/* The numbering is the ABI's (engine.h) and the frontend's enums. */
	enum Compare { CmpPrevious = 0, CmpSpecificValue, CmpSpecificAddress, CmpChanges, CmpDifference };
	enum Op { OpEqual = 0, OpGreaterThan, OpGreaterThanEqual, OpLessThan, OpLessThanEqual, OpNotEqual, OpDifferentBy };
	/* DispFloat is a float at size 4 and a double at size 8 */
	enum Display { DispUnsigned = 0, DispSigned, DispFloat };
	enum PreviousType { PrevOriginal = 0, PrevLastSearch, PrevLastFrame, PrevLastChange };
	enum Column { ColAddress = 0, ColValue, ColPrev, ColChanges, ColDiff };

	struct Query
	{
		int compare = CmpPrevious;
		int op = OpEqual;
		int display = DispUnsigned;
		/* a value as its bytes read in the search's byte order (a float's or a
		 * double's bits); a difference as a 64-bit integer */
		uint64_t value = 0;
		uint64_t differentBy = 0;
	};

	struct Row
	{
		uint64_t address = 0;
		uint64_t current = 0;
		uint64_t previous = 0;
		uint32_t changes = 0;
	};

	CeRamSearch(const uint8_t *base, CeRamSearchReadFn fn, void *user, int64_t domainSize);

	/* Which of the domain is live: n pairs of (address, length), in any order.
	 * Taken up by the next start() and kept until the one after - what is
	 * mapped later is not part of a search already begun. n < 0: all of it. */
	void setRanges(const int64_t *pairs, int64_t n);
	/* Bytes the running search covers: the domain's size, or its live part. */
	int64_t searchedSize() const { return m_domainSize; }

	/* Every address of the domain becomes a candidate, its previous value what
	 * memory holds now. History is forgotten. */
	void start(int size, bool misaligned, bool bigEndian, bool detailed);

	int64_t count() const { return (int64_t)m_set.count; }
	bool row(int64_t index, Row &out) const;
	int64_t indexOf(uint64_t address) const;

	/* Returns how many candidates the search removed. */
	int64_t search(const Query &q, int previousType);
	bool wouldRemove(int64_t index, const Query &q) const;

	/* Detailed mode's per-frame poll: counts changes and moves "previous". */
	void update(int previousType);
	/* update(), when the memory is not what the last update saw: a write or a
	 * state loaded between frames. True when it did. */
	bool catchUp(int previousType);
	void setPreviousToCurrent();
	void clearChangeCounts();
	/* A sparse set keeps its values as numbers read in the old byte order, so
	 * they are turned round when the order changes; a dense one keeps bytes. */
	void setBigEndian(bool bigEndian);
	/* LastChange moves one candidate's previous at a time, which a byte image
	 * cannot hold for overlapping candidates. */
	void setPreviousType(int previousType);

	void removeIndices(const int64_t *indices, int64_t n);
	void removeAddresses(const uint64_t *addresses, int64_t n, bool recordUndo);
	void addAddresses(const uint64_t *addresses, int64_t n, bool append);
	void convertTo(int size);
	void sort(int column, bool reverse, int display);

	int64_t outOfRangeCount() const;
	void removeOutOfRange();

	void setUndoEnabled(bool on) { m_undoEnabled = on; if (!on) clearHistory(); }
	bool canUndo() const { return m_undoEnabled && !m_undo.empty(); }
	bool canRedo() const { return m_undoEnabled && !m_redo.empty(); }
	void clearHistory() { m_undo.clear(); m_redo.clear(); }
	/* Each returns the change in the candidate count. */
	int64_t undo();
	int64_t redo();

	/* For tests: the shape is not part of the contract, but both must be run. */
	bool isDense() const { return m_set.dense; }
	/* For tests: candidates the last detailed frame looked at. */
	uint64_t walkedLastUpdate() const { return m_walked; }
	static constexpr int MaxUndoLevels = 5;
	/* What undo and redo may hold together (user-decided, 2026-10-18): the
	 * oldest level goes first, and a set larger than all of it is not kept.
	 * A detailed search of a 256 MiB domain is about 1.5 GiB a level. */
	static constexpr uint64_t UndoBudgetBytes = uint64_t{ 1 } << 30;
	void setUndoBudget(uint64_t bytes) { m_undoBudget = bytes; }

private:
	struct Set
	{
		bool dense = true;
		bool reversed = false; /* dense only: listed from the top address down */
		uint64_t count = 0;
		int size = 1;
		int step = 1;
		/* dense */
		uint64_t slots = 0;
		std::vector<uint64_t> bits;
		std::vector<uint64_t> rank; /* candidates before each RankWords words */
		std::vector<uint8_t> prevImage;
		std::vector<uint8_t> curImage;   /* detailed */
		std::vector<uint32_t> changesBySlot; /* detailed */
		/* sparse */
		std::vector<uint64_t> addr;
		std::vector<uint64_t> prev;
		std::vector<uint64_t> cur;     /* detailed */
		std::vector<uint32_t> changes; /* detailed */
	};

	static constexpr uint64_t RankWords = 64;
	static constexpr int64_t Chunk = 1 << 20;

	struct Range
	{
		int64_t real = 0;   /* where it is in the domain */
		int64_t length = 0;
		int64_t at = 0;     /* where it is in the compacted domain */
	};

	const uint8_t *m_base;
	CeRamSearchReadFn m_fn;
	void *m_user;
	int64_t m_realSize;   /* the domain's */
	int64_t m_domainSize; /* what is searched: m_realSize, or the live ranges' total */
	std::vector<Range> m_ranges;  /* of the running search; empty = the whole domain */
	std::vector<Range> m_pending; /* for the next start() */
	bool m_bigEndian = false;
	bool m_detailed = false;
	bool m_misaligned = false;
	int m_previousType = PrevLastSearch;
	bool m_undoEnabled = true;
	Set m_set;
	std::deque<Set> m_undo, m_redo;
	uint64_t m_undoBudget = UndoBudgetBytes;
	mutable std::vector<uint8_t> m_scratch;
	std::vector<uint8_t> m_nextImage;   /* detailed: the frame's image, before it becomes curImage */
	const std::vector<uint8_t> *m_walkImage = nullptr; /* the domain already read: window() serves from it */
	uint64_t m_walked = 0;

	void readReal(int64_t offset, uint8_t *buf, int64_t len) const;
	void readInto(int64_t offset, uint8_t *buf, int64_t len) const;
	uint64_t toReal(uint64_t address) const;
	bool toCompact(uint64_t real, uint64_t &address) const;
	bool inOneRange(uint64_t address, int size) const;
	const uint8_t *window(int64_t offset, int64_t len) const;
	uint64_t load(const uint8_t *p, int size) const;
	uint64_t peek(uint64_t address, int size) const;
	void readValues(const std::vector<uint64_t> &addr, int size, std::vector<uint64_t> &out) const;
	void previousFrom(const std::vector<uint8_t> &image);
	static uint64_t bytesOf(const Set &s);

	static bool matches(const Query &q, int size, uint64_t address, uint64_t cur, uint64_t prev, uint32_t changes);
	void rebuildRank();
	uint64_t slotAt(int64_t index) const;
	void snapshotImage(std::vector<uint8_t> &image) const;
	void makeSparse();
	void settle();
	bool mustBeSparse() const;
	bool recordUndo(); /* true when a level was recorded */
	void startUnguarded(int size, bool misaligned, bool bigEndian, bool detailed);
	int64_t filter(const Query &q, int previousType); /* search()'s work */
	template <typename F> void forEachDense(bool needMemory, F f);
	void filterSameAsPrevious(bool keepSame); /* filter()'s fast case */
};
