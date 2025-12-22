# 用户态架构分析

本文档分析将虚拟 NVMe 控制器的部分功能从内核态移动到用户态的可行性。

## 动机

### 内核态开发的挑战

| 问题 | 影响 |
|------|------|
| **稳定性风险** | Bug 导致蓝屏 (BSOD)，整个系统崩溃 |
| **调试困难** | 需要双机调试或本地内核调试 |
| **开发周期长** | 每次修改需要重启或重新加载驱动 |
| **安全风险** | 内核代码有完全系统权限 |
| **签名要求** | 需要驱动签名（测试或正式） |

### 用户态的优势

| 优势 | 说明 |
|------|------|
| **进程隔离** | 崩溃只影响单个进程 |
| **易于调试** | 标准调试器，热重载 |
| **快速迭代** | 无需重启系统 |
| **语言灵活** | 可使用 Rust、C++、Python 等 |
| **无需签名** | 普通应用程序 |

---

## 架构分析

### 当前全内核态架构

```
┌─────────────────────────────────────────────────────────────────────┐
│                    当前架构 (全内核态)                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│    stornvme.sys (Windows 原生)                                       │
│         │                                                            │
│         ▼ MMIO / 中断                                                │
│    ┌─────────────────────────────────────────────────────────┐      │
│    │                vnvme_emu.sys                              │      │
│    │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐ │      │
│    │  │ 寄存器   │  │ 队列引擎 │  │ 命令处理 │  │ 后端存储 │ │      │
│    │  │ 仿真     │  │          │  │          │  │          │ │      │
│    │  └──────────┘  └──────────┘  └──────────┘  └──────────┘ │      │
│    └─────────────────────────────────────────────────────────┘      │
│         │                                                            │
│         ▼                                                            │
│    vnvme_bus.sys (PCIe 枚举)                                         │
│                                                                      │
│    ═══════════════════════════════════════════════════════════      │
│                         内核态                                        │
└─────────────────────────────────────────────────────────────────────┘
```

### 必须保留在内核态的功能

某些功能由于 Windows 架构限制，**必须**在内核态实现：

| 功能 | 原因 |
|------|------|
| **PCIe 设备枚举** | PnP 子系统只接受内核驱动报告的设备 |
| **BAR0 MMIO 拦截** | stornvme.sys 直接访问物理/虚拟地址 |
| **MSI-X 中断注入** | 需要访问 LAPIC 或 Hypervisor |
| **物理内存映射** | PRP 指向的是物理地址 |

### 可以移动到用户态的功能

| 功能 | 可行性 | 说明 |
|------|--------|------|
| **命令解析/验证** | ✅ 高 | 纯数据处理，无硬件依赖 |
| **后端 I/O** | ✅ 高 | 文件/网络 I/O 在用户态更灵活 |
| **数据转换/压缩** | ✅ 高 | CPU 密集型，适合用户态 |
| **管理接口** | ✅ 高 | GUI/CLI 天然属于用户态 |
| **Identify 数据生成** | ✅ 高 | 纯数据构造 |
| **日志/监控** | ✅ 高 | 适合用户态服务 |
| **快照/克隆** | ✅ 高 | 复杂逻辑适合用户态 |

---

## 混合架构设计

### 架构概览

