#include "host_dyn.hpp"
#include "language.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace chimera {

namespace {

HostApi g_api;
void *g_lib = nullptr;
bool g_ok = false;
std::string g_error;

void *sym(void *lib, const char *name)
{
#if defined(_WIN32)
	return reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(lib), name));
#else
	return dlsym(lib, name);
#endif
}

template <typename T>
bool bind(T &fn, const char *name)
{
	fn = reinterpret_cast<T>(sym(g_lib, name));
	return fn != nullptr;
}

void loadOnce()
{
#if defined(_WIN32)
	g_lib = openLibraryBesideUs("libminiboxhost.dll", false, g_error);
#else
	g_lib = openLibraryBesideUs("libminiboxhost.so", false, g_error);
#endif
	if (g_lib == nullptr) return;
	g_ok = bind(g_api.wbx_build_info, "wbx_build_info")
		&& bind(g_api.wbx_create_host, "wbx_create_host")
		&& bind(g_api.wbx_destroy_host, "wbx_destroy_host")
		&& bind(g_api.wbx_activate_host, "wbx_activate_host")
		&& bind(g_api.wbx_deactivate_host, "wbx_deactivate_host")
		&& bind(g_api.wbx_get_proc_addr, "wbx_get_proc_addr")
		&& bind(g_api.wbx_get_callback_addr, "wbx_get_callback_addr")
		&& bind(g_api.wbx_seal, "wbx_seal")
		/* not in the && chain: a host without it still works, it just cannot
		 * tell a cached greenzone that its machine changed underneath it */
		&& (bind(g_api.wbx_machine_hash, "wbx_machine_hash") || true)
		&& (bind(g_api.wbx_get_page_len, "wbx_get_page_len") || true)
		&& (bind(g_api.wbx_get_page_data, "wbx_get_page_data") || true)
		&& (bind(g_api.wbx_guest_range_ok, "wbx_guest_range_ok") || true)
		&& (bind(g_api.wbx_guest_read_view, "wbx_guest_read_view") || true)
		&& (bind(g_api.wbx_get_death, "wbx_get_death") || true)
		&& (bind(g_api.wbx_stop_from_here, "wbx_stop_from_here") || true)
		&& (bind(g_api.wbx_stop_guest, "wbx_stop_guest") || true)
		&& (bind(g_api.wbx_stop_cancel, "wbx_stop_cancel") || true)
		&& (bind(g_api.wbx_guest_activity, "wbx_guest_activity") || true)
		&& (bind(g_api.wbx_count_work_from, "wbx_count_work_from") || true)
		&& bind(g_api.wbx_mount_file, "wbx_mount_file")
		&& bind(g_api.wbx_mount_file_path, "wbx_mount_file_path")
		&& bind(g_api.wbx_save_state, "wbx_save_state")
		&& bind(g_api.wbx_load_state, "wbx_load_state");
	if (!g_ok) g_error = chimera::lang::tr("host.missing_symbols");

	/* The optional four, bound on their own: a host without them is not a
	 * broken host, it is one that predates epochs, and the state history falls
	 * back to whole states. Only offered as a set - half of it is no use. */
	if (g_ok
		&& !(bind(g_api.wbx_epoch_begin, "wbx_epoch_begin")
			&& bind(g_api.wbx_save_delta, "wbx_save_delta")
			&& bind(g_api.wbx_load_delta, "wbx_load_delta")
			&& bind(g_api.wbx_get_epoch_page_count, "wbx_get_epoch_page_count")))
	{
		g_api.wbx_epoch_begin = nullptr;
		g_api.wbx_save_delta = nullptr;
		g_api.wbx_load_delta = nullptr;
		g_api.wbx_get_epoch_page_count = nullptr;
	}
	/* Composition on its own: epochs without it means a history that keeps
	 * every link it captured, which is dense and expensive but not wrong. */
	if (!g_ok || !bind(g_api.wbx_compose_delta, "wbx_compose_delta")) g_api.wbx_compose_delta = nullptr;
	/* the core log, which an older host cannot keep */
	if (!g_ok || !bind(g_api.wbx_set_output_file, "wbx_set_output_file")) g_api.wbx_set_output_file = nullptr;
	if (!g_ok || !bind(g_api.wbx_set_clock_start, "wbx_set_clock_start")) g_api.wbx_set_clock_start = nullptr;
	/* a core's tools, as a pair: writing without closing is no use */
	if (!g_ok || !(bind(g_api.wbx_mount_file_path_writable, "wbx_mount_file_path_writable")
			&& bind(g_api.wbx_unmount_file, "wbx_unmount_file")))
	{
		g_api.wbx_mount_file_path_writable = nullptr;
		g_api.wbx_unmount_file = nullptr;
	}
	/* and the in-memory form of it, which an older host does not have */
	if (!g_ok || !bind(g_api.wbx_compose_delta_mem, "wbx_compose_delta_mem")) g_api.wbx_compose_delta_mem = nullptr;

	/* Taking a state while the machine runs: all five or none. A host with
	 * half of them could hold pages it has no way to release. */
	if (!g_ok
		|| !(bind(g_api.wbx_state_size, "wbx_state_size")
			&& bind(g_api.wbx_state_plan, "wbx_state_plan")
			&& bind(g_api.wbx_state_pages, "wbx_state_pages")
			&& bind(g_api.wbx_state_fill, "wbx_state_fill")
			&& bind(g_api.wbx_state_finish, "wbx_state_finish")))
	{
		g_api.wbx_state_size = nullptr;
		g_api.wbx_state_plan = nullptr;
		g_api.wbx_state_pages = nullptr;
		g_api.wbx_state_fill = nullptr;
		g_api.wbx_state_finish = nullptr;
	}
}

