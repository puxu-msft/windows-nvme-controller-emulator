# 开发路线图

本文档定义 Virtual NVMe 项目的开发阶段、里程碑和详细任务列表。

## 任务状态图例

| 标记 | 含义 |
|------|------|
| `[x]` | ✅ 已完成 - 功能已完整实现并可用 |
| `[~]` | 🔶 部分完成 - 框架/存根已有，标记 TODO 待完善 |
| `[ ]` | ⬜ 未开始 - 尚未实现 |

## 项目概述

```
目标: 创建一个完整的软件 NVMe 控制器仿真器
架构: vnvme.sys (内核驱动) + vnvme-server.exe (用户态服务)
最终效果: Windows 设备管理器中显示为真正的 NVMe 控制器
```

---

## 阶段概览

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           开发阶段                                       │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Phase 1          Phase 2          Phase 3          Phase 4             │
│  ════════         ════════         ════════         ════════            │
│  项目骨架          PDO/PCIe         用户态通信        NVMe 命令           │
│  2 周              2 周             2 周             3 周               │
│     │                 │                │                │               │
│     ▼                 ▼                ▼                ▼               │
│  ┌─────┐          ┌─────┐          ┌─────┐          ┌─────┐            │
│  │ FDO │          │ PDO │          │共享 │          │Admin│            │
│  │创建 │───────▶│BAR0 │───────▶│内存 │───────▶│ I/O │            │
│  │加载 │          │PCIe │          │IOCTL│          │命令 │            │
│  └─────┘          └─────┘          └─────┘          └─────┘            │
│                                                          │               │
│                                                          ▼               │
│                              Phase 5          Phase 6                    │
│                              ════════         ════════                   │
│                              存储后端          测试&优化                  │
│                              2 周             2 周                       │
│                                 │                │                       │
│                                 ▼                ▼                       │
│                             ┌─────┐          ┌─────┐                    │
│                             │文件 │          │功能 │                    │
│                             │内存 │───────▶│性能 │ → 发布              │
│                             │后端 │          │测试 │                    │
│                             └─────┘          └─────┘                    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: 项目骨架和 FDO 创建 (预计 2 周)

### 目标
- 建立完整的项目结构
- 实现驱动加载和 FDO 创建
- 创建控制设备 `\\.\VNVMEControl`

### 里程碑
- [x] 驱动能够成功编译 (vnvme.sys ~19KB)
- [x] 用户态程序可以编译 (vnvmectl.exe, vnvme-server.exe)
- [x] 驱动框架完整实现 (PnP/Power 回调)
- [ ] 驱动能够成功加载和卸载 (需要测试签名模式)
- [ ] 设备管理器中看到 "Virtual NVMe Bus Controller"
- [ ] 用户态程序可以打开控制设备

### 详细任务

#### 1.1 项目结构创建
- [x] 创建 Visual Studio 解决方案 `vnvme.sln`
- [x] 创建 KMDF 驱动项目 `vnvme`
- [x] 创建用户态服务项目 `vnvme-server`
- [x] 创建命令行工具项目 `vnvmectl`
- [x] 创建共享头文件 `include/vnvme_common.h`
- [x] 创建 IOCTL 定义 `include/vnvme_ioctl.h`
- [x] 创建 NVMe 规范定义 `include/nvme_spec.h`

#### 1.2 内核驱动入口 (vnvme.c)
- [x] 实现 `DriverEntry()` - 创建 WDF 驱动对象
- [x] 实现 `VnvmeEvtDriverContextCleanup()` - 驱动卸载清理
- [x] 实现 `VnvmeEvtDeviceAdd()` - FDO 创建
- [x] 配置 WDF 设备属性和 PnP 回调
- [x] 实现 `VNVME_FDO_CONTEXT` 初始化

#### 1.3 控制设备 (ctrl_dev.c)
- [x] 实现 `VnvmeCreateControlDevice()` - 创建 `\\.\VNVMEControl`
- [x] 实现 `VnvmeEvtIoDeviceControl()` - IOCTL 分发
- [x] 实现基本 IOCTL: `IOCTL_VNVME_GET_VERSION`
- [x] 实现设备访问控制 (仅管理员)

#### 1.4 INF 和构建
- [x] 复制 `templates/vnvme.inf` 到项目
- [x] 配置驱动签名 (测试证书)
- [x] 创建 `scripts/build.ps1`
- [x] 创建 `scripts/install.ps1`
- [ ] 验证驱动加载/卸载 (需要测试签名模式)

