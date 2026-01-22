

cmake_minimum_required(VERSION 3.10)

project(Dashcam)

find_package(fmt CONFIG REQUIRED)


add_subdirectory(Camera)
add_executable(Dashcam main.cpp)



target_link_libraries(Dashcam PRIVATE fmt::fmt lib)
