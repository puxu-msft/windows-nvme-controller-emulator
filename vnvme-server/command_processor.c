/**
 * @file command_processor.c
 * @brief 用户态命令处理器
 * 
 * 实现 NVMe 命令的用户态处理:
 * - 从共享内存读取原始 NVME_COMMAND
 * - 处理 Admin 和 I/O 命令
 * - 写入完成到 CQ
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/vnvme_common.h"
#include "../include/nvme_spec.h"

/*===========================================================================
 * 前向声明
 *===========================================================================*/

// 外部函数
extern PVNVME_SHARED_MEMORY_CONTROL_BLOCK VnvmeGetControlBlock(PVOID shmAddress);
extern BOOL g_Running;
extern BOOL g_DebugMode;

// 存储后端函数
struct _BACKEND_CONTEXT;
typedef struct _BACKEND_CONTEXT BACKEND_CONTEXT, *PBACKEND_CONTEXT;

PBACKEND_CONTEXT BackendCreate(int type, SIZE_T size, const WCHAR* filePath);
void BackendDestroy(PBACKEND_CONTEXT ctx);
BOOL BackendRead(PBACKEND_CONTEXT ctx, UINT64 offset, void* buffer, UINT32 length);
BOOL BackendWrite(PBACKEND_CONTEXT ctx, UINT64 offset, const void* buffer, UINT32 length);
BOOL BackendFlush(PBACKEND_CONTEXT ctx);
BOOL BackendWriteZeroes(PBACKEND_CONTEXT ctx, UINT64 offset, UINT64 length);
UINT64 BackendGetSize(PBACKEND_CONTEXT ctx);

/*===========================================================================
 * 类型定义
 *===========================================================================*/

/**
 * @brief I/O 队列信息
 */
typedef struct _IO_QUEUE_INFO {
    BOOL Created;
    UINT32 Capacity;
    UINT32 CqId;                        // 关联的 CQ ID (仅 SQ)
    UINT32 Phase;                       // Phase Tag (仅 CQ)
    PNVME_COMMAND SqBase;               // SQ 基地址
    PNVME_COMPLETION CqBase;            // CQ 基地址
    PVNVME_QUEUE_DESCRIPTOR Descriptor; // 描述符指针
} IO_QUEUE_INFO, *PIO_QUEUE_INFO;

/**
 * @brief 命令处理器上下文
 */
typedef struct _CMD_PROCESSOR_CONTEXT {
    PVOID ShmAddress;
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK Shm;
    
    // 队列地址缓存
    PNVME_COMMAND AdminSqBase;
    PNVME_COMPLETION AdminCqBase;
    
    // I/O 队列
    IO_QUEUE_INFO IoSq[VNVME_MAX_IO_QUEUES];
    IO_QUEUE_INFO IoCq[VNVME_MAX_IO_QUEUES];
    PVNVME_QUEUE_DESCRIPTOR IoQueueDescriptors;
    
    // 数据缓冲区
    PVOID DataBuffer;
    UINT32 DataBufferSize;
    
    // 存储后端
    PBACKEND_CONTEXT Backend;
    
    // 控制器配置
    UINT32 NamespaceCount;
    UINT64 NamespaceSizes[16];          // 每个 NS 的块数
    UINT32 BlockSize;                   // 块大小
    
    // Phase Tag
    UINT32 AdminCqPhase;
    
    // 统计
    UINT64 AdminCommandsProcessed;
    UINT64 IoCommandsProcessed;
    UINT64 BytesRead;
    UINT64 BytesWritten;
} CMD_PROCESSOR_CONTEXT, *PCMD_PROCESSOR_CONTEXT;

// 全局处理器上下文
static CMD_PROCESSOR_CONTEXT g_CmdProcessor = {0};

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
 * @brief 获取 Admin SQ 地址
 */
static PNVME_COMMAND GetAdminSqEntry(UINT32 index)
{
    if (g_CmdProcessor.AdminSqBase == NULL) {
        return NULL;
    }
    return &g_CmdProcessor.AdminSqBase[index];
}

/**
 * @brief 获取 Admin CQ 地址
 */
static PNVME_COMPLETION GetAdminCqEntry(UINT32 index)
{
    if (g_CmdProcessor.AdminCqBase == NULL) {
        return NULL;
    }
    return &g_CmdProcessor.AdminCqBase[index];
}

