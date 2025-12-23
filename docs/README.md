# Virtual NVMe Controller Emulator

## 项目概述

本项目实现一个 **Windows 软件 NVMe 控制器仿真器**，采用**混合用户态/内核态架构**，在 Windows 宿主机上创建**真正的虚拟 NVMe 设备**，让 Windows 原生 NVMe 驱动 (stornvme.sys) 可以正常加载和使用。

> **📌 重要**: 本项目已采用 v2 混合架构设计。请优先阅读：
> - [architecture-v2.md](architecture-v2.md) - 新的统一混合架构设计
> - [core-mechanisms.md](core-mechanisms.md) - 核心机制详解 (Doorbell 轮询、共享内存、PRP 解析)
> - [architecture-analysis.md](architecture-analysis.md) - 架构分析和问题修复记录

### 核心目标

| 目标 | 说明 |
|------|------|
| **真实 NVMe 呈现** | 设备管理器中显示为 NVMe 控制器，而非普通 SCSI 磁盘 |
| **原生驱动兼容** | 使用 Windows 自带的 stornvme.sys 驱动 |
| **工具链支持** | nvme-cli、Crystal Disk Info 等工具可识别 |
| **类 SPDK 架构** | 用户态处理业务逻辑，内核态仅处理必要的硬件接口 |
| **开发友好** | 用户态代码易于调试，崩溃不导致蓝屏 |
| **灵活后端** | 支持内存、文件、VHD、网络等存储后端 |

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

## 技术架构 (v2 混合模式)

```
┌─────────────────────────────────────────────────────────────────┐
│                        用户态                                    │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                   vnvme-server.exe                          ││
│  │                                                             ││
│  │   ┌───────────┐  ┌───────────┐  ┌───────────┐  ┌─────────┐ ││
│  │   │ 命令引擎  │  │ 后端存储  │  │ 管理接口  │  │ 监控/   │ ││
│  │   │ Admin/IO  │  │ Mem/File  │  │ REST/CLI  │  │ 日志    │ ││
│  │   └───────────┘  └───────────┘  └───────────┘  └─────────┘ ││
│  │                          │                                  ││
│  │   共享内存: [控制块│提交环│完成环│数据缓冲池]               ││
│  │                          │                                  ││
│  └──────────────────────────│──────────────────────────────────┘│
│                             │ IOCTL / 事件                       │
├─────────────────────────────│───────────────────────────────────┤
│                        内核态                                    │
│                             │                                    │
│  ┌──────────────────────────▼──────────────────────────────────┐│
│  │                      vnvme.sys                               ││
│  │   ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  ││
│  │   │ 总线管理     │  │ BAR0 仿真    │  │ Doorbell 轮询    │  ││
│  │   │ PCIe 枚举    │  │ 真实物理内存 │  │ 高精度定时器     │  ││
│  │   └──────────────┘  └──────────────┘  └──────────────────┘  ││
│  │   ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  ││
│  │   │ PRP 解析     │  │ 共享内存     │  │ 完成处理         │  ││
│  │   │ 数据复制     │  │ 环形缓冲区   │  │ Phase Tag        │  ││
│  │   └──────────────┘  └──────────────┘  └──────────────────┘  ││
│  └──────────────────────────────────────────────────────────────┘│
│                             │                                    │
│  ┌──────────────────────────▼──────────────────────────────────┐│
│  │   stornvme.sys (Windows 原生 NVMe 驱动)                      ││
│  └──────────────────────────────────────────────────────────────┘│
└──────────────────────────────────────────────────────────────────┘
```

### 核心设计决策

| 决策 | 说明 |
|------|------|
| **单一内核驱动** | 合并 bus 和 emu 功能到 vnvme.sys |
| **真实内存 BAR0** | 不使用 MMIO 拦截，提供真实物理内存 |
| **Doorbell 轮询** | 高频定时器检测 stornvme 的命令提交 |
| **用户态命令处理** | 所有业务逻辑在用户态 vnvme-server.exe |
| **共享缓冲区** | 预分配大缓冲区用于内核/用户态数据传输 |

---

## 文档结构

### 📌 开始阅读

