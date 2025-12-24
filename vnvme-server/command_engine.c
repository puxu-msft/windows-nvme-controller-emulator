/**
 * @file command_engine.c
 * @brief NVMe 命令引擎实现
 * 
 * 命令引擎是 vnvme-server 的核心组件，负责:
 * - 从通知环读取命令通知
 * - 分发 Admin 和 I/O 命令到对应处理器
 * - 管理命令处理上下文
 * - 提供统一的初始化和运行接口
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "command_engine.h"
#include "backend.h"

//===========================================================================
// 外部声明
//===========================================================================

extern BOOL g_DebugMode;
extern PVNVME_SHM_CONTROL_BLOCK VnvmeGetControlBlock(PVOID shmAddress);
UINT64 BackendGetSize(PBACKEND_CONTEXT ctx);

//===========================================================================
// 辅助函数
//===========================================================================

/**
 * @brief 构造 NVMe 状态字段
 */
static UINT16 MakeStatus(UINT8 sct, UINT8 sc, UINT8 phase)
{
    return (UINT16)(phase | (sc << 1) | (sct << 9));
}

//===========================================================================
// 创建和销毁
//===========================================================================

/**
 * 创建命令引擎上下文
 */
PCMD_ENGINE_CONTEXT CmdEngineCreate(void)
{
    PCMD_ENGINE_CONTEXT pCtx;
    
    pCtx = (PCMD_ENGINE_CONTEXT)malloc(sizeof(CMD_ENGINE_CONTEXT));
    if (pCtx == NULL) {
        return NULL;
    }
    
    memset(pCtx, 0, sizeof(CMD_ENGINE_CONTEXT));
    
    return pCtx;
}

/**
 * 销毁命令引擎上下文
 */
void CmdEngineDestroy(PCMD_ENGINE_CONTEXT pCtx)
{
    if (pCtx != NULL) {
        free(pCtx);
    }
}

//===========================================================================
// 初始化
//===========================================================================

/**
 * 初始化命令引擎
 */