```
┌─────────────────────────────────────────────────────────────────────┐
│                    混合架构 (内核+用户态)                             │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  用户态                                                              │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                    vnvme_server.exe                            │  │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐       │  │
│  │  │ 命令处理 │  │ 后端存储 │  │ 管理接口 │  │ 监控/日志│       │  │
│  │  │ (Admin)  │  │ (Memory/ │  │ (REST/   │  │          │       │  │
│  │  │ (I/O)    │  │  File/   │  │  gRPC)   │  │          │       │  │
│  │  │          │  │  Network)│  │          │  │          │       │  │
│  │  └──────────┘  └──────────┘  └──────────┘  └──────────┘       │  │
│  │        │                                                       │  │
│  │        │ 共享内存 + 事件                                       │  │
│  └────────│───────────────────────────────────────────────────────┘  │
│           │                                                          │
│  ─────────│──────────────────────────────────────────────────────   │
│           │                                                          │
│  内核态   │                                                          │
│  ┌────────│───────────────────────────────────────────────────────┐  │
│  │        ▼                                                       │  │
│  │  ┌─────────────────────────────────────────────────────────┐  │  │
│  │  │              vnvme_shim.sys (最小内核驱动)               │  │  │
│  │  │  ┌──────────┐  ┌──────────┐  ┌──────────┐               │  │  │
│  │  │  │ PCIe枚举 │  │ MMIO转发 │  │ 中断注入 │               │  │  │
│  │  │  │ (PDO)    │  │ (到用户态)│  │ (从用户态)│               │  │  │
│  │  │  └──────────┘  └──────────┘  └──────────┘               │  │  │
│  │  └─────────────────────────────────────────────────────────┘  │  │
│  │        │                                                       │  │
│  │        ▼                                                       │  │
│  │  vnvme_bus.sys (保持不变)                                      │  │
│  └───────────────────────────────────────────────────────────────┘  │
│           │                                                          │
│           ▼                                                          │
│    stornvme.sys                                                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 组件职责

#### vnvme_shim.sys (最小内核驱动)

仅包含必须在内核态的功能：

```c
// 核心职责
typedef struct _VNVME_SHIM_CONTEXT {
    // PCIe 配置空间 (由内核维护)
    VNVME_PCIE_CONFIG PcieConfig;
    
    // BAR0 内存 (共享给用户态)
    PVOID Bar0KernelVa;
    PVOID Bar0UserVa;
    ULONG Bar0Size;
    
    // 通信机制
    PKEVENT CommandEvent;      // 通知用户态有新命令
    PKEVENT CompletionEvent;   // 用户态完成通知
    
    // 共享命令队列
    PVOID SharedMemory;        // 内核-用户共享内存
    ULONG SharedMemorySize;
    
    // 中断注入
    WDFINTERRUPT Interrupt;
    
} VNVME_SHIM_CONTEXT, *PVNVME_SHIM_CONTEXT;
```

#### vnvme_server.exe (用户态服务)

处理实际的业务逻辑：

```c
// 用户态服务主结构
typedef struct _VNVME_USER_CONTEXT {
    // 与内核通信
    HANDLE DeviceHandle;
    HANDLE CommandEvent;
    HANDLE CompletionEvent;
    PVOID SharedMemory;
    
    // NVMe 状态
    VNVME_CONTROLLER_STATE State;
    VNVME_REGISTERS Registers;
    
    // 队列
    VNVME_USER_QUEUE AdminSQ;
    VNVME_USER_QUEUE AdminCQ;
    LIST_ENTRY IoQueues;
    
    // 后端
    PVNVME_BACKEND Backend;
    
    // 工作线程池
    PTP_POOL ThreadPool;
    
} VNVME_USER_CONTEXT, *PVNVME_USER_CONTEXT;
```

---

## 内核-用户态通信机制

### 方案 1: 共享内存 + 事件

最高效的方案，类似于 SPDK 的设计：

```
┌─────────────────────────────────────────────────────────────────────┐
│                    共享内存布局                                       │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐    │
│  │  Header (控制信息)                               64 字节    │    │
│  │  ├── magic: 0x564E564D                                      │    │
│  │  ├── version: 1                                             │    │
│  │  ├── state: RUNNING                                         │    │
│  │  ├── kernel_seq: 序列号                                     │    │
│  │  └── user_seq: 序列号                                       │    │
│  ├─────────────────────────────────────────────────────────────┤    │
│  │  Request Ring Buffer                            64 KB       │    │
│  │  (内核 → 用户态: MMIO 访问请求)                              │    │
│  ├─────────────────────────────────────────────────────────────┤    │
│  │  Response Ring Buffer                           64 KB       │    │
│  │  (用户态 → 内核: MMIO 响应)                                  │    │
│  ├─────────────────────────────────────────────────────────────┤    │
│  │  Command Buffer                                 1 MB        │    │
│  │  (NVMe 命令队列镜像)                                        │    │
│  ├─────────────────────────────────────────────────────────────┤    │
│  │  Data Buffer                                    可配置      │    │
│  │  (I/O 数据传输)                                             │    │
│  └─────────────────────────────────────────────────────────────┘    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

#### 请求/响应结构

