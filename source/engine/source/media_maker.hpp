/* media_maker.hpp - a folder turned into ONE file, reproducibly.
 *
 * WHY THIS EXISTS. A chimera project stores names and SHA1s and never paths
 * (docs/project.md), and miniBox mounts files whose hashes bind a savestate
 * (MACHINE-SPEC.md). Both of those are the same promise: anybody holding files
 * matching the SHA1s can replay the movie. A game that was dumped as a FOLDER
 * cannot make that promise - a directory has no SHA1 - so it has to become a
 * file first, and the file has to be the same file for everyone who packs it.
 *
 * REPRODUCIBLE means the bytes depend on the CONTENTS and the NAMES, and on
 * nothing else: not on when the pack was run, not on the modification times the
 * files happen to carry, not on the mode bits a copy off a FAT drive left
 * behind, not on the order a directory listing came back in. Two people with
 * the same dump get the same SHA1 or this is worth nothing.
 *
 * Timestamps, permissions and ordering are each a way for the machine to leak
 * into the output, and each is closed deliberately: a fixed date, fixed modes,
 * and a byte-wise sort that does not consult the locale.
 */

#ifndef CHIMERA_MEDIA_MAKER_HPP
#define CHIMERA_MEDIA_MAKER_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chimera {

enum class MediaFormat
{
	ZipStored, /* one file, no compression: read by seeking, like loose files */
	Iso9660,   /* ISO 9660 + Joliet, for the cores that want a disc */
	Fat12,     /* a 1.44 MB floppy image */
};

/* One file to pack. `rel` is '/'-separated and never starts with one. */
struct MediaEntry
{
	std::string rel;
	std::string abs;
	uint64_t size = 0;
};

/* WHAT A CORE SAYS ITS MEDIA NEEDS beyond the files (one object of the "media"
 * array in its package's waterbox.config).
 *
 * The packer is nobody's in particular: it writes the files and nothing a
 * machine would recognise. One emulator does look further - it wants a table
 * in the ISO system area, the sixteen sectors ISO 9660 reserves and gives no
 * meaning - and that used to be written here, for any folder that held a file
 * of a certain name. It is the core's knowledge, so the core declares it: when
 * the recipe is its (a file at the folder's root), and what goes where.
 *
 * The bytes still depend on the folder's contents and names and on nothing
 * else, PROVIDED everybody packs with the same recipe - so a recipe is a
 * format, and a core must never change one it has published. */
struct MediaPatch
{
	enum class Value
	{
		Literal,    /* the number written in the recipe */
		LastSector, /* the image's last sector (its count less one) */
		Sectors,    /* how many sectors the image has */
	};
	uint32_t at = 0;          /* byte offset into the system area */
	bool bigEndian = true;
	Value value = Value::Literal;
	uint32_t literal = 0;
};

struct MediaRecipe
{
	MediaFormat format = MediaFormat::Iso9660;
	/* the recipe is meant for a folder with this file at its root (ASCII case
	 * ignored); empty: for any folder */
	std::string rootFile;
	std::vector<MediaPatch> systemArea;
};

/* Reads a recipe as the package wrote it. False with `error` on anything it
 * does not understand: a recipe half applied is another image. */
bool mediaParseRecipe(const char *json, MediaRecipe &out, std::string &error);

/* Whether `folder` is what the recipe was written for. */
bool mediaRecipeApplies(const MediaRecipe &recipe, const std::string &folder);

/* Called as the pack runs, often. Return false to cancel, which leaves no
 * output file behind - a half-written image that looks like an image is worse
 * than none. */
using MediaProgress = std::function<bool(const char *file, uint64_t bytesDone, uint64_t bytesTotal,
	uint64_t filesDone, uint64_t filesTotal)>;

/* Every regular file under `folder`, sorted the one way that does not depend on
 * the machine: by the bytes of the relative path. Symlinks and specials are
 * skipped rather than followed - what they point at is not in the folder, and
 * following one makes the output depend on something outside it. */
bool mediaCollect(const std::string &folder, std::vector<MediaEntry> &out, std::string &error);

/* Packs `folder` into `outPath`. `sha1Out` receives the digest of what was
 * written, taken as it was written. Returns false on failure or cancel, with
 * `error` set ("cancelled" when it was cancelled). */
bool mediaMake(const std::string &folder, const std::string &outPath, MediaFormat format,
	const MediaProgress &progress, std::string &sha1Out, std::string &error,
	const MediaRecipe *recipe = nullptr);

/* The writers, one per translation unit. Each takes the collected files already
 * sorted, and each is responsible for writing nothing at all when it fails: a
 * half-written image that still looks like an image is worse than no image. */
bool mediaWriteZipStored(const std::vector<MediaEntry> &files, const std::string &outPath,
	const MediaProgress &progress, std::string &sha1Out, std::string &error);
bool mediaWriteIso9660(const std::vector<MediaEntry> &files, const std::string &outPath,
	const MediaProgress &progress, std::string &sha1Out, std::string &error,
	const MediaRecipe *recipe = nullptr);
bool mediaWriteFat12(const std::vector<MediaEntry> &files, const std::string &outPath,
	const MediaProgress &progress, std::string &sha1Out, std::string &error);

} // namespace chimera

#endif
