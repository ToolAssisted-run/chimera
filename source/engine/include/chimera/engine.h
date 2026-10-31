/* chimera/engine.h - the engine's C ABI.
 *
 * This header is a frozen spec in the same sense as the waterbox boundary:
 * Chimera's window, jaffarPlus, and anything else that links the engine
 * compiles against exactly this. Rules:
 *
 *   - Additive changes only. Removing or reshaping anything here bumps
 *     CE_ABI_VERSION, and callers check ce_abi_version() at load.
 *   - Bulk data crosses as buffers, never a call per byte.
 *   - A returned const char* is BORROWED unless the declaration says otherwise;
 *     each declaration names the call that invalidates it.
 *   - UTF-8 everywhere. NUL-terminated unless a length is passed alongside.
 *   - An error string written through an `error_out` (or returned by a
 *     `..._last_error`) is per-thread and lives until the next such call ON
 *     THAT THREAD - and no shorter: the storage behind it is never destroyed,
 *     not when the thread ends and not when the process does. That is a
 *     requirement on the engine, not a convenience: see
 *     source/engine/source/thread_string.hpp for the Windows heap corruption
 *     that destroying it caused (chimera#123).
 */

#ifndef CHIMERA_ENGINE_H
#define CHIMERA_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Windows: the engine's API is exported EXPLICITLY. Relying on mingw's
 * export-everything default broke the moment a vendored library declared its
 * own dllexports (cJSON did), which silently un-exported the entire ce_ ABI.
 * Explicit beats implicit here, permanently. */
#if defined(_WIN32) && defined(CHIMERA_BUILDING)
#define CE_API __declspec(dllexport)
#else
#define CE_API
#endif

#define CE_ABI_VERSION 1u

/* The ABI version this library was built as. A caller compiled against a
 * different major refuses to run rather than misbehave. */
CE_API uint32_t ce_abi_version(void);

/* Build provenance as JSON, same convention as miniBox's mb_build_info():
 * a function of the inputs only, shown by the frontend and cited by movies.
 * Static string, never invalidated. */
CE_API const char *ce_build_info(void);

/* ---- do this build and this state understand each other? (issue #115) ----
 *
 * Three answers to one question, weakest first. The first two are for a person
 * to read; the third is what actually decides.
 *
 * 1. ce_version_skew: whether this Chimera and the core package a project runs
 *    on were built far enough apart to be worth mentioning. Both dates are
 *    YYYY-MM-DD - the frontend's is its commit's date, the core's is the
 *    "versionDate" its package stamps (issue #67). Returns the sentence to show,
 *    or NULL when they are close enough or either side does not say. It is a
 *    guess and never a refusal: an old pairing that works must keep working.
 *    Borrowed until the next call on this thread.
 *
 * 2. ce_state_writer_id: the build that wrote the states this session writes.
 *    The engine and the frontend come out of one repository at one commit, so
 *    this names the frontend too. Static string.
 *
 * 3. ce_state_format: the shape of a machine state as this build writes it
 *    (source/engine/source/state_format.hpp), and ce_state_format_oldest the
 *    oldest one it still reads. Every state file carries its format and every
 *    load checks it, so a state this build cannot read is refused by name
 *    instead of reaching a machine that cannot read it. The frontend records
 *    it in the project so a whole cache can be explained at open, before any
 *    one state is asked for. */
CE_API const char *ce_version_skew(
	const char *frontend_date, const char *frontend_build,
	const char *core_date, const char *core_build, const char *core_name);
CE_API const char *ce_state_writer_id(void);
CE_API uint32_t ce_state_format(void);
CE_API uint32_t ce_state_format_oldest(void);

/* ---- movie input log ----
 *
 * The [Input] lump of a movie: an ordered list of frame entries (the "|..|"
 * mnemonic lines), the LogKey describing the controller layout, and - when the
 * text came from a savestate - the frame number the state was taken at.
 *
 * This models the DATA. Policy (truncation on load, rerecord counting, movie
 * mode transitions) stays with the caller: it depends on session settings the
 * engine has no business knowing at this stage of the migration.
 */

typedef struct ce_movie_log ce_movie_log;

CE_API ce_movie_log *ce_movie_log_new(void);
CE_API void ce_movie_log_free(ce_movie_log *log);

/* Parses input-log text (an "Input Log.txt" lump, or the input block of a
 * savestate). Replaces the log's previous contents. Accepts LF, CRLF and CR
 * line ends. Returns 0 on success, nonzero on error (see _last_error).
 *
 * Quirk, kept so that every movie ever written reads the same: a "Frame N"
 * line whose N does not parse as a 32-bit integer is a hard error; a text with no "Frame" line at
 * all is NOT an error - the state frame is simply absent. A "LogKey:" line
 * takes effect even mid-log, last one wins. */
CE_API int32_t ce_movie_log_parse(ce_movie_log *log, const char *text, uint64_t len);

/* The error text of the last failed call on this log, or "" if none.
 * Invalidated by the next call on the same log. */
CE_API const char *ce_movie_log_last_error(ce_movie_log *log);

CE_API int64_t ce_movie_log_count(const ce_movie_log *log);

/* The entry for one frame, exactly as it appears in the file.
 * NULL when index is out of range. Invalidated by any mutating call. */
CE_API const char *ce_movie_log_entry(const ce_movie_log *log, int64_t index);

CE_API void ce_movie_log_add(ce_movie_log *log, const char *entry);
CE_API void ce_movie_log_clear(ce_movie_log *log);
/* Drops entries at and after count, keeping the first count. */
CE_API void ce_movie_log_truncate(ce_movie_log *log, int64_t count);
/* Out-of-range indices are ignored: the log's length never changes by set. */
CE_API void ce_movie_log_set(ce_movie_log *log, int64_t index, const char *entry);
/* index may equal the count (an append); beyond that is ignored. */
CE_API void ce_movie_log_insert(ce_movie_log *log, int64_t index, const char *entry);
/* The same entry `count` times at index, in one step: inserting many frames
 * one at a time moved every line after them each time. */
CE_API void ce_movie_log_insert_n(ce_movie_log *log, int64_t index, const char *entry, int64_t count);
/* Removes [index, index+count), clamped to the log's bounds. */
CE_API void ce_movie_log_remove_range(ce_movie_log *log, int64_t index, int64_t count);
/* Replaces dst's entries and LogKey with src's. */
CE_API void ce_movie_log_assign(ce_movie_log *dst, const ce_movie_log *src);

/* Nonzero when the parsed text carried a "Frame N" line. */
CE_API int32_t ce_movie_log_has_state_frame(const ce_movie_log *log);
CE_API int32_t ce_movie_log_state_frame(const ce_movie_log *log);

/* The LogKey, or NULL when none was parsed or set.
 * Invalidated by ce_movie_log_parse and ce_movie_log_set_key. */
CE_API const char *ce_movie_log_key(const ce_movie_log *log);
CE_API void ce_movie_log_set_key(ce_movie_log *log, const char *key);

/* First frame where the two logs differ; length of the shorter one when one
 * is a prefix of the other; -1 when identical. */
CE_API int64_t ce_movie_log_divergent_point(const ce_movie_log *a, const ce_movie_log *b);

/* Renders the whole [Input] block: header line, LogKey line, one line per
 * frame, terminator line. crlf selects the line end (the format is EOL-
 * tolerant on read; writers historically used the platform's convention).
 * The LogKey line always appears; pass the generated fallback via _set_key
 * when the log has none. Returns the text and stores its byte length in
 * *len_out (may be NULL). Invalidated by any other call on the same log. */
CE_API const char *ce_movie_log_serialize(ce_movie_log *log, int32_t crlf, uint64_t *len_out);

/* The input journal: what somebody entered survives any end of the process.
 *
 * _journal_open writes the whole log to `path` (through a fresh file that then
 * replaces it, so a crash mid-rewrite still leaves a whole journal) and from
 * then on appends every change to this log as one line, flushed to the OS the
 * moment it happens and synced to the disk at most once a second. Opening again
 * rewrites the image and continues; that is how the journal is kept short.
 * Returns 0 on success.
 *
 * _journal_close stops appending; with `remove` nonzero it also deletes the
 * journal (a session that ended cleanly has nothing to recover). Freeing the
 * log closes the journal but keeps the file.
 *
 * _journal_replay rebuilds `log` from a journal file (or from its ".new"
 * sibling when the journal itself is missing). A last line with no line end -
 * a record the crash cut short - is not replayed, and replay stops at the first
 * record it cannot read. Returns the number of records applied, or -1 when the
 * file is missing or not a journal (see _last_error). `log` should not itself
 * be journaling. */
CE_API int32_t ce_movie_log_journal_open(ce_movie_log *log, const char *path);
CE_API void ce_movie_log_journal_close(ce_movie_log *log, int32_t remove);
CE_API int32_t ce_movie_log_journaling(const ce_movie_log *log);
CE_API int64_t ce_movie_log_journal_replay(ce_movie_log *log, const char *path);

/* The frontend's own records ride the same journal - its markers and branches, which the engine
 * carries without interpreting. _journal_open_with is _journal_open with the frontend's whole image
 * (newline-separated records) written into the same fresh file right after the log's, so no rewrite
 * can separate them. _journal_note appends one record, flushed and synced exactly like an input
 * change (a record cannot hold a line end; one that does is written with spaces). A replay keeps the
 * records it found, in the order they were written, readable through _note_count and _note_at
 * (NULL out of range; invalidated by the next replay into the same log). */
CE_API int32_t ce_movie_log_journal_open_with(ce_movie_log *log, const char *path, const char *frontend_image);
CE_API void ce_movie_log_journal_note(ce_movie_log *log, const char *record);
CE_API int64_t ce_movie_log_journal_note_count(const ce_movie_log *log);
CE_API const char *ce_movie_log_journal_note_at(const ce_movie_log *log, int64_t index);

/* ---- movie header ----
 *
 * The Header.txt lump: "Key Value" per line. Parsing keeps the FIRST
 * occurrence of a key; writing keeps insertion order. Values may contain
 * spaces; keys may not.
 */

typedef struct ce_movie_header ce_movie_header;

CE_API ce_movie_header *ce_movie_header_new(void);
CE_API void ce_movie_header_free(ce_movie_header *header);

/* Replaces contents. Whitespace-only lines and lines with no value are
 * skipped; "Key  Value" parses with the separator run eaten, trailing
 * whitespace kept, as every movie's header has always been read.
 * Never fails. */
CE_API void ce_movie_header_parse(ce_movie_header *header, const char *text, uint64_t len);

CE_API int64_t ce_movie_header_count(const ce_movie_header *header);

/* Borrowed; invalidated by parse/set/free. NULL when index is out of range. */
CE_API const char *ce_movie_header_key_at(const ce_movie_header *header, int64_t index);
CE_API const char *ce_movie_header_value_at(const ce_movie_header *header, int64_t index);

/* Overwrites in place when the key exists, appends otherwise. */
CE_API void ce_movie_header_set(ce_movie_header *header, const char *key, const char *value);

/* "Key Value" per line, then a closing blank line - the old writer wrapped
 * ToString() in WriteLine(), and that extra EOL is part of the format now.
 * Invalidated by any other call on the same header. */
CE_API const char *ce_movie_header_serialize(ce_movie_header *header, int32_t crlf, uint64_t *len_out);

/* ---- plain line lumps (Comments.txt and friends) ----
 *
 * A lump that is just lines: parsing keeps every line that has a non-
 * whitespace character, in order, duplicates included; serializing writes
 * each line then the same closing blank line as every other text lump.
 */

typedef struct ce_text_lines ce_text_lines;

CE_API ce_text_lines *ce_text_lines_new(void);
CE_API void ce_text_lines_free(ce_text_lines *lines);
CE_API void ce_text_lines_parse(ce_text_lines *lines, const char *text, uint64_t len);
CE_API int64_t ce_text_lines_count(const ce_text_lines *lines);
/* Borrowed; invalidated by parse/add/free. NULL when out of range. */
CE_API const char *ce_text_lines_at(const ce_text_lines *lines, int64_t index);
CE_API void ce_text_lines_add(ce_text_lines *lines, const char *line);
/* Invalidated by any other call on the same object. */
CE_API const char *ce_text_lines_serialize(ce_text_lines *lines, int32_t crlf, uint64_t *len_out);

/* ---- subtitle lines ----
 *
 * One Subtitles.txt line: "subtitle FRAME X Y DURATION RRGGBBAA message...".
 * The leading word is ignored on parse (it always was), the message is the
 * space-joined tail with surrounding whitespace trimmed, and a line that does
 * not parse is reported, not fatal - the old loader skipped it silently.
 */

typedef struct ce_subtitle_fields
{
	int32_t frame;
	int32_t x;
	int32_t y;
	int32_t duration;
	uint32_t color;
} ce_subtitle_fields;

/* Returns the message's byte length on success (message copied into
 * message_buf, NUL-terminated, when cap allows; a message can never be longer
 * than its line), or -1 when the line does not parse. */
CE_API int64_t ce_subtitle_parse_line(
	const char *line, ce_subtitle_fields *fields, char *message_buf, uint64_t cap);

/* Renders the line (no EOL). Returns its byte length; writes NUL-terminated
 * into buf when cap allows, so call with cap 0 to size first if needed. */
CE_API int64_t ce_subtitle_format_line(
	const ce_subtitle_fields *fields, const char *message, char *buf, uint64_t cap);

/* ---- the savestate/movie container ----
 *
 * Savestates and movies share one file shape: a zip of named lumps, each
 * optionally zstd-compressed (marked by a ".zst" suffix and stored, not
 * deflated), with a "ChimeraState 1.0" version lump. The engine owns the FORMAT;
 * file I/O stays with the caller - a writer produces the finished archive as
 * one buffer, a reader takes one.
 *
 * Lumps are addressed by base name, meaning the entry's
 * path up to its first '.' ("Input Log.txt" is "Input Log" - and the version
 * lump "ChimeraState 1.0" is "ChimeraState 1"). zstd needs libzstd beside this
 * library; it is loaded on first use and its absence is a put/get error.
 */

typedef struct ce_state_writer ce_state_writer;

/* compression_level is the config's 0-9: 0 stores, otherwise deflate for
 * plain lumps and zstd level 2n+1 for zstd lumps. emu_version fills the ChimeraVersion lump; both version lumps are written
 * here. */
CE_API ce_state_writer *ce_state_writer_new(int32_t compression_level, const char *emu_version);
CE_API void ce_state_writer_free(ce_state_writer *w);

/* ext may be NULL for extensionless lumps ("GreenZone"). Returns 0 on
 * success; on failure see _last_error, and the writer is poisoned - finish
 * will fail too, matching the old writer's collect-errors-until-close. */
CE_API int32_t ce_state_writer_put_lump(
	ce_state_writer *w, const char *name, const char *ext, int32_t zstd,
	const uint8_t *data, uint64_t len);

/* The finished archive. NULL on failure (see _last_error).
 * Valid until the writer is freed. */
CE_API const uint8_t *ce_state_writer_finish(ce_state_writer *w, uint64_t *len_out);

/* "" when no error. Invalidated by the next call on the same writer. */
CE_API const char *ce_state_writer_last_error(ce_state_writer *w);

typedef struct ce_state_reader ce_state_reader;

/* Returns NULL when data is not a readable container. That is two different
 * stories, told through error_out: NULL for the quiet cases the old loader
 * returned null for (not a zip; a non-movie with no version lump), a message
 * for real corruption (duplicate lump names). error_out may be NULL if the
 * caller does not care; the message is a static buffer, valid until the next
 * failed open. is_movie relaxes the missing-version-lump case to version
 * 1.0.0, as movie loading always has. */
CE_API ce_state_reader *ce_state_reader_open(
	const uint8_t *data, uint64_t len, int32_t is_movie, const char **error_out);
CE_API void ce_state_reader_free(ce_state_reader *r);

/* The sub version from the "ChimeraState 1.0" lump (1.0.N). */
CE_API int32_t ce_state_reader_version(const ce_state_reader *r);

