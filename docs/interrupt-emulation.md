# 中断仿真

本文档详细说明 MSI-X 中断的仿真实现。

## 概述

NVMe 控制器使用 MSI-X 中断通知主机有命令完成。我们的仿真器需要：

1. 实现 MSI-X Capability 结构
2. 处理 MSI-X Table 和 PBA 的 MMIO 访问
3. 在命令完成时注入中断到 Windows

```
┌─────────────────────────────────────────────────────────────────────┐
│                      MSI-X 中断流程                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   1. stornvme.sys 配置 MSI-X                                        │
│      ├── 读取 MSI-X Capability                                      │
│      ├── 写入 Message Address/Data 到 MSI-X Table                   │
│      └── 使能 MSI-X (Capability 控制位)                              │
│                                                                      │
│   2. 命令完成时                                                       │
│      ├── vnvme_emu.sys 将完成写入 CQ                                 │
│      ├── 读取对应向量的 Message Address/Data                         │
│      └── 触发中断注入                                                 │
│                                                                      │
│   3. 中断处理                                                         │
│      ├── Windows 调用 stornvme.sys 的 ISR                            │
│      ├── stornvme.sys 读取 CQ 完成条目                               │
│      └── 更新 CQ Head Doorbell                                       │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## MSI-X Capability 结构

### PCIe Configuration Space 中的 MSI-X Capability

```c
//
// MSI-X Capability (位于 PCIe Config Space)
//
typedef struct _MSIX_CAPABILITY {
    // Capability Header
    UCHAR CapabilityId;      // 0x11 = MSI-X
    UCHAR NextCapability;
    
    // Message Control
    union {
        struct {
            USHORT TableSize : 11;    // Table Size (N-1)
            USHORT Reserved  : 3;
            USHORT FuncMask  : 1;     // Function Mask
            USHORT Enable    : 1;     // MSI-X Enable
        };
        USHORT AsUshort;
    } MessageControl;
    
    // Table Offset/BIR
    union {
        struct {
            ULONG BIR        : 3;     // BAR Indicator Register
            ULONG Offset     : 29;    // Table Offset (8-byte aligned)
        };
        ULONG AsUlong;
    } TableOffset;
    
    // PBA Offset/BIR
    union {
        struct {
            ULONG BIR        : 3;     // BAR Indicator Register
            ULONG Offset     : 29;    // PBA Offset (8-byte aligned)
        };
        ULONG AsUlong;
    } PbaOffset;
    
} MSIX_CAPABILITY, *PMSIX_CAPABILITY;

C_ASSERT(sizeof(MSIX_CAPABILITY) == 12);
```

### MSI-X Table 结构

每个向量在 Table 中占 16 字节：

```c
//
// MSI-X Table Entry (16 bytes per vector)
//
typedef struct _MSIX_TABLE_ENTRY {
    ULONG MessageAddressLo;      // Message Address [31:0]
    ULONG MessageAddressHi;      // Message Address [63:32]
    ULONG MessageData;           // Message Data
    ULONG VectorControl;         // Bit 0: Mask
} MSIX_TABLE_ENTRY, *PMSIX_TABLE_ENTRY;

C_ASSERT(sizeof(MSIX_TABLE_ENTRY) == 16);

#define MSIX_VECTOR_MASKED  0x00000001
```

### Pending Bit Array (PBA)

```c
//
// PBA 结构 (每个向量 1 bit)
//
// 对于 N 个向量，PBA 大小为 ceil(N/64) * 8 字节
// 位 i 表示向量 i 是否有挂起的中断
//
```

## MSI-X 内存布局

我们将 MSI-X Table 和 PBA 放在 BAR0 的保留区域：

```
BAR0 Layout:
┌─────────────────────────────────────┐ 0x0000
│     NVMe Controller Registers       │
│            (4 KB)                   │
├─────────────────────────────────────┤ 0x1000
│         Doorbell Registers          │
│            (4 KB)                   │
├─────────────────────────────────────┤ 0x2000
│          MSI-X Table                │
│    (64 vectors × 16 bytes = 1 KB)   │
├─────────────────────────────────────┤ 0x2400
│       MSI-X PBA                     │
│    (64 bits = 8 bytes, aligned)     │
└─────────────────────────────────────┘
```

## 数据结构

```c
//
// MSI-X 向量状态
//
typedef struct _VNVME_MSIX_VECTOR {
    ULONG64 MessageAddress;      // 组合的 64-bit 地址
    ULONG MessageData;           // 消息数据
    BOOLEAN Masked;              // 向量被屏蔽
    BOOLEAN Pending;             // 有挂起的中断
} VNVME_MSIX_VECTOR, *PVNVME_MSIX_VECTOR;

