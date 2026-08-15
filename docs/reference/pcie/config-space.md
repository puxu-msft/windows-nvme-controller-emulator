# 虚拟 PCIe 总线仿真

> ⚠️ **架构更新说明 (v2)**
> 
> **重要**: v2 架构使用单一 `vnvme.sys` 驱动，合并了原来的 bus 和 emu 功能。
> 
> 本文档包含两种代码风格：
> 
> | 章节 | 代码风格 | 说明 |
> |------|----------|------|
> | 概述、设备结构 | WDM 概念示例 | 说明 PCIe 总线概念，使用传统命名 |
> | 动态设备管理 | WDF 实际实现 | v2 架构的实际函数签名 |
> 
> **实际代码请以 `vnvme/*.c` 源文件为准。**
> 
> 请优先参考：[../../architecture/overview.md](../../architecture/overview.md)

本文档详细说明虚拟 PCIe 总线仿真的设计和实现。

> **v2 注意**: 总线功能和 NVMe 仿真功能合并到 `vnvme.sys` 单一驱动中。

## 概述

### 为什么需要虚拟 PCIe 总线？

Windows 原生 NVMe 驱动 (stornvme.sys) 只能加载到 **PCI/PCIe 总线上枚举的设备**。要让 stornvme 驱动我们的虚拟设备，必须：

1. 创建一个虚拟的 PCIe 总线
2. 在该总线上枚举虚拟 NVMe 控制器
3. 为设备提供正确的 PCI 标识和资源

### 驱动角色 (v2 架构)

我们的虚拟设备工作原理与传统的驱动栈不同。stornvme.sys 并不"发送命令给我们"，而是：

1. **vnvme.sys 创建虚拟 PDO**，报告为 NVMe 控制器
2. **stornvme.sys 作为 FDO 加载**到这个 PDO 上
3. **stornvme 通过 MMIO 直接读写 BAR0**（我们分配的物理内存）
4. **vnvme.sys 轮询 Doorbell 寄存器**检测 stornvme 提交的命令
5. **vnvme.sys 处理命令并写入 Completion Queue**

```
┌─────────────────────────────────────────────────────────────────┐
│                        命令处理流程                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│   stornvme.sys                           vnvme.sys               │
│       │                                      │                   │
│       │ 1. 将命令写入 SQ                      │                   │
│       │    (直接写入 BAR0 映射的内存)          │                   │
│       ▼                                      │                   │
│   ┌─────────────────┐                        │                   │
│   │  BAR0 物理内存  │◄───── 轮询 Doorbell ────┤                   │
│   │  (vnvme 分配)   │       检测 SQ Tail 变化 │                   │
│   │                 │                        │                   │
│   │  [NVMe 寄存器]  │       2. 读取 SQ 命令  │                   │
│   │  [Doorbell]     │──────────────────────►│                   │
│   │  [Admin SQ/CQ]  │                        │                   │
│   │  [I/O SQ/CQ]    │       3. 处理命令      │                   │
│   │                 │       (用户态/内核态)   │                   │
│   │                 │                        │                   │
│   │                 │◄───── 4. 写入 CQ ──────┤                   │
│   └─────────────────┘       设置 Phase bit   │                   │
│       │                                      │                   │
│       │ 5. stornvme 轮询 CQ 或收到中断         │                   │
│       │    读取完成状态                        │                   │
│       ▼                                      │                   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**关键理解**：
- 我们不是 stornvme 的"下层驱动"或"Filter 驱动"
- stornvme 通过 MMIO 直接操作我们的内存，我们无法拦截
- 我们通过轮询检测 Doorbell 变化来"发现"新命令
- 这类似于真实硬件的工作方式（硬件也是检测 Doorbell 写入）

### 设备栈关系

从 Windows 驱动栈角度看：

```
                    IRP 流向 (PnP/Power)
                           │
                           ▼
┌─────────────────────────────────────────┐
│   stornvme.sys (FDO)                    │ ← Windows 加载的 NVMe 驱动
│   加载到我们创建的 PDO 上                │
└─────────────────────────────────────────┘
                           │
                           ▼ (IRP 传递)
