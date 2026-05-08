#ifndef __CONTROL_H
#define __CONTROL__H
#include <stdlib.h>
struct control
{
    char control_name[128];
    int (*init)(void);
    void (*final)(void);
    void *(*get)(void *arg);
    void *(*set)(void *arg);
    struct control *next;
};
struct control *add_device_to_ctrl_list(struct control *phead, struct control *device);
#endif