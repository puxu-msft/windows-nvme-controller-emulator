/**
 * @file vnvme_common.h
 * @brief VNVME 项目公共定义 - 内核和用户态共享
 * 
 * 本文件定义内核驱动 (vnvme.sys) 和用户态服务 (vnvme-server.exe) 之间共享的
 * 数据结构、常量和类型。
 */

#ifndef _VNVME_COMMON_H_
#define _VNVME_COMMON_H_

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <stdint.h>
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
#endif

/* 静态断言宏 - 使用唯一名称避免与 winnt.h 冲突 */
#define VNVME_STATIC_ASSERT(expr, msg) \
    typedef char vnvme_static_assert_##msg[(expr) ? 1 : -1]

#pragma pack(push, 1)

/*===========================================================================
 * 版本信息
 *===========================================================================*/

#define VNVME_VERSION_MAJOR     1
#define VNVME_VERSION_MINOR     0
#define VNVME_VERSION_PATCH     0
#define VNVME_VERSION           ((VNVME_VERSION_MAJOR << 16) | \
                                 (VNVME_VERSION_MINOR << 8) | \
                                 VNVME_VERSION_PATCH)

#define VNVME_DRIVER_NAME       L"vnvme"
#define VNVME_CONTROL_DEVICE    L"\\Device\\VNVMEControl"
#define VNVME_CONTROL_LINK      L"\\DosDevices\\VNVMEControl"
#define VNVME_CONTROL_USER_PATH L"\\\\.\\VNVMEControl"

/*===========================================================================
 * 共享内存配置
 *===========================================================================*/

#define VNVME_SHARED_MEMORY_SIZE        (64 * 1024 * 1024)  // 64 MB
#define VNVME_CONTROL_BLOCK_SIZE        4096                 // 4 KB
#define VNVME_DATA_BUFFER_SIZE          (60 * 1024 * 1024)   // ~60 MB

// NVMe 队列配置
#define VNVME_ADMIN_QUEUE_DEPTH         64                   // Admin 队列深度
#define VNVME_IO_QUEUE_DEPTH            256                  // I/O 队列深度
#define VNVME_MAX_IO_QUEUES             16                   // 最大 I/O 队列数

// NVMe 条目大小 (NVMe 规范)
#define NVME_SQ_ENTRY_SIZE              64                   // Submission Queue Entry
#define NVME_CQ_ENTRY_SIZE              16                   // Completion Queue Entry

#define VNVME_SHARED_MEMORY_MAGIC       0x454D564E          // "NVME"
#define VNVME_SHARED_MEMORY_VERSION     2                    // v2: 零复制架构

/*===========================================================================
 * 队列描述符 - 描述 SQ/CQ 在共享内存中的位置
 *===========================================================================*/

/**
 * @brief 队列描述符
 * 
 * 描述一个 NVMe 队列 (SQ 或 CQ) 在共享内存中的位置和状态。
 * 用户态通过此描述符定位队列并访问原始 NVMe 条目。
 */
typedef struct _VNVME_QUEUE_DESCRIPTOR {
    UINT32 Offset;                      // 队列在共享内存中的偏移
    UINT32 EntrySize;                   // 条目大小 (SQ=64, CQ=16)
    UINT32 Capacity;                    // 队列容量 (条目数)
    UINT32 Valid;                       // 队列是否有效
    volatile UINT32 Head;               // 消费者索引
    volatile UINT32 Tail;               // 生产者索引
    volatile UINT32 Phase;              // CQ 相位位 (仅 CQ)
    UINT32 Reserved;
} VNVME_QUEUE_DESCRIPTOR, *PVNVME_QUEUE_DESCRIPTOR;

VNVME_STATIC_ASSERT(sizeof(VNVME_QUEUE_DESCRIPTOR) == 32, queue_descriptor_size);

/*===========================================================================
 * 通知环 - 轻量级 Head/Tail 同步
 *===========================================================================*/

/**
 * @brief 通知条目 - 通知用户态有新命令或完成项
 */
