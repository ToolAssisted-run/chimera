/* ram_search.cpp - see ram_search.hpp for the two shapes of the candidate set. */

#include "ram_search.hpp"

#include "chimera/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <unordered_set>

namespace
{
inline int popcount64(uint64_t v) { return __builtin_popcountll(v); }
inline int ctz64(uint64_t v) { return __builtin_ctzll(v); }

inline float asFloat(uint32_t v)
{
	float f;
	std::memcpy(&f, &v, sizeof f);
	return f;
}

/* The frontend's ApproxFloatEquality: within 2^-24. A NaN equals nothing. */
inline bool approx(float a, float b) { return std::fabs(b - a) < 5.96046448E-08f; }

inline int64_t signExtend(uint32_t v, int size, int display)
{
	if (display != CeRamSearch::DispSigned) return v;
	switch (size)
	{
	case 1: return (int8_t)v;
	case 2: return (int16_t)v;
	default: return (int32_t)v;
	}
}

template <typename T> inline bool ordered(int op, T a, T b)
{
	switch (op)
	{
	case CeRamSearch::OpGreaterThan: return a > b;
	case CeRamSearch::OpGreaterThanEqual: return a >= b;
	case CeRamSearch::OpLessThan: return a < b;
	case CeRamSearch::OpLessThanEqual: return a <= b;
	case CeRamSearch::OpNotEqual: return a != b;
	default: return a == b;
	}
}

inline bool compareInts(const CeRamSearch::Query &q, int64_t a, int64_t b)
{
	if (q.op == CeRamSearch::OpDifferentBy) return (int64_t)q.differentBy == std::llabs(a - b);
	return ordered(q.op, a, b);
}

inline bool compareFloats(const CeRamSearch::Query &q, float a, float b)
{
	switch (q.op)
	{
	case CeRamSearch::OpGreaterThan: return a > b;
	case CeRamSearch::OpGreaterThanEqual: return a > b || approx(a, b);
	case CeRamSearch::OpLessThan: return a < b;
	case CeRamSearch::OpLessThanEqual: return a < b || approx(a, b);
	case CeRamSearch::OpNotEqual: return !approx(a, b);
	case CeRamSearch::OpDifferentBy: return approx(std::fabs(a - b), asFloat(q.differentBy));
	default: return approx(a, b);
	}
}
} // namespace

CeRamSearch::CeRamSearch(const uint8_t *base, CeRamSearchReadFn fn, void *user, int64_t domainSize)
	: m_base(base), m_fn(fn), m_user(user), m_domainSize(domainSize < 0 ? 0 : domainSize)
{
	m_set.dense = false;
}

/* ---- memory ---- */

void CeRamSearch::readInto(int64_t offset, uint8_t *buf, int64_t len) const
{
	if (len <= 0) return;
	int64_t got = 0;
	if (offset >= 0 && offset < m_domainSize)
	{
		int64_t want = std::min(len, m_domainSize - offset);
		if (m_base)
		{
			std::memcpy(buf, m_base + offset, (size_t)want);
			got = want;
		}
		else if (m_fn)
		{
			got = m_fn(m_user, offset, buf, want);
			if (got < 0) got = 0;
			if (got > want) got = want;
		}
	}
	if (got < len) std::memset(buf + got, 0, (size_t)(len - got));
}

const uint8_t *CeRamSearch::window(int64_t offset, int64_t len) const
{
	if (m_base) return m_base + offset;
	if ((int64_t)m_scratch.size() < len) m_scratch.resize((size_t)len);
	readInto(offset, m_scratch.data(), len);
	return m_scratch.data();
}

uint32_t CeRamSearch::load(const uint8_t *p, int size) const
{
	switch (size)
	{
	case 1: return p[0];
	case 2: return m_bigEndian ? (uint32_t)(p[0] << 8 | p[1]) : (uint32_t)(p[1] << 8 | p[0]);
	default:
		return m_bigEndian
			? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3])
			: ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]);
	}
}

