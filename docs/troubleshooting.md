# 故障排查指南

本文档提供 Virtual NVMe StorPort Miniport 驱动开发和使用过程中常见问题的排查方法。

## 目录

1. [驱动加载问题](#驱动加载问题)
2. [蓝屏 (BSOD) 问题](#蓝屏-bsod-问题)
3. [设备不显示问题](#设备不显示问题)
4. [I/O 错误问题](#io-错误问题)
5. [性能问题](#性能问题)
6. [调试技巧](#调试技巧)
7. [StorPort 特定问题](#storport-特定问题)

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

4. 检查安全启动状态:
```powershell
Confirm-SecureBootUEFI
# 如果返回 True，可能需要禁用安全启动或使用生产签名
```

### 问题: 驱动加载后立即卸载

**症状**: 设备管理器中设备带黄色感叹号

**排查步骤**:

1. 查看设备状态码:
```powershell
Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Virtual NVMe*" } | 
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
| Code 10 | 设备无法启动 | 检查 HwFindAdapter 返回值 |
| Code 28 | 驱动未安装 | 重新安装 INF |
| Code 31 | 设备工作不正常 | 检查 HwInitialize 实现 |
| Code 37 | 驱动初始化失败 | 检查 DriverEntry |
| Code 52 | 数字签名问题 | 启用测试签名 |

### 问题: DriverEntry 返回失败

**调试方法**:

1. 在 DriverEntry 开始处设置断点:
```windbg
bp vnvme!DriverEntry
g
```

2. 检查 StorPortInitialize 调用:
```windbg
# 检查 HW_INITIALIZATION_DATA 结构
dt vnvme!HW_INITIALIZATION_DATA @rsp+xxx
```

3. 常见失败原因:
   - `HW_INITIALIZATION_DATA` 结构版本不正确
   - 回调函数指针为 NULL
   - `DeviceExtensionSize` 设置不正确

```c
// 正确的 HW_INITIALIZATION_DATA 初始化
HW_INITIALIZATION_DATA hwInitData;
RtlZeroMemory(&hwInitData, sizeof(HW_INITIALIZATION_DATA));

hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);
hwInitData.AdapterInterfaceType = Internal;  // 虚拟设备使用 Internal
hwInitData.HwFindAdapter = VNvmeHwFindAdapter;
hwInitData.HwInitialize = VNvmeHwInitialize;
hwInitData.HwStartIo = VNvmeHwStartIo;
hwInitData.HwResetBus = VNvmeHwResetBus;
hwInitData.HwAdapterControl = VNvmeHwAdapterControl;

hwInitData.DeviceExtensionSize = sizeof(VNVME_ADAPTER_EXTENSION);
hwInitData.SpecificLuExtensionSize = sizeof(VNVME_LU_EXTENSION);
hwInitData.SrbExtensionSize = sizeof(VNVME_SRB_EXTENSION);

hwInitData.NumberOfAccessRanges = 0;  // 虚拟设备不需要
hwInitData.MapBuffers = STOR_MAP_ALL_BUFFERS;
hwInitData.NeedPhysicalAddresses = FALSE;
hwInitData.TaggedQueuing = TRUE;
hwInitData.AutoRequestSense = TRUE;
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
| 0x7F | UNEXPECTED_KERNEL_MODE_TRAP | 内核陷阱 |
| 0xD1 | DRIVER_IRQL_NOT_LESS_OR_EQUAL | 驱动 IRQL 错误 |
| 0x133 | DPC_WATCHDOG_VIOLATION | DPC 运行时间过长 |

### 分析蓝屏转储

1. 收集 MEMORY.DMP:
```powershell
# 确保完整转储已启用
Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" | 
    Select-Object CrashDumpEnabled
# 1 = Complete dump, 2 = Kernel dump, 3 = Small dump
```

2. 使用 WinDbg 分析:
```windbg
# 打开转储文件
.opendump C:\Windows\MEMORY.DMP

# 自动分析
!analyze -v

# 查看调用栈
kb

# 查看故障模块
lmvm vnvme

# 查看故障地址处的代码
u @rip
```

### StorPort Miniport 常见蓝屏原因

#### IRQL_NOT_LESS_OR_EQUAL

```c
// 错误: 在 DISPATCH_LEVEL 调用分页函数
VOID VNvmeHwStartIo(...) {
    // 错误! 不能在 DISPATCH_LEVEL 调用 ZwCreateFile
    HANDLE hFile;
    ZwCreateFile(&hFile, ...);  // BSOD!
}

// 正确: 使用工作项或异步 I/O
VOID VNvmeHwStartIo(...) {
    // 在 HwBuildIo (PASSIVE_LEVEL) 或使用 StorPort 工作项
    StorPortQueueWorkItem(pAdapter, VNvmeFileIoWorkItem, ...);
}
```

#### DPC_WATCHDOG_VIOLATION

```c
// 错误: HwStartIo 执行时间过长
VOID VNvmeHwStartIo(...) {
    // 同步等待，导致 DPC 超时
    while (!backendComplete) {
        KeStallExecutionProcessor(1000);  // BSOD!
    }
}

// 正确: 异步完成
VOID VNvmeHwStartIo(...) {
    // 提交 I/O 并立即返回
    SubmitBackendIoAsync(pSrb, VNvmeIoComplete);
    return TRUE;  // 告诉 StorPort 请求正在处理
}

VOID VNvmeIoComplete(...) {
    pSrb->SrbStatus = SRB_STATUS_SUCCESS;
    StorPortNotification(RequestComplete, pAdapter, pSrb);
}
```

---

## 设备不显示问题

### 问题: 虚拟磁盘未出现在磁盘管理中

**排查步骤**:

1. 检查设备是否已枚举:
```powershell
Get-PnpDevice -Class SCSIAdapter | 
    Where-Object { $_.FriendlyName -like "*VNvme*" }
```

2. 检查 StorPort 是否识别适配器:
```powershell
Get-WmiObject -Class Win32_SCSIController | 
    Where-Object { $_.Name -like "*Virtual NVMe*" }
```

3. 检查 LUN 是否已报告:
```powershell
# 使用 diskpart
diskpart
list disk
```

4. 检查事件日志:
```powershell
Get-WinEvent -LogName System -ProviderName "disk" -MaxEvents 20
Get-WinEvent -LogName System -ProviderName "storvsc" -MaxEvents 20
```

### 问题: HwFindAdapter 返回成功但无设备

**检查点**:

```c
// 确保正确设置了 PORT_CONFIGURATION_INFORMATION
ULONG VNvmeHwFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PBOOLEAN Again)
{
    // 1. 设置 VirtualDevice = TRUE (关键!)
    ConfigInfo->VirtualDevice = TRUE;
    
    // 2. 设置最大传输长度
    ConfigInfo->MaximumTransferLength = VNVME_MAX_TRANSFER_LENGTH;
    
    // 3. 设置总线/目标/LUN 数量
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = 1;
    ConfigInfo->MaximumNumberOfLogicalUnits = VNVME_MAX_LUNS;
    
    // 4. 启用必要特性
    ConfigInfo->CachesData = TRUE;
    ConfigInfo->MapBuffers = STOR_MAP_ALL_BUFFERS;
    ConfigInfo->NeedPhysicalAddresses = FALSE;
    
    // 5. 设置对齐要求
    ConfigInfo->AlignmentMask = 0;  // 无特殊对齐要求
    
    *Again = FALSE;
    return SP_RETURN_FOUND;
}
```

### 问题: LUN 未报告

**检查 INQUIRY 处理**:

```c
// 确保 INQUIRY 返回正确的设备类型
UCHAR VNvmeHandleInquiry(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _In_ PVNVME_LU_EXTENSION pLu,
    _Inout_ PSCSI_REQUEST_BLOCK pSrb)
{
    PINQUIRYDATA pInquiry = (PINQUIRYDATA)pSrb->DataBuffer;
    
    // LUN 不存在时返回错误
    if (!pLu->Flags.Present) {
        return SRB_STATUS_NO_DEVICE;
    }
    
    RtlZeroMemory(pInquiry, pSrb->DataTransferLength);
    
    // 设置设备类型 (0x00 = 直接访问设备/磁盘)
    pInquiry->DeviceType = DIRECT_ACCESS_DEVICE;
    pInquiry->DeviceTypeQualifier = DEVICE_CONNECTED;
    
    // 设置响应格式 (必须为 2)
    pInquiry->ResponseDataFormat = 2;
    
    // 设置附加长度
    pInquiry->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
    
    // 设置供应商/产品信息
    RtlCopyMemory(pInquiry->VendorId, "VNVME   ", 8);
    RtlCopyMemory(pInquiry->ProductId, "Virtual NVMe    ", 16);
    RtlCopyMemory(pInquiry->ProductRevisionLevel, "1.0 ", 4);
    
    return SRB_STATUS_SUCCESS;
}
```

---

## I/O 错误问题

### 问题: 读写操作失败

**排查步骤**:

1. 检查系统事件日志:
```powershell
Get-WinEvent -LogName System | 
    Where-Object { $_.Message -like "*disk*" -or $_.Message -like "*storage*" } |
    Select-Object -First 20
```

2. 使用 stordiag 收集日志:
```cmd
stordiag.exe -collectEtw -out C:\StorDiag
```

3. 检查 SRB 状态:
```windbg
# 设置 HwStartIo 断点
bp vnvme!VNvmeHwStartIo

# 查看 SRB
dt storport!_SCSI_REQUEST_BLOCK @rcx
? @rcx->SrbStatus
```

### 问题: I/O 超时

**常见原因与解决方案**:

| 原因 | 解决方案 |
|------|----------|
| 后端 I/O 阻塞 | 使用异步 I/O |
| 未调用 StorPortNotification | 确保所有路径都调用 |
| 死锁 | 检查锁的使用 |

**正确的 I/O 完成模式**:

```c
// 同步完成 (简单但可能阻塞)
BOOLEAN VNvmeHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    
    // 快速处理的命令可以同步完成
    UCHAR srbStatus = VNvmeProcessSrb(pAdapter, Srb);
    
    Srb->SrbStatus = srbStatus;
    StorPortNotification(RequestComplete, pAdapter, Srb);
    StorPortNotification(NextRequest, pAdapter);
    
    return TRUE;
}

// 异步完成 (推荐)
BOOLEAN VNvmeHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    PVNVME_SRB_EXTENSION pSrbExt = StorPortGetSrbExtension(pAdapter, Srb);
    
    // 保存上下文
    pSrbExt->pAdapter = pAdapter;
    pSrbExt->pSrb = Srb;
    
    // 提交到工作队列
    StorPortQueueWorkItem(
        pAdapter,
        VNvmeWorkItemCallback,
        pSrbExt,
        VNvmeWorkItemCallbackComplete);
    
    return TRUE;  // 告诉 StorPort 请求正在异步处理
}

VOID VNvmeWorkItemCallback(
    _In_ PVOID pAdapter,
    _In_ PVOID pContext,
    _In_ PVOID pWorkItem)
{
    PVNVME_SRB_EXTENSION pSrbExt = (PVNVME_SRB_EXTENSION)pContext;
    
    // 执行 I/O (可以在 PASSIVE_LEVEL)
    UCHAR srbStatus = VNvmeProcessSrb(pSrbExt->pAdapter, pSrbExt->pSrb);
    pSrbExt->pSrb->SrbStatus = srbStatus;
    
    // 完成请求
    StorPortNotification(RequestComplete, pSrbExt->pAdapter, pSrbExt->pSrb);
    StorPortNotification(NextRequest, pSrbExt->pAdapter);
    
    // 释放工作项
    StorPortFreeWorkItem(pAdapter, pWorkItem);
}
```

---

## 性能问题

### 问题: I/O 性能低于预期

**诊断步骤**:

1. 使用性能监视器:
```powershell
# 收集磁盘计数器
Get-Counter -Counter "\PhysicalDisk(*)\*" -SampleInterval 1 -MaxSamples 60 | 
    Export-Counter -Path disk_perf.csv -FileFormat CSV
```

2. 使用 diskspd 基准测试:
```cmd
diskspd -c1G -d30 -r -t8 -o32 -b4K -Sh X:\testfile.dat
```

3. 检查队列深度:
```powershell
Get-Counter "\PhysicalDisk(*)\Current Disk Queue Length"
```

### 性能优化检查点

| 检查项 | 说明 | 优化方法 |
|--------|------|----------|
| HwBuildIo 使用 | 预处理 SRB | 在 HwBuildIo 中准备数据 |
| 内存复制 | 减少数据复制 | 使用直接 I/O |
| 锁竞争 | 减少锁持有时间 | 使用无锁算法或细粒度锁 |
| 队列深度 | 虚拟设备为 250 | 确保后端能处理 |
| 对齐 | I/O 对齐 | 使用扇区对齐的缓冲区 |

### 使用 HwBuildIo 优化

```c
//
// HwBuildIo - 在 PASSIVE_LEVEL 执行，用于预处理
//
BOOLEAN VNvmeHwBuildIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    PVNVME_SRB_EXTENSION pSrbExt = StorPortGetSrbExtension(pAdapter, Srb);
    PCDB pCdb = (PCDB)Srb->Cdb;
    
    // 初始化 SRB 扩展
    RtlZeroMemory(pSrbExt, sizeof(VNVME_SRB_EXTENSION));
    pSrbExt->pAdapter = pAdapter;
    
    // 预解析 CDB
    switch (pCdb->CDB6GENERIC.OperationCode) {
        case SCSIOP_READ:
        case SCSIOP_READ16:
            pSrbExt->IsRead = TRUE;
            pSrbExt->StartLba = VNvmeGetLbaFromCdb(pCdb, Srb->CdbLength);
            pSrbExt->BlockCount = VNvmeGetBlockCountFromCdb(pCdb, Srb->CdbLength);
            break;
            
        case SCSIOP_WRITE:
        case SCSIOP_WRITE16:
            pSrbExt->IsRead = FALSE;
            pSrbExt->StartLba = VNvmeGetLbaFromCdb(pCdb, Srb->CdbLength);
            pSrbExt->BlockCount = VNvmeGetBlockCountFromCdb(pCdb, Srb->CdbLength);
            break;
    }
    
    // 获取数据缓冲区物理地址 (如果后端需要)
    if (Srb->DataBuffer && Srb->DataTransferLength > 0) {
        pSrbExt->DataBuffer = Srb->DataBuffer;
        pSrbExt->DataLength = Srb->DataTransferLength;
    }
    
    return TRUE;  // 继续调用 HwStartIo
}
```

---

## StorPort 特定问题

### 问题: StorPortNotification 死锁

**症状**: 系统挂起，无响应

**原因**: 在错误的 IRQL 或上下文中调用 StorPortNotification

**解决方案**:

```c
// 错误: 在 DPC 中嵌套调用
VOID SomeCallbackAtDpcLevel() {
    // 可能导致死锁
    StorPortNotification(RequestComplete, pAdapter, pSrb);
}

// 正确: 确保在适当的上下文中调用
VOID VNvmeCompleteRequest(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _In_ PSCSI_REQUEST_BLOCK pSrb)
{
    // 检查当前 IRQL
    KIRQL currentIrql = KeGetCurrentIrql();
    
    if (currentIrql <= DISPATCH_LEVEL) {
        StorPortNotification(RequestComplete, pAdapter, pSrb);
        StorPortNotification(NextRequest, pAdapter);
    } else {
        // 需要降低 IRQL (不应该发生在正确设计的驱动中)
        NT_ASSERT(FALSE);
    }
}
```

### 问题: 适配器重置循环

**症状**: 设备反复重置

**排查**:

```powershell
# 查看 StorPort 事件
Get-WinEvent -LogName System -ProviderName "storvsc" -MaxEvents 50 |
    Where-Object { $_.Message -like "*reset*" }
```

**常见原因**:

1. HwResetBus 实现不正确
2. 命令超时过多
3. HwStartIo 未正确完成请求

```c
//
// 正确的 HwResetBus 实现
//
BOOLEAN VNvmeHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    
    UNREFERENCED_PARAMETER(PathId);
    
    // 完成所有待处理请求
    VNvmeCompleteAllPendingRequests(pAdapter, SRB_STATUS_BUS_RESET);
    
    // 重置后端状态
    VNvmeResetBackendState(pAdapter);
    
    // 通知 StorPort 重置完成
    StorPortCompleteServiceIrp(pAdapter, NULL);
    
    return TRUE;
}
```

### 问题: LUN 枚举失败

**检查 REPORT LUNS 实现**:

```c
UCHAR VNvmeHandleReportLuns(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _Inout_ PSCSI_REQUEST_BLOCK pSrb)
{
    PLUN_LIST pLunList = (PLUN_LIST)pSrb->DataBuffer;
    ULONG bufferSize = pSrb->DataTransferLength;
    ULONG lunCount = 0;
    
    RtlZeroMemory(pLunList, bufferSize);
    
    // 计算 LUN 列表大小
    ULONG requiredSize = sizeof(LUN_LIST);
    
    for (ULONG i = 0; i < VNVME_MAX_LUNS; i++) {
        if (pAdapter->Luns[i].Flags.Present) {
            requiredSize += 8;  // 每个 LUN 8 字节
            lunCount++;
        }
    }
    
    // 设置列表长度 (大端)
    ULONG listLength = lunCount * 8;
    pLunList->LunListLength[0] = (UCHAR)(listLength >> 24);
    pLunList->LunListLength[1] = (UCHAR)(listLength >> 16);
    pLunList->LunListLength[2] = (UCHAR)(listLength >> 8);
    pLunList->LunListLength[3] = (UCHAR)(listLength);
    
    // 填充 LUN 列表
    PUCHAR pLunData = (PUCHAR)(pLunList + 1);
    for (ULONG i = 0; i < VNVME_MAX_LUNS && 
         (pLunData - (PUCHAR)pLunList) + 8 <= bufferSize; i++) {
        if (pAdapter->Luns[i].Flags.Present) {
            // 使用平面 LUN 寻址 (简单)
            RtlZeroMemory(pLunData, 8);
            pLunData[1] = (UCHAR)i;  // LUN 号在第二字节
            pLunData += 8;
        }
    }
    
    pSrb->DataTransferLength = min(requiredSize, bufferSize);
    return SRB_STATUS_SUCCESS;
}
```

---

## 常用诊断命令

### PowerShell 诊断脚本

```powershell
# VNvme 诊断脚本
function Get-VNvmeDiagnostics {
    Write-Host "=== VNvme Diagnostics ===" -ForegroundColor Cyan
    
    # 1. 检查驱动状态
    Write-Host "`n[Driver Status]" -ForegroundColor Yellow
    Get-PnpDevice -Class SCSIAdapter | 
        Where-Object { $_.FriendlyName -like "*VNvme*" -or 
                       $_.FriendlyName -like "*Virtual NVMe*" } |
        Format-Table Status, InstanceId, FriendlyName
    
    # 2. 检查磁盘
    Write-Host "`n[Disk Status]" -ForegroundColor Yellow
    Get-Disk | Where-Object { $_.FriendlyName -like "*VNvme*" } |
        Format-Table Number, FriendlyName, Size, HealthStatus
    
    # 3. 检查分区
    Write-Host "`n[Partition Status]" -ForegroundColor Yellow
    Get-Disk | Where-Object { $_.FriendlyName -like "*VNvme*" } |
        Get-Partition | Format-Table DiskNumber, PartitionNumber, Size, DriveLetter
    
    # 4. 最近错误
    Write-Host "`n[Recent Errors]" -ForegroundColor Yellow
    Get-WinEvent -LogName System -MaxEvents 100 |
        Where-Object { $_.ProviderName -like "*disk*" -or 
                       $_.ProviderName -like "*stor*" } |
        Where-Object { $_.LevelDisplayName -eq "Error" -or 
                       $_.LevelDisplayName -eq "Warning" } |
        Select-Object -First 10 |
        Format-Table TimeCreated, ProviderName, Message -Wrap
}

# 运行诊断
Get-VNvmeDiagnostics
```

### WinDbg 常用命令

```windbg
# 加载符号
.reload /f vnvme.sys

# 查看驱动信息
lmvm vnvme

# 列出导出函数
x vnvme!*

# 设置断点
bp vnvme!VNvmeHwStartIo
bp vnvme!VNvmeHwFindAdapter

# 查看适配器扩展
dt vnvme!VNVME_ADAPTER_EXTENSION poi(vnvme!g_pAdapterExtension)

# 查看 LUN 扩展
dt vnvme!VNVME_LU_EXTENSION (poi(vnvme!g_pAdapterExtension)+xxx)

# 查看 SRB
dt storport!_SCSI_REQUEST_BLOCK @rcx

# StorPort 调试扩展
!stordump
!storlog

# 跟踪 SCSI 命令
!scsilog

# 查看 I/O 栈
!devstack <device_object>
```

---

## 参考资料

- [StorPort Driver Debugging](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storage-debugging)
- [WinDbg Commands](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/commands)
- [Bug Check Code Reference](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-code-reference2)
- [SCSI Debugger Extensions](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/scsi-debugger-extensions)
