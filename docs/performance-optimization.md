# 性能优化指南

**版本**: 1.0  
**日期**: 2024-12-23  
**目标读者**: vnvme 开发者和高级用户

---

## 目录

1. [性能目标](#性能目标)
2. [性能瓶颈分析](#性能瓶颈分析)
3. [轮询优化](#轮询优化)
4. [事件通知机制](#事件通知机制)
5. [批处理优化](#批处理优化)
6. [内存访问优化](#内存访问优化)
7. [锁和同步优化](#锁和同步优化)
8. [后端存储优化](#后端存储优化)
9. [性能测试方法](#性能测试方法)
10. [性能调优参数](#性能调优参数)

---

## 性能目标

### 目标指标

| 指标 | 目标值 | 说明 |
|------|--------|------|
| 顺序读吞吐 | > 500 MB/s | 后端为 SSD 时 |
| 顺序写吞吐 | > 400 MB/s | 后端为 SSD 时 |
| 4KB 随机读 IOPS | > 50,000 | 队列深度 32 |
| 4KB 随机写 IOPS | > 30,000 | 队列深度 32 |
| 平均延迟 | < 100 μs | 4KB 随机读 |
| P99 延迟 | < 500 μs | 4KB 随机读 |
| CPU 占用 | < 10% | 空闲时 |

### 性能层次

```
理论最大性能 (后端 I/O 能力)
        ↓ 损耗: 内存复制
用户态服务可达性能
        ↓ 损耗: 内核-用户态通信
共享内存可达性能
        ↓ 损耗: 轮询延迟
轮询检测可达性能
        ↓ 损耗: 寄存器访问
stornvme 可见性能 (实际性能)
```

---

## 性能瓶颈分析

### 1. 轮询延迟 (最大瓶颈)

**问题**: 固定间隔轮询导致延迟

```
stornvme 写 Doorbell ─┬─ 最坏情况: 等待完整轮询间隔
                      │
                      ├─ 平均: 轮询间隔 / 2
                      │
                      └─ 最好情况: 立即被检测
```

**当前实现**: 1ms 固定间隔
- 平均延迟增加: 500 μs
- 最坏延迟增加: 1000 μs

### 2. 内核-用户态通信

**问题**: 每次命令都需要跨越内核-用户态边界

```
内核:   检测命令 → 写入共享内存 → [等待用户态处理]
                                        ↓
用户态:           [轮询共享内存] → 处理命令 → 写入完成 → [等待内核投递]
                                                            ↓
内核:                            [轮询共享内存] → 投递完成到 CQ
```

**损耗来源**:
- 用户态轮询共享内存: ~1-10 μs 每次检查
- 内存屏障: ~100 ns 每次

### 3. 内存复制

**问题**: 数据需要复制多次

```
stornvme 内存 ──(PRP)──▶ 共享内存 ──(读取)──▶ 用户态缓冲区 ──(syscall)──▶ 后端存储
```

**每次 I/O 复制次数**: 2-3 次 (取决于后端)

### 4. 锁竞争

**问题**: 多队列并发访问共享资源

```
Admin 队列处理线程 ──┐
I/O 队列 1 处理线程 ──┼──▶ 共享资源 (锁竞争)
I/O 队列 N 处理线程 ──┘
```

---

## 轮询优化

### 3.1 自适应轮询间隔

根据负载动态调整轮询频率:

```c
/* doorbell.c - 自适应轮询 */

typedef struct _VNVME_ADAPTIVE_POLL {
    ULONG CurrentIntervalUs;    /* 当前轮询间隔 (微秒) */
    ULONG MinIntervalUs;        /* 最小间隔: 10 μs */
    ULONG MaxIntervalUs;        /* 最大间隔: 1000 μs */
    ULONG IdleCount;            /* 连续空闲次数 */
    ULONG BusyCount;            /* 连续繁忙次数 */
    ULONG AdjustThreshold;      /* 调整阈值 */
} VNVME_ADAPTIVE_POLL;

VOID
VnvmeAdjustPollingInterval(
    _Inout_ PVNVME_ADAPTIVE_POLL Poll,
    _In_ BOOLEAN HadWork
    )
{
    if (HadWork) {
        Poll->IdleCount = 0;
        Poll->BusyCount++;
        
        /* 繁忙时降低间隔 (更频繁轮询) */
        if (Poll->BusyCount >= Poll->AdjustThreshold) {
            Poll->CurrentIntervalUs = max(
                Poll->CurrentIntervalUs / 2,
                Poll->MinIntervalUs
            );
            Poll->BusyCount = 0;
        }
    } else {
        Poll->BusyCount = 0;
        Poll->IdleCount++;
        
        /* 空闲时增加间隔 (节省 CPU) */
        if (Poll->IdleCount >= Poll->AdjustThreshold) {
            Poll->CurrentIntervalUs = min(
                Poll->CurrentIntervalUs * 2,
                Poll->MaxIntervalUs
            );
            Poll->IdleCount = 0;
        }
    }
}
```

### 3.2 高精度定时器

使用 `KeQueryPerformanceCounter` 实现微秒级轮询:

```c
/* 高精度等待 */
VOID
VnvmePreciseWait(
    _In_ ULONG Microseconds
    )
{
    LARGE_INTEGER start, current, frequency;
    LONGLONG targetTicks;
    
    KeQueryPerformanceCounter(&frequency);
    targetTicks = (frequency.QuadPart * Microseconds) / 1000000;
    
    start = KeQueryPerformanceCounter(NULL);
    
    do {
        /* 短时间自旋而非睡眠 */
        YieldProcessor();
        current = KeQueryPerformanceCounter(NULL);
    } while ((current.QuadPart - start.QuadPart) < targetTicks);
}
```

### 3.3 轮询参数配置

| 参数 | 默认值 | 范围 | 说明 |
|------|--------|------|------|
| `MinPollingIntervalUs` | 10 | 1-100 | 最小轮询间隔 |
| `MaxPollingIntervalUs` | 1000 | 100-10000 | 最大轮询间隔 |
| `InitialPollingIntervalUs` | 100 | 10-1000 | 初始轮询间隔 |
| `AdaptiveThreshold` | 10 | 1-100 | 自适应调整阈值 |

---

## 事件通知机制

### 4.1 内核事件 vs 轮询

| 方案 | 延迟 | CPU 占用 | 复杂度 |
|------|------|----------|--------|
| 固定轮询 | 高 (avg 500μs) | 高 | 低 |
| 自适应轮询 | 中 (avg 50-500μs) | 中 | 中 |
| 事件通知 | 低 (< 10μs) | 低 | 高 |
| 混合模式 | 最优 | 最优 | 最高 |

### 4.2 事件通知实现

**阶段 1: 内核到用户态通知**

```c
/* FDO 上下文中添加 */
typedef struct _VNVME_FDO_CONTEXT {
    /* ... 现有成员 ... */
    
    /* 事件通知 */
    KEVENT CommandReadyEvent;       /* 内核事件对象 */
    HANDLE UserEventHandle;         /* 用户态可等待句柄 */
    BOOLEAN EventNotificationEnabled;
    
} VNVME_FDO_CONTEXT;

/* 创建用户态可等待事件 */
NTSTATUS
VnvmeCreateUserEventHandle(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _Out_ PHANDLE UserHandle
    )
{
    NTSTATUS status;
    
    /* 初始化内核事件 */
    KeInitializeEvent(
        &FdoContext->CommandReadyEvent,
        SynchronizationEvent,  /* 自动复位 */
        FALSE
    );
    
    /* 创建用户态可等待句柄 */
    status = ObOpenObjectByPointer(
        &FdoContext->CommandReadyEvent,
        OBJ_KERNEL_HANDLE,
        NULL,
        EVENT_ALL_ACCESS,
        *ExEventObjectType,
        UserMode,
        UserHandle
    );
    
    if (NT_SUCCESS(status)) {
        FdoContext->UserEventHandle = *UserHandle;
        FdoContext->EventNotificationEnabled = TRUE;
    }
    
    return status;
}

/* 内核通知用户态有新命令 */
VOID
VnvmeNotifyUserMode(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    if (FdoContext->EventNotificationEnabled) {
        KeSetEvent(&FdoContext->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
    }
}
```

**用户态等待:**

```c
/* vnvme-server 等待命令 */
DWORD
WaitForCommands(
    HANDLE hEvent,
    DWORD timeoutMs
    )
{
    return WaitForSingleObject(hEvent, timeoutMs);
}

/* 主循环 */
void CommandLoop(HANDLE hEvent)
{
    while (g_Running) {
        DWORD result = WaitForCommands(hEvent, 100);  /* 100ms 超时 */
        
        if (result == WAIT_OBJECT_0) {
            /* 有新命令 */
            ProcessAllPendingCommands();
        } else if (result == WAIT_TIMEOUT) {
            /* 超时，检查心跳等 */
            SendHeartbeat();
        }
    }
}
```

### 4.3 混合模式 (推荐)

结合轮询和事件通知:

```c
/* 混合模式: 低负载用事件，高负载用轮询 */

typedef enum _VNVME_NOTIFY_MODE {
    NotifyModePolling,      /* 纯轮询 */
    NotifyModeEvent,        /* 纯事件 */
    NotifyModeHybrid        /* 混合模式 */
} VNVME_NOTIFY_MODE;

VOID
VnvmeHybridNotify(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG PendingCommands
    )
{
    if (FdoContext->NotifyMode != NotifyModeHybrid) {
        return;
    }
    
    if (PendingCommands < HYBRID_THRESHOLD) {
        /* 低负载: 使用事件通知，用户态可以休眠 */
        KeSetEvent(&FdoContext->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
    }
    /* 高负载: 用户态应该在轮询，不需要通知 */
}
```

---

## 批处理优化

### 5.1 命令批处理

一次处理多个命令，减少同步开销:

```c
/* 批量处理常量 */
#define VNVME_BATCH_SIZE_MIN    1
#define VNVME_BATCH_SIZE_MAX    64
#define VNVME_BATCH_SIZE_DEFAULT 16

/* 批量获取命令 */
ULONG
VnvmeFetchCommandBatch(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _Out_writes_(MaxCommands) PNVME_COMMAND Commands,
    _In_ ULONG MaxCommands
    )
{
    ULONG fetched = 0;
    PVNVME_QUEUE_STATE sq = (QueueId == 0) ? 
        &PdoContext->AdminSq : &PdoContext->IoSq[QueueId - 1];
    
    while (fetched < MaxCommands && sq->Head != sq->Tail) {
        /* 复制命令 */
        RtlCopyMemory(
            &Commands[fetched],
            GetSqEntry(PdoContext, QueueId, sq->Head),
            sizeof(NVME_COMMAND)
        );
        
        sq->Head = (sq->Head + 1) % sq->Size;
        fetched++;
    }
    
    return fetched;
}

/* 批量投递完成 */
VOID
VnvmePostCompletionBatch(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_reads_(Count) PNVME_COMPLETION Completions,
    _In_ ULONG Count
    )
{
    PVNVME_QUEUE_STATE cq = (QueueId == 0) ? 
        &PdoContext->AdminCq : &PdoContext->IoCq[QueueId - 1];
    
    for (ULONG i = 0; i < Count; i++) {
        PNVME_COMPLETION entry = GetCqEntry(PdoContext, QueueId, cq->Tail);
        
        RtlCopyMemory(entry, &Completions[i], sizeof(NVME_COMPLETION));
        entry->DW3.P = cq->PhaseTag;  /* 设置 Phase Tag */
        
        cq->Tail = (cq->Tail + 1) % cq->Size;
        if (cq->Tail == 0) {
            cq->PhaseTag = !cq->PhaseTag;  /* 翻转 Phase */
        }
    }
    
    /* 一次性内存屏障 */
    KeMemoryBarrier();
}
```

### 5.2 用户态批处理

```c
/* vnvme-server 批处理 */

#define BATCH_SIZE 16

void ProcessCommandBatch(void)
{
    VNVME_RING_ENTRY entries[BATCH_SIZE];
    VNVME_COMPLETION completions[BATCH_SIZE];
    UINT32 count = 0;
    
    /* 批量获取 */
    while (count < BATCH_SIZE) {
        if (!TryDequeueSubmission(&entries[count])) {
            break;
        }
        count++;
    }
    
    if (count == 0) return;
    
    /* 批量处理 */
    for (UINT32 i = 0; i < count; i++) {
        ProcessSingleCommand(&entries[i], &completions[i]);
    }
    
    /* 批量提交完成 */
    EnqueueCompletionBatch(completions, count);
}
```

---

## 内存访问优化

### 6.1 缓存行对齐

```c
/* 确保关键结构缓存行对齐 */
#define CACHE_LINE_SIZE 64

typedef struct _VNVME_QUEUE_STATE {
    PHYSICAL_ADDRESS BaseAddress;
    ULONG Size;
    
    /* 分离读写频繁的成员到不同缓存行 */
    DECLSPEC_CACHEALIGN volatile ULONG Head;  /* 生产者更新 */
    DECLSPEC_CACHEALIGN volatile ULONG Tail;  /* 消费者更新 */
    
    volatile BOOLEAN PhaseTag;
    BOOLEAN Created;
} VNVME_QUEUE_STATE;
```

### 6.2 预取优化

```c
/* 预取下一个命令 */
VOID
VnvmePrefetchNextCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ ULONG Index
    )
{
    ULONG nextIndex = (Index + 1) % PdoContext->AdminSq.Size;
    PVOID nextEntry = GetSqEntry(PdoContext, QueueId, nextIndex);
    
    _mm_prefetch((const char*)nextEntry, _MM_HINT_T0);
}
```

### 6.3 非临时存储

对于大数据传输，使用非临时存储避免污染缓存:

```c
/* 非临时内存复制 (用于大块数据) */
VOID
VnvmeNonTemporalCopy(
    _Out_writes_bytes_(Length) PVOID Destination,
    _In_reads_bytes_(Length) PVOID Source,
    _In_ SIZE_T Length
    )
{
    /* 要求 16 字节对齐 */
    ASSERT(((ULONG_PTR)Destination & 0xF) == 0);
    ASSERT(((ULONG_PTR)Source & 0xF) == 0);
    
    __m128i* dst = (__m128i*)Destination;
    __m128i* src = (__m128i*)Source;
    SIZE_T count = Length / 16;
    
    for (SIZE_T i = 0; i < count; i++) {
        __m128i data = _mm_load_si128(&src[i]);
        _mm_stream_si128(&dst[i], data);
    }
    
    _mm_sfence();  /* 确保存储完成 */
}
```

---

## 锁和同步优化

### 7.1 无锁环形缓冲区

共享内存中的提交/完成环使用无锁设计:

```c
/* 无锁单生产者单消费者队列 */

/* 生产者 (内核): 写入命令 */
BOOLEAN
VnvmeLockFreeEnqueue(
    _In_ PVNVME_RING Ring,
    _In_ PVOID Entry,
    _In_ SIZE_T EntrySize
    )
{
    ULONG head = Ring->Head;
    ULONG tail = ReadAcquire(&Ring->Tail);
    ULONG next = (head + 1) % Ring->Size;
    
    if (next == tail) {
        return FALSE;  /* 队列满 */
    }
    
    RtlCopyMemory(GetRingEntry(Ring, head), Entry, EntrySize);
    
    WriteRelease(&Ring->Head, next);
    
    return TRUE;
}

/* 消费者 (用户态): 读取命令 */
BOOLEAN
VnvmeLockFreeDequeue(
    _In_ PVNVME_RING Ring,
    _Out_ PVOID Entry,
    _In_ SIZE_T EntrySize
    )
{
    ULONG tail = Ring->Tail;
    ULONG head = ReadAcquire(&Ring->Head);
    
    if (tail == head) {
        return FALSE;  /* 队列空 */
    }
    
    RtlCopyMemory(Entry, GetRingEntry(Ring, tail), EntrySize);
    
    WriteRelease(&Ring->Tail, (tail + 1) % Ring->Size);
    
    return TRUE;
}
```

### 7.2 读写锁优化

对于读多写少的场景:

```c
/* 使用 ERESOURCE 替代 KSPIN_LOCK */
typedef struct _VNVME_RW_LOCK {
    ERESOURCE Resource;
} VNVME_RW_LOCK;

VOID VnvmeAcquireShared(PVNVME_RW_LOCK Lock)
{
    ExAcquireResourceSharedLite(&Lock->Resource, TRUE);
}

VOID VnvmeAcquireExclusive(PVNVME_RW_LOCK Lock)
{
    ExAcquireResourceExclusiveLite(&Lock->Resource, TRUE);
}

VOID VnvmeRelease(PVNVME_RW_LOCK Lock)
{
    ExReleaseResourceLite(&Lock->Resource);
}
```

### 7.3 Per-CPU 数据

减少跨 CPU 访问:

```c
/* 每 CPU 统计计数器 */
typedef struct _VNVME_PER_CPU_STATS {
    DECLSPEC_CACHEALIGN LONG64 CommandsProcessed;
    DECLSPEC_CACHEALIGN LONG64 BytesTransferred;
} VNVME_PER_CPU_STATS;

VNVME_PER_CPU_STATS g_PerCpuStats[MAXIMUM_PROCESSORS];

VOID
VnvmeIncrementCommandCount(VOID)
{
    ULONG cpu = KeGetCurrentProcessorNumber();
    InterlockedIncrement64(&g_PerCpuStats[cpu].CommandsProcessed);
}

LONG64
VnvmeGetTotalCommands(VOID)
{
    LONG64 total = 0;
    for (ULONG i = 0; i < KeQueryActiveProcessorCount(NULL); i++) {
        total += g_PerCpuStats[i].CommandsProcessed;
    }
    return total;
}
```

---

## 后端存储优化

### 8.1 直接 I/O

```c
/* 使用 FILE_FLAG_NO_BUFFERING */
HANDLE
VnvmeOpenBackendDirect(
    _In_ LPCWSTR Path
    )
{
    return CreateFileW(
        Path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING |      /* 绕过文件系统缓存 */
        FILE_FLAG_WRITE_THROUGH |     /* 立即写入 */
        FILE_FLAG_OVERLAPPED,         /* 异步 I/O */
        NULL
    );
}
```

### 8.2 异步 I/O

```c
/* 使用 I/O Completion Port 实现高效异步 */

typedef struct _VNVME_ASYNC_IO {
    OVERLAPPED Overlapped;
    USHORT CommandId;
    USHORT QueueId;
    PVOID Buffer;
    ULONG Length;
} VNVME_ASYNC_IO;

HANDLE g_IoCompletionPort;

VOID
VnvmeInitAsyncIo(VOID)
{
    g_IoCompletionPort = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,
        NULL,
        0,
        0  /* 使用 CPU 数量的线程 */
    );
}

VOID
VnvmeSubmitAsyncRead(
    HANDLE hFile,
    PVNVME_ASYNC_IO IoContext
    )
{
    ReadFile(
        hFile,
        IoContext->Buffer,
        IoContext->Length,
        NULL,
        &IoContext->Overlapped
    );
}

/* I/O 完成处理线程 */
DWORD WINAPI
IoCompletionThread(LPVOID Param)
{
    DWORD bytesTransferred;
    ULONG_PTR completionKey;
    LPOVERLAPPED overlapped;
    
    while (TRUE) {
        BOOL success = GetQueuedCompletionStatus(
            g_IoCompletionPort,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            INFINITE
        );
        
        if (overlapped) {
            PVNVME_ASYNC_IO io = CONTAINING_RECORD(
                overlapped, VNVME_ASYNC_IO, Overlapped
            );
            
            CompleteNvmeCommand(
                io->QueueId,
                io->CommandId,
                success ? NVME_STATUS_SUCCESS : NVME_STATUS_INTERNAL_ERROR
            );
        }
    }
}
```

### 8.3 内存映射后端

对于内存足够的场景:

```c
/* 使用内存映射文件作为后端 */
typedef struct _VNVME_MMAP_BACKEND {
    HANDLE hFile;
    HANDLE hMapping;
    PVOID BaseAddress;
    SIZE_T Size;
} VNVME_MMAP_BACKEND;

NTSTATUS
VnvmeInitMmapBackend(
    _Out_ PVNVME_MMAP_BACKEND Backend,
    _In_ LPCWSTR Path,
    _In_ SIZE_T Size
    )
{
    Backend->hFile = CreateFileW(Path, ...);
    
    Backend->hMapping = CreateFileMappingW(
        Backend->hFile,
        NULL,
        PAGE_READWRITE,
        (DWORD)(Size >> 32),
        (DWORD)Size,
        NULL
    );
    
    Backend->BaseAddress = MapViewOfFile(
        Backend->hMapping,
        FILE_MAP_ALL_ACCESS,
        0, 0, Size
    );
    
    Backend->Size = Size;
    return STATUS_SUCCESS;
}

/* 零拷贝读写 */
VOID
VnvmeMmapRead(
    _In_ PVNVME_MMAP_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _Out_ PVOID Buffer,
    _In_ ULONG Length
    )
{
    RtlCopyMemory(Buffer, (PUCHAR)Backend->BaseAddress + Offset, Length);
}
```

---

## 性能测试方法

### 9.1 基准测试工具

**CrystalDiskMark**:
```powershell
# 运行 CrystalDiskMark 测试
# 设置: 测试大小 1GB, 测试次数 5, 队列深度 32

# 预期结果记录格式:
# Sequential Read:  XXX MB/s
# Sequential Write: XXX MB/s
# Random 4K Q32:    XXX IOPS
```

**fio**:
```bash
# 顺序读测试
fio --name=seqread --rw=read --bs=128k --size=1G --numjobs=1 --iodepth=32 --direct=1 --filename=\\.\PhysicalDriveX

# 随机读测试  
fio --name=randread --rw=randread --bs=4k --size=1G --numjobs=4 --iodepth=32 --direct=1 --filename=\\.\PhysicalDriveX

# 混合读写测试
fio --name=randrw --rw=randrw --rwmixread=70 --bs=4k --size=1G --numjobs=4 --iodepth=32 --direct=1 --filename=\\.\PhysicalDriveX
```

### 9.2 延迟分析

```c
/* 延迟采样点 */
typedef struct _VNVME_LATENCY_SAMPLE {
    LARGE_INTEGER DoorbellWriteTime;    /* stornvme 写 Doorbell */
    LARGE_INTEGER DetectedTime;         /* vnvme 检测到命令 */
    LARGE_INTEGER ForwardedTime;        /* 转发到用户态 */
    LARGE_INTEGER ProcessedTime;        /* 用户态处理完成 */
    LARGE_INTEGER CompletedTime;        /* 投递完成到 CQ */
} VNVME_LATENCY_SAMPLE;

/* 延迟分解 */
/*
 * 检测延迟   = DetectedTime - DoorbellWriteTime   (轮询间隔)
 * 转发延迟   = ForwardedTime - DetectedTime       (共享内存写入)
 * 处理延迟   = ProcessedTime - ForwardedTime      (后端 I/O)
 * 完成延迟   = CompletedTime - ProcessedTime      (CQ 投递)
 * 总延迟     = CompletedTime - DoorbellWriteTime
 */
```

### 9.3 性能监控

```c
/* 实时性能计数器 */
typedef struct _VNVME_PERF_COUNTERS {
    volatile LONG64 CommandsPerSecond;
    volatile LONG64 BytesPerSecond;
    volatile LONG64 AvgLatencyUs;
    volatile LONG64 MaxLatencyUs;
    volatile LONG64 P99LatencyUs;
} VNVME_PERF_COUNTERS;

/* ETW 事件用于性能分析 */
// TODO: 添加 ETW tracing 支持
```

---

## 性能调优参数

### 10.1 内核驱动参数

| 参数 | 注册表路径 | 默认值 | 范围 | 说明 |
|------|------------|--------|------|------|
| `PollingIntervalUs` | `HKLM\SYSTEM\CurrentControlSet\Services\vnvme\Parameters` | 100 | 10-1000 | 轮询间隔 (μs) |
| `AdaptivePolling` | 同上 | 1 | 0/1 | 启用自适应轮询 |
| `BatchSize` | 同上 | 16 | 1-64 | 命令批处理大小 |
| `EventNotification` | 同上 | 0 | 0/1 | 启用事件通知 |

### 10.2 用户态服务参数

```ini
# vnvme-server.conf

[performance]
# 轮询模式: polling, event, hybrid
notify_mode = hybrid

# 轮询间隔 (微秒)
poll_interval_us = 50

# 命令批处理大小
batch_size = 16

# I/O 完成线程数 (0 = CPU 数量)
io_threads = 0

# 直接 I/O
direct_io = true
```

### 10.3 调优建议

| 场景 | 轮询间隔 | 批处理大小 | 通知模式 | 说明 |
|------|----------|------------|----------|------|
| 低延迟 | 10-50 μs | 1-4 | hybrid | 交互式应用 |
| 高吞吐 | 100-500 μs | 16-64 | polling | 批量传输 |
| 低功耗 | 500-1000 μs | 8-16 | event | 移动设备 |
| 均衡 | 100 μs | 16 | hybrid | 通用场景 |

---

## 实施路线图

### Phase 6.3 性能优化 (建议拆分)

**6.3.1 自适应轮询 (1 天)**
- [ ] 实现 `VNVME_ADAPTIVE_POLL` 结构
- [ ] 实现 `VnvmeAdjustPollingInterval()`
- [ ] 添加注册表参数配置
- [ ] 测试验证

**6.3.2 批处理优化 (1 天)**
- [ ] 实现 `VnvmeFetchCommandBatch()`
- [ ] 实现 `VnvmePostCompletionBatch()`
- [ ] 用户态批处理实现
- [ ] 测试验证

**6.3.3 事件通知 (2 天)**
- [ ] 实现 `VnvmeCreateUserEventHandle()`
- [ ] 实现混合通知模式
- [ ] 用户态等待实现
- [ ] IOCTL 返回事件句柄
- [ ] 测试验证

**6.3.4 内存优化 (1 天)**
- [ ] 缓存行对齐关键结构
- [ ] 优化内存屏障使用
- [ ] 测试验证

**6.3.5 后端优化 (1 天)**
- [ ] 实现异步 I/O (IOCP)
- [ ] 直接 I/O 支持
- [ ] 测试验证

---

## 参考资料

1. [Intel NVMe Specification](https://nvmexpress.org/specifications/)
2. [Windows Driver Performance Guidelines](https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/improving-performance)
3. [Lock-Free Queue Algorithms](https://www.1024cores.net/home/lock-free-algorithms)
4. [Memory Ordering in C++](https://en.cppreference.com/w/cpp/atomic/memory_order)
