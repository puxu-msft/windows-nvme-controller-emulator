# Virtual NVMe Driver (vnvme) - StorPort Virtual Miniport

## 项目概述

本项目实现一个 Windows StorPort Virtual Miniport 驱动程序，用于创建虚拟 NVMe SSD 设备。采用微软官方推荐的 StorPort Virtual Miniport 架构，通过设置 `VirtualDevice = TRUE` 实现虚拟存储设备支持。

### 核心特性

- **企业级架构**：基于 StorPort Virtual Miniport，获得自动队列管理、I/O 优化等企业级功能
- **高性能队列**：自动获得 250 队列深度（物理设备为 20），无需手动管理
- **MPIO 支持**：原生支持多路径 I/O，实现高可用性和负载均衡
- **灵活后端**：支持内存、文件、VHD、远程等多种后端存储类型
- **热插拔**：支持动态添加和移除虚拟磁盘
- **标准接口**：对上层应用呈现为标准 SCSI 磁盘设备

### 技术架构

```
┌─────────────────────────────────────────────────────────────────┐
│                    用户模式应用程序                              │
│              (磁盘管理、控制面板、文件资源管理器)                   │
└───────────────────────────┬─────────────────────────────────────┘
                            │ CreateFile, ReadFile, WriteFile
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    I/O 管理器 (IoManager)                        │
└───────────────────────────┬─────────────────────────────────────┘
                            │ IRP
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                 卷管理器 (volmgr.sys/partmgr.sys)                 │
└───────────────────────────┬─────────────────────────────────────┘
                            │ IRP
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                    磁盘类驱动 (disk.sys)                         │
│              SCSI 命令生成、磁盘几何计算、分区处理                  │
└───────────────────────────┬─────────────────────────────────────┘
                            │ SCSI Request Block (SRB)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                   StorPort 驱动 (storport.sys)                   │
│         队列管理(250深度)、电源管理、错误恢复、性能优化              │
│                   VirtualDevice = TRUE                           │
└───────────────────────────┬─────────────────────────────────────┘
                            │ HwStartIo, HwBuildIo
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│            ★ Virtual NVMe Miniport (vnvme.sys) ★                │
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │ SCSI 处理层 │  │   LUN 管理  │  │    后端存储抽象层        │  │
│  │             │  │             │  │                         │  │
│  │ ◆ INQUIRY   │  │ ◆ LUN 枚举  │  │ ◆ 内存后端 (RAM Disk)   │  │
│  │ ◆ READ CAP  │  │ ◆ 热插拔    │  │ ◆ 文件后端 (File I/O)   │  │
│  │ ◆ READ/WRITE│  │ ◆ 属性管理  │  │ ◆ VHD 后端 (虚拟硬盘)   │  │
│  │ ◆ UNMAP     │  │ ◆ MPIO 标识 │  │ ◆ 远程后端 (网络存储)   │  │
│  └─────────────┘  └─────────────┘  └─────────────────────────┘  │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                      后端存储介质                                │
│    [内存池]      [本地文件]      [VHD/VHDX]      [网络存储]       │
└─────────────────────────────────────────────────────────────────┘
```

### 驱动模型

| 项目 | 说明 |
|------|------|
| **驱动框架** | WDM (StorPort Miniport 必须使用 WDM，不支持 KMDF) |
| **端口驱动** | StorPort (storport.sys) |
| **设备类型** | Virtual Miniport (VirtualDevice = TRUE) |
| **命令接口** | SCSI/SRB (由 disk.sys 生成，不是直接 NVMe 命令) |
| **INF 类** | SCSIAdapter {4D36E97B-E325-11CE-BFC1-08002BE10318} |
| **加载顺序组** | "SCSI Miniport" |

---

## 文档结构

本项目文档按功能模块组织，覆盖从架构设计到测试部署的完整开发流程：

### 核心设计文档

| 文档 | 说明 |
|------|------|
| [architecture.md](architecture.md) | 系统架构设计，StorPort Virtual Miniport 原理，组件交互 |
| [driver-design.md](driver-design.md) | 驱动程序详细设计，模块划分，回调函数实现 |
| [data-structures.md](data-structures.md) | 核心数据结构定义，适配器/LUN/SRB 扩展，后端结构 |
| [command-handling.md](command-handling.md) | SCSI 命令处理流程，各命令实现细节 |

### 实现参考文档

| 文档 | 说明 |
|------|------|
| [backend-implementation.md](backend-implementation.md) | 存储后端实现指南，内存/文件/VHD 后端详细代码 |
| [ioctl-interface.md](ioctl-interface.md) | 管理接口设计，IOCTL 定义，用户态交互 |
| [nvme-spec.md](nvme-spec.md) | NVMe 规范参考，设备标识信息定义 |
| [inf-template.md](inf-template.md) | INF 安装文件模板，设备 ID 定义，安装脚本 |

