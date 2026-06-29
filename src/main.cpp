#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <string_view>
#include <inttypes.h>
#include <iostream>
#include <vector>

#include <linux/videodev2.h>
#include <libudev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <spdlog/spdlog.h>

#include "Camera/Webcam.h"
#include "Camera/Muxor.h"
#include "Camera/WebcamUtils.h"
#include "Window.h"
#include "Settings.h"
#include "Utils.h"
#include "Config.h"



int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext);
void getCardAndDevice(const char *pcm_string, int *card, int *device);
void createGUI();
int getUsbIndex(const char *usbPath, cam_device *devices);

AVDeviceInfoList *infoList;
AppState *app_state;
Camera *cameras = NULL;
std::vector<Webcam *> webcams;

//ImGui temp variables
static bool mute = true;
static char save_dir[64];


int main(int argc, char **argv)
{
   spdlog::set_level(spdlog::level::debug);
   Config::load_cam_config();
   mute = Settings::isMuted();
   Settings::getSaveDir().copy(save_dir, sizeof(save_dir));

   struct capture_mode cap_mode = {640, 480, V4L2_PIX_FMT_MJPEG, 30.0f};
   
   int ret = 0;
   int exitCode = 0;
   unsigned int count = 1000;
   bool sdlInitialized = false;
   bool show_demo_window = true;

   int nb_of_cams = 0;



   app_state = (AppState *)calloc(1, sizeof(AppState));
   if (app_state == NULL)
   {
      exitCode = -1;
      goto cleanup;
   }

   *app_state = (AppState){
         .width = 0,
         .height = 0};
   cam_device* cam_devices;
   ret = query_all_webcams(&cam_devices, &nb_of_cams);
   if (ret < 0)
   {
      spdlog::critical("Failed to query for all webcams");
      exitCode = -1;
      goto cleanup;
   }

   if (nb_of_cams <= 0)
   {
      spdlog::critical("No webcams were detected");
      exitCode = -1;
      goto cleanup;
   }
   webcams.reserve(nb_of_cams);

   // return 0;
   for (int i = 0; i < nb_of_cams; i++)
   {

      webcams.push_back(new Webcam(cam_devices + i, &cap_mode));
      ret = webcams[i]->init();
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
   }

   app_state->width = webcams[0]->video.codecContext->width;
   app_state->height = webcams[0]->video.codecContext->height;

   app_state->audio_spec = (SDL_AudioSpec *)malloc(sizeof(SDL_AudioSpec));
   if (app_state->audio_spec == NULL)
   {
      exitCode = -1;
      goto cleanup;
   }
   ret = sdl_load_audio_spec(app_state->audio_spec, webcams[0]->audio.codecContext);
   if (ret < 0)
   {
      exitCode = ret;
      goto cleanup;
   }

   if (SDL_init(app_state, 0, NULL) != SDL_APP_CONTINUE)
   {
      exitCode = -1;
      goto cleanup;
   }
   sdlInitialized = true;

   for (int i = 0; i < nb_of_cams; i++)
   {
      ret = webcams[i]->startAudioCapture(app_state->audio_stream);
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
   }

   /* ****************************Main Loop**************************** */
   spdlog::debug("Starting app core loop");
   while (count-- > 0)
   {
      SDL_Event ev;
      while (SDL_PollEvent(&ev))
      {
         // spdlog::debug("Processing SDL Events");
         if (SDL_event(app_state, &ev) != SDL_APP_CONTINUE)
         {
            spdlog::info("App closed by ESC key press");
            exitCode = 0;
            goto cleanup;
         }
      }
      ImGui_ImplSDLRenderer3_NewFrame();
      ImGui_ImplSDL3_NewFrame();
      ImGui::NewFrame();

      createGUI();
      for (int i = 0; i < nb_of_cams; i++)
      {
         ret = webcams[i]->processVideoFrame(app_state->texture);
         if (ret < 0)
         {
            exitCode = ret;
            goto cleanup;
         }
         if (ret > 0)
         {
            break;
         }
      }
      ImGui::Render();

      if (SDL_iterate(app_state) != SDL_APP_CONTINUE)
      {
         break;
      }
   }
   spdlog::info("Frame Count concluded, ending loop");
   /* **************************End Main Loop************************** */

   if (ret < 0)
   {
      exitCode = ret;
   }

cleanup:
   Settings::save();

   /* for (int i = 0; i < max_cams; i++)
   {
      if(usb_cameras[i].usbPath != NULL)
      {
         free((void *)usb_cameras[i].usbPath);
      }
      if (usb_cameras[i].videoPath != NULL)
      {
         free((void *)usb_cameras[i].videoPath);
      }
      if (usb_cameras[i].audioPath != NULL)
      {
         free((void *)usb_cameras[i].audioPath);
      }
      if (usb_cameras[i].audioCard != NULL)
      {
         free((void *)usb_cameras[i].audioCard);
      }
      if (usb_cameras[i].audioDevice != NULL)
      {
         free((void *)usb_cameras[i].audioDevice);
      }
      if (usb_cameras[i].manufacturer != NULL)
      {
         free((void *)usb_cameras[i].manufacturer);
      }
      if (usb_cameras[i].product != NULL)
      {
         free((void *)usb_cameras[i].product);
      }
      if (usb_cameras[i].vendorID != NULL)
      {
         free((void *)usb_cameras[i].vendorID);
      }
      if (usb_cameras[i].productID != NULL)
      {
         free((void *)usb_cameras[i].productID);
      }
      if (usb_cameras[i].serialNumber != NULL)
      {
         free((void *)usb_cameras[i].serialNumber);
      }
   } */
   for (int i = 0; i < nb_of_cams; i++)
   {
      if (webcams[i] != NULL)
      {
         webcams[i]->close();
      }
      
   }
   if (app_state != NULL)
   {
      if (sdlInitialized)
      {
         SDL_quit(app_state);
      }
      else
      {
         if (app_state->audio_spec != NULL)
         {
            free(app_state->audio_spec);
         }
         free(app_state);
      }
      app_state = NULL;
   }

   if (cameras != NULL)
   {
      for (int i = 0; i < nb_of_cams; ++i)
      {
         if (cameras[i].capture_mode != NULL)
         {
            free(cameras[i].capture_mode);
            cameras[i].capture_mode = NULL;
         }
         if (cameras[i].bus_info != NULL)
         {
            free((void *)cameras[i].bus_info);
            cameras[i].bus_info = NULL;
         }
         if (cameras[i].device_name != NULL)
         {
            free((void *)cameras[i].device_name);
            cameras[i].device_name = NULL;
         }
         if (cameras[i].audio_hw != NULL)
         {
            free((void *)cameras[i].audio_hw);
            cameras[i].audio_hw = NULL;
         }
      }
      delete[] cameras;
   }

   return exitCode;
}

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext)
{
   if (codecContext == NULL)
   {
      return -1;
   }

   spec->channels = codecContext->ch_layout.nb_channels > 0 ? codecContext->ch_layout.nb_channels : 2;
   spec->freq = codecContext->sample_rate > 0 ? codecContext->sample_rate : 48000;
   spec->format = SDL_AUDIO_S16LE;

   return 1;
}

