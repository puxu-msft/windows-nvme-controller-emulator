# 核心数据结构

本文档定义虚拟 NVMe 控制器仿真器的核心数据结构。

## 控制器上下文

### 主控制器结构

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
// 主控制器上下文
//
typedef struct _VNVME_CONTROLLER {
    // === 设备标识 ===
    ULONG Signature;             // 'VNVM'
    ULONG ControllerIndex;       // 控制器索引 (0-based)
    WCHAR DeviceName[64];        // 设备名称
    
    // === WDF 对象 ===
    WDFDEVICE WdfDevice;
    WDFQUEUE DefaultQueue;
    WDFINTERRUPT Interrupt;
    
    // === 设备对象 ===
    PDEVICE_OBJECT FunctionalDeviceObject;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    
    // === 控制器状态 ===
    VNVME_CONTROLLER_STATE State;
    BOOLEAN ShutdownNotification;
    
    // === NVMe 寄存器 ===
    VNVME_REGISTERS Registers;
    
    // === BAR0 内存 ===
    PVOID Bar0VirtualAddress;
    ULONG Bar0Size;
    PHYSICAL_ADDRESS Bar0PhysicalAddress;
    
    // === Admin Queue ===
    VNVME_SUBMISSION_QUEUE AdminSQ;
    VNVME_COMPLETION_QUEUE AdminCQ;
    
    // === I/O Queues ===
    LIST_ENTRY IoSqList;         // I/O Submission Queue 列表
    LIST_ENTRY IoCqList;         // I/O Completion Queue 列表
    KSPIN_LOCK QueueLock;        // 队列列表锁
    USHORT MaxIoQueues;          // 最大 I/O 队列数 (CAP.MQES)
    
    // === 命名空间 ===
    LIST_ENTRY NamespaceList;    // 命名空间列表
    KSPIN_LOCK NamespaceLock;
    ULONG NamespaceCount;
    
    // === 后端存储 ===
    PVNVME_BACKEND Backend;
    
    // === 工作线程 ===
    VNVME_QUEUE_WORKER QueueWorker;
    
    // === 中断 ===
    VNVME_INTERRUPT_STATE InterruptState;
    VNVME_INTERRUPT_COALESCING IntCoalescing;
    
    // === 电源管理 ===
    DEVICE_POWER_STATE DevicePowerState;
    SYSTEM_POWER_STATE SystemPowerState;
    
    // === 性能计数器 ===
    ULONG64 PerformanceFrequency;
    VNVME_STATISTICS Statistics;
    
    // === 配置 ===
    VNVME_CONFIG Config;
    
} VNVME_CONTROLLER, *PVNVME_CONTROLLER;

#define VNVME_CONTROLLER_SIGNATURE 'MVNV'
```

### NVMe 寄存器结构

```c
//
// NVMe 控制器寄存器 (BAR0)
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
