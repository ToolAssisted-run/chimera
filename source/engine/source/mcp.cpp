/* mcp.cpp - the MCP server (docs/mcp-server.md): the list of tools, the
 * protocol chimera-mcp speaks with an agent's program, the link from
 * chimera-mcp to a running Chimera, and the part that lives in the running
 * Chimera and runs the tools on its machine (the ce_mcp_* calls of engine.h).
 */

#include "mcp.hpp"

#include "chimera/engine.h"
#include "file_io.hpp"
#include "thread_string.hpp"

#include "../../extern/cjson/cJSON.h"
#include "../../extern/miniz/miniz.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

/* session.cpp: which session this is, never reused within the process */
extern "C" uint64_t ce_session_serial(const ce_session *s);

namespace chimera
{

/* ---- the tools ---- */

namespace
{

const char kNoArguments[] = "{\"type\":\"object\",\"properties\":{},\"additionalProperties\":false}";

/* An address, as a number or as hex text ("0x2E329"): a slip between decimal
 * and hex sent a person to the wrong place once (issue 571). Every tool's
 * address reads both (integerArgument). */
/* What a search_filter takes, and each of search_steps' steps */
#define MCP_FILTER_PROPERTIES \
	"\"op\":{\"type\":\"string\",\"enum\":[\"equal\",\"not_equal\",\"greater\",\"greater_or_equal\",\"less\",\"less_or_equal\",\"different_by\"]}," \
	"\"compare\":{\"type\":\"string\",\"enum\":[\"value\",\"previous\",\"address\",\"changes\",\"difference\"],\"description\":\"What is compared: see the tool. value when 'value' is given, previous when it is not.\"}," \
	"\"value\":{\"type\":[\"number\",\"string\"],\"description\":\"What to compare with: a number, or hex text. A float search's is a number with a point.\"}," \
	"\"by\":{\"type\":[\"number\",\"string\"],\"description\":\"For different_by: how far apart.\"}," \
	"\"previous\":{\"type\":\"string\",\"enum\":[\"last_search\",\"original\",\"last_frame\",\"last_change\"],\"description\":\"What a previous value is from now on. last_search when left out.\"}," \
	"\"display\":{\"type\":\"string\",\"enum\":[\"unsigned\",\"signed\",\"float\"],\"description\":\"How the values are read: unsigned when left out; float for a 4- or 8-byte search.\"}," \
	"\"signed\":{\"type\":\"boolean\",\"description\":\"The same as display signed.\"}"

#define MCP_ADDRESS "\"address\":{\"type\":[\"integer\",\"string\"],\"description\":\"A number, or hex text such as \\\"0x2E329\\\".\"}"


const McpTool kTools[] = {
	{ "status",
	  "What is open in Chimera right now: the core and the system, the frame the machine is on, the movie's length, "
	  "the size of the picture, the frame rate, whether the person has allowed changes, and whether the core has stopped. "
	  "With 'controls' true, also the names of the buttons and axes plugged in (which 'controls' tells more of).",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"controls\":{\"type\":\"boolean\",\"description\":\"Name the buttons and axes too. false when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "controls",
	  "The machine's buttons and axes by name, in the order of the columns of a movie's input lines, "
	  "with each axis's rest value. 'active' is false for a control this machine does not have plugged in.",
	  kNoArguments, false, false },
	{ "get_input",
	  "The movie's input for a range of frames, one line a frame, as the movie file holds it. "
	  "'key' names the columns: groups are separated by '|', a '.' is a button not pressed, a letter is a button pressed, "
	  "and an axis is a number. At most 1000 frames a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"from\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first frame. 0 when left out.\"},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"description\":\"How many frames. 100 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "screenshot",
	  "The picture of the frame the machine is on, as a PNG. A large picture is scaled down to max_width. 'region' crops "
	  "it first, and 'pixels' answers the colour of each pixel asked for, as #RRGGBB, so a colour needs no picture: with "
	  "'image' false, none is sent.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"max_width\":{\"type\":\"integer\",\"minimum\":32,\"maximum\":4096,\"description\":\"The widest picture wanted, in pixels. 800 when left out.\"},"
	  "\"region\":{\"type\":\"object\",\"properties\":{\"x\":{\"type\":\"integer\",\"minimum\":0},\"y\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"width\":{\"type\":\"integer\",\"minimum\":1},\"height\":{\"type\":\"integer\",\"minimum\":1}},"
	  "\"required\":[\"x\",\"y\",\"width\",\"height\"],\"additionalProperties\":false,\"description\":\"Only this part of the picture, in its own pixels.\"},"
	  "\"pixels\":{\"type\":\"array\",\"maxItems\":256,\"items\":{\"type\":\"array\",\"items\":{\"type\":\"integer\",\"minimum\":0},\"minItems\":2,\"maxItems\":2},"
	  "\"description\":\"[x, y] pixels of the whole picture whose colours to answer.\"},"
	  "\"image\":{\"type\":\"boolean\",\"description\":\"Send the picture. true when left out.\"},"
	  "\"layers\":{\"type\":\"string\",\"enum\":[\"game\",\"with_lua\"],\"description\":\"game (the usual): the machine's picture. with_lua: "
	  "the picture as the window shows it, with the padding and everything Lua scripts draw, at the game's own scale; the answer gives the "
	  "padding on each side, so a pixel can be turned back into the game's. With the window only; region, pixels and image are for game.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "list_memory",
	  "The machine's memory: its domains (blocks of memory, addressed from 0) and its buses "
	  "(address spaces as the machine's processor sees them), each with its size and whether it can be written.",
	  kNoArguments, false, false },
	{ "read_memory",
	  "Reads values from a memory domain or a bus at the frame the machine is on. At most 4096 bytes a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A name from list_memory.\"},"
	  MCP_ADDRESS ","
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"description\":\"How many values. 1 when left out.\"},"
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"The byte order of values wider than a byte. false when left out.\"}"
	  "},\"required\":[\"domain\",\"address\"],\"additionalProperties\":false}",
	  false, false },
	{ "registers",
	  "The processor's registers, for a core that gives them. A core that names its registers' roles adds 'roles': the "
	  "program counter (pc), the stack pointer (sp) and the flags, each with its register, and on a segmented processor "
	  "the segment register paired with it and the linear address. 'names' reads only those; a name the core does not "
	  "have is refused with the names it has.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"names\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Only these registers, or roles (pc, sp, flags).\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "list_properties",
	  "A game core's named values (a player's position, a score), each with its type and what it reads as now. "
	  "Emulator cores have none. At most 'limit' are returned; 'total' says how many matched.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"filter\":{\"type\":\"string\",\"description\":\"Only names containing this text, in any case.\"},"
	  "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":500,\"description\":\"100 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "get_property",
	  "One named value of a game core, as a person reads it. An element of an array is asked for as Name[3].",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\"}"
	  "},\"required\":[\"name\"],\"additionalProperties\":false}",
	  false, false },
	{ "watch",
	  "Names values to be told with every answer that lands the machine on a frame: status, seek, advance_until and "
	  "load_branch then carry a 'watches' list, which saves a read_memory call after each. Each watch is a memory value "
	  "(domain, address, type) or a game core's property. The list given replaces the one before; an empty list ends "
	  "watching. Answers with the values now. At most 32.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"watches\":{\"type\":\"array\",\"maxItems\":32,\"items\":{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\",\"description\":\"What to call it in the answers. Made up from the rest when left out.\"},"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain or bus from list_memory. Give this with 'address', or give 'property'.\"},"
	  MCP_ADDRESS ","
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"},"
	  "\"property\":{\"type\":\"string\",\"description\":\"A game core's named value, in place of domain and address.\"}"
	  "},\"additionalProperties\":false}}"
	  "},\"required\":[\"watches\"],\"additionalProperties\":false}",
	  false, false },
	{ "search_start",
	  "Starts a RAM search over a memory domain: every address becomes a candidate, and its value now is remembered as its "
	  "'previous' value and its original one. Then change something in the game (seek, or let the person play) and narrow "
	  "the candidates with search_filter. Every frame the machine runs counts each candidate's changes. One search at a "
	  "time; starting a new one drops the old.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain from list_memory (not a bus).\"},"
	  "\"size\":{\"type\":\"integer\",\"enum\":[1,2,4,8],\"description\":\"Bytes a value. 1 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"},"
	  "\"misaligned\":{\"type\":\"boolean\",\"description\":\"Every address a candidate, not only multiples of the size (a 2-byte value at an odd address). false when left out.\"},"
	  "\"ranges\":{\"type\":\"array\",\"maxItems\":64,\"items\":{\"type\":\"array\",\"items\":{\"type\":[\"integer\",\"string\"]},\"minItems\":2,\"maxItems\":2},"
	  "\"description\":\"Search only these parts of the domain: [start, length] pairs, numbers or hex text, each widened to whole 8-byte blocks. The whole domain when left out.\"}"
	  "},\"required\":[\"domain\"],\"additionalProperties\":false}",
	  false, false },
	{ "search_filter",
	  "Keeps the candidates that compare as asked, and answers how many are left. What is compared with 'value' is chosen "
	  "by 'compare': the value now ('value', the usual when 'value' is given), the previous value ('previous', the usual "
	  "when it is not: then each candidate's value now is compared with its own previous one), the address, the number of "
	  "changes counted since the search started ('changes'), or the value now less the previous one ('difference'). "
	  "'previous' says what a candidate's previous value is from now on: its value at the last search (the usual), its "
	  "original value, its value on the last frame, or its value before its last change.",
	  "{\"type\":\"object\",\"properties\":{"
	  MCP_FILTER_PROPERTIES
	  "},\"required\":[\"op\"],\"additionalProperties\":false}",
	  false, false },
	{ "search_results",
	  "The candidates left, lowest address first: each with its value now, its previous value, the difference, and how "
	  "many times it changed. At most 'limit' are returned; 'candidates' says how many there are. With more than one, "
	  "'clusters' says where they lie (runs of candidates close together), and 'stride' is the distance that most often "
	  "separates one from the next when one does: the size of a record in a table, as game objects often are.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":500,\"description\":\"50 when left out.\"},"
	  "\"display\":{\"type\":\"string\",\"enum\":[\"unsigned\",\"signed\",\"float\"],\"description\":\"How values are shown. unsigned when left out; float is a 4- or 8-byte search's.\"},"
	  "\"signed\":{\"type\":\"boolean\",\"description\":\"The same as display signed.\"}"
	  "},\"additionalProperties\":false}",
	  false, false },
	{ "search_undo",
	  "Takes the last search_filter back.",
	  kNoArguments, false, false },
	{ "list_branches",
	  "The project's branches: for each, its number, the frame it was taken on, when, and its note.",
	  kNoArguments, false, true },
	{ "list_markers",
	  "The project's markers: for each, its frame, its text, and whether a person placed it "
	  "(every movie has three of its own: the run's start, its last input and its end).",
	  kNoArguments, false, true },
	{ "server_info",
	  "Which Chimera this is and where it keeps things: its build, the folder it runs from, and the folders it saves "
	  "projects (Movies), reads cores from and looks for firmware in. Without a window: the build, and the folders this "
	  "server was started with.",
	  kNoArguments, false, true },
	{ "list_cores",
	  "The core packages in the cores folder: for each, its file, the name the core gives itself, its version, the machines it "
	  "is and the kinds of file (extensions) it opens.",
	  kNoArguments, false, false },
	{ "describe_core",
	  "What one core declares: its settings, the firmware it may ask for, and the slots a project of it holds files in.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"core\":{\"type\":\"string\",\"description\":\"A file or a name from list_cores.\"}"
	  "},\"required\":[\"core\"],\"additionalProperties\":false}",
	  false, false },
	{ "lua_functions",
	  "Chimera's Lua functions, as the Lua Console documents them: for each, its name, parameters, what it returns and "
	  "what it does. Every one of them can be called with run_lua. 'library' keeps one library, 'search' the functions "
	  "whose name or description holds those words.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"library\":{\"type\":\"string\",\"description\":\"Such as emu, memory, joypad, movie, tastudio, gui.\"},"
	  "\"search\":{\"type\":\"string\"},"
	  "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"description\":\"200 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, true, false, true },
	{ "pause",
	  "Pauses the machine, as System > Pause does, and answers the frame it is on. A machine that is already paused stays so.",
	  kNoArguments, false, true },

	/* The tools that change something. They are refused until the person
	 * using Chimera allows changes, and before the first of them a branch of
	 * the movie as it was is saved. */
	{ "seek",
	  "Moves the playback position to a frame, back or forward, and answers when the machine is there. "
	  "Going back uses the saved states; going forward plays the movie's input. A frame past the movie's end is refused, "
	  "unless 'extend' is true: then frames with no button pressed are added up to it first, as one step in the undo "
	  "history, and the answer says the movie's length before and after.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"extend\":{\"type\":\"boolean\",\"description\":\"Add empty frames to reach a frame past the movie's end. false when left out.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "snapshot",
	  "Keeps the machine as it is now, memory writes and all, in memory, and answers its id: restore goes back to it. "
	  "The movie is not touched. Snapshots last while the project is open.",
	  kNoArguments, false, true },
	{ "restore",
	  "Puts the machine back as a snapshot kept it, on the frame it was taken on, without changing the movie. If the "
	  "movie was edited before that frame since, the machine is not what the movie makes there any more: seek goes back "
	  "to what the movie makes.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"id\":{\"type\":\"string\",\"description\":\"What snapshot answered.\"}"
	  "},\"required\":[\"id\"],\"additionalProperties\":false}",
	  true, true },
	{ "search_steps",
	  "Several rounds of a RAM search in one call: for each step the machine goes to the step's 'frame', as seek does, and "
	  "search_filter runs there with the step's other arguments. It stops at the first step that cannot be done, and says "
	  "which. Answers each step's frame and how many candidates were left after it. search_start begins the search first.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"steps\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":50,\"items\":{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  MCP_FILTER_PROPERTIES
	  "},\"required\":[\"frame\",\"op\"],\"additionalProperties\":false}}"
	  "},\"required\":[\"steps\"],\"additionalProperties\":false}",
	  true, true },
	{ "diff_memory",
	  "Which bytes of a range differ between two frames: the machine goes to frame_a, as seek does, then to frame_b, and "
	  "the answer lists each run of bytes that changed, with what it was and what it became. It leaves the machine on "
	  "frame_b. Up to 16 MiB of memory, and the first 200 runs.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain or bus from list_memory.\"},"
	  MCP_ADDRESS ","
	  "\"length\":{\"type\":[\"integer\",\"string\"],\"description\":\"How many bytes: a number, or hex text.\"},"
	  "\"frame_a\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"frame_b\":{\"type\":\"integer\",\"minimum\":0}"
	  "},\"required\":[\"domain\",\"address\",\"length\",\"frame_a\",\"frame_b\"],\"additionalProperties\":false}",
	  true, true },
	{ "try_input",
	  "Tries input without changing the movie: plays 'frames' frames from the frame the machine is on with input given "
	  "here, which is never written into the movie, reads what is asked for, then puts the machine back exactly where it "
	  "was. The movie, its length, its saved states and the playback position are not touched. Input: 'entries' (lines as "
	  "get_input gives them, one a frame, the last one held for the rest), or 'buttons' held every frame and 'axes'; none "
	  "is no button pressed. 'read' names memory values or properties as watch does, read after the last frame (and "
	  "after every frame with 'each_frame'); 'picture' adds the last frame's picture.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frames\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":3600},"
	  "\"entries\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"minItems\":1,\"maxItems\":3600},"
	  "\"buttons\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"description\":\"Buttons held every frame, by the names controls gives.\"},"
	  "\"axes\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"integer\"},\"description\":\"Axis name to value, every frame.\"},"
	  "\"read\":{\"type\":\"array\",\"maxItems\":32,\"items\":{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\"},\"domain\":{\"type\":\"string\"}," MCP_ADDRESS ","
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"]},"
	  "\"big_endian\":{\"type\":\"boolean\"},\"property\":{\"type\":\"string\"}},\"additionalProperties\":false}},"
	  "\"each_frame\":{\"type\":\"boolean\",\"description\":\"Read after every frame, not only the last. false when left out.\"},"
	  "\"picture\":{\"type\":\"boolean\",\"description\":\"Add the last frame's picture. false when left out.\"},"
	  "\"max_width\":{\"type\":\"integer\",\"minimum\":32,\"maximum\":4096,\"description\":\"The widest picture. 320 when left out.\"}"
	  "},\"required\":[\"frames\"],\"additionalProperties\":false}",
	  true, false },
	{ "trace_memory",
	  "Who touches an address: watches it (or 'length' addresses from it) for reads, writes or instructions run, plays "
	  "'frames' frames of the movie's own input from the frame the machine is on, or until 'max_hits', then puts the "
	  "machine back where it was. Each hit has its frame, the address, the value and its size, and - for a core that says "
	  "which instruction made the access - its address (pc); with 'registers', every register at the access. For a core "
	  "that reports memory accesses (list them with 'scope' names from the core).",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"kind\":{\"type\":\"string\",\"enum\":[\"read\",\"write\",\"exec\"]},"
	  MCP_ADDRESS ","
	  "\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":64,\"description\":\"How many addresses from 'address'. 1 when left out.\"},"
	  "\"scope\":{\"type\":\"string\",\"description\":\"The address space, as the core names it. The first that takes the watch when left out.\"},"
	  "\"frames\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":3600,\"description\":\"60 when left out.\"},"
	  "\"max_hits\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"description\":\"100 when left out.\"},"
	  "\"registers\":{\"type\":\"boolean\",\"description\":\"Every register at each hit. false when left out.\"}"
	  "},\"required\":[\"kind\",\"address\"],\"additionalProperties\":false}",
	  true, false },
	{ "trace_cpu",
	  "The core's own trace of the instructions it runs, for a core that keeps one: plays 'frames' frames of the movie's "
	  "own input from the frame the machine is on, then puts the machine back, and answers the lines traced, those holding "
	  "'filter' only when it is given, up to 'max_lines' ('lines_total' says how many there were).",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frames\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":600,\"description\":\"1 when left out.\"},"
	  "\"filter\":{\"type\":\"string\",\"description\":\"Only lines holding this text.\"},"
	  "\"max_lines\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":5000,\"description\":\"500 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  true, false },
	{ "advance_until",
	  "Plays the movie forward from the frame the machine is on until a value meets a condition, and stops there. "
	  "The value is a memory value (domain, address, type) or a game core's property. 'op' compares it with 'value'; "
	  "the op 'changed' needs no value and stops when it differs from what it was at the start. It stops at the movie's "
	  "end or after max_frames if the condition is never met, and says which.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A domain or bus from list_memory. Give this with 'address', or give 'property'.\"},"
	  MCP_ADDRESS ","
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"},"
	  "\"property\":{\"type\":\"string\",\"description\":\"A game core's named value, in place of domain and address.\"},"
	  "\"op\":{\"type\":\"string\",\"enum\":[\"equal\",\"not_equal\",\"greater\",\"greater_or_equal\",\"less\",\"less_or_equal\",\"changed\"]},"
	  "\"value\":{\"type\":\"number\",\"description\":\"What to compare with. Not needed for 'changed'.\"},"
	  "\"length\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":65536,\"description\":\"With 'changed': watch this many bytes from 'address', and stop when any of them changes.\"},"
	  "\"max_frames\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"description\":\"The most frames to play. 600 when left out.\"}"
	  "},\"required\":[\"op\"],\"additionalProperties\":false}",
	  true, true },
	{ "set_input",
	  "Replaces the movie's input from a frame on, one line a frame in the form get_input returns. "
	  "Frames past the movie's end are added. It is one step in the undo history. At most 1000 frames a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first frame to replace.\"},"
	  "\"entries\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"minItems\":1,\"maxItems\":1000}"
	  "},\"required\":[\"frame\",\"entries\"],\"additionalProperties\":false}",
	  true, true },
	{ "set_buttons",
	  "Presses or releases buttons, and sets axes, over a range of frames, by the names 'controls' gives. "
	  "Controls not named keep what they had. It is one step in the undo history.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first frame.\"},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10000,\"description\":\"How many frames. 1 when left out.\"},"
	  "\"buttons\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"boolean\"},\"description\":\"Button name to pressed (true) or released (false).\"},"
	  "\"axes\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"integer\"},\"description\":\"Axis name to value.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "press_buttons",
	  "Presses buttons on a schedule, written into the movie's input as one step in the undo history: each press names "
	  "its buttons, the frame it starts on ('frame', or 'after' this many frames from the frame the machine is on) and "
	  "how many frames it is held. Past the movie's end frames are added. Buttons not named keep what they had. "
	  "'resume', 'seek' or 'capture_series' then plays them. 'status' and 'controls' give the buttons' names.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"presses\":{\"type\":\"array\",\"minItems\":1,\"maxItems\":1000,\"items\":{\"type\":\"object\",\"properties\":{"
	  "\"buttons\":{\"type\":\"array\",\"items\":{\"type\":\"string\"},\"minItems\":1},"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The frame the press starts on.\"},"
	  "\"after\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"Or: this many frames after the frame the machine is on.\"},"
	  "\"hold\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10000,\"description\":\"Frames the buttons are held. 6 when left out.\"}"
	  "},\"required\":[\"buttons\"],\"additionalProperties\":false}}"
	  "},\"required\":[\"presses\"],\"additionalProperties\":false}",
	  true, true },
	{ "capture_series",
	  "Plays the movie and takes a picture on each of 'count' frames: the first 'first' from the frame the machine is on "
	  "(one interval when left out), then one every 'every'. Both count frames, or seconds of the machine's own time "
	  "when 'unit' is 'seconds', so the timing does not depend on how fast anything runs. Past the movie's end empty "
	  "frames are added first, one step in the undo history. Each picture says whether it is blank (one colour covers "
	  "99% of it or more), its commonest colour and how much it covers. With 'folder', each picture is also written there "
	  "as a PNG at its own size; a file that is already there is not replaced. Answers when the last is taken.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"every\":{\"type\":\"number\",\"exclusiveMinimum\":0,\"description\":\"Frames (or seconds) between pictures.\"},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":50},"
	  "\"first\":{\"type\":\"number\",\"minimum\":0,\"description\":\"Frames (or seconds) before the first picture. 'every' when left out; 0 takes the frame the machine is on.\"},"
	  "\"unit\":{\"type\":\"string\",\"enum\":[\"frames\",\"seconds\"],\"description\":\"frames when left out.\"},"
	  "\"max_width\":{\"type\":\"integer\",\"minimum\":32,\"maximum\":4096,\"description\":\"The widest picture in the answer. 320 when left out.\"},"
	  "\"folder\":{\"type\":\"string\",\"description\":\"A folder that exists, to write the pictures to as PNG files.\"},"
	  "\"name\":{\"type\":\"string\",\"description\":\"The files' names: {n} is the picture's number, {frame} its frame. shot-{n}.png when left out.\"},"
	  "\"images\":{\"type\":\"boolean\",\"description\":\"Put the pictures in the answer. true when left out.\"}"
	  "},\"required\":[\"every\",\"count\"],\"additionalProperties\":false}",
	  true, true },
	{ "insert_frames",
	  "Inserts empty frames before a frame; the input from there on moves later.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"description\":\"1 when left out.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "delete_frames",
	  "Deletes frames from a frame on; the input after them moves earlier.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"count\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":100000,\"description\":\"1 when left out.\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "undo",
	  "Undoes the last step in the movie's undo history, whoever made it: the same as Edit > Undo in TAStudio.",
	  kNoArguments, true, true },
	{ "save_branch",
	  "Saves a branch: the movie's input and the machine as they are now, to come back to later.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"note\":{\"type\":\"string\",\"description\":\"A note shown beside the branch.\"}"
	  "},\"additionalProperties\":false}",
	  true, true },
	{ "load_branch",
	  "Loads a branch by its number in list_branches: the movie's input and the machine become what the branch saved, "
	  "and so does the movie's length, which the answer gives before and after.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"number\":{\"type\":\"integer\",\"minimum\":1}"
	  "},\"required\":[\"number\"],\"additionalProperties\":false}",
	  true, true },
	{ "add_marker",
	  "Places a marker with a text on a frame.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"frame\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"text\":{\"type\":\"string\"}"
	  "},\"required\":[\"frame\"],\"additionalProperties\":false}",
	  true, true },
	{ "resume",
	  "Lets the machine run, as unpausing does: TAStudio plays the movie, and past its end every frame played is added to "
	  "it, as when a person plays on. Answers at once with the frame it started from; 'pause' stops it, and 'status' says "
	  "where it is.",
	  kNoArguments, true, true, false, true },
	{ "save_project",
	  "Saves the open project: where it was last saved or opened when no path is given, else at 'path'. A file that is "
	  "already at 'path' is not replaced unless 'overwrite' is true; nothing asks. Without a window the path must be inside "
	  "the work folder.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"path\":{\"type\":\"string\",\"description\":\"Where to save it, a .chimeraProject file. Where it was last saved or opened when left out.\"},"
	  "\"overwrite\":{\"type\":\"boolean\",\"description\":\"Replace a file that is already at 'path'. false when left out.\"}"
	  "},\"additionalProperties\":false}",
	  true, true },
	{ "create_project",
	  "Makes a new project and opens it, as finishing the New Chimera Project window does - with the core's suggested "
	  "settings for the game, then the settings given - and asks nothing. Firmware is looked for where that window looks "
	  "(the Firmware folder, files chosen for the core before) and in 'firmware_folder'; when some is missing nothing is "
	  "made, and the answer lists each missing file with the files that would do. A project already open is closed first, "
	  "and one with changes that are not saved only with 'discard_unsaved'. list_cores and describe_core say what to give.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"core\":{\"type\":\"string\",\"description\":\"A core's name or file, from list_cores.\"},"
	  "\"system\":{\"type\":\"string\",\"description\":\"The machine, for a core that is several: its id or what it is called.\"},"
	  "\"game\":{\"type\":[\"string\",\"array\"],\"items\":{\"type\":\"string\"},\"description\":\"The game's file (or files), each put in the slot that takes its kind of file.\"},"
	  "\"files\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},\"description\":\"Files by slot, as describe_core names the slots.\"},"
	  "\"settings\":{\"type\":\"object\",\"description\":\"Settings by name, as describe_core names them.\"},"
	  "\"firmware\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"string\"},\"description\":\"Firmware id to the path of its file.\"},"
	  "\"firmware_folder\":{\"type\":\"string\",\"description\":\"A folder to look for firmware in as well.\"},"
	  "\"discard_unsaved\":{\"type\":\"boolean\",\"description\":\"Close an open project even with changes that are not saved. false when left out.\"}"
	  "},\"required\":[\"core\"],\"additionalProperties\":false}",
	  true, true, false, true },
	{ "run_lua",
	  "Runs Lua as a line typed into the Lua Console runs, where the person sees it: every Lua function is there "
	  "(lua_functions lists them), and what it returns and prints comes back. A function that waits for a frame "
	  "(emu.frameadvance, emu.yield) is for scripts, as in the Lua Console: lua_script runs one. Code still running after "
	  "'seconds' (10 when left out) is stopped with an error, so a loop that never ends cannot hold Chimera. It can do "
	  "anything a script can, so it is refused until changes are allowed.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"code\":{\"type\":\"string\",\"description\":\"Lua, such as \\\"return emu.framecount()\\\".\"},"
	  "\"seconds\":{\"type\":\"number\",\"minimum\":1,\"maximum\":300,\"description\":\"How long the code may run. 10 when left out.\"}"
	  "},\"required\":[\"code\"],\"additionalProperties\":false}",
	  true, true, false, true },
	{ "lua_script",
	  "Starts a Lua script file in the Lua Console, as File > Open Script does; it then runs frame after frame. With "
	  "'stop' it is stopped and taken out. Answers whether it runs, and what the console printed since the agent last "
	  "asked; a script that failed to start, or stopped on an error, has 'error': the message, with the line.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"path\":{\"type\":\"string\",\"description\":\"The .lua file.\"},"
	  "\"stop\":{\"type\":\"boolean\",\"description\":\"Stop it instead. false when left out.\"}"
	  "},\"required\":[\"path\"],\"additionalProperties\":false}",
	  true, true, false, true },
	{ "lua_console",
	  "What the Lua Console printed, whoever wrote it - the person's scripts too - from line 'since' on: the lines are "
	  "numbered from the window's start, and the last 2000 are kept. Answers the lines, the number of the first, and "
	  "'next', the 'since' to ask with next time. Window only.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"since\":{\"type\":\"integer\",\"minimum\":0,\"description\":\"The first line wanted. The oldest kept when left out.\"},"
	  "\"limit\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"description\":\"200 when left out.\"}"
	  "},\"additionalProperties\":false}",
	  false, true, false, true },
	{ "lua_scripts",
	  "The scripts in the Lua Console's list, the person's and the agent's: each one's name, file, whether it runs, is "
	  "paused or is stopped, and the error it last stopped on. Window only.",
	  kNoArguments, false, true, false, true },
	{ "close_tastudio",
	  "Closes the open project: TAStudio and the machine, as closing TAStudio does. A project with changes that are not "
	  "saved is not closed unless 'discard_unsaved' is true; nothing asks. Without a window, 'close' does this.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"discard_unsaved\":{\"type\":\"boolean\",\"description\":\"Close even with changes that are not saved, which are then lost. false when left out.\"}"
	  "},\"additionalProperties\":false}",
	  true, true, false, true },
	{ "write_memory",
	  "Writes values to a memory domain or a bus at the frame the machine is on. A movie cannot reproduce a memory write: "
	  "it is for testing a guess, and 'status' says from then on that memory was written. A write lasts only until the "
	  "machine goes back before it: a seek back, a branch or a rewind loads a state from before the write, the movie then "
	  "plays without it, and the answer of that call says so. To hold a value through seeks, use 'freeze'. At most 4096 "
	  "bytes a call.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A name from list_memory.\"},"
	  MCP_ADDRESS ","
	  "\"values\":{\"type\":\"array\",\"items\":{\"type\":\"integer\"},\"minItems\":1},"
	  "\"type\":{\"type\":\"string\",\"enum\":[\"u8\",\"s8\",\"u16\",\"s16\",\"u32\",\"s32\"],\"description\":\"u8 when left out.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"}"
	  "},\"required\":[\"domain\",\"address\",\"values\"],\"additionalProperties\":false}",
	  true, false },
	{ "set_property",
	  "Sets one named value of a game core from text: a number, a name the value can take, true or false. "
	  "Like a memory write, a movie cannot reproduce it.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\"},"
	  "\"value\":{\"type\":\"string\"}"
	  "},\"required\":[\"name\",\"value\"],\"additionalProperties\":false}",
	  true, false },
	{ "set_register",
	  "Sets a processor register, for a core that allows it. Like a memory write, a movie cannot reproduce it.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"name\":{\"type\":\"string\",\"description\":\"A name from registers.\"},"
	  "\"value\":{\"type\":\"integer\"}"
	  "},\"required\":[\"name\",\"value\"],\"additionalProperties\":false}",
	  true, false },
	{ "freeze",
	  "Holds a value: it is written into the machine before every frame that runs, a seek's replay included, so unlike "
	  "write_memory it survives going back. A memory value (domain, address, size, value), or a game core's property "
	  "(property, text). With 'when' it is written only while the value now compares so. A freeze already on that place "
	  "is replaced. Freezes are for looking into a game: they are never saved, never in the movie, and the person sees "
	  "them in the Cheats window. Answers the list, as list_freezes does.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"domain\":{\"type\":\"string\",\"description\":\"A name from list_memory. Give this with 'address', or give 'property'.\"},"
	  MCP_ADDRESS ","
	  "\"size\":{\"type\":\"integer\",\"enum\":[1,2,4,8],\"description\":\"Bytes. 1 when left out.\"},"
	  "\"value\":{\"type\":[\"integer\",\"string\"],\"description\":\"The value to hold: a number, or hex text.\"},"
	  "\"big_endian\":{\"type\":\"boolean\",\"description\":\"false when left out.\"},"
	  "\"property\":{\"type\":\"string\",\"description\":\"A game core's named value, in place of domain and address.\"},"
	  "\"text\":{\"type\":\"string\",\"description\":\"The property's value as text: a number, a name it can take, true or false.\"},"
	  "\"when\":{\"type\":\"object\",\"properties\":{"
	  "\"op\":{\"type\":\"string\",\"enum\":[\"equal\",\"not_equal\",\"greater\",\"greater_or_equal\",\"less\",\"less_or_equal\"]},"
	  "\"value\":{\"type\":[\"integer\",\"string\"]}"
	  "},\"required\":[\"op\",\"value\"],\"additionalProperties\":false,"
	  "\"description\":\"Write it only while the value now compares so with 'value' (a memory freeze only).\"}"
	  "},\"additionalProperties\":false}",
	  true, false },
	{ "unfreeze",
	  "Lets go of freezes: one by its index in list_freezes, the one on a place (domain and address, or property), or "
	  "all of them. Answers the list left.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"index\":{\"type\":\"integer\",\"minimum\":0},"
	  "\"domain\":{\"type\":\"string\"},"
	  MCP_ADDRESS ","
	  "\"property\":{\"type\":\"string\"},"
	  "\"all\":{\"type\":\"boolean\"}"
	  "},\"additionalProperties\":false}",
	  true, false },
	{ "list_freezes",
	  "The values held now, the person's own from the Cheats window among them: for each, its index, its place, its "
	  "value, whether it is on, and its condition.",
	  kNoArguments, false, false },

