#include <stdio.h>
#include "msg_queue.h"

#define QUEUE_NAME "/mq_queue"

mqd_t msg_queue_create(void)
{
    mqd_t mqd = -1;
    struct mq_attr attr;
    attr.mq_flags = 0;
    attr.mq_maxmsg = 10;
    attr.mq_msgsize = 256;
    attr.mq_curmsgs = 0;
    mqd = mq_open(QUEUE_NAME, O_CREAT | O_RDWR, 0666, &attr);
    printf("%s|%s|%d: mqd = %d\n", __FILE__, __func__, __LINE__, mqd);
    return mqd;
}

void msg_queue_final(mqd_t mqd)
{
    if (-1 != mqd) {
        mq_close(mqd);
        mq_unlink(QUEUE_NAME);
        mqd = -1;
    }
}

int send_message(mqd_t mqd, void *msg, int msg_len)
{
    int byte_send = -1;
    byte_send = mq_send(mqd, (char *)msg, msg_len, 0);
    return byte_send;
}