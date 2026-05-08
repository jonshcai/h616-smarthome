/**
 * @file receive_interface.c
 * @brief 消息处理接口 - 核心业务逻辑模块
 * @description 从消息队列接收指令，执行对应的设备操作
 * 
 * 数据流程：
 * 语音线程 ─┐
 * 网络线程 ─┼→ 消息队列 ─→ receive线程 → 处理函数 → GPIO控制
 * 烟雾线程 ─┘                           ↓
 *                                    OLED显示
 *                                   语音播报
 * 
 * 指令格式：
 * buffer[0] = 0xAA    (帧头)
 * buffer[1] = 0x55    (帧头)
 * buffer[2] = cmd     (指令码: 0x41~0x45对应不同设备)
 * buffer[3] = param   (参数: 0x00=关/报警, 0x01=开/恢复)
 * buffer[4] = 0x55    (帧尾)
 * buffer[5] = 0xAA    (帧尾)
 * 
 * 指令码对照表：
 * 0x41 - 客厅灯
 * 0x42 - 卧室灯
 * 0x43 - 风扇/可回收垃圾桶
 * 0x44 - 门锁（需要人脸识别验证）
 * 0x45 - 蜂鸣器/烟雾报警
 * 0x46 - 人脸识别失败
 * 0x47 - 人脸识别成功
 */

#include <pthread.h>
#include <mqueue.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "wiringPi.h"
#include "control.h"
#include "receive_interface.h"
#include "msg_queue.h"
#include "global.h"
#include "face.h"
#include "myoled.h"
#include "ini.h"
#include "gdevice.h"

/*===========================================================================
 * 数据结构定义
 *===========================================================================*/

/**
 * @brief 消息接收结构体
 * @param msg_len    消息长度（字节数）
 * @param buffer     消息缓冲区指针（存放原始指令数据）
 * @param ctrl_info  全局信息结构体指针（包含消息队列描述符和控制链表头）
 */
typedef struct {
    int msg_len;                    // 消息长度（字节数）
    unsigned char *buffer;          // 消息缓冲区（指向6字节指令数据）
    ctrl_info_t *ctrl_info;         // 全局信息（消息队列、控制链表头）
} recv_msg_t;


/*===========================================================================
 * 静态全局变量
 *===========================================================================*/

static int oled_fd = -1;                // OLED设备文件描述符（-1表示未打开）
static struct gdevice *pdevhead = NULL; // 设备链表头指针（从INI配置文件创建）

/*===========================================================================
 * INI配置文件解析回调函数
 *===========================================================================*/

/**
 * @brief INI配置文件解析回调函数
 * @param user     用户数据（未使用）
 * @param section  节名称（对应设备名称，如"LV led"）
 * @param name     键名（配置项名称，如"key"）
 * @param value    键值（配置项值，如"0x41"）
 * @return 返回1表示继续解析，返回0表示停止解析
 * 
 * 配置示例（/etc/gdevice.ini）：
 * [LV led]                    # 客厅灯
 * key=0x41                    # 指令码
 * gpio_pin=2                  # GPIO引脚号
 * gpio_mode=OUTPUT            # 引脚模式
 * gpio_status=HIGH            # 初始状态
 * check_face_status=0         # 是否需要人脸识别
 * voice_set_status=0          # 是否需要语音播报
 */
