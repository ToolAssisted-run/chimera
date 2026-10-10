/* game_properties.cpp - a game core's properties (docs/game-cores.md). */

#include "game_properties.hpp"
#include "plain_numbers.hpp"

#include "../../extern/cjson/cJSON.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
struct TypeInfo
{
	const char *name;
	uint32_t size; // 0: given by "length"
	bool integer, isSigned;
};

const TypeInfo kTypes[] = {
	{ "u8", 1, true, false }, { "s8", 1, true, true },
	{ "u16", 2, true, false }, { "s16", 2, true, true },
	{ "u32", 4, true, false }, { "s32", 4, true, true },
	{ "u64", 8, true, false }, { "s64", 8, true, true },
	{ "f32", 4, false, false }, { "f64", 8, false, false },
	{ "bool", 1, false, false },
	{ "string", 0, false, false }, { "bytes", 0, false, false },
};

const char *const kEncodings[] = { "ascii", "latin1", "utf8", "utf16le" };

std::string lower(const std::string &s)
{
	std::string out = s;
	for (char &c : out)
	{
		if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
	}
	return out;
}

std::string trim(const std::string &s)
{
	size_t a = 0, b = s.size();
	while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) a++;
	while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) b--;
	return s.substr(a, b - a);
}

uint64_t mask(uint32_t bits) { return bits >= 64 ? ~0ull : (1ull << bits) - 1; }

int64_t signExtend(uint64_t raw, uint32_t bits)
{
	if (bits >= 64) return (int64_t)raw;
	const uint64_t sign = 1ull << (bits - 1);
	raw &= mask(bits);
	return (int64_t)((raw ^ sign) - sign);
}

/* A JSON member that must be a whole number, when it is there at all. */
bool integerMember(const cJSON *obj, const char *key, int64_t &out, bool &present)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	present = item != nullptr;
	if (!present) return true;
	if (!cJSON_IsNumber(item)) return false;
	const double d = item->valuedouble;
	if (d != std::floor(d) || d < -9.2e18 || d > 9.2e18) return false;
	out = (int64_t)d;
	return true;
}

std::string stringMember(const cJSON *obj, const char *key)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsString(item) ? item->valuestring : "";
}

void appendUtf8(std::string &out, uint32_t cp)
{
	if (cp < 0x80) out += char(cp);
	else if (cp < 0x800)
	{
		out += char(0xC0 | (cp >> 6));
		out += char(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		out += char(0xE0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
	else
	{
		out += char(0xF0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 0x3F));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
}

/* UTF-8 into code points, each with the byte it starts at; false for text
 * that is not UTF-8. */
bool decodeUtf8(const std::string &s, std::vector<std::pair<uint32_t, size_t>> &out)
{
	for (size_t i = 0; i < s.size();)
	{
		const auto c = (unsigned char)s[i];
		uint32_t cp;
		size_t n;
		if (c < 0x80) { cp = c; n = 1; }
		else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; n = 2; }
		else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; n = 3; }
		else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; n = 4; }
		else return false;
		if (i + n > s.size()) return false;
		for (size_t k = 1; k < n; k++)
		{
			const auto cc = (unsigned char)s[i + k];
			if ((cc & 0xC0) != 0x80) return false;
			cp = (cp << 6) | (cc & 0x3F);
		}
		out.emplace_back(cp, i);
		i += n;
	}
	return true;
}

/* The fewest significant digits that read back as the same value. */
std::string shortestFloat(double v, bool single)
{
	/* in no locale (plain_numbers.hpp): the same text on every machine */
	std::string text;
	for (int digits = 1; digits <= 17; digits++)
	{
		text = chimera::formatPlainDouble(v, digits);
		double back = 0;
		if (!chimera::parsePlainDouble(text, back)) continue;
		if (single ? (float)back == (float)v : back == v) break;
	}
	return text;
}

/* A whole number as text: decimal with an optional sign, or hex after 0x.
 * `hex` says which, for the range check that follows. */
bool parseInteger(const std::string &text, bool &negative, uint64_t &magnitude, bool &hex)
{
	std::string t = trim(text);
	negative = false;
	hex = false;
	if (!t.empty() && (t[0] == '-' || t[0] == '+'))
	{
		negative = t[0] == '-';
		t = t.substr(1);
	}
	if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X'))
	{
		hex = true;
		t = t.substr(2);
	}
	if (t.empty()) return false;
	magnitude = 0;
	for (char c : t)
	{
		unsigned d;
		if (c >= '0' && c <= '9') d = unsigned(c - '0');
		else if (hex && c >= 'a' && c <= 'f') d = unsigned(c - 'a' + 10);
		else if (hex && c >= 'A' && c <= 'F') d = unsigned(c - 'A' + 10);
		else return false;
		const uint64_t base = hex ? 16 : 10;
		if (magnitude > (~0ull - d) / base) return false;
		magnitude = magnitude * base + d;
	}
	return true;
}
} // namespace

