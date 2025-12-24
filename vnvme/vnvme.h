/**
 * @file vnvme.h
 * @brief VNVME 内核驱动主头文件
 * 
 * 定义驱动内部使用的数据结构和函数声明。
 */

#ifndef _VNVME_H_
#define _VNVME_H_

#include <ntddk.h>
#include <wdf.h>
#include <initguid.h>
#include <wdmguid.h>
#include "vnvme_common.h"
#include "vnvme_ioctl.h"
#include "nvme_spec.h"
#include "vnvme_utils.h"
#include "debug.h"
#include "trace.h"

//===========================================================================
// 驱动标识
//===========================================================================

#define VNVME_POOL_TAG              'MVNV'  // VNVM
#define VNVME_MAX_CONTROLLERS       16
// VNVME_MAX_NAMESPACES 已在 vnvme_common.h 中定义
#define VNVME_MAX_AER_COMMANDS      4               // 最大待处理 AER 命令数
#define VNVME_BAR0_SIZE             (64 * 1024)     // 64 KB
#define VNVME_PCIE_CONFIG_SIZE      4096            // 4 KB PCIe配置空间
// VNVME_MAX_IO_QUEUES 已在 vnvme_common.h 中定义
#define VNVME_POLLING_INTERVAL_MS   1               // 轮询间隔 (毫秒)

// 签名常量
#define VNVME_FDO_SIGNATURE         'FDOV'
#define VNVME_PDO_SIGNATURE         'PDOV'

// Doorbell 偏移计算
#define NVME_DOORBELL_OFFSET(qid, dstrd) \
    (0x1000 + ((2 * (qid)) * (4 << (dstrd))))

//===========================================================================
// 命令处理模式配置
//===========================================================================

/**
 * 命令处理模式:
 * - VNVME_CMD_MODE_KERNEL: 在内核中处理命令 (低延迟，但功能受限)
 * - VNVME_CMD_MODE_USER:   转发到用户态处理 (灵活，推荐)
 * 
 * 默认使用用户态模式，可通过注册表或编译时宏切换
 */
typedef enum _VNVME_COMMAND_MODE {
    VNVME_CMD_MODE_USER   = 0,          // 用户态处理 (默认)
    VNVME_CMD_MODE_KERNEL = 1           // 内核处理 (备选)
} VNVME_COMMAND_MODE;

// 编译时默认模式 (0=用户态, 1=内核)
#ifndef VNVME_DEFAULT_CMD_MODE
#define VNVME_DEFAULT_CMD_MODE      VNVME_CMD_MODE_USER
#endif

//===========================================================================
// FDO 上下文 (虚拟总线)
//===========================================================================

typedef struct _VNVME_FDO_CONTEXT {
    // 标识
    BOOLEAN IsFdo;                      // TRUE = FDO, FALSE = PDO
    ULONG Signature;                    // 'FDOV'
    
    // WDF 设备对象
    WDFDEVICE Device;
    WDFDEVICE ControlDevice;
    WDFQUEUE ControlQueue;              // 控制设备 I/O 队列 (用于优雅关闭)
    
    // 命令处理模式
    VNVME_COMMAND_MODE CommandMode;     // 用户态或内核处理
    
    // 共享内存
    PVOID ShmKernelVirtAddr;            // 内核虚拟地址
    PHYSICAL_ADDRESS ShmPhysAddr;       // 物理地址
    SIZE_T ShmSize;
    PMDL ShmMdl;
    PVOID ShmUserVirtAddr;              // 用户态虚拟地址
    
    // 子设备管理
    LIST_ENTRY ChildDeviceList;
    KSPIN_LOCK ChildDeviceListLock;
    ULONG ChildDeviceCount;             // 当前控制器数量
    ULONG NextControllerId;
    ULONG MaxControllers;               // 最大控制器数量
    
    // 用户态通信
    KEVENT CommandReadyEvent;           // 通知用户态有新命令就绪
    KEVENT UserReadyEvent;              // 用户态服务就绪事件
    KEVENT ShutdownEvent;               // 通知用户态关闭
    volatile BOOLEAN UserReady;         // 用户态服务是否就绪
    volatile BOOLEAN ShutdownRequested; // 关闭请求标志
    volatile BOOLEAN UserCrashed;       // 用户态服务崩溃标志
    ULONG UserPid;
    LARGE_INTEGER LastHeartbeat;
    HANDLE UserEventHandle;             // 用户态可等待事件句柄
    BOOLEAN EventNotificationEnabled;   // 事件通知是否启用
    
    // 统计
    volatile LONG64 CommandsProcessed;
    volatile LONG64 ErrorCount;
    LARGE_INTEGER StartTime;            // 驱动启动时间
    
} VNVME_FDO_CONTEXT, *PVNVME_FDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_FDO_CONTEXT, VnvmeGetFdoContext)

