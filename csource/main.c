/**
 * @file main.c
 * @brief 智能家居系统主程序入口
 * @description 初始化系统，创建所有监听线程，启动整个智能家居控制系统
 * 
 * 系统架构说明：
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                         main() 主函数                           │
 * ├─────────────────────────────────────────────────────────────────┤
 * │  1. 初始化 wiringPi (GPIO控制基础)                              │
 * │  2. 创建消息队列 (线程间通信桥梁)                                │
 * │  3. 添加4个控制接口到链表 (语音/网络/烟雾/消息处理)              │
 * │  4. 遍历链表，为每个接口创建线程                                 │
 * │  5. 主线程进入无限循环，等待子线程运行                           │
 * └─────────────────────────────────────────────────────────────────┘
 *                                    │
 *                                    ▼
 * ┌─────────────────────────────────────────────────────────────────┐
 * │                        消息队列                                  │
 * │  ┌─────────┐  ┌─────────┐  ┌─────────┐                        │
 * │  │语音指令 │  │网络指令 │  │烟雾报警 │  ...                    │
 * │  └─────────┘  └─────────┘  └─────────┘                        │
 * └─────────────────────────────────────────────────────────────────┘
 *         ▲            ▲            ▲                    │
 *         │            │            │                    │
 *    voice_get    socket_get   smoke_get          receive_get
 *    (语音线程)   (网络线程)   (烟雾线程)          (处理线程)
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <wiringPi.h>
#include <pthread.h>
#include "uartTool.h"
#include "garbage.h"
#include "pwm.h"
#include "socket.h"
#include "control.h"
#include "voice_interface.h"
#include "socket_interface.h"
#include "smoke_interface.h"
#include "receive_interface.h"
#include "global.h"
#include "msg_queue.h"

/*===========================================================================
 * 静态全局变量
 *===========================================================================*/

static mqd_t mqd = -1;                      // 消息队列描述符（所有线程共享）
static struct control *ctrl_phead = NULL;   // 控制接口链表头指针
static ctrl_info_t ctrl_info;               // 全局信息结构体（包含消息队列和链表头）

/*===========================================================================
 * 主函数：程序入口
 *===========================================================================*/

/**
 * @brief 主函数
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 成功返回0，失败返回-1
 * 
 * 执行流程：
 * 1. 初始化wiringPi库（GPIO控制基础）
 * 2. 创建POSIX消息队列（线程间通信）
 * 3. 将4个控制接口添加到链表（语音/网络/烟雾/消息处理）
 * 4. 遍历链表，为每个接口创建独立线程
 * 5. 主线程无限循环，保持程序运行
 * 6. 程序退出时释放资源（正常情况下不会执行）
 */
