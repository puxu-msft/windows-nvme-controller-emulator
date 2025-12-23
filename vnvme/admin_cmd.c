/**
 * @file admin_cmd.c
 * @brief Admin 命令处理
 * 
 * 实现 NVMe Admin 命令的处理，包括:
 * - Identify Controller/Namespace
 * - Create/Delete I/O Queue
 * - Set/Get Features
 * - Abort, Keep Alive
 */

#include "vnvme.h"

//===========================================================================
// 辅助函数
//===========================================================================

/**
 * @brief 构造完成状态
 */
static UINT16 MakeStatus(UINT8 sct, UINT8 sc, UINT8 phase)
{
    // Status 字段格式: [15:15] DNR, [14:14] M, [11:9] SCT, [8:1] SC, [0:0] P
    return (UINT16)(phase | (sc << 1) | (sct << 9));
}

/**
 * @brief 发送成功完成
 */
static NTSTATUS PostSuccessCompletion(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ UINT16 Cid,
    _In_ UINT32 Dw0
    )
{
    NVME_COMPLETION completion = {0};
    
    completion.CID = Cid;
    completion.DW0 = Dw0;
    completion.SQID = 0;  // Admin Queue
    completion.SQHD = (UINT16)PdoContext->LastAdminSqTail;
    completion.Status = MakeStatus(0, 0, (UINT8)PdoContext->AdminCqPhase);
    
    return VnvmePostCompletion(PdoContext, 0, &completion);
}

/**
 * @brief 发送错误完成
 */
static NTSTATUS PostErrorCompletion(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ UINT16 Cid,
    _In_ UINT8 Sct,
    _In_ UINT8 Sc
    )
{
    NVME_COMPLETION completion = {0};
    
    completion.CID = Cid;
    completion.DW0 = 0;
    completion.SQID = 0;
    completion.SQHD = (UINT16)PdoContext->LastAdminSqTail;
    completion.Status = MakeStatus(Sct, Sc, (UINT8)PdoContext->AdminCqPhase);
    
    return VnvmePostCompletion(PdoContext, 0, &completion);
}

//===========================================================================
// Identify 命令处理
//===========================================================================

/**
 * @brief 处理 Identify Controller (CNS=0x01)
 */
