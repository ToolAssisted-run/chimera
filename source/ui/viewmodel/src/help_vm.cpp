#include "chimera/vm/help_vm.hpp"
#include "chimera/vm/tr.hpp"

#include "chimera/vm/crash_notes.hpp"
#include "chimera/vm/issue_report.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include "chimera/engine.h"
#include "chimera/vm/european_clock.hpp"
#include "chimera/vm/json.hpp"
#include "ui_build.h"

/* a copy of the engine's miniz, kept to this library: a report's zip is
 * deflated, as Chimera's is */
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "../../../../extern/miniz/miniz.h"

namespace chimera::vm {

namespace {
std::string readAll(const std::filesystem::path &path)
{
	std::ifstream in(path, std::ios::binary);
	return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::vector<std::string> linesOf(const std::string &text)
{
	std::vector<std::string> out;
	size_t at = 0;
	while (at < text.size())
	{
		const size_t end = text.find('\n', at);
		std::string line = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
		at = end == std::string::npos ? text.size() : end + 1;
		if (!line.empty()) out.push_back(std::move(line));
	}
	return out;
}

std::string lowered(std::string s)
{
	for (char &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return s;
}
} // namespace

std::string HelpViewModel::thisCommit()
{
	const std::string stamp = CHIMERA_UI_BUILD;
	return stamp.substr(0, std::min<size_t>(stamp.find(' '), 9));
}

HelpViewModel::HelpViewModel(Dispatcher &dispatcher, SessionViewModel &session, SettingsViewModel &settings)
	: ViewModel("help", dispatcher), session_(session), settings_(settings)
{
	/* the build: its commit and that commit's time, as Chimera's About box
	 * and its reports name it (Commit 1a2b3c4d5 (2026-10-26 18:35 CET)) */
	{
		const std::string stamp = CHIMERA_UI_BUILD;
		const size_t space = stamp.find(' ');
		const std::string full = stamp.substr(0, space);
		const int64_t when = space == std::string::npos ? 0 : std::atoll(stamp.c_str() + space + 1);
		commit.set(full.substr(0, 9));
		commitUrl.set(homePage.get() + "/commit/" + full);
		buildName.set(when > 0 ? tr("help.build_dated", {commit.get(), europeanTime(when)}) : tr("help.build", {commit.get()}));
	}
	expose("buildName", "this build, as Chimera's About box and a report name it", buildName);
	expose("commit", "the commit this build was made from, as Lua's client.getversion gives it", commit);
	expose("commitUrl", "that commit on the web", commitUrl);
	expose("homePage", "where Chimera's code is", homePage);
	expose("siteUrl", "Chimera's site", siteUrl);
	expose("formUrl", "where an issue is filed", formUrl);
	expose("osName", "the computer's system, as the window's toolkit names it", osName, true);
	expose("windowGpu", "the graphics card the window draws on, as the window's toolkit names it", windowGpu, true);
	expose("reportText", "the report made last, its first lines filled in", reportText);
	expose("reportFiles", "the files worth attaching to it, a line each", reportFiles);
	expose("status", "what Help last had to say (the window says here what it copied)", status, true);
	expose("exeFolder", "where the program is: the sandbox's log is looked for there first", exeFolder, true);
	expose("crashFolder", "where crash notes are: the newest, and its dump, go with a report", crashFolder, true);

	command("versionReport", "the build and the core open, in the words a bug report asks for them (Chimera's Copy Version Info)",
		{}, [this](const Args &, std::string &) -> Value {
			return issue_report::versionLines(buildName.get(), coreAndVersion());
		});
	command("makeReport", "fills in a report's first lines from this session and lists the files worth attaching, as "
		"Chimera's Report an Issue", {}, [this](const Args &, std::string &) -> Value {
			issue_report::Facts facts;
			facts.build = buildName.get();
			facts.core = coreAndVersion();
			facts.os = osName.get();
			/* the card a core drew on in this session, in its driver's words; else the window's */
			facts.glRenderer = ce_gl_renderer() != nullptr ? ce_gl_renderer() : "";
			facts.glVersion = ce_gl_version() != nullptr ? ce_gl_version() : "";
			facts.windowGpu = windowGpu.get();
			facts.cpu = ce_host_cpu_name() != nullptr ? ce_host_cpu_name() : "";
			facts.threads = std::thread::hardware_concurrency();
			facts.memory = ce_host_memory_total();
			facts.projectOpen = session_.hasProject.get();
			if (facts.projectOpen)
			{
				/* the open project's own list: slot and file name */
				session_.onProjectWait([&](ce_project *p) {
					if (p == nullptr) return;
					for (int32_t i = 0; i < ce_project_file_count(p); i++)
					{
						const char *slot = ce_project_file_slot(p, i), *name = ce_project_file_name(p, i);
						facts.files.emplace_back(slot != nullptr ? slot : "", name != nullptr ? name : "");
					}
				});
			}
			facts.coreRunning = session_.isOpen.get();
			if (facts.coreRunning)
				for (const auto &c : settings_.changedSettings()) facts.changed.push_back({c.label, c.value, c.standard});
			const std::string text = issue_report::firstLines(facts);
			reportText.set(text);
			/* the files: the project, and the sandbox's log, beside the program
			 * first and then where it runs, as Chimera looks */
			std::string list;
			std::error_code ec;
			if (session_.hasProject.get() && std::filesystem::is_regular_file(session_.projectPath.get(), ec)) list = session_.projectPath.get();
			for (const std::filesystem::path &folder : {std::filesystem::path(exeFolder.get()), std::filesystem::current_path(ec)})
			{
				if (folder.empty()) continue;
				const std::filesystem::path log = folder / "minibox-diag.log";
				if (!std::filesystem::is_regular_file(log, ec) || list.find(log.string()) != std::string::npos) continue;
				list += (list.empty() ? "" : "\n") + log.string();
				break;
			}
			/* the newest crash note and its memory dump */
			if (!crashFolder.get().empty())
			{
				const std::vector<crash::Note> notes = crash::notesIn(crashFolder.get());
				if (!notes.empty())
				{
					list += (list.empty() ? "" : "\n") + notes.front().path;
					if (!notes.front().dump.empty()) list += "\n" + notes.front().dump;
				}
			}
			/* the core's log, while Export Core Log is on */
			if (const char *core = ce_core_log_path(); core != nullptr && *core != '\0' && std::filesystem::is_regular_file(core, ec))
				list += (list.empty() ? "" : "\n") + std::string(core);
			reportFiles.set(list);
			status.set(list.empty() ? tr("help.nothing_to_attach") : "");
			return text;
		});
	command("saveReport", "the report, the game's picture (a PNG file, \"\" for none) and the files to attach (a line each, of "
		"the report's; all of them when left out) in one zip, deflated as Chimera's; what came of it is said in status",
		{"zip", "picture", "files"}, [this](const Args &a, std::string &error) -> Value {
			std::string zip, picture, ticked;
			if (!arg(a, 0, zip, error)) return {};
			if (a.size() > 1 && !arg(a, 1, picture, error)) return {};
			const bool some = a.size() > 2;
			if (some && !arg(a, 2, ticked, error)) return {};
			const std::vector<std::string> wanted = linesOf(ticked);
			std::vector<std::string> paths;
			for (const std::string &path : linesOf(reportFiles.get()))
				if (!some || std::find(wanted.begin(), wanted.end(), path) != wanted.end()) paths.push_back(path);
			auto refuse = [&](const std::string &why) -> Value {
				std::error_code ec;
				std::filesystem::remove(zip, ec);
				status.set(tr("help.zip_not_saved", {why}));
				error = status.get();
				return {};
			};
			mz_zip_archive out{};
			if (!mz_zip_writer_init_heap(&out, 0, 0)) return refuse(tr("help.no_memory"));
			bool ok = true;
			/* the text with plain line ends, and one more */
			std::string text;
			for (const char c : reportText.get())
				if (c != '\r') text += c;
			text += '\n';
			ok = ok && mz_zip_writer_add_mem(&out, "report.md", text.data(), text.size(), MZ_DEFAULT_LEVEL);
			std::error_code ec;
			if (!picture.empty() && std::filesystem::is_regular_file(picture, ec))
			{
				/* a picture is stored: it is compressed already */
				const std::string png = readAll(picture);
				if (!png.empty()) ok = ok && mz_zip_writer_add_mem(&out, "game.png", png.data(), png.size(), MZ_NO_COMPRESSION);
			}
			std::vector<std::string> taken{"report.md", "game.png"}, unread;
			for (const std::string &path : paths)
			{
				std::ifstream in(path, std::ios::binary);
				if (!in)
				{
					unread.push_back(path);
					continue;
				}
				const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
				const std::filesystem::path p(path);
				std::string name = p.filename().string();
				/* two files of one name both go in, whatever their case */
				for (int n = 2; std::find(taken.begin(), taken.end(), lowered(name)) != taken.end(); n++)
					name = p.stem().string() + " (" + std::to_string(n) + ")" + p.extension().string();
				taken.push_back(lowered(name));
				ok = ok && mz_zip_writer_add_mem(&out, name.c_str(), bytes.data(), bytes.size(), MZ_DEFAULT_LEVEL);
			}
			void *buffer = nullptr;
			size_t size = 0;
			ok = ok && mz_zip_writer_finalize_heap_archive(&out, &buffer, &size);
			mz_zip_writer_end(&out);
			if (!ok)
			{
				mz_free(buffer);
				return refuse(tr("help.not_put_together"));
			}
			{
				std::ofstream file(zip, std::ios::binary);
				file.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
				mz_free(buffer);
				if (!file) return refuse(tr("session.could_not_write", {zip}));
			}
			/* as Chimera says it: the name, the size, what could not be read, past what an issue takes */
			char megabytes[32];
			std::snprintf(megabytes, sizeof megabytes, "%.1f", static_cast<double>(size) / (1024.0 * 1024.0));
			std::string said = tr("help.zip_saved", {std::filesystem::path(zip).filename().string(), megabytes});
			if (!unread.empty())
			{
				std::string names;
				for (size_t i = 0; i < unread.size(); i++) names += (i == 0 ? "" : ", ") + std::filesystem::path(unread[i]).filename().string();
				said += " " + tr("help.unread", {names});
			}
			constexpr uint64_t kLimit = 25ull * 1024 * 1024;
			if (size > kLimit) said += " " + tr("help.too_big");
			status.set(said);
			return static_cast<int64_t>(size);
		});
}

std::string HelpViewModel::coreAndVersion() const
{
	if (!session_.isOpen.get() || session_.coreName.get().empty()) return {};
	/* the version as the Cores window words it: its date, then the
	 * package's version, eight characters */
	std::string config;
	const char *perr = nullptr;
	if (ce_package *p = ce_package_open(session_.packagePath().c_str(), &perr))
	{
		uint64_t len = 0;
		if (const uint8_t *d = ce_package_entry_texts(p, "waterbox.config", &len)) config.assign(reinterpret_cast<const char *>(d), static_cast<size_t>(len));
		ce_package_free(p);
	}
	json::Json cfg;
	std::string err, version, dated;
	if (json::parse(config, cfg, err))
	{
		if (const json::Json *v = cfg.find("version")) version = v->text;
		int64_t when = 0;
		if (const json::Json *m = cfg.find("versionDate"); m != nullptr && parseIsoTime(m->text, when)) dated = europeanTime(when);
	}
	if (const size_t plus = version.find('+'); plus != std::string::npos) version.erase(plus);
	if (version.size() > 8) version.erase(8);
	std::string text = session_.coreName.get();
	if (!dated.empty()) text += " " + dated + (version.empty() ? "" : " (" + version + ")");
	else if (!version.empty()) text += " " + version;
	return text;
}

} // namespace chimera::vm
