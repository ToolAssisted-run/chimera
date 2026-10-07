/* test_game_properties.cpp - a game core's property table (docs/game-cores.md):
 * what is taken from it and what is left out and said, and that every type
 * reads, writes, shows and parses the bytes it says it does - which is all a
 * watch, a poke, a freeze or a script of one ever does.
 *
 * Two byte arrays stand in for a core's domains. Plain asserts, run by
 * `meson test -C build/meson-linux`.
 */

#include "../source/game_properties.hpp"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using GP = CeGameProperties;

namespace
{
uint8_t g_state[128];
uint8_t g_level[32];
uint8_t g_rom[16];

std::vector<GP::Domain> domains()
{
	return {
		{ "Game State", g_state, sizeof g_state, true },
		{ "Level", g_level, sizeof g_level, true },
		{ "ROM", g_rom, sizeof g_rom, false },
		{ "Bus", nullptr, 65536, true },
	};
}

const char *const kTable = R"({ "properties": [
	{ "name": "Kid.X", "domain": "Game State", "offset": 0, "type": "u8", "group": "Kid", "description": "Across the room" },
	{ "name": "Kid.Direction", "domain": "Game State", "offset": 1, "type": "s8", "values": { "-1": "Left", "0": "Right" } },
	{ "name": "Kid.Frame", "domain": "Game State", "offset": 2, "type": "u16", "writable": false },
	{ "name": "Guard.HP", "domain": "Game State", "offset": 4, "type": "s16" },
	{ "name": "Seed", "domain": "Game State", "offset": 8, "type": "u32" },
	{ "name": "Delta", "domain": "Game State", "offset": 12, "type": "s32" },
	{ "name": "Frames", "domain": "Game State", "offset": 16, "type": "u64" },
	{ "name": "Balance", "domain": "Game State", "offset": 24, "type": "s64" },
	{ "name": "Speed", "domain": "Game State", "offset": 32, "type": "f32" },
	{ "name": "Gravity", "domain": "Game State", "offset": 36, "type": "f64" },
	{ "name": "Alive", "domain": "Game State", "offset": 44, "type": "bool" },
	{ "name": "Level Name", "domain": "Game State", "offset": 48, "type": "string", "length": 8 },
	{ "name": "Hero", "domain": "Game State", "offset": 56, "type": "string", "length": 6, "encoding": "utf8" },
	{ "name": "Wide", "domain": "Game State", "offset": 64, "type": "string", "length": 8, "encoding": "utf16le" },
	{ "name": "Key", "domain": "Game State", "offset": 72, "type": "bytes", "length": 4 },
	{ "name": "Score", "domain": "Game State", "offset": 76, "type": "u32", "endian": "big" },
	{ "name": "Guards.X", "domain": "Game State", "offset": 80, "type": "s16", "count": 3, "stride": 6, "group": "Guards" },
	{ "name": "Guards.Y", "domain": "Game State", "offset": 82, "type": "u16", "count": 3, "stride": 6, "group": "Guards" },
	{ "name": "Door Open", "domain": "Level", "offset": 5, "type": "u8", "bit": 3, "bits": 1 },
	{ "name": "Tile Kind", "domain": "Level", "offset": 5, "type": "u8", "bit": 4, "bits": 4 },
	{ "name": "Tilt", "domain": "Level", "offset": 6, "type": "s8", "bit": 0, "bits": 3 },
	{ "name": "Tiles", "domain": "Level", "offset": 8, "type": "u8", "count": 16 },
	{ "name": "Version", "domain": "ROM", "offset": 0, "type": "u8" },
	{ "name": "Rooms.Left", "domain": "Level", "offset": 24, "type": "u8", "count": 4, "first": 1 }
] })";

GP loaded()
{
	std::memset(g_state, 0, sizeof g_state);
	std::memset(g_level, 0, sizeof g_level);
	std::memset(g_rom, 0, sizeof g_rom);
	GP gp;
	gp.load(kTable, domains());
	for (const std::string &p : gp.problems()) std::fprintf(stderr, "unexpected problem: %s\n", p.c_str());
	assert(gp.problems().empty());
	return gp;
}

int32_t idx(const GP &gp, const char *name)
{
	uint32_t element = 0;
	const int32_t i = gp.find(name, &element);
	assert(i >= 0);
	return i;
}

