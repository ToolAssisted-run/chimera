/* mcp_local.cpp - see mcp_local.hpp. */

#include "mcp_local.hpp"
#include "sha1.hpp"
#include "project_check.hpp"
#include "file_io.hpp"

#include "chimera/engine.h"

#include "../../extern/cjson/cJSON.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace chimera
{

namespace
{

namespace fs = std::filesystem;

const char kNothingOpen[] = "No project is open. open_project opens one; open_game starts one from a core and a game.";
const size_t kUndoKept = 100;
/* ...and no more than this of the movie's text in all: each step is the
 * whole movie, and a long movie's hundred copies were hundreds of MB */
const size_t kUndoBytes = size_t{ 64 } << 20;

std::string printed(cJSON *item)
{
	char *text = cJSON_PrintUnformatted(item);
	std::string out = text != nullptr ? text : "null";
	cJSON_free(text);
	return out;
}

McpResult answered(cJSON *object)
{
	McpResult result;
	result.content = mcpTextContent(printed(object));
	cJSON_Delete(object);
	return result;
}

/* A whole number of the arguments in [low, high]. */
bool numberOf(const cJSON *arguments, const char *name, int64_t low, int64_t high, int64_t &out)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, name);
	if (!cJSON_IsNumber(item) || item->valuedouble != static_cast<double>(static_cast<int64_t>(item->valuedouble))) return false;
	out = static_cast<int64_t>(item->valuedouble);
	return out >= low && out <= high;
}

std::string textOf(const cJSON *arguments, const char *name)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, name);
	return cJSON_IsString(item) ? item->valuestring : "";
}

/* A path with its links and dots worked out, whether or not it exists yet. */
fs::path settled(const fs::path &path)
{
	std::error_code ignored;
	const fs::path full = fs::weakly_canonical(fs::absolute(path, ignored), ignored);
	return full.empty() ? path.lexically_normal() : full;
}

bool isInside(const fs::path &path, const std::string &folder)
{
	if (folder.empty()) return false;
	const fs::path root = settled(folder);
	const fs::path rest = path.lexically_relative(root);
	return !rest.empty() && rest.native()[0] != '.';
}

bool readWhole(const std::string &path, std::string &out)
{
	std::ifstream in(fs::u8path(path), std::ios::binary);
	if (!in) return false;
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	return true;
}

/* A package's waterbox.config, parsed, or null. The caller deletes it. */
cJSON *configOf(ce_package *package)
{
	uint64_t length = 0;
	const uint8_t *bytes = ce_package_entry(package, "waterbox.config", &length);
	return bytes != nullptr ? cJSON_ParseWithLength(reinterpret_cast<const char *>(bytes), length) : nullptr;
}

std::string nowText()
{
	char stamp[32] = "";
	const std::time_t now = std::time(nullptr);
	struct tm parts;
#ifdef _WIN32
	localtime_s(&parts, &now);
#else
	localtime_r(&now, &parts);
#endif
	std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &parts);
	return stamp;
}

} // namespace

McpLocal::McpLocal(const McpLocalOptions &given) : options(given) {}

McpLocal::~McpLocal() { dropMachine(); }

/* ---- which files ---- */

bool McpLocal::mayRead(const std::string &path, std::string &resolved, std::string &why) const
{
	const fs::path asked = fs::u8path(path);
	/* a bare name, or a relative path, is looked for in the work folder */
	const fs::path full = settled(asked.is_absolute() ? asked : fs::u8path(options.workDir) / asked);
	bool allowed = isInside(full, options.workDir);
	for (const std::string &dir : options.fileDirs) allowed = allowed || isInside(full, dir);
	if (!allowed)
	{
		why = "'" + path + "' is not in a folder this server may read. It reads the folders given with --files and --work when it was started.";
		return false;
	}
	resolved = full.u8string();
	return true;
}

bool McpLocal::mayWrite(const std::string &path, std::string &resolved, std::string &why) const
{
	const fs::path asked = fs::u8path(path);
	const fs::path full = settled(asked.is_absolute() ? asked : fs::u8path(options.workDir) / asked);
	if (!isInside(full, options.workDir))
	{
		why = "'" + path + "' is not in the work folder. This server writes only inside the folder given with --work when it was started.";
		return false;
	}
	resolved = full.u8string();
	return true;
}

/* A core package of the cores folders: by its file name, or by the name the
 * core gives itself. */
bool mcpFindPackage(const std::vector<std::string> &dirs, const std::string &asked, std::string &path, std::string &why)
{
	/* By file name first, which opens nothing: every package opened here is
	 * read whole and hashed, and that was done for every file in the folder
	 * on every lookup (engine analysis, 2026-10-18). */
	std::error_code failed;
	for (const std::string &dir : dirs)
	{
		for (const fs::directory_entry &entry : fs::directory_iterator(fs::u8path(dir), failed))
		{
			if (entry.path().filename().u8string() != asked && entry.path().stem().u8string() != asked) continue;
			const char *ignored = nullptr;
			ce_package *package = ce_package_open(entry.path().u8string().c_str(), &ignored);
			if (package == nullptr) continue;
			ce_package_free(package);
			path = entry.path().u8string();
			return true;
		}
	}
	for (const std::string &dir : dirs)
	{
		for (const fs::directory_entry &entry : fs::directory_iterator(fs::u8path(dir), failed))
		{
			const char *ignored = nullptr;
			ce_package *package = ce_package_open(entry.path().u8string().c_str(), &ignored);
			if (package == nullptr) continue;
			cJSON *config = configOf(package);
			const std::string name = textOf(config, "coreName");
			cJSON_Delete(config);
			ce_package_free(package);
			if (name == asked)
			{
				path = entry.path().u8string();
				return true;
			}
		}
	}
	why = "There is no core called '" + asked + "' in the cores folder. list_cores names them.";
	return false;
}

bool McpLocal::findPackage(const std::string &asked, std::string &path, std::string &why) const
{
	return mcpFindPackage({ options.coresDir }, asked, path, why);
}

