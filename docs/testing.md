# 测试策略

本文档说明虚拟 NVMe 控制器仿真器的测试方法和策略。

## 测试层次

```
┌─────────────────────────────────────────────────────────────────────┐
│                        测试金字塔                                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│                           ┌─────────┐                               │
│                           │  E2E    │  端到端测试                    │
│                           │  Tests  │  (完整系统)                    │
│                          ─┴─────────┴─                              │
│                        ┌───────────────┐                            │
│                        │  Integration  │  集成测试                   │
│                        │    Tests      │  (驱动交互)                 │
│                       ─┴───────────────┴─                           │
│                     ┌─────────────────────┐                         │
│                     │   Functional Tests  │  功能测试                │
│                     │   (IOCTL, Commands) │  (单驱动)                │
│                    ─┴─────────────────────┴─                        │
│                  ┌───────────────────────────┐                      │
│                  │      Unit Tests           │  单元测试              │
│                  │  (Functions, Modules)     │  (代码级别)            │
│                 ─┴───────────────────────────┴─                     │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## 单元测试

### 测试框架

使用 WDK 测试框架或用户模式模拟：

```c
// tests/unit/test_queue.c

#include "test_framework.h"
#include "queue.h"

// 测试 SQ 初始化
TEST_CASE(TestSqInitialization)
{
    VNVME_SUBMISSION_QUEUE sq;
    NTSTATUS status;
    
    status = VnvmeSqInitialize(&sq, 1, 64);
    
    ASSERT_SUCCESS(status);
    ASSERT_EQUAL(sq.QueueId, 1);
    ASSERT_EQUAL(sq.Size, 64);
    ASSERT_EQUAL(sq.Head, 0);
    ASSERT_EQUAL(sq.Tail, 0);
    
    VnvmeSqCleanup(&sq);
}

// 测试 SQ 环形缓冲区回绕
TEST_CASE(TestSqWrapAround)
{
    VNVME_SUBMISSION_QUEUE sq;
    NVME_COMMAND cmd;
    
    VnvmeSqInitialize(&sq, 1, 4);  // 小队列便于测试
    
    // 模拟添加和消费
    for (int i = 0; i < 10; i++) {
        sq.Tail = (sq.Tail + 1) % sq.Size;
        
        BOOLEAN hasCmd = VnvmeFetchCommand(&sq, &cmd);
        ASSERT_TRUE(hasCmd);
    }
    
    VnvmeSqCleanup(&sq);
}

// 测试 CQ Phase Tag 翻转
TEST_CASE(TestCqPhaseToggle)
{
    VNVME_COMPLETION_QUEUE cq;
    
    VnvmeCqInitialize(&cq, 1, 4);
    
    ASSERT_EQUAL(cq.Phase, TRUE);  // 初始 Phase = 1
    
    // 填满队列，触发回绕
    for (int i = 0; i < 4; i++) {
        VnvmeCqAdvanceTail(&cq);
    }
    
    // 回绕后 Phase 应该翻转
    ASSERT_EQUAL(cq.Phase, FALSE);
    
    VnvmeCqCleanup(&cq);
}
```

### 后端单元测试

```c
// tests/unit/test_backend.c

TEST_CASE(TestMemoryBackendReadWrite)
{
    VNVME_BACKEND backend;
    VNVME_BACKEND_INIT_PARAMS params = {
        .Type = VnvmeBackendMemory,
        .Capacity = 1024 * 1024,  // 1MB
        .SectorSize = 512
    };
    UCHAR writeBuffer[512];
    UCHAR readBuffer[512];
    NTSTATUS status;
    
    // 初始化
    status = VnvmeCreateBackend(&params, &backend);
    ASSERT_SUCCESS(status);
    
    // 填充测试数据
    for (int i = 0; i < 512; i++) {
        writeBuffer[i] = (UCHAR)i;
    }
    
    // 写入
    status = backend.Operations->Write(&backend, 0, 512, writeBuffer);
    ASSERT_SUCCESS(status);
    
    // 读回
    status = backend.Operations->Read(&backend, 0, 512, readBuffer);
    ASSERT_SUCCESS(status);
    
    // 验证
    ASSERT_MEMORY_EQUAL(readBuffer, writeBuffer, 512);
    
    VnvmeDestroyBackend(&backend);
}

