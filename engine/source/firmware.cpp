/* firmware.cpp - is this file the firmware the core asked for, and the
 * canonical firmware line a movie records. The declarations, the config that
 * remembers paths, and the filesystem stay with the caller.
 */

#include "chimera/engine.h"

#include "../../extern/cjson/cJSON.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

thread_local std::string g_record;
thread_local std::string g_evaluated;

bool equalsIgnoreCase(const std::string &a, const std::string &b)
{
	if (a.size() != b.size()) return false;
	for (size_t i = 0; i < a.size(); i++)
	{
		char x = a[i], y = b[i];
		if (x >= 'a' && x <= 'z') x = static_cast<char>(x - 'a' + 'A');
		if (y >= 'a' && y <= 'z') y = static_cast<char>(y - 'a' + 'A');
		if (x != y) return false;
	}
	return true;
}

std::vector<std::string> splitLines(const char *text)
{
	std::vector<std::string> out;
	if (text == nullptr) return out;
	const char *p = text;
	while (*p != '\0')
	{
		const char *nl = p;
		while (*nl != '\0' && *nl != '\n') nl++;
		if (nl != p) out.emplace_back(p, nl);
		p = *nl == '\0' ? nl : nl + 1;
	}
	return out;
}

} // namespace

extern "C" {

int32_t ce_firmware_state(
	int64_t declared_size, const char *expected_sha1s,
	int64_t actual_size, const char *actual_sha1)
{
	if (declared_size != 0 && actual_size != declared_size) return 0; // wrong size
	auto expected = splitLines(expected_sha1s);
	if (expected.empty()) return 2; // the core pinned nothing, so any dump is good
	std::string actual = actual_sha1 != nullptr ? actual_sha1 : "";
	for (const auto &hash : expected)
	{
		if (equalsIgnoreCase(hash, actual)) return 2;
	}
	return 1; // unrecognised, used anyway - a good dump may be one the core never saw
}

const char *ce_firmware_record_line(const char *pairs, uint64_t *len_out)
{
	auto entries = splitLines(pairs);
	/* canonical order: by the id before '=', ordinal - replay must never
	 * report a difference that is only an ordering */
	std::stable_sort(entries.begin(), entries.end(),
		[](const std::string &a, const std::string &b)
		{
			return a.substr(0, a.find('=')) < b.substr(0, b.find('='));
		});
	g_record.clear();
	for (const auto &entry : entries)
	{
		if (!g_record.empty()) g_record.append(1, ' ');
		g_record.append(entry);
	}
	if (len_out != nullptr) *len_out = g_record.size();
	return g_record.c_str();
}

/* ---- the firmware decision tree ---- */

static bool valueMatches(const cJSON *want, const cJSON *have)
{
	if (want == nullptr || have == nullptr) return false;
	if (cJSON_IsString(want) && cJSON_IsString(have))
	{
		return std::string(want->valuestring) == have->valuestring;
	}
	if (cJSON_IsNumber(want) && cJSON_IsNumber(have))
	{
		return want->valuedouble == have->valuedouble;
	}
	if (cJSON_IsBool(want) && cJSON_IsBool(have))
	{
		return cJSON_IsTrue(want) == cJSON_IsTrue(have);
	}
	return false;
}

static bool slotHasExtension(const cJSON *names, const std::string &ext)
{
	for (const cJSON *name = names != nullptr ? names->child : nullptr; name != nullptr; name = name->next)
	{
		if (!cJSON_IsString(name)) continue;
		std::string n = name->valuestring;
		size_t dot = n.find_last_of('.');
		if (dot == std::string::npos) continue;
		std::string e = n.substr(dot + 1);
		for (char &c : e)
		{
			if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		}
		if (e == ext) return true;
	}
	return false;
}

/* The condition language (docs/project.md): {"slot": id[, "extension": e]},
 * {"setting": name, "is": v} / {"setting": name, "in": [v...]}, and the
 * combinators {"all": [...]}, {"any": [...]}, {"not": {...}}. Anything
 * malformed evaluates false - a core that misdeclares asks for nothing
 * rather than for everything. */
static bool evalCondition(const cJSON *cond, const cJSON *slots, const cJSON *settings)
{
	if (!cJSON_IsObject(cond)) return false;

	const cJSON *sub;
	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "all")) != nullptr)
	{
		if (!cJSON_IsArray(sub)) return false;
		for (const cJSON *c = sub->child; c != nullptr; c = c->next)
		{
			if (!evalCondition(c, slots, settings)) return false;
		}
		return true;
	}
	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "any")) != nullptr)
	{
		if (!cJSON_IsArray(sub)) return false;
		for (const cJSON *c = sub->child; c != nullptr; c = c->next)
		{
			if (evalCondition(c, slots, settings)) return true;
		}
		return false;
	}
	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "not")) != nullptr)
	{
		return !evalCondition(sub, slots, settings);
	}

	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "slot")) != nullptr)
	{
		if (!cJSON_IsString(sub)) return false;
		const cJSON *names = slots != nullptr
			? cJSON_GetObjectItemCaseSensitive(slots, sub->valuestring)
			: nullptr;
		const cJSON *ext = cJSON_GetObjectItemCaseSensitive(cond, "extension");
		if (ext != nullptr)
		{
			if (!cJSON_IsString(ext)) return false;
			std::string e = ext->valuestring;
			for (char &c : e)
			{
				if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
			}
			return slotHasExtension(names, e);
		}
		return cJSON_IsArray(names) && names->child != nullptr;
	}

	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "setting")) != nullptr)
	{
		if (!cJSON_IsString(sub)) return false;
		const cJSON *have = settings != nullptr
			? cJSON_GetObjectItemCaseSensitive(settings, sub->valuestring)
			: nullptr;
		const cJSON *want = cJSON_GetObjectItemCaseSensitive(cond, "is");
		if (want != nullptr) return valueMatches(want, have);
		const cJSON *any = cJSON_GetObjectItemCaseSensitive(cond, "in");
		if (cJSON_IsArray(any))
		{
			for (const cJSON *w = any->child; w != nullptr; w = w->next)
			{
				if (valueMatches(w, have)) return true;
			}
		}
		return false;
	}

	return false;
}