//
// MSI-X 状态
//
typedef struct _VNVME_MSIX_STATE {
    // 配置
    BOOLEAN Enabled;             // MSI-X 使能
    BOOLEAN FunctionMask;        // 全局屏蔽
    USHORT VectorCount;          // 向量数量
    
    // 向量数组
    VNVME_MSIX_VECTOR Vectors[VNVME_MAX_MSIX_VECTORS];
    
    // PBA
    ULONG64 Pba[(VNVME_MAX_MSIX_VECTORS + 63) / 64];
    
    // 中断注入机制
    PVOID InterruptObject;       // 用于中断注入
    
    // 统计
    ULONG64 InterruptsGenerated;
    ULONG64 InterruptsMasked;
    
} VNVME_MSIX_STATE, *PVNVME_MSIX_STATE;

#define VNVME_MAX_MSIX_VECTORS  64
```

## MSI-X 初始化

```c
//
// 初始化 MSI-X 状态
//
NTSTATUS VnvmeInitializeMsix(
    _In_ PVNVME_CONTROLLER Controller)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    
    RtlZeroMemory(msix, sizeof(*msix));
    
    // 默认禁用
    msix->Enabled = FALSE;
    msix->FunctionMask = FALSE;
    
    // 支持最多 64 个向量
    // 实际数量 = 1 (Admin) + MaxIoQueues
    msix->VectorCount = min(1 + Controller->MaxIoQueues,
                           VNVME_MAX_MSIX_VECTORS);
    
    // 初始化所有向量为屏蔽状态
    for (USHORT i = 0; i < msix->VectorCount; i++) {
        msix->Vectors[i].Masked = TRUE;
        msix->Vectors[i].Pending = FALSE;
    }
    
    // 设置 PCIe Config Space 中的 MSI-X Capability
    VnvmeSetupMsixCapability(Controller);
    
    return STATUS_SUCCESS;
}