std::mutex g_loadLock;

} // namespace

#if defined(_WIN32)
static std::string lastErrorText(DWORD code)
{
	wchar_t *text = nullptr;
	const DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, code, 0, reinterpret_cast<LPWSTR>(&text), 0, nullptr);
	std::string out;
	if (n != 0 && text != nullptr)
	{
		const int len = WideCharToMultiByte(CP_UTF8, 0, text, (int)n, nullptr, 0, nullptr, nullptr);
		out.resize(len > 0 ? (size_t)len : 0);
		if (len > 0) WideCharToMultiByte(CP_UTF8, 0, text, (int)n, &out[0], len, nullptr, nullptr);
		while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ' || out.back() == '.')) out.pop_back();
	}
	if (text != nullptr) LocalFree(text);
	char code_text[32];
	std::snprintf(code_text, sizeof code_text, "%lu", (unsigned long)code);
	const std::string coded = chimera::lang::tr("host.error_code", {code_text});
	return out.empty() ? coded : out + " (" + coded + ")";
}
#endif

void *openLibraryBesideUs(const char *name, bool deepBind, std::string &why)
{
	why.clear();
#if defined(_WIN32)
	(void)deepBind;
	HMODULE self = nullptr;
	wchar_t path[MAX_PATH];
	if (GetModuleHandleExW(
			GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&openLibraryBesideUs), &self)
		&& GetModuleFileNameW(self, path, MAX_PATH) != 0)
	{
		std::wstring dir(path);
		auto slash = dir.find_last_of(L"\\/");
		if (slash != std::wstring::npos)
		{
			std::wstring full = dir.substr(0, slash + 1);
			for (const char *c = name; *c != '\0'; c++) full.push_back(static_cast<wchar_t>(*c));
			if (HMODULE h = LoadLibraryW(full.c_str())) return h;
			const DWORD code = GetLastError();
			/* there but refused (blocked, held by a scanner, a runtime of its
			 * own missing) says so, rather than being looked for elsewhere */
			if (GetFileAttributesW(full.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				why = chimera::lang::tr("host.beside_not_loaded", {name, lastErrorText(code)});
				return nullptr;
			}
			/* a tool in a bundle's bin\ folder: the DLLs are in lib\ beside it */
			std::wstring inLib = dir.substr(0, slash + 1) + L"..\\lib\\";
			for (const char *c = name; *c != '\0'; c++) inLib.push_back(static_cast<wchar_t>(*c));
			if (GetFileAttributesW(inLib.c_str()) != INVALID_FILE_ATTRIBUTES)
			{
				if (HMODULE h = LoadLibraryW(inLib.c_str())) return h;
				why = chimera::lang::tr("host.lib_not_loaded", {name, lastErrorText(GetLastError())});
				return nullptr;
			}
		}
	}
	if (HMODULE h = LoadLibraryExA(name, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)) return h;
	why = chimera::lang::tr("host.not_found_windows", {name, lastErrorText(GetLastError())});
	return nullptr;
#else
	const int flags = RTLD_NOW | RTLD_LOCAL | (deepBind ? RTLD_DEEPBIND : 0);
	Dl_info info;
	if (dladdr(reinterpret_cast<void *>(&openLibraryBesideUs), &info) != 0 && info.dli_fname != nullptr)
	{
		std::string dir(info.dli_fname);
		auto slash = dir.find_last_of('/');
		if (slash != std::string::npos)
		{
			std::string full = dir.substr(0, slash + 1) + name;
			if (void *h = dlopen(full.c_str(), flags)) return h;
			const char *e = dlerror();
			if (access(full.c_str(), F_OK) == 0)
			{
				why = chimera::lang::tr("host.beside_not_loaded", {name, e != nullptr ? e : chimera::lang::tr("common.no_reason")});
				return nullptr;
			}
		}
	}
	if (void *h = dlopen(name, flags)) return h;
	const char *e = dlerror();
	why = chimera::lang::tr("host.not_found", {name, e != nullptr ? e : chimera::lang::tr("common.no_reason")});
	return nullptr;
#endif
}

