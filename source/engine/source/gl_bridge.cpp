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

int g_version;
bool g_ready;
/* Which context is current - see GL_OP_CONTEXT_ID. Zero until one is made. */
uint64_t g_context_id;
char g_description[256];

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
		snprintf(err, errlen, "could not make a window to take a pixel format from");
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
		snprintf(err, errlen, "no pixel format this driver will draw into");
		return false;
	}

	s_context = wglCreateContext(s_dc);
	if (s_context == nullptr || !wglMakeCurrent(s_dc, s_context))
	{
		snprintf(err, errlen, "the driver would not make a context current");
		return false;
	}

	s_opengl32 = LoadLibraryA("opengl32.dll");
	return s_opengl32 != nullptr;
}

void destroy_context()
{
	if (s_context) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext(s_context); s_context = nullptr; }
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

void bind_ours() { wglMakeCurrent(s_dc, s_context); }

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
		snprintf(err, errlen, "no EGL display");
		return false;
	}

	if (!eglInitialize(s_display, nullptr, nullptr))
	{
		snprintf(err, errlen, "EGL would not initialise");
		return false;
	}

	if (!eglBindAPI(EGL_OPENGL_API))
	{
		snprintf(err, errlen, "this EGL has no desktop OpenGL");
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
		snprintf(err, errlen, "no EGL config offers desktop OpenGL");
		return false;
	}

	static const EGLint contextAttribs[] = {
		EGL_CONTEXT_MAJOR_VERSION, 3,
		EGL_CONTEXT_MINOR_VERSION, 3,
		EGL_NONE
	};
	s_context = eglCreateContext(s_display, config, EGL_NO_CONTEXT, contextAttribs);
	if (s_context == EGL_NO_CONTEXT)
	{
		snprintf(err, errlen, "the driver would not make an OpenGL 3.3 context");
		return false;
	}

	if (!eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_context))
	{
		snprintf(err, errlen, "the driver would not make the context current");
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

void bind_ours()
{
	eglMakeCurrent(s_display, EGL_NO_SURFACE, EGL_NO_SURFACE, s_context);
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

/* Whether a same-session load moves the context id. Global and set before a
 * session opens, the way ce_gl_request and ce_cache_dir are; 1 unless a core
 * declares otherwise, so nothing changes for a core that says nothing. */
static bool g_rebuildOnStateLoad = true;

extern "C" void ce_gl_rebuild_on_state_load(int32_t on)
{
	g_rebuildOnStateLoad = on != 0;
}

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
	if (!keepObjects && g_rebuildOnStateLoad) mint_context_id();
	if (!glAudit()) return;
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
static uint8_t *bounceFor(size_t need)
{
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
static bool pixelBytes(GLenum format, GLenum type, GLsizei width, GLsizei height,
                       size_t *tight, size_t *scratch)
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

	const size_t effectiveWidth = rowLength > 0 ? (size_t)rowLength : (size_t)width;
	const size_t rowRaw = (size_t)pixel * effectiveWidth;
	const size_t rowStride = (rowRaw + (size_t)alignment - 1) & ~((size_t)alignment - 1);
	*tight = rowStride * ((size_t)height - 1) + (size_t)pixel * (size_t)width;
	*scratch = rowStride * (size_t)height; /* generous: a padded last row lands here */
	return true;
}

/* Whether a readback with these args writes into client memory and can be sized
 * (so it goes through the bounce buffer). False when a pixel-pack buffer is
 * bound - then `pixels` is an offset into that buffer, not a client pointer -
 * or the format cannot be sized. Fills the byte counts when true. */
static bool pixelReadGuarded(GLenum format, GLenum type, GLsizei width, GLsizei height,
                             const void *pixels, size_t *tight, size_t *scratch)
{
	if (pixels == nullptr) return false;
	GLint packBuffer = 0;
	glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &packBuffer);
	if (packBuffer != 0) return false;
	return pixelBytes(format, type, width, height, tight, scratch);
}

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

/* The name for a driver sync: generation in the high half, slot+1 in the low,
 * so a name is never zero and never collides with a name from a past life. */
static uintptr_t syncName(GLsync sync)
{
	size_t at = g_syncs.size();
	for (size_t i = 0; i < g_syncs.size(); i++)
	{
		if (g_syncs[i].sync == nullptr) { at = i; break; }
	}
	if (at == g_syncs.size()) g_syncs.push_back(SyncSlot{ nullptr, 0 });
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
	if (s.gen == ce_gl_sync_name_generation(v)) s.sync = nullptr;
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

extern "C" uintptr_t BRIDGE_ABI ce_gl_dispatch(uintptr_t op, uintptr_t a, uintptr_t b,
                                               uintptr_t c, uintptr_t d, uintptr_t e)
{
	(void)d; (void)e;
	g_calls++;
	flightNote((uint32_t)op, 0);

	/* The first GL call of a frame takes the context; ce_gl_release gives it
	 * back when the frame is over. Two calls per frame, not two per GL call. */
	if (!g_borrowed && g_ready)
	{
		save_current();
		bind_ours();
		g_borrowed = true;
		flightNote(kFlightBorrowed, 0);
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
	 * pool can serve them, which is the whole point. */
	if (glPool() && op == CHIMERA_GL_OP_glGenBuffers)
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
		return 0;
	}
	if (glPool() && op == CHIMERA_GL_OP_glDeleteBuffers)
	{
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
			 * the way the guest meant, and a lopsided guest cannot leak */
			if (what == CeGlBufferPool::Delete::Real) glDeleteBuffers(1, &name);
		}
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

	if (glProfile())
	{
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
			}
			/* ce_gl_release marks the frame boundary, and a host that is not
			 * drawing never calls it - so a run with rendering off would
			 * otherwise collect the whole profile and print none of it. */
			else atexit(profileDump);
		}
		const auto at = std::chrono::steady_clock::now();
		const uintptr_t rv = ce_gl_dispatch_one(op, a, b, c, d, e);
		const uint64_t took = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - at).count();
		g_driverNs += took;
		if (g_opCalls != nullptr && op < (uintptr_t)kProfileOps)
		{
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
		return rv;
	}
	if (glTime())
	{
		const auto at = std::chrono::steady_clock::now();
		const uintptr_t rv = ce_gl_dispatch_one(op, a, b, c, d, e);
		g_driverNs += (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now() - at).count();
		return rv;
	}
	return ce_gl_dispatch_one(op, a, b, c, d, e);
}