	/* The tools that exist only where there is no window: there the agent
	 * opens a machine of its own, and saves what it made. */
	{ "open_game",
	  "Starts a new project from a core and game files, with an empty movie, and opens its machine. "
	  "A project open before is closed without being saved.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"core\":{\"type\":\"string\",\"description\":\"A file or a name from list_cores.\"},"
	  "\"files\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}},"
	  "\"description\":\"Slot to the paths of the files that fill it, for the slots describe_core gives (\\\"rom\\\" for a core that declares none).\"},"
	  "\"settings\":{\"type\":\"object\",\"description\":\"Settings of the core, by the keys describe_core gives. The core's own where left out.\"},"
	  "\"firmware\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"string\"},\"description\":\"Firmware id to the path of its file.\"},"
	  "\"title\":{\"type\":\"string\"}"
	  "},\"required\":[\"core\",\"files\"],\"additionalProperties\":false}",
	  false, false, true },
	{ "open_project",
	  "Opens a .chimeraProject: its files are looked for beside it and in the folders this server may read, and must be the "
	  "ones it records. A project open before is closed without being saved.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"path\":{\"type\":\"string\"},"
	  "\"firmware\":{\"type\":\"object\",\"additionalProperties\":{\"type\":\"string\"},\"description\":\"Firmware id to the path of its file.\"},"
	  "\"allow_other_core_build\":{\"type\":\"boolean\",\"description\":\"Run it on the build in the cores folder when that is not the one the project names.\"}"
	  "},\"required\":[\"path\"],\"additionalProperties\":false}",
	  false, false, true },
	{ "export_movie",
	  "Writes the movie's input alone to a file inside the work folder, as chimera-run plays it.",
	  "{\"type\":\"object\",\"properties\":{"
	  "\"path\":{\"type\":\"string\"}"
	  "},\"required\":[\"path\"],\"additionalProperties\":false}",
	  false, false, true },
	{ "close",
	  "Closes the open project and its machine. What was not saved is lost.",
	  kNoArguments, false, false, true },
};

bool g_local = false;

std::string printJson(cJSON *item)
{
	char *text = cJSON_PrintUnformatted(item);
	std::string out = text != nullptr ? text : "null";
	cJSON_free(text);
	return out;
}

/* A whole number argument, or `fallback` when it is not given. False when it
 * is given and is not a whole number in [low, high]. */
bool integerArgument(const cJSON *arguments, const char *name, int64_t low, int64_t high, int64_t fallback, int64_t &out)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, name);
	if (item == nullptr || cJSON_IsNull(item)) { out = fallback; return true; }
	/* hex text ("0x2E329"), or decimal text, as well as a number (issue 571) */
	if (cJSON_IsString(item))
	{
		uint64_t value = 0;
		if (!mcpParseInteger(item->valuestring, value) || value > static_cast<uint64_t>(INT64_MAX)) return false;
		out = static_cast<int64_t>(value);
		return out >= low && out <= high;
	}
	if (!cJSON_IsNumber(item)) return false;
	const double value = item->valuedouble;
	if (value != static_cast<double>(static_cast<int64_t>(value))) return false;
	out = static_cast<int64_t>(value);
	return out >= low && out <= high;
}

std::string lowered(std::string text)
{
	for (char &c : text) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
	return text;
}

std::string base64(const uint8_t *data, size_t len)
{
	static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3)
	{
		const uint32_t a = data[i];
		const uint32_t b = i + 1 < len ? data[i + 1] : 0;
		const uint32_t c = i + 2 < len ? data[i + 2] : 0;
		const uint32_t triple = (a << 16) | (b << 8) | c;
		out += table[(triple >> 18) & 63];
		out += table[(triple >> 12) & 63];
		out += i + 1 < len ? table[(triple >> 6) & 63] : '=';
		out += i + 2 < len ? table[triple & 63] : '=';
	}
	return out;
}

} // namespace

bool mcpParseInteger(const std::string &text, uint64_t &value)
{
	size_t at = 0;
	int base = 10;
	if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
	{
		at = 2;
		base = 16;
	}
	if (at == text.size()) return false;
	value = 0;
	for (; at < text.size(); at++)
	{
		const char c = text[at];
		int digit = -1;
		if (c >= '0' && c <= '9') digit = c - '0';
		else if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
		else if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
		if (digit < 0) return false;
		if (value > (UINT64_MAX - static_cast<uint64_t>(digit)) / static_cast<uint64_t>(base)) return false;
		value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(digit);
	}
	return true;
}

std::string mcpHex(uint64_t value)
{
	char text[24];
	std::snprintf(text, sizeof text, "0x%llX", static_cast<unsigned long long>(value));
	return text;
}

void mcpSetLocal(bool local) { g_local = local; }

std::string mcpBuildMismatch(bool fromWindow, const std::string &otherBuild, const std::vector<std::string> *otherTools,
	const std::string &program)
{
	if (!otherBuild.empty() && otherBuild == CE_BUILD_COMMIT) return "";
	std::vector<std::string> mine;
	for (const McpTool &t : kTools) if (!t.local) mine.emplace_back(t.name);
	const std::string windowBuild = fromWindow ? CE_BUILD_COMMIT : otherBuild.empty() ? "older than 2026-11-12" : otherBuild;
	const std::string mcpBuild = fromWindow ? (otherBuild.empty() ? "older than 2026-11-12" : otherBuild) : CE_BUILD_COMMIT;
	std::string out = "WARNING: this chimera-mcp (build " + mcpBuild + ") is not from the same build as the Chimera it works with "
		"(build " + windowBuild + "), so some of its tools may be missing or work differently. ";
	if (otherTools != nullptr)
	{
		const std::vector<std::string> &windowTools = fromWindow ? mine : *otherTools, &mcpTools = fromWindow ? *otherTools : mine;
		std::string missing, gone;
		for (const std::string &t : windowTools)
			if (std::find(mcpTools.begin(), mcpTools.end(), t) == mcpTools.end()) missing += (missing.empty() ? "" : ", ") + t;
		for (const std::string &t : mcpTools)
			if (std::find(windowTools.begin(), windowTools.end(), t) == windowTools.end()) gone += (gone.empty() ? "" : ", ") + t;
		if (!missing.empty()) out += "Chimera has tools this chimera-mcp does not offer: " + missing + ". ";
		if (!gone.empty()) out += "This chimera-mcp offers tools Chimera does not have: " + gone + ". ";
	}
	else if (fromWindow)
		out += "It is older than this Chimera, so it does not offer the tools added since. ";
	out += "Ask the person to point the agent's MCP set-up at the chimera-mcp of the Chimera they run";
	out += program.empty() ? " (in its bin folder; Tools > MCP Server... shows the line), then restart the agent's program."
						   : ": " + program + " (Tools > MCP Server... shows the line), then restart the agent's program.";
	return out;
}

const McpTool *mcpTools(size_t &count)
{
	count = sizeof(kTools) / sizeof(kTools[0]);
	return kTools;
}

bool mcpOffered(const McpTool &tool)
{
	return g_local ? !tool.window : !tool.local;
}

const McpTool *mcpFindTool(const std::string &name)
{
	for (const McpTool &tool : kTools) if (name == tool.name) return &tool;
	return nullptr;
}

std::string mcpTextContent(const std::string &text)
{
	cJSON *content = cJSON_CreateArray();
	cJSON *item = cJSON_CreateObject();
	cJSON_AddStringToObject(item, "type", "text");
	cJSON_AddStringToObject(item, "text", text.c_str());
	cJSON_AddItemToArray(content, item);
	std::string out = printJson(content);
	cJSON_Delete(content);
	return out;
}

McpResult mcpError(const std::string &sentence)
{
	McpResult result;
	result.content = mcpTextContent(sentence);
	result.isError = true;
	return result;
}

/* ---- the protocol ---- */

/* A reply is valid UTF-8, whatever a game's memory held: a property's text is
 * the game's bytes, and an agent's MCP library decodes the stream strictly, so
 * one bad byte ended its session (engine analysis, 2026-10-18). Each byte that
 * does not begin or continue a valid sequence becomes U+FFFD. */
std::string mcpValidUtf8(const std::string &in)
{
	std::string out;
	out.reserve(in.size());
	const auto *p = reinterpret_cast<const unsigned char *>(in.data());
	const size_t n = in.size();
	for (size_t i = 0; i < n;)
	{
		const unsigned char c = p[i];
		size_t len = c < 0x80 ? 1 : (c & 0xE0) == 0xC0 ? 2 : (c & 0xF0) == 0xE0 ? 3 : (c & 0xF8) == 0xF0 ? 4 : 0;
		bool ok = len != 0 && i + len <= n && !(len == 2 && c < 0xC2) && !(len == 4 && c > 0xF4);
		for (size_t k = 1; ok && k < len; k++) ok = (p[i + k] & 0xC0) == 0x80;
		if (ok && len == 3)
		{
			const unsigned c1 = p[i + 1];
			ok = !(c == 0xE0 && c1 < 0xA0) && !(c == 0xED && c1 >= 0xA0);   /* overlong, surrogates */
		}
		if (ok && len == 4)
		{
			const unsigned c1 = p[i + 1];
			ok = !(c == 0xF0 && c1 < 0x90) && !(c == 0xF4 && c1 >= 0x90);
		}
		if (ok) { out.append(in, i, len); i += len; }
		else { out += "\xEF\xBF\xBD"; i++; }
	}
	return out;
}


namespace
{

/* The versions of the protocol this was written against. An agent's program
 * that asks for one of them gets it; any other gets the newest. */
const char *const kProtocolVersions[] = { "2025-06-18", "2025-03-26", "2024-11-05" };

const char kLocalInstructions[] =
	"Chimera is a frontend for tool-assisted speedruns. This server has no window: it opens a machine of its own. "
	"Call 'list_cores', then 'open_game' or 'open_project'; after that the other tools work on that machine and its movie. "
	"It reads games and projects only from the folders it was started with, and writes only inside its work folder.";

const char kInstructions[] =
	"Chimera is a frontend for tool-assisted speedruns. These tools work on the project a person has open in a running "
	"Chimera, after they started its server from Tools > MCP Server.... Call 'status' first. Tools that change the "
	"movie or the machine are refused until the person ticks 'Allow the agent to make changes' there.";

std::string rpcReply(const cJSON *id, cJSON *result)
{
	cJSON *reply = cJSON_CreateObject();
	cJSON_AddStringToObject(reply, "jsonrpc", "2.0");
	cJSON_AddItemToObject(reply, "id", cJSON_Duplicate(id, 1));
	cJSON_AddItemToObject(reply, "result", result);
	std::string out = mcpValidUtf8(printJson(reply));
	cJSON_Delete(reply);
	return out;
}

std::string rpcError(const cJSON *id, int code, const std::string &message)
{
	cJSON *reply = cJSON_CreateObject();
	cJSON_AddStringToObject(reply, "jsonrpc", "2.0");
	cJSON_AddItemToObject(reply, "id", id != nullptr ? cJSON_Duplicate(id, 1) : cJSON_CreateNull());
	cJSON *error = cJSON_CreateObject();
	cJSON_AddNumberToObject(error, "code", code);
	cJSON_AddStringToObject(error, "message", message.c_str());
	cJSON_AddItemToObject(reply, "error", error);
	std::string out = mcpValidUtf8(printJson(reply));
	cJSON_Delete(reply);
	return out;
}

} // namespace

std::string mcpHandleLine(const std::string &line, const McpExecutor &execute, std::string *client)
{
	cJSON *message = cJSON_ParseWithLength(line.data(), line.size());
	if (message == nullptr || !cJSON_IsObject(message))
	{
		cJSON_Delete(message);
		return rpcError(nullptr, -32700, "the line is not a JSON object");
	}
	const cJSON *id = cJSON_GetObjectItemCaseSensitive(message, "id");
	const cJSON *methodItem = cJSON_GetObjectItemCaseSensitive(message, "method");
	const cJSON *params = cJSON_GetObjectItemCaseSensitive(message, "params");
	std::string out;
	if (!cJSON_IsString(methodItem))
	{
		/* an answer to something we never asked, or a malformed request */
		if (id != nullptr && cJSON_GetObjectItemCaseSensitive(message, "result") == nullptr
			&& cJSON_GetObjectItemCaseSensitive(message, "error") == nullptr)
			out = rpcError(id, -32600, "the request names no method");
		cJSON_Delete(message);
		return out;
	}
	const std::string method = methodItem->valuestring;
	if (id == nullptr)
	{
		/* a notification: nothing is sent back, whatever it is */
		cJSON_Delete(message);
		return out;
	}

	if (method == "initialize")
	{
		const cJSON *asked = cJSON_GetObjectItemCaseSensitive(params, "protocolVersion");
		const char *version = kProtocolVersions[0];
		for (const char *known : kProtocolVersions)
		{
			if (cJSON_IsString(asked) && std::strcmp(asked->valuestring, known) == 0) version = known;
		}
		if (client != nullptr)
		{
			const cJSON *info = cJSON_GetObjectItemCaseSensitive(params, "clientInfo");
			const cJSON *name = cJSON_GetObjectItemCaseSensitive(info, "name");
			const cJSON *clientVersion = cJSON_GetObjectItemCaseSensitive(info, "version");
			*client = cJSON_IsString(name) ? name->valuestring : "an agent";
			if (cJSON_IsString(clientVersion)) *client += std::string(" ") + clientVersion->valuestring;
		}
		cJSON *result = cJSON_CreateObject();
		cJSON_AddStringToObject(result, "protocolVersion", version);
		cJSON *capabilities = cJSON_CreateObject();
		cJSON_AddItemToObject(capabilities, "tools", cJSON_CreateObject());
		cJSON_AddItemToObject(result, "capabilities", capabilities);
		cJSON *server = cJSON_CreateObject();
		cJSON_AddStringToObject(server, "name", "chimera");
		cJSON_AddStringToObject(server, "title", "Chimera");
		cJSON_AddStringToObject(server, "version", CE_BUILD_COMMIT);
		cJSON_AddItemToObject(result, "serverInfo", server);
		cJSON_AddStringToObject(result, "instructions", g_local ? kLocalInstructions : kInstructions);
		out = rpcReply(id, result);
	}
	else if (method == "ping")
	{
		out = rpcReply(id, cJSON_CreateObject());
	}
	else if (method == "tools/list")
	{
		cJSON *list = cJSON_CreateArray();
		for (const McpTool &tool : kTools)
		{
			/* a tool for where there is no window is not offered where there is one, and the other way round */
			if (!mcpOffered(tool)) continue;
			cJSON *item = cJSON_CreateObject();
			cJSON_AddStringToObject(item, "name", tool.name);
			cJSON_AddStringToObject(item, "description", tool.description);
			cJSON_AddItemToObject(item, "inputSchema", cJSON_Parse(tool.schema));
			cJSON *annotations = cJSON_CreateObject();
			cJSON_AddBoolToObject(annotations, "readOnlyHint", !tool.changes);
			cJSON_AddItemToObject(item, "annotations", annotations);
			cJSON_AddItemToArray(list, item);
		}
		cJSON *result = cJSON_CreateObject();
		cJSON_AddItemToObject(result, "tools", list);
		out = rpcReply(id, result);
	}
	else if (method == "tools/call")
	{
		const cJSON *name = cJSON_GetObjectItemCaseSensitive(params, "name");
		const cJSON *arguments = cJSON_GetObjectItemCaseSensitive(params, "arguments");
		if (!cJSON_IsString(name))
		{
			out = rpcError(id, -32602, "tools/call needs the tool's name");
		}
		else if (mcpFindTool(name->valuestring) == nullptr || !mcpOffered(*mcpFindTool(name->valuestring)))
		{
			out = rpcError(id, -32602, std::string("there is no tool called '") + name->valuestring + "'");
		}
		else
		{
			std::string argumentsText = "{}";
			if (cJSON_IsObject(arguments))
			{
				cJSON *copy = cJSON_Duplicate(arguments, 1);
				argumentsText = printJson(copy);
				cJSON_Delete(copy);
			}
			const McpResult ran = execute(name->valuestring, argumentsText);
			cJSON *result = cJSON_CreateObject();
			cJSON *content = cJSON_Parse(ran.content.c_str());
			cJSON_AddItemToObject(result, "content", content != nullptr ? content : cJSON_CreateArray());
			cJSON_AddBoolToObject(result, "isError", ran.isError);
			out = rpcReply(id, result);
		}
	}
	else
	{
		out = rpcError(id, -32601, "this server does not know the method '" + method + "'");
	}
	cJSON_Delete(message);
	return out;
}

/* ---- the link from chimera-mcp to a running Chimera ----
 *
 * Lines on the pipe, each one JSON object:
 *   chimera-mcp -> Chimera   {"hello":"<the agent's program>"}
 *                            {"id":N,"tool":"<name>","arguments":{...}}
 *   Chimera -> chimera-mcp   {"hello":"chimera"}
 *                            {"id":N,"started":true}      the call was taken up
 *                            {"id":N,"content":[...],"isError":false}
 */

McpResult McpLink::call(const std::string &tool, const std::string &argumentsJson)
{
	const char *const howToStart = " In Chimera, open Tools > MCP Server... and press Start, then call the tool again.";
	std::string line;
	const uint64_t id = nextId++;
	cJSON *request = cJSON_CreateObject();
	cJSON_AddNumberToObject(request, "id", static_cast<double>(id));
	cJSON_AddStringToObject(request, "tool", tool.c_str());
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	cJSON_AddItemToObject(request, "arguments", arguments != nullptr ? arguments : cJSON_CreateObject());
	const std::string requestLine = printJson(request);
	cJSON_Delete(request);

	/* A connection kept from an earlier call may lead to a server that has
	 * since been stopped, or to a Chimera that was closed and opened again.
	 * That shows when the request cannot be sent, and nothing was run, so the
	 * pipe is opened afresh and the request sent once more. */
	for (int attempt = 0;; attempt++)
	{
		const bool fresh = !pipe.connected();
		if (fresh)
		{
			std::string error;
			if (!pipe.connect(error)) return mcpError(error + "." + howToStart);
			cJSON *hello = cJSON_CreateObject();
			cJSON_AddStringToObject(hello, "hello", client.empty() ? "an agent" : client.c_str());
			/* which build this is and its tools, so the window can tell an agent whose chimera-mcp is old */
			cJSON_AddStringToObject(hello, "build", CE_BUILD_COMMIT);
			cJSON *names = cJSON_CreateArray();
			for (const McpTool &t : kTools) if (!t.local) cJSON_AddItemToArray(names, cJSON_CreateString(t.name));
			cJSON_AddItemToObject(hello, "tools", names);
			const bool greeted = pipe.send(printJson(hello));
			cJSON_Delete(hello);
			/* Chimera answers the greeting from its own thread, at once */
			/* ...and only that line counts: on Linux a client that arrives
			 * before the server noticed the last one leave can be handed lines
			 * meant for it, which are skipped here (engine analysis,
			 * 2026-10-18) */
			PipeEvent event = PipeEvent::Nothing;
			const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
			bool answered = false;
			while (greeted && !answered && event != PipeEvent::Disconnected && std::chrono::steady_clock::now() < until)
			{
				event = pipe.wait(100, line);
				answered = event == PipeEvent::Line && line.find("\"hello\"") != std::string::npos
					&& line.find("\"chimera\"") != std::string::npos;
			}
			if (!answered)
			{
				pipe.close();
				return mcpError(std::string("Chimera's MCP server did not answer.") + howToStart);
			}
			/* the window's build and tools; one from before 2026-11-12 says neither */
			cJSON *greeting = cJSON_ParseWithLength(line.data(), line.size());
			const cJSON *build = cJSON_GetObjectItemCaseSensitive(greeting, "build");
			const cJSON *tools = cJSON_GetObjectItemCaseSensitive(greeting, "tools");
			std::vector<std::string> theirs;
			if (cJSON_IsArray(tools))
			{
				const cJSON *t = nullptr;
				cJSON_ArrayForEach(t, tools) if (cJSON_IsString(t)) theirs.emplace_back(t->valuestring);
			}
			const cJSON *program = cJSON_GetObjectItemCaseSensitive(greeting, "program");
			mismatch = mcpBuildMismatch(false, cJSON_IsString(build) ? build->valuestring : "", cJSON_IsArray(tools) ? &theirs : nullptr,
				cJSON_IsString(program) ? program->valuestring : "");
			cJSON_Delete(greeting);
		}
		if (pipe.send(requestLine)) break;
		pipe.close();
		if (fresh || attempt > 0)
			return mcpError(std::string("Chimera's MCP server was stopped, or Chimera was closed.") + howToStart);
	}

	bool started = false;
	const auto takeUpBy = std::chrono::steady_clock::now() + std::chrono::milliseconds(kTakeUpMs);
	for (;;)
	{
		const PipeEvent event = pipe.wait(200, line);
		if (event == PipeEvent::Disconnected)
		{
			pipe.close();
			return mcpError(started
				? "Chimera's MCP server was stopped, or Chimera was closed, while the call was running. Whether it finished is not known."
				: std::string("Chimera's MCP server was stopped, or Chimera was closed, before the call was run.") + howToStart);
		}
		if (event == PipeEvent::Line)
		{
			cJSON *reply = cJSON_ParseWithLength(line.data(), line.size());
			const cJSON *replyId = cJSON_GetObjectItemCaseSensitive(reply, "id");
			if (cJSON_IsNumber(replyId) && static_cast<uint64_t>(replyId->valuedouble) == id)
			{
				cJSON *content = cJSON_GetObjectItemCaseSensitive(reply, "content");
				if (content != nullptr)
				{
					if (!mismatch.empty() && cJSON_IsArray(content))
					{
						cJSON *note = cJSON_CreateObject();
						cJSON_AddStringToObject(note, "type", "text");
						cJSON_AddStringToObject(note, "text", mismatch.c_str());
						cJSON_AddItemToArray(content, note);
					}
					McpResult result;
					result.content = printJson(content);
					result.isError = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "isError"));
					cJSON_Delete(reply);
					return result;
				}
				if (cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(reply, "started"))) started = true;
			}
			cJSON_Delete(reply); /* a line for an older call is dropped */
			continue;
		}
		if (!started && std::chrono::steady_clock::now() >= takeUpBy)
		{
			return mcpError("Chimera is busy and did not take the call within 30 seconds, so it was not run. "
				"A dialog may be open in Chimera. Call the tool again when it is closed.");
		}
	}
}

