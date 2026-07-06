#include "WebcamUtils.h"
#include <sound/asound.h>

int query_all_capture_modes(capture_mode **cap_modes, int *count, cam_device usb_camera)
{

    int fd = open(usb_camera.videoPath, O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        return -1;
    }
    struct v4l2_capability *camcap = (v4l2_capability *)malloc(sizeof(v4l2_capability));
    int ret = xioctl(fd, VIDIOC_QUERYCAP, camcap);
    if (ret < 0)
    {
        spdlog::critical("Xioctl Failed: {}", strerror(errno));
        free(camcap);
        close(fd);
        return -1;
    }

    *count = 0;
    if (fd < 0 || cap_modes == nullptr || count == nullptr)
    {
        spdlog::critical("Invalid device handle provided");
        free(camcap);
        close(fd);
        return -1;
    }

    *cap_modes = (capture_mode *)malloc(sizeof(capture_mode) * MAX_CAP_MODES);
    if (*cap_modes == nullptr)
    {
        spdlog::critical("Failed to allocate capture mode buffer");
        free(camcap);
        close(fd);
        return -1;
    }

    v4l2_fmtdesc fmtDesc = {};
    fmtDesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    spdlog::debug("Finding all capture modes");
    while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtDesc) == 0)
    {
        //spdlog::debug("Description: {}", (char *)fmtDesc.description);
        const char *input_fmt = fourcc_to_str(fmtDesc.pixelformat);
        if (input_fmt == NULL)
        {
            spdlog::critical("Couldnt parse the v4l2 pixel format.");
            fmtDesc.index++;
            continue;
        }
        //spdlog::debug("Index: {}, Type: {:x}, Pixel Format: {}", fmtDesc.index, fmtDesc.type, input_fmt);

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
                if (*count >= MAX_CAP_MODES)
                {
                    spdlog::warn("Capture mode buffer is full; stopping enumeration");
                    return 0;
                }

                (*cap_modes)[*count].fps = get_highest_fps(fd, fmtDesc.pixelformat, width, height);
                (*cap_modes)[*count].width = width;
                (*cap_modes)[*count].height = height;
                (*cap_modes)[*count].pixelFormat = fmtDesc.pixelformat;
                //spdlog::debug("PixFmt: {}, Width: {}, Height: {}, FPS: {}", (*cap_modes)[*count].pixelFormat, (*cap_modes)[*count].width, (*cap_modes)[*count].height, (*cap_modes)[*count].fps);

                (*count)++;
            }

            frameSize.index++;
        }

        fmtDesc.index++;
    }

    free(camcap);
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

