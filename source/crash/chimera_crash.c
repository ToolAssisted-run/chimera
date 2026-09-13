/*
 * chimera_crash.dll - what a Windows crash that no handler sees leaves behind
 * (docs/project.md, "Crash notes").
 *
 * A graphics driver that finds its own state corrupt ends the process with
 * __fastfail: no exception handler, no finally, no managed event runs inside a
 * process that does that. What still runs is Windows Error Reporting, in its
 * own process (WerFault.exe), and WER loads the "runtime exception helper
 * module" a process registered (WerRegisterRuntimeExceptionModule, allowed by
 * a per-user registry value - no administrator) and hands it the dead process.
 * This is that module. It reads the context block Chimera keeps in its own
 * memory (the folder to write to, the frame, a few lines about the session),
 * and writes a note - what failed, where, the faulting thread's stack - and a
 * minidump beside it. It claims nothing: Windows goes on to record the crash
 * as it always does.
 *
 * Nothing here runs in Chimera. It must not assume anything about the dead
 * process beyond what it can read, and everything it reads is clamped.
 */
#include <windows.h>
#include <dbghelp.h>
#include <psapi.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "gl_names.h"

/* What WER hands a runtime exception helper module. Declared here: mingw-w64's
 * werapi.h does not compile on its own, and this prefix is all this module reads. */
typedef struct _WER_RUNTIME_EXCEPTION_INFORMATION {
	DWORD dwSize;
	HANDLE hProcess;
	HANDLE hThread;
	EXCEPTION_RECORD exceptionRecord;
	CONTEXT context;
	PCWSTR pwszReportId;
	BOOL bIsFatal;
	DWORD dwReserved;
} WER_RUNTIME_EXCEPTION_INFORMATION, *PWER_RUNTIME_EXCEPTION_INFORMATION;

/* The context block, laid out exactly as CrashCapture.cs writes it (version 2 adds
 * the GL flight recorder; the module and the frontend ship in one bundle). */
#define CONTEXT_MAGIC 0x52434843u /* "CHCR" */
#define CONTEXT_VERSION 2u
#define FOLDER_CHARS 520
#define SESSION_CAPACITY 16380
#pragma pack(push, 1)
typedef struct {
	UINT32 magic;
	UINT32 version;
	WCHAR folder[FOLDER_CHARS];
	INT64 frame;
	UINT64 glRecorder;      /* the engine's ce_gl_flight_recorder, or 0 */
	UINT32 glRecorderBytes;
	UINT32 sessionLength;
} context_header;
#pragma pack(pop)

/* The GPU bridge's flight recorder, laid out as source/engine/source/gl_bridge.cpp
 * keeps it: a header, then a ring of (opcode, detail). */
#define FLIGHT_MAGIC 0x4C474543u /* "CEGL" */
#define FLIGHT_LINES 400
typedef struct {
	UINT32 magic;
	UINT32 capacity;
	UINT64 written;
} flight_header;
typedef struct {
	UINT32 op;
	UINT32 detail;
} flight_entry;
typedef struct {
	UINT32 op;
	UINT32 detail;
	UINT32 count;
} flight_line;

static const char *fast_fail_name(ULONG_PTR code)
{
	static const char *const names[] = {
		"LEGACY_GS_VIOLATION", "VTGUARD_CHECK_FAILURE", "STACK_COOKIE_CHECK_FAILURE",
		"CORRUPT_LIST_ENTRY", "INCORRECT_STACK", "INVALID_ARG", "GS_COOKIE_INIT",
		"FATAL_APP_EXIT", "RANGE_CHECK_FAILURE", "UNSAFE_REGISTRY_ACCESS",
		"GUARD_ICALL_CHECK_FAILURE", "GUARD_WRITE_CHECK_FAILURE", "INVALID_FIBER_SWITCH",
		"INVALID_SET_OF_CONTEXT", "INVALID_REFERENCE_COUNT",
	};
	return code < sizeof names / sizeof names[0] ? names[code] : "";
}

static const char *code_kind(DWORD code)
{
	switch (code) {
	case 0xC0000409: return "fast fail (the process ended itself; no handler runs)";
	case 0xC0000005: return "access violation";
	case 0xC00000FD: return "stack overflow";
	case 0xC0000374: return "heap corruption";
	case 0xC000001D: return "illegal instruction";
	case 0xC0000094: return "integer division by zero";
	case 0xE0434352: return ".NET exception";
	case 0x80000003: return "breakpoint";
	default: return "exception";
	}
}

