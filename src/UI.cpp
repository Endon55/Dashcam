#include "UI.h"
#include <imgui.h>
#include <string>
#include <imgui_internal.h>
#include "Camera/Camera.h"
#include "Config.h"
#include "Settings.h"
#include "State.h"
#include "Utils.h"
#include "math.h"

static bool settings_open = false;
static bool settings_modified = false;
static bool cam_config_modified = false;
void createGUI(AppState *app_state)
{     
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Settings"))
            {
                settings_open = true;
                settings_modified = false;
                cam_config_modified = false;
            }
            ImGui::EndMenu();
        }
        if (ImGui::Checkbox("Mute", &mute))
        {
            app_state->webcams[0]->muteAudioPlayback(mute);
            Settings::setMute(mute);
        }
        ImGui::EndMainMenuBar();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 pos = viewport->Pos;
        pos.y += ImGui::GetFrameHeight();
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowPos(pos);
        if(ImGui::Begin("Camera Container"))
        {
            create_cam_windows(app_state);
        }
        ImGui::End();

    }

    if (settings_open)
    {
        createSettingsMenu(app_state);
    }
   
}

static vec2i cam_layout = {0,0};
static vec2 layout_size = {0.0, 0.0};
void set_window_bounding(AppState *app_state, int index)
{
    ImVec2 size = ImGui::GetContentRegionAvail();
    layout_size.y = app_state->window_size.y / (float)cam_layout.y;
    layout_size.x = app_state->window_size.x  / (float)cam_layout.x;
    //First row will by definition always have the max number of cams
        ImGui::SetNextWindowPos(ImVec2((index % cam_layout.x) * layout_size.x, floor(index % cam_layout.y) * layout_size.y));
    ImGui::SetNextWindowSize(ImVec2(layout_size.x, layout_size.y));
}

void recalculate_windows(int nb_cams)
{
    spdlog::debug("Recalculating camera window positions");
    if(nb_cams <= 2)
    {
        cam_layout.y = 1;
        cam_layout.x = nb_cams;

        cam_number = nb_cams;
        return;
    }

    float cols = ceil(sqrt((float) nb_cams));
    cam_layout.y = (int) cols;
    cam_layout.x = cam_layout.y;
}