void setText(const GP &gp, const char *name, const std::string &text)
{
	uint32_t element = 0;
	const int32_t i = gp.find(name, &element);
	std::string error;
	const bool ok = gp.writeText(i, element, text, error);
	if (!ok) std::fprintf(stderr, "%s <- %s: %s\n", name, text.c_str(), error.c_str());
	assert(ok);
}

std::string textOf(const GP &gp, const char *name, bool named = true)
{
	uint32_t element = 0;
	const int32_t i = gp.find(name, &element);
	return gp.text(i, element, named);
}

bool refused(const GP &gp, const char *name, const std::string &text, const char *because)
{
	uint32_t element = 0;
	const int32_t i = gp.find(name, &element);
	std::string error;
	if (gp.writeText(i, element, text, error)) return false;
	if (error.find(because) == std::string::npos)
	{
		std::fprintf(stderr, "%s <- %s refused as \"%s\", not for \"%s\"\n", name, text.c_str(), error.c_str(), because);
		return false;
	}
	return true;
}

void takesTheTableInItsOrder()
{
	GP gp = loaded();
	assert(gp.all().size() == 24);
	assert(gp.all()[0].name == "Kid.X" && gp.all()[22].name == "Version");
	assert(gp.find("kid.direction", nullptr) == 1); // any case
	const auto &guards = gp.all()[size_t(idx(gp, "Guards.X"))];
	assert(guards.count == 3 && guards.stride == 6 && guards.size == 2);
	assert(gp.all()[size_t(idx(gp, "Level Name"))].size == 8);
	assert(!gp.all()[size_t(idx(gp, "Kid.Frame"))].writable);
	assert(!gp.all()[size_t(idx(gp, "Version"))].writable); // its domain is read-only
	const std::string d = gp.describe();
	assert(d.find("\"name\":\"Guards.X\"") != std::string::npos);
	assert(d.find("\"count\":3") != std::string::npos);
	assert(d.find("\"encoding\":\"utf16le\"") != std::string::npos);
	assert(d.find("\"-1\":\"Left\"") != std::string::npos);
	assert(d.find("\"problems\":[]") != std::string::npos);
}

void leavesOutWhatItCannotUseAndSaysWhy()
{
	GP gp;
	gp.load(R"({ "properties": [
		{ "name": "Fine", "domain": "Game State", "offset": 0, "type": "u8" },
		{ "name": "Nowhere", "domain": "Missing", "offset": 0, "type": "u8" },
		{ "name": "On the bus", "domain": "Bus", "offset": 0, "type": "u8" },
		{ "name": "Past the end", "domain": "Game State", "offset": 126, "type": "u32" },
		{ "name": "Array past the end", "domain": "Level", "offset": 0, "type": "u8", "count": 33 },
		{ "name": "Odd", "domain": "Game State", "offset": 0, "type": "u128" },
		{ "name": "fine", "domain": "Game State", "offset": 1, "type": "u8" },
		{ "name": "No offset", "domain": "Game State", "type": "u8" },
		{ "name": "Endless", "domain": "Game State", "offset": 0, "type": "string" },
		{ "name": "Squeezed", "domain": "Game State", "offset": 0, "type": "u16", "count": 2, "stride": 1 },
		{ "name": "Too many bits", "domain": "Game State", "offset": 0, "type": "u8", "bit": 5, "bits": 4 },
		{ "name": "Float bits", "domain": "Game State", "offset": 0, "type": "f32", "bits": 3 },
		{ "name": "Klingon", "domain": "Game State", "offset": 0, "type": "string", "length": 4, "encoding": "klingon" },
		{ "name": "Sideways", "domain": "Game State", "offset": 0, "type": "u16", "endian": "middle" },
		{ "name": "Backwards", "domain": "Game State", "offset": 0, "type": "u8", "count": 2, "first": -1 },
		{ "domain": "Game State", "offset": 0, "type": "u8" },
		7
	] })", domains());
	assert(gp.all().size() == 1 && gp.all()[0].name == "Fine");
	assert(gp.problems().size() == 16);
	auto said = [&](size_t k, const char *what) {
		if (gp.problems()[k].find(what) == std::string::npos)
		{
			std::fprintf(stderr, "problem %zu is \"%s\", expected \"%s\" in it\n", k, gp.problems()[k].c_str(), what);
			assert(false);
		}
	};
	said(0, "no domain called");
	said(1, "no memory of its own");
	said(2, "past the end");
	said(3, "past the end");
	said(4, "u128");
	said(5, "named twice");
	said(6, "offset");
	said(7, "no length");
	said(8, "stride");
	said(9, "does not fit");
	said(10, "not an integer");
	said(11, "klingon");
	said(12, "neither little nor big");
	said(13, "first index");
	said(14, "no name");
	said(15, "not an object");

	GP none;
	none.load(nullptr, domains());
	assert(none.all().empty() && none.problems().empty()); // no table is every emulator
	GP broken;
	broken.load("{ not json", domains());
	assert(broken.all().empty() && broken.problems().size() == 1);
}