┌─────────────────────────────────────────┐
│   vnvme.sys 创建的 PDO                  │ ← 我们响应 PnP IRP
│   (虚拟 NVMe 控制器)                    │    提供资源、配置空间
└─────────────────────────────────────────┘
```

**但 I/O 命令不走这个栈！** stornvme 的 I/O 操作是：
1. 将命令写入 SQ（直接写内存）
2. 写 Doorbell（直接写内存）
3. 轮询 CQ 或等待中断

我们通过共享内存 + 轮询来处理这些操作，而不是通过 IRP。

---

## 总线驱动设计

### 设备对象层次

```c
//
// 总线 FDO (Functional Device Object)
// - 由 vnvme.sys 创建
// - 挂载到根枚举器 (ROOT\VNVME)
//
typedef struct _BUS_FDO_EXTENSION {
    PDEVICE_OBJECT      Self;
    PDEVICE_OBJECT      Pdo;            // 底层 PDO
    PDEVICE_OBJECT      LowerDevice;    // 下层设备
    
    // 子设备列表
    LIST_ENTRY          ChildList;
    KSPIN_LOCK          ChildListLock;
    ULONG               ChildCount;
    
    // 用户态接口
    UNICODE_STRING      SymbolicLink;
    
} BUS_FDO_EXTENSION, *PBUS_FDO_EXTENSION;

//
// 子设备 PDO (Physical Device Object)
// - 代表一个虚拟 NVMe 控制器
// - 由总线驱动在收到创建请求时创建
//
typedef struct _CHILD_PDO_EXTENSION {
    LIST_ENTRY          ListEntry;
    PDEVICE_OBJECT      Self;
    PBUS_FDO_EXTENSION  BusExtension;
    
    // 设备标识
    ULONG               ControllerId;
    WCHAR               DeviceId[64];
    WCHAR               HardwareId[128];
    WCHAR               InstanceId[32];
    
    // PCIe 配置空间
    UCHAR               PciConfig[256];
    
    // BAR 资源
    PHYSICAL_ADDRESS    Bar0PhysAddr;
    SIZE_T              Bar0Size;
    PVOID               Bar0VirtAddr;
    
    // 关联的仿真上下文
    PVOID               EmulationContext;
    
    // 状态
    BOOLEAN             Present;
    BOOLEAN             Started;
    
} CHILD_PDO_EXTENSION, *PCHILD_PDO_EXTENSION;
```

---

## PCIe 配置空间仿真

### 配置空间布局

每个 PCI 设备有 256 字节的配置空间头：

```
偏移    大小    名称                 值 (示例)
────────────────────────────────────────────────────────────────
0x00    2       Vendor ID            0x1B36 (Red Hat)
0x02    2       Device ID            0x0010 (NVMe Controller)
0x04    2       Command              0x0006 (Memory + Bus Master)
0x06    2       Status               0x0010 (Capabilities List)
0x08    1       Revision ID          0x02
0x09    1       Prog IF              0x02 (NVMe)
0x0A    1       Sub Class            0x08 (Non-Volatile Memory)
0x0B    1       Base Class           0x01 (Mass Storage)
0x0C    1       Cache Line Size      0x10
0x0D    1       Latency Timer        0x00
0x0E    1       Header Type          0x00 (Single Function)
0x0F    1       BIST                 0x00

0x10    4       BAR0 (Low)           物理地址低 32 位
0x14    4       BAR1 (High)          物理地址高 32 位 (64-bit BAR)
0x18    4       BAR2                 0x00000000 (未使用)
0x1C    4       BAR3                 0x00000000
0x20    4       BAR4                 0x00000000
0x24    4       BAR5                 0x00000000

0x28    4       CardBus CIS          0x00000000
0x2C    2       Subsystem Vendor     0x1B36
0x2E    2       Subsystem ID         0x0001
0x30    4       Expansion ROM        0x00000000
0x34    1       Capabilities Ptr     0x40
0x35    3       Reserved             0x000000
0x38    4       Reserved             0x00000000
0x3C    1       Interrupt Line       0x00
0x3D    1       Interrupt Pin        0x01
0x3E    1       Min Grant            0x00
0x3F    1       Max Latency          0x00

