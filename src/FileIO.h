#pragma once

#include <filesystem>
#include <fstream>
#include <string.h>
#include <spdlog/spdlog.h>


namespace FileIO
{
    std::string getHomeDir();
    std::filesystem::path getConfigDirPath();
    
    std::filesystem::path getConfigFolder();
    std::filesystem::path getSettingsFile();
    std::filesystem::path getCamConfigFile();

}