/**
 * @brief 写入 Admin 完成
 */
static void PostAdminCompletion(
    UINT16 cid,
    UINT32 dw0,
    UINT8 sct,
    UINT8 sc
    )
{
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm = g_CmdProcessor.Shm;
    PNVME_COMPLETION cqe;
    UINT32 tail;
    
    if (shm == NULL) return;
    
    tail = shm->AdminCQ.Tail;
    cqe = GetAdminCqEntry(tail);
    if (cqe == NULL) return;
    
    cqe->DW0 = dw0;
    cqe->DW1 = 0;
    cqe->SQHD = (UINT16)shm->AdminSQ.Head;
    cqe->SQID = 0;
    cqe->CID = cid;
    cqe->Status = MakeStatus(sct, sc, (UINT8)g_CmdProcessor.AdminCqPhase);
    
    // 更新 Tail
    tail = (tail + 1) % shm->AdminCQ.Capacity;
    if (tail == 0) {
        // 翻转 Phase
        g_CmdProcessor.AdminCqPhase = !g_CmdProcessor.AdminCqPhase;
    }
    shm->AdminCQ.Tail = tail;
    
    g_CmdProcessor.AdminCommandsProcessed++;
}

/*===========================================================================
 * Identify 命令处理
 *===========================================================================*/

/**
 * @brief 处理 Identify Controller (CNS=1)
 */
static void HandleIdentifyController(PNVME_COMMAND cmd)
{
    // 在用户态模式下，我们需要将数据写到 PRP1 指向的位置
    // 但用户态无法直接访问物理地址，需要通过 IOCTL 让内核帮忙
    // 简化方案: 使用共享内存的数据缓冲区
    
    // TODO: 实现完整的 Identify Controller
    // 现在返回成功，数据由内核填充
    
    if (g_DebugMode) {
        printf("  Identify Controller (CNS=1)\n");
    }
    
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

/**
 * @brief 处理 Identify Namespace (CNS=0)
 */
static void HandleIdentifyNamespace(PNVME_COMMAND cmd)
{
    UINT32 nsid = cmd->NSID;
    
    if (g_DebugMode) {
        printf("  Identify Namespace (CNS=0, NSID=%u)\n", nsid);
    }
    
    if (nsid == 0 || nsid > g_CmdProcessor.NamespaceCount) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x0B);  // Invalid Namespace
        return;
    }
    
    // TODO: 填充 Identify Namespace 数据
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

/**
 * @brief 处理 Identify 命令
 */
static void HandleIdentify(PNVME_COMMAND cmd)
{
    UINT8 cns = (UINT8)(cmd->CDW10 & 0xFF);
    
    switch (cns) {
        case 0x00:  // Identify Namespace
            HandleIdentifyNamespace(cmd);
            break;
            
        case 0x01:  // Identify Controller
            HandleIdentifyController(cmd);
            break;
            
        case 0x02:  // Active Namespace ID list
            if (g_DebugMode) {
                printf("  Identify Active NS List (CNS=2)\n");
            }
            PostAdminCompletion(cmd->CID, 0, 0, 0);
            break;
            
        default:
            if (g_DebugMode) {
                printf("  Identify CNS=%u (unsupported)\n", cns);
            }
            PostAdminCompletion(cmd->CID, 0, 0, 0x02);  // Invalid Field
            break;
    }
}

/*===========================================================================
 * Queue 管理命令
 *===========================================================================*/

