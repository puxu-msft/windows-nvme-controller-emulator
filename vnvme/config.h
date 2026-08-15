/**
 * @file config.h
 * @brief VNVME 驱动配置管理
 * 
 * 提供集中式配置管理，从注册表加载配置并提供运行时访问。
 * 所有可配置参数都集中在此模块中。
 * 
 * 依赖 vnvme_common.h 以确保配置限制与编译时常量一致。
 */

#ifndef _VNVME_CONFIG_H_
#define _VNVME_CONFIG_H_

#include <ntddk.h>
#include <vnvme_common.h>   // 编译时最大值常量

//===========================================================================
// 存储后端类型
//===========================================================================

typedef enum _VNVME_STORAGE_TYPE {
    VNVME_STORAGE_TYPE_NONE = 0,        // 未初始化
    VNVME_STORAGE_TYPE_MEMORY,          // 内存后端 (非分页池，易失性)
    VNVME_STORAGE_TYPE_FILE,            // 文件后端 (持久化)
    VNVME_STORAGE_TYPE_SPARSE           // 稀疏文件后端 (按需分配)
} VNVME_STORAGE_TYPE;

//===========================================================================
// 配置结构体
//===========================================================================

typedef struct _VNVME_CONFIG {
    //-----------------------------------------------------------------------
    // 调试配置 (可运行时修改)
    //-----------------------------------------------------------------------
    
    ULONG DebugLevel;               // 调试级别 (TRACE_LEVEL_*)
    ULONG DebugFlags;               // 调试模块标志 (位掩码)
    LONGLONG HeartbeatTimeout100ns; // 心跳超时 (100ns 单位)
    
    //-----------------------------------------------------------------------
    // 存储配置 (启动时固定)
    //-----------------------------------------------------------------------
    
    VNVME_STORAGE_TYPE StorageType; // 存储后端类型
    WCHAR StoragePath[260];         // 文件后端路径
    ULONG StorageSizeGB;            // 存储容量 (GB)
    
    //-----------------------------------------------------------------------
    // 队列配置 (启动时固定)
    //-----------------------------------------------------------------------
    
    ULONG MaxIOQueues;              // 最大 I/O 队列数
    ULONG AdminQueueDepth;          // Admin 队列深度
    ULONG IOQueueDepth;             // I/O 队列深度
    
    //-----------------------------------------------------------------------
    // 性能配置 (可运行时修改)
    //-----------------------------------------------------------------------
    
    ULONG DoorbellPollIntervalUs;   // Doorbell 轮询间隔 (微秒)
    ULONG BatchSize;                // 命令批处理大小
    
    //-----------------------------------------------------------------------
    // 安全配置 (启动时固定)
    //-----------------------------------------------------------------------
    
    BOOLEAN AllowUserModeAccess;    // 允许用户态访问控制设备
    BOOLEAN RequireAdminPrivilege;  // 要求管理员权限
    
} VNVME_CONFIG, *PVNVME_CONFIG;

//===========================================================================
// 全局配置实例 (在 config.c 中定义)
//===========================================================================

extern VNVME_CONFIG g_Config;

//===========================================================================
// 便捷访问宏
//===========================================================================

// 调试配置
#define CONFIG_DEBUG_LEVEL          (g_Config.DebugLevel)
#define CONFIG_DEBUG_FLAGS          (g_Config.DebugFlags)
#define CONFIG_HEARTBEAT_TIMEOUT    (g_Config.HeartbeatTimeout100ns)

// 存储配置
#define CONFIG_STORAGE_TYPE         (g_Config.StorageType)
#define CONFIG_STORAGE_PATH         (g_Config.StoragePath)
#define CONFIG_STORAGE_SIZE_GB      (g_Config.StorageSizeGB)

// 队列配置
#define CONFIG_MAX_IO_QUEUES        (g_Config.MaxIOQueues)
#define CONFIG_ADMIN_QUEUE_DEPTH    (g_Config.AdminQueueDepth)
#define CONFIG_IO_QUEUE_DEPTH       (g_Config.IOQueueDepth)

// 性能配置
#define CONFIG_POLL_INTERVAL_US     (g_Config.DoorbellPollIntervalUs)
#define CONFIG_BATCH_SIZE           (g_Config.BatchSize)

