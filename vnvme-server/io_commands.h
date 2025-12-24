/**
 * @file io_commands.h
 * @brief NVMe I/O 命令处理接口
 * 
 * 定义 I/O 命令处理函数，从 command_processor.c 拆分而来。
 * 支持的命令: Read, Write, Flush, Write Zeroes, Dataset Management
 */

#ifndef _IO_COMMANDS_H_
#define _IO_COMMANDS_H_

#include "types.h"
#include "admin_commands.h"
#include "../include/nvme_spec.h"

// 前向声明
struct _BACKEND_CONTEXT;
typedef struct _BACKEND_CONTEXT BACKEND_CONTEXT, *PBACKEND_CONTEXT;

//===========================================================================
// I/O 命令上下文
//===========================================================================

/**
 * @brief I/O 统计信息
 */
typedef struct _IO_STATS {
    UINT64      commandsProcessed;
    UINT64      bytesRead;
    UINT64      bytesWritten;
    UINT64      readCommands;
    UINT64      writeCommands;
    UINT64      flushCommands;
    UINT64      writeZeroesCommands;
    UINT64      errors;
} IO_STATS, *PIO_STATS;

/**
 * @brief 命名空间配置 (用于 I/O 命令处理)
 */
typedef struct _IO_NAMESPACE_CONFIG {
    UINT32      nsid;
    UINT64      size;               // 总块数
    UINT32      blockSize;          // 块大小 (字节)
    BOOL        active;
    BOOL        readOnly;
} IO_NAMESPACE_CONFIG, *PIO_NAMESPACE_CONFIG;

/**
 * @brief I/O 命令处理上下文
 */
typedef struct _IO_CMD_CONTEXT {
    // Admin 命令上下文引用 (用于获取队列信息)
    PADMIN_CMD_CONTEXT      adminCtx;
    
    // 存储后端
    PBACKEND_CONTEXT        backend;
    
    // 共享内存
    PVOID                   shmBase;
    
    // 数据缓冲区 (用于读写数据)
    PVOID                   dataBuffer;
    UINT32                  dataBufferSize;
    
    // 命名空间配置
    IO_NAMESPACE_CONFIG     namespaces[16];
    UINT32                  namespaceCount;
    
    // 统计
    IO_STATS                stats;
    
    // 调试标志
    BOOL                    debugMode;
} IO_CMD_CONTEXT, *PIO_CMD_CONTEXT;

//===========================================================================
// 初始化和配置
//===========================================================================

/**
 * 初始化 I/O 命令上下文
 * 
 * @param pCtx          I/O 命令上下文
 * @param pAdminCtx     Admin 命令上下文 (用于队列管理)
 * @param backend       存储后端
 * @param shmBase       共享内存基地址
 * @param dataBuffer    数据缓冲区
 * @param dataBufferSize 数据缓冲区大小
 * @return TRUE 成功
 */
BOOL IoCmdInit(
    PIO_CMD_CONTEXT pCtx,
    PADMIN_CMD_CONTEXT pAdminCtx,
    PBACKEND_CONTEXT backend,
    PVOID shmBase,
    PVOID dataBuffer,
    UINT32 dataBufferSize
);

/**
 * 添加命名空间配置
 */
BOOL IoCmdAddNamespace(
    PIO_CMD_CONTEXT pCtx,
    const IO_NAMESPACE_CONFIG* pNsConfig
);

/**
 * 设置调试模式
 */
void IoCmdSetDebugMode(PIO_CMD_CONTEXT pCtx, BOOL debugMode);

/**
 * 获取统计信息
 */
void IoCmdGetStats(PIO_CMD_CONTEXT pCtx, PIO_STATS pStats);

/**
 * 重置统计信息
 */
void IoCmdResetStats(PIO_CMD_CONTEXT pCtx);

//===========================================================================
// 完成写入
//===========================================================================

/**
 * @brief 写入 I/O 完成
 * 
 * @param pCtx      I/O 命令上下文
 * @param queueId   I/O SQ ID (1-based)
 * @param cid       命令 ID
 * @param result    命令特定结果 (DW0)
 * @param sct       状态码类型
 * @param sc        状态码
 */
void IoCmdPostCompletion(
    PIO_CMD_CONTEXT pCtx,
    UINT16 queueId,
    UINT16 cid,
    UINT32 result,
    UINT8 sct,
    UINT8 sc
);

//===========================================================================
// 命令处理
//===========================================================================

/**
 * 处理 I/O 命令
 * 
 * @param pCtx      I/O 命令上下文
 * @param pCmd      NVMe 命令
 * @param queueId   I/O SQ ID (1-based)
 * @return 处理的命令数 (0 或 1)
 */
UINT32 IoCmdProcess(
    PIO_CMD_CONTEXT pCtx, 
    const NVME_COMMAND* pCmd, 
    UINT16 queueId
);

//===========================================================================
// 单独命令处理函数
//===========================================================================

/**
 * 处理 Read 命令 (Opcode 0x02)
 * 
 * @param pCtx      I/O 命令上下文
 * @param pCmd      NVMe 命令
 * @param queueId   I/O SQ ID
 * @return NVMe 状态 (包含 Phase)
 */
UINT16 IoCmdRead(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
);

/**
 * 处理 Write 命令 (Opcode 0x01)
 */
UINT16 IoCmdWrite(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
);

/**
 * 处理 Flush 命令 (Opcode 0x00)
 */
UINT16 IoCmdFlush(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
);

/**
 * 处理 Write Zeroes 命令 (Opcode 0x08)
 */
UINT16 IoCmdWriteZeroes(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
);

/**
 * 处理 Dataset Management 命令 (Opcode 0x09)
 * 用于 TRIM/Deallocate 操作
 */
UINT16 IoCmdDatasetManagement(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
);

//===========================================================================
// 辅助函数
//===========================================================================

/**
 * 验证 LBA 范围
 * 
 * @param pCtx      I/O 命令上下文
 * @param nsid      Namespace ID (1-based)
 * @param slba      起始 LBA
 * @param nlb       块数
 * @return TRUE 如果范围有效
 */
BOOL IoCmdValidateLbaRange(
    PIO_CMD_CONTEXT pCtx,
    UINT32 nsid,
    UINT64 slba,
    UINT32 nlb
);

/**
 * 获取命名空间的块大小
 */
UINT32 IoCmdGetBlockSize(PIO_CMD_CONTEXT pCtx, UINT32 nsid);

/**
 * 检查命名空间是否有效
 */
BOOL IoCmdIsNamespaceValid(PIO_CMD_CONTEXT pCtx, UINT32 nsid);

#endif /* _IO_COMMANDS_H_ */
