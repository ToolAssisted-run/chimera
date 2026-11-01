#pragma once
/* Chimera's languages (docs/translations.md).
 *
 * Every text Chimera shows a person is a key in a language table, one table
 * per language, English among them (owner, 2026-10-28): languages/<code>.json
 * in the source, read at run time from the languages folder beside the
 * engine. The window and the view models read them through ce_text, and the
 * engine's own messages through tr() here. A key no table has comes back
 * as the key itself, so a missing one is seen rather than covered: the
 * tests require every table to have every key. */

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace chimera::lang {

struct Language
{
	std::string code;      /* "de", "pt", "zh_CN" */
	std::string name;      /* in that language: "Deutsch" */
	std::string english;   /* "German" */
};

/* the languages folder beside the engine, ending in a slash */
std::string folder();

/* the languages there, English first, then by code; read once */
const std::vector<Language> &languages();

/* Chimera's texts in this language from now on; false when there is no
 * table for it, or it cannot be read (and the one in use stays) */
bool use(const std::string &code);
std::string inUse();

/* a key's text in the language in use, with %1 to %9 replaced by args and,
 * for a key with plural forms, the form for count chosen and its %n replaced
 * by count (a count below zero: the "other" form) */
std::string tr(const char *key, std::initializer_list<std::string> args = {});
std::string tr(const char *key, int64_t count, std::initializer_list<std::string> args = {});

/* The plural forms a language's texts give, by the names CLDR uses (one,
 * few, many, other), for whole numbers; empty for a language with no rule
 * here, whose table the tests refuse. */
std::vector<std::string> pluralForms(const std::string &code);
/* which of them a count takes */
std::string pluralForm(const std::string &code, int64_t count);

/* %1 to %9 in a pattern replaced by args; a % not followed by one of those
 * digits stays as it is */
std::string fill(const std::string &pattern, const std::vector<std::string> &args);

/* A table, read from its JSON: false and why when it is not one. Chimera's
 * tables name their language ("name", "english"); a core package's do not. */
struct Table
{
	std::string name, english;
	struct Entry
	{
		std::string text;                                       /* no plural forms */
		std::vector<std::pair<std::string, std::string>> forms; /* form name, text */
	};
	std::vector<std::pair<std::string, Entry>> entries;        /* sorted by key */
	const Entry *find(const char *key) const;
};
bool readTable(const char *json, size_t length, Table &out, std::string &error);

/* What is wrong with another language's table against English's: a line for
 * each key missing or not English's, each text whose %1..%9 or %n differ,
 * that lost a menu's shortcut letter or the spaces at its ends, each plural
 * key without the language's forms, and each text that changed a proper noun
 * (Chimera, the programs and services the texts name, and `names`: a core
 * package's own name). Empty when nothing is; for the tests, Chimera's
 * tables and every core package's. */
std::vector<std::string> compare(const Table &english, const Table &other, const std::string &code,
	const std::vector<std::string> &names = {});

/* What keeps a core package's languages from being Chimera's, a line each:
 * a table missing (languages/<code>.json, for every language Chimera has),
 * what compare() finds against the package's English, and each field of its
 * declarations a person reads that holds no key of its English table. entry
 * gives a file of the package by its name, false when there is none. */
std::vector<std::string> packageProblems(const std::function<bool(const std::string &, std::string &)> &entry);
/* the one in the languages folder for that code */
bool readTableFile(const std::string &code, Table &out, std::string &error);

/* A core package's texts (docs/translations.md). Its declarations, and what
 * the core returns while it runs (a load error, a suggestion's note, an
 * import's notes, its game's settings), hold keys of its tables:
 * languages/<code>.json inside the package. A key's arguments follow it,
 * each after a 0x1F byte: "firmware.data_file\x1f" "COL01.DAT" is the text
 * of firmware.data_file with %1 replaced by COL01.DAT, and a key with plural
 * forms takes its count as the first argument (%n, and %1 too). An
 * argument that is a key is its text, and its own arguments follow it each
 * after a 0x1E byte: a reason put inside the core's sentence ("the save data
 * is not a zip: <why>"); such a reason shown on its own reads the same.
 * Several texts, each after a 0x1D byte, are shown a line each (a report of
 * several sentences). PackageTexts keeps the tables, so a text is put into
 * the language in use after the package is closed, and again when that
 * language changes. */
class PackageTexts
{
public:
	/* the package's tables, of every language Chimera has; entry gives a
	 * file of the package by its name, as packageProblems' does */
	void read(const std::function<bool(const std::string &, std::string &)> &entry);
	/* one text in the language in use: from the package's table of it, else
	 * from its English, else from any other table it has, in the order
	 * Chimera lists its languages (owner, 2026-10-29); a message that is no
	 * key of any comes back as it is */
	std::string text(const std::string &message) const;
	/* every string value of a JSON document that is a key, its text, and the
	 * rest of the document byte for byte (a number a parser would round
	 * stays as the core wrote it); a document that is not an object or an
	 * array is one message (text()) */
	std::string texts(const std::string &json) const;

private:
	std::map<std::string, std::string> raw_;   /* code: the table's JSON */
	mutable std::mutex lock_;
	mutable std::string code_;                 /* the language table_ is */
	mutable Table table_, english_;
	mutable bool englishRead_ = false;
	mutable std::map<std::string, Table> others_;   /* code: the rest, read when first needed */
	std::string textLocked(const std::string &message) const;
	std::string resolveLocked(const std::string &message, char separator, const std::string &code) const;
};

} // namespace chimera::lang
