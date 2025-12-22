# 核心机制详解

本文档详细说明混合架构中的核心机制实现。

---

## 1. Doorbell 轮询引擎

### 为什么需要轮询？

在 Windows 下，我们无法直接拦截 stornvme.sys 对 BAR0 内存的写入。因此采用轮询方式检测 Doorbell 寄存器的变化。

### 时序要求

| 指标 | 要求 | 说明 |
|------|------|------|
| **轮询延迟** | < 100μs | 对性能敏感的 I/O |
| **最小间隔** | 10μs | 避免 CPU 过载 |
| **最大间隔** | 1000μs | 空闲时节能 |
| **自适应** | 是 | 根据负载调整 |

### 实现细节

```c
// ============ 定时器配置 ============

// 使用 WDF High Resolution Timer
NTSTATUS VnvmeCreatePollTimer(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    WDF_TIMER_CONFIG timerConfig;
    WDF_OBJECT_ATTRIBUTES timerAttr;
    
    WDF_TIMER_CONFIG_INIT(&timerConfig, VnvmePollTimerCallback);
    timerConfig.AutomaticSerialization = FALSE;
    timerConfig.UseHighResolutionTimer = TRUE;  // 高精度！
    
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttr);
    timerAttr.ParentObject = Ctx->Device;
    
    return WdfTimerCreate(&timerConfig, &timerAttr, &Ctx->PollTimer);
}

// ============ 轮询核心 ============

typedef struct _VNVME_POLL_STATE {
    // Doorbell 缓存值
    USHORT AdminSQTail;
    USHORT AdminCQHead;
    USHORT IoSQTails[VNVME_MAX_IO_QUEUES];
    USHORT IoCQHeads[VNVME_MAX_IO_QUEUES];
    
    // 自适应参数
    ULONG CurrentIntervalUs;
    ULONG ConsecutiveIdleCycles;
    ULONG ConsecutiveBusyCycles;
    
} VNVME_POLL_STATE, *PVNVME_POLL_STATE;

VOID VnvmePollTimerCallback(WDFTIMER Timer)
{
    PVNVME_CONTROLLER_CONTEXT ctx = GetControllerContext(Timer);
    PVNVME_POLL_STATE poll = &ctx->PollState;
    BOOLEAN hadWork = FALSE;
    PUCHAR bar0 = (PUCHAR)ctx->Bar0VirtAddr;
    
    // ========== 阶段 1: 检测控制器状态变化 ==========
    
    if (ctx->State == VNVME_STATE_DISABLED) {
        // 检测 CC.EN 是否被设置
        ULONG cc = *(volatile ULONG*)(bar0 + NVME_CC_OFFSET);
        if (cc & NVME_CC_EN_MASK) {
            VnvmeHandleControllerEnable(ctx);
            hadWork = TRUE;
        }
    }
    
    // ========== 阶段 2: 轮询 Doorbell 寄存器 ==========
    
    if (ctx->State == VNVME_STATE_READY) {
        PUCHAR doorbellBase = bar0 + VNVME_BAR0_DOORBELL_OFFSET;
        
        // Admin SQ Tail (偏移 0x1000)
        USHORT newAdminSQTail = *(volatile USHORT*)(doorbellBase + 0);
        if (newAdminSQTail != poll->AdminSQTail) {
            VnvmeProcessSQCommands(ctx, 0, poll->AdminSQTail, newAdminSQTail);
            poll->AdminSQTail = newAdminSQTail;
            hadWork = TRUE;
        }
        
        // Admin CQ Head (偏移 0x1004)
        USHORT newAdminCQHead = *(volatile USHORT*)(doorbellBase + 4);
        if (newAdminCQHead != poll->AdminCQHead) {
            VnvmeUpdateCQHead(ctx, 0, newAdminCQHead);
            poll->AdminCQHead = newAdminCQHead;
        }
        
        // I/O Queues (每个 SQ/CQ 对占用 8 字节)
        for (USHORT qid = 1; qid <= ctx->ActiveIoQueueCount; qid++) {
            ULONG offset = qid * 8;  // Doorbell Stride = 4, 每对 8 字节
            
            USHORT newSQTail = *(volatile USHORT*)(doorbellBase + offset);
            if (newSQTail != poll->IoSQTails[qid]) {
                VnvmeProcessSQCommands(ctx, qid, poll->IoSQTails[qid], newSQTail);
                poll->IoSQTails[qid] = newSQTail;
                hadWork = TRUE;
            }
            
            USHORT newCQHead = *(volatile USHORT*)(doorbellBase + offset + 4);
            if (newCQHead != poll->IoCQHeads[qid]) {
                VnvmeUpdateCQHead(ctx, qid, newCQHead);
                poll->IoCQHeads[qid] = newCQHead;
            }
        }
    }
    
    // ========== 阶段 3: 自适应轮询间隔 ==========
    
    if (hadWork) {
        poll->ConsecutiveBusyCycles++;
        poll->ConsecutiveIdleCycles = 0;
        
        // 繁忙时加快
        if (poll->ConsecutiveBusyCycles >= 3) {
            poll->CurrentIntervalUs = max(
                poll->CurrentIntervalUs / 2,
                VNVME_MIN_POLL_INTERVAL_US);
        }
    } else {
        poll->ConsecutiveIdleCycles++;
        poll->ConsecutiveBusyCycles = 0;
        
        // 空闲时减慢
        if (poll->ConsecutiveIdleCycles >= 10) {
            poll->CurrentIntervalUs = min(
                poll->CurrentIntervalUs * 2,
                VNVME_MAX_POLL_INTERVAL_US);
        }
    }
    
    // 重新调度
    WdfTimerStart(ctx->PollTimer, 
                  WDF_REL_TIMEOUT_IN_US(poll->CurrentIntervalUs));
}
```

