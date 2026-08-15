# 队列引擎

本文档详细说明 Submission Queue (SQ) 和 Completion Queue (CQ) 的管理机制。

## 概述

NVMe 使用生产者-消费者模型来处理命令：
- **Submission Queue (SQ)**：主机写入命令，控制器读取并执行
- **Completion Queue (CQ)**：控制器写入完成状态，主机读取确认

```
┌─────────────────────────────────────────────────────────────────────┐
│                         队列模型                                      │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│     Host (stornvme.sys)              Controller (vnvme.sys)        │
│                                                                      │
│     ┌──────────────┐                 ┌──────────────┐               │
│     │              │    Write SQ     │              │               │
│     │   Submit     │ ──────────────► │   Process    │               │
│     │   Command    │    Doorbell     │   Command    │               │
│     │              │                 │              │               │
│     └──────────────┘                 └──────────────┘               │
│            │                                │                        │
│            │                                │                        │
│            │         Read CQ Entry          │                        │
│            │ ◄─────────────────────────────│                        │
│            │        + MSI-X Interrupt       │                        │
│            │                                │                        │
│            ▼                                ▼                        │
│     ┌──────────────┐                 ┌──────────────┐               │
│     │   Update     │    CQ Head      │   Update     │               │
│     │   CQ Head    │ ──────────────► │   CQ Head    │               │
│     │              │    Doorbell     │   Pointer    │               │
│     └──────────────┘                 └──────────────┘               │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## 数据结构

### Submission Queue 结构

```c
typedef struct _VNVME_SUBMISSION_QUEUE {
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 队列标识
    USHORT QueueId;
    
    // 队列大小 (条目数)
    USHORT Size;
    
    // 队列基地址 (物理地址)
    ULONG64 BaseAddr;
    
    // 映射的虚拟地址
    PVOID VirtAddr;
    
    // Head/Tail 指针
    USHORT Head;            // 控制器消费位置
    USHORT Tail;            // 主机生产位置 (由 Doorbell 更新)
    
    // 关联的 Completion Queue
    struct _VNVME_COMPLETION_QUEUE* CQ;
    
    // 队列优先级
    UCHAR Priority;
    
    // 统计信息
    ULONG64 CommandsProcessed;
    ULONG64 CommandErrors;
    
    // 自旋锁
    KSPIN_LOCK Lock;
    
} VNVME_SUBMISSION_QUEUE, *PVNVME_SUBMISSION_QUEUE;

