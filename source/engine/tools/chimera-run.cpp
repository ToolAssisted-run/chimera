/* chimera-run - the engine's headless runner.
 *
 * Loads a core package, a rom, and a movie input log; runs the movie; dumps
 * the machine's memory domains. This is the witness gate's Level B without a
 * frontend: no Mono, no WinForms, no display - the same package, the same
 * host, the same inputs, and the dumps must be byte-identical to the goldens
 * the managed frontend produces.
 *
 *   chimera-run <package> <rom> <movie.txt>
 *       [--rerecord] [--seek <frame>] [--play <n>] [--edit-from <movie>] [--stop-at-seek] [--bands n,m,ms,fs,anchor] [--record <out.txt>]
 *       [--settings <json>]
 *       [--dump <domain>=<path>]... [--export-savedata <dir>] [--meta <path>]
 *       [--core-log <path>]
 *   chimera-run --project <p.chimeraProject> <package>
 *       [--files <dir>]... [--allow-core-mismatch] [the same run flags]
 *
 * --project runs a .chimeraProject (docs/project.md): the input log and the
 * sync settings come from the project, files resolve from the project's own
 * folder plus any --files dirs, and every hash must match (this tool has no
 * user to knowingly override). The package must match the project's core
 * pin unless --allow-core-mismatch says otherwise; when the package ships
 * file_slots.json, the manifest is validated against it. Mounts: the slot
 * map as "slots", every file under its canonical name, and (transitionally,
 * while cores learn slots) the first file of the first slot as rom/rom.name
 * with that slot's remaining files as rom2..N.
 *
 * --rerecord round-trips the whole machine through save/load state around
 * every frame, which must not change anything - that is the point.
 * --bands sets the history's density at each distance from the playhead;
 * --greenzone <MB> its memory budget and --spill <dir> where the far band
 * goes when that budget is full; --greenzone-disk <MB> bounds that file too
 * (near frames, mid frames, mid stride, far stride, anchor spacing; 0 keeps a
 * default). Its use in a test is to make the bands narrow enough that the
 * history is constantly coarsening, which is what the defaults spend minutes
 * of real play reaching. --greenzone-period <n> enables the history and sets
 * how often it stores a frame (0 off, 1 every frame, n one in n) at the frame
 * of the first pass --greenzone-period-at names (0 by default), to measure
 * what each of TAStudio's "Greenzone" settings costs.
 *
 * --stop-at-seek ends the run where the seek landed, so the dumps describe
 * frame N itself rather than the end of a replay from it.
 *
 * --settle-probe <frame>,<count>[,<tga prefix>] measures how long a renderer
 * is wrong after a load, on the machine and the card it is run on: <count>
 * frames from <frame> are remembered as first drawn, the kept pictures are
 * switched off, the machine is put back on the frame before them and each is
 * drawn again and compared, a line a frame and one for the whole. With
 * --greenzone-period 1 the load lands on the frame before, which is the hard
 * case - every frame that runs unseen after a load is a frame the renderer
 * has had to recover in. The first four pairs are written as TGA files when a
 * prefix is given. A wrong picture that is exactly the one the frame before
 * had is called that, and how many of the frames compared differ from the one
 * before them is said too: a verdict over a still screen is not one.
 * --settle-probe-state goes back by a whole state taken on the frame before
 * and loaded again, with no history at all - the same question when the
 * greenzone is the thing in doubt. The movie is not wound back with the
 * machine, so use it where the movie holds the same input for twice the count.
 *
 * --seek plays the movie to its end, seeks BACK to the given frame through
 * the greenzone, and plays to the end again - and that must not change
 * anything either.
 * --record drives RECORD mode instead of playback: the given movie is only an
 * input source (each entry decoded to buttons/axes), the session generates the
 * log itself, and it is written to <out.txt>. Feeding that file back in as an
 * ordinary movie must reach the same machine - which is what witnesses record
 * mode and entry generation, the paths playback never touches.
 * --history-out <file> keeps the state history, and --history-in <file> starts
 * from one kept earlier - which is what reopening a project does, and the only
 * way to witness that a history outlives the process that made it.
 * --export-savedata writes the core's exported save-data tree under <dir>
 * after the run (docs/save-data.md) - the gates diff it like a memory dump.
 * --firmware <id>=<path> mounts a firmware file under the id the core declares
 * (a PS2 bios, a disk-system rom). Repeatable. A project names the ids it needs
 * and the SHA1 it expects; this tool has no user to ask, so it mounts what it
 * is given and the engine checks the hash.
 * --save-state <frame>=<path> writes the whole machine after that frame;
 * --save-state-file <frame>=<path> and --state-file <path> are the same through a
 * state KEPT IN A FILE (no size limit; what a TAStudio branch keeps), and
 * --state-file-roundtrip <frame> saves to one and loads it straight back.
 * --state <path> starts from one instead of from power-on, and --frames <n>
 * stops after n. The three together are for looking at a picture: a state saved
 * near the end of a long movie makes "what does this frame look like" a
 * one-second question instead of a five-minute one. The movie's read position
 * still starts at 0, so the INPUT after a loaded state is the movie's first
 * entries rather than the ones that belong there - fine for a rendering
 * question, wrong for anything about the machine.
 * --draw-every-frame keeps the core drawing on frames nobody looks at and
 * skips only the readback (ce_session_draw_every_frame) - which is what a core
 * whose picture persists on the GPU needs, and what --render-every-frame
 * over-pays for.
 * --rates <path> writes each frame's rate as the machine reports it after that
 * frame, "frame numerator denominator" a line, and draws every frame to get it:
 * the engine asks a core its rate again after every shown frame, since a game
 * core's step is as long as the game makes it (docs/game-cores.md).
 * --ram-search-bench <domain> times RAM Search over that domain once the run is
 * done, as the RAM Search window would run it (ce_ramsearch_*, straight onto
 * the guest's memory): a start and an "equal to 0" search at each size,
 * printed to stderr. The domain's memory is the machine's own, faults and all,
 * which a benchmark over a host array is not.
 * --property-trace <name>[,<name>...] prints, after every frame, where each of
 * those game properties is and what it reads as - "prop <frame> <name> @
 * <offset> = <text>", or "prop <frame> <name> gone" - found by name the way a
 * watch finds it (ce_session_property_find, _offset, _text). After the run it
 * has the table listed again (ce_session_property_refresh) and prints the
 * count and whether the table is dynamic. --property-set <name>=<text> sets
 * one from text before the first frame, and says so or says why not. Both are
 * the witness's way to a dynamic table (docs/game-cores.md), whose properties
 * move and come and go while the machine runs.
 * --screenshot <frame>=<path> writes one frame's picture as a TGA. Repeatable.
 * The run is otherwise undrawn (turbo), so only the frames asked for cost
 * anything to draw - which is what makes "show me frame 1910 of this movie" a
 * cheap question to ask of a two-hour run.
 */

#include "chimera/engine.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#endif

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

/* One frame as an uncompressed 32-bit TGA. The engine hands over BGRA, which is
 * exactly what a TGA stores, so the rows only have to be written bottom-up. */
/* the picture the session holds now, as one number */
uint64_t pictureHash(const ce_session *session)
{
	const uint32_t *p = ce_session_video(session);
	const size_t n = static_cast<size_t>(ce_session_video_width(session)) * static_cast<size_t>(ce_session_video_height(session));
	uint64_t h = 1469598103934665603ull ^ (uint64_t)n;
	for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

bool writeTga(const std::string &path, const uint32_t *bgra, int32_t w, int32_t h)
{
    if (bgra == nullptr || w <= 0 || h <= 0) return false;
    std::vector<uint8_t> out(18 + static_cast<size_t>(w) * h * 4, 0);
    out[2] = 2;  /* uncompressed true-colour */
    out[12] = static_cast<uint8_t>(w & 0xFF);
    out[13] = static_cast<uint8_t>((w >> 8) & 0xFF);
    out[14] = static_cast<uint8_t>(h & 0xFF);
    out[15] = static_cast<uint8_t>((h >> 8) & 0xFF);
    out[16] = 32;
    out[17] = 8;  /* 8 bits of alpha, top-left origin cleared: rows go bottom-up */
    for (int32_t y = 0; y < h; y++)
    {
        std::memcpy(out.data() + 18 + static_cast<size_t>(y) * w * 4,
            bgra + static_cast<size_t>(h - 1 - y) * w, static_cast<size_t>(w) * 4);
    }
    return writeWholeFile(path, out.data(), out.size());
}

/* mkdir -p for a path's PARENT directories (the engine already refused any
 * name with "..", so walking forward is safe) */
void makeParentDirs(const std::string &path)
{
	for (size_t i = 1; i < path.size(); i++)
	{
		if (path[i] != '/') continue;
		std::string dir = path.substr(0, i);
#ifdef _WIN32
		_mkdir(dir.c_str());
#else
		mkdir(dir.c_str(), 0777);
#endif
	}
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

#if defined(_WIN32)
// The C runtime hands main an ANSI argv: a path with a letter outside the
// code page ("Broderbund" with its o-slash) arrives already wrong. The wide
// command line is what the shell really said; every path here is UTF-8 from
// this point on, as the engine takes them.
std::vector<std::string> utf8Arguments()
{
	std::vector<std::string> out;
	int n = 0;
	wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &n);
	if (wide == nullptr) return out;
	for (int i = 0; i < n; i++)
	{
		int len = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
		std::string s(len > 0 ? static_cast<size_t>(len - 1) : 0, '\0');
		if (len > 0) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, &s[0], len, nullptr, nullptr);
		out.push_back(std::move(s));
	}
	LocalFree(wide);
	return out;
}
#endif

} // namespace