### 性能优化技巧

```c
// 1. 批量读取 Doorbell
// 利用 CPU 缓存行，一次读取多个 Doorbell
VOID VnvmeBatchReadDoorbells(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    // 读取前 64 字节 (覆盖 Admin + 7 个 I/O 队列)
    // 使用 SIMD 指令一次性读取
    __m256i doorbells1 = _mm256_load_si256(
        (__m256i*)(Ctx->Bar0VirtAddr + VNVME_BAR0_DOORBELL_OFFSET));
    __m256i doorbells2 = _mm256_load_si256(
        (__m256i*)(Ctx->Bar0VirtAddr + VNVME_BAR0_DOORBELL_OFFSET + 32));
    
    // 存储到本地缓存
    _mm256_store_si256((__m256i*)&Ctx->DoorbellCache[0], doorbells1);
    _mm256_store_si256((__m256i*)&Ctx->DoorbellCache[8], doorbells2);
}

// 2. 使用 DPC 级别处理
// 轮询在 DISPATCH_LEVEL 执行，减少上下文切换
```

---

## 2. 共享内存机制

### 内存分配

```c
// 在内核态分配，映射到用户态
NTSTATUS VnvmeAllocateSharedMemory(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    SIZE_T Size)
{
    NTSTATUS status;
    
    // 1. 分配非分页内存
    Ctx->SharedMemoryKernel = ExAllocatePool2(
        POOL_FLAG_NON_PAGED | POOL_FLAG_CACHE_ALIGNED,
        Size,
        'MSHV');
    
    if (!Ctx->SharedMemoryKernel) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(Ctx->SharedMemoryKernel, Size);
    Ctx->SharedMemorySize = Size;
    
    // 2. 创建 MDL
    Ctx->SharedMemoryMdl = IoAllocateMdl(
        Ctx->SharedMemoryKernel,
        (ULONG)Size,
        FALSE, FALSE, NULL);
    
    if (!Ctx->SharedMemoryMdl) {
        ExFreePoolWithTag(Ctx->SharedMemoryKernel, 'MSHV');
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 3. 构建 MDL (锁定页面)
    MmBuildMdlForNonPagedPool(Ctx->SharedMemoryMdl);
    
    // 4. 初始化控制块
    PVNVME_SHARED_CONTROL ctrl = (PVNVME_SHARED_CONTROL)Ctx->SharedMemoryKernel;
    ctrl->Magic = VNVME_SHARED_MAGIC;
    ctrl->Version = VNVME_SHARED_VERSION;
    ctrl->State = VNVME_SHARED_STATE_INITIALIZING;
    
    // 设置各区域偏移
    ctrl->CommandRingOffset = VNVME_COMMAND_RING_OFFSET;
    ctrl->CommandRingSize = VNVME_COMMAND_RING_SIZE;
    ctrl->CompletionRingOffset = VNVME_COMPLETION_RING_OFFSET;
    ctrl->CompletionRingSize = VNVME_COMPLETION_RING_SIZE;
    ctrl->DataBufferOffset = VNVME_DATA_BUFFER_OFFSET;
    ctrl->DataBufferSize = VNVME_DATA_BUFFER_SIZE;
    ctrl->DataBufferBlockSize = VNVME_DATA_BUFFER_BLOCK_SIZE;
    
    return STATUS_SUCCESS;
}

// 映射到用户态
NTSTATUS VnvmeMapSharedMemoryToUser(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    PEPROCESS Process,
    PVOID* UserAddress)
{
    __try {
        *UserAddress = MmMapLockedPagesSpecifyCache(
            Ctx->SharedMemoryMdl,
            UserMode,
            MmCached,
            NULL,
            FALSE,
            NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return GetExceptionCode();
    }
    
    return STATUS_SUCCESS;
}
```