void everyIntegerHasItsWidthSignAndOrder()
{
	GP gp = loaded();
	setText(gp, "Kid.Direction", "-1");
	assert(g_state[1] == 0xFF);
	assert(textOf(gp, "Kid.Direction") == "Left");
	assert(textOf(gp, "Kid.Direction", false) == "-1");
	setText(gp, "Kid.Direction", "right"); // a value's name, any case
	assert(g_state[1] == 0);

	setText(gp, "Guard.HP", "-2");
	assert(g_state[4] == 0xFE && g_state[5] == 0xFF); // little-endian
	setText(gp, "Seed", "0xDEADBEEF");
	assert(g_state[8] == 0xEF && g_state[11] == 0xDE);
	assert(textOf(gp, "Seed") == "3735928559");
	setText(gp, "Delta", "-100000");
	assert(textOf(gp, "Delta") == "-100000");

	setText(gp, "Frames", "18446744073709551615");
	for (int k = 16; k < 24; k++) assert(g_state[k] == 0xFF);
	assert(textOf(gp, "Frames") == "18446744073709551615");
	setText(gp, "Balance", "-9223372036854775808");
	assert(g_state[31] == 0x80 && g_state[24] == 0);
	assert(textOf(gp, "Balance") == "-9223372036854775808");

	setText(gp, "Score", "0x01020304");
	assert(g_state[76] == 1 && g_state[79] == 4); // big-endian
	assert(textOf(gp, "Score") == "16909060");

	assert(refused(gp, "Kid.X", "256", "does not fit"));
	assert(refused(gp, "Kid.X", "-1", "does not fit"));
	assert(refused(gp, "Kid.Direction", "128", "does not fit"));
	assert(refused(gp, "Kid.X", "twelve", "not a whole number"));
	setText(gp, "Kid.Direction", "0x80"); // hex is the raw bits
	assert(textOf(gp, "Kid.Direction") == "-128");

	// a typed number is taken when it fits the width signed or unsigned, and refused
	// otherwise; -1 is how a signed caller sets every bit of a u64
	GP::Value v;
	v.kind = GP::Value::Int;
	v.i = 257;
	std::string error;
	g_state[0] = 9;
	assert(!gp.write(idx(gp, "Kid.X"), 0, v, error) && g_state[0] == 9 && error.find("does not fit") != std::string::npos);
	v.i = -1;
	assert(gp.write(idx(gp, "Kid.X"), 0, v, error) && g_state[0] == 0xFF);
	v.i = -129;
	assert(!gp.write(idx(gp, "Kid.X"), 0, v, error));
	assert(gp.write(idx(gp, "Frames"), 0, v, error) && textOf(gp, "Frames") == "18446744073709551487");
	v.kind = GP::Value::UInt;
	v.u = 256;
	assert(!gp.write(idx(gp, "Kid.X"), 0, v, error));
	v.kind = GP::Value::Float;
	v.f = -3.9;
	assert(gp.write(idx(gp, "Guard.HP"), 0, v, error) && textOf(gp, "Guard.HP") == "-3"); // truncated
	v.f = 1e30;
	assert(!gp.write(idx(gp, "Frames"), 0, v, error));
	GP::Value out;
	assert(gp.read(idx(gp, "Guard.HP"), 0, out) && out.kind == GP::Value::Int && out.i == -3);
	assert(gp.read(idx(gp, "Seed"), 0, out) && out.kind == GP::Value::UInt && out.u == 0xDEADBEEF);
}

