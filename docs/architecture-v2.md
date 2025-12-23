# 系统架构设计 (v2 - 混合架构)

## 修订历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2025-12-23 | 初始全内核态设计 |
| v2 | 2025-12-23 | 采用混合用户态/内核态架构 |
| v2.1 | 2025-12-23 | 实现双模式架构 - 支持内核态和用户态命令处理切换 |

---

## 实现状态

> **✅ 双模式架构已实现**
> 
> 通过 `VNVME_DEFAULT_CMD_MODE` 宏切换命令处理模式：
> - `VNVME_CMD_MODE_KERNEL`: 内核态处理 (低延迟，备用方案)
> - `VNVME_CMD_MODE_USER`: 用户态处理 (默认，更灵活)
>
> 核心组件状态：
> - ✅ 内核驱动 (`vnvme.sys`): FDO/PDO、BAR0、PCIe、Doorbell、命令处理
> - ✅ 用户态服务 (`vnvme-server.exe`): 命令处理器、存储后端
> - ✅ 命令行工具 (`vnvmectl.exe`): 基础管理命令
> - ⬜ 测试验证: 待在测试环境中验证

---

## 相关文档

| 文档 | 说明 |
|------|------|
| [core-mechanisms.md](core-mechanisms.md) | 核心机制详细实现 (Doorbell 轮询、共享内存、PRP 解析) |
| [architecture-analysis.md](architecture-analysis.md) | 架构决策分析和问题修复记录 |
| [pcie-emulation.md](pcie-emulation.md) | PCIe 配置空间仿真细节 |
| [nvme-controller.md](nvme-controller.md) | NVMe 寄存器和命令定义 |

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
│  │    │ 控制块 │ 提交环 │ 完成环 │ 数据缓冲池 │ 统计/日志        ││  │
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
│  │  │ • 高精度定时器 (10μs-1000μs 自适应)                        │  │  │
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

#### 1.0 驱动内部架构

vnvme.sys 在物理上是单一驱动，但在逻辑上分为两层：

