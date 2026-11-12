#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/json.hpp"
#include "chimera/vm/tr.hpp"

#include <cstdint>
#include <cstdio>
#include <map>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace chimera::vm {

/* SDL3's stable C interface, the little of it used here, declared rather than
 * included so the view models build without SDL's headers. */
namespace {
constexpr uint32_t SDL_INIT_JOYSTICK = 0x00000200u, SDL_INIT_GAMEPAD = 0x00002000u;
constexpr uint8_t SDL_HAT_UP = 1, SDL_HAT_RIGHT = 2, SDL_HAT_DOWN = 4, SDL_HAT_LEFT = 8;
/* SDL_GamepadButton and SDL_GamepadAxis, in their order. The first four are
 * the face buttons by place (bottom, right, left, top), named as on an Xbox
 * pad whatever the pad: a binding means the same button on every pad, and
 * shown() gives the letter or shape the pad in use prints on it */
const char *const kButtons[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "LeftThumb", "RightThumb",
	"LeftShoulder", "RightShoulder", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Misc",
	"Paddle1", "Paddle2", "Paddle3", "Paddle4"};
/* SDL_GamepadButtonLabel, from SDL_GAMEPAD_BUTTON_LABEL_A (1); 0 is unknown */
const char *const kLabels[] = {"A", "B", "X", "Y", "Cross", "Circle", "Square", "Triangle"};
/* the layouts a person can choose for a pad model (setLayouts), and what each
 * prints on the face buttons, bottom, right, left and top */
struct Layout
{
	const char *name;
	const char *face[4];
};
const Layout kLayouts[] = {
	{"xbox", {"A", "B", "X", "Y"}},
	{"nintendo", {"B", "A", "Y", "X"}},
	{"playstation", {"Cross", "Circle", "Square", "Triangle"}},
};
const Layout *layoutNamed(const std::string &name)
{
	for (const Layout &l : kLayouts)
		if (name == l.name) return &l;
	return nullptr;
}
enum { AXIS_LEFTX, AXIS_LEFTY, AXIS_RIGHTX, AXIS_RIGHTY, AXIS_TRIGGERLEFT, AXIS_TRIGGERRIGHT };
/* a stick counts as pressed past 40%, a trigger past about 15% */
constexpr int kStick = static_cast<int>(32768 / 2.5), kTrigger = static_cast<int>(32768 / 6.5);
} // namespace

struct Gamepads::Api
{
	void *lib = nullptr;
	bool (*Init)(uint32_t) = nullptr;
	void (*QuitSubSystem)(uint32_t) = nullptr;
	bool (*SetHint)(const char *, const char *) = nullptr;
	const char *(*GetError)() = nullptr;
	void (*Free)(void *) = nullptr;
	uint32_t *(*GetJoysticks)(int *) = nullptr;
	bool (*IsGamepad)(uint32_t) = nullptr;
	void *(*OpenGamepad)(uint32_t) = nullptr;
	void (*CloseGamepad)(void *) = nullptr;
	const char *(*GetGamepadName)(void *) = nullptr;
	bool (*GetGamepadButton)(void *, int) = nullptr;
	int (*GetGamepadButtonLabel)(void *, int) = nullptr;
	uint16_t (*GetGamepadVendor)(void *) = nullptr;
	uint16_t (*GetGamepadProduct)(void *) = nullptr;
	int16_t (*GetGamepadAxis)(void *, int) = nullptr;
	void *(*OpenJoystick)(uint32_t) = nullptr;
	void (*CloseJoystick)(void *) = nullptr;
	const char *(*GetJoystickName)(void *) = nullptr;
	int (*GetNumJoystickButtons)(void *) = nullptr;
	int (*GetNumJoystickHats)(void *) = nullptr;
	int (*GetNumJoystickAxes)(void *) = nullptr;
	int16_t (*GetJoystickAxis)(void *, int) = nullptr;
	bool (*GetJoystickButton)(void *, int) = nullptr;
	uint8_t (*GetJoystickHat)(void *, int) = nullptr;
	void (*UpdateJoysticks)() = nullptr;
	bool started = false;

