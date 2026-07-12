#include "Webcam.h"
#include "../Settings.h"
#include "Macros.h"

#include <cstdlib>
#include <string>
#include <stdio.h>
#include <chrono>
#include <string>
#include <stdio.h>
#include <spdlog/spdlog.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
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



Webcam::Webcam(cam_device *camera) : has_audio(camera->audioCard != nullptr)
{
    this->device = camera;
    this->mute.store(Settings::isMuted());
    spdlog::debug("Has audio: {}", has_audio);
}

int Webcam::init(int camIndex)
{
    avdevice_register_all();
    int ret;
    spdlog::debug("Initializing Webcam: {}", device->videoPath);

    ret = initVideo();
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize video");
        return ret;
    }
    if (has_audio)
    {
        ret = initAudio();
        if (ret < 0)
        {
            spdlog::critical("Failed to initialize audio");
            return ret;
        }
    }

    AVRational captureFrameRate = video.fmtContext->streams[video.stream_index]->avg_frame_rate;
    if (captureFrameRate.num <= 0 || captureFrameRate.den <= 0)
    {
        captureFrameRate = video.fmtContext->streams[video.stream_index]->r_frame_rate;
    }
    if (captureFrameRate.num <= 0 || captureFrameRate.den <= 0)
    {
        captureFrameRate = AVRational{30, 1};
    }

    time_t timestamp = time({});
    char timeString[std::size("yyyy-mm-ddThh:mm:ssZ") + 4];
    char *ptr = &timeString[0];

    snprintf(ptr, 10, "c%02d-", camIndex);
    strftime(ptr + 4, std::size(timeString) - 5, "%FT%TZ", localtime(&timestamp));

    spdlog::critical("Timestamp: {}", timeString);
    //muxor = new Muxor("/home/anthony/Desktop/Dashcam/c01.mp4");
    muxor = new Muxor(Settings::getVideoSaveDir().string() + "/" + timeString + ".mp4");
    ret = muxor->init(video.codecParams->width, video.codecParams->height, captureFrameRate);
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize muxor");
        return ret;
    }
    initialized = true;
    return ret;
}

