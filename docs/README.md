# Virtual NVMe Controller Emulator

## 项目概述

本项目实现一个 **Windows 软件 NVMe 控制器仿真器**，目标是在 Windows 宿主机上创建**真正的虚拟 NVMe 设备**，让 Windows 原生 NVMe 驱动 (stornvme.sys) 可以正常加载和使用。

### 核心目标

| 目标 | 说明 |
|------|------|
| **真实 NVMe 呈现** | 设备管理器中显示为 NVMe 控制器，而非普通 SCSI 磁盘 |
| **原生驱动兼容** | 使用 Windows 自带的 stornvme.sys 驱动 |
| **工具链支持** | nvme-cli、Crystal Disk Info 等工具可识别 |
| **完整协议实现** | 支持 NVMe Admin/I/O 命令集 |
| **灵活后端** | 支持内存、文件、VHD 等存储后端 |

### 最终效果

```
设备管理器:

存储控制器
├── Intel RST VMD Controller
├── Samsung NVMe Controller              ← 真实物理 NVMe
└── Virtual NVMe Controller               ← ★ 我们的虚拟 NVMe ★
    └── 命名空间 1 (100GB)

磁盘驱动器
├── Samsung SSD 980 PRO 1TB
└── Virtual NVMe Disk                     ← 作为 NVMe 命名空间
```

```powershell
PS> nvme list
Node       SN               Model                     Size
---------- ---------------- ---------------------- --------
nvme0      SAMSUNG12345678  Samsung SSD 980 PRO     1.0 TB
nvme1      VNVME0000000001  Virtual NVMe SSD      100.0 GB  ← 我们的设备
```

---

## 技术架构

```
┌─────────────────────────────────────────────────────────────────┐
│                        用户态                                    │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────────┐ │
│  │  应用程序      │  │  vnvmectl.exe  │  │  nvme-cli         │ │
│  │  (文件 I/O)    │  │  (管理工具)    │  │  (NVMe 管理)      │ │
│  └────────────────┘  └────────────────┘  └────────────────────┘ │
└───────────────────────────┬─────────────────────────────────────┘
                            │
┌───────────────────────────┼─────────────────────────────────────┐
│                        内核态                                    │
│                           │                                      │
│  ┌────────────────────────▼────────────────────────────────┐    │
│  │              Windows 存储栈                              │    │
│  │  NTFS → volmgr → partmgr → disk.sys                     │    │
│  └────────────────────────┬────────────────────────────────┘    │
│                           │                                      │
│  ┌────────────────────────▼────────────────────────────────┐    │
│  │           stornvme.sys (Windows 原生 NVMe 驱动)          │    │
│  │           • 发送 NVMe 命令                               │    │
│  │           • 管理 Submission/Completion Queue             │    │
│  └────────────────────────┬────────────────────────────────┘    │
│                           │ NVMe 寄存器访问                      │
│                           ▼                                      │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │        ★ vnvme_emu.sys (NVMe 控制器仿真) ★              │    │
│  │  ┌─────────────┐  ┌─────────────┐  ┌────────────────┐   │    │
│  │  │ 寄存器仿真  │  │ Queue 引擎  │  │ NVMe 命令处理  │   │    │
│  │  └─────────────┘  └─────────────┘  └────────────────┘   │    │
│  └────────────────────────┬────────────────────────────────┘    │
│                           │                                      │
│  ┌────────────────────────▼────────────────────────────────┐    │
│  │          vnvme_bus.sys (虚拟 PCIe 总线)                  │    │
│  │          • 枚举虚拟 NVMe 控制器                          │    │
│  │          • PCIe 配置空间仿真                             │    │
│  │          • BAR 内存映射                                  │    │
│  └─────────────────────────────────────────────────────────┘    │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    存储后端                                      │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐                   │
│  │  Memory  │    │   File   │    │   VHD    │                   │
│  │ (RAM)    │    │ (本地)   │    │ (虚拟盘) │                   │
│  └──────────┘    └──────────┘    └──────────┘                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 文档结构

### 核心设计文档

| 文档 | 说明 |
|------|------|
| [architecture.md](architecture.md) | 系统架构设计，技术方案选择 |
| [pcie-emulation.md](pcie-emulation.md) | 虚拟 PCIe 总线和配置空间仿真 |
| [nvme-controller.md](nvme-controller.md) | NVMe 控制器仿真，寄存器实现 |
| [nvme-commands.md](nvme-commands.md) | NVMe Admin/I/O 命令处理 |
| [queue-engine.md](queue-engine.md) | Submission/Completion Queue 引擎 |

### 实现文档

| 文档 | 说明 |
|------|------|
| [data-structures.md](data-structures.md) | 核心数据结构定义 |
| [backend-storage.md](backend-storage.md) | 存储后端实现 |
| [interrupt-emulation.md](interrupt-emulation.md) | MSI-X 中断仿真 |
| [ioctl-interface.md](ioctl-interface.md) | 用户态管理接口 |

### 开发运维文档

| 文档 | 说明 |
|------|------|
| [build-guide.md](build-guide.md) | 构建环境和编译流程 |
| [testing.md](testing.md) | 测试策略和验证方法 |
| [troubleshooting.md](troubleshooting.md) | 故障排查指南 |

---

## 快速开始

### 环境要求

- **操作系统**: Windows 10 20H1+ / Windows 11 / Windows Server 2019+
- **开发工具**: Visual Studio 2022
- **WDK**: Windows Driver Kit 10.0.22621+
- **测试签名**: 需要启用测试模式或使用 EV 证书

### 构建

```powershell
# 克隆项目
git clone <repository-url>
cd virtual-nvme-driver