typedef struct _VNVME_NOTIFY_ENTRY {
    UINT16 QueueId;                     // 队列 ID (0 = Admin)
    UINT16 Type;                        // 0 = SQ 有新命令, 1 = CQ 已更新
    UINT32 Index;                       // Doorbell 写入的索引值
} VNVME_NOTIFY_ENTRY, *PVNVME_NOTIFY_ENTRY;

VNVME_STATIC_ASSERT(sizeof(VNVME_NOTIFY_ENTRY) == 8, notify_entry_size);

#define VNVME_NOTIFY_RING_SIZE          256

/**
 * @brief 通知环 - 内核通知用户态 Doorbell 变化
 */
typedef struct _VNVME_NOTIFY_RING {
    volatile UINT32 Head;               // 消费者 (用户态)
    volatile UINT32 Tail;               // 生产者 (内核)
    UINT32 Size;
    UINT32 Reserved;
    VNVME_NOTIFY_ENTRY Entries[VNVME_NOTIFY_RING_SIZE];
} VNVME_NOTIFY_RING, *PVNVME_NOTIFY_RING;

/*===========================================================================
 * 共享内存控制块 (v2 - 零复制架构)
 *===========================================================================*/

/**
 * @brief 共享内存控制块 - 位于共享内存起始位置
 * 
 * 零复制架构：
 * - NVMe SQ/CQ 直接分配在共享内存中
 * - stornvme 通过物理地址写入 SQ
 * - 用户态通过虚拟地址直接读取原始 NVME_COMMAND
 * - 无需复制命令数据
 */
typedef struct _VNVME_SHARED_MEMORY_CONTROL_BLOCK {
    /* 头部 (0x00-0x1F) */
    UINT32 Magic;                       // 0x00: 魔数 VNVME_SHARED_MEMORY_MAGIC
    UINT32 Version;                     // 0x04: 版本号 (2 = 零复制)
    UINT32 TotalSize;                   // 0x08: 共享内存总大小
    UINT32 ControlBlockSize;            // 0x0C: 控制块大小
    UINT32 Flags;                       // 0x10: 标志
    UINT32 Reserved1;                   // 0x14
    UINT64 Reserved2;                   // 0x18
    
    /* Admin 队列描述符 (0x20-0x5F) */
    VNVME_QUEUE_DESCRIPTOR AdminSQ;     // 0x20: Admin Submission Queue
    VNVME_QUEUE_DESCRIPTOR AdminCQ;     // 0x40: Admin Completion Queue
    
    /* I/O 队列信息 (0x60-0x6F) */
    UINT32 IoQueueCount;                // 0x60: 当前活动 I/O 队列数
    UINT32 MaxIoQueues;                 // 0x64: 最大 I/O 队列数
    UINT32 IoQueueDescriptorOffset;     // 0x68: I/O 队列描述符数组偏移
    UINT32 Reserved3;                   // 0x6C
    
    /* 通知环 (0x70-0x7F) */
    UINT32 NotifyRingOffset;            // 0x70: 通知环偏移
    UINT32 NotifyRingSize;              // 0x74: 通知环大小
    UINT64 Reserved4;                   // 0x78
    
    /* 数据缓冲区 (0x80-0x8F) */
    UINT32 DataBufferOffset;            // 0x80: 数据缓冲区偏移
    UINT32 DataBufferSize;              // 0x84: 数据缓冲区大小
    UINT64 Reserved5;                   // 0x88
    
    /* 状态 (0x90-0x9F) */
    volatile UINT32 KernelReady;        // 0x90: 内核就绪标志
    volatile UINT32 UserReady;          // 0x94: 用户态就绪标志
    volatile UINT32 ErrorCode;          // 0x98: 错误码
    volatile UINT32 ControllerState;    // 0x9C: 控制器状态
    
    /* 统计 (0xA0-0xBF) */
    volatile UINT64 CommandsProcessed;  // 0xA0: 已处理命令数
    volatile UINT64 CompletionsPosted;  // 0xA8: 已提交完成数
    volatile UINT64 BytesRead;          // 0xB0: 读取字节数
    volatile UINT64 BytesWritten;       // 0xB8: 写入字节数
    
    /* 保留 */
    UINT8 Reserved[4096 - 0xC0];
    
} VNVME_SHARED_MEMORY_CONTROL_BLOCK, *PVNVME_SHARED_MEMORY_CONTROL_BLOCK;