/* A value that does not fit inside the domain reads as zero, as it always has. */
uint32_t CeRamSearch::peek(uint64_t address, int size) const
{
	if (address > (uint64_t)m_domainSize || (uint64_t)m_domainSize - address < (uint64_t)size) return 0;
	if (m_base) return load(m_base + address, size);
	uint8_t b[4];
	readInto((int64_t)address, b, size);
	return load(b, size);
}

void CeRamSearch::snapshotImage(std::vector<uint8_t> &image) const
{
	image.resize((size_t)m_domainSize);
	for (int64_t at = 0; at < m_domainSize; at += Chunk)
		readInto(at, image.data() + at, std::min(Chunk, m_domainSize - at));
}

/* ---- the predicate ---- */

bool CeRamSearch::matches(const Query &q, int size, uint64_t address, uint32_t cur, uint32_t prev, uint32_t changes)
{
	const bool isFloat = q.display == DispFloat;
	switch (q.compare)
	{
	case CmpSpecificValue:
		if (isFloat) return compareFloats(q, asFloat(cur), asFloat(q.value));
		return compareInts(q, signExtend(cur, size, q.display), signExtend(q.value, size, q.display));
	case CmpSpecificAddress:
		return compareInts(q, (int64_t)address, (int64_t)q.value);
	case CmpChanges:
		return compareInts(q, (int64_t)changes, (int64_t)q.value);
	case CmpDifference:
		if (isFloat) return compareFloats(q, asFloat(cur) - asFloat(prev), asFloat(q.value));
		/* A difference is as wide as it needs to be whatever the watch size: a
		 * byte can fall by 200. The box hands over a full-width int. */
		return compareInts(q,
			signExtend(cur, size, q.display) - signExtend(prev, size, q.display),
			q.display == DispSigned ? (int64_t)(int32_t)q.value : (int64_t)q.value);
	default:
		if (isFloat) return compareFloats(q, asFloat(cur), asFloat(prev));
		return compareInts(q, signExtend(cur, size, q.display), signExtend(prev, size, q.display));
	}
}

/* ---- the dense shape ---- */

void CeRamSearch::rebuildRank()
{
	const uint64_t words = m_set.bits.size();
	m_set.rank.assign((size_t)((words + RankWords - 1) / RankWords + 1), 0);
	uint64_t running = 0;
	for (uint64_t w = 0; w < words; w++)
	{
		if (w % RankWords == 0) m_set.rank[(size_t)(w / RankWords)] = running;
		running += (uint64_t)popcount64(m_set.bits[(size_t)w]);
	}
	m_set.rank.back() = running;
	m_set.count = running;
}

uint64_t CeRamSearch::slotAt(int64_t index) const
{
	uint64_t want = m_set.reversed ? m_set.count - 1 - (uint64_t)index : (uint64_t)index;
	/* the last block whose running count is <= want */
	size_t block = (size_t)(std::upper_bound(m_set.rank.begin(), m_set.rank.end() - 1, want) - m_set.rank.begin()) - 1;
	uint64_t seen = m_set.rank[block];
	for (uint64_t w = block * RankWords; w < m_set.bits.size(); w++)
	{
		uint64_t word = m_set.bits[(size_t)w];
		uint64_t n = (uint64_t)popcount64(word);
		if (seen + n <= want) { seen += n; continue; }
		for (uint64_t skip = want - seen; skip > 0; skip--) word &= word - 1;
		return w * 64 + (uint64_t)ctz64(word);
	}
	return 0;
}

/* Walks the live slots a chunk of the domain at a time, so a domain with no
 * pointer is read in a few large pieces rather than a value at a time. f gets
 * (slot, address, memory at that address) and answers whether the slot stays. */
