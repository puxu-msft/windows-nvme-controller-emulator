# 系统架构设计

## 整体架构

本项目采用 **StorPort Virtual Miniport** 架构，这是微软官方推荐的虚拟存储设备驱动方案，支持企业级功能如 MPIO、高级队列管理、负载均衡等。

```
┌─────────────────────────────────────────────────────────────────┐
│                        用户应用程序                              │
├─────────────────────────────────────────────────────────────────┤
│                     文件系统 (NTFS/ReFS)                         │
├─────────────────────────────────────────────────────────────────┤
│                     卷管理器 (volmgr.sys)                        │
├─────────────────────────────────────────────────────────────────┤
│                     分区管理器 (partmgr.sys)                     │
├─────────────────────────────────────────────────────────────────┤
│                     磁盘类驱动 (disk.sys)                        │
├─────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────┐  │
│  │              StorPort 端口驱动 (storport.sys)              │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │  │
│  │  │ 队列管理    │  │ 请求分发    │  │ 电源/PnP 管理     │  │  │
│  │  │ (深度 250)  │  │ (负载均衡)  │  │ (自动处理)        │  │  │
│  │  └─────────────┘  └─────────────┘  └───────────────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│  ┌───────────────────────────────────────────────────────────┐  │
│  │        Virtual NVMe Miniport (vnvme.sys)                   │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │  │
│  │  │ HW 回调实现 │  │ SRB 命令    │  │ 存储后端适配器    │  │  │
│  │  │ (StartIo等) │  │ 处理器      │  │ (Memory/File/VHD) │  │  │
│  │  └─────────────┘  └─────────────┘  └───────────────────┘  │  │
│  └───────────────────────────────────────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                 存储后端 (内存 / 文件 / VHD / 远程)              │
└─────────────────────────────────────────────────────────────────┘
```

## 为什么使用 SCSI 命令而非 NVMe 命令？

这是本项目最常见的架构疑问。简短回答：**NVMe 命令是为物理硬件设计的，虚拟设备无法直接使用**。

### NVMe 命令的硬件依赖

NVMe 协议专为 PCIe 直连 SSD 设计，其命令执行依赖以下**物理硬件资源**：

| NVMe 需求 | 说明 | 虚拟设备能否提供？ |
|-----------|------|-------------------|
| PCIe BAR 寄存器 | 控制器寄存器映射到物理内存地址 | ❌ 无物理 PCIe 设备 |
| Submission Queue | 命令队列必须在物理连续内存中 | ❌ 需要 DMA 映射 |
| Completion Queue | 完成队列同样需要物理地址 | ❌ 需要 DMA 映射 |
| MSI-X 中断 | 每个队列需要独立的 PCIe 中断 | ❌ 无物理中断源 |
| Doorbell 寄存器 | 通知控制器有新命令 | ❌ 需要 MMIO 写入 |

```
真实 NVMe 设备工作方式:
┌──────────┐      PCIe      ┌──────────────┐
│   CPU    │ ◄────────────► │  NVMe SSD    │
│          │   DMA / MSI-X  │  (物理硬件)  │
└──────────┘                └──────────────┘
     │
     │ 直接写 Doorbell 寄存器
     │ SQ/CQ 在物理内存中
     ▼
  NVMe 命令直接由硬件执行
```

### 虚拟设备的现实

虚拟设备**没有物理 PCIe 端点**，无法：
- 提供 BAR 空间供 CPU 访问
- 响应 Doorbell 写入
- 发起 DMA 传输
- 产生 MSI-X 中断

因此，Windows 为虚拟存储提供了 **StorPort Virtual Miniport** 框架，使用通用的 **SCSI/SRB 抽象层**：

```
虚拟 NVMe 设备工作方式:
┌──────────┐                ┌──────────────┐
│ 应用程序 │ ──ReadFile──►  │   ntfs.sys   │
└──────────┘                └──────┬───────┘
                                   │ IRP
                                   ▼
                           ┌──────────────┐
                           │   disk.sys   │  ← 将 IRP 转换为 SCSI SRB
                           └──────┬───────┘
                                  │ SRB (READ_10, WRITE_10, ...)
                                  ▼
                           ┌──────────────┐
                           │ storport.sys │  ← 队列管理、调度
                           └──────┬───────┘
                                  │ HwStartIo 回调
                                  ▼
                           ┌──────────────┐
                           │  vnvme.sys   │  ← 我们的驱动，处理 SRB
                           └──────┬───────┘
                                  │ 后端 Read/Write
                                  ▼
                           ┌──────────────┐
                           │ 内存/文件/VHD │
                           └──────────────┘
```