TEST_CASE(TestBackendBoundaryCheck)
{
    VNVME_BACKEND backend;
    VNVME_BACKEND_INIT_PARAMS params = {
        .Type = VnvmeBackendMemory,
        .Capacity = 4096,
        .SectorSize = 512
    };
    UCHAR buffer[512];
    NTSTATUS status;
    
    VnvmeCreateBackend(&params, &backend);
    
    // 尝试越界读取
    status = backend.Operations->Read(&backend, 4096, 512, buffer);
    ASSERT_STATUS(status, STATUS_INVALID_PARAMETER);
    
    // 尝试跨边界读取
    status = backend.Operations->Read(&backend, 4000, 512, buffer);
    ASSERT_STATUS(status, STATUS_INVALID_PARAMETER);
    
    VnvmeDestroyBackend(&backend);
}
```

## 功能测试

### IOCTL 测试

```c
// tests/functional/test_ioctl.c

#include <windows.h>
#include "vnvme_ioctl.h"

HANDLE g_hDevice = INVALID_HANDLE_VALUE;

BOOL SetupTest(void)
{
    g_hDevice = CreateFileW(L"\\\\.\\VNVMEControl",
                           GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING, 0, NULL);
    return g_hDevice != INVALID_HANDLE_VALUE;
}

void TeardownTest(void)
{
    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDevice);
    }
}

TEST_CASE(TestCreateController)
{
    VNVME_CREATE_CONTROLLER_IN input = {
        .TotalCapacityBytes = 100 * 1024 * 1024,  // 100MB
        .BlockSize = 512,
        .BackendType = VnvmeBackendMemory,
        .MaxQueueEntries = 256,
        .MaxIoQueues = 4,
        .CreateDefaultNamespace = TRUE
    };
    VNVME_CREATE_CONTROLLER_OUT output;
    DWORD bytesReturned;
    BOOL result;
    
    result = DeviceIoControl(g_hDevice,
                            IOCTL_VNVME_CREATE_CONTROLLER,
                            &input, sizeof(input),
                            &output, sizeof(output),
                            &bytesReturned, NULL);
    
    ASSERT_TRUE(result);
    ASSERT_SUCCESS(output.Result.Status);
    ASSERT_TRUE(output.ControllerIndex > 0);
    
    // 清理：删除创建的控制器
    VNVME_DELETE_CONTROLLER_IN deleteInput = {
        .ControllerIndex = output.ControllerIndex,
        .Force = TRUE
    };
    DeviceIoControl(g_hDevice, IOCTL_VNVME_DELETE_CONTROLLER,
                   &deleteInput, sizeof(deleteInput),
                   NULL, 0, &bytesReturned, NULL);
}

TEST_CASE(TestListControllers)
{
    VNVME_LIST_CONTROLLERS_OUT output;
    DWORD bytesReturned;
    BOOL result;
    
    result = DeviceIoControl(g_hDevice,
                            IOCTL_VNVME_LIST_CONTROLLERS,
                            NULL, 0,
                            &output, sizeof(output),
                            &bytesReturned, NULL);
    
    ASSERT_TRUE(result);
    ASSERT_SUCCESS(output.Result.Status);
    // ControllerCount 可以是 0 或更多
}
```

### NVMe 命令测试

```c
// tests/functional/test_nvme_commands.c

// 模拟 NVMe 命令提交和完成
TEST_CASE(TestIdentifyController)
{
    NVME_COMMAND cmd = { 0 };
    NVME_COMPLETION cpl;
    UCHAR identifyData[4096];
    
    // 构造 Identify Controller 命令
    cmd.CDW0.OPC = NVME_ADMIN_CMD_IDENTIFY;
    cmd.CDW0.CID = 1;
    cmd.NSID = 0;
    cmd.CDW10 = 0x01;  // CNS = 1 (Controller)
    
    // 提交命令
    // ... (需要通过 IOCTL 或直接内存访问)
    
    // 验证返回数据
    PNVME_IDENTIFY_CONTROLLER idCtrl = (PNVME_IDENTIFY_CONTROLLER)identifyData;
    
    ASSERT_EQUAL(idCtrl->VID, 0x1B36);
    ASSERT_TRUE(idCtrl->NN >= 1);
    // ...
}