#### 1.5 用户态工具骨架 (vnvmectl)
- [x] 实现 CLI 框架 (参数解析)
- [x] 实现 `vnvmectl version` 命令
- [x] 实现 `vnvmectl status` 命令
- [x] 实现 `vnvmectl list` 命令
- [x] 实现 `vnvmectl test` 命令
- [ ] 验证与控制设备的通信 (需要驱动加载)

### 验收标准
```powershell
# 1. 驱动加载成功
devcon install vnvme.inf ROOT\VNVME
# 应返回成功

# 2. 设备出现在设备管理器
Get-PnpDevice | Where-Object { $_.FriendlyName -like "*VNVME*" }
# 应看到 "Virtual NVMe Bus Controller"

# 3. 用户态工具可通信
vnvmectl version
# 应显示版本信息
```

---

## Phase 2: PDO 创建和 PCIe/BAR0 仿真 (预计 2 周)

### 目标
- 创建子设备 PDO
- 实现 PCIe 配置空间仿真
- 分配并初始化 BAR0 内存
- 使 stornvme.sys 加载到 PDO

### 里程碑
- [ ] PDO 出现在设备管理器 (需验证)
- [ ] stornvme.sys 成功附加到 PDO (需验证)
- [x] BAR0 内存分配成功，寄存器已初始化
- [x] PCIe 配置空间实现，BUS_INTERFACE_STANDARD 就绪
- [x] PnP 查询函数全部实现 (Capabilities, BusInfo, Resources)

### 详细任务

#### 2.1 PDO 创建 (bus.c)
- [x] 实现 `VnvmeCreateVirtualController()` - 高层 API，IOCTL 调用入口
- [x] 实现 `VnvmeCreateControllerPdo()` - 低层实现，实际 PDO 创建
- [x] 实现 `VnvmeDeleteVirtualController()` - 高层删除 API
- [x] 实现 `VnvmeDeleteControllerPdo()` - 低层删除实现
- [x] 实现 `VnvmeFindController()` - 查找控制器
- [x] 设置 PDO 硬件 ID: `PCI\VEN_1B36&DEV_0010&REV_01`
- [x] 设置 PDO 设备描述和位置
- [x] 定义 `VNVME_PDO_CONTEXT` 结构
- [x] 设置 PDO 与 FDO 的父子关系 (LIST_ENTRY)

#### 2.2 PDO PnP 处理 (pdo.c)
- [x] 创建 pdo.c 文件框架
- [x] 实现 `VnvmePdoQueryDeviceId()` - 设备 ID 查询
- [x] 实现 `VnvmePdoQueryDeviceText()` - 设备描述查询
- [x] 实现 `VnvmePdoEvtDevicePrepareHardware()` - 硬件准备 (处理 IRP_MN_START_DEVICE)
- [x] 实现 `VnvmePdoEvtDeviceReleaseHardware()` - 硬件释放 (处理 IRP_MN_STOP_DEVICE)
- [x] 实现 `VnvmePdoEvtDeviceD0Entry/D0Exit()` - 电源状态
- [x] 实现 `VnvmePdoQueryCapabilities()` - 设备能力
- [x] 实现 `VnvmePdoQueryBusInformation()` - 总线类型 PCIe

#### 2.3 资源报告 (pdo.c)
- [x] 实现 `VnvmePdoQueryResources()` - 报告 BAR0 内存资源
- [x] 实现 `VnvmePdoQueryResourceRequirements()` - 资源需求

#### 2.4 BAR0 内存 (bar0.c)
- [x] 实现 `VnvmeAllocateBar0()` - 分配 64KB 连续物理内存
- [x] 实现 `VnvmeFreeBar0()` - 释放 BAR0 内存
- [x] 实现 `VnvmeInitializeBar0Registers()` - 初始化 NVMe 寄存器
- [x] 设置 CAP 寄存器 (能力)
- [x] 设置 VS 寄存器 (版本 1.4)
- [x] 初始化 CSTS 为 0 (等待 CC.EN)