### 如果坚持要实现 NVMe 命令？

理论上可以，但需要：

| 方案 | 复杂度 | 说明 |
|------|--------|------|
| **完整 NVMe 控制器仿真** | 极高 | 类似 QEMU/KVM 的做法，需要仿真 PCIe 设备、BAR、中断等 |
| **NVMe over Fabrics** | 高 | 使用网络传输 NVMe 命令，需要 NVMe-oF Target/Initiator |
| **修改 stornvme.sys** | 不可行 | 微软驱动，无法修改 |

这些方案远超普通虚拟磁盘的需求。

### 本项目的定位

| 方面 | 说明 |
|------|------|
| **对外呈现** | 标准 SCSI 磁盘设备 (在设备管理器中显示为磁盘) |
| **命令接口** | SCSI/SRB (READ_10, WRITE_10, INQUIRY, UNMAP 等) |
| **NVMe 概念借鉴** | 设备标识格式、SMART 信息结构、高队列深度等 |
| **名称中的 "NVMe"** | 表示模拟 NVMe SSD 的高性能特性，而非协议实现 |

> **结论**：本项目使用 SCSI 命令是 Windows 虚拟存储驱动的**唯一实用选择**，不是技术妥协，而是正确的架构决策。

---

## 对用户而言，我们是什么？

### 设备呈现形式

作为 StorPort Virtual Miniport，我们的设备在系统中呈现为 **SCSI 磁盘设备**，而非 NVMe 设备：

```
设备管理器中的显示:

磁盘驱动器
├── Samsung SSD 980 PRO 1TB          ← 真实 NVMe (由 stornvme.sys 驱动)
├── WDC WD10EZEX-00WN4A0             ← 真实 SATA (由 storahci.sys 驱动)
└── Virtual NVMe Disk                 ← 我们的设备 (由 vnvme.sys 驱动)
                                         ↑
                                         显示为普通磁盘，不是 NVMe 类别
```

### 用户视角对比

| 方面 | 真实 NVMe 设备 | 我们的虚拟设备 |
|------|---------------|---------------|
| **设备管理器分类** | "NVMe Controller" 子项 | "磁盘驱动器" |
| **驱动程序** | stornvme.sys (Microsoft) | vnvme.sys (我们) |
| **硬件 ID** | `PCI\VEN_xxxx&DEV_xxxx` | `ROOT\VNVME` 或自定义 |
| **NVMe-CLI 工具** | ✅ 可识别、可管理 | ❌ 不可识别 |
| **Crystal Disk Info** | 显示 NVMe SMART | 显示通用信息或不支持 |
| **Windows 磁盘管理** | ✅ 正常使用 | ✅ 正常使用 |
| **格式化/分区** | ✅ 正常 | ✅ 正常 |
| **ReadFile/WriteFile** | ✅ 正常 | ✅ 正常 |

### 应用程序兼容性

对于**普通应用程序**（文件读写），完全透明：

```c
// 应用程序代码 - 无需修改，真实/虚拟设备均可用
HANDLE hFile = CreateFile(L"V:\\data.txt", ...);  // V: 是虚拟磁盘上的卷
WriteFile(hFile, buffer, size, &written, NULL);   // 正常工作
ReadFile(hFile, buffer, size, &read, NULL);       // 正常工作
CloseHandle(hFile);
```

对于 **NVMe 专用工具**（如 nvme-cli、厂商工具），无法识别：

```powershell
# nvme-cli 只识别真实 NVMe 设备
PS> nvme list
Node             SN                   Model                  ...
---------------- -------------------- ---------------------- ...
/dev/nvme0       S5GXNZ0R123456       Samsung SSD 980 PRO    ...
# 我们的虚拟设备不会出现在这里
```

