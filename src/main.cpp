#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <iostream>
#include "Window.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <spdlog/spdlog.h>

#include "Camera/Webcam.h"
#include "Camera/Muxor.h"
#include "Camera/WebcamUtils.h"
#include "Settings.h"

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext);
void createGUI();
AVDeviceInfoList *infoList;
AppState *app_state;

int main(int argc, char **argv)
{
   spdlog::set_level(spdlog::level::debug);
   int ret = 0;
   int exitCode = 0;
   unsigned int count = 1000;
   bool sdlInitialized = false;
   bool show_demo_window = true;

   Camera *cameras = NULL;
   Webcam *webcam = NULL;
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

   ret = query_all_webcams(&cameras, &nb_of_cams);
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

   ret = findBestCaptureMode(RESOLUTION, &cameras[0]);
   if (ret < 0)
   {
      spdlog::critical("Failed to find best capture mode for webcam 0");
      exitCode = -1;
      goto cleanup;
   }

   // return 0;

   webcam = new Webcam(&cameras[0]);
   ret = webcam->init();
   if (ret < 0)
   {
      exitCode = ret;
      goto cleanup;
   }

   app_state->width = webcam->video.codecContext->width;
   app_state->height = webcam->video.codecContext->height;

   app_state->audio_spec = (SDL_AudioSpec *)malloc(sizeof(SDL_AudioSpec));
   if (app_state->audio_spec == NULL)
   {
      exitCode = -1;
      goto cleanup;
   }
   ret = sdl_load_audio_spec(app_state->audio_spec, webcam->audio.codecContext);
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

   ret = webcam->startAudioCapture(app_state->audio_stream);
   if (ret < 0)
   {
      exitCode = ret;
      goto cleanup;
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

      ret = webcam->processVideoFrame(app_state->texture);
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
      if (ret > 0)
      {
         break;
      }

      ImGui::ShowDemoWindow(&show_demo_window);

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
   if (webcam != NULL)
   {
      webcam->close();
      delete webcam;
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

void createGUI()
{

   if(ImGui::BeginMainMenuBar())
   {
      if(ImGui::BeginMenu("File"))
      {
         if(ImGui::MenuItem("Test"))
         {
         }
         if (ImGui::MenuItem("Test1"))
         {
         }
         if (ImGui::MenuItem("Test2"))
         {
         }
         ImGui::EndMenu();
      }
      ImGui::EndMainMenuBar();
   }


   ImGui::Begin("Window A");
   ImGui::Text("Test");

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