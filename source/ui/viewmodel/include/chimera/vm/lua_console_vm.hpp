#pragma once
/* The Lua Console, as Chimera's: a list of scripts, each started and stopped
 * (toggled), paused, moved, duplicated or taken out, with separators between
 * them; the list kept as a session (a .luases file: a line a script, "1 " or
 * "0 " for whether it runs, then its path, and "---" for a separator); what
 * the scripts print; a line to type Lua into; and the functions the scripts
 * registered, to call or remove. The scripts run in the bridge. */

#include <filesystem>
#include <string>
#include <vector>

#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/view_model.hpp"

namespace chimera::vm {

class LuaConsoleViewModel : public ViewModel
{
public:
	/* takes over the bridge's output: everything it prints comes here (and to
	 * `echo` a line at a time, when given: Chimera gives it for --headless,
	 * where the program's standard output is all anybody sees, issue 529) */
	LuaConsoleViewModel(Dispatcher &dispatcher, LuaBridge &lua, std::function<void(const std::string &)> echo = {});
	/* Chimera's Lua Functions List made from its documentation and the
	 * window's own list ([{"library", "name", "params"}]), as JSON */
	static std::string functionsList(const std::string &windowList);
	~LuaConsoleViewModel() override;

	/* the session's file, "" until one is opened or saved */
	Property<std::string> sessionPath;
	/* the list changed since the session was opened or saved */
	Property<bool> changes{false};
	/* the line under the window: the session's file name, "* " before it
	 * while changed, or what saving it did */
	Property<std::string> status;
	/* "2 scripts (1 active, 0 paused)", as Chimera counts them */
	Property<std::string> summary{"0 scripts"};
	Property<int64_t> lines{0};
	/* any script running or paused */
	Property<bool> running{false};
	/* rows selected, and of them the scripts (not separators) */
	Property<int64_t> selectedCount{0};
	Property<int64_t> selectedScripts{0};
	/* any script in the list running or paused */
	Property<bool> anyEnabled{false};
	Property<int64_t> functionCount{0};
	/* the script Toggle Last Lua Script toggles: the last one toggled */
	Property<std::string> lastScript;
	/* the output lines selected (for Copy), first..last; -1 for none */
	Property<int64_t> outputFirst{-1};
	Property<int64_t> outputLast{-1};

	/* Settings, remembered */
	Property<bool> disableOnLoad{false};
	Property<bool> toggleAllIfNoneSelected{true};
	Property<bool> reloadOnChange{false};
	/* newest first, a line each, remembered */
	Property<std::string> recentSessions;
	Property<std::string> recentScripts;
	/* the Paths' Lua folder: where New Script's template is kept */
	Property<std::string> luaFolder;

	/* a frame done: the scripts' turn after it, then the window's part
	 * (the tests call it; the program gives the scripts their turns on the
	 * machine's thread and the window its part with afterFrame) */
	void frameDone();
	/* the window's part of a frame done: the write count for the frame
	 * begins again, and the list and functions are said again, as the
	 * scripts were seen at the end of the frame */
	void afterFrame(const LuaBridge::Seen &seen);
	/* text the scripts wrote, line ends and all */
	void append(const std::string &text);

	/* a machine opened or closed: every script stopped, and those that ran
	 * started again from their beginning, as Chimera's Lua Console restarts
	 * on a core change */
	void restartScripts();

	/* The MCP server's Lua tools (issue 355), where the person sees them: */
	/* what this console printed since the agent last asked, scripts' prints among it */
	std::string takeOutputForAgent();
	/* run_lua: the agent's code shown, then run (LuaBridge::runForAgent);
	 * what it returned as a JSON array, or false with what it raised */
	bool runForAgent(const std::string &code, std::string &results, std::string &error);
	/* lua_script: a script file started, as Open Script starts one, or
	 * stopped and taken out of the list; whether it runs now */
	bool scriptForAgent(const std::string &path, bool stop);

private:
	struct Entry
	{
		std::string path;
		bool separator = false;
		/* the bridge's number for it while it runs or is paused, else 0 */
		int64_t script = 0;
		bool selected = false;
		std::filesystem::file_time_type written{};
	};
	struct Function
	{
		std::string event, name, guid;
	};

	class Scripts : public RowModel
	{
	public:
		explicit Scripts(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.entries_.size()); }
		int32_t columnCount() const override { return 5; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		LuaConsoleViewModel &vm_;
	};
	class Output : public RowModel
	{
	public:
		explicit Output(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.output_.size()); }
		int32_t columnCount() const override { return 2; }
		std::string columnName(int32_t column) const override { return column == 0 ? "line" : "selected"; }
		Value cell(int64_t row, int32_t column) const override;

	private:
		LuaConsoleViewModel &vm_;
	};
	class Functions : public RowModel
	{
	public:
		explicit Functions(LuaConsoleViewModel &vm) : vm_(vm) {}
		int64_t rowCount() const override { return static_cast<int64_t>(vm_.functions_.size()); }
		int32_t columnCount() const override { return 3; }
		std::string columnName(int32_t column) const override;
		Value cell(int64_t row, int32_t column) const override;

	private:
		LuaConsoleViewModel &vm_;
	};

	/* a script's file into the list: started unless Disable Scripts on Load
	 * (or `start` is false); one already there is started if stopped */
	void openScript(const std::string &path, bool start);
	void enable(Entry &e);
	void toggle(Entry &e);
	void removeAll();
	bool loadSession(const std::string &path, std::string &error);
	bool saveSession(const std::string &path, std::string &error);
	/* the entries Toggle and Refresh work on: those selected, or all when
	 * none is and Toggle All if None Selected is on */
	std::vector<size_t> targets() const;
	std::vector<size_t> selectedRows() const;
	void listChanged();
	void markChanged();
	/* the list and functions said again, as the scripts are now (asked of
	 * the scripts' thread and waited for) or as they were seen */
	void refresh();
	void refresh(const LuaBridge::Seen &seen);
	void refreshFunctions(bool always);
	void refreshFunctions(bool always, const std::string &stamp);
	void addRecent(Property<std::string> &list, const std::string &path);
	void line(const std::string &text);

	LuaBridge &lua_;
	/* the scripts as last seen: what the list shows */
	LuaBridge::Seen seen_;
	std::function<void(const std::string &)> echo_;
	std::vector<Entry> entries_;
	std::vector<std::string> output_;
	std::vector<Function> functions_;
	std::vector<std::string> history_;
	int64_t historyAt_ = -1;
	/* what was printed since the agent last asked, its end kept */
	std::string forAgent_;
	/* the last line has no line end yet (console.write) */
	bool open_ = false;
	/* what was written since the last frame: past 100 the rest is not shown, as Chimera's */
	int messages_ = 0;
	int64_t anchor_ = -1;
	int64_t outputAnchor_ = -1;
	std::string sortedBy_;
	bool sortReverse_ = false;
	std::string functionsStamp_;
	Output rows_{*this};
	Scripts scripts_{*this};
	Functions functionRows_{*this};
	Connection changesWatch_, sessionWatch_;
};

} // namespace chimera::vm
