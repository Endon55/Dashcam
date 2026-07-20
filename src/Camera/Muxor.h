#pragma once
/*
    Take the input video and audio stream and combine them into an mp4 file.
*/

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/samplefmt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavcodec/defs.h>
#include <libavutil/dict.h>
}
#include <mutex>
#include <string>


struct OutputStream
{
    AVStream *stream = nullptr;
    AVCodecContext *codecContext = nullptr;

    int64_t next_pts = 0;
    int samples_count = 0;
    AVRational src_time_base = {};
    AVFrame *frame = nullptr;
    AVFrame *tmp_frame = nullptr;
    int width, height = 0;

    AVPacket *tmp_packet = nullptr;

    float t, tincr, tincr2 = 0.0;

    struct SwsContext *sws_ctx = nullptr;
    struct SwrContext *swr_ctx = nullptr;
    AVAudioFifo *audio_fifo = nullptr;
};

class Muxor
{
private:
    std::string filename;
    struct OutputStream video_stream;
    struct OutputStream audio_stream;
    std::mutex mux_write_mutex;

    AVFormatContext *outputContext = nullptr;
    bool has_audio;

public:
    Muxor(std::string filename, bool has_audio);
    int init(int width, int height, AVRational frameRate);
    int close();
    int write_video_frame(AVFrame *frame, AVRational srcTimeBase);
    int write_audio_frame(AVFrame *frame);

private:
     int add_video_stream(OutputStream *stream, AVFormatContext *fmtContext, const AVCodec **codec, AVRational frameRate, AVDictionary *opt_args);
    int add_audio_stream(OutputStream *stream, AVFormatContext *fmtContext, const AVCodec **codec, enum AVCodecID codec_id, AVRational frameRate, AVDictionary *opt_args);
    int open_video(AVFormatContext *fmtContext, const AVCodec *codec, OutputStream *stream, AVDictionary *opt_args);
    int open_audio(AVFormatContext *fmtContext, const AVCodec *codec, OutputStream *stream, AVDictionary *opt_args);
    int write_frame(AVFormatContext *outputContext, AVCodecContext *codecContext, AVStream *stream, AVFrame *frame, AVPacket *packet);

    AVFrame *alloc_frame(enum AVPixelFormat pix_fmt, int width, int height);
    AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt, const AVChannelLayout *channel_layout, int sample_rate, int nb_samples);
    int close_stream(OutputStream *stream);
};
