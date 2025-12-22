# 核心数据结构

本文档定义虚拟 NVMe 控制器仿真器的核心数据结构。

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

FDO (Functional Device Object) 上下文，由 `fdo.c`, `bus.c`, `user_comm.c`, `shared_memory.c` 操作。

```c
//
// FDO 上下文 - 总线功能
// 文件: vnvme.h
//
typedef struct _VNVME_FDO_CONTEXT {
    // === 标识 ===
    BOOLEAN                 IsFdo;              // TRUE = FDO, FALSE = PDO
    ULONG                   Signature;          // 'FDOV' (0x564F4446)
    
    // === WDF 对象 ===
    WDFDEVICE               WdfDevice;          // FDO 设备对象
    WDFCHILDLIST            ChildList;          // PDO 子设备列表
    
    // === 控制设备 (user_comm.c) ===
    WDFDEVICE               ControlDevice;      // \\.\VNVMEControl
    WDFQUEUE                ControlQueue;       // 控制设备 IOCTL 队列
    
    // === 用户态通信 (user_comm.c) ===
    BOOLEAN                 UserModeReady;      // vnvme-server 已连接
    LARGE_INTEGER           LastHeartbeat;      // 最后心跳时间 (KeQuerySystemTime)
    KEVENT                  CommandReadyEvent;  // 通知用户态有新命令
    HANDLE                  UserEventHandle;    // 用户态事件句柄 (用于 IOCTL 返回)
    
    // === 共享内存 (shared_memory.c) ===
    PVOID                   SharedMemoryKernel; // 内核虚拟地址
    PVOID                   SharedMemoryUser;   // 用户态映射地址 (映射后填充)
    PHYSICAL_ADDRESS        SharedMemoryPhys;   // 物理地址
    SIZE_T                  SharedMemorySize;   // 64MB (VNVME_SHARED_MEMORY_SIZE)
    PMDL                    SharedMemoryMdl;    // MDL for 用户态映射
    PVNVME_SHARED_CONTROL_BLOCK ControlBlock;   // 指向共享内存开头
    
    // === 子设备管理 (bus.c) ===
    ULONG                   ControllerCount;    // 当前控制器数量
    ULONG                   MaxControllers;     // 最大控制器数量
    KSPIN_LOCK              ChildListLock;
    LIST_ENTRY              PdoList;            // PDO 链表 (备用，用于遍历)
    
} VNVME_FDO_CONTEXT, *PVNVME_FDO_CONTEXT;

#define VNVME_FDO_SIGNATURE 'FDOV'

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_FDO_CONTEXT, VnvmeGetFdoContext)
```

### 共享内存布局

