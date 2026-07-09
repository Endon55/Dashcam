
#pragma once

#include <stdlib.h>
#include <atomic>
#include <thread>
#include <SDL3/SDL.h>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <alsa/asoundlib.h>



extern "C"
{
#include <libavfilter/buffersink.h>
}
#include "Muxor.h"
#include "Camera.h"
using namespace std;



struct Video
{
    int stream_index;
    AVFormatContext *fmtContext;
    const AVCodec *codec;
    AVCodecContext *codecContext;
    AVCodecParameters *codecParams;
    AVFrame *frame;
    AVFrame *yuv_frame;
    AVFrame *filtered_frame;
    AVPacket *packet;
    SwsContext *sws_ctx;
    AVFilterGraph *filterGraph;
    AVFilterContext *sourceFilter;
    AVFilterContext *sinkFilter;
    AVFilterContext *flipFilter;
};
struct Audio
{
    int stream_index;
    const char* hw;
    AVFormatContext *fmtContext;
    const AVCodec *codec;
    AVCodecParameters *codecParams;
    AVCodecContext *codecContext;
    AVFrame *frame;
    AVPacket *packet;
    AVStream *stream;
    SwrContext *swr_ctx;
    AVChannelLayout out_ch_layout;
    uint8_t *out_buf;
    int out_buf_size;
};

class Webcam
{
public:
    struct Video video;
    struct Audio audio;
    struct cam_device *device;
    Muxor *muxor;
    const bool has_audio;

public:
    Webcam(cam_device *camera);
    int init(int camIndex);
    int close();
    int processVideoFrame(SDL_Texture *texture);
    int processAudioFrame(SDL_AudioStream *audioStream);
    int startAudioCapture(SDL_AudioStream *audioStream);
    int stopAudioCapture();
    int muteAudioPlayback(bool mute);

private:
    int initVideo();
    int initAudio();
    int closeVideo();
    int closeAudio();
    void audioCaptureLoop();
    int writeEncodedPacket(AVCodecContext *encContext, AVStream *stream);
    int flushRecorderEncoder(AVCodecContext *encContext, AVStream *stream);

private:
    std::thread audioThread;
    std::atomic<bool> audioThreadRunning{false};
    std::atomic<bool> audioThreadStopRequested{false};
    SDL_AudioStream *audioStreamTarget = nullptr;
    std::atomic<int64_t> videoPtsStartUs{AV_NOPTS_VALUE};
    std::atomic<bool> mute{true};
};
