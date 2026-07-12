#pragma once
#include <toml++/toml.hpp>
#include "State.h"
#include "Camera/Camera.h"


namespace Config
{
    const char* str_or_null(std::string key, toml::table table);
    const capture_mode* parse_capture_mode(toml::table table);
    int load_cam_configs(AppState* state);
    int save_cam_configs(AppState* state);
    int get_cam_config(AppState* app_state, cam_device* cam);
}
