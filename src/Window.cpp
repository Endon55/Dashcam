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

    app_state->window = SDL_CreateWindow("Dashcam", app_state->width, app_state->height, SDL_WINDOW_FULLSCREEN);
    if (app_state->window == NULL)
    {
        SDL_Log("Couldn't create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    app_state->renderer = SDL_CreateRenderer(app_state->window, NULL);
    if (app_state->renderer == NULL)
    {
        SDL_Log("Couldn't create renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    //SDL_SetRenderLogicalPresentation(app_state->renderer, app_state->width, app_state->height, SDL_LOGICAL_PRESENTATION_DISABLED);

    // SDL_PIXELFORMAT_RGBX32
    app_state->texture = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STREAMING, app_state->width, app_state->height);

    if (app_state->texture == NULL)
    {
        SDL_Log("Couldn't create texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    // Allocate reusable decompression buffer (RGB: 3 bytes per pixel)
    app_state->decomp_buffer = (unsigned char *)malloc(app_state->width * app_state->height * tjPixelSize[TJPF_RGBA]);
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
    if(event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        SDL_GetWindowSizeInPixels(app_state->window, &app_state->width, &app_state->height);
        cout << "w:" << app_state->width << " h:" << app_state->height << endl;
    }
    if(event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.key)
        {
            case(SDLK_ESCAPE):
                return SDL_APP_SUCCESS;

            default:
                break;
        }
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