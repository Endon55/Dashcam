
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

SDL_Rect *rect;
bool frame_callback(buffer *buf)
{
   int width, height, jpegSubsamp, jpegColorspace;
   static bool transform_warned = false;

   if (tjDecompressHeader2(app_state->_jpegDecompressor, (unsigned char *)buf->start, buf->length, &width, &height, &jpegSubsamp))
   {
      if (tjGetErrorCode(app_state->_jpegDecompressor) == TJERR_FATAL)
      {
         SDL_Log("Decompress Headers Error: %s", tjGetErrorStr2(app_state->_jpegDecompressor));
         exit(errno);
         return false;
      }
   }

   int pitch = TJPAD(width * tjPixelSize[TJPF_RGBA]);
   unsigned char *jpegData = (unsigned char *)buf->start;
   unsigned long jpegSize = buf->length;
   unsigned char *transformedJpeg = NULL;
   unsigned long transformedJpegSize = 0;
   bool usedTransform = false;

   // Try TurboJPEG lossless horizontal flip. Some MJPEG streams contain markers
   // that make tjTransform fail; fall back to a software flip in that case.
   tjhandle transformHandle = tjInitTransform();
   if (transformHandle)
   {
      tjtransform transform;
      memset(&transform, 0, sizeof(tjtransform));
      transform.op = TJXOP_HFLIP;
      transform.options = TJXOPT_TRIM; // allow trimming if MCU alignment is required

      if (tjTransform(transformHandle, jpegData, jpegSize, 1, &transformedJpeg, &transformedJpegSize, &transform, 0) == 0)
      {
         jpegData = transformedJpeg;
         jpegSize = transformedJpegSize;
         usedTransform = true;
      }
      else if (!transform_warned)
      {
         SDL_Log("Transform failed (falling back to software flip): %s", tjGetErrorStr2(transformHandle));
         transform_warned = true;
      }

      tjDestroy(transformHandle);
   }
   else if (!transform_warned)
   {
      SDL_Log("Transform init failed (falling back to software flip): %s", tjGetErrorStr2(transformHandle));
      transform_warned = true;
   }

   // Decompress the (possibly transformed) JPEG
   if (tjDecompress2(app_state->_jpegDecompressor, jpegData, jpegSize, app_state->decomp_buffer, width, pitch, height, TJPF_RGBA, TJFLAG_FASTDCT))
   {
      if (tjGetErrorCode(app_state->_jpegDecompressor) == TJERR_FATAL)
      {
         SDL_Log("Decompress Error: %s", tjGetErrorStr2(app_state->_jpegDecompressor));
         if (transformedJpeg)
         {
            tjFree(transformedJpeg);
         }
         exit(errno);
         return false;
      }
   }

   if (transformedJpeg)
   {
      tjFree(transformedJpeg);
   }

   if (!usedTransform)
   {
      int pixel_size = tjPixelSize[TJPF_RGBA];
      for (int y = 0; y < height; y++)
      {
         unsigned char *row = app_state->decomp_buffer + (y * pitch);
         for (int x = 0; x < width / 2; x++)
         {
            unsigned char *left = row + (x * pixel_size);
            unsigned char *right = row + ((width - 1 - x) * pixel_size);
            for (int c = 0; c < pixel_size; c++)
            {
               unsigned char temp = left[c];
               left[c] = right[c];
               right[c] = temp;
            }
         }
      }
   }

   if (!SDL_LockTexture(app_state->texture, rect, (void **)&app_state->decomp_buffer, &pitch))
   {
      SDL_Log("Locking Failed: %s", SDL_GetError());
      return false;
   }
   SDL_UnlockTexture(app_state->texture);
   return true;
}

int main(void)
{

   app_state = (AppState *)malloc(sizeof(AppState));
   rect = (SDL_Rect *)malloc(sizeof(SDL_Rect));
   rect->x = 0;
   rect->y = 0;
   *app_state = (AppState){
       .width = 3840,
       .height = 2160};

   app_state->_jpegDecompressor = tjInitDecompress();

   Camera *camera = new Camera(0, &frame_callback);

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

   std::this_thread::sleep_for(std::chrono::milliseconds(500));
   camera->getDimensions(&rect->w, &rect->h);

   cout << rect->w << endl;
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
   free(rect);
   camera->stop_capturing();
   SDL_quit(app_state);

   return 0;
}