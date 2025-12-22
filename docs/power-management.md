# 电源管理

本文档描述 Virtual NVMe StorPort Miniport 驱动的电源管理实现。

## 概述

StorPort Miniport 驱动的电源管理由 StorPort 端口驱动统一管理。Miniport 驱动通过 `HwAdapterControl` 回调接收电源状态变化通知。

### StorPort 电源管理特点

- **简化的接口**：无需直接处理电源 IRP
- **自动队列管理**：StorPort 在电源转换期间自动暂停/恢复 I/O 队列
- **状态保存**：Miniport 负责保存和恢复必要的状态信息

---

## 系统电源状态

### S-State 说明

| 状态 | 名称 | 说明 | 驱动行为 |
|------|------|------|----------|
| S0 | Working | 系统运行中 | 正常处理 I/O |
| S1/S2 | Sleep | 轻度睡眠 | 暂停 I/O，保留状态 |
| S3 | Standby | 深度睡眠 (仅内存供电) | 刷新缓存，保存状态 |
| S4 | Hibernate | 休眠 (内存写入磁盘) | 刷新所有缓存 |
| S5 | Soft Off | 软关机 | 完成所有 I/O，释放资源 |

### 电源状态转换流程

```
                    ┌────────────────────────────────────┐
                    │         系统电源管理器              │
                    └────────────┬───────────────────────┘
                                 │
                    ┌────────────▼───────────────────────┐
                    │           storport.sys              │
                    │      (处理 IRP_MJ_POWER)            │
                    └────────────┬───────────────────────┘
                                 │
                    ┌────────────▼───────────────────────┐
                    │        HwAdapterControl             │
                    │                                     │
                    │  ScsiStopAdapter  → 进入低功耗      │
                    │  ScsiRestartAdapter → 恢复运行      │
                    └─────────────────────────────────────┘
```

---

## HwAdapterControl 回调

### 回调函数签名

```c
SCSI_ADAPTER_CONTROL_STATUS
VNvmeHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters);
```

### 控制类型枚举

| ControlType | 说明 | 必须处理 |
|-------------|------|----------|
| ScsiQuerySupportedControlTypes | 查询支持的控制类型 | ✓ |
| ScsiStopAdapter | 停止适配器 (系统休眠/关机) | ✓ |
| ScsiRestartAdapter | 重启适配器 (系统唤醒) | ✓ |
| ScsiSetBootConfig | 设置引导配置 | 可选 |
| ScsiSetRunningConfig | 设置运行配置 | 可选 |
| ScsiPowerSettingNotification | 电源设置通知 | 可选 |
| ScsiAdapterPower | 适配器电源控制 | 可选 |
| ScsiAdapterPoFxPowerRequired | PoFx 电源要求 | 可选 |
| ScsiAdapterPoFxPowerActive | PoFx 电源活动 | 可选 |
| ScsiAdapterPoFxPowerSetFState | PoFx F-State 设置 | 可选 |
| ScsiAdapterPoFxPowerControl | PoFx 电源控制 | 可选 |

### 完整实现

