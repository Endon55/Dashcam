
#include "Camera/Camera.h"

#include <fmt/core.h>
#include <string>
#include <iostream>



using namespace std;

 static const char DEVICE[] = "/dev/video0";

 
 int main(void) {

    char dev0[] = "/dev/video0";



    Camera* camera = new Camera(dev0);


     
 
   if(!camera->init_device())
   {
      cout << "Failed to init device" << endl;
      return -1;
   }
 
   camera->start_capturing();

   unsigned int count = 100;
   while (count-- > 0) 
   {
      camera->update();
   }


   camera->stop_capturing();


   printf("\n\nDone.\n");
   return 0;
   
 }
 