/* test_movie_entry.cpp - pins the Bk2 input-entry format, byte for byte.
 *
 * The fixtures are what BizHawk's Bk2Controller produced and accepted, so a
 * movie recorded before any of this moved into the engine still parses and
 * still regenerates identically.
 *
 * This is where AXES are witnessed. No core in this tree declares any (the
 * synth witness core is buttons-only), so the gate cannot reach the axis
 * paths end to end; here they are exercised directly - padding, sign,
 * neutral fill, the axes-before-buttons order, and the round trip.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/movie_entry.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using chimera::EntryAxis;
using chimera::EntryLayout;

namespace {

EntryLayout layoutOf(const std::vector<std::string> &buttons, const std::vector<EntryAxis> &axes)
{
	EntryLayout l;
	l.build(buttons, axes);
	return l;
}

EntryAxis axis(const char *name, int32_t min, int32_t max, int32_t neutral)
{
	EntryAxis a;
	a.name = name;
	a.min = min;
	a.max = max;
	a.neutral = neutral;
	return a;
}

/* the synth witness controller */
const std::vector<std::string> kSynthButtons = {
	"P1 Up", "P1 Down", "P1 Left", "P1 Right", "P1 A", "P1 B", "P1 Select", "P1 Start",
};

} // namespace

int main(void)
{
	{ // player number: "^P(\d+) " and nothing else
		assert(chimera::playerNumberOf("P1 Up") == 1);
		assert(chimera::playerNumberOf("P12 Up") == 12);
		assert(chimera::playerNumberOf("Reset") == 0);
		assert(chimera::playerNumberOf("Power") == 0);
		assert(chimera::playerNumberOf("P1") == 0);    // no space, no player
		assert(chimera::playerNumberOf("PX Up") == 0); // no digits, no player
	}

	{ // buttons only, one player - the synth core's own controller
		EntryLayout l = layoutOf(kSynthButtons, {});
		assert(l.groups() == 2); // console group (empty) + P1
		assert(l.size() == 8);

		uint64_t mask = 0;
		std::vector<int32_t> axes;
		assert(l.parse("|.U......|", mask, axes));
		assert(mask == (1ull << 1)); // Down is button index 1
		assert(axes.empty());

		assert(l.parse("|UDLRAB.S|", mask, axes));
		assert(mask == 0xBFull); // everything but Select

		// Generate is the inverse - but the CHARACTER comes from the frontend's
		// mnemonic vocabulary, not from whatever the parsed entry happened to
		// carry: parse only asks "is this position a dot".
		// The leading empty group is the console group (no Reset/Power here);
		// C#'s GenOrderedControls emits empty player groups too, and a movie
		// written by either side must have the same separators.
		assert(l.generate(1ull << 1, nullptr, "UDLRABsS") == "||.D......|");
	}

	{ // a console group that is not empty: Reset/Power come first, in their own group
		EntryLayout l = layoutOf({ "Reset", "Power", "P1 Up", "P1 Down" }, {});
		assert(l.groups() == 2);
		uint64_t mask = 0;
		std::vector<int32_t> axes;
		assert(l.parse("|r.|.D|", mask, axes));
		assert(mask == ((1ull << 0) | (1ull << 3))); // Reset and P1 Down
		assert(l.generate((1ull << 0) | (1ull << 3), nullptr, "rpUD") == "|r.|.D|");
	}

	{ // AXES: written before the buttons of their group, PadLeft(5) then a comma
		EntryLayout l = layoutOf(
			{ "P1 A", "P1 B" },
			{ axis("P1 Stick X", -128, 127, 0), axis("P1 Stick Y", -128, 127, 0) });
		assert(l.size() == 4);

		uint64_t mask = 0;
		std::vector<int32_t> axes;
		assert(l.parse("|    0,    0,..|", mask, axes));
		assert(mask == 0);
		assert(axes.size() == 2 && axes[0] == 0 && axes[1] == 0);

		assert(l.parse("|  127, -128,A.|", mask, axes));
		assert(mask == 1ull);
		assert(axes[0] == 127 && axes[1] == -128);

		// an explicit + sign parses, as it did in C#
		assert(l.parse("|  +42,   -7,..|", mask, axes));
		assert(axes[0] == 42 && axes[1] == -7);

		// generate pads to 5 and puts axes first
		const int32_t values[2] = { 127, -128 };
		assert(l.generate(1ull, values, "AB") == "||  127, -128,A.|");

		// a null axis pointer means every axis at its neutral
		assert(l.generate(0, nullptr, "AB") == "||    0,    0,..|");
	}

	{ // a non-zero neutral is what an absent or unparsed axis falls back to
		EntryLayout l = layoutOf({}, { axis("P1 Throttle", 0, 255, 128) });
		std::vector<int32_t> axes;
		uint64_t mask = 0;
		assert(l.parse("|  200,|", mask, axes));
		assert(axes[0] == 200);
		assert(l.generate(0, nullptr, "") == "||  128,|");
	}

	{ // several players: each group is axes-then-buttons, groups in player order
		EntryLayout l = layoutOf(
			{ "Reset", "P1 A", "P2 A" },
			{ axis("P2 Stick", -128, 127, 0), axis("P1 Stick", -128, 127, 0) });
		assert(l.groups() == 3);
		uint64_t mask = 0;
		std::vector<int32_t> axes;
		assert(l.parse("|r|   10,A|  -20,A|", mask, axes));
		assert(mask == 0x7ull);         // all three buttons
		assert(axes[1] == 10);          // P1 Stick, declared second
		assert(axes[0] == -20);         // P2 Stick, declared first
		const int32_t values[2] = { -20, 10 };
		assert(l.generate(0x7ull, values, "rAA") == "|r|   10,A|  -20,A|");
	}

	{ // round trip: parse then generate reproduces the entry exactly
		EntryLayout l = layoutOf(
			{ "P1 A", "P1 B" },
			{ axis("P1 Stick X", -128, 127, 0) });
		// in canonical (generated) form: the empty console group's separator is
		// part of what a written movie carries, even though parse tolerates its
		// absence - the hand-written fixtures in tests/synth/movies omit it
		const char *entries[] = { "||    0,..|", "||  -99,AB|", "||  127,.B|" };
		for (const char *entry : entries)
		{
			uint64_t mask = 0;
			std::vector<int32_t> axes;
			assert(l.parse(entry, mask, axes));
			assert(l.generate(mask, axes.data(), "AB") == entry);
		}
	}

	{ // refusals: an entry that runs out, and an axis field that will not parse
		EntryLayout l = layoutOf({ "P1 A", "P1 B" }, { axis("P1 Stick", -128, 127, 0) });
		uint64_t mask = 0;
		std::vector<int32_t> axes;
		assert(!l.parse("|    0,A|", mask, axes));  // one button short
		assert(!l.parse("|", mask, axes));
		assert(!l.parse("|zzzzz,AB|", mask, axes)); // not a number
		assert(!l.parse("|     ,AB|", mask, axes)); // blank axis field
		assert(!l.parse("|    0AB|", mask, axes));  // no comma ending the axis
	}

	{ // a button past the end of the mnemonic vocabulary is visibly wrong, not silent
		EntryLayout l = layoutOf(kSynthButtons, {});
		assert(l.generate(0xFFull, nullptr, "UD") == "||UD!!!!!!|");
		// and the same entry still parses, since parse ignores the character
		uint64_t mask = 0;
		std::vector<int32_t> axes;
		assert(l.parse("||UD!!!!!!|", mask, axes) && mask == 0xFFull);
	}

	std::printf("test_movie_entry: ok\n");
	return 0;
}