int main(int argc, char **argv)
{
#if defined(_WIN32)
	std::vector<std::string> utf8 = utf8Arguments();
	std::vector<char *> utf8Argv;
	if (!utf8.empty())
	{
		for (std::string &a : utf8) utf8Argv.push_back(&a[0]);
		utf8Argv.push_back(nullptr);
		argc = static_cast<int>(utf8.size());
		argv = utf8Argv.data();
	}
#endif
	const char *packagePath = nullptr, *romPath = nullptr, *moviePath = nullptr;
	const char *settings = nullptr;
	std::string metaPath;
	/* --core-log <path>: what the core says, kept in a file (ce_core_log), and
	 * the "corelog" request mounted so a core with a fuller log keeps it */
	std::string coreLogPath;
	std::string ratesPath;
	std::vector<std::pair<std::string, std::string>> dumps; // domain -> path
	std::string ramSearchBench;
	std::vector<std::string> propertyTrace;
	std::string propertySet;
	std::map<int64_t, std::string> shots; // frame -> TGA path
	std::vector<std::pair<std::string, std::string>> firmwareArgs; // id -> path
	std::map<int64_t, std::string> stateOuts; // frame -> state path
	std::string stateIn;
	std::string stateFileIn;                       // --state-file: the same, from a state kept in a file
	std::map<int64_t, std::string> stateFileOuts; // --save-state-file frame -> path
	int64_t stateFileRoundTrip = -1;               // --state-file-roundtrip frame
	int64_t frameLimit = -1;
	bool rerecord = false;
	int64_t seekFrame = -1;
	bool stopAtSeek = false;
	std::string bands;
	std::string spillDir;
	int64_t greenzoneMb = 256;
	int64_t greenzoneDiskMb = 0;
	std::string greenzoneMap; // where to write the frames the history holds at the end
	int64_t greenzoneMaxStride = 0; // 0: the engine's default cap
	int64_t greenzoneBandGoal = 0; // 0: the engine's default goal
	int64_t greenzoneBytes = -1;   // an exact budget in bytes, for machines too small to fill a megabyte
	int64_t greenzonePeriod = -1;   // TAStudio's "Greenzone" setting: 0 off, 1 every frame, n one in n
	int64_t greenzonePeriodAt = 0;  // the frame of the first pass it is set at
	std::string recordPath;
	std::string savedataDir;
	std::string projectPath;
	std::string historyIn, historyOut;
	/* --history-out-later: save the history the way a PROJECT save does - queued
	 * on the writer (ce_session_history_save_later) and waited for - rather than
	 * in line. The frontend's autosave takes the queued path and chimera-run has
	 * only ever taken the synchronous one, which is the one structural
	 * difference between them; this exists to ask whether that is why a
	 * greenzone reloaded through the frontend diverges when one reloaded through
	 * this tool does not. */
	bool historyOutLater = false;
	/* --play/--edit-from/--final-state: the re-recording shape. Play a while,
	 * seek back, put a DIFFERENT movie in from that frame on, and carry on past
	 * where the first pass reached. What comes out has to be what a straight run
	 * of the edited movie produces - that is the whole promise a tool-assisted
	 * run rests on, and nothing else here tests it, because --seek replays the
	 * same inputs and can pass with the edit path broken. */
	int64_t playFrames = -1;
	std::string editFrom, finalStatePath, finalShot, finalBuses;
	std::vector<std::pair<std::string, std::string>> finalBusDumps;
	std::vector<std::string> fileDirs;
	bool allowCoreMismatch = false;
	bool wantGpu = false;
	bool suggest = false;
	/* --import-movie: the positional "rom" is a movie made elsewhere (a Doom
	 * demo), mounted as "movie", and the core's ImportMovie is asked what it
	 * amounts to (ce_import_movie); --mount name=path adds the files it needs,
	 * under the names given */
	bool importMovie = false;
	std::vector<std::pair<std::string, std::string>> mounts;
	/* Frames are drawn only when a screenshot asks for one, which makes this
	 * runner a measurement of a seek rather than of play. --render-every-frame
	 * is the other half of that A/B: the same run, drawing. */
	bool renderEveryFrame = false;
	int64_t picturesMb = -1, picturesSettle = -1; /* --greenzone-pictures MiB[,settle] */
	int64_t keptCheck = -1;                         /* --kept-pictures-check <frame> */
	int64_t probeFrom = -1, probeCount = 0;         /* --settle-probe <frame>,<count>[,<tga prefix>] */
	bool probeUnseen = false;                       /* --settle-probe-unseen: no picture read back before the probe's frames */
	std::string probePrefix;
	bool drawEveryFrame = false;
	/* --rewind-loop <frame>,<times>: what re-recording actually does. A single
	 * --seek asks whether the history holds one frame; this asks whether doing
	 * it over and over leaves the machine, and the PICTURE, where a straight
	 * run leaves them. A renderer whose objects live outside the savestate
	 * degrades a little on each pass, and only a repetition shows it. */
	int64_t rewindTo = -1;
	int64_t rewindTimes = 0;
	int64_t keptFrame = -1;
	uint64_t keptFirst = 0;
	struct ProbePicture { int32_t w = 0, h = 0; std::vector<uint32_t> pixels; };
	std::vector<ProbePicture> probeFirst;
	ProbePicture probeBefore;
	bool probeByState = false;                      /* --settle-probe-state: go back by a whole state, not the greenzone */
	std::vector<uint8_t> probeState;
	int64_t probeFirstFrame = -1;
	/* --greenzone-check: the machine at the seek/rewind destination, restored
	 * through the history, must be byte-for-byte the machine the first pass had
	 * at that frame. A full ce_session_save_state is captured there on the way
	 * out, and every landing is compared against it. This catches a greenzone
	 * DELTA restore that drops or mis-restores a guest page - which a dump
	 * comparison at the end cannot, because a replay from the landing runs the
	 * GPU (outside the savestate) and its output is not deterministic, whereas
	 * the guest memory AT the frame is. The suspected cause of the Ruffle
	 * rewind crashes (guest heap/GC corruption after a restore, 2026-09-14). */
	bool greenzoneCheck = false;
	/* --greenzone-check-vs-restore: take the reference from the FIRST restore
	 * rather than from the forward pass. Comparing a restore against the
	 * forward pass asks whether the history reproduces the machine the run
	 * HAD; comparing restores against each other asks whether the history is
	 * self-consistent - and for a core whose guest memory takes bytes back
	 * from a GPU (outside the savestate), only the second question has a
	 * defined answer. */
	bool gzTruthFromRestore = false;
	std::vector<uint8_t> gzTruth;
	/* How many frames before the destination to start DRAWING again. A seek
	 * replays with rendering off, and a renderer whose display stage carries
	 * state from frame to frame needs a few composed frames to catch up. */
	int64_t rewindWarmup = 1;

	for (int i = 1; i < argc; i++)
	{
		std::string arg = argv[i];
		if (arg == "--rerecord") rerecord = true;
		else if (arg == "--seek" && i + 1 < argc) seekFrame = std::atoll(argv[++i]);
		else if (arg == "--play" && i + 1 < argc) playFrames = std::atoll(argv[++i]);
		else if (arg == "--edit-from" && i + 1 < argc) editFrom = argv[++i];
		else if (arg == "--final-state" && i + 1 < argc) finalStatePath = argv[++i];
		else if (arg == "--final-screenshot" && i + 1 < argc) finalShot = argv[++i];
		else if (arg == "--final-buses" && i + 1 < argc) finalBuses = argv[++i];
		else if (arg == "--final-bus" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			size_t eq = spec.find('=');
			if (eq == std::string::npos) { std::fprintf(stderr, "--final-bus wants NAME=PATH\n"); return 2; }
			finalBusDumps.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
		}
		else if (arg == "--bands" && i + 1 < argc) bands = argv[++i];
		else if (arg == "--spill" && i + 1 < argc) spillDir = argv[++i];
		else if (arg == "--greenzone" && i + 1 < argc) greenzoneMb = std::atoll(argv[++i]);
		else if (arg == "--greenzone-disk" && i + 1 < argc) greenzoneDiskMb = std::atoll(argv[++i]);
		else if (arg == "--greenzone-map" && i + 1 < argc) greenzoneMap = argv[++i];
		else if (arg == "--greenzone-max-stride" && i + 1 < argc) greenzoneMaxStride = std::atoll(argv[++i]);
		else if (arg == "--greenzone-band-goal" && i + 1 < argc) greenzoneBandGoal = std::atoll(argv[++i]);
		else if (arg == "--greenzone-bytes" && i + 1 < argc) greenzoneBytes = std::atoll(argv[++i]);
		else if (arg == "--greenzone-period" && i + 1 < argc) greenzonePeriod = std::atoll(argv[++i]);
		else if (arg == "--greenzone-period-at" && i + 1 < argc) greenzonePeriodAt = std::atoll(argv[++i]);
		else if (arg == "--stop-at-seek") stopAtSeek = true;
		else if (arg == "--record" && i + 1 < argc) recordPath = argv[++i];
		else if (arg == "--settings" && i + 1 < argc) settings = argv[++i];
		else if (arg == "--suggest") suggest = true;
		else if (arg == "--import-movie") importMovie = true;
		else if (arg == "--mount" && i + 1 < argc)
		{
			const std::string spec = argv[++i];
			const size_t eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--mount wants <name>=<path>");
			mounts.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
		}
		else if (arg == "--export-savedata" && i + 1 < argc) savedataDir = argv[++i];
		else if (arg == "--meta" && i + 1 < argc) metaPath = argv[++i];
		else if (arg == "--core-log" && i + 1 < argc) coreLogPath = argv[++i];
		else if (arg == "--project" && i + 1 < argc) projectPath = argv[++i];
		else if (arg == "--files" && i + 1 < argc) fileDirs.push_back(argv[++i]);
		else if (arg == "--allow-core-mismatch") allowCoreMismatch = true;
		else if (arg == "--gpu") wantGpu = true;
		else if (arg == "--render-every-frame") renderEveryFrame = true;
		else if (arg == "--greenzone-pictures" && i + 1 < argc)
		{
			/* the pictures of frames already drawn (engine.h,
			 * ce_session_greenzone_pictures): a budget in MiB, 0 for none,
			 * and optionally how many frames after a load are not kept */
			const std::string spec = argv[++i];
			const auto comma = spec.find(',');
			picturesMb = std::atoll(spec.substr(0, comma).c_str());
			if (comma != std::string::npos) picturesSettle = std::atoll(spec.substr(comma + 1).c_str());
		}
		else if (arg == "--kept-pictures-check" && i + 1 < argc) { keptCheck = std::atoll(argv[++i]); renderEveryFrame = true; }
		else if (arg == "--settle-probe-state") probeByState = true;
		else if (arg == "--settle-probe" && i + 1 < argc)
		{
			/* how long a renderer is wrong after a load, measured: <count> frames
			 * from <frame> are remembered as they were first drawn, the machine
			 * is put back before them, and each is drawn again and compared */
			const std::string spec = argv[++i];
			const auto c1 = spec.find(',');
			if (c1 == std::string::npos) return fail(metaPath, "--settle-probe wants <frame>,<count>[,<tga prefix>]");
			const auto c2 = spec.find(',', c1 + 1);
			probeFrom = std::atoll(spec.substr(0, c1).c_str());
			probeCount = std::atoll(spec.substr(c1 + 1, c2 == std::string::npos ? c2 : c2 - c1 - 1).c_str());
			if (c2 != std::string::npos) probePrefix = spec.substr(c2 + 1);
			renderEveryFrame = true;
		}
		/* the frames before the probe's are run as a seek runs them, with no
		 * picture read back: the state the load lands on is then one taken of
		 * a machine nobody was looking at, which is most of a greenzone. With
		 * --draw-every-frame they are still drawn, and what a core's renderer
		 * holds at that state is the frame - only the core's copy of the last
		 * picture read is old. */
		else if (arg == "--settle-probe-unseen") probeUnseen = true;
		else if (arg == "--rates" && i + 1 < argc) { ratesPath = argv[++i]; renderEveryFrame = true; }
		else if (arg == "--draw-every-frame") drawEveryFrame = true;
		else if (arg == "--greenzone-check") greenzoneCheck = true;
		else if (arg == "--greenzone-check-vs-restore") { greenzoneCheck = true; gzTruthFromRestore = true; }
		else if (arg == "--rewind-warmup" && i + 1 < argc) rewindWarmup = std::atoll(argv[++i]);
		else if (arg == "--rewind-loop" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto comma = spec.find(',');
			if (comma == std::string::npos) return fail(metaPath, "--rewind-loop wants <frame>,<times>");
			rewindTo = std::atoll(spec.substr(0, comma).c_str());
			rewindTimes = std::atoll(spec.substr(comma + 1).c_str());
		}
		else if (arg == "--history-in" && i + 1 < argc) historyIn = argv[++i];
		else if (arg == "--history-out" && i + 1 < argc) historyOut = argv[++i];
		else if (arg == "--history-out-later" && i + 1 < argc) { historyOut = argv[++i]; historyOutLater = true; }
		else if (arg == "--firmware" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--firmware wants <id>=<path>");
			firmwareArgs.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
		}
		else if (arg == "--save-state" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--save-state wants <frame>=<path>");
			stateOuts[std::atoll(spec.substr(0, eq).c_str())] = spec.substr(eq + 1);
		}
		else if (arg == "--state" && i + 1 < argc) stateIn = argv[++i];
		else if (arg == "--state-file" && i + 1 < argc) stateFileIn = argv[++i];
		else if (arg == "--state-file-roundtrip" && i + 1 < argc) stateFileRoundTrip = std::atoll(argv[++i]);
		else if (arg == "--save-state-file" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--save-state-file wants <frame>=<path>");
			stateFileOuts[std::atoll(spec.substr(0, eq).c_str())] = spec.substr(eq + 1);
		}
		else if (arg == "--frames" && i + 1 < argc) frameLimit = std::atoll(argv[++i]);
		else if (arg == "--screenshot" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--screenshot wants <frame>=<path>");
			shots[std::atoll(spec.substr(0, eq).c_str())] = spec.substr(eq + 1);
		}
		else if (arg == "--ram-search-bench" && i + 1 < argc) ramSearchBench = argv[++i];
		else if (arg == "--property-trace" && i + 1 < argc)
		{
			std::string names = argv[++i];
			for (size_t at = 0; at <= names.size();)
			{
				const size_t comma = std::min(names.find(',', at), names.size());
				if (comma > at) propertyTrace.push_back(names.substr(at, comma - at));
				at = comma + 1;
			}
		}
		else if (arg == "--property-set" && i + 1 < argc) propertySet = argv[++i];
		else if (arg == "--dump" && i + 1 < argc)
		{
			std::string spec = argv[++i];
			auto eq = spec.find('=');
			if (eq == std::string::npos) return fail(metaPath, "--dump wants <domain>=<path>");
			dumps.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
		}
		else if (packagePath == nullptr) packagePath = argv[i];
		else if (projectPath.empty() && romPath == nullptr) romPath = argv[i];
		else if (projectPath.empty() && moviePath == nullptr) moviePath = argv[i];
		else return fail(metaPath, "unexpected argument: " + arg);
	}
	bool projectMode = !projectPath.empty();
	if (projectMode ? packagePath == nullptr : (suggest || importMovie ? romPath == nullptr : moviePath == nullptr))
	{
		std::fprintf(stderr, "usage: chimera-run <package> <rom> <movie.txt> [--rerecord] [--seek <frame>] [--play <n>] [--edit-from <movie>] [--stop-at-seek] [--bands n,m,ms,fs,anchor] [--record <out.txt>] [--settings <json>] [--dump <domain>=<path>]... [--firmware <id>=<path>]... [--state <path>] [--frames <n>] [--save-state <frame>=<path>]... [--screenshot <frame>=<path>]... [--ram-search-bench <domain>] [--export-savedata <dir>] [--meta <path>] [--gpu] [--draw-every-frame]\n"
			"       chimera-run --project <p.chimeraProject> <package> [--files <dir>]... [--allow-core-mismatch] [the same run flags]\n"
			"       chimera-run <package> <rom> --suggest [--settings <json>] [--firmware <id>=<path>]...\n"
			"       chimera-run <package> <movie> --import-movie [--mount <name>=<path>]... [--settings <json>]\n");
		return 1;
	}
	if (projectMode && settings != nullptr)
	{
		return fail(metaPath, "--settings and --project do not mix: the project IS the settings");
	}

	std::vector<uint8_t> rom, movieText;
	if (!projectMode && !suggest && !importMovie && !readWholeFile(moviePath, movieText)) return fail(metaPath, std::string("could not read movie ") + moviePath);

	/* A .chimeraMultiFile rom is a multi-file game: the first image mounts as
	 * the rom (rom.name carrying its real name), further images as rom2..N,
	 * support files and savedata under their fixed names - the exact mounts
	 * the frontend makes. Everything hash-verified; refuse on any mismatch
	 * (this tool has no user to ask). */
	ce_multifile *multi = nullptr;
	ce_project *project = nullptr;
	std::vector<const char *> extraNames;
	std::vector<const uint8_t *> extraData;
	std::vector<uint64_t> extraLens;
	/* per extra: a path to read it from, or null to use the bytes above */
	std::vector<const char *> extraPaths;
	std::string romPathStore;
	std::vector<std::string> extraNameStore;
	std::string settingsStore, slotsStore;
	if (projectMode)
	{
		const char *perr = nullptr;
		project = ce_project_open(projectPath.c_str(), &perr);
		if (project == nullptr) return fail(metaPath, perr != nullptr ? perr : "bad project");

		/* resolution: the project's own folder first (the convenience), then
		 * every --files dir; whatever is still missing or mismatched fails -
		 * this tool has no user to knowingly override */
		size_t cut = projectPath.find_last_of("/\\");
		std::string projFolder = cut == std::string::npos ? std::string(".") : projectPath.substr(0, cut);
		ce_project_resolve_dir(project, projFolder.c_str());
		for (const std::string &dir : fileDirs) ce_project_resolve_dir(project, dir.c_str());
		if (ce_project_files_ok(project) == 0)
		{
			for (int32_t i = 0; i < ce_project_file_count(project); i++)
			{
				if (ce_project_file_status(project, i) != 0)
					return fail(metaPath, std::string("project: '") + ce_project_file_name(project, i)
						+ (ce_project_file_status(project, i) == 1
							? "' was not found in the project's folder or any --files dir"
							: "' does not match its recorded hash"));
			}
		}

		/* the core pin, against the actual package; and the manifest against
		 * the package's slot declaration when it ships one */
		const char *kerr = nullptr;
		ce_package *pkg = ce_package_open(packagePath, &kerr);
		if (pkg != nullptr)
		{
			const char *pin = ce_project_core_sha1(project);
			const char *actual = ce_package_sha1(pkg);
			if (pin[0] != '\0' && actual != nullptr && std::strcmp(pin, actual) != 0 && !allowCoreMismatch)
			{
				std::string detail = std::string("the package is not the project's pinned core (pinned ")
					+ pin + ", given " + actual + "); --allow-core-mismatch to run anyway";
				ce_package_free(pkg);
				return fail(metaPath, detail);
			}
			uint64_t declLen = 0;
			const uint8_t *decl = ce_package_entry(pkg, "file_slots.json", &declLen);
			if (decl != nullptr &&
				ce_project_validate(project, reinterpret_cast<const char *>(decl), declLen, &perr) != 0)
			{
				std::string detail = perr != nullptr ? perr : "the manifest does not fit the core's slots";
				ce_package_free(pkg);
				return fail(metaPath, detail);
			}
			ce_package_free(pkg);
		}

		uint64_t len = 0;
		settingsStore = ce_project_settings_text(project, &len);
		settings = settingsStore.c_str();
		const char *lump = ce_project_log_text(project, &len);
		movieText.assign(reinterpret_cast<const uint8_t *>(lump),
			reinterpret_cast<const uint8_t *>(lump) + len);

		/* mounts: the slot map, every file under its canonical name, and the
		 * transitional rom/rom.name/rom2..N view of the first slot */
		slotsStore = ce_project_slots_text(project, &len);
		extraNames.push_back("slots");
		extraData.push_back(reinterpret_cast<const uint8_t *>(slotsStore.data()));
		extraLens.push_back(slotsStore.size());
		extraPaths.push_back(nullptr);
		int32_t primary = -1;
		for (int32_t i = 0; i < ce_project_file_count(project); i++)
		{
			/* Where it lies, not what it holds: the engine mounts the file
			 * from disk and the guest reads it as it goes. */
			extraNames.push_back(ce_project_file_name(project, i));
			extraData.push_back(nullptr);
			extraLens.push_back(0);
			extraPaths.push_back(ce_project_file_source_path(project, i));
			if (primary < 0 && std::strcmp(ce_project_file_slot(project, i), "support") != 0) primary = i;
		}
		if (primary >= 0)
		{
			/* pointers into extraNameStore go out as they are made, so the
			 * vector must never reallocate: reserve the worst case */
			extraNameStore.reserve(static_cast<size_t>(ce_project_file_count(project)) + 1);
			romPathStore = ce_project_file_source_path(project, primary);
			extraNameStore.push_back(ce_project_file_name(project, primary));
			extraNames.push_back("rom.name");
			extraData.push_back(reinterpret_cast<const uint8_t *>(extraNameStore.back().data()));
			extraLens.push_back(extraNameStore.back().size());
			extraPaths.push_back(nullptr);
			int n = 2;
			std::string primarySlot = ce_project_file_slot(project, primary);
			for (int32_t i = primary + 1; i < ce_project_file_count(project); i++)
			{
				if (primarySlot != ce_project_file_slot(project, i)) continue;
				extraNameStore.push_back("rom" + std::to_string(n++));
				extraNames.push_back(extraNameStore.back().c_str());
				extraData.push_back(nullptr);
				extraLens.push_back(0);
				extraPaths.push_back(ce_project_file_source_path(project, i));
			}
		}
	}
	std::string romPathStr = projectMode ? std::string() : romPath;
	if (romPathStr.size() > 17 &&
		romPathStr.compare(romPathStr.size() - 17, 17, ".chimeraMultiFile") == 0)
	{
		const char *merr = nullptr;
		multi = ce_multifile_open(romPath, &merr);
		if (multi == nullptr) return fail(metaPath, merr != nullptr ? merr : "bad descriptor");
		if (ce_multifile_ok(multi) == 0)
		{
			for (int32_t i = 0; i < ce_multifile_count(multi); i++)
			{
				if (ce_multifile_status(multi, i) != 0)
					return fail(metaPath, std::string("multifile: '") + ce_multifile_name(multi, i)
						+ (ce_multifile_status(multi, i) == 1 ? "' is missing" : "' does not match its recorded hash"));
			}
		}
		int32_t primary = ce_multifile_image_index(multi, 0);
		uint64_t len = 0;
		const uint8_t *data = ce_multifile_data(multi, primary, &len);
		rom.assign(data, data + len);
		extraNameStore.push_back("rom.name");
		extraNameStore.push_back(ce_multifile_name(multi, primary));
		for (int32_t n = 1; n < ce_multifile_image_count(multi); n++)
		{
			extraNameStore.push_back("rom" + std::to_string(n + 1));
		}
		// mounts: rom.name first, then rom2..N, then the rest by name
		size_t store = 0;
		extraNames.push_back(extraNameStore[store++].c_str());
		{
			const std::string &nm = extraNameStore[store++];
			extraData.push_back(reinterpret_cast<const uint8_t *>(nm.data()));
			extraLens.push_back(nm.size());
		}
		for (int32_t n = 1; n < ce_multifile_image_count(multi); n++)
		{
			int32_t idx = ce_multifile_image_index(multi, n);
			extraNames.push_back(extraNameStore[store++].c_str());
			extraData.push_back(ce_multifile_data(multi, idx, &len));
			extraLens.push_back(len);
		}
		for (int32_t i = 0; i < ce_multifile_count(multi); i++)
		{
			const char *role = ce_multifile_role(multi, i);
			if (std::strcmp(role, "support") == 0)
			{
				extraNames.push_back(ce_multifile_name(multi, i));
				extraData.push_back(ce_multifile_data(multi, i, &len));
				extraLens.push_back(len);
			}
			else if (std::strcmp(role, "savedata") == 0)
			{
				extraNames.push_back("savedata");
				extraData.push_back(ce_multifile_data(multi, i, &len));
				extraLens.push_back(len);
			}
		}
	}
	else if (!projectMode)
	{
		/* THE PATH, not the bytes. The engine mounts a rom from where it lies
		 * when it is told where that is, and reads it lazily from there; hand
		 * it a byte array instead and the file is copied three times over -
		 * once here, once into the session, once into the guest's file system -
		 * before the machine has read a sector of it. A 2 GB DOS hard disk cost
		 * 8.3 GB that way, which is the frontend's own behaviour nowhere: it
		 * has always passed the path. */
		romPathStore = romPath;
	}

	ce_movie_log *movie = ce_movie_log_new();
	if (!suggest && !importMovie && ce_movie_log_parse(movie, reinterpret_cast<const char *>(movieText.data()), movieText.size()) != 0)
	{
		return fail(metaPath, std::string("movie: ") + ce_movie_log_last_error(movie));
	}

	/* --gpu is the same ask the frontend makes for a renderer named *-hw:
	 * an offer, not a promise. Without a bridge in this build, without a
	 * driver, or with a core that has no GL renderer, the software path
	 * draws and ce_session_deterministic still says 1. */
	ce_gl_request(wantGpu ? 1 : 0);

	/* firmware, read whole: a bios is small and the engine wants the bytes */
	std::vector<std::vector<uint8_t>> fwBytes(firmwareArgs.size());
	std::vector<const char *> fwIds;
	std::vector<const uint8_t *> fwData;
	std::vector<uint64_t> fwLens;
	for (size_t i = 0; i < firmwareArgs.size(); i++)
	{
		if (!readWholeFile(firmwareArgs[i].second.c_str(), fwBytes[i]))
		{
			return fail(metaPath, "could not read firmware " + firmwareArgs[i].second);
		}
		fwIds.push_back(firmwareArgs[i].first.c_str());
		fwData.push_back(fwBytes[i].data());
		fwLens.push_back(fwBytes[i].size());
	}

	const char *error = nullptr;
	/* --import-movie: the movie is mounted as "movie", not as the rom, and the
	 * --mount files beside it; the core's answer is printed as its JSON */
	if (importMovie)
	{
		std::vector<const char *> names, paths;
		std::vector<const uint8_t *> datas;
		std::vector<uint64_t> lens;
		names.push_back("movie");
		paths.push_back(romPath);
		for (const auto &m : mounts)
		{
			names.push_back(m.first.c_str());
			paths.push_back(m.second.c_str());
		}
		datas.assign(names.size(), nullptr);
		lens.assign(names.size(), 0);
		uint64_t len = 0;
		const char *answer = ce_import_movie(
			packagePath, nullptr, 0, nullptr,
			settings, fwIds.data(), fwData.data(), fwLens.data(), static_cast<int32_t>(fwIds.size()),
			names.data(), datas.data(), lens.data(), paths.data(),
			static_cast<int32_t>(names.size()), &len, &error);
		if (answer == nullptr) return fail(metaPath, error != nullptr ? error : "could not open the core");
		std::printf("%.*s\n", static_cast<int>(len), answer);
		return 0;
	}
	/* --suggest: what the core would choose for this game, printed as its
	 * JSON, and nothing is started (ce_suggest_settings) */
	if (suggest)
	{
		uint64_t len = 0;
		const char *answer = ce_suggest_settings(
			packagePath, rom.data(), rom.size(), romPathStore.empty() ? nullptr : romPathStore.c_str(),
			settings, fwIds.data(), fwData.data(), fwLens.data(), static_cast<int32_t>(fwIds.size()),
			extraNames.data(), extraData.data(), extraLens.data(), extraPaths.data(),
			static_cast<int32_t>(extraNames.size()), &len, &error);
		if (answer == nullptr) return fail(metaPath, error != nullptr ? error : "could not open the core");
		std::printf("%.*s\n", static_cast<int>(len), answer);
		return 0;
	}
	if (!coreLogPath.empty() && ce_core_log(coreLogPath.c_str()) == 0)
		return fail(metaPath, std::string("--core-log: ") + ce_core_log_error());
	ce_session *session = ce_session_open(
		packagePath, rom.data(), rom.size(), romPathStore.empty() ? nullptr : romPathStore.c_str(),
		settings, fwIds.data(), fwData.data(), fwLens.data(), static_cast<int32_t>(fwIds.size()),
		extraNames.data(), extraData.data(), extraLens.data(), extraPaths.data(),
		static_cast<int32_t>(extraNames.size()), &error);
	if (session == nullptr) return fail(metaPath, error != nullptr ? error : "session open failed");

	if (drawEveryFrame) ce_session_draw_every_frame(session, 1);

	int64_t frames = ce_movie_log_count(movie);

	/* the movie is the SESSION's from here: the engine parses entries, tracks
	 * the frame position, and (with --seek) keeps the greenzone */
	if (ce_session_movie_load(session, movie) != 0) return fail(metaPath, "could not load the movie into the session");

	/* --frames shortens a run. While RECORDING it may also lengthen one: the
	 * source movie is an input source there and not the timeline, so a run can
	 * outlast it and the tail is idle input. That is what makes "record N
	 * frames of nothing" expressible, which is the only way to ask a core what
	 * its entries LOOK like before owning a movie of the right shape.
	 *
	 * Settled HERE, before the record block below decodes that many entries -
	 * a limit applied after it would leave the decoded input shorter than the
	 * run. */
	/* --frames can only shorten a movie that is being played: it does not
	 * lengthen one. Asked for more frames than the movie has, the run is the
	 * movie's length - and with a movie of no rows that is NO frames, every
	 * dump still written and the exit code still 0. Say so: a dump of frame 0
	 * compared with a real run reads as a divergence (it cost somebody an
	 * afternoon, 2026-10-09). To run a core for N frames with nothing pressed,
	 * record a row and repeat it, or use --record. */
	if (frameLimit > frames && recordPath.empty())
	{
		std::fprintf(stderr, "chimera-run: --frames %lld asked, but the movie has %lld frames and nothing is being recorded: %lld will run\n",
			static_cast<long long>(frameLimit), static_cast<long long>(frames), static_cast<long long>(frames));
	}
	if (frameLimit >= 0 && (frameLimit < frames || !recordPath.empty())) frames = frameLimit;
	/* --rewind-loop needs the history too: it seeks back through it, and a run
	 * without one fails at the first pass with "no stored state at or before
	 * the target frame". */
	if (seekFrame >= 0 || rewindTo >= 0 || keptCheck >= 0 || (probeFrom >= 0 && !probeByState) || !historyIn.empty() || !historyOut.empty() || !greenzoneMap.empty()
		|| greenzoneBytes > 0 || greenzonePeriod >= 0)
	{
		/* Bands before enabling: enabling captures the anchor, and the anchor
		 * spacing decides whether it is the only one. */
		if (!bands.empty())
		{
			int64_t v[5] = { 0, 0, 0, 0, 0 };
			size_t at = 0;
			for (int64_t &slot : v)
			{
				if (at > bands.size()) break;
				const size_t comma = bands.find(',', at);
				slot = std::atoll(bands.substr(at, comma == std::string::npos ? comma : comma - at).c_str());
				if (comma == std::string::npos) break;
				at = comma + 1;
			}
			ce_session_greenzone_bands(session, v[0], v[1], v[2], v[3], v[4]);
		}
		/* Where the far band goes when the budget is full. Without one the
		 * history can only DROP, which is why the default here is nowhere: a
		 * measurement of what the greenzone costs should not quietly become a
		 * measurement of what the disk costs. */
		if (!spillDir.empty()) ce_session_greenzone_spill(session, spillDir.c_str());
		ce_session_greenzone_disk_budget(session, (uint64_t)greenzoneDiskMb << 20);
		if (greenzoneMaxStride > 0) ce_session_greenzone_max_near_stride(session, greenzoneMaxStride);
		if (greenzoneBandGoal > 0) ce_session_greenzone_band_goal(session, greenzoneBandGoal);
		ce_session_greenzone_enable(session, greenzoneBytes > 0 ? (uint64_t)greenzoneBytes : (uint64_t)greenzoneMb << 20);
		if (picturesMb >= 0) ce_session_greenzone_pictures(session, (uint64_t)picturesMb << 20, (int32_t)picturesSettle);
	}
	/* A history kept from a previous run, which is the thing a reopened project
	 * lives on. The machine id is this tool's own convention; a frontend passes
	 * whatever it knows about cores, settings and files. */
	if (!historyIn.empty())
	{
		const auto t0 = std::chrono::steady_clock::now();
		if (ce_session_history_load(session, historyIn.c_str(), "chimera-run") != 0)
		{
			return fail(metaPath, std::string("history: ") + ce_session_last_error(session));
		}
		std::fprintf(stderr, "chimera-run: history loaded in %.1f ms\n",
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
	}

	/* Record mode: decode the source movie's entries into machine input and
	 * hand THAT to the session, which generates its own log. The source is an
	 * input source only - none of its text reaches the recorded movie.
	 * The WIDE decode + set_button path is used for every controller - exact
	 * at any width, and identical to the packed path below 64 buttons. */
	int64_t buttonCount = ce_session_button_count(session);
	std::vector<std::vector<uint8_t>> recButtons;
	std::vector<std::vector<int32_t>> recAxes;
	if (!recordPath.empty())
	{
		int64_t axisCount = ce_session_axis_count(session);
		const int64_t sourceFrames = ce_movie_log_count(movie);
		for (int64_t i = 0; i < frames; i++)
		{
			std::vector<uint8_t> states(static_cast<size_t>(buttonCount), 0);
			std::vector<int32_t> axes(static_cast<size_t>(axisCount), 0);
			for (int64_t a = 0; a < axisCount; a++) axes[static_cast<size_t>(a)] = ce_session_axis_neutral(session, a);
			/* past the source movie's end the input is idle: no button, every
			 * axis at its neutral (0 on a signed stick, 127 on an Apple II
			 * paddle - zero there is the stick held hard up-left). A run may
			 * outlast its input source while recording (see --frames above);
			 * it may not read past it. */
			if (i < sourceFrames && ce_session_movie_entry_decode_wide(
					session, ce_movie_log_entry(movie, i),
					buttonCount != 0 ? states.data() : nullptr,
					axisCount != 0 ? axes.data() : nullptr) != 0)
			{
				return fail(metaPath, "could not decode entry " + std::to_string(i));
			}
			recButtons.push_back(std::move(states));
			recAxes.push_back(std::move(axes));
		}
		/* NULL mnemonics: the generated characters are the engine's fallback
		 * rather than a frontend vocabulary. Parsing ignores the character, so
		 * a replay of this file lands on the same machine either way. */
		ce_session_movie_record(session, nullptr);
	}

	/* --state: begin from a machine somebody saved earlier. The movie's read
	 * position is NOT wound forward with it (see the header comment): this is
	 * for looking at pictures, not for replaying a run. */
	if (!stateIn.empty())
	{
		std::vector<uint8_t> saved;
		if (!readWholeFile(stateIn.c_str(), saved)) return fail(metaPath, "could not read state " + stateIn);
		if (ce_session_load_state(session, saved.data(), saved.size()) != 0)
		{
			return fail(metaPath, ce_session_last_error(session));
		}
	}

	/* --state-file: the same beginning, from a state the engine kept in a file
	 * (ce_session_state_save_file) - what a TAStudio branch keeps */
	if (!stateFileIn.empty())
	{
		uint8_t tag[64];
		uint32_t tagLen = 0;
		if (ce_session_state_load_file(session, stateFileIn.c_str(), tag, sizeof tag, &tagLen) != 0)
		{
			return fail(metaPath, ce_session_last_error(session));
		}
		fprintf(stderr, "state file tag: %.*s\n", (int)(tagLen < sizeof tag ? tagLen : sizeof tag), (const char *)tag);
	}

	std::vector<uint8_t> state;
	if (rerecord)
	{
		uint64_t len = 0;
		const uint8_t *p = ce_session_save_state(session, &len);
		if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
		state.assign(p, p + len);
	}

	const int64_t firstPass = playFrames >= 0 && playFrames < frames ? playFrames : frames;
	FILE *rates = ratesPath.empty() ? nullptr : std::fopen(ratesPath.c_str(), "w");
	if (!ratesPath.empty() && rates == nullptr) return fail(metaPath, "could not write " + ratesPath);
	if (!propertySet.empty())
	{
		const size_t eq = propertySet.find('=');
		if (eq == std::string::npos) return fail(metaPath, "--property-set wants <name>=<text>");
		const std::string name = propertySet.substr(0, eq);
		uint32_t element = 0;
		const int32_t index = ce_session_property_find(session, name.c_str(), &element);
		if (index < 0) std::printf("prop set %s: no such property\n", name.c_str());
		else if (ce_session_property_set_text(session, index, element, propertySet.c_str() + eq + 1) != 0)
			std::printf("prop set %s: %s\n", name.c_str(), ce_session_last_error(session));
		else std::printf("prop set %s: done\n", name.c_str());
	}
	for (int64_t i = 0; i < firstPass; i++)
	{
		if (greenzonePeriod >= 0 && i == greenzonePeriodAt) ce_session_greenzone_capture_period(session, greenzonePeriod);
		if (rerecord && ce_session_load_state(session, state.data(), state.size()) != 0)
		{
			return fail(metaPath, ce_session_last_error(session));
		}
		if (!recordPath.empty())
		{
			const auto &states = recButtons[static_cast<size_t>(i)];
			for (size_t b = 0; b < states.size(); b++)
			{
				ce_session_set_button(session, static_cast<int32_t>(b), states[b]);
			}
		}
		const int32_t *axes = recordPath.empty() || recAxes[static_cast<size_t>(i)].empty()
			? nullptr
			: recAxes[static_cast<size_t>(i)].data();
		auto shot = shots.find(i);
		const bool unseen = probeUnseen && probeFrom >= 0 && i < probeFrom;
		if (ce_session_movie_advance(session, 0, axes,
			((renderEveryFrame && !unseen) || shot != shots.end()) ? 1 : 0) < 0)
		{
			return fail(metaPath, ce_session_last_error(session));
		}
		if (rates != nullptr)
		{
			std::fprintf(rates, "%lld %d %d\n", (long long)i, ce_session_vsync_numerator(session), ce_session_vsync_denominator(session));
		}
		for (const std::string &name : propertyTrace)
		{
			uint32_t element = 0;
			const int32_t index = ce_session_property_find(session, name.c_str(), &element);
			const int64_t offset = index < 0 ? -1 : ce_session_property_offset(session, index, element);
			char text[256] = "";
			if (offset >= 0) ce_session_property_text(session, index, element, 0, text, sizeof text);
			if (offset >= 0) std::printf("prop %lld %s @ %lld = %s\n", (long long)i, name.c_str(), (long long)offset, text);
			else std::printf("prop %lld %s gone\n", (long long)i, name.c_str());
		}
		if (i == keptCheck)
		{
			keptFrame = ce_session_frame(session);
			keptFirst = pictureHash(session);
		}
		if (probeFrom >= 1 && i == probeFrom - 1)
		{
			/* the frame before the first one compared: what a stale picture would be */
			probeBefore.w = ce_session_video_width(session);
			probeBefore.h = ce_session_video_height(session);
			const uint32_t *v = ce_session_video(session);
			probeBefore.pixels.assign(v, v + static_cast<size_t>(probeBefore.w) * static_cast<size_t>(probeBefore.h));
			if (probeByState)
			{
				uint64_t len = 0;
				const uint8_t *state = ce_session_save_state(session, &len);
				if (state == nullptr) return fail(metaPath, std::string("--settle-probe-state: ") + ce_session_last_error(session));
				probeState.assign(state, state + len);
			}
		}
		if (probeFrom >= 0 && i >= probeFrom && i < probeFrom + probeCount)
		{
			if (i == probeFrom) probeFirstFrame = ce_session_frame(session);
			ProbePicture pic;
			pic.w = ce_session_video_width(session);
			pic.h = ce_session_video_height(session);
			const uint32_t *v = ce_session_video(session);
			pic.pixels.assign(v, v + static_cast<size_t>(pic.w) * static_cast<size_t>(pic.h));
			probeFirst.push_back(std::move(pic));
		}
		if (shot != shots.end()
			&& !writeTga(shot->second, ce_session_video(session),
				ce_session_video_width(session), ce_session_video_height(session)))
		{
			return fail(metaPath, "could not write " + shot->second);
		}
		auto st = stateOuts.find(i);
		if (st != stateOuts.end())
		{
			uint64_t len = 0;
			const uint8_t *p = ce_session_save_state(session, &len);
			if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
			if (!writeWholeFile(st->second, p, static_cast<size_t>(len)))
			{
				return fail(metaPath, "could not write " + st->second);
			}
		}
		auto sf = stateFileOuts.find(i);
		if (sf != stateFileOuts.end())
		{
			const std::string tag = "frame " + std::to_string(i);
			const auto began = std::chrono::steady_clock::now();
			if (ce_session_state_save_file(session, sf->second.c_str(), reinterpret_cast<const uint8_t *>(tag.data()), (uint32_t)tag.size()) != 0)
			{
				return fail(metaPath, ce_session_last_error(session));
			}
			const auto saved = std::chrono::steady_clock::now();
			uint64_t rawBytes = 0, storedBytes = 0;
			ce_session_state_file_bytes(session, &rawBytes, &storedBytes);
			/* and straight back, timed: what a branch costs both ways */
			if (ce_session_state_load_file(session, sf->second.c_str(), nullptr, 0, nullptr) != 0)
			{
				return fail(metaPath, ce_session_last_error(session));
			}
			const auto loaded = std::chrono::steady_clock::now();
			fprintf(stderr, "state file at frame %lld: raw %llu bytes, stored %llu bytes (%.2fx), save %lld ms, load %lld ms\n",
				(long long)i, (unsigned long long)rawBytes, (unsigned long long)storedBytes,
				storedBytes != 0 ? (double)rawBytes / (double)storedBytes : 0.0,
				(long long)std::chrono::duration_cast<std::chrono::milliseconds>(saved - began).count(),
				(long long)std::chrono::duration_cast<std::chrono::milliseconds>(loaded - saved).count());
		}
		/* saved to a file and loaded straight back: the machine must not notice,
		 * so a run that does this ends exactly where one that does not ends */
		if (i == stateFileRoundTrip)
		{
			const std::string path = (metaPath.empty() ? std::string("chimera-run") : metaPath) + ".roundtrip.state";
			if (ce_session_state_save_file(session, path.c_str(), nullptr, 0) != 0
				|| ce_session_state_load_file(session, path.c_str(), nullptr, 0, nullptr) != 0)
			{
				return fail(metaPath, ce_session_last_error(session));
			}
			std::remove(path.c_str());
		}
		if (rerecord)
		{
			uint64_t len = 0;
			const uint8_t *p = ce_session_save_state(session, &len);
			if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
			state.assign(p, p + len);
		}
		/* the ground truth for --greenzone-check: the machine as the first,
		 * straight pass had it at the frame every later restore lands on */
		if (greenzoneCheck && !gzTruthFromRestore && gzTruth.empty())
		{
			const int64_t checkFrame = rewindTo >= 0 ? rewindTo : seekFrame;
			if (checkFrame >= 0 && ce_session_frame(session) == checkFrame)
			{
				uint64_t len = 0;
				const uint8_t *p = ce_session_save_state(session, &len);
				if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
				gzTruth.assign(p, p + len);
			}
		}
	}
	if (rates != nullptr) std::fclose(rates);

	/* Compare the machine at a restore landing to that ground truth. First
	 * differing byte, and how many differ, is enough to point at the dropped
	 * page; the delta restore is the whole guest image, so an offset maps
	 * straight to a memory domain. */
	auto greenzoneVerify = [&](const char *what, int64_t landedFrame) -> const char * {
		if (!greenzoneCheck) return nullptr;
		uint64_t len = 0;
		const uint8_t *p = ce_session_save_state(session, &len);
		if (p == nullptr) return ce_session_last_error(session);
		if (gzTruth.empty())
		{
			if (!gzTruthFromRestore) return "the ground-truth frame was never reached in the first pass";
			gzTruth.assign(p, p + len);
			std::fprintf(stderr, "[greenzone-check] %s at frame %lld: reference taken from this restore (%llu bytes)\n",
				what, (long long)landedFrame, (unsigned long long)len);
			std::fflush(stderr);
			return nullptr;
		}
		/* Both machines to disk on any mismatch. The blob carries the dirty map
		 * and the dirty pages themselves, so comparing the two offline says
		 * exactly which guest pages the restore failed to reproduce - which a
		 * size or a first-differing-byte alone cannot. */
		auto dumpBoth = [&]() {
			writeWholeFile("gz-truth.state", gzTruth.data(), gzTruth.size());
			writeWholeFile("gz-restored.state", p, static_cast<size_t>(len));
			std::fprintf(stderr, "[greenzone-check] wrote gz-truth.state (%zu) and gz-restored.state (%llu)\n",
				gzTruth.size(), (unsigned long long)len);
			std::fflush(stderr);
		};
		if (len != gzTruth.size())
		{
			std::fprintf(stderr, "[greenzone-check] %s at frame %lld: state SIZE changed %zu -> %llu (%lld bytes, %lld pages)\n",
				what, (long long)landedFrame, gzTruth.size(), (unsigned long long)len,
				(long long)len - (long long)gzTruth.size(),
				((long long)len - (long long)gzTruth.size()) / 4096);
			dumpBoth();
			return "greenzone restore changed the state size";
		}
		uint64_t first = UINT64_MAX, differ = 0;
		for (uint64_t k = 0; k < len; k++)
		{
			if (p[k] != gzTruth[k]) { if (first == UINT64_MAX) first = k; differ++; }
		}
		if (differ != 0)
		{
			std::fprintf(stderr, "[greenzone-check] %s at frame %lld: %llu of %llu state bytes differ,"
				" first at 0x%llx (truth %02x, restored %02x)\n",
				what, (long long)landedFrame, (unsigned long long)differ, (unsigned long long)len,
				(unsigned long long)first, gzTruth[first], p[first]);
			dumpBoth();
			return "greenzone restore did not reproduce the machine";
		}
		std::fprintf(stderr, "[greenzone-check] %s at frame %lld: exact (%llu bytes)\n",
			what, (long long)landedFrame, (unsigned long long)len);
		std::fflush(stderr);
		return nullptr;
	};

	if (seekFrame >= 0)
	{
		/* back through the greenzone, then forward again: the dumps at the end
		 * must be the dumps a straight run produces. The invalidate mimics an
		 * input edit and forces the forward seek to actually REPLAY - otherwise
		 * it would just restore the cached end state and prove nothing. */
		if (ce_session_seek(session, seekFrame) != 0) return fail(metaPath, ce_session_last_error(session));
		if (ce_session_frame(session) != seekFrame) return fail(metaPath, "seek landed on the wrong frame");
		if (const char *bad = greenzoneVerify("seek", seekFrame)) return fail(metaPath, bad);
		/* --stop-at-seek dumps the machine the seek ARRIVED AT, rather than the
		 * machine at the end of a replay from it. It is the difference between
		 * asking whether the run still finishes correctly and asking whether
		 * the history actually holds frame N - and a core whose ending is
		 * decided by its inputs answers the first question yes either way. */
		if (stopAtSeek) frames = seekFrame;
		/* The edit itself. A frontend changes the entries a person retyped and
		 * throws away everything the old ones produced; this puts the whole
		 * edited movie in, which is the same thing said in one go. The
		 * invalidate is what makes the forward seek REPLAY rather than restore
		 * the ending it already had. */
		if (!editFrom.empty())
		{
			std::vector<uint8_t> editText;
			if (!readWholeFile(editFrom.c_str(), editText))
			{
				return fail(metaPath, "could not read " + editFrom);
			}
			ce_movie_log *edited = ce_movie_log_new();
			if (ce_movie_log_parse(edited, reinterpret_cast<const char *>(editText.data()),
					editText.size()) != 0)
			{
				return fail(metaPath, std::string("edit movie: ") + ce_movie_log_last_error(edited));
			}
			if (ce_movie_log_count(edited) < frames)
			{
				return fail(metaPath, "the edit movie is shorter than the run");
			}
			if (ce_session_movie_load(session, edited) != 0)
			{
				return fail(metaPath, "could not load the edited movie");
			}
			ce_movie_log_free(edited);
		}
		ce_session_greenzone_invalidate(session, seekFrame);
		if (!stopAtSeek)
		{
			if (ce_session_seek(session, frames) != 0) return fail(metaPath, ce_session_last_error(session));
			if (ce_session_frame(session) != frames) return fail(metaPath, "replay landed on the wrong frame");
		}
	}

	/* --settle-probe: how many frames a renderer draws wrong after a load, on
	 * this machine and this core. The kept pictures are switched off for it -
	 * this is the core's own picture - the machine is put back on the frame
	 * before the remembered ones (a restore, and whatever replay the greenzone's
	 * spacing makes necessary, undrawn), and each is drawn again and compared
	 * with what it was the first time: how many pixels differ at all, how many
	 * by more than 8 in a channel, and the largest difference. */
	if (probeFrom >= 0)
	{
		if (probeFirstFrame < 1 || probeFirst.empty()) return fail(metaPath, "--settle-probe: the movie never reached that frame");
		ce_session_greenzone_pictures(session, 0, -1);
		if (probeByState)
		{
			/* the whole state taken on the frame before, loaded back: no history
			 * in it at all. The movie is not wound back with it, so the frames
			 * drawn again take the entries that FOLLOW the ones compared - the
			 * same input only where the movie is not pressing anything there. */
			if (probeState.empty()) return fail(metaPath, "--settle-probe-state: no state was taken (the frame must be 1 or more)");
			if (ce_session_load_state(session, probeState.data(), probeState.size()) != 0) return fail(metaPath, ce_session_last_error(session));
		}
		else if (ce_session_seek(session, probeFirstFrame - 1) != 0) return fail(metaPath, ce_session_last_error(session));
		int64_t lastWrong = -1;
		for (size_t k = 0; k < probeFirst.size(); k++)
		{
			if (ce_session_movie_advance(session, 0, nullptr, 1) < 0) return fail(metaPath, ce_session_last_error(session));
			const ProbePicture &first = probeFirst[k];
			const int32_t w = ce_session_video_width(session), h = ce_session_video_height(session);
			const uint32_t *now = ce_session_video(session);
			size_t differ = 0, apart = 0;
			int largest = 0;
			if (w != first.w || h != first.h) { differ = apart = first.pixels.size(); largest = 255; }
			else
				for (size_t px = 0; px < first.pixels.size(); px++)
				{
					if (now[px] == first.pixels[px]) continue;
					differ++;
					int most = 0;
					for (int c = 0; c < 24; c += 8)
						most = std::max(most, std::abs(int(now[px] >> c & 255) - int(first.pixels[px] >> c & 255)));
					if (most > 8) apart++;
					largest = std::max(largest, most);
				}
			const double total = first.pixels.empty() ? 1.0 : double(first.pixels.size());
			/* a wrong picture that is exactly the first-pass picture of the frame
			 * before is a stale one - the frame's own was never handed over -
			 * which is a different fault from a picture drawn wrong */
			const ProbePicture &prev = k == 0 ? probeBefore : probeFirst[k - 1];
			const bool stale = differ != 0 && prev.w == w && prev.h == h && prev.pixels.size() == first.pixels.size()
				&& std::memcmp(prev.pixels.data(), now, prev.pixels.size() * sizeof(uint32_t)) == 0;
			std::printf("settle-probe: frame %lld, drawn #%zu after the load: %.2f%% of pixels differ, %.2f%% by more than 8 (largest %d), %dx%d%s\n",
				(long long)(probeFirstFrame + (int64_t)k), k + 1, 100.0 * double(differ) / total, 100.0 * double(apart) / total, largest, w, h,
				stale ? " - the picture of the frame before" : "");
			if (apart * 1000 > first.pixels.size()) lastWrong = (int64_t)k;
			if (!probePrefix.empty() && k < 4)
			{
				writeTga(probePrefix + "-first-" + std::to_string(k + 1) + ".tga", first.pixels.data(), first.w, first.h);
				writeTga(probePrefix + "-after-" + std::to_string(k + 1) + ".tga", now, w, h);
			}
		}
		/* A scene that does not move proves nothing: every picture is the one
		 * before it, wrong or right. Said, so that a verdict over a still
		 * screen is not taken for one. */
		size_t moved = 0;
		for (size_t k = 0; k < probeFirst.size(); k++)
		{
			const ProbePicture &prev = k == 0 ? probeBefore : probeFirst[k - 1];
			if (prev.pixels != probeFirst[k].pixels) moved++;
		}
		std::printf("settle-probe: the scene moved in %zu of the %zu frames compared\n", moved, probeFirst.size());
		std::printf("settle-probe: %s\n", lastWrong < 0
			? "every frame drawn after the load is the picture it was (within 0.1% of pixels)"
			: (std::string("the picture is wrong up to drawn frame #") + std::to_string(lastWrong + 1) + " after the load, of " + std::to_string(probeFirst.size()) + " drawn").c_str());
	}

	/* --kept-pictures-check: the picture a frame shows when the machine is put
	 * back on it, against the one it had the first time (chimera#190). Three
	 * things are said, each on a line of its own for a gate to hold:
	 *   back on the frame through a restore and a replay, drawn: the same
	 *     picture or not;
	 *   back on it by a restore alone, nothing run: the same or not;
	 *   after an edit just before it, replayed and drawn: how many pictures
	 *     are kept - the ones past the edit are gone, and the frame just
	 *     drawn, so soon after a load, is not kept in their place. */
	if (keptCheck >= 0)
	{
		if (keptFrame < 1) return fail(metaPath, "--kept-pictures-check: the movie never reached that frame");
		const long long had = (long long)ce_session_greenzone_picture_count(session);
		if (ce_session_seek(session, keptFrame - 1) != 0) return fail(metaPath, ce_session_last_error(session));
		if (ce_session_movie_advance(session, 0, nullptr, 1) < 0) return fail(metaPath, ce_session_last_error(session));
		std::printf("kept-pictures: back on frame %lld, replayed and drawn: %s (%lld kept, %llu bytes)\n", (long long)keptFrame,
			pictureHash(session) == keptFirst ? "the picture it had" : "NOT the picture it had", had,
			(unsigned long long)ce_session_greenzone_picture_bytes(session));
		const int64_t stored = ce_session_greenzone_nearest(session, keptFrame);
		if (stored == keptFrame && ce_session_greenzone_restore(session, keptFrame) == 0)
			std::printf("kept-pictures: back on frame %lld by a restore alone: %s\n", (long long)keptFrame,
				pictureHash(session) == keptFirst ? "the picture it had" : "NOT the picture it had");
		else std::printf("kept-pictures: frame %lld has no state of its own to restore\n", (long long)keptFrame);
		ce_session_greenzone_invalidate(session, keptFrame - 1);
		const long long afterEdit = (long long)ce_session_greenzone_picture_count(session);
		if (ce_session_seek(session, keptFrame - 1) != 0) return fail(metaPath, ce_session_last_error(session));
		if (ce_session_movie_advance(session, 0, nullptr, 1) < 0) return fail(metaPath, ce_session_last_error(session));
		std::printf("kept-pictures: after an edit before frame %lld: %lld kept before the replay, %lld after it, and the frame shows %s\n",
			(long long)keptFrame, afterEdit, (long long)ce_session_greenzone_picture_count(session),
			pictureHash(session) == keptFirst ? "the picture it had" : "what the core drew");
	}

	/* Re-recording, as many times as asked. Each pass goes back to the same
	 * frame and replays to the end, invalidating first so the replay is a real
	 * replay rather than a restore of the ending already cached - which is what
	 * a person retyping inputs makes the frontend do.
	 *
	 * The machine must come back the same every time; the dumps at the end say
	 * whether it did. The picture is the other half, and the reason this exists:
	 * a renderer whose objects live on the far side of the bridge cannot be
	 * rewound with the machine, and what that costs only shows over repetition.
	 */
	for (int64_t pass = 0; pass < rewindTimes && rewindTo >= 0; pass++)
	{
		if (ce_session_seek(session, rewindTo) != 0) return fail(metaPath, ce_session_last_error(session));
		if (ce_session_frame(session) != rewindTo) return fail(metaPath, "rewind landed on the wrong frame");
		if (const char *bad = greenzoneVerify("rewind", rewindTo))
		{
			std::fprintf(stderr, "[rewind-loop] failed on pass %lld of %lld\n",
				(long long)(pass + 1), (long long)rewindTimes);
			return fail(metaPath, bad);
		}
		ce_session_greenzone_invalidate(session, rewindTo);
		/* Stop ONE frame short and take the last one by hand, drawing. A seek
		 * replays with rendering off - correctly, nobody is looking at the
		 * frames on the way - so a picture taken after a seek would be whatever
		 * was last composed rather than this frame. The final frame has to be
		 * drawn for the screenshot to mean anything. */
		const int64_t warm = rewindWarmup < 1 ? 1 : rewindWarmup;
		if (ce_session_seek(session, frames - warm) != 0) return fail(metaPath, ce_session_last_error(session));
		for (int64_t w = 0; w < warm; w++)
		{
			if (ce_session_movie_advance(session, 0, nullptr, 1) < 0)
			{
				return fail(metaPath, ce_session_last_error(session));
			}
		}
		if (ce_session_frame(session) != frames) return fail(metaPath, "replay landed on the wrong frame");
		std::fprintf(stderr, "[rewind-loop] pass %lld of %lld done\n",
			(long long)(pass + 1), (long long)rewindTimes);
		std::fflush(stderr);
	}

	if (!recordPath.empty())
	{
		/* the session's own log, one entry per line - the shape chimera-run
		 * reads back in, so a recorded movie can simply be replayed */
		const ce_movie_log *log = ce_session_movie_log(session);
		std::string text;
		for (int64_t i = 0; i < ce_movie_log_count(log); i++)
		{
			text += ce_movie_log_entry(log, i);
			text += '\n';
		}
		if (!writeWholeFile(recordPath, reinterpret_cast<const uint8_t *>(text.data()), text.size()))
		{
			return fail(metaPath, "could not write " + recordPath);
		}
		if (ce_movie_log_count(log) != frames)
		{
			return fail(metaPath, "recorded " + std::to_string(ce_movie_log_count(log))
				+ " entries for " + std::to_string(frames) + " frames");
		}
	}

	/* --greenzone-map: every frame the history can restore without replaying,
	 * newest first, one per line - the spacing a person sees in TAStudio's
	 * green rows, read from the engine rather than from the screen. */
	if (!greenzoneMap.empty())
	{
		std::string text;
		for (int64_t f = ce_session_frame(session); f >= 0;)
		{
			const int64_t stored = ce_session_greenzone_nearest(session, f);
			if (stored < 0) break;
			text += std::to_string(stored) + "\n";
			f = stored - 1;
		}
		if (!writeWholeFile(greenzoneMap, reinterpret_cast<const uint8_t *>(text.data()), text.size()))
		{
			return fail(metaPath, "could not write " + greenzoneMap);
		}
	}

	if (!propertyTrace.empty())
	{
		const int32_t listed = ce_session_property_refresh(session);
		std::printf("prop table: %s, %d listed after the run\n", ce_session_property_dynamic(session) != 0 ? "dynamic" : "fixed", listed);
		std::printf("prop table: %s\n", ce_session_property_table(session));
	}
	if (!ramSearchBench.empty())
	{
		int32_t found = -1;
		for (int32_t d = 0; d < ce_session_domain_count(session); d++)
			if (ramSearchBench == ce_session_domain_name(session, d)) found = d;
		/* a bus is read through a function, as the frontend reads one: a byte
		 * per call (what the frontend did until chimera#180) and then in runs */
		int32_t bus = -1;
		for (int32_t b = 0; found < 0 && b < ce_session_bus_count(session); b++)
			if (ramSearchBench == ce_session_bus_name(session, b)) bus = b;
		if (bus >= 0)
		{
			struct BusUser { ce_session *s; int32_t bus; };
			BusUser user{ session, bus };
			const ce_ramsearch_read_fn perByte = [](void *u, int64_t offset, uint8_t *buf, int64_t len) -> int64_t {
				auto *bu = static_cast<BusUser *>(u);
				for (int64_t i = 0; i < len; i++) buf[i] = static_cast<uint8_t>(ce_session_bus_peek(bu->s, bu->bus, static_cast<int32_t>(offset + i)));
				return len;
			};
			const ce_ramsearch_read_fn inRuns = [](void *u, int64_t offset, uint8_t *buf, int64_t len) -> int64_t {
				auto *bu = static_cast<BusUser *>(u);
				return ce_session_bus_read(bu->s, bu->bus, offset, buf, len);
			};
			const int64_t size = ce_session_bus_size(session, bus);
			/* the bulk read must BE the peeks, byte for byte: from an odd start,
			 * across every chunk boundary, and past the end, where both read 0 */
			{
				const int64_t from = 3, span = size + 7;
				std::vector<uint8_t> runs((size_t)span), peeks((size_t)span);
				ce_session_bus_read(session, bus, from, runs.data(), span);
				for (int64_t i = 0; i < span; i++)
					peeks[(size_t)i] = from + i < size ? static_cast<uint8_t>(ce_session_bus_peek(session, bus, static_cast<int32_t>(from + i))) : 0;
				for (int64_t i = 0; i < span; i++)
					if (runs[(size_t)i] != peeks[(size_t)i])
						return fail(metaPath, "--ram-search-bench: " + ramSearchBench + " read in runs differs from its peeks at " + std::to_string(from + i));
				std::fprintf(stderr, "ram-search-bench %s: runs == peeks over %lld bytes\n", ramSearchBench.c_str(), (long long)span);
			}
			for (int way = 0; way < 2; way++)
			{
				ce_ramsearch *rs = ce_ramsearch_create(nullptr, way == 0 ? perByte : inRuns, &user, size);
				if (rs == nullptr) return fail(metaPath, "--ram-search-bench: out of memory");
				const auto t0 = std::chrono::steady_clock::now();
				ce_ramsearch_start(rs, 1, 0, 0, 0);
				const auto t1 = std::chrono::steady_clock::now();
				ce_ramsearch_search(rs, 1, 0, 0, 0, 0, 1);
				const auto t2 = std::chrono::steady_clock::now();
				const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
				std::fprintf(stderr, "ram-search-bench %s (bus, %lld bytes) %s: start %.0f ms, equal-to-0 %.0f ms (%lld left)\n",
					ramSearchBench.c_str(), (long long)size, way == 0 ? "a byte per call" : "in runs", ms(t0, t1), ms(t1, t2),
					(long long)ce_ramsearch_count(rs));
				ce_ramsearch_destroy(rs);
			}
			/* A bus that says which of it is live (chimera#218): a search told
			 * so covers those ranges and nothing else. Checked against the bus
			 * itself - every candidate is an address the core called live (to
			 * the four bytes a range is kept whole to), reads what the bus
			 * peeks there, and there are as many as the ranges hold. */
			const int32_t ranges = ce_session_bus_ranges(session, bus, nullptr, 0);
			if (ranges < 0) std::fprintf(stderr, "ram-search-bench %s: no word on live ranges, all of it is searched\n", ramSearchBench.c_str());
			else
			{
				std::vector<int64_t> pairs((size_t)ranges * 2 + 2);
				if (ce_session_bus_ranges(session, bus, pairs.data(), ranges) != ranges)
					return fail(metaPath, "--ram-search-bench: " + ramSearchBench + " changed its live ranges between two calls");
				std::vector<uint8_t> live((size_t)size, 0);
				int64_t liveBytes = 0;
				for (int32_t r = 0; r < ranges; r++)
				{
					const int64_t from = std::max<int64_t>(pairs[(size_t)r * 2], 0) & ~int64_t{ 3 };
					const int64_t to = std::min<int64_t>(size, (pairs[(size_t)r * 2] + pairs[(size_t)r * 2 + 1] + 3) & ~int64_t{ 3 });
					for (int64_t a = from; a < to; a++) { liveBytes += live[(size_t)a] == 0; live[(size_t)a] = 1; }
				}
				ce_ramsearch *rs = ce_ramsearch_create(nullptr, inRuns, &user, size);
				if (rs == nullptr || ce_ramsearch_set_ranges(rs, pairs.data(), ranges) != 0 || ce_ramsearch_start(rs, 1, 0, 0, 0) != 0)
					return fail(metaPath, "--ram-search-bench: out of memory");
				const int64_t count = ce_ramsearch_count(rs);
				if (count != liveBytes || ce_ramsearch_searched_size(rs) != liveBytes)
					return fail(metaPath, "--ram-search-bench: " + ramSearchBench + " has " + std::to_string(liveBytes) + " live bytes, the search is over " + std::to_string(count));
				for (int64_t i = 0; i < count; i++)
				{
					uint64_t address = 0;
					uint32_t current = 0, previous = 0, changes = 0;
					if (ce_ramsearch_row(rs, i, &address, &current, &previous, &changes) != 1 || address >= (uint64_t)size || !live[(size_t)address]
						|| current != static_cast<uint32_t>(ce_session_bus_peek(session, bus, static_cast<int32_t>(address))) || previous != current)
						return fail(metaPath, "--ram-search-bench: " + ramSearchBench + " candidate " + std::to_string(i) + " at " + std::to_string(address) + " is not live, or does not read what the bus peeks");
				}
				ce_ramsearch_destroy(rs);
				std::fprintf(stderr, "ram-search-bench %s: %d live ranges, %lld of %lld bytes searched, every candidate live and reading what the bus peeks\n",
					ramSearchBench.c_str(), ranges, (long long)liveBytes, (long long)size);
			}
		}
		if (found < 0 && bus < 0) return fail(metaPath, "--ram-search-bench: no domain or bus named " + ramSearchBench);
		const auto *base = found < 0 ? nullptr : reinterpret_cast<const uint8_t *>(static_cast<uintptr_t>(ce_session_domain_ptr(session, found)));
		const int64_t size = found < 0 ? 0 : ce_session_domain_size(session, found);
		for (int32_t width : { 1, 2, 4 })
		{
			if (found < 0) break;
			ce_ramsearch *rs = ce_ramsearch_create(base, nullptr, nullptr, size);
			if (rs == nullptr) return fail(metaPath, "--ram-search-bench: out of memory");
			const auto t0 = std::chrono::steady_clock::now();
			ce_ramsearch_start(rs, width, 0, 0, 0);
			const auto t1 = std::chrono::steady_clock::now();
			ce_ramsearch_search(rs, 1 /* specific value */, 0 /* equal */, 0, 0, 0, 1 /* last search */);
			const auto t2 = std::chrono::steady_clock::now();
			const int64_t left = ce_ramsearch_count(rs);
			ce_ramsearch_search(rs, 0 /* previous */, 0, 0, 0, 0, 1);
			const auto t3 = std::chrono::steady_clock::now();
			const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
			std::fprintf(stderr, "ram-search-bench %s (%lld bytes) size %d: start %.0f ms, equal-to-0 %.0f ms (%lld left), previous %.0f ms\n",
				ramSearchBench.c_str(), (long long)size, width, ms(t0, t1), ms(t1, t2), (long long)left, ms(t2, t3));
			ce_ramsearch_destroy(rs);
		}
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
		if (found < 0)
		{
			std::string had;
			for (int32_t d = 0; d < count; d++)
			{
				had += (d ? ", " : "");
				had += ce_session_domain_name(session, d);
			}
			return fail(metaPath, "no memory domain named '" + dump.first
				+ "'; this core has: " + had);
		}
		int64_t size = ce_session_domain_size(session, found);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		ce_session_domain_read(session, found, 0, bytes.data(), size);
		if (!writeWholeFile(dump.second, bytes.data(), bytes.size()))
		{
			return fail(metaPath, "could not write " + dump.second);
		}
	}

	if (!savedataDir.empty())
	{
		if (ce_session_savedata_available(session) == 0)
		{
			return fail(metaPath, "this core exports no save data");
		}
		int32_t files = ce_session_savedata_count(session);
		std::vector<uint8_t> chunk(1 << 20); // ranged reads: a big file streams
		for (int32_t f = 0; f < files; f++)
		{
			std::string path = savedataDir + "/" + ce_session_savedata_name(session, f);
			makeParentDirs(path);
			FILE *out = std::fopen(path.c_str(), "wb");
			if (out == nullptr) return fail(metaPath, "could not write " + path);
			int64_t size = ce_session_savedata_size(session, f);
			bool ok = true;
			for (int64_t off = 0; off < size && ok;)
			{
				int64_t got = ce_session_savedata_read(session, f, off, chunk.data(), static_cast<int64_t>(chunk.size()));
				if (got <= 0) { ok = false; break; }
				ok = std::fwrite(chunk.data(), 1, static_cast<size_t>(got), out) == static_cast<size_t>(got);
				off += got;
			}
			std::fclose(out);
			if (!ok) return fail(metaPath, "could not write " + path);
		}
		std::printf("savedata=%d\n", files);
	}

	if (!finalShot.empty()
		&& !writeTga(finalShot, ce_session_video(session),
			ce_session_video_width(session), ce_session_video_height(session)))
	{
		return fail(metaPath, "could not write " + finalShot);
	}

	if (!finalBuses.empty())
	{
		/* Every bus the core publishes, end to end, where the run finished.
		 * This is the machine's own memory rather than the sandbox's arena -
		 * two runs that reach the same machine agree here byte for byte, which
		 * the arena does not promise. */
		std::string text;
		for (int32_t b = 0; b < ce_session_bus_count(session); b++)
		{
			int64_t size = ce_session_bus_size(session, b);
			text += ce_session_bus_name(session, b);
			text += ' ';
			uint64_t h = 1469598103934665603ull;
			for (int64_t a = 0; a < size; a++)
			{
				h ^= (uint64_t)(uint8_t)ce_session_bus_peek(session, b, (int32_t)a);
				h *= 1099511628211ull;
			}
			char line[64];
			std::snprintf(line, sizeof line, "%016llx %lld\n", (unsigned long long)h, (long long)size);
			text += line;
		}
		if (!writeWholeFile(finalBuses, reinterpret_cast<const uint8_t *>(text.data()), text.size()))
		{
			return fail(metaPath, "could not write " + finalBuses);
		}
	}

	for (const auto &bd : finalBusDumps)
	{
		int32_t which = -1;
		for (int32_t b = 0; b < ce_session_bus_count(session); b++)
			if (bd.first == ce_session_bus_name(session, b)) { which = b; break; }
		if (which < 0) return fail(metaPath, "no bus named " + bd.first);
		int64_t size = ce_session_bus_size(session, which);
		std::vector<uint8_t> bytes(static_cast<size_t>(size));
		for (int64_t a = 0; a < size; a++)
			bytes[static_cast<size_t>(a)] = (uint8_t)ce_session_bus_peek(session, which, (int32_t)a);
		if (!writeWholeFile(bd.second, bytes.data(), bytes.size()))
			return fail(metaPath, "could not write " + bd.second);
	}

	if (!finalStatePath.empty())
	{
		/* The whole machine where the run actually ended - after any seek and
		 * replay, which is where --save-state cannot reach. */
		uint64_t len = 0;
		const uint8_t *p = ce_session_save_state(session, &len);
		if (p == nullptr) return fail(metaPath, ce_session_last_error(session));
		if (!writeWholeFile(finalStatePath, p, static_cast<size_t>(len)))
		{
			return fail(metaPath, "could not write " + finalStatePath);
		}
	}

	if (!historyOut.empty())
	{
		const auto t0 = std::chrono::steady_clock::now();
		if (historyOutLater)
		{
			/* queued, then waited for - exactly what a project save does */
			if (ce_session_history_save_later(session, historyOut.c_str(), "chimera-run") != 0)
			{
				return fail(metaPath, std::string("history (queued): ") + ce_session_last_error(session));
			}
			if (ce_session_history_save_wait(session) != 0)
			{
				return fail(metaPath, std::string("history (wait): ") + ce_session_last_error(session));
			}
		}
		else if (ce_session_history_save(session, historyOut.c_str(), "chimera-run") != 0)
		{
			return fail(metaPath, std::string("history: ") + ce_session_last_error(session));
		}
		std::fprintf(stderr, "chimera-run: history saved%s in %.1f ms\n",
			historyOutLater ? " (queued, as a project save does)" : "",
			std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
	}

	if (!metaPath.empty())
	{
		std::string meta = "status=OK\ndetail=\nframes=" + std::to_string(frames) + "\nstartframe=0\n";
		// the display aspect the core reports for the last picture, when it does
		int32_t ax = 0, ay = 0;
		if (ce_session_display_aspect(session, &ax, &ay))
			meta += "aspect=" + std::to_string(ax) + ":" + std::to_string(ay) + "\n";
		// a game core's own timer at the end of the run, when it has one
		int64_t gameMs = 0;
		if (ce_session_game_time_ms(session, &gameMs))
		{
			char text[48];
			ce_game_time_text(gameMs, text, sizeof text);
			meta += "gameTimeMs=" + std::to_string(gameMs) + "\ngameTime=" + text + "\n";
		}
		writeWholeFile(metaPath, reinterpret_cast<const uint8_t *>(meta.data()), meta.size());
	}
	std::printf("frames=%lld\n", static_cast<long long>(frames));
	ce_movie_log_free(movie);
	ce_session_free(session);
	if (multi != nullptr) ce_multifile_free(multi);
	if (project != nullptr) ce_project_free(project);
	return 0;
}
