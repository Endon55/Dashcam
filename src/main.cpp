#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <iostream>
#include "Window.h"
#include "Camera/Webcam.h"

#include <spdlog/spdlog.h>

using namespace std;

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodec *codec, AVCodecParameters *params);

AVDeviceInfoList *infoList;
AppState *app_state;
SDL_Rect *rect;
int main(int argc, char **argv)
{
   int ret;
   int exitCode = 0;
   unsigned int count = 1000;
   app_state = (AppState *)malloc(sizeof(AppState));
   rect = (SDL_Rect *)malloc(sizeof(SDL_Rect));
   rect->x = 0;
   rect->y = 0;

   *app_state = (AppState){
       .width = 0,
       .height = 0};



   spdlog::set_level(spdlog::level::debug);
   Webcam *webcam = new Webcam(0);
   ret = webcam->init();
   if (ret < 0)
   {
      delete webcam;
      return ret;
   }


   app_state->width = webcam->video.codecContext->width;
   app_state->height = webcam->video.codecContext->height;
   rect->w = app_state->width;
   rect->h = app_state->height;

   if (SDL_init(app_state, 0, NULL) != SDL_APP_CONTINUE)
   {
      webcam->close();
      delete webcam;
      return -1;
   }
   app_state->audio_spec = (SDL_AudioSpec *)malloc(sizeof(SDL_AudioSpec));
   ret = sdl_load_audio_spec(app_state->audio_spec, webcam->audio.codec, webcam->audio.codecParams);
   if (ret < 0)
   {
      exitCode = ret;
      goto cleanup;
   }


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
         spdlog::debug("Processing SDL Events");
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
   webcam->close();
   SDL_quit(app_state);
   delete webcam;
   free(rect);
   return exitCode;
}

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodec *codec, AVCodecParameters *params)
{
   spec->channels = params->ch_layout.nb_channels;
   spec->freq = params->sample_rate;
   spec->format = SDL_AUDIO_S16LE;

   return 1;
}