const char *CeGameProperties::typeName(Type type) { return kTypes[type].name; }

/* One entry of a table, checked against the domains: `p` as the engine will
 * keep it, or why it cannot. The name is the caller's to check (empty, taken). */
bool CeGameProperties::parseEntry(const cJSON *entry, Property &p, std::string &why) const
{
	p.name = stringMember(entry, "name");
	const std::string type = stringMember(entry, "type");
	int t = -1;
	for (int k = 0; k < int(sizeof kTypes / sizeof kTypes[0]); k++)
	{
		if (type == kTypes[k].name) t = k;
	}
	if (t < 0)
	{
		why = "has a type this engine does not read: \"" + type + "\"";
		return false;
	}
	p.type = Type(t);
	const TypeInfo &info = kTypes[t];

	p.domain = stringMember(entry, "domain");
	for (size_t d = 0; d < m_domains.size(); d++)
	{
		if (m_domains[d].name == p.domain) p.domainIndex = int32_t(d);
	}
	if (p.domainIndex < 0)
	{
		why = "is in \"" + p.domain + "\", which the core has no domain called";
		return false;
	}
	const Domain &dom = m_domains[size_t(p.domainIndex)];
	if (dom.base == nullptr && !dom.read)
	{
		why = "is in \"" + p.domain + "\", which has no memory of its own to read";
		return false;
	}

	int64_t offset = 0, length = 0, count = 1, stride = 0, bit = 0, bits = 0;
	bool has = false, hasLength = false, hasStride = false, hasBit = false, hasBits = false;
	if (!integerMember(entry, "offset", offset, has) || !has || offset < 0)
	{
		why = "has no whole-number offset";
		return false;
	}
	if (!integerMember(entry, "length", length, hasLength))
	{
		why = "has a length that is not a whole number";
		return false;
	}
	if (info.size == 0)
	{
		if (!hasLength || length < 1 || length > 0x10000000)
		{
			why = std::string("is a ") + info.name + " with no length in bytes";
			return false;
		}
		p.size = uint32_t(length);
	}
	else
	{
		p.size = info.size;
	}
	if (!integerMember(entry, "count", count, has) || count < 1 || count > 0x10000000)
	{
		why = "has a count that is not a whole number of at least 1";
		return false;
	}
	p.count = uint32_t(count);
	int64_t first = 0;
	if (!integerMember(entry, "first", first, has) || first < 0 || first > 0x7FFFFFFF)
	{
		why = "has a first index that is not a whole number of at least 0";
		return false;
	}
	p.first = uint32_t(first);
	if (!integerMember(entry, "stride", stride, hasStride) || (hasStride && (stride < p.size || stride > 0x10000000)))
	{
		why = "has a stride shorter than one element";
		return false;
	}
	p.stride = hasStride ? uint32_t(stride) : p.size;

	const std::string endian = stringMember(entry, "endian");
	if (!endian.empty() && endian != "little" && endian != "big")
	{
		why = "has an endian that is neither little nor big: \"" + endian + "\"";
		return false;
	}
	p.bigEndian = endian == "big";

	if (p.type == String)
	{
		const std::string enc = stringMember(entry, "encoding");
		int e = enc.empty() ? int(Ascii) : -1;
		for (int k = 0; k < 4; k++)
		{
			if (enc == kEncodings[k]) e = k;
		}
		if (e < 0)
		{
			why = "has an encoding this engine does not read: \"" + enc + "\"";
			return false;
		}
		p.encoding = Encoding(e);
	}

	if (!integerMember(entry, "bit", bit, hasBit) || !integerMember(entry, "bits", bits, hasBits))
	{
		why = "has a bit field that is not whole numbers";
		return false;
	}
	if (hasBit || hasBits)
	{
		if (!info.integer)
		{
			why = "is a bit field of a type that is not an integer";
			return false;
		}
		if (!hasBits || bits < 1 || bit < 0 || bit + bits > int64_t(p.size) * 8)
		{
			why = "has a bit field that does not fit in its " + std::string(info.name);
			return false;
		}
		p.bit = uint32_t(bit);
		p.bits = uint32_t(bits);
	}

	p.offset = offset;
	if (p.span() > dom.size - offset)
	{
		why = "at " + std::to_string(offset) + " runs past the end of \"" + p.domain + "\" ("
			+ std::to_string(dom.size) + " bytes)";
		return false;
	}

	p.group = stringMember(entry, "group");
	p.description = stringMember(entry, "description");
	const cJSON *writable = cJSON_GetObjectItemCaseSensitive(entry, "writable");
	p.writable = !cJSON_IsFalse(writable) && dom.writable;

	const cJSON *values = cJSON_GetObjectItemCaseSensitive(entry, "values");
	if (cJSON_IsObject(values) && info.integer)
	{
		const cJSON *v = nullptr;
		cJSON_ArrayForEach(v, values)
		{
			char *end = nullptr;
			errno = 0;
			const long long key = std::strtoll(v->string, &end, 10);
			if (cJSON_IsString(v) && end != v->string && *end == '\0' && errno == 0)
			{
				p.values.emplace_back(int64_t(key), v->valuestring);
			}
		}
	}

	return true;
}

