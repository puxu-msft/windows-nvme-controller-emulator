# 系统架构设计 (v2 - 混合架构)

## 修订历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2025-12-23 | 初始全内核态设计 |
| v2 | 2025-12-23 | 采用混合用户态/内核态架构 |

---

## 项目目标

本项目实现一个 **Windows 软件 NVMe 控制器仿真器**，目标：

| 目标 | 说明 |
|------|------|
| **真实 NVMe 设备呈现** | 设备管理器中显示为 NVMe 控制器 |
| **原生 NVMe 驱动兼容** | 使用 Windows 原生 stornvme.sys 驱动 |
| **NVMe 工具支持** | nvme-cli、Crystal Disk Info 等可识别 |
| **用户态灵活性** | 类似 SPDK 的用户态架构，便于开发和调试 |
| **安全可靠** | 用户态崩溃不影响系统稳定性 |

---

## 设计约束

### Windows 下的技术限制

在设计前，必须理解 Windows 的几个关键限制：

| 限制 | 影响 | 应对策略 |
|------|------|---------|
| **无法拦截 MMIO** | stornvme 直接读写内存 | 提供真实物理内存作为 BAR0 |
| **无法软件触发中断** | 需要硬件或 Hypervisor | 利用 stornvme 的轮询机制 |
| **PnP 需要内核驱动** | 设备枚举必须内核态 | 最小内核驱动处理 PnP |
| **物理内存访问** | PRP 是物理地址 | 内核驱动负责 PRP 解析 |

### stornvme.sys 行为分析

理解 stornvme 的行为对设计至关重要：

