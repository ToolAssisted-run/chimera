/* session.cpp - a running waterboxed machine.
 *
 * The transliteration of WaterboxCore.cs's machine half: package + config +
 * rom + settings + firmware in, one frame at a time out. Behavioural notes
 * from the C# are carried over where they matter - especially the ones paid
 * for in blood (no activate/deactivate bracket around save/load state; the
 * frozen post-Init image as the savestate baseline).
 */

#include "chimera/engine.h"
#include "file_io.hpp"
#include "host_dyn.hpp"

#include "../../extern/cjson/cJSON.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

thread_local std::string g_openError;

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

extern "C" int32_t vectorWrite(uintptr_t userdata, const void *src, uintptr_t size)
{
	auto *v = reinterpret_cast<std::vector<uint8_t> *>(userdata);
	const auto *p = static_cast<const uint8_t *>(src);
	v->insert(v->end(), p, p + size);
	return 0;
}

struct AxisDecl
{
	std::string name;
	int32_t min = 0, max = 0, neutral = 0;
};

/* the slice of waterbox.config the machine itself needs */
struct SessionConfig
{
	std::string coreName, systemId, romFile;
	uint64_t layoutMiB[5] = { 0, 0, 0, 0, 0 };
	int32_t width = 0, height = 0, virtualWidth = 0, virtualHeight = 0;
	int32_t vsyncNum = 0, vsyncDen = 0;
	std::string getBgra, getAudio;
	int32_t samplesPerFrame = 0, channels = 1;
	std::string inputName, inputWasRead;
	std::vector<std::string> buttons;
	std::vector<AxisDecl> axes;
	bool deterministic = false;
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

bool parseConfig(const char *json, uint64_t len, const char *overrides, SessionConfig &cfg, std::string &error)
{
	cJSON *root = cJSON_ParseWithLength(json, static_cast<size_t>(len));
	if (root == nullptr || !cJSON_IsObject(root))
	{
		cJSON_Delete(root);
		error = "waterbox.config is not readable JSON";
		return false;
	}
	cfg.coreName = strOf(root, "coreName", "Waterbox");
	cfg.systemId = strOf(root, "systemId");
	cfg.romFile = strOf(root, "romFile", "rom");
	cfg.deterministic = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "deterministic"));

	const cJSON *layout = cJSON_GetObjectItemCaseSensitive(root, "memoryLayoutMiB");
	if (!cJSON_IsArray(layout) || cJSON_GetArraySize(layout) != 5)
	{
		cJSON_Delete(root);
		error = "waterbox.config is missing memoryLayoutMiB[5]";
		return false;
	}
	for (int i = 0; i < 5; i++)
	{
		cfg.layoutMiB[i] = static_cast<uint64_t>(cJSON_GetArrayItem(layout, i)->valuedouble);
	}

	const cJSON *video = cJSON_GetObjectItemCaseSensitive(root, "video");
	const cJSON *audio = cJSON_GetObjectItemCaseSensitive(root, "audio");
	const cJSON *input = cJSON_GetObjectItemCaseSensitive(root, "input");
	if (!cJSON_IsObject(video) || !cJSON_IsObject(audio) || !cJSON_IsObject(input))
	{
		cJSON_Delete(root);
		error = "waterbox.config is missing video/audio/input";
		return false;
	}
	cfg.width = intOf(video, "width");
	cfg.height = intOf(video, "height");
	cfg.virtualWidth = intOf(video, "virtualWidth", cfg.width);
	cfg.virtualHeight = intOf(video, "virtualHeight", cfg.height);
	cfg.vsyncNum = intOf(video, "vsyncNumerator");
	cfg.vsyncDen = intOf(video, "vsyncDenominator");
	cfg.getBgra = strOf(video, "getBgra", "GetVideoBgra");
	cfg.samplesPerFrame = intOf(audio, "samplesPerFrame");
	cfg.channels = intOf(audio, "channels", 1);
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
			AxisDecl axis;
			axis.name = strOf(item, "name");
			axis.min = intOf(item, "min");
			axis.max = intOf(item, "max");
			axis.neutral = intOf(item, "neutral");
			cfg.axes.push_back(std::move(axis));
		}
	}
	const cJSON *lag = cJSON_GetObjectItemCaseSensitive(root, "lag");
	if (cJSON_IsObject(lag)) cfg.inputWasRead = strOf(lag, "inputWasRead");

	/* every declared setting at its default, kept for open AND for live
	 * re-applies - what the C# EffectiveSettings started from */
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
			error = "settings overrides are not readable JSON";
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

