#pragma once
#include "State.h"


SDL_AppResult SDL_init(AppState *app_state, int argc, char **argv);

SDL_AppResult SDL_webcam_init(AppState *app_state);

SDL_AppResult SDL_iterate(AppState *app_state);

SDL_AppResult SDL_event(AppState *app_state, SDL_Event *event);


SDL_AppResult SDL_quit(AppState *app_state);

