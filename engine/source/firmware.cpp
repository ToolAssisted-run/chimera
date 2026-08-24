/* firmware.cpp - is this file the firmware the core asked for, and the
 * canonical firmware line a movie records. The declarations, the config that
 * remembers paths, and the filesystem stay with the caller.
 */

#include "chimera/engine.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

thread_local std::string g_record;

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

} // extern "C"
