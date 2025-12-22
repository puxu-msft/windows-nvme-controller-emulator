# 电源管理

本文档描述 Virtual NVMe 驱动的电源管理实现。

## 概述

Windows 电源管理涉及两个层面：
1. **系统电源状态 (S-states)**: S0 (工作) → S1/S2/S3 (睡眠) → S4 (休眠) → S5 (关机)
2. **设备电源状态 (D-states)**: D0 (全功率) → D1/D2 (中间) → D3 (关闭)

对于虚拟设备，物理电源消耗不是问题，但必须正确处理电源状态转换以确保数据完整性。

## 系统电源状态

### S-State 说明

| 状态 | 名称 | 说明 | 驱动行为 |
|------|------|------|----------|
| S0 | Working | 系统运行中 | 正常处理 I/O |
| S1 | Sleep | 轻度睡眠 (保留 CPU 上下文) | 暂停 I/O，保留状态 |
| S2 | Sleep | 中度睡眠 (CPU 上下文丢失) | 暂停 I/O，保留状态 |
| S3 | Standby | 深度睡眠 (仅内存供电) | 刷新缓存，保存状态 |
| S4 | Hibernate | 休眠 (内存写入磁盘) | 刷新所有缓存，准备关闭 |
| S5 | Soft Off | 软关机 | 完成所有 I/O，释放资源 |

### 电源状态转换流程

```
                    ┌────────────────────────────────────┐
                    │         系统电源管理器              │
                    └────────────┬───────────────────────┘
                                 │
                    ┌────────────▼───────────────────────┐
                    │      IRP_MJ_POWER                   │
                    │  IRP_MN_QUERY_POWER (可选)          │
                    │  IRP_MN_SET_POWER                   │
                    └────────────┬───────────────────────┘
                                 │
    ┌────────────────────────────┼────────────────────────────┐
    │                            │                            │
    ▼                            ▼                            ▼
┌─────────┐              ┌─────────────┐              ┌─────────────┐
│ S0 → Sx │              │ Sx → S0     │              │ D0 ↔ D3     │
│ 进入睡眠 │              │ 唤醒恢复    │              │ 设备电源    │
└─────────┘              └─────────────┘              └─────────────┘
```

## 设备电源状态

### D-State 说明

| 状态 | 名称 | 说明 | 功耗 |
|------|------|------|------|
| D0 | Full On | 设备完全运行 | 最高 |
| D1 | Intermediate | 轻度节能 (保留设备上下文) | 低于 D0 |
| D2 | Intermediate | 中度节能 (部分上下文保留) | 低于 D1 |
| D3hot | Sleep | 设备睡眠 (可快速唤醒) | 低 |
| D3cold | Off | 设备关闭 (电源移除) | 最低 |

### 虚拟设备的 D-State 映射

虚拟 NVMe 设备只需支持两种状态：

| 虚拟状态 | D-State | 行为 |
|----------|---------|------|
| 活动 | D0 | 正常处理所有 I/O 请求 |
| 停止 | D3 | 拒绝新 I/O，刷新缓存，保存状态 |

## WDF 电源管理回调

### 回调函数设置

```c
NTSTATUS EvtDriverDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    
    // PnP 回调
    pnpPowerCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = EvtDeviceReleaseHardware;
    
    // 电源状态回调
    pnpPowerCallbacks.EvtDeviceD0Entry = EvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = EvtDeviceD0Exit;
    
    // S0 空闲回调 (可选，用于运行时电源管理)
    pnpPowerCallbacks.EvtDeviceD0EntryPostInterruptsEnabled = NULL;
    pnpPowerCallbacks.EvtDeviceD0ExitPreInterruptsDisabled = NULL;
    
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    
    // 电源策略回调
    WDF_POWER_POLICY_EVENT_CALLBACKS powerPolicyCallbacks;
    WDF_POWER_POLICY_EVENT_CALLBACKS_INIT(&powerPolicyCallbacks);
    
    powerPolicyCallbacks.EvtDeviceArmWakeFromS0 = NULL;  // 虚拟设备不需要
    powerPolicyCallbacks.EvtDeviceArmWakeFromSx = NULL;  // 虚拟设备不需要
    powerPolicyCallbacks.EvtDeviceDisarmWakeFromS0 = NULL;
    powerPolicyCallbacks.EvtDeviceDisarmWakeFromSx = NULL;
    
    WdfDeviceInitSetPowerPolicyEventCallbacks(DeviceInit, &powerPolicyCallbacks);
    
    return STATUS_SUCCESS;
}
```

