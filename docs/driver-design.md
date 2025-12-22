# 驱动详细设计

## 模块划分

### 1. vnvme_driver.c - 驱动入口
```
主要函数:
- DriverEntry()          驱动加载入口
- VNvmeEvtDriverUnload() 驱动卸载
- VNvmeEvtDeviceAdd()    设备添加回调
```

### 2. vnvme_controller.c - 控制器模拟
```
主要函数:
- ControllerInitialize()    初始化控制器
- ControllerReset()         重置控制器
- ControllerEnable()        启用控制器
- ControllerDisable()       禁用控制器
- ControllerGetCapabilities() 获取能力
```

### 3. vnvme_admin.c - Admin 命令处理
```
主要函数:
- AdminQueueProcess()       处理 Admin 队列
- AdminIdentify()           处理 Identify 命令
- AdminCreateIoCq()         创建 I/O 完成队列
- AdminCreateIoSq()         创建 I/O 提交队列
- AdminDeleteIoCq()         删除 I/O 完成队列
- AdminDeleteIoSq()         删除 I/O 提交队列
- AdminGetFeatures()        获取特性
- AdminSetFeatures()        设置特性
```

### 4. vnvme_io.c - I/O 命令处理
```
主要函数:
- IoQueueProcess()          处理 I/O 队列
- IoRead()                  读取命令
- IoWrite()                 写入命令
- IoFlush()                 刷新命令
```

### 5. vnvme_namespace.c - 命名空间管理
```
主要函数:
- NamespaceCreate()         创建命名空间
- NamespaceDestroy()        销毁命名空间
- NamespaceGetInfo()        获取命名空间信息
```

### 6. vnvme_backend.c - 存储后端
```
主要函数:
- BackendInitialize()       初始化后端
- BackendRead()             读取数据
- BackendWrite()            写入数据
- BackendFlush()            刷新数据
- BackendShutdown()         关闭后端
```

## 文件结构
```
virtual-nvme-driver/
├── docs/                   # 文档目录
├── src/
│   ├── driver/
│   │   ├── vnvme_driver.c
│   │   ├── vnvme_driver.h
│   │   ├── vnvme_controller.c
│   │   ├── vnvme_controller.h
│   │   ├── vnvme_admin.c
│   │   ├── vnvme_admin.h
│   │   ├── vnvme_io.c
│   │   ├── vnvme_io.h
│   │   ├── vnvme_namespace.c
│   │   ├── vnvme_namespace.h
│   │   ├── vnvme_backend.c
│   │   ├── vnvme_backend.h
│   │   ├── vnvme_queue.c
│   │   ├── vnvme_queue.h
│   │   ├── vnvme_interrupt.c
│   │   ├── vnvme_interrupt.h
│   │   ├── vnvme_memory.c
│   │   ├── vnvme_memory.h
│   │   └── vnvme_common.h
│   └── include/
│       └── nvme_spec.h     # NVMe 规范定义
├── bus/
│   ├── vnvmebus.c          # 虚拟总线驱动
│   ├── vnvmebus.h
│   └── vnvmebus.inf
├── inf/
│   └── vnvme.inf           # 驱动安装信息
├── test/                   # 测试代码
└── tools/
    └── vnvmectl/           # 用户态控制工具
```

## 状态机

### 控制器状态
```
                    ┌─────────────────────────────────────┐
                    │                                     │
                    ▼                                     │
DISABLED ──[CC.EN=1]──> ENABLING ──[timeout]──> FATAL_ERROR
    ^                      │
    │                      │[ready]
    │                      ▼
    │                   ENABLED
    │                      │
    │        [CC.EN=0]     │[CC.SHN!=0]
    │          │           │
    │          ▼           ▼
    └────── DISABLING <── SHUTDOWN
```

### 状态转换详细说明

| 当前状态 | 触发条件 | 目标状态 | 动作 |
|----------|----------|----------|------|
| DISABLED | CC.EN = 1 | ENABLING | 初始化队列，准备控制器 |
| ENABLING | 初始化完成 | ENABLED | 设置 CSTS.RDY = 1 |
| ENABLING | 超时 (CAP.TO) | FATAL_ERROR | 设置 CSTS.CFS = 1 |
| ENABLED | CC.EN = 0 | DISABLING | 停止处理命令 |
| ENABLED | CC.SHN != 0 | SHUTDOWN | 执行关机序列 |
| DISABLING | 清理完成 | DISABLED | 设置 CSTS.RDY = 0 |

## 中断模拟模块

