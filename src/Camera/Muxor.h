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
}
#include <mutex>
#include <string>


struct OutputStream
{
    AVStream *stream;
    AVCodecContext *codecContext;

    int64_t next_pts;
    int samples_count;
    AVRational src_time_base;
    AVFrame *frame;
    AVFrame *tmp_frame;
    int width, height;

    AVPacket *tmp_packet;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
    AVAudioFifo *audio_fifo;
};

class Muxor
{
private:
    std::string filename;
    struct OutputStream video_stream;
    struct OutputStream audio_stream;
    std::mutex mux_write_mutex;

    const AVOutputFormat *fmt;
    AVFormatContext *outputContext;

public:
    Muxor(std::string filename);
    int init(int width, int height, AVRational frameRate);
    int close();
    int write_video_frame(AVFrame *frame, AVRational srcTimeBase);
    int write_audio_frame(AVFrame *frame);

private:
    int add_stream(OutputStream *stream, AVFormatContext *fmtContext, const AVCodec **codec, enum AVCodecID codec_id, AVRational frameRate);
    int open_video(AVFormatContext *fmtContext, const AVCodec *codec, OutputStream *stream, AVDictionary *opt_args);
    int open_audio(AVFormatContext *fmtContext, const AVCodec *codec, OutputStream *stream, AVDictionary *opt_args);
    int write_frame(AVFormatContext *outputContext, AVCodecContext *codecContext, AVStream *stream, AVFrame *frame, AVPacket *packet);

    AVFrame *alloc_frame(enum AVPixelFormat pix_fmt, int width, int height);
    AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt, const AVChannelLayout *channel_layout, int sample_rate, int nb_samples);
    int close_stream(OutputStream *stream);
};
