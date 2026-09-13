/* test_state_history.cpp - the history's shape, without a machine.
 *
 * A link spans one frame when it is captured and more once the history has
 * thinned it, so "which frames can this produce" stopped being arithmetic on
 * the anchor and became a search. That search, and the file format that has to
 * carry the strides, is what this pins. Capturing and restoring need a sandbox
 * and are proven end to end by the synthetic witness.
 */

#include "../source/state_history.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <new>
#include <vector>

namespace
{

const char kPath[] = "test_state_history.tmp";

void put64(std::vector<uint8_t> &out, uint64_t v)
{
	const uint8_t *p = reinterpret_cast<const uint8_t *>(&v);
	out.insert(out.end(), p, p + sizeof v);
}

void putStr(std::vector<uint8_t> &out, const char *s)
{
	out.insert(out.end(), s, s + std::strlen(s));
}

void write(const std::vector<uint8_t> &bytes)
{
	std::FILE *f = std::fopen(kPath, "wb");
	assert(f != nullptr);
	if (!bytes.empty()) assert(std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size());
	std::fclose(f);
}

/* One segment with an anchor at `anchorFrame` and links landing exactly where
 * `landings` says - which is how a thinned history looks on disk. */
std::vector<uint8_t> historyFile(const char *magic, const char *machineId,
	int64_t anchorFrame, const std::vector<int64_t> &landings)
{
	std::vector<uint8_t> out;
	putStr(out, magic);
	put64(out, std::strlen(machineId));
	putStr(out, machineId);
	put64(out, 1);                      /* one segment */
	put64(out, static_cast<uint64_t>(anchorFrame));
	put64(out, 4);                      /* an anchor of four bytes; nothing loads it here */
	out.insert(out.end(), { 1, 2, 3, 4 });
	put64(out, 0);                      /* and no note */
	put64(out, landings.size());
	for (int64_t at : landings)
	{
		put64(out, static_cast<uint64_t>(at));
		put64(out, 0);                  /* no note */
		put64(out, 2);
		out.insert(out.end(), { 9, 9 });
	}
	return out;
}

} // namespace

/* ---- a machine small enough to check by hand ----
 *
 * The history's own logic - which landings a band keeps, what a merge does to
 * the tiling, which links a restore walks - can be wrong in ways no core will
 * show you. The synthetic witness cannot: its core rewrites its whole writable
 * set every frame, so every delta fully determines the machine and any
 * subsequence of them lands in the right place. That makes it blind to exactly
 * the mistakes this file is for.
 *
 * So: a machine of numbered cells, where a frame writes only SOME of them. Now
 * a chain that skips a link, or merges the wrong pair, or mislabels where a
 * link lands, produces a machine that differs - and says so.
 *
 * The merge here is the obvious one (union, later value wins) rather than
 * miniBox's. What miniBox's does is its own to prove, and its unit tests do;
 * what is under test here is everything the engine decides around it.
 */
namespace
{

struct Machine
{
	static constexpr size_t kCells = 64;
	uint8_t cell[kCells] = { 0 };
	std::vector<uint8_t> epochBase;      /* the machine as the open epoch found it */
};

Machine g_machine;

using Cells = std::vector<std::pair<uint8_t, uint8_t>>;   /* index -> value, ascending */

Cells readCells(chimera::WbxReadCb cb, uintptr_t ud)
{
	uint32_t n = 0;
	cb(ud, &n, sizeof n);
	Cells out(n);
	for (auto &c : out) { cb(ud, &c.first, 1); cb(ud, &c.second, 1); }
	return out;
}

void writeCells(chimera::WbxWriteCb cb, uintptr_t ud, const Cells &c)
{
	const uint32_t n = static_cast<uint32_t>(c.size());
	cb(ud, &n, sizeof n);
	for (const auto &e : c) { cb(ud, &e.first, 1); cb(ud, &e.second, 1); }
}

/* Set to have the next delta load refuse, the way a damaged one would. Nothing
 * else here can produce that, and what the history does about it - leave the
 * machine somewhere real rather than half way along a chain - is the point. */
int g_refuseDeltaLoadIn = -1;

/* Set to have the next N captures fail the way a machine out of memory fails.
 * Nothing else here can produce that, and it is the one condition the history
 * is expected to survive rather than report. */
int g_outOfMemoryFor = 0;

void fakeSaveState(void *, chimera::WbxWriteCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	if (g_outOfMemoryFor > 0) { g_outOfMemoryFor--; throw std::bad_alloc(); }
	*r = {};
	cb(ud, g_machine.cell, Machine::kCells);
}

void fakeLoadState(void *, chimera::WbxReadCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	*r = {};
	cb(ud, g_machine.cell, Machine::kCells);
}

void fakeEpochBegin(void *, chimera::WbxReturn *r)
{
	*r = {};
	g_machine.epochBase.assign(g_machine.cell, g_machine.cell + Machine::kCells);
}

void fakeSaveDelta(void *, bool forward, chimera::WbxWriteCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	if (g_outOfMemoryFor > 0) { g_outOfMemoryFor--; throw std::bad_alloc(); }
	*r = {};
	if (g_machine.epochBase.empty()) { std::snprintf(r->errorMessage, sizeof r->errorMessage, "no epoch"); return; }
	Cells changed;
	for (size_t i = 0; i < Machine::kCells; i++)
	{
		if (g_machine.cell[i] == g_machine.epochBase[i]) continue;
		// forward: as the frame left it. reverse: as the frame found it.
		changed.emplace_back(static_cast<uint8_t>(i), forward ? g_machine.cell[i] : g_machine.epochBase[i]);
	}
	writeCells(cb, ud, changed);
}

void fakeLoadDelta(void *, chimera::WbxReadCb cb, uintptr_t ud, chimera::WbxReturn *r)
{
	*r = {};
	if (g_refuseDeltaLoadIn == 0)
	{
		g_refuseDeltaLoadIn = -1;
		/* a damaged delta is not a no-op: it writes part of the machine and
		 * then gives up, which is exactly what makes this worth containing */
		g_machine.cell[0] = 0xDE;
		g_machine.cell[1] = 0xAD;
		std::snprintf(r->errorMessage, sizeof r->errorMessage, "memory delta apply failed");
		return;
	}
	if (g_refuseDeltaLoadIn > 0) g_refuseDeltaLoadIn--;
	for (const auto &c : readCells(cb, ud)) g_machine.cell[c.first] = c.second;
	g_machine.epochBase.clear();
}

void fakeComposeDelta(chimera::WbxReadCb a, uintptr_t aud, chimera::WbxReadCb b, uintptr_t bud,
	chimera::WbxWriteCb out, uintptr_t oud, chimera::WbxReturn *r)
{
	*r = {};
	Cells merged = readCells(a, aud);
	for (const auto &c : readCells(b, bud))
	{
		auto it = std::lower_bound(merged.begin(), merged.end(), c.first,
			[](const std::pair<uint8_t, uint8_t> &e, uint8_t v) { return e.first < v; });
		if (it != merged.end() && it->first == c.first) it->second = c.second;   /* the later wins */
		else merged.insert(it, c);
	}
	writeCells(out, oud, merged);
}

chimera::HostApi fakeHost()
{
	chimera::HostApi api{};
	api.wbx_save_state = fakeSaveState;
	api.wbx_load_state = fakeLoadState;
	api.wbx_epoch_begin = fakeEpochBegin;
	api.wbx_save_delta = fakeSaveDelta;
	api.wbx_load_delta = fakeLoadDelta;
	api.wbx_compose_delta = fakeComposeDelta;
	return api;
}

/* The spill file, whatever it is called: the name carries the process and the
 * instance now, so that two histories handed the same directory cannot open the
 * same file. Tests ask the directory, not the name. */
std::filesystem::path spillFileIn(const std::string &dir)
{
	std::error_code ec;
	for (const auto &entry : std::filesystem::directory_iterator(dir, ec))
	{
		const std::string name = entry.path().filename().string();
		if (name.rfind("history-spill-", 0) == 0) return entry.path();
	}
	return {};
}

/* Frame n writes three cells, which cells depending on n - so a delta is a
 * PART of the machine and the order they are applied in matters. */
void advance(int64_t frame)
{
	for (int k = 0; k < 3; k++)
	{
		g_machine.cell[(frame * 7 + k * 11) % Machine::kCells] = static_cast<uint8_t>(frame);
	}
}

} // namespace

