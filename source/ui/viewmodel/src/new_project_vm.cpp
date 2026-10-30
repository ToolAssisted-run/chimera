#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/vm/compiled_code.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <thread>
#include <utility>
#include <atomic>
#include <ctime>
#include <mutex>

#include "chimera/engine.h"
#include "chimera/vm/subprocess.hpp"
#include "chimera/vm/suggestion.hpp"

namespace chimera::vm {

namespace {
std::string upper(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return s;
}
} // namespace

using json::Json;
namespace fs = std::filesystem;

namespace {
/* a slot's condition that reads only settings (no slot's files) */
bool readsOnlySettings(const Json &c)
{
	if (c.kind != Json::Kind::Object) return false;
	if (c.find("slot") != nullptr) return false;
	if (c.find("setting") != nullptr) return true;
	for (const char *key : {"all", "any"})
		if (const Json *children = c.find(key); children != nullptr && children->kind == Json::Kind::Array)
			return std::all_of(children->items.begin(), children->items.end(),
				[](const Json &x) { return x.kind == Json::Kind::Object && readsOnlySettings(x); });
	if (const Json *sub = c.find("not"); sub != nullptr && sub->kind == Json::Kind::Object) return readsOnlySettings(*sub);
	return false;
}

/* two names for one file: the same file on disk, or the same path when
 * either is not there to ask */
bool sameFile(const std::string &a, const std::string &b)
{
	std::error_code ec;
	const bool same = fs::equivalent(a, b, ec);
	if (!ec) return same;
	return fs::path(a).lexically_normal() == fs::path(b).lexically_normal();
}

/* how many files a slot takes, in Chimera's words */
std::string cardinalityOf(int64_t min, int64_t max)
{
	if (min == 1 && max == 1) return tr("new_project.exactly_one");
	if (min == 0 && max == 1) return tr("new_project.at_most_one");
	if (min == 0 && max == -1) return tr("new_project.any_number");
	if (max == -1) return tr("new_project.at_least", {std::to_string(min)});
	return tr("new_project.from_to", {std::to_string(min), std::to_string(max)});
}

std::string lower(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}

std::string baseName(const std::string &path)
{
	return fs::path(path).filename().string();
}

/* a file's kind: its extension, lower case, no dot ("" for none) */
std::string kindOf(const std::string &name)
{
	const std::string ext = fs::path(name).extension().string();
	return lower(ext.empty() ? ext : ext.substr(1));
}

/* a declaration of the package, its texts in the language in use */
std::string entryText(ce_package *p, const char *name)
{
	uint64_t len = 0;
	const uint8_t *d = ce_package_entry_texts(p, name, &len);
	return d != nullptr ? std::string(reinterpret_cast<const char *>(d), static_cast<size_t>(len)) : std::string();
}

void setMember(Json &obj, const std::string &name, Json v)
{
	obj.kind = Json::Kind::Object;
	for (auto &[k, old] : obj.members)
		if (k == name)
		{
			old = std::move(v);
			return;
		}
	obj.members.emplace_back(name, std::move(v));
}
} // namespace

/* compile sessions of the headless runner, side by side, each a share of the
 * game's code: what each stores ("[cache]
 * stored <name> <sha1>", or fetched, already there) is a module the game
 * needs, and their progress lines add up; when all end well, the modules are
 * the game's manifest.json */
struct NewProjectViewModel::CompileJob
{
	std::string runner, dir, romName, romSha1, core, version, projectFile;
	std::vector<std::string> args;
	int workers = 1;
	/* what one session is counted at (the setting, in bytes; 0 the engine's own figure) */
	uint64_t sessionBytes = 0;
	/* stop: asked to stop; ended: the thread has done all it does (the view
	 * model waits for it before it goes) */
	std::atomic<bool> stop{false}, ended{false};

	/* what an exit code says of how a session ended, as Chimera does */
	static std::string whyItDied(int code)
	{
		switch (code)
		{
		case 137: return tr("compile.killed_oom");
		case 139: return tr("compile.crashed_segv");
		case 134: return "aborted";
		case 132: return tr("compile.crashed_illegal");
		case -1073741819: return tr("compile.crashed_access");
		case -1073741801: return tr("compile.out_of_memory");
		case -1073740791: return tr("compile.crashed_overrun");
		case -1073741571: return tr("compile.crashed_stack");
		case -1073741795: return tr("compile.crashed_illegal");
		case -529697949: return tr("compile.crashed_uncaught");
		default: return tr("compile.exit_code", {std::to_string(code)});
		}
	}
	/* the codes the system gives for running out of memory */
	static bool diedForWantOfMemory(int code) { return code == 137 || code == -1073741801; }
	/* Chimera's words when memory ran out: where the disk space went (issue 259) */
	std::string outOfMemoryMessage(int sessions) const
	{
		const double each = static_cast<double>(sessionBytes != 0 ? sessionBytes : ce_compile_session_bytes()) / (1024.0 * 1024 * 1024);
		char gb[32];
		std::snprintf(gb, sizeof gb, "%.0f", each);
		const std::string what = sessions > 1 ? tr("compile.out_of_memory_sessions", {gb, std::to_string(sessions)}) : tr("compile.out_of_memory_session", {gb});
#ifdef _WIN32
		return what + " " + tr("compile.out_of_memory_windows");
#else
		return what + " " + tr("compile.out_of_memory_advice");
#endif
	}

