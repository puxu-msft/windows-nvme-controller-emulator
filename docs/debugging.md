# 调试指南

本文档说明如何调试 VNVME 驱动和用户态服务。

## 调试级别

驱动支持 5 个调试级别，可通过注册表配置：

| 级别 | 值 | 说明 |
|------|------|------|
| NONE | 0 | 禁用所有输出 |
| ERROR | 1 | 仅错误信息 |
| WARNING | 2 | 错误 + 警告 |
| INFO | 3 | 错误 + 警告 + 信息 (默认) |
| DEBUG | 4 | 上述 + 调试详情 |
| VERBOSE | 5 | 全部输出 (性能影响大) |

## 调试模块标志

可以按模块启用/禁用调试输出：

| 标志 | 值 | 模块 |
|------|------|------|
| DRIVER | 0x00000001 | 驱动入口/卸载 |
| PNP | 0x00000002 | PnP 和电源管理 |
| IOCTL | 0x00000004 | IOCTL 处理 |
| BUS | 0x00000008 | 总线/PDO 管理 |
| BAR0 | 0x00000010 | BAR0 寄存器 |
| PCIE | 0x00000020 | PCIe 配置空间 |
| DOORBELL | 0x00000040 | Doorbell 轮询 |
| QUEUE | 0x00000080 | 队列管理 |
| ADMIN | 0x00000100 | Admin 命令 |
| IO | 0x00000200 | I/O 命令 |
| PRP | 0x00000400 | PRP 解析 |
| STORAGE | 0x00000800 | 存储后端 |
| SHM | 0x00001000 | 共享内存 |
| USER | 0x00002000 | 用户态通信 |
| ALL | 0xFFFFFFFF | 所有模块 |

## 注册表配置

```powershell
# 创建 Parameters 子键
New-Item -Path "HKLM:\SYSTEM\CurrentControlSet\Services\vnvme\Parameters" -Force

# 设置调试级别 (0-5)
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\vnvme\Parameters" `
    -Name "DebugLevel" -Value 4 -Type DWord

# 设置调试标志 (位掩码)
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\vnvme\Parameters" `
    -Name "DebugFlags" -Value 0xFFFFFFFF -Type DWord
```

## 使用 DebugView

1. 下载 [DebugView](https://docs.microsoft.com/en-us/sysinternals/downloads/debugview)
2. 以管理员身份运行
3. 启用菜单: Capture → Capture Kernel (Ctrl+K)
4. 启用菜单: Capture → Enable Verbose Kernel Output
5. 加载驱动后，在 DebugView 中查看 `[VNVME:...]` 开头的消息

## 使用 WinDbg

### 连接到内核调试

```
# 本地内核调试 (需要禁用 Secure Boot)
windbg -kl

# 网络调试
windbg -k net:port=50000,key=1.2.3.4

# 串口调试
windbg -k com:port=COM1,baud=115200
```

### 加载符号

```
.symfix
.reload /f vnvme.sys
```

### 常用命令

```
# 查看驱动信息
lm m vnvme

# 设置断点
bp vnvme!DriverEntry
bp vnvme!VnvmeEvtDeviceAdd
bp vnvme!VnvmeEvtIoDeviceControl
bp vnvme!VnvmeProcessAdminSQCommand
bp vnvme!VnvmeProcessIoSQCommand

# 查看全局变量
dt vnvme!g_FdoContext
dt vnvme!g_VnvmeDebugLevel
dt vnvme!g_VnvmeDebugStats

# 查看调试统计
dx @$cursession.Modules["vnvme"].Variables["g_VnvmeDebugStats"]

# 启用内核调试输出
ed nt!Kd_IHVDRIVER_Mask 0xFFFFFFFF

# 查看 PDO 列表
!wdfdevice 0x<fdoHandle>
```

### 分析蓝屏

```
# 分析 dump 文件
.reload
!analyze -v

# 查看调用栈
kb

# 查看寄存器
r

# 查看内存
db <address>
```

## Driver Verifier

Driver Verifier 可以检测驱动中的常见问题：

```powershell
# 启用 Driver Verifier
verifier /standard /driver vnvme.sys

# 查看状态
verifier /query

# 禁用
verifier /reset
# 重启生效
```

检测的问题类型：
- 内存泄漏
- 池损坏
- 死锁
- IRQL 问题
- 句柄泄漏

## 性能统计

驱动收集以下性能统计：

- Admin 命令: 提交数、完成数、错误数
- I/O 命令: 提交数、完成数、错误数
- 字节数: 读取、写入
- Doorbell: 轮询次数、命中次数、命中率
- 延迟: 最小、最大、平均 (微秒)

统计可通过 WinDbg 查看：

```
dx @$cursession.Modules["vnvme"].Variables["g_VnvmeDebugStats"]
```

或调用 `VnvmeDebugPrintStats()` 打印到调试输出。

## 用户态服务调试

```powershell
# 启用详细日志
vnvme-server.exe --log-level debug

# 输出到文件
vnvme-server.exe --log-file C:\vnvme\server.log

# 使用 Visual Studio 附加调试
# 1. 启动 vnvme-server.exe
# 2. Debug → Attach to Process → vnvme-server.exe
```

## 常见问题

### 驱动无法加载

1. 检查测试签名是否启用: `bcdedit | findstr testsigning`
2. 检查驱动签名: `signtool verify /v vnvme.sys`
3. 查看系统事件日志

### 没有调试输出

1. 确认 DebugLevel >= 1
2. 确认 DebugFlags 包含所需模块
3. 确认 DebugView 启用了 Capture Kernel
4. 尝试 `ed nt!Kd_IHVDRIVER_Mask 0xFFFFFFFF`

### 蓝屏分析

1. 确保配置完整内存转储
2. 收集 `C:\Windows\MEMORY.DMP`
3. 使用 WinDbg 打开: `windbg -z MEMORY.DMP`
4. 运行 `!analyze -v`
