/**
 * @file admin_commands.c
 * @brief NVMe Admin 命令处理实现
 * 
 * 实现 Admin 命令处理函数，从 command_processor.c 拆分而来。
 * 
 * 支持的命令:
 * - Identify (Controller, Namespace, Active NS List)
 * - Create/Delete I/O Completion Queue
 * - Create/Delete I/O Submission Queue
 * - Get/Set Features
 * - Abort
 * - Keep Alive
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "admin_commands.h"

//===========================================================================
// 外部声明
//===========================================================================

extern BOOL g_DebugMode;

// VnvmeGetControlBlock, VnvmeGetIoSQ, VnvmeGetIoCQ 已在 vnvme_common.h 中定义为内联函数

//===========================================================================
// 辅助宏和常量
//===========================================================================

#define ADMIN_MAX_IO_QUEUES     16

/**
 * @brief 构造 NVMe 状态字段
 */
static UINT16 MakeStatus(UINT32 sct, UINT32 sc, UINT32 phase)
{
    return (UINT16)((phase & 1) | ((sc & 0xFF) << 1) | ((sct & 0x7) << 9));
}

//===========================================================================
// 初始化和清理
//===========================================================================

/**
 * 初始化 Admin 命令上下文
 */
BOOL AdminCmdInit(
    PADMIN_CMD_CONTEXT pCtx,
    PVOID shmBase,
    PNVME_COMMAND adminSqBase,
    PNVME_COMPLETION adminCqBase,
    PVOID dataBuffer,
    UINT32 dataBufferSize
)
{
    if (pCtx == NULL) {
        return FALSE;
    }
    
    memset(pCtx, 0, sizeof(ADMIN_CMD_CONTEXT));
    
    pCtx->shmBase = shmBase;
    pCtx->adminSqBase = adminSqBase;
    pCtx->adminCqBase = adminCqBase;
    pCtx->adminCqPhase = 1;
    pCtx->dataBuffer = dataBuffer;
    pCtx->dataBufferSize = dataBufferSize;
    pCtx->maxIoQueues = ADMIN_MAX_IO_QUEUES;
    
    // 设置默认的 Features 值
    pCtx->numQueues = ADMIN_MAX_IO_QUEUES - 1;
    pCtx->arbitration = 0;
    pCtx->powerManagement = 0;
    pCtx->volatileWriteCache = 1;
    
    // 设置默认控制器信息
    strncpy(pCtx->controller.serialNumber, "VNVME0001234567890", 20);
    strncpy(pCtx->controller.modelNumber, "Virtual NVMe Controller", 40);
    strncpy(pCtx->controller.firmwareRevision, "1.0.0", 8);
    pCtx->controller.vendorId = 0x1234;
    pCtx->controller.maxNamespaces = 16;
    pCtx->controller.maxQueueEntries = 64;
    
    return TRUE;
}

/**
 * 设置控制器信息
 */
void AdminCmdSetControllerInfo(
    PADMIN_CMD_CONTEXT pCtx,
    const ADMIN_CONTROLLER_INFO* pInfo
)
{
    if (pCtx == NULL || pInfo == NULL) {
        return;
    }
    
    memcpy(&pCtx->controller, pInfo, sizeof(ADMIN_CONTROLLER_INFO));
}

/**
 * 添加命名空间
 */
BOOL AdminCmdAddNamespace(
    PADMIN_CMD_CONTEXT pCtx,
    const ADMIN_NAMESPACE_INFO* pNsInfo
)
{
    if (pCtx == NULL || pNsInfo == NULL) {
        return FALSE;
    }
    
    if (pCtx->namespaceCount >= 16) {
        return FALSE;
    }
    
    memcpy(&pCtx->namespaces[pCtx->namespaceCount], pNsInfo, sizeof(ADMIN_NAMESPACE_INFO));
    pCtx->namespaceCount++;
    
    return TRUE;
}

//===========================================================================
// 完成写入
//===========================================================================

/**
 * 写入 Admin 完成
 */
