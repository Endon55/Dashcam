# Wrapper around vcpkg toolchain that selects a sane host triplet
# before vcpkg performs compiler detection.

if(NOT DEFINED VCPKG_ROOT OR VCPKG_ROOT STREQUAL "")
  if(DEFINED ENV{VCPKG_ROOT} AND NOT "$ENV{VCPKG_ROOT}" STREQUAL "")
    set(VCPKG_ROOT "$ENV{VCPKG_ROOT}")
  else()
    set(VCPKG_ROOT "$ENV{HOME}/vcpkg")
  endif()
endif()

set(_dashcam_vcpkg_toolchain "${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
if(NOT EXISTS "${_dashcam_vcpkg_toolchain}")
  message(FATAL_ERROR
    "vcpkg toolchain not found at '${_dashcam_vcpkg_toolchain}'. "
    "Set VCPKG_ROOT or run init-system.sh.")
endif()

set(_dashcam_host_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(_dashcam_host_processor STREQUAL "")
  execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE _dashcam_host_processor
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

if(_dashcam_host_processor MATCHES "^(x86_64|amd64)$")
  set(VCPKG_HOST_TRIPLET "x64-linux" CACHE STRING "Host vcpkg triplet" FORCE)
elseif(_dashcam_host_processor MATCHES "^(aarch64|arm64)$")
  set(VCPKG_HOST_TRIPLET "arm64-linux" CACHE STRING "Host vcpkg triplet" FORCE)
else()
  message(FATAL_ERROR
    "Unsupported host architecture '${_dashcam_host_processor}'. "
    "Set VCPKG_HOST_TRIPLET manually in your preset.")
endif()

if(DEFINED ENV{DASHCAM_VCPKG_TARGET_TRIPLET} AND NOT "$ENV{DASHCAM_VCPKG_TARGET_TRIPLET}" STREQUAL "")
  set(VCPKG_TARGET_TRIPLET "$ENV{DASHCAM_VCPKG_TARGET_TRIPLET}" CACHE STRING "Target vcpkg triplet" FORCE)
else()
  set(VCPKG_TARGET_TRIPLET "${VCPKG_HOST_TRIPLET}" CACHE STRING "Target vcpkg triplet" FORCE)
endif()

include("${_dashcam_vcpkg_toolchain}")