```c
//
// HwAdapterControl 实现
//
SCSI_ADAPTER_CONTROL_STATUS
VNvmeHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    
    switch (ControlType) {
        
        case ScsiQuerySupportedControlTypes: {
            //
            // 报告支持的控制类型
            //
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST pList = 
                (PSCSI_SUPPORTED_CONTROL_TYPE_LIST)Parameters;
            
            ULONG i;
            for (i = 0; i < pList->MaxControlType; i++) {
                pList->SupportedTypeList[i] = FALSE;
            }
            
            // 必须支持的类型
            if (ScsiQuerySupportedControlTypes < pList->MaxControlType) {
                pList->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            }
            if (ScsiStopAdapter < pList->MaxControlType) {
                pList->SupportedTypeList[ScsiStopAdapter] = TRUE;
            }
            if (ScsiRestartAdapter < pList->MaxControlType) {
                pList->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            }
            if (ScsiSetBootConfig < pList->MaxControlType) {
                pList->SupportedTypeList[ScsiSetBootConfig] = TRUE;
            }
            if (ScsiSetRunningConfig < pList->MaxControlType) {
                pList->SupportedTypeList[ScsiSetRunningConfig] = TRUE;
            }
            
            return ScsiAdapterControlSuccess;
        }
        
        case ScsiStopAdapter: {
            //
            // 系统进入休眠或关机前调用
            // 必须在此完成所有待处理 I/O
            //
            VNvmeTraceInfo(pAdapter, "ScsiStopAdapter called");
            
            // 设置适配器状态为停止
            pAdapter->AdapterState = VNVME_ADAPTER_STATE_STOPPED;
            
            // 刷新所有 LUN 的后端缓存
            for (ULONG lun = 0; lun < VNVME_MAX_LUNS; lun++) {
                PVNVME_LU_EXTENSION pLu = &pAdapter->Luns[lun];
                
                if (pLu->Flags.Present && pLu->pBackend) {
                    // 刷新缓存
                    if (pLu->pBackend->Flush) {
                        NTSTATUS status = pLu->pBackend->Flush(
                            pLu->pBackendContext);
                        
                        if (!NT_SUCCESS(status)) {
                            VNvmeTraceWarning(pAdapter, 
                                "Failed to flush LUN %u: 0x%08X",
                                lun, status);
                        }
                    }
                    
                    // 保存状态 (如果需要)
                    VNvmeSaveLunState(pLu);
                }
            }
            
            // 保存适配器统计信息到注册表 (可选)
            VNvmeSaveAdapterStatistics(pAdapter);
            
            return ScsiAdapterControlSuccess;
        }
        
        case ScsiRestartAdapter: {
            //
            // 系统从休眠唤醒后调用
            // 需要恢复适配器到运行状态
            //
            VNvmeTraceInfo(pAdapter, "ScsiRestartAdapter called");
            
            // 恢复适配器统计信息
            VNvmeRestoreAdapterStatistics(pAdapter);
            
            // 重新初始化后端连接
            for (ULONG lun = 0; lun < VNVME_MAX_LUNS; lun++) {
                PVNVME_LU_EXTENSION pLu = &pAdapter->Luns[lun];
                
                if (pLu->Flags.Present) {
                    // 恢复后端连接
                    NTSTATUS status = VNvmeReinitializeBackend(pLu);
                    
                    if (!NT_SUCCESS(status)) {
                        VNvmeTraceError(pAdapter,
                            "Failed to reinitialize LUN %u: 0x%08X",
                            lun, status);
                        
                        // 标记 LUN 为离线
                        pLu->Flags.Online = FALSE;
                    } else {
                        pLu->Flags.Online = TRUE;
                    }
                }
            }
            
            // 恢复适配器状态
            pAdapter->AdapterState = VNVME_ADAPTER_STATE_RUNNING;
            
            return ScsiAdapterControlSuccess;
        }
        
        case ScsiSetBootConfig: {
            //
            // 设置引导配置
            // 虚拟设备通常不需要特殊处理
            //
            return ScsiAdapterControlSuccess;
        }
        
        case ScsiSetRunningConfig: {
            //
            // 设置运行配置
            // 可以用于更新运行时配置
            //
            return ScsiAdapterControlSuccess;
        }
        
        default:
            return ScsiAdapterControlUnsuccessful;
    }
}
```

---

## 状态保存与恢复

### 需要保存的状态

