# 核心数据结构

本文档定义虚拟 NVMe 控制器仿真器的核心数据结构。

> **设计规范 vs 实际代码**
> 
> 本文档为**设计规范**，描述完整的目标结构。
> 实际代码 [vnvme/vnvme.h](../vnvme/vnvme.h) 中部分成员尚未实现（标记为 Phase 2/3）。
> 
> **以 vnvme.h 为权威**，本文档作为设计参考。

## v2 架构概述

vnvme.sys 内部分为两层，每层有独立的上下文结构：

```
┌─────────────────────────────────────────────────────────────────────┐
│                         vnvme.sys                                   │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   ┌─────────────────────────┐     ┌─────────────────────────────┐  │
│   │   FDO 层 (总线功能)      │     │   PDO 层 (NVMe 仿真)        │  │
│   │                         │     │                             │  │
│   │   VNVME_FDO_CONTEXT     │────▶│   VNVME_PDO_CONTEXT         │  │
│   │   • 共享内存            │     │   • BAR0 内存               │  │
│   │   • 用户态通信          │     │   • NVMe 寄存器             │  │
│   │   • 子设备管理          │     │   • 队列状态                │  │
│   └─────────────────────────┘     └─────────────────────────────┘  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## FDO 层数据结构

### VNVME_FDO_CONTEXT

FDO (Functional Device Object) 上下文定义在 [vnvme/vnvme.h](../vnvme/vnvme.h)，由 `bus.c`, `ctrl_dev.c`, `shm.c` 操作。

| 字段组 | 主要成员 | 说明 |
|--------|----------|------|
| 标识 | `IsFdo`, `Signature` | 设备类型识别 |
| WDF 对象 | `Device`, `ControlDevice` | FDO 和控制设备句柄 |
| 共享内存 | `ShmKernelVirtAddr`, `ShmPhysAddr`, `ShmMdl` | 内核/用户态共享区域 |
| 子设备管理 | `ChildDeviceList`, `ChildDeviceCount` | PDO 链表 (见下方设计决策) |
| 用户态通信 | `CommandReadyEvent`, `UserReady` | vnvme-server 交互 |
| 统计 | `CommandsProcessed`, `ErrorCount` | 性能计数器 |

> **设计决策: LIST_ENTRY vs WDFCHILDLIST**
> 
> 使用 `LIST_ENTRY ChildDeviceList` 而非 `WDFCHILDLIST`：
> - `WDFCHILDLIST` 适合真实总线的动态热插拔，需要标识描述符和多个回调
> - `LIST_ENTRY` 更简单直接，适合 PDO 由 IOCTL 控制创建/删除的场景
> - vnvme 控制器数量少 (≤16)，不需要框架的重量级枚举机制

### 共享内存布局

共享内存相关结构体定义在 [include/vnvme_common.h](../include/vnvme_common.h)：

| 结构体/常量 | 说明 |
|------------|------|
| `VNVME_SHARED_MEMORY_SIZE` | 共享内存总大小 (64MB) |
| `VNVME_SHARED_MEMORY_CONTROL_BLOCK` | 控制块结构 (4KB) |
| `VNVME_SUBMISSION_RING_ENTRY` | 提交环条目 (80 字节) |
| `VNVME_COMPLETION_RING_ENTRY` | 完成环条目 (16 字节) |
| `VNVME_CONTROLLER_CONFIG` | 控制器配置 |
| `VNVME_NAMESPACE_CONFIG` | 命名空间配置 |

---

## PDO 层数据结构

### PDO 与 NVMe Controller 的关系

在本项目中，**1 个 PDO = 1 个 NVMe 控制器**。这是同一实体的两种视角：

```
┌─────────────────────────────────────────────────────────────────────┐
│                        两种视角，同一实体                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│   Windows 驱动模型视角              NVMe 规范视角                    │
│   ═══════════════════              ═════════════                    │
│                                                                      │
│   ┌─────────────────┐              ┌─────────────────┐              │
│   │      PDO        │      =       │  NVMe Controller│              │
│   │ Physical Device │              │  控制器实例      │              │
│   │     Object      │              │                 │              │
│   └─────────────────┘              └─────────────────┘              │
│          │                                  │                        │
│          ▼                                  ▼                        │
│   • 设备对象           ─────────────   • BAR0 寄存器                │
│   • PnP/Power IRP      ─────────────   • Admin Queue                │
│   • 资源报告           ─────────────   • I/O Queues                 │
│   • 硬件 ID            ─────────────   • Namespaces                 │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

