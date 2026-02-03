#include "Window.h"

#include <iostream>

using namespace std;

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

SDL_AppResult SDL_webcam_init(AppState *app_state)
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

SDL_AppResult SDL_iterate(AppState *app_state)
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
SDL_AppResult SDL_event(AppState *app_state, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}
void SDL_quit(void *appstate, SDL_AppResult *result)
{
    AppState *app_state = (AppState *)appstate;
    free(app_state);
}