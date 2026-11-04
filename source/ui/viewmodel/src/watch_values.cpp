#ifdef __APPLE__
#include "charconv_apple.hpp"   /* floating-point charconv is macOS 26 and later there */
#endif
#include "chimera/vm/watch_values.hpp"
#include "chimera/vm/tr.hpp"

#include <cerrno>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace chimera::vm::watch {

const std::vector<Display> &displaysFor(int size)
{
	static const std::vector<Display> one{Display::Unsigned, Display::Signed, Display::Hex, Display::Binary};
	static const std::vector<Display> two{Display::Unsigned, Display::Signed, Display::Hex, Display::Binary, Display::Fixed12_4};
	static const std::vector<Display> four{Display::Unsigned, Display::Signed, Display::Hex, Display::Binary,
		Display::Fixed20_12, Display::Fixed16_16, Display::Float};
	static const std::vector<Display> eight{Display::Unsigned, Display::Signed, Display::Hex, Display::Double};
	static const std::vector<Display> none;
	return size == 1 ? one : size == 2 ? two : size == 4 ? four : size == 8 ? eight : none;
}

bool fits(int size, Display display)
{
	for (const Display d : displaysFor(size))
		if (d == display) return true;
	return false;
}

namespace {

/* bits in groups of four, the highest first */
std::string binary(uint32_t value, int bits)
{
	std::string out;
	for (int i = bits - 1; i >= 0; i--)
	{
		out += ((value >> i) & 1u) != 0 ? '1' : '0';
		if (i != 0 && i % 4 == 0) out += ' ';
	}
	return out;
}

/* n / 2^shift with `decimals` places, the last rounded half away from zero:
 * exact, since the value is exact */
std::string fixedPoint(int64_t n, int shift, int decimals, bool trim)
{
	const bool negative = n < 0;
	const uint64_t magnitude = static_cast<uint64_t>(negative ? -n : n);
	uint64_t scale = 1;
	for (int i = 0; i < decimals; i++) scale *= 10;
	/* magnitude * scale / 2^shift, rounded half up on the magnitude */
	const uint64_t whole = magnitude >> shift;
	const uint64_t rest = magnitude & ((uint64_t{1} << shift) - 1);
	uint64_t fraction = (rest * scale * 2 + (uint64_t{1} << shift)) >> (shift + 1);
	uint64_t integer = whole;
	if (fraction >= scale)
	{
		integer++;
		fraction -= scale;
	}
	std::string digits = std::to_string(fraction);
	digits.insert(0, static_cast<size_t>(decimals) - digits.size(), '0');
	if (trim)
		while (!digits.empty() && digits.back() == '0') digits.pop_back();
	const bool zero = integer == 0 && digits.find_first_not_of('0') == std::string::npos;
	std::string out = (negative && !zero ? "-" : "") + std::to_string(integer);
	if (!digits.empty()) out += "." + digits;
	return out;
}

/* A float the way Chimera writes one: the shortest digits that read back as
 * the same float, written plainly unless its power of ten is 9 or more or
 * below -4, where it takes an exponent of at least two digits. A double the
 * same way, its shortest digits a double's. */
template <typename T> std::string shortestFloat(T f)
{
	if (std::isnan(f)) return "NaN";
	if (std::isinf(f)) return f < 0 ? "-Infinity" : "Infinity";
#ifdef __APPLE__
	const std::string sci = apple::shortestScientific(f);
#else
	char buf[64];
	const auto r = std::to_chars(buf, buf + sizeof buf, f, std::chars_format::scientific);
	const std::string sci(buf, r.ptr);
#endif
	const bool negative = sci[0] == '-';
	const size_t e = sci.find('e');
	std::string digits;
	for (size_t i = negative ? 1 : 0; i < e; i++)
		if (sci[i] != '.') digits += sci[i];
	const int exponent = std::atoi(sci.c_str() + e + 1);
	std::string out = negative ? "-" : "";
	if (exponent >= 9 || exponent < -4)
	{
		out += digits.substr(0, 1);
		if (digits.size() > 1) out += "." + digits.substr(1);
		char ex[16];
		std::snprintf(ex, sizeof ex, "E%c%02d", exponent < 0 ? '-' : '+', exponent < 0 ? -exponent : exponent);
		return out + ex;
	}
	if (exponent < 0) return out + "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
	const size_t point = static_cast<size_t>(exponent) + 1;
	if (digits.size() <= point) return out + digits + std::string(point - digits.size(), '0');
	return out + digits.substr(0, point) + "." + digits.substr(point);
}

} // namespace

