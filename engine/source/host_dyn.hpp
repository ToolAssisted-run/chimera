/* host_dyn.hpp - libminiboxhost, loaded at runtime from beside the engine,
 * plus the calling-convention bridge into the guest.
 *
 * A waterbox guest is ALWAYS sysv64 - it is a Linux ELF, whatever the host
 * runs on. On Linux that matches us and guest entry points are called
 * directly. On Windows the engine speaks win64, so a guest entry point is
 * wrapped in a stub that routes through the host's departN trampolines -
 * the same mechanism the C# WaterboxAbiShim used.
 */

#ifndef CHIMERA_HOST_DYN_HPP
#define CHIMERA_HOST_DYN_HPP

#include <cstddef>
#include <cstdint>

namespace chimera {

/* mb_return: a 1024-byte error string plus a result word. */
struct WbxReturn
{
	char errorMessage[1024];
	uintptr_t data;

	bool ok() const { return errorMessage[0] == '\0'; }
};

using WbxReadCb = intptr_t (*)(uintptr_t userdata, void *data, uintptr_t size);
using WbxWriteCb = int32_t (*)(uintptr_t userdata, const void *data, uintptr_t size);

/* Page-aligned guest heap sizes (mirrors mb_memory_layout_template). */
struct WbxLayout
{
	uintptr_t sbrkSize, sealedSize, invisSize, plainSize, mmapSize;
};

struct HostApi
{
	const char *(*wbx_build_info)(void);
	void (*wbx_create_host)(const WbxLayout *layout, const char *moduleName, WbxReadCb cb, uintptr_t userdata, WbxReturn *ret);
	void (*wbx_destroy_host)(void *obj, WbxReturn *ret);
	void (*wbx_activate_host)(void *obj, WbxReturn *ret);
	void (*wbx_deactivate_host)(void *obj, WbxReturn *ret);
	void (*wbx_get_proc_addr)(void *obj, const char *name, WbxReturn *ret);
	void (*wbx_seal)(void *obj, WbxReturn *ret);
	void (*wbx_mount_file)(void *obj, const char *name, WbxReadCb cb, uintptr_t userdata, uint8_t writable, WbxReturn *ret);
	void (*wbx_save_state)(void *obj, WbxWriteCb cb, uintptr_t userdata, WbxReturn *ret);
	void (*wbx_load_state)(void *obj, WbxReadCb cb, uintptr_t userdata, WbxReturn *ret);
};

/* The loaded host, or nullptr with *error set. Loads once, then cached. */
const HostApi *hostApi(const char **error);

/* Returns an entry point callable with OUR convention that lands on the
 * guest's with sysv64 - identity on Unix, a depart stub on Windows.
 * argCount <= 6. Stubs live for the process; there are finitely many guest
 * entry points. Returns 0 on failure (see hostApi error / stub exhaustion). */
uintptr_t bridgeGuestCall(uintptr_t guestEntry, int argCount);

} // namespace chimera

#endif
