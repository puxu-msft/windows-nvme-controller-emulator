# 故障排除

本文档提供常见问题的诊断和解决方法。

## 诊断工具

### 驱动加载诊断

#### 检查驱动是否加载

```powershell
# 检查驱动状态
sc query vnvme_bus
sc query vnvme_emu

# 查看驱动信息
driverquery /v | findstr vnvme
```

#### 检查设备管理器

```powershell
# 列出所有 PCI 设备
Get-PnpDevice -Class "System" | Where-Object { $_.FriendlyName -like "*VNVME*" }

# 列出 NVMe 控制器
Get-PnpDevice -Class "SCSIAdapter" | Where-Object { $_.FriendlyName -like "*NVMe*" }

# 列出磁盘
Get-PnpDevice -Class "DiskDrive"
```

#### 查看系统事件日志

```powershell
# 查看驱动相关事件
Get-WinEvent -FilterHashtable @{
    LogName = 'System'
    ProviderName = 'Microsoft-Windows-Kernel-PnP', 'Service Control Manager'
} -MaxEvents 50 | Where-Object { $_.Message -like "*vnvme*" }
```

### WinDbg 调试

#### 常用调试命令

```
# 加载驱动符号
.reload /f vnvme_bus.sys
.reload /f vnvme_emu.sys

# 列出驱动
lm m vnvme*

# 查看驱动对象
!drvobj vnvme_bus 7
!drvobj vnvme_emu 7

# 查看设备栈
!devstack <device_object>

# 查看 IRP 队列
!irpfind

# 断点
bp vnvme_bus!DriverEntry
bp vnvme_emu!VnvmeProcessCommand

# 内存检查
!pool <address>
!poolused 2 vnvm

# 死锁检测
!deadlock
```

#### 驱动验证器

启用驱动验证器以检测驱动问题：

```powershell
# 启用验证器
verifier /standard /driver vnvme_bus.sys vnvme_emu.sys

# 查看验证器状态
verifier /querysettings

# 禁用验证器
verifier /reset
```

---

## 常见问题

### 问题 1: 驱动安装失败

#### 症状
```
pnputil 返回错误
设备管理器显示黄色感叹号
```

#### 诊断

```powershell
# 检查 INF 文件
infverif vnvme_bus.inf
infverif vnvme_emu.inf

# 检查签名
signtool verify /pa /v vnvme_bus.sys
signtool verify /pa /v vnvme_emu.sys
```

#### 解决方案

1. **确保测试签名已启用**
   ```powershell
   bcdedit /set testsigning on
   # 需要重启
   ```

2. **重新签名驱动**
   ```powershell
   # 使用测试证书签名
   signtool sign /v /s My /n "VNVME Test Certificate" /t http://timestamp.digicert.com vnvme_bus.sys
   ```

3. **检查 INF 语法**
   - 确保 INF 文件格式正确
   - 检查硬件 ID 是否匹配

### 问题 2: stornvme.sys 未加载

#### 症状
```
虚拟设备出现但 stornvme.sys 未加载
设备管理器显示 "未知设备"
```

#### 诊断

```
# 在 WinDbg 中
!devobj \Device\<virtual_device>
!devstack <pdo_address>
```

#### 解决方案

1. **检查 PCIe 类代码**
   ```c
   // 确保类代码正确
   PciConfig.ClassCode[0] = 0x02;  // Programming Interface: NVMe
   PciConfig.ClassCode[1] = 0x08;  // Sub-class: NVM Controller
   PciConfig.ClassCode[2] = 0x01;  // Base-class: Mass Storage
   ```

2. **验证 Vendor/Device ID**
   ```powershell
   # 检查 stornvme.sys 支持的硬件 ID
   Get-Content "C:\Windows\INF\stornvme.inf" | Select-String "VEN_"
   ```

3. **确保 BAR0 配置正确**
   - BAR0 必须是内存空间
   - 大小至少 16KB
   - 需要正确处理资源请求

### 问题 3: 设备创建后无磁盘出现

#### 症状
```
NVMe 控制器在设备管理器中出现
但没有对应的磁盘设备
```

#### 诊断

```powershell
# 检查 stornvme 日志
Get-WinEvent -ProviderName "stornvme" -MaxEvents 100

# 检查 NVMe 设备
Get-PhysicalDisk | Where-Object { $_.BusType -eq "NVMe" }
```

使用 WinDbg：
```
# 查看 Identify 命令响应
bp vnvme_emu!VnvmeProcessIdentify
```

#### 解决方案

1. **检查 Identify Controller 响应**
   - `NN` 字段必须 > 0
   - `SQES` 和 `CQES` 必须正确

2. **检查 Identify Namespace 响应**
   - `NSZE` 必须 > 0
   - `NCAP` 必须 > 0
   - `NLBAF` 至少为 0 (表示支持 1 种格式)
   - `LBAF[0].LBADS` 必须是 9 (512B) 或 12 (4KB)

3. **检查 Admin 队列初始化**
   ```
   # 在 WinDbg 中设置断点
   bp vnvme_emu!VnvmeInitializeAdminQueues
   bp vnvme_emu!VnvmeProcessCreateIoCq
   bp vnvme_emu!VnvmeProcessCreateIoSq
   ```

### 问题 4: I/O 操作失败

#### 症状
```
磁盘出现但读写失败
文件系统格式化失败
```

#### 诊断

```powershell
# 检查磁盘状态
Get-Disk | Format-List

# 使用 diskpart
diskpart
list disk
select disk X
detail disk
```

