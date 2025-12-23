/**
 * @file debug.h
 * @brief VNVME 调试基础设施
 * 
 * 提供多级别、模块化的调试输出系统。
 * 支持 DbgPrint 和 WPP 双模式。
 * 
 * 使用示例:
 *   VNVME_DBG_ERROR("Failed to allocate memory: %!STATUS!", status);
 *   VNVME_DBG_INFO("Controller %u created", controllerId);
 *   VNVME_FUNC_ENTER();
 *   VNVME_FUNC_EXIT_STATUS(status);
 */

#ifndef _VNVME_DEBUG_H_
#define _VNVME_DEBUG_H_

#include <ntddk.h>

//===========================================================================
// 调试级别定义
//===========================================================================

#define VNVME_DBG_LEVEL_NONE        0   // 禁用所有输出
#define VNVME_DBG_LEVEL_ERROR       1   // 仅错误
#define VNVME_DBG_LEVEL_WARNING     2   // 错误 + 警告
#define VNVME_DBG_LEVEL_INFO        3   // 错误 + 警告 + 信息
#define VNVME_DBG_LEVEL_DEBUG       4   // 上述 + 调试详情
#define VNVME_DBG_LEVEL_VERBOSE     5   // 全部输出 (性能影响大)

//===========================================================================
// 调试模块标志 (位掩码)
//===========================================================================

#define VNVME_DBG_FLAG_DRIVER       0x00000001  // 驱动入口/卸载
#define VNVME_DBG_FLAG_PNP          0x00000002  // PnP 和电源管理
#define VNVME_DBG_FLAG_IOCTL        0x00000004  // IOCTL 处理
#define VNVME_DBG_FLAG_BUS          0x00000008  // 总线/PDO 管理
#define VNVME_DBG_FLAG_BAR0         0x00000010  // BAR0 寄存器
#define VNVME_DBG_FLAG_PCIE         0x00000020  // PCIe 配置空间
#define VNVME_DBG_FLAG_DOORBELL     0x00000040  // Doorbell 轮询
#define VNVME_DBG_FLAG_QUEUE        0x00000080  // 队列管理
#define VNVME_DBG_FLAG_ADMIN        0x00000100  // Admin 命令
#define VNVME_DBG_FLAG_IO           0x00000200  // I/O 命令
#define VNVME_DBG_FLAG_PRP          0x00000400  // PRP 解析
#define VNVME_DBG_FLAG_STORAGE      0x00000800  // 存储后端
#define VNVME_DBG_FLAG_SHM          0x00001000  // 共享内存
#define VNVME_DBG_FLAG_USER         0x00002000  // 用户态通信
#define VNVME_DBG_FLAG_PERF         0x00004000  // 性能统计

#define VNVME_DBG_FLAG_ALL          0xFFFFFFFF  // 所有模块

//===========================================================================
// 全局调试变量 (在 debug.c 中定义)
//===========================================================================

extern ULONG g_VnvmeDebugLevel;     // 当前调试级别
extern ULONG g_VnvmeDebugFlags;     // 当前调试模块标志

//===========================================================================
// 调试输出宏
//===========================================================================

/**
 * 核心调试输出宏
 * @param level 调试级别 (VNVME_DBG_LEVEL_xxx)
 * @param flag  模块标志 (VNVME_DBG_FLAG_xxx)
 * @param fmt   格式字符串
 * @param ...   格式参数
 */
#if DBG

#define VNVME_DBG_PRINT(level, flag, fmt, ...) \
    do { \
        if ((g_VnvmeDebugLevel >= (level)) && (g_VnvmeDebugFlags & (flag))) { \
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, \
                       "[VNVME:%s:%d] " fmt "\n", \
                       __FUNCTION__, __LINE__, ##__VA_ARGS__); \
        } \
    } while(0)

#else

#define VNVME_DBG_PRINT(level, flag, fmt, ...) ((void)0)

#endif

//===========================================================================
// 级别快捷宏
//===========================================================================

// 错误 - 始终重要，影响功能
#define VNVME_DBG_ERROR(fmt, ...) \
    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_ERROR, VNVME_DBG_FLAG_ALL, "ERROR: " fmt, ##__VA_ARGS__)

