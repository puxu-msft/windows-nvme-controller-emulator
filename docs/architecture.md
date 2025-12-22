# 系统架构设计

## 项目目标

本项目实现一个**真正的软件 NVMe 控制器仿真**，目标是让 Windows 将我们的虚拟设备识别为**真实的 NVMe 设备**，而不是普通的 SCSI 磁盘。

### 核心目标

| 目标 | 说明 |
|------|------|
| **真实 NVMe 设备呈现** | 在设备管理器中显示为 NVMe 控制器，而非普通磁盘 |
| **原生 NVMe 驱动兼容** | 使用 Windows 原生 stornvme.sys 驱动 |
| **NVMe 工具支持** | nvme-cli、厂商工具可识别和管理 |
| **完整协议实现** | 支持 Admin Queue、I/O Queue、NVMe 命令集 |
| **灵活后端存储** | 支持内存、文件、VHD 等后端 |

---

## 技术方案选择

### 方案对比

在 Windows 上实现虚拟存储设备有多种方案：

| 方案 | 设备呈现 | NVMe 工具 | 复杂度 | 适用场景 |
|------|---------|----------|--------|---------|
| StorPort Virtual Miniport | SCSI 磁盘 | ❌ | 低 | 普通虚拟磁盘 |
| **虚拟 PCIe + NVMe 仿真** | NVMe 控制器 | ✅ | 高 | 真实 NVMe 模拟 |
| NVMe over Fabrics | NVMe-oF 设备 | ✅ | 中 | 网络存储 |
| Hyper-V 虚拟 NVMe | VM 内 NVMe | ✅ | 中 | 虚拟机场景 |

### 选定方案：虚拟 PCIe 总线 + NVMe 控制器仿真

这是在 Windows 宿主机上实现真正 NVMe 设备仿真的**唯一完整方案**。

#### 为什么这样选择？

1. **stornvme.sys 只识别 PCI 设备**
   - Windows 原生 NVMe 驱动绑定到 PCI 总线上的 NVMe 设备
   - 设备必须有正确的 Class Code (0x010802)
   - 必须提供 NVMe 规范的 BAR0 寄存器空间

2. **需要完整的 NVMe 控制器仿真**
   - stornvme.sys 会直接访问 NVMe 寄存器
   - 必须正确响应 Admin Queue 命令
   - 必须支持 I/O Queue 创建和命令处理

3. **中断机制仿真**
   - stornvme.sys 依赖 MSI-X 中断通知命令完成
   - 需要仿真中断注入机制

---

## 整体架构

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
│                                                                  │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │           Windows 原生 NVMe 驱动 (stornvme.sys)            │  │
│  │                                                           │  │
│  │  • NVMe 命令构造和解析                                    │  │
│  │  • Submission/Completion Queue 管理                       │  │
│  │  • Admin 命令处理 (Identify, Create Queue, ...)           │  │
│  │  • I/O 命令处理 (Read, Write, Flush, ...)                 │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                   │
│                              │ MMIO 访问 (NVMe 寄存器)           │
│                              │ DMA 访问 (SQ/CQ 内存)             │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │           ★ NVMe 控制器仿真层 (vnvme_emu.sys) ★            │  │
│  │                                                           │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌───────────────────┐  │  │
│  │  │ 寄存器仿真  │  │ Queue 引擎  │  │ NVMe 命令处理     │  │  │
│  │  │             │  │             │  │                   │  │  │
│  │  │ CAP/VS/CC   │  │ Admin SQ/CQ │  │ Identify          │  │  │
│  │  │ CSTS/AQA    │  │ I/O SQ/CQ   │  │ Read/Write        │  │  │
│  │  │ ASQ/ACQ     │  │ Doorbell    │  │ Flush/DSM         │  │  │
│  │  └─────────────┘  └─────────────┘  └───────────────────┘  │  │
│  │                                                           │  │
│  │  ┌─────────────┐  ┌─────────────────────────────────────┐ │  │
│  │  │ 中断仿真    │  │ 后端存储适配器                       │ │  │
│  │  │ MSI-X       │  │ Memory / File / VHD                 │ │  │
│  │  └─────────────┘  └─────────────────────────────────────┘ │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                   │
│                              │ 设备枚举 / PnP                    │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │             虚拟 PCIe 总线驱动 (vnvme_bus.sys)              │  │
│  │                                                           │  │
│  │  • 虚拟 PCIe 总线枚举                                     │  │
│  │  • PCIe 配置空间仿真 (Vendor/Device ID, Class Code, BAR)  │  │
│  │  • BAR 内存映射管理                                       │  │
│  │  • MSI-X 中断路由                                         │  │
│  │  • 热插拔支持                                             │  │
│  └───────────────────────────────────────────────────────────┘  │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────┐
│                    存储后端 (内存 / 文件 / VHD)                   │
└─────────────────────────────────────────────────────────────────┘
```

---

## 驱动组成

### 1. vnvme_bus.sys - 虚拟 PCIe 总线驱动

**角色**：根驱动，负责设备枚举和 PCI 资源仿真

**职责**：

| 功能 | 说明 |
|------|------|
| 总线枚举 | 在虚拟 PCIe 总线上枚举子设备 |
| PnP 处理 | 响应 IRP_MN_QUERY_DEVICE_RELATIONS 等 |
| 配置空间 | 仿真 256 字节 PCI 配置空间 |
| BAR 分配 | 为 NVMe 寄存器分配虚拟物理地址 |
| 资源仲裁 | 处理 IRP_MN_QUERY_RESOURCES 等 |
| 中断路由 | 配置和路由 MSI-X 中断 |

**设备标识**：

```
Hardware ID:  PCI\VEN_1B36&DEV_0010&SUBSYS_00001B36&REV_02
              │       │       │              │
              │       │       │              └── Revision
              │       │       └── Subsystem ID
              │       └── Device ID (NVMe Controller)
              └── Vendor ID (如使用 Red Hat 的 0x1B36)