/* What a name is looked up by. A fixed table's names are a core author's
 * labels for places, and are found whatever their case. A dynamic table's are
 * the game's own, in a language that may tell "score" from "Score": those are
 * exact (chimera#218) - two that differ by case are two properties, and one
 * asked for in another case is not there. */
std::string CeGameProperties::key(const std::string &name) const
{
	return m_dynamic ? name : lower(name);
}

void CeGameProperties::load(const char *json, const std::vector<Domain> &domains)
{
	m_domains = domains;
	m_props.clear();
	m_problems.clear();
	m_byName.clear();
	m_timer = -1;
	m_dynamic = false;
	m_generation++;
	m_describeStale = true;
	if (json == nullptr || json[0] == '\0') return;
	cJSON *root = cJSON_Parse(json);
	m_dynamic = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "dynamic"));
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "properties");
	if (root == nullptr) m_problems.emplace_back("the property table is not readable JSON");
	else if (!cJSON_IsArray(list)) m_problems.emplace_back("the property table has no \"properties\" list");

	const cJSON *entry = nullptr;
	if (cJSON_IsArray(list))
	{
		cJSON_ArrayForEach(entry, list)
		{
			if (!cJSON_IsObject(entry))
			{
				m_problems.emplace_back("an entry that is not an object");
				continue;
			}
			Property p;
			std::string why;
			const std::string name = stringMember(entry, "name");
			if (trim(name).empty())
			{
				m_problems.emplace_back("a property with no name");
				continue;
			}
			if (m_byName.count(key(name)) != 0)
			{
				m_problems.push_back("\"" + name + "\" is named twice; the second is left out");
				continue;
			}
			if (!parseEntry(entry, p, why))
			{
				m_problems.push_back("\"" + name + "\" " + why);
				continue;
			}
			m_byName[key(p.name)] = int32_t(m_props.size());
			m_props.push_back(std::move(p));
		}
	}
	/* the game's own timer: one whole number, its elapsed time in ms */
	const cJSON *timer = cJSON_GetObjectItemCaseSensitive(root, "gameTimer");
	if (timer != nullptr)
	{
		const auto named = cJSON_IsString(timer) ? m_byName.find(key(timer->valuestring)) : m_byName.end();
		const Property *p = named != m_byName.end() ? &m_props[size_t(named->second)] : nullptr;
		if (p != nullptr && kTypes[p->type].integer && p->count == 1) m_timer = named->second;
		else
			m_problems.push_back(std::string("\"gameTimer\" names ") +
				(cJSON_IsString(timer) ? "\"" + std::string(timer->valuestring) + "\"" : "no property") +
				", which is not one property holding a whole number");
	}
	cJSON_Delete(root);
}

