/* chimera/engine.h - the engine's C ABI.
 *
 * This header is a frozen spec in the same sense as the waterbox boundary: the
 * C# frontend, jaffarPlus, and anything else that links the engine compiles
 * against exactly this. Rules:
 *
 *   - Additive changes only. Removing or reshaping anything here bumps
 *     CE_ABI_VERSION, and callers check ce_abi_version() at load.
 *   - Bulk data crosses as buffers, never a call per byte.
 *   - A returned const char* is BORROWED unless the declaration says otherwise;
 *     each declaration names the call that invalidates it.
 *   - UTF-8 everywhere. NUL-terminated unless a length is passed alongside.
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
 * Quirk, preserved from the C# it replaces: a "Frame N" line whose N does not
 * parse as a 32-bit integer is a hard error; a text with no "Frame" line at
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

/* ---- movie header ----
 *
 * The Header.txt lump: "Key Value" per line. Parsing keeps the FIRST
 * occurrence of a key (that is what the C# it replaces did); writing keeps
 * insertion order, which stabilises what the old Dictionary-backed writer
 * left unspecified. Values may contain spaces; keys may not.
 */

typedef struct ce_movie_header ce_movie_header;

CE_API ce_movie_header *ce_movie_header_new(void);
CE_API void ce_movie_header_free(ce_movie_header *header);

/* Replaces contents. Whitespace-only lines and lines with no value are
 * skipped; "Key  Value" parses with the separator run eaten, trailing
 * whitespace kept - the exact net48 Split(' ', 2, RemoveEmptyEntries) rules.
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
 * deflated), with a "BizState 1.0" version lump. The engine owns the FORMAT;
 * file I/O stays with the caller - a writer produces the finished archive as
 * one buffer, a reader takes one.
 *
 * Lumps are addressed as the C# always did: by base name, meaning the entry's
 * path up to its first '.' ("Input Log.txt" is "Input Log" - and the version
 * lump "BizState 1.0" is "BizState 1"). zstd needs libzstd beside this
 * library; it is loaded on first use and its absence is a put/get error.
 */

typedef struct ce_state_writer ce_state_writer;

/* compression_level is the config's 0-9: 0 stores, otherwise deflate for
 * plain lumps and zstd level 2n+1 for zstd lumps - the mapping the C# writer
 * used. emu_version fills the BizVersion lump; both version lumps are written
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

/* The sub version from the "BizState 1.0" lump (1.0.N). */
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

/* ---- identity hashing ----
 *
 * The hash the frontend identifies files by: roms, bundle parts, firmware.
 * Writes 40 uppercase hex characters plus a NUL into out41. */
CE_API void ce_sha1_hex(const uint8_t *data, uint64_t len, char *out41);

/* ---- game bundles ----
 *
 * A .gameBundle is a CATALOGUE: a small JSON file naming a rom and per-core
 * attachments sitting beside it, each optionally pinned by SHA1. The engine
 * owns the format, the naming rules, and the bundle's identity (ContentId);
 * the C# object model and the filesystem stay with the caller.
 *
 * One tightening against the old Newtonsoft parser: bundles are now strict
 * JSON - no comments, no trailing commas. Nothing we ever wrote used either.
 */

typedef struct ce_bundle ce_bundle;

/* NULL with *error_out set (static per-thread buffer, valid until the next
 * failed parse on the thread) when the text is not an acceptable bundle:
 * unreadable JSON, a newer format version, no rom, or a part whose file name
 * breaks the naming rules. file_label is used in the error text. */
CE_API ce_bundle *ce_bundle_parse(
	const char *json, uint64_t len, const char *file_label, const char **error_out);

CE_API ce_bundle *ce_bundle_new(void);
CE_API void ce_bundle_free(ce_bundle *b);

/* Accessors return borrowed strings, invalidated by mutation or free;
 * NULL when the field is absent (an unpinned sha1, an unnamed bundle). */
CE_API const char *ce_bundle_name(const ce_bundle *b);
CE_API void ce_bundle_set_name(ce_bundle *b, const char *name);
CE_API const char *ce_bundle_rom_file(const ce_bundle *b);
CE_API const char *ce_bundle_rom_sha1(const ce_bundle *b);
CE_API void ce_bundle_set_rom(ce_bundle *b, const char *file, const char *sha1);
CE_API int64_t ce_bundle_attach_count(const ce_bundle *b);
CE_API const char *ce_bundle_attach_core(const ce_bundle *b, int64_t index);
CE_API const char *ce_bundle_attach_id(const ce_bundle *b, int64_t index);
CE_API const char *ce_bundle_attach_file(const ce_bundle *b, int64_t index);
CE_API const char *ce_bundle_attach_sha1(const ce_bundle *b, int64_t index);
CE_API void ce_bundle_add_attach(ce_bundle *b, const char *core, const char *id, const char *file, const char *sha1);
/* re-pin after rewriting an attachment's file */
CE_API void ce_bundle_set_attach_sha1(ce_bundle *b, int64_t index, const char *sha1);