int main(void)
{
	std::string error;

	{ // an empty history round trips, and an absent one is a cold cache
		chimera::StateHistory h;
		assert(h.saveTo(kPath, "machine", error));
		assert(h.loadFrom(kPath, "machine", error));
		assert(h.count() == 0);
		assert(h.nearest(0) == -1);

		std::remove(kPath);
		assert(h.loadFrom(kPath, "machine", error));   /* no file at all is not a failure */
	}

	{ // strides above one: every landing is offered, nothing between them is
		write(historyFile("ChimeraHistory3", "machine", 8, { 10, 12, 16 }));
		chimera::StateHistory h;
		assert(h.loadFrom(kPath, "machine", error));
		assert(h.count() == 4);                        /* the anchor and three links */

		assert(h.nearest(7) == -1);                    /* before the anchor there is nothing */
		assert(h.nearest(8) == 8);
		assert(h.nearest(9) == 8);                     /* inside a span, not at its end */
		assert(h.nearest(10) == 10);
		assert(h.nearest(11) == 10);
		assert(h.nearest(12) == 12);
		assert(h.nearest(15) == 12);                   /* the four frame span */
		assert(h.nearest(16) == 16);
		assert(h.nearest(1000) == 16);                 /* past the end is the end */
	}

	{ // a history of another machine is dropped rather than refused
		write(historyFile("ChimeraHistory3", "one machine", 0, { 1, 2 }));
		chimera::StateHistory h;
		assert(h.loadFrom(kPath, "another machine", error));
		assert(h.count() == 0);
	}

	{ // and so is one an older build wrote: losing a cache costs replaying
		for (const char *older : { "ChimeraHistory1", "ChimeraHistory2" })
		{
			write(historyFile(older, "machine", 0, { 1, 2 }));
			chimera::StateHistory h;
			assert(h.loadFrom(kPath, "machine", error));
			assert(h.count() == 0);
		}
	}

	{ // something that is not a history at all is worth saying out loud
		write(historyFile("NotAHistory!!!!", "machine", 0, { 1, 2 }));
		chimera::StateHistory h;
		error.clear();
		assert(!h.loadFrom(kPath, "machine", error));
		assert(!error.empty());
		assert(h.count() == 0);
	}

	{ // links that stand still or go backwards would offer frames they cannot
	  // walk to, so the file is damaged rather than merely odd
		write(historyFile("ChimeraHistory3", "machine", 0, { 4, 4 }));
		chimera::StateHistory h;
		assert(!h.loadFrom(kPath, "machine", error));
		assert(h.count() == 0);

		write(historyFile("ChimeraHistory3", "machine", 0, { 6, 3 }));
		assert(!h.loadFrom(kPath, "machine", error));
		assert(h.count() == 0);

		write(historyFile("ChimeraHistory3", "machine", 10, { 9 }));
		assert(!h.loadFrom(kPath, "machine", error));
		assert(h.count() == 0);
	}

	{ // a file that stops in the middle is damage, not a short history
		auto bytes = historyFile("ChimeraHistory3", "machine", 0, { 1, 2, 3 });
		bytes.resize(bytes.size() - 5);
		write(bytes);
		chimera::StateHistory h;
		assert(!h.loadFrom(kPath, "machine", error));
		assert(h.count() == 0);
	}

	{ // Every frame the history offers must be a frame it can actually produce,
	  // after the bands have merged most of them away.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		chimera::StateHistory h;
		h.configure(&api, nullptr, 64ull << 20);
		/* narrow enough that coarsening runs almost every frame */
		h.bands(2, 6, 3, 12, 1000);

		std::vector<std::array<uint8_t, Machine::kCells>> truth;
		truth.resize(1);
		std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
		h.capture(0);

		const int64_t kFrames = 120;
		for (int64_t f = 1; f <= kFrames; f++)
		{
			h.beforeAdvance();
			advance(f);
			const uint8_t note[2] = { static_cast<uint8_t>(f & 0xFF), 0xA5 };
			h.capture(f, note, sizeof note);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}

		/* the bands really did thin it, or the rest of this proves nothing */
		assert(h.count() < kFrames / 2);

		/* but not where the work is: the near band's promise is every frame,
		 * and it is the one somebody actually feels */
		for (int64_t f = kFrames - 1; f <= kFrames; f++) assert(h.nearest(f) == f);

		int64_t checked = 0;
		for (int64_t f = 0; f <= kFrames; f++)
		{
			if (h.nearest(f) != f) continue;   /* not a frame it claims to hold */
			assert(h.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			checked++;
		}
		assert(checked > 4);   /* including some the bands merged their way to */

		/* The caller's note rides along, and a merge keeps the note of the frame
		 * the surviving link LANDS on - the note describes that frame, not the
		 * ones composed into it. */
		int64_t withNotes = 0;
		for (int64_t f = 1; f <= kFrames; f++)
		{
			if (h.nearest(f) != f) continue;
			size_t len = 0;
			const uint8_t *note = h.noteFor(f, len);
			assert(note != nullptr && len == 2);
			assert(note[0] == static_cast<uint8_t>(f & 0xFF) && note[1] == 0xA5);
			withNotes++;
		}
		assert(withNotes > 4);
	}

	{ // A pinned frame stays reachable however hard the bands thin around it -
	  // the marker somebody wants to jump to instantly.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		chimera::StateHistory h;
		h.configure(&api, nullptr, 64ull << 20);
		h.bands(2, 6, 3, 12, 1000);

		/* frames deliberately off every band's grid, so nothing but the pin
		 * could keep them */
		const int64_t wanted[] = { 7, 13, 31, 55 };
		for (int64_t f : wanted) h.pin(f, true);

		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		h.capture(0);
		for (int64_t f = 1; f <= 120; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}

		for (int64_t f : wanted)
		{
			assert(h.nearest(f) == f);      /* still there */
			assert(h.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
		}

		/* and unpinning lets the bands have them - checked on a second run,
		 * because coarsening a frame already past is not something that happens
		 * again just for being asked */
		chimera::StateHistory loose;
		g_machine = Machine{};
		loose.configure(&api, nullptr, 64ull << 20);
		loose.bands(2, 6, 3, 12, 1000);
		loose.capture(0);
		for (int64_t f = 1; f <= 120; f++)
		{
			loose.beforeAdvance();
			advance(f);
			loose.capture(f);
		}
		int64_t survived = 0;
		for (int64_t f : wanted) if (loose.nearest(f) == f) survived++;
		assert(survived < 4);   /* the pin was doing the work, not luck */
	}

	{ // A history whose links have been merged still survives a round trip to
	  // disk, landings and all.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		chimera::StateHistory h;
		h.configure(&api, nullptr, 64ull << 20);
		h.bands(2, 6, 3, 12, 1000);
		h.capture(0);
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		for (int64_t f = 1; f <= 60; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}
		assert(h.saveTo(kPath, "fake", error));

		chimera::StateHistory back;
		back.configure(&api, nullptr, 64ull << 20);
		assert(back.loadFrom(kPath, "fake", error));
		assert(back.count() == h.count());
		for (int64_t f = 0; f <= 60; f++)
		{
			if (back.nearest(f) != f) continue;
			assert(back.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
		}
	}

	{ // A spill that cannot be written says so, and carries on. The history's
	  // fallback is to thin in memory, which is right and completely silent -
	  // from a piano roll it looks like the greenzone going sparse for no
	  // reason, so the fact has to be somewhere a frontend can ask for it.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		chimera::StateHistory h;
		h.configure(&api, nullptr, 512);
		h.bands(2, 6, 3, 12, 8);
		/* a directory that is not there: fopen fails exactly as it does on a
		 * disk with nothing left, without needing one */
		h.spillTo("work-history-nowhere/nor-here");
		h.capture(0);
		assert(!h.spillFailed());   /* nothing has been asked of it yet */

		for (int64_t f = 1; f <= 60; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
		}

		assert(h.spillFailed());
		assert(h.bytes() <= 512);          /* it kept its budget the other way */
		assert(h.count() > 0);             /* and it is still a history */
		assert(h.nearest(60) == 60);       /* with the work still in it */

		/* somewhere it can write is a fresh chance, and the flag says so */
		std::filesystem::create_directories("work-history-elsewhere");
		h.spillTo("work-history-elsewhere");
		assert(!h.spillFailed());
		std::filesystem::remove_all("work-history-elsewhere");
	}

	{ // Running out of memory halves the budget instead of ending the session.
	  //
	  // The history is the biggest thing Chimera holds that it does not need, so
	  // it is where the end of memory is met first - and a greenzone that has
	  // quietly become half as deep is a run that continues, where a throw out of
	  // a capture is a session lost.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		chimera::StateHistory h;
		/* high enough that four halvings still clear the floor */
		h.configure(&api, nullptr, 1024ull * 1024 * 1024);
		h.bands(2, 6, 3, 12, 8);
		h.capture(0);
		const uint64_t before = h.budget();

		/* four failures in a row: one halving is unlikely to be enough on a
		 * machine that has actually run out, so it keeps going */
		g_outOfMemoryFor = 4;
		h.beforeAdvance();
		advance(1);
		h.capture(1);
		assert(g_outOfMemoryFor == 0);        /* every one of them was answered */
		assert(h.budget() == before / 16);    /* halved once per failure */
		assert(h.budget() != 0);              /* and never off: zero is "no history" */

		/* the frame that provoked it is stored, because the last try succeeded */
		assert(h.nearest(1) == 1);

		/* and it stops at the floor rather than halving towards nothing */
		g_outOfMemoryFor = 1000;
		h.beforeAdvance();
		advance(2);
		h.capture(2);
		assert(h.budget() >= 1);
		assert(g_outOfMemoryFor > 0);         /* it gave up rather than spin */
		g_outOfMemoryFor = 0;
	}

	{ // The spill file has a budget of its own, and the oldest goes first.
	  //
	  // The memory budget is met by MOVING bytes to disk, so without this one
	  // half of what a history costs was bounded and the other half was not: six
	  // thousand Game Boy frames put 1.5GB in the file and nothing ever took any
	  // of it back.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		std::filesystem::remove_all("work-history-disk");
		std::filesystem::create_directories("work-history-disk");
		chimera::StateHistory h;
		h.configure(&api, nullptr, 512);
		h.bands(2, 6, 3, 12, 8);
		h.spillTo("work-history-disk");
		const uint64_t kDisk = 8192;   /* what the FILE may weigh */
		h.diskBudget(kDisk);
		h.capture(0);
		for (int64_t f = 1; f <= 400; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			/* held every frame, not merely at the end: a limit that is only true
			 * once is a limit nobody can rely on while they work */
			assert(h.diskBytes() <= kDisk / 2);
		}
		/* and the FILE is held too, not just the count of what is live in it -
		 * dropping the oldest without reclaiming the room it held would be a
		 * limit on paper only */
		h.flushWrites();
		const uint64_t onDisk = std::filesystem::file_size(spillFileIn("work-history-disk"));
		assert(onDisk <= kDisk);   /* the number given, on the number `ls` shows */
		/* and the budget counts the FILE: what is live in it is part of it */
		assert(h.diskBytes() <= onDisk);

		/* what is left still works: the newest frames are reachable, which is
		 * the half of the run the oldest was dropped to protect */
		assert(h.restore(h.nearest(400), error));
		assert(h.nearest(1) < h.nearest(400));

		/* and no limit means no limit - the behaviour a year of runs had */
		chimera::StateHistory u;
		u.configure(&api, nullptr, 512);
		u.bands(2, 6, 3, 12, 8);
		std::filesystem::remove_all("work-history-nolimit");
		std::filesystem::create_directories("work-history-nolimit");
		u.spillTo("work-history-nolimit");
		u.capture(0);
		for (int64_t f = 1; f <= 400; f++) { u.beforeAdvance(); advance(f); u.capture(f); }
		/* the same run with no limit keeps everything it ever spilled, which is
		 * the behaviour this budget exists to bound */
		u.flushWrites();
		fprintf(stderr, "  [disk budget] bounded %llu, unbounded %llu\n",
			(unsigned long long)h.diskBytes(), (unsigned long long)u.diskBytes());
		assert(u.diskBytes() > h.diskBytes());
	}
	std::filesystem::remove_all("work-history-disk");
	std::filesystem::remove_all("work-history-nolimit");

	{ // A budget too small to hold the run: the far end goes to disk, and the
	  // frames out there are still frames the history can produce.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};

		std::filesystem::create_directories("work-history-spill");
		chimera::StateHistory h;
		/* small enough that it must spill within a few segments, and anchors
		 * often enough that there are segments to spill */
		h.configure(&api, nullptr, 512);
		h.bands(2, 6, 3, 12, 8);
		h.spillTo("work-history-spill");
		h.capture(0);

		const int64_t kFrames = 120;
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		for (int64_t f = 1; f <= kFrames; f++)
		{
			h.beforeAdvance();
			advance(f);
			const uint8_t note[2] = { static_cast<uint8_t>(f & 0xFF), 0xA5 };
			h.capture(f, note, sizeof note);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}

		/* it really did spill, or this proves only that nothing broke. The
		 * FILE is asked rather than the history, so the writer has to have
		 * caught up first - it is allowed to lag, and everything that needs
		 * the bytes waits for them on its own. */
		h.flushWrites();
		assert(!spillFileIn("work-history-spill").empty());
		assert(h.bytes() <= 512);
		assert(std::filesystem::file_size(spillFileIn("work-history-spill")) > 512);

		/* an early frame, which can only be out on disk by now */
		const int64_t old = h.nearest(12);
		assert(old >= 0 && old <= 12);
		assert(h.restore(old, error));
		assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(old)].data(), Machine::kCells) == 0);

		/* and every frame it offers, wherever it is being kept */
		int64_t checked = 0;
		for (int64_t f = 0; f <= kFrames; f++)
		{
			if (h.nearest(f) != f) continue;
			assert(h.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			checked++;
		}
		assert(checked > 4);

		/* a saved history carries the spilled stretches through, and what comes
		 * back produces the same frames */
		assert(h.saveTo(kPath, "fake", error));
		chimera::StateHistory back;
		back.configure(&api, nullptr, 64ull << 20);
		assert(back.loadFrom(kPath, "fake", error));
		for (int64_t f = 0; f <= kFrames; f++)
		{
			if (h.nearest(f) != f) continue;
			assert(back.nearest(f) == f);
			assert(back.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			/* the notes came through the spill file and the saved history alike */
			size_t len = 0;
			const uint8_t *note = back.noteFor(f, len);
			if (f == 0) continue;
			assert(note != nullptr && len == 2);
			assert(note[0] == static_cast<uint8_t>(f & 0xFF) && note[1] == 0xA5);
		}
	}
	{ // A stretch spilled before the far band reached it is settled onto the far
	  // grid once the band does: read back, composed a few merges a frame, and
	  // rewritten. The same run with a far stride of one - keep everything -
	  // is the control: it offers more old frames and weighs more on disk.
		const chimera::HostApi api = fakeHost();
		auto run = [&](const char *dir, int64_t farStride, chimera::StateHistory &h,
			std::vector<std::array<uint8_t, Machine::kCells>> &truth)
		{
			g_machine = Machine{};
			std::filesystem::remove_all(dir);
			std::filesystem::create_directories(dir);
			h.configure(&api, nullptr, 512);
			h.bands(2, 6, 3, farStride, 8);
			h.spillTo(dir);
			h.capture(0);
			truth.assign(1, {});
			for (int64_t f = 1; f <= 160; f++)
			{
				h.beforeAdvance();
				advance(f);
				h.capture(f);
				std::array<uint8_t, Machine::kCells> at{};
				std::memcpy(at.data(), g_machine.cell, Machine::kCells);
				truth.push_back(at);
			}
		};
		chimera::StateHistory settled, dense;
		std::vector<std::array<uint8_t, Machine::kCells>> truthS, truthD;
		run("work-history-settle", 12, settled, truthS);
		run("work-history-settle-control", 1, dense, truthD);

		/* the far band - everything older than near + mid - offers fewer frames
		 * once settled, and the file is lighter for it. "The file" is what has
		 * been written: the disk count is taken when the writer reports, so both
		 * wait for their writers before they are weighed against each other. */
		settled.flushWrites();
		dense.flushWrites();
		int64_t offeredS = 0, offeredD = 0;
		for (int64_t f = 8; f < 160 - 2 - 6 - 8; f++)
		{
			if (settled.nearest(f) == f) offeredS++;
			if (dense.nearest(f) == f) offeredD++;
		}
		assert(offeredS > 0 && offeredS < offeredD);
		assert(settled.diskBytes() < dense.diskBytes());

		/* and every frame either still offers is exact, wherever it is kept */
		for (int64_t f = 0; f <= 160; f++)
		{
			if (settled.nearest(f) == f)
			{
				assert(settled.restore(f, error));
				assert(std::memcmp(g_machine.cell, truthS[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			}
			if (dense.nearest(f) == f)
			{
				assert(dense.restore(f, error));
				assert(std::memcmp(g_machine.cell, truthD[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			}
		}
	}
	std::filesystem::remove_all("work-history-settle");
	std::filesystem::remove_all("work-history-settle-control");

	{ // An edit that lands on the last frame of a spilled stretch starts a new
	  // stretch. A delta pushed onto the spilled one would sit in memory while
	  // every restore reads the file, where the old timeline's links still are.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-edit");
		std::filesystem::create_directories("work-history-edit");
		chimera::StateHistory h;
		h.configure(&api, nullptr, 512);
		h.bands(2, 6, 3, 12, 8);
		h.spillTo("work-history-edit");
		h.capture(0);
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		for (int64_t f = 1; f <= 40; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}
		/* frame 8 closes the first stretch, which a 512 byte budget has spilled */
		assert(h.nearest(8) == 8);
		assert(h.restore(8, error));
		/* an edit says so: a capture of a frame the history already reaches past
		 * is a replay, and keeps what is ahead */
		h.invalidateAfter(8);
		h.beforeAdvance();
		g_machine.cell[3] ^= 0x5C;   /* the edit */
		advance(9);
		std::array<uint8_t, Machine::kCells> edited{};
		std::memcpy(edited.data(), g_machine.cell, Machine::kCells);
		h.capture(9);
		assert(h.nearest(INT64_MAX) == 9);
		/* back to 8 and forward to the edited 9: the old 9 must not come back */
		assert(h.restore(8, error));
		assert(std::memcmp(g_machine.cell, truth[8].data(), Machine::kCells) == 0);
		assert(h.restore(9, error));
		assert(std::memcmp(g_machine.cell, edited.data(), Machine::kCells) == 0);

		/* and a saved history does not carry what the edit removed: an edit
		 * inside a spilled stretch, then save and load */
		const int64_t cut = h.nearest(6);   /* a landing the bands kept inside the first stretch */
		assert(cut >= 0 && cut <= 6);
		assert(h.restore(cut, error));
		h.invalidateAfter(cut);
		assert(h.nearest(INT64_MAX) == cut);
		assert(h.saveTo(kPath, "fake", error));
		chimera::StateHistory back;
		back.configure(&api, nullptr, 64ull << 20);
		assert(back.loadFrom(kPath, "fake", error));
		assert(back.nearest(INT64_MAX) == cut);
		assert(back.restore(cut, error));
		assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(cut)].data(), Machine::kCells) == 0);
	}
	std::filesystem::remove_all("work-history-edit");

	{ // A chain that will not walk leaves the machine on a frame that DID exist,
	  // says which, and gives up the stretch - rather than leaving a machine
	  // that never existed for the session to record a movie against.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		chimera::StateHistory h;
		h.configure(&api, nullptr, 64ull << 20);
		h.bands(4, 8, 1, 1, 1000);   /* one long stretch, every landing kept */
		h.capture(0);
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		for (int64_t f = 1; f <= 30; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}
		/* the fourth delta of the walk refuses */
		g_refuseDeltaLoadIn = 3;
		int64_t landed = -1;
		assert(!h.restore(25, error, &landed));
		assert(landed == 0);                                   /* the anchor it walked from */
		assert(std::memcmp(g_machine.cell, truth[0].data(), Machine::kCells) == 0);
		assert(g_refuseDeltaLoadIn == -1);
		/* and the stretch is gone rather than waiting to fail again */
		assert(h.count() == 0);
		assert(h.nearest(25) == -1);
		g_refuseDeltaLoadIn = -1;
	}

	{ // Frame zero is always reachable, whatever the budgets do. Going back to a
	  // frame the greenzone no longer covers means starting from the beginning
	  // and replaying, and that is only possible if the beginning is still
	  // there; the encode path says so in as many words. The disk budget used to
	  // drop the oldest stretch in the file whichever it was, the first one
	  // included, and then nothing could reach the early movie at all.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-zero");
		std::filesystem::create_directories("work-history-zero");
		chimera::StateHistory h;
		h.configure(&api, nullptr, 256);            /* tiny: it must spill at once */
		h.bands(2, 6, 3, 12, 6);
		h.spillTo("work-history-zero");
		h.diskBudget(4096);                          /* and the file must overflow too */
		std::array<uint8_t, Machine::kCells> atZero{};
		std::memcpy(atZero.data(), g_machine.cell, Machine::kCells);
		h.capture(0);
		for (int64_t f = 1; f <= 400; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			assert(h.nearest(0) == 0);           /* at every step, not just the end */
		}
		assert(h.restore(0, error));
		assert(std::memcmp(g_machine.cell, atZero.data(), Machine::kCells) == 0);

		/* and the frames it does keep are spread over the run rather than all
		 * huddled at the end: going back to the middle must not mean replaying
		 * everything from zero */
		int64_t covered = 0;
		for (int64_t f = 0; f <= 400; f += 20)
		{
			if (h.nearest(f) >= 0 && f - h.nearest(f) <= 100) covered++;
		}
		assert(covered >= 12);
	}
	std::filesystem::remove_all("work-history-zero");

	{ // Spill files a dead session left behind are swept when a history is
	  // pointed at the directory: they are named for the process that made
	  // them, so nothing else would ever remove them, and they are gigabytes.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-stale");
		std::filesystem::create_directories("work-history-stale");
		{
			std::ofstream dead("work-history-stale/history-spill-999999-1.bin");
			dead << "what a session that died left";
		}
		{
			std::ofstream other("work-history-stale/keep-me.bin");
			other << "not a spill file";
		}
		chimera::StateHistory h;
		h.configure(&api, nullptr, 512);
		h.bands(2, 6, 3, 12, 8);
		h.spillTo("work-history-stale");
		assert(!std::filesystem::exists("work-history-stale/history-spill-999999-1.bin"));
		assert(std::filesystem::exists("work-history-stale/keep-me.bin"));

		/* and the one this history is using is not swept from under it */
		h.capture(0);
		for (int64_t f = 1; f <= 60; f++) { h.beforeAdvance(); advance(f); h.capture(f); }
		const auto mine = spillFileIn("work-history-stale");
		assert(!mine.empty());
		h.spillTo("work-history-stale");                 /* the same directory again */
		assert(std::filesystem::exists(mine));
		assert(h.nearest(0) == 0 && h.restore(0, error));
	}
	std::filesystem::remove_all("work-history-stale");

	{ // Turning the greenzone on again - a budget changed, a project reattached -
	  // starts from nothing, the spill file included. It used to keep the file
	  // open and its live count, so the disk budget was then held against
	  // stretches that no longer existed and the room was never given back.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-again");
		std::filesystem::create_directories("work-history-again");
		chimera::StateHistory h;
		h.configure(&api, nullptr, 512);
		h.bands(2, 6, 3, 12, 8);
		h.spillTo("work-history-again");
		h.capture(0);
		for (int64_t f = 1; f <= 60; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
		}
		h.flushWrites();
		assert(h.diskBytes() > 0);
		assert(!spillFileIn("work-history-again").empty());

		h.configure(&api, nullptr, 4096);
		assert(h.count() == 0);
		assert(h.bytes() == 0);
		assert(h.diskBytes() == 0);           /* nothing is out there any more */
		assert(spillFileIn("work-history-again").empty());
		/* and it works from cold: a fresh anchor, frames, and every one exact */
		g_machine = Machine{};
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		h.capture(0);
		for (int64_t f = 1; f <= 40; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
		}
		for (int64_t f = 0; f <= 40; f++)
		{
			if (h.nearest(f) != f) continue;
			assert(h.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
		}
	}
	std::filesystem::remove_all("work-history-again");

	{ // The history under random use, against the truth. Every other block asks
	  // one question of one path; this asks the only one that matters of all
	  // of them at once: after any sequence of frames, restores, edits, pins,
	  // budgets, spills and save/load round trips, does every frame the history
	  // still offers come back exactly as it was? Tiny budgets and small bands,
	  // so that every path runs every few operations. Deterministic per seed.
		const chimera::HostApi api = fakeHost();
		/* the history says so on stderr when its own arithmetic goes wrong;
		 * that is a failure here, not a note */
		std::fflush(stderr);
		/* the trace is somebody debugging: let it through rather than filing it */
		const bool captureErr = getenv("CHIMERA_HISTORY_TRACE") == nullptr;
		const int savedErr = captureErr ? dup(2) : -1;
		const int errFile = captureErr ? open("work-history-fuzz.err", O_WRONLY | O_CREAT | O_TRUNC, 0644) : -1;
		if (captureErr) { assert(errFile >= 0); dup2(errFile, 2); }
		for (int seed = 0; seed < 16; seed++)
		{
			uint64_t rng = 0x9E3779B97F4A7C15ull * static_cast<uint64_t>(seed + 1);
			auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return static_cast<uint32_t>(rng >> 11); };
			g_machine = Machine{};
			std::filesystem::remove_all("work-history-fuzz");
			std::filesystem::remove_all("work-history-fuzz-b");
			std::filesystem::create_directories("work-history-fuzz");
			std::filesystem::create_directories("work-history-fuzz-b");
			chimera::StateHistory h;
			const uint64_t budget = 200 + rnd() % 1500;
			const int64_t near = 1 + rnd() % 4, mid = 2 + rnd() % 8, midStride = 1 + rnd() % 3,
				farStride = 1 + rnd() % 16, spacing = seed % 2 ? -(2 + static_cast<int64_t>(rnd() % 60)) : 2 + static_cast<int64_t>(rnd() % 10);
			h.configure(&api, nullptr, budget);
			h.bands(near, mid, midStride, farStride, spacing);
			if (rnd() % 4 != 0) h.spillTo("work-history-fuzz");
			if (rnd() % 2) h.diskBudget(2048 + rnd() % 8192);

			std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
			std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
			h.capture(0);
			int64_t frame = 0;
			int op = 0;
			std::string story;   /* what happened, for the seed that fails */
			auto must = [&](bool ok, const char *what, int64_t f) {
				if (ok) return;
				std::fprintf(stderr, "seed %d op %d frame %lld: %s (frame %lld): %s\nstory:%s\n", seed, op, (long long)frame, what, (long long)f, error.c_str(), story.c_str());
				std::fflush(stderr);
				std::abort();
			};
			auto machineIs = [&](int64_t f) { return std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0; };
			auto putBack = [&]() { std::memcpy(g_machine.cell, truth[static_cast<size_t>(frame)].data(), Machine::kCells); g_machine.epochBase.clear(); };
			auto checkAll = [&](chimera::StateHistory &x) {
				/* nothing beyond the truth: a frame from a timeline an edit ended */
				must(x.nearest(INT64_MAX) < static_cast<int64_t>(truth.size()), "offers a frame past the run's end", x.nearest(INT64_MAX));
				for (int64_t f = 0; f < static_cast<int64_t>(truth.size()); f++)
				{
					if (x.nearest(f) != f) continue;
					must(x.restore(f, error), &x == &h ? "check: restore refused" : "check of the loaded copy: restore refused", f);
					must(machineIs(f), &x == &h ? "check: came back wrong" : "check of the loaded copy: came back wrong", f);
					size_t len = 0;
					const uint8_t *note = x.noteFor(f, len);
					if (f != 0) assert(note != nullptr && len == 2 && note[0] == static_cast<uint8_t>(f & 0xFF) && note[1] == 0x5A);
				}
				putBack();
			};

			for (op = 0; op < 500; op++)
			{
				switch (rnd() % 14)
				{
				case 0:
				case 1:
				{ // back to a frame it offers
					const int64_t f = h.nearest(static_cast<int64_t>(rnd() % truth.size()));
					if (f < 0) break;
					story += " restore" + std::to_string(f);
					must(h.restore(f, error), "restore refused", f);
					must(machineIs(f), "restored wrong", f);
					frame = f;
					break;
				}
				case 2:
				{ // an edit: back to a frame it offers, the input there changes,
				  // and every frame after it is a timeline that never happens
					const int64_t f = h.nearest(static_cast<int64_t>(rnd() % truth.size()));
					if (f < 0) break;
					story += " edit@" + std::to_string(f);
					must(h.restore(f, error), "restore for the edit refused", f);
					must(machineIs(f), "restored wrong before the edit", f);
					frame = f;
					h.invalidateAfter(f);
					truth.resize(static_cast<size_t>(f) + 1);
					/* the edit itself diverges the NEXT frame's state, the way a
					 * changed input does - so it happens inside the epoch, after
					 * beforeAdvance, and lands in the delta that frame records */
					truth.push_back({});
					frame++;
					h.beforeAdvance();
					advance(frame);
					g_machine.cell[rnd() % Machine::kCells] ^= static_cast<uint8_t>(1 + rnd() % 255);
					std::memcpy(truth[static_cast<size_t>(frame)].data(), g_machine.cell, Machine::kCells);
					const uint8_t note[2] = { static_cast<uint8_t>(frame & 0xFF), 0x5A };
					h.capture(frame, note, sizeof note);
					break;
				}
				case 3:
				{
					const int64_t f = static_cast<int64_t>(rnd() % truth.size());
					const bool on = rnd() % 2 == 0;
					story += (on ? " pin" : " unpin") + std::to_string(f);
					h.pin(f, on);
					break;
				}
				case 4:
				{ // saved and loaded back: what comes back must be right
					story += " save";
					assert(h.saveTo(kPath, "fake", error));
					chimera::StateHistory back;
					back.configure(&api, nullptr, budget);
					back.bands(near, mid, midStride, farStride, spacing);
					if (rnd() % 2) back.spillTo("work-history-fuzz-b");
					assert(back.loadFrom(kPath, "fake", error));
					checkAll(back);
					break;
				}
				case 5:
					story += " disk";
					h.diskBudget(rnd() % 2 ? 0 : 1024 + rnd() % 16384);
					break;
				case 6:
					if (rnd() % 8 == 0) { story += " spillto"; h.spillTo(rnd() % 2 ? "work-history-fuzz-b" : "work-history-fuzz"); }
					break;
				default:
				{ // a frame
					story += " f";
					h.beforeAdvance();
					frame++;
					advance(frame);
					if (frame < static_cast<int64_t>(truth.size()))
					{
						/* a frame the run already has: the same input replayed, which
						 * reproduces that timeline - an edit's change included, the
						 * way the movie's log would - and changes nothing */
						std::memcpy(g_machine.cell, truth[static_cast<size_t>(frame)].data(), Machine::kCells);
					}
					else
					{
						truth.resize(static_cast<size_t>(frame) + 1);
						std::memcpy(truth[static_cast<size_t>(frame)].data(), g_machine.cell, Machine::kCells);
					}
					const uint8_t note[2] = { static_cast<uint8_t>(frame & 0xFF), 0x5A };
					h.capture(frame, note, sizeof note);
					break;
				}
				}
				if (op % 40 == 39) checkAll(h);
			}
			checkAll(h);
		}
		std::fflush(stderr);
		if (captureErr)
		{
			dup2(savedErr, 2);
			close(savedErr);
			close(errFile);
		}
		if (captureErr)
		{
			std::ifstream err("work-history-fuzz.err");
			std::string line;
			while (std::getline(err, line))
			{
				if (line.find("count was wrong") != std::string::npos || line.find("gave back") != std::string::npos)
				{
					std::fprintf(stderr, "the history's accounting complained: %s\n", line.c_str());
					assert(false);
				}
			}
		}
		std::filesystem::remove("work-history-fuzz.err");
		std::filesystem::remove_all("work-history-fuzz");
		std::filesystem::remove_all("work-history-fuzz-b");
	}


	{ // The same run, threaded and in line, compared step by step.
	  //
	  // The fuzz above asks whether the history is RIGHT. This asks the
	  // question a helper thread raises: is it the SAME? The claim the design
	  // makes is not "close enough" but identical - a spill settles all of its
	  // metadata the moment it is queued, so the budget's arithmetic and every
	  // decision that follows from it are what they always were, and the only
	  // thing that happens later is the write itself. Where that claim breaks,
	  // the two signatures diverge at the operation that broke it.
	  //
	  // The signature is what a caller can see: how many frames are offered,
	  // what is held in memory, what is on disk, and which frames answer.
		const chimera::HostApi api = fakeHost();
		auto signatureOf = [](chimera::StateHistory &x, int64_t upTo) {
			/* Not the disk bytes. The disk budget counts what the file really
			 * holds (user-decided, 2026-09-13), which is known when the writer
			 * reports - and a report lands whenever the writer gets there, so
			 * the disk count moves at different moments threaded and in line.
			 * What is compared step by step is what the history DECIDED; what
			 * the disk ends up holding is compared once, after a flush. */
			std::string sig = std::to_string(x.count()) + "/" + std::to_string(x.bytes()) + ":";
			for (int64_t f = 0; f <= upTo; f++)
			{
				if (x.nearest(f) == f) sig += std::to_string(f) + ",";
			}
			return sig;
		};

		int spilledIn[2] = { 0, 0 }, queuedWrites = 0;
		for (int seed = 0; seed < 8; seed++)
		{
			std::string signature[2];
			for (int pass = 0; pass < 2; pass++)
			{
				const bool threaded = pass == 0;
				const char *dir = threaded ? "work-history-diff-t" : "work-history-diff-s";
				std::filesystem::remove_all(dir);
				std::filesystem::create_directories(dir);

				uint64_t rng = 0x27BB2EE687B0B0FDull * static_cast<uint64_t>(seed + 1);
				auto rnd = [&]() { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return static_cast<uint32_t>(rng >> 11); };
				g_machine = Machine{};

				chimera::StateHistory h;
				const uint64_t budget = 200 + (rnd() % 500);
				const int64_t near = 1 + rnd() % 4, mid = 2 + rnd() % 8, midStride = 1 + rnd() % 3,
					farStride = 1 + rnd() % 16, spacing = seed % 2 ? -(2 + static_cast<int64_t>(rnd() % 60)) : 2 + static_cast<int64_t>(rnd() % 10);
				h.configure(&api, nullptr, budget);
				h.helpers(threaded);
				h.bands(near, mid, midStride, farStride, spacing);
				h.spillTo(dir);
				/* No disk budget here, on purpose: dropping from disk follows the
				 * writer's reports now, so the two modes would drop at different
				 * moments and hold different frames - correctly, and not the same.
				 * The disk budget's correctness is the fuzz above's to prove. The
				 * random draw stays, so the rest of the sequence is unchanged. */
				(void)(2048 + rnd() % 8192);

				int64_t frame = 0, newest = 0;
				bool sawDisk = false, sawWrites = false;
				h.capture(0);
				std::string sig;
				/* Where somebody works: near the playhead, not uniformly over the
				 * run. A uniform edit point walks the run back towards zero
				 * faster than frames push it forward, and then nothing is ever
				 * big enough to spill - which would compare two histories that
				 * never touched the writer at all. */
				auto somewhereRecent = [&]() {
					const int64_t back = static_cast<int64_t>(rnd() % 12);
					return h.nearest(newest > back ? newest - back : 0);
				};
				for (int op = 0; op < 400; op++)
				{
					switch (rnd() % 24)
					{
					case 0:
					case 1:
					case 2:
					{ // a seek back, which is where a pending write is met
						const int64_t f = somewhereRecent();
						if (f < 0) break;
						assert(h.restore(f, error));
						frame = f;
						break;
					}
					case 3:
					{ // an edit: everything after this frame stops being true
						const int64_t f = somewhereRecent();
						if (f < 0) break;
						assert(h.restore(f, error));
						frame = f;
						h.invalidateAfter(f);
						newest = f;
						break;
					}
					case 4:
						h.pin(static_cast<int64_t>(rnd() % static_cast<uint32_t>(newest + 1)), rnd() % 2 == 0);
						break;
					case 5:
						(void)(rnd() % 2 ? 0 : 1024 + rnd() % 16384);   /* see above */
						break;
					default:
					{ // a frame
						h.beforeAdvance();
						frame++;
						advance(frame);
						if (frame > newest) newest = frame;
						const uint8_t note[2] = { static_cast<uint8_t>(frame & 0xFF), 0x5A };
						h.capture(frame, note, sizeof note);
						break;
					}
					}
					sig += signatureOf(h, newest) + "|";
					if (h.diskBytes() != 0 || h.writesInFlight() != 0) sawDisk = true;
					/* that the writer wrote at all. Catching a write still IN FLIGHT
					 * used to be the sign, and a capture now applies what the writer
					 * reported before it returns - so a quick writer is never caught
					 * mid-write, and with helpers off it never could be */
					if (h.costs().spilledRaw != 0) sawWrites = true;
				}
				/* a run that never spilled would compare two histories that
				 * never used the writer, and prove nothing about it. Asked after
				 * the writer has caught up: a pass whose only spills came in its
				 * last few operations has written them and not yet said so, and
				 * that pass used the writer as much as any other. The same flush
				 * happens in both passes, and moves no frame. */
				h.flushWrites();
				if (h.costs().spilledRaw != 0) sawWrites = true;
				if (h.diskBytes() != 0) sawDisk = true;
				if (sawDisk) spilledIn[pass]++;
				if (threaded && sawWrites) queuedWrites++;
				/* and what a save sees, which is the other reader of the file */
				assert(h.saveTo(kPath, "fake", error));
				sig += signatureOf(h, newest);
				/* and what the disk holds, once everything has landed: the same
				 * stretches compress to the same bytes either way */
				h.flushWrites();
				sig += " disk " + std::to_string(h.diskBytes());
				signature[pass] = std::move(sig);

				std::filesystem::remove_all(dir);
			}
			if (signature[0] != signature[1])
			{
				/* say WHERE, not just that: the first difference is the decision
				 * the threading changed, and its neighbourhood names the op */
				size_t at = 0;
				while (at < signature[0].size() && at < signature[1].size()
					&& signature[0][at] == signature[1][at]) at++;
				const size_t from = at > 120 ? at - 120 : 0;
				std::fprintf(stderr, "seed %d: threaded and in line diverged at %zu\n"
					"  threaded: ...%s\n  in line:  ...%s\n",
					seed, at, signature[0].substr(from, 240).c_str(),
					signature[1].substr(from, 240).c_str());
				std::fflush(stderr);
				assert(false);
			}
		}
		/* A run that never spilled would be comparing two histories that never
		 * used the writer, which would prove nothing about it. Not every random
		 * budget forces one, so the bar is most of them - and the threaded pass
		 * must have had writes actually in flight, or the comparison is between
		 * two synchronous paths. */
		assert(spilledIn[0] >= 6 && spilledIn[1] >= 6);
		assert(spilledIn[0] == spilledIn[1]);
		assert(queuedWrites >= 6);
		std::filesystem::remove_all("work-history-diff-t");
		std::filesystem::remove_all("work-history-diff-s");
	}

	{ // Every offset in the spill file is one the reservation chose.
	  //
	  // A spill reserves its range before its bytes are written, so the length
	  // reserved and the length written have to agree exactly: one byte short
	  // and the next stretch lands on this one's tail, which nothing notices
	  // until a restore comes back as somebody else's frames. So: a budget too
	  // small to hold the run, everything driven onto disk, and then every
	  // frame the history still offers read back off it and compared.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-reserve");
		std::filesystem::create_directories("work-history-reserve");
		chimera::StateHistory h;
		h.configure(&api, nullptr, 512);      /* smaller than the run: it must spill */
		h.bands(2, 4, 1, 2, 6);
		h.spillTo("work-history-reserve");

		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
		h.capture(0);
		for (int64_t f = 1; f <= 60; f++)
		{
			h.beforeAdvance();
			advance(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
			h.capture(f);
		}
		h.flushWrites();
		assert(h.diskBytes() > 0);   /* if nothing spilled this proves nothing */
		int read = 0;
		for (int64_t f = 0; f <= 60; f++)
		{
			if (h.nearest(f) != f) continue;
			assert(h.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			read++;
		}
		assert(read > 4);
		std::filesystem::remove_all("work-history-reserve");
	}


	{ // A save on the writer writes the same file as a save in line.
	  //
	  // This is the one with seconds on it: the budgets a project is saved
	  // against are four gigabytes in memory and ten on disk, so a save can be
	  // fourteen gigabytes on the thread that runs the machine - and TAStudio
	  // fires one every thirty minutes without being asked. Moving it is only
	  // allowed if the bytes are the same bytes, so that is what is compared,
	  // including the part that has to be read back out of the spill file.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-save");
		std::filesystem::create_directories("work-history-save");

		chimera::StateHistory h;
		h.configure(&api, nullptr, 700);      /* small enough that stretches go to disk */
		h.bands(2, 6, 1, 3, 5);
		h.spillTo("work-history-save");

		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
		h.capture(0);
		for (int64_t f = 1; f <= 90; f++)
		{
			h.beforeAdvance();
			advance(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
			const uint8_t note[2] = { static_cast<uint8_t>(f & 0xFF), 0x5A };
			h.capture(f, note, sizeof note);
		}
		h.flushWrites();
		assert(h.diskBytes() > 0);   /* the spilled half is the interesting half */

		/* in line first, for the answer to compare against */
		assert(h.saveTo("work-history-save/in-line.bin", "fake", error));

		/* then queued, and it must not have finished by the time we are back */
		assert(h.saveToLater("work-history-save/queued.bin", "fake", error));
		const bool sawPending = h.savePending();
		assert(h.saveWait(error));
		assert(!h.savePending());

		auto slurp = [](const char *path) {
			std::ifstream in(path, std::ios::binary);
			return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		};
		const std::string inLine = slurp("work-history-save/in-line.bin");
		const std::string queued = slurp("work-history-save/queued.bin");
		assert(!inLine.empty());
		assert(inLine == queued);

		/* and what comes back out of it is the run, frame for frame */
		chimera::StateHistory back;
		back.configure(&api, nullptr, 1u << 20);
		assert(back.loadFrom("work-history-save/queued.bin", "fake", error));
		int checked = 0;
		for (int64_t f = 0; f <= 90; f++)
		{
			if (back.nearest(f) != f) continue;
			assert(back.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			checked++;
		}
		assert(checked > 8);

		/* The run carries on while a save is in flight, and what the file says
		 * is the history as it was ASKED about - not as it ended up. Frames
		 * captured after the request are somebody else's business. */
		assert(h.saveToLater("work-history-save/during.bin", "fake", error));
		for (int64_t f = 91; f <= 140; f++)
		{
			h.beforeAdvance();
			advance(f);
			h.capture(f);
		}
		assert(h.saveWait(error));
		chimera::StateHistory during;
		during.configure(&api, nullptr, 1u << 20);
		assert(during.loadFrom("work-history-save/during.bin", "fake", error));
		assert(during.nearest(INT64_MAX) <= 90);

		(void)sawPending;   /* a fast enough writer may have finished already */
		std::filesystem::remove_all("work-history-save");
	}

	{ // A save asked for while a spill is still being written is a whole save.
	  //
	  // A stretch handed to the writer does not know where its bytes will land
	  // until the write reports back, and the save's snapshot is taken the moment
	  // it is asked for. On a machine that spills every second or two - Ruffle,
	  // hundreds of megabytes a stretch - that is most saves.
		const chimera::HostApi api = fakeHost();
		int attempts = 0, failed = 0, checked = 0, wrong = 0;
		for (int round = 0; round < 20; round++)
		{
			g_machine = Machine{};
			std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
			std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
			std::filesystem::remove_all("work-history-inflight");
			std::filesystem::create_directories("work-history-inflight");
			chimera::StateHistory h;
			h.helpers(true);
			h.configure(&api, nullptr, 700);
			h.bands(2, 6, 1, 3, 5);
			h.spillTo("work-history-inflight");
			h.capture(0);
			for (int64_t f = 1; f <= 90; f++)
			{
				h.beforeAdvance();
				advance(f);
				std::array<uint8_t, Machine::kCells> at{};
				std::memcpy(at.data(), g_machine.cell, Machine::kCells);
				truth.push_back(at);
				h.capture(f);
				if (f < 40 || h.writesInFlight() == 0) continue;
				/* a spill this thread has not heard back about: ask for the save now */
				attempts++;
				assert(h.saveToLater("work-history-inflight/h.bin", "fake", error));
				std::string why;
				if (!h.saveWait(why))
				{
					failed++;
					if (failed == 1) std::fprintf(stderr, "in-flight save failed: %s\n", why.c_str());
					break;
				}
				/* and what it wrote is the run, frame for frame */
				chimera::StateHistory back;
				back.configure(&api, nullptr, 1u << 20);
				if (!back.loadFrom("work-history-inflight/h.bin", "fake", why)) { wrong++; break; }
				for (int64_t k = 0; k <= f; k++)
				{
					if (back.nearest(k) != k) continue;
					checked++;
					if (!back.restore(k, why)
						|| std::memcmp(g_machine.cell, truth[static_cast<size_t>(k)].data(), Machine::kCells) != 0)
					{
						if (wrong == 0) std::fprintf(stderr, "in-flight save: frame %lld came back wrong\n", (long long)k);
						wrong++;
					}
				}
				break;
			}
		}
		std::fprintf(stderr, "saves asked for mid-spill: %d, failed: %d, frames checked: %d, wrong: %d\n",
			attempts, failed, checked, wrong);
		assert(attempts > 0);
		assert(failed == 0);
		assert(checked > 0);
		assert(wrong == 0);
		std::filesystem::remove_all("work-history-inflight");
	}

	{ // Nothing is spilled behind a save that is being written.
	  //
	  // The save is one job on the writer, as big as the history. A spill queued
	  // behind it lands when the save does, and the spill after that - finding
	  // the queue over its cap - waited for both on the thread that runs the
	  // machine: a 42 second frame on nss102. While a save is pending the budget
	  // is kept by thinning instead, and that is neither a failure nor a wait.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		std::filesystem::remove_all("work-history-saving");
		std::filesystem::create_directories("work-history-saving");
		chimera::StateHistory h;
		h.helpers(true);
		h.configure(&api, nullptr, 700);
		h.bands(2, 6, 1, 3, 5);
		h.spillTo("work-history-saving");
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
		h.capture(0);
		int64_t f = 1;
		auto play = [&](int64_t until) {
			for (; f <= until; f++)
			{
				h.beforeAdvance();
				advance(f);
				std::array<uint8_t, Machine::kCells> at{};
				std::memcpy(at.data(), g_machine.cell, Machine::kCells);
				truth.push_back(at);
				h.capture(f);
			}
		};
		play(60);
		h.flushWrites();
		assert(h.diskBytes() > 0);

		int watched = 0;
		for (int round = 0; round < 10; round++)
		{
			assert(h.saveToLater("work-history-saving/h.bin", "fake", error));
			for (int k = 0; k < 20; k++)
			{
				const bool before = h.savePending();
				const uint64_t queued = h.writesInFlight();
				play(f);
				/* pending on both sides means pending throughout: it only ever clears */
				if (before && h.savePending())
				{
					watched++;
					assert(h.writesInFlight() <= queued);
				}
			}
			assert(h.saveWait(error));
		}
		assert(!h.spillFailed());
		std::fprintf(stderr, "captures watched while a save was pending: %d\n", watched);

		/* and the thinned history is still the run wherever it answers */
		int restored = 0;
		for (int64_t k = 0; k < f; k++)
		{
			if (h.nearest(k) != k) continue;
			assert(h.restore(k, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(k)].data(), Machine::kCells) == 0);
			restored++;
		}
		assert(restored > 8);
		std::filesystem::remove_all("work-history-saving");
	}

	{ // Going back and playing forward over the same input keeps what is ahead.
	  //
	  // A capture used to drop every stored frame after the one it stored, so a
	  // seek back followed by play threw the greenzone ahead away, and the way
	  // back to the end was emulated frame by frame - 3000 frames in 99 s on
	  // nss102. A replay changes nothing (user-decided, 2026-09-13); what
	  // changes the timeline calls invalidateAfter itself.
		const chimera::HostApi api = fakeHost();
		for (int pass = 0; pass < 2; pass++)
		{
			const bool threaded = pass == 0;
			g_machine = Machine{};
			const char *dir = threaded ? "work-history-replay-t" : "work-history-replay-s";
			std::filesystem::remove_all(dir);
			std::filesystem::create_directories(dir);
			chimera::StateHistory h;
			h.helpers(threaded);
			h.configure(&api, nullptr, 900);   /* small enough that the far end spills */
			h.bands(4, 8, 2, 4, 6);
			h.spillTo(dir);
			std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
			std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
			h.capture(0);
			for (int64_t f = 1; f <= 120; f++)
			{
				h.beforeAdvance();
				advance(f);
				std::array<uint8_t, Machine::kCells> at{};
				std::memcpy(at.data(), g_machine.cell, Machine::kCells);
				truth.push_back(at);
				h.capture(f);
			}
			h.flushWrites();
			const int64_t end = h.nearest(INT64_MAX);
			assert(end > 100);
			std::vector<int64_t> held;
			for (int64_t f = 0; f <= 120; f++)
			{
				if (h.nearest(f) == f) held.push_back(f);
			}

			/* back, from several places, and the same input played forward */
			for (int64_t back : { end - 3, int64_t(90), int64_t(40) })
			{
				const int64_t from = h.nearest(back);
				assert(from >= 0);
				assert(h.restore(from, error));
				for (int64_t f = from + 1; f <= back + 10 && f <= 120; f++)
				{
					h.beforeAdvance();
					advance(f);
					assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
					h.capture(f);
				}
				assert(h.nearest(INT64_MAX) == end);
			}
			h.flushWrites();
			for (int64_t f : held)
			{
				assert(h.nearest(f) == f);
				assert(h.restore(f, error));
				assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			}

			/* and playing on from the end after all that still extends it exactly */
			assert(h.restore(end, error));
			for (int64_t f = end + 1; f <= 120 + 30; f++)
			{
				h.beforeAdvance();
				advance(f);
				std::array<uint8_t, Machine::kCells> at{};
				std::memcpy(at.data(), g_machine.cell, Machine::kCells);
				if (f >= static_cast<int64_t>(truth.size())) truth.push_back(at);
				h.capture(f);
			}
			int checkedAhead = 0;
			for (int64_t f = end + 1; f <= 150; f++)
			{
				if (h.nearest(f) != f) continue;
				assert(h.restore(f, error));
				assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
				checkedAhead++;
			}
			assert(checkedAhead > 0);

			/* an edit, which says so, still drops what is ahead of it */
			assert(h.restore(h.nearest(60), error));
			h.invalidateAfter(60);
			assert(h.nearest(INT64_MAX) <= 60);
			std::filesystem::remove_all(dir);
		}
	}

	{ // A saved history is compressed, and it comes back exactly as it went.
	  //
	  // A project's history is a machine's worth of mostly unwritten memory per
	  // anchor, so it is saved as one zstd stream behind a magic of its own
	  // (ChimeraHistory4). The raw layout one version back is still read - the
	  // hand-built files above are all ChimeraHistory3 - and this is the half
	  // that proves the new one is a history rather than just a smaller file.
		const chimera::HostApi api = fakeHost();
		g_machine = Machine{};
		chimera::StateHistory h;
		h.configure(&api, nullptr, 1u << 20);
		h.bands(2, 6, 1, 3, 8);
		std::vector<std::array<uint8_t, Machine::kCells>> truth(1);
		std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
		h.capture(0);
		for (int64_t f = 1; f <= 60; f++)
		{
			h.beforeAdvance();
			advance(f);
			std::array<uint8_t, Machine::kCells> at{};
			std::memcpy(at.data(), g_machine.cell, Machine::kCells);
			truth.push_back(at);
			const uint8_t note[2] = { static_cast<uint8_t>(f & 0xFF), 0x3C };
			h.capture(f, note, sizeof note);
		}
		assert(h.saveTo("work-history-v4.bin", "fake", error));
		{
			std::ifstream in("work-history-v4.bin", std::ios::binary);
			char magic[15] = {};
			in.read(magic, sizeof magic);
			const std::string m(magic, sizeof magic);
			const bool rawAsked = getenv("CHIMERA_HISTORY_RAW") != nullptr && getenv("CHIMERA_HISTORY_RAW")[0] == '1';
			assert(m == (rawAsked ? "ChimeraHistory3" : "ChimeraHistory4"));
		}
		chimera::StateHistory back;
		back.configure(&api, nullptr, 1u << 20);
		assert(back.loadFrom("work-history-v4.bin", "fake", error));
		int checked = 0;
		for (int64_t f = 0; f <= 60; f++)
		{
			if (h.nearest(f) != f) continue;
			assert(back.nearest(f) == f);
			assert(back.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
			size_t len = 0;
			const uint8_t *note = back.noteFor(f, len);
			if (f != 0) assert(note != nullptr && len == 2 && note[1] == 0x3C);
			checked++;
		}
		assert(checked > 8);

		/* and a compressed file cut short is damage, refused, not a crash */
		{
			std::ifstream in("work-history-v4.bin", std::ios::binary);
			std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			all.resize(all.size() / 2);
			std::ofstream out("work-history-v4-cut.bin", std::ios::binary);
			out.write(all.data(), static_cast<std::streamsize>(all.size()));
		}
		chimera::StateHistory cut;
		cut.configure(&api, nullptr, 1u << 20);
		assert(!cut.loadFrom("work-history-v4-cut.bin", "fake", error));
		assert(cut.count() == 0);
		std::remove("work-history-v4.bin");
		std::remove("work-history-v4-cut.bin");
	}

	{ // The anchor spacing, chosen by weight.
	  //
	  // A positive spacing is exact: one stretch for as long as it allows. A
	  // negative one is a ceiling, and a stretch closes sooner once its links
	  // weigh as much as its anchor AND as much as the walk floor - never before
	  // thirty frames, and never later than the ceiling. This machine's anchor is
	  // 64 bytes and a frame's delta 10, so weight alone would close a stretch
	  // every seven frames; the frame floor is what holds it to thirty. With the
	  // walk floor left at its 64 MB, a machine this light never closes one early
	  // at all, which is the point of it. And whichever way the stretches were
	  // cut, every frame they offer comes back exact.
		const chimera::HostApi api = fakeHost();
		auto run = [&](int64_t spacing, chimera::StateHistory &h,
			std::vector<std::array<uint8_t, Machine::kCells>> &truth, uint64_t walkFloor = 1) {
			g_machine = Machine{};
			h.configure(&api, nullptr, 1u << 20);
			h.anchorWalkFloor(walkFloor);
			h.bands(1000, 1000, 1, 1, spacing);   /* every frame kept, nothing coarsened */
			truth.assign(1, {});
			std::memcpy(truth[0].data(), g_machine.cell, Machine::kCells);
			h.capture(0);
			for (int64_t f = 1; f <= 200; f++)
			{
				h.beforeAdvance();
				advance(f);
				std::array<uint8_t, Machine::kCells> at{};
				std::memcpy(at.data(), g_machine.cell, Machine::kCells);
				truth.push_back(at);
				h.capture(f);
			}
		};
		std::vector<std::array<uint8_t, Machine::kCells>> truth;

		chimera::StateHistory fixed;
		run(1000, fixed, truth);
		assert(fixed.anchors() == 1);

		chimera::StateHistory capped;
		run(50, capped, truth);
		assert(capped.anchors() == 4);   /* 0, 51, 102, 153 */

		chimera::StateHistory weighed;
		run(-1000, weighed, truth);
		/* 200 frames at a stretch of 31 (the floor, then the frame that closes it) */
		assert(weighed.anchors() >= 6 && weighed.anchors() <= 7);
		for (int64_t f = 0; f <= 200; f++)
		{
			assert(weighed.nearest(f) == f);
			assert(weighed.restore(f, error));
			assert(std::memcmp(g_machine.cell, truth[static_cast<size_t>(f)].data(), Machine::kCells) == 0);
		}

		/* a ceiling below the floor is still the ceiling */
		chimera::StateHistory tight;
		run(-10, tight, truth);
		assert(tight.anchors() >= 18 && tight.anchors() <= 19);

		/* a light machine under the real walk floor keeps its whole ceiling */
		chimera::StateHistory light;
		run(-1000, light, truth, 64ull << 20);
		assert(light.anchors() == 1);

		/* and the default is weighed */
		chimera::StateHistory byDefault;
		g_machine = Machine{};
		byDefault.configure(&api, nullptr, 1u << 20);
		byDefault.anchorWalkFloor(1);
		byDefault.bands(1000, 1000, 1, 1, 0);
		byDefault.capture(0);
		for (int64_t f = 1; f <= 200; f++)
		{
			byDefault.beforeAdvance();
			advance(f);
			byDefault.capture(f);
		}
		assert(byDefault.anchors() == weighed.anchors());
	}

	/* the spill file belongs to the history and goes with it */
	assert(spillFileIn("work-history-spill").empty());
	std::filesystem::remove_all("work-history-spill");

	std::remove(kPath);
	std::printf("test_state_history: ok\n");
	return 0;
}