#### 2.5 PCIe 配置空间 (pcie_config.c)
- [x] 实现 `VnvmePdoQueryInterface()` - BUS_INTERFACE_STANDARD
- [x] 实现 `VnvmeAllocatePcieConfig()` - 分配配置空间
- [x] 实现 `VnvmeFreePcieConfig()` - 释放配置空间
- [x] 实现 `VnvmeInitializePcieConfig()` - 初始化配置空间
- [x] 实现 `VnvmeReadPcieConfig()` - 读取配置空间
- [x] 实现 `VnvmeWritePcieConfig()` - 写入配置空间
- [x] 填充 256 字节配置头
- [ ] 填充 PCIe 扩展能力 (PM, MSI-X) - Phase 3

#### 2.6 Doorbell 轮询 (doorbell.c)
- [x] 实现 `VnvmeInitializePollingTimer()` - 初始化定时器
- [x] 实现 `VnvmeStartPollingTimer()` - 启动定时器
- [x] 实现 `VnvmeStopPollingTimer()` - 停止定时器
- [x] 实现 `VnvmeEvtPollingTimer()` - 定时器回调
- [x] 实现 `VnvmeProcessDoorbells()` - 处理 Doorbell 变化
- [x] CC 寄存器变化检测 (CC.EN 0→1, 1→0)
- [x] 调用 `VnvmeInitializeAdminQueues()` 当 CC.EN 设置

#### 2.7 子设备 INF
- [ ] 验证 `templates/vnvme_child.inf` 与 PDO ID 匹配
- [ ] 安装子设备 INF
- [ ] 验证 stornvme.sys 加载

### 验收标准
```powershell
# 1. 子设备出现
Get-PnpDevice -Class SCSIAdapter | Where-Object { $_.InstanceId -like "*1B36*" }
# 应看到设备

# 2. stornvme 附加
Get-PnpDeviceProperty -InstanceId "<PDO ID>" -KeyName DEVPKEY_Device_Service
# 应显示 "stornvme"

# 3. BAR0 资源分配
# 使用 WinDbg: !devext <pdo> 查看资源
```

---

## Phase 3: 用户态通信和共享内存 (预计 2 周)

### 目标
- 实现内核/用户态共享内存
- 实现完整的 IOCTL 接口
- 创建 vnvme-server.exe 基础框架

### 里程碑
- [ ] 共享内存映射成功
- [ ] IOCTL 通信正常工作
- [ ] vnvme-server 可以启动并连接

### 详细任务

#### 3.1 共享内存 (shm.c)
- [x] 实现 `VnvmeAllocateShm()` - 分配 64MB
- [x] 初始化控制块 (魔数、版本、指针)
- [x] 初始化提交环和完成环
- [x] 初始化数据缓冲区

#### 3.2 用户态映射 (ctrl_dev.c)
- [x] 实现 `IOCTL_VNVME_MAP_SHARED_MEMORY` - 映射到用户空间
- [x] 实现 `IOCTL_VNVME_USER_READY` - 用户态就绪通知
- [~] 实现 `IOCTL_VNVME_GET_COMMAND_EVENT` - 获取事件句柄 (轮询模式，TODO: 事件机制)
- [x] 实现 `IOCTL_VNVME_HEARTBEAT` - 心跳
- [x] 实现 `IOCTL_VNVME_SUBMIT_COMPLETIONS` - 提交完成
- [x] 实现 `IOCTL_VNVME_CREATE_CONTROLLER` - 创建控制器
- [x] 实现 `IOCTL_VNVME_DELETE_CONTROLLER` - 删除控制器
- [x] 实现 `IOCTL_VNVME_LIST_CONTROLLERS` - 列出控制器

#### 3.3 vnvme-server 框架 (main.c)
- [x] 创建 vnvme-server 项目骨架
- [x] 实现命令行参数解析 (--backend, --file, --size, --debug, --config)
- [x] 实现配置文件加载 (INI 格式)
- [x] 打开控制设备 `\\.\VNVMEControl`
- [x] 映射共享内存
- [x] 发送 USER_READY 通知
- [x] 实现主循环和心跳机制
- [x] 实现实际命令处理 (CmdProcessorRun)

#### 3.4 命令循环 (command_processor.c)
- [x] 实现主事件循环 (CmdProcessorRun)
- [x] 检查通知环获取命令
- [x] 从 SQ 读取命令
- [x] 分发到处理函数
- [x] 写入完成到 CQ
- [x] 更新统计信息

#### 3.5 心跳和错误恢复
- [x] 实现心跳定时器 (每秒发送)
- [ ] 实现用户态崩溃检测
- [x] 实现优雅关闭处理

#### 3.6 优雅关闭 (Graceful Shutdown)

