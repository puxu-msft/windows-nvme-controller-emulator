/**
 * @file admin_commands.h
 * @brief NVMe Admin 命令处理接口
 * 
 * 定义 Admin 命令处理函数，从 command_processor.c 拆分而来。
 * 支持的命令: Identify, Create/Delete I/O Queue, Get/Set Features 等。
 */

#ifndef _ADMIN_COMMANDS_H_
#define _ADMIN_COMMANDS_H_

#include "types.h"
#include "../include/vnvme_common.h"
#include "../include/nvme_spec.h"

//===========================================================================
// Admin 命令上下文
//===========================================================================

/**
 * @brief I/O 队列信息
 */
typedef struct _ADMIN_IO_QUEUE_INFO {
    BOOL        created;
    UINT32      capacity;
    UINT32      cqId;                   // 关联的 CQ ID (仅 SQ)
    UINT32      phase;                  // Phase Tag (仅 CQ)
    PNVME_COMMAND sqBase;               // SQ 基地址
    PNVME_COMPLETION cqBase;            // CQ 基地址
    PVOID       descriptor;             // 描述符指针
} ADMIN_IO_QUEUE_INFO, *PADMIN_IO_QUEUE_INFO;

/**
 * @brief 控制器信息 (用于 Identify)
 */
typedef struct _ADMIN_CONTROLLER_INFO {
    char        serialNumber[20];
    char        modelNumber[40];
    char        firmwareRevision[8];
    UINT16      vendorId;
    UINT32      maxNamespaces;
    UINT32      maxQueueEntries;
} ADMIN_CONTROLLER_INFO, *PADMIN_CONTROLLER_INFO;

/**
 * @brief 命名空间信息 (用于 Identify)
 */
typedef struct _ADMIN_NAMESPACE_INFO {
    UINT32      nsid;
    UINT64      size;                   // 总块数
    UINT64      capacity;               // 可用块数
    UINT32      blockSize;              // 块大小 (字节)
    UINT32      metadataSize;           // 元数据大小
    BOOL        active;
} ADMIN_NAMESPACE_INFO, *PADMIN_NAMESPACE_INFO;

/**
 * @brief Admin 命令处理上下文
 */
typedef struct _ADMIN_CMD_CONTEXT {
    // 共享内存
    PVOID                   shmBase;
    PNVME_COMMAND           adminSqBase;
    PNVME_COMPLETION        adminCqBase;
    UINT32                  adminCqPhase;
    
    // I/O 队列管理
    ADMIN_IO_QUEUE_INFO     ioSq[16];   // I/O SQ 数组
    ADMIN_IO_QUEUE_INFO     ioCq[16];   // I/O CQ 数组
    UINT32                  maxIoQueues;
    
    // 控制器和命名空间信息
    ADMIN_CONTROLLER_INFO   controller;
    ADMIN_NAMESPACE_INFO    namespaces[16];
    UINT32                  namespaceCount;
    
    // 数据缓冲区 (用于 Identify 等需要返回数据的命令)
    PVOID                   dataBuffer;
    UINT32                  dataBufferSize;
    
    // Features 值
    UINT32                  numQueues;          // Number of Queues (Feature 0x07)
    UINT32                  arbitration;        // Arbitration (Feature 0x01)
    UINT32                  powerManagement;    // Power Management (Feature 0x02)
    UINT32                  volatileWriteCache; // Volatile Write Cache (Feature 0x06)
    
    // 统计
    UINT64                  commandsProcessed;
    UINT64                  errors;
} ADMIN_CMD_CONTEXT, *PADMIN_CMD_CONTEXT;

//===========================================================================
// 初始化和清理
//===========================================================================

/**
 * 初始化 Admin 命令上下文
 * 
 * @param pCtx          Admin 命令上下文
 * @param shmBase       共享内存基地址
 * @param adminSqBase   Admin SQ 基地址
 * @param adminCqBase   Admin CQ 基地址
 * @param dataBuffer    数据缓冲区
 * @param dataBufferSize 数据缓冲区大小
 * @return TRUE 成功
 */
