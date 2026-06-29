#pragma once

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libudev.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}

#include <spdlog/spdlog.h>
#include "../Utils.h"
#include "../Config.h"

const char dev[] = "/dev/videoX";
const int dev_len = 11;
const int max_cams = 10;
const int MAX_CAP_MODES = 100;

struct capture_mode
{
    int width = 0;
    int height = 0;
    unsigned int pixelFormat = 0;
    double fps = 0.0;
};


struct Camera
{
    const char *bus_info;
    const char *device_name;
    const char *audio_hw;
    int video_index;
    int nb_of_video_entries;
    struct capture_mode *capture_mode;
};

struct cam_device
{
    const char *usbPath;
    const char *videoPath;
    const char *audioPath;
    const int  *audioCard;
    const int  *audioDevice;
    const char *manufacturer;
    const char *product;
    const char *vendorID;
    const char *productID;
    const char *serialNumber;

    const capture_mode *cap_modes;
    const int          *nb_cap_modes;
};

static const char *soundValidation = "/dev/snd/pcm";



int xioctl(int fd, int request, void *arg);

int check_for_usb_address_match(Camera *camera, int nb_of_cameras, unsigned char *device_description);

const char *getDeviceName(int index);

int query_all_webcams(Camera **cameras, int *nb_of_cameras);

int pair_audio_index_with_camera(Camera *camera, int nb_of_cameras);

AVPixelFormat canonicalizePixelFormat(AVPixelFormat pixelFormat);

int sourceColorRangeForFrame(const AVFrame *frame);

const char *fourcc_to_str(uint32_t pixelFormat);

double get_highest_fps(int fd, uint32_t pixelFormat, int width, int height);

int capture_mode_score(int width, int height, uint32_t pixelFormat, double fps);

capture_mode probeBestCaptureMode(const char *devicePath);

int findBestCaptureMode(Camera *camera);