/* ---- the part in the running Chimera ---- */

namespace
{

/* A value of the machine an agent names: a place in memory, or a game core's
 * property. What `watch` lists and what advance_until's condition reads. */
struct Source
{
	std::string name;
	bool isProperty = false;
	std::string property;
	std::string domain;
	int64_t address = 0;
	int width = 1;
	bool isSigned = false;
	bool bigEndian = false;
	/* advance_until's 'length': this many bytes from the address, watched
	 * for any of them changing (issue 571); 0 for one value */
	int64_t length = 0;
};

struct WaitingCall
{
	uint64_t id = 0;
	/* which connection asked: every client numbers its calls from 1, so an
	 * answer finished after its client left must not reach the next one */
	uint64_t connection = 0;
	std::string tool;
	std::string arguments;
	std::string prepared;  /* the arguments as the frontend was handed them, for a call it runs */
	std::chrono::steady_clock::time_point at;
};

/* The pictures of a capture_series the frontend is playing, taken so far. */
struct ShotsTaken
{
	McpShotPlan plan;
	std::vector<std::string> facts, pictures;
};

/* The server in a running Chimera. One for the process, made on first use and
 * never destroyed: it owns a thread, and a thread's destructor at the moment
 * a library is unloaded is not something to rely on (thread_string.hpp has
 * the history of that kind of trouble). */
struct Service
{
	std::mutex lock;                   /* everything below, except where said */
	std::thread thread;
	std::atomic<bool> running{ false };
	PipeServer pipe;                   /* the thread's alone while it runs */
	std::string endpoint;
	int state = CE_MCP_STOPPED;
	std::string client;
	/* the connected chimera-mcp's build ("" when it did not say: older than
	 * 2026-11-12), the warning every answer then carries ("" when none), and
	 * the window's own chimera-mcp, which that warning names */
	std::string clientBuild, mismatch, program;
	std::deque<WaitingCall> waiting;   /* read by the thread, run by the caller of ce_mcp_service */
	std::deque<std::string> outgoing;  /* lines for the thread to send */
	std::atomic<int> pending{ 0 };
	std::atomic<bool> allowChanges{ false };
	std::map<uint64_t, WaitingCall> withHost; /* calls the frontend is running */
	std::map<uint64_t, ShotsTaken> shots;     /* by the call: capture_series' pictures so far */
	std::vector<std::string> coresFolders;    /* where the frontend finds core packages, for list_cores */
	uint64_t connection = 0;           /* moved on by every connect and disconnect */
	std::deque<std::string> log;
	uint64_t logTotal = 0;
	int64_t wroteMemoryAt = -1;        /* the frame of the agent's first write to the machine, -1 for none */
	/* the frames the agent wrote the machine on that no state load has undone
	 * yet, lowest first, and what the call being answered is to say of the
	 * ones that were (lostWrites) */
	std::vector<int64_t> writeFrames;
	std::string lostNote;
	/* Before the agent's first change the frontend is asked to keep the
	 * person's work as it was (a branch). 0 not asked yet, 1 asked and
	 * waiting, 2 kept, 3 could not be kept - and then nothing is changed. */
	int kept = 0;
	uint64_t keepId = 0;
	std::string keepError;
	/* The agent's RAM search. It reads the machine's memory through a pointer,
	 * so what it was started on is remembered and checked before every use:
	 * the caller of ce_mcp_service's alone, like the machine. */
	std::vector<Source> watches;
	/* advance_until's condition, set by the frontend when it begins to play
	 * and asked once per frame until it is met */
	bool conditionSet = false;
	Source conditionSource;
	int conditionOp = 0;               /* 0 ==, 1 >, 2 >=, 3 <, 4 <=, 5 !=, 6 changed */
	double conditionValue = 0;
	std::string conditionStart;        /* the value at the start, as text, for "changed" */
	ce_ramsearch *search = nullptr;
	std::string searchDomain;
	const uint8_t *searchBase = nullptr;
	int64_t searchBytes = 0;
	uint64_t searchSession = 0; /* ce_session_serial of the session it was started in */
	uint64_t machineSession = 0; /* the session the notes above are about (onMachine) */
	int searchSize = 1;
	bool searchBigEndian = false;
	/* what a candidate's previous value is (engine.h, RAM Search: 0 original,
	 * 1 last search, 2 last frame, 3 last change), which every frame's count
	 * of changes keeps to (searchFrame) */
	int searchPreviousType = 1;
	/* diff_memory's range as frame_a had it, until frame_b is reached */
	std::vector<uint8_t> diffBefore;
	int64_t diffFrame = -1;
	/* try_input is running frames that are not the movie's: not counted by the search */
	bool trying = false;
};

Service &service()
{
	static Service *const one = new Service();
	return *one;
}

const size_t kLogKept = 5000;

std::string shortened(const std::string &text, size_t limit)
{
	std::string out;
	for (char c : text) out += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
	if (out.size() > limit) out = out.substr(0, limit) + "...";
	return out;
}

/* with the lock held */
void addLog(Service &sv, const std::string &text)
{
	char stamp[16] = "";
	const std::time_t now = std::time(nullptr);
	struct tm parts;
#ifdef _WIN32
	localtime_s(&parts, &now);
#else
	localtime_r(&now, &parts);
#endif
	std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &parts);
	sv.log.push_back(std::string(stamp) + "  " + text);
	sv.logTotal++;
	if (sv.log.size() > kLogKept) sv.log.pop_front();
}

/* The first text of a content array, for the log. */
std::string contentSummary(const std::string &content)
{
	cJSON *parsed = cJSON_Parse(content.c_str());
	std::string out;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, parsed)
	{
		const cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "type");
		const cJSON *text = cJSON_GetObjectItemCaseSensitive(item, "text");
		if (cJSON_IsString(text)) { out = text->valuestring; break; }
		if (cJSON_IsString(type)) out = std::string("(") + type->valuestring + ")";
	}
	cJSON_Delete(parsed);
	return out;
}

/* with the lock held: queues the answer and writes the log line */
void finish(Service &sv, const WaitingCall &call, const McpResult &result)
{
	if (call.connection != sv.connection)
	{
		addLog(sv, call.tool + " " + shortened(call.arguments, 120) + "  ->  not answered: the agent that asked has gone");
		return;
	}
	cJSON *reply = cJSON_CreateObject();
	cJSON_AddNumberToObject(reply, "id", static_cast<double>(call.id));
	cJSON *content = cJSON_Parse(result.content.c_str());
	if (content == nullptr) content = cJSON_CreateArray();
	if (!sv.mismatch.empty() && cJSON_IsArray(content))
	{
		cJSON *note = cJSON_CreateObject();
		cJSON_AddStringToObject(note, "type", "text");
		cJSON_AddStringToObject(note, "text", sv.mismatch.c_str());
		cJSON_AddItemToArray(content, note);
	}
	cJSON_AddItemToObject(reply, "content", content);
	cJSON_AddBoolToObject(reply, "isError", result.isError);
	sv.outgoing.push_back(printJson(reply));
	cJSON_Delete(reply);
	const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - call.at).count();
	addLog(sv, call.tool + " " + shortened(call.arguments, 120) + "  ->  "
		+ (result.isError ? "REFUSED: " : "") + shortened(contentSummary(result.content), 160)
		+ "  (" + std::to_string(ms) + " ms)");
}

void pipeThread(Service *sv)
{
	std::string line;
	while (sv->running.load())
	{
		/* a short wait, so an answer queued meanwhile leaves within
		 * milliseconds; the cost is a few hundred wake-ups a second, and only
		 * while the server is started */
		const PipeEvent event = sv->pipe.wait(4, line);
		std::vector<std::string> toSend;
		{
			std::lock_guard<std::mutex> hold(sv->lock);
			if (event == PipeEvent::Connected)
			{
				sv->connection++;
				sv->state = CE_MCP_CONNECTED;
				sv->client = "an agent";
				addLog(*sv, "an agent's program connected");
			}
			else if (event == PipeEvent::Disconnected)
			{
				sv->connection++;
				sv->state = CE_MCP_WAITING;
				sv->pending -= static_cast<int>(sv->waiting.size());
				sv->waiting.clear();
				sv->outgoing.clear();
				/* a call the frontend is still running has nobody to answer to */
				sv->withHost.clear();
				sv->shots.clear();
				addLog(*sv, sv->client + " disconnected");
				sv->client.clear();
				sv->clientBuild.clear();
				sv->mismatch.clear();
			}
			else if (event == PipeEvent::Line)
			{
				cJSON *message = cJSON_ParseWithLength(line.data(), line.size());
				const cJSON *hello = cJSON_GetObjectItemCaseSensitive(message, "hello");
				const cJSON *id = cJSON_GetObjectItemCaseSensitive(message, "id");
				const cJSON *tool = cJSON_GetObjectItemCaseSensitive(message, "tool");
				if (cJSON_IsString(hello))
				{
					/* A greeting begins a connection, whether or not the last one
					 * was seen to end: on Linux a client that opens the pipe
					 * before the server's next look is not seen to leave, and
					 * the old one's queued calls and answers went to the new one
					 * (engine analysis, 2026-10-18). */
					sv->connection++;
					sv->pending -= static_cast<int>(sv->waiting.size());
					sv->waiting.clear();
					sv->outgoing.clear();
					sv->withHost.clear();
				sv->shots.clear();
					sv->client = hello->valuestring;
					addLog(*sv, "the agent's program is " + sv->client);
					/* A chimera-mcp that names its build compares it with this
					 * one's itself and warns the agent; one that does not is from
					 * before that, and the warning is added here (issue 571: an
					 * agent spent a session working round tools it did not know
					 * its chimera-mcp was too old to offer) */
					const cJSON *build = cJSON_GetObjectItemCaseSensitive(message, "build");
					sv->clientBuild = cJSON_IsString(build) ? build->valuestring : "";
					sv->mismatch = cJSON_IsString(build) ? "" : mcpBuildMismatch(true, "", nullptr, sv->program);
					if (!sv->mismatch.empty()) addLog(*sv, "its chimera-mcp is older than this Chimera: every answer says so");
					else if (sv->clientBuild != CE_BUILD_COMMIT)
						addLog(*sv, "its chimera-mcp is build " + sv->clientBuild + ", this Chimera " + CE_BUILD_COMMIT);
					cJSON *answer = cJSON_CreateObject();
					cJSON_AddStringToObject(answer, "hello", "chimera");
					cJSON_AddStringToObject(answer, "build", CE_BUILD_COMMIT);
					cJSON *names = cJSON_CreateArray();
					for (const McpTool &t : kTools) if (!t.local) cJSON_AddItemToArray(names, cJSON_CreateString(t.name));
					cJSON_AddItemToObject(answer, "tools", names);
					if (!sv->program.empty()) cJSON_AddStringToObject(answer, "program", sv->program.c_str());
					sv->outgoing.push_back(printJson(answer));
					cJSON_Delete(answer);
				}
				else if (cJSON_IsNumber(id) && cJSON_IsString(tool))
				{
					WaitingCall call;
					call.id = static_cast<uint64_t>(id->valuedouble);
					call.connection = sv->connection;
					call.tool = tool->valuestring;
					cJSON *arguments = cJSON_GetObjectItemCaseSensitive(message, "arguments");
					call.arguments = cJSON_IsObject(arguments) ? printJson(arguments) : "{}";
					call.at = std::chrono::steady_clock::now();
					sv->waiting.push_back(std::move(call));
					sv->pending++;
				}
				cJSON_Delete(message);
			}
			toSend.assign(sv->outgoing.begin(), sv->outgoing.end());
			sv->outgoing.clear();
		}
		for (const std::string &out : toSend) sv->pipe.send(out);
	}
}

/* ---- the tools the engine runs itself ---- */

const char kNoMachine[] = "No project is open in Chimera, so there is no machine to ask.";

McpResult textResult(cJSON *object)
{
	McpResult result;
	result.content = mcpTextContent(printJson(object));
	cJSON_Delete(object);
	return result;
}

McpResult toolStatus(ce_session *s, const ce_movie_log *log, int64_t frame, const char *hostStatus, bool allowChanges, int64_t wroteMemoryAt,
	bool names)
{
	/* what the frontend knows (the project's name, whether it is paused)
	 * comes first, and the engine's own facts are added to it */
	cJSON *out = hostStatus != nullptr ? cJSON_Parse(hostStatus) : nullptr;
	if (out == nullptr || !cJSON_IsObject(out))
	{
		cJSON_Delete(out);
		out = cJSON_CreateObject();
	}
	cJSON_AddBoolToObject(out, "machine_open", s != nullptr);
	cJSON_AddBoolToObject(out, "changes_allowed", allowChanges);
	if (s != nullptr)
	{
		cJSON_AddStringToObject(out, "core", ce_session_core_name(s));
		cJSON_AddStringToObject(out, "system", ce_session_system_name(s));
		cJSON_AddNumberToObject(out, "frame", static_cast<double>(frame >= 0 ? frame : ce_session_frame(s)));
		if (log != nullptr) cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(ce_movie_log_count(log)));
		cJSON *picture = cJSON_CreateObject();
		cJSON_AddNumberToObject(picture, "width", ce_session_video_width(s));
		cJSON_AddNumberToObject(picture, "height", ce_session_video_height(s));
		cJSON_AddItemToObject(out, "picture", picture);
		/* the names set_buttons and press_buttons take, for the controls this
		 * machine has plugged in, when asked: the call an agent makes most is
		 * short without them (issue 571); 'controls' says more of each */
		if (names)
		{
			cJSON *buttons = cJSON_AddArrayToObject(out, "buttons");
			for (int64_t i = 0; i < ce_session_button_count(s); i++)
				if (ce_session_button_active(s, i) != 0) cJSON_AddItemToArray(buttons, cJSON_CreateString(ce_session_button_name(s, i)));
			cJSON *axes = cJSON_AddArrayToObject(out, "axes");
			for (int64_t i = 0; i < ce_session_axis_count(s); i++)
				if (ce_session_axis_active(s, i) != 0) cJSON_AddItemToArray(axes, cJSON_CreateString(ce_session_axis_name(s, i)));
		}
		const int32_t numerator = ce_session_vsync_numerator(s);
		const int32_t denominator = ce_session_vsync_denominator(s);
		if (denominator > 0) cJSON_AddNumberToObject(out, "frames_per_second", static_cast<double>(numerator) / denominator);
		const char *death = ce_session_guest_death(s);
		if (death != nullptr) cJSON_AddStringToObject(out, "core_stopped", death);
		else cJSON_AddBoolToObject(out, "core_stopped", false);
		/* a movie cannot reproduce a write to the machine, so it is said for as
		 * long as the server stays started */
		cJSON_AddBoolToObject(out, "memory_written_by_agent", wroteMemoryAt >= 0);
		if (wroteMemoryAt >= 0) cJSON_AddNumberToObject(out, "first_memory_write_frame", static_cast<double>(wroteMemoryAt));
	}
	return textResult(out);
}

McpResult toolControls(ce_session *s, const ce_movie_log *log)
{
	cJSON *out = cJSON_CreateObject();
	cJSON *buttons = cJSON_CreateArray();
	for (int64_t i = 0; i < ce_session_button_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_button_name(s, i));
		cJSON_AddBoolToObject(item, "active", ce_session_button_active(s, i) != 0);
		cJSON_AddItemToArray(buttons, item);
	}
	cJSON *axes = cJSON_CreateArray();
	for (int64_t i = 0; i < ce_session_axis_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_axis_name(s, i));
		cJSON_AddNumberToObject(item, "rest", ce_session_axis_neutral(s, i));
		cJSON_AddBoolToObject(item, "active", ce_session_axis_active(s, i) != 0);
		cJSON_AddItemToArray(axes, item);
	}
	cJSON_AddItemToObject(out, "buttons", buttons);
	cJSON_AddItemToObject(out, "axes", axes);
	const char *key = log != nullptr ? ce_movie_log_key(log) : nullptr;
	if (key != nullptr) cJSON_AddStringToObject(out, "key", key);
	return textResult(out);
}

McpResult toolGetInput(const ce_movie_log *log, const cJSON *arguments)
{
	if (log == nullptr) return mcpError("No movie is open in Chimera.");
	int64_t from = 0, count = 0;
	if (!integerArgument(arguments, "from", 0, INT64_MAX, 0, from)) return mcpError("'from' must be a frame number, 0 or more.");
	if (!integerArgument(arguments, "count", 1, 1000, 100, count)) return mcpError("'count' must be between 1 and 1000.");
	const int64_t length = ce_movie_log_count(log);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "movie_length", static_cast<double>(length));
	cJSON_AddNumberToObject(out, "from", static_cast<double>(from));
	const char *key = ce_movie_log_key(log);
	if (key != nullptr) cJSON_AddStringToObject(out, "key", key);
	cJSON *entries = cJSON_CreateArray();
	for (int64_t frame = from; frame < length && frame < from + count; frame++)
	{
		const char *entry = ce_movie_log_entry(log, frame);
		cJSON_AddItemToArray(entries, cJSON_CreateString(entry != nullptr ? entry : ""));
	}
	cJSON_AddNumberToObject(out, "count", cJSON_GetArraySize(entries));
	cJSON_AddItemToObject(out, "entries", entries);
	return textResult(out);
}

/* The picture as a PNG, made smaller by a whole number until it is no wider
 * than maxWidth: every pixel of the result is the average of a square of the
 * picture's. */
bool pngOf(const uint32_t *bgra, int32_t width, int32_t height, int64_t maxWidth, std::string &png, int32_t &outWidth, int32_t &outHeight)
{
	int32_t step = 1;
	while (width / step > maxWidth) step++;
	outWidth = width / step;
	outHeight = std::max(1, height / step);
	std::vector<uint8_t> rgb(static_cast<size_t>(outWidth) * outHeight * 3);
	for (int32_t y = 0; y < outHeight; y++)
	{
		for (int32_t x = 0; x < outWidth; x++)
		{
			uint32_t sum[3] = { 0, 0, 0 };
			uint32_t seen = 0;
			for (int32_t dy = 0; dy < step && y * step + dy < height; dy++)
			{
				const uint32_t *row = bgra + static_cast<size_t>(y * step + dy) * width + static_cast<size_t>(x) * step;
				for (int32_t dx = 0; dx < step; dx++)
				{
					sum[0] += (row[dx] >> 16) & 0xFF;
					sum[1] += (row[dx] >> 8) & 0xFF;
					sum[2] += row[dx] & 0xFF;
					seen++;
				}
			}
			uint8_t *pixel = &rgb[(static_cast<size_t>(y) * outWidth + x) * 3];
			for (int c = 0; c < 3; c++) pixel[c] = static_cast<uint8_t>(sum[c] / seen);
		}
	}
	size_t pngLen = 0;
	void *made = tdefl_write_image_to_png_file_in_memory_ex(rgb.data(), outWidth, outHeight, 3, &pngLen, 6, MZ_FALSE);
	if (made == nullptr) return false;
	png.assign(static_cast<const char *>(made), pngLen);
	mz_free(made);
	return true;
}

/* A picture one colour covers this much of, or more, is blank: a black
 * screen, or one solid colour where a game should be (issue 355). */
const double kBlankCoverage = 0.99;

/* What a picture looks like as a whole: its commonest colour, as #RRGGBB,
 * how much of the picture it covers, and so whether the picture is blank. */
void addLook(cJSON *facts, const uint32_t *bgra, int32_t width, int32_t height)
{
	std::unordered_map<uint32_t, uint32_t> counts;
	const size_t total = static_cast<size_t>(width) * height;
	uint32_t commonest = 0, most = 0;
	for (size_t i = 0; i < total; i++)
	{
		const uint32_t colour = bgra[i] & 0xFFFFFF;
		const uint32_t seen = ++counts[colour];
		if (seen > most)
		{
			most = seen;
			commonest = colour;
		}
	}
	const double coverage = total != 0 ? static_cast<double>(most) / static_cast<double>(total) : 0.0;
	char hex[8];
	std::snprintf(hex, sizeof hex, "#%06X", static_cast<unsigned>(commonest));
	cJSON_AddBoolToObject(facts, "blank", coverage >= kBlankCoverage);
	cJSON_AddStringToObject(facts, "dominant_colour", hex);
	cJSON_AddNumberToObject(facts, "coverage", std::floor(coverage * 1000.0) / 1000.0);
}

/* The file a shot of a plan is written to: {n} is its number, as wide as the
 * plan's last (two figures at least), and {frame} its frame. */
std::string shotName(const McpShotPlan &plan, size_t n)
{
	const std::string figures = std::to_string(plan.frames.size());
	std::string number = std::to_string(n);
	while (number.size() < std::max<size_t>(2, figures.size())) number.insert(number.begin(), '0');
	std::string name = plan.name;
	for (size_t at = name.find("{n}"); at != std::string::npos; at = name.find("{n}", at + number.size()))
		name.replace(at, 3, number);
	const std::string frame = n >= 1 && n <= plan.frames.size() ? std::to_string(plan.frames[n - 1]) : "";
	for (size_t at = name.find("{frame}"); at != std::string::npos; at = name.find("{frame}", at + frame.size()))
		name.replace(at, 7, frame);
	return name;
}

