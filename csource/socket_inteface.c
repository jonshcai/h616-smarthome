/**
 * @file socket_interface.c
 * @brief 网络监听接口
 * @description 实现TCP网络指令的监听和接收功能
 * 
 * 网络通信特点：
 * - 支持TCP KeepAlive心跳机制
 * - 心跳参数：空闲10秒，探测间隔5秒，探测3次
 */

#include <pthread.h>
#include "socket.h"
#include "control.h"
#include "socket_interface.h"
#include "msg_queue.h"
#include "global.h"

static int s_fd = -1;           // 服务器socket文件描述符

/**
 * @brief TCP Socket初始化
 * @return 成功返回socket描述符，失败返回-1
 */
static int tcpSocket_init(void)
{
    s_fd = socket_init(IPADDR, IPPORT);
    return s_fd;
}

/**
 * @brief TCP Socket资源释放
 */
static void tcpSocket_final(void)
{
    close(s_fd);
    s_fd = -1;
}

/**
 * @brief 网络接收线程函数（监听网络指令）
 * @param arg 参数指针，包含消息队列描述符
 * @return 线程退出指针
 * 
 * 工作流程：
 * 1. 等待客户端连接
 * 2. 为每个连接设置TCP KeepAlive
 * 3. 接收网络指令数据
 * 4. 将有效指令发送到消息队列
 * 
 * TCP KeepAlive参数说明：
 * - keepidle: 空闲10秒后开始发送探测包
 * - keepinterval: 探测包发送间隔5秒
 * - keepcount: 探测3次无响应后判定连接断开
 */
static void *tcpSocket_get(void *arg)
{
    int c_fd = -1;                      // 客户端socket描述符
    int ret = -1;
    struct sockaddr_in c_addr;          // 客户端地址信息
    unsigned char buffer[BUF_SIZE];     // 接收缓冲区（6字节）
    mqd_t mqd = -1;                     // 消息队列描述符
    ctrl_info_t *ctrl_info = NULL;
    
    // TCP KeepAlive参数
    int keepalive = 1;      // 开启KeepAlive
    int keepidle = 10;      // 空闲10秒后开始探测
    int keepinterval = 5;   // 探测包间隔5秒
    int keepcount = 3;      // 探测3次无响应后断开
    
    // 设置为分离状态
    pthread_detach(pthread_self());
    
    // 如果socket未初始化，则初始化
    if (-1 == s_fd) {
        s_fd = tcpSocket_init();
        if (-1 == s_fd) {
            printf("tcpSocket_init failed\n");
            pthread_exit(0);
        }
    }
    
    // 获取消息队列描述符
    if (NULL != arg)
        ctrl_info = (ctrl_info_t *)arg;
    if (NULL != ctrl_info) {
        mqd = ctrl_info->mqd;
    }
    if ((mqd_t)-1 == mqd) {
        pthread_exit(0);
    }
    
    // 清空客户端地址结构体
    memset(&c_addr, 0, sizeof(struct sockaddr_in));
    int clen = sizeof(struct sockaddr_in);
    
    printf("%s thread start\n", __func__);
    
    // 主循环：持续监听网络连接
    while (1) {
        // 等待客户端连接（阻塞）
        c_fd = accept(s_fd, (struct sockaddr *)&c_addr, &clen);
        if (-1 == c_fd) {
            continue;   // 接受连接失败，继续等待
        }
        
        // 设置TCP KeepAlive选项
        setsockopt(c_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
        setsockopt(c_fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
        setsockopt(c_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepinterval, sizeof(keepinterval));
        setsockopt(c_fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcount, sizeof(keepcount));
        
        // 持续接收该客户端的数据
        while (1) {
            memset(buffer, 0, BUF_SIZE); 
            
            ret = recv(c_fd, buffer, BUF_SIZE, 0);
            
            if (ret > 0) {
                // 验证数据帧格式
                if (buffer[0] == 0xAA && buffer[1] == 0x55 && 
                    buffer[5] == 0xAA && buffer[4] == 0x55) {
                    // 有效指令，发送到消息队列
                    send_message(mqd, buffer, ret);
                }
            } else if (-1 == ret || 0 == ret) {
                // 连接断开或出错，退出内层循环，等待下一个连接
                break;
            }
        }
    }
    
    pthread_exit(0);
}

/**
 * @brief 网络控制接口结构体
 */
struct control tcpsocket_control = {
    .control_name = "tcpsocket",
    .init = tcpSocket_init,
    .final = tcpSocket_final,
    .get = tcpSocket_get,
    .set = NULL,
    .next = NULL
};

/**
 * @brief 将网络接口添加到控制链表
 * @param phead 控制链表头指针
 * @return 新的链表头指针
 */
struct control *add_tcpsocket_to_ctrl_list(struct control *phead)
{
    return add_device_to_ctrl_list(phead, &tcpsocket_control);
}