#pragma once

#include <turbojpeg.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_iostream.h>


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


SDL_AppResult init(AppState *app_state, int argc, char **argv);

SDL_AppResult SDL_webcam_init(AppState *app_state);

SDL_AppResult SDL_iterate(AppState *app_state);

SDL_AppResult SDL_event(AppState *app_state, SDL_Event *event);

void SDL_quit(void *appstate, SDL_AppResult *result);
