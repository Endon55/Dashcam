#include "Webcam.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace
{
    struct CaptureMode
    {
        int width = 0;
        int height = 0;
        uint32_t pixelFormat = 0;
        const char *ffmpegInputFormat = nullptr;
        double fps = 0.0;
    };

    const char *v4l2PixelFormatToFfmpeg(uint32_t pixelFormat)
    {
        switch (pixelFormat)
        {
        case V4L2_PIX_FMT_MJPEG:
            return "mjpeg";
        case V4L2_PIX_FMT_YUYV:
            return "yuyv422";
        case V4L2_PIX_FMT_NV12:
            return "nv12";
        case V4L2_PIX_FMT_RGB24:
            return "rgb24";
        default:
            return nullptr;
        }
    }

    double probeBestFpsForSize(int fd, uint32_t pixelFormat, int width, int height)
    {
        double bestFps = 0.0;
        v4l2_frmivalenum frameInterval = {};
        frameInterval.pixel_format = pixelFormat;
        frameInterval.width = static_cast<uint32_t>(width);
        frameInterval.height = static_cast<uint32_t>(height);

        while (ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &frameInterval) == 0)
        {
            if (frameInterval.type == V4L2_FRMIVAL_TYPE_DISCRETE)
            {
                if (frameInterval.discrete.numerator > 0)
                {
                    double fps = static_cast<double>(frameInterval.discrete.denominator) /
                                 static_cast<double>(frameInterval.discrete.numerator);
                    if (fps > bestFps)
                    {
                        bestFps = fps;
                    }
                }
            }
            else if (frameInterval.type == V4L2_FRMIVAL_TYPE_STEPWISE || frameInterval.type == V4L2_FRMIVAL_TYPE_CONTINUOUS)
            {
                if (frameInterval.stepwise.min.numerator > 0)
                {
                    double maxFps = static_cast<double>(frameInterval.stepwise.min.denominator) /
                                    static_cast<double>(frameInterval.stepwise.min.numerator);
                    if (maxFps > bestFps)
                    {
                        bestFps = maxFps;
                    }
                }
            }

            frameInterval.index++;
        }

        return bestFps;
    }

    int64_t captureModeScore(int width, int height, uint32_t pixelFormat, double fps)
    {
        int64_t score = static_cast<int64_t>(width) * static_cast<int64_t>(height);
        score += static_cast<int64_t>(fps * 10000000.0);

        if (pixelFormat == V4L2_PIX_FMT_MJPEG)
        {
            score += static_cast<int64_t>(1) << 28;
        }

        if (fps < 15.0)
        {
            score -= static_cast<int64_t>(1) << 30;
        }

        return score;
    }

    CaptureMode probeBestCaptureMode(const char *devicePath)
    {
        CaptureMode bestMode;
        int64_t bestScore = -1;

        int fd = open(devicePath, O_RDWR | O_NONBLOCK);
        if (fd < 0)
        {
            spdlog::warn("Unable to probe V4L2 formats for {}", devicePath);
            return bestMode;
        }

        v4l2_fmtdesc fmtDesc = {};
        fmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtDesc) == 0)
        {
            const char *ffmpegInputFormat = v4l2PixelFormatToFfmpeg(fmtDesc.pixelformat);
            if (ffmpegInputFormat != nullptr)
            {
                v4l2_frmsizeenum frameSize = {};
                frameSize.pixel_format = fmtDesc.pixelformat;

                while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frameSize) == 0)
                {
                    int width = 0;
                    int height = 0;

                    if (frameSize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
                    {
                        width = static_cast<int>(frameSize.discrete.width);
                        height = static_cast<int>(frameSize.discrete.height);
                    }
                    else if (frameSize.type == V4L2_FRMSIZE_TYPE_STEPWISE || frameSize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
                    {
                        width = static_cast<int>(frameSize.stepwise.max_width);
                        height = static_cast<int>(frameSize.stepwise.max_height);
                    }

                    if (width > 0 && height > 0)
                    {
                        double fps = probeBestFpsForSize(fd, fmtDesc.pixelformat, width, height);
                        int64_t score = captureModeScore(width, height, fmtDesc.pixelformat, fps);
                        if (score > bestScore)
                        {
                            bestScore = score;
                            bestMode.width = width;
                            bestMode.height = height;
                            bestMode.pixelFormat = fmtDesc.pixelformat;
                            bestMode.ffmpegInputFormat = ffmpegInputFormat;
                            bestMode.fps = fps;
                        }
                    }

                    frameSize.index++;
                }
            }

            fmtDesc.index++;
        }

        close(fd);
        return bestMode;
    }
}