int32_t CeGameProperties::relist(const char *json)
{
	m_problems.clear();
	m_generation++;
	m_describeStale = true;
	for (Property &p : m_props) p.listed = p.present = false;
	cJSON *root = json != nullptr && json[0] != '\0' ? cJSON_Parse(json) : nullptr;
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(root, "properties");
	if (json != nullptr && json[0] != '\0' && root == nullptr) m_problems.emplace_back("the property table is not readable JSON");
	int32_t listed = 0;
	std::map<std::string, bool> seen;
	const cJSON *entry = nullptr;
	if (cJSON_IsArray(list))
	{
		cJSON_ArrayForEach(entry, list)
		{
			if (!cJSON_IsObject(entry)) continue;
			Property p;
			std::string why;
			const std::string name = stringMember(entry, "name");
			const std::string key = this->key(name);
			if (trim(name).empty())
			{
				m_problems.emplace_back("a property with no name");
				continue;
			}
			if (seen.count(key) != 0)
			{
				m_problems.push_back("\"" + name + "\" is named twice; the second is left out");
				continue;
			}
			if (!parseEntry(entry, p, why))
			{
				m_problems.push_back("\"" + name + "\" " + why);
				continue;
			}
			seen[key] = true;
			const auto known = m_byName.find(key);
			if (known != m_byName.end())
			{
				m_props[size_t(known->second)] = std::move(p);
			}
			else if (m_props.size() >= MaxProperties)
			{
				if (m_problems.empty() || m_problems.back().rfind("more than", 0) != 0)
					m_problems.push_back("more than " + std::to_string(MaxProperties) + " properties; the rest are left out");
				continue;
			}
			else
			{
				m_byName[key] = int32_t(m_props.size());
				m_props.push_back(std::move(p));
			}
			listed++;
		}
	}
	cJSON_Delete(root);
	return listed;
}

int32_t CeGameProperties::place(const std::string &name, const char *entryJson)
{
	const std::string key = this->key(name);
	const auto known = m_byName.find(key);
	Property p;
	std::string why;
	cJSON *entry = entryJson != nullptr && entryJson[0] != '\0' ? cJSON_Parse(entryJson) : nullptr;
	const bool there = cJSON_IsObject(entry) && this->key(stringMember(entry, "name")) == key && parseEntry(entry, p, why);
	cJSON_Delete(entry);
	if (!there)
	{
		if (known == m_byName.end()) return -1;
		Property &old = m_props[size_t(known->second)];
		if (old.present)
		{
			old.present = old.listed = false;
			m_generation++;
			m_describeStale = true;
		}
		return known->second;
	}
	if (known == m_byName.end())
	{
		if (m_props.size() >= MaxProperties) return -1;
		m_byName[key] = int32_t(m_props.size());
		m_props.push_back(std::move(p));
		m_generation++;
		m_describeStale = true;
		return int32_t(m_props.size()) - 1;
	}
	Property &old = m_props[size_t(known->second)];
	const bool moved = !old.present || old.offset != p.offset || old.type != p.type || old.size != p.size
		|| old.domainIndex != p.domainIndex || old.encoding != p.encoding;
	if (moved)
	{
		p.listed = true;
		old = std::move(p);
		m_generation++;
		m_describeStale = true;
	}
	return known->second;
}

std::string CeGameProperties::timeText(int64_t ms)
{
	const bool negative = ms < 0;
	const uint64_t m = negative ? uint64_t(0) - uint64_t(ms) : uint64_t(ms);
	char buf[48];
	std::snprintf(buf, sizeof buf, "%s%02llu:%02llu.%03llu", negative ? "-" : "",
		(unsigned long long)(m / 60000), (unsigned long long)(m / 1000 % 60), (unsigned long long)(m % 1000));
	return buf;
}

const std::string &CeGameProperties::describe() const
{
	if (m_describeStale) describeAll();
	return m_describe;
}

void CeGameProperties::describeAll() const
{
	m_describeStale = false;
	cJSON *root = cJSON_CreateObject();
	if (m_dynamic) cJSON_AddBoolToObject(root, "dynamic", true);
	cJSON *list = cJSON_AddArrayToObject(root, "properties");
	for (const Property &p : m_props)
	{
		cJSON *o = cJSON_CreateObject();
		cJSON_AddStringToObject(o, "name", p.name.c_str());
		cJSON_AddStringToObject(o, "domain", p.domain.c_str());
		cJSON_AddNumberToObject(o, "offset", double(p.offset));
		cJSON_AddStringToObject(o, "type", typeName(p.type));
		cJSON_AddNumberToObject(o, "size", p.size);
		cJSON_AddNumberToObject(o, "count", p.count);
		cJSON_AddNumberToObject(o, "first", p.first);
		cJSON_AddNumberToObject(o, "stride", p.stride);
		cJSON_AddStringToObject(o, "endian", p.bigEndian ? "big" : "little");
		if (p.type == String) cJSON_AddStringToObject(o, "encoding", kEncodings[p.encoding]);
		cJSON_AddNumberToObject(o, "bit", p.bit);
		cJSON_AddNumberToObject(o, "bits", p.bits);
		cJSON_AddStringToObject(o, "group", p.group.c_str());
		cJSON_AddStringToObject(o, "description", p.description.c_str());
		cJSON_AddBoolToObject(o, "writable", p.writable);
		if (m_dynamic)
		{
			cJSON_AddBoolToObject(o, "listed", p.listed);
			cJSON_AddBoolToObject(o, "present", p.present);
		}
		cJSON *values = cJSON_AddObjectToObject(o, "values");
		for (const auto &v : p.values) cJSON_AddStringToObject(values, std::to_string(v.first).c_str(), v.second.c_str());
		cJSON_AddItemToArray(list, o);
	}
	if (m_timer >= 0) cJSON_AddStringToObject(root, "gameTimer", m_props[size_t(m_timer)].name.c_str());
	cJSON *problems = cJSON_AddArrayToObject(root, "problems");
	for (const std::string &problem : m_problems) cJSON_AddItemToArray(problems, cJSON_CreateString(problem.c_str()));
	char *text = cJSON_PrintUnformatted(root);
	m_describe = text != nullptr ? text : "{\"properties\":[],\"problems\":[]}";
	cJSON_free(text);
	cJSON_Delete(root);
}

