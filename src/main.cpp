
#include "Camera/Camera.h"

#include <fmt/core.h>
#include <string>
#include <iostream>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_iostream.h>
#include <stdio.h>
#include <stdlib.h>
#include <chrono>
#include <thread>
#include <turbojpeg.h>

using namespace std;

struct AppState
{
   SDL_Window *window;
   SDL_Renderer *renderer;
   SDL_Texture *texture;
   SDL_CameraID *devices;
   SDL_Camera *camera;
   SDL_Event *event;
   int width;
   int height;
   int camera_count;
   int pitch = -1;
   long unsigned int _jpegSize;
   int jpegSubsamp;
   unsigned char *decomp_buffer;
   tjhandle _jpegDecompressor;
};

AppState *app_state;

SDL_AppResult init(AppState *app_state, int argc, char **argv)
{

   if (!SDL_Init(SDL_INIT_VIDEO))
   {
      SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
      return SDL_APP_FAILURE;
   }
   else
   {
      cout << "Success" << endl;
   }

   if (!SDL_CreateWindowAndRenderer("Dashcam", app_state->width, app_state->height, 0, &(app_state->window), &(app_state->renderer)))
   {
      SDL_Log("Couldn't create window or renderer: %s", SDL_GetError());
      return SDL_APP_FAILURE;
   }

   app_state->texture = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_RGBX32, SDL_TEXTUREACCESS_STREAMING, app_state->width, app_state->height);
   if (app_state->texture == NULL)
   {
      SDL_Log("Couldn't create texture: %s", SDL_GetError());
      return SDL_APP_FAILURE;
   }

   // Allocate reusable decompression buffer (RGB: 3 bytes per pixel)
   app_state->decomp_buffer = (unsigned char *)malloc(app_state->width * app_state->height * tjPixelSize[TJPF_XRGB]);
   if (!app_state->decomp_buffer)
   {
      SDL_Log("Failed to allocate decompression buffer");
      return SDL_APP_FAILURE;
   }

   return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_webcam_init()
{
   SDL_CameraID *devices = SDL_GetCameras(&app_state->camera_count);
   if (devices == NULL || app_state->camera_count == 0)
   {
      SDL_Log("Couldn't find cameras or no camera: %s", SDL_GetError());
      return SDL_APP_FAILURE;
   }

   SDL_Log("%d webcam(s) on system", app_state->camera_count);

   app_state->devices = devices;

   app_state->camera = SDL_OpenCamera(devices[0], NULL);
   if (app_state->camera == NULL)
   {
      SDL_Log("Couldn't open camera: %s", SDL_GetError());
      return SDL_APP_FAILURE;
   }

   SDL_CameraSpec spec;
   SDL_GetCameraFormat(app_state->camera, &spec);
   int FPS = spec.framerate_numerator / spec.framerate_denominator;
   SDL_Log("Camera supports %dx%d in %d at %dfps", spec.width, spec.height, spec.format, FPS);
   return SDL_APP_CONTINUE;
}

SDL_AppResult iterate(AppState *app_state)
{
   SDL_SetRenderDrawColorFloat(app_state->renderer, 0.4f, 0.6f, 1.0f, SDL_ALPHA_OPAQUE_FLOAT);
   SDL_RenderClear(app_state->renderer);

   /* SDL_Surface *frame = SDL_AcquireCameraFrame(app_state->camera, NULL);

     if (frame != NULL)
    {
       if (app_state->texture == NULL)
       {
          app_state->texture = SDL_CreateTexture(app_state->renderer, frame->format, SDL_TEXTUREACCESS_STREAMING, frame->w, frame->h);
          SDL_UpdateTexture(app_state->texture, NULL, frame->pixels, frame->pitch);
       }
       else
       {
          SDL_UpdateTexture(app_state->texture, NULL, frame->pixels, frame->pitch);
       }
       SDL_ReleaseCameraFrame(app_state->camera, frame);
    } */

   if (app_state->texture)
   {
      SDL_RenderTexture(app_state->renderer, app_state->texture, NULL, NULL);
   }

   SDL_RenderPresent(app_state->renderer);

   return SDL_APP_CONTINUE;
}
SDL_AppResult event(AppState *app_state, SDL_Event *event)
{
   if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
   {
      return SDL_APP_SUCCESS;
   }
   return SDL_APP_CONTINUE;
}
void quit(void *appstate, SDL_AppResult *result)
{
   AppState *app_state = (AppState *)appstate;
   free(app_state);
}

bool frame_callback(buffer *buf)
{
   SDL_Log("Locking Texture");

   int width, height, jpegSubsamp, jpegColorspace;

   if(tjDecompressHeader2(app_state->_jpegDecompressor, (unsigned char *)buf->start, buf->length, &width, &height, &jpegSubsamp))
   {
      SDL_Log("Decompress Headers Error: %s", tjGetErrorStr2(app_state->_jpegDecompressor));
      //return false;
   }
   int pitch = width * tjPixelSize[TJPF_RGBA];

   if(tjDecompress2(app_state->_jpegDecompressor, (unsigned char *)buf->start, buf->length, app_state->decomp_buffer, width, pitch, height, TJPF_RGBX, TJFLAG_FASTDCT))
   {
      SDL_Log("Decompress Error: %s", tjGetErrorStr2(app_state->_jpegDecompressor));
      //return false;
   }

   if (!SDL_LockTexture(app_state->texture, NULL, (void **)&app_state->decomp_buffer, &pitch))
   {
      SDL_Log("Locking Failed: %s", SDL_GetError());
      return false;
   }
   SDL_UnlockTexture(app_state->texture);
   SDL_Log("Unlocking Texture");
   return true;
}

int main(void)
{

   char dev0[] = "/dev/video0";

   app_state = (AppState *)malloc(sizeof(AppState));

   *app_state = (AppState){
       .width = 3840,
       .height = 2160};
   cout << "Width: " << app_state->width;
   app_state->_jpegDecompressor = tjInitDecompress();

   Camera *camera = new Camera(dev0, &frame_callback);

   init(app_state, 0, NULL);

   std::this_thread::sleep_for(std::chrono::milliseconds(500));
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

   camera->start_capturing();
   unsigned int count = 10000;
   while (count-- > 0)
   {

      SDL_Event ev;
      while (SDL_PollEvent(&ev))
      {
         if (event(app_state, &ev) != SDL_APP_CONTINUE)
         {
            return 0;
         }
      }
      if (iterate(app_state) != SDL_APP_CONTINUE)
      {
         break;
      }

      camera->update();
   }

   camera->stop_capturing();

   if (app_state->texture != NULL)
   {
      SDL_DestroyTexture(app_state->texture);
   }

   if (app_state->devices != NULL)
   {
      SDL_free(app_state->devices);
   }
   if (app_state->camera != NULL)
   {
      SDL_CloseCamera(app_state->camera);
   }
   if (app_state->decomp_buffer != NULL)
   {
      tjFree(app_state->decomp_buffer);
   }

   free(app_state);

   return 0;
}