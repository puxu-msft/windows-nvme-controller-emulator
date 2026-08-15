/**
 * @file debug.h
 * @brief VNVME 调试基础设施
 * 
 * 提供断言和性能统计功能。
 * 调试配置已迁移到 config.h，跟踪输出请使用 trace.h。
 */

#ifndef _VNVME_DEBUG_H_
#define _VNVME_DEBUG_H_

#include <ntddk.h>
#include "config.h"

//===========================================================================
// 调试模块标志 (位掩码，用于运行时过滤)
// 这些标志与 config.h 中的 DebugFlags 配合使用
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
// 便捷访问宏 (兼容旧代码)
// 这些现在是 g_Config 字段的别名
//===========================================================================

#define g_DebugLevel            (g_Config.DebugLevel)
#define g_DebugFlags            (g_Config.DebugFlags)
#define g_HeartbeatTimeout100ns (g_Config.HeartbeatTimeout100ns)

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
 * 初始化调试统计子系统
 * 注意: 配置加载已迁移到 VnvmeConfigInit()
 */
VOID
VnvmeDebugInit(VOID);

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

//===========================================================================
// 统计宏
//===========================================================================

extern VNVME_DEBUG_STATS g_DebugStats;

#define VNVME_STATS_ADMIN_SUBMIT()   InterlockedIncrement64(&g_DebugStats.AdminCmdsSubmitted)
#define VNVME_STATS_ADMIN_COMPLETE() InterlockedIncrement64(&g_DebugStats.AdminCmdsCompleted)
#define VNVME_STATS_ADMIN_ERROR()    InterlockedIncrement64(&g_DebugStats.AdminCmdsErrors)
#define VNVME_STATS_IO_SUBMIT()      InterlockedIncrement64(&g_DebugStats.IoCmdsSubmitted)
#define VNVME_STATS_IO_COMPLETE()    InterlockedIncrement64(&g_DebugStats.IoCmdsCompleted)
#define VNVME_STATS_IO_ERROR()       InterlockedIncrement64(&g_DebugStats.IoCmdsErrors)
#define VNVME_STATS_READ(bytes)      InterlockedAdd64(&g_DebugStats.BytesRead, (bytes))
#define VNVME_STATS_WRITE(bytes)     InterlockedAdd64(&g_DebugStats.BytesWritten, (bytes))
#define VNVME_STATS_DBELL_POLL()     InterlockedIncrement64(&g_DebugStats.DoorbellPolls)
#define VNVME_STATS_DBELL_HIT()      InterlockedIncrement64(&g_DebugStats.DoorbellHits)

#endif /* _VNVME_DEBUG_H_ */
