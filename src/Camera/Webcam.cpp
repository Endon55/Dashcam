#include "Webcam.h"
#include "../Settings.h"
#include "Macros.h"
#include "../memory.h"
#include "WebcamUtils.h"

#include <cstdlib>
#include <libavutil/pixfmt.h>
#include <string>
#include <stdio.h>
#include <chrono>
#include <thread>
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
    if(!device|| !device->default_mode || device->default_mode->width == 0 || device->default_mode->height == 0 || device->default_mode->pixelFormat == 0 || device->default_mode->fps == 0.0)
    {
        spdlog::critical("Tried to init webcam with invalid camera options");
        return -1;
    }

    avdevice_register_all();
    int ret;
    spdlog::debug("Initializing Webcam: {}", device->videoPath);
    spdlog::debug("Initializing Video");
    ret = initVideo();
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize video");
        return ret;
    }
    spdlog::debug("Initializing Audio");
    if (has_audio)
    {
        ret = initAudio();
        if (ret < 0)
        {
            spdlog::critical("Failed to initialize audio");
            return ret;
        }
    }
    spdlog::debug("Initializing Muxor");
    AVRational captureFrameRate = video.fmtContext->streams[video.stream_index]->avg_frame_rate;
    if (captureFrameRate.num <= 0 || captureFrameRate.den <= 0)
    {
        spdlog::debug("Video stream doesn't have an avg_frame_rate, checking r_frame_rate");
        captureFrameRate = video.fmtContext->streams[video.stream_index]->r_frame_rate;
    }
    if (captureFrameRate.num <= 0 || captureFrameRate.den <= 0)
    {
        spdlog::debug("Video stream doesn't have an r_frame_rate either, defaulting to 1/30");
        captureFrameRate = AVRational{30, 1};
    }

    time_t timestamp = time({});
    char timeString[std::size("yyyy-mm-ddThh:mm:ssZ") + 4];
    char *ptr = &timeString[0];

    snprintf(ptr, 10, "c%02d-", camIndex);
    strftime(ptr + 4, std::size(timeString) - 5, "%FT%TZ", localtime(&timestamp));

    muxor = new Muxor(Settings::getVideoSaveDir().string() + "/" + timeString + ".mp4", has_audio);
    ret = muxor->init(video.codecParams->width, video.codecParams->height, captureFrameRate);
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize muxor");
        return ret;
    }
    return ret;
}


