#include "chimera/vm/issue_report.hpp"

#include <cstdio>
#include <filesystem>

namespace chimera::vm::issue_report {

std::string versionLines(const std::string &build, const std::string &core)
{
	std::string text = "**Chimera build:** " + build;
	if (!core.empty()) text += "\n**Core and version:** " + core;
	return text;
}

std::string firstLines(const Facts &f)
{
	std::vector<std::string> fields;
	fields.push_back("**Chimera build:** " + f.build);
	fields.push_back("**Core and version:** " + (f.core.empty() ? std::string("<!-- no core was running -->") : f.core));
	const std::string gpu = !f.glRenderer.empty() ? f.glRenderer + ", OpenGL " + f.glVersion : f.windowGpu;
	fields.push_back("**OS and GPU:** " + (f.os.empty() ? std::string("<!-- type your operating system here -->") : f.os) + ", " +
		(gpu.empty() ? std::string("<!-- type your graphics card here -->") : gpu));
	std::string cpu = f.cpu.empty() ? std::string("<!-- type your processor here -->") : f.cpu;
	if (f.threads > 0) cpu += " (" + std::to_string(f.threads) + " threads)";
	char gb[32];
	std::snprintf(gb, sizeof gb, "%.1f GB of memory", static_cast<double>(f.memory) / static_cast<double>(1ull << 30));
	fields.push_back("**CPU and memory:** " + cpu + ", " +
		(f.memory == 0 ? std::string("<!-- type how much memory your computer has here -->") : std::string(gb)));
	/* the project's files: one, the game, on the line itself */
	std::string game = "**Game:**";
	if (!f.projectOpen)
		game += " <!-- no project was open -->";
	else
	{
		auto named = [](const std::pair<std::string, std::string> &file, bool alone) {
			const std::string name = std::filesystem::path(file.second).filename().string();
			return alone && file.first == "game" ? name : file.first + ": " + name;
		};
		if (f.files.empty()) game += " the project has no files of its own";
		else if (f.files.size() == 1) game += " " + named(f.files[0], true);
		else
			for (const auto &file : f.files) game += "\n- " + named(file, false);
	}
	fields.push_back(game);
	std::string settings = "**Renderer and non-default settings:**";
	if (!f.coreRunning)
		settings += " <!-- no core was running -->";
	else
	{
		if (f.changed.empty()) settings += " every setting is at its default";
		for (const Setting &c : f.changed) settings += "\n- " + c.label + ": " + c.value + " (default: " + c.standard + ")";
	}
	fields.push_back(settings);
	std::string text;
	for (const std::string &field : fields) text += (text.empty() ? "" : "\n\n") + field;
	return text;
}

} // namespace chimera::vm::issue_report