int32_t CeGameProperties::find(const std::string &name, uint32_t *element) const
{
	if (element != nullptr) *element = 0;
	const auto whole = m_byName.find(key(name));
	if (whole != m_byName.end()) return whole->second;
	// "Name[3]": an element of an array
	const size_t open = name.rfind('[');
	if (open == std::string::npos || name.size() < open + 3 || name.back() != ']') return -1;
	const auto base = m_byName.find(key(name.substr(0, open)));
	if (base == m_byName.end()) return -1;
	uint64_t index = 0;
	for (size_t k = open + 1; k + 1 < name.size(); k++)
	{
		const char c = name[k];
		if (c < '0' || c > '9' || index > 0xFFFFFFFFull) return -1;
		index = index * 10 + uint64_t(c - '0');
	}
	const Property &p = m_props[size_t(base->second)];
	if (p.count == 1 || index < p.first || index - p.first >= p.count) return -1;
	if (element != nullptr) *element = uint32_t(index - p.first);
	return base->second;
}

int32_t CeGameProperties::at(const std::string &domain, int64_t address, uint32_t *element, bool *starts) const
{
	for (size_t k = 0; k < m_props.size(); k++)
	{
		const Property &p = m_props[k];
		if (p.domain != domain) continue;
		const int64_t rel = address - p.offset;
		if (rel < 0 || rel >= p.span()) continue;
		const int64_t e = rel / p.stride;
		const int64_t within = rel - e * p.stride;
		if (e >= p.count || within >= p.size) continue;
		if (element != nullptr) *element = uint32_t(e);
		if (starts != nullptr) *starts = within == 0;
		return int32_t(k);
	}
	return -1;
}

bool CeGameProperties::valid(int32_t index, uint32_t element) const
{
	return index >= 0 && size_t(index) < m_props.size() && element < m_props[size_t(index)].count
		&& m_props[size_t(index)].present;
}

uint8_t *CeGameProperties::elementBytes(const Property &p, uint32_t element) const
{
	return m_domains[size_t(p.domainIndex)].base + p.elementOffset(element);
}

uint64_t CeGameProperties::readRaw(const Property &p, const uint8_t *at) const
{
	uint64_t raw = 0;
	for (uint32_t k = 0; k < p.size; k++)
	{
		const uint64_t byte = at[p.bigEndian ? k : p.size - 1 - k];
		raw = (raw << 8) | byte;
	}
	return raw;
}

void CeGameProperties::writeRaw(const Property &p, uint8_t *at, uint64_t raw) const
{
	for (uint32_t k = 0; k < p.size; k++)
	{
		at[p.bigEndian ? p.size - 1 - k : k] = uint8_t(raw & 0xFF);
		raw >>= 8;
	}
}

