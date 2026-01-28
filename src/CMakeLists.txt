

cmake_minimum_required(VERSION 3.10)

project(Dashcam)

add_subdirectory(Camera)
add_executable(Dashcam main.cpp)




target_link_libraries(Dashcam PRIVATE fmt::fmt SDL3::SDL3 SDL3_image::SDL3_image lib)