int Webcam::initVideo()
{
    spdlog::debug("Initializing Video");
    video = {};
    int ret = 0;
    video.stream_index = -1;
    video.fmtContext = avformat_alloc_context();
    if (!video.fmtContext)
    {
        spdlog::critical("Failed to allocate memory for av format context");
        return -1;
    }
    const AVInputFormat *inputFormat = av_find_input_format("v4l2");
    if (!inputFormat)
    {
        spdlog::critical("Couldn't load input format v4l2");
        return -1;
    }

    AVDictionary *videoInputOptions = nullptr;

    char videoSize[32];
    snprintf(videoSize, sizeof(videoSize), "%dx%d", device->default_mode->width, device->default_mode->height);
    av_dict_set(&videoInputOptions, "video_size", videoSize, 0);
    if (device->default_mode->fps > 0.0)
    {
        char frameRate[16];
        int stableFps = static_cast<int>(std::round(device->default_mode->fps));
        snprintf(frameRate, sizeof(frameRate), "%d", stableFps);
        av_dict_set(&videoInputOptions, "framerate", frameRate, 0);
    }

    av_dict_set(&videoInputOptions, "input_format", device->default_mode->pixelFmtStr, 0);

    spdlog::info("Requesting webcam mode {} @ {:.2f} fps ({})",
                 videoSize,
                 device->default_mode->fps,
                 device->default_mode->pixelFmtStr);

    spdlog::info("Opening Input");
    ret = avformat_open_input(&video.fmtContext, device->videoPath, inputFormat, &videoInputOptions);
    spdlog::info("Freeing Dict");
    av_dict_free(&videoInputOptions);
    if (ret < 0)
    {
        spdlog::critical("Error opening av format: {}", av_err2str(ret));
        return ret;
    }

    video.frame = av_frame_alloc();
    if (!video.frame)
    {
        spdlog::critical("Failed to allocate memory for frame");
        return -1;
    }
    video.yuv_frame = av_frame_alloc();
    if (!video.yuv_frame)
    {
        spdlog::critical("Failed to allocate memory for yuv frame");
        return -1;
    }
    video.filtered_frame = av_frame_alloc();
    if (!video.filtered_frame)
    {
        spdlog::critical("Failed to allocate memory for filtered frame");
        return -1;
    }
    video.packet = av_packet_alloc();
    if (!video.packet)
    {
        spdlog::critical("Failed to allocate memory for packet");
        return -1;
    }
    video.filterGraph = avfilter_graph_alloc();
    if (!video.filterGraph)
    {
        spdlog::critical("Failed to allocate memory for filter graph");
        return -1;
    }

    ret = avformat_find_stream_info(video.fmtContext, nullptr);
    if (ret < 0)
    {
        spdlog::warn("Unable to fully probe stream info, continuing with available metadata: {}", av_err2str(ret));
    }
    if (video.fmtContext->nb_streams == 0)
    {
        spdlog::critical("No streams were reported by the video device");
        return -1;
    }
    spdlog::debug("Video Streams: {}", video.fmtContext->nb_streams);
    for (int i = 0; i < video.fmtContext->nb_streams; i++)
    {
        spdlog::debug("Stream {}", i);
        AVCodecParameters *pLocalCodecParameters = nullptr;
        pLocalCodecParameters = video.fmtContext->streams[i]->codecpar;
        spdlog::debug("AVStream->time_base before open coded {}/{}", video.fmtContext->streams[i]->time_base.num, video.fmtContext->streams[i]->time_base.den);
        spdlog::debug("AVStream->r_frame_rate before open coded {}/{}", video.fmtContext->streams[i]->r_frame_rate.num, video.fmtContext->streams[i]->r_frame_rate.den);
        spdlog::debug("AVStream->start_time {}" PRId64, video.fmtContext->streams[i]->start_time);
        spdlog::debug("AVStream->duration {}" PRId64, video.fmtContext->streams[i]->duration);
        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == nullptr)
        {
            spdlog::debug("Failed to find local codec: {}", i);
            continue;
        }
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            if (video.stream_index == -1)
            {
                video.stream_index = i;
                video.codecParams = pLocalCodecParameters;
                video.codec = pLocalCodec;
                AVDictionary *dict = video.fmtContext->streams[i]->metadata;
                const AVDictionaryEntry *entry = nullptr;
                spdlog::debug("Video Stream Metadata");
                for (int j = 0; j < av_dict_count(dict); j++)
                {
                    entry = av_dict_iterate(dict, entry);

                    spdlog::debug("Key: {}, Value: {}", entry->key, entry->value);
                }
            }
            spdlog::debug("Video Codec: resolution {} x {}", pLocalCodecParameters->width, pLocalCodecParameters->height);
        }
        else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            spdlog::critical("Somehow an audio stream got mixed with the video streams");
            return -1;
        }

        /* spdlog::debug("\tCodec %s ID %d bit_rate %d", pLocalCodec->name, (int)(pLocalCodec->id), pLocalCodecParameters->bit_rate); */
    }

    if (video.stream_index == -1)
    {
        spdlog::critical("Couldn't find a video stream");
        return -1;
    }
    video.codecContext = avcodec_alloc_context3(video.codec);
    if (!video.codecContext)
    {
        spdlog::critical("failed to allocate memory for video AVCodecContext");
        return -1;
    }

    if (avcodec_parameters_to_context(video.codecContext, video.codecParams) < 0)
    {
        spdlog::critical("failed to assign codec params to video codec context");
        return -1;
    }

    if (avcodec_open2(video.codecContext, video.codec, nullptr) < 0)
    {
        spdlog::critical("failed to open the video codec");
        return -1;
    }

    const AVColorRange targetColorRange =
        video.codecContext->color_range != AVCOL_RANGE_UNSPECIFIED ? video.codecContext->color_range : AVCOL_RANGE_MPEG;
    const AVColorSpace targetColorSpace =
        video.codecContext->colorspace != AVCOL_SPC_UNSPECIFIED ? video.codecContext->colorspace : AVCOL_SPC_BT709;
    AVRational targetPixelAspect = video.codecContext->sample_aspect_ratio;
    if (targetPixelAspect.num <= 0 || targetPixelAspect.den <= 0)
    {
        targetPixelAspect = AVRational{1, 1};
    }

    // Create buffer source args with proper format
    char args[512];
    snprintf(args, sizeof(args),
             "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
             video.codecContext->width, video.codecContext->height, AV_PIX_FMT_YUV420P,
             video.fmtContext->streams[video.stream_index]->time_base.num,
             video.fmtContext->streams[video.stream_index]->time_base.den,
             video.codecContext->sample_aspect_ratio.num,
             video.codecContext->sample_aspect_ratio.den);

    ret = avfilter_graph_create_filter(&video.sourceFilter, avfilter_get_by_name("buffer"), "in", args, nullptr, video.filterGraph);
    if (ret < 0)
    {
        spdlog::critical("failed to create buffer source: {}", av_err2str(ret));
        return -1;
    }

    AVBufferSrcParameters *srcParams = av_buffersrc_parameters_alloc();
    if (!srcParams)
    {
        spdlog::critical("failed to allocate buffer source parameters");
        return -1;
    }
    srcParams->format = AV_PIX_FMT_YUV420P;
    srcParams->width = video.codecContext->width;
    srcParams->height = video.codecContext->height;
    srcParams->time_base = video.fmtContext->streams[video.stream_index]->time_base;
    srcParams->sample_aspect_ratio = targetPixelAspect;
    srcParams->color_space = targetColorSpace;
    srcParams->color_range = targetColorRange;

    ret = av_buffersrc_parameters_set(video.sourceFilter, srcParams);
    av_free(srcParams);
    if (ret < 0)
    {
        spdlog::critical("failed to set buffer source parameters: {}", av_err2str(ret));
        return -1;
    }

    video.sinkFilter = avfilter_graph_alloc_filter(video.filterGraph, avfilter_get_by_name("buffersink"), "out");
    if (!video.sinkFilter)
    {
        spdlog::critical("failed to allocate buffer sink");
        return -1;
    }

    ret = avfilter_init_str(video.sinkFilter, nullptr);
    if (ret < 0)
    {
        spdlog::critical("failed to initialize buffer sink: {}", av_err2str(ret));
        return -1;
    }

    video.flipFilter = avfilter_graph_alloc_filter(video.filterGraph, avfilter_get_by_name("hflip"), "hflip");
    if (!video.flipFilter)
    {
        spdlog::critical("failed to allocate filter context");
        return -1;
    }

    ret = avfilter_init_str(video.flipFilter, nullptr);
    if (ret < 0)
    {
        spdlog::critical("failed to initialize filter: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_link(video.sourceFilter, 0, video.flipFilter, 0);
    if (ret < 0)
    {
        spdlog::critical("failed to link source to filter: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_link(video.flipFilter, 0, video.sinkFilter, 0);
    if (ret < 0)
    {
        spdlog::critical("failed to link filter to sink: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_graph_config(video.filterGraph, nullptr);
    if (ret < 0)
    {
        spdlog::critical("failed to configure filter graph: {}", av_err2str(ret));
        return -1;
    }

    video.yuv_frame->format = AV_PIX_FMT_YUV420P;
    video.yuv_frame->width = video.codecContext->width;
    video.yuv_frame->height = video.codecContext->height;
    if (av_frame_get_buffer(video.yuv_frame, 0) < 0)
    {
        spdlog::critical("failed to allocate YUV frame buffer");
        return -1;
    }

    return 0;
}

int Webcam::initAudio()
{
    spdlog::info("Initializing Audio");
    audio = {};
    int ret = 0;
    audio.stream_index = -1;
    audio.out_ch_layout = {};
    audio.out_buf_size = 0;
    audio.fmtContext = avformat_alloc_context();
    if (!audio.fmtContext)
    {
        spdlog::critical("Failed to allocate memory for av format context");
        return -1;
    }

    const AVInputFormat *inputFormat = av_find_input_format("alsa");
    if (!inputFormat)
    {
        spdlog::critical("ALSA audio not available");
        return -1;
    }

    snd_pcm_t * handle;
    ret = snd_pcm_open(&handle, device->hw, SND_PCM_STREAM_CAPTURE, 0);
    if(ret < 0)
    {
        spdlog::critical("Failed to open device ({}) in alsa: {}", device->hw, snd_strerror(ret));
        return ret;
    }
    
    snd_pcm_hw_params_t *params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    unsigned int min_channels, max_channels;
    snd_pcm_hw_params_get_channels_min(params, &min_channels);
    snd_pcm_hw_params_get_channels_max(params, &max_channels);

    spdlog::debug("Webcam Audio Channels Min: {}, Max: {}", min_channels, max_channels);

    unsigned int min_rate, max_rate;
    snd_pcm_hw_params_get_rate_min(params, &min_rate, nullptr);
    snd_pcm_hw_params_get_rate_max(params, &max_rate, nullptr);
    spdlog::debug("Webcam Audio Rate Min: {}, Max: {}", min_rate, max_rate);

    snd_pcm_close(handle);

    
    AVDictionary *options = nullptr;
    
    char *sample_rate = (char*) malloc(sizeof(char) * 20);
    char *channels = (char*) malloc(sizeof(char) * 10);

    snprintf(sample_rate, 20, "%u", max_rate);
    
    snprintf(channels, 20, "%u", max_channels);

    av_dict_set(&options, "sample_rate", sample_rate, 0);
    av_dict_set(&options, "channels", channels, 0);

    ret = avformat_open_input(&audio.fmtContext, device->hw, inputFormat, &options);
    av_dict_free(&options);
    if (ret < 0)
    {
        spdlog::critical("Failed to open hw:{},{}: {}", *device->audioCard, *device->audioDevice, av_err2str(ret));
        return -1;
    }

    audio.frame = av_frame_alloc();
    if (!audio.frame)
    {
        spdlog::critical("Failed to allocate memory for frame");
        return -1;
    }
    audio.packet = av_packet_alloc();
    if (!audio.packet)
    {
        spdlog::critical("Failed to allocate memory for packet");
        return -1;
    }
    spdlog::debug("NB of streams: {}", audio.fmtContext->nb_streams);
    for (int i = 0; i < audio.fmtContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters = nullptr;
        pLocalCodecParameters = audio.fmtContext->streams[i]->codecpar;
        spdlog::debug("AVStream->time_base before open coded {}/{}", audio.fmtContext->streams[i]->time_base.num, audio.fmtContext->streams[i]->time_base.den);
        spdlog::debug("AVStream->r_frame_rate before open coded {}/{}", audio.fmtContext->streams[i]->r_frame_rate.num, audio.fmtContext->streams[i]->r_frame_rate.den);
        spdlog::debug("AVStream->start_time {}" PRId64, audio.fmtContext->streams[i]->start_time);
        spdlog::debug("AVStream->duration {}" PRId64, audio.fmtContext->streams[i]->duration);

        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == nullptr)
        {
            spdlog::critical("Failed to find local codec");
            continue;
        }
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            if (audio.stream_index == -1)
            {
                audio.stream_index = i;
                audio.codecParams = pLocalCodecParameters;
                audio.codec = pLocalCodec;

                if (ret < 0)
                {
                    spdlog::critical("failed to load audio spec");
                    return -1;
                }
            }

            spdlog::debug("Audio Codec: {}, Channels: {}, Sample rate: {}", pLocalCodec->name, pLocalCodecParameters->ch_layout.nb_channels, pLocalCodecParameters->sample_rate);
        }
    }

    if (audio.stream_index == -1)
    {
        spdlog::critical("Couldn't find an audio stream");
        return -1;
    }

    audio.codecContext = avcodec_alloc_context3(audio.codec);
    if (!audio.codecContext)
    {
        spdlog::critical("Failed to allocate memory for AVCodecContext");
        return -1;
    }

    if (avcodec_parameters_to_context(audio.codecContext, audio.codecParams) < 0)
    {
        spdlog::critical("Failed to assign codec params to codec context");
        return -1;
    }

    if (avcodec_open2(audio.codecContext, audio.codec, nullptr) < 0)
    {
        spdlog::critical("Failed to open the codec");
        return -1;
    }

    if (audio.codecContext->sample_rate <= 0)
    {
        audio.codecContext->sample_rate = 48000;
    }
    if (audio.codecContext->ch_layout.nb_channels <= 0)
    {
        av_channel_layout_default(&audio.codecContext->ch_layout, 2);
    }

    av_channel_layout_copy(&audio.out_ch_layout, &audio.codecContext->ch_layout);
    ret = swr_alloc_set_opts2(&audio.swr_ctx,
                              &audio.out_ch_layout,
                              AV_SAMPLE_FMT_S16,
                              audio.codecContext->sample_rate,
                              &audio.codecContext->ch_layout,
                              audio.codecContext->sample_fmt,
                              audio.codecContext->sample_rate,
                              0,
                              nullptr);
    if (ret < 0 || !audio.swr_ctx)
    {
        spdlog::critical("failed to allocate swr context: %s", av_err2str(ret));
        return -1;
    }
    if (swr_init(audio.swr_ctx) < 0)
    {
        spdlog::critical("failed to init swr context");
        return -1;
    }

    audio.stream = audio.fmtContext->streams[audio.stream_index];

    return 0;
}

int Webcam::processVideoFrame(SDL_Texture *texture)
{
    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to use it.");
        return -1;
    }
    // Keep each submitted frame aligned with the source filter's negotiated properties.
    const AVColorRange targetColorRange =
        video.codecContext->color_range != AVCOL_RANGE_UNSPECIFIED ? video.codecContext->color_range : AVCOL_RANGE_MPEG;
    const AVColorSpace targetColorSpace =
        video.codecContext->colorspace != AVCOL_SPC_UNSPECIFIED ? video.codecContext->colorspace : AVCOL_SPC_BT709;
    AVRational targetPixelAspect = video.codecContext->sample_aspect_ratio;
    if (targetPixelAspect.num <= 0 || targetPixelAspect.den <= 0)
    {
        targetPixelAspect = AVRational{1, 1};
    }

    int ret = av_read_frame(video.fmtContext, video.packet);
    if (ret < 0)
    {
        spdlog::debug("No more video frames");
        return 1;
    }

    if (video.packet->stream_index != video.stream_index)
    {
        av_packet_unref(video.packet);
        return 0;
    }

    ret = avcodec_send_packet(video.codecContext, video.packet);
    if (ret < 0)
    {
        spdlog::critical("Error while sending video packet to decoder: {}", av_err2str(ret));
        av_packet_unref(video.packet);
        return -1;
    }

    ret = avcodec_receive_frame(video.codecContext, video.frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        av_packet_unref(video.packet);
        return 0;
    }

    if (ret < 0)
    {
        spdlog::critical("Error while receiving video frame from decoder: {}", av_err2str(ret));
        av_packet_unref(video.packet);
        return -1;
    }

    float textureWidth = 0.0f;
    float textureHeight = 0.0f;
    if (!SDL_GetTextureSize(texture, &textureWidth, &textureHeight))
    {
        spdlog::critical("Failed to query texture size: {}", SDL_GetError());
        av_packet_unref(video.packet);
        return -1;
    }

    int targetWidth = static_cast<int>(textureWidth);
    int targetHeight = static_cast<int>(textureHeight);

    AVFrame *display_frame = video.frame;
    if (display_frame->format != AV_PIX_FMT_YUV420P)
    {
        const AVPixelFormat inputPixelFormat = (const AVPixelFormat)display_frame->format;

        /* video.sws_ctx = sws_getCachedContext(video.sws_ctx,
                                             display_frame->width,
                                             display_frame->height,
                                             inputPixelFormat,
                                             video.codecContext->width,
                                             video.codecContext->height,
                                             AV_PIX_FMT_YUV420P,
                                             SWS_BILINEAR,
                                             nullptr,
                                             nullptr,
                                             nullptr); */

        video.sws_ctx = sws_getCachedContext(video.sws_ctx,
                                             display_frame->width,
                                             display_frame->height,
                                             inputPixelFormat,
                                             targetWidth,
                                             targetHeight,
                                             AV_PIX_FMT_YUV420P,
                                             SWS_BILINEAR,
                                             nullptr,
                                             nullptr,
                                             nullptr);

        if (!video.sws_ctx)
        {
            spdlog::critical("failed to create sws context for input frame format {}", display_frame->format);
            av_packet_unref(video.packet);
            return -1;
        }

        if (av_frame_make_writable(video.yuv_frame) < 0)
        {
            av_packet_unref(video.packet);
            return 0;
        }
        sws_scale(video.sws_ctx,
                  display_frame->data,
                  display_frame->linesize,
                  0,
                  display_frame->height,
                  video.yuv_frame->data,
                  video.yuv_frame->linesize);

        video.yuv_frame->color_range = targetColorRange;
        video.yuv_frame->colorspace = targetColorSpace;
        video.yuv_frame->sample_aspect_ratio = targetPixelAspect;
        display_frame = video.yuv_frame;
    }

    // Keep filter input metadata stable to avoid runtime graph property changes.
    display_frame->color_range = targetColorRange;
    display_frame->colorspace = targetColorSpace;
    display_frame->sample_aspect_ratio = targetPixelAspect;

    AVFrame *filtered_frame = video.filtered_frame;
    av_frame_unref(filtered_frame);
    ret = av_buffersrc_add_frame_flags(video.sourceFilter, display_frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0)
    {
        spdlog::critical("Error while feeding the video filter: {}", av_err2str(ret));
        av_packet_unref(video.packet);
        return -1;
    }

    ret = av_buffersink_get_frame(video.sinkFilter, filtered_frame);
    if (ret < 0)
    {
        spdlog::critical("Error while retrieving frame from the video filter: {}", av_err2str(ret));
        av_packet_unref(video.packet);
        return -1;
    }

    int64_t nowUs = av_gettime_relative();
    int64_t startUs = videoPtsStartUs.load();
    if (startUs == AV_NOPTS_VALUE)
    {
        if (videoPtsStartUs.compare_exchange_strong(startUs, nowUs))
        {
            startUs = nowUs;
        }
        else
        {
            startUs = videoPtsStartUs.load();
        }
    }
    filtered_frame->pts = nowUs - startUs;

    ret = muxor->write_video_frame(filtered_frame, AVRational{1, 1000000});
    if (ret < 0)
    {
        spdlog::critical("Error while muxing frame: {}", av_err2str(ret));
    }

    SDL_UpdateYUVTexture(texture, nullptr,
                         filtered_frame->data[0], filtered_frame->linesize[0],
                         filtered_frame->data[1], filtered_frame->linesize[1],
                         filtered_frame->data[2], filtered_frame->linesize[2]);
    av_packet_unref(video.packet);
    return 0;
}
int Webcam::muteAudioPlayback(bool mute)
{
    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to use it.");
        return -1;
    }
    this->mute.store(mute);
    return 0;
}

int Webcam::processAudioFrame(SDL_AudioStream *audioStream)
{

    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to use it.");
        return -1;
    }
    int ret = av_read_frame(audio.fmtContext, audio.packet);
    if (ret < 0)
    {
        spdlog::debug("No more audio frames");
        return 1;
    }

    if (audio.packet->stream_index != audio.stream_index)
    {
        av_packet_unref(audio.packet);
        return 0;
    }

    ret = avcodec_send_packet(audio.codecContext, audio.packet);
    if (ret < 0 && ret != AVERROR(EAGAIN))
    {
        spdlog::debug("Error while sending packet to decoder: {}", av_err2str(ret));
        av_packet_unref(audio.packet);
        return 0;
    }

    while (true)
    {
        ret = avcodec_receive_frame(audio.codecContext, audio.frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
        {
            break;
        }
        if (ret < 0)
        {
            spdlog::critical("Error while receiving frame from decoder: {}", av_err2str(ret));
            av_packet_unref(audio.packet);
            return -1;
        }

        if (videoPtsStartUs.load() != AV_NOPTS_VALUE)
        {
            muxor->write_audio_frame(audio.frame);
        }

        if (mute.load())
        {
            av_frame_unref(audio.frame);
            continue;
        }

        int outChannels = audio.out_ch_layout.nb_channels > 0 ? audio.out_ch_layout.nb_channels : 2;
        int dst_nb_samples = av_rescale_rnd(swr_get_delay(audio.swr_ctx, audio.codecContext->sample_rate) + audio.frame->nb_samples,
                                            audio.codecContext->sample_rate,
                                            audio.codecContext->sample_rate,
                                            AV_ROUND_UP);
        if (dst_nb_samples <= 0)
        {
            av_frame_unref(audio.frame);
            continue;
        }

        int requiredBufferSize = av_samples_get_buffer_size(nullptr, outChannels, dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
        if (requiredBufferSize < 0)
        {
            spdlog::critical("Failed to compute SDL output buffer size: {}", av_err2str(requiredBufferSize));
            av_packet_unref(audio.packet);
            return -1;
        }

        if (requiredBufferSize > audio.out_buf_size)
        {
            uint8_t *newBuffer = (uint8_t *)av_realloc(audio.out_buf, requiredBufferSize);
            if (!newBuffer)
            {
                spdlog::critical("Failed to allocate SDL output audio buffer");
                av_packet_unref(audio.packet);
                return -1;
            }
            audio.out_buf = newBuffer;
            audio.out_buf_size = requiredBufferSize;
        }

        uint8_t *outData[1] = {audio.out_buf};
        int convertedSamples = swr_convert(audio.swr_ctx,
                                           outData,
                                           dst_nb_samples,
                                           (const uint8_t **)audio.frame->extended_data,
                                           audio.frame->nb_samples);
        if (convertedSamples < 0)
        {
            spdlog::critical("Failed to resample audio for SDL playback: {}", av_err2str(convertedSamples));
            av_packet_unref(audio.packet);
            return -1;
        }

        int data_size = av_samples_get_buffer_size(nullptr, outChannels, convertedSamples, AV_SAMPLE_FMT_S16, 1);
        if (data_size < 0)
        {
            spdlog::critical("Failed to compute SDL queued audio size: {}", av_err2str(data_size));
            av_packet_unref(audio.packet);
            return -1;
        }

        if (audioStream != nullptr)
        {
            if (!SDL_PutAudioStreamData(audioStream, audio.out_buf, data_size))
            {
                spdlog::warn("Failed to queue audio to SDL stream: {}", SDL_GetError());
            }
        }

        av_frame_unref(audio.frame);
    }

    av_packet_unref(audio.packet);
    return 0;
}

void Webcam::audioCaptureLoop()
{
    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to start the audio loop.");
        return ;
    }
    audioThreadRunning.store(true);
    if(audioStreamTarget == nullptr)
    {
        spdlog::critical("Didn't initialize audio stream before trying to start the loop.");
        return ;
    }
    while (!audioThreadStopRequested.load())
    {
        int ret = processAudioFrame(audioStreamTarget);
        if (ret < 0)
        {
            spdlog::critical("Audio capture loop failed");
            break;
        }
        if (ret > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }

    audioThreadRunning.store(false);
}

int Webcam::startAudioCapture(SDL_AudioStream *audioStream)
{
    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to use it.");
        return -1;
    }

    if (audioThreadRunning.load())
    {
        return 0;
    }

    audioStreamTarget = audioStream;
    audioThreadStopRequested.store(false);
    audioThread = std::thread(&Webcam::audioCaptureLoop, this);

    return 0;
}

int Webcam::stopAudioCapture()
{
    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to use it.");
        return -1;
    }

    audioThreadStopRequested.store(true);
    if (audioThread.joinable())
    {
        audioThread.join();
    }
    audioStreamTarget = nullptr;
    return 0;
}