/* The lump's bytes, zstd-decompressed when it was stored compressed - length
 * is the DECOMPRESSED length. NULL when absent or undecompressable (see
 * _last_error to tell which; absent is ""). ext participates only in the
 * version-1.0.2 quirk, where compression was inferred from the extension
 * instead of marked. Invalidated by the next lump read on the same reader. */
CE_API const uint8_t *ce_state_reader_lump(
	ce_state_reader *r, const char *name, const char *ext, uint64_t *len_out);

/* "" when the last lump miss was a plain absence. Invalidated by the next
 * call on the same reader. */
CE_API const char *ce_state_reader_last_error(ce_state_reader *r);

/* ---- progress ----
 *
 * How far along a slow call is, for a frontend that shows a bar. The engine
 * reports as it works: hashing a file (bytes of its length), compressing or
 * reading a state lump (bytes), a boot's stages (no total), the compiled
 * objects a boot fetches (a count, no total). The callback runs on the thread
 * doing the work - the caller's own - so a frontend may pump its message loop
 * from it; it must not call back into the engine. Pass a null fn to stop. */
typedef void (*ce_progress_fn)(const char *stage, uint64_t done, uint64_t total, void *user);
CE_API void ce_progress_set(ce_progress_fn fn, void *user);

/* ---- identity hashing ----
 *
 * The hash the frontend identifies files by: roms, firmware.
 * Writes 40 uppercase hex characters plus a NUL into out41. */
CE_API void ce_sha1_hex(const uint8_t *data, uint64_t len, char *out41);

/* The same hash of a file, taken by reading it rather than by holding it: a
 * firmware may be a disk image, and one that does not fit in memory still has
 * an identity. Writes 40 uppercase hex characters plus a NUL into out41, and
 * the length hashed into lenOut when it is given. Answers repeat calls from a
 * cache keyed by the path and the file's whole identity (size, modified and
 * changed times, volume, file index), so hashing the same file twice costs one
 * read. Returns 1 when the file was read, 0 when it was not. */
CE_API int ce_sha1_file(const char *utf8Path, char *out41, uint64_t *lenOut);
/* Keeps that cache between sessions in the file named (UTF-8; NULL keeps it
 * for this process only, as before the first call): the hashes the file holds
 * are read back now, and every hash taken from now on of a file whose
 * identity the system gives in full is added to it. A file edited, replaced
 * or copied over has another identity and is hashed again. Returns how many
 * hashes were read back. */
CE_API int32_t ce_sha1_keep_in(const char *utf8Path);

/* ---- reproducible media ----
 *
 * A folder turned into ONE file whose bytes depend on the folder's contents and
 * names and on nothing else - not the clock, not the modification times the
 * files carry, not the mode bits a copy left behind, not the order a directory
 * listing came back in. Two people packing the same dump get the same SHA1.
 *
 * That is the whole point of it: a project stores names and SHA1s and never
 * paths (docs/project.md), so a game that was dumped as a FOLDER can only
 * become a project by becoming a file first, and it has to become the SAME file
 * for everyone or a movie made on it cannot be verified by anybody else. */
enum
{
	CE_MEDIA_ZIP_STORED = 0, /* one file, no compression: read by seeking */
	CE_MEDIA_ISO9660 = 1,    /* ISO 9660 + Joliet, the disc the cores read */
	CE_MEDIA_FAT12 = 2,      /* a 1.44 MB floppy image */
};

/* Called as the pack runs, often enough to drive a bar and name the file being
 * written. Return 1 to carry on, 0 to cancel - a cancelled pack deletes what it
 * had written, because a half-written image that still looks like an image is
 * worse than no image. */
typedef int32_t (*ce_media_progress_fn)(const char *file, uint64_t bytes_done,
	uint64_t bytes_total, uint64_t files_done, uint64_t files_total, void *user);

/* Returns 1 when the file was written. On 0, ce_media_last_error says why (and
 * says "cancelled" when that is why). One pack at a time: the last hash and the
 * last error are the caller's until the next call. */
CE_API int32_t ce_media_make(const char *folder, const char *out_path, int32_t format,
	ce_media_progress_fn progress, void *user);

/* A RECIPE is what a core says its media needs beyond the files: one object of
 * the "media" array in its package's waterbox.config, handed over as the JSON
 * the package wrote.
 *
 *   { "id": "disc", "label": "...", "format": "iso9660",
 *     "when": { "rootFile": "NAME.EXT" },
 *     "systemArea": [ { "at": 0, "u32be": 1 }, { "at": 12, "u32be": "lastSector" } ] }
 *
 * "when" says which folders it is for (a file at the root, ASCII case ignored;
 * absent: any). "systemArea" writes 32-bit values into the sixteen sectors an
 * ISO 9660 image reserves: a number, or "lastSector" / "sectors", which only
 * the writer knows. The packer itself knows no machine: an image made without
 * a recipe is the files and nothing else.
 *
 * ce_media_recipe_applies: 1 when `folder` is what the recipe is for, 0 when
 * it is not, -1 when the recipe cannot be read (ce_media_last_error).
 * ce_media_recipe_format: the CE_MEDIA_* it writes, or -1.
 * ce_media_make_with: ce_media_make, in the recipe's format and with what it
 * asks for. It does not ask whether the recipe applies: that was the caller's
 * question, and a person may know better. */
CE_API int32_t ce_media_recipe_applies(const char *recipe_json, const char *folder);
CE_API int32_t ce_media_recipe_format(const char *recipe_json);
CE_API int32_t ce_media_make_with(const char *folder, const char *out_path, const char *recipe_json,
	ce_media_progress_fn progress, void *user);

/* The SHA1 of what the last successful ce_media_make wrote - what a project
 * would record - or "" if it did not finish. */
CE_API const char *ce_media_last_sha1(void);
CE_API const char *ce_media_last_error(void);

/* ---- firmware ----
 *
 * Whether a provided file is the firmware a core asked for. declared_size 0
 * means the core pinned no size; expected_sha1s is the core's accepted hash
 * list, one per '\n'-separated line, empty for "anything". Returns 0 (wrong
 * size, refused), 1 (unrecognised hash, used anyway) or 2 (a pinned match). */
CE_API int32_t ce_firmware_state(
	int64_t declared_size, const char *expected_sha1s,
	int64_t actual_size, const char *actual_sha1);

/* The canonical firmware line a movie records: "id=SHA1" pairs, one per
 * '\n'-separated line in, sorted by id and space-joined out. Thread-local
 * buffer, invalidated by the next call. */
CE_API const char *ce_firmware_record_line(const char *pairs, uint64_t *len_out);

/* The firmware decision tree (docs/project.md): which of a core's declared
 * firmware files the project's decisions call for. decl_json is the
 * package's "firmware" array, where an entry may carry "requiredWhen" - a
 * condition over the slot map and the EFFECTIVE settings:
 *   {"slot": id}                    the slot holds at least one file
 *   {"slot": id, "extension": e}    ...a file with that extension
 *   {"setting": name, "is": v}      the setting has that value
 *   {"setting": name, "in": [v..]}  ...one of those values
 *   {"all": [c..]} {"any": [c..]} {"not": c}
 * Every applying entry is REQUIRED - the decisions nail each requirement
 * to one exact file or to nothing; variants of one id are separate
 * entries with disjoint conditions (a sync setting picks between them),
 * and optional firmware does not exist. An entry without a condition
 * always applies. Malformed conditions evaluate false. Returns a JSON
 * array [{"id":..,"index":n}] in declaration order, n being the
 * position in decl_json (thread-local; invalidated by the next call). */
CE_API const char *ce_firmware_evaluate(
	const char *decl_json, uint64_t decl_len,
	const char *slots_json, uint64_t slots_len,
	const char *settings_json, uint64_t settings_len,
	uint64_t *len_out);

/* The same decision tree, gating SYNC SETTINGS (docs/project.md): the game
 * files chosen decide which settings are exposed at all (a Game Gear cart
 * exposes its sound chip, a Genesis cart its own), and settings may gate
 * further settings. decl_json is the package's "settings" array; an entry
 * may carry "exposedWhen" with the same condition language. An entry
 * without a condition is always exposed; variants of one name are separate
 * entries with disjoint conditions. Returns [{"name":..,"index":n}] in
 * declaration order (thread-local; invalidated by the next call). */
CE_API const char *ce_settings_evaluate(
	const char *decl_json, uint64_t decl_len,
	const char *slots_json, uint64_t slots_len,
	const char *settings_json, uint64_t settings_len,
	uint64_t *len_out);

/* And gating the SLOTS themselves, within the file form (docs/project.md):
 * a slot in file_slots.json may carry "exposedWhen" over the CURRENT slot
 * map, so filling one slot can make another unavailable (a Famicom disk
 * rules out a cartridge and vice versa) until it is unloaded. decl_json is
 * the whole file_slots.json. Returns the exposed slot ids as a JSON array
 * of strings, declaration order (thread-local; invalidated by the next
 * call). ce_project_validate enforces the same rule: files in an unexposed
 * slot are structurally invalid. */
CE_API const char *ce_slots_evaluate(
	const char *decl_json, uint64_t decl_len,
	const char *slots_json, uint64_t slots_len,
	const char *settings_json, uint64_t settings_len,
	uint64_t *len_out);

/* The files a cue sheet references (its FILE lines, quoted or bare), as a
 * JSON array of names in cue order - what ce_project_file_add will pull in
 * beside the cue as "support" files. Lets a form show "disc.cue (+ N
 * tracks)" and complain about a missing track at pick time rather than at
 * create time. Returns thread-local storage, invalidated by the next
 * call; not-a-cue or empty input returns []. */
CE_API const char *ce_cue_references(
	const char *cue_bytes, uint64_t cue_len,
	uint64_t *len_out);

/* ---- multi-file games ----
 *
 * A .chimeraMultiFile descriptor names the files a game is made of: bare
 * names (every file lives in the descriptor's own folder), each with the
 * SHA1 recorded at creation, in an order that matters (images load as rom,
 * rom2..romN - which IS the disc/floppy swap order). Roles: "image" (an
 * ordered, mountable, swappable item), "support" (present and hashed, but
 * only referenced by another file - a cue's bin), "savedata" (at most one:
 * a previously exported save, mounted under the fixed name "savedata" for
 * the core to consume). The engine owns the format: parsing, the rules,
 * hashing, the canonical movie line.
 *
 * ce_multifile_open enforces the STRUCTURAL rules (valid JSON, bare unique
 * names, known roles, at least one image, at most one savedata, and cue
 * closure: every file a listed cue references must itself be listed) and
 * loads + hashes the files. A missing or hash-mismatched file does NOT fail
 * the open: per-file status lets the caller refuse or knowingly proceed -
 * the movie line always records what was ACTUALLY loaded. */
typedef struct ce_multifile ce_multifile;

/* NULL with *error_out (static per-thread) on structural failure. */
CE_API ce_multifile *ce_multifile_open(const char *descriptor_path, const char **error_out);
CE_API void ce_multifile_free(ce_multifile *m);

CE_API int32_t ce_multifile_count(const ce_multifile *m);
CE_API const char *ce_multifile_name(const ce_multifile *m, int32_t index);
CE_API const char *ce_multifile_role(const ce_multifile *m, int32_t index);
/* the descriptor's recorded hash (40 uppercase hex) */
CE_API const char *ce_multifile_sha1(const ce_multifile *m, int32_t index);
/* the hash of the bytes actually on disk; "" while the file is missing */
CE_API const char *ce_multifile_actual_sha1(const ce_multifile *m, int32_t index);
/* 0 = ok, 1 = missing, 2 = hash mismatch */
CE_API int32_t ce_multifile_status(const ce_multifile *m, int32_t index);
/* 1 when every file is present with a matching hash */
CE_API int32_t ce_multifile_ok(const ce_multifile *m);
/* the loaded bytes (NULL while missing); borrowed, live as long as m */
CE_API const uint8_t *ce_multifile_data(const ce_multifile *m, int32_t index, uint64_t *len_out);

CE_API int32_t ce_multifile_image_count(const ce_multifile *m);
/* entry index of the nth image (0-based), -1 out of range */
CE_API int32_t ce_multifile_image_index(const ce_multifile *m, int32_t nth);
/* entry index of the savedata file, -1 when the descriptor has none */
CE_API int32_t ce_multifile_savedata_index(const ce_multifile *m);

/* The canonical movie line ("GameFiles" in a movie header): the entries in
 * descriptor order as name=SHA1 (images) or name=SHA1:role (the rest),
 * space-joined, with '%', '=', ':' and bytes outside 0x21..0x7E percent-
 * encoded in names. The hashes are the ACTUAL ones - a movie records what
 * was loaded, not what the descriptor claimed. NULL while any file is
 * missing. Borrowed; invalidated by ce_multifile_free. */
CE_API const char *ce_multifile_record_line(const ce_multifile *m, uint64_t *len_out);

/* Creation: names (bare, in order) + parallel roles; the files must already
 * sit in the descriptor's folder. Hashes them, enforces every structural
 * rule (including cue closure), writes the descriptor JSON. 0 on success,
 * nonzero with *error_out (static per-thread) otherwise. */
CE_API int32_t ce_multifile_save(
	const char *descriptor_path,
	const char *const *names, const char *const *roles, int32_t count,
	const char **error_out);

/* ---- the project ----
 *
 * A .chimeraProject is chimera's entry point and its movie in one file: a
 * JSON document holding everything required to reproduce the work except
 * the data bytes, which are named by SHA1 (docs/project.md). Identity
 * (title, description), the pinned core (name, version, package SHA1), the
 * file manifest (bare canonical names + SHA1 + the core-defined slot each
 * file fills, order within a slot = swap order), firmware pins, the sync
 * settings, and the TAS work itself: input log, markers, branches.
 *
 * No paths are ever stored. Files are resolved per session - the caller
 * says where each one is (ce_project_file_resolve / ce_project_resolve_dir)
 * and the engine hashes what it finds; a file whose on-disk NAME differs is
 * fine, the canonical name is the label and the hash is the identity.
 * Saving records the ACTUAL hash of every resolved file, so a knowing
 * override leaves a truthful record.
 *
 * ce_project_open enforces the structural rules (valid JSON, known keys,
 * bare unique names, well-formed hashes and slot ids) and leaves every file
 * unresolved. ce_project_validate checks the manifest against a core's
 * file_slots.json declaration: known slots, cardinality, formats, and the
 * declaration's atLeastOneOf groups. */
typedef struct ce_project ce_project;

/* a fresh, empty project (for the creation wizard) */
CE_API ce_project *ce_project_new(void);
/* NULL with *error_out (static per-thread) on structural failure */
CE_API ce_project *ce_project_open(const char *path, const char **error_out);
/* 0 on success; resolved files are written with their ACTUAL hash */
CE_API int32_t ce_project_save(ce_project *p, const char *path, const char **error_out);
/* The same save under a name of its own: the project becomes another project,
 * with an id of its own written into the file. Two files with one id share
 * one cache folder, and the one reopened would take the other's state
 * history, made for other input (owner, 2026-10-29). A save that fails leaves
 * the project the id it had. */
CE_API int32_t ce_project_save_as(ce_project *p, const char *path, const char **error_out);
CE_API void ce_project_free(ce_project *p);

/* This project's identity, and no other's: minted when it is created, carried
 * in the file, and stable across the file being renamed, moved or synced to
 * another machine. The frontend keeps everything REGENERABLE that belongs to a
 * project - the state history, and where this machine last found the project's
 * files - in a per-user cache keyed by this, so the .chimeraProject stays the
 * one file that has to travel (docs/state-manager.md). A project written
 * before ids existed is given one when it is opened, and keeps it from its
 * next save. Borrowed; valid until the project is freed. */
CE_API const char *ce_project_id(const ce_project *p);
CE_API const char *ce_project_title(const ce_project *p);
CE_API void ce_project_set_title(ce_project *p, const char *title);
CE_API const char *ce_project_description(const ce_project *p);
CE_API void ce_project_set_description(ce_project *p, const char *description);

