# 测试策略

本文档描述 Virtual NVMe StorPort Miniport 驱动的测试方法和策略。

## 测试层次

```
┌─────────────────────────────────┐
│      WHQL/HLK 认证测试          │  ← Windows 硬件认证
├─────────────────────────────────┤
│      系统集成测试               │  ← 完整系统验证
├─────────────────────────────────┤
│      功能测试                   │  ← SCSI 命令验证
├─────────────────────────────────┤
│      单元测试                   │  ← 模块验证
└─────────────────────────────────┘
```

---

## 测试环境配置

### 推荐使用虚拟机测试

⚠️ **重要**: 内核驱动开发容易导致蓝屏，强烈建议在虚拟机中进行测试。

#### Hyper-V 虚拟机配置

```powershell
# 创建测试虚拟机
New-VM -Name "VNvmeTest" -MemoryStartupBytes 4GB -Generation 2

# 创建系统虚拟硬盘
New-VHD -Path "C:\VMs\VNvmeTest.vhdx" -SizeBytes 60GB -Dynamic

# 添加虚拟硬盘
Add-VMHardDiskDrive -VMName "VNvmeTest" -Path "C:\VMs\VNvmeTest.vhdx"

# 配置处理器
Set-VMProcessor -VMName "VNvmeTest" -Count 4

# 配置网络适配器 (用于文件传输)
Add-VMNetworkAdapter -VMName "VNvmeTest" -SwitchName "Default Switch"

# 启用增强会话模式
Set-VM -VMName "VNvmeTest" -EnhancedSessionTransportType HvSocket
```

#### VMware Workstation 配置

```
1. 创建 Windows 10/11 x64 虚拟机
2. 配置:
   - 内存: 4GB+
   - 处理器: 4 核心
   - 硬盘: 60GB+
3. 添加串口 (用于内核调试):
   - 使用命名管道: \\.\pipe\vnvme_debug
   - "This end is the server"
   - "The other end is an application"
4. 在虚拟机设置中启用 "Virtualize Intel VT-x/EPT"
```

### 内核调试配置

#### 虚拟机内配置

```cmd
rem 启用内核调试 (COM1)
bcdedit /debug on
bcdedit /dbgsettings serial debugport:1 baudrate:115200

rem 或使用网络调试 (需要 Windows 8+)
bcdedit /debug on
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000 key:1.2.3.4

rem 启用测试签名
bcdedit /set testsigning on

rem 禁用驱动签名强制
bcdedit /set nointegritychecks on

rem 重启生效
shutdown /r /t 0
```

#### 主机 WinDbg 连接

```powershell
# 串口调试
windbg -k com:port=\\.\pipe\vnvme_debug,baud=115200,pipe

# 网络调试
windbg -k net:port=50000,key=1.2.3.4
```

---

## 单元测试

### 测试框架选择

- **推荐**: 使用 WDK 内置的 TAEF (Test Authoring and Execution Framework)
- **替代**: 简单的内核测试驱动

### 模块测试用例

#### 后端抽象层测试

