/* The engine's side of the GPU bridge: a real GL context, and the dispatcher a
 * sandboxed core reaches it through.
 *
 * A core that wants a GPU cannot have one. It runs in a sandbox with no
 * syscalls, no libraries and no driver, and the only thing it may call out to
 * is one callback the host registers with miniBox - six integers in, one out.
 * So its renderer's GL calls arrive here as (opcode, pointer to an argument
 * block in GUEST memory), and this answers them on a context the engine owns.
 * The guest's memory is this process's memory, so the arguments - and the
 * vertex data and textures they point at - are read in place, uncopied. That
 * is what makes it affordable.
 *
 * The opcodes are miniBox's (extern/chimera-common-minibox/source/gl),
 * shared by every core and this host, and the list is append-only so a core
 * built against an older copy still means what it meant. A guest asks how long
 * our list is and declines us if we are behind it.
 *
 * TRAFFIC IS ONE-WAY. The guest may hand us pointers into its memory; we may
 * never hand back a pointer into ours, because the sandbox stops the guest
 * reading it - and is right to. Anything GL returns by pointer is copied into a
 * buffer the guest supplied.
 *
 * THE CONTEXT IS BORROWED, NEVER KEPT. "Current context" is one slot per
 * thread, and the frontend draws its own picture through it. Holding ours
 * across a return would take that slot away - and worse, take it away
 * INVISIBLY: a frontend that binds its context through SDL is short-circuited
 * by SDL's own cache ("already current") and never rebinds, so it goes on
 * drawing into a hidden 64x64 window forever. That is a black screen with
 * working sound, and it is what happened on Windows the first time this ran
 * against a real display. So: borrow at the first call of a frame, put back
 * exactly what was there when the frame ends (ce_gl_release).
 *
 * WHAT THIS COSTS. The GPU is outside the sandbox: outside the savestate,
 * outside the determinism the rest of a core is built on, and different on
 * every machine. A session drawn this way says so (ce_session_deterministic
 * turns 0) and a movie recorded on it carries a header saying a GPU drew, so
 * that a replay that desyncs somewhere else can be understood rather than
 * merely suffered.
 */
#include "chimera/engine.h"
#include "language.hpp"

#include <cstdio>   /* snprintf: both flavours report why there is no context */
#include <cstdlib>
#include <chrono>
#include <vector>
#include <ctime>    /* one ingredient of a context's identity */

#ifdef CE_GL_BRIDGE

#include <glad/gl.h>

#include "gl-bridge.h"
#include "gl-bridge-ops.h"
#include "gl_sync_names.h"
#include "gl_buffer_pool.h"
#include "gl_names.h"

#include <cstdio>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include <glad/wgl.h>
#else
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <unistd.h>   /* getpid: per-process entropy for a context's identity */
#ifndef EGL_PLATFORM_SURFACELESS_MESA
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
#endif
#endif

namespace
{

/* The versions asked for, newest first. A driver may answer a request with
 * exactly the version asked for and no more - NVIDIA's does, on Windows: asked
 * for 3.3 core it made a 3.3 context, where its compatibility context had been
 * 4.6, and a renderer that looks for 4.x would have taken its older path. */
const int kAskVersions[][2] = { {4, 6}, {4, 5}, {4, 4}, {4, 3}, {4, 2}, {4, 1}, {4, 0}, {3, 3} };

int g_version;
bool g_ready;
/* Which context is current - see GL_OP_CONTEXT_ID. Zero until one is made. */
uint64_t g_context_id;
char g_description[256];
char g_renderer[256], g_versionText[256];

/* ---------------------------------------------------------------------------
 * Windows: a context from a window nobody sees. WGL has no headless path and a
 * pixel format has to come from somewhere, so it comes from a window that is
 * created, never shown, and never pumped.
 */
#ifdef _WIN32

HWND s_window;
HDC s_dc;
HGLRC s_context;
HMODULE s_opengl32;
/* how the context was made, for another like it (replace_context): the
 * version asked for in kAskVersions, or -1 for the old kind */
PFNWGLCREATECONTEXTATTRIBSARBPROC s_createAttribs;
int s_askedVersion = -1;

GLADapiproc loader(const char *name)
{
	/* wglGetProcAddress answers for the modern entry points and returns null -
	 * or one of several unhelpful small integers - for the 1.1 ones, which live
	 * in opengl32.dll itself. Every GL loader on Windows carries this same
	 * small piece of ugliness; here it is, once. */
	PROC p = wglGetProcAddress(name);
	if (p == nullptr || p == (PROC)1 || p == (PROC)2 || p == (PROC)3 || p == (PROC)-1)
		p = GetProcAddress(s_opengl32, name);
	return (GLADapiproc)p;
}

bool create_context(char *err, int errlen)
{
	WNDCLASSA wc;
	memset(&wc, 0, sizeof wc);
	wc.lpfnWndProc = DefWindowProcA;
	wc.hInstance = GetModuleHandleA(nullptr);
	wc.lpszClassName = "ChimeraGpuBridge";
	RegisterClassA(&wc);

	s_window = CreateWindowExA(0, "ChimeraGpuBridge", "", WS_OVERLAPPEDWINDOW,
		0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
	if (s_window == nullptr)
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_window").c_str());
		return false;
	}

	s_dc = GetDC(s_window);
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof pfd);
	pfd.nSize = sizeof pfd;
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.cStencilBits = 8;

	int format = ChoosePixelFormat(s_dc, &pfd);
	if (format == 0 || !SetPixelFormat(s_dc, format, &pfd))
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_pixel_format").c_str());
		return false;
	}

	/* A context of the old kind first: WGL hands out the call that makes any
	 * other kind only while a context is current. */
	s_context = wglCreateContext(s_dc);
	if (s_context == nullptr || !wglMakeCurrent(s_dc, s_context))
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.not_current_win").c_str());
		return false;
	}

	s_opengl32 = LoadLibraryA("opengl32.dll");
	if (s_opengl32 == nullptr)
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_opengl32").c_str());
		return false;
	}

	/* Then the newest OpenGL the driver has, 3.3 at the least, with the core
	 * profile, which is what Linux's context is: a core then meets the same
	 * OpenGL on both systems, and an old call a renderer still makes is
	 * refused on Windows as it is on Linux instead of working on one only
	 * (owner, 2026-10-19). A driver without WGL_ARB_create_context keeps the
	 * old kind. */
	s_createAttribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
		reinterpret_cast<void *>(wglGetProcAddress("wglCreateContextAttribsARB")));
	s_askedVersion = -1;
	for (size_t v = 0; s_createAttribs != nullptr && v < sizeof kAskVersions / sizeof kAskVersions[0]; v++)
	{
		const int attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, kAskVersions[v][0],
			WGL_CONTEXT_MINOR_VERSION_ARB, kAskVersions[v][1],
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			0
		};
		HGLRC core = s_createAttribs(s_dc, nullptr, attribs);
		if (core == nullptr) continue;
		if (wglMakeCurrent(s_dc, core))
		{
			wglDeleteContext(s_context);
			s_context = core;
			s_askedVersion = (int)v;
			break;
		}
		wglDeleteContext(core);
		wglMakeCurrent(s_dc, s_context);
	}
	return true;
}

/* Another context made as the one above was, current in its place; the old
 * one, and every object in it, goes. False leaves the old one as it was. */
bool replace_context()
{
	HGLRC fresh = nullptr;
	if (s_askedVersion >= 0 && s_createAttribs != nullptr)
	{
		const int attribs[] = {
			WGL_CONTEXT_MAJOR_VERSION_ARB, kAskVersions[s_askedVersion][0],
			WGL_CONTEXT_MINOR_VERSION_ARB, kAskVersions[s_askedVersion][1],
			WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
			0
		};
		fresh = s_createAttribs(s_dc, nullptr, attribs);
	}
	else fresh = wglCreateContext(s_dc);
	if (fresh == nullptr) return false;
	if (!wglMakeCurrent(s_dc, fresh))
	{
		wglDeleteContext(fresh);
		return false;
	}
	wglDeleteContext(s_context);
	s_context = fresh;
	return true;
}

void destroy_context()
{
	if (s_context) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(s_context); s_context = nullptr; }
	if (s_dc) { ReleaseDC(s_window, s_dc); s_dc = nullptr; }
	if (s_window) { DestroyWindow(s_window); s_window = nullptr; }
}

/* The same at a session's end, without unbinding what the calling thread has
 * current - the window's own context, it may be. Ours is not current by then
 * (ce_gl_release gave it back). */
void end_context()
{
	if (s_context) { wglDeleteContext(s_context); s_context = nullptr; }
	if (s_dc) { ReleaseDC(s_window, s_dc); s_dc = nullptr; }
	if (s_window) { DestroyWindow(s_window); s_window = nullptr; }
}

/* Whose context was current before we took the slot. Null is a legitimate
 * answer and must be restored as faithfully as any other. */
HGLRC s_lentContext;
HDC s_lentDC;

void save_current()
{
	s_lentContext = wglGetCurrentContext();
	s_lentDC = wglGetCurrentDC();
}

bool bind_ours() { return wglMakeCurrent(s_dc, s_context) != FALSE; }

void return_current()
{
	wglMakeCurrent(s_lentDC, s_lentContext);
	s_lentContext = nullptr;
	s_lentDC = nullptr;
}

#else

/* ---------------------------------------------------------------------------
 * Linux: EGL's surfaceless platform, which Mesa provides with or without a GPU.
 * A context with NO surface has no framebuffer 0, which is fine here - a core's
 * renderer draws into framebuffers of its own and the frame is read back from
 * those, never from the default one.
 */
EGLDisplay s_display = EGL_NO_DISPLAY;
/* how the context was made, for another like it (replace_context) */
EGLConfig s_config;
int s_askedVersion = -1;
EGLContext s_context = EGL_NO_CONTEXT;

GLADapiproc loader(const char *name)
{
	return (GLADapiproc)eglGetProcAddress(name);
}

bool create_context(char *err, int errlen)
{
	auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)
		eglGetProcAddress("eglGetPlatformDisplayEXT");
	if (getPlatformDisplay != nullptr)
		s_display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
	if (s_display == EGL_NO_DISPLAY)
		s_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (s_display == EGL_NO_DISPLAY)
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_egl_display").c_str());
		return false;
	}

	if (!eglInitialize(s_display, nullptr, nullptr))
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.egl_not_started").c_str());
		return false;
	}

	if (!eglBindAPI(EGL_OPENGL_API))
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_desktop_gl").c_str());
		return false;
	}

	static const EGLint configAttribs[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_NONE
	};
	EGLConfig config;
	EGLint configCount = 0;
	if (!eglChooseConfig(s_display, configAttribs, &config, 1, &configCount) || configCount == 0)
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_egl_config").c_str());
		return false;
	}

	/* the newest the driver has, 3.3 at the least (kAskVersions) */
	s_config = config;
	for (size_t v = 0; s_context == EGL_NO_CONTEXT && v < sizeof kAskVersions / sizeof kAskVersions[0]; v++)
	{
		const EGLint contextAttribs[] = {
			EGL_CONTEXT_MAJOR_VERSION, kAskVersions[v][0],
			EGL_CONTEXT_MINOR_VERSION, kAskVersions[v][1],
			EGL_NONE
		};
		s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttribs);
		s_askedVersion = (int)v;
	}
	if (s_context == EGL_NO_CONTEXT)
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.no_context").c_str());
		return false;
	}

	if (!eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_context))
	{
		snprintf(err, errlen, "%s", chimera::lang::tr("gl.not_current").c_str());
		return false;
	}

	return true;
}

void destroy_context()
{
	if (s_display != EGL_NO_DISPLAY)
	{
		eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		if (s_context != EGL_NO_CONTEXT) eglDestroyContext(s_display, s_context);
		eglTerminate(s_display);
		s_display = EGL_NO_DISPLAY;
		s_context = EGL_NO_CONTEXT;
	}
}

/* The same at a session's end: only the context goes. The display stays
 * initialised for the next one, and nothing current is unbound - ours is not
 * current by then (ce_gl_release gave it back). */
void end_context()
{
	if (s_display != EGL_NO_DISPLAY && s_context != EGL_NO_CONTEXT) eglDestroyContext(s_display, s_context);
	s_context = EGL_NO_CONTEXT;
}

/* Another context made as the one above was, current in its place; the old
 * one, and every object in it, goes. False leaves the old one as it was. */
bool replace_context()
{
	if (s_display == EGL_NO_DISPLAY || s_askedVersion < 0) return false;
	const EGLint attribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, kAskVersions[s_askedVersion][0],
		EGL_CONTEXT_MINOR_VERSION, kAskVersions[s_askedVersion][1],
		EGL_NONE
	};
	EGLContext fresh = eglCreateContext(s_display, s_config, EGL_NO_CONTEXT, attribs);
	if (fresh == EGL_NO_CONTEXT) return false;
	if (!eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, fresh))
	{
		eglDestroyContext(s_display, fresh);
		return false;
	}
	eglDestroyContext(s_display, s_context);
	s_context = fresh;
	return true;
}

/* Whose context was current before we took the slot. A frontend drawing
 * through GLX rather than EGL is invisible to eglGetCurrentContext, and there
 * is no way to put a GLX context back from here - so what it gets back is an
 * unbound slot, which is what it would have had if it had released its own. */
EGLContext s_lentContext = EGL_NO_CONTEXT;
EGLSurface s_lentDraw = EGL_NO_SURFACE;
EGLSurface s_lentRead = EGL_NO_SURFACE;
EGLDisplay s_lentDisplay = EGL_NO_DISPLAY;

void save_current()
{
	s_lentContext = eglGetCurrentContext();
	s_lentDraw = eglGetCurrentSurface(EGL_DRAW);
	s_lentRead = eglGetCurrentSurface(EGL_READ);
	s_lentDisplay = eglGetCurrentDisplay();
}

bool bind_ours()
{
	return eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_context) == EGL_TRUE;
}

void return_current()
{
	if (s_lentDisplay != EGL_NO_DISPLAY)
		eglMakeCurrent(s_lentDisplay, s_lentDraw, s_lentRead, s_lentContext);
	else
		eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	s_lentContext = EGL_NO_CONTEXT;
	s_lentDisplay = EGL_NO_DISPLAY;
}

#endif

} // namespace

/* ---------------------------------------------------------------------------
 * The dispatcher.
 *
 * Entered FROM GUEST CODE, so it must be sysv64 on every host - miniBox's
 * MB_GUEST_ABI says the same thing from the other side. On Windows that is not
 * cosmetic: a win64 callee spills its shadow space over the caller's stack.
 */
#if defined(_WIN32) && defined(__GNUC__)
#define BRIDGE_ABI __attribute__((sysv_abi))
#else
#define BRIDGE_ABI
#endif

/* Set between borrow_current and return_current. Reading the driver's own idea
 * of the current context is a thread-local load and cheap, but this is on the
 * path of every single GL call a renderer makes, so it is a bool. */
static bool g_borrowed;

/* The two diagnostics below are read ONCE.
 *
 * They used to be a getenv apiece on every crossing, on the reasoning that a
 * getenv is nothing beside a GL call. It is not nothing: getenv walks the
 * environment comparing strings, and a renderer crossing this bridge does so
 * thousands of times a frame - Ruffle's wgpu backend makes tens of thousands -
 * so the walk was being paid tens of thousands of times a frame to answer a
 * question whose answer cannot change while the process runs. */
static bool glTrace()
{
	static const bool on = getenv("CHIMERA_GL_TRACE") != nullptr;
	return on;
}

static bool glCheck()
{
	static const bool on = getenv("CHIMERA_GL_CHECK") != nullptr;
	return on;
}

/* CHIMERA_GL_TIME: how long a frame spends INSIDE the driver, as against
 * inside the machine that is calling it.
 *
 * A chatty renderer and a slow one look the same from outside: the frame takes
 * 30ms either way. The question that separates them is what share of that 30ms
 * is spent below this function, and the only way to answer it is to time each
 * crossing and add them up. It is off by default because the clock read either
 * side is real - about 20ns a call, so roughly 0.15ms on a six-thousand-call
 * frame, which is worth knowing when reading the number it produces. */
static bool glTime()
{
	static const bool on = getenv("CHIMERA_GL_TIME") != nullptr;
	return on;
}

static uint64_t g_driverNs, g_driverNsAtFrame;

