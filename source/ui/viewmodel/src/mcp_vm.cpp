#include "chimera/vm/mcp_vm.hpp"
#include "chimera/vm/lua_bridge.hpp"
#include "chimera/vm/tr.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <filesystem>

#include "chimera/engine.h"

namespace chimera::vm {

using json::Json;

namespace {
/* the most frames one call may write, which keeps a slip of the agent's small */
constexpr int64_t kMostFrames = 100000;

Json object() { Json j; j.kind = Json::Kind::Object; return j; }
void add(Json &o, const char *name, Json v) { o.members.emplace_back(name, std::move(v)); }

/* a whole-number argument in [low, high]; the fallback when it is left out and one is given */
bool number(const Json &arguments, const char *name, int64_t low, int64_t high, const int64_t *fallback, int64_t &value)
{
	value = fallback != nullptr ? *fallback : 0;
	const Json *token = arguments.find(name);
	if (token == nullptr || token->kind == Json::Kind::Null) return fallback != nullptr;
	if (token->kind != Json::Kind::Number || token->text.find_first_of(".eE") != std::string::npos) return false;
	char *end = nullptr;
	const long long asked = std::strtoll(token->text.c_str(), &end, 10);
	if (end == token->text.c_str() || asked < low || asked > high) return false;
	value = asked;
	return true;
}

/* a path the agent gave, in UTF-8 */
std::filesystem::path pathOf(const std::string &text) { return std::filesystem::path(std::u8string(text.begin(), text.end())); }

std::string textOf(const Json &arguments, const char *name)
{
	const Json *t = arguments.find(name);
	return t != nullptr && t->kind == Json::Kind::String ? t->text : std::string();
}

bool flagOf(const Json &arguments, const char *name)
{
	const Json *t = arguments.find(name);
	return t != nullptr && t->kind == Json::Kind::Bool && t->boolean;
}

std::string localNow(const char *format)
{
	const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	std::tm local{};
#ifdef _WIN32
	localtime_s(&local, &t);
#else
	localtime_r(&t, &local);
#endif
	char buf[40];
	std::strftime(buf, sizeof buf, format, &local);
	return buf;
}
} // namespace

McpViewModel::McpViewModel(Dispatcher &dispatcher, SessionViewModel &session, PianoRollViewModel &roll, BranchesViewModel &branches)
	: ViewModel("mcp", dispatcher), session_(session), roll_(roll), branches_(branches)
{
	expose("state", "the server: \"stopped\", \"waiting\" (no agent connected) or \"connected\"", state);
	expose("stateText", "what the window says the server is doing", stateText);
	expose("allowChanges", "the agent may make changes (edit input, move the playback position, write memory); remembered, "
		"applied every time the server is started", allowChanges, true);
	expose("allowedNow", "what the tick box shows: started, what the engine allows; stopped, what the next start applies", allowedNow);
	expose("started", "the server is started", started);
	expose("setupCommand", "the command that sets an agent up (Claude Code's)", setupCommand);
	expose("log", "what happened, one line a call", log);
	expose("note", "the line under the log", note, true);
	command("toggle", "starts the server when it is stopped, stops it when it is started", {}, [this](const Args &, std::string &) -> Value {
		startError_.clear();
		if (ce_mcp_state() == CE_MCP_STOPPED)
		{
			const char *error = nullptr;
			if (ce_mcp_start(&error) != 1) startError_ = error != nullptr ? error : tr("common.no_reason");
			else
			{
				ce_mcp_allow_changes(allowChanges.get() ? 1 : 0);
				session_.mcpServing(true);
			}
		}
		else
		{
			ce_mcp_stop();
			session_.mcpServing(false);
		}
		refresh();
		return {};
	});
	command("startForRun", "--mcp-server: started as by the button, for a run nobody clicks in; with --mcp-allow-changes "
		"the agent may make changes for this run, not remembered", {"changes"}, [this](const Args &a, std::string &error) -> Value {
		bool changes = false;
		if (!arg(a, 0, changes, error)) return {};
		const char *why = nullptr;
		if (ce_mcp_start(&why) != 1)
		{
			error = why != nullptr ? why : tr("mcp.not_started");
			return {};
		}
		ce_mcp_allow_changes(allowChanges.get() || changes ? 1 : 0);
		session_.mcpServing(true);
		refresh();
		return std::string(ce_mcp_endpoint() != nullptr ? ce_mcp_endpoint() : "");
	});
	command("refresh", "brings the window up to date with the engine: the state, the tick box, new log lines", {},
		[this](const Args &, std::string &) -> Value {
			refresh();
			return {};
		});
	command("saveLog", "writes the log as shown to a file", {"path"}, [this](const Args &a, std::string &error) -> Value {
		std::string path;
		if (!arg(a, 0, path, error)) return {};
		std::ofstream out(path, std::ios::binary);
		out << log.get();
		note.set(out ? tr("mcp.log_saved", {std::filesystem::path(path).filename().string()}) : tr("mcp.log_not_saved", {path}));
		return {};
	});
	/* the person's choice, remembered for every later start; started, it applies at once */
	allowWatch_ = allowChanges.changed.connect([this] {
		if (ce_mcp_state() != CE_MCP_STOPPED) ce_mcp_allow_changes(allowChanges.get() ? 1 : 0);
		refresh();
	});
	session_.setMcpHost([this](uint64_t id, std::string tool, std::string arguments) { host(id, tool, arguments); });
	refresh();
}

void McpViewModel::refresh()
{
	const int32_t s = ce_mcp_state();
	const char *client = ce_mcp_client();
	state.set(s == CE_MCP_CONNECTED ? "connected" : s == CE_MCP_WAITING ? "waiting" : "stopped");
	stateText.set(s == CE_MCP_CONNECTED ? tr("mcp.connected", {client != nullptr ? client : ""})
		: s == CE_MCP_WAITING ? tr("mcp.waiting")
		: startError_.empty() ? tr("mcp.stopped") : tr("mcp.start_failed", {startError_}));
	started.set(s != CE_MCP_STOPPED);
	allowedNow.set(s != CE_MCP_STOPPED ? ce_mcp_changes_allowed() != 0 : allowChanges.get());
	const uint64_t total = ce_mcp_log_total();
	if (total == logRead_) return;
	for (uint64_t i = logRead_; i < total; i++)
	{
		const char *line = ce_mcp_log_line(i);
		/* a line the engine no longer keeps (more than 5000 since the last look) */
		if (line == nullptr || *line == '\0') continue;
		lines_.emplace_back(line);
	}
	logRead_ = total;
	while (lines_.size() > 5000) lines_.pop_front();
	std::string text;
	for (const std::string &l : lines_) text += l + "\n";
	log.set(text);
}

RowModel *McpViewModel::rowsOf(ViewModel &vm, const std::string &name)
{
	for (const auto &r : vm.rowLists())
		if (r.name == name) return r.model;
	return nullptr;
}

void McpViewModel::createProject(uint64_t id, const Json &arguments)
{
	auto refuse = [this, id](const std::string &why) { session_.mcpAnswer(id, why, true); };
	if (newProject == nullptr) return refuse(tr("mcp.window_cannot_be_asked"));
	std::string core = textOf(arguments, "core");
	while (!core.empty() && std::isspace(static_cast<unsigned char>(core.back()))) core.pop_back();
	while (!core.empty() && std::isspace(static_cast<unsigned char>(core.front()))) core.erase(core.begin());
	if (core.empty()) return refuse(tr("mcp.give_core"));
	const bool discard = flagOf(arguments, "discard_unsaved");
	if (session_.hasProject.get() && session_.unsaved.get() && !discard)
		return refuse(tr("mcp.unsaved_not_made"));
	NewProjectViewModel::AgentRequest request;
	request.core = core;
	if (const Json *slots = arguments.find("files"))
	{
		bool read = slots->kind == Json::Kind::Object;
		for (const auto &[slot, list] : slots->members)
		{
			read = read && list.kind == Json::Kind::Array;
			for (const Json &f : list.items)
			{
				read = read && f.kind == Json::Kind::String;
				request.files.emplace_back(slot, f.text);
			}
		}
		if (!read) return refuse(tr("mcp.files_shape"));
	}
	if (const Json *game = arguments.find("game"))
	{
		bool read = game->kind == Json::Kind::String || game->kind == Json::Kind::Array;
		if (game->kind == Json::Kind::String) request.games.push_back(game->text);
		for (const Json &g : game->items)
		{
			read = read && g.kind == Json::Kind::String;
			request.games.push_back(g.text);
		}
		if (!read) return refuse(tr("mcp.game_shape"));
	}
	if (const Json *given = arguments.find("settings"))
	{
		bool read = given->kind == Json::Kind::Object;
		for (const auto &[name, value] : given->members)
			read = read && value.kind != Json::Kind::Null && value.kind != Json::Kind::Array && value.kind != Json::Kind::Object;
		if (!read) return refuse(tr("mcp.settings_shape"));
		request.settings = *given;
	}
	if (const Json *ids = arguments.find("firmware"))
	{
		bool read = ids->kind == Json::Kind::Object;
		for (const auto &[fid, path] : ids->members)
		{
			read = read && path.kind == Json::Kind::String;
			request.firmware.emplace_back(fid, path.text);
		}
		if (!read) return refuse(tr("mcp.firmware_shape"));
	}
	request.firmwareFolder = textOf(arguments, "firmware_folder");
	std::error_code ec;
	if (!request.firmwareFolder.empty() && !std::filesystem::is_directory(pathOf(request.firmwareFolder), ec))
		return refuse(tr("mcp.no_folder_at", {request.firmwareFolder}));
	request.system = textOf(arguments, "system");
	newProject->completeForAgent(request, [this, id, refuse](NewProjectViewModel::AgentOutcome made) {
		if (made.missing.kind == Json::Kind::Array && !made.missing.items.empty())
		{
			Json missing = object();
			add(missing, "error", Json::string("missing_firmware"));
			add(missing, "message", Json::string(made.refusal));
			add(missing, "missing", made.missing);
			add(missing, "searched", made.searched);
			return session_.mcpAnswer(id, json::write(missing), true);
		}
		if (!made.made) return refuse(!made.refusal.empty() ? made.refusal : tr("mcp.not_made"));
		/* the project open now gave way to it; what it had not saved the agent said may go */
		session_.mcpAnswerOpened(id);
	});
}

void McpViewModel::luaTool(const std::string &tool, const Json &arguments, const std::function<void(const std::string &, bool)> &answer)
{
	auto refuse = [&](const std::string &why) { answer(why, true); };
	if (lua == nullptr) return refuse(tr("mcp.no_lua"));
	auto trimmed = [](std::string t) {
		while (!t.empty() && std::isspace(static_cast<unsigned char>(t.back()))) t.pop_back();
		while (!t.empty() && std::isspace(static_cast<unsigned char>(t.front()))) t.erase(t.begin());
		return t;
	};
	if (tool == "lua_functions")
	{
		std::string error;
		std::string listText;
		fromValue(lua->run("functionsList", {}, error).value_or(Value()), listText);
		Json all;
		if (!json::parse(listText, all, error) || all.kind != Json::Kind::Array) return refuse(tr("mcp.no_lua"));
		const Json *libraryToken = arguments.find("library");
		const Json *searchToken = arguments.find("search");
		const bool byLibrary = libraryToken != nullptr && libraryToken->kind == Json::Kind::String;
		const bool bySearch = searchToken != nullptr && searchToken->kind == Json::Kind::String;
		const int64_t fallback = 200;
		int64_t limit = 0;
		if (!number(arguments, "limit", 1, 1000, &fallback, limit)) return refuse(tr("mcp.limit_range"));
		auto lower = [](std::string t) { for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return t; };
		auto field = [](const Json &f, const char *k) { const Json *j = f.find(k); return j != nullptr ? j->text : std::string(); };
		auto flag = [](const Json &f, const char *k) { const Json *j = f.find(k); return j != nullptr && j->boolean; };
		std::vector<const Json *> matching;
		std::vector<std::string> libraries;
		for (const Json &f : all.items)
		{
			const std::string library = field(f, "library");
			if (std::find(libraries.begin(), libraries.end(), library) == libraries.end()) libraries.push_back(library);
			if (byLibrary && lower(library) != lower(libraryToken->text)) continue;
			if (bySearch && lower(library + "." + field(f, "name") + " " + field(f, "description")).find(lower(searchToken->text)) == std::string::npos) continue;
			matching.push_back(&f);
		}
		std::stable_sort(matching.begin(), matching.end(), [&](const Json *a, const Json *b) {
			const std::string la = field(*a, "library"), lb = field(*b, "library");
			return la != lb ? la < lb : field(*a, "name") < field(*b, "name");
		});
		std::sort(libraries.begin(), libraries.end());
		Json out = object(), listed, names;
		listed.kind = names.kind = Json::Kind::Array;
		for (size_t i = 0; i < matching.size() && static_cast<int64_t>(i) < limit; i++)
		{
			const Json &f = *matching[i];
			Json item = object();
			add(item, "name", Json::string(field(f, "library") + "." + field(f, "name")));
			add(item, "parameters", Json::string(field(f, "params")));
			add(item, "returns", Json::string(field(f, "returns")));
			add(item, "description", Json::string(field(f, "description")));
			if (!field(f, "example").empty()) add(item, "example", Json::string(field(f, "example")));
			if (flag(f, "unavailable")) add(item, "unavailable", Json::string(tr("mcp.unavailable_function")));
			if (flag(f, "deprecated")) add(item, "deprecated", Json::boolean_(true));
			listed.items.push_back(std::move(item));
		}
		for (const std::string &l : libraries) names.items.push_back(Json::string(l));
		add(out, "total", Json::integer(static_cast<long long>(matching.size())));
		add(out, "libraries", names);
		add(out, "functions", listed);
		return answer(json::write(out), false);
	}
	if (tool == "run_lua")
	{
		const std::string code = textOf(arguments, "code");
		if (trimmed(code).empty()) return refuse(tr("mcp.give_code"));
		if (showLuaConsole) showLuaConsole();
		std::string results, error;
		double seconds = LuaBridge::kAgentSeconds;
		if (const Json *t = arguments.find("seconds"); t != nullptr && t->kind == Json::Kind::Number) seconds = std::clamp(t->number(), 1.0, 300.0);
		const bool ran = lua->runForAgent(code, results, error, seconds);
		Json out = object();
		if (!ran)
		{
			add(out, "error", Json::string(error));
			add(out, "output", Json::string(lua->takeOutputForAgent()));
			return answer(json::write(out), true);
		}
		Json values;
		std::string why;
		if (!json::parse(results, values, why)) values.kind = Json::Kind::Array;
		add(out, "results", values);
		add(out, "output", Json::string(lua->takeOutputForAgent()));
		return answer(json::write(out), false);
	}
	if (tool == "lua_console")
	{
		const int64_t usual = 200, oldest = 0;
		int64_t since = 0, limit = 0;
		if (!number(arguments, "limit", 1, 1000, &usual, limit)) return refuse(tr("mcp.limit_range"));
		/* a line before the oldest kept is the oldest kept */
		if (!number(arguments, "since", 0, INT64_MAX, &oldest, since)) since = 0;
		return answer(lua->consoleForAgent(since, limit), false);
	}
	if (tool == "lua_scripts") return answer(lua->scriptsForAgent(), false);
	/* lua_script */
	const std::string path = textOf(arguments, "path");
	if (trimmed(path).empty()) return refuse(tr("mcp.give_path"));
	const bool stop = flagOf(arguments, "stop");
	std::error_code ec;
	if (!stop && !std::filesystem::is_regular_file(pathOf(path), ec)) return refuse(tr("agent.no_file", {path}));
	if (showLuaConsole) showLuaConsole();
	std::string failed;
	const bool running = lua->scriptForAgent(path, stop, &failed);
	Json out = object();
	add(out, "script", Json::string(path));
	add(out, "running", Json::boolean_(running));
	if (!running && !stop && !failed.empty()) add(out, "error", Json::string(failed));
	add(out, "output", Json::string(lua->takeOutputForAgent()));
	answer(json::write(out), false);
}

void McpViewModel::answerAfter(std::function<void()> then)
{
	/* after the jobs the window's last commands queued for the machine, and what they told the window */
	session_.onMachine([this, then](ce_session *) { later(then); });
}

void McpViewModel::host(uint64_t id, const std::string &tool, const std::string &argumentsJson)
{
	auto answer = [this, id](const std::string &text, bool refused) { session_.mcpAnswer(id, text, refused); };
	auto refuse = [&](const std::string &why) { answer(why, true); };
	const bool movie = session_.hasProject.get() && session_.isOpen.get();
	const int64_t length = roll_.frameCount.get();
	auto done = [&](Json facts) {
		add(facts, "movie_length", Json::integer(roll_.frameCount.get()));
		answer(json::write(facts), false);
	};
	/* before the agent's first change: a branch of the movie as it is now */
	if (tool == "keep_work")
	{
		if (!session_.hasProject.get()) return answer(tr("mcp.no_work_to_keep"), false);
		if (!session_.isOpen.get()) return refuse(tr("mcp.no_tastudio"));
		std::string error;
		Value made = branches_.run("create", {tr("mcp.branch_before_agent", {localNow("%Y-%m-%d %H:%M")})}, error).value_or(Value());
		int64_t at = -1;
		if (!fromValue(made, at)) return refuse(tr("mcp.branch_not_saved", {error}));
		return answer(tr("mcp.branch_number", {std::to_string(at + 1)}), false);
	}
	Json arguments;
	std::string error;
	if (!json::parse(argumentsJson, arguments, error) || arguments.kind != Json::Kind::Object) return refuse(tr("mcp.arguments_not_object"));

	/* the tools of the main window: they need no project, or only the project as a file */
	if (tool == "server_info") return serverInfo ? answer(serverInfo(), false) : refuse(tr("mcp.window_cannot_be_asked"));
	/* the picture as the window shows it: the engine handed it here (layers with_lua) */
	if (tool == "screenshot")
	{
		if (shownPicture) return shownPicture(id);
		return session_.mcpAnswerPicture(id, {}, {});
	}
	if (tool == "pause" || tool == "resume")
	{
		if (!session_.isOpen.get()) return refuse(tr("session.no_project_in_chimera"));
		session_.run(tool == "pause" ? "pause" : "play", {}, error);
		return session_.mcpAnswerRunState(id);
	}
	if (tool == "save_project")
	{
		if (!session_.hasProject.get()) return refuse(tr("session.no_project_in_chimera"));
		namespace fs = std::filesystem;
		const std::string own = session_.projectPath.get();
		std::string path, asked = textOf(arguments, "path");
		while (!asked.empty() && std::isspace(static_cast<unsigned char>(asked.back()))) asked.pop_back();
		while (!asked.empty() && std::isspace(static_cast<unsigned char>(asked.front()))) asked.erase(asked.begin());
		std::error_code ec;
		if (!asked.empty())
		{
			const std::string ext = ".chimeraProject";
			auto lower = [](std::string t) { for (char &c : t) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return t; };
			if (asked.size() < ext.size() || lower(asked.substr(asked.size() - ext.size())) != lower(ext)) asked += ext;
			const fs::path p(asked);
			path = fs::absolute(p.is_absolute() ? p : fs::path(projectsFolder ? projectsFolder() : std::string()) / p, ec).lexically_normal().string();
		}
		else if (!own.empty() && fs::exists(own, ec))
			path = own;
		else
			return refuse(tr("mcp.give_save_path"));
		if (!flagOf(arguments, "overwrite") && path != own && fs::exists(path, ec))
			return refuse(tr("mcp.file_exists", {path}));
		const fs::path folder = fs::path(path).parent_path();
		if (!folder.empty() && !fs::is_directory(folder, ec)) return refuse(tr("mcp.no_folder", {folder.string()}));
		session_.run("saveProject", {path}, error);
		return answerAfter([this, answer, path] {
			if (session_.projectPath.get() != path || session_.unsaved.get())
				return answer(tr("mcp.not_saved", {session_.status.get()}), true);
			Json saved = object();
			add(saved, "saved", Json::string(path));
			add(saved, "movie_length", Json::integer(roll_.frameCount.get()));
			add(saved, "unsaved_changes", Json::boolean_(session_.unsaved.get()));
			answer(json::write(saved), false);
		});
	}
	if (tool == "close_tastudio")
	{
		if (!session_.hasProject.get()) return refuse(tr("session.no_project_in_chimera"));
		const bool lost = session_.unsaved.get();
		if (lost && !flagOf(arguments, "discard_unsaved"))
			return refuse(tr("mcp.unsaved_not_closed"));
		session_.run("close", {}, error);
		return answerAfter([this, answer, lost] {
			Json closed = object();
			add(closed, "closed", Json::boolean_(!session_.hasProject.get()));
			add(closed, "machine_open", Json::boolean_(session_.isOpen.get()));
			if (lost) add(closed, "note", Json::string(tr("mcp.changes_lost")));
			answer(json::write(closed), false);
		});
	}
	if (tool == "create_project") return createProject(id, arguments);
	if (tool == "lua_functions" || tool == "run_lua" || tool == "lua_script" || tool == "lua_console" || tool == "lua_scripts")
		return luaTool(tool, arguments, answer);
	if (!movie) return refuse(tr("session.no_project_in_chimera"));

	if (tool == "list_markers")
	{
		Json list;
		list.kind = Json::Kind::Array;
		if (RowModel *rows = rowsOf(roll_, "markers"))
			for (int64_t i = 0; i < rows->rowCount(); i++)
			{
				int64_t frame = 0;
				std::string text;
				bool permanent = false;
				fromValue(rows->cell(i, 0), frame);
				fromValue(rows->cell(i, 1), text);
				fromValue(rows->cell(i, 3), permanent);
				Json m = object();
				add(m, "frame", Json::integer(frame));
				add(m, "text", Json::string(text));
				/* every movie has three of its own (the run's start, its last input, its end) */
				add(m, "placed_by_user", Json::boolean_(!permanent));
				list.items.push_back(std::move(m));
			}
		Json out = object();
		add(out, "markers", std::move(list));
		return answer(json::write(out), false);
	}
	if (tool == "list_branches")
	{
		Json list;
		list.kind = Json::Kind::Array;
		if (RowModel *rows = rowsOf(branches_, "branches"))
			for (int64_t i = 0; i < rows->rowCount(); i++)
			{
				std::string name, time;
				int64_t frame = 0;
				fromValue(rows->cell(i, 0), name);
				fromValue(rows->cell(i, 1), frame);
				fromValue(rows->cell(i, 2), time);
				Json b = object();
				/* numbered as the Branches box numbers them, from 1 */
				add(b, "number", Json::integer(i + 1));
				add(b, "frame", Json::integer(frame));
				add(b, "taken", Json::string(time));
				add(b, "note", Json::string(name));
				add(b, "current", Json::boolean_(branches_.current.get() == i));
				list.items.push_back(std::move(b));
			}
		Json out = object();
		add(out, "branches", std::move(list));
		return answer(json::write(out), false);
	}
	/* the engine has checked these against the movie and the controls: a plain step, with its name for the undo history */
	const std::string label = textOf(arguments, "label").empty() ? tr("mcp.agent") : textOf(arguments, "label");
	if (tool == "set_input")
	{
		int64_t frame = 0;
		const Json *entries = arguments.find("entries");
		bool good = number(arguments, "frame", 0, INT32_MAX - kMostFrames, nullptr, frame) && entries != nullptr &&
			entries->kind == Json::Kind::Array && !entries->items.empty();
		std::string lines;
		if (good)
			for (const Json &e : entries->items)
			{
				if (e.kind != Json::Kind::String) good = false;
				lines += (lines.empty() && &e == &entries->items.front() ? "" : "\n") + e.text;
			}
		if (!good || !roll_.run("agentLines", {frame, lines, label}, error)) return refuse(tr("piano_roll.unknown_step"));
		Json facts = object();
		add(facts, "frame", Json::integer(frame));
		add(facts, "count", Json::integer(static_cast<int64_t>(entries->items.size())));
		return done(std::move(facts));
	}
	if (tool == "insert_frames")
	{
		int64_t frame = 0, count = 0;
		if (!number(arguments, "frame", 0, length, nullptr, frame) || !number(arguments, "count", 1, kMostFrames, nullptr, count) ||
			!roll_.run("agentInsert", {frame, count, label}, error))
			return refuse(tr("piano_roll.unknown_step"));
		Json facts = object();
		add(facts, "frame", Json::integer(frame));
		add(facts, "inserted", Json::integer(count));
		return done(std::move(facts));
	}
	if (tool == "delete_frames")
	{
		int64_t frame = 0, count = 0;
		if (!number(arguments, "frame", 0, std::max<int64_t>(0, length - 1), nullptr, frame) ||
			!number(arguments, "count", 1, std::max<int64_t>(1, length - frame), nullptr, count) || !roll_.run("agentDelete", {frame, count, label}, error))
			return refuse(tr("piano_roll.unknown_step"));
		Json facts = object();
		add(facts, "frame", Json::integer(frame));
		add(facts, "deleted", Json::integer(count));
		return done(std::move(facts));
	}
	if (tool == "undo")
	{
		if (!roll_.canUndo.get()) return refuse(tr("mcp.nothing_to_undo"));
		roll_.run("undo", {}, error);
		Json facts = object();
		add(facts, "undone", Json::boolean_(true));
		return done(std::move(facts));
	}
	if (tool == "add_marker")
	{
		int64_t frame = 0;
		if (!number(arguments, "frame", 0, INT32_MAX - 1, nullptr, frame)) return refuse(tr("mcp.frame_number"));
		const std::string text = textOf(arguments, "text");
		roll_.run("addMarker", {frame, text}, error);
		Json facts = object();
		add(facts, "frame", Json::integer(frame));
		add(facts, "text", Json::string(text));
		return answer(json::write(facts), false);
	}
	if (tool == "seek")
	{
		int64_t frame = 0;
		if (!number(arguments, "frame", 0, length, nullptr, frame))
			return refuse(tr("mcp.frame_range", {std::to_string(length)}));
		if (session_.frame.get() == frame && !session_.running.get())
		{
			Json at = object();
			add(at, "frame", Json::integer(frame));
			return answer(json::write(at), false);
		}
		return session_.mcpSeek(id, frame);
	}
	if (tool == "diff_memory")
	{
		int64_t frameA = 0, frameB = 0;
		if (!number(arguments, "frame_a", 0, length, nullptr, frameA) || !number(arguments, "frame_b", 0, length, nullptr, frameB))
			return refuse(tr("mcp.frame_range", {std::to_string(length)}));
		return session_.mcpDiffMemory(id, frameA, frameB, argumentsJson);
	}
	if (tool == "search_steps")
	{
		/* each step's frame here; the rest of it is the engine's search_filter's to read */
		const Json *list = arguments.find("steps");
		std::vector<std::pair<int64_t, std::string>> steps;
		for (const Json &step : list != nullptr && list->kind == Json::Kind::Array ? list->items : std::vector<Json>{})
		{
			int64_t frame = 0;
			if (!number(step, "frame", 0, length, nullptr, frame)) return refuse(tr("mcp.frame_range", {std::to_string(length)}));
			Json filter = object();
			for (const auto &[name, value] : step.members)
				if (name != "frame") filter.members.emplace_back(name, value);
			steps.emplace_back(frame, json::write(filter));
		}
		if (steps.empty()) return refuse(tr("piano_roll.unknown_step"));
		return session_.mcpSearchSteps(id, std::move(steps));
	}
	if (tool == "advance_until")
	{
		const int64_t usual = 600;
		int64_t most = 0;
		if (!number(arguments, "max_frames", 1, kMostFrames, &usual, most))
			return refuse(tr("mcp.max_frames_range", {std::to_string(kMostFrames)}));
		return session_.mcpPlayUntil(id, argumentsJson, most);
	}
	if (tool == "capture_series")
	{
		const Json *list = arguments.find("frames");
		std::vector<int64_t> frames;
		if (list != nullptr && list->kind == Json::Kind::Array)
			for (const Json &f : list->items)
			{
				if (f.kind != Json::Kind::Number) { frames.clear(); break; }
				frames.push_back(static_cast<int64_t>(f.number()));
			}
		if (frames.empty()) return refuse(tr("piano_roll.unknown_step"));
		/* frames past the movie's end added first, empty and in one step, so that playing reaches every one */
		const int64_t last = *std::max_element(frames.begin(), frames.end());
		if (last > length) roll_.run("agentInsert", {length, last - length, label}, error);
		return session_.mcpPictures(id, frames);
	}
	if (tool == "save_branch")
	{
		Value made = branches_.run("create", {textOf(arguments, "note")}, error).value_or(Value());
		int64_t at = -1;
		if (!fromValue(made, at)) return refuse(tr("mcp.branch_not_saved", {error}));
		Json out = object();
		add(out, "number", Json::integer(at + 1));
		return answer(json::write(out), false);
	}
	if (tool == "load_branch")
	{
		const int64_t count = branches_.count.get();
		int64_t n = 0;
		if (!number(arguments, "number", 1, count, nullptr, n))
			return refuse(count == 0 ? tr("mcp.no_branches") : tr("mcp.branch_range", {std::to_string(count)}));
		if (!branches_.run("load", {n - 1}, error)) return refuse(tr("mcp.branch_not_loaded", {error}));
		return answerAfter([this, answer, n] {
			Json out = object();
			add(out, "number", Json::integer(n));
			add(out, "frame", Json::integer(session_.frame.get()));
			add(out, "movie_length", Json::integer(roll_.frameCount.get()));
			answer(json::write(out), false);
		});
	}
	refuse(tr("mcp.no_tool", {tool}));
}

} // namespace chimera::vm
