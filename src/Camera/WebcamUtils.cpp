#include "WebcamUtils.h"
#include <sound/asound.h>

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
    *cameras = new Camera[max_cams];
    Camera *cams = *cameras;

    int index = 0;

    while (true)
    {
        const char *devname = getDeviceName(index);
        int fd = open(devname, O_RDWR | O_NONBLOCK);
        if (fd < 0)
        {
            break;
        }
        struct v4l2_capability *camcap = (v4l2_capability *)malloc(sizeof(v4l2_capability));

        int ret = xioctl(fd, VIDIOC_QUERYCAP, camcap);
        if (ret < 0)
        {
            spdlog::critical("Xioctl Failed: {}", strerror(errno));
            return -1;
        }
        const char *bus_info = (const char *)camcap->bus_info;
        if (unique_webcams == 0 || strcmp(cams[unique_webcams - 1].bus_info, bus_info) != 0)
        {
            cams[unique_webcams] = {
                .bus_info = std::move((char *)camcap->bus_info),
                .device_name = std::move(devname),
                .audio_hw = NULL,
                .video_index = index,
                .nb_of_video_entries = 1};
            unique_webcams++;
        }
        else
        {
            cams[unique_webcams - 1].nb_of_video_entries++;
        }

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
        spdlog::debug("Device Name: {}, Device Index: {}, Audio Index: {}, Device Entries:{}, USB-ID: {}", cams[i].device_name, cams[i].video_index,cams[i].audio_hw, cams[i].nb_of_video_entries, cams[i].bus_info);
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
            if(ret < 0)
            {
                spdlog::critical("Failed to read audio device info: {}", strerror(errno));
                return -1;
            }
            camera[cam_index].audio_hw = new char[12];

            snprintf((char*)camera[cam_index].audio_hw, 12, "hw:%d,%d", card, device);
        }
        close(fd);
    }
    return 0;
}