// Capability: MSI-X (0x40)
0x40    1       Cap ID               0x11 (MSI-X)
0x41    1       Next Ptr             0x00
0x42    2       Message Control      0x0003 (4 vectors, enabled)
0x44    4       Table Offset/BIR     0x00002000 (offset 0x2000, BAR0)
0x48    4       PBA Offset/BIR       0x00003000 (offset 0x3000, BAR0)
```

### 配置空间实现

```c
//
// 初始化 PCIe 配置空间
//
VOID VnvmeInitPciConfig(
    _Inout_ PCHILD_PDO_EXTENSION PdoExt)
{
    PUCHAR config = PdoExt->PciConfig;
    
    RtlZeroMemory(config, 256);
    
    // Vendor ID / Device ID
    *(PUSHORT)(config + 0x00) = 0x1B36;  // Red Hat
    *(PUSHORT)(config + 0x02) = 0x0010;  // Virtual NVMe
    
    // Command: Memory Space + Bus Master
    *(PUSHORT)(config + 0x04) = 0x0006;
    
    // Status: Capabilities List exists
    *(PUSHORT)(config + 0x06) = 0x0010;
    
    // Class Code: NVMe Mass Storage Controller
    config[0x09] = 0x02;  // Prog IF: NVMe
    config[0x0A] = 0x08;  // Subclass: Non-Volatile Memory
    config[0x0B] = 0x01;  // Base Class: Mass Storage
    
    // Header Type: Single Function
    config[0x0E] = 0x00;
    
    // BAR0: 64-bit, Memory, Non-prefetchable
    // 低 32 位: 地址 + 类型标志
    // Bit 0 = 0 (Memory), Bit 2:1 = 10 (64-bit), Bit 3 = 0 (Non-prefetch)
    *(PULONG)(config + 0x10) = 
        (PdoExt->Bar0PhysAddr.LowPart & 0xFFFF0000) | 0x04;
    // 高 32 位
    *(PULONG)(config + 0x14) = PdoExt->Bar0PhysAddr.HighPart;
    
    // Subsystem Vendor / ID
    *(PUSHORT)(config + 0x2C) = 0x1B36;
    *(PUSHORT)(config + 0x2E) = 0x0001;
    
    // Capabilities Pointer
    config[0x34] = 0x40;
    
    // Interrupt Pin
    config[0x3D] = 0x01;
    
    // MSI-X Capability (at 0x40)
    config[0x40] = 0x11;        // Cap ID: MSI-X
    config[0x41] = 0x00;        // Next: None
    *(PUSHORT)(config + 0x42) = 0x8003;  // 4 vectors, enabled
    *(PULONG)(config + 0x44) = 0x00002000;  // Table at BAR0+0x2000
    *(PULONG)(config + 0x48) = 0x00003000;  // PBA at BAR0+0x3000
}
```

---

## BAR 空间管理

### BAR0 布局

NVMe 控制器的 BAR0 包含控制器寄存器和 Doorbell：

```
BAR0 空间 (最小 16KB, 我们分配 64KB):

0x0000 - 0x0FFF    Controller Registers (4KB)
                   ├── 0x0000  CAP (Controller Capabilities)
                   ├── 0x0008  VS (Version)
                   ├── 0x000C  INTMS (Interrupt Mask Set)
                   ├── 0x0010  INTMC (Interrupt Mask Clear)
                   ├── 0x0014  CC (Controller Configuration)
                   ├── 0x001C  CSTS (Controller Status)
                   ├── 0x0020  NSSR (NVM Subsystem Reset)
                   ├── 0x0024  AQA (Admin Queue Attributes)
                   ├── 0x0028  ASQ (Admin SQ Base Address)
                   ├── 0x0030  ACQ (Admin CQ Base Address)
                   └── ...

0x1000 - 0x1FFF    Doorbell Registers
                   ├── 0x1000  SQ0 Tail Doorbell (Admin SQ)
                   ├── 0x1004  CQ0 Head Doorbell (Admin CQ)
                   ├── 0x1008  SQ1 Tail Doorbell (I/O SQ 1)
                   ├── 0x100C  CQ1 Head Doorbell (I/O CQ 1)
                   └── ...

0x2000 - 0x2FFF    MSI-X Table (4KB)