```
stornvme.sys 初始化流程:
┌─────────────────────────────────────────────────────────────────────┐
│ 1. MmMapIoSpace(BAR0) 获取寄存器虚拟地址                             │
├─────────────────────────────────────────────────────────────────────┤
│ 2. 读取 CAP 寄存器，获取控制器能力                                    │
├─────────────────────────────────────────────────────────────────────┤
│ 3. 读取 VS 寄存器，获取 NVMe 版本                                    │
├─────────────────────────────────────────────────────────────────────┤
│ 4. 分配 Admin SQ/CQ 内存                                            │
├─────────────────────────────────────────────────────────────────────┤
│ 5. 写入 AQA/ASQ/ACQ 寄存器配置 Admin Queue                           │
├─────────────────────────────────────────────────────────────────────┤
│ 6. 写入 CC.EN=1 启用控制器                                          │
├─────────────────────────────────────────────────────────────────────┤
│ 7. 轮询 CSTS.RDY 直到为 1                                           │
├─────────────────────────────────────────────────────────────────────┤
│ 8. 发送 Identify Controller 命令                                     │
├─────────────────────────────────────────────────────────────────────┤
│ 9. 创建 I/O Queue，开始正常操作                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**关键发现**：
- stornvme 使用**轮询**检查 CSTS.RDY
- stornvme 在高负载时会**禁用中断**，使用轮询模式
- 这意味着我们不一定需要真正的中断！

---

## 整体架构

### 混合架构设计

```
┌─────────────────────────────────────────────────────────────────────┐
│                           用户态                                     │
│                                                                      │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                      vnvme-server.exe                          │  │
│  │                                                                │  │
│  │    ┌────────────┐  ┌────────────┐  ┌────────────────────────┐ │  │
│  │    │ 命令引擎   │  │ 后端存储   │  │ 管理接口               │ │  │
│  │    │            │  │            │  │                        │ │  │
│  │    │ • Identify │  │ • Memory   │  │ • REST API (可选)      │ │  │
│  │    │ • Read     │  │ • File     │  │ • gRPC (可选)          │ │  │
│  │    │ • Write    │  │ • VHD      │  │ • Named Pipe           │ │  │
│  │    │ • Flush    │  │ • iSCSI    │  │                        │ │  │
│  │    │ • DSM      │  │ • NVMe-oF  │  │                        │ │  │
│  │    │ • Create Q │  │            │  │                        │ │  │
│  │    │ • Delete Q │  │            │  │                        │ │  │
│  │    └────────────┘  └────────────┘  └────────────────────────┘ │  │
│  │                                                                │  │
│  │    共享内存视图:                                                │  │
│  │    ┌──────────────────────────────────────────────────────────┐│  │
│  │    │ 控制块 │ 命令环 │ 完成环 │ 数据缓冲池 │ 统计/日志        ││  │
│  │    └──────────────────────────────────────────────────────────┘│  │
│  │                          ▲                                     │  │
│  └──────────────────────────│─────────────────────────────────────┘  │
│                             │ DeviceIoControl / 事件 / 共享内存      │
├─────────────────────────────│───────────────────────────────────────┤
│                           内核态                                     │
│                             │                                        │
│  ┌──────────────────────────▼─────────────────────────────────────┐  │
│  │                        vnvme.sys                                │  │
│  │                   (单一内核驱动)                                 │  │
│  │                                                                 │  │
│  │  ┌───────────────────────────────────────────────────────────┐  │  │
│  │  │ 总线管理模块                                               │  │  │
│  │  │ • 根设备创建 (ROOT\VNVME)                                  │  │  │
│  │  │ • 子设备 PDO 创建和管理                                    │  │  │
│  │  │ • PnP IRP 处理                                             │  │  │
│  │  │ • PCIe 配置空间仿真                                        │  │  │
│  │  │ • 资源分配 (BAR0, Interrupt)                               │  │  │
│  │  └───────────────────────────────────────────────────────────┘  │  │
│  │                                                                 │  │
│  │  ┌───────────────────────────────────────────────────────────┐  │  │
│  │  │ BAR0 仿真模块                                              │  │  │
│  │  │ • 分配真实物理内存作为 BAR0                                │  │  │
│  │  │ • 初始化 CAP, VS 等静态寄存器                              │  │  │
│  │  │ • Doorbell 区域监控                                        │  │  │
│  │  │ • MSI-X Table/PBA 仿真                                     │  │  │
│  │  └───────────────────────────────────────────────────────────┘  │  │
│  │                                                                 │  │
│  │  ┌───────────────────────────────────────────────────────────┐  │  │
│  │  │ Doorbell 轮询引擎                                          │  │  │
│  │  │ • 高精度定时器 (10-100μs)                                  │  │  │
│  │  │ • 检测 SQ Tail 变化 → 提取命令 → 转发到用户态              │  │  │
│  │  │ • 检测 CQ Head 变化 → 更新内部状态                         │  │  │
│  │  │ • 自适应轮询频率 (负载感知)                                │  │  │
│  │  └───────────────────────────────────────────────────────────┘  │  │
│  │                                                                 │  │
│  │  ┌───────────────────────────────────────────────────────────┐  │  │
│  │  │ 用户态通信模块                                             │  │  │
│  │  │ • 共享内存分配和管理                                       │  │  │
│  │  │ • 命令/完成环形缓冲区                                      │  │  │
│  │  │ • 事件通知机制                                             │  │  │
│  │  │ • IOCTL 接口                                               │  │  │
│  │  └───────────────────────────────────────────────────────────┘  │  │
│  │                                                                 │  │
│  │  ┌───────────────────────────────────────────────────────────┐  │  │
│  │  │ PRP 解析和数据传输模块                                     │  │  │
│  │  │ • 解析 PRP1/PRP2 物理地址                                  │  │  │
│  │  │ • 映射物理内存                                             │  │  │
│  │  │ • 数据复制到/从共享缓冲区                                  │  │  │
│  │  │ • 大 I/O 零拷贝路径 (可选)                                 │  │  │
│  │  └───────────────────────────────────────────────────────────┘  │  │
│  │                                                                 │  │
│  │  ┌───────────────────────────────────────────────────────────┐  │  │
│  │  │ 中断仿真模块                                               │  │  │
│  │  │ • 完成写入 CQ 后设置 Phase Tag                             │  │  │
│  │  │ • 可选: KEVENT 唤醒机制                                    │  │  │
│  │  │ • 可选: 共享中断触发                                       │  │  │
│  │  └───────────────────────────────────────────────────────────┘  │  │
│  │                                                                 │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                             │                                        │
│                             │ 报告为 PCI 设备                        │
│                             ▼                                        │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                      stornvme.sys                                │  │
│  │                 (Windows 原生 NVMe 驱动)                          │  │
│  │                                                                  │  │
│  │  • 直接读写 BAR0 内存                                           │  │
│  │  • 提交命令到 Submission Queue                                  │  │
│  │  • 轮询/中断检查 Completion Queue                               │  │
│  │  • 更新 Doorbell                                                │  │
│  └─────────────────────────────────────────────────────────────────┘  │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌──────────────────────────────────────────────────────────────────────┐
│                    存储后端 (由用户态管理)                            │
│                                                                      │
│     ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐       │
│     │  Memory  │   │   File   │   │   VHD    │   │ Network  │       │
│     │ (RAM)    │   │ (Sparse) │   │ (VHD/X)  │   │ (iSCSI)  │       │
│     └──────────┘   └──────────┘   └──────────┘   └──────────┘       │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 组件职责详解

