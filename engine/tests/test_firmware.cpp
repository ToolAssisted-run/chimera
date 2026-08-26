/* test_firmware.cpp - the firmware decision tree, pinned.
 *
 * Everything can affect whether firmware is needed: the core itself, the
 * game files chosen, the sync settings. The decisions nail each requirement
 * to ONE exact file or to nothing - variants of one id are separate entries
 * with disjoint conditions (a sync setting picks between them), and
 * optional firmware does not exist. This pins the evaluation: slot
 * presence, slot extension, setting values, the combinators, entry indices
 * for same-id variants, and the malformed-condition rule (asks for
 * nothing).
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
	// a Famicom Disk System game needs the bios; a plain cartridge needs
	// NOTHING - not even a mention
	const char *fds =
		"[{\"id\":\"bios\",\"requiredWhen\":{\"slot\":\"rom\",\"extension\":\"fds\"}}]";
	assert(eval(fds, "{\"rom\":[\"game.fds\"]}", "{}")
		== "[{\"id\":\"bios\",\"index\":0}]");
	assert(eval(fds, "{\"rom\":[\"game.nes\"]}", "{}") == "[]");

	// the Sega CD bios, nailed to ONE file by a sync setting: same id, one
	// entry per dump, the setting picks - the index says which entry applies
	const char *segacd =
		"[{\"id\":\"bios_cd\",\"requiredWhen\":{\"all\":[{\"slot\":\"cd\"},{\"setting\":\"cdBios\",\"is\":\"usa_1.10\"}]}},"
		"{\"id\":\"bios_cd\",\"requiredWhen\":{\"all\":[{\"slot\":\"cd\"},{\"setting\":\"cdBios\",\"is\":\"jpn_1.00\"}]}}]";
	assert(eval(segacd, "{\"cd\":[\"sonic cd.cue\"]}", "{\"cdBios\":\"usa_1.10\"}")
		== "[{\"id\":\"bios_cd\",\"index\":0}]");
	assert(eval(segacd, "{\"cd\":[\"sonic cd.cue\"]}", "{\"cdBios\":\"jpn_1.00\"}")
		== "[{\"id\":\"bios_cd\",\"index\":1}]");
	assert(eval(segacd, "{\"cart\":[\"sonic.md\"]}", "{\"cdBios\":\"usa_1.10\"}") == "[]");

	// the no-optional rule in person: a font-source setting either needs no
	// firmware at all (the free font) or exactly one file (the real one)
	const char *fonts =
		"[{\"id\":\"ltn0.pgf\",\"requiredWhen\":{\"setting\":\"fontSource\",\"is\":\"sony\"}}]";
	assert(eval(fonts, "{}", "{\"fontSource\":\"sony\"}")
		== "[{\"id\":\"ltn0.pgf\",\"index\":0}]");
	assert(eval(fonts, "{}", "{\"fontSource\":\"bundled\"}") == "[]");

	// an entry without a condition always applies (a core that cannot start
	// without its bios, regardless of anything else)
	const char *always = "[{\"id\":\"boot\"}]";
	assert(eval(always, "{}", "{}") == "[{\"id\":\"boot\",\"index\":0}]");

	// extension and membership conditions
	const char *byExt =
		"[{\"id\":\"cdrom_fw\",\"requiredWhen\":{\"any\":[{\"slot\":\"cd\",\"extension\":\"cue\"},{\"slot\":\"cd\",\"extension\":\"iso\"}]}},"
		"{\"id\":\"region_fw\",\"requiredWhen\":{\"setting\":\"region\",\"in\":[\"us\",\"eu\"]}}]";
	assert(eval(byExt, "{\"cd\":[\"disc.iso\"]}", "{\"region\":\"jp\"}")
		== "[{\"id\":\"cdrom_fw\",\"index\":0}]");
	assert(eval(byExt, "{\"cd\":[\"notes.txt\"]}", "{\"region\":\"eu\"}")
		== "[{\"id\":\"region_fw\",\"index\":1}]");

	// number and bool settings compare by their own type; not-combinator
	const char *typed =
		"[{\"id\":\"a\",\"requiredWhen\":{\"all\":[{\"setting\":\"n\",\"is\":3},{\"not\":{\"setting\":\"f\",\"is\":false}}]}}]";
	assert(eval(typed, "{}", "{\"n\":3,\"f\":true}") == "[{\"id\":\"a\",\"index\":0}]");
	assert(eval(typed, "{}", "{\"n\":\"3\",\"f\":true}") == "[]");

	// a malformed condition asks for nothing rather than for everything
	const char *broken =
		"[{\"id\":\"x\",\"requiredWhen\":{\"nonsense\":1}},{\"id\":\"y\",\"requiredWhen\":\"what\"}]";
	assert(eval(broken, "{\"any\":[\"thing.bin\"]}", "{\"any\":1}") == "[]");
	assert(eval("not json at all", "{}", "{}") == "[]");

	std::printf("firmware: all assertions passed\n");
	return 0;
}