/* ---- the machine ---- */

void McpLocal::dropMachine()
{
	if (session != nullptr) ce_session_free(session);
	if (movie != nullptr) ce_movie_log_free(movie);
	if (project != nullptr) ce_project_free(project);
	session = nullptr;
	movie = nullptr;
	project = nullptr;
	projectPath.clear();
	packagePath.clear();
	unsaved = false;
	currentBranch = -1;
	undoSteps.clear();
	snapshots.clear();
}

McpLocal::Previous McpLocal::setAside()
{
	Previous previous;
	if (session != nullptr) ce_session_free(session);
	session = nullptr;
	snapshots.clear();
	previous.project = project;
	previous.movie = movie;
	previous.projectPath = projectPath;
	previous.packagePath = packagePath;
	previous.firmware = startedFirmware;
	previous.otherBuild = startedOtherBuild;
	previous.unsaved = unsaved;
	previous.currentBranch = currentBranch;
	previous.undoSteps = std::move(undoSteps);
	project = nullptr;
	movie = nullptr;
	dropMachine();
	return previous;
}

void McpLocal::giveUp(Previous &previous)
{
	if (previous.movie != nullptr) ce_movie_log_free(previous.movie);
	if (previous.project != nullptr) ce_project_free(previous.project);
	previous = Previous{};
}

/* The project that was open, open again with its input as it was; its machine
 * starts from the beginning, since it could not be kept. Said in the refusal. */
std::string McpLocal::bringBack(Previous &previous)
{
	if (previous.project == nullptr) return std::string();
	dropMachine();
	project = previous.project;
	packagePath = previous.packagePath;
	previous.project = nullptr;
	std::string why;
	if (!startMachine(previous.firmware, previous.otherBuild, why))
	{
		dropMachine();
		giveUp(previous);
		return " The project that was open could not be started again (" + why + "), so nothing is open.";
	}
	ce_movie_log_free(movie);
	movie = previous.movie;
	previous.movie = nullptr;
	ce_session_movie_load(session, movie);
	projectPath = previous.projectPath;
	unsaved = previous.unsaved;
	currentBranch = previous.currentBranch;
	undoSteps = std::move(previous.undoSteps);
	return " The project that was open is open again, its input as it was; its machine starts again from frame 0.";
}

/* Starts the machine of `project` on the package at `packagePath`, with the
 * project's input as the movie. `firmwareJson` is {"id": "path", ...}. */
bool McpLocal::startMachine(const std::string &firmwareJson, bool otherBuildAllowed, std::string &why)
{
	std::vector<std::string> ids, blobs;
	cJSON *firmware = cJSON_Parse(firmwareJson.empty() ? "{}" : firmwareJson.c_str());
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, firmware)
	{
		std::string path, blob;
		if (!cJSON_IsString(item) || !mayRead(item->valuestring, path, why))
		{
			if (why.empty()) why = std::string("The firmware '") + item->string + "' must be given as a path.";
			break;
		}
		if (!readWhole(path, blob))
		{
			why = std::string("The firmware file '") + item->valuestring + "' could not be read.";
			break;
		}
		ids.emplace_back(item->string);
		blobs.push_back(std::move(blob));
	}
	cJSON_Delete(firmware);
	if (!why.empty()) return false;
	std::vector<const char *> idPointers;
	std::vector<const uint8_t *> blobPointers;
	std::vector<uint64_t> lengths;
	cJSON *pins = cJSON_CreateArray();
	for (size_t i = 0; i < ids.size(); i++)
	{
		idPointers.push_back(ids[i].c_str());
		blobPointers.push_back(reinterpret_cast<const uint8_t *>(blobs[i].data()));
		lengths.push_back(blobs[i].size());
		cJSON *pin = cJSON_CreateObject();
		cJSON_AddStringToObject(pin, "id", ids[i].c_str());
		cJSON_AddStringToObject(pin, "sha1", sha1Hex(blobPointers.back(), lengths.back()).c_str());
		cJSON_AddItemToArray(pins, pin);
	}
	firmwarePins = printed(pins);
	cJSON_Delete(pins);
	/* the project's pins against what was given: said to the agent, not refused */
	firmwareWarnings = ceFirmwarePinMismatches(project, idPointers.data(), blobPointers.data(), lengths.data(),
		static_cast<int32_t>(ids.size()));

	ce_gl_request(options.gpu ? 1 : 0);
	const char *error = nullptr;
	session = ce_session_open_project(packagePath.c_str(), project, nullptr,
		idPointers.data(), blobPointers.data(), lengths.data(), static_cast<int32_t>(ids.size()),
		otherBuildAllowed ? CE_OPEN_ALLOW_CORE_MISMATCH : 0u, &error);
	if (session == nullptr)
	{
		why = error != nullptr ? error : "the machine could not be started";
		return false;
	}
	movie = ce_movie_log_new();
	uint64_t length = 0;
	const char *text = ce_project_log_text(project, &length);
	if (length != 0 && ce_movie_log_parse(movie, text, length) != 0)
	{
		why = std::string("The project's input cannot be read: ") + ce_movie_log_last_error(movie);
		return false;
	}
	/* A movie with no LogKey is in the machine's own order: it is given the
	 * key that names that order, so that the project an agent saves reads the
	 * same after the package gains a control. */
	{
		const char *key = ce_movie_log_key(movie);
		chimera::EntryLayout keyed;
		if (key == nullptr || key[0] == '\0' || !keyed.buildFromKey(key, {}, {}))
		{
			const std::string named = sessionKeyOf(session, sessionLayoutOf(session, movie));
			if (!named.empty()) ce_movie_log_set_key(movie, named.c_str());
		}
	}
	if (ce_session_movie_load(session, movie) != 0)
	{
		why = "The project's input could not be given to the machine.";
		return false;
	}
	/* the saved states that make going back cheap, from frame 0 on */
	ce_session_greenzone_enable(session, static_cast<uint64_t>(options.historyMiB) << 20);
	startedFirmware = firmwareJson;
	startedOtherBuild = otherBuildAllowed;
	return true;
}