template <typename F> void CeRamSearch::forEachDense(bool needMemory, F f)
{
	const int size = m_set.size, step = m_set.step;
	for (int64_t chunkStart = 0; chunkStart < m_domainSize; chunkStart += Chunk)
	{
		const int64_t chunkEnd = std::min(chunkStart + Chunk, m_domainSize);
		const uint64_t firstSlot = (uint64_t)chunkStart / (uint64_t)step;
		const uint64_t endSlot = std::min(m_set.slots, (uint64_t)(chunkEnd + step - 1) / (uint64_t)step);
		if (firstSlot >= endSlot) break;
		const uint64_t firstWord = firstSlot / 64, endWord = (endSlot + 63) / 64;
		bool any = false;
		for (uint64_t w = firstWord; w < endWord && !any; w++) any = m_set.bits[(size_t)w] != 0;
		if (!any) continue;

		const int64_t windowLen = std::min(chunkEnd + size - 1, m_domainSize) - chunkStart;
		const uint8_t *mem = needMemory ? window(chunkStart, windowLen) : nullptr;
		for (uint64_t w = firstWord; w < endWord; w++)
		{
			uint64_t word = m_set.bits[(size_t)w];
			while (word)
			{
				const int bit = ctz64(word);
				word &= word - 1;
				const uint64_t slot = w * 64 + (uint64_t)bit;
				const uint64_t address = slot * (uint64_t)step;
				if (!f(slot, address, mem ? mem + (address - (uint64_t)chunkStart) : nullptr))
					m_set.bits[(size_t)w] &= ~(uint64_t{ 1 } << bit);
			}
		}
	}
}

bool CeRamSearch::mustBeSparse() const
{
	return m_detailed && m_previousType == PrevLastChange && m_misaligned && m_set.size > 1;
}

void CeRamSearch::makeSparse()
{
	if (!m_set.dense) return;
	Set s;
	s.dense = false;
	s.size = m_set.size;
	s.step = m_set.step;
	s.count = m_set.count;
	s.addr.reserve((size_t)m_set.count);
	s.prev.reserve((size_t)m_set.count);
	if (m_detailed)
	{
		s.cur.reserve((size_t)m_set.count);
		s.changes.reserve((size_t)m_set.count);
	}
	forEachDense(false, [&](uint64_t slot, uint64_t address, const uint8_t *) {
		s.addr.push_back(address);
		s.prev.push_back(load(m_set.prevImage.data() + address, s.size));
		if (m_detailed)
		{
			s.cur.push_back(load(m_set.curImage.data() + address, s.size));
			s.changes.push_back(m_set.changesBySlot[(size_t)slot]);
		}
		return true;
	});
	if (m_set.reversed)
	{
		std::reverse(s.addr.begin(), s.addr.end());
		std::reverse(s.prev.begin(), s.prev.end());
		std::reverse(s.cur.begin(), s.cur.end());
		std::reverse(s.changes.begin(), s.changes.end());
	}
	m_set = std::move(s);
}

/* After anything that changed the candidates: recount, and take the shape that
 * suits what is left. */
void CeRamSearch::settle()
{
	if (!m_set.dense)
	{
		m_set.count = m_set.addr.size();
		return;
	}
	rebuildRank();
	if (mustBeSparse() || m_set.count <= 4096 || m_set.count <= m_set.slots / 32) makeSparse();
}

void CeRamSearch::recordUndo()
{
	if (!m_undoEnabled) return;
	try
	{
		m_redo.clear();
		m_undo.push_back(m_set);
		while ((int)m_undo.size() > MaxUndoLevels) m_undo.pop_front();
	}
	catch (const std::bad_alloc &)
	{
		/* history is a convenience; the search is not */
		clearHistory();
	}
}

/* ---- operations ---- */

void CeRamSearch::start(int size, bool misaligned, bool bigEndian, bool detailed)
{
	clearHistory();
	m_bigEndian = bigEndian;
	m_detailed = detailed;
	m_misaligned = misaligned;
	if (size != 1 && size != 2 && size != 4) size = 1;

	Set s;
	s.dense = true;
	s.size = size;
	s.step = misaligned ? 1 : size;
	s.slots = m_domainSize < size ? 0
		: misaligned ? (uint64_t)(m_domainSize - size + 1)
		: (uint64_t)(m_domainSize / size);
	s.bits.assign((size_t)((s.slots + 63) / 64), ~uint64_t{ 0 });
	if (s.slots % 64) s.bits.back() = (uint64_t{ 1 } << (s.slots % 64)) - 1;
	snapshotImage(s.prevImage);
	if (detailed)
	{
		s.curImage = s.prevImage;
		s.changesBySlot.assign((size_t)s.slots, 0);
	}
	m_set = std::move(s);
	settle();
}

