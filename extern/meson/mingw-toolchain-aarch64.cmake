# CMake toolchain for the cmake-driven native deps (SDL2, openal-soft) when
# cross-building Windows on arm64 artifacts from Linux with llvm-mingw, whose
# bin/ folder is on PATH. Mirrors extern/meson/mingw-w64-aarch64.ini.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

set(CMAKE_C_COMPILER aarch64-w64-mingw32-clang)
set(CMAKE_CXX_COMPILER aarch64-w64-mingw32-clang++)
set(CMAKE_RC_COMPILER aarch64-w64-mingw32-windres)

# the target's headers and libraries sit beside llvm-mingw's bin/
find_program(_llvm_mingw_cc aarch64-w64-mingw32-clang REQUIRED)
get_filename_component(_llvm_mingw_bin "${_llvm_mingw_cc}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${_llvm_mingw_bin}/../aarch64-w64-mingw32")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# openal-soft's bundled fmt 11.1.1 uses malloc and free without <cstdlib>,
# which libc++ does not bring in by itself (GCC's libstdc++ does)
set(CMAKE_CXX_FLAGS_INIT "-include cstdlib")

# static runtime: produced dlls must not depend on libc++/libunwind dlls
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")