bool CeGameProperties::read(int32_t index, uint32_t element, Value &out) const
{
	if (!valid(index, element)) return false;
	const Property &p = m_props[size_t(index)];
	const Domain &dom = m_domains[size_t(p.domainIndex)];
	std::vector<uint8_t> copy; // a domain with no pointer is read into this
	if (dom.base == nullptr)
	{
		copy.resize(p.size);
		dom.read(p.elementOffset(element), copy.data(), int64_t(p.size));
	}
	const uint8_t *at = dom.base != nullptr ? elementBytes(p, element) : copy.data();
	const TypeInfo &info = kTypes[p.type];
	out = Value{};
	if (info.integer)
	{
		uint64_t raw = readRaw(p, at);
		uint32_t width = p.size * 8;
		if (p.bits != 0)
		{
			raw = (raw >> p.bit) & mask(p.bits);
			width = p.bits;
		}
		if (info.isSigned)
		{
			out.kind = Value::Int;
			out.i = signExtend(raw, width);
		}
		else
		{
			out.kind = Value::UInt;
			out.u = raw & mask(width);
		}
		return true;
	}
	switch (p.type)
	{
	case F32:
	{
		const auto bits32 = uint32_t(readRaw(p, at));
		float f;
		std::memcpy(&f, &bits32, 4);
		out.kind = Value::Float;
		out.f = f;
		return true;
	}
	case F64:
	{
		const uint64_t bits64 = readRaw(p, at);
		std::memcpy(&out.f, &bits64, 8);
		out.kind = Value::Float;
		return true;
	}
	case Bool:
		out.kind = Value::Boolean;
		out.i = at[0] != 0 ? 1 : 0;
		return true;
	case Bytes:
		out.kind = Value::Raw;
		out.data.assign(reinterpret_cast<const char *>(at), p.size);
		return true;
	case String:
		out.kind = Value::Text;
		if (p.encoding == Utf16le)
		{
			for (uint32_t k = 0; k + 1 < p.size; k += 2)
			{
				uint32_t unit = uint32_t(at[k]) | (uint32_t(at[k + 1]) << 8);
				if (unit == 0) break;
				if (unit >= 0xD800 && unit < 0xDC00 && k + 3 < p.size)
				{
					const uint32_t low = uint32_t(at[k + 2]) | (uint32_t(at[k + 3]) << 8);
					if (low >= 0xDC00 && low < 0xE000)
					{
						appendUtf8(out.data, 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00));
						k += 2;
						continue;
					}
				}
				if (unit >= 0xD800 && unit < 0xE000) unit = 0xFFFD; // half a pair
				appendUtf8(out.data, unit);
			}
			return true;
		}
		for (uint32_t k = 0; k < p.size && at[k] != 0; k++)
		{
			if (p.encoding == Utf8) out.data += char(at[k]);
			else appendUtf8(out.data, at[k]); // ascii and latin1: a byte is a code point
		}
		return true;
	default:
		return false;
	}
}

bool CeGameProperties::write(int32_t index, uint32_t element, const Value &in, std::string &error) const
{
	if (!valid(index, element))
	{
		error = index >= 0 && size_t(index) < m_props.size() && !m_props[size_t(index)].present
			? "\"" + m_props[size_t(index)].name + "\" is not there now"
			: "no such property";
		return false;
	}
	const Property &p = m_props[size_t(index)];
	if (!p.writable)
	{
		error = "\"" + p.name + "\" is worked out by the game every step, so setting it would change nothing";
		// a dynamic table's (a movie's variables): the core says which it can only show
		if (m_dynamic) error = "\"" + p.name + "\" can be read but not set: the core lists it read-only";
		if (!m_domains[size_t(p.domainIndex)].writable) error = "\"" + p.name + "\" is in a domain that cannot be written";
		return false;
	}
	const Domain &dom = m_domains[size_t(p.domainIndex)];
	if (dom.base != nullptr) return writeBytes(p, elementBytes(p, element), in, error);
	/* a domain with no pointer: the element out, changed, and back in whole
	 * (a bit field keeps the bits around it that way too) */
	std::vector<uint8_t> copy(p.size);
	dom.read(p.elementOffset(element), copy.data(), int64_t(p.size));
	if (!writeBytes(p, copy.data(), in, error)) return false;
	dom.write(p.elementOffset(element), copy.data(), int64_t(p.size));
	return true;
}