	using Post = std::function<void(std::function<void()>)>;
	void run(const Post &post, std::function<void(const std::string &, const std::string &)> stored,
		std::function<void(uint32_t, uint32_t)> progress, std::function<void(const std::string &)> finished)
	{
		static const std::regex cacheLine(R"(^\[cache\] (stored|fetched) (\S+) ([0-9A-Fa-f]{40})\s*$)");
		static const std::regex progressLine(R"(^Precompiled (\d+)/(\d+) modules)");
		std::mutex m;
		std::map<std::string, std::string> found;
		std::string failure;
		/* A session the system killed took its share of the work with it, and a
		 * manifest written from what the others managed would have holes in it:
		 * as Chimera's, the answer is to want less at once - half the sessions,
		 * and again - until one alone runs out */
		for (;;)
		{
			std::vector<uint32_t> done(static_cast<size_t>(workers), 0), total(static_cast<size_t>(workers), 0);
			std::vector<std::unique_ptr<Subprocess>> sessions;
			for (int i = 0; i < workers; i++)
			{
				auto p = std::make_unique<Subprocess>();
				p->onLine = [&, i](const std::string &line) {
					std::smatch g;
					if (std::regex_match(line, g, cacheLine))
					{
						const std::string name = g[2], sha = upper(g[3]);
						std::lock_guard<std::mutex> lock(m);
						if (found.emplace(name, sha).second) post([stored, name, sha] { stored(name, sha); });
					}
					else if (std::regex_search(line, g, progressLine))
					{
						std::lock_guard<std::mutex> lock(m);
						done[static_cast<size_t>(i)] = static_cast<uint32_t>(std::stoul(g[1]));
						total[static_cast<size_t>(i)] = static_cast<uint32_t>(std::stoul(g[2]));
						uint32_t d = 0, t = 0;
						for (size_t k = 0; k < done.size(); k++) d += done[k], t = std::max(t, total[k]);
						post([progress, d, t] { progress(d, t); });
					}
				};
				std::vector<std::string> a = args;
				a.insert(a.begin() + 2, {"--precompile", std::to_string(i) + "/" + std::to_string(workers)});
				std::string why;
				if (!p->start(runner, a, why))
				{
					failure = tr("compile.not_started", {why});
					break;
				}
				sessions.push_back(std::move(p));
			}
			/* waited for, or stopped; the least the machine could spare while
			 * they ran, looked at every couple of seconds */
			bool stopped = false;
			uint64_t leastToSpare = 0;
			for (int looks = 0;; looks++)
			{
				bool running = false;
				for (auto &p : sessions) running = running || p->wait(0) < 0;
				if (!running) break;
				if (looks % 40 == 0)
				{
					const uint64_t now = ce_compile_memory_to_spare();
					if (now != 0 && (leastToSpare == 0 || now < leastToSpare)) leastToSpare = now;
				}
				if (stop && !stopped)
				{
					stopped = true;
					for (auto &p : sessions) p->kill();
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
			}
			if (!failure.empty()) break;
			if (stopped)
			{
				failure = tr("compile.stopped");
				break;
			}
			/* the sessions that died, and in what words; one's own last line is worth more than its code */
			std::vector<int> dead;
			std::string said, why;
			for (auto &p : sessions)
			{
				const int code = p->wait(-1);
				if (code == 0) continue;
				dead.push_back(code);
				if (said.empty())
				{
					std::string tail = p->errorTail();
					while (!tail.empty() && tail.back() == '\n') tail.pop_back();
					const size_t nl = tail.rfind('\n');
					said = tail.substr(nl == std::string::npos ? 0 : nl + 1);
				}
				const std::string w = whyItDied(code);
				if (why.find(w) == std::string::npos) why += (why.empty() ? "" : ", ") + w;
			}
			if (dead.empty()) break;
			const bool allForMemory = std::all_of(dead.begin(), dead.end(), diedForWantOfMemory);
			/* memory is what ran out when the system says so, when a session said the
			 * sandbox could not get its memory, or when the machine had next to
			 * nothing left while they ran */
			const bool wantOfMemory = ce_compile_memory_ran_short(leastToSpare, sessionBytes) != 0 || allForMemory ||
				said.find("memory block") != std::string::npos;
			if (wantOfMemory && workers > 1)
			{
				workers = std::max(1, workers / 2);
				std::fprintf(stderr, "precompile: memory ran out; retrying with %d session(s)\n", workers);
				continue;
			}
			if (wantOfMemory)
				failure = outOfMemoryMessage(static_cast<int>(sessions.size())) + (allForMemory ? "" : " " + tr("compile.sessions_ended", {why}));
			else
				failure = tr("compile.sessions_failed", {std::to_string(dead.size()), std::to_string(sessions.size()), said.empty() ? why : why + " (" + said + ")"})
					+ " " + tr(workers > 1 ? "compile.fewer_at_once" : "compile.one_at_a_time");
			break;
		}
		std::error_code ec;
		fs::remove(projectFile, ec);
		if (failure.empty())
		{
			/* what the sessions compiled is what this game needs, with who compiled it */
			json::Json manifest;
			manifest.kind = json::Json::Kind::Object;
			manifest.members.emplace_back("rom", json::Json::string(romName));
			manifest.members.emplace_back("romSha1", json::Json::string(romSha1));
			manifest.members.emplace_back("core", json::Json::string(core));
			manifest.members.emplace_back("coreVersion", json::Json::string(version));
			const std::time_t now = std::time(nullptr);
			char when[32] = {};
			if (const std::tm *t = std::gmtime(&now)) std::strftime(when, sizeof when, "%Y-%m-%dT%H:%M:%SZ", t);
			manifest.members.emplace_back("compiled", json::Json::string(when));
			json::Json files;
			files.kind = json::Json::Kind::Array;
			for (const auto &[name, sha] : found)
			{
				json::Json f;
				f.kind = json::Json::Kind::Object;
				f.members.emplace_back("name", json::Json::string(name));
				f.members.emplace_back("sha1", json::Json::string(sha));
				files.items.push_back(std::move(f));
			}
			manifest.members.emplace_back("files", std::move(files));
			std::ofstream(fs::path(dir) / "manifest.json", std::ios::binary) << json::write(manifest);
			if (found.empty()) failure = tr("compile.stored_nothing");
		}
		post([finished, failure] { finished(failure); });
	}
};

NewProjectViewModel::NewProjectViewModel(Dispatcher &dispatcher, SessionViewModel &session, CoresViewModel &cores)
	: ViewModel("newProject", dispatcher), session_(session), cores_(cores)
{
	expose("coreNames", "the installed cores, a line each", coreNames);
	expose("kind", "which cores are listed: \"emulators\" or \"games\"", kind, true);
	expose("coreLines", "the packages of that kind, a line each: the core, its version dated, its systems", coreLines);
	expose("coreLine", "the line chosen, -1 for none", coreLine);
	expose("systems", "the chosen package's systems", systems);
	expose("renderers", "the machine's renderers, a line each, by what they are", renderers);
	expose("renderer", "the renderer chosen, by line", renderer);
	expose("rendererCaveat", "what the renderer chosen costs (\"\" for a software one)", rendererCaveat);
	expose("core", "the core chosen", core);
	expose("machines", "the chosen package's machines, a line each (\"\" for a package of one)", machines);
	expose("machine", "the machine chosen, by its place in machines", machine);
	expose("title", "the project's title (the first game file's name when empty)", title, true);
	clockStart.set(clockText(CE_CLOCK_START_DEFAULT));
	expose("clockStart", "when the machine's clock starts, UTC, written as 2017-05-27 12:44:28 (from 1970 to 2200)", clockStart, true);
	expose("problems", "what keeps the project from being made, a line each; \"\" when ready", problems);
	expose("compileSessionGigabytes", "how much memory one compile session is counted at, in GB (1 to 256; Tools > "
		"Pre-Compiled Modules' setting)", compileSessionGigabytes, true);
	expose("ready", "the project can be made", ready);
	expose("status", "what New Project last had to say", status);
	expose("filesAsked", "the files page is asked: the package declares slots", filesAsked);
	expose("filesComplaint", "what keeps the files page from going on, in Chimera's words (\"\" for nothing)", filesComplaint);
	expose("firmwareAsked", "the firmware page is asked: the machine, files and settings call for firmware", firmwareAsked);
	expose("firmwareMissing", "the firmware required and not provided, by name (\"\" for none)", firmwareMissing);
	command("clearFirmware", "the firmware page's Clear: no file for this firmware, until the page is shown again", {"id"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id;
			if (!arg(a, 0, id, error)) return {};
			chosenFirmware_.erase(id);
			if (std::find(clearedFirmware_.begin(), clearedFirmware_.end(), id) == clearedFirmware_.end()) clearedFirmware_.push_back(id);
			evaluate();
			return {};
		});
	command("firmwarePage", "the firmware page shown: what is found is found again, the files chosen by hand kept", {},
		[this](const Args &, std::string &) -> Value {
			clearedFirmware_.clear();
			evaluate();
			return {};
		});
	form_.listMachineSetting(false);
	form_.unlist("renderer");
	rows("slots", "the kinds of file the project holds: id, title, min, max, formats, help, exposed, files, count, filter", slotRows_);
	rows("settings", "the settings that apply, as the Settings window lists them", settingRows_);
	rows("presets", "the presets this machine is offered: id, label, description", presetRows_);
	command("applyPreset", "writes a preset's values into the settings", {"id"}, [this](const Args &a, std::string &error) -> Value {
		std::string id, label;
		if (!arg(a, 0, id, error)) return {};
		std::vector<std::string> skipped;
		int64_t applied = 0;
		if (!form_.applyPreset(id, label, skipped, error, &applied)) return {};
		/* in Chimera's words */
		std::string none;
		for (size_t i = 0; i < skipped.size(); i++) none += (i == 0 ? "" : ", ") + skipped[i];
		const std::string note = skipped.empty() ? tr("new_project.preset_applied", applied, {label})
												 : tr("new_project.preset_applied_skipped", applied, {label, none});
		status.set(note);
		evaluate();
		return {};
	});
	expose("compileAsked", "Chimera's compile step applies: the package says precompile and a game is chosen", compileAsked);
	expose("compiling", "compile sessions are running", compiling);
	expose("compileFolder", "the folder the compile sessions are filling, \"\" while none runs", compileFolder);
	expose("compileReady", "the game's code is compiled: every module its manifest lists is there", compileReady);
	expose("compileStatus", "what the compile step says, in Chimera's words", compileStatus);
	expose("compileDone", "modules compiled, all sessions together", compileDone);
	expose("compileTotal", "modules the game has (0 until a session says)", compileTotal);
	expose("compileRunner", "the headless runner the compile sessions are (chimera-run)", compileRunner, true);
	expose("compileRoot", "where compiled code is kept, a folder a game by its SHA1", compileRoot, true);
	rows("compiled", "the game's compiled modules: name, sha1, present", moduleRows_);
	command("compile", "compiles the game's code in compile sessions side by side, as Chimera's Compile button does", {},
		[this](const Args &, std::string &error) -> Value {
			if (!compileAsked.get())
			{
				error = tr("compile.nothing_to_compile");
				return {};
			}
			if (compiling.get())
			{
				error = tr("compile.already");
				return {};
			}
			const std::string game = compileGame(), dir = compileDirOf(game);
			if (dir.empty() || compileRunner.get().empty())
			{
				error = tr(dir.empty() ? "compile.no_folder" : "compile.no_runner");
				return {};
			}
			/* the folder made this build's: code another build compiled is
			 * removed, or the sessions would take it as already compiled */
			const compiled::Build build = compiled::buildOf(package_).value_or(compiled::Build{core.get(), version_});
			compiled::claim(dir, build);
			char sha[41] = {};
			ce_sha1_file(game.c_str(), sha, nullptr);
			/* everything a session boots with: the settings, the firmware, the other files and the slot map */
			auto job = std::make_shared<CompileJob>();
			job->runner = compileRunner.get();
			job->dir = dir;
			job->romName = fs::path(game).filename().string();
			job->romSha1 = sha;
			job->core = build.core;
			job->version = build.version;
			/* as many side by side as the machine can give each what the setting counts it at */
			job->sessionBytes = static_cast<uint64_t>(std::clamp<int64_t>(compileSessionGigabytes.get(), 1, 256)) << 30;
			job->workers = std::max(1, ce_compile_sessions(static_cast<int32_t>(std::max(1u, std::thread::hardware_concurrency())),
				ce_compile_memory_to_spare(), job->sessionBytes));
			/* a game runs only in a project (owner, 2026-10-26): the sessions
			 * open one of every file picked, in its slot, with the settings
			 * chosen, saved beside the compiled code while they run */
			std::vector<AskFile> picked;
			for (const Slot &sl : slots_)
				if (const auto it = files_.find(sl.id); it != files_.end())
					for (const Picked &f : it->second) picked.push_back({sl.id, f.name, f.path});
			/* outside the compiled code's folder, which counts what is in it */
			job->projectFile = (fs::temp_directory_path() / ("chimera-compile-" + std::string(sha) + "-" +
				std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".chimeraProject")).string();
			std::vector<std::string> dirs;
			std::string why;
			if (!saveAskingProject(package_, picked, json::write(form_.recorded()), job->projectFile, dirs, why))
			{
				error = tr("compile.no_project", {why});
				return {};
			}
			std::vector<std::string> common = {"--project", job->projectFile, package_, "--cache-dir", dir};
			for (const std::string &d : dirs) common.insert(common.end(), {"--files", d});
			for (const Need &n : needs_)
				if (!n.path.empty()) common.insert(common.end(), {"--firmware", n.id + "=" + n.path});
			job->args = common;
			compileJob_ = job;
			modules_.clear();
			moduleRows_.reset.emit();
			compiling.set(true);
			compileFolder.set(job->dir);
			compileReady.set(false);
			compileDone.set(0);
			compileTotal.set(0);
			compileStatus.set(tr("compile.compiling"));
			std::thread([this, job] {
				job->run([this](std::function<void()> fn) { later(std::move(fn)); }, [this](const std::string &name, const std::string &sha1) {
					modules_.push_back({name, sha1, true});
					moduleRows_.reset.emit();
				}, [this](uint32_t done, uint32_t total) {
					compileDone.set(done);
					compileTotal.set(total);
					compileStatus.set(tr("compile.progress", {std::to_string(done), std::to_string(total)}));
				}, [this](const std::string &failure) {
					compiling.set(false);
					compileFolder.set("");
					compileJob_.reset();
					if (!failure.empty())
					{
						compileStatus.set(failure);
						return;
					}
					compileSeen_.clear();
					refreshCompile();
				});
				job->ended = true;
			}).detach();
			return {};
		}, &compileAsked);
	command("stopCompile", "stops the compile sessions; what they stored stays, and Compile goes on from there", {},
		[this](const Args &, std::string &) -> Value {
			if (compileJob_) compileJob_->stop = true;
			return {};
		});
	rows("firmware", "the firmware the machine needs: id, name, the file, how it was found, missing; and as the firmware page "
		"shows it: title, expected (the file), sha1Shown, state, satisfied, description", firmwareRows_);
	expose("suggestion", "what the core suggests for the game chosen, and what of it was applied", suggestion);
	expose("importMenu", "the movie import's menu item (\"\" when the core imports none)", importMenu);
	expose("importMovieLabel", "what the movie to import is", importMovieLabel);
	expose("importMovieFilter", "the kinds of file it comes in, for a file dialog", importMovieFilter);
	expose("importMovie", "the movie picked to import", importMovie);
	expose("importNotes", "the notes of the core's answer, a line each", importNotes);
	expose("imported", "the project will take the imported movie's inputs", imported);
	expose("clockWrong", "the last create was refused for the clock's start, which is not a time Chimera reads", clockWrong);
	expose("making", "a project is being made: Create read and hashes its files away from the view", making);
	expose("made", "how many projects were made; goes up once each is made, when it opens", made);
	expose("filesRefused", "the last making was refused for its files (the reason is the status)", filesRefused);
	expose("importFailure", "why the last import was not taken (\"\" when it was), in Chimera's words", importFailure);
	expose("importSummary", "what the core said of the movie taken, when it noted anything (Chimera's message)", importSummary);
	expose("importsTaken", "goes up with each import taken", importsTaken);
	rows("importFiles", "the files the import reads: option, label, filter, required, more than one, the files picked", importFileRows_);
	rows("importOptions", "the import's options: name, label, ticked", importOptionRows_);
	command("importSetMovie", "the movie to import", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		std::error_code ec;
		if (import_.kind != Json::Kind::Object || !fs::is_regular_file(path, ec))
		{
			error = import_.kind != Json::Kind::Object ? tr("import.no_import") : tr("new_project.not_a_file", {path});
			return {};
		}
		importMovie.set(path);
		return {};
	});
	command("importAddFile", "a file the import reads, for one of its inputs", {"option", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string option, path;
			if (!arg(a, 0, option, error) || !arg(a, 1, path, error)) return {};
			const Json *input = importInput(option);
			std::error_code ec;
			if (input == nullptr || !fs::is_regular_file(path, ec))
			{
				error = input == nullptr ? tr("import.no_input", {option}) : tr("new_project.not_a_file", {path});
				return {};
			}
			const Json *many = input->find("multiple");
			std::vector<std::string> &list = importPicked_[option];
			if (many == nullptr || !many->boolean) list.clear();
			/* each file is read under its own name: two of one name cannot both be */
			const std::string name = lower(baseName(path));
			for (const auto &[other, picked] : importPicked_)
				for (const std::string &p : picked)
					if (lower(baseName(p)) == name)
					{
						importFileRows_.reset.emit();
						error = tr("new_project.two_files_named", {baseName(path)});
						return {};
					}
			list.push_back(path);
			importFileRows_.reset.emit();
			return {};
		});
	command("importRemoveFile", "takes a file out of an import input", {"option", "index"},
		[this](const Args &a, std::string &error) -> Value {
			std::string option;
			int64_t i = 0;
			if (!arg(a, 0, option, error) || !arg(a, 1, i, error)) return {};
			auto it = importPicked_.find(option);
			if (it == importPicked_.end() || i < 0 || i >= static_cast<int64_t>(it->second.size()))
			{
				error = tr("session.no_such_file");
				return {};
			}
			it->second.erase(it->second.begin() + static_cast<long>(i));
			importFileRows_.reset.emit();
			return {};
		});
	command("importMoveFile", "moves a file within an import input that takes several (Up and Down)", {"option", "from", "to"},
		[this](const Args &a, std::string &error) -> Value {
			std::string option;
			int64_t from = 0, to = 0;
			if (!arg(a, 0, option, error) || !arg(a, 1, from, error) || !arg(a, 2, to, error)) return {};
			auto it = importPicked_.find(option);
			const int64_t n = it == importPicked_.end() ? 0 : static_cast<int64_t>(it->second.size());
			if (from < 0 || from >= n || to < 0 || to >= n)
			{
				error = tr("session.no_such_file");
				return {};
			}
			std::string moved = it->second[static_cast<size_t>(from)];
			it->second.erase(it->second.begin() + static_cast<long>(from));
			it->second.insert(it->second.begin() + static_cast<long>(to), moved);
			importFileRows_.reset.emit();
			return {};
		});
	command("importSetOption", "ticks an import option, or not", {"name", "on"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		bool on = false;
		if (!arg(a, 0, name, error) || !arg(a, 1, on, error)) return {};
		importOptions_[name] = on;
		importOptionRows_.reset.emit();
		return {};
	});
	command("importRun", "asks the core what the movie amounts to, and fills the form with its answer", {},
		[this](const Args &, std::string &error) -> Value {
			if (import_.kind != Json::Kind::Object)
			{
				error = tr("import.no_import");
				return {};
			}
			/* what is missing, in Chimera's words */
			if (importMovie.get().empty())
			{
				error = importMovieLabel.get().empty() ? tr("import.choose_movie") : tr("import.choose_named", {lower(importMovieLabel.get())});
				importFailure.set(error);
				return {};
			}
			/* each picked file under its own name, its input's option naming it
			 * (a multiple input's names joined); options only when ticked */
			std::vector<AskFile> mounts;
			Json settings;
			settings.kind = Json::Kind::Object;
			if (const Json *files = import_.find("files"))
				for (const Json &f : files->items)
				{
					/* the slot its files go in; none for a file the core may take
					 * as firmware, which only its answer can name */
					const Json *firmwareFlag = f.find("firmware");
					const Json *slotOf = f.find("slot");
					const std::string slot = (firmwareFlag != nullptr && firmwareFlag->boolean) || slotOf == nullptr ? std::string() : slotOf->text;
					const Json *opt = f.find("option");
					const std::string option = opt != nullptr ? opt->text : "";
					const auto it = importPicked_.find(option);
					const bool none = it == importPicked_.end() || it->second.empty();
					if (none)
					{
						if (const Json *req = f.find("required"); req != nullptr && req->boolean)
						{
							const Json *label = f.find("label");
							error = tr("import.choose_input", {label != nullptr ? label->text : option});
							importFailure.set(error);
							return {};
						}
						continue;
					}
					std::string names;
					const Json *sep = f.find("separator");
					for (const std::string &p : it->second)
					{
						const std::string name = baseName(p);
						mounts.push_back({slot, name, p});
						names += (names.empty() ? "" : (sep != nullptr ? sep->text : std::string(";"))) + name;
					}
					if (!option.empty()) setMember(settings, option, Json::string(names));
				}
			for (const auto &[name, on] : importOptions_)
				if (on) setMember(settings, name, Json::boolean_(true));
			status.set(tr("import.asking", {core.get(), baseName(importMovie.get())}));
			importFailure.set("");
			/* an answer to a question since withdrawn (the dialog closed, another
			 * core chosen, another import asked) is no answer */
			const uint64_t ticket = ++importTicket_;
			auto done = [this, ticket](std::string answer, std::string why) {
				if (ticket != importTicket_) return;
				if (answer.empty())
				{
					status.set(tr("import.not_imported", {why.empty() ? tr("import.core_gave_no_answer", {core.get()}) : why}));
					importFailure.set(why.empty() ? tr("import.no_answer") : why);
					return;
				}
				applyImport(answer);
			};
			if (importRunner_)
				importRunner_(package_, importMovie.get(), mounts, json::write(settings), done);
			else
			{
				/* asked of a project (owner, 2026-10-26): the files with a slot in
				 * it, the options its settings, the rest beside the movie */
				std::string why;
				ce_project *project = askingProject(package_, mounts, json::write(settings), why);
				if (project == nullptr)
				{
					done({}, why);
					return {};
				}
				std::vector<const char *> names, paths;
				for (const AskFile &m : mounts)
					if (m.slot.empty())
					{
						names.push_back(m.name.c_str());
						paths.push_back(m.path.c_str());
					}
				uint64_t len = 0;
				const char *err = nullptr;
				const char *answer = ce_project_import_movie(package_.c_str(), project, importMovie.get().c_str(), names.data(), paths.data(),
					static_cast<int32_t>(names.size()), nullptr, nullptr, nullptr, 0, &len, &err);
				ce_project_free(project);
				done(answer != nullptr ? std::string(answer, static_cast<size_t>(len)) : std::string(), err != nullptr ? err : "");
			}
			return {};
		});
	command("importCancel", "the import asked is let go: its answer, when it comes, is not taken", {}, [this](const Args &, std::string &) -> Value {
		++importTicket_;
		return {};
	});
	command("importForget", "the project will not take the imported movie's inputs", {}, [this](const Args &, std::string &) -> Value {
		++importTicket_;
		pendingInput_.clear();
		imported.set(false);
		importNotes.set("");
		return {};
	});