```c
//
// 适配器持久状态 (保存到注册表)
//
typedef struct _VNVME_ADAPTER_PERSISTENT_STATE {
    ULONG       Version;            // 状态版本
    ULONG       Checksum;           // 校验和
    
    // 统计信息
    ULONGLONG   TotalReadOperations;
    ULONGLONG   TotalWriteOperations;
    ULONGLONG   TotalBytesRead;
    ULONGLONG   TotalBytesWritten;
    ULONGLONG   TotalErrorCount;
    
    // 运行时间
    ULONGLONG   TotalUptimeSeconds;
    
} VNVME_ADAPTER_PERSISTENT_STATE, *PVNVME_ADAPTER_PERSISTENT_STATE;

//
// LUN 持久状态
//
typedef struct _VNVME_LUN_PERSISTENT_STATE {
    ULONG       Version;
    UCHAR       LunId;
    BOOLEAN     WasOnline;
    ULONGLONG   LastKnownSize;
    
} VNVME_LUN_PERSISTENT_STATE, *PVNVME_LUN_PERSISTENT_STATE;
```

### 状态保存函数

```c
//
// 保存适配器统计到注册表
//
NTSTATUS
VNvmeSaveAdapterStatistics(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter)
{
    HANDLE hKey = NULL;
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING keyPath;
    
    RtlInitUnicodeString(&keyPath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\vnvme\\State");
    
    InitializeObjectAttributes(&objAttr, &keyPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL, NULL);
    
    status = ZwCreateKey(&hKey, KEY_WRITE, &objAttr, 0, NULL,
        REG_OPTION_NON_VOLATILE, NULL);
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 准备状态数据
    VNVME_ADAPTER_PERSISTENT_STATE state = {0};
    state.Version = 1;
    state.TotalReadOperations = pAdapter->Statistics.ReadOperations;
    state.TotalWriteOperations = pAdapter->Statistics.WriteOperations;
    state.TotalBytesRead = pAdapter->Statistics.BytesRead;
    state.TotalBytesWritten = pAdapter->Statistics.BytesWritten;
    state.TotalUptimeSeconds = pAdapter->Statistics.UptimeSeconds;
    
    // 计算校验和
    state.Checksum = VNvmeCalculateChecksum(&state, sizeof(state));
    
    // 写入注册表
    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, L"AdapterState");
    
    status = ZwSetValueKey(hKey, &valueName, 0, REG_BINARY,
        &state, sizeof(state));
    
    ZwClose(hKey);
    
    return status;
}

//
// 恢复适配器统计
//
NTSTATUS
VNvmeRestoreAdapterStatistics(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter)
{
    HANDLE hKey = NULL;
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    UNICODE_STRING keyPath;
    
    RtlInitUnicodeString(&keyPath,
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\vnvme\\State");
    
    InitializeObjectAttributes(&objAttr, &keyPath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL, NULL);
    
    status = ZwOpenKey(&hKey, KEY_READ, &objAttr);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 读取状态
    UNICODE_STRING valueName;
    RtlInitUnicodeString(&valueName, L"AdapterState");
    
    UCHAR buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + 
                 sizeof(VNVME_ADAPTER_PERSISTENT_STATE)];
    PKEY_VALUE_PARTIAL_INFORMATION pValueInfo = 
        (PKEY_VALUE_PARTIAL_INFORMATION)buffer;
    ULONG resultLength;
    
    status = ZwQueryValueKey(hKey, &valueName, 
        KeyValuePartialInformation,
        pValueInfo, sizeof(buffer), &resultLength);
    
    if (NT_SUCCESS(status)) {
        PVNVME_ADAPTER_PERSISTENT_STATE pState = 
            (PVNVME_ADAPTER_PERSISTENT_STATE)pValueInfo->Data;
        
        // 验证校验和
        ULONG savedChecksum = pState->Checksum;
        pState->Checksum = 0;
        ULONG calcChecksum = VNvmeCalculateChecksum(pState, sizeof(*pState));
        
        if (savedChecksum == calcChecksum && pState->Version == 1) {
            // 恢复统计信息
            pAdapter->Statistics.ReadOperations = pState->TotalReadOperations;
            pAdapter->Statistics.WriteOperations = pState->TotalWriteOperations;
            pAdapter->Statistics.BytesRead = pState->TotalBytesRead;
            pAdapter->Statistics.BytesWritten = pState->TotalBytesWritten;
            pAdapter->Statistics.UptimeSeconds = pState->TotalUptimeSeconds;
        }
    }
    
    ZwClose(hKey);
    return status;
}
```

