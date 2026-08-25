/* test_bundle.cpp - pins the .gameBundle format, identity, and naming rules,
 * plus the sha1 helper and the firmware verdict/record line.
 * The frontend's GameBundleTests pin the same behaviour through the ABI.
 */

#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

static ce_bundle *parsed(const char *json, const char *label = "game.gameBundle")
{
	const char *error = nullptr;
	ce_bundle *b = ce_bundle_parse(json, std::strlen(json), label, &error);
	if (b == nullptr) std::printf("unexpected refusal: %s\n", error);
	assert(b != nullptr);
	return b;
}

static const char *refused(const char *json, const char *label = "game.gameBundle")
{
	const char *error = nullptr;
	ce_bundle *b = ce_bundle_parse(json, std::strlen(json), label, &error);
	assert(b == nullptr && error != nullptr);
	return error;
}

int main(void)
{
	{ // sha1: the standard vectors, uppercase hex
		char hex[41];
		ce_sha1_hex(reinterpret_cast<const uint8_t *>(""), 0, hex);
		assert(std::strcmp(hex, "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709") == 0);
		ce_sha1_hex(reinterpret_cast<const uint8_t *>("abc"), 3, hex);
		assert(std::strcmp(hex, "A9993E364706816ABA3E25717850C26C9CD0D89D") == 0);
		// > one block
		std::string big(1000, 'a');
		ce_sha1_hex(reinterpret_cast<const uint8_t *>(big.data()), big.size(), hex);
		assert(std::strcmp(hex, "291E9A6C66994949B57BA5E650361E98FC36B1BA") == 0);
	}

	{ // a full bundle round trips
		ce_bundle *b = parsed(R"({ "bundle": 1, "name": "after world 1",
			"rom": { "file": "smb3.nes", "sha1": "aa" },
			"attach": [ { "core": "QuickerNesHawk", "id": "sram", "file": "smb3.sram", "sha1": "bb" } ] })");
		assert(std::strcmp(ce_bundle_name(b), "after world 1") == 0);
		assert(std::strcmp(ce_bundle_rom_file(b), "smb3.nes") == 0);
		assert(ce_bundle_attach_count(b) == 1);
		assert(std::strcmp(ce_bundle_attach_id(b, 0), "sram") == 0);

		uint64_t len = 0;
		const char *json = ce_bundle_serialize(b, &len);
		ce_bundle *again = parsed(json);
		assert(std::strcmp(ce_bundle_content_id(again), ce_bundle_content_id(b)) == 0);
		ce_bundle_free(again);
		ce_bundle_free(b);
	}

	{ // identity: over the parts, order-independent, formatting-independent
		ce_bundle *a = ce_bundle_new();
		ce_bundle_set_rom(a, "r.nes", "AA");
		ce_bundle_add_attach(a, "CoreB", "x", "f1", "11");
		ce_bundle_add_attach(a, "CoreA", "y", "f2", "22");
		ce_bundle *b = ce_bundle_new();
		ce_bundle_set_rom(b, "renamed.nes", "aa"); // file names and hash case do not matter
		ce_bundle_add_attach(b, "CoreA", "y", "other", "22");
		ce_bundle_add_attach(b, "CoreB", "x", "names", "11");
		ce_bundle_set_name(b, "different name");
		assert(std::strcmp(ce_bundle_content_id(a), ce_bundle_content_id(b)) == 0);

		ce_bundle_set_attach_sha1(b, 0, "33"); // a changed part IS a different bundle
		assert(std::strcmp(ce_bundle_content_id(a), ce_bundle_content_id(b)) != 0);
		ce_bundle_set_attach_sha1(b, 0, nullptr); // an unpinned part means no identity
		assert(ce_bundle_content_id(b) == nullptr);
		ce_bundle_free(b);
		ce_bundle_free(a);
	}

	{ // the naming rules, as pure string logic
		assert(ce_bundle_check_path("smb3.nes") == 0);
		assert(ce_bundle_check_path("sub/dir/file.bin") == 0);
		assert(ce_bundle_check_path("sub/../file.bin") == 0); // stays inside
		assert(ce_bundle_check_path("") == 1);
		assert(ce_bundle_check_path("  ") == 1);
		assert(ce_bundle_check_path("/etc/passwd") == 2);
		assert(ce_bundle_check_path("C:\\roms\\x.nes") == 2);
		assert(ce_bundle_check_path("..\\outside.nes") == 3); // uniform: backslash separates everywhere
		assert(ce_bundle_check_path("../outside.nes") == 3);
		assert(ce_bundle_check_path("sub/../../outside.nes") == 3);
	}

	{ // refusals carry the file's name and the frontend's exact phrasing
		const char *e = refused("this is not json at all");
		assert(std::strstr(e, "game.gameBundle") != nullptr);
		e = refused(R"({ "bundle": 2, "rom": { "file": "x.nes" } })");
		assert(std::strstr(e, "version 2") != nullptr);
		e = refused(R"({ "bundle": 1, "attach": [] })");
		assert(std::strstr(e, "names no rom") != nullptr);
		e = refused(R"({ "bundle": 1, "rom": { "file": "/etc/passwd" } })");
		assert(std::strstr(e, "not absolute paths") != nullptr);
	}

	{ // an absent version field means version 1, and an unpinned rom is fine
		ce_bundle *b = parsed(R"({ "rom": { "file": "x.nes" } })");
		assert(ce_bundle_rom_sha1(b) == nullptr);
		assert(ce_bundle_content_id(b) == nullptr);
		ce_bundle_free(b);
	}

	{ // firmware verdicts
		assert(ce_firmware_state(512, "AA\nBB", 100, "AA") == 0); // size decides: a matching hash at the wrong size is still a substituted file
		assert(ce_firmware_state(0, "AA\nBB", 100, "CC") == 1);   // unrecognised
		assert(ce_firmware_state(0, "AA\nBB", 100, "bb") == 2);   // case-insensitive match
		assert(ce_firmware_state(512, "", 512, "CC") == 2);       // nothing pinned: any dump is good
		assert(ce_firmware_state(0, "", 5, "CC") == 2);
	}

	{ // the movie's firmware line: sorted by id, space-joined
		uint64_t len = 0;
		const char *line = ce_firmware_record_line("main=BB\nbios=AA", &len);
		assert(std::string(line, len) == "bios=AA main=BB");
		line = ce_firmware_record_line("", &len);
		assert(len == 0 && line[0] == '\0');
	}

	std::puts("test_bundle: all ok");
	return 0;
}