VNVME_STATIC_ASSERT(sizeof(VNVME_SHARED_MEMORY_CONTROL_BLOCK) == 4096, control_block_size);

/*===========================================================================
 * 控制器状态
 *===========================================================================*/

typedef enum _VNVME_CONTROLLER_STATE {
    VNVME_CTRL_STATE_DISABLED   = 0,    // CC.EN=0, CSTS.RDY=0
    VNVME_CTRL_STATE_ENABLING   = 1,    // CC.EN=1, CSTS.RDY=0
    VNVME_CTRL_STATE_READY      = 2,    // CC.EN=1, CSTS.RDY=1
    VNVME_CTRL_STATE_DISABLING  = 3,    // CC.EN=0, CSTS.RDY=1
    VNVME_CTRL_STATE_FAILED     = 4,    // CSTS.CFS=1
} VNVME_CONTROLLER_STATE;

/*===========================================================================
 * 旧结构保留 (兼容性/迁移用, 标记为 deprecated)
 *===========================================================================*/

#ifdef VNVME_INCLUDE_DEPRECATED

/**
 * @deprecated 使用零复制架构，用户态直接访问 NVME_COMMAND
 */
typedef enum _VNVME_COMMAND_TYPE {
    VNVME_CMD_ADMIN             = 0,
    VNVME_CMD_IO_READ           = 1,
    VNVME_CMD_IO_WRITE          = 2,
    VNVME_CMD_IO_FLUSH          = 3,
    VNVME_CMD_CONTROLLER_ENABLE = 4,
    VNVME_CMD_CONTROLLER_RESET  = 5,
    VNVME_CMD_CREATE_QUEUE      = 6,
    VNVME_CMD_DELETE_QUEUE      = 7,
} VNVME_COMMAND_TYPE;

/**
 * @deprecated 用户态直接访问原始 NVME_COMMAND (64字节)
 */
typedef struct _VNVME_SUBMISSION_RING_ENTRY_DEPRECATED {
    UINT32 Type;
    UINT16 CommandId;
    UINT16 QueueId;
    UINT8  Opcode;
    UINT8  Flags;
    UINT16 Reserved1;
    UINT32 NSID;
    UINT32 DataBufferOffset;
    UINT32 DataLength;
    UINT64 StartLBA;
    UINT32 CDW10;
    UINT32 CDW11;
    UINT32 CDW12;
    UINT32 CDW13;
    UINT32 CDW14;
    UINT32 CDW15;
    UINT64 PRP1;
    UINT64 PRP2;
    UINT64 Timestamp;
} VNVME_SUBMISSION_RING_ENTRY_DEPRECATED;

#endif // VNVME_INCLUDE_DEPRECATED

/*===========================================================================
 * NVMe 状态码
 *===========================================================================*/

/* 通用状态 */
#define VNVME_STATUS_SUCCESS                0x0000
#define VNVME_STATUS_INVALID_OPCODE         0x0001
#define VNVME_STATUS_INVALID_FIELD          0x0002
#define VNVME_STATUS_COMMAND_ID_CONFLICT    0x0003
#define VNVME_STATUS_DATA_TRANSFER_ERROR    0x0004
#define VNVME_STATUS_ABORTED_POWER_LOSS     0x0005
#define VNVME_STATUS_INTERNAL_ERROR         0x0006
#define VNVME_STATUS_ABORTED_BY_REQUEST     0x0007
#define VNVME_STATUS_ABORTED_SQ_DELETED     0x0008
#define VNVME_STATUS_ABORTED_FUSED_FAILED   0x0009
#define VNVME_STATUS_ABORTED_MISSING_FUSED  0x000A

/* 命令特定状态 */
#define VNVME_STATUS_LBA_OUT_OF_RANGE       0x0080
#define VNVME_STATUS_NAMESPACE_NOT_READY    0x0082
#define VNVME_STATUS_INVALID_NAMESPACE      0x000B

