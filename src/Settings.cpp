#include "Settings.h"
#include "Utils.h"
#include <filesystem>
#include <fstream>
#include <string.h>
#include <map>
#include <toml++/toml.hpp>
#include <iostream>


#include <spdlog/spdlog.h>

#ifndef SettingsState
settings state;
#endif

namespace
{
    int loadFromToml(toml::table toml)
    {
        state.mute = toml["mute"].value_or(false);
        state.save_dir = toml["save_dir"].value_or(Utils::getHomeDir() + "/Desktop/Dashcam");

        toml::node_view flute_view = toml["flute"];

        return 0;
    }
}

std::filesystem::path Settings::getSettingsFilePath()
{
    return Utils::getConfigDirPath() / "settings.toml";
}

int Settings::load()
{
    int ret = 0;

    std::string homedir = Utils::getHomeDir();

    std::filesystem::path path = Utils::getConfigDirPath();

    if(!std::filesystem::exists(path))
    {
        if(!std::filesystem::create_directory(path))
        {

            ret = -1;
            spdlog::critical("Failed to create the dashcam directory");

            return ret;
        }
    }

    path = getSettingsFilePath();


    if (!std::filesystem::exists(path))
    {
        std::ofstream{path}.close();
    }

    std::ifstream config;
    config.open(getSettingsFilePath());
    if(!config.is_open())
    {
        spdlog::critical("Failed to open settings file");
        return -1;
    }
    

    toml::table configToml = toml::parse_file(getSettingsFilePath().string());

    try
    {
        configToml = toml::parse_file(getSettingsFilePath().string());
    }
    catch(const toml::parse_error& e)
    {
        std::cerr << e.what() << '\n';
        spdlog::critical("Failed to parse Settings file.");
        return -1;
    }
    spdlog::debug("Config Toml");
    std::cout << configToml << std::endl;

    return loadFromToml(configToml);
}

int Settings::save()
{
    toml::table table;

    table.insert_or_assign("mute", state.mute);
    table.insert_or_assign("save_dir", state.save_dir);

    std::cout << table << std::endl;

    std::ofstream config;
    config.open(getSettingsFilePath());
    config << table;
    config.close();

    return 0;
}

bool Settings::isMuted()
{
    return state.mute;
}
void Settings::setMute(bool mute)
{
    state.mute = mute;
    save();
}

std::string Settings::getSaveDir()
{
    return state.save_dir;
}
void Settings::setSaveDir(std::string saveDir)
{
    state.save_dir = saveDir;
    save();
}