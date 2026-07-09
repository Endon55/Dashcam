#pragma once

#include <filesystem>
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
    std::filesystem::path getVideoSaveDir();
    void setSaveDir(std::string saveDir);
    std::filesystem::path getSettingsFilePath();
}
