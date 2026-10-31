#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/vm/cores_vm.hpp"
#include "chimera/vm/sha256.hpp"

#include "chimera/vm/branch_state.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <future>
#include <random>
#include <iterator>
#include <optional>
#include <utility>

#include "chimera/engine.h"
#include "chimera/vm/firmware.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/compiled_code.hpp"
#include "chimera/vm/crash_notes.hpp"
#include "chimera/vm/process_stamp.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/suggestion.hpp"

namespace chimera::vm {

namespace {
/* Two paths to one project file: the same file when both are there (as the
 * system matches names and links), else the same whole path. */
bool samePlace(const std::string &a, const std::string &b)
{
	namespace fs = std::filesystem;
	std::error_code both, one, other;
	const bool same = fs::equivalent(fs::path(a), fs::path(b), both);
	if (!both) return same;
	const fs::path x = fs::weakly_canonical(fs::path(a), one), y = fs::weakly_canonical(fs::path(b), other);
	return !one && !other ? x == y : a == b;
}

/* A game's name made fit for a file's, as Chimera makes it: '/' and '|'
 * become '+', ':' becomes " -", '"' and what no file name may hold are left
 * out, and so are dots at its end (the extension follows them) */
std::string projectFileName(const std::string &name)
{
	std::string out;
	for (const char c : name)
	{
		if (c == '/' || c == '|') out += '+';
		else if (c == ':') out += " -";
		else if (c == '"' || c == '<' || c == '>' || c == '\\' || c == '?' || c == '*' || static_cast<unsigned char>(c) < 32) continue;
		else out += c;
	}
	while (!out.empty() && out.back() == '.') out.pop_back();
	return out;
}

/* A zip of the files given, each stored as it is (the savedata export's,
 * for more than one file; a zip any unzipper opens) */
uint32_t crc32Of(const std::vector<uint8_t> &bytes)
{
	static uint32_t table[256];
	static bool made = false;
	if (!made)
	{
		for (uint32_t i = 0; i < 256; i++)
		{
			uint32_t c = i;
			for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
		made = true;
	}
	uint32_t c = 0xFFFFFFFFu;
	for (uint8_t b : bytes) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

/* Chimera asks for the GPU bridge when the core's renderer setting, as given
 * or the package's default, is a hardware one ("opengl-hw", "vulkan-hw"); the
 * request is for every session opened after it, so it is made every time */
void requestGpu(const std::string &package, const std::string &settings)
{
	std::string renderer;
	json::Json given;
	std::string error;
	if (json::parse(settings.empty() ? "{}" : settings, given, error))
		if (const json::Json *v = given.find("renderer"); v != nullptr && v->kind == json::Json::Kind::String) renderer = v->text;
	if (renderer.empty())
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package.c_str(), &perr))
		{
			uint64_t len = 0;
			json::Json cfg;
			if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len))
				json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, error);
			ce_package_free(pk);
			if (const json::Json *list = cfg.find("settings"))
				for (const json::Json &item : list->items)
					if (const json::Json *name = item.find("name"); name != nullptr && name->text == "renderer")
						if (const json::Json *d = item.find("default"); d != nullptr && d->kind == json::Json::Kind::String) renderer = d->text;
		}
	}
	ce_gl_request(renderer.size() >= 3 && renderer.compare(renderer.size() - 3, 3, "-hw") == 0 ? 1 : 0);
}

} // namespace

std::vector<uint8_t> storedZip(const std::vector<std::pair<std::string, std::vector<uint8_t>>> &files)
{
	std::vector<uint8_t> out, central;
	auto u16 = [](std::vector<uint8_t> &v, uint32_t x) { v.push_back(x & 0xFF), v.push_back((x >> 8) & 0xFF); };
	auto u32 = [&](std::vector<uint8_t> &v, uint32_t x) { u16(v, x & 0xFFFF), u16(v, x >> 16); };
	for (const auto &[name, bytes] : files)
	{
		const uint32_t crc = crc32Of(bytes), size = static_cast<uint32_t>(bytes.size()), at = static_cast<uint32_t>(out.size());
		u32(out, 0x04034b50), u16(out, 20), u16(out, 0x0800), u16(out, 0), u16(out, 0), u16(out, 0x21);
		u32(out, crc), u32(out, size), u32(out, size), u16(out, static_cast<uint32_t>(name.size())), u16(out, 0);
		out.insert(out.end(), name.begin(), name.end());
		out.insert(out.end(), bytes.begin(), bytes.end());
		u32(central, 0x02014b50), u16(central, 20), u16(central, 20), u16(central, 0x0800), u16(central, 0), u16(central, 0);
		u16(central, 0x21), u32(central, crc), u32(central, size), u32(central, size), u16(central, static_cast<uint32_t>(name.size()));
		u16(central, 0), u16(central, 0), u16(central, 0), u16(central, 0), u32(central, 0), u32(central, at);
		central.insert(central.end(), name.begin(), name.end());
	}
	const uint32_t centralAt = static_cast<uint32_t>(out.size()), centralSize = static_cast<uint32_t>(central.size());
	out.insert(out.end(), central.begin(), central.end());
	u32(out, 0x06054b50), u16(out, 0), u16(out, 0), u16(out, static_cast<uint32_t>(files.size()));
	u16(out, static_cast<uint32_t>(files.size())), u32(out, centralSize), u32(out, centralAt), u16(out, 0);
	return out;
}