| Windows 概念 | NVMe 概念 | 说明 |
|-------------|-----------|------|
| PDO | Controller | 一个 PDO 呈现为一个 NVMe 控制器 |
| PDO 的 HardwareID | PCIe VID/DID | `PCI\VEN_1B36&DEV_0010` |
| PDO 的资源 (BAR0) | Controller Registers | 64KB MMIO 区域 |
| PDO 的 PnP 状态 | Controller State | Started ↔ Ready |

**命名选择**: 使用 `VNVME_PDO_CONTEXT` 而非 `VNVME_CONTROLLER_CONTEXT` 是为了：
1. **与 FDO_CONTEXT 对称** - 代码中 FDO/PDO 一目了然
2. **明确 IRP 处理职责** - PDO 负责处理底层 PnP/Power IRP
3. **驱动开发者熟悉** - 符合 Windows 驱动的通用模式

### 多控制器场景

FDO 可以创建多个 PDO，每个 PDO 是一个独立的 NVMe 控制器：

```
FDO (ROOT\VNVME)
└── VNVME_FDO_CONTEXT
     │
     ├── PDO[0] ──── VNVME_PDO_CONTEXT ──── NVMe Controller 0
     │               • 独立的 Bar0, Registers
     │               • 独立的 AdminSQ/CQ, IoSQ/CQ
     │               • 独立的 Namespaces
     │
     ├── PDO[1] ──── VNVME_PDO_CONTEXT ──── NVMe Controller 1
     │
     └── PDO[2] ──── VNVME_PDO_CONTEXT ──── NVMe Controller 2
```

---

### VNVME_PDO_CONTEXT

PDO (Physical Device Object) 上下文，由 `pdo.c`, `pcie_config.c`, `bar0.c`, `doorbell.c`, `queue.c`, `prp.c` 操作。

```c
//
// 队列状态 (简化版本)
//
typedef struct _VNVME_QUEUE_STATE {
    PHYSICAL_ADDRESS        BaseAddress;
    ULONG                   Size;               // 队列大小 (条目数)
    volatile ULONG          Head;
    volatile ULONG          Tail;
    volatile BOOLEAN        PhaseTag;
    BOOLEAN                 Created;
} VNVME_QUEUE_STATE, *PVNVME_QUEUE_STATE;

//
// PDO 上下文 - NVMe 控制器仿真
// 文件: vnvme.h
//
// 注意: 此结构同时是：
//   1. PDO 的设备扩展 (DeviceExtension) - Windows 视角
//   2. NVMe 控制器的状态容器 - NVMe 视角
//
// 控制器状态通过直接检查寄存器位确定:
//   - Registers->CC.EN   = 0/1 (禁用/启用)
//   - Registers->CSTS.RDY = 0/1 (未就绪/就绪)
//   - Registers->CC.SHN  != 0 (关闭中)
//   - Registers->CSTS.SHST (关闭状态)
//
typedef struct _VNVME_PDO_CONTEXT {
    // === 标识 ===
    BOOLEAN                 IsFdo;              // FALSE = PDO
    ULONG                   Signature;          // 'PDOV'
    
    // === WDF 设备对象 ===
    WDFDEVICE               Device;
    WDFDEVICE               ParentFdo;
    ULONG                   ControllerId;
    
    // === BAR0 内存 (bar0.c) ===
    PVOID                   Bar0VirtAddr;       // 内核虚拟地址
    PHYSICAL_ADDRESS        Bar0PhysAddr;       // 物理地址 (报告给 stornvme)
    SIZE_T                  Bar0Size;
    PMDL                    Bar0Mdl;
    
    // === NVMe 寄存器指针 ===
    volatile PNVME_CONTROLLER_REGISTERS Registers;
    volatile PULONG         Doorbells;          // Doorbell 区域基址
    ULONG                   CachedCC;           // CC 寄存器缓存 (检测变化)
    
    // === PCIe 配置空间 ===
    PVOID                   PcieConfig;
    SIZE_T                  PcieConfigSize;
    
    // === Admin 队列 ===
    VNVME_QUEUE_STATE       AdminSq;
    VNVME_QUEUE_STATE       AdminCq;
    ULONGLONG               AdminSqBase;
    ULONG                   AdminSqSize;
    ULONGLONG               AdminCqBase;
    ULONG                   AdminCqSize;
    ULONG                   LastAdminSqTail;
    ULONG                   LastAdminCqHead;
    ULONG                   AdminCqPhase;
    
    // === I/O 队列 ===
    VNVME_QUEUE_STATE       IoSq[VNVME_MAX_IO_QUEUES];
    VNVME_QUEUE_STATE       IoCq[VNVME_MAX_IO_QUEUES];
    USHORT                  IoQueueCount;       // 当前 I/O 队列数
    USHORT                  MaxIoQueues;        // 最大 I/O 队列数 (CAP.MQES)
    
    // === Doorbell 轮询 (doorbell.c) ===
    WDFTIMER                PollingTimer;
    ULONG                   PollingIntervalUs;
    volatile BOOLEAN        PollingEnabled;
    volatile BOOLEAN        PollingActive;
    
    // === 命名空间 ===
    USHORT                  NamespaceCount;     // 活动命名空间数量
    VNVME_NAMESPACE         Namespaces[VNVME_MAX_NAMESPACES];
    
    // === 链表节点 ===
    LIST_ENTRY              ListEntry;
    
    // === 统计 ===
    volatile LONG64         AdminCommandsProcessed;
    volatile LONG64         IoCommandsProcessed;
    volatile LONG64         BytesRead;          // 读取字节数
    volatile LONG64         BytesWritten;       // 写入字节数
    
} VNVME_PDO_CONTEXT, *PVNVME_PDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_PDO_CONTEXT, VnvmeGetPdoContext)
```

