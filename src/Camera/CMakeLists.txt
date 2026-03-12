

set(sources 
Camera.h
Camera.cpp
Webcam.h
Webcam.cpp
Muxor.h
Muxor.cpp
WebcamUtils.h
WebcamUtils.cpp
Macros.h)

add_library(camera ${sources})
target_link_libraries(camera PRIVATE PkgConfig::FFMPEG spdlog::spdlog)