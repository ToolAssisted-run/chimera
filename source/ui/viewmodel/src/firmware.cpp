#include "chimera/vm/firmware.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;

namespace {
std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}
std::string text(const Json &j, const char *key)
{
	const Json *v = j.find(key);
	return v != nullptr && v->kind == Json::Kind::String ? v->text : std::string();
}
/* a file as large as this or larger cannot cross into the sandbox, so it is not firmware */
constexpr int64_t kMostBytes = 0x7fffffff;
} // namespace

std::vector<FirmwareFile> indexFirmware(const std::vector<std::string> &folders, bool recurse, size_t most)
{
	namespace fs = std::filesystem;
	std::vector<FirmwareFile> index;
	std::set<std::string> seen;
	for (const std::string &folder : folders)
	{
		std::error_code ec;
		/* a file chosen by hand stands for itself */
		if (!folder.empty() && fs::is_regular_file(folder, ec))
		{
			const auto size = static_cast<int64_t>(fs::file_size(folder, ec));
			if (!ec && size > 0 && size < kMostBytes && seen.insert(folder).second)
				index.push_back({folder, fs::path(folder).filename().string(), size, {}});
			continue;
		}
		if (folder.empty() || !fs::is_directory(folder, ec)) continue;
		auto take = [&](const fs::directory_entry &e) {
			if (!e.is_regular_file(ec)) return;
			const std::string path = e.path().string();
			const auto size = static_cast<int64_t>(e.file_size(ec));
			if (ec || size <= 0 || size >= kMostBytes || !seen.insert(path).second) return;
			index.push_back({path, e.path().filename().string(), size, {}});
		};
		if (recurse)
		{
			for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
				 it != end && index.size() < most; it.increment(ec))
			{
				if (ec) break;
				take(*it);
			}
		}
		else
			for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; it != end && index.size() < most;
				 it.increment(ec))
			{
				if (ec) break;
				take(*it);
			}
	}
	return index;
}

namespace {
thread_local DeferLargeHashes *g_defer = nullptr;
}

DeferLargeHashes::DeferLargeHashes() : outer_(g_defer) { g_defer = this; }
DeferLargeHashes::~DeferLargeHashes() { g_defer = outer_; }

const std::string &FirmwareFile::hash() const
{
	if (sha1.empty() && g_defer != nullptr && size >= kLargeFile && ce_sha1_file_known(path.c_str(), nullptr, nullptr) != 1)
	{
		/* not read here: the path is noted, and the answer matches nothing */
		g_defer->note(path);
		static thread_local std::string unknown;
		unknown = "?" + path;
		return unknown;
	}
	if (sha1.empty())
	{
		char sha[41] = {};
		uint64_t len = 0;
		if (ce_sha1_file(path.c_str(), sha, &len) == 1) sha1 = sha;
		else sha1 = "-"; /* unreadable: matches nothing */
	}
	return sha1;
}

const FirmwareFile *findFirmware(const Json &decl, const std::string &pinnedSha1, const std::vector<FirmwareFile> &index,
	std::string &how)
{
	const Json *sizeDecl = decl.find("size");
	const int64_t size = sizeDecl != nullptr ? static_cast<int64_t>(sizeDecl->number()) : 0;
	/* a hash, when one is pinned, is the only thing that counts; a pin the
	 * project made of a file of another size (a custom one) is honoured
	 * too, so only the package's own hash narrows by size */
	const std::string sha = !pinnedSha1.empty() ? pinnedSha1 : text(decl, "sha1");
	if (!sha.empty())
	{
		for (const FirmwareFile &f : index)
			if ((!pinnedSha1.empty() || size == 0 || f.size == size) && lower(f.hash()) == lower(sha))
			{
				how = tr(pinnedSha1.empty() ? "firmware.by_hash" : "firmware.by_pinned_hash");
				return &f;
			}
		return nullptr;
	}
	/* by its name, and its size where the package gives one: a file of the
	 * same name and another size is another machine's (issue 379) */
	const std::string name = lower(text(decl, "name"));
	if (!name.empty())
		for (const FirmwareFile &f : index)
			if (lower(f.name) == name && (size == 0 || f.size == size))
			{
				how = tr("firmware.by_name");
				return &f;
			}
	/* the one file the size and the name hints point at */
	const std::vector<const FirmwareFile *> candidates = hintCandidates(decl, index);
	if (candidates.size() != 1) return nullptr;
	how = tr("firmware.by_size_name");
	return candidates[0];
}

