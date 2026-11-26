#pragma once
/* Chimera's Paths window (Config > Paths): the global folders (the base,
 * where games are looked for, firmware, projects and their backups, video
 * dumps, tools, Lua scripts, themes, watch lists, debug logs, bug reports,
 * macros, Output, temporary files). Output is where a core's own files go by
 * default, each core in a folder of its id (outputFor); Chimera names no
 * system and keeps no folder for one (owner, 2026-11-26, issue 655). A path
 * is absolute, or starts with "." for the base (which starts from the
 * program's folder), or with %exe% (the program's folder), %rom% (where the
 * last game came from) or %recent% (the system's recent files folder);
 * anything else is the program's folder, as Chimera's AbsolutePathFor
 * decides. The window and its other parts ask here where a folder is
 * (resolve, outputFor). Changed paths are remembered; the rest
 * stay Chimera's. "Always use recent path for ROMs" opens the game dialog
 * where the last game came from. */

#include <string>
#include <vector>

#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class PathsViewModel : public ViewModel
{
public:
	explicit PathsViewModel(Dispatcher &dispatcher);

	/* the global base, as set ("" for Chimera's "."); relative paths start from it */
	Property<std::string> base;
	/* the program's folder, said by the window at the start */
	Property<std::string> programFolder;
	/* the global paths changed from Chimera's, {"Movies": "D:/runs", ...} */
	Property<std::string> changed{"{}"};
	/* the systems that have folders of their own (a game was opened for each), and
	 * their paths changed from Chimera's: {"NES": {"Screenshots": "./Shots"}} */
	Property<std::string> systems{"[]"};
	Property<std::string> systemChanged{"{}"};
	/* the game dialog opens where the last game came from */
	Property<bool> recentForRoms{true};
	Property<std::string> lastRomFolder;

	/* Chimera's global names and paths, in its order */
	static const std::vector<std::pair<std::string, std::string>> &defaults();
	/* a global folder as it is: its path made absolute; "" for a name that is not one */
	std::string resolve(const std::string &name) const;
	/* where a core's own files go by default: the Output folder, then a folder
	 * named by the core's id (its package's file name without .chimeraCore,
	 * never its version). The Output folder itself for "". Nothing is made. */
	std::string outputFor(const std::string &core) const;
	/* the Temp Files folder made absolute, "" while it is not set (the
	 * system's own temporary folder is used then) */
	std::string tempFolder() const;
	/* a path as it is set (changed or Chimera's) */
	std::string pathOf(const std::string &name) const;
	/* a path made absolute */
	std::string absolute(const std::string &path) const;
	/* any of them changed or moved */
	Signal<> moved;

private:
	class Rows : public RowModel
	{
	public:
		explicit Rows(PathsViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override;
		int32_t columnCount() const override { return 4; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		PathsViewModel &vm_;
	};
	std::string globalBase() const;
	Rows rows_{*this};
	Connection baseWatch_, programWatch_, changedWatch_;
};

} // namespace chimera::vm