### Windows 能帮我们模拟真实 NVMe 吗？

**简短回答：不能**。Windows 本身不提供 NVMe 设备仿真框架。

| 方案 | 可行性 | 说明 |
|------|--------|------|
| **Windows 原生** | ❌ 不支持 | 没有 "Virtual NVMe" 框架 |
| **Hyper-V** | ⚠️ 有限 | 虚拟机内可见 NVMe，但宿主机看不到 |
| **QEMU/KVM** | ✅ 可以 | 完整 NVMe 控制器仿真，但需要虚拟化环境 |
| **SPDK vhost** | ✅ 可以 | 高性能 NVMe 仿真，需要特殊配置 |

如果确实需要**真正的 NVMe 设备仿真**（让 nvme-cli 能识别），需要：

1. **虚拟机方案**：在 Hyper-V/VMware 中将虚拟磁盘以 NVMe 控制器形式呈现给 Guest OS
2. **自己实现 NVMe 控制器仿真**：极其复杂，需要仿真完整的 PCIe 设备

### 那我们的价值是什么？

| 价值 | 说明 |
|------|------|
| **通用存储虚拟化** | 99% 的应用场景不需要 "真正的 NVMe"，只需要高性能磁盘 |
| **灵活后端** | 内存、文件、VHD、网络存储都可以作为后端 |
| **高性能** | StorPort 250 队列深度，媲美真实 NVMe 性能 |
| **企业功能** | MPIO、热插拔、动态调整大小 |
| **开发测试** | 无需真实硬件即可测试存储相关功能 |
| **特殊用途** | RAM Disk、加密磁盘、压缩磁盘、远程存储等 |

> **总结**：我们的虚拟设备对普通应用程序**完全透明**，表现为高性能 SCSI 磁盘。只有 NVMe 专用管理工具无法识别我们，但这在绝大多数场景下不是问题。

---

## StorPort Virtual Miniport 优势

### 为什么选择 StorPort Virtual Miniport？

| 特性 | 说明 |
|------|------|
| **微软官方推荐** | 用于虚拟 HBA、iSCSI Initiator、软件 RAID 等场景 |
| **企业级功能** | 内置 MPIO 支持、负载均衡、故障转移 |
| **高性能** | 优化的队列管理，虚拟设备队列深度 250 |
| **简化开发** | StorPort 处理 PnP、电源、DPC、同步等复杂逻辑 |
| **标准接口** | 与 Windows 存储栈完美集成 |

### 关键配置：VirtualDevice 标志

通过设置 `PORT_CONFIGURATION_INFORMATION.VirtualDevice = TRUE`，StorPort 进入虚拟模式：

```c
ULONG VNvmeHwFindAdapter(
    PVOID DeviceExtension,
    PVOID HwContext,
    PVOID BusInformation,
    PCHAR ArgumentString,
    PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    PUCHAR Again)
{
    // 关键: 标记为虚拟设备
    ConfigInfo->VirtualDevice = TRUE;
    
    // 虚拟设备特性
    ConfigInfo->NumberOfPhysicalBreaks = STORPORT_DEFAULT_PHYSICAL_BREAKS;
    ConfigInfo->MaximumTransferLength = 4 * 1024 * 1024;  // 4MB
    ConfigInfo->AlignmentMask = 0;  // 无对齐要求
    
    // ...
    return SP_RETURN_FOUND;
}
```

**VirtualDevice = TRUE 时的行为变化：**
- 不需要 DMA 适配器对象
- 不需要硬件中断处理
- 不需要 I/O 端口或内存映射
- 初始 LUN 队列深度自动设为 250（物理设备为 20）
- StorPort 不尝试访问任何硬件资源

## Windows 存储栈集成

### 存储栈层次

```
┌──────────────────┬────────────────────────────────────────────┐
│ 层级             │ 说明                                       │
├──────────────────┼────────────────────────────────────────────┤
│ 文件系统         │ NTFS/ReFS - 文件和目录管理                 │
│ 卷管理器         │ volmgr.sys - 管理卷设备                    │
│ 分区管理器       │ partmgr.sys - 管理磁盘分区                 │
│ 磁盘类驱动       │ disk.sys - 将 IRP 转换为 SRB               │
│ 端口驱动         │ storport.sys - SRB 调度、队列管理          │
│ Miniport 驱动    │ vnvme.sys - 执行实际 I/O（本驱动）         │
│ 存储后端         │ 内存/文件/VHD - 数据持久化                 │
└──────────────────┴────────────────────────────────────────────┘
```