SessionViewModel::SessionViewModel(Dispatcher &dispatcher, InputLog &inputs)
	: ViewModel("session", dispatcher), inputs_(inputs)
{
	expose("isOpen", "whether a machine is open", isOpen);
	expose("opens", "how many times a machine has been opened", opens);
	expose("openFailures", "how many opens failed, a file that is no project among them; the status says why", openFailures);
	expose("unplayableMovies", "how many times the movie could not be played; the status says why", unplayableMovies);
	expose("projectId", "the open project's id (its cache folder's name), \"\" for none", projectId);
	expose("resolvingFiles", "a project is waiting for its files to be found (the projectFiles rows)", resolvingFiles);
	rows("projectFiles", "the waiting project's files: name, slot, status (0 found, 1 not found, 2 not the recorded bytes), "
		"statusText, sha1 (recorded), actualSha1", projectFileRows_);
	command("locateProjectFile", "points a waiting project's file at a file on disk; its hash decides", {"index", "path"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t index = 0;
			std::string path;
			if (!arg(a, 0, index, error) || !arg(a, 1, path, error)) return {};
			std::string why;
			onMachineWait([&](ce_session *) {
				ce_project *p = pendingOpenM_.project;
				const int64_t fileCount = p != nullptr ? ce_project_file_count(p) : 0;
				/* the firmware, listed after the files (issue 397) */
				if (p != nullptr && index >= fileCount && index - fileCount < static_cast<int64_t>(missingFirmwareM_.size()))
				{
					why = locateFirmwareOnMachine(missingFirmwareM_[static_cast<size_t>(index - fileCount)], path);
					publishProjectFilesOnMachine();
					return;
				}
				if (p == nullptr || index < 0 || index >= fileCount)
				{
					why = tr("session.no_such_file");
					return;
				}
				const char *err = nullptr;
				if (ce_project_file_resolve(p, static_cast<int32_t>(index), path.c_str(), &err) != 0)
					why = tr("session.cannot_read_file", {err != nullptr ? err : tr("common.no_reason")});
				publishProjectFilesOnMachine();
			});
			if (!why.empty())
			{
				error = why;
				return {};
			}
			return {};
		}, &resolvingFiles);
	command("scanForProjectFiles", "looks through a folder (and its subfolders when asked) for the waiting project's files: "
		"by their hashes, any name, then by their names; and for its missing firmware, as the firmware folder is looked "
		"through; says how many it found", {"folder", "subfolders"},
		[this](const Args &a, std::string &error) -> Value {
			std::string folder;
			bool recurse = true;
			if (!arg(a, 0, folder, error)) return {};
			if (a.size() > 1 && !arg(a, 1, recurse, error)) return {};
			int64_t found = 0;
			onMachineWait([&](ce_session *) {
				namespace fs = std::filesystem;
				ce_project *p = pendingOpenM_.project;
				if (p == nullptr) return;
				std::vector<int32_t> wanted;
				for (int32_t i = 0; i < ce_project_file_count(p); i++)
					if (ce_project_file_status(p, i) != 0) wanted.push_back(i);
				std::error_code ec;
				if (!fs::is_directory(folder, ec)) return;
				/* the firmware, as the firmware folder is looked through: by the
				 * hash it must have, else by the name and size the package gives */
				if (std::any_of(missingFirmwareM_.begin(), missingFirmwareM_.end(), [](const MissingFirmware &m) { return m.path.empty(); }))
				{
					const std::vector<FirmwareFile> there = indexFirmware({folder}, recurse, 10000);
					for (MissingFirmware &m : missingFirmwareM_)
					{
						std::string how;
						if (!m.path.empty()) continue;
						if (const FirmwareFile *f = findFirmware(m.decl, m.pinned, there, how); f != nullptr && locateFirmwareOnMachine(m, f->path).empty()) found++;
					}
					publishProjectFilesOnMachine();
				}
				if (wanted.empty()) return;
				/* the files there, the smallest first (as Chimera's scan), ten thousand at most */
				std::vector<std::pair<uint64_t, std::string>> candidates;
				auto add = [&](const fs::directory_entry &e) {
					if (candidates.size() < 10000 && e.is_regular_file(ec)) candidates.emplace_back(e.file_size(ec), e.path().string());
				};
				if (recurse)
					for (fs::recursive_directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) add(*it);
				else
					for (fs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec)) add(*it);
				std::stable_sort(candidates.begin(), candidates.end());
				/* by hash: each file hashed once, claiming every entry it is */
				for (const auto &[size, path] : candidates)
				{
					bool left = false;
					for (int32_t i : wanted) left = left || ce_project_file_status(p, i) != 0;
					if (!left) break;
					char sha[41] = {};
					uint64_t length = 0;
					if (ce_sha1_file(path.c_str(), sha, &length) != 1) continue;
					for (int32_t i : wanted)
						if (ce_project_file_status(p, i) != 0 && ce_project_file_sha1(p, i) != nullptr &&
							std::string(ce_project_file_sha1(p, i)) == sha)
						{
							const char *err = nullptr;
							if (ce_project_file_resolve(p, i, path.c_str(), &err) == 0) found++;
						}
				}
				/* by name, for what no hash claimed: the engine's verdict shows in the row */
				for (int32_t i : wanted)
				{
					if (ce_project_file_status(p, i) == 0) continue;
					const std::string name = fs::path(ce_project_file_name(p, i) != nullptr ? ce_project_file_name(p, i) : "").filename().string();
					auto lower = [](std::string t) {
						for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
						return t;
					};
					for (const auto &[size, path] : candidates)
						if (lower(fs::path(path).filename().string()) == lower(name))
						{
							const char *err = nullptr;
							if (ce_project_file_resolve(p, i, path.c_str(), &err) == 0) found++;
							break;
						}
				}
				publishProjectFilesOnMachine();
			});
			return found;
		}, &resolvingFiles);
	command("openWithFiles", "goes on opening the waiting project once its files are found; one that is not the recorded "
		"file is opened anyway, as Chimera's Open Anyway", {}, [this](const Args &, std::string &error) -> Value {
			bool missing = false;
			for (const ProjectFile &f : projectFiles_) missing = missing || f.status == 1;
			if (missing)
			{
				error = tr("session.file_still_missing");
				return {};
			}
			resolvingFiles.set(false);
			status.set(tr("session.opening"));
			submit([this] {
				PendingOpen o = std::move(pendingOpenM_);
				pendingOpenM_ = PendingOpen{};
				if (o.project == nullptr) return;
				bool mismatch = false;
				for (int32_t i = 0; i < ce_project_file_count(o.project); i++) mismatch = mismatch || ce_project_file_status(o.project, i) == 2;
				recoveredM_ = o.recovered;
				bootProjectOnMachine(o.project, o.path, o.package, o.keepInputs, o.reuse, o.kept, o.keptRerecords,
					mismatch ? CE_OPEN_ALLOW_FILE_MISMATCH : 0);
			});
			return {};
		}, &resolvingFiles);
	command("cancelOpen", "leaves the waiting project unopened", {}, [this](const Args &, std::string &) -> Value {
		resolvingFiles.set(false);
		submit([this] {
			missingFirmwareM_.clear();
			locatedFirmwareM_.clear();
			/* nobody's: a reused project was let go by projectM_ when its open began */
			if (pendingOpenM_.project != nullptr) ce_project_free(pendingOpenM_.project);
			pendingOpenM_ = PendingOpen{};
			later([this] {
				projectFiles_.clear();
				projectFileRows_.reset.emit();
				status.set(tr("session.not_opened"));
			});
		});
		return {};
	}, &resolvingFiles);
	expose("running", "whether the machine is running on its own", running);
	expose("frame", "the frame the machine is about to run", frame);
	expose("fps", "frames run in the last half second, per second", fps);
	expose("status", "what the session last had to say", status);
	expose("coreName", "the open core", coreName);
	expose("systemName", "the machine the core runs", systemName);
	expose("systemId", "the machine's id, as the core names it", systemId);
	expose("buttonCount", "how many buttons the machine has", buttonCount);
	expose("gamepads", "the gamepads in use, or why there are none", gamepads);
	expose("speedPercent", "how fast the machine runs, a percent of its own rate (1 to 6400)", speedPercent, true);
	expose("unthrottled", "the machine runs as fast as it can", unthrottled, true);
	expose("fastDrawOneIn", "Turbo and unthrottled draw every frame (1), or one in 2 to 10 (3 at first, as Chimera's)", fastDrawOneIn, true);
	fastDrawWatch_ = fastDrawOneIn.changed.connect([this] {
		if (fastDrawOneIn.get() < 1 || fastDrawOneIn.get() > 10) fastDrawOneIn.set(std::clamp<int64_t>(fastDrawOneIn.get(), 1, 10));
		fastDrawM_ = fastDrawOneIn.get();
	});
	expose("soundOn", "the machine's sound is played", soundOn, true);
	expose("volume", "the sound's volume, 0 to 100", volume, true);
	speedWatch_ = speedPercent.changed.connect([this] {
		speedM_ = std::clamp<int64_t>(speedPercent.get(), 1, 6400);
	});
	unthrottledWatch_ = unthrottled.changed.connect([this] { unthrottledM_ = unthrottled.get(); });
	expose("throttle", "what keeps the machine to its speed: clock, audio, vsync, or \"\" for nothing", throttle, true);
	expose("vsyncEnabled", "the window's pictures given to the screen at its refresh (from the next start)", vsyncEnabled, true);
	throttleWatch_ = throttle.changed.connect([this] {
		const std::string t = throttle.get();
		const int mode = t == "clock" ? 0 : t == "audio" ? 1 : t == "vsync" ? 2 : t.empty() ? 3 : -1;
		if (mode < 0)
		{
			throttle.set("clock");
			return;
		}
		throttleM_ = mode;
		{
			std::lock_guard<std::mutex> lock(m_);
			due_ = Clock::now();
		}
		cv_.notify_all();
		/* as Chimera says it */
		static const char *const types[] = {"session.throttle_clock", "session.throttle_sound", "session.throttle_vsync", "session.throttle_none"};
		status.set(tr(unthrottled.get() ? "session.unthrottled_by" : "session.throttled_by", {tr(types[mode])})
			+ (mode == 2 ? tr(vsyncEnabled.get() ? "session.vsync_on" : "session.vsync_off") : std::string()));
	});
	soundOnWatch_ = soundOn.changed.connect([this] { soundOnM_ = soundOn.get(); });
	expose("fastForwardPercent", "how fast fast forward runs, a percent of the machine's rate (Chimera's alternate speed)",
		fastForwardPercent, true);
	expose("soundDevice", "the sound device, by name; \"\" the system's own", soundDevice, true);
	expose("soundBufferMs", "how far ahead the sound may be queued, in milliseconds (10 to 1000)", soundBufferMs, true);
	expose("soundNormalOn", "the sound is heard at the machine's own speed", soundNormalOn, true);
	expose("soundFastOn", "the sound is heard while fast forwarding", soundFastOn, true);
	expose("fastVolume", "the volume while fast forwarding, a percent of the volume", fastVolume, true);
	expose("muteFrameAdvance", "no sound for frames advanced one at a time", muteFrameAdvance, true);
	expose("soundDevices", "the sound devices there are, as [\"name\", ...]", soundDevices);
	soundDeviceWatch_ = soundDevice.changed.connect([this] {
		submit([this, d = soundDevice.get()] {
			soundDeviceM_ = d;
			if (soundM_) reopenSoundOnMachine();
		});
	});
	soundBufferWatch_ = soundBufferMs.changed.connect([this] {
		if (soundBufferMs.get() < 10 || soundBufferMs.get() > 1000) soundBufferMs.set(std::clamp<int64_t>(soundBufferMs.get(), 10, 1000));
		submit([this, ms = soundBufferMs.get()] {
			soundBufferMsM_ = ms;
			if (soundM_) reopenSoundOnMachine();
		});
	});
	soundNormalWatch_ = soundNormalOn.changed.connect([this] { soundNormalOnM_ = soundNormalOn.get(); });
	soundFastWatch_ = soundFastOn.changed.connect([this] { soundFastOnM_ = soundFastOn.get(); });
	muteAdvanceWatch_ = muteFrameAdvance.changed.connect([this] { muteAdvanceM_ = muteFrameAdvance.get(); });
	fastVolumeWatch_ = fastVolume.changed.connect([this] {
		if (fastVolume.get() < 0 || fastVolume.get() > 100) fastVolume.set(std::clamp<int64_t>(fastVolume.get(), 0, 100));
		fastVolumeM_ = fastVolume.get();
	});
	command("findSoundDevices", "looks for the sound devices again (soundDevices)", {}, [this](const Args &, std::string &) -> Value {
		submit([this] {
			if (!soundM_) soundM_ = std::make_unique<Sound>(soundDeviceM_, static_cast<int32_t>(soundBufferMsM_));
			json::Json list;
			list.kind = json::Json::Kind::Array;
			for (const std::string &n : soundM_->devices()) list.items.push_back(json::Json::string(n));
			later([this, text = json::write(list)] { soundDevices.set(text); });
		});
		return {};
	});
	expose("greenzoneBudgetMb", "what the history of states may keep in memory, in MB (64 at least), for every project",
		greenzoneBudgetMb, true);
	expose("greenzoneNearStride", "how close the history stays behind the playhead: one frame in this many (1 to 32)",
		greenzoneNearStride, true);
	expose("pictureSettleFrames", "how many frames after a load show the picture a frame had before (0 to 10)",
		pictureSettleFrames, true);
	expose("projectBudgetMb", "the open project's own memory budget, MB; 0 for every project's", projectBudgetMb);
	expose("projectNearStride", "the open project's own closeness behind the playhead; 0 for every project's", projectNearStride);
	expose("budgetMbApplied", "the memory budget the machine open was given, MB", budgetMbApplied);
	expose("nearStrideApplied", "the closeness behind the playhead the machine open was given", nearStrideApplied);
	budgetWatch_ = greenzoneBudgetMb.changed.connect([this] {
		if (greenzoneBudgetMb.get() < 64) greenzoneBudgetMb.set(64);
		budgetMbM_ = greenzoneBudgetMb.get();
	});
	strideWatch_ = greenzoneNearStride.changed.connect([this] {
		if (greenzoneNearStride.get() < 1 || greenzoneNearStride.get() > 32) greenzoneNearStride.set(std::clamp<int64_t>(greenzoneNearStride.get(), 1, 32));
		nearStrideM_ = greenzoneNearStride.get();
	});
	settleWatch_ = pictureSettleFrames.changed.connect([this] {
		if (pictureSettleFrames.get() < 0 || pictureSettleFrames.get() > 10) pictureSettleFrames.set(std::clamp<int64_t>(pictureSettleFrames.get(), 0, 10));
		pictureSettleM_ = pictureSettleFrames.get();
	});
	command("setProjectBudgets", "the open project's own budgets (memory in MB, 64 at least; closeness 1 to 32), or none "
		"(0 and 0): kept beside its cache, as Chimera's; they take effect when it next opens", {"memoryMb", "nearStride"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t mb = 0, stride = 0;
			if (!arg(a, 0, mb, error) || !arg(a, 1, stride, error)) return {};
			if ((mb != 0 && mb < 64) || stride < 0 || stride > 32)
			{
				error = tr("session.budget_range");
				return {};
			}
			bool open = false;
			onMachineWait([&](ce_session *) {
				namespace fs = std::filesystem;
				const fs::path dir = cacheDirOf(projectM_);
				if (dir.empty()) return;
				open = true;
				std::error_code ec;
				/* nothing asked for leaves no file to explain later */
				if (mb == 0 && stride == 0)
				{
					fs::remove(dir / "budgets.json", ec);
					return;
				}
				json::Json b;
				b.kind = json::Json::Kind::Object;
				if (mb != 0) b.members.emplace_back("memoryMb", json::Json::integer(mb));
				if (stride != 0) b.members.emplace_back("maxNearStride", json::Json::integer(stride));
				fs::create_directories(dir, ec);
				std::ofstream(dir / "budgets.json", std::ios::binary | std::ios::trunc) << json::write(b);
			});
			if (!open)
			{
				error = tr("session.no_project");
				return {};
			}
			projectBudgetMb.set(mb);
			projectNearStride.set(stride);
			return {};
		}, &hasProject);
	expose("compileCacheRoot", "where cores that compile their machine's code keep it, a folder a game", compileCacheRoot, true);
	expose("compileCacheDir", "the open machine's compile cache folder, \"\" for none", compileCacheDir);
	compileRootWatch_ = compileCacheRoot.changed.connect([this] { submit([this, r = compileCacheRoot.get()] { compileRootM_ = r; }); });
	fastSpeedWatch_ = fastForwardPercent.changed.connect([this] {
		if (fastForwardPercent.get() < 1 || fastForwardPercent.get() > 6400) fastForwardPercent.set(std::clamp<int64_t>(fastForwardPercent.get(), 1, 6400));
		fastSpeedM_ = fastForwardPercent.get();
	});
	expose("skipLagFrames", "a frame advance that lands after a lag frame runs on until one that is not (Chimera's Skip Lag "
		"Frames)", skipLagFrames, true);
	expose("displayNothing", "the frames are run without being drawn; the speed goes to the window's title (Chimera's "
		"Display Nothing)", displayNothing, true);
	expose("acceptBackgroundInput", "a gamepad is heard while the window is not the one in front", acceptBackgroundInput, true);
	expose("windowActive", "the window is the one in front (the window says)", windowActive, true);
	skipLagWatch_ = skipLagFrames.changed.connect([this] { skipLagM_ = skipLagFrames.get(); });
	displayNothingWatch_ = displayNothing.changed.connect([this] { displayNothingM_ = displayNothing.get(); });
	auto heard = [this] { padsHeardM_ = acceptBackgroundInput.get() || windowActive.get(); };
	backgroundWatch_ = acceptBackgroundInput.changed.connect(heard);
	activeWatch_ = windowActive.changed.connect(heard);
	volumeWatch_ = volume.changed.connect([this] { volumeM_ = std::clamp<int64_t>(volume.get(), 0, 100); });
	/* Chimera's speeds, a step at a time */
	auto speedStep = [this](int direction) {
		static const int64_t steps[] = {1, 3, 6, 12, 25, 50, 75, 100, 150, 200, 300, 400, 800, 1600, 3200, 6400};
		const int64_t now = speedPercent.get();
		int64_t next = now;
		if (direction > 0)
		{
			for (int64_t s : steps)
				if (s > now)
				{
					next = s;
					break;
				}
		}
		else
			for (int i = static_cast<int>(std::size(steps)) - 1; i >= 0; i--)
				if (steps[i] < now)
				{
					next = steps[i];
					break;
				}
		speedPercent.set(next);
		status.set(tr("session.speed", {std::to_string(next)}));
	};
	command("speedUp", "the next of Chimera's speeds up", {}, [speedStep](const Args &, std::string &) -> Value {
		speedStep(1);
		return {};
	});
	command("speedDown", "the next of Chimera's speeds down", {}, [speedStep](const Args &, std::string &) -> Value {
		speedStep(-1);
		return {};
	});
	command("speedReset", "the machine's own speed, 100%", {}, [this](const Args &, std::string &) -> Value {
		speedPercent.set(100);
		status.set("Speed: 100%");
		return {};
	});
	command("toggleThrottle", "as fast as it can, or at its speed again", {}, [this](const Args &, std::string &) -> Value {
		unthrottled.set(!unthrottled.get());
		status.set(tr(unthrottled.get() ? "session.unthrottled" : "session.throttled"));
		return {};
	});
	command("toggleSound", "the sound on or off", {}, [this](const Args &, std::string &) -> Value {
		soundOn.set(!soundOn.get());
		status.set(tr(soundOn.get() ? "session.sound_on" : "session.sound_off"));
		return {};
	});
	command("volumeUp", "the volume up by ten", {}, [this](const Args &, std::string &) -> Value {
		volume.set(std::min<int64_t>(100, volume.get() + 10));
		status.set(tr("session.volume", {std::to_string(volume.get())}));
		return {};
	});
	command("volumeDown", "the volume down by ten", {}, [this](const Args &, std::string &) -> Value {
		volume.set(std::max<int64_t>(0, volume.get() - 10));
		status.set(tr("session.volume", {std::to_string(volume.get())}));
		return {};
	});
	expose("showFps", "the speed is shown over the picture", showFps, true);
	expose("showFrameCounter", "the frame counter is shown over the picture", showFrameCounter, true);
	expose("showLagCounter", "the lag counter is shown over the picture", showLagCounter, true);
	expose("showInput", "the input of the frame just run is shown over the picture", showInput, true);
	expose("fpsText", "the speed as shown: \"60 fps\", with >> fast forwarding, << rewinding", fpsText);
	expose("frameCounterText", "the frame counter as shown: the frame, the last frame with input, (Finished) past it", frameCounterText);
	expose("inputText", "the input of the frame just run, as the input display writes it", inputText);
	expose("lagCount", "lag frames so far", lagCount);
	expose("lagFrame", "the frame just run was a lag frame", lagFrame);
	expose("showAverageFps", "the average speed since it was turned on or reset is shown on the speed's line", showAverageFps, true);
	expose("averageFpsText", "the average speed as shown: \"avg 59.9 fps\", \"\" until something counted", averageFpsText);
	expose("showRerecords", "the rerecord count is shown over the picture", showRerecords, true);
	expose("showMessages", "messages are shown over the picture a moment", showMessages, true);
	expose("showStatusBar", "the status bar is shown", showStatusBar, true);
	expose("captureOsd", "a screenshot has the counters and messages over the picture too", captureOsd, true);
	expose("captureMouse", "Chimera's Capture Mouse: the pointer kept over the picture and hidden (remembered)", captureMouse, true);
	expose("mouseBound", "an analog control is bound to the pointer (WMouse or RMouse X or Y), as Capture Mouse asks", mouseBound);
	expose("isGameCore", "the core open is a game (its menu is Game, not Emulator)", isGameCore);
	expose("hasGameTimer", "the game open has a timer of its own", hasGameTimer);
	expose("hasSaveData", "the core open writes out the user's save data", hasSaveData);
	expose("hasTrace", "the core traces what its CPU runs (the Trace Logger)", hasTrace);
	expose("hasRegisters", "the core shows its registers (CPU Registers)", hasRegisters);
	expose("traceHeader", "the core's words for its trace lines' columns", traceHeader);
	expose("surfaces", "the pictures the core draws for itself, as [{\"name\", \"width\", \"height\"}]", surfaces);
	expose("movieImportMenu", "the menu item of the core open's movie importer, in its package's words (\"\" when it has none)", movieImportMenu);
	expose("showGameTime", "a game's own timer is shown over the picture", showGameTime, true);
	expose("gameTimeText", "the game's timer as shown: \"IGT 01:02.345\"", gameTimeText);
	expose("coreLogPath", "the file the core log goes to, \"\" while it is off", coreLogPath);
	command("coreLogName", "the name Chimera suggests for a core log: the core, \"core log\", the date and time", {},
		[this](const Args &, std::string &) -> Value {
			const std::time_t now = std::time(nullptr);
			char when[32];
			std::strftime(when, sizeof when, "%Y-%m-%d %H.%M.%S", std::localtime(&now));
			const std::string core = isOpen.get() && !coreName.get().empty() ? coreName.get() : "Chimera";
			return tr("session.core_log_file", {core, when});
		});
	command("startCoreLog", "keeps what the cores write in this file from now on (a session opened from now notes its core)",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			if (ce_core_log(path.c_str()) != 1)
			{
				const char *why = ce_core_log_error();
				error = tr("session.core_log_not_started", {why != nullptr ? why : tr("common.no_reason")});
				return {};
			}
			coreLogPath.set(path);
			return true;
		});
	command("stopCoreLog", "stops keeping the core log; the file stays", {}, [this](const Args &, std::string &) -> Value {
		const std::string was = coreLogPath.get();
		ce_core_log(nullptr);
		coreLogPath.set("");
		if (!was.empty()) status.set(tr("session.core_log_off", {was}));
		return was;
	});
	command("coreLogSize", "how many bytes the core log has so far", {}, [this](const Args &, std::string &) -> Value {
		std::error_code ec;
		const auto size = std::filesystem::file_size(coreLogPath.get(), ec);
		return ec ? int64_t{0} : static_cast<int64_t>(size);
	});
	overlayWatches_.push_back(showGameTime.changed.connect([this] {
		showGameTimeM_.store(showGameTime.get());
		submit([this] { report(-1, -1, -1, -1, -1); });
	}));
	command("saveDataFiles", "the save files the machine holds now, a line each", {}, [this](const Args &, std::string &error) -> Value {
		std::string names;
		bool open = false;
		onMachineWait([&](ce_session *s) {
			if (s == nullptr || !ce_session_savedata_available(s)) return;
			open = true;
			const int32_t n = ce_session_savedata_count(s);
			for (int32_t i = 0; i < n; i++)
				if (const char *name = ce_session_savedata_name(s, i)) names += (names.empty() ? "" : "\n") + std::string(name);
		});
		if (!open)
		{
			error = tr("session.no_save_data_kept");
			return {};
		}
		return names;
	}, &hasSaveData);
	command("exportSaveData", "writes the save data out, as Chimera's Export Save Data: one file as it is, more in a zip; "
		"says how many files", {"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			std::string why;
			int64_t written = -1;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr || !ce_session_savedata_available(s))
				{
					why = tr("session.no_save_data_kept");
					return;
				}
				const int32_t n = ce_session_savedata_count(s);
				if (n == 0)
				{
					why = tr("session.no_save_data_yet");
					return;
				}
				/* each file read in pieces, as the engine streams it */
				auto contents = [&](int32_t i) {
					std::vector<uint8_t> bytes(static_cast<size_t>(std::max<int64_t>(0, ce_session_savedata_size(s, i))));
					int64_t at = 0;
					while (at < static_cast<int64_t>(bytes.size()))
					{
						const int64_t got = ce_session_savedata_read(s, i, at, bytes.data() + at,
							std::min<int64_t>(1 << 20, static_cast<int64_t>(bytes.size()) - at));
						if (got <= 0) break;
						at += got;
					}
					bytes.resize(static_cast<size_t>(at));
					return bytes;
				};
				std::ofstream out(path, std::ios::binary);
				if (n == 1)
				{
					const std::vector<uint8_t> one = contents(0);
					out.write(reinterpret_cast<const char *>(one.data()), static_cast<std::streamsize>(one.size()));
				}
				else
				{
					std::vector<std::pair<std::string, std::vector<uint8_t>>> files;
					for (int32_t i = 0; i < n; i++)
					{
						const char *name = ce_session_savedata_name(s, i);
						files.emplace_back(name != nullptr ? name : "file" + std::to_string(i), contents(i));
					}
					const std::vector<uint8_t> zip = storedZip(files);
					out.write(reinterpret_cast<const char *>(zip.data()), static_cast<std::streamsize>(zip.size()));
				}
				if (!out)
				{
					why = tr("session.could_not_write", {path});
					return;
				}
				written = n;
			});
			if (written < 0)
			{
				error = why;
				return {};
			}
			status.set(tr("session.save_data_written", {path}));
			return written;
		}, &hasSaveData);
	command("resetAverageFps", "the average speed counted afresh from now", {}, [this](const Args &, std::string &) -> Value {
		averageResetM_.store(true);
		averageFpsText.set("");
		return {};
	});
	/* turned on, it counts from then: what ran while nobody looked is not part of it */
	overlayWatches_.push_back(showAverageFps.changed.connect([this] {
		if (!showAverageFps.get()) return;
		averageResetM_.store(true);
		averageFpsText.set("");
	}));
	/* turned on, a counter says at once what it has to say, counted afresh:
	 * off, it heard of no change */
	for (Property<bool> *shown : {&showFps, &showFrameCounter, &showLagCounter, &showInput})
		overlayWatches_.push_back(shown->changed.connect([this] {
			lastInputFor_ = -1;
			lagsTo_ = lags_ = 0;
			updateOverlay(frame.get(), -1);
		}));
	expose("autoheld", "the buttons autohold holds, a space between", autoheld);
	expose("autofired", "the buttons autofire fires, a space between", autofired);
	expose("stickyArmed", "autohold or autofire is armed: a button pressed now is toggled", stickyArmed);
	expose("stickyText", "the sticky buttons as Chimera shows them while arming: \"Held: P1 A Auto-P1 B \"", stickyText);
	expose("autofireOn", "frames autofire presses a button for", autofireOn, true);
	expose("autofireOff", "frames autofire lets it go for", autofireOff, true);
	expose("autofireSkipsLag", "lag frames do not count in autofire's pattern", autofireSkipsLag, true);
	stickyWatches_.push_back(autofireOn.changed.connect([this] { fireOnM_ = std::max<int64_t>(1, autofireOn.get()); }));
	stickyWatches_.push_back(autofireOff.changed.connect([this] { fireOffM_ = std::max<int64_t>(1, autofireOff.get()); }));
	stickyWatches_.push_back(autofireSkipsLag.changed.connect([this] { fireSkipsLagM_ = autofireSkipsLag.get(); }));
	command("clearAutohold", "no button held or fired by autohold and autofire any longer", {}, [this](const Args &, std::string &) -> Value {
		autoholdMask_.set({});
		autofireMask_.set({});
		stickyChanged();
		status.set(tr("session.autohold_cleared"));
		return {};
	});
	command("toggleAutohold", "a button held by autohold, or no longer", {"button"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		for (size_t b = 0; b < buttonNames_.size(); b++)
			if (buttonNames_[b] == name)
			{
				autoholdMask_.flip(b);
				stickyChanged();
				return {};
			}
		error = tr("session.no_button", {name});
		return {};
	});
	command("toggleAutofire", "a button fired by autofire, or no longer", {"button"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		for (size_t b = 0; b < buttonNames_.size(); b++)
			if (buttonNames_[b] == name)
			{
				autofireMask_.flip(b);
				submit([this, b] {
					if (fireAtM_.size() <= b) fireAtM_.resize(b + 1, 0);
					fireAtM_[b] = 0;
				});
				stickyChanged();
				return {};
			}
		error = tr("session.no_button", {name});
		return {};
	});
	expose("catchingPad", "waiting for a gamepad input to be pressed (catchPad)", catchingPad);
	expose("caughtPad", "the gamepad input caught, by Chimera's name; \"\" while waiting", caughtPad);
	expose("sound", "\"Sound on\", or why there is none", sound);
	expose("soundQueued", "sample pairs sent to the speakers", soundQueued);
	expose("soundDropped", "sample pairs dropped because the machine ran ahead of the speakers", soundDropped);
	expose("greenEnd", "frames before this one were run with the inputs they have now", greenEnd);
	expose("greenzoneStates", "machine states the greenzone holds", greenzoneStates);
	expose("rerecords", "how many times the movie was changed over work already done (saved with the project)", rerecords);
	expose("autoRestore", "after an edit before the current frame, run on to where the machine was", autoRestore, true);
	expose("rewinding", "rewind is held: the machine goes back (Chimera's client.is_rewinding)", rewinding);
	expose("turbo", "turbo is held (Chimera's client.isturbo)", turbo);
	expose("turboSeek", "TAStudio's Turbo seek: a seek goes to its frame at once; off, past what the greenzone holds it "
		"plays there at the machine's own speed", turboSeek, true);
	expose("seekingTo", "the frame a seek that plays is going to, -1 when none is", seekingTo);
	expose("seekFrom", "the frame a seek that plays set off from", seekFrom);
	expose("restoreFrame", "the frame the machine goes back to after edits (Chimera's green arrow), -1 for none", restoreFrame);
	expose("drives", "the machine's drives for the status bar's lights: [{name, on, short, long}], what is in each and "
		"what its selector is on", drives);
	turboSeekWatch_ = turboSeek.changed.connect([this] { turboSeekM_.store(turboSeek.get()); });
	expose("recordingMode", "TAStudio's recording mode: each frame played with the person's input becomes what is held; "
		"off, the movie is played as it is", recordingMode, true);
	recordingWatch_ = recordingMode.changed.connect([this] { recordingModeM_ = recordingMode.get(); });
	command("toggleRecording", "recording mode on or off (Chimera's Toggle read-only)", {}, [this](const Args &, std::string &) -> Value {
		recordingMode.set(!recordingMode.get());
		return recordingMode.get();
	}, &hasProject);
	expose("frozenCount", "how many frozen addresses are on (Chimera's freezes)", frozenCount);
	expose("stoppedAt", "the frame the core stopped on, -1 while it runs (Chimera's The core stopped)", stoppedAt);
	expose("stoppedReason", "what the sandbox said when the core stopped", stoppedReason);
	expose("stoppedSafePoint", "the newest state at or before the frame the core stopped on, -1 for none", stoppedSafePoint);
	expose("stoppedCanRestart", "the state history still holds frame 0, to run again from", stoppedCanRestart);
	expose("stoppedFreezesOff", "how many frozen addresses were switched off when the core stopped", stoppedFreezesOff);
	command("stoppedChoice", "answers The core stopped, as Chimera's: \"back\" goes to the latest safe point, \"restart\" "
		"runs again from frame 0, \"keepWork\" keeps the work against a crash and lets the project go without "
		"saving when the program closes (it is offered at the next open); \"\" only closes the question",
		{"choice"}, [this](const Args &a, std::string &error) -> Value {
			std::string choice;
			if (!arg(a, 0, choice, error)) return {};
			const int64_t to = choice == "back" ? stoppedSafePoint.get() : choice == "restart" ? int64_t{0} : -1;
			if ((choice == "back" && to < 0) || (choice == "restart" && !stoppedCanRestart.get()))
			{
				error = tr("session.nothing_to_go_back_to");
				return {};
			}
			if (!choice.empty() && choice != "back" && choice != "restart" && choice != "keepWork")
			{
				error = tr("session.no_choice", {choice});
				return {};
			}
			stoppedAt.set(-1);
			if (to >= 0) submit([this, to] { seekOnMachine(to, false); });
			/* waited for: the program closes next, and a job still queued then is not run */
			if (choice == "keepWork")
				waitOnMachine([this] {
					keepRecoveryOnMachine();
					keepWorkM_ = true;
				});
			return {};
		}, &isOpen);
	command("freeze", "a domain's 1, 2 or 4 bytes at an address written with this value before every frame, for this "
		"session only", {"domain", "address", "size", "bigEndian", "value"}, [this](const Args &a, std::string &error) -> Value {
			Freeze f;
			int64_t size = 1, value = 0;
			if (!arg(a, 0, f.domain, error) || !arg(a, 1, f.address, error) || !arg(a, 2, size, error) || !arg(a, 3, f.bigEndian, error) ||
				!arg(a, 4, value, error))
				return {};
			f.size = static_cast<int32_t>(size);
			f.value = static_cast<uint64_t>(value);
			if (!freeze(f, error)) return {};
			return true;
		}, &isOpen);
	command("freezeProperty", "a game property set from this text before every frame, for this session only", {"name", "text"},
		[this](const Args &a, std::string &error) -> Value {
			Freeze f;
			if (!arg(a, 0, f.property, error) || !arg(a, 1, f.text, error)) return {};
			if (!freeze(f, error)) return {};
			return true;
		}, &isOpen);
	command("unfreeze", "the freezes writing this address of a domain go", {"domain", "address"}, [this](const Args &a, std::string &error) -> Value {
		std::string domain;
		int64_t address = 0;
		if (!arg(a, 0, domain, error) || !arg(a, 1, address, error)) return {};
		unfreeze(domain, address);
		return {};
	});
	command("unfreezeProperty", "a game property's freeze goes", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		unfreezeProperty(name);
		return {};
	});
	command("unfreezeAll", "every freeze goes (Chimera's Unfreeze All)", {}, [this](const Args &, std::string &) -> Value {
		unfreezeAll();
		return {};
	});
	command("disableAllFreezes", "every freeze switched off, kept to switch on again", {},
		[this](const Args &, std::string &) -> Value {
			for (Freeze &f : freezes_) f.enabled = false;
			syncFreezes();
			return {};
		});
	command("toggleAllFreezes", "every freeze switched on or off (Chimera's Toggle All Freezes)", {}, [this](const Args &, std::string &) -> Value {
		if (freezes_.empty()) return {};
		/* as Chimera's: each one turned over, and said as the first one went unless they differed */
		const bool first = freezes_.front().enabled;
		const bool same = std::all_of(freezes_.begin(), freezes_.end(), [first](const Freeze &f) { return f.enabled == first; });
		for (Freeze &f : freezes_) f.enabled = !f.enabled;
		syncFreezes();
		status.set(tr(!same ? "session.freezes_mixed" : first ? "session.freezes_off" : "session.freezes_on"));
		return {};
	});
	expose("greenzonePeriod", "which frames the greenzone stores: 1 every frame, N one in N, 0 none (saved with the project)",
		greenzonePeriod, true);
	expose("greenzoneEvery", "the N of storing every N frames, 2 to 999", greenzoneEvery, true);
	command("cycleGreenzone", "every frame, then every N frames, then none, then every frame again", {},
		[this](const Args &, std::string &) -> Value {
			const int64_t now = greenzonePeriod.get();
			greenzonePeriod.set(now == 1 ? std::clamp<int64_t>(greenzoneEvery.get(), 2, 999) : now == 0 ? 1 : 0);
			return greenzonePeriod.get();
		});
	periodWatch_ = greenzonePeriod.changed.connect([this] {
		if (greenzonePeriod.get() < 0) greenzonePeriod.set(0);
		const int64_t p = greenzonePeriod.get();
		periodM_.store(p);
		submit([this, p] {
			if (s_ != nullptr) ce_session_greenzone_capture_period(s_, p);
		});
	});
	expose("lastSeekMs", "how long the last seek took, in milliseconds", lastSeekMs);
	expose("lastSeekFrames", "how many frames the last seek ran", lastSeekFrames);
	expose("projectPath", "the open project's file, \"\" while it is not saved yet", projectPath);
	expose("hasProject", "a project is open, saved or not yet", hasProject);
	expose("projectTitle", "the open project's title", projectTitle);
	expose("gameName", "the game's name: its file's, without the kind", gameName);
	expose("backupFolder", "where a project's backups go", backupFolder, true);
	backupWatch_ = backupFolder.changed.connect([this] { submit([this, f = backupFolder.get()] { backupFolderM_ = f; }); });
	expose("recoveryFolder", "where a project's unsaved work is kept against a crash, a folder per project; \"\" keeps none",
		recoveryFolder);
	recoveryWatch_ = recoveryFolder.changed.connect([this] { submit([this, f = recoveryFolder.get()] { recoveryFolderM_ = f; }); });
	expose("recoveredCopy", "the copy among the backups a crash's unsaved work was rebuilt into, while the project's open "
		"waits for recoverWork; \"\" when nothing is offered", recoveredCopy);
	expose("recoveredUpTo", "when the recovered work was last kept, as the clock here reads (HH:MM:SS)", recoveredUpTo);
	expose("crashFolder", "where crash notes are written (Windows), <data>/Crashes", crashFolder);
	crashWatch_ = crashFolder.changed.connect([this] { submit([this, f = crashFolder.get()] { crashFolderM_ = f; }); });
	expose("recoveredCrash", "beside recovered work, what ended the session that left it, as its crash note says (\"a fast fail "
		"in nvoglv64.dll+0x108eb9d (the NVIDIA graphics driver) at frame 21\"); \"\" when no note says", recoveredCrash);
	expose("recoveredCrashNote", "that crash note's file", recoveredCrashNote);
	command("recoverWork", "answers the offer of a crash's unsaved work: true opens that work, unsaved, and saving writes "
		"it to the project; false opens the project as it was last saved. The recovered copy stays among the backups "
		"either way", {"open"}, [this](const Args &a, std::string &error) -> Value {
			bool open = false;
			if (!arg(a, 0, open, error)) return {};
			const std::string copy = recoveredCopy.get();
			if (copy.empty())
			{
				error = tr("session.no_recovered_work");
				return {};
			}
			recoveredCopy.set("");
			recoveredUpTo.set("");
			status.set(tr("session.opening"));
			submit([this, open, copy] {
				PendingOpen o = std::move(pendingOpenM_);
				pendingOpenM_ = PendingOpen{};
				if (o.project == nullptr) return;
				if (open)
				{
					const char *err = nullptr;
					if (ce_project *q = ce_project_open(copy.c_str(), &err))
					{
						ce_project_free(o.project);
						o.project = q;
						o.recovered = true;
					}
					else
					{
						/* the project as it was saved, then; the copy is still there */
						const std::string why = err != nullptr ? err : tr("common.no_reason");
						later([this, why, copy] { status.set(tr("session.recovered_not_opened", {why, copy})); });
					}
				}
				findFilesAndBootOnMachine(std::move(o));
			});
			return {};
		});
	command("suggestedProjectFile", "where Save Project As proposes the project goes, as Chimera's: its own file once it "
		"has one, else the game's name in the folder given (the Movies folder), made fit for a file name", {"folder"},
		[this](const Args &a, std::string &error) -> Value {
			std::string folder;
			if (!arg(a, 0, folder, error)) return {};
			if (!projectPath.get().empty()) return projectPath.get();
			const std::string name = projectFileName(gameName.get().empty() ? projectTitle.get() : gameName.get());
			if (name.empty()) return folder;
			return (std::filesystem::path(folder) / (name + ".chimeraProject")).string();
		}, &hasProject);
	command("saveBackup", "writes the project as it stands to the backup folder, named for it and the date and time, "
		"as Chimera's Save Project Backup does; the project's own file is left as it is", {},
		[this](const Args &, std::string &error) -> Value {
			if (projectPath.get().empty())
			{
				error = tr(hasProject.get() ? "session.not_saved_yet" : "session.no_project");
				return {};
			}
			if (backupFolder.get().empty())
			{
				error = tr("session.no_backup_folder");
				return {};
			}
			const std::string path = backupPathFor(projectPath.get());
			submit([this, path] { saveProjectOnMachine(path, true); });
			return path;
		}, &hasProject);
	command("clearGreenzone", "every stored state but the first frame's thrown away, and the machine back to frame 0, "
		"as Chimera's Clear Greenzone", {}, [this](const Args &, std::string &) -> Value {
			submit([this] {
				if (s_ == nullptr) return;
				ce_session_greenzone_invalidate(s_, 0);
				greenEndM_ = 0;
				seekOnMachine(0, false);
				report(-1, -1, -1, -1, -1);
			});
			return {};
		}, &isOpen);
	command("checkStateHistory", "Chimera's state history check: says in status whether every state the history holds "
		"puts the machine back exactly; the two states of the first that does not are written to the folder given",
		{"keepIn"}, [this](const Args &a, std::string &error) -> Value {
			std::string keepIn;
			if (!a.empty() && !arg(a, 0, keepIn, error)) return {};
			status.set(tr("session.checking_history"));
			submit([this, keepIn] { checkHistoryOnMachine(keepIn); });
			return {};
		}, &isOpen);
	expose("recentProjects", "projects opened, newest first, a line each", recentProjects, true);
	picture("screen", "the machine's picture", screen, screenChanged);
	expose("unsaved", "the open project was changed since it was opened or last saved", unsaved);
	expose("pauseAtEnd", "pause on the frame the movie runs out (TAStudio's \"automatically pause at end of movie\")",
		pauseAtEnd, true);
	expose("rewindStep", "how many frames a step of rewind goes back", rewindStep, true);
	expose("rewindStepFast", "how many frames a step of rewind goes back while fast forwarding", rewindStepFast, true);
	expose("autosaveSeconds", "save a project with changes by itself every so many seconds; 0 never", autosaveSeconds, true);
	expose("autosaveAsBackup", "the autosave writes a backup (no greenzone) instead of the project's own file",
		autosaveAsBackup, true);
	expose("backupOnSave", "every save by hand writes a backup as well", backupOnSave, true);
	pauseWatch_ = pauseAtEnd.changed.connect([this] { pauseAtEndM_.store(pauseAtEnd.get()); });
	rewindWatch_ = rewindStep.changed.connect([this] {
		if (rewindStep.get() < 1) rewindStep.set(1);
		rewindStepM_.store(rewindStep.get());
	});
	rewindFastWatch_ = rewindStepFast.changed.connect([this] {
		if (rewindStepFast.get() < 1) rewindStepFast.set(1);
		rewindStepFastM_.store(rewindStepFast.get());
	});
	autosaveWatch_ = autosaveSeconds.changed.connect([this] {
		if (autosaveSeconds.get() < 0) autosaveSeconds.set(0);
		const int64_t ms = autosaveSeconds.get() * 1000;
		autosaveMsM_.store(ms);
		/* counted again from now */
		submit([this] { autosaveDue_ = {}; });
	});
	expose("cacheFolder", "where what a project can make again is kept (its branches' states), a folder per project", cacheFolder, true);
	cacheFolderWatch_ = cacheFolder.changed.connect([this] {
		submit([this, folder = cacheFolder.get()] { cacheFolderM_ = folder; });
	});

	/* for scripts (Chimera's Lua memory and joypad libraries) */
	command("memoryDomains", "the machine's memory domains, a line each: name, size and whether it can be written, between "
		"tabs (the first is the main one)", {}, [this](const Args &, std::string &) -> Value {
			std::string out;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				{
					if (ce_session_domain_hidden(s, i) != 0) continue;
					const char *n = ce_session_domain_name(s, i);
					out += std::string(n != nullptr ? n : "") + "\t" + std::to_string(ce_session_domain_size(s, i)) + "\t" +
						(ce_session_domain_writable(s, i) != 0 ? "1" : "0") + "\n";
				}
			});
			return out;
		});
	command("sha256", "bytes' SHA-256, upper-case hex (Chimera's memory.hash_region)", {"bytes"}, [this](const Args &a, std::string &error) -> Value {
		std::string bytes;
		if (!arg(a, 0, bytes, error)) return {};
		return sha256Hex(bytes);
	});
	command("readMemory", "bytes of a memory domain, as a string of them", {"domain", "address", "length"},
		[this](const Args &a, std::string &error) -> Value {
			std::string domain;
			int64_t address = 0, length = 0;
			if (!arg(a, 0, domain, error) || !arg(a, 1, address, error) || !arg(a, 2, length, error)) return {};
			std::string bytes;
			std::string why;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr)
				{
					why = tr("session.no_machine_running");
					return;
				}
				for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				{
					const char *n = ce_session_domain_name(s, i);
					if (n == nullptr || domain != n) continue;
					const int64_t size = ce_session_domain_size(s, i);
					if (address < 0 || length < 0 || address + length > size)
					{
						why = tr("session.no_address", {domain, std::to_string(address + length - 1)});
						return;
					}
					bytes.resize(static_cast<size_t>(length));
					ce_session_domain_read(s, i, address, reinterpret_cast<uint8_t *>(bytes.data()), length);
					return;
				}
				why = tr("session.no_domain", {domain});
			});
			if (!why.empty())
			{
				error = why;
				return {};
			}
			return bytes;
		});
	command("writeMemory", "bytes written into a memory domain (the states after this frame no longer hold)",
		{"domain", "address", "bytes"}, [this](const Args &a, std::string &error) -> Value {
			std::string domain, bytes;
			int64_t address = 0;
			if (!arg(a, 0, domain, error) || !arg(a, 1, address, error) || !arg(a, 2, bytes, error)) return {};
			std::string why;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr)
				{
					why = tr("session.no_machine_running");
					return;
				}
				for (int32_t i = 0; i < ce_session_domain_count(s); i++)
				{
					const char *n = ce_session_domain_name(s, i);
					if (n == nullptr || domain != n) continue;
					const int64_t size = ce_session_domain_size(s, i);
					const uint64_t ptr = ce_session_domain_writable(s, i) != 0 ? ce_session_domain_ptr(s, i) : 0;
					if (ptr == 0)
					{
						why = tr("session.domain_read_only", {domain});
						return;
					}
					if (address < 0 || address + static_cast<int64_t>(bytes.size()) > size)
					{
						why = tr("session.no_address", {domain, std::to_string(address + static_cast<int64_t>(bytes.size()) - 1)});
						return;
					}
					std::memcpy(reinterpret_cast<uint8_t *>(ptr) + address, bytes.data(), bytes.size());
					dropStatesAfterOnMachine(frameM_);
					return;
				}
				why = tr("session.no_domain", {domain});
			});
			if (!why.empty())
			{
				error = why;
				return {};
			}
			return {};
		});
	/* what a script reads and writes of the machine's memory, and the frame
	 * it is on, the machine answers on its own thread, where scripts run */
	command("videoSize", "the picture the machine draws: its width, a space, its height (Chimera's client.bufferwidth "
		"and bufferheight); with nothing open, Chimera's empty machine's, 256 by 192", {}, [this](const Args &, std::string &) -> Value {
			int64_t w = 256, h = 192;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				w = ce_session_video_width(s);
				h = ce_session_video_height(s);
			});
			return std::to_string(w) + " " + std::to_string(h);
		});
	command("controlLabels", "what this game calls its controls, where it names them (Chimera's labels, beside the declared "
		"names): a line each, the declared name, a tab, the game's", {}, [this](const Args &, std::string &) -> Value {
			std::string out;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				for (size_t b = 0; b < buttonNames_.size(); b++)
					if (const char *l = ce_session_button_label(s, static_cast<int64_t>(b)); l != nullptr && *l != '\0') out += buttonNames_[b] + "\t" + l + "\n";
				for (size_t x = 0; x < axes_.size(); x++)
					if (const char *l = ce_session_axis_label(s, static_cast<int64_t>(x)); l != nullptr && *l != '\0') out += axes_[x].name + "\t" + l + "\n";
			});
			return out;
		});
	command("gameSha1", "the SHA-1 of the game's file (a project's first), as Chimera's game hash: \"\" with none; read "
		"when first asked", {}, [this](const Args &, std::string &) -> Value {
			std::string out;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr || romFileM_.empty()) return;
				if (romSha1For_ != romFileM_)
				{
					char hex[41] = {};
					uint64_t len = 0;
					romSha1_ = ce_sha1_file(romFileM_.c_str(), hex, &len) == 1 ? std::string(hex, 40) : std::string();
					romSha1For_ = romFileM_;
				}
				out = romSha1_;
			});
			return out;
		});
	for (const char *name : {"memoryDomains", "readMemory", "writeMemory", "sha256", "videoSize", "gameSha1"}) scriptSideCommand(name);
	scriptSideProperty("frame", [this] { return Value(int64_t{frameM_}); });
	command("movieInput", "a frame's input in the movie, a line each: a button pressed by its name, an analog control as "
		"name=value; \"\" past the movie's end", {"frame"}, [this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			std::string out;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr || frame < 0 || frame >= ce_session_movie_length(s)) return;
				std::vector<int32_t> axes(static_cast<size_t>(ce_session_axis_count(s)));
				std::vector<uint8_t> states(static_cast<size_t>(ce_session_button_count(s)));
				/* 0 read; 1 past the end; -1 a line that cannot be read */
				if (ce_session_movie_get(s, frame, states.data(), axes.empty() ? nullptr : axes.data()) != 0) return;
				for (size_t b = 0; b < buttonNames_.size() && b < states.size(); b++)
					if (states[b] != 0) out += buttonNames_[b] + "\n";
				for (size_t x = 0; x < axes.size() && x < axes_.size(); x++) out += axes_[x].name + "=" + std::to_string(axes[x]) + "\n";
			});
			return out;
		});
	command("movieLine", "a frame's line in the movie, as the movie writes it (Chimera's input log mnemonic); \"\" past the "
		"movie's end", {"frame"}, [this](const Args &a, std::string &error) -> Value {
			int64_t frame = 0;
			if (!arg(a, 0, frame, error)) return {};
			if (frame < 0 || frame >= inputs_.size()) return std::string();
			const std::vector<std::string> one = inputs_.lines(frame, frame);
			return one.empty() ? std::string() : one.front();
		});
	command("countingRerecords", "whether the movie counts rerecords (Chimera's movie.getrerecordcounting)", {},
		[this](const Args &, std::string &) -> Value { return inputs_.countingRerecords(); });
	command("setCountingRerecords", "whether the movie counts rerecords from now on, until another machine is opened "
		"(Chimera's movie.setrerecordcounting)", {"on"}, [this](const Args &a, std::string &error) -> Value {
			bool on = true;
			if (!arg(a, 0, on, error)) return {};
			inputs_.setCountingRerecords(on);
			return {};
		});
	command("setRerecords", "the movie's rerecord count, made this (Chimera's movie.setrerecordcount)", {"count"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t count = 0;
			if (!arg(a, 0, count, error)) return {};
			if (count < 0)
			{
				error = tr("session.rerecords_negative");
				return {};
			}
			onMachineWait([&](ce_session *s) {
				if (s != nullptr) ce_session_set_rerecords(s, static_cast<uint64_t>(count));
			});
			rerecords.set(count);
			return {};
		});
	command("setFutureFrames", "the most frames played ahead after each frame for a script's show_future (0: none)",
		{"most"}, [this](const Args &a, std::string &error) -> Value {
			int64_t most = 0;
			if (!arg(a, 0, most, error)) return {};
			futureMaxM_.store(std::clamp<int64_t>(most, 0, 32767));
			return {};
		});
	command("setLagCount", "the lag count, made this from the frame the machine is on; the lag frames after it add to it, "
		"as Chimera's emu.setlagcount; the machine going back before that frame counts afresh", {"count"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t count = 0;
			if (!arg(a, 0, count, error)) return {};
			const int64_t f = frame.get();
			updateOverlay(f, -1);
			lagOffset_ = count - lags_;
			lagOffsetFrom_ = f;
			lagCount.set(count);
			return {};
		});
	command("axisList", "the machine's analog controls, a line each", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		for (const auto &x : axes_) out += x.name + "\n";
		return out;
	});
	command("hasState", "whether the greenzone holds a state for a frame", {"frame"}, [this](const Args &a, std::string &error) -> Value {
		int64_t f = 0;
		if (!arg(a, 0, f, error)) return {};
		return hasState(f);
	});
	command("lagAt", "whether a frame was a lag frame: 1, 0, or -1 when not known (not run)", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t f = 0;
			if (!arg(a, 0, f, error)) return {};
			return !inputs_.ranAt(f) ? int64_t{-1} : inputs_.lagAt(f) ? int64_t{1} : int64_t{0};
		});
	command("setLag", "a frame's lag flag: 1, 0, or -1 for not known", {"frame", "lag"}, [this](const Args &a, std::string &error) -> Value {
		int64_t f = 0, lag = 0;
		if (!arg(a, 0, f, error) || !arg(a, 1, lag, error)) return {};
		const bool changed = lag < 0 ? inputs_.forgetLag(f) : inputs_.setLag(f, lag > 0);
		if (changed && f < inputs_.size()) inputsChanged.emit(f, f);
		return {};
	});
	command("setIsLagged", "the frame just run made a lag frame or not, as Chimera's emu.setislagged: its row and the "
		"counters say so at once", {"lag"}, [this](const Args &a, std::string &error) -> Value {
			bool lag = true;
			if (!a.empty() && !arg(a, 0, lag, error)) return {};
			const int64_t f = frame.get();
			if (f <= 0) return {};
			if (inputs_.setLag(f - 1, lag))
			{
				if (f - 1 < inputs_.size()) inputsChanged.emit(f - 1, f - 1);
				updateOverlay(f, f - 1);
			}
			return {};
		});
	command("decodeEntry", "a movie line read against this machine's controller, as movieInput says a frame", {"line"},
		[this](const Args &a, std::string &error) -> Value {
			std::string line, out;
			if (!arg(a, 0, line, error)) return {};
			onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				std::vector<int32_t> axes(static_cast<size_t>(ce_session_axis_count(s)));
				Buttons buttons;
				if (!decodeEntry(s, line, buttons, axes.empty() ? nullptr : axes.data())) return;
				for (size_t b = 0; b < buttonNames_.size(); b++)
					if (buttons.test(b)) out += buttonNames_[b] + "\n";
				for (size_t x = 0; x < axes.size() && x < axes_.size(); x++) out += axes_[x].name + "=" + std::to_string(axes[x]) + "\n";
			});
			return out;
		});
	command("buttonList", "the machine's buttons, a line each", {}, [this](const Args &, std::string &) -> Value {
		std::string out;
		for (const std::string &b : buttonNames_) out += b + "\n";
		return out;
	});
	command("frameNow", "the frame the machine is on now, as its thread has it (a script's emu.framecount: the frame "
		"property follows it a moment later)", {}, [this](const Args &, std::string &) -> Value {
			int64_t f = 0;
			onMachineWait([&](ce_session *s) { f = s != nullptr ? frameM_ : 0; });
			return f;
		});
	command("movieLength", "the open project's movie, in frames, as the engine has it now (a script's movie.length)", {},
		[this](const Args &, std::string &) -> Value {
			int64_t n = 0;
			onMachineWait([&](ce_session *s) { n = s != nullptr ? ce_session_movie_length(s) : 0; });
			return n;
		});
	command("borrowed", "the machine is lent to an encode now, until it is given back (Chimera's client.encodingvideo)", {},
		[this](const Args &, std::string &) -> Value { return borrowedM_.load(); });
	command("playedButtons", "the buttons the core was handed on the frame just run (the movie's frame, which the engine plays), "
		"a line each: Chimera's joypad.getwithmovie", {}, [this](const Args &, std::string &) -> Value {
		const Buttons played = playedM_.get();
		std::string out;
		for (size_t b = 0; b < buttonNames_.size(); b++)
			if (played.test(b)) out += buttonNames_[b] + "\n";
		return out;
	});
	command("heldButtons", "the buttons held now (by keys, gamepads and autohold), a line each", {}, [this](const Args &, std::string &) -> Value {
		const Buttons held = held_.get() | autoholdMask_.get() | scriptHeld_.get();
		std::string out;
		for (size_t b = 0; b < buttonNames_.size(); b++)
			if (held.test(b)) out += buttonNames_[b] + "\n";
		return out;
	});
	command("scriptHold", "buttons a script holds for the next frame run, a line each (Chimera's joypad.set)", {"buttons"},
		[this](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			Buttons named;
			std::istringstream in(list);
			/* the machine's own names where a script asks on its thread */
			const std::vector<std::string> &names = onMachineThread() ? buttonNamesM_ : buttonNames_;
			for (std::string name; std::getline(in, name);)
			{
				if (name.empty()) continue;
				const auto it = std::find(names.begin(), names.end(), name);
				if (it == names.end())
				{
					error = tr("session.no_button", {name});
					return {};
				}
				named.set(static_cast<size_t>(it - names.begin()));
			}
			scriptHeld_.set(std::move(named));
			return {};
		});
	command("scriptRelease", "buttons a script lets go for the next frame run, a line each, whoever holds them (Chimera's "
		"joypad.set with false)", {"buttons"}, [this](const Args &a, std::string &error) -> Value {
			std::string list;
			if (!arg(a, 0, list, error)) return {};
			Buttons named;
			std::istringstream in(list);
			const std::vector<std::string> &names = onMachineThread() ? buttonNamesM_ : buttonNames_;
			for (std::string name; std::getline(in, name);)
			{
				if (name.empty()) continue;
				const auto it = std::find(names.begin(), names.end(), name);
				if (it == names.end())
				{
					error = tr("session.no_button", {name});
					return {};
				}
				named.set(static_cast<size_t>(it - names.begin()));
			}
			scriptReleased_.set(std::move(named));
			return {};
		});
	command("scriptAxis", "an analog control's value a script gives: for the next frame run, or held until let go (held; "
		"a value of nothing lets a held one go)", {"name", "value", "held"}, [this](const Args &a, std::string &error) -> Value {
			std::string name;
			if (!arg(a, 0, name, error)) return {};
			const std::vector<Axis> &axes = onMachineThread() ? axesM_ : axes_;
			size_t axis = axes.size();
			for (size_t i = 0; i < axes.size(); i++)
				if (axes[i].name == name) axis = i;
			if (axis == axes.size())
			{
				error = tr("session.no_analog", {name});
				return {};
			}
			bool held = false;
			if (a.size() > 2 && std::holds_alternative<bool>(a[2])) held = std::get<bool>(a[2]);
			std::lock_guard<std::mutex> lock(scriptAxesM_);
			if (a.size() < 2 || std::holds_alternative<std::monostate>(a[1]) ||
				(std::holds_alternative<std::string>(a[1]) && std::get<std::string>(a[1]).empty()))
			{
				scriptAxesHeld_.erase(axis);
				return {};
			}
			int64_t v = 0;
			if (!arg(a, 1, v, error)) return {};
			(held ? scriptAxesHeld_ : scriptAxesOnce_)[axis] = static_cast<int32_t>(v);
			return {};
		});
	command("scriptEntry", "a movie line a script gives (Chimera's joypad.setfrommnemonicstr): its buttons and analog values "
		"for the next frame run", {"line"}, [this](const Args &a, std::string &error) -> Value {
			std::string line;
			if (!arg(a, 0, line, error)) return {};
			/* read from its first '|', as Chimera's (a copied frame number, an indent) */
			line = movieLineFromBar(line);
			bool read = false;
			Buttons buttons;
			std::vector<int32_t> values;
			onMachineWait([&](ce_session *s) {
				if (s == nullptr) return;
				values.resize(static_cast<size_t>(ce_session_axis_count(s)));
				read = decodeEntry(s, line, buttons, values.empty() ? nullptr : values.data());
			});
			if (!read)
			{
				error = tr("session.unreadable_line", {line});
				return {};
			}
			scriptHeld_.set(std::move(buttons));
			std::lock_guard<std::mutex> lock(scriptAxesM_);
			for (size_t i = 0; i < values.size(); i++) scriptAxesOnce_[i] = values[i];
			return {};
		});
	/* and what a script holds for the next frame (joypad.set and its kin), as bots do every frame */
	for (const char *name : {"scriptHold", "scriptRelease", "scriptAxis", "scriptEntry"}) scriptSideCommand(name);
	command("stopMovie", "Chimera's Stop Movie under TAStudio: a new project of the same core, files, settings and "
		"firmware with an empty movie, written nowhere until its first save (the window asks about changes not saved first)",
		{}, [this](const Args &, std::string &) -> Value {
			status.set(tr("session.starting_movie"));
			submit([this] {
				if (projectM_ == nullptr) return;
				ce_project *p = ce_project_new();
				ce_project_set_core(p, ce_project_core_name(projectM_), ce_project_core_version(projectM_), ce_project_core_sha1(projectM_));
				if (const char *platform = ce_project_header_get(projectM_, "Platform"); platform != nullptr && *platform != '\0')
					ce_project_header_set(p, "Platform", platform);
				const char *error = nullptr;
				std::string why;
				for (int32_t i = 0; i < ce_project_file_count(projectM_) && why.empty(); i++)
				{
					/* a cue's own files come back with it */
					const std::string slot = ce_project_file_slot(projectM_, i);
					if (slot == "support") continue;
					const char *source = ce_project_file_source_path(projectM_, i);
					const char *name = ce_project_file_name(projectM_, i);
					if (source == nullptr || *source == '\0' || ce_project_file_add(p, name, slot.c_str(), source, &error) != 0)
						why = tr("session.named_reason", {name, error != nullptr ? error : tr("session.file_moved")});
				}
				uint64_t len = 0;
				if (const char *t = ce_project_settings_text(projectM_, &len); why.empty() && t != nullptr && len != 0)
					ce_project_set_settings_text(p, std::string(t, static_cast<size_t>(len)).c_str(), &error);
				if (const char *t = ce_project_firmware_text(projectM_, &len); why.empty() && t != nullptr && len != 0)
					ce_project_set_firmware_text(p, std::string(t, static_cast<size_t>(len)).c_str(), &error);
				ce_project_set_clock_start(p, ce_project_clock_start(projectM_));
				ce_project_set_title(p, ce_project_title(projectM_));
				if (!why.empty())
				{
					ce_project_free(p);
					later([this, why] { status.set(tr("session.no_new_movie", {why})); });
					return;
				}
				openMadeProjectOnMachine(p, package_);
			});
			return {};
		}, &hasProject);
	command("openProject", "opens a Chimera project; its files are looked for beside it, its core among the installed "
		"ones unless a package is given", {"project", "package"}, [this](const Args &a, std::string &error) -> Value {
			std::string project, package;
			if (!arg(a, 0, project, error)) return {};
			if (a.size() > 1 && !arg(a, 1, package, error)) return {};
			/* false at once for a file that is no project, as Chimera's
			 * client.openproject says; the open itself goes on after */
			const char *unreadable = nullptr;
			ce_project *probe = ce_project_open(project.c_str(), &unreadable);
			if (probe == nullptr)
			{
				status.set(tr("session.could_not_open", {project, unreadable != nullptr ? unreadable : tr("common.no_reason")}));
				/* a failed open, as one the machine refuses: the window says why in a box */
				openFailures.set(openFailures.get() + 1);
				return false;
			}
			ce_project_free(probe);
			status.set(tr("session.opening_project", {project}));
			submit([this, project, package] { openProjectOnMachine(project, package, false); });
			return true;
		});
	command("openProjectForScript", "opens a Chimera project for a script, as Chimera's client.openproject: at once (the "
		"script's next line sees it open), and the scripts go on with it; false for one that does not open", {"project"},
		[this](const Args &a, std::string &error) -> Value {
			std::string project;
			if (!arg(a, 0, project, error)) return {};
			const char *unreadable = nullptr;
			ce_project *probe = ce_project_open(project.c_str(), &unreadable);
			if (probe == nullptr)
			{
				status.set(tr("session.could_not_open", {project, unreadable != nullptr ? unreadable : tr("common.no_reason")}));
				/* a failed open, as one the machine refuses: the window says why in a box */
				openFailures.set(openFailures.get() + 1);
				return false;
			}
			ce_project_free(probe);
			bool opened = false;
			waitOnMachine([&] {
				scriptRebootM_ = true;
				openProjectOnMachine(project, "", false);
				opened = s_ != nullptr && projectM_ != nullptr;
				if (!opened) scriptRebootM_ = false;
			});
			return opened;
		});
	command("saveProject", "saves the open project with its inputs, to its own file or to the one given",
		{"path"}, [this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!a.empty() && !arg(a, 0, path, error)) return {};
			if (!hasProject.get())
			{
				error = tr("session.no_project");
				return {};
			}
			if (path.empty() && projectPath.get().empty())
			{
				error = tr("session.say_where");
				return {};
			}
			saveTo(path);
			/* "When manually saving, also save .backup" */
			if (backupOnSave.get() && !backupFolder.get().empty())
			{
				const std::string backup = backupPathFor(path.empty() ? projectPath.get() : path);
				submit([this, backup] { saveProjectOnMachine(backup, true); });
			}
			return {};
		}, &isOpen);
	command("catchPad", "waits for the next gamepad input pressed, a game open or not, and puts its name in caughtPad",
		{}, [this](const Args &, std::string &) -> Value {
			caughtPad.set("");
			catchingPad.set(true);
			submit([this] {
				if (!padsM_) padsM_ = std::make_unique<Gamepads>();
				/* what is held already is not what is pressed */
				catchHeldM_.clear();
				padsM_->poll(catchHeldM_);
				catchingAxisM_ = false;
				catchingM_ = true;
				catchDue_ = Clock::now() + std::chrono::milliseconds(16);
			});
			return {};
		});
	command("catchAxis", "waits for a gamepad axis to be moved, as Chimera's analog binding does (a stick wiggled), "
		"and puts its name in caughtPad", {}, [this](const Args &, std::string &) -> Value {
			caughtPad.set("");
			catchingPad.set(true);
			submit([this] {
				if (!padsM_) padsM_ = std::make_unique<Gamepads>();
				std::vector<std::string> held;
				catchAxesFromM_.clear();
				padsM_->poll(held, &catchAxesFromM_);
				catchMovedM_.clear();
				catchingAxisM_ = true;
				catchingM_ = true;
				catchDue_ = Clock::now() + std::chrono::milliseconds(16);
			});
			return {};
		});
	command("stopCatchPad", "stops waiting for a gamepad input or axis", {}, [this](const Args &, std::string &) -> Value {
		catchingPad.set(false);
		submit([this] {
			catchingM_ = false;
			catchingAxisM_ = false;
		});
		return {};
	});
	command("pointer", "where the pointer is over the picture, for an analog control bound to \"WMouse X\" or \"WMouse Y\": "
		"-10000 at the picture's left or top edge to 10000 at its right or bottom", {"x", "y"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t x = 0, y = 0;
			if (!arg(a, 0, x, error) || !arg(a, 1, y, error)) return {};
			pointerXM_.store(static_cast<int>(std::clamp<int64_t>(x, -1000000, 1000000)));
			pointerYM_.store(static_cast<int>(std::clamp<int64_t>(y, -1000000, 1000000)));
			return {};
		});
	command("pointerMoved", "how far the pointer moved, for an analog control bound to \"RMouse X\" or \"RMouse Y\" "
		"(Chimera's relative mouse): added up until the next frame reads it", {"dx", "dy"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t dx = 0, dy = 0;
			if (!arg(a, 0, dx, error) || !arg(a, 1, dy, error)) return {};
			movedXM_.fetch_add(static_cast<int>(std::clamp<int64_t>(dx, -100000, 100000)));
			movedYM_.fetch_add(static_cast<int>(std::clamp<int64_t>(dy, -100000, 100000)));
			return {};
		});
	command("close", "closes the machine", {}, [this](const Args &, std::string &) -> Value {
		/* the freezes go with the game, as Chimera's */
		if (!freezes_.empty())
		{
			freezes_.clear();
			freezesUpdated();
		}
		submit([this] {
			freezesM_.clear();
			freezeGameM_.clear();
			closeOnMachine();
		});
		return {};
	}, &isOpen);
	command("play", "runs the machine at its own speed", {}, [this](const Args &, std::string &) -> Value {
		submit([this] {
			if (s_ == nullptr) return;
			endTurboSeekOnMachine();
			runningM_ = true;
			/* played on, a seek that plays is over: the machine runs on past it */
			pauseOnFrameM_ = -1;
			due_ = fpsSince_ = Clock::now();
			fpsFrames_ = 0;
			later([this] { running.set(true); seekingTo.set(-1); });
		});
		return {};
	}, &isOpen);
	command("pause", "stops the machine after the frame it is on; a seek under way stops where it is (Cancel Seek)", {},
		[this](const Args &, std::string &) -> Value {
		submit([this] {
			endTurboSeekOnMachine();
			runningM_ = false;
			pauseOnFrameM_ = -1;
			later([this] { running.set(false); fps.set(0.0); seekingTo.set(-1); });
		});
		return {};
	}, &isOpen);
	command("frameAdvance", "runs one frame and stops", {}, [this](const Args &, std::string &) -> Value {
		submit([this] {
			if (s_ == nullptr) return;
			endTurboSeekOnMachine();
			runningM_ = false;
			passOnMachine(1, true, false);
			/* Skip Lag Frames: on past lag frames, a few seconds' worth at most */
			steppingM_ = true;
			int32_t lag = advanceOnMachine();
			for (int i = 0; lag > 0 && skipLagM_.load() && i < 600; i++) lag = advanceOnMachine();
			steppingM_ = false;
			later([this] { running.set(false); });
		});
		return {};
	}, &isOpen);
	command("savePicture", "writes the picture now on screen as a TGA file, as chimera-run does", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			Picture::Frame f;
			if (!screen.takeIfNewer(UINT64_MAX, f) || f.width <= 0 || f.height <= 0)
			{
				error = tr("session.no_picture");
				return {};
			}
			/* uncompressed 32-bit, rows bottom-up: the engine's BGRA is what a TGA stores */
			std::vector<uint8_t> out(18 + static_cast<size_t>(f.width) * f.height * 4, 0);
			out[2] = 2;
			out[12] = static_cast<uint8_t>(f.width & 0xFF);
			out[13] = static_cast<uint8_t>((f.width >> 8) & 0xFF);
			out[14] = static_cast<uint8_t>(f.height & 0xFF);
			out[15] = static_cast<uint8_t>((f.height >> 8) & 0xFF);
			out[16] = 32;
			out[17] = 8;
			const size_t row = static_cast<size_t>(f.width) * 4;
			for (int32_t y = 0; y < f.height; y++)
				std::memcpy(&out[18 + static_cast<size_t>(f.height - 1 - y) * row], &f.pixels[static_cast<size_t>(y) * f.width], row);
			std::ofstream file(path, std::ios::binary);
			file.write(reinterpret_cast<const char *>(out.data()), static_cast<std::streamsize>(out.size()));
			if (!file)
			{
				error = tr("session.could_not_write", {path});
				return {};
			}
			return static_cast<int64_t>(f.serial);
		}, &isOpen);
	/* Lua's memorysavestate, as Chimera's: the core's raw state (no movie, no picture), in memory */
	command("saveCoreState", "keeps the core's state in memory; its id", {}, [this](const Args &, std::string &error) -> Value {
		std::string id;
		onMachineWait([&](ce_session *s) {
			if (s == nullptr) return;
			uint64_t len = 0;
			const uint8_t *data = ce_session_save_state(s, &len);
			if (data == nullptr)
			{
				error = tr("session.state_not_saved", {ce_session_last_error(s)});
				return;
			}
			char name[40];
			std::snprintf(name, sizeof name, "%016llx-%llu", static_cast<unsigned long long>(std::random_device{}()) << 32 | std::random_device{}(),
				static_cast<unsigned long long>(++memoryStateCount_));
			id = name;
			memoryStatesM_[id] = MemoryState{std::vector<uint8_t>(data, data + len), ce_session_frame(s)};
		});
		if (id.empty() && error.empty()) error = tr("session.no_machine");
		if (!error.empty()) return {};
		return id;
	}, &isOpen);
	command("loadCoreState", "puts the core back in a state kept in memory, by its id", {"id"}, [this](const Args &a, std::string &error) -> Value {
		std::string id;
		if (!arg(a, 0, id, error)) return {};
		onMachineWait([&](ce_session *s) {
			const auto it = memoryStatesM_.find(id);
			if (s == nullptr || it == memoryStatesM_.end())
			{
				error = tr("session.no_state", {id});
				return;
			}
			/* the machine runs on, or stays paused, as it was: as Chimera's, whose scripts load
			 * one each frame and wait for the next (it was paused here, and such a script waited for ever) */
			if (ce_session_load_state(s, it->second.bytes.data(), it->second.bytes.size()) != 0)
			{
				error = tr("session.state_not_loaded", {ce_session_last_error(s)});
				return;
			}
			/* the engine told which frame of the movie that is, as a branch's state is */
			if (ce_session_movie_at(s, it->second.frame) == 0) frameM_ = it->second.frame;
			report(-1, -1, -1, -1, -1);
		});
		if (!error.empty()) return {};
		running.set(false);
		return true;
	}, &isOpen);
	command("removeState", "forgets a state kept in memory", {"id"}, [this](const Args &a, std::string &error) -> Value {
		std::string id;
		if (!arg(a, 0, id, error)) return {};
		onMachineWait([&](ce_session *) { memoryStatesM_.erase(id); });
		return {};
	});
	command("clearStates", "forgets every state kept in memory", {}, [this](const Args &, std::string &) -> Value {
		onMachineWait([&](ce_session *) { memoryStatesM_.clear(); });
		return {};
	});
	command("seek", "puts the machine on a frame, from the nearest stored state; stops it running", {"frame"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t target = 0;
			if (!arg(a, 0, target, error)) return {};
			if (target < 0)
			{
				error = tr("session.no_frame_before_0");
				return {};
			}
			submit([this, target] { seekAsAskedOnMachine(target); });
			return {};
		}, &isOpen);
	/* the first notch from the frame the machine is on: a target kept from an
	 * earlier hold took the person back there after they had moved on */
	command("wheelSeek", "the wheel turned with the right button held over TAStudio, as Chimera's: a frame back a notch "
		"up, on a notch down. The first notch of a hold goes from the frame the machine is on; the next go on from "
		"where the last was going, which a seek may not have reached yet. The frame it goes to", {"notches", "first"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t notches = 0;
			bool first = true;
			if (!arg(a, 0, notches, error) || !arg(a, 1, first, error)) return {};
			const int64_t from = first || wheelTarget_ < 0 ? frame.get() : wheelTarget_;
			const int64_t target = std::max<int64_t>(0, from - notches);
			wheelTarget_ = target;
			submit([this, target] { seekAsAskedOnMachine(target); });
			return target;
		}, &isOpen);
	command("seekToRestore", "goes back to the frame the first of a run of edits found the machine on (Chimera's green arrow)",
		{}, [this](const Args &, std::string &) -> Value {
			submit([this] {
				if (s_ == nullptr || restoreFrameM_ < 0) return;
				restoreOnMachine(restoreFrameM_);
			});
			return {};
		}, &isOpen);
	command("restart", "opens the same core and game again at frame 0; the inputs stay", {},
		[this](const Args &, std::string &) -> Value {
			submit([this] {
				/* the project opens again as it is in memory, its movie and
				 * markers as they are now */
				if (projectM_ != nullptr) openProjectOnMachine(projectPathM_, package_, true, projectM_);
			});
			return {};
		}, &isOpen);
	command("inputPollsReported", "the core open reports each read of its controls, so a script's event.oninputpoll is called "
		"part way through a frame (issue 344)", {}, [this](const Args &, std::string &) -> Value {
			bool reported = false;
			onMachineWait([&](ce_session *s) { reported = s != nullptr && ce_session_input_polls_reported(s) != 0; });
			return reported;
		});
	command("setInputPolls", "the core asked to call the scripts' event.oninputpoll functions at its reads (true) or not", {"on"},
		[this](const Args &a, std::string &error) -> Value {
			bool on = false;
			if (!arg(a, 0, on, error)) return {};
			inputPollsWantedM_ = on;
			/* not waited for: a script stopped as Chimera closes takes its function off a core already gone */
			submit([this] { applyInputPollsOnMachine(); });
			return {};
		});
	command("rebootForScript", "opens the same core and game again at frame 0 for a script, as Chimera's client.reboot_core: "
		"at once (the script's next line sees frame 0), and the scripts go on rather than start again", {},
		[this](const Args &, std::string &) -> Value {
			waitOnMachine([this] {
				if (projectM_ == nullptr) return;
				scriptRebootM_ = true;
				openProjectOnMachine(projectPathM_, package_, true, projectM_);
				/* one that did not open opens nothing the scripts could go on with */
				if (s_ == nullptr) scriptRebootM_ = false;
			});
			return {};
		}, &isOpen);
	command("key", "a key went down (true) or up (false), by Chimera's key name", {"name", "down"},
		[this](const Args &a, std::string &error) -> Value {
			std::string name;
			bool down = false;
			if (!arg(a, 0, name, error) || !arg(a, 1, down, error)) return {};
			key(name, down);
			return {};
		});

	thread_ = std::thread([this] { loop(); });
	{
		std::lock_guard<std::mutex> lock(m_);
		machineThread_ = thread_.get_id();
	}
}

