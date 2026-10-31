/* session.cpp - a running waterboxed machine.
 *
 * Package + config + rom + settings + firmware in, one frame at a time out.
 * Behavioural notes are kept where they matter - especially the ones paid
 * for in blood (no activate/deactivate bracket around save/load state; the
 * frozen post-Init image as the savestate baseline).
 */

#include "chimera/engine.h"
#include <cstdlib>

#include "movie_entry.hpp"
#include "control_names.hpp"
#include "file_io.hpp"
#include "zstd_dyn.hpp"
#include "host_dyn.hpp"
#include "core_log.hpp"
#include "progress.hpp"
#include "thread_string.hpp"
#include "state_history.hpp"
#include "kept_pictures.hpp"
#include "resampler.hpp"
#include "project_check.hpp"
#include "state_format.hpp"
#include "game_properties.hpp"
#include "mcp.hpp"
#include "language.hpp"

#include "../../extern/cjson/cJSON.h"

using chimera::lang::tr;

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

thread_local chimera::ThreadString g_openError;   /* never destroyed: see thread_string.hpp */

struct ByteStream
{
	const uint8_t *data;
	uint64_t len;
	uint64_t pos = 0;
};

extern "C" intptr_t streamRead(uintptr_t userdata, void *dst, uintptr_t size)
{
	auto *s = reinterpret_cast<ByteStream *>(userdata);
	uint64_t want = size;
	uint64_t avail = s->len - s->pos;
	uint64_t n = want < avail ? want : avail;
	if (n != 0)
	{
		std::memcpy(dst, s->data + s->pos, static_cast<size_t>(n));
		s->pos += n;
	}
	return static_cast<intptr_t>(n);
}

/* A stream is read whole by the create or mount it was handed to, and never
 * again, so it points at bytes the open does not own (a package entry, the
 * caller's buffer) and is emptied as soon as the host has its copy: nothing
 * can read those bytes through it after the open returns. */
void doneWith(ByteStream &stream)
{
	stream.data = nullptr;
	stream.len = 0;
	stream.pos = 0;
}

/* Running out of memory is a failed write to miniBox (a negative answer), not
 * an exception through its C frames (engine analysis, 2026-10-18). */
struct VectorSink { std::vector<uint8_t> *v; bool outOfMemory = false; };
extern "C" int32_t vectorWrite(uintptr_t userdata, const void *src, uintptr_t size)
{
	auto *s = reinterpret_cast<VectorSink *>(userdata);
	const auto *p = static_cast<const uint8_t *>(src);
	try
	{
		s->v->insert(s->v->end(), p, p + size);
	}
	catch (const std::bad_alloc &)
	{
		s->outOfMemory = true;
		return -1;
	}
	return 0;
}

/* the slice of waterbox.config the machine itself needs */
struct SessionConfig
{
	std::string coreName, systemId, romFile;
	/* which of the package's machines this is, -1 for a package that is one */
	int32_t machineIndex = -1;
	uint64_t layoutMiB[5] = { 0, 0, 0, 0, 0 };
	int32_t width = 0, height = 0, virtualWidth = 0, virtualHeight = 0;
	/* whether the package or its machine gave a virtual size: without one the pixels are square */
	bool virtualDeclared = false;
	int32_t vsyncNum = 0, vsyncDen = 0;
	std::string getBgra, getAudio;
	int32_t samplesPerFrame = 0, channels = 1;
	/* the rate the core mixes at: the package's audio.rate, or its machine's audioRate */
	int32_t audioRate = 44100;
	std::string inputName, inputWasRead;
	std::vector<std::string> buttons;
	std::vector<chimera::EntryAxis> axes;
	/* what the package calls its controls and its system (control_names.hpp);
	 * `mnemonics` is one character a button, in declaration order */
	chimera::ControlNames names;
	std::string mnemonics;
	std::string systemName;
	bool deterministic = false;
	bool drawEveryFrame = false; // video.drawEveryFrame - see ce_session_draw_every_frame
	/* video.rebuildOnStateLoad - whether a SAME-SESSION load should look like a
	 * new context to this core's renderer (issue #43). Absent means true, which
	 * is what every core did before the question existed. */
	bool rebuildOnStateLoad = true;
	std::string defaultsJson; // JSON object: every declared setting at its default
	std::string settingsJson; // the effective settings, serialized for the guest
};

/* effective settings = the declared defaults overlaid with the overrides -
 * shared by open and by a live re-apply */
bool composeSettings(const std::string &defaultsJson, const char *overrides, std::string &out, std::string &error);

const char *strOf(const cJSON *obj, const char *key, const char *fallback = "")
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsString(item) ? item->valuestring : fallback;
}

int32_t intOf(const cJSON *obj, const char *key, int32_t fallback = 0)
{
	const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
	return cJSON_IsNumber(item) ? item->valueint : fallback;
}

/* WHICH MACHINE this session is, for a package that is several (one core.wbx
 * that is a Mega Drive, a Master System, a Game Gear and an SG-1000). The
 * choice is a setting like any other, so it is already in the overrides or in
 * the package's own defaults; what it selects is the machine's system id,
 * controller and picture. A package that is one machine has no machines array
 * and this answers nullptr. */
const cJSON *chooseMachine(const cJSON *root, const char *overrides)
{
	const cJSON *machines = cJSON_GetObjectItemCaseSensitive(root, "machines");
	if (!cJSON_IsArray(machines) || machines->child == nullptr) return nullptr;

	const char *settingName = strOf(root, "machineSetting", nullptr);
	std::string chosen;
	if (settingName != nullptr)
	{
		/* the package's default first, then whatever the caller pinned */
		const cJSON *decls = cJSON_GetObjectItemCaseSensitive(root, "settings");
		const cJSON *decl = nullptr;
		cJSON_ArrayForEach(decl, decls)
		{
			if (std::string(strOf(decl, "name")) != settingName) continue;
			chosen = strOf(decl, "default");
			break;
		}
		cJSON *over = overrides != nullptr && overrides[0] != '\0' ? cJSON_Parse(overrides) : nullptr;
		const cJSON *pinned = cJSON_GetObjectItemCaseSensitive(over, settingName);
		if (cJSON_IsString(pinned)) chosen = pinned->valuestring;
		cJSON_Delete(over);
	}

	const cJSON *machine = nullptr;
	cJSON_ArrayForEach(machine, machines)
	{
		const cJSON *when = cJSON_GetObjectItemCaseSensitive(machine, "when");
		const cJSON *value = nullptr;
		cJSON_ArrayForEach(value, when)
		{
			if (cJSON_IsString(value) && chosen == value->valuestring) return machine;
		}
		if (!cJSON_IsArray(when) && std::string(strOf(machine, "id")) == chosen) return machine;
	}
	/* a value naming no machine is the first one: a session always has a machine */
	return machines->child;
}

const char *const kConfigNotJson = "engine.config_not_json";

/* " (<package>: N bytes, starting with ...)": where the declarations were
 * read from, how much there was, and its first bytes - text as text, anything
 * else as \xNN - so a report of this error says what the file actually is. */
std::string describeUnreadable(const char *packagePath, const uint8_t *bytes, uint64_t len)
{
	std::string out = " (";
	out += packagePath != nullptr ? packagePath : "?";
	out += ": " + tr("engine.bytes", static_cast<int64_t>(len));
	if (bytes != nullptr && len != 0)
	{
		out += ", " + tr("engine.starting_with") + " ";
		const uint64_t shown = len < 16 ? len : 16;
		for (uint64_t i = 0; i < shown; i++)
		{
			const uint8_t b = bytes[i];
			if (b >= 0x20 && b < 0x7f && b != '\\')
			{
				out += static_cast<char>(b);
			}
			else
			{
				char hex[8];
				std::snprintf(hex, sizeof hex, "\\x%02x", static_cast<unsigned>(b));
				out += hex;
			}
		}
	}
	out += ")";
	return out;
}

bool parseConfig(const char *json, uint64_t len, const char *overrides, SessionConfig &cfg, std::string &error)
{
	cJSON *root = cJSON_ParseWithLength(json, static_cast<size_t>(len));
	if (root == nullptr || !cJSON_IsObject(root))
	{
		cJSON_Delete(root);
		error = tr(kConfigNotJson);
		return false;
	}
	const cJSON *machine = chooseMachine(root, overrides);
	cfg.coreName = strOf(root, "coreName", "Waterbox");
	cfg.systemId = machine != nullptr ? strOf(machine, "id") : strOf(root, "systemId");
	cfg.machineIndex = -1;
	if (machine != nullptr)
	{
		int32_t i = 0;
		const cJSON *m = nullptr;
		cJSON_ArrayForEach(m, cJSON_GetObjectItemCaseSensitive(root, "machines"))
		{
			if (m == machine) cfg.machineIndex = i;
			i++;
		}
	}
	cfg.romFile = strOf(root, "romFile", "rom");
	cfg.deterministic = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "deterministic"));

	const cJSON *layout = cJSON_GetObjectItemCaseSensitive(root, "memoryLayoutMiB");
	if (!cJSON_IsArray(layout) || cJSON_GetArraySize(layout) != 5)
	{
		cJSON_Delete(root);
		error = tr("engine.config_no_layout");
		return false;
	}
	for (int i = 0; i < 5; i++)
	{
		cfg.layoutMiB[i] = static_cast<uint64_t>(cJSON_GetArrayItem(layout, i)->valuedouble);
	}

	const cJSON *video = cJSON_GetObjectItemCaseSensitive(root, "video");
	const cJSON *audio = cJSON_GetObjectItemCaseSensitive(root, "audio");
	/* the controller is the machine's, and the picture may be too; a machine
	 * that declares none shares the package's (dolphin's GameCube and Wii
	 * are one pad) */
	const cJSON *input = machine != nullptr
		? cJSON_GetObjectItemCaseSensitive(machine, "input")
		: nullptr;
	if (!cJSON_IsObject(input))
	{
		input = cJSON_GetObjectItemCaseSensitive(root, "input");
	}
	if (!cJSON_IsObject(video) || !cJSON_IsObject(audio) || !cJSON_IsObject(input))
	{
		cJSON_Delete(root);
		error = tr("engine.config_no_video");
		return false;
	}
	cfg.width = intOf(video, "width");
	cfg.height = intOf(video, "height");
	cfg.virtualWidth = intOf(video, "virtualWidth", cfg.width);
	cfg.virtualHeight = intOf(video, "virtualHeight", cfg.height);
	cfg.virtualDeclared = cJSON_GetObjectItemCaseSensitive(video, "virtualWidth") != nullptr;
	if (machine != nullptr)
	{
		if (cJSON_GetObjectItemCaseSensitive(machine, "virtualWidth") != nullptr) cfg.virtualDeclared = true;
		cfg.virtualWidth = intOf(machine, "virtualWidth", cfg.virtualWidth);
		cfg.virtualHeight = intOf(machine, "virtualHeight", cfg.virtualHeight);
	}
	cfg.vsyncNum = intOf(video, "vsyncNumerator");
	cfg.vsyncDen = intOf(video, "vsyncDenominator");
	/* a package that says nothing (or nothing usable) runs at 60 frames a
	 * second, as the frontend's VideoConfig has always said; 0/0 was a frame
	 * rate of NaN, and a frontend that divides by it stops */
	if (cfg.vsyncNum <= 0 || cfg.vsyncDen <= 0)
	{
		cfg.vsyncNum = 60;
		cfg.vsyncDen = 1;
	}
	cfg.getBgra = strOf(video, "getBgra", "GetVideoBgra");
	cfg.drawEveryFrame = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(video, "drawEveryFrame"));
	/* ABSENT MEANS TRUE here, unlike the flags above: a core that says nothing
	 * must behave exactly as it did before this declaration existed, so only a
	 * package that explicitly declines is changed. */
	{
		const cJSON *rebuild = cJSON_GetObjectItemCaseSensitive(video, "rebuildOnStateLoad");
		cfg.rebuildOnStateLoad = rebuild == nullptr || cJSON_IsTrue(rebuild);
	}
	cfg.samplesPerFrame = intOf(audio, "samplesPerFrame");
	cfg.channels = intOf(audio, "channels", 1);
	cfg.audioRate = intOf(audio, "rate", 44100);
	if (machine != nullptr) cfg.audioRate = intOf(machine, "audioRate", cfg.audioRate);
	/* The sizes a package declares size buffers, so they are checked first: a
	 * negative width became a buffer of 2^64 bytes and ended Chimera (engine
	 * analysis, 2026-10-18). The largest any core declares today is 6144 x
	 * 4608 and 65536 samples a frame. */
	{
		auto outside = [&](const std::string &what, int64_t value, int64_t least, int64_t most)
		{
			if (value >= least && value <= most) return false;
			error = tr("engine.config_out_of_range", {what, std::to_string(value), std::to_string(least), std::to_string(most)});
			return true;
		};
		if (outside("video.width", cfg.width, 1, 16384) || outside("video.height", cfg.height, 1, 16384)
			|| outside(tr("engine.virtual_width"), cfg.virtualWidth, 1, 16384) || outside(tr("engine.virtual_height"), cfg.virtualHeight, 1, 16384)
			|| outside("audio.samplesPerFrame", cfg.samplesPerFrame, 0, 1 << 20) || outside("audio.channels", cfg.channels, 1, 2)
			|| outside(tr("engine.sound_rate"), cfg.audioRate, 1000, 384000))
		{
			cJSON_Delete(root);
			return false;
		}
	}
	cfg.getAudio = strOf(audio, "get", "GetAudio");
	cfg.inputName = strOf(input, "name", "Waterbox Controller");
	const cJSON *buttons = cJSON_GetObjectItemCaseSensitive(input, "buttons");
	const cJSON *item = nullptr;
	if (cJSON_IsArray(buttons))
	{
		cJSON_ArrayForEach(item, buttons)
		{
			if (cJSON_IsString(item)) cfg.buttons.emplace_back(item->valuestring);
		}
	}
	const cJSON *axes = cJSON_GetObjectItemCaseSensitive(input, "axes");
	if (cJSON_IsArray(axes))
	{
		cJSON_ArrayForEach(item, axes)
		{
			chimera::EntryAxis axis;
			axis.name = strOf(item, "name");
			axis.min = intOf(item, "min");
			axis.max = intOf(item, "max");
			axis.neutral = intOf(item, "neutral");
			const char *header = strOf(item, "header");
			if (header[0] != '\0') cfg.names.axisHeaders[axis.name] = header;
			cfg.axes.push_back(std::move(axis));
		}
	}
	/* The letters the package gives its buttons. One that cannot be written
	 * (longer than a character, a '.', a '|') is not taken: the rule answers
	 * for that button, which is wrong in a way a person sees at once and no
	 * movie is harmed by. */
	const cJSON *mnemonics = cJSON_GetObjectItemCaseSensitive(input, "mnemonics");
	if (cJSON_IsObject(mnemonics))
	{
		cJSON_ArrayForEach(item, mnemonics)
		{
			if (item->string != nullptr && cJSON_IsString(item) && item->valuestring[0] != '\0'
				&& item->valuestring[1] == '\0' && chimera::usableMnemonic(item->valuestring[0]))
			{
				cfg.names.mnemonics[item->string] = item->valuestring[0];
			}
		}
	}
	for (const auto &b : cfg.buttons) cfg.mnemonics.push_back(cfg.names.mnemonicOf(b));
	/* What heads a button's column where a letter is not enough. One that is
	 * not a header (empty, long, not ASCII) is not taken, and the column is
	 * headed by the letter as it always was. */
	const cJSON *headers = cJSON_GetObjectItemCaseSensitive(input, "headers");
	if (cJSON_IsObject(headers))
	{
		cJSON_ArrayForEach(item, headers)
		{
			if (item->string != nullptr && cJSON_IsString(item)
				&& chimera::usableButtonHeader(item->valuestring))
			{
				cfg.names.buttonHeaders[item->string] = item->valuestring;
			}
		}
	}
	/* whose each control is: the package's lists, one a player, the rest the
	 * console's; a list past the most players a movie can name is not taken */
	const cJSON *players = cJSON_GetObjectItemCaseSensitive(input, "players");
	if (cJSON_IsArray(players))
	{
		cfg.names.playersDeclared = true;
		int32_t player = 0;
		cJSON_ArrayForEach(item, players)
		{
			if (++player > chimera::kMostPlayers) break;
			const cJSON *control = nullptr;
			if (cJSON_IsArray(item))
				cJSON_ArrayForEach(control, item)
					if (cJSON_IsString(control)) cfg.names.players[control->valuestring] = player;
		}
	}
	/* what to call the system in front of a person: the package's word for
	 * this id, or the id */
	cfg.systemName = cfg.systemId;
	const cJSON *systemNames = cJSON_GetObjectItemCaseSensitive(root, "systemNames");
	if (cJSON_IsObject(systemNames))
	{
		const char *named = strOf(systemNames, cfg.systemId.c_str());
		if (named[0] != '\0') cfg.systemName = named;
	}
	const cJSON *lag = cJSON_GetObjectItemCaseSensitive(root, "lag");
	if (cJSON_IsObject(lag)) cfg.inputWasRead = strOf(lag, "inputWasRead");

	/* every declared setting at its default, kept for open AND for live
	 * re-applies */
	cJSON *defaults = cJSON_CreateObject();
	const cJSON *decls = cJSON_GetObjectItemCaseSensitive(root, "settings");
	if (cJSON_IsArray(decls))
	{
		cJSON_ArrayForEach(item, decls)
		{
			const char *name = strOf(item, "name", nullptr);
			const cJSON *fallback = cJSON_GetObjectItemCaseSensitive(item, "default");
			if (name != nullptr && fallback != nullptr)
			{
				cJSON_AddItemToObject(defaults, name, cJSON_Duplicate(fallback, 1));
			}
		}
	}
	char *printed = cJSON_PrintUnformatted(defaults);
	cfg.defaultsJson = printed != nullptr ? printed : "{}";
	if (printed != nullptr) cJSON_free(printed);
	cJSON_Delete(defaults);
	cJSON_Delete(root);
	return composeSettings(cfg.defaultsJson, overrides, cfg.settingsJson, error);
}

bool composeSettings(const std::string &defaultsJson, const char *overrides, std::string &out, std::string &error)
{
	cJSON *effective = cJSON_Parse(defaultsJson.c_str());
	if (effective == nullptr) effective = cJSON_CreateObject();
	if (overrides != nullptr && overrides[0] != '\0')
	{
		cJSON *over = cJSON_Parse(overrides);
		if (over == nullptr || !cJSON_IsObject(over))
		{
			cJSON_Delete(over);
			cJSON_Delete(effective);
			error = tr("engine.overrides_not_json");
			return false;
		}
		const cJSON *item = nullptr;
		cJSON_ArrayForEach(item, over)
		{
			cJSON *dup = cJSON_Duplicate(item, 1);
			if (cJSON_GetObjectItemCaseSensitive(effective, item->string) != nullptr)
			{
				cJSON_ReplaceItemInObjectCaseSensitive(effective, item->string, dup);
			}
			else
			{
				cJSON_AddItemToObject(effective, item->string, dup);
			}
		}
		cJSON_Delete(over);
	}
	char *printed = cJSON_PrintUnformatted(effective);
	out = printed != nullptr ? printed : "{}";
	if (printed != nullptr) cJSON_free(printed);
	cJSON_Delete(effective);
	return true;
}

} // namespace

/* source/engine/source/gl_bridge.cpp - present in every build; a build without
 * CE_GL_BRIDGE answers that it has no context and nothing here happens. */
extern "C" int32_t ce_gl_start(char *error_out, int32_t error_len);
extern "C" void ce_gl_session_end(void);
extern "C" void ce_gl_guest_check(int (*can)(void *, uintptr_t, uintptr_t, int), void *ctx);
extern "C" uintptr_t ce_cache_dispatch(uintptr_t op, uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d, uintptr_t e);
extern "C" void ce_cache_announce(int32_t precompile);

/* Sessions that ask the core one question instead of starting it: open stops
 * before Init, calls the named export (no arguments, a NUL-terminated string
 * back) and keeps the answer. ce_project_suggest_settings asks SuggestSettings,
 * ce_project_import_movie asks ImportMovie. nullptr is an ordinary session. One per
 * thread: process-wide, a question asked on one thread while another thread
 * opened a session turned that session into a question, with no frame
 * export, and its first frame called a null pointer (engine analysis,
 * 2026-10-18). */
static thread_local const char *s_answerExport = nullptr;

/* precompile sessions: asked for before open, on the thread that opens, like
 * the question above */
static thread_local int32_t s_precompileIndex = 0, s_precompileCount = 0, s_precompileFirmware = 1;

extern "C" void ce_precompile_request(int32_t index, int32_t count, int32_t firmware_too)
{
	s_precompileIndex = index;
	s_precompileCount = count;
	s_precompileFirmware = firmware_too;
}
extern "C" const char *ce_gl_description(void);
extern "C" int32_t ce_gl_requested(void);
extern "C" void ce_gl_release(void);
/* The bridge cannot see a savestate go by, and a savestate is exactly when its
 * world and the guest's stop agreeing - see CHIMERA_GL_STATEAUDIT. */
extern "C" void ce_gl_audit_frame(int64_t frame);
extern "C" void ce_gl_state_loaded(int64_t to);
extern "C" void ce_gl_rebuild_on_state_load(int32_t on);
extern "C" uintptr_t ce_gl_dispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

/* The Vulkan bridge's host half is miniBox's own file (source/vk/vk-host.c),
 * compiled into the engine when the GPU bridge is: docs/gpu-bridge.md. */
#ifdef CE_VK_BRIDGE
#include "vk-host.h"
#endif

/* A core that never comes back from a call - a frame, or starting - froze
 * Chimera: nothing could stop it (engine analysis, 2026-10-18). A watchdog of
 * the session's, armed around the call, asks miniBox to stop it when it runs
 * past its time (wbx_stop_guest): it then ends as a crash does, with the reason
 * the session reports. 20 seconds for a frame and 2 minutes for starting (the
 * owner's numbers, 2026-10-19); CHIMERA_FRAME_TIMEOUT_MS and
 * CHIMERA_START_TIMEOUT_MS set others, 0 none. The first frame after starting
 * and after every state load has the 2 minutes too (owner, 2026-10-21, issue
 * 332): RPCS3 analyses and compiles a game's code in it, and a core rebuilds
 * what a load threw away there. A core that is working, not
 * stuck, shows it, and the time starts again from that (wbx_guest_activity):
 * it makes system calls of the kind a waiting or polling loop does not (RPCS3
 * compiling a game's code with LLVM spends 90 seconds in its first frames
 * here, and was stopped at 20 before this), or a call through a GPU bridge or
 * to the compile cache comes back (ce_guest_work, below). A stop asked for
 * before such work is dropped by miniBox: a driver call that took long is not
 * a stuck core. What is stopped is a core looping in its own code, or one
 * only waiting and polling. Not in a precompile session, whose frames are
 * long by design. */

/* The engine's own work for the guest: one more after each call through the GL
 * or Vulkan bridge or to the compile cache has been answered. miniBox adds it
 * into the guest's activity (wbx_count_work_from). Written by the thread
 * running the guest, read by others; only its moving matters. */
