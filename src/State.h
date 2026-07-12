#pragma once

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_init.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3/SDL_iostream.h>
#include "Camera/Camera.h"
#include "Camera/Webcam.h"
#include <toml++/toml.hpp>

struct AppState
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Event *event;
    SDL_AudioSpec *audio_spec;
    SDL_AudioStream *audio_stream;
    int width;
    int height;
    int pitch = -1;
    SDL_Texture** cam_textures;
    cam_device** devices;
    int nb_cams;
    Webcam **webcams;
};