```
┌─────────────────────────────────────────────────────────────────────┐
│                         vnvme.sys 内部架构                          │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────────┐ │
│  │                     vnvme.c (驱动入口 + IRP 路由)                │ │
│  │                                                                 │ │
│  │   NTSTATUS VnvmeDispatchPnp(DeviceObject, Irp)                  │ │
│  │   {                                                             │ │
│  │       if (IsDeviceFdo(DeviceObject))                            │ │
│  │           return VnvmeFdoPnp(...);   // → FDO 层                │ │
│  │       else                                                      │ │
│  │           return VnvmePdoPnp(...);   // → PDO 层                │ │
│  │   }                                                             │ │
│  └─────────────────────────────────────────────────────────────────┘ │
│                              │                                       │
│               ┌──────────────┴──────────────┐                        │
│               ▼                             ▼                        │
│  ┌─────────────────────────┐   ┌─────────────────────────────────┐  │
│  │       FDO 层            │   │          PDO 层                  │  │
│  │   (总线功能)            │   │      (NVMe 仿真)                 │  │
│  │                         │   │                                 │  │
│  │ • bus.c     子设备枚举  │   │ • pdo.c       PDO PnP/资源      │  │
│  │ • ctrl_dev  控制设备   │──▶│ • pcie_config PCIe 配置空间     │  │
│  │ • shm.c      共享内存   │   │ • bar0.c      BAR0/寄存器       │  │
│  │                         │◀─▶│ • doorbell.c  轮询引擎          │  │
│  │                         │   │ • queue.c     队列管理          │  │
│  │                         │   │ • prp.c       PRP 解析          │  │
│  └─────────────────────────┘   └─────────────────────────────────┘  │
│           │                                │                        │
│           ▼                                ▼                        │
│   VNVME_FDO_CONTEXT                VNVME_PDO_CONTEXT                │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

**为什么必须合并到单一驱动？**

| 问题 | 分离架构 | 合并架构 |
|------|----------|----------|
| PDO 的 PnP IRP 谁响应？ | ❌ 无法控制 - Windows 会加载 stornvme | ✅ 创建者负责响应 |
| IRP_MN_QUERY_RESOURCES | ❌ 需要复杂的 Filter Driver | ✅ 直接在 PDO 层处理 |
| 共享内存访问 | ❌ 跨驱动通信开销 | ✅ 同一驱动直接访问 |
| PCIe 配置空间 | ❌ 需要 IRP 转发 | ✅ 直接处理 BusQueryInterface |

**关键点**: Windows 驱动模型中，创建 PDO 的驱动**必须**处理该 PDO 的底层 PnP/Power IRP。

---

#### 1.1 根设备枚举

Windows 需要知道 `ROOT\VNVME` 设备存在，有三种方式：

**方式 1: devcon 手动安装 (开发测试推荐)**

```powershell
# 安装驱动并创建根设备实例
devcon install vnvme.inf ROOT\VNVME
```

**方式 2: 驱动内部使用 IoReportDetectedDevice (自动枚举)**

```c
// 在 DriverEntry 中自动报告根设备
NTSTATUS VnvmeReportRootDevice(PDRIVER_OBJECT DriverObject)
{
    NTSTATUS status;
    PDEVICE_OBJECT pdo = NULL;
    
    // 报告根枚举设备
    status = IoReportDetectedDevice(
        DriverObject,
        InterfaceTypeUndefined,  // 总线类型
        -1,                      // 总线号 (未知)
        -1,                      // 槽号 (未知)
        NULL,                    // 资源列表
        NULL,                    // 资源需求列表
        FALSE,                   // 非重复检测
        &pdo);                   // 输出 PDO
    
    if (NT_SUCCESS(status)) {
        // 可选: 设置设备属性
        // 调用 IoReportResourceForDetectedDevice 分配资源
        
        // 标记设备已启动
        pdo->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    
    return status;
}
```

**方式 3: 注册表预配置 (生产环境)**

```powershell
# 在注册表中添加根枚举设备
$regPath = "HKLM:\SYSTEM\CurrentControlSet\Enum\ROOT\VNVME\0000"
New-Item -Path $regPath -Force
New-ItemProperty -Path $regPath -Name "HardwareID" -Value "ROOT\VNVME" -PropertyType MultiString
New-ItemProperty -Path $regPath -Name "ConfigFlags" -Value 0 -PropertyType DWord
```

**推荐流程**:

```
┌─────────────────────────────────────────────────────────────────────┐
│                    根设备枚举流程                                    │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. 安装驱动: pnputil /add-driver vnvme.inf                         │
│                    │                                                 │
│                    ▼                                                 │
│  2. 创建根设备: devcon install vnvme.inf ROOT\VNVME                 │
│                    │                                                 │
│                    ▼                                                 │
│  3. Windows PnP 调用 VnvmeEvtDeviceAdd()                            │
│                    │                                                 │
│                    ▼                                                 │
│  4. 驱动创建 FDO，初始化 BAR0 内存                                   │
│                    │                                                 │
│                    ▼                                                 │
│  5. 驱动创建子设备 PDO (虚拟 NVMe 控制器)                           │
│                    │                                                 │
│                    ▼                                                 │
│  6. Windows PnP 为 PDO 加载 stornvme.sys                            │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

### 1.2 FDO 层 - 总线功能

FDO (Functional Device Object) 层负责：
- 总线设备管理
- 子设备 (PDO) 的创建和枚举
- 用户态通信
- 共享内存管理

#### 1.2.1 FDO 上下文结构

> **实现**: [vnvme.h - VNVME_FDO_CONTEXT](../vnvme/vnvme.h#L100-L150)

主要成员:
- `WdfDevice` / `ChildList` - WDF 设备和子设备列表
- `ControlDevice` / `ControlQueue` - 控制设备 `\\.\VNVMEControl`
- `SharedMemory*` - 共享内存指针 (64MB)
- `ControllerCount` / `MaxControllers` - 子设备管理

#### 1.2.2 总线管理 (bus.c)

```c
// 驱动入口
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, VnvmeEvtDeviceAdd);
    return WdfDriverCreate(DriverObject, RegistryPath, NULL, &config, NULL);
}

// 创建根总线设备 FDO
NTSTATUS VnvmeEvtDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT DeviceInit)
{
    PVNVME_FDO_CONTEXT fdoCtx;
    
    // 创建总线 FDO
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
    
    // 创建设备
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    
    // 获取 FDO 上下文
    fdoCtx = VnvmeGetFdoContext(device);
    fdoCtx->IsFdo = TRUE;
    fdoCtx->Signature = 'FDOV';
    
    // 初始化共享内存
    status = VnvmeAllocateSharedMemory(fdoCtx);
    
    // 创建控制设备 \\.\VNVMEControl
    status = VnvmeCreateControlDevice(fdoCtx);
    
    // 创建子设备列表
    status = WdfChildListCreate(device, ...);
    
    // [可选] 驱动加载时自动创建一个默认控制器
    // 注意: 这里直接调用低层函数，因为不是 IOCTL 路径
    // 如果希望完全由用户态通过 IOCTL 控制，可以删除这段
    status = VnvmeCreateControllerPdo(device, 0, NULL);
    
    return status;
}
```

---

### 1.3 PDO 层 - NVMe 仿真

PDO (Physical Device Object) 层负责：
- 呈现为 PCIe NVMe 设备
- BAR0 内存和 NVMe 寄存器
- Doorbell 轮询和命令检测
- PRP 解析和数据复制

#### 1.3.1 PDO 上下文结构

> **实现**: [vnvme.h - VNVME_PDO_CONTEXT](../vnvme/vnvme.h#L180-L250)

主要成员:
- `Bar0VirtAddr` / `Bar0PhysAddr` - BAR0 内存 (64KB)
- `Registers` / `Doorbells` - 指向 BAR0 内部区域
- `State` / `CachedCC` - 控制器状态
- `AdminSQ` / `AdminCQ` / `IoSQ[]` / `IoCQ[]` - 队列
- `PollingTimer` / `PollIntervalUs` - 轮询定时器

#### 1.3.2 BAR0 内存分配 (bar0.c)

> **实现**: [bar0.c - VnvmeAllocateBar0](../vnvme/bar0.c#L14-L42)

BAR0 布局 (64KB):
| 区域 | 偏移 | 大小 | 用途 |
|------|------|------|------|
| 寄存器 | 0x0000 | 4KB | NVMe 控制器寄存器 |
| Doorbell | 0x1000 | 4KB | SQ/CQ Tail/Head Doorbell |
| MSI-X Table | 0x2000 | 1KB | 中断向量表 |
| MSI-X PBA | 0x2400 | 8B | Pending Bit Array |

#### 1.3.3 静态寄存器初始化 (bar0.c)

> **实现**: [bar0.c - VnvmeInitializeBar0Registers](../vnvme/bar0.c#L63-L112)

stornvme 读取时直接获得值，无需拦截。初始化的关键寄存器:

| 寄存器 | 偏移 | 关键字段 | 说明 |
|--------|------|----------|------|
| CAP | 0x00 | MQES=4095, CQR=1, CSS=1 | 控制器能力 |
| VS | 0x08 | MJR=1, MNR=4 | 版本 1.4 |
| CC | 0x14 | 初始为 0 | stornvme 写入 |
| CSTS | 0x1C | 初始为 0 | 轮询检测 |

#### 1.3.4 Doorbell 轮询引擎 (doorbell.c)

这是核心机制 - 检测 stornvme 的命令提交：

```c
// doorbell.c - 操作 VNVME_PDO_CONTEXT

// 轮询定时器回调
VOID VnvmePollTimerCallback(WDFTIMER Timer)
{
    PVNVME_PDO_CONTEXT pdoCtx = WdfObjectGetTypedContext(
        WdfTimerGetParentObject(Timer), VNVME_PDO_CONTEXT);
    
    BOOLEAN hadWork = FALSE;
    
    // 1. 检查 CC 寄存器变化 (控制器启用/禁用)
    ULONG currentCC = pdoCtx->Registers->CC.AsUlong;
    if (currentCC != pdoCtx->CachedCC) {
        VnvmeHandleCCChange(pdoCtx, currentCC);
        pdoCtx->CachedCC = currentCC;
        hadWork = TRUE;
    }
    
    // 2. 如果控制器已启用，检查 Admin Queue 配置
    if (pdoCtx->State == VNVME_STATE_ENABLED) {
        // stornvme 写入 AQA/ASQ/ACQ 后设置 CC.EN=1
        VnvmeSetupAdminQueue(pdoCtx);
    }
    
    // 3. 检查 Doorbell 变化
    if (pdoCtx->State == VNVME_STATE_READY) {
        // 检查 Admin SQ Tail Doorbell
        USHORT currentTail = (USHORT)pdoCtx->Doorbells[0];  // Doorbell 0 = Admin SQ
        if (currentTail != pdoCtx->AdminSQTailCached) {
            VnvmeProcessAdminCommands(pdoCtx, pdoCtx->AdminSQTailCached, currentTail);
            pdoCtx->AdminSQTailCached = currentTail;
            hadWork = TRUE;
        }
        
        // 检查所有 I/O SQ Tails
        for (USHORT i = 0; i < pdoCtx->IoQueueCount; i++) {
            USHORT ioTail = (USHORT)pdoCtx->Doorbells[2 + i * 2];  // I/O SQ Doorbell
            if (ioTail != pdoCtx->IoSQ[i].TailCached) {
                VnvmeProcessIoCommands(pdoCtx, i, pdoCtx->IoSQ[i].TailCached, ioTail);
                pdoCtx->IoSQ[i].TailCached = ioTail;
                hadWork = TRUE;
            }
        }
    }
    
    // 4. 自适应轮询间隔
    if (hadWork) {
        // 有工作时加快轮询 (最小 10μs)
        pdoCtx->PollIntervalUs = max(pdoCtx->PollIntervalUs / 2, 10);
    } else {
        // 无工作时减慢轮询 (最大 1000μs)
        pdoCtx->PollIntervalUs = min(pdoCtx->PollIntervalUs * 2, 1000);
    }
    
    // 5. 重新调度定时器
    WdfTimerStart(Timer, WDF_REL_TIMEOUT_IN_US(pdoCtx->PollIntervalUs));
}
```

#### 1.3.5 命令提取和转发 (queue.c)

```c
// queue.c - 操作 VNVME_PDO_CONTEXT

VOID VnvmeProcessAdminCommands(
    PVNVME_PDO_CONTEXT PdoCtx,
    USHORT OldTail,
    USHORT NewTail)
{
    PVNVME_QUEUE sq = &PdoCtx->AdminSQ;
    
    // 安全检查: 确保队列已初始化
    if (!sq->MappedAddr) {
        TraceEvents(TRACE_LEVEL_WARNING, TRACE_QUEUE,
                   "Admin SQ not initialized");
        return;
    }
    
    // 获取父 FDO 的共享内存
    PVNVME_FDO_CONTEXT fdoCtx = PdoCtx->ParentFdo;
    PNVME_COMMAND sqBase = (PNVME_COMMAND)sq->MappedAddr;
    
    // 遍历新提交的命令
    USHORT idx = OldTail;
    while (idx != NewTail) {
        PNVME_COMMAND cmd = &sqBase[idx];
        
        // 将命令复制到共享内存环形缓冲区
        PVNVME_SHARED_COMMAND sharedCmd = VnvmeGetNextCommandSlot(fdoCtx);
        if (sharedCmd) {
            sharedCmd->ControllerIndex = PdoCtx->ControllerIndex;
            sharedCmd->QueueId = 0;  // Admin Queue
            sharedCmd->CommandIndex = idx;
            RtlCopyMemory(&sharedCmd->Command, cmd, sizeof(NVME_COMMAND));
            
            // 如果是 Write 命令，解析 PRP 并复制数据到共享内存
            if (cmd->CDW0.OPC == NVME_OPC_WRITE) {
                NTSTATUS status = VnvmeCopyDataFromPrp(PdoCtx, cmd, sharedCmd);
                if (!NT_SUCCESS(status)) {
                    TraceEvents(TRACE_LEVEL_ERROR, TRACE_COMMAND,
                               "Failed to copy PRP data: 0x%08X", status);
                    VnvmeReleaseCommandSlot(fdoCtx, sharedCmd);
                    idx = (idx + 1) % sq->Size;
                    continue;
                }
            }
            
            VnvmeSubmitCommandToUser(fdoCtx, sharedCmd);
        }
        
        idx = (idx + 1) % sq->Size;
    }
    
    // 通知用户态有新命令
    KeSetEvent(&fdoCtx->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
}
```

#### 1.3.6 完成处理 (queue.c)

```c
// queue.c - 用户态处理完成后，内核将结果写入 CQ

VOID VnvmePostCompletion(
    PVNVME_PDO_CONTEXT PdoCtx,
    USHORT CqId,
    PNVME_COMPLETION Completion)
{
    PVNVME_QUEUE cq = (CqId == 0) ? &PdoCtx->AdminCQ : &PdoCtx->IoCQ[CqId - 1];
    PNVME_COMPLETION cqBase = (PNVME_COMPLETION)cq->MappedAddr;
    
    // 设置 Phase Tag (bit 0)
    USHORT statusWithPhase = (Completion->Status & 0xFFFE) | (cq->Phase ? 1 : 0);
    Completion->Status = statusWithPhase;
    
    // 写入 CQ
    RtlCopyMemory(&cqBase[cq->Tail], Completion, sizeof(NVME_COMPLETION));
    
    // 更新 Tail，处理 Phase 翻转
    cq->Tail++;
    if (cq->Tail >= cq->Size) {
        cq->Tail = 0;
        cq->Phase = !cq->Phase;
    }
    
    // 如果是 Read 命令，将数据从共享内存复制到 stornvme 的 PRP 缓冲区
    // ...
    
    // 更新统计
    PdoCtx->CommandsProcessed++;
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
    PVNVME_SUBMISSION_RING subRing = &Shared->SubmissionRing;
    
    while (subRing->Head != subRing->Tail) {
        PVNVME_SUBMISSION_RING_ENTRY entry = &subRing->Entries[subRing->Head];
        NVME_COMPLETION completion = {0};
        
        // 根据 Opcode 处理
        switch (entry->Opcode) {
        case NVME_ADMIN_OPC_IDENTIFY:
            VnvmeHandleIdentify(Shared, entry, &completion);
            break;
            
        case NVME_ADMIN_OPC_CREATE_IO_CQ:
            VnvmeHandleCreateIoCQ(Shared, entry, &completion);
            break;
            
        case NVME_ADMIN_OPC_CREATE_IO_SQ:
            VnvmeHandleCreateIoSQ(Shared, entry, &completion);
            break;
            
        case NVME_IO_OPC_READ:
            VnvmeHandleRead(Shared, entry, Backend, &completion);
            break;
            
        case NVME_IO_OPC_WRITE:
            VnvmeHandleWrite(Shared, entry, Backend, &completion);
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
        VnvmeSubmitCompletion(Shared, entry->QueueId, &completion);
        
        // 前进 Head
        subRing->Head = (subRing->Head + 1) % subRing->Size;
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
│              │  提交环 (1MB)                                     │   │
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
    
    // 提交环配置
    ULONG SubmissionRingOffset;
    ULONG SubmissionRingSize;
    
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