/* CHIMERA_GL_PROFILE: WHICH calls, not just how many.
 *
 * "Fifty thousand GL calls a frame" is not actionable; "forty thousand of them
 * are glUniform4fv" is. Counted per opcode, with the time each opcode's calls
 * took, and dumped every 300 frames. The names are not carried here - the
 * opcodes are the master list's order (miniBox source/gl/gl-entry-points.txt,
 * first entry is opcode 100) and resolving them is a job for whoever reads the
 * dump, not for a hot path. */
static bool glProfile()
{
	static const bool on = getenv("CHIMERA_GL_PROFILE") != nullptr;
	return on;
}

enum { kProfileOps = CHIMERA_GL_OP_LIST_LENGTH + 100 };
static uint64_t *g_opCalls, *g_opNs, *g_opMax, *g_opSlow;

/* A call that averages a hundred microseconds is one of two very different
 * things: every call costing a hundred, or one call in fifty costing five
 * milliseconds while the rest cost nothing. The first is overhead and the
 * second is a BLOCK - the CPU waiting for the GPU - and they want opposite
 * fixes. So the longest call and the number over 50us are kept beside the
 * total, because the average cannot tell them apart. */
static const uint64_t kSlowNs = 50000;

/* CHIMERA_GL_WHY: what the guest was DOING when it blocked.
 *
 * A profile says which call waited; it cannot say what the wait was for. The
 * calls before it can: a fence waited on straight after a readback is a
 * different problem from one waited on straight after a draw. So the last few
 * opcodes are kept in a ring, and a call that blocks prints them.
 *
 * Bounded on purpose - the first few blocks of a run say everything, and a
 * hundred thousand of them would say nothing at all. */
static unsigned g_whyRing[16];
static unsigned g_whyAt;
static int g_whyLeft = -1;

/* CHIMERA_GL_GPUTIME: is the GPU actually the one that is busy?
 *
 * The profile says the guest blocks eighteen times a frame waiting on a fence.
 * That has two readings and they want opposite fixes: the GPU genuinely has
 * that much work, or it does not and the waits are the pipeline being drained
 * for no good reason. A timestamp at the first call of a frame and another at
 * the last, read a frame later when they are certainly available, says which -
 * if the GPU's own span is most of the frame it is saturated, and if it is a
 * fraction the CPU is waiting on something it need not.
 */
static bool glGpuTime()
{
	static const bool on = getenv("CHIMERA_GL_GPUTIME") != nullptr;
	return on;
}

static GLuint g_tsQuery[2];
static bool g_tsPending;
static uint64_t g_gpuNsTotal, g_gpuFrames;

static void gpuTimeFrameBegin(void)
{
	if (!glGpuTime()) return;
	if (g_tsQuery[0] == 0) glGenQueries(2, g_tsQuery);
	if (g_tsPending)
	{
		GLuint64 a = 0, b = 0;
		glGetQueryObjectui64v(g_tsQuery[0], GL_QUERY_RESULT, &a);
		glGetQueryObjectui64v(g_tsQuery[1], GL_QUERY_RESULT, &b);
		if (b > a) { g_gpuNsTotal += (uint64_t)(b - a); g_gpuFrames++; }
		g_tsPending = false;
	}
	glQueryCounter(g_tsQuery[0], GL_TIMESTAMP);
}

static void gpuTimeFrameEnd(void)
{
	if (!glGpuTime() || g_tsQuery[1] == 0) return;
	glQueryCounter(g_tsQuery[1], GL_TIMESTAMP);
	g_tsPending = true;
	if (g_gpuFrames > 0 && g_gpuFrames % 100 == 0)
	{
		fprintf(stderr, "[ce-gl-gpu] %llu frames, GPU span %.3f ms a frame\n",
			(unsigned long long)g_gpuFrames,
			(double)g_gpuNsTotal / 1e6 / (double)g_gpuFrames);
		fflush(stderr);
	}
}

/* CHIMERA_GL_LIFETIME: is the guest binding objects that no longer exist?
 *
 * The bridge's whole contract is that GL object NAMES live in guest memory
 * while the objects themselves live in the driver. A savestate carries the
 * names and not the objects, so after a load the guest can be holding the name
 * of something that was deleted since - and the driver answers a bind on a dead
 * name by doing nothing, silently, which is a picture that is quietly wrong
 * rather than a crash.
 *
 * The driver already knows. glIsTexture and its family answer whether a name is
 * a live object, so every bind is checked against the truth. One extra GL call
 * per bind is far too dear to leave on, which is why this is a diagnostic; what
 * it produces is a per-frame count of binds that named nothing.
 *
 * A freshly generated name that has never been bound is not yet an object and
 * answers false here too, so the FIRST bind of each object counts. That is a
 * constant handful a frame; what matters is whether the number climbs. */
static bool glLifetime()
{
	static const bool on = getenv("CHIMERA_GL_LIFETIME") != nullptr;
	return on;
}

static uint64_t g_deadBinds, g_deadBindsAtFrame, g_liveBinds;

static void lifetimeCheck(uintptr_t op, uintptr_t a)
{
	GLuint name = 0;
	GLboolean live = GL_TRUE;
	switch (op)
	{
		case CHIMERA_GL_OP_glBindTexture:
			name = ((struct ChimeraGlArgs_glBindTexture *)a)->texture;
			if (name != 0) live = glIsTexture(name);
			break;
		case CHIMERA_GL_OP_glBindBuffer:
			name = ((struct ChimeraGlArgs_glBindBuffer *)a)->buffer;
			if (name != 0) live = glIsBuffer(name);
			break;
		case CHIMERA_GL_OP_glBindFramebuffer:
			name = ((struct ChimeraGlArgs_glBindFramebuffer *)a)->framebuffer;
			if (name != 0) live = glIsFramebuffer(name);
			break;
		case CHIMERA_GL_OP_glBindVertexArray:
			name = ((struct ChimeraGlArgs_glBindVertexArray *)a)->array;
			if (name != 0) live = glIsVertexArray(name);
			break;
		case CHIMERA_GL_OP_glUseProgram:
			name = ((struct ChimeraGlArgs_glUseProgram *)a)->program;
			if (name != 0) live = glIsProgram(name);
			break;
		default:
			return;
	}
	if (name == 0) return;
	if (live) g_liveBinds++; else g_deadBinds++;
}

/* ---------------------------------------------------------------------------
 * What a savestate does to the objects the guest is holding.
 *
 * The bridge's contract is that GL object NAMES live in guest memory and the
 * objects live in the driver. A savestate carries the names. It cannot carry
 * the objects, so between saving a state and loading it the driver's world
 * moves on underneath the names, and there are two ways that hurts:
 *
 *  - a name the guest still holds was DELETED since. Every call naming it is
 *    refused, silently, and that part of the picture is whatever was there.
 *  - a name the guest still holds was deleted and HANDED OUT AGAIN since, to a
 *    different object. Every call naming it succeeds and draws the wrong
 *    thing. This is the one nothing can see: no GL error, no crash, just a
 *    picture that is quietly wrong and gets worse with every reload.
 *
 * Both are counted here, exactly, by giving every name a generation that goes
 * up each time the driver hands it out. A snapshot at save time and a
 * comparison at load time is the whole measurement.
 *
 * CHIMERA_GL_STATEAUDIT=1 turns it on. It is a diagnostic, not a fix: knowing
 * the number is what decides whether a fix is worth its cost. */
static bool glAudit()
{
	static const bool on = getenv("CHIMERA_GL_STATEAUDIT") != nullptr;
	return on;
}

enum { kAuditTexture = 0, kAuditBuffer = 1, kAuditFramebuffer = 2, kAuditKinds = 3 };

/* One object's life, in frames. A name is handed out, used, deleted, and later
 * handed out again to something else entirely; each of those is an interval,
 * and the whole question this answers is which interval a savestate believes
 * in. */
struct AuditLife { int64_t born; int64_t died; };  /* died < 0 while it lives */

/* Lazily constructed on first use, and deliberately NOT a container at
 * namespace scope. One of those makes this file a static-initialiser function,
 * and mingw-w64 13.2 - what the Windows bundle is cross-built with - cannot
 * emit this TU's: it ICEs in choose_baseaddr (ix86_expand_prologue) at -O2 and
 * above, because the function realigns its frame to register the containers'
 * destructors with atexit. A function-local static is initialised on first use,
 * so there is no such function to emit and the bridge still gets -O3. */
static std::vector<std::vector<AuditLife>> *auditLives()
{
	static std::vector<std::vector<AuditLife>> lives[kAuditKinds];
	return lives;
}

static int64_t g_auditFrame;
static uint64_t g_auditLoads, g_auditDead, g_auditReused, g_auditLeaked;

static std::vector<AuditLife> &auditSlot(int kind, GLuint name)
{
	std::vector<std::vector<AuditLife>> &all = auditLives()[kind];
	if (all.size() <= name) all.resize((size_t)name + 1024);
	return all[name];
}

static void auditGen(int kind, GLuint name)
{
	if (!glAudit() || name == 0) return;
	auditSlot(kind, name).push_back(AuditLife{ g_auditFrame, -1 });
}

static void auditDelete(int kind, GLuint name)
{
	if (!glAudit() || name == 0) return;
	std::vector<AuditLife> &lives = auditSlot(kind, name);
	if (!lives.empty() && lives.back().died < 0) lives.back().died = g_auditFrame;
}

/* ---------------------------------------------------------------------------
 * The flight recorder: the last calls that crossed, kept where a crash can
 * still read them (docs/gpu-bridge.md, "The flight recorder").
 *
 * A driver that finds its state corrupt fast-fails, and nothing in this process
 * runs after that - not a trace flush, not a handler. What the crash module in
 * WerFault.exe CAN do is read this process's memory. So every crossing writes
 * its opcode into a fixed ring here BEFORE the driver is called (a crash inside
 * the call leaves that call newest), with markers where a frame took and gave
 * back the context, where a state was loaded and where a session took the
 * bridge. Two stores and an increment a call, against a crossing that already
 * costs tens of nanoseconds; always on, because the crash that needs it is
 * never the run that had a trace switched on.
 *
 * Written unsynchronised: a restore on another thread can tear one entry, and
 * a torn entry in a crash note is a price worth not taking a lock per call. The
 * layout is read by source/crash/chimera_crash.c.
 */
enum : uint32_t
{
	kFlightBorrowed = 0xFFFFFF01,    /* the first call of a frame took the context */
	kFlightReleased = 0xFFFFFF02,    /* the frame gave it back; detail: the calls it made */
	kFlightStateLoaded = 0xFFFFFF03, /* a savestate was restored; detail: the frame restored to */
	kFlightSession = 0xFFFFFF04,     /* a session took the bridge (a fresh context id) */
	kFlightStopped = 0xFFFFFF05,     /* the context was destroyed */
	kFlightRenewed = 0xFFFFFF06,     /* a load gave the core a fresh context; detail: the names kept */
	kFlightVulkan = 0x40000000,      /* added to a Vulkan bridge opcode: the crash module names it from Vulkan's list */
};
enum { kFlightCapacity = 8192 }; /* a power of two */
struct FlightEntry
{
	uint32_t op;
	uint32_t detail;
};
struct FlightRecorder
{
	uint32_t magic;    /* "CEGL" */
	uint32_t capacity;
	uint64_t written;  /* entries ever written; the newest is at (written - 1) % capacity */
	FlightEntry entries[kFlightCapacity];
};
static FlightRecorder g_flight = { 0x4C474543u, kFlightCapacity, 0, {} };

static inline void flightNote(uint32_t op, uint32_t detail)
{
	FlightEntry &entry = g_flight.entries[g_flight.written & (kFlightCapacity - 1)];
	entry.op = op;
	entry.detail = detail;
	g_flight.written++;
}

extern "C" void ce_gl_audit_frame(int64_t frame)
{
	if (glAudit()) g_auditFrame = frame;
}

/* What a restore to `to` does to the objects the guest is about to believe in.
 *
 * dead:   it holds the name of something deleted since - every call naming it
 *         is refused silently and that part of the picture is stale.
 * reused: it holds a name that was deleted AND handed out again to a different
 *         object. Every call naming it succeeds and draws the wrong thing.
 *         Nothing can see this one: no GL error, no crash.
 * leaked: made after the frame being restored, so the guest has just forgotten
 *         it and nothing will ever delete it.
 */
static void mint_context_id();
static void renewContext();

/* Whether a same-session load moves the context id. Global and set before a
 * session opens, the way ce_gl_request and ce_cache_dir are; 1 unless a core
 * declares otherwise, so nothing changes for a core that says nothing. */
static bool g_rebuildOnStateLoad = true;
static bool g_syncReap; /* see syncReap */
static bool g_shadowReap; /* see shadowReap */

extern "C" void ce_gl_rebuild_on_state_load(int32_t on)
{
	g_rebuildOnStateLoad = on != 0;
}

/* `to` is the frame the state was taken at, -1 when the caller cannot say (a
 * state handed in from outside, or read from a file) */
extern "C" void ce_gl_state_loaded(int64_t to)
{
	flightNote(kFlightStateLoaded, (uint32_t)to);
	/* A restore is a new context as far as the guest can tell (issue #43). The
	 * state puts back the renderer's idea of every object - a surface cache
	 * saying texture 56 is a 512x512 depth buffer - but not the objects: those
	 * stayed as the frames AFTER the restored one left them, reallocated,
	 * redrawn, reattached. Same session, same names, different things. xemu
	 * attached that texture, the driver refused it, and the renderer's own
	 * assert took the machine down; a renderer that asserts less draws garbage
	 * instead. Every bridged core already rebuilds from emulated memory when
	 * this id moves (the cross-session reload), which is exactly the answer,
	 * so a load moves it. Proved on FlatOut 2: a seeded TAStudio stress died
	 * at step 13 on every build, and ran 212 steps with this.
	 *
	 * CHIMERA_GL_KEEP_OBJECTS_ON_LOAD puts the old behaviour back, for A/B. */
	static const bool keepObjects = getenv("CHIMERA_GL_KEEP_OBJECTS_ON_LOAD") != nullptr;
	/* ...and a core may say the same of itself, because for some renderers the
	 * rebuild is the damage rather than the repair (see
	 * ce_gl_rebuild_on_state_load). A core that declares nothing keeps the
	 * behaviour above exactly. */
	if (!keepObjects && g_rebuildOnStateLoad)
	{
		renewContext();
		mint_context_id();
		g_syncReap = true;
	}
	/* mappings left open in a context that is kept (shadowReap) */
	g_shadowReap = true;
	if (!glAudit()) return;
	if (to < 0)
	{
		g_auditLoads++;
		fprintf(stderr, "[ce-gl-audit] restore %llu to a frame not known: not counted\n",
			(unsigned long long)g_auditLoads);
		fflush(stderr);
		return;
	}
	uint64_t dead = 0, reused = 0, held = 0, leaked = 0;
	for (int k = 0; k < kAuditKinds; k++)
	{
		for (size_t n = 1; n < auditLives()[k].size(); n++)
		{
			const std::vector<AuditLife> &lives = auditLives()[k][n];
			if (lives.empty()) continue;
			/* which interval was this name living in at frame `to`? */
			int at = -1;
			for (size_t i = 0; i < lives.size(); i++)
			{
				if (lives[i].born <= to && (lives[i].died < 0 || lives[i].died > to)) { at = (int)i; break; }
			}
			if (at < 0)
			{
				if (lives.back().died < 0 && lives.back().born > to) leaked++;
				continue;
			}
			held++;
			const bool current = (size_t)at + 1 == lives.size();
			if (!current) reused++;             /* died and was handed out again */
			else if (lives[at].died >= 0) dead++;  /* died and never came back */
		}
	}
	g_auditLoads++;
	g_auditDead += dead;
	g_auditReused += reused;
	g_auditLeaked += leaked;
	fprintf(stderr, "[ce-gl-audit] restore %llu to frame %lld: the state believes in"
		" %llu objects - %llu deleted since, %llu handed out again since,"
		" %llu made since and now orphaned\n",
		(unsigned long long)g_auditLoads, (long long)to,
		(unsigned long long)held, (unsigned long long)dead,
		(unsigned long long)reused, (unsigned long long)leaked);
	fflush(stderr);
}

static bool glWhy()
{
	static const bool on = getenv("CHIMERA_GL_WHY") != nullptr;
	if (on && g_whyLeft < 0)
	{
		const char *v = getenv("CHIMERA_GL_WHY");
		g_whyLeft = (v != nullptr && *v >= '1' && *v <= '9') ? atoi(v) : 40;
	}
	return on;
}

