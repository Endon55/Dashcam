#include "WebcamUtils.h"
#include <sound/asound.h>

int findBestCaptureMode(Capture_Priority priority, Camera *camera)
{
    if (camera->capture_mode != NULL)
    {
        free(camera->capture_mode);
        camera->capture_mode = NULL;
    }
    camera->capture_mode = (CaptureMode *)malloc(sizeof(CaptureMode));
    CaptureMode *bestMode = camera->capture_mode;
    int64_t bestScore = -1;

    int fd = open(camera->device_name, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        spdlog::warn("Unable to probe V4L2 formats for {}", camera->device_name);
        return -1;
    }

    v4l2_fmtdesc fmtDesc = {};
    fmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    spdlog::critical("Finding best mode");
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtDesc) == 0)
    {
        const char *input_fmt = fourcc_to_str(fmtDesc.pixelformat);
        if (input_fmt == NULL)
        {
            spdlog::critical("Couldnt parse the v4l2 pixel format.");
        }
        spdlog::debug("Index: {}, Type: {:x}, Pixel Format: {}", fmtDesc.index, fmtDesc.type, input_fmt);

        if (input_fmt != nullptr)
        {
            v4l2_frmsizeenum frameSize = {};
            frameSize.pixel_format = fmtDesc.pixelformat;
            // Each loop enumerates another frame option.
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

                    double fps = get_highest_fps(fd, fmtDesc.pixelformat, width, height);
                    int score = capture_mode_score(width, height, fmtDesc.pixelformat, fps, priority);
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestMode->width = width;
                        bestMode->height = height;
                        bestMode->pixelFormat = fmtDesc.pixelformat;
                        bestMode->input_fmt = input_fmt;
                        bestMode->fps = fps;
                    }
                    spdlog::debug("Type: {}, Width: {}, Height: {}, FPS: {}", frameSize.type, width, height, fps);
                }

                frameSize.index++;
            }
        }

        fmtDesc.index++;
    }

    close(fd);
    return 0;
}

AVPixelFormat canonicalizePixelFormat(AVPixelFormat pixelFormat)
{
    switch (pixelFormat)
    {
    case AV_PIX_FMT_YUVJ420P:
        return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P:
        return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
        return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
        return AV_PIX_FMT_YUV440P;
    default:
        return pixelFormat;
    }
}

int sourceColorRangeForFrame(const AVFrame *frame)
{
    if (frame->color_range == AVCOL_RANGE_JPEG)
    {
        return 1;
    }

    AVPixelFormat srcFormat = static_cast<AVPixelFormat>(frame->format);
    if (srcFormat == AV_PIX_FMT_YUVJ420P || srcFormat == AV_PIX_FMT_YUVJ422P ||
        srcFormat == AV_PIX_FMT_YUVJ444P || srcFormat == AV_PIX_FMT_YUVJ440P)
    {
        return 1;
    }

    return 0;
}

const char *fourcc_to_str(uint32_t pixelFormat)
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

double get_highest_fps(int fd, uint32_t pixelFormat, int width, int height)
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

