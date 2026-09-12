/* What does a captured frame cost when the budget is full and stretches are
 * going to disk - and what does it cost with the writer on a helper?
 *
 * The other benches here measure the sandbox: what an epoch costs, what a
 * delta costs to walk and to copy. This one measures the HISTORY, and the one
 * thing about it a person actually feels: a capture that has to write a whole
 * stretch to a file before the frame can end. Ten megabytes measured 7 ms to
 * ext4 and 57 to NTFS, and evict() can do several of them in one frame once
 * the budget is full, which is the steady state of any long session.
 *
 * So the number that matters is not the mean, it is the WORST frame. Both are
 * printed; the mean is there to show that nothing was traded for the maximum.
 *
 * No sandbox and no core: a fake host with a machine of a chosen size, which
 * is what makes the writes the size a real one's are. Build and run with
 * tests/perf/run-spillbench.sh. Not part of any gate - it answers a question
 * about cost, not about correctness.
 */
#include "../../source/engine/source/state_history.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

/* The machine: a block of bytes, of which a frame changes a few pages. Big
 * enough that an anchor is worth writing and a delta is worth measuring. */
struct Machine
{
	std::vector<uint8_t> cell;
	std::vector<uint8_t> epochBase;
};
Machine g_machine;
size_t g_dirtyBytes = 256 * 1024;

double now()
{
	const auto t = std::chrono::steady_clock::now().time_since_epoch();
	return std::chrono::duration<double>(t).count();
}

void saveState(void *, chimera::WbxWriteCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	*r = {};
	cb(ud, g_machine.cell.data(), g_machine.cell.size());
}

void loadState(void *, chimera::WbxReadCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	*r = {};
	cb(ud, g_machine.cell.data(), g_machine.cell.size());
}

void epochBegin(void *, chimera::WbxReturn *r)
{
	*r = {};
	g_machine.epochBase = g_machine.cell;
}

/* A delta is (offset, length, bytes) runs - the shape a real one has, and the
 * only property that matters here is that it costs what the frame changed. */
void saveDelta(void *, bool, chimera::WbxWriteCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	*r = {};
	std::vector<std::pair<uint64_t, uint64_t>> runs;
	const size_t page = 4096;
	for (size_t at = 0; at < g_machine.cell.size(); at += page)
	{
		const size_t len = std::min(page, g_machine.cell.size() - at);
		if (g_machine.epochBase.size() == g_machine.cell.size()
			&& std::memcmp(&g_machine.cell[at], &g_machine.epochBase[at], len) == 0) continue;
		if (!runs.empty() && runs.back().first + runs.back().second == at) runs.back().second += len;
		else runs.emplace_back(at, len);
	}
	const uint64_t n = runs.size();
	cb(ud, &n, sizeof n);
	for (const auto &run : runs)
	{
		cb(ud, &run.first, sizeof run.first);
		cb(ud, &run.second, sizeof run.second);
		cb(ud, &g_machine.cell[run.first], run.second);
	}
}

void loadDelta(void *, chimera::WbxReadCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	*r = {};
	uint64_t n = 0;
	cb(ud, &n, sizeof n);
	for (uint64_t i = 0; i < n; i++)
	{
		uint64_t at = 0, len = 0;
		cb(ud, &at, sizeof at);
		cb(ud, &len, sizeof len);
		if (at + len > g_machine.cell.size()) return;
		cb(ud, &g_machine.cell[at], len);
	}
	g_machine.epochBase.clear();
}

