#include "Window.h"
#include <iostream>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
using namespace std;

SDL_AppResult SDL_init(AppState *app_state, int argc, char **argv)
{
    // SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_FULLSCREEN;
    app_state->window = SDL_CreateWindow("Dashcam", app_state->width, app_state->height, window_flags);
    if (app_state->window == NULL)
    {
        SDL_Log("Couldn't create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetWindowFullscreenMode(app_state->window, NULL);
    SDL_SetWindowFullscreen(app_state->window, true);

    app_state->renderer = SDL_CreateRenderer(app_state->window, NULL);
    if (app_state->renderer == NULL)
    {
        SDL_Log("Couldn't create renderer: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_SetRenderVSync(app_state->renderer, 1);

    // Initializing ImGui

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();
    ImGuiStyle &style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);
    io.FontGlobalScale = main_scale;

    ImGui_ImplSDL3_InitForSDLRenderer(app_state->window, app_state->renderer);
    ImGui_ImplSDLRenderer3_Init(app_state->renderer);

    SDL_SetRenderLogicalPresentation(app_state->renderer, app_state->width, app_state->height, SDL_LOGICAL_PRESENTATION_DISABLED);

    // SDL_PIXELFORMAT_RGBX32
    /* app_state->texture = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, app_state->width, app_state->height);

    if (app_state->texture == NULL)
    {
        SDL_Log("Couldn't create texture: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    } */

    //app_state->audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, app_state->audio_spec, NULL, NULL);

    /* if (!app_state->audio_stream)
    {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    } */
    //SDL_ResumeAudioStreamDevice(app_state->audio_stream);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_iterate(AppState *app_state)
{
    SDL_SetRenderDrawColorFloat(app_state->renderer, 0.4f, 0.6f, 1.0f, SDL_ALPHA_OPAQUE_FLOAT);
    // SDL_SetRenderDrawColor(app_state->renderer, 0, 0, 0, 255);
    SDL_RenderClear(app_state->renderer);

    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), app_state->renderer);

    SDL_RenderPresent(app_state->renderer);

    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_event(AppState *app_state, SDL_Event *event)
{
    if (event->type == SDL_EVENT_QUIT || event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        return SDL_APP_SUCCESS;
    }
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        SDL_GetWindowSizeInPixels(app_state->window, &app_state->width, &app_state->height);
        cout << "w:" << app_state->width << " h:" << app_state->height << endl;
    }
    ImGui_ImplSDL3_ProcessEvent(event);
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.key)
        {
        case (SDLK_ESCAPE):
            return SDL_APP_SUCCESS;

        default:
            break;
        }
    }
    return SDL_APP_CONTINUE;
}
SDL_AppResult SDL_quit(AppState *app_state)
{
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    return SDL_APP_SUCCESS;
}
