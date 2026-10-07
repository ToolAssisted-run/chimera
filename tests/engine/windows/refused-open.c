/* refused-open - does a refused session leave the engine's heap alone?
 *
 * chimera#123. A licensed .pkg with no .rap crashed the frontend while it
 * compiled. The crash was in ntdll's heap, minutes of log after the graceful
 * refusal sentence, with the sandbox already gone - and the cause was the
 * SENTENCE: `thread_local std::string` in a DLL built with mingw-w64 has its
 * destructor run twice on the thread that ends the process, so the string that
 * carries the engine's last error was freed twice and the CRT heap was left
 * corrupt. An empty string has no buffer and survives that; only a path that
 * reports a long error pays, which is why refusing a game crashed and running
 * one did not.
 *
 * This program is the detector, and it is deterministic where the crash is not
 * (the crash needs the freed block to have been recycled first, which on a
 * large session happens about three runs in four and on a small one never).
 * It asks the question one step earlier: after the thread that was refused has
 * ENDED, is the string the engine handed it still an allocated block?
 *
 *   1. a worker thread asks the engine to open something that cannot be
 *      opened, and keeps the `const char *` the engine returns;
 *   2. the thread ends - which is where a mingw DLL runs its thread_local
 *      destructors for the first time;
 *   3. the main thread asks the C runtime's heap whether that pointer is still
 *      a live block.
 *
 * Live is right: the engine's per-thread strings are never destroyed on
 * purpose (source/engine/source/thread_string.hpp). Freed is the bug, and the
 * heap is already corrupt at that point whether or not anything trips over it.
 *
 * Both answers are checked against a control - a block this program allocates
 * and frees itself - so a HeapValidate that cannot tell the difference is
 * reported as such rather than passing.
 *
 * With a core package and a rom it does not accept, it also drives the real
 * shape of the report: a machine REFUSED AT Init, torn down by the engine,
 * with the process then exiting normally.
 *
 *   refused-open.exe [<core package> <rom that it will refuse>]
 *
 * Windows only, because the defect is: Linux runs the destructor once, and
 * chimera-run links the engine into an exe, where there is no DLL detach.
 */
#include <windows.h>
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void *(*ce_session_open_fn)(
	const char *package_path, const unsigned char *rom, unsigned long long rom_len,
	const char *rom_path, const char *settings_overrides_json,
	const char *const *firmware_ids, const unsigned char *const *firmware_data,
	const unsigned long long *firmware_lens, int firmware_count,
	const char *const *extra_names, const unsigned char *const *extra_data,
	const unsigned long long *extra_lens, const char *const *extra_paths, int extra_count,
	const char **error_out);

static ce_session_open_fn ce_session_open;

typedef const char *(*ce_suggest_settings_fn)(
	const char *package_path, const unsigned char *rom, unsigned long long rom_len,
	const char *rom_path, const char *settings_overrides_json,
	const char *const *firmware_ids, const unsigned char *const *firmware_data,
	const unsigned long long *firmware_lens, int firmware_count,
	const char *const *extra_names, const unsigned char *const *extra_data,
	const unsigned long long *extra_lens, const char *const *extra_paths, int extra_count,
	unsigned long long *len_out, const char **error_out);

static ce_suggest_settings_fn ce_suggest_settings;
static const char *g_answer;      /* what ce_suggest_settings returned */
static char g_answer_text[512];

static const char *g_package;    /* what the worker opens */
static const char *g_rom_path;   /* NULL for the no-such-package case */
static const char *g_error;      /* what the engine answered, kept past the thread */
static char g_error_text[512];   /* ...and a copy of the words, for the report */
static void *g_session;

static DWORD WINAPI refused_open(LPVOID unused)
{
	const char *error = NULL;
	(void)unused;
	g_session = ce_session_open(g_package, NULL, 0, g_rom_path, NULL,
	                            NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, &error);
	g_error = error;
	if (error != NULL) snprintf(g_error_text, sizeof g_error_text, "%s", error);
	return 0;
}

/* The suggestion call keeps its answer in a per-thread string too
 * (ce_suggest_settings): the same rule, the same way to break it. */
