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
