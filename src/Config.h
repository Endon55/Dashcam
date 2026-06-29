#pragma once

#include "FileIO.h"
#include "Camera/WebcamUtils.h"
#include <fstream>
#include <toml++/toml.hpp>
#include <iostream>
#include <algorithm>

struct cam_config
{
    int index;
    int set_fps;
    int set_width;
    int set_height;
    const char *pix_format;
    bool enabled;

    const char *manufacturer;
    const char *product;
    const char *vendorID;
    const char *productID;
    const char *serialNumber;

    const struct capture_mode *cap_modes;
    const int *nb_cap_modes;
};

namespace Config
{
    int load_cam_config();
    int save_cam_config(int nb_of_cameras, cam_config* configs);
    cam_config get_cam_config(char* serialNumber);
}