std::string McpLocal::movieText() const
{
	uint64_t length = 0;
	const char *text = ce_movie_log_serialize(movie, 0, &length);
	return std::string(text, static_cast<size_t>(length));
}

void McpLocal::rememberForUndo()
{
	undoSteps.push_back(movieText());
	size_t bytes = 0;
	for (const std::string &step : undoSteps) bytes += step.size();
	/* the newest is always kept: an edit can be undone at least once */
	while (undoSteps.size() > 1 && (undoSteps.size() > kUndoKept || bytes > kUndoBytes))
	{
		bytes -= undoSteps.front().size();
		undoSteps.erase(undoSteps.begin());
	}
}

/* The movie was changed from `fromFrame` on. The machine goes back to that
 * frame when it is past it, plays the new input from there, and the saved
 * states after the change are dropped - what TAStudio does for an edit. The
 * states before it stay, even when the machine is behind the edit: they hang
 * on input that did not change. */
bool McpLocal::movieChanged(int64_t fromFrame, std::string &why)
{
	unsaved = true;
	const bool past = ce_session_frame(session) > fromFrame;
	if (past && ce_session_seek(session, fromFrame) != 0)
	{
		why = ce_session_last_error(session);
		return false;
	}
	if (ce_session_movie_load(session, movie) != 0)
	{
		why = "The changed input could not be given to the machine.";
		return false;
	}
	ce_session_greenzone_invalidate(session, fromFrame);
	return past ? landOn(fromFrame, why) : true;
}

/* Puts the machine on a frame with that frame's picture drawn. A seek alone
 * draws nothing, so the frame before is sought and the last one played. */
bool McpLocal::landOn(int64_t frame, std::string &why)
{
	const int result = frame <= 0
		? ce_session_seek(session, 0)
		: ce_session_seek(session, frame - 1) != 0 ? -1 : ce_session_movie_advance(session, 0, nullptr, 1) < 0 ? -1 : 0;
	if (result == 0) return true;
	why = ce_session_last_error(session);
	return false;
}

std::string McpLocal::hostStatus() const
{
	cJSON *status = cJSON_CreateObject();
	if (project != nullptr)
	{
		const std::string title = ce_project_title(project);
		cJSON_AddStringToObject(status, "project", title.c_str());
		cJSON_AddStringToObject(status, "project_file", projectPath.c_str());
		cJSON_AddBoolToObject(status, "unsaved_changes", unsaved);
		cJSON_AddBoolToObject(status, "paused", true);
	}
	const std::string out = printed(status);
	cJSON_Delete(status);
	return out;
}

/* ---- the tools that exist only without a window ---- */

