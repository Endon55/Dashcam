#pragma once

#include <filesystem>
#include <string>

struct settings
{
    bool mute = false;
    std::string save_dir = "";
};

namespace Settings
{
    int load();
    int save();
    bool isMuted();
    void setMute(bool mute);
    std::string getSaveDir();
    void setSaveDir(std::string saveDir);
    std::filesystem::path getSettingsFilePath();
}