static void HandleCreateIoCq(PNVME_COMMAND cmd)
{
    UINT16 qid = (UINT16)(cmd->CDW10 & 0xFFFF);
    UINT16 qsize = (UINT16)((cmd->CDW10 >> 16) & 0xFFFF) + 1;
    UINT32 queueIndex;
    
    if (g_DebugMode) {
        printf("  Create I/O CQ: QID=%u, Size=%u\n", qid, qsize);
    }
    
    // 验证 QID
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);  // Invalid Queue Identifier
        return;
    }
    
    queueIndex = qid - 1;
    
    // 检查是否已存在
    if (g_CmdProcessor.IoCq[queueIndex].Created) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);  // Invalid Queue Identifier
        return;
    }
    
    // 初始化 CQ 信息
    g_CmdProcessor.IoCq[queueIndex].Created = TRUE;
    g_CmdProcessor.IoCq[queueIndex].Capacity = qsize;
    g_CmdProcessor.IoCq[queueIndex].Phase = 1;
    g_CmdProcessor.IoCq[queueIndex].CqBase = (PNVME_COMPLETION)VnvmeGetIoCQ(
        g_CmdProcessor.ShmAddress, queueIndex);
    
    // 更新 I/O 队列描述符
    if (g_CmdProcessor.IoQueueDescriptors != NULL) {
        PVNVME_QUEUE_DESCRIPTOR cqDesc = &g_CmdProcessor.IoQueueDescriptors[queueIndex * 2 + 1];
        cqDesc->Valid = 1;
        cqDesc->Capacity = qsize;
        cqDesc->EntrySize = sizeof(NVME_COMPLETION);
        cqDesc->Head = 0;
        cqDesc->Tail = 0;
        cqDesc->Phase = 1;
        g_CmdProcessor.IoCq[queueIndex].Descriptor = cqDesc;
    }
    
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

static void HandleCreateIoSq(PNVME_COMMAND cmd)
{
    UINT16 qid = (UINT16)(cmd->CDW10 & 0xFFFF);
    UINT16 qsize = (UINT16)((cmd->CDW10 >> 16) & 0xFFFF) + 1;
    UINT16 cqid = (UINT16)(cmd->CDW11 & 0xFFFF);
    UINT32 queueIndex;
    
    if (g_DebugMode) {
        printf("  Create I/O SQ: QID=%u, Size=%u, CQID=%u\n", qid, qsize, cqid);
    }
    
    // 验证 QID
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);  // Invalid Queue Identifier
        return;
    }
    
    queueIndex = qid - 1;
    
    // 验证关联的 CQ 是否存在
    if (cqid == 0 || cqid > VNVME_MAX_IO_QUEUES || !g_CmdProcessor.IoCq[cqid - 1].Created) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x00);  // Completion Queue Invalid
        return;
    }
    
    // 检查是否已存在
    if (g_CmdProcessor.IoSq[queueIndex].Created) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);  // Invalid Queue Identifier
        return;
    }
    
    // 初始化 SQ 信息
    g_CmdProcessor.IoSq[queueIndex].Created = TRUE;
    g_CmdProcessor.IoSq[queueIndex].Capacity = qsize;
    g_CmdProcessor.IoSq[queueIndex].CqId = cqid;
    g_CmdProcessor.IoSq[queueIndex].SqBase = (PNVME_COMMAND)VnvmeGetIoSQ(
        g_CmdProcessor.ShmAddress, queueIndex);
    
    // 更新 I/O 队列描述符
    if (g_CmdProcessor.IoQueueDescriptors != NULL) {
        PVNVME_QUEUE_DESCRIPTOR sqDesc = &g_CmdProcessor.IoQueueDescriptors[queueIndex * 2];
        sqDesc->Valid = 1;
        sqDesc->Capacity = qsize;
        sqDesc->EntrySize = sizeof(NVME_COMMAND);
        sqDesc->Head = 0;
        sqDesc->Tail = 0;
        g_CmdProcessor.IoSq[queueIndex].Descriptor = sqDesc;
    }
    
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

static void HandleDeleteIoCq(PNVME_COMMAND cmd)
{
    UINT16 qid = (UINT16)(cmd->CDW10 & 0xFFFF);
    UINT32 queueIndex;
    
    if (g_DebugMode) {
        printf("  Delete I/O CQ: QID=%u\n", qid);
    }
    
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);
        return;
    }
    
    queueIndex = qid - 1;
    
    if (!g_CmdProcessor.IoCq[queueIndex].Created) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);
        return;
    }
    
    // 清除 CQ
    memset(&g_CmdProcessor.IoCq[queueIndex], 0, sizeof(IO_QUEUE_INFO));
    
    if (g_CmdProcessor.IoQueueDescriptors != NULL) {
        PVNVME_QUEUE_DESCRIPTOR cqDesc = &g_CmdProcessor.IoQueueDescriptors[queueIndex * 2 + 1];
        cqDesc->Valid = 0;
    }
    
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