/* I/O 命令状态 */
#define VNVME_STATUS_WRITE_FAULT            0x0280
#define VNVME_STATUS_READ_ERROR             0x0281
#define VNVME_STATUS_MEDIA_ERROR            0x0282

/*===========================================================================
 * 控制器配置
 *===========================================================================*/

/**
 * @brief 控制器配置结构
 */
typedef struct _VNVME_CONTROLLER_CONFIG {
    CHAR SerialNumber[20];              // 序列号
    CHAR ModelNumber[40];               // 型号
    CHAR FirmwareRevision[8];           // 固件版本
    UINT32 MaxNamespaces;               // 最大命名空间数
    UINT32 MaxQueuePairs;               // 最大队列对数
    UINT32 MaxQueueDepth;               // 最大队列深度
    UINT32 MaxTransferSize;             // 最大传输大小 (字节)
} VNVME_CONTROLLER_CONFIG, *PVNVME_CONTROLLER_CONFIG;

/**
 * @brief 命名空间配置
 */
typedef struct _VNVME_NAMESPACE_CONFIG {
    UINT32 NSID;                        // 命名空间 ID
    UINT64 TotalBlocks;                 // 总块数
    UINT32 BlockSize;                   // 块大小 (512/4096)
    UINT32 Flags;                       // 标志
    CHAR BackendPath[260];              // 后端路径 (文件后端)
} VNVME_NAMESPACE_CONFIG, *PVNVME_NAMESPACE_CONFIG;

#define VNVME_NS_FLAG_ENABLED           0x0001
#define VNVME_NS_FLAG_READONLY          0x0002
#define VNVME_NS_FLAG_SPARSE            0x0004

/*===========================================================================
 * 共享内存布局计算宏
 *===========================================================================*/

/**
 * 共享内存布局 (零复制架构):
 * 
 * +------------------+ 0x00000000
 * | Control Block    | 4KB (VNVME_SHARED_MEMORY_CONTROL_BLOCK)
 * +------------------+ 0x00001000
 * | Notify Ring      | 4KB (VNVME_NOTIFY_RING, 对齐)
 * +------------------+ 0x00002000
 * | Admin SQ         | 64×64B = 4KB
 * +------------------+ 0x00003000
 * | Admin CQ         | 64×16B = 1KB (对齐到 4KB)
 * +------------------+ 0x00004000
 * | I/O Queue Desc   | 16×2×32B = 1KB (SQ+CQ 描述符)
 * +------------------+ 0x00005000
 * | I/O SQ[0]        | 256×64B = 16KB
 * +------------------+ 0x00009000
 * | I/O CQ[0]        | 256×16B = 4KB
 * +------------------+ 0x0000A000
 * | I/O SQ[1]        | 16KB
 * +------------------+ ...
 * | Data Buffer      | 剩余空间 (~60MB)
 * +------------------+ 0x04000000 (64MB)
 */

// 对齐到 4KB 页边界
#define VNVME_ALIGN_PAGE(x)             (((x) + 4095) & ~4095ULL)

// Admin 队列大小
#define VNVME_ADMIN_SQ_SIZE             VNVME_ALIGN_PAGE(VNVME_ADMIN_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE)
#define VNVME_ADMIN_CQ_SIZE             VNVME_ALIGN_PAGE(VNVME_ADMIN_QUEUE_DEPTH * NVME_CQ_ENTRY_SIZE)

// I/O 队列大小
#define VNVME_IO_SQ_SIZE                VNVME_ALIGN_PAGE(VNVME_IO_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE)
#define VNVME_IO_CQ_SIZE                VNVME_ALIGN_PAGE(VNVME_IO_QUEUE_DEPTH * NVME_CQ_ENTRY_SIZE)

