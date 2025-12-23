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
#define VNVME_MAX_NAMESPACES        16              // 每个控制器最大命名空间数
#define VNVME_BAR0_SIZE             (64 * 1024)     // 64 KB
#define VNVME_PCIE_CONFIG_SIZE      4096            // 4 KB PCIe配置空间
#define VNVME_MAX_QUEUES            64
#define VNVME_POLLING_INTERVAL_MS   1               // 轮询间隔 (毫秒)

/* 签名常量 */
#define VNVME_FDO_SIGNATURE         'FDOV'
#define VNVME_PDO_SIGNATURE         'PDOV'

/* Doorbell 偏移计算 */
#define NVME_DOORBELL_OFFSET(qid, dstrd) \
    (0x1000 + ((2 * (qid)) * (4 << (dstrd))))

/*===========================================================================
 * FDO 上下文 (虚拟总线)
 *===========================================================================*/

typedef struct _VNVME_FDO_CONTEXT {
    /* 标识 */
    BOOLEAN IsFdo;                      /* TRUE = FDO, FALSE = PDO */
    ULONG Signature;                    /* 'FDOV' */
    
    /* WDF 设备对象 */
    WDFDEVICE Device;
    WDFDEVICE ControlDevice;
    /* WDFQUEUE ControlQueue; */        /* TODO: 优雅关闭时需要保存队列句柄 */
    
    /* 共享内存 */
    PVOID ShmKernelVirtAddr;            /* 内核虚拟地址 */
    PHYSICAL_ADDRESS ShmPhysAddr;       /* 物理地址 */
    SIZE_T ShmSize;
    PMDL ShmMdl;
    PVOID ShmUserVirtAddr;              /* 用户态虚拟地址 */
    
    /* 子设备管理 */
    LIST_ENTRY ChildDeviceList;
    KSPIN_LOCK ChildDeviceListLock;
    ULONG ChildDeviceCount;             /* 当前控制器数量 */
    ULONG NextControllerId;
    ULONG MaxControllers;               /* 最大控制器数量 */
    
    /* 用户态通信 */
    KEVENT CommandReadyEvent;           /* 通知用户态有新命令就绪 */
    KEVENT UserReadyEvent;              /* 用户态服务就绪事件 */
    /* KEVENT ShutdownEvent; */         /* TODO: 通知用户态关闭 */
    volatile BOOLEAN UserReady;         /* 用户态服务是否就绪 */
    /* volatile BOOLEAN ShutdownRequested; */ /* TODO: 关闭请求标志 */
    ULONG UserPid;
    LARGE_INTEGER LastHeartbeat;
    /* HANDLE UserEventHandle; */       /* TODO: 用户可等待事件句柄 (性能优化) */
    
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
    /* 标识 */
    BOOLEAN IsFdo;                      /* FALSE = PDO */
    ULONG Signature;                    /* 'PDOV' */
    
    /* WDF 设备对象 */
    WDFDEVICE Device;
    WDFDEVICE ParentFdo;
    ULONG ControllerId;
    
    /* BAR0 内存 */
    PVOID Bar0VirtAddr;                 /* 内核虚拟地址 */
    PHYSICAL_ADDRESS Bar0PhysAddr;      /* 物理地址 (报告给 stornvme) */
    SIZE_T Bar0Size;
    PMDL Bar0Mdl;
    
    /* NVMe 寄存器指针 */
    volatile PNVME_CONTROLLER_REGISTERS Registers;
    volatile PULONG Doorbells;          /* Doorbell 区域基地址 */
    ULONG CachedCC;                     /* CC 寄存器缓存 (检测变化) */
    
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
    ULONG MaxIoQueues;                  /* 最大 I/O 队列数 */
    
    /* Doorbell 轮询 */
    WDFTIMER PollingTimer;
    ULONG PollingIntervalUs;
    volatile BOOLEAN PollingEnabled;
    volatile BOOLEAN PollingActive;
    
    /* 命名空间 */
    ULONG NamespaceCount;               /* 活动命名空间数量 */
    ULONGLONG NamespaceSizes[VNVME_MAX_NAMESPACES]; /* 每个命名空间大小 (字节) */
    
    /* 链表节点 */
    LIST_ENTRY ListEntry;
    
    /* 统计 */
    volatile LONG64 AdminCommandsProcessed;
    volatile LONG64 IoCommandsProcessed;
    volatile LONG64 BytesRead;          /* 读取字节数 */
    volatile LONG64 BytesWritten;       /* 写入字节数 */
    
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
 * 函数声明 - ctrl_dev.c
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
 * 函数声明 - shm.c
 *===========================================================================*/

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
 * 函数声明 - bus.c (高层 API - 供 IOCTL 调用)
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
 * 函数声明 - bus.c (低层实现 - 内部 PDO 操作)
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
 * 辅助宏
 *===========================================================================*/

#define VNVME_ALLOC_POOL(PoolType, Size) \
    ExAllocatePool2(POOL_FLAG_NON_PAGED, (Size), VNVME_POOL_TAG)

#define VNVME_FREE_POOL(Ptr) \
    do { if (Ptr) { ExFreePoolWithTag(Ptr, VNVME_POOL_TAG); Ptr = NULL; } } while(0)

#endif /* _VNVME_H_ */
