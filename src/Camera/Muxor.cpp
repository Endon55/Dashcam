#include "Muxor.h"
#include "Macros.h"
#include <libavutil/avutil.h>
#include <libavutil/dict.h>

extern "C"
{
#include <libavutil/avassert.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>
#include <libavutil/rational.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
}
#include <spdlog/spdlog.h>
#include <mutex>
#include <string>

/*
 * the purpose of the Muxor(multiplexer) is to combine the separate audio streams and
 * video streams into one coherent box with properly aligned timestamps.
 */

Muxor::Muxor(std::string filename, bool has_audio)
{
    spdlog::debug("Filename: {}", filename.c_str());
    this->filename = std::move(filename);
    this->has_audio = has_audio;
}

int Muxor::init(int width, int height, AVRational frameRate)
{
    spdlog::debug("Initializing Muxor");
    const AVCodec *audio_codec, *video_codec = nullptr;
    int ret;
    AVDictionary *opt = nullptr;

    ret = avformat_alloc_output_context2(&outputContext, nullptr, "MP4", filename.c_str());

    if (!outputContext || ret < 0)
    {
        spdlog::critical("Failed to allocate output context memory");
        return -1;
    }

    if (outputContext->oformat->audio_codec == AV_CODEC_ID_NONE)
    {
        spdlog::critical("Output Format failed to find: Audio Codec");
        return -1;
    }
    if (outputContext->oformat->video_codec == AV_CODEC_ID_NONE)
    {
        spdlog::critical("Output Format failed to find: Video Codec");
        return -1;
    }

    audio_stream = {0};
    video_stream = {0};
    video_stream.src_time_base = AVRational{0, 1}; // set this way so that we can update it later with proper frame info
    video_stream.width = width;
    video_stream.height = height;
    audio_stream.width = width;
    audio_stream.height = height;

    spdlog::debug("Video resolution {} x {}", width, height);

    ret = add_video_stream(&video_stream, outputContext, &video_codec, frameRate, opt);
    if (ret < 0)
    {
        spdlog::critical("Failed to add video stream");
        return -1;
    }
    ret = add_audio_stream(&audio_stream, outputContext, &audio_codec, outputContext->oformat->audio_codec, AVRational{0, 1}, opt);

    if (ret < 0)
    {
        spdlog::critical("Failed to add video stream");
        return -1;
    }
    // Debug Info about the stream
    av_dump_format(outputContext, 0, filename.c_str(), 1);

    ret = avio_open(&outputContext->pb, filename.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        spdlog::critical("Failed to open output file: {}", av_err2str(ret));
        return -1;
    }

    ret = avformat_write_header(outputContext, &opt);
    if (ret < 0)
    {
        spdlog::critical("Error while writing output header: {}", av_err2str(ret));
        return -1;
    }
    return 0;
}

AVFrame *Muxor::alloc_frame(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *frame;
    int ret;
    frame = av_frame_alloc();
    if (!frame)
    {
        spdlog::critical("Failed to allocate frame memory");
        return nullptr;
    }
    frame->format = pix_fmt;
    frame->width = width;
    frame->height = height;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0)
    {
        spdlog::critical("Could not allocate frame data");
        return nullptr;
    }
    return frame;
}
AVFrame *Muxor::alloc_audio_frame(enum AVSampleFormat sample_fmt, const AVChannelLayout *channel_layout, int sample_rate, int nb_samples)
{
    AVFrame *frame;
    int ret;
    frame = av_frame_alloc();
    if (!frame)
    {
        spdlog::critical("Failed to allocate audio frame memory");
        return nullptr;
    }
    frame->format = sample_fmt;
    av_channel_layout_copy(&frame->ch_layout, channel_layout);
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples)
    {
        if (av_frame_get_buffer(frame, 0) < 0)
        {
            spdlog::critical("Could not allocate audio buffer");
            return nullptr;
        }
    }
    return frame;
}

