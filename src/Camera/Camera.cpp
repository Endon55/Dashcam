#include "Camera.h"
#include <stdlib.h>
#include "../memory.h"
#include <spdlog/spdlog.h>

void camera_free(cam_device* camera)
{
    if (camera == nullptr)
    {
        return;

    }
    dc_free((void*)camera->usbPath);
    dc_free((void*)camera->videoPath);
    dc_free((void*)camera->audioPath);
    dc_free((void*)camera->audioCard);
    dc_free((void*)camera->audioDevice);
    dc_free((void*)camera->hw);
    dc_free((void*)camera->manufacturer);
    dc_free((void*)camera->product);
    dc_free((void*)camera->vendorID);
    dc_free((void*)camera->productID);
    dc_free((void*)camera->serialNumber);

   
   for(int i = 0; i < *camera->nb_cap_modes; i++)
   {
       capture_mode_free((capture_mode*)camera->cap_modes[i]);
   }

   dc_free((void*)camera->cap_modes);
   camera->default_mode = nullptr;

   dc_free((void*)camera->config_index);
   dc_free((void*)camera->nb_cap_modes);
   dc_free((void*)camera);
   return;
}

void capture_mode_free(capture_mode* mode)
{
    if(mode == nullptr)
    {
        return;
    }
    dc_free((void*)mode);
    return;
}