### D0Entry 回调 (设备上电)

```c
NTSTATUS EvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState)
{
    PDEVICE_CONTEXT pDevCtx = GetDeviceContext(Device);
    
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_POWER,
        "D0Entry: Previous state = %!WDF_POWER_DEVICE_STATE!", 
        PreviousState);
    
    switch (PreviousState) {
        case WdfPowerDeviceD3:
        case WdfPowerDeviceD3Final:
            // 从 D3 恢复 - 重新初始化控制器状态
            NvmeControllerReinitialize(pDevCtx);
            break;
            
        case WdfPowerDevicePrepareForHibernation:
            // 从休眠恢复 - 验证存储后端
            if (!ValidateStorageBackend(pDevCtx)) {
                return STATUS_DEVICE_DATA_ERROR;
            }
            break;
            
        case WdfPowerDeviceD1:
        case WdfPowerDeviceD2:
            // 从轻度睡眠恢复 - 快速重启
            break;
            
        default:
            // 首次启动或其他情况
            break;
    }
    
    // 标记控制器就绪
    pDevCtx->ControllerReady = TRUE;
    
    // 恢复队列处理
    WdfIoQueueStart(pDevCtx->IoQueue);
    
    return STATUS_SUCCESS;
}
```

### D0Exit 回调 (设备下电)

```c
NTSTATUS EvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState)
{
    PDEVICE_CONTEXT pDevCtx = GetDeviceContext(Device);
    NTSTATUS status = STATUS_SUCCESS;
    
    TraceEvents(TRACE_LEVEL_INFORMATION, DBG_POWER,
        "D0Exit: Target state = %!WDF_POWER_DEVICE_STATE!", 
        TargetState);
    
    // 停止接收新请求
    pDevCtx->ControllerReady = FALSE;
    WdfIoQueueStop(pDevCtx->IoQueue, NULL, NULL);
    
    // 等待进行中的 I/O 完成
    WaitForPendingIoCompletion(pDevCtx, POWER_TIMEOUT_MS);
    
    switch (TargetState) {
        case WdfPowerDeviceD3:
        case WdfPowerDeviceD3Final:
            // 进入 D3 - 刷新所有缓存
            status = FlushAllCaches(pDevCtx);
            if (!NT_SUCCESS(status)) {
                TraceEvents(TRACE_LEVEL_ERROR, DBG_POWER,
                    "Failed to flush caches: %!STATUS!", status);
                // 继续关闭，避免阻止系统睡眠
            }
            break;
            
        case WdfPowerDevicePrepareForHibernation:
            // 准备休眠 - 确保所有数据持久化
            status = FlushAllCaches(pDevCtx);
            SaveControllerState(pDevCtx);
            break;
            
        case WdfPowerDeviceD1:
        case WdfPowerDeviceD2:
            // 轻度睡眠 - 保持状态，可快速恢复
            break;
            
        default:
            break;
    }
    
    return status;
}
```

## S3/S4 状态处理

### S3 (睡眠/待机) 处理

S3 时，系统 RAM 保持供电，所有内存内容保留。

