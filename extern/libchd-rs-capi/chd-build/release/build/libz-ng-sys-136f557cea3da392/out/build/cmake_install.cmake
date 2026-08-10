# Install script for directory: /root/.cargo/registry/src/index.crates.io-1949cf8c6b5b557f/libz-ng-sys-1.1.15/src/zlib-ng

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

# Set default install directory permissions.
if(NOT DEFINED CMAKE_OBJDUMP)
  set(CMAKE_OBJDUMP "/usr/bin/objdump")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/libz-ng.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE RENAME "zlib-ng.h" FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/zlib-ng.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE RENAME "zlib_name_mangling-ng.h" FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/zlib_name_mangling-ng.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include" TYPE FILE RENAME "zconf-ng.h" FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/zconf-ng.h")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/zlib-ng.pc")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/libz-ng.a")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  include("/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/CMakeFiles/zlib.dir/install-cxx-module-bmi-Release.cmake" OPTIONAL)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/zlib-ng/zlib-ng.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/zlib-ng/zlib-ng.cmake"
         "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/CMakeFiles/Export/74a773ded784692c5e5de9162942044e/zlib-ng.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/zlib-ng/zlib-ng-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/zlib-ng/zlib-ng.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/zlib-ng" TYPE FILE FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/CMakeFiles/Export/74a773ded784692c5e5de9162942044e/zlib-ng.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/zlib-ng" TYPE FILE FILES "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/CMakeFiles/Export/74a773ded784692c5e5de9162942044e/zlib-ng-release.cmake")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/mnt/c/Users/sergiom/Documents/BizHawk/extern/libchd-rs-capi/chd-build/release/build/libz-ng-sys-136f557cea3da392/out/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
