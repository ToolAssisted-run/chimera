#pragma once
/* Chimera's Help menu: the build and the core open, worded as a bug report
 * asks for them (Copy Version Info, chimera issue 188); a report's first
 * lines filled in from this session, the files worth attaching, and a zip
 * of them all (Report an Issue, chimera issue 243). Nothing is sent: the
 * person files the report in their browser. */

#include <functional>
#include <string>
#include <vector>

#include "chimera/vm/crash_notes.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/settings_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class HelpViewModel : public ViewModel
{
public:
	HelpViewModel(Dispatcher &dispatcher, SessionViewModel &session, SettingsViewModel &settings);
	/* this build's commit, its first nine characters (what --version prints, as Chimera's) */
	static std::string thisCommit();

	/* this build, as Chimera names one: "Commit 1a2b3c4d5 (2026-10-26 18:35
	 * CET)", its commit and when that commit was made, on the European clock
	 * every date is written in (chimera issue 331); the commit alone, as
	 * Lua's client.getversion gives it; the commit on the web */
	Property<std::string> buildName, commit, commitUrl;
	/* where Chimera's code is, and its site */
	Property<std::string> homePage{"https://github.com/ToolAssisted-run/chimera"};
	Property<std::string> siteUrl{"https://toolassisted.run"};
	Property<std::string> formUrl{"https://github.com/ToolAssisted-run/chimera/issues/new/choose"};
	/* the computer's system, as the window's toolkit names it (the window
	 * says; "" leaves the report's line for the person to fill in) */
	Property<std::string> osName;
	/* the graphics card the window draws on, as the window's toolkit names
	 * it (the window says once it has drawn; "" when it cannot tell). A
	 * report names the card a core drew on first (issue 427) */
	Property<std::string> windowGpu;
	/* asked for the card when a report is made and windowGpu has no name
	 * (the window sets it: no toolkit here) */
	std::function<std::string()> gpuNow;
	/* the report made last: its text, and the files to attach, a line each */
	Property<std::string> reportText, reportFiles;
	Property<std::string> status;
	/* where the program is: the sandbox's log is looked for there first */
	Property<std::string> exeFolder;
	/* where crash notes are: the newest one not reported yet goes with a
	 * report, its dump beside it */
	Property<std::string> crashFolder;
	/* when a report was last saved, local time "yyyy-MM-dd HH:mm:ss" (kept
	 * between sessions): a crash note older than it was reported already */
	Property<std::string> lastReport;

	/* Which of these notes (newest first) goes with a report, or -1: the
	 * newest written after `lastReport` ("" for never), and, with a project
	 * open, one from that project. A note of another project, or one already
	 * reported, is not this report's (a report carried a ten-day-old note of
	 * another game, issue 659). */
	static int reportableNote(const std::vector<crash::Note> &newestFirst, const std::string &lastReport, bool projectOpen,
		const std::string &projectPath);
	/* when a note was written, as its time= line says, else as its file name does */
	static std::string noteTime(const crash::Note &note);
	/* the project a note names under "session:", "" for none */
	static std::string noteProject(const crash::Note &note);

	/* the core open and its version, as the Cores window words it ("" for none) */
	std::string coreAndVersion() const;

private:
	SessionViewModel &session_;
	SettingsViewModel &settings_;
	/* the build as a report names it: in the issue form's English, where
	 * buildName is in the window's language (owner, 2026-10-31) */
	std::string reportBuild_;
};

} // namespace chimera::vm
