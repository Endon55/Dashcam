#pragma once

#include <spdlog/spdlog.h>
#include "Camera/Camera.h"

namespace Utils
{
    bool str_starts_with(const char *base_string, const char *condition_string);
    const char* str_or_default(const char* str, const char* def);
    void log_cap_mode(const capture_mode* mode);
}
