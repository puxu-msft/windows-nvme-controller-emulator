/**
 * @file vnvme_ioctl.h
 * @brief VNVME IOCTL 接口定义
 * 
 * 本文件定义用户态与内核驱动之间的 IOCTL 通信接口。
 */

#ifndef _VNVME_IOCTL_H_
#define _VNVME_IOCTL_H_

#include "vnvme_common.h"

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <winioctl.h>
#endif

//===========================================================================
// IOCTL 代码定义
//===========================================================================

/* 设备类型 - 使用自定义类型 (unsigned 避免符号扩展) */
#define FILE_DEVICE_VNVME       0x8000U

/* IOCTL 功能码 */
#define VNVME_IOCTL_INDEX_BASE  0x800U

/* 
 * IOCTL 代码格式:
 * CTL_CODE(DeviceType, Function, Method, Access)
 */

/* 版本和信息 */
#define IOCTL_VNVME_GET_VERSION         \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 0, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_VNVME_GET_STATUS          \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 1, METHOD_BUFFERED, FILE_ANY_ACCESS)

/* 共享内存 */
#define IOCTL_VNVME_MAP_SHM   \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 10, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

#define IOCTL_VNVME_UNMAP_SHM \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 11, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

/* 用户态状态 */
#define IOCTL_VNVME_USER_READY          \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 20, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_VNVME_USER_SHUTDOWN       \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 21, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_VNVME_HEARTBEAT           \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 22, METHOD_BUFFERED, FILE_WRITE_DATA)

/* 事件通知 */
#define IOCTL_VNVME_GET_COMMAND_EVENT   \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 30, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_VNVME_SUBMIT_COMPLETIONS  \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 31, METHOD_BUFFERED, FILE_WRITE_DATA)

/* 控制器管理 */
#define IOCTL_VNVME_CREATE_CONTROLLER   \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 40, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_VNVME_DELETE_CONTROLLER   \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 41, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_VNVME_LIST_CONTROLLERS    \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 42, METHOD_BUFFERED, FILE_READ_DATA)

/* 命名空间管理 */
#define IOCTL_VNVME_CREATE_NAMESPACE    \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 50, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_VNVME_DELETE_NAMESPACE    \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 51, METHOD_BUFFERED, FILE_WRITE_DATA)

#define IOCTL_VNVME_LIST_NAMESPACES     \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 52, METHOD_BUFFERED, FILE_READ_DATA)

/* 调试 */
#define IOCTL_VNVME_GET_DEBUG_INFO      \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 100, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_VNVME_SET_DEBUG_LEVEL     \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 101, METHOD_BUFFERED, FILE_WRITE_DATA)

/* 性能统计 */
#define IOCTL_VNVME_GET_STATS           \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 110, METHOD_BUFFERED, FILE_READ_DATA)

#define IOCTL_VNVME_RESET_STATS         \
    CTL_CODE(FILE_DEVICE_VNVME, VNVME_IOCTL_INDEX_BASE + 111, METHOD_BUFFERED, FILE_WRITE_DATA)

//===========================================================================
// IOCTL 输入/输出结构
//===========================================================================

#pragma pack(push, 1)

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_GET_VERSION
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_GET_VERSION_OUTPUT {
    UINT32 DriverVersion;               // 驱动版本
    UINT32 ApiVersion;                  // API 版本
    UINT32 BuildNumber;                 // 构建号
    UINT32 Reserved;
} VNVME_GET_VERSION_OUTPUT, *PVNVME_GET_VERSION_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_GET_STATUS
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_GET_STATUS_OUTPUT {
    /* 基本状态 */
    UINT32 DriverStatus;                // 驱动状态 (VNVME_DRIVER_STATUS_*)
    UINT32 UserServiceStatus;           // 用户态服务状态 (VNVME_USER_STATUS_*)
    
    /* 设备统计 */
    UINT32 ControllerCount;             // 控制器数量
    UINT32 NamespaceCount;              // 命名空间数量
    
    /* 共享内存状态 */
    UINT32 ShmMapped;                   // 共享内存是否已映射 (0/1)
    UINT32 ShmSize;                     // 共享内存大小 (字节)
    
    /* 用户态连接 */
    UINT32 UserReady;                   // 用户态是否就绪 (0/1)
    UINT32 UserPid;                     // 用户态进程 ID
    
    /* 处理统计 */
    UINT64 CommandsProcessed;           // 已处理命令数
    UINT64 CompletionsPosted;           // 已提交完成数
    UINT64 BytesRead;                   // 读取字节数
    UINT64 BytesWritten;                // 写入字节数
    UINT64 ErrorCount;                  // 错误数
    
    /* 时间信息 */
    UINT64 UptimeMs;                    // 运行时间 (毫秒)
    UINT64 LastHeartbeatMs;             // 上次心跳时间 (毫秒)
    
} VNVME_GET_STATUS_OUTPUT, *PVNVME_GET_STATUS_OUTPUT;

