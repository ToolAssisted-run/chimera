#include "chimera/vm/branch_state.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace chimera::vm::branch_state {

namespace fs = std::filesystem;

namespace {
constexpr const char *kFirstLine = "chimera branch state note 1";
}

std::string utf8(const fs::path &path)
{
	const std::u8string s = path.u8string();
	return std::string(reinterpret_cast<const char *>(s.data()), s.size());
}

std::string fingerprint(std::string_view text)
{
	uint64_t h = 1469598103934665603ull;
	for (const char c : text)
	{
		h ^= static_cast<uint8_t>(c);
		h *= 1099511628211ull;
	}
	char buf[17];
	std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
	return buf;
}

std::string nameFor(const std::string &time, int64_t frame, const std::string &text)
{
	return fingerprint(time + "\n" + std::to_string(frame) + "\n" + text);
}

/* The note is a few lines of text, then the picture's pixels as they are in
 * memory. */
bool writeNote(const fs::path &path, const Note &note)
{
	fs::path writing = path;
	writing += ".writing";
	{
		std::ofstream out(writing, std::ios::binary | std::ios::trunc);
		if (!out) return false;
		char aspect[32];
		std::snprintf(aspect, sizeof aspect, "%.17g", note.picture.aspect());
		/* a picture whose pixels are not all there is written as none: its
		 * size with no pixels after it was a note no read would take */
		const size_t n = static_cast<size_t>(note.picture.width) * static_cast<size_t>(note.picture.height);
		const bool whole = n != 0 && note.picture.pixels.size() == n;
		out << kFirstLine << "\n"
			<< "machine " << note.machine << "\n"
			<< "frame " << note.frame << "\n"
			<< "picture " << (whole ? note.picture.width : 0) << " " << (whole ? note.picture.height : 0) << " " << aspect << "\n";
		if (whole)
			out.write(reinterpret_cast<const char *>(note.picture.pixels.data()), static_cast<std::streamsize>(n * sizeof(uint32_t)));
		if (!out) return false;
	}
	std::error_code ec;
	fs::rename(writing, path, ec);
	if (ec) fs::remove(writing, ec);
	return !ec;
}

bool readNote(const fs::path &path, Note &out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) return false;
	std::string first, line;
	if (!std::getline(in, first) || first != kFirstLine) return false;
	Note n;
	int32_t w = 0, h = 0;
	double aspect = 0;
	for (int i = 0; i < 3 && std::getline(in, line); i++)
	{
		std::istringstream words(line);
		std::string key;
		words >> key;
		if (key == "machine") words >> n.machine;
		else if (key == "frame") words >> n.frame;
		else if (key == "picture") words >> w >> h >> aspect;
	}
	if (n.machine.empty() || n.frame < 0 || w < 0 || h < 0 || w > 16384 || h > 16384) return false;
	const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
	if (count != 0)
	{
		n.picture.pixels.resize(count);
		in.read(reinterpret_cast<char *>(n.picture.pixels.data()), static_cast<std::streamsize>(count * sizeof(uint32_t)));
		if (static_cast<size_t>(in.gcount()) != count * sizeof(uint32_t)) return false;
		n.picture.width = w;
		n.picture.height = h;
		/* the note keeps the shape: its height at that shape */
		n.picture.shownHeight = h;
		n.picture.shownWidth = h * (aspect > 0 ? aspect : static_cast<double>(w) / h);
	}
	out = std::move(n);
	return true;
}

} // namespace chimera::vm::branch_state