/*
    Initializes all the stuff required to save audio data.
*/
int Muxor::open_audio(AVFormatContext *fmtContext, const AVCodec *codec, OutputStream *stream, AVDictionary *opt_args)
{
    int ret;
    int nb_samples;
    AVCodecContext *codecContext = stream->codecContext;
    AVDictionary *opt = nullptr;

    av_dict_copy(&opt, opt_args, 0);

    ret = avcodec_open2(codecContext, codec, &opt);
    if (ret < 0)
    {
        spdlog::critical("Failed to open audio codec: {}", av_err2str(ret));
        return -1;
    }

    // Signal generator
    stream->t = 0;
    stream->tincr = 2 * M_PI * 110.0 / codecContext->sample_rate;
    stream->tincr2 = 2 * M_PI * 110.0 / codecContext->sample_rate / codecContext->sample_rate;

    if (codecContext->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 1024;
    else
        nb_samples = codecContext->frame_size;
    if (nb_samples <= 0)
    {
        nb_samples = 1024;
    }

    stream->frame = alloc_audio_frame(codecContext->sample_fmt, &codecContext->ch_layout, codecContext->sample_rate, nb_samples);
    stream->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &codecContext->ch_layout, codecContext->sample_rate, nb_samples);

    ret = avcodec_parameters_from_context(stream->stream->codecpar, codecContext);
    if (ret < 0)
    {
        spdlog::critical("Failed to copy audio stream parameters");
        return -1;
    }

    // Initialize when the first captured frame arrives so input format/rate/layout are accurate.
    stream->swr_ctx = nullptr;
    stream->audio_fifo = av_audio_fifo_alloc(codecContext->sample_fmt,
                                             codecContext->ch_layout.nb_channels,
                                             nb_samples * 4);
    if (!stream->audio_fifo)
    {
        spdlog::critical("Failed to allocate audio fifo");
        return -1;
    }

    return 0;
}
/*
    Initializes all the stuff required to save video data.
*/
int Muxor::open_video(AVFormatContext *fmtContext, const AVCodec *codec, OutputStream *stream, AVDictionary *opt_args)
{
    int ret;
    AVCodecContext *codecContext = stream->codecContext;
    AVDictionary *opt = nullptr;

    av_dict_copy(&opt, opt_args, 0);
    if (codecContext->codec_id == AV_CODEC_ID_H264 || codecContext->codec_id == AV_CODEC_ID_HEVC)
    {
        av_dict_set(&opt, "preset", "veryfast", 0);
        av_dict_set(&opt, "crf", "18", 0);
    }

    ret = avcodec_open2(codecContext, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0)
    {
        spdlog::critical("Failed to open video codec: {}", av_err2str(ret));
        return -1;
    }
    stream->frame = alloc_frame(codecContext->pix_fmt, codecContext->width, codecContext->height);
    if (!stream->frame)
    {
        spdlog::critical("Failed to initialize frame");
        return -1;
    }
    stream->tmp_frame = nullptr;
    if (codecContext->pix_fmt != AV_PIX_FMT_YUV420P)
    {
        stream->tmp_frame = alloc_frame(AV_PIX_FMT_YUV420P, codecContext->width, codecContext->height);
        if (!stream->tmp_frame)
        {
            spdlog::critical("Failed to initialize temp frame");
            return -1;
        }
    }

    ret = avcodec_parameters_from_context(stream->stream->codecpar, codecContext);
    if (ret < 0)
    {
        spdlog::critical("Failed to copy stream parameters");
        return -1;
    }

    return 0;
}