Compatible ID: PCI\CC_010802  (NVMe Mass Storage Controller)
```

### 2. vnvme_emu.sys - NVMe 控制器仿真驱动

**角色**：功能驱动，完整仿真 NVMe 控制器行为

**职责**：

| 功能 | 说明 |
|------|------|
| 寄存器仿真 | 实现 NVMe 规范定义的所有控制器寄存器 |
| Admin Queue | 处理 Admin Submission/Completion Queue |
| I/O Queue | 处理多个 I/O Submission/Completion Queue |
| 命令处理 | 实现 Identify, Read, Write, Flush 等命令 |
| Doorbell | 监控并响应 SQ Tail / CQ Head Doorbell 写入 |
| 中断触发 | 命令完成时触发 MSI-X 中断 |
| 后端交互 | 将 NVMe 命令翻译为后端存储操作 |

### 3. vnvmectl.exe - 用户态管理工具

**角色**：配置和管理虚拟 NVMe 设备

```powershell
# 创建虚拟 NVMe 控制器 (1个命名空间, 100GB, 文件后端)
vnvmectl create --size 100GB --backend file --path C:\vnvme\disk.img

# 列出所有虚拟 NVMe 设备
vnvmectl list

# 添加命名空间到现有控制器
vnvmectl add-namespace --controller 0 --size 50GB --backend memory

# 删除虚拟设备
vnvmectl delete --controller 0

# 查看详细信息
vnvmectl info --controller 0
```

---

## 关键技术点

### 1. stornvme.sys 如何发现设备

Windows 原生 NVMe 驱动 (stornvme.sys) 通过以下方式发现设备：

```
1. PCI 总线枚举
   └─► 发现 Class Code = 0x010802 的设备
       └─► 检查 Vendor/Device ID
           └─► 加载 stornvme.sys

2. stornvme.sys 初始化
   └─► 读取 BAR0 获取寄存器基址
       └─► 读取 CAP 寄存器获取控制器能力
           └─► 读取 VS 寄存器获取 NVMe 版本
               └─► 配置 Admin Queue (AQA/ASQ/ACQ)
                   └─► 设置 CC.EN = 1 启用控制器
                       └─► 等待 CSTS.RDY = 1
                           └─► 发送 Identify 命令
```

### 2. 寄存器访问拦截

stornvme.sys 通过 MMIO 访问 NVMe 寄存器。我们需要拦截这些访问：

```c
// BAR0 映射的虚拟地址被 stornvme 访问时:

// 读操作 (如读取 CAP 寄存器)
ULONG64 cap = *(volatile ULONG64*)BarBase;
// 我们的处理: 返回仿真的 CAP 值

// 写操作 (如写入 CC 寄存器)
*(volatile ULONG*)((PUCHAR)BarBase + 0x14) = cc_value;
// 我们的处理: 更新控制器状态，可能需要启用/禁用控制器

// Doorbell 写入 (如通知新命令)
*(volatile ULONG*)((PUCHAR)BarBase + 0x1000) = new_sq_tail;
// 我们的处理: 触发命令处理
```

### 3. Queue 内存访问

NVMe 使用主机内存中的 Submission Queue 和 Completion Queue：

```
┌──────────────────────────────────────────────────────────────┐
│                    主机物理内存                               │
│                                                              │
│  ASQ (Admin Submission Queue)    ACQ (Admin Completion Queue)│
│  ┌─────┬─────┬─────┬─────┐       ┌─────┬─────┬─────┬─────┐   │
│  │ Cmd │ Cmd │ ... │ Cmd │       │ Cpl │ Cpl │ ... │ Cpl │   │
│  │  0  │  1  │     │  N  │       │  0  │  1  │     │  N  │   │
│  └─────┴─────┴─────┴─────┘       └─────┴─────┴─────┴─────┘   │
│    ▲                               │                         │
│    │ 我们读取命令                   │ 我们写入完成条目        │
│    │                               ▼                         │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │                    vnvme_emu.sys                         │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
```

### 4. 中断触发机制

命令完成后需要通知 stornvme.sys：

```
命令完成流程:

1. 将 Completion Entry 写入 CQ
2. 更新 CQ Phase Tag
3. 触发 MSI-X 中断
   └─► stornvme ISR 被调用
       └─► 检查 CQ 中的新条目
           └─► 处理完成的命令
               └─► 写入 CQ Head Doorbell
```

---

## 内存和寻址

### NVMe PRP (Physical Region Page)

NVMe 使用 PRP 描述数据缓冲区的物理地址：

```c
// 简单情况: 数据 <= 1 页
// PRP1 = 数据缓冲区物理地址
// PRP2 = 0

// 数据 <= 2 页
// PRP1 = 第一页物理地址
// PRP2 = 第二页物理地址

// 数据 > 2 页
// PRP1 = 第一页物理地址
// PRP2 = PRP List 物理地址 (包含剩余页的地址列表)
```

我们需要：
1. 解析 PRP1/PRP2
2. 将物理地址映射到虚拟地址 (MmMapIoSpace 或 MmGetSystemAddressForMdlSafe)
3. 读取或写入数据

---

## 状态机

### 控制器状态机

```
                    ┌─────────────┐
                    │   Disabled  │◄──────────────────┐
                    │  (初始状态) │                   │
                    └──────┬──────┘                   │
                           │ CC.EN = 1                │ CC.EN = 0
                           ▼                          │
                    ┌─────────────┐                   │
                    │  Enabling   │                   │
                    │  (初始化中) │                   │
                    └──────┬──────┘                   │
                           │ 初始化完成               │
                           ▼                          │
                    ┌─────────────┐                   │
                    │   Enabled   │───────────────────┘
                    │   (运行中)  │
                    └─────────────┘
```

### NVMe 控制器寄存器状态

| 寄存器 | 控制器禁用时 | 启用过程中 | 控制器就绪 |
|--------|-------------|-----------|-----------|
| CC.EN | 0 | 1 | 1 |
| CSTS.RDY | 0 | 0→1 | 1 |
| CSTS.CFS | 0 | 0 | 0 (正常) / 1 (致命错误) |

---

## 技术挑战

### 1. MMIO 拦截

**问题**：Windows 没有直接的 API 拦截 MMIO 访问

**解决方案**：
- 使用 Memory-Mapped Section 配合 Exception Handler
- 或使用 Filter Driver 拦截 MmMapIoSpace 调用
- 或在 vnvme_bus 层提供虚拟 BAR 地址，完全控制访问

### 2. 中断注入

**问题**：如何在没有物理硬件的情况下触发中断

**解决方案**：
- 调用 IoRequestDpc 请求延迟过程调用
- 使用 KeInsertQueueDpc 插入 DPC
- 与 stornvme 共享现有中断资源

### 3. 物理内存访问

**问题**：需要访问 stornvme 分配的 SQ/CQ 物理内存

**解决方案**：
- 使用 MmMapIoSpace 映射物理地址
- 使用 MmGetPhysicalAddress 和 MmMapLockedPages

### 4. 性能

**问题**：软件仿真的性能开销

**优化方向**：
- 减少上下文切换
- 批量处理多个命令
- 使用无锁数据结构
- DPC 级别处理关键路径

---

## 开发路线图

### Phase 1: 基础框架 (Week 1-2)

- [ ] vnvme_bus.sys 骨架
- [ ] 设备枚举 (IRP_MN_QUERY_DEVICE_RELATIONS)
- [ ] PCIe 配置空间基础
- [ ] 让 Windows 识别为 PCI 设备

### Phase 2: 让 stornvme 加载 (Week 3-4)

- [ ] 正确的 Class Code (0x010802)
- [ ] BAR0 分配和映射
- [ ] 基础寄存器仿真 (CAP, VS)
- [ ] stornvme.sys 成功绑定

### Phase 3: 控制器初始化 (Week 5-6)

- [ ] CC/CSTS 寄存器
- [ ] Admin Queue 配置 (AQA, ASQ, ACQ)
- [ ] 控制器状态机
- [ ] stornvme 完成初始化

### Phase 4: Admin 命令 (Week 7-8)

- [ ] Identify Controller
- [ ] Identify Namespace
- [ ] Create I/O CQ
- [ ] Create I/O SQ
- [ ] Set Features / Get Features

### Phase 5: I/O 命令 (Week 9-10)

- [ ] Read 命令
- [ ] Write 命令
- [ ] Flush 命令
- [ ] PRP 解析

### Phase 6: 完善和测试 (Week 11-12)

- [ ] MSI-X 中断完善
- [ ] 错误处理
- [ ] 性能优化
- [ ] 兼容性测试

---

## 参考资源

- [NVM Express Base Specification 2.0](https://nvmexpress.org/specifications/)
- [PCI Local Bus Specification 3.0](https://pcisig.com/specifications)
- [QEMU NVMe 实现](https://github.com/qemu/qemu/blob/master/hw/nvme/ctrl.c)
- [Linux NVMe 驱动](https://github.com/torvalds/linux/tree/master/drivers/nvme)
- [Windows 驱动开发](https://learn.microsoft.com/en-us/windows-hardware/drivers/)