extern "C" { uint64_t ce_guest_work = 0; }
static inline void guestWorked()
{
	__atomic_store_n(&ce_guest_work, __atomic_load_n(&ce_guest_work, __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
}

/* The session whose frame this thread is running: a core calling back part
 * way through a frame (an input read, issue 344) is that session's. */
static thread_local ce_session *t_framing = nullptr;

#if defined(_WIN32) && defined(__GNUC__)
#define CE_GUEST_CALLBACK_ABI __attribute__((sysv_abi))
#else
#define CE_GUEST_CALLBACK_ABI
#endif

/* A core read its controls (its call through callback slot 3). The caller's
 * function runs now, inside the frame; the read counts as the core's work. */
static uintptr_t CE_GUEST_CALLBACK_ABI inputPollDispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);

#ifdef CE_VK_BRIDGE
/* The Vulkan bridge's dispatcher, each call counted as work once the driver
 * has answered it. */
static uintptr_t VKB_GUEST_ABI vkDispatchCounted(uintptr_t op, uintptr_t a, uintptr_t b,
                                                 uintptr_t c, uintptr_t d, uintptr_t e)
{
	const uintptr_t r = chimera_vk_host_dispatch(op, a, b, c, d, e);
	guestWorked();
	return r;
}
#endif

static int64_t nowMs()
{
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

/* "20 seconds", "1.5 seconds", "1 second": a limit as the user reads it,
 * with a point whatever the locale */
static std::string secondsText(int64_t ms)
{
	std::string text = std::to_string(ms / 1000);
	if (ms % 1000 != 0)
	{
		char frac[8];
		std::snprintf(frac, sizeof frac, "%03d", static_cast<int>(ms % 1000));
		std::string f = frac;
		while (!f.empty() && f.back() == '0') f.pop_back();
		text += "." + f;
	}
	return tr(ms == 1000 ? "engine.one_second" : "engine.seconds", {text});
}

static int64_t timeoutMs(const char *variable, int64_t fallback)
{
	const char *e = std::getenv(variable);
	return e != nullptr && e[0] != '\0' ? std::strtoll(e, nullptr, 10) : fallback;
}

static int64_t frameLimitMs()
{
	static const int64_t limit = timeoutMs("CHIMERA_FRAME_TIMEOUT_MS", 20000);
	return limit;
}

static int64_t startLimitMs()
{
	static const int64_t limit = timeoutMs("CHIMERA_START_TIMEOUT_MS", 120000);
	return limit;
}

struct GuestWatchdog
{
	const chimera::HostApi *host;
	void *obj;
	std::mutex lock;
	std::condition_variable wake;
	std::thread thread;
	bool armed = false, quit = false;
	int64_t armedAt = 0, limit = 0, activeAt = 0;
	uint64_t activity = 0;
	std::string why;
	std::thread::id caller;

	GuestWatchdog(const chimera::HostApi *h, void *o) : host(h), obj(o)
	{
		chimera::WbxReturn r{};
		if (host->wbx_count_work_from != nullptr) host->wbx_count_work_from(obj, &ce_guest_work, &r);
	}
	~GuestWatchdog()
	{
		{
			std::lock_guard<std::mutex> hold(lock);
			quit = true;
		}
		wake.notify_one();
		if (thread.joinable()) thread.join();
	}
	bool usable() const { return host->wbx_stop_guest != nullptr && host->wbx_stop_from_here != nullptr; }
	uint64_t readActivity() const
	{
		if (host->wbx_guest_activity == nullptr) return 0;
		chimera::WbxReturn r{};
		host->wbx_guest_activity(obj, &r);
		return r.data;
	}
	void arm(int64_t ms, std::string reason)
	{
		if (ms <= 0 || !usable()) return;
		/* the thread making the call is the one a stop goes to */
		if (caller != std::this_thread::get_id())
		{
			chimera::WbxReturn r{};
			host->wbx_stop_from_here(obj, &r);
			caller = std::this_thread::get_id();
		}
		{
			std::lock_guard<std::mutex> hold(lock);
			armed = true;
			armedAt = nowMs();
			activeAt = armedAt;
			activity = readActivity();
			limit = ms;
			why = std::move(reason);
			if (!thread.joinable()) thread = std::thread([this] { run(); });
		}
		wake.notify_one();
	}
	void disarm()
	{
		if (!usable()) return;
		{
			std::lock_guard<std::mutex> hold(lock);
			if (!armed) return;
			armed = false;
		}
		chimera::WbxReturn r{};
		if (host->wbx_stop_cancel != nullptr) host->wbx_stop_cancel(obj, &r);
	}
	void run()
	{
		std::unique_lock<std::mutex> hold(lock);
		while (!quit)
		{
			if (!armed)
			{
				wake.wait(hold);
				continue;
			}
			const uint64_t now = readActivity();
			if (now != activity)
			{
				activity = now;
				activeAt = nowMs();
			}
			const int64_t from = std::max(armedAt, activeAt);
			const int64_t left = from + limit - nowMs();
			if (left > 0)
			{
				wake.wait_for(hold, std::chrono::milliseconds(std::min<int64_t>(left, 1000)));
				continue;
			}
			/* past its time: asked until the call comes back (a core in host
			 * code - a system call, a driver - is asked again later) */
			const std::string reason = why;
			hold.unlock();
			chimera::WbxReturn r{};
			host->wbx_stop_guest(obj, reason.c_str(), &r);
			hold.lock();
			wake.wait_for(hold, std::chrono::milliseconds(250));
		}
	}
};

struct ce_session
{
	SessionConfig cfg;
	const chimera::HostApi *host = nullptr;
	void *obj = nullptr;
	bool active = false;
	/* a GPU outside the sandbox drew this session's pictures */
	bool gpuDrew = false;
	/* the core took the Vulkan bridge: a load is a new device to it */
	bool vulkanBridge = false;
	bool glBridge = false;   /* this session started the GL bridge: it ends it (ce_gl_session_end) */
	/* The core reports each read of its controls during a frame (issue 344):
	 * its SetInputPollCallback export took the engine's callback. fn is the
	 * caller's, called at each read. */
	bool inputPollsReported = false;
	ce_input_poll_fn inputPollFn = nullptr;
	void *inputPollCtx = nullptr;
	/* The pictures of frames already drawn, shown again when the machine is put
	 * back on one (kept_pictures.hpp; chimera#190). On by itself for a session
	 * whose pictures a GPU draws and whose greenzone is on; a caller may say
	 * otherwise (ce_session_greenzone_pictures).
	 *   machineFrame  the frame the machine is on, as far as the session can
	 *                 tell: counted here, and corrected by whoever counts it
	 *                 for real (a capture, a restore); -1 when it cannot tell.
	 *   sinceLoad     frames SHOWN since a state was last loaded - not frames
	 *                 run: a renderer rebuilds what a load threw away by
	 *                 drawing, and a core told not to draw through a seek is
	 *                 as wrong on the first frame it draws afterwards as it
	 *                 would have been at once. A picture among the first
	 *                 pictureSettle shown is never KEPT: a wrong picture kept
	 *                 would be shown for good.
	 *   lastKept      the frame the last advance kept a picture for, so that a
	 *                 count that turns out wrong can take it back. */
	CeKeptPictures pictures;
	bool picturesChosen = false;
	int64_t machineFrame = 0;
	int64_t sinceLoad = int64_t{ 1 } << 40;
	int32_t pictureSettle = 3;
	int32_t tracedSinceLoad = 8;   /* CHIMERA_PICTURE_TRACE: frames reported since the last load */
	int64_t lastKept = -1;
	void afterFrame(bool rendered, int64_t known);
	void afterLoad(int64_t landedOn);
	/* the compile cache and precompile sessions (optional exports) */
	bool precompile = false;
	/* what the core answered, in a session that only asks (s_answerExport) */
	std::string suggestion;
	/* the loaded game's own settings, declared by the core (GetGameSettings) */
	std::string gameSettings;
	/* the core package's language tables: the texts the core returns are keys
	 * of them (docs/translations.md) */
	chimera::lang::PackageTexts texts;
	mutable std::string gameSettingsText;
	uintptr_t fnCacheStored = 0, fnCacheFetched = 0;
	uintptr_t fnPrecompileDone = 0, fnPrecompileDoneCount = 0, fnPrecompileTotal = 0;

	// the mounted byte sources must outlive the mounts
	std::vector<uint8_t> romNameBytes;
	std::string settingsBytes;
	std::vector<ByteStream> streams; // stable addresses: reserved up front

	// guest entry points (already bridged to our convention)
	void (*frameAdvance)(uint64_t) = nullptr;
	/* frameAdvance under the watchdog (GuestWatchdog) */
	void runFrame(uint64_t pad);
	/* frozen addresses (ce_session_freeze): written before every frame */
	struct Freeze
	{
		bool enabled = true;
		int32_t domain = -1;
		int64_t address = 0;
		int32_t size = 1;
		bool bigEndian = false;
		uint64_t value = 0;
		int32_t compareKind = 0;
		uint64_t compare = 0;
		/* a game property's element instead, set from text */
		int32_t property = -1;
		uint32_t element = 0;
		std::string text;
	};
	std::vector<Freeze> freezes;
	void applyFreezes();
	std::unique_ptr<struct GuestWatchdog> watchdog;
	/* the next frame is the first after starting or a state load: it has the
	 * start's time (GuestWatchdog) */
	bool firstFrame = true;
	uintptr_t (*getVideoBgra)() = nullptr;
	uintptr_t (*getAudio)() = nullptr;
	int32_t (*inputWasRead)() = nullptr;
	int32_t (*getAudioSampleCount)() = nullptr;
	void (*setAxis)(int32_t, int32_t) = nullptr;
	// wide input: buttons past the packed mask's 64 (a DOS keyboard). The
	// caller's ce_session_set_button values persist here like axes do;
	// deltas cross into the guest through its SetButton export.
	void (*setButton)(int32_t, int32_t) = nullptr;
	std::vector<uint8_t> btnState;     // what ce_session_set_button set
	std::vector<uint8_t> btnSent;      // what the guest last received
	std::vector<uint8_t> btnEffective; // scratch: state OR packed mask
	std::vector<uint8_t> movieButtons; // scratch: a parsed entry's buttons
	/* WHICH DECLARED CONTROLS THIS MACHINE ACTUALLY HAS.
	 *
	 * waterbox.config declares the union of every peripheral a package's ports
	 * can hold, because it is a static declaration and cannot know what a
	 * project plugged in. The running core can: it read the port settings
	 * itself and built the machine from them, so it is the one place the answer
	 * is not a duplicate of somebody else's logic.
	 *
	 * A core that exports neither answer has every declared control, which is
	 * what every core did before this existed. */
	/* Drive lights: one per medium the machine actually has, lit on any frame
	 * that drive was read or written. Optional, and all three or none. */
	int32_t (*driveCount)() = nullptr;
	uintptr_t (*driveName)(int32_t) = nullptr;
	int32_t (*driveLight)(int32_t) = nullptr;
	std::vector<std::string> driveNames;
	/* what each drive can hold (engine.h, "What is in a drive"): the names are
	 * copied at load, the two indices are asked of the running machine */
	int32_t (*driveMediaSelected)(int32_t) = nullptr;
	int32_t (*driveMediaInserted)(int32_t) = nullptr;
	std::vector<std::vector<std::string>> driveMedia;
	/* what the last state kept in a file weighed, as the machine and as the file */
	uint64_t stateFileRawBytes = 0, stateFileStoredBytes = 0;

	/* Optional, both or neither: the display aspect of what the machine shows
	 * NOW, as x:y (4:3; 3:4 for an arcade game whose monitor stood on its
	 * side). Wins over the declared virtual size. */
	int32_t (*aspectX)() = nullptr;
	int32_t (*aspectY)() = nullptr;

	int32_t (*isButtonActive)(int32_t) = nullptr;
	int32_t (*isAxisActive)(int32_t) = nullptr;
	/* what THIS GAME calls a control (see buttonLabels) */
	const char *(*getButtonLabel)(int32_t) = nullptr;
	const char *(*getAxisLabel)(int32_t) = nullptr;
	/* Optional: told after every load of the machine - a savestate, a branch
	 * file, a greenzone restore. For a core that keeps something DERIVED
	 * outside its states (xemu's translated-code cache lives in invisible
	 * memory) and has to throw it away when the memory it was derived from is
	 * replaced. Called with the machine stopped, before it runs again. */
	void (*stateLoaded)(void) = nullptr;
	/* Optional, the other end: told BEFORE every state the engine takes of the
	 * machine - a greenzone capture, a savestate, a branch's state file. For a
	 * core part of whose machine is somewhere a state does not reach until it
	 * is brought back: xemu's GPU draws into surfaces that live in the driver,
	 * and writes one into the console's RAM only when the processor reads it,
	 * so a state taken without this holds the RAM of a machine whose screen is
	 * elsewhere, and a load loses what was drawn (chimera#190). Called with the
	 * machine stopped between frames; what it writes is in the state. */
	void (*stateSavingExport)(void) = nullptr;
	bool stateSaving();
	void vulkanStateLoaded();
	std::vector<uint8_t> buttonActive;
	std::vector<uint8_t> axisActive;
	/* A control's supplemental name for this game, by its index in the
	 * declaration; "" where the core has none. The declared name stays the
	 * control's identity - a movie, a binding and a column are keyed by it -
	 * and this is only what a person is shown beside it. Copied out once,
	 * after Init, with the activity above. */
	std::vector<std::string> buttonLabels;
	std::vector<std::string> axisLabels;
	void buildControlActivity();
	int32_t (*mdCount)() = nullptr;
	uintptr_t (*mdName)(int32_t) = nullptr;
	uintptr_t (*mdPtr)(int32_t) = nullptr;
	int64_t (*mdSize)(int32_t) = nullptr;
	int32_t (*mdWritable)(int32_t) = nullptr;
	int32_t (*mdHidden)(int32_t) = nullptr; /* optional: GetMemoryDomainHidden */
	CeGameProperties properties;    // a game core's (docs/game-cores.md); empty for an emulator
	uintptr_t (*gameProperties)() = nullptr;             // GetGameProperties: the table, again when it is dynamic
	uintptr_t (*gameProperty)(const char *) = nullptr;   // GetGameProperty: one entry by name (a dynamic table's)
	/* a dynamic table's property, looked for again by name before it is used */
	void placeProperty(int32_t index);
	CeGameProperties::Value propertyValue; // what the last ce_session_property_get lent out

	int32_t vsyncNum = 0, vsyncDen = 0;
	/* the core's own rate exports, asked again after every frame: a game
	 * core's step is as long as the game's logic makes it (docs/game-cores.md),
	 * and a machine changes its refresh with its video mode */
	int32_t (*getVsyncNum)() = nullptr;
	int32_t (*getVsyncDen)() = nullptr;
	void readRate()
	{
		if (getVsyncNum == nullptr || getVsyncDen == nullptr) return;
		const int32_t n = getVsyncNum(), d = getVsyncDen();
		if (n > 0 && d > 0)
		{
			vsyncNum = n;
			vsyncDen = d;
		}
	}
	// dynamic video size: a DOS machine changes modes; the guest reports the
	// live frame size (clamped to the config's buffer) through optional
	// exports, and the config's width/height stay the buffer's capacity
	/* CHIMERA_TRACE=<n>: every n frames, the machine's own numbers on stderr.
	 * The per-core diagnostic runners print a line of exactly this shape, so a
	 * machine that misbehaves under the frontend and behaves under the runner
	 * can be diffed on one machine instead of guessed at from two. Everything
	 * here is an optional export; a core that answers nothing prints dashes. */
	int32_t traceEvery = 0;
	int64_t traceFrame = 0, traceLag = 0, traceTtySeen = 0;
	int32_t (*traceThreads)() = nullptr;
	int32_t (*traceRunning)() = nullptr;
	uint64_t (*traceDigest)() = nullptr;
	uint64_t (*traceTimeNs)() = nullptr;
	uintptr_t (*traceTty)() = nullptr;
	int64_t (*traceTtySize)() = nullptr;
	void trace(int32_t lag, int32_t render);

	int32_t (*getVideoWidth)() = nullptr;
	int32_t (*getVideoHeight)() = nullptr;
	int32_t vidW = 0, vidH = 0;
	bool saidOversize = false; /* copyVideo: a picture larger than declared, said once */
	/* The picture, as large as the largest one the machine has drawn: the
	 * package's video size is the most a core can draw (DuckStation's 16x is
	 * 12288x9216, 450 MB), and a session that never draws that much does not
	 * hold room for it. Never smaller than vidW x vidH. */
	std::vector<uint32_t> videoBuf;
	void growVideo(size_t pixels)
	{
		if (videoBuf.size() < pixels) videoBuf.resize(pixels, 0);
	}
	/* room for any kept picture: none is larger than CeKeptPictures keeps */
	void roomForKept()
	{
		const size_t most = static_cast<size_t>(cfg.width) * static_cast<size_t>(cfg.height);
		growVideo(std::min(most, CeKeptPictures::MaxRawBytes / sizeof(uint32_t)));
	}
	std::vector<int16_t> audioBuf;
	int32_t sampleCount = 0;
	/* each frame's sound counted; the sound at 44100 Hz (ce_session_sound),
	 * made from the frame its count says, and whether the frames before it
	 * were the machine's last (not a seek or a state loaded away) */
	uint64_t audioSerial = 0, soundSerial = 0;
	bool soundGap = true;
	chimera::Resampler resampler;
	std::vector<int16_t> soundBuf;
	std::vector<uint8_t> stateBuf;
	std::string error;

	// ---- the optional guest ABI groups, probed once post-Init ----
	// surfaces
	uintptr_t (*renderSurface)(int32_t) = nullptr;
	std::vector<std::string> surfaceNames;
	std::vector<int32_t> surfaceWidths, surfaceHeights;
	std::vector<std::vector<uint32_t>> surfaceBufs;
	// registers
	int64_t (*regValue)(int32_t) = nullptr;
	void (*regSet)(int32_t, int64_t) = nullptr;
	int64_t (*executedCycles)() = nullptr;
	std::vector<std::string> regNames;
	std::vector<int32_t> regBits;
	// buses
	int32_t (*busPeek)(int32_t, int32_t) = nullptr;
	uintptr_t (*busRead)(int32_t, int64_t, int32_t) = nullptr; /* optional: ReadBus */
	uintptr_t (*busRanges)(int32_t) = nullptr; /* optional: GetBusRanges */
	void (*busPoke)(int32_t, int32_t, int32_t) = nullptr;
	std::vector<std::string> busNames;
	std::vector<int64_t> busSizes;
	std::vector<bool> busWritables;
	// savedata export (docs/save-data.md); a snapshot maps engine index ->
	// guest index because entries with unclean paths are dropped here
	int32_t (*sdCount)() = nullptr;
	uintptr_t (*sdName)(int32_t) = nullptr;
	int64_t (*sdSize)(int32_t) = nullptr;
	uintptr_t (*sdBuffer)(int32_t) = nullptr;
	/* The streaming half of the save-data channel, for a file too big to be
	 * addressable all at once - a DOS hard disk that does not live in guest
	 * memory. The guest serves a window into a scratch buffer of its own; the
	 * file's name and size are unchanged, so what comes out is the same file. */
	int64_t (*sdRead)(int32_t, int64_t, int64_t) = nullptr;
	uintptr_t (*sdScratch)() = nullptr;
	std::vector<std::string> sdNames;
	std::vector<int64_t> sdSizes;
	std::vector<int32_t> sdGuestIndex;
	// turbo: the core is told to stop drawing, and the machine must not
	// notice. -1 means "the guest has not been told anything yet", which is
	// also what a state load leaves behind (see ce_session_load_state).
	void (*setRendering)(int32_t) = nullptr;
	int32_t renderingSent = -1;
	/* When set, the core is never told to stop drawing - only the READBACK is
	 * skipped on a frame nobody looks at. A renderer whose picture lives on the
	 * far side of the bridge needs this: what it draws persists there, so a
	 * frame skipped during a seek is not deferred work but a picture that no
	 * later frame redraws. See docs/gpu-bridge.md. */
	int32_t drawAlways = 0;
	// trace
	void (*traceSetEnabled)(int32_t) = nullptr;
	int32_t (*traceLineCount)() = nullptr;
	uintptr_t (*traceBuffer)() = nullptr;
	int32_t (*traceOverflow)() = nullptr;
	int32_t (*traceUsedBytes)() = nullptr;
	void (*traceClear)() = nullptr;
	std::string traceHeader = tr("engine.instructions");
	bool traceDesired = false;
	std::vector<uint8_t> traceOut;

	/* which session this is in the process, never reused (an address can be:
	 * a new session may be allocated where a closed one was) */
	uint64_t serial = 0;

	// ---- the session's movie ----
	ce_movie_log *movie = nullptr;
	int32_t movieMode = 0; // 0 none, 1 play, 2 record, 3 finished
	int64_t frame = 0;
	std::string mnemonics; // one char per button, for generated entries
	chimera::EntryLayout layout; // the Bk2 entry order (see movie_entry.hpp)
	/* The movie's own columns, when its LogKey names them: lines are read and
	 * written by those names, as the window reads them (user-decided,
	 * 2026-10-15). A movie with no key goes by the machine's own order. */
	chimera::EntryLayout movieLayout;
	bool movieKeyed = false;
	const chimera::EntryLayout &entries() const { return movieKeyed ? movieLayout : layout; }
	void coverInKey(const uint8_t *states, const int32_t *axes);
	/* The movie edited from another thread (ce_session_movie_set): the lock
	 * every touch of `movie` takes, and the earliest frame edited since the
	 * machine last moved. */
	mutable std::mutex movieLock;
	int64_t editedFrom = -1;
	/* Rerecords (ce_session_rerecords): the count, whether edits count
	 * now, and whether one of the edits not yet taken counts (both under
	 * movieLock: they are decided when the edit is made). */
	uint64_t rerecords = 0;
	bool countingRerecords = true;
	bool countedEdit = false;
	/* a rerecord, when this drops a stored state: before it is dropped */
	void rerecordIfDropping(int64_t after);
	/* the machine's thread's: the earliest edited frame the machine is past */
	int64_t staleFrom = -1;
	void takeEdits();
	bool catchUp(int64_t to, int64_t render);
	bool movieEntry(int64_t at, std::vector<int32_t> &axes);
	void nameColumns();

	// ---- the state history (docs/state-manager.md) ----
	// Anchors and the deltas that walk forward from them, so a stored frame
	// costs what the frame changed rather than what the machine is.
	chimera::StateHistory history;

	void probeOptionalGroups();
	int32_t advanceCore(const uint8_t *buttons, int32_t render);
	void wantRendering(int32_t on);
	void copyVideo();
	const uint8_t *computeEffective(uint64_t mask);
	uint64_t sendButtons(const uint8_t *states);
	void greenzoneCapture();
	bool greenzoneRestore(int64_t to);
	void greenzoneLoaded(int64_t at);
	/* the half that must run BEFORE the advance: an epoch has to be marked
	 * while the machine is still where the delta will be measured from */
	void greenzoneBeforeAdvance() { history.beforeAdvance(); }

	bool activate(std::string &err)
	{
		if (active) return true;
		chimera::WbxReturn r{};
		host->wbx_activate_host(obj, &r);
		if (!r.ok()) { err = r.errorMessage; return false; }
		active = true;
		return true;
	}

	bool deactivate(std::string &err)
	{
		if (!active) return true;
		chimera::WbxReturn r{};
		host->wbx_deactivate_host(obj, &r);
		if (!r.ok()) { err = r.errorMessage; return false; }
		active = false;
		return true;
	}

	/* Whether the guest died in the call just made (miniBox: an abort, a halt,
	 * a fault, an exit). If so the session's error says why, and nothing more of
	 * this frame is asked of the machine - its calls would only return 0, and a
	 * null picture or audio buffer is not somewhere to copy from. A state load
	 * (a greenzone restore, a branch) brings the machine back. */
	std::string death;
	/* A greenzone restore that could put no machine back at all - not its
	 * anchor, nor any earlier one (2026-10-18). Whatever the core holds now is
	 * not a frame the movie can go on from, so it counts as dead until a
	 * state loads or a restore lands. */
	std::string lostMachine;

	bool guestDied()
	{
		if (!lostMachine.empty())
		{
			death = lostMachine;
			error = tr("engine.core_stopped", {lostMachine});
			return true;
		}
		if (host == nullptr || obj == nullptr || host->wbx_get_death == nullptr) return false;
		char why[512] = {};
		chimera::WbxReturn r{};
		host->wbx_get_death(obj, why, sizeof why, &r);
		if (!r.ok() || r.data == 0)
		{
			death.clear();
			return false;
		}
		death = why;
		error = tr("engine.core_stopped", {why});
		return true;
	}

	uintptr_t proc(const char *name, int argCount, bool required, std::string &err)
	{
		chimera::WbxReturn r{};
		host->wbx_get_proc_addr(obj, name, &r);
		if (!r.ok()) { err = r.errorMessage; return 0; }
		if (r.data == 0)
		{
			if (required) err = tr("engine.no_export", {cfg.coreName, name});
			return 0;
		}
		uintptr_t bridged = chimera::bridgeGuestCall(r.data, argCount);
		if (bridged == 0) err = tr("engine.no_bridge");
		return bridged;
	}
};

/* Asked once, after Init - which is the only moment it can be asked, because
 * before Init the core has not read its settings and after the first frame the
 * answer must not change: a controller that grew a button mid-movie is not a
 * machine anything can replay. */
void ce_session::buildControlActivity()
{
	buttonActive.assign(cfg.buttons.size(), 1);
	axisActive.assign(cfg.axes.size(), 1);
	if (isButtonActive != nullptr)
	{
		for (size_t i = 0; i < buttonActive.size(); i++)
			buttonActive[i] = isButtonActive(static_cast<int32_t>(i)) != 0 ? 1 : 0;
	}
	if (isAxisActive != nullptr)
	{
		for (size_t i = 0; i < axisActive.size(); i++)
			axisActive[i] = isAxisActive(static_cast<int32_t>(i)) != 0 ? 1 : 0;
	}
	/* what the game calls each control it has; a control it lacks has none */
	buttonLabels.assign(cfg.buttons.size(), std::string());
	axisLabels.assign(cfg.axes.size(), std::string());
	if (getButtonLabel != nullptr)
	{
		for (size_t i = 0; i < buttonLabels.size(); i++)
		{
			if (buttonActive[i] == 0) continue;
			const char *label = getButtonLabel(static_cast<int32_t>(i));
			if (label != nullptr) buttonLabels[i] = label;
		}
	}
	if (getAxisLabel != nullptr)
	{
		for (size_t i = 0; i < axisLabels.size(); i++)
		{
			if (axisActive[i] == 0) continue;
			const char *label = getAxisLabel(static_cast<int32_t>(i));
			if (label != nullptr) axisLabels[i] = label;
		}
	}
}

void ce_session::probeOptionalGroups()
{
	std::string err; // optional probes never fail the session
	auto opt = [&](const char *name, int argCount) { return proc(name, argCount, false, err); };
	auto cstr = [](uintptr_t p) { return p != 0 ? reinterpret_cast<const char *>(p) : nullptr; };

	/* which declared controls this machine has (see buttonActive). Each is
	 * optional on its own: a core whose ports change only its buttons need not
	 * answer for its axes. */
	/* the compile cache's counters and the precompile session's progress */
	fnCacheStored = opt("GetCacheStored", 0);
	fnCacheFetched = opt("GetCacheFetched", 0);
	fnPrecompileDone = opt("IsPrecompileDone", 0);
	fnPrecompileDoneCount = opt("GetPrecompileDone", 0);
	fnPrecompileTotal = opt("GetPrecompileTotal", 0);

	/* the drive lights, all three or none: a count with no way to read a light
	 * is a status bar with dead icons on it */
	{
		auto count = reinterpret_cast<int32_t (*)()>(opt("GetDriveCount", 0));
		auto name = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetDriveName", 1));
		auto light = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetDriveLight", 1));
		if (count != nullptr && name != nullptr && light != nullptr)
		{
			driveCount = count;
			driveName = name;
			driveLight = light;
			const int32_t n = count();
			for (int32_t i = 0; i < n; i++)
			{
				const char *nm = cstr(name(i));
				driveNames.push_back(nm != nullptr ? nm : tr("engine.drive"));
			}

			/* what the drives hold: all four or none, for the lights' reason -
			 * a count of images with no way to ask which one is in is a label
			 * that can only ever be wrong */
			auto mediaCount = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetDriveMediaCount", 1));
			auto mediaName = reinterpret_cast<uintptr_t (*)(int32_t, int32_t)>(opt("GetDriveMediaName", 2));
			auto mediaSelected = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetDriveMediaSelected", 1));
			auto mediaInserted = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetDriveMediaInserted", 1));
			driveMedia.assign(static_cast<size_t>(n > 0 ? n : 0), {});
			if (mediaCount != nullptr && mediaName != nullptr && mediaSelected != nullptr && mediaInserted != nullptr)
			{
				driveMediaSelected = mediaSelected;
				driveMediaInserted = mediaInserted;
				for (int32_t i = 0; i < n; i++)
				{
					const int32_t held = mediaCount(i);
					for (int32_t m = 0; m < held; m++)
					{
						const char *nm = cstr(mediaName(i, m));
						driveMedia[static_cast<size_t>(i)].push_back(nm != nullptr ? nm : "");
					}
				}
			}
		}
	}

	/* the settings this GAME has beyond the package's (an arcade game's dip
	 * switches): declared by the running core, read once, after Init */
	if (auto gs = reinterpret_cast<uintptr_t (*)()>(opt("GetGameSettings", 0)))
	{
		const char *text = cstr(gs());
		gameSettings = text != nullptr ? text : "";
	}

	{
		auto ax = reinterpret_cast<int32_t (*)()>(opt("GetDisplayAspectX", 0));
		auto ay = reinterpret_cast<int32_t (*)()>(opt("GetDisplayAspectY", 0));
		if (ax != nullptr && ay != nullptr)
		{
			aspectX = ax;
			aspectY = ay;
		}
	}

	isButtonActive = reinterpret_cast<int32_t (*)(int32_t)>(opt("IsButtonActive", 1));
	isAxisActive = reinterpret_cast<int32_t (*)(int32_t)>(opt("IsAxisActive", 1));
	getButtonLabel = reinterpret_cast<const char *(*)(int32_t)>(opt("GetButtonLabel", 1));
	getAxisLabel = reinterpret_cast<const char *(*)(int32_t)>(opt("GetAxisLabel", 1));
	stateLoaded = reinterpret_cast<void (*)(void)>(opt("StateLoaded", 0));
	stateSavingExport = reinterpret_cast<void (*)(void)>(opt("StateSaving", 0));
	history.beforeState([this] { return stateSaving(); });

	// surfaces: all five or nothing
	{
		auto count = reinterpret_cast<int32_t (*)()>(opt("GetSurfaceCount", 0));
		auto name = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetSurfaceName", 1));
		auto width = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetSurfaceWidth", 1));
		auto height = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetSurfaceHeight", 1));
		auto render = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("RenderSurface", 1));
		if (count != nullptr && name != nullptr && width != nullptr && height != nullptr && render != nullptr)
		{
			int32_t n = count();
			for (int32_t i = 0; i < n; i++)
			{
				const char *sn = cstr(name(i));
				surfaceNames.emplace_back(sn != nullptr ? sn : tr("engine.surface", {std::to_string(i)}));
				surfaceWidths.push_back(width(i));
				surfaceHeights.push_back(height(i));
				surfaceBufs.emplace_back(static_cast<size_t>(width(i)) * height(i), 0);
			}
			if (n > 0) renderSurface = render;
		}
	}

	// registers: count/name/value required; bits, set and cycles optional
	{
		auto count = reinterpret_cast<int32_t (*)()>(opt("GetRegisterCount", 0));
		auto name = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetRegisterName", 1));
		auto value = reinterpret_cast<int64_t (*)(int32_t)>(opt("GetRegisterValue", 1));
		if (count != nullptr && name != nullptr && value != nullptr)
		{
			int32_t n = count();
			if (n > 0)
			{
				/* width is per-register and only the core knows it; cores that
				 * don't say get 32 bits, which only shapes the hex display */
				auto bits = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetRegisterBits", 1));
				for (int32_t i = 0; i < n; i++)
				{
					const char *rn = cstr(name(i));
					regNames.emplace_back(rn != nullptr ? rn : ("R" + std::to_string(i)));
					int32_t b = bits != nullptr ? bits(i) : 32;
					regBits.push_back(b > 0 && b <= 64 ? b : 32);
				}
				regValue = value;
				regSet = reinterpret_cast<void (*)(int32_t, int64_t)>(opt("SetRegisterValue", 2));
				executedCycles = reinterpret_cast<int64_t (*)()>(opt("GetExecutedCycles", 0));
			}
		}
	}

	// buses: count/name/peek required; poke, size and writability optional
	{
		auto count = reinterpret_cast<int32_t (*)()>(opt("GetBusCount", 0));
		auto name = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetBusName", 1));
		auto peek = reinterpret_cast<int32_t (*)(int32_t, int32_t)>(opt("PeekBus", 2));
		if (count != nullptr && name != nullptr && peek != nullptr)
		{
			auto poke = reinterpret_cast<void (*)(int32_t, int32_t, int32_t)>(opt("PokeBus", 3));
			auto size = reinterpret_cast<int64_t (*)(int32_t)>(opt("GetBusSize", 1));
			auto writable = reinterpret_cast<int32_t (*)(int32_t)>(opt("GetBusWritable", 1));
			int32_t n = count();
			for (int32_t i = 0; i < n; i++)
			{
				const char *bn = cstr(name(i));
				busNames.emplace_back(bn != nullptr ? bn : tr("engine.bus", {std::to_string(i)}));
				busSizes.push_back(size != nullptr ? size(i) : 0x10000); // 64K default: the common case
				/* a core with a poke may still have read-only buses; claiming
				 * writable there hands the hex editor a silently discarded edit */
				busWritables.push_back(poke != nullptr && (writable != nullptr ? writable(i) : 1) != 0);
			}
			busPeek = peek;
			busPoke = poke;
			busRead = reinterpret_cast<uintptr_t (*)(int32_t, int64_t, int32_t)>(opt("ReadBus", 3));
			busRanges = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetBusRanges", 1));
		}
	}

	// trace: enable/lineCount/buffer required; the rest optional
	{
		auto setEnabled = reinterpret_cast<void (*)(int32_t)>(opt("TraceSetEnabled", 1));
		auto lineCount = reinterpret_cast<int32_t (*)()>(opt("TraceGetLineCount", 0));
		auto buffer = reinterpret_cast<uintptr_t (*)()>(opt("TraceGetBuffer", 0));
		if (setEnabled != nullptr && lineCount != nullptr && buffer != nullptr)
		{
			traceLineCount = lineCount;
			traceBuffer = buffer;
			traceOverflow = reinterpret_cast<int32_t (*)()>(opt("TraceGetOverflow", 0));
			traceUsedBytes = reinterpret_cast<int32_t (*)()>(opt("TraceGetUsedBytes", 0));
			traceClear = reinterpret_cast<void (*)()>(opt("TraceClear", 0));
			auto header = reinterpret_cast<uintptr_t (*)()>(opt("TraceGetHeader", 0));
			const char *h = header != nullptr ? cstr(header()) : nullptr;
			if (h != nullptr) traceHeader = h;
			traceSetEnabled = setEnabled;
			traceSetEnabled(0); // tracing costs the guest time; off until a sink asks
		}
	}

	// turbo: one export, and a core either has it or draws every frame. The
	// guest is told nothing here - the first advance sends whatever it wants,
	// and until then the core's own default (drawing) stands.
	setRendering = reinterpret_cast<void (*)(int32_t)>(opt("SetRenderingEnabled", 1));
	/* ...unless the package says its renderer must never be stopped, in which
	 * case turbo skips the readback and nothing else. The declaration lives
	 * here rather than in a frontend because it is a property of the core, and
	 * every host that opens this package needs it to hold. */
	drawAlways = cfg.drawEveryFrame ? 1 : 0;
	/* ...and whether a restore should look like a new context to this renderer.
	 * Here for the same reason drawAlways is: it is a property of the core, and
	 * every host that opens this package - the frontend, chimera-run, a gate -
	 * needs it to hold without being told separately. */
	ce_gl_rebuild_on_state_load(cfg.rebuildOnStateLoad ? 1 : 0);

	// savedata export: all four or nothing. Only the POINTERS are kept - the
	// file list is dynamic (a game creates files while it runs), so it is
	// snapshotted at export time, never here.
	{
		auto count = reinterpret_cast<int32_t (*)()>(opt("GetSaveDataFileCount", 0));
		auto name = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetSaveDataFileName", 1));
		auto size = reinterpret_cast<int64_t (*)(int32_t)>(opt("GetSaveDataFileSize", 1));
		auto buffer = reinterpret_cast<uintptr_t (*)(int32_t)>(opt("GetSaveDataFileBuffer", 1));
		if (count != nullptr && name != nullptr && size != nullptr && buffer != nullptr)
		{
			sdCount = count;
			sdName = name;
			sdSize = size;
			sdBuffer = buffer;
		}
		/* ...and the streaming pair, which replaces the pointer when a core has
		 * one. Both or neither: a reader that could ask for a window but not
		 * find it would silently export nothing. */
		auto read = reinterpret_cast<int64_t (*)(int32_t, int64_t, int64_t)>(opt("ReadSaveDataFile", 3));
		auto scratch = reinterpret_cast<uintptr_t (*)()>(opt("GetSaveDataScratch", 0));
		if (read != nullptr && scratch != nullptr)
		{
			sdRead = read;
			sdScratch = scratch;
		}
	}
}





/* The effective button states this frame: what ce_session_set_button holds,
 * OR'd with the packed mask's low 64 - callers use one path or the other, and
 * the OR keeps either alone exact. */
const uint8_t *ce_session::computeEffective(uint64_t mask)
{
	size_t n = btnEffective.size();
	for (size_t i = 0; i < n; i++)
	{
		uint8_t v = btnState[i];
		if (i < 64 && ((mask >> i) & 1ull) != 0) v = 1;
		btnEffective[i] = v;
	}
	return btnEffective.data();
}

/* Delivers wide states to the guest (deltas through its SetButton export -
 * only changes cross the boundary) and returns the packed mask for the low
 * 64, which every FrameAdvance still receives. */
uint64_t ce_session::sendButtons(const uint8_t *states)
{
	uint64_t mask = 0;
	size_t n = btnSent.size();
	for (size_t i = 0; i < n; i++)
	{
		uint8_t v = states != nullptr && states[i] != 0 ? 1 : 0;
		if (i < 64 && v != 0) mask |= 1ull << i;
		if (setButton != nullptr && v != btnSent[i])
		{
			setButton(static_cast<int32_t>(i), v);
			btnSent[i] = v;
		}
	}
	return mask;
}

void ce_session::copyVideo()
{
	/* THE FRAME FIRST, THEN ITS SIZE. A core settles what it is about to hand
	 * over while it hands it over - Flycast asks its renderer for the finished
	 * picture inside GetVideoBgra, and only then knows whether the machine is
	 * in a 640x480 mode or a 320x240 one. Asking the size first reads the
	 * PREVIOUS frame's, which is a stride away from the data that follows: the
	 * 240p Test Suite came out as two half-width copies of itself, because a
	 * 320-wide picture was copied 640 wide.
	 *
	 * Both gate runners have always done it in this order, which is the order
	 * the cores were written against; this was the odd one out.
	 *
	 * A machine that has not rendered yet answers null (DOS before its first
	 * mode set); the buffer keeps its previous frame. */
	const void *src = reinterpret_cast<const void *>(getVideoBgra());
	/* the core's rows are as long as ITS width: a picture wider than the
	 * package declares is cropped row by row, not copied as one block at the
	 * declared width (every row after the first then started in the wrong
	 * place - Xenia's 1080p picture, sheared, chimera#270) */
	int32_t srcStride = vidW;
	if (getVideoWidth != nullptr && getVideoHeight != nullptr)
	{
		int32_t w = getVideoWidth(), h = getVideoHeight();
		if (w > 0 && h > 0)
		{
			if ((w > cfg.width || h > cfg.height) && !saidOversize)
			{
				saidOversize = true;
				fprintf(stderr, "chimera: %s draws %dx%d, larger than the %dx%d its package declares; the picture is cropped\n",
					cfg.coreName.c_str(), w, h, cfg.width, cfg.height);
			}
			srcStride = w;
			vidW = w < cfg.width ? w : cfg.width;
			vidH = h < cfg.height ? h : cfg.height;
		}
	}
	growVideo(static_cast<size_t>(vidW) * static_cast<size_t>(vidH));
	if (src != nullptr)
	{
		if (srcStride == vidW)
			std::memcpy(videoBuf.data(), src, static_cast<size_t>(vidW) * vidH * sizeof(uint32_t));
		else
			for (int32_t y = 0; y < vidH; y++)
				std::memcpy(videoBuf.data() + static_cast<size_t>(y) * vidW,
					static_cast<const uint32_t *>(src) + static_cast<size_t>(y) * srcStride,
					static_cast<size_t>(vidW) * sizeof(uint32_t));
	}
}

/* One line per traceEvery frames, on stderr, cheap enough to leave in a
 * release build because it costs nothing until the variable is set. The video
 * checksum is what tells a black picture apart from a missing one: a machine
 * drawing a dark scene still moves it. */
void ce_session::trace(int32_t lag, int32_t render)
{
	traceFrame++;
	if (lag != 0) traceLag++;
	if (traceEvery <= 0) return;
	if (traceTty != nullptr && traceTtySize != nullptr)
	{
		const int64_t n = traceTtySize();
		const auto *text = reinterpret_cast<const uint8_t *>(traceTty());
		if (text != nullptr && n > traceTtySeen)
		{
			fwrite(text + traceTtySeen, 1, static_cast<size_t>(n - traceTtySeen), stderr);
			traceTtySeen = n;
		}
		else if (n < traceTtySeen)
		{
			traceTtySeen = n; // a state load rewound the machine's own log
		}
	}
	if ((traceFrame % traceEvery) != 0) return;
	uint64_t vsum = 0;
	uint64_t vlit = 0;
	if (render != 0)
	{
		const size_t px = static_cast<size_t>(vidW) * static_cast<size_t>(vidH);
		for (size_t i = 0; i < px && i < videoBuf.size(); i++)
		{
			const uint32_t p = videoBuf[i];
			vsum = vsum * 131 + p;
			if ((p & 0x00FFFFFFu) != 0) vlit++;
		}
	}
	fprintf(stderr,
		"[trace] frame %lld lag %lld threads %d running %d time %llums digest %016llx "
		"video %dx%d sum %016llx lit %llu audio %d\n",
		(long long)traceFrame, (long long)traceLag,
		traceThreads != nullptr ? traceThreads() : -1,
		traceRunning != nullptr ? traceRunning() : -1,
		(unsigned long long)(traceTimeNs != nullptr ? traceTimeNs() / 1000000ull : 0ull),
		(unsigned long long)(traceDigest != nullptr ? traceDigest() : 0ull),
		vidW, vidH, (unsigned long long)vsum, (unsigned long long)vlit, sampleCount);
	fflush(stderr);
}

/* Turbo. A frame nobody is going to look at does not need to be drawn, and for
 * a machine with a 3D chip in it the drawing is most of the frame. The core is
 * asked to stop producing a picture; it must go on being exactly the machine it
 * would have been, which is the whole contract and what each core's gate
 * checks. Cores without the export simply draw, and the caller pays.
 *
 * The state is a delta: telling a core the same thing every frame would be one
 * pointless guest call per frame on the seek path, which is the path this
 * exists for. */

/* CHIMERA_PICTURE_TRACE=1: a line on stderr for every load, and for each of
 * the eight frames that run after it - was the frame read back, how many have
 * been shown since the load, and a sum of the picture AS THE CORE HANDED IT
 * OVER, before a kept one stands in for it. Two frames with the same sum are
 * the same picture, which is how the question "is this frame's picture its
 * own?" is answered on a machine nobody is looking at: on a real card xemu's
 * first frame after a load carries the sum of the frame it was loaded on
 * (#190). One getenv a process until it is set. */
static bool pictureTrace()
{
	static const bool on = std::getenv("CHIMERA_PICTURE_TRACE") != nullptr;
	return on;
}

static unsigned pictureSum(const std::vector<uint32_t> &picture)
{
	unsigned sum = 2166136261u;
	for (size_t i = 0; i < picture.size(); i += 7) sum = (sum ^ picture[i]) * 16777619u;
	return sum;
}

/* A frame has run and was shown. For the first pictureSettle frames shown
 * after a state load - however many ran unseen in between, a seek's among them -
 * the picture just read back is suspect - a renderer on the far side of the
 * bridge is still rebuilding what the load threw away - so a frame that has a
 * picture from an earlier time shows that one: the machine is the same
 * machine, so it is the same picture. Such a frame's own picture is never
 * kept. Past the settle the picture read back is the one shown, and it is
 * kept, replacing what the frame had: a renderer can also be late the FIRST
 * time (a shader still compiling), and the later picture is the better one. */
void ce_session::afterFrame(bool rendered, int64_t known)
{
	if (known >= 0) machineFrame = known;
	else if (machineFrame >= 0) machineFrame++;
	lastKept = -1;
	if (pictureTrace() && tracedSinceLoad < 8)
	{
		tracedSinceLoad++;
		std::fprintf(stderr, "pictrace: frame %lld, #%d after the load: %s, %lld shown before it (settle %d), picture %08x\n",
			(long long)machineFrame, tracedSinceLoad, rendered ? "read back" : "not read back",
			(long long)sinceLoad, pictureSettle, pictureSum(videoBuf));
	}
	if (!rendered) return;
	sinceLoad++;
	if (machineFrame < 0 || pictures.budget() == 0) return;
	if (sinceLoad > pictureSettle)
	{
		if (pictures.keep(machineFrame, videoBuf.data(), vidW, vidH)) lastKept = machineFrame;
		return;
	}
	int32_t w = 0, h = 0;
	roomForKept();
	if (pictures.show(machineFrame, videoBuf.data(), videoBuf.size(), &w, &h))
	{
		vidW = w;
		vidH = h;
	}
}

/* A state was loaded: the machine is on `landedOn` (-1: the caller knows, the
 * session does not), and what it draws next is not to be kept for a while.
 * A frame the session has a picture for shows it at once - a load draws
 * nothing by itself, and until now the picture left on the screen was the one
 * from before the load. */
void ce_session::afterLoad(int64_t landedOn)
{
	machineFrame = landedOn;
	sinceLoad = 0;
	firstFrame = true;
	lastKept = -1;
	tracedSinceLoad = 0;
	const unsigned before = pictureTrace() ? pictureSum(videoBuf) : 0;
	bool shown = false;
	if (landedOn >= 0 && pictures.budget() != 0)
	{
		int32_t w = 0, h = 0;
		roomForKept();
		shown = pictures.show(landedOn, videoBuf.data(), videoBuf.size(), &w, &h);
		if (shown)
		{
			vidW = w;
			vidH = h;
		}
	}
	if (pictureTrace())
	{
		std::fprintf(stderr, "pictrace: load, on frame %lld: picture %08x before it, %s %08x\n", (long long)landedOn, before,
			shown ? "the frame's kept picture put up," : "no kept picture, still", pictureSum(videoBuf));
	}
}

/* A state is about to be taken. The core may draw on the bridge to answer -
 * a renderer reading back what it drew does - and it does so outside a frame,
 * so the context it borrowed is given back here as a frame's is.
 *
 * CHIMERA_NO_STATE_SAVING=1 leaves the core untold: the machine as it was
 * before the export existed, which is the control for what the export buys
 * (the picture after a load) and for what it costs (a capture's time). */
static bool stateSavingOff()
{
	static const bool off = std::getenv("CHIMERA_NO_STATE_SAVING") != nullptr;
	return off;
}

/* A savestate was loaded into the guest. To a core drawing through the Vulkan
 * bridge everything its memory now names belongs to another moment: the host
 * half destroys it all and the context is a new one, which the core finds out
 * by asking (chimera_vk_context_id) and makes its objects again. */
void ce_session::vulkanStateLoaded()
{
#ifdef CE_VK_BRIDGE
	if (vulkanBridge) chimera_vk_host_state_loaded();
#endif
}

/* False when the machine is dead - before, or because of, the core's
 * StateSaving (nine GPU cores read textures back in it, and a core can die
 * there): a state of a dead machine is not one to keep (2026-10-18). */
bool ce_session::stateSaving()
{
	if (guestDied()) return false;
	if (stateSavingExport == nullptr || stateSavingOff()) return true;
	stateSavingExport();
	ce_gl_release();
	return !guestDied();
}

void ce_session::wantRendering(int32_t on)
{
	if (drawAlways != 0) on = 1;
	if (setRendering == nullptr || renderingSent == on) return;
	setRendering(on);
	renderingSent = on;
}

void ce_session::runFrame(uint64_t pad)
{
	if (watchdog && !precompile)
	{
		if (firstFrame)
		{
			const int64_t limit = startLimitMs();
			watchdog->arm(limit, tr("engine.watchdog_first_frame", {secondsText(limit)}));
		}
		else
		{
			const int64_t limit = frameLimitMs();
			watchdog->arm(limit, tr("engine.watchdog_frame", {secondsText(limit)}));
		}
	}
	firstFrame = false;
	applyFreezes();
	t_framing = this;
	frameAdvance(pad);
	t_framing = nullptr;
	if (watchdog) watchdog->disarm();
}

int32_t ce_session::advanceCore(const uint8_t *buttons, int32_t render)
{
	if (guestDied()) return -1;
	wantRendering(render != 0 ? 1 : 0);
	runFrame(sendButtons(buttons));
	if (guestDied())
	{
		ce_gl_release();   /* the frame's borrowed context, which the frame never gave back */
		return -1;
	}
	if (render != 0) copyVideo();
	/* after every frame, a seek's too: a frame's length is the rate the core
	 * says after it, which the window adds up (Display Real Time) */
	readRate();
	/* The core runs after its frame too - the picture's readback, the audio, the
	 * input question - and can die in any of them. A frame a dead machine ended
	 * is not counted, recorded or stored, as one that died in FrameAdvance is
	 * not (2026-10-18). */
	const int32_t lag = inputWasRead != nullptr && inputWasRead() == 0 ? 1 : 0;
	int32_t nsamp = getAudioSampleCount != nullptr ? getAudioSampleCount() : cfg.samplesPerFrame;
	if (nsamp < 0) nsamp = 0;
	if (nsamp > cfg.samplesPerFrame) nsamp = cfg.samplesPerFrame;
	const auto *src = reinterpret_cast<const int16_t *>(getAudio());
	if (src == nullptr) nsamp = 0; /* no sound set up yet: silence, not a read of address 0 */
	if (cfg.channels == 2)
	{
		if (nsamp > 0) std::memcpy(audioBuf.data(), src, static_cast<size_t>(nsamp) * 2 * sizeof(int16_t));
	}
	else
	{
		for (int32_t i = 0; i < nsamp; i++)
		{
			audioBuf[static_cast<size_t>(i) * 2] = src[i];
			audioBuf[static_cast<size_t>(i) * 2 + 1] = src[i];
		}
	}
	sampleCount = nsamp;
	audioSerial++;
	if (guestDied())
	{
		ce_gl_release();
		return -1;
	}
	frame++;
	afterFrame(render != 0, frame);
	ce_gl_audit_frame(frame);   /* which frame the bridge's objects are being made on */
	/* The frame is over and the caller draws next, so the GL context the bridge
	 * borrowed goes back before it does - the same thing ce_session_frame_advance
	 * does, and for the same reason.
	 *
	 * It was missing here, which is how the movie path differed from the
	 * frontend's: a caller that plays a movie AND draws would have found our
	 * context still current under it. Nothing in the tree does both, so it cost
	 * nobody a picture; what it cost was the frame BOUNDARY, which is where the
	 * bridge counts a frame, prints its per-frame diagnostics and retires a
	 * frame's worth of bookkeeping. On this path there were no frames at all,
	 * and a measurement taken through chimera-run silently had none. */
	ce_gl_release();
	trace(lag, render);
	return lag;
}

void ce_session::greenzoneCapture()
{
	history.capture(frame);
}

/* Puts the machine on a stored frame and re-establishes what a state load
 * always breaks. Shared by ce_session_seek, which then replays the rest, and by
 * ce_session_greenzone_restore, whose caller replays it itself. */
bool ce_session::greenzoneRestore(int64_t to)
{
	int64_t landed = -1;
	if (!history.restore(to, error, &landed))
	{
		/* A restore that could not be walked leaves the machine on a frame it
		 * CAN be trusted on (StateHistory::restoreFailed), which is not the one
		 * asked for. Following it here is what keeps the session's idea of
		 * where it is and the machine itself the same thing - a seek that
		 * refuses is recoverable, a session that has quietly moved is not. */
		if (landed >= 0) greenzoneLoaded(landed);   /* an anchor was loaded, and stays */
		else lostMachine = tr("engine.greenzone_lost", {error});
		return false;
	}
	greenzoneLoaded(to);
	return true;
}

/* Everything a whole machine put in place by the history breaks, put right -
 * after a restore, and after one that fell back to its anchor, which loads a
 * machine just as much. That fallback used to do only the first and the last
 * of these, so its next frames sent inputs against the latches of the machine
 * before it and drew from GPU objects the load had replaced. */
void ce_session::greenzoneLoaded(int64_t at)
{
	lostMachine.clear();   /* a whole machine is back */
	if (stateLoaded != nullptr) stateLoaded();
	if (traceSetEnabled != nullptr)
	{
		traceSetEnabled(traceDesired ? 1 : 0);
		if (traceClear != nullptr) traceClear();
	}
	/* same as ce_session_load_state: the restore rewrote the guest's wide-input
	 * latches, so resend every button on the next advance */
	std::fill(btnSent.begin(), btnSent.end(), uint8_t{ 0xFF });
	renderingSent = -1; // see ce_session_load_state
	/* The greenzone restores through the history rather than through
	 * ce_session_load_state, so the bridge has to be told here too - this is
	 * the path a rewind and a re-record actually take. */
	ce_gl_release();
	ce_gl_state_loaded(at);
	vulkanStateLoaded();
	frame = at;
	afterLoad(at);
}

std::string chimera::sessionKeyOf(const ce_session *s, const chimera::EntryLayout &layout)
{
	return layout.key(s->cfg.buttons);
}

chimera::EntryLayout chimera::sessionLayoutOf(const ce_session *s, const ce_movie_log *log)
{
	const char *key = log != nullptr ? ce_movie_log_key(log) : nullptr;
	chimera::EntryLayout keyed;
	if (key != nullptr && key[0] != '\0' && keyed.buildFromKey(key, s->cfg.buttons, s->cfg.axes)) return keyed;
	return s->layout;
}

std::string chimera::sessionEntryIn(const ce_session *s, const chimera::EntryLayout &layout,
	const uint8_t *states, const int32_t *axes)
{
	return layout.generate(states, axes, s->cfg.mnemonics);
}

bool chimera::sessionDecodeIn(const ce_session *s, const chimera::EntryLayout &layout, const char *line,
	uint8_t *states, int32_t *axes)
{
	std::vector<uint8_t> pressed;
	std::vector<int32_t> values;
	if (line == nullptr || !layout.parse(line, pressed, values)) return false;
	if (states != nullptr) std::copy(pressed.begin(), pressed.end(), states);
	if (axes != nullptr) std::copy(values.begin(), values.end(), axes);
	(void)s;
	return true;
}

bool chimera::sessionWidenLayout(const ce_session *s, const chimera::EntryLayout &layout, const char *key,
	const uint8_t *states, const int32_t *axes, chimera::EntryLayout &wider, std::string &newKey, std::string &columns)
{
	std::string group;
	for (size_t i = 0; states != nullptr && i < s->cfg.buttons.size(); i++)
	{
		if (states[i] != 0 && !layout.hasControl(false, static_cast<int32_t>(i))) group.append(s->cfg.buttons[i]).push_back('|');
	}
	for (size_t i = 0; axes != nullptr && i < s->cfg.axes.size(); i++)
	{
		if (axes[i] != s->cfg.axes[i].neutral && !layout.hasControl(true, static_cast<int32_t>(i)))
			group.append(s->cfg.axes[i].name).push_back('|');
	}
	if (group.empty()) return false;
	/* a log with no key is in the machine's order: the key that names it */
	std::string base = key != nullptr && key[0] != '\0' ? std::string(key) : layout.key(s->cfg.buttons);
	if (base.empty()) return false;
	newKey = base + "#" + group;
	if (!wider.buildFromKey(newKey, s->cfg.buttons, s->cfg.axes)) return false;
	/* the new group is last, so its empty columns follow every line */
	const std::string before = layout.generate(nullptr, nullptr, s->cfg.mnemonics);
	const std::string after = wider.generate(nullptr, nullptr, s->cfg.mnemonics);
	if (after.compare(0, before.size(), before) != 0) return false;
	columns = after.substr(before.size());
	return true;
}

void chimera::widenMovieLog(ce_movie_log *log, const std::string &newKey, const std::string &columns)
{
	const int64_t n = ce_movie_log_count(log);
	for (int64_t i = 0; i < n; i++)
	{
		std::string line = ce_movie_log_entry(log, i);
		if (line.empty() || line.back() != '|') line.push_back('|');
		ce_movie_log_set(log, i, (line + columns).c_str());
	}
	ce_movie_log_set_key(log, newKey.c_str());
}

/* A control the movie's LogKey does not name, pressed or off its neutral:
 * the key gains it, as a group of its own at the end, and every line gains
 * that group's empty columns (user-decided, 2026-10-18). Lines are read and
 * written by the key's names, so without this the machine was given a press
 * the movie did not keep, and the recording replayed differently. */
void ce_session::coverInKey(const uint8_t *states, const int32_t *axes)
{
	if (!movieKeyed || movie == nullptr) return;
	chimera::EntryLayout wider;
	std::string newKey, columns;
	if (!chimera::sessionWidenLayout(this, movieLayout, ce_movie_log_key(movie), states, axes, wider, newKey, columns)) return;
	const char *was = ce_movie_log_key(movie);
	const std::string added = newKey.substr(was != nullptr ? std::strlen(was) : 0);
	chimera::widenMovieLog(movie, newKey, columns);
	movieLayout = std::move(wider);
	fprintf(stderr, "[%s] the movie's LogKey did not name %s; it does now, and every line has its columns\n",
		cfg.coreName.c_str(), chimera::EntryLayout::namesIn(added).c_str());
	fflush(stderr);
}

/* Internal (mcp.hpp): the line this session's movie holds for a set of
 * pressed buttons and axis values. Not in the ABI on purpose - a frontend
 * that wants a line drives the movie; the MCP server's editing tools are the
 * engine's own. */
std::string chimera::sessionMovieEntry(const ce_session *s, const uint8_t *states, const int32_t *axes)
{
	/* the letters are the machine's own (cfg.mnemonics), not the ones a
	 * recording was started with, which a session that only plays never has */
	return s->entries().generate(states, axes, s->cfg.mnemonics);
}

/* Where a tool READS a domain's bytes [offset, offset + len): miniBox's read
 * view, a second view of the core's memory that never faults. Read through
 * the core's own pointer, a page the core protected (RPCS3's texture cache
 * watches pages that way) ran the core's fault handler on the tool's thread,
 * and the handler may write the picture back into memory: a movie could play
 * differently with RAM Search open (engine analysis, 2026-10-18). Null when
 * miniBox gives no view; a miniBox without the call is read through the
 * core's pointer, as before. Writes still go through the core's pointer,
 * where the state history sees them. */
static const uint8_t *domainReadView(const ce_session *s, int32_t index, int64_t offset, int64_t len)
{
	const uintptr_t base = s->mdPtr(index);
	if (base == 0 || offset < 0 || len <= 0) return nullptr;
	if (s->host->wbx_guest_read_view == nullptr) return reinterpret_cast<const uint8_t *>(base + static_cast<uintptr_t>(offset));
	chimera::WbxReturn r{};
	s->host->wbx_guest_read_view(s->obj, base + static_cast<uintptr_t>(offset), static_cast<uintptr_t>(len), &r);
	return r.errorMessage[0] == 0 ? reinterpret_cast<const uint8_t *>(r.data) : nullptr;
}

extern "C" {

/* openMachine's and ce_session_open_project's refusal, from outside them */
static ce_session *openRefused(std::string message, const char **error_out)
{
	*g_openError = std::move(message);
	if (error_out != nullptr) *error_out = g_openError->c_str();
	return nullptr;
}

/* The second the clock of the next machine opened on this thread starts at:
 * the project's, set by openProjectSession around its openMachine, and -1
 * (the sandbox's own) for everything else. */
static thread_local int64_t t_clockStart = -1;

static ce_session *openSession(
	const char *package_path,
	const uint8_t *rom, uint64_t rom_len, const char *rom_path,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	const char *const *extra_names, const uint8_t *const *extra_data,
	const uint64_t *extra_lens, const char *const *extra_paths, int32_t extra_count,
	const char **error_out)
{
	auto fail = [&](std::string message) -> ce_session *
	{
		*g_openError = std::move(message);
		if (error_out != nullptr) *error_out = g_openError->c_str();
		return nullptr;
	};
	if (error_out != nullptr) *error_out = nullptr;

	const char *hostError = nullptr;
	const chimera::HostApi *host = chimera::hostApi(&hostError);
	if (host == nullptr) return fail(hostError);

	const char *pkgError = nullptr;
	chimera::progress(tr("engine.progress_reading_package").c_str(), 0, 0);
	ce_package *pkg = ce_package_open(package_path, &pkgError);
	if (pkg == nullptr)
	{
		return fail(pkgError != nullptr ? pkgError : tr("engine.not_a_package", {package_path}));
	}
	if (!ce_package_is_waterbox(pkg))
	{
		ce_package_free(pkg);
		return fail(tr("engine.not_a_waterbox_package", {package_path}));
	}

	auto *s = new ce_session();
	static std::atomic<uint64_t> serials{ 0 };
	s->serial = ++serials;
	s->host = host;
	auto abort = [&](std::string message) -> ce_session *
	{
		if (pkg != nullptr) ce_package_free(pkg);
		pkg = nullptr;
		ce_session_free(s);
		return fail(std::move(message));
	};

	uint64_t len = 0;
	const uint8_t *entry = ce_package_entry(pkg, "waterbox.config", &len);
	if (entry == nullptr) return abort(tr("engine.no_config"));
	std::string cfgError;
	if (!parseConfig(reinterpret_cast<const char *>(entry), len, settings_overrides_json, s->cfg, cfgError))
	{
		/* Say which file and what is in it. This sentence reaches a person
		 * through a window that shows one line (the project wizard's compile
		 * step shows it for a child session), and the declarations alone not
		 * being JSON does not tell an empty file from a damaged copy or from
		 * one saved in another encoding.
		 * Only for the declarations themselves: parseConfig also refuses
		 * settings it cannot read, and those are not this file. */
		if (cfgError == tr(kConfigNotJson)) cfgError += describeUnreadable(package_path, entry, len);
		return abort(std::move(cfgError));
	}
	const char *sha1 = ce_package_sha1(pkg);
	const std::string packageSha1 = sha1 != nullptr ? sha1 : "";
	const int32_t asset_count = ce_package_asset_count(pkg);
	const bool romFromDisk = rom_path != nullptr && rom_path[0] != '\0';
	s->settingsBytes = s->cfg.settingsJson;

	// every mounted stream needs a stable address for the host's callback
	s->streams.reserve(6 + static_cast<size_t>(firmware_count) + static_cast<size_t>(extra_count)
	                   + static_cast<size_t>(asset_count > 0 ? asset_count : 0));

	chimera::WbxLayout layout{};
	layout.sbrkSize = static_cast<uintptr_t>(s->cfg.layoutMiB[0] << 20);
	layout.sealedSize = static_cast<uintptr_t>(s->cfg.layoutMiB[1] << 20);
	layout.invisSize = static_cast<uintptr_t>(s->cfg.layoutMiB[2] << 20);
	layout.plainSize = static_cast<uintptr_t>(s->cfg.layoutMiB[3] << 20);
	layout.mmapSize = static_cast<uintptr_t>(s->cfg.layoutMiB[4] << 20);

	/* Every create and mount reads its stream whole into the host's own copy
	 * and never again, so each is handed the bytes where they already are -
	 * the package's entry, the caller's buffer - and not a copy of its own.
	 * The engine used to copy the core (333 MB for MAME, 140 for RPCS3), the
	 * firmware (a PS3 update is 206 MB) and every asset first, all the assets
	 * at once (engine analysis, 2026-10-18). */
	chimera::WbxReturn r{};
	entry = ce_package_entry(pkg, "core.wbx", &len);
	if (entry == nullptr) return abort(tr("engine.no_wbx"));
	s->streams.push_back({ entry, len });
	host->wbx_create_host(&layout, "core.wbx", streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), &r);
	doneWith(s->streams.back());
	if (!r.ok()) return abort(r.errorMessage);
	s->obj = reinterpret_cast<void *>(r.data);
	/* before the core first runs: a game may read the clock as it starts */
	if (t_clockStart >= 0)
	{
		if (host->wbx_set_clock_start == nullptr)
			return abort(tr("engine.no_clock_start"));
		host->wbx_set_clock_start(s->obj, static_cast<uint64_t>(t_clockStart), &r);
		if (!r.ok()) return abort(r.errorMessage);
	}

	/* The core's own files travel in the package and are mounted with the
	 * "assets" prefix dropped: assets/sys/... becomes /sys/... in the guest.
	 * One at a time, while the package is open (an entry lasts until the next
	 * is read), and before the game: the order files are mounted in is not
	 * something a core can see. */
	for (int32_t i = 0; i < asset_count; i++)
	{
		const char *aname = ce_package_asset_name(pkg, i);
		if (aname == nullptr) continue;
		const std::string assetName = std::string(aname).substr(6);
		const uint8_t *abytes = ce_package_entry(pkg, aname, &len);
		if (abytes == nullptr) return abort(tr("engine.asset_unreadable", {aname}));
		s->streams.push_back({ abytes, len });
		host->wbx_mount_file(s->obj, assetName.c_str(), streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
		doneWith(s->streams.back());
		if (!r.ok()) return abort(tr("engine.mounting_asset", {assetName, r.errorMessage}));
	}
	/* the package's texts outlive it: what the core returns names their keys */
	s->texts.read([pkg](const std::string &name, std::string &bytes) {
		uint64_t size = 0;
		const uint8_t *data = ce_package_entry(pkg, name.c_str(), &size);
		if (data == nullptr) return false;
		bytes.assign(reinterpret_cast<const char *>(data), static_cast<size_t>(size));
		return true;
	});
	ce_package_free(pkg);
	pkg = nullptr; /* a later abort() must not free it again */

	chimera::progress(tr("engine.progress_mounting").c_str(), 0, 0);
	/* The game itself, which is the one most likely to be enormous: mounted
	 * from where it lies when the caller said where that is. */
	if (romFromDisk)
	{
		host->wbx_mount_file_path(s->obj, s->cfg.romFile.c_str(), rom_path, &r);
	}
	else
	{
		s->streams.push_back({ rom, rom != nullptr ? rom_len : 0 });
		host->wbx_mount_file(s->obj, s->cfg.romFile.c_str(), streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
		doneWith(s->streams.back());
	}
	if (!r.ok()) return abort(tr("engine.mounting", {s->cfg.romFile, r.errorMessage}));

	/* The same bytes under the file's real name too, the way a project mounts
	 * its slot files: an extension-driven core (a GameCube .dol vs .iso) can
	 * then boot the name instead of guessing from the fixed mount. */
	std::string romAlias;
	if (rom_path != nullptr && rom_path[0] != '\0')
	{
		const char *base = strrchr(rom_path, '/');
#ifdef _WIN32
		const char *bs = strrchr(rom_path, '\\');
		if (bs != nullptr && (base == nullptr || bs > base)) base = bs;
#endif
		base = base != nullptr ? base + 1 : rom_path;
		romAlias = std::string("/") + base;
		const std::string &alias = romAlias;
		if (alias != s->cfg.romFile)
		{
			if (romFromDisk)
			{
				host->wbx_mount_file_path(s->obj, alias.c_str(), rom_path, &r);
			}
			else
			{
				s->streams.push_back({ rom, rom != nullptr ? rom_len : 0 });
				host->wbx_mount_file(s->obj, alias.c_str(), streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
				doneWith(s->streams.back());
			}
			if (!r.ok()) return abort(tr("engine.mounting", {alias, r.errorMessage}));
		}
	}

	/* a directly-opened rom has a name too: when no caller-provided extra
	 * carries "rom.name", it is the name the rom was just MOUNTED under, so a
	 * core can open what rom.name says and find it there. It used to be the
	 * bare basename while the mount carried a leading slash, and every core
	 * that booted rom.name verbatim had to paper over the difference (Dolphin
	 * and PPSSPP both probe two spellings because of it). */
	{
		bool haveName = false;
		for (int32_t i = 0; i < extra_count; i++)
			if (extra_names != nullptr && extra_names[i] != nullptr && strcmp(extra_names[i], "rom.name") == 0)
			{
				haveName = true;
				break;
			}
		if (!haveName && rom_path != nullptr && rom_path[0] != '\0')
		{
			const char *base = strrchr(rom_path, '/');
#ifdef _WIN32
			const char *bs = strrchr(rom_path, '\\');
			if (bs != nullptr && (base == nullptr || bs > base)) base = bs;
#endif
			base = base != nullptr ? base + 1 : rom_path;
			const std::string name = romAlias.empty() ? std::string(base) : romAlias;
			s->romNameBytes.assign(name.begin(), name.end());
			s->streams.push_back({ s->romNameBytes.data(), s->romNameBytes.size() });
			host->wbx_mount_file(s->obj, "rom.name", streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
			if (!r.ok()) return abort(tr("engine.mounting", {"rom.name", r.errorMessage}));
		}
	}

	/* the settings channel: always mounted (empty object when the core has no
	 * settings), so the guest ABI is uniform */
	s->streams.push_back({ reinterpret_cast<const uint8_t *>(s->settingsBytes.data()), s->settingsBytes.size() });
	host->wbx_mount_file(s->obj, "settings", streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
	if (!r.ok()) return abort(r.errorMessage);

	for (int32_t i = 0; i < firmware_count; i++)
	{
		s->streams.push_back({ firmware_data[i], firmware_data[i] != nullptr ? firmware_lens[i] : 0 });
		host->wbx_mount_file(s->obj, firmware_ids[i], streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
		doneWith(s->streams.back());
		if (!r.ok()) return abort(r.errorMessage);
	}

	/* a multi-file game's additional mounts: rom2..romN, support files,
	 * savedata, rom.name - the engine mounts names, the core gives them
	 * meaning (the same division firmware uses) */
	for (int32_t i = 0; i < extra_count; i++)
	{
		const char *path = extra_paths != nullptr ? extra_paths[i] : nullptr;
		if (path != nullptr && path[0] != '\0')
		{
			host->wbx_mount_file_path(s->obj, extra_names[i], path, &r);
		}
		else
		{
			s->streams.push_back({ extra_data[i], extra_data != nullptr && extra_data[i] != nullptr ? extra_lens[i] : 0 });
			host->wbx_mount_file(s->obj, extra_names[i], streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
			doneWith(s->streams.back());
		}
		if (!r.ok()) return abort(tr("engine.mounting", {extra_names[i], r.errorMessage}));
	}

	/* The core log's request (ce_core_log): an empty file a core checks for
	 * and never reads, so the machine is the same with the log on or off. A
	 * caller that mounted its own "corelog" keeps it. */
	if (chimera::coreLogOn())
	{
		static const uint8_t none = 0;
		s->streams.push_back({ &none, 0 });
		host->wbx_mount_file(s->obj, "corelog", streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
		chimera::coreLogNote("session: core package " + std::string(package_path)
			+ (packageSha1.empty() ? std::string() : " (sha1 " + packageSha1 + ")"));
	}

	std::string err;
	if (!s->activate(err)) return abort(std::move(err));

	/* A session that only asks ends here: the files are mounted exactly as a
	 * run would have them, and the core is asked its question instead of
	 * being started - what it suggests for these files (SuggestSettings), or
	 * what a movie it is handed amounts to (ImportMovie). The caller reads the
	 * answer and frees the session; nothing else ever sees one. */
	if (s_answerExport != nullptr)
	{
		std::string ignored;
		auto ask = reinterpret_cast<uintptr_t (*)()>(s->proc(s_answerExport, 0, false, ignored));
		const char *text = ask != nullptr ? reinterpret_cast<const char *>(ask()) : nullptr;
		s->suggestion = text != nullptr ? s->texts.texts(text) : "";
		return s;
	}

	/* The GPU bridge, before Init because Init is where a core picks its
	 * renderer. Three things have to be true and any of them may not be: the
	 * caller asked (ce_gl_request), this build has a bridge and a driver that
	 * will give it a context, and the core knows what to do with one. When any
	 * fails the core draws the way it draws without a GPU, which is the
	 * deterministic way, and the session simply does not claim otherwise.
	 */
	/* CHIMERA_NO_GPU=1 refuses the bridge for this run whatever the project
	 * asked for. A core drawn by a GPU and a core drawn by nobody fail in
	 * different ways, and telling them apart on a machine that is not here is
	 * otherwise a rebuild away. */
	const char *noGpu = getenv("CHIMERA_NO_GPU");
	if (noGpu != nullptr && noGpu[0] != '\0' && noGpu[0] != '0')
	{
		fprintf(stderr, "chimera gl: refused by CHIMERA_NO_GPU\n");
	}
	else if (ce_gl_requested() != 0)
	{
		std::string ignored;
		auto setBridge = reinterpret_cast<void (*)(uint64_t)>(
			s->proc("SetGpuBridge", 1, false, ignored));
		char glerr[256] = "";
		if (setBridge == nullptr)
		{
			/* not a failure: most cores have no GL renderer at all */
		}
		else if (ce_gl_start(glerr, (int32_t)sizeof glerr) == 0)
		{
			fprintf(stderr, "chimera gl: no context (%s); drawing in software\n", glerr);
		}
		else
		{
			s->glBridge = true;
			/* what the core hands GL is asked of the sandbox before it is followed */
			ce_gl_guest_check([](void *ctx, uintptr_t addr, uintptr_t len, int write) -> int {
				auto *self = static_cast<ce_session *>(ctx);
				if (self->host->wbx_guest_range_ok == nullptr) return 1;
				chimera::WbxReturn rr{};
				self->host->wbx_guest_range_ok(self->obj, addr, len, write != 0, &rr);
				return !rr.ok() || rr.data != 0;
			}, s);
			chimera::WbxReturn r;
			host->wbx_get_callback_addr(s->obj, reinterpret_cast<void *>(&ce_gl_dispatch), 0, &r);
			if (!r.ok() || r.data == 0)
			{
				fprintf(stderr, "chimera gl: the sandbox would not take the callback\n");
			}
			else
			{
				setBridge(static_cast<uint64_t>(r.data));
				/* The core decides whether it took it - an older core, or one
				 * built against a longer entry-point list, declines - and it
				 * says so through the renderer it then reports using. What is
				 * certain here is that a GPU was offered and a context exists;
				 * the session records that, and the movie says a GPU drew. */
				s->gpuDrew = true;
				fprintf(stderr, "chimera gl: %s\n", ce_gl_description());
			}
		}
	}

#ifdef CE_VK_BRIDGE
	/* The Vulkan bridge: the same offer, for a core whose renderer has no
	 * OpenGL. The same request and the same refusal; another export, and a
	 * core has one or the other. The host half finds the machine's Vulkan
	 * loader when it is asked and makes nothing until the core asks for an
	 * instance, so a machine with no Vulkan loses nothing but this. */
	if (!(noGpu != nullptr && noGpu[0] != '\0' && noGpu[0] != '0') && ce_gl_requested() != 0)
	{
		std::string ignored;
		auto setVulkan = reinterpret_cast<void (*)(uint64_t)>(
			s->proc("SetVulkanBridge", 1, false, ignored));
		char vkerr[256] = "";
		if (setVulkan == nullptr)
		{
			/* not a failure: most cores draw some other way */
		}
		else if (chimera_vk_host_init(vkerr, (int)sizeof vkerr) != 0)
		{
			fprintf(stderr, "chimera vk: no Vulkan on this machine (%s)\n", vkerr);
		}
		else
		{
			chimera_vk_host_set_call_hook(ce_vk_flight_note);
			chimera::WbxReturn r;
			host->wbx_get_callback_addr(s->obj, reinterpret_cast<void *>(&vkDispatchCounted), 2, &r);
			if (!r.ok() || r.data == 0)
			{
				fprintf(stderr, "chimera vk: the sandbox would not take the callback\n");
			}
			else
			{
				setVulkan(static_cast<uint64_t>(r.data));
				s->gpuDrew = true;
				s->vulkanBridge = true;
				fprintf(stderr, "chimera vk: the bridge was handed over\n");
			}
		}
	}
#endif

	/* The compile cache, before Init because Init is where a core first asks
	 * for compiled objects. A core without the export keeps nothing. */
	if (ce_cache_dir_get()[0] != 0)
	{
		ce_cache_announce(s_precompileCount > 0);
		std::string ignored;
		auto setCache = reinterpret_cast<void (*)(uint64_t)>(s->proc("SetCacheBridge", 1, false, ignored));
		if (setCache != nullptr)
		{
			chimera::WbxReturn r;
			host->wbx_get_callback_addr(s->obj, reinterpret_cast<void *>(&ce_cache_dispatch), 1, &r);
			if (!r.ok() || r.data == 0)
				fprintf(stderr, "chimera cache: the sandbox would not take the callback\n");
			else
				setCache(static_cast<uint64_t>(r.data));
		}
	}

	/* Calls during a frame: a core that reports its input reads is handed the
	 * engine's callback (issue 344). A core without the export has none. */
	{
		std::string ignored;
		auto setInputPoll = reinterpret_cast<void (*)(uint64_t)>(s->proc("SetInputPollCallback", 1, false, ignored));
		if (setInputPoll != nullptr)
		{
			chimera::WbxReturn r;
			host->wbx_get_callback_addr(s->obj, reinterpret_cast<void *>(&inputPollDispatch), 3, &r);
			if (!r.ok() || r.data == 0)
				fprintf(stderr, "chimera input polls: the sandbox would not take the callback\n");
			else
			{
				setInputPoll(static_cast<uint64_t>(r.data));
				s->inputPollsReported = true;
			}
		}
	}

	/* A precompile session: the core boots, compiles its share and stops. */
	if (s_precompileCount > 0)
	{
		std::string ignored;
		auto setPre = reinterpret_cast<void (*)(int32_t, int32_t, int32_t)>(s->proc("SetPrecompile", 3, false, ignored));
		if (setPre == nullptr)
		{
			return abort(tr("engine.no_precompile", {s->cfg.coreName}));
		}
		setPre(s_precompileIndex, s_precompileCount, s_precompileFirmware);
		s->precompile = true;
	}

	chimera::progress(tr("engine.progress_starting").c_str(), 0, 0);
	auto init = reinterpret_cast<int32_t (*)()>(s->proc("Init", 0, true, err));
	if (init == nullptr) return abort(std::move(err));
	s->watchdog = std::make_unique<GuestWatchdog>(host, s->obj);
	s->watchdog->arm(startLimitMs(), tr("engine.watchdog_start", {secondsText(startLimitMs())}));
	const int32_t initResult = init();
	s->watchdog->disarm();
	/* Init is where a renderer makes its context and compiles its shaders, so
	 * it is also where the bridge first borrows the caller's GL slot. */
	ce_gl_release();
#ifdef CE_VK_BRIDGE
	/* ...and where a Vulkan renderer makes its device: which one is now
	 * known, and is the one line that says which card drew this run. */
	if (s->vulkanBridge)
	{
		const char *device = chimera_vk_host_description();
		fprintf(stderr, "chimera vk: %s\n", device != nullptr && device[0] != '\0'
			? device : "the core took the bridge and made no device");
	}
#endif
	/* A core that died in Init returns 0 like one that refused the file, and
	 * its reason - an assertion, a missing BIOS, what it printed - was lost to
	 * "could not load this file" (engine analysis, 2026-10-18). */
	if (s->guestDied()) return abort(tr("engine.stopped_starting", {s->death}));
	if (initResult != 1)
	{
		/* the core knows why it refused - GetLoadError is optional, so a core
		 * that says nothing still fails, just less helpfully */
		std::string reason;
		auto getLoadError = reinterpret_cast<uintptr_t (*)()>(s->proc("GetLoadError", 0, false, err));
		if (getLoadError != nullptr)
		{
			const char *text = reinterpret_cast<const char *>(getLoadError());
			if (text != nullptr) reason = s->texts.text(text);
		}
		return abort(reason.empty() ? tr("engine.could_not_load_file", {s->cfg.coreName}) : s->cfg.coreName + ": " + reason);
	}

	if (!s->deactivate(err)) return abort(std::move(err));
	chimera::progress(tr("engine.progress_sealing").c_str(), 0, 0);
	host->wbx_seal(s->obj, &r); // freeze the post-init image as the savestate baseline
	if (!r.ok()) return abort(r.errorMessage);
	if (!s->activate(err)) return abort(std::move(err));

	s->frameAdvance = reinterpret_cast<void (*)(uint64_t)>(s->proc("FrameAdvance", 1, true, err));
	if (s->frameAdvance == nullptr) return abort(std::move(err));
	if (!s->cfg.axes.empty())
	{
		s->setAxis = reinterpret_cast<void (*)(int32_t, int32_t)>(s->proc("SetAxis", 2, true, err));
		if (s->setAxis == nullptr)
		{
			return abort(tr("engine.no_set_axis", {s->cfg.coreName}));
		}
	}
	/* wide input: optional in general, REQUIRED past 64 buttons - without the
	 * export the extra buttons could never reach the machine, and a controller
	 * that silently drops keys is worse than one that refuses to load */
	s->setButton = reinterpret_cast<void (*)(int32_t, int32_t)>(s->proc("SetButton", 2, false, err));
	if (s->cfg.buttons.size() > 64 && s->setButton == nullptr)
	{
		return abort(tr("engine.no_set_button", {s->cfg.coreName, std::to_string(s->cfg.buttons.size())}));
	}
	s->getVideoBgra = reinterpret_cast<uintptr_t (*)()>(s->proc(s->cfg.getBgra.c_str(), 0, true, err));
	if (s->getVideoBgra == nullptr) return abort(std::move(err));
	s->getAudio = reinterpret_cast<uintptr_t (*)()>(s->proc(s->cfg.getAudio.c_str(), 0, true, err));
	if (s->getAudio == nullptr) return abort(std::move(err));

	/* optional exports: the guest's own answers override the config's */
	s->getAudioSampleCount = reinterpret_cast<int32_t (*)()>(s->proc("GetAudioSampleCount", 0, false, err));
	s->getVideoWidth = reinterpret_cast<int32_t (*)()>(s->proc("GetVideoWidth", 0, false, err));
	s->getVideoHeight = reinterpret_cast<int32_t (*)()>(s->proc("GetVideoHeight", 0, false, err));
	s->vidW = s->cfg.width;
	s->vidH = s->cfg.height;
	s->getVsyncNum = reinterpret_cast<int32_t (*)()>(s->proc("GetVsyncNumerator", 0, false, err));
	s->getVsyncDen = reinterpret_cast<int32_t (*)()>(s->proc("GetVsyncDenominator", 0, false, err));
	s->vsyncNum = s->cfg.vsyncNum;
	s->vsyncDen = s->cfg.vsyncDen;
	s->readRate();
	/* launch window: the first rendered frame may be many frames away (or
	 * never, when paused on frame 0), and until copyVideo runs the display
	 * shows the buffer capacity. Seed the live size from the guest now
	 * (same clamp as copyVideo); a guest that has not rendered yet answers
	 * <= 0 and keeps the capacity default. */
	if (s->getVideoWidth != nullptr && s->getVideoHeight != nullptr)
	{
		int32_t w = s->getVideoWidth(), h = s->getVideoHeight();
		if (w > 0 && h > 0)
		{
			s->vidW = w < s->cfg.width ? w : s->cfg.width;
			s->vidH = h < s->cfg.height ? h : s->cfg.height;
		}
	}
	if (!s->cfg.inputWasRead.empty())
	{
		s->inputWasRead = reinterpret_cast<int32_t (*)()>(s->proc(s->cfg.inputWasRead.c_str(), 0, true, err));
		if (s->inputWasRead == nullptr) return abort(std::move(err));
	}

	/* the trace's optional exports, resolved only when someone asked for it */
	if (const char *traceEnv = getenv("CHIMERA_TRACE"))
	{
		s->traceEvery = atoi(traceEnv);
		if (s->traceEvery <= 0) s->traceEvery = 100;
		s->traceThreads = reinterpret_cast<int32_t (*)()>(s->proc("GetThreadCount", 0, false, err));
		s->traceRunning = reinterpret_cast<int32_t (*)()>(s->proc("IsRunning", 0, false, err));
		s->traceDigest = reinterpret_cast<uint64_t (*)()>(s->proc("GetMainMemoryDigest", 0, false, err));
		s->traceTimeNs = reinterpret_cast<uint64_t (*)()>(s->proc("GetMachineTimeNs", 0, false, err));
		s->traceTty = reinterpret_cast<uintptr_t (*)()>(s->proc("GetTty", 0, false, err));
		s->traceTtySize = reinterpret_cast<int64_t (*)()>(s->proc("GetTtySize", 0, false, err));
		err.clear(); // every one of them is allowed to be absent
		fprintf(stderr, "[trace] every %d frames; %d firmware file(s)", s->traceEvery, firmware_count);
		for (int32_t i = 0; i < firmware_count; i++)
		{
			fprintf(stderr, " %s=%llu", firmware_ids[i], (unsigned long long)firmware_lens[i]);
		}
		fprintf(stderr, "; rom %s\n", rom_path != nullptr ? rom_path : "(bytes)");
		fflush(stderr);
	}

	/* memory domains are self-described post-Init (size can depend on settings) */
	s->mdCount = reinterpret_cast<int32_t (*)()>(s->proc("GetMemoryDomainCount", 0, true, err));
	s->mdName = reinterpret_cast<uintptr_t (*)(int32_t)>(s->proc("GetMemoryDomainName", 1, true, err));
	s->mdPtr = reinterpret_cast<uintptr_t (*)(int32_t)>(s->proc("GetMemoryDomainPtr", 1, true, err));
	s->mdSize = reinterpret_cast<int64_t (*)(int32_t)>(s->proc("GetMemoryDomainSize", 1, true, err));
	s->mdWritable = reinterpret_cast<int32_t (*)(int32_t)>(s->proc("GetMemoryDomainWritable", 1, true, err));
	/* every one of the five, not only the last: a core that lacked one opened,
	 * and the first domain call went through a null pointer (engine analysis,
	 * 2026-10-18) */
	if (s->mdCount == nullptr || s->mdName == nullptr || s->mdPtr == nullptr || s->mdSize == nullptr
		|| s->mdWritable == nullptr)
		return abort(err.empty() ? tr("engine.no_memory_domains") : std::move(err));
	{
		std::string none;
		s->mdHidden = reinterpret_cast<int32_t (*)(int32_t)>(s->proc("GetMemoryDomainHidden", 1, false, none));
	}

	/* the live size the guest gave, or the package's when it gave none */
	s->videoBuf.assign(static_cast<size_t>(s->vidW) * static_cast<size_t>(s->vidH), 0);
	s->audioBuf.assign(static_cast<size_t>(s->cfg.samplesPerFrame) * 2, 0);
	s->resampler.configure(s->cfg.audioRate, 44100);
	s->soundGap = true;
	s->btnState.assign(s->cfg.buttons.size(), 0);
	s->btnSent.assign(s->cfg.buttons.size(), 0); // a fresh guest holds nothing down
	s->btnEffective.assign(s->cfg.buttons.size(), 0);
	s->probeOptionalGroups();
	s->buildControlActivity();
	s->layout.build(s->cfg.buttons, s->cfg.axes, &s->buttonActive, &s->axisActive,
		[s](const std::string &name) { return s->cfg.names.playerOf(name); });

	/* a game core's properties: a table read once and checked against the
	 * domains, whose pointers hold for the session's lifetime - and against the
	 * buses, which is why this comes after they were probed for */
	{
		const char *table = nullptr;
		s->gameProperties = reinterpret_cast<uintptr_t (*)()>(s->proc("GetGameProperties", 0, false, err));
		if (s->gameProperties != nullptr) table = reinterpret_cast<const char *>(s->gameProperties());
		s->gameProperty = reinterpret_cast<uintptr_t (*)(const char *)>(s->proc("GetGameProperty", 1, false, err));
		err.clear(); // allowed to be absent
		std::vector<CeGameProperties::Domain> domains;
		for (int32_t i = 0; table != nullptr && i < s->mdCount(); i++)
		{
			CeGameProperties::Domain d;
			if (const char *name = reinterpret_cast<const char *>(s->mdName(i))) d.name = name;
			d.base = reinterpret_cast<uint8_t *>(s->mdPtr(i));
			d.size = s->mdSize(i);
			d.readBase = domainReadView(s, i, 0, d.size);
			d.writable = s->mdWritable(i) != 0;
			domains.push_back(std::move(d));
		}
		/* A bus can hold properties too: it has no pointer, so its memory is
		 * read and written through the bus (a Flash movie's variables are on
		 * its emulator's heap, which is one - docs/game-cores.md). A domain
		 * of the same name comes first and wins. */
		for (int32_t b = 0; table != nullptr && b < static_cast<int32_t>(s->busNames.size()); b++)
		{
			CeGameProperties::Domain d;
			d.name = s->busNames[static_cast<size_t>(b)];
			d.size = s->busSizes[static_cast<size_t>(b)];
			d.writable = s->busWritables[static_cast<size_t>(b)];
			ce_session *session = s;
			d.read = [session, b](int64_t offset, uint8_t *buf, int64_t len) { ce_session_bus_read(session, b, offset, buf, len); };
			d.write = [session, b](int64_t offset, const uint8_t *buf, int64_t len)
			{
				for (int64_t k = 0; k < len; k++) ce_session_bus_poke(session, b, static_cast<int32_t>(offset + k), buf[k]);
			};
			/* PokeBus takes a 32-bit address: past it a write would land at
			 * another, so a property there is read-only */
			d.writableBelow = int64_t{ INT32_MAX } + 1;
			domains.push_back(std::move(d));
		}
		s->properties.useTexts(&s->texts);
		s->properties.load(table, domains);
		for (const std::string &problem : s->properties.problems())
		{
			fprintf(stderr, "[%s] game properties: %s\n", s->cfg.coreName.c_str(), problem.c_str());
		}
	}
	return s;
}

/* Opens a machine from the files given. Not the engine's to offer: every
 * machine is a project's (user-decided, 2026-10-26), and openProjectSession
 * is the one caller, which gives a project's files this way.
 *
 * A file reaches the guest as BYTES (rom/rom_len, extra_data/extra_lens) for
 * something made here - a slot map, a name - or as a PATH (rom_path,
 * extra_paths[i]) for something on disk, which is never read into memory:
 * the guest's reads go to the file as it makes them. A PS2 disc image is
 * over four gigabytes, more than a frontend may be able to hold. The guest sees the
 * same bytes either way. */
static ce_session *openMachine(
	const char *package_path,
	const uint8_t *rom, uint64_t rom_len, const char *rom_path,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	const char *const *extra_names, const uint8_t *const *extra_data,
	const uint64_t *extra_lens, const char *const *extra_paths, int32_t extra_count,
	const char **error_out)
{
	/* Nothing thrown while a machine starts may end the process: the package
	 * and the memory it asks for are not Chimera's to vouch for (engine
	 * analysis, 2026-10-18). A machine started part way is not given back. */
	try
	{
		return openSession(package_path, rom, rom_len, rom_path, settings_overrides_json,
			firmware_ids, firmware_data, firmware_lens, firmware_count,
			extra_names, extra_data, extra_lens, extra_paths, extra_count, error_out);
	}
	catch (const std::bad_alloc &)
	{
		return openRefused(tr("engine.no_memory_start"), error_out);
	}
	catch (const std::exception &e)
	{
		return openRefused(tr("engine.not_started", {e.what()}), error_out);
	}
}

/* A session that only asks the core a question (askCore) of a project still
 * being made: its files are not checked against the package's slots, since a
 * slot the person has not filled yet is no reason to refuse a question. Not
 * in engine.h: no caller outside asks for it. */
static constexpr uint32_t kOpenToAsk = 0x80000000u;

/* more_names/more_paths: files mounted after the project's own, by path - a
 * movie being imported and the files offered with it (ce_project_import_movie) */
static ce_session *openProjectSession(
	const char *package_path, ce_project *project,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint32_t flags,
	const char *const *more_names, const char *const *more_paths, int32_t more_count,
	const char **error_out)
{
	auto fail = [&](std::string message) -> ce_session *
	{
		*g_openError = std::move(message);
		if (error_out != nullptr) *error_out = g_openError->c_str();
		return nullptr;
	};
	if (project == nullptr) return fail(tr("engine.no_project_given"));

	/* every file found, and holding what the project recorded - unless the
	 * caller says a person looked at the difference and went on */
	const int32_t fileCount = ce_project_file_count(project);
	for (int32_t i = 0; i < fileCount; i++)
	{
		/* Found when the project was opened, and maybe changed on the disk
		 * since (a ROM rebuilt in place, then Reboot Core): it is read again
		 * here, which costs nothing while the file is the one it was (the
		 * hash is kept by the file's identity), so the machine runs the bytes
		 * the project is checked against and a save records them (engine
		 * analysis, 2026-10-18). A file that cannot be read any more is not
		 * found. */
		const std::string source = ce_project_file_source_path(project, i);
		if (!source.empty() && ce_project_file_resolve(project, i, source.c_str(), nullptr) != 0)
			ce_project_file_unresolve(project, i);
		const int32_t status = ce_project_file_status(project, i);
		if (status == 1)
			return fail(tr("engine.project_file_missing", {ce_project_file_name(project, i)}));
		if (status == 2 && (flags & CE_OPEN_ALLOW_FILE_MISMATCH) == 0)
			return fail(tr("engine.project_file_changed", {ce_project_file_name(project, i)}));
	}

	/* the settings the machine starts with: the caller's, or the project's own */
	uint64_t len = 0;
	std::string settingsText;
	if (settings_overrides_json == nullptr)
	{
		settingsText = ce_project_settings_text(project, &len);
		settings_overrides_json = settingsText.c_str();
	}

	/* the core the project names, against the package given; and the project's
	 * files against the slots that package declares, when it declares any */
	{
		const char *packageError = nullptr;
		ce_package *pkg = ce_package_open(package_path, &packageError);
		if (pkg != nullptr)
		{
			const char *pin = ce_project_core_sha1(project);
			const char *actual = ce_package_sha1(pkg);
			if (pin[0] != '\0' && actual != nullptr && std::strcmp(pin, actual) != 0
				&& (flags & CE_OPEN_ALLOW_CORE_MISMATCH) == 0)
			{
				std::string detail = tr("engine.not_pinned_core", {pin, actual});
				ce_package_free(pkg);
				return fail(detail);
			}
			uint64_t declLen = 0;
			const uint8_t *declBytes = ce_package_entry(pkg, "file_slots.json", &declLen);
			/* kept: the next entry read may reuse the package's buffer */
			const std::string declText = declBytes != nullptr
				? std::string(reinterpret_cast<const char *>(declBytes), static_cast<size_t>(declLen)) : std::string();
			const char *decl = declBytes != nullptr ? declText.c_str() : nullptr;
			/* A slot gated on a setting is judged by the value the machine
			 * will boot with: the package's default where neither the caller
			 * nor the project gives one. Judged on the project's settings
			 * alone, a project that left the setting out (an agent's, a
			 * hand-written one) was refused though the machine would boot. */
			cJSON *running = nullptr;
			uint64_t configLen = 0;
			const uint8_t *config = ce_package_entry(pkg, "waterbox.config", &configLen);
			SessionConfig declared;
			std::string configError;
			if (decl != nullptr && config != nullptr
				&& parseConfig(reinterpret_cast<const char *>(config), configLen, settings_overrides_json, declared, configError))
			{
				running = cJSON_Parse(declared.settingsJson.c_str());
			}
			const char *slotError = nullptr;
			const int32_t refused = decl != nullptr && (flags & kOpenToAsk) == 0
				? ceProjectValidate(project, decl, declLen, running, &slotError)
				: 0;
			cJSON_Delete(running);
			if (refused != 0)
			{
				std::string detail = slotError != nullptr ? slotError : tr("engine.manifest_not_fit");
				ce_package_free(pkg);
				return fail(detail);
			}
			ce_package_free(pkg);
		}
	}

	/* firmware other than what the project pins: said, not refused (the
	 * window asks a person; a program going on is its own choice) */
	for (const std::string &warning : ceFirmwarePinMismatches(project, firmware_ids, firmware_data, firmware_lens, firmware_count))
	{
		fprintf(stderr, "[project] %s\n", warning.c_str());
	}
	fflush(stderr);

	/* What the core sees: the slot map as "slots", every file under its own
	 * name, and the first file that is not a support file as the rom, with its
	 * name as "rom.name" and the later files of its slot as rom2..N
	 * (docs/project.md). Files are given by where they lie, never read here. */
	const std::string slotsText = ce_project_slots_text(project, &len);

	std::vector<std::string> names;      /* the names this function makes */
	names.reserve(static_cast<size_t>(fileCount) + 1); /* pointers into it go out: it must not move */
	std::vector<const char *> extraNames, extraPaths;
	std::vector<const uint8_t *> extraData;
	std::vector<uint64_t> extraLens;
	auto addBytes = [&](const char *name, const std::string &bytes)
	{
		extraNames.push_back(name);
		extraData.push_back(reinterpret_cast<const uint8_t *>(bytes.data()));
		extraLens.push_back(bytes.size());
		extraPaths.push_back(nullptr);
	};
	auto addFile = [&](const char *name, const char *path)
	{
		extraNames.push_back(name);
		extraData.push_back(nullptr);
		extraLens.push_back(0);
		extraPaths.push_back(path);
	};

	addBytes("slots", slotsText);
	for (int32_t i = 0; i < fileCount; i++)
	{
		addFile(ce_project_file_name(project, i), ce_project_file_source_path(project, i));
	}
	const int32_t primary = ce_project_game_file(project);
	/* a project may hold no game at all: a machine that starts without a
	 * medium is opened with no rom, and the core sees an empty slot */
	const char *romPath = nullptr;
	if (primary >= 0)
	{
		romPath = ce_project_file_source_path(project, primary);
		names.emplace_back(ce_project_file_name(project, primary));
		addBytes("rom.name", names.back());
		const std::string primarySlot = ce_project_file_slot(project, primary);
		int n = 2;
		for (int32_t i = primary + 1; i < fileCount; i++)
		{
			if (primarySlot != ce_project_file_slot(project, i)) continue;
			names.push_back("rom" + std::to_string(n++));
			addFile(names.back().c_str(), ce_project_file_source_path(project, i));
		}
	}

	for (int32_t i = 0; i < more_count; i++) addFile(more_names[i], more_paths[i]);

	/* the project's clock start, for this one session */
	struct ClockFor
	{
		explicit ClockFor(int64_t start) { t_clockStart = start; }
		~ClockFor() { t_clockStart = -1; }
	} clock(ce_project_clock_start(project));
	return openMachine(
		package_path, nullptr, 0, romPath, settings_overrides_json,
		firmware_ids, firmware_data, firmware_lens, firmware_count,
		extraNames.data(), extraData.data(), extraLens.data(), extraPaths.data(),
		static_cast<int32_t>(extraNames.size()), error_out);
}

ce_session *ce_session_open_project(
	const char *package_path, ce_project *project,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint32_t flags, const char **error_out)
{
	try
	{
		return openProjectSession(package_path, project, settings_overrides_json,
			firmware_ids, firmware_data, firmware_lens, firmware_count, flags & ~kOpenToAsk, nullptr, nullptr, 0, error_out);
	}
	catch (const std::bad_alloc &)
	{
		return openRefused(tr("engine.no_memory_start"), error_out);
	}
	catch (const std::exception &e)
	{
		return openRefused(tr("engine.not_started", {e.what()}), error_out);
	}
}

void ce_session_free(ce_session *s)
{
	if (s != nullptr && ce_cache_dir_get()[0] != 0 && (s->fnCacheStored != 0 || s->fnCacheFetched != 0))
	{
		fprintf(stderr, "chimera cache: %llu stored, %llu fetched\n",
			(unsigned long long)ce_session_cache_stored(s), (unsigned long long)ce_session_cache_fetched(s));
	}
	if (s == nullptr) return;
#ifdef CE_VK_BRIDGE
	/* what the core made on the machine's Vulkan device goes with the session */
	if (s->vulkanBridge) chimera_vk_host_shutdown();
#endif
	if (s->movie != nullptr) ce_movie_log_free(s->movie);
	if (s->obj != nullptr)
	{
		/* An anchor may still be being filled, and it is filled from the
		 * machine that is about to go. Finishing costs what is left of one
		 * copy; not finishing is a thread reading a freed mapping. */
		s->history.configure(nullptr, nullptr, 0);
		std::string err;
		s->deactivate(err); // tearing down anyway
		chimera::WbxReturn r{};
		s->watchdog.reset();   /* before the machine it watches goes */
		s->host->wbx_destroy_host(s->obj, &r);
	}
	/* and what it made through the GL bridge, once nothing can call it */
	if (s->glBridge) { ce_gl_guest_check(nullptr, nullptr); ce_gl_session_end(); }
	delete s;
}

const char *ce_session_core_name(const ce_session *s) { return s->cfg.coreName.c_str(); }
const char *ce_session_system_id(const ce_session *s) { return s->cfg.systemId.c_str(); }
int32_t ce_session_machine_index(const ce_session *s) { return s->cfg.machineIndex; }
int32_t ce_session_width(const ce_session *s) { return s->cfg.width; }
int32_t ce_session_height(const ce_session *s) { return s->cfg.height; }
/* a machine that declares no virtual size has square pixels, so its picture
 * as it is now is its own shape (a Flash movie's stage), as Chimera's window
 * has it; the declared buffer is only the largest picture */
int32_t ce_session_virtual_width(const ce_session *s) { return s->cfg.virtualDeclared || s->vidW <= 0 ? s->cfg.virtualWidth : s->vidW; }
int32_t ce_session_virtual_height(const ce_session *s) { return s->cfg.virtualDeclared || s->vidH <= 0 ? s->cfg.virtualHeight : s->vidH; }

int32_t ce_session_display_aspect(const ce_session *s, int32_t *x_out, int32_t *y_out)
{
	if (s->aspectX == nullptr || s->aspectY == nullptr) return 0;
	const int32_t x = s->aspectX(), y = s->aspectY();
	if (x <= 0 || y <= 0) return 0;
	if (x_out != nullptr) *x_out = x;
	if (y_out != nullptr) *y_out = y;
	return 1;
}
int32_t ce_session_vsync_numerator(const ce_session *s) { return s->vsyncNum; }
int32_t ce_session_vsync_denominator(const ce_session *s) { return s->vsyncDen; }
int32_t ce_session_samples_per_frame(const ce_session *s) { return s->cfg.samplesPerFrame; }
int32_t ce_session_channels(const ce_session *s) { return s->cfg.channels; }
/* A machine a GPU drew is not the deterministic one, whatever its config says:
 * the GPU is outside the sandbox, outside the savestate, and different on every
 * machine it runs on. */
int32_t ce_session_deterministic(const ce_session *s)
{
	return (s->cfg.deterministic && !s->gpuDrew) ? 1 : 0;
}

/* Whether a GPU outside the sandbox drew this session's pictures. A movie
 * records it, so a replay that desyncs elsewhere can be understood. */
int32_t ce_session_gpu_drew(const ce_session *s) { return s->gpuDrew ? 1 : 0; }
int64_t ce_session_button_count(const ce_session *s) { return static_cast<int64_t>(s->cfg.buttons.size()); }

const char *ce_session_button_name(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->cfg.buttons.size())) return nullptr;
	return s->cfg.buttons[static_cast<size_t>(index)].c_str();
}

/* Whether a declared control is one this machine HAS. A package declares the
 * union of every peripheral its ports can hold; the project decides which of
 * them are plugged in, and the core is the only thing that knows. A control
 * that is not active is not in the frontend's controller, not a column in
 * TAStudio, and not a character in a movie entry - but its index on the wire
 * never moves, so nothing else has to care. */
/* The drive lights. Names are settled at load - a machine does not grow a
 * drive - and the light itself is asked every frame. */
const char *ce_session_game_settings(const ce_session *s)
{
	/* in the language in use when asked, which may have changed since the load */
	s->gameSettingsText = s->texts.texts(s->gameSettings);
	return s->gameSettingsText.c_str();
}

int32_t ce_session_drive_count(const ce_session *s)
{
	return static_cast<int32_t>(s->driveNames.size());
}

const char *ce_session_drive_name(const ce_session *s, int32_t index)
{
	if (index < 0 || static_cast<size_t>(index) >= s->driveNames.size()) return nullptr;
	return s->driveNames[static_cast<size_t>(index)].c_str();
}

int32_t ce_session_drive_light(const ce_session *s, int32_t index)
{
	if (s->driveLight == nullptr || index < 0
		|| static_cast<size_t>(index) >= s->driveNames.size()) return 0;
	return s->driveLight(index) != 0 ? 1 : 0;
}

int32_t ce_session_drive_media_count(const ce_session *s, int32_t index)
{
	if (s == nullptr || index < 0 || static_cast<size_t>(index) >= s->driveMedia.size()) return 0;
	return static_cast<int32_t>(s->driveMedia[static_cast<size_t>(index)].size());
}

const char *ce_session_drive_media_name(const ce_session *s, int32_t index, int32_t media)
{
	if (media < 0 || media >= ce_session_drive_media_count(s, index)) return nullptr;
	return s->driveMedia[static_cast<size_t>(index)][static_cast<size_t>(media)].c_str();
}

/* What the guest says, held to the list it declared: an index the list does not
 * have would be a name the frontend cannot show. */
int32_t ce_session_drive_media_selected(const ce_session *s, int32_t index)
{
	const int32_t held = ce_session_drive_media_count(s, index);
	if (held == 0 || s->driveMediaSelected == nullptr) return 0;
	const int32_t at = s->driveMediaSelected(index);
	return at < 0 ? 0 : at >= held ? held - 1 : at;
}

int32_t ce_session_drive_media_inserted(const ce_session *s, int32_t index)
{
	const int32_t held = ce_session_drive_media_count(s, index);
	if (held == 0 || s->driveMediaInserted == nullptr) return -1;
	const int32_t at = s->driveMediaInserted(index);
	return at < 0 || at >= held ? -1 : at;
}

int32_t ce_session_button_active(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->buttonActive.size())) return 0;
	return s->buttonActive[static_cast<size_t>(index)];
}

int64_t ce_session_axis_count(const ce_session *s) { return static_cast<int64_t>(s->cfg.axes.size()); }

int32_t ce_session_axis_active(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->axisActive.size())) return 0;
	return s->axisActive[static_cast<size_t>(index)];
}

const char *ce_session_button_label(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->buttonLabels.size())) return "";
	return s->buttonLabels[static_cast<size_t>(index)].c_str();
}

const char *ce_session_axis_label(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->axisLabels.size())) return "";
	return s->axisLabels[static_cast<size_t>(index)].c_str();
}