// 警告 - 可能的问题，但可恢复
#define VNVME_DBG_WARN(fmt, ...) \
    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_WARNING, VNVME_DBG_FLAG_ALL, "WARN: " fmt, ##__VA_ARGS__)

// 信息 - 重要的状态变化
#define VNVME_DBG_INFO(fmt, ...) \
    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_INFO, VNVME_DBG_FLAG_ALL, "INFO: " fmt, ##__VA_ARGS__)

//===========================================================================
// 模块化调试宏
//===========================================================================

// 驱动入口
#define VNVME_DBG_DRV(fmt, ...)     VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_DRIVER, fmt, ##__VA_ARGS__)
// PnP/Power
#define VNVME_DBG_PNP(fmt, ...)     VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_PNP, fmt, ##__VA_ARGS__)
// IOCTL
#define VNVME_DBG_IOCTL(fmt, ...)   VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_IOCTL, fmt, ##__VA_ARGS__)
// 总线/PDO
#define VNVME_DBG_BUS(fmt, ...)     VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_BUS, fmt, ##__VA_ARGS__)
// BAR0
#define VNVME_DBG_BAR0(fmt, ...)    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_BAR0, fmt, ##__VA_ARGS__)
// PCIe
#define VNVME_DBG_PCIE(fmt, ...)    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_PCIE, fmt, ##__VA_ARGS__)
// Doorbell
#define VNVME_DBG_DBELL(fmt, ...)   VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_DOORBELL, fmt, ##__VA_ARGS__)
// 队列
#define VNVME_DBG_QUEUE(fmt, ...)   VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_QUEUE, fmt, ##__VA_ARGS__)
// Admin 命令
#define VNVME_DBG_ADMIN(fmt, ...)   VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_ADMIN, fmt, ##__VA_ARGS__)
// I/O 命令
#define VNVME_DBG_IO(fmt, ...)      VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_IO, fmt, ##__VA_ARGS__)
// PRP
#define VNVME_DBG_PRP(fmt, ...)     VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_PRP, fmt, ##__VA_ARGS__)
// 存储
#define VNVME_DBG_STOR(fmt, ...)    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_STORAGE, fmt, ##__VA_ARGS__)
// 共享内存
#define VNVME_DBG_SHM(fmt, ...)     VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_SHM, fmt, ##__VA_ARGS__)
// 用户态通信
#define VNVME_DBG_USER(fmt, ...)    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_USER, fmt, ##__VA_ARGS__)

//===========================================================================
// Verbose 级别宏 (高频调用，仅在 VERBOSE 级别启用)
//===========================================================================

#define VNVME_DBG_DBELL_V(fmt, ...) VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_DOORBELL, fmt, ##__VA_ARGS__)
#define VNVME_DBG_QUEUE_V(fmt, ...) VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_QUEUE, fmt, ##__VA_ARGS__)
#define VNVME_DBG_IO_V(fmt, ...)    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_IO, fmt, ##__VA_ARGS__)
#define VNVME_DBG_PRP_V(fmt, ...)   VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_PRP, fmt, ##__VA_ARGS__)

//===========================================================================
// 函数跟踪宏
//===========================================================================

#if DBG

#define VNVME_FUNC_ENTER() \
    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_ALL, ">>> %s", __FUNCTION__)

#define VNVME_FUNC_EXIT() \
    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_ALL, "<<< %s", __FUNCTION__)

#define VNVME_FUNC_EXIT_STATUS(status) \
    VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_ALL, "<<< %s (0x%08X)", __FUNCTION__, (status))

#define VNVME_FUNC_EXIT_NTSTATUS(status) \
    do { \
        NTSTATUS _s = (status); \
        if (NT_SUCCESS(_s)) { \
            VNVME_DBG_PRINT(VNVME_DBG_LEVEL_VERBOSE, VNVME_DBG_FLAG_ALL, "<<< %s SUCCESS", __FUNCTION__); \
        } else { \
            VNVME_DBG_PRINT(VNVME_DBG_LEVEL_DEBUG, VNVME_DBG_FLAG_ALL, "<<< %s FAILED (0x%08X)", __FUNCTION__, _s); \
        } \
    } while(0)

#else

#define VNVME_FUNC_ENTER()              ((void)0)
#define VNVME_FUNC_EXIT()               ((void)0)
#define VNVME_FUNC_EXIT_STATUS(s)       ((void)0)
#define VNVME_FUNC_EXIT_NTSTATUS(s)     ((void)0)