### 1. vnvme.sys (内核驱动)

**核心原则**：只做必须在内核态的事情，尽量简单。

#### 1.1 总线管理

```c
// 驱动入口
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, VnvmeEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, NULL, &config, NULL);
}

// 创建根总线设备
NTSTATUS VnvmeEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    // 创建总线 FDO
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
    // ...
}
```

#### 1.2 BAR0 内存分配

```c
// BAR0 布局 (64KB)
#define VNVME_BAR0_SIZE             0x10000

#define VNVME_BAR0_REGS_OFFSET      0x0000   // 0x0000-0x0FFF: NVMe 寄存器
#define VNVME_BAR0_REGS_SIZE        0x1000

#define VNVME_BAR0_DOORBELL_OFFSET  0x1000   // 0x1000-0x1FFF: Doorbell
#define VNVME_BAR0_DOORBELL_SIZE    0x1000

#define VNVME_BAR0_MSIX_TABLE_OFF   0x2000   // 0x2000-0x23FF: MSI-X Table
#define VNVME_BAR0_MSIX_PBA_OFF     0x2400   // 0x2400-0x2407: MSI-X PBA

NTSTATUS VnvmeAllocateBar0(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    // 分配连续物理内存
    PHYSICAL_ADDRESS lowAddr = {0};
    PHYSICAL_ADDRESS highAddr = {.QuadPart = -1};
    PHYSICAL_ADDRESS boundary = {0};
    
    Ctx->Bar0VirtAddr = MmAllocateContiguousMemorySpecifyCache(
        VNVME_BAR0_SIZE,
        lowAddr, highAddr, boundary,
        MmNonCached);
    
    if (!Ctx->Bar0VirtAddr) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Ctx->Bar0PhysAddr = MmGetPhysicalAddress(Ctx->Bar0VirtAddr);
    Ctx->Bar0Size = VNVME_BAR0_SIZE;
    
    // 初始化 NVMe 寄存器
    VnvmeInitRegisters(Ctx);
    
    return STATUS_SUCCESS;
}
```

#### 1.3 静态寄存器初始化

stornvme 读取时直接获得值，无需拦截：

