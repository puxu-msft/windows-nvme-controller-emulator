# 测试策略

## 测试层次

```
┌─────────────────────────────────┐
│      WHQL 认证测试              │  ← Windows 认证
├─────────────────────────────────┤
│      系统集成测试               │  ← 完整系统验证
├─────────────────────────────────┤
│      功能测试                   │  ← 功能验证
├─────────────────────────────────┤
│      单元测试                   │  ← 模块验证
└─────────────────────────────────┘
```

## 测试环境配置

### 推荐使用虚拟机测试

⚠️ **重要**: 内核驱动开发容易导致蓝屏，强烈建议在虚拟机中进行测试。

#### Hyper-V 虚拟机配置
```powershell
# 创建测试虚拟机
New-VM -Name "VNvmeTest" -MemoryStartupBytes 4GB -Generation 2

# 创建虚拟硬盘
New-VHD -Path "C:\VMs\VNvmeTest.vhdx" -SizeBytes 60GB -Dynamic

# 添加虚拟硬盘
Add-VMHardDiskDrive -VMName "VNvmeTest" -Path "C:\VMs\VNvmeTest.vhdx"

# 启用嵌套虚拟化 (可选)
Set-VMProcessor -VMName "VNvmeTest" -ExposeVirtualizationExtensions $true

# 配置调试
Set-VMComPort -VMName "VNvmeTest" -Number 1 -Path "\\.\pipe\vnvme_debug"
```

#### VMware 配置
```
1. 创建 Windows 10/11 虚拟机
2. 设置 4GB+ 内存, 60GB+ 磁盘
3. 添加串口设备用于调试 (Named Pipe)
4. 启用 "Virtualize Intel VT-x/EPT"
```

### 代码覆盖率

使用 Visual Studio 代码覆盖率工具：

```powershell
# 在构建时启用覆盖率
msbuild VirtualNvme.sln /p:Configuration=Debug /p:EnableCodeCoverage=true

# 运行测试并收集覆盖率
vstest.console.exe VNvmeTests.dll /EnableCodeCoverage

# 覆盖率目标
# - 核心模块 (Controller, Queue, Backend): ≥ 80%
# - 命令处理: ≥ 90%
# - 错误路径: ≥ 70%
```

## 单元测试

### 测试用例

| 模块 | 测试项 |
|------|--------|
| 控制器 | 初始化、状态转换、寄存器操作 |
| 队列 | 创建、删除、入队、出队 |
| 命名空间 | 创建、销毁、参数验证 |
| 后端 | 读写、刷新、边界条件 |

### 示例测试
```c
// 测试队列创建
VOID TestQueueCreate() {
    VNVME_QUEUE queue;
    NTSTATUS status = QueueCreate(&queue, 1, 64, TRUE);
    ASSERT(NT_SUCCESS(status));
    ASSERT(queue.QueueId == 1);
    ASSERT(queue.QueueSize == 64);
    QueueDestroy(&queue);
}
```

## 功能测试

### Admin 命令测试
- [ ] Identify Controller
- [ ] Identify Namespace
- [ ] Create/Delete I/O Queue
- [ ] Get/Set Features

### I/O 命令测试
- [ ] 顺序读取
- [ ] 顺序写入
- [ ] 随机读取
- [ ] 随机写入
- [ ] 混合读写
- [ ] Flush 操作

### 边界条件测试
- [ ] 最大 LBA 访问
- [ ] 零长度操作
- [ ] 无效参数处理
- [ ] 队列满/空处理

## 系统集成测试

### Windows 认证测试
使用 Windows HLK (Hardware Lab Kit):
- Device Fundamentals Tests
- Storage Tests
- Power Management Tests

### WHQL 认证流程

#### 1. 设置 HLK 环境
```
a. 下载 Windows HLK: https://learn.microsoft.com/windows-hardware/test/hlk/
b. 安装 HLK Controller 到测试服务器
c. 安装 HLK Studio 到开发机
d. 安装 HLK Client 到测试虚拟机
```

#### 2. 配置测试目标
```
a. 在 HLK Studio 中创建项目
b. 添加测试机器到计算机池
c. 选择设备类型: Storage > Disk
d. 选择驱动目标
```

