/**
 * @file config.c
 * @brief VNVME 驱动配置管理实现
 * 
 * 从注册表加载配置，提供默认值和验证。
 */

#include "config.h"
#include "trace.h"
#include <ntstrsafe.h>

//===========================================================================
// 全局配置实例
//===========================================================================

VNVME_CONFIG g_Config = {
    // 调试配置
    .DebugLevel = VNVME_DEFAULT_DEBUG_LEVEL,
    .DebugFlags = VNVME_DEFAULT_DEBUG_FLAGS,
    .HeartbeatTimeout100ns = (LONGLONG)VNVME_DEFAULT_HEARTBEAT_MS * 10000LL,
    
    // 存储配置
    .StorageType = VNVME_DEFAULT_STORAGE_TYPE,
    .StoragePath = { 0 },
    .StorageSizeGB = VNVME_DEFAULT_STORAGE_SIZE_GB,
    
    // 队列配置
    .MaxIOQueues = VNVME_DEFAULT_MAX_IO_QUEUES,
    .AdminQueueDepth = VNVME_DEFAULT_ADMIN_QUEUE_DEPTH,
    .IOQueueDepth = VNVME_DEFAULT_IO_QUEUE_DEPTH,
    
    // 性能配置
    .DoorbellPollIntervalUs = VNVME_DEFAULT_POLL_INTERVAL_US,
    .BatchSize = VNVME_DEFAULT_BATCH_SIZE,
    
    // 安全配置
    .AllowUserModeAccess = TRUE,
    .RequireAdminPrivilege = TRUE
};

//===========================================================================
// 注册表键名
//===========================================================================

static const WCHAR REG_DEBUG_LEVEL[] = L"DebugLevel";
static const WCHAR REG_DEBUG_FLAGS[] = L"DebugFlags";
static const WCHAR REG_HEARTBEAT_TIMEOUT[] = L"HeartbeatTimeoutMs";
static const WCHAR REG_STORAGE_TYPE[] = L"StorageType";
static const WCHAR REG_STORAGE_PATH[] = L"StoragePath";
static const WCHAR REG_STORAGE_SIZE[] = L"StorageSizeGB";
static const WCHAR REG_MAX_IO_QUEUES[] = L"MaxIOQueues";
static const WCHAR REG_ADMIN_QUEUE_DEPTH[] = L"AdminQueueDepth";
static const WCHAR REG_IO_QUEUE_DEPTH[] = L"IOQueueDepth";
static const WCHAR REG_POLL_INTERVAL[] = L"DoorbellPollIntervalUs";
static const WCHAR REG_BATCH_SIZE[] = L"BatchSize";

//===========================================================================
// 内部函数
//===========================================================================

/**
 * 从注册表读取 DWORD 值
 */
static
NTSTATUS
ConfigReadDword(
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

/**
 * 从注册表读取字符串值
 */
static
NTSTATUS
ConfigReadString(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _Out_writes_(BufferLength) PWCHAR Buffer,
    _In_ ULONG BufferLength
    )
{
    NTSTATUS status;
    UNICODE_STRING valueName;
    UCHAR infoBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 520];
    PKEY_VALUE_PARTIAL_INFORMATION valueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)infoBuffer;
    ULONG resultLength;
    
    RtlInitUnicodeString(&valueName, ValueName);
    
    status = ZwQueryValueKey(
        KeyHandle,
        &valueName,
        KeyValuePartialInformation,
        valueInfo,
        sizeof(infoBuffer),
        &resultLength
        );
    
    if (NT_SUCCESS(status)) {
        if (valueInfo->Type == REG_SZ || valueInfo->Type == REG_EXPAND_SZ) {
            ULONG copyLen = min(valueInfo->DataLength, (BufferLength - 1) * sizeof(WCHAR));
            RtlCopyMemory(Buffer, valueInfo->Data, copyLen);
            Buffer[copyLen / sizeof(WCHAR)] = L'\0';
        } else {
            status = STATUS_INVALID_PARAMETER;
        }
    }
    
    return status;
}