	/* the joysticks plugged in now, by SDL's instance numbers, in SDL's order */
	std::vector<uint32_t> joysticks()
	{
		int n = 0;
		uint32_t *ids = GetJoysticks(&n);
		std::vector<uint32_t> out;
		if (ids == nullptr) return out;
		out.assign(ids, ids + n);
		Free(ids);
		return out;
	}

	void *sym(const char *name)
	{
#ifdef _WIN32
		return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
		return dlsym(lib, name);
#endif
	}

	template <class F>
	bool bind(F &f, const char *name)
	{
		f = reinterpret_cast<F>(sym(name));
		return f != nullptr;
	}
};

struct Gamepads::Pad
{
	void *handle = nullptr;
	bool controller = false;
	std::string prefix, name;
	/* what the pad prints on its face buttons, bottom, right, left and
	 * top; null where SDL does not know */
	const char *face[4] = {nullptr, nullptr, nullptr, nullptr};
	/* its model, by USB vendor and product ("2dc8:5112"), for a layout chosen for it */
	std::string model;
	int buttons = 0, hats = 0, axes = 0;
};

Gamepads::Gamepads() : api_(std::make_unique<Api>())
{
	Api &a = *api_;
#ifdef _WIN32
	a.lib = LoadLibraryW(L"SDL3.dll");
#elif defined(__APPLE__)
	/* the program's own folder (a build) and the bundle's Frameworks, then
	 * wherever the loader looks by name */
	for (const char *name : {"@executable_path/libSDL3.dylib", "@executable_path/../Frameworks/libSDL3.dylib",
	                         "@rpath/libSDL3.dylib", "libSDL3.dylib", "libSDL3.0.dylib"})
		if (a.lib == nullptr) a.lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#else
	/* Chimera's own, then the system's */
	a.lib = dlopen("libSDL3.so", RTLD_NOW | RTLD_LOCAL);
	if (a.lib == nullptr) a.lib = dlopen("libSDL3.so.0", RTLD_NOW | RTLD_LOCAL);
#endif
	if (a.lib == nullptr)
	{
		problem_ = tr("gamepads.no_sdl");
		return;
	}
	const bool all = a.bind(a.Init, "SDL_Init") && a.bind(a.QuitSubSystem, "SDL_QuitSubSystem") &&
		a.bind(a.SetHint, "SDL_SetHint") && a.bind(a.GetError, "SDL_GetError") && a.bind(a.Free, "SDL_free") &&
		a.bind(a.GetJoysticks, "SDL_GetJoysticks") && a.bind(a.IsGamepad, "SDL_IsGamepad") &&
		a.bind(a.OpenGamepad, "SDL_OpenGamepad") && a.bind(a.CloseGamepad, "SDL_CloseGamepad") &&
		a.bind(a.GetGamepadName, "SDL_GetGamepadName") &&
		a.bind(a.GetGamepadButton, "SDL_GetGamepadButton") &&
		a.bind(a.GetGamepadButtonLabel, "SDL_GetGamepadButtonLabel") &&
		a.bind(a.GetGamepadVendor, "SDL_GetGamepadVendor") && a.bind(a.GetGamepadProduct, "SDL_GetGamepadProduct") &&
		a.bind(a.GetGamepadAxis, "SDL_GetGamepadAxis") &&
		a.bind(a.OpenJoystick, "SDL_OpenJoystick") && a.bind(a.CloseJoystick, "SDL_CloseJoystick") &&
		a.bind(a.GetJoystickName, "SDL_GetJoystickName") &&
		a.bind(a.GetNumJoystickButtons, "SDL_GetNumJoystickButtons") &&
		a.bind(a.GetNumJoystickHats, "SDL_GetNumJoystickHats") &&
		a.bind(a.GetNumJoystickAxes, "SDL_GetNumJoystickAxes") &&
		a.bind(a.GetJoystickAxis, "SDL_GetJoystickAxis") &&
		a.bind(a.GetJoystickButton, "SDL_GetJoystickButton") &&
		a.bind(a.GetJoystickHat, "SDL_GetJoystickHat") && a.bind(a.UpdateJoysticks, "SDL_UpdateJoysticks");
	if (!all)
	{
		problem_ = tr("gamepads.sdl_incomplete");
		return;
	}
	/* the window is never SDL's, so SDL must not wait for its focus */
	a.SetHint("SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS", "1");
	/* nor take Ctrl+C and the request to end (SIGINT, SIGTERM): SDL would turn
	 * them into a quit event nobody reads, and the program could not be ended
	 * by them - a run nobody watches outlived its time limit */
	a.SetHint("SDL_NO_SIGNAL_HANDLERS", "1");
	if (!a.Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
	{
		problem_ = tr("gamepads.sdl_refused", {a.GetError()});
		return;
	}
	a.started = true;
	reopen();
}

Gamepads::~Gamepads()
{
	Api &a = *api_;
	if (a.started)
	{
		for (Pad &p : pads_)
			if (p.controller) a.CloseGamepad(p.handle);
			else a.CloseJoystick(p.handle);
		a.QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
	}
	/* SDL stays loaded: unloading a library that started threads is not safe */
}

bool Gamepads::available() const { return api_->started; }

bool Gamepads::isPadInput(const std::string &name)
{
	/* the pad's prefix, as reopen() makes it: X or J, its number from 1, a space */
	if (name.size() < 4 || (name[0] != 'X' && name[0] != 'J')) return false;
	size_t i = 1;
	while (i < name.size() && name[i] >= '0' && name[i] <= '9') i++;
	return i > 1 && i + 1 < name.size() && name[i] == ' ';
}

std::vector<std::string> Gamepads::names() const
{
	std::vector<std::string> out;
	for (const Pad &p : pads_) out.push_back(p.prefix.substr(0, p.prefix.size() - 1) + " (" + p.name + ")");
	return out;
}

void Gamepads::setLayouts(const std::map<std::string, std::string> &byModel) { layouts_ = byModel; }

void Gamepads::setSwapped(const std::set<std::string> &models) { swapped_ = models; }

/* the SDL button at a face button's place: a pad model that reports its face
 * buttons by their letters (setSwapped) has A and B, and X and Y, the other
 * way round (bottom 0 and right 1, left 2 and top 3) */
static int sdlButton(bool swapped, int b) { return swapped && b < 4 ? b ^ 1 : b; }

/* what SDL says a pad prints, place by place, the swap taken into account */
static void sdlFace(const char *const face[4], bool swapped, const char *out[4])
{
	for (int b = 0; b < 4; b++) out[b] = face[sdlButton(swapped, b)];
}

/* what a pad prints on its face buttons: the layout chosen for its model, else SDL's; false when not known */
static bool padFace(const std::map<std::string, std::string> &layouts, const std::string &model, const char *const sdl[4],
	const char *out[4])
{
	const auto chosen = layouts.find(model);
	if (const Layout *l = chosen != layouts.end() ? layoutNamed(chosen->second) : nullptr)
	{
		for (int b = 0; b < 4; b++) out[b] = l->face[b];
		return true;
	}
	for (int b = 0; b < 4; b++)
	{
		out[b] = sdl[b];
		if (out[b] == nullptr) return false;
	}
	return true;
}

std::string Gamepads::faces() const
{
	std::string out;
	for (const Pad &p : pads_)
	{
		const char *face[4];
		const char *sdl[4];
		sdlFace(p.face, swapped_.count(p.model) != 0, sdl);
		if (!p.controller || !padFace(layouts_, p.model, sdl, face)) continue;
		if (!out.empty()) out += ";";
		out += p.prefix.substr(0, p.prefix.size() - 1) + ":" + face[0] + "," + face[1] + "," + face[2] + "," + face[3];
	}
	return out;
}

std::string Gamepads::models() const
{
	json::Json list;
	list.kind = json::Json::Kind::Array;
	for (const Pad &p : pads_)
	{
		if (!p.controller) continue;
		/* the layout SDL's own labels make, whatever is chosen */
		std::string detected = "unknown";
		for (const Layout &l : kLayouts)
		{
			bool same = true;
			const char *sdl[4];
			sdlFace(p.face, swapped_.count(p.model) != 0, sdl);
			for (int b = 0; b < 4; b++) same = same && sdl[b] != nullptr && std::string(sdl[b]) == l.face[b];
			if (same) detected = l.name;
		}
		json::Json pad;
		pad.kind = json::Json::Kind::Object;
		pad.members.emplace_back("pad", json::Json::string(p.prefix.substr(0, p.prefix.size() - 1)));
		pad.members.emplace_back("name", json::Json::string(p.name));
		pad.members.emplace_back("model", json::Json::string(p.model));
		pad.members.emplace_back("detected", json::Json::string(detected));
		list.items.push_back(std::move(pad));
	}
	return json::write(list);
}

bool Gamepads::isLayout(const std::string &name) { return layoutNamed(name) != nullptr; }

std::string Gamepads::shown(const std::string &keys, const std::string &faces)
{
	if (faces.empty()) return keys;
	/* faces: "X1:Cross,Circle,Square,Triangle;X2:B,A,Y,X" */
	std::map<std::string, std::vector<std::string>> labels;
	size_t at = 0;
	while (at < faces.size())
	{
		size_t end = faces.find(';', at);
		if (end == std::string::npos) end = faces.size();
		const std::string pad = faces.substr(at, end - at);
		const size_t colon = pad.find(':');
		if (colon != std::string::npos)
		{
			std::vector<std::string> four;
			size_t from = colon + 1;
			while (from <= pad.size())
			{
				size_t comma = pad.find(',', from);
				if (comma == std::string::npos) comma = pad.size();
				four.push_back(pad.substr(from, comma - from));
				from = comma + 1;
			}
			if (four.size() == 4) labels[pad.substr(0, colon)] = std::move(four);
		}
		at = end + 1;
	}
	/* a whole name only, between the separators of a binding ("X1 A, Z") */
	std::string out;
	at = 0;
	while (at < keys.size())
	{
		size_t end = keys.find_first_of("+,", at);
		if (end == std::string::npos) end = keys.size();
		std::string part = keys.substr(at, end - at);
		const size_t first = part.find_first_not_of(' '), last = part.find_last_not_of(' ');
		if (first != std::string::npos)
		{
			const std::string name = part.substr(first, last - first + 1);
			const size_t space = name.find(' ');
			if (space != std::string::npos && name[0] == 'X')
			{
				const auto pad = labels.find(name.substr(0, space));
				const std::string button = name.substr(space + 1);
				if (pad != labels.end())
					for (int b = 0; b < 4; b++)
						if (button == kButtons[b])
							part = part.substr(0, first) + pad->first + " " + pad->second[static_cast<size_t>(b)] +
								part.substr(last + 1);
			}
		}
		out += part;
		if (end < keys.size()) out += keys[end];
		at = end + 1;
	}
	return out;
}

void Gamepads::reopen()
{
	Api &a = *api_;
	for (Pad &p : pads_)
		if (p.controller) a.CloseGamepad(p.handle);
		else a.CloseJoystick(p.handle);
	pads_.clear();
	ids_ = a.joysticks();
	for (size_t i = 0; i < ids_.size(); i++)
	{
		const uint32_t id = ids_[i];
		Pad p;
		p.controller = a.IsGamepad(id);
		p.handle = p.controller ? a.OpenGamepad(id) : a.OpenJoystick(id);
		if (p.handle == nullptr) continue;
		p.prefix = (p.controller ? "X" : "J") + std::to_string(i + 1) + " ";
		const char *name = p.controller ? a.GetGamepadName(p.handle) : a.GetJoystickName(p.handle);
		p.name = name != nullptr ? name : "";
		if (p.controller)
		{
			/* what it prints on them: a Nintendo pad's bottom button is B */
			for (int b = 0; b < 4; b++)
			{
				const int label = a.GetGamepadButtonLabel(p.handle, b);
				if (label >= 1 && label <= 8) p.face[b] = kLabels[label - 1];
			}
			char model[10];
			std::snprintf(model, sizeof model, "%04x:%04x", a.GetGamepadVendor(p.handle), a.GetGamepadProduct(p.handle));
			p.model = model;
		}
		else
		{
			p.buttons = a.GetNumJoystickButtons(p.handle);
			p.hats = a.GetNumJoystickHats(p.handle);
			p.axes = a.GetNumJoystickAxes(p.handle);
		}
		pads_.push_back(std::move(p));
	}
}

void Gamepads::poll(std::vector<std::string> &held, std::vector<std::pair<std::string, int>> *axes)
{
	if (axes != nullptr) axes->clear();
	/* SDL's -32768..32767 as Chimera's -10000..10000 */
	auto conv = [](int v) { return static_cast<int>(v / (32768 / 10000.0f)); };
	held.clear();
	Api &a = *api_;
	if (!a.started) return;
#ifdef _WIN32
	/* SDL's device notices arrive as window messages to this thread */
	MSG msg;
	while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
#endif
	a.UpdateJoysticks();

	/* a pad plugged in or out: open them all again, in SDL's new order */
	if (a.joysticks() != ids_) reopen();

	for (const Pad &p : pads_)
	{
		if (p.controller)
		{
			const bool swapped = swapped_.count(p.model) != 0;
			for (int b = 0; b < static_cast<int>(sizeof kButtons / sizeof kButtons[0]); b++)
				if (a.GetGamepadButton(p.handle, sdlButton(swapped, b))) held.push_back(p.prefix + kButtons[b]);
			/* SDL's y axes are negative upwards */
			const int lx = a.GetGamepadAxis(p.handle, AXIS_LEFTX), ly = a.GetGamepadAxis(p.handle, AXIS_LEFTY);
			const int rx = a.GetGamepadAxis(p.handle, AXIS_RIGHTX), ry = a.GetGamepadAxis(p.handle, AXIS_RIGHTY);
			if (ly <= -kStick) held.push_back(p.prefix + "LStickUp");
			if (ly >= kStick) held.push_back(p.prefix + "LStickDown");
			if (lx <= -kStick) held.push_back(p.prefix + "LStickLeft");
			if (lx >= kStick) held.push_back(p.prefix + "LStickRight");
			if (ry <= -kStick) held.push_back(p.prefix + "RStickUp");
			if (ry >= kStick) held.push_back(p.prefix + "RStickDown");
			if (rx <= -kStick) held.push_back(p.prefix + "RStickLeft");
			if (rx >= kStick) held.push_back(p.prefix + "RStickRight");
			if (a.GetGamepadAxis(p.handle, AXIS_TRIGGERLEFT) > kTrigger) held.push_back(p.prefix + "LeftTrigger");
			if (a.GetGamepadAxis(p.handle, AXIS_TRIGGERRIGHT) > kTrigger) held.push_back(p.prefix + "RightTrigger");
			if (axes != nullptr)
			{
				/* as Chimera's gamepads have always named them, the y axes as SDL gives them */
				axes->emplace_back(p.prefix + "LeftThumbX Axis", conv(lx));
				axes->emplace_back(p.prefix + "LeftThumbY Axis", conv(ly));
				axes->emplace_back(p.prefix + "RightThumbX Axis", conv(rx));
				axes->emplace_back(p.prefix + "RightThumbY Axis", conv(ry));
				axes->emplace_back(p.prefix + "LeftTrigger Axis", conv(a.GetGamepadAxis(p.handle, AXIS_TRIGGERLEFT)));
				axes->emplace_back(p.prefix + "RightTrigger Axis", conv(a.GetGamepadAxis(p.handle, AXIS_TRIGGERRIGHT)));
			}
		}
		else
		{
			/* the first nine axes by DirectInput's letters, the rest by number */
			static const char *const letters[] = {"X", "Y", "Z", "W", "V", "S", "Q", "P", "N"};
			for (int x = 0; x < p.axes; x++)
			{
				const int v = a.GetJoystickAxis(p.handle, x);
				const std::string axis = p.prefix + (x < 9 ? std::string(letters[x]) : "Axis" + std::to_string(x));
				if (axes != nullptr) axes->emplace_back(axis + " Axis", conv(v));
				if (v >= kStick) held.push_back(axis + "+");
				if (v <= -kStick) held.push_back(axis + "-");
			}
			for (int b = 0; b < p.buttons; b++)
				if (a.GetJoystickButton(p.handle, b)) held.push_back(p.prefix + "B" + std::to_string(b + 1));
			for (int h = 0; h < p.hats; h++)
			{
				const uint8_t v = a.GetJoystickHat(p.handle, h);
				const std::string pov = p.prefix + "POV" + std::to_string(h);
				if (v & SDL_HAT_UP) held.push_back(pov + "U");
				if (v & SDL_HAT_DOWN) held.push_back(pov + "D");
				if (v & SDL_HAT_LEFT) held.push_back(pov + "L");
				if (v & SDL_HAT_RIGHT) held.push_back(pov + "R");
			}
		}
	}
}

} // namespace chimera::vm