#### 3. 运行必需测试
```
必须通过的测试类别:
□ Device Fundamentals Reliability
  - DF - PNP (Disable and Enable)
  - DF - Reboot Restart
  - DF - Sleep and PnP
  
□ Storage Tests
  - Disk Stress
  - Disk Verification
  - SCSI Compliance Test
  
□ Crash Dump Support (如支持)
```

#### 4. 提交认证
```
a. 所有必需测试通过后创建 .hlkx 包
b. 登录 Windows 硬件开发人员中心
c. 提交 .hlkx 包进行签名
d. 下载认证后的驱动包
```

### 文件系统测试
```powershell
# 格式化
Format-Volume -DriveLetter V -FileSystem NTFS

# 文件操作
Copy-Item test.dat V:\
Compare-Object (Get-FileHash test.dat) (Get-FileHash V:\test.dat)
```

### 性能测试
使用工具:
- CrystalDiskMark
- fio
- diskspd

```powershell
# diskspd 示例 - 4K 随机读写
diskspd -b4K -d60 -o32 -t4 -r -w50 V:\testfile.dat

# diskspd 示例 - 顺序读取
diskspd -b1M -d60 -o8 -t1 -s -w0 V:\testfile.dat

# 结果指标
# - IOPS (随机 4K): 目标 > 50,000
# - 吞吐量 (顺序): 目标 > 500 MB/s
# - 延迟: 目标 < 1ms
```

## 压力测试

### 长时间运行
- 连续 I/O 操作 24 小时
- 随机断电恢复测试
- 内存泄漏检测

### 工具
```powershell
# Driver Verifier 启用
verifier /standard /driver vnvme.sys

# 检查结果
verifier /query
```

## 模糊测试 (Fuzzing)

模糊测试通过生成随机或变异的输入数据，发现驱动中的边界条件错误和安全漏洞。

### IOCTL 模糊测试

