# NVMe 控制器仿真

> ⚠️ **架构更新说明 (v2)**
> 
> 本文档基于 v1 全内核态架构编写，描述了 `vnvme_emu.sys` 独立驱动。
> 
> **v2 架构的变化**：
> - `vnvme_emu.sys` 已合并到 `vnvme.sys` 单一驱动
> - 命令处理已移至用户态 `vnvme-server.exe`
> - 内核只负责 Doorbell 轮询和 PRP 解析
> 
> 本文档中关于 **NVMe 寄存器定义、数据结构、寄存器初始化** 的内容仍然适用。
> 
> 请优先参考：
> - [architecture-v2.md](architecture-v2.md) - 整体架构
> - [core-mechanisms.md](core-mechanisms.md) - Doorbell 轮询和完成处理

本文档详细说明 NVMe 控制器仿真驱动 (vnvme_emu.sys) 的设计和实现。

## 概述

vnvme_emu.sys 是本项目的核心，负责完整仿真 NVMe 控制器的行为，使 Windows 原生 NVMe 驱动 (stornvme.sys) 能够正常工作。

### 仿真范围

| 组件 | 仿真内容 |
|------|---------|
| **控制器寄存器** | CAP, VS, CC, CSTS, AQA, ASQ, ACQ, Doorbell 等 |
| **Admin Queue** | 处理 Admin Submission/Completion Queue |
| **I/O Queue** | 支持多个 I/O Queue 对 |
| **Admin 命令** | Identify, Create/Delete Queue, Get/Set Features |
| **I/O 命令** | Read, Write, Flush, Dataset Management |
| **中断** | MSI-X 中断仿真 |

---

## 控制器寄存器

### 寄存器映射

NVMe 控制器寄存器位于 BAR0 空间，布局如下：

```
偏移      名称        大小   R/W   描述
────────────────────────────────────────────────────────────────────
0x0000    CAP         8      RO    Controller Capabilities
0x0008    VS          4      RO    Version
0x000C    INTMS       4      RW    Interrupt Mask Set
0x0010    INTMC       4      RW    Interrupt Mask Clear
0x0014    CC          4      RW    Controller Configuration
0x0018    Reserved    4      --    保留
0x001C    CSTS        4      RO    Controller Status
0x0020    NSSR        4      RW    NVM Subsystem Reset
0x0024    AQA         4      RW    Admin Queue Attributes
0x0028    ASQ         8      RW    Admin Submission Queue Base Address
0x0030    ACQ         8      RW    Admin Completion Queue Base Address
0x0038    CMBLOC      4      RO    Controller Memory Buffer Location
0x003C    CMBSZ       4      RO    Controller Memory Buffer Size
0x0040    BPINFO      4      RO    Boot Partition Information
0x0044    BPRSEL      4      RW    Boot Partition Read Select
0x0048    BPMBL       8      RW    Boot Partition Memory Buffer Location
0x0050    CMBMSC      8      RW    Controller Memory Buffer Memory Space Control
0x0058    CMBSTS      4      RO    Controller Memory Buffer Status
0x005C    CMBEBS      4      RO    Controller Memory Buffer Elasticity Buffer Size
0x0060    CMBSWTP     4      RO    Controller Memory Buffer Sustained Write Throughput
0x0064    NSSD        4      RW    NVM Subsystem Shutdown
0x0068    CRTO        4      RO    Controller Ready Timeouts
...
0x1000    SQ0TDBL     4      WO    Submission Queue 0 Tail Doorbell
0x1004    CQ0HDBL     4      WO    Completion Queue 0 Head Doorbell
0x1008    SQ1TDBL     4      WO    Submission Queue 1 Tail Doorbell
0x100C    CQ1HDBL     4      WO    Completion Queue 1 Head Doorbell
...       (每个 Queue 占用 4 字节，间隔由 CAP.DSTRD 定义)
```

### 寄存器数据结构

