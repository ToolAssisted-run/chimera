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
	enum Display { DispUnsigned = 0, DispSigned, DispFloat };
	enum PreviousType { PrevOriginal = 0, PrevLastSearch, PrevLastFrame, PrevLastChange };
	enum Column { ColAddress = 0, ColValue, ColPrev, ColChanges, ColDiff };

	struct Query
	{
		int compare = CmpPrevious;
		int op = OpEqual;
		int display = DispUnsigned;
		uint32_t value = 0;
		uint32_t differentBy = 0;
	};

	struct Row
	{
		uint64_t address = 0;
		uint32_t current = 0;
		uint32_t previous = 0;
		uint32_t changes = 0;
	};

	CeRamSearch(const uint8_t *base, CeRamSearchReadFn fn, void *user, int64_t domainSize);

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
	void setPreviousToCurrent();
	void clearChangeCounts();
	void setBigEndian(bool bigEndian) { m_bigEndian = bigEndian; }
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
	static constexpr int MaxUndoLevels = 5;

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
		std::vector<uint32_t> prev;
		std::vector<uint32_t> cur;     /* detailed */
		std::vector<uint32_t> changes; /* detailed */
	};

	static constexpr uint64_t RankWords = 64;
	static constexpr int64_t Chunk = 1 << 20;

	const uint8_t *m_base;
	CeRamSearchReadFn m_fn;
	void *m_user;
	int64_t m_domainSize;
	bool m_bigEndian = false;
	bool m_detailed = false;
	bool m_misaligned = false;
	int m_previousType = PrevLastSearch;
	bool m_undoEnabled = true;
	Set m_set;
	std::deque<Set> m_undo, m_redo;
	mutable std::vector<uint8_t> m_scratch;

	void readInto(int64_t offset, uint8_t *buf, int64_t len) const;
	const uint8_t *window(int64_t offset, int64_t len) const;
	uint32_t load(const uint8_t *p, int size) const;
	void store(uint8_t *p, int size, uint32_t v) const;
	uint32_t peek(uint64_t address, int size) const;

	static bool matches(const Query &q, int size, uint64_t address, uint32_t cur, uint32_t prev, uint32_t changes);
	void rebuildRank();
	uint64_t slotAt(int64_t index) const;
	void snapshotImage(std::vector<uint8_t> &image) const;
	void makeSparse();
	void settle();
	bool mustBeSparse() const;
	void recordUndo();
	template <typename F> void forEachDense(bool needMemory, F f);
};