std::string format(uint64_t value, int size, Display display)
{
	char buf[32];
	const uint64_t mask = size == 1 ? 0xFFu : size == 2 ? 0xFFFFu : size == 4 ? 0xFFFFFFFFu : ~uint64_t{0};
	switch (display)
	{
	case Display::Unsigned: return std::to_string(value & mask);
	case Display::Signed:
		return std::to_string(size == 1	  ? static_cast<int64_t>(static_cast<int8_t>(value))
							  : size == 2 ? static_cast<int64_t>(static_cast<int16_t>(value))
							  : size == 4 ? static_cast<int64_t>(static_cast<int32_t>(value))
										  : static_cast<int64_t>(value));
	case Display::Hex:
		std::snprintf(buf, sizeof buf, "%0*llX", size * 2, static_cast<unsigned long long>(value & mask));
		return buf;
	case Display::Binary: return binary(static_cast<uint32_t>(value), size * 8);
	case Display::Fixed12_4: return fixedPoint(static_cast<int16_t>(value), 4, 4, false);
	case Display::Fixed20_12: return fixedPoint(static_cast<int32_t>(value), 12, 6, true);
	case Display::Fixed16_16: return fixedPoint(static_cast<int32_t>(value), 16, 6, true);
	case Display::Float:
	{
		const uint32_t bits = static_cast<uint32_t>(value);
		float f;
		std::memcpy(&f, &bits, sizeof f);
		return shortestFloat(f);
	}
	case Display::Double:
	{
		double d;
		std::memcpy(&d, &value, sizeof d);
		return shortestFloat(d);
	}
	}
	return std::to_string(value);
}

