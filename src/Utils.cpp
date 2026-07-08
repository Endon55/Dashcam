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

const char* Utils::resolution_to_str(int width, int height)
{
    char* str = (char*)malloc(sizeof(char) * 10);
    snprintf(str, 10, "%dx%d", width, height);
    return str;
}


const char* Utils::double_to_str(double num)
{
    char* str = (char*)malloc(sizeof(char) * 9);
    snprintf(str, 9, "%f", num);
    return str;
}


std::map<const char* , std::map<double, std::vector<const capture_mode*>>> Utils::decompose_capture_modes(const capture_mode** modes, int nb_modes)
{
    std::map<const char* , std::map<double, std::vector<const capture_mode*>>> grouped_modes;

        for (int i = 0; i < nb_modes; i++)
        {
            const capture_mode* mode = modes[i];
            grouped_modes[mode->pixelFmtStr][mode->fps].push_back(mode);
        }
        return grouped_modes;
}