int Muxor::write_audio_frame(AVFrame *frame)
{
    int ret;
    if (frame)
    {
        const int outSampleRate = audio_stream.codecContext->sample_rate;
        const int inSampleRate = frame->sample_rate > 0 ? frame->sample_rate : outSampleRate;
        AVChannelLayout inLayout = frame->ch_layout;
        if (inLayout.nb_channels <= 0)
        {
            av_channel_layout_copy(&inLayout, &audio_stream.codecContext->ch_layout);
        }

        if (!audio_stream.swr_ctx)
        {
            ret = swr_alloc_set_opts2(&audio_stream.swr_ctx,
                                      &audio_stream.codecContext->ch_layout,
                                      audio_stream.codecContext->sample_fmt,
                                      outSampleRate,
                                      &inLayout,
                                      (AVSampleFormat)frame->format,
                                      inSampleRate,
                                      0,
                                      nullptr);
            if (ret < 0 || !audio_stream.swr_ctx)
            {
                spdlog::critical("Failed to allocate mux audio resampler: {}", av_err2str(ret));
                av_channel_layout_uninit(&inLayout);
                return -1;
            }

            ret = swr_init(audio_stream.swr_ctx);
            if (ret < 0)
            {
                spdlog::critical("Failed to initialize mux audio resampler: {}", av_err2str(ret));
                av_channel_layout_uninit(&inLayout);
                return -1;
            }
        }

        av_channel_layout_uninit(&inLayout);

        int dst_nb_samples = av_rescale_rnd(swr_get_delay(audio_stream.swr_ctx, inSampleRate) + frame->nb_samples,
                                            outSampleRate,
                                            inSampleRate,
                                            AV_ROUND_UP);
        if (dst_nb_samples <= 0)
        {
            return 0;
        }

        const int outChannels = audio_stream.codecContext->ch_layout.nb_channels;
        uint8_t **convertedData = nullptr;
        ret = av_samples_alloc_array_and_samples(&convertedData,
                                                 nullptr,
                                                 outChannels,
                                                 dst_nb_samples,
                                                 audio_stream.codecContext->sample_fmt,
                                                 0);
        if (ret < 0)
        {
            spdlog::critical("Failed to allocate converted audio samples: {}", av_err2str(ret));
            return -1;
        }

        ret = swr_convert(audio_stream.swr_ctx,
                          convertedData,
                          dst_nb_samples,
                          (const uint8_t **)frame->extended_data,
                          frame->nb_samples);
        if (ret < 0)
        {
            spdlog::critical("Failed convert swr");
            if (convertedData)
            {
                av_freep(&convertedData[0]);
                av_freep(&convertedData);
            }
            return -1;
        }

        const int convertedSamples = ret;
        const int fifoSpaceNeeded = av_audio_fifo_size(audio_stream.audio_fifo) + convertedSamples;
        ret = av_audio_fifo_realloc(audio_stream.audio_fifo, fifoSpaceNeeded);
        if (ret < 0)
        {
            spdlog::critical("Failed to grow audio fifo: {}", av_err2str(ret));
            if (convertedData)
            {
                av_freep(&convertedData[0]);
                av_freep(&convertedData);
            }
            return -1;
        }

        ret = av_audio_fifo_write(audio_stream.audio_fifo, (void **)convertedData, convertedSamples);
        if (ret < convertedSamples)
        {
            spdlog::critical("Failed to write converted audio to fifo");
            if (convertedData)
            {
                av_freep(&convertedData[0]);
                av_freep(&convertedData);
            }
            return -1;
        }

        if (convertedData)
        {
            av_freep(&convertedData[0]);
            av_freep(&convertedData);
        }
    }

    const int encoderFrameSize = audio_stream.codecContext->frame_size > 0 ? audio_stream.codecContext->frame_size : 1024;
    bool draining = (frame == nullptr);
    while (av_audio_fifo_size(audio_stream.audio_fifo) >= encoderFrameSize || (draining && av_audio_fifo_size(audio_stream.audio_fifo) > 0))
    {
        int nbOut = encoderFrameSize;
        if (draining)
        {
            nbOut = FFMIN(av_audio_fifo_size(audio_stream.audio_fifo), encoderFrameSize);
        }

        av_frame_unref(audio_stream.frame);
        audio_stream.frame->nb_samples = nbOut;
        audio_stream.frame->format = audio_stream.codecContext->sample_fmt;
        audio_stream.frame->sample_rate = audio_stream.codecContext->sample_rate;
        ret = av_channel_layout_copy(&audio_stream.frame->ch_layout, &audio_stream.codecContext->ch_layout);
        if (ret < 0)
        {
            spdlog::critical("Failed to set mux audio channel layout: {}", av_err2str(ret));
            return -1;
        }

        ret = av_frame_get_buffer(audio_stream.frame, 0);
        if (ret < 0)
        {
            spdlog::critical("Failed to allocate mux audio frame buffer: {}", av_err2str(ret));
            return -1;
        }

        ret = av_frame_make_writable(audio_stream.frame);
        if (ret < 0)
        {
            spdlog::critical("Failed to make frame writable: {}", av_err2str(ret));
            return -1;
        }

        ret = av_audio_fifo_read(audio_stream.audio_fifo, (void **)audio_stream.frame->data, nbOut);
        if (ret < nbOut)
        {
            spdlog::critical("Failed to read enough samples from audio fifo");
            return -1;
        }

        audio_stream.frame->pts = av_rescale_q(audio_stream.samples_count,
                                               (AVRational){1, audio_stream.codecContext->sample_rate},
                                               audio_stream.codecContext->time_base);
        audio_stream.samples_count += nbOut;

        ret = write_frame(outputContext,
                          audio_stream.codecContext,
                          audio_stream.stream,
                          audio_stream.frame,
                          audio_stream.tmp_packet);
        if (ret < 0)
        {
            return ret;
        }
    }

    if (!frame)
    {
        return write_frame(outputContext, audio_stream.codecContext, audio_stream.stream, nullptr, audio_stream.tmp_packet);
    }

    return 0;
}