//
// 设置 MSI-X Capability
//
VOID VnvmeSetupMsixCapability(
    _In_ PVNVME_CONTROLLER Controller)
{
    PMSIX_CAPABILITY cap = &Controller->PcieConfig.MsixCapability;
    
    cap->CapabilityId = 0x11;  // MSI-X
    cap->NextCapability = 0;   // 最后一个 Capability
    
    // Message Control
    cap->MessageControl.TableSize = Controller->MsixState.VectorCount - 1;
    cap->MessageControl.Enable = 0;
    cap->MessageControl.FuncMask = 0;
    
    // Table Offset: BAR0 + 0x2000
    cap->TableOffset.BIR = 0;          // BAR0
    cap->TableOffset.Offset = 0x2000 >> 3;  // 8-byte aligned
    
    // PBA Offset: BAR0 + 0x2400
    cap->PbaOffset.BIR = 0;            // BAR0
    cap->PbaOffset.Offset = 0x2400 >> 3;
}
```

## MSI-X Table MMIO 处理

### Table 读取

```c
//
// 读取 MSI-X Table
//
NTSTATUS VnvmeMsixTableRead(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _Out_ PULONG Value)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    ULONG vectorIndex = Offset / sizeof(MSIX_TABLE_ENTRY);
    ULONG fieldOffset = Offset % sizeof(MSIX_TABLE_ENTRY);
    
    if (vectorIndex >= msix->VectorCount) {
        *Value = 0;
        return STATUS_SUCCESS;
    }
    
    PVNVME_MSIX_VECTOR vector = &msix->Vectors[vectorIndex];
    
    switch (fieldOffset) {
    case 0:  // MessageAddressLo
        *Value = (ULONG)vector->MessageAddress;
        break;
        
    case 4:  // MessageAddressHi
        *Value = (ULONG)(vector->MessageAddress >> 32);
        break;
        
    case 8:  // MessageData
        *Value = vector->MessageData;
        break;
        
    case 12: // VectorControl
        *Value = vector->Masked ? MSIX_VECTOR_MASKED : 0;
        break;
        
    default:
        *Value = 0;
        break;
    }
    
    return STATUS_SUCCESS;
}
```

### Table 写入

```c
//
// 写入 MSI-X Table
//
NTSTATUS VnvmeMsixTableWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    ULONG vectorIndex = Offset / sizeof(MSIX_TABLE_ENTRY);
    ULONG fieldOffset = Offset % sizeof(MSIX_TABLE_ENTRY);
    
    if (vectorIndex >= msix->VectorCount) {
        return STATUS_SUCCESS;
    }
    
    PVNVME_MSIX_VECTOR vector = &msix->Vectors[vectorIndex];
    
    switch (fieldOffset) {
    case 0:  // MessageAddressLo
        vector->MessageAddress = (vector->MessageAddress & 0xFFFFFFFF00000000) | Value;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_MSIX,
            "Vector %d: Address Lo = 0x%08X", vectorIndex, Value);
        break;
        
    case 4:  // MessageAddressHi
        vector->MessageAddress = (vector->MessageAddress & 0x00000000FFFFFFFF) | 
                                 ((ULONG64)Value << 32);
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_MSIX,
            "Vector %d: Address Hi = 0x%08X", vectorIndex, Value);
        break;
        
    case 8:  // MessageData
        vector->MessageData = Value;
        TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_MSIX,
            "Vector %d: Data = 0x%08X", vectorIndex, Value);
        break;
        
    case 12: // VectorControl
        {
            BOOLEAN wasMasked = vector->Masked;
            vector->Masked = (Value & MSIX_VECTOR_MASKED) != 0;
            
            TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_MSIX,
                "Vector %d: Mask = %d", vectorIndex, vector->Masked);
            
            // 如果解除屏蔽且有挂起的中断，发送中断
            if (wasMasked && !vector->Masked && vector->Pending) {
                VnvmeDeliverMsixInterrupt(Controller, vectorIndex);
            }
        }
        break;
    }
    
    return STATUS_SUCCESS;
}
```

### PBA 访问

```c
//
// 读取 PBA
//
NTSTATUS VnvmeMsixPbaRead(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _Out_ PULONG Value)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    
    // PBA 是只读的，返回当前挂起状态
    ULONG qwordIndex = Offset / 8;
    ULONG dwordOffset = (Offset % 8) / 4;
    
    if (qwordIndex >= ARRAYSIZE(msix->Pba)) {
        *Value = 0;
        return STATUS_SUCCESS;
    }
    
    // 构建 PBA 值
    ULONG64 pbaValue = 0;
    for (USHORT i = 0; i < msix->VectorCount; i++) {
        if (msix->Vectors[i].Pending) {
            pbaValue |= (1ULL << i);
        }
    }
    
    msix->Pba[qwordIndex] = pbaValue;
    
    if (dwordOffset == 0) {
        *Value = (ULONG)msix->Pba[qwordIndex];
    } else {
        *Value = (ULONG)(msix->Pba[qwordIndex] >> 32);
    }
    
    return STATUS_SUCCESS;
}

//
// PBA 是只读的，写入被忽略
//
NTSTATUS VnvmeMsixPbaWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    UNREFERENCED_PARAMETER(Controller);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Value);
    
    // PBA 是只读的
    return STATUS_SUCCESS;
}
```

## 中断注入

### 中断注入方法

在虚拟化环境中，有几种注入中断的方法：

1. **软件模拟**：写入特定内存地址触发中断
2. **Hypervisor API**：使用 Hyper-V 或其他虚拟化 API
3. **用户模式通知**：通过事件通知用户模式代理

对于我们的仿真器，由于没有真正的硬件虚拟化，我们使用软件方法：

```c
//
// 注入 MSI-X 中断
//
NTSTATUS VnvmeInjectMsixInterrupt(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ USHORT Vector)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    PVNVME_MSIX_VECTOR vec;
    NTSTATUS status = STATUS_SUCCESS;
    
    if (Vector >= msix->VectorCount) {
        return STATUS_INVALID_PARAMETER;
    }
    
    vec = &msix->Vectors[Vector];
    
    // 检查 MSI-X 是否使能
    if (!msix->Enabled) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_MSIX,
            "MSI-X disabled, cannot inject interrupt");
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 检查全局屏蔽
    if (msix->FunctionMask) {
        vec->Pending = TRUE;
        msix->InterruptsMasked++;
        return STATUS_SUCCESS;
    }
    
    // 检查向量屏蔽
    if (vec->Masked) {
        vec->Pending = TRUE;
        msix->InterruptsMasked++;
        return STATUS_SUCCESS;
    }
    
    // 发送中断
    status = VnvmeDeliverMsixInterrupt(Controller, Vector);
    
    if (NT_SUCCESS(status)) {
        vec->Pending = FALSE;
        msix->InterruptsGenerated++;
    }
    
    return status;
}