BOOL CmdEngineInit(
    PCMD_ENGINE_CONTEXT pCtx,
    const CMD_ENGINE_CONFIG* pConfig
)
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    UINT32 i;
    
    if (pCtx == NULL || pConfig == NULL || pConfig->shmAddress == NULL) {
        return FALSE;
    }
    
    // 获取共享内存控制块
    shm = VnvmeGetControlBlock(pConfig->shmAddress);
    if (shm->Magic != VNVME_SHM_MAGIC) {
        fprintf(stderr, "CmdEngineInit: Invalid SHM magic (got 0x%X, expected 0x%X)\n",
                shm->Magic, VNVME_SHM_MAGIC);
        return FALSE;
    }
    
    // 保存引用
    pCtx->shmAddress = pConfig->shmAddress;
    pCtx->shm = shm;
    pCtx->backend = pConfig->backend;
    pCtx->maxIoQueues = pConfig->maxIoQueues > 0 ? pConfig->maxIoQueues : 16;
    pCtx->debugMode = pConfig->debugMode;
    
    // 计算 Admin 队列地址
    pCtx->adminSqBase = (PNVME_COMMAND)((PUCHAR)pConfig->shmAddress + shm->AdminSQ.Offset);
    pCtx->adminCqBase = (PNVME_COMPLETION)((PUCHAR)pConfig->shmAddress + shm->AdminCQ.Offset);
    pCtx->adminCqPhase = 1;
    
    // 获取 I/O 队列描述符
    if (shm->IoQueueDescriptorOffset != 0) {
        pCtx->ioQueueDescriptors = (PVNVME_QUEUE_DESCRIPTOR)(
            (PUCHAR)pConfig->shmAddress + shm->IoQueueDescriptorOffset);
    }
    
    // 获取数据缓冲区
    if (shm->DataBufferOffset != 0 && shm->DataBufferSize != 0) {
        pCtx->dataBuffer = (PUCHAR)pConfig->shmAddress + shm->DataBufferOffset;
        pCtx->dataBufferSize = shm->DataBufferSize;
    }
    
    // 初始化 Admin 命令上下文
    if (!AdminCmdInit(&pCtx->adminCtx, pConfig->shmAddress,
                      pCtx->adminSqBase, pCtx->adminCqBase,
                      pCtx->dataBuffer, pCtx->dataBufferSize)) {
        fprintf(stderr, "CmdEngineInit: Failed to initialize admin command context\n");
        return FALSE;
    }
    
    // 设置控制器信息
    {
        ADMIN_CONTROLLER_INFO ctrlInfo = {0};
        strncpy(ctrlInfo.serialNumber, pConfig->serialNumber, 20);
        strncpy(ctrlInfo.modelNumber, pConfig->modelNumber, 40);
        strncpy(ctrlInfo.firmwareRevision, pConfig->firmwareRevision, 8);
        ctrlInfo.vendorId = pConfig->vendorId;
        ctrlInfo.maxNamespaces = 16;
        ctrlInfo.maxQueueEntries = 64;
        AdminCmdSetControllerInfo(&pCtx->adminCtx, &ctrlInfo);
    }
    
    // 添加命名空间到 Admin 上下文
    for (i = 0; i < pConfig->namespaceCount && i < 16; i++) {
        ADMIN_NAMESPACE_INFO nsInfo = {0};
        nsInfo.nsid = i + 1;
        nsInfo.size = pConfig->namespaces[i].size;
        nsInfo.capacity = pConfig->namespaces[i].size;
        nsInfo.blockSize = pConfig->namespaces[i].blockSize;
        nsInfo.active = pConfig->namespaces[i].active;
        AdminCmdAddNamespace(&pCtx->adminCtx, &nsInfo);
    }
    
    // 初始化 I/O 命令上下文
    if (!IoCmdInit(&pCtx->ioCtx, &pCtx->adminCtx, pConfig->backend,
                   pConfig->shmAddress, pCtx->dataBuffer, pCtx->dataBufferSize)) {
        fprintf(stderr, "CmdEngineInit: Failed to initialize I/O command context\n");
        return FALSE;
    }
    
    // 添加命名空间到 I/O 上下文
    for (i = 0; i < pConfig->namespaceCount && i < 16; i++) {
        IO_NAMESPACE_CONFIG nsConfig = {0};
        nsConfig.nsid = i + 1;
        nsConfig.size = pConfig->namespaces[i].size;
        nsConfig.blockSize = pConfig->namespaces[i].blockSize;
        nsConfig.active = pConfig->namespaces[i].active;
        nsConfig.readOnly = pConfig->namespaces[i].readOnly;
        IoCmdAddNamespace(&pCtx->ioCtx, &nsConfig);
    }
    
    IoCmdSetDebugMode(&pCtx->ioCtx, pConfig->debugMode);
    
    pCtx->initialized = TRUE;
    
    if (pConfig->debugMode) {
        printf("Command engine initialized\n");
        printf("  Admin SQ at offset 0x%X\n", shm->AdminSQ.Offset);
        printf("  Admin CQ at offset 0x%X\n", shm->AdminCQ.Offset);
        printf("  Data buffer: offset 0x%X, size %u bytes\n",
               shm->DataBufferOffset, shm->DataBufferSize);
        printf("  Namespaces: %u\n", pConfig->namespaceCount);
    }
    
    return TRUE;
}

/**
 * 使用默认配置初始化命令引擎
 */