bool CeGameProperties::writeBytes(const Property &p, uint8_t *at, const Value &in, std::string &error) const
{
	const TypeInfo &info = kTypes[p.type];
	const bool numeric = in.kind == Value::Int || in.kind == Value::UInt || in.kind == Value::Float || in.kind == Value::Boolean;

	if (info.integer || p.type == Bool)
	{
		if (!numeric)
		{
			error = "\"" + p.name + "\" takes a number";
			return false;
		}
		if (p.type == Bool)
		{
			const bool truth = in.kind == Value::Float ? in.f != 0 : in.kind == Value::UInt ? in.u != 0 : in.i != 0;
			at[0] = truth ? 1 : 0;
			return true;
		}
		// a number fits when it is in the width as a signed or an unsigned value - so -1
		// sets a u64 to all ones, which is the only way a signed 64-bit caller can - and
		// anything else is refused rather than wrapped into something nobody asked for
		const uint32_t width = p.bits != 0 ? p.bits : p.size * 8;
		bool negative = false;
		uint64_t magnitude = 0;
		if (in.kind == Value::Float)
		{
			const double t = std::trunc(in.f);
			if (!std::isfinite(in.f) || t <= -9.3e18 || t >= 1.9e19)
			{
				error = "\"" + p.name + "\" takes a whole number that fits in it";
				return false;
			}
			negative = t < 0;
			magnitude = negative ? uint64_t(-(t + 1)) + 1 : uint64_t(t);
		}
		else if (in.kind == Value::UInt)
		{
			magnitude = in.u;
		}
		else
		{
			negative = in.i < 0;
			magnitude = negative ? uint64_t(0) - uint64_t(in.i) : uint64_t(in.i);
		}
		const bool fits = negative ? (width >= 64 || magnitude <= (1ull << (width - 1))) : magnitude <= mask(width);
		if (!fits)
		{
			error = std::string(negative ? "-" : "") + std::to_string(magnitude) + " does not fit in \"" + p.name + "\" ("
				+ info.name + (p.bits != 0 ? ", " + std::to_string(p.bits) + " bits" : "") + ")";
			return false;
		}
		const uint64_t v = negative ? uint64_t(0) - magnitude : magnitude;
		if (p.bits != 0)
		{
			const uint64_t m = mask(p.bits) << p.bit;
			const uint64_t raw = (readRaw(p, at) & ~m) | ((v << p.bit) & m);
			writeRaw(p, at, raw);
		}
		else
		{
			writeRaw(p, at, v & mask(p.size * 8));
		}
		return true;
	}
	switch (p.type)
	{
	case F32:
	case F64:
	{
		if (!numeric)
		{
			error = "\"" + p.name + "\" takes a number";
			return false;
		}
		const double d = in.kind == Value::Float ? in.f
			: in.kind == Value::UInt ? double(in.u)
			: double(in.i);
		if (p.type == F32)
		{
			const float f = float(d);
			uint32_t bits32;
			std::memcpy(&bits32, &f, 4);
			writeRaw(p, at, bits32);
		}
		else
		{
			uint64_t bits64;
			std::memcpy(&bits64, &d, 8);
			writeRaw(p, at, bits64);
		}
		return true;
	}
	case Bytes:
		if (in.kind != Value::Raw || in.data.size() != p.size)
		{
			error = "\"" + p.name + "\" takes exactly " + std::to_string(p.size) + " bytes";
			return false;
		}
		std::memcpy(at, in.data.data(), p.size);
		return true;
	case String:
	{
		if (in.kind != Value::Text)
		{
			error = "\"" + p.name + "\" takes text";
			return false;
		}
		std::vector<std::pair<uint32_t, size_t>> cps;
		if (!decodeUtf8(in.data, cps))
		{
			error = "the text for \"" + p.name + "\" is not UTF-8";
			return false;
		}
		std::string bytes;
		for (const auto &cp : cps)
		{
			std::string one;
			switch (p.encoding)
			{
			case Ascii:
			case Latin1:
				if (cp.first > (p.encoding == Ascii ? 0x7Fu : 0xFFu))
				{
					error = "\"" + p.name + "\" is " + kEncodings[p.encoding] + " text, which cannot hold every character given";
					return false;
				}
				one += char(cp.first);
				break;
			case Utf8:
				appendUtf8(one, cp.first);
				break;
			case Utf16le:
				if (cp.first >= 0x10000)
				{
					const uint32_t v = cp.first - 0x10000;
					const uint32_t hi = 0xD800 + (v >> 10), lo = 0xDC00 + (v & 0x3FF);
					one += char(hi & 0xFF);
					one += char(hi >> 8);
					one += char(lo & 0xFF);
					one += char(lo >> 8);
				}
				else
				{
					one += char(cp.first & 0xFF);
					one += char(cp.first >> 8);
				}
				break;
			}
			if (bytes.size() + one.size() > p.size) break; // cut at a whole character
			bytes += one;
		}
		std::memset(at, 0, p.size);
		std::memcpy(at, bytes.data(), bytes.size());
		return true;
	}
	default:
		error = "no such property";
		return false;
	}
}