static bool settings_open = false;
void createGUI()
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
         webcams[0]->muteAudioPlayback(mute);
         Settings::setMute(mute);
      }
      ImGui::EndMainMenuBar();
   }
   if (settings_open)
   {
      ImGui::SetNextWindowSize(ImVec2(400.0f, 300.0f), ImGuiCond_FirstUseEver);
      ImGui::Begin("Settings", &settings_open, 0);
      ImGui::Text("Save Directory");
      if (ImGui::InputText("##Save Directory", save_dir, sizeof(save_dir)))
      {
         Settings::setSaveDir(std::string(save_dir));
      }

      ImGui::End();
   }
   for (int i = 0; i < webcams.size(); i++)
   {
      ImGui::Begin("Camera " + i);

      ImVec2 pos = ImGui::GetWindowPos();
      ImVec2 size = ImGui::GetWindowSize();

      ImGui::Text("window pos %.1f %.1f", pos.x, pos.y);
      ImGui::Text("window size %.1f %.1f", size.x, size.y);

      ImVec2 avail = ImGui::GetContentRegionAvail();
      if (avail.x > 1.0f && avail.y > 1.0f && app_state != NULL && app_state->texture != NULL)
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
         ImGui::Image((ImTextureID)app_state->texture, imageSize);
      }

      ImGui::End();
   }
}

void getCardAndDevice(const char *pcm_string, int *card, int *device)
{
   const char* pcm = pcm_string;
   //only supporting a max of 3 digit numbers
   char *tmp_str = (char *)malloc(3 * sizeof(char));
   tmp_str[0] = '\0';
   char *tmp = tmp_str;
   int num_len = 0;
   while(true)
   {
      if(std::isdigit(*pcm))
      {
         *tmp = *pcm;
         *pcm++;
         *tmp++;
      }
      else if (*pcm == 'C')
      {
         *pcm++;



         continue;
      }
      else if (*pcm == 'D')
      {
         spdlog::critical("Temp String: {}", tmp_str);
         *card = std::atoi(tmp_str);
         *tmp = *tmp_str;
         tmp_str[0] = 0;
         tmp_str[1] = 0;
         tmp_str[2] = 0;
         *pcm++;
         continue;
      }
      else if (*pcm == 'c')
      {
         spdlog::critical("Temp String: {}", tmp_str);
         *device = std::atoi(tmp_str);
         break;
      }
      else if (*pcm == '\0')
      {
         break;
      }
   }
   free(tmp_str);
}


int getUsbIndex(const char *usbPath, cam_device *devices)
{


   return -1;
}