/* 驱动状态码 */
#define VNVME_DRIVER_STATUS_INITIALIZING    0
#define VNVME_DRIVER_STATUS_READY           1
#define VNVME_DRIVER_STATUS_RUNNING         2
#define VNVME_DRIVER_STATUS_ERROR           3
#define VNVME_DRIVER_STATUS_STOPPING        4

/* 用户态服务状态码 */
#define VNVME_USER_STATUS_NOT_CONNECTED     0
#define VNVME_USER_STATUS_CONNECTED         1
#define VNVME_USER_STATUS_READY             2
#define VNVME_USER_STATUS_ERROR             3

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_MAP_SHM
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_MAP_SHM_INPUT {
    UINT32 RequestedSize;               // 请求大小 (0 = 使用默认)
    UINT32 Reserved;
} VNVME_MAP_SHM_INPUT, *PVNVME_MAP_SHM_INPUT;

typedef struct _VNVME_MAP_SHM_OUTPUT {
    PVOID UserAddress;                  // 映射到用户空间的地址
    UINT32 ActualSize;                  // 实际大小
    UINT32 Reserved;
    HANDLE CommandEventHandle;          // 命令事件句柄 (可等待)
} VNVME_MAP_SHM_OUTPUT, *PVNVME_MAP_SHM_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_USER_READY
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_USER_READY_INPUT {
    UINT32 UserPid;                     // 用户态进程 ID
    UINT32 UserVersion;                 // 用户态版本
    UINT32 Capabilities;                // 能力标志
    UINT32 Reserved;
} VNVME_USER_READY_INPUT, *PVNVME_USER_READY_INPUT;

#define VNVME_USER_CAP_ASYNC            0x0001  // 支持异步处理
#define VNVME_USER_CAP_BATCH            0x0002  // 支持批处理
#define VNVME_USER_CAP_DIRECT_IO        0x0004  // 支持直接 I/O

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_HEARTBEAT
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_HEARTBEAT_INPUT {
    UINT64 Timestamp;                   // 时间戳
    UINT64 CommandsProcessed;           // 已处理命令数
} VNVME_HEARTBEAT_INPUT, *PVNVME_HEARTBEAT_INPUT;

typedef struct _VNVME_HEARTBEAT_OUTPUT {
    UINT64 KernelTimestamp;             // 内核时间戳
    UINT32 PendingCommands;             // 待处理命令数
    UINT32 Reserved;
} VNVME_HEARTBEAT_OUTPUT, *PVNVME_HEARTBEAT_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_GET_COMMAND_EVENT
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_GET_COMMAND_EVENT_OUTPUT {
    HANDLE EventHandle;                 // 事件句柄
} VNVME_GET_COMMAND_EVENT_OUTPUT, *PVNVME_GET_COMMAND_EVENT_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_SUBMIT_COMPLETIONS
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_SUBMIT_COMPLETIONS_INPUT {
    UINT32 CompletionCount;             // 完成数量
    UINT32 ControllerId;                // 目标控制器 ID (0 = 广播到所有控制器)
} VNVME_SUBMIT_COMPLETIONS_INPUT, *PVNVME_SUBMIT_COMPLETIONS_INPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_CREATE_CONTROLLER
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_CREATE_CONTROLLER_INPUT {
    VNVME_CONTROLLER_CONFIG Config;     // 控制器配置
} VNVME_CREATE_CONTROLLER_INPUT, *PVNVME_CREATE_CONTROLLER_INPUT;