int Muxor::write_video_frame(AVFrame *frame, AVRational srcTimeBase)
{
    if (!frame)
    {
        return 0;
    }

    if (srcTimeBase.num > 0 && srcTimeBase.den > 0)
    {
        video_stream.src_time_base = srcTimeBase;
    }

    if (frame->pts != AV_NOPTS_VALUE && video_stream.src_time_base.num > 0 && video_stream.src_time_base.den > 0)
    {
        frame->pts = av_rescale_q(frame->pts, video_stream.src_time_base, video_stream.codecContext->time_base);
        if (video_stream.next_pts > 0 && frame->pts < video_stream.next_pts)
        {
            frame->pts = video_stream.next_pts;
        }
        video_stream.next_pts = frame->pts + 1;
    }
    else
    {
        frame->pts = video_stream.next_pts++;
    }

    return write_frame(outputContext, video_stream.codecContext, video_stream.stream, frame, video_stream.tmp_packet);
}

int Muxor::write_frame(AVFormatContext *outputContext, AVCodecContext *codecContext, AVStream *stream, AVFrame *frame, AVPacket *packet)
{
    std::lock_guard<std::mutex> lock(mux_write_mutex);

    int ret = avcodec_send_frame(codecContext, frame);
    if (ret < 0)
    {
        spdlog::critical("Failed to send frame to the encoder: {}", av_err2str(ret));
        return -1;
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_packet(codecContext, packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        else if (ret < 0)
        {
            spdlog::critical("Failed recieve packet from encoder: {}", av_err2str(ret));
            return -1;
        }

        av_packet_rescale_ts(packet, codecContext->time_base, stream->time_base);
        packet->stream_index = stream->index;
        ret = av_interleaved_write_frame(outputContext, packet);
        if (ret < 0)
        {
            spdlog::critical("Failed to write output packet: {}", av_err2str(ret));
            return -1;
        }
        av_packet_unref(packet);
    }

    return 0;
}
/*
    Takes in a locally defined OutputStream struct to hold all the values for the stream. Fmt Context will be the same object between video and audio streams. Codec is the a pointer to where you want the value of codec to be stored and Codec ID is the value of the codec you want fetched and stored.
*/
int Muxor::add_audio_stream(OutputStream *stream, AVFormatContext *fmtContext, const AVCodec **codec, enum AVCodecID codec_id, AVRational frameRate, AVDictionary *opt_args)
{
    AVCodecContext *codecContext;
    int ret = 0;

    
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec))
    {
        spdlog::critical("Failed to locate codec: {}", avcodec_get_name(codec_id));
        return -1;
    }

    spdlog::debug("Audio Codec: {}", avcodec_get_name(codec_id));
    stream->tmp_packet = av_packet_alloc();
    if (!stream->tmp_packet)
    {
        spdlog::critical("Failed to allocate memory for packet");
        return -1;
    }
    // Adding a stream to the context
    stream->stream = avformat_new_stream(fmtContext, nullptr);
    if (!stream->stream)
    {
        spdlog::critical("Failed to allocate memory for stream");
        return -1;
    }

    stream->stream->id = fmtContext->nb_streams - 1;
    codecContext = avcodec_alloc_context3(*codec);
    if (!codecContext)
    {
        spdlog::critical("Failed to allocate memory for codec context");
        return -1;
    }

    stream->codecContext = codecContext;

    const AVSampleFormat *sample_fmt;
    ret = avcodec_get_supported_config(codecContext, *codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, (const void **)&sample_fmt, nullptr);
    codecContext->sample_fmt = ret >= 0 ? sample_fmt[0] : AV_SAMPLE_FMT_FLTP;
    codecContext->bit_rate = 64000;
    codecContext->sample_rate = 44100;
    int *sample_rate;
    ret = avcodec_get_supported_config(codecContext, *codec, AV_CODEC_CONFIG_SAMPLE_RATE, 0, (const void **)&sample_rate, nullptr);
    if (ret >= 0)
    {
        codecContext->sample_rate = sample_rate[0];
        for (int i = 0; sample_rate[i]; i++)
        {
            if (sample_rate[i] == 44100)
            {
                codecContext->sample_rate = 44100;
            }
        }
    }
    // Cant inline this without compiler getting mad
    AVChannelLayout channel_layout = AV_CHANNEL_LAYOUT_STEREO;
    av_channel_layout_copy(&codecContext->ch_layout, &channel_layout);

    stream->stream->time_base = (AVRational){1, codecContext->sample_rate};
    codecContext->time_base = stream->stream->time_base;

    /*some formats get fucky*/
    if (fmtContext->oformat->flags & AVFMT_GLOBALHEADER)
    {
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    spdlog::debug("Codec: {}", codecContext->codec->long_name);

    int nb_samples;
    AVDictionary *opt = nullptr;

    av_dict_copy(&opt, opt_args, 0);

    ret = avcodec_open2(codecContext, *codec, &opt);
    if (ret < 0)
    {
        spdlog::critical("Failed to open audio codec: {}", av_err2str(ret));
        return -1;
    }

    // Signal generator
    stream->t = 0;
    stream->tincr = 2 * M_PI * 110.0 / codecContext->sample_rate;
    stream->tincr2 = 2 * M_PI * 110.0 / codecContext->sample_rate / codecContext->sample_rate;

    if (codecContext->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 1024;
    else
        nb_samples = codecContext->frame_size;
    if (nb_samples <= 0)
    {
        nb_samples = 1024;
    }

    stream->frame = alloc_audio_frame(codecContext->sample_fmt, &codecContext->ch_layout, codecContext->sample_rate, nb_samples);
    stream->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, &codecContext->ch_layout, codecContext->sample_rate, nb_samples);

    ret = avcodec_parameters_from_context(stream->stream->codecpar, codecContext);
    if (ret < 0)
    {
        spdlog::critical("Failed to copy audio stream parameters");
        return -1;
    }

    // Initialize when the first captured frame arrives so input format/rate/layout are accurate.
    stream->swr_ctx = nullptr;
    stream->audio_fifo = av_audio_fifo_alloc(codecContext->sample_fmt,
                                             codecContext->ch_layout.nb_channels,
                                             nb_samples * 4);
    if (!stream->audio_fifo)
    {
        spdlog::critical("Failed to allocate audio fifo");
        return -1;
    }

    return 0;
}

