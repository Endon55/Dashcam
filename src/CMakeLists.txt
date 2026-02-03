

cmake_minimum_required(VERSION 3.10)

project(Dashcam)

add_subdirectory(Camera)
add_executable(Dashcam main.cpp)

set(sources 
Window.h
Window.cpp)

add_library(source ${sources})
target_link_libraries(source PRIVATE libjpeg-turbo::turbojpeg-static SDL3::SDL3)

target_link_libraries(Dashcam PRIVATE fmt::fmt SDL3::SDL3 SDL3_image::SDL3_image libjpeg-turbo::turbojpeg-static camera source)
