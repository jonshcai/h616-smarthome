/**
 * @file smoke_interface.c
 * @brief 烟雾报警监听接口
 * @description 实现烟雾传感器的检测和报警功能
 * 
 * 烟雾传感器连接GPIO6引脚，高电平表示正常，低电平表示烟雾触发
 * 报警时发送0x45指令码，参数0x00表示报警，0x01表示恢复
 */

#include <pthread.h>
#include <wiringPi.h>
#include <stdio.h>
#include "control.h"
#include "smoke_interface.h"
#include "msg_queue.h"
#include "global.h"

#define SMOKE_PIN 6         // 烟雾传感器连接的GPIO引脚
#define SMOKE_MODE INPUT    // 输入模式

/**
 * @brief 烟雾传感器初始化
 * @return 成功返回0
 */
static int smoke_init(void)
{
    printf("%s|%s|%d\n", __FILE__, __func__, __LINE__);
    pinMode(SMOKE_PIN, SMOKE_MODE);     // 设置引脚为输入模式
    return 0;
}

/**
 * @brief 烟雾传感器资源释放（无需操作）
 */
static void smoke_final(void)
{
    // 无需特殊释放操作
}

/**
 * @brief 烟雾检测线程函数
 * @param arg 参数指针，包含消息队列描述符
 * @return 线程退出指针
 * 
 * 工作流程：
 * 1. 持续读取烟雾传感器状态
 * 2. 当状态变化时（高→低：报警，低→高：恢复）
 * 3. 发送相应指令到消息队列
 * 
 * 状态说明：
 * - 正常状态（HIGH）：buffer[3]=0x01
 * - 报警状态（LOW）：buffer[3]=0x00
 */
static void *smoke_get(void *arg)
{
    int status = HIGH;                  // 当前传感器状态
    int switch_status = 0;              // 报警状态标记（0=正常，1=报警）
    unsigned char buffer[6] = {0xAA, 0x55, 0x00, 0x00, 0x55, 0xAA};
    ssize_t byte_send = -1;
    mqd_t mqd = -1;
    ctrl_info_t *ctrl_info = NULL;
    
    // 获取消息队列描述符
    if (NULL != arg)
        ctrl_info = (ctrl_info_t *)arg;
    if (NULL != ctrl_info) {
        mqd = ctrl_info->mqd;
    }
    if ((mqd_t)-1 == mqd) {
        pthread_exit(0);
    }
    
    // 设置为分离状态
    pthread_detach(pthread_self());
    printf("%s thread start\n", __func__);
    
    // 主循环：持续监测烟雾状态
    while (1) {
        // 读取GPIO引脚状态
        status = digitalRead(SMOKE_PIN);
        
        // 烟雾触发（低电平）
        if (LOW == status) {
            buffer[2] = 0x45;       // 指令码：0x45表示烟雾报警
            buffer[3] = 0x00;       // 参数：0x00表示报警触发
            switch_status = 1;      // 标记报警状态
            
            // 发送报警消息到队列
            byte_send = mq_send(mqd, buffer, 6, 0);
            if (-1 == byte_send) {
                continue;
            }
        } 
        // 烟雾恢复（高电平且之前处于报警状态）
        else if (HIGH == status && 1 == switch_status) {
            buffer[2] = 0x45;       // 指令码：0x45表示烟雾报警
            buffer[3] = 0x01;       // 参数：0x01表示报警恢复
            switch_status = 0;      // 清除报警标记
            
            // 发送恢复消息到队列
            byte_send = mq_send(mqd, buffer, 6, 0);
            if (-1 == byte_send) {
                continue;
            }
        }
        
        // 每5秒检测一次
        sleep(5);
    }
    
    pthread_exit(0);
}

/**
 * @brief 烟雾报警接口结构体
 */
struct control smoke_control = {
    .control_name = "smoke",
    .init = smoke_init,
    .final = smoke_final,
    .get = smoke_get,
    .set = NULL,
    .next = NULL
};

/**
 * @brief 将烟雾报警接口添加到控制链表
 * @param phead 控制链表头指针
 * @return 新的链表头指针
 */
struct control *add_smoke_to_ctrl_list(struct control *phead)
{
    return add_device_to_ctrl_list(phead, &smoke_control);
}