TEST_CASE(TestReadWriteBasic)
{
    UCHAR writeData[4096];
    UCHAR readData[4096];
    
    // 填充测试数据
    for (int i = 0; i < 4096; i++) {
        writeData[i] = (UCHAR)(i & 0xFF);
    }
    
    // 写入 LBA 0
    NVME_COMMAND writeCmd = {
        .CDW0.OPC = NVME_IO_CMD_WRITE,
        .CDW0.CID = 1,
        .NSID = 1,
        .CDW10 = 0,      // Starting LBA (low)
        .CDW11 = 0,      // Starting LBA (high)
        .CDW12 = 7       // NLB = 8 (0-based), 8 * 512 = 4096 bytes
    };
    
    // 提交写入并等待完成
    // ...
    
    // 读取 LBA 0
    NVME_COMMAND readCmd = {
        .CDW0.OPC = NVME_IO_CMD_READ,
        .CDW0.CID = 2,
        .NSID = 1,
        .CDW10 = 0,
        .CDW11 = 0,
        .CDW12 = 7
    };
    
    // 提交读取并等待完成
    // ...
    
    // 验证数据一致性
    ASSERT_MEMORY_EQUAL(readData, writeData, 4096);
}
```

## 集成测试

### stornvme.sys 集成测试

当我们的虚拟 NVMe 设备创建后，Windows 的 stornvme.sys 会自动加载。我们可以测试：

```c
// tests/integration/test_stornvme_integration.c

TEST_CASE(TestDeviceEnumeration)
{
    // 创建虚拟控制器后，检查设备管理器
    
    // 使用 SetupAPI 枚举 NVMe 设备
    HDEVINFO devInfo = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_DISK,
        NULL, NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    
    // 查找我们的虚拟设备
    // ...
}