std::vector<const FirmwareFile *> hintCandidates(const Json &decl, const std::vector<FirmwareFile> &index)
{
	std::vector<const FirmwareFile *> candidates;
	const Json *hints = decl.find("nameHints");
	if (!text(decl, "sha1").empty() || hints == nullptr || hints->kind != Json::Kind::Array || hints->items.empty()) return candidates;
	const Json *sizeDecl = decl.find("size");
	const int64_t size = sizeDecl != nullptr ? static_cast<int64_t>(sizeDecl->number()) : 0;
	/* one dump in two places is one candidate */
	std::set<std::string> hashes;
	for (const FirmwareFile &f : index)
	{
		if (size != 0 && f.size != size) continue;
		bool hinted = false;
		for (const Json &h : hints->items)
			hinted = hinted || (!h.text.empty() && lower(f.name).find(lower(h.text)) != std::string::npos);
		if (hinted && hashes.insert(lower(f.hash())).second) candidates.push_back(&f);
	}
	return candidates;
}

std::vector<FirmwareNeed> neededFirmware(const Json &firmwareDecl, const Json &settingsDecl, const std::string &slotsJson,
	const Json &settings, const Json &pins, const std::vector<FirmwareFile> &index)
{
	/* the effective settings: the package's defaults, what was chosen over them */
	Json effective;
	effective.kind = Json::Kind::Object;
	for (const Json &d : settingsDecl.items)
		if (const Json *def = d.find("default"); def != nullptr && !text(d, "name").empty())
			effective.members.emplace_back(text(d, "name"), *def);
	for (const auto &[k, v] : settings.members)
	{
		bool replaced = false;
		for (auto &[ek, ev] : effective.members)
			if (ek == k)
			{
				ev = v;
				replaced = true;
			}
		if (!replaced) effective.members.emplace_back(k, v);
	}
	const std::string declText = json::write(firmwareDecl), settingsText = json::write(effective);
	uint64_t len = 0;
	const char *out = ce_firmware_evaluate(declText.data(), declText.size(), slotsJson.data(), slotsJson.size(),
		settingsText.data(), settingsText.size(), &len);
	Json list;
	std::string error;
	std::vector<FirmwareNeed> needs;
	if (out == nullptr || !json::parse(std::string_view(out, static_cast<size_t>(len)), list, error)) return needs;
	for (const Json &e : list.items)
	{
		const Json *i = e.find("index");
		if (i == nullptr || i->number() < 0 || i->number() >= firmwareDecl.items.size()) continue;
		const Json &decl = firmwareDecl.items[static_cast<size_t>(i->number())];
		FirmwareNeed need{text(decl, "id"), text(decl, "display"), static_cast<size_t>(i->number()), {}, {}};
		if (const Json *r = decl.find("required"); r != nullptr && r->kind == Json::Kind::Bool) need.required = r->boolean;
		std::string pinned;
		for (const Json &p : pins.items)
			if (text(p, "id") == need.id) pinned = text(p, "sha1");
		std::string how;
		if (const FirmwareFile *f = findFirmware(decl, pinned, index, how))
		{
			const std::string &h = f->hash();
			/* a file not read yet (DeferLargeHashes) is not found yet */
			if (h.empty() || h[0] != '?')
			{
				need.path = f->path;
				need.how = how;
				need.sha1 = h;
			}
		}
		needs.push_back(std::move(need));
	}
	return needs;
}

} // namespace chimera::vm
