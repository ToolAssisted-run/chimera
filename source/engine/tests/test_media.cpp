/* test_media.cpp - the reproducible packer: that the same CONTENT packs to the
 * same bytes whatever the machine says about it, that what comes out holds what
 * went in, and that a cancelled pack leaves nothing behind.
 *
 * The first of those is the whole point. A project stores names and SHA1s, so
 * two people with the same dump have to produce the same file or the movie one
 * of them records cannot be verified by the other. Timestamps, permissions and
 * directory order are the three ways the machine leaks into the output, and
 * each gets a test here rather than a comment saying it was thought about.
 */

#include "../source/media_maker.hpp"

#include "../../extern/miniz/miniz.h"

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <csignal>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/stat.h>
#include <utime.h>
#endif

namespace fs = std::filesystem;

static fs::path workRoot()
{
	return fs::path("work.test_media");
}

static void put(const fs::path &p, const std::string &bytes)
{
	fs::create_directories(p.parent_path());
	FILE *f = std::fopen(p.string().c_str(), "wb");
	assert(f != nullptr);
	if (!bytes.empty()) std::fwrite(bytes.data(), 1, bytes.size(), f);
	std::fclose(f);
}

/* A small tree with nesting, a big-ish file, and names whose order differs
 * between a byte-wise sort and most locale collations. */
static void makeTree(const fs::path &root)
{
	fs::remove_all(root);
	put(root / "PS3_DISC.SFB", "SFB payload");
	put(root / "PS3_GAME" / "PARAM.SFO", "param");
	put(root / "PS3_GAME" / "USRDIR" / "EBOOT.BIN", std::string(300000, 'E'));
	put(root / "PS3_GAME" / "USRDIR" / "data.bin", std::string(1024, '\x7f'));
	put(root / "Z_last.txt", "z");
	put(root / "a_first.txt", "a");
}

/* Everything about the tree that is NOT its content: what must not reach the
 * output. */
static void disturbMetadata(const fs::path &root)
{
#if !defined(_WIN32)
	for (const auto &e : fs::recursive_directory_iterator(root))
	{
		::chmod(e.path().string().c_str(), fs::is_directory(e.path()) ? 0700 : 0600);
		struct utimbuf t;
		t.actime = 1234567890;
		t.modtime = 1400000000;
		::utime(e.path().string().c_str(), &t);
	}
	::chmod(root.string().c_str(), 0700);
#else
	(void)root;
#endif
}

static std::string packOk(const fs::path &tree, const fs::path &out)
{
	std::string sha, err;
	const bool ok = chimera::mediaMake(tree.string(), out.string(), chimera::MediaFormat::ZipStored,
		nullptr, sha, err);
	if (!ok) std::fprintf(stderr, "pack failed: %s\n", err.c_str());
	assert(ok);
	assert(sha.size() == 40);
	return sha;
}

static void theSameContentPacksToTheSameBytes()
{
	const fs::path a = workRoot() / "a";
	const fs::path b = workRoot() / "b";
	makeTree(a);
	fs::remove_all(b);
	fs::copy(a, b, fs::copy_options::recursive);
	disturbMetadata(b); /* same files, different dates and modes */

	const std::string sa = packOk(a, workRoot() / "a.zip");
	const std::string sb = packOk(b, workRoot() / "b.zip");
	assert(sa == sb);

	/* and again from the same tree, later: no wall clock in the output */
	const std::string sa2 = packOk(a, workRoot() / "a2.zip");
	assert(sa == sa2);
}