int Webcam::initFilterGraph(enum AVPixelFormat fmt)
{
    //Initializing the converter that converts frame data to a format SDL3 can display.
    video.sourceFilter = avfilter_graph_alloc_filter(video.filterGraph, avfilter_get_by_name("buffer"), "in");

    int ret = 0;
    if (!video.sourceFilter)
    {

        spdlog::critical("Failed to create buffer src filter");
        return -1;
    }

    AVBufferSrcParameters *srcParams = av_buffersrc_parameters_alloc();
    if (!srcParams)
    {
        spdlog::critical("Failed to allocate buffer source parameters");
        return -1;
    }
    const AVColorRange targetColorRange =
        video.codecContext->color_range != AVCOL_RANGE_UNSPECIFIED ? video.codecContext->color_range : COLOR_RANGE_DEFAULT;
    const AVColorSpace targetColorSpace =
        video.codecContext->colorspace != AVCOL_SPC_UNSPECIFIED ? video.codecContext->colorspace : COLOR_SPACE_DEFAULT;
    AVRational targetPixelAspect = video.codecContext->sample_aspect_ratio;
    if (targetPixelAspect.num <= 0 || targetPixelAspect.den <= 0)
    {
        targetPixelAspect = AVRational{1, 1};
    }

    srcParams->format = fmt;
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
        spdlog::critical("Failed to set buffer source parameters: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_init_str(video.sourceFilter, nullptr);
    if(ret < 0)
    {
        spdlog::critical("Failed to initalize buffersrc filter: {}", av_err2str(ret));
        return -1;
    }

    video.sinkFilter = avfilter_graph_alloc_filter(video.filterGraph, avfilter_get_by_name("buffersink"), "out");
    if (!video.sinkFilter)
    {
        spdlog::critical("Failed to allocate buffer sink filter");
        return -1;
    }

    ret = avfilter_init_str(video.sinkFilter, nullptr);
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize buffer sink filter: {}", av_err2str(ret));
        return -1;
    }

    video.flipFilter = avfilter_graph_alloc_filter(video.filterGraph, avfilter_get_by_name("hflip"), "hflip");
    if (!video.flipFilter)
    {
        spdlog::critical("Failed to allocate hflip filter");
        return -1;
    }

    ret = avfilter_init_str(video.flipFilter, nullptr);
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize hflip filter: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_link(video.sourceFilter, 0, video.flipFilter, 0);
    if (ret < 0)
    {
        spdlog::critical("Failed to link source to hflip: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_link(video.flipFilter, 0, video.sinkFilter, 0);
    if (ret < 0)
    {
        spdlog::critical("Failed to link hflip to sink: {}", av_err2str(ret));
        return -1;
    }

    ret = avfilter_graph_config(video.filterGraph, nullptr);
    if (ret < 0)
    {
        spdlog::critical("Failed to configure filter graph: {}", av_err2str(ret));
        return -1;
    }


    video.yuv_frame->format = fmt;
    video.yuv_frame->width = video.codecContext->width;
    video.yuv_frame->height = video.codecContext->height;
    if (av_frame_get_buffer(video.yuv_frame, 0) < 0)
    {
        spdlog::critical("Failed to allocate YUV frame buffer");
        return -1;
    }
    return 0;
}

int Webcam::initVideo()
{

    video = {};
    int ret = 0;
    video.stream_index = -1;
    video.fmtContext = avformat_alloc_context();
    if (!video.fmtContext)
    {
        spdlog::critical("Failed to allocate memory for format context");
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
    
    char frameRate[16];
    int stableFps = static_cast<int>(std::round(device->default_mode->fps));
    snprintf(frameRate, sizeof(frameRate), "%d", stableFps);
    av_dict_set(&videoInputOptions, "framerate", frameRate, 0);

    av_dict_set(&videoInputOptions, "input_format", (device->default_mode->pixelFmtStr != nullptr ? device->default_mode->pixelFmtStr : fourcc_to_str(device->default_mode->pixelFormat)), 0);

    spdlog::info("Opening webcam with default capture mode {} @ {}fps ({})",
                 videoSize,
                 frameRate,
                 device->default_mode->pixelFmtStr);

    ret = avformat_open_input(&video.fmtContext, device->videoPath, inputFormat, &videoInputOptions);
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
        spdlog::critical("Webcam initVideo: No streams were reported by the video device");
        return -1;
    }
    spdlog::debug("Streams: {}", video.fmtContext->nb_streams);
    for (int i = 0; i < video.fmtContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters = video.fmtContext->streams[i]->codecpar;
        if(!pLocalCodecParameters)
        {
            spdlog::warn("Failed to find local codec parameters: stream-{}", i);
            continue;
        }
        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (!pLocalCodec)
        {
            spdlog::warn("Failed to find local codec: stream-{}", i);
            continue;
        }

        //We only handle single video streams right now, no idea if more is possible.
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            video.stream_index = i;
            video.codecParams = pLocalCodecParameters;
            video.codec = pLocalCodec;
            AVDictionary *dict = video.fmtContext->streams[i]->metadata;
            const AVDictionaryEntry *entry = nullptr;
            int dict_size = av_dict_count(dict);
            if(dict_size > 0)
            {
                spdlog::debug("Video Stream Metadata");
                for (int j = 0; j < av_dict_count(dict); j++)
                {
                    entry = av_dict_iterate(dict, entry);

                    spdlog::debug(" Key: {}, Value: {}", entry->key, entry->value);
                }
            }

            spdlog::debug("Stream Resolution: {}x{}", pLocalCodecParameters->width, pLocalCodecParameters->height);
            break;
        }
        else if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            spdlog::critical("Somehow an audio stream got mixed with the video streams");
            return -1;
        }

    }

    if (video.stream_index == -1)
    {
        spdlog::critical("Failed to find a video stream");
        return -1;
    }

    video.codecContext = avcodec_alloc_context3(video.codec);
    if (!video.codecContext)
    {
        spdlog::critical("Failed to allocate memory for video AVCodecContext");
        return -1;
    }

    ret = avcodec_parameters_to_context(video.codecContext, video.codecParams); 
    if (ret < 0)
    {
        spdlog::critical("Filed to assign codec params to video codec context: {}", av_err2str(ret));
        return -1;
    }

    ret = avcodec_open2(video.codecContext, video.codec, nullptr);
    //This is the finalization call to the codec, so we can send/receive data from it.
    if (ret < 0)
    {
        spdlog::critical("Failed to open the video codec: {}", av_err2str(ret));
        return -1;
    }

    //Opaque is just a user defined variable, in this case a class object.
    video.codecContext->opaque = this;
    //FFMPEG doesn't have a concrete idea of what all the finalized formats are until it receives the first frame of camera data and has a built in callback so we can use the chosen format.
    video.codecContext->get_format = [](AVCodecContext* ctx, const enum AVPixelFormat* fmt)
    {
        Webcam* self = static_cast<Webcam*>(ctx->opaque);
        int index = 0;
        int ret = 0;
        const enum AVPixelFormat *p = fmt;
        while(*p != AV_PIX_FMT_NONE)
        {
            if(fmt[index] == FFMPEG_IMAGE_FORMAT)
            {
                spdlog::debug("Proper image format available");
                if(self->initFilterGraph(fmt[0]) < 0)
                {
                    spdlog::critical("FAILED TO INITIALIZE FILTER GRAPH BUT I CANT BREAK THE PROGRAM");
                }
                return fmt[index];
            }
            index++;
            p++;
        }

        spdlog::debug("Proper image format NOT available");
        if(self->initFilterGraph(fmt[0]) < 0)
        {
            spdlog::critical("FAILED TO INITIALIZE FILTER GRAPH BUT I CANT BREAK THE PROGRAM");
        }
        return fmt[0];
    };

    return 0;
}