0x3000 - 0x3FFF    MSI-X PBA (4KB)
```

### BAR 分配

```c
//
// 分配 BAR0 虚拟物理地址
//
NTSTATUS VnvmeAllocateBarSpace(
    _Inout_ PCHILD_PDO_EXTENSION PdoExt)
{
    // 分配 64KB 非分页内存
    PdoExt->Bar0Size = 64 * 1024;
    
    PdoExt->Bar0VirtAddr = ExAllocatePool2(
        POOL_FLAG_NON_PAGED | POOL_FLAG_CACHE_ALIGNED,
        PdoExt->Bar0Size,
        '0RAB');
    
    if (!PdoExt->Bar0VirtAddr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(PdoExt->Bar0VirtAddr, PdoExt->Bar0Size);
    
    // 获取物理地址
    PdoExt->Bar0PhysAddr = MmGetPhysicalAddress(PdoExt->Bar0VirtAddr);
    
    return STATUS_SUCCESS;
}
```

---

## PnP 处理

### 设备枚举

```c
//
// 处理 IRP_MN_QUERY_DEVICE_RELATIONS (BusRelations)
//
NTSTATUS VnvmeBusQueryBusRelations(
    _In_ PBUS_FDO_EXTENSION BusExt,
    _In_ PIRP Irp)
{
    PDEVICE_RELATIONS relations;
    PLIST_ENTRY entry;
    ULONG count = 0;
    ULONG i = 0;
    
    // 计算子设备数量
    KIRQL oldIrql;
    KeAcquireSpinLock(&BusExt->ChildListLock, &oldIrql);
    
    for (entry = BusExt->ChildList.Flink;
         entry != &BusExt->ChildList;
         entry = entry->Flink) {
        PCHILD_PDO_EXTENSION pdoExt = 
            CONTAINING_RECORD(entry, CHILD_PDO_EXTENSION, ListEntry);
        if (pdoExt->Present) {
            count++;
        }
    }
    
    // 分配 DEVICE_RELATIONS
    relations = ExAllocatePool2(
        POOL_FLAG_PAGED,
        sizeof(DEVICE_RELATIONS) + (count - 1) * sizeof(PDEVICE_OBJECT),
        'LERV');
    
    if (!relations) {
        KeReleaseSpinLock(&BusExt->ChildListLock, oldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    relations->Count = count;
    
    // 填充设备对象列表
    for (entry = BusExt->ChildList.Flink;
         entry != &BusExt->ChildList;
         entry = entry->Flink) {
        PCHILD_PDO_EXTENSION pdoExt = 
            CONTAINING_RECORD(entry, CHILD_PDO_EXTENSION, ListEntry);
        if (pdoExt->Present) {
            relations->Objects[i] = pdoExt->Self;
            ObReferenceObject(pdoExt->Self);
            i++;
        }
    }
    
    KeReleaseSpinLock(&BusExt->ChildListLock, oldIrql);
    
    Irp->IoStatus.Information = (ULONG_PTR)relations;
    return STATUS_SUCCESS;
}
```

### 设备标识查询

```c
//
// 处理 IRP_MN_QUERY_ID
//
NTSTATUS VnvmePdoQueryId(
    _In_ PCHILD_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PWCHAR idString = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    
    switch (stack->Parameters.QueryId.IdType) {
        
    case BusQueryDeviceID:
        // 格式: PCI\VEN_xxxx&DEV_xxxx&SUBSYS_xxxxxxxx&REV_xx
        idString = ExAllocatePool2(POOL_FLAG_PAGED, 128 * sizeof(WCHAR), 'DIVQ');
        if (idString) {
            RtlStringCchPrintfW(idString, 128,
                L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X",
                0x1B36, 0x0010,  // Vendor, Device
                0x0001, 0x1B36,  // Subsys ID, Vendor
                0x02);          // Revision
        }
        break;
        
    case BusQueryHardwareIDs:
        // 多字符串，以双 NULL 结尾
        idString = ExAllocatePool2(POOL_FLAG_PAGED, 256 * sizeof(WCHAR), 'DIHQ');
        if (idString) {
            PWCHAR p = idString;
            // 完整硬件 ID
            p += swprintf(p, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X&REV_%02X",
                          0x1B36, 0x0010, 0x0001, 0x1B36, 0x02) + 1;
            // 不含版本
            p += swprintf(p, L"PCI\\VEN_%04X&DEV_%04X&SUBSYS_%04X%04X",
                          0x1B36, 0x0010, 0x0001, 0x1B36) + 1;
            // 仅设备
            p += swprintf(p, L"PCI\\VEN_%04X&DEV_%04X",
                          0x1B36, 0x0010) + 1;
            // 类代码
            p += swprintf(p, L"PCI\\CC_010802") + 1;
            p += swprintf(p, L"PCI\\CC_0108") + 1;
            *p = L'\0';  // 双 NULL 结尾
        }
        break;
        
    case BusQueryCompatibleIDs:
        idString = ExAllocatePool2(POOL_FLAG_PAGED, 64 * sizeof(WCHAR), 'DICQ');
        if (idString) {
            PWCHAR p = idString;
            p += swprintf(p, L"PCI\\CC_010802") + 1;
            p += swprintf(p, L"PCI\\CC_0108") + 1;
            *p = L'\0';
        }
        break;
        
    case BusQueryInstanceID:
        idString = ExAllocatePool2(POOL_FLAG_PAGED, 32 * sizeof(WCHAR), 'DIIQ');
        if (idString) {
            RtlStringCchPrintfW(idString, 32, L"%d", PdoExt->ControllerId);
        }
        break;
        
    default:
        status = Irp->IoStatus.Status;
        break;
    }
    
    if (idString) {
        Irp->IoStatus.Information = (ULONG_PTR)idString;
    } else if (NT_SUCCESS(status)) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    
    return status;
}
```

### 资源需求

```c
//
// 处理 IRP_MN_QUERY_RESOURCE_REQUIREMENTS
//
NTSTATUS VnvmePdoQueryResourceRequirements(
    _In_ PCHILD_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp)
{
    PIO_RESOURCE_REQUIREMENTS_LIST reqList;
    PIO_RESOURCE_DESCRIPTOR desc;
    ULONG listSize;
    
    // 我们需要: 1个内存资源 (BAR0) + 1个中断资源 (MSI-X)
    listSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) + 
               sizeof(IO_RESOURCE_DESCRIPTOR);
    
    reqList = ExAllocatePool2(POOL_FLAG_PAGED, listSize, 'QRRV');
    if (!reqList) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(reqList, listSize);
    
    reqList->ListSize = listSize;
    reqList->InterfaceType = PCIBus;
    reqList->BusNumber = 0;
    reqList->SlotNumber = PdoExt->ControllerId;
    reqList->AlternativeLists = 1;
    reqList->List[0].Version = 1;
    reqList->List[0].Revision = 1;
    reqList->List[0].Count = 2;
    
    // 内存资源 (BAR0)
    desc = &reqList->List[0].Descriptors[0];
    desc->Type = CmResourceTypeMemory;
    desc->ShareDisposition = CmResourceShareDeviceExclusive;
    desc->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    desc->u.Memory.Length = (ULONG)PdoExt->Bar0Size;
    desc->u.Memory.Alignment = 0x10000;  // 64KB 对齐
    desc->u.Memory.MinimumAddress.QuadPart = 0;
    desc->u.Memory.MaximumAddress.QuadPart = (ULONGLONG)-1;
    
    // 中断资源 (MSI-X)
    desc = &reqList->List[0].Descriptors[1];
    desc->Type = CmResourceTypeInterrupt;
    desc->ShareDisposition = CmResourceShareDeviceExclusive;
    desc->Flags = CM_RESOURCE_INTERRUPT_LATCHED | 
                  CM_RESOURCE_INTERRUPT_MESSAGE;
    desc->u.Interrupt.MinimumVector = 0;
    desc->u.Interrupt.MaximumVector = (ULONG)-1;
    desc->u.Interrupt.AffinityPolicy = IrqPolicyMachineDefault;
    
    Irp->IoStatus.Information = (ULONG_PTR)reqList;
    return STATUS_SUCCESS;
}
```

### 资源分配报告

```c
//
// 处理 IRP_MN_QUERY_RESOURCES
// 返回设备当前使用的资源 (对于我们来说是 BAR0 物理地址)
//
NTSTATUS VnvmePdoQueryResources(
    _In_ PCHILD_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp)
{
    PCM_RESOURCE_LIST resourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc;
    ULONG listSize;
    
    // 计算所需大小: 1 个内存资源 + 1 个中断资源
    listSize = sizeof(CM_RESOURCE_LIST) + 
               sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);
    
    resourceList = ExAllocatePool2(POOL_FLAG_PAGED, listSize, 'RSRV');
    if (!resourceList) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(resourceList, listSize);
    
    resourceList->Count = 1;
    resourceList->List[0].InterfaceType = PCIBus;
    resourceList->List[0].BusNumber = 0;
    resourceList->List[0].PartialResourceList.Version = 1;
    resourceList->List[0].PartialResourceList.Revision = 1;
    resourceList->List[0].PartialResourceList.Count = 2;
    
    // BAR0 内存资源 - 关键: 报告我们分配的真实物理地址
    desc = &resourceList->List[0].PartialResourceList.PartialDescriptors[0];
    desc->Type = CmResourceTypeMemory;
    desc->ShareDisposition = CmResourceShareDeviceExclusive;
    desc->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    desc->u.Memory.Start = PdoExt->Bar0PhysAddr;  // 真实物理地址!
    desc->u.Memory.Length = (ULONG)PdoExt->Bar0Size;
    
    // 中断资源 (MSI-X 模式)
    desc = &resourceList->List[0].PartialResourceList.PartialDescriptors[1];
    desc->Type = CmResourceTypeInterrupt;
    desc->ShareDisposition = CmResourceShareDeviceExclusive;
    desc->Flags = CM_RESOURCE_INTERRUPT_LATCHED | 
                  CM_RESOURCE_INTERRUPT_MESSAGE;
    desc->u.Interrupt.Level = 0;
    desc->u.Interrupt.Vector = 0;
    desc->u.Interrupt.Affinity = (KAFFINITY)-1;
    
    Irp->IoStatus.Information = (ULONG_PTR)resourceList;
    return STATUS_SUCCESS;
}

//
// 处理 IRP_MN_START_DEVICE
// stornvme.sys 会使用我们报告的资源来映射 BAR0
//
NTSTATUS VnvmePdoStartDevice(
    _In_ PCHILD_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PCM_RESOURCE_LIST allocatedResources = 
        stack->Parameters.StartDevice.AllocatedResources;
    
    // 对于 PDO，我们不需要做太多处理
    // stornvme 会使用 AllocatedResources 中的地址调用 MmMapIoSpace
    // 我们的 BAR0 物理地址已经指向我们分配的真实内存
    
    if (allocatedResources) {
        // 可选: 记录分配的资源用于调试
        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc = 
            &allocatedResources->List[0].PartialResourceList.PartialDescriptors[0];
        
        if (desc->Type == CmResourceTypeMemory) {
            // 验证系统分配的地址与我们期望的一致
            if (desc->u.Memory.Start.QuadPart != PdoExt->Bar0PhysAddr.QuadPart) {
                // 警告: 地址不匹配，可能需要重新映射
                // 这在使用真实物理内存时通常不会发生
            }
        }
    }
    
    PdoExt->Started = TRUE;
    return STATUS_SUCCESS;
}
```

### PDO PnP IRP 分发

```c
//
// PDO 的 PnP IRP 处理入口
//
NTSTATUS VnvmePdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PCHILD_PDO_EXTENSION pdoExt = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS status;
    
    switch (stack->MinorFunction) {
    
    case IRP_MN_QUERY_ID:
        status = VnvmePdoQueryId(pdoExt, Irp);
        break;
        
    case IRP_MN_QUERY_DEVICE_TEXT:
        status = VnvmePdoQueryDeviceText(pdoExt, Irp);
        break;
        
    case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
        status = VnvmePdoQueryResourceRequirements(pdoExt, Irp);
        break;
        
    case IRP_MN_QUERY_RESOURCES:
        status = VnvmePdoQueryResources(pdoExt, Irp);
        break;
        
    case IRP_MN_START_DEVICE:
        status = VnvmePdoStartDevice(pdoExt, Irp);
        break;
        
    case IRP_MN_STOP_DEVICE:
        pdoExt->Started = FALSE;
        status = STATUS_SUCCESS;
        break;
        
    case IRP_MN_REMOVE_DEVICE:
        pdoExt->Present = FALSE;
        status = STATUS_SUCCESS;
        break;
        
    case IRP_MN_QUERY_CAPABILITIES:
        status = VnvmePdoQueryCapabilities(pdoExt, Irp);
        break;
        
    case IRP_MN_QUERY_BUS_INFORMATION:
        status = VnvmePdoQueryBusInformation(pdoExt, Irp);
        break;
        
    case IRP_MN_QUERY_INTERFACE:
        // 处理 PCI 配置空间接口请求
        status = VnvmePdoQueryInterface(pdoExt, Irp);
        break;
        
    default:
        status = Irp->IoStatus.Status;
        break;
    }
    
    Irp->IoStatus.Status = status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return status;
}

