/**
 * @file io_commands.c
 * @brief NVMe I/O 命令处理实现
 * 
 * 实现 I/O 命令处理函数，从 command_processor.c 拆分而来。
 * 
 * 支持的命令:
 * - Read (Opcode 0x02)
 * - Write (Opcode 0x01)
 * - Flush (Opcode 0x00)
 * - Write Zeroes (Opcode 0x08)
 * - Dataset Management (Opcode 0x09)
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "io_commands.h"
#include "backend.h"

/*===========================================================================
 * 外部声明
 *===========================================================================*/

// 后端函数
BOOL BackendRead(PBACKEND_CONTEXT ctx, UINT64 offset, void* buffer, UINT32 length);
BOOL BackendWrite(PBACKEND_CONTEXT ctx, UINT64 offset, const void* buffer, UINT32 length);
BOOL BackendFlush(PBACKEND_CONTEXT ctx);
BOOL BackendWriteZeroes(PBACKEND_CONTEXT ctx, UINT64 offset, UINT64 length);

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

/**
 * @brief 构造 NVMe 状态字段
 */
static UINT16 MakeStatus(UINT8 sct, UINT8 sc, UINT8 phase)
{
    return (UINT16)(phase | (sc << 1) | (sct << 9));
}

/**
 * @brief 获取 CQ Phase
 */
static UINT32 GetCqPhase(PIO_CMD_CONTEXT pCtx, UINT16 queueId)
{
    PADMIN_IO_QUEUE_INFO cqInfo;
    PADMIN_IO_QUEUE_INFO sqInfo;
    
    if (pCtx == NULL || pCtx->adminCtx == NULL) {
        return 1;
    }
    
    // 获取 SQ 对应的 CQ
    sqInfo = AdminCmdGetIoSq(pCtx->adminCtx, queueId);
    if (sqInfo == NULL) {
        return 1;
    }
    
    cqInfo = AdminCmdGetIoCq(pCtx->adminCtx, (UINT16)sqInfo->cqId);
    if (cqInfo == NULL) {
        return 1;
    }
    
    return cqInfo->phase;
}

/*===========================================================================
 * 初始化和配置
 *===========================================================================*/

/**
 * 初始化 I/O 命令上下文
 */
BOOL IoCmdInit(
    PIO_CMD_CONTEXT pCtx,
    PADMIN_CMD_CONTEXT pAdminCtx,
    PBACKEND_CONTEXT backend,
    PVOID shmBase,
    PVOID dataBuffer,
    UINT32 dataBufferSize
)
{
    if (pCtx == NULL) {
        return FALSE;
    }
    
    memset(pCtx, 0, sizeof(IO_CMD_CONTEXT));
    
    pCtx->adminCtx = pAdminCtx;
    pCtx->backend = backend;
    pCtx->shmBase = shmBase;
    pCtx->dataBuffer = dataBuffer;
    pCtx->dataBufferSize = dataBufferSize;
    pCtx->debugMode = FALSE;
    
    return TRUE;
}

/**
 * 添加命名空间配置
 */
BOOL IoCmdAddNamespace(
    PIO_CMD_CONTEXT pCtx,
    const IO_NAMESPACE_CONFIG* pNsConfig
)
{
    if (pCtx == NULL || pNsConfig == NULL) {
        return FALSE;
    }
    
    if (pCtx->namespaceCount >= 16) {
        return FALSE;
    }
    
    memcpy(&pCtx->namespaces[pCtx->namespaceCount], pNsConfig, sizeof(IO_NAMESPACE_CONFIG));
    pCtx->namespaceCount++;
    
    return TRUE;
}

/**
 * 设置调试模式
 */
void IoCmdSetDebugMode(PIO_CMD_CONTEXT pCtx, BOOL debugMode)
{
    if (pCtx != NULL) {
        pCtx->debugMode = debugMode;
    }
}

/**
 * 获取统计信息
 */
void IoCmdGetStats(PIO_CMD_CONTEXT pCtx, PIO_STATS pStats)
{
    if (pCtx == NULL || pStats == NULL) {
        return;
    }
    memcpy(pStats, &pCtx->stats, sizeof(IO_STATS));
}

/**
 * 重置统计信息
 */
void IoCmdResetStats(PIO_CMD_CONTEXT pCtx)
{
    if (pCtx != NULL) {
        memset(&pCtx->stats, 0, sizeof(IO_STATS));
    }
}

/*===========================================================================
 * 完成写入
 *===========================================================================*/

/**
 * 写入 I/O 完成
 */