### 7. vnvme_interrupt.c - 中断模拟
```
主要函数:
- InterruptInitialize()     初始化中断模拟
- InterruptRaise()          触发中断通知
- InterruptMask()           屏蔽中断
- InterruptUnmask()         取消屏蔽中断
- InterruptGetVector()      获取中断向量
```

由于是虚拟设备，我们不使用真实硬件中断，而是通过以下机制模拟：

```c
// 中断模拟策略
typedef enum _VNVME_INTERRUPT_MODE {
    VNVME_INT_POLLING,      // 轮询模式 (无中断)
    VNVME_INT_DPC,          // 使用 DPC 模拟
    VNVME_INT_WORKITEM,     // 使用工作项模拟
} VNVME_INTERRUPT_MODE;

// 完成通知流程
VOID NotifyCompletion(PVNVME_CONTROLLER Ctrl, UINT16 CQId) {
    PVNVME_QUEUE cq = Ctrl->IoCQs[CQId];
    
    // 检查中断是否被屏蔽
    if (IsInterruptMasked(Ctrl, CQId)) {
        return;
    }
    
    // 根据模式触发通知
    switch (Ctrl->InterruptMode) {
        case VNVME_INT_DPC:
            KeInsertQueueDpc(&Ctrl->CompletionDpc, cq, NULL);
            break;
        case VNVME_INT_WORKITEM:
            IoQueueWorkItem(Ctrl->CompletionWorkItem, ...);
            break;
    }
}
```

## 内存池管理模块

### 8. vnvme_memory.c - 内存管理
```
主要函数:
- MemoryPoolCreate()        创建内存池
- MemoryPoolDestroy()       销毁内存池
- MemoryAllocate()          分配内存
- MemoryFree()              释放内存
- MemoryMapPhysical()       映射物理地址
- MemoryUnmapPhysical()     取消映射
```

### 内存分配策略

```c
// 使用 Lookaside List 优化频繁分配
typedef struct _VNVME_MEMORY_POOL {
    LOOKASIDE_LIST_EX SqePool;      // SQE 对象池
    LOOKASIDE_LIST_EX CqePool;      // CQE 对象池
    LOOKASIDE_LIST_EX IoContextPool; // I/O 上下文池
    
    PNPAGED_LOOKASIDE_LIST SmallBufferPool;   // 小缓冲区 (<=4KB)
    PNPAGED_LOOKASIDE_LIST MediumBufferPool;  // 中缓冲区 (<=64KB)
    
    // 大缓冲区使用直接分配
} VNVME_MEMORY_POOL, *PVNVME_MEMORY_POOL;

// 内存分配接口
PVOID VNvmeAllocateMemory(
    PVNVME_MEMORY_POOL Pool,
    SIZE_T Size,
    ULONG Tag)
{
    if (Size <= 4096) {
        return ExAllocateFromLookasideListEx(&Pool->SmallBufferPool);
    } else if (Size <= 65536) {
        return ExAllocateFromLookasideListEx(&Pool->MediumBufferPool);
    } else {
        return ExAllocatePoolWithTag(NonPagedPoolNx, Size, Tag);
    }
}
```

## DPC 队列处理

### 完成处理 DPC

```c
KDEFERRED_ROUTINE CompletionDpcRoutine;

VOID CompletionDpcRoutine(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
{
    PVNVME_CONTROLLER ctrl = (PVNVME_CONTROLLER)DeferredContext;
    PVNVME_QUEUE cq = (PVNVME_QUEUE)SystemArgument1;
    
    // 处理完成队列中的所有条目
    while (HasPendingCompletions(cq)) {
        NVME_CQE cqe;
        DequeueCompletion(cq, &cqe);
        
        // 完成对应的 I/O 请求
        WDFREQUEST request = FindRequestByCid(ctrl, cqe.CID);
        if (request) {
            CompleteRequest(request, &cqe);
        }
    }
}
```

## I/O 请求处理流程

```
WDFREQUEST
    │
    ▼
┌───────────────────┐
│ EvtIoDeviceControl│ ←─ IOCTL 请求
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│  请求类型分发     │
└─────────┬─────────┘
          │
    ┌─────┴─────┐
    ▼           ▼
 SCSI/ATA    NVMe Pass-through
    │           │
    ▼           ▼
┌───────────────────┐
│ 转换为 NVMe 命令  │
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ 提交到 SQ        │
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ 处理命令 (同步)   │
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ 生成 CQE         │
└─────────┬─────────┘
          │
          ▼
┌───────────────────┐
│ 完成请求         │
└───────────────────┘
```