```c
//
// NVMe 控制器寄存器结构
//

// CAP - Controller Capabilities (Offset 0x00, 64-bit)
typedef union _NVME_CAP {
    struct {
        ULONG64 MQES   : 16;  // Maximum Queue Entries Supported (0-based)
        ULONG64 CQR    : 1;   // Contiguous Queues Required
        ULONG64 AMS    : 2;   // Arbitration Mechanism Supported
        ULONG64 Rsvd   : 5;
        ULONG64 TO     : 8;   // Timeout (in 500ms units)
        ULONG64 DSTRD  : 4;   // Doorbell Stride (2^(2+DSTRD) bytes)
        ULONG64 NSSRS  : 1;   // NVM Subsystem Reset Supported
        ULONG64 CSS    : 8;   // Command Sets Supported
        ULONG64 BPS    : 1;   // Boot Partition Support
        ULONG64 CPS    : 2;   // Controller Power Scope
        ULONG64 MPSMIN : 4;   // Memory Page Size Minimum (2^(12+MPSMIN))
        ULONG64 MPSMAX : 4;   // Memory Page Size Maximum (2^(12+MPSMAX))
        ULONG64 PMRS   : 1;   // Persistent Memory Region Supported
        ULONG64 CMBS   : 1;   // Controller Memory Buffer Supported
        ULONG64 NSSS   : 1;   // NVM Subsystem Shutdown Supported
        ULONG64 CRMS   : 2;   // Controller Ready Modes Supported
        ULONG64 Rsvd2  : 3;
    };
    ULONG64 AsUlonglong;
} NVME_CAP, *PNVME_CAP;

// VS - Version (Offset 0x08, 32-bit)
typedef union _NVME_VS {
    struct {
        ULONG TER : 8;   // Tertiary Version
        ULONG MNR : 8;   // Minor Version
        ULONG MJR : 16;  // Major Version
    };
    ULONG AsUlong;
} NVME_VS, *PNVME_VS;

// CC - Controller Configuration (Offset 0x14, 32-bit)
typedef union _NVME_CC {
    struct {
        ULONG EN     : 1;   // Enable
        ULONG Rsvd   : 3;
        ULONG CSS    : 3;   // I/O Command Set Selected
        ULONG MPS    : 4;   // Memory Page Size (2^(12+MPS))
        ULONG AMS    : 3;   // Arbitration Mechanism Selected
        ULONG SHN    : 2;   // Shutdown Notification
        ULONG IOSQES : 4;   // I/O Submission Queue Entry Size (2^n)
        ULONG IOCQES : 4;   // I/O Completion Queue Entry Size (2^n)
        ULONG CRIME  : 1;   // Controller Ready Independent of Media Enable
        ULONG Rsvd2  : 7;
    };
    ULONG AsUlong;
} NVME_CC, *PNVME_CC;

// CSTS - Controller Status (Offset 0x1C, 32-bit)
typedef union _NVME_CSTS {
    struct {
        ULONG RDY   : 1;   // Ready
        ULONG CFS   : 1;   // Controller Fatal Status
        ULONG SHST  : 2;   // Shutdown Status
        ULONG NSSRO : 1;   // NVM Subsystem Reset Occurred
        ULONG PP    : 1;   // Processing Paused
        ULONG ST    : 1;   // Shutdown Type
        ULONG Rsvd  : 25;
    };
    ULONG AsUlong;
} NVME_CSTS, *PNVME_CSTS;

// AQA - Admin Queue Attributes (Offset 0x24, 32-bit)
typedef union _NVME_AQA {
    struct {
        ULONG ASQS : 12;   // Admin Submission Queue Size (0-based)
        ULONG Rsvd : 4;
        ULONG ACQS : 12;   // Admin Completion Queue Size (0-based)
        ULONG Rsvd2: 4;
    };
    ULONG AsUlong;
} NVME_AQA, *PNVME_AQA;
```

### 寄存器初始化

```c
//
// 初始化 NVMe 控制器寄存器
//
VOID VnvmeInitControllerRegisters(
    _Inout_ PVNVME_CONTROLLER Controller)
{
    // CAP: 控制器能力
    Controller->Regs.CAP.MQES = 4095;      // 最多 4096 条目
    Controller->Regs.CAP.CQR = 1;          // 需要连续队列
    Controller->Regs.CAP.AMS = 0;          // 仅支持 Round Robin
    Controller->Regs.CAP.TO = 40;          // 20 秒超时 (40 * 500ms)
    Controller->Regs.CAP.DSTRD = 0;        // Doorbell 步长 4 字节
    Controller->Regs.CAP.NSSRS = 0;        // 不支持子系统复位
    Controller->Regs.CAP.CSS = 1;          // 支持 NVM Command Set
    Controller->Regs.CAP.MPSMIN = 0;       // 最小页大小 4KB
    Controller->Regs.CAP.MPSMAX = 0;       // 最大页大小 4KB
    
    // VS: 版本 (1.4.0)
    Controller->Regs.VS.MJR = 1;
    Controller->Regs.VS.MNR = 4;
    Controller->Regs.VS.TER = 0;
    
    // CC: 控制器配置 (初始禁用)
    Controller->Regs.CC.AsUlong = 0;
    
    // CSTS: 控制器状态 (未就绪)
    Controller->Regs.CSTS.AsUlong = 0;
    
    // AQA, ASQ, ACQ: 由主机配置
    Controller->Regs.AQA.AsUlong = 0;
    Controller->Regs.ASQ = 0;
    Controller->Regs.ACQ = 0;
}
```

