

cmake_minimum_required(VERSION 3.10)

project(Dashcam)


find_package(fmt CONFIG REQUIRED)
find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_image REQUIRED CONFIG)


add_subdirectory(src)


