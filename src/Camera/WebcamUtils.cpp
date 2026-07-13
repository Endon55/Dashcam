#include "WebcamUtils.h"
#include "../memory.h"

#include <errno.h>
#include "../Utils.h"

#include <linux/videodev2.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <ctime>
#include <alsa/asoundlib.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <libudev.h>
#include <spdlog/spdlog.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
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

int query_all_capture_modes(AppState* state) {
    int ret = 0;

    for (int i = 0; i < state->nb_cams; i++)
    {
        cam_device* device = state->devices[i];
        if(device->cap_modes != nullptr)
        {
            continue;
        }
        spdlog::debug("No Cached config found, querying the device");
        ret = query_capture_modes(device);
        if (ret < 0)
        {
            spdlog::critical("Failed to query the capture modes");
            return ret;
        }
    }
    return 0;
}
int query_capture_modes(cam_device* device)
{
    int count = 0;
    int fd = open(device->videoPath, O_RDWR | O_NONBLOCK); if (fd < 0)
    {
        return -1;
    }
    struct v4l2_capability *camcap = (v4l2_capability *)dc_malloc(sizeof(v4l2_capability));
    int ret = xioctl(fd, VIDIOC_QUERYCAP, camcap);
    if (ret < 0)
    {
        spdlog::critical("Xioctl Failed: {}", strerror(errno));
        dc_free(camcap);
        close(fd);
        return -1;
    }



    device->cap_modes = (const capture_mode**)dc_malloc(sizeof(capture_mode*) * MAX_CAP_MODES);

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

            capture_mode* mode = (capture_mode*)dc_malloc(sizeof(capture_mode));

            if(fmtDesc.pixelformat == V4L2_PIX_FMT_MJPEG)
            {
                device->default_mode = mode;
            }


            if (frameSize.type == V4L2_FRMSIZE_TYPE_DISCRETE)
            {
                mode->width = static_cast<int>(frameSize.discrete.width);
                mode->height = static_cast<int>(frameSize.discrete.height);
            }
            else if (frameSize.type == V4L2_FRMSIZE_TYPE_STEPWISE || frameSize.type == V4L2_FRMSIZE_TYPE_CONTINUOUS)
            {
                mode->width = static_cast<int>(frameSize.stepwise.max_width);
                mode->height = static_cast<int>(frameSize.stepwise.max_height);
            }

            if (mode->width > 0 && mode->height > 0)
            {
                if (count >= MAX_CAP_MODES)
                {
                    spdlog::warn("Capture mode buffer is full; stopping enumeration");
                    return 0;
                }

                mode->fps = get_highest_fps(fd, fmtDesc.pixelformat, mode->width, mode->height);
                mode->pixelFormat = fmtDesc.pixelformat;
                mode->pixelFmtStr = fourcc_to_str(mode->pixelFormat);
                
                device->cap_modes[count] = mode;    
                count++;
            }

            frameSize.index++;
        }

        fmtDesc.index++;
    }
    device->nb_cap_modes = new_int(count);
    free(camcap);
    close(fd);
    return 0;

}