const char *ce_session_button_mnemonics(const ce_session *s)
{
	return s->cfg.mnemonics.c_str();
}

int32_t ce_session_mnemonic_of(const ce_session *s, const char *name)
{
	return name != nullptr ? s->cfg.names.mnemonicOf(name) : '?';
}

const char *ce_session_button_header_of(const ce_session *s, const char *name)
{
	static thread_local chimera::ThreadString held;   /* never destroyed: see thread_string.hpp */
	*held = name != nullptr ? s->cfg.names.buttonHeaderOf(name) : std::string();
	return held->c_str();
}

const char *ce_session_axis_header_of(const ce_session *s, const char *name)
{
	static thread_local chimera::ThreadString held;   /* never destroyed: see thread_string.hpp */
	*held = name != nullptr ? s->cfg.names.axisHeaderOf(name) : std::string();
	return held->c_str();
}

int32_t ce_session_player_of(const ce_session *s, const char *name)
{
	return name != nullptr ? s->cfg.names.playerOf(name) : 0;
}

const char *ce_session_system_name(const ce_session *s)
{
	return s->cfg.systemName.c_str();
}

int32_t ce_control_mnemonic(const char *name)
{
	return name != nullptr ? chimera::genericMnemonic(name) : '?';
}

const char *ce_control_button_header(const char *name)
{
	static thread_local chimera::ThreadString held;   /* never destroyed: see thread_string.hpp */
	*held = name != nullptr ? std::string(1, chimera::genericMnemonic(name)) : std::string();
	return held->c_str();
}