/* the pinned core: package name, version, package zip SHA1 */
CE_API const char *ce_project_core_name(const ce_project *p);
CE_API const char *ce_project_core_version(const ce_project *p);
CE_API const char *ce_project_core_sha1(const ce_project *p);
CE_API void ce_project_set_core(ce_project *p, const char *name, const char *version, const char *sha1);

CE_API uint64_t ce_project_rerecords(const ce_project *p);
CE_API void ce_project_set_rerecords(ce_project *p, uint64_t count);

/* The second (since 1970, UTC) the machine's clock starts at (owner,
 * 2026-10-24, issue 366). Every run of a project starts the clock at the same
 * second, so a movie replays; a game that takes its luck from the date deals
 * the same each time. A project may choose another second to give the game
 * other luck. -1 when the project names none, and the machine starts at the
 * sandbox's own, CE_CLOCK_START_DEFAULT; the file says "clockStart" only for
 * another start, so a project at the default opens in an older Chimera.
 * Setting -1 or the default clears it; 1 for a start later than 2200-01-01. */
#define CE_CLOCK_START_DEFAULT 1495889068
CE_API int64_t ce_project_clock_start(const ce_project *p);
CE_API int32_t ce_project_set_clock_start(ce_project *p, int64_t unix_seconds);

/* The sync settings, as the JSON object the session's settings channel
 * takes. Borrowed; invalidated by the next settings call on p. The setter
 * rejects anything that does not parse as a JSON object. */
CE_API const char *ce_project_settings_text(ce_project *p, uint64_t *len_out);
CE_API int32_t ce_project_set_settings_text(ce_project *p, const char *json, const char **error_out);

/* The firmware pins, as a JSON array carried verbatim (the firmware
 * channel's record: id + SHA1 entries). Borrowed as above; the setter
 * rejects anything that does not parse as a JSON array. */
/* What the core compiled for this game: a JSON array of {name, sha1}, one per
 * cached object (docs/compile-cache.md). Regenerable from the same rom and
 * package, recorded so a later open can check it is the same compiled code. */
CE_API const char *ce_project_core_cache_text(ce_project *p, uint64_t *len_out);
CE_API int32_t ce_project_set_core_cache_text(ce_project *p, const char *json, const char **error_out);
/* TAStudio's layout of the piano roll (columns, order, widths, orientation, lag
 * display) as a JSON object the frontend wrote and reads back; the engine only
 * keeps it. "" when the project has none. The setter clears it on "" or null,
 * and otherwise rejects anything that is not a JSON object. Not a sync setting:
 * it travels with the project because it is part of the work somebody set up
 * (issue #83). */
CE_API const char *ce_project_tastudio_text(ce_project *p, uint64_t *len_out);
CE_API int32_t ce_project_set_tastudio_text(ce_project *p, const char *json, const char **error_out);
CE_API const char *ce_project_firmware_text(ce_project *p, uint64_t *len_out);
CE_API int32_t ce_project_set_firmware_text(ce_project *p, const char *json, const char **error_out);

/* Which machine a project makes, as the SHA-1 (40 hex digits) of the core
 * build that runs it, its settings and firmware (keys in order, so the same
 * settings written in another order are the same machine) and each file's
 * slot and hash (its name is not its identity). A state is the memory of one
 * exact machine; what a frontend keeps beside a project - branch states, the
 * greenzone - is checked against this before it reaches the sandbox, which
 * checks only the core binary (issue #63). running_core_sha1 is the package
 * that will run, which can differ from the one the project pins; NULL or ""
 * takes the pin. */
CE_API void ce_project_machine_id(ce_project *p, const char *running_core_sha1, char *out41);

/* The input log lump, exactly what ce_movie_log_parse/_serialize exchange
 * (LogKey line + entries). "" for a project with no frames yet. */
CE_API const char *ce_project_log_text(const ce_project *p, uint64_t *len_out);
CE_API void ce_project_set_log_text(ce_project *p, const char *text, uint64_t len);

/* Markers, kept sorted by frame. keep_state is the user's keep-a-state-here
 * choice (default true; serialized only when false). Add returns the index
 * the marker landed at. */
CE_API int32_t ce_project_marker_count(const ce_project *p);
CE_API int64_t ce_project_marker_frame(const ce_project *p, int32_t index);
CE_API const char *ce_project_marker_text(const ce_project *p, int32_t index);
CE_API int32_t ce_project_marker_keep_state(const ce_project *p, int32_t index);
CE_API int32_t ce_project_marker_add(ce_project *p, int64_t frame, const char *text, int32_t keep_state);
CE_API void ce_project_marker_remove(ce_project *p, int32_t index);

/* Branches: a named alternative input log at a frame, in creation order,
 * with an optional timestamp text (carried verbatim) and its own markers -
 * a branch is work, so everything about it except the regenerable state
 * lives in the project. */
CE_API int32_t ce_project_branch_count(const ce_project *p);
CE_API const char *ce_project_branch_name(const ce_project *p, int32_t index);
CE_API int64_t ce_project_branch_frame(const ce_project *p, int32_t index);
CE_API const char *ce_project_branch_time(const ce_project *p, int32_t index);
CE_API const char *ce_project_branch_log_text(const ce_project *p, int32_t index, uint64_t *len_out);
CE_API void ce_project_branch_add(ce_project *p, const char *name, int64_t frame, const char *time, const char *log_text, uint64_t len);
CE_API void ce_project_branch_remove(ce_project *p, int32_t index);
CE_API int32_t ce_project_branch_marker_count(const ce_project *p, int32_t branch);
CE_API int64_t ce_project_branch_marker_frame(const ce_project *p, int32_t branch, int32_t index);
CE_API const char *ce_project_branch_marker_text(const ce_project *p, int32_t branch, int32_t index);
CE_API int32_t ce_project_branch_marker_keep_state(const ce_project *p, int32_t branch, int32_t index);
CE_API void ce_project_branch_marker_add(ce_project *p, int32_t branch, int64_t frame, const char *text, int32_t keep_state);

/* The headers map: movie metadata the format does not first-class (Author,
 * emulator version, platform facts) as ordered verbatim key/value pairs.
 * set with a NULL value removes; a new key appends. */
CE_API int32_t ce_project_header_count(const ce_project *p);
CE_API const char *ce_project_header_key_at(const ce_project *p, int32_t index);
CE_API const char *ce_project_header_value_at(const ce_project *p, int32_t index);
CE_API const char *ce_project_header_get(const ce_project *p, const char *key);
CE_API void ce_project_header_set(ce_project *p, const char *key, const char *value);

/* subtitles: verbatim display lines, in order */
CE_API int32_t ce_project_subtitle_count(const ce_project *p);
CE_API const char *ce_project_subtitle_at(const ce_project *p, int32_t index);
CE_API void ce_project_subtitle_add(ce_project *p, const char *line);
CE_API void ce_project_subtitle_remove(ce_project *p, int32_t index);

/* Adds a file: canonical (bare) name, the slot it fills, and where its
 * bytes are RIGHT NOW - read and hashed immediately, so the entry is born
 * resolved. A .cue's referenced files are auto-added from the cue's own
 * folder with the reserved slot "support" (closure at creation). 0 on
 * success; nonzero with *error_out (static per-thread) otherwise. */
CE_API int32_t ce_project_file_add(ce_project *p, const char *name, const char *slot, const char *source_path, const char **error_out);
CE_API void ce_project_file_remove(ce_project *p, int32_t index);

CE_API int32_t ce_project_file_count(const ce_project *p);
CE_API const char *ce_project_file_name(const ce_project *p, int32_t index);
CE_API const char *ce_project_file_slot(const ce_project *p, int32_t index);
/* the recorded hash (40 uppercase hex) */
CE_API const char *ce_project_file_sha1(const ce_project *p, int32_t index);
/* the hash of the resolved bytes; "" while unresolved */
CE_API const char *ce_project_file_actual_sha1(const ce_project *p, int32_t index);
/* 0 = resolved and matching, 1 = unresolved, 2 = resolved but mismatched */
CE_API int32_t ce_project_file_status(const ce_project *p, int32_t index);
/* Where this file's bytes were read from in THIS run, or "" if not yet read.
 * In memory only - NEVER written to the project, which is distributable and
 * has no business carrying one machine's paths. A frontend that wants to
 * remember locations keeps its own sidecar and reads them from here. */
CE_API const char *ce_project_file_source_path(const ce_project *p, int32_t index);
/* How many bytes the resolved file holds, or 0 while unresolved.
 *
 * There is deliberately no call for the BYTES. A project's files stay where
 * they are and are read when the machine asks for them (by path, see
 * ce_session_open_project): a PS2 disc is over four gigabytes, and every copy of one
 * between the disk and the guest is four gigabytes nothing needed. What a
 * project knows about a file is its name, its hash, its size and its path. */
CE_API uint64_t ce_project_file_size(const ce_project *p, int32_t index);

/* Resolves one file from a caller-provided location (the per-session
 * resolution dialog): reads, hashes, sets status 0 or 2. The on-disk name
 * may differ from the canonical one. 0 on success (even a mismatch - that
 * is a status, not an error); nonzero with *error_out when unreadable. */
CE_API int32_t ce_project_file_resolve(ce_project *p, int32_t index, const char *path, const char **error_out);

/* Puts a file back to unresolved, dropping the bytes, the actual hash and the
 * source path. For a caller that resolved speculatively (a remembered location
 * that turned out to hold something else) and wants the file asked about
 * rather than mounted. */
CE_API void ce_project_file_unresolve(ce_project *p, int32_t index);
/* Tries to resolve every unresolved file by its canonical name inside dir
 * (the "files beside the project" convenience). Returns how many resolved. */
CE_API int32_t ce_project_resolve_dir(ce_project *p, const char *dir);
/* 1 when every file is resolved with a matching hash */
CE_API int32_t ce_project_files_ok(const ce_project *p);
/* Which file is the game: the first that is not a support file, or -1 for a
 * project that holds none (a machine that starts without a medium). It is the
 * file ce_session_open_project gives the core as its rom, and the one a
 * frontend names the project after. */
CE_API int32_t ce_project_game_file(const ce_project *p);

/* The manifest checked against a core's file_slots.json declaration (slot
 * ids, cardinality, formats, atLeastOneOf groups; "support" is exempt).
 * 0 when it conforms; nonzero with *error_out otherwise. */
CE_API int32_t ce_project_validate(const ce_project *p, const char *slots_json, uint64_t slots_len, const char **error_out);

/* The slot map the session mounts as "slots": a JSON object of slot id ->
 * canonical names in manifest order, "support" excluded. Borrowed;
 * invalidated by the next call on p. */
CE_API const char *ce_project_slots_text(ce_project *p, uint64_t *len_out);

/* ---- core packages ----
 *
 * A core package is a zip (or, for development, a directory) whose root holds
 * core.wbx + waterbox.config - the data-driven form the generic adapter runs -
 * or a chimera-core.json manifest. The engine owns the container: what makes
 * a path a package, the package's identity (the SHA1 of its zip), and access
 * to its entries. What the entries MEAN stays with the caller for now; the
 * waterbox.config schema moves into the engine with the session.
 *
 * This is the engine's first filesystem read - packages are read-only inputs,
 * and the coming native session has to open them without a frontend anyway.
 */

typedef struct ce_package ce_package;

/* Returns NULL two ways, told apart by error_out: NULL error for a path that
 * is simply not a core package (an unrelated zip or directory, a missing
 * path), a message (static per-thread, valid until the thread's next failed
 * open) for something that looks like a package but cannot be read. The zip
 * is hashed only after it is known to be a package. */
CE_API ce_package *ce_package_open(const char *path, const char **error_out);
CE_API void ce_package_free(ce_package *p);

/* The package's identity: SHA1 of the zip file, uppercase hex. NULL for the
 * directory form, which has no file to hash. */
CE_API const char *ce_package_sha1(const ce_package *p);

/* Nonzero for the data-driven waterbox form (core.wbx + waterbox.config). */
CE_API int32_t ce_package_is_waterbox(const ce_package *p);

CE_API int32_t ce_package_has_entry(ce_package *p, const char *name);

/* Core-owned asset files: every entry under "assets/", sorted. A waterbox
 * session mounts each one at its path with the "assets" prefix dropped, so
 * a package carrying assets/sys/GC/font.bin gives its guest /sys/GC/font.bin.
 * The names stay valid until the package is freed. */
CE_API int32_t ce_package_asset_count(ce_package *p);
CE_API const char *ce_package_asset_name(ce_package *p, int32_t i);

/* The entry's bytes (decompressed from the zip, or the file in the directory
 * form). NULL when absent or unreadable (see _last_error to tell which;
 * absent is ""). Invalidated by the next entry read on the same package. */
CE_API const uint8_t *ce_package_entry(ce_package *p, const char *name, uint64_t *len_out);

/* "" when the last entry miss was a plain absence.
 * Invalidated by the next call on the same package. */
CE_API const char *ce_package_last_error(ce_package *p);

/* A declaration file (waterbox.config, file_slots.json) with its texts in
 * the language in use: every string in it that is a key of the package's
 * table for that language (languages/<code>.json) is that key's text. A
 * package with no table for the language: the file as written. Invalidated
 * as ce_package_entry is.
 * _language_problems: what keeps the package's languages from being
 * Chimera's, a line each - a table missing, a key missing or not English's,
 * a text whose placeholders differ, a field a person reads that holds no key
 * (docs/translations.md). "" when nothing does; per-thread, until the next
 * call on the thread. */
CE_API const uint8_t *ce_package_entry_texts(ce_package *p, const char *name, uint64_t *len_out);
CE_API const char *ce_package_language_problems(ce_package *p);

/* ---- the session ----
 *
 * A running waterboxed machine: a core package's guest, loaded through the
 * miniBox host (libminiboxhost, found beside this library), configured by its
 * own waterbox.config, fed a rom and settings, and driven a frame at a time:
 * one machine, one behaviour, with or without a frontend.
 *
 * The engine composes the effective settings itself: every declared setting
 * at its config default, overlaid with the caller's overrides JSON (the
 * user's or the movie's sync settings). Buttons map to input bits by their
 * position in the config's buttons array.
 */

typedef struct ce_session ce_session;

/* Opens the machine a project describes. Every machine is a project's: a
 * game runs only in a project (user-decided, 2026-10-26), and there is no
 * call that opens one from a game file alone. This is the one place that
 * knows how a project becomes a running machine; chimera-run, the frontend
 * and anything else that opens a project call it, so the rules below are
 * written once.
 *
 * The caller has already resolved the project's files (ce_project_resolve_dir,
 * ce_project_file_resolve): finding them is its business, since only it knows
 * which folders to look in or whom to ask. This call then:
 *
 *   - refuses a project with a file still unresolved, and one whose file does
 *     not match its recorded hash unless CE_OPEN_ALLOW_FILE_MISMATCH is given;
 *   - refuses a package whose SHA1 is not the one the project names, unless
 *     CE_OPEN_ALLOW_CORE_MISMATCH is given (a directory package has no SHA1
 *     and is not compared);
 *   - checks the project's files against the package's file_slots.json, when
 *     the package has one (ce_project_validate), with a slot gated on a
 *     setting judged by the settings the machine starts with: the package's
 *     default for a setting neither the caller nor the project gives;
 *   - gives the core its files: the slot map as "slots", every file under its
 *     own name, the first file that is not a support file as the rom with its
 *     name as "rom.name", and the later files of that slot as rom2..N. Every
 *     file is given by its path and none is read here. A project with no game
 *     file opens the machine with no rom;
 *   - starts the machine with settings_overrides_json, or with the project's
 *     own settings when that is NULL.
 *
 * The two flags are for a caller with a person in front of it who was shown
 * the difference and chose to go on. Firmware is the caller's to find and
 * arrives as bytes, ids and blobs in parallel arrays (it is small, and a
 * frontend finds it in its own store, not where the project is). Every file
 * is given to the guest by its path and read as the guest reads it, never
 * copied into memory: a PS2 disc image is over four gigabytes. The bytes are
 * read during the call and not kept. Two threads may open sessions at once;
 * what ce_precompile_request asks applies to the thread that asked.
 * ce_gl_request and ce_cache_dir are the whole process's (one GPU context
 * per process), so sessions that differ in them are opened one at a time.
 *
 * The project is only read, and may be freed once this returns. NULL with
 * *error_out set (per thread, valid until the thread's next failed open) on
 * any failure - an unreadable package, a missing export, a core that refused
 * its files (its GetLoadError text is the message when it gives one). The
 * project's input is not loaded: that is ce_session_movie_load, as before. */
