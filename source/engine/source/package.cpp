/* package.cpp - the core package container.
 *
 * The zip/directory reading and identity hashing; what a package's entries
 * MEAN is the session's business. Package files live
 * at the archive root - that is what the extraction cache hands to the
 * loader, so anything nested is not a package we can load.
 */

#include "chimera/engine.h"
#include "language.hpp"
#include "file_io.hpp"
#include "sha1.hpp"
#include "thread_string.hpp"

#include "../../extern/miniz/miniz.h"
#include "../../extern/cjson/cJSON.h"

#include <cstdint>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <iterator>
#include <map>
#include <new>
#include <string>
#include <vector>

namespace {

thread_local chimera::ThreadString g_openError;   /* never destroyed: see thread_string.hpp */

constexpr const char *WBX_FILE = "core.wbx";
constexpr const char *CONFIG_FILE = "waterbox.config";
constexpr const char *MANIFEST_FILE = "chimera-core.json";
constexpr const char *MANIFEST_FILE_LEGACY = "minihawk-core.json"; // packages built before the rename

} // namespace

struct ce_package
{
	// zip form
	std::vector<uint8_t> zipData;
	mz_zip_archive zip{};
	bool zipOpen = false;
	std::map<std::string, mz_uint> entries;
	std::vector<std::string> assetNames;
	bool assetsBuilt = false;
	std::string sha1;

	// directory form
	std::string dir;

	bool isWaterbox = false;
	std::string error;
	std::vector<uint8_t> entryData;

	/* the package's language tables, read when a declaration's texts are
	 * first asked for (docs/translations.md) */
	bool textsRead = false;
	chimera::lang::PackageTexts texts;
	std::vector<uint8_t> textsData;

	~ce_package()
	{
		if (zipOpen) mz_zip_reader_end(&zip);
	}
};

namespace {

thread_local chimera::ThreadString g_problems;   /* never destroyed: see thread_string.hpp */

/* a file of the package, copied: for the language tables and the checks */
bool entryCopy(ce_package *p, const std::string &name, std::string &bytes)
{
	uint64_t len = 0;
	const uint8_t *data = ce_package_entry(p, name.c_str(), &len);
	if (data == nullptr) return false;
	bytes.assign(reinterpret_cast<const char *>(data), static_cast<size_t>(len));
	return true;
}

} // namespace

