#include "Config.h"
#include <filesystem>

int Config::load_cam_config(AppState* state)
{
    std::filesystem::path configPath = FileIO::getCamConfigFile();

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

    toml::node_view entry = configToml["1"];

    return 0;
}
int Config::save_cam_config(int nb_of_cameras, cam_device *devices)
{
    toml::table root;
    spdlog::debug("Cam Num: {}", nb_of_cameras);
    for (int i = 0; i < nb_of_cameras; i++)
    {
        toml::table cam_table;
        cam_device device = devices[i];
        cam_table.insert_or_assign("set_fps", toml::value{device.default_mode->fps});

        cam_table.insert_or_assign("set_width", toml::value{device.default_mode->width});
        cam_table.insert_or_assign("set_height", toml::value{device.default_mode->height});
        cam_table.insert_or_assign("pix_format", toml::value{std::string_view{device.default_mode->pixelFmtStr ? device.default_mode->pixelFmtStr : "[none]"}});
        cam_table.insert_or_assign("manufacturer", toml::value{std::string_view{device.manufacturer ? device.manufacturer : "[none]"}});
        cam_table.insert_or_assign("product", toml::value{std::string_view{device.product ? device.product : "[none]"}});
        cam_table.insert_or_assign("vendorID", toml::value{std::string_view{device.vendorID ? device.vendorID : "[none]"}});
        cam_table.insert_or_assign("productID", toml::value{std::string_view{device.productID ? device.productID : "[none]"}});
        cam_table.insert_or_assign("serialNumber", toml::value{std::string_view{device.serialNumber ? device.serialNumber : "[none]"}});

        if (device.nb_cap_modes == NULL)
        {
            spdlog::warn("No valid capture modes");
            continue;
        }

        int cap_cout = *(device.nb_cap_modes);

        std::map<const char* , std::map<double, std::vector<const capture_mode*>>> grouped_modes;

        for (int j = 0; j < cap_cout; j++)
        {
            const capture_mode* mode = device.cap_modes[j];
            grouped_modes[mode->pixelFmtStr][mode->fps].push_back(mode);
        }
        toml::array formats_array;

        for (auto &[format, fps_map] : grouped_modes)
        {
            toml::array fps_array;

            toml::table formats_table;
            for (auto &[fps, modes] : fps_map)
            {
                toml::array modes_array;
                std::sort(modes.begin(), modes.end(),
                          [](const capture_mode* a, const capture_mode* b) -> bool
                          {
                              return (a->width * a->height) > (b->width * b->height);
                          });
                for (auto &mode : modes)
                {
                    modes_array.push_back(toml::table{
                        {"width", mode->width},
                        {"height", mode->height}});
                }
                toml::table fps_table;
                fps_table.insert_or_assign(std::to_string(fps), modes_array);
                fps_array.push_back(fps_table);
            }
            formats_table.insert_or_assign(format, fps_array);
            formats_array.push_back(formats_table);
        }
        cam_table.insert_or_assign("Formats", formats_array);
        root.insert_or_assign(std::to_string(i), std::move(cam_table));
    }

    std::ofstream config;
    config.open(FileIO::getCamConfigFile());
    config << root;
    config.close();

    return 0;
}

cam_config* Config::get_cam_config(AppState* state, cam_device* cam)
{
   if(state->configs == NULL)
   {
       spdlog::critical("Configs have not been loaded yet, load first then call this function again");
       return NULL;
   }
    return NULL;
}

