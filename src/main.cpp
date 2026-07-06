#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <inttypes.h>
#include <vector>

#include <linux/videodev2.h>
#include <libudev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>

#include "Camera/Webcam.h"
#include "Camera/WebcamUtils.h"
#include "Window.h"
#include "Settings.h"
#include "Config.h"

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext);
void getCardAndDevice(const char *pcm_string, int *card, int *device);
void createGUI();
int getUsbIndex(const char *usbPath, cam_device *devices);

AVDeviceInfoList *infoList;
AppState *app_state;
CamState *cam_states;
Camera *cameras = NULL;
std::vector<Webcam *> webcams;

// ImGui temp variables
static bool mute = true;
static char save_dir[64];

int main(int argc, char **argv)
{
   /*Logitech cameras add proprietary information to the end of mjpeg data packets,
   ffmpeg cannot properly parse it, thus it throws an error every frame.
   The frame data is still in tact and readable but to suppress the console spam, the log level has been set to fatal.

   I would like to resolve this in a better way in the future.
   */
   av_log_set_level(AV_LOG_FATAL);
   spdlog::set_level(spdlog::level::debug);
   Config::load_cam_config();
   Settings::load();
   mute = Settings::isMuted();
   Settings::getVideoSaveDir().string().copy(save_dir, sizeof(save_dir));
   bool any_has_audio = false;

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
   cam_device *cam_devices;
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
   cam_states = (CamState *)malloc(sizeof(CamState) * nb_of_cams);
   spdlog::debug("Save Dir: {}", Settings::getVideoSaveDir().string());
   // return 0;
   for (int i = 0; i < nb_of_cams; i++)
   {
      webcams.push_back(new Webcam(cam_devices + i, &cap_mode));
      ret = webcams[i]->init(i);
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
      any_has_audio = webcams[i]->has_audio;
   }

   // app_state->width = webcams[0]->video.codecContext->width;
   // app_state->height = webcams[0]->video.codecContext->height;

   app_state->audio_spec = (SDL_AudioSpec *)malloc(sizeof(SDL_AudioSpec));
   if (app_state->audio_spec == NULL)
   {
      exitCode = -1;
      goto cleanup;
   }
   if (any_has_audio)
   {
      ret = sdl_load_audio_spec(app_state->audio_spec, webcams[0]->audio.codecContext);
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
   }

   if (SDL_init(app_state, 0, NULL) != SDL_APP_CONTINUE)
   {
      exitCode = -1;
      goto cleanup;
   }
   sdlInitialized = true;

   for (int i = 0; i < nb_of_cams; i++)
   {
      CamState *state = &cam_states[i];

      state->texture = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, webcams[i]->video.codecContext->width, webcams[i]->video.codecContext->height);

      if (state->texture == NULL)
      {
         spdlog::critical("Couldn't create texture[{}]: {}", i, SDL_GetError());
         goto cleanup;
      }
      if (webcams[i]->has_audio)
      {
         ret = webcams[i]->startAudioCapture(app_state->audio_stream);
         if (ret < 0)
         {
            exitCode = ret;
            goto cleanup;
         }
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
         ret = webcams[i]->processVideoFrame(cam_states[i].texture);
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

   for (int i = 0; i < nb_of_cams; i++)
   {
      if (cam_devices[i].usbPath != NULL)
      {
         free((void *)cam_devices[i].usbPath);
      }
      if (cam_devices[i].videoPath != NULL)
      {
         free((void *)cam_devices[i].videoPath);
      }
      if (cam_devices[i].audioPath != NULL)
      {
         free((void *)cam_devices[i].audioPath);
      }
      if (cam_devices[i].audioCard != NULL)
      {
         free((void *)cam_devices[i].audioCard);
      }
      if (cam_devices[i].audioDevice != NULL)
      {
         free((void *)cam_devices[i].audioDevice);
      }
      if (cam_devices[i].manufacturer != NULL)
      {
         free((void *)cam_devices[i].manufacturer);
      }
      if (cam_devices[i].product != NULL)
      {
         free((void *)cam_devices[i].product);
      }
      if (cam_devices[i].vendorID != NULL)
      {
         free((void *)cam_devices[i].vendorID);
      }
      if (cam_devices[i].productID != NULL)
      {
         free((void *)cam_devices[i].productID);
      }
      if (cam_devices[i].serialNumber != NULL)
      {
         free((void *)cam_devices[i].serialNumber);
      }
   }

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
      if (avail.x > 1.0f && avail.y > 1.0f && app_state != NULL && cam_states[i].texture != NULL)
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
         ImGui::Image((ImTextureID)cam_states[i].texture, imageSize);
      }

      ImGui::End();
   }
}

void getCardAndDevice(const char *pcm_string, int *card, int *device)
{
   const char *pcm = pcm_string;
   // only supporting a max of 3 digit numbers
   char *tmp_str = (char *)malloc(3 * sizeof(char));
   tmp_str[0] = '\0';
   char *tmp = tmp_str;
   int num_len = 0;
   while (true)
   {
      if (std::isdigit(*pcm))
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