## WDF 对象层次结构

WDF 框架采用对象层次模型管理驱动资源，父对象销毁时自动清理子对象。

### 对象层次图

```
WDFDRIVER (根对象)
    │
    ├── WDFDEVICE (FDO - 功能设备对象)
    │       │
    │       ├── WDFQUEUE (默认 I/O 队列)
    │       │       │
    │       │       └── WDFREQUEST (I/O 请求)
    │       │
    │       ├── WDFQUEUE (Admin 命令队列)
    │       │
    │       ├── WDFINTERRUPT (中断对象 - 虚拟设备可选)
    │       │
    │       ├── WDFTIMER (定时器对象)
    │       │       │
    │       │       ├── 心跳定时器 (SMART 更新)
    │       │       └── 空闲检测定时器
    │       │
    │       ├── WDFWORKITEM (工作项)
    │       │
    │       ├── WDFSPINLOCK (自旋锁)
    │       │
    │       ├── WDFWAITLOCK (等待锁)
    │       │
    │       ├── WDFMEMORY (内存对象)
    │       │
    │       └── WDFFILEOBJECT (文件对象 - 每个用户态句柄)
    │
    └── WDFDEVICE (Bus FDO - 总线设备)
            │
            ├── WDFCHILDLIST (子设备列表)
            │
            └── WDFDEVICE (PDO - 物理设备对象)
                    │
                    └── [关联的功能驱动 FDO]
```

### 对象生命周期管理

```c
// 设备上下文结构
typedef struct _DEVICE_CONTEXT {
    WDFDEVICE           Device;
    WDFQUEUE            DefaultQueue;
    WDFQUEUE            AdminQueue;
    WDFTIMER            HeartbeatTimer;
    WDFSPINLOCK         ControllerLock;
    WDFWAITLOCK         IoLock;
    
    // NVMe 控制器状态
    VNVME_CONTROLLER    Controller;
    
    // 后端存储
    VNVME_BACKEND       Backend;
    
    // 统计信息
    VNVME_STATISTICS    Stats;
    
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

// 对象创建示例
NTSTATUS CreateDeviceObjects(WDFDEVICE Device)
{
    PDEVICE_CONTEXT ctx = GetDeviceContext(Device);
    WDF_OBJECT_ATTRIBUTES objAttr;
    NTSTATUS status;
    
    // 创建自旋锁 (父对象 = Device)
    WDF_OBJECT_ATTRIBUTES_INIT(&objAttr);
    objAttr.ParentObject = Device;
    
    status = WdfSpinLockCreate(&objAttr, &ctx->ControllerLock);
    if (!NT_SUCCESS(status)) return status;
    
    // 创建等待锁 (父对象 = Device)
    status = WdfWaitLockCreate(&objAttr, &ctx->IoLock);
    if (!NT_SUCCESS(status)) return status;
    
    // 创建定时器 (父对象 = Device)
    WDF_TIMER_CONFIG timerConfig;
    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig,
        EvtHeartbeatTimer,
        1000    // 1秒周期
    );
    
    status = WdfTimerCreate(&timerConfig, &objAttr, &ctx->HeartbeatTimer);
    if (!NT_SUCCESS(status)) return status;
    
    // 当 Device 销毁时，所有子对象自动清理
    return STATUS_SUCCESS;
}
```

## 电源状态机

### 设备电源状态转换图

```
                        ┌──────────────────┐
                        │   DriverEntry    │
                        └────────┬─────────┘
                                 │
                                 ▼
                        ┌──────────────────┐
                        │ EvtDeviceAdd     │
                        └────────┬─────────┘
                                 │
                                 ▼
                        ┌──────────────────┐
         ┌─────────────│ PrepareHardware  │
         │              └────────┬─────────┘
         │                       │
         │                       ▼
         │              ┌──────────────────┐
         │    ┌────────│     D0Entry      │◄─────────┐
         │    │         └────────┬─────────┘          │
         │    │                  │                    │
         │    │                  ▼                    │
         │    │         ┌──────────────────┐          │
         │    │         │  设备运行 (D0)    │          │
         │    │         │  处理 I/O 请求   │          │
         │    │         └────────┬─────────┘          │
         │    │                  │                    │
         │    │    ┌─────────────┼─────────────┐      │
         │    │    │             │             │      │
         │    │    ▼             ▼             ▼      │
         │    │ ┌──────┐    ┌──────┐    ┌──────┐      │
         │    │ │ Idle │    │ S3   │    │ S4   │      │
         │    │ │ D3   │    │ D3   │    │ D3   │      │
         │    │ └──┬───┘    └──┬───┘    └──┬───┘      │
         │    │    │           │           │          │
         │    │    ▼           ▼           ▼          │
         │    │ ┌──────────────────────────────┐      │
         │    │ │          D0Exit              │      │
         │    │ │  刷新缓存，保存状态           │      │
         │    │ └──────────────┬───────────────┘      │
         │    │                │                      │
         │    │                ▼                      │
         │    │ ┌──────────────────────────────┐      │
         │    │ │       设备睡眠 (D3)          │──────┘
         │    │ │                              │ (唤醒)
         │    │ └──────────────────────────────┘
         │    │
         │    │ (设备移除)
         │    │
         │    ▼
         │ ┌──────────────────┐
         │ │  ReleaseHardware │
         │ └────────┬─────────┘
         │          │
         │          ▼
         └────► ┌──────────────────┐
                │  设备已卸载      │
                └──────────────────┘
```