```c
VOID VnvmeInitRegisters(PVNVME_CONTROLLER_CONTEXT Ctx)
{
    PVOID regs = Ctx->Bar0VirtAddr;
    
    // CAP (偏移 0x00, 64-bit, 只读)
    // 这些值 stornvme 会直接读取
    PNVME_CAP cap = (PNVME_CAP)regs;
    cap->MQES = 4095;           // 最大队列条目 4096
    cap->CQR = 1;               // 需要连续队列
    cap->AMS = 0;               // 仅 Round Robin
    cap->TO = 40;               // 超时 20 秒
    cap->DSTRD = 0;             // Doorbell 步长 4 字节
    cap->NSSRS = 0;             // 不支持子系统复位
    cap->CSS = 1;               // NVM Command Set
    cap->MPSMIN = 0;            // 最小页 4KB
    cap->MPSMAX = 0;            // 最大页 4KB
    
    // VS (偏移 0x08, 32-bit, 只读)
    PNVME_VS vs = (PNVME_VS)((PUCHAR)regs + 0x08);
    vs->MJR = 1;
    vs->MNR = 4;
    vs->TER = 0;
    
    // 其他寄存器初始化为 0
    // CC, CSTS, AQA, ASQ, ACQ 由 stornvme 写入，我们轮询检测
}
```

#### 1.4 Doorbell 轮询引擎

这是核心机制 - 检测 stornvme 的命令提交：

```c
// 控制器上下文
typedef struct _VNVME_CONTROLLER_CONTEXT {
    // BAR0
    PVOID Bar0VirtAddr;
    PHYSICAL_ADDRESS Bar0PhysAddr;
    
    // 寄存器缓存 (用于检测变化)
    ULONG CachedCC;
    ULONG CachedAQA;
    ULONG64 CachedASQ;
    ULONG64 CachedACQ;
    
    // Admin Queue
    PVNVME_QUEUE AdminSQ;
    PVNVME_QUEUE AdminCQ;
    USHORT AdminSQTailCached;
    
    // I/O Queues
    LIST_ENTRY IoQueues;
    USHORT ActiveIoQueueCount;
    
    // 轮询定时器
    WDFTIMER PollTimer;
    ULONG PollIntervalUs;      // 当前轮询间隔
    ULONG MinPollIntervalUs;   // 最小轮询间隔 (10μs)
    ULONG MaxPollIntervalUs;   // 最大轮询间隔 (1000μs)
    
    // 用户态通信
    PVOID SharedMemory;
    SIZE_T SharedMemorySize;
    KEVENT CommandReadyEvent;
    KEVENT CompletionReadyEvent;
    BOOLEAN UserConnected;
    
    // 状态
    VNVME_STATE State;
    
} VNVME_CONTROLLER_CONTEXT, *PVNVME_CONTROLLER_CONTEXT;

// 轮询定时器回调
VOID VnvmePollTimerCallback(WDFTIMER Timer)
{
    PVNVME_CONTROLLER_CONTEXT ctx = WdfObjectGetTypedContext(
        WdfTimerGetParentObject(Timer), VNVME_CONTROLLER_CONTEXT);
    
    BOOLEAN hadWork = FALSE;
    
    // 1. 检查 CC 寄存器变化 (控制器启用/禁用)
    PULONG ccReg = (PULONG)((PUCHAR)ctx->Bar0VirtAddr + NVME_CC_OFFSET);
    ULONG currentCC = *ccReg;
    if (currentCC != ctx->CachedCC) {
        VnvmeHandleCCChange(ctx, currentCC);
        ctx->CachedCC = currentCC;
        hadWork = TRUE;
    }
    
    // 2. 如果控制器已启用，检查队列配置变化
    if (ctx->State == VNVME_STATE_DISABLED) {
        PULONG aqaReg = (PULONG)((PUCHAR)ctx->Bar0VirtAddr + NVME_AQA_OFFSET);
        PULONG64 asqReg = (PULONG64)((PUCHAR)ctx->Bar0VirtAddr + NVME_ASQ_OFFSET);
        PULONG64 acqReg = (PULONG64)((PUCHAR)ctx->Bar0VirtAddr + NVME_ACQ_OFFSET);
        
        ctx->CachedAQA = *aqaReg;
        ctx->CachedASQ = *asqReg;
        ctx->CachedACQ = *acqReg;
    }
    
    // 3. 检查 Doorbell 变化
    if (ctx->State == VNVME_STATE_READY) {
        // 检查 Admin SQ Tail
        PUSHORT adminSQTail = (PUSHORT)((PUCHAR)ctx->Bar0VirtAddr + 
                                        VNVME_BAR0_DOORBELL_OFFSET);
        USHORT currentTail = *adminSQTail;
        if (currentTail != ctx->AdminSQTailCached) {
            VnvmeProcessAdminCommands(ctx, ctx->AdminSQTailCached, currentTail);
            ctx->AdminSQTailCached = currentTail;
            hadWork = TRUE;
        }
        
        // 检查所有 I/O SQ Tails
        // ...类似处理...
    }
    
    // 4. 自适应轮询间隔
    if (hadWork) {
        // 有工作时加快轮询
        ctx->PollIntervalUs = max(ctx->PollIntervalUs / 2, ctx->MinPollIntervalUs);
    } else {
        // 无工作时减慢轮询
        ctx->PollIntervalUs = min(ctx->PollIntervalUs * 2, ctx->MaxPollIntervalUs);
    }
    
    // 5. 重新调度定时器
    WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_US(ctx->PollIntervalUs));
}
```