---

## 后端连接恢复

### 重新初始化后端

```c
//
// 重新初始化后端连接
//
NTSTATUS
VNvmeReinitializeBackend(
    _Inout_ PVNVME_LU_EXTENSION pLu)
{
    NTSTATUS status = STATUS_SUCCESS;
    
    if (!pLu->pBackend) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    switch (pLu->BackendType) {
        
        case VNVME_BACKEND_MEMORY:
            //
            // 内存后端: 休眠后内存内容已丢失
            // 需要清零或从备份恢复
            //
            if (pLu->pBackendContext) {
                // 内存在休眠后仍然有效 (S3)
                // 但在休眠到磁盘后 (S4) 需要从镜像恢复
                status = STATUS_SUCCESS;
            }
            break;
            
        case VNVME_BACKEND_FILE:
            //
            // 文件后端: 重新打开文件句柄
            //
            if (pLu->pBackendContext) {
                PVNVME_FILE_BACKEND_CONTEXT pFileCtx = 
                    (PVNVME_FILE_BACKEND_CONTEXT)pLu->pBackendContext;
                
                // 关闭旧句柄
                if (pFileCtx->FileHandle) {
                    ZwClose(pFileCtx->FileHandle);
                    pFileCtx->FileHandle = NULL;
                }
                
                // 重新打开文件
                OBJECT_ATTRIBUTES objAttr;
                IO_STATUS_BLOCK ioStatus;
                
                InitializeObjectAttributes(&objAttr,
                    &pFileCtx->FilePath,
                    OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                    NULL, NULL);
                
                status = ZwOpenFile(
                    &pFileCtx->FileHandle,
                    GENERIC_READ | GENERIC_WRITE,
                    &objAttr,
                    &ioStatus,
                    FILE_SHARE_READ,
                    FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT);
            }
            break;
            
        case VNVME_BACKEND_VHD:
            //
            // VHD 后端: 重新打开 VHD
            //
            status = VNvmeVhdBackendReopen(pLu);
            break;
            
        case VNVME_BACKEND_REMOTE:
            //
            // 远程后端: 重新建立网络连接
            //
            status = VNvmeRemoteBackendReconnect(pLu);
            break;
            
        default:
            status = STATUS_NOT_SUPPORTED;
            break;
    }
    
    if (NT_SUCCESS(status)) {
        // 验证后端状态
        if (pLu->pBackend->Validate) {
            status = pLu->pBackend->Validate(pLu->pBackendContext);
        }
    }
    
    return status;
}
```

---

## 运行时电源管理 (可选)

### Power Framework (PoFx) 集成

对于需要更细粒度电源管理的场景，可以使用 StorPort 的 PoFx 支持：

