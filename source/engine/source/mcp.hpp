/* mcp.hpp - internal: the MCP server's shared parts (docs/mcp-server.md). Not
 * part of the ABI; the frontend's calls are the ce_mcp_* ones in engine.h.
 *
 * MCP (Model Context Protocol) is how an AI agent's program uses another
 * program: the other program lists tools, and the agent calls them. Three
 * pieces are here:
 *
 *   - the list of tools, the same for every program that offers them;
 *   - the protocol: one line of JSON in from the agent's program, one line
 *     out. chimera-mcp, the program the agent starts, runs this;
 *   - the link from chimera-mcp to a running Chimera, over the local pipe.
 *
 * The other end of that link - the part that lives in the running Chimera and
 * runs the tools on its machine - is behind ce_mcp_start and its neighbours.
 */
#pragma once

#include "local_pipe.hpp"
#include "movie_entry.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct ce_session;
struct ce_movie_log;

namespace chimera
{

/* The line a session's movie holds for these buttons (one byte each, in the
 * session's order; null for none pressed) and axis values (null for each at
 * rest). Defined in session.cpp. */
std::string sessionMovieEntry(const ce_session *s, const uint8_t *states, const int32_t *axes);
/* The text with every byte that does not begin or continue valid UTF-8
 * replaced by U+FFFD; what every reply goes through. */
std::string mcpValidUtf8(const std::string &in);
/* The columns a movie log's lines are in, for this session's machine: its
 * LogKey's when that names any, else the machine's own order. Defined in
 * session.cpp, with the three below. */
EntryLayout sessionLayoutOf(const ce_session *s, const ce_movie_log *log);
/* The LogKey that names a layout's columns with this machine's names. */
std::string sessionKeyOf(const ce_session *s, const EntryLayout &layout);
/* A line in that layout for these controls, and back (false when it cannot
 * be read). */
std::string sessionEntryIn(const ce_session *s, const EntryLayout &layout, const uint8_t *states, const int32_t *axes);
bool sessionDecodeIn(const ce_session *s, const EntryLayout &layout, const char *line, uint8_t *states, int32_t *axes);
/* `layout`, whose log has `key`, widened by every control `states` presses or
 * `axes` moves off its neutral that it does not have: a group of their own at
 * the end of the key. False when it lacks none. `newKey` is the log's key
 * then, and `columns` the new group's empty columns, which follow every line
 * (user-decided, 2026-10-18: a control the key lacks joins it). */
bool sessionWidenLayout(const ce_session *s, const EntryLayout &layout, const char *key,
	const uint8_t *states, const int32_t *axes, EntryLayout &wider, std::string &newKey, std::string &columns);
/* Gives a log that key, and every line of it those columns. */
void widenMovieLog(ce_movie_log *log, const std::string &newKey, const std::string &columns);

struct McpTool
{
	const char *name;
	const char *description;
	const char *schema;  /* the JSON Schema of its arguments */
	bool changes;        /* it changes the movie or the machine: refused unless the person allowed changes */
	bool host;           /* the frontend runs it, not the engine */
	bool local;          /* it exists only where there is no window (mcp_local.hpp) */
	bool window = false; /* it exists only where there is a window */
};

/* A whole number given as text: decimal, or hex with 0x ("0x2E329"); and a
 * number written as such hex, as answers give addresses beside the number
 * (issue 571). */
bool mcpParseInteger(const std::string &text, uint64_t &value);
std::string mcpHex(uint64_t value);

/* freeze and unfreeze, read (issue 571): a memory freeze (domain, address,
 * size, value, its condition as ce_session_freeze takes one), a property's
 * (property, text), or what unfreeze lets go of (index, all, a place). False
 * with why for arguments that do not make one. */
struct McpFreezeAsk
{
	std::string domain, property, text;
	int64_t address = 0, index = -1;
	int32_t size = 1, compareKind = 0;
	uint64_t value = 0, compare = 0;
	bool bigEndian = false, all = false;
};
bool mcpReadFreeze(const std::string &tool, const std::string &argumentsJson, McpFreezeAsk &ask, std::string &why);

/* Whether a tool is offered by this program: a tool of one kind of server is
 * not listed, or run, by the other. */
bool mcpOffered(const McpTool &tool);

/* Whether this program serves a machine of its own, with no window. It
 * decides which tools are listed and what the introduction says. */
void mcpSetLocal(bool local);

const McpTool *mcpTools(size_t &count);
const McpTool *mcpFindTool(const std::string &name);

/* What a tool call ends with: the MCP "content" array as JSON text, and
 * whether it is an error. */
struct McpResult
{
	std::string content;
	bool isError = false;
};

/* freeze and unfreeze, on the engine's list (a window reads it back into its
 * Cheats list, ce_session_freeze_changes): answers the list as list_freezes does */
McpResult mcpFreezeTool(ce_session *s, const std::string &tool, const std::string &argumentsJson);
/* one search_filter on the agent's search, for search_steps between its seeks */
McpResult mcpSearchFilter(ce_session *s, const std::string &argumentsJson);

/* [{"type":"text","text":<text>}] */
std::string mcpTextContent(const std::string &text);
McpResult mcpError(const std::string &sentence);

/* capture_series (issue 355), the same for the window's server and the
 * windowless one. The engine reads the agent's arguments into a plan - the
 * frames to take pictures of, how to scale them, where to write them - and
 * hands it, as the call's prepared arguments, to whoever moves the machine;
 * it takes each picture when the machine is on one of those frames. */
struct McpShotPlan
{
	std::vector<int64_t> frames;
	int64_t maxWidth = 320;
	std::string folder;              /* "" for no files */
	std::string name = "shot-{n}.png";
	bool images = true;              /* the pictures are in the answer */
	double framesPerSecond = 0;      /* the machine's own, for the seconds a shot is at */
};
bool mcpReadShotPlan(const std::string &preparedJson, McpShotPlan &plan);
/* A picture of the frame the machine is on - `frame`, as whoever moved it
 * counts - as shot n (from 1) of the plan: its facts as a JSON object, and
 * the PNG (base64) for the answer when the plan wants them. The file, when
 * the plan has a folder, is written. */
bool mcpTakeShot(ce_session *s, const McpShotPlan &plan, size_t n, int64_t frame, std::string &factsJson, std::string &pictureBase64, std::string &why);
/* The answer to capture_series: the frontend's text, which is a JSON
 * object, with "shots" added, then the pictures. */
std::string mcpShotsContent(const std::string &answerJson, const std::vector<std::string> &facts, const std::vector<std::string> &pictures);

/* list_cores and describe_core over these folders, and a core of them by its
 * file or its name: the windowless server's cores folder, or the window's
 * (mcp_local.cpp). */
McpResult mcpListCores(const std::vector<std::string> &dirs);
McpResult mcpDescribeCore(const std::vector<std::string> &dirs, const std::string &argumentsJson);
bool mcpFindPackage(const std::vector<std::string> &dirs, const std::string &asked, std::string &path, std::string &why);

/* Runs one tool call for the protocol. */
using McpExecutor = std::function<McpResult(const std::string &tool, const std::string &argumentsJson)>;

/* One line from the agent's program in, the line to send back out - empty
 * when the line needs no answer. `client`, when given, receives the name and
 * version the agent's program gave when it introduced itself. */
std::string mcpHandleLine(const std::string &line, const McpExecutor &execute, std::string *client);

/* Runs one tool call at once, on the calling thread, where there is no
 * frontend to hand anything to: the engine's own tools on `s` and `log`, and
 * for a tool the movie's holder runs, `host` - after the engine has checked
 * an edit and worked out the step it amounts to, and with the watched values
 * added to an answer that lands on a frame. Nothing is refused for not being
 * allowed: where there is no window, the machine is the agent's own. */
using McpHostRunner = std::function<McpResult(const std::string &tool, const std::string &argumentsJson)>;
McpResult mcpRunDirect(const std::string &tool, const std::string &argumentsJson, ce_session *s, const ce_movie_log *log,
	const std::string &hostStatusJson, const McpHostRunner &host);

/* The link from chimera-mcp to a running Chimera. It connects when a call
 * needs it and again after Chimera, or its server, went away and came back,
 * so the agent's program never has to be restarted for that. */
class McpLink
{
public:
	/* The name the running Chimera shows in Tools > MCP Server... */
	void setClient(const std::string &nameAndVersion) { client = nameAndVersion; }
	McpResult call(const std::string &tool, const std::string &argumentsJson);

	/* how long a call may wait for Chimera to take it up, and (in Chimera) how
	 * old a waiting call may be and still be run. The second is the shorter,
	 * so a call the agent was told had failed is never run afterwards. */
	static constexpr int kTakeUpMs = 30000;
	static constexpr int kStaleMs = 25000;

private:
	PipeClient pipe;
	std::string client;
	uint64_t nextId = 1;
	/* what the running Chimera said of itself when it answered the greeting:
	 * a warning added to every answer when its build is not this program's */
	std::string mismatch;
};

/* What chimera-mcp and the running Chimera say of each other (issue 571): a
 * sentence for the agent when their builds differ, naming the tools one has
 * and the other does not and the chimera-mcp to point at; "" when they match.
 * otherTools is nullptr when the other side did not list its tools (a build
 * from before 2026-11-12); program is the window's own chimera-mcp, or "". */
std::string mcpBuildMismatch(bool fromWindow, const std::string &otherBuild, const std::vector<std::string> *otherTools,
	const std::string &program);

} // namespace chimera