static void describe_module(HANDLE process, DWORD64 address, wchar_t *name, DWORD64 *offset)
{
	HMODULE mods[1024];
	DWORD needed = 0;
	wcscpy(name, L"(no module)");
	*offset = address;
	if (!EnumProcessModulesEx(process, mods, sizeof mods, &needed, LIST_MODULES_ALL)) return;
	if (needed > sizeof mods) needed = sizeof mods;
	for (DWORD i = 0; i < needed / sizeof(HMODULE); i++) {
		MODULEINFO mi;
		if (!GetModuleInformation(process, mods[i], &mi, sizeof mi)) continue;
		DWORD64 base = (DWORD64)(ULONG_PTR)mi.lpBaseOfDll;
		if (address >= base && address < base + mi.SizeOfImage) {
			if (!GetModuleBaseNameW(process, mods[i], name, MAX_PATH)) wcscpy(name, L"(unnamed module)");
			*offset = address - base;
			return;
		}
	}
}

static void write_gl_entry(FILE *f, const flight_line *line)
{
	char name[96];
	switch (line->op) {
	case 0xFFFFFF01: snprintf(name, sizeof name, "-- a frame takes the context"); break;
	case 0xFFFFFF02: snprintf(name, sizeof name, "-- the frame gives it back (%lu calls)", (unsigned long)line->detail); break;
	case 0xFFFFFF03: snprintf(name, sizeof name, "-- a state is loaded (frame %lu)", (unsigned long)line->detail); break;
	case 0xFFFFFF04: snprintf(name, sizeof name, "-- a session takes the bridge (a fresh context id)"); break;
	case 0xFFFFFF05: snprintf(name, sizeof name, "-- the context is destroyed"); break;
	default:
		if (line->op >= GL_NAMES_FIRST_OPCODE && line->op - GL_NAMES_FIRST_OPCODE < GL_NAMES_COUNT)
			snprintf(name, sizeof name, "%s", gl_names[line->op - GL_NAMES_FIRST_OPCODE]);
		else
			snprintf(name, sizeof name, "bridge op %lu", (unsigned long)line->op);
	}
	if (line->count > 1) fprintf(f, "  %s x%lu\n", name, (unsigned long)line->count);
	else fprintf(f, "  %s\n", name);
}

/* The last calls that crossed the GPU bridge, oldest first, repeats folded. The
 * entry is written before the driver is called, so a crash inside a GL call
 * leaves that call last. */
static void write_gl_calls(FILE *f, HANDLE process, UINT64 address, UINT32 bytes)
{
	if (address == 0 || bytes < sizeof(flight_header)) return;
	flight_header header;
	SIZE_T got = 0;
	if (!ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)address, &header, sizeof header, &got) || got != sizeof header) return;
	if (header.magic != FLIGHT_MAGIC || header.capacity == 0 || (header.capacity & (header.capacity - 1)) != 0
		|| sizeof header + (UINT64)header.capacity * sizeof(flight_entry) > bytes) return;
	if (header.written == 0) {
		fprintf(f, "\ngl: nothing crossed the GPU bridge this run\n");
		return;
	}
	const HANDLE heap = GetProcessHeap();
	flight_entry *entries = (flight_entry *)HeapAlloc(heap, 0, header.capacity * sizeof(flight_entry));
	flight_line *lines = (flight_line *)HeapAlloc(heap, 0, header.capacity * sizeof(flight_line));
	if (entries && lines
		&& ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)(address + sizeof header), entries, header.capacity * sizeof(flight_entry), &got)
		&& got == header.capacity * sizeof(flight_entry)) {
		const UINT64 kept = header.written < header.capacity ? header.written : header.capacity;
		UINT32 count = 0;
		for (UINT64 i = header.written - kept; i < header.written; i++) {
			const flight_entry *entry = &entries[i & (header.capacity - 1)];
			if (count > 0 && lines[count - 1].op == entry->op && lines[count - 1].detail == entry->detail) {
				lines[count - 1].count++;
				continue;
			}
			lines[count].op = entry->op;
			lines[count].detail = entry->detail;
			lines[count].count = 1;
			count++;
		}
		const UINT32 first = count > FLIGHT_LINES ? count - FLIGHT_LINES : 0;
		fprintf(f, "\ngl (the last %llu of %llu crossings of the GPU bridge, oldest first; the newest is last):\n",
			(unsigned long long)kept, (unsigned long long)header.written);
		for (UINT32 i = first; i < count; i++) write_gl_entry(f, &lines[i]);
		fflush(f);
	}
	if (entries) HeapFree(heap, 0, entries);
	if (lines) HeapFree(heap, 0, lines);
}

