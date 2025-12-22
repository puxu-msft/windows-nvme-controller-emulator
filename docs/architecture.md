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
