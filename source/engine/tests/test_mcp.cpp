/* test_mcp.cpp - the MCP server's parts that need no machine
 * (docs/mcp-server.md): the local pipe, the protocol an agent's program
 * speaks, and the way a call travels from chimera-mcp to a running Chimera
 * and back. The tools that read a machine are tested with the synthetic core
 * by tests/synth/run-witness.sh.
 */

#include "../source/mcp.hpp"
#include "chimera/engine.h"
#include "../../../extern/cjson/cJSON.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <thread>

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace
{

using chimera::PipeEvent;

/* Waits until the pipe gives something other than Nothing, or two seconds. */
template <typename Pipe>
PipeEvent next(Pipe &pipe, std::string &line)
{
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (std::chrono::steady_clock::now() < until)
	{
		const PipeEvent event = pipe.wait(20, line);
		if (event != PipeEvent::Nothing) return event;
	}
	return PipeEvent::Nothing;
}

bool has(const std::string &text, const char *part) { return text.find(part) != std::string::npos; }

void thePipe()
{
	std::string error, line;
	chimera::PipeClient client;
	/* nobody serves yet: a client is told so */
	assert(!client.connect(error));
	assert(has(error, "not started"));

	chimera::PipeServer server;
	assert(server.open(error));
	/* one server at a time */
	{
		chimera::PipeServer second;
		std::string secondError;
		assert(!second.open(secondError));
		assert(has(secondError, "already has its MCP server started"));
	}

	/* nothing happens while nobody is there */
	assert(server.wait(30, line) == PipeEvent::Nothing);

	assert(client.connect(error));
	assert(client.send("first line"));
	assert(next(server, line) == PipeEvent::Connected);
	assert(next(server, line) == PipeEvent::Line && line == "first line");
	/* one client at a time */
	{
		chimera::PipeClient second;
		std::string secondError;
		assert(!second.connect(secondError));
		assert(has(secondError, "another agent"));
	}
	/* both ways, several lines in one go, and a long one */
	assert(server.send("answer one"));
	assert(server.send("answer two"));
	assert(next(client, line) == PipeEvent::Line && line == "answer one");
	assert(next(client, line) == PipeEvent::Line && line == "answer two");
	const std::string big(200000, 'x');
	std::thread writer([&] { assert(client.send(big)); });
	std::string got;
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (got.empty() && std::chrono::steady_clock::now() < until)
	{
		if (server.wait(20, line) == PipeEvent::Line) got = line;
	}
	writer.join();
	assert(got == big);

	/* the client leaves: the server is told, and takes the next one */
	client.close();
	assert(next(server, line) == PipeEvent::Disconnected);
	assert(!server.send("to nobody"));
	assert(client.connect(error));
	assert(client.send("again"));
	assert(next(server, line) == PipeEvent::Connected);
	assert(next(server, line) == PipeEvent::Line && line == "again");
	assert(server.send("welcome back"));
	assert(next(client, line) == PipeEvent::Line && line == "welcome back");

	/* the server leaves: the client is told, and cannot connect again */
	server.close();
	assert(next(client, line) == PipeEvent::Disconnected);
	client.close();
	assert(!client.connect(error));
	assert(has(error, "not started"));

#ifndef _WIN32
	/* Each end picks its folder from its own environment. A server started
	 * without XDG_RUNTIME_DIR, and a client given one (or the other way round),
	 * still meet. */
	{
		const char *had = std::getenv("XDG_RUNTIME_DIR");
		const std::string kept = had != nullptr ? had : "";
		unsetenv("XDG_RUNTIME_DIR");
		chimera::PipeServer elsewhere;
		assert(elsewhere.open(error));
		setenv("XDG_RUNTIME_DIR", "/nonexistent-runtime-dir", 1);
		chimera::PipeClient looking;
		assert(looking.connect(error));
		assert(looking.send("found"));
		assert(next(elsewhere, line) == PipeEvent::Connected);
		assert(next(elsewhere, line) == PipeEvent::Line && line == "found");
		looking.close();
		elsewhere.close();
		if (had != nullptr) setenv("XDG_RUNTIME_DIR", kept.c_str(), 1);
		else unsetenv("XDG_RUNTIME_DIR");
	}
#endif
}

/* The reply to one line, parsed. The caller deletes it. */
cJSON *reply(const std::string &line, const chimera::McpExecutor &execute, std::string *client = nullptr)
{
	const std::string out = chimera::mcpHandleLine(line, execute, client);
	assert(!out.empty());
	assert(out.find('\n') == std::string::npos); /* one line */
	cJSON *parsed = cJSON_Parse(out.c_str());
	assert(parsed != nullptr);
	assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(parsed, "jsonrpc")->valuestring, "2.0") == 0);
	return parsed;
}