int Webcam::initAudio()
{
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
    //Query the webcam for its audio parameters.
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
    
    AVDictionary *options = nullptr;//Doesn't need to be initialized prior to use.
    
    char *args= (char*) dc_malloc(sizeof(char) * 20);
    snprintf(args, 20, "%u", max_rate);
    av_dict_set(&options, "sample_rate", args, 0);
    snprintf(args, 20, "%u", max_channels);
    av_dict_set(&options, "channels", args, 0);

    dc_free(args);

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
    spdlog::debug("Streams: {}", audio.fmtContext->nb_streams);
    for (int i = 0; i < audio.fmtContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters = audio.fmtContext->streams[i]->codecpar;
        if(!pLocalCodecParameters)
        {
            spdlog::warn("Failed to find local codec parameters: stream-{}", i);
            continue;
        }
        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (!pLocalCodec)
        {
            spdlog::critical("Failed to find local codec");
            continue;
        }

        //We only handle single audio streams right now, no idea if more is possible.
        if (pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audio.stream_index = i;
            audio.codecParams = pLocalCodecParameters;
            audio.codec = pLocalCodec;

            break;
        }
    }

    if (audio.stream_index == -1)
    {
        spdlog::critical("Failed to find an audio stream");
        return -1;
    }

    audio.codecContext = avcodec_alloc_context3(audio.codec);
    if (!audio.codecContext)
    {
        spdlog::critical("Failed to allocate memory for audio AVCodecContext");
        return -1;
    }

    ret = avcodec_parameters_to_context(audio.codecContext, audio.codecParams);
    if (ret < 0)
    {
        spdlog::critical("Failed to assign codec params to audio codec context");
        return -1;
    }

    ret = avcodec_open2(audio.codecContext, audio.codec, nullptr);
    if (ret < 0)
    {
        spdlog::critical("Failed to open the audio codec: {}", av_err2str(ret));
        return -1;
    }
    //Make sure the audio is in a format that sdl3 can understand.
    if (audio.codecContext->sample_rate <= 0)
    {
        audio.codecContext->sample_rate = max_rate;
    }
    if (audio.codecContext->ch_layout.nb_channels <= 0)
    {
        av_channel_layout_default(&audio.codecContext->ch_layout, max_channels);
    }
    av_channel_layout_copy(&audio.out_ch_layout, &audio.codecContext->ch_layout);
    ret = swr_alloc_set_opts2(&audio.swr_ctx,
                              //output deets
                              &audio.out_ch_layout,
                              SDL_SAMPLE_FORMAT,
                              audio.codecContext->sample_rate,
                              //input deets
                              &audio.codecContext->ch_layout,
                              audio.codecContext->sample_fmt,
                              audio.codecContext->sample_rate,
                              0,
                              nullptr);
    if (ret < 0 || !audio.swr_ctx)
    {
        spdlog::critical("Failed to allocate swr context: {}", av_err2str(ret));
        return -1;
    }
    ret = swr_init(audio.swr_ctx);
    if (ret < 0)
    {
        spdlog::critical("Failed to init swr context");
        return -1;
    }

    audio.stream = audio.fmtContext->streams[audio.stream_index];

    return 0;
}