#define CE_OPEN_ALLOW_CORE_MISMATCH 1u
#define CE_OPEN_ALLOW_FILE_MISMATCH 2u
CE_API ce_session *ce_session_open_project(
	const char *package_path, ce_project *project,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint32_t flags, const char **error_out);

CE_API void ce_session_free(ce_session *s);

/* The core log, kept only when a frontend asks for it. With a path (UTF-8),
 * the file is started anew with a line saying when and by which Chimera, and
 * from then on everything any core writes to its stdout and stderr is
 * appended to it, flushed as it is written, so a crash leaves it whole. A
 * session opened while it is on also notes its core package in it, and is
 * mounted an empty file named "corelog": a core that keeps a fuller log only
 * on request (RPCS3's) reads that as the request. It is checked for, never
 * read, so the machine is the same with the log on or off. NULL or "" stops
 * it; the file stays. 1 on success; 0 with ce_core_log_error() saying why.
 * Process-wide, and never remembered: each run of a frontend starts off. */
CE_API int32_t ce_core_log(const char *path);
/* The file the core log goes to, or "" while it is off. */
CE_API const char *ce_core_log_path(void);
CE_API const char *ce_core_log_error(void);

/* config-derived facts (borrowed strings live as long as the session) */
CE_API const char *ce_session_core_name(const ce_session *s);
CE_API const char *ce_session_system_id(const ce_session *s);
/* Which entry of the package's "machines" this session is, as its machine
 * setting chose it (several machines may share a system id: MAME's Amstrad
 * CPC464 and GX4000), or -1 for a package that is one machine. */
CE_API int32_t ce_session_machine_index(const ce_session *s);
CE_API int32_t ce_session_width(const ce_session *s);
CE_API int32_t ce_session_height(const ce_session *s);
/* The shape the picture is shown at, as a size: the package's or its machine's
 * declared virtual size, else the picture's own size as it is now (square
 * pixels; the declared width and height are only the largest picture). */
CE_API int32_t ce_session_virtual_width(const ce_session *s);
CE_API int32_t ce_session_virtual_height(const ce_session *s);
/* The display aspect the machine reports for what it shows NOW, x:y, from the
 * core's optional GetDisplayAspectX/GetDisplayAspectY exports (both or
 * neither). 1 with the aspect written; 0 when the core does not say, and the
 * declared virtual size stands. An arcade core answers 3:4 for a game whose
 * monitor stood on its side, which no per-machine declaration can know. */
CE_API int32_t ce_session_display_aspect(const ce_session *s, int32_t *x_out, int32_t *y_out);
/* The rate of frames NOW: the guest's own answer when it gives one (asked
 * after Init and again after every frame advanced, drawn or not, since a
 * machine changes its refresh with its video mode and a game core's step is as
 * long as the game makes it), else the config's. The rate read after a frame
 * is that frame's length. */
CE_API int32_t ce_session_vsync_numerator(const ce_session *s);
CE_API int32_t ce_session_vsync_denominator(const ce_session *s);
CE_API int32_t ce_session_samples_per_frame(const ce_session *s);
CE_API int32_t ce_session_channels(const ce_session *s);
CE_API int32_t ce_session_deterministic(const ce_session *s);

/* Whether a GPU OUTSIDE the sandbox drew this session's pictures.
 *
 * A core can be given a real OpenGL context: its renderer runs in the sandbox
 * as always, but its GL calls are answered on the host's device. That is much
 * faster than a software rasteriser and it is not deterministic - the GPU is
 * outside the savestate and different on every machine - so a session that had
 * one reports 0 from ce_session_deterministic whatever its config said, and a
 * movie recorded on it must record that a GPU drew. Without that a replay
 * elsewhere desyncs with nothing to explain it.
 *
 * It is only ever true when the caller asked (ce_gl_request), this build has a
 * bridge, a driver gave it a context, and the core knew what to do with one. */
CE_API int32_t ce_session_gpu_drew(const ce_session *s);

/* ---------------------------------------------------------------------------
 * The GPU bridge itself. One context per process, because there is only ever
 * one machine running; sessions borrow it.
 */

/* Ask for hardware acceleration on the sessions opened after this call. Asking
 * is not having: see ce_session_gpu_drew for what actually happened. */
CE_API void ce_gl_request(int32_t want);
CE_API int32_t ce_gl_requested(void);

/* Whether a context exists now. Null-safe, cheap, false in a build without the
 * bridge compiled in. */
CE_API int32_t ce_gl_available(void);

/* What the driver calls itself ("4.6 (Core Profile) Mesa ... on ..."), for a
 * movie header and for saying which GPU a recording was made on. Empty when
 * there is none. */
CE_API const char *ce_gl_description(void);
/* The card and the driver a core's OpenGL context runs on, as the driver
 * gives them (GL_RENDERER, GL_VERSION) and in no language of Chimera's, for
 * a bug report to quote. Empty when there is no context. */
CE_API const char *ce_gl_renderer(void);
CE_API const char *ce_gl_version(void);

/* Give the current-context slot back to whoever had it.
 *
 * "Current context" is one slot per thread and the frontend draws its own
 * picture through it, so the bridge borrows it for the duration of a guest
 * call and returns it here. The engine calls this itself at every point where
 * a core stops running and the caller gets control back; a caller that runs
 * guest code by some other route can call it too. Harmless with no bridge, no
 * context, or nothing borrowed. */
CE_API void ce_gl_release(void);

/* The bridge's flight recorder: the last calls that crossed, an opcode each,
 * with markers where a frame took and gave back the context, a state was
 * loaded and a session took the bridge. A fixed block in this library's
 * memory, written as calls cross, so a crash module reading the dead process
 * finds what the driver was doing (docs/gpu-bridge.md, "The flight recorder").
 * Null and 0 bytes in a build without the bridge. */
/* Whether a SAME-SESSION state load should move the context id.
 *
 * A load moves it by default (issue #43): the state puts back a renderer's idea
 * of its GL objects but not the objects, so the renderer must build them again
 * or attach handles the driver has since given to something else. That is right
 * for a renderer whose caches survive the load holding stale names.
 *
 * It is wrong for one whose picture lives in the objects the rebuild throws
 * away: RPCS3 tears down its render targets and texture cache on a moved id,
 * and a rewind then presents an opaque black clear while the machine plays on
 * (and, after a few rewinds, aborts in its texture cache). Such a core declares
 * video.rebuildOnStateLoad false and keeps its renderer across a rewind.
 *
 * A genuinely new context still mints a new id at ce_gl_start, so a core that
 * opts out here still rebuilds when a project is reopened - which is the case
 * the rebuild exists for. Default 1: a core that says nothing is unaffected. */
CE_API void ce_gl_rebuild_on_state_load(int32_t on);
CE_API void *ce_gl_flight_recorder(uint32_t *bytes);
/* A Vulkan bridge command into the same flight recorder, as opcode +
 * 0x40000000 (the session hands this to miniBox's Vulkan host). */
CE_API void ce_vk_flight_note(unsigned op);

/* ---------------------------------------------------------------------------
 * The compile cache. A core that recompiles its machine's code keeps the
 * compiled objects between sessions in a directory the host owns: an object
 * is a pure function of the module, the core package and the target CPU, so a
 * warm run is a cold run minus the compile, and nothing about determinism,
 * movies or projects depends on whether the cache was warm. The contract is
 * miniBox's source/cache/cache-bridge.h; a core that keeps such files exports
 * SetCacheBridge and receives the dispatcher before Init.
 */
/* The directory for the sessions opened after this call (NULL: none). The
 * caller names it by core and package identity; the engine creates it and
 * keeps whatever the core names under it, relative paths only. */
CE_API void ce_cache_dir(const char *dir);
CE_API const char *ce_cache_dir_get(void);
/* Objects stored into and fetched from the cache by this session's core. */
CE_API uint64_t ce_session_cache_stored(const ce_session *s);
CE_API uint64_t ce_session_cache_fetched(const ce_session *s);

/* ---------------------------------------------------------------------------
 * Precompile sessions. A core that can fill its compile cache without running
 * exports SetPrecompile(index, count, firmware_too): the session boots, never
 * runs, compiles every module part whose name hashes to its index (so several
 * sessions side by side, in separate processes, each compile a share), and
 * reports when it is done. The caller pumps ce_session_frame_advance until
 * ce_session_precompile_done and reads the progress for its bar.
 */
/* Sessions this thread opens after this call are precompile sessions (count
 * 0: off). */
CE_API void ce_precompile_request(int32_t index, int32_t count, int32_t firmware_too);
/* -1 when this session is no precompile session or the core has none. */
CE_API int32_t ce_session_precompile_done(const ce_session *s);
CE_API int32_t ce_session_precompile_progress(const ce_session *s, uint32_t *done_out, uint32_t *total_out);

/* ---------------------------------------------------------------------------
 * Suggested settings. Before a project is final, a core may say what it would
 * choose for its files - the RPCS3 core looks the game up in the RPCS3 wiki's
 * recommendations it carries. The project is opened as ce_session_open_project
 * opens it (its files, its settings, the firmware given), then the core's
 * optional SuggestSettings export is called INSTEAD of Init, and the machine
 * is thrown away unstarted. A frontend asks this of the project its New
 * Project window has made so far, saved where it likes.
 *
 * Returns the core's answer, a JSON object - {"values": {setting: value, ...},
 * "note": "where they come from, or that nothing was found", "settings":
 * [the game's own setting declarations, as ce_session_game_settings has
 * them], ...} - valid until the next call on this thread; "" (length 0) when
 * the core has no such export; NULL with *error_out when the package or the
 * project cannot be opened at all. The values are suggestions for the
 * settings page, never applied by the engine.
 */
CE_API const char *ce_project_suggest_settings(
	const char *package_path, ce_project *project,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint64_t *len_out, const char **error_out);

/* What a movie made elsewhere amounts to, as this core reads it - a Doom
 * demo, say. The same session as ce_project_suggest_settings, asking the
 * core's optional ImportMovie export instead of SuggestSettings. The project
 * holds the files the person offered that have a slot, and the import options
 * the core declares (movieImport in waterbox.config) as its settings; the
 * movie is mounted as "movie", and each mount_names[i] from mount_paths[i]
 * beside it - a file the core may take as firmware, whose firmware id only
 * its answer can say. The machine is thrown away unstarted.
 *
 * Returns the core's JSON answer: {"error": "<a sentence>"} when it refuses,
 * otherwise the configuration the movie dictates - "settings", "firmware"
 * [{id, sha1}], "files" [{name, sha1, slot}] in load order, "notes" - and
 * "input", the movie as Chimera's input log. "" when the core has no such
 * export; NULL with *error_out when the package or the project cannot be
 * opened. Nothing is applied: the frontend builds the project from it with
 * its own project creation. */
CE_API const char *ce_project_import_movie(
	const char *package_path, ce_project *project, const char *movie_path,
	const char *const *mount_names, const char *const *mount_paths, int32_t mount_count,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint64_t *len_out, const char **error_out);
CE_API int64_t ce_session_button_count(const ce_session *s);
CE_API const char *ce_session_button_name(const ce_session *s, int64_t index);
CE_API int64_t ce_session_axis_count(const ce_session *s);
CE_API const char *ce_session_axis_name(const ce_session *s, int64_t index);
/* WHAT A CONTROL IS CALLED where a person reads very little of it: the one
 * character a pressed button writes into a movie's text, what heads its input
 * column, and the short header of an axis's column. The core's package
 * declares them ("mnemonics", "headers" and each axis's "header", in its input
 * declaration); a name it declares nothing for gets the rule - the first
 * character of the last word, the initials of an axis - which is all this
 * engine knows about naming, and knows of no machine (control_names.hpp).
 *
 * A button's header is for a machine whose buttons one character cannot tell
 * apart - a keyboard. It is one to eight characters, heads the column, and is
 * never written into a movie; a button with none is headed by its mnemonic.
 *
 * ce_session_button_mnemonics: one character a button, in declaration order.
 * ce_session_mnemonic_of / ce_session_button_header_of /
 * ce_session_axis_header_of: ANY name, looked up the way the session's own
 * are - a movie can carry a control the running machine does not have.
 * ce_control_mnemonic / ce_control_button_header / ce_control_axis_header:
 * the rule alone, for when there is no machine to ask.
 *
 * None of it is what a movie means: an entry is read by position, and any
 * character but '.' is a pressed button.
 *
 * The strings returned by the *_header* calls belong to the calling thread
 * until its next such call. */
CE_API const char *ce_session_button_mnemonics(const ce_session *s);
CE_API int32_t ce_session_mnemonic_of(const ce_session *s, const char *name);
CE_API const char *ce_session_button_header_of(const ce_session *s, const char *name);
CE_API const char *ce_session_axis_header_of(const ce_session *s, const char *name);
CE_API int32_t ce_control_mnemonic(const char *name);
CE_API const char *ce_control_button_header(const char *name);
CE_API const char *ce_control_axis_header(const char *name);

/* Whose a control is: its player (1, 2...) or the console's (0), as the
 * package declares it ("players" in its input declaration: a list of each
 * player's controls), or for a package that declares none, the "P<n> " in
 * front of its name. A movie entry's '|' groups are these, the console's
 * first, and the roll's thick lines part them. ANY name, as
 * ce_session_mnemonic_of takes. */
CE_API int32_t ce_session_player_of(const ce_session *s, const char *name);

/* What to call the session's system in front of a person: the package's own
 * word for it ("systemNames", by system id), or the id where it has none. */
CE_API const char *ce_session_system_name(const ce_session *s);

/* The value an axis rests at when nothing moves it (the package's `neutral`;
 * 127 on an Apple II paddle, 0 on a signed stick). What idle input carries. */
CE_API int32_t ce_session_axis_neutral(const ce_session *s, int64_t index);

/* Whether a declared control is one THIS machine has.
 *
 * waterbox.config declares the union of every peripheral a package's ports can
 * hold - it is a static declaration and cannot know what a project plugged in.
 * The running core can, because it read the port settings and built the machine
 * from them, so it is asked (its optional IsButtonActive/IsAxisActive exports)
 * once, after Init.
 *
 * An inactive control is absent from the frontend's controller, from TAStudio's
 * columns and from a movie entry. Its INDEX never moves: the count and the
 * order are the declaration's, so ce_session_set_button and every core's own
 * wire enum are untouched by any of this. A core that exports neither answer
 * has all of its controls, which is what every core had before this existed. */
/* Drive lights: one per medium the machine has (a disc, a hard disk), lit on
 * any frame that drive was read or written. A core that exports none has none,
 * and the count is zero - which is what a machine with no removable media
 * should show, rather than an icon that never lights. Names are settled at
 * load; the light is asked every frame. */
/* The settings THIS GAME has beyond the package's declaration - an arcade
 * game's dip switches, which differ game to game and so cannot be declared
 * once per package. A JSON array of setting declarations in waterbox.config's
 * own format ({"name", "display", "type", "options", "default",
 * "description"}), from the core's optional GetGameSettings export, read once
 * after Init. "" when the core declares none. A core that has them also
 * returns them from SuggestSettings, under "settings", so a new project's
 * wizard can show them before any machine runs. */
CE_API const char *ce_session_game_settings(const ce_session *s);

