/* ram_search.cpp - see ram_search.hpp for the two shapes of the candidate set. */

#include "ram_search.hpp"

#include "chimera/engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <numeric>
#include <unordered_set>

namespace
{
inline int popcount64(uint64_t v) { return __builtin_popcountll(v); }
inline int ctz64(uint64_t v) { return __builtin_ctzll(v); }

inline float asFloat(uint64_t v)
{
	const uint32_t bits = (uint32_t)v;
	float f;
	std::memcpy(&f, &bits, sizeof f);
	return f;
}

inline double asDouble(uint64_t v)
{
	double d;
	std::memcpy(&d, &v, sizeof d);
	return d;
}

/* The frontend's ApproxFloatEquality: within 2^-24. A NaN equals nothing. */
inline bool approx(float a, float b) { return std::fabs(b - a) < 5.96046448E-08f; }
/* A double (an 8-byte candidate shown as floating point, issue 474): within a
 * part in a billion of the larger, so 0.3 - 0.1 is 0.2 as typed */
inline bool approx(double a, double b) { return a == b || std::fabs(b - a) <= 1e-9 * std::max(std::fabs(a), std::fabs(b)); }

inline int64_t signExtend(uint64_t v, int size, int display)
{
	if (display != CeRamSearch::DispSigned) return (int64_t)v;
	switch (size)
	{
	case 1: return (int8_t)v;
	case 2: return (int16_t)v;
	case 4: return (int32_t)v;
	default: return (int64_t)v;
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

/* Two values of a candidate's size: an 8-byte one shown unsigned is ordered
 * as the unsigned number it is, past 2^63 too */
inline bool compareValues(const CeRamSearch::Query &q, int size, uint64_t a, uint64_t b)
{
	if (size == 8 && q.display != CeRamSearch::DispSigned)
	{
		if (q.op == CeRamSearch::OpDifferentBy) return q.differentBy == (a > b ? a - b : b - a);
		return ordered(q.op, a, b);
	}
	return compareInts(q, signExtend(a, size, q.display), signExtend(b, size, q.display));
}

template <typename T> inline bool compareFloats(const CeRamSearch::Query &q, T a, T b, T differentBy)
{
	switch (q.op)
	{
	case CeRamSearch::OpGreaterThan: return a > b;
	case CeRamSearch::OpGreaterThanEqual: return a > b || approx(a, b);
	case CeRamSearch::OpLessThan: return a < b;
	case CeRamSearch::OpLessThanEqual: return a < b || approx(a, b);
	case CeRamSearch::OpNotEqual: return !approx(a, b);
	case CeRamSearch::OpDifferentBy: return approx(std::fabs(a - b), differentBy);
	default: return approx(a, b);
	}
}

/* A float at size 4, a double at size 8 */
inline bool compareReals(const CeRamSearch::Query &q, int size, uint64_t a, uint64_t b)
{
	if (size == 8) return compareFloats(q, asDouble(a), asDouble(b), asDouble(q.differentBy));
	return compareFloats(q, asFloat(a), asFloat(b), asFloat(q.differentBy));
}
} // namespace

CeRamSearch::CeRamSearch(const uint8_t *base, CeRamSearchReadFn fn, void *user, int64_t domainSize)
	: m_base(base), m_fn(fn), m_user(user), m_realSize(domainSize < 0 ? 0 : domainSize), m_domainSize(m_realSize)
{
	m_set.dense = false;
}

/* ---- live ranges ---- */

/* Ranges are kept whole multiples of eight bytes - the largest candidate - so
 * that an aligned candidate is aligned in the compacted domain too and lies in
 * one range; a few bytes more than a core said are harmless, they read as the
 * core reads them. */
void CeRamSearch::setRanges(const int64_t *pairs, int64_t n)
{
	m_pending.clear();
	if (pairs == nullptr || n < 0) return;
	std::vector<Range> list;
	for (int64_t i = 0; i < n; i++)
	{
		int64_t from = pairs[2 * i], length = pairs[2 * i + 1];
		if (length <= 0 || from >= m_realSize || from < 0 && length <= -from) continue;
		int64_t to = length > m_realSize - std::max<int64_t>(from, 0) ? m_realSize : from + length;
		from = std::max<int64_t>(from, 0) & ~int64_t{ 7 };
		to = std::min(m_realSize, (to + 7) & ~int64_t{ 7 });
		Range r;
		r.real = from;
		r.length = to - from;
		list.push_back(r);
	}
	std::sort(list.begin(), list.end(), [](const Range &a, const Range &b) { return a.real < b.real; });
	for (const Range &r : list)
	{
		if (!m_pending.empty() && r.real <= m_pending.back().real + m_pending.back().length)
		{
			Range &last = m_pending.back();
			last.length = std::max(last.length, r.real + r.length - last.real);
		}
		else m_pending.push_back(r);
	}
	/* No range at all is still an answer: nothing is live, nothing is searched. */
	if (m_pending.empty())
	{
		Range none;
		m_pending.push_back(none);
	}
	int64_t at = 0;
	for (Range &r : m_pending)
	{
		r.at = at;
		at += r.length;
	}
}

uint64_t CeRamSearch::toReal(uint64_t address) const
{
	if (m_ranges.empty()) return address;
	auto it = std::upper_bound(m_ranges.begin(), m_ranges.end(), address,
		[](uint64_t a, const Range &r) { return a < (uint64_t)r.at; });
	if (it == m_ranges.begin()) return address;
	--it;
	return (uint64_t)it->real + (address - (uint64_t)it->at);
}

bool CeRamSearch::toCompact(uint64_t real, uint64_t &address) const
{
	address = real;
	if (m_ranges.empty()) return true;
	auto it = std::upper_bound(m_ranges.begin(), m_ranges.end(), real,
		[](uint64_t a, const Range &r) { return a < (uint64_t)r.real; });
	if (it == m_ranges.begin()) return false;
	--it;
	if (real - (uint64_t)it->real >= (uint64_t)it->length) return false;
	address = (uint64_t)it->at + (real - (uint64_t)it->real);
	return true;
}

/* Does a candidate of this size at this (compacted) address lie in one range? */
bool CeRamSearch::inOneRange(uint64_t address, int size) const
{
	if (m_ranges.empty()) return true;
	auto it = std::upper_bound(m_ranges.begin(), m_ranges.end(), address,
		[](uint64_t a, const Range &r) { return a < (uint64_t)r.at; });
	if (it == m_ranges.begin()) return false;
	--it;
	return address - (uint64_t)it->at + (uint64_t)size <= (uint64_t)it->length;
}

/* ---- memory ---- */

/* The compacted domain, a range at a time. */
void CeRamSearch::readInto(int64_t offset, uint8_t *buf, int64_t len) const
{
	if (m_ranges.empty())
	{
		readReal(offset, buf, len);
		return;
	}
	if (len <= 0) return;
	auto it = std::upper_bound(m_ranges.begin(), m_ranges.end(), offset,
		[](int64_t a, const Range &r) { return a < r.at; });
	if (offset >= 0 && it != m_ranges.begin()) --it;
	while (len > 0 && offset >= 0 && it != m_ranges.end() && offset - it->at < it->length)
	{
		const int64_t piece = std::min(len, it->length - (offset - it->at));
		readReal(it->real + (offset - it->at), buf, piece);
		buf += piece;
		offset += piece;
		len -= piece;
		++it;
	}
	if (len > 0) std::memset(buf, 0, (size_t)len);
}

void CeRamSearch::readReal(int64_t offset, uint8_t *buf, int64_t len) const
{
	if (len <= 0) return;
	int64_t got = 0;
	if (offset >= 0 && offset < m_realSize)
	{
		int64_t want = std::min(len, m_realSize - offset);
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
	if (m_walkImage != nullptr) return m_walkImage->data() + offset;
	if (m_base && m_ranges.empty()) return m_base + offset;
	if ((int64_t)m_scratch.size() < len) m_scratch.resize((size_t)len);
	readInto(offset, m_scratch.data(), len);
	return m_scratch.data();
}

uint64_t CeRamSearch::load(const uint8_t *p, int size) const
{
	switch (size)
	{
	case 1: return p[0];
	case 2: return m_bigEndian ? (uint32_t)(p[0] << 8 | p[1]) : (uint32_t)(p[1] << 8 | p[0]);
	case 4:
		return m_bigEndian
			? ((uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3])
			: ((uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0]);
	default:
	{
		uint64_t v = 0;
		for (int k = 0; k < 8; k++) v |= (uint64_t)p[m_bigEndian ? k : 7 - k] << (8 * (7 - k));
		return v;
	}
	}
}

/* A value that does not fit inside the domain reads as zero, as it always has. */
/* Turns a flag off for a scope and puts it back however the scope ends. */
struct UndoOff
{
	bool &flag;
	const bool was;
	explicit UndoOff(bool &f) : flag(f), was(f) { flag = false; }
	~UndoOff() { flag = was; }
};

uint64_t CeRamSearch::peek(uint64_t address, int size) const
{
	if (address > (uint64_t)m_domainSize || (uint64_t)m_domainSize - address < (uint64_t)size) return 0;
	if (m_base && m_ranges.empty()) return load(m_base + address, size);
	uint8_t b[8];
	readInto((int64_t)address, b, size);
	return load(b, size);
}

/* Many candidates' values. Where the domain has no pointer (a bus, or live
 * ranges) every read is a call through the frontend into the core, and a
 * sparse set made one per candidate (engine analysis, 2026-10-18). So runs of
 * candidates with little between them are read together: a run ends at a gap
 * of more than 4 KiB, or at Chunk bytes. */
void CeRamSearch::readValues(const std::vector<uint64_t> &addr, int size, std::vector<uint64_t> &out) const
{
	out.resize(addr.size());
	if (m_base && m_ranges.empty())
	{
		for (size_t i = 0; i < addr.size(); i++) out[i] = peek(addr[i], size);
		return;
	}
	const uint64_t domain = (uint64_t)m_domainSize, wide = (uint64_t)size, gap = 4096;
	size_t i = 0;
	while (i < addr.size())
	{
		const uint64_t start = addr[i];
		if (start > domain || domain - start < wide)
		{
			out[i++] = 0;   /* does not fit inside the domain: zero, as peek says */
			continue;
		}
		uint64_t end = start + wide;
		size_t j = i + 1;
		while (j < addr.size() && addr[j] >= addr[j - 1] && addr[j] <= end + gap
			&& addr[j] + wide - start <= (uint64_t)Chunk && addr[j] <= domain && domain - addr[j] >= wide)
		{
			end = std::max(end, addr[j] + wide);
			j++;
		}
		const int64_t len = (int64_t)(end - start);
		if ((int64_t)m_scratch.size() < len) m_scratch.resize((size_t)len);
		readInto((int64_t)start, m_scratch.data(), len);
		for (size_t k = i; k < j; k++) out[k] = load(m_scratch.data() + (addr[k] - start), size);
		i = j;
	}
}

void CeRamSearch::snapshotImage(std::vector<uint8_t> &image) const
{
	image.resize((size_t)m_domainSize);
	for (int64_t at = 0; at < m_domainSize; at += Chunk)
		readInto(at, image.data() + at, std::min(Chunk, m_domainSize - at));
}

/* ---- the predicate ---- */

bool CeRamSearch::matches(const Query &q, int size, uint64_t address, uint64_t cur, uint64_t prev, uint32_t changes)
{
	const bool isFloat = q.display == DispFloat && (size == 4 || size == 8);
	switch (q.compare)
	{
	case CmpSpecificValue:
		if (isFloat) return compareReals(q, size, cur, q.value);
		return compareValues(q, size, cur, q.value);
	case CmpSpecificAddress:
		return compareInts(q, (int64_t)address, (int64_t)q.value);
	case CmpChanges:
		return compareInts(q, (int64_t)changes, (int64_t)q.value);
	case CmpDifference:
		if (isFloat)
		{
			if (size == 8) return compareFloats(q, asDouble(cur) - asDouble(prev), asDouble(q.value), asDouble(q.differentBy));
			return compareFloats(q, asFloat(cur) - asFloat(prev), asFloat(q.value), asFloat(q.differentBy));
		}
		/* A difference is as wide as it needs to be whatever the watch size: a
		 * byte can fall by 200. The box hands over a 64-bit integer (an 8-byte
		 * difference past what that holds wraps, as the values do). */
		return compareInts(q, (int64_t)((uint64_t)signExtend(cur, size, q.display) - (uint64_t)signExtend(prev, size, q.display)),
			(int64_t)q.value);
	default:
		if (isFloat) return compareReals(q, size, cur, prev);
		return compareValues(q, size, cur, prev);
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

/* Dense, equal or not equal to the previous value, every candidate on bytes
 * of its own (step == size): an integer is the same exactly when its bytes
 * are, whatever the byte order or sign, so the memory is compared with the
 * previous image eight bytes at a time, and a candidate is looked at alone
 * only in eight bytes that differ. One call per candidate made this about 7
 * times slower than it needs to be (engine analysis, 2026-10-18). A float is
 * equal within a margin, and is left to filter(). */
void CeRamSearch::filterSameAsPrevious(bool keepSame)
{
	const int size = m_set.size;
	const int lanes = 8 / size;
	const uint64_t laneBits = (uint64_t{ 1 } << lanes) - 1;
	const uint64_t laneMask = size == 8 ? ~0ull : size == 4 ? 0xFFFFFFFFull : size == 2 ? 0xFFFFull : 0xFFull;
	const uint8_t *prev = m_set.prevImage.data();
	const int64_t prevSize = (int64_t)m_set.prevImage.size();
	for (int64_t chunkStart = 0; chunkStart < m_domainSize; chunkStart += Chunk)
	{
		const int64_t chunkEnd = std::min(chunkStart + Chunk, m_domainSize);
		const uint64_t firstSlot = (uint64_t)chunkStart / (uint64_t)size;
		const uint64_t endSlot = std::min(m_set.slots, (uint64_t)(chunkEnd + size - 1) / (uint64_t)size);
		if (firstSlot >= endSlot) break;
		const uint64_t firstWord = firstSlot / 64, endWord = (endSlot + 63) / 64;
		bool any = false;
		for (uint64_t w = firstWord; w < endWord && !any; w++) any = m_set.bits[(size_t)w] != 0;
		if (!any) continue;
		const int64_t windowLen = std::min(chunkEnd + size - 1, m_domainSize) - chunkStart;
		const uint8_t *mem = window(chunkStart, windowLen);
		for (uint64_t w = firstWord; w < endWord; w++)
		{
			const uint64_t bits = m_set.bits[(size_t)w];
			if (bits == 0) continue;
			uint64_t differ = 0;
			for (int g = 0; g < 64; g += lanes)
			{
				if (((bits >> g) & laneBits) == 0) continue;
				const int64_t at = (int64_t)((w * 64 + (uint64_t)g) * (uint64_t)size);
				const int64_t rel = at - chunkStart;
				if (rel >= 0 && rel + 8 <= windowLen && at + 8 <= prevSize)
				{
					uint64_t a, b;
					std::memcpy(&a, mem + rel, 8);
					std::memcpy(&b, prev + at, 8);
					const uint64_t x = a ^ b;
					if (x == 0) continue;
					for (int k = 0; k < lanes; k++)
						if ((size == 8 ? x : x >> (k * size * 8)) & laneMask) differ |= uint64_t{ 1 } << (g + k);
					continue;
				}
				/* the domain's last bytes, or a chunk's edge: one candidate at a time */
				for (int k = 0; k < lanes; k++)
				{
					if (((bits >> (g + k)) & 1) == 0) continue;
					const int64_t one = at + (int64_t)k * size;
					if (one - chunkStart + size > windowLen || one + size > prevSize) continue;
					if (std::memcmp(mem + (one - chunkStart), prev + one, (size_t)size) != 0) differ |= uint64_t{ 1 } << (g + k);
				}
			}
			m_set.bits[(size_t)w] = keepSame ? bits & ~differ : bits & differ;
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
	if (mustBeSparse()) makeSparse();
	else if (m_set.count <= 4096 || m_set.count <= m_set.slots / 32)
	{
		/* the smaller shape is a saving, not a need: without the memory for it
		 * the set stays as it is (makeSparse changes nothing until the end) */
		try
		{
			makeSparse();
		}
		catch (const std::bad_alloc &)
		{
		}
	}
}

uint64_t CeRamSearch::bytesOf(const Set &s)
{
	return s.bits.capacity() * 8 + s.rank.capacity() * 8 + s.prevImage.capacity() + s.curImage.capacity()
		+ s.changesBySlot.capacity() * 4 + s.addr.capacity() * 8
		+ (s.prev.capacity() + s.cur.capacity()) * 8 + s.changes.capacity() * 4;
}

/* A level is a whole copy of the set. Room is made BEFORE the copy, oldest
 * first, so the history never holds more than its budget, not even for a
 * moment: on Linux an allocation that does not fit is not refused, it ends the
 * process, and the bad_alloc below is not a promise (engine analysis,
 * 2026-10-18). */
bool CeRamSearch::recordUndo()
{
	if (!m_undoEnabled) return false;
	try
	{
		m_redo.clear();
		const uint64_t need = bytesOf(m_set);
		if (need > m_undoBudget)
		{
			clearHistory();
			return false;
		}
		uint64_t held = 0;
		for (const Set &s : m_undo) held += bytesOf(s);
		while (!m_undo.empty() && ((int)m_undo.size() >= MaxUndoLevels || held + need > m_undoBudget))
		{
			held -= bytesOf(m_undo.front());
			m_undo.pop_front();
		}
		m_undo.push_back(m_set);
		return true;
	}
	catch (const std::bad_alloc &)
	{
		/* history is a convenience; the search is not */
		clearHistory();
		return false;
	}
}

/* ---- operations ---- */

void CeRamSearch::start(int size, bool misaligned, bool bigEndian, bool detailed)
{
	/* Running out of memory below leaves the search as it was: the switches
	 * and ranges were set before anything was allocated, so the next row read
	 * a detailed image that was never made (engine analysis, 2026-10-18). */
	const bool wasBigEndian = m_bigEndian, wasDetailed = m_detailed, wasMisaligned = m_misaligned;
	const std::vector<Range> wasRanges = m_ranges;
	const int64_t wasDomainSize = m_domainSize;
	try
	{
		startUnguarded(size, misaligned, bigEndian, detailed);
	}
	catch (const std::bad_alloc &)
	{
		m_bigEndian = wasBigEndian;
		m_detailed = wasDetailed;
		m_misaligned = wasMisaligned;
		m_ranges = wasRanges;
		m_domainSize = wasDomainSize;
		throw;
	}
}

void CeRamSearch::startUnguarded(int size, bool misaligned, bool bigEndian, bool detailed)
{
	clearHistory();
	m_bigEndian = bigEndian;
	m_detailed = detailed;
	m_misaligned = misaligned;
	if (size != 1 && size != 2 && size != 4 && size != 8) size = 1;

	/* what is live now is what this search is over, until the next one starts */
	m_ranges = m_pending;
	if (m_ranges.size() == 1 && m_ranges[0].real == 0 && m_ranges[0].length == m_realSize) m_ranges.clear();
	m_domainSize = m_realSize;
	if (!m_ranges.empty()) m_domainSize = m_ranges.back().at + m_ranges.back().length;

	Set s;
	s.dense = true;
	s.size = size;
	s.step = misaligned ? 1 : size;
	s.slots = m_domainSize < size ? 0
		: misaligned ? (uint64_t)(m_domainSize - size + 1)
		: (uint64_t)(m_domainSize / size);
	s.bits.assign((size_t)((s.slots + 63) / 64), ~uint64_t{ 0 });
	if (s.slots % 64) s.bits.back() = (uint64_t{ 1 } << (s.slots % 64)) - 1;
	/* no candidate spans two ranges: the last few of each, when they overlap */
	if (misaligned && size > 1)
		for (size_t r = 0; r + 1 < m_ranges.size(); r++)
		{
			const uint64_t end = (uint64_t)(m_ranges[r].at + m_ranges[r].length);
			for (uint64_t a = end >= (uint64_t)size ? end - (uint64_t)size + 1 : 0; a < end && a < s.slots; a++)
				s.bits[(size_t)(a / 64)] &= ~(uint64_t{ 1 } << (a % 64));
		}
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
		out.address = toReal(out.address);
		return true;
	}
	const size_t i = (size_t)index;
	out.address = m_set.addr[i];
	out.previous = m_set.prev[i];
	out.current = m_detailed ? m_set.cur[i] : peek(out.address, m_set.size);
	out.changes = m_detailed ? m_set.changes[i] : 0;
	out.address = toReal(out.address);
	return true;
}

int64_t CeRamSearch::indexOf(uint64_t realAddress) const
{
	uint64_t address;
	if (!toCompact(realAddress, address)) return -1;
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
	const bool recorded = recordUndo();
	try
	{
		return filter(q, previousType);
	}
	catch (const std::bad_alloc &)
	{
		/* "not enough memory" means the search is as it was: the level just
		 * recorded is what it was */
		if (recorded)
		{
			m_set = std::move(m_undo.back());
			m_undo.pop_back();
		}
		throw;
	}
}

int64_t CeRamSearch::filter(const Query &q, int previousType)
{
	const uint64_t before = m_set.count;
	const int size = m_set.size;
	/* a search by address is by the real one */
	const bool byAddress = q.compare == CmpSpecificAddress && !m_ranges.empty();

	/* The domain read once, when the previous values are to become what it
	 * holds now: the walk serves from this image, and so do they after it. It
	 * used to be read twice, and on a bus each read is a call into the core. */
	const bool takeImage = m_set.dense && !m_detailed && previousType == PrevLastSearch && !(m_base && m_ranges.empty());
	if (takeImage) snapshotImage(m_nextImage);
	/* the commonest search there is, "the same as before" or "changed", over a
	 * whole domain, eight bytes at a time */
	if (m_set.dense && !m_detailed && q.compare == CmpPrevious && (q.op == OpEqual || q.op == OpNotEqual)
		&& q.display != DispFloat && m_set.step == m_set.size)
	{
		m_walkImage = takeImage ? &m_nextImage : nullptr;
		filterSameAsPrevious(q.op == OpEqual);
		m_walkImage = nullptr;
		rebuildRank();
	}
	else if (m_set.dense)
	{
		const bool needMemory = !m_detailed && q.compare != CmpSpecificAddress && q.compare != CmpChanges;
		m_walkImage = takeImage ? &m_nextImage : nullptr;
		forEachDense(needMemory, [&](uint64_t slot, uint64_t address, const uint8_t *mem) {
			const uint64_t prev = load(m_set.prevImage.data() + address, size);
			const uint64_t cur = m_detailed ? load(m_set.curImage.data() + address, size) : mem ? load(mem, size) : 0;
			return matches(q, size, byAddress ? toReal(address) : address, cur, prev, m_detailed ? m_set.changesBySlot[(size_t)slot] : 0);
		});
		m_walkImage = nullptr;
		rebuildRank();
	}
	else
	{
		std::vector<uint64_t> now;
		if (!m_detailed) readValues(m_set.addr, size, now);
		size_t kept = 0;
		for (size_t i = 0; i < m_set.addr.size(); i++)
		{
			const uint64_t cur = m_detailed ? m_set.cur[i] : now[i];
			if (!matches(q, size, byAddress ? toReal(m_set.addr[i]) : m_set.addr[i], cur, m_set.prev[i], m_detailed ? m_set.changes[i] : 0)) continue;
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
	if (previousType == PrevLastSearch)
	{
		if (takeImage) previousFrom(m_nextImage);
		else setPreviousToCurrent();
	}
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
	else readValues(m_set.addr, m_set.size, m_set.prev);
}

/* The previous values from an image of the domain read a moment ago. */
void CeRamSearch::previousFrom(const std::vector<uint8_t> &image)
{
	if (m_set.dense)
	{
		m_set.prevImage = image;
		return;
	}
	for (size_t i = 0; i < m_set.addr.size(); i++)
	{
		const uint64_t a = m_set.addr[i];
		m_set.prev[i] = a <= (uint64_t)m_domainSize && (uint64_t)m_domainSize - a >= (uint64_t)m_set.size
			? load(image.data() + a, m_set.size) : 0;
	}
}

void CeRamSearch::update(int previousType)
{
	if (!m_detailed) return;
	setPreviousType(previousType);
	const int size = m_set.size;
	m_walked = 0;
	if (m_set.dense)
	{
		/* The domain is read once, and only blocks whose bytes changed are
		 * walked. Every candidate was compared every frame: about 1.3 ms a MiB
		 * a frame, on a domain that mostly does not change (engine analysis,
		 * 2026-10-18). A candidate starting in a block may end in the next, so
		 * a block is compared with the size - 1 bytes after it. */
		snapshotImage(m_nextImage);
		const uint8_t *now = m_nextImage.data(), *was = m_set.curImage.data();
		const int64_t block = 4096, step = m_set.step;
		for (int64_t at = 0; at < m_domainSize; at += block)
		{
			const int64_t stop = std::min(at + block, m_domainSize);
			const int64_t compared = std::min(stop + size - 1, m_domainSize) - at;
			if (std::memcmp(now + at, was + at, (size_t)compared) == 0) continue;
			const uint64_t first = (uint64_t)((at + step - 1) / step);
			const uint64_t end = std::min(m_set.slots, (uint64_t)((stop + step - 1) / step));
			for (uint64_t slot = first; slot < end; slot++)
			{
				if (((m_set.bits[(size_t)(slot / 64)] >> (slot % 64)) & 1) == 0) continue;
				m_walked++;
				const uint64_t address = slot * (uint64_t)step;
				if (std::memcmp(now + address, was + address, (size_t)size) == 0) continue;
				m_set.changesBySlot[(size_t)slot]++;
				/* safe: never dense when candidates overlap (mustBeSparse) */
				if (previousType == PrevLastChange)
					std::memcpy(m_set.prevImage.data() + address, was + address, (size_t)size);
			}
		}
		/* the images move whole, after the walk: candidates may overlap */
		if (previousType == PrevLastFrame) std::swap(m_set.prevImage, m_set.curImage);
		std::swap(m_set.curImage, m_nextImage);
		return;
	}
	std::vector<uint64_t> values;
	readValues(m_set.addr, size, values);
	m_walked = m_set.addr.size();
	for (size_t i = 0; i < m_set.addr.size(); i++)
	{
		const uint64_t now = values[i];
		if (now != m_set.cur[i])
		{
			m_set.changes[i]++;
			if (previousType == PrevLastChange) m_set.prev[i] = m_set.cur[i];
		}
		if (previousType == PrevLastFrame) m_set.prev[i] = m_set.cur[i];
		m_set.cur[i] = now;
	}
}

bool CeRamSearch::catchUp(int previousType)
{
	if (!m_detailed) return false;
	bool differs = false;
	if (m_set.dense)
	{
		snapshotImage(m_nextImage);
		differs = m_nextImage.size() != m_set.curImage.size()
			|| std::memcmp(m_nextImage.data(), m_set.curImage.data(), m_nextImage.size()) != 0;
	}
	else
	{
		std::vector<uint64_t> values;
		readValues(m_set.addr, m_set.size, values);
		differs = values != m_set.cur;
	}
	if (differs) update(previousType);
	return differs;
}

/* Switching the byte order mid-search: a dense set's previous and current
 * values are bytes, read in the new order from now on. A sparse set holds them
 * as numbers decoded in the old order, so each is turned round within its
 * size, in the set and in every undo and redo level. Left alone, a sparse
 * search dropped good candidates and detailed mode counted changes that were
 * not there (engine analysis, 2026-10-18). */
static uint64_t swapWithin(uint64_t v, int size)
{
	uint64_t out = 0;
	for (int k = 0; k < size; k++) out |= ((v >> (8 * k)) & 0xFF) << (8 * (size - 1 - k));
	return size == 1 ? v : out;
}

void CeRamSearch::setBigEndian(bool bigEndian)
{
	if (bigEndian == m_bigEndian) return;
	m_bigEndian = bigEndian;
	auto turn = [](Set &s) {
		if (s.dense || s.size == 1) return;
		for (uint64_t &v : s.prev) v = swapWithin(v, s.size);
		for (uint64_t &v : s.cur) v = swapWithin(v, s.size);
	};
	turn(m_set);
	for (Set &s : m_undo) turn(s);
	for (Set &s : m_redo) turn(s);
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

void CeRamSearch::removeAddresses(const uint64_t *realAddresses, int64_t n, bool withUndo)
{
	std::vector<uint64_t> compact;
	const uint64_t *addresses = realAddresses;
	if (!m_ranges.empty())
	{
		uint64_t a;
		for (int64_t i = 0; i < n; i++)
			if (toCompact(realAddresses[i], a)) compact.push_back(a);
		addresses = compact.data();
		n = (int64_t)compact.size();
	}
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
	/* undo is off for the removal (recorded above, or not wanted) and on again
	 * however it ends: running out of memory in it skipped the restore and
	 * left undo off for good (engine analysis, 2026-10-18) */
	UndoOff off(m_undoEnabled);
	removeIndices(indices.data(), (int64_t)indices.size());
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
		/* an address outside what this search is over is not memory it has */
		uint64_t address;
		if (!toCompact(addresses[i], address) || !inOneRange(address, m_set.size)) continue;
		const uint64_t now = peek(address, m_set.size);
		m_set.addr.push_back(address);
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
	if (size != 1 && size != 2 && size != 4 && size != 8) return;
	clearHistory();
	const int oldSize = m_set.size;
	const int span = m_misaligned ? 1 : oldSize;
	const int newStep = m_misaligned ? 1 : size;
	auto fits = [&](uint64_t a) {
		return m_domainSize >= size && a <= (uint64_t)(m_domainSize - size) && a % (uint64_t)newStep == 0
			&& inOneRange(a, size);
	};

	Set s;
	s.size = size;
	s.step = newStep;
	if (m_set.dense)
	{
		s.dense = true;
		/* the order a person sorted it in stays, as a sparse set's does */
		s.reversed = m_set.reversed;
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
					const uint64_t now = peek(address + (uint64_t)k, size);
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

	/* every key is a long double: 64 bits of mantissa with the compilers
	 * Chimera is built with, so an 8-byte value sorts exactly */
	auto shown = [&](uint64_t v) -> long double {
		if (display == DispFloat && (size == 4 || size == 8))
		{
			const double f = size == 8 ? asDouble(v) : (double)asFloat(v);
			return std::isnan(f) ? -std::numeric_limits<long double>::infinity() : (long double)f;
		}
		if (size == 8 && display != DispSigned) return (long double)v;
		return (long double)signExtend(v, size, display);
	};
	std::vector<long double> key(n);
	for (size_t i = 0; i < n; i++)
	{
		const uint64_t cur = column == ColValue || column == ColDiff
			? (m_detailed ? m_set.cur[i] : peek(m_set.addr[i], size)) : 0;
		switch (column)
		{
		case ColValue: key[i] = shown(cur); break;
		case ColPrev: key[i] = shown(m_set.prev[i]); break;
		case ColChanges: key[i] = (long double)m_set.changes[i]; break;
		case ColDiff: key[i] = shown(cur) - shown(m_set.prev[i]); break;
		default: key[i] = (long double)m_set.addr[i]; break;
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
	UndoOff off(m_undoEnabled);
	removeIndices(indices.data(), (int64_t)indices.size());
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

int32_t ce_ramsearch_set_ranges(ce_ramsearch *rs, const int64_t *pairs, int64_t n)
{
	CE_RS_GUARD(rs->impl.setRanges(pairs, n); return 0, -1)
}

int64_t ce_ramsearch_searched_size(const ce_ramsearch *rs) { return rs->impl.searchedSize(); }

int64_t ce_ramsearch_count(const ce_ramsearch *rs) { return rs->impl.count(); }

int32_t ce_ramsearch_row64(const ce_ramsearch *rs, int64_t index, uint64_t *address, uint64_t *current, uint64_t *previous, uint32_t *changes)
{
	CeRamSearch::Row r;
	if (!rs->impl.row(index, r)) return 0;
	*address = r.address;
	*current = r.current;
	*previous = r.previous;
	*changes = r.changes;
	return 1;
}

int32_t ce_ramsearch_row(const ce_ramsearch *rs, int64_t index, uint64_t *address, uint32_t *current, uint32_t *previous, uint32_t *changes)
{
	uint64_t cur = 0, prev = 0;
	if (!ce_ramsearch_row64(rs, index, address, &cur, &prev, changes)) return 0;
	*current = (uint32_t)cur;
	*previous = (uint32_t)prev;
	return 1;
}

int64_t ce_ramsearch_index_of(const ce_ramsearch *rs, uint64_t address) { return rs->impl.indexOf(address); }

static CeRamSearch::Query queryOf(int32_t compare, int32_t op, int32_t display, uint64_t value, uint64_t different_by)
{
	CeRamSearch::Query q;
	q.compare = compare;
	q.op = op;
	q.display = display;
	q.value = value;
	q.differentBy = different_by;
	return q;
}

int64_t ce_ramsearch_search64(ce_ramsearch *rs, int32_t compare, int32_t op, int32_t display, uint64_t value, uint64_t different_by, int32_t previous_type)
{
	CE_RS_GUARD(return rs->impl.search(queryOf(compare, op, display, value, different_by), previous_type), -1)
}

int32_t ce_ramsearch_would_remove64(const ce_ramsearch *rs, int64_t index, int32_t compare, int32_t op, int32_t display, uint64_t value, uint64_t different_by)
{
	return rs->impl.wouldRemove(index, queryOf(compare, op, display, value, different_by)) ? 1 : 0;
}

/* The 32-bit calls: a value as 32 bits, a signed difference widened from
 * them, as they were before 8-byte candidates (issue 474) */
static uint64_t widened(int32_t display, int32_t compare, uint32_t v)
{
	return display == CeRamSearch::DispSigned && compare == CeRamSearch::CmpDifference ? (uint64_t)(int64_t)(int32_t)v : v;
}

int64_t ce_ramsearch_search(ce_ramsearch *rs, int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by, int32_t previous_type)
{
	return ce_ramsearch_search64(rs, compare, op, display, widened(display, compare, value), different_by, previous_type);
}

int32_t ce_ramsearch_would_remove(const ce_ramsearch *rs, int64_t index, int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by)
{
	return ce_ramsearch_would_remove64(rs, index, compare, op, display, widened(display, compare, value), different_by);
}

int32_t ce_ramsearch_update(ce_ramsearch *rs, int32_t previous_type)
{
	CE_RS_GUARD(rs->impl.update(previous_type); return 0, -1)
}

int32_t ce_ramsearch_catch_up(ce_ramsearch *rs, int32_t previous_type)
{
	CE_RS_GUARD(return rs->impl.catchUp(previous_type) ? 1 : 0, -1)
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
