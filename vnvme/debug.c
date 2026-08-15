/**
 * @file debug.c
 * @brief VNVME 调试子系统实现
 * 
 * 提供性能统计功能。
 * 配置加载已迁移到 config.c。
 */

#include "debug.h"
#include "trace.h"

//===========================================================================
// 全局变量
//===========================================================================

// 性能统计
VNVME_DEBUG_STATS g_DebugStats = { 0 };

//===========================================================================
// 公开函数
//===========================================================================

/**
 * 初始化调试统计子系统
 */
VOID
VnvmeDebugInit(VOID)
{
    VnvmeDebugResetStats();
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:DEBUG] Statistics initialized\n");
}

/**
 * 获取当前调试统计
 */
_Use_decl_annotations_
VOID
VnvmeDebugGetStats(
    PVNVME_DEBUG_STATS Stats
    )
{
    // 复制统计数据 (原子读取各字段)
    Stats->AdminCmdsSubmitted = g_DebugStats.AdminCmdsSubmitted;
    Stats->AdminCmdsCompleted = g_DebugStats.AdminCmdsCompleted;
    Stats->AdminCmdsErrors = g_DebugStats.AdminCmdsErrors;
    Stats->IoCmdsSubmitted = g_DebugStats.IoCmdsSubmitted;
    Stats->IoCmdsCompleted = g_DebugStats.IoCmdsCompleted;
    Stats->IoCmdsErrors = g_DebugStats.IoCmdsErrors;
    Stats->BytesRead = g_DebugStats.BytesRead;
    Stats->BytesWritten = g_DebugStats.BytesWritten;
    Stats->DoorbellPolls = g_DebugStats.DoorbellPolls;
    Stats->DoorbellHits = g_DebugStats.DoorbellHits;
    Stats->TotalLatency = g_DebugStats.TotalLatency;
    Stats->MinLatency = g_DebugStats.MinLatency;
    Stats->MaxLatency = g_DebugStats.MaxLatency;
    Stats->LatencySamples = g_DebugStats.LatencySamples;
}

/**
 * 重置调试统计
 */
VOID
VnvmeDebugResetStats(VOID)
{
    RtlZeroMemory(&g_DebugStats, sizeof(g_DebugStats));
    
    // 初始化最小延迟为最大值
    g_DebugStats.MinLatency = MAXLONGLONG;
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:DEBUG] Statistics reset\n");
}

/**
 * 记录延迟样本 (内联辅助)
 */
VOID
VnvmeDebugRecordLatency(
    _In_ LONG64 LatencyIn100ns
    )
{
    LONG64 oldMin, oldMax;
    
    InterlockedAdd64(&g_DebugStats.TotalLatency, LatencyIn100ns);
    InterlockedIncrement64(&g_DebugStats.LatencySamples);
    
    // 更新最小值 (CAS loop)
    do {
        oldMin = g_DebugStats.MinLatency;
        if (LatencyIn100ns >= oldMin) break;
    } while (InterlockedCompareExchange64(&g_DebugStats.MinLatency, LatencyIn100ns, oldMin) != oldMin);
    
    // 更新最大值 (CAS loop)
    do {
        oldMax = g_DebugStats.MaxLatency;
        if (LatencyIn100ns <= oldMax) break;
    } while (InterlockedCompareExchange64(&g_DebugStats.MaxLatency, LatencyIn100ns, oldMax) != oldMax);
}

/**
 * 打印当前统计到调试输出
 */
VOID
VnvmeDebugPrintStats(VOID)
{
    VNVME_DEBUG_STATS stats;
    LONG64 avgLatency;
    ULONG hitRatePercent;
    
    VnvmeDebugGetStats(&stats);
    
    // 计算平均延迟
    if (stats.LatencySamples > 0) {
        avgLatency = stats.TotalLatency / stats.LatencySamples;
    } else {
        avgLatency = 0;
    }
    
    // 计算命中率 (整数百分比)
    hitRatePercent = stats.DoorbellPolls > 0 
        ? (ULONG)((stats.DoorbellHits * 100) / stats.DoorbellPolls) 
        : 0;
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] ====== Performance Statistics ======\n");
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] Admin Commands: Submit=%lld, Complete=%lld, Error=%lld\n",
               stats.AdminCmdsSubmitted, stats.AdminCmdsCompleted, stats.AdminCmdsErrors);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] I/O Commands:   Submit=%lld, Complete=%lld, Error=%lld\n",
               stats.IoCmdsSubmitted, stats.IoCmdsCompleted, stats.IoCmdsErrors);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] Bytes: Read=%lld MB, Written=%lld MB\n",
               stats.BytesRead / (1024 * 1024), stats.BytesWritten / (1024 * 1024));
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] Doorbell: Polls=%lld, Hits=%lld (%u%% hit rate)\n",
               stats.DoorbellPolls, stats.DoorbellHits, hitRatePercent);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] Latency (us): Min=%lld, Max=%lld, Avg=%lld\n",
               stats.MinLatency == MAXLONGLONG ? 0 : stats.MinLatency / 10,
               stats.MaxLatency / 10,
               avgLatency / 10);
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:STATS] ====================================\n");
}
