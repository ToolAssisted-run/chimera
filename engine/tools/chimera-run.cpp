/* chimera-run - the engine's headless runner.
 *
 * Loads a core package, a rom, and a movie input log; runs the movie; dumps
 * the machine's memory domains. This is the witness gate's Level B without a
 * frontend: no Mono, no WinForms, no display - the same package, the same
 * host, the same inputs, and the dumps must be byte-identical to the goldens
 * the managed frontend produces.
 *
 *   chimera-run <package> <rom> <movie.txt>
 *       [--rerecord] [--seek <frame>] [--settings <json>]
 *       [--dump <domain>=<path>]... [--meta <path>]
 *
 * --rerecord round-trips the whole machine through save/load state around
 * every frame, which must not change anything - that is the point.
 * --seek plays the movie to its end, seeks BACK to the given frame through
 * the greenzone, and plays to the end again - and that must not change
 * anything either.
 */

#include "chimera/engine.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

bool readWholeFile(const char *path, std::vector<uint8_t> &out)
{
	FILE *f = std::fopen(path, "rb");
	if (f == nullptr) return false;
	uint8_t chunk[1 << 16];
	size_t got;
	out.clear();
	while ((got = std::fread(chunk, 1, sizeof chunk, f)) != 0) out.insert(out.end(), chunk, chunk + got);
	bool ok = std::ferror(f) == 0;
	std::fclose(f);
	return ok;
}

bool writeWholeFile(const std::string &path, const uint8_t *data, size_t len)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (f == nullptr) return false;
	bool ok = len == 0 || std::fwrite(data, 1, len, f) == len;
	std::fclose(f);
	return ok;
}

int fail(const std::string &metaPath, const std::string &detail)
{
	if (!metaPath.empty())
	{
		std::string meta = "status=ERROR\ndetail=" + detail + "\nframes=0\nstartframe=-1\n";
		writeWholeFile(metaPath, reinterpret_cast<const uint8_t *>(meta.data()), meta.size());
	}
	std::fprintf(stderr, "chimera-run: %s\n", detail.c_str());
	return 2;
}

} // namespace

int main(int argc, char **argv)
{
	const char *packagePath = nullptr, *romPath = nullptr, *moviePath = nullptr;
	const char *settings = nullptr;
	std::string metaPath;
	std::vector<std::pair<std::string, std::string>> dumps; // domain -> path
	bool rerecord = false;
	int64_t seekFrame = -1;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--rerecord") rerecord = true;
		else if (arg == "--seek" && i + 1 < argc) seekFrame = std::atoll(argv[++i]);
		else if (arg == "--settings" && i + 1 < argc) settings = argv[++i];
		else if (arg == "--meta" && i + 1 < argc) metaPath = argv[++i];
		else if (arg == "--dump" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--dump wants <domain>=<path>");
			dumps.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
		}
		else if (packagePath == nullptr) packagePath = argv[i];
		else if (romPath == nullptr) romPath = argv[i];
		else if (moviePath == nullptr) moviePath = argv[i];
		else return fail(metaPath, "unexpected argument: " + arg);
	}
	if (moviePath == nullptr)
	{
		std::fprintf(stderr, "usage: chimera-run <package> <rom> <movie.txt> [--rerecord] [--settings <json>] [--dump <domain>=<path>]... [--meta <path>]\n");
		return 1;
	}

	std::vector<uint8_t> rom, movieText;
	if (!readWholeFile(romPath, rom)) return fail(metaPath, std::string("could not read rom ") + romPath);
	if (!readWholeFile(moviePath, movieText)) return fail(metaPath, std::string("could not read movie ") + moviePath);

	ce_movie_log *movie = ce_movie_log_new();
	if (ce_movie_log_parse(movie, reinterpret_cast<const char *>(movieText.data()), movieText.size()) != 0)
	{
		return fail(metaPath, std::string("movie: ") + ce_movie_log_last_error(movie));
	}

	const char *error = nullptr;
	ce_session *session = ce_session_open(
		packagePath, rom.data(), rom.size(), settings, nullptr, nullptr, nullptr, 0, &error);
	if (session == nullptr) return fail(metaPath, error != nullptr ? error : "session open failed");

	int64_t frames = ce_movie_log_count(movie);

	/* the movie is the SESSION's from here: the engine parses entries, tracks
	 * the frame position, and (with --seek) keeps the greenzone */
	if (ce_session_movie_load(session, movie) != 0) return fail(metaPath, "could not load the movie into the session");
	if (seekFrame >= 0) ce_session_greenzone_enable(session, 256ull << 20);

	std::vector<uint8_t> state;
	if (rerecord)
	{
		uint64_t len = 0;
		const uint8_t *p = ce_session_save_state(session, &len);
		if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
		state.assign(p, p + len);
	}

	for (int64_t i = 0; i < frames; i++)
	{
		if (rerecord && ce_session_load_state(session, state.data(), state.size()) != 0)
		{
			return fail(metaPath, ce_session_last_error(session));
		}
		if (ce_session_movie_advance(session, 0, nullptr, 0) < 0)
		{
			return fail(metaPath, ce_session_last_error(session));
		}
		if (rerecord)
		{
			uint64_t len = 0;
			const uint8_t *p = ce_session_save_state(session, &len);
			if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
			state.assign(p, p + len);
		}
	}

	if (seekFrame >= 0)
	{
		/* back through the greenzone, then forward again: the dumps at the end
		 * must be the dumps a straight run produces. The invalidate mimics an
		 * input edit and forces the forward seek to actually REPLAY - otherwise
		 * it would just restore the cached end state and prove nothing. */
		if (ce_session_seek(session, seekFrame) != 0) return fail(metaPath, ce_session_last_error(session));
		if (ce_session_frame(session) != seekFrame) return fail(metaPath, "seek landed on the wrong frame");
		ce_session_greenzone_invalidate(session, seekFrame);
		if (ce_session_seek(session, frames) != 0) return fail(metaPath, ce_session_last_error(session));
		if (ce_session_frame(session) != frames) return fail(metaPath, "replay landed on the wrong frame");
	}

	for (const auto &dump : dumps)
	{
		int32_t count = ce_session_domain_count(session);
		int32_t found = -1;
		for (int32_t d = 0; d < count; d++)
		{
			if (dump.first == ce_session_domain_name(session, d))
			{
				found = d;
				break;
			}
		}
		if (found < 0) return fail(metaPath, "no memory domain named " + dump.first);
		int64_t size = ce_session_domain_size(session, found);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		ce_session_domain_read(session, found, 0, bytes.data(), size);
		if (!writeWholeFile(dump.second, bytes.data(), bytes.size()))
		{
			return fail(metaPath, "could not write " + dump.second);
		}
	}

	if (!metaPath.empty())
	{
		std::string meta = "status=OK\ndetail=\nframes=" + std::to_string(frames) + "\nstartframe=0\n";
		writeWholeFile(metaPath, reinterpret_cast<const uint8_t *>(meta.data()), meta.size());
	}
	std::printf("frames=%lld\n", static_cast<long long>(frames));
	ce_movie_log_free(movie);
	ce_session_free(session);
	return 0;
}