//
// 实际发送中断
//
NTSTATUS VnvmeDeliverMsixInterrupt(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ USHORT Vector)
{
    PVNVME_MSIX_VECTOR vec = &Controller->MsixState.Vectors[Vector];
    
    //
    // 方法 1: 写入 Message Address
    // 在真实系统上，CPU 的中断控制器会监视特定地址范围
    // 对于仿真，这通常不起作用
    //
    
    //
    // 方法 2: 使用 WDF 中断对象
    // 如果我们创建了 WDFINTERRUPT，可以使用它
    //
    if (Controller->Interrupt) {
        // WdfInterruptQueueDpcForIsr 不适用于模拟中断
    }
    
    //
    // 方法 3: 使用事件通知测试应用
    // 这是最实用的测试方法
    //
    if (Controller->InterruptEvent) {
        KeSetEvent(Controller->InterruptEvent, IO_NO_INCREMENT, FALSE);
    }
    
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_MSIX,
        "Delivered MSI-X Vector %d: Addr=0x%llX, Data=0x%X",
        Vector, vec->MessageAddress, vec->MessageData);
    
    return STATUS_SUCCESS;
}
```

### 基于 HvCallSignalEvent 的中断注入

如果运行在 Hyper-V 之上，可以使用 Hypervisor 调用：

```c
//
// 使用 Hyper-V 注入中断 (需要 VBS 支持)
//
#if defined(HYPERV_INTERRUPT_INJECTION)

NTSTATUS VnvmeHvInjectInterrupt(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ USHORT Vector)
{
    HV_INPUT_SIGNAL_EVENT signalEvent;
    HV_STATUS hvStatus;
    
    PVNVME_MSIX_VECTOR vec = &Controller->MsixState.Vectors[Vector];
    
    // 设置中断参数
    RtlZeroMemory(&signalEvent, sizeof(signalEvent));
    signalEvent.ConnectionId.AsUINT32 = Controller->HvConnectionId;
    signalEvent.FlagNumber = Vector;
    
    // 调用 Hypervisor
    hvStatus = HvCallSignalEvent(&signalEvent);
    
    if (hvStatus != HV_STATUS_SUCCESS) {
        return STATUS_UNSUCCESSFUL;
    }
    
    return STATUS_SUCCESS;
}

#endif
```

## MSI-X Capability MMIO

### Message Control 寄存器处理

```c
//
// 处理 MSI-X Message Control 写入
//
VOID VnvmeMsixMessageControlWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ USHORT Value)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    BOOLEAN wasEnabled = msix->Enabled;
    BOOLEAN wasMasked = msix->FunctionMask;
    
    // 更新状态
    msix->Enabled = (Value >> 15) & 1;
    msix->FunctionMask = (Value >> 14) & 1;
    
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_MSIX,
        "MSI-X Message Control: Enable=%d, FuncMask=%d",
        msix->Enabled, msix->FunctionMask);
    
    // 如果刚使能且解除全局屏蔽，处理挂起的中断
    if (msix->Enabled && !msix->FunctionMask) {
        if (!wasEnabled || wasMasked) {
            VnvmeProcessPendingInterrupts(Controller);
        }
    }
}

//
// 处理挂起的中断
//
VOID VnvmeProcessPendingInterrupts(
    _In_ PVNVME_CONTROLLER Controller)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    
    for (USHORT i = 0; i < msix->VectorCount; i++) {
        if (msix->Vectors[i].Pending && !msix->Vectors[i].Masked) {
            VnvmeDeliverMsixInterrupt(Controller, i);
            msix->Vectors[i].Pending = FALSE;
        }
    }
}
```

## 中断合并

为了减少中断开销，实现中断合并：

```c
//
// 中断合并配置
//
typedef struct _VNVME_INTERRUPT_COALESCING {
    BOOLEAN Enabled;
    UCHAR Threshold;           // 触发中断的完成数量阈值
    UCHAR Time;               // 触发中断的时间 (100us 单位)
    
    // 运行时状态
    ULONG PendingCompletions;  // 待处理的完成数量
    LARGE_INTEGER LastInterruptTime;
    KTIMER CoalesceTimer;
    KDPC CoalesceDpc;
    
} VNVME_INTERRUPT_COALESCING, *PVNVME_INTERRUPT_COALESCING;