int Webcam::close()
{
    if(!initialized)
    {
        spdlog::critical("Didn't initialize webcam before trying to use it.");
        return -1;
    }

    spdlog::debug("Closing Webcam: {}", device->videoPath);
    int ret = 0;
    stopAudioCapture();

    int videoRet = closeVideo();
    if (videoRet < 0)
    {
        spdlog::critical("Failed to close webcam video");
        ret = videoRet;
    }
    int audioRet = closeAudio();
    if (audioRet < 0)
    {
        spdlog::critical("Failed to close webcam audio");
        if (ret == 0)
        {
            ret = audioRet;
        }
    }
    if (muxor != nullptr)
    {
        int muxorRet = muxor->close();
        if (muxorRet < 0)
        {
            spdlog::critical("Failed to close muxor");
            if (ret == 0)
            {
                ret = muxorRet;
            }
        }

        delete muxor;
        muxor = nullptr;
    }

    return ret;
}

int Webcam::closeVideo()
{
    if (video.fmtContext != nullptr)
    {
        avformat_close_input(&video.fmtContext);
        video.fmtContext = nullptr;
    }
    if(video.codecParams != nullptr)
    {
        avcodec_parameters_free(&audio.codecParams);
        video.codecParams = nullptr;
    }
    if (video.codecContext != nullptr)
    {
        avcodec_free_context(&video.codecContext);
        video.codecContext = nullptr;
    }
    if (video.packet != nullptr)
    {
        av_packet_free(&video.packet);
        video.packet= nullptr;
    }
    if (video.frame != nullptr)
    {
        av_frame_free(&video.frame);
        video.frame= nullptr;
    }
    if (video.sws_ctx != nullptr)
    {
        sws_free_context(&video.sws_ctx);
        video.sws_ctx = nullptr;
    }
    if (video.yuv_frame != nullptr)
    {
        av_frame_free(&video.yuv_frame);
        video.yuv_frame = nullptr;
    }
    if (video.filtered_frame != nullptr)
    {
        av_frame_free(&video.filtered_frame);
        video.filtered_frame = nullptr;
    }
    if (video.filterGraph != nullptr)
    {
        avfilter_graph_free(&video.filterGraph);
        video.sourceFilter = nullptr;
        video.sinkFilter = nullptr;
        video.flipFilter = nullptr;
        video.filterGraph = nullptr;
    }
    return 0;
}

int Webcam::closeAudio()
{
    if (audio.fmtContext != nullptr)
    {
        avformat_close_input(&audio.fmtContext);
        audio.fmtContext = nullptr;
    }
    if(audio.codecParams != nullptr)
    {
        avcodec_parameters_free(&audio.codecParams);
        audio.codecParams = nullptr;
    }
    if (audio.codecContext != nullptr)
    {
        avcodec_free_context(&audio.codecContext);
        audio.codecContext = nullptr;
    }
    if (audio.packet != nullptr)
    {
        av_packet_free(&audio.packet);
        audio.packet = nullptr;
    }
    if (audio.frame != nullptr)
    {
        av_frame_free(&audio.frame);
        audio.frame = nullptr;
    }
    if (audio.swr_ctx != nullptr)
    {
        swr_free(&audio.swr_ctx);
        audio.swr_ctx = nullptr;
    }
    av_channel_layout_uninit(&audio.out_ch_layout);
    if (audio.out_buf != nullptr)
    {
        av_free(audio.out_buf);
        free((void *)audio.out_buf);
        audio.out_buf_size = 0;
        audio.out_buf = nullptr;
    }

    return 0;
}