| 文档 | 说明 |
|------|------|
| [ROADMAP.md](ROADMAP.md) | **开发路线图** - 阶段规划、任务清单、里程碑 |
| [architecture-v2.md](architecture-v2.md) | **核心架构设计** - 内核/用户态分工、FDO/PDO 层 |
| [core-mechanisms.md](core-mechanisms.md) | **核心机制详解** - 轮询、共享内存、PRP、数据路径 |
| [CODING-STANDARDS.md](CODING-STANDARDS.md) | **编码规范** - 命名约定、开发原则、最佳实践 |

### 🔧 开发文档

| 文档 | 说明 |
|------|------|
| [build-guide.md](build-guide.md) | 构建环境、项目结构、编译流程 |
| [inf-guide.md](inf-guide.md) | INF 文件详解、安装/卸载脚本、测试签名 |
| [data-structures.md](data-structures.md) | 核心数据结构 (FDO/PDO Context、共享内存) |
| [user-mode-service.md](user-mode-service.md) | vnvme-server.exe 详细设计 |
| [vnvme-server-modular-design.md](vnvme-server-modular-design.md) | vnvme-server 模块化架构 (v2) |
| [debugging.md](debugging.md) | 调试基础设施和 WinDbg 使用 |

### 📚 NVMe/PCIe 规范参考

| 文档 | 说明 |
|------|------|
| [pcie-emulation.md](pcie-emulation.md) | PCIe 配置空间仿真 |
| [nvme-controller.md](nvme-controller.md) | NVMe 控制器寄存器 (BAR0) |
| [nvme-commands.md](nvme-commands.md) | NVMe 命令格式 (Admin/IO) |
| [queue-engine.md](queue-engine.md) | SQ/CQ 队列机制 |

### 📦 实现模块

| 文档 | 说明 |
|------|------|
| [backend-storage.md](backend-storage.md) | 存储后端实现 (内存/文件) |
| [ioctl-interface.md](ioctl-interface.md) | 用户态管理接口 |
| [interrupt-emulation.md](interrupt-emulation.md) | 中断机制参考 |

### 🔍 测试与运维

| 文档 | 说明 |
|------|------|
| [testing.md](testing.md) | 测试策略和验证方法 |
| [troubleshooting.md](troubleshooting.md) | 故障排查指南 |
| [performance-optimization.md](performance-optimization.md) | **性能优化指南** - 轮询优化、事件通知、批处理、内存访问 |

### 📜 历史记录

| 文档 | 说明 |
|------|------|
| [architecture-analysis.md](architecture-analysis.md) | 架构分析和问题修复记录 |
| [history/](history/) | 评审记录、废弃设计等 |

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
# 1. 启用测试签名 (需要管理员权限)
bcdedit /set testsigning on
# 重启

# 2. 安装内核驱动
pnputil /add-driver vnvme.inf /install

# 3. 启动用户态服务
#    方式 A: 使用命令行参数
vnvme-server.exe --size 100G --backend file --file C:\vnvme\disk.img

#    方式 B: 使用配置文件
vnvme-server.exe --config vnvme.conf

# 4. 使用管理工具
vnvmectl status     # 查看驱动和服务状态
vnvmectl list       # 列出虚拟控制器
vnvmectl create --size 100G --backend memory    # 创建内存后端控制器
vnvmectl delete 1   # 删除控制器
```

### 配置文件示例

```ini
; vnvme.conf - 虚拟 NVMe 控制器配置
[controller]
ControllerName=Virtual NVMe Controller
VendorId=0x1234
DeviceId=0x5678
SubsystemVendorId=0x1234
SubsystemId=0x5678

[namespace]
NamespaceSize=100G        ; 支持 K/M/G/T 后缀
BlockSize=512
BackendType=file          ; memory 或 file
BackendFile=C:\vnvme\disk.img

[storage]
Type=file
File=C:\vnvme\disk.img
ReadOnly=false
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

## 与其他方案的对比

