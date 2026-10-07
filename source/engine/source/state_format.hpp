/* state_format.hpp - one number for the shape of a machine state, and the
 * layout that number stands for.
 *
 * Issue #115, split out of #111: a savestate problem there turned out to be a
 * frontend and a core from different weeks. The state layout genuinely changed
 * three times in the week of 2026-09-14 - the memory filesystem, the sandbox's
 * delta format, and greenzone packing - and nothing in a state said so. What a
 * person saw was "core stopped", or a state that loaded into nonsense, or a
 * corrupt-file error naming a file that was not corrupt.
 *
 * So every state this engine writes carries CE_STATE_FORMAT, and every state it
 * reads is checked against it BEFORE a byte reaches the machine. The number is
 * the exact answer; the build-date comparison next to it (ce_version_skew) is a
 * guess for humans.
 *
 * THE RULE: bump kStateFormat whenever ANYTHING in kStateLayout changes - this
 * engine's own framing, the greenzone history's, or the sandbox's. Forgetting is
 * meant to be hard: test_state_format hashes kStateLayout and compares it with
 * kStateLayoutDigest, so a layout change that leaves the number alone fails the
 * engine gate. The magic strings below are the ones the writers actually write
 * (they are defined here and used there), so renaming one moves the digest on
 * its own; the field orders are spelled out in prose, which a layout change has
 * to be honest enough to edit.
 *
 * What the number does NOT catch: a sandbox change that alters the bytes without
 * renaming any of its magics. miniBox's layout is miniBox's, and the engine
 * cannot hash what it never sees. That case is a hand bump, and it is exactly
 * why the date heuristic exists beside this.
 */
#ifndef CHIMERA_STATE_FORMAT_HPP
#define CHIMERA_STATE_FORMAT_HPP

#include <cstdint>
#include <string>
#include <vector>

/* ---- the magics, defined once, written by the writers, hashed by the test ---- */

/* A state kept in a file (ce_session_state_save_file): a TAStudio branch's
 * state, and anything else too big to cross the managed boundary whole. */
#define CE_STATE_FILE_MAGIC "CESTATE2"
/* The same file before it carried a format number. Read, never written. */
#define CE_STATE_FILE_MAGIC_V1 "CESTATE1"

/* The greenzone history (state_history.cpp). */
#define CE_HISTORY_MAGIC "ChimeraHistory5"
#define CE_HISTORY_MAGIC_V4 "ChimeraHistory4"
#define CE_HISTORY_MAGIC_V3 "ChimeraHistory3"

/* The sandbox's own framing, which the engine passes through and never parses.
 * Listed so that renaming one of them moves this engine's layout digest: a
 * miniBox that renames its format has changed it. */
#define CE_SANDBOX_SAVE_MAGIC "ActivatedWaterboxHost_v1"
#define CE_SANDBOX_DELTA_MAGIC "MiniBoxHostDelta_v1"
#define CE_SANDBOX_BLOCK_DELTA_MAGIC "MiniBoxDelta1"
#define CE_SANDBOX_BLOCK_MAGIC "ActivatedMemoryBlock"

namespace chimera
{

/* The shape of a machine state as THIS build writes and reads it.
 *
 * 1: the first numbered format. Everything written before this existed is read
 *    as a 1, because 1 is what the layout was on the day the number arrived -
 *    so nothing in the wild is refused today, and the first real bump is what
 *    starts refusing the states that came before it. */
constexpr uint32_t kStateFormat = 1;

/* Two builds far enough apart that their pairing is worth a word. The states in
 * #111 came from builds a week apart; a fortnight is loose enough not to nag at
 * somebody who updates one side on a Friday and the other on a Monday. */
constexpr int kVersionSkewDays = 14;

/* Everything kStateFormat stands for. The test hashes this. */
constexpr char kStateLayout[] =
	"chimera state layout\n"
	"state file: \"" CE_STATE_FILE_MAGIC "\" u32 format, u32 writerLen, writer,"
	" u8 compressed, u32 tagLen, tag, then the machine (zstd when compressed) to EOF\n"
	"state file, read only: \"" CE_STATE_FILE_MAGIC_V1 "\" u8 compressed, u32 tagLen, tag,"
	" then the machine; taken as format 1\n"
	"history: \"" CE_HISTORY_MAGIC "\" u64 idLen, id, u64 segCount,"
	" per segment u64 anchorFrame, body, u64 noteLen, note, u64 linkCount,"
	" per link u64 endFrame, u64 noteLen, note, body;"
	" body = u64 rawLen, u64 heldLen, bytes (held < raw means a zstd frame)\n"
	"history, read only: \"" CE_HISTORY_MAGIC_V4 "\" one zstd stream behind the magic;"
	" \"" CE_HISTORY_MAGIC_V3 "\" raw, body = u64 len, bytes\n"
	"history id: <caller's machine id>@<32-byte machine hash, lowercase hex>#f<format>\n"
	"history note cap: 4096 bytes\n"
	"sandbox framing (miniBox's, passed through): \"" CE_SANDBOX_SAVE_MAGIC "\","
	" \"" CE_SANDBOX_DELTA_MAGIC "\", \"" CE_SANDBOX_BLOCK_DELTA_MAGIC "\","
	" \"" CE_SANDBOX_BLOCK_MAGIC "\"\n";

/* SHA1 of kStateLayout, recorded the last time kStateFormat was set. When the
 * engine gate says this no longer matches, the layout moved: bump kStateFormat
 * and paste the digest the test printed. */
constexpr char kStateLayoutDigest[] = "A3F1EF44F9D1B704F1D25AF0F10C196552DE5895";

/* ---- the state file's header ---- */

struct StateFileHeader
{
	uint32_t format = kStateFormat;
	std::string writer;    /* the build that wrote it; "" when it did not say */
	bool compressed = false;
	std::vector<uint8_t> tag;
	size_t length = 0;     /* bytes of file the header occupies */
};

/* Appends a header for `h` to `out`. */
void writeStateFileHeader(const StateFileHeader &h, std::vector<uint8_t> &out);

/* Reads one out of `data`. Returns
 *   0 read, and this build can load what follows;
 *   1 a state file, but of another format - `why` names both numbers and the
 *     build that wrote it, and nothing may be fed to a machine;
 *   2 not a state file at all (or cut short) - `why` says so.
 * A short read is 2, never 1: a file that is not one cannot disagree about its
 * format. */
int readStateFileHeader(const uint8_t *data, size_t len, StateFileHeader &h, std::string &why);

/* The build that wrote a state this engine writes. The engine and the frontend
 * come out of one repository at one commit, so naming the engine's commit names
 * the frontend that wrote it too. */
const char *stateWriterId();

/* The greenzone history's id for this format: a history written under another
 * one is another machine's as far as a load is concerned. */
std::string historyFormatSuffix();

/* "Chimera was built on X, the core on Y, and that is N days apart" - or "" when
 * they are close enough, or when either side does not say. Dates are
 * YYYY-MM-DD; anything else is "does not say". */
std::string versionSkewMessage(
	const std::string &frontendDate, const std::string &frontendBuild,
	const std::string &coreDate, const std::string &coreBuild, const std::string &coreName);

} // namespace chimera

#endif
