#pragma once

#include <stdlib.h>
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <SDL3/SDL.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}
#include <spdlog/spdlog.h>

#include "Macros.h"
#include "Muxor.h"
#include "WebcamUtils.h"

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
    struct Camera *camera;
    Muxor *muxor;
    const bool has_audio;

public:
    Webcam(Camera *camera);
    int init();
    int close();
    int processVideoFrame(SDL_Texture *texture, SDL_Rect *rect);
    int processAudioFrame(SDL_AudioStream *audioStream);
    int startAudioCapture(SDL_AudioStream *audioStream);
    int stopAudioCapture();


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
};