SessionViewModel::~SessionViewModel()
{
	{
		std::lock_guard<std::mutex> lock(m_);
		quit_ = true;
	}
	cv_.notify_all();
	thread_.join();
}

void SessionViewModel::armAutohold(bool down)
{
	armed_ = down ? 1 : (armed_ == 1 ? 0 : armed_.load());
	stickyArmed.set(armed_ != 0);
}

void SessionViewModel::armAutofire(bool down)
{
	armed_ = down ? 2 : (armed_ == 2 ? 0 : armed_.load());
	stickyArmed.set(armed_ != 0);
}

void SessionViewModel::toggleSticky(const Buttons &pressed)
{
	/* any thread: armed, the buttons just pressed change over */
	const int armed = armed_.load();
	if (armed == 0 || !pressed.any()) return;
	if (armed == 1) autoholdMask_.toggle(pressed);
	else
	{
		autofireMask_.toggle(pressed);
		submit([this, pressed] {
			pressed.each([this](size_t b) {
				if (fireAtM_.size() <= b) fireAtM_.resize(b + 1, 0);
				fireAtM_[b] = 0;
			});
		});
	}
}

void SessionViewModel::stickyChanged()
{
	const Buttons autoheldNow = autoholdMask_.get(), autofiredNow = autofireMask_.get();
	auto names = [this](const Buttons &buttons) {
		std::string out;
		for (size_t b = 0; b < buttonNames_.size(); b++)
			if (buttons.test(b)) out += (out.empty() ? "" : " ") + buttonNames_[b];
		return out;
	};
	autoheld.set(names(autoheldNow));
	autofired.set(names(autofiredNow));
	std::string held;
	for (size_t b = 0; b < buttonNames_.size(); b++)
		if (autoheldNow.test(b)) held += buttonNames_[b] + " ";
	for (size_t b = 0; b < buttonNames_.size(); b++)
		if (autofiredNow.test(b)) held += tr("session.autofired_button", {buttonNames_[b]}) + " ";
	std::string text = tr("session.held", {held});
	stickyText.set(text);
}

void SessionViewModel::key(const std::string &name, bool down)
{
	bool &was = keysDown_[name];
	if (was == down) return;
	was = down;
	/* armed: the buttons this key presses change over (and are pressed as well) */
	if (down && armed_ != 0)
	{
		Buttons pressed;
		for (int b : bindings_.buttonsOf(name))
			if (b >= 0) pressed.set(static_cast<size_t>(b));
		toggleSticky(pressed);
		stickyChanged();
	}
	Buttons held;
	std::vector<std::string> keys;
	for (const auto &[k, isDown] : keysDown_)
		if (isDown)
		{
			keys.push_back(k);
			for (int b : bindings_.buttonsOf(k))
				if (b >= 0) held.set(static_cast<size_t>(b));
		}
	held_.set(std::move(held));
	/* the keys held, for the autofire keys and the analog controls' keys, read by the machine's thread */
	std::lock_guard<std::mutex> lock(keysHeldM_);
	keysHeld_ = std::move(keys);
}

void SessionViewModel::setAutofireTexts(std::vector<std::string> texts)
{
	texts.resize(buttonNames_.size());
	autofireBindings_.setTexts(texts);
	submit([this, texts] { autofireBindingsM_.setTexts(texts); });
}

void SessionViewModel::setAnalogBinds(std::vector<AnalogBind> binds)
{
	binds.resize(axes_.size());
	analogBinds_ = binds;
	mouseBound.set(std::any_of(binds.begin(), binds.end(), [](const AnalogBind &b) {
		return b.value == "WMouse X" || b.value == "WMouse Y" || b.value == "RMouse X" || b.value == "RMouse Y";
	}));
	submit([this, binds] { analogBindsM_ = binds; });
}

void SessionViewModel::setMouseSensitivity(int64_t percent)
{
	mouseSensitivityM_.store(static_cast<int>(std::clamp<int64_t>(percent, 1, 1000)));
}

std::vector<std::pair<std::string, int>> SessionViewModel::padAxesNow() const
{
	std::lock_guard<std::mutex> lock(padHeldCopyM_);
	return padAxesCopy_;
}

void SessionViewModel::holdFrameAdvance(bool down)
{
	submit([this, down] {
		if (s_ == nullptr) return;
		if (down)
		{
			endTurboSeekOnMachine();
			runningM_ = false;
			/* Skip Lag Frames: on past lag frames, a few seconds' worth at most */
			steppingM_ = true;
			int32_t lag = advanceOnMachine();
			for (int i = 0; lag > 0 && skipLagM_.load() && i < 600; i++) lag = advanceOnMachine();
			steppingM_ = false;
			later([this] { running.set(false); });
			holdM_ = Hold::Advance;
			holdSince_ = Clock::now();
			holdRunning_ = false;
		}
		else if (holdM_ == Hold::Advance)
		{
			holdM_ = Hold::None;
			if (holdRunning_)
			{
				runningM_ = false;
				holdRunning_ = false;
				later([this] { running.set(false); });
			}
		}
	});
}

void SessionViewModel::holdRewind(bool down)
{
	rewindHeld_ = down;
	rewinding.set(down);
	submit([this, down] {
		if (s_ == nullptr) return;
		if (down)
		{
			endTurboSeekOnMachine();
			runningM_ = false;
			later([this] { running.set(false); });
			if (frameM_ > 0) seekOnMachine(std::max<int64_t>(0, frameM_ - (fastM_ || turboM_ ? rewindStepFastM_.load() : rewindStepM_.load())));
			holdM_ = Hold::Rewind;
			/* the first step back at once, the next after a moment */
			due_ = Clock::now() + std::chrono::milliseconds(500);
		}
		else if (holdM_ == Hold::Rewind)
			holdM_ = Hold::None;
	});
}

void SessionViewModel::holdTurbo(bool down)
{
	turboHeld_ = down;
	turbo.set(down);
	turboM_ = down;
	submit([this, down] {
		if (!down) due_ = Clock::now();
	});
}

void SessionViewModel::holdFastForward(bool down)
{
	fastHeld_ = down;
	submit([this, down] {
		fastM_ = down;
		if (!down) due_ = Clock::now();
	});
}

void SessionViewModel::setBindingTexts(std::vector<std::string> texts)
{
	texts.resize(buttonNames_.size());
	bindings_.setTexts(texts);
	/* the keys held now hold what they hold now */
	Buttons held;
	for (const auto &[k, isDown] : keysDown_)
		if (isDown)
			for (int b : bindings_.buttonsOf(k))
				if (b >= 0) held.set(static_cast<size_t>(b));
	held_.set(std::move(held));
	submit([this, texts] { bindingsM_.setTexts(texts); });
}

void SessionViewModel::submit(std::function<void()> job)
{
	{
		std::lock_guard<std::mutex> lock(m_);
		jobs_.push_back({false, std::move(job)});
	}
	cv_.notify_all();
}

void SessionViewModel::loop()
{
	for (;;)
	{
		/* the MCP server's calls, between frames */
		mcpOnMachine();
		std::function<void()> job;
		bool advance = false, runOn = false, back = false, catchPoll = false, autosaveTick = false, keepTick = false,
			journalTick = false, idleTick = false, turboPiece = false;
		/* the scripts' turns when no frame is run, while there are scripts */
		const bool idleScripts = (s_ == nullptr || !runningM_) && scriptsIdleM_ && scriptsWantedM_ && scriptsWantedM_();
		if (!idleScripts) idleDue_ = {};
		else if (idleDue_ == Clock::time_point{})
			idleDue_ = Clock::now() + std::chrono::microseconds(16667);
		{
			std::unique_lock<std::mutex> lock(m_);
			for (;;)
			{
				if (quit_) break;
				if (!jobs_.empty())
				{
					job = std::move(jobs_.front().run);
					jobs_.pop_front();
					break;
				}
				/* the autosave's clock: every so many seconds while a project is open */
				const int64_t autoMs = autosaveMsM_.load();
				const bool autoOn = autoMs > 0 && projectM_ != nullptr;
				if (autoOn && autosaveDue_ == Clock::time_point{}) autosaveDue_ = Clock::now() + std::chrono::milliseconds(autoMs);
				if (autoOn && Clock::now() >= autosaveDue_)
				{
					autosaveTick = true;
					break;
				}
				/* the work kept against a crash: every five seconds while the
				 * project has changes not saved, the first at once */
				const bool keepOn = projectM_ != nullptr && unsavedM_.load() && !recoveryFolderM_.empty() && !projectPathM_.empty();
				if (!keepOn) recoveryDue_ = {};
				if (keepOn && recoveryDue_ == Clock::time_point{}) recoveryDue_ = Clock::now();
				if (keepOn && Clock::now() >= recoveryDue_)
				{
					keepTick = true;
					break;
				}
				/* and its journal, from the moment it is kept: again at once for a
				 * machine started afresh (its movie is a new one), and a second
				 * after a journal that could not be written */
				if (keepOn && s_ != nullptr && Clock::now() >= journalRetryDue_ && ce_session_movie_journaling(s_) == 0)
				{
					journalTick = true;
					break;
				}
				if (idleScripts && Clock::now() >= idleDue_)
				{
					idleTick = true;
					break;
				}
				/* a started MCP server is looked at sixty times a second */
				const bool mcpOn = mcpOnM_.load();
				const bool timed = autoOn || keepOn || mcpOn || idleScripts;
				Clock::time_point wake = autoOn && keepOn ? std::min(autosaveDue_, recoveryDue_) : autoOn ? autosaveDue_ : recoveryDue_;
				if (mcpOn) wake = (autoOn || keepOn) ? std::min(wake, Clock::now() + std::chrono::milliseconds(16)) : Clock::now() + std::chrono::milliseconds(16);
				if (idleScripts) wake = (autoOn || keepOn || mcpOn) ? std::min(wake, idleDue_) : idleDue_;
				/* a call waiting (not one the window is answering), or a move of the agent's that has stopped */
				if (mcpOn && !mcpBusyM_ && (ce_mcp_pending() != 0 || (mcpMoveM_ && !(runningM_ && pauseOnFrameM_ >= 0) && turboSeekToM_ < 0))) break;
				/* a turbo seek going in pieces: the next piece, once any job waiting has run */
				if (turboSeekToM_ >= 0 && s_ != nullptr)
				{
					turboPiece = true;
					break;
				}
				auto waitUntil = [&](Clock::time_point t) { cv_.wait_until(lock, timed ? std::min(t, wake) : t); };
				/* a binding waiting for a gamepad input: the pads looked at
				 * sixty times a second, a game open or not */
				if (catchingM_ && Clock::now() >= catchDue_)
				{
					catchPoll = true;
					break;
				}
				/* frame advance held half a second runs on */
				if (s_ != nullptr && holdM_ == Hold::Advance && !runningM_)
				{
					const auto when = holdSince_ + std::chrono::milliseconds(500);
					if (Clock::now() >= when) { runOn = true; break; }
					waitUntil(when);
					continue;
				}
				/* rewind held goes back a frame a frame */
				if (s_ != nullptr && holdM_ == Hold::Rewind)
				{
					if (Clock::now() >= due_) { back = true; break; }
					waitUntil(due_);
					continue;
				}
				if (runningM_ && s_ != nullptr && !holdForScriptM_.load())
				{
					/* the vsync throttle: a frame each picture the window has shown since the last */
					if (throttleM_.load() == 2 && !unthrottledM_ && !turboM_ && !fastM_ && !mcpFastM_)
					{
						if (vsyncTicks_.load() != vsyncSeen_) { vsyncSeen_ = vsyncTicks_.load(); advance = true; break; }
						waitUntil(Clock::now() + std::chrono::milliseconds(catchingM_ ? 16 : 50));
						continue;
					}
					if (Clock::now() >= due_) { advance = true; break; }
					waitUntil(catchingM_ ? std::min(due_, catchDue_) : due_);
				}
				else
				{
					/* not running: the time until it runs again is not run time */
					pausedSincePassM_ = true;
					if (catchingM_)
						waitUntil(catchDue_);
					else if (timed)
						cv_.wait_until(lock, wake);
					else
						cv_.wait(lock);
				}
			}
			if (quit_) break;
		}
		if (job)
		{
			job();
			continue;
		}
		if (catchPoll)
		{
			catchPollOnMachine();
			continue;
		}
		if (turboPiece)
		{
			turboSeekPieceOnMachine();
			continue;
		}
		if (idleTick)
		{
			/* about sixty a second, not more to catch up after a long job */
			idleDue_ = std::max(idleDue_ + std::chrono::microseconds(16667), Clock::now());
			if (s_ == nullptr && runningM_)
			{
				scriptTurnOnMachine(scriptsBeforeM_);
				scriptTurnOnMachine(scriptsAfterM_);
			}
			else if (scriptsNowOnMachine())
				scriptsIdleM_();
			continue;
		}
		if (keepTick)
		{
			keepRecoveryOnMachine();
			recoveryDue_ = Clock::now() + std::chrono::seconds(5);
			continue;
		}
		if (journalTick)
		{
			journalOnMachine();
			continue;
		}
		if (autosaveTick)
		{
			autosaveDue_ = Clock::now() + std::chrono::milliseconds(autosaveMsM_.load());
			later([this] { autosave(); });
			continue;
		}
		if (runOn)
		{
			runningM_ = true;
			holdRunning_ = true;
			due_ = Clock::now();
			later([this] { running.set(true); });
			continue;
		}
		if (back)
		{
			passOnMachine(0, false, true);
			/* TAStudio's rewind step, and its fast one while fast forwarding */
			if (frameM_ > 0) seekOnMachine(std::max<int64_t>(0, frameM_ - (fastM_ || turboM_ ? rewindStepFastM_.load() : rewindStepM_.load())));
			due_ += period_;
			if (Clock::now() - due_ > 4 * period_) due_ = Clock::now();
			continue;
		}
		if (advance)
		{
			passOnMachine(1, false, false);
			advanceOnMachine();
			/* turbo or unthrottled: the next frame at once; fast forward at
			 * its own speed; else at the speed asked for */
			const int64_t percent = fastM_ ? fastSpeedM_.load() : speedM_.load();
			const int mode = throttleM_.load();
			if (turboM_ || unthrottledM_ || mode == 3 || mcpFastM_)
				due_ += Clock::duration::zero();
			else if (mode == 1 && soundM_ && soundM_->available() && soundOnM_ && !fastM_)
			{
				/* the audio throttle: the next frame when the sound queued has played down to half the buffer */
				const int64_t ahead = soundM_->ahead(), target = soundBufferMsM_ * Sound::kRate / 2000;
				due_ = Clock::now() + std::chrono::duration_cast<Clock::duration>(
					std::chrono::duration<double>(static_cast<double>(std::max<int64_t>(0, ahead - target)) / Sound::kRate));
			}
			else
				due_ += std::chrono::duration_cast<Clock::duration>(period_ * (100.0 / static_cast<double>(percent)));
			/* behind by more than a few frames (a slow frame, a busy machine):
			 * carry on from now rather than race to catch up */
			const auto now = Clock::now();
			if (now - due_ > 4 * period_) due_ = now;
		}
	}
	freeSessionOnMachine();
	freeProjectOnMachine();
	padsM_.reset(); /* SDL stops on the thread that started it */
	if (averageM_ != nullptr) ce_average_fps_destroy(averageM_);
	averageM_ = nullptr;
	soundM_.reset();
}

void SessionViewModel::freeSessionOnMachine()
{
	if (s_ == nullptr) return;
	/* a turbo seek under way was this machine's */
	turboSeekToM_ = -1;
	for (const Hooks &h : hooksM_)
		if (h.closing) h.closing(s_);
	inputs_.bind(nullptr);
	/* a greenzone being written is finished before its session goes */
	ce_session_history_save_wait(s_);
	ce_session_free(s_);
	s_ = nullptr;
	if (!runningMarkM_.empty())
	{
		std::error_code ec;
		std::filesystem::remove(runningMarkM_, ec);
		runningMarkM_.clear();
	}
}

void SessionViewModel::onMachine(MachineJob job)
{
	submit([this, job = std::move(job)] { job(s_); });
}

std::string SessionViewModel::settingsOnMachine() const
{
	if (projectM_ != nullptr)
	{
		uint64_t len = 0;
		const char *t = ce_project_settings_text(projectM_, &len);
		return t != nullptr ? std::string(t, static_cast<size_t>(len)) : std::string("{}");
	}
	return "{}";
}

std::string SessionViewModel::slotsOnMachine() const
{
	if (projectM_ == nullptr) return "{}";
	uint64_t len = 0;
	const char *t = ce_project_slots_text(projectM_, &len);
	return t != nullptr ? std::string(t, static_cast<size_t>(len)) : std::string("{}");
}

void SessionViewModel::changeSettings(std::string json, std::function<void(bool, std::string)> done)
{
	submit([this, json, done] {
		auto answer = [this, done](bool ok, std::string what) { later([done, ok, what] { done(ok, what); }); };
		if (s_ == nullptr)
		{
			answer(false, tr("session.no_machine"));
			return;
		}
		const int32_t live = ce_session_apply_settings(s_, json.c_str());
		if (live == 2)
		{
			answer(false, tr("session.core_refused_settings", {ce_session_last_error(s_)}));
			return;
		}
		/* kept in the project for the next open */
		if (projectM_ != nullptr)
		{
			const char *error = nullptr;
			if (ce_project_set_settings_text(projectM_, json.c_str(), &error) != 0)
			{
				answer(false, tr("session.project_refused_settings", {error != nullptr ? error : tr("common.no_reason")}));
				return;
			}
		}
		if (live == 0)
		{
			answer(true, tr("session.settings_applied"));
			return;
		}
		/* the core takes them only at start: open it again with them */
		runningM_ = false;
		later([this] { running.set(false); });
		if (projectM_ != nullptr) openProjectOnMachine(projectPathM_, package_, true, projectM_);
		answer(s_ != nullptr, s_ != nullptr ? tr("session.settings_restarted")
											: tr("session.settings_not_restarted"));
	});
}

void SessionViewModel::changeProjectSettings(std::string json, int64_t clockStart, std::function<void(bool)> done)
{
	submit([this, json, clockStart, done] {
		if (projectM_ == nullptr || s_ == nullptr)
		{
			later([done] { done(false); });
			return;
		}
		const std::string before = settingsOnMachine();
		const int64_t clockBefore = clockOnMachine();
		auto boot = [this](const std::string &settings, int64_t clock) {
			const char *error = nullptr;
			if (ce_project_set_settings_text(projectM_, settings.c_str(), &error) != 0 || ce_project_set_clock_start(projectM_, clock) != 0)
				return false;
			/* as Reboot Core in a project: opened again as it is in memory */
			runningM_ = false;
			later([this] { running.set(false); });
			openProjectOnMachine(projectPathM_, package_, true, projectM_);
			return s_ != nullptr && projectM_ != nullptr;
		};
		const bool ok = boot(json, clockStart);
		if (!ok && projectM_ != nullptr) boot(before, clockBefore);
		/* changed, the project is unsaved: a save writes what it now boots with */
		later([this, done, ok] {
			if (ok) markChanged();
			done(ok);
		});
	});
}

int64_t SessionViewModel::clockOnMachine() const
{
	const int64_t clock = projectM_ != nullptr ? ce_project_clock_start(projectM_) : -1;
	return clock < 0 ? CE_CLOCK_START_DEFAULT : clock;
}