### 电源回调顺序

**进入低功耗 (D0 → D3)**:
1. `EvtDeviceD0ExitPreInterruptsDisabled` (可选)
2. 中断断开
3. `EvtDeviceD0Exit` - **主要清理点**
4. 队列自动停止 (如果 PowerManaged = TRUE)

**恢复全功率 (D3 → D0)**:
1. `EvtDeviceD0Entry` - **主要初始化点**
2. 中断连接
3. `EvtDeviceD0EntryPostInterruptsEnabled` (可选)
4. 队列自动启动 (如果 PowerManaged = TRUE)

```c
// 电源回调注册
WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

pnpPowerCallbacks.EvtDevicePrepareHardware = EvtDevicePrepareHardware;
pnpPowerCallbacks.EvtDeviceReleaseHardware = EvtDeviceReleaseHardware;
pnpPowerCallbacks.EvtDeviceD0Entry = EvtDeviceD0Entry;
pnpPowerCallbacks.EvtDeviceD0Exit = EvtDeviceD0Exit;
pnpPowerCallbacks.EvtDeviceSelfManagedIoInit = EvtDeviceSelfManagedIoInit;
pnpPowerCallbacks.EvtDeviceSelfManagedIoCleanup = EvtDeviceSelfManagedIoCleanup;

WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
```

## I/O 请求取消处理

### 取消机制概述

用户可能在 I/O 完成前取消请求，驱动必须正确处理以避免资源泄漏和死锁。

### WDF 取消回调

```c
// 为请求设置取消回调
VOID EvtIoRead(
    WDFQUEUE Queue,
    WDFREQUEST Request,
    size_t Length)
{
    PDEVICE_CONTEXT ctx = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    NTSTATUS status;
    
    // 创建 I/O 上下文
    PREQUEST_CONTEXT reqCtx = GetRequestContext(Request);
    reqCtx->Request = Request;
    reqCtx->StartTime = KeQueryPerformanceCounter(NULL);
    
    // 标记请求为可取消
    status = WdfRequestMarkCancelableEx(Request, EvtRequestCancel);
    if (!NT_SUCCESS(status)) {
        // 请求已被取消
        WdfRequestComplete(Request, status);
        return;
    }
    
    // 将请求排入内部队列
    InsertPendingRequest(ctx, reqCtx);
    
    // 开始异步处理
    StartAsyncRead(ctx, reqCtx);
}

// 取消回调
VOID EvtRequestCancel(WDFREQUEST Request)
{
    PREQUEST_CONTEXT reqCtx = GetRequestContext(Request);
    PDEVICE_CONTEXT ctx = GetDeviceContext(
        WdfIoQueueGetDevice(WdfRequestGetIoQueue(Request)));
    
    TraceEvents(TRACE_LEVEL_WARNING, DBG_IO,
        "Request %p cancelled", Request);
    
    // 从待处理队列移除
    if (RemovePendingRequest(ctx, reqCtx)) {
        // 请求尚未开始处理，直接完成
        WdfRequestComplete(Request, STATUS_CANCELLED);
    }
    // 如果请求正在处理中，处理完成时会检查取消状态
}
```

### 安全完成取消的请求

```c
VOID CompleteAsyncRequest(PREQUEST_CONTEXT reqCtx, NTSTATUS Status)
{
    WDFREQUEST request = reqCtx->Request;
    NTSTATUS unmarkStatus;
    
    // 尝试取消可取消状态
    unmarkStatus = WdfRequestUnmarkCancelable(request);
    
    if (unmarkStatus == STATUS_CANCELLED) {
        // 请求正在被取消回调处理
        // 不要在这里完成请求！取消回调会处理
        return;
    }
    
    // 正常完成请求
    WdfRequestCompleteWithInformation(request, Status, reqCtx->BytesTransferred);
}
```