### 环形缓冲区

```c
// 无锁环形缓冲区实现
typedef struct _VNVME_RING_BUFFER {
    volatile ULONG Head;        // 消费者读取位置
    volatile ULONG Tail;        // 生产者写入位置
    ULONG Size;                 // 缓冲区大小 (必须是 2 的幂)
    ULONG Mask;                 // Size - 1
    ULONG EntrySize;            // 每个条目大小
    // 数据紧随其后
} VNVME_RING_BUFFER, *PVNVME_RING_BUFFER;

// 检查是否有空间 (生产者调用)
FORCEINLINE BOOLEAN VnvmeRingHasSpace(PVNVME_RING_BUFFER Ring)
{
    ULONG head = Ring->Head;    // 读取一次
    ULONG tail = Ring->Tail;
    ULONG nextTail = (tail + 1) & Ring->Mask;
    return nextTail != head;
}

// 检查是否有数据 (消费者调用)
FORCEINLINE BOOLEAN VnvmeRingHasData(PVNVME_RING_BUFFER Ring)
{
    return Ring->Head != Ring->Tail;
}

// 生产者提交条目
FORCEINLINE PVOID VnvmeRingProducerGet(PVNVME_RING_BUFFER Ring)
{
    if (!VnvmeRingHasSpace(Ring)) {
        return NULL;
    }
    
    PUCHAR data = (PUCHAR)(Ring + 1);  // 数据紧随头部
    return data + (Ring->Tail * Ring->EntrySize);
}

FORCEINLINE VOID VnvmeRingProducerCommit(PVNVME_RING_BUFFER Ring)
{
    // 写屏障确保数据可见
    MemoryBarrier();
    Ring->Tail = (Ring->Tail + 1) & Ring->Mask;
}

// 消费者获取条目
FORCEINLINE PVOID VnvmeRingConsumerGet(PVNVME_RING_BUFFER Ring)
{
    if (!VnvmeRingHasData(Ring)) {
        return NULL;
    }
    
    PUCHAR data = (PUCHAR)(Ring + 1);
    return data + (Ring->Head * Ring->EntrySize);
}

FORCEINLINE VOID VnvmeRingConsumerRelease(PVNVME_RING_BUFFER Ring)
{
    MemoryBarrier();
    Ring->Head = (Ring->Head + 1) & Ring->Mask;
}
```

---

## 3. PRP 解析和数据传输

### PRP 解析