static void profileDump(void)
{
	if (g_opCalls == nullptr) return;
	fprintf(stderr, "[ce-gl-profile] op,calls,ms,slow,maxus\n");
	for (int op = 0; op < kProfileOps; op++)
	{
		if (g_opCalls[op] == 0) continue;
		fprintf(stderr, "[ce-gl-profile] %d,%llu,%.3f,%llu,%.1f\n", op,
			(unsigned long long)g_opCalls[op], (double)g_opNs[op] / 1e6,
			(unsigned long long)g_opSlow[op], (double)g_opMax[op] / 1e3);
	}
	fflush(stderr);
}

/* How many calls crossed, and how many frames they were spread over: the two
 * numbers that say whether a core's renderer is chatty. Printed by
 * ce_gl_release under CHIMERA_GL_TRACE, which is once a frame. */
static uint64_t g_calls, g_callsAtFrame, g_frames;

/* Defined below, beside the pool they belong to; needed here because the frame
 * boundary is where a deferred delete comes due. */
static bool glPool();
static bool glLifetime();
static bool glAudit();
static void auditGen(int kind, GLuint name);
static void gpuTimeFrameEnd(void);

extern "C" void ce_gl_release(void)
{
	if (!g_borrowed) return;

	flightNote(kFlightReleased, (uint32_t)(g_calls - g_callsAtFrame));
	gpuTimeFrameEnd();
	return_current();
	g_borrowed = false;
	if (glTrace())
	{
		g_frames++;
		if (glTime())
			fprintf(stderr,
				"[ce-gl] frame %llu: %llu calls, %.3f ms in the driver"
				" (%llu so far, %.0f a frame, %.3f ms a frame)\n",
				(unsigned long long)g_frames, (unsigned long long)(g_calls - g_callsAtFrame),
				(double)(g_driverNs - g_driverNsAtFrame) / 1e6,
				(unsigned long long)g_calls, (double)g_calls / (double)g_frames,
				(double)g_driverNs / 1e6 / (double)g_frames);
		else
			fprintf(stderr, "[ce-gl] frame %llu: %llu calls (%llu so far, %.0f a frame)\n",
				(unsigned long long)g_frames, (unsigned long long)(g_calls - g_callsAtFrame),
				(unsigned long long)g_calls, (double)g_calls / (double)g_frames);
		fflush(stderr);
	}
	if (glLifetime())
	{
		fprintf(stderr, "[ce-gl-life] frame %llu: %llu binds named nothing"
			" (%llu this frame, %llu live)\n",
			(unsigned long long)g_frames, (unsigned long long)g_deadBinds,
			(unsigned long long)(g_deadBinds - g_deadBindsAtFrame),
			(unsigned long long)g_liveBinds);
		fflush(stderr);
		g_deadBindsAtFrame = g_deadBinds;
	}
	g_callsAtFrame = g_calls;
	g_driverNsAtFrame = g_driverNs;
	if (glProfile() && g_frames > 0 && g_frames % 300 == 0) profileDump();
}

static uintptr_t ce_gl_dispatch_one(uintptr_t op, uintptr_t a, uintptr_t b,
                                    uintptr_t c, uintptr_t d, uintptr_t e);

/* ---------------------------------------------------------------------------
 * Driver -> guest reads go through a bounce buffer, never the guest's heap.
 *
 * A readback (glGetBufferSubData, and glReadPixels into client memory) hands the
 * driver a destination in the caller's address space and a size, and the driver
 * writes there itself. The driver is outside the sandbox. If it writes even one
 * byte past that size - a row padded to the pack alignment, a device that
 * rounds up - it lands in the GUEST's own heap, and corrupts an allocator that
 * then dies, seemingly at random, many frames later. That is the New Star
 * Soccer crash (guest musl malloc, 0xc0000005; dumps read 2026-09-13): the
 * driver over-wrote a guest buffer, the guest's free list was poisoned, and the
 * next allocation followed a wild pointer. It shows on the real driver and not
 * on the software one the gate uses, which is exactly how a padding difference
 * would behave.
 *
 * So the driver never sees a guest pointer for these: it writes into a host
 * buffer sized generously for what it might write, with a canary past the bytes
 * the guest asked for, and exactly those bytes are copied on to the guest. A
 * driver that overruns hits the canary here - named, at the call, the frame it
 * happened - and the guest heap is untouched either way.
 */
static uint8_t *g_bounce;
static size_t g_bounceCap;
static const size_t kBounceCanary = 4096; /* room past the ask for an overrun to land in */
static const uint8_t kCanaryByte = 0xCE;
static uint64_t g_bounceOverruns;

/* A scratch buffer holding at least `need` bytes plus the canary, or null if it
 * could not be grown (the caller then falls back to a direct, unguarded call -
 * a readback is better done unguarded than not at all). The canary is laid down
 * fresh each time, right where the guest's bytes end. */
static const size_t kBounceMost = size_t{ 1 } << 30; /* past this a readback goes direct */

static uint8_t *bounceFor(size_t need)
{
	/* the size is the guest's (glGetBufferSubData's), and can be anything */
	if (need > kBounceMost) return nullptr;
	const size_t want = need + kBounceCanary;
	if (want > g_bounceCap)
	{
		size_t grown = g_bounceCap ? g_bounceCap : 1u << 16;
		while (grown < want) grown <<= 1;
		uint8_t *bigger = (uint8_t *)realloc(g_bounce, grown);
		if (bigger == nullptr) return nullptr;
		g_bounce = bigger;
		g_bounceCap = grown;
	}
	memset(g_bounce + need, kCanaryByte, kBounceCanary);
	return g_bounce;
}

/* Did the driver write past the `need` bytes it was asked for? Says so once per
 * breach, with how far it reached - the whole point of the guard is to name the
 * call that the crash never could. */
static void bounceCheckCanary(size_t need, const char *what)
{
	size_t over = 0;
	for (size_t i = kBounceCanary; i-- > 0;)
	{
		if (g_bounce[need + i] != kCanaryByte) { over = i + 1; break; }
	}
	if (over == 0) return;
	g_bounceOverruns++;
	fprintf(stderr, "[ce-gl-guard] %s asked for %zu bytes and the driver wrote at least"
		" %zu past them - the guest heap was spared (overrun #%llu)\n",
		what, need, over, (unsigned long long)g_bounceOverruns);
	fflush(stderr);
}

/* Pixels a readback into client memory will write, per the pack state the guest
 * set. Returns false when this cannot be sized safely - a packed or exotic
 * format, a skip offset, a bound pixel-pack buffer (then the write goes to the
 * buffer, not client memory) - and the caller passes the guest pointer straight
 * through, exactly as before. `tight` is the spec's write (the last row not
 * padded); `scratch` allows for a driver that pads the last row too. */
/* The rows a readback writes: `rows` of `rowBytes` each, `rowStride` apart. GL
 * writes those and nothing between them - with a row length larger than the
 * width, or rows padded to the pack alignment, the bytes between rows are the
 * client's own and stay as they were. */
struct PixelRows { size_t rowStride = 0, rowBytes = 0, rows = 0; };

static bool pixelBytes(GLenum format, GLenum type, GLsizei width, GLsizei height,
                       size_t *tight, size_t *scratch, PixelRows *shape = nullptr)
{
	if (width <= 0 || height <= 0) return false;

	int components;
	switch (format)
	{
		case GL_RED: case GL_GREEN: case GL_BLUE: case GL_ALPHA:
		case GL_RED_INTEGER: case GL_GREEN_INTEGER: case GL_BLUE_INTEGER:
		case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX:
			components = 1; break;
		case GL_RG: case GL_RG_INTEGER:
			components = 2; break;
		case GL_RGB: case GL_BGR: case GL_RGB_INTEGER: case GL_BGR_INTEGER:
			components = 3; break;
		case GL_RGBA: case GL_BGRA: case GL_RGBA_INTEGER: case GL_BGRA_INTEGER:
			components = 4; break;
		default:
			return false; /* DEPTH_STENCIL and anything unlisted: pass through */
	}

	int pixel;
	switch (type)
	{
		case GL_UNSIGNED_BYTE: case GL_BYTE:
			pixel = components * 1; break;
		case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT:
			pixel = components * 2; break;
		case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT:
			pixel = components * 4; break;
		default:
			return false; /* packed types fold components into one unit: pass through */
	}

	/* A skip offset would move where the driver starts writing; only the plain
	 * case (what a renderer doing a readback uses) is sized here. */
	GLint rowLength = 0, skipPixels = 0, skipRows = 0, alignment = 4;
	glGetIntegerv(GL_PACK_ROW_LENGTH, &rowLength);
	glGetIntegerv(GL_PACK_SKIP_PIXELS, &skipPixels);
	glGetIntegerv(GL_PACK_SKIP_ROWS, &skipRows);
	glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
	if (skipPixels != 0 || skipRows != 0) return false;
	if (alignment != 1 && alignment != 2 && alignment != 4 && alignment != 8) return false;

	if (width <= 0 || height <= 0) return false;

	const size_t effectiveWidth = rowLength > 0 ? (size_t)rowLength : (size_t)width;
	const size_t rowRaw = (size_t)pixel * effectiveWidth;
	const size_t rowStride = (rowRaw + (size_t)alignment - 1) & ~((size_t)alignment - 1);
	/* the guest's numbers: a product that would not fit is not guarded */
	if (rowStride > (SIZE_MAX >> 1) / (size_t)height) return false;
	*tight = rowStride * ((size_t)height - 1) + (size_t)pixel * (size_t)width;
	/* generous: a padded last row lands here. And never less than the span GL
	 * writes: with a row length SMALLER than the width the rows overlap, and
	 * rowStride * height fell short of it - the driver then wrote past the
	 * bounce buffer, into Chimera's own heap (2026-10-18). */
	*scratch = rowStride * (size_t)height;
	if (*scratch < *tight) *scratch = *tight;
	if (shape != nullptr) *shape = PixelRows{ rowStride, (size_t)pixel * (size_t)width, (size_t)height };
	return true;
}

/* What GL wrote into the bounce buffer, copied on to the guest row by row: the
 * bytes between rows are not GL's, and copying them handed the guest whatever
 * an earlier readback had left there - xemu reads surfaces straight into
 * console RAM, so the machine depended on what was read before it
 * (2026-10-18). Rows in order: where they overlap, the later wins, as GL's. */
static void copyRows(void *dst, const uint8_t *src, const PixelRows &shape)
{
	uint8_t *d = static_cast<uint8_t *>(dst);
	for (size_t r = 0; r < shape.rows; r++)
		std::memcpy(d + r * shape.rowStride, src + r * shape.rowStride, shape.rowBytes);
}

/* Whether a readback with these args writes into client memory and can be sized
 * (so it goes through the bounce buffer). False when a pixel-pack buffer is
 * bound - then `pixels` is an offset into that buffer, not a client pointer -
 * or the format cannot be sized. Fills the byte counts when true. */
static bool pixelReadGuarded(GLenum format, GLenum type, GLsizei width, GLsizei height,
                             const void *pixels, size_t *tight, size_t *scratch, PixelRows *shape)
{
	if (pixels == nullptr) return false;
	GLint packBuffer = 0;
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
	if (packBuffer != 0) return false;
	return pixelBytes(format, type, width, height, tight, scratch, shape);
}

/* Whether host code may read (write false) or write the guest's bytes the
 * core handed GL: asked of the sandbox (wbx_guest_range_ok) through the
 * session that started the bridge. A null, freed or garbage pointer used to
 * be followed by the driver or by this file, a fault in host code that ended
 * Chimera (2026-10-18). With no session, or an older host, the range is taken
 * as before. */
static int (*g_guestCan)(void *, uintptr_t, uintptr_t, int);
static void *g_guestCanCtx;

/* how many readbacks the driver wrote past what the guard sized (tests) */
extern "C" uint64_t ce_gl_readback_overruns(void) { return g_bounceOverruns; }

extern "C" void ce_gl_guest_check(int (*can)(void *, uintptr_t, uintptr_t, int), void *ctx)
{
	g_guestCan = can;
	g_guestCanCtx = ctx;
}

static bool guestCan(const void *p, size_t n, bool write)
{
	if (n == 0 || g_guestCan == nullptr) return true;
	return g_guestCan(g_guestCanCtx, reinterpret_cast<uintptr_t>(p), n, write ? 1 : 0) != 0;
}

/* A call the bridge would not make, said once per entry point: the driver
 * lacks the function (glad left its pointer null - calling through it ended
 * Chimera), or the core handed it memory the host may not touch. It answers
 * 0, as a call the driver refused (2026-10-18). */
static uintptr_t glRefused(uintptr_t op, const char *name, const char *why)
{
	static std::vector<bool> said;
	if (op < 4096)
	{
		if (said.size() <= op) said.resize(op + 1, false);
		if (said[op]) return 0;
		said[op] = true;
	}
	fprintf(stderr, "[ce-gl] %s was not run: %s (said once)\n", name, why);
	fflush(stderr);
	return 0;
}
#define CHIMERA_GL_UNSUPPLIED(op, name) glRefused((op), (name), "this GL driver does not provide it")
#define GL_PROVIDED_OR_REFUSE(fn) do { if (glad_##fn == nullptr) return glRefused(op, #fn, "this GL driver does not provide it"); } while (0)
/* the reason glRefused logs for a pointer outside the guest */
#define CE_GL_BAD_POINTER "the core handed it memory it does not have"

/* ---------------------------------------------------------------------------
 * Sync objects are NAMED to the guest, never handed to it.
 *
 * glFenceSync returns a GLsync, which is a pointer into the DRIVER's memory,
 * and the generated dispatcher returned it to the guest verbatim - precisely
 * what the note at the top of this file forbids. A pointer into our address
 * space then lives in the guest's heap, where the sandbox cannot let it be
 * read, and where nothing can make it mean the same thing twice.
 *
 * What follows is that a saved state becomes unusable outside the process that
 * wrote it: reloaded, it still holds the old pointers, and the first
 * glGetSynciv or glDeleteSync on one hands the driver an address belonging to a
 * process that has since exited.
 *
 * What does NOT follow - measured, not assumed - is determinism. Two identical
 * twenty-frame Ruffle runs on a real GTX 1060 still diverge with this in place,
 * by millions of bytes and a different dirty-page count, so the leaked pointer
 * was never the reason a drawing core fails to reproduce itself. What that
 * cause is, this does not say and did not measure. Differences of that kind
 * have previously been traced to host addresses left in DEAD guest stack
 * (below the stack pointer, varying with ASLR) and to GPU-derived bytes
 * entering guest memory through readbacks; naming syncs removes one source of
 * the former without settling the question.
 *
 * So the guest gets a NAME: a slot index, plus a generation bumped each time
 * the slot is reused. The name is small, it is ours, and a name from an older
 * session cannot be mistaken for a live one - the generation will not match.
 * Such a name reads as a fence that has already passed, which is the truth
 * about any GPU work a reloaded state could still be waiting for: it finished,
 * in a process that is gone.
 */
struct SyncSlot
{
	GLsync sync;   /* the driver's pointer; null when the slot is free */
	uint32_t gen;  /* bumped on every reuse, so an old name stays old */
};
static std::vector<SyncSlot> g_syncs;
/* slots with no sync, to be taken without a search. A function's static, not
 * a file's: as a second vector among this file's globals it made the mingw
 * GCC 13 that CI uses fail with an internal error in the file's static
 * initializers (choose_baseaddr, without _GLIBCXX_ASSERTIONS). */
static std::vector<uint32_t> &syncFree()
{
	static std::vector<uint32_t> list;
	return list;
}

/* A state load leaves fences behind that no one will delete: those made after
 * the restored frame, which the restored guest never heard of. Once one has
 * signalled it answers exactly as a name the bridge does not know (a fence
 * that is over), so at the next fence after a load that rebuilds, every
 * signalled one is deleted and its slot given back. It waits for the next
 * fence because only then is the bridge's context the current one.
 * (g_syncReap, set by ce_gl_state_loaded) */

static void syncReap()
{
	g_syncReap = false;
	for (size_t i = 0; i < g_syncs.size(); i++)
	{
		SyncSlot &slot = g_syncs[i];
		if (slot.sync == nullptr) continue;
		GLint status = 0;
		glGetSynciv(slot.sync, GL_SYNC_STATUS, 1, nullptr, &status);
		if (status != GL_SIGNALED) continue;
		glDeleteSync(slot.sync);
		slot.sync = nullptr;
		syncFree().push_back((uint32_t)i);
	}
}

