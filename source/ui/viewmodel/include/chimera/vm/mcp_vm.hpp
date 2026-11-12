#pragma once
/* Chimera's MCP server in the window (Tools > MCP Server..., docs/mcp-server.md):
 * a person starts it, allows the agent to make changes or not, and reads what it
 * did. The server is the engine's; the machine's thread runs the agent's calls
 * between frames (SessionViewModel), and the ones only the window can answer -
 * the movie as TAStudio holds it, its branches and markers, saving, closing,
 * Lua, making a project - come here.
 * Every change goes through the calls TAStudio itself uses, so it shows in the
 * piano roll and is a step of its undo history. */

#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "chimera/vm/branches_vm.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/lua_console_vm.hpp"
#include "chimera/vm/new_project_vm.hpp"
#include "chimera/vm/piano_roll_vm.hpp"
#include "chimera/vm/session_vm.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class McpViewModel : public ViewModel
{
public:
	McpViewModel(Dispatcher &dispatcher, SessionViewModel &session, PianoRollViewModel &roll, BranchesViewModel &branches);

	/* what only the window's program knows, given by it: server_info's answer
	 * (this build, its folders), and the folder a project saved by a relative
	 * path goes in (the Movies folder) */
	std::function<std::string()> serverInfo;
	std::function<std::string()> projectsFolder;
	/* the windows the agent's Lua runs in and its projects are made by, when
	 * the program has them; and the Lua Console shown, as a person opens it,
	 * so the agent's Lua is seen where it runs */
	LuaConsoleViewModel *lua = nullptr;
	NewProjectViewModel *newProject = nullptr;
	std::function<void()> showLuaConsole;
	/* screenshot with layers "with_lua" (issue 571): the picture as the
	 * window shows it, answered to this call by the window's program; none
	 * where no window is shown */
	std::function<void(uint64_t)> shownPicture;

	/* "stopped", "waiting" (no agent connected) or "connected" */
	Property<std::string> state{"stopped"};
	/* what the window says it is doing */
	Property<std::string> stateText{"Stopped."};
	/* the tick box: the agent may make changes; remembered, applied at every start */
	Property<bool> allowChanges{false};
	/* what the tick box shows: started, what the engine allows (the command line's
	 * --mcp-allow-changes is not remembered); stopped, what the next start applies */
	Property<bool> allowedNow{false};
	/* whether the server is started, for the title bar and the menu's icon */
	Property<bool> started{false};
	/* the command that sets an agent up (Claude Code's), with the program it starts */
	Property<std::string> setupCommand;
	/* the log, one line a call, the last 5000 */
	Property<std::string> log;
	/* the line under the log */
	Property<std::string> note;

	/* the answer to a call the engine handed the window: the tool's name, its
	 * arguments as JSON; `answer` is given the text and whether it is a refusal,
	 * or null is returned for one answered later (a seek, advance_until,
	 * capture_series) */
	void host(uint64_t id, const std::string &tool, const std::string &arguments);

private:
	SessionViewModel &session_;
	PianoRollViewModel &roll_;
	BranchesViewModel &branches_;
	std::deque<std::string> lines_;
	uint64_t logRead_ = 0;
	Connection allowWatch_;
	void refresh();
	std::string startError_;
	void answerAfter(std::function<void()> then);
	RowModel *rowsOf(ViewModel &vm, const std::string &name);
	/* the tools of other windows': create_project, lua_functions, run_lua, lua_script */
	void createProject(uint64_t id, const json::Json &arguments);
	void luaTool(const std::string &tool, const json::Json &arguments, const std::function<void(const std::string &, bool)> &answer);
};

} // namespace chimera::vm
