# 故障排查指南

本文档提供 Virtual NVMe 驱动开发和使用过程中常见问题的排查方法。

## 目录

1. [驱动加载问题](#驱动加载问题)
2. [蓝屏 (BSOD) 问题](#蓝屏-bsod-问题)
3. [设备不显示问题](#设备不显示问题)
4. [I/O 错误问题](#io-错误问题)
5. [性能问题](#性能问题)
6. [调试技巧](#调试技巧)

---

## 驱动加载问题

### 问题: 驱动安装失败，提示签名错误

**症状**:
```
Windows 无法验证此驱动程序软件的发布者
```

**解决方案**:

1. 启用测试签名模式:
```powershell
bcdedit /set testsigning on
Restart-Computer
```

2. 检查驱动是否正确签名:
```powershell
signtool verify /v /pa vnvme.sys
```

3. 确认证书已安装到受信任的根证书颁发机构:
```powershell
certutil -addstore "Root" vnvme_test.cer
certutil -addstore "TrustedPublisher" vnvme_test.cer
```

### 问题: 驱动加载后立即卸载

**症状**: 设备管理器中设备带黄色感叹号

**排查步骤**:

1. 查看设备状态码:
```powershell
Get-PnpDevice | Where-Object { $_.FriendlyName -like "*NVMe*" } | 
    Select-Object Status, InstanceId, Problem
```

2. 检查系统事件日志:
```powershell
Get-WinEvent -LogName System -MaxEvents 50 | 
    Where-Object { $_.ProviderName -eq "Microsoft-Windows-Kernel-PnP" }
```

3. 常见错误码:

| 错误码 | 说明 | 解决方案 |
|--------|------|----------|
| Code 10 | 设备无法启动 | 检查 DriverEntry 返回值 |
| Code 28 | 驱动未安装 | 重新安装 INF |
| Code 31 | 设备工作不正常 | 检查资源冲突 |
| Code 37 | 驱动初始化失败 | 检查 EvtDeviceAdd |
| Code 52 | 数字签名问题 | 启用测试签名 |

### 问题: DriverEntry 返回失败

**调试方法**:

1. 在 DriverEntry 开始处设置断点:
```windbg
bp vnvme!DriverEntry
g
```

2. 检查返回的 NTSTATUS:
```windbg
# 在 DriverEntry 返回前
? @rax  ; 查看返回值
!error @rax  ; 解析错误码
```

---

## 蓝屏 (BSOD) 问题

### 常见蓝屏错误码

| Bug Check | 名称 | 常见原因 |
|-----------|------|----------|
| 0x0A | IRQL_NOT_LESS_OR_EQUAL | 在高 IRQL 访问分页内存 |
| 0x1E | KMODE_EXCEPTION_NOT_HANDLED | 未处理的异常 |
| 0x50 | PAGE_FAULT_IN_NONPAGED_AREA | 访问无效内存 |
| 0x7E | SYSTEM_THREAD_EXCEPTION_NOT_HANDLED | 系统线程异常 |
| 0x9F | DRIVER_POWER_STATE_FAILURE | 电源状态转换失败 |
| 0xC4 | DRIVER_VERIFIER_DETECTED_VIOLATION | Driver Verifier 检测到违规 |
| 0xD1 | DRIVER_IRQL_NOT_LESS_OR_EQUAL | 驱动 IRQL 问题 |

### 蓝屏分析步骤

1. 收集内存转储:
```powershell
# 确保启用完整内存转储
wmic recoveros set DebugInfoType=1
```

2. 使用 WinDbg 分析:
```windbg
# 打开 dump 文件后
!analyze -v

# 查看调用栈
k

# 查看故障指令
u @rip

# 如果是我们的驱动
lm m vnvme  ; 确认模块加载
!lmi vnvme  ; 查看模块信息
```

### IRQL 问题排查

```windbg
# 检查当前 IRQL
!irql

# 常见 IRQL 值
# 0 - PASSIVE_LEVEL (可以访问分页内存)
# 1 - APC_LEVEL
# 2 - DISPATCH_LEVEL (不能访问分页内存!)
# 更高 - 中断级别
```

**常见错误模式**:
```c
// 错误: 在 DPC (DISPATCH_LEVEL) 中调用分页函数
VOID DpcRoutine(...) {
    ZwReadFile(...);  // 错误! ZwReadFile 需要 PASSIVE_LEVEL
}

// 正确: 使用工作项
VOID DpcRoutine(...) {
    IoQueueWorkItem(WorkItem, WorkerRoutine, DelayedWorkQueue, Context);
}
```

---

## 设备不显示问题

### 问题: 虚拟磁盘在磁盘管理中不显示

**排查步骤**:

1. 检查总线驱动是否加载:
```powershell
sc query vnvmebus
Get-PnpDevice -Class System | Where-Object { $_.FriendlyName -like "*VNvme*" }
```

2. 检查设备是否创建:
```powershell
# 使用 devcon 查看设备树
devcon find *vnvme*
devcon status *vnvme*
```

3. 检查驱动日志:
```powershell
# 使用 DebugView 查看 DbgPrint 输出
# 或查看 WPP 跟踪
```

4. 手动触发设备枚举:
```powershell
# 通过 IOCTL 通知总线驱动创建设备
# 或重新扫描设备
pnputil /scan-devices
```

### 问题: 磁盘显示但无法初始化

**可能原因**:
- Identify Controller/Namespace 返回数据不正确
- 块大小设置不兼容
- 容量报告错误

**调试方法**:
```powershell
# 使用 PowerShell 查看磁盘信息
Get-Disk | Format-List *

# 使用 diskpart
diskpart
list disk
select disk N
detail disk
```

---

## I/O 错误问题

### 问题: 读写操作失败

**排查步骤**:

1. 检查 NVMe 状态码:
```c
// 在命令完成处理中添加日志
DbgPrint("Command CID=%04X completed with status: SCT=%d SC=0x%02X\n",
         cqe->CID, 
         NVME_STATUS_GET_SCT(cqe->Status),
         NVME_STATUS_GET_SC(cqe->Status));
```

2. 常见 I/O 错误:

| 症状 | 可能原因 | 解决方案 |
|------|----------|----------|
| 读取返回全零 | 后端未初始化 | 检查 BackendInitialize |
| 写入无效 | 数据未持久化 | 检查 BackendWrite 和 Flush |
| 超时 | 命令未完成 | 检查完成队列处理 |
| 数据损坏 | PRP 处理错误 | 验证 PRP 列表解析 |

3. 使用 I/O 跟踪:
```powershell
# 启用 StorPort 跟踪
logman create trace storport -p "Microsoft-Windows-StorPort" -o storport.etl
logman start storport
# 执行 I/O 操作
logman stop storport
```

### 问题: 随机 I/O 失败

**可能原因**:
- 并发访问未正确同步
- 内存池耗尽
- 队列溢出

**调试方法**:
```windbg
# 检查队列状态
dt vnvme!VNVME_QUEUE
!pool @rcx  ; 检查内存分配

# 检查自旋锁
!locks
```

---

## 性能问题

### 问题: I/O 性能低于预期

**排查步骤**:

1. 使用性能计数器:
```powershell
Get-Counter '\PhysicalDisk(*)\*' | 
    Select-Object -ExpandProperty CounterSamples |
    Where-Object { $_.InstanceName -like "*vnvme*" }
```

2. 检查瓶颈:

| 瓶颈位置 | 症状 | 解决方案 |
|----------|------|----------|
| 后端 I/O | 高延迟 | 使用内存后端或优化文件 I/O |
| 锁竞争 | CPU 使用高但吞吐低 | 减小锁粒度 |
| 内存复制 | CPU 使用高 | 使用直接 I/O，避免复制 |
| 队列深度 | 低 IOPS | 增加队列深度 |

3. 性能分析:
```powershell
# 使用 xperf/WPR 收集性能数据
wpr -start GeneralProfile
# 执行测试
wpr -stop perf.etl
# 使用 WPA 分析
```

---

## 调试技巧

### 启用详细日志

```c
// 定义调试级别
#define VNVME_DBG_ERROR   0x00000001
#define VNVME_DBG_WARN    0x00000002
#define VNVME_DBG_INFO    0x00000004
#define VNVME_DBG_TRACE   0x00000008
#define VNVME_DBG_IOCTL   0x00000010
#define VNVME_DBG_IO      0x00000020

ULONG g_DebugLevel = VNVME_DBG_ERROR | VNVME_DBG_WARN;

#define VNvmeDbgPrint(level, fmt, ...) \
    if (g_DebugLevel & (level)) { \
        DbgPrint("[VNvme] " fmt, ##__VA_ARGS__); \
    }
```

### 使用 Driver Verifier

```powershell
# 启用完整验证
verifier /standard /driver vnvme.sys vnvmebus.sys

# 启用特定检查
verifier /flags 0x9BB /driver vnvme.sys

# 标志说明:
# 0x001 - 特殊池
# 0x002 - 强制 IRQL 检查
# 0x008 - 池跟踪
# 0x010 - I/O 验证
# 0x020 - 死锁检测
# 0x080 - DMA 检查
# 0x100 - 安全检查
# 0x800 - 杂项检查

# 查看状态
verifier /query

# 禁用
verifier /reset
```

### WinDbg 快速参考

```windbg
# 设置符号路径
.sympath srv*c:\symbols*https://msdl.microsoft.com/download/symbols
.sympath+ <your_driver_pdb_path>
.reload

# 加载 WDF 扩展
.load wdfkd
!wdfkd.wdfldr  ; 查看 WDF 驱动列表

# 常用命令
!devnode 0 1    ; 设备树
!devstack <pdo> ; 设备栈
!drvobj vnvme 7 ; 驱动对象详情
!object \Device ; 设备对象列表

# 内存
!pool <addr>    ; 池分配信息
!poolused       ; 池使用统计
!verifier       ; Verifier 信息
```

---

## 获取帮助

如果以上方法无法解决问题:

1. 收集以下信息:
   - 完整的内存转储 (如有蓝屏)
   - 驱动调试输出日志
   - 系统事件日志
   - 驱动版本和 Windows 版本

2. 参考资源:
   - [Windows Driver Documentation](https://learn.microsoft.com/windows-hardware/drivers/)
   - [NVMe Specification](https://nvmexpress.org/specifications/)
   - [OSR Online Forums](https://community.osr.com/)