void SessionViewModel::seekAsAskedOnMachine(int64_t target)
{
	restoreToM_ = -1;
	turboSeekToM_ = -1;
	runningM_ = false;
	pauseOnFrameM_ = -1;
	mcpFastM_ = false;
	later([this] { running.set(false); fps.set(0.0); seekingTo.set(-1); });
	/* Turbo seek off, as Chimera's: what the greenzone holds is gone to at
	 * once (from its state at or before the frame), and the rest is played at
	 * the machine's own speed, every frame drawn */
	if (!turboSeekM_.load() && s_ != nullptr)
	{
		const int64_t from = ce_session_frame(s_);
		const int64_t stored = target > 0 ? ce_session_greenzone_nearest(s_, target - 1) : 0;
		if (stored < target - 1)
		{
			if (target < from || stored > from) seekOnMachine(std::max<int64_t>(stored, 0));
			const int64_t at = ce_session_frame(s_);
			if (at == target) return;
			if (at < target)
			{
				pauseOnFrameM_ = playedSeekToM_ = target;
				runningM_ = true;
				due_ = fpsSince_ = Clock::now();
				fpsFrames_ = 0;
				later([this, at, target] { seekFrom.set(at); seekingTo.set(target); running.set(true); });
				return;
			}
		}
	}
	/* Turbo seek on: a long way to go is gone in pieces, between which the
	 * machine's thread does what it is asked - Cancel Seek stops it where it
	 * is, as Chimera's turbo seek can be stopped part way */
	if (turboSeekM_.load() && s_ != nullptr && target > 0 && target <= ce_session_movie_length(s_))
	{
		const int64_t from = ce_session_frame(s_);
		const int64_t stored = ce_session_greenzone_nearest(s_, target - 1);
		const int64_t start = stored >= 0 && (stored > from || from > target - 1) ? stored : from;
		if (target - 1 - start > kTurboPieceFrames)
		{
			turboSeekToM_ = target;
			turboPiece_ = kTurboPieceFrames;
			turboFrom_ = start;
			turboBegan_ = Clock::now();
			later([this, from, target] { seekFrom.set(from); seekingTo.set(target); });
			return;
		}
	}
	seekOnMachine(target);
}

void SessionViewModel::restoreOnMachine(int64_t target)
{
	/* a movie made shorter is followed to its end: going back adds no frames */
	target = std::min(target, ce_session_movie_length(s_));
	/* as Chimera's TAStudio: the machine paused first ("restore makes no
	 * sense without pausing"), then the seek */
	restoreToM_ = -1;
	turboSeekToM_ = -1;
	runningM_ = false;
	pauseOnFrameM_ = -1;
	mcpFastM_ = false;
	later([this] { running.set(false); fps.set(0.0); seekingTo.set(-1); });
	/* where it starts from: the nearest stored state before the target, and
	 * before the edit while the machine is past it. All the greenzone holds
	 * is gone at once */
	const int64_t from = ce_session_frame(s_);
	const int64_t stored = target > 0 ? ce_session_greenzone_nearest(s_, greenEndM_ < from ? std::min(target - 1, greenEndM_) : target - 1) : 0;
	/* with Turbo seek, a way no longer than a piece is gone at once, as any
	 * seek of the machine's own; a longer one, and without Turbo seek any,
	 * goes back to that state now (a short seek: the edit honoured), then the
	 * rest */
	if (stored < target - 1 && (!turboSeekM_.load() || target - 1 - std::max<int64_t>(stored, 0) > kTurboPieceFrames))
	{
		seekOnMachine(std::max<int64_t>(stored, 0), false);
		const int64_t at = ce_session_frame(s_);
		if (!turboSeekM_.load() && at < target)
		{
			/* Turbo seek off: played at the machine's own speed, every frame drawn */
			restoreToM_ = pauseOnFrameM_ = playedSeekToM_ = target;
			runningM_ = true;
			due_ = fpsSince_ = Clock::now();
			fpsFrames_ = 0;
			later([this, at, target] { seekFrom.set(at); seekingTo.set(target); running.set(true); });
			return;
		}
		if (turboSeekM_.load() && target - 1 - at > kTurboPieceFrames)
		{
			/* Turbo seek on: in pieces, between which the window is answered */
			restoreToM_ = turboSeekToM_ = target;
			turboPiece_ = kTurboPieceFrames;
			turboFrom_ = at;
			turboBegan_ = Clock::now();
			later([this, at, target] { seekFrom.set(at); seekingTo.set(target); });
			return;
		}
	}
	seekOnMachine(target, false);
	restoredOnMachine();
}

void SessionViewModel::restoredOnMachine()
{
	restoreToM_ = -1;
	restoredMovesM_ = movesM_;
	restoredAtM_ = frameM_;
}

void SessionViewModel::turboSeekPieceOnMachine()
{
	const int64_t target = turboSeekToM_;
	const int64_t at = ce_session_frame(s_);
	const int64_t next = std::min(target - 1, std::max<int64_t>(at, 0) + turboPiece_);
	if (next <= at || at >= target - 1)
	{
		/* there: the last frame run with its picture, the scripts' turn, as any seek lands;
		 * what it ran and how long it took, the whole seek's. A way back after
		 * an edit adds no frames to the movie */
		const bool restoring = restoringOnMachine();
		turboSeekToM_ = -1;
		seekOnMachine(target, !restoring);
		if (restoring) restoredOnMachine();
		const int64_t ran = target - turboFrom_;
		const double ms = std::chrono::duration<double, std::milli>(Clock::now() - turboBegan_).count();
		later([this, ran, ms] {
			seekingTo.set(-1);
			lastSeekFrames.set(ran);
			lastSeekMs.set(ms);
		});
		return;
	}
	const auto began = Clock::now();
	if (ce_session_seek(s_, next) != 0)
	{
		/* the window edited the movie under it, or it cannot go on: it lands where it is
		 * (a way back after an edit as if there, so the edit's turn goes on to its frame) */
		const bool restoring = restoringOnMachine();
		turboSeekToM_ = -1;
		seekOnMachine(std::min<int64_t>(target, ce_session_frame(s_)), !restoring);
		if (restoring) restoredOnMachine();
		later([this] { seekingTo.set(-1); });
		return;
	}
	/* pieces of about a tenth of a second: more frames while they are quick, fewer while slow */
	const double ms = std::chrono::duration<double, std::milli>(Clock::now() - began).count();
	if (ms < 50) turboPiece_ = std::min<int64_t>(turboPiece_ * 2, 1 << 16);
	else if (ms > 200) turboPiece_ = std::max<int64_t>(turboPiece_ / 2, 1);
	frameM_ = ce_session_frame(s_);
	report(-1, -1, -1, -1, -1);
}

void SessionViewModel::endTurboSeekOnMachine()
{
	if (turboSeekToM_ < 0 || s_ == nullptr) return;
	/* stopped part way: on the frame it reached, with its picture */
	turboSeekToM_ = -1;
	seekOnMachine(ce_session_frame(s_));
}

void SessionViewModel::mcpServing(bool on)
{
	mcpOnM_.store(on);
	/* the machine's thread looks for calls from now (or stops looking) */
	submit([this, on] {
		if (on) return;
		mcpBusyM_ = false;
		mcpMoveM_.reset();
		mcpFastM_ = false;
	});
}

void SessionViewModel::mcpAnswerOnMachine(uint64_t id, const std::string &text, bool isError)
{
	ce_mcp_answer(s_, id, text.c_str(), isError ? 1 : 0);
	mcpBusyM_ = false;
}

void SessionViewModel::mcpAnswer(uint64_t id, std::string text, bool isError)
{
	submit([this, id, text, isError] { mcpAnswerOnMachine(id, text, isError); });
}

void SessionViewModel::mcpAnswerRunState(uint64_t id)
{
	submit([this, id] {
		const std::string text = std::string("{\"paused\": ") + (runningM_ ? "false" : "true") +
			", \"frame\": " + std::to_string(s_ != nullptr ? ce_session_frame(s_) : -1) + "}";
		mcpAnswerOnMachine(id, text, false);
	});
}

void SessionViewModel::mcpAnswerOpened(uint64_t id)
{
	submit([this, id] {
		if (s_ == nullptr)
		{
			mcpAnswerOnMachine(id, tr("session.made_not_started"), true);
			return;
		}
		json::Json opened;
		std::string why;
		if (!json::parse(mcpStatusOnMachine(), opened, why) || opened.kind != json::Json::Kind::Object)
		{
			opened = json::Json();
			opened.kind = json::Json::Kind::Object;
		}
		auto text = [](const char *t) { return json::Json::string(t != nullptr ? t : ""); };
		opened.members.emplace_back("core", text(ce_session_core_name(s_)));
		opened.members.emplace_back("system", text(ce_session_system_id(s_)));
		opened.members.emplace_back("frame", json::Json::integer(ce_session_frame(s_)));
		opened.members.emplace_back("movie_length", json::Json::integer(ce_session_movie_length(s_)));
		mcpAnswerOnMachine(id, json::write(opened), false);
	});
}

void SessionViewModel::mcpSeek(uint64_t id, int64_t frame)
{
	submit([this, id, frame] {
		if (s_ == nullptr)
		{
			mcpAnswerOnMachine(id, tr("session.no_project_in_chimera"), true);
			return;
		}
		const int64_t start = ce_session_frame(s_);
		seekAsAskedOnMachine(frame);
		mcpMoveM_ = McpMove{id, start, frame, McpMove::Seek, {}};
	});
}

void SessionViewModel::mcpPlayUntil(uint64_t id, std::string arguments, int64_t most)
{
	submit([this, id, arguments, most] {
		if (s_ == nullptr)
		{
			mcpAnswerOnMachine(id, tr("session.no_project_in_chimera"), true);
			return;
		}
		/* what the condition means is the engine's: it reads the arguments itself */
		const char *why = nullptr;
		if (ce_mcp_condition_set(s_, arguments.c_str(), &why) != 1)
		{
			mcpAnswerOnMachine(id, why != nullptr ? why : tr("session.condition_not_understood"), true);
			return;
		}
		const int64_t start = ce_session_frame(s_), length = ce_session_movie_length(s_);
		if (ce_mcp_condition_met(s_) == 1)
		{
			mcpMoveM_ = McpMove{id, start, start, McpMove::Until, {}};
			mcpMoveOnMachine();
			return;
		}
		if (start >= length)
		{
			mcpAnswerOnMachine(id, tr("session.at_movie_end", {std::to_string(length)}), true);
			return;
		}
		/* played a frame at a time, the condition asked after each; with Turbo
		 * seek as fast as it goes, as Chimera's seek is */
		const int64_t target = std::min(length, start + most);
		pauseOnFrameM_ = target;
		mcpFastM_ = turboSeekM_.load();
		runningM_ = true;
		due_ = fpsSince_ = Clock::now();
		fpsFrames_ = 0;
		later([this, start, target] { seekFrom.set(start); seekingTo.set(target); running.set(true); });
		mcpMoveM_ = McpMove{id, start, target, McpMove::Until, {}};
	});
}

void SessionViewModel::mcpPictures(uint64_t id, std::vector<int64_t> frames)
{
	submit([this, id, frames] {
		if (s_ == nullptr || frames.empty())
		{
			mcpAnswerOnMachine(id, tr("session.no_project_in_chimera"), true);
			return;
		}
		McpMove move{id, ce_session_frame(s_), frames.front(), McpMove::Pictures, {}};
		move.pictures.assign(frames.begin(), frames.end());
		seekAsAskedOnMachine(frames.front());
		mcpMoveM_ = std::move(move);
	});
}

bool SessionViewModel::mcpMoveOnMachine()
{
	if (!mcpMoveM_) return true;
	/* still on its way */
	if ((runningM_ && pauseOnFrameM_ >= 0) || turboSeekToM_ >= 0) return false;
	McpMove &move = *mcpMoveM_;
	const int64_t at = s_ != nullptr ? ce_session_frame(s_) : -1;
	const int64_t length = s_ != nullptr ? ce_session_movie_length(s_) : 0;
	std::string text;
	bool failed = false;
	if (s_ == nullptr)
	{
		text = tr("session.closed_while_moving");
		failed = true;
	}
	else if (move.kind == McpMove::Pictures)
	{
		/* on a frame of the series: its picture, then on to the next */
		const char *why = nullptr;
		if (at != move.pictures.front())
		{
			text = tr("session.pictures_interrupted", {std::to_string(at), std::to_string(move.pictures.front())});
			failed = true;
		}
		else if (ce_mcp_capture(s_, move.id, at, &why) != 1)
		{
			text = tr("session.picture_not_taken", {std::to_string(at), why != nullptr ? why : tr("common.no_reason")});
			failed = true;
		}
		else
		{
			move.pictures.pop_front();
			if (!move.pictures.empty())
			{
				move.target = move.pictures.front();
				seekAsAskedOnMachine(move.target);
				return false;
			}
			text = "{\"frame\": " + std::to_string(at) + ", \"movie_length\": " + std::to_string(length) + "}";
		}
	}
	else if (move.kind == McpMove::Until)
	{
		const bool met = ce_mcp_condition_met(s_) == 1;
		const char *stopped = met ? "condition" : at >= length ? "movie_end" : at >= move.target ? "max_frames" : "interrupted";
		text = std::string("{\"frame\": ") + std::to_string(at) + ", \"met\": " + (met ? "true" : "false") + ", \"frames_played\": " +
			std::to_string(at - move.start) + ", \"stopped_at\": \"" + stopped + "\"}";
	}
	else if (at != move.target)
	{
		text = tr("session.seek_interrupted", {std::to_string(at), std::to_string(move.target)});
		failed = true;
	}
	else
		text = "{\"frame\": " + std::to_string(at) + "}";
	const uint64_t id = move.id;
	mcpMoveM_.reset();
	mcpFastM_ = false;
	mcpAnswerOnMachine(id, text, failed);
	return true;
}

std::string SessionViewModel::mcpStatusOnMachine() const
{
	/* what only the window knows; the engine adds the machine's facts */
	json::Json status;
	status.kind = json::Json::Kind::Object;
	if (s_ == nullptr) return "{}";
	auto add = [&](const char *name, json::Json v) { status.members.emplace_back(name, std::move(v)); };
	const std::string project = projectM_ != nullptr && ce_project_title(projectM_) != nullptr ? ce_project_title(projectM_) : "";
	add("project", json::Json::string(project));
	if (projectM_ != nullptr)
	{
		add("project_file", json::Json::string(projectPathM_));
		add("unsaved_changes", json::Json::boolean_(unsavedM_.load()));
	}
	add("paused", json::Json::boolean_(!runningM_));
	add("measured_frames_per_second", json::Json::number(std::round(lastFpsM_ * 100) / 100));
	add("speed_percent", json::Json::integer(speedM_.load()));
	if (projectM_ != nullptr)
	{
		json::Json files;
		files.kind = json::Json::Kind::Array;
		for (int32_t i = 0; i < ce_project_file_count(projectM_); i++)
		{
			json::Json f;
			f.kind = json::Json::Kind::Object;
			auto text = [](const char *t) { return json::Json::string(t != nullptr ? t : ""); };
			f.members.emplace_back("slot", text(ce_project_file_slot(projectM_, i)));
			f.members.emplace_back("name", text(ce_project_file_name(projectM_, i)));
			f.members.emplace_back("path", text(ce_project_file_source_path(projectM_, i)));
			files.items.push_back(std::move(f));
		}
		add("files", std::move(files));
	}
	/* the firmware the machine was started with, as the project pins it */
	json::Json pins;
	std::string error;
	uint64_t pinsLen = 0;
	const char *pinsText = projectM_ != nullptr ? ce_project_firmware_text(projectM_, &pinsLen) : nullptr;
	if (pinsText == nullptr || !json::parse(std::string_view(pinsText, static_cast<size_t>(pinsLen)), pins, error) || pins.kind != json::Json::Kind::Array)
	{
		pins = json::Json();
		pins.kind = json::Json::Kind::Array;
	}
	add("firmware", std::move(pins));
	return json::write(status);
}

void SessionViewModel::mcpOnMachine()
{
	if (!mcpOnM_.load()) return;
	if (!mcpMoveOnMachine() || mcpBusyM_ || ce_mcp_pending() == 0) return;
	const std::string status = mcpStatusOnMachine();
	uint64_t id = 0;
	const char *tool = nullptr, *arguments = nullptr;
	if (ce_mcp_service(s_, s_ != nullptr ? ce_session_movie_log(s_) : nullptr, -1, status.c_str(), &id, &tool, &arguments) != 1) return;
	/* a call only the window can answer: nothing else runs until it does */
	mcpBusyM_ = true;
	later([this, id, t = std::string(tool != nullptr ? tool : ""), a = std::string(arguments != nullptr ? arguments : "{}")] {
		if (mcpHost_) mcpHost_(id, t, a);
		else mcpAnswer(id, tr("session.window_cannot_be_asked"), true);
	});
}

void SessionViewModel::setMarkers(std::vector<Marker> markers)
{
	submit([this, markers = std::move(markers)] {
		markersM_ = markers;
		pinMarkersOnMachine();
		journalMarksOnMachine();
	});
}

void SessionViewModel::borrowMachine(int64_t from, int64_t to, BorrowFrame frame, std::function<void()> finishing,
	std::function<void(int, std::string)> done)
{
	/* lent from now until it is given back: a script asking at once sees it (client.encodingvideo) */
	borrowedM_.store(true);
	borrowStopM_.store(false);
	borrowFramesM_.store(0);
	submit([this, from, to, frame = std::move(frame), finishing = std::move(finishing), done = std::move(done)] {
		namespace fs = std::filesystem;
		/* refused before a frame: what the caller made ready for the frames
		 * (an encoder) is closed as at the end */
		auto fail = [&](const std::string &why) {
			if (finishing) finishing();
			borrowedM_.store(false);
			later([done, why] { done(2, why); });
		};
		if (s_ == nullptr) return fail(tr("session.no_machine"));
		if (from < 0 || to < from) return fail(tr("session.end_before_start"));
		if (to >= ce_session_movie_length(s_)) return fail(tr("session.movie_ends_at", {std::to_string(ce_session_movie_length(s_) - 1)}));
		/* where the person is, kept as a state file the engine writes: a
		 * machine's state can be bigger than memory holds at once */
		fs::path folder = projectCacheOnMachine();
		std::error_code ec;
		if (folder.empty()) folder = fs::temp_directory_path(ec);
		fs::create_directories(folder, ec);
		const fs::path restore = folder / ("encode-restore-" + std::to_string(++borrowCount_) + ".state");
		if (ce_session_state_save_file(s_, branch_state::utf8(restore).c_str(), nullptr, 0) != 0)
			return fail(tr("session.encode_no_state", {ce_session_last_error(s_)}));
		const int64_t was = ce_session_frame(s_);
		const bool wasRunning = runningM_, wasRecording = recordingM_.load();
		Picture::Frame shown;
		screen.takeIfNewer(UINT64_MAX, shown);
		runningM_ = false;
		later([this] { running.set(false); });
		/* a reproduction: nothing recorded over the movie, no state kept */
		ce_session_greenzone_capture_period(s_, 0);
		recordingM_ = true;
		seekOnMachine(from, false);
		int ended = 0;
		std::string why;
		while (s_ != nullptr)
		{
			if (borrowStopM_.load())
			{
				ended = 1;
				break;
			}
			if (ce_session_frame(s_) > to) break;
			/* the scripts have their turns, as each frame Chimera encodes is a frame it plays */
			ce_session *const machine = s_;
			scriptTurnOnMachine(scriptsBeforeM_);
			if (s_ == nullptr || s_ != machine)
			{
				/* a script closed the machine or opened another: not an encode that finished */
				ended = 2;
				why = tr("session.closed_while_encoding");
				break;
			}
			const int32_t lag = stepOnMachine(false, true, false);
			if (lag < 0 && diedM_)
			{
				ended = 2;
				why = tr("session.machine_stopped");
				break;
			}
			/* the window's counters follow, as what it draws over the picture may be in the video */
			report(-1, -1, -1, -1, -1);
			scriptTurnOnMachine(scriptsAfterM_);
			if (!frame(s_, why))
			{
				ended = 2;
				break;
			}
			borrowFramesM_.fetch_add(1);
		}
		if (ended == 0 && s_ == nullptr)
		{
			ended = 2;
			why = tr("session.closed_while_encoding");
		}
		if (finishing) finishing();
		/* given back: the frame, its picture, the greenzone, running or not */
		recordingM_ = wasRecording;
		if (s_ != nullptr)
		{
			ce_session_greenzone_capture_period(s_, periodM_.load());
			if (ce_session_state_load_file(s_, branch_state::utf8(restore).c_str(), nullptr, 0, nullptr) == 0 &&
				ce_session_movie_at(s_, was) == 0)
			{
				frameM_ = was;
				if (shown.width > 0 && shown.height > 0) screen.publish(shown.pixels.data(), shown.width, shown.height, shown.shownWidth, shown.shownHeight);
			}
			else
			{
				seekOnMachine(was, false);
				why += (why.empty() ? "" : " ") + tr("session.put_back_by_playing");
			}
			runningM_ = wasRunning;
			due_ = Clock::now();
			later([this, wasRunning] { running.set(wasRunning); });
		}
		fs::remove(restore, ec);
		report(-1, -1, -1, -1, -1);
		borrowedM_.store(false);
		later([done, ended, why] { done(ended, why); });
		/* the scripts' turn on the frame given back, as on the frame a seek lands
		 * on: a script waiting out the encode goes on there, as Chimera's does
		 * (after the encode is said to be over, so it sees that) */
		if (s_ != nullptr) scriptTurnOnMachine(scriptsAfterM_);
	});
}

void SessionViewModel::vsyncTick()
{
	vsyncTicks_.fetch_add(1);
	if (throttleM_.load() == 2) cv_.notify_all();
}

void SessionViewModel::setRunMarkers(std::vector<int64_t> frames)
{
	submit([this, frames = std::move(frames)] {
		if (frames == runMarkersM_) return;
		runMarkersM_ = frames;
		pinMarkersOnMachine();
	});
}

void SessionViewModel::setBranches(std::vector<Branch> branches)
{
	submit([this, branches = std::move(branches)] {
		branchesM_ = branches;
		journalMarksOnMachine();
	});
}

void SessionViewModel::loadMovie(std::string text, int64_t target)
{
	submit([this, text, target] {
		if (s_ == nullptr) return;
		runningM_ = false;
		later([this] { running.set(false); });
		replaceMovieOnMachine(text);
		seekOnMachine(target);
		later([this] { inputsReset.emit(); });
		report(-1, -1, -1, -1, -1);
	});
}

int64_t SessionViewModel::replaceMovieOnMachine(const std::string &text)
{
	/* where the two movies part: frames before it keep their green */
	ce_movie_log *now = ce_movie_log_new(), *next = ce_movie_log_new();
	const std::string current = movieTextOnMachine();
	if (!current.empty()) ce_movie_log_parse(now, current.data(), current.size());
	if (!text.empty()) ce_movie_log_parse(next, text.data(), text.size());
	const int64_t from = ce_movie_log_divergent_point(now, next);
	ce_session_movie_load(s_, next);
	ce_movie_log_free(now);
	ce_movie_log_free(next);
	if (from >= 0)
	{
		greenEndM_ = std::min(greenEndM_, from);
		forgetGameTimeAfterOnMachine(from);
	}
	return from;
}

std::filesystem::path SessionViewModel::projectCacheOnMachine() const
{
	if (projectM_ == nullptr || cacheFolderM_.empty()) return {};
	const char *id = ce_project_id(projectM_);
	if (id == nullptr || *id == '\0') return {};
	return std::filesystem::path(cacheFolderM_) / id;
}

std::filesystem::path SessionViewModel::branchDirOnMachine() const
{
	const std::filesystem::path dir = projectCacheOnMachine();
	return dir.empty() ? dir : dir / "branches";
}

std::string SessionViewModel::machineIdOnMachine()
{
	if (packageSha1M_.empty())
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package_.c_str(), &perr))
		{
			if (const char *sha = ce_package_sha1(pk)) packageSha1M_ = sha;
			ce_package_free(pk);
		}
	}
	/* the engine's rule, for a project; a game alone keeps nothing, but
	 * the same three things name its machine */
	if (projectM_ != nullptr)
	{
		char id[41];
		ce_project_machine_id(projectM_, packageSha1M_.c_str(), id);
		return id;
	}
	return branch_state::fingerprint(packageSha1M_ + "\n" + settingsOnMachine() + "\n" + slotsOnMachine());
}

void SessionViewModel::startProjectSessionOnMachine(bool loadHistory)
{
	/* read only until the window says otherwise, from this project's first frame */
	recordingModeM_ = false;
	namespace fs = std::filesystem;
	const fs::path dir = projectCacheOnMachine();
	if (dir.empty() || s_ == nullptr) return;
	std::error_code ec;
	fs::create_directories(dir, ec);
	const fs::path mark = dir / "session.running", history = dir / "history.bin";
	if (fs::exists(mark, ec))
	{
		/* the last session did not end: what it wrote may be half a file,
		 * and a machine that killed it may have stored it */
		fs::remove(history, ec);
	}
	else if (loadHistory && fs::exists(history, ec) &&
			 ce_session_history_load(s_, branch_state::utf8(history).c_str(), machineIdOnMachine().c_str()) == 0)
	{
		const int64_t newest = ce_session_greenzone_nearest(s_, ce_session_movie_length(s_));
		greenEndM_ = std::max<int64_t>(0, newest);
		report(-1, -1, -1, -1, -1);
	}
	std::ofstream(mark) << "a session of this project is running\n";
	runningMarkM_ = mark;
}

