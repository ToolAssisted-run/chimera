/* control_names.cpp - see control_names.hpp */
#include "control_names.hpp"

#include "movie_entry.hpp"

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

bool usableButtonHeader(const std::string &header)
{
	if (header.empty() || header.size() > 8) return false;
	if (header.front() == ' ' || header.back() == ' ') return false;
	for (char c : header)
	{
		if (c < ' ' || c >= 0x7F) return false;
	}
	return true;
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
