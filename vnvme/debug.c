/**
 * @file debug.c
 * @brief VNVME 调试子系统实现
 * 
 * 提供调试初始化、运行时配置和性能统计功能。
 */

#include "debug.h"
#include <ntstrsafe.h>

//===========================================================================
// 全局变量
//===========================================================================

// 调试级别 - 默认 INFO 级别
ULONG g_VnvmeDebugLevel = VNVME_DBG_LEVEL_INFO;

// 调试标志 - 默认启用所有模块
ULONG g_VnvmeDebugFlags = VNVME_DBG_FLAG_ALL;

// 性能统计
VNVME_DEBUG_STATS g_VnvmeDebugStats = { 0 };

//===========================================================================
// 注册表参数名称
//===========================================================================

static const WCHAR VNVME_REG_DEBUG_LEVEL[] = L"DebugLevel";
static const WCHAR VNVME_REG_DEBUG_FLAGS[] = L"DebugFlags";

//===========================================================================
// 内部函数
//===========================================================================

/**
 * 从注册表读取 DWORD 值
 */
static
NTSTATUS
VnvmeDebugReadRegDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _Out_ PULONG Value
    )
{
    NTSTATUS status;
    UNICODE_STRING valueName;
    UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION valueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    ULONG resultLength;
    
    RtlInitUnicodeString(&valueName, ValueName);
    
    status = ZwQueryValueKey(
        KeyHandle,
        &valueName,
        KeyValuePartialInformation,
        valueInfo,
        sizeof(buffer),
        &resultLength
        );
    
    if (NT_SUCCESS(status)) {
        if (valueInfo->Type == REG_DWORD && valueInfo->DataLength == sizeof(ULONG)) {
            *Value = *(PULONG)valueInfo->Data;
        } else {
            status = STATUS_INVALID_PARAMETER;
        }
    }
    
    return status;
}

//===========================================================================
// 公开函数
//===========================================================================

/**
 * 初始化调试子系统
 * 从注册表读取调试级别和标志
 */
_Use_decl_annotations_
VOID
VnvmeDebugInit(
    PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objectAttributes;
    HANDLE keyHandle = NULL;
    UNICODE_STRING parametersPath;
    WCHAR parametersBuffer[256];
    ULONG value;
    
    // 构建 Parameters 子键路径
    status = RtlStringCbPrintfW(
        parametersBuffer,
        sizeof(parametersBuffer),
        L"%wZ\\Parameters",
        RegistryPath
        );
    
    if (!NT_SUCCESS(status)) {
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[VNVME:DEBUG] Failed to build parameters path\n");
        return;
    }
    
    RtlInitUnicodeString(&parametersPath, parametersBuffer);
    
    InitializeObjectAttributes(
        &objectAttributes,
        &parametersPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
        );
    
    status = ZwOpenKey(&keyHandle, KEY_READ, &objectAttributes);
    
    if (!NT_SUCCESS(status)) {
        // 注册表项不存在，使用默认值
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
                   "[VNVME:DEBUG] Parameters key not found, using defaults (Level=%u, Flags=0x%08X)\n",
                   g_VnvmeDebugLevel, g_VnvmeDebugFlags);
        return;
    }
    
    // 读取 DebugLevel
    status = VnvmeDebugReadRegDword(keyHandle, VNVME_REG_DEBUG_LEVEL, &value);
    if (NT_SUCCESS(status)) {
        g_VnvmeDebugLevel = value;
    }
    
    // 读取 DebugFlags
    status = VnvmeDebugReadRegDword(keyHandle, VNVME_REG_DEBUG_FLAGS, &value);
    if (NT_SUCCESS(status)) {
        g_VnvmeDebugFlags = value;
    }
    
    ZwClose(keyHandle);
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:DEBUG] Initialized: Level=%u, Flags=0x%08X\n",
               g_VnvmeDebugLevel, g_VnvmeDebugFlags);
    
    // 重置统计
    VnvmeDebugResetStats();
}

/**
 * 设置调试级别
 */
_Use_decl_annotations_
VOID
VnvmeDebugSetLevel(
    ULONG Level
    )
{
    ULONG oldLevel = g_VnvmeDebugLevel;
    g_VnvmeDebugLevel = Level;
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:DEBUG] Level changed: %u -> %u\n",
               oldLevel, Level);
}

/**
 * 设置调试标志
 */
_Use_decl_annotations_
VOID
VnvmeDebugSetFlags(
    ULONG Flags
    )
{
    ULONG oldFlags = g_VnvmeDebugFlags;
    g_VnvmeDebugFlags = Flags;
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:DEBUG] Flags changed: 0x%08X -> 0x%08X\n",
               oldFlags, Flags);
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
    Stats->AdminCmdsSubmitted = g_VnvmeDebugStats.AdminCmdsSubmitted;
    Stats->AdminCmdsCompleted = g_VnvmeDebugStats.AdminCmdsCompleted;
    Stats->AdminCmdsErrors = g_VnvmeDebugStats.AdminCmdsErrors;
    Stats->IoCmdsSubmitted = g_VnvmeDebugStats.IoCmdsSubmitted;
    Stats->IoCmdsCompleted = g_VnvmeDebugStats.IoCmdsCompleted;
    Stats->IoCmdsErrors = g_VnvmeDebugStats.IoCmdsErrors;
    Stats->BytesRead = g_VnvmeDebugStats.BytesRead;
    Stats->BytesWritten = g_VnvmeDebugStats.BytesWritten;
    Stats->DoorbellPolls = g_VnvmeDebugStats.DoorbellPolls;
    Stats->DoorbellHits = g_VnvmeDebugStats.DoorbellHits;
    Stats->TotalLatency = g_VnvmeDebugStats.TotalLatency;
    Stats->MinLatency = g_VnvmeDebugStats.MinLatency;
    Stats->MaxLatency = g_VnvmeDebugStats.MaxLatency;
    Stats->LatencySamples = g_VnvmeDebugStats.LatencySamples;
}

/**
 * 重置调试统计
 */
VOID
VnvmeDebugResetStats(VOID)
{
    RtlZeroMemory(&g_VnvmeDebugStats, sizeof(g_VnvmeDebugStats));
    
    // 初始化最小延迟为最大值
    g_VnvmeDebugStats.MinLatency = MAXLONGLONG;
    
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
    
    InterlockedAdd64(&g_VnvmeDebugStats.TotalLatency, LatencyIn100ns);
    InterlockedIncrement64(&g_VnvmeDebugStats.LatencySamples);
    
    // 更新最小值 (CAS loop)
    do {
        oldMin = g_VnvmeDebugStats.MinLatency;
        if (LatencyIn100ns >= oldMin) break;
    } while (InterlockedCompareExchange64(&g_VnvmeDebugStats.MinLatency, LatencyIn100ns, oldMin) != oldMin);
    
    // 更新最大值 (CAS loop)
    do {
        oldMax = g_VnvmeDebugStats.MaxLatency;
        if (LatencyIn100ns <= oldMax) break;
    } while (InterlockedCompareExchange64(&g_VnvmeDebugStats.MaxLatency, LatencyIn100ns, oldMax) != oldMax);
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