BOOL AdminCmdInit(
    PADMIN_CMD_CONTEXT pCtx,
    PVOID shmBase,
    PNVME_COMMAND adminSqBase,
    PNVME_COMPLETION adminCqBase,
    PVOID dataBuffer,
    UINT32 dataBufferSize
);

/**
 * 设置控制器信息
 */
void AdminCmdSetControllerInfo(
    PADMIN_CMD_CONTEXT pCtx,
    const ADMIN_CONTROLLER_INFO* pInfo
);

/**
 * 添加命名空间
 */
BOOL AdminCmdAddNamespace(
    PADMIN_CMD_CONTEXT pCtx,
    const ADMIN_NAMESPACE_INFO* pNsInfo
);

//===========================================================================
// 命令处理
//===========================================================================

/**
 * 处理 Admin 命令
 * 
 * @param pCtx  Admin 命令上下文
 * @param pCmd  NVMe 命令
 * @return 处理的命令数 (0 或 1)
 */
UINT32 AdminCmdProcess(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd);

/**
 * 写入 Admin 完成
 * 
 * @param pCtx      Admin 命令上下文
 * @param cid       命令 ID
 * @param result    命令特定结果
 * @param sqHead    SQ Head 指针
 * @param status    状态码
 */
void AdminCmdPostCompletion(
    PADMIN_CMD_CONTEXT pCtx,
    UINT16 cid,
    UINT32 result,
    UINT16 sqHead,
    UINT16 status
);

//===========================================================================
// 单独命令处理函数 (可单独测试)
//===========================================================================

/**
 * 处理 Identify 命令 (Opcode 0x06)
 */
UINT16 AdminCmdIdentify(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Identify Controller (CNS=1)
 */
UINT16 AdminCmdIdentifyController(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Identify Namespace (CNS=0)
 */
UINT16 AdminCmdIdentifyNamespace(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Identify Active Namespace List (CNS=2)
 */
UINT16 AdminCmdIdentifyNamespaceList(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Create I/O Completion Queue (Opcode 0x05)
 */
UINT16 AdminCmdCreateIoCq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Create I/O Submission Queue (Opcode 0x01)
 */
UINT16 AdminCmdCreateIoSq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Delete I/O Completion Queue (Opcode 0x04)
 */
UINT16 AdminCmdDeleteIoCq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Delete I/O Submission Queue (Opcode 0x00)
 */
UINT16 AdminCmdDeleteIoSq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Get Features (Opcode 0x0A)
 */
UINT16 AdminCmdGetFeatures(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Set Features (Opcode 0x09)
 */
UINT16 AdminCmdSetFeatures(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Abort (Opcode 0x08)
 */
UINT16 AdminCmdAbort(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Get Log Page (Opcode 0x02)
 */
UINT16 AdminCmdGetLogPage(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

/**
 * 处理 Keep Alive (Opcode 0x18)
 */
UINT16 AdminCmdKeepAlive(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
);

//===========================================================================
// I/O 队列查询 (供 I/O 命令处理使用)
//===========================================================================

/**
 * 获取 I/O SQ 信息
 */
PADMIN_IO_QUEUE_INFO AdminCmdGetIoSq(PADMIN_CMD_CONTEXT pCtx, UINT16 qid);

/**
 * 获取 I/O CQ 信息
 */
PADMIN_IO_QUEUE_INFO AdminCmdGetIoCq(PADMIN_CMD_CONTEXT pCtx, UINT16 qid);

/**
 * 检查 I/O 队列是否已创建
 */
BOOL AdminCmdIsIoQueueCreated(PADMIN_CMD_CONTEXT pCtx, UINT16 sqid);

#endif /* _ADMIN_COMMANDS_H_ */
