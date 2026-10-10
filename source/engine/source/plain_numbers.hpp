#pragma once
/* plain_numbers.hpp - a fraction as text, the same on every machine.
 *
 * strtod and printf use the decimal separator of the process's locale, and a
 * frontend's runtime may put the process in the user's (Mono does). Text the
 * engine shows, stores or accepts for a number must not depend on that: a
 * value typed as 1.5 is 1.5 where the system writes 1,5 too, and a file
 * written on one machine reads on another (issue 244).
 */

#include <charconv>
#include <string>
#include <system_error>

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
	std::chars_format format = std::chars_format::general;
	if (last - first > 2 && first[0] == '0' && (first[1] == 'x' || first[1] == 'X'))
	{
		format = std::chars_format::hex;
		first += 2;
	}
	double value = 0;
	const auto result = std::from_chars(first, last, value, format);
	if (result.ec != std::errc() || result.ptr != last) return false;
	out = negative ? -value : value;
	return true;
}

/* Like snprintf("%.*g", digits, v), in no locale. */
inline std::string formatPlainDouble(double v, int digits)
{
	char buf[64];
	const auto result = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::general, digits);
	if (result.ec != std::errc()) return "0";
	return std::string(buf, result.ptr);
}
} // namespace chimera
