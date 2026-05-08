#ifndef __GDEVICE__H
#define __GDEVICE__H
struct gdevice{
    char dev_name[128];
    int key;
    int gpio_pin;
    int gpio_mode;
    int gpio_status;
    int check_face_status;
    int voice_set_status;
    struct gdevice *next;
};

#endif