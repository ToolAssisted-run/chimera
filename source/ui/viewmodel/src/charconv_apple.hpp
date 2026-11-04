#pragma once
/* charconv_apple.hpp - the floating-point from_chars and to_chars, for macOS.
 *
 * Apple's C++ library has them only from macOS 26, and Chimera runs on 14.4 and
 * later. These write and read the same text with the C library's strtod_l and
 * snprintf_l in the "C" locale, which do not follow the user's decimal comma
 * (issue 244, as plain_numbers.hpp in the engine). Used only where
 * __APPLE__ is defined; elsewhere the standard ones are.
 *
 * shortestScientific is the digits of the shortest decimal that reads back as
 * the same value, in the form std::to_chars(v, chars_format::scientific) gives
 * ("1.5e-07"); shortest is std::to_chars(v)'s: scientific or plain, whichever
 * is the shorter, plain when they tie.
 */

#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <xlocale.h>

namespace chimera::vm::apple
{
inline locale_t plainLocale()
{
	static const locale_t plain = newlocale(LC_ALL_MASK, "C", nullptr);
	return plain;
}

/* the number at the start of text, as from_chars reads one: false (and `out`
 * as it was) when there is none */
inline bool parsePrefix(const char *first, const char *last, double &out)
{
	const std::string text(first, last);
	if (text.empty() || std::isspace(static_cast<unsigned char>(text[0])) || text[0] == '+') return false;
	char *end = nullptr;
	errno = 0;
	const double v = strtod_l(text.c_str(), &end, plainLocale());
	if (end == text.c_str() || errno == ERANGE) return false;
	out = v;
	return true;
}

template <typename T> std::string shortestScientific(T v)
{
	char buf[64];
	/* the fewest digits that read back as the same value */
	for (int digits = 0; digits <= 17; digits++)
	{
		snprintf_l(buf, sizeof buf, plainLocale(), "%.*e", digits, static_cast<double>(v));
		char *end = nullptr;
		const T back = sizeof(T) == sizeof(float) ? static_cast<T>(strtof_l(buf, &end, plainLocale()))
		                                          : static_cast<T>(strtod_l(buf, &end, plainLocale()));
		if (back == v) break;
	}
	/* "%e" writes at least two exponent digits, as to_chars does */
	return buf;
}

template <typename T> std::string shortest(T v)
{
	const std::string sci = shortestScientific(v);
	if (v == 0) return std::signbit(v) ? "-0" : "0";
	const bool negative = sci[0] == '-';
	const size_t e = sci.find('e');
	std::string digits;
	for (size_t i = negative ? 1 : 0; i < e; i++)
		if (sci[i] != '.') digits += sci[i];
	const int exponent = std::atoi(sci.c_str() + e + 1);
	const int n = static_cast<int>(digits.size());
	std::string plain = negative ? "-" : "";
	if (exponent >= 0)
	{
		if (n <= exponent + 1)
		{
			/* a whole number is written as what it is exactly, not as its shortest digits and zeros */
			char whole[400];
			snprintf_l(whole, sizeof whole, plainLocale(), "%.0f", static_cast<double>(v < 0 ? -v : v));
			plain += whole;
		}
		else plain += digits.substr(0, static_cast<size_t>(exponent + 1)) + "." + digits.substr(static_cast<size_t>(exponent + 1));
	}
	else plain += "0." + std::string(static_cast<size_t>(-exponent - 1), '0') + digits;
	/* the shorter wins, the plain one a tie */
	return plain.size() <= sci.size() ? plain : sci;
}
} // namespace chimera::vm::apple