void IoCmdPostCompletion(
    PIO_CMD_CONTEXT pCtx,
    UINT16 queueId,
    UINT16 cid,
    UINT32 result,
    UINT8 sct,
    UINT8 sc
)
{
    PADMIN_IO_QUEUE_INFO sqInfo;
    PADMIN_IO_QUEUE_INFO cqInfo;
    PNVME_COMPLETION cqe;
    UINT32 tail;
    
    if (pCtx == NULL || pCtx->adminCtx == NULL) {
        return;
    }
    
    if (queueId == 0 || queueId > 16) {
        return;
    }
    
    // 获取 SQ 对应的 CQ
    sqInfo = AdminCmdGetIoSq(pCtx->adminCtx, queueId);
    if (sqInfo == NULL || !sqInfo->created) {
        return;
    }
    
    cqInfo = AdminCmdGetIoCq(pCtx->adminCtx, (UINT16)sqInfo->cqId);
    if (cqInfo == NULL || !cqInfo->created || cqInfo->cqBase == NULL) {
        return;
    }
    
    // 获取当前 Tail (简化实现，假设 descriptor 字段有效)
    tail = 0;  // 实际使用时从 descriptor 获取
    
    cqe = &cqInfo->cqBase[tail];
    cqe->DW0 = result;
    cqe->DW1 = 0;
    cqe->SQHD = 0;  // 实际使用时从 SQ descriptor 获取
    cqe->SQID = queueId;
    cqe->CID = cid;
    cqe->Status = MakeStatus(sct, sc, (UINT8)cqInfo->phase);
    
    // 更新统计
    pCtx->stats.commandsProcessed++;
}

/*===========================================================================
 * 验证函数
 *===========================================================================*/

/**
 * 检查命名空间是否有效
 */
BOOL IoCmdIsNamespaceValid(PIO_CMD_CONTEXT pCtx, UINT32 nsid)
{
    if (pCtx == NULL || nsid == 0 || nsid > pCtx->namespaceCount) {
        return FALSE;
    }
    return pCtx->namespaces[nsid - 1].active;
}

/**
 * 获取命名空间的块大小
 */
UINT32 IoCmdGetBlockSize(PIO_CMD_CONTEXT pCtx, UINT32 nsid)
{
    if (pCtx == NULL || nsid == 0 || nsid > pCtx->namespaceCount) {
        return 512;  // 默认
    }
    return pCtx->namespaces[nsid - 1].blockSize;
}

/**
 * 验证 LBA 范围
 */
BOOL IoCmdValidateLbaRange(
    PIO_CMD_CONTEXT pCtx,
    UINT32 nsid,
    UINT64 slba,
    UINT32 nlb
)
{
    PIO_NAMESPACE_CONFIG nsConfig;
    
    if (!IoCmdIsNamespaceValid(pCtx, nsid)) {
        return FALSE;
    }
    
    nsConfig = &pCtx->namespaces[nsid - 1];
    
    // 检查溢出
    if (slba + nlb < slba) {
        return FALSE;
    }
    
    // 检查是否超出范围
    if (slba + nlb > nsConfig->size) {
        return FALSE;
    }
    
    return TRUE;
}

/*===========================================================================
 * Read 命令
 *===========================================================================*/

/**
 * 处理 Read 命令 (Opcode 0x02)
 */
UINT16 IoCmdRead(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
)
{
    UINT32 nsid;
    UINT64 slba;
    UINT16 nlb;
    UINT64 byteOffset;
    UINT32 length;
    UINT32 blockSize;
    UINT8 phase;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);  // Invalid Field
    }
    
    *pResult = 0;
    phase = (UINT8)GetCqPhase(pCtx, queueId);
    
    nsid = pCmd->NSID;
    slba = pCmd->CDW10 | ((UINT64)pCmd->CDW11 << 32);
    nlb = (UINT16)(pCmd->CDW12 & 0xFFFF) + 1;
    
    // 验证 NSID
    if (!IoCmdIsNamespaceValid(pCtx, nsid)) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x0B, phase);  // Invalid Namespace
    }
    
    blockSize = IoCmdGetBlockSize(pCtx, nsid);
    length = nlb * blockSize;
    byteOffset = slba * blockSize;
    
    // 验证 LBA 范围
    if (!IoCmdValidateLbaRange(pCtx, nsid, slba, nlb)) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x80, phase);  // LBA Out of Range
    }
    
    if (pCtx->debugMode) {
        printf("I/O Read: QID=%u, NSID=%u, LBA=%llu, NLB=%u, Len=%u\n",
               queueId, nsid, (unsigned long long)slba, nlb, length);
    }
    
    // 从后端读取数据到数据缓冲区
    if (pCtx->backend && pCtx->dataBuffer && length <= pCtx->dataBufferSize) {
        if (!BackendRead(pCtx->backend, byteOffset, pCtx->dataBuffer, length)) {
            pCtx->stats.errors++;
            return MakeStatus(0x02, 0x81, phase);  // Media Error: Unrecovered Read Error
        }
    }
    
    pCtx->stats.bytesRead += length;
    pCtx->stats.readCommands++;
    
    return MakeStatus(0, 0, phase);
}