McpResult toolScreenshot(ce_session *s, int64_t frame, const cJSON *arguments)
{
	int64_t maxWidth = 0;
	if (!integerArgument(arguments, "max_width", 32, 4096, 800, maxWidth)) return mcpError("'max_width' must be between 32 and 4096.");
	const uint32_t *whole = ce_session_video(s);
	const int32_t wholeWidth = ce_session_video_width(s);
	const int32_t wholeHeight = ce_session_video_height(s);
	if (whole == nullptr || wholeWidth <= 0 || wholeHeight <= 0) return mcpError("The machine has not drawn a picture yet.");
	/* the colours asked for, of the whole picture (issue 571) */
	cJSON *colours = nullptr;
	const cJSON *pixels = cJSON_GetObjectItemCaseSensitive(arguments, "pixels");
	if (pixels != nullptr)
	{
		colours = cJSON_CreateArray();
		const cJSON *pixel = nullptr;
		cJSON_ArrayForEach(pixel, pixels)
		{
			const cJSON *xi = cJSON_GetArrayItem(pixel, 0), *yi = cJSON_GetArrayItem(pixel, 1);
			const int64_t x = cJSON_IsNumber(xi) ? static_cast<int64_t>(xi->valuedouble) : -1;
			const int64_t y = cJSON_IsNumber(yi) ? static_cast<int64_t>(yi->valuedouble) : -1;
			if (x < 0 || y < 0 || x >= wholeWidth || y >= wholeHeight)
			{
				cJSON_Delete(colours);
				return mcpError("Each of 'pixels' is [x, y] inside the picture, which is " + std::to_string(wholeWidth) + " by "
					+ std::to_string(wholeHeight) + ".");
			}
			const uint32_t c = whole[static_cast<size_t>(y) * static_cast<size_t>(wholeWidth) + static_cast<size_t>(x)];
			char text[8];
			std::snprintf(text, sizeof text, "#%02X%02X%02X", (c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
			cJSON *item = cJSON_CreateObject();
			cJSON_AddNumberToObject(item, "x", static_cast<double>(x));
			cJSON_AddNumberToObject(item, "y", static_cast<double>(y));
			cJSON_AddStringToObject(item, "colour", text);
			cJSON_AddItemToArray(colours, item);
		}
	}
	/* a part of it only, cropped before it is scaled */
	int32_t x0 = 0, y0 = 0, width = wholeWidth, height = wholeHeight;
	const cJSON *region = cJSON_GetObjectItemCaseSensitive(arguments, "region");
	if (region != nullptr)
	{
		int64_t rx = 0, ry = 0, rw = 0, rh = 0;
		if (!integerArgument(region, "x", 0, wholeWidth - 1, -1, rx) || !integerArgument(region, "y", 0, wholeHeight - 1, -1, ry)
			|| !integerArgument(region, "width", 1, wholeWidth, -1, rw) || !integerArgument(region, "height", 1, wholeHeight, -1, rh)
			|| rx < 0 || ry < 0 || rw < 1 || rh < 1 || rx + rw > wholeWidth || ry + rh > wholeHeight)
		{
			cJSON_Delete(colours);
			return mcpError("'region' must lie inside the picture, which is " + std::to_string(wholeWidth) + " by "
				+ std::to_string(wholeHeight) + ".");
		}
		x0 = static_cast<int32_t>(rx);
		y0 = static_cast<int32_t>(ry);
		width = static_cast<int32_t>(rw);
		height = static_cast<int32_t>(rh);
	}
	std::vector<uint32_t> cropped;
	const uint32_t *bgra = whole;
	if (region != nullptr)
	{
		cropped.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
		for (int32_t y = 0; y < height; y++)
			std::memcpy(&cropped[static_cast<size_t>(y) * static_cast<size_t>(width)],
				whole + static_cast<size_t>(y0 + y) * static_cast<size_t>(wholeWidth) + static_cast<size_t>(x0), static_cast<size_t>(width) * 4);
		bgra = cropped.data();
	}
	const cJSON *imageItem = cJSON_GetObjectItemCaseSensitive(arguments, "image");
	const bool sendImage = imageItem == nullptr || !cJSON_IsFalse(imageItem);
	cJSON *content = cJSON_CreateArray();
	int32_t outWidth = 0, outHeight = 0;
	if (sendImage)
	{
		std::string png;
		if (!pngOf(bgra, width, height, maxWidth, png, outWidth, outHeight))
		{
			cJSON_Delete(colours);
			cJSON_Delete(content);
			return mcpError("The picture could not be made into a PNG.");
		}
		const std::string encoded = base64(reinterpret_cast<const uint8_t *>(png.data()), png.size());
		cJSON *image = cJSON_CreateObject();
		cJSON_AddStringToObject(image, "type", "image");
		cJSON_AddStringToObject(image, "data", encoded.c_str());
		cJSON_AddStringToObject(image, "mimeType", "image/png");
		cJSON_AddItemToArray(content, image);
	}
	cJSON *facts = cJSON_CreateObject();
	cJSON_AddNumberToObject(facts, "frame", static_cast<double>(frame >= 0 ? frame : ce_session_frame(s)));
	if (sendImage)
	{
		cJSON_AddNumberToObject(facts, "width", outWidth);
		cJSON_AddNumberToObject(facts, "height", outHeight);
	}
	cJSON_AddNumberToObject(facts, "picture_width", wholeWidth);
	cJSON_AddNumberToObject(facts, "picture_height", wholeHeight);
	if (region != nullptr)
	{
		cJSON *r = cJSON_CreateObject();
		cJSON_AddNumberToObject(r, "x", x0);
		cJSON_AddNumberToObject(r, "y", y0);
		cJSON_AddNumberToObject(r, "width", width);
		cJSON_AddNumberToObject(r, "height", height);
		cJSON_AddItemToObject(facts, "region", r);
	}
	if (colours != nullptr) cJSON_AddItemToObject(facts, "pixels", colours);
	addLook(facts, bgra, width, height);
	cJSON *text = cJSON_CreateObject();
	cJSON_AddStringToObject(text, "type", "text");
	const std::string factsText = printJson(facts);
	cJSON_Delete(facts);
	cJSON_AddStringToObject(text, "text", factsText.c_str());
	cJSON_AddItemToArray(content, text);
	McpResult result;
	result.content = printJson(content);
	cJSON_Delete(content);
	return result;
}

McpResult toolListMemory(ce_session *s)
{
	cJSON *out = cJSON_CreateObject();
	cJSON *domains = cJSON_CreateArray();
	for (int32_t i = 0; i < ce_session_domain_count(s); i++)
	{
		/* the buses the domain list ends with are listed as buses */
		if (ce_session_domain_hidden(s, i) != 0 || ce_session_domain_bus(s, i) >= 0) continue;
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_domain_name(s, i));
		cJSON_AddNumberToObject(item, "size", static_cast<double>(ce_session_domain_size(s, i)));
		cJSON_AddBoolToObject(item, "writable", ce_session_domain_writable(s, i) != 0);
		cJSON_AddItemToArray(domains, item);
	}
	cJSON *buses = cJSON_CreateArray();
	for (int32_t i = 0; i < ce_session_bus_count(s); i++)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_bus_name(s, i));
		cJSON_AddNumberToObject(item, "size", static_cast<double>(ce_session_bus_size(s, i)));
		cJSON_AddBoolToObject(item, "writable", ce_session_bus_writable(s, i) != 0);
		cJSON_AddItemToArray(buses, item);
	}
	cJSON_AddItemToObject(out, "domains", domains);
	cJSON_AddItemToObject(out, "buses", buses);
	return textResult(out);
}

/* The domain or the bus of that name: one index is set, the other is -1. */
bool findMemory(ce_session *s, const std::string &name, int32_t &domainIndex, int32_t &busIndex)
{
	domainIndex = busIndex = -1;
	for (int32_t i = 0; i < ce_session_domain_count(s) && domainIndex < 0; i++)
	{
		if (ce_session_domain_hidden(s, i) == 0 && ce_session_domain_bus(s, i) < 0 && name == ce_session_domain_name(s, i)) domainIndex = i;
	}
	for (int32_t i = 0; domainIndex < 0 && i < ce_session_bus_count(s) && busIndex < 0; i++)
	{
		if (name == ce_session_bus_name(s, i)) busIndex = i;
	}
	return domainIndex >= 0 || busIndex >= 0;
}

McpResult toolReadMemory(ce_session *s, const cJSON *arguments)
{
	const cJSON *domainItem = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	if (!cJSON_IsString(domainItem)) return mcpError("'domain' must be a name from list_memory.");
	const std::string domain = domainItem->valuestring;
	const cJSON *typeItem = cJSON_GetObjectItemCaseSensitive(arguments, "type");
	const std::string type = cJSON_IsString(typeItem) ? typeItem->valuestring : "u8";
	if (type.size() < 2 || (type[0] != 'u' && type[0] != 's')
		|| (type.substr(1) != "8" && type.substr(1) != "16" && type.substr(1) != "32"))
		return mcpError("'type' must be one of u8, s8, u16, s16, u32, s32.");
	const bool isSigned = type[0] == 's';
	const int64_t width = type.substr(1) == "8" ? 1 : type.substr(1) == "16" ? 2 : 4;
	const bool bigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	int64_t address = 0, count = 0;
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, 0, address)) return mcpError("'address' must be 0 or more.");
	if (!integerArgument(arguments, "count", 1, 4096 / width, 1, count))
		return mcpError("'count' must be at least 1, and the values read must fit in 4096 bytes.");

	int32_t domainIndex = -1, busIndex = -1;
	findMemory(s, domain, domainIndex, busIndex);
	if (domainIndex < 0 && busIndex < 0) return mcpError("There is no domain or bus called '" + domain + "'. list_memory names them.");
	const int64_t size = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
	if (address + count * width > size)
		return mcpError("'" + domain + "' is " + std::to_string(size) + " bytes, and the read goes past its end.");

	std::vector<uint8_t> bytes(static_cast<size_t>(count * width));
	const int64_t got = domainIndex >= 0
		? ce_session_domain_read(s, domainIndex, address, bytes.data(), static_cast<int64_t>(bytes.size()))
		: ce_session_bus_read(s, busIndex, address, bytes.data(), static_cast<int64_t>(bytes.size()));
	if (got != static_cast<int64_t>(bytes.size())) return mcpError("The machine did not give those bytes.");

	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", domain.c_str());
	cJSON_AddNumberToObject(out, "address", static_cast<double>(address));
	cJSON_AddStringToObject(out, "address_hex", mcpHex(static_cast<uint64_t>(address)).c_str());
	cJSON_AddStringToObject(out, "type", type.c_str());
	cJSON *values = cJSON_CreateArray();
	for (int64_t i = 0; i < count; i++)
	{
		uint32_t value = 0;
		for (int64_t b = 0; b < width; b++)
		{
			const uint8_t byte = bytes[static_cast<size_t>(i * width + b)];
			value |= static_cast<uint32_t>(byte) << (8 * (bigEndian ? width - 1 - b : b));
		}
		double number = value;
		if (isSigned)
		{
			number = width == 1 ? static_cast<double>(static_cast<int8_t>(value))
				: width == 2 ? static_cast<double>(static_cast<int16_t>(value))
				: static_cast<double>(static_cast<int32_t>(value));
		}
		cJSON_AddItemToArray(values, cJSON_CreateNumber(number));
	}
	cJSON_AddItemToObject(out, "values", values);
	std::string hex;
	static const char digits[] = "0123456789ABCDEF";
	for (uint8_t byte : bytes) { hex += digits[byte >> 4]; hex += digits[byte & 15]; }
	cJSON_AddStringToObject(out, "bytes_hex", hex.c_str());
	return textResult(out);
}

McpResult toolRegisters(ce_session *s, const cJSON *arguments)
{
	auto hexOf = [&](int32_t i) {
		const int32_t bits = ce_session_register_bits(s, i);
		char hex[32];
		std::snprintf(hex, sizeof(hex), "0x%0*llX", std::max(1, (bits + 3) / 4),
			static_cast<unsigned long long>(static_cast<uint64_t>(ce_session_register_value(s, i))));
		return std::string(hex);
	};
	const int32_t count = ce_session_register_count(s);
	auto named = [&](const std::string &name) {
		for (int32_t i = 0; i < count; i++)
			if (name == ce_session_register_name(s, i)) return i;
		/* a role the core named: pc, sp, flags */
		return ce_session_register_role(s, name.c_str(), nullptr, nullptr);
	};
	/* only those asked for: a name the core does not have says which it has (issue 571) */
	std::vector<int32_t> shown;
	const cJSON *names = cJSON_GetObjectItemCaseSensitive(arguments, "names");
	const cJSON *name = nullptr;
	cJSON_ArrayForEach(name, names)
	{
		const int32_t i = cJSON_IsString(name) ? named(name->valuestring) : -1;
		if (i < 0)
		{
			std::string all;
			for (int32_t r = 0; r < count; r++) all += (r == 0 ? "" : ", ") + std::string(ce_session_register_name(s, r));
			return mcpError(std::string("This core has no register called '") + (cJSON_IsString(name) ? name->valuestring : "?")
				+ "'. Its registers are: " + (all.empty() ? "none" : all) + ".");
		}
		shown.push_back(i);
	}
	if (names == nullptr)
		for (int32_t i = 0; i < count; i++) shown.push_back(i);
	cJSON *out = cJSON_CreateObject();
	cJSON *registers = cJSON_CreateArray();
	for (const int32_t i : shown)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", ce_session_register_name(s, i));
		cJSON_AddNumberToObject(item, "bits", ce_session_register_bits(s, i));
		cJSON_AddStringToObject(item, "value", hexOf(i).c_str());
		cJSON_AddItemToArray(registers, item);
	}
	cJSON_AddItemToObject(out, "registers", registers);
	/* the roles the core named (issue 571): which register is the program
	 * counter, the stack pointer, the flags, and on a segmented processor the
	 * segment paired with each and the linear address it makes */
	cJSON *roles = cJSON_CreateObject();
	for (const char *role : { "pc", "sp", "flags" })
	{
		int32_t segment = -1, base = -1;
		const int32_t i = ce_session_register_role(s, role, &segment, &base);
		if (i < 0) continue;
		cJSON *r = cJSON_CreateObject();
		cJSON_AddStringToObject(r, "register", ce_session_register_name(s, i));
		cJSON_AddStringToObject(r, "value", hexOf(i).c_str());
		if (segment >= 0)
		{
			cJSON_AddStringToObject(r, "segment", ce_session_register_name(s, segment));
			cJSON_AddStringToObject(r, "segment_value", hexOf(segment).c_str());
		}
		if (base >= 0)
		{
			cJSON_AddStringToObject(r, "segment_base", hexOf(base).c_str());
			cJSON_AddStringToObject(r, "linear", mcpHex(static_cast<uint64_t>(ce_session_register_value(s, base)) +
				static_cast<uint64_t>(ce_session_register_value(s, i))).c_str());
		}
		cJSON_AddItemToObject(roles, role, r);
	}
	if (cJSON_GetArraySize(roles) > 0) cJSON_AddItemToObject(out, "roles", roles);
	else cJSON_Delete(roles);
	if (ce_session_has_executed_cycles(s) != 0)
		cJSON_AddNumberToObject(out, "executed_cycles", static_cast<double>(ce_session_executed_cycles(s)));
	return textResult(out);
}

/* The whole text: a longer one was cut at 1023 bytes, inside a character as
 * often as not (engine analysis, 2026-10-18). */
std::string propertyText(ce_session *s, int32_t index, uint32_t element)
{
	std::vector<char> buffer(1024);
	int32_t length = ce_session_property_text(s, index, element, 1, buffer.data(), static_cast<int32_t>(buffer.size()));
	if (length >= static_cast<int32_t>(buffer.size()) && length < (1 << 24))
	{
		buffer.resize(static_cast<size_t>(length) + 1);
		length = ce_session_property_text(s, index, element, 1, buffer.data(), static_cast<int32_t>(buffer.size()));
	}
	if (length < 0) return std::string();
	return std::string(buffer.data(), std::min<size_t>(static_cast<size_t>(length), std::strlen(buffer.data())));
}

McpResult toolListProperties(ce_session *s, const cJSON *arguments)
{
	int64_t limit = 0;
	if (!integerArgument(arguments, "limit", 1, 500, 100, limit)) return mcpError("'limit' must be between 1 and 500.");
	const cJSON *filterItem = cJSON_GetObjectItemCaseSensitive(arguments, "filter");
	const std::string filter = cJSON_IsString(filterItem) ? lowered(filterItem->valuestring) : std::string();
	/* a table the game makes as it runs is listed afresh before it is read */
	if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
	cJSON *table = cJSON_Parse(ce_session_property_table(s));
	const cJSON *properties = cJSON_GetObjectItemCaseSensitive(table, "properties");
	cJSON *out = cJSON_CreateObject();
	cJSON *list = cJSON_CreateArray();
	int32_t index = 0;
	int64_t total = 0;
	const cJSON *property = nullptr;
	cJSON_ArrayForEach(property, properties)
	{
		const cJSON *name = cJSON_GetObjectItemCaseSensitive(property, "name");
		if (cJSON_IsString(name) && (filter.empty() || lowered(name->valuestring).find(filter) != std::string::npos))
		{
			total++;
			if (cJSON_GetArraySize(list) < limit)
			{
				cJSON *item = cJSON_CreateObject();
				cJSON_AddStringToObject(item, "name", name->valuestring);
				const cJSON *type = cJSON_GetObjectItemCaseSensitive(property, "type");
				if (cJSON_IsString(type)) cJSON_AddStringToObject(item, "type", type->valuestring);
				const cJSON *count = cJSON_GetObjectItemCaseSensitive(property, "count");
				if (cJSON_IsNumber(count) && count->valuedouble > 1) cJSON_AddNumberToObject(item, "elements", count->valuedouble);
				cJSON_AddStringToObject(item, "value", propertyText(s, index, 0).c_str());
				cJSON_AddItemToArray(list, item);
			}
		}
		index++;
	}
	cJSON_Delete(table);
	cJSON_AddNumberToObject(out, "total", static_cast<double>(total));
	cJSON_AddItemToObject(out, "properties", list);
	return textResult(out);
}

McpResult toolGetProperty(ce_session *s, const cJSON *arguments)
{
	const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	if (!cJSON_IsString(nameItem)) return mcpError("'name' must be a property's name. list_properties names them.");
	if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
	uint32_t element = 0;
	const int32_t index = ce_session_property_find(s, nameItem->valuestring, &element);
	if (index < 0) return mcpError(std::string("There is no property called '") + nameItem->valuestring + "'. list_properties names them.");
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "name", nameItem->valuestring);
	cJSON_AddStringToObject(out, "value", propertyText(s, index, element).c_str());
	return textResult(out);
}

McpResult toolWriteMemory(ce_session *s, const cJSON *arguments)
{
	const cJSON *domainItem = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	if (!cJSON_IsString(domainItem)) return mcpError("'domain' must be a name from list_memory.");
	const std::string domain = domainItem->valuestring;
	const cJSON *typeItem = cJSON_GetObjectItemCaseSensitive(arguments, "type");
	const std::string type = cJSON_IsString(typeItem) ? typeItem->valuestring : "u8";
	if (type.size() < 2 || (type[0] != 'u' && type[0] != 's')
		|| (type.substr(1) != "8" && type.substr(1) != "16" && type.substr(1) != "32"))
		return mcpError("'type' must be one of u8, s8, u16, s16, u32, s32.");
	const bool isSigned = type[0] == 's';
	const int64_t width = type.substr(1) == "8" ? 1 : type.substr(1) == "16" ? 2 : 4;
	const bool bigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	int64_t address = 0;
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, 0, address)) return mcpError("'address' must be 0 or more.");
	const cJSON *values = cJSON_GetObjectItemCaseSensitive(arguments, "values");
	const int64_t count = cJSON_IsArray(values) ? cJSON_GetArraySize(values) : 0;
	if (count < 1 || count * width > 4096) return mcpError("'values' must hold at least one value, and they must fit in 4096 bytes.");

	/* every value is checked before a byte is written */
	const double low = isSigned ? -static_cast<double>(1ULL << (8 * width - 1)) : 0;
	const double high = isSigned ? static_cast<double>((1ULL << (8 * width - 1)) - 1) : static_cast<double>((1ULL << (8 * width)) - 1);
	std::vector<uint8_t> bytes(static_cast<size_t>(count * width));
	int64_t at = 0;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, values)
	{
		if (!cJSON_IsNumber(item) || item->valuedouble != static_cast<double>(static_cast<int64_t>(item->valuedouble))
			|| item->valuedouble < low || item->valuedouble > high)
			return mcpError("Every value must be a whole number that fits in " + type + ".");
		const uint32_t value = static_cast<uint32_t>(static_cast<int64_t>(item->valuedouble));
		for (int64_t b = 0; b < width; b++)
		{
			bytes[static_cast<size_t>(at * width + b)] = static_cast<uint8_t>(value >> (8 * (bigEndian ? width - 1 - b : b)));
		}
		at++;
	}

	int32_t domainIndex = -1, busIndex = -1;
	if (!findMemory(s, domain, domainIndex, busIndex))
		return mcpError("There is no domain or bus called '" + domain + "'. list_memory names them.");
	const int64_t size = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
	if (address + static_cast<int64_t>(bytes.size()) > size)
		return mcpError("'" + domain + "' is " + std::to_string(size) + " bytes, and the write goes past its end.");
	if (domainIndex >= 0)
	{
		/* the same way the frontend's own memory tools write: through the
		 * domain's memory itself */
		uint8_t *memory = reinterpret_cast<uint8_t *>(ce_session_domain_ptr(s, domainIndex));
		if (ce_session_domain_writable(s, domainIndex) == 0 || memory == nullptr)
			return mcpError("'" + domain + "' cannot be written.");
		std::memcpy(memory + address, bytes.data(), bytes.size());
	}
	else
	{
		if (ce_session_bus_writable(s, busIndex) == 0) return mcpError("'" + domain + "' cannot be written.");
		for (size_t i = 0; i < bytes.size(); i++)
		{
			ce_session_bus_poke(s, busIndex, static_cast<int32_t>(address + static_cast<int64_t>(i)), bytes[i]);
		}
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", domain.c_str());
	cJSON_AddNumberToObject(out, "address", static_cast<double>(address));
	cJSON_AddStringToObject(out, "address_hex", mcpHex(static_cast<uint64_t>(address)).c_str());
	cJSON_AddNumberToObject(out, "bytes_written", static_cast<double>(bytes.size()));
	return textResult(out);
}

/* ---- freezes (issue 571) ---- */

/* An agent's condition reads "the value now OP value"; the engine's reads
 * "compare OP now". */
const struct { const char *op; int32_t kind; } kFreezeOps[] = {
	{ "equal", 1 }, { "not_equal", 2 }, { "greater", 5 }, { "greater_or_equal", 6 }, { "less", 3 }, { "less_or_equal", 4 },
};

/* the property's name in the table, an array's element with its [n] */
std::string propertyName(ce_session *s, int32_t index, uint32_t element)
{
	cJSON *table = cJSON_Parse(ce_session_property_table(s));
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(
		cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(table, "properties"), index), "name");
	std::string out = cJSON_IsString(name) ? name->valuestring : "#" + std::to_string(index);
	cJSON_Delete(table);
	if (element > 0) out += "[" + std::to_string(element) + "]";
	return out;
}

McpResult toolListFreezes(ce_session *s)
{
	cJSON *out = cJSON_CreateObject();
	cJSON *list = cJSON_CreateArray();
	for (int32_t i = 0; i < ce_session_freeze_count(s); i++)
	{
		int32_t domain = -1, size = 1, bigEndian = 0, kind = 0, property = -1;
		int64_t address = 0;
		uint64_t value = 0, compare = 0;
		uint32_t element = 0;
		const char *text = nullptr;
		if (ce_session_freeze_info(s, i, &domain, &address, &size, &bigEndian, &value, &kind, &compare, &property, &element, &text) == 0)
			continue;
		cJSON *item = cJSON_CreateObject();
		cJSON_AddNumberToObject(item, "index", i);
		cJSON_AddBoolToObject(item, "on", ce_session_freeze_enabled(s, i) == 1);
		if (property >= 0)
		{
			cJSON_AddStringToObject(item, "property", propertyName(s, property, element).c_str());
			cJSON_AddStringToObject(item, "text", text != nullptr ? text : "");
		}
		else
		{
			const char *name = ce_session_domain_name(s, domain);
			cJSON_AddStringToObject(item, "domain", name != nullptr ? name : "");
			cJSON_AddNumberToObject(item, "address", static_cast<double>(address));
			cJSON_AddStringToObject(item, "address_hex", mcpHex(static_cast<uint64_t>(address)).c_str());
			cJSON_AddNumberToObject(item, "size", size);
			cJSON_AddNumberToObject(item, "value", static_cast<double>(value));
			cJSON_AddStringToObject(item, "value_hex", mcpHex(value).c_str());
			if (size > 1) cJSON_AddBoolToObject(item, "big_endian", bigEndian != 0);
			for (const auto &op : kFreezeOps)
				if (op.kind == kind)
				{
					cJSON *when = cJSON_CreateObject();
					cJSON_AddStringToObject(when, "op", op.op);
					cJSON_AddNumberToObject(when, "value", static_cast<double>(compare));
					cJSON_AddItemToObject(item, "when", when);
				}
		}
		cJSON_AddItemToArray(list, item);
	}
	cJSON_AddItemToObject(out, "freezes", list);
	return textResult(out);
}

int32_t domainNamed(ce_session *s, const std::string &name)
{
	for (int32_t d = 0; d < ce_session_domain_count(s); d++)
	{
		const char *n = ce_session_domain_name(s, d);
		if (n != nullptr && name == n) return d;
	}
	return -1;
}

} // namespace

bool mcpReadFreeze(const std::string &tool, const std::string &argumentsJson, McpFreezeAsk &ask, std::string &why)
{
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const auto text = [&](const char *name) {
		const cJSON *t = cJSON_GetObjectItemCaseSensitive(arguments, name);
		return cJSON_IsString(t) ? std::string(t->valuestring) : std::string();
	};
	ask = McpFreezeAsk{};
	ask.domain = text("domain");
	ask.property = text("property");
	ask.text = text("text");
	ask.all = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "all"));
	ask.bigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	const bool hasAddress = cJSON_GetObjectItemCaseSensitive(arguments, "address") != nullptr;
	int64_t size = 1, value = 0;
	bool ok = true;
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, 0, ask.address)) { why = "'address' must be 0 or more."; ok = false; }
	else if (!integerArgument(arguments, "size", 1, 8, 1, size) || (size != 1 && size != 2 && size != 4 && size != 8)) { why = "'size' must be 1, 2, 4 or 8."; ok = false; }
	else if (!integerArgument(arguments, "index", 0, INT32_MAX, -1, ask.index)) { why = "'index' must be 0 or more."; ok = false; }
	else if (!integerArgument(arguments, "value", INT64_MIN, INT64_MAX, 0, value)) { why = "'value' must be a whole number."; ok = false; }
	ask.size = static_cast<int32_t>(size);
	ask.value = static_cast<uint64_t>(value);
	const cJSON *when = cJSON_GetObjectItemCaseSensitive(arguments, "when");
	if (ok && when != nullptr)
	{
		const cJSON *op = cJSON_GetObjectItemCaseSensitive(when, "op");
		int64_t compare = 0;
		for (const auto &o : kFreezeOps)
			if (cJSON_IsString(op) && std::strcmp(op->valuestring, o.op) == 0) ask.compareKind = o.kind;
		if (ask.compareKind == 0 || !integerArgument(when, "value", INT64_MIN, INT64_MAX, 0, compare))
		{
			why = "'when' needs an 'op' (equal, not_equal, greater, greater_or_equal, less, less_or_equal) and a whole-number 'value'.";
			ok = false;
		}
		ask.compare = static_cast<uint64_t>(compare);
	}
	cJSON_Delete(arguments);
	if (!ok) return false;
	if (tool == "freeze")
	{
		if (ask.property.empty() == ask.domain.empty())
			why = "Give either 'domain' with 'address' and 'value', or 'property' with 'text'.";
		else if (!ask.property.empty() && ask.compareKind != 0)
			why = "'when' is for a memory freeze, not a property's.";
		else if (ask.property.empty() && !hasAddress)
			why = "'address' must be given with 'domain'.";
		return why.empty();
	}
	const int given = (ask.index >= 0 ? 1 : 0) + (ask.all ? 1 : 0) + (!ask.property.empty() ? 1 : 0) + (!ask.domain.empty() ? 1 : 0);
	if (given != 1 || (!ask.domain.empty() && !hasAddress))
		why = "Give one of: 'index', 'all', 'property', or 'domain' with 'address'.";
	return why.empty();
}

McpResult mcpFreezeTool(ce_session *s, const std::string &tool, const std::string &argumentsJson)
{
	if (s == nullptr) return mcpError(kNoMachine);
	McpFreezeAsk ask;
	std::string why;
	if (!mcpReadFreeze(tool, argumentsJson, ask, why)) return mcpError(why);
	int32_t index = -1;
	if (tool == "freeze")
	{
		if (!ask.property.empty())
		{
			uint32_t element = 0;
			const int32_t p = ce_session_property_find(s, ask.property.c_str(), &element);
			if (p < 0) return mcpError("There is no property called '" + ask.property + "' (list_properties lists them).");
			index = ce_session_freeze_property(s, p, element, ask.text.c_str());
		}
		else
		{
			const int32_t d = domainNamed(s, ask.domain);
			if (d < 0) return mcpError("There is no memory domain called '" + ask.domain + "' (list_memory lists them).");
			index = ce_session_freeze(s, d, ask.address, ask.size, ask.bigEndian ? 1 : 0, ask.value, ask.compareKind, ask.compare);
		}
		if (index < 0)
		{
			const char *e = ce_session_last_error(s);
			return mcpError(e != nullptr && *e != '\0' ? e : "The freeze was refused.");
		}
	}
	else if (ask.all)
		ce_session_unfreeze_all(s);
	else
	{
		/* by index, or every freeze on the place named */
		for (int32_t i = ce_session_freeze_count(s) - 1; i >= 0; i--)
		{
			int32_t domain = -1, size = 1, property = -1;
			int64_t address = 0;
			uint32_t element = 0;
			ce_session_freeze_info(s, i, &domain, &address, &size, nullptr, nullptr, nullptr, nullptr, &property, &element, nullptr);
			bool goes = ask.index >= 0 ? i == ask.index : false;
			if (ask.index < 0 && !ask.property.empty()) goes = property >= 0 && propertyName(s, property, element) == ask.property;
			if (ask.index < 0 && ask.property.empty())
				goes = property < 0 && domain == domainNamed(s, ask.domain) && ask.address >= address && ask.address < address + size;
			if (goes)
			{
				ce_session_unfreeze(s, i);
				index = i;
			}
		}
		if (index < 0) return mcpError("No freeze is there (list_freezes lists them).");
	}
	return toolListFreezes(s);
}

