#include "control.h"
struct control *add_device_to_ctrl_list(struct control *phead,struct control *device)
{
    struct control *pcontrol;
    if(NULL==phead){
        pcontrol = device;
        return pcontrol;
    }else
    {
        device->next=phead;
        phead=device;
        return phead;
    }
}
