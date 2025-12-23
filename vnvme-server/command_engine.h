/**
 * @file command_engine.h
 * @brief NVMe 命令引擎接口
 * 
 * 命令引擎是 vnvme-server 的核心组件，负责:
 * - 从通知环读取命令通知
 * - 分发 Admin 和 I/O 命令到对应处理器
 * - 管理命令处理上下文
 * - 提供统一的初始化和运行接口
 */

#ifndef _COMMAND_ENGINE_H_
#define _COMMAND_ENGINE_H_

#include "types.h"
#include "admin_commands.h"
#include "io_commands.h"
#include "../include/vnvme_common.h"
#include "../include/nvme_spec.h"

// 前向声明
struct _BACKEND_CONTEXT;
typedef struct _BACKEND_CONTEXT BACKEND_CONTEXT, *PBACKEND_CONTEXT;

/*===========================================================================
 * 配置结构
 *===========================================================================*/

/**
 * @brief 命令引擎配置
 */
typedef struct _CMD_ENGINE_CONFIG {
    // 共享内存
    PVOID                   shmAddress;
    SIZE_T                  shmSize;
    
    // 存储后端
    PBACKEND_CONTEXT        backend;
    
    // 控制器配置
    char                    serialNumber[20];
    char                    modelNumber[40];
    char                    firmwareRevision[8];
    UINT16                  vendorId;
    
    // 命名空间配置
    struct {
        UINT64              size;           // 总块数
        UINT32              blockSize;      // 块大小
        BOOL                active;
        BOOL                readOnly;
    } namespaces[16];
    UINT32                  namespaceCount;
    
    // 选项
    UINT32                  maxIoQueues;
    BOOL                    debugMode;
} CMD_ENGINE_CONFIG, *PCMD_ENGINE_CONFIG;

/**
 * @brief 命令引擎统计
 */
typedef struct _CMD_ENGINE_STATS {
    UINT64                  adminCommandsProcessed;
    UINT64                  ioCommandsProcessed;
    UINT64                  bytesRead;
    UINT64                  bytesWritten;
    UINT64                  errors;
    UINT64                  pollCount;
    UINT64                  lastPollTimestamp;
} CMD_ENGINE_STATS, *PCMD_ENGINE_STATS;

/**
 * @brief 命令引擎上下文
 */
typedef struct _CMD_ENGINE_CONTEXT {
    // 状态
    BOOL                    initialized;
    BOOL                    running;
    
    // 共享内存
    PVOID                   shmAddress;
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm;
    
    // Admin 队列地址
    PNVME_COMMAND           adminSqBase;
    PNVME_COMPLETION        adminCqBase;
    
    // 数据缓冲区
    PVOID                   dataBuffer;
    UINT32                  dataBufferSize;
    
    // 子上下文
    ADMIN_CMD_CONTEXT       adminCtx;
    IO_CMD_CONTEXT          ioCtx;
    
    // 存储后端
    PBACKEND_CONTEXT        backend;
    
    // I/O 队列描述符
    PVNVME_QUEUE_DESCRIPTOR ioQueueDescriptors;
    
    // Admin 队列 Phase
    UINT32                  adminCqPhase;
    
    // 配置
    UINT32                  maxIoQueues;
    BOOL                    debugMode;
    
    // 统计
    CMD_ENGINE_STATS        stats;
} CMD_ENGINE_CONTEXT, *PCMD_ENGINE_CONTEXT;

/*===========================================================================
 * 初始化和清理
 *===========================================================================*/

/**
 * 创建命令引擎上下文
 * 
 * @return 新创建的上下文，失败返回 NULL
 */
PCMD_ENGINE_CONTEXT CmdEngineCreate(void);

/**
 * 销毁命令引擎上下文
 * 
 * @param pCtx  命令引擎上下文
 */
void CmdEngineDestroy(PCMD_ENGINE_CONTEXT pCtx);

/**
 * 初始化命令引擎
 * 
 * @param pCtx      命令引擎上下文
 * @param pConfig   配置
 * @return TRUE 成功
 */
BOOL CmdEngineInit(
    PCMD_ENGINE_CONTEXT pCtx,
    const CMD_ENGINE_CONFIG* pConfig
);

/**
 * 使用默认配置初始化命令引擎
 * 
 * @param pCtx          命令引擎上下文
 * @param shmAddress    共享内存地址
 * @param backend       存储后端
 * @return TRUE 成功
 */
BOOL CmdEngineInitSimple(
    PCMD_ENGINE_CONTEXT pCtx,
    PVOID shmAddress,
    PBACKEND_CONTEXT backend
);

/**
 * 设置调试模式
 */
void CmdEngineSetDebugMode(PCMD_ENGINE_CONTEXT pCtx, BOOL debugMode);

/*===========================================================================
 * 命令处理
 *===========================================================================*/

/**
 * 轮询并处理待处理命令
 * 
 * @param pCtx  命令引擎上下文
 * @return 处理的命令数
 */
UINT64 CmdEnginePoll(PCMD_ENGINE_CONTEXT pCtx);

/**
 * 运行命令处理循环
 * 
 * 此函数会阻塞直到:
 * - pRunning 变为 FALSE
 * - 发生不可恢复的错误
 * 
 * @param pCtx      命令引擎上下文
 * @param pRunning  运行标志 (设为 FALSE 停止)
 * @return TRUE 正常退出
 */
BOOL CmdEngineRun(PCMD_ENGINE_CONTEXT pCtx, volatile BOOL* pRunning);

/*===========================================================================
 * 统计和状态
 *===========================================================================*/

/**
 * 获取统计信息
 */
void CmdEngineGetStats(PCMD_ENGINE_CONTEXT pCtx, PCMD_ENGINE_STATS pStats);

/**
 * 重置统计信息
 */
void CmdEngineResetStats(PCMD_ENGINE_CONTEXT pCtx);

/**
 * 获取 Admin 命令处理器上下文
 */
PADMIN_CMD_CONTEXT CmdEngineGetAdminContext(PCMD_ENGINE_CONTEXT pCtx);

/**
 * 获取 I/O 命令处理器上下文
 */
PIO_CMD_CONTEXT CmdEngineGetIoContext(PCMD_ENGINE_CONTEXT pCtx);

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

/**
 * 处理单个 Admin 命令
 * 
 * @param pCtx  命令引擎上下文
 * @param pCmd  NVMe 命令
 * @return 1 成功处理, 0 失败
 */
UINT32 CmdEngineProcessAdminCommand(
    PCMD_ENGINE_CONTEXT pCtx,
    PNVME_COMMAND pCmd
);

/**
 * 处理单个 I/O 命令
 * 
 * @param pCtx      命令引擎上下文
 * @param pCmd      NVMe 命令
 * @param queueId   I/O SQ ID (1-based)
 * @return 1 成功处理, 0 失败
 */
UINT32 CmdEngineProcessIoCommand(
    PCMD_ENGINE_CONTEXT pCtx,
    PNVME_COMMAND pCmd,
    UINT16 queueId
);

/**
 * 写入 Admin 完成
 */
void CmdEnginePostAdminCompletion(
    PCMD_ENGINE_CONTEXT pCtx,
    UINT16 cid,
    UINT32 dw0,
    UINT8 sct,
    UINT8 sc
);

#endif /* _COMMAND_ENGINE_H_ */