```c
// PRP (Physical Region Page) 解析
// NVMe 使用 PRP 描述物理内存位置

typedef struct _VNVME_PRP_PARSER {
    ULONG64 Prp1;
    ULONG64 Prp2;
    ULONG TotalLength;
    ULONG PageSize;             // 通常 4KB
    
    // 解析结果
    ULONG SegmentCount;
    struct {
        ULONG64 PhysAddr;
        ULONG Length;
    } Segments[VNVME_MAX_PRP_SEGMENTS];
    
} VNVME_PRP_PARSER, *PVNVME_PRP_PARSER;

NTSTATUS VnvmeParsePrp(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    PNVME_COMMAND Cmd,
    ULONG DataLength,
    PVNVME_PRP_PARSER Parser)
{
    Parser->Prp1 = Cmd->DPTR.PRP.PRP1;
    Parser->Prp2 = Cmd->DPTR.PRP.PRP2;
    Parser->TotalLength = DataLength;
    Parser->PageSize = 4096;  // 从 CC.MPS 获取
    Parser->SegmentCount = 0;
    
    if (DataLength == 0) {
        return STATUS_SUCCESS;
    }
    
    // ====== 情况 1: 数据在 PRP1 指向的单个页面内 ======
    ULONG prp1Offset = Parser->Prp1 & (Parser->PageSize - 1);
    ULONG prp1Available = Parser->PageSize - prp1Offset;
    
    if (DataLength <= prp1Available) {
        // 全部在 PRP1
        Parser->Segments[0].PhysAddr = Parser->Prp1;
        Parser->Segments[0].Length = DataLength;
        Parser->SegmentCount = 1;
        return STATUS_SUCCESS;
    }
    
    // ====== 情况 2: 数据跨越 PRP1 和 PRP2 ======
    Parser->Segments[0].PhysAddr = Parser->Prp1;
    Parser->Segments[0].Length = prp1Available;
    Parser->SegmentCount = 1;
    
    ULONG remaining = DataLength - prp1Available;
    
    if (remaining <= Parser->PageSize) {
        // PRP2 是第二个页面地址
        Parser->Segments[1].PhysAddr = Parser->Prp2;
        Parser->Segments[1].Length = remaining;
        Parser->SegmentCount = 2;
        return STATUS_SUCCESS;
    }
    
    // ====== 情况 3: PRP2 是 PRP List 地址 ======
    // PRP List 包含后续页面的地址列表
    PHYSICAL_ADDRESS prpListPhys;
    prpListPhys.QuadPart = Parser->Prp2;
    
    PULONG64 prpList = (PULONG64)MmMapIoSpace(
        prpListPhys, Parser->PageSize, MmNonCached);
    
    if (!prpList) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    ULONG idx = 0;
    while (remaining > 0 && Parser->SegmentCount < VNVME_MAX_PRP_SEGMENTS) {
        ULONG64 prpAddr = prpList[idx++];
        
        // 检查是否是 PRP List 链接 (指向下一个 PRP List)
        if (remaining > Parser->PageSize && 
            (idx * sizeof(ULONG64)) >= Parser->PageSize) {
            // 最后一个条目指向下一个 PRP List
            MmUnmapIoSpace(prpList, Parser->PageSize);
            prpListPhys.QuadPart = prpAddr;
            prpList = (PULONG64)MmMapIoSpace(
                prpListPhys, Parser->PageSize, MmNonCached);
            if (!prpList) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            idx = 0;
            continue;
        }
        
        ULONG segmentLen = min(remaining, Parser->PageSize);
        Parser->Segments[Parser->SegmentCount].PhysAddr = prpAddr;
        Parser->Segments[Parser->SegmentCount].Length = segmentLen;
        Parser->SegmentCount++;
        remaining -= segmentLen;
    }
    
    MmUnmapIoSpace(prpList, Parser->PageSize);
    
    return STATUS_SUCCESS;
}
```

### 数据复制

```c
// 从 PRP 复制数据到共享缓冲区 (用于 Write 命令)
NTSTATUS VnvmeCopyFromPrp(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    PVNVME_PRP_PARSER Parser,
    PVOID Destination)
{
    PUCHAR dest = (PUCHAR)Destination;
    
    for (ULONG i = 0; i < Parser->SegmentCount; i++) {
        PHYSICAL_ADDRESS physAddr;
        physAddr.QuadPart = Parser->Segments[i].PhysAddr;
        
        PVOID mapped = MmMapIoSpace(
            physAddr,
            Parser->Segments[i].Length,
            MmNonCached);
        
        if (!mapped) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        RtlCopyMemory(dest, mapped, Parser->Segments[i].Length);
        dest += Parser->Segments[i].Length;
        
        MmUnmapIoSpace(mapped, Parser->Segments[i].Length);
    }
    
    return STATUS_SUCCESS;
}

// 从共享缓冲区复制数据到 PRP (用于 Read 命令)
NTSTATUS VnvmeCopyToPrp(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    PVNVME_PRP_PARSER Parser,
    PVOID Source)
{
    PUCHAR src = (PUCHAR)Source;
    
    for (ULONG i = 0; i < Parser->SegmentCount; i++) {
        PHYSICAL_ADDRESS physAddr;
        physAddr.QuadPart = Parser->Segments[i].PhysAddr;
        
        PVOID mapped = MmMapIoSpace(
            physAddr,
            Parser->Segments[i].Length,
            MmNonCached);
        
        if (!mapped) {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        RtlCopyMemory(mapped, src, Parser->Segments[i].Length);
        src += Parser->Segments[i].Length;
        
        MmUnmapIoSpace(mapped, Parser->Segments[i].Length);
    }
    
    return STATUS_SUCCESS;
}
```