//===========================================================================
// 存储后端类型前向声明
//===========================================================================

typedef enum _VNVME_STORAGE_TYPE {
    VNVME_STORAGE_TYPE_NONE = 0,
    VNVME_STORAGE_TYPE_MEMORY,          // 内存后端 (非分页池)
    VNVME_STORAGE_TYPE_FILE,            // 文件后端
    VNVME_STORAGE_TYPE_SPARSE           // 稀疏内存后端 (TODO Phase 5)
} VNVME_STORAGE_TYPE;

typedef struct _VNVME_STORAGE_CONTEXT VNVME_STORAGE_CONTEXT, *PVNVME_STORAGE_CONTEXT;

//===========================================================================
// NVMe LBA Format 定义 (NVMe 规范)
//===========================================================================

/**
 * @brief LBA Format 数据结构 (NVMe Spec 1.4+)
 * 
 * 每个 LBA Format 描述一种支持的 LBA 大小和元数据配置。
 * 最多支持 16 种 LBA 格式 (索引 0-15)。
 */
typedef struct _NVME_LBAF {
    USHORT MS;                          // Metadata Size (字节)
    UCHAR LBADS;                        // LBA Data Size (2^n 字节, 如 9=512, 12=4096)
    UCHAR RP;                           // Relative Performance (0=最佳, 3=最差)
} NVME_LBAF, *PNVME_LBAF;

//===========================================================================
// PDO 上下文 (虚拟 NVMe 控制器)
//===========================================================================

//
// 命名空间状态
// 
// Phase 1: 仅保存容量信息，实际 I/O 由用户态处理
// Phase 2+: 取消注释后端偏移、LBA 格式、统计等字段
//
typedef struct _VNVME_NAMESPACE {
    // 链表节点 (用于动态管理)
    LIST_ENTRY ListEntry;               // 链接到控制器的命名空间列表
    
    // 标识
    ULONG NsId;                         // 命名空间 ID (1-based, 0=未使用)
    BOOLEAN Active;                     // 是否激活
    BOOLEAN ReadOnly;                   // 只读标志
    BOOLEAN ThinProvisioned;            // 精简配置
    BOOLEAN Reserved1;                  // 对齐填充
    
    // 唯一标识
    GUID Guid;                          // 命名空间 GUID
    UCHAR Nguid[16];                    // Namespace Globally Unique Identifier
    UCHAR Eui64[8];                     // IEEE Extended Unique Identifier
    
    // 容量
    ULONG BlockSize;                    // 逻辑块大小 (512 或 4096)
    ULONGLONG TotalBlocks;              // 总逻辑块数
    ULONGLONG TotalBytes;               // 总字节数
    
    // 存储后端
    PVNVME_STORAGE_CONTEXT Storage;     // 存储后端上下文 (可为 NULL)
    
    // LBA 格式
    UCHAR FormattedLbaSize;             // FLBAS: Formatted LBA Size index
    UCHAR NumberOfLbaFormats;           // NLBAF: Number of LBA Formats (0-based)
    UCHAR NsFeatures;                   // NSFEAT: Namespace Features
    UCHAR Reserved2;                    // 对齐填充
    NVME_LBAF LbaFormats[16];           // LBA Format 数组 (最多 16 种格式)
    
    // 统计 (使用 Interlocked 操作)
    volatile LONG64 ReadCommands;       // 读命令计数
    volatile LONG64 WriteCommands;      // 写命令计数
    volatile LONG64 ReadBytes;          // 读取字节数
    volatile LONG64 WriteBytes;         // 写入字节数
    volatile LONG64 FlushCommands;      // Flush 命令计数
} VNVME_NAMESPACE, *PVNVME_NAMESPACE;

typedef struct _VNVME_QUEUE_STATE {
    PHYSICAL_ADDRESS BaseAddress;
    ULONG Size;                         // 队列大小 (条目数)
    volatile ULONG Head;
    volatile ULONG Tail;
    volatile BOOLEAN PhaseTag;
    BOOLEAN Created;
    USHORT CqId;                        // 关联的 CQ ID (仅 SQ 使用)
    USHORT Reserved;
} VNVME_QUEUE_STATE, *PVNVME_QUEUE_STATE;