### 通用上下文访问

```c
//
// 通用设备上下文头 (用于 IRP 分发路由)
// vnvme.c 中使用此结构判断设备类型
//
typedef struct _VNVME_COMMON_CONTEXT {
    BOOLEAN                 IsFdo;
    ULONG                   Signature;
} VNVME_COMMON_CONTEXT, *PVNVME_COMMON_CONTEXT;

//
// IRP 分发路由示例
//
NTSTATUS VnvmeDispatchPnp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PVNVME_COMMON_CONTEXT ctx = (PVNVME_COMMON_CONTEXT)DeviceObject->DeviceExtension;
    
    if (ctx->IsFdo) {
        return VnvmeFdoPnp((PVNVME_FDO_CONTEXT)ctx, DeviceObject, Irp);
    } else {
        return VnvmePdoPnp((PVNVME_PDO_CONTEXT)ctx, DeviceObject, Irp);
    }
}
```

---

## 队列结构

### VNVME_QUEUE

```c
//
// 通用队列结构 (SQ 和 CQ 共用)
//
typedef struct _VNVME_QUEUE {
    // === 队列标识 ===
    USHORT                  QueueId;            // 队列 ID (0 = Admin)
    BOOLEAN                 IsSubmissionQueue;  // TRUE = SQ, FALSE = CQ
    
    // === 队列内存 ===
    PHYSICAL_ADDRESS        PhysAddr;           // stornvme 分配的物理地址
    PVOID                   MappedAddr;         // 内核映射的虚拟地址
    PMDL                    Mdl;                // MDL (用于映射)
    
    // === 队列参数 ===
    USHORT                  Size;               // 队列条目数
    USHORT                  EntrySize;          // 每条目大小 (SQ=64, CQ=16)
    
    // === 队列状态 ===
    USHORT                  Head;               // 头指针
    USHORT                  Tail;               // 尾指针
    USHORT                  TailCached;         // 尾指针缓存 (检测变化)
    BOOLEAN                 Phase;              // 当前 Phase Tag (仅 CQ)
    
    // === 关联 ===
    USHORT                  CompletionQueueId;  // 关联的 CQ ID (仅 SQ)
    USHORT                  InterruptVector;    // 中断向量 (仅 CQ)
    
} VNVME_QUEUE, *PVNVME_QUEUE;
```

---

## NVMe 寄存器结构

NVMe 寄存器结构体定义在 [include/nvme_spec.h](../include/nvme_spec.h)：