typedef struct _VNVME_CREATE_CONTROLLER_OUTPUT {
    UINT32 ControllerId;                // 分配的控制器 ID
    WCHAR DeviceInstanceId[200];        // 设备实例 ID
} VNVME_CREATE_CONTROLLER_OUTPUT, *PVNVME_CREATE_CONTROLLER_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_DELETE_CONTROLLER
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_DELETE_CONTROLLER_INPUT {
    UINT32 ControllerId;                // 控制器 ID
    UINT32 Force;                       // 强制删除 (即使有活动 I/O)
} VNVME_DELETE_CONTROLLER_INPUT, *PVNVME_DELETE_CONTROLLER_INPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_LIST_CONTROLLERS
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_CONTROLLER_INFO {
    UINT32 ControllerId;                // 控制器 ID
    UINT32 Status;                      // 状态
    CHAR SerialNumber[20];              // 序列号
    CHAR ModelNumber[40];               // 型号
    UINT32 NamespaceCount;              // 命名空间数
    UINT64 TotalCapacity;               // 总容量
} VNVME_CONTROLLER_INFO, *PVNVME_CONTROLLER_INFO;

typedef struct _VNVME_LIST_CONTROLLERS_OUTPUT {
    UINT32 ControllerCount;             // 控制器数量
    UINT32 Reserved;
    VNVME_CONTROLLER_INFO Controllers[16];
} VNVME_LIST_CONTROLLERS_OUTPUT, *PVNVME_LIST_CONTROLLERS_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_CREATE_NAMESPACE
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_CREATE_NAMESPACE_INPUT {
    UINT32 ControllerId;                // 控制器 ID
    VNVME_NAMESPACE_CONFIG Config;      // 命名空间配置
} VNVME_CREATE_NAMESPACE_INPUT, *PVNVME_CREATE_NAMESPACE_INPUT;

typedef struct _VNVME_CREATE_NAMESPACE_OUTPUT {
    UINT32 NSID;                        // 分配的 NSID
    UINT32 Reserved;
} VNVME_CREATE_NAMESPACE_OUTPUT, *PVNVME_CREATE_NAMESPACE_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_DELETE_NAMESPACE
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_DELETE_NAMESPACE_INPUT {
    UINT32 ControllerId;                // 控制器 ID
    UINT32 NSID;                        // 命名空间 ID
} VNVME_DELETE_NAMESPACE_INPUT, *PVNVME_DELETE_NAMESPACE_INPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_LIST_NAMESPACES
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_LIST_NAMESPACES_INPUT {
    UINT32 ControllerId;                // 控制器 ID
    UINT32 Reserved;
} VNVME_LIST_NAMESPACES_INPUT, *PVNVME_LIST_NAMESPACES_INPUT;

#define VNVME_MAX_NAMESPACES_PER_CONTROLLER  16

typedef struct _VNVME_NAMESPACE_INFO {
    UINT32 NSID;                        // 命名空间 ID
    UINT32 Flags;                       // 标志 (VNVME_NS_FLAG_*)
    UINT64 TotalBlocks;                 // 总块数
    UINT32 BlockSize;                   // 块大小
    UINT32 Reserved;
} VNVME_NAMESPACE_INFO, *PVNVME_NAMESPACE_INFO;

typedef struct _VNVME_LIST_NAMESPACES_OUTPUT {
    UINT32 Count;                       // 命名空间数量
    UINT32 Reserved;
    VNVME_NAMESPACE_INFO Namespaces[VNVME_MAX_NAMESPACES_PER_CONTROLLER];
} VNVME_LIST_NAMESPACES_OUTPUT, *PVNVME_LIST_NAMESPACES_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_GET_DEBUG_INFO
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_DEBUG_INFO_OUTPUT {
    UINT64 Uptime;                      // 运行时间 (毫秒)
    UINT64 AdminCommandsProcessed;      // Admin 命令数
    UINT64 IoCommandsProcessed;         // I/O 命令数
    UINT64 ReadBytes;                   // 读取字节数
    UINT64 WriteBytes;                  // 写入字节数
    UINT64 ErrorCount;                  // 错误数
    UINT32 CurrentPollingInterval;      // 当前轮询间隔 (微秒)
    UINT32 QueueDepth;                  // 当前队列深度
    UINT64 LastHeartbeat;               // 上次心跳时间
    UINT64 HeartbeatCount;              // 心跳次数
} VNVME_DEBUG_INFO_OUTPUT, *PVNVME_DEBUG_INFO_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_SET_DEBUG_LEVEL
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_SET_DEBUG_LEVEL_INPUT {
    UINT32 DebugLevel;                  // 调试级别 (0-5)
    UINT32 DebugFlags;                  // 调试标志
} VNVME_SET_DEBUG_LEVEL_INPUT, *PVNVME_SET_DEBUG_LEVEL_INPUT;