/*===========================================================================
 * Write 命令
 *===========================================================================*/

/**
 * 处理 Write 命令 (Opcode 0x01)
 */
UINT16 IoCmdWrite(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
)
{
    UINT32 nsid;
    UINT64 slba;
    UINT16 nlb;
    UINT64 byteOffset;
    UINT32 length;
    UINT32 blockSize;
    UINT8 phase;
    PIO_NAMESPACE_CONFIG nsConfig;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    *pResult = 0;
    phase = (UINT8)GetCqPhase(pCtx, queueId);
    
    nsid = pCmd->NSID;
    slba = pCmd->CDW10 | ((UINT64)pCmd->CDW11 << 32);
    nlb = (UINT16)(pCmd->CDW12 & 0xFFFF) + 1;
    
    // 验证 NSID
    if (!IoCmdIsNamespaceValid(pCtx, nsid)) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x0B, phase);  // Invalid Namespace
    }
    
    nsConfig = &pCtx->namespaces[nsid - 1];
    
    // 检查只读
    if (nsConfig->readOnly) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x82, phase);  // Write Protected
    }
    
    blockSize = nsConfig->blockSize;
    length = nlb * blockSize;
    byteOffset = slba * blockSize;
    
    // 验证 LBA 范围
    if (!IoCmdValidateLbaRange(pCtx, nsid, slba, nlb)) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x80, phase);  // LBA Out of Range
    }
    
    if (pCtx->debugMode) {
        printf("I/O Write: QID=%u, NSID=%u, LBA=%llu, NLB=%u, Len=%u\n",
               queueId, nsid, (unsigned long long)slba, nlb, length);
    }
    
    // 从数据缓冲区写入后端
    if (pCtx->backend && pCtx->dataBuffer && length <= pCtx->dataBufferSize) {
        if (!BackendWrite(pCtx->backend, byteOffset, pCtx->dataBuffer, length)) {
            pCtx->stats.errors++;
            return MakeStatus(0x02, 0x82, phase);  // Media Error: Write Fault
        }
    }
    
    pCtx->stats.bytesWritten += length;
    pCtx->stats.writeCommands++;
    
    return MakeStatus(0, 0, phase);
}

/*===========================================================================
 * Flush 命令
 *===========================================================================*/

/**
 * 处理 Flush 命令 (Opcode 0x00)
 */
UINT16 IoCmdFlush(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
)
{
    UINT8 phase;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    *pResult = 0;
    phase = (UINT8)GetCqPhase(pCtx, queueId);
    
    if (pCtx->debugMode) {
        printf("I/O Flush: QID=%u, NSID=%u\n", queueId, pCmd->NSID);
    }
    
    if (pCtx->backend) {
        if (!BackendFlush(pCtx->backend)) {
            pCtx->stats.errors++;
            return MakeStatus(0x02, 0x82, phase);  // Media Error: Write Fault
        }
    }
    
    pCtx->stats.flushCommands++;
    
    return MakeStatus(0, 0, phase);
}

/*===========================================================================
 * Write Zeroes 命令
 *===========================================================================*/

/**
 * 处理 Write Zeroes 命令 (Opcode 0x08)
 */