### 开发运维文档

| 文档 | 说明 |
|------|------|
| [build-guide.md](build-guide.md) | 构建环境配置，项目设置，编译部署流程 |
| [power-management.md](power-management.md) | 电源管理实现，适配器控制回调 |
| [testing.md](testing.md) | 测试策略，验证方法，自动化测试脚本 |
| [error-codes.md](error-codes.md) | 错误码定义，SCSI 状态码映射 |
| [troubleshooting.md](troubleshooting.md) | 故障排查指南，常见问题解决方案 |

---

## 快速开始

### 环境要求

- **操作系统**：Windows 10 版本 1903 或更高 / Windows 11 / Windows Server 2019+
- **开发工具**：Visual Studio 2022 (v17.0+)
- **WDK 版本**：Windows Driver Kit 10.0.22621.0 或更高
- **SDK 版本**：Windows SDK 10.0.22621.0 或更高
- **目标平台**：x64 (本项目仅支持 64 位)

### 构建步骤

1. **克隆项目**
   ```powershell
   git clone <repository-url>
   cd virtual-nvme-driver
   ```

2. **打开解决方案**
   ```powershell
   # 使用 Visual Studio 2022 打开
   start vnvme.sln
   ```

3. **配置构建**
   - 选择配置: `Debug` 或 `Release`
   - 选择平台: `x64`
   - 确保 WDK 已正确安装

4. **构建驱动**
   ```powershell
   msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64
   ```

5. **签名驱动** (测试环境)
   ```powershell
   # 创建测试证书
   makecert -r -pe -ss PrivateCertStore -n "CN=VNvme Test" vnvme_test.cer
   
   # 签名驱动
   signtool sign /v /s PrivateCertStore /n "VNvme Test" /t http://timestamp.digicert.com vnvme.sys
   ```

### 安装部署

1. **启用测试签名** (测试环境)
   ```cmd
   bcdedit /set testsigning on
   # 重启系统
   ```

2. **安装驱动**
   ```powershell
   # 使用设备管理器添加旧式硬件
   # 或使用命令行
   pnputil /add-driver vnvme.inf /install
   ```

3. **创建虚拟磁盘**
   ```powershell
   # 使用管理工具创建 1GB 内存磁盘
   vnvme-cli.exe create --size 1G --backend memory
   ```

4. **验证安装**
   ```powershell
   # 检查设备管理器
   Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Virtual NVMe*" }
   
   # 检查磁盘
   Get-Disk | Where-Object { $_.FriendlyName -like "*VNvme*" }
   ```

---

## 项目结构

```
virtual-nvme-driver/
├── docs/                           # 文档目录
│   ├── README.md                   # 本文件 - 项目概述
│   ├── architecture.md             # 系统架构设计
│   ├── driver-design.md            # 驱动程序设计
│   ├── data-structures.md          # 数据结构定义
│   ├── command-handling.md         # 命令处理流程
│   ├── ioctl-interface.md          # 管理接口设计
│   ├── nvme-spec.md                # NVMe 规范参考
│   ├── inf-template.md             # INF 模板
│   ├── build-guide.md              # 构建指南
│   ├── power-management.md         # 电源管理
│   ├── testing.md                  # 测试指南
│   ├── error-codes.md              # 错误码定义
│   └── troubleshooting.md          # 故障排查
│
├── src/                            # 源代码目录
│   ├── vnvme/                      # 主驱动源码
│   │   ├── vnvme_main.c            # 驱动入口、初始化
│   │   ├── vnvme_adapter.c         # 适配器管理
│   │   ├── vnvme_lun.c             # LUN 管理
│   │   ├── vnvme_scsi.c            # SCSI 命令处理
│   │   ├── vnvme_backend.c         # 后端抽象层
│   │   ├── backend_memory.c        # 内存后端实现
│   │   ├── backend_file.c          # 文件后端实现
│   │   ├── backend_vhd.c           # VHD 后端实现
│   │   ├── backend_remote.c        # 远程后端实现
│   │   ├── vnvme.h                 # 主头文件
│   │   ├── vnvme_scsi.h            # SCSI 定义
│   │   └── vnvme_backend.h         # 后端接口定义
│   │
│   └── common/                     # 公共代码
│       ├── vnvme_ioctl.h           # IOCTL 定义 (用户态/内核态共享)
│       └── vnvme_types.h           # 公共类型定义
│
├── tools/                          # 管理工具
│   ├── vnvme-cli/                  # 命令行管理工具
│   │   ├── main.cpp
│   │   ├── commands.cpp
│   │   └── vnvme-cli.vcxproj
│   │
│   └── vnvme-gui/                  # GUI 管理工具 (可选)
│
├── test/                           # 测试代码
│   ├── unit/                       # 单元测试
│   ├── integration/                # 集成测试
│   └── stress/                     # 压力测试
│
├── vnvme.sln                       # Visual Studio 解决方案
├── vnvme.vcxproj                   # 驱动项目文件
├── vnvme.inf                       # 驱动安装文件
├── LICENSE                         # 许可证
└── README.md                       # 项目根 README
```