#### 1.5 命令提取和转发

```c
VOID VnvmeProcessAdminCommands(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    USHORT OldTail,
    USHORT NewTail)
{
    PVNVME_QUEUE sq = Ctx->AdminSQ;
    PNVME_COMMAND sqBase = (PNVME_COMMAND)sq->MappedAddr;
    
    // 遍历新提交的命令
    USHORT idx = OldTail;
    while (idx != NewTail) {
        PNVME_COMMAND cmd = &sqBase[idx];
        
        // 将命令复制到共享内存环形缓冲区
        PVNVME_SHARED_COMMAND sharedCmd = VnvmeGetNextCommandSlot(Ctx);
        if (sharedCmd) {
            sharedCmd->QueueId = 0;  // Admin Queue
            sharedCmd->CommandIndex = idx;
            RtlCopyMemory(&sharedCmd->Command, cmd, sizeof(NVME_COMMAND));
            
            // 如果是 I/O 命令，解析 PRP 并复制数据
            if (cmd->CDW0.OPC == NVME_OPC_WRITE) {
                VnvmeCopyDataFromPrp(Ctx, cmd, sharedCmd);
            }
            
            VnvmeSubmitCommandToUser(Ctx, sharedCmd);
        }
        
        idx = (idx + 1) % sq->Size;
    }
    
    // 通知用户态有新命令
    KeSetEvent(&Ctx->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
}
```

#### 1.6 完成处理

```c
// 用户态处理完成后调用
VOID VnvmePostCompletion(
    PVNVME_CONTROLLER_CONTEXT Ctx,
    USHORT CqId,
    PNVME_COMPLETION Completion)
{
    PVNVME_QUEUE cq = (CqId == 0) ? Ctx->AdminCQ : VnvmeFindIoCQ(Ctx, CqId);
    if (!cq) return;
    
    PNVME_COMPLETION cqBase = (PNVME_COMPLETION)cq->MappedAddr;
    
    // 设置 Phase Tag
    Completion->Status = (Completion->Status & ~1) | (cq->Phase ? 1 : 0);
    
    // 写入 CQ
    RtlCopyMemory(&cqBase[cq->Tail], Completion, sizeof(NVME_COMPLETION));
    
    // 更新 Tail，处理 Phase 翻转
    cq->Tail++;
    if (cq->Tail >= cq->Size) {
        cq->Tail = 0;
        cq->Phase = !cq->Phase;
    }
    
    // 如果是 Read 命令，将数据复制到 stornvme 的缓冲区
    // (数据已在共享内存中准备好)
    
    // 可选：触发中断
    // 在实践中，stornvme 会定期轮询 CQ，所以中断不是必须的
}
```