**目标**: 驱动卸载时安全停止所有操作，避免数据丢失和蓝屏

**内核侧 (vnvme.sys)**:
- [x] 添加 `ShutdownEvent` 到 FDO 上下文
- [x] 添加 `ShutdownRequested` 标志
- [ ] 添加 `ControlQueue` 用于等待 IOCTL 完成
- [x] 在 `VnvmeEvtDeviceD0Exit` 中触发关闭事件
- [ ] 使用 `WdfIoQueueStop()` 停止接收新请求
- [x] 等待所有待处理命令完成 (超时 5 秒)
- [ ] 清理共享内存和 PDO

**用户态侧 (vnvme-server)**:
- [x] 检测 `ShutdownRequested` 标志
- [x] 完成所有正在处理的命令
- [x] 刷新后端存储缓存
- [x] 通知内核关闭完成
- [x] 安全退出主循环

**关闭序列**:
```
1. 用户请求卸载驱动 (devcon remove / pnputil)
2. WDF 调用 VnvmeEvtDeviceD0Exit()
3. 内核设置 ShutdownRequested = TRUE
4. 内核 KeSetEvent(ShutdownEvent)
5. 用户态检测到关闭事件
6. 用户态完成所有待处理命令
7. 用户态发送 IOCTL_VNVME_USER_SHUTDOWN
8. 内核等待 ControlQueue 清空
9. 内核删除 PDO 和释放资源
10. 驱动卸载完成
```

### 验收标准
```powershell
# 1. vnvme-server 启动成功
vnvme-server.exe --config vnvme.conf
# 应显示 "Connected to kernel driver"

# 2. 共享内存映射
# 日志显示 "Shared memory mapped at 0x..."

# 3. 心跳正常
# 连续运行 1 分钟无错误
```

---

## Phase 4: NVMe 命令处理 (预计 3 周)

### 目标
- 实现 Doorbell 轮询引擎
- 处理关键 Admin 命令
- 处理 I/O 命令

### 里程碑
- [x] Admin 命令处理完成 (12 个命令)
- [x] I/O 命令处理完成 (5 个命令)
- [x] PRP 数据传输实现 (1-2 页)
- [x] 队列管理完成
- [ ] stornvme 初始化成功 (需测试验证)
- [ ] 磁盘在 Windows 中出现 (需测试验证)

### 详细任务

> **架构变更说明**: Admin/I/O 命令处理已在内核驱动中实现 (admin_cmd.c, io_cmd.c)，
> 而非原计划的用户态服务。这提供更低延迟和更简单的物理内存访问。

> **双模式架构 (v2)**: 支持内核态和用户态两种命令处理模式，可通过 `VNVME_DEFAULT_CMD_MODE` 切换：
> - `VNVME_CMD_MODE_KERNEL`: 内核态处理 (低延迟，备用方案)
> - `VNVME_CMD_MODE_USER`: 用户态处理 (默认，更灵活)
>
> 用户态模式通过 NotifyRing 通知机制，让 vnvme-server 直接从共享内存读取 NVME_COMMAND。

#### 4.1 Doorbell 轮询 (doorbell.c)
- [x] 实现 `VnvmeInitializePollingTimer()` - 创建轮询定时器
- [x] 实现 `VnvmeStartPollingTimer()` - 启动定时器
- [x] 实现 `VnvmeStopPollingTimer()` - 停止定时器
- [x] 实现 `VnvmeEvtPollingTimer()` - 轮询回调
- [x] 实现 `VnvmeProcessDoorbells()` - 处理 Doorbell 变化框架
- [x] 检测 Admin SQ Tail 变化
- [x] 检测 CC 寄存器变化
- [x] 检测 I/O SQ Tail 变化
- [ ] 实现自适应轮询间隔

#### 4.2 控制器启用流程
- [x] 检测 CC.EN = 1
- [x] 读取 AQA/ASQ/ACQ 配置
- [x] 映射 Admin 队列内存 (PRP → 虚拟地址)
- [x] 设置 CSTS.RDY = 1