std::string folderOfUs()
{
#if defined(_WIN32)
	HMODULE self = nullptr;
	wchar_t path[MAX_PATH];
	if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCWSTR>(&folderOfUs), &self)
		|| GetModuleFileNameW(self, path, MAX_PATH) == 0)
	{
		return {};
	}
	std::wstring dir(path);
	const auto slash = dir.find_last_of(L"\\/");
	if (slash == std::wstring::npos) return {};
	dir.resize(slash + 1);
	const int n = WideCharToMultiByte(CP_UTF8, 0, dir.data(), static_cast<int>(dir.size()), nullptr, 0, nullptr, nullptr);
	std::string out(static_cast<size_t>(n > 0 ? n : 0), '\0');
	if (n > 0) WideCharToMultiByte(CP_UTF8, 0, dir.data(), static_cast<int>(dir.size()), out.data(), n, nullptr, nullptr);
	return out;
#else
	Dl_info info;
	if (dladdr(reinterpret_cast<void *>(&folderOfUs), &info) == 0 || info.dli_fname == nullptr) return {};
	std::string dir(info.dli_fname);
	const auto slash = dir.find_last_of('/');
	return slash == std::string::npos ? std::string("./") : dir.substr(0, slash + 1);
#endif
}

bool retryLibraryLoad(int64_t &lastMs)
{
	const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	if (lastMs != 0 && now - lastMs < 2000) return false;
	lastMs = now;
	return true;
}

const HostApi *hostApi(const char **error)
{
	/* Loaded until it loads: a library that would not load the first time (a
	 * scanner holding it, say) is tried again by the next caller. One that
	 * loaded but lacks what is needed stays refused - it will not change. */
	std::lock_guard<std::mutex> lock(g_loadLock);
	static int64_t lastTry = 0;
	if (g_lib == nullptr && retryLibraryLoad(lastTry)) loadOnce();
	if (!g_ok)
	{
		if (error != nullptr) *error = g_error.c_str();
		return nullptr;
	}
	return &g_api;
}

#if defined(_WIN32)

namespace {

constexpr int MAX_ARGS = 6; // depart0..depart6, matching miniBox
constexpr int STUB_SIZE = 32;
constexpr int STUB_COUNT = 256;

uintptr_t g_departs[MAX_ARGS + 1];
bool g_departsLoaded = false;
uint8_t *g_stubPage = nullptr;
int g_stubsUsed = 0;
std::map<std::pair<uintptr_t, int>, uintptr_t> g_stubs;

bool loadDeparts()
{
	if (g_departsLoaded) return true;
	const char *err = nullptr;
	if (hostApi(&err) == nullptr) return false;
	for (int i = 0; i <= MAX_ARGS; i++)
	{
		char name[16];
		std::snprintf(name, sizeof name, "depart%d", i);
		g_departs[i] = reinterpret_cast<uintptr_t>(sym(g_lib, name));
		if (g_departs[i] == 0) return false;
	}
	g_stubPage = static_cast<uint8_t *>(VirtualAlloc(
		nullptr, STUB_SIZE * STUB_COUNT, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
	if (g_stubPage == nullptr) return false;
	g_departsLoaded = true;
	return true;
}

} // namespace

uintptr_t bridgeGuestCall(uintptr_t guestEntry, int argCount)
{
	if (guestEntry == 0) return 0;
	if (argCount < 0 || argCount > MAX_ARGS) return 0;
	if (!loadDeparts()) return 0;
	auto key = std::make_pair(guestEntry, argCount);
	auto it = g_stubs.find(key);
	if (it != g_stubs.end()) return it->second;
	if (g_stubsUsed >= STUB_COUNT)
	{
		/* Full: another page. A session takes 40 to 90 stubs and a new one's
		 * exports are new keys whenever its thunks land elsewhere, so a fixed
		 * 256 ran out after a few reopens - and an export with no stub reads
		 * exactly like one the core lacks. The full page stays: a stub in it
		 * may be in a live session's table, and a page per 256 stubs is not
		 * worth taking back. */
		uint8_t *more = static_cast<uint8_t *>(VirtualAlloc(
			nullptr, STUB_SIZE * STUB_COUNT, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
		if (more == nullptr) return 0;
		g_stubPage = more;
		g_stubsUsed = 0;
	}

	/* 48 B8 <target>  mov rax, imm64
	 * 49 BB <departN> mov r11, imm64
	 * 41 FF E3        jmp r11
	 * (r11: caller-saved in both conventions, never carries an argument) */
	uint8_t *stub = g_stubPage + g_stubsUsed * STUB_SIZE;
	stub[0] = 0x48; stub[1] = 0xB8;
	std::memcpy(stub + 2, &guestEntry, 8);
	stub[10] = 0x49; stub[11] = 0xBB;
	std::memcpy(stub + 12, &g_departs[argCount], 8);
	stub[20] = 0x41; stub[21] = 0xFF; stub[22] = 0xE3;
	FlushInstructionCache(GetCurrentProcess(), stub, STUB_SIZE);
	g_stubsUsed++;
	g_stubs.emplace(key, reinterpret_cast<uintptr_t>(stub));
	return reinterpret_cast<uintptr_t>(stub);
}

#else

uintptr_t bridgeGuestCall(uintptr_t guestEntry, int argCount)
{
	(void)argCount;
	return guestEntry; // host and guest are both sysv64
}

#endif

} // namespace chimera
