

cmake_minimum_required(VERSION 3.10)

set(VCPKG_HOST_TRIPLET "x64-linux")
set(VCPKG_TARGET_TRIPLET "arm64-linux")

project(Dashcam)


find_package(fmt CONFIG REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)
find_package(libjpeg-turbo CONFIG REQUIRED)


add_subdirectory(src)