void AdminCmdPostCompletion(
    PADMIN_CMD_CONTEXT pCtx,
    UINT16 cid,
    UINT32 result,
    UINT16 sqHead,
    UINT16 status
)
{
    PNVME_COMPLETION cqe;
    
    if (pCtx == NULL || pCtx->adminCqBase == NULL) {
        return;
    }
    
    // 简化实现 - 假设 CQ 指针由外部管理
    // 实际使用时需要与共享内存的队列描述符同步
    // 这里仅写入完成条目
    
    cqe = pCtx->adminCqBase;
    cqe->DW0 = result;
    cqe->DW1 = 0;
    cqe->SQHD = sqHead;
    cqe->SQID = 0;
    cqe->CID = cid;
    cqe->Status = status;
    
    pCtx->commandsProcessed++;
}

//===========================================================================
// Identify 命令实现
//===========================================================================

/**
 * 处理 Identify Controller (CNS=1)
 */
UINT16 AdminCmdIdentifyController(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    (void)pCmd;
    
    if (pCtx == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);  // Invalid Field
    }
    
    // 如果有数据缓冲区，填充 Identify Controller 数据结构
    if (pCtx->dataBuffer != NULL && pCtx->dataBufferSize >= 4096) {
        PUCHAR data = (PUCHAR)pCtx->dataBuffer;
        memset(data, 0, 4096);
        
        // VID (Bytes 0-1)
        *(PUINT16)(data + 0) = pCtx->controller.vendorId;
        
        // SSVID (Bytes 2-3)
        *(PUINT16)(data + 2) = pCtx->controller.vendorId;
        
        // SN (Bytes 4-23) - Serial Number
        memcpy(data + 4, pCtx->controller.serialNumber, 20);
        
        // MN (Bytes 24-63) - Model Number
        memcpy(data + 24, pCtx->controller.modelNumber, 40);
        
        // FR (Bytes 64-71) - Firmware Revision
        memcpy(data + 64, pCtx->controller.firmwareRevision, 8);
        
        // RAB (Byte 72) - Recommended Arbitration Burst
        data[72] = 6;
        
        // OACS (Bytes 256-257) - Optional Admin Command Support
        *(PUINT16)(data + 256) = 0x0006;  // Format NVM, Security Send/Receive
        
        // ACLS (Byte 258) - Abort Command Limit
        data[258] = 3;
        
        // ONCS (Bytes 520-521) - Optional NVM Command Support
        *(PUINT16)(data + 520) = 0x0014;  // Write Zeroes, Dataset Management
        
        // NN (Bytes 516-519) - Number of Namespaces
        *(PUINT32)(data + 516) = pCtx->namespaceCount > 0 ? pCtx->namespaceCount : 1;
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Identify Namespace (CNS=0)
 */
UINT16 AdminCmdIdentifyNamespace(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT32 nsid;
    PADMIN_NAMESPACE_INFO nsInfo;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);  // Invalid Field
    }
    
    nsid = pCmd->NSID;
    
    // 验证 NSID
    if (nsid == 0 || nsid > pCtx->namespaceCount) {
        *pResult = 0;
        return MakeStatus(0, 0x0B, pCtx->adminCqPhase);  // Invalid Namespace
    }
    
    nsInfo = &pCtx->namespaces[nsid - 1];
    
    // 填充 Identify Namespace 数据
    if (pCtx->dataBuffer != NULL && pCtx->dataBufferSize >= 4096) {
        PUCHAR data = (PUCHAR)pCtx->dataBuffer;
        memset(data, 0, 4096);
        
        // NSZE (Bytes 0-7) - Namespace Size
        *(PUINT64)(data + 0) = nsInfo->size;
        
        // NCAP (Bytes 8-15) - Namespace Capacity
        *(PUINT64)(data + 8) = nsInfo->capacity > 0 ? nsInfo->capacity : nsInfo->size;
        
        // NUSE (Bytes 16-23) - Namespace Utilization
        *(PUINT64)(data + 16) = nsInfo->size;
        
        // NSFEAT (Byte 24) - Namespace Features
        data[24] = 0;
        
        // NLBAF (Byte 25) - Number of LBA Formats (0 means 1 format)
        data[25] = 0;
        
        // FLBAS (Byte 26) - Formatted LBA Size (format 0 active)
        data[26] = 0;
        
        // LBA Format 0 (Bytes 128-131)
        // MS (Metadata Size) = 0
        // LBADS = 9 (512 bytes) or 12 (4096 bytes)
        UINT8 lbads = 9;  // 默认 512 字节
        if (nsInfo->blockSize >= 4096) lbads = 12;
        else if (nsInfo->blockSize >= 2048) lbads = 11;
        else if (nsInfo->blockSize >= 1024) lbads = 10;
        
        data[128] = 0;  // MS low
        data[129] = 0;  // MS high
        data[130] = lbads;  // LBADS
        data[131] = 0;  // RP = Best performance
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Identify Active Namespace List (CNS=2)
 */
UINT16 AdminCmdIdentifyNamespaceList(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT32 startNsid;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    startNsid = pCmd->NSID;
    
    // 填充活动命名空间列表
    if (pCtx->dataBuffer != NULL && pCtx->dataBufferSize >= 4096) {
        PUINT32 data = (PUINT32)pCtx->dataBuffer;
        UINT32 count = 0;
        UINT32 i;
        
        memset(data, 0, 4096);
        
        for (i = 0; i < pCtx->namespaceCount && count < 1024; i++) {
            UINT32 nsid = i + 1;
            if (nsid > startNsid && pCtx->namespaces[i].active) {
                data[count++] = nsid;
            }
        }
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Identify 命令 (Opcode 0x06)
 */
UINT16 AdminCmdIdentify(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT8 cns;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    cns = (UINT8)(pCmd->CDW10 & 0xFF);
    
    if (g_DebugMode) {
        printf("  Identify: CNS=0x%02X, NSID=%u\n", cns, pCmd->NSID);
    }
    
    switch (cns) {
        case 0x00:  // Identify Namespace
            return AdminCmdIdentifyNamespace(pCtx, pCmd, pResult);
            
        case 0x01:  // Identify Controller
            return AdminCmdIdentifyController(pCtx, pCmd, pResult);
            
        case 0x02:  // Active Namespace ID list
            return AdminCmdIdentifyNamespaceList(pCtx, pCmd, pResult);
            
        default:
            if (g_DebugMode) {
                printf("    CNS=0x%02X unsupported\n", cns);
            }
            *pResult = 0;
            return MakeStatus(0, 0x02, pCtx->adminCqPhase);  // Invalid Field
    }
}

//===========================================================================
// Queue 管理命令实现
//===========================================================================

/**
 * 处理 Create I/O Completion Queue (Opcode 0x05)
 */
UINT16 AdminCmdCreateIoCq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT16 qid;
    UINT16 qsize;
    UINT32 queueIndex;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    qid = (UINT16)(pCmd->CDW10 & 0xFFFF);
    qsize = (UINT16)((pCmd->CDW10 >> 16) & 0xFFFF) + 1;
    
    if (g_DebugMode) {
        printf("  Create I/O CQ: QID=%u, Size=%u\n", qid, qsize);
    }
    
    // 验证 QID
    if (qid == 0 || qid > pCtx->maxIoQueues) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);  // Invalid Queue Identifier
    }
    
    queueIndex = qid - 1;
    
    // 检查是否已存在
    if (pCtx->ioCq[queueIndex].created) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    // 初始化 CQ 信息
    pCtx->ioCq[queueIndex].created = TRUE;
    pCtx->ioCq[queueIndex].capacity = qsize;
    pCtx->ioCq[queueIndex].phase = 1;
    
    // 获取 CQ 基地址 (从共享内存)
    if (pCtx->shmBase != NULL) {
        pCtx->ioCq[queueIndex].cqBase = (PNVME_COMPLETION)VnvmeGetIoCQ(
            pCtx->shmBase, queueIndex);
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Create I/O Submission Queue (Opcode 0x01)
 */
UINT16 AdminCmdCreateIoSq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT16 qid;
    UINT16 qsize;
    UINT16 cqid;
    UINT32 queueIndex;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    qid = (UINT16)(pCmd->CDW10 & 0xFFFF);
    qsize = (UINT16)((pCmd->CDW10 >> 16) & 0xFFFF) + 1;
    cqid = (UINT16)(pCmd->CDW11 & 0xFFFF);
    
    if (g_DebugMode) {
        printf("  Create I/O SQ: QID=%u, Size=%u, CQID=%u\n", qid, qsize, cqid);
    }
    
    // 验证 QID
    if (qid == 0 || qid > pCtx->maxIoQueues) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    queueIndex = qid - 1;
    
    // 验证关联的 CQ 是否存在
    if (cqid == 0 || cqid > pCtx->maxIoQueues || !pCtx->ioCq[cqid - 1].created) {
        *pResult = 0;
        return MakeStatus(0, 0x00, pCtx->adminCqPhase);  // Completion Queue Invalid
    }
    
    // 检查是否已存在
    if (pCtx->ioSq[queueIndex].created) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    // 初始化 SQ 信息
    pCtx->ioSq[queueIndex].created = TRUE;
    pCtx->ioSq[queueIndex].capacity = qsize;
    pCtx->ioSq[queueIndex].cqId = cqid;
    
    // 获取 SQ 基地址 (从共享内存)
    if (pCtx->shmBase != NULL) {
        pCtx->ioSq[queueIndex].sqBase = (PNVME_COMMAND)VnvmeGetIoSQ(
            pCtx->shmBase, queueIndex);
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Delete I/O Completion Queue (Opcode 0x04)
 */
UINT16 AdminCmdDeleteIoCq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT16 qid;
    UINT32 queueIndex;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    qid = (UINT16)(pCmd->CDW10 & 0xFFFF);
    
    if (g_DebugMode) {
        printf("  Delete I/O CQ: QID=%u\n", qid);
    }
    
    if (qid == 0 || qid > pCtx->maxIoQueues) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    queueIndex = qid - 1;
    
    if (!pCtx->ioCq[queueIndex].created) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    // 清除 CQ
    memset(&pCtx->ioCq[queueIndex], 0, sizeof(ADMIN_IO_QUEUE_INFO));
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Delete I/O Submission Queue (Opcode 0x00)
 */
UINT16 AdminCmdDeleteIoSq(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT16 qid;
    UINT32 queueIndex;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    qid = (UINT16)(pCmd->CDW10 & 0xFFFF);
    
    if (g_DebugMode) {
        printf("  Delete I/O SQ: QID=%u\n", qid);
    }
    
    if (qid == 0 || qid > pCtx->maxIoQueues) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    queueIndex = qid - 1;
    
    if (!pCtx->ioSq[queueIndex].created) {
        *pResult = 0;
        return MakeStatus(0, 0x01, pCtx->adminCqPhase);
    }
    
    // 清除 SQ
    memset(&pCtx->ioSq[queueIndex], 0, sizeof(ADMIN_IO_QUEUE_INFO));
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

//===========================================================================
// Features 命令实现
//===========================================================================

/**
 * 处理 Get Features (Opcode 0x0A)
 */
UINT16 AdminCmdGetFeatures(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT8 fid;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    fid = (UINT8)(pCmd->CDW10 & 0xFF);
    
    if (g_DebugMode) {
        printf("  Get Features: FID=0x%02X\n", fid);
    }
    
    switch (fid) {
        case 0x01:  // Arbitration
            *pResult = pCtx->arbitration;
            break;
            
        case 0x02:  // Power Management
            *pResult = pCtx->powerManagement;
            break;
            
        case 0x06:  // Volatile Write Cache
            *pResult = pCtx->volatileWriteCache;
            break;
            
        case 0x07:  // Number of Queues
            {
                UINT16 nsqa = (UINT16)(pCtx->maxIoQueues - 1);
                UINT16 ncqa = (UINT16)(pCtx->maxIoQueues - 1);
                *pResult = nsqa | ((UINT32)ncqa << 16);
            }
            break;
            
        default:
            *pResult = 0;
            break;
    }
    
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Set Features (Opcode 0x09)
 */
UINT16 AdminCmdSetFeatures(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT8 fid;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    fid = (UINT8)(pCmd->CDW10 & 0xFF);
    
    if (g_DebugMode) {
        printf("  Set Features: FID=0x%02X, CDW11=0x%08X\n", fid, pCmd->CDW11);
    }
    
    switch (fid) {
        case 0x01:  // Arbitration
            pCtx->arbitration = pCmd->CDW11;
            *pResult = 0;
            break;
            
        case 0x02:  // Power Management
            pCtx->powerManagement = pCmd->CDW11;
            *pResult = 0;
            break;
            
        case 0x06:  // Volatile Write Cache
            pCtx->volatileWriteCache = pCmd->CDW11 & 0x01;
            *pResult = 0;
            break;
            
        case 0x07:  // Number of Queues
            {
                UINT16 nsqr = (UINT16)(pCmd->CDW11 & 0xFFFF);
                UINT16 ncqr = (UINT16)((pCmd->CDW11 >> 16) & 0xFFFF);
                UINT16 nsqa = (nsqr < pCtx->maxIoQueues - 1) ? nsqr : (UINT16)(pCtx->maxIoQueues - 1);
                UINT16 ncqa = (ncqr < pCtx->maxIoQueues - 1) ? ncqr : (UINT16)(pCtx->maxIoQueues - 1);
                
                if (g_DebugMode) {
                    printf("    Number of Queues: NSQR=%u, NCQR=%u -> NSQA=%u, NCQA=%u\n",
                           nsqr, ncqr, nsqa, ncqa);
                }
                
                *pResult = nsqa | ((UINT32)ncqa << 16);
            }
            break;
            
        default:
            *pResult = 0;
            break;
    }
    
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

//===========================================================================
// 其他 Admin 命令
//===========================================================================

/**
 * 处理 Abort (Opcode 0x08)
 */
UINT16 AdminCmdAbort(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    if (g_DebugMode) {
        printf("  Abort: SQID=%u, CID=%u\n",
               pCmd->CDW10 & 0xFFFF, (pCmd->CDW10 >> 16) & 0xFFFF);
    }
    
    // 返回 0 表示命令已完成或未找到
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Get Log Page (Opcode 0x02)
 */
UINT16 AdminCmdGetLogPage(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    UINT8 lid;
    UINT32 numdl;
    UINT32 numdu;
    UINT32 bytes;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    lid = (UINT8)(pCmd->CDW10 & 0xFF);
    numdl = (pCmd->CDW10 >> 16) & 0xFFFF;
    numdu = pCmd->CDW11 & 0xFFFF;
    bytes = ((numdu << 16) | numdl) * 4 + 4;  // NUMD is 0-based dwords
    
    if (g_DebugMode) {
        printf("  Get Log Page: LID=0x%02X, Bytes=%u\n", lid, bytes);
    }
    
    // 填充日志页数据
    if (pCtx->dataBuffer != NULL && bytes <= pCtx->dataBufferSize) {
        memset(pCtx->dataBuffer, 0, bytes);
        
        switch (lid) {
            case 0x01:  // Error Information
                // 返回空的错误日志
                break;
                
            case 0x02:  // SMART / Health Information
                {
                    PUCHAR data = (PUCHAR)pCtx->dataBuffer;
                    // Critical Warning (Byte 0) = 0
                    // Temperature (Bytes 1-2) = 298K (25°C)
                    *(PUINT16)(data + 1) = 298;
                    // Available Spare (Byte 3) = 100%
                    data[3] = 100;
                    // Available Spare Threshold (Byte 4) = 10%
                    data[4] = 10;
                    // Percentage Used (Byte 5) = 0%
                    data[5] = 0;
                }
                break;
                
            case 0x03:  // Firmware Slot Information
                break;
                
            default:
                *pResult = 0;
                return MakeStatus(0, 0x09, pCtx->adminCqPhase);  // Invalid Log Page
        }
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

/**
 * 处理 Keep Alive (Opcode 0x18)
 */
UINT16 AdminCmdKeepAlive(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    PUINT32 pResult
)
{
    (void)pCmd;
    
    if (pCtx == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    if (g_DebugMode) {
        printf("  Keep Alive\n");
    }
    
    *pResult = 0;
    return MakeStatus(0, 0, pCtx->adminCqPhase);
}

//===========================================================================
// 命令分发
//===========================================================================

/**
 * 处理 Admin 命令
 */
UINT32 AdminCmdProcess(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd)
{
    UINT32 result = 0;
    UINT16 status;
    
    if (pCtx == NULL || pCmd == NULL) {
        return 0;
    }
    
    if (g_DebugMode) {
        printf("Admin Command: OPC=0x%02X, CID=%u, NSID=%u\n",
               pCmd->OPC, pCmd->CID, pCmd->NSID);
    }
    
    switch (pCmd->OPC) {
        case NVME_ADMIN_IDENTIFY:
            status = AdminCmdIdentify(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_CREATE_IO_CQ:
            status = AdminCmdCreateIoCq(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_CREATE_IO_SQ:
            status = AdminCmdCreateIoSq(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_DELETE_IO_CQ:
            status = AdminCmdDeleteIoCq(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_DELETE_IO_SQ:
            status = AdminCmdDeleteIoSq(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_SET_FEATURES:
            status = AdminCmdSetFeatures(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_GET_FEATURES:
            status = AdminCmdGetFeatures(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_ABORT:
            status = AdminCmdAbort(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_GET_LOG_PAGE:
            status = AdminCmdGetLogPage(pCtx, pCmd, &result);
            break;
            
        case NVME_ADMIN_KEEP_ALIVE:
            status = AdminCmdKeepAlive(pCtx, pCmd, &result);
            break;
            
        default:
            if (g_DebugMode) {
                printf("  Unknown Admin opcode 0x%02X\n", pCmd->OPC);
            }
            result = 0;
            status = MakeStatus(0, 0x01, pCtx->adminCqPhase);  // Invalid Opcode
            pCtx->errors++;
            break;
    }
    
    // 写入完成 - 由调用者负责
    AdminCmdPostCompletion(pCtx, pCmd->CID, result, 0, status);
    
    return 1;
}

//===========================================================================
// I/O 队列查询
//===========================================================================

/**
 * 获取 I/O SQ 信息
 */
PADMIN_IO_QUEUE_INFO AdminCmdGetIoSq(PADMIN_CMD_CONTEXT pCtx, UINT16 qid)
{
    if (pCtx == NULL || qid == 0 || qid > pCtx->maxIoQueues) {
        return NULL;
    }
    return &pCtx->ioSq[qid - 1];
}

/**
 * 获取 I/O CQ 信息
 */
PADMIN_IO_QUEUE_INFO AdminCmdGetIoCq(PADMIN_CMD_CONTEXT pCtx, UINT16 qid)
{
    if (pCtx == NULL || qid == 0 || qid > pCtx->maxIoQueues) {
        return NULL;
    }
    return &pCtx->ioCq[qid - 1];
}

/**
 * 检查 I/O 队列是否已创建
 */
BOOL AdminCmdIsIoQueueCreated(PADMIN_CMD_CONTEXT pCtx, UINT16 sqid)
{
    if (pCtx == NULL || sqid == 0 || sqid > pCtx->maxIoQueues) {
        return FALSE;
    }
    return pCtx->ioSq[sqid - 1].created;
}
