#pragma once

#define MAX_CAP_MODES 100
#define MAX_CAMS 10

struct capture_mode
{
    int width = 0;
    int height = 0;
    unsigned int pixelFormat = 0;
    const char * pixelFmtStr;
    double fps = 0.0;
};

struct cam_device
{
    const char *usbPath;
    const char *videoPath;
    const char *audioPath;
    const int *audioCard;
    const int *audioDevice;
    const char *hw;
    const char *manufacturer;
    const char *product;
    const char *vendorID;
    const char *productID;
    const char *serialNumber;
    const capture_mode *default_mode;
    const capture_mode **cap_modes;
    const int *nb_cap_modes;
};


struct cam_config
{
    int index;
    bool enabled;

    const char *manufacturer;
    const char *product;
    const char *vendorID;
    const char *productID;
    const char *serialNumber;

    const capture_mode *default_mode;
    const capture_mode **cap_modes;
    const int *nb_cap_modes;
};