| 结构体 | 偏移 | 大小 | 说明 |
|--------|------|------|------|
| `NVME_CAP` | 0x00 | 8 字节 | Controller Capabilities |
| `NVME_VS` | 0x08 | 4 字节 | Version |
| `NVME_CC` | 0x14 | 4 字节 | Controller Configuration |
| `NVME_CSTS` | 0x1C | 4 字节 | Controller Status |
| `NVME_AQA` | 0x24 | 4 字节 | Admin Queue Attributes |
| `NVME_CONTROLLER_REGISTERS` | 0x00 | 4KB | 完整寄存器布局 |
| `NVME_COMMAND` | - | 64 字节 | 通用 NVMe 命令 |
| `NVME_COMPLETION` | - | 16 字节 | 完成队列条目 |

## 命名空间

### VNVME_NAMESPACE

命名空间状态结构定义在 [vnvme/vnvme.h](../vnvme/vnvme.h)。

```c
//
// 命名空间状态 (Phase 1 简化版本)
//
// Phase 1: 仅保存容量信息，实际 I/O 由用户态处理
// Phase 2+: 添加后端偏移、LBA 格式、统计等
//
typedef struct _VNVME_NAMESPACE {
    ULONG NsId;                         // 命名空间 ID (1-based, 0=未使用)
    BOOLEAN Active;                     // 是否激活
    ULONG BlockSize;                    // 逻辑块大小 (512 或 4096)
    ULONGLONG TotalBlocks;              // 总逻辑块数
    ULONGLONG TotalBytes;               // 总字节数
} VNVME_NAMESPACE, *PVNVME_NAMESPACE;
```

> **Phase 2/3 扩展字段** (尚未实现)
> 
> | 字段 | 说明 |
> |------|------|
> | `GUID`, `Nguid`, `Eui64` | 唯一标识符 |
> | `BackendOffset` | 在后端存储中的偏移 |
> | `FormattedLbaSize`, `LbaFormats[]` | LBA 格式信息 |
> | `ReadCommands`, `WriteCommands`, etc. | 统计计数器 |

## 队列结构

### Submission Queue

```c
//
// Submission Queue
//
typedef struct _VNVME_SUBMISSION_QUEUE {
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 队列属性
    USHORT QueueId;
    USHORT Size;             // 条目数
    UCHAR Priority;          // 0=Urgent, 1=High, 2=Medium, 3=Low
    
    // 内存地址
    ULONG64 BaseAddr;        // 物理地址
    PVOID VirtAddr;          // 映射的虚拟地址
    PMDL Mdl;               // 内存描述符列表
    
    // 队列指针
    volatile USHORT Head;    // 控制器消费位置
    volatile USHORT Tail;    // 主机生产位置
    
    // 关联的 CQ
    struct _VNVME_COMPLETION_QUEUE* CQ;
    
    // 流控
    ULONG MaxOutstanding;    // 最大未完成命令数
    volatile LONG Outstanding; // 当前未完成命令数
    
    // 统计
    ULONG64 CommandsProcessed;
    ULONG64 CommandErrors;
    
    // 同步
    KSPIN_LOCK Lock;
    
} VNVME_SUBMISSION_QUEUE, *PVNVME_SUBMISSION_QUEUE;
```

### Completion Queue

```c
//
// Completion Queue
//
typedef struct _VNVME_COMPLETION_QUEUE {
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 队列属性
    USHORT QueueId;
    USHORT Size;             // 条目数
    
    // 内存地址
    ULONG64 BaseAddr;        // 物理地址
    PVOID VirtAddr;          // 映射的虚拟地址
    PMDL Mdl;               // 内存描述符列表
    
    // 队列指针
    volatile USHORT Head;    // 主机消费位置
    volatile USHORT Tail;    // 控制器生产位置
    
    // Phase Tag
    BOOLEAN Phase;
    
    // 中断配置
    BOOLEAN InterruptEnabled;
    USHORT Vector;
    
    // 关联的 SQ 列表
    LIST_ENTRY SqList;
    ULONG SqCount;
    
    // 统计
    ULONG64 CompletionsPosted;
    
    // 同步
    KSPIN_LOCK Lock;
    
} VNVME_COMPLETION_QUEUE, *PVNVME_COMPLETION_QUEUE;
```

## 后端存储

### 后端接口