static NTSTATUS HandleIdentifyController(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    NTSTATUS status;
    PNVME_IDENTIFY_CONTROLLER_DATA identData = NULL;
    PHYSICAL_ADDRESS prp1Phys;
    PVOID prp1Va = NULL;
    
    TRACE_INFO("HandleIdentifyController: CID=%u", Command->CID);
    
    // 分配并初始化 Identify Controller 数据
    identData = (PNVME_IDENTIFY_CONTROLLER_DATA)VNVME_ALLOC_POOL(
        NonPagedPoolNx,
        sizeof(NVME_IDENTIFY_CONTROLLER_DATA)
        );
    
    if (identData == NULL) {
        TRACE_ERROR("HandleIdentifyController: Failed to allocate identify data");
        return PostErrorCompletion(PdoContext, Command->CID, 
                                   NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
    }
    
    RtlZeroMemory(identData, sizeof(NVME_IDENTIFY_CONTROLLER_DATA));
    
    // 填充控制器信息
    identData->VID = 0x1B36;        // Red Hat (QEMU) vendor ID
    identData->SSVID = 0x1B36;
    RtlCopyMemory(identData->SN, "VNVME00000000001    ", 20);
    RtlCopyMemory(identData->MN, "Virtual NVMe Controller                 ", 40);
    RtlCopyMemory(identData->FR, "1.0.0   ", 8);
    
    identData->RAB = 6;             // Recommended Arbitration Burst
    identData->MDTS = 5;            // Max Data Transfer Size (2^5 * 4KB = 128KB)
    identData->CNTLID = (UINT16)PdoContext->ControllerId;
    identData->VER = 0x00010400;    // NVMe 1.4.0
    
    // Admin Command Set Attributes
    identData->OACS = 0x0006;       // Support Namespace Management & Firmware
    identData->ACL = 3;             // Abort Command Limit
    identData->AERL = 3;            // Async Event Request Limit
    identData->FRMW = 0x02;         // 1 firmware slot
    identData->LPA = 0x00;          // Log Page Attributes
    identData->ELPE = 63;           // Error Log Page Entries
    identData->NPSS = 0;            // 1 power state (0-based)
    
    // NVM Command Set Attributes
    identData->SQES = 0x66;         // SQ Entry Size: 2^6 = 64 bytes
    identData->CQES = 0x44;         // CQ Entry Size: 2^4 = 16 bytes
    identData->NN = PdoContext->NamespaceCount > 0 ? PdoContext->NamespaceCount : 1;
    identData->ONCS = 0x001F;       // Support Compare, Write Uncorrectable, DSM, Write Zeroes, Save/Select
    identData->VWC = 0x01;          // Volatile Write Cache present
    identData->AWUN = 0;            // Atomic Write Unit Normal
    identData->AWUPF = 0;           // Atomic Write Unit Power Fail
    
    // 复制数据到 PRP1 指向的内存
    // 注意: 需要映射 PRP1 物理地址到虚拟地址
    prp1Phys.QuadPart = Command->PRP1;
    prp1Va = MmMapIoSpaceEx(prp1Phys, sizeof(NVME_IDENTIFY_CONTROLLER_DATA), PAGE_READWRITE | PAGE_NOCACHE);
    
    if (prp1Va == NULL) {
        TRACE_ERROR("HandleIdentifyController: Failed to map PRP1 0x%016llX", Command->PRP1);
        VNVME_FREE_POOL(identData);
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_GENERIC, NVME_SC_DATA_TRANSFER_ERROR);
    }
    
    RtlCopyMemory(prp1Va, identData, sizeof(NVME_IDENTIFY_CONTROLLER_DATA));
    MmUnmapIoSpace(prp1Va, sizeof(NVME_IDENTIFY_CONTROLLER_DATA));
    
    VNVME_FREE_POOL(identData);
    
    status = PostSuccessCompletion(PdoContext, Command->CID, 0);
    
    TRACE_INFO("HandleIdentifyController: Success");
    return status;
}

/**
 * @brief 处理 Identify Namespace (CNS=0x00)
 */