Webcam::Webcam(Camera *camera) : has_audio(camera->audio_hw != NULL)
{
    this->camera = camera;
}

int Webcam::init()
{
    avdevice_register_all();
    int ret;
    spdlog::debug("Initializing Webcam: {}", camera->device_name);

    ret = initVideo();
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize video");
        return ret;
    }
    if(has_audio)
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

    muxor = new Muxor("/home/anthony/Desktop/videos/Test.mp4");
    ret = muxor->init(video.codecParams->width, video.codecParams->height, captureFrameRate);
    if (ret < 0)
    {
        spdlog::critical("Failed to initialize muxor");
        return ret;
    }
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

    AVDictionary *videoInputOptions = NULL;
    CaptureMode captureMode = probeBestCaptureMode(camera->device_name);
    if (captureMode.width > 0 && captureMode.height > 0)
    {
        char videoSize[32];
        snprintf(videoSize, sizeof(videoSize), "%dx%d", captureMode.width, captureMode.height);
        av_dict_set(&videoInputOptions, "video_size", videoSize, 0);
        if (captureMode.fps > 0.0)
        {
            char frameRate[16];
            int stableFps = static_cast<int>(std::round(captureMode.fps));
            snprintf(frameRate, sizeof(frameRate), "%d", stableFps);
            av_dict_set(&videoInputOptions, "framerate", frameRate, 0);
        }
        if (captureMode.ffmpegInputFormat != nullptr)
        {
            av_dict_set(&videoInputOptions, "input_format", captureMode.ffmpegInputFormat, 0);
        }
        spdlog::info("Requesting webcam mode {} @ {:.2f} fps ({})",
                     videoSize,
                     captureMode.fps,
                     captureMode.ffmpegInputFormat != nullptr ? captureMode.ffmpegInputFormat : "default");
    }

    ret = avformat_open_input(&video.fmtContext, camera->device_name, inputFormat, &videoInputOptions);
    av_dict_free(&videoInputOptions);
    if (ret < 0)
    {
        spdlog::critical("Error opening av format: {}", av_err2str(ret));
        return ret;
    }

    AVDeviceInfoList *devicesInfo = NULL;
    int deviceCount = avdevice_list_devices(video.fmtContext, &devicesInfo);
    spdlog::debug("Device count: {}", deviceCount);
    if (deviceCount > 0 && devicesInfo != NULL)
    {
        //Linux will use multiple /dev/videoX slots to convey all the relevant information about a stream
        //Both webcams I have for testing have the raw stream data in video0 but have metadata in video1
        for (int i = 0; i < devicesInfo->nb_devices; i++)
        {
            AVDeviceInfo *info = devicesInfo->devices[i];
            
            spdlog::debug("Device Name: {}, Device Description: {}, Media Type: {}", info->device_name, info->device_description, (long)info->media_types);
                }
    }
    else if (deviceCount < 0)
    {
        spdlog::warn("Unable to enumerate video devices: {}", av_err2str(deviceCount));
    }

    if (devicesInfo != NULL)
    {
        avdevice_free_list_devices(&devicesInfo);
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

    ret = avformat_find_stream_info(video.fmtContext, NULL);
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
        AVCodecParameters *pLocalCodecParameters = NULL;
        pLocalCodecParameters = video.fmtContext->streams[i]->codecpar;
        spdlog::debug("AVStream->time_base before open coded {}/{}", video.fmtContext->streams[i]->time_base.num, video.fmtContext->streams[i]->time_base.den);
        spdlog::debug("AVStream->r_frame_rate before open coded {}/{}", video.fmtContext->streams[i]->r_frame_rate.num, video.fmtContext->streams[i]->r_frame_rate.den);
        spdlog::debug("AVStream->start_time {}" PRId64, video.fmtContext->streams[i]->start_time);
        spdlog::debug("AVStream->duration {}" PRId64, video.fmtContext->streams[i]->duration);
        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL)
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
                const AVDictionaryEntry *entry = NULL;
                spdlog::debug("Video Stream Metadata");
                for(int j = 0; j < av_dict_count(dict); j++)
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

    if (avcodec_open2(video.codecContext, video.codec, NULL) < 0)
    {
        spdlog::critical("failed to open the video codec");
        return -1;
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

    ret = avfilter_graph_create_filter(&video.sourceFilter, avfilter_get_by_name("buffer"), "in", args, NULL, video.filterGraph);
    if (ret < 0)
    {
        spdlog::critical("failed to create buffer source: {}", av_err2str(ret));
        return -1;
    }

    video.sinkFilter = avfilter_graph_alloc_filter(video.filterGraph, avfilter_get_by_name("buffersink"), "out");
    if (!video.sinkFilter)
    {
        spdlog::critical("failed to allocate buffer sink");
        return -1;
    }

    ret = avfilter_init_str(video.sinkFilter, NULL);
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

    ret = avfilter_init_str(video.flipFilter, NULL);
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

    ret = avfilter_graph_config(video.filterGraph, NULL);
    if (ret < 0)
    {
        spdlog::critical("failed to configure filter graph: {}", av_err2str(ret));
        return -1;
    }

    video.yuv_frame->format = AV_PIX_FMT_YUV420P;
    video.yuv_frame->width = video.codecContext->width;
    video.yuv_frame->height = video.codecContext->height;
    if (av_frame_get_buffer(video.yuv_frame, 32) < 0)
    {
        spdlog::critical("failed to allocate YUV frame buffer");
        return -1;
    }

    return 0;
}

