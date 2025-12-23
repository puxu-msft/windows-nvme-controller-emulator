/**
 * @file vnvme_server.h
 * @brief vnvme-server 公共头文件
 * 
 * 包含所有模块共享的类型定义、常量和宏。
 */

#ifndef _VNVME_SERVER_H_
#define _VNVME_SERVER_H_

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

// 共享定义
#include "../include/vnvme_common.h"
#include "../include/vnvme_ioctl.h"
#include "../include/nvme_spec.h"

//===========================================================================
// 版本信息
//===========================================================================

#define VNVME_SERVER_VERSION_MAJOR  1
#define VNVME_SERVER_VERSION_MINOR  0
#define VNVME_SERVER_VERSION_PATCH  0
#define VNVME_SERVER_VERSION_STRING "1.0.0"

//===========================================================================
// 配置结构
//===========================================================================

/**
 * 后端类型
 */
typedef enum _BACKEND_TYPE {
    BACKEND_TYPE_MEMORY = 0,
    BACKEND_TYPE_FILE   = 1,
    BACKEND_TYPE_MAX
} BACKEND_TYPE;

/**
 * 日志级别
 */
typedef enum _LOG_LEVEL {
    LOG_LEVEL_NONE    = 0,
    LOG_LEVEL_ERROR   = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_INFO    = 3,
    LOG_LEVEL_DEBUG   = 4,
    LOG_LEVEL_VERBOSE = 5
} LOG_LEVEL;

/**
 * 日志配置
 */
typedef struct _LOG_CONFIG {
    LOG_LEVEL   Level;                  // 日志级别
    BOOL        EnableConsole;          // 输出到控制台
    BOOL        EnableFile;             // 输出到文件
    BOOL        EnableColor;            // 控制台彩色输出
    WCHAR       FilePath[MAX_PATH];     // 日志文件路径
} LOG_CONFIG, *PLOG_CONFIG;

/**
 * 存储配置
 */
typedef struct _STORAGE_CONFIG {
    BACKEND_TYPE Type;                  // 后端类型
    UINT64      Size;                   // 存储大小 (字节)
    WCHAR       FilePath[MAX_PATH];     // 文件后端路径
    BOOL        ReadOnly;               // 只读模式
} STORAGE_CONFIG, *PSTORAGE_CONFIG;

/**
 * 控制器配置
 */
typedef struct _CONTROLLER_CONFIG {
    WCHAR       Name[64];               // 控制器名称
    WCHAR       ModelNumber[40];        // 型号
    WCHAR       SerialNumber[20];       // 序列号
    UINT16      VendorId;               // 厂商 ID
    UINT16      DeviceId;               // 设备 ID
} CONTROLLER_CONFIG, *PCONTROLLER_CONFIG;

/**
 * 主配置结构
 */
typedef struct _VNVME_SERVER_CONFIG {
    // 日志配置
    LOG_CONFIG          Log;
    
    // 存储配置
    STORAGE_CONFIG      Storage;
    
    // 控制器配置
    CONTROLLER_CONFIG   Controller;
    
    // 运行时选项
    UINT32              HeartbeatIntervalMs;    // 心跳间隔
    UINT32              PollIntervalUs;         // 轮询间隔
    BOOL                DaemonMode;             // 后台运行
    
    // 配置文件路径
    WCHAR               ConfigFilePath[MAX_PATH];
} VNVME_SERVER_CONFIG, *PVNVME_SERVER_CONFIG;

//===========================================================================
// 运行时上下文
//===========================================================================

/**
 * 共享内存上下文
 */
typedef struct _SHM_CONTEXT {
    PVOID               BaseAddress;        // 用户态映射基地址
    SIZE_T              Size;               // 总大小
    
    // 控制块
    PVNVME_SHM_CONTROL_BLOCK ControlBlock;
    
    // 通知环
    PVNVME_NOTIFY_RING  NotifyRing;
    
    // 完成通知环
    PVNVME_COMPLETION_NOTIFY_RING CompletionRing;
    
    // 数据缓冲区
    PVOID               DataBuffer;
    SIZE_T              DataBufferSize;
} SHM_CONTEXT, *PSHM_CONTEXT;

/**
 * 驱动通信上下文
 */
typedef struct _DRIVER_CONTEXT {
    HANDLE              DeviceHandle;       // 驱动设备句柄
    SHM_CONTEXT         Shm;                // 共享内存
    HANDLE              HeartbeatThread;    // 心跳线程
    volatile BOOL       Running;            // 运行状态
} DRIVER_CONTEXT, *PDRIVER_CONTEXT;

/**
 * 后端上下文 (前向声明，具体定义在 backend.h)
 */
typedef struct _BACKEND_CONTEXT BACKEND_CONTEXT, *PBACKEND_CONTEXT;

/**
 * 服务器上下文
 */
typedef struct _SERVER_CONTEXT {
    VNVME_SERVER_CONFIG Config;             // 配置
    DRIVER_CONTEXT      Driver;             // 驱动通信
    PBACKEND_CONTEXT    Backend;            // 存储后端
    volatile BOOL       ShutdownRequested;  // 关闭请求
} SERVER_CONTEXT, *PSERVER_CONTEXT;

//===========================================================================
// 错误码
//===========================================================================

#define VNVME_SUCCESS               0
#define VNVME_ERROR_INVALID_PARAM   1
#define VNVME_ERROR_NO_MEMORY       2
#define VNVME_ERROR_DRIVER_OPEN     3
#define VNVME_ERROR_SHM_MAP         4
#define VNVME_ERROR_BACKEND_INIT    5
#define VNVME_ERROR_CONFIG_LOAD     6
#define VNVME_ERROR_IO              7

//===========================================================================
// 工具宏
//===========================================================================

#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#define ALIGN_UP(x, align)      (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align)    ((x) & ~((align) - 1))

// 安全释放
#define SAFE_FREE(ptr)          do { if (ptr) { free(ptr); ptr = NULL; } } while(0)
#define SAFE_CLOSE_HANDLE(h)    do { if (h && h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = NULL; } } while(0)

//===========================================================================
// 模块头文件包含
//===========================================================================

#include "logger.h"

#endif /* _VNVME_SERVER_H_ */