TEST_CASE(TestDiskAppearance)
{
    // 创建带命名空间的控制器
    // 等待磁盘出现
    
    Sleep(5000);  // 等待 PnP 枚举
    
    // 检查磁盘是否出现
    WCHAR diskPath[64];
    swprintf_s(diskPath, 64, L"\\\\.\\PhysicalDrive%d", expectedDriveNumber);
    
    HANDLE hDisk = CreateFileW(diskPath,
                              GENERIC_READ,
                              FILE_SHARE_READ | FILE_SHARE_WRITE,
                              NULL, OPEN_EXISTING, 0, NULL);
    
    ASSERT_TRUE(hDisk != INVALID_HANDLE_VALUE);
    
    // 获取磁盘信息
    STORAGE_PROPERTY_QUERY query = {
        .PropertyId = StorageDeviceProperty,
        .QueryType = PropertyStandardQuery
    };
    STORAGE_DEVICE_DESCRIPTOR desc;
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(hDisk,
                                 IOCTL_STORAGE_QUERY_PROPERTY,
                                 &query, sizeof(query),
                                 &desc, sizeof(desc),
                                 &bytesReturned, NULL);
    
    ASSERT_TRUE(result);
    ASSERT_EQUAL(desc.BusType, BusTypeNvme);
    
    CloseHandle(hDisk);
}
```

### 文件系统测试

```c
TEST_CASE(TestFileSystemOperations)
{
    // 假设虚拟磁盘已格式化为 NTFS 并分配盘符 V:
    
    WCHAR testFile[] = L"V:\\test_file.bin";
    HANDLE hFile;
    DWORD bytesWritten, bytesRead;
    UCHAR writeBuffer[1024 * 1024];  // 1MB
    UCHAR readBuffer[1024 * 1024];
    
    // 生成测试数据
    for (int i = 0; i < sizeof(writeBuffer); i++) {
        writeBuffer[i] = (UCHAR)rand();
    }
    
    // 创建并写入文件
    hFile = CreateFileW(testFile, GENERIC_WRITE, 0, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_TRUE(hFile != INVALID_HANDLE_VALUE);
    
    BOOL result = WriteFile(hFile, writeBuffer, sizeof(writeBuffer),
                           &bytesWritten, NULL);
    ASSERT_TRUE(result);
    ASSERT_EQUAL(bytesWritten, sizeof(writeBuffer));
    
    CloseHandle(hFile);
    
    // 读回验证
    hFile = CreateFileW(testFile, GENERIC_READ, 0, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    ASSERT_TRUE(hFile != INVALID_HANDLE_VALUE);
    
    result = ReadFile(hFile, readBuffer, sizeof(readBuffer),
                     &bytesRead, NULL);
    ASSERT_TRUE(result);
    ASSERT_EQUAL(bytesRead, sizeof(readBuffer));
    ASSERT_MEMORY_EQUAL(readBuffer, writeBuffer, sizeof(writeBuffer));
    
    CloseHandle(hFile);
    DeleteFileW(testFile);
}
```

## 性能测试

### I/O 吞吐量测试

```c
// tests/performance/test_throughput.c

TEST_CASE(TestSequentialReadThroughput)
{
    HANDLE hDisk = OpenVirtualDisk();
    LARGE_INTEGER fileSize = { .QuadPart = 1024 * 1024 * 1024 };  // 1GB
    UCHAR* buffer;
    DWORD bytesRead;
    LARGE_INTEGER startTime, endTime, frequency;
    
    buffer = VirtualAlloc(NULL, 1024 * 1024, MEM_COMMIT, PAGE_READWRITE);
    
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTime);
    
    LARGE_INTEGER offset = { 0 };
    ULONG64 totalRead = 0;
    
    while (totalRead < fileSize.QuadPart) {
        SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
        ReadFile(hDisk, buffer, 1024 * 1024, &bytesRead, NULL);
        totalRead += bytesRead;
        offset.QuadPart += bytesRead;
    }
    
    QueryPerformanceCounter(&endTime);
    
    double seconds = (double)(endTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
    double mbps = (totalRead / (1024.0 * 1024.0)) / seconds;
    
    printf("Sequential Read: %.2f MB/s\n", mbps);
    
    VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(hDisk);
}

TEST_CASE(TestRandomIops)
{
    HANDLE hDisk = OpenVirtualDisk();
    UCHAR buffer[4096];
    DWORD bytesRead;
    LARGE_INTEGER frequency, startTime, endTime;
    ULONG operations = 10000;
    ULONG64 diskSize = 100 * 1024 * 1024;  // 100MB
    
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&startTime);
    
    for (ULONG i = 0; i < operations; i++) {
        LARGE_INTEGER offset;
        offset.QuadPart = (rand() % (diskSize / 4096)) * 4096;
        
        SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
        ReadFile(hDisk, buffer, 4096, &bytesRead, NULL);
    }
    
    QueryPerformanceCounter(&endTime);
    
    double seconds = (double)(endTime.QuadPart - startTime.QuadPart) / frequency.QuadPart;
    double iops = operations / seconds;
    
    printf("Random 4K Read: %.0f IOPS\n", iops);
    
    CloseHandle(hDisk);
}
```

### 延迟测试

```c
TEST_CASE(TestIoLatency)
{
    HANDLE hDisk = OpenVirtualDisk();
    UCHAR buffer[4096];
    LARGE_INTEGER frequency, startTime, endTime;
    ULONG samples = 1000;
    double* latencies = malloc(samples * sizeof(double));
    
    QueryPerformanceFrequency(&frequency);
    
    for (ULONG i = 0; i < samples; i++) {
        QueryPerformanceCounter(&startTime);
        
        DWORD bytesRead;
        ReadFile(hDisk, buffer, 4096, &bytesRead, NULL);
        
        QueryPerformanceCounter(&endTime);
        
        latencies[i] = (double)(endTime.QuadPart - startTime.QuadPart) * 1000000.0 / frequency.QuadPart;  // us
    }
    
    // 计算统计数据
    double sum = 0, min = latencies[0], max = latencies[0];
    for (ULONG i = 0; i < samples; i++) {
        sum += latencies[i];
        if (latencies[i] < min) min = latencies[i];
        if (latencies[i] > max) max = latencies[i];
    }
    double avg = sum / samples;
    
    // 排序计算 p99
    qsort(latencies, samples, sizeof(double), CompareDouble);
    double p99 = latencies[(int)(samples * 0.99)];
    
    printf("Latency: avg=%.2fus, min=%.2fus, max=%.2fus, p99=%.2fus\n",
           avg, min, max, p99);
    
    free(latencies);
    CloseHandle(hDisk);
}
```

## 压力测试

### 长时间运行测试

```powershell
# scripts/stress_test.ps1

param(
    [int]$DurationMinutes = 60,
    [int]$ThreadCount = 4
)

$diskPath = "\\.\PhysicalDrive2"  # 虚拟磁盘

# 使用 diskspd 进行压力测试
diskspd -c1G -d$($DurationMinutes * 60) -t$ThreadCount -o32 -b4K -r -w50 -L $diskPath
```

### 并发访问测试

```c
DWORD WINAPI StressWorkerThread(LPVOID param)
{
    DWORD threadId = GetCurrentThreadId();
    HANDLE hDisk = OpenVirtualDisk();
    UCHAR buffer[4096];
    ULONG operations = 0;
    
    while (!g_StopTest) {
        LARGE_INTEGER offset;
        offset.QuadPart = (rand() % (g_DiskSize / 4096)) * 4096;
        
        SetFilePointerEx(hDisk, offset, NULL, FILE_BEGIN);
        
        if (rand() % 2) {
            WriteFile(hDisk, buffer, 4096, NULL, NULL);
        } else {
            ReadFile(hDisk, buffer, 4096, NULL, NULL);
        }
        
        operations++;
    }
    
    InterlockedAdd64(&g_TotalOperations, operations);
    CloseHandle(hDisk);
    return 0;
}
```

## 测试自动化

### 测试运行脚本

`scripts/run_tests.ps1`:

```powershell
param(
    [ValidateSet("unit", "functional", "integration", "performance", "all")]
    [string]$TestType = "all"
)

$testExe = "tests\x64\Release\vnvme_tests.exe"

function Run-Tests($filter) {
    Write-Host "Running tests: $filter" -ForegroundColor Cyan
    & $testExe --filter=$filter --output=results\$filter.xml
    
    if ($LASTEXITCODE -ne 0) {
        Write-Host "FAILED: $filter" -ForegroundColor Red
        return $false
    }
    Write-Host "PASSED: $filter" -ForegroundColor Green
    return $true
}

$results = @()

if ($TestType -eq "unit" -or $TestType -eq "all") {
    $results += Run-Tests "Unit.*"
}

if ($TestType -eq "functional" -or $TestType -eq "all") {
    $results += Run-Tests "Functional.*"
}

if ($TestType -eq "integration" -or $TestType -eq "all") {
    # 需要驱动已安装
    $results += Run-Tests "Integration.*"
}

if ($TestType -eq "performance" -or $TestType -eq "all") {
    $results += Run-Tests "Performance.*"
}

$failed = ($results | Where-Object { $_ -eq $false }).Count
if ($failed -gt 0) {
    Write-Host "`n$failed test suite(s) failed!" -ForegroundColor Red
    exit 1
} else {
    Write-Host "`nAll tests passed!" -ForegroundColor Green
    exit 0
}
```

## 代码覆盖率

使用 Visual Studio 或其他工具收集代码覆盖率：

```powershell
# 使用 OpenCppCoverage
OpenCppCoverage.exe `
    --sources src\ `
    --export_type=html:coverage_report `
    -- tests\x64\Release\vnvme_tests.exe
```