void SessionViewModel::readBudgetsOnMachine(ce_project *p, int64_t &memoryMb, int64_t &nearStride) const
{
	memoryMb = nearStride = 0;
	const std::filesystem::path dir = cacheDirOf(p);
	if (dir.empty()) return;
	std::ifstream in(dir / "budgets.json", std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	json::Json b;
	std::string err;
	/* unreadable is the same as absent: every project's apply */
	if (text.empty() || !json::parse(text, b, err) || b.kind != json::Json::Kind::Object) return;
	if (const json::Json *v = b.find("memoryMb"); v != nullptr && v->kind == json::Json::Kind::Number)
		memoryMb = std::max<int64_t>(0, static_cast<int64_t>(v->number()));
	if (const json::Json *v = b.find("maxNearStride"); v != nullptr && v->kind == json::Json::Kind::Number)
		nearStride = std::clamp<int64_t>(static_cast<int64_t>(v->number()), 0, 32);
}

std::filesystem::path SessionViewModel::recoveryDirOf(ce_project *p) const
{
	/* its own folder per project, beside the caches and never in one: removing
	 * a project's cache must not take its unsaved work with it */
	if (p == nullptr || recoveryFolderM_.empty()) return {};
	const char *id = ce_project_id(p);
	if (id == nullptr || *id == '\0') return {};
	return std::filesystem::path(recoveryFolderM_) / id;
}

namespace {
/* who wrote a recovery folder: enough to tell a crash from another window
 * that has the project open now */
struct RecoveryRecord
{
	int64_t processId = 0, processStarted = -1, lastWork = 0;
	std::string projectPath;
};

bool readRecoveryRecord(const std::filesystem::path &dir, RecoveryRecord &r)
{
	std::ifstream in(dir / "session.json", std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	json::Json j;
	std::string err;
	if (text.empty() || !json::parse(text, j, err) || j.kind != json::Json::Kind::Object) return false;
	/* read as written: a start time does not fit a double */
	auto whole = [](const json::Json *v, int64_t otherwise) -> int64_t {
		if (v == nullptr || v->kind != json::Json::Kind::Number) return otherwise;
		try
		{
			return static_cast<int64_t>(std::stoll(v->text));
		}
		catch (...)
		{
			return otherwise;
		}
	};
	r.processId = whole(j.find("processId"), 0);
	r.processStarted = whole(j.find("processStarted"), -1);
	r.lastWork = whole(j.find("lastWork"), 0);
	if (const json::Json *v = j.find("projectPath")) r.projectPath = v->text;
	return true;
}

bool writtenByThisProcess(const RecoveryRecord &r)
{
	return r.processId == currentProcessId() && r.processStarted == processStarted(currentProcessId());
}

bool writerRunning(const RecoveryRecord &r)
{
	return r.processId > 0 && r.processStarted >= 0 && processStarted(r.processId) == r.processStarted;
}

std::string localTime(int64_t utc, const char *format)
{
	const std::time_t t = static_cast<std::time_t>(utc);
	char out[48] = {};
	if (const std::tm *tm = std::localtime(&t)) std::strftime(out, sizeof out, format, tm);
	return out;
}
} // namespace

namespace {
/* Chimera's records for the markers and branches in the journal:
 * "M [[frame, keepsState, "message"], ...]" every
 * marker a person placed, "B {...}" one branch whole, "O [ids]" the
 * branches in order - the list is these */
json::Json markersJson(const std::vector<Marker> &markers)
{
	json::Json a;
	a.kind = json::Json::Kind::Array;
	for (const Marker &m : markers)
	{
		json::Json one;
		one.kind = json::Json::Kind::Array;
		one.items = {json::Json::integer(m.frame), json::Json::boolean_(m.keepState), json::Json::string(m.text)};
		a.items.push_back(std::move(one));
	}
	return a;
}

std::string markersRecord(const std::vector<Marker> &markers)
{
	return "M " + json::write(markersJson(markers));
}

/* a branch's id in the records: the window's own for it, or, for the
 * branches a project opened with until the window has given them theirs, its
 * place in that list */
std::string recordIdOf(const SessionViewModel::Branch &b, size_t at)
{
	return b.id != 0 ? std::to_string(b.id) : "p" + std::to_string(at);
}

std::string branchRecord(const SessionViewModel::Branch &b, const std::string &id)
{
	json::Json r;
	r.kind = json::Json::Kind::Object;
	r.members.emplace_back("id", json::Json::string(id));
	r.members.emplace_back("text", json::Json::string(b.name));
	r.members.emplace_back("frame", json::Json::integer(b.frame));
	r.members.emplace_back("time", json::Json::string(b.time));
	r.members.emplace_back("log", json::Json::string(b.text));
	r.members.emplace_back("markers", markersJson(b.markers));
	return "B " + json::write(r);
}

std::string orderRecord(const std::vector<SessionViewModel::Branch> &branches)
{
	json::Json a;
	a.kind = json::Json::Kind::Array;
	for (size_t i = 0; i < branches.size(); i++) a.items.push_back(json::Json::string(recordIdOf(branches[i], i)));
	return "O " + json::write(a);
}

int64_t wholeOf(const json::Json &v)
{
	if (v.kind != json::Json::Kind::Number) return 0;
	try
	{
		return static_cast<int64_t>(std::stoll(v.text));
	}
	catch (...)
	{
		return 0;
	}
}

/* the markers and branches a journal's records end on, written into the
 * project, as Chimera does: a journal with no
 * marker record, or no order record, leaves the project's own, and a record
 * that does not read ends the reading, as the engine's replay ends at a line
 * it cannot read */
void applyMarkRecords(ce_project *p, const ce_movie_log *log)
{
	std::optional<json::Json> markers, order;
	std::map<std::string, json::Json> branches;
	const int64_t n = ce_movie_log_journal_note_count(log);
	for (int64_t i = 0; i < n; i++)
	{
		const char *note = ce_movie_log_journal_note_at(log, i);
		if (note == nullptr || note[0] == '\0' || note[1] != ' ') continue;
		json::Json j;
		std::string err;
		if (!json::parse(note + 2, j, err)) break;
		if (note[0] == 'M' && j.kind == json::Json::Kind::Array) markers = std::move(j);
		else if (note[0] == 'O' && j.kind == json::Json::Kind::Array) order = std::move(j);
		else if (note[0] == 'B' && j.kind == json::Json::Kind::Object)
		{
			const json::Json *id = j.find("id");
			if (id != nullptr && id->kind == json::Json::Kind::String) branches[id->text] = std::move(j);
		}
	}
	auto marker = [](const json::Json &m, auto add) {
		if (m.kind == json::Json::Kind::Array && m.items.size() >= 3) add(wholeOf(m.items[0]), m.items[2].text, m.items[1].boolean);
	};
	if (markers)
	{
		for (int32_t i = ce_project_marker_count(p) - 1; i >= 0; i--) ce_project_marker_remove(p, i);
		for (const json::Json &m : markers->items)
			marker(m, [p](int64_t frame, const std::string &text, bool keep) { ce_project_marker_add(p, frame, text.c_str(), keep ? 1 : 0); });
	}
	if (!order) return;
	for (int32_t i = ce_project_branch_count(p) - 1; i >= 0; i--) ce_project_branch_remove(p, i);
	for (const json::Json &id : order->items)
	{
		const auto it = branches.find(id.text);
		if (it == branches.end()) continue;
		const json::Json &b = it->second;
		auto text = [&b](const char *name) {
			const json::Json *v = b.find(name);
			return v != nullptr ? v->text : std::string();
		};
		const json::Json *frame = b.find("frame");
		const std::string movie = text("log");
		ce_project_branch_add(p, text("text").c_str(), frame != nullptr ? wholeOf(*frame) : 0, text("time").c_str(), movie.data(), movie.size());
		const int32_t at = ce_project_branch_count(p) - 1;
		if (const json::Json *ms = b.find("markers"); ms != nullptr && ms->kind == json::Json::Kind::Array)
			for (const json::Json &m : ms->items)
				marker(m, [p, at](int64_t f, const std::string &t, bool keep) { ce_project_branch_marker_add(p, at, f, t.c_str(), keep ? 1 : 0); });
	}
}

void writeRecoveryRecord(const std::filesystem::path &dir, const std::string &projectPath)
{
	json::Json r;
	r.kind = json::Json::Kind::Object;
	r.members.emplace_back("processId", json::Json::integer(currentProcessId()));
	r.members.emplace_back("processStarted", json::Json::integer(processStarted(currentProcessId())));
	r.members.emplace_back("projectPath", json::Json::string(projectPath));
	r.members.emplace_back("lastWork", json::Json::integer(static_cast<int64_t>(std::time(nullptr))));
	/* written beside, then put in place, as the snapshot is: a crash (or a
	 * copy) while it is written finds the last whole one, never an empty one */
	const std::filesystem::path fresh = dir / "session.json.new";
	{
		std::ofstream out(fresh, std::ios::binary | std::ios::trunc);
		out << json::write(r);
		if (!out) return;
	}
	std::error_code ec;
	std::filesystem::rename(fresh, dir / "session.json", ec);
}

/* when a file was last written, as the clock here reads it; 0 when it is not there */
int64_t writtenAt(const std::filesystem::path &file)
{
	std::error_code ec;
	const auto t = std::filesystem::last_write_time(file, ec);
	if (ec) return 0;
	return static_cast<int64_t>(std::chrono::system_clock::to_time_t(std::chrono::clock_cast<std::chrono::system_clock>(t)));
}
} // namespace

void SessionViewModel::journalOnMachine()
{
	namespace fs = std::filesystem;
	journalRetryDue_ = Clock::now() + std::chrono::seconds(1);
	const fs::path dir = recoveryDirOf(projectM_);
	if (dir.empty() || s_ == nullptr) return;
	std::error_code ec;
	/* another window working on this project keeps its own work here */
	RecoveryRecord was;
	if (readRecoveryRecord(dir, was) && !writtenByThisProcess(was) && writerRunning(was)) return;
	fs::create_directories(dir, ec);
	/* the markers and branches go into the same fresh file as the movie, so
	 * that no rewrite can part them */
	journaledMarkers_ = markersRecord(markersM_);
	journaledOrder_ = orderRecord(branchesM_);
	journaledBranches_.clear();
	std::string image = journaledMarkers_ + "\n";
	for (size_t i = 0; i < branchesM_.size(); i++)
	{
		const std::string id = recordIdOf(branchesM_[i], i);
		std::string record = branchRecord(branchesM_[i], id);
		image += record + "\n";
		journaledBranches_[id] = std::move(record);
	}
	image += journaledOrder_ + "\n";
	const fs::path journal = dir / "work.journal";
	if (ce_session_movie_journal_open(s_, journal.string().c_str(), image.c_str()) != 0) return;
	journalImageBytes_ = fs::file_size(journal, ec);
	if (ec) journalImageBytes_ = 0;
	if (!readRecoveryRecord(dir, was)) writeRecoveryRecord(dir, projectPathM_);
}

void SessionViewModel::journalMarksOnMachine()
{
	if (s_ == nullptr || ce_session_movie_journaling(s_) == 0) return;
	if (std::string m = markersRecord(markersM_); m != journaledMarkers_)
	{
		ce_session_movie_journal_note(s_, m.c_str());
		journaledMarkers_ = std::move(m);
	}
	std::vector<std::string> ids;
	for (size_t i = 0; i < branchesM_.size(); i++)
	{
		ids.push_back(recordIdOf(branchesM_[i], i));
		std::string record = branchRecord(branchesM_[i], ids.back());
		const auto it = journaledBranches_.find(ids.back());
		if (it != journaledBranches_.end() && it->second == record) continue;
		ce_session_movie_journal_note(s_, record.c_str());
		journaledBranches_[ids.back()] = std::move(record);
	}
	if (std::string o = orderRecord(branchesM_); o != journaledOrder_)
	{
		ce_session_movie_journal_note(s_, o.c_str());
		journaledOrder_ = std::move(o);
		for (auto it = journaledBranches_.begin(); it != journaledBranches_.end();)
		{
			const bool kept = std::find(ids.begin(), ids.end(), it->first) != ids.end();
			it = kept ? std::next(it) : journaledBranches_.erase(it);
		}
	}
}

void SessionViewModel::keepRecoveryOnMachine()
{
	namespace fs = std::filesystem;
	const fs::path dir = recoveryDirOf(projectM_);
	if (dir.empty() || s_ == nullptr) return;
	std::error_code ec;
	/* another window working on this project keeps its own work here */
	RecoveryRecord was;
	if (readRecoveryRecord(dir, was) && !writtenByThisProcess(was) && writerRunning(was)) return;
	fs::create_directories(dir, ec);
	/* the journal first, so that no snapshot is without one; written again
	 * from the start once it has grown well past its last start, as
	 * Chimera's keeps it short */
	if (ce_session_movie_journaling(s_) == 0)
		journalOnMachine();
	else if (journalImageBytes_ > 0)
	{
		const uintmax_t now = fs::file_size(dir / "work.journal", ec);
		if (!ec && now > std::max<uintmax_t>(uintmax_t{16} << 20, 3 * journalImageBytes_)) journalOnMachine();
	}
	/* written beside, then put in place: a crash while it is written leaves the last whole one */
	const fs::path snapshot = dir / "snapshot.chimeraProject", fresh = dir / "snapshot.chimeraProject.new";
	if (!saveProjectOnMachine(fresh.string(), true, true)) return;
	fs::rename(fresh, snapshot, ec);
	if (ec) return;
	writeRecoveryRecord(dir, projectPathM_);
}

void SessionViewModel::dropRecoveryOnMachine(const std::filesystem::path &dir)
{
	namespace fs = std::filesystem;
	recoveryDue_ = {};
	std::error_code ec;
	if (dir.empty() || !fs::is_directory(dir, ec)) return;
	/* only this window's own: another's work in progress, or a crash's not offered yet, stays */
	RecoveryRecord r;
	if (readRecoveryRecord(dir, r) && !writtenByThisProcess(r)) return;
	/* the journal let go of before its folder goes (Windows will not delete a
	 * file being written), and the next one opened as soon as there is work */
	if (s_ != nullptr) ce_session_movie_journal_close(s_, 1);
	journalRetryDue_ = {};
	fs::remove_all(dir, ec);
}

bool SessionViewModel::recoveredCopyOnMachine(ce_project *p, const std::string &projectPath, std::string &copy, std::string &upTo,
	std::string &crash, std::string &crashNote)
{
	namespace fs = std::filesystem;
	const fs::path dir = recoveryDirOf(p);
	std::error_code ec;
	if (dir.empty() || !fs::is_directory(dir, ec)) return false;
	RecoveryRecord r;
	const bool hasRecord = readRecoveryRecord(dir, r);
	/* its writer still runs: another window has this project open (or this
	 * one, the project opened again over itself) - not a crash */
	if (hasRecord && writerRunning(r)) return false;
	const fs::path snapshot = dir / "snapshot.chimeraProject", journal = dir / "work.journal";
	const bool hasSnapshot = fs::is_regular_file(snapshot, ec);
	/* the engine reads a journal from its ".new" when a rewrite was cut short */
	const fs::path journalOnDisk = fs::is_regular_file(journal, ec) ? journal : fs::path(journal.string() + ".new");
	const bool hasJournal = fs::is_regular_file(journalOnDisk, ec);
	if (!hasSnapshot && !hasJournal)
	{
		/* the remains of a clean end whose removal did not land */
		fs::remove_all(dir, ec);
		return false;
	}
	/* the project saved after the work was last kept holds that work already */
	const int64_t when = std::max(hasSnapshot ? writtenAt(snapshot) : 0, hasJournal ? writtenAt(journalOnDisk) : 0);
	const bool hasProject = !projectPath.empty() && fs::is_regular_file(projectPath, ec);
	if (hasProject && writtenAt(projectPath) > when)
	{
		fs::remove_all(dir, ec);
		return false;
	}
	auto kept = [this, dir](const std::string &why) {
		later([this, why, kept = dir.string()] {
			status.set(tr("session.recovery_not_rebuilt", {why, kept}));
		});
		return false;
	};
	/* rebuilt on whichever is newer, the last snapshot or the project as last
	 * saved, with the movie, markers and branches the journal ends on, which
	 * is never older than either */
	const fs::path base = hasSnapshot && !(hasProject && writtenAt(projectPath) > writtenAt(snapshot)) ? snapshot
		: hasProject ? fs::path(projectPath) : fs::path();
	if (base.empty()) return kept(tr("session.no_copy_to_rebuild"));
	const char *err = nullptr;
	ce_project *rebuilt = ce_project_open(base.string().c_str(), &err);
	if (rebuilt == nullptr) return kept(err != nullptr ? err : tr("session.project_would_not_open"));
	if (hasJournal)
	{
		ce_movie_log *log = ce_movie_log_new();
		if (ce_movie_log_journal_replay(log, journal.string().c_str()) >= 0)
		{
			/* a journal that never saw the movie's columns keeps the project's */
			if (ce_movie_log_key(log) == nullptr)
			{
				uint64_t len = 0;
				const char *text = ce_project_log_text(rebuilt, &len);
				ce_movie_log *saved = ce_movie_log_new();
				if (text != nullptr && ce_movie_log_parse(saved, text, len) == 0 && ce_movie_log_key(saved) != nullptr)
					ce_movie_log_set_key(log, ce_movie_log_key(saved));
				ce_movie_log_free(saved);
			}
			uint64_t len = 0;
			const char *text = ce_movie_log_serialize(log, 0, &len);
			ce_project_set_log_text(rebuilt, text, len);
			applyMarkRecords(rebuilt, log);
		}
		else
		{
			std::fprintf(stderr, "[recovery] the journal could not be read (%s); the snapshot's work is used\n", ce_movie_log_last_error(log));
		}
		ce_movie_log_free(log);
	}
	/* rebuilt as a project of its own among the backups, so whatever is
	 * answered nothing is lost, and never over the project unasked */
	const fs::path folder = !backupFolderM_.empty() ? fs::path(backupFolderM_) : fs::path(recoveryFolderM_) / "recovered";
	fs::create_directories(folder, ec);
	const std::string stem = projectPath.empty() ? std::string("project") : fs::path(projectPath).stem().string();
	const std::string name = tr("session.recovered_file", {stem, localTime(when, "%Y-%m-%d %H.%M.%S")});
	fs::path target = folder / (name + ".chimeraProject");
	for (int n = 2; fs::exists(target, ec) && n < 100; n++) target = folder / (name + " (" + std::to_string(n) + ").chimeraProject");
	const bool saved = ce_project_save(rebuilt, target.string().c_str(), &err) == 0;
	const std::string why = saved ? std::string() : err != nullptr ? err : tr("session.not_written");
	ce_project_free(rebuilt);
	if (!saved) return kept(why);
	/* the copy holds the work now, and this session keeps its own */
	fs::remove_all(dir, ec);
	copy = target.string();
	upTo = localTime(when, "%H:%M:%S");
	/* what ended that session, when Windows let the crash module say */
	if (hasRecord && r.processId > 0)
		if (const auto note = crash::noteFor(r.processId, crash::notBeforeStart(r.processStarted), crashFolderM_))
		{
			crash = note->summary();
			crashNote = note->path;
		}
	return true;
}

std::filesystem::path SessionViewModel::cacheDirOf(ce_project *p) const
{
	if (p == nullptr || cacheFolderM_.empty()) return {};
	const char *id = ce_project_id(p);
	if (id == nullptr || *id == '\0') return {};
	return std::filesystem::path(cacheFolderM_) / id;
}

void SessionViewModel::applyLocalPathsOnMachine(ce_project *p)
{
	const std::filesystem::path dir = cacheDirOf(p);
	if (dir.empty()) return;
	std::ifstream in(dir / "local-paths.json", std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	json::Json local;
	std::string err;
	if (!json::parse(text, local, err)) return;
	const json::Json *files = local.find("files");
	if (files == nullptr) return;
	/* a hint, never authority: a file there that is not the recorded one is let go */
	for (int32_t i = 0; i < ce_project_file_count(p); i++)
	{
		if (ce_project_file_status(p, i) == 0) continue;
		const char *name = ce_project_file_name(p, i);
		const json::Json *where = name != nullptr ? files->find(name) : nullptr;
		if (where == nullptr || where->text.empty()) continue;
		const char *e = nullptr;
		if (ce_project_file_resolve(p, i, where->text.c_str(), &e) != 0 || ce_project_file_status(p, i) != 0) ce_project_file_unresolve(p, i);
	}
}

void SessionViewModel::rememberLocalPathsOnMachine()
{
	const std::filesystem::path dir = cacheDirOf(projectM_);
	if (dir.empty()) return;
	/* what was there before (Chimera keeps its firmware's places beside them) */
	json::Json local;
	{
		std::ifstream in(dir / "local-paths.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::string err;
		if (!json::parse(text, local, err) || local.kind != json::Json::Kind::Object)
		{
			local = json::Json();
			local.kind = json::Json::Kind::Object;
		}
	}
	json::Json files;
	files.kind = json::Json::Kind::Object;
	for (int32_t i = 0; i < ce_project_file_count(projectM_); i++)
	{
		const char *name = ce_project_file_name(projectM_, i), *source = ce_project_file_source_path(projectM_, i);
		if (name != nullptr && source != nullptr && *source != '\0' && ce_project_file_status(projectM_, i) == 0)
			files.members.emplace_back(name, json::Json::string(std::filesystem::absolute(source).string()));
	}
	bool set = false;
	for (auto &[k, v] : local.members)
		if (k == "files") v = files, set = true;
	if (!set) local.members.emplace_back("files", files);
	std::error_code ec;
	std::filesystem::create_directories(dir, ec);
	std::ofstream(dir / "local-paths.json", std::ios::binary) << json::write(local);
}

void SessionViewModel::publishProjectFilesOnMachine()
{
	std::vector<ProjectFile> files;
	ce_project *p = pendingOpenM_.project;
	for (int32_t i = 0; p != nullptr && i < ce_project_file_count(p); i++)
	{
		ProjectFile f;
		auto text = [](const char *t) { return std::string(t != nullptr ? t : ""); };
		f.name = text(ce_project_file_name(p, i));
		f.slot = text(ce_project_file_slot(p, i));
		f.sha1 = text(ce_project_file_sha1(p, i));
		f.actualSha1 = text(ce_project_file_actual_sha1(p, i));
		f.status = ce_project_file_status(p, i);
		files.push_back(std::move(f));
	}
	for (const MissingFirmware &m : missingFirmwareM_)
	{
		ProjectFile f;
		f.name = m.display.empty() ? m.id : m.display + " (" + m.id + ")";
		f.slot = "firmware";
		f.sha1 = !m.pinned.empty() ? m.pinned : m.gameCore ? std::string() : m.declared;
		f.actualSha1 = m.sha1;
		f.status = m.path.empty() ? 1 : 0;
		f.firmware = true;
		files.push_back(std::move(f));
	}
	later([this, files] {
		projectFiles_ = files;
		projectFileRows_.reset.emit();
	});
}

std::string SessionViewModel::locateFirmwareOnMachine(MissingFirmware &m, const std::string &path)
{
	char sha[41] = {};
	uint64_t length = 0;
	if (ce_sha1_file(path.c_str(), sha, &length) != 1) return tr("session.cannot_read_file", {path});
	/* the hash the project pins, else the package's; a game core takes a
	 * file of the person's own where the project pins none, as New Project does */
	const std::string wanted = !m.pinned.empty() ? m.pinned : m.gameCore ? std::string() : m.declared;
	auto upper = [](std::string t) {
		for (char &c : t) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		return t;
	};
	if (!wanted.empty() && upper(wanted) != upper(sha))
		return tr("session.wrong_firmware", {std::filesystem::path(path).filename().string(), upper(sha), upper(wanted)});
	m.path = path;
	m.sha1 = upper(sha);
	locatedFirmwareM_[m.id] = path;
	/* found again at the next open, as New Project remembers what was chosen */
	later([this, core = m.core, id = m.id, path] {
		if (keepFirmware_) keepFirmware_(core, id, path);
	});
	return {};
}

std::string SessionViewModel::ProjectFileRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "slot", "status", "statusText", "sha1", "actualSha1"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value SessionViewModel::ProjectFileRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const ProjectFile &f = vm_.projectFiles_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return f.name;
	case 1: return f.slot;
	case 2: return static_cast<int64_t>(f.status);
	/* Chimera's words for each */
	case 3:
		/* a firmware no hash is asked of is taken as chosen */
		if (f.firmware && f.status == 0 && f.sha1.empty()) return tr("project_files.chosen");
		return tr(f.status == 0 ? "project_files.found" : f.status == 2 ? "project_files.mismatch" : "project_files.not_found");
	case 4: return f.sha1;
	case 5: return f.actualSha1;
	default: return {};
	}
}

void SessionViewModel::rememberAboutOnMachine(const std::string &projectPath)
{
	namespace fs = std::filesystem;
	const fs::path dir = projectCacheOnMachine();
	if (dir.empty() || projectM_ == nullptr) return;
	json::Json about;
	about.kind = json::Json::Kind::Object;
	const char *title = ce_project_title(projectM_);
	const char *core = ce_project_core_name(projectM_);
	std::error_code ec;
	const fs::path full = projectPath.empty() ? fs::path() : fs::absolute(projectPath, ec);
	about.members.emplace_back("title", json::Json::string(title != nullptr ? title : ""));
	about.members.emplace_back("projectPath", json::Json::string(full.string()));
	about.members.emplace_back("core", json::Json::string(core != nullptr ? core : ""));
	const char *system = s_ != nullptr ? ce_session_system_name(s_) : nullptr;
	about.members.emplace_back("system", json::Json::string(system != nullptr ? system : ""));
	json::Json games;
	games.kind = json::Json::Kind::Array;
	for (int32_t i = 0; i < ce_project_file_count(projectM_); i++)
		if (const char *name = ce_project_file_name(projectM_, i)) games.items.push_back(json::Json::string(name));
	about.members.emplace_back("games", games);
	fs::create_directories(dir, ec);
	std::ofstream(dir / "about.json", std::ios::binary) << json::write(about);
}

void SessionViewModel::keepHistoryOnMachine()
{
	namespace fs = std::filesystem;
	const fs::path dir = projectCacheOnMachine();
	if (dir.empty() || s_ == nullptr) return;
	/* A machine a GPU drew makes states good only in the session that
	 * made them, unless its core says they outlive it; a machine that died
	 * cannot vouch for anything it stored. */
	bool survives = ce_session_gpu_drew(s_) == 0;
	if (!survives)
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package_.c_str(), &perr))
		{
			uint64_t len = 0;
			json::Json cfg;
			std::string jerr;
			if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
				d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, jerr))
				if (const json::Json *video = cfg.find("video"))
					if (const json::Json *keep = video->find("greenzoneSurvivesReload")) survives = keep->boolean;
			ce_package_free(pk);
		}
	}
	std::error_code ec;
	fs::create_directories(dir, ec);
	const fs::path history = dir / "history.bin";
	if (survives && !diedM_)
		ce_session_history_save_later(s_, branch_state::utf8(history).c_str(), machineIdOnMachine().c_str());
	else
	{
		ce_session_history_save_wait(s_);
		fs::remove(history, ec);
	}
}

void SessionViewModel::becameAnotherOnMachine(const std::filesystem::path &cacheWas, const std::filesystem::path &recoveryWas)
{
	namespace fs = std::filesystem;
	std::error_code ec;
	/* the work kept against a crash under the old id is in the new file now */
	dropRecoveryOnMachine(recoveryWas);
	const fs::path dir = projectCacheOnMachine();
	if (dir.empty()) return;
	fs::create_directories(dir, ec);
	/* The branches' states and notes, and the budgets: the history is written
	 * again by this save, and the rest says where the project is now. A state
	 * is written whole to a file of its own and moved into place, so the two
	 * folders can share one until either writes it again; the rest is copied. */
	auto bring = [&](const fs::path &from, const fs::path &to, bool share) {
		fs::remove(to, ec);
		if (share) fs::create_hard_link(from, to, ec);
		if (!share || ec) fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
	};
	if (!cacheWas.empty())
	{
		if (fs::is_regular_file(cacheWas / "budgets.json", ec)) bring(cacheWas / "budgets.json", dir / "budgets.json", false);
		if (fs::is_directory(cacheWas / "branches", ec))
		{
			fs::create_directories(dir / "branches", ec);
			for (const fs::directory_entry &e : fs::directory_iterator(cacheWas / "branches", ec))
				if (e.is_regular_file(ec)) bring(e.path(), dir / "branches" / e.path().filename(), e.path().extension() == ".state");
		}
	}
	/* this window's session goes on under the new id */
	if (!runningMarkM_.empty())
	{
		fs::remove(runningMarkM_, ec);
		runningMarkM_ = dir / "session.running";
		std::ofstream(runningMarkM_) << "a session of this project is running\n";
	}
}

void SessionViewModel::makeBranch(std::string name, std::string time, std::function<void(Branch, std::string)> done)
{
	submit([this, name = std::move(name), time = std::move(time), done = std::move(done)] {
		if (s_ == nullptr) return;
		namespace fs = std::filesystem;
		Branch b;
		b.name = name;
		b.time = time;
		b.frame = ce_session_frame(s_);
		b.text = movieTextOnMachine();
		b.frames = ce_session_movie_length(s_);
		/* the picture shown now, which a state load will not draw */
		auto picture = std::make_shared<Picture::Frame>();
		screen.takeIfNewer(~uint64_t{0}, *picture);
		if (picture->width > 0 && picture->height > 0) b.picture = picture;
		std::string why;
		if (const fs::path dir = branchDirOnMachine(); !dir.empty())
		{
			std::error_code ec;
			fs::create_directories(dir, ec);
			const std::string stem = branch_state::nameFor(b.time, b.frame, b.text);
			const fs::path state = dir / (stem + ".state");
			if (ce_session_state_save_file(s_, branch_state::utf8(state).c_str(), nullptr, 0) != 0)
				why = ce_session_last_error(s_);
			else if (!branch_state::writeNote(dir / (stem + ".note"),
						 {machineIdOnMachine(), b.frame, b.picture ? *b.picture : Picture::Frame{}}))
			{
				why = tr("session.note_not_written", {dir.string()});
				fs::remove(state, ec);
			}
			else
				b.state = stem;
		}
		later([done, b, why] { done(b, why); });
	});
}

void SessionViewModel::loadBranch(Branch b, std::function<void(bool, std::string)> done)
{
	submit([this, b = std::move(b), done = std::move(done)] {
		if (s_ == nullptr) return;
		namespace fs = std::filesystem;
		runningM_ = false;
		later([this] { running.set(false); });
		bool fromState = false;
		std::string why;
		const fs::path dir = branchDirOnMachine();
		branch_state::Note note;
		const bool fits = !b.state.empty() && !dir.empty() && branch_state::readNote(dir / (b.state + ".note"), note) &&
						  note.frame == b.frame && note.machine == machineIdOnMachine();
		const int64_t was = ce_session_frame(s_);
		replaceMovieOnMachine(b.text);
		if (fits)
		{
			const int32_t loaded = ce_session_state_load_file(s_, branch_state::utf8(dir / (b.state + ".state")).c_str(), nullptr, 0, nullptr);
			if (loaded == 0 && ce_session_movie_at(s_, b.frame) == 0)
			{
				fromState = true;
				frameM_ = b.frame;
				const Picture::Frame &shown = b.picture ? *b.picture : note.picture;
				if (shown.width > 0 && shown.height > 0) screen.publish(shown.pixels.data(), shown.width, shown.height, shown.shownWidth, shown.shownHeight);
				for (const Hooks &h : hooksM_)
					if (h.frame) h.frame(s_);
			}
			else
			{
				why = ce_session_last_error(s_);
				/* a state the machine took and could not use leaves it on no
				 * frame at all: back to a stored one first */
				if (loaded == 1)
				{
					const int64_t base = ce_session_greenzone_nearest(s_, std::min(was, b.frame));
					if (base >= 0 && ce_session_greenzone_restore(s_, base) == 0) frameM_ = ce_session_frame(s_);
				}
			}
		}
		if (!fromState) seekOnMachine(b.frame);
		later([this, done, fromState, why] {
			inputsReset.emit();
			done(fromState, why);
		});
		report(-1, -1, -1, -1, -1);
	});
}

void SessionViewModel::forgetBranch(Branch b)
{
	submit([this, b = std::move(b)] {
		if (b.state.empty()) return;
		/* another branch may be the same one */
		for (const Branch &other : branchesM_)
			if (other.state == b.state) return;
		const std::filesystem::path dir = branchDirOnMachine();
		if (dir.empty()) return;
		std::error_code ec;
		std::filesystem::remove(dir / (b.state + ".state"), ec);
		std::filesystem::remove(dir / (b.state + ".note"), ec);
	});
}

void SessionViewModel::tidyBranchStatesOnMachine()
{
	namespace fs = std::filesystem;
	const fs::path dir = branchDirOnMachine();
	std::error_code ec;
	if (dir.empty() || !fs::is_directory(dir, ec)) return;
	std::vector<fs::path> gone;
	for (const auto &entry : fs::directory_iterator(dir, ec))
	{
		const fs::path f = entry.path();
		const std::string ext = f.extension().string();
		if (ext != ".state" && ext != ".note" && ext != ".writing") continue;
		const std::string stem = f.stem().string();
		bool kept = false;
		for (const Branch &b : branchesM_) kept = kept || (!b.state.empty() && stem.rfind(b.state, 0) == 0 && ext != ".writing");
		if (!kept) gone.push_back(f);
	}
	for (const fs::path &f : gone) fs::remove(f, ec);
}

void SessionViewModel::forgetElsewhereCacheOnMachine(const std::string &path)
{
	/* Opened from another path than the one its cache was last opened or
	 * saved from: a file copied by hand, a moved one, a backup or a recovered
	 * copy, each keeping the id and so the folder. The state history and the
	 * branches' states there were made for whatever the other file held, so
	 * they go and the project starts with an empty greenzone; the budgets
	 * stay, being the person's choice for the project. A cache that never
	 * said where its project was is left alone (user-decided, 2026-10-15). */
	namespace fs = std::filesystem;
	const fs::path dir = projectCacheOnMachine();
	if (path.empty() || dir.empty()) return;
	std::ifstream in(dir / "about.json", std::ios::binary);
	const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	json::Json about;
	std::string err;
	if (text.empty() || !json::parse(text, about, err) || about.kind != json::Json::Kind::Object) return;
	const json::Json *was = about.find("projectPath");
	if (was == nullptr || was->kind != json::Json::Kind::String || was->text.empty() || samePlace(was->text, path)) return;
	/* a history the session closing now is still writing may be this folder's */
	if (s_ != nullptr) ce_session_history_save_wait(s_);
	std::error_code ec;
	fs::remove(dir / "history.bin", ec);
	fs::remove_all(dir / "branches", ec);
}

std::string SessionViewModel::movieTextOnMachine() const
{
	if (s_ == nullptr) return {};
	uint64_t len = 0;
	const char *t = ce_session_movie_text(s_, &len);
	return std::string(t != nullptr ? t : "", static_cast<size_t>(len));
}

void SessionViewModel::loadMovieOnMachine(ce_session *s, const std::string &text)
{
	ce_movie_log *log = ce_movie_log_new();
	if (!text.empty()) ce_movie_log_parse(log, text.data(), text.size());
	ce_session_movie_load(s, log);
	ce_movie_log_free(log);
}

void SessionViewModel::pinMarkersOnMachine()
{
	if (s_ == nullptr) return;
	ce_session_greenzone_unpin_all(s_);
	for (const Marker &m : markersM_)
		if (m.keepState) ce_session_greenzone_pin(s_, m.frame, 1);
	/* the run's own want their states, as Chimera's markers do unless told not to */
	for (int64_t f : runMarkersM_) ce_session_greenzone_pin(s_, f, 1);
}

void SessionViewModel::setFinders(PackageFinder packages, FolderFinder firmwareFolders)
{
	submit([this, packages = std::move(packages), firmwareFolders = std::move(firmwareFolders)] {
		findPackageM_ = packages;
		firmwareFoldersM_ = firmwareFolders;
	});
}

void SessionViewModel::setRunFirmware(std::map<std::string, std::string> byId)
{
	submit([this, byId = std::move(byId)] { runFirmwareM_ = byId; });
}