---

### 2. vnvme-server.exe (用户态服务)

**核心原则**：承担所有可以在用户态完成的工作。

#### 2.1 主循环

```c
int main(int argc, char** argv)
{
    // 1. 解析配置
    VNVME_CONFIG config;
    VnvmeParseConfig(argc, argv, &config);
    
    // 2. 连接到内核驱动
    HANDLE deviceHandle = CreateFile(
        L"\\\\.\\VNVMEControl",
        GENERIC_READ | GENERIC_WRITE,
        0, NULL, OPEN_EXISTING, 0, NULL);
    
    if (deviceHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "无法连接到 vnvme.sys\n");
        return 1;
    }
    
    // 3. 映射共享内存
    VNVME_MAP_SHARED_MEMORY_REQUEST req;
    VNVME_MAP_SHARED_MEMORY_RESPONSE resp;
    DeviceIoControl(deviceHandle, IOCTL_VNVME_MAP_SHARED_MEMORY,
                    &req, sizeof(req), &resp, sizeof(resp), NULL, NULL);
    
    PVNVME_SHARED_MEMORY shared = (PVNVME_SHARED_MEMORY)resp.UserAddress;
    
    // 4. 初始化后端
    PVNVME_BACKEND backend = VnvmeCreateBackend(&config);
    
    // 5. 通知内核已就绪
    DeviceIoControl(deviceHandle, IOCTL_VNVME_USER_READY,
                    NULL, 0, NULL, 0, NULL, NULL);
    
    // 6. 主处理循环
    HANDLE events[2] = {
        shared->CommandReadyEvent,
        shared->ShutdownEvent
    };
    
    while (TRUE) {
        DWORD result = WaitForMultipleObjects(2, events, FALSE, 100);
        
        if (result == WAIT_OBJECT_0 + 1) {
            // 关机请求
            break;
        }
        
        // 处理所有待处理命令
        VnvmeProcessPendingCommands(shared, backend);
    }
    
    // 7. 清理
    VnvmeDestroyBackend(backend);
    CloseHandle(deviceHandle);
    
    return 0;
}
```

#### 2.2 命令处理

```c
VOID VnvmeProcessPendingCommands(
    PVNVME_SHARED_MEMORY Shared,
    PVNVME_BACKEND Backend)
{
    PVNVME_COMMAND_RING cmdRing = &Shared->CommandRing;
    
    while (cmdRing->Head != cmdRing->Tail) {
        PVNVME_SHARED_COMMAND cmd = &cmdRing->Commands[cmdRing->Head];
        NVME_COMPLETION completion = {0};
        
        // 根据 Opcode 处理
        switch (cmd->Command.CDW0.OPC) {
        case NVME_ADMIN_OPC_IDENTIFY:
            VnvmeHandleIdentify(Shared, cmd, &completion);
            break;
            
        case NVME_ADMIN_OPC_CREATE_IO_CQ:
            VnvmeHandleCreateIoCQ(Shared, cmd, &completion);
            break;
            
        case NVME_ADMIN_OPC_CREATE_IO_SQ:
            VnvmeHandleCreateIoSQ(Shared, cmd, &completion);
            break;
            
        case NVME_IO_OPC_READ:
            VnvmeHandleRead(Shared, cmd, Backend, &completion);
            break;
            
        case NVME_IO_OPC_WRITE:
            VnvmeHandleWrite(Shared, cmd, Backend, &completion);
            break;
            
        case NVME_IO_OPC_FLUSH:
            VnvmeHandleFlush(Backend, &completion);
            break;
            
        default:
            completion.Status = NVME_MAKE_STATUS(NVME_SCT_GENERIC, 
                                                  NVME_SC_INVALID_OPCODE, 0);
            break;
        }
        
        // 提交完成
        VnvmeSubmitCompletion(Shared, cmd->QueueId, &completion);
        
        // 前进 Head
        cmdRing->Head = (cmdRing->Head + 1) % cmdRing->Size;
    }
}
```

