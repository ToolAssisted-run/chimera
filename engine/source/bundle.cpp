/* bundle.cpp - the .gameBundle catalogue format.
 *
 * Replaces GameBundle.cs's parsing, validation, identity and serialization.
 * The refusal messages are the ones the frontend always showed (its tests pin
 * them); the path rule is now pure string logic, so Linux and Windows agree
 * about what a bundle may name.
 */

#include "chimera/engine.h"
#include "sha1.hpp"

#include "../../extern/cjson/cJSON.h"

#include <cstring>
#include <algorithm>
#include <optional>
#include <string>
#include <vector>

struct ce_bundle
{
	std::optional<std::string> name;
	std::string romFile;
	std::optional<std::string> romSha1;
	struct Attachment
	{
		std::string core, id, file;
		std::optional<std::string> sha1;
	};
	std::vector<Attachment> attach;
	std::string scratch;
};

namespace {

thread_local std::string g_parseError;

bool isBlankStr(const char *s)
{
	if (s == nullptr) return true;
	for (; *s != '\0'; s++)
	{
		if (*s != ' ' && (*s < '\t' || *s > '\r')) return false;
	}
	return true;
}

const char *stringField(const cJSON *obj, const char *key)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsString(item) ? item->valuestring : nullptr;
}

std::string upper(std::string s)
{
	for (auto &c : s)
	{
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
	}
	return s;
}

/* validates every part's file field at parse, as the loader always has */
bool checkParts(const ce_bundle *b, const char *label, std::string &error)
{
	auto fail = [&](const std::string &file, int32_t rc)
	{
		switch (rc)
		{
			case 1: error = "a bundle entry names no file"; return;
			case 2: error = "\"" + file + "\": a bundle may only name files beside it, not absolute paths"; return;
			default: error = "\"" + file + "\": a bundle may only name files beside it, not outside its own folder"; return;
		}
	};
	(void)label;
	int32_t rc = ce_bundle_check_path(b->romFile.c_str());
	if (rc != 0)
	{
		fail(b->romFile, rc);
		return false;
	}
	for (const auto &a : b->attach)
	{
		rc = ce_bundle_check_path(a.file.c_str());
		if (rc != 0)
		{
			fail(a.file, rc);
			return false;
		}
	}
	return true;
}

} // namespace