```c
//
// 注册 PoFx 组件 (在 HwFindAdapter 中)
//
NTSTATUS
VNvmeRegisterPoFx(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter)
{
    STOR_POFX_DEVICE_V2 poFxDevice = {0};
    
    poFxDevice.Version = STOR_POFX_DEVICE_VERSION_V2;
    poFxDevice.Size = sizeof(STOR_POFX_DEVICE_V2);
    
    // 设置空闲超时 (毫秒)
    poFxDevice.IdleTimeout = 30000;  // 30 秒
    
    // 设置组件数量
    poFxDevice.ComponentCount = 1;
    
    // 配置组件
    STOR_POFX_COMPONENT component = {0};
    component.Version = STOR_POFX_COMPONENT_VERSION_V1;
    component.Size = sizeof(STOR_POFX_COMPONENT);
    component.FStateCount = 2;  // F0 (活动) 和 F1 (空闲)
    component.DeepestWakeableFState = 1;
    
    // F-State 定义
    STOR_POFX_COMPONENT_IDLE_STATE fStates[2];
    
    // F0 - 活动状态
    fStates[0].Version = STOR_POFX_COMPONENT_IDLE_STATE_VERSION_V1;
    fStates[0].Size = sizeof(STOR_POFX_COMPONENT_IDLE_STATE);
    fStates[0].TransitionLatency = 0;
    fStates[0].ResidencyRequirement = 0;
    fStates[0].NominalPower = 100;  // 100% 功耗
    
    // F1 - 空闲状态
    fStates[1].Version = STOR_POFX_COMPONENT_IDLE_STATE_VERSION_V1;
    fStates[1].Size = sizeof(STOR_POFX_COMPONENT_IDLE_STATE);
    fStates[1].TransitionLatency = 1000;  // 1 毫秒唤醒延迟
    fStates[1].ResidencyRequirement = 10000;  // 10 毫秒最小停留
    fStates[1].NominalPower = 10;  // 10% 功耗
    
    component.IdleStates = fStates;
    poFxDevice.Components = &component;
    
    // 注册到 StorPort
    ULONG status = StorPortInitializePoFxPower(
        pAdapter,
        &poFxDevice,
        NULL,  // D3 Cold 信息 (可选)
        NULL   // 保留
    );
    
    return (status == STOR_STATUS_SUCCESS) ? 
           STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

//
// 处理 PoFx F-State 转换
//
SCSI_ADAPTER_CONTROL_STATUS
VNvmeHandlePoFxFStateSet(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _In_ PSTOR_POFX_FSTATE_CONTEXT pFStateContext)
{
    ULONG componentIndex = pFStateContext->ComponentIndex;
    ULONG fState = pFStateContext->FState;
    
    VNvmeTraceInfo(pAdapter, 
        "PoFx F-State change: Component=%u, FState=%u",
        componentIndex, fState);
    
    if (fState == 0) {
        // 进入活动状态 (F0)
        pAdapter->Flags.IdlePowerState = FALSE;
    } else {
        // 进入空闲状态 (F1+)
        pAdapter->Flags.IdlePowerState = TRUE;
    }
    
    return ScsiAdapterControlSuccess;
}
```

---

## 虚拟设备电源管理注意事项

### 内存后端特殊处理

内存后端在系统休眠 (S4) 时需要特殊考虑：

```c
//
// 检查是否需要保存内存后端数据
//
VOID
VNvmeCheckMemoryBackendHibernate(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter)
{
    for (ULONG lun = 0; lun < VNVME_MAX_LUNS; lun++) {
        PVNVME_LU_EXTENSION pLu = &pAdapter->Luns[lun];
        
        if (pLu->Flags.Present && 
            pLu->BackendType == VNVME_BACKEND_MEMORY) {
            
            // 检查是否启用了持久化
            if (pLu->Flags.Persistent) {
                // 将内存内容保存到文件
                VNvmeSaveMemoryToFile(pLu, pLu->PersistentFilePath);
            } else {
                // 警告用户数据将丢失
                VNvmeTraceWarning(pAdapter,
                    "LUN %u (memory backend) data will be lost on hibernate",
                    lun);
            }
        }
    }
}
```

### 测试电源转换

```powershell
# 测试休眠
powercfg /hibernate on
shutdown /h

# 测试睡眠
powercfg /a  # 查看支持的睡眠状态
rundll32.exe powrprof.dll,SetSuspendState 0,1,0  # 睡眠

# 检查驱动电源状态
Get-WmiObject -Class Win32_PnPEntity | 
    Where-Object { $_.Name -like "*VNvme*" } |
    Select-Object Name, Status, ConfigManagerErrorCode
```

---

## 参考资料

- [StorPort Power Management](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/handling-power-requests-to-storage-peripherals)
- [SCSI_ADAPTER_CONTROL_TYPE](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/ne-storport-scsi_adapter_control_type)
- [Power Framework (PoFx)](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/overview-of-the-power-management-framework)
- [StorPort PoFx APIs](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storport-pofx-support)