const char *ce_firmware_evaluate(
	const char *decl_json, uint64_t decl_len,
	const char *slots_json, uint64_t slots_len,
	const char *settings_json, uint64_t settings_len,
	uint64_t *len_out)
{
	if (len_out != nullptr) *len_out = 0;

	cJSON *decl = decl_json != nullptr
		? cJSON_ParseWithLength(decl_json, static_cast<size_t>(decl_len)) : nullptr;
	cJSON *slots = slots_json != nullptr
		? cJSON_ParseWithLength(slots_json, static_cast<size_t>(slots_len)) : nullptr;
	cJSON *settings = settings_json != nullptr
		? cJSON_ParseWithLength(settings_json, static_cast<size_t>(settings_len)) : nullptr;

	cJSON *out = cJSON_CreateArray();
	if (cJSON_IsArray(decl))
	{
		int32_t index = 0;
		for (const cJSON *entry = decl->child; entry != nullptr; entry = entry->next, index++)
		{
			const cJSON *id = cJSON_GetObjectItemCaseSensitive(entry, "id");
			if (!cJSON_IsString(id)) continue;
			/* every applying entry is REQUIRED: the decisions nail each
			 * requirement to one exact file or to nothing - variants are
			 * separate entries (same id, disjoint conditions) selected by a
			 * sync setting, and optional firmware does not exist */
			const cJSON *when = cJSON_GetObjectItemCaseSensitive(entry, "requiredWhen");
			if (when != nullptr && !evalCondition(when, slots, settings)) continue;
			cJSON *item = cJSON_CreateObject();
			cJSON_AddStringToObject(item, "id", id->valuestring);
			cJSON_AddNumberToObject(item, "index", index);
			cJSON_AddItemToArray(out, item);
		}
	}

	char *text = cJSON_PrintUnformatted(out);
	g_evaluated = text != nullptr ? text : "[]";
	if (text != nullptr) cJSON_free(text);
	cJSON_Delete(out);
	cJSON_Delete(decl);
	cJSON_Delete(slots);
	cJSON_Delete(settings);

	if (len_out != nullptr) *len_out = g_evaluated.size();
	return g_evaluated.c_str();
}

const char *ce_settings_evaluate(
	const char *decl_json, uint64_t decl_len,
	const char *slots_json, uint64_t slots_len,
	const char *settings_json, uint64_t settings_len,
	uint64_t *len_out)
{
	if (len_out != nullptr) *len_out = 0;

	cJSON *decl = decl_json != nullptr
		? cJSON_ParseWithLength(decl_json, static_cast<size_t>(decl_len)) : nullptr;
	cJSON *slots = slots_json != nullptr
		? cJSON_ParseWithLength(slots_json, static_cast<size_t>(slots_len)) : nullptr;
	cJSON *settings = settings_json != nullptr
		? cJSON_ParseWithLength(settings_json, static_cast<size_t>(settings_len)) : nullptr;

	cJSON *out = cJSON_CreateArray();
	if (cJSON_IsArray(decl))
	{
		int32_t index = 0;
		for (const cJSON *entry = decl->child; entry != nullptr; entry = entry->next, index++)
		{
			const cJSON *name = cJSON_GetObjectItemCaseSensitive(entry, "name");
			if (!cJSON_IsString(name)) continue;
			/* same rules as the firmware tree: an entry without a condition is
			 * always exposed; variants of one name are separate entries with
			 * disjoint conditions, the index says which applies */
			const cJSON *when = cJSON_GetObjectItemCaseSensitive(entry, "exposedWhen");
			if (when != nullptr && !evalCondition(when, slots, settings)) continue;
			cJSON *item = cJSON_CreateObject();
			cJSON_AddStringToObject(item, "name", name->valuestring);
			cJSON_AddNumberToObject(item, "index", index);
			cJSON_AddItemToArray(out, item);
		}
	}

	char *text = cJSON_PrintUnformatted(out);
	g_evaluated = text != nullptr ? text : "[]";
	if (text != nullptr) cJSON_free(text);
	cJSON_Delete(out);
	cJSON_Delete(decl);
	cJSON_Delete(slots);
	cJSON_Delete(settings);

	if (len_out != nullptr) *len_out = g_evaluated.size();
	return g_evaluated.c_str();
}

} // extern "C"