extern "C" {

static ce_package *openPackage(const char *path, const char **error_out)
{
	if (error_out != nullptr) *error_out = nullptr;

	if (chimera::isDirectory(path))
	{
		auto join = [&](const char *name) { return std::string(path) + "/" + name; };
		bool waterbox = chimera::fileExists(join(WBX_FILE).c_str()) && chimera::fileExists(join(CONFIG_FILE).c_str());
		if (!waterbox && !chimera::fileExists(join(MANIFEST_FILE).c_str())
			&& !chimera::fileExists(join(MANIFEST_FILE_LEGACY).c_str()))
		{
			return nullptr;
		}
		auto *p = new ce_package();
		p->dir = path;
		p->isWaterbox = waterbox;
		return p;
	}

	std::vector<uint8_t> data;
	if (!chimera::readFile(path, data)) return nullptr; // no file, no package
	auto *p = new ce_package();
	p->zipData = std::move(data);
	if (mz_zip_reader_init_mem(&p->zip, p->zipData.data(), p->zipData.size(), 0) == MZ_FALSE)
	{
		/* a file that claims to be a zip but does not parse is BROKEN, not
		 * invisible - a corrupt package must stay listable with its error */
		bool looksLikeZip = p->zipData.size() >= 4 && std::memcmp(p->zipData.data(), "PK\x03\x04", 4) == 0;
		delete p;
		if (looksLikeZip)
		{
			*g_openError = chimera::lang::tr("package.not_zip");
			if (error_out != nullptr) *error_out = g_openError->c_str();
		}
		return nullptr; // otherwise: not a zip, so not a package - the quiet case
	}
	p->zipOpen = true;
	mz_uint count = mz_zip_reader_get_num_files(&p->zip);
	for (mz_uint i = 0; i < count; i++)
	{
		char nameBuf[512];
		mz_zip_reader_get_filename(&p->zip, i, nameBuf, sizeof nameBuf);
		p->entries.emplace(nameBuf, i); // root-relative names; first wins on the (never-seen) dup
	}
	bool waterbox = p->entries.count(WBX_FILE) != 0 && p->entries.count(CONFIG_FILE) != 0;
	if (!waterbox && p->entries.count(MANIFEST_FILE) == 0 && p->entries.count(MANIFEST_FILE_LEGACY) == 0)
	{
		delete p;
		return nullptr; // an ordinary zip
	}
	p->isWaterbox = waterbox;
	/* only now is the hash worth paying for: it is the identity of a thing we
	 * know to be a package */
	p->sha1 = chimera::sha1Hex(p->zipData.data(), p->zipData.size());
	return p;
}

ce_package *ce_package_open(const char *path, const char **error_out)
{
	/* a package is somebody's file: nothing in it may end the process */
	try
	{
		return openPackage(path, error_out);
	}
	catch (const std::bad_alloc &)
	{
		*g_openError = chimera::lang::tr("package.no_memory");
		if (error_out != nullptr) *error_out = g_openError->c_str();
		return nullptr;
	}
}

void ce_package_free(ce_package *p) { delete p; }

const char *ce_package_sha1(const ce_package *p) { return p->sha1.empty() ? nullptr : p->sha1.c_str(); }

int32_t ce_package_is_waterbox(const ce_package *p) { return p->isWaterbox ? 1 : 0; }

/* The package's core-owned asset files: every entry under "assets/". The
 * names are cached on first ask; bytes come through ce_package_entry like
 * any other entry. */
static void buildAssetList(ce_package *p)
{
	if (p->assetsBuilt) return;
	p->assetsBuilt = true;
	if (!p->dir.empty())
	{
		std::error_code ec;
		std::filesystem::path base = std::filesystem::path(p->dir) / "assets";
		if (std::filesystem::is_directory(base, ec))
		{
			for (auto it = std::filesystem::recursive_directory_iterator(base, ec);
			     !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
			{
				if (!it->is_regular_file(ec)) continue;
				std::error_code rec;
				std::string rel = std::filesystem::relative(it->path(), std::filesystem::path(p->dir), rec).generic_string();
				if (!rec) p->assetNames.push_back(std::move(rel));
			}
		}
	}
	else
	{
		for (const auto &e : p->entries)
			if (e.first.rfind("assets/", 0) == 0 && e.first.back() != '/')
				p->assetNames.push_back(e.first);
	}
	std::sort(p->assetNames.begin(), p->assetNames.end());
}

int32_t ce_package_asset_count(ce_package *p)
{
	buildAssetList(p);
	return static_cast<int32_t>(p->assetNames.size());
}

const char *ce_package_asset_name(ce_package *p, int32_t i)
{
	buildAssetList(p);
	if (i < 0 || static_cast<size_t>(i) >= p->assetNames.size()) return nullptr;
	return p->assetNames[static_cast<size_t>(i)].c_str();
}

int32_t ce_package_has_entry(ce_package *p, const char *name)
{
	if (!p->dir.empty()) return chimera::fileExists((p->dir + "/" + name).c_str()) ? 1 : 0;
	return p->entries.count(name) != 0 ? 1 : 0;
}

const uint8_t *ce_package_entry(ce_package *p, const char *name, uint64_t *len_out)
{
	p->error.clear();
	if (!p->dir.empty())
	{
		std::string full = p->dir + "/" + name;
		if (!chimera::fileExists(full.c_str())) return nullptr;
		if (!chimera::readFile(full.c_str(), p->entryData))
		{
			p->error = chimera::lang::tr("package.not_read", {name});
			return nullptr;
		}
	}
	else
	{
		auto it = p->entries.find(name);
		if (it == p->entries.end()) return nullptr;
		/* straight into entryData: extracting to a buffer of its own and
		 * copying that held the entry twice for a moment (engine analysis,
		 * 2026-10-18; a core.wbx is up to 333 MB) */
		mz_zip_archive_file_stat stat{};
		bool ok = mz_zip_reader_file_stat(&p->zip, it->second, &stat) == MZ_TRUE && stat.m_uncomp_size < SIZE_MAX / 2;
		if (ok)
		{
			try
			{
				p->entryData.clear();
				p->entryData.resize(static_cast<size_t>(stat.m_uncomp_size));
			}
			catch (const std::bad_alloc &)
			{
				p->entryData = std::vector<uint8_t>();
				p->error = chimera::lang::tr("container.no_memory_for", {name});
				return nullptr;
			}
			if (!p->entryData.empty())
				ok = mz_zip_reader_extract_to_mem(&p->zip, it->second, p->entryData.data(), p->entryData.size(), 0) == MZ_TRUE;
		}
		if (!ok)
		{
			p->error = chimera::lang::tr("container.not_extracted", {name});
			return nullptr;
		}
	}
	if (len_out != nullptr) *len_out = p->entryData.size();
	return p->entryData.data();
}

const char *ce_package_last_error(ce_package *p) { return p->error.c_str(); }

const uint8_t *ce_package_entry_texts(ce_package *p, const char *name, uint64_t *len_out)
{
	if (!p->textsRead)
	{
		p->textsRead = true;
		p->texts.read([p](const std::string &file, std::string &bytes) { return entryCopy(p, file, bytes); });
	}
	std::string raw;
	if (!entryCopy(p, name, raw)) return nullptr;
	const std::string text = p->texts.texts(raw);
	p->textsData.assign(text.begin(), text.end());
	if (len_out != nullptr) *len_out = p->textsData.size();
	return p->textsData.data();
}

const char *ce_package_language_problems(ce_package *p)
{
	std::string out;
	for (const std::string &line : chimera::lang::packageProblems(
			 [p](const std::string &name, std::string &bytes) { return entryCopy(p, name, bytes); }))
		out += line + "\n";
	*g_problems = out;
	return g_problems->c_str();
}

const char *ce_host_platform(void)
{
#if defined(_WIN32)
#define CE_HOST_SYSTEM "windows"
#elif defined(__APPLE__)
#define CE_HOST_SYSTEM "macos"
#else
#define CE_HOST_SYSTEM "linux"
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
	return CE_HOST_SYSTEM "-aarch64";
#else
	return CE_HOST_SYSTEM "-x86_64";
#endif
#undef CE_HOST_SYSTEM
}

const char *ce_package_runs_here(ce_package *p)
{
	static const char *const known[] = { "windows-x86_64", "windows-aarch64", "linux-x86_64",
		                                  "linux-aarch64", "macos-x86_64", "macos-aarch64" };
	const std::string here = ce_host_platform();
	std::string config;
	cJSON *root = entryCopy(p, CONFIG_FILE, config) ? cJSON_ParseWithLength(config.data(), config.size()) : nullptr;
	const cJSON *runsOn = root != nullptr ? cJSON_GetObjectItemCaseSensitive(root, "runsOn") : nullptr;
	std::string why;
	if (!cJSON_IsArray(runsOn) || cJSON_GetArraySize(runsOn) == 0)
		why = chimera::lang::tr("package.runs_on_missing");
	else
	{
		bool listed = false;
		std::string names;
		const cJSON *entry = nullptr;
		cJSON_ArrayForEach(entry, runsOn)
		{
			const char *name = cJSON_GetStringValue(entry);
			if (name == nullptr || std::find_if(std::begin(known), std::end(known), [name](const char *k) { return std::strcmp(k, name) == 0; }) == std::end(known))
			{
				why = chimera::lang::tr("package.runs_on_unknown", {name != nullptr ? std::string(name) : std::string("?")});
				break;
			}
			listed = listed || here == name;
			names += (names.empty() ? "" : ", ") + std::string(name);
		}
		if (why.empty() && !listed) why = chimera::lang::tr("package.runs_elsewhere", {names, here});
	}
	cJSON_Delete(root);
	if (why.empty()) return nullptr;
	*g_problems = why;
	return g_problems->c_str();
}

} // extern "C"