/* The bundle's identity: a hash over what its parts ARE (rom sha1, then each
 * attachment's core:id:sha1 ordered by core then id, ordinal), so renaming or
 * reformatting the bundle file changes nothing. NULL when any part is
 * unpinned. Borrowed; invalidated by mutation or another call. */
CE_API const char *ce_bundle_content_id(ce_bundle *b);

/* Indented JSON, ready to write to disk. Invalidated by any other call. */
CE_API const char *ce_bundle_serialize(ce_bundle *b, uint64_t *len_out);

/* The naming rule for one part's file field:
 * 0 = fine; 1 = names no file; 2 = absolute (or drive-qualified) path;
 * 3 = escapes the bundle's folder. Pure string logic, so every platform
 * agrees on what a bundle may say. */
CE_API int32_t ce_bundle_check_path(const char *file);

/* ---- firmware ----
 *
 * The frontend's firmware knowledge is entirely "does this file match what
 * the core declared". The declaration and the file live with the caller;
 * the verdict and the movie's canonical firmware line live here.
 */

/* 0 = wrong size, 1 = unrecognised dump (usable), 2 = good.
 * declared_size 0 means the core pinned no size; expected_sha1s is a
 * newline-separated list of acceptable hashes (case-insensitive), "" when
 * the core pinned none - and pinning none means any dump is good. */
CE_API int32_t ce_firmware_state(
	int64_t declared_size, const char *expected_sha1s,
	int64_t actual_size, const char *actual_sha1);

/* The canonical "<id>=<sha1> <id>=<sha1>" line a movie records: pairs arrive
 * newline-separated in any order, leave sorted by id (ordinal). A different
 * BIOS is a different machine; this line is why replays can say so.
 * Borrowed per-thread buffer, valid until the next call on the thread. */
CE_API const char *ce_firmware_record_line(const char *pairs, uint64_t *len_out);

/* ---- core packages ----
 *
 * A core package is a zip (or, for development, a directory) whose root holds
 * core.wbx + waterbox.config - the data-driven form the generic adapter runs -
 * or a minihawk-core.json manifest. The engine owns the container: what makes
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

/* The entry's bytes (decompressed from the zip, or the file in the directory
 * form). NULL when absent or unreadable (see _last_error to tell which;
 * absent is ""). Invalidated by the next entry read on the same package. */
CE_API const uint8_t *ce_package_entry(ce_package *p, const char *name, uint64_t *len_out);

/* "" when the last entry miss was a plain absence.
 * Invalidated by the next call on the same package. */
CE_API const char *ce_package_last_error(ce_package *p);

/* ---- the session ----
 *
 * A running waterboxed machine: a core package's guest, loaded through the
 * miniBox host (libminiboxhost, found beside this library), configured by its
 * own waterbox.config, fed a rom and settings, and driven a frame at a time.
 * This is the same machine the C# WaterboxCore adapter runs - one machine,
 * one behaviour, with or without a frontend.
 *
 * The engine composes the effective settings itself: every declared setting
 * at its config default, overlaid with the caller's overrides JSON (the
 * user's or the movie's sync settings). Buttons map to input bits by their
 * position in the config's buttons array.
 */

typedef struct ce_session ce_session;

/* rom_len 0 with rom NULL is allowed for coreless boots (none exist today).
 * settings_overrides_json may be NULL for pure defaults. firmware arrives as
 * ids and blobs, parallel arrays. NULL with *error_out set (static per-
 * thread, valid until the thread's next failed open) on any failure - an
 * unreadable package, a missing export, a core that refused the rom (its
 * GetLoadError text is the message when it gives one). */
CE_API ce_session *ce_session_open(
	const char *package_path,
	const uint8_t *rom, uint64_t rom_len,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	const char **error_out);

CE_API void ce_session_free(ce_session *s);

/* config-derived facts (borrowed strings live as long as the session) */
CE_API const char *ce_session_core_name(const ce_session *s);
CE_API const char *ce_session_system_id(const ce_session *s);
CE_API int32_t ce_session_width(const ce_session *s);
CE_API int32_t ce_session_height(const ce_session *s);
CE_API int32_t ce_session_virtual_width(const ce_session *s);
CE_API int32_t ce_session_virtual_height(const ce_session *s);
/* post-Init: the guest's own answer when it gives one, else the config's */
CE_API int32_t ce_session_vsync_numerator(const ce_session *s);
CE_API int32_t ce_session_vsync_denominator(const ce_session *s);
CE_API int32_t ce_session_samples_per_frame(const ce_session *s);
CE_API int32_t ce_session_channels(const ce_session *s);
CE_API int32_t ce_session_deterministic(const ce_session *s);
CE_API int64_t ce_session_button_count(const ce_session *s);
CE_API const char *ce_session_button_name(const ce_session *s, int64_t index);
CE_API int64_t ce_session_axis_count(const ce_session *s);
CE_API const char *ce_session_axis_name(const ce_session *s, int64_t index);

