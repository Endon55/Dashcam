#pragma once

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <ctime>
#include <alsa/asoundlib.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <libudev.h>
#include <spdlog/spdlog.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
}

#include "Camera.h"
#include "../State.h"
#include "../Utils.h"
const char dev[] = "/dev/videoX";
const int dev_len = 11;

static const char *soundValidation = "/dev/snd/pcm";



int xioctl(int fd, int request, void *arg);

const char *getDeviceName(int index);

int query_all_capture_modes(AppState* state);

int query_capture_modes(cam_device* device);

int query_all_webcams(AppState* state);

int sourceColorRangeForFrame(const AVFrame *frame);

const char *fourcc_to_str(uint32_t pixelFormat);

double get_highest_fps(int fd, uint32_t pixelFormat, int width, int height);

char *getAudioHW(const int* card, const int* device);