static uintptr_t ce_gl_dispatch_one(uintptr_t op, uintptr_t a, uintptr_t b,
                                    uintptr_t c, uintptr_t d, uintptr_t e)
{
	(void)d; (void)e;

	/* Readbacks that would have the driver write into guest memory go through
	 * the bounce buffer above rather than the generated case below. */
	switch (op)
	{
		/* Maps and unmaps, noted for the buffer pool (see bufferPool()): it must
		 * never recycle a buffer that is still mapped. */
		case CHIMERA_GL_OP_glMapBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapBufferRange *>(a);
			void *mapped = glMapBufferRange(p->target, p->offset, p->length, p->access);
			if (mapped != nullptr && glPool()) bufferPool().noteMapped(boundBuffer(p->target), true);
			if (poolTrace())
				fprintf(stderr, "[pool] mapRange target=%#x bound=%u access=%#x -> %p\n", (unsigned)p->target,
					(unsigned)boundBuffer(p->target), (unsigned)p->access, mapped);
			return (uintptr_t)mapped;
		}
		case CHIMERA_GL_OP_glMapBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapBuffer *>(a);
			void *mapped = glMapBuffer(p->target, p->access);
			if (mapped != nullptr && glPool()) bufferPool().noteMapped(boundBuffer(p->target), true);
			return (uintptr_t)mapped;
		}
		case CHIMERA_GL_OP_glMapNamedBufferRange:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glMapNamedBufferRange *>(a);
			void *mapped = glMapNamedBufferRange(p->buffer, p->offset, p->length, p->access);
			if (mapped != nullptr) bufferPool().noteMapped(p->buffer, true);
			return (uintptr_t)mapped;
		}
		case CHIMERA_GL_OP_glUnmapBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glUnmapBuffer *>(a);
			const GLuint name = glPool() ? boundBuffer(p->target) : 0;
			const GLboolean ok = glUnmapBuffer(p->target);
			bufferPool().noteMapped(name, false);
			if (poolTrace())
				fprintf(stderr, "[pool] unmap target=%#x bound=%u -> %d\n", (unsigned)p->target, (unsigned)name, (int)ok);
			return (uintptr_t)ok;
		}
		case CHIMERA_GL_OP_glBufferStorage:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glBufferStorage *>(a);
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
			glNamedBufferStorage(p->buffer, p->size, p->data, p->flags);
			bufferPool().noteImmutable(p->buffer, true);
			return 0;
		}
		case CHIMERA_GL_OP_glUnmapNamedBuffer:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glUnmapNamedBuffer *>(a);
			const GLboolean ok = glUnmapNamedBuffer(p->buffer);
			bufferPool().noteMapped(p->buffer, false);
			return (uintptr_t)ok;
		}

		case CHIMERA_GL_OP_glGetBufferSubData:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetBufferSubData *>(a);
			/* size is exactly the bytes written - no computation to get wrong */
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
			uint8_t *buf = nullptr;
			if (pixelReadGuarded(p->format, p->type, p->width, p->height, p->pixels, &tight, &scratch)
				&& (buf = bounceFor(scratch)) != nullptr)
			{
				glReadPixels(p->x, p->y, p->width, p->height, p->format, p->type, buf);
				bounceCheckCanary(scratch, "glReadPixels");
				memcpy(p->pixels, buf, tight);
				return 0;
			}
			glReadPixels(p->x, p->y, p->width, p->height, p->format, p->type, p->pixels);
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
			uint8_t *buf = nullptr;
			if (p->pixels != nullptr && (p->target == GL_TEXTURE_2D || p->target == GL_TEXTURE_RECTANGLE))
			{
				glGetTexLevelParameteriv(p->target, p->level, GL_TEXTURE_WIDTH, &w);
				glGetTexLevelParameteriv(p->target, p->level, GL_TEXTURE_HEIGHT, &h);
			}
			if (w > 0 && h > 0
				&& pixelReadGuarded(p->format, p->type, w, h, p->pixels, &tight, &scratch)
				&& (buf = bounceFor(scratch)) != nullptr)
			{
				glGetTexImage(p->target, p->level, p->format, p->type, buf);
				bounceCheckCanary(scratch, "glGetTexImage");
				memcpy(p->pixels, buf, tight);
				return 0;
			}
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
			GLsync sync = glFenceSync(p->condition, p->flags);
			if (sync == nullptr) return 0;
			return syncName(sync);
		}

		case CHIMERA_GL_OP_glClientWaitSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glClientWaitSync *>(a);
			GLsync sync = syncFor(p->sync);
			if (sync == nullptr) return (uintptr_t)GL_ALREADY_SIGNALED;
			return (uintptr_t)glClientWaitSync(sync, p->flags, p->timeout);
		}

		case CHIMERA_GL_OP_glWaitSync:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glWaitSync *>(a);
			GLsync sync = syncFor(p->sync);
			if (sync != nullptr) glWaitSync(sync, p->flags, p->timeout);
			return 0;
		}

		case CHIMERA_GL_OP_glGetSynciv:
		{
			auto *p = reinterpret_cast<ChimeraGlArgs_glGetSynciv *>(a);
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

		default:
			break;
	}

	switch (op)
	{
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

		/* the generated cases: one per GL entry point in the master list */
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

extern "C" void *ce_gl_flight_recorder(uint32_t *bytes)
{
	if (bytes) *bytes = (uint32_t)sizeof g_flight;
	return &g_flight;
}

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
		return 1;
	}

	/* Making a context makes it current, and this runs while the frontend is
	 * on screen with a context of its own. Remember what it had, hand it back
	 * on every path out of here. */
	save_current();

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
			snprintf(error_out, error_len, "the driver's entry points would not load");
		return 0;
	}

	const char *renderer = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
	const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
	snprintf(g_description, sizeof g_description, "%s on %s",
		version ? version : "?", renderer ? renderer : "?");

	/* A driver offering only the old fixed-function pipeline cannot run any of
	 * these renderers, and saying so here is kinder than a null call in the
	 * middle of a shader compile. */
	if (GLAD_VERSION_MAJOR(g_version) < 3)
	{
		destroy_context();
		return_current();
		if (error_out && error_len > 0)
			snprintf(error_out, error_len, "the driver offers OpenGL %d.%d, and this needs 3.3",
				GLAD_VERSION_MAJOR(g_version), GLAD_VERSION_MINOR(g_version));
		return 0;
	}

	g_ready = true;
	/* An identity the guest stores beside its GL objects and compares. Minted
	 * per session, not merely per context (see the g_ready path above). */
	mint_context_id();
	flightNote(kFlightSession, 0);
	return_current();
	return 1;
}