CE_API int32_t ce_session_drive_count(const ce_session *s);
CE_API const char *ce_session_drive_name(const ce_session *s, int32_t index);
CE_API int32_t ce_session_drive_light(const ce_session *s, int32_t index);

/* What is in a drive, for a drive that can be given something else: a machine
 * with several floppies or discs swaps between them through its own inputs, and
 * until this existed nothing on screen said which one was selected or whether
 * it had actually gone in.
 *
 * SELECTED and INSERTED are two facts and a core may keep them apart. DOSBox-X's
 * changer is a selector that Previous/Next move and a Swap input that puts the
 * selected image in: between the two, the image selected is not the image the
 * machine is reading. A Sega CD's tray can be EMPTY, which is inserted = -1. A
 * core whose swap is one step reports the same index for both.
 *
 * The guest's optional exports GetDriveMediaCount/Name/Selected/Inserted, all
 * four or none, indexed like the drive lights. _count is 0 for a drive that
 * holds one fixed thing (a hard disk) and for every core that exports none, and
 * the frontend then shows nothing - an indicator that never changes is noise.
 * _name is the image's name in the project, as the guest was given it; it is
 * copied out at load, because the list is settled then, while _selected and
 * _inserted are asked of the running machine. They are whole-machine state:
 * loading a state, a rewind included, answers with what was true at that
 * frame. */
CE_API int32_t ce_session_drive_media_count(const ce_session *s, int32_t index);
CE_API const char *ce_session_drive_media_name(const ce_session *s, int32_t index, int32_t media);
CE_API int32_t ce_session_drive_media_selected(const ce_session *s, int32_t index);
/* -1 when the drive is empty */
CE_API int32_t ce_session_drive_media_inserted(const ce_session *s, int32_t index);

CE_API int32_t ce_session_button_active(const ce_session *s, int64_t index);
CE_API int32_t ce_session_axis_active(const ce_session *s, int64_t index);

/* What THIS GAME calls a control.
 *
 * A package that runs thousands of games declares its controls by what they
 * are - "P1 Button 1" - and that declared name is the control's identity: a
 * movie's column, a key binding and a Lua script all go by it, so it must not
 * change from game to game. But the game has a name for it too - "Weak
 * Punch" - and a person reads that one. The running core knows it (its
 * optional GetButtonLabel/GetAxisLabel exports, asked once after Init with
 * the control's index in the declaration), and a frontend shows it BESIDE the
 * declared name: on hover over the control's column, in the key
 * configuration. It is never written into a movie or a project.
 *
 * "" for a control the game gives no name of its own, for one the machine
 * does not have, and for every control of a core that exports neither. The
 * pointer is valid as long as the session. */
CE_API const char *ce_session_button_label(const ce_session *s, int64_t index);
CE_API const char *ce_session_axis_label(const ce_session *s, int64_t index);

/* Axes are set per frame, before the advance they belong to. */
CE_API void ce_session_set_axis(ce_session *s, int32_t index, int32_t value);

/* WIDE INPUT: buttons past the packed mask's 64 (a DOS keyboard is 101 keys
 * before the mouse and joysticks). Set like axes - per frame, before the
 * advance; values persist until changed. The effective state of button i at
 * an advance is this call's value OR'd with packed bit i (i < 64), so a
 * caller uses whichever path fits and either alone is exact. The session
 * delivers only CHANGES to the guest through its SetButton export; a config
 * declaring more than 64 buttons refuses to open without that export.
 * Movie entries have always carried arbitrary button counts - one mnemonic
 * column per declared button - so recording and playback are unchanged. */
CE_API void ce_session_set_button(ce_session *s, int32_t index, int32_t pressed);

/* CALLS DURING A FRAME (issue 344). A core that reports each time the machine
 * reads its controls - its optional SetInputPollCallback export - has the
 * engine call fn(ctx) at each such read, part way through an advance. fn runs
 * on the advancing thread, inside the frame: it may read the machine's memory,
 * and must not advance, save, load or end the session. Null stops the calls.
 * ce_session_input_polls_reported is 1 when the core reports its reads, 0 when
 * it does not (fn is then never called). */
typedef void (*ce_input_poll_fn)(void *ctx);
CE_API int32_t ce_session_input_polls_reported(const ce_session *s);
CE_API void ce_session_set_input_poll_callback(ce_session *s, ce_input_poll_fn fn, void *ctx);

/* One frame: buttons is the bitmask (bit i = the config's buttons[i]);
 * buttons beyond 63 ride ce_session_set_button. Returns nonzero when the frame
 * was a lag frame (the guest never read input), per the config's lag export.
 *
 * render 0 skips the video copy, and - for a core that exports
 * SetRenderingEnabled - tells the core itself to stop drawing. That is turbo:
 * the frame still happens, the machine is still exactly the machine it would
 * have been, and only the picture is not produced. On a console with a 3D chip
 * the picture is most of the frame's cost, so a seek that would have taken a
 * minute takes a few seconds. The video buffer keeps the last frame that was
 * drawn, so a caller that wants a picture at the end of a fast run asks for the
 * last frame with render 1. */
CE_API int32_t ce_session_frame_advance(ce_session *s, uint64_t buttons, int32_t render);

/* The last rendered frame, BGRA, video_width*video_height pixels.
 * Invalidated by the next rendered frame_advance. */
CE_API const uint32_t *ce_session_video(const ce_session *s);

/* Keep the core DRAWING even on frames with render 0, so that only the readback
 * is skipped.
 *
 * Turbo assumes a skipped frame is work deferred: nobody looks at it, and the
 * next frame someone does look at is drawn from scratch. That holds for a
 * renderer that composes its picture out of the machine's own memory every
 * frame. It does not hold for one whose picture lives on the far side of the
 * GPU bridge, because what it draws PERSISTS there: a screen a game paints once
 * and then leaves alone is painted by exactly one frame, and if that frame was
 * skipped no later frame repaints it. The picture then shows an older screen
 * and no amount of warm-up brings it back - only replaying from before the
 * paint does. Cores that need this say so in their package (video.drawEveryFrame)
 * and pay the drawing, which on a real GPU is a fraction of the frame; the
 * readback, which is most of what turbo was saving, is still skipped. */
CE_API void ce_session_draw_every_frame(ce_session *s, int32_t on);

/* The LIVE frame size: a machine that changes video modes (DOS) reports it
 * per frame through optional GetVideoWidth/GetVideoHeight exports, clamped
 * to the config's declared buffer. Without the exports these equal
 * ce_session_width/height. Valid after the advance they belong to. */
CE_API int32_t ce_session_video_width(const ce_session *s);
CE_API int32_t ce_session_video_height(const ce_session *s);

/* The last frame's audio as interleaved stereo s16 (mono sources are
 * doubled), and its sample-pair count. Invalidated by the next advance. */
CE_API const int16_t *ce_session_audio(const ce_session *s, int32_t *sample_count);

/* The rate that sound is at: the package's audio.rate, or its machine's
 * audioRate (44100 when it says none). */
CE_API int32_t ce_session_audio_rate(const ce_session *s);

/* The last frame's sound at 44100 Hz, the rate Chimera plays and encodes at:
 * ce_session_audio resampled when the core mixes at another rate, and
 * ce_session_audio itself when it does not. Interleaved stereo s16; *pairs is
 * how many. Consecutive frames asked for join without a seam; a seek or a
 * state loaded starts afresh. Invalidated by the next advance. */
CE_API const int16_t *ce_session_sound(ce_session *s, int32_t *pairs);

/* The guest's whole-machine state. The buffer is the session's, reused and
 * invalidated by the next save. Load returns 0 on success. */
CE_API const uint8_t *ce_session_save_state(ce_session *s, uint64_t *len_out);
CE_API int32_t ce_session_load_state(ce_session *s, const uint8_t *data, uint64_t len);

/* A state kept in a file. _save_state hands the whole machine over as one
 * buffer, and a frontend array cannot be more than 2 GiB; a PS3's state is
 * (issue #84: creating a branch threw an overflow). Here the state goes from the
 * machine through zstd to the file and back without ever existing whole, so it
 * has no size it cannot be. This is what a TAStudio branch keeps: its state is a
 * file beside the project, not bytes in the frontend.
 *
 * tag is the caller's own few bytes, stored with the state and handed back on a
 * load - the frontend's frame and lag counters, which are not the machine's. The
 * file is written beside its name and moved into place, so a failed save leaves
 * whatever was there.
 *
 * The file says which savestate format it is (ce_state_format) and which build
 * wrote it (ce_state_writer_id), and a load checks the format before a byte
 * reaches the machine: a state of another format is refused by name rather than
 * loaded into nonsense (issue #115).
 *
 * _save: 0 done, 1 not (see _last_error). _load: 0 loaded; 1 the machine refused
 * the state or the file is damaged, and the machine is where a refused load
 * leaves it; 2 the state was never offered to the machine, which is untouched -
 * not a state file, cut short, or written in another savestate format, and
 * _last_error says which. */
CE_API int32_t ce_session_state_save_file(ce_session *s, const char *utf8_path, const uint8_t *tag, uint32_t tag_len);
CE_API int32_t ce_session_state_load_file(ce_session *s, const char *utf8_path, uint8_t *tag_out, uint32_t tag_cap, uint32_t *tag_len_out);
/* What the last state saved to or loaded from a file weighed: as the machine gave
 * it, and as the file holds it. Both saves and loads report through ce_progress_set
 * as they go - a load against the file's length, a save against what the last
 * state weighed (the end of a save is not known until it is reached). */
CE_API void ce_session_state_file_bytes(const ce_session *s, uint64_t *raw_out, uint64_t *stored_out);

/* The guest's self-described memory domains. */
CE_API int32_t ce_session_domain_count(const ce_session *s);
CE_API const char *ce_session_domain_name(const ce_session *s, int32_t index);
CE_API int64_t ce_session_domain_size(const ce_session *s, int32_t index);
CE_API int32_t ce_session_domain_writable(const ce_session *s, int32_t index);
/* A domain that is no memory of the machine's, only somewhere a core puts
 * what its game properties point at - a value with no one place of its own,
 * copied where the engine can read it (chimera#218) - or a block that a bus
 * already shows, in another byte order (ares' Nintendo 64, chimera#288). The
 * core says so with an
 * optional guest export, int32_t GetMemoryDomainHidden(int32_t index). Game
 * properties read and write through such a domain like any other; the memory
 * tools do not offer it: it is not in the Hex Editor's, RAM Search's or RAM
 * Watch's domain lists. 0 for a core that does not say. */
CE_API int32_t ce_session_domain_hidden(const ce_session *s, int32_t index);
/* Copies out [offset, offset+len); returns bytes copied (clamped at end). */
CE_API int64_t ce_session_domain_read(const ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len);

/* A game core's properties (docs/game-cores.md): named places in the memory
 * domains above - integers, floats, bools, text, bytes, arrays of them, bit
 * fields - from the JSON table of the core's optional GetGameProperties
 * export, read once after Init and checked against the domains. The engine
 * owns what the bytes mean: every read, write, text and parse below goes by
 * the one set of rules, so a watch, a poke, a freeze and a script agree.
 *
 * _table: the table as the engine understood it, every field filled in, and
 * what it left out and why - {"properties": [...], "problems": [...]}; a
 * property's index is its place in that list. Borrowed until the next
 * _table, and for a dynamic table also until the next _refresh or a call that
 * finds a property moved or gone (_get, _set, _text, _offset), which make the
 * next _table write it afresh; a caller that keeps it copies it (2026-10-18:
 * it said "for the session's lifetime", which a dynamic table is not). A core
 * without the export has an empty table.
 * _find: "Name" or "Name[3]" (an array's element, by the number the game
 * calls it - from the table's `first`, 0 unless it says); the index, or -1.
 * A fixed table's names are found in any case: they are a core author's
 * labels. A dynamic table's are the game's own and EXACT - "score" and
 * "Score" are two properties, and one asked for in another case is not there
 * (chimera#218). *element_out gets the element counted from 0, which is what
 * every other call here takes (0 without an index).
 * _at: the first property, in the table's order, one of whose elements covers
 * `address` in the named domain; -1 when none does. *starts_out: 1 when the
 * address is the element's first byte.
 * _get/_set: a value by kind (CE_PROPERTY_*). An integer comes back as INT
 * when its type is signed and UINT when not; _set takes any numeric kind for
 * a number - a float truncated - that fits the width as a signed or an
 * unsigned value (so -1 sets every bit of a u64) and refuses one that does
 * not; for a bool, anything but 0 is true; TEXT (as
 * UTF-8) for a string - cut at a whole character, NUL-padded - and BYTES of
 * exactly the property's length for bytes. _get's data is borrowed until the
 * next call on the session.
 * _text: the value as a person reads it - an enumeration's name when `named`
 * and it has one, a float at the fewest digits that read back the same, text
 * as UTF-8, bytes as hex pairs. Returns the length (without the NUL, which is
 * always written when cap > 0); -1 for no such property.
 * _set_text: the inverse - a whole number (decimal, or hex after 0x), an
 * enumeration's name, true/false, a number, text, or hex bytes.
 * The setters: 0 done; 1 not, and _last_error says why (no such property, a
 * value of the wrong kind or out of range, a property the game works out
 * afresh every step). */
enum
{
	CE_PROPERTY_INT = 0,
	CE_PROPERTY_UINT = 1,
	CE_PROPERTY_FLOAT = 2,
	CE_PROPERTY_BOOL = 3,
	CE_PROPERTY_TEXT = 4,
	CE_PROPERTY_BYTES = 5,
};
typedef struct ce_property_value
{
	int32_t kind;
	int32_t reserved;
	int64_t i;         /* INT; BOOL as 0 or 1 */
	uint64_t u;        /* UINT */
	double f;          /* FLOAT */
	const char *data;  /* TEXT (UTF-8) or BYTES */
	int64_t len;
} ce_property_value;
CE_API const char *ce_session_property_table(const ce_session *s);
CE_API int32_t ce_session_property_find(ce_session *s, const char *name, uint32_t *element_out);
CE_API int32_t ce_session_property_at(const ce_session *s, const char *domain, int64_t address, uint32_t *element_out, int32_t *starts_out);
CE_API int32_t ce_session_property_get(ce_session *s, int32_t index, uint32_t element, ce_property_value *out);
CE_API int32_t ce_session_property_set(ce_session *s, int32_t index, uint32_t element, const ce_property_value *in);
CE_API int32_t ce_session_property_text(ce_session *s, int32_t index, uint32_t element, int32_t named, char *buf, int32_t cap);
CE_API int32_t ce_session_property_set_text(ce_session *s, int32_t index, uint32_t element, const char *text);

/* A DYNAMIC table (its "dynamic": true): properties that move and come and go
 * while the machine runs - a Flash movie's variables, which live on its
 * emulator's heap. The table of such a core is its listing at one moment.
 * _dynamic: 1 when the table is one.
 * _refresh: takes the listing again (the core's GetGameProperties) and returns
 * how many it has. A property already known keeps its index and takes its new
 * place; a new one is added at the end; one the listing no longer has stays,
 * marked `"listed": false, "present": false` in _table. Nothing for a table
 * that is not dynamic.
 * _offset: where an element is now in its domain, -1 when it is not there.
 *
 * Between listings a dynamic property is looked for by NAME before every use
 * (the core's GetGameProperty): _get, _set, _text, _set_text and _offset each
 * ask where it is now, so a watch follows a variable that moved and reads
 * nothing from one that is gone. _find asks the core for a name the listing
 * did not have. */
CE_API int32_t ce_session_property_dynamic(const ce_session *s);
CE_API int32_t ce_session_property_refresh(ce_session *s);
CE_API int64_t ce_session_property_offset(ce_session *s, int32_t index, uint32_t element);

/* A game core's own timer (docs/game-cores.md: the table's "gameTimer"): the
 * game's elapsed time in milliseconds as the game counts it now, in *ms_out.
 * Returns 1 when the core names one, 0 when it does not (and *ms_out is 0).
 * _text: a time in milliseconds as a timer shows it, "mm:ss.mmm" (more
 * minute digits past 99); returns the length (without the NUL, always written
 * when cap > 0). */
