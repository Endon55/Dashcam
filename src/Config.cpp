#include "Config.h"

int Config::load_cam_config()
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
int Config::save_cam_config(int nb_of_cameras, cam_config *configs)
{
    toml::table root;
    spdlog::debug("Cam Num: {}", nb_of_cameras);
    for (int i = 0; i < nb_of_cameras; i++)
    {
        toml::table cam_table;
        cam_config config = configs[i];
        cam_table.insert_or_assign("set_fps", toml::value{config.set_fps});

        cam_table.insert_or_assign("set_width", toml::value{config.set_width});
        cam_table.insert_or_assign("set_height", toml::value{config.set_height});
        cam_table.insert_or_assign("pix_format", toml::value{std::string_view{config.pix_format ? config.pix_format : "[none]"}});
        cam_table.insert_or_assign("manufacturer", toml::value{std::string_view{config.manufacturer ? config.manufacturer : "[none]"}});
        cam_table.insert_or_assign("product", toml::value{std::string_view{config.product ? config.product : "[none]"}});
        cam_table.insert_or_assign("vendorID", toml::value{std::string_view{config.vendorID ? config.vendorID : "[none]"}});
        cam_table.insert_or_assign("productID", toml::value{std::string_view{config.productID ? config.productID : "[none]"}});
        cam_table.insert_or_assign("serialNumber", toml::value{std::string_view{config.serialNumber ? config.serialNumber : "[none]"}});

        if (config.nb_cap_modes == NULL)
        {
            spdlog::warn("No valid capture modes");
            continue;
        }

        int cap_cout = *(config.nb_cap_modes);

        std::map<unsigned int, std::map<double, std::vector<capture_mode>>> grouped_modes;

         for (int j = 0; j < cap_cout; j++)
        {
            //toml::table formats;
            const capture_mode mode = config.cap_modes[j];
            grouped_modes[mode.pixelFormat][mode.fps].push_back(mode);

            //formats.insert_or_assign("fps", toml::value{mode.fps});
            //formats.insert_or_assign("width", toml::value{mode.width});
            //formats.insert_or_assign("height", toml::value{mode.height});

            //cam_table.insert_or_assign(std::to_string(mode.pixelFormat), std::move(formats));
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
                            [](capture_mode const &a, capture_mode const &b) -> bool
                            {
                                return (a.width * a.height) > (b.width * b.height);
                            });
                for (auto &mode : modes)
                {
                    modes_array.push_back(toml::table{
                        {"width", mode.width},
                        {"height", mode.height}});
                }
                toml::table fps_table;
                fps_table.insert_or_assign(std::to_string(fps), modes_array);
                fps_array.push_back(fps_table);
            }
            formats_table.insert_or_assign(fourcc_to_str(format), fps_array);
            formats_array.push_back(formats_table);
        }
        cam_table.insert_or_assign("Formats", formats_array);
        root.insert_or_assign(std::to_string(config.index), std::move(cam_table));
    }

    std::ofstream config;
    config.open(FileIO::getCamConfigFile());
    config << root;
    config.close();

    return 0;
}