void theProtocol()
{
	std::string seenTool, seenArguments;
	const chimera::McpExecutor execute = [&](const std::string &tool, const std::string &arguments)
	{
		seenTool = tool;
		seenArguments = arguments;
		chimera::McpResult result;
		result.content = chimera::mcpTextContent("ran " + tool);
		return result;
	};

	/* the introduction: the version asked for is given when it is known, the
	 * agent's program is named, and the server says it has tools */
	std::string client;
	cJSON *r = reply("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2024-11-05\","
		"\"capabilities\":{},\"clientInfo\":{\"name\":\"Test Agent\",\"version\":\"3.1\"}}}", execute, &client);
	const cJSON *result = cJSON_GetObjectItemCaseSensitive(r, "result");
	assert(cJSON_GetObjectItemCaseSensitive(r, "id")->valuedouble == 1);
	assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(result, "protocolVersion")->valuestring, "2024-11-05") == 0);
	assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(result, "capabilities"), "tools") != nullptr);
	assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(result, "serverInfo"), "name")->valuestring, "chimera") == 0);
	assert(client == "Test Agent 3.1");
	cJSON_Delete(r);
	/* a version this server does not know gets the newest it does */
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":\"a\",\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"1999-01-01\"}}", execute);
	assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "result"), "protocolVersion")->valuestring, "2025-06-18") == 0);
	/* an id that is text comes back as text */
	assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(r, "id")->valuestring, "a") == 0);
	cJSON_Delete(r);

	/* a notification is not answered */
	assert(chimera::mcpHandleLine("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}", execute, nullptr).empty());
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"ping\"}", execute);
	assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(r, "result")));
	cJSON_Delete(r);

	/* the list: every tool with a name, a description and a schema that is
	 * JSON, and the listed tools are the ones the table has */
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/list\"}", execute);
	const cJSON *tools = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "result"), "tools");
	size_t count = 0;
	const chimera::McpTool *table = chimera::mcpTools(count);
	/* listed here: every tool but those for where there is no window, in the table's order */
	std::vector<size_t> windowed, windowless;
	for (size_t i = 0; i < count; i++)
	{
		if (!table[i].local) windowed.push_back(i);
		if (!table[i].window) windowless.push_back(i);
		assert(!(table[i].local && table[i].window));
	}
	assert(!windowed.empty() && windowed.size() < count && windowless.size() < count);
	assert(cJSON_GetArraySize(tools) == static_cast<int>(windowed.size()));
	for (size_t n = 0; n < windowed.size(); n++)
	{
		const size_t i = windowed[n];
		const cJSON *tool = cJSON_GetArrayItem(tools, static_cast<int>(n));
		assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(tool, "name")->valuestring, table[i].name) == 0);
		assert(std::strlen(cJSON_GetObjectItemCaseSensitive(tool, "description")->valuestring) > 20);
		const cJSON *schema = cJSON_GetObjectItemCaseSensitive(tool, "inputSchema");
		assert(cJSON_IsObject(schema));
		assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(schema, "type")->valuestring, "object") == 0);
		const cJSON *readOnly = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(tool, "annotations"), "readOnlyHint");
		assert(cJSON_IsBool(readOnly) && (cJSON_IsTrue(readOnly) != 0) == !table[i].changes);
		assert(chimera::mcpFindTool(table[i].name) == &table[i]);
	}
	cJSON_Delete(r);

	/* a tool for where there is no window does not exist here, and nothing is run */
	seenTool.clear();
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":30,\"method\":\"tools/call\",\"params\":{\"name\":\"open_game\",\"arguments\":{}}}", execute);
	assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "code")->valuedouble == -32602);
	assert(seenTool.empty());
	cJSON_Delete(r);
	/* with no window, every tool but the window's is listed (each with a schema that is JSON) and reaches the executor */
	chimera::mcpSetLocal(true);
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":31,\"method\":\"tools/list\"}", execute);
	const cJSON *all = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "result"), "tools");
	assert(cJSON_GetArraySize(all) == static_cast<int>(windowless.size()));
	for (size_t n = 0; n < windowless.size(); n++)
	{
		const cJSON *tool = cJSON_GetArrayItem(all, static_cast<int>(n));
		assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(tool, "name")->valuestring, table[windowless[n]].name) == 0);
		assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(tool, "inputSchema")));
	}
	cJSON_Delete(r);
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":32,\"method\":\"tools/call\",\"params\":{\"name\":\"open_game\",\"arguments\":{}}}", execute);
	assert(seenTool == "open_game");
	cJSON_Delete(r);
	/* and a tool for a window does not exist there (issue 355: resume, close_tastudio) */
	seenTool.clear();
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":33,\"method\":\"tools/call\",\"params\":{\"name\":\"resume\",\"arguments\":{}}}", execute);
	assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "code")->valuedouble == -32602);
	assert(seenTool.empty());
	cJSON_Delete(r);
	chimera::mcpSetLocal(false);

	/* a call reaches the executor with its arguments, and its answer comes back */
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"read_memory\","
		"\"arguments\":{\"domain\":\"RAM\",\"address\":16}}}", execute);
	assert(seenTool == "read_memory");
	assert(has(seenArguments, "\"domain\":\"RAM\"") && has(seenArguments, "\"address\":16"));
	result = cJSON_GetObjectItemCaseSensitive(r, "result");
	assert(cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(result, "isError")));
	const cJSON *first = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(result, "content"), 0);
	assert(std::strcmp(cJSON_GetObjectItemCaseSensitive(first, "text")->valuestring, "ran read_memory") == 0);
	cJSON_Delete(r);
	/* no arguments is an empty object */
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"tools/call\",\"params\":{\"name\":\"status\"}}", execute);
	assert(seenTool == "status" && seenArguments == "{}");
	cJSON_Delete(r);

	/* what is wrong is said, and never reaches the executor */
	seenTool.clear();
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"tools/call\",\"params\":{\"name\":\"no_such_tool\"}}", execute);
	assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "code")->valuedouble == -32602);
	cJSON_Delete(r);
	r = reply("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"resources/list\"}", execute);
	assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "code")->valuedouble == -32601);
	cJSON_Delete(r);
	r = reply("this is not JSON", execute);
	assert(cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(r, "error"), "code")->valuedouble == -32700);
	cJSON_Delete(r);
	assert(seenTool.empty());
}

