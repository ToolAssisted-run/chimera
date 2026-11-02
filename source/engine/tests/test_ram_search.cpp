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
 * The same script runs again over a domain that is mostly nothing, told
 * which ranges of it are live (chimera#218): the model then holds a record
 * only where a whole candidate is live, and must still agree on every row -
 * with the real address of each.
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
#include <new>
#include <cstdlib>
#include <random>
#include <vector>

using RS = CeRamSearch;

/* An allocation that can be made to fail, the n-th from when it is armed:
 * this test's whole program allocates through it. */
static long g_failIn = -1;
void *operator new(std::size_t n)
{
	if (g_failIn == 0)
	{
		g_failIn = -1;
		throw std::bad_alloc();
	}
	if (g_failIn > 0) g_failIn--;
	void *p = std::malloc(n != 0 ? n : 1);
	if (p == nullptr) throw std::bad_alloc();
	return p;
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }

namespace
{
struct Rec
{
	uint64_t addr;
	uint64_t prev, cur;
	uint32_t changes;
};

struct Model
{
	std::vector<uint8_t> *mem;
	int size = 1;
	bool misaligned = false, bigEndian = false, detailed = false;
	std::vector<Rec> list;
	std::vector<std::vector<Rec>> undo, redo;
	/* live ranges, as the search keeps them: a byte each, whole eights */
	std::vector<uint8_t> liveMap;

	void setLive(const std::vector<int64_t> &pairs)
	{
		liveMap.assign(mem->size(), 0);
		for (size_t i = 0; i + 1 < pairs.size(); i += 2)
		{
			const int64_t from = std::max<int64_t>(pairs[i], 0) & ~int64_t{ 7 };
			const int64_t to = std::min<int64_t>((int64_t)mem->size(), (pairs[i] + pairs[i + 1] + 7) & ~int64_t{ 7 });
			for (int64_t a = from; a < to; a++) liveMap[(size_t)a] = 1;
		}
	}
	/* every byte of the candidate live: then they are one range's, since a gap is not */
	bool live(uint64_t a, int sz) const
	{
		if (liveMap.empty()) return true;
		for (int i = 0; i < sz; i++)
			if (a + (uint64_t)i >= liveMap.size() || !liveMap[a + (uint64_t)i]) return false;
		return true;
	}

	uint64_t peek(uint64_t a, int sz) const
	{
		if (a + (uint64_t)sz > mem->size()) return 0;
		uint64_t v = 0;
		for (int i = 0; i < sz; i++)
			v |= (uint64_t)(*mem)[a + (uint64_t)i] << 8 * (bigEndian ? sz - 1 - i : i);
		return v;
	}
	uint64_t value(const Rec &r) const { return detailed ? r.cur : peek(r.addr, size); }

	void start(int sz, bool mis, bool be, bool det)
	{
		size = sz; misaligned = mis; bigEndian = be; detailed = det;
		undo.clear(); redo.clear(); list.clear();
		const int step = mis ? 1 : sz;
		for (uint64_t a = 0; a + (uint64_t)sz <= mem->size(); a += (uint64_t)step)
			if (live(a, sz)) list.push_back({ a, peek(a, sz), peek(a, sz), 0 });
	}
	void record()
	{
		redo.clear();
		undo.push_back(list);
		if (undo.size() > (size_t)RS::MaxUndoLevels) undo.erase(undo.begin());
	}
	static int64_t sx(uint64_t v, int sz, int display)
	{
		if (display != RS::DispSigned) return (int64_t)v;
		return sz == 1 ? (int64_t)(int8_t)v : sz == 2 ? (int64_t)(int16_t)v : sz == 4 ? (int64_t)(int32_t)v : (int64_t)v;
	}
	static float f32(uint64_t v) { const uint32_t b = (uint32_t)v; float f; std::memcpy(&f, &b, 4); return f; }
	static double f64(uint64_t v) { double d; std::memcpy(&d, &v, 8); return d; }
	static bool near(float a, float b) { return std::fabs(b - a) < 5.96046448E-08f; }
	static bool near(double a, double b) { return a == b || std::fabs(b - a) <= 1e-9 * std::max(std::fabs(a), std::fabs(b)); }
	template <typename T> static bool realKeep(int op, T a, T b, T by)
	{
		switch (op)
		{
		case RS::OpEqual: return near(a, b);
		case RS::OpNotEqual: return !near(a, b);
		case RS::OpGreaterThan: return a > b;
		case RS::OpGreaterThanEqual: return a > b || near(a, b);
		case RS::OpLessThan: return a < b;
		case RS::OpLessThanEqual: return a < b || near(a, b);
		default: return near(std::fabs(a - b), by);
		}
	}
	bool keep(const Rec &r, const RS::Query &q) const
	{
		const uint64_t cur = value(r);
		bool isFloat = q.display == RS::DispFloat && (size == 4 || size == 8)
			&& (q.compare == RS::CmpPrevious || q.compare == RS::CmpSpecificValue || q.compare == RS::CmpDifference);
		if (isFloat && size == 8)
		{
			double a = f64(cur), b = f64(r.prev);
			if (q.compare == RS::CmpSpecificValue) b = f64(q.value);
			if (q.compare == RS::CmpDifference) { a = a - b; b = f64(q.value); }
			return realKeep(q.op, a, b, f64(q.differentBy));
		}
		if (isFloat)
		{
			float a = f32(cur), b = f32(r.prev);
			if (q.compare == RS::CmpSpecificValue) b = f32(q.value);
			if (q.compare == RS::CmpDifference) { a = a - b; b = f32(q.value); }
			return realKeep(q.op, a, b, f32(q.differentBy));
		}
		/* an 8-byte value shown unsigned is ordered as the unsigned number it is */
		if (size == 8 && q.display != RS::DispSigned && (q.compare == RS::CmpPrevious || q.compare == RS::CmpSpecificValue))
		{
			const uint64_t a = cur, b = q.compare == RS::CmpSpecificValue ? q.value : r.prev;
			switch (q.op)
			{
			case RS::OpEqual: return a == b;
			case RS::OpNotEqual: return a != b;
			case RS::OpGreaterThan: return a > b;
			case RS::OpGreaterThanEqual: return a >= b;
			case RS::OpLessThan: return a < b;
			case RS::OpLessThanEqual: return a <= b;
			default: return q.differentBy == (a > b ? a - b : b - a);
			}
		}
		int64_t a, b;
		switch (q.compare)
		{
		case RS::CmpSpecificValue: a = sx(cur, size, q.display); b = sx(q.value, size, q.display); break;
		case RS::CmpSpecificAddress: a = (int64_t)r.addr; b = q.value; break;
		case RS::CmpChanges: a = r.changes; b = q.value; break;
		case RS::CmpDifference:
			a = (int64_t)((uint64_t)sx(cur, size, q.display) - (uint64_t)sx(r.prev, size, q.display));
			b = (int64_t)q.value;
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
			const uint64_t now = peek(r.addr, size);
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
				if (a + (uint64_t)newSize > mem->size() || a % (uint64_t)step || !live(a, newSize)) continue;
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
				std::fprintf(stderr, "after %s: row %zu is %llx prev %llx cur %llx ch %u, model %llx prev %llx cur %llx ch %u\n",
					after, i, (unsigned long long)row.address, (unsigned long long)row.previous, (unsigned long long)row.current,
					row.changes, (unsigned long long)r.addr, (unsigned long long)r.prev, (unsigned long long)model.value(r), r.changes);
				assert(false);
			}
			if (i % 997 == 0) assert(rs->indexOf(r.addr) == (int64_t)i);
		}
		RS::Row none;
		assert(!rs->row((int64_t)model.list.size(), none));
	}
};

void script(bool throughPointer, int size, bool misaligned, bool bigEndian, bool detailed, uint32_t seed, Harness &h, bool ranged = false)
{
	std::mt19937 rng(seed);
	h.mem.resize(256 * 1024 + 3); /* a tail no aligned slot covers */
	for (auto &b : h.mem) b = (uint8_t)(rng() % 7); /* few values: searches thin it slowly */
	h.model.mem = &h.mem;

	RS rs(throughPointer ? h.mem.data() : nullptr, throughPointer ? nullptr : Harness::readFn, &h.mem, (int64_t)h.mem.size());
	h.rs = &rs;
	if (ranged)
	{
		/* a third of it live, in pieces of any length at any address, out of
		 * order, two of them touching, one running off the end */
		std::vector<int64_t> pairs;
		for (int i = 0; i < 9; i++)
		{
			pairs.push_back((int64_t)(rng() % h.mem.size()));
			pairs.push_back((int64_t)(1 + rng() % 20000));
		}
		pairs.push_back(pairs[0] + pairs[1]);
		pairs.push_back(777);
		pairs.push_back((int64_t)h.mem.size() - 5);
		pairs.push_back(4000);
		rs.setRanges(pairs.data(), (int64_t)pairs.size() / 2);
		h.model.setLive(pairs);
	}
	rs.start(size, misaligned, bigEndian, detailed);
	if (ranged)
	{
		int64_t liveBytes = 0;
		for (uint8_t b : h.model.liveMap) liveBytes += b;
		assert(rs.searchedSize() == liveBytes && liveBytes < (int64_t)h.mem.size() / 2);
	}
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
			q.display = (int)(rng() % (size == 4 || size == 8 ? 3 : 2));
			q.value = q.compare == RS::CmpSpecificAddress ? (uint64_t)(rng() % h.mem.size()) : (uint64_t)(rng() % 7);
			if (q.compare == RS::CmpDifference && rng() % 2) q.value = (uint64_t)-(int64_t)(rng() % 4);
			q.differentBy = (uint64_t)(rng() % 3);
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
	const int other = size == 4 ? 8 : size == 8 ? 2 : 4;
	rs.convertTo(other);
	h.model.convertTo(other);
	h.agree("convert");
	assert(!rs.canUndo());
}

/* 8-byte candidates (issue 474): a double found by its value, as Flash
 * games keep a position (440.0 is 407B800000000000), a difference of
 * doubles, an unsigned value past 2^63 ordered as unsigned, a signed one
 * below zero, and the byte order */
void eightBytes()
{
	std::vector<uint8_t> mem(64, 0);
	auto put = [&](int at, uint64_t v, bool big) {
		for (int k = 0; k < 8; k++) mem[(size_t)(at + k)] = (uint8_t)(v >> (8 * (big ? 7 - k : k)));
	};
	auto bits = [](double d) { uint64_t v; std::memcpy(&v, &d, 8); return v; };
	put(0, bits(440.0), false);
	put(8, 0xF000000000000000ull, false);
	put(16, (uint64_t)-5, false);
	put(24, bits(0.1), false);
	auto search = [&](RS &rs, int compare, int op, int display, uint64_t value) {
		RS::Query q;
		q.compare = compare;
		q.op = op;
		q.display = display;
		q.value = value;
		rs.search(q, RS::PrevLastSearch);
		std::vector<uint64_t> found;
		RS::Row row;
		for (int64_t i = 0; rs.row(i, row); i++) found.push_back(row.address);
		return found;
	};
	{
		RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
		rs.start(8, false, false, false);
		assert(rs.count() == 8);
		RS::Row row;
		assert(rs.row(0, row) && row.current == 0x407B800000000000ull);
		assert(search(rs, RS::CmpSpecificValue, RS::OpEqual, RS::DispFloat, bits(440.0)) == std::vector<uint64_t>{ 0 });
	}
	{
		RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
		rs.start(8, false, false, false);
		/* unsigned: past 2^63 is the largest; signed: -5 and that one are below zero */
		assert(search(rs, RS::CmpSpecificValue, RS::OpGreaterThan, RS::DispUnsigned, 0x8000000000000000ull)
			== (std::vector<uint64_t>{ 8, 16 }));
		rs.start(8, false, false, false);
		assert(search(rs, RS::CmpSpecificValue, RS::OpLessThan, RS::DispSigned, 0) == (std::vector<uint64_t>{ 8, 16 }));
		rs.start(8, false, false, false);
		assert(search(rs, RS::CmpSpecificValue, RS::OpEqual, RS::DispSigned, (uint64_t)-5) == std::vector<uint64_t>{ 16 });
	}
	{
		/* a double that moved by 0.2: 0.1 to 0.3 is 0.2 as typed */
		RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
		rs.start(8, false, false, false);
		put(24, bits(0.3), false);
		RS::Query q;
		q.compare = RS::CmpDifference;
		q.op = RS::OpEqual;
		q.display = RS::DispFloat;
		q.value = bits(0.2);
		rs.search(q, RS::PrevLastSearch);
		RS::Row row;
		assert(rs.count() == 1 && rs.row(0, row) && row.address == 24);
		put(24, bits(0.1), false);
	}
	{
		/* the whole domain at once, eight bytes at a time: a value whose top
		 * byte alone changed has changed */
		std::vector<uint8_t> wide(64 * 1024, 0);
		RS rs(wide.data(), nullptr, nullptr, (int64_t)wide.size());
		rs.start(8, false, false, false);
		assert(rs.isDense());
		wide[8 * 100 + 7] = 1;
		assert(search(rs, RS::CmpPrevious, RS::OpNotEqual, RS::DispUnsigned, 0) == std::vector<uint64_t>{ 800 });
	}
	{
		/* big-endian: the same double, its bytes the other way round */
		std::vector<uint8_t> big(16, 0);
		const uint64_t v = bits(440.0);
		for (int k = 0; k < 8; k++) big[(size_t)k] = (uint8_t)(v >> (8 * (7 - k)));
		RS rs(big.data(), nullptr, nullptr, (int64_t)big.size());
		rs.start(8, false, true, false);
		RS::Row row;
		assert(rs.row(0, row) && row.current == v);
		/* switched while sparse, the value turns round with it */
		rs.removeIndices(std::vector<int64_t>{ 1 }.data(), 1);
		assert(!rs.isDense());
		rs.setBigEndian(false);
		assert(rs.row(0, row) && row.previous == 0x0000000000807B40ull);
	}
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

/* Live ranges at the edges of what they mean: an address outside them is not
 * found, removed or added; a candidate that would span two is not one; no
 * range at all is nothing to search; the whole domain as one range, and no
 * word about ranges, are the search it always was. */
void liveRangesAtTheirEdges()
{
	std::vector<uint8_t> mem(4096);
	for (size_t i = 0; i < mem.size(); i++) mem[i] = (uint8_t)(i * 13 + 1);
	RS rs(nullptr, Harness::readFn, &mem, (int64_t)mem.size());

	const int64_t two[] = { 1024, 16, 256, 8 }; /* out of order: 256..263 and 1024..1039 */
	rs.setRanges(two, 2);
	rs.start(4, true, false, false);
	/* 4-byte candidates at every byte: 5 in the first range, 13 in the second, none across */
	assert(rs.searchedSize() == 24 && rs.count() == 5 + 13);
	RS::Row row;
	uint32_t first;
	std::memcpy(&first, mem.data() + 256, 4);
	assert(rs.row(0, row) && row.address == 256 && row.current == first && row.previous == first);
	assert(rs.row(4, row) && row.address == 260);
	assert(rs.row(5, row) && row.address == 1024 && rs.row(17, row) && row.address == 1036);
	assert(rs.indexOf(1024) == 5 && rs.indexOf(261) == -1 && rs.indexOf(0) == -1 && rs.indexOf(1040) == -1);

	/* by address: the real one */
	RS::Query q;
	q.compare = RS::CmpSpecificAddress;
	q.op = RS::OpGreaterThanEqual;
	q.value = 1030;
	assert(rs.search(q, RS::PrevLastSearch) == 5 + 6 && rs.count() == 7);
	assert(rs.row(0, row) && row.address == 1030);

	/* the address lists: what is outside is not memory this search has */
	const uint64_t gone[] = { 1031, 300, 5000 };
	rs.removeAddresses(gone, 3, false);
	assert(rs.count() == 6 && rs.indexOf(1031) == -1 && rs.indexOf(1032) == 1);
	const uint64_t add[] = { 256, 261, 300, 1038 }; /* in; across the end of its range; outside; across the end */
	rs.addAddresses(add, 4, true);
	assert(rs.count() == 7 && rs.row(6, row) && row.address == 256);

	/* a conversion keeps to the ranges too */
	rs.start(1, false, false, false);
	assert(rs.count() == 24);
	rs.convertTo(4);
	assert(rs.count() == 2 + 4 && rs.row(1, row) && row.address == 260 && rs.row(2, row) && row.address == 1024);

	/* nothing live: nothing to search, and the next word about ranges is taken at the next start */
	rs.setRanges(two, 0);
	assert(rs.count() == 6);
	rs.start(1, false, false, false);
	assert(rs.count() == 0 && rs.searchedSize() == 0 && !rs.row(0, row));

	/* all of it, said in one range or not said: every byte */
	const int64_t all[] = { 0, 4096 };
	rs.setRanges(all, 1);
	rs.start(1, false, false, false);
	assert(rs.count() == 4096 && rs.searchedSize() == 4096);
	rs.setRanges(nullptr, -1);
	rs.start(2, false, false, false);
	assert(rs.count() == 2048 && rs.row(2047, row) && row.address == 4094);

	/* through the ABI */
	ce_ramsearch *abi = ce_ramsearch_create(mem.data(), nullptr, nullptr, (int64_t)mem.size());
	assert(abi && ce_ramsearch_set_ranges(abi, two, 2) == 0 && ce_ramsearch_start(abi, 1, 0, 0, 0) == 0);
	assert(ce_ramsearch_count(abi) == 24 && ce_ramsearch_searched_size(abi) == 24);
	uint64_t address; uint32_t current, previous, changes;
	assert(ce_ramsearch_row(abi, 8, &address, &current, &previous, &changes) == 1 && address == 1024 && current == mem[1024]);
	ce_ramsearch_destroy(abi);
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

/* Running out of memory in the middle of a search leaves it as it was: the
 * window says "Not enough memory for that search", and that must be true. A
 * search that is left small changes shape (makeSparse), which allocates after
 * the candidates have been filtered; it used to throw from there with the
 * filtering done. Every allocation of the search is made to fail in turn. */
void runningOutLeavesTheSearchAsItWas()
{
	std::vector<uint8_t> mem(1 << 16);
	for (size_t i = 0; i < mem.size(); i++) mem[i] = static_cast<uint8_t>(i * 7);
	int failed = 0, survived = 0;
	for (long k = 0;; k++)
	{
		RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
		rs.start(1, false, false, false);
		for (size_t a = 100; a < 110; a++) mem[a]++;
		RS::Query q;
		q.compare = RS::CmpPrevious;
		q.op = RS::OpNotEqual;
		RS::Row first;
		assert(rs.row(0, first));
		g_failIn = k;
		bool threw = false;
		try
		{
			rs.search(q, RS::PrevLastSearch);
		}
		catch (const std::bad_alloc &)
		{
			threw = true;
		}
		const bool armedPast = g_failIn > 0; /* no allocation was the k-th */
		g_failIn = -1;
		for (size_t a = 100; a < 110; a++) mem[a]--;
		if (threw)
		{
			failed++;
			RS::Row row;
			assert(rs.count() == (int64_t)mem.size());
			assert(rs.row(0, row) && row.address == first.address && row.previous == first.previous);
		}
		else
		{
			survived++;
			assert(rs.count() == 10);
		}
		if (armedPast) break;
	}
	assert(survived > 0);
	std::printf("ram search: out of memory at each of %d allocations, %d refused and left as it was\n", failed + survived - 1, failed);
}

/* Undo keeps to its budget: the oldest level goes first, and a set larger
 * than all of it is not kept (user-decided, 2026-10-18). */
void undoKeepsToItsBudget()
{
	std::vector<uint8_t> mem(65536, 0);
	RS rs(mem.data(), nullptr, nullptr, (int64_t)mem.size());
	rs.start(1, false, false, false);   /* a level is the image and the bits: 72 KiB and a little */
	rs.setUndoBudget(200 * 1024);
	RS::Query q;
	q.compare = RS::CmpSpecificAddress;
	q.op = RS::OpNotEqual;
	for (uint32_t a = 0; a < 5; a++)
	{
		q.value = a;
		assert(rs.search(q, RS::PrevLastSearch) == 1);
	}
	int levels = 0;
	while (rs.canUndo()) { rs.undo(); levels++; }
	assert(levels == 2);                /* two fit in 200 KiB, not five */
	rs.setUndoBudget(10 * 1024);
	q.value = 100;
	rs.search(q, RS::PrevLastSearch);
	assert(!rs.canUndo());              /* larger than the whole budget: not kept */
}

/* A detailed frame walks only the blocks whose bytes changed (engine
 * analysis, 2026-10-18), and still counts every change. */
void aDetailedFrameWalksOnlyWhatChanged()
{
	std::vector<uint8_t> mem(1 << 20, 7);
	for (int pointer = 0; pointer < 2; pointer++)
	{
		RS rs(pointer ? mem.data() : nullptr, pointer ? nullptr : Harness::readFn, &mem, (int64_t)mem.size());
		rs.start(2, false, false, true);
		rs.update(RS::PrevLastFrame);
		assert(rs.walkedLastUpdate() == 0);
		mem[300001]++;
		rs.update(RS::PrevLastFrame);
		assert(rs.walkedLastUpdate() > 0 && rs.walkedLastUpdate() <= 4096 / 2);
		RS::Row row;
		assert(rs.row(rs.indexOf(300000), row) && row.changes == 1 && row.previous == 0x0707 && row.current == 0x0807);
		/* a candidate across two blocks is seen from the first */
		mem[4096]++;
		rs.update(RS::PrevLastFrame);
		assert(rs.row(rs.indexOf(4096), row) && row.changes == 1);
		mem[300001]--;
		mem[4096]--;
	}
}

/* On a bus, a sparse set is read a run of candidates at a time, not one call
 * per candidate (engine analysis, 2026-10-18). */
static int g_reads = 0;
static int64_t countedRead(void *user, int64_t offset, uint8_t *buf, int64_t len)
{
	g_reads++;
	return Harness::readFn(user, offset, buf, len);
}
void aBusIsReadAWindowAtATime()
{
	std::vector<uint8_t> mem(1 << 20);
	for (size_t i = 0; i < mem.size(); i++) mem[i] = (uint8_t)(i * 7);
	RS rs(nullptr, countedRead, &mem, (int64_t)mem.size());
	rs.start(4, false, false, false);
	RS::Query q;
	q.compare = RS::CmpSpecificAddress;
	q.op = RS::OpLessThan;
	q.value = 400;                      /* 100 candidates, close together */
	rs.search(q, RS::PrevOriginal);
	assert(rs.count() == 100 && !rs.isDense());
	g_reads = 0;
	q.compare = RS::CmpPrevious;
	q.op = RS::OpEqual;
	assert(rs.search(q, RS::PrevLastSearch) == 0);
	assert(g_reads <= 2);               /* the values, then the previous ones: was 200 */
	mem[40] ^= 1;
	g_reads = 0;
	assert(rs.search(q, RS::PrevLastSearch) == 1);
	assert(g_reads <= 2);
}

/* Switching the byte order of a sparse search keeps previous and current
 * meaning the same bytes: an unchanged memory still equals its previous
 * (2026-10-18). Sparse values were left decoded in the old order. */
void aByteOrderSwitchKeepsSparseValues()
{
	std::vector<uint8_t> mem(4096);
	for (size_t i = 0; i < mem.size(); i++) mem[i] = (uint8_t)(i * 31 + 7);
	for (int pointer = 0; pointer < 2; pointer++)
	{
		RS rs(pointer ? mem.data() : nullptr, pointer ? nullptr : Harness::readFn, &mem, (int64_t)mem.size());
		rs.start(2, false, false, false);
		RS::Query q;
		q.compare = RS::CmpSpecificAddress;
		q.op = RS::OpLessThan;
		q.value = 64;                       /* 32 candidates: sparse */
		rs.search(q, RS::PrevOriginal);
		assert(!rs.isDense() && rs.count() == 32);
		rs.setBigEndian(true);
		RS::Row row;
		assert(rs.row(1, row) && row.address == 2 && row.previous == row.current
			&& row.current == (uint32_t)(mem[2] << 8 | mem[3]));
		q.compare = RS::CmpPrevious;
		q.op = RS::OpEqual;
		assert(rs.search(q, RS::PrevLastSearch) == 0 && rs.count() == 32);
	}
}

int main()
{
	runningOutLeavesTheSearchAsItWas();
	aByteOrderSwitchKeepsSparseValues();
	undoKeepsToItsBudget();
	aDetailedFrameWalksOnlyWhatChanged();
	aBusIsReadAWindowAtATime();
	bool dense = false, sparse = false;
	uint32_t seed = 1;
	for (int pointer = 0; pointer < 2; pointer++)
		for (int size : { 1, 2, 4, 8 })
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

	/* the same, over the live third of the domain */
	dense = sparse = false;
	for (int pointer = 0; pointer < 2; pointer++)
		for (int size : { 1, 2, 4, 8 })
			for (int misaligned = 0; misaligned < 2; misaligned++)
				for (int detailed = 0; detailed < 2; detailed++)
				{
					Harness h;
					script(pointer != 0, size, misaligned != 0, (seed & 1) != 0, detailed != 0, seed, h, true);
					dense |= h.sawDense;
					sparse |= h.sawSparse;
					seed++;
				}
	assert(dense && sparse);

	eightBytes();
	sortsByWhatIsShown();
	addedAddressesCanBeOutOfRange();
	liveRangesAtTheirEdges();
	pastTheOldLimit();
	std::printf("test_ram_search: ok\n");
	return 0;
}