int main(int argc, char *argv[])
{
    int ret = -1;                   // 临时返回值变量
    pthread_t tid = -1;             // 线程ID（循环复用，不需要保存每个线程ID）
    struct control *pctrl = NULL;   // 遍历控制链表的指针
    
    /*=========================================================================
     * 第1步：初始化wiringPi库
     * 
     * wiringPiSetup() 是wiringPi库的初始化函数
     * 必须在使用任何GPIO操作前调用
     * 它会初始化引脚映射和底层硬件
     *=========================================================================*/
    wiringPiSetup();
    printf("wiringPi initialized\n");
    
    /*=========================================================================
     * 第2步：创建POSIX消息队列
     * 
     * 消息队列是线程间通信的核心：
     * - 生产者线程（语音/网络/烟雾）将指令发送到队列
     * - 消费者线程（receive）从队列取出指令并处理
     * 
     * 队列属性：
     * - 最大消息数：10条
     * - 每条消息最大大小：256字节
     * - 阻塞模式（队列满时发送方阻塞）
     *=========================================================================*/
    mqd = msg_queue_create();
    if ((mqd_t)-1 == mqd) {
        printf("msg_queue_create failed\n");
        return -1;
    }
    printf("message queue created, mqd=%d\n", mqd);
    
    /*=========================================================================
     * 第3步：初始化全局信息结构体
     * 
     * ctrl_info 结构体包含：
     * - mqd: 消息队列描述符（供所有线程使用）
     * - ctrl_phead: 控制链表头（供receive线程查找语音接口进行播报）
     *=========================================================================*/
    ctrl_info.mqd = mqd;                    // 保存消息队列描述符
    ctrl_info.ctrl_phead = ctrl_phead;      // 初始为NULL，后面会更新
    
    /*=========================================================================
     * 第4步：将所有控制接口添加到链表（头插法）
     * 
     * 添加顺序（从后往前）：
     * 1. voice     - 语音监听接口
     * 2. socket    - 网络监听接口
     * 3. smoke     - 烟雾报警接口
     * 4. receive   - 消息处理接口（最后添加，会变成链表头）
     * 
     * 最终链表结构（头插法导致添加顺序与链表顺序相反）：
     * ctrl_phead → [receive] → [smoke] → [socket] → [voice] → NULL
     * 
     * 为什么receive在链表头？
     * - receive线程需要遍历链表找到voice接口进行语音播报
     * - 放在头部可以提高查找效率（虽然不是必须）
     *=========================================================================*/
    ctrl_phead = add_voice_to_ctrl_list(ctrl_phead);        // 添加语音接口
    printf("voice interface added to control list\n");
    
    ctrl_phead = add_tcpsocket_to_ctrl_list(ctrl_phead);    // 添加网络接口
    printf("socket interface added to control list\n");
    
    ctrl_phead = add_smoke_to_ctrl_list(ctrl_phead);        // 添加烟雾接口
    printf("smoke interface added to control list\n");
    
    ctrl_phead = add_receive_to_ctrl_list(ctrl_phead);      // 添加消息处理接口
    printf("receive interface added to control list\n");
    
    /* 更新全局信息中的控制链表头（receive线程需要用到） */
    ctrl_info.ctrl_phead = ctrl_phead;
    
    /*=========================================================================
     * 第5步：遍历控制链表，为每个接口创建独立线程
     * 
     * 每个控制接口都实现了三个函数：
     * - init:   初始化函数（打开串口、创建socket、初始化硬件等）
     * - get:    接收函数（线程主函数，监听指令）
     * - set:    发送函数（可选，用于语音播报等）
     * - final:  释放函数（程序结束时清理资源）
     * 
     * 线程创建流程：
     * 1. 调用接口的init函数进行初始化
     * 2. 调用pthread_create创建线程，执行接口的get函数
     * 3. 传入ctrl_info指针作为参数（包含消息队列和链表头）
     *=========================================================================*/
    pctrl = ctrl_phead;                     // 从链表头开始遍历
    while (NULL != pctrl) {
        /* 5.1 调用接口的初始化函数 */
        if (pctrl->init) {
            ret = pctrl->init();            // 执行初始化（如打开串口、创建socket）
            printf("%s init returned %d\n", pctrl->control_name, ret);
        }
        
        /* 5.2 创建线程，执行接口的get函数（监听线程） */
        if (pctrl->get) {
            /* 
             * pthread_create 参数说明：
             * - &tid:    线程ID存储位置
             * - NULL:    线程属性（默认）
             * - pctrl->get: 线程函数指针（如 voice_get、smoke_get 等）
             * - &ctrl_info: 传递给线程函数的参数
             */
            if (pthread_create(&tid, NULL, pctrl->get, (void *)&ctrl_info) != 0) {
                printf("Failed to create thread for %s\n", pctrl->control_name);
            } else {
                printf("Thread created for %s\n", pctrl->control_name);
            }
        }
        
        /* 5.3 移动到下一个接口 */
        pctrl = pctrl->next;
    }
    
    /*=========================================================================
     * 第6步：主线程进入无限循环
     * 
     * 主线程不能退出，原因：
     * - 如果main函数返回，所有子线程都会被终止
     * - 需要保持程序持续运行
     * 
     * 使用sleep让主线程休眠，把CPU让给子线程
     * 实际项目中也可以使用pause()或信号量等待
     *=========================================================================*/
    printf("\n=== Smart Home System Started ===\n");
    printf("Voice control: speak to the voice module (UART5)\n");
    printf("Network control: connect to port %s\n", IPPORT);
    printf("Smoke detection: monitoring GPIO6\n");
    printf("================================\n\n");
    
    while (1) {
        sleep(10);      // 主线程休眠10秒，让子线程运行
        // 注意：这里永远不会退出，除非收到外部信号
    }
    
    /*=========================================================================
     * 第7步：程序退出时的清理工作
     * 
     * 注意：正常情况下不会执行到这里（因为上面是无限循环）
     * 如果程序需要优雅退出，应该使用信号处理机制
     * 
     * 清理顺序：
     * 1. 遍历链表，调用每个接口的final函数
     * 2. 销毁消息队列
     *=========================================================================*/
    pctrl = ctrl_phead;
    while (NULL != pctrl) {
        if (pctrl->final) {
            pctrl->final();                 // 释放接口资源
        }
        pctrl = pctrl->next;
    }
    
    msg_queue_final(mqd);                   // 销毁消息队列
    printf("System shutdown\n");
    
    return 0;
}