#endif

//===========================================================================
// 断言和验证
//===========================================================================

#if DBG

#define VNVME_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, \
                       "[VNVME:ASSERT] %s:%d: %s\n", __FILE__, __LINE__, #expr); \
            DbgBreakPoint(); \
        } \
    } while(0)

#define VNVME_VERIFY(expr) VNVME_ASSERT(expr)

#else

#define VNVME_ASSERT(expr)      ((void)0)
#define VNVME_VERIFY(expr)      (expr)

#endif

//===========================================================================
// 性能统计结构
//===========================================================================

typedef struct _VNVME_DEBUG_STATS {
    // 命令统计
    volatile LONG64 AdminCmdsSubmitted;
    volatile LONG64 AdminCmdsCompleted;
    volatile LONG64 AdminCmdsErrors;
    volatile LONG64 IoCmdsSubmitted;
    volatile LONG64 IoCmdsCompleted;
    volatile LONG64 IoCmdsErrors;
    
    // I/O 统计
    volatile LONG64 BytesRead;
    volatile LONG64 BytesWritten;
    
    // Doorbell 统计
    volatile LONG64 DoorbellPolls;
    volatile LONG64 DoorbellHits;           // 有命令需要处理的轮询次数
    
    // 延迟统计 (以 100ns 为单位)
    volatile LONG64 TotalLatency;           // 累计延迟
    volatile LONG64 MinLatency;             // 最小延迟
    volatile LONG64 MaxLatency;             // 最大延迟
    volatile LONG64 LatencySamples;         // 样本数
} VNVME_DEBUG_STATS, *PVNVME_DEBUG_STATS;

//===========================================================================
// 调试子系统函数
//===========================================================================

/**
 * 初始化调试子系统
 * 从注册表读取调试级别和标志
 * 
 * @param RegistryPath 驱动注册表路径
 */
VOID
VnvmeDebugInit(
    _In_ PUNICODE_STRING RegistryPath
    );

/**
 * 设置调试级别
 * 
 * @param Level 新的调试级别
 */
VOID
VnvmeDebugSetLevel(
    _In_ ULONG Level
    );

/**
 * 设置调试标志
 * 
 * @param Flags 新的调试标志
 */
VOID
VnvmeDebugSetFlags(
    _In_ ULONG Flags
    );

/**
 * 获取当前调试统计
 * 
 * @param Stats 输出统计结构
 */
VOID
VnvmeDebugGetStats(
    _Out_ PVNVME_DEBUG_STATS Stats
    );

/**
 * 重置调试统计
 */
VOID
VnvmeDebugResetStats(VOID);

/**
 * 增加命令提交计数
 */
#define VNVME_STATS_ADMIN_SUBMIT()  InterlockedIncrement64(&g_VnvmeDebugStats.AdminCmdsSubmitted)
#define VNVME_STATS_ADMIN_COMPLETE() InterlockedIncrement64(&g_VnvmeDebugStats.AdminCmdsCompleted)
#define VNVME_STATS_ADMIN_ERROR()   InterlockedIncrement64(&g_VnvmeDebugStats.AdminCmdsErrors)
#define VNVME_STATS_IO_SUBMIT()     InterlockedIncrement64(&g_VnvmeDebugStats.IoCmdsSubmitted)
#define VNVME_STATS_IO_COMPLETE()   InterlockedIncrement64(&g_VnvmeDebugStats.IoCmdsCompleted)
#define VNVME_STATS_IO_ERROR()      InterlockedIncrement64(&g_VnvmeDebugStats.IoCmdsErrors)
#define VNVME_STATS_READ(bytes)     InterlockedAdd64(&g_VnvmeDebugStats.BytesRead, (bytes))
#define VNVME_STATS_WRITE(bytes)    InterlockedAdd64(&g_VnvmeDebugStats.BytesWritten, (bytes))
#define VNVME_STATS_DBELL_POLL()    InterlockedIncrement64(&g_VnvmeDebugStats.DoorbellPolls)
#define VNVME_STATS_DBELL_HIT()     InterlockedIncrement64(&g_VnvmeDebugStats.DoorbellHits)

extern VNVME_DEBUG_STATS g_VnvmeDebugStats;

#endif /* _VNVME_DEBUG_H_ */