extern "C" void ce_gl_stop(void)
{
	if (!g_ready) return;
	ce_gl_release();
	flightNote(kFlightStopped, 0);
	destroy_context();
	g_ready = false;
	g_context_id = 0;
	g_description[0] = 0;
}

#else /* !CE_GL_BRIDGE - a build without a GPU bridge answers honestly */

static int32_t g_requested;
extern "C" void ce_gl_request(int32_t want) { g_requested = want ? 1 : 0; }
/* A build with no bridge can still be ASKED; it simply never delivers, and the
 * session says so once rather than the caller guessing. */
extern "C" int32_t ce_gl_requested(void) { return g_requested; }
extern "C" int32_t ce_gl_available(void) { return 0; }
extern "C" const char *ce_gl_description(void) { return ""; }
extern "C" void *ce_gl_flight_recorder(uint32_t *bytes) { if (bytes) *bytes = 0; return nullptr; }
extern "C" void ce_gl_release(void) { }
extern "C" void ce_gl_audit_frame(int64_t) { }
extern "C" void ce_gl_state_loaded(int64_t) { }
extern "C" uintptr_t ce_gl_dispatch(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t) { return 0; }
extern "C" int32_t ce_gl_start(char *error_out, int32_t error_len)
{
	if (error_out && error_len > 0)
		snprintf(error_out, error_len, "this build has no GPU bridge");
	return 0;
}
extern "C" void ce_gl_stop(void) {}

#endif
