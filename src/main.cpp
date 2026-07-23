#include <SDL3/SDL_audio.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string>
#include <inttypes.h>
#include <SDL3/SDL_render.h>
#include <linux/videodev2.h>
#include <libudev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <alsa/asoundlib.h>
#include <toml++/toml.hpp>

#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_DEBUG
#include <spdlog/spdlog.h>
#include "Camera/Camera.h"
#include "Camera/Webcam.h"
#include "Camera/WebcamUtils.h"
#include "Window.h"
#include "Settings.h"
#include "Config.h"
#include "State.h"
#include "UI.h"
#include "memory.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext);
void getCardAndDevice(const char *pcm_string, int *card, int *device);
int getUsbIndex(const char *usbPath, cam_device *devices);

AVDeviceInfoList *infoList;
AppState *app_state = nullptr;

// ImGui temp variables
int main(int argc, char **argv)
{
    /*Logitech cameras add proprietary information to the end of mjpeg data packets,
      ffmpeg cannot properly parse it, thus it throws an error every frame.
      The frame data is still in tact and readable but to suppress the console spam, the log level has been set to fatal.

      I would like to resolve this in a better way in the future.
      */
    av_log_set_level(AV_LOG_FATAL);
    spdlog::set_level(spdlog::level::debug);
    Settings::load();
    mute = Settings::isMuted();
    Settings::getVideoSaveDir().string().copy(save_dir, sizeof(save_dir));
    bool any_has_audio = false;
    int ret = 0;
    int exitCode = 0;
    unsigned int count = 1000;

    bool sdlInitialized = false;
    bool show_demo_window = true;
    app_state = (AppState *)dc_malloc(sizeof(AppState));

    if (app_state == nullptr)
    {
        exitCode = -1;
        goto cleanup;
    }
    *app_state = (AppState){
        .window = nullptr,
        .renderer = nullptr,
        .event = nullptr,
        .audio_spec = nullptr,
        .audio_stream = nullptr,
        .window_size = {0, 0},
        .pitch = -1,
        .cam_textures = nullptr,
        .devices = nullptr,
        .nb_cams = 0,
        .webcams = nullptr
    };

    app_state->audio_spec = (SDL_AudioSpec*)dc_malloc(sizeof(SDL_AudioSpec));
    app_state->audio_spec->format = SDL_AUDIO_S16LE;
    app_state->audio_spec->channels = 2;
    app_state->audio_spec->freq = 48000;

    ret = query_all_webcams(app_state);
    if (ret < 0)
    {
        spdlog::critical("Failed to query for all webcams");
        exitCode = -1;
        goto cleanup;
    }
    ret = Config::load_cam_configs(app_state);
    if(ret < 0)
    {
        spdlog::critical("Critical error when parsing configs");
        exitCode = -1;
        goto cleanup;
    }
    if (app_state->nb_cams <= 0)
    {
        spdlog::critical("No webcams were detected");
        exitCode = -1;
        goto cleanup;
    }
    

    ret = query_all_capture_modes(app_state);

    if(ret < 0)
    {
        spdlog::critical("Failed to quary capture modes for webcams");
        return ret;
    }
    Config::save_cam_configs(app_state);

    app_state->webcams = new Webcam*[app_state->nb_cams];
    app_state->cam_textures = (SDL_Texture**)dc_malloc(sizeof(SDL_Texture*) * app_state->nb_cams); 
    spdlog::debug("Save Dir: {}", Settings::getVideoSaveDir().string());
    for (int i = 0; i < app_state->nb_cams; i++)
    {
        app_state->webcams[i] = new Webcam(app_state->devices[i]);
        ret = app_state->webcams[i]->init(i);
        if (ret < 0)
        {
            exitCode = ret;
            goto cleanup;
        }
        any_has_audio = app_state->webcams[i]->has_audio;
    }

    // app_state->width = webcams[0]->video.codecContext->width;
    // app_state->height = webcams[0]->video.codecContext->height;

    app_state->audio_spec = (SDL_AudioSpec *)dc_malloc(sizeof(SDL_AudioSpec));
    if (app_state->audio_spec == nullptr)
    {
        exitCode = -1;
        goto cleanup;
    }
    if (any_has_audio)
    {
        ret = sdl_load_audio_spec(app_state->audio_spec, app_state->webcams[0]->audio.codecContext);
        if (ret < 0)
        {
            exitCode = ret;
            goto cleanup;
        }
    }

    if (SDL_init(app_state, 0, nullptr) != SDL_APP_CONTINUE)
    {
        exitCode = -1;
        goto cleanup;
    }
    sdlInitialized = true;

    for (int i = 0; i < app_state->nb_cams; i++)
    {
        app_state->cam_textures[i] = SDL_CreateTexture(app_state->renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, app_state->webcams[i]->video.codecContext->width, app_state->webcams[i]->video.codecContext->height);

        if (app_state->cam_textures[i] == nullptr)
        {
            spdlog::critical("Couldn't create texture[{}]: {}", i, SDL_GetError());
            goto cleanup;
        }
        if (app_state->webcams[i]->has_audio)
        {
            ret = app_state->webcams[i]->startAudioCapture(app_state->audio_stream);
            if (ret < 0)
            {
                exitCode = ret;
                goto cleanup;
            }
        }
    }

    /* ****************************Main Loop**************************** */
    spdlog::debug("Starting app core loop");
    while (true)
    {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
        {
            // spdlog::debug("Processing SDL Events");
            if (SDL_event(app_state, &ev) != SDL_APP_CONTINUE)
            {
                spdlog::info("App closed by ESC key press");
                exitCode = 0;
                goto cleanup;
            }
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        createGUI(app_state);
        for (int i = 0; i < app_state->nb_cams; i++)
        {
            ret = app_state->webcams[i]->processVideoFrame(app_state->cam_textures[i]);
            if (ret < 0)
            {
                exitCode = ret;
                goto cleanup;
            }
            if (ret > 0)
            {
                break;
            }
        }
        ImGui::Render();

        if (SDL_iterate(app_state) != SDL_APP_CONTINUE)
        {
            break;
        }
    }
    spdlog::info("Frame Count concluded, ending loop");
    /* **************************End Main Loop************************** */

    if (ret < 0)
    {
        exitCode = ret;
    }

cleanup:
    Settings::save();
    for (int i = 0; i < app_state->nb_cams; i++)
    {
        camera_free(app_state->devices[i]);
        if(app_state->cam_textures != nullptr && app_state->cam_textures[i] != nullptr)
        {
            SDL_DestroyTexture(app_state->cam_textures[i]);
            app_state->cam_textures[i] = nullptr;
        }
    }
    dc_free(app_state->cam_textures);
    dc_free(app_state->devices);
    for (int i = 0; i < app_state->nb_cams; i++)
    {
        if (app_state->webcams[i] != nullptr)
        {
            app_state->webcams[i]->close();
            delete app_state->webcams[i];
            app_state->webcams[i] = nullptr;
        }
    }
    delete[] app_state->webcams;
    app_state->webcams = nullptr;
    if (app_state != nullptr)
    {
        if (sdlInitialized)
        {
            SDL_quit(app_state);
        }
        if (app_state->audio_stream != nullptr)
        {
            SDL_DestroyAudioStream(app_state->audio_stream);
            app_state->audio_stream = nullptr;
        }

        if (app_state->audio_spec != NULL)
        {
            dc_free(app_state->audio_spec);
            app_state->audio_spec = nullptr;
        }

        dc_free(app_state);

        app_state = nullptr;
    }

    spdlog::info("Memory status: {}", outstanding_references());

    return exitCode;
}

int sdl_load_audio_spec(SDL_AudioSpec *spec, const AVCodecContext *codecContext)
{
    if (codecContext == nullptr)
    {
        return -1;
    }

    spec->channels = codecContext->ch_layout.nb_channels > 0 ? codecContext->ch_layout.nb_channels : 2;
    spec->freq = codecContext->sample_rate > 0 ? codecContext->sample_rate : 48000;
    spec->format = SDL_AUDIO_S16LE;

    return 1;
}
void getCardAndDevice(const char *pcm_string, int *card, int *device)
{
    const char *pcm = pcm_string;
    // only supporting a max of 3 digit numbers
    char *tmp_str = (char *)dc_malloc(3 * sizeof(char));
    tmp_str[0] = '\0';
    char *tmp = tmp_str;
    int num_len = 0;
    while (true)
    {
        if (std::isdigit(*pcm))
        {
            *tmp = *pcm;
            *pcm++;
            *tmp++;
        }
        else if (*pcm == 'C')
        {
            *pcm++;

            continue;
        }
        else if (*pcm == 'D')
        {
            spdlog::critical("Temp String: {}", tmp_str);
            *card = std::atoi(tmp_str);
            *tmp = *tmp_str;
            tmp_str[0] = 0;
            tmp_str[1] = 0;
            tmp_str[2] = 0;
            *pcm++;
            continue;
        }
        else if (*pcm == 'c')
        {
            spdlog::critical("Temp String: {}", tmp_str);
            *device = std::atoi(tmp_str);
            break;
        }
        else if (*pcm == '\0')
        {
            break;
        }
    }
    dc_free(tmp_str);
}

int getUsbIndex(const char *usbPath, cam_device *devices)
{

    return -1;
}