bool create_cam_windows(AppState *state)
{
    if(cam_number != state->nb_cams)
    {
       recalculate_windows(state->nb_cams); 
    }
    
    for (int i = 0; i < state->nb_cams; i++)
    {
        set_window_bounding(state, i);
        snprintf((char *)&cam_name, sizeof(cam_name), "Camera %d", i);
        if(ImGui::BeginChild(cam_name, ImVec2(0,0), ImGuiChildFlags_Border, ImGuiWindowFlags_MenuBar))
        {

            if(ImGui::BeginMenuBar())
            {

                if(ImGui::BeginMenu("File"))
                {
                    if(ImGui::MenuItem("Test"))
                    {

                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenuBar();
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (avail.x > 1.0f && avail.y > 1.0f && state != nullptr && state->cam_textures[i] != NULL)
            {
                float videoAspect = static_cast<float>(layout_size.x) / static_cast<float>(layout_size.y);
                float availAspect = avail.x / avail.y;

                ImVec2 imageSize = avail;
                if (availAspect > videoAspect)
                {
                    imageSize.x = avail.y * videoAspect;
                }
                else
                {
                    imageSize.y = avail.x / videoAspect;
                }

                float xOffset = (avail.x - imageSize.x) * 0.5f;
                float yOffset = (avail.y - imageSize.y) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + xOffset);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + yOffset);
                ImGui::Image((ImTextureID)state->cam_textures[i], imageSize);
            }

        }
        ImGui::EndChild();
    }
    return true;
}


const capture_mode *new_mode;
static vector<const capture_mode *> selected_modes(MAX_CAMS);
static vector<const capture_mode *> original_modes(MAX_CAMS);
bool create_dropdown_entry(cam_device *device, int index)
{
    ImGui::PushID(index);
    if (selected_modes[index] == nullptr)
    {
        selected_modes[index] = device->default_mode;
        original_modes[index] = device->default_mode;
    }
    static bool is_selected = false;
    ImGui::Text("Camera %d", index);

    std::map<const char *, std::map<double, std::vector<const capture_mode *>>> sorted_modes = Utils::decompose_capture_modes(device->cap_modes, *device->nb_cap_modes);

    int i = 0;
    if (ImGui::BeginCombo("Format", selected_modes[index]->pixelFmtStr))
    {
        for (auto &[format, fps_map] : sorted_modes)
        {
            ImGui::PushID(i++);
            is_selected = (format == selected_modes[index]->pixelFmtStr);
            if (ImGui::Selectable(format, is_selected))
            {
                if (!is_selected)
                {
                    if (format == original_modes[index]->pixelFmtStr)
                    {
                        selected_modes[index] = original_modes[index];
                        cam_config_modified = false;
                    }
                    else
                    {
                        selected_modes[index] = fps_map.begin()->second[0];
                        cam_config_modified = true;
                    }
                }
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("FPS", Utils::double_to_str(selected_modes[index]->fps)))
    {
        for (auto &[fps, resolutions] : sorted_modes.at(selected_modes[index]->pixelFmtStr))
        {
            ImGui::PushID(i++);
            is_selected = (fps == selected_modes[index]->fps);
            if (ImGui::Selectable(Utils::double_to_str(fps), is_selected))
            {
                if (!is_selected)
                {
                    // double check the format is still the same
                    if (selected_modes[index]->pixelFmtStr == original_modes[index]->pixelFmtStr &&
                        fps == original_modes[index]->fps)
                    {
                        selected_modes[index] = original_modes[index];
                        cam_config_modified = false;
                    }
                    else
                    {
                        selected_modes[index] = resolutions[0];
                        cam_config_modified = true;
                    }
                }
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
    if (ImGui::BeginCombo("Resolution", Utils::resolution_to_str(selected_modes[index]->width, selected_modes[index]->height)))
    {
        vector<const capture_mode *> vec = sorted_modes.at(selected_modes[index]->pixelFmtStr).at(selected_modes[index]->fps);

        for (int j = 0; j < vec.size(); j++)
        {
            ImGui::PushID(i++);
            is_selected = (vec[j]->width == selected_modes[index]->width && vec[j]->height == selected_modes[index]->height);
            if (ImGui::Selectable(Utils::resolution_to_str(vec[j]->width, vec[j]->height), is_selected))
            {
                if (!is_selected)
                {
                    // double check the format is still the same
                    if (selected_modes[index]->pixelFmtStr == original_modes[index]->pixelFmtStr &&
                        selected_modes[index]->fps == original_modes[index]->fps &&
                        vec[j]->width == original_modes[index]->width && vec[j]->height == original_modes[index]->height)
                    {

                        selected_modes[index] = original_modes[index];
                        cam_config_modified = false;
                    }
                    else
                    {
                        selected_modes[index] = vec[j];
                        cam_config_modified = true;
                    }
                }
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    ImGui::PopID();
    return true;
}
void createSettingsMenu(AppState *app_state)
{

    ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings", &settings_open, 0);
    ImGui::Text("Save Directory");
    if (ImGui::InputText("##Save Directory", save_dir, sizeof(save_dir)))
    {
        Settings::setSaveDir(std::string(save_dir));
    }
    for (int i = 0; i < app_state->nb_cams; i++)
    {
        if (create_dropdown_entry(app_state->devices[i], i))
        {
        }
    }

    bool pushed = false;
    if (!settings_modified && !cam_config_modified)
    {
        pushed = true;
        ImGui::Text("   "); 
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    else ImGui::Text("Changes will take effect on app restart");

    if (ImGui::Button("Save"))
    {
        if (settings_modified)
        {
            int ret = Settings::save(); 
            if(ret < 0)
            {
                spdlog::critical("Failed to save settings");
            }
        }
        if (cam_config_modified)
        {
            for (int i = 0; i < app_state->nb_cams; i++)
            {
                app_state->devices[i]->default_mode = selected_modes[i];
            }
            int ret = Config::save_cam_configs(app_state);
            if(ret < 0)
            {
                spdlog::critical("Failed to save cam configuration");
            }
            // save cam_config file
        }
        settings_modified = false;
        cam_config_modified = false;
        settings_open = false;
    }
    if (pushed)
    {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();

        pushed = false;
    }
    if (ImGui::Button("Close"))
    {
    }
    ImGui::End();
}