CE_API int32_t ce_session_game_time_ms(ce_session *s, int64_t *ms_out);
CE_API int32_t ce_game_time_text(int64_t ms, char *buf, int32_t cap);

/* "" when no error. Invalidated by the next call on the same session. */
CE_API const char *ce_session_last_error(ce_session *s);

/* Whether the machine's guest has died (aborted, halted, faulted, exited), and
 * why, as one line; null while it lives. A frame advance, movie advance or seek
 * on a dead machine fails and runs nothing. Loading a state - a greenzone
 * restore, a branch - brings it back. Borrowed until the next call. */
CE_API const char *ce_session_guest_death(ce_session *s);

/* What built the waterbox host this engine loads (its wbx_build_info JSON):
 * the frontend shows it and movies record it. NULL when the host is not
 * loadable. Static string, never invalidated. */
CE_API const char *ce_host_build_info(void);

/* The raw guest pointer behind a memory domain, for pointer-backed peek/poke
 * (the hex editor's path). Stable for the session's lifetime - the guest is
 * non-PIE - and 0 when the domain has no linear backing. */
CE_API uint64_t ce_session_domain_ptr(const ce_session *s, int32_t index);
/* The same domain for READING only: a pointer to the same bytes that never
 * runs the core's own code. A read through ce_session_domain_ptr can: a core
 * that watches its memory by protecting pages (RPCS3's texture cache) hears
 * of the read and may change the machine. For the tools that read between
 * frames - the hex editor, RAM Search, RAM Watch. Never write through it: the
 * state history would not see the write. Stable for the session's lifetime;
 * 0 when the domain has no linear backing or miniBox cannot give a view. */
CE_API uint64_t ce_session_domain_read_ptr(const ce_session *s, int32_t index);

/* Re-composes the effective settings (declared defaults overlaid with
 * overrides_json) and hands them to the RUNNING guest through its live-
 * settings exports. 0 = applied; 1 = the core has no live-settings group
 * (the caller must reboot instead); 2 = error (see _last_error). */
CE_API int32_t ce_session_apply_settings(ce_session *s, const char *overrides_json);

/* ---- the optional guest ABI groups ----
 *
 * A core may export any subset of four independent tooling groups (surfaces,
 * registers, buses, trace). The session probes them once, post-Init (names and counts may depend on the rom and
 * settings). An absent group answers with a zero count / zero flag, and the
 * frontend simply does not offer the tool.
 */

/* surfaces: core-rendered viewer windows */
CE_API int32_t ce_session_surface_count(const ce_session *s);
CE_API const char *ce_session_surface_name(const ce_session *s, int32_t index);
CE_API int32_t ce_session_surface_width(const ce_session *s, int32_t index);
CE_API int32_t ce_session_surface_height(const ce_session *s, int32_t index);
/* BGRA, width*height pixels. Borrowed; invalidated by the next render of the
 * same surface. NULL when the guest gave nothing. */
CE_API const uint32_t *ce_session_surface_render(ce_session *s, int32_t index);

/* registers: the generic debugger's register box */
CE_API int32_t ce_session_register_count(const ce_session *s);
CE_API const char *ce_session_register_name(const ce_session *s, int32_t index);
/* how many hex digits the debugger shows; 32 when the core does not say */
CE_API int32_t ce_session_register_bits(const ce_session *s, int32_t index);
CE_API int64_t ce_session_register_value(const ce_session *s, int32_t index);
/* 0 = written; 1 = this core does not support writing registers */
CE_API int32_t ce_session_register_set(ce_session *s, int32_t index, int64_t value);
CE_API int32_t ce_session_has_executed_cycles(const ce_session *s);
CE_API int64_t ce_session_executed_cycles(const ce_session *s);

/* buses: address SPACES the guest resolves through its own mapper logic -
 * peek/poke, never pointer-mapped */
CE_API int32_t ce_session_bus_count(const ce_session *s);
CE_API const char *ce_session_bus_name(const ce_session *s, int32_t index);
CE_API int64_t ce_session_bus_size(const ce_session *s, int32_t index);
CE_API int32_t ce_session_bus_writable(const ce_session *s, int32_t index);
CE_API int32_t ce_session_bus_peek(const ce_session *s, int32_t index, int32_t addr);
/* A run of a bus at once: len bytes from addr into buf, the bytes that many
 * peeks would read. A core that exports ReadBus is asked a chunk at a time -
 * const uint8_t *ReadBus(int32_t bus, int64_t addr, int32_t len), len at most
 * CE_BUS_READ_CHUNK, answering a pointer to len bytes in its own memory that
 * stays good until the next call - and one without it is peeked a byte at a
 * time from here, which still spares the caller a call per byte. Bytes outside
 * the bus read as zero. Returns len, or 0 (and a zeroed buf) for a bus the core
 * does not have. RAM Search reads a bus this way: 64 MB a byte per call was
 * seconds of crossings into the guest (chimera#180). */
#define CE_BUS_READ_CHUNK 65536
CE_API int64_t ce_session_bus_read(const ce_session *s, int32_t index, int64_t addr, uint8_t *buf, int64_t len);
/* Which of a bus is live now. A bus's size is read once, so one over an
 * address space that is mostly nothing - a heap arena of a gigabyte with
 * thirty megabytes mapped - is that large whatever is in it, and a search of
 * it paid for all of it (chimera#218). A core that knows says, with an
 * optional guest export
 *   const int64_t *GetBusRanges(int32_t bus)
 * answering (address, length) pairs in its own memory, ended by a pair whose
 * length is 0, good until the next call; null for a bus it has no word on.
 * Like every between-frame export it must not change the machine.
 * This copies up to cap pairs into pairs and returns how many the bus has -
 * more than cap means call again with room - or -1 when nothing is said, which
 * is "all of it". 0 is an answer: nothing is live. The ranges are for
 * ce_ramsearch_set_ranges; reading and poking a bus are what they were. */
#define CE_BUS_RANGES_MAX (1 << 20)
CE_API int32_t ce_session_bus_ranges(const ce_session *s, int32_t index, int64_t *pairs, int32_t cap);
CE_API void ce_session_bus_poke(ce_session *s, int32_t index, int32_t addr, int32_t value);

/* savedata export: files the guest deems the user's progress (a memory
 * card's contents, a disk image), enumerated at export time - the list is
 * dynamic, a game creates files while it runs (docs/save-data.md). The
 * frontend writes them out verbatim; nothing here interprets a byte. */

/* nonzero when the core exports the savedata group */
CE_API int32_t ce_session_savedata_available(const ce_session *s);
/* Snapshots the guest's file list and returns its length; name and size
 * below refer to this snapshot. Call at a frame boundary. Entries with a
 * path that is not relative-and-clean (leading '/', "..", backslash) are
 * dropped here, with a warning on stderr, rather than handed to a writer. */
CE_API int32_t ce_session_savedata_count(ce_session *s);
/* Borrowed; invalidated by the next snapshot. NULL when out of range. */
CE_API const char *ce_session_savedata_name(const ce_session *s, int32_t index);
CE_API int64_t ce_session_savedata_size(const ce_session *s, int32_t index);
/* Copies out [offset, offset+len) of file index; returns bytes copied
 * (clamped at the file's end). Ranged so a huge file streams in chunks and
 * is never materialized whole. */
CE_API int64_t ce_session_savedata_read(ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len);

/* trace: the guest appends lines to a buffer of its own; drain it once per
 * frame - a callback per instruction would cross the sandbox boundary
 * millions of times a second */
CE_API int32_t ce_session_trace_available(const ce_session *s);
CE_API const char *ce_session_trace_header(const ce_session *s);
/* The session REMEMBERS the desired flag: a savestate overwrites the guest's
 * own tracing state, and load_state re-asserts this (and clears the restored
 * buffer - its lines were traced before the load and would appear out of
 * order). */
CE_API void ce_session_trace_enable(ce_session *s, int32_t on);
/* The lines the guest traced since the last drain, as consecutive
 * NUL-terminated strings (line_count of them), cleared on the way out.
 * overflow_out reports the guest's raw truncation flag for this window.
 * Borrowed; invalidated by the next drain. */
CE_API const uint8_t *ce_session_trace_drain(
	ce_session *s, uint64_t *len_out, int32_t *line_count_out, int32_t *overflow_out);


/* ---- the session's movie ----
 *
 * The session can carry the movie itself: the input log lives inside the
 * engine, entries are parsed and generated by the engine (the Bk2 entry
 * format - groups by player, axes before buttons, positional parse), and
 * the frame position is session state. This is the movie-correctness core:
 * a frontend can display it, but cannot desync it.
 *
 * Frame numbering: the session sits "before frame N" having applied inputs
 * 0..N-1; ce_session_frame reports N.
 */

/* Copies a log's entries in and enters PLAY mode at the current frame
 * (normally 0, right after open). 0 on success. */
CE_API int32_t ce_session_movie_load(ce_session *s, const ce_movie_log *log);

/* Enters RECORD mode. mnemonics gives the character generated entries use
 * for each pressed button, one per button in config order (the frontend's
 * per-system vocabulary; pass NULL for a neutral fallback of the button
 * name's first character). Recording over existing entries truncates them:
 * recording at frame N drops entries >= N before appending - the rerecord. */
CE_API void ce_session_movie_record(ce_session *s, const char *mnemonics);

/* 0 = no movie, 1 = play, 2 = record, 3 = finished (play ran past the end;
 * input is the caller's again, nothing is appended). */
CE_API int32_t ce_session_movie_mode(const ce_session *s);
CE_API int64_t ce_session_movie_length(const ce_session *s);
CE_API int64_t ce_session_frame(const ce_session *s);

/* The session's own log, for saving through ce_movie_log_serialize.
 * Borrowed and READ-ONLY: mutate the movie through the session, never
 * through this. Invalidated when the session is freed, and not to be read
 * while another thread edits the movie: ce_session_movie_text is. */
CE_API const ce_movie_log *ce_session_movie_log(const ce_session *s);

/* Decodes one entry against THIS session's controller: states_out (may be
 * null) gets ce_session_button_count bytes, 0/1 per button in config order -
 * exact for any width (a keyboard is a hundred keys, Mednafen's controllers
 * are 594: issue 411) - and axes_out (may be null) gets ce_session_axis_count
 * values in config order, each axis the entry does not carry left at its
 * declared neutral. 0 on success, nonzero when the entry runs out before the
 * controller does or an axis field will not parse.
 * The inverse - generating an entry - is what record mode does; a caller that
 * wants an entry drives the movie rather than formatting one itself. */
CE_API int32_t ce_session_movie_entry_decode(
	const ce_session *s, const char *entry, uint8_t *states_out, int32_t *axes_out);

/* ---- the movie, edited frame by frame (the window, issue 142)
 *
 * The session's movie is the one a piano roll edits: a frontend reads and
 * writes a frame's input through these calls and never writes a line itself -
 * the engine writes it, in the movie's own columns (its LogKey), widening the
 * key for a control it does not name yet, as recording does (owner,
 * 2026-10-22: the engine owns the movie).
 *
 * These four, ce_session_movie_length and ce_session_movie_text may be called
 * from any thread while another runs the machine: the movie has a lock of its
 * own. Every other session call stays on one thread.
 *
 * An edit is honoured before the machine moves again: the next
 * ce_session_movie_advance or ce_session_seek drops the stored states after
 * the earliest frame edited, and when the machine is already past it, puts the
 * machine back on a stored frame before the edit and plays the movie up to
 * where it was - so the machine is always what the movie it has makes it. */

/* A frame's buttons (states_out: ce_session_button_count bytes, 0/1 per
 * button in config order, every one of them; may be NULL) and axes
 * (ce_session_axis_count values; may be NULL). 0 read; 1 past the movie's
 * end (nothing pressed, axes neutral); -1 the line cannot be read. */
CE_API int32_t ce_session_movie_get(const ce_session *s, int64_t frame, uint8_t *states_out, int32_t *axes_out);
/* Writes a frame's input: states (ce_session_button_count bytes; NULL for
 * none pressed), axes (NULL for neutral). A frame past the end lengthens the
 * movie with empty frames up to it. A session with no movie gets one, in play
 * mode, naming its columns as a recording does. 0, or -1 for a frame before 0. */
CE_API int32_t ce_session_movie_set(ce_session *s, int64_t frame, const uint8_t *states, const int32_t *axes);
/* Shortens the movie, or lengthens it with empty frames. */
CE_API void ce_session_movie_resize(ce_session *s, int64_t frames);
/* `count` empty frames put in at `frame` (the frames from it move later),
 * and `count` frames taken out from `frame` (the ones after move earlier).
 * Either is an edit from `frame`, honoured as _set's are. Any thread. */
CE_API void ce_session_movie_insert(ce_session *s, int64_t frame, int64_t count);
CE_API void ce_session_movie_remove(ce_session *s, int64_t frame, int64_t count);
/* The whole movie as ce_movie_log_serialize writes it (the [Input] block), to
 * save or to keep as a branch; ce_session_movie_load takes it back. Belongs to
 * the calling thread until its next call of this. */
CE_API const char *ce_session_movie_text(ce_session *s, uint64_t *len_out);

/* The input journal of the session's own movie (owner, 2026-10-24): what
 * ce_movie_log_journal_open_with does for a log a frontend holds, done on the
 * movie the session keeps, so that every change to it - an edit from any
 * thread, a frame recorded, a movie loaded over it - is on its way to the disk
 * the moment it is made. _journal_open writes the whole movie and the
 * frontend's records (`frontend_image`, may be NULL) to `path` and appends
 * from then on; a session with no movie yet gets an empty one. 0 on success.
 * _journal_note appends one of the frontend's records; _journal_close stops
 * appending and, with `remove` nonzero, deletes the journal; _journaling says
 * whether one is being written. Freeing the session closes the journal and
 * keeps the file. Any thread: they take the movie's lock. A journal is read
 * back with ce_movie_log_journal_replay. */
CE_API int32_t ce_session_movie_journal_open(ce_session *s, const char *path, const char *frontend_image);
CE_API void ce_session_movie_journal_note(ce_session *s, const char *record);
CE_API void ce_session_movie_journal_close(ce_session *s, int32_t remove);
CE_API int32_t ce_session_movie_journaling(const ce_session *s);

/* One frame under the movie. In play mode the input comes from the log
 * (buttons/axes are ignored) until the log runs out, which flips the mode to
 * finished; in record and finished modes the input is the caller's - the
 * packed mask OR'd with the ce_session_set_button states, so wide
 * controllers record exactly what the machine receives - and record appends
 * the generated entry. axes may be NULL for all-neutral;
 * otherwise it carries ce_session_axis_count values in config order.
 * Returns like ce_session_frame_advance (nonzero = lag frame), or -1 on
 * error (no movie loaded, or an unparseable entry - see _last_error). */
CE_API int32_t ce_session_movie_advance(ce_session *s, uint64_t buttons, const int32_t *axes, int32_t render);

/* ---- the greenzone ----
 *
 * A budget-bounded history of whole-machine states along the movie, so a
 * seek is "restore the nearest state at or before N, replay to N" instead of
 * a replay from power-on. States are captured after every movie advance; the
 * oldest above the anchor (the first captured state, normally frame 0) are
 * evicted when the budget fills, and the anchor never is - every frame stays
 * reachable.
 */

/* Enables with a byte budget (captures the current frame immediately as the
 * anchor), or disables and drops everything with budget 0. */
CE_API void ce_session_greenzone_enable(ce_session *s, uint64_t budget_bytes);
CE_API int64_t ce_session_greenzone_count(const ce_session *s);

