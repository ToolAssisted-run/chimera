/* state_format.cpp - see state_format.hpp. */

#include "state_format.hpp"
#include "thread_string.hpp"

#include "chimera/engine.h"

#include <cstring>
#include <cstdlib>

#ifndef CE_BUILD_COMMIT
#define CE_BUILD_COMMIT "unknown"
#endif

namespace chimera
{

namespace
{

void putU32(std::vector<uint8_t> &out, uint32_t v)
{
	/* little-endian on the wire whatever the host is: a state file is written
	 * on one machine and read on another often enough to be worth it, and the
	 * rest of the file already travels (zstd frames are endian-neutral) */
	out.push_back(static_cast<uint8_t>(v & 0xffu));
	out.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
	out.push_back(static_cast<uint8_t>((v >> 16) & 0xffu));
	out.push_back(static_cast<uint8_t>((v >> 24) & 0xffu));
}

bool takeU32(const uint8_t *data, size_t len, size_t &at, uint32_t &v)
{
	if (len - at < 4) return false;
	v = static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8)
		| (static_cast<uint32_t>(data[at + 2]) << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
	at += 4;
	return true;
}

constexpr size_t kMagicLen = sizeof CE_STATE_FILE_MAGIC - 1;
static_assert(sizeof CE_STATE_FILE_MAGIC == sizeof CE_STATE_FILE_MAGIC_V1,
	"the two state-file magics must be the same length: a reader sniffs one read of both");

/* A tag is the frontend's counters and a writer id is a commit; neither is a
 * place to keep things, and a file claiming otherwise is damaged rather than
 * merely of another format. */
constexpr uint32_t kMaxTag = 1u << 20;
constexpr uint32_t kMaxWriter = 4096;

/* Days since 1970-01-01 for a YYYY-MM-DD date, or INT64_MIN when it is not one.
 * Proleptic Gregorian, which every date a git commit carries is. */
int64_t daysOf(const std::string &date)
{
	if (date.size() != 10 || date[4] != '-' || date[7] != '-') return INT64_MIN;
	for (size_t i = 0; i < date.size(); i++)
	{
		if (i == 4 || i == 7) continue;
		if (date[i] < '0' || date[i] > '9') return INT64_MIN;
	}
	const int64_t y = std::atoi(date.substr(0, 4).c_str());
	const int64_t m = std::atoi(date.substr(5, 2).c_str());
	const int64_t d = std::atoi(date.substr(8, 2).c_str());
	if (m < 1 || m > 12 || d < 1 || d > 31 || y < 1) return INT64_MIN;
	/* Howard Hinnant's days_from_civil */
	const int64_t yy = y - (m <= 2 ? 1 : 0);
	const int64_t era = (yy >= 0 ? yy : yy - 399) / 400;
	const int64_t yoe = yy - era * 400;
	const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}

std::string named(const std::string &build)
{
	return build.empty() ? std::string("a build that does not say") : build;
}

} // namespace

void writeStateFileHeader(const StateFileHeader &h, std::vector<uint8_t> &out)
{
	out.insert(out.end(), CE_STATE_FILE_MAGIC, CE_STATE_FILE_MAGIC + kMagicLen);
	putU32(out, h.format);
	putU32(out, static_cast<uint32_t>(h.writer.size()));
	out.insert(out.end(), h.writer.begin(), h.writer.end());
	out.push_back(h.compressed ? 1 : 0);
	putU32(out, static_cast<uint32_t>(h.tag.size()));
	out.insert(out.end(), h.tag.begin(), h.tag.end());
}

int readStateFileHeader(const uint8_t *data, size_t len, StateFileHeader &h, std::string &why)
{
	h = StateFileHeader();
	why.clear();
	if (data == nullptr || len < kMagicLen)
	{
		why = "is not a state file";
		return 2;
	}
	const bool numbered = std::memcmp(data, CE_STATE_FILE_MAGIC, kMagicLen) == 0;
	const bool legacy = !numbered && std::memcmp(data, CE_STATE_FILE_MAGIC_V1, kMagicLen) == 0;
	if (!numbered && !legacy)
	{
		why = "is not a state file";
		return 2;
	}

	size_t at = kMagicLen;
	uint32_t writerLen = 0;
	if (numbered)
	{
		if (!takeU32(data, len, at, h.format) || !takeU32(data, len, at, writerLen)
			|| writerLen > kMaxWriter || len - at < writerLen)
		{
			why = "is a state file cut short";
			return 2;
		}
		h.writer.assign(reinterpret_cast<const char *>(data + at), writerLen);
		at += writerLen;
	}
	else
	{
		/* A state from before states carried a number. 1 is what the layout was
		 * on the day the number arrived, so this build - still on 1 - reads
		 * every one of them, and the first bump is what starts refusing them
		 * (state_format.hpp). */
		h.format = 1;
		h.writer.clear();
	}

	if (len - at < 1)
	{
		why = "is a state file cut short";
		return 2;
	}
	h.compressed = data[at++] != 0;
	uint32_t tagLen = 0;
	if (!takeU32(data, len, at, tagLen) || tagLen > kMaxTag || len - at < tagLen)
	{
		why = "is a state file cut short";
		return 2;
	}
	h.tag.assign(data + at, data + at + tagLen);
	at += tagLen;
	h.length = at;

	if (h.format != kStateFormat)
	{
		/* A whole sentence, capitalised: it is shown to a person as it stands,
		 * on its own or after a lead line, and never with a path in front. */
		why = "This state was written in savestate format " + std::to_string(h.format)
			+ " by " + (h.writer.empty() ? std::string("a build from before states said which") : h.writer)
			+ ", and this Chimera (" + stateWriterId() + ") writes and reads format "
			+ std::to_string(kStateFormat)
			+ ". The machine's layout changed between the two, so the state cannot be loaded;"
			+ " it was not touched.";
		return 1;
	}
	return 0;
}

const char *stateWriterId() { return "Chimera commit " CE_BUILD_COMMIT; }

std::string historyFormatSuffix()
{
	return "#f" + std::to_string(static_cast<unsigned long long>(kStateFormat));
}

std::string versionSkewMessage(
	const std::string &frontendDate, const std::string &frontendBuild,
	const std::string &coreDate, const std::string &coreBuild, const std::string &coreName)
{
	const int64_t a = daysOf(frontendDate);
	const int64_t b = daysOf(coreDate);
	if (a == INT64_MIN || b == INT64_MIN) return ""; /* one side does not say: say nothing */
	const int64_t apart = a > b ? a - b : b - a;
	if (apart < kVersionSkewDays) return "";

	const std::string core = coreName.empty() ? std::string("core") : coreName;
	std::string out = "This Chimera was built on " + frontendDate + " (" + named(frontendBuild)
		+ ") and the " + core + " package this project runs on was built on " + coreDate
		+ " (" + named(coreBuild) + ") - " + std::to_string(static_cast<long long>(apart))
		+ " days apart. A savestate's layout can change between builds, so cached states may be"
		+ " rebuilt and a branch may refuse to load. If anything looks wrong, install the "
		+ core + " build from the same week as this Chimera (File > Core Manager), or update Chimera.";
	return out;
}

} // namespace chimera

/* ---- the C ABI ---- */

extern "C" {

uint32_t ce_state_format(void) { return chimera::kStateFormat; }

const char *ce_state_writer_id(void) { return chimera::stateWriterId(); }

const char *ce_version_skew(
	const char *frontend_date, const char *frontend_build,
	const char *core_date, const char *core_build, const char *core_name)
{
	static thread_local chimera::ThreadString message;   /* never destroyed: see thread_string.hpp */
	*message = chimera::versionSkewMessage(
		frontend_date != nullptr ? frontend_date : "",
		frontend_build != nullptr ? frontend_build : "",
		core_date != nullptr ? core_date : "",
		core_build != nullptr ? core_build : "",
		core_name != nullptr ? core_name : "");
	return message->empty() ? nullptr : message->c_str();
}

} // extern "C"