UINT16 IoCmdWriteZeroes(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
)
{
    UINT32 nsid;
    UINT64 slba;
    UINT16 nlb;
    UINT64 byteOffset;
    UINT64 length;
    UINT32 blockSize;
    UINT8 phase;
    PIO_NAMESPACE_CONFIG nsConfig;
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    *pResult = 0;
    phase = (UINT8)GetCqPhase(pCtx, queueId);
    
    nsid = pCmd->NSID;
    slba = pCmd->CDW10 | ((UINT64)pCmd->CDW11 << 32);
    nlb = (UINT16)(pCmd->CDW12 & 0xFFFF) + 1;
    
    // 验证 NSID
    if (!IoCmdIsNamespaceValid(pCtx, nsid)) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x0B, phase);  // Invalid Namespace
    }
    
    nsConfig = &pCtx->namespaces[nsid - 1];
    
    // 检查只读
    if (nsConfig->readOnly) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x82, phase);  // Write Protected
    }
    
    blockSize = nsConfig->blockSize;
    length = (UINT64)nlb * blockSize;
    byteOffset = slba * blockSize;
    
    // 验证 LBA 范围
    if (!IoCmdValidateLbaRange(pCtx, nsid, slba, nlb)) {
        pCtx->stats.errors++;
        return MakeStatus(0, 0x80, phase);  // LBA Out of Range
    }
    
    if (pCtx->debugMode) {
        printf("I/O Write Zeroes: QID=%u, NSID=%u, LBA=%llu, NLB=%u\n",
               queueId, nsid, (unsigned long long)slba, nlb);
    }
    
    // 写零到后端
    if (pCtx->backend) {
        if (!BackendWriteZeroes(pCtx->backend, byteOffset, length)) {
            pCtx->stats.errors++;
            return MakeStatus(0x02, 0x82, phase);  // Media Error: Write Fault
        }
    }
    
    pCtx->stats.bytesWritten += length;
    pCtx->stats.writeZeroesCommands++;
    
    return MakeStatus(0, 0, phase);
}

/*===========================================================================
 * Dataset Management 命令
 *===========================================================================*/

/**
 * 处理 Dataset Management 命令 (Opcode 0x09)
 * 用于 TRIM/Deallocate 操作
 */
UINT16 IoCmdDatasetManagement(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId,
    PUINT32 pResult
)
{
    UINT8 phase;
    UINT32 nr;      // Number of Ranges
    UINT32 ad;      // Attribute - Deallocate
    
    if (pCtx == NULL || pCmd == NULL || pResult == NULL) {
        return MakeStatus(0, 0x01, 0);
    }
    
    *pResult = 0;
    phase = (UINT8)GetCqPhase(pCtx, queueId);
    
    nr = (pCmd->CDW10 & 0xFF) + 1;  // NR is 0-based
    ad = (pCmd->CDW11 >> 2) & 0x01;  // AD bit
    
    if (pCtx->debugMode) {
        printf("I/O Dataset Management: QID=%u, NSID=%u, NR=%u, AD=%u\n",
               queueId, pCmd->NSID, nr, ad);
    }
    
    // 目前简单返回成功
    // 完整实现需要解析 range 数据并调用后端的 deallocate
    
    return MakeStatus(0, 0, phase);
}

/*===========================================================================
 * 命令分发
 *===========================================================================*/

/**
 * 处理 I/O 命令
 */
UINT32 IoCmdProcess(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    UINT16 queueId
)
{
    UINT32 result = 0;
    UINT16 status;
    UINT8 sct, sc;
    
    if (pCtx == NULL || pCmd == NULL) {
        return 0;
    }
    
    if (pCtx->debugMode) {
        printf("I/O Command: OPC=0x%02X, CID=%u, NSID=%u, QID=%u\n",
               pCmd->OPC, pCmd->CID, pCmd->NSID, queueId);
    }
    
    switch (pCmd->OPC) {
        case NVME_IO_READ:
            status = IoCmdRead(pCtx, pCmd, queueId, &result);
            break;
            
        case NVME_IO_WRITE:
            status = IoCmdWrite(pCtx, pCmd, queueId, &result);
            break;
            
        case NVME_IO_FLUSH:
            status = IoCmdFlush(pCtx, pCmd, queueId, &result);
            break;
            
        case NVME_IO_WRITE_ZEROES:
            status = IoCmdWriteZeroes(pCtx, pCmd, queueId, &result);
            break;
            
        case 0x09:  // Dataset Management
            status = IoCmdDatasetManagement(pCtx, pCmd, queueId, &result);
            break;
            
        default:
            if (pCtx->debugMode) {
                printf("  Unknown I/O opcode 0x%02X\n", pCmd->OPC);
            }
            result = 0;
            status = MakeStatus(0, 0x01, (UINT8)GetCqPhase(pCtx, queueId));  // Invalid Opcode
            pCtx->stats.errors++;
            break;
    }
    
    // 解析状态
    sct = (UINT8)((status >> 9) & 0x07);
    sc = (UINT8)((status >> 1) & 0xFF);
    
    // 写入完成
    IoCmdPostCompletion(pCtx, queueId, pCmd->CID, result, sct, sc);
    
    return 1;
}
