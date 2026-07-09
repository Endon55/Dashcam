#include "Config.h"
#include <filesystem>
#include <spdlog/spdlog.h>
#include <iostream>
#include "Camera/Camera.h"
#include "FileIO.h"
#include <fstream>
#include <string.h>

int Config::load_cam_configs(AppState* state)
{
    std::filesystem::path configPath = FileIO::getCamConfigFile();
    spdlog::info("Loading configs");

    toml::table configToml;

    try
    {
        configToml = toml::parse_file(FileIO::getCamConfigFile().string());
    }
    catch (const toml::parse_error &e)
    {
        std::cerr << e.what() << '\n';
        spdlog::critical("Failed to parse Cam Config file.");
        return -1;
    }
    int counter = 0;
    state->configs = (cam_config**)malloc(sizeof(cam_config*) * MAX_CAMS);

    while(true)
    {
        toml::table* entry = configToml[std::to_string(counter)].as_table();

        if(!entry)
        {
            spdlog::debug("{} configs loaded", counter);
            break;
        }
        cam_config* config = (cam_config*)malloc(sizeof(cam_config));
        
        config->manufacturer = str_or_null("manufacturer", *entry);
        config->product = str_or_null("product", *entry);
        config->productID= str_or_null("productID", *entry);
        config->serialNumber = str_or_null("serialNumber", *entry);
        config->vendorID = str_or_null("vendorID", *entry);
        toml::table* def_cap_mode = (*entry)["default_capture_mode"].as_table();
        toml::array* cap_modes = (*entry)["capture_modes"].as_array();

        config->default_mode = parse_capture_mode(*def_cap_mode);
        int nb_cap_modes = cap_modes->size();
        config->cap_modes = (const capture_mode**)malloc(sizeof(capture_mode*) * nb_cap_modes);
        for(int i = 0; i < nb_cap_modes; i++)
        {
            toml::table* cap_mode = (*cap_modes)[i].as_table();
            if(cap_mode)
            {

                config->cap_modes[i] = parse_capture_mode(*cap_mode);
            }
            else{
                spdlog::critical("error in parsing configs, some kind of size mismatch: i-{} total-{}", i, nb_cap_modes);
                break;
            }
        }
        config->nb_cap_modes = new int(nb_cap_modes);
        state->configs[counter] = config;
        counter++; 

    }
    state->nb_configs = counter;
    return 0;
}
int Config::save_cam_configs(AppState* state)
{
    toml::table root;
    spdlog::debug("Cam Num: {}", state->nb_cams);

    for (int i = 0; i < state->nb_cams; i++)
    {
        toml::table cam_table;
        cam_device* device = state->devices[i];
        toml::table def_cap_mode;
        def_cap_mode.insert_or_assign("width", toml::value{state->devices[i]->default_mode->width});
        def_cap_mode.insert_or_assign("fps", toml::value{state->devices[i]->default_mode->fps});
        def_cap_mode.insert_or_assign("height", toml::value{state->devices[i]->default_mode->height});
        def_cap_mode.insert_or_assign("format", toml::value{state->devices[i]->default_mode->pixelFormat});
        def_cap_mode.insert_or_assign("format-str", toml::value{state->devices[i]->default_mode->pixelFmtStr});
        cam_table.insert_or_assign("default_capture_mode", def_cap_mode);

        cam_table.insert_or_assign("manufacturer", toml::value{std::string_view{device->manufacturer ? device->manufacturer : "[none]"}});
        cam_table.insert_or_assign("product", toml::value{std::string_view{device->product ? device->product : "[none]"}});
        cam_table.insert_or_assign("vendorID", toml::value{std::string_view{device->vendorID ? device->vendorID : "[none]"}});
        cam_table.insert_or_assign("productID", toml::value{std::string_view{device->productID ? device->productID : "[none]"}});
        cam_table.insert_or_assign("serialNumber", toml::value{std::string_view{device->serialNumber ? device->serialNumber : "[none]"}});

        if (device->nb_cap_modes == NULL)
        {
            spdlog::warn("No valid capture modes");
            continue;
        }

        int cap_cout = *(device->nb_cap_modes);
        toml::array capture_modes_arr;

         for (int j = 0; j < cap_cout; j++)
         {
             toml::table cap_mode;
             cap_mode.insert_or_assign("width", toml::value{state->devices[i]->cap_modes[j]->width});
             cap_mode.insert_or_assign("height", toml::value{state->devices[i]->cap_modes[j]->height});
             cap_mode.insert_or_assign("format", toml::value{state->devices[i]->cap_modes[j]->pixelFormat});
             cap_mode.insert_or_assign("format-str", toml::value{state->devices[i]->cap_modes[j]->pixelFmtStr
                    });
             cap_mode.insert_or_assign("fps", toml::value{state->devices[i]->cap_modes[j]->fps});

             capture_modes_arr.push_back(cap_mode);
         }
         cam_table.insert_or_assign("capture_modes", capture_modes_arr);
         root.insert_or_assign(std::to_string(i), cam_table);
    } 
    std::ofstream config;
    config.open(FileIO::getCamConfigFile());
    config << root;
    config.close();

    return 0;
}

cam_config* Config::get_cam_config(AppState* state, cam_device* cam)
{
   if(state->configs == nullptr || cam == nullptr || cam->serialNumber == nullptr)
   {
       return nullptr;
   }

   for(int i = 0; i < state->nb_configs; i++)
   {
     //add support for checking vendorID, prodctID etc. 
     cam_config* cnfg = state->configs[i];

       if(state->configs[i]->serialNumber == nullptr)
       {
            continue;
       }
       else if(cam->serialNumber == state->configs[i]->serialNumber || 
               strcmp(cam->serialNumber, state->configs[i]->serialNumber) == 0)
       {
           return state->configs[i];
       }
   }
   return nullptr;
}
const char* Config::str_or_null(std::string key, toml::table table)
{
    std::optional<std::string> keyopt = table[key].value<std::string>();
    if(keyopt)
    {
        return strdup(keyopt.value().c_str());
    }
    return nullptr;
}
const capture_mode* Config::parse_capture_mode(toml::table table)
{
    capture_mode* mode = (capture_mode*)malloc(sizeof(capture_mode));
    mode->pixelFmtStr = str_or_null("format-str", table);
    mode->width = table["width"].value_or(-1);
    mode->height = table["height"].value_or(-1);
    mode->pixelFormat = table["format"].value_or(0u);
    mode->fps = table["fps"].value_or(0.0);
    return mode;
}