// 队列优先级
#define VNVME_SQ_PRIORITY_URGENT    0
#define VNVME_SQ_PRIORITY_HIGH      1
#define VNVME_SQ_PRIORITY_MEDIUM    2
#define VNVME_SQ_PRIORITY_LOW       3
```

### Completion Queue 结构

```c
typedef struct _VNVME_COMPLETION_QUEUE {
    // 链表节点
    LIST_ENTRY ListEntry;
    
    // 队列标识
    USHORT QueueId;
    
    // 队列大小 (条目数)
    USHORT Size;
    
    // 队列基地址 (物理地址)
    ULONG64 BaseAddr;
    
    // 映射的虚拟地址
    PVOID VirtAddr;
    
    // Head/Tail 指针
    USHORT Head;            // 主机消费位置 (由 Doorbell 更新)
    USHORT Tail;            // 控制器生产位置
    
    // Phase Tag (每次队列回绕时翻转)
    BOOLEAN Phase;
    
    // 中断配置
    BOOLEAN InterruptEnabled;
    USHORT Vector;
    
    // 关联的 SQ 列表
    LIST_ENTRY SqList;
    
    // 统计信息
    ULONG64 CompletionsPosted;
    
    // 自旋锁
    KSPIN_LOCK Lock;
    
} VNVME_COMPLETION_QUEUE, *PVNVME_COMPLETION_QUEUE;
```

## Admin Queue

### Admin Queue 初始化

Admin Queue (SQ0/CQ0) 在控制器启动时通过寄存器配置：

```c
NTSTATUS VnvmeInitializeAdminQueues(
    _In_ PVNVME_CONTROLLER Controller)
{
    NTSTATUS status;
    
    // 从 AQA 寄存器获取队列大小
    USHORT asqSize = (Controller->Registers.AQA & 0x0FFF) + 1;
    USHORT acqSize = ((Controller->Registers.AQA >> 16) & 0x0FFF) + 1;
    
    // 初始化 Admin Submission Queue (SQ0)
    PVNVME_SUBMISSION_QUEUE adminSq = &Controller->AdminSQ;
    RtlZeroMemory(adminSq, sizeof(*adminSq));
    
    adminSq->QueueId = 0;
    adminSq->Size = asqSize;
    adminSq->BaseAddr = Controller->Registers.ASQ;
    adminSq->Head = 0;
    adminSq->Tail = 0;
    adminSq->Priority = VNVME_SQ_PRIORITY_URGENT;
    KeInitializeSpinLock(&adminSq->Lock);
    
    // 映射 Admin SQ 内存
    adminSq->VirtAddr = VnvmeMapPhysicalMemory(
        adminSq->BaseAddr,
        adminSq->Size * sizeof(NVME_COMMAND));
    
    if (!adminSq->VirtAddr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 初始化 Admin Completion Queue (CQ0)
    PVNVME_COMPLETION_QUEUE adminCq = &Controller->AdminCQ;
    RtlZeroMemory(adminCq, sizeof(*adminCq));
    
    adminCq->QueueId = 0;
    adminCq->Size = acqSize;
    adminCq->BaseAddr = Controller->Registers.ACQ;
    adminCq->Head = 0;
    adminCq->Tail = 0;
    adminCq->Phase = 1;
    adminCq->InterruptEnabled = TRUE;
    adminCq->Vector = 0;
    KeInitializeSpinLock(&adminCq->Lock);
    InitializeListHead(&adminCq->SqList);
    
    // 映射 Admin CQ 内存
    adminCq->VirtAddr = VnvmeMapPhysicalMemory(
        adminCq->BaseAddr,
        adminCq->Size * sizeof(NVME_COMPLETION));
    
    if (!adminCq->VirtAddr) {
        VnvmeUnmapPhysicalMemory(adminSq->VirtAddr);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 关联 SQ 和 CQ
    adminSq->CQ = adminCq;
    InsertTailList(&adminCq->SqList, &adminSq->ListEntry);
    
    return STATUS_SUCCESS;
}
```

## 命令提取

### 从 SQ 提取命令

```c
//
// 从 Submission Queue 获取下一条命令
//
BOOLEAN VnvmeFetchCommand(
    _In_ PVNVME_SUBMISSION_QUEUE SQ,
    _Out_ PNVME_COMMAND Command)
{
    KIRQL oldIrql;
    BOOLEAN hasCommand = FALSE;
    
    KeAcquireSpinLock(&SQ->Lock, &oldIrql);
    
    // 检查是否有待处理的命令
    if (SQ->Head != SQ->Tail) {
        // 从队列头部获取命令
        PNVME_COMMAND entry = (PNVME_COMMAND)SQ->VirtAddr + SQ->Head;
        RtlCopyMemory(Command, entry, sizeof(NVME_COMMAND));
        
        // 更新 Head 指针 (回绕处理)
        SQ->Head = (SQ->Head + 1) % SQ->Size;
        
        hasCommand = TRUE;
        SQ->CommandsProcessed++;
    }
    
    KeReleaseSpinLock(&SQ->Lock, oldIrql);
    
    return hasCommand;
}

//
// 检查 SQ 是否有待处理命令
//
BOOLEAN VnvmeSqHasCommands(
    _In_ PVNVME_SUBMISSION_QUEUE SQ)
{
    return SQ->Head != SQ->Tail;
}

//
// 获取 SQ 中待处理命令数量
//
USHORT VnvmeSqPendingCount(
    _In_ PVNVME_SUBMISSION_QUEUE SQ)
{
    if (SQ->Tail >= SQ->Head) {
        return SQ->Tail - SQ->Head;
    } else {
        return SQ->Size - SQ->Head + SQ->Tail;
    }
}
```

## 完成发布

### 向 CQ 发布完成条目

```c
//
// 发布命令完成
//
NTSTATUS VnvmePostCompletion(
    _In_ PVNVME_COMPLETION_QUEUE CQ,
    _In_ PVNVME_SUBMISSION_QUEUE SQ,
    _In_ USHORT CommandId,
    _In_ USHORT Status,
    _In_ ULONG DW0)
{
    KIRQL oldIrql;
    PNVME_COMPLETION entry;
    
    KeAcquireSpinLock(&CQ->Lock, &oldIrql);
    
    // 检查 CQ 是否已满
    USHORT nextTail = (CQ->Tail + 1) % CQ->Size;
    if (nextTail == CQ->Head) {
        KeReleaseSpinLock(&CQ->Lock, oldIrql);
        return STATUS_BUFFER_OVERFLOW;
    }
    
    // 获取 CQ 条目位置
    entry = (PNVME_COMPLETION)CQ->VirtAddr + CQ->Tail;
    
    // 填充完成条目
    entry->DW0 = DW0;
    entry->DW1 = 0;
    entry->SQHD = SQ->Head;     // 当前 SQ Head
    entry->SQID = SQ->QueueId;
    entry->CID = CommandId;
    
    // 设置 Status，包含 Phase bit
    entry->Status = Status | (CQ->Phase ? 1 : 0);
    
    // 写入内存屏障
    KeMemoryBarrier();
    
    // 更新 Tail 指针
    CQ->Tail = nextTail;
    
    // 检查是否需要翻转 Phase
    if (CQ->Tail == 0) {
        CQ->Phase = !CQ->Phase;
    }
    
    CQ->CompletionsPosted++;
    
    KeReleaseSpinLock(&CQ->Lock, oldIrql);
    
    return STATUS_SUCCESS;
}
```

### 写入完成到物理内存

由于 CQ 位于 stornvme.sys 分配的物理内存中，我们需要直接写入：

```c
//
// 将完成条目写入物理内存
//
NTSTATUS VnvmeWriteCompletionToMemory(
    _In_ PVNVME_COMPLETION_QUEUE CQ,
    _In_ USHORT Index,
    _In_ PNVME_COMPLETION Completion)
{
    PHYSICAL_ADDRESS physAddr;
    PVOID mapped;
    
    // 计算物理地址
    physAddr.QuadPart = CQ->BaseAddr + (Index * sizeof(NVME_COMPLETION));
    
    // 映射物理内存
    mapped = MmMapIoSpace(physAddr, sizeof(NVME_COMPLETION), MmNonCached);
    if (!mapped) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 写入完成条目
    RtlCopyMemory(mapped, Completion, sizeof(NVME_COMPLETION));
    
    // 内存屏障确保写入可见
    KeMemoryBarrier();
    
    MmUnmapIoSpace(mapped, sizeof(NVME_COMPLETION));
    
    return STATUS_SUCCESS;
}
```

## 队列处理工作线程

### 主处理循环

```c
typedef struct _VNVME_QUEUE_WORKER {
    PVNVME_CONTROLLER Controller;
    PKTHREAD Thread;
    KEVENT StopEvent;
    KEVENT WakeEvent;
    BOOLEAN Running;
} VNVME_QUEUE_WORKER, *PVNVME_QUEUE_WORKER;

//
// 队列工作线程入口
//
VOID VnvmeQueueWorkerThread(
    _In_ PVOID Context)
{
    PVNVME_QUEUE_WORKER worker = (PVNVME_QUEUE_WORKER)Context;
    PVNVME_CONTROLLER controller = worker->Controller;
    PVOID waitObjects[2];
    NTSTATUS waitStatus;
    
    waitObjects[0] = &worker->StopEvent;
    waitObjects[1] = &worker->WakeEvent;
    
    while (worker->Running) {
        // 等待事件 (带超时用于周期性检查)
        LARGE_INTEGER timeout;
        timeout.QuadPart = -10000 * 10;  // 10ms
        
        waitStatus = KeWaitForMultipleObjects(
            2,
            waitObjects,
            WaitAny,
            Executive,
            KernelMode,
            FALSE,
            &timeout,
            NULL);
        
        if (waitStatus == STATUS_WAIT_0) {
            // 收到停止信号
            break;
        }
        
        // 处理所有队列
        VnvmeProcessAllQueues(controller);
    }
    
    PsTerminateSystemThread(STATUS_SUCCESS);
}

//
// 处理所有队列的命令
//
VOID VnvmeProcessAllQueues(
    _In_ PVNVME_CONTROLLER Controller)
{
    NVME_COMMAND command;
    NVME_COMPLETION completion;
    NTSTATUS status;
    
    // 首先处理 Admin Queue
    while (VnvmeFetchCommand(&Controller->AdminSQ, &command)) {
        RtlZeroMemory(&completion, sizeof(completion));
        
        status = VnvmeProcessAdminCommand(Controller, &command, &completion);
        
        // 发布完成
        VnvmePostCompletion(&Controller->AdminCQ,
                           &Controller->AdminSQ,
                           command.CDW0.CID,
                           completion.Status,
                           completion.DW0);
        
        // 触发中断
        if (Controller->AdminCQ.InterruptEnabled) {
            VnvmeInjectMsiX(Controller, Controller->AdminCQ.Vector);
        }
    }
    
    // 然后按优先级处理 I/O Queues
    VnvmeProcessIoQueues(Controller);
}

//
// 处理 I/O 队列
//
VOID VnvmeProcessIoQueues(
    _In_ PVNVME_CONTROLLER Controller)
{
    PLIST_ENTRY entry;
    PVNVME_SUBMISSION_QUEUE sq;
    NVME_COMMAND command;
    NVME_COMPLETION completion;
    
    // 遍历所有 I/O SQ
    // 注意：实际实现应按优先级排序
    KIRQL oldIrql;
    KeAcquireSpinLock(&Controller->QueueLock, &oldIrql);
    
    for (entry = Controller->IoSqList.Flink;
         entry != &Controller->IoSqList;
         entry = entry->Flink) {
        
        sq = CONTAINING_RECORD(entry, VNVME_SUBMISSION_QUEUE, ListEntry);
        
        // 处理该 SQ 的所有命令 (或限制数量以保证公平)
        ULONG maxCommands = 64;  // 每次最多处理 64 条
        
        while (maxCommands-- > 0 && VnvmeFetchCommand(sq, &command)) {
            RtlZeroMemory(&completion, sizeof(completion));
            
            // 处理 I/O 命令
            VnvmeProcessIoCommand(Controller, sq, &command, &completion);
            
            // 发布完成
            VnvmePostCompletion(sq->CQ, sq,
                               command.CDW0.CID,
                               completion.Status,
                               completion.DW0);
        }
    }
    
    KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
    
    // 触发中断 (合并多个完成)
    VnvmeProcessInterrupts(Controller);
}
```

## 中断合并

### 中断合并策略

```c
typedef struct _VNVME_INTERRUPT_COALESCING {
    ULONG Threshold;         // 完成条目数阈值
    ULONG TimeUs;           // 时间阈值 (微秒)
    ULONG64 LastInterrupt;  // 上次中断时间
    ULONG PendingCount;     // 待发送中断的完成数
} VNVME_INTERRUPT_COALESCING, *PVNVME_INTERRUPT_COALESCING;

//
// 检查是否应该发送中断
//
BOOLEAN VnvmeShouldSendInterrupt(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PVNVME_COMPLETION_QUEUE CQ)
{
    PVNVME_INTERRUPT_COALESCING coalesce = &Controller->IntCoalescing;
    ULONG64 currentTime = KeQueryPerformanceCounter(NULL).QuadPart;
    
    // 累计完成数
    coalesce->PendingCount++;
    
    // 检查阈值
    if (coalesce->PendingCount >= coalesce->Threshold) {
        coalesce->PendingCount = 0;
        coalesce->LastInterrupt = currentTime;
        return TRUE;
    }
    
    // 检查时间
    ULONG64 elapsed = currentTime - coalesce->LastInterrupt;
    // 转换为微秒进行比较
    if (elapsed >= (coalesce->TimeUs * Controller->PerformanceFrequency / 1000000)) {
        coalesce->PendingCount = 0;
        coalesce->LastInterrupt = currentTime;
        return TRUE;
    }
    
    return FALSE;
}

//
// 处理中断发送
//
VOID VnvmeProcessInterrupts(
    _In_ PVNVME_CONTROLLER Controller)
{
    PLIST_ENTRY entry;
    PVNVME_COMPLETION_QUEUE cq;
    
    // 遍历所有 CQ
    for (entry = Controller->IoCqList.Flink;
         entry != &Controller->IoCqList;
         entry = entry->Flink) {
        
        cq = CONTAINING_RECORD(entry, VNVME_COMPLETION_QUEUE, ListEntry);
        
        // 检查是否有新完成且需要中断
        if (cq->InterruptEnabled && VnvmeCqHasNewCompletions(cq)) {
            if (VnvmeShouldSendInterrupt(Controller, cq)) {
                VnvmeInjectMsiX(Controller, cq->Vector);
            }
        }
    }
}
```

## 队列删除

### Delete I/O Completion Queue

```c
NTSTATUS VnvmeProcessDeleteIoCq(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    USHORT qid = Cmd->CDW10 & 0xFFFF;
    PVNVME_COMPLETION_QUEUE cq;
    PLIST_ENTRY entry;
    KIRQL oldIrql;
    
    // QID 0 是 Admin Queue，不能删除
    if (qid == 0) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 查找 CQ
    cq = VnvmeFindIoCQ(Controller, qid);
    if (!cq) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_NOT_FOUND;
    }
    
    // 检查是否还有关联的 SQ
    if (!IsListEmpty(&cq->SqList)) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_DELETION, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 从列表移除
    KeAcquireSpinLock(&Controller->QueueLock, &oldIrql);
    RemoveEntryList(&cq->ListEntry);
    KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
    
    // 解除内存映射
    if (cq->VirtAddr) {
        VnvmeUnmapPhysicalMemory(cq->VirtAddr);
    }
    
    // 释放结构
    ExFreePoolWithTag(cq, 'QCNV');
    
    VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    return STATUS_SUCCESS;
}
```

### Delete I/O Submission Queue

```c
NTSTATUS VnvmeProcessDeleteIoSq(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    USHORT qid = Cmd->CDW10 & 0xFFFF;
    PVNVME_SUBMISSION_QUEUE sq;
    KIRQL oldIrql;
    
    if (qid == 0) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    sq = VnvmeFindIoSQ(Controller, qid);
    if (!sq) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_NOT_FOUND;
    }
    
    // 等待队列中的命令处理完成
    // (简化实现：直接丢弃)
    
    // 从 CQ 的 SQ 列表移除
    KeAcquireSpinLock(&sq->CQ->Lock, &oldIrql);
    RemoveEntryList(&sq->ListEntry);
    KeReleaseSpinLock(&sq->CQ->Lock, oldIrql);
    
    // 从全局列表移除
    KeAcquireSpinLock(&Controller->QueueLock, &oldIrql);
    // 已经通过 CQ 的列表移除了
    KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
    
    // 解除内存映射
    if (sq->VirtAddr) {
        VnvmeUnmapPhysicalMemory(sq->VirtAddr);
    }
    
    ExFreePoolWithTag(sq, 'QSNV');
    
    VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    return STATUS_SUCCESS;
}
```

## 队列查找

```c
//
// 查找 I/O Completion Queue
//
PVNVME_COMPLETION_QUEUE VnvmeFindIoCQ(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ USHORT QueueId)
{
    PLIST_ENTRY entry;
    PVNVME_COMPLETION_QUEUE cq;
    KIRQL oldIrql;
    
    if (QueueId == 0) {
        return &Controller->AdminCQ;
    }
    
    KeAcquireSpinLock(&Controller->QueueLock, &oldIrql);
    
    for (entry = Controller->IoCqList.Flink;
         entry != &Controller->IoCqList;
         entry = entry->Flink) {
        
        cq = CONTAINING_RECORD(entry, VNVME_COMPLETION_QUEUE, ListEntry);
        if (cq->QueueId == QueueId) {
            KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
            return cq;
        }
    }
    
    KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
    return NULL;
}

