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
 * WIDE controllers are witnessed here too: buttons are a byte per control
 * with no 64 limit (a DOS keyboard is 101 keys), and the layout must keep
 * every byte-exact behaviour at any width.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/movie_entry.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
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

/* the packed view of a parse result, for fixtures pinned as masks */
uint64_t maskOf(const std::vector<uint8_t> &states)
{
	uint64_t mask = 0;
	for (size_t i = 0; i < states.size() && i < 64; i++)
	{
		if (states[i] != 0) mask |= 1ull << i;
	}
	return mask;
}

/* the wide view of a mask, for driving generate with pinned inputs */
std::vector<uint8_t> statesOf(uint64_t mask, size_t count)
{
	std::vector<uint8_t> s(count, 0);
	for (size_t i = 0; i < count && i < 64; i++) s[i] = (mask >> i) & 1;
	return s;
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
		assert(l.buttonCount() == 8);

		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(l.parse("|.U......|", states, axes));
		assert(maskOf(states) == (1ull << 1)); // Down is button index 1
		assert(axes.empty());

		assert(l.parse("|UDLRAB.S|", states, axes));
		assert(maskOf(states) == 0xBFull); // everything but Select

		// Generate is the inverse - but the CHARACTER comes from the frontend's
		// mnemonic vocabulary, not from whatever the parsed entry happened to
		// carry: parse only asks "is this position a dot".
		// The leading empty group is the console group (no Reset/Power here);
		// C#'s GenOrderedControls emits empty player groups too, and a movie
		// written by either side must have the same separators.
		auto down = statesOf(1ull << 1, l.buttonCount());
		assert(l.generate(down.data(), nullptr, "UDLRABsS") == "||.D......|");
	}

	{ // a console group that is not empty: Reset/Power come first, in their own group
		EntryLayout l = layoutOf({ "Reset", "Power", "P1 Up", "P1 Down" }, {});
		assert(l.groups() == 2);
		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(l.parse("|r.|.D|", states, axes));
		assert(maskOf(states) == ((1ull << 0) | (1ull << 3))); // Reset and P1 Down
		auto in = statesOf((1ull << 0) | (1ull << 3), l.buttonCount());
		assert(l.generate(in.data(), nullptr, "rpUD") == "|r.|.D|");
	}

	{ // AXES: written before the buttons of their group, PadLeft(5) then a comma
		EntryLayout l = layoutOf(
			{ "P1 A", "P1 B" },
			{ axis("P1 Stick X", -128, 127, 0), axis("P1 Stick Y", -128, 127, 0) });
		assert(l.size() == 4);

		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(l.parse("|    0,    0,..|", states, axes));
		assert(maskOf(states) == 0);
		assert(axes.size() == 2 && axes[0] == 0 && axes[1] == 0);

		assert(l.parse("|  127, -128,A.|", states, axes));
		assert(maskOf(states) == 1ull);
		assert(axes[0] == 127 && axes[1] == -128);

		// an explicit + sign parses, as it did in C#
		assert(l.parse("|  +42,   -7,..|", states, axes));
		assert(axes[0] == 42 && axes[1] == -7);

		// generate pads to 5 and puts axes first
		const int32_t values[2] = { 127, -128 };
		auto a = statesOf(1ull, l.buttonCount());
		assert(l.generate(a.data(), values, "AB") == "||  127, -128,A.|");

		// null buttons and axes mean everything released, every axis neutral
		assert(l.generate(nullptr, nullptr, "AB") == "||    0,    0,..|");
	}

	{ // a non-zero neutral is what an absent or unparsed axis falls back to
		EntryLayout l = layoutOf({}, { axis("P1 Throttle", 0, 255, 128) });
		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(l.parse("|  200,|", states, axes));
		assert(axes[0] == 200);
		assert(l.generate(nullptr, nullptr, "") == "||  128,|");
	}

	{ // several players: each group is axes-then-buttons, groups in player order
		EntryLayout l = layoutOf(
			{ "Reset", "P1 A", "P2 A" },
			{ axis("P2 Stick", -128, 127, 0), axis("P1 Stick", -128, 127, 0) });
		assert(l.groups() == 3);
		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(l.parse("|r|   10,A|  -20,A|", states, axes));
		assert(maskOf(states) == 0x7ull); // all three buttons
		assert(axes[1] == 10);            // P1 Stick, declared second
		assert(axes[0] == -20);           // P2 Stick, declared first
		const int32_t values[2] = { -20, 10 };
		auto in = statesOf(0x7ull, l.buttonCount());
		assert(l.generate(in.data(), values, "rAA") == "|r|   10,A|  -20,A|");
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
			std::vector<uint8_t> states;
			std::vector<int32_t> axes;
			assert(l.parse(entry, states, axes));
			assert(l.generate(states.data(), axes.data(), "AB") == entry);
		}
	}

	{ // refusals: an entry that runs out, and an axis field that will not parse
		EntryLayout l = layoutOf({ "P1 A", "P1 B" }, { axis("P1 Stick", -128, 127, 0) });
		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(!l.parse("|    0,A|", states, axes)); // one button short
		assert(!l.parse("|", states, axes));
		assert(!l.parse("|zzzzz,AB|", states, axes)); // not a number
		assert(!l.parse("|     ,AB|", states, axes)); // blank axis field
		assert(!l.parse("|    0AB|", states, axes));  // no comma ending the axis
	}

	{ // a button past the end of the mnemonic vocabulary is visibly wrong, not silent
		EntryLayout l = layoutOf(kSynthButtons, {});
		auto all = statesOf(0xFFull, l.buttonCount());
		assert(l.generate(all.data(), nullptr, "UD") == "||UD!!!!!!|");
		// and the same entry still parses, since parse ignores the character
		std::vector<uint8_t> states;
		std::vector<int32_t> axes;
		assert(l.parse("||UD!!!!!!|", states, axes) && maskOf(states) == 0xFFull);
	}

	{ // WIDE: 101 buttons - a DOS keyboard's worth - far past any packed word.
	  // Buttons 64 and above must parse, generate, and round-trip exactly like
	  // the first 64; a movie column exists for every declared control.
		std::vector<std::string> keys;
		std::string mnemonics;
		for (int i = 0; i < 101; i++)
		{
			keys.push_back("P1 Key" + std::to_string(i));
			mnemonics.push_back(static_cast<char>('A' + (i % 26)));
		}
		EntryLayout l = layoutOf(keys, {});
		assert(l.buttonCount() == 101);
		assert(l.groups() == 2);

		// press the first, one in the middle of the upper half, and the last
		std::vector<uint8_t> in(101, 0);
		in[0] = in[64] = in[77] = in[100] = 1;
		std::string entry = l.generate(in.data(), nullptr, mnemonics);
		assert(entry.size() == 2 + 101 + 1); // "||" + columns + "|"
		assert(entry[2 + 0] == 'A');
		assert(entry[2 + 64] == mnemonics[64]);
		assert(entry[2 + 77] == mnemonics[77]);
		assert(entry[2 + 100] == mnemonics[100]);
		assert(entry[2 + 1] == '.' && entry[2 + 63] == '.' && entry[2 + 99] == '.');

		std::vector<uint8_t> back;
		std::vector<int32_t> axes;
		assert(l.parse(entry.c_str(), back, axes));
		assert(back == in); // byte-exact round trip across the 64 boundary
	}

	{ // A MACHINE HAS ONLY SOME OF ITS DECLARED CONTROLS.
		//
		// A package declares the union of every peripheral its ports can hold,
		// because the declaration is static; the running core says which of
		// them exist. An entry carries those and no others - but every INDEX
		// stays the declaration's, so what a core reads off its own wire is
		// untouched by any of it.
		const std::vector<std::string> declared = {
			"P1 A", "P1 B", "P2 A", "P2 B", "P3 A", "P3 B",
		};
		const std::vector<EntryAxis> declaredAxes = {
			axis("P1 Paddle", 0, 160, 80), axis("P3 Paddle", 0, 160, 80),
		};
		const std::string mnemonics = "ABABAB";

		{ // nothing said: every declared control, which is what a core that
		  // does not answer the question gets
			EntryLayout l;
			l.build(declared, declaredAxes);
			assert(l.groups() == 4);
			assert(l.size() == 8);
		}

		// port two empty and no paddle anywhere: P2 vanishes, and so does the
		// player-three GROUP once its axis goes with it
		const std::vector<uint8_t> activeButtons = { 1, 1, 0, 0, 1, 1 };
		const std::vector<uint8_t> activeAxes = { 0, 0 };
		EntryLayout l;
		l.build(declared, declaredAxes, &activeButtons, &activeAxes);
		assert(l.groups() == 4);   // console, P1, P2 (empty), P3
		assert(l.size() == 4);
		assert(l.buttonCount() == declared.size()); // the WIRE is unchanged

		// P1 A and P3 B, which are declared indices 0 and 5
		std::vector<uint8_t> in(declared.size(), 0);
		in[0] = in[5] = 1;
		const std::string entry = l.generate(in.data(), nullptr, mnemonics);
		// the console group and player two's are both EMPTY and both still
		// written: a group is a place in the entry, and one that vanished
		// would shift every column after it
		assert(entry == "||A.||.B|");

		std::vector<uint8_t> back;
		std::vector<int32_t> axes;
		assert(l.parse(entry.c_str(), back, axes));
		assert(back.size() == declared.size());
		assert(back == in);                       // at the DECLARED positions
		assert(axes.size() == declaredAxes.size());
		assert(axes[0] == 80 && axes[1] == 80);   // an absent axis reads neutral

		// and an entry written for the full controller is no longer this
		// machine's: it has columns this machine has no controls for
		assert(!l.parse("||AB|AB|AB|", back, axes) || back != in);
	}

	std::printf("test_movie_entry: ok\n");
	return 0;
}