/* The name for a driver sync: generation in the high half, slot+1 in the low,
 * so a name is never zero and never collides with a name from a past life. */
static uintptr_t syncName(GLsync sync)
{
	if (g_syncReap) syncReap();
	size_t at;
	if (!syncFree().empty())
	{
		at = syncFree().back();
		syncFree().pop_back();
	}
	else
	{
		/* A new slot's generations start where this process says, not at 0:
		 * every process named its first fence 0x100000001, so a name in a state
		 * from another process could be a live, unrelated fence here (engine
		 * analysis, 2026-10-18). The context id is this process's own. */
		static const uint32_t base = static_cast<uint32_t>(g_context_id ^ (g_context_id >> 32));
		at = g_syncs.size();
		g_syncs.push_back(SyncSlot{ nullptr, base });
	}
	g_syncs[at].sync = sync;
	g_syncs[at].gen++;
	return (uintptr_t)ce_gl_sync_name_make((uint64_t)at, g_syncs[at].gen);
}

/* The driver sync a name stands for, or null if the name is not a live one -
 * which is what every name restored from an older session looks like. */
static GLsync syncFor(GLsync name)
{
	const uint64_t v = (uint64_t)(uintptr_t)name;
	const uint64_t slot = ce_gl_sync_name_slot(v);
	if (slot >= (uint64_t)g_syncs.size()) return nullptr;
	const SyncSlot &s = g_syncs[(size_t)slot];
	if (s.gen != ce_gl_sync_name_generation(v)) return nullptr;
	return s.sync;
}

/* Gives the slot back, so a later fence can have it under a new generation. */
static void syncForget(GLsync name)
{
	const uint64_t v = (uint64_t)(uintptr_t)name;
	const uint64_t slot = ce_gl_sync_name_slot(v);
	if (slot >= (uint64_t)g_syncs.size()) return;
	SyncSlot &s = g_syncs[(size_t)slot];
	if (s.gen == ce_gl_sync_name_generation(v) && s.sync != nullptr)
	{
		s.sync = nullptr;
		syncFree().push_back((uint32_t)slot);
	}
}

/* ---------------------------------------------------------------------------
 * Buffer names, recycled.
 *
 * Measured on a GTX 1060 (Ruffle, New Star Soccer): a hundred and thirty-nine
 * buffers created, filled with glBufferData, and deleted EVERY FRAME - the
 * counts pair exactly - costing 1.74 ms in glGenBuffers alone. Twelve
 * microseconds a call, for an entry point that is supposed to do nothing but
 * reserve a name.
 *
 * It does nothing but reserve a name when the driver is idle. It is not idle:
 * the buffers being deleted are ones the GPU is still reading, so the delete
 * is deferred and the next reservation waits behind that queue. The churn pays
 * for itself twice over.
 *
 * So a deleted buffer's name is kept rather than given back to the driver, and
 * the next request for one is answered from that list. Four things make this
 * safe rather than clever:
 *
 *  - a recycled buffer is RE-SPECIFIED before use. Every one of those
 *    hundred and thirty-nine is followed by a glBufferData, which replaces its
 *    size and its contents outright and orphans whatever the GPU still held.
 *    Immutable storage would not allow that, and a guest CAN have it (Dolphin
 *    does), so a buffer given immutable storage is never pooled - see the last
 *    point.
 *  - only names this bridge HANDED OUT are pooled. A guest deleting something
 *    it never generated is a guest with a bug, and passing that through to the
 *    driver is how it stays visible.
 *  - the list is bounded. Past the cap the delete is a real delete, so a guest
 *    that frees far more than it allocates cannot make this a leak.
 *  - a MAPPED buffer, or one with IMMUTABLE storage, is never pooled. A real
 *    delete unmaps it and frees its storage; a pooled name keeps both, so the
 *    guest's next buffer would come back still mapped, or refuse its
 *    glBufferStorage, and the map after it would hand the guest NULL. The
 *    first point assumes no guest has immutable storage, and Dolphin does:
 *    its stream buffers are glBufferStorage and persistently mapped, and its
 *    rebuild wrote a batch of vertices to address 0 through a recycled one
 *    (issue #43). Maps, unmaps and storage are tracked as they cross
 *    (gl_buffer_pool.h), and such a buffer's delete is real.
 *
 * What it does NOT do is textures, and the reason is worth recording: they are
 * created with glTexStorage2D, which is immutable. A recycled texture would
 * have to be handed back for exactly the shape it already has, and the shape is
 * not known until the call AFTER the one that has to choose. Doing it properly
 * means translating texture names throughout the bridge, which is a great deal
 * of surface for the megabyte-a-frame this would save.
 */
static bool glPool()
{
	static const bool on = getenv("CHIMERA_GL_NO_POOL") == nullptr;
	return on;
}

/* Both lazily constructed rather than namespace-scope globals: see auditLives()
 * for why this file must not have a static initialiser. */
/* CHIMERA_GL_POOL_TRACE: every buffer name the pool hands out or takes back,
 * and every map, unmap and immutable storage it notes, as it happens. */
static bool poolTrace()
{
	static const bool on = getenv("CHIMERA_GL_POOL_TRACE") != nullptr;
	return on;
}

/* The bookkeeping itself lives in gl_buffer_pool.h, apart from the GL calls,
 * so test_gl_buffer_pool.cpp can check it without a driver. */
static CeGlBufferPool &bufferPool()
{
	static CeGlBufferPool pool;
	return pool;
}

/* What each buffer target has bound, as the binds crossed the bridge. A pooled
 * delete unbinds its name where a real one would - GL unbinds a deleted buffer
 * from every target of the current context, and a pooled name left bound to
 * the pixel-unpack target made the next upload read the guest's pointer as an
 * offset into it. Asking the driver for every target on every delete would
 * cost more than the pool saves, so this is consulted instead; it is only a
 * hint, and the driver is asked before anything is unbound. */
struct BoundRecord
{
	GLenum target;
	GLuint name;
};
static BoundRecord g_bound[] = {
	{ GL_ARRAY_BUFFER, 0 }, { GL_ELEMENT_ARRAY_BUFFER, 0 }, { GL_PIXEL_PACK_BUFFER, 0 },
	{ GL_PIXEL_UNPACK_BUFFER, 0 }, { GL_UNIFORM_BUFFER, 0 }, { GL_COPY_READ_BUFFER, 0 },
	{ GL_COPY_WRITE_BUFFER, 0 }, { 0x8C8E, 0 } /* GL_TRANSFORM_FEEDBACK_BUFFER */,
	{ 0x8C2A, 0 } /* GL_TEXTURE_BUFFER */, { 0x8F3F, 0 } /* GL_DRAW_INDIRECT_BUFFER */,
	{ 0x90EE, 0 } /* GL_DISPATCH_INDIRECT_BUFFER */, { 0x90D2, 0 } /* GL_SHADER_STORAGE_BUFFER */,
	{ 0x92C0, 0 } /* GL_ATOMIC_COUNTER_BUFFER */, { 0x9192, 0 } /* GL_QUERY_BUFFER */,
};

static void recordBind(GLenum target, GLuint name)
{
	for (BoundRecord &r : g_bound)
		if (r.target == target) { r.name = name; return; }
}

static GLuint boundBuffer(GLenum target);

/* A name leaves every target it was bound to: really (`unbind`, a pooled
 * delete, after the driver confirms it is still there) or in the record only
 * (a real delete, which unbinds by itself). */
static void forgetBinds(GLuint name, bool unbind)
{
	if (name == 0) return;
	for (BoundRecord &r : g_bound)
	{
		if (r.name != name) continue;
		if (unbind && boundBuffer(r.target) == name) glBindBuffer(r.target, 0);
		r.name = 0;
	}
}

/* Called once a buffer is mapped or given storage on a target boundBuffer() does
 * not know: the pool can no longer tell which names are safe to recycle, so it
 * stops for the rest of the process and gives back what it holds. */
static void poolDistrust(GLenum target)
{
	if (bufferPool().isDistrusted()) return;
	fprintf(stderr, "[ce-gl] buffer pool off: a buffer was mapped or given storage on target %#x,"
		" which it cannot track\n", (unsigned)target);
	const std::vector<uint32_t> held = bufferPool().distrust();
	if (!held.empty()) glDeleteBuffers((GLsizei)held.size(), held.data());
}

/* The buffer a map, unmap or storage on `target` names: whatever is bound there.
 * Every buffer target GL has, by value, because the texel buffer Dolphin gives
 * immutable storage lives on GL_TEXTURE_BUFFER and a list without it let that
 * name be recycled (issue #43). A target not listed distrusts the pool. */
static GLuint boundBuffer(GLenum target)
{
	GLenum binding;
	switch (target)
	{
		case 0x8C2A: binding = 0x8C2A; break; /* GL_TEXTURE_BUFFER -> GL_TEXTURE_BUFFER_BINDING */
		case 0x90EE: binding = 0x90EF; break; /* GL_DISPATCH_INDIRECT_BUFFER -> _BINDING */
		case 0x92C0: binding = 0x92C1; break; /* GL_ATOMIC_COUNTER_BUFFER -> _BINDING */
		case 0x9192: binding = 0x9193; break; /* GL_QUERY_BUFFER -> _BINDING */
		case GL_ARRAY_BUFFER: binding = GL_ARRAY_BUFFER_BINDING; break;
		case GL_ELEMENT_ARRAY_BUFFER: binding = GL_ELEMENT_ARRAY_BUFFER_BINDING; break;
		case GL_PIXEL_PACK_BUFFER: binding = GL_PIXEL_PACK_BUFFER_BINDING; break;
		case GL_PIXEL_UNPACK_BUFFER: binding = GL_PIXEL_UNPACK_BUFFER_BINDING; break;
		case GL_UNIFORM_BUFFER: binding = GL_UNIFORM_BUFFER_BINDING; break;
		case GL_COPY_READ_BUFFER: binding = GL_COPY_READ_BUFFER_BINDING; break;
		case GL_COPY_WRITE_BUFFER: binding = GL_COPY_WRITE_BUFFER_BINDING; break;
		case GL_TRANSFORM_FEEDBACK_BUFFER: binding = GL_TRANSFORM_FEEDBACK_BUFFER_BINDING; break;
		case GL_SHADER_STORAGE_BUFFER: binding = GL_SHADER_STORAGE_BUFFER_BINDING; break;
		case GL_DRAW_INDIRECT_BUFFER: binding = GL_DRAW_INDIRECT_BUFFER_BINDING; break;
		default:
			poolDistrust(target);
			return 0;
	}
	GLint name = 0;
	glGetIntegerv(binding, &name);
	return (GLuint)name;
}

/* ---------------------------------------------------------------------------
 * Shadows of mapped buffers (owner's decision, 2026-10-19).
 *
 * A mapped buffer is the driver's memory, and a guest given its pointer writes
 * into the host's memory: those bytes are in no state, and the pointer dangles
 * once the context goes (a load that rebuilds makes a fresh one). So a mapping the guest
 * flushes or unmaps is a buffer of the guest's own: the generated wrapper
 * hands one in (b, with its size in c), it is filled from the driver here, its
 * bytes are copied to the driver on each flush and on the unmap, and the unmap
 * gives it back to be freed (miniBox gl-bridge.h, GL_OP_BUFFER_SIZE). A
 * persistent map not flushed explicitly - coherent, or only read - keeps the
 * driver's pointer: its writes reach the GPU with no call at all. A guest
 * built before this hands in no buffer and gets the driver's pointer as ever.
 */
struct GlShadow
{
	GLuint buffer;
	uint8_t *driver;     /* the driver's mapping */
	uint8_t *shadow;     /* the guest's buffer */
	GLsizeiptr length;
	bool write;          /* copied to the driver on the unmap */
	bool explicitFlush;  /* only flushed ranges are copied */
	bool persistent;     /* kept open across a load (shadowReap) */
};

static std::vector<GlShadow> &glShadows()
{
	static std::vector<GlShadow> list;
	return list;
}

static GlShadow *shadowOf(GLuint buffer)
{
	for (GlShadow &s : glShadows())
		if (s.buffer == buffer) return &s;
	return nullptr;
}

static void shadowForget(GLuint buffer)
{
	std::vector<GlShadow> &list = glShadows();
	for (size_t i = 0; i < list.size(); i++)
	{
		if (list[i].buffer != buffer) continue;
		list.erase(list.begin() + (std::ptrdiff_t)i);
		return;
	}
}

/* After a map: the guest's buffer in place of the driver's pointer, filled with
 * what the buffer holds when the guest may read it or the unmap copies all of
 * it back - a write covering part of the range must leave the rest as it was.
 * A map that lets the old bytes go is not filled, nor one written and flushed
 * range by range: only what is flushed goes back. Reading a mapping a driver
 * made for writing is slow (write-combined memory: the Vulkan bridge measured
 * 25 ms for 2 MB on a GTX 1060). Anything amiss answers the driver's pointer,
 * which the guest takes as a host that does not shadow. */
static bool shadowFills(GLbitfield access)
{
	if ((access & GL_MAP_READ_BIT) != 0) return true;
	return (access & (GL_MAP_INVALIDATE_RANGE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT | GL_MAP_FLUSH_EXPLICIT_BIT)) == 0;
}

static uintptr_t shadowMap(void *mapped, GLuint buffer, GLsizeiptr length, bool write, bool fill,
                           bool explicitFlush, bool persistent, uintptr_t b, uintptr_t c)
{
	if (mapped == nullptr || b == 0 || buffer == 0 || length <= 0 || (uintptr_t)length > c) return (uintptr_t)mapped;
	if (!guestCan(reinterpret_cast<const void *>(b), (size_t)length, true)) return (uintptr_t)mapped;
	uint8_t *shadow = reinterpret_cast<uint8_t *>(b);
	if (fill) std::memcpy(shadow, mapped, (size_t)length);
	/* CHIMERA_GL_SHADOW_TRACE: the first shadows made, and how many */
	static const bool trace = getenv("CHIMERA_GL_SHADOW_TRACE") != nullptr;
	static uint64_t made;
	if (trace && (++made <= 20 || (made & (made - 1)) == 0))
		fprintf(stderr, "[ce-gl-shadow] #%llu: buffer %u, %lld bytes%s%s%s\n", (unsigned long long)made, (unsigned)buffer,
			(long long)length, write ? ", written" : ", read", fill ? "" : ", not filled", explicitFlush ? ", flushed by range" : "");
	shadowForget(buffer);
	glShadows().push_back(GlShadow{ buffer, static_cast<uint8_t *>(mapped), shadow, length, write, explicitFlush, persistent });
	return b;
}

/* A flush: the range's bytes to the driver (offset is from the mapping's start). */
static void shadowFlush(GLuint buffer, GLintptr offset, GLsizeiptr length)
{
	GlShadow *s = shadowOf(buffer);
	if (s == nullptr || offset < 0 || length <= 0 || offset + length > s->length) return;
	std::memcpy(s->driver + offset, s->shadow + offset, (size_t)length);
}

/* Before an unmap: the bytes to the driver, unless they were only read or are
 * flushed range by range; the guest's buffer, to give back, or null. */
static uint8_t *shadowUnmap(GLuint buffer)
{
	GlShadow *s = shadowOf(buffer);
	if (s == nullptr) return nullptr;
	if (s->write && !s->explicitFlush) std::memcpy(s->driver, s->shadow, (size_t)s->length);
	uint8_t *given = s->shadow;
	shadowForget(buffer);
	return given;
}

static void shadowGiveBack(uint8_t *given, uintptr_t b)
{
	if (given == nullptr || b == 0 || !guestCan(reinterpret_cast<const void *>(b), sizeof(uint64_t), true)) return;
	*reinterpret_cast<uint64_t *>(b) = (uint64_t)(uintptr_t)given;
}

/* After a load that keeps the context (a core that keeps its objects, or a
 * second session holding the context): a mapping that is not persistent was
 * made by the frames the load threw away, and the restored machine does not
 * know it - it may even be in the middle of a map of its own, made with
 * another buffer of its own. So at the first GL call after the load the
 * driver's mapping is closed, nothing is copied (the guest's memory is the
 * restored machine's now), and nothing is given back to free: an unmap the
 * restored machine makes is then refused by the driver, and its next map
 * works. A persistent mapping is kept: it is made once and the restored
 * machine holds the same buffer. */