```c
//
// 测试文件: test_backend.c
//

// 测试内存后端初始化
void Test_MemoryBackend_Init()
{
    VNVME_BACKEND_CONFIG config = {0};
    config.Type = VNVME_BACKEND_MEMORY;
    config.SizeBytes = 1024 * 1024 * 1024;  // 1 GB
    config.BlockSize = 512;
    
    PVOID pContext = NULL;
    NTSTATUS status = VNvmeMemoryBackendInit(&config, &pContext);
    
    ASSERT(NT_SUCCESS(status));
    ASSERT(pContext != NULL);
    
    VNvmeMemoryBackendCleanup(pContext);
}

// 测试读写操作
void Test_MemoryBackend_ReadWrite()
{
    // 初始化后端
    VNVME_BACKEND_CONFIG config = {0};
    config.Type = VNVME_BACKEND_MEMORY;
    config.SizeBytes = 1024 * 1024;  // 1 MB
    config.BlockSize = 512;
    
    PVOID pContext = NULL;
    VNvmeMemoryBackendInit(&config, &pContext);
    
    // 准备测试数据
    UCHAR writeBuffer[512];
    UCHAR readBuffer[512];
    
    for (int i = 0; i < 512; i++) {
        writeBuffer[i] = (UCHAR)(i & 0xFF);
    }
    
    // 写入
    NTSTATUS status = VNvmeMemoryBackendWrite(
        pContext, 0, writeBuffer, 512);
    ASSERT(NT_SUCCESS(status));
    
    // 读取
    status = VNvmeMemoryBackendRead(
        pContext, 0, readBuffer, 512);
    ASSERT(NT_SUCCESS(status));
    
    // 验证
    ASSERT(RtlCompareMemory(writeBuffer, readBuffer, 512) == 512);
    
    VNvmeMemoryBackendCleanup(pContext);
}

// 测试边界条件
void Test_MemoryBackend_Boundary()
{
    VNVME_BACKEND_CONFIG config = {0};
    config.Type = VNVME_BACKEND_MEMORY;
    config.SizeBytes = 4096;  // 8 个 512 字节块
    config.BlockSize = 512;
    
    PVOID pContext = NULL;
    VNvmeMemoryBackendInit(&config, &pContext);
    
    UCHAR buffer[512];
    
    // 测试最后一个有效块
    NTSTATUS status = VNvmeMemoryBackendRead(
        pContext, 7, buffer, 512);
    ASSERT(NT_SUCCESS(status));
    
    // 测试超出范围
    status = VNvmeMemoryBackendRead(
        pContext, 8, buffer, 512);
    ASSERT(status == STATUS_INVALID_PARAMETER);
    
    VNvmeMemoryBackendCleanup(pContext);
}
```

#### SCSI 命令处理测试