#### 4.3 Admin 命令 - 内核 (admin_cmd.c)
- [x] 实现 Identify Controller (Opcode 0x06, CNS=1)
- [x] 实现 Identify Namespace (Opcode 0x06, CNS=0)
- [~] 实现 Identify Namespace List (Opcode 0x06, CNS=2) - 返回空列表, TODO
- [x] 实现 Create I/O CQ (Opcode 0x05)
- [x] 实现 Create I/O SQ (Opcode 0x01)
- [x] 实现 Delete I/O CQ (Opcode 0x04)
- [x] 实现 Delete I/O SQ (Opcode 0x00)
- [x] 实现 Set Features (Opcode 0x09) - Number of Queues
- [x] 实现 Get Features (Opcode 0x0A)
- [x] 实现 Abort (Opcode 0x08)
- [~] 实现 Async Event Request (Opcode 0x0C) - 框架, 不存储命令
- [x] 实现 Keep Alive (Opcode 0x18)
- [ ] 实现 Get Log Page (Opcode 0x02) - TODO

#### 4.4 I/O 命令 - 内核 (io_cmd.c)
- [x] 实现 Read (Opcode 0x02) - 支持 PRP1/PRP2
- [x] 实现 Write (Opcode 0x01) - 支持 PRP1/PRP2
- [x] 实现 Flush (Opcode 0x00)
- [x] 实现 Write Zeroes (Opcode 0x08)
- [~] 实现 Dataset Management (Opcode 0x09) - 返回成功但不处理范围
- [ ] 实现 Compare (Opcode 0x05) - TODO
- [~] PRP List 支持 (>2 页) - 返回 NOT_IMPLEMENTED

#### 4.5 PRP 解析 (prp.c)
- [x] 创建 prp.c 文件和函数框架 (175 行)
- [x] 实现 `VnvmeParsePrpList()` - 解析 PRP1/PRP2
- [x] 定义 `VNVME_PRP_ENTRY` 结构
- [x] 实现 PRP1 单页数据传输
- [x] 实现 PRP2 双页数据传输
- [~] 处理 PRP List 情况 (>2 页返回 NOT_IMPLEMENTED，TODO Phase 4)

#### 4.6 完成处理 (queue.c)
- [x] 创建 queue.c 文件 (185 行)
- [x] 实现 `VnvmeInitializeAdminQueues()` - 读取 AQA/ASQ/ACQ
- [~] 实现 `VnvmeFetchCommand()` - 获取命令 (TODO Phase 4)
- [~] 实现 `VnvmePostCompletion()` - 写入 CQ (TODO Phase 4)
- [x] 正确设置 Phase Tag
- [x] 更新 CQ Tail 和 Phase
- [~] I/O SQ/CQ 创建/删除 (TODO Phase 5)

#### 4.7 用户态命令转发 (user_forward.c)
- [x] 创建 user_forward.c 文件 (231 行)
- [x] 实现 `VnvmeForwardAdminCommandsToUser()` - Admin 队列转发
- [x] 实现 `VnvmeForwardIoCommandsToUser()` - I/O 队列转发
- [x] 实现 `NotifyRingPush()` - 通知环写入
- [x] 实现 `SignalUserMode()` - 唤醒用户态
- [x] 实现 `VnvmeProcessUserCompletions()` - 处理用户态完成
- [~] 处理 I/O CQ 更新 (TODO)
- [~] 触发 MSI-X 中断 (TODO)

#### 4.8 用户态命令处理 (vnvme-server/command_processor.c)
- [x] 创建 command_processor.c 文件 (943 行)
- [x] 实现 `CmdProcessorInit()` - 初始化处理器
- [x] 实现 `CmdProcessorRun()` - 主处理循环
- [x] Admin 命令: Identify, Create/Delete CQ/SQ, Set/Get Features, Abort, KeepAlive
- [x] I/O 命令: Read, Write, Flush, Write Zeroes, Dataset Management
- [x] 实现 `PostAdminCompletion()` - Admin 完成写入
- [x] 实现 `PostIoCompletion()` - I/O 完成写入
- [x] I/O 队列跟踪和管理
- [~] 完整的 Identify Controller 数据填充 (TODO)
- [~] 完整的 Identify Namespace 数据填充 (TODO)

#### 4.9 用户态存储后端 (vnvme-server/backend.c)
- [x] 创建 backend.c 文件 (403 行)
- [x] 实现 `BackendCreate()` - 创建后端 (内存/文件)
- [x] 实现 `BackendDestroy()` - 销毁后端
- [x] 实现 `BackendRead()` - 读取数据
- [x] 实现 `BackendWrite()` - 写入数据
- [x] 实现 `BackendFlush()` - 刷新缓存
- [x] 实现 `BackendWriteZeroes()` - 写零
- [x] 实现 `BackendGetSize()` - 获取后端大小
- [x] 实现内存后端 (VirtualAlloc)
- [x] 实现文件后端 (CreateFile/ReadFile/WriteFile)