//
// 处理 IRP_MN_QUERY_CAPABILITIES
// 声明设备能力 (热插拔、电源管理等)
//
NTSTATUS VnvmePdoQueryCapabilities(
    _In_ PCHILD_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION stack = IoGetCurrentIrpStackLocation(Irp);
    PDEVICE_CAPABILITIES caps = stack->Parameters.DeviceCapabilities.Capabilities;
    
    // 基本能力
    caps->Removable = FALSE;         // 不支持热拔
    caps->EjectSupported = FALSE;
    caps->UniqueID = TRUE;           // 实例 ID 是唯一的
    caps->SilentInstall = TRUE;      // 静默安装
    caps->RawDeviceOK = FALSE;       // 需要驱动
    caps->SurpriseRemovalOK = FALSE;
    
    // 电源状态映射
    caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    caps->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
    caps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    caps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    caps->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    caps->DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    
    // 地址: 用于 PCI 设备的 Bus/Device/Function
    caps->Address = PdoExt->ControllerId;
    caps->UINumber = PdoExt->ControllerId;
    
    return STATUS_SUCCESS;
}

//
// 处理 IRP_MN_QUERY_BUS_INFORMATION
// 告诉上层驱动我们是 PCI 总线
//
NTSTATUS VnvmePdoQueryBusInformation(
    _In_ PCHILD_PDO_EXTENSION PdoExt,
    _In_ PIRP Irp)
{
    PPNP_BUS_INFORMATION busInfo;
    
    busInfo = ExAllocatePool2(POOL_FLAG_PAGED, sizeof(PNP_BUS_INFORMATION), 'IBSV');
    if (!busInfo) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 声明为 PCI 总线 - 这让 stornvme.sys 认为我们是真正的 PCI 设备
    busInfo->BusTypeGuid = GUID_BUS_TYPE_PCI;
    busInfo->LegacyBusType = PCIBus;
    busInfo->BusNumber = 0;
    
    Irp->IoStatus.Information = (ULONG_PTR)busInfo;
    return STATUS_SUCCESS;
}
```

---

## INF 文件

> **v2 注意**: 以下为概念性 INF 示例。实际项目使用 `templates/vnvme.inf`。
> 详细说明请参阅 [INF 文件指南](../../development/inf-guide.md)。

### vnvme.inf (v2 单驱动架构)

```ini
[Version]
Signature   = "$WINDOWS NT$"
Class       = System
ClassGuid   = {4D36E97D-E325-11CE-BFC1-08002BE10318}
Provider    = %ManufacturerName%
CatalogFile = vnvme.cat
DriverVer   = 12/23/2025,1.0.0.0
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13