```c
// S3 进入前的处理
VOID PrepareForS3(PDEVICE_CONTEXT pDevCtx)
{
    // 1. 停止所有队列
    for (ULONG i = 0; i < pDevCtx->NumQueues; i++) {
        StopQueue(&pDevCtx->IoQueues[i]);
    }
    
    // 2. 刷新写缓存
    if (pDevCtx->WriteCache) {
        FlushWriteCache(pDevCtx->WriteCache);
    }
    
    // 3. 如果使用文件后端，刷新文件缓存
    if (pDevCtx->BackendType == BACKEND_FILE) {
        FlushFileBuffers(pDevCtx->BackendFile);
    }
    
    // 4. 保存 NVMe 控制器状态 (可选)
    // 内存后端：状态在 RAM 中自动保留
    // 文件后端：状态已持久化
}
```

### S4 (休眠) 处理

S4 时，系统 RAM 内容写入休眠文件，然后断电。

```c
// S4 进入前的处理
VOID PrepareForS4(PDEVICE_CONTEXT pDevCtx)
{
    // 1. 完成所有待处理 I/O
    DrainAllQueues(pDevCtx);
    
    // 2. 刷新所有缓存
    FlushWriteCache(pDevCtx->WriteCache);
    
    // 3. 文件后端处理
    if (pDevCtx->BackendType == BACKEND_FILE) {
        // 关闭文件句柄 (休眠期间不能持有句柄)
        ZwClose(pDevCtx->BackendFile);
        pDevCtx->BackendFile = NULL;
    }
    
    // 4. 内存后端处理
    if (pDevCtx->BackendType == BACKEND_MEMORY) {
        // 注意：纯内存后端数据会在休眠时保存
        // 但如果不在休眠文件中，数据会丢失
        // 建议：内存后端视为非持久性存储
    }
}

// S4 恢复后的处理
NTSTATUS ResumeFromS4(PDEVICE_CONTEXT pDevCtx)
{
    // 1. 重新打开文件后端
    if (pDevCtx->BackendType == BACKEND_FILE) {
        NTSTATUS status = OpenBackendFile(
            pDevCtx->BackendFilePath,
            &pDevCtx->BackendFile);
        if (!NT_SUCCESS(status)) {
            return status;
        }
    }
    
    // 2. 重新初始化队列
    InitializeQueues(pDevCtx);
    
    // 3. 恢复控制器状态
    pDevCtx->Controller.CSTS.RDY = 1;
    
    return STATUS_SUCCESS;
}
```

## 运行时电源管理 (Runtime PM)

### S0 空闲电源管理

WDF 支持 S0 状态下的设备空闲关闭：

```c
NTSTATUS ConfigureIdlePowerManagement(WDFDEVICE Device)
{
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;
    
    WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(
        &idleSettings,
        IdleCannotWakeFromS0  // 虚拟设备不能唤醒系统
    );
    
    // 空闲超时
    idleSettings.IdleTimeout = 60000;  // 60 秒
    idleSettings.IdleTimeoutType = DriverManagedIdleTimeout;
    
    // 空闲时进入 D3
    idleSettings.DxState = PowerDeviceD3;
    
    // 启用空闲检测
    idleSettings.Enabled = WdfTrue;
    
    return WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
}
```

### I/O 队列电源管理

```c
NTSTATUS ConfigurePowerManagedQueues(PDEVICE_CONTEXT pDevCtx)
{
    WDF_IO_QUEUE_CONFIG queueConfig;
    
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchParallel
    );
    
    // 启用电源管理队列
    // 当设备不在 D0 时，队列自动停止
    queueConfig.PowerManaged = WdfTrue;
    
    queueConfig.EvtIoRead = EvtIoRead;
    queueConfig.EvtIoWrite = EvtIoWrite;
    queueConfig.EvtIoDeviceControl = EvtIoDeviceControl;
    
    return WdfIoQueueCreate(
        pDevCtx->Device,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &pDevCtx->IoQueue
    );
}
```

## 电源状态机