### 验收标准
```powershell
# 1. stornvme 初始化完成
# Event Viewer 无 stornvme 错误

# 2. 磁盘出现
Get-Disk | Where-Object { $_.FriendlyName -like "*Virtual NVMe*" }
# 应看到磁盘

# 3. nvme-cli 识别
nvme list
# 应看到我们的设备
```

---

## Phase 5: 存储后端 (预计 2 周)

### 目标
- 实现内存后端 (用于测试)
- 实现文件后端 (主要功能)
- 支持命名空间管理

### 里程碑
- [x] 存储后端抽象层已实现
- [x] 内存后端已实现 (最大 256 MB)
- [x] 文件后端已实现
- [ ] 可以格式化和使用虚拟磁盘 (待测试验证)
- [ ] 数据持久化到文件 (待测试验证)

### 详细任务

> **实现说明**: 存储后端已在内核驱动中实现 (storage.c)，采用统一 API 设计，
> 支持内存和文件两种后端类型，可通过 VnvmeStorageCreate() 选择。

#### 5.1 后端抽象 (storage.c)
- [x] 定义 `VNVME_STORAGE_CONTEXT` 结构
- [x] 定义 `VNVME_STORAGE_TYPE` 枚举 (MEMORY, FILE, SPARSE)
- [x] 实现 `VnvmeStorageCreate()` - 创建后端
- [x] 实现 `VnvmeStorageDestroy()` - 销毁后端
- [x] 实现后端类型选择逻辑

#### 5.2 内存后端 (storage.c - Memory)
- [x] 实现 `StorageMemoryInit()` - 分配内存 (最大 256 MB)
- [x] 实现 `VnvmeStorageRead()` - 内存读取
- [x] 实现 `VnvmeStorageWrite()` - 内存写入
- [x] 实现 `VnvmeStorageGetDirect()` - 零拷贝直接访问 (仅内存后端)
- [x] 实现 `VnvmeStorageWriteZeroes()` - 零填充
- [x] 实现 `StorageMemoryCleanup()` - 释放内存

#### 5.3 文件后端 (storage.c - File)
- [x] 实现 `StorageFileInit()` - 打开/创建文件 (ZwCreateFile)
- [x] 实现 `VnvmeStorageRead()` - 文件读取 (ZwReadFile)
- [x] 实现 `VnvmeStorageWrite()` - 文件写入 (ZwWriteFile)
- [x] 实现 `VnvmeStorageFlush()` - 刷新缓冲区 (更新时间戳)
- [x] 实现 `StorageFileCleanup()` - 关闭文件
- [ ] 支持稀疏文件 (VNVME_STORAGE_TYPE_SPARSE 已定义，待实现)
- [ ] 支持预分配

#### 5.4 命名空间管理
- [x] VNVME_NAMESPACE 包含 Storage 字段
- [ ] 实现命名空间创建时的存储关联
- [ ] 实现命名空间删除
- [ ] 实现命名空间属性查询
- [x] Identify Namespace 返回正确数据 (admin_cmd.c)

#### 5.5 vnvmectl 管理命令
- [x] 实现 `vnvmectl create` - 支持 --size, --backend, --file, --model, --serial
- [x] 实现 `vnvmectl delete` - 删除指定控制器
- [x] 实现 `vnvmectl list` - 列出虚拟磁盘
- [x] 实现 `vnvmectl status` - 显示驱动状态
- [x] 实现 `vnvmectl version` - 显示版本信息
- [x] 实现 `vnvmectl test` - 运行测试套件

#### 5.6 I/O 命令与存储后端集成
- [x] HandleRead() 使用 VnvmeStorageRead/VnvmeStorageGetDirect
- [x] HandleWrite() 使用 VnvmeStorageWrite
- [x] HandleFlush() 调用 VnvmeStorageFlush
- [x] HandleWriteZeroes() 调用 VnvmeStorageWriteZeroes

### 验收标准
```powershell
# 1. 创建虚拟磁盘
vnvmectl create --size=1G --backend=file --file=C:\vnvme\disk.img
# 成功

# 2. 格式化
Initialize-Disk -Number <n> -PartitionStyle GPT
New-Partition -DiskNumber <n> -UseMaximumSize -AssignDriveLetter
Format-Volume -DriveLetter X -FileSystem NTFS
# 成功

# 3. 读写测试
echo "test" > X:\test.txt
Get-Content X:\test.txt
# 返回 "test"
```

