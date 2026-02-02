set(VCPKG_TARGET_ARCHITECTURE arm64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CMAKE_SYSTEM_PROCESSOR aarch64)

# ARM cross-compiler
set(VCPKG_C_COMPILER /usr/bin/aarch64-linux-gnu-gcc)
set(VCPKG_CXX_COMPILER /usr/bin/aarch64-linux-gnu-g++)

# Compiler flags to target older glibc compatibility
set(VCPKG_C_FLAGS "-march=armv8-a -mtune=generic -Wl,--hash-style=gnu")
set(VCPKG_CXX_FLAGS "-march=armv8-a -mtune=generic -Wl,--hash-style=gnu")

# DBus cross-compilation setup
set(VCPKG_CMAKE_CONFIGURE_OPTIONS
    -DDBUS_SESSION_SOCKET_DIR=/tmp
)