### I/O 请求流转

```
应用程序 ReadFile(hFile, buffer, 4096)
                    │
                    ▼
         ┌──────────────────┐
         │    ntfs.sys      │  IRP_MJ_READ
         └────────┬─────────┘
                  │
                  ▼
         ┌──────────────────┐
         │    disk.sys      │  创建 SRB (SCSI_REQUEST_BLOCK)
         └────────┬─────────┘
                  │ SRB: READ_10, LBA=xxx, Length=8 sectors
                  ▼
         ┌──────────────────┐
         │  storport.sys    │  队列管理、请求调度
         └────────┬─────────┘
                  │ 调用 HwStartIo()
                  ▼
         ┌──────────────────┐
         │    vnvme.sys     │  解析 SRB，执行后端 I/O
         │  (Miniport)      │
         └────────┬─────────┘
                  │ Backend->Read(offset, length, buffer)
                  ▼
         ┌──────────────────┐
         │   存储后端       │  内存拷贝 / 文件读取 / VHD 读取
         └────────┬─────────┘
                  │
                  ▼
    StorPortNotification(RequestComplete, ...) 完成请求
```

## 核心组件

### 1. Miniport 驱动入口 (DriverEntry)

```c
ULONG DriverEntry(
    PVOID DriverObject,
    PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA hwInitData;
    
    RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));
    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);
    
    // 虚拟适配器使用 Internal 接口类型
    hwInitData.AdapterInterfaceType = Internal;
    
    // 必需的回调函数
    hwInitData.HwInitialize = VNvmeHwInitialize;
    hwInitData.HwStartIo = VNvmeHwStartIo;
    hwInitData.HwFindAdapter = VNvmeHwFindAdapter;
    hwInitData.HwResetBus = VNvmeHwResetBus;
    hwInitData.HwAdapterControl = VNvmeHwAdapterControl;
    
    // 虚拟设备不需要中断处理
    hwInitData.HwInterrupt = NULL;
    
    // 扩展结构大小
    hwInitData.DeviceExtensionSize = sizeof(VNVME_ADAPTER_EXTENSION);
    hwInitData.SpecificLuExtensionSize = sizeof(VNVME_LU_EXTENSION);
    hwInitData.SrbExtensionSize = sizeof(VNVME_SRB_EXTENSION);
    
    // 不需要自动请求感知（虚拟设备）
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;
    hwInitData.ReceiveEvent = TRUE;
    
    return StorPortInitialize(
        DriverObject,
        RegistryPath,
        &hwInitData,
        NULL);
}
```

### 2. StorPort 回调函数

| 回调函数 | 必需 | 职责 |
|----------|------|------|
| `HwFindAdapter` | ✓ | 初始化适配器，设置配置信息 |
| `HwInitialize` | ✓ | 适配器硬件初始化（虚拟设备可为空） |
| `HwStartIo` | ✓ | **核心**：处理 SRB 请求 |
| `HwResetBus` | ✓ | 总线复位处理 |
| `HwAdapterControl` | ✓ | 电源管理、停止/启动适配器 |
| `HwInterrupt` | ✗ | 中断处理（虚拟设备不需要） |
| `HwBuildIo` | ✗ | 预处理 SRB（可选优化） |
| `HwDmaStarted` | ✗ | DMA 完成通知（虚拟设备不需要） |

### 3. SRB 命令处理器

处理 disk.sys 发送的 SCSI 命令：