```c
//
// 后端类型
//
typedef enum _VNVME_BACKEND_TYPE {
    VnvmeBackendMemory,      // 内存后端 (RAM Disk)
    VnvmeBackendFile,        // 文件后端
    VnvmeBackendVhd,         // VHD/VHDX 后端
    VnvmeBackendPhysical     // 物理磁盘后端
} VNVME_BACKEND_TYPE;

//
// 后端操作函数表
//
typedef struct _VNVME_BACKEND_OPERATIONS {
    // 初始化
    NTSTATUS (*Initialize)(
        _In_ struct _VNVME_BACKEND* Backend,
        _In_ PVOID InitParams);
    
    // 关闭
    VOID (*Shutdown)(
        _In_ struct _VNVME_BACKEND* Backend);
    
    // 读取
    NTSTATUS (*Read)(
        _In_ struct _VNVME_BACKEND* Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG Length,
        _Out_writes_bytes_(Length) PVOID Buffer);
    
    // 写入
    NTSTATUS (*Write)(
        _In_ struct _VNVME_BACKEND* Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG Length,
        _In_reads_bytes_(Length) PVOID Buffer);
    
    // 刷新
    NTSTATUS (*Flush)(
        _In_ struct _VNVME_BACKEND* Backend);
    
    // TRIM/Unmap
    NTSTATUS (*Trim)(
        _In_ struct _VNVME_BACKEND* Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG64 Length);
    
    // 获取信息
    NTSTATUS (*GetInfo)(
        _In_ struct _VNVME_BACKEND* Backend,
        _Out_ struct _VNVME_BACKEND_INFO* Info);
    
} VNVME_BACKEND_OPERATIONS, *PVNVME_BACKEND_OPERATIONS;

//
// 后端基本结构
//
typedef struct _VNVME_BACKEND {
    VNVME_BACKEND_TYPE Type;
    PVNVME_BACKEND_OPERATIONS Operations;
    ULONG64 Capacity;        // 总容量 (字节)
    ULONG SectorSize;        // 扇区大小
    BOOLEAN ReadOnly;
    PVOID PrivateData;       // 类型特定数据
} VNVME_BACKEND, *PVNVME_BACKEND;

//
// 后端信息
//
typedef struct _VNVME_BACKEND_INFO {
    VNVME_BACKEND_TYPE Type;
    ULONG64 Capacity;
    ULONG64 UsedBytes;
    ULONG SectorSize;
    BOOLEAN SupportsTrim;
    BOOLEAN SupportsFlush;
    WCHAR Path[260];
} VNVME_BACKEND_INFO, *PVNVME_BACKEND_INFO;
```

### 内存后端

```c
//
// 内存后端私有数据
//
typedef struct _VNVME_MEMORY_BACKEND {
    PVOID Memory;
    ULONG64 Size;
    PMDL Mdl;
} VNVME_MEMORY_BACKEND, *PVNVME_MEMORY_BACKEND;

//
// 内存后端操作实现
//
static VNVME_BACKEND_OPERATIONS VnvmeMemoryBackendOps = {
    .Initialize = VnvmeMemoryBackendInit,
    .Shutdown   = VnvmeMemoryBackendShutdown,
    .Read       = VnvmeMemoryBackendRead,
    .Write      = VnvmeMemoryBackendWrite,
    .Flush      = VnvmeMemoryBackendFlush,
    .Trim       = VnvmeMemoryBackendTrim,
    .GetInfo    = VnvmeMemoryBackendGetInfo
};
```

### 文件后端

```c
//
// 文件后端私有数据
//
typedef struct _VNVME_FILE_BACKEND {
    HANDLE FileHandle;
    PFILE_OBJECT FileObject;
    UNICODE_STRING FilePath;
    ULONG64 FileSize;
    BOOLEAN SparseFile;
} VNVME_FILE_BACKEND, *PVNVME_FILE_BACKEND;

//
// 文件后端操作实现
//
static VNVME_BACKEND_OPERATIONS VnvmeFileBackendOps = {
    .Initialize = VnvmeFileBackendInit,
    .Shutdown   = VnvmeFileBackendShutdown,
    .Read       = VnvmeFileBackendRead,
    .Write      = VnvmeFileBackendWrite,
    .Flush      = VnvmeFileBackendFlush,
    .Trim       = VnvmeFileBackendTrim,
    .GetInfo    = VnvmeFileBackendGetInfo
};
```

## 中断状态

