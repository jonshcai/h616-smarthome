/**
 * @file voice_interface.c
 * @brief 语音模块监听接口
 * @description 实现语音模块的初始化、监听和播报功能
 * 
 * 语音模块通信协议：
 * - 串口波特率：115200
 * - 数据格式：{0xAA, 0x55, 指令码, 参数, 0x55, 0xAA}
 * - 指令码对应：0x41~0x45分别对应不同设备
 */

#include <pthread.h>
#include <stdio.h>
#include "voice_interface.h"
#include "uartTool.h"
#include "msg_queue.h"
#include "global.h"

static int serial_fd = -1;          // 串口文件描述符（全局，供所有线程使用）

/**
 * @brief 语音模块初始化
 * @return 成功返回串口文件描述符，失败返回-1
 */
static int voice_init(void)
{
    // 打开串口设备（/dev/ttyS5，115200波特率）
    serial_fd = myserialOpen(SERIAL_DEV, BAUD);
    printf("%s|%s|%d:serial_fd=%d\n", __FILE__, __func__, __LINE__, serial_fd);
    return serial_fd;
}

/**
 * @brief 语音模块资源释放
 */
static void voice_final(void)
{
    if (-1 != serial_fd) {
        close(serial_fd);       // 关闭串口
        serial_fd = -1;         // 标记为无效
    }
}

/**
 * @brief 语音接收线程函数（监听语音指令）
 * @param arg 参数指针，包含消息队列描述符和控制链表头
 * @return 线程退出指针
 * 
 * 工作流程：
 * 1. 从串口读取语音模块发来的指令
 * 2. 验证数据帧格式（以0xAA 0x55开头，0x55 0xAA结尾）
 * 3. 将有效指令发送到消息队列，供处理线程处理
 */
static void *voice_get(void *arg)
{
    unsigned char buffer[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    //unsigned char buffer[6] = {0x00,0x00,0x0,0x00,0x00,0x00,0x00};
    //int len = 0; 
    int len = 0;
    mqd_t mqd = -1;
    ctrl_info_t *ctrl_info = NULL;
    
    // 获取全局信息结构体
    if (NULL != arg)
        ctrl_info = (ctrl_info_t *)arg;
    
    // 如果串口未打开，则打开串口
    if (-1 == serial_fd) {
        serial_fd = voice_init();
        if (-1 == serial_fd) {
            pthread_exit(0);    // 串口打开失败，退出线程
        }
    }
    
    // 获取消息队列描述符
    if (NULL != ctrl_info) {
        mqd = ctrl_info->mqd;
    }
    if ((mqd_t)-1 == mqd) {
        pthread_exit(0);        // 消息队列无效，退出线程
    }
    
    // 设置线程为分离状态（自动回收资源）
    pthread_detach(pthread_self());
    printf("%s thread start\n", __func__);
    
    // 主循环：持续监听语音指令
    while (1) {
        // 从串口读取数据
        len = serialGetstring(serial_fd, buffer);
        
        if (len > 0) {
            // 验证数据帧格式：帧头0xAA 0x55，帧尾0x55 0xAA
            if (buffer[0] == 0xAA && buffer[1] == 0x55 && 
                buffer[5] == 0xAA && buffer[4] == 0x55) {
                // 有效指令，发送到消息队列
                send_message(mqd, buffer, len);
            }
            // 清空缓冲区，准备下次读取
            memset(buffer, 0, sizeof(buffer));
        }
    }
    
    pthread_exit(0);
}

/**
 * @brief 语音播报线程函数
 * @param arg 要发送的指令数据
 * @return 线程退出指针
 * 
 * 功能：通过串口向语音模块发送指令，触发语音播报
 */
static void *voice_set(void *arg)
{
    // 设置为分离状态
    pthread_detach(pthread_self());
    
    unsigned char *buffer = (unsigned char *)arg;
    
    // 如果串口未打开，则打开串口
    if (-1 == serial_fd) {
        serial_fd = voice_init();
        if (-1 == serial_fd) {
            pthread_exit(0);
        }
    }
    
    // 发送指令到语音模块
    if (NULL != buffer) {
        serialSendstring(serial_fd, buffer, 6);
    }
    
    pthread_exit(0);
}

/**
 * @brief 语音控制接口结构体
 * 
 * 成员说明：
 * - control_name: 接口名称
 * - init: 初始化函数指针
 * - final: 资源释放函数指针
 * - get: 接收函数指针（监听语音指令）
 * - set: 发送函数指针（语音播报）
 */
struct control voice_control = {
    .control_name = "voice",
    .init = voice_init,
    .final = voice_final,
    .get = voice_get,
    .set = voice_set,
    .next = NULL
};

/**
 * @brief 将语音接口添加到控制链表
 * @param phead 控制链表头指针
 * @return 新的链表头指针
 */
struct control *add_voice_to_ctrl_list(struct control *phead)
{
    return add_device_to_ctrl_list(phead, &voice_control);
}