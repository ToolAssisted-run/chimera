/* test_firmware.cpp - the firmware decision tree, pinned.
 *
 * Everything can affect whether firmware is needed and which file satisfies
 * it: the core itself, the game files chosen, the sync settings. The core
 * declares that logic as conditions; this pins the evaluation - slot
 * presence, slot extension, setting values, the combinators, the legacy
 * required flag, and the malformed-condition rule (asks for nothing).
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static std::string eval(const char *decl, const char *slots, const char *settings)
{
	uint64_t len = 0;
	const char *out = ce_firmware_evaluate(
		decl, std::strlen(decl),
		slots, std::strlen(slots),
		settings, std::strlen(settings),
		&len);
	return std::string(out, len);
}

int main(void)
{
	// the motivating cases, straight from the design discussion:

	// a Famicom Disk System game needs the bios; a plain cartridge does not
	const char *fds =
		"[{\"id\":\"disksys.rom\",\"requiredWhen\":{\"slot\":\"fds\"}}]";
	assert(eval(fds, "{\"fds\":[\"game.fds\"]}", "{}")
		== "[{\"id\":\"disksys.rom\",\"state\":\"required\"}]");
	assert(eval(fds, "{\"rom\":[\"game.nes\"]}", "{}") == "[]");

	// a CD game needs the CD bios; a cartridge in the same core does not
	const char *segacd =
		"[{\"id\":\"bios_cd\",\"requiredWhen\":{\"any\":[{\"slot\":\"cd\",\"extension\":\"cue\"},{\"slot\":\"cd\",\"extension\":\"iso\"}]}}]";
	assert(eval(segacd, "{\"cd\":[\"sonic cd.cue\"]}", "{}")
		== "[{\"id\":\"bios_cd\",\"state\":\"required\"}]");
	assert(eval(segacd, "{\"cart\":[\"sonic.md\"]}", "{}") == "[]");
	assert(eval(segacd, "{\"cd\":[\"notes.txt\"]}", "{}") == "[]");

	// a region setting selects between region bioses
	const char *region =
		"[{\"id\":\"bios_jp\",\"requiredWhen\":{\"setting\":\"region\",\"is\":\"jp\"}},"
		"{\"id\":\"bios_us\",\"requiredWhen\":{\"setting\":\"region\",\"in\":[\"us\",\"eu\"]}}]";
	assert(eval(region, "{}", "{\"region\":\"jp\"}")
		== "[{\"id\":\"bios_jp\",\"state\":\"required\"}]");
	assert(eval(region, "{}", "{\"region\":\"eu\"}")
		== "[{\"id\":\"bios_us\",\"state\":\"required\"}]");
	assert(eval(region, "{}", "{}") == "[]");

	// a setting flips a bundled freebie into a real-bios requirement
	const char *fonts =
		"[{\"id\":\"ltn0.pgf\",\"requiredWhen\":{\"all\":[{\"setting\":\"fontSource\",\"is\":\"sony\"},{\"not\":{\"setting\":\"language\",\"is\":0}}]}}]";
	assert(eval(fonts, "{}", "{\"fontSource\":\"sony\",\"language\":1}")
		== "[{\"id\":\"ltn0.pgf\",\"state\":\"required\"}]");
	assert(eval(fonts, "{}", "{\"fontSource\":\"free\",\"language\":1}") == "[]");
	assert(eval(fonts, "{}", "{\"fontSource\":\"sony\",\"language\":0}") == "[]");

	// number and bool settings compare by their own type
	const char *typed =
		"[{\"id\":\"a\",\"requiredWhen\":{\"setting\":\"n\",\"is\":3}},"
		"{\"id\":\"b\",\"requiredWhen\":{\"setting\":\"f\",\"is\":true}}]";
	assert(eval(typed, "{}", "{\"n\":3,\"f\":true}")
		== "[{\"id\":\"a\",\"state\":\"required\"},{\"id\":\"b\",\"state\":\"required\"}]");
	assert(eval(typed, "{}", "{\"n\":\"3\",\"f\":1}") == "[]");

	// no condition: the legacy flag decides between required and optional
	const char *legacy =
		"[{\"id\":\"always\",\"required\":true},{\"id\":\"maybe\",\"required\":false},{\"id\":\"also-maybe\"}]";
	assert(eval(legacy, "{}", "{}")
		== "[{\"id\":\"always\",\"state\":\"required\"},{\"id\":\"maybe\",\"state\":\"optional\"},{\"id\":\"also-maybe\",\"state\":\"optional\"}]");

	// a malformed condition asks for nothing rather than for everything
	const char *broken =
		"[{\"id\":\"x\",\"requiredWhen\":{\"nonsense\":1}},{\"id\":\"y\",\"requiredWhen\":\"what\"}]";
	assert(eval(broken, "{\"any\":[\"thing.bin\"]}", "{\"any\":1}") == "[]");
	assert(eval("not json at all", "{}", "{}") == "[]");

	std::printf("firmware: all assertions passed\n");
	return 0;
}