[SourceDisksNames]
1 = %DiskName%

[SourceDisksFiles]
vnvme.sys = 1

[Manufacturer]
%ManufacturerName% = Standard,NTamd64

[Standard.NTamd64]
%Vnvme.DeviceDesc% = Vnvme_Device, ROOT\VNVME

[Vnvme_Device.NT]
CopyFiles = Vnvme_Device.CopyFiles

[Vnvme_Device.CopyFiles]
vnvme.sys

[Vnvme_Device.NT.Services]
AddService = vnvme, 0x00000002, Vnvme_Service

[Vnvme_Service]
DisplayName    = %Vnvme.ServiceDesc%
ServiceType    = 1               ; SERVICE_KERNEL_DRIVER
StartType      = 3               ; SERVICE_DEMAND_START
ErrorControl   = 1               ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\vnvme.sys

[Strings]
ManufacturerName    = "Virtual NVMe Project"
Vnvme.DeviceDesc    = "Virtual NVMe Controller"
Vnvme.ServiceDesc   = "Virtual NVMe Controller Driver"
DiskName            = "Virtual NVMe Installation Disk"
```

### 安装根设备

```powershell
# 创建根枚举设备
devcon install vnvme.inf ROOT\VNVME

# 或使用 pnputil (Windows 10+)
pnputil /add-driver vnvme.inf /install
```

---

## 动态设备管理

### 添加虚拟 NVMe 控制器

控制器创建采用分层设计：
- **高层 API**: `VnvmeCreateVirtualController()` - IOCTL 入口，参数验证
- **低层实现**: `VnvmeCreateControllerPdo()` - 实际 PDO 创建

```c
//
// 高层 API: IOCTL 入口
//
NTSTATUS VnvmeCreateVirtualController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId,
    _Out_opt_ WDFDEVICE* ChildDevice)
{
    NTSTATUS status;
    WDFDEVICE pdoDevice = NULL;
    
    // 1. 验证 ControllerId 是否已存在
    if (VnvmeFindController(FdoContext, ControllerId) != NULL) {
        return STATUS_OBJECT_NAME_COLLISION;
    }
    
    // 2. 调用低层实现创建 PDO
    status = VnvmeCreateControllerPdo(
        FdoContext->WdfDevice,
        ControllerId,
        &pdoDevice);
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 3. 添加到子设备列表
    // 4. 触发总线重新枚举
    
    if (ChildDevice) {
        *ChildDevice = pdoDevice;
    }
    
    return STATUS_SUCCESS;
}

