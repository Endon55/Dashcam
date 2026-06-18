#pragma once

#include <string>
#include <filesystem>

namespace Utils
{
    std::string getHomeDir()
    {
        return getenv("HOME");
    }

    std::filesystem::path getConfigDirPath()
    {
        return std::filesystem::path(getHomeDir()) / ".config" / "dashcam";
    }
}