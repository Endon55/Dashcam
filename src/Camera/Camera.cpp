
#include "Camera.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <iostream>
#include <cstring>

Camera::Camera(int deviceNumber, bool (*frame_callback_func)(buffer *))
{
    this->frame_callback_func = frame_callback_func;
    device = new char[dev_len];
    strcpy(device, dev);
    device[dev_len - 1] = '0' + deviceNumber;
}
void Camera::getDimensions(int* width, int* height)
{
    *width = this->width;
    *height = this->height;
}
void Camera::update()
{

    fd_set fds;
    struct timeval tv;
    int r;

    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    tv.tv_sec = 5;
    tv.tv_usec = 0;

    r = select(fd + 1, &fds, NULL, NULL, &tv);

    if (-1 == r)
    {
        if (EINTR == errno)
            return;

        perror("select");
        exit(errno);
    }

    if (0 == r)
    {
        fprintf(stderr, "select timeout\n");
        exit(EXIT_FAILURE);
    }

    while (read_frame() != 1)
    {
        return;
    }
}
int Camera::xioctl(int fd, int request, void *arg)
{
    int r;

    do
    {
        r = ioctl(fd, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

void Camera::init_mmap(void)
{
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count = 5;
    cout << "Init MMAP2" << endl;
    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &reqbuf))
    {
        perror("VIDIOC_REQBUFS");
        exit(errno);
    }
    if (reqbuf.count < 2)
    {
        fprintf(stderr, "Not enough buffer memory\n");
        exit(EXIT_FAILURE);
    }
    buffers = (struct buffer *)calloc(reqbuf.count, sizeof(struct buffer));
    if (!buffers)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    num_buffers = reqbuf.count;

    struct v4l2_buffer buffer;
    for (unsigned int i = 0; i < num_buffers; i++)
    {
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = reqbuf.type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buffer))
        {
            perror("VIDIOC_QUERYBUF");
            exit(errno);
        }

        buffers[i].length = buffer.length;
        buffers[i].start = mmap(
            NULL,
            buffer.length,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            buffer.m.offset);

        if (MAP_FAILED == buffers[i].start)
        {
            perror("mmap");
            exit(errno);
        }
    }
}

bool Camera::init_device()
{

    fd = open(device, O_RDWR);
    if (fd < 0)
    {
        perror(device);
        return false;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    fmt.fmt.pix.width = 3840;
    fmt.fmt.pix.height = 2160;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
    {
        perror("VIDIOC_S_FMT");
        exit(errno);
    }
    this->width = fmt.fmt.pix.width;
    this->height = fmt.fmt.pix.height;
    char format_code[5];
    strncpy(format_code, (char *)&fmt.fmt.pix.pixelformat, 4);
    format_code[4] = '\0';
    printf(
        "Set format:\n"
        " Width: %d\n"
        " Height: %d\n"
        " Pixel format: %s\n"
        " Field: %d\n\n",
        fmt.fmt.pix.width,
        fmt.fmt.pix.height,
        format_code,
        fmt.fmt.pix.field);

    init_mmap();
    return true;
}

void Camera::start_capturing(void)
{
    enum v4l2_buf_type type;
    struct v4l2_buffer buffer;

    for (unsigned int i = 0; i < num_buffers; i++)
    {
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        if (-1 == xioctl(fd, VIDIOC_QBUF, &buffer))
        {
            perror("VIDIOC_QBUF");
            exit(errno);
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
    {
        perror("VIDIOC_STREAMON");
        exit(errno);
    }
}

void Camera::stop_capturing(void)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
    {
        perror("VIDIOC_STREAMOFF");
        exit(errno);
    }

    for (unsigned int i = 0; i < num_buffers; i++)
    {
        if (buffers[i].start)
        {
            munmap(buffers[i].start, buffers[i].length);
        }
    }

    free(buffers);
    close(fd);
}

bool Camera::process_image(buffer *buf)
{

    return frame_callback_func(buf);
}

int Camera::read_frame()
{
    struct v4l2_buffer buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buffer))
    {
        switch (errno)
        {
        case EAGAIN:
            return 0;
        case EIO:
        default:
            perror("VIDIOC_DQBUF _ Read Frame");
            exit(errno);
        }
    }

    assert(buffer.index < num_buffers);
    buffers[buffer.index].length = buffer.bytesused;
    if (process_image(&buffers[buffer.index]))
    {
        if (previous_buffer != NULL)
        {
            if (-1 == xioctl(fd, VIDIOC_QBUF, &buffer))
            {
                perror("VIDIOC_QBUF _ Read Frame");
                exit(errno);
            }
        }
        previous_buffer = &buffer;
    }

    return 1;
}