//
// 查找 I/O Submission Queue
//
PVNVME_SUBMISSION_QUEUE VnvmeFindIoSQ(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ USHORT QueueId)
{
    PLIST_ENTRY entry;
    PVNVME_SUBMISSION_QUEUE sq;
    KIRQL oldIrql;
    
    if (QueueId == 0) {
        return &Controller->AdminSQ;
    }
    
    KeAcquireSpinLock(&Controller->QueueLock, &oldIrql);
    
    for (entry = Controller->IoSqList.Flink;
         entry != &Controller->IoSqList;
         entry = entry->Flink) {
        
        sq = CONTAINING_RECORD(entry, VNVME_SUBMISSION_QUEUE, ListEntry);
        if (sq->QueueId == QueueId) {
            KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
            return sq;
        }
    }
    
    KeReleaseSpinLock(&Controller->QueueLock, oldIrql);
    return NULL;
}
```

## Phase Tag 机制

### Phase 翻转说明

NVMe 使用 Phase Tag 来区分新旧完成条目：

```
┌────────────────────────────────────────────────────────────────────┐
│                      Phase Tag 示例                                  │
├────────────────────────────────────────────────────────────────────┤
│                                                                     │
│     CQ Size = 4, 初始 Phase = 1                                     │
│                                                                     │
│     第一轮 (Phase = 1):                                              │
│     ┌─────┬─────┬─────┬─────┐                                       │
│     │ P=1 │ P=1 │ P=1 │ P=1 │   写入位置 0,1,2,3                      │
│     └─────┴─────┴─────┴─────┘                                       │
│       [0]   [1]   [2]   [3]                                         │
│                                                                     │
│     第二轮 (Phase = 0，回绕后翻转):                                    │
│     ┌─────┬─────┬─────┬─────┐                                       │
│     │ P=0 │ P=0 │ P=1 │ P=1 │   写入位置 0,1 覆盖旧条目               │
│     └─────┴─────┴─────┴─────┘                                       │
│       [0]   [1]   [2]   [3]                                         │
│       ↑                                                             │
│       主机检查 Phase 变化来发现新条目                                  │
│                                                                     │
└────────────────────────────────────────────────────────────────────┘
```

### Phase 处理代码

```c
//
// 发布完成时处理 Phase
//
VOID VnvmeUpdateCqTail(
    _In_ PVNVME_COMPLETION_QUEUE CQ)
{
    // 更新 Tail
    CQ->Tail = (CQ->Tail + 1) % CQ->Size;
    
    // 如果回绕到队列开头，翻转 Phase
    if (CQ->Tail == 0) {
        CQ->Phase = !CQ->Phase;
    }
}