---

## Phase 6: 测试和优化 (预计 2 周)

### 目标
- 完成功能测试
- 性能测试和优化
- 文档完善

### 6.0 调试基础设施 (优先)

> **说明**: 调试能力是测试和优化的前提，应首先实现。

#### 6.0.1 调试输出系统 (0.5 天)
- [x] 创建 `debug.h` - 统一调试宏定义
- [x] 实现多级别调试输出 (Error/Warning/Info/Debug/Verbose)
- [x] 实现模块化调试标志 (可按模块启用/禁用)
- [x] 支持 DbgPrint 和 WPP 双模式
- [x] 实现函数进入/退出跟踪宏

#### 6.0.2 运行时调试控制 (0.5 天)
- [x] 注册表参数读取 (`DebugLevel`, `DebugFlags`)
- [x] 实现 `VnvmeDebugInit()` - 初始化调试子系统
- [ ] IOCTL 动态调整调试级别 (`IOCTL_VNVME_SET_DEBUG_LEVEL`)
- [ ] DebugView 实时查看验证

#### 6.0.3 调试工具集成 (1 天)
- [ ] WinDbg 调试脚本 (`scripts/windbg/vnvme.wds`)
- [ ] Driver Verifier 配置说明
- [ ] 蓝屏 dump 分析指南
- [ ] 常用断点列表文档

#### 6.0.4 性能计数器 (0.5 天)
- [ ] 命令处理统计 (提交数/完成数/错误数)
- [ ] 延迟统计 (最小/最大/平均)
- [ ] I/O 统计 (读/写字节数、IOPS)
- [ ] IOCTL 查询性能统计 (`IOCTL_VNVME_GET_STATS`)

#### 6.0.5 用户态服务调试 (0.5 天)
- [ ] 日志文件输出 (`--log-file` 选项)
- [ ] 日志级别控制 (`--log-level` 选项)
- [ ] 控制台彩色输出
- [ ] 性能计数器输出

#### 6.0.6 用户态服务重构 (2 天)

> **说明**: 将紧凑版 (3 文件) 重构为模块化版本，提高可测试性和可扩展性

**6.0.6.1 基础设施模块 (0.5 天)**
- [x] 创建 `types.h` - 基础类型定义 (避免循环依赖)
- [x] 创建 `vnvme_server.h` - 公共类型定义和宏
- [x] 创建 `logger.c/.h` - 统一日志系统
  - 多级别输出 (Error/Warning/Info/Debug/Verbose)
  - 控制台 + 文件双输出
  - 时间戳和模块标识
- [x] 创建 `config.c/.h` - 从 main.c 分离配置解析
  - 命令行参数解析
  - INI 配置文件加载
  - 配置验证

**6.0.6.2 驱动通信模块 (0.5 天)**
- [x] 创建 `driver_comm.c/.h` - 从 main.c 分离驱动通信
  - `DriverConnect()` / `DriverDisconnect()`
  - `MapSharedMemory()` / `UnmapSharedMemory()`
  - `SendHeartbeat()` / `SendUserReady()`
  - `IsShutdownRequested()`

**6.0.6.3 命令处理模块拆分 (0.5 天)**
- [ ] 分离 `admin_commands.c` - Admin 命令实现
  - Identify, Get Features, Set Features 等
- [ ] 分离 `io_commands.c` - I/O 命令实现
  - Read, Write, Flush, Write Zeroes 等
- [ ] 精简 `command_processor.c` - 仅命令分发

**6.0.6.4 后端模块拆分 (0.5 天)**
- [x] 创建 `backend.h` - 后端接口定义
- [x] 分离 `backend_memory.c` - 内存后端实现
- [x] 分离 `backend_file.c` - 文件后端实现
- [x] 创建 `backend_common.c` - 后端分发器

**6.0.6.5 集成和切换 (待定)**
- [ ] 创建 `main_v2.c` - 使用新模块的入口 (已创建，待测试)
- [ ] 更新 vcxproj 启用新模块编译
- [ ] 测试新旧版本功能一致性
- [ ] 替换 main.c 为 main_v2.c

### 里程碑
- [ ] 所有功能测试通过
- [ ] 性能达到可接受水平
- [ ] 文档和代码注释完整

