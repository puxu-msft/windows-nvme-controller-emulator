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
#include "trace.h"

/*===========================================================================
 * 驱动标识
 *===========================================================================*/

#define VNVME_POOL_TAG              'MVNV'  // VNVM
#define VNVME_MAX_CONTROLLERS       16
#define VNVME_BAR0_SIZE             (64 * 1024)     // 64 KB
#define VNVME_PCIE_CONFIG_SIZE      4096            // 4 KB PCIe配置空间
#define VNVME_MAX_QUEUES            64
#define VNVME_POLLING_INTERVAL_MS   1               // 轮询间隔 (毫秒)

/* 共享内存配置 */
#define VNVME_SHARED_MEMORY_SIZE    (16 * 1024 * 1024)  // 16 MB
#define VNVME_SHARED_MEMORY_MAGIC   0x454D564E  // 'NVME'
#define VNVME_SHARED_MEMORY_VERSION 1
#define VNVME_CONTROL_BLOCK_SIZE    4096
#define VNVME_COMMAND_RING_SIZE     1024
#define VNVME_COMPLETION_RING_SIZE  1024

/* Doorbell 偏移计算 */
#define NVME_DOORBELL_OFFSET(qid, dstrd) \
    (0x1000 + ((2 * (qid)) * (4 << (dstrd))))

/*===========================================================================
 * FDO 上下文 (虚拟总线)
 *===========================================================================*/

typedef struct _VNVME_FDO_CONTEXT {
    /* WDF 设备对象 */
    WDFDEVICE Device;
    WDFDEVICE ControlDevice;
    
    /* 共享内存 */
    PVOID SharedMemory;
    PHYSICAL_ADDRESS SharedMemoryPhysical;
    SIZE_T SharedMemorySize;
    PMDL SharedMemoryMdl;
    PVOID SharedMemoryUserVa;
    
    /* 子设备管理 */
    LIST_ENTRY ChildDeviceList;
    KSPIN_LOCK ChildDeviceListLock;
    ULONG ChildDeviceCount;
    ULONG NextControllerId;
    
    /* 用户态通信 */
    KEVENT CommandEvent;
    KEVENT UserReadyEvent;
    volatile BOOLEAN UserReady;
    ULONG UserPid;
    LARGE_INTEGER LastHeartbeat;
    
    /* 统计 */
    volatile LONG64 CommandsProcessed;
    volatile LONG64 ErrorCount;
    
} VNVME_FDO_CONTEXT, *PVNVME_FDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_FDO_CONTEXT, VnvmeGetFdoContext)

/*===========================================================================
 * PDO 上下文 (虚拟 NVMe 控制器)
 *===========================================================================*/

typedef struct _VNVME_QUEUE_STATE {
    PHYSICAL_ADDRESS BaseAddress;
    ULONG Size;                         // 队列大小 (条目数)
    volatile ULONG Head;
    volatile ULONG Tail;
    volatile BOOLEAN PhaseTag;
    BOOLEAN Created;
} VNVME_QUEUE_STATE, *PVNVME_QUEUE_STATE;

typedef struct _VNVME_PDO_CONTEXT {
    /* WDF 设备对象 */
    WDFDEVICE Device;
    WDFDEVICE ParentFdo;
    ULONG ControllerId;
    
    /* BAR0 内存 */
    PVOID Bar0;
    PHYSICAL_ADDRESS Bar0Physical;
    SIZE_T Bar0Size;
    PMDL Bar0Mdl;
    
    /* BAR0 兼容指针 */
    PVOID Bar0Virtual;  /* 别名，等于 Bar0 */
    
    /* NVMe 寄存器指针 */
    volatile PNVME_CONTROLLER_REGISTERS Registers;
    
    /* PCIe 配置空间 */
    PVOID PcieConfig;
    SIZE_T PcieConfigSize;
    
    /* Admin 队列 */
    VNVME_QUEUE_STATE AdminSq;
    VNVME_QUEUE_STATE AdminCq;
    ULONGLONG AdminSqBase;
    ULONG AdminSqSize;
    ULONGLONG AdminCqBase;
    ULONG AdminCqSize;
    ULONG LastAdminSqTail;
    ULONG LastAdminCqHead;
    ULONG AdminCqPhase;
    
    /* I/O 队列 */
    VNVME_QUEUE_STATE IoSq[VNVME_MAX_QUEUES];
    VNVME_QUEUE_STATE IoCq[VNVME_MAX_QUEUES];
    ULONG IoQueueCount;
    
    /* Doorbell 轮询 */
    WDFTIMER PollingTimer;
    ULONG PollingIntervalUs;
    volatile BOOLEAN PollingEnabled;
    volatile BOOLEAN PollingActive;
    
    /* 控制器状态 */
    volatile BOOLEAN ControllerEnabled;
    volatile BOOLEAN ControllerReady;
    
    /* 链表节点 */
    LIST_ENTRY ListEntry;
    
    /* 统计 */
    volatile LONG64 AdminCommandsProcessed;
    volatile LONG64 IoCommandsProcessed;
    
} VNVME_PDO_CONTEXT, *PVNVME_PDO_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_PDO_CONTEXT, VnvmeGetPdoContext)