void floatsBoolsTextAndBytes()
{
	GP gp = loaded();
	setText(gp, "Speed", "1.5");
	assert(textOf(gp, "Speed") == "1.5");
	setText(gp, "Speed", "0.1");
	assert(textOf(gp, "Speed") == "0.1"); // the fewest digits that read back as the same float
	setText(gp, "Gravity", "0.1");
	assert(textOf(gp, "Gravity") == "0.1");
	GP::Value out;
	assert(gp.read(idx(gp, "Gravity"), 0, out) && out.kind == GP::Value::Float && out.f == 0.1);
	assert(refused(gp, "Speed", "fast", "not a number"));

	setText(gp, "Alive", "true");
	assert(g_state[44] == 1 && textOf(gp, "Alive") == "true");
	g_state[44] = 7;
	assert(textOf(gp, "Alive") == "true"); // any other byte is true, as the game reads it
	setText(gp, "Alive", "0");
	assert(g_state[44] == 0 && textOf(gp, "Alive") == "false");
	assert(refused(gp, "Alive", "maybe", "true or false"));

	setText(gp, "Level Name", "Dungeon");
	assert(std::memcmp(g_state + 48, "Dungeon\0", 8) == 0);
	assert(textOf(gp, "Level Name") == "Dungeon");
	setText(gp, "Level Name", "The Palace Gate"); // cut at the length
	assert(std::memcmp(g_state + 48, "The Pala", 8) == 0 && textOf(gp, "Level Name") == "The Pala");
	setText(gp, "Level Name", "Up");
	assert(std::memcmp(g_state + 48, "Up\0\0\0\0\0\0", 8) == 0); // and NUL-padded
	assert(refused(gp, "Level Name", "caf\xC3\xA9", "cannot hold")); // ascii has no e-acute
	g_state[48] = 0xE9;
	g_state[49] = 0;
	assert(textOf(gp, "Level Name") == "\xC3\xA9"); // a high byte reads as its latin-1 character

	setText(gp, "Hero", "\xC3\xA9t\xC3\xA9\xC3\xA9"); // e-acute, t, two e-acutes: 7 bytes of UTF-8 into 6
	assert(textOf(gp, "Hero") == "\xC3\xA9t\xC3\xA9"); // cut at a whole character, not through the last
	assert(g_state[61] == 0);

	setText(gp, "Wide", "A\xF0\x9F\x98\x80"); // A and an emoji: 1 + 2 UTF-16 units
	assert(g_state[64] == 'A' && g_state[65] == 0 && g_state[66] == 0x3D && g_state[67] == 0xD8);
	assert(textOf(gp, "Wide") == "A\xF0\x9F\x98\x80");

	setText(gp, "Key", "de ad be ef");
	assert(g_state[72] == 0xDE && g_state[75] == 0xEF);
	assert(textOf(gp, "Key") == "DE AD BE EF");
	setText(gp, "Key", "0x01020304");
	assert(g_state[72] == 1 && g_state[75] == 4);
	assert(refused(gp, "Key", "01 02", "4 bytes"));
}

