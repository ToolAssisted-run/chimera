#pragma once
/* Finding the firmware a machine needs, as Chimera always has: which of a
 * package's declared files a project calls for is the engine's to say
 * (ce_firmware_evaluate); each is then looked for in the firmware folder - by
 * its hash when one is pinned (the project's pin first, else the package's),
 * which is the only thing that counts then; without a hash by the name the
 * package gives it, and failing that by the one file whose size and name the
 * package's hints point at - never one of several (chimera issue 349). */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "chimera/vm/json.hpp"

namespace chimera::vm {

struct FirmwareFile
{
	std::string path, name;
	int64_t size = 0;
	/* hashed only when a hash is asked of it (a firmware folder can hold
	 * gigabytes, and a file the wrong size is never asked) */
	mutable std::string sha1;
	const std::string &hash() const;
};

/* The size from which a file's hash is read away from the window (issue 611):
 * under it, the read is a blink. */
constexpr int64_t kLargeFile = 16ll << 20;

/* While one of these lives on a thread, FirmwareFile::hash() does not read a
 * file of kLargeFile or more whose hash is not yet known: it notes the path
 * (paths()) and answers a text beginning "?" that matches nothing, and
 * neededFirmware() leaves a file it could only answer so out. The caller reads
 * the noted files away from the window and asks again when they are known. */
class DeferLargeHashes
{
public:
	DeferLargeHashes();
	~DeferLargeHashes();
	DeferLargeHashes(const DeferLargeHashes &) = delete;
	DeferLargeHashes &operator=(const DeferLargeHashes &) = delete;
	const std::vector<std::string> &paths() const { return paths_; }
	void note(const std::string &path) { paths_.push_back(path); }

private:
	std::vector<std::string> paths_;
	DeferLargeHashes *outer_;
};

/* every file in these folders (and below, unless recurse is false), names
 * and sizes, nothing read, at most `most` of them; a path that is a file is
 * that file */
std::vector<FirmwareFile> indexFirmware(const std::vector<std::string> &folders, bool recurse = true, size_t most = SIZE_MAX);

/* the file that answers one declared entry, or nullptr; `how` says by what.
 * A declared size rules out every file of another size before any is read. */
const FirmwareFile *findFirmware(const json::Json &decl, const std::string &pinnedSha1,
	const std::vector<FirmwareFile> &index, std::string &how);
/* the different files a declaration's size and name hints point at (one dump
 * in two places is one), for one that pins no hash: two or more, and the
 * person chooses (issue 349) */
std::vector<const FirmwareFile *> hintCandidates(const json::Json &decl, const std::vector<FirmwareFile> &index);

struct FirmwareNeed
{
	std::string id, display;
	/* which entry of the package's "firmware" array this is: entries may
	 * share an id and differ in when they are needed (one dump per version
	 * of a program a setting picks) */
	size_t declIndex = 0;
	/* the file found, empty when none, and its hash */
	std::string path, how, sha1;
	/* false: the package says the core starts without it (an optional
	 * identity file), so none found is no reason to refuse, as Chimera's */
	bool required = true;
};

/* What a machine with this package, these game files and these settings
 * needs, each with the file found for it. firmwareDecl: the package's
 * "firmware" array; settingsDecl its "settings" array (their defaults fill
 * what `settings` leaves out); pins: a project's "firmware" array of
 * {"id", "sha1"}, or empty. */
std::vector<FirmwareNeed> neededFirmware(const json::Json &firmwareDecl, const json::Json &settingsDecl,
	const std::string &slotsJson, const json::Json &settings, const json::Json &pins,
	const std::vector<FirmwareFile> &index);

} // namespace chimera::vm