bool CeRamSearch::row(int64_t index, Row &out) const
{
	if (index < 0 || (uint64_t)index >= m_set.count) return false;
	if (m_set.dense)
	{
		const uint64_t slot = slotAt(index);
		out.address = slot * (uint64_t)m_set.step;
		out.previous = load(m_set.prevImage.data() + out.address, m_set.size);
		out.current = m_detailed ? load(m_set.curImage.data() + out.address, m_set.size) : peek(out.address, m_set.size);
		out.changes = m_detailed ? m_set.changesBySlot[(size_t)slot] : 0;
		return true;
	}
	const size_t i = (size_t)index;
	out.address = m_set.addr[i];
	out.previous = m_set.prev[i];
	out.current = m_detailed ? m_set.cur[i] : peek(out.address, m_set.size);
	out.changes = m_detailed ? m_set.changes[i] : 0;
	return true;
}

int64_t CeRamSearch::indexOf(uint64_t address) const
{
	if (!m_set.dense)
	{
		auto it = std::find(m_set.addr.begin(), m_set.addr.end(), address);
		return it == m_set.addr.end() ? -1 : (int64_t)(it - m_set.addr.begin());
	}
	if (address % (uint64_t)m_set.step) return -1;
	const uint64_t slot = address / (uint64_t)m_set.step;
	if (slot >= m_set.slots) return -1;
	const uint64_t word = slot / 64, bit = slot % 64;
	if (!(m_set.bits[(size_t)word] >> bit & 1)) return -1;
	uint64_t before = m_set.rank[(size_t)(word / RankWords)];
	for (uint64_t w = word / RankWords * RankWords; w < word; w++) before += (uint64_t)popcount64(m_set.bits[(size_t)w]);
	before += (uint64_t)popcount64(m_set.bits[(size_t)word] & ((uint64_t{ 1 } << bit) - 1));
	return (int64_t)(m_set.reversed ? m_set.count - 1 - before : before);
}

int64_t CeRamSearch::search(const Query &q, int previousType)
{
	m_previousType = previousType;
	recordUndo();
	const uint64_t before = m_set.count;
	const int size = m_set.size;

	if (m_set.dense)
	{
		const bool needMemory = !m_detailed && q.compare != CmpSpecificAddress && q.compare != CmpChanges;
		forEachDense(needMemory, [&](uint64_t slot, uint64_t address, const uint8_t *mem) {
			const uint32_t prev = load(m_set.prevImage.data() + address, size);
			const uint32_t cur = m_detailed ? load(m_set.curImage.data() + address, size) : mem ? load(mem, size) : 0;
			return matches(q, size, address, cur, prev, m_detailed ? m_set.changesBySlot[(size_t)slot] : 0);
		});
		rebuildRank();
	}
	else
	{
		size_t kept = 0;
		for (size_t i = 0; i < m_set.addr.size(); i++)
		{
			const uint32_t cur = m_detailed ? m_set.cur[i] : peek(m_set.addr[i], size);
			if (!matches(q, size, m_set.addr[i], cur, m_set.prev[i], m_detailed ? m_set.changes[i] : 0)) continue;
			m_set.addr[kept] = m_set.addr[i];
			m_set.prev[kept] = m_set.prev[i];
			if (m_detailed)
			{
				m_set.cur[kept] = m_set.cur[i];
				m_set.changes[kept] = m_set.changes[i];
			}
			kept++;
		}
		m_set.addr.resize(kept);
		m_set.prev.resize(kept);
		if (m_detailed)
		{
			m_set.cur.resize(kept);
			m_set.changes.resize(kept);
		}
		m_set.count = kept;
	}

	/* shape first: what is left is usually far less to take a previous of */
	settle();
	if (previousType == PrevLastSearch) setPreviousToCurrent();
	return (int64_t)(before - m_set.count);
}

bool CeRamSearch::wouldRemove(int64_t index, const Query &q) const
{
	Row r;
	if (!row(index, r)) return false;
	return !matches(q, m_set.size, r.address, r.current, r.previous, r.changes);
}

