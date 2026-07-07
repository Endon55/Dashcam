#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <inttypes.h>
#include <SDL3/SDL_render.h>
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
#include "State.h"
#include "UI.h"

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext);
void getCardAndDevice(const char *pcm_string, int *card, int *device);
int getUsbIndex(const char *usbPath, cam_device *devices);

AVDeviceInfoList *infoList;
AppState *app_state;

// ImGui temp variables
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
   app_state->camera_count = nb_of_cams;
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

   app_state->webcams = new Webcam*[nb_of_cams];
   app_state->cam_textures = (SDL_Texture**)malloc(sizeof(SDL_Texture*) * nb_of_cams); 
   spdlog::debug("Save Dir: {}", Settings::getVideoSaveDir().string());
   // return 0;
   for (int i = 0; i < nb_of_cams; i++)
   {
      app_state->webcams[i] = new Webcam(cam_devices + i,  &cap_mode);
      ret = app_state->webcams[i]->init(i);
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
      any_has_audio = app_state->webcams[i]->has_audio;
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
      ret = sdl_load_audio_spec(app_state->audio_spec, app_state->webcams[0]->audio.codecContext);
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
      app_state->cam_textures[i] = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, app_state->webcams[i]->video.codecContext->width, app_state->webcams[i]->video.codecContext->height);

      if (app_state->cam_textures[i] == NULL)
      {
         spdlog::critical("Couldn't create texture[{}]: {}", i, SDL_GetError());
         goto cleanup;
      }
      if (app_state->webcams[i]->has_audio)
      {
         ret = app_state->webcams[i]->startAudioCapture(app_state->audio_stream);
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

      createGUI(app_state);
      for (int i = 0; i < nb_of_cams; i++)
      {
         ret = app_state->webcams[i]->processVideoFrame(app_state->cam_textures[i]);
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
      if (app_state->webcams[i] != NULL)
      {
        app_state->webcams[i]->close();
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
/*
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
*/
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