const char *ce_control_axis_header(const char *name)
{
	static thread_local chimera::ThreadString held;   /* never destroyed: see thread_string.hpp */
	*held = name != nullptr ? chimera::genericAxisHeader(name) : std::string();
	return held->c_str();
}

const char *ce_session_axis_name(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->cfg.axes.size())) return nullptr;
	return s->cfg.axes[static_cast<size_t>(index)].name.c_str();
}

int32_t ce_session_axis_neutral(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->cfg.axes.size())) return 0;
	return s->cfg.axes[static_cast<size_t>(index)].neutral;
}

/* The index calls below check the index against what the session knows
 * before the core sees it: a core indexes its own arrays without a check, and
 * a stale index (a tool window holding one across a reboot) made it write
 * outside them, or hand back a pointer the host then read (engine analysis,
 * 2026-10-18). */
void ce_session_set_axis(ce_session *s, int32_t index, int32_t value)
{
	if (s->setAxis != nullptr && index >= 0 && static_cast<size_t>(index) < s->cfg.axes.size()) s->setAxis(index, value);
}

void ce_session_set_button(ce_session *s, int32_t index, int32_t pressed)
{
	if (index >= 0 && static_cast<size_t>(index) < s->btnState.size())
	{
		s->btnState[static_cast<size_t>(index)] = pressed != 0 ? 1 : 0;
	}
}