/*===========================================================================
 * 全局变量声明
 *===========================================================================*/

extern PVNVME_FDO_CONTEXT g_FdoContext;

/*===========================================================================
 * 函数声明 - vnvme.c
 *===========================================================================*/

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD VnvmeEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP VnvmeEvtDriverContextCleanup;
EVT_WDF_DEVICE_PREPARE_HARDWARE VnvmeEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE VnvmeEvtDeviceReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY VnvmeEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT VnvmeEvtDeviceD0Exit;

/*===========================================================================
 * 函数声明 - control_device.c
 *===========================================================================*/

NTSTATUS
VnvmeCreateControlDevice(
    _In_ WDFDEVICE Device
    );

VOID
VnvmeDeleteControlDevice(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL VnvmeEvtIoDeviceControl;

/*===========================================================================
 * 函数声明 - bus.c
 *===========================================================================*/

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

/*===========================================================================
 * 函数声明 - bar0.c
 *===========================================================================*/

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

/*===========================================================================
 * 函数声明 - shared_memory.c
 *===========================================================================*/

NTSTATUS
VnvmeAllocateSharedMemory(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

VOID
VnvmeFreeSharedMemory(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

NTSTATUS
VnvmeMapSharedMemoryToUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _Out_ PVOID* UserAddress
    );

VOID
VnvmeUnmapSharedMemoryFromUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    );

/*===========================================================================
 * 函数声明 - doorbell.c
 *===========================================================================*/

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

VOID
VnvmeProcessDoorbells(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    );

/*===========================================================================
 * 函数声明 - queue.c
 *===========================================================================*/

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

/*===========================================================================
 * 函数声明 - prp.c
 *===========================================================================*/

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

/*===========================================================================
 * 函数声明 - pcie_config.c
 *===========================================================================*/

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

/*===========================================================================
 * 函数声明 - bus.c
 *===========================================================================*/

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

/*===========================================================================
 * 函数声明 - pdo.c
 *===========================================================================*/

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

/*===========================================================================
 * 共享内存结构 (内核/用户态共享)
 *===========================================================================*/

/* 命令环条目 */
typedef struct _VNVME_COMMAND_RING_ENTRY {
    UINT32 Opcode;
    UINT32 ControllerId;
    UINT32 NamespaceId;
    UINT32 Flags;
    UINT64 DataOffset;
    UINT32 DataLength;
    UINT32 Reserved;
    UINT8 CommandData[64];
} VNVME_COMMAND_RING_ENTRY, *PVNVME_COMMAND_RING_ENTRY;

/* 完成环条目 */
typedef struct _VNVME_COMPLETION_RING_ENTRY {
    UINT32 Status;
    UINT32 ControllerId;
    UINT32 CommandId;
    UINT32 Result;
    UINT8 CompletionData[32];
} VNVME_COMPLETION_RING_ENTRY, *PVNVME_COMPLETION_RING_ENTRY;

/* 命令环 */
typedef struct _VNVME_COMMAND_RING {
    volatile UINT32 Head;
    volatile UINT32 Tail;
    UINT32 Size;
    UINT32 Reserved;
    VNVME_COMMAND_RING_ENTRY Entries[VNVME_COMMAND_RING_SIZE];
} VNVME_COMMAND_RING, *PVNVME_COMMAND_RING;

/* 完成环 */
typedef struct _VNVME_COMPLETION_RING {
    volatile UINT32 Head;
    volatile UINT32 Tail;
    UINT32 Size;
    UINT32 Reserved;
    VNVME_COMPLETION_RING_ENTRY Entries[VNVME_COMPLETION_RING_SIZE];
} VNVME_COMPLETION_RING, *PVNVME_COMPLETION_RING;

/* 共享内存控制块 */
typedef struct _VNVME_SHARED_MEMORY_CONTROL_BLOCK {
    UINT32 Magic;
    UINT32 Version;
    UINT32 TotalSize;
    UINT32 ControlBlockSize;
    
    /* 环偏移 */
    UINT32 CommandRingOffset;
    UINT32 CommandRingSize;
    UINT32 CompletionRingOffset;
    UINT32 CompletionRingSize;
    
    /* 数据缓冲区 */
    UINT32 DataBufferOffset;
    UINT32 DataBufferSize;
    
    /* 状态标志 */
    volatile UINT32 KernelReady;
    volatile UINT32 UserReady;
    volatile UINT32 ErrorCode;
    UINT32 Reserved[3];
} VNVME_SHARED_MEMORY_CONTROL_BLOCK, *PVNVME_SHARED_MEMORY_CONTROL_BLOCK;

/*===========================================================================
 * 辅助宏
 *===========================================================================*/

#define VNVME_ALLOC_POOL(PoolType, Size) \
    ExAllocatePool2(POOL_FLAG_NON_PAGED, (Size), VNVME_POOL_TAG)

#define VNVME_FREE_POOL(Ptr) \
    do { if (Ptr) { ExFreePoolWithTag(Ptr, VNVME_POOL_TAG); Ptr = NULL; } } while(0)

#endif /* _VNVME_H_ */