	command("refresh", "looks at the cores folder again and lists its cores: a package put in or taken out while the program "
		"runs (issue 423)", {}, [this](const Args &, std::string &) -> Value {
		std::string looked;
		cores_.run("refresh", {}, looked);
		return {};
	});
	command("chooseKind", "lists emulators (\"emulators\") or game cores (\"games\")", {"kind"},
		[this](const Args &a, std::string &error) -> Value {
			std::string k;
			if (!arg(a, 0, k, error)) return {};
			if (k != "emulators" && k != "games")
			{
				error = tr("new_project.kind_values");
				return {};
			}
			kind.set(k);
			return {};
		});
	command("chooseRenderer", "the renderer, by its line in renderers", {"line"}, [this](const Args &a, std::string &error) -> Value {
		int64_t i = 0;
		if (!arg(a, 0, i, error)) return {};
		const Json *decl = form_.applies("renderer") ? form_.declaration("renderer") : nullptr;
		const Json *options = decl != nullptr ? decl->find("options") : nullptr;
		const int64_t count = options != nullptr && !options->items.empty() ? static_cast<int64_t>(options->items.size()) : 1;
		if (i < 0 || i >= count)
		{
			error = tr("new_project.no_renderer", {std::to_string(i)});
			return {};
		}
		if (options != nullptr && !options->items.empty())
		{
			Json values = form_.values();
			setMember(values, "renderer", Json::string(options->items[static_cast<size_t>(i)].text));
			form_.setValues(values);
		}
		evaluate();
		return {};
	});
	command("chooseCoreLine", "starts the project over with the package on this line of coreLines", {"line"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			if (i < 0 || i >= static_cast<int64_t>(shown_.size()))
			{
				error = tr("new_project.no_line", {std::to_string(i)});
				return {};
			}
			const CoresViewModel::Package chosen = shown_[static_cast<size_t>(i)];
			chooseCore(chosen.name, error, chosen.path);
			return {};
		});
	command("chooseCore", "starts the project over with this installed core", {"name"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		chooseCore(name, error);
		return {};
	});
	command("chooseMachine", "the machine the project is, by its place in machines", {"index"},
		[this](const Args &a, std::string &error) -> Value {
			int64_t i = 0;
			if (!arg(a, 0, i, error)) return {};
			const Json *list = cfg_.find("machines");
			if (list == nullptr || i < 0 || i >= static_cast<int64_t>(list->items.size()))
			{
				error = tr("new_project.no_machine", {std::to_string(i)});
				return {};
			}
			const Json &m = list->items[static_cast<size_t>(i)];
			const Json *when = m.find("when");
			const Json *id = m.find("id");
			const std::string value = when != nullptr && !when->items.empty() ? when->items[0].text : id != nullptr ? id->text : "";
			Json values = form_.values();
			setMember(values, form_.machineSetting(), Json::string(value));
			form_.setValues(values);
			machine.set(i);
			evaluate();
			return {};
		});
	command("addFile", "puts a file in a slot", {"slot", "path"}, [this](const Args &a, std::string &error) -> Value {
		std::string id, path;
		if (!arg(a, 0, id, error) || !arg(a, 1, path, error)) return {};
		const Slot *s = slot(id);
		if (s == nullptr)
		{
			error = core.get().empty() ? tr("new_project.choose_core_first") : tr("new_project.no_slot", {id});
			return {};
		}
		if (!s->exposed)
		{
			error = tr("new_project.slot_not_with_choice", {s->title});
			return {};
		}
		const std::string name = baseName(path), kind = kindOf(name);
		const std::vector<std::string> formats = formatsFor(*s);
		if (!s->namePattern.empty())
		{
			bool matches = false;
			try
			{
				matches = std::regex_match(name, std::regex(s->namePattern));
			}
			catch (const std::regex_error &)
			{
				matches = true; /* a pattern this cannot read is the core's to enforce */
			}
			if (!matches)
			{
				error = name + ": " + (!s->nameHelp.empty() ? s->nameHelp : s->help);
				return {};
			}
		}
		/* the same file in the same slot again: nothing to do, as Chimera's */
		std::vector<Picked> &list = files_[id];
		if (std::any_of(list.begin(), list.end(), [&](const Picked &f) { return f.name == name; })) return {};
		std::error_code ec;
		if (!fs::is_regular_file(path, ec))
		{
			error = tr("new_project.not_a_file", {path});
			return {};
		}
		for (const auto &[other, picked] : files_)
			for (const Picked &f : picked)
				if (f.name == name)
				{
					error = tr("new_project.file_already_in", {name});
					return {};
				}
		/* a cue's tracks are taken from beside it when the project is made:
		 * one that is not there is said now, not then */
		if (kind == "cue")
		{
			std::ifstream in(path, std::ios::binary);
			const std::string cue((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
			uint64_t len = 0;
			const char *refs = ce_cue_references(cue.data(), cue.size(), &len);
			Json tracks;
			std::string jerr;
			if (refs != nullptr && json::parse(std::string_view(refs, static_cast<size_t>(len)), tracks, jerr))
				for (const Json &t : tracks.items)
					if (!fs::is_regular_file(fs::path(path).parent_path() / t.text, ec))
					{
						error = tr("new_project.cue_reference_missing", {name, t.text});
						return {};
					}
		}
		list.push_back({name, path});
		/* A slot that names its formats has still not been given a rule: a
		 * file of another kind is said, not refused, as Chimera's. More files
		 * than a slot takes are said when the files page goes on. */
		std::string formatsText;
		for (const std::string &f : formats) formatsText += (formatsText.empty() ? "" : ", ") + (f.empty() ? tr("new_project.no_extension") : "." + f);
		const bool expected = formats.empty() || std::find(formats.begin(), formats.end(), kind) != formats.end();
		status.set(expected ? std::string() : tr("new_project.unexpected_format", {name, formatsText}));
		evaluate();
		return {};
	});
	command("removeFile", "takes a file out of a slot", {"slot", "index"}, [this](const Args &a, std::string &error) -> Value {
		std::string id;
		int64_t i = 0;
		if (!arg(a, 0, id, error) || !arg(a, 1, i, error)) return {};
		auto it = files_.find(id);
		if (it == files_.end() || i < 0 || i >= static_cast<int64_t>(it->second.size()))
		{
			error = tr("new_project.no_file_in", {std::to_string(i), id});
			return {};
		}
		it->second.erase(it->second.begin() + static_cast<long>(i));
		evaluate();
		return {};
	});
	command("moveFile", "moves a file within its slot (the order discs are swapped in)", {"slot", "from", "to"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id;
			int64_t from = 0, to = 0;
			if (!arg(a, 0, id, error) || !arg(a, 1, from, error) || !arg(a, 2, to, error)) return {};
			auto it = files_.find(id);
			const int64_t n = it == files_.end() ? 0 : static_cast<int64_t>(it->second.size());
			if (from < 0 || from >= n || to < 0 || to >= n)
			{
				error = tr("new_project.no_place_in", {id});
				return {};
			}
			Picked f = it->second[static_cast<size_t>(from)];
			it->second.erase(it->second.begin() + static_cast<long>(from));
			it->second.insert(it->second.begin() + static_cast<long>(to), f);
			evaluate();
			return {};
		});
	command("set", "changes a setting that applies", {"name", "value"}, [this](const Args &a, std::string &error) -> Value {
		std::string name;
		if (!arg(a, 0, name, error)) return {};
		if (a.size() < 2)
		{
			error = tr("new_project.argument_missing", {"2"});
			return {};
		}
		if (!form_.set(name, a[1], error)) return {};
		evaluate();
		return {};
	});
	command("chooseFirmware", "a file chosen by hand for a firmware the machine needs", {"id", "path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id, path;
			if (!arg(a, 0, id, error) || !arg(a, 1, path, error)) return {};
			const auto need = std::find_if(needs_.begin(), needs_.end(), [&](const Need &n) { return n.id == id; });
			if (need == needs_.end())
			{
				error = tr("new_project.no_firmware_need", {id});
				return {};
			}
			char sha[41];
			uint64_t len = 0;
			if (ce_sha1_file(path.c_str(), sha, &len) != 1)
			{
				error = tr("new_project.cannot_read", {path});
				return {};
			}
			/* an entry with a hash names one exact file; a game core takes a
			 * file of the person's own instead and the project pins its hash,
			 * as Chimera's New Project does */
			std::string declared;
			if (const Json decls = firmwareDecls(); need->declIndex < decls.items.size())
				if (const Json *h = decls.items[need->declIndex].find("sha1")) declared = h->text;
			const bool custom = !declared.empty() && lower(declared) != lower(sha);
			const Json *kind = cfg_.find("kind");
			const bool gameCore = kind != nullptr && lower(kind->text) == "game";
			if (custom && !gameCore)
			{
				error = tr("new_project.not_this_file", {baseName(path), sha, declared});
				return {};
			}
			chosenFirmware_[id] = {path, sha};
			clearedFirmware_.erase(std::remove(clearedFirmware_.begin(), clearedFirmware_.end(), id), clearedFirmware_.end());
			status.set(custom ? tr("new_project.own_firmware", {baseName(path), id, sha}) : std::string());
			evaluate();
			return {};
		});
	command("scanFolder", "looks through a folder (and its subfolders, unless recurse is false; ten thousand files at most) "
		"for the firmware still missing: by hash where the package gives one, else by the name it declares, as Chimera's "
		"Scan Folder does. A folder that has some becomes the firmware folder when the one set is not there.",
		{"folder", "recurse"}, [this](const Args &a, std::string &error) -> Value {
			std::string folder;
			bool recurse = true;
			if (!arg(a, 0, folder, error)) return {};
			if (a.size() > 1 && !arg(a, 1, recurse, error)) return {};
			std::error_code ec;
			if (!fs::is_directory(folder, ec))
			{
				error = tr("new_project.not_a_folder", {folder});
				return {};
			}
			const std::vector<FirmwareFile> found = indexFirmware({folder}, recurse, 10000);
			firmwareIndex_.insert(firmwareIndex_.end(), found.begin(), found.end());
			const Json decls = firmwareDecls();
			/* the entry the page asks for, not another with its id (#390) */
			auto declOf = [&](const Need &n) -> const Json * {
				return n.declIndex < decls.items.size() ? &decls.items[n.declIndex] : nullptr;
			};
			auto named = [&](const Json &d) -> const FirmwareFile * {
				const Json *n = d.find("name");
				if (n == nullptr || n->text.empty()) return nullptr;
				/* and its size, where the package gives one (issue 379) */
				const Json *sz = d.find("size");
				const int64_t size = sz != nullptr ? static_cast<int64_t>(sz->number()) : 0;
				for (const FirmwareFile &f : found)
					if (lower(f.name) == lower(n->text) && (size == 0 || f.size == size)) return &f;
				return nullptr;
			};
			/* a file this page asks for is in that folder: it is where the
			 * person keeps firmware, if the folder set is not there (#293) */
			bool any = false;
			for (const Need &n : needs_)
				if (const Json *d = declOf(n))
				{
					std::string how;
					any = any || findFirmware(*d, "", found, how) != nullptr || named(*d) != nullptr;
				}
			if (any && !fs::is_directory(cores_.firmwareFolder.get(), ec)) cores_.firmwareFolder.set(fs::absolute(folder, ec).string());
			int64_t filled = 0;
			const std::vector<Need> needs = needs_;
			for (const Need &n : needs)
			{
				const Json *d = declOf(n);
				if (!n.path.empty() || d == nullptr) continue;
				if (const Json *sha = d->find("sha1"); sha != nullptr && !sha->text.empty())
				{
					std::string how;
					if (const FirmwareFile *f = findFirmware(*d, "", found, how))
					{
						chosenFirmware_[n.id] = {f->path, f->hash()};
						filled++;
					}
					continue;
				}
				/* no hash: the declared name is the only lead */
				if (const FirmwareFile *f = named(*d))
				{
					std::string e;
					if (run("chooseFirmware", {n.id, f->path}, e)) filled++;
				}
			}
			evaluate();
			/* in Chimera's words */
			const std::string scanned = tr("new_project.scanned", static_cast<int64_t>(found.size()));
			status.set(filled == 0 ? tr("new_project.scan_nothing", {scanned}) : tr("new_project.scan_resolved", filled, {scanned}));
			return filled;
		});
	command("forgetFirmware", "a firmware chosen by hand goes back to the one found", {"id"},
		[this](const Args &a, std::string &error) -> Value {
			std::string id;
			if (!arg(a, 0, id, error)) return {};
			chosenFirmware_.erase(id);
			evaluate();
			return {};
		});
	command("startFrom", "starts the form from a game file, as Chimera does with a file dropped on its window: the core "
		"that takes its kind of file (on the last project's answers when they are for that core: its files too only when "
		"this file is one of them), and the file in the slot that takes it, in place of what that slot held when it holds "
		"no more", {"path"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			if (!arg(a, 0, path, error)) return {};
			const std::string kind = kindOf(baseName(path));
			const auto cores = cores_.coresFor(kind);
			if (cores.empty())
			{
				error = tr("new_project.no_core_takes", {kind});
				return false;
			}
			const auto &[name, package] = cores.front();
			std::string e;
			/* the last project's answers are worth having only for the same core,
			 * and its files only for the same game: one of its own files dropped
			 * again brings back the rest (its other disks), and another game's
			 * files are not this one's (issue 398) */
			const SessionViewModel::ProjectAnswers &last = session_.lastAnswers();
			if (last.coreName == name)
			{
				const bool sameGame = std::any_of(last.files.begin(), last.files.end(),
					[&](const std::pair<std::string, std::string> &f) { return sameFile(f.second, path); });
				run("openOnLastProject", {sameGame}, e);
			}
			if (package_ != package && !chooseCore(name, error, package)) return false;
			/* the first slot that takes the kind, else the first */
			const Slot *into = nullptr;
			for (const Slot &s : slots_)
			{
				const std::vector<std::string> formats = formatsFor(s);
				if (into == nullptr && std::find(formats.begin(), formats.end(), kind) != formats.end()) into = &s;
			}
			if (into == nullptr && !slots_.empty()) into = &slots_.front();
			if (into == nullptr)
			{
				error = tr("new_project.no_file_slot", {core.get()});
				return false;
			}
			const std::string slotId = into->id;
			if (auto it = files_.find(slotId); it != files_.end() && into->max >= 0 && static_cast<int64_t>(it->second.size()) >= into->max)
			{
				it->second.clear();
				evaluate();
			}
			if (!run("addFile", {slotId, path}, error)) return false;
			return true;
		});
	command("openOnLastProject", "fills the form with the last project's answers, as Chimera's New Project opens: its core, "
		"its settings, and its files that are still where they were found (left out when files is false); its settings "
		"are its own, so what the core would suggest is not put over them. False when no project was opened, or its core "
		"is not installed",
		{"files"}, [this](const Args &a, std::string &error) -> Value {
			bool withFiles = true;
			if (!a.empty() && !arg(a, 0, withFiles, error)) return {};
			const SessionViewModel::ProjectAnswers &last = session_.lastAnswers();
			if (last.coreName.empty())
			{
				error = tr("new_project.none_opened");
				return false;
			}
			if (!chooseCore(last.coreName, error, last.package)) return false;
			Json values;
			std::string jerr;
			if (!json::parse(last.settings, values, jerr) || values.kind != Json::Kind::Object) values = Json(), values.kind = Json::Kind::Object;
			seeding_ = true;
			/* the machine its settings name, then the settings */
			if (const Json *chosen = values.find(form_.machineSetting()); chosen != nullptr && !form_.machineSetting().empty())
				if (const Json *list = cfg_.find("machines"))
					for (size_t i = 0; i < list->items.size(); i++)
						if (const Json *when = list->items[i].find("when"))
							for (const Json &w : when->items)
								if (w.text == chosen->text)
								{
									std::string e;
									run("chooseMachine", {static_cast<int64_t>(i)}, e);
								}
			form_.setValues(values);
			std::error_code ec;
			for (const auto &[slotId, path] : last.files)
			{
				if (!withFiles || !fs::is_regular_file(path, ec)) continue;
				/* a hand-written project may name no slot this package
				 * declares: its game goes in the first */
				const std::string into = slot(slotId) != nullptr || slots_.empty() ? slotId : slots_.front().id;
				std::string e;
				run("addFile", {into, path}, e);
			}
			seeding_ = false;
			evaluate();
			status.set(tr("new_project.last_answers"));
			return true;
		});
	command("create", "makes the project and opens it, as Chimera does: written nowhere until it is first saved, which "
		"asks where it goes; given a file, written there at once (and opened unless open is false)", {"path", "open"},
		[this](const Args &a, std::string &error) -> Value {
			std::string path;
			bool open = true;
			if (!a.empty() && !arg(a, 0, path, error)) return {};
			if (a.size() > 1 && !arg(a, 1, open, error)) return {};
			if (path.empty() && !open)
			{
				error = tr("new_project.must_open");
				return {};
			}
			if (making.get())
			{
				error = tr("new_project.making");
				return {};
			}
			if (!ready.get())
			{
				error = tr("new_project.not_yet", {problems.get()});
				return {};
			}
			if (compileAsked.get() && !compileReady.get())
			{
				error = tr("compile.first");
				return {};
			}
			const std::optional<int64_t> clock = parseClockText(clockStart.get());
			clockWrong.set(!clock);
			if (!clock)
			{
				error = tr("new_project.clock_format");
				return {};
			}
			/* everything the project is, taken now: making it runs on the
			 * machine's thread, since it reads and hashes every file */
			std::string platform;
			if (const Json *list = cfg_.find("machines"); list != nullptr && machine.get() >= 0 && machine.get() < static_cast<int64_t>(list->items.size()))
			{
				if (const Json *id = list->items[static_cast<size_t>(machine.get())].find("id")) platform = id->text;
			}
			else if (const Json *system = cfg_.find("systemId"))
				platform = system->text;
			std::vector<std::pair<std::string, Picked>> files;
			for (const Slot &s : slots_)
				if (auto it = files_.find(s.id); it != files_.end())
					for (const Picked &f : it->second) files.emplace_back(s.id, f);
			std::string name = title.get();
			if (name.empty() && !files.empty()) name = fs::path(files.front().second.name).stem().string();
			Json pins;
			pins.kind = Json::Kind::Array;
			for (const Need &n : needs_)
			{
				/* only what was given: an optional one left out pinned with no
				 * hash was a warning at every open, as Chimera pins none */
				if (n.path.empty() || n.sha1.empty()) continue;
				Json pin;
				setMember(pin, "id", Json::string(n.id));
				setMember(pin, "sha1", Json::string(n.sha1));
				pins.items.push_back(pin);
			}
			const std::string settings = json::write(form_.recorded()), firmware = json::write(pins);
			/* what the core compiled for this game, by name and hash, as Chimera's
			 * project records it: an open can then say whether it runs the same code */
			std::string coreCache;
			if (compileAsked.get() && !modules_.empty())
			{
				Json list;
				list.kind = Json::Kind::Array;
				for (const Module &m : modules_)
				{
					Json o;
					setMember(o, "name", Json::string(m.name));
					setMember(o, "sha1", Json::string(m.sha1));
					list.items.push_back(o);
				}
				coreCache = json::write(list);
			}
			const std::string coreName = core.get(), version = version_, sha = packageSha1_, slotsText = slotsText_, package = package_;
			const std::string input = pendingInput_;
			/* a file chosen by hand is remembered, so the project finds it
			 * when it opens - now, before the open looks */
			for (const Need &n : needs_)
				if (n.how == "chosen" || n.how == "your own") cores_.rememberFirmware(coreName, n.id, n.path);
			status.set(path.empty() ? tr("new_project.making") : tr("new_project.making_at", {path}));
			making.set(true);
			filesRefused.set(false);
			session_.onMachine([=, this](ce_session *) {
				ce_project *p = ce_project_new();
				ce_project_set_core(p, coreName.c_str(), version.c_str(), sha.c_str());
				if (!platform.empty()) ce_project_header_set(p, "Platform", platform.c_str());
				const char *err = nullptr;
				std::string why;
				bool forFiles = false;
				for (const auto &[slotId, f] : files)
					if (ce_project_file_add(p, f.name.c_str(), slotId.c_str(), f.path.c_str(), &err) != 0)
					{
						why = tr("session.named_reason", {f.name, err != nullptr ? err : tr("new_project.not_added")});
						forFiles = true;
						break;
					}
				if (why.empty() && ce_project_set_settings_text(p, settings.c_str(), &err) != 0) why = err != nullptr ? err : "settings";
				if (why.empty() && firmware != "[]" && ce_project_set_firmware_text(p, firmware.c_str(), &err) != 0) why = err != nullptr ? err : "firmware";
				if (why.empty() && !coreCache.empty() && ce_project_set_core_cache_text(p, coreCache.c_str(), &err) != 0)
					why = err != nullptr ? err : tr("new_project.modules_list");
				ce_project_set_title(p, name.c_str());
				ce_project_set_clock_start(p, *clock);
				/* an imported movie's inputs are the project's */
				if (!input.empty()) ce_project_set_log_text(p, input.data(), input.size());
				/* the engine's rule for the files, which the wizard only says:
				 * a file of a format its slot does not take is refused here */
				if (why.empty() && !slotsText.empty() && ce_project_validate(p, slotsText.data(), slotsText.size(), &err) != 0)
				{
					why = err != nullptr ? err : tr("new_project.files_do_not_fit");
					forFiles = true;
				}
				if (why.empty() && path.empty())
				{
					/* written nowhere: it opens as it is, and its first save
					 * asks where it goes */
					later([this] {
						status.set(tr("new_project.made_unsaved"));
						madeOrNot({}, false);
					});
					session_.openMadeProjectOnMachine(p, package);
					return;
				}
				if (why.empty() && ce_project_save(p, path.c_str(), &err) != 0) why = err != nullptr ? err : tr("session.not_written");
				ce_project_free(p);
				later([this, why, forFiles, path, package, open] {
					if (!why.empty())
					{
						status.set(tr("new_project.not_made", {why}));
						madeOrNot(why, forFiles);
						return;
					}
					status.set(tr("new_project.made", {path}));
					madeOrNot({}, false);
					std::string e;
					if (open) session_.run("openProject", {path, package}, e);
				});
			});
			return {};
		});

	listCores();
	coresWatch_ = cores_.scanned.connect([this] { listCores(); });
	kindWatch_ = kind.changed.connect([this] { listCores(); });
}

namespace {
/* a JSON value as a setting's value is given to the form */
Value valueOfJson(const Json &j)
{
	switch (j.kind)
	{
	case Json::Kind::Bool:
		return j.boolean;
	case Json::Kind::Number:
		if (j.text.find_first_of(".eE") == std::string::npos)
		{
			try
			{
				return static_cast<int64_t>(std::stoll(j.text));
			}
			catch (const std::exception &)
			{
				return j.text; /* past a long: the form reads it from its digits */
			}
		}
		return j.number();
	case Json::Kind::String:
		return j.text;
	default:
		return json::write(j);
	}
}
} // namespace

void NewProjectViewModel::completeForAgent(const AgentRequest &request, std::function<void(AgentOutcome)> done)
{
	agentWait_.disconnect();
	auto refused = [&done](std::string why) {
		AgentOutcome o;
		o.refusal = std::move(why);
		done(std::move(o));
	};
	/* the core, by the name it gives itself or its package's file name, in the cores folder as it is now */
	std::string looked;
	cores_.run("refresh", {}, looked);
	const std::vector<CoresViewModel::Package> all = cores_.packages();
	const auto named = std::find_if(all.begin(), all.end(), [&](const CoresViewModel::Package &p) {
		const fs::path file(p.path);
		return lower(p.name) == lower(request.core) || lower(file.filename().string()) == lower(request.core) ||
			lower(file.stem().string()) == lower(request.core);
	});
	std::string error;
	if (named == all.end() || !chooseCore(named->name, error, named->path))
		return refused(tr("agent.no_core", {request.core}));
	const std::string coreName = core.get();

	/* the machine the agent named: its id, its label, or a value of its setting */
	Json values = request.settings.kind == Json::Kind::Object ? request.settings : Json();
	values.kind = Json::Kind::Object;
	int64_t machineAt = -1;
	if (!request.system.empty())
	{
		const Json *list = cfg_.find("machines");
		if (list != nullptr && !list->items.empty() && !form_.machineSetting().empty())
		{
			std::string names;
			for (size_t i = 0; i < list->items.size() && machineAt < 0; i++)
			{
				const Json &m = list->items[i];
				std::vector<std::string> words;
				for (const char *k : {"id", "label"})
					if (const Json *w = m.find(k)) words.push_back(w->text);
				if (const Json *when = m.find("when"))
					for (const Json &w : when->items) words.push_back(w.text);
				for (const std::string &w : words)
					if (!w.empty() && lower(w) == lower(request.system)) machineAt = static_cast<int64_t>(i);
			}
			if (machineAt < 0)
			{
				for (const Json &m : list->items)
				{
					const Json *label = m.find("label");
					const Json *id = m.find("id");
					names += (names.empty() ? "" : ", ") + (label != nullptr ? label->text : id != nullptr ? id->text : std::string());
				}
				return refused(tr("agent.no_machine", {coreName, request.system, names}));
			}
		}
		else
		{
			bool known = false;
			for (const auto &[id, word] : CoresViewModel::systemPairs(cfg_))
				known = known || lower(id) == lower(request.system) || lower(word) == lower(request.system);
			if (!known) return refused(tr("agent.wrong_system", {coreName, CoresViewModel::systemsOf(cfg_), request.system}));
		}
	}

	/* what is asked for must exist before anything is put anywhere */
	std::error_code ec;
	std::vector<std::string> paths;
	for (const auto &[slotId, path] : request.files) paths.push_back(path);
	paths.insert(paths.end(), request.games.begin(), request.games.end());
	for (const auto &[id, path] : request.firmware) paths.push_back(path);
	for (const std::string &path : paths)
		if (!fs::is_regular_file(path, ec)) return refused(tr("agent.no_file", {path}));

	/* the machine, then the agent's settings as a reopened project's go in,
	 * which decide which slots the files can go in */
	if (machineAt >= 0 && !run("chooseMachine", {machineAt}, error)) return refused(error);
	{
		Json seeded = form_.values();
		for (const auto &[name, value] : values.members)
		{
			bool replaced = false;
			for (auto &[n, v] : seeded.members)
				if (n == name) v = value, replaced = true;
			if (!replaced) seeded.members.emplace_back(name, value);
		}
		form_.setValues(seeded);
	}
	if (!request.firmwareFolder.empty())
	{
		const std::vector<FirmwareFile> found = indexFirmware({request.firmwareFolder}, true, 10000);
		firmwareIndex_.insert(firmwareIndex_.end(), found.begin(), found.end());
	}
	evaluate();

	/* the files, each in its slot; a file with no slot named in the slot that takes its kind */
	std::vector<std::pair<std::string, std::string>> files = request.files;
	for (const std::string &game : request.games)
	{
		const std::string kind = kindOf(baseName(game));
		std::string into;
		for (const Slot &sl : slots_)
		{
			const std::vector<std::string> formats = formatsFor(sl);
			if (into.empty() && std::find(formats.begin(), formats.end(), kind) != formats.end()) into = sl.id;
		}
		if (into.empty() && !slots_.empty()) into = slots_.front().id;
		files.emplace_back(into, game);
	}
	for (const auto &[slotId, path] : files)
	{
		if (slot(slotId) == nullptr)
		{
			std::string ids;
			for (const Slot &sl : slots_) ids += (ids.empty() ? "" : ", ") + sl.id;
			return refused(tr("agent.no_slot", {coreName, slotId, ids}));
		}
		if (!run("addFile", {slotId, path}, error))
			return refused(!error.empty() ? error : tr("agent.not_taken", {path, slotId}));
	}
	if (!filesComplaint.get().empty()) return refused(filesComplaint.get());

	/* the core's suggestions for the game, as on arriving at the settings
	 * page: asked when the game came in, waited for when not yet answered */
	if (suggestion.get() == tr("new_project.looking_up"))
	{
		auto waited = std::make_shared<bool>(false);
		agentWait_ = suggestion.changed.connect([this, request, done, waited] {
			if (*waited) return;
			*waited = true;
			later([this, request, done] {
				agentWait_.disconnect();
				finishForAgent(request, done);
			});
		});
		return;
	}
	finishForAgent(request, done);
}

void NewProjectViewModel::finishForAgent(const AgentRequest &request, const std::function<void(AgentOutcome)> &done)
{
	auto refused = [&done](std::string why) {
		AgentOutcome o;
		o.refusal = std::move(why);
		done(std::move(o));
	};
	const std::string coreName = core.get();
	std::string error;
	/* the agent's own settings over the suggestions */
	if (request.settings.kind == Json::Kind::Object)
		for (const auto &[name, value] : request.settings.members)
		{
			const bool declared = std::any_of(form_.declarations().begin(), form_.declarations().end(), [&](const Json &d) {
				const Json *n = d.find("name");
				return n != nullptr && n->text == name;
			});
			if (!declared) return refused(tr("agent.no_setting", {coreName, name}));
			if (!form_.set(name, valueOfJson(value), error)) return refused(error);
		}
	evaluate();

	/* firmware the agent picked, as if picked on the firmware page */
	for (const auto &[id, path] : request.firmware)
	{
		const bool needed = std::any_of(needs_.begin(), needs_.end(), [&](const Need &n) { return n.id == id; });
		if (needed && !run("chooseFirmware", {id, path}, error)) return refused(error);
	}
	/* what is still missing is said, not asked for */
	std::vector<const Need *> missing;
	for (const Need &n : needs_)
		if (n.path.empty() && n.required) missing.push_back(&n);
	if (!missing.empty())
	{
		AgentOutcome o;
		std::string names;
		o.missing.kind = Json::Kind::Array;
		const Json decls = firmwareDecls();
		for (const Need *n : missing)
		{
			names += (names.empty() ? "" : ", ") + n->display;
			Json item;
			setMember(item, "id", Json::string(n->id));
			setMember(item, "name", Json::string(n->display));
			Json accepted;
			accepted.kind = Json::Kind::Array;
			if (n->declIndex < decls.items.size())
			{
				const Json &d = decls.items[n->declIndex];
				Json file;
				const Json *name = d.find("name");
				const Json *size = d.find("size");
				const Json *sha = d.find("sha1");
				setMember(file, "file", Json::string(name != nullptr ? name->text : ""));
				setMember(file, "size", size != nullptr ? *size : Json());
				setMember(file, "sha1", Json::string(sha != nullptr ? sha->text : ""));
				accepted.items.push_back(file);
			}
			setMember(item, "accepted", accepted);
			o.missing.items.push_back(item);
		}
		o.searched.kind = Json::Kind::Array;
		for (const std::string &folder : cores_.firmwareFolders()) o.searched.items.push_back(Json::string(folder));
		if (!request.firmwareFolder.empty()) o.searched.items.push_back(Json::string(request.firmwareFolder));
		o.refusal = tr("agent.firmware_not_found", {names});
		done(std::move(o));
		return;
	}
	if (compileAsked.get() && !compileReady.get())
		return refused(tr("agent.compile_first", {coreName}));
	if (!ready.get()) return refused(problems.get());
	/* the firmware found is remembered, as Chimera's wizard remembers it: one
	 * in the agent's own folder is then found when the project opens */
	for (const Need &n : needs_)
		if (!n.path.empty() && n.how != "remembered") cores_.rememberFirmware(coreName, n.id, n.path);
	/* told once the project is made, or why it was not */
	makeTold_ = [done](const std::string &why) {
		AgentOutcome o;
		o.made = why.empty();
		o.refusal = why;
		done(std::move(o));
	};
	if (!run("create", {}, error))
	{
		makeTold_ = nullptr;
		return refused(error);
	}
}

void NewProjectViewModel::madeOrNot(const std::string &why, bool forFiles)
{
	making.set(false);
	filesRefused.set(forFiles);
	if (why.empty()) made.set(made.get() + 1);
	if (auto told = std::exchange(makeTold_, nullptr)) told(why);
}

void NewProjectViewModel::listCores()
{
	std::string names;
	for (const auto &[name, path] : cores_.installed()) names += (names.empty() ? "" : "\n") + name;
	coreNames.set(names);
	/* a kind with nothing installed lists the other, as Chimera's does */
	const std::vector<CoresViewModel::Package> all = cores_.packages();
	const bool anyGame = std::any_of(all.begin(), all.end(), [](const auto &p) { return p.gameCore; });
	const bool anyEmulator = std::any_of(all.begin(), all.end(), [](const auto &p) { return !p.gameCore; });
	const bool games = (kind.get() == "games" && anyGame) || !anyEmulator;
	shown_.clear();
	std::string lines;
	int64_t at = -1;
	for (const auto &p : all)
	{
		if (p.gameCore != games) continue;
		if (p.path == package_) at = static_cast<int64_t>(shown_.size());
		lines += (lines.empty() ? "" : "\n") + p.line;
		shown_.push_back(p);
	}
	coreLines.set(lines);
	coreLine.set(at);
	/* the package chosen gone from the folder and another of its core there
	 * (a newer one put in its place while the program runs, issue 423): the
	 * project starts over with that one */
	std::error_code ec;
	if (at < 0 && !package_.empty() && !core.get().empty() && !fs::is_regular_file(package_, ec))
		for (const auto &p : shown_)
			if (p.name == core.get())
			{
				std::string error;
				chooseCore(p.name, error, p.path);
				break;
			}
}

NewProjectViewModel::~NewProjectViewModel()
{
	if (compileJob_)
	{
		compileJob_->stop = true;
		while (!compileJob_->ended) std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
}

std::string NewProjectViewModel::ModuleRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "sha1", "present"};
	return column >= 0 && column < 3 ? names[column] : "";
}

Value NewProjectViewModel::ModuleRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Module &m = vm_.modules_[static_cast<size_t>(row)];
	if (column == 0) return m.name;
	if (column == 1) return m.sha1;
	if (column == 2) return m.present;
	return {};
}

std::string NewProjectViewModel::compileGame() const
{
	for (const Slot &sl : slots_)
		if (const auto it = files_.find(sl.id); it != files_.end() && !it->second.empty()) return it->second.front().path;
	return {};
}

std::string NewProjectViewModel::compileDirOf(const std::string &game) const
{
	if (game.empty() || compileRoot.get().empty()) return {};
	char sha[41] = {};
	if (ce_sha1_file(game.c_str(), sha, nullptr) != 1) return {};
	return (fs::path(compileRoot.get()) / sha).string();
}

void NewProjectViewModel::refreshCompile()
{
	if (compiling.get()) return;
	const std::string game = compileAsked.get() ? compileGame() : std::string();
	if (game == compileSeen_ && !game.empty()) return;
	compileSeen_ = game;
	modules_.clear();
	bool ready = false;
	/* code another build of the core compiled is not this one's (compiled_code.hpp) */
	std::string otherBuild;
	if (!game.empty())
	{
		/* the manifest a compile left, each module looked at */
		const std::string dir = compileDirOf(game);
		if (const std::optional<compiled::Build> build = compiled::buildOf(package_); build && !dir.empty() && !compiled::belongsTo(dir, *build))
		{
			const std::optional<compiled::Build> owner = compiled::ownerOf(dir);
			otherBuild = owner ? owner->core + " " + owner->version : tr("compile.unnamed_build");
		}
		std::ifstream in(fs::path(dir) / "manifest.json", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		json::Json m;
		std::string err;
		if (!dir.empty() && !text.empty() && json::parse(text, m, err))
			if (const json::Json *files = m.find("files"); files != nullptr && files->kind == json::Json::Kind::Array)
				for (const json::Json &f : files->items)
				{
					Module mod;
					if (const json::Json *v = f.find("name")) mod.name = v->text;
					if (const json::Json *v = f.find("sha1")) mod.sha1 = upper(v->text);
					char sha[41] = {};
					mod.present = ce_sha1_file((fs::path(dir) / mod.name).string().c_str(), sha, nullptr) == 1 && upper(sha) == mod.sha1;
					modules_.push_back(std::move(mod));
				}
		ready = !modules_.empty() && std::all_of(modules_.begin(), modules_.end(), [](const Module &mod) { return mod.present; });
		if (!otherBuild.empty())
		{
			modules_.clear();
			ready = false;
		}
	}
	moduleRows_.reset.emit();
	compileReady.set(ready);
	const size_t there = static_cast<size_t>(std::count_if(modules_.begin(), modules_.end(), [](const Module &mod) { return mod.present; }));
	compileStatus.set(game.empty() ? std::string()
		: !otherBuild.empty() ? tr("compile.other_build", {otherBuild})
		: ready ? tr("compile.all_compiled", static_cast<int64_t>(modules_.size()))
		: modules_.empty() ? tr("compile.not_compiled")
						   : tr("compile.some_there", {std::to_string(there), std::to_string(modules_.size())}));
}

std::string NewProjectViewModel::clockText(int64_t unixSeconds)
{
	/* the civil date of a day count (Howard Hinnant's days_from_civil, backwards) */
	const int64_t days = unixSeconds / 86400, secs = unixSeconds % 86400;
	const int64_t z = days + 719468, era = z / 146097, doe = z - era * 146097;
	const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
	const int64_t d = doy - (153 * mp + 2) / 5 + 1, m = mp < 10 ? mp + 3 : mp - 9, y = yoe + era * 400 + (m <= 2);
	/* room for any six numbers, which the compiler cannot tell are small */
	char out[128];
	std::snprintf(out, sizeof out, "%04lld-%02lld-%02lld %02lld:%02lld:%02lld", static_cast<long long>(y), static_cast<long long>(m),
		static_cast<long long>(d), static_cast<long long>(secs / 3600), static_cast<long long>(secs / 60 % 60), static_cast<long long>(secs % 60));
	return out;
}

std::optional<int64_t> NewProjectViewModel::parseClockText(const std::string &text)
{
	const size_t a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
	if (a == std::string::npos) return std::nullopt;
	const std::string t = text.substr(a, b - a + 1);
	/* exactly yyyy-MM-dd HH:mm:ss */
	if (t.size() != 19 || t[4] != '-' || t[7] != '-' || t[10] != ' ' || t[13] != ':' || t[16] != ':') return std::nullopt;
	for (size_t i : {0, 1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 15, 17, 18})
		if (t[i] < '0' || t[i] > '9') return std::nullopt;
	auto num = [&t](size_t at, size_t n) { return static_cast<int64_t>(std::stoll(t.substr(at, n))); };
	const int64_t y = num(0, 4), m = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), s = num(17, 2);
	const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
	static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (m < 1 || m > 12 || d < 1 || d > kDays[m - 1] + (m == 2 && leap) || h > 23 || mi > 59 || s > 59) return std::nullopt;
	/* days_from_civil */
	const int64_t yy = y - (m <= 2), era = yy / 400, yoe = yy - era * 400;
	const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	const int64_t seconds = (era * 146097 + doe - 719468) * 86400 + h * 3600 + mi * 60 + s;
	/* from 1970 to the first second of 2200, as the project keeps it */
	if (seconds < 0 || seconds > 7258118400) return std::nullopt;
	return seconds;
}

bool NewProjectViewModel::chooseCore(const std::string &name, std::string &error, const std::string &preferred)
{
	std::string package;
	for (const auto &[n, p] : cores_.installed())
		if (n == name) package = p;
	std::error_code ec;
	if (!package.empty() && !preferred.empty() && preferred != package && fs::is_regular_file(preferred, ec))
	{
		const char *perr = nullptr;
		if (ce_package *p = ce_package_open(preferred.c_str(), &perr))
		{
			Json c;
			std::string jerr;
			if (json::parse(entryText(p, "waterbox.config"), c, jerr))
				if (const Json *n = c.find("coreName"); n != nullptr && n->text == name) package = preferred;
			ce_package_free(p);
		}
	}
	if (package.empty())
	{
		error = tr("new_project.core_not_installed", {name});
		return false;
	}
	const char *perr = nullptr;
	ce_package *p = ce_package_open(package.c_str(), &perr);
	if (p == nullptr)
	{
		error = perr != nullptr ? tr("new_project.package_unreadable_why", {package, perr}) : tr("new_project.package_unreadable", {package});
		return false;
	}
	const std::string config = entryText(p, "waterbox.config");
	slotsText_ = entryText(p, "file_slots.json");
	packageSha1_ = ce_package_sha1(p) != nullptr ? ce_package_sha1(p) : "";
	ce_package_free(p);
	std::string jerr;
	cfg_ = Json();
	if (!json::parse(config, cfg_, jerr))
	{
		error = tr("new_project.config_unreadable", {package, jerr});
		return false;
	}
	package_ = package;
	version_ = cfg_.find("version") != nullptr ? cfg_.find("version")->text : "";
	systems.set(CoresViewModel::systemsOf(cfg_));
	/* the list shows this package's kind (a project's answers or a file can
	 * name a core of either), and its line */
	{
		const Json *k = cfg_.find("kind");
		const bool game = k != nullptr && lower(k->text) == "game";
		if (kind.get() != (game ? "games" : "emulators")) kind.set(game ? "games" : "emulators");
		else listCores();
	}

	/* the slots it declares; a package without the file is one game file of
	 * the kinds it says it takes */
	slots_.clear();
	atLeastOneOf_.clear();
	Json decl;
	if (!slotsText_.empty() && json::parse(slotsText_, decl, jerr))
	{
		if (const Json *list = decl.find("slots"))
			for (const Json &d : list->items)
			{
				Slot s;
				auto text = [&](const char *k) { const Json *j = d.find(k); return j != nullptr ? j->text : std::string(); };
				s.id = text("id");
				s.title = text("title");
				s.help = text("help");
				s.namePattern = text("namePattern");
				s.nameHelp = text("nameHelp");
				if (const Json *m = d.find("min")) s.min = static_cast<int64_t>(m->number());
				if (const Json *m = d.find("max")) s.max = static_cast<int64_t>(m->number());
				if (const Json *f = d.find("formats"))
					for (const Json &k : f->items) s.formats.push_back(lower(k.text));
				if (const Json *when = d.find("exposedWhen")) s.settingGatedOnly = readsOnlySettings(*when);
				slots_.push_back(s);
			}
		if (const Json *groups = decl.find("atLeastOneOf"))
			for (const Json &g : groups->items)
			{
				std::vector<std::string> ids;
				for (const Json &id : g.items) ids.push_back(id.text);
				atLeastOneOf_.push_back(ids);
			}
	}
	else
	{
		slotsText_.clear();
		Slot s;
		s.id = "rom";
		s.title = tr("new_project.slot_game");
		s.min = 1;
		s.max = 1;
		if (const Json *ext = cfg_.find("extensions"))
			for (const auto &[e, system] : ext->members) s.formats.push_back(lower(e.size() > 1 && e[0] == '.' ? e.substr(1) : e));
		slots_.push_back(s);
	}

	/* whether it suggests settings for a game */
	suggests_ = cfg_.find("suggestSettings") != nullptr && cfg_.find("suggestSettings")->boolean;
	suggestedFor_.clear();
	suggestedKeys_.clear();
	suggestion.set("");
	/* another core: what the last one was asked is no longer this form's */
	++suggestTicket_;
	++importTicket_;
	/* the movie import it declares, if any */
	import_ = Json();
	importPicked_.clear();
	importOptions_.clear();
	pendingInput_.clear();
	imported.set(false);
	importNotes.set("");
	importMovie.set("");
	importMenu.set("");
	importMovieLabel.set("");
	importMovieFilter.set("");
	if (const Json *mi = cfg_.find("movieImport"); mi != nullptr && mi->kind == Json::Kind::Object)
	{
		import_ = *mi;
		if (const Json *m = mi->find("menu")) importMenu.set(m->text);
		if (const Json *mv = mi->find("movie"))
		{
			if (const Json *l = mv->find("label")) importMovieLabel.set(l->text);
			std::string filter;
			if (const Json *ex = mv->find("extensions"))
				for (const Json &e : ex->items) filter += (filter.empty() ? "" : " ") + std::string("*.") + lower(e.text[0] == '.' ? e.text.substr(1) : e.text);
			importMovieFilter.set(filter.empty() ? "*" : filter);
		}
		if (importMenu.get().empty()) importMenu.set(tr("import.menu"));
	}
	importFileRows_.reset.emit();
	importOptionRows_.reset.emit();

	/* its settings at their defaults, and its machines */
	std::vector<Json> settings;
	if (const Json *list = cfg_.find("settings")) settings = list->items;
	const std::string machineSetting = cfg_.find("machineSetting") != nullptr ? cfg_.find("machineSetting")->text : "";
	form_.declare(std::move(settings), machineSetting);
	{
		std::vector<Json> machineDecls;
		if (const Json *m = cfg_.find("machines")) machineDecls = m->items;
		form_.declareMachines(std::move(machineDecls));
	}
	{
		std::vector<Json> presets;
		if (const Json *p = cfg_.find("presets")) presets = p->items;
		form_.declarePresets(std::move(presets));
	}
	Json values;
	values.kind = Json::Kind::Object;
	form_.setValues(values);
	std::string names;
	int64_t chosen = -1;
	if (const Json *list = cfg_.find("machines"); list != nullptr && !machineSetting.empty())
	{
		std::string preset;
		for (const Json &d : form_.declarations())
			if (const Json *n = d.find("name"); n != nullptr && n->text == machineSetting)
				if (const Json *v = d.find("default")) preset = v->text;
		for (size_t i = 0; i < list->items.size(); i++)
		{
			const Json &m = list->items[i];
			const Json *label = m.find("label");
			const Json *id = m.find("id");
			names += (i ? "\n" : "") + (label != nullptr ? label->text : id != nullptr ? id->text : std::to_string(i));
			if (const Json *when = m.find("when"); chosen < 0 && when != nullptr)
				for (const Json &w : when->items) chosen = w.text == preset ? static_cast<int64_t>(i) : chosen;
		}
		if (chosen < 0 && !list->items.empty()) chosen = 0;
	}
	machines.set(names);
	machine.set(chosen);
	files_.clear();
	chosenFirmware_.clear();
	gameSettings_.clear();
	gameFirmware_ = Json();
	keptFor_.clear();
	seededGame_.clear();
	ownNames_.clear();
	ownFor_.clear();
	firmwareIndex_ = indexFirmware(cores_.firmwareFolders(), true, 10000);
	rememberedSha_.clear();
	core.set(name);
	title.set("");
	status.set(tr("new_project.a_new", {name}));
	if (chosen >= 0)
	{
		std::string e;
		run("chooseMachine", {chosen}, e);
	}
	else
		evaluate();
	return true;
}

void NewProjectViewModel::suggestFor(const std::string &game)
{
	/* what the last game's suggestion set goes back to the default */
	if (!suggestedKeys_.empty())
	{
		Json values = form_.values();
		for (const std::string &k : suggestedKeys_)
			for (auto it = values.members.begin(); it != values.members.end(); ++it)
				if (it->first == k)
				{
					values.members.erase(it);
					break;
				}
		form_.setValues(values);
		suggestedKeys_.clear();
	}
	/* and what the last game had of its own goes: its declarations now,
	 * their values when another game comes (the same game back keeps them,
	 * and a seeded project's game has them as its own) */
	if (!gameSettings_.empty() || !gameFirmware_.items.empty())
	{
		ownNames_.clear();
		for (const Json &d : gameSettings_)
			if (const Json *n = d.find("name")) ownNames_.push_back(n->text);
		ownFor_ = suggestedFor_;
		gameSettings_.clear();
		gameFirmware_ = Json();
		declareSettings();
	}
	if (!game.empty() && game != ownFor_ && game != keptFor_ && !ownNames_.empty())
	{
		Json values = form_.values();
		for (const std::string &k : ownNames_)
			for (auto it = values.members.begin(); it != values.members.end(); ++it)
				if (it->first == k)
				{
					values.members.erase(it);
					break;
				}
		form_.setValues(values);
		ownNames_.clear();
	}
	suggestedFor_ = game;
	suggestion.set(game.empty() ? "" : tr("new_project.looking_up"));
	if (game.empty()) return;
	auto ask = [this, game] {
		/* an answer for a game no longer chosen, or to a question asked
		 * before another (of another core, or of this game again), is no answer */
		const uint64_t ticket = ++suggestTicket_;
		auto done = [this, game, ticket](std::string answer, std::string why) {
			if (game != suggestedFor_ || ticket != suggestTicket_) return;
			applySuggestion(answer, why);
		};
		/* what a run would have: every file picked, in its slot (a cue brings
		 * its tracks into the project), and the settings so far */
		Mounts files;
		for (const Slot &sl : slots_)
			if (auto it = files_.find(sl.id); it != files_.end())
				for (const Picked &f : it->second) files.emplace_back(f.name, f.path);
		const std::string slots = slotMap(), settings = json::write(form_.recorded());
		if (suggestRunner_)
		{
			suggestRunner_(package_, game, files, slots, settings, done);
			return;
		}
		/* here, with no machine open */
		std::string why;
		const std::string answer = askForSuggestion(package_, game, files, slots, settings, why);
		done(answer, why);
	};
	/* another game than a seeded project's: what the core would suggest for
	 * that project's game comes out first, where it still sits */
	if (!seededGame_.empty() && game != seededGame_)
	{
		const std::string seeded = seededGame_;
		seededGame_.clear();
		if (package_ == seededPackage_)
		{
			auto then = [this, game, ask](std::string answer, std::string) {
				if (game != suggestedFor_) return;
				takeBackSeeded(answer);
				ask();
			};
			if (suggestRunner_)
				suggestRunner_(package_, seeded, {}, "{}", "{}", then);
			else
			{
				std::string why;
				then(askForSuggestion(package_, seeded, {}, "{}", "{}", why), why);
			}
			return;
		}
	}
	ask();
}

void NewProjectViewModel::takeBackSeeded(const std::string &text)
{
	Json answer;
	std::string error;
	if (text.empty() || !json::parse(text, answer, error)) return;
	const Json *suggested = answer.find("values");
	if (suggested == nullptr) return;
	auto same = [](const Json &a, const Json &b) {
		if (a.kind == Json::Kind::Number && b.kind == Json::Kind::Number) return a.number() == b.number();
		if (a.kind == Json::Kind::Bool && b.kind == Json::Kind::Bool) return a.boolean == b.boolean;
		return a.kind == b.kind && a.text == b.text;
	};
	Json values = form_.values();
	const Json *package = cfg_.find("settings");
	for (const auto &[name, value] : suggested->members)
	{
		/* never the machine; the package's settings only */
		if (name == form_.machineSetting() || package == nullptr) continue;
		const bool declared = std::any_of(package->items.begin(), package->items.end(),
			[&](const Json &d) { const Json *n = d.find("name"); return n != nullptr && n->text == name; });
		if (!declared) continue;
		/* a value moved away from the suggestion is the person's, and stays */
		for (auto it = values.members.begin(); it != values.members.end(); ++it)
			if (it->first == name)
			{
				if (same(it->second, value)) values.members.erase(it);
				break;
			}
	}
	form_.setValues(values);
	form_.evaluate();
}

void NewProjectViewModel::refreshRenderers()
{
	/* named for what they are: where the drawing happens, and how */
	auto spelled = [](const std::string &v) -> std::string {
		if (v == "software" || v == "default") return "Software (Native)";
		if (v == "opengl") return "Software (OpenGL)";
		if (v == "opengl-hw") return "Hardware (OpenGL)";
		if (v == "vulkan-hw") return "Hardware (Vulkan)";
		return v;
	};
	const Json *decl = core.get().empty() || !form_.applies("renderer") ? nullptr : form_.declaration("renderer");
	const Json *options = decl != nullptr ? decl->find("options") : nullptr;
	if (core.get().empty())
	{
		renderers.set("");
		renderer.set(-1);
		rendererCaveat.set("");
		return;
	}
	if (options == nullptr || options->items.empty())
	{
		/* nothing declared: the core draws one way and does not name it */
		renderers.set(spelled("default"));
		renderer.set(0);
		rendererCaveat.set("");
		return;
	}
	std::string lines;
	for (const Json &o : options->items) lines += (lines.empty() ? "" : "\n") + spelled(o.text);
	renderers.set(lines);
	/* the one chosen, if this machine has it, else the machine's default,
	 * else the first - written into the settings, where it is a setting
	 * like any other */
	auto at = [&](const std::string &v) {
		for (size_t i = 0; i < options->items.size(); i++)
			if (options->items[i].text == v) return static_cast<int64_t>(i);
		return int64_t{-1};
	};
	int64_t chosen = -1;
	if (const Json *v = form_.values().find("renderer")) chosen = at(v->text);
	if (chosen < 0)
		if (const Json *d = decl->find("default")) chosen = at(d->text);
	if (chosen < 0) chosen = 0;
	const std::string value = options->items[static_cast<size_t>(chosen)].text;
	if (const Json *v = form_.values().find("renderer"); v == nullptr || v->text != value)
	{
		Json values = form_.values();
		setMember(values, "renderer", Json::string(value));
		form_.setValues(values);
		form_.evaluate();
	}
	renderer.set(chosen);
	/* said where the choice is made, and only for a hardware one, as Chimera says it */
	rendererCaveat.set(value.size() > 3 && value.compare(value.size() - 3, 3, "-hw") == 0
			? tr("new_project.hardware_renderer")
			: "");
}

void NewProjectViewModel::declareSettings()
{
	std::vector<Json> decl;
	if (const Json *list = cfg_.find("settings")) decl = list->items;
	decl.insert(decl.end(), gameSettings_.begin(), gameSettings_.end());
	form_.declare(std::move(decl), form_.machineSetting());
	form_.evaluate();
}

Json NewProjectViewModel::firmwareDecls() const
{
	Json all;
	all.kind = Json::Kind::Array;
	if (const Json *list = cfg_.find("firmware")) all.items = list->items;
	all.items.insert(all.items.end(), gameFirmware_.items.begin(), gameFirmware_.items.end());
	return all;
}

void NewProjectViewModel::applySuggestion(const std::string &text, const std::string &why)
{
	Json answer;
	std::string jerr;
	if (text.empty() || !json::parse(text, answer, jerr) || answer.kind != Json::Kind::Object)
	{
		suggestion.set(tr("new_project.no_suggestion", {!why.empty() ? why : tr(text.empty() ? "new_project.core_gave_none" : "new_project.answer_unreadable")}));
		return;
	}
	/* what the game has of its own: settings after the package's, and, for
	 * a package that says its games have them, firmware after the package's */
	gameSettings_ = gameSettingsOf(answer, cfg_.find("settings"));
	const Json *gameFirmware = cfg_.find("gameFirmware");
	gameFirmware_ = gameFirmware != nullptr && gameFirmware->boolean ? gameFirmwareOf(answer, cfg_.find("firmware")) : Json();
	declareSettings();
	std::string applied;
	const bool kept = !keptFor_.empty() && keptFor_ == suggestedFor_;
	if (const Json *values = answer.find("values"); values != nullptr && !kept)
	{
		form_.evaluate();
		for (const auto &[name, value] : values->members)
		{
			/* never the machine: that is the person's choice; and the
			 * package's settings only */
			if (name == form_.machineSetting()) continue;
			const bool packages = std::any_of(form_.declarations().begin(), form_.declarations().end() - static_cast<std::ptrdiff_t>(gameSettings_.size()),
				[&](const Json &d) { const Json *n = d.find("name"); return n != nullptr && n->text == name; });
			if (!packages) continue;
			Value v;
			switch (value.kind)
			{
			case Json::Kind::Bool: v = value.boolean; break;
			case Json::Kind::Number:
				v = std::floor(value.number()) == value.number() ? Value(static_cast<int64_t>(value.number())) : Value(value.number());
				break;
			default: v = value.text; break;
			}
			std::string e;
			if (!form_.set(name, v, e)) continue;
			suggestedKeys_.push_back(name);
			const Json *d = form_.declared(name);
			const Json *label = d != nullptr ? d->find("display") : nullptr;
			applied += (applied.empty() ? "" : ", ") + (label != nullptr ? label->text : name);
		}
	}
	const Json *note = answer.find("note");
	std::string out = note != nullptr ? note->text : std::string();
	if (!applied.empty()) out += (out.empty() ? "" : "\n") + tr("new_project.applied_below", {applied});
	else if (kept) out += (out.empty() ? "" : "\n") + tr("new_project.own_settings_kept");
	suggestion.set(out);
	evaluate();
}

const Json *NewProjectViewModel::importInput(const std::string &option) const
{
	if (const Json *files = import_.find("files"))
		for (const Json &f : files->items)
			if (const Json *o = f.find("option"); o != nullptr && o->text == option) return &f;
	return nullptr;
}

void NewProjectViewModel::applyImport(const std::string &text)
{
	Json answer;
	std::string why;
	if (!json::parse(text, answer, why) || answer.kind != Json::Kind::Object)
	{
		status.set(tr("import.answer_unreadable", {why}));
		importFailure.set(tr("import.no_answer"));
		return;
	}
	if (const Json *refused = answer.find("error"))
	{
		status.set(tr("import.not_imported", {refused->text}));
		importFailure.set(refused->text);
		return;
	}
	std::vector<std::string> notes;
	/* the settings it dictates; the machine's, by choosing the machine */
	if (const Json *settings = answer.find("settings"))
		for (const auto &[name, value] : settings->members)
		{
			if (name == form_.machineSetting())
			{
				if (const Json *list = cfg_.find("machines"))
					for (size_t i = 0; i < list->items.size(); i++)
						if (const Json *when = list->items[i].find("when"))
							for (const Json &w : when->items)
								if (w.text == value.text)
								{
									std::string e;
									run("chooseMachine", {static_cast<int64_t>(i)}, e);
								}
				continue;
			}
			Value v;
			switch (value.kind)
			{
			case Json::Kind::Bool: v = value.boolean; break;
			case Json::Kind::Number:
				v = std::floor(value.number()) == value.number() ? Value(static_cast<int64_t>(value.number())) : Value(value.number());
				break;
			default: v = value.text; break;
			}
			form_.evaluate();
			std::string e;
			if (!form_.set(name, v, e)) notes.push_back(tr("import.setting_not_taken", {name, e}));
		}
	/* the files, in the answer's order: the ones picked for the import */
	files_.clear();
	seeding_ = true;
	if (const Json *files = answer.find("files"))
		for (const Json &f : files->items)
		{
			const Json *name = f.find("name"), *slot = f.find("slot");
			if (name == nullptr) continue;
			std::string path, declared;
			for (const auto &[option, picked] : importPicked_)
				for (const std::string &p : picked)
					if (baseName(p) == name->text)
					{
						path = p;
						/* the slot the input it was picked in declares, for an answer that names none (Chimera's) */
						if (const Json *inputs = import_.find("files"))
							for (const Json &input : inputs->items)
								if (const Json *o = input.find("option"); o != nullptr && o->text == option)
									if (const Json *sl = input.find("slot")) declared = sl->text;
					}
			if (path.empty())
			{
				notes.push_back(tr("import.not_among_files", {name->text}));
				continue;
			}
			std::string e;
			const std::string into = slot != nullptr && !slot->text.empty() ? slot->text : !declared.empty() ? declared : std::string("rom");
			if (!run("addFile", {into, path}, e)) notes.push_back(name->text + ": " + e);
		}
	/* the firmware: a file picked for a firmware input is the one the answer
	 * names, by its hash - or, with one such file and one firmware named, that
	 * file whatever its hash, as Chimera takes it (a game core's firmware may
	 * be any release) */
	std::vector<std::string> firmwareFiles;
	if (const Json *files = import_.find("files"))
		for (const Json &input : files->items)
		{
			const Json *isFirmware = input.find("firmware"), *opt = input.find("option");
			if (isFirmware == nullptr || !isFirmware->boolean || opt == nullptr) continue;
			if (const auto it = importPicked_.find(opt->text); it != importPicked_.end())
				firmwareFiles.insert(firmwareFiles.end(), it->second.begin(), it->second.end());
		}
	if (const Json *firmware = answer.find("firmware"); firmware != nullptr && firmware->items.size() == 1 && firmwareFiles.size() == 1)
	{
		if (const Json *id = firmware->items.front().find("id"))
		{
			std::string e;
			run("chooseFirmware", {id->text, firmwareFiles.front()}, e);
		}
	}
	else if (const Json *firmware = answer.find("firmware"))
		for (const Json &need : firmware->items)
		{
			const Json *id = need.find("id"), *sha = need.find("sha1");
			if (id == nullptr) continue;
			if (const Json *files = import_.find("files"))
				for (const Json &input : files->items)
				{
					const Json *isFirmware = input.find("firmware"), *opt = input.find("option");
					if (isFirmware == nullptr || !isFirmware->boolean || opt == nullptr) continue;
					const auto it = importPicked_.find(opt->text);
					if (it == importPicked_.end()) continue;
					for (const std::string &p : it->second)
					{
						char got[41];
						uint64_t len = 0;
						if (ce_sha1_file(p.c_str(), got, &len) == 1 && (sha == nullptr || lower(got) == lower(sha->text)))
						{
							std::string e;
							run("chooseFirmware", {id->text, p}, e);
						}
					}
				}
		}
	seeding_ = false;
	if (const Json *n = answer.find("notes"))
		for (const Json &line : n->items) notes.push_back(line.text);
	pendingInput_ = answer.find("input") != nullptr ? answer.find("input")->text : std::string();
	imported.set(!pendingInput_.empty());
	std::string lines;
	for (const std::string &n : notes) lines += (lines.empty() ? "" : "\n") + n;
	importNotes.set(lines);
	if (title.get().empty()) title.set(fs::path(importMovie.get()).stem().string());
	evaluate();
	const Json *frames = answer.find("frames");
	status.set(frames != nullptr ? tr("import.imported_frames", {baseName(importMovie.get()), frames->text}) : tr("import.imported", {baseName(importMovie.get())}));
	/* what Chimera says of it when there are notes */
	std::string summary;
	if (!notes.empty())
	{
		const Json *format = answer.find("format");
		summary = tr("import.summary", {core.get(), format != nullptr ? format->text : std::string(), frames != nullptr ? frames->text : std::string("0")}) + "\n";
		for (const std::string &n : notes) summary += "\n- " + n + "\n";
		while (!summary.empty() && summary.back() == '\n') summary.pop_back();
	}
	importSummary.set(summary);
	importFailure.set("");
	importsTaken.set(importsTaken.get() + 1);
}

std::string NewProjectViewModel::slotMap() const
{
	Json map;
	map.kind = Json::Kind::Object;
	for (const Slot &s : slots_)
		if (auto it = files_.find(s.id); it != files_.end() && !it->second.empty())
		{
			Json names;
			names.kind = Json::Kind::Array;
			for (const Picked &f : it->second) names.items.push_back(Json::string(f.name));
			map.members.emplace_back(s.id, names);
		}
	return json::write(map);
}

std::vector<std::string> NewProjectViewModel::formatsFor(const Slot &s) const
{
	const Json *machines = cfg_.find("machines");
	if (machines == nullptr || s.formats.empty() || machine.get() < 0 || machine.get() >= static_cast<int64_t>(machines->items.size()))
		return s.formats;
	auto kinds = [](const Json &m) {
		std::vector<std::string> out;
		if (const Json *e = m.find("extensions"))
			for (const auto &[ext, system] : e->members) out.push_back(lower(ext.size() > 1 && ext[0] == '.' ? ext.substr(1) : ext));
		return out;
	};
	const std::vector<std::string> mine = kinds(machines->items[static_cast<size_t>(machine.get())]);
	auto in = [](const std::vector<std::string> &list, const std::string &k) { return std::find(list.begin(), list.end(), k) != list.end(); };
	if (mine.empty() || std::none_of(s.formats.begin(), s.formats.end(), [&](const std::string &f) { return in(mine, f); }))
		return s.formats;
	std::vector<std::string> anyMachine;
	for (const Json &m : machines->items)
		for (const std::string &k : kinds(m)) anyMachine.push_back(k);
	std::vector<std::string> claimed;
	for (const std::string &f : s.formats)
		if (in(mine, f) || !in(anyMachine, f)) claimed.push_back(f);
	return claimed.empty() ? s.formats : claimed;
}

const NewProjectViewModel::Slot *NewProjectViewModel::slot(const std::string &id) const
{
	for (const Slot &s : slots_)
		if (s.id == id) return &s;
	return nullptr;
}

void NewProjectViewModel::evaluate()
{
	const std::string map = slotMap();
	form_.setSlots(map);
	form_.evaluate();
	refreshRenderers();

	/* the core's suggestion for the game, once a game: first, since what the
	 * game has of its own is part of what applies */
	if (suggests_)
	{
		std::string game;
		for (const Slot &s : slots_)
			if (auto it = files_.find(s.id); game.empty() && it != files_.end() && !it->second.empty()) game = it->second.front().path;
		if (game != suggestedFor_)
		{
			/* a seeded project's settings are kept: nothing of this game's
			 * suggestion was put in, so nothing is taken back */
			if (seeding_) suggestedKeys_.clear();
			keptFor_ = seeding_ ? game : std::string();
			if (seeding_ && !game.empty())
			{
				seededGame_ = game;
				seededPackage_ = package_;
			}
			suggestFor(game);
		}
	}

	const Json recorded = form_.recorded();
	const std::string settingsText = json::write(recorded);

	/* the slots this map and these settings leave open: the engine's rule */
	if (!slotsText_.empty())
	{
		uint64_t len = 0;
		const char *out = ce_slots_evaluate(slotsText_.data(), slotsText_.size(), map.data(), map.size(),
			settingsText.data(), settingsText.size(), &len);
		Json ids;
		std::string jerr;
		const bool read = out != nullptr && json::parse(std::string_view(out, static_cast<size_t>(len)), ids, jerr);
		for (Slot &s : slots_)
		{
			s.exposed = !read;
			for (const Json &id : ids.items) s.exposed = s.exposed || id.text == s.id;
		}
		/* what the machine has before any file: a slot only settings open,
		 * which this machine closes, is not on the form, as Chimera's */
		const char *bare = ce_slots_evaluate(slotsText_.data(), slotsText_.size(), "{}", 2, settingsText.data(), settingsText.size(), &len);
		Json bareIds;
		const bool bareRead = bare != nullptr && json::parse(std::string_view(bare, static_cast<size_t>(len)), bareIds, jerr);
		for (Slot &s : slots_)
		{
			s.machineHas = !bareRead;
			for (const Json &id : bareIds.items) s.machineHas = s.machineHas || id.text == s.id;
		}
	}

	/* the firmware these call for, found by the engine's rules unless chosen */
	needs_.clear();
	Json none, firmwareDecl, settingsDecl;
	none.kind = Json::Kind::Array;
	firmwareDecl = firmwareDecls();
	settingsDecl.kind = Json::Kind::Array;
	settingsDecl.items = form_.declarations();
	for (const FirmwareNeed &n : neededFirmware(firmwareDecl, settingsDecl, map, recorded, none, firmwareIndex_))
	{
		Need need{n.id, n.display.empty() ? n.id : n.display, n.path, n.how, n.sha1};
		need.declIndex = n.declIndex;
		std::string declared;
		/* the entry the engine chose: entries may share an id, one for each
		 * version a setting picks, and only this one is wanted */
		if (n.declIndex < firmwareDecl.items.size())
		{
			const Json &f = firmwareDecl.items[n.declIndex];
			if (const Json *h = f.find("sha1")) declared = h->text;
			auto text = [&f](const char *k) { const Json *j = f.find(k); return j != nullptr ? j->text : std::string(); };
			need.name = text("name");
			need.label = text("label");
			need.description = text("description");
			if (const Json *r = f.find("required")) need.required = r->boolean;
		}
		need.declSha1 = declared;
		if (auto it = chosenFirmware_.find(n.id); it != chosenFirmware_.end())
		{
			need.path = it->second.first;
			need.sha1 = it->second.second;
			need.how = !declared.empty() && lower(declared) != lower(need.sha1) ? "your own" : "chosen";
		}
		else if (need.path.empty())
		{
			/* where this core last had it, as Chimera remembers: a file of
			 * another hash only for a game core */
			const std::string remembered = cores_.chosenFirmwareFor(core.get(), n.id);
			std::error_code ec;
			if (!remembered.empty() && fs::is_regular_file(remembered, ec))
			{
				auto known = rememberedSha_.find(remembered);
				if (known == rememberedSha_.end())
				{
					char sha[41];
					uint64_t len = 0;
					known = rememberedSha_.emplace(remembered, ce_sha1_file(remembered.c_str(), sha, &len) == 1 ? std::string(sha) : std::string()).first;
				}
				const Json *kind = cfg_.find("kind");
				const bool gameCore = kind != nullptr && lower(kind->text) == "game";
				const bool other = !declared.empty() && lower(declared) != lower(known->second);
				if (!known->second.empty() && (!other || gameCore))
				{
					need.path = remembered;
					need.sha1 = known->second;
					need.how = other ? "your own" : "remembered";
				}
			}
		}
		/* Clear on the firmware page: none, until the page is shown again */
		if (std::find(clearedFirmware_.begin(), clearedFirmware_.end(), n.id) != clearedFirmware_.end() &&
			chosenFirmware_.find(n.id) == chosenFirmware_.end())
		{
			need.path.clear();
			need.sha1.clear();
			need.how.clear();
		}
		needs_.push_back(need);
	}

	/* what is missing */
	std::string lines;
	auto say = [&lines](const std::string &line) { lines += (lines.empty() ? "" : "\n") + line; };
	if (core.get().empty()) say(tr("new_project.choose_core"));
	for (const Slot &s : slots_)
	{
		const auto it = files_.find(s.id);
		const int64_t n = it == files_.end() ? 0 : static_cast<int64_t>(it->second.size());
		if (!s.exposed)
		{
			if (n > 0) say(tr("new_project.take_files_out", {s.title}));
			continue;
		}
		if (n < s.min) say(tr("new_project.files_needed", s.min, {s.title}));
		if (s.max >= 0 && n > s.max) say(tr("new_project.at_most", {s.title, std::to_string(s.max)}));
	}
	for (const std::vector<std::string> &group : atLeastOneOf_)
	{
		int64_t n = 0;
		std::string titles;
		for (const std::string &id : group)
		{
			if (const auto it = files_.find(id); it != files_.end()) n += static_cast<int64_t>(it->second.size());
			if (const Slot *s = slot(id); s != nullptr && s->exposed) titles += (titles.empty() ? "" : ", ") + s->title;
		}
		if (n == 0 && !titles.empty()) say(tr("new_project.one_of_these", {titles}));
	}
	for (const Need &n : needs_)
		if (n.path.empty() && n.required) say(tr("new_project.firmware_choose", {n.display}));
	problems.set(lines);

	/* the wizard's pages, in Chimera's words: the files page's complaint
	 * (a slot not on the form demands nothing), the firmware required and
	 * not provided */
	filesAsked.set(!slots_.empty());
	std::string complaint;
	for (const Slot &s : slots_)
	{
		if (!complaint.empty()) break;
		if (s.settingGatedOnly && !s.machineHas) continue;
		const auto it = files_.find(s.id);
		const int64_t n = it == files_.end() ? 0 : static_cast<int64_t>(it->second.size());
		if (n < s.min && s.exposed) complaint = tr("new_project.slot_needs", {s.title, cardinalityOf(s.min, s.max), std::to_string(n)});
		else if (s.max >= 0 && n > s.max) complaint = tr("new_project.slot_takes", {s.title, cardinalityOf(s.min, s.max), std::to_string(n)});
	}
	for (const std::vector<std::string> &group : atLeastOneOf_)
	{
		if (!complaint.empty()) break;
		int64_t n = 0;
		std::string titles;
		for (const std::string &id : group)
		{
			if (const auto it = files_.find(id); it != files_.end()) n += static_cast<int64_t>(it->second.size());
			const Slot *s = slot(id);
			titles += (titles.empty() ? "" : ", ") + (s != nullptr ? s->title : id);
		}
		if (n == 0) complaint = tr("new_project.one_across", {titles});
	}
	filesComplaint.set(complaint);
	firmwareAsked.set(!needs_.empty());
	std::string missing;
	for (const Need &n : needs_)
		if (missing.empty() && n.path.empty() && n.required) missing = n.display;
	firmwareMissing.set(missing);
	ready.set(lines.empty());
	/* Chimera's compile step: a package that compiles, and a game to compile */
	const json::Json *pre = cfg_.find("precompile");
	compileAsked.set(pre != nullptr && pre->kind == json::Json::Kind::Bool && pre->boolean && !compileGame().empty());
	refreshCompile();
	slotRows_.reset.emit();
	settingRows_.refreshed(settingRowsTold_);
	settingRowsTold_ = settingRows_.rowCount();
	presetRows_.reset.emit();
	firmwareRows_.reset.emit();
}

std::string NewProjectViewModel::SlotRows::columnName(int32_t column) const
{
	static const char *const names[] = {"id", "title", "min", "max", "formats", "help", "exposed", "files", "count", "filter",
		"cardinality", "shown"};
	return column >= 0 && column < 12 ? names[column] : "";
}

Value NewProjectViewModel::SlotRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Slot &s = vm_.slots_[static_cast<size_t>(row)];
	const auto it = vm_.files_.find(s.id);
	switch (column)
	{
	case 0: return s.id;
	case 1: return s.title;
	case 2: return s.min;
	case 3: return s.max;
	case 4:
	{
		std::string out;
		/* as Chimera says them: ".iso, .cue", a file with no extension in words */
		for (const std::string &f : vm_.formatsFor(s)) out += (out.empty() ? "" : ", ") + (f.empty() ? tr("new_project.no_extension") : "." + f);
		return out;
	}
	case 5: return s.help;
	case 6: return s.exposed;
	case 7:
	{
		std::string out;
		if (it != vm_.files_.end())
			for (const Picked &f : it->second) out += (out.empty() ? "" : "\n") + f.name;
		return out;
	}
	case 8: return it == vm_.files_.end() ? int64_t{0} : static_cast<int64_t>(it->second.size());
	case 9:
	{
		std::string out;
		for (const std::string &f : vm_.formatsFor(s)) out += (out.empty() ? "" : " ") + (f.empty() ? std::string("*") : "*." + f);
		return out.empty() ? std::string("*") : out;
	}
	case 10: return cardinalityOf(s.min, s.max);
	case 11: return !s.settingGatedOnly || s.machineHas;
	default: return {};
	}
}