static NTSTATUS HandleIdentifyNamespace(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    PNVME_IDENTIFY_NAMESPACE_DATA nsData = NULL;
    PHYSICAL_ADDRESS prp1Phys;
    PVOID prp1Va = NULL;
    ULONG nsid = Command->NSID;
    PVNVME_NAMESPACE ns = NULL;
    
    TRACE_INFO("HandleIdentifyNamespace: CID=%u, NSID=%u", Command->CID, nsid);
    
    // 验证 NSID
    if (nsid == 0 || nsid > VNVME_MAX_NAMESPACES) {
        TRACE_ERROR("HandleIdentifyNamespace: Invalid NSID %u", nsid);
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[nsid - 1];
    
    if (!ns->Active) {
        TRACE_WARN("HandleIdentifyNamespace: NSID %u not active", nsid);
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    // 分配 Identify Namespace 数据
    nsData = (PNVME_IDENTIFY_NAMESPACE_DATA)VNVME_ALLOC_POOL(
        NonPagedPoolNx,
        sizeof(NVME_IDENTIFY_NAMESPACE_DATA)
        );
    
    if (nsData == NULL) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
    }
    
    RtlZeroMemory(nsData, sizeof(NVME_IDENTIFY_NAMESPACE_DATA));
    
    // 填充命名空间信息
    nsData->NSZE = ns->TotalBlocks;
    nsData->NCAP = ns->TotalBlocks;
    nsData->NUSE = ns->TotalBlocks;
    nsData->NSFEAT = 0;
    nsData->NLBAF = 0;              // 1 LBA format (0-based)
    nsData->FLBAS = 0;              // Format 0 in use
    nsData->MC = 0;                 // No metadata
    nsData->DPC = 0;                // No end-to-end protection
    nsData->DPS = 0;
    
    // LBA Format 0: 512 bytes or 4096 bytes
    if (ns->BlockSize == 4096) {
        nsData->LBAF[0].LBADS = 12;       // 2^12 = 4096
    } else {
        nsData->LBAF[0].LBADS = 9;        // 2^9 = 512
    }
    nsData->LBAF[0].MS = 0;               // No metadata
    nsData->LBAF[0].RP = 0;               // Best performance
    
    // 映射并复制数据
    prp1Phys.QuadPart = Command->PRP1;
    prp1Va = MmMapIoSpaceEx(prp1Phys, sizeof(NVME_IDENTIFY_NAMESPACE_DATA), PAGE_READWRITE | PAGE_NOCACHE);
    
    if (prp1Va == NULL) {
        VNVME_FREE_POOL(nsData);
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_GENERIC, NVME_SC_DATA_TRANSFER_ERROR);
    }
    
    RtlCopyMemory(prp1Va, nsData, sizeof(NVME_IDENTIFY_NAMESPACE_DATA));
    MmUnmapIoSpace(prp1Va, sizeof(NVME_IDENTIFY_NAMESPACE_DATA));
    
    VNVME_FREE_POOL(nsData);
    
    TRACE_INFO("HandleIdentifyNamespace: NSID=%u, Size=%llu blocks", nsid, ns->TotalBlocks);
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

/**
 * @brief 处理 Identify 命令
 */
static NTSTATUS HandleIdentify(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    UINT8 cns = (UINT8)(Command->CDW10 & 0xFF);
    
    TRACE_INFO("HandleIdentify: CNS=%u", cns);
    
    switch (cns) {
        case 0x00:  // Identify Namespace
            return HandleIdentifyNamespace(PdoContext, Command);
            
        case 0x01:  // Identify Controller
            return HandleIdentifyController(PdoContext, Command);
            
        case 0x02:  // Active Namespace ID List
            // TODO: 实现活动命名空间列表
            TRACE_WARN("HandleIdentify: CNS=0x02 not implemented");
            return PostErrorCompletion(PdoContext, Command->CID,
                                       NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
            
        default:
            TRACE_WARN("HandleIdentify: Unknown CNS 0x%02X", cns);
            return PostErrorCompletion(PdoContext, Command->CID,
                                       NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
    }
}

//===========================================================================
// 队列管理命令
//===========================================================================

/**
 * @brief 处理 Create I/O Completion Queue
 */
static NTSTATUS HandleCreateIoCq(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    USHORT qid = (USHORT)(Command->CDW10 & 0xFFFF);
    USHORT qsize = (USHORT)((Command->CDW10 >> 16) & 0xFFFF) + 1;
    BOOLEAN pc = (Command->CDW11 & 0x01) != 0;        // Physically Contiguous
    BOOLEAN ien = (Command->CDW11 & 0x02) != 0;       // Interrupt Enable
    USHORT iv = (USHORT)((Command->CDW11 >> 16) & 0xFFFF);  // Interrupt Vector
    
    TRACE_INFO("HandleCreateIoCq: QID=%u, Size=%u, PC=%u, IEN=%u, IV=%u",
               qid, qsize, pc, ien, iv);
    
    // 验证参数
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    if (qsize < 2) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_SIZE);
    }
    
    // 检查队列是否已存在
    if (PdoContext->IoCq[qid - 1].Created) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    // 创建队列
    PdoContext->IoCq[qid - 1].BaseAddress.QuadPart = Command->PRP1;
    PdoContext->IoCq[qid - 1].Size = qsize;
    PdoContext->IoCq[qid - 1].Head = 0;
    PdoContext->IoCq[qid - 1].Tail = 0;
    PdoContext->IoCq[qid - 1].PhaseTag = TRUE;
    PdoContext->IoCq[qid - 1].Created = TRUE;
    
    TRACE_INFO("HandleCreateIoCq: CQ%u created, Base=0x%016llX, Size=%u",
               qid, Command->PRP1, qsize);
    
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

/**
 * @brief 处理 Create I/O Submission Queue
 */
static NTSTATUS HandleCreateIoSq(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    USHORT qid = (USHORT)(Command->CDW10 & 0xFFFF);
    USHORT qsize = (USHORT)((Command->CDW10 >> 16) & 0xFFFF) + 1;
    BOOLEAN pc = (Command->CDW11 & 0x01) != 0;
    USHORT cqid = (USHORT)((Command->CDW11 >> 16) & 0xFFFF);
    
    TRACE_INFO("HandleCreateIoSq: QID=%u, Size=%u, PC=%u, CQID=%u",
               qid, qsize, pc, cqid);
    
    // 验证参数
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    if (qsize < 2) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_SIZE);
    }
    
    // 验证关联的 CQ 是否存在
    if (cqid == 0 || cqid > VNVME_MAX_IO_QUEUES || !PdoContext->IoCq[cqid - 1].Created) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_CQ_INVALID);
    }
    
    // 检查队列是否已存在
    if (PdoContext->IoSq[qid - 1].Created) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    // 创建队列
    PdoContext->IoSq[qid - 1].BaseAddress.QuadPart = Command->PRP1;
    PdoContext->IoSq[qid - 1].Size = qsize;
    PdoContext->IoSq[qid - 1].Head = 0;
    PdoContext->IoSq[qid - 1].Tail = 0;
    PdoContext->IoSq[qid - 1].Created = TRUE;
    
    if (PdoContext->IoQueueCount < qid) {
        PdoContext->IoQueueCount = qid;
    }
    
    TRACE_INFO("HandleCreateIoSq: SQ%u created, Base=0x%016llX, Size=%u, CQ=%u",
               qid, Command->PRP1, qsize, cqid);
    
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