/* Anchor spacing, and the density of what is SPILLED to disk, all in FRAMES
 * because the engine does not know a core's frame rate and the caller does.
 * Any argument left at 0 keeps the current value.
 *
 * near_frames, mid_frames, mid_stride and far_stride once decided how dense the
 * history was at each distance from the newest frame, whatever the budget. Since
 * 2026-09-15 a greenzone keeps every frame in memory until its budget is full and
 * is then thinned toward ce_session_greenzone_band_goal's shape; these four now
 * only decide how a stretch already spilled to disk is settled once the far
 * boundary (near_frames + mid_frames behind the newest frame) passes it, to one
 * in far_stride.
 *
 * anchor_spacing is the one that decides what a seek costs: a restore walks the
 * links of one anchor's stretch and no further, so it trades memory for
 * latency directly. A far_stride wider than it collapses old stretches to their
 * anchors, which is usually what is wanted. Positive, it is the spacing
 * exactly. Negative, its magnitude is the widest a stretch may be, and a
 * stretch closes sooner once its links weigh as much as its anchor - which
 * picks the spacing per core by what a seek would cost. That is the default,
 * capped at 600 frames.
 *
 * The defaults suit 60 frames a second on a core nobody has measured. What they
 * cost on a heavy one, and how to pick better, is docs/state-manager.md. */
CE_API void ce_session_greenzone_bands(ce_session *s, int64_t near_frames, int64_t mid_frames,
	int64_t mid_stride, int64_t far_stride, int64_t anchor_spacing);

/* Driving the history yourself, for a caller that owns its own movie and its
 * own frame advance - which the frontend does, and which the hooks inside
 * ce_session_movie_advance therefore never see.
 *
 * _before_advance marks the epoch and MUST be called before the machine moves,
 * since a delta is what changed since a moment and the moment has to be marked
 * first; a capture with no epoch open is simply a whole state, which is correct
 * and merely dearer. _capture stores the frame just reached - the caller says
 * which, because it is the caller counting. A frame the history already
 * reaches past is a replay: nothing is stored and nothing is dropped, so going
 * back and playing the same input forward keeps what is ahead. A caller that
 * CHANGES the timeline - an edit, recording over an entry, input that is not
 * the movie's - says so with ce_session_greenzone_invalidate before the frame
 * is played (user-decided, 2026-09-13). _restore puts the machine on a
 * stored frame, one ce_session_greenzone_nearest offered, and replaying from
 * there is then the caller's business.
 *
 * ce_session_movie_advance and ce_session_seek are these same three, for a
 * caller that lets the engine hold the movie. 0 on success. */
CE_API void ce_session_greenzone_before_advance(ce_session *s);
CE_API int32_t ce_session_greenzone_capture(
	ce_session *s, int64_t frame, const uint8_t *note, uint32_t note_len);
CE_API int32_t ce_session_greenzone_restore(ce_session *s, int64_t frame);

/* What _capture was given for `frame`, copied into `out` (which may be NULL to
 * ask only the size). Returns the note's length, or 0 when there is none.
 *
 * The note is for the caller's own bookkeeping beside the machine: a frontend
 * has a lag flag, a lag count and a frame number that a savestate does not
 * carry, and keeping those in a table of its own would mean mirroring every
 * invalidation, eviction, coarsening and spill the history does. Riding along
 * is the only way it stays true. The engine never looks inside it. */
CE_API uint32_t ce_session_greenzone_note(
	const ce_session *s, int64_t frame, uint8_t *out, uint32_t out_len);

/* Frames to keep reachable whatever the bands would otherwise do: a pinned
 * frame is never coarsened away, and a stretch holding one is spilled rather
 * than dropped. What deserves pinning is the caller's business - a marker
 * somebody wants to jump to instantly is the case this exists for, and nothing
 * the engine could work out for itself. Pinning a frame the history does not
 * hold is not an error; it takes effect if that frame is ever stored.
 * _unpin_all forgets the lot, for a caller rebuilding its set. */
CE_API void ce_session_greenzone_pin(ce_session *s, int64_t frame, int32_t pinned);
CE_API void ce_session_greenzone_unpin_all(ce_session *s);

/* Where the far band goes once the budget is full; NULL or "" for nowhere.
 *
 * The oldest stretches are the right thing to put on disk - large, rarely
 * touched, and regenerable if the file is lost - so with a directory the budget
 * costs reading them back, and without one it costs replaying to them. The
 * caller owns the directory and the file in it is the engine's, named and
 * removed by it; changing the directory throws away what was already out there.
 * A project's cache directory is what this is for. */
CE_API void ce_session_greenzone_spill(ce_session *s, const char *dir);
/* Whether putting the far band on disk has failed since the directory was set;
 * a full disk, near enough always. Setting a new directory clears it.
 *
 * The history carries on when it happens - it thins in memory instead, which
 * is the right fallback and completely silent. From a piano roll that looks
 * like the greenzone mysteriously going sparse, so somebody has to be told.
 * The engine knows; saying it is the frontend's job, which is why this is a
 * question to ask rather than a message to print. */
/* What the spill file may weigh, or 0 for no limit.
 *
 * ce_session_greenzone_enable bounds MEMORY, and the history meets that budget
 * by moving the oldest stretches to the spill directory - so on its own it
 * bounds half of what a history costs. This bounds the other half, by the same
 * rule: oldest first, and the room they held is given back. What it costs is
 * replaying to reach a frame that used to be stored, which is what every cache
 * here costs - time, never work.
 *
 * A pinned frame's stretch is never dropped, and neither is the newest.
 */
CE_API void ce_session_greenzone_disk_budget(ce_session *s, uint64_t budget_bytes);

/* The widest the near band's stride may grow: at most one frame in this many
 * is kept right behind the playhead, 1 to 32, 4 by default.
 *
 * The history thins the near band when storing every frame costs too much of
 * the run, and on a heavy core it used to climb to one in 32 - the frames a
 * person rewinds to were then the sparsest ones it held. A rewind near the
 * playhead replays at most max_stride - 1 frames; each step down costs speed
 * (docs/state-manager.md, "How close the near band stays"). */
CE_API void ce_session_greenzone_max_near_stride(ce_session *s, int64_t max_stride);

/* How often the greenzone stores a frame (TAStudio's "Greenzone" box): 1 is
 * every frame (the default), N stores only the multiples of N, 0 turns it off.
 * Only which frames are stored changes; anchors, deltas and bands work as
 * ever, and a sparse history stores deltas that span the frames between.
 * While off, ce_session_greenzone_before_advance and
 * ce_session_greenzone_capture store nothing and cost nothing - they only note
 * where the machine is, so that edits made meanwhile are still honoured - and
 * what is already stored stays usable. Turning it on from off stores a whole
 * state at the current frame at once. */
CE_API void ce_session_greenzone_capture_period(ce_session *s, int64_t period);

/* How many snapshots each band of a full greenzone aims to hold, 32 by default.
 *
 * A greenzone keeps every frame it captures until its memory budget is full;
 * then it gives frames up toward bands measured back from the newest stored
 * frame that double in length - with the defaults the last 128 frames, then to
 * 384, 896, 1920 ... - each aiming for this many snapshots, so 4 apart, then 8,
 * 16, 32 ... A goal, not a quota: the band holding the most gives one up first,
 * so a budget too small for the goal is spread across the bands, and a band's
 * last snapshot is never given up (docs/state-manager.md). */
CE_API void ce_session_greenzone_band_goal(ce_session *s, int64_t goal);

/* What the stretches now in the spill file weigh. */
CE_API uint64_t ce_session_greenzone_disk_bytes(const ce_session *s);

CE_API int32_t ce_session_greenzone_spill_failed(const ce_session *s);
/* The nearest stored frame at or before frame; -1 when none is. */
CE_API int64_t ce_session_greenzone_nearest(const ce_session *s, int64_t frame);
/* Drops stored states AFTER frame - an input edit at frame N makes every
 * later state a lie, while the state at N itself (inputs 0..N-1) still holds. */
CE_API void ce_session_greenzone_invalidate(ce_session *s, int64_t after_frame);

/* The pictures of frames already drawn, kept beside the greenzone and shown
 * again (chimera#190).
 *
 * A machine put back on a frame it has been on draws that frame's picture
 * again. A renderer on the far side of the GPU bridge draws it wrong for a
 * while after a state load - it rebuilds its textures and buffers as the game
 * touches them, and until then what comes back is pieces of the picture from
 * before the load. The picture the frame had before is the same picture, so
 * the session keeps the picture of every frame it shows, compressed, and for
 * the first `settle_frames` frames SHOWN after a load a frame that has one
 * shows that instead of what was just read back. Shown, not run: a renderer
 * rebuilds by drawing, so the frames a seek runs unseen do not count. A
 * restore onto a frame that has one shows it at once: a load draws nothing by
 * itself.
 *
 * What it cannot do is draw a frame nobody has seen: after an edit the frames
 * past it are new, they have no picture, and they show what the core draws.
 * A picture drawn within the settle is never KEPT - a wrong one kept would be
 * the one shown from then on - and past the settle the picture read back is
 * the one shown, and replaces what the frame had.
 *
 * Pictures are in no savestate, history file or movie, and take no part in
 * what the machine does. The least recently used go first when the budget is
 * full; ce_session_greenzone_invalidate forgets those past the edit.
 *
 * A session whose pictures a GPU draws keeps them without being asked, from
 * the moment its greenzone is enabled: an eighth of the greenzone's budget,
 * between 32 and 256 MiB. _pictures chooses instead: a budget in bytes (0:
 * none are kept) and the settle in frames (negative: leave it), for any
 * session.
 *
 * THE SETTLE IS SHORT ON PURPOSE: 3 frames unless told otherwise
 * (user-decided, 2026-10-08: "2~3 frames at most, configurable"). While it
 * lasts, a frame that has a kept picture does not show what the core has
 * just drawn, and somebody who changed something wants to see what the core
 * draws. _picture_settle sets it alone, leaving the budget as it is; 0 means
 * a frame advance always shows the core's picture, and only a restore onto a
 * frame shows its kept one. */
CE_API void ce_session_greenzone_pictures(ce_session *s, uint64_t budget_bytes, int32_t settle_frames);
CE_API void ce_session_greenzone_picture_settle(ce_session *s, int32_t settle_frames);
CE_API int64_t ce_session_greenzone_picture_count(const ce_session *s);
CE_API uint64_t ce_session_greenzone_picture_bytes(const ce_session *s);
/* Restores the nearest stored state at or before frame, then replays the
 * movie to frame. Needs the movie's entries up to frame. 0 on success. */
CE_API int32_t ce_session_seek(ce_session *s, int64_t frame);

/* The machine was put on `frame` of the session's movie by a state the
 * caller loaded itself - a branch's, kept in a file (ce_session_state_load_file)
 * - and the caller vouches that it is the machine the movie makes there. The
 * session counts from that frame, a machine the movie had left behind is no
 * longer behind, edits made meanwhile still drop the states after them, and
 * the frame is stored in the history so that coming back to it is a restore.
 * 0 done; 1 there is no movie, or the frame is past its end. */
CE_API int32_t ce_session_movie_at(ce_session *s, int64_t frame);

/* Rerecords: how many times the movie was changed over work already done. An
 * edit counts one when, honoured, it drops at least one stored state (an edit
 * past everything stored redoes nothing), and so does a movie loaded over this
 * one (a branch). Whether an edit counts is decided when it is made: a
 * frontend turns counting off for the rest of a stroke after its first cell,
 * so painting across many frames is one rerecord (as Chimera's TAStudio
 * counts them). _set gives the count a project was saved with. Any thread. */
CE_API uint64_t ce_session_rerecords(const ce_session *s);
CE_API void ce_session_set_rerecords(ce_session *s, uint64_t count);
CE_API void ce_session_rerecord_counting(ce_session *s, int32_t on);

/* Frozen addresses (cheats): values the engine writes into the machine before
 * every frame it runs - a frame advance, a movie advance, a seek's replay -
 * for this session only. They are never saved, and never go into a movie, a
 * project or a state: frozen values are for looking into a game, not for
 * making a movie (the owner, 2026-10-24). A freeze changes the machine, so
 * the states kept while one is on are of the machine with it.
 * _freeze: bytes of a writable memory domain with linear backing - size 1, 2
 * or 4 at address, value in the byte order asked (big_endian 1 or 0). With
 * compare_kind other than 0 the value is written only when `compare OP now`
 * holds, OP by kind: 1 ==, 2 !=, 3 >, 4 >=, 5 <, 6 <= (Chimera's conditional
 * cheats). A freeze already at that domain and address is replaced. Returns
 * its index, or -1 (ce_session_last_error says why).
 * _freeze_property: a game property's element, set from its text by the
 * engine's rules before every frame; one already on that element is replaced.
 * _unfreeze: by index (the later ones move down). _freeze_enable: a freeze
 * switched off stays in the list and writes nothing; _freeze_enabled is 1 on,
 * 0 off, -1 no such freeze. _frozen: the index of a freeze that is on and
 * writes `address` of `domain`, else -1. The machine's thread. */
CE_API int32_t ce_session_freeze(ce_session *s, int32_t domain, int64_t address, int32_t size, int32_t big_endian, uint64_t value,
	int32_t compare_kind, uint64_t compare);
CE_API int32_t ce_session_freeze_property(ce_session *s, int32_t index, uint32_t element, const char *text);
CE_API int32_t ce_session_freeze_count(const ce_session *s);
CE_API void ce_session_unfreeze(ce_session *s, int32_t index);
CE_API void ce_session_unfreeze_all(ce_session *s);
CE_API void ce_session_freeze_enable(ce_session *s, int32_t index, int32_t on);
CE_API int32_t ce_session_freeze_enabled(const ce_session *s, int32_t index);
CE_API int32_t ce_session_frozen(const ce_session *s, int32_t domain, int64_t address);

/* The history, kept between sessions. It is written to one file, streamed a
 * segment at a time - nothing about it is ever assembled in memory, which is
 * the whole point: a history assembled in one buffer stops near 2GB, so a
 * long run's history would silently fail to save and take the work with it.
 *
 * machine_id says which machine these states belong to. The caller owns that
 * question, knowing about cores, settings and files as it does; the engine
 * only carries the string and refuses a history that names a different one,
 * because a state is the memory of one exact machine and the sandbox checks
 * only the core binary. Loading an absent, unreadable or foreign history is
 * not an error - it leaves the history empty and costs recomputation, never
 * work, which is what a cold greenzone has always meant. 0 on success, and
 * see _last_error otherwise. */
CE_API int32_t ce_session_history_save(ce_session *s, const char *path, const char *machine_id);
CE_API int32_t ce_session_history_load(ce_session *s, const char *path, const char *machine_id);

/* The same save, queued rather than waited for.
 *
 * Writing a history is the longest thing a project save does - up to fourteen
 * gigabytes under the default budgets, which measured ten seconds to a Linux
 * disk and over a minute to NTFS - and it has nothing to do with the machine:
 * it is bytes going to a file. So it goes to the history's writer, and the run
 * carries on. The file describes the history as it stands at THIS call, not as
 * it ends up; frames captured while it is written belong to the next save.
 *
 * _later returns 0 when it was queued (and does it in line, returning the same,
 * when helpers are off). _pending says whether it is still being written.
 * _wait blocks until it is done and returns 0 when it worked - including when
 * there was nothing queued, which is not a failure. Closing a project is where
 * the wait belongs: the files must be whole before they are let go. */
CE_API int32_t ce_session_history_save_later(ce_session *s, const char *path, const char *machine_id);
CE_API int32_t ce_session_history_save_pending(ce_session *s);
CE_API int32_t ce_session_history_save_wait(ce_session *s);

/* ---------------------------------------------------------------------------
 * RAM Search. The candidate set, its previous values, its change counts and
 * its undo history live here, so that a search costs what the memory searched
 * costs and no more: a domain of any size can be searched, and the only limit
 * is the host's memory (issue #89; the frontend used to hold one object per
 * address and refused a domain over 64 MiB).
 *
 * A search is independent of any session: it is given the domain's memory as a
 * pointer, or - for a domain with no linear backing, such as a bus - as a
 * function that copies a range out. The pointer must outlive the search.
 *
 * Numbering. size: 1, 2, 4 bytes. compare: 0 previous value, 1 specific value,
 * 2 specific address, 3 number of changes, 4 difference. op: 0 ==, 1 >, 2 >=,
 * 3 <, 4 <=, 5 !=, 6 different by. display: 0 unsigned (and anything shown
 * from the raw bits), 1 signed, 2 float. previous_type: 0 original, 1 last
 * search, 2 last frame, 3 last change. column: 0 address, 1 value, 2 previous,
 * 3 changes, 4 difference.
 *
 * Calls that can need memory return 0 when they worked and -1 when the host
 * had none to give (_search returns the number removed, or -1).
 */