static void shadowReap()
{
	g_shadowReap = false;
	std::vector<GlShadow> &list = glShadows();
	size_t closed = 0;
	for (size_t i = 0; i < list.size();)
	{
		if (list[i].persistent) { i++; continue; }
		const GLuint buffer = list[i].buffer;
		list.erase(list.begin() + (std::ptrdiff_t)i);
		if (glad_glUnmapNamedBuffer != nullptr)
			glUnmapNamedBuffer(buffer);
		else
		{
			GLint was = 0;
			glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &was);
			glBindBuffer(GL_COPY_WRITE_BUFFER, buffer);
			glUnmapBuffer(GL_COPY_WRITE_BUFFER);
			glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)was);
		}
		bufferPool().noteMapped(buffer, false);
		closed++;
	}
	static bool said;
	if (closed != 0 && !said)
	{
		said = true;
		fprintf(stderr, "chimera gl: a load left %zu mapped buffer(s) open from the frames it undid; closed\n", closed);
	}
}


/* Textures are NOT recycled, and the reason is worth keeping.
 *
 * They are created with glTexStorage2D, which is immutable: a recycled texture
 * would have to be handed back for exactly the shape it already has, and the
 * shape is not known until the call AFTER the one that has to choose a name.
 * Doing it properly means translating texture names throughout the bridge,
 * which is a great deal of surface for the megabyte a frame it would save.
 *
 * Deferring the DELETES was tried instead - hold them a few frames so the GPU
 * is finished before the driver is told, on the theory that glGenTextures is
 * dear (eleven microseconds, against sixty nanoseconds for the glTexStorage2D
 * behind it) because it drains a queue of deferred deletes. It is not: measured
 * three ways against buffer pooling alone, it moved nothing outside the noise,
 * and glGenTextures cost the same either way. Removed rather than kept on the
 * strength of a plausible story, because it holds textures the guest has
 * finished with and that is a real cost to carry for nothing.
 */

/* One call's time, in the driver's share and, under CHIMERA_GL_PROFILE, in
 * its opcode's counts. */
static void noteTook(uintptr_t op, uint64_t took)
{
	g_driverNs += took;
	if (!glProfile()) return;
	if (g_opCalls == nullptr)
	{
		g_opCalls = (uint64_t *)calloc(kProfileOps, sizeof(uint64_t));
		g_opNs = (uint64_t *)calloc(kProfileOps, sizeof(uint64_t));
		g_opMax = (uint64_t *)calloc(kProfileOps, sizeof(uint64_t));
		g_opSlow = (uint64_t *)calloc(kProfileOps, sizeof(uint64_t));
		if (g_opCalls == nullptr || g_opNs == nullptr
			|| g_opMax == nullptr || g_opSlow == nullptr)
		{
			free(g_opCalls); free(g_opNs); free(g_opMax); free(g_opSlow);
			g_opCalls = nullptr; g_opNs = nullptr; g_opMax = nullptr; g_opSlow = nullptr;
			return;
		}
		/* ce_gl_release marks the frame boundary, and a host that is not
		 * drawing never calls it - so a run with rendering off would
		 * otherwise collect the whole profile and print none of it. */
		atexit(profileDump);
	}
	if (op >= (uintptr_t)kProfileOps) return;
	g_opCalls[op]++;
	g_opNs[op] += took;
	if (took > g_opMax[op]) g_opMax[op] = took;
	if (took > kSlowNs) g_opSlow[op]++;
	if (glWhy())
	{
		if (took > kSlowNs && g_whyLeft > 0)
		{
			g_whyLeft--;
			fprintf(stderr, "[ce-gl-why] op %lu blocked %.0f us after:",
				(unsigned long)op, (double)took / 1e3);
			for (unsigned i = 0; i < 16; i++)
			{
				const unsigned at = (g_whyAt + i) % 16;
				if (g_whyRing[at] != 0) fprintf(stderr, " %u", g_whyRing[at]);
			}
			fprintf(stderr, "\n");
			fflush(stderr);
		}
		g_whyRing[g_whyAt] = (unsigned)op;
		g_whyAt = (g_whyAt + 1) % 16;
	}
}

/* The buffer pool's two calls (see bufferPool()): glGenBuffers served from
 * recycled names where it can, glDeleteBuffers into the pool where it can. */
static void poolCall(uintptr_t op, uintptr_t a)
{
	if (op == CHIMERA_GL_OP_glGenBuffers)
	{
		struct ChimeraGlArgs_glGenBuffers *args = (struct ChimeraGlArgs_glGenBuffers *)a;
		GLsizei served = 0;
		while (served < args->n)
		{
			const GLuint name = bufferPool().take();
			if (name == 0) break;
			args->buffers[served++] = name;
		}
		if (served < args->n)
		{
			glGenBuffers(args->n - served, args->buffers + served);
			for (GLsizei i = served; i < args->n; i++) bufferPool().made(args->buffers[i]);
		}
		/* Including the pooled ones: a name the pool hands back IS a different
		 * object as far as the guest is concerned, and counting it is the whole
		 * point of the audit. */
		for (GLsizei i = 0; i < args->n; i++) auditGen(kAuditBuffer, args->buffers[i]);
		if (poolTrace())
			for (GLsizei i = 0; i < args->n; i++)
				fprintf(stderr, "[pool] gen %u (%s)\n", (unsigned)args->buffers[i], i < served ? "recycled" : "driver");
		return;
	}
	struct ChimeraGlArgs_glDeleteBuffers *args = (struct ChimeraGlArgs_glDeleteBuffers *)a;
	for (GLsizei i = 0; i < args->n; i++)
	{
		const GLuint name = args->buffers[i];
		const bool ours = bufferPool().isOurs(name), mapped = bufferPool().isMapped(name),
			immutable = bufferPool().isImmutable(name);
		const CeGlBufferPool::Delete what = bufferPool().deleted(name);
		if (poolTrace())
			fprintf(stderr, "[pool] delete %u: ours=%d mapped=%d immutable=%d -> %s\n", (unsigned)name,
				(int)ours, (int)mapped, (int)immutable,
				what == CeGlBufferPool::Delete::Ignore ? "already deleted, ignored"
					: what == CeGlBufferPool::Delete::Pool ? "pooled" : "real");
		/* not ours, still mapped, immutable, or the pool is full: a real
		 * delete, so a guest bug stays visible, a mapping and a storage end
		 * the way the guest meant, and a lopsided guest cannot leak - and
		 * either way the name leaves the targets it was bound to, as GL's
		 * own delete would have it */
		if (what == CeGlBufferPool::Delete::Real)
		{
			glDeleteBuffers(1, &name);
			forgetBinds(name, false);
		}
		else if (what == CeGlBufferPool::Delete::Pool)
		{
			forgetBinds(name, true);
		}
	}
}

static uintptr_t BRIDGE_ABI glDispatchBody(uintptr_t op, uintptr_t a, uintptr_t b,
                                          uintptr_t c, uintptr_t d, uintptr_t e);

extern "C" uint64_t ce_guest_work;   /* session.cpp */

/* Each call is the core's work once the driver has answered it: one that took
 * long (a shader compiled) is not a stuck core (session.cpp, ce_guest_work).
 * Counted here rather than by a destructor in the body: a sysv function may
 * carry no C++ unwind data under mingw (".seh_handlerdata used outside of
 * .seh_proc"). */
static void noteNames(uintptr_t op, uintptr_t a, uintptr_t r);