---

## 4. 命令处理流程

### 完整流程图

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           命令处理完整流程                                │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  stornvme.sys                                                           │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │ 1. 构建 NVMe 命令                                               │     │
│  │ 2. 写入 SQ[Tail]                                               │     │
│  │ 3. 写入 SQ Tail Doorbell                                       │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                    │                                     │
│                                    ▼                                     │
│  ════════════════════════════════════════════════════════════════════   │
│                                    │                                     │
│  vnvme.sys (内核)                  │                                     │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │ 4. 轮询定时器检测 Doorbell 变化                                 │     │
│  │ 5. 从 SQ 读取命令                                               │     │
│  │ 6. 解析 PRP，复制 Write 数据到共享缓冲区                        │     │
│  │ 7. 将命令放入命令环                                             │     │
│  │ 8. 设置 CommandReadyEvent                                       │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                    │                                     │
│                                    ▼                                     │
│  ════════════════════════════════════════════════════════════════════   │
│                                    │                                     │
│  vnvme-server.exe (用户态)         │                                     │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │ 9. WaitForSingleObject(CommandReadyEvent)                       │     │
│  │ 10. 从命令环读取命令                                            │     │
│  │ 11. 处理命令 (读写后端存储)                                     │     │
│  │ 12. 构建完成条目                                                │     │
│  │ 13. 放入完成环                                                  │     │
│  │ 14. 设置 CompletionReadyEvent                                   │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                    │                                     │
│                                    ▼                                     │
│  ════════════════════════════════════════════════════════════════════   │
│                                    │                                     │
│  vnvme.sys (内核)                  │                                     │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │ 15. 轮询完成环或收到事件                                        │     │
│  │ 16. 从完成环读取完成条目                                        │     │
│  │ 17. 如果是 Read 命令，复制数据到 PRP                            │     │
│  │ 18. 将完成条目写入 CQ[Tail]                                     │     │
│  │ 19. 设置正确的 Phase Tag                                        │     │
│  │ 20. 更新 CQ Tail                                                │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                    │                                     │
│                                    ▼                                     │
│  ════════════════════════════════════════════════════════════════════   │
│                                    │                                     │
│  stornvme.sys                      │                                     │
│  ┌────────────────────────────────────────────────────────────────┐     │
│  │ 21. 轮询 CQ 检测 Phase Tag 变化                                 │     │
│  │ 22. 读取完成条目                                                │     │
│  │ 23. 处理完成 (返回 I/O 结果)                                    │     │
│  │ 24. 写入 CQ Head Doorbell                                       │     │
│  └────────────────────────────────────────────────────────────────┘     │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### 代码实现