void CeRamSearch::setPreviousToCurrent()
{
	if (m_set.dense)
	{
		if (m_detailed) m_set.prevImage = m_set.curImage;
		else snapshotImage(m_set.prevImage);
		return;
	}
	if (m_detailed) m_set.prev = m_set.cur;
	else
		for (size_t i = 0; i < m_set.addr.size(); i++) m_set.prev[i] = peek(m_set.addr[i], m_set.size);
}

void CeRamSearch::update(int previousType)
{
	if (!m_detailed) return;
	setPreviousType(previousType);
	const int size = m_set.size;
	if (m_set.dense)
	{
		forEachDense(true, [&](uint64_t slot, uint64_t address, const uint8_t *mem) {
			if (std::memcmp(mem, m_set.curImage.data() + address, (size_t)size) != 0)
			{
				m_set.changesBySlot[(size_t)slot]++;
				/* safe: never dense when candidates overlap (mustBeSparse) */
				if (previousType == PrevLastChange)
					std::memcpy(m_set.prevImage.data() + address, m_set.curImage.data() + address, (size_t)size);
			}
			return true;
		});
		/* the images move whole, after the walk: candidates may overlap */
		if (previousType == PrevLastFrame) std::swap(m_set.prevImage, m_set.curImage);
		snapshotImage(m_set.curImage);
		return;
	}
	for (size_t i = 0; i < m_set.addr.size(); i++)
	{
		const uint32_t now = peek(m_set.addr[i], size);
		if (now != m_set.cur[i])
		{
			m_set.changes[i]++;
			if (previousType == PrevLastChange) m_set.prev[i] = m_set.cur[i];
		}
		if (previousType == PrevLastFrame) m_set.prev[i] = m_set.cur[i];
		m_set.cur[i] = now;
	}
}

void CeRamSearch::setPreviousType(int previousType)
{
	m_previousType = previousType;
	if (m_set.dense && mustBeSparse()) makeSparse();
}

void CeRamSearch::clearChangeCounts()
{
	std::fill(m_set.changesBySlot.begin(), m_set.changesBySlot.end(), 0);
	std::fill(m_set.changes.begin(), m_set.changes.end(), 0);
}

void CeRamSearch::removeIndices(const int64_t *indices, int64_t n)
{
	recordUndo();
	if (m_set.dense)
	{
		std::vector<uint64_t> slots;
		slots.reserve((size_t)n);
		for (int64_t i = 0; i < n; i++)
			if (indices[i] >= 0 && (uint64_t)indices[i] < m_set.count) slots.push_back(slotAt(indices[i]));
		for (uint64_t slot : slots) m_set.bits[(size_t)(slot / 64)] &= ~(uint64_t{ 1 } << (slot % 64));
		settle();
		return;
	}
	std::vector<bool> gone(m_set.addr.size(), false);
	for (int64_t i = 0; i < n; i++)
		if (indices[i] >= 0 && (uint64_t)indices[i] < m_set.count) gone[(size_t)indices[i]] = true;
	size_t kept = 0;
	for (size_t i = 0; i < gone.size(); i++)
	{
		if (gone[i]) continue;
		m_set.addr[kept] = m_set.addr[i];
		m_set.prev[kept] = m_set.prev[i];
		if (m_detailed)
		{
			m_set.cur[kept] = m_set.cur[i];
			m_set.changes[kept] = m_set.changes[i];
		}
		kept++;
	}
	m_set.addr.resize(kept);
	m_set.prev.resize(kept);
	if (m_detailed)
	{
		m_set.cur.resize(kept);
		m_set.changes.resize(kept);
	}
	settle();
}