| SCSI 命令 | 操作码 | 处理说明 |
|-----------|--------|----------|
| `SCSIOP_READ6/10/16` | 0x08/0x28/0x88 | 从后端读取数据 |
| `SCSIOP_WRITE6/10/16` | 0x0A/0x2A/0x8A | 向后端写入数据 |
| `SCSIOP_READ_CAPACITY` | 0x25 | 返回磁盘容量 |
| `SCSIOP_READ_CAPACITY16` | 0x9E | 返回大容量磁盘信息 |
| `SCSIOP_INQUIRY` | 0x12 | 返回设备标识信息 |
| `SCSIOP_MODE_SENSE` | 0x1A | 返回设备模式页 |
| `SCSIOP_TEST_UNIT_READY` | 0x00 | 检查设备就绪状态 |
| `SCSIOP_SYNCHRONIZE_CACHE` | 0x35 | 刷新缓存到后端 |
| `SCSIOP_UNMAP` | 0x42 | TRIM/UNMAP 支持 |
| `SCSIOP_START_STOP_UNIT` | 0x1B | 启动/停止设备 |

### 4. 存储后端适配器

```
┌─────────────────────────────────────────────────────────────┐
│                    后端接口层                                │
│  VNvmeBackendRead() | VNvmeBackendWrite() | VNvmeBackendFlush()
└─────────────────────────────────────────────────────────────┘
        │                    │                    │
        ▼                    ▼                    ▼
  ┌───────────┐       ┌───────────┐       ┌───────────┐
  │  Memory   │       │   File    │       │    VHD    │
  │  Backend  │       │  Backend  │       │  Backend  │
  └───────────┘       └───────────┘       └───────────┘
       │                   │                   │
       ▼                   ▼                   ▼
   NonPagedPool       ZwReadFile           virtdisk
   RtlCopyMemory      ZwWriteFile           API
```

**后端类型：**

| 后端 | 持久化 | 性能 | 适用场景 |
|------|--------|------|----------|
| Memory | ✗ | 最高 | 测试、临时存储 |
| File | ✓ | 中等 | 生产环境、简单持久化 |
| VHD | ✓ | 中等 | 高级功能（快照、差异盘） |
| Remote | ✓ | 取决于网络 | iSCSI/NVMe-oF（预留） |

### 5. 虚拟 LUN 管理

支持多个虚拟磁盘（LUN）：

```
Adapter (PathId = 0)
    │
    ├── Target 0
    │   ├── LUN 0  →  Namespace 1 (512GB, File Backend)
    │   ├── LUN 1  →  Namespace 2 (256GB, Memory Backend)
    │   └── LUN 2  →  Namespace 3 (1TB, VHD Backend)
    │
    └── Target 1 (可选，用于 MPIO 测试)
        └── LUN 0  →  同一 Namespace 的另一路径
```

## 企业级功能

### 1. MPIO (多路径 I/O) 支持

StorPort 原生支持 MPIO，虚拟驱动可以：
- 暴露同一 LUN 的多条路径
- 支持负载均衡策略（Round Robin、Least Queue Depth 等）
- 支持故障转移

```c
// 在 INQUIRY 中报告 MPIO 支持
VPD_IDENTIFICATION_PAGE vpdPage;
vpdPage.PageCode = VPD_DEVICE_IDENTIFIERS;
// 添加 NAA 或 EUI64 标识符
```

### 2. 高级队列管理

- 虚拟设备自动获得 250 的队列深度
- 支持多请求并发 (`MultipleRequestPerLu = TRUE`)
- StorPort 自动进行请求调度和负载均衡

### 3. 电源管理

通过 `HwAdapterControl` 回调处理：
- `ScsiStopAdapter` - 适配器停止
- `ScsiRestartAdapter` - 适配器重启
- `ScsiPowerSettingNotification` - 电源状态变更

### 4. 动态容量调整

支持运行时调整虚拟磁盘大小，通过 IOCTL 接口触发。

## 设备枚举与管理

### 设备创建流程

```
1. 驱动加载 (DriverEntry)
        │
        ▼
2. StorPortInitialize() 注册到 StorPort
        │
        ▼
3. PnP 管理器调用 HwFindAdapter()
        │
        ▼
4. 设置 VirtualDevice = TRUE，配置适配器
        │
        ▼
5. StorPort 调用 HwInitialize()
        │
        ▼
6. 驱动响应 INQUIRY 命令，报告 LUN
        │
        ▼
7. disk.sys 附加到每个 LUN
        │
        ▼
8. 磁盘管理器显示新磁盘
```

### 用户态管理工具