bool SessionViewModel::firmwareOnMachine(const std::string &package, const std::string &slots, const std::string &settings,
	const std::string &pins, const std::string &game, const std::vector<std::pair<std::string, std::string>> &files,
	Firmware &out, std::string &why, std::vector<MissingFirmware> *missingOut)
{
	json::Json cfg, settingsJson, pinsJson;
	std::string error;
	const char *perr = nullptr;
	if (ce_package *pk = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len))
			json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, error);
		ce_package_free(pk);
	}
	json::Json decl, settingsDecl;
	decl.kind = settingsDecl.kind = json::Json::Kind::Array;
	if (const json::Json *list = cfg.find("firmware")) decl.items = list->items;
	if (const json::Json *list = cfg.find("settings")) settingsDecl.items = list->items;
	/* what this game needs beyond the package's: only the core can say, and
	 * it is asked now rather than read from the project, so a game opened
	 * with no project is completed the same way. No machine is open while
	 * it is asked. A core that cannot answer needs nothing more here. */
	if (const json::Json *own = cfg.find("gameFirmware"); own != nullptr && own->boolean && !game.empty())
	{
		std::string unanswered;
		const std::string text = askForSuggestion(package, game, files, slots, settings, unanswered);
		json::Json answer;
		if (!text.empty() && json::parse(text, answer, error) && answer.kind == json::Json::Kind::Object)
		{
			const json::Json theirs = gameFirmwareOf(answer, cfg.find("firmware"));
			decl.items.insert(decl.items.end(), theirs.items.begin(), theirs.items.end());
			for (json::Json &d : gameSettingsOf(answer, cfg.find("settings"))) settingsDecl.items.push_back(std::move(d));
		}
	}
	if (decl.items.empty()) return true;
	if (!json::parse(settings, settingsJson, error) || settingsJson.kind != json::Json::Kind::Object) settingsJson.kind = json::Json::Kind::Object;
	if (!pins.empty()) json::parse(pins, pinsJson, error);
	/* the files this run was given come first, so one the hash finds twice is theirs */
	std::vector<std::string> places;
	for (const auto &[id, path] : runFirmwareM_) places.push_back(path);
	if (firmwareFoldersM_)
		for (std::string &f : firmwareFoldersM_()) places.push_back(std::move(f));
	const std::vector<FirmwareFile> index = indexFirmware(places, true, 10000);
	std::vector<FirmwareNeed> needs = neededFirmware(decl, settingsDecl, slots, settingsJson, pinsJson, index);
	/* given for an entry the project pins no hash for: taken as it is, as Chimera's */
	for (FirmwareNeed &n : needs)
	{
		const auto given = runFirmwareM_.find(n.id);
		if (given == runFirmwareM_.end()) continue;
		bool pinned = false;
		for (const json::Json &pin : pinsJson.items)
			if (const json::Json *id = pin.find("id"); id != nullptr && id->text == n.id) pinned = true;
		if (pinned) continue;
		for (const FirmwareFile &f : index)
			if (f.path == given->second)
			{
				n.path = f.path;
				n.sha1 = f.hash();
				n.how = tr("session.from_command_line");
			}
	}
	/* located while the project waited for it (issue 397), its hash checked then */
	for (FirmwareNeed &n : needs)
	{
		const auto located = locatedFirmwareM_.find(n.id);
		if (!n.path.empty() || located == locatedFirmwareM_.end()) continue;
		char sha[41] = {};
		uint64_t length = 0;
		if (ce_sha1_file(located->second.c_str(), sha, &length) != 1) continue;
		n.path = located->second;
		n.sha1 = sha;
		n.how = "located";
	}
	std::string missing;
	json::Json recorded;
	recorded.kind = json::Json::Kind::Array;
	for (const FirmwareNeed &n : needs)
	{
		if (n.path.empty())
		{
			/* an optional one not found is not mounted, and the core carries on */
			if (!n.required) continue;
			missing += (missing.empty() ? "" : ", ") + (n.display.empty() ? n.id : n.display + " (" + n.id + ")");
			if (missingOut != nullptr)
			{
				MissingFirmware m;
				if (const json::Json *name = cfg.find("coreName")) m.core = name->text;
				m.id = n.id;
				m.display = n.display;
				if (n.declIndex < decl.items.size())
				{
					m.decl = decl.items[n.declIndex];
					if (const json::Json *h = m.decl.find("sha1")) m.declared = h->text;
				}
				for (const json::Json &pin : pinsJson.items)
					if (const json::Json *id = pin.find("id"); id != nullptr && id->text == n.id)
						if (const json::Json *h = pin.find("sha1")) m.pinned = h->text;
				const json::Json *kind = cfg.find("kind");
				m.gameCore = kind != nullptr && kind->text == "game";
				missingOut->push_back(std::move(m));
			}
			continue;
		}
		std::ifstream in(n.path, std::ios::binary);
		out.ids.push_back(n.id);
		out.data.emplace_back((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		json::Json pin;
		pin.kind = json::Json::Kind::Object;
		pin.members.emplace_back("id", json::Json::string(n.id));
		pin.members.emplace_back("sha1", json::Json::string(n.sha1));
		recorded.items.push_back(std::move(pin));
	}
	out.pins = json::write(recorded);
	if (!missing.empty())
	{
		why = tr("session.firmware_missing", {missing});
		return false;
	}
	return true;
}

void SessionViewModel::onMachineWait(MachineJob job)
{
	waitOnMachine([&] { job(s_); });
}

void SessionViewModel::onProjectWait(ProjectJob job)
{
	waitOnMachine([&] { job(projectM_); });
}

void SessionViewModel::waitOnMachine(const std::function<void()> &job, bool betweenTurns)
{
	if (onMachineThread())
	{
		job();
		return;
	}
	/* while it waits, the window's thread does what the machine's thread
	 * waits on it for: a script's call into a view model */
	const bool window = std::this_thread::get_id() == windowThread_;
	auto done = std::make_shared<bool>(false);
	std::unique_lock<std::mutex> lock(m_);
	jobs_.push_back({true, [this, &job, done] {
		job();
		{
			std::lock_guard<std::mutex> finished(m_);
			*done = true;
		}
		cv_.notify_all();
	}, betweenTurns});
	cv_.notify_all();
	for (;;)
	{
		cv_.wait(lock, [&] { return *done || (window && !windowJobs_.empty()); });
		if (*done) return;
		WindowJob w = std::move(windowJobs_.front());
		windowJobs_.pop_front();
		lock.unlock();
		w.run();
		lock.lock();
	}
}

bool SessionViewModel::onMachineThreadWait(const std::function<void()> &job)
{
	waitOnMachine(job);
	return true;
}

bool SessionViewModel::onWindowWait(const std::function<void()> &job)
{
	if (std::this_thread::get_id() == windowThread_)
	{
		job();
		return true;
	}
	auto done = std::make_shared<bool>(false);
	std::unique_lock<std::mutex> lock(m_);
	if (quit_) return false;
	windowJobs_.push_back({done, [this, &job, done] {
		job();
		{
			std::lock_guard<std::mutex> finished(m_);
			*done = true;
		}
		cv_.notify_all();
	}});
	lock.unlock();
	cv_.notify_all();
	later([this] { serveWindowJobs(); });
	lock.lock();
	/* while it waits, the machine's thread does the jobs a thread waits on
	 * it for: what the window does for the script may ask the machine. Only
	 * those: the rest (a frame, a seek) wait until the script's turn is over */
	auto waited = [this] { return std::find_if(jobs_.begin(), jobs_.end(), [](const Job &j) { return j.waited && !j.betweenTurns; }); };
	for (;;)
	{
		cv_.wait(lock, [&] { return *done || waited() != jobs_.end() || quit_; });
		if (*done) return true;
		if (quit_)
		{
			/* the window is closing and will not do it: taken back */
			std::erase_if(windowJobs_, [&](const WindowJob &w) { return w.done == done; });
			return false;
		}
		const auto at = waited();
		std::function<void()> w = std::move(at->run);
		jobs_.erase(at);
		lock.unlock();
		w();
		lock.lock();
	}
}

void SessionViewModel::serveWindowJobs()
{
	std::unique_lock<std::mutex> lock(m_);
	while (!windowJobs_.empty())
	{
		WindowJob w = std::move(windowJobs_.front());
		windowJobs_.pop_front();
		lock.unlock();
		w.run();
		lock.lock();
	}
}

void SessionViewModel::setScriptTurns(std::function<void()> before, std::function<void()> after)
{
	waitOnMachine([&] {
		scriptsBeforeM_ = std::move(before);
		scriptsAfterM_ = std::move(after);
	}, true);
}

void SessionViewModel::setInputPollTurn(std::function<void()> turn)
{
	waitOnMachine([&] { inputPollTurnM_ = std::move(turn); }, true);
}

void SessionViewModel::inputPollCalled(void *ctx)
{
	auto *self = static_cast<SessionViewModel *>(ctx);
	if (self->inputPollTurnM_ && self->scriptsNowOnMachine()) self->inputPollTurnM_();
}

void SessionViewModel::applyInputPollsOnMachine()
{
	if (s_ == nullptr || ce_session_input_polls_reported(s_) == 0) return;
	ce_session_set_input_poll_callback(s_, inputPollsWantedM_.load() ? &SessionViewModel::inputPollCalled : nullptr, this);
}

void SessionViewModel::setFutureTurn(std::function<bool(int64_t played)> turn)
{
	waitOnMachine([&] { futureTurnM_ = std::move(turn); }, true);
}

void SessionViewModel::showFutureOnMachine()
{
	/* the machine as it is, kept to be put back */
	uint64_t len = 0;
	const uint8_t *saved = ce_session_save_state(s_, &len);
	if (saved == nullptr || len == 0) return;
	const std::vector<uint8_t> state(saved, saved + len);
	const int64_t first = ce_session_frame(s_), most = futureMaxM_.load();
	const auto axisCount = static_cast<size_t>(ce_session_axis_count(s_));
	std::vector<int32_t> axes(axisCount);
	int64_t played = 0;
	bool drawn = false;
	/* asked before each frame, as Chimera's (asked once more when the most is reached): true is enough */
	/* the buttons past the 64 one number carries go to the machine one by one */
	std::vector<uint8_t> states(static_cast<size_t>(ce_session_button_count(s_)));
	while (!futureTurnM_(played) && played < most)
	{
		if (ce_session_movie_get(s_, first + played, states.data(), axisCount != 0 ? axes.data() : nullptr) != 0)
		{
			std::fill(states.begin(), states.end(), uint8_t{0});
			for (size_t i = 0; i < axisCount; i++) axes[i] = ce_session_axis_neutral(s_, static_cast<int64_t>(i));
		}
		for (size_t i = 0; i < axisCount; i++) ce_session_set_axis(s_, static_cast<int32_t>(i), axes[i]);
		uint64_t first64 = 0;
		for (size_t b = 0; b < states.size(); b++)
		{
			if (b < 64) first64 |= uint64_t{states[b] != 0} << b;
			else ce_session_set_button(s_, static_cast<int32_t>(b), states[b]);
		}
		played++;
		if (ce_session_frame_advance(s_, first64, 1) < 0) break;
		drawn = true;
	}
	for (size_t b = 64; b < states.size(); b++) ce_session_set_button(s_, static_cast<int32_t>(b), 0);
	/* the machine back where it was, the future's picture kept on screen */
	ce_session_load_state(s_, state.data(), state.size());
	if (drawn) publishPictureOnMachine();
}

void SessionViewModel::publishPictureOnMachine()
{
	const uint32_t *px = ce_session_video(s_);
	const int32_t w = ce_session_video_width(s_), h = ce_session_video_height(s_);
	if (px == nullptr || w <= 0 || h <= 0) return;
	double shownWidth = 0, shownHeight = 0;
	shownSize(s_, shownWidth, shownHeight);
	screen.publish(px, w, h, shownWidth, shownHeight);
}

void SessionViewModel::shownSize(ce_session *s, double &width, double &height)
{
	int32_t ax = 0, ay = 0;
	width = ce_session_virtual_width(s);
	height = ce_session_virtual_height(s);
	if (ce_session_display_aspect(s, &ax, &ay) != 0 && ax > 0 && ay > 0)
	{
		height = ce_session_video_height(s);
		width = height * ax / ay;
	}
}

void SessionViewModel::setScriptIdle(std::function<bool()> wanted, std::function<void()> idle)
{
	waitOnMachine([&] {
		scriptsWantedM_ = std::move(wanted);
		scriptsIdleM_ = std::move(idle);
	}, true);
}

void SessionViewModel::scriptTurnOnMachine(const std::function<void()> &turn)
{
	if (!turn || inScriptTurnM_ || !scriptsNowOnMachine()) return;
	inScriptTurnM_ = true;
	turn();
	inScriptTurnM_ = false;
}

int SessionViewModel::addMachineHooks(MachineJob frame, MachineJob closing)
{
	const int id = nextHook_++;
	submit([this, id, frame = std::move(frame), closing = std::move(closing)] { hooksM_.push_back({id, frame, closing}); });
	return id;
}

void SessionViewModel::removeMachineHooks(int id)
{
	waitOnMachine([&] { std::erase_if(hooksM_, [id](const Hooks &h) { return h.id == id; }); });
}

void SessionViewModel::freeProjectOnMachine()
{
	/* let go cleanly: what was kept against a crash goes too, unless the
	 * person closed without saving after the core stopped */
	if (!keepWorkM_) dropRecoveryOnMachine(recoveryDirOf(projectM_));
	if (projectM_ != nullptr) ce_project_free(projectM_);
	projectM_ = nullptr;
	projectPathM_.clear();
	crash::describeSession("no project open");
}

void SessionViewModel::openProjectOnMachine(const std::string &path, const std::string &packageGiven, bool keepInputs,
	ce_project *reuse)
{
	const std::string kept = keepInputs && s_ != nullptr ? movieTextOnMachine() : std::string();
	const uint64_t keptRerecords = keepInputs && s_ != nullptr ? ce_session_rerecords(s_) : 0;
	freeSessionOnMachine();
	runningM_ = false;
	/* nothing is open after a failed open: the machine went before it, and
	 * the project that was open goes with it, as a close would take it */
	auto fail = [this](const std::string &why) {
		freeProjectOnMachine();
		later([this, why] {
			status.set(why);
			openFailures.set(openFailures.get() + 1);
			isOpen.set(false);
			running.set(false);
			projectPath.set("");
			projectTitle.set("");
			projectId.set("");
			hasProject.set(false);
			projectSwitched();
		});
	};

	/* reuse: the project already open, changed in memory (its settings) */
	const char *error = nullptr;
	ce_project *p = reuse != nullptr ? reuse : ce_project_open(path.c_str(), &error);
	if (p == nullptr)
	{
		fail(tr("session.could_not_open", {path, error != nullptr ? error : tr("common.no_reason")}));
		return;
	}
	if (reuse != nullptr) projectM_ = nullptr; /* taken over below, not freed */
	/* work a crash left behind comes back before anything else, as
	 * recovery offered: kept as a copy among the backups, and
	 * offered; the open waits for the answer */
	if (!keepInputs && reuse == nullptr)
	{
		std::string copy, upTo, crashSaid, crashNote;
		if (recoveredCopyOnMachine(p, path, copy, upTo, crashSaid, crashNote))
		{
			if (pendingOpenM_.project != nullptr) ce_project_free(pendingOpenM_.project);
			freeProjectOnMachine();
			pendingOpenM_ = PendingOpen{p, path, packageGiven, keepInputs, reuse, kept, keptRerecords};
			later([this, copy, upTo, crashSaid, crashNote] {
				isOpen.set(false);
				running.set(false);
				projectPath.set("");
				projectTitle.set("");
				projectId.set("");
				hasProject.set(false);
				projectSwitched();
				resolvingFiles.set(false);
				recoveredUpTo.set(upTo);
				recoveredCrash.set(crashSaid);
				recoveredCrashNote.set(crashNote);
				recoveredCopy.set(copy);
				status.set(tr("session.recovered_kept"));
			});
			return;
		}
	}
	findFilesAndBootOnMachine(PendingOpen{p, path, packageGiven, keepInputs, reuse, kept, keptRerecords});
}

void SessionViewModel::findFilesAndBootOnMachine(PendingOpen open)
{
	ce_project *p = open.project;
	const std::string &path = open.path;
	/* the files are looked for beside the project; a project not saved yet
	 * has them where they were picked */
	if (!path.empty())
	{
		const size_t slash = path.find_last_of("/\\");
		const std::string dir = slash == std::string::npos ? "." : path.substr(0, slash);
		ce_project_resolve_dir(p, dir.c_str());
	}
	/* then where this machine last found them, each checked by its hash */
	applyLocalPathsOnMachine(p);
	/* another open: what was located for one before is not this one's */
	missingFirmwareM_.clear();
	locatedFirmwareM_.clear();
	if (ce_project_files_ok(p) == 0)
	{
		/* not found beside it, nor where this machine last had them: asked
		 * for, as Chimera's resolution window asks, the project waiting */
		waitForFilesOnMachine(std::move(open), tr("session.files_not_found"));
		return;
	}
	recoveredM_ = open.recovered;
	bootProjectOnMachine(p, path, open.package, open.keepInputs, open.reuse, open.kept, open.keptRerecords, 0);
}

void SessionViewModel::waitForFilesOnMachine(PendingOpen open, const std::string &said)
{
	if (pendingOpenM_.project != nullptr && pendingOpenM_.project != open.project) ce_project_free(pendingOpenM_.project);
	/* the machine is closed already; the project it ran goes too, unless it is the one waiting */
	if (open.reuse == nullptr) freeProjectOnMachine();
	pendingOpenM_ = std::move(open);
	publishProjectFilesOnMachine();
	later([this, said] {
		isOpen.set(false);
		running.set(false);
		projectPath.set("");
		projectTitle.set("");
		projectId.set("");
		hasProject.set(false);
		projectSwitched();
		recoveredCopy.set("");
		recoveredUpTo.set("");
		resolvingFiles.set(true);
		status.set(said);
	});
}

void SessionViewModel::bootProjectOnMachine(ce_project *p, const std::string &path, const std::string &packageGiven, bool keepInputs,
	ce_project *reuse, const std::string &kept, uint64_t keptRerecords, uint32_t extraFlags)
{
	const bool recovered = std::exchange(recoveredM_, false);
	/* nothing is open after a failed open: the machine went before it, and
	 * the project that was open goes with it, as a close would take it */
	auto fail = [this](const std::string &why) {
		freeProjectOnMachine();
		later([this, why] {
			status.set(why);
			openFailures.set(openFailures.get() + 1);
			isOpen.set(false);
			running.set(false);
			projectPath.set("");
			projectTitle.set("");
			projectId.set("");
			hasProject.set(false);
			projectSwitched();
		});
	};
	const char *error = nullptr;
	/* a package other than the one the project names opens, and says so */
	uint32_t flags = extraFlags;
	std::string note;
	const std::string pinned = ce_project_core_sha1(p) != nullptr ? ce_project_core_sha1(p) : "";
	std::string package = packageGiven;
	if (package.empty())
	{
		const std::string coreName = ce_project_core_name(p) != nullptr ? ce_project_core_name(p) : "";
		std::string found;
		if (findPackageM_) package = findPackageM_(coreName, pinned, found);
		if (package.empty())
		{
			ce_project_free(p); /* a reused one too: projectM_ let it go */
			fail(found.empty() ? tr("session.core_not_installed", {coreName}) : tr("session.core_cannot_run", {coreName, found}));
			return;
		}
		if (!found.empty()) note = "; " + found;
	}
	/* a package built for a guest ABI this Chimera does not run is not run, a given one too */
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package.c_str(), &perr))
		{
			uint64_t len = 0;
			const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
			json::Json cfg;
			std::string jerr;
			const std::string refused = d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, jerr)
				? CoresViewModel::abiRefusal(cfg) : std::string();
			ce_package_free(pk);
			if (!refused.empty())
			{
				ce_project_free(p);
				fail(tr("session.cannot_run_package", {package, refused}));
				return;
			}
		}
	}
	if (!pinned.empty())
	{
		const char *perr = nullptr;
		if (ce_package *pk = ce_package_open(package.c_str(), &perr))
		{
			const char *sha = ce_package_sha1(pk);
			if (sha == nullptr || pinned != sha)
			{
				flags |= CE_OPEN_ALLOW_CORE_MISMATCH;
				note = tr("session.other_package_note");
			}
			ce_package_free(pk);
		}
	}
	/* the firmware it needs, by the hashes it pins */
	Firmware fw;
	{
		uint64_t len = 0;
		const char *slots = ce_project_slots_text(p, &len);
		const std::string slotsText(slots != nullptr ? slots : "{}", slots != nullptr ? static_cast<size_t>(len) : 2);
		const char *settings = ce_project_settings_text(p, &len);
		const std::string settingsText(settings != nullptr ? settings : "{}", settings != nullptr ? static_cast<size_t>(len) : 2);
		const char *pins = ce_project_firmware_text(p, &len);
		const std::string pinsText(pins != nullptr ? pins : "", pins != nullptr ? static_cast<size_t>(len) : 0);
		/* the game is the project's first file; every file is mounted
		 * under its name */
		std::string game;
		std::vector<std::pair<std::string, std::string>> files;
		for (int32_t i = 0; i < ce_project_file_count(p); i++)
		{
			const char *name = ce_project_file_name(p, i), *source = ce_project_file_source_path(p, i);
			if (name == nullptr || source == nullptr || *source == '\0') continue;
			if (game.empty()) game = source;
			files.emplace_back(name, source);
		}
		std::string why;
		std::vector<MissingFirmware> missing;
		const bool found = firmwareOnMachine(package, slotsText, settingsText, pinsText, game, files, fw, why, &missing);
		locatedFirmwareM_.clear();
		if (!found && !missing.empty())
		{
			/* asked for, as the project's own files are (issue 397), the project waiting */
			missingFirmwareM_ = std::move(missing);
			waitForFilesOnMachine(PendingOpen{p, path, packageGiven, keepInputs, reuse, kept, keptRerecords, recovered},
				tr("session.firmware_not_found"));
			return;
		}
		missingFirmwareM_.clear();
		if (!found)
		{
			ce_project_free(p); /* a reused one too: projectM_ let it go */
			fail(why);
			return;
		}
	}
	std::vector<const char *> fwIds;
	std::vector<const uint8_t *> fwData;
	std::vector<uint64_t> fwLens;
	for (size_t i = 0; i < fw.ids.size(); i++)
	{
		fwIds.push_back(fw.ids[i].c_str());
		fwData.push_back(fw.data[i].data());
		fwLens.push_back(fw.data[i].size());
	}
	/* the game is the project's first file, and its hash is the one the project records */
	compileCacheOnMachine(package, ce_project_file_count(p) > 0 && ce_project_file_source_path(p, 0) != nullptr ? ce_project_file_source_path(p, 0) : "",
		ce_project_file_count(p) > 0 && ce_project_file_sha1(p, 0) != nullptr ? ce_project_file_sha1(p, 0) : "");
	{
		uint64_t len = 0;
		const char *settings = ce_project_settings_text(p, &len);
		requestGpu(package, settings != nullptr ? std::string(settings, static_cast<size_t>(len)) : std::string());
	}
	ce_session *s = ce_session_open_project(package.c_str(), p, nullptr, fwIds.data(), fwData.data(), fwLens.data(),
		static_cast<int32_t>(fwIds.size()), flags, &error);
	/* what was mounted is what the project records, as always: by the bytes */
	if (s != nullptr && !fw.ids.empty())
	{
		const char *ferr = nullptr;
		ce_project_set_firmware_text(p, fw.pins.c_str(), &ferr);
	}
	if (s == nullptr)
	{
		ce_project_free(p);
		fail(tr("session.machine_not_opened", {error != nullptr ? error : tr("common.no_reason")}));
		return;
	}

	/* the movie: the project's, given to the machine, which keeps it from
	 * now on and writes every line of it - or, started again, the one it had */
	if (keepInputs)
		loadMovieOnMachine(s, kept);
	else
	{
		uint64_t len = 0;
		const char *text = ce_project_log_text(p, &len);
		loadMovieOnMachine(s, std::string(text != nullptr ? text : "", static_cast<size_t>(len)));
	}
	/* the count goes on from the project's, or from where a restart was */
	ce_session_set_rerecords(s, keepInputs ? keptRerecords : ce_project_rerecords(p));
	/* the Greenzone box's choice, from TAStudio's part of the project */
	std::function<void()> columnsLater;
	if (!keepInputs)
	{
		int64_t period = 1;
		uint64_t tlen = 0;
		json::Json tastudio;
		std::string terr;
		if (const char *t = ce_project_tastudio_text(p, &tlen);
			t != nullptr && tlen > 0 && json::parse(std::string_view(t, static_cast<size_t>(tlen)), tastudio, terr))
			if (const json::Json *o = tastudio.find("o"))
				if (const json::Json *g = o->find("GreenzonePeriod")) period = std::max<int64_t>(0, static_cast<int64_t>(g->number()));
		periodM_.store(period);
		later([this, period] { greenzonePeriod.set(period); });
		/* the piano roll's columns: the first roll's hidden ones, as
		 * Chimera's TAStudio saved them; none saved, its defaults */
		std::vector<std::string> hidden, held;
		bool saved = false;
		if (const json::Json *o = tastudio.find("o"))
			if (const json::Json *rolls = o->find("Columns"); rolls != nullptr && rolls->kind == json::Json::Kind::Array && !rolls->items.empty())
			{
				saved = true;
				for (const json::Json &c : rolls->items[0].items)
				{
					const json::Json *name = c.find("Name");
					const json::Json *visible = c.find("Visible");
					const json::Json *emphasis = c.find("Emphasis");
					if (name != nullptr && visible != nullptr && visible->kind == json::Json::Kind::Bool && !visible->boolean)
						hidden.push_back(name->text);
					if (name != nullptr && name->text != "CursorColumn" && name->text != "FrameColumn" && emphasis != nullptr &&
						emphasis->kind == json::Json::Kind::Bool && emphasis->boolean)
						held.push_back(name->text);
				}
			}
		/* given once the window has this machine's buttons (adoptOnMachine,
		 * below): a project with none saved hides by them */
		columnsLater = [this, held, hidden, saved] {
			autoHeldColumnsLoaded.emit(held);
			if (saved) hiddenColumnsLoaded.emit(hidden);
			else loadDefaultColumns();
		};
		/* the rest of TAStudio's part, for the view models that keep it */
		std::map<std::string, std::string> members;
		if (const json::Json *o = tastudio.find("o"))
			for (const auto &[k, v] : o->members) members[k] = json::write(v);
		tastudioMembersM_.clear();
		later([this, members] { tastudioLoaded.emit(members); });
	}

	if (!keepInputs)
	{
		std::vector<Marker> markers;
		for (int32_t i = 0; i < ce_project_marker_count(p); i++)
		{
			const char *text = ce_project_marker_text(p, i);
			markers.push_back({ce_project_marker_frame(p, i), text != nullptr ? text : "", ce_project_marker_keep_state(p, i) != 0});
		}
		markersM_ = markers;
		later([this, markers] { markersLoaded.emit(markers); });
		/* the branches: their movies as text, as the project keeps them */
		std::vector<Branch> branches;
		for (int32_t i = 0; i < ce_project_branch_count(p); i++)
		{
			Branch b;
			b.name = ce_project_branch_name(p, i) != nullptr ? ce_project_branch_name(p, i) : "";
			b.time = ce_project_branch_time(p, i) != nullptr ? ce_project_branch_time(p, i) : "";
			b.frame = ce_project_branch_frame(p, i);
			uint64_t blen = 0;
			const char *btext = ce_project_branch_log_text(p, i, &blen);
			b.text.assign(btext != nullptr ? btext : "", static_cast<size_t>(blen));
			ce_movie_log *bl = ce_movie_log_new();
			if (!b.text.empty()) ce_movie_log_parse(bl, b.text.data(), b.text.size());
			b.frames = ce_movie_log_count(bl);
			ce_movie_log_free(bl);
			for (int32_t k = 0; k < ce_project_branch_marker_count(p, i); k++)
			{
				const char *t = ce_project_branch_marker_text(p, i, k);
				b.markers.push_back({ce_project_branch_marker_frame(p, i, k), t != nullptr ? t : "",
					ce_project_branch_marker_keep_state(p, i, k) != 0});
			}
			b.state = branch_state::nameFor(b.time, b.frame, b.text);
			branches.push_back(std::move(b));
		}
		branchesM_ = branches;
		later([this, branches] { branchesLoaded.emit(branches); });
	}
	if (projectM_ != nullptr && projectM_ != p)
	{
		dropRecoveryOnMachine(recoveryDirOf(projectM_));
		ce_project_free(projectM_);
	}
	projectM_ = p;
	projectPathM_ = path;
	rememberLocalPathsOnMachine();
	package_ = package;
	forgetElsewhereCacheOnMachine(path);
	tidyBranchStatesOnMachine();
	rememberAboutOnMachine(path);
	const std::string title = ce_project_title(p) != nullptr ? ce_project_title(p) : "";
	ProjectAnswers answers;
	answers.coreName = ce_project_core_name(p) != nullptr ? ce_project_core_name(p) : "";
	answers.package = package;
	{
		uint64_t len = 0;
		const char *settings = ce_project_settings_text(p, &len);
		answers.settings = settings != nullptr ? std::string(settings, static_cast<size_t>(len)) : "{}";
	}
	for (int32_t i = 0; i < ce_project_file_count(p); i++)
	{
		const char *slot = ce_project_file_slot(p, i), *source = ce_project_file_source_path(p, i);
		if (source != nullptr && *source != '\0') answers.files.emplace_back(slot != nullptr ? slot : "", source);
	}
	const std::string id = ce_project_id(p) != nullptr ? ce_project_id(p) : "";
	/* the project is known by the time the machine says it is open: posted
	 * after it, a person (or a test) acting on "open" met no project, and
	 * recording turned on then was turned off by the project's arrival */
	adoptOnMachine(s, package, (title.empty() ? path : title) + note, true, [this, path, title, answers, id] {
		projectPath.set(path);
		projectTitle.set(title);
		projectId.set(id);
		hasProject.set(true);
		projectSwitched();
		lastAnswers_ = answers;
		if (!path.empty()) addRecent(path);
		recoveredCopy.set("");
		recoveredUpTo.set("");
	});
	/* the recovered work is not in the project's file until it is saved */
	if (recovered)
		later([this, path] {
			markChanged();
			status.set(tr("session.recovered_open", {path}));
		});
	if (columnsLater) later(std::move(columnsLater));
	{
		/* the game a project plays: its first file, where it was found */
		const char *source = ce_project_file_count(p) > 0 ? ce_project_file_source_path(p, 0) : nullptr;
		const std::string game = source != nullptr && *source != '\0' ? source : path;
		romFileM_ = source != nullptr ? source : "";
		std::error_code ec;
		const std::filesystem::path where = std::filesystem::weakly_canonical(game, ec);
		carryFreezesOnMachine(package + "\n" + (ec ? game : where.string()));
	}
	{
		const char *first = ce_project_file_count(p) > 0 ? ce_project_file_name(p, 0) : nullptr;
		std::string stem = first != nullptr ? std::filesystem::path(first).stem().string() : title;
		later([this, stem] { gameName.set(stem); });
	}
	/* the game time the project was saved with, as the end of the movie's
	 * own: an edit before the end forgets it, as it would one the machine
	 * had just shown */
	if (!keepInputs && !recovered && gameTimerM_)
	{
		const char *atText = ce_project_header_get(p, "GameTimeFrame"), *msText = ce_project_header_get(p, "GameTimeMs");
		char *atEnd = nullptr, *msEnd = nullptr;
		const long long at = atText != nullptr && *atText != '\0' ? std::strtoll(atText, &atEnd, 10) : -1;
		const long long ms = msText != nullptr && *msText != '\0' ? std::strtoll(msText, &msEnd, 10) : 0;
		if (at >= 0 && atEnd != nullptr && *atEnd == '\0' && msEnd != nullptr && *msEnd == '\0' && at == ce_session_movie_length(s_))
		{
			if (gameTimeLogM_.size() <= static_cast<size_t>(at)) gameTimeLogM_.resize(static_cast<size_t>(at) + 1, kNoGameTime);
			gameTimeLogM_[static_cast<size_t>(at)] = ms;
		}
	}
	/* what a crash note says of the session: the project, the system and the
	 * movie's header; and where the GPU bridge keeps its last calls */
	{
		std::string about = "project=" + path + "\nsystem=" + std::string(ce_session_system_id(s_) != nullptr ? ce_session_system_id(s_) : "");
		for (int32_t i = 0; i < ce_project_header_count(p); i++)
		{
			const char *k = ce_project_header_key_at(p, i), *v = ce_project_header_value_at(p, i);
			const std::string value = v != nullptr ? v : "";
			if (k != nullptr && !value.empty() && value.size() < 240 && value.find('\n') == std::string::npos)
				about += "\n" + std::string(k) + "=" + value;
		}
		crash::describeSession(about);
		uint32_t recorderBytes = 0;
		const void *recorder = ce_gl_flight_recorder(&recorderBytes);
		crash::noteGlRecorder(recorder, recorderBytes);
	}
	startProjectSessionOnMachine(!keepInputs);
}

void SessionViewModel::checkHistoryOnMachine(const std::string &keepIn)
{
	if (s_ == nullptr) return;
	auto say = [this](std::string text) { later([this, text] { status.set(text); }); };
	const int64_t last = ce_session_greenzone_nearest(s_, INT64_MAX);
	if (last <= 0)
	{
		say(tr("session.check_nothing"));
		return;
	}
	/* From frame 0 as the history keeps it, as Chimera's check starts - and
	 * put there twice. Seen 2026-10-23 on the synthetic core: frame 0 put back
	 * from a machine far ahead keeps pages its state does not have (the
	 * machine saves two pages more from then on), while put back from a
	 * machine already on the history's own line it does not; the first
	 * comparison failed on that alone, with nothing desynced. The second
	 * time it is put back from a machine the history made. */
	movesM_++;
	for (int i = 0; i < 2; i++)
	{
		if (ce_session_greenzone_restore(s_, 0) != 0)
		{
			say(tr("session.check_no_frame_0"));
			return;
		}
		if (i == 0 && stepOnMachine(false, true) < 0) return;
	}
	frameM_ = ce_session_frame(s_);
	auto saved = [this] {
		uint64_t len = 0;
		const uint8_t *d = ce_session_save_state(s_, &len);
		return d != nullptr ? std::vector<uint8_t>(d, d + len) : std::vector<uint8_t>();
	};
	int64_t lastState = 0, checked = 0;
	while (frameM_ < last)
	{
		/* each frame run as Frame Advance runs it, drawn: a machine whose
		 * picture is in its memory is not the same undrawn */
		if (stepOnMachine(false, true) < 0) return;
		const int64_t f = frameM_;
		if (ce_session_greenzone_nearest(s_, f) != f) continue;
		const std::vector<uint8_t> fresh = saved();
		ce_session_greenzone_restore(s_, f);
		const std::vector<uint8_t> kept = saved();
		checked++;
		if (fresh != kept)
		{
			std::string text = tr("session.check_failed", {std::to_string(lastState), std::to_string(f)});
			if (!keepIn.empty())
			{
				std::error_code ec;
				std::filesystem::create_directories(keepIn, ec);
				std::ofstream(std::filesystem::path(keepIn) / "integrity.fresh", std::ios::binary)
					.write(reinterpret_cast<const char *>(fresh.data()), static_cast<std::streamsize>(fresh.size()));
				std::ofstream(std::filesystem::path(keepIn) / "integrity.greenzoned", std::ios::binary)
					.write(reinterpret_cast<const char *>(kept.data()), static_cast<std::streamsize>(kept.size()));
				text += tr("session.check_states_kept", {keepIn});
			}
			report(-1, -1, -1, -1, -1);
			say(text);
			return;
		}
		lastState = f;
	}
	report(-1, -1, -1, -1, -1);
	say(tr("session.check_passed", checked));
}

void SessionViewModel::catchPollOnMachine()
{
	catchDue_ = Clock::now() + std::chrono::milliseconds(16);
	if (!padsM_) return;
	std::vector<std::string> held;
	/* an axis: caught once it has moved 20000 in all, a stick
	 * wiggled, so one resting off its centre is not taken */
	if (catchingAxisM_)
	{
		std::vector<std::pair<std::string, int>> axes;
		padsM_->poll(held, &axes);
		for (const auto &[name, v] : axes)
		{
			int was = v;
			for (const auto &[n, old] : catchAxesFromM_)
				if (n == name) was = old;
			const int moved = (catchMovedM_[name] += std::abs(v - was));
			if (moved >= 20000)
			{
				catchingM_ = catchingAxisM_ = false;
				later([this, name] {
					caughtPad.set(name);
					catchingPad.set(false);
				});
				return;
			}
		}
		catchAxesFromM_ = std::move(axes);
		return;
	}
	padsM_->poll(held);
	for (const std::string &name : held)
		if (std::find(catchHeldM_.begin(), catchHeldM_.end(), name) == catchHeldM_.end())
		{
			catchingM_ = false;
			later([this, name] {
				caughtPad.set(name);
				catchingPad.set(false);
			});
			return;
		}
	/* let go since: pressed again, it counts */
	catchHeldM_ = held;
}

bool SessionViewModel::hiddenByDefault(const std::string &n)
{
	auto starts = [&n](const char *p) { return n.rfind(p, 0) == 0; };
	auto ends = [&n](const std::string &e) { return n.size() >= e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0; };
	return n == "Power" || n == "Reset" || n == "Light Sensor" || n == "Disc Select" || n == "Disk Index" || n == "Next Drive" ||
		n == "Next Slot" || n == "Insert Disk" || n == "Eject Disk" || starts("Tilt") || starts("Key ") || starts("Open") ||
		starts("Close") || ends("Tape") || ends("Disk") || ends("Block") || ends("Status");
}

void SessionViewModel::loadDefaultColumns()
{
	std::vector<std::string> hidden;
	for (const std::string &n : buttonNames_)
		if (hiddenByDefault(n)) hidden.push_back(n);
	for (const Axis &a : axes_)
		if (hiddenByDefault(a.name)) hidden.push_back(a.name);
	hiddenColumnsLoaded.emit(hidden);
}

void SessionViewModel::setAutoHeldColumns(const std::vector<std::string> &names)
{
	submit([this, names] { autoHeldColumnsM_ = names; });
}

void SessionViewModel::setColumnPatterns(std::vector<ColumnPattern> patterns)
{
	submit([this, patterns = std::move(patterns)] {
		columnPatternsM_ = patterns;
		columnAtM_.assign(columnPatternsM_.size(), 0);
	});
}

void SessionViewModel::setTastudioMember(const std::string &name, const std::string &json)
{
	submit([this, name, json] { tastudioMembersM_[name] = json; });
}

void SessionViewModel::setHiddenColumns(const std::vector<std::string> &names)
{
	/* a roll of Chimera's shape: its cursor and frame columns, then a column
	 * a control, named, its mnemonic for a label, as a new piano roll makes */
	json::Json roll;
	roll.kind = json::Json::Kind::Array;
	auto column = [&](const std::string &name, const std::string &text, bool visible, bool rotatable) {
		json::Json c;
		c.kind = json::Json::Kind::Object;
		c.members.emplace_back("VerticalWidth", json::Json::integer(10));
		c.members.emplace_back("HorizontalHeight", json::Json::integer(10));
		c.members.emplace_back("Left", json::Json::integer(0));
		c.members.emplace_back("Right", json::Json::integer(0));
		c.members.emplace_back("Name", json::Json::string(name));
		c.members.emplace_back("Text", json::Json::string(text));
		c.members.emplace_back("Visible", json::Json::boolean_(visible));
		c.members.emplace_back("Emphasis", json::Json::boolean_(false));
		c.members.emplace_back("Rotatable", json::Json::boolean_(rotatable));
		roll.items.push_back(c);
	};
	auto shown = [&names](const std::string &n) { return std::find(names.begin(), names.end(), n) == names.end(); };
	column("CursorColumn", "", true, false);
	column("FrameColumn", "Frame#", true, true);
	for (size_t b = 0; b < buttonNames_.size(); b++)
	{
		const std::string mn = b < mnemonics_.size() && mnemonics_[b] != '.' ? std::string(1, mnemonics_[b]) : buttonNames_[b];
		column(buttonNames_[b], mn, shown(buttonNames_[b]), false);
	}
	for (const Axis &a : axes_) column(a.name, a.header.empty() ? a.name : a.header, shown(a.name), true);
	const std::string fresh = json::write(roll);
	submit([this, names, fresh] {
		hiddenColumnsM_ = names;
		freshColumnsM_ = fresh;
	});
}

void SessionViewModel::openMadeProjectOnMachine(ce_project *p, const std::string &package)
{
	/* the project open before is done with: this one takes its place */
	freeSessionOnMachine();
	freeProjectOnMachine();
	openProjectOnMachine("", package, false, p);
}

void SessionViewModel::addRecent(const std::string &path)
{
	/* newest first, each once, ten at most */
	std::string list = path, rest = recentProjects.get();
	int kept = 1;
	size_t at = 0;
	while (at <= rest.size() && kept < 10)
	{
		const size_t end = rest.find('\n', at);
		const std::string line = rest.substr(at, end == std::string::npos ? std::string::npos : end - at);
		if (!line.empty() && line != path)
		{
			list += "\n" + line;
			kept++;
		}
		if (end == std::string::npos) break;
		at = end + 1;
	}
	recentProjects.set(list);
}

