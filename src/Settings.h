#pragma once

#include <filesystem>
#include <fstream>
#include <string.h>
#include <map>
#include <toml++/toml.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include "FileIO.h"

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