struct ce_session
{
	SessionConfig cfg;
	const chimera::HostApi *host = nullptr;
	void *obj = nullptr;
	bool active = false;

	// the mounted byte sources must outlive the mounts
	std::vector<uint8_t> wbxBytes, romBytes;
	std::string settingsBytes;
	std::vector<std::vector<uint8_t>> firmwareBytes;
	std::vector<ByteStream> streams; // stable addresses: reserved up front

	// guest entry points (already bridged to our convention)
	void (*frameAdvance)(uint64_t) = nullptr;
	uintptr_t (*getVideoBgra)() = nullptr;
	uintptr_t (*getAudio)() = nullptr;
	int32_t (*inputWasRead)() = nullptr;
	int32_t (*getAudioSampleCount)() = nullptr;
	void (*setAxis)(int32_t, int32_t) = nullptr;
	int32_t (*mdCount)() = nullptr;
	uintptr_t (*mdName)(int32_t) = nullptr;
	uintptr_t (*mdPtr)(int32_t) = nullptr;
	int64_t (*mdSize)(int32_t) = nullptr;
	int32_t (*mdWritable)(int32_t) = nullptr;

	int32_t vsyncNum = 0, vsyncDen = 0;
	std::vector<uint32_t> videoBuf;
	std::vector<int16_t> audioBuf;
	int32_t sampleCount = 0;
	std::vector<uint8_t> stateBuf;
	std::string error;

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

	uintptr_t proc(const char *name, int argCount, bool required, std::string &err)
	{
		chimera::WbxReturn r{};
		host->wbx_get_proc_addr(obj, name, &r);
		if (!r.ok()) { err = r.errorMessage; return 0; }
		if (r.data == 0)
		{
			if (required) err = std::string(cfg.coreName) + ": core.wbx exports no " + name;
			return 0;
		}
		uintptr_t bridged = chimera::bridgeGuestCall(r.data, argCount);
		if (bridged == 0) err = "could not bridge a guest call";
		return bridged;
	}
};

