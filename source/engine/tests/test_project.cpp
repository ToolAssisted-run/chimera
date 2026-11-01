/* test_project.cpp - the .chimeraProject rules, pinned.
 *
 * Round-trips a full project (identity, core pin, settings, firmware, log,
 * markers, branches, files), then pins the file semantics: born-resolved
 * creation with cue auto-add, per-session resolution (by folder and by an
 * arbitrarily named path), mismatch statuses, save recording ACTUAL hashes,
 * cue closure at save, validation against a core's slot declaration, and
 * the structural rejections.
 */

#include "chimera/engine.h"

#include <cassert>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <cstring>
#include <string>

#ifndef _WIN32
#include <sys/resource.h>
#endif

static std::string g_dir;

static void writeFile(const std::string &name, const std::string &content)
{
	FILE *f = std::fopen((g_dir + "/" + name).c_str(), "wb");
	assert(f != nullptr);
	std::fwrite(content.data(), 1, content.size(), f);
	std::fclose(f);
}

/* A file bigger than a 32-bit length, made sparse so it costs no disk: the
 * point is the SIZE arithmetic, not the bytes. Returns false when the
 * filesystem will not make one, which is not a failure of the project. */
static bool writeSparse(const std::string &name, uint64_t size)
{
	FILE *f = std::fopen((g_dir + "/" + name).c_str(), "wb");
	if (f == nullptr) return false;
	bool ok = fseeko(f, static_cast<off_t>(size) - 1, SEEK_SET) == 0
		&& std::fputc(0, f) != EOF;
	std::fclose(f);
	return ok;
}

static std::string sha1Of(const std::string &content)
{
	char hex[41];
	ce_sha1_hex(reinterpret_cast<const uint8_t *>(content.data()), content.size(), hex);
	return hex;
}

static const char *SLOTS_DECL =
	"{\"slots\":["
	"{\"id\":\"floppy\",\"title\":\"Floppy disks\",\"min\":0,\"max\":-1,\"formats\":[\"img\",\"ima\"]},"
	"{\"id\":\"cdrom\",\"title\":\"CD-ROMs\",\"min\":0,\"max\":-1,\"formats\":[\"iso\",\"cue\"]},"
	"{\"id\":\"hdd\",\"title\":\"Hard disk\",\"min\":0,\"max\":1,\"formats\":[\"hdd\",\"img\"]}"
	"],\"atLeastOneOf\":[[\"floppy\",\"cdrom\",\"hdd\"]]}";