```c
// ============ 步骤 5-8: 提取命令并转发 ============

VOID VnvmeProcessSQCommands(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    USHORT QueueId,
    USHORT OldTail,
    USHORT NewTail)
{
    PVNVME_QUEUE sq = VnvmeGetQueue(Ctx, QueueId, TRUE);
    PNVME_COMMAND sqBase = (PNVME_COMMAND)sq->MappedVirtAddr;
    
    // 遍历新命令
    USHORT idx = OldTail;
    while (idx != NewTail) {
        PNVME_COMMAND cmd = &sqBase[idx];
        
        // 获取共享缓冲区槽位
        PVNVME_SHARED_COMMAND sharedCmd = VnvmeAllocateCommandSlot(Ctx);
        if (!sharedCmd) {
            // 缓冲区满，等待下次轮询
            break;
        }
        
        // 填充共享命令
        sharedCmd->QueueId = QueueId;
        sharedCmd->CommandIndex = idx;
        RtlCopyMemory(&sharedCmd->Command, cmd, sizeof(NVME_COMMAND));
        
        // 如果是 Write 命令，复制数据
        if (cmd->CDW0.OPC == NVME_IO_OPC_WRITE) {
            ULONG dataLen = (cmd->CDW12 + 1) * sq->NamespaceBlockSize;
            PVOID dataBuf = VnvmeAllocateDataBuffer(Ctx, dataLen);
            
            if (dataBuf) {
                VNVME_PRP_PARSER parser;
                VnvmeParsePrp(Ctx, cmd, dataLen, &parser);
                VnvmeCopyFromPrp(Ctx, &parser, dataBuf);
                
                sharedCmd->DataBufferOffset = 
                    (ULONG)((PUCHAR)dataBuf - (PUCHAR)Ctx->SharedMemoryKernel);
                sharedCmd->DataBufferLength = dataLen;
            }
        }
        
        // 提交到命令环
        VnvmeCommitCommandSlot(Ctx, sharedCmd);
        
        // 更新 SQ Head
        sq->Head = (idx + 1) % sq->Size;
        
        idx = (idx + 1) % sq->Size;
    }
    
    // 通知用户态
    if (idx != OldTail) {
        KeSetEvent(&Ctx->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
    }
}

// ============ 步骤 15-20: 处理完成 ============

VOID VnvmeProcessCompletions(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    PVNVME_RING_BUFFER completionRing = VnvmeGetCompletionRing(Ctx);
    
    while (VnvmeRingHasData(completionRing)) {
        PVNVME_SHARED_COMPLETION sharedCpl = 
            (PVNVME_SHARED_COMPLETION)VnvmeRingConsumerGet(completionRing);
        
        PVNVME_QUEUE cq = VnvmeGetQueue(Ctx, sharedCpl->CqId, FALSE);
        PNVME_COMPLETION cqBase = (PNVME_COMPLETION)cq->MappedVirtAddr;
        
        // 如果是 Read 命令完成，复制数据到 PRP
        if (sharedCpl->HasData) {
            PVOID dataBuf = (PUCHAR)Ctx->SharedMemoryKernel + 
                            sharedCpl->DataBufferOffset;
            
            // 获取原始命令的 PRP
            PVNVME_QUEUE sq = VnvmeGetSQByCQ(Ctx, sharedCpl->CqId);
            PNVME_COMMAND cmd = VnvmeGetCommand(sq, sharedCpl->CommandIndex);
            
            VNVME_PRP_PARSER parser;
            VnvmeParsePrp(Ctx, cmd, sharedCpl->DataBufferLength, &parser);
            VnvmeCopyToPrp(Ctx, &parser, dataBuf);
            
            VnvmeFreeDataBuffer(Ctx, dataBuf, sharedCpl->DataBufferLength);
        }
        
        // 构建 CQ Entry
        NVME_COMPLETION cqe = {0};
        cqe.DW0 = sharedCpl->DW0;
        cqe.SQHD = VnvmeGetQueue(Ctx, sharedCpl->SqId, TRUE)->Head;
        cqe.SQID = sharedCpl->SqId;
        cqe.CID = sharedCpl->CommandId;
        cqe.Status = sharedCpl->Status;
        
        // 设置 Phase Tag
        if (cq->Phase) {
            cqe.Status |= 1;
        } else {
            cqe.Status &= ~1;
        }
        
        // 写入 CQ
        RtlCopyMemory(&cqBase[cq->Tail], &cqe, sizeof(NVME_COMPLETION));
        
        // 更新 CQ Tail 和 Phase
        cq->Tail++;
        if (cq->Tail >= cq->Size) {
            cq->Tail = 0;
            cq->Phase = !cq->Phase;
        }
        
        VnvmeRingConsumerRelease(completionRing);
    }
}
```

---

## 5. 用户态服务可靠性

### 启动顺序处理