//===========================================================================
// 默认值常量
//
// 注意: 默认值必须在 vnvme_common.h 编译时常量的范围内。
//===========================================================================

#define VNVME_DEFAULT_DEBUG_LEVEL           4   // TRACE_LEVEL_INFORMATION
#define VNVME_DEFAULT_DEBUG_FLAGS           0xFFFFFFFF
#define VNVME_DEFAULT_HEARTBEAT_MS          10000   // 10 秒
#define VNVME_DEFAULT_STORAGE_TYPE          VNVME_STORAGE_TYPE_MEMORY
#define VNVME_DEFAULT_STORAGE_SIZE_GB       1
// 默认使用编译时最大值 (来自 vnvme_common.h)
#define VNVME_DEFAULT_MAX_IO_QUEUES         VNVME_MAX_IO_QUEUES
#define VNVME_DEFAULT_ADMIN_QUEUE_DEPTH     VNVME_ADMIN_QUEUE_DEPTH
#define VNVME_DEFAULT_IO_QUEUE_DEPTH        VNVME_IO_QUEUE_DEPTH
#define VNVME_DEFAULT_POLL_INTERVAL_US      100
#define VNVME_DEFAULT_BATCH_SIZE            32

//===========================================================================
// 配置限制常量
//
// 注意: 队列相关的最大值不能超过 vnvme_common.h 中定义的编译时常量，
// 因为那些常量决定了数组大小和共享内存布局。
//===========================================================================

#define VNVME_MIN_HEARTBEAT_MS              1000    // 1 秒
#define VNVME_MAX_HEARTBEAT_MS              60000   // 60 秒
#define VNVME_MIN_STORAGE_SIZE_GB           1
#define VNVME_MAX_STORAGE_SIZE_GB           1024

// 队列配置限制 - 上限必须 <= vnvme_common.h 中的 VNVME_MAX_IO_QUEUES
#define VNVME_CFG_MIN_IO_QUEUES             1
#define VNVME_CFG_MAX_IO_QUEUES             VNVME_MAX_IO_QUEUES  // 来自 vnvme_common.h

// 队列深度限制 - 上限不超过 vnvme_common.h 中的编译时常量
#define VNVME_MIN_QUEUE_DEPTH               16
#define VNVME_CFG_MAX_ADMIN_QUEUE_DEPTH     VNVME_ADMIN_QUEUE_DEPTH
#define VNVME_CFG_MAX_IO_QUEUE_DEPTH        VNVME_IO_QUEUE_DEPTH

#define VNVME_MIN_POLL_INTERVAL_US          1
#define VNVME_MAX_POLL_INTERVAL_US          10000
#define VNVME_MIN_BATCH_SIZE                1
#define VNVME_MAX_BATCH_SIZE                256

//===========================================================================
// 函数声明
//===========================================================================

/**
 * 初始化配置子系统
 * 
 * 从注册表 Parameters 子键加载配置，未配置的项使用默认值。
 * 应在 DriverEntry 早期调用。
 * 
 * @param RegistryPath 驱动注册表路径
 * @return STATUS_SUCCESS 或错误码
 */
NTSTATUS
VnvmeConfigInit(
    _In_ PUNICODE_STRING RegistryPath
    );

/**
 * 清理配置子系统
 * 
 * 释放配置相关资源 (如动态分配的字符串)。
 * 应在驱动卸载时调用。
 */
VOID
VnvmeConfigCleanup(VOID);

/**
 * 获取当前配置的只读副本
 * 
 * @param Config 输出配置结构
 */
VOID
VnvmeConfigGet(
    _Out_ PVNVME_CONFIG Config
    );

/**
 * 更新可动态修改的配置
 * 
 * 仅更新标记为可动态修改的字段:
 * - DebugLevel, DebugFlags, HeartbeatTimeout
 * - DoorbellPollIntervalUs, BatchSize
 * 
 * @param Config 新配置 (仅读取可修改字段)
 * @return STATUS_SUCCESS 或 STATUS_INVALID_PARAMETER
 */
NTSTATUS
VnvmeConfigUpdate(
    _In_ PVNVME_CONFIG Config
    );

#endif /* _VNVME_CONFIG_H_ */