namespace
{

McpResult toolSetProperty(ce_session *s, const cJSON *arguments)
{
	const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	if (!cJSON_IsString(nameItem) || !cJSON_IsString(valueItem)) return mcpError("'name' and 'value' must both be text.");
	if (ce_session_property_dynamic(s) != 0) ce_session_property_refresh(s);
	uint32_t element = 0;
	const int32_t index = ce_session_property_find(s, nameItem->valuestring, &element);
	if (index < 0) return mcpError(std::string("There is no property called '") + nameItem->valuestring + "'. list_properties names them.");
	if (ce_session_property_set_text(s, index, element, valueItem->valuestring) != 0)
	{
		const char *why = ce_session_last_error(s);
		return mcpError(std::string("'") + nameItem->valuestring + "' was not set: " + (why != nullptr && why[0] != '\0' ? why : "the value does not fit it") + ".");
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "name", nameItem->valuestring);
	cJSON_AddStringToObject(out, "value", propertyText(s, index, element).c_str());
	return textResult(out);
}

McpResult toolSetRegister(ce_session *s, const cJSON *arguments)
{
	const cJSON *nameItem = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	if (!cJSON_IsString(nameItem) || !cJSON_IsNumber(valueItem)) return mcpError("'name' must be a register from registers, and 'value' a whole number.");
	for (int32_t i = 0; i < ce_session_register_count(s); i++)
	{
		if (std::strcmp(nameItem->valuestring, ce_session_register_name(s, i)) != 0) continue;
		if (ce_session_register_set(s, i, static_cast<int64_t>(valueItem->valuedouble)) != 0)
			return mcpError("This core does not allow its registers to be written.");
		cJSON *out = cJSON_CreateObject();
		cJSON_AddStringToObject(out, "name", nameItem->valuestring);
		cJSON_AddNumberToObject(out, "value", static_cast<double>(ce_session_register_value(s, i)));
		return textResult(out);
	}
	return mcpError(std::string("There is no register called '") + nameItem->valuestring + "'. registers names them.");
}

double searchNumber(uint32_t raw, int size, bool isSigned)
{
	if (!isSigned) return raw;
	return size == 1 ? static_cast<double>(static_cast<int8_t>(raw))
		: size == 2 ? static_cast<double>(static_cast<int16_t>(raw))
		: static_cast<double>(static_cast<int32_t>(raw));
}

/* ---- named values: watch, and advance_until's condition ---- */

/* Reads what an agent named out of a tool's arguments. False with `why`. */
bool parseSource(const cJSON *arguments, Source &source, std::string &why)
{
	const cJSON *property = cJSON_GetObjectItemCaseSensitive(arguments, "property");
	const cJSON *domain = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	if (cJSON_IsString(property) && domain == nullptr)
	{
		source.isProperty = true;
		source.property = property->valuestring;
		source.name = cJSON_IsString(name) ? name->valuestring : source.property;
		return true;
	}
	if (!cJSON_IsString(domain) || property != nullptr)
	{
		why = "Give either 'domain' and 'address', or 'property'.";
		return false;
	}
	const cJSON *typeItem = cJSON_GetObjectItemCaseSensitive(arguments, "type");
	const std::string type = cJSON_IsString(typeItem) ? typeItem->valuestring : "u8";
	if (type.size() < 2 || (type[0] != 'u' && type[0] != 's')
		|| (type.substr(1) != "8" && type.substr(1) != "16" && type.substr(1) != "32"))
	{
		why = "'type' must be one of u8, s8, u16, s16, u32, s32.";
		return false;
	}
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, -1, source.address) || source.address < 0)
	{
		why = "'address' must be given with 'domain', and be 0 or more.";
		return false;
	}
	source.domain = domain->valuestring;
	source.isSigned = type[0] == 's';
	source.width = type.substr(1) == "8" ? 1 : type.substr(1) == "16" ? 2 : 4;
	source.bigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	char place[64];
	std::snprintf(place, sizeof(place), " 0x%llX ", static_cast<unsigned long long>(source.address));
	source.name = cJSON_IsString(name) ? name->valuestring : source.domain + place + type;
	return true;
}

/* The value now: as text always, and as a number when it is one. False with
 * `why` when it cannot be read (no such domain, past its end, no such
 * property). */
bool readSource(ce_session *s, const Source &source, std::string &text, bool &isNumber, double &number, std::string &why)
{
	isNumber = false;
	if (source.isProperty)
	{
		/* No refresh first: _find, _get and _text place a dynamic property by name
		 * before every use (engine.h), and a refresh is a guest call and a full
		 * parse of the table - once per watch per answer, and once a frame in
		 * advance_until (engine analysis, 2026-10-18). */
		uint32_t element = 0;
		const int32_t index = ce_session_property_find(s, source.property.c_str(), &element);
		if (index < 0)
		{
			why = "there is no property called '" + source.property + "'";
			return false;
		}
		text = propertyText(s, index, element);
		ce_property_value value;
		std::memset(&value, 0, sizeof(value));
		if (ce_session_property_get(s, index, element, &value) == 0)
		{
			isNumber = value.kind == CE_PROPERTY_INT || value.kind == CE_PROPERTY_UINT
				|| value.kind == CE_PROPERTY_FLOAT || value.kind == CE_PROPERTY_BOOL;
			number = value.kind == CE_PROPERTY_UINT ? static_cast<double>(value.u)
				: value.kind == CE_PROPERTY_FLOAT ? value.f : static_cast<double>(value.i);
		}
		return true;
	}
	int32_t domainIndex = -1, busIndex = -1;
	if (!findMemory(s, source.domain, domainIndex, busIndex))
	{
		why = "there is no domain or bus called '" + source.domain + "'";
		return false;
	}
	if (source.length > 0)
	{
		/* a range of bytes, as hex text: what 'changed' compares */
		const int64_t limit = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
		std::vector<uint8_t> range(static_cast<size_t>(source.length));
		const int64_t got = source.address < 0 || source.address > limit - source.length ? 0
			: domainIndex >= 0 ? ce_session_domain_read(s, domainIndex, source.address, range.data(), source.length)
			: ce_session_bus_read(s, busIndex, source.address, range.data(), source.length);
		if (got != source.length)
		{
			why = "'" + source.domain + "' has no such range";
			return false;
		}
		static const char hex[] = "0123456789ABCDEF";
		text.clear();
		for (uint8_t b : range) { text += hex[b >> 4]; text += hex[b & 15]; }
		return true;
	}
	uint8_t bytes[4] = { 0, 0, 0, 0 };
	/* a bus reads what is past its end as zeros, so its size is asked: a watch
	 * there read 0, and advance_until "equal 0" stopped at once (engine
	 * analysis, 2026-10-18) */
	const int64_t limit = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
	const int64_t got = source.address < 0 || source.address > limit - source.width ? 0
		: domainIndex >= 0
		? ce_session_domain_read(s, domainIndex, source.address, bytes, source.width)
		: ce_session_bus_read(s, busIndex, source.address, bytes, source.width);
	if (got != source.width)
	{
		why = "'" + source.domain + "' has no such address";
		return false;
	}
	uint32_t raw = 0;
	for (int b = 0; b < source.width; b++)
	{
		raw |= static_cast<uint32_t>(bytes[b]) << (8 * (source.bigEndian ? source.width - 1 - b : b));
	}
	number = searchNumber(raw, source.width, source.isSigned);
	isNumber = true;
	char digits[32];
	std::snprintf(digits, sizeof(digits), "%.0f", number);
	text = digits;
	return true;
}

/* [{"name":..,"value":..}] for the watches, a value that cannot be read
 * carrying "error" in place of "value". The caller owns the array. */
cJSON *watchValues(Service &sv, ce_session *s)
{
	cJSON *list = cJSON_CreateArray();
	for (const Source &watch : sv.watches)
	{
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "name", watch.name.c_str());
		std::string text, why;
		bool isNumber = false;
		double number = 0;
		if (!readSource(s, watch, text, isNumber, number, why)) cJSON_AddStringToObject(item, "error", why.c_str());
		else if (isNumber && !watch.isProperty) cJSON_AddNumberToObject(item, "value", number);
		else cJSON_AddStringToObject(item, "value", text.c_str());
		cJSON_AddItemToArray(list, item);
	}
	return list;
}

/* The tools whose answer lands the machine on a frame carry the watches. */
bool carriesWatches(const std::string &tool)
{
	return tool == "status" || tool == "seek" || tool == "advance_until" || tool == "load_branch";
}

/* Adds "watches" to an answer that is a JSON object. Anything else is left
 * as it is. */
std::string withWatches(Service &sv, ce_session *s, const std::string &text)
{
	if (s == nullptr || sv.watches.empty()) return text;
	cJSON *object = cJSON_Parse(text.c_str());
	if (!cJSON_IsObject(object))
	{
		cJSON_Delete(object);
		return text;
	}
	cJSON_AddItemToObject(object, "watches", watchValues(sv, s));
	std::string out = printJson(object);
	cJSON_Delete(object);
	return out;
}

McpResult toolWatch(Service &sv, ce_session *s, const cJSON *arguments)
{
	const cJSON *list = cJSON_GetObjectItemCaseSensitive(arguments, "watches");
	if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) > 32) return mcpError("'watches' must be a list of at most 32 values to watch.");
	std::vector<Source> watches;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, list)
	{
		Source source;
		std::string why;
		if (!cJSON_IsObject(item) || !parseSource(item, source, why))
			return mcpError("Watch " + std::to_string(watches.size() + 1) + ": " + (why.empty() ? "it must be an object." : why));
		watches.push_back(source);
	}
	sv.watches = watches;
	cJSON *out = cJSON_CreateObject();
	cJSON_AddItemToObject(out, "watches", watchValues(sv, s));
	return textResult(out);
}

/* ---- the editing tools: what they mean ----
 *
 * set_input, set_buttons, insert_frames and delete_frames are run by whoever
 * holds the movie: TAStudio in the window, the engine itself where there is
 * no window. What they MEAN is decided here, once, before either is asked:
 * the arguments are checked against the movie and the machine's controls,
 * set_buttons is turned into the lines it amounts to, and what is handed on
 * is one of three plain steps - set these lines from this frame, insert this
 * many frames here, delete this many there - with the name the step has in
 * the undo history. The holder of the movie only applies it. */

const char kNoMovie[] = "No project is open in Chimera.";

std::string labelled(const char *what, int64_t frame, int64_t count)
{
	return std::string("Agent: ") + what + (count == 1
		? ", frame " + std::to_string(frame)
		: ", frames " + std::to_string(frame) + " to " + std::to_string(frame + count - 1));
}

/* Whether a line has the columns of this machine's lines: as many characters
 * between its bars as the empty line has, and readable as one. The bars are
 * not counted, because a line may be written with or without the empty first
 * group of a machine that has no console buttons: the engine and the
 * interface write it ("||....|"), and movies written by hand, the test
 * movies among them, leave it out ("|....|"). Both are read alike. */
bool lineFits(ce_session *s, const EntryLayout &held, const std::string &line, const std::string &blank)
{
	const auto columns = [](const std::string &text)
	{
		return std::count_if(text.begin(), text.end(), [](char c) { return c != '|'; });
	};
	/* printable ASCII only: a newline, another control byte or a byte of a
	 * UTF-8 character each passed as a column, was played as a pressed button,
	 * and a newline split the line when the movie was saved (engine analysis,
	 * 2026-10-18) */
	const bool printable = std::all_of(line.begin(), line.end(),
		[](char c) { return static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F; });
	return printable && !line.empty() && line.front() == '|' && line.back() == '|' && columns(line) == columns(blank)
		&& sessionDecodeIn(s, held, line.c_str(), nullptr, nullptr);
}

/* How far past the movie's end an edit may start: the frames between are
 * added empty, one at a time, and a frame number two billion past the end
 * filled memory until the process was killed (engine analysis, 2026-10-18).
 * The same as the most insert_frames adds at once. */
const int64_t kMostPastEnd = 100000;

std::string pastEndRefusal(int64_t length)
{
	return "'frame' may be at most " + std::to_string(kMostPastEnd) + " frames past the movie's end, which is frame "
		+ std::to_string(length) + ". insert_frames adds more.";
}

/* Hands back the step for the movie's holder as (tool, arguments), or an
 * error. `tool` comes in as the agent's and goes out as the holder's.
 * `engineHolds` says the movie is the engine's own (no window), which takes a
 * step that widens its LogKey. */
/* The lines of `count` frames from `frame`: each the line the frame has now
 * (or the empty one past the movie's end) as `change` alters its buttons and
 * axes, written in the held movie's columns. A control `pressed` or `moved`
 * names that the movie's LogKey does not joins it, as a group of its own at
 * the end, rather than being left out of the lines (user-decided,
 * 2026-10-18): the step says so ("key", and the "columns" every line gains).
 * The engine's own movie (no window) takes that; the window's movie keeps its
 * columns in TAStudio, so there it is refused, said. Fills the step's
 * "entries". */
bool changedLines(ce_session *s, const ce_movie_log *log, bool engineHolds, int64_t frame, int64_t count,
	const std::vector<uint8_t> &pressed, const std::vector<int32_t> &moved,
	const std::function<void(int64_t, uint8_t *, int32_t *)> &change, cJSON *step, std::string &why)
{
	const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s));
	const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s));
	const int64_t length = ce_movie_log_count(log);
	const EntryLayout held = sessionLayoutOf(s, log);
	const std::string blank = sessionEntryIn(s, held, nullptr, nullptr);
	EntryLayout wider;
	std::string newKey, columns;
	const bool widened = sessionWidenLayout(s, held, ce_movie_log_key(log), pressed.data(), moved.data(), wider, newKey, columns);
	if (widened && !engineHolds)
	{
		const char *was = ce_movie_log_key(log);
		const std::string names = EntryLayout::namesIn(newKey.substr(was != nullptr ? std::strlen(was) : 0));
		why = "This movie's columns (its LogKey) do not include " + names + ", so its input cannot hold them. Nothing was changed.";
		return false;
	}
	cJSON *entries = cJSON_CreateArray();
	std::vector<uint8_t> states(buttonCount);
	std::vector<int32_t> values(axisCount);
	for (int64_t i = 0; i < count; i++)
	{
		const char *now = frame + i < length ? ce_movie_log_entry(log, frame + i) : nullptr;
		std::fill(states.begin(), states.end(), 0);
		for (size_t a = 0; a < axisCount; a++) values[a] = ce_session_axis_neutral(s, static_cast<int64_t>(a));
		if (!sessionDecodeIn(s, held, now != nullptr ? now : blank.c_str(), states.data(), values.data()))
		{
			why = "The movie's input on frame " + std::to_string(frame + i) + " cannot be read.";
			cJSON_Delete(entries);
			return false;
		}
		change(frame + i, states.data(), values.data());
		cJSON_AddItemToArray(entries, cJSON_CreateString(sessionEntryIn(s, widened ? wider : held, states.data(), values.data()).c_str()));
	}
	if (widened)
	{
		cJSON_AddStringToObject(step, "key", newKey.c_str());
		cJSON_AddStringToObject(step, "columns", columns.c_str());
	}
	cJSON_AddNumberToObject(step, "frame", static_cast<double>(frame));
	cJSON_AddItemToObject(step, "entries", entries);
	return true;
}

/* A button of this machine by its name, among those plugged in; the count of
 * buttons when there is none. */
size_t buttonNamed(ce_session *s, const char *name)
{
	const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s));
	for (size_t i = 0; i < buttonCount; i++)
	{
		if (std::strcmp(name, ce_session_button_name(s, static_cast<int64_t>(i))) == 0)
			return ce_session_button_active(s, static_cast<int64_t>(i)) != 0 ? i : buttonCount;
	}
	return buttonCount;
}

/* The most frames press_buttons writes in one step. */
const int64_t kMostPressFrames = 100000;

bool prepareEdit(ce_session *s, const ce_movie_log *log, bool engineHolds, int64_t frameNow, std::string &tool, std::string &argumentsJson, McpResult &refusal)
{
	auto refuse = [&](const std::string &why)
	{
		refusal = mcpError(why);
		return false;
	};
	if (s == nullptr || log == nullptr) return refuse(kNoMovie);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const int64_t length = ce_movie_log_count(log);
	/* the lines are written and read in the held movie's own columns */
	const EntryLayout held = sessionLayoutOf(s, log);
	const std::string blank = sessionEntryIn(s, held, nullptr, nullptr);
	cJSON *step = cJSON_CreateObject();
	std::string why;
	int64_t frame = 0, count = 0;

	if (tool == "insert_frames" || tool == "delete_frames")
	{
		const bool inserting = tool == "insert_frames";
		if (!integerArgument(arguments, "frame", 0, inserting ? length : length - 1, -1, frame) || frame < 0)
			why = inserting ? "'frame' must be between 0 and the movie's length, " + std::to_string(length) + "."
				: length == 0 ? "The movie has no frames."
				: "'frame' must be a frame of the movie, which has " + std::to_string(length) + ".";
		else if (!integerArgument(arguments, "count", 1, 100000, 1, count)) why = "'count' must be between 1 and 100000.";
		else
		{
			/* deleting past the end stops at the end */
			if (!inserting && count > length - frame) count = length - frame;
			cJSON_AddNumberToObject(step, "frame", static_cast<double>(frame));
			cJSON_AddNumberToObject(step, "count", static_cast<double>(count));
			cJSON_AddStringToObject(step, "label", labelled(inserting ? "insert frames" : "delete frames", frame, count).c_str());
		}
	}
	else if (tool == "set_input")
	{
		const cJSON *entries = cJSON_GetObjectItemCaseSensitive(arguments, "entries");
		count = cJSON_IsArray(entries) ? cJSON_GetArraySize(entries) : 0;
		if (!integerArgument(arguments, "frame", 0, INT32_MAX - 2000, -1, frame) || frame < 0) why = "'frame' must be a frame number, 0 or more.";
		else if (frame > length + kMostPastEnd) why = pastEndRefusal(length);
		else if (count < 1 || count > 1000) why = "'entries' must be between 1 and 1000 lines of input, as get_input returns them.";
		else
		{
			/* every line is checked before one is written */
			int64_t at = 0;
			const cJSON *entry = nullptr;
			cJSON_ArrayForEach(entry, entries)
			{
				at++;
				if (cJSON_IsString(entry) && lineFits(s, held, entry->valuestring, blank)) continue;
				why = "Line " + std::to_string(at) + " of 'entries' does not have the shape of this movie's input lines. get_input shows them.";
				break;
			}
			if (why.empty())
			{
				cJSON_AddNumberToObject(step, "frame", static_cast<double>(frame));
				cJSON_AddItemToObject(step, "entries", cJSON_Duplicate(entries, 1));
				cJSON_AddStringToObject(step, "label", labelled("set input", frame, count).c_str());
			}
		}
	}
	else if (tool == "press_buttons")
	{
		/* Each press: its buttons, the frame it starts on (given, or counted
		 * from the frame the machine is on) and how long it is held. They
		 * become the lines from the first press's frame to the end of the
		 * last, one step; a button not named keeps what it had. */
		const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s));
		const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s));
		/* the frame the frontend says the machine is on: in the window that is
		 * TAStudio's, which the session's own count need not be */
		const int64_t now = frameNow >= 0 ? frameNow : ce_session_frame(s);
		struct Press { int64_t from, until; std::vector<size_t> buttons; };
		std::vector<Press> presses;
		const cJSON *list = cJSON_GetObjectItemCaseSensitive(arguments, "presses");
		if (!cJSON_IsArray(list) || cJSON_GetArraySize(list) < 1 || cJSON_GetArraySize(list) > 1000)
			why = "'presses' must be a list of 1 to 1000 presses, such as [{\"after\": 60, \"buttons\": [\"P1 Start\"]}].";
		const cJSON *item = nullptr;
		int64_t number = 0;
		cJSON_ArrayForEach(item, (why.empty() ? list : nullptr))
		{
			number++;
			const std::string which = "Press " + std::to_string(number);
			Press press;
			int64_t at = -1, after = -1, hold = 0;
			const bool hasFrame = cJSON_GetObjectItemCaseSensitive(item, "frame") != nullptr;
			const bool hasAfter = cJSON_GetObjectItemCaseSensitive(item, "after") != nullptr;
			const cJSON *buttons = cJSON_GetObjectItemCaseSensitive(item, "buttons");
			if (!cJSON_IsObject(item)) why = which + " is not an object.";
			else if (hasFrame == hasAfter) why = which + " needs 'frame' or 'after', one of them.";
			else if (hasFrame && (!integerArgument(item, "frame", 0, INT32_MAX - 20000, -1, at) || at < 0)) why = which + ": 'frame' must be a frame number, 0 or more.";
			else if (hasAfter && (!integerArgument(item, "after", 0, INT32_MAX - 20000, -1, after) || after < 0)) why = which + ": 'after' must be a number of frames, 0 or more.";
			else if (!integerArgument(item, "hold", 1, 10000, 6, hold)) why = which + ": 'hold' must be between 1 and 10000 frames.";
			else if (!cJSON_IsArray(buttons) || cJSON_GetArraySize(buttons) < 1) why = which + ": 'buttons' must be a list of button names, such as [\"P1 Start\"].";
			if (!why.empty()) break;
			press.from = hasFrame ? at : now + after;
			press.until = press.from + hold;
			const cJSON *name = nullptr;
			cJSON_ArrayForEach(name, buttons)
			{
				const size_t index = cJSON_IsString(name) ? buttonNamed(s, name->valuestring) : buttonCount;
				if (index == buttonCount)
				{
					why = which + ": there is no button called '" + (cJSON_IsString(name) ? name->valuestring : "?") + "'. 'controls' names them.";
					break;
				}
				press.buttons.push_back(index);
			}
			if (!why.empty()) break;
			presses.push_back(press);
		}
		int64_t first = INT64_MAX, last = 0;
		for (const Press &press : presses)
		{
			first = std::min(first, press.from);
			last = std::max(last, press.until);
		}
		if (!why.empty()) {}
		else if (first > length + kMostPastEnd) why = pastEndRefusal(length);
		else if (last - first > kMostPressFrames)
			why = "The presses span " + std::to_string(last - first) + " frames; one call writes at most " + std::to_string(kMostPressFrames) + ".";
		else
		{
			std::vector<uint8_t> pressed(buttonCount, 0);
			std::vector<int32_t> moved(axisCount, 0);
			for (const Press &press : presses) for (size_t b : press.buttons) pressed[b] = 1;
			for (size_t a = 0; a < axisCount; a++) moved[a] = ce_session_axis_neutral(s, static_cast<int64_t>(a));
			changedLines(s, log, engineHolds, first, last - first, pressed, moved,
				[&](int64_t at, uint8_t *states, int32_t *)
				{
					for (const Press &press : presses)
						if (at >= press.from && at < press.until) for (size_t b : press.buttons) states[b] = 1;
				}, step, why);
			cJSON_AddStringToObject(step, "label", labelled("press buttons", first, last - first).c_str());
			tool = "set_input";
		}
	}
	else /* set_buttons: the lines it amounts to */
	{
		const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s));
		const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s));
		std::vector<int> buttonAsked(buttonCount, -1); /* -1 keep, 0 release, 1 press */
		std::vector<bool> axisAsked(axisCount, false);
		std::vector<int32_t> axisValue(axisCount, 0);
		bool any = false;
		const cJSON *asked = nullptr;
		/* Each is an object from names to values. A list of names (a natural
		 * slip) has children with no names, and a name is what is looked up. */
		const cJSON *askedButtons = cJSON_GetObjectItemCaseSensitive(arguments, "buttons");
		const cJSON *askedAxesArg = cJSON_GetObjectItemCaseSensitive(arguments, "axes");
		if ((askedButtons != nullptr && !cJSON_IsObject(askedButtons)) || (askedAxesArg != nullptr && !cJSON_IsObject(askedAxesArg)))
		{
			why = "'buttons' and 'axes' are objects from names to values, such as {\"P1 A\": true} and {\"P1 X\": 128}.";
			askedButtons = nullptr;
		}
		cJSON_ArrayForEach(asked, askedButtons)
		{
			size_t index = buttonCount;
			for (size_t i = 0; i < buttonCount && index == buttonCount; i++)
			{
				if (std::strcmp(asked->string, ce_session_button_name(s, static_cast<int64_t>(i))) == 0) index = i;
			}
			if (index == buttonCount || ce_session_button_active(s, static_cast<int64_t>(index)) == 0)
				why = std::string("There is no button called '") + asked->string + "'. 'controls' names them.";
			else if (!cJSON_IsBool(asked)) why = std::string("The button '") + asked->string + "' must be true (pressed) or false (released).";
			else buttonAsked[index] = cJSON_IsTrue(asked) ? 1 : 0;
			any = true;
			if (!why.empty()) break;
		}
		const cJSON *askedAxes = why.empty() ? askedAxesArg : nullptr;
		cJSON_ArrayForEach(asked, askedAxes)
		{
			size_t index = axisCount;
			for (size_t i = 0; i < axisCount && index == axisCount; i++)
			{
				if (std::strcmp(asked->string, ce_session_axis_name(s, static_cast<int64_t>(i))) == 0) index = i;
			}
			if (index == axisCount || ce_session_axis_active(s, static_cast<int64_t>(index)) == 0)
				why = std::string("There is no axis called '") + asked->string + "'. 'controls' names them.";
			else if (!cJSON_IsNumber(asked) || asked->valuedouble != static_cast<double>(static_cast<int32_t>(asked->valuedouble)))
				why = std::string("The axis '") + asked->string + "' must be a whole number.";
			else
			{
				axisAsked[index] = true;
				axisValue[index] = static_cast<int32_t>(asked->valuedouble);
			}
			any = true;
			if (!why.empty()) break;
		}
		if (!why.empty()) {}
		else if (!integerArgument(arguments, "frame", 0, INT32_MAX - 20000, -1, frame) || frame < 0) why = "'frame' must be a frame number, 0 or more.";
		else if (frame > length + kMostPastEnd) why = pastEndRefusal(length);
		else if (!integerArgument(arguments, "count", 1, 10000, 1, count)) why = "'count' must be between 1 and 10000.";
		else if (!any) why = "Name at least one button in 'buttons' or one axis in 'axes'.";
		else
		{
			std::vector<uint8_t> pressed(buttonCount, 0);
			std::vector<int32_t> moved(axisCount, 0);
			for (size_t b = 0; b < buttonCount; b++) pressed[b] = buttonAsked[b] == 1 ? 1 : 0;
			for (size_t a = 0; a < axisCount; a++)
				moved[a] = axisAsked[a] ? axisValue[a] : ce_session_axis_neutral(s, static_cast<int64_t>(a));
			changedLines(s, log, engineHolds, frame, count, pressed, moved,
				[&](int64_t, uint8_t *states, int32_t *values)
				{
					for (size_t b = 0; b < buttonCount; b++) if (buttonAsked[b] >= 0) states[b] = static_cast<uint8_t>(buttonAsked[b]);
					for (size_t a = 0; a < axisCount; a++) if (axisAsked[a]) values[a] = axisValue[a];
				}, step, why);
			cJSON_AddStringToObject(step, "label", labelled("set controls", frame, count).c_str());
			tool = "set_input";
		}
	}
	cJSON_Delete(arguments);
	if (!why.empty())
	{
		cJSON_Delete(step);
		return refuse(why);
	}
	argumentsJson = printJson(step);
	cJSON_Delete(step);
	return true;
}