static void write_stack(FILE *f, HANDLE process, HANDLE thread, CONTEXT ctx)
{
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
	const BOOL symbols = SymInitializeW(process, NULL, TRUE);
	STACKFRAME64 frame;
	memset(&frame, 0, sizeof frame);
	frame.AddrPC.Offset = ctx.Rip;
	frame.AddrPC.Mode = AddrModeFlat;
	frame.AddrFrame.Offset = ctx.Rbp;
	frame.AddrFrame.Mode = AddrModeFlat;
	frame.AddrStack.Offset = ctx.Rsp;
	frame.AddrStack.Mode = AddrModeFlat;
	fprintf(f, "\nstack (faulting thread%s):\n", symbols ? "" : ", no symbols");
	for (int i = 0; i < 64; i++) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, NULL,
				SymFunctionTableAccess64, SymGetModuleBase64, NULL)) break;
		DWORD64 pc = frame.AddrPC.Offset;
		if (pc == 0) break;
		wchar_t module[MAX_PATH];
		DWORD64 offset = 0;
		describe_module(process, pc, module, &offset);
		char buffer[sizeof(SYMBOL_INFO) + 256];
		SYMBOL_INFO *symbol = (SYMBOL_INFO *)buffer;
		memset(buffer, 0, sizeof buffer);
		symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		symbol->MaxNameLen = 255;
		DWORD64 displacement = 0;
		if (symbols && SymFromAddr(process, pc, &displacement, symbol))
			fprintf(f, "  #%02d %ls+0x%llx  %s+0x%llx\n", i, module, (unsigned long long)offset,
				symbol->Name, (unsigned long long)displacement);
		else
			fprintf(f, "  #%02d %ls+0x%llx\n", i, module, (unsigned long long)offset);
		fflush(f);
	}
	if (symbols) SymCleanup(process);
}