#define VNVME_DEBUG_FLAG_TRACE_IOCTL    0x0001
#define VNVME_DEBUG_FLAG_TRACE_CMD      0x0002
#define VNVME_DEBUG_FLAG_TRACE_DMA      0x0004
#define VNVME_DEBUG_FLAG_TRACE_QUEUE    0x0008
#define VNVME_DEBUG_FLAG_TRACE_ALL      0xFFFF

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_GET_STATS
 *---------------------------------------------------------------------------*/

// 命名空间统计
typedef struct _VNVME_NAMESPACE_STATS {
    UINT32 NSID;                        // 命名空间 ID
    UINT32 Active;                      // 是否激活
    UINT64 TotalBlocks;                 // 总块数
    UINT32 BlockSize;                   // 块大小
    UINT64 ReadCommands;                // 读命令数
    UINT64 WriteCommands;               // 写命令数
    UINT64 FlushCommands;               // Flush 命令数
    UINT64 ReadBytes;                   // 读取字节数
    UINT64 WriteBytes;                  // 写入字节数
} VNVME_NAMESPACE_STATS, *PVNVME_NAMESPACE_STATS;

// 控制器统计
typedef struct _VNVME_CONTROLLER_STATS {
    UINT32 ControllerId;                // 控制器 ID
    UINT32 NamespaceCount;              // 命名空间数量
    UINT64 AdminCommandsProcessed;      // Admin 命令数
    UINT64 IoCommandsProcessed;         // I/O 命令数
    UINT64 TotalReadBytes;              // 总读取字节数
    UINT64 TotalWriteBytes;             // 总写入字节数
    UINT32 IoQueueCount;                // I/O 队列数量
    UINT32 PollingIntervalUs;           // 当前轮询间隔 (微秒)
} VNVME_CONTROLLER_STATS, *PVNVME_CONTROLLER_STATS;

// 系统统计输入
typedef struct _VNVME_GET_STATS_INPUT {
    UINT32 ControllerId;                // 控制器 ID (0 = 所有控制器)
    UINT32 Flags;                       // 标志 (保留)
} VNVME_GET_STATS_INPUT, *PVNVME_GET_STATS_INPUT;

// 系统统计输出
#define VNVME_MAX_STATS_CONTROLLERS 8
#define VNVME_MAX_STATS_NAMESPACES  16

typedef struct _VNVME_GET_STATS_OUTPUT {
    UINT32 ControllerCount;             // 返回的控制器数
    UINT32 TotalNamespaceCount;         // 总命名空间数
    UINT64 Uptime;                      // 驱动运行时间 (毫秒)
    UINT64 TotalCommandsProcessed;      // 总命令数
    VNVME_CONTROLLER_STATS Controllers[VNVME_MAX_STATS_CONTROLLERS];
    VNVME_NAMESPACE_STATS Namespaces[VNVME_MAX_STATS_NAMESPACES];
} VNVME_GET_STATS_OUTPUT, *PVNVME_GET_STATS_OUTPUT;

/*---------------------------------------------------------------------------
 * IOCTL_VNVME_RESET_STATS
 *---------------------------------------------------------------------------*/

typedef struct _VNVME_RESET_STATS_INPUT {
    UINT32 ControllerId;                // 控制器 ID (0 = 所有控制器)
    UINT32 Flags;                       // 保留
} VNVME_RESET_STATS_INPUT, *PVNVME_RESET_STATS_INPUT;

#pragma pack(pop)

#endif /* _VNVME_IOCTL_H_ */
