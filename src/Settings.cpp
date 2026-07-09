#include "Settings.h"

#include "FileIO.h"
#include <fstream>
#include <toml++/toml.hpp>
#include <iostream>
#include <spdlog/spdlog.h>
#include "FileIO.h"


#ifndef SettingsState
settings state;
#endif

namespace
{
    int loadFromToml(toml::table toml)
    {
        state.mute = toml["mute"].value_or(false);
        state.save_dir = toml["save_dir"].value_or(FileIO::getHomeDir() + "/Desktop/Dashcam");
        toml::node_view flute_view = toml["flute"];

        return 0;
    }
}

std::filesystem::path Settings::getSettingsFilePath()
{
    return FileIO::getConfigDirPath() / "settings.toml";
}

int Settings::load()
{
    int ret = 0;

    std::filesystem::path settingsFile = FileIO::getSettingsFile();
    toml::table configToml;

    try
    {
        configToml = toml::parse_file(getSettingsFilePath().string());
    }
    catch (const toml::parse_error &e)
    {
        std::cerr << e.what() << '\n';
        spdlog::critical("Failed to parse Settings file.");
        return -1;
    }

    return loadFromToml(configToml);
}

int Settings::save()
{
    toml::table table;

    table.insert_or_assign("mute", state.mute);
    table.insert_or_assign("save_dir", state.save_dir);

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

std::filesystem::path Settings::getVideoSaveDir()
{
    return FileIO::get_or_create_folder(state.save_dir);
}
void Settings::setSaveDir(std::string saveDir)
{
    state.save_dir = saveDir;
    save();
}
