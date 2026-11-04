#include "chimera/vm/gamepads.hpp"
#include "chimera/vm/tr.hpp"

#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace chimera::vm {

/* SDL2's stable C interface, the little of it used here, declared rather than
 * included so the view models build without SDL's headers. */
namespace {
constexpr uint32_t SDL_INIT_JOYSTICK = 0x00000200u, SDL_INIT_GAMECONTROLLER = 0x00002000u;
constexpr uint8_t SDL_HAT_UP = 1, SDL_HAT_RIGHT = 2, SDL_HAT_DOWN = 4, SDL_HAT_LEFT = 8;
/* SDL_GameControllerButton and SDL_GameControllerAxis, in their order */
const char *const kButtons[] = {"A", "B", "X", "Y", "Back", "Guide", "Start", "LeftThumb", "RightThumb",
	"LeftShoulder", "RightShoulder", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Misc",
	"Paddle1", "Paddle2", "Paddle3", "Paddle4"};
enum { AXIS_LEFTX, AXIS_LEFTY, AXIS_RIGHTX, AXIS_RIGHTY, AXIS_TRIGGERLEFT, AXIS_TRIGGERRIGHT };
/* a stick counts as pressed past 40%, a trigger past about 15% */
constexpr int kStick = static_cast<int>(32768 / 2.5), kTrigger = static_cast<int>(32768 / 6.5);
} // namespace

struct Gamepads::Api
{
	void *lib = nullptr;
	int (*Init)(uint32_t) = nullptr;
	void (*QuitSubSystem)(uint32_t) = nullptr;
	int (*SetHint)(const char *, const char *) = nullptr;
	const char *(*GetError)() = nullptr;
	int (*NumJoysticks)() = nullptr;
	int32_t (*JoystickGetDeviceInstanceID)(int) = nullptr;
	int (*IsGameController)(int) = nullptr;
	void *(*GameControllerOpen)(int) = nullptr;
	void (*GameControllerClose)(void *) = nullptr;
	const char *(*GameControllerName)(void *) = nullptr;
	uint8_t (*GameControllerGetButton)(void *, int) = nullptr;
	int16_t (*GameControllerGetAxis)(void *, int) = nullptr;
	void *(*JoystickOpen)(int) = nullptr;
	void (*JoystickClose)(void *) = nullptr;
	const char *(*JoystickName)(void *) = nullptr;
	int (*JoystickNumButtons)(void *) = nullptr;
	int (*JoystickNumHats)(void *) = nullptr;
	int (*JoystickNumAxes)(void *) = nullptr;
	int16_t (*JoystickGetAxis)(void *, int) = nullptr;
	uint8_t (*JoystickGetButton)(void *, int) = nullptr;
	uint8_t (*JoystickGetHat)(void *, int) = nullptr;
	void (*JoystickUpdate)() = nullptr;
	bool started = false;

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
	int buttons = 0, hats = 0, axes = 0;
};

Gamepads::Gamepads() : api_(std::make_unique<Api>())
{
	Api &a = *api_;
#ifdef _WIN32
	a.lib = LoadLibraryW(L"SDL2.dll");
#elif defined(__APPLE__)
	/* the program's own folder (a build) and the bundle's Frameworks, then
	 * wherever the loader looks by name */
	for (const char *name : {"@executable_path/libSDL2.dylib", "@executable_path/../Frameworks/libSDL2.dylib",
	                         "@rpath/libSDL2.dylib", "libSDL2.dylib"})
		if (a.lib == nullptr) a.lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
#else
	a.lib = dlopen("libSDL2.so", RTLD_NOW | RTLD_LOCAL);
	if (a.lib == nullptr) a.lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
#endif
	if (a.lib == nullptr)
	{
		problem_ = tr("gamepads.no_sdl");
		return;
	}
	const bool all = a.bind(a.Init, "SDL_Init") && a.bind(a.QuitSubSystem, "SDL_QuitSubSystem") &&
		a.bind(a.SetHint, "SDL_SetHint") && a.bind(a.GetError, "SDL_GetError") &&
		a.bind(a.NumJoysticks, "SDL_NumJoysticks") &&
		a.bind(a.JoystickGetDeviceInstanceID, "SDL_JoystickGetDeviceInstanceID") &&
		a.bind(a.IsGameController, "SDL_IsGameController") &&
		a.bind(a.GameControllerOpen, "SDL_GameControllerOpen") &&
		a.bind(a.GameControllerClose, "SDL_GameControllerClose") &&
		a.bind(a.GameControllerName, "SDL_GameControllerName") &&
		a.bind(a.GameControllerGetButton, "SDL_GameControllerGetButton") &&
		a.bind(a.GameControllerGetAxis, "SDL_GameControllerGetAxis") &&
		a.bind(a.JoystickOpen, "SDL_JoystickOpen") && a.bind(a.JoystickClose, "SDL_JoystickClose") &&
		a.bind(a.JoystickName, "SDL_JoystickName") &&
		a.bind(a.JoystickNumButtons, "SDL_JoystickNumButtons") &&
		a.bind(a.JoystickNumHats, "SDL_JoystickNumHats") &&
		a.bind(a.JoystickNumAxes, "SDL_JoystickNumAxes") && a.bind(a.JoystickGetAxis, "SDL_JoystickGetAxis") &&
		a.bind(a.JoystickGetButton, "SDL_JoystickGetButton") &&
		a.bind(a.JoystickGetHat, "SDL_JoystickGetHat") && a.bind(a.JoystickUpdate, "SDL_JoystickUpdate");
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
	if (a.Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0)
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
			if (p.controller) a.GameControllerClose(p.handle);
			else a.JoystickClose(p.handle);
		a.QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);
	}
	/* SDL2 stays loaded: unloading a library that started threads is not safe */
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