---

## 控制器状态机

### 状态转换

```
                         ┌─────────────┐
                         │   Disabled  │ ◄───────────────┐
                         │   (初始)    │                 │
                         └──────┬──────┘                 │
                                │                        │
                                │ CC.EN = 1 写入         │
                                ▼                        │
                         ┌─────────────┐                 │
                         │  Enabling   │                 │
                         │             │                 │ CC.EN = 0
                         └──────┬──────┘                 │
                                │                        │
                                │ 初始化完成             │
                                │ (配置 Admin Queue)     │
                                ▼                        │
                         ┌─────────────┐                 │
                ┌───────►│   Ready     │─────────────────┘
                │        │  CSTS.RDY=1 │
                │        └──────┬──────┘
                │               │
                │               │ CC.SHN != 0
                │               ▼
                │        ┌─────────────┐
                │        │ Shutting    │
                │        │    Down     │
                │        └──────┬──────┘
                │               │
                │               │ 关机完成
                │               ▼
                │        ┌─────────────┐
                └────────│  Shutdown   │
                         │ CSTS.SHST=2 │
                         └─────────────┘
```

### 状态处理代码

```c
//
// 处理 CC (Controller Configuration) 寄存器写入
//
VOID VnvmeHandleCcWrite(
    _Inout_ PVNVME_CONTROLLER Controller,
    _In_ ULONG NewValue)
{
    NVME_CC newCC;
    NVME_CC oldCC;
    
    newCC.AsUlong = NewValue;
    oldCC = Controller->Regs.CC;
    
    // 检查 Enable 位变化
    if (!oldCC.EN && newCC.EN) {
        // 启用控制器
        VnvmeEnableController(Controller);
    }
    else if (oldCC.EN && !newCC.EN) {
        // 禁用控制器
        VnvmeDisableController(Controller);
    }
    
    // 检查关机通知
    if (newCC.SHN != 0 && oldCC.SHN == 0) {
        VnvmeInitiateShutdown(Controller, newCC.SHN);
    }
    
    // 保存配置
    Controller->Regs.CC = newCC;
}

//
// 启用控制器
//
VOID VnvmeEnableController(
    _Inout_ PVNVME_CONTROLLER Controller)
{
    // 验证 Admin Queue 配置
    if (Controller->Regs.ASQ == 0 || Controller->Regs.ACQ == 0) {
        Controller->Regs.CSTS.CFS = 1;  // Fatal error
        return;
    }
    
    // 验证 AQA
    if (Controller->Regs.AQA.ASQS == 0 || Controller->Regs.AQA.ACQS == 0) {
        Controller->Regs.CSTS.CFS = 1;
        return;
    }
    
    // 初始化 Admin Queue
    NTSTATUS status = VnvmeInitAdminQueue(Controller);
    if (!NT_SUCCESS(status)) {
        Controller->Regs.CSTS.CFS = 1;
        return;
    }
    
    // 设置就绪状态
    Controller->Regs.CSTS.RDY = 1;
    Controller->State = VNVME_CONTROLLER_READY;
}
```

---

## 寄存器访问处理

### MMIO 访问拦截

stornvme.sys 通过内存映射 I/O 访问寄存器。我们需要拦截这些访问：