extern "C" {

ce_session *ce_session_open(
	const char *package_path,
	const uint8_t *rom, uint64_t rom_len,
	const char *settings_overrides_json,
	const char *const *firmware_ids, const uint8_t *const *firmware_data,
	const uint64_t *firmware_lens, int32_t firmware_count,
	const char **error_out)
{
	auto fail = [&](std::string message) -> ce_session *
	{
		g_openError = std::move(message);
		if (error_out != nullptr) *error_out = g_openError.c_str();
		return nullptr;
	};
	if (error_out != nullptr) *error_out = nullptr;

	const char *hostError = nullptr;
	const chimera::HostApi *host = chimera::hostApi(&hostError);
	if (host == nullptr) return fail(hostError);

	const char *pkgError = nullptr;
	ce_package *pkg = ce_package_open(package_path, &pkgError);
	if (pkg == nullptr)
	{
		return fail(pkgError != nullptr ? pkgError : std::string(package_path) + " is not a core package");
	}
	if (!ce_package_is_waterbox(pkg))
	{
		ce_package_free(pkg);
		return fail(std::string(package_path) + " is not a waterbox core package");
	}

	auto *s = new ce_session();
	s->host = host;
	auto abort = [&](std::string message) -> ce_session *
	{
		ce_package_free(pkg);
		ce_session_free(s);
		return fail(std::move(message));
	};

	uint64_t len = 0;
	const uint8_t *entry = ce_package_entry(pkg, "waterbox.config", &len);
	if (entry == nullptr) return abort("package has no waterbox.config");
	std::string cfgError;
	if (!parseConfig(reinterpret_cast<const char *>(entry), len, settings_overrides_json, s->cfg, cfgError))
	{
		return abort(std::move(cfgError));
	}
	entry = ce_package_entry(pkg, "core.wbx", &len);
	if (entry == nullptr) return abort("package has no core.wbx");
	s->wbxBytes.assign(entry, entry + len);
	ce_package_free(pkg);

	if (rom != nullptr && rom_len != 0) s->romBytes.assign(rom, rom + rom_len);
	s->settingsBytes = s->cfg.settingsJson;

	// every mounted stream needs a stable address for the host's callback
	s->streams.reserve(3 + static_cast<size_t>(firmware_count));

	chimera::WbxLayout layout{};
	layout.sbrkSize = static_cast<uintptr_t>(s->cfg.layoutMiB[0] << 20);
	layout.sealedSize = static_cast<uintptr_t>(s->cfg.layoutMiB[1] << 20);
	layout.invisSize = static_cast<uintptr_t>(s->cfg.layoutMiB[2] << 20);
	layout.plainSize = static_cast<uintptr_t>(s->cfg.layoutMiB[3] << 20);
	layout.mmapSize = static_cast<uintptr_t>(s->cfg.layoutMiB[4] << 20);

	chimera::WbxReturn r{};
	s->streams.push_back({ s->wbxBytes.data(), s->wbxBytes.size() });
	host->wbx_create_host(&layout, "core.wbx", streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), &r);
	if (!r.ok()) return abort(r.errorMessage);
	s->obj = reinterpret_cast<void *>(r.data);

	s->streams.push_back({ s->romBytes.data(), s->romBytes.size() });
	host->wbx_mount_file(s->obj, s->cfg.romFile.c_str(), streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
	if (!r.ok()) return abort(r.errorMessage);

	/* the settings channel: always mounted (empty object when the core has no
	 * settings), so the guest ABI is uniform */
	s->streams.push_back({ reinterpret_cast<const uint8_t *>(s->settingsBytes.data()), s->settingsBytes.size() });
	host->wbx_mount_file(s->obj, "settings", streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
	if (!r.ok()) return abort(r.errorMessage);

	for (int32_t i = 0; i < firmware_count; i++)
	{
		s->firmwareBytes.emplace_back(firmware_data[i], firmware_data[i] + firmware_lens[i]);
		s->streams.push_back({ s->firmwareBytes.back().data(), s->firmwareBytes.back().size() });
		host->wbx_mount_file(s->obj, firmware_ids[i], streamRead, reinterpret_cast<uintptr_t>(&s->streams.back()), 0, &r);
		if (!r.ok()) return abort(r.errorMessage);
	}

	std::string err;
	if (!s->activate(err)) return abort(std::move(err));

	auto init = reinterpret_cast<int32_t (*)()>(s->proc("Init", 0, true, err));
	if (init == nullptr) return abort(std::move(err));
	if (init() != 1)
	{
		/* the core knows why it refused - GetLoadError is optional, so a core
		 * that says nothing still fails, just less helpfully */
		std::string reason;
		auto getLoadError = reinterpret_cast<uintptr_t (*)()>(s->proc("GetLoadError", 0, false, err));
		if (getLoadError != nullptr)
		{
			const char *text = reinterpret_cast<const char *>(getLoadError());
			if (text != nullptr) reason = text;
		}
		return abort(reason.empty()
			? s->cfg.coreName + " could not load this file."
			: s->cfg.coreName + ": " + reason);
	}

	if (!s->deactivate(err)) return abort(std::move(err));
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
			return abort(s->cfg.coreName + ": waterbox.config declares axes but core.wbx exports no SetAxis");
		}
	}
	s->getVideoBgra = reinterpret_cast<uintptr_t (*)()>(s->proc(s->cfg.getBgra.c_str(), 0, true, err));
	if (s->getVideoBgra == nullptr) return abort(std::move(err));
	s->getAudio = reinterpret_cast<uintptr_t (*)()>(s->proc(s->cfg.getAudio.c_str(), 0, true, err));
	if (s->getAudio == nullptr) return abort(std::move(err));

	/* optional exports: the guest's own answers override the config's */
	s->getAudioSampleCount = reinterpret_cast<int32_t (*)()>(s->proc("GetAudioSampleCount", 0, false, err));
	auto vsyncN = reinterpret_cast<int32_t (*)()>(s->proc("GetVsyncNumerator", 0, false, err));
	auto vsyncD = reinterpret_cast<int32_t (*)()>(s->proc("GetVsyncDenominator", 0, false, err));
	s->vsyncNum = vsyncN != nullptr ? vsyncN() : 0;
	s->vsyncDen = vsyncD != nullptr ? vsyncD() : 0;
	if (s->vsyncNum <= 0 || s->vsyncDen <= 0)
	{
		s->vsyncNum = s->cfg.vsyncNum;
		s->vsyncDen = s->cfg.vsyncDen;
	}
	if (!s->cfg.inputWasRead.empty())
	{
		s->inputWasRead = reinterpret_cast<int32_t (*)()>(s->proc(s->cfg.inputWasRead.c_str(), 0, true, err));
		if (s->inputWasRead == nullptr) return abort(std::move(err));
	}

	/* memory domains are self-described post-Init (size can depend on settings) */
	s->mdCount = reinterpret_cast<int32_t (*)()>(s->proc("GetMemoryDomainCount", 0, true, err));
	s->mdName = reinterpret_cast<uintptr_t (*)(int32_t)>(s->proc("GetMemoryDomainName", 1, true, err));
	s->mdPtr = reinterpret_cast<uintptr_t (*)(int32_t)>(s->proc("GetMemoryDomainPtr", 1, true, err));
	s->mdSize = reinterpret_cast<int64_t (*)(int32_t)>(s->proc("GetMemoryDomainSize", 1, true, err));
	s->mdWritable = reinterpret_cast<int32_t (*)(int32_t)>(s->proc("GetMemoryDomainWritable", 1, true, err));
	if (s->mdWritable == nullptr) return abort(std::move(err));

	s->videoBuf.assign(static_cast<size_t>(s->cfg.width) * s->cfg.height, 0);
	s->audioBuf.assign(static_cast<size_t>(s->cfg.samplesPerFrame) * 2, 0);
	return s;
}

