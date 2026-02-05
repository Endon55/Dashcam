#pragma once

#include <linux/videodev2.h>
#include <stdlib.h>
#include <string>

using namespace std;

const char dev[] = "/dev/videoX";
const int dev_len = 11;

class Camera
{
public:
    int fd;
    char* device;

    struct buffer *buffers;

    unsigned int num_buffers = 0;
    struct v4l2_requestbuffers reqbuf = {0};
    bool (*frame_callback_func)(buffer *);
    struct v4l2_buffer *previous_buffer;

private:
    int width;
    int height;

public:
    Camera(int deviceNumber, bool (*frame_callback_func)(buffer *));
    void update();
    bool init_device();
    void start_capturing(void);
    void stop_capturing(void);
    void getDimensions(int *width, int *height);

private:
    int xioctl(int fd, int request, void *arg);
    void init_mmap(void);
    bool process_image(buffer *pBuffer);
    int read_frame();
};

struct buffer
{
    void *start;
    size_t length;
};