/* The union of two, later wins - enough for the bands to coarsen. */
void composeDelta(chimera::WbxReadCb ra, uintptr_t aud, chimera::WbxReadCb rb, uintptr_t bud,
	chimera::WbxWriteCb out, uintptr_t oud, chimera::WbxReturn *r)
{
	*r = {};
	std::vector<uint8_t> merged(g_machine.cell.size());
	std::vector<bool> have(g_machine.cell.size() / 4096 + 1, false);
	for (int which = 0; which < 2; which++)
	{
		chimera::WbxReadCb cb = which == 0 ? ra : rb;
		const uintptr_t ud = which == 0 ? aud : bud;
		uint64_t n = 0;
		cb(ud, &n, sizeof n);
		for (uint64_t i = 0; i < n; i++)
		{
			uint64_t at = 0, len = 0;
			cb(ud, &at, sizeof at);
			cb(ud, &len, sizeof len);
			if (at + len > merged.size()) return;
			cb(ud, &merged[at], len);
			for (uint64_t p = at; p < at + len; p += 4096) have[p / 4096] = true;
		}
	}
	std::vector<std::pair<uint64_t, uint64_t>> runs;
	for (size_t p = 0; p < have.size(); p++)
	{
		if (!have[p]) continue;
		const uint64_t at = p * 4096;
		const uint64_t len = std::min<uint64_t>(4096, merged.size() - at);
		if (!runs.empty() && runs.back().first + runs.back().second == at) runs.back().second += len;
		else runs.emplace_back(at, len);
	}
	const uint64_t n = runs.size();
	out(oud, &n, sizeof n);
	for (const auto &run : runs)
	{
		out(oud, &run.first, sizeof run.first);
		out(oud, &run.second, sizeof run.second);
		out(oud, &merged[run.first], run.second);
	}
}

chimera::HostApi host()
{
	chimera::HostApi api{};
	api.wbx_save_state = saveState;
	api.wbx_load_state = loadState;
	api.wbx_epoch_begin = epochBegin;
	api.wbx_save_delta = saveDelta;
	api.wbx_load_delta = loadDelta;
	api.wbx_compose_delta = composeDelta;
	return api;
}

/* A frame writes a wandering slice of the machine, so deltas are a real size
 * and every frame is a different set of pages. */
void advance(int64_t frame)
{
	const size_t at = static_cast<size_t>(frame * 4093) % (g_machine.cell.size() - g_dirtyBytes);
	std::memset(&g_machine.cell[at], static_cast<int>(frame & 0xFF), g_dirtyBytes);
}

struct Result
{
	double mean = 0, worst = 0, p99 = 0, total = 0;
	double saveInLine = 0, saveQueued = 0, saveWhole = 0;
	chimera::StateHistory::Costs costs;
	uint64_t disk = 0;
};

int64_t g_anchorSpacing = 60;
uint64_t g_diskTimes = 8;

Result run(bool threaded, int frames, size_t machineBytes, uint64_t budget, const std::string &dir)
{
	std::filesystem::remove_all(dir);
	std::filesystem::create_directories(dir);
	g_machine.cell.assign(machineBytes, 0);
	g_machine.epochBase.clear();

	const chimera::HostApi api = host();
	chimera::StateHistory h;
	h.configure(&api, nullptr, budget);
	h.helpers(threaded);
	/* An anchor every `g_anchorSpacing` frames, because a stretch can only go
	 * to disk once there is a newer one - the newest is never spilled. The
	 * default 600 would make one stretch out of a run this length and nothing
	 * would ever reach the file. */
	h.bands(120, 1800, 3, 1200, g_anchorSpacing);
	h.spillTo(dir.c_str());
	/* 0 means no limit on the file, which takes evictDisk and its compactions
	 * out of the picture - they are the other thing that touches the file, and
	 * telling the two apart is half of what this bench is for. */
	if (g_diskTimes != 0) h.diskBudget(budget * g_diskTimes);

	if (getenv("CHIMERA_HISTORY_TRACE") != nullptr)
	{
		fprintf(stderr, "==== pass: %s ====\n", threaded ? "helpers" : "in line");
		fflush(stderr);
	}
	std::vector<double> each;
	each.reserve(static_cast<size_t>(frames));
	h.capture(0);
	for (int64_t f = 1; f <= frames; f++)
	{
		h.beforeAdvance();
		advance(f);
		const double t0 = now();
		h.capture(f);
		each.push_back(now() - t0);
	}
	/* and what a project save costs: the whole history to a file, which is the
	 * longest thing a save does and was happening on this thread */
	std::string err;
	const std::string saved = dir + "/history.bin";
	const double s0 = now();
	h.saveTo(saved.c_str(), "bench", err);
	const double s1 = now();
	h.saveToLater(saved.c_str(), "bench", err);
	const double s2 = now();
	h.saveWait(err);
	const double s3 = now();

	Result out;
	out.costs = h.costs();
	out.saveInLine = s1 - s0;
	out.saveQueued = s2 - s1;
	out.saveWhole = s3 - s1;
	out.disk = h.diskBytes();
	for (double d : each) { out.total += d; out.worst = std::max(out.worst, d); }
	out.mean = out.total / static_cast<double>(each.size());
	std::sort(each.begin(), each.end());
	out.p99 = each[static_cast<size_t>(each.size() * 99 / 100)];
	std::filesystem::remove_all(dir);
	return out;
}

} // namespace