/* capture_series: the frames to take pictures of, and what to make of them,
 * as the step the movie's holder plays. Frames past the movie's end are
 * added as empty ones first ("blank" is such a line), so every frame is one
 * the movie reaches. */
bool prepareCapture(ce_session *s, const ce_movie_log *log, int64_t frameNow, std::string &argumentsJson, McpResult &refusal)
{
	if (s == nullptr || log == nullptr)
	{
		refusal = mcpError(kNoMovie);
		return false;
	}
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	std::string why;
	const cJSON *every = cJSON_GetObjectItemCaseSensitive(arguments, "every");
	const cJSON *first = cJSON_GetObjectItemCaseSensitive(arguments, "first");
	const cJSON *unit = cJSON_GetObjectItemCaseSensitive(arguments, "unit");
	const cJSON *folder = cJSON_GetObjectItemCaseSensitive(arguments, "folder");
	const cJSON *name = cJSON_GetObjectItemCaseSensitive(arguments, "name");
	const cJSON *images = cJSON_GetObjectItemCaseSensitive(arguments, "images");
	const int32_t numerator = ce_session_vsync_numerator(s), denominator = ce_session_vsync_denominator(s);
	const double framesPerSecond = denominator > 0 ? static_cast<double>(numerator) / denominator : 60.0;
	const bool seconds = cJSON_IsString(unit) && std::strcmp(unit->valuestring, "seconds") == 0;
	int64_t count = 0, maxWidth = 0;
	McpShotPlan plan;
	if (unit != nullptr && !seconds && !(cJSON_IsString(unit) && std::strcmp(unit->valuestring, "frames") == 0)) why = "'unit' is 'frames' or 'seconds'.";
	else if (!cJSON_IsNumber(every) || !(every->valuedouble > 0)) why = "'every' must be a number more than 0.";
	else if (first != nullptr && (!cJSON_IsNumber(first) || first->valuedouble < 0)) why = "'first' must be a number, 0 or more.";
	else if (!integerArgument(arguments, "count", 1, 50, -1, count) || count < 1) why = "'count' must be between 1 and 50.";
	else if (!integerArgument(arguments, "max_width", 32, 4096, 320, maxWidth)) why = "'max_width' must be between 32 and 4096.";
	else if (images != nullptr && !cJSON_IsBool(images)) why = "'images' is true or false.";
	else if (name != nullptr && (!cJSON_IsString(name) || std::strpbrk(name->valuestring, "/\\") != nullptr || name->valuestring[0] == '\0'))
		why = "'name' is a file's name, without a folder.";
	else if (folder != nullptr && !cJSON_IsString(folder)) why = "'folder' is the path of a folder.";
	if (why.empty())
	{
		/* frames are whole: a time in seconds is the frame nearest it */
		auto frames = [&](double amount) { return static_cast<int64_t>(std::llround(seconds ? amount * framesPerSecond : amount)); };
		const int64_t step = std::max<int64_t>(1, frames(every->valuedouble));
		const int64_t now = frameNow >= 0 ? frameNow : ce_session_frame(s);
		const int64_t start = now + (first != nullptr ? frames(first->valuedouble) : step);
		for (int64_t k = 0; k < count; k++) plan.frames.push_back(start + k * step);
		plan.maxWidth = maxWidth;
		plan.images = images == nullptr || cJSON_IsTrue(images);
		plan.framesPerSecond = framesPerSecond;
		if (cJSON_IsString(name)) plan.name = name->valuestring;
		if (cJSON_IsString(folder)) plan.folder = folder->valuestring;
		const int64_t length = ce_movie_log_count(log);
		std::error_code failed;
		if (plan.frames.back() > length + kMostPastEnd)
			why = "The last picture would be on frame " + std::to_string(plan.frames.back()) + ", more than "
				+ std::to_string(kMostPastEnd) + " frames past the movie's end, which is frame " + std::to_string(length) + ".";
		else if (!plan.folder.empty() && !std::filesystem::is_directory(std::filesystem::u8path(plan.folder), failed))
			why = "The folder '" + plan.folder + "' does not exist.";
		else if (!plan.folder.empty() && count > 1 && plan.name.find("{n}") == std::string::npos && plan.name.find("{frame}") == std::string::npos)
			why = "With more than one picture, 'name' must hold {n} or {frame}, or every file would have one name.";
	}
	if (why.empty() && !plan.folder.empty())
	{
		/* nothing is replaced: a series whose files are already there is not begun */
		for (size_t n = 1; n <= plan.frames.size() && why.empty(); n++)
		{
			const std::filesystem::path file = std::filesystem::u8path(plan.folder) / std::filesystem::u8path(shotName(plan, n));
			std::error_code failed;
			if (std::filesystem::exists(file, failed)) why = "A file is already at '" + file.u8string() + "', so no picture was taken.";
		}
	}
	cJSON_Delete(arguments);
	if (!why.empty())
	{
		refusal = mcpError(why);
		return false;
	}
	cJSON *step = cJSON_CreateObject();
	cJSON *list = cJSON_AddArrayToObject(step, "frames");
	for (int64_t f : plan.frames) cJSON_AddItemToArray(list, cJSON_CreateNumber(static_cast<double>(f)));
	cJSON_AddStringToObject(step, "blank", sessionEntryIn(s, sessionLayoutOf(s, log), nullptr, nullptr).c_str());
	cJSON_AddStringToObject(step, "label", labelled("add frames for capture_series", ce_movie_log_count(log),
		std::max<int64_t>(0, plan.frames.back() - ce_movie_log_count(log))).c_str());
	cJSON_AddNumberToObject(step, "max_width", static_cast<double>(plan.maxWidth));
	cJSON_AddStringToObject(step, "folder", plan.folder.c_str());
	cJSON_AddStringToObject(step, "name", plan.name.c_str());
	cJSON_AddBoolToObject(step, "images", plan.images);
	cJSON_AddNumberToObject(step, "frames_per_second", plan.framesPerSecond);
	argumentsJson = printJson(step);
	cJSON_Delete(step);
	return true;
}

/* A tool the tick box governs that does not change the movie or the machine:
 * it saves, or closes, the work as it is. No branch is saved before it, which
 * would itself be a change to the work: a project closed first thing would
 * have had unsaved changes the agent had not made. */
bool leavesTheWorkAsItIs(const std::string &tool)
{
	return tool == "save_project" || tool == "close_tastudio" || tool == "create_project";
}

bool isEdit(const std::string &tool)
{
	return tool == "set_input" || tool == "set_buttons" || tool == "press_buttons" || tool == "insert_frames" || tool == "delete_frames";
}

/* ---- RAM search ---- */

void dropSearch(Service &sv)
{
	if (sv.search != nullptr) ce_ramsearch_destroy(sv.search);
	sv.search = nullptr;
	sv.searchBase = nullptr;
}

/* Whether the search still stands on the memory it was started on. A project
 * closed, or another one opened, takes that memory away; the search is then
 * dropped before anything reads through its pointer. The session is asked by
 * serial: the sandbox puts guest memory at the same address every time, so a
 * reopened project of the same core has a domain of the same name, pointer
 * and size, and the old candidates would carry on against a new machine. */
bool searchStands(Service &sv, ce_session *s)
{
	if (sv.search == nullptr) return false;
	bool stands = false;
	if (s == nullptr || ce_session_serial(s) != sv.searchSession)
	{
		dropSearch(sv);
		return false;
	}
	for (int32_t i = 0; s != nullptr && i < ce_session_domain_count(s) && !stands; i++)
	{
		stands = sv.searchDomain == ce_session_domain_name(s, i)
			&& reinterpret_cast<const uint8_t *>(ce_session_domain_read_ptr(s, i)) == sv.searchBase
			&& ce_session_domain_size(s, i) == sv.searchBytes;
	}
	if (!stands) dropSearch(sv);
	return stands;
}

/* The agent's notes about a machine - its first write, its watches, the
 * condition advance_until waits for, the search - are about that machine. A
 * project closed and another opened (with a window or without one) starts
 * clean, rather than reporting the old machine's write, or reading the old
 * core's watches in the new one. */
void onMachine(Service &sv, ce_session *s)
{
	const uint64_t serial = s != nullptr ? ce_session_serial(s) : 0;
	if (serial == 0 || serial == sv.machineSession) return;
	if (sv.machineSession != 0)
	{
		sv.wroteMemoryAt = -1;
		sv.writeFrames.clear();
		sv.lostNote.clear();
		sv.watches.clear();
		sv.conditionSet = false;
		dropSearch(sv);
		/* the person's work is kept again before the agent's first change to
		 * THIS machine: once per server start left a project opened later with
		 * nothing kept (engine analysis, 2026-10-18). Not while a keep is being
		 * answered. */
		if (sv.kept != 1) sv.kept = 0;
	}
	sv.machineSession = serial;
}

const char kNoSearch[] = "No search is running. search_start begins one (a search ends when the project is closed).";

/* Every frame the machine runs, the agent's search counts its candidates'
 * changes, as the RAM Search window's does, and keeps their previous values
 * as asked. On the machine's thread, like every use of the search. */
void searchFrame(void *ctx, ce_session *s)
{
	Service &sv = *static_cast<Service *>(ctx);
	if (sv.search != nullptr && !sv.trying && ce_session_serial(s) == sv.searchSession) ce_ramsearch_update(sv.search, sv.searchPreviousType);
}

McpResult toolSearchStart(Service &sv, ce_session *s, const cJSON *arguments)
{
	const cJSON *domainItem = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	if (!cJSON_IsString(domainItem)) return mcpError("'domain' must be a domain from list_memory.");
	int64_t size = 1;
	if (!integerArgument(arguments, "size", 1, 8, 1, size) || (size != 1 && size != 2 && size != 4 && size != 8))
		return mcpError("'size' must be 1, 2, 4 or 8.");
	int32_t domainIndex = -1, busIndex = -1;
	findMemory(s, domainItem->valuestring, domainIndex, busIndex);
	if (domainIndex < 0)
		return mcpError(std::string("'") + domainItem->valuestring + (busIndex >= 0
			? "' is a bus. A search runs over a domain; list_memory names them."
			: "' is not a domain. list_memory names them."));
	/* read through the view that never runs the core's code (a search reads
	 * the whole domain at every step) */
	const uint8_t *base = reinterpret_cast<const uint8_t *>(ce_session_domain_read_ptr(s, domainIndex));
	if (base == nullptr) return mcpError(std::string("'") + domainItem->valuestring + "' cannot be searched: the core gives it no memory of its own.");
	const int64_t bytes = ce_session_domain_size(s, domainIndex);
	/* parts of the domain only, as [start, length] pairs */
	std::vector<int64_t> pairs;
	const cJSON *ranges = cJSON_GetObjectItemCaseSensitive(arguments, "ranges");
	const cJSON *range = nullptr;
	cJSON_ArrayForEach(range, ranges)
	{
		int64_t at[2] = { -1, -1 };
		for (int i = 0; i < 2; i++)
		{
			const cJSON *v = cJSON_GetArrayItem(range, i);
			uint64_t parsed = 0;
			if (cJSON_IsNumber(v) && v->valuedouble >= 0) at[i] = static_cast<int64_t>(v->valuedouble);
			else if (cJSON_IsString(v) && mcpParseInteger(v->valuestring, parsed) && parsed < static_cast<uint64_t>(INT64_MAX)) at[i] = static_cast<int64_t>(parsed);
		}
		if (at[0] < 0 || at[1] <= 0 || at[0] + at[1] > bytes)
			return mcpError("Each of 'ranges' is [start, length] inside the domain, which is " + std::to_string(bytes) + " bytes.");
		pairs.push_back(at[0]);
		pairs.push_back(at[1]);
	}
	dropSearch(sv);
	sv.searchBytes = bytes;
	sv.search = ce_ramsearch_create(base, nullptr, nullptr, sv.searchBytes);
	sv.searchDomain = domainItem->valuestring;
	sv.searchSession = ce_session_serial(s);
	sv.searchBase = base;
	sv.searchSize = static_cast<int>(size);
	sv.searchBigEndian = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "big_endian"));
	sv.searchPreviousType = 1;
	const bool misaligned = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "misaligned"));
	if (sv.search != nullptr)
	{
		ce_ramsearch_set_undo_enabled(sv.search, 1);
		if (!pairs.empty()) ce_ramsearch_set_ranges(sv.search, pairs.data(), static_cast<int64_t>(pairs.size() / 2));
	}
	/* detailed: a value now and a count of changes for each, kept up every frame */
	if (sv.search == nullptr || ce_ramsearch_start(sv.search, sv.searchSize, misaligned ? 1 : 0, sv.searchBigEndian ? 1 : 0, 1) != 0)
	{
		dropSearch(sv);
		return mcpError("There is not enough memory to search that domain.");
	}
	ce_session_add_frame_watch(s, searchFrame, &sv);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", sv.searchDomain.c_str());
	cJSON_AddNumberToObject(out, "size", sv.searchSize);
	cJSON_AddBoolToObject(out, "misaligned", misaligned);
	cJSON_AddNumberToObject(out, "searched_bytes", static_cast<double>(ce_ramsearch_searched_size(sv.search)));
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(ce_ramsearch_count(sv.search)));
	return textResult(out);
}

/* how a search's values are read: 0 unsigned, 1 signed, 2 float (engine.h) */
bool searchDisplay(const cJSON *arguments, int size, int &display, std::string &why)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(arguments, "display");
	const std::string name = cJSON_IsString(item) ? item->valuestring : "";
	display = name == "signed" || cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "signed")) ? 1 : name == "float" ? 2 : 0;
	if (!name.empty() && name != "signed" && name != "unsigned" && name != "float") why = "'display' must be unsigned, signed or float.";
	else if (display == 2 && size != 4 && size != 8) why = "A float is 4 or 8 bytes; this search's values are " + std::to_string(size) + ".";
	return why.empty();
}

/* a value of the search's as its 64 bits: a float's or a double's bits, a
 * negative number's two's complement, hex text as it is */
bool searchBits(const cJSON *item, int display, int size, uint64_t &bits)
{
	uint64_t parsed = 0;
	if (cJSON_IsString(item))
	{
		if (!mcpParseInteger(item->valuestring, parsed)) return false;
		bits = parsed;
		return true;
	}
	if (!cJSON_IsNumber(item)) return false;
	const double v = item->valuedouble;
	if (display == 2 && size == 4)
	{
		const float f = static_cast<float>(v);
		uint32_t b = 0;
		std::memcpy(&b, &f, 4);
		bits = b;
		return true;
	}
	if (display == 2)
	{
		std::memcpy(&bits, &v, 8);
		return true;
	}
	if (v != std::floor(v) || v < -9.3e18 || v > 1.85e19) return false;
	bits = v < 0 ? static_cast<uint64_t>(static_cast<int64_t>(v)) : static_cast<uint64_t>(v);
	return true;
}

/* a value's 64 bits as the number they are, read as asked */
double searchShown(uint64_t raw, int size, int display)
{
	if (display == 2 && size == 4)
	{
		float f = 0;
		const uint32_t b = static_cast<uint32_t>(raw);
		std::memcpy(&f, &b, 4);
		return f;
	}
	if (display == 2)
	{
		double d = 0;
		std::memcpy(&d, &raw, 8);
		return d;
	}
	if (display == 1)
		return size == 1 ? static_cast<int8_t>(raw) : size == 2 ? static_cast<int16_t>(raw)
			: size == 4 ? static_cast<int32_t>(raw) : static_cast<double>(static_cast<int64_t>(raw));
	return static_cast<double>(raw);
}

McpResult runSearchFilter(Service &sv, const cJSON *arguments)
{
	const cJSON *opItem = cJSON_GetObjectItemCaseSensitive(arguments, "op");
	const std::string opName = cJSON_IsString(opItem) ? opItem->valuestring : "";
	/* the engine's numbering (engine.h, RAM Search): 0 ==, 1 >, 2 >=, 3 <, 4 <=, 5 !=, 6 different by */
	const int op = opName == "equal" ? 0 : opName == "greater" ? 1 : opName == "greater_or_equal" ? 2
		: opName == "less" ? 3 : opName == "less_or_equal" ? 4 : opName == "not_equal" ? 5 : opName == "different_by" ? 6 : -1;
	if (op < 0) return mcpError("'op' must be one of equal, not_equal, greater, greater_or_equal, less, less_or_equal, different_by.");
	int display = 0;
	std::string why;
	if (!searchDisplay(arguments, sv.searchSize, display, why)) return mcpError(why);
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	const bool hasValue = valueItem != nullptr && !cJSON_IsNull(valueItem);
	const cJSON *compareItem = cJSON_GetObjectItemCaseSensitive(arguments, "compare");
	const std::string compareName = cJSON_IsString(compareItem) ? compareItem->valuestring : hasValue ? "value" : "previous";
	/* 0 previous value, 1 specific value, 2 specific address, 3 number of changes, 4 difference */
	const int compare = compareName == "previous" ? 0 : compareName == "value" ? 1 : compareName == "address" ? 2
		: compareName == "changes" ? 3 : compareName == "difference" ? 4 : -1;
	if (compare < 0) return mcpError("'compare' must be one of value, previous, address, changes, difference.");
	if (compare != 0 && !hasValue) return mcpError("Comparing by " + compareName + " needs a 'value'.");
	/* a count of changes and an address are whole numbers whatever the values are */
	const int valueDisplay = compare == 2 || compare == 3 ? 0 : display;
	uint64_t value = 0, by = 0;
	if (hasValue && !searchBits(valueItem, valueDisplay, sv.searchSize, value))
		return mcpError(valueDisplay == 2 ? "'value' must be a number." : "'value' must be a whole number, or hex text.");
	const cJSON *byItem = cJSON_GetObjectItemCaseSensitive(arguments, "by");
	if (op == 6 && (byItem == nullptr || !searchBits(byItem, display, sv.searchSize, by)))
		return mcpError("different_by needs 'by': how far apart, a number.");
	const cJSON *previousItem = cJSON_GetObjectItemCaseSensitive(arguments, "previous");
	const std::string previousName = cJSON_IsString(previousItem) ? previousItem->valuestring : "last_search";
	const int previous = previousName == "original" ? 0 : previousName == "last_search" ? 1 : previousName == "last_frame" ? 2
		: previousName == "last_change" ? 3 : -1;
	if (previous < 0) return mcpError("'previous' must be one of last_search, original, last_frame, last_change.");
	/* what was written, or loaded, since the last frame is what is compared */
	ce_ramsearch_catch_up(sv.search, sv.searchPreviousType);
	sv.searchPreviousType = previous;
	const int64_t removed = ce_ramsearch_search64(sv.search, compare, op, display, value, by, previous);
	if (removed < 0) return mcpError("There is not enough memory to run that search.");
	cJSON *out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "removed", static_cast<double>(removed));
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(ce_ramsearch_count(sv.search)));
	return textResult(out);
}

McpResult toolSearchFilter(Service &sv, ce_session *s, const cJSON *arguments)
{
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	return runSearchFilter(sv, arguments);
}

/* Where the candidates lie: runs of them close together, the biggest first,
 * and the distance that most often separates one from the next - a table's
 * record size, when the candidates are a field of each record. */
void addSearchShape(Service &sv, cJSON *out, int64_t count)
{
	const int64_t most = std::min<int64_t>(count, 200000);
	std::vector<uint64_t> at;
	at.reserve(static_cast<size_t>(most));
	for (int64_t i = 0; i < most; i++)
	{
		uint64_t address = 0, current = 0, previous = 0;
		uint32_t changes = 0;
		if (ce_ramsearch_row64(sv.search, i, &address, &current, &previous, &changes) == 0) break;
		at.push_back(address);
	}
	std::sort(at.begin(), at.end());
	struct Run { uint64_t from, to; int64_t count; };
	std::vector<Run> runs;
	std::map<uint64_t, int64_t> gaps;
	const uint64_t near = 256;
	for (size_t i = 0; i < at.size(); i++)
	{
		if (i > 0) gaps[at[i] - at[i - 1]]++;
		if (runs.empty() || at[i] - runs.back().to > near) runs.push_back({ at[i], at[i], 1 });
		else
		{
			runs.back().to = at[i];
			runs.back().count++;
		}
	}
	std::stable_sort(runs.begin(), runs.end(), [](const Run &a, const Run &b) { return a.count > b.count; });
	cJSON *clusters = cJSON_CreateArray();
	for (size_t i = 0; i < runs.size() && i < 8; i++)
	{
		cJSON *c = cJSON_CreateObject();
		cJSON_AddStringToObject(c, "from_hex", mcpHex(runs[i].from).c_str());
		cJSON_AddStringToObject(c, "to_hex", mcpHex(runs[i].to).c_str());
		cJSON_AddNumberToObject(c, "count", static_cast<double>(runs[i].count));
		cJSON_AddItemToArray(clusters, c);
	}
	cJSON_AddItemToObject(out, "clusters", clusters);
	if (runs.size() > 8) cJSON_AddNumberToObject(out, "more_clusters", static_cast<double>(runs.size() - 8));
	/* a stride is worth saying when it is most of the gaps, and more than one value's width */
	uint64_t stride = 0;
	int64_t seen = 0;
	for (const auto &[gap, n] : gaps)
		if (n > seen) { stride = gap; seen = n; }
	if (seen >= 3 && seen * 2 >= static_cast<int64_t>(at.size()) - 1 && stride > static_cast<uint64_t>(sv.searchSize))
	{
		cJSON_AddNumberToObject(out, "stride", static_cast<double>(stride));
		cJSON_AddStringToObject(out, "stride_hex", mcpHex(stride).c_str());
	}
}

McpResult toolSearchResults(Service &sv, ce_session *s, const cJSON *arguments)
{
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	int64_t limit = 0;
	if (!integerArgument(arguments, "limit", 1, 500, 50, limit)) return mcpError("'limit' must be between 1 and 500.");
	int display = 0;
	std::string why;
	if (!searchDisplay(arguments, sv.searchSize, display, why)) return mcpError(why);
	ce_ramsearch_catch_up(sv.search, sv.searchPreviousType);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", sv.searchDomain.c_str());
	cJSON_AddNumberToObject(out, "size", sv.searchSize);
	const int64_t count = ce_ramsearch_count(sv.search);
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(count));
	cJSON *results = cJSON_CreateArray();
	for (int64_t i = 0; i < count && i < limit; i++)
	{
		uint64_t address = 0, current = 0, previous = 0;
		uint32_t changes = 0;
		if (ce_ramsearch_row64(sv.search, i, &address, &current, &previous, &changes) == 0) break;
		/* the value now is read from the machine itself: the search keeps
		 * only what it needs to compare with */
		uint64_t now = 0;
		for (int b = 0; b < sv.searchSize; b++)
		{
			now |= static_cast<uint64_t>(sv.searchBase[address + static_cast<uint64_t>(b)])
				<< (8 * (sv.searchBigEndian ? sv.searchSize - 1 - b : b));
		}
		cJSON *row = cJSON_CreateObject();
		cJSON_AddNumberToObject(row, "address", static_cast<double>(address));
		cJSON_AddStringToObject(row, "address_hex", mcpHex(static_cast<uint64_t>(address)).c_str());
		const double shownNow = searchShown(now, sv.searchSize, display), shownBefore = searchShown(previous, sv.searchSize, display);
		cJSON_AddNumberToObject(row, "value", shownNow);
		cJSON_AddNumberToObject(row, "previous", shownBefore);
		cJSON_AddNumberToObject(row, "difference", shownNow - shownBefore);
		cJSON_AddNumberToObject(row, "changes", changes);
		cJSON_AddItemToArray(results, row);
	}
	cJSON_AddItemToObject(out, "results", results);
	if (count > 1) addSearchShape(sv, out, count);
	return textResult(out);
}

McpResult toolSearchUndo(Service &sv, ce_session *s)
{
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	if (ce_ramsearch_can_undo(sv.search) == 0) return mcpError("There is no search_filter to take back.");
	ce_ramsearch_undo(sv.search);
	cJSON *out = cJSON_CreateObject();
	cJSON_AddNumberToObject(out, "candidates", static_cast<double>(ce_ramsearch_count(sv.search)));
	return textResult(out);
}

/* A tool that writes to the machine, and the note that it did. */
/* The agent's memory writes that a state load has thrown away (issue 571).
 * A write lasts only until the machine goes back before it: a seek back, a
 * branch or a rewind loads a state from before the write, and the movie
 * replays without it. An agent not told so takes the write for having done
 * nothing. A load onto the frame of a write undoes it too: that frame's state
 * was kept when the machine arrived there, before the write. Called with the
 * lock held, on the machine's thread; what it finds goes into lostNote, for
 * the answer of the call being run. */
void lostWrites(Service &sv, ce_session *s)
{
	if (s == nullptr) return;
	const int64_t low = ce_session_take_loaded_low(s);
	if (low < 0 || sv.writeFrames.empty() || sv.writeFrames.back() < low) return;
	const auto from = std::lower_bound(sv.writeFrames.begin(), sv.writeFrames.end(), low);
	const int64_t first = *from, last = sv.writeFrames.back();
	sv.writeFrames.erase(from, sv.writeFrames.end());
	const std::string frames = first == last ? "frame " + std::to_string(first)
		: "frames " + std::to_string(first) + "-" + std::to_string(last);
	if (!sv.lostNote.empty()) sv.lostNote += " ";
	sv.lostNote += "Warning: the memory writes made on " + frames + " were thrown away: the machine was put back on frame "
		+ std::to_string(low) + " by loading the state kept there, from before them, and what is played from there runs "
		"without them. A write lasts only until the machine goes back before it; 'freeze' holds a value through seeks.";
}