static void HandleDeleteIoSq(PNVME_COMMAND cmd)
{
    UINT16 qid = (UINT16)(cmd->CDW10 & 0xFFFF);
    UINT32 queueIndex;
    
    if (g_DebugMode) {
        printf("  Delete I/O SQ: QID=%u\n", qid);
    }
    
    if (qid == 0 || qid > VNVME_MAX_IO_QUEUES) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);
        return;
    }
    
    queueIndex = qid - 1;
    
    if (!g_CmdProcessor.IoSq[queueIndex].Created) {
        PostAdminCompletion(cmd->CID, 0, 0, 0x01);
        return;
    }
    
    // 清除 SQ
    memset(&g_CmdProcessor.IoSq[queueIndex], 0, sizeof(IO_QUEUE_INFO));
    
    if (g_CmdProcessor.IoQueueDescriptors != NULL) {
        PVNVME_QUEUE_DESCRIPTOR sqDesc = &g_CmdProcessor.IoQueueDescriptors[queueIndex * 2];
        sqDesc->Valid = 0;
    }
    
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

/*===========================================================================
 * Features 命令
 *===========================================================================*/

static void HandleSetFeatures(PNVME_COMMAND cmd)
{
    UINT8 fid = (UINT8)(cmd->CDW10 & 0xFF);
    
    if (g_DebugMode) {
        printf("  Set Features: FID=0x%02X, CDW11=0x%08X\n", fid, cmd->CDW11);
    }
    
    switch (fid) {
        case 0x07:  // Number of Queues
            {
                UINT16 nsqr = (UINT16)(cmd->CDW11 & 0xFFFF);
                UINT16 ncqr = (UINT16)((cmd->CDW11 >> 16) & 0xFFFF);
                UINT16 nsqa = (nsqr < VNVME_MAX_IO_QUEUES - 1) ? nsqr : (VNVME_MAX_IO_QUEUES - 1);
                UINT16 ncqa = (ncqr < VNVME_MAX_IO_QUEUES - 1) ? ncqr : (VNVME_MAX_IO_QUEUES - 1);
                UINT32 dw0 = nsqa | ((UINT32)ncqa << 16);
                
                if (g_DebugMode) {
                    printf("    Number of Queues: NSQR=%u, NCQR=%u -> NSQA=%u, NCQA=%u\n",
                           nsqr, ncqr, nsqa, ncqa);
                }
                
                PostAdminCompletion(cmd->CID, dw0, 0, 0);
            }
            break;
            
        default:
            PostAdminCompletion(cmd->CID, 0, 0, 0);
            break;
    }
}

static void HandleGetFeatures(PNVME_COMMAND cmd)
{
    UINT8 fid = (UINT8)(cmd->CDW10 & 0xFF);
    
    if (g_DebugMode) {
        printf("  Get Features: FID=0x%02X\n", fid);
    }
    
    switch (fid) {
        case 0x07:  // Number of Queues
            {
                UINT16 nsqa = VNVME_MAX_IO_QUEUES - 1;
                UINT16 ncqa = VNVME_MAX_IO_QUEUES - 1;
                UINT32 dw0 = nsqa | ((UINT32)ncqa << 16);
                PostAdminCompletion(cmd->CID, dw0, 0, 0);
            }
            break;
            
        default:
            PostAdminCompletion(cmd->CID, 0, 0, 0);
            break;
    }
}

/*===========================================================================
 * 其他 Admin 命令
 *===========================================================================*/