#### 2.3 后端存储抽象

```c
// 后端接口
typedef struct _VNVME_BACKEND_OPS {
    NTSTATUS (*Read)(PVNVME_BACKEND Backend, ULONG64 Lba, 
                     ULONG BlockCount, PVOID Buffer);
    NTSTATUS (*Write)(PVNVME_BACKEND Backend, ULONG64 Lba, 
                      ULONG BlockCount, PVOID Buffer);
    NTSTATUS (*Flush)(PVNVME_BACKEND Backend);
    VOID (*Destroy)(PVNVME_BACKEND Backend);
} VNVME_BACKEND_OPS;

// 文件后端实现
typedef struct _VNVME_FILE_BACKEND {
    VNVME_BACKEND Base;
    HANDLE FileHandle;
    ULONG64 SizeBytes;
    ULONG BlockSize;
} VNVME_FILE_BACKEND;

NTSTATUS VnvmeFileBackendRead(
    PVNVME_BACKEND Backend,
    ULONG64 Lba,
    ULONG BlockCount,
    PVOID Buffer)
{
    PVNVME_FILE_BACKEND fb = (PVNVME_FILE_BACKEND)Backend;
    
    LARGE_INTEGER offset;
    offset.QuadPart = Lba * fb->BlockSize;
    
    DWORD bytesToRead = BlockCount * fb->BlockSize;
    DWORD bytesRead;
    
    SetFilePointerEx(fb->FileHandle, offset, NULL, FILE_BEGIN);
    
    if (!ReadFile(fb->FileHandle, Buffer, bytesToRead, &bytesRead, NULL)) {
        return STATUS_IO_DEVICE_ERROR;
    }
    
    return STATUS_SUCCESS;
}

// 内存后端实现
typedef struct _VNVME_MEMORY_BACKEND {
    VNVME_BACKEND Base;
    PVOID Memory;
    ULONG64 SizeBytes;
    ULONG BlockSize;
} VNVME_MEMORY_BACKEND;

NTSTATUS VnvmeMemoryBackendRead(
    PVNVME_BACKEND Backend,
    ULONG64 Lba,
    ULONG BlockCount,
    PVOID Buffer)
{
    PVNVME_MEMORY_BACKEND mb = (PVNVME_MEMORY_BACKEND)Backend;
    
    ULONG64 offset = Lba * mb->BlockSize;
    ULONG length = BlockCount * mb->BlockSize;
    
    if (offset + length > mb->SizeBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    RtlCopyMemory(Buffer, (PUCHAR)mb->Memory + offset, length);
    return STATUS_SUCCESS;
}
```

---

## 共享内存设计

### 布局

