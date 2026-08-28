/* movie_entry - the Bk2 input-entry format, as a pure function of a
 * controller's declaration.
 *
 * This is movie-correctness data: what "|....U...|" means for a given set of
 * buttons and axes, and what a given machine input generates. It lives apart
 * from the session so it can be tested with byte-exact fixtures against
 * controllers no core in the tree declares - axes especially, which the synth
 * witness core has none of.
 *
 * The layout rule is BizHawk's ControllerDefinition.GenOrderedControls:
 * controls grouped by player number (0 = console), axes before buttons within
 * a group, both in declaration order.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chimera {

struct EntryAxis
{
	std::string name;
	int32_t min = 0, max = 0, neutral = 0;
};

/* ControllerDefinition.PlayerNumber: "^P(\d+) " captures the player, else 0 */
int32_t playerNumberOf(const std::string &name);

/* The '|'-delimited groups an entry is written in, and where each control
 * sits inside them. Build once per controller, then parse/generate with it. */
class EntryLayout
{
public:
	void build(const std::vector<std::string> &buttons, const std::vector<EntryAxis> &axes);

	/* Positional and '|'-tolerant - the exact Bk2Controller.SetFromMnemonic
	 * walk. false when the entry runs out before the controller does, or an
	 * axis field will not parse. buttonsOut is sized to the button count
	 * (one byte per button, 0/1, declaration order - a DOS keyboard is wider
	 * than any packed word); axesOut is sized to the axis count and
	 * pre-filled with each axis's neutral. */
	bool parse(const char *entry, std::vector<uint8_t> &buttonsOut, std::vector<int32_t> &axesOut) const;

	/* mnemonics gives the character a pressed button generates, one per button
	 * in declaration order; a button past the end of the string generates '!'.
	 * buttons carries buttonCount() 0/1 states (null = all released); axes may
	 * be null for all-neutral. */
	std::string generate(const uint8_t *buttons, const int32_t *axes, const std::string &mnemonics) const;

	int32_t groups() const { return groups_; }
	size_t size() const { return items_.size(); }
	size_t buttonCount() const { return buttonCount_; }

private:
	struct Item
	{
		bool isAxis;
		int32_t index; // into buttons / axes
	};
	std::vector<Item> items_;
	std::vector<int32_t> groupStarts_;
	std::vector<EntryAxis> axes_;
	int32_t groups_ = 0;
	size_t buttonCount_ = 0;
};

} // namespace chimera
