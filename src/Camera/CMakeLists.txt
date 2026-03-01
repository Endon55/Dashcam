

set(sources 
Camera.h
Camera.cpp
Webcam.h
Webcam.cpp)

add_library(camera ${sources})
target_link_libraries(camera PRIVATE PkgConfig::FFMPEG spdlog::spdlog)