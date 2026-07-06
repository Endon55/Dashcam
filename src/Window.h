#pragma once

#include <turbojpeg.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_iostream.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include "State.h"


SDL_AppResult SDL_init(AppState *app_state, int argc, char **argv);

SDL_AppResult SDL_webcam_init(AppState *app_state);

SDL_AppResult SDL_iterate(AppState *app_state);

SDL_AppResult SDL_event(AppState *app_state, SDL_Event *event);

SDL_AppResult SDL_quit(AppState *app_state);