//
// 检查是否应该触发中断
//
BOOLEAN VnvmeShouldTriggerInterrupt(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PVNVME_COMPLETION_QUEUE CQ)
{
    PVNVME_INTERRUPT_COALESCING coal = &Controller->IntCoalescing;
    LARGE_INTEGER currentTime;
    
    if (!coal->Enabled) {
        // 合并禁用，立即触发
        return TRUE;
    }
    
    coal->PendingCompletions++;
    
    // 检查阈值
    if (coal->PendingCompletions >= coal->Threshold) {
        coal->PendingCompletions = 0;
        return TRUE;
    }
    
    // 检查时间
    KeQuerySystemTime(&currentTime);
    LONGLONG elapsed = (currentTime.QuadPart - coal->LastInterruptTime.QuadPart) / 1000;  // us
    
    if (elapsed >= (LONGLONG)coal->Time * 100) {
        coal->PendingCompletions = 0;
        coal->LastInterruptTime = currentTime;
        return TRUE;
    }
    
    // 设置定时器以确保最终触发中断
    if (!KeReadStateTimer(&coal->CoalesceTimer)) {
        LARGE_INTEGER dueTime;
        dueTime.QuadPart = -((LONGLONG)coal->Time * 100 * 10);  // 相对时间，100ns 单位
        KeSetTimer(&coal->CoalesceTimer, dueTime, &coal->CoalesceDpc);
    }
    
    return FALSE;
}

//
// 合并定时器 DPC
//
VOID VnvmeCoalesceTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Arg1,
    _In_opt_ PVOID Arg2)
{
    PVNVME_CONTROLLER Controller = Context;
    
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    
    // 触发中断
    if (Controller->IntCoalescing.PendingCompletions > 0) {
        // 向所有有待处理完成的 CQ 发送中断
        VnvmeTriggerPendingInterrupts(Controller);
        
        Controller->IntCoalescing.PendingCompletions = 0;
        KeQuerySystemTime(&Controller->IntCoalescing.LastInterruptTime);
    }
}
```

## 完整中断流程

### 命令完成时的中断处理

```c
//
// 命令完成后发送中断
//
VOID VnvmeCompleteCommandWithInterrupt(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PVNVME_COMPLETION_QUEUE CQ,
    _In_ PVNVME_SUBMISSION_QUEUE SQ,
    _In_ PNVME_COMMAND Cmd,
    _In_ NTSTATUS CommandStatus)
{
    NVME_COMPLETION completion;
    
    // 构造完成条目
    RtlZeroMemory(&completion, sizeof(completion));
    completion.SQHD = SQ->Head;
    completion.SQID = SQ->QueueId;
    completion.CID = Cmd->CDW0.CID;
    
    if (NT_SUCCESS(CommandStatus)) {
        VnvmeSetStatus(&completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    } else {
        VnvmeSetStatus(&completion, NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR, 0);
    }
    
    // 发布完成到 CQ
    VnvmePostCompletion(CQ, &completion);
    
    // 检查是否需要发送中断
    if (CQ->InterruptEnabled) {
        if (VnvmeShouldTriggerInterrupt(Controller, CQ)) {
            VnvmeInjectMsixInterrupt(Controller, CQ->Vector);
        }
    }
}
```

## 调试与追踪

```c
//
// MSI-X 调试输出
//
VOID VnvmeDumpMsixState(
    _In_ PVNVME_CONTROLLER Controller)
{
    PVNVME_MSIX_STATE msix = &Controller->MsixState;
    
    DbgPrint("VNVME: MSI-X State:\n");
    DbgPrint("  Enabled: %d\n", msix->Enabled);
    DbgPrint("  FunctionMask: %d\n", msix->FunctionMask);
    DbgPrint("  VectorCount: %d\n", msix->VectorCount);
    DbgPrint("  InterruptsGenerated: %llu\n", msix->InterruptsGenerated);
    DbgPrint("  InterruptsMasked: %llu\n", msix->InterruptsMasked);
    
    for (USHORT i = 0; i < min(msix->VectorCount, 8); i++) {
        PVNVME_MSIX_VECTOR vec = &msix->Vectors[i];
        DbgPrint("  Vector %d: Addr=0x%llX, Data=0x%X, Mask=%d, Pend=%d\n",
            i, vec->MessageAddress, vec->MessageData,
            vec->Masked, vec->Pending);
    }
}
```