```
┌─────────────────────────────────────────────────────────────────────┐
│                    共享内存布局 (总 64MB)                            │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  0x00000000  ┌──────────────────────────────────────────────────┐   │
│              │  控制块 (4KB)                                     │   │
│              │  • Magic: 0x454D564E ("VNME")                    │   │
│              │  • Version: 1                                    │   │
│              │  • State: RUNNING/STOPPED                        │   │
│              │  • Events handles                                │   │
│              │  • Statistics                                    │   │
│  0x00001000  ├──────────────────────────────────────────────────┤   │
│              │  命令环 (1MB)                                     │   │
│              │  • Ring size: 4096 entries                       │   │
│              │  • Entry size: 256 bytes                         │   │
│              │  • Head/Tail pointers                            │   │
│  0x00101000  ├──────────────────────────────────────────────────┤   │
│              │  完成环 (256KB)                                   │   │
│              │  • Ring size: 4096 entries                       │   │
│              │  • Entry size: 64 bytes                          │   │
│  0x00141000  ├──────────────────────────────────────────────────┤   │
│              │  数据缓冲区池 (62MB)                              │   │
│              │  • 用于 I/O 数据传输                              │   │
│              │  • 4KB 块为单位分配                               │   │
│              │  • 位图跟踪空闲块                                 │   │
│  0x04000000  └──────────────────────────────────────────────────┘   │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### 数据结构

```c
// 共享内存控制块
typedef struct _VNVME_SHARED_CONTROL {
    ULONG Magic;                    // 0x454D564E
    ULONG Version;                  // 1
    volatile LONG State;            // VNVME_SHARED_STATE_*
    
    // 事件句柄 (内核创建，用户态使用)
    HANDLE CommandReadyEvent;
    HANDLE CompletionReadyEvent;
    HANDLE ShutdownEvent;
    
    // 命令环配置
    ULONG CommandRingOffset;
    ULONG CommandRingSize;
    
    // 完成环配置
    ULONG CompletionRingOffset;
    ULONG CompletionRingSize;
    
    // 数据缓冲区配置
    ULONG DataBufferOffset;
    ULONG DataBufferSize;
    ULONG DataBufferBlockSize;
    
    // 统计
    ULONG64 CommandsProcessed;
    ULONG64 BytesRead;
    ULONG64 BytesWritten;
    ULONG64 Errors;
    
} VNVME_SHARED_CONTROL, *PVNVME_SHARED_CONTROL;

// 共享命令条目
typedef struct _VNVME_SHARED_COMMAND {
    USHORT QueueId;                 // 队列 ID (0=Admin)
    USHORT CommandIndex;            // 命令在 SQ 中的索引
    NVME_COMMAND Command;           // 64 bytes
    
    // 数据缓冲区 (用于 I/O 命令)
    ULONG DataBufferOffset;         // 在共享内存中的偏移
    ULONG DataBufferLength;         // 数据长度
    
    // 填充到 256 字节
    UCHAR Reserved[172];
    
} VNVME_SHARED_COMMAND, *PVNVME_SHARED_COMMAND;

C_ASSERT(sizeof(VNVME_SHARED_COMMAND) == 256);
```

---

## 开发路线图

### Phase 1: 最小可用 (Week 1-3)

- [ ] vnvme.sys 基础框架
  - [ ] 驱动入口和设备创建
  - [ ] 根设备安装 (ROOT\VNVME)
  - [ ] 控制设备 (\\.\VNVMEControl)
- [ ] BAR0 内存分配和寄存器初始化
- [ ] 子设备 PDO 创建
- [ ] PCIe 配置空间填充
- [ ] 让 stornvme.sys 成功加载

### Phase 2: Doorbell 轮询 (Week 4-5)

- [ ] 高精度定时器实现
- [ ] CC 寄存器检测
- [ ] Admin Queue 初始化
- [ ] Doorbell 轮询和命令提取

### Phase 3: 用户态通信 (Week 6-7)

- [ ] 共享内存分配
- [ ] 命令/完成环形缓冲区
- [ ] IOCTL 接口
- [ ] vnvme-server.exe 基础框架

### Phase 4: 命令处理 (Week 8-10)

- [ ] Identify Controller/Namespace
- [ ] Create/Delete I/O Queue
- [ ] Read/Write 命令
- [ ] Flush 命令

### Phase 5: 后端存储 (Week 11-12)

- [ ] 内存后端
- [ ] 文件后端
- [ ] VHD 后端 (可选)

### Phase 6: 完善 (Week 13-14)

- [ ] 错误处理
- [ ] 性能优化
- [ ] 文档和测试
- [ ] 管理工具 (vnvmectl)

---

## 参考资源

- [NVM Express Specification](https://nvmexpress.org/specifications/)
- [QEMU NVMe 实现](https://github.com/qemu/qemu/blob/master/hw/nvme/ctrl.c)
- [SPDK NVMe 用户态驱动](https://spdk.io/doc/nvme.html)
- [Windows 驱动开发](https://learn.microsoft.com/en-us/windows-hardware/drivers/)
- [WDF 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/)