---

## 技术规格

### 支持的 SCSI 命令

| 命令 | 操作码 | 说明 |
|------|--------|------|
| TEST UNIT READY | 0x00 | 设备就绪检测 |
| REQUEST SENSE | 0x03 | 获取错误信息 |
| INQUIRY | 0x12 | 设备识别 (含 VPD 页) |
| MODE SENSE(6/10) | 0x1A/0x5A | 模式参数查询 |
| READ CAPACITY(10/16) | 0x25/0x9E | 容量查询 |
| READ(6/10/12/16) | 0x08/0x28/0xA8/0x88 | 数据读取 |
| WRITE(6/10/12/16) | 0x0A/0x2A/0xAA/0x8A | 数据写入 |
| SYNCHRONIZE CACHE | 0x35/0x91 | 缓存同步 |
| UNMAP | 0x42 | TRIM/取消映射 |
| REPORT LUNS | 0xA0 | LUN 枚举 |

### 支持的 VPD 页

| VPD 页 | 页码 | 说明 |
|--------|------|------|
| Supported Pages | 0x00 | 支持的 VPD 页列表 |
| Unit Serial Number | 0x80 | 设备序列号 |
| Device Identification | 0x83 | 设备标识 (NAA, 供应商) |
| Block Limits | 0xB0 | 块限制信息 |
| Block Device Characteristics | 0xB1 | 块设备特性 (SSD) |
| Logical Block Provisioning | 0xB2 | 逻辑块配置 (UNMAP) |

### 后端类型

| 类型 | 标识符 | 说明 |
|------|--------|------|
| 内存后端 | VNVME_BACKEND_MEMORY | RAM 磁盘，最高性能，重启丢失数据 |
| 文件后端 | VNVME_BACKEND_FILE | 使用本地文件作为存储，持久化 |
| VHD 后端 | VNVME_BACKEND_VHD | 使用 VHD/VHDX 格式，兼容 Hyper-V |
| 远程后端 | VNVME_BACKEND_REMOTE | 网络存储 (iSCSI-like)，分布式 |

### 性能指标 (参考)

| 指标 | 内存后端 | 文件后端 | VHD 后端 |
|------|----------|----------|----------|
| 顺序读取 | ~10 GB/s | ~500 MB/s | ~400 MB/s |
| 顺序写入 | ~10 GB/s | ~400 MB/s | ~350 MB/s |
| 随机读取 (4K) | ~500K IOPS | ~50K IOPS | ~40K IOPS |
| 随机写入 (4K) | ~500K IOPS | ~30K IOPS | ~25K IOPS |
| 延迟 | < 10 μs | < 100 μs | < 150 μs |

*注：实际性能取决于硬件配置和系统负载*

---

## 企业功能

### MPIO (多路径 I/O)

本驱动原生支持 Windows MPIO：

1. **VPD 0x83 设备标识**：提供唯一设备标识符供 MPIO 识别
2. **ALUA 支持**：非对称逻辑单元访问（可选）
3. **路径故障转移**：自动检测路径故障并切换
4. **负载均衡**：支持轮询、加权、最少队列深度等策略

### 高可用性

- **热插拔**：运行时添加/移除虚拟磁盘
- **在线扩容**：动态扩展磁盘容量
- **快照支持**：后端可实现快照功能
- **数据保护**：支持持久化后端确保数据安全

---

## 许可证

本项目采用 MIT 许可证。详见 [LICENSE](../LICENSE) 文件。

---

## 贡献指南

欢迎贡献代码和文档！请遵循以下流程：

1. Fork 本仓库
2. 创建功能分支 (`git checkout -b feature/amazing-feature`)
3. 提交更改 (`git commit -m 'Add amazing feature'`)
4. 推送分支 (`git push origin feature/amazing-feature`)
5. 创建 Pull Request

### 代码规范

- 使用 4 空格缩进
- 函数名使用 `VNvme` 前缀
- 遵循 Windows 驱动开发最佳实践
- 所有公共 API 必须有文档注释

---

## 参考资料

- [StorPort Driver Documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storport-driver)
- [StorPort Miniport Drivers](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storport-miniport-drivers)
- [Virtual Miniport Drivers](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/overview-of-storage-virtual-miniport-drivers)
- [NVMe Specification](https://nvmexpress.org/specifications/)
- [Windows Driver Kit (WDK)](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
- [SCSI Reference](https://www.t10.org/drafts.htm)

---

## 联系方式

如有问题或建议，请通过以下方式联系：

- **Issue Tracker**: GitHub Issues
- **Email**: [项目维护者邮箱]

---

*最后更新: 2024*
