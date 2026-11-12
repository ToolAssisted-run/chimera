cmake_minimum_required(VERSION 3.13 FATAL_ERROR)

project(SDL3-Chimera C CXX)

# SDL 3 built for Chimera's window, which takes only gamepads and joysticks
# from it. The defaults are "on where the system has it", so only what must
# differ is said here.

# Only the joystick and gamepad parts: SDL 3's joysticks need no video, and
# without it SDL needs none of the X11 or Wayland libraries (SDL 3 refuses
# to build on Linux when one it was asked to use is missing, where SDL 2 left
# it out)
set(SDL_AUDIO OFF)
set(SDL_VIDEO OFF)
set(SDL_RENDER OFF)
set(SDL_GPU OFF)
set(SDL_CAMERA OFF)
set(SDL_JOYSTICK ON) # the gamepads and joysticks Chimera reads
set(SDL_HAPTIC OFF)
set(SDL_HIDAPI ON) # more joysticks, and the virtual joystick the tests use
set(SDL_POWER OFF)
set(SDL_SENSOR OFF)
set(SDL_DIALOG OFF)
set(SDL_TRAY OFF)
set(SDL_LIBURING OFF)
# no windows, so SDL's check that it can make them on Linux is not wanted
set(SDL_UNIX_CONSOLE_BUILD ON)

# We're just building a shared library, so
set(SDL_INSTALL OFF)
set(SDL_UNINSTALL OFF)
set(SDL_TEST_LIBRARY OFF)
set(SDL_TESTS OFF)
set(SDL_EXAMPLES OFF)

set(SDL_LIBC ON)

# This is only used for IME stuff, we don't use that
set(SDL_IBUS OFF)

# We handle ld path finding with LD_LIBRARY_PATH for Linux
set(SDL_RPATH OFF)

# libusb gives us support for even more joysticks (notably, the official GC Adapter!)
# Elsewhere SDL opens the system's libusb at run time when it was found at build time.
set(SDL_HIDAPI_LIBUSB ON)

# Windows has no libusb of its own: Chimera builds libusb from extern/SDL3/libusb
# and links it into SDL3.dll. SDL's search for libusb (cmake/FindLibUSB.cmake)
# is given that library and finds it.
if(WIN32)
	add_library(usb STATIC EXCLUDE_FROM_ALL
		libusb/libusb/core.c
		libusb/libusb/descriptor.c
		libusb/libusb/hotplug.c
		libusb/libusb/io.c
		libusb/libusb/strerror.c
		libusb/libusb/sync.c
		libusb/libusb/os/threads_windows.c
		libusb/libusb/os/windows_common.c
		libusb/libusb/os/windows_usbdk.c
		libusb/libusb/os/windows_winusb.c
		libusb/libusb/os/events_windows.c
	)

	# Silence warnings with clang-cl
	if(CMAKE_C_COMPILER_ID MATCHES "Clang")
		target_compile_options(usb PRIVATE -w)
	endif()

	set_target_properties(usb PROPERTIES VERSION 1.0.29)
	if(MSVC)
		target_include_directories(usb BEFORE
			PUBLIC libusb/libusb
			PRIVATE libusb/msvc
		)
	else()
		# mingw-w64 cross builds: libusb's msvc/config.h is MSVC-only, use ours
		target_include_directories(usb BEFORE
			PUBLIC libusb/libusb
			PRIVATE mingw
		)
	endif()

	add_library(LibUSB::LibUSB ALIAS usb)
	set(SDL_HIDAPI_LIBUSB_SHARED OFF)
	set(LibUSB_LIBRARY usb CACHE STRING "" FORCE)
	set(LibUSB_INCLUDE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/libusb/libusb" CACHE PATH "" FORCE)
	# SDL's check links a test program against libusb, which cannot use a
	# library not built yet; libusb.h is known to have what SDL needs
	set(HAVE_LIBUSB_H TRUE CACHE INTERNAL "")
endif()

# Mark this as a Chimera build
set(SDL_VENDOR_INFO "Chimera" CACHE STRING "" FORCE)

# We only want a shared library
set(SDL_SHARED ON)
set(SDL_STATIC OFF)

# We need the newer behavior for our changes to go through
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

# Silence a warning when specifying INTERPROCEDURAL_OPTIMIZATION
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)

add_subdirectory(SDL)

# Ensure we have SDL3.dll / libSDL3.so
set_target_properties(SDL3-shared PROPERTIES
	NO_SONAME ON
	OUTPUT_NAME "SDL3"
	DEBUG_POSTFIX ""
)

# Strip release builds
target_link_options(SDL3-shared PRIVATE $<$<CONFIG:Release>:-s>)
