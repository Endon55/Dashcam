#include "Window.h"

#include <iostream>

using namespace std;

SDL_AppResult SDL_init(AppState *app_state, int argc, char **argv)
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

SDL_AppResult SDL_iterate(AppState *app_state)
{
    SDL_SetRenderDrawColorFloat(app_state->renderer, 0.4f, 0.6f, 1.0f, SDL_ALPHA_OPAQUE_FLOAT);
    SDL_RenderClear(app_state->renderer);

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
SDL_AppResult SDL_quit(AppState *app_state)
{

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

    return SDL_APP_SUCCESS;
}