```c
//
// 寄存器读取处理
//
ULONG64 VnvmeRegisterRead(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _In_ ULONG Size)
{
    PVOID regBase = Controller->RegBase;
    ULONG64 value = 0;
    
    switch (Offset) {
    case NVME_REG_CAP:
        value = Controller->Regs.CAP.AsUlonglong;
        break;
        
    case NVME_REG_VS:
        value = Controller->Regs.VS.AsUlong;
        break;
        
    case NVME_REG_INTMS:
        value = Controller->Regs.INTMS;
        break;
        
    case NVME_REG_INTMC:
        value = Controller->Regs.INTMC;
        break;
        
    case NVME_REG_CC:
        value = Controller->Regs.CC.AsUlong;
        break;
        
    case NVME_REG_CSTS:
        value = Controller->Regs.CSTS.AsUlong;
        break;
        
    case NVME_REG_AQA:
        value = Controller->Regs.AQA.AsUlong;
        break;
        
    case NVME_REG_ASQ:
        value = Controller->Regs.ASQ;
        break;
        
    case NVME_REG_ACQ:
        value = Controller->Regs.ACQ;
        break;
        
    default:
        // Doorbell 或未知寄存器
        if (Offset >= 0x1000) {
            value = VnvmeDoorbellRead(Controller, Offset);
        }
        break;
    }
    
    return value;
}

//
// 寄存器写入处理
//
VOID VnvmeRegisterWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _In_ ULONG64 Value,
    _In_ ULONG Size)
{
    switch (Offset) {
    case NVME_REG_INTMS:
        Controller->Regs.INTMS |= (ULONG)Value;
        break;
        
    case NVME_REG_INTMC:
        Controller->Regs.INTMS &= ~(ULONG)Value;
        break;
        
    case NVME_REG_CC:
        VnvmeHandleCcWrite(Controller, (ULONG)Value);
        break;
        
    case NVME_REG_AQA:
        if (!Controller->Regs.CC.EN) {
            Controller->Regs.AQA.AsUlong = (ULONG)Value;
        }
        break;
        
    case NVME_REG_ASQ:
        if (!Controller->Regs.CC.EN) {
            Controller->Regs.ASQ = Value;
        }
        break;
        
    case NVME_REG_ACQ:
        if (!Controller->Regs.CC.EN) {
            Controller->Regs.ACQ = Value;
        }
        break;
        
    default:
        // Doorbell 寄存器
        if (Offset >= 0x1000) {
            VnvmeDoorbellWrite(Controller, Offset, (ULONG)Value);
        }
        break;
    }
}
```

---

## Doorbell 处理

### Doorbell 布局

```
Doorbell 寄存器偏移计算:

SQ y Tail Doorbell = 0x1000 + (2y * (4 << CAP.DSTRD))
CQ y Head Doorbell = 0x1000 + ((2y + 1) * (4 << CAP.DSTRD))

当 CAP.DSTRD = 0 时:
SQ 0 Tail: 0x1000
CQ 0 Head: 0x1004
SQ 1 Tail: 0x1008
CQ 1 Head: 0x100C
...
```

### Doorbell 写入处理

```c
//
// Doorbell 写入处理
//
VOID VnvmeDoorbellWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    ULONG stride = 4 << Controller->Regs.CAP.DSTRD;
    ULONG doorbellIndex = (Offset - 0x1000) / stride;
    ULONG queueId = doorbellIndex / 2;
    BOOLEAN isSQ = (doorbellIndex % 2) == 0;
    
    if (isSQ) {
        // Submission Queue Tail Doorbell
        VnvmeSqTailDoorbellWrite(Controller, queueId, Value);
    } else {
        // Completion Queue Head Doorbell
        VnvmeCqHeadDoorbellWrite(Controller, queueId, Value);
    }
}

//
// SQ Tail Doorbell 写入 - 表示有新命令提交
//
VOID VnvmeSqTailDoorbellWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG QueueId,
    _In_ ULONG NewTail)
{
    PVNVME_SUBMISSION_QUEUE sq;
    
    if (QueueId == 0) {
        sq = &Controller->AdminSQ;
    } else {
        sq = VnvmeFindIoSQ(Controller, QueueId);
    }
    
    if (!sq) {
        return;  // 无效队列
    }
    
    // 更新 Tail 指针
    ULONG oldTail = sq->Tail;
    sq->Tail = NewTail;
    
    // 处理新提交的命令
    while (sq->Head != sq->Tail) {
        VnvmeProcessCommand(Controller, sq);
        sq->Head = (sq->Head + 1) % sq->Size;
    }
}

//
// CQ Head Doorbell 写入 - 表示完成条目已被处理
//
VOID VnvmeCqHeadDoorbellWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG QueueId,
    _In_ ULONG NewHead)
{
    PVNVME_COMPLETION_QUEUE cq;
    
    if (QueueId == 0) {
        cq = &Controller->AdminCQ;
    } else {
        cq = VnvmeFindIoCQ(Controller, QueueId);
    }
    
    if (!cq) {
        return;
    }
    
    // 更新 Head 指针
    cq->Head = NewHead;
    
    // 如果还有待处理的完成，可能需要再次触发中断
    if (cq->Head != cq->Tail) {
        VnvmeTriggerInterrupt(Controller, cq->Vector);
    }
}
```

