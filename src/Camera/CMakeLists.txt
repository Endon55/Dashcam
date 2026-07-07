

set(sources 
Webcam.h
Webcam.cpp
Muxor.h
Muxor.cpp
WebcamUtils.h
WebcamUtils.cpp
Camera.h
Macros.h)

add_library(camera ${sources})
target_link_libraries(camera PRIVATE PkgConfig::FFMPEG spdlog::spdlog ALSA::ALSA)