static int handler_gdevice(void *user, const char *section, const char *name, const char *value)
{
    struct gdevice *pdev = NULL;
    
    (void)user;  // 避免编译警告
    
    /* 情况1：设备链表为空，创建第一个设备节点 */
    if (NULL == pdevhead) {
        pdevhead = (struct gdevice *)malloc(sizeof(struct gdevice));
        if (NULL == pdevhead) {
            printf("malloc failed in handler_gdevice\n");
            return 0;
        }
        pdevhead->next = NULL;
        memset(pdevhead, 0, sizeof(struct gdevice));
        strcpy(pdevhead->dev_name, section);    // 设置设备名称
    }
    /* 情况2：节名称改变，创建新节点（头插法） */
    else if (0 != strcmp(section, pdevhead->dev_name)) {
        pdev = (struct gdevice *)malloc(sizeof(struct gdevice));
        if (NULL == pdev) {
            printf("malloc failed in handler_gdevice\n");
            return 0;
        }
        memset(pdev, 0, sizeof(struct gdevice));
        strcpy(pdev->dev_name, section);
        pdev->next = pdevhead;      // 新节点指向原头节点
        pdevhead = pdev;            // 新节点成为新头节点
    }
    /* 解析当前节点的配置项 */
    if (NULL != pdevhead) {
        /* 解析key值（16进制格式） */
        if (strcmp(name, "key") == 0) {
            sscanf(value, "%x", &pdevhead->key);
        }
        /* 解析GPIO引脚号 */
        else if (strcmp(name, "gpio_pin") == 0) {
            pdevhead->gpio_pin = atoi(value);
        }
        /* 解析GPIO模式 */
        else if (strcmp(name, "gpio_mode") == 0) {
            if (strcmp(value, "OUTPUT") == 0) {
                pdevhead->gpio_mode = OUTPUT;
            } else if (strcmp(value, "INPUT") == 0) {
                pdevhead->gpio_mode = INPUT;
            }
        }
        /* 解析GPIO初始状态 */
        else if (strcmp(name, "gpio_status") == 0) {
            if (strcmp(value, "LOW") == 0) {
                pdevhead->gpio_status = LOW;
            } else if (strcmp(value, "HIGH") == 0) {
                pdevhead->gpio_status = HIGH;
            }
        }
        /* 解析是否需要人脸识别 */
        else if (strcmp(name, "check_face_status") == 0) {
            pdevhead->check_face_status = atoi(value);
        }
        /* 解析是否需要语音播报 */
        else if (strcmp(name, "voice_set_status") == 0) {
            pdevhead->voice_set_status = atoi(value);
        }
    }
    
    return 1;   // 返回1继续解析下一行
}

/*===========================================================================
 * 接收接口的初始化/释放函数
 *===========================================================================*/

/**
 * @brief 接收接口初始化
 * @return 成功返回OLED文件描述符，失败返回-1
 * 
 * 初始化步骤：
 * 1. 解析INI配置文件，创建设备链表
 * 2. 初始化OLED屏幕
 * 3. 初始化人脸识别模块
 */
static int receive_init(void)
{
    int ret;
    
    /* 1. 解析INI配置文件，动态创建设备链表 */
    ret = ini_parse("/etc/gdevice.ini", handler_gdevice, NULL);
    if (ret < 0) {
        printf("Can't load '/etc/gdevice.ini'\n");
    } else {
        printf("Loaded devices from config file\n");
    }
    
    /* 2. 初始化OLED屏幕 */
    oled_fd = myoled_init();
    if (oled_fd < 0) {
        printf("OLED init failed\n");
    }
    
    /* 3. 初始化人脸识别模块 */
    face_init();
    
    return oled_fd;
}

/**
 * @brief 接收接口资源释放
 */
static void receive_final(void)
{
    /* 1. 释放人脸识别模块 */
    face_final();
    
    /* 2. 关闭OLED设备 */
    if (oled_fd != -1) {
        close(oled_fd);
        oled_fd = -1;
    }
}

/*===========================================================================
 * 设备处理线程函数
 *===========================================================================*/

/**
 * @brief 设备处理线程函数
 * @param arg 消息接收结构体指针
 * @return 线程退出指针
 * 
 * 功能流程：
 * 1. 解析指令码，查找对应设备
 * 2. 如果需要人脸识别，调用阿里云API验证
 * 3. 执行GPIO控制
 * 4. 触发语音播报
 * 5. OLED显示操作结果
 */