bool SessionViewModel::saveProjectOnMachine(const std::string &to, bool backup, bool quiet)
{
	if (projectM_ == nullptr || s_ == nullptr) return false;
	const std::string path = to.empty() ? projectPathM_ : to;
	if (path.empty())
	{
		later([this] { status.set(tr("session.not_saved_no_file")); });
		return false;
	}
	/* the movie as the machine keeps it: the engine wrote every line */
	const int64_t n = ce_session_movie_length(s_);
	const std::string log = movieTextOnMachine();
	ce_project_set_log_text(projectM_, log.data(), log.size());
	ce_project_set_rerecords(projectM_, ce_session_rerecords(s_));
	/* the Greenzone box's choice, into TAStudio's part, whatever else is there */
	{
		uint64_t tlen = 0;
		json::Json tastudio;
		std::string terr;
		const char *t = ce_project_tastudio_text(projectM_, &tlen);
		if (t == nullptr || tlen == 0 || !json::parse(std::string_view(t, static_cast<size_t>(tlen)), tastudio, terr) ||
			tastudio.kind != json::Json::Kind::Object)
		{
			tastudio = json::Json();
			tastudio.kind = json::Json::Kind::Object;
		}
		json::Json *o = nullptr;
		for (auto &[k, v] : tastudio.members)
			if (k == "o") o = &v;
		if (o == nullptr || o->kind != json::Json::Kind::Object)
		{
			if (o == nullptr)
			{
				tastudio.members.emplace_back("o", json::Json());
				o = &tastudio.members.back().second;
			}
			*o = json::Json();
			o->kind = json::Json::Kind::Object;
		}
		/* a type name older projects carry there, which nothing reads */
		o->members.erase(std::remove_if(o->members.begin(), o->members.end(), [](const auto &m) { return m.first == "$type"; }),
			o->members.end());
		bool set = false;
		for (auto &[k, v] : o->members)
			if (k == "GreenzonePeriod")
			{
				v = json::Json::integer(periodM_.load());
				set = true;
			}
		if (!set) o->members.emplace_back("GreenzonePeriod", json::Json::integer(periodM_.load()));
		/* the piano roll's columns: shown or hidden on the project's own
		 * entries (their widths and the rest left as Chimera saved them),
		 * else a roll of Chimera's shape */
		json::Json *rolls = nullptr;
		for (auto &[k, v] : o->members)
			if (k == "Columns" && v.kind == json::Json::Kind::Array && !v.items.empty()) rolls = &v;
		json::Json fresh;
		std::string ferr;
		const bool haveFresh = !freshColumnsM_.empty() && json::parse(freshColumnsM_, fresh, ferr);
		if (rolls != nullptr && haveFresh)
		{
			for (json::Json &roll : rolls->items)
				for (json::Json &c : roll.items)
				{
					const json::Json *name = c.find("Name");
					if (name == nullptr || name->text == "CursorColumn" || name->text == "FrameColumn") continue;
					const bool hide = std::find(hiddenColumnsM_.begin(), hiddenColumnsM_.end(), name->text) != hiddenColumnsM_.end();
					const bool emphasis = std::find(autoHeldColumnsM_.begin(), autoHeldColumnsM_.end(), name->text) != autoHeldColumnsM_.end();
					for (auto &[ck, cv] : c.members)
					{
						if (ck == "Visible") cv = json::Json::boolean_(!hide);
						if (ck == "Emphasis") cv = json::Json::boolean_(emphasis);
					}
				}
		}
		else if (haveFresh)
		{
			for (json::Json &c : fresh.items)
				if (const json::Json *name = c.find("Name"))
				{
					const bool emphasis = std::find(autoHeldColumnsM_.begin(), autoHeldColumnsM_.end(), name->text) != autoHeldColumnsM_.end();
					for (auto &[ck, cv] : c.members)
						if (ck == "Emphasis") cv = json::Json::boolean_(emphasis);
				}
			json::Json all;
			all.kind = json::Json::Kind::Array;
			all.items.push_back(fresh);
			o->members.emplace_back("Columns", all);
		}
		/* what the view models keep there, over what the project had */
		for (const auto &[name, text] : tastudioMembersM_)
		{
			json::Json value;
			std::string verr;
			if (!json::parse(text, value, verr)) continue;
			bool there = false;
			for (auto &[k, v] : o->members)
				if (k == name)
				{
					v = value;
					there = true;
				}
			if (!there) o->members.emplace_back(name, value);
		}
		const char *err = nullptr;
		ce_project_set_tastudio_text(projectM_, json::write(tastudio).c_str(), &err);
	}
	for (int32_t i = ce_project_marker_count(projectM_) - 1; i >= 0; i--) ce_project_marker_remove(projectM_, i);
	for (const Marker &m : markersM_) ce_project_marker_add(projectM_, m.frame, m.text.c_str(), m.keepState ? 1 : 0);
	for (int32_t i = ce_project_branch_count(projectM_) - 1; i >= 0; i--) ce_project_branch_remove(projectM_, i);
	for (const Branch &b : branchesM_)
	{
		ce_project_branch_add(projectM_, b.name.c_str(), b.frame, b.time.c_str(), b.text.data(), b.text.size());
		const int32_t at = ce_project_branch_count(projectM_) - 1;
		for (const Marker &m : b.markers) ce_project_branch_marker_add(projectM_, at, m.frame, m.text.c_str(), m.keepState ? 1 : 0);
	}
	gameTimeHeadersOnMachine(projectM_, n);
	/* Saved under another name, the project becomes a project of its own
	 * (owner, 2026-10-29). The two files used to share one id and so one
	 * cache folder: the one opened next took the history the other's save
	 * had written, states made for other input, and seeking back found
	 * nothing to start from. A backup is the same project kept, and keeps
	 * its id. */
	namespace fs = std::filesystem;
	const bool another = !backup && !projectPathM_.empty() && !samePlace(projectPathM_, path);
	const fs::path cacheWas = another ? projectCacheOnMachine() : fs::path();
	const fs::path recoveryWas = another ? recoveryDirOf(projectM_) : fs::path();
	const char *error = nullptr;
	if ((another ? ce_project_save_as(projectM_, path.c_str(), &error) : ce_project_save(projectM_, path.c_str(), &error)) != 0)
	{
		const std::string why = error != nullptr ? error : tr("common.no_reason");
		if (!quiet) later([this, path, why] { status.set(tr("session.could_not_save", {path, why})); });
		return false;
	}
	if (backup)
	{
		if (!quiet) later([this, path] { status.set(tr("session.backup_saved", {path})); });
		return true;
	}
	if (another) becameAnotherOnMachine(cacheWas, recoveryWas);
	const bool first = projectPathM_ != path;
	projectPathM_ = path;
	keepHistoryOnMachine();
	rememberAboutOnMachine(path);
	rememberLocalPathsOnMachine();
	const std::string id = ce_project_id(projectM_) != nullptr ? ce_project_id(projectM_) : "";
	later([this, path, n, first, id] {
		projectPath.set(path);
		projectId.set(id);
		if (first) addRecent(path);
		status.set(tr("session.saved_frames", n, {path}));
	});
	return true;
}

void SessionViewModel::passOnMachine(int64_t frames, bool stepping, bool goingBack)
{
	/* as Chimera does: the engine is told of each pass,
	 * and decides what counts; time spent paused never does */
	if (averageM_ == nullptr) averageM_ = ce_average_fps_create();
	if (averageM_ == nullptr) return;
	if (averageResetM_.exchange(false))
	{
		ce_average_fps_reset(averageM_);
		pausedSincePassM_ = true;
	}
	const auto now = Clock::now();
	if (passAt_ != Clock::time_point{})
	{
		const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now - passAt_).count();
		ce_average_fps_pass(averageM_, frames, ns, pausedSincePassM_ ? 1 : 0, stepping ? 1 : 0, goingBack ? 1 : 0);
	}
	passAt_ = now;
	/* a pass not made running leaves the next one paused (what it waited is not run time) */
	pausedSincePassM_ = stepping || goingBack;
	const double average = ce_average_fps_value(averageM_);
	std::lock_guard<std::mutex> lock(pendingM_);
	pendingAverage_ = average;
}

void SessionViewModel::saveTo(const std::string &path)
{
	const uint64_t gen = changeGen_;
	submit([this, path, gen] {
		if (saveProjectOnMachine(path)) later([this, gen] { savedAt(gen); });
	});
}

std::string SessionViewModel::backupPathFor(const std::string &projectFile) const
{
	/* "walker.2026-10-23 06.26.46.chimeraProject" */
	const std::filesystem::path own(projectFile);
	const std::time_t now = std::time(nullptr);
	char when[32];
	std::strftime(when, sizeof when, "%Y-%m-%d %H.%M.%S", std::localtime(&now));
	std::error_code ec;
	std::filesystem::create_directories(backupFolder.get(), ec);
	return (std::filesystem::path(backupFolder.get()) / (own.stem().string() + "." + when + own.extension().string())).string();
}

void SessionViewModel::markChanged()
{
	if (!hasProject.get()) return;
	changeGen_++;
	unsavedM_.store(true);
	if (!unsaved.get()) unsaved.set(true);
	/* the machine keeps the work against a crash from now: a paused one is woken to */
	{
		std::lock_guard<std::mutex> lock(m_);
	}
	cv_.notify_all();
}

void SessionViewModel::savedAt(uint64_t gen)
{
	/* a change made while the save was on its way is still unsaved */
	if (gen != changeGen_) return;
	unsavedM_.store(false);
	unsaved.set(false);
	/* the work kept against a crash is in the project now; one kept between
	 * the save and this goes too, and a change since is kept again at once */
	submit([this] {
		if (!unsavedM_.load()) dropRecoveryOnMachine(recoveryDirOf(projectM_));
	});
}

void SessionViewModel::projectSwitched()
{
	/* a project opens read only, as Chimera's TAStudio does */
	recordingMode.set(false);
	changeGen_++;
	unsavedM_.store(false);
	unsaved.set(false);
}

void SessionViewModel::autosave()
{
	/* as Chimera's: a project with a file of its own, changed since it was
	 * last saved; a backup leaves it unsaved, as Chimera's does */
	if (!hasProject.get() || projectPath.get().empty() || !unsaved.get()) return;
	if (autosaveAsBackup.get())
	{
		if (backupFolder.get().empty()) return;
		const std::string path = backupPathFor(projectPath.get());
		submit([this, path] { saveProjectOnMachine(path, true); });
	}
	else
		saveTo("");
}

std::vector<uint8_t> SessionViewModel::readDomain(const std::string &name)
{
	std::vector<uint8_t> bytes;
	waitOnMachine([&] {
		if (s_ != nullptr)
			for (int32_t i = 0; i < ce_session_domain_count(s_); i++)
				if (name == ce_session_domain_name(s_, i))
				{
					bytes.resize(static_cast<size_t>(ce_session_domain_size(s_, i)));
					ce_session_domain_read(s_, i, 0, bytes.data(), static_cast<int64_t>(bytes.size()));
					break;
				}
	});
	return bytes;
}

void SessionViewModel::adoptOnMachine(ce_session *s, const std::string &package, const std::string &label, bool inputsReplaced,
	std::function<void()> withOpen)
{
	s_ = s;
	/* a script's input-poll functions go on with the machine opened again */
	applyInputPollsOnMachine();
	packageSha1M_.clear();
	diedM_ = false;
	inputs_.bind(s);
	inputs_.clearLags();
	frameM_ = ce_session_frame(s);
	greenEndM_ = 0;
	gameTimeLogM_.clear();
	/* a history of the machine's states, so a seek replays from the nearest
	 * one rather than from power-on: as Chimera's, every project's budgets
	 * unless the project asks for its own; the closeness before enabling,
	 * so the first stretch is held to it */
	int64_t ownMb = 0, ownStride = 0;
	readBudgetsOnMachine(projectM_, ownMb, ownStride);
	const int64_t mb = std::max<int64_t>(ownMb != 0 ? ownMb : budgetMbM_.load(), 64);
	const int64_t stride = std::clamp<int64_t>(ownStride != 0 ? ownStride : nearStrideM_.load(), 1, 32);
	ce_session_greenzone_max_near_stride(s, stride);
	ce_session_greenzone_picture_settle(s, static_cast<int32_t>(std::clamp<int64_t>(pictureSettleM_.load(), 0, 10)));
	ce_session_greenzone_enable(s, static_cast<uint64_t>(mb) * 1024 * 1024);
	later([this, ownMb, ownStride, mb, stride] {
		projectBudgetMb.set(ownMb);
		projectNearStride.set(ownStride);
		budgetMbApplied.set(mb);
		nearStrideApplied.set(stride);
	});
	ce_session_greenzone_capture_period(s, periodM_.load());
	pinMarkersOnMachine();

	std::vector<std::string> names;
	std::vector<int32_t> players;
	const int64_t count = ce_session_button_count(s);
	for (int64_t i = 0; i < count; i++)
	{
		const char *n = ce_session_button_name(s, i);
		names.emplace_back(n != nullptr ? n : "");
		players.push_back(ce_session_player_of(s, names.back().c_str()));
	}
	const char *mn = ce_session_button_mnemonics(s);
	const std::string mnemonics = mn != nullptr ? mn : "";
	/* what heads each button's column in TAStudio, as Chimera's: the package's
	 * header for it, else its letter */
	std::vector<std::string> headers;
	for (size_t i = 0; i < names.size(); i++)
	{
		const char *h = ce_session_button_header_of(s, names[i].c_str());
		headers.emplace_back(h != nullptr && *h != '\0' ? std::string(h)
			: i < mnemonics.size() && mnemonics[i] != '.' ? std::string(1, mnemonics[i]) : names[i]);
	}

	std::string keybinds, config;
	const char *perr = nullptr;
	if (ce_package *p = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry(p, "default_keybinds.json", &len))
			keybinds.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		if (const uint8_t *d = ce_package_entry_texts(p, "waterbox.config", &len))
			config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		ce_package_free(p);
	}
	/* the analog controls: the engine's names, headers and rest values, the
	 * ranges as the package declares them (its own input, or a machine's) */
	std::vector<Axis> axes;
	std::string controller = "Waterbox Controller", picture;
	bool gameCore = false;
	std::string movieImport;
	int64_t ms = 0;
	gameTimerM_ = ce_session_game_time_ms(s, &ms) == 1;
	const bool timer = gameTimerM_, saveData = ce_session_savedata_available(s) != 0;
	const bool traces = ce_session_trace_available(s) != 0, registers = ce_session_register_count(s) > 0;
	json::Json surfaceList;
	surfaceList.kind = json::Json::Kind::Array;
	for (int32_t i = 0; i < ce_session_surface_count(s); i++)
	{
		json::Json one;
		one.kind = json::Json::Kind::Object;
		const char *n = ce_session_surface_name(s, i);
		one.members.emplace_back("name", json::Json::string(n != nullptr ? n : ""));
		one.members.emplace_back("width", json::Json::integer(ce_session_surface_width(s, i)));
		one.members.emplace_back("height", json::Json::integer(ce_session_surface_height(s, i)));
		surfaceList.items.push_back(one);
	}
	const std::string surfaceText = json::write(surfaceList);
	const std::string traceHead = traces && ce_session_trace_header(s) != nullptr ? ce_session_trace_header(s) : "";
	/* a trace asked for goes on with the next machine */
	if (traces && traceOnM_) ce_session_trace_enable(s, 1);
	{
		json::Json cfg;
		std::string jerr;
		const bool read = json::parse(config, cfg, jerr);
		/* the controller is the machine's when the package is more than one, else the
		 * package's: its name and the picture of it */
		if (read)
		{
			const json::Json *input = cfg.find("input");
			/* the machine the engine chose by the machine setting, as Chimera's
			 * did: several may share a system id */
			const int32_t machine = ce_session_machine_index(s);
			if (const json::Json *machines = cfg.find("machines"); machines != nullptr && machine >= 0 &&
				static_cast<size_t>(machine) < machines->items.size())
				if (const json::Json *own = machines->items[static_cast<size_t>(machine)].find("input")) input = own;
			if (input != nullptr)
			{
				if (const json::Json *n = input->find("name"); n != nullptr && !n->text.empty()) controller = n->text;
				if (const json::Json *pic = input->find("picture"); pic != nullptr && !pic->text.empty())
					if (ce_package *p = ce_package_open(package.c_str(), &perr))
					{
						uint64_t len = 0;
						if (const uint8_t *d = ce_package_entry(p, pic->text.c_str(), &len))
							picture.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
						ce_package_free(p);
					}
			}
		}
		if (read)
		{
			if (const json::Json *k = cfg.find("kind")) gameCore = k->text == "game";
			if (const json::Json *mi = cfg.find("movieImport"))
				if (const json::Json *m = mi->find("menu")) movieImport = m->text;
		}
		auto declared = [&](const std::string &name) -> const json::Json * {
			if (!read) return nullptr;
			auto look = [&](const json::Json *input) -> const json::Json * {
				if (input == nullptr) return nullptr;
				if (const json::Json *list = input->find("axes"))
					for (const json::Json &a : list->items)
						if (const json::Json *n = a.find("name"); n != nullptr && n->text == name) return &a;
				return nullptr;
			};
			if (const json::Json *a = look(cfg.find("input"))) return a;
			if (const json::Json *machines = cfg.find("machines"))
				for (const json::Json &m : machines->items)
					if (const json::Json *a = look(m.find("input"))) return a;
			return nullptr;
		};
		for (int64_t i = 0; i < ce_session_axis_count(s); i++)
		{
			Axis a;
			const char *n = ce_session_axis_name(s, i);
			a.name = n != nullptr ? n : "";
			const char *h = ce_session_axis_header_of(s, a.name.c_str());
			a.header = h != nullptr ? h : a.name;
			a.neutral = ce_session_axis_neutral(s, i);
			a.player = ce_session_player_of(s, a.name.c_str());
			if (const json::Json *d = declared(a.name))
			{
				if (const json::Json *v = d->find("min")) a.min = static_cast<int64_t>(v->number());
				if (const json::Json *v = d->find("max")) a.max = static_cast<int64_t>(v->number());
			}
			axes.push_back(a);
		}
	}

	std::string bindError;
	bindingsM_ = Bindings();
	if (!keybinds.empty()) bindingsM_.parse(keybinds, names, "AllTrollers", bindError, controller);
	/* the keys that fire a button while held, and the analog controls' bindings, as the package says */
	autofireBindingsM_ = Bindings();
	if (!keybinds.empty()) autofireBindingsM_.parse(keybinds, names, "AllTrollersAutoFire", bindError, controller);
	{
		std::vector<std::string> axisNames;
		for (const Axis &a : axes) axisNames.push_back(a.name);
		analogBindsM_ = keybinds.empty() ? std::vector<AnalogBind>(axes.size()) : parseAnalogBinds(keybinds, axisNames, bindError, controller);
		axesM_ = axes;
		buttonNamesM_ = names;
	}
	autofireWasM_.clear();
	if (!padsM_) padsM_ = std::make_unique<Gamepads>();
	padCount_ = 0;
	std::string padText = padsM_->available() ? tr("session.no_gamepads") : padsM_->problem();
	if (!soundM_) soundM_ = std::make_unique<Sound>(soundDeviceM_, static_cast<int32_t>(soundBufferMsM_));
	const std::string soundText = soundM_->available() ? tr("session.sound_on") : soundM_->problem();
	later([this, padText, soundText] { gamepads.set(padText); sound.set(soundText); });

	const int32_t num = ce_session_vsync_numerator(s), den = ce_session_vsync_denominator(s);
	if (num > 0 && den > 0)
		period_ = std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(static_cast<double>(den) / num));
	const char *core = ce_session_core_name(s);
	const char *system = ce_session_system_name(s);
	const std::string coreText = core != nullptr ? core : "", systemText = system != nullptr ? system : "";
	const char *sysId = ce_session_system_id(s);
	const std::string systemIdText = sysId != nullptr ? sysId : "";
	const double rate = num > 0 && den > 0 ? static_cast<double>(num) / den : 0;

	const int64_t frames = inputs_.size();
	/* another game, or the same started again: another average, as Chimera's */
	averageResetM_.store(true);
	later([this, names, players, headers, axes, mnemonics, keybinds, coreText, systemText, systemIdText, rate, label, inputsReplaced, frames, controller,
			   picture, gameCore, timer, saveData, movieImport, package, traces, registers, traceHead, surfaceText, withOpen] {
		surfaces.set(surfaceText);
		hasTrace.set(traces);
		hasRegisters.set(registers);
		traceHeader.set(traceHead);
		controllerName_ = controller;
		controllerPicture_ = picture;
		packagePath_ = package;
		isGameCore.set(gameCore);
		hasGameTimer.set(timer);
		hasSaveData.set(saveData);
		movieImportMenu.set(movieImport);
		gameTimeText.set("");
		averageFpsText.set("");
		buttonNames_ = names;
		buttonPlayers_ = players;
		axes_ = axes;
		mnemonics_ = mnemonics;
		buttonHeaders_ = headers;
		std::string why;
		const bool bound = keybinds.empty() || bindings_.parse(keybinds, buttonNames_, "AllTrollers", why, controller);
		defaultBindings_ = bindings_.texts();
		defaultBindings_.resize(buttonNames_.size());
		autofireBindings_ = Bindings();
		if (!keybinds.empty()) autofireBindings_.parse(keybinds, buttonNames_, "AllTrollersAutoFire", why, controller);
		defaultAutofire_ = autofireBindings_.texts();
		defaultAutofire_.resize(buttonNames_.size());
		{
			std::vector<std::string> axisNames;
			for (const Axis &a : axes_) axisNames.push_back(a.name);
			std::string analogWhy;
			analogBinds_ = keybinds.empty() ? std::vector<AnalogBind>(axes_.size()) : parseAnalogBinds(keybinds, axisNames, analogWhy, controller);
			defaultAnalog_ = analogBinds_;
		}
		buttonCount.set(static_cast<int64_t>(names.size()));
		coreName.set(coreText);
		systemName.set(systemText);
		systemId.set(systemIdText);
		frame.set(0);
		greenEnd.set(0);
		running.set(false);
		stoppedAt.set(-1);
		if (withOpen) withOpen();
		isOpen.set(true);
		/* a lag count a script set was the last machine's */
		lagOffset_ = 0;
		lagOffsetFrom_ = INT64_MAX;
		opens.set(opens.get() + 1);
		char hz[32];
		std::snprintf(hz, sizeof hz, "%.3f", rate);
		status.set(bound ? tr("session.opened", {label, hz, std::to_string(bindings_.boundKeys())})
						 : tr("session.opened_unbound", {label, hz, std::to_string(bindings_.boundKeys()), why}));
		buttonsChanged.emit();
		if (inputsReplaced) inputsReset.emit();
		(void)frames;
	});
}

int32_t SessionViewModel::playerOf(const std::string &name) const
{
	for (size_t i = 0; i < buttonNames_.size(); i++)
		if (buttonNames_[i] == name) return i < buttonPlayers_.size() ? buttonPlayers_[i] : 0;
	for (const Axis &a : axes_)
		if (a.name == name) return a.player;
	return 0;
}

void SessionViewModel::closeOnMachine()
{
	if (s_ == nullptr) return;
	freeSessionOnMachine();
	freeProjectOnMachine();
	runningM_ = false;
	later([this] {
		stoppedAt.set(-1);
		isOpen.set(false);
		running.set(false);
		fps.set(0.0);
		projectPath.set("");
		projectTitle.set("");
		projectId.set("");
		hasProject.set(false);
		projectSwitched();
		status.set(tr("session.closed"));
	});
}

int64_t SessionViewModel::frameVolume(bool soundOn, int64_t volume, bool fast, bool fastOn, int64_t fastVolume, bool advanced,
	bool muteAdvance)
{
	int64_t v = soundOn ? volume : 0;
	if (fast) v = fastOn ? v * fastVolume / 100 : 0;
	if (advanced && muteAdvance) v = 0;
	return v;
}

std::string SessionViewModel::compileDirFor(const std::string &root, bool compiles, const std::string &gameSha1)
{
	if (!compiles || root.empty() || gameSha1.empty()) return {};
	/* the hash, upper case, and nothing that could make a path of it */
	std::string name;
	for (char c : gameSha1) name += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : '_';
	return (std::filesystem::path(root) / name).string();
}

void SessionViewModel::compileCacheOnMachine(const std::string &package, const std::string &gamePath, const std::string &knownSha1)
{
	bool compiles = false;
	const char *perr = nullptr;
	if (ce_package *pk = ce_package_open(package.c_str(), &perr))
	{
		uint64_t len = 0;
		json::Json cfg;
		std::string err;
		if (const uint8_t *d = ce_package_entry_texts(pk, "waterbox.config", &len);
			d != nullptr && json::parse(std::string_view(reinterpret_cast<const char *>(d), static_cast<size_t>(len)), cfg, err))
			if (const json::Json *v = cfg.find("precompile")) compiles = v->kind == json::Json::Kind::Bool && v->boolean;
		ce_package_free(pk);
	}
	std::string sha = knownSha1;
	/* a game alone is hashed (the engine keeps the answer for the file) */
	if (compiles && sha.empty() && !gamePath.empty())
	{
		char hash[41] = {};
		uint64_t size = 0;
		if (ce_sha1_file(gamePath.c_str(), hash, &size) == 1) sha = hash;
	}
	const std::string dir = compileDirFor(compileRootM_, compiles, sha);
	/* the game's compiled code, if another build of the core compiled it,
	 * is removed before this one reads it (compiled_code.hpp) */
	if (const std::optional<compiled::Build> build = dir.empty() ? std::nullopt : compiled::buildOf(package);
		build && compiled::claim(dir, *build))
		later([this] { status.set(tr("session.compiled_code_removed")); });
	ce_cache_dir(dir.empty() ? nullptr : dir.c_str());
	later([this, dir] { compileCacheDir.set(dir); });
}

void SessionViewModel::setTracing(bool on, TraceSink sink)
{
	traceSink_ = std::move(sink);
	submit([this, on] {
		traceOnM_ = on;
		if (s_ == nullptr || ce_session_trace_available(s_) == 0) return;
		/* what was traced before is let go with the switch */
		if (!on) drainTraceOnMachine();
		ce_session_trace_enable(s_, on ? 1 : 0);
	});
}

void SessionViewModel::drainTraceOnMachine()
{
	if (s_ == nullptr || ce_session_trace_available(s_) == 0) return;
	uint64_t len = 0;
	int32_t count = 0, overflow = 0;
	const uint8_t *bytes = ce_session_trace_drain(s_, &len, &count, &overflow);
	if (!traceOnM_ || (count == 0 && overflow == 0)) return;
	std::vector<std::string> lines;
	lines.reserve(static_cast<size_t>(count) + 1);
	for (uint64_t at = 0; bytes != nullptr && at < len;)
	{
		const char *line = reinterpret_cast<const char *>(bytes + at);
		const size_t n = strnlen(line, static_cast<size_t>(len - at));
		lines.emplace_back(line, n);
		at += n + 1;
	}
	/* as Chimera's: a full buffer is said where the lines stop */
	if (overflow != 0)
	{
		const char *core = ce_session_core_name(s_);
		lines.push_back(tr("session.trace_buffer_full", {core != nullptr ? core : tr("session.the_core")}));
	}
	later([this, lines = std::move(lines)]() mutable {
		if (traceSink_) traceSink_(std::move(lines));
	});
}

std::vector<SessionViewModel::Register> SessionViewModel::readRegisters(int64_t &cycles, bool &hasCycles)
{
	std::vector<Register> out;
	cycles = 0;
	hasCycles = false;
	onMachineWait([&](ce_session *s) {
		if (s == nullptr) return;
		for (int32_t i = 0; i < ce_session_register_count(s); i++)
		{
			const char *name = ce_session_register_name(s, i);
			out.push_back({name != nullptr ? name : "", ce_session_register_value(s, i), ce_session_register_bits(s, i)});
		}
		hasCycles = ce_session_has_executed_cycles(s) != 0;
		if (hasCycles) cycles = ce_session_executed_cycles(s);
	});
	return out;
}

bool SessionViewModel::renderSurface(int32_t index, Picture &into)
{
	bool drawn = false;
	onMachineWait([&](ce_session *s) {
		if (s == nullptr || index < 0 || index >= ce_session_surface_count(s)) return;
		const int32_t w = ce_session_surface_width(s, index), h = ce_session_surface_height(s, index);
		const uint32_t *px = w > 0 && h > 0 ? ce_session_surface_render(s, index) : nullptr;
		if (px == nullptr) return;
		into.publish(px, w, h, w, h);
		drawn = true;
	});
	return drawn;
}

bool SessionViewModel::writeRegister(int32_t index, int64_t value)
{
	bool written = false;
	onMachineWait([&](ce_session *s) {
		if (s == nullptr || index < 0 || index >= ce_session_register_count(s)) return;
		written = ce_session_register_set(s, index, value) == 0;
	});
	return written;
}

void SessionViewModel::reopenSoundOnMachine()
{
	soundM_.reset();
	soundM_ = std::make_unique<Sound>(soundDeviceM_, static_cast<int32_t>(soundBufferMsM_));
	const std::string text = soundM_->available() ? tr("session.sound_on") : soundM_->problem();
	later([this, text] { sound.set(text); });
}

int32_t SessionViewModel::advanceOnMachine()
{
	/* the scripts' turn before the frame: what they hold now is this frame's.
	 * A script's call may close the machine; then there is no frame */
	ce_session *const was = s_;
	scriptTurnOnMachine(scriptsBeforeM_);
	if (s_ == nullptr || s_ != was) return -1;
	const int64_t f = ce_session_frame(s_);
	const int64_t size = ce_session_movie_length(s_);
	/* Display Nothing runs the frame without drawing, and Turbo and
	 * unthrottled draw one frame in fastDrawOneIn (the frame skip's one
	 * setting left: owner, 2026-10-24)
	 * it - unless it is being recorded */
	bool draw = !displayNothingM_.load();
	if (turboM_.load() || unthrottledM_.load())
	{
		draw = draw && ++fastUndrawn_ >= fastDrawM_.load();
		if (draw) fastUndrawn_ = 0;
	}
	else
		fastUndrawn_ = 0;
	const int32_t lag = stepOnMachine(!playedSeekOnMachine(), recordingM_.load() || draw);
	/* past the end the movie grew: the project has a change */
	if (f >= size && !unsavedM_.exchange(true)) later([this] { markChanged(); });
	/* TAStudio's "automatically pause at end of movie": the frame the movie
	 * runs out on pauses, once; played on from there, it records */
	if (pauseAtEndM_.load())
	{
		const bool atEnd = ce_session_frame(s_) == ce_session_movie_length(s_);
		if (doPauseM_ && atEnd)
		{
			runningM_ = false;
			later([this] { running.set(false); });
		}
		doPauseM_ = !atEnd;
	}
	/* advance_until: stopped where the condition is met */
	if (mcpMoveM_ && mcpMoveM_->kind == McpMove::Until && pauseOnFrameM_ >= 0 && ce_mcp_condition_met(s_) == 1)
	{
		pauseOnFrameM_ = -1;
		runningM_ = false;
		later([this] { running.set(false); seekingTo.set(-1); });
	}
	/* a seek that plays (Turbo seek off) stops on its frame */
	if (pauseOnFrameM_ >= 0 && ce_session_frame(s_) >= pauseOnFrameM_)
	{
		if (restoringOnMachine()) restoredOnMachine();
		pauseOnFrameM_ = -1;
		runningM_ = false;
		later([this] { running.set(false); seekingTo.set(-1); });
	}
	/* the step reports what it changed; here the frames added and the rate */
	double measured = -1;
	fpsFrames_++;
	const auto now = Clock::now();
	const double secs = std::chrono::duration<double>(now - fpsSince_).count();
	if (secs >= 0.5)
	{
		measured = fpsFrames_ / secs;
		lastFpsM_ = measured;
		fpsFrames_ = 0;
		fpsSince_ = now;
	}
	report(-1, -1, f >= size ? size : -1, f >= size ? f : -1, measured);
	/* and after it */
	if (lag >= 0) scriptTurnOnMachine(scriptsAfterM_);
	/* a script's show_future: frames ahead, their picture shown, the machine put back */
	if (lag >= 0 && s_ != nullptr && futureMaxM_.load() > 0 && futureTurnM_) showFutureOnMachine();
	return lag;
}

