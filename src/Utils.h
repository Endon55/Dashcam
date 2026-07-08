#pragma once

#include <spdlog/spdlog.h>
#include "Camera/Camera.h"
#include <map>
#include <vector>
namespace Utils
{
    bool str_starts_with(const char *base_string, const char *condition_string);
    const char* str_or_default(const char* str, const char* def);
    void log_cap_mode(const capture_mode* mode);

    const char* resolution_to_str(int width, int height);
    const char* double_to_str(double num);
        std::map<const char* , std::map<double, std::vector<const capture_mode*>>> decompose_capture_modes(const capture_mode** modes, int nb_modes);
}
