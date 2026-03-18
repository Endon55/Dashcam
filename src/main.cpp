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
AVDeviceInfoList *infoList;
AppState *app_state;
SDL_Rect *rect;

int main(int argc, char **argv)
{
   spdlog::set_level(spdlog::level::debug);
   int ret = 0;
   int exitCode = 0;
   unsigned int count = 1000;
   bool sdlInitialized = false;

   Camera *cameras = NULL;
   Webcam *webcam = NULL;
   int nb_of_cams = 0;

   app_state = (AppState *)calloc(1, sizeof(AppState));
   rect = (SDL_Rect *)calloc(1, sizeof(SDL_Rect));
   if (app_state == NULL || rect == NULL)
   {
      exitCode = -1;
      goto cleanup;
   }

   rect->x = 0;
   rect->y = 0;

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
   rect->w = app_state->width;
   rect->h = app_state->height;

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

      ret = webcam->processVideoFrame(app_state->texture, rect);
      if (ret < 0)
      {
         exitCode = ret;
         goto cleanup;
      }
      if (ret > 0)
      {
         break;
      }

      if (SDL_iterate(app_state) != SDL_APP_CONTINUE)
      {
         break;
      }
   }

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

   if (rect != NULL)
   {
      free(rect);
      rect = NULL;
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