//
// 构造完成状态字 (包含 Phase)
//
USHORT VnvmeBuildStatus(
    _In_ BOOLEAN Phase,
    _In_ UCHAR StatusCodeType,
    _In_ UCHAR StatusCode,
    _In_ BOOLEAN DoNotRetry)
{
    USHORT status = 0;
    
    // Bit 0: Phase Tag
    if (Phase) {
        status |= 0x0001;
    }
    
    // Bits 1-8: Status Code
    status |= ((USHORT)StatusCode << 1);
    
    // Bits 9-11: Status Code Type
    status |= ((USHORT)StatusCodeType << 9);
    
    // Bit 15: Do Not Retry
    if (DoNotRetry) {
        status |= 0x8000;
    }
    
    return status;
}
```

## 性能优化

### 批量命令处理

```c
//
// 批量获取命令以减少锁开销
//
ULONG VnvmeFetchCommandBatch(
    _In_ PVNVME_SUBMISSION_QUEUE SQ,
    _Out_writes_(MaxCount) PNVME_COMMAND Commands,
    _In_ ULONG MaxCount)
{
    KIRQL oldIrql;
    ULONG fetched = 0;
    
    KeAcquireSpinLock(&SQ->Lock, &oldIrql);
    
    while (fetched < MaxCount && SQ->Head != SQ->Tail) {
        PNVME_COMMAND entry = (PNVME_COMMAND)SQ->VirtAddr + SQ->Head;
        RtlCopyMemory(&Commands[fetched], entry, sizeof(NVME_COMMAND));
        
        SQ->Head = (SQ->Head + 1) % SQ->Size;
        fetched++;
    }
    
    SQ->CommandsProcessed += fetched;
    
    KeReleaseSpinLock(&SQ->Lock, oldIrql);
    
    return fetched;
}