int32_t SessionViewModel::stepOnMachine(bool live, bool render, bool grow)
{
	movesM_++;
	const int64_t f = ce_session_frame(s_);
	/* held: all the person's input this frame; sticky: the part of it autohold, autofire and the
	 * Auto-hold columns give, which alone extends the movie when not recording */
	Buttons held, sticky;
	if (live)
	{
		Buttons padHeld;
		if (padsM_)
		{
			padsM_->poll(padHeld_, &padAxes_);
			{
				std::lock_guard<std::mutex> lock(padHeldCopyM_);
				padHeldCopy_ = padHeld_;
				padAxesCopy_ = padAxes_;
			}
			for (const std::string &name : padHeld_)
				for (int b : bindingsM_.buttonsOf(name))
					if (b >= 0) padHeld.set(static_cast<size_t>(b));
			const std::vector<std::string> open = padsM_->names();
			if (open.size() != padCount_)
			{
				padCount_ = open.size();
				std::string text = open.empty() ? tr("session.no_gamepads") : tr("session.gamepads", static_cast<int64_t>(open.size()));
				for (const std::string &n : open) text += " " + n;
				later([this, text] { gamepads.set(text); });
			}
		}
		/* a pad is not heard while the window is behind another, unless asked */
		if (!padsHeardM_.load()) padHeld.clear();
		/* a script's buttons hold for one frame */
		held = held_.get() | padHeld | scriptHeld_.exchange({});
		/* a pad's button pressed while autohold or autofire is armed changes over */
		const Buttons padPressed = padHeld.without(padWasM_);
		padWasM_ = padHeld;
		if (padPressed.any() && armed_ != 0)
		{
			toggleSticky(padPressed);
			later([this] { stickyChanged(); });
		}
		/* the autofire keys: a button pressed on its first frame and every period after, Chimera's
		 * AutofireController counting from the frame the key went down */
		{
			std::vector<std::string> keys;
			{
				std::lock_guard<std::mutex> lock(keysHeldM_);
				keys = keysHeld_;
			}
			if (padsHeardM_.load()) keys.insert(keys.end(), padHeld_.begin(), padHeld_.end());
			Buttons firingKeys;
			for (const std::string &k : keys)
				for (int b : autofireBindingsM_.buttonsOf(k))
					if (b >= 0) firingKeys.set(static_cast<size_t>(b));
			firingKeys.without(autofireWasM_).each([&](size_t b) {
				if (autofireStartM_.size() <= b) autofireStartM_.resize(b + 1, 0);
				autofireStartM_[b] = f;
			});
			autofireWasM_ = firingKeys;
			const int64_t keyOn = fireOnM_.load(), keyPeriod = keyOn + fireOffM_.load();
			firingKeys.each([&](size_t b) {
				if (keyPeriod > 0 && (f - autofireStartM_[b]) % keyPeriod < keyOn) held.set(b);
			});
			keysThisFrameM_ = std::move(keys);
		}
		/* what autohold holds, and what autofire presses this frame */
		sticky |= autoholdMask_.get();
		const int64_t on = fireOnM_.load(), period = on + fireOffM_.load();
		autofireMask_.get().each([&](size_t b) {
			if (fireAtM_.size() <= b) fireAtM_.resize(b + 1, 0);
			if (fireAtM_[b] % period < on) sticky.set(b);
		});
		/* the piano roll's Auto-hold columns: a button's pattern says whether it is pressed */
		for (size_t i = 0; i < columnPatternsM_.size(); i++)
		{
			const ColumnPattern &cp = columnPatternsM_[i];
			if (!cp.isAxis && !cp.values.empty() && cp.index >= 0 && cp.values[columnAtM_[i] % cp.values.size()] != 0)
				sticky.set(static_cast<size_t>(cp.index));
		}
		held |= sticky;
	}
	/* a script's false lets a button go for the frame, held by a key, a pad or
	 * autohold or not, as Chimera's override does (joypad.set) */
	held = held.without(scriptReleased_.exchange({}));
	/* What is held goes into the movie before the frame plays it. Recording, the frame becomes what is held, an
	 * analog control left alone at rest, and a column hidden while hidden
	 * columns are not edited keeps what it had. Not recording, a frame the
	 * movie has is played as it is, and past the end the movie grows by the
	 * sticky part alone: autohold, autofire and the Auto-hold columns. A
	 * frame changed is an edit (the engine drops the states after it). The
	 * engine plays the movie; the window never hands the machine input of
	 * its own. */
	const bool recording = live && projectM_ != nullptr && recordingModeM_.load();
	bool changed = false;
	const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s_));
	axesBuf_.resize(axisCount);
	statesBuf_.resize(static_cast<size_t>(ce_session_button_count(s_)));
	const int32_t had = ce_session_movie_get(s_, f, statesBuf_.data(), axisCount ? axesBuf_.data() : nullptr);
	const Buttons logged = Buttons::fromStates(statesBuf_.data(), statesBuf_.size());
	if (had == 1 && !grow) return 0;
	loggedAxes_ = axesBuf_;
	if (live && !recording) held = sticky;
	auto axisShown = [this](size_t i) { return recordAxesM_.empty() || (i < recordAxesM_.size() && recordAxesM_[i]); };
	if (recording)
		for (size_t i = 0; i < axisCount && i < axesM_.size(); i++)
			if (axisShown(i)) axesBuf_[i] = axesM_[i].neutral;
	/* how far the pointer moved since the last frame played with the person's input: read once
	 * such a frame, as Chimera's are; a frame replayed leaves it */
	const int movedX = live ? movedXM_.exchange(0) : 0, movedY = live ? movedYM_.exchange(0) : 0;
	/* as an axis, as Chimera's: scaled by the sensitivity, 120 pixels all the
	 * way and no further, the host's -10000..10000 */
	auto relative = [this](int moved) {
		const double v = std::clamp(moved * (mouseSensitivityM_.load() / 100.0), -120.0, 120.0) / 120.0;
		return static_cast<int>(v * 10000);
	};
	/* the analog controls' bindings, when recording: a stick off its rest, or a key pushing one */
	if (recording)
		for (size_t i = 0; i < axisCount && i < analogBindsM_.size() && i < axesM_.size(); i++)
		{
			const AnalogBind &bind = analogBindsM_[i];
			if (bind.empty() || !axisShown(i)) continue;
			int host = 0;
			if (bind.value == "WMouse X") host = pointerXM_.load();
			else if (bind.value == "WMouse Y") host = pointerYM_.load();
			else if (bind.value == "RMouse X") host = relative(movedX);
			else if (bind.value == "RMouse Y") host = relative(movedY);
			else if (padsHeardM_.load())
				for (const auto &[name, v] : padAxes_)
					if (name == bind.value) host = v;
			auto heldNow = [this](const std::string &key) {
				return !key.empty() && std::find(keysThisFrameM_.begin(), keysThisFrameM_.end(), key) != keysThisFrameM_.end();
			};
			bool active = false;
			const int32_t v = analogValue(bind, host, heldNow(bind.positive), heldNow(bind.negative), axesM_[i].min, axesM_[i].max,
				axesM_[i].neutral, active);
			if (active) axesBuf_[i] = v;
		}
	/* an Auto-hold analog column: its value goes in as a held button does, recording or past the end */
	if (recording || (live && had == 1))
		for (size_t i = 0; i < columnPatternsM_.size(); i++)
		{
			const ColumnPattern &cp = columnPatternsM_[i];
			if (!cp.isAxis || cp.values.empty() || cp.index < 0 || static_cast<size_t>(cp.index) >= axisCount) continue;
			if (axisShown(static_cast<size_t>(cp.index))) axesBuf_[static_cast<size_t>(cp.index)] = cp.values[columnAtM_[i] % cp.values.size()];
		}
	/* a script's analog values go in as its buttons do, when recording: held, then this frame's own */
	if (live)
	{
		std::lock_guard<std::mutex> lock(scriptAxesM_);
		if (recording)
			for (const std::map<size_t, int32_t> *values : {&scriptAxesHeld_, &scriptAxesOnce_})
				for (const auto &[axis, v] : *values)
					if (axis < axisCount && axisShown(axis)) axesBuf_[axis] = v;
		scriptAxesOnce_.clear();
	}
	/* recording changes only the buttons recording may change (the columns shown) */
	Buttons buttons = logged;
	if (recording || had == 1)
		buttons = recordAllM_.load() ? held : logged.without(recordShownM_.get()) | (held & recordShownM_.get());
	const bool axesChanged = axesBuf_ != loggedAxes_;
	if (had == 1 || buttons != logged || axesChanged)
		statesBuf_ = buttons.states(statesBuf_.size());
	if (had == 1)
		ce_session_movie_set(s_, f, statesBuf_.data(), axesChanged ? axesBuf_.data() : nullptr);
	else if (buttons != logged || axesChanged)
	{
		ce_session_movie_set(s_, f, statesBuf_.data(), axisCount ? axesBuf_.data() : nullptr);
		greenEndM_ = std::min(greenEndM_, f);
		forgetGameTimeAfterOnMachine(f);
		changed = true;
	}
	/* a frame the person's input wrote: a step of the undo history, as Chimera's Record Frame */
	if (live && projectM_ != nullptr && (had == 1 || changed))
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		recordedM_.push_back({f, ce_session_movie_length(s_), had == 1, runningM_, logged, buttons, loggedAxes_, axesBuf_});
	}

	/* what the core is handed this frame: the movie's frame, which the engine plays (joypad.getwithmovie) */
	playedM_.set(buttons);
	const int32_t lag = ce_session_movie_advance(s_, 0, nullptr, render ? 1 : 0);
	/* autofire's patterns move on a frame, a lag frame not counted unless asked */
	if (live && lag >= 0 && (lag == 0 || !fireSkipsLagM_.load()))
		autofireMask_.get().each([this](size_t b) {
			if (fireAtM_.size() <= b) fireAtM_.resize(b + 1, 0);
			fireAtM_[b]++;
		});
	/* and the Auto-hold columns' patterns, each by its own rule for lag frames */
	if (live && lag >= 0)
		for (size_t i = 0; i < columnPatternsM_.size(); i++)
		{
			const ColumnPattern &cp = columnPatternsM_[i];
			if (cp.values.empty() || (lag != 0 && cp.skipLag)) continue;
			if (++columnAtM_[i] >= cp.values.size()) columnAtM_[i] = static_cast<size_t>(std::max<int64_t>(0, cp.loop));
		}
	if (lag < 0)
	{
		const char *death = ce_session_guest_death(s_);
		/* the window took the frame out just as it was to be played: the
		 * machine is well, and the edit's own turn puts it right */
		if ((death == nullptr || *death == '\0') && ce_session_movie_length(s_) <= f)
		{
			frameM_ = ce_session_frame(s_);
			return 0;
		}
		runningM_ = false;
		diedM_ = true;
		/* as Chimera's: a frozen address may be what stopped the core, and it would be written again on
		 * the first frame after going back; the freezes are switched off, not removed */
		const int64_t freezesOff = std::count_if(freezesM_.begin(), freezesM_.end(), [](const Freeze &z) { return z.enabled; });
		if (freezesOff > 0)
		{
			for (Freeze &z : freezesM_) z.enabled = false;
			for (int32_t i = 0; i < ce_session_freeze_count(s_); i++) ce_session_freeze_enable(s_, i, 0);
			later([this, list = freezesM_] {
				freezes_ = list;
				freezesUpdated();
			});
		}
		const std::string why = death != nullptr && *death != '\0' ? tr("session.machine_stopped_why", {death})
																	 : tr("session.movie_not_played", {ce_session_last_error(s_)});
		const bool unplayable = death == nullptr || *death == '\0';
		later([this, why, unplayable] {
			running.set(false);
			status.set(why);
			if (unplayable) unplayableMovies.set(unplayableMovies.get() + 1);
		});
		/* asked what now, as Chimera's: the frame, the latest safe point, frame 0
		 * (the same death reported again while it is asked asks nothing more) */
		if (death != nullptr && *death != '\0')
		{
			const int64_t at = frameM_, safe = ce_session_greenzone_nearest(s_, at), restart = ce_session_greenzone_nearest(s_, 0) == 0;
			later([this, at, safe, restart, freezesOff, reason = std::string(death)] {
				if (stoppedAt.get() >= 0) return;
				stoppedReason.set(reason);
				stoppedSafePoint.set(safe <= at ? safe : -1);
				stoppedCanRestart.set(restart);
				stoppedFreezesOff.set(freezesOff);
				stoppedAt.set(at);
			});
		}
		return lag;
	}
	frameM_ = ce_session_frame(s_);
	greenEndM_ = std::max(greenEndM_, frameM_);
	lastLagM_ = lag > 0;
	crash::noteFrame(frameM_);
	if (gameTimerM_ && frameM_ >= 0)
	{
		int64_t gameMs = 0;
		if (ce_session_game_time_ms(s_, &gameMs) == 1)
		{
			if (gameTimeLogM_.size() <= static_cast<size_t>(frameM_)) gameTimeLogM_.resize(static_cast<size_t>(frameM_) + 1, kNoGameTime);
			gameTimeLogM_[static_cast<size_t>(frameM_)] = gameMs;
		}
	}
	for (const Hooks &h : hooksM_)
		if (h.frame) h.frame(s_);
	if (inputs_.setLag(f, lag > 0)) changed = true;
	if (traceOnM_) drainTraceOnMachine();

	if (live && soundM_ && !turboM_.load())
	{
		int32_t pairs = 0;
		/* at 44100 Hz, the rate the sound output plays at, whatever the core mixes at */
		const int16_t *samples = ce_session_sound(s_, &pairs);
		const int64_t v = frameVolume(soundOnM_ && soundNormalOnM_, volumeM_.load(), fastM_, soundFastOnM_, fastVolumeM_.load(),
			steppingM_ || holdRunning_, muteAdvanceM_);
		soundM_->play(samples, pairs, static_cast<int32_t>(v));
	}

	if (render) publishPictureOnMachine();
	if (changed)
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		changedFirst_ = changedFirst_ < 0 ? f : std::min(changedFirst_, f);
		changedLast_ = std::max(changedLast_, f);
	}
	return lag;
}

void SessionViewModel::seekOnMachine(int64_t target, bool grow)
{
	if (s_ == nullptr || target < 0) return;
	movesM_++;
	const auto start = Clock::now();
	const int64_t from = ce_session_frame(s_);
	/* a frame past the movie's end: the movie grows to it, with empty frames,
	 * or the machine stops at the end. The length is read here, at the last
	 * moment: the window edits the movie while this thread runs. */
	const int64_t length = ce_session_movie_length(s_);
	if (target > length)
	{
		if (grow) ce_session_movie_resize(s_, target);
		else target = length;
	}
	/* The engine seeks: back to its nearest stored state and forward by the
	 * movie. It draws nothing on the way, so the seek stops a frame short
	 * and the last frame is run with its picture. What it ran, roughly: from
	 * the stored state it went back to, or from where it was. */
	const int64_t want = target > 0 ? target - 1 : 0;
	/* an edit the engine has not honoured yet (greenEnd fell to it) sends
	 * the machine back before it */
	const int64_t stored = ce_session_greenzone_nearest(s_, greenEndM_ < from ? std::min(want, greenEndM_) : want);
	const int64_t ran = (stored >= 0 && (stored > from || from > want) ? want - stored : want - from) + (target > 0 ? 1 : 0);
	if (ce_session_seek(s_, want) != 0)
	{
		/* the window edited the movie while the machine went through it
		 * (frames taken out, and maybe put back by an undo since): that
		 * edit's own turn comes next and puts the machine right */
		if (!grow && (ce_session_movie_length(s_) < length || editsSent_.load() != editRunningM_))
		{
			frameM_ = ce_session_frame(s_);
			report(-1, -1, -1, -1, -1);
			return;
		}
		const std::string why = tr("session.could_not_seek", {std::to_string(target), ce_session_last_error(s_)});
		later([this, why] { status.set(why); });
		frameM_ = ce_session_frame(s_);
		report(-1, -1, -1, -1, -1);
		return;
	}
	frameM_ = ce_session_frame(s_);
	if (target > 0) stepOnMachine(false, true, grow);
	const double ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
	later([this, ms, ran] {
		lastSeekMs.set(ms);
		lastSeekFrames.set(ran);
	});
	report(-1, -1, -1, -1, -1);
	/* the scripts' turn on the frame the machine landed on */
	scriptTurnOnMachine(scriptsAfterM_);
}

void SessionViewModel::dropStatesAfterOnMachine(int64_t frame)
{
	if (s_ == nullptr) return;
	ce_session_greenzone_invalidate(s_, frame);
	greenEndM_ = std::min(greenEndM_, frame);
	forgetGameTimeAfterOnMachine(frame);
	report(-1, -1, -1, -1, -1);
}

void SessionViewModel::forgetGameTimeAfterOnMachine(int64_t frame)
{
	if (frame < 0) frame = 0;
	if (gameTimeLogM_.size() > static_cast<size_t>(frame) + 1) gameTimeLogM_.resize(static_cast<size_t>(frame) + 1);
}

void SessionViewModel::gameTimeHeadersOnMachine(ce_project *p, int64_t length)
{
	/* the game's timer at the end of the movie, for whoever reads the project
	 * later: only when the machine has run to the end since the last edit
	 * before it, or the project said so when it was opened and nothing before
	 * that has changed; otherwise none, never a stale one */
	const bool known = length >= 0 && static_cast<size_t>(length) < gameTimeLogM_.size() &&
		gameTimeLogM_[static_cast<size_t>(length)] != kNoGameTime;
	if (!known)
	{
		ce_project_header_set(p, "GameTimeMs", nullptr);
		ce_project_header_set(p, "GameTime", nullptr);
		ce_project_header_set(p, "GameTimeFrame", nullptr);
		return;
	}
	const int64_t gameMs = gameTimeLogM_[static_cast<size_t>(length)];
	char text[48];
	ce_game_time_text(gameMs, text, sizeof text);
	ce_project_header_set(p, "GameTimeMs", std::to_string(gameMs).c_str());
	ce_project_header_set(p, "GameTime", text);
	ce_project_header_set(p, "GameTimeFrame", std::to_string(length).c_str());
}

void SessionViewModel::edited(int64_t first)
{
	greenzoneInvalidated.emit(first);
	const uint64_t sent = ++editsSent_;
	/* the setting in force when the edit was made: one changed while the
	 * machine still has edits to get to does not change what they do */
	const bool restore = autoRestore.get();
	submit([this, first, sent, restore] {
		if (s_ == nullptr) return;
		editRunningM_ = sent;
		greenEndM_ = std::min(greenEndM_, first);
		forgetGameTimeAfterOnMachine(first);
		/* a way back under way is this run of edits' own: its frame is kept */
		const bool restoring = restoringOnMachine();
		/* moved since the last edit moved it: that edit's frame is forgotten */
		if (!restoring && (movesM_ != restoredMovesM_ || frameM_ != restoredAtM_)) restoreFrameM_ = -1;
		const int64_t length = ce_session_movie_length(s_);
		/* an edit that gave a movie made shorter its frames again (an undo)
		 * lets the machine go on to where the first edit found it */
		const bool shortOfIt = restore && !restoring && restoreFrameM_ > frameM_ && length > frameM_;
		if (frameM_ > first || shortOfIt)
		{
			/* The machine is past the edit. The engine honours it before the
			 * machine next moves; here the window says where: with autoRestore
			 * back on the frame it was on at the first edit, with the new
			 * inputs, else at the edit. Going back adds no frames to the
			 * movie: one made shorter is followed to its end. It goes as a
			 * seek asked for goes, never in one long run that keeps the
			 * window waiting (issue 406) */
			if (restoreFrameM_ < 0) restoreFrameM_ = frameM_;
			restoreOnMachine(restore ? restoreFrameM_ : first);
		}
		else if (restoring && restoreToM_ > length)
		{
			/* the way back under way went past a movie made shorter ahead of
			 * the machine: it stops at the end, or now when it is there */
			if (pauseOnFrameM_ == restoreToM_) pauseOnFrameM_ = playedSeekToM_ = length;
			if (turboSeekToM_ == restoreToM_) turboSeekToM_ = length;
			restoreToM_ = length;
			if (frameM_ >= length)
			{
				turboSeekToM_ = pauseOnFrameM_ = -1;
				runningM_ = false;
				later([this] { running.set(false); seekingTo.set(-1); });
				restoredOnMachine();
			}
		}
		report(-1, -1, -1, -1, -1);
	});
}

/* a drive's media text: an image's name cut in the middle, keeping its end
 * (the disks of one game differ at the end), the image in a drive and the one
 * its selector is on said once when they are the same */
static std::string driveFit(std::string name, size_t room)
{
	const size_t slash = name.find_last_of("/\\");
	if (slash != std::string::npos && slash + 1 < name.size()) name = name.substr(slash + 1);
	if (name.size() <= room || room < 8) return name;
	const size_t tail = room * 2 / 3;
	return name.substr(0, room - tail - 2) + ".." + name.substr(name.size() - tail);
}

static std::string driveEntry(const std::vector<std::string> &names, int32_t index, size_t room)
{
	if (index < 0 || index >= static_cast<int32_t>(names.size())) return "?";
	return std::to_string(index + 1) + "/" + std::to_string(names.size()) + " " + driveFit(names[static_cast<size_t>(index)], room);
}

std::string SessionViewModel::driveMediaShort(const std::vector<std::string> &names, int32_t selected, int32_t inserted)
{
	const std::string in = inserted >= 0 ? driveEntry(names, inserted, 28) : tr("session.drive_empty_short");
	return inserted == selected ? in : tr("session.drive_selected_short", {in, driveEntry(names, selected, 28)});
}

std::string SessionViewModel::driveMediaLong(const std::string &drive, const std::vector<std::string> &names, int32_t selected, int32_t inserted)
{
	const std::string in = inserted >= 0 ? tr("session.drive_holds", {driveEntry(names, inserted, std::string::npos)}) : tr("session.drive_empty");
	return inserted == selected ? tr("session.drive_line", {drive, in})
								: tr("session.drive_line_selected", {drive, in, driveEntry(names, selected, std::string::npos)});
}

void SessionViewModel::drivesOnMachine()
{
	json::Json out;
	out.kind = json::Json::Kind::Array;
	const int32_t count = s_ != nullptr ? ce_session_drive_count(s_) : 0;
	for (int32_t i = 0; i < count; i++)
	{
		const char *n = ce_session_drive_name(s_, i);
		const std::string name = n != nullptr ? n : "";
		std::string shortText, longText = name;
		if (const int32_t media = ce_session_drive_media_count(s_, i); media > 0)
		{
			std::vector<std::string> names;
			for (int32_t m = 0; m < media; m++)
			{
				const char *mn = ce_session_drive_media_name(s_, i, m);
				names.emplace_back(mn != nullptr ? mn : "");
			}
			const int32_t selected = ce_session_drive_media_selected(s_, i), inserted = ce_session_drive_media_inserted(s_, i);
			shortText = driveMediaShort(names, selected, inserted);
			longText = driveMediaLong(name, names, selected, inserted);
		}
		json::Json d;
		d.kind = json::Json::Kind::Object;
		d.members.emplace_back("name", json::Json::string(name));
		d.members.emplace_back("on", json::Json::boolean_(ce_session_drive_light(s_, i) != 0));
		d.members.emplace_back("short", json::Json::string(shortText));
		d.members.emplace_back("long", json::Json::string(longText));
		out.items.push_back(std::move(d));
	}
	std::string text = json::write(out);
	if (text == drivesSaidM_) return;
	drivesSaidM_ = text;
	later([this, text] { drives.set(text); });
}

void SessionViewModel::report(int64_t cf, int64_t cl, int64_t af, int64_t al, double fpsNow)
{
	bool post = false;
	drivesOnMachine();
	if (restoreFrameM_ != restoreSaidM_)
	{
		restoreSaidM_ = restoreFrameM_;
		later([this, at = restoreFrameM_] { restoreFrame.set(at); });
	}
	/* the game's own timer, when shown */
	int64_t gameMs = -1;
	if (s_ != nullptr && gameTimerM_ && showGameTimeM_.load())
	{
		int64_t ms = 0;
		if (ce_session_game_time_ms(s_, &ms) == 1) gameMs = ms;
	}
	/* the states among the frames shown: a few dozen lookups a report */
	const int64_t first = watchFirstM_.load(), count = std::min<int64_t>(watchCountM_.load(), 512);
	std::vector<uint8_t> stated(static_cast<size_t>(std::max<int64_t>(count, 0)), 0);
	if (s_ != nullptr)
		for (int64_t i = 0; i < count; i++) stated[static_cast<size_t>(i)] = ce_session_greenzone_nearest(s_, first + i) == first + i ? 1 : 0;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		pendingGameMs_ = gameMs;
		if (pendingStatedFrom_ != first || pendingStated_ != stated)
		{
			pendingStatedFrom_ = first;
			pendingStated_ = std::move(stated);
			statedFresh_ = true;
		}
		pendingFrame_ = frameM_;
		pendingGreenEnd_ = greenEndM_;
		pendingStates_ = s_ != nullptr ? ce_session_greenzone_count(s_) : 0;
		pendingRerecords_ = s_ != nullptr ? static_cast<int64_t>(ce_session_rerecords(s_)) : 0;
		if (fpsNow >= 0) pendingFps_ = fpsNow;
		if (soundM_)
		{
			pendingQueued_ = soundM_->queued();
			pendingDropped_ = soundM_->dropped();
		}
		if (cf >= 0)
		{
			changedFirst_ = changedFirst_ < 0 ? cf : std::min(changedFirst_, cf);
			changedLast_ = std::max(changedLast_, cl);
		}
		if (af >= 0)
		{
			appendedFirst_ = appendedFirst_ < 0 ? af : std::min(appendedFirst_, af);
			appendedLast_ = std::max(appendedLast_, al);
		}
		if (!tickPosted_) post = tickPosted_ = true;
	}
	if (post) later([this] { tick(); });
}

void SessionViewModel::tick()
{
	int64_t f, cf, cl, af, al, green, states, rr;
	double measured, average;
	int64_t gameMs;
	int64_t queued, dropped;
	bool statedNow = false;
	std::vector<RecordedFrame> recorded;
	{
		std::lock_guard<std::mutex> lock(pendingM_);
		tickPosted_ = false;
		if (statedFresh_)
		{
			statedFrom_ = pendingStatedFrom_;
			stated_ = pendingStated_;
			statedFresh_ = false;
			statedNow = true;
		}
		f = pendingFrame_;
		measured = pendingFps_;
		cf = changedFirst_, cl = changedLast_, af = appendedFirst_, al = appendedLast_;
		queued = pendingQueued_, dropped = pendingDropped_;
		green = pendingGreenEnd_, states = pendingStates_;
		rr = pendingRerecords_;
		average = pendingAverage_;
		gameMs = pendingGameMs_;
		pendingFps_ = -1;
		changedFirst_ = changedLast_ = appendedFirst_ = appendedLast_ = -1;
		recorded.swap(recordedM_);
	}
	frame.set(f);
	if (measured >= 0) fps.set(measured);
	if (gameMs >= 0)
	{
		char text[48];
		ce_game_time_text(gameMs, text, sizeof text);
		gameTimeText.set(tr("session.game_time", {text}));
	}
	if (showAverageFps.get() && average > 0)
	{
		char text[48];
		std::snprintf(text, sizeof text, "%.1f", average);
		averageFpsText.set(tr("session.average_fps", {text}));
	}
	soundQueued.set(queued);
	soundDropped.set(dropped);
	greenEnd.set(green);
	greenzoneStates.set(states);
	rerecords.set(rr);
	updateOverlay(f, cf >= 0 ? cf : af);
	if (!recorded.empty()) framesRecorded.emit(recorded);
	if (cf >= 0) inputsChanged.emit(cf, cl);
	if (af >= 0) inputsAppended.emit(af, al);
	if (cf >= 0 || af >= 0) greenzoneInvalidated.emit(cf >= 0 ? cf : af);
	if (statedNow) statesShown.emit();
	screenChanged.emit();
}

void SessionViewModel::watchStates(int64_t first, int64_t count)
{
	if (first == watchFirstM_.load() && count == watchCountM_.load()) return;
	watchFirstM_.store(std::max<int64_t>(first, 0));
	watchCountM_.store(std::max<int64_t>(count, 0));
	/* answered now, not at the next frame run */
	submit([this] { report(-1, -1, -1, -1, -1); });
}

bool SessionViewModel::hasState(int64_t frame) const
{
	const int64_t i = frame - statedFrom_;
	return i >= 0 && i < static_cast<int64_t>(stated_.size()) && stated_[static_cast<size_t>(i)] != 0;
}

void SessionViewModel::updateOverlay(int64_t f, int64_t changedFrom)
{
	if (showFps.get())
		fpsText.set(tr("session.fps", {std::to_string(static_cast<int64_t>(std::llround(fps.get())))}) +
			(rewindHeld_ ? (fastHeld_ || turboHeld_ ? " <<<<" : " <<") : turboHeld_ ? " >>>>" : fastHeld_ ? " >>" : ""));
	lagFrame.set(f > 0 && inputs_.lagAt(f - 1));
	const int64_t size = inputs_.size();
	if (showFrameCounter.get())
	{
		/* the last frame with input: zero for a movie with none */
		if (changedFrom >= 0 || lastInputFor_ != size)
		{
			lastInput_ = 0;
			std::vector<int32_t> axes(inputs_.axisCount()), neutral(inputs_.axisCount());
			if (!neutral.empty()) inputs_.neutralAxes(neutral.data());
			for (int64_t at = size - 1; at >= 0; at--)
			{
				bool pressed = inputs_.at(at).any();
				if (!pressed && !axes.empty())
				{
					inputs_.axesAt(at, axes.data());
					pressed = axes != neutral;
				}
				if (pressed)
				{
					lastInput_ = at;
					break;
				}
			}
			lastInputFor_ = size;
		}
		frameCounterText.set(std::to_string(f) + "/" + std::to_string(lastInput_) + (size > 0 && f > lastInput_ ? " (Finished)" : ""));
	}
	/* counted on from where it was, unless something before changed; counted
	 * whether it is shown or not, as scripts read it (emu.lagcount) */
	if (f < lagsTo_ || (changedFrom >= 0 && changedFrom < lagsTo_))
	{
		lagsTo_ = 0;
		lags_ = 0;
	}
	for (; lagsTo_ < f; lagsTo_++) lags_ += inputs_.lagAt(lagsTo_) ? 1 : 0;
	/* a count a script set holds from its frame on */
	if (f < lagOffsetFrom_) lagOffset_ = 0, lagOffsetFrom_ = INT64_MAX;
	lagCount.set(lags_ + lagOffset_);
	if (showInput.get()) inputText.set(inputDisplay(f - 1));
}

std::string SessionViewModel::inputDisplay(int64_t frame) const
{
	if (frame < 0 || frame >= inputs_.size()) return {};
	std::vector<int32_t> values(inputs_.axisCount());
	if (!values.empty()) inputs_.axesAt(frame, values.data());
	return inputDisplay(buttonNames_, mnemonics_, axes_, inputs_.at(frame), values);
}

std::string SessionViewModel::inputDisplay(const std::vector<std::string> &buttons, const std::string &mnemonics,
	const std::vector<Axis> &axes, const Buttons &pressed, const std::vector<int32_t> &values)
{
	/* a control's player: the number after "P" in its name, 0 for the console */
	auto player = [](const std::string &name) {
		if (name.size() < 3 || name[0] != 'P' || !std::isdigit(static_cast<unsigned char>(name[1]))) return 0;
		size_t at = 1;
		int n = 0;
		while (at < name.size() && std::isdigit(static_cast<unsigned char>(name[at]))) n = n * 10 + (name[at++] - '0');
		return at < name.size() && name[at] == ' ' ? n : 0;
	};
	int most = 0;
	for (const std::string &n : buttons) most = std::max(most, player(n));
	for (const Axis &a : axes) most = std::max(most, player(a.name));
	std::string out;
	for (int i = 1; i <= most + 1; i++)
	{
		const int p = i <= most ? i : 0;
		for (size_t b = 0; b < buttons.size(); b++)
			if (player(buttons[b]) == p)
			{
				const char mn = b < mnemonics.size() ? mnemonics[b] : (buttons[b].empty() ? '?' : buttons[b].back());
				out += pressed.test(b) ? mn : ' ';
			}
		for (size_t x = 0; x < axes.size() && x < values.size(); x++)
			if (player(axes[x].name) == p)
			{
				if (values[x] == axes[x].neutral) out += "      ";
				else
				{
					std::string v = std::to_string(values[x]);
					if (v.size() < 5) v.insert(0, 5 - v.size(), ' ');
					out += v + ",";
				}
			}
	}
	return out;
}

void SessionViewModel::setRecordMask(std::optional<Buttons> shown, std::vector<bool> axes)
{
	if (shown) recordShownM_.set(std::move(*shown));
	recordAllM_.store(!shown);
	submit([this, axes = std::move(axes)] { recordAxesM_ = axes; });
}

bool SessionViewModel::frozen(const std::string &domain, int64_t address) const
{
	for (const Freeze &f : freezes_)
		if (f.enabled && f.property.empty() && f.domain == domain && address >= f.address && address < f.address + f.size) return true;
	return false;
}

bool SessionViewModel::propertyFrozen(const std::string &name) const
{
	for (const Freeze &f : freezes_)
		if (f.enabled && f.property == name) return true;
	return false;
}

int32_t SessionViewModel::addFreezeOnMachine(const Freeze &f)
{
	if (s_ == nullptr) return -1;
	int32_t i = -1;
	if (!f.property.empty())
	{
		uint32_t element = 0;
		const int32_t index = ce_session_property_find(s_, f.property.c_str(), &element);
		if (index < 0) return -1;
		i = ce_session_freeze_property(s_, index, element, f.text.c_str());
	}
	else
		for (int32_t d = 0; d < ce_session_domain_count(s_); d++)
		{
			const char *n = ce_session_domain_name(s_, d);
			if (n != nullptr && f.domain == n)
			{
				i = ce_session_freeze(s_, d, f.address, f.size, f.bigEndian ? 1 : 0, f.value, 0, 0);
				break;
			}
		}
	if (i >= 0) ce_session_freeze_enable(s_, i, f.enabled ? 1 : 0);
	return i;
}

std::vector<SessionViewModel::Freeze> SessionViewModel::syncFreezesOnMachine(const std::vector<Freeze> &list)
{
	std::vector<Freeze> kept;
	if (s_ == nullptr) return kept;
	ce_session_unfreeze_all(s_);
	for (const Freeze &f : list)
		if (addFreezeOnMachine(f) >= 0) kept.push_back(f);
	return kept;
}

void SessionViewModel::carryFreezesOnMachine(const std::string &game)
{
	/* counted before another game's are let go: the window is told they went */
	const size_t had = freezesM_.size();
	if (game != freezeGameM_) freezesM_.clear();
	freezeGameM_ = game;
	freezesM_ = syncFreezesOnMachine(freezesM_);
	if (had == 0 && freezesM_.empty()) return;
	later([this, list = freezesM_] {
		freezes_ = list;
		freezesUpdated();
	});
}

void SessionViewModel::freezesUpdated()
{
	frozenCount.set(std::count_if(freezes_.begin(), freezes_.end(), [](const Freeze &f) { return f.enabled; }));
	freezesChanged.emit();
}

void SessionViewModel::syncFreezes()
{
	freezesUpdated();
	submit([this, list = freezes_] {
		freezesM_ = list;
		if (s_ == nullptr) return;
		std::vector<Freeze> kept = syncFreezesOnMachine(list);
		if (kept.size() == list.size()) return;
		freezesM_ = kept;
		later([this, kept] {
			freezes_ = kept;
			freezesUpdated();
		});
	});
}

bool SessionViewModel::freeze(const Freeze &f, std::string &error)
{
	std::vector<Freeze> list = freezes_;
	const auto same = [&f](const Freeze &g) {
		return f.property.empty() ? g.property.empty() && g.domain == f.domain && g.address == f.address : g.property == f.property;
	};
	if (auto it = std::find_if(list.begin(), list.end(), same); it != list.end()) *it = f;
	else list.push_back(f);
	std::string why;
	onMachineWait([&](ce_session *s) {
		if (s == nullptr)
		{
			why = tr("session.no_machine_running");
			return;
		}
		if (addFreezeOnMachine(f) < 0)
		{
			const char *e = ce_session_last_error(s);
			why = e != nullptr && *e != '\0' ? e : f.property.empty() ? tr("session.no_domain", {f.domain}) : tr("session.no_property", {f.property});
			return;
		}
		freezesM_ = list;
	});
	if (!why.empty())
	{
		error = why;
		return false;
	}
	freezes_ = std::move(list);
	freezesUpdated();
	return true;
}

void SessionViewModel::unfreeze(const std::string &domain, int64_t address)
{
	const size_t had = freezes_.size();
	std::erase_if(freezes_, [&](const Freeze &f) {
		return f.property.empty() && f.domain == domain && address >= f.address && address < f.address + f.size;
	});
	if (freezes_.size() != had) syncFreezes();
}

void SessionViewModel::unfreezeProperty(const std::string &name)
{
	const size_t had = freezes_.size();
	std::erase_if(freezes_, [&](const Freeze &f) { return f.property == name; });
	if (freezes_.size() != had) syncFreezes();
}

void SessionViewModel::unfreezeAll()
{
	if (freezes_.empty()) return;
	freezes_.clear();
	syncFreezes();
}

} // namespace chimera::vm