namespace {
std::string trimmed(const std::string &s)
{
	const size_t a = s.find_first_not_of(" \t\r\n");
	if (a == std::string::npos) return {};
	return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

bool wholeNumber(const std::string &t, int base, long long low, long long high, long long &out)
{
	if (t.empty()) return false;
	const char *b = t.c_str();
	if (base != 10 && (*b == '+' || *b == '-')) return false;
	/* hex digits only: no 0x in front, as Chimera's boxes take none */
	if (base == 16 && t.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
	char *end = nullptr;
	errno = 0;
	const long long v = std::strtoll(b, &end, base);
	if (end == b || *end != '\0' || errno == ERANGE || v < low || v > high) return false;
	out = v;
	return true;
}

bool wholeUnsigned(const std::string &t, int base, unsigned long long &out)
{
	if (t.empty() || t[0] == '+' || t[0] == '-') return false;
	if (base == 16 && t.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
	if (base == 10 && t.find_first_not_of("0123456789") != std::string::npos) return false;
	char *end = nullptr;
	errno = 0;
	const unsigned long long v = std::strtoull(t.c_str(), &end, base);
	if (end == t.c_str() || *end != '\0' || errno == ERANGE) return false;
	out = v;
	return true;
}

bool decimal(const std::string &t, double &out)
{
	if (t.empty()) return false;
	char *end = nullptr;
	out = std::strtod(t.c_str(), &end);
	return end != t.c_str() && *end == '\0' && std::isfinite(out);
}
} // namespace

bool parse(const std::string &text, Display display, uint64_t &out, int size)
{
	const std::string t = trimmed(text);
	long long whole = 0;
	unsigned long long wide = 0;
	double d = 0;
	/* 8 bytes: the whole 64 bits */
	if (size == 8)
		switch (display)
		{
		case Display::Unsigned:
			if (!wholeUnsigned(t, 10, wide)) return false;
			out = wide;
			return true;
		case Display::Signed:
			if (!wholeNumber(t, 10, LLONG_MIN, LLONG_MAX, whole)) return false;
			out = static_cast<uint64_t>(whole);
			return true;
		case Display::Hex:
			if (t.size() > 16 || !wholeUnsigned(t, 16, wide)) return false;
			out = wide;
			return true;
		default: break;
		}
	switch (display)
	{
	case Display::Unsigned:
		if (!wholeNumber(t, 10, 0, 0xFFFFFFFFll, whole)) return false;
		out = static_cast<uint32_t>(whole);
		return true;
	case Display::Signed:
		if (!wholeNumber(t, 10, INT32_MIN, INT32_MAX, whole)) return false;
		out = static_cast<uint32_t>(static_cast<int32_t>(whole));
		return true;
	case Display::Hex:
		if (t.size() > 8 || !wholeNumber(t, 16, 0, 0xFFFFFFFFll, whole)) return false;
		out = static_cast<uint32_t>(whole);
		return true;
	case Display::Binary:
		if (t.size() > 32 || t.find_first_not_of("01") != std::string::npos || !wholeNumber(t, 2, 0, 0xFFFFFFFFll, whole)) return false;
		out = static_cast<uint32_t>(whole);
		return true;
	case Display::Fixed12_4:
	case Display::Fixed20_12:
	case Display::Fixed16_16:
	{
		if (!decimal(t, d)) return false;
		const double scale = display == Display::Fixed12_4 ? 16.0 : display == Display::Fixed20_12 ? 4096.0 : 65536.0;
		const double v = std::trunc(d * scale);
		if (v < INT32_MIN || v > INT32_MAX) return false;
		out = static_cast<uint32_t>(static_cast<int32_t>(v));
		return true;
	}
	case Display::Float:
	{
		if (!decimal(t, d) || std::fabs(d) > 3.4028235677973366e38) return false;
		const float f = static_cast<float>(d);
		uint32_t bits = 0;
		std::memcpy(&bits, &f, sizeof bits);
		out = bits;
		return true;
	}
	case Display::Double:
		if (!decimal(t, d)) return false;
		std::memcpy(&out, &d, sizeof out);
		return true;
	}
	return false;
}

int addressDigits(int64_t number)
{
	if (number < 0) return 8;
	if (number < 0x100) return 2;
	if (number < 0x10000) return 4;
	if (number < 0x1000000) return 6;
	if (number < 0x100000000ll) return 8;
	return 16;
}

std::string hex(int64_t number, int digits)
{
	char buf[32];
	std::snprintf(buf, sizeof buf, "%0*llX", digits, static_cast<unsigned long long>(number));
	return buf;
}

char sizeLetter(int size)
{
	return size == 1 ? 'b' : size == 2 ? 'w' : size == 4 ? 'd' : size == 8 ? 'q' : 'S';
}

int sizeFromLetter(char c)
{
	switch (c)
	{
	case 'b': return 1;
	case 'w': return 2;
	case 'd': return 4;
	case 'q': return 8;
	case 'S': return 0;
	default: return -1;
	}
}

char displayLetter(Display display)
{
	switch (display)
	{
	case Display::Unsigned: return 'u';
	case Display::Signed: return 's';
	case Display::Hex: return 'h';
	case Display::Binary: return 'b';
	case Display::Fixed12_4: return '1';
	case Display::Fixed20_12: return '2';
	case Display::Fixed16_16: return '3';
	case Display::Float: return 'f';
	case Display::Double: return 'F';
	}
	return '_';
}

bool displayFromLetter(char c, Display &out)
{
	switch (c)
	{
	case 'u': out = Display::Unsigned; return true;
	case 's': out = Display::Signed; return true;
	case 'h': out = Display::Hex; return true;
	case 'b': out = Display::Binary; return true;
	case '1': out = Display::Fixed12_4; return true;
	case '2': out = Display::Fixed20_12; return true;
	case '3': out = Display::Fixed16_16; return true;
	case 'f': out = Display::Float; return true;
	case 'F': out = Display::Double; return true;
	default: return false;
	}
}

std::string thousands(int64_t n)
{
	std::string digits = std::to_string(n < 0 ? -n : n), out;
	for (size_t i = 0; i < digits.size(); i++)
	{
		if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
		out += digits[i];
	}
	return n < 0 ? "-" + out : out;
}

std::string displayName(Display display)
{
	switch (display)
	{
	case Display::Unsigned: return tr("watch.unsigned");
	case Display::Signed: return tr("watch.signed");
	case Display::Hex: return tr("watch.hex");
	case Display::Binary: return tr("watch.binary");
	case Display::Fixed12_4: return tr("watch.fixed_12_4");
	case Display::Fixed20_12: return tr("watch.fixed_20_12");
	case Display::Fixed16_16: return tr("watch.fixed_16_16");
	case Display::Float: return tr("watch.float");
	case Display::Double: return tr("watch.double");
	}
	return {};
}

} // namespace chimera::vm::watch