```c
//
// 中断类型
//
typedef enum _VNVME_INTERRUPT_TYPE {
    VnvmeInterruptPin,       // INTx
    VnvmeInterruptMsi,       // MSI
    VnvmeInterruptMsix       // MSI-X
} VNVME_INTERRUPT_TYPE;

//
// MSI-X 向量
//
typedef struct _VNVME_MSIX_VECTOR {
    ULONG64 MessageAddress;
    ULONG MessageData;
    BOOLEAN Masked;
    BOOLEAN Pending;
} VNVME_MSIX_VECTOR, *PVNVME_MSIX_VECTOR;

//
// 中断状态
//
typedef struct _VNVME_INTERRUPT_STATE {
    VNVME_INTERRUPT_TYPE Type;
    
    // MSI-X 配置
    ULONG MsixVectorCount;
    VNVME_MSIX_VECTOR MsixVectors[64];  // 最多 64 个向量
    
    // MSI-X Table BAR
    PVOID MsixTableVa;
    ULONG MsixTableSize;
    
    // 中断掩码
    ULONG InterruptMask;
    
} VNVME_INTERRUPT_STATE, *PVNVME_INTERRUPT_STATE;
```

## 统计信息

```c
//
// 控制器统计
//
typedef struct _VNVME_STATISTICS {
    // 命令计数
    ULONG64 AdminCommandsReceived;
    ULONG64 AdminCommandsCompleted;
    ULONG64 IoCommandsReceived;
    ULONG64 IoCommandsCompleted;
    
    // 数据传输
    ULONG64 TotalReadBytes;
    ULONG64 TotalWriteBytes;
    ULONG64 TotalReadCommands;
    ULONG64 TotalWriteCommands;
    
    // 错误计数
    ULONG64 CommandErrors;
    ULONG64 MediaErrors;
    ULONG64 InternalErrors;
    
    // 中断
    ULONG64 InterruptsGenerated;
    
    // 时间戳
    LARGE_INTEGER StartTime;
    LARGE_INTEGER LastCommandTime;
    
} VNVME_STATISTICS, *PVNVME_STATISTICS;
```

## 配置结构

```c
//
// 控制器配置
//
typedef struct _VNVME_CONFIG {
    // 设备 ID
    USHORT VendorId;
    USHORT DeviceId;
    USHORT SubsystemVendorId;
    USHORT SubsystemDeviceId;
    UCHAR RevisionId;
    
    // 容量配置
    ULONG64 TotalCapacityBytes;
    ULONG BlockSize;
    
    // 队列配置
    USHORT MaxQueueEntries;     // CAP.MQES
    USHORT MaxIoQueues;
    
    // 性能配置
    ULONG MaxTransferSize;      // 最大传输大小 (字节)
    BOOLEAN EnableCoalescing;   // 中断合并
    ULONG CoalesceThreshold;    // 合并阈值
    ULONG CoalesceTimeUs;       // 合并时间 (微秒)
    
    // 后端配置
    VNVME_BACKEND_TYPE BackendType;
    WCHAR BackendPath[260];
    BOOLEAN BackendReadOnly;
    
    // 特性开关
    BOOLEAN EnableTrim;
    BOOLEAN EnableFlush;
    BOOLEAN EnableVolatileWriteCache;
    
    // 调试
    ULONG DebugLevel;
    
} VNVME_CONFIG, *PVNVME_CONFIG;
```

## 工作项

```c
//
// 命令工作项
//
typedef struct _VNVME_COMMAND_WORK_ITEM {
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 关联的控制器
    PVNVME_CONTROLLER Controller;
    
    // 关联的 SQ
    PVNVME_SUBMISSION_QUEUE SQ;
    
    // 命令副本
    NVME_COMMAND Command;
    
    // 完成回调
    PIO_WORKITEM WorkItem;
    
    // 状态
    NTSTATUS Status;
    
} VNVME_COMMAND_WORK_ITEM, *PVNVME_COMMAND_WORK_ITEM;
```

## 内存管理

```c
//
// 物理内存映射条目
//
typedef struct _VNVME_MEMORY_MAPPING {
    LIST_ENTRY ListEntry;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID VirtualAddress;
    SIZE_T Size;
    PMDL Mdl;
} VNVME_MEMORY_MAPPING, *PVNVME_MEMORY_MAPPING;

//
// 内存池标签
//
#define VNVME_POOL_TAG_GENERAL   'VNVM'  // 通用
#define VNVME_POOL_TAG_QUEUE     'QVNM'  // 队列
#define VNVME_POOL_TAG_NAMESPACE 'NVNM'  // 命名空间
#define VNVME_POOL_TAG_BACKEND   'BVNM'  // 后端
#define VNVME_POOL_TAG_COMMAND   'CVNM'  // 命令
#define VNVME_POOL_TAG_READ      'RVNM'  // 读取数据
#define VNVME_POOL_TAG_WRITE     'WVNM'  // 写入数据
```