__declspec(dllexport) HRESULT WINAPI OutOfProcessExceptionEventCallback(PVOID context,
	const PWER_RUNTIME_EXCEPTION_INFORMATION info, PBOOL claimed, PWSTR eventName, PDWORD eventNameSize,
	PDWORD signatureCount)
{
	(void)eventName; (void)eventNameSize; (void)signatureCount;
	*claimed = FALSE;

	context_header header;
	SIZE_T got = 0;
	if (!ReadProcessMemory(info->hProcess, context, &header, sizeof header, &got) || got != sizeof header) return S_OK;
	if (header.magic != CONTEXT_MAGIC || header.version != CONTEXT_VERSION) return S_OK;
	header.folder[FOLDER_CHARS - 1] = 0;
	if (header.folder[0] == 0) return S_OK;

	static char session[SESSION_CAPACITY + 1];
	UINT32 sessionLength = header.sessionLength < SESSION_CAPACITY ? header.sessionLength : SESSION_CAPACITY;
	got = 0;
	if (sessionLength && !ReadProcessMemory(info->hProcess, (LPVOID)((ULONG_PTR)context + sizeof header), session, sessionLength, &got))
		got = 0;
	session[got] = 0;

	CreateDirectoryW(header.folder, NULL);
	const DWORD pid = GetProcessId(info->hProcess);
	SYSTEMTIME t;
	GetLocalTime(&t);
	wchar_t stem[FOLDER_CHARS + 64];
	swprintf(stem, sizeof stem / sizeof stem[0], L"%ls\\%04u-%02u-%02u %02u.%02u.%02u pid%lu",
		header.folder, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, (unsigned long)pid);
	wchar_t notePath[FOLDER_CHARS + 72], dumpPath[FOLDER_CHARS + 72];
	swprintf(notePath, sizeof notePath / sizeof notePath[0], L"%ls.txt", stem);
	swprintf(dumpPath, sizeof dumpPath / sizeof dumpPath[0], L"%ls.dmp", stem);

	const EXCEPTION_RECORD *record = &info->exceptionRecord;
	const DWORD code = record->ExceptionCode;
	const DWORD64 at = (DWORD64)(ULONG_PTR)record->ExceptionAddress;
	const ULONG_PTR param0 = record->NumberParameters > 0 ? record->ExceptionInformation[0] : 0;
	const ULONG_PTR param1 = record->NumberParameters > 1 ? record->ExceptionInformation[1] : 0;
	wchar_t module[MAX_PATH];
	DWORD64 offset = 0;
	describe_module(info->hProcess, at, module, &offset);

	/* The note first, flushed line by line: whatever step below fails or stalls,
	 * what failed and where is already on disk. */
	FILE *f = _wfopen(notePath, L"wb");
	if (f) {
		fprintf(f, "Chimera crash note\n");
		fprintf(f, "time=%04u-%02u-%02u %02u:%02u:%02u\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		fprintf(f, "process=%lu\n", (unsigned long)pid);
		fprintf(f, "code=0x%08lx\n", (unsigned long)code);
		fprintf(f, "kind=%s\n", code_kind(code));
		if (code == 0xC0000409) fprintf(f, "fastfail=%llu %s\n", (unsigned long long)param0, fast_fail_name(param0));
		if (code == 0xC0000005)
			fprintf(f, "access=%s 0x%llx\n", param0 == 0 ? "read" : param0 == 1 ? "write" : param0 == 8 ? "execute" : "?",
				(unsigned long long)param1);
		fprintf(f, "address=0x%llx\n", (unsigned long long)at);
		fprintf(f, "module=%ls\n", module);
		fprintf(f, "offset=0x%llx\n", (unsigned long long)offset);
		fprintf(f, "fatal=%d\n", info->bIsFatal ? 1 : 0);
		fprintf(f, "frame=%lld\n", (long long)header.frame);
		fprintf(f, "report=%ls\n", info->pwszReportId ? info->pwszReportId : L"");
		fflush(f);
		if (session[0]) {
			fprintf(f, "\nsession:\n");
			for (char *line = session; *line;) {
				char *end = strchr(line, '\n');
				if (end) *end = 0;
				fprintf(f, "  %s\n", line);
				if (!end) break;
				line = end + 1;
			}
			fflush(f);
		}
		write_stack(f, info->hProcess, info->hThread, info->context);
		write_gl_calls(f, info->hProcess, header.glRecorder, header.glRecorderBytes);
	}

	HANDLE dump = CreateFileW(dumpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	BOOL dumped = FALSE;
	if (dump != INVALID_HANDLE_VALUE) {
		EXCEPTION_RECORD recordCopy = info->exceptionRecord;
		CONTEXT contextCopy = info->context;
		EXCEPTION_POINTERS pointers = { &recordCopy, &contextCopy };
		MINIDUMP_EXCEPTION_INFORMATION exception = { GetThreadId(info->hThread), &pointers, FALSE };
		dumped = MiniDumpWriteDump(info->hProcess, pid, dump,
			(MINIDUMP_TYPE)(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules | MiniDumpWithIndirectlyReferencedMemory),
			&exception, NULL, NULL);
		CloseHandle(dump);
		if (!dumped) DeleteFileW(dumpPath);
	}
	if (f) {
		fprintf(f, "\ndump=%s\n", dumped ? "written" : "not written");
		fclose(f);
	}
	return S_OK;
}

__declspec(dllexport) HRESULT WINAPI OutOfProcessExceptionEventSignatureCallback(PVOID context,
	const PWER_RUNTIME_EXCEPTION_INFORMATION info, DWORD index, PWSTR name, PDWORD nameSize, PWSTR value, PDWORD valueSize)
{
	(void)context; (void)info; (void)index; (void)name; (void)nameSize; (void)value; (void)valueSize;
	return E_NOTIMPL;
}

__declspec(dllexport) HRESULT WINAPI OutOfProcessExceptionEventDebuggerLaunchCallback(PVOID context,
	const PWER_RUNTIME_EXCEPTION_INFORMATION info, PBOOL isCustomDebugger, PWSTR launch, PDWORD launchSize, PBOOL autolaunch)
{
	(void)context; (void)info; (void)isCustomDebugger; (void)launch; (void)launchSize; (void)autolaunch;
	return E_NOTIMPL;
}
