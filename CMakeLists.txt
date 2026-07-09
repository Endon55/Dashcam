

cmake_minimum_required(VERSION 3.10)

# Configure vcpkg triplets before project() so toolchain integration
# picks up the correct host/target values during configure.
if(NOT DEFINED VCPKG_HOST_TRIPLET OR VCPKG_HOST_TRIPLET STREQUAL "")
  if(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
    set(_dashcam_host_triplet "x64-linux")
  elseif(CMAKE_HOST_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(_dashcam_host_triplet "arm64-linux")
  else()
    message(FATAL_ERROR
      "Unsupported host architecture '${CMAKE_HOST_SYSTEM_PROCESSOR}'. "
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
)

add_subdirectory(src)


