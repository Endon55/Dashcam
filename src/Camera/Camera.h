#pragma once


#include <linux/videodev2.h>
#include <stdlib.h>
#include <string>

using namespace std;


class Camera
{
    public: 
        int fd;
        char* device;

        struct buffer *buffers;
        unsigned int num_buffers = 0;
        struct v4l2_requestbuffers reqbuf = {0};
    
    public:
        Camera(char* device);
        void update();
        bool init_device();
        void start_capturing(void);
        void stop_capturing(void);

    private:
        int xioctl(int fd, int request, void *arg);
        void init_mmap(void);
        void process_image(const void *pBuffer);
        int read_frame(void);


};


 struct buffer 
 {
     void *start;
     size_t length;
 };