void ce_session_free(ce_session *s)
{
	if (s == nullptr) return;
	if (s->obj != nullptr)
	{
		std::string err;
		s->deactivate(err); // tearing down anyway
		chimera::WbxReturn r{};
		s->host->wbx_destroy_host(s->obj, &r);
	}
	delete s;
}

const char *ce_session_core_name(const ce_session *s) { return s->cfg.coreName.c_str(); }
const char *ce_session_system_id(const ce_session *s) { return s->cfg.systemId.c_str(); }
int32_t ce_session_width(const ce_session *s) { return s->cfg.width; }
int32_t ce_session_height(const ce_session *s) { return s->cfg.height; }
int32_t ce_session_virtual_width(const ce_session *s) { return s->cfg.virtualWidth; }
int32_t ce_session_virtual_height(const ce_session *s) { return s->cfg.virtualHeight; }
int32_t ce_session_vsync_numerator(const ce_session *s) { return s->vsyncNum; }
int32_t ce_session_vsync_denominator(const ce_session *s) { return s->vsyncDen; }
int32_t ce_session_samples_per_frame(const ce_session *s) { return s->cfg.samplesPerFrame; }
int32_t ce_session_channels(const ce_session *s) { return s->cfg.channels; }
int32_t ce_session_deterministic(const ce_session *s) { return s->cfg.deterministic ? 1 : 0; }
int64_t ce_session_button_count(const ce_session *s) { return static_cast<int64_t>(s->cfg.buttons.size()); }

const char *ce_session_button_name(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->cfg.buttons.size())) return nullptr;
	return s->cfg.buttons[static_cast<size_t>(index)].c_str();
}

int64_t ce_session_axis_count(const ce_session *s) { return static_cast<int64_t>(s->cfg.axes.size()); }

const char *ce_session_axis_name(const ce_session *s, int64_t index)
{
	if (index < 0 || index >= static_cast<int64_t>(s->cfg.axes.size())) return nullptr;
	return s->cfg.axes[static_cast<size_t>(index)].name.c_str();
}

void ce_session_set_axis(ce_session *s, int32_t index, int32_t value)
{
	if (s->setAxis != nullptr) s->setAxis(index, value);
}