extern "C" uintptr_t BRIDGE_ABI ce_gl_dispatch(uintptr_t op, uintptr_t a, uintptr_t b,
                                               uintptr_t c, uintptr_t d, uintptr_t e)
{
	const uintptr_t r = glDispatchBody(op, a, b, c, d, e);
	noteNames(op, a, r);
	__atomic_store_n(&ce_guest_work, __atomic_load_n(&ce_guest_work, __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
	return r;
}

/* The names the core holds (gl_names.h): what each call that makes or deletes
 * objects did to them, as the core sees it - a delete takes a name away
 * whatever the pool or the driver then does with it. Only while the context
 * is ours: a call that was not run made nothing. */
static CeGlNames &glNames()
{
	static CeGlNames names;
	return names;
}

static void namesOf(int kind, GLsizei n, const GLuint *list, bool made)
{
	if (n <= 0 || n > (1 << 20) || list == nullptr || !guestCan(list, (size_t)n * sizeof(GLuint), false)) return;
	for (GLsizei i = 0; i < n; i++)
	{
		if (made) glNames().made(kind, list[i]);
		else glNames().gone(kind, list[i]);
		/* a deleted buffer is unmapped by the driver: its shadow stays with the guest */
		if (!made && kind == kNameBuffer && !glShadows().empty()) shadowForget(list[i]);
	}
}

/* CHIMERA_GL_STALE: the first uses of a name the core held from before a fresh
 * context (renewContext) - a bind, an attach, a read - which reach an empty
 * name: the object it meant went with the old context, and the core did not
 * make it again. A core that rebuilds everything never shows one. */
static void staleSay(int kind, GLuint name, const char *what)
{
	static int left = 60;
	if (left <= 0 || name == 0 || (glNames().isLive(kind, name) && !glNames().isKept(kind, name))) return;
	const bool never = !glNames().isLive(kind, name);
	left--;
	/* with the calls before it, from the flight recorder: what the core was doing */
	char before[160] = "";
	size_t at = 0;
	for (uint64_t i = g_flight.written > 12 ? g_flight.written - 12 : 0; i + 1 < g_flight.written && at + 12 < sizeof before; i++)
	{
		const uint32_t o = g_flight.entries[i & (kFlightCapacity - 1)].op;
		at += (size_t)snprintf(before + at, sizeof before - at, " %u", (unsigned)o);
	}
	fprintf(stderr, "[ce-gl-stale] %s names %u, %s (calls before it:%s)%s\n", what, (unsigned)name,
		never ? "which no call the bridge saw made" : "held from before the fresh context", before,
		left == 0 ? " (no more said)" : "");
}

static void staleList(int kind, GLsizei n, const GLuint *list, const char *what)
{
	if (n <= 0 || n > 64 || list == nullptr || !guestCan(list, (size_t)n * sizeof(GLuint), false)) return;
	for (GLsizei i = 0; i < n; i++) staleSay(kind, list[i], what);
}

static void staleUse(uintptr_t op, uintptr_t a)
{
	static const bool on = getenv("CHIMERA_GL_STALE") != nullptr;
	if (!on) return;
#define CE_STALE(OP, KIND, FIELD) \
	case CHIMERA_GL_OP_##OP: staleSay(KIND, reinterpret_cast<ChimeraGlArgs_##OP *>(a)->FIELD, #OP); break;
#define CE_STALE2(OP, KIND1, FIELD1, KIND2, FIELD2) \
	case CHIMERA_GL_OP_##OP: \
		staleSay(KIND1, reinterpret_cast<ChimeraGlArgs_##OP *>(a)->FIELD1, #OP); \
		staleSay(KIND2, reinterpret_cast<ChimeraGlArgs_##OP *>(a)->FIELD2, #OP); \
		break;
#define CE_STALES(OP, KIND, COUNT, LIST) \
	case CHIMERA_GL_OP_##OP: \
		staleList(KIND, reinterpret_cast<ChimeraGlArgs_##OP *>(a)->COUNT, reinterpret_cast<ChimeraGlArgs_##OP *>(a)->LIST, #OP); \
		break;
	switch (op)
	{
		CE_STALE(glBindTexture, kNameTexture, texture)
		CE_STALE(glBindTextureUnit, kNameTexture, texture)
		CE_STALES(glBindTextures, kNameTexture, count, textures)
		CE_STALE(glBindImageTexture, kNameTexture, texture)
		CE_STALE(glGetTextureSubImage, kNameTexture, texture)
		CE_STALE(glTextureSubImage2D, kNameTexture, texture)
		CE_STALE(glTextureSubImage3D, kNameTexture, texture)
		CE_STALE(glTextureStorage2D, kNameTexture, texture)
		CE_STALE(glTextureStorage3D, kNameTexture, texture)
		CE_STALE(glTextureParameteri, kNameTexture, texture)
		CE_STALE(glGenerateTextureMipmap, kNameTexture, texture)
		CE_STALE(glCopyTextureSubImage2D, kNameTexture, texture)
		CE_STALE(glClearTexImage, kNameTexture, texture)
		CE_STALE(glClearTexSubImage, kNameTexture, texture)
		CE_STALE2(glTextureView, kNameTexture, texture, kNameTexture, origtexture)
		CE_STALE2(glCopyImageSubData, kNameTexture, srcName, kNameTexture, dstName)
		CE_STALE(glFramebufferTexture, kNameTexture, texture)
		CE_STALE(glFramebufferTexture2D, kNameTexture, texture)
		CE_STALE(glFramebufferTextureLayer, kNameTexture, texture)
		CE_STALE2(glNamedFramebufferTexture, kNameFramebuffer, framebuffer, kNameTexture, texture)
		CE_STALE(glFramebufferRenderbuffer, kNameRenderbuffer, renderbuffer)
		CE_STALE2(glNamedFramebufferRenderbuffer, kNameFramebuffer, framebuffer, kNameRenderbuffer, renderbuffer)
		CE_STALE2(glBlitNamedFramebuffer, kNameFramebuffer, readFramebuffer, kNameFramebuffer, drawFramebuffer)
		CE_STALE(glBindBuffer, kNameBuffer, buffer)
		CE_STALE(glBindBufferBase, kNameBuffer, buffer)
		CE_STALE(glBindBufferRange, kNameBuffer, buffer)
		CE_STALES(glBindBuffersBase, kNameBuffer, count, buffers)
		CE_STALE(glBindVertexBuffer, kNameBuffer, buffer)
		CE_STALE(glNamedBufferSubData, kNameBuffer, buffer)
		CE_STALE(glNamedBufferData, kNameBuffer, buffer)
		CE_STALE2(glCopyNamedBufferSubData, kNameBuffer, readBuffer, kNameBuffer, writeBuffer)
		CE_STALE(glTexBuffer, kNameBuffer, buffer)
		CE_STALE2(glTextureBuffer, kNameTexture, texture, kNameBuffer, buffer)
		CE_STALE(glBindFramebuffer, kNameFramebuffer, framebuffer)
		CE_STALE(glBindRenderbuffer, kNameRenderbuffer, renderbuffer)
		CE_STALE(glBindSampler, kNameSampler, sampler)
		CE_STALES(glBindSamplers, kNameSampler, count, samplers)
		CE_STALE(glBindVertexArray, kNameVertexArray, array)
		CE_STALE(glUseProgram, kNameProgram, program)
		CE_STALE(glAttachShader, kNameProgram, program)
		default: break;
	}
#undef CE_STALE
#undef CE_STALE2
#undef CE_STALES
}

static void noteNames(uintptr_t op, uintptr_t a, uintptr_t r)
{
	if (!g_borrowed) return;
	staleUse(op, a);
#define CE_NAMES(OP, KIND, COUNT, LIST, MADE) \
	case CHIMERA_GL_OP_##OP: { auto *p = reinterpret_cast<ChimeraGlArgs_##OP *>(a); namesOf(KIND, p->COUNT, p->LIST, MADE); break; }
	switch (op)
	{
		CE_NAMES(glGenTextures, kNameTexture, n, textures, true)
		CE_NAMES(glCreateTextures, kNameTexture, n, textures, true)
		CE_NAMES(glDeleteTextures, kNameTexture, n, textures, false)
		CE_NAMES(glGenBuffers, kNameBuffer, n, buffers, true)
		CE_NAMES(glCreateBuffers, kNameBuffer, n, buffers, true)
		CE_NAMES(glDeleteBuffers, kNameBuffer, n, buffers, false)
		CE_NAMES(glGenFramebuffers, kNameFramebuffer, n, framebuffers, true)
		CE_NAMES(glCreateFramebuffers, kNameFramebuffer, n, framebuffers, true)
		CE_NAMES(glDeleteFramebuffers, kNameFramebuffer, n, framebuffers, false)
		CE_NAMES(glGenRenderbuffers, kNameRenderbuffer, n, renderbuffers, true)
		CE_NAMES(glCreateRenderbuffers, kNameRenderbuffer, n, renderbuffers, true)
		CE_NAMES(glDeleteRenderbuffers, kNameRenderbuffer, n, renderbuffers, false)
		CE_NAMES(glGenSamplers, kNameSampler, count, samplers, true)
		CE_NAMES(glCreateSamplers, kNameSampler, n, samplers, true)
		CE_NAMES(glDeleteSamplers, kNameSampler, count, samplers, false)
		CE_NAMES(glGenQueries, kNameQuery, n, ids, true)
		CE_NAMES(glCreateQueries, kNameQuery, n, ids, true)
		CE_NAMES(glDeleteQueries, kNameQuery, n, ids, false)
		CE_NAMES(glGenVertexArrays, kNameVertexArray, n, arrays, true)
		CE_NAMES(glCreateVertexArrays, kNameVertexArray, n, arrays, true)
		CE_NAMES(glDeleteVertexArrays, kNameVertexArray, n, arrays, false)
		CE_NAMES(glGenProgramPipelines, kNamePipeline, n, pipelines, true)
		CE_NAMES(glCreateProgramPipelines, kNamePipeline, n, pipelines, true)
		CE_NAMES(glDeleteProgramPipelines, kNamePipeline, n, pipelines, false)
		CE_NAMES(glGenTransformFeedbacks, kNameTransformFeedback, n, ids, true)
		CE_NAMES(glCreateTransformFeedbacks, kNameTransformFeedback, n, ids, true)
		CE_NAMES(glDeleteTransformFeedbacks, kNameTransformFeedback, n, ids, false)
		case CHIMERA_GL_OP_glCreateProgram:
		case CHIMERA_GL_OP_glCreateShader:
		case CHIMERA_GL_OP_glCreateShaderProgramv:
			glNames().made(kNameProgram, (uint32_t)r);
			break;
		case CHIMERA_GL_OP_glDeleteProgram:
			glNames().gone(kNameProgram, reinterpret_cast<ChimeraGlArgs_glDeleteProgram *>(a)->program);
			break;
		case CHIMERA_GL_OP_glDeleteShader:
			glNames().gone(kNameProgram, reinterpret_cast<ChimeraGlArgs_glDeleteShader *>(a)->shader);
			break;
		default: break;
	}
#undef CE_NAMES
}

/* The guest's own record of the highest name of each kind it was handed
 * (miniBox gl-bridge.h, GL_OP_NAME_MARKS), in guest memory: a state carries
 * it, so after a load it is the restored machine's, whichever process took
 * the state. Null for a guest that does not keep one, and once its session
 * has ended. */
static const ChimeraGlNameMarks *g_nameMarks;
static_assert(int(CHIMERA_GL_NAME_TEXTURE) == int(kNameTexture) && int(CHIMERA_GL_NAME_BUFFER) == int(kNameBuffer)
	&& int(CHIMERA_GL_NAME_FRAMEBUFFER) == int(kNameFramebuffer) && int(CHIMERA_GL_NAME_RENDERBUFFER) == int(kNameRenderbuffer)
	&& int(CHIMERA_GL_NAME_SAMPLER) == int(kNameSampler) && int(CHIMERA_GL_NAME_QUERY) == int(kNameQuery)
	&& int(CHIMERA_GL_NAME_VERTEX_ARRAY) == int(kNameVertexArray) && int(CHIMERA_GL_NAME_PIPELINE) == int(kNamePipeline)
	&& int(CHIMERA_GL_NAME_TRANSFORM_FEEDBACK) == int(kNameTransformFeedback) && int(CHIMERA_GL_NAME_PROGRAM) == int(kNameProgram)
	&& int(CHIMERA_GL_NAME_KINDS) == int(kNameKinds), "the guest's kinds of name are the host's");

/* the restored machine's highest name of a kind, 0 when its guest keeps no marks */
static uint32_t nameMark(int kind)
{
	const ChimeraGlNameMarks *m = g_nameMarks;
	if (m == nullptr || m->magic != CHIMERA_GL_NAME_MARKS_MAGIC || kind < 0 || (uint32_t)kind >= m->kinds
		|| kind >= (int)(sizeof m->high / sizeof m->high[0]))
		return 0;
	return m->high[kind];
}

/* In a fresh context (renewContext), every name the core held is taken
 * again, empty, before the core runs: names are handed out from 1 in a new
 * context, so a new object could otherwise get a number a stale call in the
 * restored state still uses. The driver hands out the lowest free names, so
 * taking up to the highest and giving back the ones the core does not hold
 * leaves exactly the core's. Programs and shaders share one namespace and
 * are taken as empty programs. Answers how many names are kept. */
static size_t reserveNames()
{
	size_t kept = 0, missed = 0;
	for (int k = 0; k < kNameKinds; k++)
	{
		/* and every name up to the restored machine's own marks: the names
		 * seen made are only this process's, and a state another process
		 * took holds names this one never saw (chimera#367). A name below a
		 * mark that the machine had let go is kept too, empty: a number held
		 * back costs nothing */
		const uint32_t mark = nameMark(k);
		const uint32_t top = std::max(glNames().highest(k), mark);
		if (top == 0) continue;
		std::vector<GLuint> got;
		if (k == kNameProgram)
		{
			for (uint32_t i = 0; i < top + 64u && glad_glCreateProgram != nullptr; i++)
			{
				const GLuint p = glCreateProgram();
				if (p == 0) break;
				got.push_back(p);
				if (p >= top) break;
			}
		}
		else
		{
			got.assign(top, 0);
			switch (k)
			{
				case kNameTexture: glGenTextures((GLsizei)top, got.data()); break;
				case kNameBuffer: glGenBuffers((GLsizei)top, got.data()); break;
				case kNameFramebuffer: glGenFramebuffers((GLsizei)top, got.data()); break;
				case kNameRenderbuffer: glGenRenderbuffers((GLsizei)top, got.data()); break;
				case kNameSampler: if (glad_glGenSamplers) glGenSamplers((GLsizei)top, got.data()); break;
				case kNameQuery: glGenQueries((GLsizei)top, got.data()); break;
				case kNameVertexArray: glGenVertexArrays((GLsizei)top, got.data()); break;
				case kNamePipeline: if (glad_glGenProgramPipelines) glGenProgramPipelines((GLsizei)top, got.data()); break;
				case kNameTransformFeedback: if (glad_glGenTransformFeedbacks) glGenTransformFeedbacks((GLsizei)top, got.data()); break;
				default: break;
			}
		}
		/* what the core does not hold goes back */
		std::vector<GLuint> back;
		std::vector<bool> taken(top + 1, false);
		for (GLuint g : got)
		{
			if (g == 0) continue;
			if (g <= top && (glNames().isLive(k, g) || g <= mark)) taken[g] = true;
			else back.push_back(g);
		}
		for (uint32_t n = 1; n <= top; n++)
		{
			if (!glNames().isLive(k, n)) continue;
			if (taken[n]) kept++;
			else missed++;
		}
		if (back.empty()) continue;
		switch (k)
		{
			case kNameTexture: glDeleteTextures((GLsizei)back.size(), back.data()); break;
			case kNameBuffer: glDeleteBuffers((GLsizei)back.size(), back.data()); break;
			case kNameFramebuffer: glDeleteFramebuffers((GLsizei)back.size(), back.data()); break;
			case kNameRenderbuffer: glDeleteRenderbuffers((GLsizei)back.size(), back.data()); break;
			case kNameSampler: glDeleteSamplers((GLsizei)back.size(), back.data()); break;
			case kNameQuery: glDeleteQueries((GLsizei)back.size(), back.data()); break;
			case kNameVertexArray: glDeleteVertexArrays((GLsizei)back.size(), back.data()); break;
			case kNamePipeline: glDeleteProgramPipelines((GLsizei)back.size(), back.data()); break;
			case kNameTransformFeedback: glDeleteTransformFeedbacks((GLsizei)back.size(), back.data()); break;
			case kNameProgram: for (GLuint g : back) glDeleteProgram(g); break;
			default: break;
		}
	}
	if (missed != 0)
	{
		static bool said;
		if (!said)
		{
			said = true;
			fprintf(stderr, "chimera gl: a fresh context did not hand back %llu of the names the core held"
				" (this driver does not give out the lowest free names)\n", (unsigned long long)missed);
		}
	}
	return kept;
}

static uintptr_t BRIDGE_ABI glDispatchBody(uintptr_t op, uintptr_t a, uintptr_t b,
                                          uintptr_t c, uintptr_t d, uintptr_t e)
{
	(void)d; (void)e;
	g_calls++;
	flightNote((uint32_t)op, 0);

	/* The first GL call of a frame takes the context; ce_gl_release gives it
	 * back when the frame is over. Two calls per frame, not two per GL call. */
	if (!g_borrowed && g_ready)
	{
		/* noted first: a driver that dies making the context current is then
		 * blamed, rather than the call that wanted it */
		flightNote(kFlightBorrowed, 0);
		save_current();
		if (!bind_ours())
		{
			/* The frontend's context is the one current, and the guest's calls
			 * would make objects in it and change its state: none of them is
			 * run. (libglvnd's EGL refuses while a GLX context is current on
			 * the thread.) wglMakeCurrent leaves no context current when it
			 * fails, so what was lent is put back. */
			return_current();
			static bool said;
			if (!said)
			{
				said = true;
				fprintf(stderr, "[ce-gl] the bridge's context could not be made current; the core's GL calls are not run\n");
				fflush(stderr);
			}
			return 0;
		}
		g_borrowed = true;
		gpuTimeFrameBegin();
		if (glTrace())
			fprintf(stderr, "[ce-gl] borrowed the context (GL_VERSION now %s)\n",
				glGetString(GL_VERSION) ? (const char *)glGetString(GL_VERSION) : "(null)");
	}
	if (glLifetime()) lifetimeCheck(op, a);
	if (glAudit())
	{
		switch (op)
		{
			case CHIMERA_GL_OP_glDeleteTextures:
			{
				struct ChimeraGlArgs_glDeleteTextures *p = (struct ChimeraGlArgs_glDeleteTextures *)a;
				for (GLsizei i = 0; i < p->n; i++) auditDelete(kAuditTexture, p->textures[i]);
				break;
			}
			case CHIMERA_GL_OP_glDeleteBuffers:
			{
				struct ChimeraGlArgs_glDeleteBuffers *p = (struct ChimeraGlArgs_glDeleteBuffers *)a;
				for (GLsizei i = 0; i < p->n; i++) auditDelete(kAuditBuffer, p->buffers[i]);
				break;
			}
			case CHIMERA_GL_OP_glDeleteFramebuffers:
			{
				struct ChimeraGlArgs_glDeleteFramebuffers *p = (struct ChimeraGlArgs_glDeleteFramebuffers *)a;
				for (GLsizei i = 0; i < p->n; i++) auditDelete(kAuditFramebuffer, p->framebuffers[i]);
				break;
			}
			default: break;
		}
	}

	/* Recycled buffer names, before anything else looks at the opcode: see
	 * bufferPool(). Both of these answer without reaching the driver when the
	 * pool can serve them, which is the whole point. Timed like any other call
	 * when asked to: they used to return before CHIMERA_GL_PROFILE and
	 * CHIMERA_GL_TIME saw them, so the pool's own driver calls were left out of
	 * the driver's share (engine analysis, 2026-10-18). */
	if (glPool() && (op == CHIMERA_GL_OP_glGenBuffers || op == CHIMERA_GL_OP_glDeleteBuffers))
	{
		if (!glProfile() && !glTime())
		{
			poolCall(op, a);
			return 0;
		}
		const auto at = std::chrono::steady_clock::now();
		poolCall(op, a);
		noteTook(op, (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - at).count());
		return 0;
	}

	/* CHIMERA_GL_CHECK asks the driver, after every crossing, whether that call
	 * upset it. Nothing else can: a core's renderer sees only what this hands
	 * back, so a GL error raised out here is invisible to it and to the user -
	 * the frame simply comes out empty, with nothing anywhere saying why. That
	 * is how a whole class of bridge bugs hides, and this is how they are
	 * found. Off unless asked for; a getenv per call is nothing beside a GL
	 * call. */
	if (glCheck())
	{
		while (glGetError() != GL_NO_ERROR) { }
		const uintptr_t rv = ce_gl_dispatch_one(op, a, b, c, d, e);
		const GLenum err = glGetError();
		if (err != GL_NO_ERROR)
			fprintf(stderr, "[ce-gl!] op=%lu raised %#x\n", (unsigned long)op, (unsigned)err);
		return rv;
	}
	/* Names the driver (or the pool) just handed out, recorded AFTER the call
	 * because that is when the out parameter holds them. */
	if (glAudit())
	{
		switch (op)
		{
			case CHIMERA_GL_OP_glGenTextures:
			{
				const uintptr_t rv = ce_gl_dispatch_one(op, a, b, c, d, e);
				struct ChimeraGlArgs_glGenTextures *p = (struct ChimeraGlArgs_glGenTextures *)a;
				for (GLsizei i = 0; i < p->n; i++) auditGen(kAuditTexture, p->textures[i]);
				return rv;
			}
			case CHIMERA_GL_OP_glGenFramebuffers:
			{
				const uintptr_t rv = ce_gl_dispatch_one(op, a, b, c, d, e);
				struct ChimeraGlArgs_glGenFramebuffers *p = (struct ChimeraGlArgs_glGenFramebuffers *)a;
				for (GLsizei i = 0; i < p->n; i++) auditGen(kAuditFramebuffer, p->framebuffers[i]);
				return rv;
			}
			default: break;
		}
	}

	if (glProfile() || glTime())
	{
		const auto at = std::chrono::steady_clock::now();
		const uintptr_t rv = ce_gl_dispatch_one(op, a, b, c, d, e);
		noteTook(op, (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - at).count());
		return rv;
	}
	return ce_gl_dispatch_one(op, a, b, c, d, e);
}

static uintptr_t ce_gl_dispatch_one(uintptr_t op, uintptr_t a, uintptr_t b,
                                    uintptr_t c, uintptr_t d, uintptr_t e)
{
	(void)d; (void)e;
	if (g_shadowReap) shadowReap();

	/* Buffers whose bytes the driver reads out of guest memory: the range is
	 * asked of the sandbox first (guestCan), so a pointer into memory the core
	 * gave back is a refused call, not a fault in the driver (2026-10-18). A
	 * fine one goes on to the generated case. */
	switch (op)
	{
		case CHIMERA_GL_OP_glBufferData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glBufferData *>(a);
			if (p->data != nullptr && p->size > 0 && !guestCan(p->data, (size_t)p->size, false)) return glRefused(op, "glBufferData", CE_GL_BAD_POINTER);
			break;
		}
		case CHIMERA_GL_OP_glBufferSubData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glBufferSubData *>(a);
			if (p->size > 0 && !guestCan(p->data, (size_t)p->size, false)) return glRefused(op, "glBufferSubData", CE_GL_BAD_POINTER);
			break;
		}
		case CHIMERA_GL_OP_glNamedBufferData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glNamedBufferData *>(a);
			if (p->data != nullptr && p->size > 0 && !guestCan(p->data, (size_t)p->size, false)) return glRefused(op, "glNamedBufferData", CE_GL_BAD_POINTER);
			break;
		}
		case CHIMERA_GL_OP_glNamedBufferSubData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glNamedBufferSubData *>(a);
			if (p->size > 0 && !guestCan(p->data, (size_t)p->size, false)) return glRefused(op, "glNamedBufferSubData", CE_GL_BAD_POINTER);
			break;
		}
		default:
			break;
	}

	/* Readbacks that would have the driver write into guest memory go through
	 * the bounce buffer above rather than the generated case below. */
	switch (op)
	{
		/* Maps and unmaps, noted for the buffer pool (see bufferPool()): it must
		 * never recycle a buffer that is still mapped. */
		case CHIMERA_GL_OP_glMapBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapBufferRange *>(a);
			GL_PROVIDED_OR_REFUSE(glMapBufferRange);
			void *mapped = glMapBufferRange(p->target, p->offset, p->length, p->access);
			const GLuint name = boundBuffer(p->target);
			if (mapped != nullptr && glPool()) bufferPool().noteMapped(name, true);
			if (poolTrace())
				fprintf(stderr, "[pool] mapRange target=%#x bound=%u access=%#x -> %p\n", (unsigned)p->target,
					(unsigned)name, (unsigned)p->access, mapped);
			return shadowMap(mapped, name, p->length, (p->access & GL_MAP_WRITE_BIT) != 0,
				shadowFills(p->access), (p->access & GL_MAP_FLUSH_EXPLICIT_BIT) != 0,
				(p->access & GL_MAP_PERSISTENT_BIT) != 0, b, c);
		}
		case CHIMERA_GL_OP_glMapBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapBuffer *>(a);
			GL_PROVIDED_OR_REFUSE(glMapBuffer);
			void *mapped = glMapBuffer(p->target, p->access);
			const GLuint name = boundBuffer(p->target);
			if (mapped != nullptr && glPool()) bufferPool().noteMapped(name, true);
			GLint64 size = 0;
			if (mapped != nullptr && b != 0 && glad_glGetBufferParameteri64v != nullptr)
				glGetBufferParameteri64v(p->target, GL_BUFFER_SIZE, &size);
			return shadowMap(mapped, name, (GLsizeiptr)size, p->access != GL_READ_ONLY, true, false, false, b, c);
		}
		case CHIMERA_GL_OP_glMapNamedBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapNamedBufferRange *>(a);
			GL_PROVIDED_OR_REFUSE(glMapNamedBufferRange);
			void *mapped = glMapNamedBufferRange(p->buffer, p->offset, p->length, p->access);
			if (mapped != nullptr) bufferPool().noteMapped(p->buffer, true);
			return shadowMap(mapped, p->buffer, p->length, (p->access & GL_MAP_WRITE_BIT) != 0,
				shadowFills(p->access), (p->access & GL_MAP_FLUSH_EXPLICIT_BIT) != 0,
				(p->access & GL_MAP_PERSISTENT_BIT) != 0, b, c);
		}
		case CHIMERA_GL_OP_glMapNamedBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapNamedBuffer *>(a);
			GL_PROVIDED_OR_REFUSE(glMapNamedBuffer);
			void *mapped = glMapNamedBuffer(p->buffer, p->access);
			if (mapped != nullptr) bufferPool().noteMapped(p->buffer, true);
			GLint64 size = 0;
			if (mapped != nullptr && b != 0 && glad_glGetNamedBufferParameteri64v != nullptr)
				glGetNamedBufferParameteri64v(p->buffer, GL_BUFFER_SIZE, &size);
			return shadowMap(mapped, p->buffer, (GLsizeiptr)size, p->access != GL_READ_ONLY, true, false, false, b, c);
		}
		case CHIMERA_GL_OP_glFlushMappedBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glFlushMappedBufferRange *>(a);
			GL_PROVIDED_OR_REFUSE(glFlushMappedBufferRange);
			if (!glShadows().empty()) shadowFlush(boundBuffer(p->target), p->offset, p->length);
			glFlushMappedBufferRange(p->target, p->offset, p->length);
			return 0;
		}
		case CHIMERA_GL_OP_glFlushMappedNamedBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glFlushMappedNamedBufferRange *>(a);
			GL_PROVIDED_OR_REFUSE(glFlushMappedNamedBufferRange);
			shadowFlush(p->buffer, p->offset, p->length);
			glFlushMappedNamedBufferRange(p->buffer, p->offset, p->length);
			return 0;
		}
		case CHIMERA_GL_OP_glUnmapBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glUnmapBuffer *>(a);
			GL_PROVIDED_OR_REFUSE(glUnmapBuffer);
			const GLuint name = boundBuffer(p->target);
			uint8_t *given = shadowUnmap(name);
			const GLboolean ok = glUnmapBuffer(p->target);
			bufferPool().noteMapped(name, false);
			shadowGiveBack(given, b);
			if (poolTrace())
				fprintf(stderr, "[pool] unmap target=%#x bound=%u -> %d\n", (unsigned)p->target, (unsigned)name, (int)ok);
			return (uintptr_t)ok;
		}
		case CHIMERA_GL_OP_glBufferStorage:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glBufferStorage *>(a);
			GL_PROVIDED_OR_REFUSE(glBufferStorage);
			if (p->data != nullptr && p->size > 0 && !guestCan(p->data, (size_t)p->size, false)) return glRefused(op, "glBufferStorage", CE_GL_BAD_POINTER);
			glBufferStorage(p->target, p->size, p->data, p->flags);
			if (glPool()) bufferPool().noteImmutable(boundBuffer(p->target), true);
			if (poolTrace())
				fprintf(stderr, "[pool] storage target=%#x bound=%u size=%lld flags=%#x err=%#x\n", (unsigned)p->target,
					(unsigned)boundBuffer(p->target), (long long)p->size, (unsigned)p->flags, (unsigned)glGetError());
			return 0;
		}
		case CHIMERA_GL_OP_glNamedBufferStorage:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glNamedBufferStorage *>(a);
			GL_PROVIDED_OR_REFUSE(glNamedBufferStorage);
			if (p->data != nullptr && p->size > 0 && !guestCan(p->data, (size_t)p->size, false)) return glRefused(op, "glNamedBufferStorage", CE_GL_BAD_POINTER);
			glNamedBufferStorage(p->buffer, p->size, p->data, p->flags);
			bufferPool().noteImmutable(p->buffer, true);
			return 0;
		}
		case CHIMERA_GL_OP_glUnmapNamedBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glUnmapNamedBuffer *>(a);
			GL_PROVIDED_OR_REFUSE(glUnmapNamedBuffer);
			uint8_t *given = shadowUnmap(p->buffer);
			const GLboolean ok = glUnmapNamedBuffer(p->buffer);
			bufferPool().noteMapped(p->buffer, false);
			shadowGiveBack(given, b);
			return (uintptr_t)ok;
		}

		case CHIMERA_GL_OP_glGetBufferSubData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetBufferSubData *>(a);
			GL_PROVIDED_OR_REFUSE(glGetBufferSubData);
			/* size is exactly the bytes written - no computation to get wrong */
			if (p->size > 0 && !guestCan(p->data, (size_t)p->size, true)) return glRefused(op, "glGetBufferSubData", CE_GL_BAD_POINTER);
			if (p->data == nullptr || p->size <= 0)
			{
				glGetBufferSubData(p->target, p->offset, p->size, p->data);
				return 0;
			}
			const size_t need = (size_t)p->size;
			uint8_t *scratch = bounceFor(need);
			if (scratch == nullptr) /* cannot guard it; a readback direct beats none */
			{
				glGetBufferSubData(p->target, p->offset, p->size, p->data);
				return 0;
			}
			glGetBufferSubData(p->target, p->offset, p->size, scratch);
			bounceCheckCanary(need, "glGetBufferSubData");
			memcpy(p->data, scratch, need);
			return 0;
		}

		case CHIMERA_GL_OP_glReadPixels:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glReadPixels *>(a);
			size_t tight = 0, scratch = 0;
			PixelRows shape;
			uint8_t *buf = nullptr;
			if (pixelReadGuarded(p->format, p->type, p->width, p->height, p->pixels, &tight, &scratch, &shape)
				&& (buf = bounceFor(scratch)) != nullptr)
			{
				if (!guestCan(p->pixels, tight, true)) return glRefused(op, "glReadPixels", CE_GL_BAD_POINTER);
				glReadPixels(p->x, p->y, p->width, p->height, p->format, p->type, buf);
				bounceCheckCanary(scratch, "glReadPixels");
				copyRows(p->pixels, buf, shape);
				return 0;
			}
			/* unsized, or into a pack buffer: a client pointer is checked as far
			 * as its first byte */
			GLint packBuffer = 0;
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
			if (packBuffer == 0 && p->pixels != nullptr && !guestCan(p->pixels, 1, true))
				return glRefused(op, "glReadPixels", CE_GL_BAD_POINTER);
			glReadPixels(p->x, p->y, p->width, p->height, p->format, p->type, p->pixels);
			return 0;
		}

		/* Three more readbacks into the core's memory go through the same guard
		 * (engine analysis, 2026-10-18: it covered 3 of 12): a named buffer's
		 * bytes, of exactly `size`; and pixels and a texture's part, whose
		 * shape is the call's own and whose bufSize caps what GL may write. */
		case CHIMERA_GL_OP_glGetNamedBufferSubData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetNamedBufferSubData *>(a);
			GL_PROVIDED_OR_REFUSE(glGetNamedBufferSubData);
			if (p->size > 0 && !guestCan(p->data, (size_t)p->size, true)) return glRefused(op, "glGetNamedBufferSubData", CE_GL_BAD_POINTER);
			uint8_t *scratch = p->data != nullptr && p->size > 0 ? bounceFor((size_t)p->size) : nullptr;
			if (scratch == nullptr)
			{
				glGetNamedBufferSubData(p->buffer, p->offset, p->size, p->data);
				return 0;
			}
			glGetNamedBufferSubData(p->buffer, p->offset, p->size, scratch);
			bounceCheckCanary((size_t)p->size, "glGetNamedBufferSubData");
			memcpy(p->data, scratch, (size_t)p->size);
			return 0;
		}

		case CHIMERA_GL_OP_glReadnPixels:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glReadnPixels *>(a);
			GL_PROVIDED_OR_REFUSE(glReadnPixels);
			size_t tight = 0, scratch = 0;
			PixelRows shape;
			uint8_t *buf = nullptr;
			if (pixelReadGuarded(p->format, p->type, p->width, p->height, p->data, &tight, &scratch, &shape)
				&& p->bufSize >= 0 && tight <= (size_t)p->bufSize && (buf = bounceFor(scratch)) != nullptr)
			{
				if (!guestCan(p->data, tight, true)) return glRefused(op, "glReadnPixels", CE_GL_BAD_POINTER);
				glReadnPixels(p->x, p->y, p->width, p->height, p->format, p->type, (GLsizei)scratch, buf);
				bounceCheckCanary(scratch, "glReadnPixels");
				copyRows(p->data, buf, shape);
				return 0;
			}
			GLint packBuffer = 0;
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
			if (packBuffer == 0 && p->data != nullptr && p->bufSize > 0 && !guestCan(p->data, (size_t)p->bufSize, true))
				return glRefused(op, "glReadnPixels", CE_GL_BAD_POINTER);
			glReadnPixels(p->x, p->y, p->width, p->height, p->format, p->type, p->bufSize, p->data);
			return 0;
		}

		case CHIMERA_GL_OP_glGetTextureSubImage:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetTextureSubImage *>(a);
			GL_PROVIDED_OR_REFUSE(glGetTextureSubImage);
			size_t tight = 0, scratch = 0;
			PixelRows shape;
			uint8_t *buf = nullptr;
			if (p->depth == 1
				&& pixelReadGuarded(p->format, p->type, p->width, p->height, p->pixels, &tight, &scratch, &shape)
				&& p->bufSize >= 0 && tight <= (size_t)p->bufSize && (buf = bounceFor(scratch)) != nullptr)
			{
				if (!guestCan(p->pixels, tight, true)) return glRefused(op, "glGetTextureSubImage", CE_GL_BAD_POINTER);
				glGetTextureSubImage(p->texture, p->level, p->xoffset, p->yoffset, p->zoffset, p->width, p->height, p->depth,
					p->format, p->type, (GLsizei)scratch, buf);
				bounceCheckCanary(scratch, "glGetTextureSubImage");
				copyRows(p->pixels, buf, shape);
				return 0;
			}
			GLint packBuffer = 0;
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
			if (packBuffer == 0 && p->pixels != nullptr && p->bufSize > 0 && !guestCan(p->pixels, (size_t)p->bufSize, true))
				return glRefused(op, "glGetTextureSubImage", CE_GL_BAD_POINTER);
			glGetTextureSubImage(p->texture, p->level, p->xoffset, p->yoffset, p->zoffset, p->width, p->height, p->depth,
				p->format, p->type, p->bufSize, p->pixels);
			return 0;
		}

		case CHIMERA_GL_OP_glGetTexImage:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetTexImage *>(a);
			/* the level's width and height are the driver's to know; ask it, so
			 * the size matches what it will write. A cube-map or array target,
			 * or a level query that fails, falls through to a direct call. */
			GLint w = 0, h = 0;
			size_t tight = 0, scratch = 0;
			PixelRows shape;
			uint8_t *buf = nullptr;
			if (p->pixels != nullptr && (p->target == GL_TEXTURE_2D || p->target == GL_TEXTURE_RECTANGLE))
			{
				glGetTexLevelParameteriv(p->target, p->level, GL_TEXTURE_WIDTH, &w);
				glGetTexLevelParameteriv(p->target, p->level, GL_TEXTURE_HEIGHT, &h);
			}
			if (w > 0 && h > 0
				&& pixelReadGuarded(p->format, p->type, w, h, p->pixels, &tight, &scratch, &shape)
				&& (buf = bounceFor(scratch)) != nullptr)
			{
				if (!guestCan(p->pixels, tight, true)) return glRefused(op, "glGetTexImage", CE_GL_BAD_POINTER);
				glGetTexImage(p->target, p->level, p->format, p->type, buf);
				bounceCheckCanary(scratch, "glGetTexImage");
				copyRows(p->pixels, buf, shape);
				return 0;
			}
			GLint packBuffer = 0;
			glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
			if (packBuffer == 0 && p->pixels != nullptr && !guestCan(p->pixels, 1, true))
				return glRefused(op, "glGetTexImage", CE_GL_BAD_POINTER);
			glGetTexImage(p->target, p->level, p->format, p->type, p->pixels);
			return 0;
		}

		/* Sync objects: the guest deals in names, this side in driver pointers.
		 * A name that no longer stands for anything - every name a reloaded
		 * state carries - answers as a fence that has already passed, because
		 * it has: the work it waited on finished in a process that is gone. */
		case CHIMERA_GL_OP_glFenceSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glFenceSync *>(a);
			GL_PROVIDED_OR_REFUSE(glFenceSync);
			GLsync sync = glFenceSync(p->condition, p->flags);
			if (sync == nullptr) return 0;
			return syncName(sync);
		}

		case CHIMERA_GL_OP_glClientWaitSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glClientWaitSync *>(a);
			GL_PROVIDED_OR_REFUSE(glClientWaitSync);
			GLsync sync = syncFor(p->sync);
			if (sync == nullptr) return (uintptr_t)GL_ALREADY_SIGNALED;
			return (uintptr_t)glClientWaitSync(sync, p->flags, p->timeout);
		}

		case CHIMERA_GL_OP_glWaitSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glWaitSync *>(a);
			GL_PROVIDED_OR_REFUSE(glWaitSync);
			GLsync sync = syncFor(p->sync);
			if (sync != nullptr) glWaitSync(sync, p->flags, p->timeout);
			return 0;
		}

		case CHIMERA_GL_OP_glGetSynciv:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetSynciv *>(a);
			GL_PROVIDED_OR_REFUSE(glGetSynciv);
			GLsync sync = syncFor(p->sync);
			if (sync != nullptr)
			{
				glGetSynciv(sync, p->pname, p->count, p->length, p->values);
				return 0;
			}
			/* Answer for a fence that is over, in the guest's own buffers. */
			if (p->count > 0 && p->values != nullptr)
			{
				switch (p->pname)
				{
					case GL_OBJECT_TYPE:     p->values[0] = GL_SYNC_FENCE; break;
					case GL_SYNC_STATUS:     p->values[0] = GL_SIGNALED; break;
					case GL_SYNC_CONDITION:  p->values[0] = GL_SYNC_GPU_COMMANDS_COMPLETE; break;
					case GL_SYNC_FLAGS:      p->values[0] = 0; break;
					default:                 p->values[0] = 0; break;
				}
				if (p->length != nullptr) *p->length = 1;
			}
			else if (p->length != nullptr) *p->length = 0;
			return 0;
		}

		case CHIMERA_GL_OP_glDeleteSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glDeleteSync *>(a);
			GL_PROVIDED_OR_REFUSE(glDeleteSync);
			GLsync sync = syncFor(p->sync);
			if (sync != nullptr) glDeleteSync(sync);
			syncForget(p->sync);
			return 0;
		}

		case CHIMERA_GL_OP_glIsSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glIsSync *>(a);
			return (uintptr_t)(syncFor(p->sync) != nullptr ? GL_TRUE : GL_FALSE);
		}

		/* What the buffer pool must know of, noted on the way to the generated
		 * case below (see BoundRecord and bufferPool()). */
		case CHIMERA_GL_OP_glBindBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glBindBuffer *>(a);
			recordBind(p->target, p->buffer);
			break;
		}
		case CHIMERA_GL_OP_glBindBufferBase:
		{
			/* (the generic binding point is set too) */
			auto *p = reinterpret_cast<ChimeraGlArgs_glBindBufferBase *>(a);
			recordBind(p->target, p->buffer);
			break;
		}
		case CHIMERA_GL_OP_glBindBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glBindBufferRange *>(a);
			recordBind(p->target, p->buffer);
			break;
		}
		/* the storage and map calls the cases above do not cover, by their
		 * other names: a buffer given storage or mapped through them was pooled */
		case CHIMERA_GL_OP_glBufferStorageEXT:
			if (glPool()) bufferPool().noteImmutable(boundBuffer(reinterpret_cast<ChimeraGlArgs_glBufferStorageEXT *>(a)->target), true);
			break;
		case CHIMERA_GL_OP_glNamedBufferStorageEXT:
			bufferPool().noteImmutable(reinterpret_cast<ChimeraGlArgs_glNamedBufferStorageEXT *>(a)->buffer, true);
			break;
		case CHIMERA_GL_OP_glMapNamedBufferRangeEXT:
			bufferPool().noteMapped(reinterpret_cast<ChimeraGlArgs_glMapNamedBufferRangeEXT *>(a)->buffer, true);
			break;

		default:
			break;
	}

	switch (op)
	{
		case GL_OP_BUFFER_SIZE:
		{
			/* a buffer's size, for a shadow of a whole mapping: a = the target, or
			 * the buffer's name when b is 1 */
			GLint64 size = 0;
			if (b != 0 && glad_glGetNamedBufferParameteri64v != nullptr)
				glGetNamedBufferParameteri64v((GLuint)a, GL_BUFFER_SIZE, &size);
			else if (b == 0 && glad_glGetBufferParameteri64v != nullptr)
				glGetBufferParameteri64v((GLenum)a, GL_BUFFER_SIZE, &size);
			return size > 0 ? (uintptr_t)size : 0;
		}
		case GL_OP_NAME_MARKS:
			/* where the guest keeps its marks; see g_nameMarks */
			g_nameMarks = a != 0 && b >= sizeof(ChimeraGlNameMarks) ? reinterpret_cast<const ChimeraGlNameMarks *>(a) : nullptr;
			return g_nameMarks != nullptr ? 1 : 0;

		case GL_OP_LIST_LENGTH:
			/* How many entry points this host knows. The guest compares it with
			 * what it was built against and declines us if we are behind; the
			 * list being append-only is what makes that check sufficient. */
			return CHIMERA_GL_OP_LIST_LENGTH;

		case GL_OP_CONTEXT_ID:
			/* Which context these calls are landing on. A renderer keeps its
			 * GL objects by the names this context handed out, and those names
			 * live in guest memory - so they survive a savestate into a session
			 * where they name nothing, and every call using one is refused
			 * without the guest ever hearing about it. Storing this number
			 * beside them is how a renderer can tell, and rebuild. It moves per
			 * session and on every state load: a load in the same session
			 * brings the names back but not what the objects held
			 * (ce_gl_state_loaded). */
			return g_context_id;

		case GL_OP_VERSION:
		{
			/* Never hand a host pointer back: the guest cannot read our memory
			 * and the sandbox is right to stop it. Copied into its buffer. */
			auto *args = reinterpret_cast<GlVersionArgs *>(a);
			if (!args->out || args->size == 0) return 0;
			snprintf(reinterpret_cast<char *>(args->out), args->size, "%s", g_description);
			return 1;
		}

		/* the generated cases: one per GL entry point in the master list
		 * (GL_OP_NAME_MARKS is answered above, so its generated case is left out) */
#define CHIMERA_GL_HOST_NAME_MARKS 1
#include "gl-bridge-host.inc"

		default:
			/* A core asking for something this host has never heard of is a
			 * mismatch the handshake should have caught. Say so rather than
			 * returning a plausible zero. */
			fprintf(stderr, "chimera gl: opcode %llu has no case\n", (unsigned long long)op);
			return 0;
	}
}

