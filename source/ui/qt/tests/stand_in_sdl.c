/* A stand-in for SDL3 in the window's gamepad hotkey test (issue 466): one
 * gamepad, "Stand-in Pad", whose right trigger is held from
 * STAND_IN_PAD_FROM to STAND_IN_PAD_TO milliseconds after SDL_Init, so a
 * test can press a pad. It has only what the window's gamepads.cpp looks
 * up; the test puts it in a folder of its own as libSDL3.so. */
#include <stdbool.h>
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

static bool trigger_held(void)
{
	const char *from = getenv("STAND_IN_PAD_FROM"), *to = getenv("STAND_IN_PAD_TO");
	const double since = now_ms() - started;
	return from != NULL && to != NULL && since >= atof(from) && since < atof(to);
}

bool SDL_Init(uint32_t flags)
{
	(void)flags;
	started = now_ms();
	return true;
}
void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }
bool SDL_SetHint(const char *name, const char *value)
{
	(void)name;
	(void)value;
	return true;
}
const char *SDL_GetError(void) { return ""; }
void SDL_free(void *p) { free(p); }
/* one pad, SDL's number 1 */
uint32_t *SDL_GetJoysticks(int *count)
{
	uint32_t *ids = malloc(2 * sizeof *ids);
	ids[0] = 1;
	ids[1] = 0;
	if (count != NULL) *count = 1;
	return ids;
}
bool SDL_IsGamepad(uint32_t id)
{
	(void)id;
	return true;
}
void *SDL_OpenGamepad(uint32_t id)
{
	(void)id;
	return &pad;
}
void SDL_CloseGamepad(void *p) { (void)p; }
const char *SDL_GetGamepadName(void *p)
{
	(void)p;
	return "Stand-in Pad";
}
bool SDL_GetGamepadButton(void *p, int button)
{
	(void)p;
	(void)button;
	return false;
}
/* SDL_GAMEPAD_BUTTON_LABEL_UNKNOWN: the face buttons are named by place */
int SDL_GetGamepadButtonLabel(void *p, int button)
{
	(void)p;
	(void)button;
	return 0;
}
uint16_t SDL_GetGamepadVendor(void *p)
{
	(void)p;
	return 0;
}
uint16_t SDL_GetGamepadProduct(void *p)
{
	(void)p;
	return 0;
}
/* SDL_GAMEPAD_AXIS_RIGHT_TRIGGER is 5 */
int16_t SDL_GetGamepadAxis(void *p, int axis)
{
	(void)p;
	return axis == 5 && trigger_held() ? 32767 : 0;
}
void *SDL_OpenJoystick(uint32_t id)
{
	(void)id;
	return &pad;
}
void SDL_CloseJoystick(void *p) { (void)p; }
const char *SDL_GetJoystickName(void *p)
{
	(void)p;
	return "Stand-in Pad";
}
int SDL_GetNumJoystickButtons(void *p)
{
	(void)p;
	return 0;
}
int SDL_GetNumJoystickHats(void *p)
{
	(void)p;
	return 0;
}
int SDL_GetNumJoystickAxes(void *p)
{
	(void)p;
	return 0;
}
int16_t SDL_GetJoystickAxis(void *p, int axis)
{
	(void)p;
	(void)axis;
	return 0;
}
bool SDL_GetJoystickButton(void *p, int button)
{
	(void)p;
	(void)button;
	return false;
}
uint8_t SDL_GetJoystickHat(void *p, int hat)
{
	(void)p;
	(void)hat;
	return 0;
}
void SDL_UpdateJoysticks(void) {}