static DWORD WINAPI suggest(LPVOID unused)
{
	const char *error = NULL;
	unsigned long long len = 0;
	(void)unused;
	g_answer = ce_suggest_settings(g_package, NULL, 0, g_rom_path, NULL,
	                               NULL, NULL, NULL, 0, NULL, NULL, NULL, NULL, 0, &len, &error);
	g_error = error;
	if (error != NULL) snprintf(g_error_text, sizeof g_error_text, "%s", error);
	if (g_answer != NULL) snprintf(g_answer_text, sizeof g_answer_text, "%.*s", (int)len, g_answer);
	return 0;
}

static int suggest_on_its_own_thread(const char *package, const char *rom_path)
{
	HANDLE t;
	g_package = package;
	g_rom_path = rom_path;
	g_error = NULL;
	g_answer = NULL;
	g_error_text[0] = g_answer_text[0] = '\0';
	t = CreateThread(NULL, 0, suggest, NULL, 0, NULL);
	if (t == NULL) { fprintf(stderr, "FAIL could not start a thread\n"); return 0; }
	WaitForSingleObject(t, INFINITE);
	CloseHandle(t);
	return 1;
}

/* Runs refused_open on a thread of its own and waits for that thread to END,
 * which is the moment this test is about. */
static int open_on_its_own_thread(const char *package, const char *rom_path)
{
	HANDLE t;
	g_package = package;
	g_rom_path = rom_path;
	g_error = NULL;
	g_session = NULL;
	g_error_text[0] = '\0';
	t = CreateThread(NULL, 0, refused_open, NULL, 0, NULL);
	if (t == NULL) { fprintf(stderr, "FAIL could not start a thread\n"); return 0; }
	WaitForSingleObject(t, INFINITE);
	CloseHandle(t);
	return 1;
}