---

## 控制器上下文

```c
//
// NVMe 控制器上下文
//
typedef struct _VNVME_CONTROLLER {
    //
    // 设备对象
    //
    PDEVICE_OBJECT      DeviceObject;
    PDEVICE_OBJECT      PhysicalDeviceObject;
    PDEVICE_OBJECT      LowerDevice;
    
    //
    // 控制器状态
    //
    VNVME_CONTROLLER_STATE State;
    
    //
    // 寄存器
    //
    struct {
        NVME_CAP        CAP;
        NVME_VS         VS;
        ULONG           INTMS;
        ULONG           INTMC;
        NVME_CC         CC;
        NVME_CSTS       CSTS;
        NVME_AQA        AQA;
        ULONG64         ASQ;
        ULONG64         ACQ;
    } Regs;
    
    //
    // BAR0 映射
    //
    PVOID               RegBase;        // 寄存器虚拟地址
    PHYSICAL_ADDRESS    RegPhysAddr;    // 寄存器物理地址
    SIZE_T              RegSize;        // 寄存器空间大小
    
    //
    // Admin Queue
    //
    VNVME_SUBMISSION_QUEUE AdminSQ;
    VNVME_COMPLETION_QUEUE AdminCQ;
    
    //
    // I/O Queue 管理
    //
    LIST_ENTRY          IoSqList;
    LIST_ENTRY          IoCqList;
    KSPIN_LOCK          QueueLock;
    ULONG               MaxIoQueues;
    
    //
    // 命名空间
    //
    LIST_ENTRY          NamespaceList;
    ULONG               NamespaceCount;
    
    //
    // 后端存储
    //
    PVNVME_BACKEND      Backend;
    
    //
    // 中断
    //
    ULONG               MsixVectors;
    PKINTERRUPT         Interrupt[VNVME_MAX_MSIX_VECTORS];
    
    //
    // 统计
    //
    VNVME_STATS         Stats;
    
} VNVME_CONTROLLER, *PVNVME_CONTROLLER;
```

---

## 与 stornvme.sys 交互

### stornvme 初始化序列

```
1. stornvme 读取 CAP 寄存器
   └─► 获取队列大小限制、超时值等

2. stornvme 读取 VS 寄存器
   └─► 获取 NVMe 版本

3. stornvme 配置 Admin Queue:
   └─► 写入 AQA (队列大小)
   └─► 写入 ASQ (SQ 基地址)
   └─► 写入 ACQ (CQ 基地址)

4. stornvme 写入 CC 寄存器 (EN=1)
   └─► 启用控制器

5. stornvme 轮询 CSTS.RDY
   └─► 等待控制器就绪

6. stornvme 发送 Identify Controller 命令
   └─► 获取控制器信息

7. stornvme 发送 Identify Namespace List
   └─► 获取命名空间列表

8. stornvme 为每个命名空间发送 Identify Namespace
   └─► 获取命名空间信息

9. stornvme 发送 Set Features (Number of Queues)
   └─► 配置 I/O 队列数量

10. stornvme 创建 I/O CQ 和 SQ
    └─► Create I/O Completion Queue
    └─► Create I/O Submission Queue
```

### 验证 stornvme 加载

```powershell
# 检查设备是否被 stornvme 驱动
Get-PnpDevice | Where-Object { $_.Class -eq "SCSIAdapter" -and $_.FriendlyName -like "*NVMe*" }

# 查看驱动详情
pnputil /enum-drivers /class SCSIAdapter

# 检查 NVMe 控制器
Get-PhysicalDisk | Where-Object BusType -eq "NVMe"
```

---

## 参考资源

- [NVM Express Base Specification 2.0](https://nvmexpress.org/specifications/)
- [QEMU NVMe Controller](https://github.com/qemu/qemu/blob/master/hw/nvme/ctrl.c)
- [Linux NVMe Driver](https://github.com/torvalds/linux/blob/master/drivers/nvme/host/core.c)
