#pragma once
/* A value in memory as a person reads it: 1, 2 or 4 bytes shown unsigned,
 * signed, in hex, in binary, as fixed point or as a float, and 8 bytes
 * unsigned, signed, in hex or as a double (issue 474) - written exactly
 * as Chimera's RAM Watch writes them, so a value reads the same in either
 * window. RAM Watch, RAM Search and the hex editor share it. */

#include <cstdint>
#include <string>
#include <vector>

namespace chimera::vm::watch {

enum class Display
{
	Unsigned,
	Signed,
	Hex,
	Binary,
	Fixed12_4,  /* 2 bytes */
	Fixed20_12, /* 4 bytes */
	Fixed16_16, /* 4 bytes */
	Float,      /* 4 bytes */
	Double,     /* 8 bytes */
};

/* the kinds a value of this many bytes can be shown as, in Chimera's order */
const std::vector<Display> &displaysFor(int size);
bool fits(int size, Display display);

/* the value, `size` bytes of it, as shown */
std::string format(uint64_t value, int size, Display display);

/* A value typed as it is shown, read as Chimera's value boxes read one:
 * decimal, signed decimal, hex, binary digits, a decimal number for the
 * fixed points (cut toward zero) and for a float. false when it does not
 * read. */
bool parse(const std::string &text, Display display, uint64_t &out, int size = 4);

/* how big an address in a domain of `domainSize` bytes is written: the
 * window shows the digits its last address needs, a file the digits of
 * its size (as Chimera writes them) */
int addressDigits(int64_t number);
std::string hex(int64_t number, int digits);

/* the letters a .wch file names them by: b w d q (S a separator, p a
 * game property), and u s h b 1 2 3 f F (_ a separator); q and F, 8 bytes
 * and a double, are Chimera's own */
char sizeLetter(int size);
int sizeFromLetter(char c); /* 0: a separator, -1: a property or not one */
char displayLetter(Display display);
bool displayFromLetter(char c, Display &out);
/* a count as Chimera writes one, thousands apart: "1,024" */
std::string thousands(int64_t n);
/* the names the window shows, as Chimera writes them ("Hex", "Fixed Point 12.4") */
std::string displayName(Display display);

} // namespace chimera::vm::watch