### 详细任务

#### 6.1 功能测试
- [ ] 驱动加载/卸载测试
- [ ] 热插拔测试
- [ ] 多磁盘测试
- [ ] 并发 I/O 测试
- [ ] 大文件读写测试
- [ ] 随机 I/O 测试

#### 6.2 性能测试
- [ ] 使用 CrystalDiskMark 测试
- [ ] 使用 fio 测试
- [ ] 记录基线性能
- [ ] 识别瓶颈

#### 6.3 性能优化

> 详见 [performance-optimization.md](performance-optimization.md)

**6.3.1 自适应轮询 (1 天)**
- [ ] 实现 `VNVME_ADAPTIVE_POLL` 结构
- [ ] 实现 `VnvmeAdjustPollingInterval()` 函数
- [ ] 添加注册表参数配置 (`PollingIntervalUs`, `AdaptivePolling`)
- [ ] 测试验证轮询间隔动态调整

**6.3.2 批处理优化 (1 天)**
- [ ] 实现 `VnvmeFetchCommandBatch()` 批量获取命令
- [ ] 实现 `VnvmePostCompletionBatch()` 批量投递完成
- [ ] 用户态服务批处理实现
- [ ] 测试验证吞吐量提升

**6.3.3 事件通知 (2 天)**
- [ ] 实现 `VnvmeCreateUserEventHandle()` 创建用户可等待事件
- [ ] 实现混合通知模式 (低负载事件 + 高负载轮询)
- [ ] 用户态 `WaitForSingleObject()` 等待实现
- [ ] IOCTL 返回事件句柄给用户态
- [ ] 测试验证延迟降低

**6.3.4 内存访问优化 (1 天)**
- [ ] 缓存行对齐关键结构 (`DECLSPEC_CACHEALIGN`)
- [ ] 优化内存屏障使用 (减少不必要的屏障)
- [ ] 预取优化 (`_mm_prefetch`)
- [ ] 测试验证 CPU 使用率降低

**6.3.5 后端存储优化 (1 天)**
- [ ] 实现异步 I/O (I/O Completion Port)
- [ ] 直接 I/O 支持 (`FILE_FLAG_NO_BUFFERING`)
- [ ] 可选: 内存映射后端
- [ ] 测试验证后端 I/O 性能

#### 6.4 文档完善
- [ ] 更新所有文档
- [ ] 添加代码注释
- [ ] 创建用户手册
- [ ] 创建 API 文档

#### 6.5 发布准备
- [ ] 创建发布包结构
- [ ] 编写安装说明
- [ ] 创建发布日志

---

## 时间线总览

| 阶段 | 持续时间 | 累计时间 |
|------|----------|----------|
| Phase 1: 项目骨架 | 2 周 | 2 周 |
| Phase 2: PDO/PCIe | 2 周 | 4 周 |
| Phase 3: 用户态通信 | 2 周 | 6 周 |
| Phase 4: NVMe 命令 | 3 周 | 9 周 |
| Phase 5: 存储后端 | 2 周 | 11 周 |
| Phase 6: 测试优化 | 2 周 | 13 周 |

**总预计时间: 约 3 个月**

---

## 风险和依赖

### 技术风险

| 风险 | 影响 | 缓解措施 |
|------|------|----------|
| stornvme 行为不符合预期 | 高 | 使用 WinDbg 跟踪 stornvme 调用 |
| 轮询延迟过高 | 中 | 测试不同间隔，考虑 KeQueryPerformanceCounter |
| 共享内存竞争 | 中 | 仔细设计无锁算法，使用内存屏障 |
| 驱动蓝屏 | 高 | 增量开发，频繁测试，使用 WPP 跟踪 |

### 依赖项

| 依赖 | 说明 |
|------|------|
| WDK | Windows Driver Kit 10.0.22621+ |
| Visual Studio | 2022 with C++ desktop development |
| 测试签名 | 开发期间需要启用 testsigning |
| devcon.exe | 用于安装和调试 |

---

## 下一步

**立即开始 Phase 1**:

1. 打开 Visual Studio，创建解决方案
2. 按照 [build-guide.md](build-guide.md) 配置项目
3. 从 `DriverEntry` 开始实现
4. 参考 [architecture-v2.md](architecture-v2.md) 理解架构

**遇到问题时**:
- 查看 [troubleshooting.md](troubleshooting.md)
- 查看 [history/](history/) 中的设计决策记录