typedef struct ce_ramsearch ce_ramsearch;
/* Copies [offset, offset+len) into buf; returns the bytes copied. */
typedef int64_t (*ce_ramsearch_read_fn)(void *user, int64_t offset, uint8_t *buf, int64_t len);

CE_API ce_ramsearch *ce_ramsearch_create(const uint8_t *base, ce_ramsearch_read_fn fn, void *user, int64_t domain_size);
CE_API void ce_ramsearch_destroy(ce_ramsearch *rs);
/* Which of the domain is live: n pairs of (address, length), in any order -
 * what ce_session_bus_ranges answers for a bus that is mostly nothing. The
 * next _start searches those alone (its memory and its reads cost what the
 * live part costs), and keeps to them until the one after: what is mapped
 * later is not part of a search already begun. No candidate spans two
 * ranges, and an address outside them is not one _add_addresses takes.
 * n < 0: the whole domain, which is how every search starts out. */
CE_API int32_t ce_ramsearch_set_ranges(ce_ramsearch *rs, const int64_t *pairs, int64_t n);
/* Bytes the running search covers: the domain's size, or its live part. */
CE_API int64_t ce_ramsearch_searched_size(const ce_ramsearch *rs);
/* Every address becomes a candidate. detailed keeps a current value and a
 * change count per candidate, fed by _update once a frame. */
CE_API int32_t ce_ramsearch_start(ce_ramsearch *rs, int32_t size, int32_t misaligned, int32_t big_endian, int32_t detailed);
CE_API int64_t ce_ramsearch_count(const ce_ramsearch *rs);
/* The index-th candidate as listed; 0 when there is no such row. */
CE_API int32_t ce_ramsearch_row(const ce_ramsearch *rs, int64_t index, uint64_t *address, uint32_t *current, uint32_t *previous, uint32_t *changes);
/* Where an address is listed, or -1. */
CE_API int64_t ce_ramsearch_index_of(const ce_ramsearch *rs, uint64_t address);
CE_API int64_t ce_ramsearch_search(ce_ramsearch *rs, int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by, int32_t previous_type);
/* Whether that search would remove this row (the preview colouring). */
CE_API int32_t ce_ramsearch_would_remove(const ce_ramsearch *rs, int64_t index, int32_t compare, int32_t op, int32_t display, uint32_t value, uint32_t different_by);
CE_API int32_t ce_ramsearch_update(ce_ramsearch *rs, int32_t previous_type);
CE_API int32_t ce_ramsearch_set_previous_to_current(ce_ramsearch *rs);
CE_API void ce_ramsearch_clear_change_counts(ce_ramsearch *rs);
CE_API void ce_ramsearch_set_big_endian(ce_ramsearch *rs, int32_t big_endian);
CE_API int32_t ce_ramsearch_set_previous_type(ce_ramsearch *rs, int32_t previous_type);
CE_API int32_t ce_ramsearch_remove_indices(ce_ramsearch *rs, const int64_t *indices, int64_t n);
CE_API int32_t ce_ramsearch_remove_addresses(ce_ramsearch *rs, const uint64_t *addresses, int64_t n, int32_t record_undo);
CE_API int32_t ce_ramsearch_add_addresses(ce_ramsearch *rs, const uint64_t *addresses, int64_t n, int32_t append);
/* Re-reads the candidates at another size; forgets the history. */
CE_API int32_t ce_ramsearch_convert_to(ce_ramsearch *rs, int32_t size);
CE_API int32_t ce_ramsearch_sort(ce_ramsearch *rs, int32_t column, int32_t reverse, int32_t display);
CE_API int64_t ce_ramsearch_out_of_range_count(const ce_ramsearch *rs);
CE_API int32_t ce_ramsearch_remove_out_of_range(ce_ramsearch *rs);
/* Undo: a search and a removal can each be taken back, five deep. */
CE_API void ce_ramsearch_set_undo_enabled(ce_ramsearch *rs, int32_t on);
CE_API int32_t ce_ramsearch_can_undo(const ce_ramsearch *rs);
CE_API int32_t ce_ramsearch_can_redo(const ce_ramsearch *rs);
CE_API void ce_ramsearch_clear_history(ce_ramsearch *rs);
/* Each returns the change in the candidate count. */
CE_API int64_t ce_ramsearch_undo(ce_ramsearch *rs);
CE_API int64_t ce_ramsearch_redo(ce_ramsearch *rs);

/* ---- the MCP server ----
 *
 * A way for an AI agent's program to use the machine a frontend has open
 * (docs/mcp-server.md). MCP, the Model Context Protocol, is the standard such
 * programs speak: a server lists tools and the agent calls them.
 *
 * The agent's program does not talk to the frontend. It starts chimera-mcp
 * (source/engine/tools), and chimera-mcp reaches the frontend through a pipe
 * that is local to the machine and open to the same user only - a named pipe
 * on Windows, two FIFO files on Linux. No socket is opened and no network API
 * is used.
 *
 * The frontend's part:
 *   - ce_mcp_start when a person asks for it, ce_mcp_stop when they stop it.
 *     Nothing starts it otherwise, and nothing remembers that it was started.
 *   - Once per pass of the loop that runs the machine, and on that thread:
 *     if ce_mcp_pending says a call waits, ce_mcp_service. It answers every
 *     waiting call the engine can answer by itself (reading memory, the
 *     picture, the input). For a call only the frontend can run - one that
 *     must go through its own editor, so that the person sees the change and
 *     can undo it - it returns 1 with the call; the frontend runs it and
 *     gives the answer with ce_mcp_answer, at once or later, then calls
 *     ce_mcp_service again for the calls behind it.
 *   - The rest is for a window that shows what is happening: the state, the
 *     name of the agent's program, and the log, one line a call.
 *
 * A tool that changes the movie or the machine is refused unless
 * ce_mcp_allow_changes(1) was called since the last start: a person allows
 * changes, each time, and until then an agent can only look.
 *
 * Before the first change after a start, ce_mcp_service hands the frontend a
 * call named "keep_work" that is not the agent's: the frontend keeps the
 * person's work as it is now (a branch of the movie) and answers with
 * ce_mcp_answer - a few words saying where it was kept, or, as a refusal, why
 * it could not be. Only after a good answer is the agent's change run; after
 * a refusal every change is refused until the next start. */
enum
{
	CE_MCP_STOPPED = 0,
	CE_MCP_WAITING = 1,    /* started; no agent's program is connected */
	CE_MCP_CONNECTED = 2,
};

/* 1 when the server is started (or was already); 0 with *error_out (static
 * per-thread) when the pipe cannot be made, which includes another program of
 * this user already serving. */
CE_API int32_t ce_mcp_start(const char **error_out);
/* Stops the server and ends the connection. A call that was waiting is not
 * run; one the frontend is running may still finish, and its answer is
 * dropped. */
CE_API void ce_mcp_stop(void);
/* A name for this program's pipe, so that it does not meet the pipe of
 * another Chimera of the same user: the second of two that both want to
 * serve, or a test beside a Chimera in use. chimera-mcp is given the same
 * name in the environment variable CHIMERA_MCP_NAME, which this program
 * reads too when no name is set here. NULL or "" for none. Takes effect at
 * the next ce_mcp_start. */
CE_API void ce_mcp_name(const char *name);
CE_API int32_t ce_mcp_state(void);
/* The name and version the agent's program gave, "" when none is connected.
 * Borrowed until this thread's next call of it. */
CE_API const char *ce_mcp_client(void);
/* The pipe's name (Windows) or folder (Linux), for a person to read. */
CE_API const char *ce_mcp_endpoint(void);
CE_API void ce_mcp_allow_changes(int32_t on);
CE_API int32_t ce_mcp_changes_allowed(void);

/* 1 when a call waits to be run. Cheap enough to ask on every pass. */
CE_API int32_t ce_mcp_pending(void);
/* Runs the waiting calls, on the thread that runs the machine. `s` is the
 * open machine and `log` its movie's input, either NULL when there is none.
 * `frame` is the frame the machine is on as the frontend counts it, or -1 to
 * use ce_session_frame. `host_status_json`, when given, is a JSON object of
 * what only the frontend knows (the project's title, whether it is paused);
 * the status tool adds its own facts to it.
 * Returns 0 when nothing waits any more, or 1 with a call for the frontend:
 * *id_out for ce_mcp_answer, the tool's name and its arguments as a JSON
 * object. Both strings are borrowed until this thread's next call. */
CE_API int32_t ce_mcp_service(ce_session *s, const ce_movie_log *log, int64_t frame, const char *host_status_json,
	uint64_t *id_out, const char **tool_out, const char **arguments_out);
/* The frontend's answer to a call ce_mcp_service handed it: text for the
 * agent (JSON by convention), and whether it is a refusal. An id that is not
 * waiting for an answer is ignored. `s` is the open machine as it is now, or
 * NULL: an answer that lands the machine on a frame (seek, advance_until,
 * load_branch) has the values the agent asked to watch read from it and
 * added. */
CE_API void ce_mcp_answer(ce_session *s, uint64_t id, const char *text, int32_t is_error);

/* capture_series (issue 355): the frontend plays the movie to each frame of
 * the plan it was handed (its arguments' "frames") and, on each, has the
 * engine take the picture with this, saying the frame it is on as it counts
 * them; the pictures go with its answer. 1, or 0 with the reason (borrowed
 * until this thread's next call). */
CE_API int32_t ce_mcp_capture(ce_session *s, uint64_t id, int64_t frame, const char **error_out);
/* The folders the frontend finds core packages in, as a JSON array of
 * paths: what list_cores and describe_core read in the window. */
CE_API void ce_mcp_cores_folders(const char *folders_json);

/* advance_until: play forward until a value meets a condition. The frontend
 * does the playing, because the playback position is its own; what the
 * condition means is the engine's.
 * _set: takes the tool's arguments as they came (the value to read, the
 * comparison, what to compare with) and remembers the value as it is now.
 * 1 when the condition is understood and the value can be read; 0 with
 * *error_out (static per-thread) saying why not.
 * _met: asked after each frame played. 1 met, 0 not yet, -1 when no
 * condition is set or the value can no longer be read. */
CE_API int32_t ce_mcp_condition_set(ce_session *s, const char *arguments_json, const char **error_out);
CE_API int32_t ce_mcp_condition_met(ce_session *s);

/* The log: one line a call, and a line for each start, stop, connection and
 * change of what is allowed. _total counts every line since the process
 * began; the last 5000 are kept. _line gives line `index` (0 the first ever
 * written), "" for one no longer kept. Borrowed until this thread's next
 * call of it. */
CE_API uint64_t ce_mcp_log_total(void);
CE_API const char *ce_mcp_log_line(uint64_t index);

/* ---- how fast the machine has been running, on average ---------------------
 *
 * Frames emulated over the time they took, since the last reset (chimera
 * issue 265): the figure for comparing two settings, where the frame counter
 * on screen is a figure for the moment. The frontend tells it of every pass
 * of its loop - the frames that pass emulated, the nanoseconds since the pass
 * before, and what the emulator was doing - and the engine decides what
 * counts: free running does, fast-forward included; a pass made paused,
 * advancing frames by hand, or going back does not, with neither its frames
 * nor its time. _fps is 0 until something has counted. */
typedef struct ce_average_fps ce_average_fps;
CE_API ce_average_fps *ce_average_fps_create(void);
CE_API void ce_average_fps_destroy(ce_average_fps *a);
CE_API void ce_average_fps_reset(ce_average_fps *a);
CE_API void ce_average_fps_pass(ce_average_fps *a, int64_t frames, int64_t nanoseconds, int32_t paused, int32_t stepping, int32_t going_back);
CE_API double ce_average_fps_value(const ce_average_fps *a);

/* ---- how many compile sessions a machine can run at once -------------------
 *
 * A core that recompiles fills its cache in sessions run side by side before
 * a game first runs (docs/compile-cache.md). Each holds about 8 GB, most of
 * it the sandbox's memory, which Windows has to promise before it is used;
 * asked for more than it has, Windows makes its page file bigger on the
 * system drive until that is full (chimera issue 259). So the count is the
 * engine's, from what the machine can really spare.
 * _memory_to_spare: bytes that can be handed out now - on Windows the smaller
 *   of free memory and of what the system can still promise with the page
 *   file at its present size; 0 when the system will not say.
 * _sessions: how many to run for that many processors and that much to
 *   spare: one per session_bytes after 4 GB kept back, at most half the
 *   processors, at most 8, at least 1. session_bytes is the frontend's
 *   setting (Tools > Pre-Compiled Modules); 0 takes the default.
 * _session_bytes: the default, what one session is taken to need (8 GB).
 * _memory_ran_short: 1 when the least that could be spared while sessions
 *   ran (the frontend samples _memory_to_spare) was under half of
 *   session_bytes (0: the default) - they ran out. */
CE_API uint64_t ce_compile_memory_to_spare(void);
CE_API int32_t ce_compile_sessions(int32_t processors, uint64_t to_spare, uint64_t session_bytes);
CE_API uint64_t ce_compile_session_bytes(void);
CE_API int32_t ce_compile_memory_ran_short(uint64_t least_to_spare, uint64_t session_bytes);
/* _session_begins: called by a compile session for itself, first thing. On
 *   Linux it asks the kernel to stop this process before any other when
 *   memory runs out (oom_score_adj 1000), so a compile that wants more than
 *   the computer has loses a session - which the frontend sees and retries
 *   with fewer - and not the window that started it (chimera issue 300: the
 *   kernel stopped Chimera itself). 1 when that was set; 0 elsewhere, where
 *   there is no such choice to make (Windows refuses the memory instead). */
CE_API int32_t ce_compile_session_begins(void);

/* ---- the computer, for a bug report (chimera issue 277) ---------------------
 * _cpu_name: the processor's name as it gives it, for example "AMD Ryzen 7
 *   5800X 8-Core Processor"; "" when it will not say. Static, never
 *   invalidated.
 * _memory_total: the memory installed, in bytes; 0 when the system will not
 *   say. */
CE_API const char *ce_host_cpu_name(void);
CE_API uint64_t ce_host_memory_total(void);

/* ---- Chimera's languages (docs/translations.md) -----------------------------
 * Every text Chimera shows is a key in a language table, one file per
 * language, English among them (owner, 2026-10-28), in the languages folder
 * beside the engine.
 * _count, _code, _name, _english_name: the languages there, English first,
 *   then by code: "de", its name in German ("Deutsch") and in English
 *   ("German"). Read once; never invalidated.
 * _use: Chimera's texts in that language from now on, for every thread; 0
 *   when there is no table for it (the one in use stays).
 * _in_use: its code; per-thread, until the next call on that thread.
 * ce_text: a key's text in the language in use, with %1 to %9 replaced by
 *   args (at most 9) and, for a key with plural forms, the form for count
 *   chosen and its %n replaced by count (count below zero: the "other" form).
 *   A key no table has comes back as the key. Per-thread, until the next
 *   ce_text on that thread. */
CE_API int32_t ce_language_count(void);
CE_API const char *ce_language_code(int32_t index);
CE_API const char *ce_language_name(int32_t index);
CE_API const char *ce_language_english_name(int32_t index);
CE_API int32_t ce_language_use(const char *code);
CE_API const char *ce_language_in_use(void);
CE_API const char *ce_text(const char *key, int64_t count, const char *const *args, int32_t arg_count);

#ifdef __cplusplus
}
#endif

#endif
