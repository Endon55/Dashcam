#include "UI.h"

static bool settings_open = false;
void createGUI(AppState *app_state)
{
   if (ImGui::BeginMainMenuBar())
   {
      if (ImGui::BeginMenu("File"))
      {
         if (ImGui::MenuItem("Settings"))
         {
            settings_open = true;
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

static capture_mode** setting_modes = (capture_mode**)malloc(sizeof(capture_mode*) * MAX_CAP_MODES);
static char** modes_raw = (char**)malloc(sizeof(char*) * MAX_CAP_MODES);
static int nb_of_modes = 0;
static int selected_index = 0;

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
        const bool is_selected = (selected_index == i);
        snprintf((char*)&cam_name, sizeof(cam_name), "Camera %d", i);
        ImGui::Text(cam_name);

        //create 3 dropdowns that filter left to right - Pixel Format -> FPS -> resolution
        //
        if(ImGui::BeginCombo("Format", fourcc_to_str(app_state->devices[i]->default_mode->pixelFormat)))
        {
            for(int j = 0; j < (*app_state->devices[i]->nb_cap_modes); j++)
            {
                  
            }


            for(int j = 0; j < nb_of_modes; j++)
            {
                if(ImGui::Selectable(fourcc_to_str(setting_modes[j]->pixelFormat)))
                {
                   selected_index = j; 
                }

            }
        }

    }
    ImGui::End();
}