int32_t ce_session_frame_advance(ce_session *s, uint64_t buttons, int32_t render)
{
	/* -1: the guest is dead (ce_session_guest_death says why); nothing ran */
	if (s->guestDied()) return -1;
	s->wantRendering(render != 0 ? 1 : 0);
	s->runFrame(s->sendButtons(s->computeEffective(buttons)));
	if (s->guestDied())
	{
		ce_gl_release();
		return -1;
	}
	if (render != 0) s->copyVideo();
	/* after every frame, a seek's too: a frame's length is the rate the core
	 * says after it, which the window adds up (Display Real Time) */
	s->readRate();
	/* asked before the frame is counted: the core can die in it (advanceCore) */
	const int32_t lag = s->inputWasRead != nullptr && s->inputWasRead() == 0 ? 1 : 0;
	/* a core that reports its own count may produce a different number every
	 * frame (blip resamplers do); the declared samplesPerFrame is the buffer
	 * we must not overrun */
	int32_t nsamp = s->getAudioSampleCount != nullptr ? s->getAudioSampleCount() : s->cfg.samplesPerFrame;
	if (nsamp < 0) nsamp = 0;
	if (nsamp > s->cfg.samplesPerFrame) nsamp = s->cfg.samplesPerFrame;
	const auto *src = reinterpret_cast<const int16_t *>(s->getAudio());
	if (src == nullptr) nsamp = 0; /* no sound set up yet: silence, not a read of address 0 */
	if (s->cfg.channels == 2)
	{
		if (nsamp > 0) std::memcpy(s->audioBuf.data(), src, static_cast<size_t>(nsamp) * 2 * sizeof(int16_t));
	}
	else
	{
		for (int32_t i = 0; i < nsamp; i++)
		{
			s->audioBuf[static_cast<size_t>(i) * 2] = src[i];
			s->audioBuf[static_cast<size_t>(i) * 2 + 1] = src[i];
		}
	}
	s->sampleCount = nsamp;
	s->audioSerial++;
	if (s->guestDied())   /* in the readback, the audio or the input question */
	{
		ce_gl_release();
		return -1;
	}
	s->afterFrame(render != 0, -1);
	/* The frame is over and the caller draws next: whatever GL context the
	 * bridge borrowed goes back before it does. */
	ce_gl_release();
	s->trace(lag, render);
	return lag;
}

const uint32_t *ce_session_video(const ce_session *s) { return s->videoBuf.data(); }

void ce_session_draw_every_frame(ce_session *s, int32_t on) { s->drawAlways = on != 0 ? 1 : 0; }

int32_t ce_session_video_width(const ce_session *s) { return s->vidW; }
int32_t ce_session_video_height(const ce_session *s) { return s->vidH; }

const int16_t *ce_session_audio(const ce_session *s, int32_t *sample_count)
{
	if (sample_count != nullptr) *sample_count = s->sampleCount;
	return s->audioBuf.data();
}

