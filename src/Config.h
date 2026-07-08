#pragma once

#include "FileIO.h"
#include <fstream>
#include <toml++/toml.hpp>
#include <iostream>
#include <algorithm>
#include "State.h"
#include "Camera/Camera.h"
namespace Config
{
    int load_cam_config(AppState* state);
    int save_cam_config(int nb_of_cameras, cam_device* devices);
    cam_config* get_cam_config(AppState* app_state, cam_device* cam);
}
