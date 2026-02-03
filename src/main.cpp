
#include "Camera/Camera.h"

#include <fmt/core.h>
#include <string>
#include <iostream>

#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <thread>
#include <turbojpeg.h>
#include "Window.h"

using namespace std;

AppState *app_state;

bool frame_callback(buffer *buf)
{
   int width, height, jpegSubsamp, jpegColorspace;

   if (tjDecompressHeader2(app_state->_jpegDecompressor, (unsigned char *)buf->start, buf->length, &width, &height, &jpegSubsamp))
   {
      if (tjGetErrorCode(app_state->_jpegDecompressor) == TJERR_FATAL)
      {
         SDL_Log("Decompress Headers Error: %s", tjGetErrorStr2(app_state->_jpegDecompressor));
         exit(errno);
         return false;
      }
   }
   int pitch = width * tjPixelSize[TJPF_RGBA];
   // Returns 0 on a success
   if (tjDecompress2(app_state->_jpegDecompressor, (unsigned char *)buf->start, buf->length, app_state->decomp_buffer, width, pitch, height, TJPF_RGBX, TJFLAG_FASTDCT))
   {
      if (tjGetErrorCode(app_state->_jpegDecompressor) == TJERR_FATAL)
      {
         SDL_Log("Decompress Error: %s", tjGetErrorStr2(app_state->_jpegDecompressor));
         exit(errno);
         return false;
      }
   }

   if (!SDL_LockTexture(app_state->texture, NULL, (void **)&app_state->decomp_buffer, &pitch))
   {
      SDL_Log("Locking Failed: %s", SDL_GetError());
      return false;
   }
   SDL_UnlockTexture(app_state->texture);
   return true;
}

int main(void)
{

   char dev0[] = "/dev/video0";

   app_state = (AppState *)malloc(sizeof(AppState));

   *app_state = (AppState){
       .width = 3840,
       .height = 2160};

   app_state->_jpegDecompressor = tjInitDecompress();

   Camera *camera = new Camera(dev0, &frame_callback);

   SDL_init(app_state, 0, NULL);

   if (!camera->init_device())
   {
      // Cleanup
      if (app_state->_jpegDecompressor)
      {
         tjDestroy(app_state->_jpegDecompressor);
      }

      if (app_state->decomp_buffer)
      {
         free(app_state->decomp_buffer);
      }

      cout << "Failed to init device" << endl;
      return -1;
   }

   // std::this_thread::sleep_for(std::chrono::milliseconds(2000));
   camera->start_capturing();

   unsigned int count = 10000;
   while (count-- > 0)
   {

      SDL_Event ev;
      while (SDL_PollEvent(&ev))
      {
         if (SDL_event(app_state, &ev) != SDL_APP_CONTINUE)
         {
            return 0;
         }
      }
      if (SDL_iterate(app_state) != SDL_APP_CONTINUE)
      {
         break;
      }

      camera->update();
   }

   camera->stop_capturing();
   SDL_quit(app_state);

   return 0;
}