static void whatComesOutIsWhatWentIn()
{
	const fs::path a = workRoot() / "a";
	const fs::path zip = workRoot() / "a.zip";
	mz_zip_archive z{};
	assert(mz_zip_reader_init_file(&z, zip.string().c_str(), 0) == MZ_TRUE);

	const mz_uint n = mz_zip_reader_get_num_files(&z);
	assert(n == 6);

	/* names, in the order they were written: byte-wise sorted */
	std::vector<std::string> names;
	for (mz_uint i = 0; i < n; i++)
	{
		mz_zip_archive_file_stat st{};
		assert(mz_zip_reader_file_stat(&z, i, &st) == MZ_TRUE);
		names.push_back(st.m_filename);
		/* STORED, so a core can seek in it rather than decompress */
		assert(st.m_method == 0);
	}
	for (size_t i = 1; i < names.size(); i++) assert(names[i - 1] < names[i]);

	size_t sz = 0;
	void *p = mz_zip_reader_extract_file_to_heap(&z, "PS3_GAME/USRDIR/EBOOT.BIN", &sz, 0);
	assert(p != nullptr);
	assert(sz == 300000);
	assert(std::memcmp(p, std::string(300000, 'E').data(), sz) == 0);
	mz_free(p);

	p = mz_zip_reader_extract_file_to_heap(&z, "PS3_DISC.SFB", &sz, 0);
	assert(p != nullptr && sz == 11 && std::memcmp(p, "SFB payload", 11) == 0);
	mz_free(p);

	mz_zip_reader_end(&z);
	(void)a;
}

static void aCancelledPackLeavesNothing()
{
	const fs::path a = workRoot() / "a";
	const fs::path out = workRoot() / "cancelled.zip";
	fs::remove(out);
	std::string sha, err;
	int calls = 0;
	const bool ok = chimera::mediaMake(a.string(), out.string(), chimera::MediaFormat::ZipStored,
		[&](const char *, uint64_t, uint64_t, uint64_t, uint64_t) {
			calls++;
			return false; /* stop at the first chance */
		},
		sha, err);
	assert(!ok);
	assert(calls == 1);
	assert(err == "cancelled");
	/* a half-written image that looks like an image is worse than none */
	assert(!fs::exists(out));
}

static void progressCountsEveryByteAndFile()
{
	const fs::path a = workRoot() / "a";
	uint64_t lastBytes = 0, lastTotal = 0, lastFiles = 0, totalFiles = 0;
	std::string sha, err;
	const bool ok = chimera::mediaMake(a.string(), (workRoot() / "p.zip").string(),
		chimera::MediaFormat::ZipStored,
		[&](const char *name, uint64_t done, uint64_t total, uint64_t files, uint64_t nfiles) {
			assert(name != nullptr && *name != '\0');
			assert(done >= lastBytes); /* never goes backwards */
			lastBytes = done;
			lastTotal = total;
			lastFiles = files;
			totalFiles = nfiles;
			return true;
		},
		sha, err);
	assert(ok);
	assert(totalFiles == 6);
	assert(lastFiles == 6);
	assert(lastBytes == lastTotal);
	assert(lastTotal == 11 + 5 + 300000 + 1024 + 1 + 1);
}

static void anEmptyFolderIsRefused()
{
	const fs::path empty = workRoot() / "empty";
	fs::remove_all(empty);
	fs::create_directories(empty);
	std::string sha, err;
	assert(!chimera::mediaMake(empty.string(), (workRoot() / "empty.zip").string(),
		chimera::MediaFormat::ZipStored, nullptr, sha, err));
	assert(err.find("no files") != std::string::npos);
}

static void collectSkipsWhatIsNotAFile()
{
	const fs::path a = workRoot() / "a";
	std::vector<chimera::MediaEntry> files;
	std::string err;
	assert(chimera::mediaCollect(a.string(), files, err));
	assert(files.size() == 6);
	for (const auto &e : files)
	{
		assert(!e.rel.empty());
		assert(e.rel.front() != '/');
		assert(e.rel.find('\\') == std::string::npos); /* '/' everywhere, even on Windows */
	}
}

/* ---- ISO ---- */