```c
// 控制器状态
typedef enum _VNVME_STATE {
    VNVME_STATE_NOT_PRESENT,     // 用户态未连接
    VNVME_STATE_DISABLED,        // 已连接，控制器禁用
    VNVME_STATE_WAITING_USER,    // CC.EN=1 但用户态未就绪
    VNVME_STATE_ENABLING,        // 正在启用
    VNVME_STATE_READY,           // 正常运行
    VNVME_STATE_ERROR            // 错误状态
} VNVME_STATE;

// CC.EN 被设置时
VOID VnvmeHandleControllerEnable(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    // 读取 AQA, ASQ, ACQ
    PUCHAR bar0 = (PUCHAR)Ctx->Bar0VirtAddr;
    ULONG aqa = *(PULONG)(bar0 + NVME_AQA_OFFSET);
    ULONG64 asq = *(PULONG64)(bar0 + NVME_ASQ_OFFSET);
    ULONG64 acq = *(PULONG64)(bar0 + NVME_ACQ_OFFSET);
    
    // 验证参数
    if (asq == 0 || acq == 0) {
        VnvmeSetControllerError(Ctx);
        return;
    }
    
    // 保存配置
    Ctx->AdminSQBase = asq;
    Ctx->AdminCQBase = acq;
    Ctx->AdminSQSize = (aqa & 0xFFF) + 1;
    Ctx->AdminCQSize = ((aqa >> 16) & 0xFFF) + 1;
    
    // 检查用户态是否就绪
    if (!Ctx->UserServiceReady) {
        Ctx->State = VNVME_STATE_WAITING_USER;
        // CSTS.RDY 保持为 0，stornvme 会继续等待
        return;
    }
    
    // 完成初始化
    VnvmeCompleteControllerEnable(Ctx);
}

// 用户态连接时
NTSTATUS VnvmeHandleUserConnect(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    PIRP Irp)
{
    Ctx->UserServiceReady = TRUE;
    
    // 如果控制器在等待
    if (Ctx->State == VNVME_STATE_WAITING_USER) {
        VnvmeCompleteControllerEnable(Ctx);
    }
    
    return STATUS_SUCCESS;
}

// 完成控制器启用
VOID VnvmeCompleteControllerEnable(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    // 映射 Admin Queue 内存
    NTSTATUS status = VnvmeMapQueues(Ctx);
    if (!NT_SUCCESS(status)) {
        VnvmeSetControllerError(Ctx);
        return;
    }
    
    // 初始化队列状态
    Ctx->AdminSQ.Head = 0;
    Ctx->AdminSQ.Tail = 0;
    Ctx->AdminCQ.Head = 0;
    Ctx->AdminCQ.Tail = 0;
    Ctx->AdminCQ.Phase = 1;
    
    // 设置 CSTS.RDY = 1
    PUCHAR bar0 = (PUCHAR)Ctx->Bar0VirtAddr;
    PULONG csts = (PULONG)(bar0 + NVME_CSTS_OFFSET);
    *csts |= NVME_CSTS_RDY;
    
    Ctx->State = VNVME_STATE_READY;
    
    // 开始轮询
    WdfTimerStart(Ctx->PollTimer, WDF_REL_TIMEOUT_IN_US(100));
}
```

### 用户态崩溃处理

```c
// 心跳检测
VOID VnvmeHeartbeatTimerCallback(WDFTIMER Timer)
{
    PVNVME_CONTROLLER_CONTEXT Ctx = GetControllerContext(Timer);
    
    PVNVME_SHARED_CONTROL ctrl = 
        (PVNVME_SHARED_CONTROL)Ctx->SharedMemoryKernel;
    
    LONG64 userHeartbeat = InterlockedCompareExchange64(
        &ctrl->UserHeartbeat, 0, 0);
    
    if (userHeartbeat == Ctx->LastUserHeartbeat) {
        Ctx->MissedHeartbeats++;
        
        if (Ctx->MissedHeartbeats >= VNVME_MAX_MISSED_HEARTBEATS) {
            // 用户态无响应
            VnvmeHandleUserCrash(Ctx);
        }
    } else {
        Ctx->LastUserHeartbeat = userHeartbeat;
        Ctx->MissedHeartbeats = 0;
    }
}

VOID VnvmeHandleUserCrash(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    // 1. 停止轮询
    WdfTimerStop(Ctx->PollTimer, FALSE);
    
    // 2. 将所有待处理命令返回错误
    VnvmeFailAllPendingCommands(Ctx, 
        NVME_MAKE_STATUS(NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR, 1));
    
    // 3. 设置控制器错误状态
    PUCHAR bar0 = (PUCHAR)Ctx->Bar0VirtAddr;
    PULONG csts = (PULONG)(bar0 + NVME_CSTS_OFFSET);
    *csts |= NVME_CSTS_CFS;  // Controller Fatal Status
    
    Ctx->State = VNVME_STATE_ERROR;
    Ctx->UserServiceReady = FALSE;
    
    // 4. 可选：尝试重启用户态服务
    // VnvmeRestartUserService(Ctx);
}
```

---

## 6. 性能优化策略

### 批量处理