int32_t ce_session_audio_rate(const ce_session *s) { return s->cfg.audioRate; }

const int16_t *ce_session_sound(ce_session *s, int32_t *pairs)
{
	if (s->cfg.audioRate == 44100) return ce_session_audio(s, pairs);
	if (s->soundSerial != s->audioSerial)
	{
		/* the frames in between were not asked for (a seek, turbo) or are
		 * another machine's: the filter starts again, rather than join
		 * sound that never followed this */
		if (s->soundGap || s->soundSerial + 1 != s->audioSerial) s->resampler.reset();
		s->soundGap = false;
		s->soundBuf.clear();
		s->resampler.process(s->audioBuf.data(), static_cast<size_t>(s->sampleCount), s->soundBuf);
		s->soundSerial = s->audioSerial;
	}
	if (pairs != nullptr) *pairs = static_cast<int32_t>(s->soundBuf.size() / 2);
	return s->soundBuf.data();
}

/* What every load owes the machine afterwards, however the state arrived. */
static void afterStateLoaded(ce_session *s)
{
	s->lostMachine.clear();   /* a whole machine is back */
	s->soundGap = true;       /* and its sound does not follow the last frame's */
	/* what the core derived from the memory that was just replaced goes first:
	 * nothing below may run guest code on a cache of the old machine */
	if (s->stateLoaded != nullptr) s->stateLoaded();
	/* a savestate is guest memory, and the guest's wide-input latches are
	 * guest memory too: the load just rewrote what the guest believes is
	 * held, so the delta tracker must forget its history and resend every
	 * button's current state on the next advance */
	std::fill(s->btnSent.begin(), s->btnSent.end(), uint8_t{ 0xFF });
	/* the turbo flag is host policy rather than machine state and belongs in
	 * memory the state does not cover - but a core that kept it in ordinary
	 * memory would have just had it rewritten, so forget what we told it */
	s->renderingSent = -1;

	/* a savestate is guest memory, and the guest's "tracing on" flag is guest
	 * memory too: re-assert the desired flag, and discard whatever lines the
	 * restored buffer holds - they were traced before the load and would
	 * appear out of order */
	if (s->traceSetEnabled != nullptr)
	{
		s->traceSetEnabled(s->traceDesired ? 1 : 0);
		if (s->traceClear != nullptr) s->traceClear();
	}
	/* where the machine is now is the caller's to say (a capture will), and
	 * what it draws next is not to be kept for a while (chimera#190) */
	s->afterLoad(-1);
}

const uint8_t *ce_session_save_state(ce_session *s, uint64_t *len_out)
{
	s->error.clear();
	s->stateBuf.clear();
	/* A machine that died is part way through the frame it died in. Saved, it
	 * came back from the state as a live machine (a load clears the death). */
	if (s->guestDied())
	{
		s->error = tr("engine.core_stopped", {s->death});
		if (len_out != nullptr) *len_out = 0;
		return nullptr;
	}
	chimera::WbxReturn r{};
	if (!s->stateSaving())   /* it died telling the core a state is coming */
	{
		s->error = tr("engine.core_stopped", {s->death});
		if (len_out != nullptr) *len_out = 0;
		return nullptr;
	}
	/* no deactivate/activate bracket: the host activates itself for the
	 * duration - bracketing costs four full guest-address-space remaps per
	 * state, which at rewind's one state per frame was a 10x slowdown */
	/* Sized to the state before it is written: grown by doubling, a state of
	 * N bytes was copied once more on the way and the buffer kept up to 2N for
	 * the rest of the session, which on Windows is committed memory (engine
	 * analysis, 2026-10-18). One much bigger than this state is let go. */
	if (s->host->wbx_state_size != nullptr)
	{
		chimera::WbxReturn sr{};
		s->host->wbx_state_size(s->obj, &sr);
		if (sr.ok() && sr.data > 0)
		{
			const size_t want = static_cast<size_t>(sr.data);
			if (s->stateBuf.capacity() > want * 2) std::vector<uint8_t>().swap(s->stateBuf);
			try { s->stateBuf.reserve(want); } catch (const std::bad_alloc &) {}
		}
	}
	VectorSink sink{ &s->stateBuf };
	s->host->wbx_save_state(s->obj, vectorWrite, reinterpret_cast<uintptr_t>(&sink), &r);
	if (!r.ok())
	{
		s->error = sink.outOfMemory ? tr("engine.no_memory_save") : std::string(r.errorMessage);
		return nullptr;
	}
	if (len_out != nullptr) *len_out = s->stateBuf.size();
	if (s->traceEvery > 0)
	{
		fprintf(stderr, "[trace] save state: %zu bytes at frame %lld\n", s->stateBuf.size(), (long long)s->traceFrame);
		fflush(stderr);
	}
	return s->stateBuf.data();
}

static int32_t loadState(ce_session *s, const uint8_t *data, uint64_t len)
{
	s->error.clear();
	/* a load rewrites the machine the history may still be copying an anchor of
	 * in the background (a branch loaded right after one was taken) */
	s->history.prepareForLoad();
	ByteStream stream{ data, len };
	chimera::WbxReturn r{};
	s->host->wbx_load_state(s->obj, streamRead, reinterpret_cast<uintptr_t>(&stream), &r); // see save re: no bracket
	ce_gl_release(); /* a restore can run guest code, and guest code can draw */
	/* A load the sandbox refused before it touched memory (a state of another
	 * build or another boot) changed nothing: the machine is the one it was,
	 * still past an edit if it was, and its GPU objects are its own. It was
	 * handled as a load all the same - the history forgot the edit and every
	 * GPU core was told to rebuild (engine analysis, 2026-10-18). One that
	 * failed after it began left a dead machine, which is a change. */
	if (r.ok() || s->guestDied())
	{
		s->history.beforeLoad();
		ce_gl_state_loaded(-1); /* which frame a state from outside belongs to is not known here */
		s->vulkanStateLoaded();
	}
	if (s->traceEvery > 0)
	{
		fprintf(stderr, "[trace] load state: %llu bytes, %s, machine now: threads %d running %d digest %016llx\n",
			(unsigned long long)len, r.ok() ? "ok" : r.errorMessage,
			s->traceThreads != nullptr ? s->traceThreads() : -1,
			s->traceRunning != nullptr ? s->traceRunning() : -1,
			(unsigned long long)(s->traceDigest != nullptr ? s->traceDigest() : 0ull));
		fflush(stderr);
	}
	if (!r.ok())
	{
		s->error = r.errorMessage;
		return 1;
	}
	afterStateLoaded(s);
	ce_gl_release(); /* StateLoaded is guest code too, and may have drawn */
	return 0;
}

int32_t ce_session_load_state(ce_session *s, const uint8_t *data, uint64_t len)
{
	try
	{
		return loadState(s, data, len);
	}
	catch (const std::bad_alloc &)
	{
		s->error = tr("engine.no_memory_load");
		return 1;
	}
}

/* ---- a state kept in a file (engine.h, "A state kept in a file") ----------
 *
 * The sandbox saves and loads a state through a write and a read callback, so
 * the state never has to exist whole anywhere: it goes from the machine through
 * zstd to the file, and back the same way. That is the whole reason this exists
 * - a PS3's state is past 2 GiB, which is more than a frontend array can be. */
namespace
{
constexpr size_t STATE_FILE_CHUNK = size_t{ 1 } << 20;

/* how often a save or a load says how far it is: often enough for a bar to move,
 * rarely enough that a small machine's state is one report */
constexpr uint64_t STATE_FILE_REPORT_EVERY = uint64_t{ 32 } << 20;

struct StateFileSink
{
	chimera::FileWriter *out;
	const chimera::ZstdApi *z;
	void *zcs;
	std::vector<uint8_t> buf;
	uint64_t expected = 0; /* what the last state of this machine weighed, 0 when none has been taken */
	bool failed = false;
	uint64_t raw = 0, stored = 0, reportedAt = 0;

	bool feed(const void *src, size_t size, int endOp)
	{
		if (failed) return false;
		raw += size;
		if (raw - reportedAt >= STATE_FILE_REPORT_EVERY)
		{
			reportedAt = raw;
			/* a state is as big as it turns out to be; the last one is the best guess there is,
			 * and one that has been overtaken is an end nobody knows */
			chimera::progress(tr("engine.progress_saving").c_str(), raw, expected > raw ? expected : 0);
		}
		if (zcs == nullptr)
		{
			stored += size;
			return out->write(src, size) || !(failed = true);
		}
		chimera::ZstdApi::Buffer in{ src, size, 0 };
		for (;;)
		{
			chimera::ZstdApi::OutBuffer o{ buf.data(), buf.size(), 0 };
			const size_t left = z->compressStream2(zcs, &o, &in, endOp);
			if (z->isError(left) || !out->write(buf.data(), o.pos)) { failed = true; return false; }
			stored += o.pos;
			if (endOp == 0 ? in.pos == in.size : left == 0) return true;
		}
	}
};

/* miniBox reads a NEGATIVE answer as a failed write; 1 read as success, so a
 * save whose file could not be written went on to the end (engine analysis,
 * 2026-10-18). The sink remembers the failure for the caller too. */
extern "C" int32_t stateFileWrite(uintptr_t userdata, const void *src, uintptr_t size)
{
	return reinterpret_cast<StateFileSink *>(userdata)->feed(src, size, 0) ? 0 : -1;
}

struct StateFileSource
{
	chimera::FileReader *in;
	const chimera::ZstdApi *z;
	void *zds;
	std::vector<uint8_t> buf;
	uint64_t fileBytes = 0; /* the whole file, which is what a load knows the length of */
	size_t have = 0, pos = 0;
	bool failed = false;
	uint64_t raw = 0, consumed = 0, reportedAt = 0;
	/* What reading the header over-read. A FileReader cannot seek, and the
	 * header's length is not known until it is parsed, so the bytes past it are
	 * handed back here rather than read twice. */
	std::vector<uint8_t> pending;
	size_t pendingPos = 0;

	/* the file from where the header stopped, pending bytes first */
	uint64_t fill(uint8_t *dst, uint64_t max)
	{
		uint64_t got = 0;
		if (pendingPos < pending.size())
		{
			got = pending.size() - pendingPos;
			if (got > max) got = max;
			std::memcpy(dst, pending.data() + pendingPos, static_cast<size_t>(got));
			pendingPos += static_cast<size_t>(got);
			if (got == max) return got;
		}
		return got + in->read(dst + got, max - got);
	}

	void report()
	{
		if (consumed - reportedAt < STATE_FILE_REPORT_EVERY / 4) return;
		reportedAt = consumed;
		chimera::progress(tr("engine.progress_loading").c_str(), consumed, fileBytes);
	}

	size_t take(void *dst, size_t want)
	{
		if (zds == nullptr)
		{
			const size_t got = static_cast<size_t>(fill(static_cast<uint8_t *>(dst), want));
			raw += got; consumed += got; report();
			return got;
		}
		chimera::ZstdApi::OutBuffer o{ dst, want, 0 };
		while (o.pos < want && !failed)
		{
			if (pos == have)
			{
				have = static_cast<size_t>(fill(buf.data(), buf.size()));
				pos = 0;
				if (have == 0) break; /* the end of the file is the end of the state */
				consumed += have;
				report();
			}
			chimera::ZstdApi::Buffer i{ buf.data(), have, pos };
			const size_t rc = z->decompressStream(zds, &o, &i);
			if (z->isError(rc)) failed = true;
			else if (rc == 0) frameDone = true;
			pos = i.pos;
		}
		raw += o.pos;
		return o.pos;
	}

	/* After the load took what it needed: the rest of the frame decoded, so
	 * its checksum is checked - zstd checks it at the frame's end, which a load
	 * that stops reading never reaches (2026-10-18). */
	bool frameDone = false;
	bool verifyEnd()
	{
		if (zds == nullptr) return !failed;
		uint8_t sink[64];
		while (!frameDone && !failed)
		{
			if (pos == have)
			{
				have = static_cast<size_t>(fill(buf.data(), buf.size()));
				pos = 0;
				if (have == 0) return false;   /* cut short */
			}
			chimera::ZstdApi::OutBuffer o{ sink, sizeof sink, 0 };
			chimera::ZstdApi::Buffer i{ buf.data(), have, pos };
			const size_t rc = z->decompressStream(zds, &o, &i);
			if (z->isError(rc) || o.pos != 0) return false;   /* damaged, or more than was read */
			if (rc == 0) frameDone = true;
			pos = i.pos;
		}
		return frameDone && !failed;
	}
};

extern "C" intptr_t stateFileRead(uintptr_t userdata, void *dst, uintptr_t size)
{
	return static_cast<intptr_t>(reinterpret_cast<StateFileSource *>(userdata)->take(dst, size));
}
} // namespace

int32_t ce_session_state_save_file(ce_session *s, const char *utf8_path, const uint8_t *tag, uint32_t tag_len)
{
	s->error.clear();
	if (utf8_path == nullptr || (tag == nullptr && tag_len != 0)) { s->error = tr("engine.no_state_path"); return 1; }
	/* as ce_session_save_state: a dead machine is no state to keep */
	if (s->guestDied()) { s->error = tr("engine.core_stopped", {s->death}); return 1; }
	chimera::FileWriter out;
	if (!out.open(utf8_path)) { s->error = tr("engine.could_not_create", {utf8_path}); return 1; }

	/* Level 1 unless somebody measuring says otherwise: CHIMERA_STATE_RAW=1 writes the state as
	 * it is, CHIMERA_STATE_ZSTD_LEVEL picks another level. They exist so the cost of compressing
	 * can be told apart from the cost of the state itself. */
	const char *rawEnv = std::getenv("CHIMERA_STATE_RAW");
	const char *levelEnv = std::getenv("CHIMERA_STATE_ZSTD_LEVEL");
	const bool wantRaw = rawEnv != nullptr && rawEnv[0] == '1';
	const int level = levelEnv != nullptr && std::atoi(levelEnv) != 0 ? std::atoi(levelEnv) : 1;
	const char *why = nullptr;
	const chimera::ZstdApi *z = wantRaw ? nullptr : chimera::zstdApi(&why); /* without it the file is simply bigger */
	void *zcs = z != nullptr && z->createCStream != nullptr ? z->createCStream() : nullptr;
	if (zcs != nullptr && z->isError(z->initCStream(zcs, level))) { z->freeCStream(zcs); zcs = nullptr; }
	chimera::zstdChecksum(z, zcs);

	/* The header says which savestate format this is and which build wrote it,
	 * so a load can refuse by name rather than hand the machine bytes it cannot
	 * read (issue #115, state_format.hpp). */
	chimera::StateFileHeader head;
	head.compressed = zcs != nullptr;
	head.writer = chimera::stateWriterId();
	if (tag != nullptr && tag_len != 0) head.tag.assign(tag, tag + tag_len);
	std::vector<uint8_t> headBytes;
	chimera::writeStateFileHeader(head, headBytes);
	out.write(headBytes.data(), headBytes.size());

	StateFileSink sink{ &out, z, zcs, std::vector<uint8_t>(STATE_FILE_CHUNK), s->stateFileRawBytes };
	chimera::WbxReturn r{};
	if (!s->stateSaving())
	{
		if (zcs != nullptr) z->freeCStream(zcs);
		s->error = tr("engine.core_stopped", {s->death});
		return 1;
	}
	s->host->wbx_save_state(s->obj, stateFileWrite, reinterpret_cast<uintptr_t>(&sink), &r); // see ce_session_save_state re: no bracket
	const bool flushed = sink.feed(nullptr, 0, 2) || zcs == nullptr;
	if (zcs != nullptr) z->freeCStream(zcs);

	if (!r.ok()) { s->error = r.errorMessage; return 1; }
	/* on the disk before it takes the name: a branch is the user's work, and a
	 * power cut must not leave half a state under it (2026-10-18) */
	if (sink.failed || !flushed || !out.commit(true))
	{
		s->error = tr("engine.disk_full", {utf8_path});
		return 1;
	}
	s->stateFileRawBytes = sink.raw;
	s->stateFileStoredBytes = sink.stored;
	return 0;
}

void ce_session_state_file_bytes(const ce_session *s, uint64_t *raw_out, uint64_t *stored_out)
{
	if (raw_out != nullptr) *raw_out = s->stateFileRawBytes;
	if (stored_out != nullptr) *stored_out = s->stateFileStoredBytes;
}

int32_t ce_session_state_load_file(ce_session *s, const char *utf8_path, uint8_t *tag_out, uint32_t tag_cap, uint32_t *tag_len_out)
{
	s->error.clear();
	if (tag_len_out != nullptr) *tag_len_out = 0;
	chimera::FileReader in;
	if (utf8_path == nullptr || !in.open(utf8_path))
	{
		s->error = tr("engine.not_state_file", {utf8_path != nullptr ? utf8_path : tr("engine.no_path")});
		return 2; /* the machine was not touched */
	}

	/* The header, read in growing prefixes because its length is in it (a tag
	 * is a handful of bytes in practice, so the first read is nearly always the
	 * only one) and a FileReader cannot seek back. */
	chimera::StateFileHeader head;
	std::vector<uint8_t> prefix;
	std::string why;
	int headRc = 2;
	for (size_t want = 8192;; want *= 4)
	{
		const size_t was = prefix.size();
		prefix.resize(want);
		const uint64_t got = in.read(prefix.data() + was, want - was);
		prefix.resize(was + static_cast<size_t>(got));
		headRc = chimera::readStateFileHeader(prefix.data(), prefix.size(), head, why);
		/* a short read means the file ended: nothing more is coming */
		if (headRc != 2 || prefix.size() < want || want >= (2u << 20)) break;
	}
	if (headRc != 0)
	{
		/* A format disagreement is a whole sentence about two builds and reads as
		 * one; a damaged or foreign file is about THIS file, and wants naming. */
		s->error = headRc == 1 ? why : std::string(utf8_path) + " " + why;
		return 2; /* another format, or not a state file: either way, untouched */
	}
	const uint8_t compressed = head.compressed ? 1 : 0;

	const char *zwhy = nullptr;
	const chimera::ZstdApi *z = compressed != 0 ? chimera::zstdApi(&zwhy) : nullptr;
	void *zds = z != nullptr ? z->createDStream() : nullptr;
	if (compressed != 0 && (zds == nullptr || z->isError(z->initDStream(zds))))
	{
		if (zds != nullptr) z->freeDStream(zds);
		s->error = zwhy != nullptr ? tr("engine.no_zstd_why", {zwhy}) : tr("engine.no_zstd");
		return 2;
	}

	s->history.prepareForLoad(); /* see ce_session_load_state */
	StateFileSource source{ &in, z, zds, std::vector<uint8_t>(STATE_FILE_CHUNK) };
	source.pending.assign(prefix.begin() + static_cast<std::ptrdiff_t>(head.length), prefix.end());
	{
		uint64_t size = 0;
		int64_t mtime = 0;
		if (chimera::fileStamp(utf8_path, &size, &mtime)) source.fileBytes = size;
	}
	chimera::WbxReturn r{};
	s->host->wbx_load_state(s->obj, stateFileRead, reinterpret_cast<uintptr_t>(&source), &r);
	/* loaded, but is it what was saved: the checksum at the frame's end */
	const bool intact = !r.ok() || source.verifyEnd();
	if (zds != nullptr) z->freeDStream(zds);
	ce_gl_release();
	if (r.ok() || s->guestDied())   /* as ce_session_load_state */
	{
		s->history.beforeLoad();
		ce_gl_state_loaded(-1);
		s->vulkanStateLoaded();
	}
	if (!intact)
	{
		/* the machine took the damaged bytes: not one to play on */
		s->lostMachine = tr("engine.state_damaged_load_another", {utf8_path});
		s->error = s->lostMachine;
		return 1;
	}
	if (!r.ok() || source.failed)
	{
		/* a frame that does not decode, or whose checksum does not match, is the
		 * reason - the sandbox only saw its state end early */
		s->error = source.failed ? tr("engine.state_damaged", {utf8_path})
		                         : std::string(r.errorMessage);
		return 1;
	}
	afterStateLoaded(s);
	ce_gl_release(); /* StateLoaded is guest code too, and may have drawn */
	s->stateFileRawBytes = source.raw;
	s->stateFileStoredBytes = source.fileBytes;

	const uint32_t tagLen = static_cast<uint32_t>(head.tag.size());
	if (tag_len_out != nullptr) *tag_len_out = tagLen;
	if (tag_out != nullptr && tagLen != 0) std::memcpy(tag_out, head.tag.data(), tagLen < tag_cap ? tagLen : tag_cap);
	return 0;
}

int32_t ce_session_domain_count(const ce_session *s) { return s->mdCount(); }

static bool domainIndexOk(const ce_session *s, int32_t index) { return index >= 0 && index < s->mdCount(); }

const char *ce_session_domain_name(const ce_session *s, int32_t index)
{
	return domainIndexOk(s, index) ? reinterpret_cast<const char *>(s->mdName(index)) : nullptr;
}

int64_t ce_session_domain_size(const ce_session *s, int32_t index) { return domainIndexOk(s, index) ? s->mdSize(index) : 0; }

int32_t ce_session_domain_writable(const ce_session *s, int32_t index) { return domainIndexOk(s, index) ? s->mdWritable(index) : 0; }

int32_t ce_session_domain_hidden(const ce_session *s, int32_t index)
{
	return s->mdHidden != nullptr && index >= 0 && index < s->mdCount() && s->mdHidden(index) != 0 ? 1 : 0;
}

int64_t ce_session_domain_read(const ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len)
{
	if (!domainIndexOk(s, index)) return 0;
	int64_t size = s->mdSize(index);
	if (offset < 0 || offset >= size || len <= 0) return 0;
	int64_t n = len < size - offset ? len : size - offset;
	const uint8_t *src = domainReadView(s, index, offset, n);
	if (src == nullptr) return 0;
	std::memcpy(buf, src, static_cast<size_t>(n));
	return n;
}

const char *ce_session_last_error(ce_session *s) { return s->error.c_str(); }

const char *ce_session_guest_death(ce_session *s)
{
	if (s == nullptr) return nullptr;
	return s->guestDied() ? s->death.c_str() : nullptr;
}

const char *ce_host_build_info(void)
{
	const char *error = nullptr;
	const chimera::HostApi *host = chimera::hostApi(&error);
	return host != nullptr ? host->wbx_build_info() : nullptr;
}

const char *ce_session_property_table(const ce_session *s) { return s->properties.describe().c_str(); }

/* A dynamic table's property is where the core says it is NOW: asked by name
 * before every use, because the thing it names (a variable on a heap) moves
 * when its table grows and is gone when its object dies. One call into the
 * core; the property keeps its index either way. */
void ce_session::placeProperty(int32_t index)
{
	if (!properties.dynamic() || gameProperty == nullptr) return;
	if (index < 0 || static_cast<size_t>(index) >= properties.all().size()) return;
	const std::string name = properties.all()[static_cast<size_t>(index)].name;
	properties.place(name, reinterpret_cast<const char *>(gameProperty(name.c_str())));
}

int32_t ce_session_property_dynamic(const ce_session *s) { return s->properties.dynamic() ? 1 : 0; }

int32_t ce_session_property_refresh(ce_session *s)
{
	if (!s->properties.dynamic() || s->gameProperties == nullptr) return static_cast<int32_t>(s->properties.all().size());
	return s->properties.relist(reinterpret_cast<const char *>(s->gameProperties()));
}

int64_t ce_session_property_offset(ce_session *s, int32_t index, uint32_t element)
{
	s->placeProperty(index);
	const auto &all = s->properties.all();
	if (index < 0 || static_cast<size_t>(index) >= all.size()) return -1;
	const CeGameProperties::Property &p = all[static_cast<size_t>(index)];
	return p.present && element < p.count ? p.elementOffset(element) : -1;
}

int32_t ce_session_game_time_ms(ce_session *s, int64_t *ms_out)
{
	if (ms_out != nullptr) *ms_out = 0;
	CeGameProperties::Value v;
	if (s->properties.timer() < 0 || !s->properties.read(s->properties.timer(), 0, v)) return 0;
	if (ms_out != nullptr) *ms_out = v.kind == CeGameProperties::Value::UInt ? int64_t(v.u) : v.i;
	return 1;
}

int32_t ce_game_time_text(int64_t ms, char *buf, int32_t cap)
{
	const std::string text = CeGameProperties::timeText(ms);
	if (buf != nullptr && cap > 0)
	{
		const size_t n = std::min(text.size(), size_t(cap - 1));
		std::memcpy(buf, text.data(), n);
		buf[n] = '\0';
	}
	return int32_t(text.size());
}

int32_t ce_session_property_find(ce_session *s, const char *name, uint32_t *element_out)
{
	if (name == nullptr) return -1;
	int32_t index = s->properties.find(name, element_out);
	/* a dynamic table may not have listed it yet, or ever: the core is asked */
	if (index < 0 && s->properties.dynamic() && s->gameProperty != nullptr)
	{
		index = s->properties.place(name, reinterpret_cast<const char *>(s->gameProperty(name)));
		if (index >= 0 && !s->properties.all()[static_cast<size_t>(index)].present) index = -1;
		if (element_out != nullptr) *element_out = 0;
	}
	return index;
}

int32_t ce_session_property_at(const ce_session *s, const char *domain, int64_t address, uint32_t *element_out, int32_t *starts_out)
{
	bool starts = false;
	const int32_t index = domain != nullptr ? s->properties.at(domain, address, element_out, &starts) : -1;
	if (starts_out != nullptr) *starts_out = starts ? 1 : 0;
	return index;
}

int32_t ce_session_property_get(ce_session *s, int32_t index, uint32_t element, ce_property_value *out)
{
	s->error.clear();
	s->placeProperty(index);
	CeGameProperties::Value &v = s->propertyValue;
	if (out == nullptr || !s->properties.read(index, element, v))
	{
		s->error = tr("engine.no_such_property");
		return 1;
	}
	*out = ce_property_value{};
	out->kind = int32_t(v.kind);
	out->i = v.i;
	out->u = v.u;
	out->f = v.f;
	out->data = v.data.data();
	out->len = int64_t(v.data.size());
	return 0;
}

int32_t ce_session_property_set(ce_session *s, int32_t index, uint32_t element, const ce_property_value *in)
{
	s->error.clear();
	if (in == nullptr || in->kind < CE_PROPERTY_INT || in->kind > CE_PROPERTY_BYTES)
	{
		s->error = tr("engine.value_of_no_kind");
		return 1;
	}
	CeGameProperties::Value v;
	v.kind = CeGameProperties::Value::Kind(in->kind);
	v.i = in->i;
	v.u = in->u;
	v.f = in->f;
	if (in->data != nullptr && in->len > 0) v.data.assign(in->data, size_t(in->len));
	s->placeProperty(index);
	return s->properties.write(index, element, v, s->error) ? 0 : 1;
}

int32_t ce_session_property_text(ce_session *s, int32_t index, uint32_t element, int32_t named, char *buf, int32_t cap)
{
	if (index < 0 || size_t(index) >= s->properties.all().size() || element >= s->properties.all()[size_t(index)].count) return -1;
	s->placeProperty(index);
	const std::string text = s->properties.text(index, element, named != 0);
	if (buf != nullptr && cap > 0)
	{
		const size_t n = std::min(text.size(), size_t(cap - 1));
		std::memcpy(buf, text.data(), n);
		buf[n] = '\0';
	}
	return int32_t(text.size());
}