BOOL CmdEngineInitSimple(
    PCMD_ENGINE_CONTEXT pCtx,
    PVOID shmAddress,
    PBACKEND_CONTEXT backend
)
{
    CMD_ENGINE_CONFIG config = {0};
    UINT64 backendSize;
    
    config.shmAddress = shmAddress;
    config.backend = backend;
    
    // 默认控制器信息
    strncpy(config.serialNumber, "VNVME0001234567890", 20);
    strncpy(config.modelNumber, "Virtual NVMe Controller", 40);
    strncpy(config.firmwareRevision, "1.0.0", 8);
    config.vendorId = 0x1234;
    
    // 配置命名空间
    config.namespaceCount = 1;
    config.namespaces[0].blockSize = 512;
    config.namespaces[0].active = TRUE;
    config.namespaces[0].readOnly = FALSE;
    
    // 根据后端大小计算命名空间大小
    if (backend != NULL) {
        backendSize = BackendGetSize(backend);
        config.namespaces[0].size = backendSize / 512;
    } else {
        config.namespaces[0].size = 128 * 1024;  // 64MB @ 512B
    }
    
    config.maxIoQueues = 16;
    config.debugMode = g_DebugMode;
    
    return CmdEngineInit(pCtx, &config);
}

/**
 * 设置调试模式
 */
void CmdEngineSetDebugMode(PCMD_ENGINE_CONTEXT pCtx, BOOL debugMode)
{
    if (pCtx != NULL) {
        pCtx->debugMode = debugMode;
        IoCmdSetDebugMode(&pCtx->ioCtx, debugMode);
    }
}

//===========================================================================
// 完成写入
//===========================================================================

/**
 * 写入 Admin 完成
 */
void CmdEnginePostAdminCompletion(
    PCMD_ENGINE_CONTEXT pCtx,
    UINT16 cid,
    UINT32 dw0,
    UINT8 sct,
    UINT8 sc
)
{
    PNVME_COMPLETION cqe;
    UINT32 tail;
    
    if (pCtx == NULL || pCtx->shm == NULL) {
        return;
    }
    
    tail = pCtx->shm->AdminCQ.Tail;
    cqe = &pCtx->adminCqBase[tail];
    
    cqe->DW0 = dw0;
    cqe->DW1 = 0;
    cqe->SQHD = (UINT16)pCtx->shm->AdminSQ.Head;
    cqe->SQID = 0;
    cqe->CID = cid;
    cqe->Status = MakeStatus(sct, sc, (UINT8)pCtx->adminCqPhase);
    
    // 更新 Tail
    tail = (tail + 1) % pCtx->shm->AdminCQ.Capacity;
    if (tail == 0) {
        pCtx->adminCqPhase = !pCtx->adminCqPhase;
    }
    pCtx->shm->AdminCQ.Tail = tail;
    
    pCtx->stats.adminCommandsProcessed++;
}

//===========================================================================
// 命令处理
//===========================================================================

/**
 * 处理单个 Admin 命令
 */
