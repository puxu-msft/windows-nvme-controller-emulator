# VNVME 调试指南

本文档介绍如何调试 VNVME 虚拟 NVMe 驱动和用户态服务。

---

## 目录

1. [开发环境准备](#开发环境准备)
2. [测试签名模式](#测试签名模式)
3. [Driver Verifier 配置](#driver-verifier-配置)
4. [WinDbg 内核调试](#windbg-内核调试)
5. [蓝屏分析](#蓝屏分析)
6. [常用断点](#常用断点)
7. [用户态服务调试](#用户态服务调试)
8. [故障排除](#故障排除)

---

## 开发环境准备

### 必备工具

| 工具 | 用途 | 下载 |
|------|------|------|
| WDK | 驱动编译 | [Windows Driver Kit](https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk) |
| WinDbg | 内核调试 | [Microsoft Store](https://aka.ms/windbg) 或 WDK 附带 |
| DebugView | DbgPrint 查看 | [Sysinternals](https://learn.microsoft.com/sysinternals/downloads/debugview) |
| DevCon | 设备管理 | WDK Tools 目录 |
| Driver Verifier | 驱动验证 | Windows 内置 (`verifier.exe`) |

### 符号路径设置

```batch
set _NT_SYMBOL_PATH=srv*c:\symbols*https://msdl.microsoft.com/download/symbols
set _NT_SYMBOL_PATH=%_NT_SYMBOL_PATH%;Q:\src\virtual-nvme-driver\vnvme\build\Debug\x64
```

---

## 测试签名模式

VNVME 驱动需要在测试签名模式下运行（除非使用 EV 代码签名证书）。

### 启用测试签名

```powershell
# 以管理员权限运行
bcdedit /set testsigning on

# 重启生效
shutdown /r /t 0
```

### 禁用测试签名

```powershell
bcdedit /set testsigning off
shutdown /r /t 0
```

### 验证状态

```powershell
bcdedit /enum | findstr testsigning
```

---

## Driver Verifier 配置

Driver Verifier 是检测驱动问题的最重要工具。

### 推荐设置（开发阶段）

```powershell
# 对 vnvme.sys 启用标准验证
verifier /standard /driver vnvme.sys

# 查看当前设置
verifier /querysettings

# 重启生效
shutdown /r /t 0
```

### 高级设置（问题追踪）

```powershell
# 启用更多检查（可能影响性能）
verifier /flags 0xBB /driver vnvme.sys
```

标志说明：
- `0x01` - 特殊池 (Special Pool)
- `0x02` - 强制 IRQL 检查
- `0x08` - 池跟踪 (Pool Tracking)
- `0x10` - I/O 验证
- `0x20` - 死锁检测
- `0x80` - DMA 验证

### 清除 Verifier 设置

```powershell
verifier /reset
shutdown /r /t 0
```

### 查看违规统计

```powershell
verifier /query
```

---

## WinDbg 内核调试

### 配置目标机器

**方法 1: 网络调试 (推荐)**

在目标机器上以管理员权限运行：

```batch
bcdedit /debug on
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000 key:1.2.3.4
shutdown /r /t 0
```

**方法 2: 本地调试**

```batch
bcdedit /debug on
bcdedit /dbgsettings local
shutdown /r /t 0
```

### 连接 WinDbg

1. 打开 WinDbg (Preview)
2. `Attach to kernel` → 选择连接方式
3. 等待连接建立

### 加载 VNVME 调试脚本

```
$<Q:\src\virtual-nvme-driver\scripts\windbg\vnvme.wds
```

或

```
.run Q:\src\virtual-nvme-driver\scripts\windbg\vnvme.wds
```

### 常用 WinDbg 命令

```
$$ 显示驱动信息
!drvobj vnvme 7

$$ 显示设备对象
!devobj \Driver\vnvme

$$ 显示控制设备
!devobj \Device\VNVMEControl

$$ 列出 VNVME 内存分配
!poolfind VNVM

$$ 显示驱动模块
lm m vnvme

$$ 检查符号
x vnvme!*

$$ 反汇编函数
u vnvme!DriverEntry

$$ 显示结构
dt vnvme!VNVME_FDO_CONTEXT
dt vnvme!VNVME_PDO_CONTEXT
dt vnvme!VNVME_SHM_CONTROL_BLOCK
```

---

## 蓝屏分析

### 收集 dump 文件

Windows 默认将内存转储保存到 `%SystemRoot%\MEMORY.DMP`。

确保启用完整转储：

```powershell
# 检查当前设置
Get-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl"

# 设置完整转储 (CrashDumpEnabled = 1)
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" -Name "CrashDumpEnabled" -Value 1
```

### 分析 dump 文件

1. 打开 WinDbg
2. `File` → `Open Crash Dump` → 选择 `MEMORY.DMP`
3. 运行：

```
$$ 自动分析
!analyze -v

$$ 显示调用栈
kb 50

$$ 显示 bugcheck 参数
!bugcheck

$$ 如果涉及 VNVME
lm m vnvme
```

### 常见蓝屏代码

| Bugcheck | 名称 | 常见原因 |
|----------|------|----------|
| `0x7E` | SYSTEM_THREAD_EXCEPTION_NOT_HANDLED | 未处理异常，检查空指针 |
| `0x7F` | UNEXPECTED_KERNEL_MODE_TRAP | 栈溢出或硬件错误 |
| `0x9F` | DRIVER_POWER_STATE_FAILURE | 电源状态转换问题 |
| `0xA0` | INTERNAL_POWER_ERROR | IRP 处理错误 |
| `0xC4` | DRIVER_VERIFIER_DETECTED_VIOLATION | Verifier 检测到问题 |
| `0xD1` | DRIVER_IRQL_NOT_LESS_OR_EQUAL | 在高 IRQL 访问分页内存 |
| `0xCE` | DRIVER_UNLOADED_WITHOUT_CANCELLING_PENDING_OPERATIONS | 卸载时有待处理操作 |

---

## 常用断点

### 驱动生命周期

```
$$ 驱动加载
bp vnvme!DriverEntry

$$ 设备添加
bp vnvme!VnvmeEvtDeviceAdd

$$ 硬件准备
bp vnvme!VnvmePdoEvtDevicePrepareHardware

$$ 电源状态变化
bp vnvme!VnvmePdoEvtDeviceD0Entry
bp vnvme!VnvmePdoEvtDeviceD0Exit

$$ 驱动卸载
bp vnvme!VnvmeEvtDriverContextCleanup
```

### IOCTL 处理

```
$$ 所有 IOCTL
bp vnvme!VnvmeEvtIoDeviceControl

$$ 特定 IOCTL (通过控制码)
bp vnvme!VnvmeEvtIoDeviceControl "j (dwo(@esp+8)==0x222004) '';'gc'"
```

### 控制器管理

```
$$ 创建控制器
bp vnvme!VnvmeCreateVirtualController
bp vnvme!VnvmeCreateControllerPdo

$$ 删除控制器
bp vnvme!VnvmeDeleteVirtualController
bp vnvme!VnvmeDeleteControllerPdo
```

### NVMe 命令处理

```
$$ Doorbell 轮询
bp vnvme!VnvmeEvtPollingTimer
bp vnvme!VnvmeProcessDoorbells

$$ Admin 命令
bp vnvme!VnvmeProcessAdminSQCommand

$$ I/O 命令
bp vnvme!VnvmeProcessIoSQCommand

$$ PRP 解析
bp vnvme!VnvmeParsePrpList
bp vnvme!VnvmeReadPrpData
bp vnvme!VnvmeWritePrpData
```

### 共享内存

```
$$ 共享内存分配
bp vnvme!VnvmeAllocateShm

$$ 映射到用户态
bp vnvme!VnvmeHandleMapShm

$$ 用户态就绪
bp vnvme!VnvmeHandleUserReady
```

### 用户态崩溃检测

```
$$ 心跳检查 (10秒超时)
bp vnvme!CheckUserModeHeartbeat

$$ 查看崩溃状态
dt vnvme!VNVME_FDO_CONTEXT UserCrashed

$$ 查看控制块时间戳
dt vnvme!VNVME_SHM_CONTROL_BLOCK LastUserHeartbeat
```

### 条件断点示例

```
$$ 仅在特定 opcode 时中断
bp vnvme!VnvmeProcessAdminSQCommand "j (by(@rcx)==6) '';'gc'"

$$ 仅在错误状态时中断
bp vnvme!VnvmePostCompletion "j (dwo(@rcx+0xc)!=0) '';'gc'"
```

---

## 用户态服务调试

### 使用 Visual Studio

1. 打开解决方案 `vnvme.sln`
2. 设置 `vnvme-server` 为启动项目
3. 配置调试参数：
   - 命令参数：`--debug --backend memory --size 64M`
   - 以管理员身份运行
4. F5 启动调试

### 模块化架构调试 (v2)

vnvme-server v2 使用模块化架构，各模块可独立调试：

```
$$ 后端初始化
bp vnvme_server!BackendFileCreate
bp vnvme_server!BackendMemoryCreate

$$ 命令引擎
bp vnvme_server!CmdEngineCreate
bp vnvme_server!CmdEngineRun

$$ 事件等待模式
bp vnvme_server!CmdEngineSetCommandEvent

$$ 服务启动
bp vnvme_server!ServerStart
bp vnvme_server!ServerRun
```

### 性能模式调试

```powershell
# 轮询模式 (高性能，高 CPU)
vnvme-server.exe --poll-interval 0

# 事件等待模式 (低 CPU，需驱动支持)
vnvme-server.exe --poll-interval 0 --use-events

# 混合模式 (平衡)
vnvme-server.exe --poll-interval 100
```

### 使用 WinDbg

```powershell
# 启动 vnvme-server 并附加
windbg -g -G build\Debug\x64\vnvme-server.exe --debug
```

### 使用 DebugView

vnvme-server 使用 `OutputDebugString` 输出调试信息：

1. 以管理员身份启动 DebugView
2. 启用 `Capture` → `Capture Global Win32`
3. 启动 vnvme-server

### 日志级别

```powershell
# 启用详细日志
vnvme-server.exe --debug --log-level verbose

# 输出到文件
vnvme-server.exe --log-file C:\vnvme\server.log
```

---

## 故障排除

### 驱动无法加载

**症状**: `devcon install` 返回错误

**检查项**:
1. 是否启用测试签名 (`bcdedit /enum | findstr test`)
2. 驱动文件是否存在且可访问
3. 查看事件查看器：`Event Viewer` → `Windows Logs` → `System`

**常见错误**:
- `0xC0000428` - 签名问题，启用测试签名
- `0xC0000022` - 权限问题，以管理员运行

### 设备管理器显示黄色感叹号

**检查项**:
1. 右键设备 → 属性 → 查看错误代码
2. 使用 WinDbg 检查驱动是否正常加载

**常见错误代码**:
- Code 10 - 设备无法启动
- Code 28 - 缺少驱动程序
- Code 31 - 设备无法正常工作

### vnvme-server 无法连接

**症状**: "Failed to open device" 错误

**检查项**:
1. 驱动是否已加载：`sc query vnvme`
2. 控制设备是否存在：在 WinDbg 中 `!devobj \Device\VNVMEControl`
3. 是否以管理员权限运行

### 性能问题

**诊断步骤**:
1. 使用 `vnvmectl stats` 查看统计信息
2. 检查 CPU 使用率（轮询间隔可能太短）
3. 使用 `xperf` 或 `WPR` 收集性能跟踪
4. 检查是否使用了事件等待模式

**优化建议**:
- 使用 `--poll-interval 100` 减少 CPU 使用
- 启用文件预分配 `--preallocate` 提升写入性能
- 使用内存后端进行性能测试

### 用户态服务崩溃

**症状**: vnvme-server 意外退出

**驱动行为**:
- 驱动会在 10 秒内检测到用户态崩溃
- 自动切换到内核态处理（返回错误）
- `UserCrashed` 标志置为 TRUE

**恢复步骤**:
1. 重新启动 vnvme-server
2. 驱动自动检测并恢复用户态处理

### 内存泄漏

**诊断步骤**:
1. 启用 Driver Verifier 的池跟踪
2. 运行一段时间后使用 `!poolfind VNVM` 检查分配
3. 对比不同时间点的分配数量

---

## 附录：调试检查清单

### 部署前检查

- [ ] Debug 构建编译成功
- [ ] Release 构建编译成功
- [ ] Driver Verifier 无报告
- [ ] 运行基本功能测试
- [ ] 检查内存泄漏
- [ ] 测试驱动卸载

### 问题报告模板

```
## 环境
- Windows 版本：
- WDK 版本：
- 驱动版本：

## 问题描述


## 重现步骤
1. 
2. 
3. 

## 期望行为


## 实际行为


## 日志/Dump
(附加相关文件)
```

---

## 相关文档

- [构建指南](build-guide.md)
- [架构概述](architecture-v2.md)
- [IOCTL 参考](ioctl-reference.md)
- [测试策略](testing-strategy.md)