void CeRamSearch::removeAddresses(const uint64_t *addresses, int64_t n, bool withUndo)
{
	if (withUndo) recordUndo();
	if (m_set.dense)
	{
		for (int64_t i = 0; i < n; i++)
		{
			if (addresses[i] % (uint64_t)m_set.step) continue;
			const uint64_t slot = addresses[i] / (uint64_t)m_set.step;
			if (slot < m_set.slots) m_set.bits[(size_t)(slot / 64)] &= ~(uint64_t{ 1 } << (slot % 64));
		}
		settle();
		return;
	}
	std::unordered_set<uint64_t> gone(addresses, addresses + n);
	std::vector<int64_t> indices;
	for (size_t i = 0; i < m_set.addr.size(); i++)
		if (gone.count(m_set.addr[i])) indices.push_back((int64_t)i);
	const bool undo = m_undoEnabled;
	m_undoEnabled = false; /* recorded above, or not wanted */
	removeIndices(indices.data(), (int64_t)indices.size());
	m_undoEnabled = undo;
}

void CeRamSearch::addAddresses(const uint64_t *addresses, int64_t n, bool append)
{
	if (append) makeSparse();
	else
	{
		Set s;
		s.dense = false;
		s.size = m_set.size;
		s.step = m_set.step;
		m_set = std::move(s);
	}
	for (int64_t i = 0; i < n; i++)
	{
		const uint32_t now = peek(addresses[i], m_set.size);
		m_set.addr.push_back(addresses[i]);
		m_set.prev.push_back(now);
		if (m_detailed)
		{
			m_set.cur.push_back(now);
			m_set.changes.push_back(0);
		}
	}
	settle();
}

/* Every byte a candidate covered is offered to the new size; what fits the
 * domain and the alignment is a candidate afresh, its previous what memory
 * holds now. */
void CeRamSearch::convertTo(int size)
{
	if (size != 1 && size != 2 && size != 4) return;
	clearHistory();
	const int oldSize = m_set.size;
	const int span = m_misaligned ? 1 : oldSize;
	const int newStep = m_misaligned ? 1 : size;
	auto fits = [&](uint64_t a) {
		return m_domainSize >= size && a <= (uint64_t)(m_domainSize - size) && a % (uint64_t)newStep == 0;
	};

	Set s;
	s.size = size;
	s.step = newStep;
	if (m_set.dense)
	{
		s.dense = true;
		s.slots = m_domainSize < size ? 0
			: m_misaligned ? (uint64_t)(m_domainSize - size + 1)
			: (uint64_t)(m_domainSize / size);
		s.bits.assign((size_t)((s.slots + 63) / 64), 0);
		forEachDense(false, [&](uint64_t, uint64_t address, const uint8_t *) {
			for (int k = 0; k < span; k++)
				if (fits(address + (uint64_t)k))
				{
					const uint64_t slot = (address + (uint64_t)k) / (uint64_t)newStep;
					s.bits[(size_t)(slot / 64)] |= uint64_t{ 1 } << (slot % 64);
				}
			return true;
		});
		m_set = Set(); /* the old images go before the new ones come */
		snapshotImage(s.prevImage);
		if (m_detailed)
		{
			s.curImage = s.prevImage;
			s.changesBySlot.assign((size_t)s.slots, 0);
		}
	}
	else
	{
		s.dense = false;
		for (uint64_t address : m_set.addr)
			for (int k = 0; k < span; k++)
				if (fits(address + (uint64_t)k))
				{
					const uint32_t now = peek(address + (uint64_t)k, size);
					s.addr.push_back(address + (uint64_t)k);
					s.prev.push_back(now);
					if (m_detailed)
					{
						s.cur.push_back(now);
						s.changes.push_back(0);
					}
				}
	}
	m_set = std::move(s);
	settle();
}