static void *handle_device(void *arg)
{
    recv_msg_t *recv_msg = NULL;
    struct gdevice *cur_gdev = NULL;
    char success_or_failed[20] = "success";
    int ret = -1;
    pthread_t tid = -1;
    int smoke_status = 0;
    double face_result = 0.0;
    
    /* 设置为分离状态，线程结束时自动回收资源 */
    pthread_detach(pthread_self());
    
    /* 获取消息数据 */
    if (NULL != arg) {
        recv_msg = (recv_msg_t *)arg;
    }
    
    /* 根据指令码查找对应的设备节点 */
    if (NULL != recv_msg && NULL != recv_msg->buffer) {
        cur_gdev = find_device_by_key(pdevhead, recv_msg->buffer[2]);
    }
    
    /* 如果找到对应设备，执行控制操作 */
    if (NULL != cur_gdev) {
        /* 设置GPIO状态（参数0表示关闭，1表示打开） */
        cur_gdev->gpio_status = recv_msg->buffer[3] == 0 ? LOW : HIGH;
        
        /*-------------------------------------------------------------------
         * 特殊情况1：门锁需要人脸识别验证
         *-------------------------------------------------------------------*/
        if (1 == cur_gdev->check_face_status) {
            /* 调用阿里云人脸识别API */
            face_result = face_category();
            printf("face_result=%f\n", face_result);
            
            /* Score > 0.6 表示人脸匹配成功 */
            if (face_result > 0.6) {
                ret = set_gpio_gdevice_status(cur_gdev);  // 开门
                recv_msg->buffer[2] = 0x47;               // 修改指令码为成功
            } else {
                recv_msg->buffer[2] = 0x46;               // 修改指令码为失败
                ret = -1;
            }
        }
        /*-------------------------------------------------------------------
         * 普通设备：直接执行GPIO控制
         *-------------------------------------------------------------------*/
        else if (0 == cur_gdev->check_face_status) {
            ret = set_gpio_gdevice_status(cur_gdev);
        }
        
        /*-------------------------------------------------------------------
         * 语音播报：如果设备配置了语音播报，创建语音播报线程
         *-------------------------------------------------------------------*/
        if (1 == cur_gdev->voice_set_status) {
            if (NULL != recv_msg && NULL != recv_msg->ctrl_info && 
                NULL != recv_msg->ctrl_info->ctrl_phead) {
                struct control *pcontrol = recv_msg->ctrl_info->ctrl_phead;
                /* 遍历控制链表，找到语音接口 */
                while (NULL != pcontrol) {
                    if (strstr(pcontrol->control_name, "voice")) {
                        /* 创建语音播报线程，发送指令到语音模块 */
                        pthread_create(&tid, NULL, pcontrol->set, (void *)recv_msg->buffer);
                        break;
                    }
                    pcontrol = pcontrol->next;
                }
            }
        }
        
        /* 如果操作失败，修改状态字符串 */
        if (-1 == ret) {
            memset(success_or_failed, '\0', sizeof(success_or_failed));
            strncpy(success_or_failed, "failed", 6);
        }
        
        /*-------------------------------------------------------------------
         * OLED显示操作结果
         *-------------------------------------------------------------------*/
        char oled_msg[512];
        memset(oled_msg, 0, sizeof(oled_msg));
        char *change_status = cur_gdev->gpio_status == LOW ? "Open" : "Close";
        sprintf(oled_msg, "%s %s %s!\n", change_status, cur_gdev->dev_name, success_or_failed);
        
        /* 特殊处理：烟雾报警显示火灾警告 */
        if (smoke_status == 1) {
            memset(oled_msg, 0, sizeof(oled_msg));
            strcpy(oled_msg, "A risk of fire!\n");
        }
        
        /* 更新OLED屏幕显示 */
        oled_show(oled_msg);
        
        /*-------------------------------------------------------------------
         * 门锁特殊处理：开门5秒后自动关门
         *-------------------------------------------------------------------*/
        if (1 == cur_gdev->check_face_status && 0 == ret && face_result > 0.6) {
            sleep(5);                       // 保持开门5秒
            cur_gdev->gpio_status = HIGH;   // 恢复高电平，电磁锁吸合关门
            set_gpio_gdevice_status(cur_gdev);
        }
    }
    
    pthread_exit(0);
}

/*===========================================================================
 * 消息接收线程函数
 *===========================================================================*/

/**
 * @brief 消息接收线程函数
 * @param arg 全局信息结构体指针（包含消息队列描述符和控制链表头）
 * @return 线程退出指针
 * 
 * 功能流程：
 * 1. 从消息队列中接收指令
 * 2. 验证指令格式
 * 3. 创建处理线程执行具体操作
 * 
 * 超时机制：使用mq_timedreceive设置5秒超时
 */