capture_mode probeBestCaptureMode(const char *devicePath)
{

    capture_mode bestMode;
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
                    // TODO Remove Score
                    int score = 1000;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestMode.width = width;
                        bestMode.height = height;
                        bestMode.pixelFormat = fmtDesc.pixelformat;
                        // bestMode.input_fmt = ffmpegInputFormat;
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

cam_config *merge_cam_structs(int nb_of_cameras, cam_device *usb_cameras)
{
    cam_config *return_cam_configs = (cam_config *)malloc(sizeof(cam_config) * nb_of_cameras);

    for (int i = 0; i < nb_of_cameras; i++)
    {
        return_cam_configs[i].index = i;
        return_cam_configs[i].set_fps = 30;
        return_cam_configs[i].set_width = 1920;
        return_cam_configs[i].set_height = 1080;
        return_cam_configs[i].pix_format = new char[6]{"mjpeg"};
        return_cam_configs[i].manufacturer = usb_cameras[i].manufacturer;
        return_cam_configs[i].serialNumber = usb_cameras[i].serialNumber;
        return_cam_configs[i].product = usb_cameras[i].product;
        return_cam_configs[i].productID = usb_cameras[i].productID;
        return_cam_configs[i].vendorID = usb_cameras[i].vendorID;
        return_cam_configs[i].cap_modes = usb_cameras[i].cap_modes;
        return_cam_configs[i].nb_cap_modes = usb_cameras[i].nb_cap_modes;
    }
    return return_cam_configs;
}

int query_all_webcams(cam_device **cameras, int *nb_of_cameras)
{

    int ret = 0;
    cam_device *usb_cameras = (cam_device *)malloc(sizeof(cam_device) * MAX_CAMS);
    *cameras = usb_cameras;
    int nb_usb_cameras = 0;

    udev *udev;
    udev_enumerate *enum_video, *enum_audio;
    udev_list_entry *video_devices, *audio_devices, *dev_list_entry, *dev_list_entry2;
    udev_device *dev, *dev2;

    udev = udev_new();
    if (!udev)
    {
        spdlog::critical("Failied to start udev");
    }
    else
    {
        spdlog::info("Udev Started");
    }
    enum_video = udev_enumerate_new(udev);
    enum_audio = udev_enumerate_new(udev);
    udev_enumerate_add_match_subsystem(enum_video, "video4linux");
    udev_enumerate_scan_devices(enum_video);
    video_devices = udev_enumerate_get_list_entry(enum_video);

    const char *path;
    const char *usbPath;
    const char *videoPath;
    const char *audioPath;
    const char *manufacturer;
    const char *product;
    const char *vendorID;
    const char *productID;
    const char *serialNumber;
    const char *test;
    const int *card_num;
    const int *device_num;

    /*
       Every webcam has 2 dev/video0 inputs, because the second one is used for camera meta-data. We simply skip every other option.
                          dev/video1
    */
    bool metadata = false;

    spdlog::info("Querying Video Devices");

    udev_list_entry_foreach(dev_list_entry, video_devices)
    {
        path = udev_list_entry_get_name(dev_list_entry);
        dev = udev_device_new_from_syspath(udev, path);

        videoPath = udev_device_get_devnode(dev);
        dev = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");

        usbPath = udev_device_get_syspath(dev);
        manufacturer = udev_device_get_sysattr_value(dev, "manufacturer");
        product = udev_device_get_sysattr_value(dev, "product");

        vendorID = udev_device_get_sysattr_value(dev, "idVendor");
        productID = udev_device_get_sysattr_value(dev, "idProduct");
        serialNumber = udev_device_get_sysattr_value(dev, "serial");

        // Identify the sound subsystem
        dev = udev_device_new_from_syspath(udev, usbPath);
        udev_enumerate_add_match_parent(enum_audio, dev);
        udev_enumerate_add_match_subsystem(enum_audio, "sound");
        udev_enumerate_scan_devices(enum_audio);
        audio_devices = udev_enumerate_get_list_entry(enum_audio);

        udev_list_entry_foreach(dev_list_entry2, audio_devices)
        {

            path = udev_list_entry_get_name(dev_list_entry2);
            dev = udev_device_new_from_syspath(udev, path);
            audioPath = udev_device_get_devnode(dev);
            test = udev_device_get_sysnum(dev);
            if (audioPath)
            {
                spdlog::debug("   Sound:");
                spdlog::debug("      Path: {}", path);
                spdlog::debug("      Audio Path: {}", audioPath);

                if (Utils::str_starts_with(audioPath, soundValidation))
                {
                    // apath should look something like this C0D1c
                    // C0 = card 0
                    // D1 = device 1
                    //  c = capture device(p would be playback device)
                    std::string apath(audioPath);

                    std::string tmp = apath.substr(std::strlen(soundValidation));

                    std::string::size_type size;

                    // We have to skip one character the C
                    card_num = new int(std::stoi(tmp.substr(1), &size));
                    // We have to skip two characters the C, D, and whatever the size of the int was.
                    device_num = new int(std::stoi(tmp.substr(2 + size)));

                    break;
                }
            }
        }

        if (!metadata)
        {
            if (!usbPath)
            {
                spdlog::critical("No no pci/usb coordinates, how did we even find this???");
            }
            if (!videoPath)
            {
                spdlog::critical("No dev/videoX location found, must not be valid???");
            }

            cam_device *device = (usb_cameras + nb_usb_cameras);

            device->usbPath = usbPath;
            device->videoPath = videoPath;
            device->audioPath = audioPath;

            device->audioCard = card_num;
            device->audioDevice = device_num;

            device->manufacturer = manufacturer;
            device->product = product;

            device->vendorID = vendorID;
            device->productID = productID;
            device->serialNumber = serialNumber;
            nb_usb_cameras++;
        }

        metadata = !metadata;
    }
    spdlog::debug("Number of Cameras: {}", nb_usb_cameras);
    for (int i = 0; i < nb_usb_cameras; i++)
    {
        spdlog::debug("Camera - {}", i);

        cam_device cam = usb_cameras[i];
        spdlog::debug("   USB Path:      {}", cam.usbPath);
        spdlog::debug("   Video Path:      {}", cam.videoPath);
        spdlog::debug("   Audio Path:      {}", cam.audioPath);
        spdlog::debug("   Card Number:      {}", *cam.audioCard);
        spdlog::debug("   Device Number:      {}", *cam.audioDevice);
        spdlog::debug("   Manufacturer:  {}", Utils::str_or_default(cam.manufacturer, "[none]"));
        spdlog::debug("   Product        {}", Utils::str_or_default(cam.product, "[none]"));
        spdlog::debug("   Vendor ID:     {}", Utils::str_or_default(cam.vendorID, "[none]"));
        spdlog::debug("   Product ID:    {}", Utils::str_or_default(cam.productID, "[none]"));
        spdlog::debug("   Serial Number: {}", Utils::str_or_default(cam.serialNumber, "[none]"));
    }

    for (int i = 0; i < nb_usb_cameras; i++)
    {
        capture_mode *cap_modes = nullptr;
        int *nb_of_cap_modes = new int(0);

        ret = query_all_capture_modes(&cap_modes, nb_of_cap_modes, usb_cameras[i]);
        if (ret < 0)
        {
            spdlog::critical("Failed to query the capture modes");
        }
        usb_cameras[i].cap_modes = cap_modes;
        usb_cameras[i].nb_cap_modes = nb_of_cap_modes;
        spdlog::critical("Cap Modes: {}", *nb_of_cap_modes);
    }

    cam_config *configs = merge_cam_structs(nb_usb_cameras, usb_cameras);

    Config::save_cam_config(nb_usb_cameras, configs);

    *nb_of_cameras = nb_usb_cameras;
    return 0;
}

// PlugHW is the alternate mode but I dont think I care about that.
char *getAudioHW(int card, int device)
{
    char *hw = (char*)malloc(sizeof(char) * 12);

    snprintf(hw, sizeof(hw), "hw:%d,%d", card, device);
    spdlog::debug("AudioHW: {}", hw);

    return std::move(hw);
}