```c
//
// 共享内存常量
//
#define VNVME_SHARED_MEMORY_SIZE        (64 * 1024 * 1024)  // 64MB
#define VNVME_CONTROL_BLOCK_SIZE        (4 * 1024)          // 4KB
#define VNVME_COMMAND_RING_SIZE         (1 * 1024 * 1024)   // 1MB
#define VNVME_COMPLETION_RING_SIZE      (256 * 1024)        // 256KB
#define VNVME_DATA_BUFFER_SIZE          (62 * 1024 * 1024)  // ~62MB

//
// 共享内存控制块 (位于共享内存开头)
//
typedef struct _VNVME_SHARED_CONTROL_BLOCK {
    // === 魔数和版本 ===
    ULONG                   Magic;              // 'VNME' (0x454D4E56)
    ULONG                   Version;            // 协议版本
    
    // === 状态 ===
    volatile LONG           KernelReady;        // 内核已准备
    volatile LONG           UserReady;          // 用户态已准备
    volatile LONG           Shutdown;           // 关闭标志
    
    // === 心跳 ===
    volatile ULONG64        KernelHeartbeat;    // 内核心跳计数
    volatile ULONG64        UserHeartbeat;      // 用户态心跳计数
    
    // === 命令环形缓冲区指针 ===
    volatile ULONG          CommandHead;        // 内核写入位置
    volatile ULONG          CommandTail;        // 用户态读取位置
    ULONG                   CommandRingOffset;  // 命令环偏移 (从共享内存开头)
    ULONG                   CommandRingSize;    // 命令环大小
    ULONG                   CommandEntrySize;   // 每条命令大小
    
    // === 完成环形缓冲区指针 ===
    volatile ULONG          CompletionHead;     // 用户态写入位置
    volatile ULONG          CompletionTail;     // 内核读取位置
    ULONG                   CompletionRingOffset;
    ULONG                   CompletionRingSize;
    ULONG                   CompletionEntrySize;
    
    // === 数据缓冲区 ===
    ULONG                   DataBufferOffset;   // 数据区偏移
    ULONG                   DataBufferSize;     // 数据区大小
    
    // === 统计 ===
    volatile ULONG64        CommandsSubmitted;
    volatile ULONG64        CommandsCompleted;
    volatile ULONG64        BytesRead;
    volatile ULONG64        BytesWritten;
    
    // === 填充到 4KB ===
    UCHAR                   Reserved[4096 - 128];
    
} VNVME_SHARED_CONTROL_BLOCK, *PVNVME_SHARED_CONTROL_BLOCK;

C_ASSERT(sizeof(VNVME_SHARED_CONTROL_BLOCK) == 4096);
```

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
// 控制器状态
//
typedef enum _VNVME_CONTROLLER_STATE {
    VnvmeStateNotInitialized = 0,
    VnvmeStateDisabled,          // CC.EN = 0
    VnvmeStateEnabled,           // CC.EN = 1, 等待 Ready
    VnvmeStateReady,             // CSTS.RDY = 1
    VnvmeStateShuttingDown,      // CC.SHN 设置
    VnvmeStateShutdownComplete,  // CSTS.SHST = 10b
    VnvmeStateFailed             // 致命错误
} VNVME_CONTROLLER_STATE;

//
// PDO 上下文 - NVMe 控制器仿真
// 文件: vnvme.h
//
// 注意: 此结构同时是：
//   1. PDO 的设备扩展 (DeviceExtension) - Windows 视角
//   2. NVMe 控制器的状态容器 - NVMe 视角
//
typedef struct _VNVME_PDO_CONTEXT {
    // === 标识 ===
    BOOLEAN                 IsFdo;              // FALSE = 这是 PDO
    ULONG                   Signature;          // 'PDOV' (0x564F4450)
    ULONG                   ControllerIndex;    // 控制器索引 (0, 1, 2...)
    
    // === 父 FDO 引用 ===
    PVNVME_FDO_CONTEXT      ParentFdo;          // 访问共享内存和用户态通信
    LIST_ENTRY              PdoListEntry;       // 链入 FDO 的 PdoList
    
    // === 设备对象 (pdo.c) ===
    PDEVICE_OBJECT          PhysicalDeviceObject;
    PDEVICE_OBJECT          AttachedDevice;     // stornvme 附加在此
    BOOLEAN                 Present;            // 设备是否存在
    BOOLEAN                 ReportedMissing;    // 已报告移除
    
    // === BAR0 内存 (bar0.c) ===
    PVOID                   Bar0VirtAddr;       // 内核虚拟地址
    PHYSICAL_ADDRESS        Bar0PhysAddr;       // 物理地址 (报告给 stornvme)
    ULONG                   Bar0Size;           // 64KB
    
    // === NVMe 寄存器 (指向 BAR0 内部) ===
    volatile PNVME_REGISTERS  Registers;        // 寄存器基地址 (BAR0 + 0x0000)
    volatile PULONG           Doorbells;        // Doorbell 基地址 (BAR0 + 0x1000)
    
    // === 控制器状态 ===
    VNVME_CONTROLLER_STATE  State;
    ULONG                   CachedCC;           // CC 寄存器缓存 (检测变化)
    
    // === Admin Queue (queue.c) ===
    VNVME_QUEUE             AdminSQ;
    VNVME_QUEUE             AdminCQ;
    USHORT                  AdminSQTailCached;  // 检测新命令
    USHORT                  AdminCQHeadCached;
    
    // === I/O Queues (queue.c) ===
    VNVME_QUEUE             IoSQ[VNVME_MAX_IO_QUEUES];
    VNVME_QUEUE             IoCQ[VNVME_MAX_IO_QUEUES];
    USHORT                  IoQueueCount;       // 当前 I/O 队列数
    USHORT                  MaxIoQueues;        // 最大 I/O 队列数
    
    // === Doorbell 轮询 (doorbell.c) ===
    WDFTIMER                PollTimer;
    ULONG                   PollIntervalUs;     // 当前轮询间隔 (自适应)
    ULONG                   MinPollIntervalUs;  // 最小 10μs
    ULONG                   MaxPollIntervalUs;  // 最大 1000μs
    BOOLEAN                 PollingActive;
    
    // === 命名空间 ===
    VNVME_NAMESPACE         Namespaces[VNVME_MAX_NAMESPACES];
    ULONG                   NamespaceCount;
    
    // === 性能统计 ===
    ULONG64                 CommandsProcessed;
    ULONG64                 BytesRead;
    ULONG64                 BytesWritten;
    
} VNVME_PDO_CONTEXT, *PVNVME_PDO_CONTEXT;