int main(int argc, char **argv)
{
	const size_t machineMb = argc > 1 ? static_cast<size_t>(atoll(argv[1])) : 64;
	const int frames = argc > 2 ? atoi(argv[2]) : 400;
	const size_t budgetMb = argc > 3 ? static_cast<size_t>(atoll(argv[3])) : 16;
	const std::string dir = argc > 4 ? argv[4] : "spillbench-work";
	g_dirtyBytes = argc > 5 ? static_cast<size_t>(atoll(argv[5])) << 10 : 256 * 1024;
	g_anchorSpacing = argc > 6 ? atoll(argv[6]) : 60;
	g_diskTimes = argc > 7 ? static_cast<uint64_t>(atoll(argv[7])) : 8;

	const size_t machineBytes = machineMb << 20;
	const uint64_t budget = static_cast<uint64_t>(budgetMb) << 20;

	/* Each mode twice, interleaved, best of the two.
	 *
	 * The first run of anything here warms the page cache and the allocator, so
	 * whichever mode went first used to look worse - which turned a 1.1x win
	 * into a 0.7x "regression" from one run to the next. Interleaving and
	 * taking the best of each is the cheapest way to stop measuring the order. */
	Result in_line, helped;
	for (int rep = 0; rep < 2; rep++)
	{
		const Result a = run(false, frames, machineBytes, budget, dir);
		const Result b = run(true, frames, machineBytes, budget, dir);
		if (rep == 0 || a.total < in_line.total) in_line = a;
		if (rep == 0 || b.total < helped.total) helped = b;
	}

	std::printf("%4zu MB machine, %4zu KB/frame, %3zu MB budget, %d frames"
		" (%.0f MB reached disk)\n",
		machineMb, g_dirtyBytes >> 10, budgetMb, frames, in_line.disk / 1048576.0);
	std::printf("    in line: mean %7.3f ms   99th %7.3f ms   WORST %8.3f ms   total %6.2f s\n",
		in_line.mean * 1e3, in_line.p99 * 1e3, in_line.worst * 1e3, in_line.total);
	std::printf("    helpers: mean %7.3f ms   99th %7.3f ms   WORST %8.3f ms   total %6.2f s\n",
		helped.mean * 1e3, helped.p99 * 1e3, helped.worst * 1e3, helped.total);
	std::printf("             compactions: %llu in line, %llu helped;"
		" the loop waited for the writer %llu times, %.1f ms in all\n",
		(unsigned long long)in_line.costs.compactions, (unsigned long long)helped.costs.compactions,
		(unsigned long long)helped.costs.waits, helped.costs.waitSeconds * 1e3);
	/* what the writer was handed against what reached the file: the logical
	 * body the history reserved, and the zstd frame the writer appended */
	std::printf("             spilled: %.1f MB of bodies became %.1f MB in the file (%.1fx)\n",
		helped.costs.spilledRaw / 1048576.0, helped.costs.spilledPacked / 1048576.0,
		helped.costs.spilledPacked > 0 ? static_cast<double>(helped.costs.spilledRaw) / helped.costs.spilledPacked : 0.0);
	std::printf("    a project save: in line %7.1f ms   queued %7.1f ms to return"
		" (%7.1f ms to finish)\n",
		helped.saveInLine * 1e3, helped.saveQueued * 1e3, helped.saveWhole * 1e3);
	std::printf("             %.1fx the mean, %.1fx the 99th, %.1fx the worst frame\n",
		helped.mean > 0 ? in_line.mean / helped.mean : 0.0,
		helped.p99 > 0 ? in_line.p99 / helped.p99 : 0.0,
		helped.worst > 0 ? in_line.worst / helped.worst : 0.0);
	return 0;
}