使用 WinDbg：
```
bp vnvme_emu!VnvmeProcessRead
bp vnvme_emu!VnvmeProcessWrite
```

#### 解决方案

1. **检查 PRP 处理**
   ```c
   // 确保 PRP 地址映射正确
   NTSTATUS VnvmeReadFromPrp(...) {
       // 验证物理地址有效
       if (!MmIsAddressValid(...)) {
           return STATUS_INVALID_PARAMETER;
       }
   }
   ```

2. **检查后端 I/O**
   - 确保后端文件/内存可访问
   - 检查偏移量计算

3. **验证完成条目**
   - 确保 Phase bit 正确
   - 确保 Status 字段正确

### 问题 5: 系统蓝屏 (BSOD)

#### 常见蓝屏代码

| 代码 | 含义 | 可能原因 |
|------|------|----------|
| `DRIVER_IRQL_NOT_LESS_OR_EQUAL` | IRQL 违规 | 在高 IRQL 访问分页内存 |
| `KERNEL_DATA_INPAGE_ERROR` | 页面错误 | 后端 I/O 失败 |
| `BAD_POOL_HEADER` | 池损坏 | 内存越界写入 |
| `SYSTEM_THREAD_EXCEPTION_NOT_HANDLED` | 未处理异常 | 空指针、除零等 |

#### 分析 Dump 文件

```
# 在 WinDbg 中打开 dump
.open C:\Windows\Minidump\xxx.dmp

# 分析
!analyze -v

# 查看调用栈
k

# 查看出错代码
ub <address>
```

#### 常见修复

1. **IRQL 问题**
   ```c
   // 使用 PASSIVE_LEVEL 操作
   PAGED_CODE();
   
   // 或确保在正确 IRQL
   ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
   ```

2. **内存访问问题**
   ```c
   // 验证指针
   if (!MmIsAddressValid(ptr)) {
       return STATUS_INVALID_PARAMETER;
   }
   
   // 使用 try/except
   __try {
       RtlCopyMemory(dest, src, len);
   } __except (EXCEPTION_EXECUTE_HANDLER) {
       return GetExceptionCode();
   }
   ```

### 问题 6: 性能问题

#### 症状
```
I/O 性能远低于预期
高延迟
```

#### 诊断

```powershell
# 使用 diskspd 测试
diskspd -d30 -t1 -o1 -b4K -r -L \\.\PhysicalDriveX

# 使用性能监视器
perfmon
# 添加 PhysicalDisk 计数器
```

#### 解决方案

1. **减少内存复制**
   ```c
   // 使用直接 I/O
   // 避免不必要的缓冲区复制
   ```

2. **启用中断合并**
   ```c
   Controller->IntCoalescing.Threshold = 16;
   Controller->IntCoalescing.Time = 100;  // 100us
   ```

3. **增加 I/O 队列深度**
   ```c
   Controller->MaxQueueEntries = 1024;
   ```

4. **使用异步 I/O**
   ```c
   // 后端使用异步操作
   Backend->Operations->WriteAsync(...);
   ```

### 问题 7: 内存泄漏

#### 诊断

```
# 在 WinDbg 中
!poolused 2 vnvm
!poolused 2 VNVM

# 使用驱动验证器池跟踪
verifier /flags 0x100 /driver vnvme_bus.sys vnvme_emu.sys
```

#### 解决方案

1. **确保释放所有分配**
   ```c
   // 在卸载时
   while (!IsListEmpty(&Controller->IoSqList)) {
       PLIST_ENTRY entry = RemoveHeadList(&Controller->IoSqList);
       PVNVME_SUBMISSION_QUEUE sq = CONTAINING_RECORD(...);
       ExFreePoolWithTag(sq, VNVME_POOL_TAG);
   }
   ```

2. **使用调试池标签**
   ```c
   #define VNVME_TAG_QUEUE  'QVNM'
   #define VNVME_TAG_CMD    'CVNM'
   // 方便追踪来源
   ```

---

## 调试日志

### 启用详细日志

在调试版本中，可以增加日志输出：

```c
// 定义日志级别
#define VNVME_LOG_ERROR    1
#define VNVME_LOG_WARNING  2
#define VNVME_LOG_INFO     3
#define VNVME_LOG_DEBUG    4
#define VNVME_LOG_TRACE    5

extern ULONG g_VnvmeLogLevel;

#define VnvmeLog(level, fmt, ...) \
    do { \
        if (level <= g_VnvmeLogLevel) { \
            DbgPrint("[VNVME:%d] " fmt "\n", level, ##__VA_ARGS__); \
        } \
    } while(0)
```

### 使用 ETW 跟踪

```powershell
# 启动跟踪
logman create trace vnvme -p {VNVME-GUID} -o vnvme.etl
logman start vnvme

# 重现问题...

# 停止跟踪
logman stop vnvme

# 查看
tracefmt vnvme.etl -o vnvme.txt
```

---

## 参考信息

### 相关文档
- [NVMe Base Specification](https://nvmexpress.org/specifications/)
- [WDK 文档](https://docs.microsoft.com/en-us/windows-hardware/drivers/)
- [stornvme.sys 源码](https://github.com/microsoft/Windows-driver-samples)

### 有用的工具
- WinDbg / WinDbg Preview
- Driver Verifier
- DebugView
- Process Monitor
- DiskSPD
- NVMe-CLI (如有 Windows 版本)

### 社区资源
- [OSR Online](https://www.osronline.com/)
- [NTDEV 邮件列表](https://www.osronline.com/page.cfm?name=ntdev)