/* The note lostWrites left for this call, added to its answer as a text of its own. */
void addLostNote(Service &sv, McpResult &result)
{
	if (sv.lostNote.empty()) return;
	cJSON *content = cJSON_Parse(result.content.c_str());
	if (cJSON_IsArray(content))
	{
		cJSON *note = cJSON_CreateObject();
		cJSON_AddStringToObject(note, "type", "text");
		cJSON_AddStringToObject(note, "text", sv.lostNote.c_str());
		cJSON_AddItemToArray(content, note);
		result.content = printJson(content);
	}
	cJSON_Delete(content);
	sv.lostNote.clear();
}

McpResult runWriteTool(Service &sv, const std::string &tool, ce_session *s, int64_t frame, const cJSON *arguments)
{
	{
		/* loads before this write are not about it */
		std::lock_guard<std::mutex> hold(sv.lock);
		lostWrites(sv, s);
	}
	const McpResult result = tool == "write_memory" ? toolWriteMemory(s, arguments)
		: tool == "set_property" ? toolSetProperty(s, arguments)
		: toolSetRegister(s, arguments);
	if (!result.isError)
	{
		std::lock_guard<std::mutex> hold(sv.lock);
		const int64_t at = frame >= 0 ? frame : ce_session_frame(s);
		if (sv.wroteMemoryAt < 0) sv.wroteMemoryAt = at;
		const auto place = std::lower_bound(sv.writeFrames.begin(), sv.writeFrames.end(), at);
		if (place == sv.writeFrames.end() || *place != at) sv.writeFrames.insert(place, at);
	}
	return result;
}

/* Frames played aside: the movie's own input from the frame the machine is on
 * (nothing pressed past its end), each followed by `after` (false stops),
 * and the machine put back as it was - the movie, its states and the
 * playback position untouched. trace_memory's and trace_cpu's (issue 571). */
bool playAside(Service &sv, ce_session *s, int64_t frames, const std::function<bool(int64_t)> &after, int64_t &played, std::string &why)
{
	{
		std::lock_guard<std::mutex> hold(sv.lock);
		lostWrites(sv, s);
	}
	uint64_t len = 0;
	const uint8_t *saved = ce_session_save_state(s, &len);
	if (saved == nullptr || len == 0)
	{
		why = std::string("The machine could not be kept to put back: ") + ce_session_last_error(s);
		return false;
	}
	const std::vector<uint8_t> kept(saved, saved + len);
	const int64_t from = ce_session_frame(s);
	const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s)), axisCount = static_cast<size_t>(ce_session_axis_count(s));
	std::vector<uint8_t> states(buttonCount);
	std::vector<int32_t> axes(axisCount);
	played = 0;
	sv.trying = true;
	ce_session_hold_callbacks(s, 1);
	for (int64_t f = 0; f < frames; f++)
	{
		if (ce_session_movie_get(s, from + f, states.data(), axisCount != 0 ? axes.data() : nullptr) != 0)
		{
			std::fill(states.begin(), states.end(), uint8_t{ 0 });
			for (size_t i = 0; i < axisCount; i++) axes[i] = ce_session_axis_neutral(s, static_cast<int64_t>(i));
		}
		for (size_t i = 0; i < axisCount; i++) ce_session_set_axis(s, static_cast<int32_t>(i), axes[i]);
		uint64_t first64 = 0;
		for (size_t b = 0; b < buttonCount; b++)
		{
			if (b < 64) first64 |= uint64_t{ states[b] != 0 } << b;
			else ce_session_set_button(s, static_cast<int32_t>(b), states[b]);
		}
		if (ce_session_frame_advance(s, first64, 0) < 0)
		{
			const char *death = ce_session_guest_death(s);
			why = std::string("The core stopped: ") + (death != nullptr ? death : "no reason was given");
			break;
		}
		played++;
		if (!after(from + f + 1)) break;
	}
	sv.trying = false;
	ce_session_hold_callbacks(s, 0);
	for (size_t b = 64; b < buttonCount; b++) ce_session_set_button(s, static_cast<int32_t>(b), 0);
	if (ce_session_load_state(s, kept.data(), kept.size()) != 0)
	{
		why = std::string("The machine could not be put back: ") + ce_session_last_error(s);
		return false;
	}
	ce_session_take_loaded_low(s);
	return why.empty();
}

/* trace_memory's watch: the hits on the places asked, whoever else watches */
struct MemoryTrace
{
	ce_session *s = nullptr;
	int32_t kind = 0;
	uint64_t from = 0, to = 0;
	int64_t frame = 0, maxHits = 0;
	bool registers = false;
	cJSON *hits = nullptr;
	int64_t count = 0;
};

int32_t traceHit(void *ctx, int32_t kind, int32_t, uint64_t address, uint64_t value, int32_t size, uint64_t *)
{
	MemoryTrace &t = *static_cast<MemoryTrace *>(ctx);
	if (kind != t.kind || address < t.from || address >= t.to || t.count >= t.maxHits) return 0;
	t.count++;
	cJSON *hit = cJSON_CreateObject();
	cJSON_AddNumberToObject(hit, "frame", static_cast<double>(t.frame));
	cJSON_AddStringToObject(hit, "address_hex", mcpHex(address).c_str());
	cJSON_AddNumberToObject(hit, "value", static_cast<double>(value));
	cJSON_AddStringToObject(hit, "value_hex", mcpHex(value).c_str());
	if (size > 0) cJSON_AddNumberToObject(hit, "size", size);
	uint64_t pc = 0;
	if (ce_session_memory_pc(t.s, &pc) != 0) cJSON_AddStringToObject(hit, "pc_hex", mcpHex(pc).c_str());
	if (t.registers)
	{
		cJSON *regs = cJSON_CreateObject();
		for (int32_t i = 0; i < ce_session_register_count(t.s); i++)
			cJSON_AddStringToObject(regs, ce_session_register_name(t.s, i), mcpHex(static_cast<uint64_t>(ce_session_register_value(t.s, i))).c_str());
		cJSON_AddItemToObject(hit, "registers", regs);
	}
	cJSON_AddItemToArray(t.hits, hit);
	return 0;
}

McpResult toolTraceMemory(Service &sv, ce_session *s, const cJSON *arguments)
{
	const cJSON *kindItem = cJSON_GetObjectItemCaseSensitive(arguments, "kind");
	const std::string kindName = cJSON_IsString(kindItem) ? kindItem->valuestring : "";
	const int32_t kind = kindName == "exec" ? 0 : kindName == "read" ? 1 : kindName == "write" ? 2 : -1;
	if (kind < 0) return mcpError("'kind' must be read, write or exec.");
	int64_t address = 0, length = 0, frames = 0, maxHits = 0;
	if (!integerArgument(arguments, "address", 0, INT64_MAX / 8, -1, address) || address < 0) return mcpError("'address' must be 0 or more.");
	if (!integerArgument(arguments, "length", 1, 64, 1, length)) return mcpError("'length' must be between 1 and 64.");
	if (!integerArgument(arguments, "frames", 1, 3600, 60, frames)) return mcpError("'frames' must be between 1 and 3600.");
	if (!integerArgument(arguments, "max_hits", 1, 1000, 100, maxHits)) return mcpError("'max_hits' must be between 1 and 1000.");
	std::vector<std::string> scopes;
	{
		std::istringstream lines(ce_session_memory_scopes(s));
		std::string line;
		while (std::getline(lines, line)) if (!line.empty()) scopes.push_back(line);
	}
	if (scopes.empty()) return mcpError("This core does not report the machine's memory accesses, so nothing can be traced in it.");
	const cJSON *scopeItem = cJSON_GetObjectItemCaseSensitive(arguments, "scope");
	int32_t scope = -1;
	if (cJSON_IsString(scopeItem))
	{
		for (size_t i = 0; i < scopes.size(); i++) if (scopes[i] == scopeItem->valuestring) scope = static_cast<int32_t>(i);
		if (scope < 0) return mcpError(std::string("The core has no scope called '") + scopeItem->valuestring + "'.");
	}
	/* the first scope that takes the watch, when none was named */
	std::vector<int32_t> tried = scope >= 0 ? std::vector<int32_t>{ scope } : std::vector<int32_t>{};
	if (scope < 0) for (size_t i = 0; i < scopes.size(); i++) tried.push_back(static_cast<int32_t>(i));
	int32_t watching = -1;
	int64_t watched = 0;
	for (const int32_t sc : tried)
	{
		watched = 0;
		for (int64_t a = 0; a < length; a++)
			if (ce_session_watch_memory(s, kind, sc, static_cast<uint64_t>(address + a), 1) != 0) watched++;
			else break;
		if (watched == length) { watching = sc; break; }
		for (int64_t a = 0; a < watched; a++) ce_session_watch_memory(s, kind, sc, static_cast<uint64_t>(address + a), 0);
	}
	if (watching < 0) return mcpError("The core cannot watch that: no scope of it (" + std::string(ce_session_memory_scopes(s)) + ") takes a " + kindName + " watch there.");
	MemoryTrace trace;
	trace.s = s;
	trace.kind = kind;
	trace.from = static_cast<uint64_t>(address);
	trace.to = static_cast<uint64_t>(address + length);
	trace.maxHits = maxHits;
	trace.registers = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "registers"));
	trace.hits = cJSON_CreateArray();
	trace.frame = ce_session_frame(s) + 1;
	ce_session_add_memory_observer(s, traceHit, &trace);
	int64_t played = 0;
	std::string why;
	const bool ok = playAside(sv, s, frames, [&](int64_t done) {
		trace.frame = done + 1;
		return trace.count < maxHits;
	}, played, why);
	ce_session_remove_memory_observer(s, traceHit, &trace);
	for (int64_t a = 0; a < length; a++) ce_session_watch_memory(s, kind, watching, static_cast<uint64_t>(address + a), 0);
	if (!ok && played == 0)
	{
		cJSON_Delete(trace.hits);
		return mcpError(why);
	}
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "scope", scopes[static_cast<size_t>(watching)].c_str());
	cJSON_AddNumberToObject(out, "frames_played", static_cast<double>(played));
	cJSON_AddNumberToObject(out, "hit_count", static_cast<double>(trace.count));
	cJSON_AddStringToObject(out, "stopped_at", trace.count >= maxHits ? "max_hits" : !why.empty() ? "core_stopped" : "frames");
	cJSON_AddBoolToObject(out, "pc_reported", ce_session_memory_pc(s, nullptr) != 0);
	if (ce_session_memory_pc(s, nullptr) == 0)
		cJSON_AddStringToObject(out, "note", "This core does not say which instruction made each access; registers read at it may already be past it.");
	if (!why.empty()) cJSON_AddStringToObject(out, "core_stopped", why.c_str());
	cJSON_AddItemToObject(out, "hits", trace.hits);
	return textResult(out);
}

McpResult toolTraceCpu(Service &sv, ce_session *s, const cJSON *arguments)
{
	if (ce_session_trace_available(s) == 0) return mcpError("This core keeps no trace of the instructions it runs.");
	int64_t frames = 0, maxLines = 0;
	if (!integerArgument(arguments, "frames", 1, 600, 1, frames)) return mcpError("'frames' must be between 1 and 600.");
	if (!integerArgument(arguments, "max_lines", 1, 5000, 500, maxLines)) return mcpError("'max_lines' must be between 1 and 5000.");
	const cJSON *filterItem = cJSON_GetObjectItemCaseSensitive(arguments, "filter");
	const std::string filter = cJSON_IsString(filterItem) ? filterItem->valuestring : "";
	const int32_t was = ce_session_trace_enabled(s);
	/* what was traced before is the window's Trace Logger's, not this call's */
	uint64_t len = 0;
	int32_t count = 0, overflow = 0;
	ce_session_trace_enable(s, 1);
	ce_session_trace_drain(s, &len, &count, &overflow);
	cJSON *lines = cJSON_CreateArray();
	int64_t total = 0, kept = 0;
	bool cut = false;
	int64_t played = 0;
	std::string why;
	playAside(sv, s, frames, [&](int64_t frame) {
		const uint8_t *text = ce_session_trace_drain(s, &len, &count, &overflow);
		cut = cut || overflow != 0;
		const char *at = reinterpret_cast<const char *>(text);
		for (int32_t i = 0; text != nullptr && i < count; i++)
		{
			const std::string line = at;
			at += line.size() + 1;
			if (!filter.empty() && line.find(filter) == std::string::npos) continue;
			total++;
			if (kept < maxLines)
			{
				cJSON *row = cJSON_CreateObject();
				cJSON_AddNumberToObject(row, "frame", static_cast<double>(frame));
				cJSON_AddStringToObject(row, "line", line.c_str());
				cJSON_AddItemToArray(lines, row);
				kept++;
			}
		}
		return true;
	}, played, why);
	ce_session_trace_enable(s, was);
	cJSON *out = cJSON_CreateObject();
	const char *header = ce_session_trace_header(s);
	if (header != nullptr) cJSON_AddStringToObject(out, "header", header);
	cJSON_AddNumberToObject(out, "frames_played", static_cast<double>(played));
	cJSON_AddNumberToObject(out, "lines_total", static_cast<double>(total));
	if (cut) cJSON_AddBoolToObject(out, "core_buffer_overflowed", true);
	if (!why.empty()) cJSON_AddStringToObject(out, "core_stopped", why.c_str());
	cJSON_AddItemToObject(out, "lines", lines);
	return textResult(out);
}

/* try_input (issue 571): frames run with input that is not the movie's, the
 * values asked for read, and the machine put back as it was, as a script's
 * show_future does. The engine's own frame advance runs them: the movie,
 * its saved states and the playback position are not touched. */
McpResult toolTryInput(Service &sv, ce_session *s, const cJSON *arguments)
{
	int64_t frames = 0, maxWidth = 0;
	if (!integerArgument(arguments, "frames", 1, 3600, 0, frames) || frames < 1) return mcpError("'frames' must be between 1 and 3600.");
	if (!integerArgument(arguments, "max_width", 32, 4096, 320, maxWidth)) return mcpError("'max_width' must be between 32 and 4096.");
	const size_t buttonCount = static_cast<size_t>(ce_session_button_count(s));
	const size_t axisCount = static_cast<size_t>(ce_session_axis_count(s));
	/* each frame's input: the lines given, or the buttons and axes held */
	std::vector<std::vector<uint8_t>> states;
	std::vector<std::vector<int32_t>> axes;
	const cJSON *entries = cJSON_GetObjectItemCaseSensitive(arguments, "entries");
	const cJSON *entry = nullptr;
	cJSON_ArrayForEach(entry, entries)
	{
		std::vector<uint8_t> st(buttonCount);
		std::vector<int32_t> ax(axisCount);
		if (!cJSON_IsString(entry) || ce_session_movie_entry_decode(s, entry->valuestring, st.data(), ax.data()) != 0)
			return mcpError("Entry " + std::to_string(states.size() + 1) + " is not an input line of this machine: get_input shows the form.");
		states.push_back(std::move(st));
		axes.push_back(std::move(ax));
	}
	if (states.empty())
	{
		std::vector<uint8_t> st(buttonCount);
		std::vector<int32_t> ax(axisCount);
		for (size_t i = 0; i < axisCount; i++) ax[i] = ce_session_axis_neutral(s, static_cast<int64_t>(i));
		const cJSON *name = nullptr;
		cJSON_ArrayForEach(name, cJSON_GetObjectItemCaseSensitive(arguments, "buttons"))
		{
			const size_t b = cJSON_IsString(name) ? buttonNamed(s, name->valuestring) : buttonCount;
			if (b >= buttonCount) return mcpError(std::string("There is no button called '") + (cJSON_IsString(name) ? name->valuestring : "?") + "' (controls lists them).");
			st[b] = 1;
		}
		const cJSON *axis = nullptr;
		cJSON_ArrayForEach(axis, cJSON_GetObjectItemCaseSensitive(arguments, "axes"))
		{
			size_t a = axisCount;
			for (size_t i = 0; i < axisCount; i++)
				if (axis->string != nullptr && std::strcmp(axis->string, ce_session_axis_name(s, static_cast<int64_t>(i))) == 0) a = i;
			if (a >= axisCount || !cJSON_IsNumber(axis)) return mcpError(std::string("There is no axis called '") + (axis->string != nullptr ? axis->string : "?") + "' (controls lists them).");
			ax[a] = static_cast<int32_t>(axis->valuedouble);
		}
		states.push_back(std::move(st));
		axes.push_back(std::move(ax));
	}
	std::vector<Source> reads;
	const cJSON *item = nullptr;
	cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(arguments, "read"))
	{
		Source source;
		std::string why;
		if (!parseSource(item, source, why)) return mcpError("'read': " + why);
		reads.push_back(source);
	}
	const bool eachFrame = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "each_frame"));
	const bool picture = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "picture"));
	auto readAll = [&]() {
		cJSON *values = cJSON_CreateArray();
		for (const Source &source : reads)
		{
			cJSON *v = cJSON_CreateObject();
			cJSON_AddStringToObject(v, "name", source.name.c_str());
			std::string text, why;
			bool isNumber = false;
			double number = 0;
			if (!readSource(s, source, text, isNumber, number, why)) cJSON_AddStringToObject(v, "error", why.c_str());
			else if (isNumber && !source.isProperty) cJSON_AddNumberToObject(v, "value", number);
			else cJSON_AddStringToObject(v, "value", text.c_str());
			cJSON_AddItemToArray(values, v);
		}
		return values;
	};
	/* loads before this call are about writes before it */
	{
		std::lock_guard<std::mutex> hold(sv.lock);
		lostWrites(sv, s);
	}
	uint64_t len = 0;
	const uint8_t *saved = ce_session_save_state(s, &len);
	if (saved == nullptr || len == 0) return mcpError(std::string("The machine could not be kept to put back: ") + ce_session_last_error(s));
	const std::vector<uint8_t> kept(saved, saved + len);
	const int64_t from = ce_session_frame(s);
	cJSON *out = cJSON_CreateObject();
	cJSON *each = eachFrame ? cJSON_CreateArray() : nullptr;
	int64_t played = 0, lag = 0;
	std::string died;
	sv.trying = true;
	ce_session_hold_callbacks(s, 1);
	for (int64_t f = 0; f < frames; f++)
	{
		const size_t at = std::min(static_cast<size_t>(f), states.size() - 1);
		for (size_t i = 0; i < axisCount; i++) ce_session_set_axis(s, static_cast<int32_t>(i), axes[at][i]);
		uint64_t first64 = 0;
		for (size_t b = 0; b < buttonCount; b++)
		{
			if (b < 64) first64 |= uint64_t{ states[at][b] != 0 } << b;
			else ce_session_set_button(s, static_cast<int32_t>(b), states[at][b]);
		}
		const int32_t ran = ce_session_frame_advance(s, first64, picture && f == frames - 1 ? 1 : 0);
		if (ran < 0)
		{
			const char *death = ce_session_guest_death(s);
			died = death != nullptr ? death : "the core stopped";
			break;
		}
		played++;
		lag += ran != 0 ? 1 : 0;
		if (each != nullptr)
		{
			cJSON *row = cJSON_CreateObject();
			cJSON_AddNumberToObject(row, "frame", static_cast<double>(from + f + 1));
			cJSON_AddItemToObject(row, "values", readAll());
			cJSON_AddItemToArray(each, row);
		}
	}
	sv.trying = false;
	ce_session_hold_callbacks(s, 0);
	cJSON_AddNumberToObject(out, "from_frame", static_cast<double>(from));
	cJSON_AddNumberToObject(out, "frames_played", static_cast<double>(played));
	cJSON_AddNumberToObject(out, "lag_frames", static_cast<double>(lag));
	if (!died.empty()) cJSON_AddStringToObject(out, "core_stopped", died.c_str());
	if (died.empty()) cJSON_AddItemToObject(out, "values", readAll());
	if (each != nullptr) cJSON_AddItemToObject(out, "each_frame", each);
	std::string png;
	int32_t outWidth = 0, outHeight = 0;
	if (picture && died.empty() && ce_session_video(s) != nullptr)
		pngOf(ce_session_video(s), ce_session_video_width(s), ce_session_video_height(s), maxWidth, png, outWidth, outHeight);
	for (size_t b = 64; b < buttonCount; b++) ce_session_set_button(s, static_cast<int32_t>(b), 0);
	/* the machine back as it was: the agent's writes before the try are in that state too */
	const bool back = ce_session_load_state(s, kept.data(), kept.size()) == 0;
	ce_session_take_loaded_low(s);
	cJSON_AddBoolToObject(out, "restored", back);
	McpResult result = textResult(out);
	if (!png.empty())
	{
		cJSON *content = cJSON_Parse(result.content.c_str());
		cJSON *image = cJSON_CreateObject();
		cJSON_AddStringToObject(image, "type", "image");
		cJSON_AddStringToObject(image, "data", base64(reinterpret_cast<const uint8_t *>(png.data()), png.size()).c_str());
		cJSON_AddStringToObject(image, "mimeType", "image/png");
		cJSON_InsertItemInArray(content, 0, image);
		result.content = printJson(content);
		cJSON_Delete(content);
	}
	if (!back) return mcpError(std::string("The machine could not be put back: ") + ce_session_last_error(s));
	return result;
}

McpResult runEngineTool(Service &sv, const std::string &tool, ce_session *s, const ce_movie_log *log,
	int64_t frame, const char *hostStatus, const cJSON *arguments)
{
	onMachine(sv, s);
	if (tool == "status")
	{
		/* where there is no window the machine is the agent's own to change */
		McpResult result = toolStatus(s, log, frame, hostStatus, sv.allowChanges.load() || g_local, sv.wroteMemoryAt,
			cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(arguments, "controls")));
		if (s != nullptr && !sv.watches.empty())
		{
			cJSON *content = cJSON_Parse(result.content.c_str());
			const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
			if (cJSON_IsString(text)) result.content = mcpTextContent(withWatches(sv, s, text->valuestring));
			cJSON_Delete(content);
		}
		return result;
	}
	if (tool == "get_input") return toolGetInput(log, arguments);
	if (tool == "list_cores" || tool == "describe_core")
	{
		std::vector<std::string> folders;
		{
			std::lock_guard<std::mutex> hold(sv.lock);
			folders = sv.coresFolders;
		}
		cJSON *copy = cJSON_Duplicate(arguments, 1);
		const std::string argumentsJson = copy != nullptr ? printJson(copy) : "{}";
		cJSON_Delete(copy);
		return tool == "list_cores" ? mcpListCores(folders) : mcpDescribeCore(folders, argumentsJson);
	}
	/* a search whose memory is gone is dropped before anything else runs */
	searchStands(sv, s);
	if (s == nullptr) return mcpError(kNoMachine);
	if (tool == "controls") return toolControls(s, log);
	if (tool == "screenshot")
	{
		const cJSON *layers = cJSON_GetObjectItemCaseSensitive(arguments, "layers");
		if (cJSON_IsString(layers) && std::strcmp(layers->valuestring, "with_lua") == 0)
			return mcpError("The picture as the window shows it needs the window: without one there is no Lua drawing. "
				"Without 'layers', screenshot gives the machine's picture.");
		return toolScreenshot(s, frame, arguments);
	}
	if (tool == "list_memory") return toolListMemory(s);
	if (tool == "read_memory") return toolReadMemory(s, arguments);
	if (tool == "registers") return toolRegisters(s, arguments);
	if (tool == "list_properties") return toolListProperties(s, arguments);
	if (tool == "get_property") return toolGetProperty(s, arguments);
	if (tool == "write_memory" || tool == "set_property" || tool == "set_register") return runWriteTool(sv, tool, s, frame, arguments);
	if (tool == "watch") return toolWatch(sv, s, arguments);
	if (tool == "list_freezes") return toolListFreezes(s);
	if (tool == "try_input") return toolTryInput(sv, s, arguments);
	if (tool == "trace_memory") return toolTraceMemory(sv, s, arguments);
	if (tool == "trace_cpu") return toolTraceCpu(sv, s, arguments);
	if (tool == "freeze" || tool == "unfreeze") return mcpFreezeTool(s, tool, printJson(const_cast<cJSON *>(arguments)));
	if (tool == "search_start") return toolSearchStart(sv, s, arguments);
	if (tool == "search_filter") return toolSearchFilter(sv, s, arguments);
	if (tool == "search_results") return toolSearchResults(sv, s, arguments);
	if (tool == "search_undo") return toolSearchUndo(sv, s);
	return mcpError("Chimera does not run the tool '" + tool + "' yet.");
}

} // namespace

bool mcpReadShotPlan(const std::string &preparedJson, McpShotPlan &plan)
{
	cJSON *step = cJSON_Parse(preparedJson.c_str());
	const cJSON *frames = cJSON_GetObjectItemCaseSensitive(step, "frames");
	const bool ok = cJSON_IsArray(frames) && cJSON_GetArraySize(frames) > 0;
	plan.frames.clear();
	const cJSON *frame = nullptr;
	cJSON_ArrayForEach(frame, (ok ? frames : nullptr)) plan.frames.push_back(static_cast<int64_t>(frame->valuedouble));
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(step, "max_width");
	if (cJSON_IsNumber(item)) plan.maxWidth = static_cast<int64_t>(item->valuedouble);
	item = cJSON_GetObjectItemCaseSensitive(step, "folder");
	if (cJSON_IsString(item)) plan.folder = item->valuestring;
	item = cJSON_GetObjectItemCaseSensitive(step, "name");
	if (cJSON_IsString(item)) plan.name = item->valuestring;
	plan.images = !cJSON_IsFalse(cJSON_GetObjectItemCaseSensitive(step, "images"));
	item = cJSON_GetObjectItemCaseSensitive(step, "frames_per_second");
	if (cJSON_IsNumber(item)) plan.framesPerSecond = item->valuedouble;
	cJSON_Delete(step);
	return ok;
}

bool mcpTakeShot(ce_session *s, const McpShotPlan &plan, size_t n, int64_t frame, std::string &factsJson, std::string &pictureBase64, std::string &why)
{
	const uint32_t *bgra = ce_session_video(s);
	const int32_t width = ce_session_video_width(s);
	const int32_t height = ce_session_video_height(s);
	if (bgra == nullptr || width <= 0 || height <= 0)
	{
		why = "The machine has not drawn a picture yet.";
		return false;
	}
	cJSON *facts = cJSON_CreateObject();
	cJSON_AddNumberToObject(facts, "n", static_cast<double>(n));
	cJSON_AddNumberToObject(facts, "frame", static_cast<double>(frame));
	if (plan.framesPerSecond > 0)
		cJSON_AddNumberToObject(facts, "seconds", std::round(static_cast<double>(frame) / plan.framesPerSecond * 1000.0) / 1000.0);
	addLook(facts, bgra, width, height);
	std::string png;
	int32_t outWidth = 0, outHeight = 0;
	if (!plan.folder.empty())
	{
		/* the file is the picture at its own size */
		const std::filesystem::path file = std::filesystem::u8path(plan.folder) / std::filesystem::u8path(shotName(plan, n));
		std::error_code failed;
		FileWriter out;
		if (std::filesystem::exists(file, failed)) cJSON_AddStringToObject(facts, "file_error", "a file is already there, so it was not replaced");
		else if (!pngOf(bgra, width, height, width, png, outWidth, outHeight) || !out.open(file.u8string().c_str())
			|| !out.write(png.data(), png.size()) || !out.commit())
			cJSON_AddStringToObject(facts, "file_error", "the file could not be written");
		else cJSON_AddStringToObject(facts, "file", file.u8string().c_str());
	}
	if (plan.images)
	{
		if (!pngOf(bgra, width, height, plan.maxWidth, png, outWidth, outHeight))
		{
			cJSON_Delete(facts);
			why = "The picture could not be made into a PNG.";
			return false;
		}
		pictureBase64 = base64(reinterpret_cast<const uint8_t *>(png.data()), png.size());
	}
	else
	{
		pictureBase64.clear();
	}
	factsJson = printJson(facts);
	cJSON_Delete(facts);
	return true;
}