int Webcam::initAudio()
{
    spdlog::debug("Initializing Audio");

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


    AVDictionary *options = NULL;
    av_dict_set(&options, "buffer_size", "65536", 0);
    av_dict_set(&options, "period_size", "2048", 0);

    ret = avformat_open_input(&audio.fmtContext, camera->audio_hw, inputFormat, &options);
    av_dict_free(&options);
    if (ret < 0)
    {
        spdlog::critical("Failed to open hw:0,0: {}", av_err2str(ret));
        return -1;
    }

    AVDeviceInfoList *devicesInfo;
    spdlog::critical("audio device count: {}", avdevice_list_devices(audio.fmtContext, &devicesInfo));
    for (int i = 0; i < devicesInfo->nb_devices; i++)
    {
        AVDeviceInfo *info = devicesInfo->devices[i];
        if (strncmp("hw", info->device_name, 2) == 0)
        {
            spdlog::debug("Device Name: {}, Device Description: {}, Media Type: {}", info->device_name, info->device_description, (long)info->media_types);
            spdlog::debug("Device Name: {}, ", info->device_name);
        }
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

    for (int i = 0; i < audio.fmtContext->nb_streams; i++)
    {
        AVCodecParameters *pLocalCodecParameters = NULL;
        pLocalCodecParameters = audio.fmtContext->streams[i]->codecpar;
        spdlog::debug("AVStream->time_base before open coded {}/{}", audio.fmtContext->streams[i]->time_base.num, audio.fmtContext->streams[i]->time_base.den);
        spdlog::debug("AVStream->r_frame_rate before open coded {}/{}", audio.fmtContext->streams[i]->r_frame_rate.num, audio.fmtContext->streams[i]->r_frame_rate.den);
        spdlog::debug("AVStream->start_time {}" PRId64, audio.fmtContext->streams[i]->start_time);
        spdlog::debug("AVStream->duration {}" PRId64, audio.fmtContext->streams[i]->duration);

        const AVCodec *pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if (pLocalCodec == NULL)
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

            spdlog::debug("Audio Codec: {} channels, sample rate {}", pLocalCodecParameters->ch_layout.nb_channels, pLocalCodecParameters->sample_rate);
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

    if (avcodec_open2(audio.codecContext, audio.codec, NULL) < 0)
    {
        spdlog::critical("Failed to open the codec");
        return -1;
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
                              NULL);
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

int Webcam::processVideoFrame(SDL_Texture *texture, SDL_Rect *rect)
{
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

    AVFrame *display_frame = video.frame;
    if (display_frame->format != AV_PIX_FMT_YUV420P)
    {
        video.sws_ctx = sws_getCachedContext(video.sws_ctx,
                                             display_frame->width,
                                             display_frame->height,
                                             static_cast<AVPixelFormat>(display_frame->format),
                                             video.codecContext->width,
                                             video.codecContext->height,
                                             AV_PIX_FMT_YUV420P,
                                             SWS_BILINEAR,
                                             NULL,
                                             NULL,
                                             NULL);
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
        display_frame = video.yuv_frame;
    }

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

    muxor->write_video_frame(filtered_frame);

    SDL_UpdateYUVTexture(texture, rect,
                         filtered_frame->data[0], filtered_frame->linesize[0],
                         filtered_frame->data[1], filtered_frame->linesize[1],
                         filtered_frame->data[2], filtered_frame->linesize[2]);

    av_packet_unref(video.packet);
    return 0;
}

int Webcam::processAudioFrame(SDL_AudioStream *audioStream)
{
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
    if (ret < 0)
    {
        spdlog::debug("Error while sending packet to decoder: {}", av_err2str(ret));
        av_packet_unref(audio.packet);
        return 0;
    }

    ret = avcodec_receive_frame(audio.codecContext, audio.frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
    {
        av_packet_unref(audio.packet);
        return 0;
    }
    if (ret < 0)
    {
        spdlog::critical("Error while receiving frame from decoder: {}", av_err2str(ret));
        av_packet_unref(audio.packet);
        return -1;
    }

    int dst_nb_samples = (int)av_rescale_rnd(
        swr_get_delay(audio.swr_ctx, audio.codecContext->sample_rate) + audio.frame->nb_samples,
        audio.codecContext->sample_rate,
        audio.codecContext->sample_rate,
        AV_ROUND_UP);
    int dst_nb_channels = audio.codecContext->ch_layout.nb_channels;
    int dst_buf_size = av_samples_get_buffer_size(
        NULL, dst_nb_channels, dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
    if (dst_buf_size > audio.out_buf_size)
    {
        audio.out_buf = (uint8_t *)av_realloc(audio.out_buf, dst_buf_size);
        audio.out_buf_size = dst_buf_size;
    }

    uint8_t *out_planes[1] = {audio.out_buf};
    int out_samples = swr_convert(
        audio.swr_ctx,
        out_planes,
        dst_nb_samples,
        (const uint8_t **)audio.frame->data,
        audio.frame->nb_samples);
    if (out_samples > 0)
    {
        int out_size = out_samples * dst_nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
        SDL_PutAudioStreamData(audioStream, audio.out_buf, out_size);
    }

    av_packet_unref(audio.packet);
    return 0;
}

void Webcam::audioCaptureLoop()
{
    audioThreadRunning.store(true);

    while (!audioThreadStopRequested.load())
    {
        if (audioStreamTarget == nullptr)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

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
    spdlog::debug("Closing Webcam: {}", camera->device_name);
    int ret;
    stopAudioCapture();
    ret = closeVideo();
    if (ret < 0)
    {
        spdlog::critical("Failed to close webcam video");
        return ret;
    }
    ret = closeAudio();
    if (ret < 0)
    {
        spdlog::critical("Failed to close webcam audio");
        return ret;
    }
    ret = muxor->close();
    if (ret < 0)
    {
        spdlog::critical("Failed to close muxor");
        return ret;
    }
    return 0;
}

int Webcam::closeVideo()
{

    if (video.fmtContext != NULL)
    {
        avformat_close_input(&video.fmtContext);
        avformat_free_context(video.fmtContext);
    }
    if (video.packet != NULL)
    {
        av_packet_free(&video.packet);
    }
    if (video.frame != NULL)
    {
        av_frame_free(&video.frame);
    }
    if (video.yuv_frame != NULL)
    {
        av_frame_free(&video.yuv_frame);
    }
    if (video.filtered_frame != NULL)
    {
        av_frame_free(&video.filtered_frame);
    }
    if (video.sws_ctx != NULL)
    {
        sws_freeContext(video.sws_ctx);
    }
    if (video.codecContext != NULL)
    {
        avcodec_free_context(&video.codecContext);
    }
    if (video.filterGraph != NULL)
    {
        avfilter_graph_free(&video.filterGraph);
    }
    return 0;
}

int Webcam::closeAudio()
{
    /*    if (audioFormatContext != NULL)
       {
          avformat_close_input(&audioFormatContext);
          avformat_free_context(audioFormatContext);
       } */
    if (audio.packet != NULL)
    {
        av_packet_free(&audio.packet);
    }
    if (audio.frame != NULL)
    {
        av_frame_free(&audio.frame);
    }
    if (audio.codecContext != NULL)
    {
        avcodec_free_context(&audio.codecContext);
    }
    if (audio.swr_ctx != NULL)
    {
        swr_free(&audio.swr_ctx);
    }
    av_channel_layout_uninit(&audio.out_ch_layout);
    if (audio.out_buf != NULL)
    {
        av_free(audio.out_buf);
    }

    return 0;
}