/**
 * @brief 处理 Delete I/O Completion Queue
 */
static NTSTATUS HandleDeleteIoCq(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    USHORT qid = (USHORT)(Command->CDW10 & 0xFFFF);
    
    TRACE_INFO("HandleDeleteIoCq: QID=%u", qid);
    
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    if (!PdoContext->IoCq[qid - 1].Created) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    // TODO: 检查是否有 SQ 关联到这个 CQ
    
    RtlZeroMemory(&PdoContext->IoCq[qid - 1], sizeof(VNVME_QUEUE_STATE));
    
    TRACE_INFO("HandleDeleteIoCq: CQ%u deleted", qid);
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

/**
 * @brief 处理 Delete I/O Submission Queue
 */
static NTSTATUS HandleDeleteIoSq(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    USHORT qid = (USHORT)(Command->CDW10 & 0xFFFF);
    
    TRACE_INFO("HandleDeleteIoSq: QID=%u", qid);
    
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    if (!PdoContext->IoSq[qid - 1].Created) {
        return PostErrorCompletion(PdoContext, Command->CID,
                                   NVME_SCT_COMMAND, NVME_SC_INVALID_QUEUE_ID);
    }
    
    RtlZeroMemory(&PdoContext->IoSq[qid - 1], sizeof(VNVME_QUEUE_STATE));
    
    TRACE_INFO("HandleDeleteIoSq: SQ%u deleted", qid);
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

//===========================================================================
// Set/Get Features
//===========================================================================

/**
 * @brief 处理 Set Features 命令
 */
static NTSTATUS HandleSetFeatures(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    UINT8 fid = (UINT8)(Command->CDW10 & 0xFF);
    
    TRACE_INFO("HandleSetFeatures: FID=0x%02X, CDW11=0x%08X", fid, Command->CDW11);
    
    switch (fid) {
        case 0x01:  // Arbitration
        case 0x02:  // Power Management
        case 0x04:  // Temperature Threshold
        case 0x05:  // Error Recovery
        case 0x06:  // Volatile Write Cache
            // 接受但忽略这些功能设置
            return PostSuccessCompletion(PdoContext, Command->CID, 0);
            
        case 0x07:  // Number of Queues
            {
                USHORT ncqr = (USHORT)(Command->CDW11 & 0xFFFF);        // CQ Requested
                USHORT nsqr = (USHORT)((Command->CDW11 >> 16) & 0xFFFF); // SQ Requested
                USHORT ncqa = (ncqr > VNVME_MAX_IO_QUEUES) ? VNVME_MAX_IO_QUEUES : ncqr;
                USHORT nsqa = (nsqr > VNVME_MAX_IO_QUEUES) ? VNVME_MAX_IO_QUEUES : nsqr;
                UINT32 dw0 = ncqa | ((UINT32)nsqa << 16);
                
                TRACE_INFO("HandleSetFeatures: Number of Queues - Req CQ=%u SQ=%u, Alloc CQ=%u SQ=%u",
                           ncqr, nsqr, ncqa, nsqa);
                
                PdoContext->MaxIoQueues = (ncqa < nsqa) ? ncqa : nsqa;
                return PostSuccessCompletion(PdoContext, Command->CID, dw0);
            }
            
        case 0x0B:  // Interrupt Coalescing
        case 0x0C:  // Interrupt Vector Configuration
            return PostSuccessCompletion(PdoContext, Command->CID, 0);
            
        default:
            TRACE_WARN("HandleSetFeatures: Unsupported FID 0x%02X", fid);
            return PostErrorCompletion(PdoContext, Command->CID,
                                       NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
    }
}

/**
 * @brief 处理 Get Features 命令
 */
static NTSTATUS HandleGetFeatures(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    UINT8 fid = (UINT8)(Command->CDW10 & 0xFF);
    UINT32 dw0 = 0;
    
    TRACE_INFO("HandleGetFeatures: FID=0x%02X", fid);
    
    switch (fid) {
        case 0x01:  // Arbitration
            dw0 = 0x00000000;   // Default arbitration
            break;
            
        case 0x02:  // Power Management
            dw0 = 0x00000000;   // Power state 0
            break;
            
        case 0x04:  // Temperature Threshold
            dw0 = 0x0157;       // 343K = 70°C
            break;
            
        case 0x05:  // Error Recovery
            dw0 = 0x00000000;
            break;
            
        case 0x06:  // Volatile Write Cache
            dw0 = 0x00000001;   // VWC enabled
            break;
            
        case 0x07:  // Number of Queues
            dw0 = PdoContext->MaxIoQueues | ((UINT32)PdoContext->MaxIoQueues << 16);
            break;
            
        case 0x0B:  // Interrupt Coalescing
            dw0 = 0x00000000;
            break;
            
        default:
            TRACE_WARN("HandleGetFeatures: Unsupported FID 0x%02X", fid);
            return PostErrorCompletion(PdoContext, Command->CID,
                                       NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
    }
    
    return PostSuccessCompletion(PdoContext, Command->CID, dw0);
}

//===========================================================================
// 其他 Admin 命令
//===========================================================================

/**
 * @brief 处理 Abort 命令
 */
static NTSTATUS HandleAbort(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    USHORT sqid = (USHORT)(Command->CDW10 & 0xFFFF);
    USHORT cid = (USHORT)((Command->CDW10 >> 16) & 0xFFFF);
    
    TRACE_INFO("HandleAbort: SQID=%u, CID=%u", sqid, cid);
    
    // 简单实现：总是返回命令未找到
    // DW0 bit 0 = 1 表示命令未被中止
    return PostSuccessCompletion(PdoContext, Command->CID, 1);
}

/**
 * @brief 处理 Async Event Request 命令
 */
static NTSTATUS HandleAsyncEventRequest(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    TRACE_INFO("HandleAsyncEventRequest: CID=%u", Command->CID);
    
    // TODO: 存储 AER 命令，稍后完成
    // 当前简单返回成功
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

/**
 * @brief 处理 Keep Alive 命令
 */
static NTSTATUS HandleKeepAlive(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    TRACE_VERBOSE("HandleKeepAlive: CID=%u", Command->CID);
    return PostSuccessCompletion(PdoContext, Command->CID, 0);
}

//===========================================================================
// Admin 命令分发
//===========================================================================

/**
 * @brief 处理 Admin 命令
 * 
 * @param PdoContext PDO 上下文
 * @param Command 命令指针
 * @return NTSTATUS 状态码
 */
NTSTATUS
VnvmeProcessAdminCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ PNVME_COMMAND Command
    )
{
    NTSTATUS status;
    
    TRACE_INFO("VnvmeProcessAdminCommand: OPC=0x%02X, CID=%u, NSID=%u",
               Command->OPC, Command->CID, Command->NSID);
    
    switch (Command->OPC) {
        case NVME_ADMIN_IDENTIFY:
            status = HandleIdentify(PdoContext, Command);
            break;
            
        case NVME_ADMIN_CREATE_IO_CQ:
            status = HandleCreateIoCq(PdoContext, Command);
            break;
            
        case NVME_ADMIN_CREATE_IO_SQ:
            status = HandleCreateIoSq(PdoContext, Command);
            break;
            
        case NVME_ADMIN_DELETE_IO_CQ:
            status = HandleDeleteIoCq(PdoContext, Command);
            break;
            
        case NVME_ADMIN_DELETE_IO_SQ:
            status = HandleDeleteIoSq(PdoContext, Command);
            break;
            
        case NVME_ADMIN_SET_FEATURES:
            status = HandleSetFeatures(PdoContext, Command);
            break;
            
        case NVME_ADMIN_GET_FEATURES:
            status = HandleGetFeatures(PdoContext, Command);
            break;
            
        case NVME_ADMIN_ABORT:
            status = HandleAbort(PdoContext, Command);
            break;
            
        case NVME_ADMIN_ASYNC_EVENT_REQUEST:
            status = HandleAsyncEventRequest(PdoContext, Command);
            break;
            
        case NVME_ADMIN_KEEP_ALIVE:
            status = HandleKeepAlive(PdoContext, Command);
            break;
            
        case NVME_ADMIN_GET_LOG_PAGE:
            // TODO: 实现 Get Log Page
            TRACE_WARN("VnvmeProcessAdminCommand: Get Log Page not implemented");
            status = PostErrorCompletion(PdoContext, Command->CID,
                                         NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
            break;
            
        default:
            TRACE_WARN("VnvmeProcessAdminCommand: Unknown opcode 0x%02X", Command->OPC);
            status = PostErrorCompletion(PdoContext, Command->CID,
                                         NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
            break;
    }
    
    // 更新统计
    InterlockedIncrement64(&PdoContext->AdminCommandsProcessed);
    
    return status;
}

//===========================================================================
// 批量命令处理 (内核模式)
//===========================================================================

/**
 * @brief 处理 Admin 队列中所有待处理命令
 * 
 * 从上次处理的 Tail 到新 Tail 之间的所有命令逐个处理。
 */
VOID
VnvmeProcessAdminCommands(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG NewTail
    )
{
    ULONG head = PdoContext->AdminSq.Head;
    ULONG tail = NewTail;
    ULONG queueSize = PdoContext->AdminSqSize;
    NTSTATUS status;
    NVME_COMMAND command;
    
    if (queueSize == 0) {
        TRACE_WARN("VnvmeProcessAdminCommands: Queue not initialized");
        return;
    }
    
    // 处理从 head 到 tail 的所有命令
    while (head != tail) {
        // 获取命令
        status = VnvmeFetchCommand(PdoContext, 0, &command);
        if (!NT_SUCCESS(status)) {
            TRACE_ERROR("VnvmeProcessAdminCommands: VnvmeFetchCommand failed 0x%08X", status);
            break;
        }
        
        // 处理命令
        VnvmeProcessAdminCommand(PdoContext, &command);
        
        // 更新 head
        head = (head + 1) % queueSize;
        PdoContext->AdminSq.Head = head;
    }
}