/* Axes are set per frame, before the advance they belong to. */
CE_API void ce_session_set_axis(ce_session *s, int32_t index, int32_t value);

/* One frame: buttons is the bitmask (bit i = the config's buttons[i]).
 * render 0 skips the video copy. Returns nonzero when the frame was a lag
 * frame (the guest never read input), per the config's lag export. */
CE_API int32_t ce_session_frame_advance(ce_session *s, uint64_t buttons, int32_t render);

/* The last rendered frame, BGRA, width*height pixels.
 * Invalidated by the next rendered frame_advance. */
CE_API const uint32_t *ce_session_video(const ce_session *s);

/* The last frame's audio as interleaved stereo s16 (mono sources are
 * doubled), and its sample-pair count. Invalidated by the next advance. */
CE_API const int16_t *ce_session_audio(const ce_session *s, int32_t *sample_count);

/* The guest's whole-machine state. The buffer is the session's, reused and
 * invalidated by the next save. Load returns 0 on success. */
CE_API const uint8_t *ce_session_save_state(ce_session *s, uint64_t *len_out);
CE_API int32_t ce_session_load_state(ce_session *s, const uint8_t *data, uint64_t len);

/* The guest's self-described memory domains. */
CE_API int32_t ce_session_domain_count(const ce_session *s);
CE_API const char *ce_session_domain_name(const ce_session *s, int32_t index);
CE_API int64_t ce_session_domain_size(const ce_session *s, int32_t index);
CE_API int32_t ce_session_domain_writable(const ce_session *s, int32_t index);
/* Copies out [offset, offset+len); returns bytes copied (clamped at end). */
CE_API int64_t ce_session_domain_read(const ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len);

/* "" when no error. Invalidated by the next call on the same session. */
CE_API const char *ce_session_last_error(ce_session *s);

/* What built the waterbox host this engine loads (its wbx_build_info JSON):
 * the frontend shows it and movies record it. NULL when the host is not
 * loadable. Static string, never invalidated. */
CE_API const char *ce_host_build_info(void);

/* The raw guest pointer behind a memory domain, for pointer-backed peek/poke
 * (the hex editor's path). Stable for the session's lifetime - the guest is
 * non-PIE - and 0 when the domain has no linear backing. */
CE_API uint64_t ce_session_domain_ptr(const ce_session *s, int32_t index);

/* Re-composes the effective settings (declared defaults overlaid with
 * overrides_json) and hands them to the RUNNING guest through its live-
 * settings exports. 0 = applied; 1 = the core has no live-settings group
 * (the caller must reboot instead); 2 = error (see _last_error). */
CE_API int32_t ce_session_apply_settings(ce_session *s, const char *overrides_json);

/* ---- the optional guest ABI groups ----
 *
 * A core may export any subset of four independent tooling groups (surfaces,
 * registers, buses, trace) plus the persistent-data channel. The session
 * probes them once, post-Init (names and counts may depend on the rom and
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
CE_API void ce_session_bus_poke(ce_session *s, int32_t index, int32_t addr, int32_t value);

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

/* persistent data: what the machine keeps when switched off. Only the core
 * knows what belongs in the file, so it serializes into a buffer it owns and
 * the session moves bytes it does not interpret. */
CE_API int32_t ce_session_persist_available(const ce_session *s); // exported AND nonempty for THIS rom
CE_API const char *ce_session_persist_name(const ce_session *s);
CE_API const char *ce_session_persist_id(const ce_session *s);
/* Borrowed until the next call; NULL when the core has nothing right now. */
CE_API const uint8_t *ce_session_persist_get(ce_session *s, uint64_t *len_out);
/* 0 = accepted; 1 = the core refused it (the file does not belong to this
 * game - loading half a save silently would be worse); 2 = no room. */
CE_API int32_t ce_session_persist_put(ce_session *s, const uint8_t *data, uint64_t len);

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
 * through this. Invalidated when the session is freed. */
CE_API const ce_movie_log *ce_session_movie_log(const ce_session *s);

/* One frame under the movie. In play mode the input comes from the log
 * (buttons/axes are ignored) until the log runs out, which flips the mode to
 * finished; in record and finished modes the input is the caller's, and
 * record appends the generated entry. axes may be NULL for all-neutral;
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
/* The nearest stored frame at or before frame; -1 when none is. */
CE_API int64_t ce_session_greenzone_nearest(const ce_session *s, int64_t frame);
/* Drops stored states AFTER frame - an input edit at frame N makes every
 * later state a lie, while the state at N itself (inputs 0..N-1) still holds. */
CE_API void ce_session_greenzone_invalidate(ce_session *s, int64_t after_frame);
/* Restores the nearest stored state at or before frame, then replays the
 * movie to frame. Needs the movie's entries up to frame. 0 on success. */
CE_API int32_t ce_session_seek(ce_session *s, int64_t frame);

#ifdef __cplusplus
}
#endif

#endif