使用 [IOCTL Fuzzer](https://github.com/koutto/ioctlbf) 或自定义工具：

```c
// 自定义 IOCTL 模糊器示例
#include <windows.h>
#include <stdio.h>

void FuzzIoctl(HANDLE hDevice) {
    BYTE inputBuffer[4096];
    BYTE outputBuffer[4096];
    DWORD bytesReturned;
    
    // 模糊各种 IOCTL 代码
    for (ULONG func = 0; func < 0x1000; func++) {
        for (int method = 0; method < 4; method++) {
            ULONG ioctl = CTL_CODE(FILE_DEVICE_UNKNOWN, 
                                   0x800 + func, 
                                   method, 
                                   FILE_ANY_ACCESS);
            
            // 填充随机数据
            for (int i = 0; i < sizeof(inputBuffer); i++) {
                inputBuffer[i] = rand() & 0xFF;
            }
            
            // 尝试各种输入大小
            for (DWORD size = 0; size <= 4096; size += 64) {
                __try {
                    DeviceIoControl(
                        hDevice,
                        ioctl,
                        inputBuffer, size,
                        outputBuffer, sizeof(outputBuffer),
                        &bytesReturned,
                        NULL
                    );
                }
                __except(EXCEPTION_EXECUTE_HANDLER) {
                    printf("Crash at IOCTL 0x%08X, size %lu\n", ioctl, size);
                }
            }
        }
    }
}
```

### NVMe 命令模糊测试

测试 NVMe passthrough 命令的各种边界情况：

```c
typedef struct _FUZZ_CASE {
    const char* Name;
    VNVME_PASSTHROUGH_CMD Cmd;
} FUZZ_CASE;

FUZZ_CASE FuzzCases[] = {
    // 无效操作码
    {"Invalid Opcode", {.Opcode = 0xFF, .NamespaceId = 1}},
    
    // 无效命名空间
    {"Invalid NSID", {.Opcode = 0x02, .NamespaceId = 0xFFFFFFFF}},
    
    // 越界 LBA
    {"Out of Range LBA", {.Opcode = 0x02, .CDW10 = 0xFFFFFFFF, .CDW11 = 0xFFFFFFFF}},
    
    // 零长度传输
    {"Zero Length", {.Opcode = 0x02, .DataTransferLength = 0}},
    
    // 超大传输
    {"Huge Transfer", {.Opcode = 0x02, .DataTransferLength = 0xFFFFFFFF}},
    
    // 负超时
    {"Zero Timeout", {.Opcode = 0x06, .TimeoutMs = 0}},
};

void RunFuzzTests(HANDLE hDevice) {
    for (int i = 0; i < ARRAYSIZE(FuzzCases); i++) {
        DWORD bytesReturned;
        BYTE buffer[4096] = {0};
        
        printf("Testing: %s\n", FuzzCases[i].Name);
        
        BOOL result = DeviceIoControl(
            hDevice,
            IOCTL_VNVME_PASSTHROUGH,
            &FuzzCases[i].Cmd, sizeof(VNVME_PASSTHROUGH_CMD),
            buffer, sizeof(buffer),
            &bytesReturned,
            NULL
        );
        
        // 预期应该失败，但不应该崩溃
        if (!result) {
            printf("  Expected failure: error %lu\n", GetLastError());
        } else {
            printf("  WARNING: Unexpected success!\n");
        }
    }
}
```

### 使用 WinAFL 进行持续模糊

[WinAFL](https://github.com/googleprojectzero/winafl) 是 AFL 的 Windows 移植版本：

```powershell
# 编译测试入口点
cl /Zi /GS- harness.c /link /DEBUG

# 运行 WinAFL
afl-fuzz.exe -i input_corpus -o output -D C:\DynamoRIO\bin64 `
    -t 5000 -- -coverage_module vnvme.sys `
    -- harness.exe @@
```

### 内核模糊测试注意事项

1. **在虚拟机中运行**: 模糊测试会导致蓝屏，务必在 VM 中进行
2. **配置自动重启**: 
   ```powershell
   # 启用自动重启
   bcdedit /set {default} bootstatuspolicy ignoreallfailures
   ```
3. **收集崩溃转储**:
   ```powershell
   # 配置完整内存转储
   Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" `
       -Name "CrashDumpEnabled" -Value 1
   Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl" `
       -Name "DumpFile" -Value "C:\MEMORY.DMP"
   ```

## 虚拟机测试详细配置

### Hyper-V 调试配置

#### 配置调试端口
```powershell
# 创建内部虚拟交换机用于调试
New-VMSwitch -Name "DebugSwitch" -SwitchType Internal

# 获取 VM 的网络适配器 MAC
$vm = Get-VM -Name "VNvmeTest"
$adapter = Get-VMNetworkAdapter -VM $vm
$macAddress = $adapter.MacAddress

# 配置网络调试
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000 key:1.2.3.4
```

#### 自动快照用于回滚
```powershell
# 创建测试前快照
Checkpoint-VM -Name "VNvmeTest" -SnapshotName "PreTest"

# 运行测试...

# 如果失败，回滚
Restore-VMSnapshot -Name "PreTest" -VMName "VNvmeTest" -Confirm:$false
```

### VMware 串口调试配置

1. 虚拟机设置 → 添加 → 串行端口
2. 选择 "使用命名管道"
3. 名称: `\\.\pipe\vnvme_debug`
4. 端: "这是服务器"
5. 另一端: "一个应用程序"

**调试目标配置**:
```cmd
bcdedit /debug on
bcdedit /dbgsettings serial debugport:1 baudrate:115200
```

**WinDbg 连接**:
```
File → Kernel Debug → COM
Port: \\.\pipe\vnvme_debug
Pipe: checked
Reconnect: checked
Baud Rate: 115200
```

### 自动化测试脚本

```powershell
# test-driver.ps1 - 自动化 VM 驱动测试

param(
    [string]$VMName = "VNvmeTest",
    [string]$DriverPath,
    [switch]$SkipSnapshot
)

$ErrorActionPreference = "Stop"

try {
    # 1. 创建快照
    if (-not $SkipSnapshot) {
        Write-Host "Creating snapshot..." -ForegroundColor Yellow
        Checkpoint-VM -Name $VMName -SnapshotName "BeforeTest_$(Get-Date -Format 'yyyyMMdd_HHmmss')"
    }
    
    # 2. 复制驱动到 VM
    Write-Host "Copying driver to VM..." -ForegroundColor Yellow
    $session = New-PSSession -VMName $VMName -Credential (Get-Credential)
    Copy-Item -Path $DriverPath\* -Destination "C:\Drivers\" -ToSession $session
    
    # 3. 安装驱动
    Write-Host "Installing driver..." -ForegroundColor Yellow
    Invoke-Command -Session $session -ScriptBlock {
        pnputil /add-driver C:\Drivers\vnvme.inf /install
    }
    
    # 4. 运行基本测试
    Write-Host "Running tests..." -ForegroundColor Yellow
    $testResult = Invoke-Command -Session $session -ScriptBlock {
        # 检查驱动是否加载
        $driver = Get-WmiObject Win32_SystemDriver | Where-Object {$_.Name -eq "vnvme"}
        if (-not $driver) {
            throw "Driver not loaded"
        }
        
        # 检查设备是否存在
        $device = Get-PnpDevice | Where-Object {$_.FriendlyName -like "*Virtual NVMe*"}
        if (-not $device) {
            throw "Device not found"
        }
        
        # 运行 I/O 测试
        $disk = Get-Disk | Where-Object {$_.FriendlyName -like "*Virtual NVMe*"}
        if ($disk) {
            # 初始化磁盘
            Initialize-Disk -Number $disk.Number -PartitionStyle GPT -PassThru |
                New-Partition -UseMaximumSize -AssignDriveLetter |
                Format-Volume -FileSystem NTFS -Confirm:$false
            
            # 写入测试文件
            $testFile = "V:\test_$(Get-Random).dat"
            [byte[]]$data = 1..1024 | ForEach-Object { Get-Random -Maximum 256 }
            [System.IO.File]::WriteAllBytes($testFile, $data)
            
            # 读回验证
            $readData = [System.IO.File]::ReadAllBytes($testFile)
            if (Compare-Object $data $readData) {
                throw "Data verification failed"
            }
            
            Remove-Item $testFile
        }
        
        return @{Success = $true; Message = "All tests passed"}
    }
    
    Write-Host "Test Result: $($testResult.Message)" -ForegroundColor Green
    
    # 5. 清理
    Remove-PSSession $session
}
catch {
    Write-Host "Error: $_" -ForegroundColor Red
    
    # 回滚快照
    if (-not $SkipSnapshot) {
        Write-Host "Rolling back to snapshot..." -ForegroundColor Yellow
        Get-VMSnapshot -VMName $VMName | Sort-Object CreationTime -Descending | 
            Select-Object -First 1 | Restore-VMSnapshot -Confirm:$false
    }
    
    exit 1
}
```

## 测试报告模板

```
测试名称: 
测试日期: 
测试环境: 
测试结果: PASS / FAIL
详细描述:
错误日志:
```

## 测试覆盖率检查清单

### 功能测试
- [ ] 驱动加载/卸载
- [ ] 设备枚举
- [ ] Identify Controller
- [ ] Identify Namespace
- [ ] Create/Delete I/O Queue
- [ ] Read (各种大小)
- [ ] Write (各种大小)
- [ ] Flush
- [ ] IOCTL 接口

### 错误处理测试
- [ ] 无效参数拒绝
- [ ] 资源不足处理
- [ ] 超时处理
- [ ] 请求取消

### 边界条件测试
- [ ] 最大 LBA
- [ ] 最大传输大小
- [ ] 队列满
- [ ] 并发请求极限

### 电源管理测试
- [ ] S3 睡眠/唤醒
- [ ] S4 休眠/恢复
- [ ] 设备 D3/D0 转换

### 安全测试
- [ ] IOCTL 模糊测试
- [ ] NVMe 命令模糊测试
- [ ] 缓冲区边界测试
- [ ] 权限检查

### 性能测试
- [ ] 顺序读吞吐量
- [ ] 顺序写吞吐量
- [ ] 4K 随机读 IOPS
- [ ] 4K 随机写 IOPS
- [ ] 延迟测量