typedef struct _VNVME_PDO_CONTEXT {
    // 标识
    BOOLEAN IsFdo;                      // FALSE = PDO
    ULONG Signature;                    // 'PDOV'
    
    // WDF 设备对象
    WDFDEVICE Device;
    WDFDEVICE ParentFdo;
    ULONG ControllerId;                 // 控制器索引 ID (0, 1, 2, ...)
    
    // BAR0 内存
    PVOID Bar0VirtAddr;                 // 内核虚拟地址
    PHYSICAL_ADDRESS Bar0PhysAddr;      // 物理地址 (报告给 stornvme)
    SIZE_T Bar0Size;
    PMDL Bar0Mdl;
    
    // NVMe 寄存器指针
    volatile PNVME_CONTROLLER_REGISTERS Registers;
    volatile PULONG Doorbells;          // Doorbell 区域基地址
    ULONG CachedCC;                     // CC 寄存器缓存 (检测变化)
    
    // PCIe 配置空间
    PVOID PcieConfig;
    SIZE_T PcieConfigSize;
    
    // Admin 队列
    VNVME_QUEUE_STATE AdminSq;
    VNVME_QUEUE_STATE AdminCq;
    ULONGLONG AdminSqBase;
    ULONG AdminSqSize;
    ULONGLONG AdminCqBase;
    ULONG AdminCqSize;
    ULONG LastAdminSqTail;
    ULONG LastAdminCqHead;
    ULONG AdminCqPhase;
    
    // I/O 队列
    VNVME_QUEUE_STATE IoSq[VNVME_MAX_IO_QUEUES];
    VNVME_QUEUE_STATE IoCq[VNVME_MAX_IO_QUEUES];
    USHORT IoQueueCount;                // 当前 I/O 队列数
    USHORT MaxIoQueues;                 // 最大 I/O 队列数 (CAP.MQES)
    
    // Doorbell 轮询
    WDFTIMER PollingTimer;
    ULONG PollingIntervalUs;
    volatile BOOLEAN PollingEnabled;
    volatile BOOLEAN PollingActive;
    
    // Async Event Request (AER) 存储
    // NVMe 允许主机提交多个 AER 命令，控制器在有事件时完成它们
    USHORT AerCids[VNVME_MAX_AER_COMMANDS];     // 存储的 AER 命令 CID
    USHORT AerCount;                             // 当前存储的 AER 命令数
    
    // 命名空间
    USHORT NamespaceCount;              // 活动命名空间数量
    VNVME_NAMESPACE Namespaces[VNVME_MAX_NAMESPACES];
    
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 统计
    volatile LONG64 AdminCommandsProcessed;
    volatile LONG64 IoCommandsProcessed;
    volatile LONG64 BytesRead;          // 读取字节数
    volatile LONG64 BytesWritten;       // 写入字节数
    
} VNVME_PDO_CONTEXT, *PVNVME_PDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_PDO_CONTEXT, VnvmeGetPdoContext)

//===========================================================================
// 心跳和用户态崩溃检测常量
//===========================================================================

#define VNVME_HEARTBEAT_TIMEOUT_MS      5000    // 心跳超时 (毫秒)
#define VNVME_HEARTBEAT_CHECK_INTERVAL  1000    // 检查间隔 (毫秒)

//===========================================================================
// 全局变量声明
//===========================================================================

extern PVNVME_FDO_CONTEXT g_FdoContext;

//===========================================================================
// 函数声明 - vnvme.c
//===========================================================================

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VnvmeEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VnvmeEvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE VnvmeEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VnvmeEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VnvmeEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VnvmeEvtDeviceD0Exit;

//===========================================================================
// 函数声明 - ctrl_dev.c
//===========================================================================

NTSTATUS
VnvmeCreateControlDevice(
    _In_ WDFDEVICE Device
    );