void CeRamSearch::sort(int column, bool reverse, int display)
{
	if (column == ColChanges && !m_detailed) return;
	if (column == ColAddress && m_set.dense)
	{
		m_set.reversed = reverse;
		return;
	}
	makeSparse();
	const size_t n = m_set.addr.size();
	const int size = m_set.size;

	/* every key fits a double exactly: 33 bits at most */
	auto shown = [&](uint32_t v) -> double {
		if (display == DispFloat)
		{
			const float f = asFloat(v);
			return std::isnan(f) ? -std::numeric_limits<double>::infinity() : (double)f;
		}
		return (double)signExtend(v, size, display);
	};
	std::vector<double> key(n);
	for (size_t i = 0; i < n; i++)
	{
		const uint32_t cur = column == ColValue || column == ColDiff
			? (m_detailed ? m_set.cur[i] : peek(m_set.addr[i], size)) : 0;
		switch (column)
		{
		case ColValue: key[i] = shown(cur); break;
		case ColPrev: key[i] = shown(m_set.prev[i]); break;
		case ColChanges: key[i] = (double)m_set.changes[i]; break;
		case ColDiff: key[i] = shown(cur) - shown(m_set.prev[i]); break;
		default: key[i] = (double)m_set.addr[i]; break;
		}
	}
	std::vector<size_t> order(n);
	std::iota(order.begin(), order.end(), size_t{ 0 });
	if (reverse) std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key[a] > key[b]; });
	else std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return key[a] < key[b]; });

	auto apply = [&](auto &v) {
		if (v.empty()) return;
		std::remove_reference_t<decltype(v)> out(n);
		for (size_t i = 0; i < n; i++) out[i] = v[order[i]];
		v = std::move(out);
	};
	apply(m_set.addr);
	apply(m_set.prev);
	apply(m_set.cur);
	apply(m_set.changes);
}

/* Only a list that was added to can hold an address the domain has not. */
int64_t CeRamSearch::outOfRangeCount() const
{
	if (m_set.dense) return 0;
	int64_t n = 0;
	for (uint64_t a : m_set.addr)
		if (a > (uint64_t)m_domainSize || (uint64_t)m_domainSize - a < (uint64_t)m_set.size) n++;
	return n;
}

void CeRamSearch::removeOutOfRange()
{
	if (m_set.dense) return;
	std::vector<int64_t> indices;
	for (size_t i = 0; i < m_set.addr.size(); i++)
	{
		const uint64_t a = m_set.addr[i];
		if (a > (uint64_t)m_domainSize || (uint64_t)m_domainSize - a < (uint64_t)m_set.size) indices.push_back((int64_t)i);
	}
	const bool undo = m_undoEnabled;
	m_undoEnabled = false;
	removeIndices(indices.data(), (int64_t)indices.size());
	m_undoEnabled = undo;
}

int64_t CeRamSearch::undo()
{
	if (!canUndo()) return 0;
	const int64_t before = (int64_t)m_set.count;
	m_redo.push_back(std::move(m_set));
	m_set = std::move(m_undo.back());
	m_undo.pop_back();
	return (int64_t)m_set.count - before;
}

int64_t CeRamSearch::redo()
{
	if (!canRedo()) return 0;
	const int64_t before = (int64_t)m_set.count;
	m_undo.push_back(std::move(m_set));
	m_set = std::move(m_redo.back());
	m_redo.pop_back();
	return (int64_t)m_set.count - before;
}

/* ---- the ABI ---- */

struct ce_ramsearch
{
	CeRamSearch impl;
	ce_ramsearch(const uint8_t *base, ce_ramsearch_read_fn fn, void *user, int64_t size) : impl(base, fn, user, size) {}
};

/* Running out of memory is the one limit RAM Search has, and it is the
 * caller's to report: the search is left as it was where that is possible. */
#define CE_RS_GUARD(body, failed) \
	try { body; } catch (const std::bad_alloc &) { return failed; }

ce_ramsearch *ce_ramsearch_create(const uint8_t *base, ce_ramsearch_read_fn fn, void *user, int64_t domain_size)
{
	return new (std::nothrow) ce_ramsearch(base, fn, user, domain_size);
}

void ce_ramsearch_destroy(ce_ramsearch *rs) { delete rs; }

int32_t ce_ramsearch_start(ce_ramsearch *rs, int32_t size, int32_t misaligned, int32_t big_endian, int32_t detailed)
{
	CE_RS_GUARD(rs->impl.start(size, misaligned != 0, big_endian != 0, detailed != 0); return 0, -1)
}

int64_t ce_ramsearch_count(const ce_ramsearch *rs) { return rs->impl.count(); }

int32_t ce_ramsearch_row(const ce_ramsearch *rs, int64_t index, uint64_t *address, uint32_t *current, uint32_t *previous, uint32_t *changes)
{
	CeRamSearch::Row r;
	if (!rs->impl.row(index, r)) return 0;
	*address = r.address;
	*current = r.current;
	*previous = r.previous;
	*changes = r.changes;
	return 1;
}