int64_t NewProjectViewModel::ImportFileRows::rowCount() const
{
	const Json *files = vm_.import_.find("files");
	return files != nullptr ? static_cast<int64_t>(files->items.size()) : 0;
}

std::string NewProjectViewModel::ImportFileRows::columnName(int32_t column) const
{
	static const char *const names[] = {"option", "label", "filter", "required", "multiple", "files"};
	return column >= 0 && column < 6 ? names[column] : "";
}

Value NewProjectViewModel::ImportFileRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &f = vm_.import_.find("files")->items[static_cast<size_t>(row)];
	auto text = [&](const char *k) { const Json *j = f.find(k); return j != nullptr ? j->text : std::string(); };
	auto flag = [&](const char *k) { const Json *j = f.find(k); return j != nullptr && j->boolean; };
	switch (column)
	{
	case 0: return text("option");
	case 1: return text("label");
	case 2:
	{
		std::string filter;
		if (const Json *ex = f.find("extensions"))
			for (const Json &e : ex->items) filter += (filter.empty() ? "" : " ") + std::string("*.") + lower(!e.text.empty() && e.text[0] == '.' ? e.text.substr(1) : e.text);
		return filter.empty() ? std::string("*") : filter;
	}
	case 3: return flag("required");
	case 4: return flag("multiple");
	case 5:
	{
		std::string names;
		const auto it = vm_.importPicked_.find(text("option"));
		if (it != vm_.importPicked_.end())
			for (const std::string &p : it->second) names += (names.empty() ? "" : "\n") + baseName(p);
		return names;
	}
	default: return {};
	}
}