```c
//
// 测试文件: test_scsi.c
//

// 测试 INQUIRY 命令
void Test_Scsi_Inquiry()
{
    SCSI_REQUEST_BLOCK srb = {0};
    UCHAR cdb[16] = {0};
    UCHAR dataBuffer[96];
    UCHAR senseBuffer[32];
    
    srb.CdbLength = 6;
    srb.Cdb = cdb;
    srb.DataBuffer = dataBuffer;
    srb.DataTransferLength = 96;
    srb.SenseInfoBuffer = senseBuffer;
    srb.SenseInfoBufferLength = 32;
    
    // 构建 INQUIRY CDB
    cdb[0] = SCSIOP_INQUIRY;
    cdb[4] = 96;  // Allocation length
    
    // 创建模拟 LUN
    VNVME_LU_EXTENSION lu = {0};
    lu.Flags.Present = TRUE;
    lu.Flags.Online = TRUE;
    RtlCopyMemory(lu.Identity.VendorId, "VNVME   ", 8);
    RtlCopyMemory(lu.Identity.ProductId, "Virtual NVMe    ", 16);
    
    // 执行命令
    UCHAR srbStatus = VNvmeHandleInquiry(NULL, &lu, &srb);
    
    // 验证
    ASSERT(srbStatus == SRB_STATUS_SUCCESS);
    ASSERT(srb.DataTransferLength == 96);
    
    PINQUIRYDATA pInquiry = (PINQUIRYDATA)dataBuffer;
    ASSERT(pInquiry->DeviceType == DIRECT_ACCESS_DEVICE);
    ASSERT(RtlCompareMemory(pInquiry->VendorId, "VNVME   ", 8) == 8);
}

// 测试 READ CAPACITY 命令
void Test_Scsi_ReadCapacity()
{
    SCSI_REQUEST_BLOCK srb = {0};
    UCHAR cdb[16] = {0};
    READ_CAPACITY_DATA capacityData;
    
    srb.CdbLength = 10;
    srb.Cdb = cdb;
    srb.DataBuffer = &capacityData;
    srb.DataTransferLength = sizeof(capacityData);
    
    // 构建 READ CAPACITY(10) CDB
    cdb[0] = SCSIOP_READ_CAPACITY;
    
    // 创建模拟 LUN (1 GB, 512 字节块)
    VNVME_LU_EXTENSION lu = {0};
    lu.Flags.Present = TRUE;
    lu.Flags.Online = TRUE;
    lu.BlockSize = 512;
    lu.BlockCount = 2097152;  // 1 GB / 512
    
    // 执行命令
    UCHAR srbStatus = VNvmeHandleReadCapacity10(NULL, &lu, &srb);
    
    // 验证
    ASSERT(srbStatus == SRB_STATUS_SUCCESS);
    
    // 解析返回数据 (大端)
    ULONG lastLba = (capacityData.LogicalBlockAddress[0] << 24) |
                    (capacityData.LogicalBlockAddress[1] << 16) |
                    (capacityData.LogicalBlockAddress[2] << 8) |
                    capacityData.LogicalBlockAddress[3];
    ULONG blockLen = (capacityData.BytesPerBlock[0] << 24) |
                     (capacityData.BytesPerBlock[1] << 16) |
                     (capacityData.BytesPerBlock[2] << 8) |
                     capacityData.BytesPerBlock[3];
    
    ASSERT(lastLba == 2097151);  // BlockCount - 1
    ASSERT(blockLen == 512);
}

// 测试 READ/WRITE 命令
void Test_Scsi_ReadWrite()
{
    // 创建带内存后端的 LUN
    VNVME_LU_EXTENSION lu = {0};
    lu.Flags.Present = TRUE;
    lu.Flags.Online = TRUE;
    lu.BlockSize = 512;
    lu.BlockCount = 2048;
    
    // 初始化内存后端
    VNVME_BACKEND_CONFIG config = {0};
    config.Type = VNVME_BACKEND_MEMORY;
    config.SizeBytes = 2048 * 512;
    config.BlockSize = 512;
    VNvmeMemoryBackendInit(&config, &lu.pBackendContext);
    lu.pBackend = &VNvmeMemoryBackendOps;
    
    // 准备写入数据
    UCHAR writeData[512];
    for (int i = 0; i < 512; i++) {
        writeData[i] = (UCHAR)i;
    }
    
    // WRITE 命令
    SCSI_REQUEST_BLOCK writeSrb = {0};
    UCHAR writeCdb[10] = {0};
    writeCdb[0] = SCSIOP_WRITE;
    writeCdb[5] = 100;  // LBA = 100
    writeCdb[8] = 1;    // 传输 1 块
    
    writeSrb.CdbLength = 10;
    writeSrb.Cdb = writeCdb;
    writeSrb.DataBuffer = writeData;
    writeSrb.DataTransferLength = 512;
    
    UCHAR status = VNvmeHandleWrite(NULL, &lu, &writeSrb);
    ASSERT(status == SRB_STATUS_SUCCESS);
    
    // READ 命令
    SCSI_REQUEST_BLOCK readSrb = {0};
    UCHAR readCdb[10] = {0};
    UCHAR readData[512];
    
    readCdb[0] = SCSIOP_READ;
    readCdb[5] = 100;  // LBA = 100
    readCdb[8] = 1;    // 传输 1 块
    
    readSrb.CdbLength = 10;
    readSrb.Cdb = readCdb;
    readSrb.DataBuffer = readData;
    readSrb.DataTransferLength = 512;
    
    status = VNvmeHandleRead(NULL, &lu, &readSrb);
    ASSERT(status == SRB_STATUS_SUCCESS);
    
    // 验证数据一致性
    ASSERT(RtlCompareMemory(writeData, readData, 512) == 512);
    
    VNvmeMemoryBackendCleanup(lu.pBackendContext);
}
```

---

## 功能测试

### SCSI 命令完整测试

