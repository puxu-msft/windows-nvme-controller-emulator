/**
 * @file queue.c
 * @brief 队列管理
 * 
 * NVMe Admin 和 I/O 队列的创建、删除和管理。
 */

#include "vnvme.h"

//===========================================================================
// Admin 队列管理
//===========================================================================

/**
 * @brief 初始化 Admin 队列
 */
NTSTATUS
VnvmeInitializeAdminQueues(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PNVME_CONTROLLER_REGISTERS regs;
    ULONGLONG asq, acq;
    ULONG aqa;
    ULONG asqs, acqs;
    
    TRACE_INFO("VnvmeInitializeAdminQueues");
    
    if (PdoContext->Bar0VirtAddr == NULL) {
        TRACE_ERROR("VnvmeInitializeAdminQueues: BAR0 not allocated");
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    regs = (PNVME_CONTROLLER_REGISTERS)PdoContext->Bar0VirtAddr;
    
    // 读取 Admin Queue Attributes
    aqa = regs->AQA.AsUint32;
    asqs = (aqa & 0xFFF) + 1;        // Admin Submission Queue Size
    acqs = ((aqa >> 16) & 0xFFF) + 1; // Admin Completion Queue Size
    
    // 读取队列基地址
    asq = regs->ASQ;
    acq = regs->ACQ;
    
    TRACE_INFO("VnvmeInitializeAdminQueues: ASQ=0x%016llX, ACQ=0x%016llX", asq, acq);
    TRACE_INFO("VnvmeInitializeAdminQueues: ASQS=%u, ACQS=%u", asqs, acqs);
    
    // 保存队列信息
    PdoContext->AdminSqBase = asq;
    PdoContext->AdminSqSize = asqs;
    PdoContext->AdminCqBase = acq;
    PdoContext->AdminCqSize = acqs;
    
    // 重置队列状态
    PdoContext->LastAdminSqTail = 0;
    PdoContext->LastAdminCqHead = 0;
    PdoContext->AdminCqPhase = 1;
    
    return STATUS_SUCCESS;
}

//===========================================================================
// I/O 队列管理
//===========================================================================

/**
 * @brief 创建 I/O Submission Queue
 */
NTSTATUS
VnvmeCreateIoSubmissionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ USHORT QueueSize,
    _In_ ULONGLONG PrpAddress,
    _In_ USHORT CqId
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_QUEUE_DESCRIPTOR ioDescs;
    PVNVME_QUEUE_DESCRIPTOR sqDesc;
    ULONG queueIndex;
    ULONG sqOffset;
    
    TRACE_INFO("VnvmeCreateIoSubmissionQueue: QID=%u, Size=%u, CQ=%u, PRP=0x%016llX",
               QueueId, QueueSize, CqId, PrpAddress);
    
    // 验证 QueueId 范围 (使用运行时配置限制)
    if (QueueId == 0 || QueueId > CONFIG_MAX_IO_QUEUES) {
        TRACE_ERROR("VnvmeCreateIoSubmissionQueue: Invalid QID %u (max=%u)", 
                    QueueId, CONFIG_MAX_IO_QUEUES);
        return STATUS_INVALID_PARAMETER;
    }
    
    queueIndex = QueueId - 1;
    
    // 验证对应的 CQ 是否存在
    if (CqId == 0 || CqId > CONFIG_MAX_IO_QUEUES) {
        TRACE_ERROR("VnvmeCreateIoSubmissionQueue: Invalid CQ ID %u (max=%u)", 
                    CqId, CONFIG_MAX_IO_QUEUES);
        return STATUS_INVALID_PARAMETER;
    }
    
    if (!PdoContext->IoCq[VNVME_QUEUE_ID_TO_INDEX(CqId)].Created) {
        TRACE_ERROR("VnvmeCreateIoSubmissionQueue: CQ %u not created", CqId);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 检查 SQ 是否已存在
    if (PdoContext->IoSq[queueIndex].Created) {
        TRACE_WARN("VnvmeCreateIoSubmissionQueue: SQ %u already exists", QueueId);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    
    // 获取共享内存和队列描述符
    shm = VnvmeShmGetControlBlock(NULL);
    ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
    
    if (shm == NULL || ioDescs == NULL) {
        TRACE_ERROR("VnvmeCreateIoSubmissionQueue: Shared memory not available");
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 计算 SQ 在共享内存中的偏移
    // I/O 队列布局: [SQ0][CQ0][SQ1][CQ1]...
    // 每个 SQ = VNVME_IO_QUEUE_DEPTH * 64 字节
    // 每个 CQ = VNVME_IO_QUEUE_DEPTH * 16 字节
    sqOffset = shm->IoQueueDescriptorOffset + 
               (VNVME_MAX_IO_QUEUES * 2 * sizeof(VNVME_QUEUE_DESCRIPTOR)) +
               (queueIndex * (VNVME_IO_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE + 
                              VNVME_IO_QUEUE_DEPTH * NVME_CQ_ENTRY_SIZE));
    
    // 初始化 PDO 上下文中的 SQ 状态
    PdoContext->IoSq[queueIndex].BaseAddress.QuadPart = PrpAddress;
    PdoContext->IoSq[queueIndex].Size = QueueSize;
    PdoContext->IoSq[queueIndex].Head = 0;
    PdoContext->IoSq[queueIndex].Tail = 0;
    PdoContext->IoSq[queueIndex].Created = TRUE;
    
    // 初始化共享内存中的队列描述符 (索引 queueIndex * 2 是 SQ)
    sqDesc = &ioDescs[queueIndex * 2];
    sqDesc->Offset = sqOffset;
    sqDesc->EntrySize = NVME_SQ_ENTRY_SIZE;
    sqDesc->Capacity = QueueSize;
    sqDesc->Head = 0;
    sqDesc->Tail = 0;
    sqDesc->Valid = 1;
    
    // 清零队列内存
    RtlZeroMemory((PUCHAR)shm + sqOffset, (SIZE_T)QueueSize * NVME_SQ_ENTRY_SIZE);
    
    PdoContext->IoQueueCount++;
    
    TRACE_INFO("VnvmeCreateIoSubmissionQueue: SQ %u created, offset=0x%X, size=%u",
               QueueId, sqOffset, QueueSize);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 创建 I/O Completion Queue
 */
NTSTATUS
VnvmeCreateIoCompletionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ USHORT QueueSize,
    _In_ ULONGLONG PrpAddress,
    _In_ USHORT IrqVector
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_QUEUE_DESCRIPTOR ioDescs;
    PVNVME_QUEUE_DESCRIPTOR cqDesc;
    ULONG queueIndex;
    ULONG cqOffset;
    
    UNREFERENCED_PARAMETER(IrqVector);  // MSI-X 向量，当前轮询模式不使用
    
    TRACE_INFO("VnvmeCreateIoCompletionQueue: QID=%u, Size=%u, IRQ=%u, PRP=0x%016llX",
               QueueId, QueueSize, IrqVector, PrpAddress);
    
    // 验证 QueueId 范围 (使用运行时配置限制)
    if (QueueId == 0 || QueueId > CONFIG_MAX_IO_QUEUES) {
        TRACE_ERROR("VnvmeCreateIoCompletionQueue: Invalid QID %u (max=%u)", 
                    QueueId, CONFIG_MAX_IO_QUEUES);
        return STATUS_INVALID_PARAMETER;
    }
    
    queueIndex = QueueId - 1;
    
    // 检查 CQ 是否已存在
    if (PdoContext->IoCq[queueIndex].Created) {
        TRACE_WARN("VnvmeCreateIoCompletionQueue: CQ %u already exists", QueueId);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    
    // 获取共享内存和队列描述符
    shm = VnvmeShmGetControlBlock(NULL);
    ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
    
    if (shm == NULL || ioDescs == NULL) {
        TRACE_ERROR("VnvmeCreateIoCompletionQueue: Shared memory not available");
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 计算 CQ 在共享内存中的偏移
    // CQ 紧跟在对应的 SQ 之后
    cqOffset = shm->IoQueueDescriptorOffset +
               (VNVME_MAX_IO_QUEUES * 2 * sizeof(VNVME_QUEUE_DESCRIPTOR)) +
               (queueIndex * (VNVME_IO_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE +
                              VNVME_IO_QUEUE_DEPTH * NVME_CQ_ENTRY_SIZE)) +
               (VNVME_IO_QUEUE_DEPTH * NVME_SQ_ENTRY_SIZE);  // 跳过 SQ
    
    // 初始化 PDO 上下文中的 CQ 状态
    PdoContext->IoCq[queueIndex].BaseAddress.QuadPart = PrpAddress;
    PdoContext->IoCq[queueIndex].Size = QueueSize;
    PdoContext->IoCq[queueIndex].Head = 0;
    PdoContext->IoCq[queueIndex].Tail = 0;
    PdoContext->IoCq[queueIndex].PhaseTag = TRUE;  // 初始 Phase = 1
    PdoContext->IoCq[queueIndex].Created = TRUE;
    
    // 初始化共享内存中的队列描述符 (索引 queueIndex * 2 + 1 是 CQ)
    cqDesc = &ioDescs[queueIndex * 2 + 1];
    cqDesc->Offset = cqOffset;
    cqDesc->EntrySize = NVME_CQ_ENTRY_SIZE;
    cqDesc->Capacity = QueueSize;
    cqDesc->Head = 0;
    cqDesc->Tail = 0;
    cqDesc->Phase = 1;
    cqDesc->Valid = 1;
    
    // 清零队列内存
    RtlZeroMemory((PUCHAR)shm + cqOffset, (SIZE_T)QueueSize * NVME_CQ_ENTRY_SIZE);
    
    TRACE_INFO("VnvmeCreateIoCompletionQueue: CQ %u created, offset=0x%X, size=%u",
               QueueId, cqOffset, QueueSize);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 删除 I/O Submission Queue
 */
NTSTATUS
VnvmeDeleteIoSubmissionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId
    )
{
    PVNVME_QUEUE_DESCRIPTOR ioDescs;
    PVNVME_QUEUE_DESCRIPTOR sqDesc;
    ULONG queueIndex;
    
    TRACE_INFO("VnvmeDeleteIoSubmissionQueue: QID=%u", QueueId);
    
    // 验证 QueueId 范围 (使用运行时配置限制)
    if (QueueId == 0 || QueueId > CONFIG_MAX_IO_QUEUES) {
        TRACE_ERROR("VnvmeDeleteIoSubmissionQueue: Invalid QID %u (max=%u)", 
                    QueueId, CONFIG_MAX_IO_QUEUES);
        return STATUS_INVALID_PARAMETER;
    }
    
    queueIndex = QueueId - 1;
    
    // 检查 SQ 是否存在
    if (!PdoContext->IoSq[queueIndex].Created) {
        TRACE_WARN("VnvmeDeleteIoSubmissionQueue: SQ %u not found", QueueId);
        return STATUS_NOT_FOUND;
    }
    
    // 清除 PDO 上下文中的 SQ 状态
    RtlZeroMemory(&PdoContext->IoSq[queueIndex], sizeof(VNVME_QUEUE_STATE));
    
    // 更新共享内存中的队列描述符
    ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
    if (ioDescs != NULL) {
        sqDesc = &ioDescs[queueIndex * 2];
        sqDesc->Valid = 0;
        sqDesc->Head = 0;
        sqDesc->Tail = 0;
    }
    
    if (PdoContext->IoQueueCount > 0) {
        PdoContext->IoQueueCount--;
    }
    
    TRACE_INFO("VnvmeDeleteIoSubmissionQueue: SQ %u deleted", QueueId);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 删除 I/O Completion Queue
 */
NTSTATUS
VnvmeDeleteIoCompletionQueue(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId
    )
{
    PVNVME_QUEUE_DESCRIPTOR ioDescs;
    PVNVME_QUEUE_DESCRIPTOR cqDesc;
    ULONG queueIndex;
    ULONG i;
    
    TRACE_INFO("VnvmeDeleteIoCompletionQueue: QID=%u", QueueId);
    
    // 验证 QueueId 范围 (使用运行时配置限制)
    if (QueueId == 0 || QueueId > CONFIG_MAX_IO_QUEUES) {
        TRACE_ERROR("VnvmeDeleteIoCompletionQueue: Invalid QID %u (max=%u)", 
                    QueueId, CONFIG_MAX_IO_QUEUES);
        return STATUS_INVALID_PARAMETER;
    }
    
    queueIndex = QueueId - 1;
    
    // 检查 CQ 是否存在
    if (!PdoContext->IoCq[queueIndex].Created) {
        TRACE_WARN("VnvmeDeleteIoCompletionQueue: CQ %u not found", QueueId);
        return STATUS_NOT_FOUND;
    }
    
    // 检查是否有 SQ 关联到这个 CQ
    // 注意: 当前简化实现假设 SQ ID = CQ ID，实际应该检查所有 SQ 的 CqId 字段
    for (i = 0; i < VNVME_MAX_IO_QUEUES; i++) {
        if (PdoContext->IoSq[i].Created) {
            // 简化检查: 如果同 ID 的 SQ 存在，拒绝删除
            // 完整实现应该跟踪 SQ -> CQ 映射
            if (i == queueIndex) {
                TRACE_ERROR("VnvmeDeleteIoCompletionQueue: CQ %u has associated SQ", QueueId);
                return STATUS_DEVICE_BUSY;
            }
        }
    }
    
    // 清除 PDO 上下文中的 CQ 状态
    RtlZeroMemory(&PdoContext->IoCq[queueIndex], sizeof(VNVME_QUEUE_STATE));
    
    // 更新共享内存中的队列描述符
    ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
    if (ioDescs != NULL) {
        cqDesc = &ioDescs[queueIndex * 2 + 1];
        cqDesc->Valid = 0;
        cqDesc->Head = 0;
        cqDesc->Tail = 0;
        cqDesc->Phase = 1;
    }
    
    TRACE_INFO("VnvmeDeleteIoCompletionQueue: CQ %u deleted", QueueId);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 队列操作
//===========================================================================

/**
 * @brief 从 SQ 获取下一个命令
 * 
 * 从指定的 Submission Queue 获取下一个待处理命令。
 * 如果队列为空，返回 STATUS_NO_MORE_ENTRIES。
 */
NTSTATUS
VnvmeFetchCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _Out_ PNVME_COMMAND Command
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_QUEUE_DESCRIPTOR sqDesc;
    PNVME_COMMAND sqBase;
    ULONG head, tail;
    
    if (Command == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    RtlZeroMemory(Command, sizeof(NVME_COMMAND));
    
    shm = VnvmeShmGetControlBlock(NULL);
    if (shm == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (QueueId == 0) {
        // Admin Queue
        sqDesc = &shm->AdminSQ;
        sqBase = (PNVME_COMMAND)((PUCHAR)shm + sqDesc->Offset);
    } else {
        // I/O Queue
        PVNVME_QUEUE_DESCRIPTOR ioDescs;
        ULONG queueIndex = QueueId - 1;
        
        if (QueueId > CONFIG_MAX_IO_QUEUES || !PdoContext->IoSq[queueIndex].Created) {
            return STATUS_INVALID_PARAMETER;
        }
        
        ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
        if (ioDescs == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }
        
        sqDesc = &ioDescs[queueIndex * 2];
        sqBase = (PNVME_COMMAND)((PUCHAR)shm + sqDesc->Offset);
    }
    
    if (!sqDesc->Valid) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    // 检查队列是否有新命令
    head = sqDesc->Head;
    tail = sqDesc->Tail;
    
    if (head == tail) {
        // 队列为空
        return STATUS_NO_MORE_ENTRIES;
    }
    
    // 复制命令
    RtlCopyMemory(Command, &sqBase[head], sizeof(NVME_COMMAND));
    
    // 更新 Head
    head = (head + 1) % sqDesc->Capacity;
    sqDesc->Head = head;
    
    // 同步到 PDO 上下文
    if (QueueId == 0) {
        PdoContext->AdminSq.Head = head;
    } else {
        PdoContext->IoSq[QueueId - 1].Head = head;
    }
    
    TRACE_VERBOSE("VnvmeFetchCommand: QID=%u, CID=%u, OPC=0x%02X",
                  QueueId, Command->CID, Command->OPC);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 向 CQ 提交完成项
 * 
 * 将完成项写入指定的 Completion Queue。
 */
NTSTATUS
VnvmePostCompletion(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMPLETION Completion
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_QUEUE_DESCRIPTOR cqDesc;
    PNVME_COMPLETION cqBase;
    PNVME_COMPLETION cqEntry;
    ULONG tail;
    BOOLEAN phase;
    
    if (Completion == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    shm = VnvmeShmGetControlBlock(NULL);
    if (shm == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (QueueId == 0) {
        // Admin Queue
        cqDesc = &shm->AdminCQ;
        cqBase = (PNVME_COMPLETION)((PUCHAR)shm + cqDesc->Offset);
        phase = PdoContext->AdminCq.PhaseTag;
    } else {
        // I/O Queue
        PVNVME_QUEUE_DESCRIPTOR ioDescs;
        ULONG queueIndex = QueueId - 1;
        
        if (QueueId > CONFIG_MAX_IO_QUEUES || !PdoContext->IoCq[queueIndex].Created) {
            return STATUS_INVALID_PARAMETER;
        }
        
        ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
        if (ioDescs == NULL) {
            return STATUS_DEVICE_NOT_READY;
        }
        
        cqDesc = &ioDescs[queueIndex * 2 + 1];
        cqBase = (PNVME_COMPLETION)((PUCHAR)shm + cqDesc->Offset);
        phase = PdoContext->IoCq[queueIndex].PhaseTag;
    }
    
    if (!cqDesc->Valid) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    tail = cqDesc->Tail;
    cqEntry = &cqBase[tail];
    
    // 写入完成项
    cqEntry->DW0 = Completion->DW0;
    cqEntry->DW1 = Completion->DW1;
    cqEntry->SQHD = Completion->SQHD;
    cqEntry->SQID = Completion->SQID;
    cqEntry->CID = Completion->CID;
    
    // 设置状态和 Phase 位
    // Status 格式: [15:1]=Status, [0]=Phase
    cqEntry->Status = (Completion->Status & 0xFFFE) | (phase ? 1 : 0);
    
    // 更新 Tail 和 Phase
    tail = (tail + 1) % cqDesc->Capacity;
    if (tail == 0) {
        // 队列回绕，翻转 Phase
        phase = !phase;
        cqDesc->Phase = phase ? 1 : 0;
        
        if (QueueId == 0) {
            PdoContext->AdminCq.PhaseTag = phase;
        } else {
            PdoContext->IoCq[QueueId - 1].PhaseTag = phase;
        }
    }
    
    cqDesc->Tail = tail;
    
    // 同步到 PDO 上下文
    if (QueueId == 0) {
        PdoContext->AdminCq.Tail = tail;
    } else {
        PdoContext->IoCq[QueueId - 1].Tail = tail;
    }
    
    // 更新统计
    InterlockedIncrement64((volatile LONG64*)&shm->CompletionsPosted);
    
    TRACE_VERBOSE("VnvmePostCompletion: QID=%u, CID=%u, Status=0x%04X",
                  QueueId, Completion->CID, Completion->Status);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 批处理优化函数
//===========================================================================

/**
 * @brief 批量获取命令
 * 
 * 从指定队列批量获取多个命令，减少轮询开销。
 * 
 * @param PdoContext 控制器上下文
 * @param QueueId 队列 ID (0 = Admin, 1+ = I/O)
 * @param Commands 输出命令数组
 * @param MaxCommands 最大获取数量
 * @param CommandsFetched 实际获取数量
 * @return NTSTATUS
 */
NTSTATUS
VnvmeFetchCommandBatch(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _Out_writes_(MaxCommands) PNVME_COMMAND Commands,
    _In_ ULONG MaxCommands,
    _Out_ PULONG CommandsFetched
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_QUEUE_DESCRIPTOR sqDesc;
    PNVME_COMMAND sqBase;
    ULONG head, tail, depth;
    ULONG fetched = 0;
    
    UNREFERENCED_PARAMETER(PdoContext);
    
    *CommandsFetched = 0;
    
    if (MaxCommands == 0) {
        return STATUS_SUCCESS;
    }
    
    shm = VnvmeShmGetControlBlock(NULL);
    if (shm == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 获取队列描述符
    if (QueueId == 0) {
        sqDesc = &shm->AdminSQ;
    } else {
        PVNVME_QUEUE_DESCRIPTOR ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
        USHORT queueIndex = QueueId - 1;
        
        // 边界检查：防止数组越界访问
        if (ioDescs == NULL || QueueId > shm->IoQueueCount || queueIndex >= VNVME_MAX_IO_QUEUES) {
            return STATUS_INVALID_PARAMETER;
        }
        sqDesc = &ioDescs[queueIndex * 2];  // SQ descriptor
    }
    
    // 获取队列状态
    head = sqDesc->Head;
    tail = sqDesc->Tail;
    depth = sqDesc->Capacity;
    
    if (head == tail) {
        // 队列为空
        return STATUS_SUCCESS;
    }
    
    // 获取 SQ 基地址
    sqBase = (PNVME_COMMAND)((PUCHAR)shm + sqDesc->Offset);
    
    // 批量获取命令
    while (head != tail && fetched < MaxCommands) {
        RtlCopyMemory(&Commands[fetched], &sqBase[head], sizeof(NVME_COMMAND));
        
        head = (head + 1) % depth;
        fetched++;
    }
    
    // 更新 Head
    sqDesc->Head = head;
    
    // 更新统计
    InterlockedAdd64((volatile LONG64*)&shm->CommandsProcessed, (LONG64)fetched);
    
    *CommandsFetched = fetched;
    
    TRACE_VERBOSE("VnvmeFetchCommandBatch: QID=%u, Fetched=%lu", QueueId, fetched);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 批量投递完成
 * 
 * 批量投递多个完成条目，提高吞吐量。
 * 
 * @param PdoContext 控制器上下文
 * @param QueueId 队列 ID (0 = Admin, 1+ = I/O)
 * @param Completions 完成条目数组
 * @param CompletionCount 完成条目数量
 * @return NTSTATUS
 */
NTSTATUS
VnvmePostCompletionBatch(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_reads_(CompletionCount) PNVME_COMPLETION Completions,
    _In_ ULONG CompletionCount
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm;
    PVNVME_QUEUE_DESCRIPTOR cqDesc;
    PNVME_COMPLETION cqBase;
    ULONG tail, depth;
    BOOLEAN phase;
    ULONG i;
    
    if (CompletionCount == 0) {
        return STATUS_SUCCESS;
    }
    
    shm = VnvmeShmGetControlBlock(NULL);
    if (shm == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 获取 CQ 描述符
    if (QueueId == 0) {
        cqDesc = &shm->AdminCQ;
        phase = PdoContext->AdminCq.PhaseTag;
    } else {
        PVNVME_QUEUE_DESCRIPTOR ioDescs = VnvmeShmGetIoQueueDescriptors(NULL);
        USHORT queueIndex = QueueId - 1;
        
        // 边界检查：防止数组越界访问
        if (ioDescs == NULL || QueueId > shm->IoQueueCount || queueIndex >= VNVME_MAX_IO_QUEUES) {
            return STATUS_INVALID_PARAMETER;
        }
        cqDesc = &ioDescs[queueIndex * 2 + 1];  // CQ descriptor
        phase = PdoContext->IoCq[queueIndex].PhaseTag;
    }
    
    tail = cqDesc->Tail;
    depth = cqDesc->Capacity;
    cqBase = (PNVME_COMPLETION)((PUCHAR)shm + cqDesc->Offset);
    
    // 批量投递
    for (i = 0; i < CompletionCount; i++) {
        PNVME_COMPLETION src = &Completions[i];
        PNVME_COMPLETION dst = &cqBase[tail];
        
        // 复制完成条目
        dst->DW0 = src->DW0;
        dst->DW1 = src->DW1;
        dst->SQHD = src->SQHD;
        dst->SQID = src->SQID;
        dst->CID = src->CID;
        dst->Status = (src->Status & 0xFFFE) | (phase ? 1 : 0);
        
        // 更新 Tail
        tail = (tail + 1) % depth;
        if (tail == 0) {
            // 队列回绕，翻转 Phase
            phase = !phase;
            cqDesc->Phase = phase ? 1 : 0;
        }
    }
    
    cqDesc->Tail = tail;
    
    // 同步到 PDO 上下文
    if (QueueId == 0) {
        PdoContext->AdminCq.Tail = tail;
        PdoContext->AdminCq.PhaseTag = phase;
    } else {
        PdoContext->IoCq[QueueId - 1].Tail = tail;
        PdoContext->IoCq[QueueId - 1].PhaseTag = phase;
    }
    
    // 更新统计
    InterlockedAdd64((volatile LONG64*)&shm->CompletionsPosted, (LONG64)CompletionCount);
    
    TRACE_VERBOSE("VnvmePostCompletionBatch: QID=%u, Count=%lu", QueueId, CompletionCount);
    
    return STATUS_SUCCESS;
}