void arraysAreAddressedByElement()
{
	GP gp = loaded();
	uint32_t element = 9;
	assert(gp.find("Guards.X[2]", &element) == idx(gp, "Guards.X") && element == 2);
	assert(gp.find("guards.x[3]", &element) == -1); // past the end
	assert(gp.find("Kid.X[0]", &element) == -1);    // not an array
	assert(gp.find("Guards.X", &element) == idx(gp, "Guards.X") && element == 0);

	setText(gp, "Guards.X[1]", "-5");
	setText(gp, "Guards.Y[1]", "300");
	assert(g_state[86] == 0xFB && g_state[87] == 0xFF); // 80 + 6
	assert(g_state[88] == 0x2C && g_state[89] == 0x01); // 82 + 6
	assert(textOf(gp, "Guards.X[1]") == "-5" && textOf(gp, "Guards.X[0]") == "0");
	setText(gp, "Tiles[15]", "9");
	assert(g_level[23] == 9);

	// interleaved arrays of a structure: each byte belongs to the right field
	bool starts = false;
	assert(gp.at("Game State", 86, &element, &starts) == idx(gp, "Guards.X") && element == 1 && starts);
	assert(gp.at("Game State", 89, &element, &starts) == idx(gp, "Guards.Y") && element == 1 && !starts);
	assert(gp.at("Game State", 84, &element, &starts) == -1); // the struct's third field, not described
	assert(gp.at("Game State", 50, &element, &starts) == idx(gp, "Level Name") && !starts);
	assert(gp.at("Level", 23, &element, &starts) == idx(gp, "Tiles") && element == 15);
	assert(gp.at("Level", 40, &element, &starts) == -1);

	// an array the game counts from 1 is named as the game counts it
	assert(gp.find("Rooms.Left[0]", &element) == -1);
	assert(gp.find("Rooms.Left[1]", &element) == idx(gp, "Rooms.Left") && element == 0);
	assert(gp.find("Rooms.Left[4]", &element) == idx(gp, "Rooms.Left") && element == 3);
	assert(gp.find("Rooms.Left[5]", &element) == -1);
	setText(gp, "Rooms.Left[4]", "9");
	assert(g_level[27] == 9);
	assert(gp.describe().find("\"first\":1") != std::string::npos);
}

void bitFieldsTouchOnlyTheirBits()
{
	GP gp = loaded();
	g_level[5] = 0x05;
	setText(gp, "Door Open", "1");
	assert(g_level[5] == 0x0D);
	setText(gp, "Tile Kind", "0xA");
	assert(g_level[5] == 0xAD);
	assert(textOf(gp, "Door Open") == "1" && textOf(gp, "Tile Kind") == "10");
	assert(refused(gp, "Door Open", "2", "1 bits"));
	GP::Value two;
	two.kind = GP::Value::Int;
	two.i = 2;
	std::string error;
	assert(!gp.write(idx(gp, "Door Open"), 0, two, error) && error.find("1 bits") != std::string::npos);
	setText(gp, "Door Open", "0");
	assert(g_level[5] == 0xA5);
	g_level[6] = 0xF8;
	setText(gp, "Tilt", "-1"); // three bits, signed
	assert(g_level[6] == 0xFF && textOf(gp, "Tilt") == "-1");
	setText(gp, "Tilt", "3");
	assert(g_level[6] == 0xFB && textOf(gp, "Tilt") == "3");
	assert(refused(gp, "Tilt", "4", "does not fit"));
	uint32_t element = 0;
	bool starts = false;
	assert(gp.at("Level", 5, &element, &starts) == idx(gp, "Door Open")); // the first of the two in its byte
}

void whatTheGameWorksOutCannotBeSet()
{
	GP gp = loaded();
	assert(refused(gp, "Kid.Frame", "3", "worked out by the game"));
	assert(refused(gp, "Version", "3", "cannot be written"));
	GP::Value v;
	v.kind = GP::Value::Text;
	v.data = "x";
	std::string error;
	assert(!gp.write(idx(gp, "Kid.X"), 0, v, error) && error.find("takes a number") != std::string::npos);
	v.kind = GP::Value::Int;
	assert(!gp.write(idx(gp, "Level Name"), 0, v, error) && error.find("takes text") != std::string::npos);
}
void theGamesOwnTimer()
{
	// the table names a whole-number property as the game's timer, in ms
	GP gp;
	gp.load(R"({ "gameTimer": "IGT Ms", "properties": [
		{ "name": "IGT Ms", "domain": "Game State", "offset": 64, "type": "u32" },
		{ "name": "Speed", "domain": "Game State", "offset": 68, "type": "f32" },
		{ "name": "Laps", "domain": "Game State", "offset": 72, "type": "u16", "count": 3 } ] })", domains());
	assert(gp.problems().empty());
	assert(gp.timer() == idx(gp, "IGT Ms"));
	assert(gp.describe().find("\"gameTimer\":\"IGT Ms\"") != std::string::npos);
	const uint32_t ms = 754083;
	std::memcpy(g_state + 64, &ms, 4);
	GP::Value v;
	assert(gp.read(gp.timer(), 0, v) && v.kind == GP::Value::UInt && v.u == 754083);

	// anything else it could name is not a timer, and is said
	for (const char *named : { "\"Speed\"", "\"Laps\"", "\"Nothing\"", "3" })
	{
		GP bad;
		bad.load((std::string(R"({ "gameTimer": )") + named + R"(, "properties": [
			{ "name": "Speed", "domain": "Game State", "offset": 68, "type": "f32" },
			{ "name": "Laps", "domain": "Game State", "offset": 72, "type": "u16", "count": 3 } ] })").c_str(), domains());
		assert(bad.timer() == -1);
		assert(bad.problems().size() == 1 && bad.problems()[0].find("gameTimer") != std::string::npos);
	}
	GP none;
	none.load(kTable, domains());
	assert(none.timer() == -1 && none.describe().find("gameTimer") == std::string::npos);

	// as a timer shows it: mm:ss.mmm
	assert(GP::timeText(0) == "00:00.000");
	assert(GP::timeText(83) == "00:00.083");
	assert(GP::timeText(754083) == "12:34.083");
	assert(GP::timeText(3599999) == "59:59.999");
	assert(GP::timeText(6000000) == "100:00.000");
	assert(GP::timeText(-1500) == "-00:01.500");
}