int64_t NewProjectViewModel::ImportOptionRows::rowCount() const
{
	const Json *opts = vm_.import_.find("options");
	return opts != nullptr ? static_cast<int64_t>(opts->items.size()) : 0;
}

std::string NewProjectViewModel::ImportOptionRows::columnName(int32_t column) const
{
	static const char *const names[] = {"name", "label", "on"};
	return column >= 0 && column < 3 ? names[column] : "";
}

Value NewProjectViewModel::ImportOptionRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Json &o = vm_.import_.find("options")->items[static_cast<size_t>(row)];
	const Json *name = o.find("name"), *label = o.find("display");
	const std::string n = name != nullptr ? name->text : "";
	switch (column)
	{
	case 0: return n;
	case 1: return label != nullptr ? label->text : n;
	case 2:
	{
		const auto it = vm_.importOptions_.find(n);
		return it != vm_.importOptions_.end() && it->second;
	}
	default: return {};
	}
}

std::string NewProjectViewModel::FirmwareRows::columnName(int32_t column) const
{
	static const char *const names[] = {"id", "name", "file", "how", "missing", "title", "expected", "sha1Shown", "state", "satisfied",
		"description"};
	return column >= 0 && column < 11 ? names[column] : "";
}

Value NewProjectViewModel::FirmwareRows::cell(int64_t row, int32_t column) const
{
	if (row < 0 || row >= rowCount()) return {};
	const Need &n = vm_.needs_[static_cast<size_t>(row)];
	switch (column)
	{
	case 0: return n.id;
	case 1: return n.display;
	case 2: return n.path;
	case 3: return n.how;
	case 4: return n.path.empty();
	/* the firmware page's columns, in Chimera's words */
	case 5: return n.label.empty() ? n.display : n.display + "  (" + n.label + ")";
	case 6: return n.name;
	case 7: return n.declSha1.empty() ? tr("new_project.own_dump") : n.declSha1;
	case 8:
		if (!n.path.empty())
			return tr(n.how == "your own" ? "new_project.firmware_own" : "new_project.firmware_found", {fs::path(n.path).filename().string()});
		return tr(n.required ? "project_files.not_found" : "new_project.firmware_optional");
	case 9: return !n.path.empty() || !n.required;
	case 10: return n.description;
	default: return {};
	}
}

} // namespace chimera::vm