| 命令 | 测试点 | 预期结果 |
|------|--------|----------|
| TEST UNIT READY | LUN 在线 | SRB_STATUS_SUCCESS |
| TEST UNIT READY | LUN 离线 | CHECK CONDITION, NOT READY |
| INQUIRY | 标准 INQUIRY | 正确的设备类型和标识 |
| INQUIRY VPD 0x00 | 支持的页列表 | 返回支持的 VPD 页 |
| INQUIRY VPD 0x80 | 序列号 | 返回正确序列号 |
| INQUIRY VPD 0x83 | 设备标识 | 返回 NAA 标识符 |
| READ CAPACITY(10) | 小于 2TB | 正确的 LastLBA 和块大小 |
| READ CAPACITY(16) | 任意大小 | 正确的 64 位 LastLBA |
| READ(10) | 有效 LBA | 正确的数据 |
| READ(10) | 无效 LBA | CHECK CONDITION, LBA OUT OF RANGE |
| WRITE(10) | 有效 LBA | 数据正确写入 |
| WRITE(10) | 只读 LUN | CHECK CONDITION, WRITE PROTECTED |
| MODE SENSE(6) | 所有页 | 返回模式数据 |
| SYNCHRONIZE CACHE | - | 缓存刷新 |
| UNMAP | 有效范围 | 块释放成功 |
| REPORT LUNS | - | 返回 LUN 列表 |

### PowerShell 功能测试脚本

