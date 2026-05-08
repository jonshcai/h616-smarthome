# EcoHome - H616 智能家居控制系统

基于 Allwinner H616 ARM64 开发板的智能家居控制系统，支持语音控制、网络远程控制、烟雾报警、人脸识别门禁等功能。

## 项目架构

系统采用**多线程生产者-消费者模式**，通过 POSIX 消息队列将多个输入源汇聚到统一的设备调度模块：

```
                        main()
                          |
                wiringPiSetup()        [GPIO 初始化]
                          |
                msg_queue_create()     [POSIX 消息队列: /mq_queue]
                          |
            +-------------+-------------+-------------+
            |             |             |             |
     voice_control  tcpsocket_control  smoke_control  receive_control
     (UART 串口)    (TCP 服务器)      (GPIO 输入)    (消息调度)
            |             |             |                    |
        voice_init    tcpSocket_init  smoke_init        receive_init
            |             |             |           (解析 INI, 初始化 OLED, 人脸)
            v             v             v                    v
      voice_get()   tcpSocket_get() smoke_get()      receive_get()
      [线程]         [线程]         [线程]            [线程]
            |             |             |                    |
            +------+------+------+------> 消息队列 <---+
                                                   |
                                              handle_device()
                                              [每条消息一个线程]
                                                   |
                                         GPIO 控制 (wiringPi)
                                         人脸识别 (阿里云 API)
                                         语音反馈 (串口)
                                         OLED 显示 (I2C)
```

## 模块说明

### 四大控制输入

| 模块 | 源文件 | 输入方式 | 说明 |
|------|--------|----------|------|
| 语音控制 | `voice_interface.c` | UART5 `/dev/ttyS5` 115200 | 监听语音识别模块的串口指令 |
| 网络控制 | `socket_inteface.c` | TCP `192.168.1.21:8192` | 接收手机/客户端的网络命令，支持 TCP KeepAlive |
| 烟雾报警 | `smoke_interface.c` | GPIO6 数字输入 | 每 5 秒轮询烟雾传感器状态 |
| 消息调度 | `receive_interface.c` | 消息队列（消费者） | 统一调度设备、人脸识别、OLED、语音反馈 |

### 核心模块

| 模块 | 源文件 | 说明 |
|------|--------|------|
| 主程序 | `main.c` | 入口，初始化所有子系统，创建线程 |
| 控制接口管理 | `control.c` | 控制接口链表管理（插入/遍历） |
| 设备管理 | `gdevice.c` | 通用设备 GPIO 控制与链表操作 |
| 消息队列 | `msg_queue.c` | POSIX 消息队列封装 |
| TCP 服务器 | `socket.c` | TCP Socket 初始化与绑定 |
| 串口工具 | `uartTool.c` | UART 串口配置与读写工具函数 |
| 人脸识别 | `face.c` | 通过 Python C API 调用阿里云人脸识别接口 |
| OLED 显示 | `my_oled.c` | I2C OLED 显示屏驱动 (`/dev/i2c-3`) |
| INI 解析 | `ini.c` | 基于 inih 库的配置文件解析器 |

## 通信协议

系统采用 **6 字节命令帧** 格式：

```
[0xAA] [0x55] [cmd] [param] [0x55] [0xAA]
  ^      ^     ^      ^      ^      ^
  帧头          命令   参数   帧尾
```

### 命令码定义

| 命令码 | 设备 | 功能 |
|--------|------|------|
| `0x41` | 客厅灯 | 控制客厅灯光开关 |
| `0x42` | 卧室灯 | 控制卧室灯光开关 |
| `0x43` | 风扇/垃圾桶 | 控制风扇或智能垃圾桶 |
| `0x44` | 门锁 | 人脸识别门锁，需通过阿里云人脸验证 |
| `0x45` | 烟雾报警器 | 烟雾报警蜂鸣器控制 |

## 设备配置

运行时通过 `/etc/gdevice.ini` 文件动态配置设备参数：

```ini
[LV led]
key=0x41
gpio_pin=2
gpio_mode=OUTPUT
gpio_status=HIGH
check_face_status=0
voice_set_status=0
```

## 构建说明

### 环境要求

- **交叉编译器**: `aarch64-linux-gnu-gcc`
- **目标板依赖**: wiringPi, Python 3.10, expat, zlib, crypt
- **目标平台**: Allwinner H616 ARM64 Linux

### 编译

```bash
# 编译
make compile

# 清理
make clean

# 调试模式
make debug
```

### 编译前准备

1. 从目标 H616 开发板提取依赖库和头文件，放置到 `3rd/` 目录：
   ```
   3rd/
   ├── usr/local/include/       # wiringPi 头文件
   ├── usr/local/lib/           # wiringPi 库
   ├── usr/include/             # 系统头文件
   ├── usr/include/python3.10/  # Python 头文件
   ├── lib/aarch64-linux-gnu/   # 系统库
   └── usr/lib/python3.10/      # Python 库
   ```

2. 确保目标板上存在 `face.py` 人脸识别脚本（使用阿里云人脸比对 API）

## 硬件连接

| 外设 | 接口 | 引脚/设备 |
|------|------|-----------|
| 语音模块 | UART5 | `/dev/ttyS5` (115200 baud) |
| OLED 显示屏 | I2C | `/dev/i2c-3` |
| 烟雾传感器 | GPIO | GPIO6 (输入) |
| 灯光/风扇/门锁/蜂鸣器 | GPIO | 见 `gdevice.ini` 配置 |

## 目录结构

```
ecohome/
├── Makefile           # 交叉编译构建文件
├── README.md          # 项目说明文档
├── 3rd/               # 第三方依赖 sysroot
├── csource/           # C 源文件 (13 个)
│   ├── main.c         # 主程序入口
│   ├── control.c      # 控制接口链表管理
│   ├── voice_interface.c   # 语音控制模块
│   ├── socket_inteface.c   # TCP 网络控制模块
│   ├── smoke_interface.c   # 烟雾报警模块
│   ├── receive_interface.c # 消息调度核心模块
│   ├── msg_queue.c    # 消息队列封装
│   ├── socket.c       # TCP Socket 初始化
│   ├── uartTool.c     # 串口工具
│   ├── gdevice.c      # 设备 GPIO 控制
│   ├── face.c         # 人脸识别模块
│   ├── my_oled.c      # OLED 显示驱动
│   └── ini.c          # INI 配置解析
└── inc/               # 头文件 (13 个)
    ├── global.h       # 全局类型定义
    ├── control.h      # 控制接口结构体
    ├── voice_interface.h
    ├── socket_interface.h
    ├── smoke_interface.h
    ├── receive_interface.h
    ├── msg_queue.h
    ├── socket.h
    ├── uartTool.h
    ├── gdevice.h      # 设备结构体定义
    ├── face.h
    ├── myoled.h
    └── ini.h
```

## License

MIT
