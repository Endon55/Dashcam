#pragma once

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}

#include <spdlog/spdlog.h>

const char dev[] = "/dev/videoX";
const int dev_len = 11;
const int max_cams = 10;

enum Capture_Priority
{
    RESOLUTION = 1,
    FPS = 2,
    BALANCED = 3,
};

struct CaptureMode
{
    int width = 0;
    int height = 0;
    uint32_t pixelFormat = 0;
    const char *input_fmt = nullptr;
    double fps = 0.0;
};

struct Camera
{
    const char *bus_info;
    const char *device_name;
    const char *audio_hw;
    int video_index;
    int nb_of_video_entries;
    struct CaptureMode *capture_mode;
};

int xioctl(int fd, int request, void *arg);

int check_for_usb_address_match(Camera *camera, int nb_of_cameras, unsigned char *device_description);

const char *getDeviceName(int index);

int query_all_webcams(Camera **cameras, int *nb_of_cameras);

int pair_audio_index_with_camera(Camera *camera, int nb_of_cameras);

AVPixelFormat canonicalizePixelFormat(AVPixelFormat pixelFormat);

int sourceColorRangeForFrame(const AVFrame *frame);

const char *fourcc_to_str(uint32_t pixelFormat);

double get_highest_fps(int fd, uint32_t pixelFormat, int width, int height);

int capture_mode_score(int width, int height, uint32_t pixelFormat, double fps, Capture_Priority priority);

CaptureMode probeBestCaptureMode(const char *devicePath);

int findBestCaptureMode(Capture_Priority priority, Camera *camera);