McpResult mcpListCores(const std::vector<std::string> &dirs)
{
	cJSON *cores = cJSON_CreateArray();
	std::error_code failed;
	std::vector<fs::path> entries;
	for (const std::string &dir : dirs)
	{
		std::vector<fs::path> here;
		for (const fs::directory_entry &entry : fs::directory_iterator(fs::u8path(dir), failed)) here.push_back(entry.path());
		std::sort(here.begin(), here.end());
		entries.insert(entries.end(), here.begin(), here.end());
	}
	for (const fs::path &path : entries)
	{
		const char *ignored = nullptr;
		ce_package *package = ce_package_open(path.u8string().c_str(), &ignored);
		if (package == nullptr) continue;
		cJSON *config = configOf(package);
		cJSON *core = cJSON_CreateObject();
		cJSON_AddStringToObject(core, "file", path.filename().u8string().c_str());
		cJSON_AddStringToObject(core, "name", textOf(config, "coreName").c_str());
		cJSON_AddStringToObject(core, "version", textOf(config, "version").c_str());
		cJSON *machines = cJSON_CreateArray();
		const cJSON *machine = nullptr;
		cJSON_ArrayForEach(machine, cJSON_GetObjectItemCaseSensitive(config, "machines"))
		{
			const std::string name = textOf(machine, "systemName").empty() ? textOf(machine, "id") : textOf(machine, "systemName");
			if (!name.empty()) cJSON_AddItemToArray(machines, cJSON_CreateString(name.c_str()));
		}
		if (cJSON_GetArraySize(machines) == 0 && !textOf(config, "systemName").empty())
			cJSON_AddItemToArray(machines, cJSON_CreateString(textOf(config, "systemName").c_str()));
		cJSON_AddItemToObject(core, "machines", machines);
		/* the kinds of file it opens: what its slots take, and the extensions it
		 * claims (issue 355: a ColecoVision core takes .col and .cv only) */
		std::vector<std::string> kinds;
		const cJSON *claimed = nullptr;
		cJSON_ArrayForEach(claimed, cJSON_GetObjectItemCaseSensitive(config, "extensions"))
		{
			std::string kind = claimed->string != nullptr ? claimed->string : "";
			if (!kind.empty() && kind[0] == '.') kind.erase(0, 1);
			if (!kind.empty()) kinds.push_back(kind);
		}
		uint64_t length = 0;
		const uint8_t *slots = ce_package_entry(package, "file_slots.json", &length);
		cJSON *slotsJson = slots != nullptr ? cJSON_ParseWithLength(reinterpret_cast<const char *>(slots), length) : nullptr;
		const cJSON *slot = nullptr;
		cJSON_ArrayForEach(slot, cJSON_GetObjectItemCaseSensitive(slotsJson, "slots"))
		{
			const cJSON *format = nullptr;
			cJSON_ArrayForEach(format, cJSON_GetObjectItemCaseSensitive(slot, "formats"))
				if (cJSON_IsString(format)) kinds.push_back(format->valuestring);
		}
		cJSON_Delete(slotsJson);
		for (std::string &kind : kinds) std::transform(kind.begin(), kind.end(), kind.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		std::sort(kinds.begin(), kinds.end());
		kinds.erase(std::unique(kinds.begin(), kinds.end()), kinds.end());
		cJSON *extensions = cJSON_AddArrayToObject(core, "extensions");
		for (const std::string &kind : kinds) cJSON_AddItemToArray(extensions, cJSON_CreateString(kind.c_str()));
		cJSON_AddItemToArray(cores, core);
		cJSON_Delete(config);
		ce_package_free(package);
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddItemToObject(out, "cores", cores);
	return answered(out);
}

McpResult mcpDescribeCore(const std::vector<std::string> &dirs, const std::string &argumentsJson)
{
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const std::string asked = textOf(arguments, "core");
	cJSON_Delete(arguments);
	std::string path, why;
	if (!mcpFindPackage(dirs, asked, path, why)) return mcpError(why);
	const char *ignored = nullptr;
	ce_package *package = ce_package_open(path.c_str(), &ignored);
	if (package == nullptr) return mcpError("The package of '" + asked + "' cannot be read.");
	cJSON *config = configOf(package);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "file", fs::u8path(path).filename().u8string().c_str());
	cJSON_AddStringToObject(out, "name", textOf(config, "coreName").c_str());
	cJSON_AddStringToObject(out, "version", textOf(config, "version").c_str());
	/* what the core declares, as it declares it: its settings, the firmware
	 * it may ask for, and the files a project of it holds */
	for (const char *part : { "settings", "firmware", "machines" })
	{
		cJSON *declared = cJSON_DetachItemFromObjectCaseSensitive(config, part);
		if (declared != nullptr) cJSON_AddItemToObject(out, part, declared);
	}
	uint64_t length = 0;
	const uint8_t *slots = ce_package_entry(package, "file_slots.json", &length);
	cJSON *slotsJson = slots != nullptr ? cJSON_ParseWithLength(reinterpret_cast<const char *>(slots), length) : nullptr;
	cJSON *declaredSlots = cJSON_DetachItemFromObjectCaseSensitive(slotsJson, "slots");
	if (declaredSlots != nullptr) cJSON_AddItemToObject(out, "file_slots", declaredSlots);
	else cJSON_AddStringToObject(out, "file_slots_note", "This core declares no slots: a project of it holds one game file, in the slot \"rom\".");
	cJSON_Delete(slotsJson);
	cJSON_Delete(config);
	ce_package_free(package);
	return answered(out);
}

McpResult McpLocal::listCores()
{
	return mcpListCores({ options.coresDir });
}

McpResult McpLocal::describeCore(const std::string &argumentsJson)
{
	return mcpDescribeCore({ options.coresDir }, argumentsJson);
}

McpResult McpLocal::openGame(const std::string &argumentsJson)
{
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	std::string why;
	const std::string core = textOf(arguments, "core");
	std::string package;
	if (!findPackage(core, package, why))
	{
		cJSON_Delete(arguments);
		return mcpError(why);
	}
	Previous previous = setAside();
	packagePath = package;
	project = ce_project_new();
	const char *ignored = nullptr;
	ce_package *opened = ce_package_open(package.c_str(), &ignored);
	cJSON *config = opened != nullptr ? configOf(opened) : nullptr;
	const char *sha1 = opened != nullptr ? ce_package_sha1(opened) : nullptr;
	ce_project_set_core(project, textOf(config, "coreName").c_str(), textOf(config, "version").c_str(), sha1 != nullptr ? sha1 : "");
	cJSON_Delete(config);
	if (opened != nullptr) ce_package_free(opened);

	/* the files: {"slot": ["path", ...]}, each kept under its own name */
	std::string firstName;
	const cJSON *slot = nullptr;
	cJSON_ArrayForEach(slot, cJSON_GetObjectItemCaseSensitive(arguments, "files"))
	{
		const cJSON *file = nullptr;
		cJSON_ArrayForEach(file, slot)
		{
			std::string path;
			const char *error = nullptr;
			if (!cJSON_IsString(file) || !mayRead(file->valuestring, path, why)) why = why.empty() ? "'files' must give, for each slot, a list of paths." : why;
			else if (ce_project_file_add(project, fs::u8path(path).filename().u8string().c_str(), slot->string, path.c_str(), &error) != 0)
				why = std::string("'") + file->valuestring + "' could not be added: " + (error != nullptr ? error : "it could not be read");
			else if (firstName.empty()) firstName = fs::u8path(path).stem().u8string();
			if (!why.empty()) break;
		}
		if (!cJSON_IsArray(slot) && why.empty()) why = "'files' must give, for each slot, a list of paths.";
		if (!why.empty()) break;
	}
	const cJSON *settings = cJSON_GetObjectItemCaseSensitive(arguments, "settings");
	if (why.empty() && cJSON_IsObject(settings))
	{
		const char *error = nullptr;
		cJSON *copy = cJSON_Duplicate(settings, 1);
		if (ce_project_set_settings_text(project, printed(copy).c_str(), &error) != 0) why = error != nullptr ? error : "The settings were not understood.";
		cJSON_Delete(copy);
	}
	const std::string title = textOf(arguments, "title");
	ce_project_set_title(project, title.empty() ? firstName.c_str() : title.c_str());
	cJSON *firmware = cJSON_GetObjectItemCaseSensitive(arguments, "firmware");
	const std::string firmwareJson = cJSON_IsObject(firmware) ? printed(firmware) : "{}";
	cJSON_Delete(arguments);
	if (!why.empty() || !startMachine(firmwareJson, false, why))
	{
		dropMachine();
		return mcpError(why + bringBack(previous));
	}
	giveUp(previous);
	/* the firmware it was started with is pinned, as the window's projects
	 * pin theirs, so that opening it later on other files is said */
	ce_project_set_firmware_text(project, firmwarePins.c_str(), nullptr);
	unsaved = true; /* a project that has never been saved */
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "project", ce_project_title(project));
	cJSON_AddStringToObject(out, "core", ce_session_core_name(session));
	cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
	return answered(out);
}