int query_all_webcams(AppState* state)
{

    int ret = 0;
    state->devices = (cam_device **)dc_malloc(sizeof(cam_device*) * MAX_CAMS);
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
        spdlog::debug("Udev Started");
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
    int  card_num;
    int  device_num;

    /*
       Every webcam has 2 dev/video0 inputs, because the second one is used for camera meta-data. We simply skip every other option.
                          dev/video1
    */
    bool metadata = false;

    spdlog::info("Querying Video Devices");

    udev_list_entry_foreach(dev_list_entry, video_devices)
    {
        path = nullptr;
        usbPath = nullptr;
        videoPath = nullptr;
        manufacturer = nullptr;
        product = nullptr;
        productID = nullptr;
        vendorID = nullptr;
        serialNumber = nullptr;
        audioPath = nullptr;
        card_num = -1;
        device_num = -1;

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

            audioPath = nullptr;
            card_num = -1;
            device_num = -1;
            path = udev_list_entry_get_name(dev_list_entry2);
            dev = udev_device_new_from_syspath(udev, path);
            audioPath = udev_device_get_devnode(dev);
            test = udev_device_get_sysnum(dev);
            if (audioPath)
            {

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
                    card_num = std::stoi(tmp.substr(1), &size);
                    // We have to skip two characters the C, D, and whatever the size of the int was.
                    device_num = std::stoi(tmp.substr(2 + size));

                    break;
                }
            }
        }

        if (!metadata)
        {
            if (!usbPath)
            {
                spdlog::debug("No no pci/usb coordinates, must be an internal device: {}", videoPath);
                continue;
            }
            if (!videoPath)
            {
                spdlog::debug("No dev/videoX location found, must not be valid???");
                continue;
            }

            state->devices[nb_usb_cameras] = (cam_device*)dc_malloc(sizeof(cam_device));

            state->devices[nb_usb_cameras]->usbPath = dc_strdup(usbPath);
            state->devices[nb_usb_cameras]->videoPath = dc_strdup(videoPath);
 
            state->devices[nb_usb_cameras]->manufacturer = dc_strdup(manufacturer);
            state->devices[nb_usb_cameras]->product = dc_strdup(product);
            state->devices[nb_usb_cameras]->vendorID = dc_strdup(vendorID);
            state->devices[nb_usb_cameras]->productID = dc_strdup(productID);
            state->devices[nb_usb_cameras]->serialNumber = dc_strdup(serialNumber);
            state->devices[nb_usb_cameras]->audioPath = dc_strdup(audioPath);
            state->devices[nb_usb_cameras]->audioCard = (card_num != -1 ? new_int(card_num) : nullptr);
            state->devices[nb_usb_cameras]->audioDevice = (device_num != -1 ? new_int(device_num): nullptr);
        
            if(card_num != -1 && device_num != -1)
            {
               state->devices[nb_usb_cameras]->hw = (const char*)getAudioHW(state->devices[nb_usb_cameras]->audioCard, state->devices[nb_usb_cameras]->audioDevice);
            }
            else state->devices[nb_usb_cameras]->hw = nullptr;
            nb_usb_cameras++;
        }
        metadata = !metadata;
    }
    cam_device* cam;
    spdlog::debug("Number of Cameras: {}", nb_usb_cameras);
    for (int i = 0; i < nb_usb_cameras; i++)
    {
        spdlog::debug("Camera - {}", i);

        cam = state->devices[i];
        spdlog::debug("   USB Path:      {}", cam->usbPath);
        spdlog::debug("   Video Path:      {}", Utils::str_or_default(cam->videoPath, "[none]"));
        spdlog::debug("   Audio Path:      {}", Utils::str_or_default(cam->audioPath, "[none]"));
        spdlog::debug("   Card Number:      {}", (cam->audioCard != nullptr ? *cam->audioCard : -1));
        spdlog::debug("   Device Number:      {}", (cam->audioDevice != nullptr ? *cam->audioDevice : -1));
        spdlog::debug("   Manufacturer:  {}", Utils::str_or_default(cam->manufacturer, "[none]"));
        spdlog::debug("   Product        {}", Utils::str_or_default(cam->product, "[none]"));
        spdlog::debug("   Vendor ID:     {}", Utils::str_or_default(cam->vendorID, "[none]"));
        spdlog::debug("   Product ID:    {}", Utils::str_or_default(cam->productID, "[none]"));
        spdlog::debug("   Serial Number: {}", Utils::str_or_default(cam->serialNumber, "[none]"));
    }
    state->nb_cams = nb_usb_cameras;
    return 0;

    udev_enumerate_unref(enum_audio);
    udev_enumerate_unref(enum_video);
    udev_device_unref(dev);
    udev_device_unref(dev2);
    video_devices = nullptr;
    audio_devices = nullptr;
    dev_list_entry = nullptr;
    dev_list_entry2 = nullptr;
    
    udev_unref(udev);


}

// PlugHW is the alternate mode but I dont think I care about that.
char *getAudioHW(const int* card, const int* device)
{
    char *hw = (char*)dc_malloc(sizeof(char) * 12);

    snprintf(hw, sizeof(hw), "hw:%d,%d", *card, *device);
    spdlog::debug("AudioHW: {}", hw);

    return std::move(hw);
}