int Webcam::processVideoFrame(SDL_Texture *texture)
{
   // Keep each submitted frame aligned with the source filter's negotiated properties.
    const AVColorRange targetColorRange =
        video.codecContext->color_range != AVCOL_RANGE_UNSPECIFIED ? video.codecContext->color_range : COLOR_RANGE_DEFAULT;
    const AVColorSpace targetColorSpace =
        video.codecContext->colorspace != AVCOL_SPC_UNSPECIFIED ? video.codecContext->colorspace : COLOR_SPACE_DEFAULT;
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
    if (ret == AVERROR(EAGAIN) ||ret == AVERROR(EINVAL) || ret == AVERROR_EOF)
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

    //Convert the camera frames from its native raw format to the one SDL3 can display;
    if (display_frame->format != FFMPEG_IMAGE_FORMAT)
    {
        const AVPixelFormat inputPixelFormat = (const AVPixelFormat)display_frame->format;

        video.sws_ctx = sws_getCachedContext(video.sws_ctx,
                                             display_frame->width,
                                             display_frame->height,
                                             inputPixelFormat,
                                             targetWidth,
                                             targetHeight,
                                             FFMPEG_IMAGE_FORMAT,
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

    ret = av_buffersrc_add_frame_flags(video.sourceFilter, display_frame, AV_BUFFERSRC_FLAG_KEEP_REF); //flag stops ffmpeg from taking ownership of the frame
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
        //compare_exchange_strong is a function for updating atomic variables. In this instance I want to verify that my atomic variable hasnt been changed and is still AV_NOPTS_VALUE, and if it is, then store the current time, otherwise do nothing.
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
    this->mute.store(mute);
    return 0;
}

int Webcam::processAudioFrame(SDL_AudioStream *audioStream)
{
    int ret = av_read_frame(audio.fmtContext, audio.packet);
    if (ret < 0)
    {
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

    ret = avcodec_receive_frame(audio.codecContext, audio.frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        return -1;
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

    if (mute.load() || audioStream == nullptr)
    {
        av_frame_unref(audio.frame);
        av_frame_unref(audio.frame);
        return 0;
    }

    int outChannels = audio.out_ch_layout.nb_channels > 0 ? audio.out_ch_layout.nb_channels : 2;
    int dst_nb_samples = av_rescale_rnd(
            swr_get_delay(audio.swr_ctx, audio.codecContext->sample_rate) + audio.frame->nb_samples,
            audio.codecContext->sample_rate,
            audio.codecContext->sample_rate,
            AV_ROUND_UP);
    if (dst_nb_samples <= 0)
    {
        av_frame_unref(audio.frame);
        av_frame_unref(audio.frame);

        return 0;
    }

    int requiredBufferSize = av_samples_get_buffer_size(nullptr, outChannels, dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
    if (requiredBufferSize < 0)
    {
        spdlog::critical("Failed to compute SDL output buffer size: {}", av_err2str(requiredBufferSize));
        av_frame_unref(audio.frame);
        av_packet_unref(audio.packet);
        return -1;
    }

    if (requiredBufferSize > audio.out_buf_size)
    {
        uint8_t *newBuffer = (uint8_t *)av_realloc(audio.out_buf, requiredBufferSize);
        if (!newBuffer)
        {
            spdlog::critical("Failed to allocate SDL output audio buffer"); 
            av_frame_unref(audio.frame);
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
        av_frame_unref(audio.frame);
        av_packet_unref(audio.packet);
        return -1;
    }

    int data_size = av_samples_get_buffer_size(nullptr, outChannels, convertedSamples, AV_SAMPLE_FMT_S16, 1);
    if (data_size < 0)
    {
        spdlog::critical("Failed to compute SDL queued audio size: {}", av_err2str(data_size));
        av_frame_unref(audio.frame);
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
    av_packet_unref(audio.packet);

    return 0;
}

void Webcam::audioCaptureLoop()
{
    audioThreadRunning.store(true);
    spdlog::info("Thread Started");

    while (!audioThreadStopRequested.load())
    {
        int ret = processAudioFrame(sdlAudioStream);
        //-1s are something weird happened but its not a show stopper
        if (ret < -1)
        {
            spdlog::critical("Thread Terminated: Audio capture loop failed");
            break;
        }
        //no frames, sleep for a sec(micro)
        if (ret > 0)
        {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
    }
    spdlog::info("Audio Capture Loop Finished");
    audioThreadRunning.store(false);
}

int Webcam::startAudioCapture(SDL_AudioStream *audioStream)
{
    if (audioThreadRunning.load())
    {
        return 0;
    }

    sdlAudioStream = audioStream;
    audioThreadStopRequested.store(false);
    audioThread = std::thread(&Webcam::audioCaptureLoop, this);

    return 0;
}

auto max_wait_fallback = std::chrono::seconds(3);

int Webcam::stopAudioCapture()
{
    audioThreadStopRequested.store(true);
    auto start = std::chrono::steady_clock::now();

    //force the main thread to wait until the thread has finished executing.
    while(audioThreadRunning.load())
    {
        if(std::chrono::steady_clock::now() - start > max_wait_fallback)
        {
            spdlog::critical("Audio thread is on the lam, not waiting any longer");
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (audioThread.joinable())
    {
        audioThread.join();
    }
    sdlAudioStream = nullptr;
    return 0;
}

int Webcam::close()
{
    int ret = 0;
    stopAudioCapture();
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
        avcodec_parameters_free(&video.codecParams);
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
        dc_free((void *)audio.out_buf);
        audio.out_buf_size = 0;
        audio.out_buf = nullptr;
    }

    return 0;
}