McpResult McpLocal::openProject(const std::string &argumentsJson)
{
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const std::string asked = textOf(arguments, "path");
	const bool otherBuild = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "allow_other_core_build"));
	cJSON *firmware = cJSON_GetObjectItemCaseSensitive(arguments, "firmware");
	const std::string firmwareJson = cJSON_IsObject(firmware) ? printed(firmware) : "{}";
	cJSON_Delete(arguments);
	std::string path, why;
	if (!mayRead(asked, path, why)) return mcpError(why);
	Previous previous = setAside();
	const char *error = nullptr;
	project = ce_project_open(path.c_str(), &error);
	if (project == nullptr)
		return mcpError(std::string("The project could not be opened: ") + (error != nullptr ? error : "it is not a project")
			+ bringBack(previous));
	/* its files: beside it first, then in every folder this server may read */
	ce_project_resolve_dir(project, fs::u8path(path).parent_path().u8string().c_str());
	for (const std::string &dir : options.fileDirs) ce_project_resolve_dir(project, dir.c_str());
	ce_project_resolve_dir(project, options.workDir.c_str());

	/* its core: the build it names when that is in the cores folder, else a
	 * package of the same core, which the engine refuses unless allowed */
	const std::string wantedSha1 = ce_project_core_sha1(project);
	std::error_code failed;
	/* a package's SHA-1 is its file's, which is kept by the file's identity:
	 * a second look at an unchanged folder hashes nothing */
	for (const fs::directory_entry &entry : fs::directory_iterator(fs::u8path(options.coresDir), failed))
	{
		if (wantedSha1.empty() || !entry.is_regular_file(failed)) continue;
		uint64_t length = 0;
		std::string sha1;
		if (!chimera::sha1HexOfFile(entry.path().u8string().c_str(), &length, sha1) || sha1 != wantedSha1) continue;
		const char *ignored = nullptr;
		ce_package *package = ce_package_open(entry.path().u8string().c_str(), &ignored);
		if (package == nullptr) continue;
		ce_package_free(package);
		packagePath = entry.path().u8string();
		break;
	}
	if (packagePath.empty() && !findPackage(ce_project_core_name(project), packagePath, why))
	{
		const std::string coreName = ce_project_core_name(project);
		dropMachine();
		return mcpError("The project runs on the core '" + coreName + "', which is not in the cores folder." + bringBack(previous));
	}
	if (!startMachine(firmwareJson, otherBuild, why))
	{
		dropMachine();
		return mcpError(why + (why.find("pinned core") != std::string::npos
			? ". To run it on the build that is in the cores folder, give allow_other_core_build: true." : "") + bringBack(previous));
	}
	giveUp(previous);
	projectPath = path;
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "project", ce_project_title(project));
	cJSON_AddStringToObject(out, "core", ce_session_core_name(session));
	cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
	if (!firmwareWarnings.empty())
	{
		cJSON *warnings = cJSON_AddArrayToObject(out, "warnings");
		for (const std::string &warning : firmwareWarnings) cJSON_AddItemToArray(warnings, cJSON_CreateString(warning.c_str()));
	}
	return answered(out);
}

McpResult McpLocal::saveProject(const std::string &argumentsJson)
{
	if (project == nullptr) return mcpError(kNothingOpen);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	std::string asked = textOf(arguments, "path");
	const bool overwrite = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "overwrite"));
	cJSON_Delete(arguments);
	if (asked.empty()) asked = projectPath;
	if (asked.empty()) return mcpError("Give 'path': this project has not been saved yet.");
	std::string path, why;
	if (!mayWrite(asked, path, why)) return mcpError(why);
	/* nothing asks before a file is replaced, so a file that is there and is
	 * not this project's own is kept unless the agent said to replace it */
	std::error_code failed;
	if (!overwrite && path != projectPath && fs::exists(fs::u8path(path), failed))
		return mcpError("A file is already at '" + asked + "', so the project was not saved. Give overwrite: true to replace it.");
	const std::string text = movieText();
	ce_project_set_log_text(project, text.data(), text.size());
	const char *error = nullptr;
	if (ce_project_save(project, path.c_str(), &error) != 0)
		return mcpError(std::string("The project was not saved: ") + (error != nullptr ? error : "the file could not be written"));
	projectPath = path;
	unsaved = false;
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "saved", path.c_str());
	cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
	cJSON_AddBoolToObject(out, "unsaved_changes", false);
	return answered(out);
}

McpResult McpLocal::serverInfo() const
{
	cJSON *out = cJSON_CreateObject();
	cJSON *build = cJSON_Parse(ce_build_info());
	cJSON_AddItemToObject(out, "build", build != nullptr ? build : cJSON_CreateString(ce_build_info()));
	cJSON_AddBoolToObject(out, "window", false);
	cJSON_AddStringToObject(out, "cores_folder", options.coresDir.c_str());
	cJSON_AddStringToObject(out, "work_folder", options.workDir.c_str());
	cJSON *files = cJSON_AddArrayToObject(out, "file_folders");
	for (const std::string &dir : options.fileDirs) cJSON_AddItemToArray(files, cJSON_CreateString(dir.c_str()));
	return answered(out);
}

McpResult McpLocal::pause() const
{
	if (project == nullptr) return mcpError(kNothingOpen);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddBoolToObject(out, "paused", true);
	cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
	cJSON_AddStringToObject(out, "note", "Without a window the machine is always paused: it moves only when a tool moves it.");
	return answered(out);
}