int main(int argc, char **argv)
{
	HMODULE engine;
	HANDLE crt;
	void *control;
	int control_live, control_freed, still_live, failures = 0;

	engine = LoadLibraryA("libchimera.dll");
	if (engine == NULL)
	{
		fprintf(stderr, "FAIL libchimera.dll would not load (error %lu); run this beside it\n",
		        (unsigned long)GetLastError());
		return 2;
	}
	ce_session_open = (ce_session_open_fn)(void *)GetProcAddress(engine, "ce_session_open");
	if (ce_session_open == NULL)
	{
		fprintf(stderr, "FAIL libchimera.dll exports no ce_session_open\n");
		return 2;
	}

	/* The control, first: this whole test is one HeapValidate call, so prove
	 * that HeapValidate on THIS heap can say both things before believing
	 * either of them. */
	crt = (HANDLE)(uintptr_t)_get_heap_handle();
	control = malloc(400);
	control_live = control != NULL && HeapValidate(crt, 0, control);
	free(control);
	/* asking about a freed block on purpose: that is the half of the control
	 * which proves this check can answer "freed" at all */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuse-after-free"
	control_freed = control != NULL && HeapValidate(crt, 0, control);
#pragma GCC diagnostic pop
	printf("control: a live block reads %s, the same block once freed reads %s\n",
	       control_live ? "live" : "freed", control_freed ? "live" : "freed");
	if (!control_live || control_freed)
	{
		fprintf(stderr, "FAIL the control did not behave: this check cannot tell live from freed here\n");
		return 2;
	}

	/* 1. A refusal that needs nothing but the engine: a package that is not
	 *    there. The sentence has to be longer than a std::string keeps inside
	 *    itself, or there is no heap buffer to free and the check is vacuous. */
	if (!open_on_its_own_thread("no-such-package.chimeraCore", NULL)) return 2;
	if (g_error == NULL || strlen(g_error_text) <= 15)
	{
		fprintf(stderr, "FAIL the engine answered %s - this check needs a long refusal sentence\n",
		        g_error == NULL ? "no error at all" : "too short an error");
		return 2;
	}
	printf("refused: \"%s\"\n", g_error_text);
	still_live = HeapValidate(crt, 0, (LPCVOID)g_error);
	printf("the thread that was refused has ended; its error string reads %s\n",
	       still_live ? "live" : "FREED");
	if (!still_live)
	{
		fprintf(stderr,
			"FAIL the engine's per-thread error string was freed when its thread ended.\n"
			"     A mingw DLL destroys a thread_local twice (chimera#123), so that buffer\n"
			"     is freed again at process exit and the CRT heap is corrupt from here on.\n");
		failures++;
	}

	/* 2. The real shape, when the caller has a core and something it will not
	 *    take: a machine REFUSED AT Init and torn down by the engine. Pre-fix
	 *    this did not crash on its own - the heap is too quiet in a program
	 *    this small - so it is here as the regression it is, not as the
	 *    detector. */
	if (argc >= 3 && strcmp(argv[1], "-") != 0)
	{
		if (!open_on_its_own_thread(argv[1], argv[2])) return 2;
		if (g_session != NULL)
		{
			fprintf(stderr, "FAIL %s was expected to refuse %s, and opened it\n", argv[1], argv[2]);
			failures++;
		}
		else
		{
			printf("refused at Init: \"%s\"\n", g_error_text);
			if (!HeapValidate(crt, 0, (LPCVOID)g_error))
			{
				fprintf(stderr, "FAIL the error string of a refused Init was freed with its thread\n");
				failures++;
			}
			if (!HeapValidate(crt, 0, NULL))
			{
				fprintf(stderr, "FAIL the C runtime's heap is corrupt after a refused Init\n");
				failures++;
			}
		}
	}
	else
	{
		printf("SKIP the refused-Init half: no core package and rom given "
		       "(would prove: a machine refused at Init is torn down and the heap survives it)\n");
	}

	/* 4. The suggestion call, both ways: a package that is not there (an error
	 *    through error_out) and, when given one, a core asked about a file -
	 *    a core with no SuggestSettings answers "". Each on a thread that then
	 *    ends; the heap must be whole after, and the process must end cleanly,
	 *    which the script checks (exit status, no fault report). Before the
	 *    answer was a ThreadString, every such process died at exit with an
	 *    access violation in the heap. */
	ce_suggest_settings = (ce_suggest_settings_fn)(void *)GetProcAddress(engine, "ce_suggest_settings");
	if (ce_suggest_settings == NULL)
	{
		fprintf(stderr, "FAIL libchimera.dll exports no ce_suggest_settings\n");
		failures++;
	}
	else
	{
		if (!suggest_on_its_own_thread("no-such-package.chimeraCore", NULL)) return 2;
		printf("suggest, no package: %s \"%s\"\n", g_answer == NULL ? "refused" : "ANSWERED", g_error_text);
		if (g_answer != NULL) failures++;
		/* A core that answers "" proves nothing here - an empty string owns no
		 * heap buffer, so freeing it twice is harmless (thread_string.hpp).
		 * The detector is a core that answers in a SENTENCE: argv[3] and
		 * argv[4], a core with SuggestSettings and a file to ask about. */
		if (argc >= 5)
		{
			/* ON THE MAIN THREAD, deliberately: the double destroy happens on
			 * the thread that ends the process (thread_string.hpp), which is
			 * where Chimera --suggest-settings asks. A worker's copy is
			 * destroyed once, when it ends, and hides the bug. */
			g_package = argv[3];
			g_rom_path = argv[4];
			suggest(NULL);
			printf("suggest, %s: %s %u bytes\n", argv[3], g_answer == NULL ? "REFUSED" : "answered",
			       (unsigned)strlen(g_answer_text));
			if (g_answer == NULL || strlen(g_answer_text) < 64)
			{
				fprintf(stderr, "FAIL the suggesting core gave no long answer, so this check tested nothing\n");
				failures++;
			}
		}
		else
		{
			printf("SKIP the suggestion's long answer: no suggesting core given "
			       "(would prove: its answer string is not freed twice at exit)\n");
		}
		if (!HeapValidate(crt, 0, NULL))
		{
			fprintf(stderr, "FAIL the C runtime's heap is corrupt after the suggestion calls\n");
			failures++;
		}
	}

	printf(failures == 0 ? "PASS refused-open\n" : "FAIL refused-open: %d check(s) failed\n", failures);
	return failures == 0 ? 0 : 1;
}