```c
// 批量提交命令
VOID VnvmeProcessSQCommandsBatch(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    USHORT QueueId,
    USHORT OldTail,
    USHORT NewTail)
{
    PVNVME_QUEUE sq = VnvmeGetQueue(Ctx, QueueId, TRUE);
    PNVME_COMMAND sqBase = (PNVME_COMMAND)sq->MappedVirtAddr;
    
    // 计算命令数量
    USHORT count = (NewTail >= OldTail) ? 
                   (NewTail - OldTail) : 
                   (sq->Size - OldTail + NewTail);
    
    // 限制每批处理的命令数
    count = min(count, VNVME_MAX_BATCH_SIZE);
    
    // 批量分配共享缓冲区槽位
    PVNVME_SHARED_COMMAND batch[VNVME_MAX_BATCH_SIZE];
    ULONG allocated = VnvmeAllocateCommandSlotBatch(Ctx, batch, count);
    
    // 批量处理
    for (ULONG i = 0; i < allocated; i++) {
        USHORT idx = (OldTail + i) % sq->Size;
        PNVME_COMMAND cmd = &sqBase[idx];
        
        batch[i]->QueueId = QueueId;
        batch[i]->CommandIndex = idx;
        RtlCopyMemory(&batch[i]->Command, cmd, sizeof(NVME_COMMAND));
        
        // ... 数据复制 ...
    }
    
    // 批量提交
    VnvmeCommitCommandSlotBatch(Ctx, batch, allocated);
    
    // 一次性通知用户态
    KeSetEvent(&Ctx->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
}
```

### 数据缓冲区池

```c
// 使用位图管理数据缓冲区池
typedef struct _VNVME_BUFFER_POOL {
    PUCHAR BaseAddress;
    SIZE_T TotalSize;
    ULONG BlockSize;           // 4KB
    ULONG BlockCount;
    
    KSPIN_LOCK Lock;
    RTL_BITMAP Bitmap;
    PULONG BitmapBuffer;
    
    // 统计
    ULONG AllocatedBlocks;
    ULONG PeakAllocatedBlocks;
    
} VNVME_BUFFER_POOL, *PVNVME_BUFFER_POOL;

PVOID VnvmeAllocateDataBuffer(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    ULONG Size)
{
    PVNVME_BUFFER_POOL pool = &Ctx->BufferPool;
    ULONG blocksNeeded = (Size + pool->BlockSize - 1) / pool->BlockSize;
    
    KIRQL oldIrql;
    KeAcquireSpinLock(&pool->Lock, &oldIrql);
    
    // 查找连续空闲块
    ULONG startBit = RtlFindClearBits(&pool->Bitmap, blocksNeeded, 0);
    if (startBit == (ULONG)-1) {
        KeReleaseSpinLock(&pool->Lock, oldIrql);
        return NULL;
    }
    
    // 标记为已分配
    RtlSetBits(&pool->Bitmap, startBit, blocksNeeded);
    pool->AllocatedBlocks += blocksNeeded;
    pool->PeakAllocatedBlocks = max(pool->PeakAllocatedBlocks, 
                                     pool->AllocatedBlocks);
    
    KeReleaseSpinLock(&pool->Lock, oldIrql);
    
    return pool->BaseAddress + (startBit * pool->BlockSize);
}

VOID VnvmeFreeDataBuffer(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    PVOID Buffer,
    ULONG Size)
{
    PVNVME_BUFFER_POOL pool = &Ctx->BufferPool;
    ULONG startBit = (ULONG)((PUCHAR)Buffer - pool->BaseAddress) / pool->BlockSize;
    ULONG blocksToFree = (Size + pool->BlockSize - 1) / pool->BlockSize;
    
    KIRQL oldIrql;
    KeAcquireSpinLock(&pool->Lock, &oldIrql);
    
    RtlClearBits(&pool->Bitmap, startBit, blocksToFree);
    pool->AllocatedBlocks -= blocksToFree;
    
    KeReleaseSpinLock(&pool->Lock, oldIrql);
}
```

---

## 总结

本混合架构的核心优势：

| 优势 | 说明 |
|------|------|
| **简化内核代码** | 只处理必须的硬件接口 |
| **用户态灵活性** | 复杂逻辑在用户态实现 |
| **安全性** | 用户态崩溃不影响系统 |
| **可调试性** | 用户态使用标准调试工具 |
| **可扩展性** | 后端存储容易添加新类型 |

核心机制包括：
1. **Doorbell 轮询** - 检测 stornvme 命令提交
2. **共享内存** - 高效的内核/用户态通信
3. **PRP 解析** - 处理物理内存访问
4. **Phase Tag** - 正确的 CQ 完成通知
