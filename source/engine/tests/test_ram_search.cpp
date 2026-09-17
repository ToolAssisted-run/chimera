/* test_ram_search.cpp - RAM Search against a model that holds one record per
 * address, which is how the frontend used to do it and why it stopped at
 * 64 MiB (issue #89).
 *
 * The model is the oracle: a random script of searches, polls, removals,
 * sorts, conversions and undos is run on both, and after every step every row
 * must agree. The script is long enough to pass through both shapes of the
 * candidate set, through a pointer and through a read function, and the test
 * fails if either shape was never reached. Then a domain well past the old
 * limit is searched, and what it cost is printed.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/ram_search.hpp"

#include "chimera/engine.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

using RS = CeRamSearch;

namespace
{
struct Rec
{
	uint64_t addr;
	uint32_t prev, cur, changes;
};

struct Model
{
	std::vector<uint8_t> *mem;
	int size = 1;
	bool misaligned = false, bigEndian = false, detailed = false;
	std::vector<Rec> list;
	std::vector<std::vector<Rec>> undo, redo;

	uint32_t peek(uint64_t a, int sz) const
	{
		if (a + (uint64_t)sz > mem->size()) return 0;
		uint32_t v = 0;
		for (int i = 0; i < sz; i++)
			v |= (uint32_t)(*mem)[a + (uint64_t)i] << 8 * (bigEndian ? sz - 1 - i : i);
		return v;
	}
	uint32_t value(const Rec &r) const { return detailed ? r.cur : peek(r.addr, size); }

	void start(int sz, bool mis, bool be, bool det)
	{
		size = sz; misaligned = mis; bigEndian = be; detailed = det;
		undo.clear(); redo.clear(); list.clear();
		const int step = mis ? 1 : sz;
		for (uint64_t a = 0; a + (uint64_t)sz <= mem->size(); a += (uint64_t)step)
			list.push_back({ a, peek(a, sz), peek(a, sz), 0 });
	}
	void record()
	{
		redo.clear();
		undo.push_back(list);
		if (undo.size() > (size_t)RS::MaxUndoLevels) undo.erase(undo.begin());
	}
	static int64_t sx(uint32_t v, int sz, int display)
	{
		if (display != RS::DispSigned) return v;
		return sz == 1 ? (int64_t)(int8_t)v : sz == 2 ? (int64_t)(int16_t)v : (int64_t)(int32_t)v;
	}
	static float f32(uint32_t v) { float f; std::memcpy(&f, &v, 4); return f; }
	static bool near(float a, float b) { return std::fabs(b - a) < 5.96046448E-08f; }
	bool keep(const Rec &r, const RS::Query &q) const
	{
		const uint32_t cur = value(r);
		bool isFloat = q.display == RS::DispFloat && (q.compare == RS::CmpPrevious || q.compare == RS::CmpSpecificValue || q.compare == RS::CmpDifference);
		if (isFloat)
		{
			float a = f32(cur), b = f32(r.prev);
			if (q.compare == RS::CmpSpecificValue) b = f32(q.value);
			if (q.compare == RS::CmpDifference) { a = a - b; b = f32(q.value); }
			switch (q.op)
			{
			case RS::OpEqual: return near(a, b);
			case RS::OpNotEqual: return !near(a, b);
			case RS::OpGreaterThan: return a > b;
			case RS::OpGreaterThanEqual: return a > b || near(a, b);
			case RS::OpLessThan: return a < b;
			case RS::OpLessThanEqual: return a < b || near(a, b);
			default: return near(std::fabs(a - b), f32(q.differentBy));
			}
		}
		int64_t a, b;
		switch (q.compare)
		{
		case RS::CmpSpecificValue: a = sx(cur, size, q.display); b = sx(q.value, size, q.display); break;
		case RS::CmpSpecificAddress: a = (int64_t)r.addr; b = q.value; break;
		case RS::CmpChanges: a = r.changes; b = q.value; break;
		case RS::CmpDifference:
			a = sx(cur, size, q.display) - sx(r.prev, size, q.display);
			b = q.display == RS::DispSigned ? (int64_t)(int32_t)q.value : (int64_t)q.value;
			break;
		default: a = sx(cur, size, q.display); b = sx(r.prev, size, q.display); break;
		}
		switch (q.op)
		{
		case RS::OpEqual: return a == b;
		case RS::OpNotEqual: return a != b;
		case RS::OpGreaterThan: return a > b;
		case RS::OpGreaterThanEqual: return a >= b;
		case RS::OpLessThan: return a < b;
		case RS::OpLessThanEqual: return a <= b;
		default: return (int64_t)q.differentBy == std::llabs(a - b);
		}
	}
	int64_t search(const RS::Query &q, int previousType)
	{
		record();
		const size_t before = list.size();
		list.erase(std::remove_if(list.begin(), list.end(), [&](const Rec &r) { return !keep(r, q); }), list.end());
		if (previousType == RS::PrevLastSearch) setPrev();
		return (int64_t)(before - list.size());
	}
	void setPrev()
	{
		for (Rec &r : list) r.prev = value(r);
	}
	void update(int previousType)
	{
		if (!detailed) return;
		for (Rec &r : list)
		{
			const uint32_t now = peek(r.addr, size);
			if (now != r.cur)
			{
				r.changes++;
				if (previousType == RS::PrevLastChange) r.prev = r.cur;
			}
			if (previousType == RS::PrevLastFrame) r.prev = r.cur;
			r.cur = now;
		}
	}
	void convertTo(int newSize)
	{
		undo.clear(); redo.clear();
		std::vector<Rec> out;
		const int span = misaligned ? 1 : size, step = misaligned ? 1 : newSize;
		for (const Rec &r : list)
			for (int k = 0; k < span; k++)
			{
				const uint64_t a = r.addr + (uint64_t)k;
				if (a + (uint64_t)newSize > mem->size() || a % (uint64_t)step) continue;
				out.push_back({ a, peek(a, newSize), peek(a, newSize), 0 });
			}
		list = out;
		size = newSize;
	}
};

struct Harness
{
	std::vector<uint8_t> mem;
	Model model;
	RS *rs = nullptr;
	bool sawDense = false, sawSparse = false;

	static int64_t readFn(void *user, int64_t offset, uint8_t *buf, int64_t len)
	{
		auto *m = (std::vector<uint8_t> *)user;
		std::memcpy(buf, m->data() + offset, (size_t)len);
		return len;
	}

	void agree(const char *after)
	{
		(rs->isDense() ? sawDense : sawSparse) = true;
		if (rs->count() != (int64_t)model.list.size())
		{
			std::fprintf(stderr, "after %s: count %lld, model %zu\n", after, (long long)rs->count(), model.list.size());
			assert(false);
		}
		for (size_t i = 0; i < model.list.size(); i++)
		{
			RS::Row row;
			assert(rs->row((int64_t)i, row));
			const Rec &r = model.list[i];
			if (row.address != r.addr || row.previous != r.prev || row.current != model.value(r)
				|| row.changes != (model.detailed ? r.changes : 0))
			{
				std::fprintf(stderr, "after %s: row %zu is %llx prev %x cur %x ch %u, model %llx prev %x cur %x ch %u\n",
					after, i, (unsigned long long)row.address, row.previous, row.current, row.changes,
					(unsigned long long)r.addr, r.prev, model.value(r), r.changes);
				assert(false);
			}
			if (i % 997 == 0) assert(rs->indexOf(r.addr) == (int64_t)i);
		}
		RS::Row none;
		assert(!rs->row((int64_t)model.list.size(), none));
	}
};

void script(bool throughPointer, int size, bool misaligned, bool bigEndian, bool detailed, uint32_t seed, Harness &h)
{
	std::mt19937 rng(seed);
	h.mem.resize(256 * 1024 + 3); /* a tail no aligned slot covers */
	for (auto &b : h.mem) b = (uint8_t)(rng() % 7); /* few values: searches thin it slowly */
	h.model.mem = &h.mem;

	RS rs(throughPointer ? h.mem.data() : nullptr, throughPointer ? nullptr : Harness::readFn, &h.mem, (int64_t)h.mem.size());
	h.rs = &rs;
	rs.start(size, misaligned, bigEndian, detailed);
	h.model.start(size, misaligned, bigEndian, detailed);
	h.agree("start");

	int previousType = RS::PrevLastSearch;
	bool byAddress = true; /* the model is only kept in address order */
	for (int step = 0; step < 60; step++)
	{
		/* the machine runs: some memory moves */
		for (int i = 0; i < 20000; i++) h.mem[rng() % h.mem.size()] = (uint8_t)(rng() % 7);
		rs.update(previousType);
		h.model.update(previousType);
		h.agree("update");

		switch (rng() % 10)
		{
		case 0:
		{
			previousType = (int)(rng() % 4);
			rs.setPreviousType(previousType);
			h.agree("previous type");
			break;
		}
		case 1:
		{
			if (h.model.list.empty()) break;
			std::vector<int64_t> idx;
			for (int i = 0; i < 50; i++) idx.push_back((int64_t)(rng() % h.model.list.size()));
			rs.removeIndices(idx.data(), (int64_t)idx.size());
			h.model.record();
			std::sort(idx.begin(), idx.end());
			idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
			for (auto it = idx.rbegin(); it != idx.rend(); ++it) h.model.list.erase(h.model.list.begin() + *it);
			h.agree("remove indices");
			break;
		}
		case 2:
		{
			const int64_t a = rs.undo();
			if (!h.model.undo.empty())
			{
				const int64_t before = (int64_t)h.model.list.size();
				h.model.redo.push_back(h.model.list);
				h.model.list = h.model.undo.back();
				h.model.undo.pop_back();
				assert(a == (int64_t)h.model.list.size() - before);
			}
			else assert(a == 0 && !rs.canUndo());
			/* a restored record's current value is as old as the record */
			if (detailed) { rs.update(RS::PrevOriginal); h.model.update(RS::PrevOriginal); }
			h.agree("undo");
			break;
		}
		case 3:
		{
			const int64_t a = rs.redo();
			if (!h.model.redo.empty())
			{
				h.model.undo.push_back(h.model.list);
				h.model.list = h.model.redo.back();
				h.model.redo.pop_back();
			}
			else assert(a == 0);
			if (detailed) { rs.update(RS::PrevOriginal); h.model.update(RS::PrevOriginal); }
			h.agree("redo");
			break;
		}
		case 4:
		{
			rs.setPreviousToCurrent();
			h.model.setPrev();
			h.agree("set previous");
			break;
		}
		case 5:
		{
			/* reversed by address, checked from the far end, and back */
			rs.sort(RS::ColAddress, true, RS::DispUnsigned);
			const size_t n = h.model.list.size();
			for (size_t i = 0; i < n; i += 1 + n / 50)
			{
				RS::Row row;
				assert(rs.row((int64_t)i, row) && row.address == h.model.list[n - 1 - i].addr);
				assert(rs.indexOf(row.address) == (int64_t)i);
			}
			rs.sort(RS::ColAddress, false, RS::DispUnsigned);
			h.agree("sort by address");
			break;
		}
		default:
		{
			RS::Query q;
			q.compare = (int)(rng() % 5);
			if (q.compare == RS::CmpChanges && !detailed) q.compare = RS::CmpPrevious;
			q.op = (int)(rng() % 7);
			q.display = (int)(rng() % (size == 4 ? 3 : 2));
			q.value = q.compare == RS::CmpSpecificAddress ? (uint32_t)(rng() % h.mem.size()) : (uint32_t)(rng() % 7);
			if (q.compare == RS::CmpDifference && rng() % 2) q.value = (uint32_t)-(int32_t)(rng() % 4);
			q.differentBy = (uint32_t)(rng() % 3);
			/* never let one search empty it: the script has further to go */
			size_t would = 0;
			for (const Rec &r : h.model.list) would += h.model.keep(r, q);
			if (would < 200 && h.model.list.size() > 200) break;
			for (size_t i = 0; i < h.model.list.size(); i += 1 + h.model.list.size() / 40)
				assert(rs.wouldRemove((int64_t)i, q) == !h.model.keep(h.model.list[i], q));
			const int64_t removed = rs.search(q, previousType);
			assert(removed == h.model.search(q, previousType));
			h.agree("search");
			break;
		}
		}
		(void)byAddress;
	}

	/* a conversion, from whatever shape the script ended in */
	const int other = size == 4 ? 2 : 4;
	rs.convertTo(other);
	h.model.convertTo(other);
	h.agree("convert");
	assert(!rs.canUndo());
}