/**
 * 读取 DWORD 并限制范围
 */
static
VOID
ConfigReadDwordClamped(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR ValueName,
    _Inout_ PULONG Value,
    _In_ ULONG MinValue,
    _In_ ULONG MaxValue
    )
{
    ULONG regValue;
    NTSTATUS status = ConfigReadDword(KeyHandle, ValueName, &regValue);
    
    if (NT_SUCCESS(status)) {
        if (regValue < MinValue) {
            regValue = MinValue;
        } else if (regValue > MaxValue) {
            regValue = MaxValue;
        }
        *Value = regValue;
    }
}

//===========================================================================
// 公开函数
//===========================================================================

_Use_decl_annotations_
NTSTATUS
VnvmeConfigInit(
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
        DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL,
                   "[VNVME:CONFIG] Failed to build parameters path, using defaults\n");
        return STATUS_SUCCESS;  // 使用默认值继续
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
                   "[VNVME:CONFIG] Parameters key not found, using defaults\n");
        return STATUS_SUCCESS;
    }
    
    //-----------------------------------------------------------------------
    // 读取调试配置
    //-----------------------------------------------------------------------
    
    if (NT_SUCCESS(ConfigReadDword(keyHandle, REG_DEBUG_LEVEL, &value))) {
        g_Config.DebugLevel = value;
    }
    
    if (NT_SUCCESS(ConfigReadDword(keyHandle, REG_DEBUG_FLAGS, &value))) {
        g_Config.DebugFlags = value;
    }
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_HEARTBEAT_TIMEOUT,
        &value,
        VNVME_MIN_HEARTBEAT_MS,
        VNVME_MAX_HEARTBEAT_MS
        );
    // 注意: HeartbeatTimeout 注册表中是毫秒，内部存储为 100ns
    if (NT_SUCCESS(ConfigReadDword(keyHandle, REG_HEARTBEAT_TIMEOUT, &value))) {
        if (value < VNVME_MIN_HEARTBEAT_MS) value = VNVME_MIN_HEARTBEAT_MS;
        if (value > VNVME_MAX_HEARTBEAT_MS) value = VNVME_MAX_HEARTBEAT_MS;
        g_Config.HeartbeatTimeout100ns = (LONGLONG)value * 10000LL;
    }
    
    //-----------------------------------------------------------------------
    // 读取存储配置
    //-----------------------------------------------------------------------
    
    if (NT_SUCCESS(ConfigReadDword(keyHandle, REG_STORAGE_TYPE, &value))) {
        if (value <= VNVME_STORAGE_TYPE_SPARSE) {
            g_Config.StorageType = (VNVME_STORAGE_TYPE)value;
        }
    }
    
    ConfigReadString(
        keyHandle,
        REG_STORAGE_PATH,
        g_Config.StoragePath,
        sizeof(g_Config.StoragePath) / sizeof(WCHAR)
        );
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_STORAGE_SIZE,
        &g_Config.StorageSizeGB,
        VNVME_MIN_STORAGE_SIZE_GB,
        VNVME_MAX_STORAGE_SIZE_GB
        );
    
    //-----------------------------------------------------------------------
    // 读取队列配置
    //-----------------------------------------------------------------------
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_MAX_IO_QUEUES,
        &g_Config.MaxIOQueues,
        VNVME_CFG_MIN_IO_QUEUES,
        VNVME_CFG_MAX_IO_QUEUES
        );
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_ADMIN_QUEUE_DEPTH,
        &g_Config.AdminQueueDepth,
        VNVME_MIN_QUEUE_DEPTH,
        VNVME_CFG_MAX_ADMIN_QUEUE_DEPTH
        );
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_IO_QUEUE_DEPTH,
        &g_Config.IOQueueDepth,
        VNVME_MIN_QUEUE_DEPTH,
        VNVME_CFG_MAX_IO_QUEUE_DEPTH
        );
    
    //-----------------------------------------------------------------------
    // 读取性能配置
    //-----------------------------------------------------------------------
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_POLL_INTERVAL,
        &g_Config.DoorbellPollIntervalUs,
        VNVME_MIN_POLL_INTERVAL_US,
        VNVME_MAX_POLL_INTERVAL_US
        );
    
    ConfigReadDwordClamped(
        keyHandle,
        REG_BATCH_SIZE,
        &g_Config.BatchSize,
        VNVME_MIN_BATCH_SIZE,
        VNVME_MAX_BATCH_SIZE
        );
    
    ZwClose(keyHandle);
    
    //-----------------------------------------------------------------------
    // 输出最终配置
    //-----------------------------------------------------------------------
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:CONFIG] Loaded configuration:\n"
               "  DebugLevel=%u, DebugFlags=0x%08X\n"
               "  HeartbeatTimeout=%lldms\n"
               "  StorageType=%d, StorageSizeGB=%u\n"
               "  MaxIOQueues=%u, AdminQueueDepth=%u, IOQueueDepth=%u\n"
               "  PollInterval=%uus, BatchSize=%u\n",
               g_Config.DebugLevel, g_Config.DebugFlags,
               g_Config.HeartbeatTimeout100ns / 10000LL,
               g_Config.StorageType, g_Config.StorageSizeGB,
               g_Config.MaxIOQueues, g_Config.AdminQueueDepth, g_Config.IOQueueDepth,
               g_Config.DoorbellPollIntervalUs, g_Config.BatchSize);
    
    return STATUS_SUCCESS;
}