/* The first text of a result, which for the engine's tools is JSON. */
std::string textOf(const chimera::McpResult &result)
{
	cJSON *content = cJSON_Parse(result.content.c_str());
	const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
	std::string out = cJSON_IsString(text) ? text->valuestring : "";
	cJSON_Delete(content);
	return out;
}

/* Calls a tool from another thread, as chimera-mcp would, while this thread
 * is the running Chimera: it services the server until the call has come
 * back. `answerHost`, when not null, is the answer given to a call the engine
 * hands to the frontend. */
chimera::McpResult callThroughServer(chimera::McpLink &link, const char *tool, const char *arguments,
	const char *answerHost = nullptr, std::string *hostTool = nullptr, bool refuseToKeep = false)
{
	chimera::McpResult result;
	std::atomic<bool> done{ false };
	std::thread agent([&] { result = link.call(tool, arguments); done = true; });
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
	while (!done.load() && std::chrono::steady_clock::now() < until)
	{
		uint64_t id = 0;
		const char *name = nullptr, *args = nullptr;
		if (ce_mcp_pending() != 0 && ce_mcp_service(nullptr, nullptr, -1, "{\"project\":\"the test\"}", &id, &name, &args) != 0)
		{
			if (hostTool != nullptr) *hostTool += std::string(hostTool->empty() ? "" : " | ") + name + " " + args;
			if (std::strcmp(name, "keep_work") == 0) ce_mcp_answer(nullptr, id, refuseToKeep ? "the disk is full" : "branch 3", refuseToKeep ? 1 : 0);
			else ce_mcp_answer(nullptr, id, answerHost != nullptr ? answerHost : "", 0);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	agent.join();
	assert(done.load());
	return result;
}

/* The tools that change something: refused until a person allows changes;
 * before the first of them the frontend is asked to keep the work as it was,
 * and only a good answer lets the change through. Called with the server
 * started and changes allowed. */
void theChanges(chimera::McpLink &link)
{
	const char *error = nullptr;
	ce_mcp_stop();
	assert(ce_mcp_start(&error) == 1);

	/* not allowed: the call is refused with the way to allow it, and the
	 * frontend is never asked anything */
	std::string hostTool;
	chimera::McpResult result = callThroughServer(link, "add_marker", "{\"frame\":3,\"text\":\"x\"}", "{}", &hostTool);
	assert(result.isError && has(textOf(result), "Allow the agent to make changes"));
	assert(hostTool.empty());
	result = callThroughServer(link, "write_memory", "{\"domain\":\"RAM\",\"address\":0,\"values\":[1]}", "{}", &hostTool);
	assert(result.isError && has(textOf(result), "Allow the agent to make changes"));
	assert(hostTool.empty());

	/* allowed: the first change is preceded by "keep the work", once */
	ce_mcp_allow_changes(1);
	result = callThroughServer(link, "add_marker", "{\"frame\":3,\"text\":\"x\"}", "{\"frame\":3}", &hostTool);
	assert(hostTool == "keep_work {} | add_marker {\"frame\":3,\"text\":\"x\"}");
	assert(!result.isError && textOf(result) == "{\"frame\":3}");
	hostTool.clear();
	result = callThroughServer(link, "undo", "{}", "{\"undone\":true}", &hostTool);
	assert(hostTool == "undo {}");
	assert(!result.isError);
	/* looking never asks for the work to be kept */
	hostTool.clear();
	result = callThroughServer(link, "list_markers", "{}", "{\"markers\":[]}", &hostTool);
	assert(hostTool == "list_markers {}");
	/* a change the engine makes itself goes the same way; with no machine it
	 * is then refused for that reason */
	result = callThroughServer(link, "write_memory", "{\"domain\":\"RAM\",\"address\":0,\"values\":[1]}");
	assert(result.isError && has(textOf(result), "No project is open"));
	bool sawKept = false;
	for (uint64_t i = 0; i < ce_mcp_log_total(); i++)
	{
		if (has(ce_mcp_log_line(i), "the work as it was has been kept: branch 3")) sawKept = true;
	}
	assert(sawKept);

	/* a new start asks again; when the work cannot be kept, nothing is
	 * changed, now or for the rest of that start */
	ce_mcp_stop();
	assert(ce_mcp_start(&error) == 1);
	ce_mcp_allow_changes(1);
	hostTool.clear();
	result = callThroughServer(link, "add_marker", "{\"frame\":3}", "{}", &hostTool, true);
	assert(hostTool == "keep_work {}");
	assert(result.isError && has(textOf(result), "Nothing was changed") && has(textOf(result), "the disk is full"));
	hostTool.clear();
	result = callThroughServer(link, "set_input", "{\"frame\":0,\"entries\":[\"|.|\"]}", "{}", &hostTool, true);
	assert(hostTool.empty());
	assert(result.isError && has(textOf(result), "Nothing was changed"));
	/* looking still works */
	result = callThroughServer(link, "status", "{}");
	assert(!result.isError);
	ce_mcp_allow_changes(1);
}

void theServer()
{
	chimera::McpLink link;
	link.setClient("Test Agent 3.1");

	/* not started: the call says how to start it, and nothing hangs */
	chimera::McpResult result = link.call("status", "{}");
	assert(result.isError && has(textOf(result), "not started") && has(textOf(result), "Tools > MCP Server..."));
	assert(ce_mcp_state() == CE_MCP_STOPPED);
	assert(ce_mcp_pending() == 0);

	const char *error = nullptr;
	assert(ce_mcp_start(&error) == 1);
	assert(ce_mcp_start(&error) == 1); /* starting what is started is fine */
	assert(ce_mcp_state() == CE_MCP_WAITING);
	assert(std::strlen(ce_mcp_endpoint()) > 0);
	assert(ce_mcp_changes_allowed() == 0);

	/* a tool the engine answers itself, with no machine open: status says so
	 * and carries what the frontend told it */
	result = callThroughServer(link, "status", "{}");
	assert(!result.isError);
	assert(has(textOf(result), "\"machine_open\":false"));
	assert(has(textOf(result), "\"project\":\"the test\""));
	assert(has(textOf(result), "\"changes_allowed\":false"));
	assert(ce_mcp_state() == CE_MCP_CONNECTED);
	assert(std::strcmp(ce_mcp_client(), "Test Agent 3.1") == 0);

	/* a tool that needs a machine is refused with a sentence, not a crash */
	result = callThroughServer(link, "read_memory", "{\"domain\":\"RAM\",\"address\":0}");
	assert(result.isError && has(textOf(result), "No project is open"));

	/* the same for a search: there is no memory to search */
	result = callThroughServer(link, "search_start", "{\"domain\":\"RAM\"}");
	assert(result.isError && has(textOf(result), "No project is open"));
	result = callThroughServer(link, "search_filter", "{\"op\":\"equal\"}");
	assert(result.isError && has(textOf(result), "No project is open"));

	/* a tool only the frontend can run is handed to it, and its answer is
	 * what the agent gets */
	std::string hostTool;
	result = callThroughServer(link, "list_markers", "{}", "{\"markers\":[]}", &hostTool);
	assert(hostTool == "list_markers {}");
	assert(!result.isError && textOf(result) == "{\"markers\":[]}");

	/* allowing changes is remembered until the next start, and logged */
	ce_mcp_allow_changes(1);
	assert(ce_mcp_changes_allowed() == 1);
	result = callThroughServer(link, "status", "{}");
	assert(has(textOf(result), "\"changes_allowed\":true"));

	/* the log has a line for the start, the connection and each call */
	const uint64_t lines = ce_mcp_log_total();
	assert(lines >= 7);
	bool sawCall = false, sawRefusal = false, sawStart = false;
	for (uint64_t i = 0; i < lines; i++)
	{
		const std::string text = ce_mcp_log_line(i);
		assert(!text.empty());
		if (has(text, "list_markers {}") && has(text, "ms)")) sawCall = true;
		if (has(text, "read_memory") && has(text, "REFUSED")) sawRefusal = true;
		if (has(text, "the server was started")) sawStart = true;
	}
	assert(sawCall && sawRefusal && sawStart);
	assert(std::strlen(ce_mcp_log_line(lines + 5)) == 0);

	theChanges(link);

	/* a server given a name serves on a pipe of that name. (The name is the
	 * process's, so the link in this same process follows it; that two
	 * programs with different names do not meet is what a pipe's name is.) */
	ce_mcp_stop();
	const std::string unnamed = ce_mcp_endpoint();
	ce_mcp_name("else where");
	assert(ce_mcp_start(&error) == 1);
	assert(std::string(ce_mcp_endpoint()) == unnamed + "-else_where");
	result = callThroughServer(link, "status", "{}");
	assert(!result.isError);
	ce_mcp_stop();
	ce_mcp_name(nullptr);
	assert(ce_mcp_start(&error) == 1);
	assert(std::string(ce_mcp_endpoint()) == unnamed);

	/* stopped: the agent is told, and a new start begins with changes off */
	ce_mcp_stop();
	assert(ce_mcp_state() == CE_MCP_STOPPED);
	result = link.call("status", "{}");
	assert(result.isError && has(textOf(result), "Tools > MCP Server..."));
	assert(ce_mcp_start(&error) == 1);
	assert(ce_mcp_changes_allowed() == 0);
	/* the same link finds the new server by itself */
	result = callThroughServer(link, "status", "{}");
	assert(!result.isError && has(textOf(result), "\"changes_allowed\":false"));
	ce_mcp_stop();
}

/* chimera-mcp and the window say their builds to each other (issue 571): an
 * agent whose chimera-mcp is from another build is told so in every answer,
 * with the tools one side has and the other not, and which chimera-mcp to
 * use instead. */
void theBuilds()
{
	/* from this build: no warning, and the window knows its build */
	ce_mcp_program("/opt/chimera/bin/chimera-mcp");
	const char *error = nullptr;
	assert(ce_mcp_start(&error) == 1);
	{
		chimera::McpLink link;
		link.setClient("Test Agent 3.1");
		link.setProgramPath("/home/someone/chimera/bin/chimera-mcp");
		chimera::McpResult result = callThroughServer(link, "status", "{}");
		assert(!result.isError && !has(result.content, "WARNING"));
		assert(std::string(ce_mcp_client_build()) == CE_BUILD_COMMIT);
		/* Tools > MCP Server... says where it is and that it is this build (issue 571) */
		assert(std::string(ce_mcp_client_program()) == "/home/someone/chimera/bin/chimera-mcp" && ce_mcp_client_same_build() == 1);
	}
	ce_mcp_stop();

	/* a chimera-mcp from before builds were said: its greeting names none,
	 * and every answer carries the warning, the window's own chimera-mcp in it */
	assert(ce_mcp_start(&error) == 1);
	chimera::PipeClient old;
	std::string connectError, line;
	assert(old.connect(connectError));
	assert(old.send("{\"hello\":\"Old Agent 1.0\"}"));
	std::string greeting;
	const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (greeting.empty() && std::chrono::steady_clock::now() < until)
		if (old.wait(20, line) == PipeEvent::Line) greeting = line;
	assert(has(greeting, "\"build\":\"" CE_BUILD_COMMIT "\"") && has(greeting, "\"status\""));
	assert(std::string(ce_mcp_client_build()) == "older" && ce_mcp_client_same_build() == 0 && std::string(ce_mcp_client_program()).empty());
	assert(old.send("{\"id\":1,\"tool\":\"status\",\"arguments\":{}}"));
	std::string answer;
	while (answer.empty() && std::chrono::steady_clock::now() < until)
	{
		uint64_t id = 0;
		const char *name = nullptr, *args = nullptr;
		if (ce_mcp_pending() != 0) ce_mcp_service(nullptr, nullptr, -1, "{}", &id, &name, &args);
		if (old.wait(5, line) == PipeEvent::Line && has(line, "\"id\":1") && has(line, "\"content\"")) answer = line;
	}
	std::printf("an old chimera-mcp is told: %s\n", answer.c_str());
	std::fflush(stdout);
	assert(has(answer, "machine_open") && has(answer, "WARNING") && has(answer, "older than 2026-11-12")
		&& has(answer, "/opt/chimera/bin/chimera-mcp"));
	old.close();
	ce_mcp_stop();

	/* chimera-mcp's own comparison: the tools each side lacks are named */
	const std::vector<std::string> windowTools = { "status", "a_new_tool" };
	const std::string said = chimera::mcpBuildMismatch(false, "0123456789ab", &windowTools, "C:\\Chimera\\bin\\chimera-mcp.exe");
	assert(has(said, "0123456789ab") && has(said, "Chimera has tools this chimera-mcp does not offer: a_new_tool")
		&& has(said, "This chimera-mcp offers tools Chimera does not have: controls") && has(said, "C:\\Chimera\\bin\\chimera-mcp.exe"));
	assert(chimera::mcpBuildMismatch(false, CE_BUILD_COMMIT, &windowTools, "").empty());
	ce_mcp_program(nullptr);
}

/* chimera-mcp lists the window's tools while one is reached, says a window of
 * another build at initialize, tells the agent's program the list changed,
 * and passes on a call to a tool only the window has (issue 571) */
void theListing()
{
	const char *error = nullptr;
	std::string client, said;
	{
		/* this build's window: no warning, its list is this program's, nothing changed */
		chimera::McpLink link;
		chimera::mcpSetWindowListing([&link] { return link.windowTools(); }, [&link] { return link.greet(); });
		const chimera::McpExecutor execute = [&](const std::string &tool, const std::string &arguments) { return link.call(tool, arguments); };
		assert(ce_mcp_start(&error) == 1);
		said = chimera::mcpHandleLine(
			"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}", execute, &client);
		assert(has(said, "\"listChanged\":true") && !has(said, "WARNING"));
		assert(has(link.windowTools(), "\"trace_memory\"") && !link.takeToolsChanged());
		chimera::mcpSetWindowListing(nullptr, nullptr);
		ce_mcp_stop();
	}

	/* a window of another build, with a tool this program has not */
	chimera::McpLink other;
	chimera::mcpSetWindowListing([&other] { return other.windowTools(); }, [&other] { return other.greet(); });
	const chimera::McpExecutor viaOther = [&](const std::string &tool, const std::string &arguments) { return other.call(tool, arguments); };
	chimera::PipeServer window;
	std::string openError;
	assert(window.open(openError));
	std::atomic<bool> done{ false };
	std::thread answering([&] {
		std::string line;
		while (!done.load())
		{
			if (window.wait(20, line) != PipeEvent::Line) continue;
			if (has(line, "\"hello\""))
				window.send("{\"hello\":\"chimera\",\"build\":\"fakebuild0001\",\"tools\":[\"status\",\"a_new_tool\"],"
					"\"definitions\":[{\"name\":\"status\",\"description\":\"s\",\"inputSchema\":{\"type\":\"object\"}},"
					"{\"name\":\"a_new_tool\",\"description\":\"new\",\"inputSchema\":{\"type\":\"object\"}}]}");
			else if (has(line, "\"a_new_tool\""))
			{
				const size_t at = line.find("\"id\":");
				const std::string id = line.substr(at + 5, line.find_first_of(",}", at) - at - 5);
				window.send("{\"id\":" + id + ",\"content\":[{\"type\":\"text\",\"text\":\"ran the new tool\"}],\"isError\":false}");
			}
		}
	});
	said = chimera::mcpHandleLine(
		"{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}", viaOther, &client);
	std::fprintf(stderr, "a window of another build, at initialize: %s\n", said.c_str());
	assert(has(said, "fakebuild0001") && has(said, "a_new_tool"));
	assert(other.takeToolsChanged() && !other.takeToolsChanged());
	said = chimera::mcpHandleLine("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}", viaOther, &client);
	assert(has(said, "\"a_new_tool\"") && !has(said, "\"trace_memory\""));
	said = chimera::mcpHandleLine(
		"{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"a_new_tool\",\"arguments\":{}}}", viaOther, &client);
	assert(has(said, "ran the new tool"));
	done.store(true);
	answering.join();
	window.close();
	chimera::mcpSetWindowListing(nullptr, nullptr);
}

} // namespace

/* Replies are valid UTF-8 (2026-10-18): valid text passes untouched, and each
 * byte that does not begin or continue a sequence becomes U+FFFD. */
static void repliesAreValidUtf8()
{
	const std::string kept = "plain, caf\xC3\xA9, \xE2\x82\xAC, \xF0\x9F\x8E\xAE";
	assert(chimera::mcpValidUtf8(kept) == kept);
	const std::string fffd = "\xEF\xBF\xBD";
	assert(chimera::mcpValidUtf8("a\xFF" "b") == "a" + fffd + "b");
	assert(chimera::mcpValidUtf8("cut \xC3") == "cut " + fffd);                 /* a character cut short */
	assert(chimera::mcpValidUtf8("\xC0\xAF") == fffd + fffd);                 /* overlong */
	assert(chimera::mcpValidUtf8("\xED\xA0\x80") == fffd + fffd + fffd);     /* a surrogate */
	assert(chimera::mcpValidUtf8("\xF4\x90\x80\x80") == fffd + fffd + fffd + fffd);   /* past U+10FFFF */
}

int main()
{
	repliesAreValidUtf8();
#ifndef _WIN32
	/* An assertion that fails ends the test through abort(), and a core dump
	 * is written first. On a machine that hands core dumps to another program
	 * (WSL does) that took over a minute, during which the failed test looked
	 * like a test that hangs. No dump is wanted from a test. */
	const struct rlimit none = { 0, 0 };
	setrlimit(RLIMIT_CORE, &none);
#endif
	/* a pipe of this test's own, so a Chimera the same user is running is
	 * neither disturbed nor in the way */
	chimera::pipeNameSuffixForTests("-test-" + std::to_string(static_cast<long>(getpid())));
	thePipe();
	theProtocol();
	theServer();
	theBuilds();
	theListing();
	std::printf("test_mcp: ok\n");
	return 0;
}