// 共享内存区域偏移
#define VNVME_OFFSET_CONTROL_BLOCK      0
#define VNVME_OFFSET_NOTIFY_RING        VNVME_CONTROL_BLOCK_SIZE
#define VNVME_OFFSET_ADMIN_SQ           (VNVME_OFFSET_NOTIFY_RING + VNVME_ALIGN_PAGE(sizeof(VNVME_NOTIFY_RING)))
#define VNVME_OFFSET_ADMIN_CQ           (VNVME_OFFSET_ADMIN_SQ + VNVME_ADMIN_SQ_SIZE)
#define VNVME_OFFSET_IO_QUEUE_DESC      (VNVME_OFFSET_ADMIN_CQ + VNVME_ADMIN_CQ_SIZE)

// I/O 队列描述符数组大小 (每个队列需要 SQ + CQ 两个描述符)
#define VNVME_IO_QUEUE_DESC_SIZE        VNVME_ALIGN_PAGE(VNVME_MAX_IO_QUEUES * 2 * sizeof(VNVME_QUEUE_DESCRIPTOR))

// I/O 队列区域起始偏移
#define VNVME_OFFSET_IO_QUEUES          (VNVME_OFFSET_IO_QUEUE_DESC + VNVME_IO_QUEUE_DESC_SIZE)

// 单个 I/O 队列对大小 (SQ + CQ)
#define VNVME_IO_QUEUE_PAIR_SIZE        (VNVME_IO_SQ_SIZE + VNVME_IO_CQ_SIZE)

// 计算第 N 个 I/O SQ 的偏移
#define VNVME_IO_SQ_OFFSET(n)           (VNVME_OFFSET_IO_QUEUES + (n) * VNVME_IO_QUEUE_PAIR_SIZE)

// 计算第 N 个 I/O CQ 的偏移
#define VNVME_IO_CQ_OFFSET(n)           (VNVME_IO_SQ_OFFSET(n) + VNVME_IO_SQ_SIZE)

// 数据缓冲区偏移 (所有队列之后)
#define VNVME_OFFSET_DATA_BUFFER        (VNVME_OFFSET_IO_QUEUES + VNVME_MAX_IO_QUEUES * VNVME_IO_QUEUE_PAIR_SIZE)

/*===========================================================================
 * 辅助内联函数
 *===========================================================================*/

/**
 * @brief 获取 Admin SQ 指针
 */
static inline void* VnvmeGetAdminSQ(void* SharedMemory)
{
    return (UINT8*)SharedMemory + VNVME_OFFSET_ADMIN_SQ;
}

/**
 * @brief 获取 Admin CQ 指针
 */
static inline void* VnvmeGetAdminCQ(void* SharedMemory)
{
    return (UINT8*)SharedMemory + VNVME_OFFSET_ADMIN_CQ;
}

/**
 * @brief 获取 I/O SQ 指针
 */
static inline void* VnvmeGetIoSQ(void* SharedMemory, UINT32 QueueIndex)
{
    return (UINT8*)SharedMemory + VNVME_IO_SQ_OFFSET(QueueIndex);
}

/**
 * @brief 获取 I/O CQ 指针
 */
static inline void* VnvmeGetIoCQ(void* SharedMemory, UINT32 QueueIndex)
{
    return (UINT8*)SharedMemory + VNVME_IO_CQ_OFFSET(QueueIndex);
}

/**
 * @brief 获取控制块指针
 */
static inline PVNVME_SHARED_MEMORY_CONTROL_BLOCK VnvmeGetControlBlock(void* SharedMemory)
{
    return (PVNVME_SHARED_MEMORY_CONTROL_BLOCK)SharedMemory;
}

/**
 * @brief 获取通知环指针
 */
static inline PVNVME_NOTIFY_RING VnvmeGetNotifyRing(void* SharedMemory)
{
    return (PVNVME_NOTIFY_RING)((UINT8*)SharedMemory + VNVME_OFFSET_NOTIFY_RING);
}

/**
 * @brief 获取数据缓冲区指针
 */
static inline void* VnvmeGetDataBuffer(void* SharedMemory)
{
    return (UINT8*)SharedMemory + VNVME_OFFSET_DATA_BUFFER;
}

#pragma pack(pop)

#endif /* _VNVME_COMMON_H_ */