```
                         ┌─────────────────────┐
                         │                     │
                         ▼                     │
    ┌──────────┐    ┌──────────┐    ┌──────────┴───┐
    │ PowerOff │───▶│   D0     │───▶│     D3       │
    │          │    │ (Active) │    │  (Stopped)   │
    └──────────┘    └────┬─────┘    └──────────────┘
         ▲               │                 ▲
         │               ▼                 │
         │          ┌──────────┐           │
         │          │  I/O     │           │
         │          │ 处理中    │           │
         │          └──────────┘           │
         │                                 │
         │     S3/S4 电源事件               │
         └─────────────────────────────────┘
```

## NVMe 电源状态 (模拟)

NVMe 规范定义了自己的电源状态 (PS0-PS31)，通过 Set Features 命令设置：

```c
typedef struct _NVME_POWER_STATE_DESCRIPTOR {
    UINT16  MP;         // 最大功耗 (单位: 0.01W 或 0.0001W)
    UINT8   Reserved1;
    UINT8   MPS     : 1;    // 最大功耗刻度 (0=0.01W, 1=0.0001W)
    UINT8   NOPS    : 1;    // 非操作状态
    UINT8   Reserved2 : 6;
    UINT32  ENLAT;      // 进入延迟 (微秒)
    UINT32  EXLAT;      // 退出延迟 (微秒)
    UINT8   RRT     : 5;    // 相对读吞吐量
    UINT8   Reserved3 : 3;
    UINT8   RRL     : 5;    // 相对读延迟
    UINT8   Reserved4 : 3;
    UINT8   RWT     : 5;    // 相对写吞吐量
    UINT8   Reserved5 : 3;
    UINT8   RWL     : 5;    // 相对写延迟
    UINT8   Reserved6 : 3;
    UINT8   Reserved7[16];
    
} NVME_POWER_STATE_DESCRIPTOR, *PNVME_POWER_STATE_DESCRIPTOR;
```

对于虚拟设备，可以简化为两种状态：

| NVMe 电源状态 | 描述 | 虚拟实现 |
|---------------|------|----------|
| PS0 | 最大性能 | 正常处理 |
| PS1+ | 节能状态 | 忽略或返回 PS0 相同值 |

## 最佳实践

### 数据完整性

1. **D0Exit 时务必刷新缓存**
   - 写缓存中的数据必须持久化
   - 使用超时防止无限等待

2. **处理 I/O 排空**
   ```c
   NTSTATUS DrainQueues(PDEVICE_CONTEXT pDevCtx, ULONG TimeoutMs)
   {
       LARGE_INTEGER timeout;
       timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000;
       
       return KeWaitForSingleObject(
           &pDevCtx->IoCompleteEvent,
           Executive,
           KernelMode,
           FALSE,
           &timeout
       );
   }
   ```

### 避免电源问题

1. **不要在电源回调中阻塞太久**
   - 系统有电源超时限制
   - 使用工作项处理长时间操作

2. **正确处理 TargetState**
   ```c
   // 区分正常 D3 和系统关机的 D3
   if (TargetState == WdfPowerDeviceD3Final) {
       // 系统关机 - 完全清理
       CleanupAllResources(pDevCtx);
   } else if (TargetState == WdfPowerDeviceD3) {
       // 普通睡眠 - 保留可恢复状态
       SaveState(pDevCtx);
   }
   ```

3. **处理电源回调失败**
   - D0Exit 失败通常被忽略 (系统必须睡眠)
   - D0Entry 失败可能导致设备不可用

## 测试电源管理

### 测试命令

```powershell
# 测试睡眠/唤醒
powercfg /sleepstudy

# 触发 S3 睡眠
rundll32.exe powrprof.dll,SetSuspendState Sleep

# 触发 S4 休眠  
shutdown /h

# 查看电源配置
powercfg /query

# 启用休眠
powercfg /hibernate on
```

### Driver Verifier 电源测试

```powershell
verifier /flags 0x2000 /driver vnvme.sys vnvmebus.sys
```

### 常见测试场景

1. 系统睡眠/唤醒循环
2. 快速睡眠/唤醒 (S3 压力测试)
3. 休眠/恢复
4. 带活动 I/O 的电源转换
5. 空闲超时触发的 D3 转换