/* Whether the CALLER wants hardware acceleration. Asked by the session before
 * it offers a core a bridge, and set by the frontend from the project. It is a
 * process-wide switch because the context is: one GL context, made once, shared
 * by whatever session is open, because there is only ever one.
 *
 * Separate from ce_gl_available on purpose. Wanting it and having it are
 * different facts, and a person who asked for a GPU and did not get one is owed
 * a different answer from one who never asked. */
static int32_t g_requested;

extern "C" void ce_gl_request(int32_t want) { g_requested = want ? 1 : 0; }
extern "C" int32_t ce_gl_requested(void) { return g_requested; }

extern "C" int32_t ce_gl_available(void)
{
	return g_ready ? 1 : 0;
}

extern "C" const char *ce_gl_description(void)
{
	return g_ready ? g_description : "";
}

extern "C" const char *ce_gl_renderer(void)
{
	return g_ready ? g_renderer : "";
}

extern "C" const char *ce_gl_version(void)
{
	return g_ready ? g_versionText : "";
}

extern "C" void *ce_gl_flight_recorder(uint32_t *bytes)
{
	if (bytes) *bytes = (uint32_t)sizeof g_flight;
	return &g_flight;
}

/* The Vulkan bridge's commands go into the same ring (chimera issues 276 and
 * 278: two Vulkan drivers crashed with nothing to say what they were given).
 * miniBox's host calls this before the driver sees the command. */
