#include "UI.h"
#include <format>
#include <string>
#include <imgui_internal.h>

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
   }
   if (settings_open)
   {
       createSettingsMenu(app_state);
   }
   for (int i = 0; i < app_state->camera_count; i++)
   {
       snprintf((char*)&cam_name, sizeof(cam_name), "Camera %d", i);
      ImGui::Begin(cam_name);

      ImVec2 pos = ImGui::GetWindowPos();
      ImVec2 size = ImGui::GetWindowSize();

      ImGui::Text("window pos %.1f %.1f", pos.x, pos.y);
      ImGui::Text("window size %.1f %.1f", size.x, size.y);

      ImVec2 avail = ImGui::GetContentRegionAvail();
      if (avail.x > 1.0f && avail.y > 1.0f && app_state != NULL && app_state->cam_textures[i] != NULL)
      {
         float videoAspect = static_cast<float>(app_state->width) / static_cast<float>(app_state->height);
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
         ImGui::Image((ImTextureID)app_state->cam_textures[i], imageSize);
      }

      ImGui::End();
   }
}

const capture_mode* new_mode;
static vector<const capture_mode*> selected_modes(MAX_CAMS);
bool create_dropdown_entry(cam_device* device, int index)
{
    ImGui::PushID(index);
    if(selected_modes[index] == NULL)
    {
        selected_modes[index] = device->default_mode;
    }
    const bool is_selected = false; 
    ImGui::Text("Camera %d", index);


    std::map<const char* , std::map<double, std::vector<const capture_mode*>>> sorted_modes = Utils::decompose_capture_modes(device->cap_modes, *device->nb_cap_modes); 
    
    int i = 0;
    if(ImGui::BeginCombo("Format", selected_modes[index]->pixelFmtStr))
    {
        for(auto &[format, fps_map] : sorted_modes)
        {
            ImGui::PushID(i);
            if(ImGui::Selectable(format, is_selected))
            {
               new_mode = fps_map.begin()->second[0];
               if(new_mode != selected_modes[index])
               {
                        selected_modes[index] = new_mode;
                        settings_modified = true;
               }
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
    i = 0;
    if(ImGui::BeginCombo("FPS", Utils::double_to_str(selected_modes[index]->fps)))
    {
        
        for(auto &[fps, resolutions] : sorted_modes.at(selected_modes[index]->pixelFmtStr))
        {
            ImGui::PushID(i);
            if(ImGui::Selectable(Utils::double_to_str(fps)), is_selected)
            {
                new_mode = resolutions[0];
                if(new_mode != selected_modes[index])
                {
                    selected_modes[index] = new_mode;
                    settings_modified = true;
                }
            }
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }
    if(ImGui::BeginCombo("Resolution", Utils::resolution_to_str(selected_modes[index]->width, selected_modes[index]->height)))
    {
        vector<const capture_mode*> vec = sorted_modes.at(selected_modes[index]->pixelFmtStr).at(selected_modes[index]->fps);

        for(int j = 0; j < vec.size(); j++)         {
            ImGui::PushID(j);
            if(ImGui::Selectable(Utils::resolution_to_str(vec[j]->width, vec[j]->height), is_selected))
            {
                new_mode = vec[j];
                if(new_mode != selected_modes[index])
                {
                    selected_modes[index] = new_mode;
                    settings_modified = true;
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
    for(int i = 0; i < app_state->camera_count; i++)
    {  
        if(create_dropdown_entry(app_state->devices[i], i))
        {
            
        }
    }

    
    if(settings_modified || cam_config_modified)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    if(ImGui::Button("Save"))
    {
        if(settings_modified)
        {
            //save settigns file
        }
        if(cam_config_modified)
        {
            for(int i = 0; i < app_state->camera_count; i++)
            {
                app_state->devices[i]->default_mode = selected_modes[i];
            }
            //save cam_config file
        }

        settings_open = false;
    }
    if(settings_modified || cam_config_modified)
    {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }
    if(ImGui::Button("Close"))
    {
        
    }
    ImGui::End();
}