int capture_mode_score(int width, int height, uint32_t pixelFormat, double fps, Capture_Priority priority)
{
    int res_weight = 1;
    int fps_weight = 1;
    switch (priority)
    {
    case RESOLUTION:
        res_weight = 10000;
        break;
    case FPS:
        fps_weight = 10000;
        break;
    case BALANCED:
    default:
        break;
    }
    int score = width * height * res_weight;
    score += fps * fps_weight;

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
        const char *ffmpegInputFormat = fourcc_to_str(fmtDesc.pixelformat);
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
                    double fps = get_highest_fps(fd, fmtDesc.pixelformat, width, height);
                    int score = capture_mode_score(width, height, fmtDesc.pixelformat, fps, {});
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestMode.width = width;
                        bestMode.height = height;
                        bestMode.pixelFormat = fmtDesc.pixelformat;
                        bestMode.input_fmt = ffmpegInputFormat;
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

int xioctl(int fd, int request, void *arg)
{
    int r;

    do
    {
        r = ioctl(fd, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

const char *getDeviceName(int index)
{
    char *device = new char[dev_len];
    strcpy(device, dev);
    device[dev_len - 1] = '0' + index;

    return device;
}

int query_all_webcams(Camera **cameras, int *nb_of_cameras)
{
    int ret = 0;
    spdlog::debug("Querying webcams connected to system");
    int unique_webcams = 0;
    *cameras = new Camera[max_cams]();
    Camera *cams = *cameras;

    int index = 0;

    while (true)
    {
        const char *devname = getDeviceName(index);
        int fd = open(devname, O_RDWR | O_NONBLOCK);
        if (fd < 0)
        {
            delete[] devname;
            break;
        }
        struct v4l2_capability *camcap = (v4l2_capability *)malloc(sizeof(v4l2_capability));

        int ret = xioctl(fd, VIDIOC_QUERYCAP, camcap);
        if (ret < 0)
        {
            spdlog::critical("Xioctl Failed: {}", strerror(errno));
            free(camcap);
            delete[] devname;
            close(fd);
            return -1;
        }
        const char *bus_info = (const char *)camcap->bus_info;
        if (unique_webcams == 0 || strcmp(cams[unique_webcams - 1].bus_info, bus_info) != 0)
        {
            cams[unique_webcams] = {
                .bus_info = strdup((char *)camcap->bus_info),
                .device_name = strdup(devname),
                .audio_hw = NULL,
                .video_index = index,
                .nb_of_video_entries = 1};
            unique_webcams++;
        }
        else
        {
            cams[unique_webcams - 1].nb_of_video_entries++;
        }

        free(camcap);
        delete[] devname;

        // check previous index to see if the bus info matches
        close(fd);
        index++;
    }
    ret = pair_audio_index_with_camera(cams, unique_webcams);
    if (ret < 0)
    {
        spdlog::critical("Failed to get the audio device associated with webcam");
        return -1;
    }

    spdlog::debug("Webcam Info Dump");
    for (int i = 0; i < unique_webcams; i++)
    {
        spdlog::debug("Device Name: {}, Device Index: {}, Audio Index: {}, Device Entries:{}, USB-ID: {}", cams[i].device_name, cams[i].video_index, cams[i].audio_hw, cams[i].nb_of_video_entries, cams[i].bus_info);
    }

    *nb_of_cameras = unique_webcams;
    return 0;
}

int check_for_usb_address_match(Camera *camera, int nb_of_cameras, unsigned char *device_description)
{
    for (int i = 0; i < nb_of_cameras; i++)
    {
        char *ch = strstr((char *)device_description, camera[i].bus_info);
        if (ch != nullptr)
        {
            return i;
        }
    }
    return -1;
}

int pair_audio_index_with_camera(Camera *camera, int nb_of_cameras)
{
    int ret;
    char control_path[32];

    // Iterate through potential sound cards (0 to 31 is the standard ALSA limit)
    for (int card = 0; card < 32; card++)
    {

        snprintf(control_path, sizeof(control_path), "/dev/snd/controlC%d", card);
        int fd = open(control_path, O_RDONLY);
        if (fd < 0)
            continue;

        struct snd_ctl_card_info card_info;
        ret = xioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, &card_info);
        if (ret == 0)
        {
            printf("Card %d: %s [%s]\n", card, card_info.name, card_info.longname);

            int cam_index = check_for_usb_address_match(camera, nb_of_cameras, card_info.longname);
            if (cam_index < 0)
            {
                continue;
            }

            // Now we query the device to find out what it's "hw:x,x handle is"
            int device = 0;
            struct snd_pcm_info pcm_info = {0};
            pcm_info.device = device;
            pcm_info.stream = SNDRV_PCM_STREAM_CAPTURE;

            ret = xioctl(fd, SNDRV_CTL_IOCTL_PCM_INFO, &pcm_info);
            if (ret < 0)
            {
                spdlog::critical("Failed to read audio device info: {}", strerror(errno));
                return -1;
            }
            if (camera[cam_index].audio_hw != NULL)
            {
                free((void *)camera[cam_index].audio_hw);
                camera[cam_index].audio_hw = NULL;
            }

            char audio_hw[12];
            snprintf(audio_hw, sizeof(audio_hw), "hw:%d,%d", card, device);
            camera[cam_index].audio_hw = strdup(audio_hw);
        }
        close(fd);
    }
    return 0;
}