std::string CeGameProperties::text(int32_t index, uint32_t element, bool named) const
{
	Value v;
	if (!read(index, element, v)) return "";
	const Property &p = m_props[size_t(index)];
	if (named && (v.kind == Value::Int || v.kind == Value::UInt))
	{
		const int64_t key = v.kind == Value::Int ? v.i : int64_t(v.u);
		for (const auto &value : p.values)
		{
			if (value.first == key) return value.second;
		}
	}
	char buf[32];
	switch (v.kind)
	{
	case Value::Int:
		std::snprintf(buf, sizeof buf, "%lld", (long long)v.i);
		return buf;
	case Value::UInt:
		std::snprintf(buf, sizeof buf, "%llu", (unsigned long long)v.u);
		return buf;
	case Value::Float:
		return shortestFloat(v.f, p.type == F32);
	case Value::Boolean:
		return v.i != 0 ? "true" : "false";
	case Value::Text:
		return v.data;
	case Value::Raw:
	{
		std::string out;
		for (size_t k = 0; k < v.data.size(); k++)
		{
			std::snprintf(buf, sizeof buf, k == 0 ? "%02X" : " %02X", unsigned((unsigned char)v.data[k]));
			out += buf;
		}
		return out;
	}
	}
	return "";
}

bool CeGameProperties::writeText(int32_t index, uint32_t element, const std::string &text, std::string &error) const
{
	if (!valid(index, element))
	{
		error = index >= 0 && size_t(index) < m_props.size() && !m_props[size_t(index)].present
			? "\"" + m_props[size_t(index)].name + "\" is not there now"
			: "no such property";
		return false;
	}
	const Property &p = m_props[size_t(index)];
	const TypeInfo &info = kTypes[p.type];
	Value v;
	if (info.integer)
	{
		const std::string t = trim(text);
		for (const auto &value : p.values)
		{
			if (lower(value.second) == lower(t))
			{
				v.kind = Value::Int;
				v.i = value.first;
				return write(index, element, v, error);
			}
		}
		bool negative, hex;
		uint64_t magnitude;
		if (!parseInteger(t, negative, magnitude, hex))
		{
			error = "\"" + t + "\" is not a whole number" + (p.values.empty() ? "" : " nor one of the names of \"" + p.name + "\"'s values");
			return false;
		}
		const uint32_t width = p.bits != 0 ? p.bits : p.size * 8;
		const bool fits = negative
			? info.isSigned && !hex && (width >= 64 ? magnitude <= (1ull << 63) : magnitude <= (1ull << (width - 1)))
			: magnitude <= (info.isSigned && !hex ? mask(width - 1) : mask(width));
		if (!fits)
		{
			error = t + " does not fit in \"" + p.name + "\" (" + info.name + (p.bits != 0 ? ", " + std::to_string(p.bits) + " bits" : "") + ")";
			return false;
		}
		if (negative)
		{
			v.kind = Value::Int;
			v.i = int64_t(uint64_t(0) - magnitude); // the least s64 too
		}
		else
		{
			v.kind = Value::UInt;
			v.u = magnitude;
		}
		return write(index, element, v, error);
	}
	switch (p.type)
	{
	case Bool:
	{
		const std::string t = lower(trim(text));
		if (t == "true" || t == "1") v.i = 1;
		else if (t == "false" || t == "0") v.i = 0;
		else
		{
			error = "\"" + p.name + "\" is true or false";
			return false;
		}
		v.kind = Value::Boolean;
		return write(index, element, v, error);
	}
	case F32:
	case F64:
	{
		const std::string t = trim(text);
		/* a dot, whatever the system's decimal separator is */
		if (!chimera::parsePlainDouble(t, v.f))
		{
			error = "\"" + t + "\" is not a number";
			return false;
		}
		v.kind = Value::Float;
		return write(index, element, v, error);
	}
	case String:
		v.kind = Value::Text;
		v.data = text;
		return write(index, element, v, error);
	case Bytes:
	{
		std::string digits;
		for (size_t k = 0; k < text.size(); k++)
		{
			const char c = text[k];
			if (c == ' ' || c == '\t' || c == ',') continue;
			if (c == '0' && k + 1 < text.size() && (text[k + 1] == 'x' || text[k + 1] == 'X'))
			{
				k++;
				continue;
			}
			if (!std::isxdigit((unsigned char)c))
			{
				error = "\"" + p.name + "\" is " + std::to_string(p.size) + " bytes in hex";
				return false;
			}
			digits += c;
		}
		if (digits.size() != size_t(p.size) * 2)
		{
			error = "\"" + p.name + "\" is " + std::to_string(p.size) + " bytes in hex, not " + std::to_string(digits.size()) + " digits";
			return false;
		}
		v.kind = Value::Raw;
		for (size_t k = 0; k < digits.size(); k += 2) v.data += char(std::strtoul(digits.substr(k, 2).c_str(), nullptr, 16));
		return write(index, element, v, error);
	}
	default:
		error = "no such property";
		return false;
	}
}