extern "C" {

ce_bundle *ce_bundle_new(void) { return new ce_bundle(); }

void ce_bundle_free(ce_bundle *b) { delete b; }

ce_bundle *ce_bundle_parse(
	const char *json, uint64_t len, const char *file_label, const char **error_out)
{
	std::string label = file_label != nullptr ? file_label : "bundle";
	auto refuse = [&](std::string message) -> ce_bundle *
	{
		g_parseError = std::move(message);
		if (error_out != nullptr) *error_out = g_parseError.c_str();
		return nullptr;
	};
	if (error_out != nullptr) *error_out = nullptr;

	cJSON *root = cJSON_ParseWithLength(json, static_cast<size_t>(len));
	if (root == nullptr) return refuse(label + " is not a readable bundle");
	if (!cJSON_IsObject(root))
	{
		cJSON_Delete(root);
		return refuse(label + " is not a readable bundle");
	}

	const cJSON *versionItem = cJSON_GetObjectItemCaseSensitive(root, "bundle");
	int version = cJSON_IsNumber(versionItem) ? versionItem->valueint : 1;
	if (version > 1)
	{
		cJSON_Delete(root);
		return refuse(label + " is a version " + std::to_string(version) + " bundle; this build understands version 1");
	}

	auto *b = new ce_bundle();
	const char *name = stringField(root, "name");
	if (name != nullptr) b->name = name;

	const cJSON *rom = cJSON_GetObjectItemCaseSensitive(root, "rom");
	const char *romFile = cJSON_IsObject(rom) ? stringField(rom, "file") : nullptr;
	if (isBlankStr(romFile))
	{
		cJSON_Delete(root);
		delete b;
		return refuse(label + " names no rom");
	}
	b->romFile = romFile;
	const char *romSha1 = stringField(rom, "sha1");
	if (romSha1 != nullptr) b->romSha1 = romSha1;

	const cJSON *attach = cJSON_GetObjectItemCaseSensitive(root, "attach");
	if (cJSON_IsArray(attach))
	{
		const cJSON *item = nullptr;
		cJSON_ArrayForEach(item, attach)
		{
			if (!cJSON_IsObject(item)) continue;
			ce_bundle::Attachment a;
			const char *core = stringField(item, "core");
			const char *id = stringField(item, "id");
			const char *file = stringField(item, "file");
			const char *sha1 = stringField(item, "sha1");
			a.core = core != nullptr ? core : "";
			a.id = id != nullptr ? id : "";
			a.file = file != nullptr ? file : "";
			if (sha1 != nullptr) a.sha1 = sha1;
			b->attach.push_back(std::move(a));
		}
	}
	cJSON_Delete(root);

	std::string error;
	if (!checkParts(b, file_label, error))
	{
		delete b;
		return refuse(std::move(error));
	}
	return b;
}

const char *ce_bundle_name(const ce_bundle *b) { return b->name.has_value() ? b->name->c_str() : nullptr; }

void ce_bundle_set_name(ce_bundle *b, const char *name)
{
	if (name == nullptr) b->name.reset();
	else b->name = name;
}

const char *ce_bundle_rom_file(const ce_bundle *b) { return b->romFile.c_str(); }

const char *ce_bundle_rom_sha1(const ce_bundle *b) { return b->romSha1.has_value() ? b->romSha1->c_str() : nullptr; }

void ce_bundle_set_rom(ce_bundle *b, const char *file, const char *sha1)
{
	b->romFile = file != nullptr ? file : "";
	if (sha1 == nullptr) b->romSha1.reset();
	else b->romSha1 = sha1;
}

int64_t ce_bundle_attach_count(const ce_bundle *b) { return static_cast<int64_t>(b->attach.size()); }

#define CE_ATTACH_AT(b, index) \
	((index) >= 0 && (index) < static_cast<int64_t>((b)->attach.size()) \
		? &(b)->attach[static_cast<size_t>(index)] : nullptr)

const char *ce_bundle_attach_core(const ce_bundle *b, int64_t index)
{
	const auto *a = CE_ATTACH_AT(b, index);
	return a != nullptr ? a->core.c_str() : nullptr;
}

const char *ce_bundle_attach_id(const ce_bundle *b, int64_t index)
{
	const auto *a = CE_ATTACH_AT(b, index);
	return a != nullptr ? a->id.c_str() : nullptr;
}

const char *ce_bundle_attach_file(const ce_bundle *b, int64_t index)
{
	const auto *a = CE_ATTACH_AT(b, index);
	return a != nullptr ? a->file.c_str() : nullptr;
}

const char *ce_bundle_attach_sha1(const ce_bundle *b, int64_t index)
{
	const auto *a = CE_ATTACH_AT(b, index);
	return a != nullptr && a->sha1.has_value() ? a->sha1->c_str() : nullptr;
}

void ce_bundle_add_attach(ce_bundle *b, const char *core, const char *id, const char *file, const char *sha1)
{
	ce_bundle::Attachment a;
	a.core = core != nullptr ? core : "";
	a.id = id != nullptr ? id : "";
	a.file = file != nullptr ? file : "";
	if (sha1 != nullptr) a.sha1 = sha1;
	b->attach.push_back(std::move(a));
}

void ce_bundle_set_attach_sha1(ce_bundle *b, int64_t index, const char *sha1)
{
	auto *a = CE_ATTACH_AT(b, index);
	if (a == nullptr) return;
	if (sha1 == nullptr) a->sha1.reset();
	else a->sha1 = sha1;
}

const char *ce_bundle_content_id(ce_bundle *b)
{
	/* identity is over the parts, not the file: "rom:<SHA>\n" then each
	 * "<core>:<id>:<SHA>\n" ordered by core then id (ordinal), hashes
	 * uppercased, the whole thing SHA1'd - exactly what the C# hashed */
	if (!b->romSha1.has_value()) return nullptr;
	std::string material = "rom:" + upper(*b->romSha1) + "\n";
	std::vector<const ce_bundle::Attachment *> ordered;
	ordered.reserve(b->attach.size());
	for (const auto &a : b->attach) ordered.push_back(&a);
	std::stable_sort(ordered.begin(), ordered.end(),
		[](const ce_bundle::Attachment *x, const ce_bundle::Attachment *y)
		{
			int c = x->core.compare(y->core);
			return c != 0 ? c < 0 : x->id.compare(y->id) < 0;
		});
	for (const auto *a : ordered)
	{
		if (!a->sha1.has_value()) return nullptr;
		material.append(a->core).append(1, ':').append(a->id).append(1, ':').append(upper(*a->sha1)).append(1, '\n');
	}
	b->scratch = chimera::sha1Hex(reinterpret_cast<const uint8_t *>(material.data()), material.size());
	return b->scratch.c_str();
}

const char *ce_bundle_serialize(ce_bundle *b, uint64_t *len_out)
{
	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "bundle", 1);
	if (b->name.has_value()) cJSON_AddStringToObject(root, "name", b->name->c_str());
	cJSON *rom = cJSON_AddObjectToObject(root, "rom");
	cJSON_AddStringToObject(rom, "file", b->romFile.c_str());
	if (b->romSha1.has_value()) cJSON_AddStringToObject(rom, "sha1", b->romSha1->c_str());
	cJSON *attach = cJSON_AddArrayToObject(root, "attach");
	for (const auto &a : b->attach)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "core", a.core.c_str());
		cJSON_AddStringToObject(item, "id", a.id.c_str());
		cJSON_AddStringToObject(item, "file", a.file.c_str());
		if (a.sha1.has_value()) cJSON_AddStringToObject(item, "sha1", a.sha1->c_str());
		cJSON_AddItemToArray(attach, item);
	}
	char *printed = cJSON_Print(root);
	cJSON_Delete(root);
	b->scratch = printed != nullptr ? printed : "";
	if (printed != nullptr) cJSON_free(printed);
	b->scratch.append(1, '\n');
	if (len_out != nullptr) *len_out = b->scratch.size();
	return b->scratch.c_str();
}

int32_t ce_bundle_check_path(const char *file)
{
	if (isBlankStr(file)) return 1;
	std::string s(file);
	if (s[0] == '/' || s[0] == '\\' || s.find(':') != std::string::npos) return 2;
	/* depth-track the segments; dipping below the bundle's own folder is out */
	int depth = 0;
	size_t pos = 0;
	while (pos <= s.size())
	{
		size_t sep = s.find_first_of("/\\", pos);
		if (sep == std::string::npos) sep = s.size();
		std::string segment = s.substr(pos, sep - pos);
		if (segment == "..")
		{
			if (--depth < 0) return 3;
		}
		else if (!segment.empty() && segment != ".")
		{
			depth++;
		}
		pos = sep + 1;
	}
	return 0;
}

void ce_sha1_hex(const uint8_t *data, uint64_t len, char *out41)
{
	std::string hex = chimera::sha1Hex(data, len);
	std::memcpy(out41, hex.data(), 40);
	out41[40] = '\0';
}

} // extern "C"
