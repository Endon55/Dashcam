

cmake_minimum_required(VERSION 3.10)

# Configure vcpkg triplets before project() so toolchain integration
# picks up the correct host/target values during configure.
set(_dashcam_host_processor "${CMAKE_HOST_SYSTEM_PROCESSOR}")
if(_dashcam_host_processor STREQUAL "")
  execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE _dashcam_host_processor
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

if(NOT DEFINED VCPKG_HOST_TRIPLET OR VCPKG_HOST_TRIPLET STREQUAL "")
  if(_dashcam_host_processor MATCHES "^(x86_64|amd64)$")
    set(_dashcam_host_triplet "x64-linux")
  elseif(_dashcam_host_processor MATCHES "^(aarch64|arm64)$")
    set(_dashcam_host_triplet "arm64-linux")
  else()
    message(FATAL_ERROR
      "Unsupported host architecture '${_dashcam_host_processor}'. "
      "Set VCPKG_HOST_TRIPLET and VCPKG_TARGET_TRIPLET in your preset.")
  endif()

  set(VCPKG_HOST_TRIPLET "${_dashcam_host_triplet}" CACHE STRING "Host vcpkg triplet")
endif()

if(NOT DEFINED VCPKG_TARGET_TRIPLET OR VCPKG_TARGET_TRIPLET STREQUAL "")
  set(VCPKG_TARGET_TRIPLET "${VCPKG_HOST_TRIPLET}" CACHE STRING "Target vcpkg triplet")
endif()

project(Dashcam LANGUAGES C CXX)


find_package(fmt CONFIG REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(ALSA REQUIRED)
find_package(GTest CONFIG REQUIRED)
# FFmpeg uses pkg-config
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavdevice
    libavformat
    libavfilter
    libavutil
    libswresample
    libswscale
    alsa
    x264
)

enable_testing()
include(GoogleTest)

add_subdirectory(src)
add_subdirectory(test)