# 打开 Visual Studio 解决方案
start vnvme.sln

# 或使用命令行构建
msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64
```

### 安装

```powershell
# 启用测试签名 (需要管理员权限)
bcdedit /set testsigning on
# 重启

# 安装驱动
pnputil /add-driver vnvme_bus.inf /install
pnputil /add-driver vnvme_emu.inf /install

# 创建虚拟 NVMe 设备
vnvmectl create --size 100GB --backend memory
```

### 验证

```powershell
# 检查设备管理器
devmgmt.msc
# 应在 "存储控制器" 下看到 "Virtual NVMe Controller"

# 使用 nvme-cli 验证
nvme list
# 应看到我们的虚拟设备

# 使用 PowerShell 验证
Get-PhysicalDisk | Where-Object BusType -eq "NVMe"
```

---

## 与普通虚拟磁盘的区别

| 方面 | 普通虚拟磁盘 (StorPort) | 本项目 (NVMe 仿真) |
|------|------------------------|-------------------|
| **Windows 识别** | SCSI 磁盘 | NVMe 控制器 + 命名空间 |
| **驱动** | 自定义 miniport | Windows 原生 stornvme.sys |
| **nvme-cli** | ❌ 不可用 | ✅ 完全支持 |
| **SMART 信息** | 通用或无 | NVMe 原生日志页 |
| **设备管理器** | 磁盘驱动器 | 存储控制器 (NVMe) |
| **BusType** | SAS/RAID | NVMe |
| **协议** | SCSI/SRB | NVMe 命令 |
| **复杂度** | 中等 | 高 |

---

## 项目组成

```
virtual-nvme-driver/
├── vnvme_bus/              # 虚拟 PCIe 总线驱动
│   ├── vnvme_bus.c         # 总线驱动主文件
│   ├── pcie_config.c       # PCIe 配置空间仿真
│   ├── pnp.c               # PnP 处理
│   └── vnvme_bus.inf       # 安装文件
│
├── vnvme_emu/              # NVMe 控制器仿真驱动
│   ├── vnvme_emu.c         # 仿真驱动主文件
│   ├── nvme_regs.c         # NVMe 寄存器仿真
│   ├── nvme_admin.c        # Admin 命令处理
│   ├── nvme_io.c           # I/O 命令处理
│   ├── queue_engine.c      # SQ/CQ 引擎
│   ├── interrupt.c         # 中断仿真
│   └── vnvme_emu.inf       # 安装文件
│
├── vnvme_backend/          # 存储后端
│   ├── backend.c           # 后端抽象层
│   ├── memory_backend.c    # 内存后端
│   ├── file_backend.c      # 文件后端
│   └── vhd_backend.c       # VHD 后端
│
├── vnvmectl/               # 用户态管理工具
│   ├── vnvmectl.c          # 命令行工具
│   └── vnvmectl.exe        # 编译产物
│
├── docs/                   # 文档
│   └── ...
│
└── tests/                  # 测试
    └── ...
```

---

## 参考资源

- [NVM Express Base Specification 2.0](https://nvmexpress.org/specifications/)
- [PCI Express Base Specification](https://pcisig.com/specifications)
- [QEMU NVMe 仿真源码](https://github.com/qemu/qemu/blob/master/hw/nvme/)
- [Windows 驱动开发文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/)
