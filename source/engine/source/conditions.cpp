/* conditions.cpp - the one condition language every decision tree speaks
 * (docs/project.md): firmware requirements, sync-setting exposure, and slot
 * availability all evaluate the same {"slot"}, {"setting"}, all/any/not
 * grammar over the slot map and the effective settings.
 */

#include "conditions.hpp"

#include <string>

namespace {


bool valueMatches(const cJSON *want, const cJSON *have)
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

bool slotHasExtension(const cJSON *names, const std::string &ext)
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


} // namespace

/* The condition language (docs/project.md): {"slot": id[, "extension": e]},
 * {"setting": name, "is": v} / {"setting": name, "in": [v...]}, and the
 * combinators {"all": [...]}, {"any": [...]}, {"not": {...}}. Anything
 * malformed evaluates false - a core that misdeclares asks for nothing
 * rather than for everything. */
bool ceEvalCondition(const cJSON *cond, const cJSON *slots, const cJSON *settings)
{
	if (!cJSON_IsObject(cond)) return false;

	const cJSON *sub;
	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "all")) != nullptr)
	{
		if (!cJSON_IsArray(sub)) return false;
		for (const cJSON *c = sub->child; c != nullptr; c = c->next)
		{
			if (!ceEvalCondition(c, slots, settings)) return false;
		}
		return true;
	}
	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "any")) != nullptr)
	{
		if (!cJSON_IsArray(sub)) return false;
		for (const cJSON *c = sub->child; c != nullptr; c = c->next)
		{
			if (ceEvalCondition(c, slots, settings)) return true;
		}
		return false;
	}
	if ((sub = cJSON_GetObjectItemCaseSensitive(cond, "not")) != nullptr)
	{
		return !ceEvalCondition(sub, slots, settings);
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


