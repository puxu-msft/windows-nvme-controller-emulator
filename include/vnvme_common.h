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
#define VNVME_SUBMISSION_RING_SIZE      1024                 // 提交条目数
#define VNVME_COMPLETION_RING_SIZE      1024                 // 完成条目数
#define VNVME_DATA_BUFFER_SIZE          (60 * 1024 * 1024)   // ~60 MB

#define VNVME_SHARED_MEMORY_MAGIC       0x454D564E          // "NVME"
#define VNVME_SHARED_MEMORY_VERSION     1

/*===========================================================================
 * 共享内存控制块
 *===========================================================================*/

/**
 * @brief 共享内存控制块 - 位于共享内存起始位置
 */
typedef struct _VNVME_SHARED_MEMORY_CONTROL_BLOCK {
    /* 魔数和版本 (0x00-0x0F) */
    UINT32 Magic;                       // 0x00: 魔数 VNVME_SHARED_MEMORY_MAGIC
    UINT32 Version;                     // 0x04: 版本号
    UINT32 TotalSize;                   // 0x08: 共享内存总大小
    UINT32 ControlBlockSize;            // 0x0C: 控制块大小
    
    /* 环偏移 (0x10-0x1F) */
    UINT32 SubmissionRingOffset;        // 0x10: 提交环偏移
    UINT32 SubmissionRingSize;          // 0x14: 提交环条目数
    UINT32 CompletionRingOffset;        // 0x18: 完成环偏移
    UINT32 CompletionRingSize;          // 0x1C: 完成环条目数
    
    /* 数据缓冲区 (0x20-0x2F) */
    UINT32 DataBufferOffset;            // 0x20: 数据缓冲区偏移
    UINT32 DataBufferSize;              // 0x24: 数据缓冲区大小
    UINT32 Reserved1[2];
    
    /* 状态 (0x30-0x3F) */
    volatile UINT32 KernelReady;        // 0x30: 内核就绪标志
    volatile UINT32 UserReady;          // 0x34: 用户态就绪标志
    volatile UINT32 ErrorCode;          // 0x38: 错误码
    UINT32 Reserved2;
    
    /* 统计 (0x40-0x5F) */
    volatile UINT64 CommandsProcessed;  // 0x40: 已处理命令数
    volatile UINT64 CompletionsPosted;  // 0x48: 已提交完成数
    volatile UINT64 BytesRead;          // 0x50: 读取字节数
    volatile UINT64 BytesWritten;       // 0x58: 写入字节数
    
    /* 保留 */
    UINT8 Reserved[4096 - 0x60];
    
} VNVME_SHARED_MEMORY_CONTROL_BLOCK, *PVNVME_SHARED_MEMORY_CONTROL_BLOCK;

VNVME_STATIC_ASSERT(sizeof(VNVME_SHARED_MEMORY_CONTROL_BLOCK) == 4096, control_block_size);

/*===========================================================================
 * 提交环 (Submission Ring) - 内核向用户态提交命令
 *===========================================================================*/

/**
 * @brief 命令类型
 */
typedef enum _VNVME_COMMAND_TYPE {
    VNVME_CMD_ADMIN             = 0,    // Admin 命令
    VNVME_CMD_IO_READ           = 1,    // I/O 读
    VNVME_CMD_IO_WRITE          = 2,    // I/O 写
    VNVME_CMD_IO_FLUSH          = 3,    // I/O 刷新
    VNVME_CMD_CONTROLLER_ENABLE = 4,    // 控制器启用
    VNVME_CMD_CONTROLLER_RESET  = 5,    // 控制器重置
    VNVME_CMD_CREATE_QUEUE      = 6,    // 创建队列
    VNVME_CMD_DELETE_QUEUE      = 7,    // 删除队列
} VNVME_COMMAND_TYPE;

/**
 * @brief 提交环条目 - 内核提交给用户态处理的命令
 */
typedef struct _VNVME_SUBMISSION_RING_ENTRY {
    /* 基本信息 */
    UINT32 Type;                        // 命令类型 (VNVME_COMMAND_TYPE)
    UINT16 CommandId;                   // 命令 ID
    UINT16 QueueId;                     // 队列 ID (0 = Admin)
    
    /* NVMe 命令相关 */
    UINT8  Opcode;                      // NVMe opcode
    UINT8  Flags;                       // 标志
    UINT16 Reserved1;
    UINT32 NSID;                        // 命名空间 ID
    
    /* 数据位置 */
    UINT32 DataBufferOffset;            // 数据在共享内存中的偏移
    UINT32 DataLength;                  // 数据长度
    
    /* I/O 参数 */
    UINT64 StartLBA;                    // 起始 LBA
    UINT32 CDW10;                       // 命令双字 10
    UINT32 CDW11;                       // 命令双字 11
    UINT32 CDW12;                       // 命令双字 12
    UINT32 CDW13;                       // 命令双字 13
    UINT32 CDW14;                       // 命令双字 14
    UINT32 CDW15;                       // 命令双字 15
    
    /* PRP 信息 (内核填充) */
    UINT64 PRP1;                        // PRP1 (物理地址)
    UINT64 PRP2;                        // PRP2 (物理地址或 PRP List)
    
    /* 时间戳 */
    UINT64 Timestamp;                   // 命令提交时间
    
} VNVME_SUBMISSION_RING_ENTRY, *PVNVME_SUBMISSION_RING_ENTRY;

VNVME_STATIC_ASSERT(sizeof(VNVME_SUBMISSION_RING_ENTRY) == 80, submission_ring_entry_size);

/**
 * @brief 提交环 - 内核向用户态提交命令的环形缓冲区
 */
typedef struct _VNVME_SUBMISSION_RING {
    volatile UINT32 Head;               // 消费者 (用户态) 更新
    volatile UINT32 Tail;               // 生产者 (内核) 更新
    UINT32 Size;                        // 环大小
    UINT32 Reserved;
    VNVME_SUBMISSION_RING_ENTRY Entries[VNVME_SUBMISSION_RING_SIZE];
} VNVME_SUBMISSION_RING, *PVNVME_SUBMISSION_RING;

/*===========================================================================
 * 完成环
 *===========================================================================*/

/**
 * @brief 完成环条目
 */
typedef struct _VNVME_COMPLETION_RING_ENTRY {
    UINT16 CommandId;                   // 对应的命令 ID
    UINT16 QueueId;                     // 队列 ID
    UINT32 Status;                      // NVMe 状态码
    UINT32 Result;                      // 命令特定结果 (DW0)
    UINT32 Reserved;
} VNVME_COMPLETION_RING_ENTRY, *PVNVME_COMPLETION_RING_ENTRY;

VNVME_STATIC_ASSERT(sizeof(VNVME_COMPLETION_RING_ENTRY) == 16, completion_ring_entry_size);

/**
 * @brief 完成环
 */
typedef struct _VNVME_COMPLETION_RING {
    volatile UINT32 Head;               // 消费者 (内核) 更新
    volatile UINT32 Tail;               // 生产者 (用户态) 更新
    UINT32 Size;                        // 环大小
    UINT32 Reserved;
    VNVME_COMPLETION_RING_ENTRY Entries[VNVME_COMPLETION_RING_SIZE];
} VNVME_COMPLETION_RING, *PVNVME_COMPLETION_RING;

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

#pragma pack(pop)

#endif /* _VNVME_COMMON_H_ */
