/* control_names.cpp - see control_names.hpp */
#include "control_names.hpp"

#include "movie_entry.hpp"

#include <cstdint>
#include <vector>

namespace chimera {

std::string bareControlName(const std::string &name)
{
	if (playerNumberOf(name) == 0) return name;
	return name.substr(name.find(' ') + 1);
}

bool usableMnemonic(char c)
{
	return c > ' ' && c < 0x7F && c != '.' && c != '|';
}

/* Reads the UTF-8 character at s[i] and moves i past it. The whole of it must
 * be there, in its shortest form, no surrogate, nothing past U+10FFFF.
 * Returns its code point, or -1. */
static int64_t nextCodePoint(const std::string &s, size_t &i)
{
	const unsigned char c = static_cast<unsigned char>(s[i]);
	size_t extra = 0;
	uint32_t cp = 0, least = 0;
	if (c < 0x80) { i++; return c; }
	else if (c >= 0xC2 && c < 0xE0) { extra = 1; cp = c & 0x1Fu; least = 0x80; }
	else if (c >= 0xE0 && c < 0xF0) { extra = 2; cp = c & 0x0Fu; least = 0x800; }
	else if (c >= 0xF0 && c < 0xF5) { extra = 3; cp = c & 0x07u; least = 0x10000; }
	else return -1;
	if (i + extra >= s.size()) return -1;
	for (size_t k = 1; k <= extra; k++)
	{
		const unsigned char d = static_cast<unsigned char>(s[i + k]);
		if ((d & 0xC0) != 0x80) return -1;
		cp = (cp << 6) | (d & 0x3Fu);
	}
	if (cp < least || cp > 0x10FFFF || (cp >= 0xD800 && cp < 0xE000)) return -1;
	i += extra + 1;
	return cp;
}

/* A character a header may hold: not a control character, not a space of any
 * width, not one that only steers text (a joiner, a direction mark), not a
 * code point Unicode keeps out of text. */
static bool printableCodePoint(int64_t cp)
{
	if (cp <= ' ' || cp == 0x7F) return false;                 /* controls, space, DEL */
	if (cp >= 0x80 && cp < 0xA0) return false;                 /* C1 controls */
	if (cp == 0xA0 || cp == 0xAD || cp == 0x1680) return false; /* no-break space, soft hyphen, ogham space */
	if (cp >= 0x2000 && cp <= 0x200F) return false;            /* spaces, zero-width and direction marks */
	if (cp >= 0x2028 && cp <= 0x202F) return false;            /* separators, direction embeddings, narrow space */
	if (cp >= 0x205F && cp <= 0x206F) return false;            /* medium space, invisible operators, isolates */
	if (cp == 0x3000 || cp == 0xFEFF) return false;            /* ideographic space, byte order mark */
	if (cp >= 0xFDD0 && cp <= 0xFDEF) return false;            /* noncharacters */
	if ((cp & 0xFFFE) == 0xFFFE) return false;                 /* U+FFFE and U+FFFF of every plane */
	if (cp >= 0xE000 && cp <= 0xF8FF) return false;            /* private use */
	return true;
}

bool usableButtonHeader(const std::string &header)
{
	size_t characters = 0, at = 0;
	while (at < header.size())
	{
		const size_t start = at;
		const int64_t cp = nextCodePoint(header, at);
		/* a space between two characters is allowed ("F 1"), at either end it is not */
		const bool inner = cp == ' ' && start > 0 && at < header.size();
		if (cp < 0 || (!inner && !printableCodePoint(cp))) return false;
		if (++characters > 8) return false;
	}
	return characters > 0;
}

namespace {

std::vector<std::string> wordsOf(const std::string &name)
{
	std::vector<std::string> words;
	std::string word;
	for (char c : name)
	{
		if (c == ' ')
		{
			if (!word.empty()) words.push_back(word);
			word.clear();
		}
		else word.push_back(c);
	}
	if (!word.empty()) words.push_back(word);
	return words;
}

} // namespace

char genericMnemonic(const std::string &name)
{
	const std::vector<std::string> words = wordsOf(bareControlName(name));
	for (auto it = words.rbegin(); it != words.rend(); ++it)
	{
		if (usableMnemonic((*it)[0])) return (*it)[0];
	}
	return '?';
}

std::string genericAxisHeader(const std::string &name)
{
	std::string out, word;
	const auto flush = [&] {
		if (word.empty()) return;
		/* "P1", "X", "Y", "L", "R" say everything they have to say */
		if (word.size() <= 2) out += word;
		else out.push_back(word[0]);
		word.clear();
	};
	for (char c : name)
	{
		if (c == ' ' || c == '-' || c == '_') flush();
		else word.push_back(c);
	}
	flush();
	return out.empty() ? name : out;
}

int32_t ControlNames::playerOf(const std::string &control) const
{
	if (!playersDeclared) return playerNumberOf(control);
	const auto it = players.find(control);
	return it != players.end() ? it->second : 0;
}

char ControlNames::mnemonicOf(const std::string &button) const
{
	auto it = mnemonics.find(button);
	if (it == mnemonics.end()) it = mnemonics.find(bareControlName(button));
	return it != mnemonics.end() ? it->second : genericMnemonic(button);
}

std::string ControlNames::buttonHeaderOf(const std::string &button) const
{
	auto it = buttonHeaders.find(button);
	if (it == buttonHeaders.end()) it = buttonHeaders.find(bareControlName(button));
	return it != buttonHeaders.end() ? it->second : std::string(1, mnemonicOf(button));
}

std::string ControlNames::axisHeaderOf(const std::string &axis) const
{
	const auto it = axisHeaders.find(axis);
	return it != axisHeaders.end() ? it->second : genericAxisHeader(axis);
}

} // namespace chimera