// A domain with no pointer - a bus - holds properties through the two
// functions it is given: every read and every write goes through them, a write
// of part of an element (a bit field) reads the element first, and without
// the functions the domain still cannot hold one.
void aBusHoldsPropertiesThroughItsFunctions()
{
	static uint8_t heap[64];
	std::memset(heap, 0, sizeof heap);
	int reads = 0, writes = 0;
	GP::Domain bus;
	bus.name = "Heap";
	bus.size = sizeof heap;
	bus.writable = true;
	bus.read = [&](int64_t offset, uint8_t *buf, int64_t len) { reads++; std::memcpy(buf, heap + offset, size_t(len)); };
	bus.write = [&](int64_t offset, const uint8_t *buf, int64_t len) { writes++; std::memcpy(heap + offset, buf, size_t(len)); };
	GP gp;
	gp.load(R"({ "properties": [
		{ "name": "v", "domain": "Heap", "offset": 8, "type": "f64" },
		{ "name": "flag", "domain": "Heap", "offset": 16, "type": "bool" },
		{ "name": "bit", "domain": "Heap", "offset": 17, "type": "u8", "bit": 2, "bits": 1 },
		{ "name": "name", "domain": "Heap", "offset": 24, "type": "string", "length": 4, "encoding": "latin1" },
		{ "name": "past", "domain": "Heap", "offset": 60, "type": "f64" },
		{ "name": "nowhere", "domain": "Bus", "offset": 0, "type": "u8" } ] })", { bus, { "Bus", nullptr, 65536, true } });
	assert(gp.all().size() == 4);
	assert(gp.problems().size() == 2); // one runs past the end, one is in a domain with neither a pointer nor functions

	const double d = 123456789.0;
	std::memcpy(heap + 8, &d, 8);
	GP::Value v;
	assert(gp.read(gp.find("v", nullptr), 0, v) && v.kind == GP::Value::Float && v.f == 123456789.0);
	assert(gp.text(gp.find("v", nullptr), 0, true) == "123456789");
	assert(reads == 2 && writes == 0);

	std::string error;
	assert(gp.writeText(gp.find("v", nullptr), 0, "2.5", error));
	double back = 0;
	std::memcpy(&back, heap + 8, 8);
	assert(back == 2.5 && writes == 1);

	heap[17] = 0xF0;
	assert(gp.writeText(gp.find("bit", nullptr), 0, "1", error));
	assert(heap[17] == 0xF4); // the bits around it kept: the element was read first
	assert(gp.writeText(gp.find("flag", nullptr), 0, "true", error) && heap[16] == 1);
	std::memcpy(heap + 24, "Kid\0", 4);
	assert(gp.text(gp.find("name", nullptr), 0, true) == "Kid");
}

