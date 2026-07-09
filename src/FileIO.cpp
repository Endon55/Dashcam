#include "FileIO.h"
#include <fstream>
#include <spdlog/spdlog.h>


std::filesystem::path FileIO::get_or_create_folder(std::filesystem::path folder)
{
    if (!std::filesystem::exists(folder))
    {
        if (!std::filesystem::create_directory(folder))
        {
            spdlog::critical("Failed to create the folder: {}", folder.string());
        }
    }
    return folder;
}

std::filesystem::path FileIO::get_or_create_file(std::filesystem::path file)
{
    if (!std::filesystem::exists(file))
    {
        std::ofstream fileStream;
        fileStream.open(file);
        if (!fileStream.is_open())
        {
            spdlog::critical("Failed to create the file: {}", file.string());
        }
        else
        {
            fileStream.close();
        }
    }
    return file;
}

std::string FileIO::getHomeDir()
{
    return getenv("HOME");
}

std::filesystem::path FileIO::getConfigFolder()
{
    return get_or_create_folder(getConfigDirPath());
}

std::filesystem::path FileIO::getConfigDirPath()
{
    return std::filesystem::path(getHomeDir()) / ".config" / "dashcam";
}

std::filesystem::path FileIO::getSettingsFile()
{
    return get_or_create_file(getConfigDirPath() / "settings.toml");
}

std::filesystem::path FileIO::getCamConfigFile()
{
    return get_or_create_file(getConfigDirPath() / "camconfig.toml");
}
