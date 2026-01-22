

cmake_minimum_required(VERSION 3.10)

project(Dashcam)

find_package(fmt CONFIG REQUIRED)

add_executable(Dashcam src/dashcam.cpp)

target_link_libraries(Dashcam PRIVATE fmt::fmt)