```powershell
#
# VNvme 功能测试脚本
#

# 获取 VNvme 磁盘
function Get-VNvmeDisk {
    Get-Disk | Where-Object { $_.FriendlyName -like "*VNvme*" -or 
                              $_.FriendlyName -like "*Virtual NVMe*" }
}

# 测试磁盘基本功能
function Test-VNvmeDisk {
    param(
        [Parameter(Mandatory=$true)]
        [int]$DiskNumber
    )
    
    $results = @{
        DiskNumber = $DiskNumber
        Tests = @()
    }
    
    # 测试 1: 获取磁盘信息
    $disk = Get-Disk -Number $DiskNumber
    $results.Tests += @{
        Name = "Get-Disk"
        Result = if ($disk) { "PASS" } else { "FAIL" }
        Details = $disk.Size
    }
    
    # 测试 2: 初始化磁盘
    try {
        if ($disk.PartitionStyle -eq "RAW") {
            Initialize-Disk -Number $DiskNumber -PartitionStyle GPT -ErrorAction Stop
        }
        $results.Tests += @{
            Name = "Initialize-Disk"
            Result = "PASS"
        }
    } catch {
        $results.Tests += @{
            Name = "Initialize-Disk"
            Result = "FAIL"
            Details = $_.Exception.Message
        }
    }
    
    # 测试 3: 创建分区
    try {
        $partition = New-Partition -DiskNumber $DiskNumber -UseMaximumSize -ErrorAction Stop
        $results.Tests += @{
            Name = "New-Partition"
            Result = "PASS"
        }
    } catch {
        $results.Tests += @{
            Name = "New-Partition"
            Result = "FAIL"
            Details = $_.Exception.Message
        }
    }
    
    # 测试 4: 格式化
    try {
        $volume = Format-Volume -Partition $partition -FileSystem NTFS -NewFileSystemLabel "VNvmeTest" -ErrorAction Stop
        $results.Tests += @{
            Name = "Format-Volume"
            Result = "PASS"
        }
    } catch {
        $results.Tests += @{
            Name = "Format-Volume"
            Result = "FAIL"
            Details = $_.Exception.Message
        }
    }
    
    # 测试 5: 分配驱动器号
    try {
        Add-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber 2 -AssignDriveLetter
        $driveLetter = (Get-Partition -DiskNumber $DiskNumber -PartitionNumber 2).DriveLetter
        $results.Tests += @{
            Name = "Assign-DriveLetter"
            Result = "PASS"
            Details = "$($driveLetter):"
        }
    } catch {
        $results.Tests += @{
            Name = "Assign-DriveLetter"
            Result = "FAIL"
            Details = $_.Exception.Message
        }
    }
    
    return $results
}

# I/O 测试
function Test-VNvmeIO {
    param(
        [Parameter(Mandatory=$true)]
        [string]$DriveLetter,
        
        [int]$FileSizeMB = 100,
        [int]$BlockSizeKB = 4
    )
    
    $testFile = "${DriveLetter}:\vnvme_test.bin"
    $results = @{
        DriveLetter = $DriveLetter
        Tests = @()
    }
    
    # 顺序写入测试
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    $buffer = New-Object byte[] ($BlockSizeKB * 1024)
    (New-Object Random).NextBytes($buffer)
    
    try {
        $stream = [System.IO.File]::OpenWrite($testFile)
        $totalBlocks = ($FileSizeMB * 1024) / $BlockSizeKB
        
        for ($i = 0; $i -lt $totalBlocks; $i++) {
            $stream.Write($buffer, 0, $buffer.Length)
        }
        $stream.Flush()
        $stream.Close()
        
        $stopwatch.Stop()
        $writeMBps = $FileSizeMB / ($stopwatch.ElapsedMilliseconds / 1000)
        
        $results.Tests += @{
            Name = "Sequential-Write"
            Result = "PASS"
            Details = "$([math]::Round($writeMBps, 2)) MB/s"
        }
    } catch {
        $results.Tests += @{
            Name = "Sequential-Write"
            Result = "FAIL"
            Details = $_.Exception.Message
        }
    }
    
    # 顺序读取测试
    $stopwatch.Restart()
    try {
        $stream = [System.IO.File]::OpenRead($testFile)
        $readBuffer = New-Object byte[] ($BlockSizeKB * 1024)
        
        while ($stream.Read($readBuffer, 0, $readBuffer.Length) -gt 0) { }
        $stream.Close()
        
        $stopwatch.Stop()
        $readMBps = $FileSizeMB / ($stopwatch.ElapsedMilliseconds / 1000)
        
        $results.Tests += @{
            Name = "Sequential-Read"
            Result = "PASS"
            Details = "$([math]::Round($readMBps, 2)) MB/s"
        }
    } catch {
        $results.Tests += @{
            Name = "Sequential-Read"
            Result = "FAIL"
            Details = $_.Exception.Message
        }
    }
    
    # 清理测试文件
    Remove-Item $testFile -Force -ErrorAction SilentlyContinue
    
    return $results
}

# 运行所有测试
function Invoke-VNvmeFullTest {
    $allResults = @()
    
    # 获取所有 VNvme 磁盘
    $disks = Get-VNvmeDisk
    
    if ($disks.Count -eq 0) {
        Write-Warning "No VNvme disks found"
        return
    }
    
    foreach ($disk in $disks) {
        Write-Host "Testing Disk $($disk.Number): $($disk.FriendlyName)" -ForegroundColor Cyan
        
        # 基本测试
        $diskResults = Test-VNvmeDisk -DiskNumber $disk.Number
        $allResults += $diskResults
        
        # I/O 测试 (如果有驱动器号)
        $partition = Get-Partition -DiskNumber $disk.Number | 
                     Where-Object { $_.DriveLetter } | 
                     Select-Object -First 1
        
        if ($partition) {
            $ioResults = Test-VNvmeIO -DriveLetter $partition.DriveLetter
            $allResults += $ioResults
        }
    }
    
    # 输出结果
    Write-Host "`n=== Test Results ===" -ForegroundColor Green
    foreach ($result in $allResults) {
        foreach ($test in $result.Tests) {
            $color = if ($test.Result -eq "PASS") { "Green" } else { "Red" }
            Write-Host "[$($test.Result)] $($test.Name): $($test.Details)" -ForegroundColor $color
        }
    }
}
```

---

## 压力测试

### diskspd 压力测试

```powershell
# 安装 diskspd
# 下载: https://github.com/microsoft/diskspd/releases

# 顺序读取测试 (8 线程, 64K 块, 30 秒)
diskspd -c100M -d30 -r -w0 -t8 -o32 -b64K -Sh V:\testfile.dat

# 顺序写入测试
diskspd -c100M -d30 -r -w100 -t8 -o32 -b64K -Sh V:\testfile.dat

# 随机读取测试 (4K 块)
diskspd -c100M -d30 -r -w0 -t8 -o32 -b4K -Sh V:\testfile.dat