## 辅助宏

```c
//
// 结构访问宏
//
#define VNVME_GET_CONTROLLER(WdfDevice) \
    ((PVNVME_CONTROLLER)WdfObjectGetTypedContext(WdfDevice, VNVME_CONTROLLER))

#define VNVME_IS_VALID_CONTROLLER(Ctrl) \
    ((Ctrl) && (Ctrl)->Signature == VNVME_CONTROLLER_SIGNATURE)

//
// 队列操作宏
//
#define VNVME_SQ_IS_EMPTY(sq)  ((sq)->Head == (sq)->Tail)
#define VNVME_SQ_IS_FULL(sq)   (((sq)->Tail + 1) % (sq)->Size == (sq)->Head)
#define VNVME_SQ_COUNT(sq)     \
    (((sq)->Tail >= (sq)->Head) ? \
        ((sq)->Tail - (sq)->Head) : \
        ((sq)->Size - (sq)->Head + (sq)->Tail))

#define VNVME_CQ_IS_EMPTY(cq)  ((cq)->Head == (cq)->Tail)
#define VNVME_CQ_IS_FULL(cq)   (((cq)->Tail + 1) % (cq)->Size == (cq)->Head)

//
// 字节/块转换
//
#define VNVME_BYTES_TO_BLOCKS(bytes, blockSize)  ((bytes) / (blockSize))
#define VNVME_BLOCKS_TO_BYTES(blocks, blockSize) ((blocks) * (blockSize))

//
// 对齐宏
//
#define VNVME_ALIGN_DOWN(val, align)  ((val) & ~((align) - 1))
#define VNVME_ALIGN_UP(val, align)    (((val) + (align) - 1) & ~((align) - 1))
#define VNVME_IS_ALIGNED(val, align)  (((val) & ((align) - 1)) == 0)
```

## 初始化示例

```c
//
// 初始化控制器上下文
//
NTSTATUS VnvmeInitializeController(
    _In_ WDFDEVICE WdfDevice,
    _In_ PVNVME_CONFIG Config)
{
    PVNVME_CONTROLLER controller;
    NTSTATUS status;
    
    controller = VNVME_GET_CONTROLLER(WdfDevice);
    
    RtlZeroMemory(controller, sizeof(*controller));
    
    // 设置签名
    controller->Signature = VNVME_CONTROLLER_SIGNATURE;
    controller->WdfDevice = WdfDevice;
    
    // 初始化状态
    controller->State = VnvmeStateNotInitialized;
    
    // 复制配置
    RtlCopyMemory(&controller->Config, Config, sizeof(*Config));
    
    // 初始化队列列表
    InitializeListHead(&controller->IoSqList);
    InitializeListHead(&controller->IoCqList);
    InitializeListHead(&controller->NamespaceList);
    
    KeInitializeSpinLock(&controller->QueueLock);
    KeInitializeSpinLock(&controller->NamespaceLock);
    
    // 初始化 CAP 寄存器
    controller->Registers.CAP.MQES = Config->MaxQueueEntries - 1;
    controller->Registers.CAP.CQR = 1;     // 需要连续队列
    controller->Registers.CAP.TO = 40;     // 20 秒超时
    controller->Registers.CAP.DSTRD = 0;   // 4 字节门铃步长
    controller->Registers.CAP.CSS = 0x01;  // 支持 NVM 命令集
    controller->Registers.CAP.MPSMIN = 0;  // 4KB 最小页
    controller->Registers.CAP.MPSMAX = 0;  // 4KB 最大页
    
    // 初始化 VS 寄存器 (NVMe 1.4)
    controller->Registers.VS.MJR = 1;
    controller->Registers.VS.MNR = 4;
    controller->Registers.VS.TER = 0;
    
    // 初始化后端
    status = VnvmeInitializeBackend(controller);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 创建命名空间
    status = VnvmeCreateNamespace(controller, 1, Config->TotalCapacityBytes);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 启动工作线程
    status = VnvmeStartQueueWorker(controller);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    controller->State = VnvmeStateDisabled;
    
    return STATUS_SUCCESS;
}
```
