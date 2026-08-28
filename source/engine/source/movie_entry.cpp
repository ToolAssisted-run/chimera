/* movie_entry.cpp - see movie_entry.hpp. Transliterated from what
 * Bk2Controller.SetFromMnemonic / GenerateMnemonic did in C#, byte for byte:
 * a movie written before this moved into the engine must parse and regenerate
 * identically, so the walk is kept literal rather than tidied.
 */

#include "movie_entry.hpp"

#include <algorithm>
#include <cstring>

namespace chimera {

namespace {

/* int parse for an axis field: optional spaces (the padding), sign, digits */
bool parseAxisValue(const char *begin, const char *end, int32_t &out)
{
	while (begin < end && *begin == ' ') begin++;
	if (begin >= end) return false;
	bool negative = false;
	if (*begin == '+' || *begin == '-')
	{
		negative = *begin == '-';
		if (++begin >= end) return false;
	}
	int64_t value = 0;
	for (; begin < end; begin++)
	{
		if (*begin < '0' || *begin > '9') return false;
		value = value * 10 + (*begin - '0');
		if (value > 2147483648LL) return false;
	}
	out = static_cast<int32_t>(negative ? -value : value);
	return true;
}

} // namespace

int32_t playerNumberOf(const std::string &name)
{
	if (name.size() < 3 || name[0] != 'P') return 0;
	size_t i = 1;
	int32_t value = 0;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9')
	{
		value = value * 10 + (name[i] - '0');
		i++;
	}
	return i > 1 && i < name.size() && name[i] == ' ' ? value : 0;
}

void EntryLayout::build(const std::vector<std::string> &buttons, const std::vector<EntryAxis> &axes)
{
	items_.clear();
	groupStarts_.clear();
	axes_ = axes;
	buttonCount_ = buttons.size();

	int32_t maxPlayer = 0;
	for (const auto &a : axes) maxPlayer = std::max(maxPlayer, playerNumberOf(a.name));
	for (const auto &b : buttons) maxPlayer = std::max(maxPlayer, playerNumberOf(b));
	groups_ = maxPlayer + 1;
	for (int32_t g = 0; g < groups_; g++)
	{
		groupStarts_.push_back(static_cast<int32_t>(items_.size()));
		for (int32_t i = 0; i < static_cast<int32_t>(axes.size()); i++)
		{
			if (playerNumberOf(axes[static_cast<size_t>(i)].name) == g) items_.push_back({ true, i });
		}
		for (int32_t i = 0; i < static_cast<int32_t>(buttons.size()); i++)
		{
			if (playerNumberOf(buttons[static_cast<size_t>(i)]) == g) items_.push_back({ false, i });
		}
	}
}

bool EntryLayout::parse(const char *entry, std::vector<uint8_t> &buttonsOut, std::vector<int32_t> &axesOut) const
{
	buttonsOut.assign(buttonCount_, 0);
	axesOut.assign(axes_.size(), 0);
	for (size_t i = 0; i < axes_.size(); i++) axesOut[i] = axes_[i].neutral;
	const char *p = entry;
	for (const auto &item : items_)
	{
		while (*p == '|') p++;
		if (*p == '\0') return false; // entry shorter than the controller
		if (item.isAxis)
		{
			const char *comma = std::strchr(p, ',');
			if (comma == nullptr) return false;
			int32_t value;
			if (!parseAxisValue(p, comma, value)) return false;
			axesOut[static_cast<size_t>(item.index)] = value;
			p = comma + 1;
		}
		else
		{
			if (*p != '.') buttonsOut[static_cast<size_t>(item.index)] = 1;
			p++;
		}
	}
	return true;
}

std::string EntryLayout::generate(const uint8_t *buttons, const int32_t *axes, const std::string &mnemonics) const
{
	std::string out;
	out.push_back('|');
	size_t next = 0;
	for (int32_t g = 0; g < groups_; g++)
	{
		size_t end = g + 1 < groups_
			? static_cast<size_t>(groupStarts_[static_cast<size_t>(g) + 1])
			: items_.size();
		for (; next < end; next++)
		{
			const auto &item = items_[next];
			if (item.isAxis)
			{
				int32_t value = axes != nullptr ? axes[item.index] : axes_[static_cast<size_t>(item.index)].neutral;
				std::string text = std::to_string(value);
				while (text.size() < 5) text.insert(text.begin(), ' '); // PadLeft(5)
				out.append(text).push_back(',');
			}
			else
			{
				char mnemonic = static_cast<size_t>(item.index) < mnemonics.size()
					? mnemonics[static_cast<size_t>(item.index)]
					: '!';
				bool pressed = buttons != nullptr && buttons[static_cast<size_t>(item.index)] != 0;
				out.push_back(pressed ? mnemonic : '.');
			}
		}
		out.push_back('|');
	}
	return out;
}

} // namespace chimera