// A table that says it is dynamic names places that move. Listed again, a
// property keeps its index and takes its new place; a new one goes on the end;
// one the listing lost stays, not there. Looked for by name, one is put back,
// moved or marked gone without the others being touched.
void aDynamicTableMovesAndKeepsItsIndices()
{
	std::memset(g_state, 0, sizeof g_state);
	const double a = 1.5, a2 = 7.25;
	std::memcpy(g_state + 0, &a, 8);
	std::memcpy(g_state + 16, &a2, 8);
	g_state[8] = 42;
	g_state[40] = 99;

	GP fixed;
	fixed.load(kTable, domains());
	assert(!fixed.dynamic());

	GP gp;
	gp.load(R"({ "dynamic": true, "properties": [
		{ "name": "_root.a", "domain": "Game State", "offset": 0, "type": "f64" },
		{ "name": "_root.b", "domain": "Game State", "offset": 8, "type": "u8" } ] })", domains());
	assert(gp.dynamic() && gp.all().size() == 2);
	assert(gp.describe().find("\"dynamic\":true") != std::string::npos);
	const int32_t ia = gp.find("_root.a", nullptr), ib = gp.find("_root.b", nullptr);
	assert(ia == 0 && ib == 1);
	assert(gp.text(ia, 0, true) == "1.5" && gp.text(ib, 0, true) == "42");

	const uint64_t g0 = gp.generation();
	const int32_t listed = gp.relist(R"({ "dynamic": true, "properties": [
		{ "name": "_root.c", "domain": "Game State", "offset": 24, "type": "u8" },
		{ "name": "_root.a", "domain": "Game State", "offset": 16, "type": "f64" } ] })");
	assert(listed == 2 && gp.all().size() == 3 && gp.generation() > g0);
	assert(gp.find("_root.a", nullptr) == ia && gp.find("_root.b", nullptr) == ib && gp.find("_root.c", nullptr) == 2);
	assert(gp.text(ia, 0, true) == "7.25"); // the same index, the new place
	assert(gp.all()[size_t(ia)].listed && gp.all()[size_t(ia)].present);
	assert(!gp.all()[size_t(ib)].listed && !gp.all()[size_t(ib)].present);
	GP::Value v;
	assert(!gp.read(ib, 0, v) && gp.text(ib, 0, true).empty());
	std::string error;
	assert(!gp.writeText(ib, 0, "1", error) && error.find("not there now") != std::string::npos);
	assert(gp.describe().find("\"listed\":false") != std::string::npos);

	// by name: back at another place, the same index
	const uint64_t g1 = gp.generation();
	assert(gp.place("_root.b", R"({ "name": "_root.b", "domain": "Game State", "offset": 40, "type": "u8" })") == ib);
	assert(gp.generation() > g1 && gp.text(ib, 0, true) == "99");
	// found where it already was: nothing changes, and the generation says so
	const uint64_t g2 = gp.generation();
	assert(gp.place("_root.b", R"({ "name": "_root.b", "domain": "Game State", "offset": 40, "type": "u8" })") == ib);
	assert(gp.generation() == g2);
	// gone: the index stays, the property is not there
	assert(gp.place("_root.a", "") == ia && !gp.all()[size_t(ia)].present && !gp.read(ia, 0, v));
	// an entry for another name is no answer
	assert(gp.place("_root.c", R"({ "name": "_root.z", "domain": "Game State", "offset": 0, "type": "u8" })") == 2);
	assert(!gp.all()[2].present);
	// a name it never had: nothing when the core has none, added when it has
	assert(gp.place("_root.nope", "") == -1 && gp.place("_root.nope", nullptr) == -1);
	assert(gp.place("_root.d", R"({ "name": "_root.d", "domain": "Game State", "offset": 8, "type": "u8" })") == 3);
	assert(gp.text(3, 0, true) == "42");
	// an entry the engine cannot use is no answer either
	assert(gp.place("_root.e", R"({ "name": "_root.e", "domain": "Nowhere", "offset": 0, "type": "u8" })") == -1);
}
} // namespace

int main()
{
	takesTheTableInItsOrder();
	leavesOutWhatItCannotUseAndSaysWhy();
	everyIntegerHasItsWidthSignAndOrder();
	floatsBoolsTextAndBytes();
	arraysAreAddressedByElement();
	bitFieldsTouchOnlyTheirBits();
	whatTheGameWorksOutCannotBeSet();
	theGamesOwnTimer();
	aBusHoldsPropertiesThroughItsFunctions();
	aDynamicTableMovesAndKeepsItsIndices();
	std::printf("test_game_properties: ok\n");
	return 0;
}