int32_t ce_session_property_set_text(ce_session *s, int32_t index, uint32_t element, const char *text)
{
	s->error.clear();
	s->placeProperty(index);
	return s->properties.writeText(index, element, text != nullptr ? text : "", s->error) ? 0 : 1;
}

uint64_t ce_session_domain_ptr(const ce_session *s, int32_t index)
{
	return domainIndexOk(s, index) ? static_cast<uint64_t>(s->mdPtr(index)) : 0;
}

uint64_t ce_session_domain_read_ptr(const ce_session *s, int32_t index)
{
	if (!domainIndexOk(s, index)) return 0;
	return reinterpret_cast<uintptr_t>(domainReadView(s, index, 0, s->mdSize(index)));
}

int32_t ce_session_apply_settings(ce_session *s, const char *overrides_json)
{
	s->error.clear();
	std::string err;
	auto capacity = reinterpret_cast<int32_t (*)()>(s->proc("GetSettingsCapacity", 0, false, err));
	auto buffer = reinterpret_cast<uintptr_t (*)()>(s->proc("GetSettingsBuffer", 0, false, err));
	auto apply = reinterpret_cast<void (*)(int32_t)>(s->proc("PutSettings", 1, false, err));
	if (capacity == nullptr || buffer == nullptr || apply == nullptr) return 1;

	std::string json;
	if (!composeSettings(s->cfg.defaultsJson, overrides_json, json, s->error)) return 2;
	int32_t cap = capacity();
	if (static_cast<int32_t>(json.size()) > cap)
	{
		s->error = tr("engine.settings_too_big", {s->cfg.coreName, std::to_string(json.size()), std::to_string(cap)});
		return 2;
	}
	uintptr_t dest = buffer();
	if (dest == 0) return 1;
	std::memcpy(reinterpret_cast<void *>(dest), json.data(), json.size());
	apply(static_cast<int32_t>(json.size()));
	return 0;
}

int32_t ce_session_surface_count(const ce_session *s)
{
	return static_cast<int32_t>(s->surfaceNames.size());
}

const char *ce_session_surface_name(const ce_session *s, int32_t index)
{
	if (index < 0 || index >= static_cast<int32_t>(s->surfaceNames.size())) return nullptr;
	return s->surfaceNames[static_cast<size_t>(index)].c_str();
}

int32_t ce_session_surface_width(const ce_session *s, int32_t index)
{
	return index >= 0 && index < static_cast<int32_t>(s->surfaceWidths.size()) ? s->surfaceWidths[static_cast<size_t>(index)] : 0;
}

int32_t ce_session_surface_height(const ce_session *s, int32_t index)
{
	return index >= 0 && index < static_cast<int32_t>(s->surfaceHeights.size()) ? s->surfaceHeights[static_cast<size_t>(index)] : 0;
}

const uint32_t *ce_session_surface_render(ce_session *s, int32_t index)
{
	if (s->renderSurface == nullptr || index < 0 || index >= static_cast<int32_t>(s->surfaceBufs.size())) return nullptr;
	uintptr_t p = s->renderSurface(index);
	if (p == 0) return nullptr;
	auto &buf = s->surfaceBufs[static_cast<size_t>(index)];
	std::memcpy(buf.data(), reinterpret_cast<const void *>(p), buf.size() * sizeof(uint32_t));
	return buf.data();
}

int32_t ce_session_register_count(const ce_session *s)
{
	return static_cast<int32_t>(s->regNames.size());
}

const char *ce_session_register_name(const ce_session *s, int32_t index)
{
	if (index < 0 || index >= static_cast<int32_t>(s->regNames.size())) return nullptr;
	return s->regNames[static_cast<size_t>(index)].c_str();
}

int32_t ce_session_register_bits(const ce_session *s, int32_t index)
{
	return index >= 0 && index < static_cast<int32_t>(s->regBits.size()) ? s->regBits[static_cast<size_t>(index)] : 32;
}

int64_t ce_session_register_value(const ce_session *s, int32_t index)
{
	return s->regValue != nullptr && index >= 0 && static_cast<size_t>(index) < s->regNames.size() ? s->regValue(index) : 0;
}

int32_t ce_session_register_set(ce_session *s, int32_t index, int64_t value)
{
	if (s->regSet == nullptr || index < 0 || static_cast<size_t>(index) >= s->regNames.size()) return 1;
	s->regSet(index, value);
	return 0;
}

int32_t ce_session_has_executed_cycles(const ce_session *s) { return s->executedCycles != nullptr ? 1 : 0; }

int64_t ce_session_executed_cycles(const ce_session *s)
{
	return s->executedCycles != nullptr ? s->executedCycles() : 0;
}

int32_t ce_session_bus_count(const ce_session *s) { return static_cast<int32_t>(s->busNames.size()); }

const char *ce_session_bus_name(const ce_session *s, int32_t index)
{
	if (index < 0 || index >= static_cast<int32_t>(s->busNames.size())) return nullptr;
	return s->busNames[static_cast<size_t>(index)].c_str();
}

int64_t ce_session_bus_size(const ce_session *s, int32_t index)
{
	return index >= 0 && index < static_cast<int32_t>(s->busSizes.size()) ? s->busSizes[static_cast<size_t>(index)] : 0;
}

int32_t ce_session_bus_writable(const ce_session *s, int32_t index)
{
	return index >= 0 && index < static_cast<int32_t>(s->busWritables.size()) && s->busWritables[static_cast<size_t>(index)] ? 1 : 0;
}

int32_t ce_session_bus_peek(const ce_session *s, int32_t index, int32_t addr)
{
	return s->busPeek != nullptr && index >= 0 && static_cast<size_t>(index) < s->busNames.size() ? s->busPeek(index, addr) : 0;
}

int64_t ce_session_bus_read(const ce_session *s, int32_t index, int64_t addr, uint8_t *buf, int64_t len)
{
	if (buf == nullptr || len <= 0) return 0;
	if (s->busPeek == nullptr || index < 0 || index >= static_cast<int32_t>(s->busSizes.size()))
	{
		std::memset(buf, 0, static_cast<size_t>(len));
		return 0;
	}
	const int64_t size = s->busSizes[static_cast<size_t>(index)];
	int64_t done = 0;
	while (done < len)
	{
		const int64_t at = addr + done;
		int64_t n = std::min<int64_t>(len - done, CE_BUS_READ_CHUNK);
		if (at < 0 || at >= size)
		{
			/* outside the bus: zeros, up to where the bus begins if it is ahead */
			if (at < 0) n = std::min<int64_t>(n, -at);
			std::memset(buf + done, 0, static_cast<size_t>(n));
			done += n;
			continue;
		}
		n = std::min<int64_t>(n, size - at);
		const auto *from = s->busRead != nullptr
			? reinterpret_cast<const uint8_t *>(s->busRead(index, at, static_cast<int32_t>(n)))
			: nullptr;
		if (from != nullptr) std::memcpy(buf + done, from, static_cast<size_t>(n));
		else
			/* PeekBus takes a 32-bit address: past it, zeros, not the byte
			 * at an address wrapped into the negatives */
			for (int64_t i = 0; i < n; i++)
				buf[done + i] = at + i <= INT32_MAX ? static_cast<uint8_t>(s->busPeek(index, static_cast<int32_t>(at + i))) : 0;
		done += n;
	}
	return len;
}

int32_t ce_session_bus_ranges(const ce_session *s, int32_t index, int64_t *pairs, int32_t cap)
{
	if (s->busRanges == nullptr || index < 0 || index >= static_cast<int32_t>(s->busSizes.size())) return -1;
	const auto *list = reinterpret_cast<const int64_t *>(s->busRanges(index));
	if (list == nullptr) return -1;
	/* (address, length) pairs, ended by one of no length; a list with no end is cut */
	int32_t n = 0;
	while (n < CE_BUS_RANGES_MAX && list[2 * n + 1] > 0)
	{
		if (pairs != nullptr && n < cap)
		{
			pairs[2 * n] = list[2 * n];
			pairs[2 * n + 1] = list[2 * n + 1];
		}
		n++;
	}
	return n;
}

void ce_session_bus_poke(ce_session *s, int32_t index, int32_t addr, int32_t value)
{
	if (s->busPoke != nullptr && ce_session_bus_writable(s, index) != 0) s->busPoke(index, addr, value);
}

// ---- savedata export (docs/save-data.md) ----

namespace {

/* relative and clean, or it does not leave the sandbox: no absolute paths,
 * no backslashes, no "." or ".." components, no empty ones */
bool savedataNameClean(const char *name)
{
	if (name == nullptr || name[0] == '\0' || name[0] == '/') return false;
	const char *p = name;
	while (*p != '\0')
	{
		const char *end = p;
		while (*end != '\0' && *end != '/') { if (*end == '\\') return false; end++; }
		size_t n = static_cast<size_t>(end - p);
		if (n == 0) return false;                                   // "//" or trailing '/'
		if (n == 1 && p[0] == '.') return false;
		if (n == 2 && p[0] == '.' && p[1] == '.') return false;
		p = *end == '/' ? end + 1 : end;
	}
	return true;
}

} // namespace

int32_t ce_session_savedata_available(const ce_session *s) { return s->sdCount != nullptr ? 1 : 0; }

int32_t ce_session_savedata_count(ce_session *s)
{
	s->sdNames.clear();
	s->sdSizes.clear();
	s->sdGuestIndex.clear();
	if (s->sdCount == nullptr) return 0;
	int32_t n = s->sdCount();
	for (int32_t i = 0; i < n; i++)
	{
		const auto *name = reinterpret_cast<const char *>(s->sdName(i));
		if (!savedataNameClean(name))
		{
			std::fprintf(stderr, "%s: savedata entry %d has an unclean path%s%s%s - dropped\n",
				s->cfg.coreName.c_str(), i,
				name != nullptr ? " (\"" : "", name != nullptr ? name : "", name != nullptr ? "\")" : "");
			continue;
		}
		int64_t size = s->sdSize(i);
		if (size < 0) size = 0;
		s->sdNames.emplace_back(name);
		s->sdSizes.push_back(size);
		s->sdGuestIndex.push_back(i);
	}
	return static_cast<int32_t>(s->sdNames.size());
}

const char *ce_session_savedata_name(const ce_session *s, int32_t index)
{
	if (index < 0 || static_cast<size_t>(index) >= s->sdNames.size()) return nullptr;
	return s->sdNames[static_cast<size_t>(index)].c_str();
}

int64_t ce_session_savedata_size(const ce_session *s, int32_t index)
{
	if (index < 0 || static_cast<size_t>(index) >= s->sdSizes.size()) return 0;
	return s->sdSizes[static_cast<size_t>(index)];
}

int64_t ce_session_savedata_read(ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len)
{
	if (index < 0 || static_cast<size_t>(index) >= s->sdGuestIndex.size()) return 0;
	int64_t size = s->sdSizes[static_cast<size_t>(index)];
	if (offset < 0 || offset >= size || len <= 0) return 0;
	int64_t n = len < size - offset ? len : size - offset;
	const int32_t guest = s->sdGuestIndex[static_cast<size_t>(index)];

	/* A file the guest cannot hand over whole is served a window at a time. It
	 * is the same file - same name, same size, same bytes - and this loop is
	 * the only thing that knows the difference. */
	if (s->sdRead != nullptr)
	{
		int64_t done = 0;
		while (done < n)
		{
			int64_t got = s->sdRead(guest, offset + done, n - done);
			if (got <= 0) break;
			/* the guest's count, so no more than was asked: past it is the
			 * caller's memory, outside the sandbox */
			if (got > n - done)
			{
				fprintf(stderr, "[%s] ReadSaveDataFile answered %lld bytes when %lld were asked; the rest is not copied\n",
					s->cfg.coreName.c_str(), (long long)got, (long long)(n - done));
				got = n - done;
			}
			const auto *win = reinterpret_cast<const uint8_t *>(s->sdScratch());
			if (win == nullptr) break;
			std::memcpy(buf + done, win, static_cast<size_t>(got));
			done += got;
		}
		return done;
	}

	/* fetched per call rather than kept: the guest owns the allocation and a
	 * snapshot only promises the file, not the address */
	const auto *src = reinterpret_cast<const uint8_t *>(s->sdBuffer(guest));
	if (src == nullptr) return 0;
	std::memcpy(buf, src + offset, static_cast<size_t>(n));
	return n;
}

int32_t ce_session_trace_available(const ce_session *s) { return s->traceSetEnabled != nullptr ? 1 : 0; }

const char *ce_session_trace_header(const ce_session *s) { return s->traceHeader.c_str(); }

void ce_session_trace_enable(ce_session *s, int32_t on)
{
	s->traceDesired = on != 0;
	if (s->traceSetEnabled != nullptr) s->traceSetEnabled(on != 0 ? 1 : 0);
}

const uint8_t *ce_session_trace_drain(
	ce_session *s, uint64_t *len_out, int32_t *line_count_out, int32_t *overflow_out)
{
	s->traceOut.clear();
	int32_t lines = 0;
	if (s->traceLineCount != nullptr && s->traceBuffer != nullptr)
	{
		int32_t reported = s->traceLineCount();
		if (reported > 0)
		{
			/* one bulk copy of the whole used region beats a call per line when
			 * a frame can produce tens of thousands; cores that don't report the
			 * byte count fall back to walking line by line */
			int32_t used = s->traceUsedBytes != nullptr ? s->traceUsedBytes() : -1;
			const auto *base = reinterpret_cast<const uint8_t *>(s->traceBuffer());
			if (used > 0)
			{
				s->traceOut.assign(base, base + used);
				for (int32_t i = 0; i < used; i++)
				{
					if (s->traceOut[static_cast<size_t>(i)] == 0) lines++;
				}
			}
			else if (base != nullptr)
			{
				const uint8_t *p = base;
				for (int32_t i = 0; i < reported; i++)
				{
					size_t n = std::strlen(reinterpret_cast<const char *>(p));
					if (n == 0) break;
					s->traceOut.insert(s->traceOut.end(), p, p + n + 1);
					p += n + 1;
					lines++;
				}
			}
		}
		int32_t overflow = s->traceOverflow != nullptr && s->traceOverflow() != 0 ? 1 : 0;
		if (overflow_out != nullptr) *overflow_out = overflow;
		if (s->traceClear != nullptr) s->traceClear();
	}
	else if (overflow_out != nullptr)
	{
		*overflow_out = 0;
	}
	if (len_out != nullptr) *len_out = s->traceOut.size();
	if (line_count_out != nullptr) *line_count_out = lines;
	return s->traceOut.data();
}

int32_t ce_session_movie_load(ce_session *s, const ce_movie_log *log)
{
	s->takeEdits();
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr) s->movie = ce_movie_log_new();
	/* What the history holds past the first entry the two logs disagree on
	 * describes the old movie. A capture used to find that out on the way
	 * through, by dropping everything ahead of every frame it stored; it no
	 * longer does (a replay keeps what is ahead), so the new log says so here.
	 * A log that merely goes on further agrees everywhere the old one reached,
	 * and what was played past the old end was the caller's input, not this. */
	{
		const int64_t was = ce_movie_log_count(s->movie);
		const int64_t now = ce_movie_log_count(log);
		int64_t agree = 0;
		const char *oldKey = ce_movie_log_key(s->movie);
		const char *newKey = ce_movie_log_key(log);
		if (std::strcmp(oldKey != nullptr ? oldKey : "", newKey != nullptr ? newKey : "") == 0)
		{
			while (agree < was && agree < now
				&& std::strcmp(ce_movie_log_entry(s->movie, agree), ce_movie_log_entry(log, agree)) == 0)
			{
				agree++;
			}
		}
		else
		{
			/* Other columns (a LogKey that gained a control): the lines differ
			 * as text everywhere, so they are compared as the input they give
			 * the machine, or the whole greenzone would go for nothing. */
			const chimera::EntryLayout before = chimera::sessionLayoutOf(s, s->movie);
			const chimera::EntryLayout after = chimera::sessionLayoutOf(s, log);
			std::vector<uint8_t> b1, b2;
			std::vector<int32_t> a1, a2;
			while (agree < was && agree < now
				&& before.parse(ce_movie_log_entry(s->movie, agree), b1, a1)
				&& after.parse(ce_movie_log_entry(log, agree), b2, a2)
				&& b1 == b2 && a1 == a2)
			{
				agree++;
			}
		}
		/* a rerecord when it drops a stored state (movieLock is held here) */
		if (s->countingRerecords && ce_session_greenzone_nearest(s, INT64_MAX) > agree) s->rerecords++;
		s->history.invalidateAfter(agree);
		/* and the pictures kept for frames past it, which were drawn for the
		 * input the new log replaces (engine analysis, 2026-10-18) */
		s->pictures.dropAfter(agree);
		/* a machine past the first changed frame was made by the old movie */
		if (s->frame > agree) s->staleFrom = s->staleFrom < 0 ? agree : std::min(s->staleFrom, agree);
	}
	ce_movie_log_clear(s->movie);
	int64_t n = ce_movie_log_count(log);
	for (int64_t i = 0; i < n; i++) ce_movie_log_add(s->movie, ce_movie_log_entry(log, i));
	const char *key = ce_movie_log_key(log);
	ce_movie_log_set_key(s->movie, key);
	s->movieKeyed = key != nullptr && key[0] != '\0' && s->movieLayout.buildFromKey(key, s->cfg.buttons, s->cfg.axes);
	s->movieMode = 1;
	return 0;
}

void ce_session_movie_record(ce_session *s, const char *mnemonics)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr) s->movie = ce_movie_log_new();
	/* A recording names its columns, so that it reads the same after the
	 * package gains a control. A movie with no key is in the machine's own
	 * order, and the key that names that order describes its lines too. */
	if (!s->movieKeyed)
	{
		const std::string key = s->layout.key(s->cfg.buttons);
		if (!key.empty() && s->movieLayout.buildFromKey(key, s->cfg.buttons, s->cfg.axes))
		{
			ce_movie_log_set_key(s->movie, key.c_str());
			s->movieKeyed = true;
		}
	}
	if (mnemonics != nullptr)
	{
		s->mnemonics = mnemonics;
	}
	else
	{
		/* the package's own letters, and the rule where it gave none */
		s->mnemonics = s->cfg.mnemonics;
	}
	s->movieMode = 2;
}

int32_t ce_session_movie_mode(const ce_session *s) { return s->movieMode; }

int64_t ce_session_movie_length(const ce_session *s)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	return s->movie != nullptr ? ce_movie_log_count(s->movie) : 0;
}

/* ---- the movie, edited frame by frame (engine.h) ---- */

void ce_session::nameColumns()
{
	/* as a recording does: a movie edited names its columns (movieLock held) */
	if (movieKeyed) return;
	const std::string key = layout.key(cfg.buttons);
	if (!key.empty() && movieLayout.buildFromKey(key, cfg.buttons, cfg.axes))
	{
		ce_movie_log_set_key(movie, key.c_str());
		movieKeyed = true;
	}
}

int32_t ce_session_movie_get(const ce_session *s, int64_t frame, uint8_t *states_out, int32_t *axes_out)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (states_out != nullptr) std::fill(states_out, states_out + s->cfg.buttons.size(), uint8_t{0});
	if (axes_out != nullptr)
		for (size_t i = 0; i < s->cfg.axes.size(); i++) axes_out[i] = s->cfg.axes[i].neutral;
	if (s->movie == nullptr || frame < 0 || frame >= ce_movie_log_count(s->movie)) return 1;
	std::vector<uint8_t> states;
	std::vector<int32_t> values;
	if (!s->entries().parse(ce_movie_log_entry(s->movie, frame), states, values)) return -1;
	if (states_out != nullptr)
		for (size_t i = 0; i < states.size() && i < s->cfg.buttons.size(); i++) states_out[i] = states[i] != 0 ? 1 : 0;
	if (axes_out != nullptr)
		for (size_t i = 0; i < values.size(); i++) axes_out[i] = values[i];
	return 0;
}

int32_t ce_session_movie_set(ce_session *s, int64_t frame, const uint8_t *pressed, const int32_t *axes)
{
	if (frame < 0) return -1;
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr) s->movie = ce_movie_log_new();
	if (s->movieMode == 0) s->movieMode = 1;
	s->nameColumns();
	std::vector<uint8_t> states(s->cfg.buttons.size(), 0);
	if (pressed != nullptr)
		for (size_t i = 0; i < states.size(); i++) states[i] = pressed[i] != 0 ? 1 : 0;
	s->coverInKey(states.data(), axes);
	const int64_t count = ce_movie_log_count(s->movie);
	if (frame > count)
	{
		const std::string blank = s->entries().generate(nullptr, nullptr, s->cfg.mnemonics);
		for (int64_t f = count; f < frame; f++) ce_movie_log_add(s->movie, blank.c_str());
	}
	const std::string line = s->entries().generate(states.data(), axes, s->cfg.mnemonics);
	if (frame >= count) ce_movie_log_add(s->movie, line.c_str());
	else ce_movie_log_set(s->movie, frame, line.c_str());
	const int64_t from = std::min(frame, count);
	s->editedFrom = s->editedFrom < 0 ? from : std::min(s->editedFrom, from);
	if (s->countingRerecords) s->countedEdit = true;
	return 0;
}

void ce_session_movie_insert(ce_session *s, int64_t frame, int64_t count)
{
	if (frame < 0 || count <= 0) return;
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr) s->movie = ce_movie_log_new();
	if (s->movieMode == 0) s->movieMode = 1;
	s->nameColumns();
	const int64_t had = ce_movie_log_count(s->movie);
	const std::string blank = s->entries().generate(nullptr, nullptr, s->cfg.mnemonics);
	/* past the end: the frames up to it first, as _set does */
	if (frame > had) ce_movie_log_insert_n(s->movie, had, blank.c_str(), frame - had);
	ce_movie_log_insert_n(s->movie, frame, blank.c_str(), count);
	const int64_t from = std::min(frame, had);
	s->editedFrom = s->editedFrom < 0 ? from : std::min(s->editedFrom, from);
	if (s->countingRerecords) s->countedEdit = true;
}

void ce_session_movie_remove(ce_session *s, int64_t frame, int64_t count)
{
	if (frame < 0 || count <= 0) return;
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr || frame >= ce_movie_log_count(s->movie)) return;
	ce_movie_log_remove_range(s->movie, frame, count);
	s->editedFrom = s->editedFrom < 0 ? frame : std::min(s->editedFrom, frame);
	if (s->countingRerecords) s->countedEdit = true;
}

void ce_session_movie_resize(ce_session *s, int64_t frames)
{
	if (frames < 0) frames = 0;
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr) s->movie = ce_movie_log_new();
	if (s->movieMode == 0) s->movieMode = 1;
	s->nameColumns();
	const int64_t count = ce_movie_log_count(s->movie);
	if (frames < count) ce_movie_log_truncate(s->movie, frames);
	else if (frames > count)
	{
		const std::string blank = s->entries().generate(nullptr, nullptr, s->cfg.mnemonics);
		for (int64_t f = count; f < frames; f++) ce_movie_log_add(s->movie, blank.c_str());
	}
	const int64_t from = std::min(frames, count);
	if (frames != count)
	{
		s->editedFrom = s->editedFrom < 0 ? from : std::min(s->editedFrom, from);
		if (s->countingRerecords) s->countedEdit = true;
	}
}

const char *ce_session_movie_text(ce_session *s, uint64_t *len_out)
{
	static thread_local std::string text;
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr)
	{
		text.clear();
	}
	else
	{
		uint64_t len = 0;
		const char *t = ce_movie_log_serialize(s->movie, 0, &len);
		text.assign(t != nullptr ? t : "", static_cast<size_t>(len));
	}
	if (len_out != nullptr) *len_out = text.size();
	return text.c_str();
}

/* The session's movie journaled (engine.h): the log's own journal, under the
 * movie's lock, as every change to the log is made under it. */
int32_t ce_session_movie_journal_open(ce_session *s, const char *path, const char *frontend_image)
{
	if (path == nullptr) return 1;
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie == nullptr) s->movie = ce_movie_log_new();
	return ce_movie_log_journal_open_with(s->movie, path, frontend_image);
}

void ce_session_movie_journal_note(ce_session *s, const char *record)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie != nullptr) ce_movie_log_journal_note(s->movie, record);
}

void ce_session_movie_journal_close(ce_session *s, int32_t remove)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	if (s->movie != nullptr) ce_movie_log_journal_close(s->movie, remove);
}

int32_t ce_session_movie_journaling(const ce_session *s)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	return ce_movie_log_journaling(s->movie);
}

/* What was edited since the machine last moved: the states past it go, and
 * a machine already past it is behind its movie (machine's thread). */
void ce_session::takeEdits()
{
	int64_t from;
	bool counts;
	{
		std::lock_guard<std::mutex> movieLocked(movieLock);
		from = editedFrom;
		editedFrom = -1;
		counts = countedEdit;
		countedEdit = false;
	}
	if (from < 0) return;
	if (counts) rerecordIfDropping(from);
	history.invalidateAfter(from);
	pictures.dropAfter(from);
	if (frame > from) staleFrom = staleFrom < 0 ? from : std::min(staleFrom, from);
}

/* The entry for frame `at`, parsed into movieButtons and axes (movieLock
 * taken here): false when there is none or it does not read. */
bool ce_session::movieEntry(int64_t at, std::vector<int32_t> &axes)
{
	std::lock_guard<std::mutex> movieLocked(movieLock);
	const char *entry = movie != nullptr && at < ce_movie_log_count(movie) ? ce_movie_log_entry(movie, at) : nullptr;
	if (entry == nullptr || !entries().parse(entry, movieButtons, axes))
	{
		error = tr("engine.unparseable_entry", {std::to_string(at)});
		return false;
	}
	return true;
}

/* A machine behind its movie (staleFrom) put back on a stored frame before
 * the edit and played to `to`. */
bool ce_session::catchUp(int64_t to, int64_t render)
{
	if (staleFrom < 0) return true;
	const int64_t base = ce_session_greenzone_nearest(this, std::min(staleFrom, to));
	staleFrom = -1;
	if (base < 0)
	{
		error = tr("engine.changed_before");
		return false;
	}
	if (!greenzoneRestore(base)) return false;
	while (frame < to)
	{
		std::vector<int32_t> movieAxes;
		if (!movieEntry(frame, movieAxes)) return false;
		for (size_t i = 0; i < movieAxes.size(); i++)
		{
			if (setAxis != nullptr) setAxis(static_cast<int32_t>(i), movieAxes[i]);
		}
		greenzoneBeforeAdvance();
		if (advanceCore(movieButtons.data(), render) < 0) return false;
		greenzoneCapture();
	}
	return true;
}

int64_t ce_session_frame(const ce_session *s) { return s->frame; }

void ce_session::rerecordIfDropping(int64_t after)
{
	if (ce_session_greenzone_nearest(this, INT64_MAX) > after)
	{
		std::lock_guard<std::mutex> movieLocked(movieLock);
		rerecords++;
	}
}

uint64_t ce_session_rerecords(const ce_session *s)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	return s->rerecords;
}

void ce_session_set_rerecords(ce_session *s, uint64_t count)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	s->rerecords = count;
}

void ce_session_rerecord_counting(ce_session *s, int32_t on)
{
	std::lock_guard<std::mutex> movieLocked(s->movieLock);
	s->countingRerecords = on != 0;
}

/* for the engine's own bookkeeping (mcp.cpp): not in engine.h */
extern "C" uint64_t ce_session_serial(const ce_session *s) { return s != nullptr ? s->serial : 0; }