static void *receive_get(void *arg)
{
    recv_msg_t *recv_msg = NULL;
    ssize_t read_len = -1;
    pthread_t tid = -1;
    char *buffer = NULL;
    struct mq_attr attr;
    
    /* 分配消息接收结构体内存 */
    if (NULL != arg) {
        recv_msg = (recv_msg_t *)malloc(sizeof(recv_msg_t));
        if (NULL == recv_msg) {
            printf("malloc recv_msg failed\n");
            pthread_exit(0);
        }
        recv_msg->ctrl_info = (ctrl_info_t *)arg;   // 保存全局信息
        recv_msg->msg_len = -1;
        recv_msg->buffer = NULL;
    } else {
        pthread_exit(0);
    }
    
    /* 获取消息队列属性，获取消息大小 */
    if (mq_getattr(recv_msg->ctrl_info->mqd, &attr) == -1) {
        printf("mq_getattr failed\n");
        free(recv_msg);
        pthread_exit(0);
    }
    
    /* 分配接收缓冲区 */
    recv_msg->buffer = (unsigned char *)malloc(attr.mq_msgsize);
    buffer = (unsigned char *)malloc(attr.mq_msgsize);
    if (NULL == recv_msg->buffer || NULL == buffer) {
        printf("malloc buffer failed\n");
        free(recv_msg->buffer);
        free(buffer);
        free(recv_msg);
        pthread_exit(0);
    }
    memset(recv_msg->buffer, 0, attr.mq_msgsize);
    memset(buffer, 0, attr.mq_msgsize);
    
    /* 设置为分离状态 */
    pthread_detach(pthread_self());
    
    /* 设置超时时间：5秒 */
    struct timespec timeout = {.tv_sec = 5, .tv_nsec = 0};
    
    /* 主循环：持续从消息队列接收指令 */
    while (1) {
        /* 从消息队列接收消息（带超时） */
        read_len = mq_timedreceive(recv_msg->ctrl_info->mqd, buffer, 
                                    attr.mq_msgsize, NULL, &timeout);
        
        if (-1 == read_len) {
            /* 队列为空，继续等待 */
            if (errno == EAGAIN) {
                printf("queue is empty\n");
            }
            /* 超时，继续下一次循环 */
            else if (errno == ETIMEDOUT) {
                printf("timeout\n");
                continue;
            }
            /* 其他错误，退出循环 */
            else {
                break;
            }
        }
        /* 收到有效消息，验证帧格式 */
        else if (buffer[0] == 0xAA && buffer[1] == 0x55 && 
                 buffer[5] == 0xAA && buffer[4] == 0x55) {
            /* 保存消息数据 */
            recv_msg->msg_len = read_len;
            memcpy(recv_msg->buffer, buffer, read_len);
            
            /* 创建处理线程执行具体设备操作 */
            if (pthread_create(&tid, NULL, handle_device, (void *)recv_msg) != 0) {
                printf("pthread_create handle_device failed\n");
            }
        }
    }
    
    /* 释放资源 */
    free(recv_msg->buffer);
    free(buffer);
    free(recv_msg);
    
    pthread_exit(0);
}

/*===========================================================================
 * 控制接口结构体及注册函数
 *===========================================================================*/

/**
 * @brief 消息接收控制接口结构体
 * 
 * 成员说明：
 * - control_name: 接口名称
 * - init: 初始化函数指针
 * - final: 资源释放函数指针
 * - get: 接收函数指针（从消息队列接收指令）
 * - set: 发送函数指针（本接口不需要）
 */
struct control receive_control = {
    .control_name = "receive",
    .init = receive_init,
    .final = receive_final,
    .get = receive_get,
    .set = NULL,
    .next = NULL
};

/**
 * @brief 将消息接收接口添加到控制链表
 * @param phead 控制链表头指针
 * @return 新的链表头指针
 */
struct control *add_receive_to_ctrl_list(struct control *phead)
{
    return add_device_to_ctrl_list(phead, &receive_control);
}