void sortsByWhatIsShown()
{
	std::vector<uint8_t> mem(64, 0);
	const int32_t values[] = { 5, -3, 100, -200, 0, 7 };
	std::memcpy(mem.data(), values, sizeof values);
	RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
	rs.start(4, false, false, false);
	rs.sort(RS::ColValue, false, RS::DispSigned);
	RS::Row row;
	assert(rs.row(0, row) && (int32_t)row.current == -200);
	assert(rs.row(1, row) && (int32_t)row.current == -3);
	rs.sort(RS::ColValue, true, RS::DispSigned);
	assert(rs.row(0, row) && (int32_t)row.current == 100);
	/* unsigned: the negative ones are the largest */
	rs.sort(RS::ColValue, true, RS::DispUnsigned);
	assert(rs.row(0, row) && (int32_t)row.current == -3);
}

void addedAddressesCanBeOutOfRange()
{
	std::vector<uint8_t> mem(16, 9);
	RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
	rs.start(2, false, false, false);
	const uint64_t add[] = { 4, 15, 400 };
	rs.addAddresses(add, 3, false);
	assert(rs.count() == 3 && rs.outOfRangeCount() == 2);
	RS::Row row;
	assert(rs.row(1, row) && row.address == 15 && row.current == 0);
	rs.removeOutOfRange();
	assert(rs.count() == 1 && rs.outOfRangeCount() == 0);
	const uint64_t more[] = { 8 };
	rs.addAddresses(more, 1, true);
	assert(rs.count() == 2 && rs.row(1, row) && row.address == 8 && row.previous == 0x0909);
}