_Use_decl_annotations_
VOID
VnvmeConfigCleanup(VOID)
{
    // 当前没有需要清理的动态资源
    // 预留给未来可能的动态分配
}

_Use_decl_annotations_
VOID
VnvmeConfigGet(
    PVNVME_CONFIG Config
    )
{
    if (Config != NULL) {
        RtlCopyMemory(Config, &g_Config, sizeof(VNVME_CONFIG));
    }
}

_Use_decl_annotations_
NTSTATUS
VnvmeConfigUpdate(
    PVNVME_CONFIG Config
    )
{
    if (Config == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 仅更新可动态修改的配置
    
    // 调试配置 - 无需验证
    g_Config.DebugLevel = Config->DebugLevel;
    g_Config.DebugFlags = Config->DebugFlags;
    
    // 心跳超时 - 范围检查
    if (Config->HeartbeatTimeout100ns >= (LONGLONG)VNVME_MIN_HEARTBEAT_MS * 10000LL &&
        Config->HeartbeatTimeout100ns <= (LONGLONG)VNVME_MAX_HEARTBEAT_MS * 10000LL) {
        g_Config.HeartbeatTimeout100ns = Config->HeartbeatTimeout100ns;
    }
    
    // 轮询间隔 - 范围检查
    if (Config->DoorbellPollIntervalUs >= VNVME_MIN_POLL_INTERVAL_US &&
        Config->DoorbellPollIntervalUs <= VNVME_MAX_POLL_INTERVAL_US) {
        g_Config.DoorbellPollIntervalUs = Config->DoorbellPollIntervalUs;
    }
    
    // 批处理大小 - 范围检查
    if (Config->BatchSize >= VNVME_MIN_BATCH_SIZE &&
        Config->BatchSize <= VNVME_MAX_BATCH_SIZE) {
        g_Config.BatchSize = Config->BatchSize;
    }
    
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL,
               "[VNVME:CONFIG] Updated: DebugLevel=%u, DebugFlags=0x%08X, "
               "PollInterval=%uus, BatchSize=%u\n",
               g_Config.DebugLevel, g_Config.DebugFlags,
               g_Config.DoorbellPollIntervalUs, g_Config.BatchSize);
    
    return STATUS_SUCCESS;
}
