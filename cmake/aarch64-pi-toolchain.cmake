# CMake toolchain for cross-compiling to Raspberry Pi 5 (aarch64) using a Pi sysroot
# Usage: pass -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-pi-toolchain.cmake
# and -DCMAKE_SYSROOT=/path/to/pi-sysroot

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

if(NOT DEFINED CMAKE_SYSROOT)
  set(CMAKE_SYSROOT "${USER}/pi-sysroot" CACHE PATH "Path to Raspberry Pi sysroot")
endif()

set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

# Ensure the compiler/linker uses the sysroot
set(CMAKE_SYSROOT ${CMAKE_SYSROOT})
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} --sysroot=${CMAKE_SYSROOT}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --sysroot=${CMAKE_SYSROOT}")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --sysroot=${CMAKE_SYSROOT}")

# Search in the sysroot for libraries and headers
set(CMAKE_FIND_ROOT_PATH ${CMAKE_SYSROOT})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Make pkg-config use the sysroot's pkgconfig directories
set(ENV{PKG_CONFIG_DIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")
set(ENV{PKG_CONFIG_LIBDIR} "${CMAKE_SYSROOT}/usr/lib/pkgconfig:${CMAKE_SYSROOT}/usr/share/pkgconfig")

# Helpful hint: If you use vcpkg, set CMAKE_PREFIX_PATH to point into your
# cross-installed vcpkg tree or use vcpkg toolchain integration for cross builds.
