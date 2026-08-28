#include "manifest_util.hpp"

#include "chimera/engine.h"

namespace chimera {
namespace manifest {

bool bareName(const std::string &n)
{
	if (n.empty()) return false;
	if (n == "." || n == "..") return false;
	return n.find('/') == std::string::npos && n.find('\\') == std::string::npos;
}

bool validSlot(const std::string &s)
{
	if (s.empty()) return false;
	for (char c : s)
	{
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
	}
	return true;
}

std::string upperHex(std::string s)
{
	for (char &c : s)
	{
		if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
	}
	return s;
}

bool validSha1(const std::string &s)
{
	if (s.size() != 40) return false;
	for (char c : s)
	{
		if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) return false;
	}
	return true;
}

std::string folderOf(const std::string &path)
{
	size_t cut = path.find_last_of("/\\");
	return cut == std::string::npos ? std::string() : path.substr(0, cut + 1);
}

std::vector<std::string> cueReferences(const std::vector<uint8_t> &bytes)
{
	std::vector<std::string> out;
	std::string text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t eol = text.find('\n', pos);
		if (eol == std::string::npos) eol = text.size();
		std::string line = text.substr(pos, eol - pos);
		pos = eol + 1;

		size_t at = 0;
		while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) at++;
		if (line.compare(at, 4, "FILE") != 0) continue;
		at += 4;
		while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) at++;
		std::string name;
		if (at < line.size() && line[at] == '"')
		{
			size_t close = line.find('"', at + 1);
			if (close == std::string::npos) continue;
			name = line.substr(at + 1, close - at - 1);
		}
		else
		{
			size_t end = at;
			while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r') end++;
			name = line.substr(at, end - at);
		}
		if (!name.empty()) out.push_back(name);
	}
	return out;
}

bool hasCueSuffix(const std::string &name)
{
	if (name.size() < 4) return false;
	std::string tail = name.substr(name.size() - 4);
	for (char &c : tail)
	{
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	}
	return tail == ".cue";
}

std::string encodeName(const std::string &name)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string out;
	for (unsigned char c : name)
	{
		if (c < 0x21 || c > 0x7E || c == '%' || c == '=' || c == ':')
		{
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 0xF];
		}
		else out += static_cast<char>(c);
	}
	return out;
}

void hashInto(const std::vector<uint8_t> &bytes, std::string &out)
{
	char hex[41];
	ce_sha1_hex(bytes.data(), bytes.size(), hex);
	out = hex;
}

} // namespace manifest
} // namespace chimera
