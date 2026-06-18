#pragma once

#include <filesystem>

struct settings
{
    bool mute = false;
    
};

namespace Settings
{
    int load();
    int save();
    bool isMuted();
    void setMute(bool mute);
    std::filesystem::path getSettingsFilePath();
}