### 取消安全队列

使用 WDF 手动分发队列实现取消安全：

```c
NTSTATUS CreateCancelSafeQueue(PDEVICE_CONTEXT ctx)
{
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_OBJECT_ATTRIBUTES queueAttr;
    
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    
    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttr);
    queueAttr.ParentObject = ctx->Device;
    
    return WdfIoQueueCreate(
        ctx->Device,
        &queueConfig,
        &queueAttr,
        &ctx->PendingQueue
    );
}

// 从队列获取请求（自动处理取消）
NTSTATUS GetNextPendingRequest(PDEVICE_CONTEXT ctx, WDFREQUEST *pRequest)
{
    return WdfIoQueueRetrieveNextRequest(ctx->PendingQueue, pRequest);
}

// 转发请求到待处理队列
VOID QueuePendingRequest(PDEVICE_CONTEXT ctx, WDFREQUEST Request)
{
    NTSTATUS status = WdfRequestForwardToIoQueue(Request, ctx->PendingQueue);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
}
```

### 超时与取消结合

```c
typedef struct _REQUEST_CONTEXT {
    WDFREQUEST      Request;
    WDFTIMER        TimeoutTimer;
    LONG            Completed;      // 原子完成标志
    LARGE_INTEGER   StartTime;
    
} REQUEST_CONTEXT, *PREQUEST_CONTEXT;

// 创建带超时的请求
NTSTATUS StartRequestWithTimeout(PREQUEST_CONTEXT reqCtx, ULONG TimeoutMs)
{
    WDF_TIMER_CONFIG timerConfig;
    WDF_OBJECT_ATTRIBUTES timerAttr;
    
    WDF_TIMER_CONFIG_INIT(&timerConfig, EvtRequestTimeout);
    timerConfig.AutomaticSerialization = FALSE;
    
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttr);
    timerAttr.ParentObject = reqCtx->Request;
    
    NTSTATUS status = WdfTimerCreate(
        &timerConfig, 
        &timerAttr, 
        &reqCtx->TimeoutTimer);
    
    if (NT_SUCCESS(status)) {
        // 启动超时定时器
        WdfTimerStart(reqCtx->TimeoutTimer, 
            WDF_REL_TIMEOUT_IN_MS(TimeoutMs));
    }
    
    return status;
}

// 超时回调
VOID EvtRequestTimeout(WDFTIMER Timer)
{
    WDFREQUEST request = (WDFREQUEST)WdfTimerGetParentObject(Timer);
    PREQUEST_CONTEXT reqCtx = GetRequestContext(request);
    
    // 原子地标记为已完成
    if (InterlockedCompareExchange(&reqCtx->Completed, 1, 0) == 0) {
        // 取消请求
        if (NT_SUCCESS(WdfRequestUnmarkCancelable(request))) {
            WdfRequestComplete(request, STATUS_IO_TIMEOUT);
        }
    }
}

// 正常完成时
VOID CompleteRequestNormal(PREQUEST_CONTEXT reqCtx, NTSTATUS Status)
{
    // 停止超时定时器
    WdfTimerStop(reqCtx->TimeoutTimer, FALSE);
    
    // 原子地标记为已完成
    if (InterlockedCompareExchange(&reqCtx->Completed, 1, 0) == 0) {
        if (NT_SUCCESS(WdfRequestUnmarkCancelable(reqCtx->Request))) {
            WdfRequestComplete(reqCtx->Request, Status);
        }
    }
}
```

### 取消处理最佳实践

1. **始终检查 WdfRequestUnmarkCancelable 返回值**
   - STATUS_CANCELLED 表示取消回调正在运行

2. **使用原子操作防止双重完成**
   - InterlockedCompareExchange 确保只完成一次

3. **清理与完成分离**
   - 完成请求后不要访问请求上下文

4. **正确处理队列停止**
   ```c
   // 停止队列并等待进行中的请求
   WdfIoQueueStop(ctx->IoQueue, EvtQueueStopComplete, ctx);
   
   VOID EvtQueueStopComplete(WDFQUEUE Queue, WDFCONTEXT Context)
   {
       // 所有请求已完成或取消
   }
   ```

5. **驱动卸载时清理**
   ```c
   // 清空待处理队列
   WdfIoQueuePurgeSynchronously(ctx->PendingQueue);
   ```

