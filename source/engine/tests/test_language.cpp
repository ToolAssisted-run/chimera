/* test_language.cpp - Chimera's language tables (docs/translations.md).
 *
 * Every text Chimera shows is a key in one table per language, and every
 * language has every key (owner, 2026-10-28). This checks the tables in the
 * languages folder against English - the same keys, the same %1..%9 and %n, a menu's
 * shortcut letter kept, the spaces at either end kept, the plural forms the
 * language takes - and what reads them: the plural rules, the filling, a
 * language used and another in its place, a core package's texts and what
 * its declarations are checked for, and the C ABI.
 *
 * Plain asserts, run by `meson test -C build/meson-linux`.
 */

#include "../source/language.hpp"
#include "chimera/engine.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

namespace lang = chimera::lang;

int problems = 0;
void problem(const std::string &what)
{
	if (problems++ < 40) std::fprintf(stderr, "%s\n", what.c_str());
}

} // namespace

int main()
{
	{ // a table is read, and what is not one is said
		lang::Table t;
		std::string error;
		const char good[] = R"({"name": "Test", "english": "Test", "texts": {"b.two": "Two", "a.one": "One",
			"c.frames": {"one": "%n frame", "other": "%n frames"}}})";
		assert(lang::readTable(good, sizeof good - 1, t, error));
		assert(t.name == "Test" && t.entries.size() == 3);
		assert(t.entries[0].first == "a.one");   /* sorted */
		assert(t.find("b.two") != nullptr && t.find("b.two")->text == "Two");
		assert(t.find("c.frames")->forms.size() == 2 && t.find("c.frames")->forms[0].first == "one");
		assert(t.find("d.none") == nullptr);
		const char *bad[] = {
			"[]",
			R"({"name": "x", "english": "x"})",
			R"({"name": "x", "english": "x", "texts": {"a": 3}})",
			R"({"name": "x", "english": "x", "texts": {"a": {"one": 1}}})",
			R"({"name": "x", "english": "x", "texts": {"a": "1", "a": "2"}})",
		};
		for (const char *b : bad)
		{
			error.clear();
			assert(!lang::readTable(b, std::strlen(b), t, error));
			assert(!error.empty());
		}
	}

	{ // the plural rules, whole numbers
		assert(lang::pluralForm("en", 1) == "one" && lang::pluralForm("en", 0) == "other" && lang::pluralForm("en", 2) == "other");
		assert(lang::pluralForm("fr", 0) == "one" && lang::pluralForm("fr", 1) == "one" && lang::pluralForm("fr", 2) == "other");
		assert(lang::pluralForm("pt", 0) == "one" && lang::pluralForm("pt", 2) == "other");
		assert(lang::pluralForm("ja", 1) == "other" && lang::pluralForm("zh_CN", 5) == "other");
		const int ru[][2] = {{1, 0}, {21, 0}, {101, 0}, {11, 2}, {2, 1}, {3, 1}, {4, 1}, {22, 1}, {12, 2}, {14, 2}, {5, 2}, {0, 2}, {111, 2}};
		const char *ruForms[] = {"one", "few", "many"};
		for (const auto &c : ru)
		{
			assert(lang::pluralForm("ru", c[0]) == ruForms[c[1]]);
			assert(lang::pluralForm("uk", c[0]) == ruForms[c[1]]);
		}
		assert(lang::pluralForm("pl", 1) == "one" && lang::pluralForm("pl", 21) == "many" && lang::pluralForm("pl", 22) == "few");
		assert(lang::pluralForm("cs", 1) == "one" && lang::pluralForm("cs", 4) == "few" && lang::pluralForm("cs", 5) == "other");
		assert(lang::pluralForm("ro", 0) == "few" && lang::pluralForm("ro", 19) == "few" && lang::pluralForm("ro", 20) == "other"
			&& lang::pluralForm("ro", 102) == "few");
		assert(lang::pluralForms("xx").empty());
	}

	{ // filling
		assert(lang::fill("%1 of %2", {"3", "4"}) == "3 of 4");
		assert(lang::fill("%2, then %1", {"a", "b"}) == "b, then a");
		assert(lang::fill("100% done, %3 left", {"a"}) == "100% done, %3 left");
		assert(lang::fill("%1", {"%2"}) == "%2");   /* what fills is not filled again */
	}

	{ // a core package's texts: keys with their arguments, in the language in use
		std::map<std::string, std::string> files = {
			{"languages/en.json", R"({"texts": {"fw.data": "%1 of the game, which you supply.",
				"load.missing": "%1 is not in %2", "slot.help": "The disk.",
				"load.files": {"one": "%n file is missing", "other": "%n files are missing"}}})"},
			{"languages/de.json", R"({"texts": {"fw.data": "%1 des Spiels, das Sie mitbringen.",
				"load.missing": "%1 fehlt in %2", "slot.help": "Die Diskette.",
				"load.files": {"one": "%n Datei fehlt", "other": "%n Dateien fehlen"}}})"},
		};
		auto entry = [&files](const std::string &name, std::string &bytes) {
			auto it = files.find(name);
			if (it == files.end()) return false;
			bytes = it->second;
			return true;
		};
		lang::PackageTexts texts;
		texts.read(entry);
		assert(texts.text("slot.help") == "The disk.");
		assert(texts.text("load.missing\x1fGAME.DAT\x1fthe zip") == "GAME.DAT is not in the zip");
		assert(texts.text("load.files\x1f" "1") == "1 file is missing" && texts.text("load.files\x1f" "3") == "3 files are missing");
		assert(texts.text("Not a key") == "Not a key");   /* words a core wrote in are shown as they are */
		assert(texts.text("load.missing\x1f" "slot.help\x1fthe zip") == "The disk. is not in the zip");   /* a key inside */
		/* a key inside with its own arguments, and the same reason on its own */
		assert(texts.text("load.missing\x1f" "fw.data\x1e" "COL01.DAT\x1fthe zip") == "COL01.DAT of the game, which you supply. is not in the zip");
		assert(texts.text("fw.data\x1e" "COL01.DAT") == "COL01.DAT of the game, which you supply.");
		/* several texts, a line each */
		assert(texts.text("slot.help\x1d" "load.missing\x1f" "a\x1f" "b\x1d") == "The disk.\na is not in b");
		/* in a document, only the strings that are keys change: a member's
		 * name stays, and so does a number no double holds */
		assert(texts.texts(R"({"note": "slot.help", "values": {"x": "fw.data\u001fCOL\u00e901.DAT", "big": 18446744073709551615},
			"slot.help": ["Not a key", "load.missing\u001f\"a\"\u001fb\\c"]})")
			== R"({"note": "The disk.", "values": {"x": "COLé01.DAT of the game, which you supply.", "big": 18446744073709551615},
			"slot.help": ["Not a key", "\"a\" is not in b\\c"]})");
		assert(texts.texts("  [\"slot.help\"]") == "  [\"The disk.\"]");
		assert(texts.texts("slot.help") == "The disk.");   /* not a document: one text */
		assert(lang::use("de"));
		assert(texts.text("load.missing\x1fGAME.DAT\x1f" "dem Zip") == "GAME.DAT fehlt in dem Zip");
		assert(texts.text("load.files\x1f" "1") == "1 Datei fehlt");
		assert(lang::use("fr"));   /* the package has no French: its English */
		assert(texts.text("slot.help") == "The disk.");
		/* Neither the language in use nor English, nor a key the one table
		 * there is lacks, nor a table that does not read, stops anything: any
		 * table the package has gives the text (owner, 2026-10-29). */
		{
			std::map<std::string, std::string> only = {
				{"languages/de.json", R"({"texts": {"slot.help": "Die Diskette.", "load.missing": "%1 fehlt in %2"}})"},
				{"languages/es.json", R"({"texts": {"slot.help": "El disco."}})"},
				{"languages/it.json", "not a table"},
			};
			auto onlyEntry = [&only](const std::string &name, std::string &bytes) {
				auto it = only.find(name);
				if (it == only.end()) return false;
				bytes = it->second;
				return true;
			};
			lang::PackageTexts some;
			some.read(onlyEntry);
			assert(some.text("slot.help") == "Die Diskette.");   /* French in use: German comes first of what there is */
			assert(lang::use("es"));
			assert(some.text("slot.help") == "El disco." && some.text("load.missing\x1f" "a\x1f" "b") == "a fehlt in b");
			assert(lang::use("it"));
			assert(some.text("slot.help") == "Die Diskette." && some.text("Not a key") == "Not a key");
		}
		assert(lang::use("en"));

		/* the declarations: every text a key, given every argument it uses */
		files["waterbox.config"] = R"({"firmware": [{"display": "fw.data\u001fCOL01.DAT", "description": "fw.data"}],
			"settings": [{"display": "Words", "description": "slot.help"}]})";
		std::vector<std::string> lines = lang::packageProblems(entry);
		auto has = [&lines](const std::string &part) {
			for (const std::string &l : lines)
				if (l.find(part) != std::string::npos) return true;
			return false;
		};
		assert(has("settings[].display is not a key of languages/en.json: \"Words\""));
		assert(has("firmware[].description \"fw.data\" uses %1 and is given 0 argument(s)"));
		assert(!has("firmware[].display"));
		assert(!has("slot.help"));
		assert(has("languages/fr.json is not in the package"));
	}

	{ // every table there has every key English has, and nothing else
		const auto &all = lang::languages();
		assert(all.size() >= 8 && all.front().code == "en");
		std::vector<lang::Table> tables(all.size());
		for (size_t i = 0; i < all.size(); i++)
		{
			std::string error;
			if (!lang::readTableFile(all[i].code, tables[i], error)) problem(all[i].code + ": " + error);
		}
		const lang::Table *english = &tables[0];
		for (size_t i = 0; i < tables.size(); i++)
			for (const std::string &p : lang::compare(*english, tables[i], all[i].code)) problem(p);
		if (problems != 0) std::fprintf(stderr, "%d problems in the language tables\n", problems);
		assert(problems == 0);
	}

	{ // proper nouns are never translated (owner, 2026-10-31): Chimera is Chimera
		auto table = [](const char *json) {
			lang::Table t;
			std::string error;
			assert(lang::readTable(json, std::strlen(json), t, error));
			return t;
		};
		const lang::Table english = table(R"j({"texts": {"about": "About Chimera", "open": "Chimera projects (*.chimeraProject)",
			"core": "DuckStation draws it", "tool": "A column in TAStudio"}})j");
		auto problemsOf = [&](const char *json, const std::vector<std::string> &names = {}) {
			std::string all;
			for (const std::string &p : lang::compare(english, table(json), "pl", names)) all += p + "\n";
			return all;
		};
		/* a common noun beside the name, and a file's name, are fine */
		assert(problemsOf(R"j({"texts": {"about": "O programie Chimera", "open": "Projekty programu Chimera (*.chimeraProject)",
			"core": "DuckStation to rysuje", "tool": "Kolumna w narzędziu TAStudio"}})j", {"DuckStation"}) == "");
		/* a case ending on the name, or the name gone, is not */
		const std::string bent = problemsOf(R"j({"texts": {"about": "O Chimerze", "open": "Projekty Chimery (*.chimeraProject)",
			"core": "rysuje to DuckStationa", "tool": "Kolumna w TAStudiu"}})j", {"DuckStation"});
		assert(bent.find("about: Chimera is not there as itself") != std::string::npos);
		assert(bent.find("open: Chimera is not there as itself") != std::string::npos);
		assert(bent.find("core: DuckStation is not there as itself") != std::string::npos);
		assert(bent.find("tool: TAStudio is not there as itself") != std::string::npos);
		/* a letter glued on where the name is also whole elsewhere, and in a text English names it in none */
		const lang::Table plain = table(R"j({"texts": {"x": "Settings"}})j");
		const lang::Table glued = table(R"j({"texts": {"x": "Chimeran asetukset"}})j");
		assert(lang::compare(plain, glued, "fi").size() == 1 && lang::compare(plain, glued, "fi")[0].find("glued") != std::string::npos);
		/* Korean's particles are its own script: the name stays whole */
		const lang::Table korean = table(R"j({"texts": {"about": "Chimera 정보", "open": "Chimera 프로젝트 (*.chimeraProject)",
			"core": "DuckStation이 그립니다", "tool": "TAStudio의 열"}})j");
		assert(lang::compare(english, korean, "ko", {"DuckStation"}).empty());
	}

	{ // used, and another in its place; a key no table has comes back as itself
		assert(lang::inUse() == "en");
		assert(lang::tr("common.file") == "&File");
		assert(lang::tr("no.such.key") == "no.such.key");
		assert(lang::use("es"));
		assert(lang::tr("common.file") == "&Archivo");
		assert(!lang::use("xx"));
		assert(lang::inUse() == "es" && lang::tr("common.file") == "&Archivo");
		assert(lang::use("en"));
	}

	{ // the C ABI
		assert(ce_language_count() == static_cast<int32_t>(lang::languages().size()));
		assert(std::strcmp(ce_language_code(0), "en") == 0 && std::strcmp(ce_language_name(0), "English") == 0);
		bool german = false;
		for (int32_t i = 0; i < ce_language_count(); i++)
		{
			if (std::strcmp(ce_language_code(i), "de") == 0)
				german = std::strcmp(ce_language_name(i), "Deutsch") == 0 && std::strcmp(ce_language_english_name(i), "German") == 0;
		}
		assert(german);
		assert(std::strcmp(ce_language_code(-1), "") == 0 && std::strcmp(ce_language_code(ce_language_count()), "") == 0);
		assert(ce_language_use("fr") == 1 && std::strcmp(ce_language_in_use(), "fr") == 0);
		assert(ce_language_use("xx") == 0 && std::strcmp(ce_language_in_use(), "fr") == 0);
		assert(std::strcmp(ce_text("common.file", -1, nullptr, 0), "&Fichier") == 0);
		const char *args[] = {"60"};
		assert(std::strcmp(ce_language_use("en") == 1 ? ce_text("display_config.framerate_hz", -1, args, 1) : "", "Framerate: 60 Hz") == 0);
	}

	std::printf("language tables: %zu, every key in every one\n", lang::languages().size());
	return 0;
}
