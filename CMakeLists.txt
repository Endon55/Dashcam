

cmake_minimum_required(VERSION 3.10)

set(VCPKG_HOST_TRIPLET "x64-linux")
# VCPKG_TARGET_TRIPLET is set via CMakePresets.json
# Default to x64-linux if not specified in preset
if(NOT VCPKG_TARGET_TRIPLET)
  set(VCPKG_TARGET_TRIPLET "x64-linux")
endif()

project(Dashcam)


find_package(fmt CONFIG REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(libjpeg-turbo CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

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
)

add_subdirectory(src)