int64_t ce_ramsearch_index_of(const ce_ramsearch *rs, uint64_t address) { return rs->impl.indexOf(address); }

static CeRamSearch::Query queryOf(int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by)
{
	CeRamSearch::Query q;
	q.compare = compare;
	q.op = op;
	q.display = display;
	q.value = value;
	q.differentBy = different_by;
	return q;
}

int64_t ce_ramsearch_search(ce_ramsearch *rs, int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by, int32_t previous_type)
{
	CE_RS_GUARD(return rs->impl.search(queryOf(compare, op, display, value, different_by), previous_type), -1)
}

int32_t ce_ramsearch_would_remove(const ce_ramsearch *rs, int64_t index, int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by)
{
	return rs->impl.wouldRemove(index, queryOf(compare, op, display, value, different_by)) ? 1 : 0;
}

int32_t ce_ramsearch_update(ce_ramsearch *rs, int32_t previous_type)
{
	CE_RS_GUARD(rs->impl.update(previous_type); return 0, -1)
}

int32_t ce_ramsearch_set_previous_to_current(ce_ramsearch *rs)
{
	CE_RS_GUARD(rs->impl.setPreviousToCurrent(); return 0, -1)
}

void ce_ramsearch_clear_change_counts(ce_ramsearch *rs) { rs->impl.clearChangeCounts(); }
void ce_ramsearch_set_big_endian(ce_ramsearch *rs, int32_t big_endian) { rs->impl.setBigEndian(big_endian != 0); }

int32_t ce_ramsearch_set_previous_type(ce_ramsearch *rs, int32_t previous_type)
{
	CE_RS_GUARD(rs->impl.setPreviousType(previous_type); return 0, -1)
}

int32_t ce_ramsearch_remove_indices(ce_ramsearch *rs, const int64_t *indices, int64_t n)
{
	CE_RS_GUARD(rs->impl.removeIndices(indices, n); return 0, -1)
}

int32_t ce_ramsearch_remove_addresses(ce_ramsearch *rs, const uint64_t *addresses, int64_t n, int32_t record_undo)
{
	CE_RS_GUARD(rs->impl.removeAddresses(addresses, n, record_undo != 0); return 0, -1)
}

int32_t ce_ramsearch_add_addresses(ce_ramsearch *rs, const uint64_t *addresses, int64_t n, int32_t append)
{
	CE_RS_GUARD(rs->impl.addAddresses(addresses, n, append != 0); return 0, -1)
}

int32_t ce_ramsearch_convert_to(ce_ramsearch *rs, int32_t size)
{
	CE_RS_GUARD(rs->impl.convertTo(size); return 0, -1)
}

int32_t ce_ramsearch_sort(ce_ramsearch *rs, int32_t column, int32_t reverse, int32_t display)
{
	CE_RS_GUARD(rs->impl.sort(column, reverse != 0, display); return 0, -1)
}

int64_t ce_ramsearch_out_of_range_count(const ce_ramsearch *rs) { return rs->impl.outOfRangeCount(); }

int32_t ce_ramsearch_remove_out_of_range(ce_ramsearch *rs)
{
	CE_RS_GUARD(rs->impl.removeOutOfRange(); return 0, -1)
}

void ce_ramsearch_set_undo_enabled(ce_ramsearch *rs, int32_t on) { rs->impl.setUndoEnabled(on != 0); }
int32_t ce_ramsearch_can_undo(const ce_ramsearch *rs) { return rs->impl.canUndo() ? 1 : 0; }
int32_t ce_ramsearch_can_redo(const ce_ramsearch *rs) { return rs->impl.canRedo() ? 1 : 0; }
void ce_ramsearch_clear_history(ce_ramsearch *rs) { rs->impl.clearHistory(); }
int64_t ce_ramsearch_undo(ce_ramsearch *rs) { return rs->impl.undo(); }
int64_t ce_ramsearch_redo(ce_ramsearch *rs) { return rs->impl.redo(); }
