#pragma once

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <spdlog/spdlog.h>

const char dev[] = "/dev/videoX";
const int dev_len = 11;
const int max_cams = 10;

struct Camera
{
    const char *bus_info;
    const char *device_name;
    const char *audio_hw;
    int video_index;
    int nb_of_video_entries;
};
int xioctl(int fd, int request, void *arg);
int check_for_usb_address_match(Camera *camera, int nb_of_cameras, unsigned char *device_description);

const char *getDeviceName(int index);

int query_all_webcams(Camera **cameras, int *nb_of_cameras);

int pair_audio_index_with_camera(Camera *camera, int nb_of_cameras);