```c
//
// 内核 → 用户态请求
//
typedef struct _VNVME_KERNEL_REQUEST {
    ULONG64 SequenceNumber;
    ULONG Type;
    union {
        // MMIO 读请求
        struct {
            ULONG Offset;
            ULONG Size;
        } MmioRead;
        
        // MMIO 写请求
        struct {
            ULONG Offset;
            ULONG Size;
            ULONG64 Value;
        } MmioWrite;
        
        // Doorbell 写请求
        struct {
            USHORT QueueId;
            USHORT NewValue;
            BOOLEAN IsSubmission;
        } Doorbell;
        
        // 命令提交通知
        struct {
            USHORT SqId;
            USHORT CommandCount;
        } CommandNotify;
    };
} VNVME_KERNEL_REQUEST, *PVNVME_KERNEL_REQUEST;

//
// 用户态 → 内核响应
//
typedef struct _VNVME_USER_RESPONSE {
    ULONG64 SequenceNumber;
    NTSTATUS Status;
    union {
        // MMIO 读响应
        struct {
            ULONG64 Value;
        } MmioRead;
        
        // 完成通知
        struct {
            USHORT CqId;
            USHORT CompletionCount;
            BOOLEAN TriggerInterrupt;
            USHORT Vector;
        } Completion;
    };
} VNVME_USER_RESPONSE, *PVNVME_USER_RESPONSE;
```

#### 通信流程

```c
// ===================== 内核态 (vnvme_shim.sys) =====================

NTSTATUS VnvmeShimHandleMmioRead(
    _In_ PVNVME_SHIM_CONTEXT Context,
    _In_ ULONG Offset,
    _Out_ PULONG64 Value)
{
    PVNVME_SHARED_MEMORY shared = Context->SharedMemory;
    VNVME_KERNEL_REQUEST request;
    VNVME_USER_RESPONSE response;
    
    // 构造请求
    request.SequenceNumber = InterlockedIncrement64(&shared->KernelSeq);
    request.Type = REQUEST_MMIO_READ;
    request.MmioRead.Offset = Offset;
    request.MmioRead.Size = 4;
    
    // 放入请求队列
    VnvmeRingBufferPush(&shared->RequestRing, &request);
    
    // 通知用户态
    KeSetEvent(Context->CommandEvent, IO_NO_INCREMENT, FALSE);
    
    // 等待响应 (带超时)
    LARGE_INTEGER timeout;
    timeout.QuadPart = -10000 * 100;  // 100ms
    
    NTSTATUS status = KeWaitForSingleObject(
        Context->CompletionEvent,
        Executive, KernelMode, FALSE, &timeout);
    
    if (status == STATUS_TIMEOUT) {
        // 用户态服务可能已崩溃，返回默认值
        *Value = 0xFFFFFFFF;
        return STATUS_IO_TIMEOUT;
    }
    
    // 获取响应
    VnvmeRingBufferPop(&shared->ResponseRing, &response);
    *Value = response.MmioRead.Value;
    
    return response.Status;
}

// ===================== 用户态 (vnvme_server.exe) =====================

void VnvmeUserMainLoop(PVNVME_USER_CONTEXT ctx)
{
    VNVME_KERNEL_REQUEST request;
    VNVME_USER_RESPONSE response;
    
    while (ctx->Running) {
        // 等待内核请求
        WaitForSingleObject(ctx->CommandEvent, INFINITE);
        
        // 处理所有待处理请求
        while (VnvmeRingBufferPop(&ctx->Shared->RequestRing, &request)) {
            response.SequenceNumber = request.SequenceNumber;
            
            switch (request.Type) {
            case REQUEST_MMIO_READ:
                response.Status = VnvmeHandleMmioRead(ctx,
                    request.MmioRead.Offset,
                    &response.MmioRead.Value);
                break;
                
            case REQUEST_MMIO_WRITE:
                response.Status = VnvmeHandleMmioWrite(ctx,
                    request.MmioWrite.Offset,
                    request.MmioWrite.Value);
                break;
                
            case REQUEST_DOORBELL:
                response.Status = VnvmeHandleDoorbell(ctx,
                    request.Doorbell.QueueId,
                    request.Doorbell.NewValue,
                    request.Doorbell.IsSubmission);
                break;
            }
            
            // 发送响应
            VnvmeRingBufferPush(&ctx->Shared->ResponseRing, &response);
            SetEvent(ctx->CompletionEvent);
        }
    }
}
```

### 方案 2: IOCTL 批处理

对于较低频率的操作，使用传统 IOCTL：

```c
// 批量命令处理 IOCTL
#define IOCTL_VNVME_PROCESS_COMMANDS \
    CTL_CODE(FILE_DEVICE_VNVME, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _VNVME_BATCH_COMMANDS {
    ULONG CommandCount;
    NVME_COMMAND Commands[64];
} VNVME_BATCH_COMMANDS;

typedef struct _VNVME_BATCH_COMPLETIONS {
    ULONG CompletionCount;
    NVME_COMPLETION Completions[64];
} VNVME_BATCH_COMPLETIONS;
```

