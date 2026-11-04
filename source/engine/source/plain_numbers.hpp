#pragma once
/* plain_numbers.hpp - a fraction as text, the same on every machine.
 *
 * strtod and printf use the decimal separator of the process's locale, and a
 * frontend may put the process in the user's. Text the
 * engine shows, stores or accepts for a number must not depend on that: a
 * value typed as 1.5 is 1.5 where the system writes 1,5 too, and a file
 * written on one machine reads on another (issue 244).
 */

#include <charconv>
#include <string>
#include <system_error>

#ifdef __APPLE__
/* Apple's C++ library has the floating-point from_chars and to_chars only from
 * macOS 26, and Chimera runs on 14.4 and later (what Qt asks of it). There the
 * C library's strtod_l and snprintf_l are used in the "C" locale, which read and
 * write the same text. */
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <xlocale.h>
#endif

namespace chimera
{
/* Like strtod on the WHOLE of `text`, in no locale: an optional sign, then a
 * decimal or scientific number, a hex one after 0x, inf or nan. False when
 * the text is not exactly one number. */
inline bool parsePlainDouble(const std::string &text, double &out)
{
	const char *first = text.data();
	const char *last = first + text.size();
	bool negative = false;
	if (first != last && (*first == '+' || *first == '-'))
	{
		negative = *first == '-';
		first++;
	}
	if (first == last || *first == '+' || *first == '-') return false;
#ifdef __APPLE__
	const char *const numberStart = first;
#endif
	std::chars_format format = std::chars_format::general;
	if (last - first > 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X'))
	{
		format = std::chars_format::hex;
		first += 2;
		/* from_chars takes a sign of its own, and inf and nan, after the 0x:
		 * "0x-1" would be -1 and "-0x-1" +1. A hex number starts with a digit
		 * or the point. */
		const char c = *first;
		const bool hexStart = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '.';
		if (!hexStart) return false;
	}
	double value = 0;
#ifdef __APPLE__
	(void)format;
	/* strtod skips white space first, as from_chars does not; and it sets ERANGE
	 * where from_chars says out of range, an overflow or an underflow */
	if (std::isspace(static_cast<unsigned char>(*numberStart))) return false;
	static const locale_t plain = newlocale(LC_ALL_MASK, "C", nullptr);
	char *end = nullptr;
	errno = 0;
	value = strtod_l(numberStart, &end, plain);
	if (errno == ERANGE || end != last) return false;
#else
	const auto result = std::from_chars(first, last, value, format);
	if (result.ec != std::errc() || result.ptr != last) return false;
#endif
	out = negative ? -value : value;
	return true;
}

/* Like snprintf("%.*g", digits, v), in no locale. */
inline std::string formatPlainDouble(double v, int digits)
{
	char buf[64];
#ifdef __APPLE__
	static const locale_t plain = newlocale(LC_ALL_MASK, "C", nullptr);
	const int n = snprintf_l(buf, sizeof buf, plain, "%.*g", digits, v);
	if (n < 0 || n >= static_cast<int>(sizeof buf)) return "0";
	return std::string(buf, static_cast<size_t>(n));
#else
	const auto result = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, digits);
	if (result.ec != std::errc()) return "0";
	return std::string(buf, result.ptr);
#endif
}
} // namespace chimera
