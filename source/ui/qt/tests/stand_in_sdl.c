/* A stand-in for SDL2 in the window's gamepad hotkey test (issue 466): one
 * game controller, "Stand-in Pad", whose right trigger is held from
 * STAND_IN_PAD_FROM to STAND_IN_PAD_TO milliseconds after SDL_Init, so a
 * test can press a pad. It has only what the window's gamepads.cpp looks
 * up; the test puts it in a folder of its own as libSDL2.so. */
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

static double started;
static int pad;

static double now_ms(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

static int trigger_held(void)
{
	const char *from = getenv("STAND_IN_PAD_FROM"), *to = getenv("STAND_IN_PAD_TO");
	const double since = now_ms() - started;
	return from != NULL && to != NULL && since >= atof(from) && since < atof(to);
}

int SDL_Init(uint32_t flags)
{
	(void)flags;
	started = now_ms();
	return 0;
}
void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }
int SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return 1;
}
const char *SDL_GetError(void) { return ""; }
int SDL_NumJoysticks(void) { return 1; }
int32_t SDL_JoystickGetDeviceInstanceID(int index) { return index; }
int SDL_IsGameController(int index)
{
	(void)index;
	return 1;
}
void *SDL_GameControllerOpen(int index)
{
	(void)index;
	return &pad;
}
void SDL_GameControllerClose(void *p) { (void)p; }
const char *SDL_GameControllerName(void *p)
{
	(void)p;
	return "Stand-in Pad";
}
uint8_t SDL_GameControllerGetButton(void *p, int button)
{
	(void)p;
	(void)button;
	return 0;
}
/* SDL_CONTROLLER_AXIS_TRIGGERRIGHT is 5 */
int16_t SDL_GameControllerGetAxis(void *p, int axis)
{
	(void)p;
	return axis == 5 && trigger_held() ? 32767 : 0;
}
void *SDL_JoystickOpen(int index)
{
	(void)index;
	return &pad;
}
void SDL_JoystickClose(void *p) { (void)p; }
const char *SDL_JoystickName(void *p)
{
	(void)p;
	return "Stand-in Pad";
}
int SDL_JoystickNumButtons(void *p)
{
	(void)p;
	return 0;
}
int SDL_JoystickNumHats(void *p)
{
	(void)p;
	return 0;
}
int SDL_JoystickNumAxes(void *p)
{
	(void)p;
	return 0;
}
int16_t SDL_JoystickGetAxis(void *p, int axis)
{
	(void)p;
	(void)axis;
	return 0;
}
uint8_t SDL_JoystickGetButton(void *p, int button)
{
	(void)p;
	(void)button;
	return 0;
}
uint8_t SDL_JoystickGetHat(void *p, int hat)
{
	(void)p;
	(void)hat;
	return 0;
}
void SDL_JoystickUpdate(void) {}