# 随机写入测试
diskspd -c100M -d30 -r -w100 -t8 -o32 -b4K -Sh V:\testfile.dat

# 混合负载 (70% 读, 30% 写)
diskspd -c100M -d30 -r -w30 -t8 -o32 -b4K -Sh V:\testfile.dat
```

### fio 压力测试

```ini
# vnvme_stress.fio

[global]
ioengine=windowsaio
direct=1
size=1G
runtime=60
time_based

[seq-read]
rw=read
bs=128k
numjobs=4
iodepth=32

[seq-write]
rw=write
bs=128k
numjobs=4
iodepth=32

[rand-read]
rw=randread
bs=4k
numjobs=8
iodepth=64

[rand-write]
rw=randwrite
bs=4k
numjobs=8
iodepth=64
```

```powershell
# 运行 fio 测试
fio vnvme_stress.fio --filename=V:\testfile
```

---

## HLK/WHQL 测试

### Windows Hardware Lab Kit (HLK)

HLK 用于 Windows 硬件认证，包含存储设备的完整测试套件。

#### 设置 HLK 环境

1. 下载 Windows HLK:
   - https://docs.microsoft.com/en-us/windows-hardware/test/hlk/

2. 安装 HLK Controller (测试服务器)

3. 安装 HLK Client (测试虚拟机)

4. 创建 Machine Pool

#### 存储相关测试

| 测试类别 | 说明 |
|----------|------|
| Disk Stress (LOGO) | 磁盘压力测试 |
| Storage HBA Compliance Test | HBA 合规性测试 |
| SCSI Compliance Test | SCSI 命令合规性 |
| Flush Test | 缓存刷新测试 |
| Thin Provisioning Tests | 精简配置测试 |
| TRIM Support Test | TRIM/UNMAP 测试 |

#### 常见测试命令

```powershell
# 列出可用测试
Get-HLKTest -Pool "VNvme Pool" | Where-Object { $_.Name -like "*Storage*" }

# 运行特定测试
Start-HLKTest -TestID "12345678-1234-1234-1234-123456789ABC"

# 检查测试结果
Get-HLKTestResult -TestID "12345678-1234-1234-1234-123456789ABC"
```

---

## 调试与故障排查

### 启用驱动跟踪

```powershell
# 启用 ETW 跟踪
logman create trace vnvme_trace -p "{12345678-1234-1234-1234-123456789ABC}" -o vnvme.etl -ets

# 运行测试...

# 停止跟踪
logman stop vnvme_trace -ets

# 查看跟踪
tracefmt vnvme.etl -o vnvme.txt
```

### WinDbg 常用命令

```windbg
# 加载驱动符号
.reload /f vnvme.sys

# 设置断点
bp vnvme!VNvmeHwStartIo

# 查看适配器扩展
dt vnvme!VNVME_ADAPTER_EXTENSION poi(vnvme!g_pAdapterExtension)

# 查看 SRB
dt storport!_SCSI_REQUEST_BLOCK @rcx

# 跟踪 SCSI 命令
!stordump

# 查看 StorPort 日志
!storlog
```

### 常见问题排查

| 问题 | 可能原因 | 排查方法 |
|------|----------|----------|
| 驱动加载失败 | 签名问题 | 检查 testsigning，验证签名 |
| 设备带黄色感叹号 | HwFindAdapter 失败 | 检查返回状态，查看事件日志 |
| I/O 超时 | HwStartIo 未完成 | 检查 StorPortNotification 调用 |
| 数据损坏 | 后端 I/O 错误 | 启用后端日志，验证数据一致性 |
| 蓝屏 | 内存访问错误 | 分析 dump 文件 |

---

## 参考资料

- [Windows Hardware Lab Kit](https://docs.microsoft.com/en-us/windows-hardware/test/hlk/)
- [diskspd](https://github.com/microsoft/diskspd)
- [TAEF](https://docs.microsoft.com/en-us/windows-hardware/drivers/taef/)
- [StorPort Driver Debugging](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storage-debugging)