int main(int argc, char **argv)
{
	/* A directory of its own. These tests scribble files with ordinary
	 * names - save.hdd, disc2.iso - and two of them scribble the SAME
	 * names, so sharing the build directory made them race: one removed
	 * save.hdd to prove a missing file is reported missing while the
	 * other was writing it back. */
	g_dir = argc > 1 ? argv[1] : "work-project";
	std::filesystem::create_directories(g_dir);
	const char *err = nullptr;
	const std::string path = g_dir + "/work.chimeraProject";

	writeFile("disc1.cue", "FILE \"disc one.bin\" BINARY\n  TRACK 01 MODE1/2048\n");
	writeFile("disc one.bin", "first disc bytes");
	writeFile("disc2.iso", "second disc bytes");
	writeFile("save.hdd", "savedata bytes");

	// ---- creation: everything set, files born resolved, cue auto-add ----
	{
		ce_project *p = ce_project_new();
		ce_project_set_title(p, "Alley Cat in 4:03");
		ce_project_set_description(p, "the funny cat game");
		ce_project_set_core(p, "dosbox-x", "abc123+local", sha1Of("a package").c_str());
		ce_project_set_rerecords(p, 421);
		assert(ce_project_set_settings_text(p, "{\"cpuCycles\":3000,\"machinePreset\":\"x\",\"seed\":18446744073709551615}", &err) == 0);
		std::string firmware = "[{\"id\":\"font\",\"sha1\":\"" + sha1Of("f") + "\"}]";
		assert(ce_project_set_firmware_text(p, firmware.c_str(), &err) == 0);
		const char *lump = "LogKey:#P1 Up|\n|..|\n|U.|\n";
		ce_project_set_log_text(p, lump, std::strlen(lump));

		assert(ce_project_file_add(p, "disc1.cue", "cdrom", (g_dir + "/disc1.cue").c_str(), &err) == 0);
		// the cue brought its bin, as support, right after it
		assert(ce_project_file_count(p) == 2);
		assert(std::strcmp(ce_project_file_name(p, 1), "disc one.bin") == 0);
		assert(std::strcmp(ce_project_file_slot(p, 1), "support") == 0);
		assert(ce_project_file_status(p, 1) == 0);
		assert(ce_project_file_add(p, "disc2.iso", "cdrom", (g_dir + "/disc2.iso").c_str(), &err) == 0);
		assert(ce_project_file_add(p, "save.hdd", "hdd", (g_dir + "/save.hdd").c_str(), &err) == 0);
		assert(ce_project_files_ok(p) == 1);

		// markers keep frame order regardless of insertion order
		ce_project_marker_add(p, 500, "lap two", 1);
		ce_project_marker_add(p, 10, "power on", 0);
		ce_project_marker_add(p, 500, "lap two again", 1);
		assert(ce_project_marker_count(p) == 3);
		assert(ce_project_marker_frame(p, 0) == 10);
		assert(std::strcmp(ce_project_marker_text(p, 1), "lap two") == 0);
		assert(ce_project_marker_keep_state(p, 0) == 0);
		assert(ce_project_marker_keep_state(p, 1) == 1);

		ce_project_branch_add(p, "risky route", 400, "2026-08-26 21:00:00", "|..|\n", 5);
		ce_project_branch_marker_add(p, 0, 350, "the setup", 1);
		ce_project_branch_marker_add(p, 0, 360, "no state here", 0);
		ce_project_header_set(p, "Author", "sergio");
		ce_project_header_set(p, "Platform", "DOS");
		ce_project_header_set(p, "Platform", "DOSBox");
		ce_project_header_set(p, "Doomed", "yes");
		ce_project_header_set(p, "Doomed", nullptr);

		assert(ce_project_validate(p, SLOTS_DECL, std::strlen(SLOTS_DECL), &err) == 0);
		assert(ce_project_save(p, path.c_str(), &err) == 0);
		ce_project_free(p);
	}

	// ---- open: everything round-trips; files come back unresolved ----
	{
		ce_project *p = ce_project_open(path.c_str(), &err);
		assert(p != nullptr);
		assert(std::strcmp(ce_project_title(p), "Alley Cat in 4:03") == 0);
		assert(std::strcmp(ce_project_description(p), "the funny cat game") == 0);
		assert(std::strcmp(ce_project_core_name(p), "dosbox-x") == 0);
		assert(std::strcmp(ce_project_core_version(p), "abc123+local") == 0);
		assert(std::strcmp(ce_project_core_sha1(p), sha1Of("a package").c_str()) == 0);
		assert(ce_project_rerecords(p) == 421);
		uint64_t len = 0;
		assert(std::string(ce_project_settings_text(p, &len)).find("\"cpuCycles\":3000") != std::string::npos);
		// a 64-bit setting comes back with every digit (issue 358)
		assert(std::string(ce_project_settings_text(p, &len)).find("\"seed\":18446744073709551615") != std::string::npos);
		assert(std::string(ce_project_firmware_text(p, &len)).find("\"font\"") != std::string::npos);
		/* the input comes back as the text block a movie is, the same key and entries */
		const char *logText = ce_project_log_text(p, &len);
		assert(std::string(logText, len) == "[Input]\nLogKey:#P1 Up|\n|..|\n|U.|\n[/Input]\n");
		assert(ce_project_marker_count(p) == 3 && ce_project_marker_frame(p, 2) == 500);
		assert(ce_project_branch_count(p) == 1);
		assert(std::strcmp(ce_project_branch_name(p, 0), "risky route") == 0);
		assert(ce_project_branch_frame(p, 0) == 400);
		assert(std::strcmp(ce_project_branch_time(p, 0), "2026-08-26 21:00:00") == 0);
		const char *branchLog = ce_project_branch_log_text(p, 0, &len);
		assert(std::string(branchLog, len) == "[Input]\n|..|\n[/Input]\n");
		assert(ce_project_branch_marker_count(p, 0) == 2);
		assert(ce_project_branch_marker_frame(p, 0, 0) == 350);
		assert(std::strcmp(ce_project_branch_marker_text(p, 0, 0), "the setup") == 0);
		/* a branch marker's keep-a-state choice survives the file, as a marker's does */
		assert(ce_project_branch_marker_keep_state(p, 0, 0) == 1);
		assert(ce_project_branch_marker_keep_state(p, 0, 1) == 0);
		assert(ce_project_header_count(p) == 2);
		assert(std::strcmp(ce_project_header_key_at(p, 0), "Author") == 0);
		assert(std::strcmp(ce_project_header_get(p, "Platform"), "DOSBox") == 0);
		assert(ce_project_header_get(p, "Doomed") == nullptr);

		assert(ce_project_file_count(p) == 4);
		assert(ce_project_file_status(p, 0) == 1);
		assert(ce_project_file_size(p, 0) == 0);            // unresolved: nothing known yet
		assert(ce_project_file_source_path(p, 0)[0] == '\0');
		assert(ce_project_files_ok(p) == 0);
		assert(std::strcmp(ce_project_file_sha1(p, 2), sha1Of("second disc bytes").c_str()) == 0);

		// resolution by folder: the "files beside the project" convenience
		assert(ce_project_resolve_dir(p, g_dir.c_str()) == 4);
		assert(ce_project_files_ok(p) == 1);
		// a resolved file says how big it is and WHERE it is; the bytes stay on
		// the disk until a machine asks for them
		assert(ce_project_file_size(p, 2) == std::strlen("second disc bytes"));
		assert(std::string(ce_project_file_source_path(p, 2)) == g_dir + "/disc2.iso");

		// the slot map the session mounts: manifest order, support excluded
		const char *slots = ce_project_slots_text(p, &len);
		assert(std::string(slots, len) ==
			"{\"cdrom\":[\"disc1.cue\",\"disc2.iso\"],\"hdd\":[\"save.hdd\"]}");
		ce_project_free(p);
	}

	// ---- resolution by path: the on-disk name may differ ----
	{
		writeFile("renamed-elsewhere.iso", "second disc bytes");
		ce_project *p = ce_project_open(path.c_str(), &err);
		assert(p != nullptr);
		assert(ce_project_file_resolve(p, 2, (g_dir + "/renamed-elsewhere.iso").c_str(), &err) == 0);
		assert(ce_project_file_status(p, 2) == 0);
		assert(std::strcmp(ce_project_file_name(p, 2), "disc2.iso") == 0); // the canonical name is the label
		// an unreadable path is an error, not a status
		assert(ce_project_file_resolve(p, 3, (g_dir + "/not-there").c_str(), &err) != 0);
		assert(err != nullptr && ce_project_file_status(p, 3) == 1);
		ce_project_free(p);
	}

	// ---- a mismatch is a status, and save records what actually ran ----
	{
		writeFile("disc2.iso", "TAMPERED disc bytes");
		ce_project *p = ce_project_open(path.c_str(), &err);
		assert(p != nullptr);
		ce_project_resolve_dir(p, g_dir.c_str());
		assert(ce_project_file_status(p, 2) == 2);
		assert(ce_project_files_ok(p) == 0);
		assert(std::strcmp(ce_project_file_actual_sha1(p, 2), sha1Of("TAMPERED disc bytes").c_str()) == 0);
		assert(ce_project_save(p, (g_dir + "/overridden.chimeraProject").c_str(), &err) == 0);
		ce_project_free(p);

		ce_project *q = ce_project_open((g_dir + "/overridden.chimeraProject").c_str(), &err);
		assert(q != nullptr);
		assert(std::strcmp(ce_project_file_sha1(q, 2), sha1Of("TAMPERED disc bytes").c_str()) == 0);
		ce_project_free(q);
		writeFile("disc2.iso", "second disc bytes"); // restore
	}

	// ---- validation against the declaration ----
	{
		ce_project *p = ce_project_new();
		// unknown slot
		assert(ce_project_file_add(p, "disc2.iso", "tape", (g_dir + "/disc2.iso").c_str(), &err) == 0);
		assert(ce_project_validate(p, SLOTS_DECL, std::strlen(SLOTS_DECL), &err) != 0);
		assert(std::strstr(err, "tape") != nullptr);
		ce_project_file_remove(p, 0);
		// wrong format for the slot
		assert(ce_project_file_add(p, "disc2.iso", "floppy", (g_dir + "/disc2.iso").c_str(), &err) == 0);
		assert(ce_project_validate(p, SLOTS_DECL, std::strlen(SLOTS_DECL), &err) != 0);
		ce_project_file_remove(p, 0);
		// cardinality: two hdds against max 1
		assert(ce_project_file_add(p, "save.hdd", "hdd", (g_dir + "/save.hdd").c_str(), &err) == 0);
		assert(ce_project_file_add(p, "save2.hdd", "hdd", (g_dir + "/save.hdd").c_str(), &err) == 0);
		assert(ce_project_validate(p, SLOTS_DECL, std::strlen(SLOTS_DECL), &err) != 0);
		ce_project_file_remove(p, 1);
		assert(ce_project_validate(p, SLOTS_DECL, std::strlen(SLOTS_DECL), &err) == 0);
		ce_project_file_remove(p, 0);
		// nothing at all: the atLeastOneOf group speaks
		assert(ce_project_validate(p, SLOTS_DECL, std::strlen(SLOTS_DECL), &err) != 0);
		assert(std::strstr(err, "floppy, cdrom, hdd") != nullptr);
		// a file with no extension is a format too, when a slot declares it
		// as the empty string: some consoles keep a game as one file with a
		// long name and nothing after it (chimera issue 250)
		const char *bare = "{\"slots\":[{\"id\":\"game\",\"min\":1,\"max\":1,\"formats\":[\"iso\",\"\"]}]}";
		const char *dotted = "{\"slots\":[{\"id\":\"game\",\"min\":1,\"max\":1,\"formats\":[\"iso\"]}]}";
		assert(ce_project_file_add(p, "0123456789ABCDEF", "game", (g_dir + "/disc2.iso").c_str(), &err) == 0);
		assert(ce_project_validate(p, bare, std::strlen(bare), &err) == 0);
		assert(ce_project_validate(p, dotted, std::strlen(dotted), &err) != 0);
		ce_project_file_remove(p, 0);
		assert(ce_project_file_add(p, "save.hdd", "game", (g_dir + "/save.hdd").c_str(), &err) == 0);
		assert(ce_project_validate(p, bare, std::strlen(bare), &err) != 0);
		ce_project_file_remove(p, 0);
		// a min is enforced
		const char *needOne = "{\"slots\":[{\"id\":\"rom\",\"min\":1,\"max\":1}]}";
		assert(ce_project_validate(p, needOne, std::strlen(needOne), &err) != 0);
		// mutually exclusive slots: files in a slot the manifest itself makes
		// unavailable are structurally invalid
		const char *exclusive =
			"{\"slots\":["
			"{\"id\":\"cart\",\"formats\":[\"iso\"],\"exposedWhen\":{\"not\":{\"slot\":\"fdsx\"}}},"
			"{\"id\":\"fdsx\",\"formats\":[\"hdd\"],\"exposedWhen\":{\"not\":{\"slot\":\"cart\"}}}]}";
		assert(ce_project_file_add(p, "disc2.iso", "cart", (g_dir + "/disc2.iso").c_str(), &err) == 0);
		assert(ce_project_validate(p, exclusive, std::strlen(exclusive), &err) == 0);
		assert(ce_project_file_add(p, "save.hdd", "fdsx", (g_dir + "/save.hdd").c_str(), &err) == 0);
		assert(ce_project_validate(p, exclusive, std::strlen(exclusive), &err) != 0);
		assert(std::strstr(err, "unavailable") != nullptr);
		ce_project_file_remove(p, 1);
		assert(ce_project_validate(p, exclusive, std::strlen(exclusive), &err) == 0);
		// an unexposed slot's minimum does not bind: "cart min 1 unless
		// fdsx" plus "fdsx min 1 unless cart" means exactly one of either
		const char *eitherOne =
			"{\"slots\":["
			"{\"id\":\"cart\",\"min\":1,\"max\":1,\"formats\":[\"iso\"],\"exposedWhen\":{\"not\":{\"slot\":\"fdsx\"}}},"
			"{\"id\":\"fdsx\",\"min\":1,\"max\":1,\"formats\":[\"hdd\"],\"exposedWhen\":{\"not\":{\"slot\":\"cart\"}}}]}";
		assert(ce_project_validate(p, eitherOne, std::strlen(eitherOne), &err) == 0);
		ce_project_file_remove(p, 0);
		ce_project_free(p);
	}

	// ---- creation-side errors ----
	{
		ce_project *p = ce_project_new();
		assert(ce_project_file_add(p, "disc2.iso", "cdrom", (g_dir + "/disc2.iso").c_str(), &err) == 0);
		// duplicates, unreadable sources, bad names and slots
		assert(ce_project_file_add(p, "disc2.iso", "cdrom", (g_dir + "/disc2.iso").c_str(), &err) != 0);
		assert(ce_project_file_add(p, "gone.iso", "cdrom", (g_dir + "/gone.iso").c_str(), &err) != 0);
		assert(ce_project_file_add(p, "../evil.iso", "cdrom", (g_dir + "/disc2.iso").c_str(), &err) != 0);
		assert(ce_project_file_add(p, "disc3.iso", "CDROM", (g_dir + "/disc2.iso").c_str(), &err) != 0);
		// a cue whose bin is not next to it: all or nothing
		writeFile("lonely.cue", "FILE \"nowhere.bin\" BINARY\n  TRACK 01 MODE1/2048\n");
		assert(ce_project_file_add(p, "lonely.cue", "cdrom", (g_dir + "/lonely.cue").c_str(), &err) != 0);
		assert(std::strstr(err, "nowhere.bin") != nullptr);
		assert(ce_project_file_count(p) == 1);
		ce_project_free(p);
	}

	// ---- cue closure at save ----
	{
		ce_project *p = ce_project_new();
		assert(ce_project_file_add(p, "disc1.cue", "cdrom", (g_dir + "/disc1.cue").c_str(), &err) == 0);
		ce_project_file_remove(p, 1); // drop the bin the cue brought
		assert(ce_project_save(p, (g_dir + "/bad.chimeraProject").c_str(), &err) != 0);
		assert(std::strstr(err, "disc one.bin") != nullptr);
		ce_project_free(p);
	}

	// ---- structural rejections at open ----
	auto expectReject = [&](const char *tag, const std::string &json)
	{
		writeFile("bad.chimeraProject", json);
		err = nullptr;
		ce_project *p = ce_project_open((g_dir + "/bad.chimeraProject").c_str(), &err);
		if (p != nullptr)
		{
			std::fprintf(stderr, "%s: expected rejection\n", tag);
			assert(false);
		}
		assert(err != nullptr && err[0] != '\0');
	};
	const std::string goodSha = sha1Of("x");
	expectReject("not json", "so very not json");
	expectReject("not an object", "[1,2,3]");
	expectReject("unknown key", "{\"totallyNew\":1}");
	/* subtitles are gone from the format (owner, 2026-11-01, issue 440): a project with some is not opened */
	expectReject("subtitles", "{\"subtitles\":[\"subtitle 100 10 10 300 FFFFFFFF hello\"]}");
	expectReject("title type", "{\"title\":5}");
	expectReject("core incomplete", "{\"core\":{\"name\":\"x\"}}");
	expectReject("core sha malformed", "{\"core\":{\"name\":\"x\",\"version\":\"v\",\"sha1\":\"nope\"}}");
	expectReject("file path name",
		"{\"files\":[{\"name\":\"a/b.iso\",\"sha1\":\"" + goodSha + "\",\"slot\":\"cdrom\"}]}");
	expectReject("file dup",
		"{\"files\":[{\"name\":\"a.iso\",\"sha1\":\"" + goodSha + "\",\"slot\":\"cdrom\"},"
		"{\"name\":\"a.iso\",\"sha1\":\"" + goodSha + "\",\"slot\":\"cdrom\"}]}");
	expectReject("file bad sha",
		"{\"files\":[{\"name\":\"a.iso\",\"sha1\":\"nothex\",\"slot\":\"cdrom\"}]}");
	expectReject("file bad slot",
		"{\"files\":[{\"name\":\"a.iso\",\"sha1\":\"" + goodSha + "\",\"slot\":\"CD ROM\"}]}");
	expectReject("settings type", "{\"settings\":[]}");
	expectReject("branch incomplete", "{\"branches\":[{\"name\":\"x\"}]}");

	// an empty project is structurally fine (validation is the core's say)
	{
		writeFile("empty.chimeraProject", "{}");
		ce_project *p = ce_project_open((g_dir + "/empty.chimeraProject").c_str(), &err);
		assert(p != nullptr);
		assert(ce_project_file_count(p) == 0 && ce_project_rerecords(p) == 0);
		uint64_t len = 0;
		assert(std::string(ce_project_settings_text(p, &len)) == "{}");
		ce_project_free(p);
	}

	// the cue parser, exported: what a form shows as "(+ N tracks)"
	{
		const char *cue =
			"FILE \"track01.bin\" BINARY\n  TRACK 01 MODE1/2352\nFILE track02.bin BINARY\n";
		uint64_t len = 0;
		const char *refs = ce_cue_references(cue, std::strlen(cue), &len);
		assert(std::string(refs, len) == "[\"track01.bin\",\"track02.bin\"]");
		refs = ce_cue_references("not a cue at all", 16, &len);
		assert(std::string(refs, len) == "[]");
	}

	// A file no 32-bit length could count.
	//
	// This is the whole reason a project keeps paths rather than bytes: a PS2
	// disc is over four gigabytes. What is checked is that the size survives
	// intact (a truncation to 32 bits would show here and nowhere else) and
	// that adding it costs nothing but the hash - the file is sparse, so a
	// build that read it whole would have to find 5GB of memory to do it.
	//
	// OPT-IN, because it hashes five gigabytes and takes half a minute, and
	// this gate runs on every build: CHIMERA_BIG_FILE_TEST=1 to include it.
	if (std::getenv("CHIMERA_BIG_FILE_TEST") != nullptr)
	{
		const uint64_t huge = 5ULL << 30;
		if (writeSparse("huge.iso", huge))
		{
			ce_project *p = ce_project_new();
			const char *err = nullptr;
			assert(ce_project_file_add(p, "huge.iso", "disc", (g_dir + "/huge.iso").c_str(), &err) == 0);
			assert(ce_project_file_size(p, 0) == huge);
			assert(ce_project_file_status(p, 0) == 0);
			assert(std::string(ce_project_file_source_path(p, 0)) == g_dir + "/huge.iso");
			ce_project_free(p);
			std::remove((g_dir + "/huge.iso").c_str());
		}
		else
		{
			std::printf("project: skipped the 5GB entry (no sparse file here)\n");
		}
	}

	// ---- TAStudio's layout (issue #83): kept verbatim, only an object, absent unless written ----
	{
		const std::string lpath = g_dir + "/layout.chimeraProject";
		ce_project *p = ce_project_new();
		ce_project_set_title(p, "layout");
		uint64_t len = 7;
		assert(std::string(ce_project_tastudio_text(p, &len)).empty() && len == 0);
		assert(ce_project_set_tastudio_text(p, "[1,2]", &err) != 0 && err != nullptr);
		assert(ce_project_set_tastudio_text(p, "{\"Columns\":[[{\"Name\":\"A\",\"Width\":23}]]}", &err) == 0);
		assert(ce_project_save(p, lpath.c_str(), &err) == 0);
		ce_project_free(p);

		p = ce_project_open(lpath.c_str(), &err);
		assert(p != nullptr);
		assert(std::string(ce_project_tastudio_text(p, &len)).find("\"Width\":23") != std::string::npos);
		assert(ce_project_set_tastudio_text(p, "", &err) == 0);
		assert(ce_project_save(p, lpath.c_str(), &err) == 0);
		ce_project_free(p);

		std::string text;
		if (FILE *f = std::fopen(lpath.c_str(), "rb"))
		{
			char buf[4096];
			size_t n;
			while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
			std::fclose(f);
		}
		assert(!text.empty() && text.find("\"tastudio\"") == std::string::npos);
		std::remove(lpath.c_str());
	}

	// ---- a save that fails part way leaves the last good project ----
	// The cap on file size makes the second save fail after its first 64 KiB,
	// as a full disk would. A save that empties the file before writing it
	// loses the first save; one written beside the file and moved over it does not.
#ifndef _WIN32
	{
		const std::string kpath = g_dir + "/kept.chimeraProject";
		auto readText = [](const std::string &path) {
			std::string text;
			if (FILE *f = std::fopen(path.c_str(), "rb"))
			{
				char buf[4096];
				size_t n;
				while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
				std::fclose(f);
			}
			return text;
		};
		ce_project *p = ce_project_new();
		ce_project_set_title(p, "kept");
		assert(ce_project_save(p, kpath.c_str(), &err) == 0);
		const std::string before = readText(kpath);
		assert(!before.empty() && before.size() < 64 * 1024);

		ce_project_set_description(p, std::string(1 << 20, 'x').c_str());
		struct rlimit old;
		assert(getrlimit(RLIMIT_FSIZE, &old) == 0);
		struct rlimit capped = old;
		capped.rlim_cur = 64 * 1024;
		void (*prev)(int) = std::signal(SIGXFSZ, SIG_IGN);
		assert(setrlimit(RLIMIT_FSIZE, &capped) == 0);
		const int rc = ce_project_save(p, kpath.c_str(), &err);
		assert(setrlimit(RLIMIT_FSIZE, &old) == 0);
		std::signal(SIGXFSZ, prev);
		assert(rc != 0 && err != nullptr);
		assert(readText(kpath) == before);
		assert(!std::filesystem::exists(kpath + ".writing"));

		// and with room again, the save goes through whole
		assert(ce_project_save(p, kpath.c_str(), &err) == 0);
		assert(readText(kpath).size() > (1u << 20));
		ce_project_free(p);
		std::remove(kpath.c_str());
	}
#endif

	{ // A project with no id (written before ids, or by hand) gets the same one
	  // at every open until a save writes it in, so the work of a session that
	  // crashed before its first save is found again (2026-10-18).
		writeFile("idless.chimeraProject",
			"{\"title\":\"no id\",\"core\":{\"name\":\"Synth\",\"version\":\"\",\"sha1\":\"\"},\"files\":[]}");
		const std::string ipath = g_dir + "/idless.chimeraProject";
		const char *err = nullptr;
		ce_project *a = ce_project_open(ipath.c_str(), &err);
		ce_project *b = ce_project_open(ipath.c_str(), &err);
		assert(a != nullptr && b != nullptr);
		const std::string idA = ce_project_id(a);
		assert(!idA.empty() && idA == ce_project_id(b));
		assert(ce_project_save(a, ipath.c_str(), &err) == 0);
		ce_project *c = ce_project_open(ipath.c_str(), &err);
		assert(c != nullptr && idA == ce_project_id(c));

		// Saved under a name of its own, it is a project of its own (owner,
		// 2026-10-29): another id, written in, while the first file keeps its
		// own; a save that fails leaves the id it had.
		const std::string copyPath = g_dir + "/idless copy.chimeraProject";
		assert(ce_project_save_as(c, copyPath.c_str(), &err) == 0);
		const std::string idCopy = ce_project_id(c);
		assert(!idCopy.empty() && idCopy != idA);
		ce_project *copy = ce_project_open(copyPath.c_str(), &err);
		ce_project *first = ce_project_open(ipath.c_str(), &err);
		assert(copy != nullptr && first != nullptr && idCopy == ce_project_id(copy) && idA == ce_project_id(first));
		const std::string nowhere = g_dir + "/no such folder/x.chimeraProject";
		assert(ce_project_save_as(c, nowhere.c_str(), &err) != 0 && idCopy == ce_project_id(c));
		ce_project_free(copy);
		ce_project_free(first);
		std::remove(copyPath.c_str());
		ce_project_free(a);
		ce_project_free(b);
		ce_project_free(c);
		std::remove(ipath.c_str());
	}

	// ---- which machine a project makes: core build, settings, firmware, files ----
	{
		auto idOf = [](ce_project *p, const char *running) {
			char id[41];
			ce_project_machine_id(p, running, id);
			return std::string(id);
		};
		auto make = [&](const char *settings, const char *file) {
			ce_project *p = ce_project_new();
			ce_project_set_core(p, "dosbox-x", "1", sha1Of("a package").c_str());
			assert(ce_project_set_settings_text(p, settings, &err) == 0);
			assert(ce_project_file_add(p, file, "cdrom", (g_dir + "/" + file).c_str(), &err) == 0);
			return p;
		};
		writeFile("same bytes.iso", "second disc bytes");
		ce_project *a = make("{\"cpuCycles\":3000,\"machinePreset\":\"x\",\"deep\":{\"b\":1,\"a\":2}}", "disc2.iso");
		const std::string id = idOf(a, nullptr);
		assert(id.size() == 40);
		// the same settings in another order, a file of the same bytes by another name
		ce_project *b = make("{ \"deep\": {\"a\": 2, \"b\": 1}, \"machinePreset\": \"x\", \"cpuCycles\": 3000 }", "same bytes.iso");
		assert(idOf(b, nullptr) == id);
		// the pin given as the build that runs is the pin; another build is another machine
		assert(idOf(a, sha1Of("a package").c_str()) == id && idOf(a, "") == id);
		assert(idOf(a, sha1Of("another package").c_str()) != id);
		// another setting, other firmware, another file
		ce_project *c = make("{\"cpuCycles\":3001,\"machinePreset\":\"x\",\"deep\":{\"b\":1,\"a\":2}}", "disc2.iso");
		assert(idOf(c, nullptr) != id);
		assert(ce_project_set_firmware_text(b, ("[{\"id\":\"font\",\"sha1\":\"" + sha1Of("f") + "\"}]").c_str(), &err) == 0);
		assert(idOf(b, nullptr) != id);
		ce_project *d = make("{\"cpuCycles\":3000,\"machinePreset\":\"x\",\"deep\":{\"b\":1,\"a\":2}}", "save.hdd");
		assert(idOf(d, nullptr) != id);
		// the clock's start (issue 366): the sandbox's own is no change, another is another machine
		assert(ce_project_set_clock_start(a, CE_CLOCK_START_DEFAULT) == 0 && idOf(a, nullptr) == id);
		assert(ce_project_set_clock_start(a, 1893553445) == 0 && idOf(a, nullptr) != id);
		assert(ce_project_set_clock_start(a, -1) == 0 && idOf(a, nullptr) == id);
		ce_project_free(a);
		ce_project_free(b);
		ce_project_free(c);
		ce_project_free(d);
	}

	// ---- the clock's start (issue 366): kept, written only when it is not the
	// sandbox's own, and refused when it is not a time ----
	{
		auto text = [](const std::string &file) {
			std::string out;
			FILE *f = std::fopen(file.c_str(), "rb");
			assert(f != nullptr);
			for (int c; (c = std::fgetc(f)) != EOF;) out.push_back(static_cast<char>(c));
			std::fclose(f);
			return out;
		};
		const std::string cpath = g_dir + "/clock.chimeraProject";
		ce_project *p = ce_project_new();
		assert(ce_project_clock_start(p) == -1);
		assert(ce_project_set_clock_start(p, 7258118401) == 1 && ce_project_clock_start(p) == -1);
		assert(ce_project_set_clock_start(p, CE_CLOCK_START_DEFAULT) == 0 && ce_project_clock_start(p) == -1);
		assert(ce_project_save(p, cpath.c_str(), &err) == 0);
		assert(text(cpath).find("clockStart") == std::string::npos);   /* the default says nothing */
		assert(ce_project_set_clock_start(p, 1893553445) == 0 && ce_project_clock_start(p) == 1893553445);
		assert(ce_project_save(p, cpath.c_str(), &err) == 0);
		assert(text(cpath).find("2030-01-02T03:04:05Z") != std::string::npos);
		ce_project_free(p);
		p = ce_project_open(cpath.c_str(), &err);
		assert(p != nullptr && ce_project_clock_start(p) == 1893553445);
		/* the last second there is, and a leap day */
		assert(ce_project_set_clock_start(p, 7258118400) == 0 && ce_project_save(p, cpath.c_str(), &err) == 0);
		assert(text(cpath).find("2200-01-01T00:00:00Z") != std::string::npos);
		ce_project_free(p);
		writeFile("clock-leap.chimeraProject", "{\"clockStart\": \"2024-02-29T23:59:59Z\"}");
		p = ce_project_open((g_dir + "/clock-leap.chimeraProject").c_str(), &err);
		assert(p != nullptr && ce_project_clock_start(p) == 1709251199);
		ce_project_free(p);
		for (const char *bad : { "2023-02-29T00:00:00Z", "1969-12-31T23:59:59Z", "2200-01-01T00:00:01Z",
			"2030-01-02 03:04:05", "2030-01-02T03:04:05", "tomorrow" })
		{
			writeFile("clock-bad.chimeraProject", std::string("{\"clockStart\": \"") + bad + "\"}");
			err = nullptr;
			assert(ce_project_open((g_dir + "/clock-bad.chimeraProject").c_str(), &err) == nullptr);
			assert(err != nullptr && std::strstr(err, "clockStart") != nullptr);
		}
		std::remove(cpath.c_str());
	}

	{ // The input as the file holds it (owner, 2026-10-28): "inputs", its key's
	  // groups of columns and one entry a line, not the movie's text block as a
	  // string; the old string is refused; the key comes back byte for byte
	  // even where a name holds the key's own separators
		auto text = [](const std::string &path) {
			std::string out;
			if (FILE *f = std::fopen(path.c_str(), "rb"))
			{
				char buf[4096];
				size_t n;
				while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
				std::fclose(f);
			}
			return out;
		};
		const std::string ipath = g_dir + "/inputs.chimeraProject";
		ce_project *p = ce_project_new();
		const char *lump = "[Input]\nLogKey:#Reset|Power|#P1 Up|P1 \"#|\n|..|.U|\n|.P|..|\n[/Input]\n";
		ce_project_set_log_text(p, lump, std::strlen(lump));
		const char *exotic = "[Input]\nLogKey:#| @|# >|#P1 A|P1 #|\n|.@|.|\n[/Input]\n";
		ce_project_branch_add(p, "odd names", 1, "", exotic, std::strlen(exotic));
		assert(ce_project_save(p, ipath.c_str(), &err) == 0);
		ce_project_free(p);
		const std::string saved = text(ipath);
		assert(saved.find("\"input\"") == std::string::npos && saved.find("[Input]") == std::string::npos);
		assert(saved.find("\"columns\":\t[\n\t\t\t[\"Reset\", \"Power\"],\n\t\t\t[\"P1 Up\", \"P1 \\\"#\"]\n\t\t]") != std::string::npos);
		assert(saved.find("\"entries\":\t[\n\t\t\t\"|..|.U|\",\n\t\t\t\"|.P|..|\"\n\t\t]") != std::string::npos);
		p = ce_project_open(ipath.c_str(), &err);
		assert(p != nullptr);
		uint64_t len = 0;
		const char *got = ce_project_log_text(p, &len);
		assert(std::string(got, len) == lump);
		got = ce_project_branch_log_text(p, 0, &len);
		assert(std::string(got, len) == exotic);
		ce_project_free(p);
		// the text block as builds before 2026-10-28 wrote it is not read: no
		// old forms are kept (owner, 2026-10-28)
		writeFile("old-input.chimeraProject", "{\"input\": \"[Input]\\nLogKey:#P1 Up|\\n|U|\\n[/Input]\\n\"}");
		err = nullptr;
		assert(ce_project_open((g_dir + "/old-input.chimeraProject").c_str(), &err) == nullptr);
		assert(err != nullptr && std::strstr(err, "unknown project key \"input\"") != nullptr);
		// what is not an input is refused, and says where
		for (const char *bad : { "{\"inputs\": [], \"input\": \"\"}", "{\"inputs\": {\"entries\": [\"LogKey:#A|\"]}}",
			"{\"inputs\": {\"columns\": [\"A\"]}}", "{\"inputs\": {\"keys\": []}}",
			"{\"inputs\": {\"columns\": [], \"key\": \"#\"}}" })
		{
			writeFile("bad-input.chimeraProject", bad);
			err = nullptr;
			assert(ce_project_open((g_dir + "/bad-input.chimeraProject").c_str(), &err) == nullptr);
			assert(err != nullptr && std::strstr(err, "input") != nullptr);
		}
	}

	std::printf("project: all assertions passed\n");
	return 0;
}