`vnvmectl.exe` 通过 IOCTL 与驱动通信：

| 功能 | IOCTL |
|------|-------|
| 创建虚拟磁盘 | `IOCTL_VNVME_CREATE_DISK` |
| 删除虚拟磁盘 | `IOCTL_VNVME_DELETE_DISK` |
| 查询磁盘信息 | `IOCTL_VNVME_QUERY_DISK` |
| 调整磁盘大小 | `IOCTL_VNVME_RESIZE_DISK` |
| 配置后端 | `IOCTL_VNVME_SET_BACKEND` |

## 与真实 NVMe 驱动的关系

本驱动模拟 NVMe 设备的**行为**，但在 Windows 存储栈中：
- 使用 SCSI/SRB 接口而非直接 NVMe 命令
- 不与 stornvme.sys（真实 NVMe 驱动）冲突
- 可选实现 NVMe Pass-through 供管理工具使用

```
真实 NVMe 设备:  stornvme.sys ──► PCIe 硬件
虚拟 NVMe 设备:  vnvme.sys ──► StorPort ──► 后端存储
```

## 文件结构

```
vnvme/
├── vnvme.sys           # StorPort Virtual Miniport 驱动
├── vnvme.inf           # 驱动安装信息文件
├── vnvme.cat           # 驱动目录签名
├── vnvmectl.exe        # 用户态管理工具
└── vnvme_backend.dll   # 可选：外部后端插件
```

## 开发环境要求

| 组件 | 版本 |
|------|------|
| Visual Studio | 2022 或更高 |
| WDK | 10.0.22621.0 或更高 |
| Windows SDK | 10.0.22621.0 或更高 |
| 目标平台 | Windows 10/11 x64 |

## 参考资源

- [StorPort Miniport Drivers](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/storport-miniport-drivers)
- [PORT_CONFIGURATION_INFORMATION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/storport/ns-storport-_port_configuration_information)
- [Virtual Storport Miniport](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/virtual-miniport-drivers)
- [SCSI Request Block](https://learn.microsoft.com/en-us/windows-hardware/drivers/storage/scsi-request-block-srb)

---

## 附录：架构决策记录

### 决策: 选择 StorPort Virtual Miniport 而非虚拟总线驱动

**决策日期**: 项目初期

**背景**: 
实现 Windows 虚拟存储设备有多种架构选择：

| 方案 | 官方支持 | 适用场景 | 复杂度 |
|------|----------|----------|--------|
| StorPort Virtual Miniport | ✅ 官方推荐 | 虚拟 HBA、iSCSI、软件 RAID | 中等 |
| 虚拟总线 + Class Driver | ✅ 常用 | 简单虚拟磁盘 | 中等 |
| Storage Filter Driver | ✅ 官方支持 | 增强现有设备功能 | 低 |

**考虑的替代方案**:

**方案 A: 虚拟总线 + 功能驱动**
```
disk.sys ──SRB──▶ vnvme.sys ──▶ 后端存储
     ▲
     └─── vnvmebus.sys (设备枚举)
```
- 优点: 架构简单，完全控制设备生命周期
- 缺点: 需自己实现队列管理、MPIO、错误恢复等功能

**方案 B: StorPort Virtual Miniport** (已选择)
```
storport.sys ──SRB──▶ vnvme.sys ──▶ 后端存储
(VirtualDevice = TRUE)
```
- 优点: 微软官方推荐，内置企业级功能，队列深度 250
- 缺点: 需遵循 StorPort 回调模型

**最终决策**: 
选择 **StorPort Virtual Miniport** 方案，原因：
1. 微软官方推荐的虚拟存储设备驱动架构
2. 内置 MPIO、队列管理、负载均衡、错误恢复等企业级功能
3. 通过 `VirtualDevice = TRUE` 简化开发，无需处理硬件细节
4. 队列深度自动提升到 250，满足高性能需求
5. 与 Windows 存储栈深度集成，兼容性更好

**影响**:
- 驱动框架必须使用 WDM（StorPort Miniport 不支持 KMDF）
- 必须实现 StorPort 定义的回调函数集
- 单驱动文件 (vnvme.sys) 即可完成全部功能