static std::vector<uint8_t> slurp(const fs::path &p)
{
	FILE *f = std::fopen(p.string().c_str(), "rb");
	assert(f != nullptr);
	std::fseek(f, 0, SEEK_END);
	const long n = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> v(static_cast<size_t>(n));
	if (n) assert(std::fread(v.data(), 1, v.size(), f) == v.size());
	std::fclose(f);
	return v;
}

static constexpr uint16_t kFixedFatDate = (0 << 9) | (1 << 5) | 1;

static uint32_t rd32le(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

/* Walks the JOLIET tree, which is the one a reader prefers and the one rpcs3
 * decodes, and collects every file as path -> bytes. Small on purpose: enough
 * to prove the image says what the folder said, without a library. */
static void readIsoJoliet(const std::vector<uint8_t> &iso, uint32_t lba, uint32_t len,
	const std::string &prefix, std::map<std::string, std::vector<uint8_t>> &out)
{
	const uint8_t *dir = iso.data() + size_t(lba) * 2048;
	uint32_t at = 0;
	while (at < len)
	{
		const uint8_t *r = dir + at;
		if (r[0] == 0)
		{
			/* padding to the end of a sector: jump to the next one */
			const uint32_t next = ((at / 2048) + 1) * 2048;
			if (next >= len) break;
			at = next;
			continue;
		}
		const uint32_t recLen = r[0];
		const uint32_t childLba = rd32le(r + 2);
		const uint32_t childLen = rd32le(r + 10);
		const bool isDir = (r[25] & 0x02) != 0;
		const uint32_t idLen = r[32];
		std::string name;
		for (uint32_t i = 0; i + 1 < idLen; i += 2)
			name.push_back(static_cast<char>(r[33 + i + 1])); /* UCS-2BE, ASCII here */
		at += recLen;
		if (idLen == 1 && (r[33] == 0 || r[33] == 1)) continue; /* . and .. */
		if (isDir) readIsoJoliet(iso, childLba, childLen, prefix + name + "/", out);
		else
		{
			const uint8_t *data = iso.data() + size_t(childLba) * 2048;
			out[prefix + name] = std::vector<uint8_t>(data, data + childLen);
		}
	}
}

static void anIsoIsReproducibleAndHoldsWhatWentIn()
{
	const fs::path a = workRoot() / "a";
	const fs::path b = workRoot() / "b";
	const fs::path ia = workRoot() / "a.iso";
	const fs::path ib = workRoot() / "b.iso";

	std::string sa, sb, err;
	assert(chimera::mediaMake(a.string(), ia.string(), chimera::MediaFormat::Iso9660, nullptr, sa, err));
	assert(chimera::mediaMake(b.string(), ib.string(), chimera::MediaFormat::Iso9660, nullptr, sb, err));
	assert(sa == sb); /* same content, different dates and modes */

	const std::vector<uint8_t> iso = slurp(ia);
	/* the descriptors rpcs3's loader scans for: PVD, Joliet SVD, terminator */
	assert(iso[16 * 2048] == 1 && std::memcmp(&iso[16 * 2048 + 1], "CD001", 5) == 0);
	assert(iso[17 * 2048] == 2 && std::memcmp(&iso[17 * 2048 + 1], "CD001", 5) == 0);
	assert(iso[18 * 2048] == 255);
	/* and the Joliet escape sequence that says the names are UCS-2 */
	assert(std::memcmp(&iso[17 * 2048 + 88], "%/E", 3) == 0);

	/* the root directory record lives in the descriptor, at offset 156 */
	const uint8_t *rootRec = &iso[17 * 2048 + 156];
	std::map<std::string, std::vector<uint8_t>> got;
	readIsoJoliet(iso, rd32le(rootRec + 2), rd32le(rootRec + 10), "", got);

	assert(got.size() == 6);
	assert(got.count("PS3_GAME/USRDIR/EBOOT.BIN") == 1);
	assert(got["PS3_GAME/USRDIR/EBOOT.BIN"] == std::vector<uint8_t>(300000, 'E'));
	assert(got["PS3_DISC.SFB"] == std::vector<uint8_t>({ 'S', 'F', 'B', ' ', 'p', 'a', 'y', 'l', 'o', 'a', 'd' }));
	/* the real names survive, case and all, which is what Joliet is for */
	assert(got.count("a_first.txt") == 1 && got.count("Z_last.txt") == 1);
}

/* What a core declares its media needs, written where it says. The first
 * recipe there was is the test's: an emulator that reads a big-endian count
 * out of the first four bytes of the system area and refuses a count below 1,
 * with a last sector at byte 12. The packer knew that itself once, for any
 * folder with a certain file at its root; now it is told. */
static const char *kRecipe =
	"{ \"id\": \"disc\", \"label\": \"a disc\", \"format\": \"iso9660\","
	"  \"when\": { \"rootFile\": \"ps3_disc.sfb\" },"
	"  \"systemArea\": [ { \"at\": 0, \"u32be\": 1 }, { \"at\": 12, \"u32be\": \"lastSector\" },"
	"                    { \"at\": 32, \"u32le\": \"sectors\" } ] }";

static void aRecipeWritesTheSystemArea()
{
	const fs::path a = workRoot() / "a"; /* PS3_DISC.SFB at its root */
	const fs::path iso = workRoot() / "recipe.iso";
	std::string sha, err;
	chimera::MediaRecipe recipe;
	assert(chimera::mediaParseRecipe(kRecipe, recipe, err));
	assert(recipe.format == chimera::MediaFormat::Iso9660 && recipe.systemArea.size() == 3);
	/* the file's name is matched whatever its case */
	assert(chimera::mediaRecipeApplies(recipe, a.string()));
	assert(chimera::mediaMake(a.string(), iso.string(), recipe.format, nullptr, sha, err, &recipe));

	const std::vector<uint8_t> img = slurp(iso);
	const auto be32 = [&](size_t at) {
		return (uint32_t(img[at]) << 24) | (uint32_t(img[at + 1]) << 16)
			| (uint32_t(img[at + 2]) << 8) | uint32_t(img[at + 3]);
	};
	const auto le32 = [&](size_t at) {
		return uint32_t(img[at]) | (uint32_t(img[at + 1]) << 8)
			| (uint32_t(img[at + 2]) << 16) | (uint32_t(img[at + 3]) << 24);
	};
	assert(be32(0) == 1);
	assert(be32(12) == (img.size() / 2048) - 1);
	assert(le32(32) == img.size() / 2048);

	/* WITHOUT a recipe the same folder is its files and a system area of
	 * zeros: the packer recognises no machine by a file's name any more */
	const fs::path bare = workRoot() / "bare.iso";
	std::string bareSha;
	assert(chimera::mediaMake(a.string(), bare.string(), chimera::MediaFormat::Iso9660, nullptr, bareSha, err));
	const std::vector<uint8_t> other = slurp(bare);
	for (size_t i = 0; i < 16 * 2048; i++) assert(other[i] == 0);
	assert(bareSha != sha);
	/* and past the system area the two are the same image */
	assert(other.size() == img.size());
	assert(std::memcmp(other.data() + 16 * 2048, img.data() + 16 * 2048, img.size() - 16 * 2048) == 0);

	/* a folder the recipe was not written for */
	const fs::path plain = workRoot() / "plain";
	fs::remove_all(plain);
	put(plain / "readme.txt", "hello");
	assert(!chimera::mediaRecipeApplies(recipe, plain.string()));
}

/* A recipe that is not understood is refused whole: half of one is another
 * image, with another SHA1, that nobody else can make. */
static void aRecipeNotUnderstoodIsRefused()
{
	chimera::MediaRecipe recipe;
	std::string err;
	assert(!chimera::mediaParseRecipe("not json", recipe, err));
	assert(!chimera::mediaParseRecipe("{ \"format\": \"udf\" }", recipe, err));
	assert(err.find("udf") != std::string::npos);
	/* a value past the system area's end */
	assert(!chimera::mediaParseRecipe(
		"{ \"format\": \"iso9660\", \"systemArea\": [ { \"at\": 32765, \"u32be\": 1 } ] }", recipe, err));
	/* a word the writer does not know */
	assert(!chimera::mediaParseRecipe(
		"{ \"format\": \"iso9660\", \"systemArea\": [ { \"at\": 0, \"u32be\": \"firstSector\" } ] }", recipe, err));
	/* a system area on something that has none */
	assert(!chimera::mediaParseRecipe(
		"{ \"format\": \"zip\", \"systemArea\": [ { \"at\": 0, \"u32be\": 1 } ] }", recipe, err));
	/* and one with nothing wrong, for any folder */
	assert(chimera::mediaParseRecipe("{ \"format\": \"iso9660\" }", recipe, err));
	assert(chimera::mediaRecipeApplies(recipe, workRoot().string()));
}

/* ---- FAT12 ---- */

static void aFloppyIsReproducibleAndReadable()
{
	const fs::path a = workRoot() / "a";
	const fs::path b = workRoot() / "b";
	const fs::path fa = workRoot() / "a.img";
	const fs::path fb = workRoot() / "b.img";
	std::string sa, sb, err;
	assert(chimera::mediaMake(a.string(), fa.string(), chimera::MediaFormat::Fat12, nullptr, sa, err));
	assert(chimera::mediaMake(b.string(), fb.string(), chimera::MediaFormat::Fat12, nullptr, sb, err));
	assert(sa == sb);

	const std::vector<uint8_t> img = slurp(fa);
	assert(img.size() == 2880u * 512u);
	assert(img[510] == 0x55 && img[511] == 0xAA);
	/* marked bootable, so it carries code to run: it says the disk holds no
	 * system and waits for a key, as a DOS data floppy does (it was zeros) */
	assert(img[0x3E] == 0xFA); /* cli, the code's first instruction */
	const std::string bootSector(reinterpret_cast<const char *>(img.data()), 512);
	assert(bootSector.find("This floppy holds files, not a system to start.") == 0x62 + 2);
	/* the BPB a 1.44 MB floppy must carry */
	assert(img[11] == 0x00 && img[12] == 0x02); /* 512 bytes per sector */
	assert(img[13] == 1);                        /* one sector per cluster */
	assert(img[21] == 0xF0);                     /* media descriptor */
	/* DOS put the moment of formatting in the volume serial; ours is fixed, or
	 * two identical disks would not hash alike */
	assert(rd32le(&img[39]) == 0);

	/* walk the root directory and find a file we put there */
	const uint32_t rootOff = (1u + 2u * 9u) * 512u;
	bool foundEboot = false, foundDir = false;
	for (uint32_t i = 0; i < 224; i++)
	{
		const uint8_t *e = &img[rootOff + i * 32];
		if (e[0] == 0) break;
		const std::string name(reinterpret_cast<const char *>(e), 11);
		if (e[11] & 0x10) foundDir = true;
		if (name.compare(0, 8, "Z_LAST  ") == 0) foundEboot = true;
		/* every date field is the fixed one */
		assert(rd32le(e + 16) == ((uint32_t(kFixedFatDate) << 16) | kFixedFatDate));
	}
	assert(foundDir);  /* PS3_GAME became a subdirectory */
	assert(foundEboot);
}

/* A file that fills the disk ends its chain in the disk's last cluster. The
 * FAT holds 2849 entries, an odd count, and they are packed in pairs: the last
 * one went unwritten and read as free, so the file had no end of chain. */
static void aFullFloppyEndsItsChainInTheLastCluster()
{
	const fs::path full = workRoot() / "full";
	fs::remove_all(full);
	const uint32_t clusters = 2880u - (1u + 2u * 9u + 14u);   /* 2847 data sectors */
	put(full / "whole.bin", std::string(size_t(clusters) * 512u, 'x'));
	std::string sha, err;
	const fs::path out = workRoot() / "full.img";
	if (!chimera::mediaMake(full.string(), out.string(), chimera::MediaFormat::Fat12, nullptr, sha, err))
	{
		std::fprintf(stderr, "full floppy: %s\n", err.c_str());
		assert(false);
	}
	const std::vector<uint8_t> img = slurp(out);
	auto entry = [&](uint32_t n) {
		const uint8_t *f = &img[512u + (n * 3u) / 2u];   /* the first FAT */
		return (n & 1u) ? uint32_t((f[0] >> 4) | (f[1] << 4)) : uint32_t(f[0] | ((f[1] & 0x0Fu) << 8));
	};
	assert(entry(2847) == 2848);    /* the chain reaches the last cluster */
	assert(entry(2848) == 0xFFF);   /* and ends there */
}

static void aFloppyRefusesWhatDoesNotFit()
{
	const fs::path big = workRoot() / "big";
	fs::remove_all(big);
	/* comfortably past 1.44 MB */
	for (int i = 0; i < 4; i++)
		put(big / ("part" + std::to_string(i) + ".bin"), std::string(500000, 'x'));
	std::string sha, err;
	assert(!chimera::mediaMake(big.string(), (workRoot() / "big.img").string(),
		chimera::MediaFormat::Fat12, nullptr, sha, err));
	assert(err.find("does not fit") != std::string::npos);
	assert(!fs::exists(workRoot() / "big.img"));
}

/* Names that shorten to the same name each get one of their own: twelve that
 * agree in their first 64 characters (Joliet's limit; its counter once had one
 * digit, so the twelfth was given the second's name), and on a floppy two that
 * shorten alike beside a file already called what the second would become. */
static void namesThatShortenAlikeStayApart()
{
	const fs::path tree = workRoot() / "alike";
	const std::string stem(64, 'n');
	for (int i = 0; i < 12; i++) put(tree / (stem + "-" + std::to_string(i) + ".txt"), std::to_string(i));
	std::string sha, err;
	const fs::path iso = workRoot() / "alike.iso";
	assert(chimera::mediaMake(tree.string(), iso.string(), chimera::MediaFormat::Iso9660, nullptr, sha, err));
	const std::vector<uint8_t> bytes = slurp(iso);
	const uint8_t *rootRec = &bytes[17 * 2048 + 156];
	std::map<std::string, std::vector<uint8_t>> got;
	readIsoJoliet(bytes, rd32le(rootRec + 2), rd32le(rootRec + 10), "", got);
	assert(got.size() == 12);

	const fs::path floppy = workRoot() / "alike-floppy";
	// The file already called what the second would become is made first:
	// on Windows NTFS gives longname2.txt that very short name of its own,
	// and a file made after it under that name opened longname2.txt instead
	put(floppy / "LONGNA~2.TXT", "3");
	put(floppy / "longname1.txt", "1");
	put(floppy / "longname2.txt", "2");
	const fs::path img = workRoot() / "alike.img";
	assert(chimera::mediaMake(floppy.string(), img.string(), chimera::MediaFormat::Fat12, nullptr, sha, err));
	const std::vector<uint8_t> disk = slurp(img);
	const uint32_t rootOff = (1u + 2u * 9u) * 512u;
	std::set<std::string> names;
	int entries = 0;
	for (uint32_t i = 0; i < 224; i++)
	{
		const uint8_t *e = &disk[rootOff + i * 32];
		if (e[0] == 0) break;
		names.insert(std::string(reinterpret_cast<const char *>(e), 11));
		entries++;
	}
	assert(entries == 3 && names.size() == 3);
}

/* A counter given to an earlier loser must not be a later file's own short
 * name: the folder held two entries of one name, and one file could not be
 * reached (engine analysis, 2026-10-18). The later file's name sorts after
 * the losers (lower case, '~' and '_' after letters), which is the case the
 * old check missed. */
static void aCounterIsNeverALaterFilesOwnName()
{
	std::string sha, err;
	const fs::path floppy = workRoot() / "counter-floppy";
	fs::remove_all(floppy);
	// (made first, as above: NTFS would give aaaaaaaa2.txt this short name)
	put(floppy / "aaaaaa~2.txt", "3");
	put(floppy / "aaaaaaaa1.txt", "1");
	put(floppy / "aaaaaaaa2.txt", "2");
	const fs::path img = workRoot() / "counter.img";
	assert(chimera::mediaMake(floppy.string(), img.string(), chimera::MediaFormat::Fat12, nullptr, sha, err));
	const std::vector<uint8_t> disk = slurp(img);
	const uint32_t rootOff = (1u + 2u * 9u) * 512u;
	std::set<std::string> names;
	int entries = 0;
	for (uint32_t i = 0; i < 224; i++)
	{
		const uint8_t *e = &disk[rootOff + i * 32];
		if (e[0] == 0) break;
		names.insert(std::string(reinterpret_cast<const char *>(e), 11));
		entries++;
	}
	assert(entries == 3 && names.size() == 3);

	/* the ISO's primary tree: two names that mangle alike, and a later file
	 * whose own name is what the second would be given */
	const fs::path tree = workRoot() / "counter-iso";
	fs::remove_all(tree);
	put(tree / "a!b.txt", "1");
	put(tree / "a#b.txt", "2");
	put(tree / "a_b.txt_2", "3");   /* A_B.TXT_2;1, the counter the second would get */
	const fs::path iso = workRoot() / "counter.iso";
	assert(chimera::mediaMake(tree.string(), iso.string(), chimera::MediaFormat::Iso9660, nullptr, sha, err));
	const std::vector<uint8_t> bytes = slurp(iso);
	const uint8_t *pvdRoot = &bytes[16 * 2048 + 156];
	const uint32_t at = rd32le(pvdRoot + 2) * 2048, len = rd32le(pvdRoot + 10);
	std::set<std::string> isoNames;
	int isoEntries = 0;
	for (uint32_t off = 0; off < len;)
	{
		const uint8_t recLen = bytes[at + off];
		if (recLen == 0) break;
		const uint8_t nameLen = bytes[at + off + 32];
		const std::string name(reinterpret_cast<const char *>(&bytes[at + off + 33]), nameLen);
		if (!(nameLen == 1 && (name[0] == 0 || name[0] == 1))) { isoNames.insert(name); isoEntries++; }
		off += recLen;
	}
	assert(isoEntries == 3 && isoNames.size() == 3);
}

/* The path tables list folders by level, then by their parent's number,
 * then by name (ECMA-119 6.9.1). B/A and A/Z sit at the same level under
 * different parents: by name alone A came first, under a parent numbered
 * after Z's. */
static void pathTablesGoByParent()
{
	const fs::path tree = workRoot() / "levels";
	put(tree / "B" / "A" / "one.txt", "1");
	put(tree / "A" / "Z" / "two.txt", "2");
	std::string sha, err;
	const fs::path iso = workRoot() / "levels.iso";
	assert(chimera::mediaMake(tree.string(), iso.string(), chimera::MediaFormat::Iso9660, nullptr, sha, err));
	const std::vector<uint8_t> bytes = slurp(iso);
	for (const uint32_t vd : { 16u, 17u }) /* the primary table, then the Joliet one */
	{
		const uint8_t *d = &bytes[size_t(vd) * 2048];
		const uint32_t size = rd32le(d + 132), at = rd32le(d + 140);
		const uint8_t *t = &bytes[size_t(at) * 2048];
		std::vector<uint16_t> parents;
		for (uint32_t pos = 0; pos < size;)
		{
			const uint8_t idLen = t[pos];
			parents.push_back(uint16_t(t[pos + 6] | (t[pos + 7] << 8)));
			pos += 8 + idLen + (idLen & 1);
		}
		assert(parents.size() == 5); /* the root, A, B, A/Z, B/A */
		assert(std::is_sorted(parents.begin(), parents.end()));
	}
}

/* An image already there stays as it was when a pack of any format fails,
 * and no temporary file is left beside it. A cancelled or failed pack used to
 * delete it (engine analysis, 2026-10-18). On Linux a full disk is played by
 * a file size limit: the pack must fail and say so, not report an image. */
static void aFailedPackKeepsTheImageThatWasThere()
{
	const fs::path a = workRoot() / "a";
	const struct { chimera::MediaFormat format; const char *name; } kinds[] = {
		{ chimera::MediaFormat::ZipStored, "kept.zip" },
		{ chimera::MediaFormat::Iso9660, "kept.iso" },
		{ chimera::MediaFormat::Fat12, "kept.img" },
	};
	const fs::path small = workRoot() / "small";
	fs::remove_all(small);
	put(small / "ONE.TXT", std::string(200000, 'o'));
	for (const auto &kind : kinds)
	{
		const fs::path out = workRoot() / kind.name;
		put(out, "the image that was there");
		std::string sha, err;
		const bool ok = chimera::mediaMake(small.string(), out.string(), kind.format,
			[&](const char *, uint64_t, uint64_t, uint64_t, uint64_t) { return false; }, sha, err);
		assert(!ok && err == "cancelled");
		const std::vector<uint8_t> kept = slurp(out);
		assert(std::string(kept.begin(), kept.end()) == "the image that was there");
		assert(!fs::exists(out.string() + ".writing"));
#if !defined(_WIN32)
		/* the disk fills: writes past 64 KiB fail, as on a full disk */
		std::signal(SIGXFSZ, SIG_IGN);
		struct rlimit was {};
		getrlimit(RLIMIT_FSIZE, &was);
		struct rlimit full = was;
		full.rlim_cur = 64 * 1024;
		setrlimit(RLIMIT_FSIZE, &full);
		const bool wrote = chimera::mediaMake(small.string(), out.string(), kind.format, nullptr, sha, err);
		setrlimit(RLIMIT_FSIZE, &was);
		std::signal(SIGXFSZ, SIG_DFL);
		assert(!wrote && err.find("disk") != std::string::npos);
		assert(slurp(out).size() == 24);
		assert(!fs::exists(out.string() + ".writing"));
#endif
		/* and when it works, the image is replaced */
		assert(chimera::mediaMake(small.string(), out.string(), kind.format, nullptr, sha, err));
		assert(slurp(out).size() > 24);
	}
	(void)a;
}

int main()
{
	fs::remove_all(workRoot());
	fs::create_directories(workRoot());

	theSameContentPacksToTheSameBytes();
	whatComesOutIsWhatWentIn();
	collectSkipsWhatIsNotAFile();
	progressCountsEveryByteAndFile();
	anIsoIsReproducibleAndHoldsWhatWentIn();
	aRecipeWritesTheSystemArea();
	aRecipeNotUnderstoodIsRefused();
	aFloppyIsReproducibleAndReadable();
	aFullFloppyEndsItsChainInTheLastCluster();
	aFloppyRefusesWhatDoesNotFit();
	aCancelledPackLeavesNothing();
	aFailedPackKeepsTheImageThatWasThere();
	anEmptyFolderIsRefused();
	namesThatShortenAlikeStayApart();
	aCounterIsNeverALaterFilesOwnName();
	pathTablesGoByParent();

	fs::remove_all(workRoot());
	std::printf("test_media: ok\n");
	return 0;
}