const ce_movie_log *ce_session_movie_log(const ce_session *s) { return s->movie; }

int32_t ce_session_movie_entry_decode(
	const ce_session *s, const char *entry, uint8_t *states_out, int32_t *axes_out)
{
	if (entry == nullptr) return -1;
	std::vector<uint8_t> states;
	std::vector<int32_t> values;
	if (!s->entries().parse(entry, states, values)) return -1;
	if (states_out != nullptr)
	{
		for (size_t i = 0; i < states.size(); i++) states_out[i] = states[i];
	}
	if (axes_out != nullptr)
	{
		for (size_t i = 0; i < values.size(); i++) axes_out[i] = values[i];
	}
	return 0;
}

int32_t ce_session_movie_advance(ce_session *s, uint64_t buttons, const int32_t *axes, int32_t render)
{
	s->error.clear();
	if (s->movieMode == 0 || s->movie == nullptr)
	{
		s->error = tr("engine.no_movie");
		return -1;
	}
	/* A dead machine plays nothing, and recording on it must change nothing:
	 * the check came after the movie was cut at this frame and the greenzone
	 * past it dropped (engine analysis, 2026-10-18). */
	if (s->guestDied())
	{
		s->error = tr("engine.core_stopped", {s->death});
		return -1;
	}

	/* edits made since the machine last moved, honoured first: a machine past
	 * them catches up with the movie it now has */
	s->takeEdits();
	if (!s->catchUp(s->frame, 0)) return -1;
	/* a movie that ran out and has since grown plays again */
	if (s->movieMode == 3 && s->frame < ce_session_movie_length(s)) s->movieMode = 1;

	/* the epoch the coming frame's delta is measured against, marked while the
	 * machine is still where it starts */
	s->greenzoneBeforeAdvance();

	int32_t lag;
	if (s->movieMode == 1 && s->frame < ce_session_movie_length(s))
	{
		std::vector<int32_t> movieAxes;
		if (!s->movieEntry(s->frame, movieAxes)) return -1;
		for (size_t i = 0; i < movieAxes.size(); i++)
		{
			if (s->setAxis != nullptr) s->setAxis(static_cast<int32_t>(i), movieAxes[i]);
		}
		lag = s->advanceCore(s->movieButtons.data(), render);
		if (lag < 0) return -1;   /* the guest died: the error says why */
	}
	else
	{
		if (s->movieMode == 1) s->movieMode = 3; // the log ran out: input is the caller's again
		if (s->movieMode == 2 && s->frame < ce_session_movie_length(s))
		{
			/* recording over existing entries IS the rerecord: everything from
			 * here on described a timeline that no longer happens */
			std::lock_guard<std::mutex> movieLocked(s->movieLock);
			ce_movie_log_truncate(s->movie, s->frame);
		}
		/* Input that is not the log's changes what follows, so the history says
		 * so here. A capture no longer does it for us: a frame it has already
		 * gone past is a replay, and a replay keeps what is ahead. */
		s->history.invalidateAfter(s->frame);
		s->pictures.dropAfter(s->frame);
		for (size_t i = 0; i < s->cfg.axes.size(); i++)
		{
			int32_t value = axes != nullptr ? axes[i] : s->cfg.axes[i].neutral;
			if (s->setAxis != nullptr) s->setAxis(static_cast<int32_t>(i), value);
		}
		/* the caller's input: the packed mask OR'd with the set_button states,
		 * so a wide controller records exactly what the machine receives */
		const uint8_t *effective = s->computeEffective(buttons);
		/* a dead machine records nothing: the input was never played */
		if (s->guestDied()) return -1;
		if (s->movieMode == 2)
		{
			std::lock_guard<std::mutex> movieLocked(s->movieLock);
			s->coverInKey(effective, axes);
			std::string entry = s->entries().generate(effective, axes, s->mnemonics);
			ce_movie_log_add(s->movie, entry.c_str());
		}
		lag = s->advanceCore(effective, render);
		if (lag < 0)
		{
			/* it died playing it: the entry describes a frame that never happened */
			if (s->movieMode == 2)
			{
				std::lock_guard<std::mutex> movieLocked(s->movieLock);
				ce_movie_log_truncate(s->movie, s->frame);
			}
			return -1;
		}
	}
	s->greenzoneCapture();
	return lag;
}

/* The budget a session is given when nobody chose one: an eighth of the
 * greenzone's, between 32 and 256 MiB. A 640x480 picture packs to a few
 * hundred KiB, so that is the last several hundred to few thousand frames
 * shown - where somebody working on a movie is. */
static uint64_t defaultPictureBudget(uint64_t greenzoneBudget)
{
	const uint64_t mib = uint64_t{ 1 } << 20;
	return std::min<uint64_t>(256 * mib, std::max<uint64_t>(32 * mib, greenzoneBudget / 8));
}

void ce_session_greenzone_enable(ce_session *s, uint64_t budget_bytes)
{
	s->history.configure(s->host, s->obj, budget_bytes);
	if (!s->picturesChosen) s->pictures.budget(s->gpuDrew && budget_bytes != 0 ? defaultPictureBudget(budget_bytes) : 0);
	if (budget_bytes != 0) s->greenzoneCapture(); // the anchor: the frame we stand on now
}

void ce_session_greenzone_bands(ce_session *s, int64_t near_frames, int64_t mid_frames,
	int64_t mid_stride, int64_t far_stride, int64_t anchor_spacing)
{
	if (s == nullptr) return;
	s->history.bands(near_frames, mid_frames, mid_stride, far_stride, anchor_spacing);
}

void ce_session_greenzone_before_advance(ce_session *s)
{
	if (s == nullptr) return;
	s->greenzoneBeforeAdvance();
}

int32_t ce_session_greenzone_capture(
	ce_session *s, int64_t frame, const uint8_t *note, uint32_t note_len)
{
	if (s == nullptr) return 1;
	/* nothing of a machine that died is worth a frame: stored, a restore would
	 * bring it back as if it were alive */
	if (s->guestDied()) return 0;
	/* the caller is the one counting: if the session's own count was off, the
	 * picture the last frame kept was filed under the wrong frame */
	if (s->machineFrame != frame)
	{
		if (s->lastKept >= 0) s->pictures.drop(s->lastKept);
		s->machineFrame = frame;
	}
	s->lastKept = -1;
	s->history.capture(frame, note, note_len);
	return 0;
}

uint32_t ce_session_greenzone_note(
	const ce_session *s, int64_t frame, uint8_t *out, uint32_t out_len)
{
	if (s == nullptr) return 0;
	size_t len = 0;
	const uint8_t *note = s->history.noteFor(frame, len);
	if (note == nullptr) return 0;
	if (out != nullptr && out_len != 0)
	{
		const size_t n = len < out_len ? len : out_len;
		std::memcpy(out, note, n);
	}
	return static_cast<uint32_t>(len);
}

int32_t ce_session_greenzone_restore(ce_session *s, int64_t frame)
{
	if (s == nullptr) return 1;
	return s->greenzoneRestore(frame) ? 0 : 1;
}

void ce_session_greenzone_pin(ce_session *s, int64_t frame, int32_t pinned)
{
	if (s == nullptr) return;
	s->history.pin(frame, pinned != 0);
}

void ce_session_greenzone_unpin_all(ce_session *s)
{
	if (s == nullptr) return;
	s->history.unpinAll();
}

void ce_session_greenzone_spill(ce_session *s, const char *dir)
{
	if (s == nullptr) return;
	s->history.spillTo(dir);
}

void ce_session_greenzone_disk_budget(ce_session *s, uint64_t budget_bytes)
{
	if (s == nullptr) return;
	s->history.diskBudget(budget_bytes);
}

void ce_session_greenzone_band_goal(ce_session *s, int64_t goal)
{
	if (s == nullptr) return;
	s->history.bandGoal(goal);
}

void ce_session_greenzone_max_near_stride(ce_session *s, int64_t max_stride)
{
	if (s == nullptr) return;
	s->history.maxNearStride(max_stride);
}

void ce_session_greenzone_capture_period(ce_session *s, int64_t period)
{
	if (s == nullptr) return;
	const bool resuming = s->history.capturePeriod() == 0 && period > 0;
	s->history.capturePeriod(period);
	/* The anchor the history goes on from, where the machine stands now. That
	 * is where the last capture said it was (they go on while nothing is
	 * stored), not the session's own frame, which only a restore and the
	 * movie path move: under the frontend it was where the last seek went,
	 * and the machine of frame N was filed under it. When nobody has said
	 * yet, the next capture stores the anchor itself. */
	if (resuming && s->history.machineFrame() >= 0) s->history.capture(s->history.machineFrame());
}

uint64_t ce_session_greenzone_disk_bytes(const ce_session *s)
{
	if (s == nullptr) return 0;
	return s->history.diskBytes();
}

int32_t ce_session_greenzone_spill_failed(const ce_session *s)
{
	if (s == nullptr) return 0;
	return s->history.spillFailed() ? 1 : 0;
}

int64_t ce_session_greenzone_count(const ce_session *s)
{
	return s->history.count();
}

int64_t ce_session_greenzone_nearest(const ce_session *s, int64_t frame)
{
	return s->history.nearest(frame);
}

void ce_session_greenzone_invalidate(ce_session *s, int64_t after_frame)
{
	s->history.invalidateAfter(after_frame);
	s->pictures.dropAfter(after_frame); /* drawn for input that is no longer the movie's */
}

void ce_session_greenzone_pictures(ce_session *s, uint64_t budget_bytes, int32_t settle_frames)
{
	if (s == nullptr) return;
	s->picturesChosen = true;
	s->pictures.budget(budget_bytes);
	if (settle_frames >= 0) s->pictureSettle = settle_frames;
}

/* (asking takes in what the helper has finished: the store is lazy, the session is not changed) */
void ce_session_greenzone_picture_settle(ce_session *s, int32_t settle_frames)
{
	if (s != nullptr) s->pictureSettle = settle_frames < 0 ? 0 : settle_frames;
}

int64_t ce_session_greenzone_picture_count(const ce_session *s) { return s != nullptr ? const_cast<ce_session *>(s)->pictures.count() : 0; }
uint64_t ce_session_greenzone_picture_bytes(const ce_session *s) { return s != nullptr ? const_cast<ce_session *>(s)->pictures.bytes() : 0; }

/* The id a persisted history is kept under.
 *
 * The caller's half says which core, settings and files this is - everything a
 * person would call "the same machine". That is not enough. Two boots of one
 * core with one configuration can still seal DIFFERENTLY, because the sealed
 * baseline's identity is which pages the machine touched on its way there, and
 * a state cannot be loaded into a machine with another baseline: a savestate
 * carries only the pages dirtied since the seal and reads every clean one back
 * from it.
 *
 * Reusing states across that line does not fail where the mistake is. Measured
 * on a PS2 project whose cache outlived its session: the greenzone loaded, one
 * seek back walked to an anchor from the other baseline, miniBox said the state
 * was foreign and carried on, and the guest died sixty frames later inside
 * std::_Rb_tree_rebalance_for_erase with nothing to connect the two.
 *
 * So the engine appends the machine's own hash rather than asking the frontend
 * to remember to. A history whose id does not match is dropped as a cold cache,
 * which is what it is - the states are recomputable, and that is the whole
 * promise of the greenzone. */
static std::string historyId(ce_session *s, const char *machine_id)
{
	std::string id = machine_id != nullptr ? machine_id : "";
	/* The savestate format rides in the id (issue #115). A history is a cache of
	 * states, and states of another format cannot be fed to this build's machine
	 * any more than another machine's can - so the same answer serves: the file
	 * is dropped, quietly, and the greenzone is rebuilt by playing. A history
	 * written before formats were numbered carries no suffix and is dropped once,
	 * on the first open after this change. */
	const std::string format = chimera::historyFormatSuffix();
	if (s->host->wbx_machine_hash == nullptr) return id + format;   /* an older host: caller's half alone */
	uint8_t hash[32] = { 0 };
	chimera::WbxReturn r{};
	s->host->wbx_machine_hash(s->obj, hash, &r);
	if (!r.ok()) return id + format;
	static const char hex[] = "0123456789abcdef";
	id += '@';
	for (uint8_t b : hash) { id += hex[b >> 4]; id += hex[b & 15]; }
	return id + format;
}

int32_t ce_session_history_save(ce_session *s, const char *path, const char *machine_id)
{
	s->error.clear();
	return s->history.saveTo(path, historyId(s, machine_id).c_str(), s->error) ? 0 : 1;
}

/* The same save, queued on the history's writer.
 *
 * A project save writes the whole history - up to fourteen gigabytes under the
 * default budgets - and it does it on the thread that runs the machine, which
 * is a freeze measured in seconds and fired every thirty minutes by TAStudio's
 * autosave. The file describes the history as it stands at THIS call; the run
 * carries on while it is written.
 *
 * 0 means it was queued (or, with helpers off, written). The answer comes from
 * ce_session_history_save_wait, which is what closing a project must call. */
int32_t ce_session_history_save_later(ce_session *s, const char *path, const char *machine_id)
{
	s->error.clear();
	return s->history.saveToLater(path, historyId(s, machine_id).c_str(), s->error) ? 0 : 1;
}

/* Whether a queued save is still being written. */
int32_t ce_session_history_save_pending(ce_session *s)
{
	return s->history.savePending() ? 1 : 0;
}

/* Waits for a queued save. 0 when it worked - and when there was nothing to
 * wait for, which is not a failure: a caller that saved in line has nothing
 * queued and nothing to hear about. */
int32_t ce_session_history_save_wait(ce_session *s)
{
	s->error.clear();
	std::string why;
	const bool ok = s->history.saveWait(why);
	if (ok || why.empty()) return 0;
	s->error = why;
	return 1;
}

static int32_t historyLoad(ce_session *s, const char *path, const char *machine_id)
{
	s->error.clear();
	/* a history this build can read, written in an older format, is kept */
	const std::string id = historyId(s, machine_id);
	const std::string now = chimera::historyFormatSuffix();
	std::vector<std::string> older;
	for (uint32_t f = chimera::kOldestReadableStateFormat; f < chimera::kStateFormat; f++)
		older.push_back(id.substr(0, id.size() - now.size()) + chimera::historyFormatSuffix(f));
	if (s->history.loadFrom(path, id.c_str(), s->error, older)) return 0;
	/* a file this machine's that turned out damaged has taken the states with
	 * it: the machine's state now is the one it starts from again */
	if (s->history.count() == 0) s->greenzoneCapture();
	return 1;
}

int32_t ce_session_history_load(ce_session *s, const char *path, const char *machine_id)
{
	try
	{
		return historyLoad(s, path, machine_id);
	}
	catch (const std::bad_alloc &)
	{
		s->error = tr("engine.no_memory_states");
		s->history.clear();
		return 1;
	}
}

int32_t ce_session_seek(ce_session *s, int64_t frame)
{
	s->error.clear();
	if (s->movieMode == 0 || s->movie == nullptr)
	{
		s->error = tr("engine.seek_needs_movie");
		return 1;
	}
	if (frame > ce_session_movie_length(s))
	{
		s->error = tr("engine.seek_past_end");
		return 1;
	}

	/* edits since the machine last moved: a machine past them goes back
	 * before them, even to stay on the frame it is on */
	s->takeEdits();
	if (s->staleFrom >= 0)
	{
		if (!s->catchUp(std::min(frame, s->frame), 0)) return 1;
	}

	int64_t base = ce_session_greenzone_nearest(s, frame);
	if (base >= 0 && (base > s->frame || s->frame > frame))
	{
		if (!s->greenzoneRestore(base)) return 1;
	}
	else if (s->frame > frame)
	{
		s->error = tr("engine.no_state_before");
		return 1;
	}

	/* replay the log to the target - a seek is a replay, never a guess */
	while (s->frame < frame)
	{
		std::vector<int32_t> movieAxes;
		if (!s->movieEntry(s->frame, movieAxes)) return 1;
		for (size_t i = 0; i < movieAxes.size(); i++)
		{
			if (s->setAxis != nullptr) s->setAxis(static_cast<int32_t>(i), movieAxes[i]);
		}
		s->greenzoneBeforeAdvance();
		if (s->advanceCore(s->movieButtons.data(), 0) < 0) return 1;   /* the guest died */
		s->greenzoneCapture();
	}
	if (s->movieMode == 3 && s->frame < ce_session_movie_length(s)) s->movieMode = 1;
	return 0;
}

int32_t ce_session_movie_at(ce_session *s, int64_t frame)
{
	s->error.clear();
	if (s->movieMode == 0 || s->movie == nullptr)
	{
		s->error = tr("engine.session_no_movie");
		return 1;
	}
	if (frame < 0 || frame > ce_session_movie_length(s))
	{
		s->error = tr("piano_roll.past_end", {std::to_string(frame)});
		return 1;
	}
	/* edits drop the states past them; the machine itself is the one loaded */
	s->takeEdits();
	s->staleFrom = -1;
	s->frame = frame;
	s->machineFrame = frame;
	s->greenzoneCapture();
	if (s->movieMode == 3 && s->frame < ce_session_movie_length(s)) s->movieMode = 1;
	return 0;
}

} // extern "C"

/* ---- the compile cache and precompile sessions -------------------------- */

extern "C" uint64_t ce_session_cache_stored(const ce_session *s)
{
	if (s == nullptr || s->fnCacheStored == 0) return 0;
	return reinterpret_cast<uint64_t (*)()>(s->fnCacheStored)();
}

extern "C" uint64_t ce_session_cache_fetched(const ce_session *s)
{
	if (s == nullptr || s->fnCacheFetched == 0) return 0;
	return reinterpret_cast<uint64_t (*)()>(s->fnCacheFetched)();
}

/* Opens a project's machine that asks `exportName` instead of Init
 * (s_answerExport) and hands back its answer: "" when the core has no such
 * export, nullptr with *error_out when the package or files cannot be opened
 * at all. */
static const char *askCore(const char *exportName,
	const char *package_path, ce_project *project,
	const char *const *more_names, const char *const *more_paths, int32_t more_count,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint64_t *len_out, const char **error_out)
{
	/* never destroyed: a thread_local std::string is freed twice by a mingw
	 * DLL at exit (thread_string.hpp, chimera#123) */
	static thread_local chimera::ThreadString answer;
	answer->clear();
	if (len_out != nullptr) *len_out = 0;
	struct Asking
	{
		explicit Asking(const char *name) { s_answerExport = name; }
		~Asking() { s_answerExport = nullptr; }
	};
	ce_session *s = nullptr;
	try
	{
		Asking asking(exportName);
		s = openProjectSession(package_path, project, nullptr,
			firmware_ids, firmware_data, firmware_lens, firmware_count, kOpenToAsk,
			more_names, more_paths, more_count, error_out);
	}
	catch (const std::bad_alloc &)
	{
		openRefused(tr("engine.no_memory_start"), error_out);
		return nullptr;
	}
	catch (const std::exception &e)
	{
		openRefused(tr("engine.not_started", {e.what()}), error_out);
		return nullptr;
	}
	if (s == nullptr) return nullptr;
	*answer = s->suggestion;
	ce_session_free(s);
	if (len_out != nullptr) *len_out = answer->size();
	return answer->c_str();
}

extern "C" const char *ce_project_suggest_settings(
	const char *package_path, ce_project *project,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint64_t *len_out, const char **error_out)
{
	return askCore("SuggestSettings", package_path, project, nullptr, nullptr, 0,
		firmware_ids, firmware_data, firmware_lens, firmware_count, len_out, error_out);
}

extern "C" const char *ce_project_import_movie(
	const char *package_path, ce_project *project, const char *movie_path,
	const char *const *mount_names, const char *const *mount_paths, int32_t mount_count,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	uint64_t *len_out, const char **error_out)
{
	std::vector<const char *> names{ "movie" }, paths{ movie_path };
	for (int32_t i = 0; i < mount_count; i++)
	{
		names.push_back(mount_names[i]);
		paths.push_back(mount_paths[i]);
	}
	return askCore("ImportMovie", package_path, project, names.data(), paths.data(), static_cast<int32_t>(names.size()),
		firmware_ids, firmware_data, firmware_lens, firmware_count, len_out, error_out);
}

extern "C" int32_t ce_session_precompile_done(const ce_session *s)
{
	if (s == nullptr || !s->precompile || s->fnPrecompileDone == 0) return -1;
	return reinterpret_cast<int32_t (*)()>(s->fnPrecompileDone)() ? 1 : 0;
}

extern "C" int32_t ce_session_precompile_progress(const ce_session *s, uint32_t *done_out, uint32_t *total_out)
{
	if (s == nullptr || !s->precompile || s->fnPrecompileDoneCount == 0 || s->fnPrecompileTotal == 0) return -1;
	const uint32_t done = reinterpret_cast<uint32_t (*)()>(s->fnPrecompileDoneCount)();
	const uint32_t total = reinterpret_cast<uint32_t (*)()>(s->fnPrecompileTotal)();
	if (done_out) *done_out = done;
	if (total_out) *total_out = total;
	/* Said here, on the same stream the object lines use: whoever started this
	 * session is watching it, and a caller's own writes may sit in a buffer
	 * this one does not control. */
	static uint32_t lastDone = UINT32_MAX, lastTotal = UINT32_MAX;
	if (done != lastDone || total != lastTotal)
	{
		lastDone = done;
		lastTotal = total;
		printf("Precompiled %u/%u modules\n", done, total);
		fflush(stdout);
	}
	return 0;
}

static uintptr_t CE_GUEST_CALLBACK_ABI inputPollDispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t)
{
	ce_session *s = t_framing;
	if (s != nullptr && s->inputPollFn != nullptr) s->inputPollFn(s->inputPollCtx);
	guestWorked();
	return 0;
}

int32_t ce_session_input_polls_reported(const ce_session *s)
{
	return s != nullptr && s->inputPollsReported ? 1 : 0;
}

void ce_session_set_input_poll_callback(ce_session *s, ce_input_poll_fn fn, void *ctx)
{
	if (s == nullptr) return;
	s->inputPollFn = fn;
	s->inputPollCtx = ctx;
}

/* ---- frozen addresses ---- */

static uint64_t freezeRead(const ce_session::Freeze &f, const uint8_t *at)
{
	uint64_t v = 0;
	for (int32_t i = 0; i < f.size; i++) v |= uint64_t{at[f.bigEndian ? f.size - 1 - i : i]} << (8 * i);
	return v;
}

void ce_session::applyFreezes()
{
	for (const Freeze &f : freezes)
	{
		if (!f.enabled) continue;
		if (f.property >= 0)
		{
			std::string ignored;
			placeProperty(f.property);
			properties.writeText(f.property, f.element, f.text, ignored);
			continue;
		}
		if (f.domain < 0 || f.domain >= mdCount() || mdPtr == nullptr || f.address < 0 || f.address + f.size > mdSize(f.domain)) continue;
		const uintptr_t base = mdPtr(f.domain);
		if (base == 0) continue;
		if (f.compareKind != 0)
		{
			const uint8_t *seen = domainReadView(this, f.domain, f.address, f.size);
			if (seen == nullptr) continue;
			const uint64_t now = freezeRead(f, seen);
			bool write = true;
			switch (f.compareKind)
			{
			case 1: write = f.compare == now; break;
			case 2: write = f.compare != now; break;
			case 3: write = f.compare > now; break;
			case 4: write = f.compare >= now; break;
			case 5: write = f.compare < now; break;
			case 6: write = f.compare <= now; break;
			default: break;
			}
			if (!write) continue;
		}
		uint8_t *at = reinterpret_cast<uint8_t *>(base) + f.address;
		for (int32_t i = 0; i < f.size; i++) at[f.bigEndian ? f.size - 1 - i : i] = static_cast<uint8_t>(f.value >> (8 * i));
	}
}

int32_t ce_session_freeze(ce_session *s, int32_t domain, int64_t address, int32_t size, int32_t big_endian, uint64_t value,
	int32_t compare_kind, uint64_t compare)
{
	s->error.clear();
	if (!domainIndexOk(s, domain))
	{
		s->error = tr("session.no_domain", {std::to_string(domain)});
		return -1;
	}
	const char *name = reinterpret_cast<const char *>(s->mdName(domain));
	if (s->mdWritable(domain) == 0 || s->mdPtr == nullptr || s->mdPtr(domain) == 0)
	{
		s->error = tr("session.domain_read_only", {name != nullptr ? name : tr("engine.the_domain")});
		return -1;
	}
	if ((size != 1 && size != 2 && size != 4) || address < 0 || address + size > s->mdSize(domain))
	{
		s->error = tr("engine.domain_short", {name != nullptr ? name : tr("engine.the_domain"), std::to_string(size), std::to_string(address)});
		return -1;
	}
	if (compare_kind < 0 || compare_kind > 6)
	{
		s->error = tr("engine.no_compare_kind", {std::to_string(compare_kind)});
		return -1;
	}
	ce_session::Freeze f;
	f.domain = domain;
	f.address = address;
	f.size = size;
	f.bigEndian = big_endian != 0;
	f.value = value & ((uint64_t{1} << (8 * size)) - 1);
	f.compareKind = compare_kind;
	f.compare = compare;
	for (size_t i = 0; i < s->freezes.size(); i++)
		if (s->freezes[i].property < 0 && s->freezes[i].domain == domain && s->freezes[i].address == address)
		{
			s->freezes[i] = f;
			return static_cast<int32_t>(i);
		}
	s->freezes.push_back(f);
	return static_cast<int32_t>(s->freezes.size() - 1);
}

int32_t ce_session_freeze_property(ce_session *s, int32_t index, uint32_t element, const char *text)
{
	s->error.clear();
	/* the text is checked once, by writing it now */
	if (ce_session_property_set_text(s, index, element, text) != 0) return -1;
	ce_session::Freeze f;
	f.property = index;
	f.element = element;
	f.text = text != nullptr ? text : "";
	for (size_t i = 0; i < s->freezes.size(); i++)
		if (s->freezes[i].property == index && s->freezes[i].element == element)
		{
			s->freezes[i] = f;
			return static_cast<int32_t>(i);
		}
	s->freezes.push_back(f);
	return static_cast<int32_t>(s->freezes.size() - 1);
}

int32_t ce_session_freeze_count(const ce_session *s) { return static_cast<int32_t>(s->freezes.size()); }

void ce_session_unfreeze(ce_session *s, int32_t index)
{
	if (index >= 0 && static_cast<size_t>(index) < s->freezes.size()) s->freezes.erase(s->freezes.begin() + index);
}

void ce_session_unfreeze_all(ce_session *s) { s->freezes.clear(); }

void ce_session_freeze_enable(ce_session *s, int32_t index, int32_t on)
{
	if (index >= 0 && static_cast<size_t>(index) < s->freezes.size()) s->freezes[static_cast<size_t>(index)].enabled = on != 0;
}

int32_t ce_session_freeze_enabled(const ce_session *s, int32_t index)
{
	if (index < 0 || static_cast<size_t>(index) >= s->freezes.size()) return -1;
	return s->freezes[static_cast<size_t>(index)].enabled ? 1 : 0;
}

int32_t ce_session_frozen(const ce_session *s, int32_t domain, int64_t address)
{
	for (size_t i = 0; i < s->freezes.size(); i++)
	{
		const ce_session::Freeze &f = s->freezes[i];
		if (f.enabled && f.property < 0 && f.domain == domain && address >= f.address && address < f.address + f.size)
			return static_cast<int32_t>(i);
	}
	return -1;
}