//
// 低层实现: 实际创建 WDF PDO
//
NTSTATUS VnvmeCreateControllerPdo(
    _In_ WDFDEVICE ParentDevice,
    _In_ ULONG ControllerId,
    _Out_ WDFDEVICE* PdoDevice)
{
    NTSTATUS status;
    PWDFDEVICE_INIT deviceInit = NULL;
    WDFDEVICE pdo = NULL;
    PVNVME_PDO_CONTEXT pdoContext;
    WDF_OBJECT_ATTRIBUTES attributes;
    
    // 分配 PDO 初始化结构
    deviceInit = WdfPdoInitAllocate(ParentDevice);
    if (deviceInit == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 设置硬件 ID
    DECLARE_UNICODE_STRING_SIZE(hardwareId, 64);
    RtlUnicodeStringPrintf(&hardwareId,
        L"PCI\\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02");
    WdfPdoInitAssignDeviceID(deviceInit, &hardwareId);
    
    // 创建 PDO
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VNVME_PDO_CONTEXT);
    status = WdfDeviceCreate(&deviceInit, &attributes, &pdo);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 初始化 PDO 上下文
    pdoContext = VnvmeGetPdoContext(pdo);
    pdoContext->ControllerId = ControllerId;
    pdoContext->ParentFdo = VnvmeGetFdoContext(ParentDevice);
    
    // 分配 BAR0
    status = VnvmeAllocateBar0(pdoContext);
    if (!NT_SUCCESS(status)) {
        WdfObjectDelete(pdo);
        return status;
    }
    
    // 初始化 PCIe 配置空间
    VnvmeInitializePcieConfig(pdoContext);
    
    *PdoDevice = pdo;
    return STATUS_SUCCESS;
}
```

### 移除虚拟 NVMe 控制器

同样采用分层设计：
- **高层 API**: `VnvmeDeleteVirtualController()` - IOCTL 入口
- **低层实现**: `VnvmeDeleteControllerPdo()` - 实际 PDO 删除

```c
//
// 高层 API: IOCTL 入口
//
NTSTATUS VnvmeDeleteVirtualController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId)
{
    PVNVME_PDO_CONTEXT pdoContext;
    
    // 1. 查找控制器
    pdoContext = VnvmeFindController(FdoContext, ControllerId);
    if (pdoContext == NULL) {
        return STATUS_NOT_FOUND;
    }
    
    // 2. 调用低层删除
    return VnvmeDeleteControllerPdo(pdoContext);
}

//
// 低层实现: 实际删除 PDO
//
NTSTATUS VnvmeDeleteControllerPdo(
    _In_ PVNVME_PDO_CONTEXT PdoContext)
{
    // 1. 停止轮询定时器
    VnvmeStopPollingTimer(PdoContext);
    
    // 2. 释放 BAR0 内存
    VnvmeFreeBar0(PdoContext);
    
    // 3. 从子设备列表移除
    // 4. 通知 PnP 重新枚举
    
    return STATUS_SUCCESS;
}
```

---

## 参考资源

- [Windows 总线驱动开发](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/introduction-to-plug-and-play)
- [PCI 配置空间](https://wiki.osdev.org/PCI#Configuration_Space)
- [toaster 示例 (WDK)](https://github.com/microsoft/Windows-driver-samples/tree/main/general/toaster)
