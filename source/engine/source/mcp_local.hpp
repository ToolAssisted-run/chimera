/* mcp_local.hpp - internal: the MCP server with no window (docs/mcp-server.md).
 * Not part of the ABI.
 *
 * In a running Chimera the server's tools work on the project a person has
 * open, and TAStudio holds the movie. Here there is no window and no person:
 * chimera-mcp opens a machine of its own for the agent, and this class holds
 * the movie - its input, its undo history, its branches and markers - with
 * nothing but the engine under it.
 *
 * What the tools MEAN is not decided here. The engine's tools (memory, the
 * picture, RAM search, watches) are the same ones, and an edit arrives as the
 * step the engine made of it (mcp.cpp, "the editing tools"). What is this
 * class's own is what a window would do: open and save projects, keep the
 * undo history, move the playback position.
 *
 * It reads and writes only where it was told it may when it was started.
 */
#pragma once

#include "mcp.hpp"

#include <map>
#include <string>
#include <vector>

struct ce_movie_log;
struct ce_project;

namespace chimera
{

struct McpLocalOptions
{
	std::string coresDir;               /* core packages are read from here and nowhere else */
	std::vector<std::string> fileDirs;  /* games, projects, movies and firmware may be read from these */
	std::string workDir;                /* everything written goes here; it may be read from too */
	bool gpu = false;                   /* ask for the GPU bridge, as chimera-run --gpu does */
	int64_t historyMiB = 256;           /* the budget of the saved states that make going back cheap */
};

class McpLocal
{
public:
	explicit McpLocal(const McpLocalOptions &options);
	~McpLocal();
	McpLocal(const McpLocal &) = delete;
	McpLocal &operator=(const McpLocal &) = delete;

	/* Runs one tool call and answers it. */
	McpResult call(const std::string &tool, const std::string &argumentsJson);

private:
	McpLocalOptions options;
	ce_project *project = nullptr;
	ce_session *session = nullptr;
	/* snapshot's states, by id, with the frame each was taken on (issue 571) */
	struct Kept { std::vector<uint8_t> bytes; int64_t frame = 0; };
	std::map<std::string, Kept> snapshots;
	int64_t snapshotCount = 0;
	ce_movie_log *movie = nullptr;      /* the movie being worked on; the session plays a copy of it */
	std::string projectPath;            /* where the project was opened from or last saved to, "" for neither */
	std::string firmwarePins;           /* [{"id", "sha1"}] of what the last start was given */
	std::vector<std::string> firmwareWarnings; /* its firmware against the project's pins */
	std::string packagePath;
	bool unsaved = false;
	int currentBranch = -1;
	std::vector<std::string> undoSteps; /* the movie's text before each edit, newest last */

	McpResult runHost(const std::string &tool, const std::string &argumentsJson);
	McpResult listCores();
	McpResult describeCore(const std::string &argumentsJson);
	McpResult openGame(const std::string &argumentsJson);
	McpResult openProject(const std::string &argumentsJson);
	McpResult saveProject(const std::string &argumentsJson);
	McpResult serverInfo() const;
	McpResult pause() const;
	McpResult exportMovie(const std::string &argumentsJson);
	McpResult closeMachine();

	bool startMachine(const std::string &firmwareJson, bool otherBuildAllowed, std::string &why);
	void dropMachine();

	/* What was open, set aside while another opens: one machine at a time is
	 * all a process can hold, so the old one's session goes and its project and
	 * input are kept. An open that fails brings them back (engine analysis,
	 * 2026-10-18: a failed open used to lose the unsaved input). */
	struct Previous
	{
		ce_project *project = nullptr;
		ce_movie_log *movie = nullptr;
		std::string projectPath, packagePath, firmware;
		bool otherBuild = false, unsaved = false;
		int currentBranch = -1;
		std::vector<std::string> undoSteps;
	};
	Previous setAside();
	void giveUp(Previous &previous);
	std::string bringBack(Previous &previous);
	std::string startedFirmware;        /* what the running machine was started with */
	bool startedOtherBuild = false;
	std::string movieText() const;
	void rememberForUndo();
	bool movieChanged(int64_t fromFrame, std::string &why);
	bool landOn(int64_t frame, std::string &why);
	std::string hostStatus() const;

	bool mayRead(const std::string &path, std::string &resolved, std::string &why) const;
	bool mayWrite(const std::string &path, std::string &resolved, std::string &why) const;
	bool findPackage(const std::string &asked, std::string &path, std::string &why) const;
};

} // namespace chimera