int32_t ce_session_frame_advance(ce_session *s, uint64_t buttons, int32_t render)
{
	s->frameAdvance(buttons);
	if (render != 0)
	{
		std::memcpy(s->videoBuf.data(), reinterpret_cast<const void *>(s->getVideoBgra()),
			s->videoBuf.size() * sizeof(uint32_t));
	}
	/* a core that reports its own count may produce a different number every
	 * frame (blip resamplers do); the declared samplesPerFrame is the buffer
	 * we must not overrun */
	int32_t nsamp = s->getAudioSampleCount != nullptr ? s->getAudioSampleCount() : s->cfg.samplesPerFrame;
	if (nsamp < 0) nsamp = 0;
	if (nsamp > s->cfg.samplesPerFrame) nsamp = s->cfg.samplesPerFrame;
	const auto *src = reinterpret_cast<const int16_t *>(s->getAudio());
	if (s->cfg.channels == 2)
	{
		std::memcpy(s->audioBuf.data(), src, static_cast<size_t>(nsamp) * 2 * sizeof(int16_t));
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
	return s->inputWasRead != nullptr && s->inputWasRead() == 0 ? 1 : 0;
}

const uint32_t *ce_session_video(const ce_session *s) { return s->videoBuf.data(); }

const int16_t *ce_session_audio(const ce_session *s, int32_t *sample_count)
{
	if (sample_count != nullptr) *sample_count = s->sampleCount;
	return s->audioBuf.data();
}

const uint8_t *ce_session_save_state(ce_session *s, uint64_t *len_out)
{
	s->error.clear();
	s->stateBuf.clear();
	chimera::WbxReturn r{};
	/* no deactivate/activate bracket: the host activates itself for the
	 * duration - bracketing costs four full guest-address-space remaps per
	 * state, which at rewind's one state per frame was a 10x slowdown */
	s->host->wbx_save_state(s->obj, vectorWrite, reinterpret_cast<uintptr_t>(&s->stateBuf), &r);
	if (!r.ok())
	{
		s->error = r.errorMessage;
		return nullptr;
	}
	if (len_out != nullptr) *len_out = s->stateBuf.size();
	return s->stateBuf.data();
}

int32_t ce_session_load_state(ce_session *s, const uint8_t *data, uint64_t len)
{
	s->error.clear();
	ByteStream stream{ data, len };
	chimera::WbxReturn r{};
	s->host->wbx_load_state(s->obj, streamRead, reinterpret_cast<uintptr_t>(&stream), &r); // see save re: no bracket
	if (!r.ok())
	{
		s->error = r.errorMessage;
		return 1;
	}
	return 0;
}

int32_t ce_session_domain_count(const ce_session *s) { return s->mdCount(); }

const char *ce_session_domain_name(const ce_session *s, int32_t index)
{
	return reinterpret_cast<const char *>(s->mdName(index));
}

int64_t ce_session_domain_size(const ce_session *s, int32_t index) { return s->mdSize(index); }

int32_t ce_session_domain_writable(const ce_session *s, int32_t index) { return s->mdWritable(index); }

int64_t ce_session_domain_read(const ce_session *s, int32_t index, int64_t offset, uint8_t *buf, int64_t len)
{
	int64_t size = s->mdSize(index);
	if (offset < 0 || offset >= size || len <= 0) return 0;
	int64_t n = len < size - offset ? len : size - offset;
	const auto *src = reinterpret_cast<const uint8_t *>(s->mdPtr(index));
	if (src == nullptr) return 0;
	std::memcpy(buf, src + offset, static_cast<size_t>(n));
	return n;
}

const char *ce_session_last_error(ce_session *s) { return s->error.c_str(); }

const char *ce_host_build_info(void)
{
	const char *error = nullptr;
	const chimera::HostApi *host = chimera::hostApi(&error);
	return host != nullptr ? host->wbx_build_info() : nullptr;
}

uint64_t ce_session_domain_ptr(const ce_session *s, int32_t index)
{
	return static_cast<uint64_t>(s->mdPtr(index));
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
		s->error = s->cfg.coreName + ": settings JSON is " + std::to_string(json.size())
			+ " bytes but the core's buffer holds " + std::to_string(cap);
		return 2;
	}
	uintptr_t dest = buffer();
	if (dest == 0) return 1;
	std::memcpy(reinterpret_cast<void *>(dest), json.data(), json.size());
	apply(static_cast<int32_t>(json.size()));
	return 0;
}

uint64_t ce_session_guest_proc(ce_session *s, const char *name, int32_t arg_count)
{
	std::string err;
	return static_cast<uint64_t>(s->proc(name, arg_count, false, err));
}

} // extern "C"