VOID
VnvmeDeleteControlDevice(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VnvmeEvtIoDeviceControl;

//===========================================================================
// 函数声明 - bar0.c
//===========================================================================

NTSTATUS
VnvmeAllocateBar0(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

VOID
VnvmeFreeBar0(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

VOID
VnvmeInitializeBar0Registers(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

ULONG
VnvmeReadBar0Register(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset
    );

VOID
VnvmeWriteBar0Register(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONG Value
    );

ULONGLONG
VnvmeReadBar0Register64(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset
    );

VOID
VnvmeWriteBar0Register64(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONGLONG Value
    );

//===========================================================================
// 函数声明 - shm.c
//===========================================================================

NTSTATUS
VnvmeAllocateShm(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

VOID
VnvmeFreeShm(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

NTSTATUS
VnvmeMapShmToUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _Out_ PVOID* UserAddress
    );

VOID
VnvmeUnmapShmFromUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

//===========================================================================
// 函数声明 - doorbell.c
//===========================================================================

NTSTATUS
VnvmeInitializePollingTimer(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

VOID
VnvmeStartPollingTimer(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

VOID
VnvmeStopPollingTimer(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

EVT_WDF_TIMER VnvmeEvtPollingTimer;

BOOLEAN
VnvmeProcessDoorbells(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

//===========================================================================
// 函数声明 - queue.c
//===========================================================================

NTSTATUS
VnvmeInitializeAdminQueues(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

NTSTATUS
VnvmeCreateIoSubmissionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ USHORT QueueSize,
    _In_ ULONGLONG PrpAddress,
    _In_ USHORT CqId
    );

NTSTATUS
VnvmeCreateIoCompletionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ USHORT QueueSize,
    _In_ ULONGLONG PrpAddress,
    _In_ USHORT IrqVector
    );

NTSTATUS
VnvmeDeleteIoSubmissionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId
    );

NTSTATUS
VnvmeDeleteIoCompletionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId
    );

NTSTATUS
VnvmeFetchCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _Out_ PNVME_COMMAND Command
    );

NTSTATUS
VnvmePostCompletion(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMPLETION Completion
    );

// 批处理优化函数
NTSTATUS
VnvmeFetchCommandBatch(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _Out_writes_(MaxCommands) PNVME_COMMAND Commands,
    _In_ ULONG MaxCommands,
    _Out_ PULONG CommandsFetched
    );

NTSTATUS
VnvmePostCompletionBatch(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_reads_(CompletionCount) PNVME_COMPLETION Completions,
    _In_ ULONG CompletionCount
    );

//===========================================================================
// 函数声明 - prp.c
//===========================================================================

typedef struct _VNVME_PRP_ENTRY {
    ULONGLONG PhysicalAddress;
    ULONG Offset;
    ULONG Length;
} VNVME_PRP_ENTRY, *PVNVME_PRP_ENTRY;

NTSTATUS
VnvmeParsePrpList(
    _In_ ULONGLONG Prp1,
    _In_ ULONGLONG Prp2,
    _In_ ULONG DataLength,
    _Out_ PVNVME_PRP_ENTRY* PrpEntries,
    _Out_ PULONG EntryCount
    );

VOID
VnvmeFreePrpEntries(
    _In_ PVNVME_PRP_ENTRY PrpEntries
    );

NTSTATUS
VnvmeReadFromHostMemory(
    _In_ ULONGLONG PhysicalAddress,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
VnvmeWriteToHostMemory(
    _In_ ULONGLONG PhysicalAddress,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

//===========================================================================
// 函数声明 - admin_cmd.c
//===========================================================================

/**
 * @brief 处理单个 Admin 命令 (内核模式)
 */
NTSTATUS
VnvmeProcessAdminCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    );

/**
 * @brief 处理 Admin 队列中所有待处理命令 (内核模式)
 */
VOID
VnvmeProcessAdminCommands(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG NewTail
    );

/**
 * @brief 完成一个待处理的 AER 命令
 * 
 * @param PdoContext PDO 上下文
 * @param EventType 事件类型 (0=Error, 1=SMART, 2=Notice, 6=Vendor)
 * @param EventInfo 事件信息
 * @param LogPage 关联的日志页 ID
 */
NTSTATUS
VnvmeCompleteAer(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ UINT8 EventType,
    _In_ UINT8 EventInfo,
    _In_ UINT8 LogPage
    );

//===========================================================================
// 函数声明 - io_cmd.c
//===========================================================================

/**
 * @brief 处理单个 I/O 命令 (内核模式)
 */
NTSTATUS
VnvmeProcessIoCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    );

/**
 * @brief 处理 I/O 队列中所有待处理命令 (内核模式)
 */
VOID
VnvmeProcessIoCommands(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ ULONG NewTail
    );

//===========================================================================
// 函数声明 - user_forward.c (用户态转发)
//===========================================================================

/**
 * @brief 通知用户态有新命令就绪
 * 
 * 当有新命令到达时调用，会设置 CommandReadyEvent 事件。
 * 用户态可以等待此事件，或使用轮询模式。
 */
VOID
VnvmeNotifyUserMode(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

/**
 * @brief 转发 Admin 命令到共享内存供用户态处理
 */
VOID
VnvmeForwardAdminCommandsToUser(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG NewTail
    );

/**
 * @brief 转发 I/O 命令到共享内存供用户态处理
 */
VOID
VnvmeForwardIoCommandsToUser(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ ULONG NewTail
    );

/**
 * @brief 处理用户态提交的完成结果
 */
NTSTATUS
VnvmeProcessUserCompletions(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

//===========================================================================
// 函数声明 - storage.c
//===========================================================================

NTSTATUS
VnvmeStorageCreate(
    _Out_ PVNVME_STORAGE_CONTEXT* StorageContext,
    _In_ VNVME_STORAGE_TYPE Type,
    _In_ ULONGLONG TotalBytes,
    _In_ ULONG BlockSize,
    _In_opt_ PUNICODE_STRING FilePath
    );

VOID
VnvmeStorageDestroy(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext
    );

NTSTATUS
VnvmeStorageRead(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
VnvmeStorageWrite(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
VnvmeStorageWriteZeroes(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length
    );

NTSTATUS
VnvmeStorageFlush(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext
    );

NTSTATUS
VnvmeStorageDeallocate(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length
    );

NTSTATUS
VnvmeStorageGetDirect(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PVOID* DirectPtr
    );

VOID
VnvmeStorageGetStats(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _Out_ PULONG64 ReadCount,
    _Out_ PULONG64 WriteCount,
    _Out_ PULONG64 BytesRead,
    _Out_ PULONG64 BytesWritten
    );

//===========================================================================
// 函数声明 - pcie_config.c
//===========================================================================

NTSTATUS
VnvmeAllocatePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

VOID
VnvmeFreePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

VOID
VnvmeInitializePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

NTSTATUS
VnvmeReadPcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer
    );

NTSTATUS
VnvmeWritePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer
    );

//===========================================================================
// 函数声明 - bus.c (高层 API - 供 IOCTL 调用)
//===========================================================================

NTSTATUS
VnvmeCreateVirtualController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId,
    _Out_opt_ WDFDEVICE* ChildDevice
    );

NTSTATUS
VnvmeDeleteVirtualController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId
    );

NTSTATUS
VnvmeEnumerateChildren(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

//===========================================================================
// 函数声明 - bus.c (低层实现 - 内部 PDO 操作)
//===========================================================================

NTSTATUS
VnvmeCreateControllerPdo(
    _In_ WDFDEVICE ParentDevice,
    _In_ ULONG ControllerId,
    _Out_ WDFDEVICE* PdoDevice
    );

NTSTATUS
VnvmeDeleteControllerPdo(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

//===========================================================================
// 函数声明 - pdo.c
//===========================================================================

EVT_WDF_DEVICE_PREPARE_HARDWARE VnvmePdoEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VnvmePdoEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VnvmePdoEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VnvmePdoEvtDeviceD0Exit;

NTSTATUS
VnvmePdoQueryDeviceId(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR* DeviceId
    );

NTSTATUS
VnvmePdoQueryDeviceText(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ DEVICE_TEXT_TYPE TextType,
    _In_ LCID LocaleId,
    _Out_ PWSTR* DeviceText
    );

NTSTATUS
VnvmePdoQueryCapabilities(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Inout_ PDEVICE_CAPABILITIES Capabilities
    );

NTSTATUS
VnvmePdoQueryBusInformation(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Out_ PPNP_BUS_INFORMATION* BusInformation
    );

NTSTATUS
VnvmePdoQueryResourceRequirements(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST* ResourceRequirements
    );

NTSTATUS
VnvmePdoQueryResources(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Out_ PCM_RESOURCE_LIST* Resources
    );

NTSTATUS
VnvmePdoQueryInterface(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ LPCGUID InterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _In_opt_ PVOID InterfaceSpecificData,
    _Inout_ PINTERFACE Interface
    );

//===========================================================================
// 函数声明 - utils.c
//===========================================================================

PWSTR
VnvmeAllocateString(
    _In_ PCWSTR SourceString
    );

PWSTR
VnvmeAllocateMultiString(
    _In_ PCWSTR String1,
    _In_opt_ PCWSTR String2
    );

//===========================================================================
// 辅助宏
//===========================================================================

#define VNVME_ALLOC_POOL(PoolType, Size) \
    ExAllocatePool2(POOL_FLAG_NON_PAGED, (Size), VNVME_POOL_TAG)

#define VNVME_FREE_POOL(Ptr) \
    do { if (Ptr) { ExFreePoolWithTag(Ptr, VNVME_POOL_TAG); Ptr = NULL; } } while(0)

#endif // _VNVME_H_