---

## 数据路径优化

### 零拷贝设计

对于 I/O 数据，避免不必要的复制：

```
┌─────────────────────────────────────────────────────────────────────┐
│                    零拷贝数据路径                                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. stornvme.sys 分配 DMA 缓冲区                                     │
│                    │                                                 │
│                    ▼                                                 │
│  2. 内核驱动将 PRP 地址传给用户态                                     │
│                    │                                                 │
│                    ▼                                                 │
│  3. 用户态通过特殊 IOCTL 访问物理内存                                 │
│     (或使用预映射的共享缓冲区)                                        │
│                    │                                                 │
│                    ▼                                                 │
│  4. 直接 DMA 到/从后端存储                                           │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

#### 物理内存访问 (需要内核配合)

```c
// 内核态：映射物理内存到用户空间
NTSTATUS VnvmeShimMapPhysicalMemory(
    _In_ PVNVME_SHIM_CONTEXT Context,
    _In_ ULONG64 PhysicalAddress,
    _In_ ULONG Size,
    _Out_ PVOID* UserVa)
{
    PHYSICAL_ADDRESS physAddr;
    PMDL mdl;
    PVOID kernelVa;
    PVOID userVa;
    
    physAddr.QuadPart = PhysicalAddress;
    
    // 映射到内核空间
    kernelVa = MmMapIoSpace(physAddr, Size, MmNonCached);
    if (!kernelVa) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 创建 MDL
    mdl = IoAllocateMdl(kernelVa, Size, FALSE, FALSE, NULL);
    MmBuildMdlForNonPagedPool(mdl);
    
    // 映射到用户空间
    __try {
        userVa = MmMapLockedPagesSpecifyCache(
            mdl, UserMode, MmNonCached,
            NULL, FALSE, NormalPagePriority);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        IoFreeMdl(mdl);
        MmUnmapIoSpace(kernelVa, Size);
        return STATUS_ACCESS_VIOLATION;
    }
    
    // 保存映射信息以便后续释放
    // ...
    
    *UserVa = userVa;
    return STATUS_SUCCESS;
}
```

---

## 性能考虑

### 延迟分析

| 操作 | 全内核态 | 混合架构 | 开销 |
|------|----------|----------|------|
| 寄存器读取 | ~100ns | ~10μs | 100x |
| Doorbell 写入 | ~100ns | ~5μs | 50x |
| 小 I/O (4KB) | ~5μs | ~15μs | 3x |
| 大 I/O (128KB) | ~50μs | ~60μs | 1.2x |

### 优化策略

1. **批处理**：累积多个操作一起处理
2. **轮询模式**：高负载时避免事件等待开销
3. **预分配缓冲区**：避免动态分配
4. **无锁队列**：使用原子操作的环形缓冲区

```c
// 自适应轮询/等待
void VnvmeAdaptiveWait(PVNVME_USER_CONTEXT ctx)
{
    ULONG spinCount = 0;
    const ULONG maxSpin = 10000;
    
    while (!VnvmeHasPendingRequests(ctx)) {
        if (spinCount < maxSpin) {
            // 短暂自旋
            _mm_pause();
            spinCount++;
        } else {
            // 切换到事件等待
            WaitForSingleObject(ctx->CommandEvent, 1);  // 1ms 超时
            spinCount = 0;
        }
    }
}
```

---

## 可靠性设计

### 用户态服务崩溃处理

```c
// 内核态：检测用户态服务状态
typedef struct _VNVME_HEALTH_CHECK {
    volatile LONG64 UserHeartbeat;
    volatile LONG64 KernelHeartbeat;
    LARGE_INTEGER LastUserBeat;
} VNVME_HEALTH_CHECK;

VOID VnvmeShimHealthCheckTimer(WDFTIMER Timer)
{
    PVNVME_SHIM_CONTEXT ctx = WdfTimerGetParentObject(Timer);
    PVNVME_HEALTH_CHECK health = &ctx->HealthCheck;
    LARGE_INTEGER now;
    
    KeQuerySystemTime(&now);
    
    // 检查用户态心跳
    LONG64 userBeat = InterlockedCompareExchange64(
        &health->UserHeartbeat, 0, 0);
    
    if (userBeat == ctx->LastUserBeat) {
        // 3 秒无心跳，认为用户态已死
        LONGLONG elapsed = (now.QuadPart - health->LastUserBeat.QuadPart) / 10000;
        if (elapsed > 3000) {
            VnvmeShimHandleUserCrash(ctx);
        }
    } else {
        ctx->LastUserBeat = userBeat;
        health->LastUserBeat = now;
    }
    
    // 更新内核心跳
    InterlockedIncrement64(&health->KernelHeartbeat);
}

