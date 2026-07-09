#pragma once

#include <filesystem>

namespace FileIO
{
    std::string getHomeDir();
    std::filesystem::path getConfigDirPath();
    
    std::filesystem::path getConfigFolder();
    std::filesystem::path getSettingsFile();
    std::filesystem::path getCamConfigFile();
    std::filesystem::path get_or_create_folder(std::filesystem::path folder);
    std::filesystem::path get_or_create_file(std::filesystem::path file);
}
