

cmake_minimum_required(VERSION 3.10)

project(Dashcam)

add_executable(Dashcam main.cpp)
add_subdirectory(Camera)

set(sources 
Window.h
Window.cpp
Settings.h
Settings.cpp
Config.h
Config.cpp
FileIO.h
FileIO.cpp
Utils.h
Utils.cpp
State.h
UI.h
UI.cpp
memory.h
memory.cpp
)


add_library(source ${sources})
target_include_directories(source PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(source PRIVATE libjpeg-turbo::turbojpeg-static SDL3::SDL3 imgui::imgui)
target_link_libraries(source PUBLIC spdlog::spdlog)
target_link_libraries(source PUBLIC udev)

target_link_libraries(Dashcam PUBLIC udev)

target_include_directories(Dashcam PRIVATE ${ALSA_INCLUDE_DIR})
target_link_libraries(Dashcam PRIVATE 
fmt::fmt 
spdlog::spdlog
SDL3::SDL3 
SDL3_image::SDL3_image 
ALSA::ALSA
libjpeg-turbo::turbojpeg-static 
PkgConfig::FFMPEG
imgui::imgui
tomlplusplus::tomlplusplus
GTest::gtest
camera 
source
)