/* Through the ABI, on a domain four times the size the frontend used to
 * refuse: what a first search and a narrowing one cost. */
void pastTheOldLimit()
{
	const int64_t size = int64_t{ 256 } << 20;
	std::vector<uint8_t> mem((size_t)size);
	std::mt19937_64 rng(89);
	for (int64_t i = 0; i + 8 <= size; i += 8) { uint64_t v = rng(); std::memcpy(mem.data() + i, &v, 8); }

	auto ms = [](auto a, auto b) { return (long long)std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count(); };
	auto t0 = std::chrono::steady_clock::now();
	ce_ramsearch *rs = ce_ramsearch_create(mem.data(), nullptr, nullptr, size);
	assert(rs && ce_ramsearch_start(rs, 1, 0, 0, 0) == 0);
	assert(ce_ramsearch_count(rs) == size);
	auto t1 = std::chrono::steady_clock::now();

	/* one byte in a thousand moves; "not equal to previous" must find exactly those */
	int64_t moved = 0;
	for (int64_t i = 500; i < size; i += 1000) { mem[(size_t)i] ^= 0x5a; moved++; }
	int64_t removed = ce_ramsearch_search(rs, 0, 5, 0, 0, 0, 1);
	auto t2 = std::chrono::steady_clock::now();
	assert(removed == size - moved && ce_ramsearch_count(rs) == moved);

	uint64_t address; uint32_t current, previous, changes;
	assert(ce_ramsearch_row(rs, 0, &address, &current, &previous, &changes) == 1 && address == 500);
	assert(ce_ramsearch_row(rs, moved - 1, &address, &current, &previous, &changes) == 1);
	assert(address == (uint64_t)(500 + (moved - 1) * 1000) && current == mem[(size_t)address]);
	assert(ce_ramsearch_index_of(rs, 1500) == 1 && ce_ramsearch_index_of(rs, 1501) == -1);

	/* and the first search can be taken back, whole */
	assert(ce_ramsearch_can_undo(rs) == 1 && ce_ramsearch_undo(rs) == size - moved);
	assert(ce_ramsearch_count(rs) == size);
	assert(ce_ramsearch_row(rs, size - 1, &address, &current, &previous, &changes) == 1 && address == (uint64_t)size - 1);
	ce_ramsearch_destroy(rs);

	std::printf("256 MiB by bytes: start %lld ms, first search %lld ms\n", ms(t0, t1), ms(t1, t2));
}
} // namespace

int main()
{
	bool dense = false, sparse = false;
	uint32_t seed = 1;
	for (int pointer = 0; pointer < 2; pointer++)
		for (int size : { 1, 2, 4 })
			for (int misaligned = 0; misaligned < 2; misaligned++)
				for (int detailed = 0; detailed < 2; detailed++)
				{
					Harness h;
					script(pointer != 0, size, misaligned != 0, (seed & 1) != 0, detailed != 0, seed, h);
					dense |= h.sawDense;
					sparse |= h.sawSparse;
					seed++;
				}
	assert(dense && sparse);

	sortsByWhatIsShown();
	addedAddressesCanBeOutOfRange();
	pastTheOldLimit();
	std::printf("test_ram_search: ok\n");
	return 0;
}