std::string mcpShotsContent(const std::string &answerJson, const std::vector<std::string> &facts, const std::vector<std::string> &pictures)
{
	cJSON *answer = cJSON_Parse(answerJson.c_str());
	if (!cJSON_IsObject(answer))
	{
		cJSON_Delete(answer);
		answer = cJSON_CreateObject();
	}
	cJSON *shots = cJSON_AddArrayToObject(answer, "shots");
	for (const std::string &fact : facts)
	{
		cJSON *item = cJSON_Parse(fact.c_str());
		if (item != nullptr) cJSON_AddItemToArray(shots, item);
	}
	cJSON *content = cJSON_CreateArray();
	cJSON *text = cJSON_CreateObject();
	cJSON_AddStringToObject(text, "type", "text");
	const std::string answerText = printJson(answer);
	cJSON_Delete(answer);
	cJSON_AddStringToObject(text, "text", answerText.c_str());
	cJSON_AddItemToArray(content, text);
	for (const std::string &picture : pictures)
	{
		if (picture.empty()) continue;
		cJSON *image = cJSON_CreateObject();
		cJSON_AddStringToObject(image, "type", "image");
		cJSON_AddStringToObject(image, "data", picture.c_str());
		cJSON_AddStringToObject(image, "mimeType", "image/png");
		cJSON_AddItemToArray(content, image);
	}
	const std::string out = printJson(content);
	cJSON_Delete(content);
	return out;
}

McpResult mcpSearchFilter(ce_session *s, const std::string &argumentsJson)
{
	Service &sv = service();
	onMachine(sv, s);
	if (!searchStands(sv, s)) return mcpError(kNoSearch);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	McpResult result = runSearchFilter(sv, arguments);
	cJSON_Delete(arguments);
	return result;
}

McpResult mcpDiffMemory(Service &sv, ce_session *s, int phase, const std::string &argumentsJson)
{
	if (s == nullptr) return mcpError(kNoMachine);
	cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
	const cJSON *domainItem = cJSON_GetObjectItemCaseSensitive(arguments, "domain");
	const std::string domain = cJSON_IsString(domainItem) ? domainItem->valuestring : "";
	int64_t address = 0, length = 0, frameA = 0, frameB = 0;
	const bool ok = integerArgument(arguments, "address", 0, INT64_MAX / 8, -1, address) && address >= 0
		&& integerArgument(arguments, "length", 1, 16 << 20, -1, length) && length >= 1
		&& integerArgument(arguments, "frame_a", 0, INT32_MAX, 0, frameA) && integerArgument(arguments, "frame_b", 0, INT32_MAX, 0, frameB);
	cJSON_Delete(arguments);
	if (!ok) return mcpError("'address' must be 0 or more, and 'length' between 1 and 16 MiB.");
	int32_t domainIndex = -1, busIndex = -1;
	if (!findMemory(s, domain, domainIndex, busIndex)) return mcpError("There is no domain or bus called '" + domain + "' (list_memory lists them).");
	const int64_t limit = domainIndex >= 0 ? ce_session_domain_size(s, domainIndex) : ce_session_bus_size(s, busIndex);
	if (address > limit - length) return mcpError("The range goes past the end of '" + domain + "', which is " + std::to_string(limit) + " bytes.");
	std::vector<uint8_t> now(static_cast<size_t>(length));
	const int64_t got = domainIndex >= 0 ? ce_session_domain_read(s, domainIndex, address, now.data(), length)
		: ce_session_bus_read(s, busIndex, address, now.data(), length);
	if (got != length) return mcpError("'" + domain + "' could not be read there.");
	if (phase == 0)
	{
		sv.diffBefore = std::move(now);
		sv.diffFrame = ce_session_frame(s);
		cJSON *out = cJSON_CreateObject();
		cJSON_AddNumberToObject(out, "frame_a", static_cast<double>(sv.diffFrame));
		return textResult(out);
	}
	if (sv.diffBefore.size() != now.size()) return mcpError("diff_memory lost the bytes of frame_a.");
	static const char hex[] = "0123456789ABCDEF";
	auto hexOf = [&](const std::vector<uint8_t> &v, int64_t from, int64_t to) {
		std::string t;
		for (int64_t i = from; i < to; i++) { t += hex[v[static_cast<size_t>(i)] >> 4]; t += hex[v[static_cast<size_t>(i)] & 15]; }
		return t;
	};
	cJSON *out = cJSON_CreateObject();
	cJSON_AddStringToObject(out, "domain", domain.c_str());
	cJSON_AddStringToObject(out, "address_hex", mcpHex(static_cast<uint64_t>(address)).c_str());
	cJSON_AddNumberToObject(out, "length", static_cast<double>(length));
	cJSON_AddNumberToObject(out, "frame_a", static_cast<double>(sv.diffFrame));
	cJSON_AddNumberToObject(out, "frame_b", static_cast<double>(ce_session_frame(s)));
	cJSON *runs = cJSON_CreateArray();
	int64_t changed = 0, runCount = 0;
	for (int64_t i = 0; i < length;)
	{
		if (sv.diffBefore[static_cast<size_t>(i)] == now[static_cast<size_t>(i)]) { i++; continue; }
		int64_t end = i;
		while (end < length && sv.diffBefore[static_cast<size_t>(end)] != now[static_cast<size_t>(end)]) end++;
		changed += end - i;
		if (runCount++ < 200)
		{
			cJSON *run = cJSON_CreateObject();
			cJSON_AddNumberToObject(run, "address", static_cast<double>(address + i));
			cJSON_AddStringToObject(run, "address_hex", mcpHex(static_cast<uint64_t>(address + i)).c_str());
			cJSON_AddStringToObject(run, "before_hex", hexOf(sv.diffBefore, i, std::min(end, i + 256)).c_str());
			cJSON_AddStringToObject(run, "after_hex", hexOf(now, i, std::min(end, i + 256)).c_str());
			if (end - i > 256) cJSON_AddNumberToObject(run, "length", static_cast<double>(end - i));
			cJSON_AddItemToArray(runs, run);
		}
		i = end;
	}
	cJSON_AddNumberToObject(out, "changed_bytes", static_cast<double>(changed));
	cJSON_AddNumberToObject(out, "runs_count", static_cast<double>(runCount));
	cJSON_AddItemToObject(out, "runs", runs);
	sv.diffBefore.clear();
	return textResult(out);
}

McpResult mcpDiffMemoryNow(ce_session *s, int phase, const std::string &argumentsJson)
{
	Service &sv = service();
	onMachine(sv, s);
	return mcpDiffMemory(sv, s, phase, argumentsJson);
}

McpResult mcpRunDirect(const std::string &tool, const std::string &argumentsJson, ce_session *s, const ce_movie_log *log,
	const std::string &hostStatusJson, const McpHostRunner &host)
{
	Service &sv = service();
	onMachine(sv, s);
	const McpTool *found = mcpFindTool(tool);
	if (found == nullptr) return mcpError("There is no tool called '" + tool + "'.");
	if (!found->host)
	{
		cJSON *arguments = cJSON_Parse(argumentsJson.c_str());
		McpResult result = runEngineTool(sv, tool, s, log, -1, hostStatusJson.c_str(), arguments);
		cJSON_Delete(arguments);
		std::lock_guard<std::mutex> hold(sv.lock);
		lostWrites(sv, s);
		addLostNote(sv, result);
		return result;
	}
	std::string hostTool = tool, hostArguments = argumentsJson;
	McpResult refusal;
	if (isEdit(tool) && !prepareEdit(s, log, true, -1, hostTool, hostArguments, refusal)) return refusal;
	if (tool == "capture_series" && !prepareCapture(s, log, -1, hostArguments, refusal)) return refusal;
	McpResult result = host(hostTool, hostArguments);
	if (!result.isError && carriesWatches(tool) && s != nullptr && !sv.watches.empty())
	{
		cJSON *content = cJSON_Parse(result.content.c_str());
		const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
		if (cJSON_IsString(text)) result.content = mcpTextContent(withWatches(sv, s, text->valuestring));
		cJSON_Delete(content);
	}
	std::lock_guard<std::mutex> hold(sv.lock);
	lostWrites(sv, s);
	addLostNote(sv, result);
	return result;
}

} // namespace chimera

/* ---- the ABI ---- */

using chimera::Service;
using chimera::service;

extern "C" {

int32_t ce_mcp_start(const char **error_out)
{
	static thread_local chimera::ThreadString error;   /* never destroyed: see thread_string.hpp */
	Service &sv = service();
	if (sv.running.load()) return 1;
	if (!sv.pipe.open(*error))
	{
		if (error_out != nullptr) *error_out = error->c_str();
		return 0;
	}
	{
		std::lock_guard<std::mutex> hold(sv.lock);
		sv.endpoint = sv.pipe.name();
		sv.state = CE_MCP_WAITING;
		sv.client.clear();
		sv.waiting.clear();
		sv.outgoing.clear();
		sv.withHost.clear();
		sv.shots.clear();
		sv.pending = 0;
		/* every start begins with changes not allowed */
		sv.allowChanges = false;
		sv.wroteMemoryAt = -1;
		sv.kept = 0;
		sv.keepError.clear();
		chimera::addLog(sv, "the server was started");
	}
	sv.running = true;
	sv.thread = std::thread(chimera::pipeThread, &sv);
	return 1;
}

void ce_mcp_stop(void)
{
	Service &sv = service();
	if (!sv.running.exchange(false)) return;
	if (sv.thread.joinable()) sv.thread.join();
	sv.pipe.close();
	std::lock_guard<std::mutex> hold(sv.lock);
	sv.state = CE_MCP_STOPPED;
	sv.client.clear();
	sv.waiting.clear();
	sv.outgoing.clear();
	sv.withHost.clear();
	sv.shots.clear();
	sv.pending = 0;
	sv.allowChanges = false;
	/* the agent's search ends with the server (this is the thread that runs
	 * the machine, as every call of ce_mcp_service is) */
	chimera::dropSearch(sv);
	sv.watches.clear();
	sv.conditionSet = false;
	chimera::addLog(sv, "the server was stopped");
}

void ce_mcp_name(const char *name)
{
	chimera::pipeName(name != nullptr ? name : "");
}

int32_t ce_mcp_state(void)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	return sv.state;
}

const char *ce_mcp_client(void)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	*held = sv.client;
	return held->c_str();
}

const char *ce_mcp_client_build(void)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	*held = sv.client.empty() ? "" : sv.clientBuild.empty() ? "older" : sv.clientBuild;
	return held->c_str();
}

void ce_mcp_program(const char *path)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	sv.program = path != nullptr ? path : "";
}

const char *ce_mcp_endpoint(void)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	*held = sv.endpoint;
	return held->c_str();
}

void ce_mcp_allow_changes(int32_t on)
{
	Service &sv = service();
	const bool allow = on != 0;
	if (sv.allowChanges.exchange(allow) == allow) return;
	std::lock_guard<std::mutex> hold(sv.lock);
	chimera::addLog(sv, allow ? "changes by the agent were allowed" : "changes by the agent were disallowed");
}

int32_t ce_mcp_changes_allowed(void) { return service().allowChanges.load() ? 1 : 0; }

int32_t ce_mcp_pending(void) { return service().pending.load() > 0 ? 1 : 0; }

int32_t ce_mcp_service(ce_session *s, const ce_movie_log *log, int64_t frame, const char *host_status_json,
	uint64_t *id_out, const char **tool_out, const char **arguments_out)
{
	static thread_local chimera::ThreadString heldTool, heldArguments;
	Service &sv = service();
	chimera::onMachine(sv, s);
	for (;;)
	{
		chimera::WaitingCall call;
		const chimera::McpTool *tool = nullptr;
		{
			std::lock_guard<std::mutex> hold(sv.lock);
			if (!sv.running.load() || sv.waiting.empty()) return 0;
			call = std::move(sv.waiting.front());
			sv.waiting.pop_front();
			sv.pending--;
			const long long waited = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::steady_clock::now() - call.at).count();
			if (waited > chimera::McpLink::kStaleMs)
			{
				/* the agent has been told by now that this call was not run,
				 * so it must not be */
				chimera::addLog(sv, call.tool + "  ->  NOT RUN: it waited " + std::to_string(waited / 1000) + " s for Chimera to be free");
				continue;
			}
			if (call.connection != sv.connection) continue; /* asked by an agent that has gone */
			sv.outgoing.push_back("{\"id\":" + std::to_string(call.id) + ",\"started\":true}");
			tool = chimera::mcpFindTool(call.tool);
			if (tool == nullptr)
			{
				chimera::finish(sv, call, chimera::mcpError("There is no tool called '" + call.tool + "'."));
				continue;
			}
			if (tool->changes && !sv.allowChanges.load())
			{
				chimera::finish(sv, call, chimera::mcpError(
					"Changes are not allowed. The person using Chimera can allow them: Tools > MCP Server... > "
					"Allow the agent to make changes."));
				continue;
			}
			if (tool->changes && sv.kept == 0 && !chimera::leavesTheWorkAsItIs(call.tool))
			{
				/* The first change since the server was started. The call goes
				 * back to the head of the line, and the frontend is asked first
				 * to keep the person's work as it is now. Its answer decides
				 * whether the call is then run. */
				sv.outgoing.pop_back(); /* not started after all */
				sv.waiting.push_front(std::move(call));
				sv.pending++;
				sv.kept = 1;
				sv.keepId = UINT64_MAX - sv.logTotal;
				*heldTool = "keep_work";
				*heldArguments = "{}";
				if (id_out != nullptr) *id_out = sv.keepId;
				if (tool_out != nullptr) *tool_out = heldTool->c_str();
				if (arguments_out != nullptr) *arguments_out = heldArguments->c_str();
				return 1;
			}
			if (tool->changes && sv.kept == 1)
			{
				/* the frontend has not answered yet: the call waits its turn */
				sv.outgoing.pop_back();
				sv.waiting.push_front(std::move(call));
				sv.pending++;
				return 0;
			}
			if (tool->changes && sv.kept == 3)
			{
				chimera::finish(sv, call, chimera::mcpError(
					"Nothing was changed: Chimera could not keep a copy of the work as it was before the agent's first change ("
					+ sv.keepError + "). Stop and start the server to try again."));
				continue;
			}
			/* the picture as the window shows it is the window's to take (issue 571) */
			const bool shown = call.tool == "screenshot" && call.arguments.find("\"with_lua\"") != std::string::npos;
			if (tool->host || shown)
			{
				*heldTool = call.tool;
				*heldArguments = call.arguments;
				/* an edit is checked, and worked out into the step it amounts
				 * to, before the movie's holder is asked to make it */
				chimera::McpResult refusal;
				if (chimera::isEdit(call.tool) && !chimera::prepareEdit(s, log, false, frame, *heldTool, *heldArguments, refusal))
				{
					chimera::finish(sv, call, refusal);
					continue;
				}
				if (call.tool == "capture_series" && !chimera::prepareCapture(s, log, frame, *heldArguments, refusal))
				{
					chimera::finish(sv, call, refusal);
					continue;
				}
				call.prepared = *heldArguments;
				if (id_out != nullptr) *id_out = call.id;
				if (tool_out != nullptr) *tool_out = heldTool->c_str();
				if (arguments_out != nullptr) *arguments_out = heldArguments->c_str();
				sv.withHost[call.id] = std::move(call);
				return 1;
			}
		}
		/* the engine's own tools run without the lock: they only touch the
		 * machine, which is this thread's */
		cJSON *arguments = cJSON_Parse(call.arguments.c_str());
		chimera::McpResult result = chimera::runEngineTool(sv, call.tool, s, log, frame, host_status_json, arguments);
		cJSON_Delete(arguments);
		std::lock_guard<std::mutex> hold(sv.lock);
		chimera::lostWrites(sv, s);
		chimera::addLostNote(sv, result);
		chimera::finish(sv, call, result);
	}
}

void ce_mcp_answer_picture(uint64_t id, const uint8_t *png, uint64_t png_len, const char *facts_json)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	const auto found = sv.withHost.find(id);
	if (found == sv.withHost.end()) return;
	chimera::McpResult result;
	if (png == nullptr || png_len == 0)
		result = chimera::mcpError("The picture as the window shows it needs the window, and this Chimera shows none (--headless). "
			"Without 'layers', screenshot gives the machine's picture.");
	else
	{
		cJSON *content = cJSON_CreateArray();
		cJSON *image = cJSON_CreateObject();
		cJSON_AddStringToObject(image, "type", "image");
		cJSON_AddStringToObject(image, "data", chimera::base64(png, static_cast<size_t>(png_len)).c_str());
		cJSON_AddStringToObject(image, "mimeType", "image/png");
		cJSON_AddItemToArray(content, image);
		cJSON *text = cJSON_CreateObject();
		cJSON_AddStringToObject(text, "type", "text");
		cJSON_AddStringToObject(text, "text", facts_json != nullptr ? facts_json : "{}");
		cJSON_AddItemToArray(content, text);
		result.content = chimera::printJson(content);
		cJSON_Delete(content);
	}
	chimera::finish(sv, found->second, result);
	sv.withHost.erase(found);
}

void ce_mcp_answer(ce_session *s, uint64_t id, const char *text, int32_t is_error)
{
	Service &sv = service();
	chimera::onMachine(sv, s);
	std::lock_guard<std::mutex> hold(sv.lock);
	if (sv.kept == 1 && id == sv.keepId)
	{
		/* the frontend's answer to "keep the person's work": not a call of the
		 * agent's, so nothing is sent; the call that waited is run next */
		sv.kept = is_error != 0 ? 3 : 2;
		sv.keepError = is_error != 0 && text != nullptr ? text : "";
		chimera::addLog(sv, is_error != 0
			? std::string("the work could not be kept before the agent's first change: ") + sv.keepError
			: std::string("before the agent's first change, the work as it was has been kept: ") + (text != nullptr ? text : ""));
		return;
	}
	const auto found = sv.withHost.find(id);
	if (found == sv.withHost.end()) return;
	chimera::McpResult result;
	std::string answer = text != nullptr ? text : "";
	if (is_error == 0 && chimera::carriesWatches(found->second.tool)) answer = chimera::withWatches(sv, s, answer);
	result.content = chimera::mcpTextContent(answer);
	result.isError = is_error != 0;
	/* capture_series: the pictures taken go with the answer */
	const auto taken = sv.shots.find(id);
	if (taken != sv.shots.end())
	{
		if (is_error == 0) result.content = chimera::mcpShotsContent(answer, taken->second.facts, taken->second.pictures);
		sv.shots.erase(taken);
	}
	chimera::lostWrites(sv, s);
	chimera::addLostNote(sv, result);
	chimera::finish(sv, found->second, result);
	sv.withHost.erase(found);
}

int32_t ce_mcp_search_step(ce_session *s, int32_t step, const char *arguments_json, const char **answer_out)
{
	static thread_local chimera::ThreadString held;
	const chimera::McpResult result = chimera::mcpSearchFilter(s, arguments_json != nullptr ? arguments_json : "{}");
	/* the answer's text, not the content list it is wrapped in */
	cJSON *content = cJSON_Parse(result.content.c_str());
	const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
	const std::string said = cJSON_IsString(text) ? text->valuestring : "";
	cJSON_Delete(content);
	const int64_t frame = s != nullptr ? ce_session_frame(s) : -1;
	if (result.isError) *held = "Step " + std::to_string(step) + " (frame " + std::to_string(frame) + "): " + said;
	else
	{
		cJSON *row = cJSON_Parse(said.c_str());
		if (row == nullptr) row = cJSON_CreateObject();
		cJSON_AddNumberToObject(row, "frame", static_cast<double>(frame));
		*held = chimera::printJson(row);
		cJSON_Delete(row);
	}
	if (answer_out != nullptr) *answer_out = held->c_str();
	return result.isError ? 0 : 1;
}

int32_t ce_mcp_diff_memory(ce_session *s, int32_t phase, const char *arguments_json, const char **answer_out)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	chimera::onMachine(sv, s);
	const chimera::McpResult result = chimera::mcpDiffMemory(sv, s, phase, arguments_json != nullptr ? arguments_json : "{}");
	cJSON *content = cJSON_Parse(result.content.c_str());
	const cJSON *text = cJSON_GetObjectItemCaseSensitive(cJSON_GetArrayItem(content, 0), "text");
	*held = cJSON_IsString(text) ? text->valuestring : "";
	cJSON_Delete(content);
	if (answer_out != nullptr) *answer_out = held->c_str();
	return result.isError ? 0 : 1;
}

void ce_mcp_cores_folders(const char *folders_json)
{
	Service &sv = service();
	std::vector<std::string> folders;
	cJSON *list = folders_json != nullptr ? cJSON_Parse(folders_json) : nullptr;
	const cJSON *folder = nullptr;
	cJSON_ArrayForEach(folder, list)
		if (cJSON_IsString(folder)) folders.push_back(folder->valuestring);
	cJSON_Delete(list);
	std::lock_guard<std::mutex> hold(sv.lock);
	sv.coresFolders = std::move(folders);
}

int32_t ce_mcp_capture(ce_session *s, uint64_t id, int64_t frame, const char **error_out)
{
	static thread_local chimera::ThreadString error;   /* never destroyed: see thread_string.hpp */
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	const auto found = sv.withHost.find(id);
	*error = "";
	if (found == sv.withHost.end() || found->second.tool != "capture_series") *error = "No capture_series is waiting for a picture.";
	else if (s == nullptr) *error = "No project is open in Chimera.";
	if (!error->empty())
	{
		if (error_out != nullptr) *error_out = error->c_str();
		return 0;
	}
	chimera::ShotsTaken &taken = sv.shots[id];
	if (taken.facts.empty() && !chimera::mcpReadShotPlan(found->second.prepared, taken.plan)) *error = "The pictures' plan cannot be read.";
	std::string facts, picture, why;
	if (error->empty() && !chimera::mcpTakeShot(s, taken.plan, taken.facts.size() + 1, frame, facts, picture, why)) *error = why;
	if (!error->empty())
	{
		if (error_out != nullptr) *error_out = error->c_str();
		return 0;
	}
	taken.facts.push_back(std::move(facts));
	taken.pictures.push_back(std::move(picture));
	return 1;
}

int32_t ce_mcp_condition_set(ce_session *s, const char *arguments_json, const char **error_out)
{
	static thread_local chimera::ThreadString error;   /* never destroyed: see thread_string.hpp */
	Service &sv = service();
	chimera::onMachine(sv, s);
	sv.conditionSet = false;
	auto refuse = [&](const std::string &why)
	{
		*error = why;
		if (error_out != nullptr) *error_out = error->c_str();
		return 0;
	};
	if (s == nullptr) return refuse(chimera::kNoMachine);
	cJSON *arguments = cJSON_Parse(arguments_json != nullptr ? arguments_json : "{}");
	chimera::Source source;
	std::string why;
	const cJSON *opItem = cJSON_GetObjectItemCaseSensitive(arguments, "op");
	const std::string opName = cJSON_IsString(opItem) ? opItem->valuestring : "";
	const int op = opName == "equal" ? 0 : opName == "greater" ? 1 : opName == "greater_or_equal" ? 2
		: opName == "less" ? 3 : opName == "less_or_equal" ? 4 : opName == "not_equal" ? 5 : opName == "changed" ? 6 : -1;
	const cJSON *valueItem = cJSON_GetObjectItemCaseSensitive(arguments, "value");
	std::string text;
	bool isNumber = false;
	double number = 0;
	int64_t length = 0;
	if (!cJSON_IsObject(arguments) || !chimera::parseSource(arguments, source, why)) why = why.empty() ? "The arguments are not a JSON object." : why;
	else if (op < 0) why = "'op' must be one of equal, not_equal, greater, greater_or_equal, less, less_or_equal, changed.";
	else if (!chimera::integerArgument(arguments, "length", 1, 65536, 0, length)) why = "'length' must be between 1 and 65536 bytes.";
	else if (length > 0 && (op != 6 || source.isProperty)) why = "'length' watches a range of memory for 'changed' only.";
	else if (op != 6 && !cJSON_IsNumber(valueItem)) why = "'value' must be a number, for every op but 'changed'.";
	else if (source.length = length; !chimera::readSource(s, source, text, isNumber, number, why)) why = "The value cannot be read: " + why + ".";
	else if (op != 6 && !isNumber) why = "'" + source.name + "' is not a number, so it can only be watched for 'changed'.";
	const double value = cJSON_IsNumber(valueItem) ? valueItem->valuedouble : 0;
	cJSON_Delete(arguments);
	if (!why.empty()) return refuse(why);
	sv.conditionSource = source;
	sv.conditionOp = op;
	sv.conditionValue = value;
	sv.conditionStart = text;
	sv.conditionSet = true;
	return 1;
}

int32_t ce_mcp_condition_met(ce_session *s)
{
	Service &sv = service();
	chimera::onMachine(sv, s);
	if (!sv.conditionSet || s == nullptr) return -1;
	std::string text, why;
	bool isNumber = false;
	double number = 0;
	if (!chimera::readSource(s, sv.conditionSource, text, isNumber, number, why)) return -1;
	switch (sv.conditionOp)
	{
		case 0: return number == sv.conditionValue ? 1 : 0;
		case 1: return number > sv.conditionValue ? 1 : 0;
		case 2: return number >= sv.conditionValue ? 1 : 0;
		case 3: return number < sv.conditionValue ? 1 : 0;
		case 4: return number <= sv.conditionValue ? 1 : 0;
		case 5: return number != sv.conditionValue ? 1 : 0;
		default: return text != sv.conditionStart ? 1 : 0;
	}
}

uint64_t ce_mcp_log_total(void)
{
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	return sv.logTotal;
}

const char *ce_mcp_log_line(uint64_t index)
{
	static thread_local chimera::ThreadString held;
	Service &sv = service();
	std::lock_guard<std::mutex> hold(sv.lock);
	held->clear();
	const uint64_t first = sv.logTotal - sv.log.size();
	if (index >= first && index < sv.logTotal) *held = sv.log[static_cast<size_t>(index - first)];
	return held->c_str();
}

} // extern "C"