#define VNVME_PDO_SIGNATURE 'PDOV'
#define VNVME_MAX_IO_QUEUES     64
#define VNVME_MAX_NAMESPACES    16

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

### VNVME_REGISTERS

```c
//
// NVMe 控制器寄存器 (BAR0 偏移 0x0000-0x0FFF)
//
typedef struct _VNVME_REGISTERS {
    // 0x00: Controller Capabilities
    NVME_CAP CAP;
    
    // 0x08: Version
    NVME_VS VS;
    
    // 0x0C: Interrupt Mask Set
    ULONG INTMS;
    
    // 0x10: Interrupt Mask Clear
    ULONG INTMC;
    
    // 0x14: Controller Configuration
    NVME_CC CC;
    
    // 0x18: Reserved
    ULONG Reserved1;
    
    // 0x1C: Controller Status
    NVME_CSTS CSTS;
    
    // 0x20: NVM Subsystem Reset
    ULONG NSSR;
    
    // 0x24: Admin Queue Attributes
    NVME_AQA AQA;
    
    // 0x28: Admin Submission Queue Base Address
    ULONG64 ASQ;
    
    // 0x30: Admin Completion Queue Base Address
    ULONG64 ACQ;
    
    // 0x38: Controller Memory Buffer Location
    ULONG CMBLOC;
    
    // 0x3C: Controller Memory Buffer Size
    ULONG CMBSZ;
    
} VNVME_REGISTERS, *PVNVME_REGISTERS;
```

### 寄存器位定义