| 方面 | StorPort Virtual Miniport | 本项目 (NVMe 仿真) | SPDK | QEMU NVMe |
|------|---------------------------|-------------------|------|-----------|
| **设备呈现** | SCSI 磁盘 | NVMe 控制器 | 用户态直接访问 | NVMe 控制器 |
| **驱动** | 自定义 miniport | stornvme.sys | 无内核驱动 | stornvme.sys |
| **nvme-cli** | ❌ 不支持 | ✅ 完全支持 | ✅ 需要改造 | ✅ 完全支持 |
| **用户态灵活性** | ❌ 全内核 | ✅ 类 SPDK | ✅ 完全用户态 | ⚠️ 需 QEMU |
| **与内核栈兼容** | ✅ 是 | ✅ 是 | ❌ 否 | ✅ 是 |
| **开发难度** | 中等 | 中高 | 高 | 低 (仅使用) |
| **性能开销** | 低 | 中 (轮询) | 最低 | 高 (虚拟化) |
| **独立运行** | ✅ 是 | ✅ 是 | ✅ 是 | ❌ 需 VM |
| **Windows 原生** | ✅ 是 | ✅ 是 | ⚠️ 移植中 | ❌ 需 QEMU |

---

## 项目组成 (v2 混合架构 - 已实现)

> 📋 **状态**: 截至 2025-12-23，核心功能已实现，待测试验证

### 代码统计

| 组件 | 源文件数 | 总行数 | 状态 |
|------|---------|--------|------|
| **vnvme.sys** (内核驱动) | 15 | ~5,863 | ✅ 主要功能已实现 |
| **vnvme-server.exe** (用户态服务) | 3 | ~1,967 | ✅ 核心功能已实现 |
| **vnvmectl.exe** (命令行工具) | 1 | ~512 | ✅ 基本功能已实现 |
| **共享头文件** | 3 | ~1,160 | ✅ 完整定义 |

```
virtual-nvme-driver/
├── vnvme/                  # 内核驱动 (单一驱动)
│   ├── vnvme.c             # ✅ 驱动入口、PnP/Power 回调 (292行)
│   ├── vnvme.h             # ✅ 主头文件、数据结构定义 (761行)
│   ├── ctrl_dev.c          # ✅ 控制设备、IOCTL 处理 (865行)
│   ├── bus.c               # ✅ 总线枚举、PDO 创建 (362行)
│   ├── pdo.c               # ✅ PDO PnP 处理 (496行)
│   ├── bar0.c              # ✅ BAR0 寄存器模拟 (246行)
│   ├── pcie_config.c       # ✅ PCIe 配置空间模拟 (375行)
│   ├── doorbell.c          # ✅ Doorbell 轮询处理 (240行)
│   ├── shm.c               # ✅ 共享内存管理 (263行)
│   ├── queue.c             # ✅ 队列管理 (185行)
│   ├── prp.c               # ✅ PRP 列表处理 (175行)
│   ├── admin_cmd.c         # ✅ Admin 命令处理 (686行)
│   ├── io_cmd.c            # ✅ I/O 命令处理 (639行)
│   ├── storage.c           # ✅ 内核存储后端 (733行)
│   ├── user_forward.c      # ✅ 用户态命令转发 (231行)
│   ├── utils.c             # ✅ 工具函数 (75行)
│   └── vnvme.inf           # ✅ 安装文件
│
├── vnvme-server/           # 用户态服务
│   ├── main.c              # ✅ 服务入口、配置加载、主循环 (621行)
│   ├── command_processor.c # ✅ 命令处理引擎 Admin+I/O (943行)
│   ├── backend.c           # ✅ 存储后端 内存+文件 (403行)
│   └── vnvme.conf.example  # ✅ 示例配置文件
│
├── vnvmectl/               # 命令行管理工具
│   └── main.c              # ✅ 主程序 version/status/list/create/delete (512行)
│
├── include/                # 共享头文件
│   ├── vnvme_common.h      # ✅ 公共定义、共享内存结构 (372行)
│   ├── vnvme_ioctl.h       # ✅ IOCTL 接口定义 (252行)
│   └── nvme_spec.h         # ✅ NVMe 规范定义 (536行)
│
├── docs/                   # 文档
│   ├── README.md           # 本文件
│   ├── ROADMAP.md          # 开发路线图
│   ├── architecture-v2.md  # 混合架构设计
│   └── ...                 # 其他文档 (20+ 文件)
│
├── scripts/                # 构建和安装脚本
├── templates/              # INF 模板等
└── tests/                  # 测试

---

## 参考资源

- [NVM Express Base Specification 2.0](https://nvmexpress.org/specifications/)
- [PCI Express Base Specification](https://pcisig.com/specifications)
- [QEMU NVMe 仿真源码](https://github.com/qemu/qemu/blob/master/hw/nvme/)
- [Windows 驱动开发文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/)