extern "C" void ce_vk_flight_note(unsigned op)
{
	flightNote(kFlightVulkan | (op & 0x0FFFFFFFu), 0);
}

/* Sessions that took the bridge and have not ended (ce_gl_session_end). */
static int g_holders;

/* Mint a fresh id. It need only DIFFER from any other - the guest compares it
 * for equality and nothing else - so it is taken from where this process sits
 * in memory (the loader decides that anew every run), the clock, and a
 * monotone count. See ce_gl_start for why it changes per session, not just
 * per context. */
static void mint_context_id()
{
	static uint64_t made;

	/* Where this process sits in memory is NOT per-process entropy on Windows.
	 * A DLL's base is randomised once per BOOT and shared by every process that
	 * loads it, so &g_context_id inside libchimera.dll is the same address in
	 * every Chimera on the machine until it reboots. Measured 2026-09-14: two
	 * separate chimera-run processes minted 0x7ff746ec6fc1f001 and
	 * 0x7ff746ec6fc1f101 - identical but for one tick of the clock term.
	 *
	 * That left `time()`, at one-second granularity, as the only thing telling
	 * two processes apart (`made` is 1 in both, being a fresh static in each).
	 * Two processes minting within the same second therefore got the SAME id,
	 * and a guest comparing it concludes its GL objects are still good - keeps
	 * the names a dead process's context handed out, and gives them to the
	 * driver. The whole safety of carrying GPU state across a restart rests on
	 * this number differing, so it must not rest on a one-second clock.
	 *
	 * The pid and a high-resolution counter are what actually differ per
	 * process; both are cheap and neither can be constant across two of them. */
	uint64_t per_process;
#ifdef _WIN32
	LARGE_INTEGER qpc;
	QueryPerformanceCounter(&qpc);
	per_process = (static_cast<uint64_t>(GetCurrentProcessId()) << 32)
		^ static_cast<uint64_t>(qpc.QuadPart);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	per_process = (static_cast<uint64_t>(getpid()) << 32)
		^ (static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + static_cast<uint64_t>(ts.tv_nsec));
#endif

	g_context_id = (static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&g_context_id)) << 16)
		^ (static_cast<uint64_t>(time(nullptr)) << 8)
		^ per_process
		^ (++made);
	if (g_context_id == 0) g_context_id = 1; /* 0 means "cannot tell" */
}

extern "C" int32_t ce_gl_start(char *error_out, int32_t error_len)
{
	if (g_ready)
	{
		/* The context is made once per process and shared by every session,
		 * because there is only ever one machine. But the GL objects a
		 * renderer holds are not the context's, they are that SESSION's: made
		 * during its boot, and meaningless to the next session even though the
		 * driver's context outlived them. So each session that takes the bridge
		 * gets a fresh id, and a state carried across the session boundary
		 * (a greenzone reload after close-and-reopen, all in this one process)
		 * shows the guest an id that has moved, which is its cue to rebuild.
		 * Without this the id stayed put across sessions, the guest kept the
		 * dead session's object names, and the first draw bound a gone
		 * framebuffer and aborted - a crash on reopening a saved project
		 * (issue #43). A fresh process already gets a new id from the block
		 * below; this is the same signal for the in-process case. */
		mint_context_id();
		flightNote(kFlightSession, 0);
		g_holders++;
		return 1;
	}

	/* Making a context makes it current, and this runs while the frontend is
	 * on screen with a context of its own. Remember what it had, hand it back
	 * on every path out of here. */
	save_current();

	/* noted before the driver is asked anything, so that a death inside
	 * create_context or gladLoadGL is not blamed on the call before */
	flightNote(kFlightSession, 0);
	char err[192] = "";
	if (!create_context(err, (int)sizeof err))
	{
		destroy_context();
		return_current();
		if (error_out && error_len > 0) snprintf(error_out, error_len, "%s", err);
		return 0;
	}

	g_version = gladLoadGL(loader);
	if (g_version == 0)
	{
		destroy_context();
		return_current();
		if (error_out && error_len > 0)
			snprintf(error_out, error_len, "%s", chimera::lang::tr("gl.no_entry_points").c_str());
		return 0;
	}

	const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	/* which profile the driver gave: core, or the old kind (a Windows driver
	 * without the call that asks for core) */
	GLint profile = 0;
	if (GLAD_VERSION_MAJOR(g_version) > 3 || (GLAD_VERSION_MAJOR(g_version) == 3 && GLAD_VERSION_MINOR(g_version) >= 2))
		glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
	snprintf(g_description, sizeof g_description, "%s", chimera::lang::tr((profile & GL_CONTEXT_CORE_PROFILE_BIT) != 0 ? "gl.description_core" : "gl.description_compatibility",
		{version ? version : "?", renderer ? renderer : "?"}).c_str());
	snprintf(g_renderer, sizeof g_renderer, "%s", renderer ? renderer : "");
	snprintf(g_versionText, sizeof g_versionText, "%s", version ? version : "");

	/* Every renderer here is written for OpenGL 3.3 or later, and saying a
	 * driver offers less here is kinder than a null call in the middle of a
	 * shader compile. Linux asks EGL for 3.3 and gets nothing less; a Windows
	 * driver of the old kind could answer 3.0 to 3.2 (owner, 2026-10-19: 3.3
	 * on both). */
	if (GLAD_VERSION_MAJOR(g_version) < 3 || (GLAD_VERSION_MAJOR(g_version) == 3 && GLAD_VERSION_MINOR(g_version) < 3))
	{
		destroy_context();
		return_current();
		if (error_out && error_len > 0)
			snprintf(error_out, error_len, "%s", chimera::lang::tr("gl.too_old", {std::to_string(GLAD_VERSION_MAJOR(g_version)) + "." + std::to_string(GLAD_VERSION_MINOR(g_version))}).c_str());
		return 0;
	}

	g_ready = true;
	/* An identity the guest stores beside its GL objects and compares. Minted
	 * per session, not merely per context (see the g_ready path above). */
	mint_context_id();
	g_holders++;
	return_current();
	return 1;
}

/* What names objects of the context being ended: none of it means anything in
 * the next one. A sync's slot keeps its generation, moved on, so an old name
 * reads as a fence that has passed (see SyncSlot). */
static void forgetContext()
{
	bufferPool().reset();
	for (BoundRecord &r : g_bound) r.name = 0;
	syncFree().clear();
	for (size_t i = 0; i < g_syncs.size(); i++)
	{
		SyncSlot &slot = g_syncs[i];
		if (slot.sync != nullptr) slot.gen++;
		slot.sync = nullptr;
		syncFree().push_back((uint32_t)i);
	}
	g_syncReap = false;
	/* one large readback kept the scratch at its size for the process */
	free(g_bounce);
	g_bounce = nullptr;
	g_bounceCap = 0;
	g_tsQuery[0] = g_tsQuery[1] = 0;
	g_tsPending = false;
	glShadows().clear();   /* the mappings went with the context */
	g_shadowReap = false;
	for (int k = 0; k < kAuditKinds; k++) auditLives()[k].clear();
}

/* A load that makes the core rebuild its GL objects (ce_gl_state_loaded) also
 * ends the context they were in, and the core rebuilds in a fresh one (owner,
 * 2026-10-19; the Vulkan bridge does the same). The objects of the frames
 * after the restored one are orphans - nothing in the restored machine names
 * them - and stayed in the driver until the session ended: RPCS3 left about
 * 55 objects and 600 MB in it with each rewind. Every name the core held is
 * taken again in the fresh context (reserveNames). Not while a second session
 * holds the context: its objects are in it. CHIMERA_GL_SAME_CONTEXT_ON_LOAD
 * keeps the old behaviour (the same context, the id moved), for A/B. */
static uint64_t g_renewed;
static void renewContext()
{
	static const bool off = getenv("CHIMERA_GL_SAME_CONTEXT_ON_LOAD") != nullptr;
	if (off || !g_ready || g_holders != 1) return;
	ce_gl_release();
	save_current();
	if (!replace_context())
	{
		return_current();
		static bool said;
		if (!said)
		{
			said = true;
			fprintf(stderr, "chimera gl: no fresh context for a load; the old one is kept\n");
		}
		return;
	}
	gladLoadGL(loader);   /* the entry points, for the context that is current now */
	forgetContext();
	glNames().keepAll();
	const size_t kept = reserveNames();
	flightNote(kFlightRenewed, (uint32_t)kept);
	g_renewed++;
	return_current();
}

extern "C" uint64_t ce_gl_contexts_renewed(void) { return g_renewed; }

/* A session that took the bridge has ended. When it is the last, its context
 * goes, and with it every object its renderer made: no core tears its
 * renderer down when its sandbox is destroyed, and the context used to live
 * for the whole process, so every texture, buffer, framebuffer, program and
 * sync a closed project made stayed in the driver - another project's worth
 * with each one opened. The next session that asks makes a new context. */
extern "C" void ce_gl_session_end(void)
{
	if (!g_ready || g_holders == 0) return;
	if (--g_holders != 0) return;
	ce_gl_release();
	flightNote(kFlightStopped, 0);
	end_context();
	forgetContext();
	glNames().clear();
	g_nameMarks = nullptr;   /* the guest's memory goes with its session */
	g_ready = false;
	g_context_id = 0;
	g_description[0] = 0;
	g_renderer[0] = 0;
	g_versionText[0] = 0;
}

extern "C" void ce_gl_stop(void)
{
	if (!g_ready) return;
	ce_gl_release();
	flightNote(kFlightStopped, 0);
	destroy_context();
	glNames().clear();
	g_ready = false;
	g_context_id = 0;
	g_description[0] = 0;
	g_renderer[0] = 0;
	g_versionText[0] = 0;
}

#else /* !CE_GL_BRIDGE - a build without a GPU bridge answers honestly */

static int32_t g_requested;
extern "C" void ce_gl_request(int32_t want) { g_requested = want ? 1 : 0; }
/* A build with no bridge can still be ASKED; it simply never delivers, and the
 * session says so once rather than the caller guessing. */
extern "C" int32_t ce_gl_requested(void) { return g_requested; }
extern "C" int32_t ce_gl_available(void) { return 0; }
extern "C" const char *ce_gl_description(void) { return ""; }
extern "C" const char *ce_gl_renderer(void) { return ""; }
extern "C" const char *ce_gl_version(void) { return ""; }
extern "C" void *ce_gl_flight_recorder(uint32_t *bytes) { if (bytes) *bytes = 0; return nullptr; }
extern "C" void ce_vk_flight_note(unsigned) { }
extern "C" void ce_gl_release(void) { }
extern "C" void ce_gl_audit_frame(int64_t) { }
extern "C" void ce_gl_guest_check(int (*)(void *, uintptr_t, uintptr_t, int), void *) { }
extern "C" uint64_t ce_gl_readback_overruns(void) { return 0; }
extern "C" void ce_gl_state_loaded(int64_t) { }
extern "C" uint64_t ce_gl_contexts_renewed(void) { return 0; }
extern "C" uintptr_t ce_gl_dispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) { return 0; }
extern "C" int32_t ce_gl_start(char *error_out, int32_t error_len)
{
	if (error_out && error_len > 0)
		snprintf(error_out, error_len, "%s", chimera::lang::tr("gl.no_bridge").c_str());
	return 0;
}
extern "C" void ce_gl_stop(void) {}
extern "C" void ce_gl_session_end(void) {}

#endif