```c
//
// CAP - Controller Capabilities (64-bit)
//
typedef union _NVME_CAP {
    struct {
        ULONG64 MQES   : 16;  // Maximum Queue Entries Supported (0-based)
        ULONG64 CQR    : 1;   // Contiguous Queues Required
        ULONG64 AMS    : 2;   // Arbitration Mechanism Supported
        ULONG64 Rsvd1  : 5;
        ULONG64 TO     : 8;   // Timeout (in 500ms units)
        ULONG64 DSTRD  : 4;   // Doorbell Stride (2^(2+DSTRD) bytes)
        ULONG64 NSSRS  : 1;   // NVM Subsystem Reset Supported
        ULONG64 CSS    : 8;   // Command Sets Supported
        ULONG64 BPS    : 1;   // Boot Partition Support
        ULONG64 CPS    : 2;   // Controller Power Scope
        ULONG64 MPSMIN : 4;   // Memory Page Size Minimum (2^(12+MPSMIN))
        ULONG64 MPSMAX : 4;   // Memory Page Size Maximum
        ULONG64 PMRS   : 1;   // Persistent Memory Region Supported
        ULONG64 CMBS   : 1;   // Controller Memory Buffer Supported
        ULONG64 NSSS   : 1;   // NVM Subsystem Shutdown Supported
        ULONG64 CRMS   : 2;   // Controller Ready Modes Supported
        ULONG64 Rsvd2  : 3;
    };
    ULONG64 AsUlong64;
} NVME_CAP;

//
// VS - Version (32-bit)
//
typedef union _NVME_VS {
    struct {
        ULONG TER : 8;   // Tertiary Version
        ULONG MNR : 8;   // Minor Version
        ULONG MJR : 16;  // Major Version
    };
    ULONG AsUlong;
} NVME_VS;

//
// CC - Controller Configuration (32-bit)
//
typedef union _NVME_CC {
    struct {
        ULONG EN     : 1;   // Enable
        ULONG Rsvd1  : 3;
        ULONG CSS    : 3;   // I/O Command Set Selected
        ULONG MPS    : 4;   // Memory Page Size (2^(12+MPS))
        ULONG AMS    : 3;   // Arbitration Mechanism Selected
        ULONG SHN    : 2;   // Shutdown Notification
        ULONG IOSQES : 4;   // I/O Submission Queue Entry Size (2^n)
        ULONG IOCQES : 4;   // I/O Completion Queue Entry Size (2^n)
        ULONG CRIME  : 1;   // Controller Ready Independent of Media Enable
        ULONG Rsvd2  : 7;
    };
    ULONG AsUlong;
} NVME_CC;

//
// CSTS - Controller Status (32-bit)
//
typedef union _NVME_CSTS {
    struct {
        ULONG RDY   : 1;   // Ready
        ULONG CFS   : 1;   // Controller Fatal Status
        ULONG SHST  : 2;   // Shutdown Status
        ULONG NSSRO : 1;   // NVM Subsystem Reset Occurred
        ULONG PP    : 1;   // Processing Paused
        ULONG ST    : 1;   // Shutdown Type
        ULONG Rsvd  : 25;
    };
    ULONG AsUlong;
} NVME_CSTS;

//
// AQA - Admin Queue Attributes (32-bit)
//
typedef union _NVME_AQA {
    struct {
        ULONG ASQS  : 12;  // Admin Submission Queue Size (0-based)
        ULONG Rsvd1 : 4;
        ULONG ACQS  : 12;  // Admin Completion Queue Size (0-based)
        ULONG Rsvd2 : 4;
    };
    ULONG AsUlong;
} NVME_AQA;
```

## 命名空间

### 命名空间结构

```c
//
// 命名空间
//
typedef struct _VNVME_NAMESPACE {
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 命名空间 ID (1-based)
    ULONG NsId;
    
    // 唯一标识
    GUID Guid;
    UCHAR Nguid[16];
    UCHAR Eui64[8];
    
    // 容量信息
    ULONG64 TotalBlocks;     // 总逻辑块数
    ULONG BlockSize;         // 逻辑块大小 (512 或 4096)
    ULONG64 TotalBytes;      // 总字节数
    
    // 后端偏移
    ULONG64 BackendOffset;   // 在后端存储中的偏移
    
    // 状态
    BOOLEAN Active;
    BOOLEAN ReadOnly;
    BOOLEAN ThinProvisioned;
    
    // 格式信息
    UCHAR FormattedLbaSize;  // FLBAS
    UCHAR NumberOfLbaFormats;// NLBAF
    
    // LBA 格式数组
    NVME_LBAF LbaFormats[64];
    
    // 特性
    UCHAR NsFeatures;        // NSFEAT
    
    // 统计
    ULONG64 ReadCommands;
    ULONG64 WriteCommands;
    ULONG64 ReadBytes;
    ULONG64 WriteBytes;
    
} VNVME_NAMESPACE, *PVNVME_NAMESPACE;

//
// LBA 格式
//
typedef struct _NVME_LBAF {
    USHORT MetadataSize;     // MS: Metadata Size
    UCHAR LbaDataSize;       // LBADS: LBA Data Size (2^n bytes)
    UCHAR RelativePerformance; // RP: 00b-Best, 01b-Better, 10b-Good, 11b-Degraded
} NVME_LBAF, *PNVME_LBAF;
```

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