int Muxor::add_video_stream(OutputStream *stream, AVFormatContext *fmtContext, const AVCodec **codec, AVRational frameRate, AVDictionary *opt_args)
{
    *codec = avcodec_find_encoder_by_name("libx264");


    if (!*codec)
    {
        spdlog::critical("Failed to find the H.264 codec.");
        return -1;
    }

    AVCodecID codec_id = (*codec)->id;
    AVCodecContext *codecContext;
    int ret = 0;

    spdlog::debug("Codec: {}", avcodec_get_name(codec_id));
    stream->tmp_packet = av_packet_alloc();
    if (!stream->tmp_packet)
    {
        spdlog::critical("Failed to allocate memory for packet");
        return -1;
    }
    // Adding a video stream to the context
    stream->stream = avformat_new_stream(fmtContext, nullptr);
    if (!stream->stream)
    {
        spdlog::critical("Failed to allocate memory for stream");
        return -1;
    }

    stream->stream->id = fmtContext->nb_streams - 1;
    codecContext = avcodec_alloc_context3(*codec);
    if (!codecContext)
    {
        spdlog::critical("Failed to allocate memory for codec context");
        return -1;
    }

    stream->codecContext = codecContext;

    AVRational targetFrameRate = frameRate;
    if (targetFrameRate.num <= 0 || targetFrameRate.den <= 0)
    {
        targetFrameRate = AVRational{30, 1};
    }

    codecContext->codec_id = codec_id;
    codecContext->width = stream->width;
    codecContext->height = stream->height;
    codecContext->framerate = targetFrameRate;
    stream->stream->avg_frame_rate = targetFrameRate;
    stream->stream->time_base = av_inv_q(targetFrameRate);
    codecContext->time_base = stream->stream->time_base;

    codecContext->gop_size = 10 * stream->stream->time_base.den;  // approx 10 seconds between key frames.
    codecContext->keyint_min = 1 * stream->stream->time_base.den; // min number of derived frames between key frames.
    codecContext->max_b_frames = 0;                               // b_frames are the most compressable but they lost the most detail.

    codecContext->pix_fmt = AV_PIX_FMT_YUV420P;

    /*adds headers once to the container rather than to every frame*/
    if (fmtContext->oformat->flags & AVFMT_GLOBALHEADER)
    {
        codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    AVDictionary *opt = nullptr;

    av_dict_copy(&opt, opt_args, 0);
    codecContext->flags |= AV_CODEC_FLAG_QSCALE; //Sets the encoder to use a consistent quality for every frame rather than an setting an average quality across all frames.
    codecContext->global_quality = 1 * FF_QP2LAMBDA;
    // // Realtime-friendly defaults for software HEVC on CPU-only systems.
    av_dict_set(&opt, "preset", "ultrafast", 0);
    av_dict_set(&opt, "tune", "zerolatency", 0);
    //av_dict_set(&opt, "crf", "28", 0);

    ret = avcodec_open2(codecContext, *codec, &opt);
    av_dict_free(&opt);
    if (ret < 0)
    {
        spdlog::critical("Failed to open video codec: {}", av_err2str(ret));
        return -1;
    }
    stream->frame = alloc_frame(codecContext->pix_fmt, codecContext->width, codecContext->height);
    if (!stream->frame)
    {
        spdlog::critical("Failed to initialize frame");
        return -1;
    }
    stream->tmp_frame = nullptr;
    if (codecContext->pix_fmt != AV_PIX_FMT_YUV420P)
    {
        stream->tmp_frame = alloc_frame(AV_PIX_FMT_YUV420P, codecContext->width, codecContext->height);
        if (!stream->tmp_frame)
        {
            spdlog::critical("Failed to initialize temp frame");
            return -1;
        }
    }

    ret = avcodec_parameters_from_context(stream->stream->codecpar, codecContext);
    if (ret < 0)
    {
        spdlog::critical("Failed to copy stream parameters");
        return -1;
    }

    return 0;
}

int Muxor::close()
{
    if (!outputContext)
    {
        return 0;
    }

    int ret = 0;
    if (outputContext->pb != nullptr)
    {
        int audioFlushRet = write_audio_frame(nullptr);
        if (audioFlushRet < 0)
        {
            spdlog::critical("Failed to flush audio encoder");
            ret = audioFlushRet;
        }

        int videoFlushRet = write_frame(outputContext, video_stream.codecContext, video_stream.stream, nullptr, video_stream.tmp_packet);
        if (videoFlushRet < 0)
        {
            spdlog::critical("Failed to flush video encoder");
            if (ret == 0)
            {
                ret = videoFlushRet;
            }
        }

        ret = av_write_trailer(outputContext);
        if (ret < 0)
        {
            spdlog::critical("Failed to write trailer: {}", av_err2str(ret));
        }
    }

    close_stream(&video_stream);
    close_stream(&audio_stream);

    if (!(outputContext->oformat->flags & AVFMT_NOFILE) && outputContext->pb != nullptr)
    {
        avio_closep(&outputContext->pb);
    }

    avformat_free_context(outputContext);
    outputContext = nullptr;

    return ret < 0 ? ret : 0;
}

int Muxor::close_stream(OutputStream *stream)
{
    if (stream == nullptr)
    {
        return 0;
    }

    if (stream->frame)
    {
        av_frame_free(&stream->frame);
    }
    if (stream->tmp_frame)
    {
        av_frame_free(&stream->tmp_frame);
    }
    if (stream->tmp_packet)
    {
        av_packet_free(&stream->tmp_packet);
    }
    if (stream->swr_ctx)
    {
        swr_free(&stream->swr_ctx);
    }
    if (stream->audio_fifo)
    {
        av_audio_fifo_free(stream->audio_fifo);
        stream->audio_fifo = nullptr;
    }
    if (stream->sws_ctx)
    {
        sws_freeContext(stream->sws_ctx);
        stream->sws_ctx = nullptr;
    }
    if (stream->codecContext)
    {
        avcodec_free_context(&stream->codecContext);
    }

    return 0;
}