McpResult McpLocal::exportMovie(const std::string &argumentsJson)
{
	if (project == nullptr) return mcpError(kNothingOpen);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const std::string asked = textOf(arguments, "path");
	cJSON_Delete(arguments);
	std::string path, why;
	if (asked.empty()) return mcpError("Give 'path', the file to write the movie's input to.");
	if (!mayWrite(asked, path, why)) return mcpError(why);
	/* A link is refused: one whose target does not exist passed the check as
	 * a plain new file inside the work folder, and writing followed it out
	 * (engine analysis, 2026-10-18). Written beside it and moved into place. */
	std::error_code linkError;
	if (fs::is_symlink(fs::u8path(path), linkError))
		return mcpError("'" + asked + "' is a link. The movie is written only to a plain file in the work folder.");
	const std::string text = movieText();
	chimera::FileWriter out;
	if (!out.open(path.c_str()) || !out.write(text.data(), text.size()) || !out.commit())
		return mcpError("The movie was not written: the file could not be written.");
	cJSON *done = cJSON_CreateObject();
	cJSON_AddStringToObject(done, "written", path.c_str());
	cJSON_AddNumberToObject(done, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
	return answered(done);
}

McpResult McpLocal::closeMachine()
{
	if (project == nullptr) return mcpError(kNothingOpen);
	const bool lost = unsaved;
	dropMachine();
	cJSON *out = cJSON_CreateObject();
	cJSON_AddBoolToObject(out, "closed", true);
	if (lost) cJSON_AddStringToObject(out, "note", "The project had changes that were not saved.");
	return answered(out);
}

/* ---- what a window would do: the tools the engine hands to the movie's holder ---- */

McpResult McpLocal::runHost(const std::string &tool, const std::string &argumentsJson)
{
	if (tool == "keep_work")
	{
		/* there is no person's work here to keep: the machine is the agent's own */
		McpResult kept;
		kept.content = mcpTextContent("nothing to keep");
		return kept;
	}
	if (project == nullptr) return mcpError(kNothingOpen);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	std::string why;
	cJSON *out = cJSON_CreateObject();
	const int64_t length = ce_movie_log_count(movie);
	int64_t frame = 0, count = 0;

	if (tool == "set_input")
	{
		const cJSON *entries = cJSON_GetObjectItemCaseSensitive(arguments, "entries");
		if (!numberOf(arguments, "frame", 0, INT32_MAX, frame) || !cJSON_IsArray(entries)) why = "The step to apply is not one the engine makes.";
		else
		{
			rememberForUndo();
			/* a control the movie's LogKey did not name: the key gains it and
			 * every line its empty columns, before the step's lines go in */
			const cJSON *key = cJSON_GetObjectItemCaseSensitive(arguments, "key");
			const cJSON *columns = cJSON_GetObjectItemCaseSensitive(arguments, "columns");
			if (cJSON_IsString(key) && cJSON_IsString(columns)) widenMovieLog(movie, key->valuestring, columns->valuestring);
			/* past the movie's end the frames between are empty ones */
			const std::string blank = sessionEntryIn(session, sessionLayoutOf(session, movie), nullptr, nullptr);
			while (ce_movie_log_count(movie) < frame) ce_movie_log_add(movie, blank.c_str());
			const cJSON *entry = nullptr;
			int64_t at = frame;
			cJSON_ArrayForEach(entry, entries)
			{
				if (at < ce_movie_log_count(movie)) ce_movie_log_set(movie, at, entry->valuestring);
				else ce_movie_log_add(movie, entry->valuestring);
				at++;
			}
			if (movieChanged(frame, why))
			{
				cJSON_AddNumberToObject(out, "frame", static_cast<double>(frame));
				cJSON_AddNumberToObject(out, "count", static_cast<double>(at - frame));
				cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
			}
		}
	}
	else if (tool == "insert_frames" || tool == "delete_frames")
	{
		const bool inserting = tool == "insert_frames";
		if (!numberOf(arguments, "frame", 0, length, frame) || !numberOf(arguments, "count", 1, 100000, count)
			|| (!inserting && frame + count > length))
			why = "The step to apply is not one the engine makes.";
		else
		{
			rememberForUndo();
			const std::string blank = sessionEntryIn(session, sessionLayoutOf(session, movie), nullptr, nullptr);
			/* in one step: one at a time moved every line after them each time
			 * (engine analysis, 2026-10-18) */
			if (inserting) ce_movie_log_insert_n(movie, frame, blank.c_str(), count);
			else ce_movie_log_remove_range(movie, frame, count);
			if (movieChanged(frame, why))
			{
				cJSON_AddNumberToObject(out, "frame", static_cast<double>(frame));
				cJSON_AddNumberToObject(out, inserting ? "inserted" : "deleted", static_cast<double>(count));
				cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
			}
		}
	}
	else if (tool == "undo")
	{
		if (undoSteps.empty()) why = "There is nothing to undo.";
		else
		{
			ce_movie_log *before = ce_movie_log_new();
			ce_movie_log_parse(before, undoSteps.back().data(), undoSteps.back().size());
			undoSteps.pop_back();
			const int64_t differs = ce_movie_log_divergent_point(movie, before);
			ce_movie_log_assign(movie, before);
			ce_movie_log_free(before);
			if (movieChanged(differs < 0 ? ce_session_frame(session) : differs, why))
			{
				cJSON_AddBoolToObject(out, "undone", true);
				cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
			}
		}
	}
	else if (tool == "seek")
	{
		const bool extend = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "extend"));
		if (!numberOf(arguments, "frame", 0, extend ? length + 100000 : length, frame))
			why = "'frame' must be between 0 and the movie's length, " + std::to_string(length) + (extend ? " (and at most 100000 past it with extend)." : "; give extend: true to add frames up to one past it.");
		else if (frame > length)
		{
			/* empty frames up to it first, one step (issue 571) */
			rememberForUndo();
			const std::string blank = sessionEntryIn(session, sessionLayoutOf(session, movie), nullptr, nullptr);
			ce_movie_log_insert_n(movie, length, blank.c_str(), frame - length);
			if (movieChanged(length, why) && landOn(frame, why)) cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
		}
		else if (landOn(frame, why)) cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
	}
	else if (tool == "snapshot")
	{
		uint64_t len = 0;
		const uint8_t *data = ce_session_save_state(session, &len);
		if (data == nullptr || len == 0) why = std::string("The machine could not be kept: ") + ce_session_last_error(session);
		else
		{
			const std::string id = "s" + std::to_string(++snapshotCount);
			snapshots[id] = { std::vector<uint8_t>(data, data + len), ce_session_frame(session) };
			cJSON_AddStringToObject(out, "id", id.c_str());
			cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
		}
	}
	else if (tool == "restore")
	{
		const cJSON *idItem = cJSON_GetObjectItemCaseSensitive(arguments, "id");
		const auto kept = cJSON_IsString(idItem) ? snapshots.find(idItem->valuestring) : snapshots.end();
		if (kept == snapshots.end()) why = "There is no snapshot with that id.";
		else if (ce_session_load_state(session, kept->second.bytes.data(), kept->second.bytes.size()) != 0)
			why = std::string("The snapshot could not be loaded: ") + ce_session_last_error(session);
		else if (kept->second.frame > ce_movie_log_count(movie)) why = "The movie is shorter than the snapshot's frame now.";
		else if (ce_session_movie_at(session, kept->second.frame) != 0) why = ce_session_last_error(session);
		else cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
	}
	else if (tool == "diff_memory")
	{
		/* frame_a's bytes kept, then frame_b's compared with them (issue 571) */
		int64_t frameB = 0;
		if (!numberOf(arguments, "frame_a", 0, length, frame) || !numberOf(arguments, "frame_b", 0, length, frameB))
			why = "'frame_a' and 'frame_b' must be between 0 and the movie's length, " + std::to_string(length) + ".";
		for (int phase = 0; why.empty() && phase < 2; phase++)
		{
			if (!landOn(phase == 0 ? frame : frameB, why)) break;
			const McpResult step = mcpDiffMemoryNow(session, phase, argumentsJson);
			cJSON *content = cJSON_Parse(step.content.c_str());
			const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
			const std::string said = cJSON_IsString(text) ? text->valuestring : "";
			cJSON_Delete(content);
			if (step.isError) why = said;
			else if (phase == 1)
			{
				cJSON_Delete(out);
				out = cJSON_Parse(said.c_str());
				if (out == nullptr) out = cJSON_CreateObject();
			}
		}
	}
	else if (tool == "search_steps")
	{
		/* each step: the machine to its frame, then the filter there (issue 571) */
		cJSON *done = cJSON_CreateArray();
		const cJSON *step = nullptr;
		int n = 0;
		cJSON_ArrayForEach(step, cJSON_GetObjectItemCaseSensitive(arguments, "steps"))
		{
			n++;
			if (!numberOf(step, "frame", 0, length, frame))
				why = "Step " + std::to_string(n) + ": 'frame' must be between 0 and the movie's length, " + std::to_string(length) + ".";
			else if (!landOn(frame, why))
				why = "Step " + std::to_string(n) + ": " + why;
			if (!why.empty()) break;
			cJSON *filter = cJSON_Duplicate(step, 1);
			cJSON_DeleteItemFromObjectCaseSensitive(filter, "frame");
			const McpResult filtered = mcpSearchFilter(session, printed(filter));
			cJSON_Delete(filter);
			cJSON *content = cJSON_Parse(filtered.content.c_str());
			const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
			const std::string said = cJSON_IsString(text) ? text->valuestring : "";
			cJSON_Delete(content);
			if (filtered.isError)
			{
				why = "Step " + std::to_string(n) + " (frame " + std::to_string(frame) + "): " + said;
				break;
			}
			cJSON *row = cJSON_Parse(said.c_str());
			if (row == nullptr) row = cJSON_CreateObject();
			cJSON_AddNumberToObject(row, "frame", static_cast<double>(frame));
			cJSON_AddItemToArray(done, row);
		}
		if (why.empty()) cJSON_AddItemToObject(out, "steps", done);
		else cJSON_Delete(done);
	}
	else if (tool == "advance_until")
	{
		const char *refusal = nullptr;
		int64_t most = 600;
		const cJSON *mostItem = cJSON_GetObjectItemCaseSensitive(arguments, "max_frames");
		if (mostItem != nullptr && !numberOf(arguments, "max_frames", 1, 100000, most)) why = "'max_frames' must be between 1 and 100000.";
		else if (ce_mcp_condition_set(session, argumentsJson.c_str(), &refusal) == 0) why = refusal != nullptr ? refusal : "The condition was not understood.";
		else
		{
			const int64_t start = ce_session_frame(session);
			const int64_t target = std::min(length, start + most);
			bool met = ce_mcp_condition_met(session) == 1;
			if (!met && start >= length) why = "The playback position is at the movie's end, frame " + std::to_string(length) + ", so there is nothing left to play.";
			while (why.empty() && !met && ce_session_frame(session) < target)
			{
				if (ce_session_movie_advance(session, 0, nullptr, 0) < 0) why = ce_session_last_error(session);
				else met = ce_mcp_condition_met(session) == 1;
			}
			/* the frame it stopped on is drawn, so a picture asked for next is that frame's */
			const int64_t stopped = ce_session_frame(session);
			if (why.empty() && stopped != start) landOn(stopped, why);
			if (why.empty())
			{
				cJSON_AddNumberToObject(out, "frame", static_cast<double>(stopped));
				cJSON_AddBoolToObject(out, "met", met);
				cJSON_AddNumberToObject(out, "frames_played", static_cast<double>(stopped - start));
				cJSON_AddStringToObject(out, "stopped_at", met ? "condition" : stopped >= length ? "movie_end" : "max_frames");
			}
		}
	}
	else if (tool == "list_markers")
	{
		cJSON *markers = cJSON_CreateArray();
		for (int32_t i = 0; i < ce_project_marker_count(project); i++)
		{
			cJSON *marker = cJSON_CreateObject();
			cJSON_AddNumberToObject(marker, "frame", static_cast<double>(ce_project_marker_frame(project, i)));
			cJSON_AddStringToObject(marker, "text", ce_project_marker_text(project, i));
			cJSON_AddBoolToObject(marker, "placed_by_user", true);
			cJSON_AddItemToArray(markers, marker);
		}
		cJSON_AddItemToObject(out, "markers", markers);
	}
	else if (tool == "add_marker")
	{
		const std::string text = textOf(arguments, "text");
		if (!numberOf(arguments, "frame", 0, INT32_MAX, frame)) why = "'frame' must be a frame number, 0 or more.";
		else
		{
			ce_project_marker_add(project, frame, text.c_str(), 0);
			unsaved = true;
			cJSON_AddNumberToObject(out, "frame", static_cast<double>(frame));
			cJSON_AddStringToObject(out, "text", text.c_str());
		}
	}
	else if (tool == "list_branches")
	{
		cJSON *branches = cJSON_CreateArray();
		for (int32_t i = 0; i < ce_project_branch_count(project); i++)
		{
			cJSON *branch = cJSON_CreateObject();
			cJSON_AddNumberToObject(branch, "number", i + 1);
			cJSON_AddNumberToObject(branch, "frame", static_cast<double>(ce_project_branch_frame(project, i)));
			cJSON_AddStringToObject(branch, "taken", ce_project_branch_time(project, i));
			cJSON_AddStringToObject(branch, "note", ce_project_branch_name(project, i));
			cJSON_AddBoolToObject(branch, "current", currentBranch == i);
			cJSON_AddItemToArray(branches, branch);
		}
		cJSON_AddItemToObject(out, "branches", branches);
	}
	else if (tool == "save_branch")
	{
		const std::string text = movieText();
		ce_project_branch_add(project, textOf(arguments, "note").c_str(), ce_session_frame(session), nowText().c_str(), text.data(), text.size());
		currentBranch = ce_project_branch_count(project) - 1;
		unsaved = true;
		cJSON_AddNumberToObject(out, "number", currentBranch + 1);
	}
	else if (tool == "load_branch")
	{
		const int32_t branches = ce_project_branch_count(project);
		int64_t number = 0;
		if (!numberOf(arguments, "number", 1, branches, number))
			why = branches == 0 ? "The project has no branches."
				: "'number' must be between 1 and " + std::to_string(branches) + ", the branches list_branches gives.";
		else
		{
			rememberForUndo();
			const int32_t index = static_cast<int32_t>(number - 1);
			uint64_t textLength = 0;
			const char *text = ce_project_branch_log_text(project, index, &textLength);
			ce_movie_log *loaded = ce_movie_log_new();
			ce_movie_log_parse(loaded, text, textLength);
			const int64_t differs = ce_movie_log_divergent_point(movie, loaded);
			ce_movie_log_assign(movie, loaded);
			ce_movie_log_free(loaded);
			const int64_t branchFrame = std::min<int64_t>(ce_project_branch_frame(project, index), ce_movie_log_count(movie));
			/* the machine becomes what the branch saved: its input, played to its frame */
			if ((differs < 0 || movieChanged(differs, why)) && landOn(branchFrame, why))
			{
				currentBranch = index;
				cJSON_AddNumberToObject(out, "number", static_cast<double>(number));
				cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
				cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
			}
		}
	}
	else if (tool == "capture_series")
	{
		/* the plan the engine made: past the movie's end empty frames are
		 * added first, then each frame is played to and its picture taken */
		McpShotPlan plan;
		std::string folder;
		const cJSON *blank = cJSON_GetObjectItemCaseSensitive(arguments, "blank");
		if (!mcpReadShotPlan(argumentsJson, plan) || !cJSON_IsString(blank)) why = "The step to apply is not one the engine makes.";
		else if (!plan.folder.empty() && mayWrite(plan.folder, folder, why)) plan.folder = folder;
		std::vector<std::string> facts, pictures;
		if (why.empty() && plan.frames.back() > length)
		{
			rememberForUndo();
			while (ce_movie_log_count(movie) < plan.frames.back()) ce_movie_log_add(movie, blank->valuestring);
			movieChanged(length, why);
		}
		for (size_t n = 1; why.empty() && n <= plan.frames.size(); n++)
		{
			std::string fact, picture;
			if (landOn(plan.frames[n - 1], why) && mcpTakeShot(session, plan, n, ce_session_frame(session), fact, picture, why))
			{
				facts.push_back(std::move(fact));
				pictures.push_back(std::move(picture));
			}
		}
		if (why.empty())
		{
			cJSON_AddNumberToObject(out, "frame", static_cast<double>(ce_session_frame(session)));
			cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
			const std::string answer = printed(out);
			cJSON_Delete(out);
			cJSON_Delete(arguments);
			McpResult result;
			result.content = mcpShotsContent(answer, facts, pictures);
			return result;
		}
	}
	else
	{
		why = "This server does not run the tool '" + tool + "'.";
	}
	cJSON_Delete(arguments);
	if (!why.empty())
	{
		cJSON_Delete(out);
		return mcpError(why);
	}
	/* a step that changed the movie's length says so (issue 571) */
	if (movie != nullptr && ce_movie_log_count(movie) != length && cJSON_IsObject(out))
	{
		cJSON_DeleteItemFromObjectCaseSensitive(out, "movie_length");
		cJSON_AddNumberToObject(out, "movie_length_before", static_cast<double>(length));
		cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(movie)));
	}
	return answered(out);
}

McpResult McpLocal::call(const std::string &tool, const std::string &argumentsJson)
{
	if (tool == "list_cores") return listCores();
	if (tool == "describe_core") return describeCore(argumentsJson);
	if (tool == "open_game") return openGame(argumentsJson);
	if (tool == "open_project") return openProject(argumentsJson);
	if (tool == "save_project") return saveProject(argumentsJson);
	if (tool == "server_info") return serverInfo();
	if (tool == "pause") return pause();
	if (tool == "export_movie") return exportMovie(argumentsJson);
	if (tool == "close") return closeMachine();
	/* every other tool is one a window offers too: the engine's own, or one
	 * it hands to the movie's holder - which here is this class */
	return mcpRunDirect(tool, argumentsJson, session, movie, hostStatus(),
		[this](const std::string &hostTool, const std::string &hostArguments) { return runHost(hostTool, hostArguments); });
}

} // namespace chimera