UINT32 CmdEngineProcessAdminCommand(
    PCMD_ENGINE_CONTEXT pCtx,
    PNVME_COMMAND pCmd
)
{
    UINT32 result = 0;
    UINT16 status;
    UINT8 sct, sc;
    
    if (pCtx == NULL || pCmd == NULL) {
        return 0;
    }
    
    if (pCtx->debugMode) {
        printf("Admin Command: OPC=0x%02X, CID=%u, NSID=%u\n",
               pCmd->OPC, pCmd->CID, pCmd->NSID);
    }
    
    // 使用 Admin 命令处理器处理命令
    // AdminCmdProcess 会调用 AdminCmdPostCompletion，但我们需要自己的完成写入
    // 为了兼容，这里我们直接调用分发逻辑
    
    switch (pCmd->OPC) {
        case NVME_ADMIN_IDENTIFY:
            status = AdminCmdIdentify(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_CREATE_IO_CQ:
            status = AdminCmdCreateIoCq(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_CREATE_IO_SQ:
            status = AdminCmdCreateIoSq(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_DELETE_IO_CQ:
            status = AdminCmdDeleteIoCq(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_DELETE_IO_SQ:
            status = AdminCmdDeleteIoSq(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_SET_FEATURES:
            status = AdminCmdSetFeatures(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_GET_FEATURES:
            status = AdminCmdGetFeatures(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_ABORT:
            status = AdminCmdAbort(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_GET_LOG_PAGE:
            status = AdminCmdGetLogPage(&pCtx->adminCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_KEEP_ALIVE:
            status = AdminCmdKeepAlive(&pCtx->adminCtx, pCmd, &result);
            break;
            
        default:
            if (pCtx->debugMode) {
                printf("  Unknown Admin opcode 0x%02X\n", pCmd->OPC);
            }
            result = 0;
            status = MakeStatus(0, 0x01, (UINT8)pCtx->adminCqPhase);  // Invalid Opcode
            pCtx->stats.errors++;
            break;
    }
    
    // 解析状态并写入完成
    sct = (UINT8)((status >> 9) & 0x07);
    sc = (UINT8)((status >> 1) & 0xFF);
    
    CmdEnginePostAdminCompletion(pCtx, pCmd->CID, result, sct, sc);
    
    return 1;
}

/**
 * 处理单个 I/O 命令
 */
UINT32 CmdEngineProcessIoCommand(
    PCMD_ENGINE_CONTEXT pCtx,
    PNVME_COMMAND pCmd,
    UINT16 queueId
)
{
    if (pCtx == NULL || pCmd == NULL) {
        return 0;
    }
    
    // 使用 I/O 命令处理器处理
    return IoCmdProcess(&pCtx->ioCtx, pCmd, queueId);
}

//===========================================================================
// 轮询和主循环
//===========================================================================

/**
 * 轮询并处理待处理命令
 */
UINT64 CmdEnginePoll(PCMD_ENGINE_CONTEXT pCtx)
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_NOTIFY_RING notifyRing;
    UINT32 head, tail;
    UINT64 processed = 0;
    
    if (pCtx == NULL || !pCtx->initialized) {
        return 0;
    }
    
    shm = pCtx->shm;
    
    // 检查通知环
    if (shm->NotifyRingOffset == 0) {
        return 0;
    }
    
    notifyRing = (PVNVME_NOTIFY_RING)((PUCHAR)shm + shm->NotifyRingOffset);
    head = notifyRing->Head;
    tail = notifyRing->Tail;
    
    while (head != tail) {
        PVNVME_NOTIFY_ENTRY entry = &notifyRing->Entries[head];
        
        if (entry->Type == 0) {
            // SQ 有新命令
            if (entry->QueueId == 0) {
                // Admin 队列
                UINT32 sqHead = shm->AdminSQ.Head;
                UINT32 sqTail = entry->Index;
                UINT32 sqCap = shm->AdminSQ.Capacity;
                
                while (sqHead != sqTail) {
                    PNVME_COMMAND cmd = &pCtx->adminSqBase[sqHead];
                    if (cmd != NULL) {
                        CmdEngineProcessAdminCommand(pCtx, cmd);
                        processed++;
                    }
                    sqHead = (sqHead + 1) % sqCap;
                }
                
                shm->AdminSQ.Head = sqHead;
            } else {
                // I/O 队列
                UINT16 queueId = entry->QueueId;
                PADMIN_IO_QUEUE_INFO sqInfo;
                
                sqInfo = AdminCmdGetIoSq(&pCtx->adminCtx, queueId);
                
                if (sqInfo != NULL && sqInfo->created && sqInfo->sqBase != NULL) {
                    PVNVME_QUEUE_DESCRIPTOR sqDesc = (PVNVME_QUEUE_DESCRIPTOR)sqInfo->descriptor;
                    
                    if (sqDesc != NULL) {
                        UINT32 sqHead = sqDesc->Head;
                        UINT32 sqTail = entry->Index;
                        UINT32 sqCap = sqInfo->capacity;
                        
                        while (sqHead != sqTail) {
                            PNVME_COMMAND cmd = &sqInfo->sqBase[sqHead];
                            CmdEngineProcessIoCommand(pCtx, cmd, queueId);
                            processed++;
                            sqHead = (sqHead + 1) % sqCap;
                        }
                        
                        sqDesc->Head = sqHead;
                    }
                }
            }
        }
        
        head = (head + 1) % notifyRing->Size;
    }
    
    // 更新 Head
    notifyRing->Head = head;
    
    // 更新共享内存统计
    if (processed > 0) {
        IO_STATS ioStats;
        IoCmdGetStats(&pCtx->ioCtx, &ioStats);
        
        shm->CommandsProcessed = pCtx->stats.adminCommandsProcessed + ioStats.commandsProcessed;
        shm->BytesRead = ioStats.bytesRead;
        shm->BytesWritten = ioStats.bytesWritten;
        
        pCtx->stats.ioCommandsProcessed = ioStats.commandsProcessed;
        pCtx->stats.bytesRead = ioStats.bytesRead;
        pCtx->stats.bytesWritten = ioStats.bytesWritten;
    }
    
    pCtx->stats.pollCount++;
    
    return processed;
}

/**
 * 运行命令处理循环
 * 
 * 支持两种模式:
 * 1. 轮询模式 (默认): 持续轮询 + 短暂 Sleep
 * 2. 事件等待模式: 使用 WaitForSingleObject 等待命令就绪事件
 */
BOOL CmdEngineRun(PCMD_ENGINE_CONTEXT pCtx, volatile BOOL* pRunning)
{
    if (pCtx == NULL || !pCtx->initialized || pRunning == NULL) {
        return FALSE;
    }
    
    pCtx->running = TRUE;
    
    while (*pRunning) {
        UINT64 processed = CmdEnginePoll(pCtx);
        
        if (processed == 0) {
            // 没有命令处理
            if (pCtx->useEventWait && pCtx->commandEvent != NULL) {
                // 事件等待模式: 等待内核信号或超时
                DWORD waitResult = WaitForSingleObject(pCtx->commandEvent, 10);
                if (waitResult == WAIT_OBJECT_0) {
                    // 事件被触发，继续处理
                    pCtx->stats.eventWakeups++;
                }
            } else {
                // 轮询模式: 短暂休眠
                Sleep(1);
            }
        }
    }
    
    pCtx->running = FALSE;
    
    return TRUE;
}

//===========================================================================
// 统计和状态
//===========================================================================

/**
 * 获取统计信息
 */
void CmdEngineGetStats(PCMD_ENGINE_CONTEXT pCtx, PCMD_ENGINE_STATS pStats)
{
    if (pCtx == NULL || pStats == NULL) {
        return;
    }
    
    memcpy(pStats, &pCtx->stats, sizeof(CMD_ENGINE_STATS));
}

/**
 * 重置统计信息
 */
void CmdEngineResetStats(PCMD_ENGINE_CONTEXT pCtx)
{
    if (pCtx != NULL) {
        memset(&pCtx->stats, 0, sizeof(CMD_ENGINE_STATS));
        IoCmdResetStats(&pCtx->ioCtx);
    }
}

/**
 * 设置命令就绪事件
 */
void CmdEngineSetCommandEvent(PCMD_ENGINE_CONTEXT pCtx, HANDLE hEvent)
{
    if (pCtx != NULL) {
        pCtx->commandEvent = hEvent;
        pCtx->useEventWait = (hEvent != NULL);
        
        if (pCtx->useEventWait) {
            printf("Command engine: Event wait mode enabled\n");
        }
    }
}

/**
 * 获取 Admin 命令处理器上下文
 */
PADMIN_CMD_CONTEXT CmdEngineGetAdminContext(PCMD_ENGINE_CONTEXT pCtx)
{
    if (pCtx == NULL) {
        return NULL;
    }
    return &pCtx->adminCtx;
}

/**
 * 获取 I/O 命令处理器上下文
 */
PIO_CMD_CONTEXT CmdEngineGetIoContext(PCMD_ENGINE_CONTEXT pCtx)
{
    if (pCtx == NULL) {
        return NULL;
    }
    return &pCtx->ioCtx;
}