VOID VnvmeShimHandleUserCrash(PVNVME_SHIM_CONTEXT ctx)
{
    // 1. 标记设备为错误状态
    ctx->State = VNVME_STATE_ERROR;
    
    // 2. 对所有待处理 I/O 返回错误
    VnvmeShimFailAllPendingIo(ctx, STATUS_DEVICE_NOT_READY);
    
    // 3. 尝试重启用户态服务
    VnvmeShimRestartUserService(ctx);
    
    // 4. 如果重启失败，触发设备移除
    // IoInvalidateDeviceState(ctx->Pdo);
}
```

### 用户态服务恢复

```c
// 用户态启动时恢复状态
NTSTATUS VnvmeUserRecover(PVNVME_USER_CONTEXT ctx)
{
    // 1. 重新连接共享内存
    ctx->SharedMemory = MapSharedMemory();
    
    // 2. 从共享内存恢复状态
    PVNVME_SHARED_STATE state = ctx->SharedMemory;
    ctx->Registers = state->Registers;
    
    // 3. 重建队列状态
    for (USHORT i = 0; i < state->ActiveQueueCount; i++) {
        VnvmeUserRecreateQueue(ctx, &state->QueueInfo[i]);
    }
    
    // 4. 恢复后端连接
    VnvmeBackendReconnect(ctx->Backend);
    
    // 5. 通知内核已恢复
    VnvmeUserNotifyRecovery(ctx);
    
    return STATUS_SUCCESS;
}
```

---

## 类似项目参考

### 1. SPDK (Storage Performance Development Kit)

- **架构**：完全绕过内核，直接访问 NVMe 硬件
- **机制**：UIO/VFIO 提供用户态 PCIe 访问
- **局限**：需要专用设备，不能与内核驱动共存

### 2. FUSE (Filesystem in Userspace)

- **架构**：内核提供 VFS 接口，用户态实现文件系统
- **机制**：/dev/fuse 设备，请求/响应队列
- **参考**：我们的设计类似但针对块设备

### 3. vhost-user (QEMU)

- **架构**：QEMU 的 virtio 后端运行在独立用户态进程
- **机制**：Unix socket + 共享内存
- **参考**：成熟的用户态设备仿真模型

### 4. Windows Projected File System (ProjFS)

- **架构**：内核提供占位符，用户态按需提供内容
- **机制**：回调到用户态服务
- **参考**：Windows 原生的用户态文件系统支持

---

## 实现建议

### 阶段 1: 最小可行产品

先实现基本的混合架构：

```
内核态 (vnvme_shim.sys):
├── PCIe 设备枚举
├── BAR0 MMIO 拦截和转发
├── 共享内存管理
└── 中断注入

用户态 (vnvme_server.exe):
├── NVMe 寄存器模拟
├── Admin 命令处理
├── 内存后端 (简单实现)
└── 基本日志
```

### 阶段 2: 完整功能

扩展用户态功能：

```
用户态扩展:
├── 完整命令处理
├── 文件/VHD 后端
├── 零拷贝数据路径
├── 管理 REST API
└── 监控仪表板
```

### 阶段 3: 高级特性

```
高级特性:
├── 多控制器支持
├── 快照和克隆
├── 远程后端 (iSCSI, NVMe-oF)
├── 数据压缩/重删
└── QoS 控制
```

---

## 结论

### 可行性评估

| 方面 | 评估 |
|------|------|
| **技术可行性** | ✅ 高 - 有成熟的参考实现 |
| **性能影响** | ⚠️ 中等 - 小 I/O 延迟增加，大 I/O 可接受 |
| **开发效率** | ✅ 显著提升 - 主要逻辑在用户态 |
| **可靠性** | ⚠️ 需要设计 - 需处理用户态崩溃 |
| **复杂度** | ⚠️ 增加 - 需维护两个组件 |

### 推荐方案

**采用混合架构**，原因：

1. 大部分代码可以在安全的用户态开发和调试
2. 后端存储和管理逻辑更灵活
3. 只有最小的内核代码需要签名和特别小心
4. 类似 SPDK 的灵活性，但兼容 Windows 现有驱动栈

### 下一步

1. 设计详细的共享内存协议
2. 实现最小内核 shim 驱动
3. 实现用户态服务框架
4. 性能基准测试和优化