void Gamepads::reopen()
{
	Api &a = *api_;
	for (Pad &p : pads_)
		if (p.controller) a.GameControllerClose(p.handle);
		else a.JoystickClose(p.handle);
	pads_.clear();
	ids_.clear();
	const int n = a.NumJoysticks();
	for (int i = 0; i < n; i++)
	{
		ids_.push_back(a.JoystickGetDeviceInstanceID(i));
		Pad p;
		p.controller = a.IsGameController(i) != 0;
		p.handle = p.controller ? a.GameControllerOpen(i) : a.JoystickOpen(i);
		if (p.handle == nullptr) continue;
		p.prefix = (p.controller ? "X" : "J") + std::to_string(i + 1) + " ";
		const char *name = p.controller ? a.GameControllerName(p.handle) : a.JoystickName(p.handle);
		p.name = name != nullptr ? name : "";
		if (!p.controller)
		{
			p.buttons = a.JoystickNumButtons(p.handle);
			p.hats = a.JoystickNumHats(p.handle);
			p.axes = a.JoystickNumAxes(p.handle);
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
	a.JoystickUpdate();

	/* a pad plugged in or out: open them all again, in SDL's new order */
	const int n = a.NumJoysticks();
	bool same = n == static_cast<int>(ids_.size());
	for (int i = 0; same && i < n; i++) same = a.JoystickGetDeviceInstanceID(i) == ids_[static_cast<size_t>(i)];
	if (!same) reopen();

	for (const Pad &p : pads_)
	{
		if (p.controller)
		{
			for (int b = 0; b < static_cast<int>(sizeof kButtons / sizeof kButtons[0]); b++)
				if (a.GameControllerGetButton(p.handle, b) != 0) held.push_back(p.prefix + kButtons[b]);
			/* SDL's y axes are negative upwards */
			const int lx = a.GameControllerGetAxis(p.handle, AXIS_LEFTX), ly = a.GameControllerGetAxis(p.handle, AXIS_LEFTY);
			const int rx = a.GameControllerGetAxis(p.handle, AXIS_RIGHTX), ry = a.GameControllerGetAxis(p.handle, AXIS_RIGHTY);
			if (ly <= -kStick) held.push_back(p.prefix + "LStickUp");
			if (ly >= kStick) held.push_back(p.prefix + "LStickDown");
			if (lx <= -kStick) held.push_back(p.prefix + "LStickLeft");
			if (lx >= kStick) held.push_back(p.prefix + "LStickRight");
			if (ry <= -kStick) held.push_back(p.prefix + "RStickUp");
			if (ry >= kStick) held.push_back(p.prefix + "RStickDown");
			if (rx <= -kStick) held.push_back(p.prefix + "RStickLeft");
			if (rx >= kStick) held.push_back(p.prefix + "RStickRight");
			if (a.GameControllerGetAxis(p.handle, AXIS_TRIGGERLEFT) > kTrigger) held.push_back(p.prefix + "LeftTrigger");
			if (a.GameControllerGetAxis(p.handle, AXIS_TRIGGERRIGHT) > kTrigger) held.push_back(p.prefix + "RightTrigger");
			if (axes != nullptr)
			{
				/* as Chimera's SDL2 gamepad names them, the y axes as SDL gives them */
				axes->emplace_back(p.prefix + "LeftThumbX Axis", conv(lx));
				axes->emplace_back(p.prefix + "LeftThumbY Axis", conv(ly));
				axes->emplace_back(p.prefix + "RightThumbX Axis", conv(rx));
				axes->emplace_back(p.prefix + "RightThumbY Axis", conv(ry));
				axes->emplace_back(p.prefix + "LeftTrigger Axis", conv(a.GameControllerGetAxis(p.handle, AXIS_TRIGGERLEFT)));
				axes->emplace_back(p.prefix + "RightTrigger Axis", conv(a.GameControllerGetAxis(p.handle, AXIS_TRIGGERRIGHT)));
			}
		}
		else
		{
			/* the first nine axes by DirectInput's letters, the rest by number */
			static const char *const letters[] = {"X", "Y", "Z", "W", "V", "S", "Q", "P", "N"};
			for (int x = 0; x < p.axes; x++)
			{
				const int v = a.JoystickGetAxis(p.handle, x);
				const std::string axis = p.prefix + (x < 9 ? std::string(letters[x]) : "Axis" + std::to_string(x));
				if (axes != nullptr) axes->emplace_back(axis + " Axis", conv(v));
				if (v >= kStick) held.push_back(axis + "+");
				if (v <= -kStick) held.push_back(axis + "-");
			}
			for (int b = 0; b < p.buttons; b++)
				if (a.JoystickGetButton(p.handle, b) != 0) held.push_back(p.prefix + "B" + std::to_string(b + 1));
			for (int h = 0; h < p.hats; h++)
			{
				const uint8_t v = a.JoystickGetHat(p.handle, h);
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