//
// 批量发布完成
//
NTSTATUS VnvmePostCompletionBatch(
    _In_ PVNVME_COMPLETION_QUEUE CQ,
    _In_ PVNVME_SUBMISSION_QUEUE SQ,
    _In_reads_(Count) PNVME_COMPLETION Completions,
    _In_ ULONG Count)
{
    KIRQL oldIrql;
    
    KeAcquireSpinLock(&CQ->Lock, &oldIrql);
    
    for (ULONG i = 0; i < Count; i++) {
        USHORT nextTail = (CQ->Tail + 1) % CQ->Size;
        if (nextTail == CQ->Head) {
            KeReleaseSpinLock(&CQ->Lock, oldIrql);
            return STATUS_BUFFER_OVERFLOW;
        }
        
        PNVME_COMPLETION entry = (PNVME_COMPLETION)CQ->VirtAddr + CQ->Tail;
        RtlCopyMemory(entry, &Completions[i], sizeof(NVME_COMPLETION));
        entry->SQHD = SQ->Head;
        entry->SQID = SQ->QueueId;
        entry->Status |= (CQ->Phase ? 1 : 0);
        
        CQ->Tail = nextTail;
        if (CQ->Tail == 0) {
            CQ->Phase = !CQ->Phase;
        }
    }
    
    KeMemoryBarrier();
    CQ->CompletionsPosted += Count;
    
    KeReleaseSpinLock(&CQ->Lock, oldIrql);
    
    return STATUS_SUCCESS;
}
```