static void HandleAbort(PNVME_COMMAND cmd)
{
    if (g_DebugMode) {
        printf("  Abort: SQID=%u, CID=%u\n",
               cmd->CDW10 & 0xFFFF, (cmd->CDW10 >> 16) & 0xFFFF);
    }
    
    // 返回 "Abort not found" - 表示命令已完成
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

static void HandleKeepAlive(PNVME_COMMAND cmd)
{
    if (g_DebugMode) {
        printf("  Keep Alive\n");
    }
    
    PostAdminCompletion(cmd->CID, 0, 0, 0);
}

/*===========================================================================
 * Admin 命令分发
 *===========================================================================*/

/**
 * @brief 处理单个 Admin 命令
 */
static void ProcessAdminCommand(PNVME_COMMAND cmd)
{
    if (g_DebugMode) {
        printf("Admin Command: OPC=0x%02X, CID=%u, NSID=%u\n",
               cmd->OPC, cmd->CID, cmd->NSID);
    }
    
    switch (cmd->OPC) {
        case NVME_ADMIN_IDENTIFY:
            HandleIdentify(cmd);
            break;
            
        case NVME_ADMIN_CREATE_IO_CQ:
            HandleCreateIoCq(cmd);
            break;
            
        case NVME_ADMIN_CREATE_IO_SQ:
            HandleCreateIoSq(cmd);
            break;
            
        case NVME_ADMIN_DELETE_IO_CQ:
            HandleDeleteIoCq(cmd);
            break;
            
        case NVME_ADMIN_DELETE_IO_SQ:
            HandleDeleteIoSq(cmd);
            break;
            
        case NVME_ADMIN_SET_FEATURES:
            HandleSetFeatures(cmd);
            break;
            
        case NVME_ADMIN_GET_FEATURES:
            HandleGetFeatures(cmd);
            break;
            
        case NVME_ADMIN_ABORT:
            HandleAbort(cmd);
            break;
            
        case NVME_ADMIN_KEEP_ALIVE:
            HandleKeepAlive(cmd);
            break;
            
        default:
            if (g_DebugMode) {
                printf("  Unknown opcode 0x%02X\n", cmd->OPC);
            }
            PostAdminCompletion(cmd->CID, 0, 0, 0x01);  // Invalid Opcode
            break;
    }
}

/*===========================================================================
 * I/O 命令处理
 *===========================================================================*/

/**
 * @brief 写入 I/O 完成
 */
static void PostIoCompletion(
    UINT16 queueId,
    UINT16 cid,
    UINT32 dw0,
    UINT8 sct,
    UINT8 sc
    )
{
    UINT32 cqIndex;
    PIO_QUEUE_INFO cqInfo;
    PNVME_COMPLETION cqe;
    UINT32 tail;
    
    if (queueId == 0 || queueId > VNVME_MAX_IO_QUEUES) {
        return;
    }
    
    // 获取关联的 CQ ID (从 SQ 信息)
    cqIndex = g_CmdProcessor.IoSq[queueId - 1].CqId - 1;
    cqInfo = &g_CmdProcessor.IoCq[cqIndex];
    
    if (!cqInfo->Created || cqInfo->CqBase == NULL) {
        return;
    }
    
    // 获取 CQ 描述符中的 Tail
    tail = cqInfo->Descriptor ? cqInfo->Descriptor->Tail : 0;
    
    cqe = &cqInfo->CqBase[tail];
    cqe->DW0 = dw0;
    cqe->DW1 = 0;
    cqe->SQHD = (UINT16)(g_CmdProcessor.IoSq[queueId - 1].Descriptor ? 
                         g_CmdProcessor.IoSq[queueId - 1].Descriptor->Head : 0);
    cqe->SQID = queueId;
    cqe->CID = cid;
    cqe->Status = MakeStatus(sct, sc, (UINT8)cqInfo->Phase);
    
    // 更新 Tail
    tail = (tail + 1) % cqInfo->Capacity;
    if (tail == 0) {
        cqInfo->Phase = !cqInfo->Phase;
    }
    
    if (cqInfo->Descriptor) {
        cqInfo->Descriptor->Tail = tail;
        cqInfo->Descriptor->Phase = cqInfo->Phase;
    }
}

/**
 * @brief 处理 Read 命令
 * 
 * 用户态 Read 流程:
 * 1. 验证参数
 * 2. 从后端读取数据到共享内存数据缓冲区
 * 3. 写入完成 (内核会从数据缓冲区复制到 PRP)
 */
static void ProcessIoRead(PNVME_COMMAND cmd, UINT16 queueId)
{
    UINT32 nsid = cmd->NSID;
    UINT64 slba = cmd->CDW10 | ((UINT64)cmd->CDW11 << 32);
    UINT16 nlb = (UINT16)(cmd->CDW12 & 0xFFFF) + 1;
    UINT64 byteOffset;
    UINT32 length;
    UINT8 sct = 0, sc = 0;
    
    // 验证 NSID
    if (nsid == 0 || nsid > g_CmdProcessor.NamespaceCount) {
        PostIoCompletion(queueId, cmd->CID, 0, 0, 0x0B);  // Invalid Namespace
        g_CmdProcessor.IoCommandsProcessed++;
        return;
    }
    
    length = nlb * g_CmdProcessor.BlockSize;
    byteOffset = slba * g_CmdProcessor.BlockSize;
    
    // 验证 LBA 范围
    if (slba + nlb > g_CmdProcessor.NamespaceSizes[nsid - 1]) {
        PostIoCompletion(queueId, cmd->CID, 0, 0, 0x80);  // LBA Out of Range
        g_CmdProcessor.IoCommandsProcessed++;
        return;
    }
    
    if (g_DebugMode) {
        printf("I/O Read: QID=%u, NSID=%u, LBA=%llu, NLB=%u, Len=%u\n",
               queueId, nsid, slba, nlb, length);
    }
    
    // 从后端读取数据到数据缓冲区
    if (g_CmdProcessor.Backend && g_CmdProcessor.DataBuffer && length <= g_CmdProcessor.DataBufferSize) {
        if (!BackendRead(g_CmdProcessor.Backend, byteOffset, g_CmdProcessor.DataBuffer, length)) {
            sct = 0x02;  // Media Error
            sc = 0x81;   // Unrecovered Read Error
        }
    }
    
    g_CmdProcessor.BytesRead += length;
    PostIoCompletion(queueId, cmd->CID, 0, sct, sc);
    g_CmdProcessor.IoCommandsProcessed++;
}

/**
 * @brief 处理 Write 命令
 * 
 * 用户态 Write 流程:
 * 1. 验证参数
 * 2. 从共享内存数据缓冲区写入后端 (内核已从 PRP 复制)
 * 3. 写入完成
 */
static void ProcessIoWrite(PNVME_COMMAND cmd, UINT16 queueId)
{
    UINT32 nsid = cmd->NSID;
    UINT64 slba = cmd->CDW10 | ((UINT64)cmd->CDW11 << 32);
    UINT16 nlb = (UINT16)(cmd->CDW12 & 0xFFFF) + 1;
    UINT64 byteOffset;
    UINT32 length;
    UINT8 sct = 0, sc = 0;
    
    // 验证 NSID
    if (nsid == 0 || nsid > g_CmdProcessor.NamespaceCount) {
        PostIoCompletion(queueId, cmd->CID, 0, 0, 0x0B);  // Invalid Namespace
        g_CmdProcessor.IoCommandsProcessed++;
        return;
    }
    
    length = nlb * g_CmdProcessor.BlockSize;
    byteOffset = slba * g_CmdProcessor.BlockSize;
    
    // 验证 LBA 范围
    if (slba + nlb > g_CmdProcessor.NamespaceSizes[nsid - 1]) {
        PostIoCompletion(queueId, cmd->CID, 0, 0, 0x80);  // LBA Out of Range
        g_CmdProcessor.IoCommandsProcessed++;
        return;
    }
    
    if (g_DebugMode) {
        printf("I/O Write: QID=%u, NSID=%u, LBA=%llu, NLB=%u, Len=%u\n",
               queueId, nsid, slba, nlb, length);
    }
    
    // 从数据缓冲区写入后端
    if (g_CmdProcessor.Backend && g_CmdProcessor.DataBuffer && length <= g_CmdProcessor.DataBufferSize) {
        if (!BackendWrite(g_CmdProcessor.Backend, byteOffset, g_CmdProcessor.DataBuffer, length)) {
            sct = 0x02;  // Media Error
            sc = 0x82;   // Write Fault
        }
    }
    
    g_CmdProcessor.BytesWritten += length;
    PostIoCompletion(queueId, cmd->CID, 0, sct, sc);
    g_CmdProcessor.IoCommandsProcessed++;
}

/**
 * @brief 处理 Write Zeroes 命令
 */
static void ProcessIoWriteZeroes(PNVME_COMMAND cmd, UINT16 queueId)
{
    UINT32 nsid = cmd->NSID;
    UINT64 slba = cmd->CDW10 | ((UINT64)cmd->CDW11 << 32);
    UINT16 nlb = (UINT16)(cmd->CDW12 & 0xFFFF) + 1;
    UINT64 byteOffset;
    UINT64 length;
    UINT8 sct = 0, sc = 0;
    
    // 验证 NSID
    if (nsid == 0 || nsid > g_CmdProcessor.NamespaceCount) {
        PostIoCompletion(queueId, cmd->CID, 0, 0, 0x0B);
        g_CmdProcessor.IoCommandsProcessed++;
        return;
    }
    
    length = (UINT64)nlb * g_CmdProcessor.BlockSize;
    byteOffset = slba * g_CmdProcessor.BlockSize;
    
    // 验证 LBA 范围
    if (slba + nlb > g_CmdProcessor.NamespaceSizes[nsid - 1]) {
        PostIoCompletion(queueId, cmd->CID, 0, 0, 0x80);
        g_CmdProcessor.IoCommandsProcessed++;
        return;
    }
    
    if (g_DebugMode) {
        printf("I/O Write Zeroes: QID=%u, NSID=%u, LBA=%llu, NLB=%u\n",
               queueId, nsid, slba, nlb);
    }
    
    // 写零到后端
    if (g_CmdProcessor.Backend) {
        if (!BackendWriteZeroes(g_CmdProcessor.Backend, byteOffset, length)) {
            sct = 0x02;
            sc = 0x82;
        }
    }
    
    g_CmdProcessor.BytesWritten += length;
    PostIoCompletion(queueId, cmd->CID, 0, sct, sc);
    g_CmdProcessor.IoCommandsProcessed++;
}

/**
 * @brief 处理 Flush 命令
 */
static void ProcessIoFlush(PNVME_COMMAND cmd, UINT16 queueId)
{
    UINT8 sct = 0, sc = 0;
    
    if (g_DebugMode) {
        printf("I/O Flush: QID=%u, NSID=%u\n", queueId, cmd->NSID);
    }
    
    if (g_CmdProcessor.Backend) {
        if (!BackendFlush(g_CmdProcessor.Backend)) {
            sct = 0x02;
            sc = 0x82;
        }
    }
    
    PostIoCompletion(queueId, cmd->CID, 0, sct, sc);
    g_CmdProcessor.IoCommandsProcessed++;
}

/**
 * @brief 处理单个 I/O 命令
 */
static void ProcessIoCommand(PNVME_COMMAND cmd, UINT16 queueId)
{
    switch (cmd->OPC) {
        case NVME_IO_READ:
            ProcessIoRead(cmd, queueId);
            break;
            
        case NVME_IO_WRITE:
            ProcessIoWrite(cmd, queueId);
            break;
            
        case NVME_IO_FLUSH:
            ProcessIoFlush(cmd, queueId);
            break;
            
        case NVME_IO_WRITE_ZEROES:
            ProcessIoWriteZeroes(cmd, queueId);
            break;
            
        default:
            if (g_DebugMode) {
                printf("I/O Unknown: OPC=0x%02X, QID=%u\n", cmd->OPC, queueId);
            }
            // 返回 Invalid Opcode
            PostIoCompletion(queueId, cmd->CID, 0, 0, 0x01);
            g_CmdProcessor.IoCommandsProcessed++;
            break;
    }
}

/*===========================================================================
 * 公共 API
 *===========================================================================*/

/**
 * @brief 初始化命令处理器
 */
BOOL CmdProcessorInit(PVOID shmAddress, PBACKEND_CONTEXT backend)
{
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm;
    UINT64 backendSize;
    
    if (shmAddress == NULL) {
        return FALSE;
    }
    
    shm = VnvmeGetControlBlock(shmAddress);
    if (shm->Magic != VNVME_SHARED_MEMORY_MAGIC) {
        fprintf(stderr, "CmdProcessorInit: Invalid shared memory magic\n");
        return FALSE;
    }
    
    // 清零上下文
    memset(&g_CmdProcessor, 0, sizeof(g_CmdProcessor));
    
    g_CmdProcessor.ShmAddress = shmAddress;
    g_CmdProcessor.Shm = shm;
    g_CmdProcessor.Backend = backend;
    
    // 计算 Admin 队列地址
    g_CmdProcessor.AdminSqBase = (PNVME_COMMAND)((PUCHAR)shmAddress + shm->AdminSQ.Offset);
    g_CmdProcessor.AdminCqBase = (PNVME_COMPLETION)((PUCHAR)shmAddress + shm->AdminCQ.Offset);
    
    // 初始化 I/O 队列描述符指针
    if (shm->IoQueueDescriptorOffset != 0) {
        g_CmdProcessor.IoQueueDescriptors = (PVNVME_QUEUE_DESCRIPTOR)(
            (PUCHAR)shmAddress + shm->IoQueueDescriptorOffset);
        printf("  I/O queue descriptors at offset 0x%X\n", shm->IoQueueDescriptorOffset);
    }
    
    // 初始化数据缓冲区
    if (shm->DataBufferOffset != 0 && shm->DataBufferSize != 0) {
        g_CmdProcessor.DataBuffer = (PUCHAR)shmAddress + shm->DataBufferOffset;
        g_CmdProcessor.DataBufferSize = shm->DataBufferSize;
        printf("  Data buffer at offset 0x%X, size %u bytes\n", 
               shm->DataBufferOffset, shm->DataBufferSize);
    }
    
    // 配置 Namespace
    g_CmdProcessor.BlockSize = 512;
    g_CmdProcessor.NamespaceCount = 1;
    
    // 根据后端大小计算 NS 大小
    if (backend != NULL) {
        backendSize = BackendGetSize(backend);
        g_CmdProcessor.NamespaceSizes[0] = backendSize / g_CmdProcessor.BlockSize;
        printf("  Namespace 1: %llu blocks (%llu MB)\n",
               g_CmdProcessor.NamespaceSizes[0],
               backendSize / (1024 * 1024));
    } else {
        g_CmdProcessor.NamespaceSizes[0] = 128 * 1024;  // 64MB @ 512B
    }
    
    g_CmdProcessor.AdminCqPhase = 1;
    
    printf("Command processor initialized\n");
    printf("  Admin SQ at offset 0x%X\n", shm->AdminSQ.Offset);
    printf("  Admin CQ at offset 0x%X\n", shm->AdminCQ.Offset);
    
    return TRUE;
}

/**
 * @brief 处理所有待处理命令
 * 
 * @return 处理的命令数
 */
UINT64 CmdProcessorRun(void)
{
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm = g_CmdProcessor.Shm;
    PVNVME_NOTIFY_RING notifyRing;
    UINT32 head, tail;
    UINT64 processed = 0;
    
    if (shm == NULL) {
        return 0;
    }
    
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
                    PNVME_COMMAND cmd = GetAdminSqEntry(sqHead);
                    if (cmd != NULL) {
                        ProcessAdminCommand(cmd);
                        processed++;
                    }
                    sqHead = (sqHead + 1) % sqCap;
                }
                
                shm->AdminSQ.Head = sqHead;
            } else {
                // I/O 队列
                UINT16 queueId = entry->QueueId;
                UINT32 queueIndex = queueId - 1;
                
                if (queueIndex < VNVME_MAX_IO_QUEUES && g_CmdProcessor.IoSq[queueIndex].Created) {
                    PIO_QUEUE_INFO sqInfo = &g_CmdProcessor.IoSq[queueIndex];
                    PVNVME_QUEUE_DESCRIPTOR sqDesc = sqInfo->Descriptor;
                    
                    if (sqDesc != NULL && sqInfo->SqBase != NULL) {
                        UINT32 sqHead = sqDesc->Head;
                        UINT32 sqTail = entry->Index;
                        UINT32 sqCap = sqInfo->Capacity;
                        
                        while (sqHead != sqTail) {
                            PNVME_COMMAND cmd = &sqInfo->SqBase[sqHead];
                            ProcessIoCommand(cmd, queueId);
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
    
    // 更新统计
    shm->CommandsProcessed = g_CmdProcessor.AdminCommandsProcessed + 
                             g_CmdProcessor.IoCommandsProcessed;
    shm->BytesRead = g_CmdProcessor.BytesRead;
    shm->BytesWritten = g_CmdProcessor.BytesWritten;
    
    return processed;
}

/**
 * @brief 获取统计信息
 */
void CmdProcessorGetStats(UINT64* adminCmds, UINT64* ioCmds, UINT64* bytesRead, UINT64* bytesWritten)
{
    if (adminCmds) *adminCmds = g_CmdProcessor.AdminCommandsProcessed;
    if (ioCmds) *ioCmds = g_CmdProcessor.IoCommandsProcessed;
    if (bytesRead) *bytesRead = g_CmdProcessor.BytesRead;
    if (bytesWritten) *bytesWritten = g_CmdProcessor.BytesWritten;
}
