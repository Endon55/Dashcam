#include "Utils.h"

bool Utils::str_starts_with(const char* base_string, const char* condition_string)
{
    const char * base = base_string;
    const char * cond = condition_string;

    while (*base != '\0')
    {
        if (*cond == '\0')
        {
            return true;
        }

        if(*base != *cond)
        {
            return false;
        }
        *base++;
        *cond++;
    }
    return false;
}

const char* Utils::str_or_default(const char* str, const char* def)
{
    return (str != NULL ? str : def);
}


void Utils::log_cap_mode(const capture_mode* mode)
{
    spdlog::debug("Format: {}", mode->pixelFmtStr);
    spdlog::debug("FPS: {}", mode->fps);
    spdlog::debug("Resolution: {}x{}", mode->width, mode->height);
}
