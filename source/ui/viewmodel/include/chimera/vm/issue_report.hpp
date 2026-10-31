#pragma once
/* A bug report's first lines, as Chimera's issue form on GitHub asks for
 * them (Help > Report an Issue). They stay in English, the form's own
 * words: the report is read by Chimera's developers, as its logs are
 * (docs/translations.md). */

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace chimera::vm::issue_report {

struct Setting
{
	std::string label, value, standard;
};

struct Facts
{
	std::string build, core, os, cpu;
	/* the graphics card: the one a core drew on through OpenGL, as its
	 * driver gives it, else the one the window draws on; "" when not known */
	std::string glRenderer, glVersion, windowGpu;
	unsigned threads = 0;
	uint64_t memory = 0;
	bool projectOpen = false, coreRunning = false;
	/* the open project's files: slot and file name */
	std::vector<std::pair<std::string, std::string>> files;
	/* the settings not at their defaults */
	std::vector<Setting> changed;
};

/* the build as the form names it, in its English whatever the window's
 * language: "Commit 1a2b3c4d5 (2026-10-26 18:35 CET)", the time left out
 * when it is not known */
std::string buildLine(const std::string &commit, const std::string &time);
/* the build and the core open (Chimera's Copy Version Info) */
std::string versionLines(const std::string &build, const std::string &core);
/* every first line of a report */
std::string firstLines(const Facts &facts);

} // namespace chimera::vm::issue_report
