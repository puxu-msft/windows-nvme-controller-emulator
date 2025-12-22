# IOCTL 接口

本文档定义 Virtual NVMe 驱动的用户态接口，用于应用程序与驱动程序的通信。

## 概述

IOCTL (I/O Control) 是 Windows 驱动程序提供自定义控制接口的标准方式。Virtual NVMe 驱动通过 IOCTL 提供：

- 设备配置和管理
- 状态查询
- NVMe passthrough 命令
- 诊断和调试功能

## 设备接口 GUID

驱动注册以下设备接口，用户态程序通过此 GUID 发现设备：

```c
// {E5D5A8E0-1234-5678-9ABC-DEF012345678}
DEFINE_GUID(GUID_DEVINTERFACE_VNVME,
    0xe5d5a8e0, 0x1234, 0x5678, 
    0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78);

// 总线驱动接口
// {E5D5A8E1-1234-5678-9ABC-DEF012345678}
DEFINE_GUID(GUID_DEVINTERFACE_VNVME_BUS,
    0xe5d5a8e1, 0x1234, 0x5678,
    0x9a, 0xbc, 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78);
```

## IOCTL 代码定义

### IOCTL 代码格式

```c
#define VNVME_DEVICE_TYPE   FILE_DEVICE_UNKNOWN
#define VNVME_FUNCTION_BASE 0x800

// IOCTL 代码宏
#define VNVME_CTL_CODE(function, method, access) \
    CTL_CODE(VNVME_DEVICE_TYPE, \
             VNVME_FUNCTION_BASE + (function), \
             method, \
             access)
```

### 功能驱动 IOCTL

| IOCTL 代码 | 功能号 | 方法 | 访问 | 说明 |
|------------|--------|------|------|------|
| IOCTL_VNVME_GET_DEVICE_INFO | 0x00 | BUFFERED | READ | 获取设备信息 |
| IOCTL_VNVME_GET_CONTROLLER_INFO | 0x01 | BUFFERED | READ | 获取控制器信息 |
| IOCTL_VNVME_GET_NAMESPACE_INFO | 0x02 | BUFFERED | READ | 获取命名空间信息 |
| IOCTL_VNVME_GET_SMART_LOG | 0x03 | BUFFERED | READ | 获取 SMART 日志 |
| IOCTL_VNVME_PASSTHROUGH | 0x10 | DIRECT | READ/WRITE | NVMe 命令透传 |
| IOCTL_VNVME_ADMIN_PASSTHROUGH | 0x11 | DIRECT | READ/WRITE | Admin 命令透传 |
| IOCTL_VNVME_FLUSH | 0x20 | NEITHER | WRITE | 刷新缓存 |
| IOCTL_VNVME_GET_STATISTICS | 0x30 | BUFFERED | READ | 获取统计信息 |
| IOCTL_VNVME_RESET_STATISTICS | 0x31 | NEITHER | WRITE | 重置统计 |

### 总线驱动 IOCTL

| IOCTL 代码 | 功能号 | 方法 | 访问 | 说明 |
|------------|--------|------|------|------|
| IOCTL_VNVME_BUS_CREATE_DISK | 0x00 | BUFFERED | WRITE | 创建虚拟磁盘 |
| IOCTL_VNVME_BUS_DELETE_DISK | 0x01 | BUFFERED | WRITE | 删除虚拟磁盘 |
| IOCTL_VNVME_BUS_ENUM_DISKS | 0x02 | BUFFERED | READ | 枚举虚拟磁盘 |
| IOCTL_VNVME_BUS_GET_VERSION | 0x10 | BUFFERED | READ | 获取驱动版本 |

## IOCTL 定义头文件

```c
// vnvme_ioctl.h - 用户态和内核态共享头文件

#pragma once

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <windows.h>
#include <winioctl.h>
#endif

//
// 设备类型
//
#define VNVME_DEVICE_TYPE       FILE_DEVICE_UNKNOWN
#define VNVME_FUNCTION_BASE     0x800

//
// 功能驱动 IOCTL
//
#define IOCTL_VNVME_GET_DEVICE_INFO \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x00, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_GET_CONTROLLER_INFO \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x01, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_GET_NAMESPACE_INFO \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x02, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_GET_SMART_LOG \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x03, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_PASSTHROUGH \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x10, \
             METHOD_OUT_DIRECT, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_VNVME_ADMIN_PASSTHROUGH \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x11, \
             METHOD_OUT_DIRECT, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_VNVME_FLUSH \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x20, \
             METHOD_NEITHER, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_GET_STATISTICS \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x30, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_RESET_STATISTICS \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x31, \
             METHOD_NEITHER, FILE_WRITE_ACCESS)

//
// 总线驱动 IOCTL
//
#define IOCTL_VNVME_BUS_CREATE_DISK \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x100, \
             METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_BUS_DELETE_DISK \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x101, \
             METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_BUS_ENUM_DISKS \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x102, \
             METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_BUS_GET_VERSION \
    CTL_CODE(VNVME_DEVICE_TYPE, VNVME_FUNCTION_BASE + 0x110, \
             METHOD_BUFFERED, FILE_READ_ACCESS)
```

## 数据结构定义

### 设备信息

```c
#pragma pack(push, 1)

typedef struct _VNVME_DEVICE_INFO {
    ULONG       StructureSize;      // 结构大小 (版本控制)
    ULONG       DeviceId;           // 设备 ID
    ULONG       NamespaceId;        // 命名空间 ID
    ULONGLONG   TotalSizeBytes;     // 总容量 (字节)
    ULONG       BlockSize;          // 块大小
    ULONG       MaxTransferSize;    // 最大传输大小
    BOOLEAN     ReadOnly;           // 只读标志
    UCHAR       BackendType;        // 后端类型 (0=内存, 1=文件)
    WCHAR       SerialNumber[21];   // 序列号
    WCHAR       ModelNumber[41];    // 型号
    
} VNVME_DEVICE_INFO, *PVNVME_DEVICE_INFO;

#pragma pack(pop)
```

### 控制器信息

```c
typedef struct _VNVME_CONTROLLER_INFO {
    ULONG       StructureSize;
    USHORT      VendorId;
    USHORT      SubsystemVendorId;
    CHAR        SerialNumber[20];
    CHAR        ModelNumber[40];
    CHAR        FirmwareRevision[8];
    UCHAR       NvmeVersion[4];     // [Major, Minor, Tertiary, 0]
    ULONG       MaxQueueEntries;    // 最大队列条目
    ULONG       NumQueues;          // 当前队列数
    ULONGLONG   TotalNvmCapacity;   // 总 NVM 容量
    ULONGLONG   UnallocatedNvmCapacity; // 未分配容量
    
} VNVME_CONTROLLER_INFO, *PVNVME_CONTROLLER_INFO;
```

### 命名空间信息

```c
typedef struct _VNVME_NAMESPACE_INFO {
    ULONG       StructureSize;
    ULONG       NamespaceId;
    ULONGLONG   Size;               // NSZE
    ULONGLONG   Capacity;           // NCAP
    ULONGLONG   Utilization;        // NUSE
    ULONG       FormattedLbaSize;   // 当前 LBA 格式
    UCHAR       NumLbaFormats;      // 支持的 LBA 格式数
    UCHAR       MetadataSize;       // 元数据大小
    UCHAR       ProtectionType;     // 数据保护类型
    UCHAR       Features;           // NSFEAT
    UCHAR       Guid[16];           // NGUID
    UCHAR       Eui64[8];           // EUI64
    
} VNVME_NAMESPACE_INFO, *PVNVME_NAMESPACE_INFO;
```

### SMART 日志

```c
typedef struct _VNVME_SMART_LOG {
    ULONG       StructureSize;
    UCHAR       CriticalWarning;    // 临界警告
    USHORT      CompositeTemperature; // 温度 (Kelvin)
    UCHAR       AvailableSpare;     // 可用备用 (%)
    UCHAR       AvailableSpareThreshold; // 备用阈值
    UCHAR       PercentageUsed;     // 已用寿命 (%)
    ULONGLONG   DataUnitsRead;      // 读取单元数
    ULONGLONG   DataUnitsWritten;   // 写入单元数
    ULONGLONG   HostReadCommands;   // 读命令数
    ULONGLONG   HostWriteCommands;  // 写命令数
    ULONGLONG   ControllerBusyTime; // 忙时间 (分钟)
    ULONGLONG   PowerCycles;        // 电源周期
    ULONGLONG   PowerOnHours;       // 上电小时
    ULONGLONG   UnsafeShutdowns;    // 非安全关机
    ULONGLONG   MediaErrors;        // 介质错误
    ULONGLONG   ErrorLogEntries;    // 错误日志条目
    
} VNVME_SMART_LOG, *PVNVME_SMART_LOG;
```

### NVMe Passthrough

```c
typedef struct _VNVME_PASSTHROUGH_CMD {
    ULONG       StructureSize;
    ULONG       Opcode;             // NVMe 操作码
    ULONG       NamespaceId;        // 命名空间 ID (0 = 控制器)
    ULONG       CDW2;
    ULONG       CDW3;
    ULONG       CDW10;
    ULONG       CDW11;
    ULONG       CDW12;
    ULONG       CDW13;
    ULONG       CDW14;
    ULONG       CDW15;
    ULONG       DataTransferLength; // 数据传输长度
    ULONG       MetadataTransferLength; // 元数据长度
    ULONG       TimeoutMs;          // 超时 (毫秒)
    
} VNVME_PASSTHROUGH_CMD, *PVNVME_PASSTHROUGH_CMD;

typedef struct _VNVME_PASSTHROUGH_RESULT {
    ULONG       StructureSize;
    ULONG       DW0;                // 完成结果 DW0
    ULONG       DW1;                // 完成结果 DW1
    USHORT      StatusCode;         // 状态码
    UCHAR       StatusCodeType;     // 状态码类型
    BOOLEAN     DoNotRetry;         // DNR 标志
    
} VNVME_PASSTHROUGH_RESULT, *PVNVME_PASSTHROUGH_RESULT;
```

### 统计信息

```c
typedef struct _VNVME_STATISTICS {
    ULONG       StructureSize;
    ULONGLONG   ReadOperations;     // 读操作数
    ULONGLONG   WriteOperations;    // 写操作数
    ULONGLONG   BytesRead;          // 读字节数
    ULONGLONG   BytesWritten;       // 写字节数
    ULONGLONG   ReadLatencyTotal;   // 累计读延迟 (微秒)
    ULONGLONG   WriteLatencyTotal;  // 累计写延迟 (微秒)
    ULONGLONG   ErrorCount;         // 错误计数
    ULONGLONG   Uptime;             // 运行时间 (秒)
    
} VNVME_STATISTICS, *PVNVME_STATISTICS;
```

### 总线驱动结构

```c
typedef struct _VNVME_CREATE_DISK_PARAMS {
    ULONG       StructureSize;
    ULONG       DiskId;             // 请求的磁盘 ID (0 = 自动分配)
    ULONGLONG   SizeBytes;          // 磁盘大小
    ULONG       BlockSize;          // 块大小 (512 或 4096)
    UCHAR       BackendType;        // 0=内存, 1=文件
    WCHAR       BackendPath[260];   // 文件后端路径 (仅文件类型)
    WCHAR       SerialNumber[21];   // 自定义序列号 (可选)
    BOOLEAN     ReadOnly;           // 只读
    BOOLEAN     Persistent;         // 持久化配置
    
} VNVME_CREATE_DISK_PARAMS, *PVNVME_CREATE_DISK_PARAMS;

typedef struct _VNVME_CREATE_DISK_RESULT {
    ULONG       StructureSize;
    ULONG       AssignedDiskId;     // 分配的磁盘 ID
    NTSTATUS    Status;             // 操作状态
    WCHAR       DevicePath[260];    // 设备路径
    
} VNVME_CREATE_DISK_RESULT, *PVNVME_CREATE_DISK_RESULT;

typedef struct _VNVME_DISK_ENTRY {
    ULONG       DiskId;
    ULONGLONG   SizeBytes;
    UCHAR       BackendType;
    BOOLEAN     Active;
    WCHAR       DevicePath[260];
    
} VNVME_DISK_ENTRY, *PVNVME_DISK_ENTRY;

typedef struct _VNVME_ENUM_DISKS_RESULT {
    ULONG               StructureSize;
    ULONG               DiskCount;
    VNVME_DISK_ENTRY    Disks[1];   // 可变长度数组
    
} VNVME_ENUM_DISKS_RESULT, *PVNVME_ENUM_DISKS_RESULT;
```

## 用户态使用示例

### 发现设备

```c
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include "vnvme_ioctl.h"

HANDLE OpenVnvmeDevice(DWORD index)
{
    HDEVINFO devInfo;
    SP_DEVICE_INTERFACE_DATA devInterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA devInterfaceDetailData = NULL;
    DWORD requiredSize;
    HANDLE hDevice = INVALID_HANDLE_VALUE;
    
    devInfo = SetupDiGetClassDevs(
        &GUID_DEVINTERFACE_VNVME,
        NULL,
        NULL,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE
    );
    
    if (devInfo == INVALID_HANDLE_VALUE) {
        return INVALID_HANDLE_VALUE;
    }
    
    devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
    
    if (!SetupDiEnumDeviceInterfaces(
            devInfo,
            NULL,
            &GUID_DEVINTERFACE_VNVME,
            index,
            &devInterfaceData)) {
        goto Cleanup;
    }
    
    // 获取所需缓冲区大小
    SetupDiGetDeviceInterfaceDetail(
        devInfo,
        &devInterfaceData,
        NULL,
        0,
        &requiredSize,
        NULL
    );
    
    devInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA)
        malloc(requiredSize);
    devInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);
    
    if (!SetupDiGetDeviceInterfaceDetail(
            devInfo,
            &devInterfaceData,
            devInterfaceDetailData,
            requiredSize,
            NULL,
            NULL)) {
        goto Cleanup;
    }
    
    hDevice = CreateFile(
        devInterfaceDetailData->DevicePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );
    
Cleanup:
    if (devInterfaceDetailData) free(devInterfaceDetailData);
    SetupDiDestroyDeviceInfoList(devInfo);
    
    return hDevice;
}
```

### 获取设备信息

```c
BOOL GetDeviceInfo(HANDLE hDevice, PVNVME_DEVICE_INFO pInfo)
{
    DWORD bytesReturned;
    
    pInfo->StructureSize = sizeof(VNVME_DEVICE_INFO);
    
    return DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_DEVICE_INFO,
        NULL,                   // 无输入
        0,
        pInfo,                  // 输出缓冲区
        sizeof(VNVME_DEVICE_INFO),
        &bytesReturned,
        NULL
    );
}
```

### 获取 SMART 日志

```c
BOOL GetSmartLog(HANDLE hDevice, PVNVME_SMART_LOG pLog)
{
    DWORD bytesReturned;
    
    pLog->StructureSize = sizeof(VNVME_SMART_LOG);
    
    return DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_SMART_LOG,
        NULL,
        0,
        pLog,
        sizeof(VNVME_SMART_LOG),
        &bytesReturned,
        NULL
    );
}

void PrintSmartLog(PVNVME_SMART_LOG pLog)
{
    printf("=== SMART Log ===\n");
    printf("Critical Warning: 0x%02X\n", pLog->CriticalWarning);
    printf("Temperature: %d K (%.1f C)\n", 
           pLog->CompositeTemperature,
           pLog->CompositeTemperature - 273.15);
    printf("Available Spare: %d%%\n", pLog->AvailableSpare);
    printf("Percentage Used: %d%%\n", pLog->PercentageUsed);
    printf("Data Read: %llu GB\n", 
           pLog->DataUnitsRead * 512000 / 1000000000);
    printf("Data Written: %llu GB\n", 
           pLog->DataUnitsWritten * 512000 / 1000000000);
    printf("Power On Hours: %llu\n", pLog->PowerOnHours);
}
```

### NVMe Passthrough

```c
BOOL NvmeIdentify(HANDLE hDevice, ULONG cns, PVOID pBuffer, ULONG bufferSize)
{
    VNVME_PASSTHROUGH_CMD cmd = {0};
    VNVME_PASSTHROUGH_RESULT result = {0};
    DWORD bytesReturned;
    BOOL success;
    
    cmd.StructureSize = sizeof(cmd);
    cmd.Opcode = 0x06;              // Identify
    cmd.NamespaceId = 0;            // 控制器级
    cmd.CDW10 = cns;                // CNS (0=NS, 1=Controller)
    cmd.DataTransferLength = bufferSize;
    cmd.TimeoutMs = 5000;
    
    // METHOD_OUT_DIRECT: 输入在 InputBuffer，输出在 OutputBuffer (MDL)
    success = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_ADMIN_PASSTHROUGH,
        &cmd,                       // 命令结构
        sizeof(cmd),
        pBuffer,                    // 数据缓冲区
        bufferSize,
        &bytesReturned,
        NULL
    );
    
    return success;
}
```

### 创建虚拟磁盘 (总线驱动)

```c
BOOL CreateVirtualDisk(HANDLE hBus, ULONGLONG sizeBytes, PULONG pDiskId)
{
    VNVME_CREATE_DISK_PARAMS params = {0};
    VNVME_CREATE_DISK_RESULT result = {0};
    DWORD bytesReturned;
    
    params.StructureSize = sizeof(params);
    params.DiskId = 0;              // 自动分配
    params.SizeBytes = sizeBytes;
    params.BlockSize = 512;
    params.BackendType = 0;         // 内存后端
    params.ReadOnly = FALSE;
    params.Persistent = FALSE;
    
    if (!DeviceIoControl(
            hBus,
            IOCTL_VNVME_BUS_CREATE_DISK,
            &params,
            sizeof(params),
            &result,
            sizeof(result),
            &bytesReturned,
            NULL)) {
        return FALSE;
    }
    
    if (NT_SUCCESS(result.Status)) {
        *pDiskId = result.AssignedDiskId;
        printf("Created disk %u at %S\n", 
               result.AssignedDiskId, result.DevicePath);
        return TRUE;
    }
    
    return FALSE;
}
```

## 内核态实现

### 注册设备接口

```c
NTSTATUS RegisterDeviceInterface(PDEVICE_CONTEXT pDevCtx)
{
    return WdfDeviceCreateDeviceInterface(
        pDevCtx->Device,
        &GUID_DEVINTERFACE_VNVME,
        NULL    // 无参考字符串
    );
}
```

### IOCTL 分发

```c
VOID EvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT pDevCtx = GetDeviceContext(WdfIoQueueGetDevice(Queue));
    size_t bytesReturned = 0;
    
    switch (IoControlCode) {
        case IOCTL_VNVME_GET_DEVICE_INFO:
            status = HandleGetDeviceInfo(
                pDevCtx, Request, OutputBufferLength, &bytesReturned);
            break;
            
        case IOCTL_VNVME_GET_CONTROLLER_INFO:
            status = HandleGetControllerInfo(
                pDevCtx, Request, OutputBufferLength, &bytesReturned);
            break;
            
        case IOCTL_VNVME_GET_SMART_LOG:
            status = HandleGetSmartLog(
                pDevCtx, Request, OutputBufferLength, &bytesReturned);
            break;
            
        case IOCTL_VNVME_PASSTHROUGH:
        case IOCTL_VNVME_ADMIN_PASSTHROUGH:
            status = HandlePassthrough(
                pDevCtx, Request, InputBufferLength, 
                OutputBufferLength, &bytesReturned);
            break;
            
        case IOCTL_VNVME_FLUSH:
            status = HandleFlush(pDevCtx);
            break;
            
        case IOCTL_VNVME_GET_STATISTICS:
            status = HandleGetStatistics(
                pDevCtx, Request, OutputBufferLength, &bytesReturned);
            break;
            
        case IOCTL_VNVME_RESET_STATISTICS:
            status = HandleResetStatistics(pDevCtx);
            break;
            
        default:
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
```

### 获取设备信息处理

```c
NTSTATUS HandleGetDeviceInfo(
    PDEVICE_CONTEXT pDevCtx,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t *BytesReturned)
{
    PVNVME_DEVICE_INFO pOutput;
    NTSTATUS status;
    
    if (OutputBufferLength < sizeof(VNVME_DEVICE_INFO)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_DEVICE_INFO),
        (PVOID*)&pOutput,
        NULL
    );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    RtlZeroMemory(pOutput, sizeof(VNVME_DEVICE_INFO));
    
    pOutput->StructureSize = sizeof(VNVME_DEVICE_INFO);
    pOutput->DeviceId = pDevCtx->DeviceId;
    pOutput->NamespaceId = pDevCtx->NamespaceId;
    pOutput->TotalSizeBytes = pDevCtx->TotalSize;
    pOutput->BlockSize = pDevCtx->BlockSize;
    pOutput->MaxTransferSize = pDevCtx->MaxTransferSize;
    pOutput->ReadOnly = pDevCtx->ReadOnly;
    pOutput->BackendType = pDevCtx->BackendType;
    
    RtlCopyMemory(pOutput->SerialNumber, 
                  pDevCtx->SerialNumber, 
                  sizeof(pOutput->SerialNumber));
    RtlCopyMemory(pOutput->ModelNumber,
                  pDevCtx->ModelNumber,
                  sizeof(pOutput->ModelNumber));
    
    *BytesReturned = sizeof(VNVME_DEVICE_INFO);
    
    return STATUS_SUCCESS;
}
```

## 安全考虑

### 访问控制

```c
// 限制管理操作只能由管理员执行
NTSTATUS ValidateAccess(WDFREQUEST Request, ULONG IoControlCode)
{
    PIRP pIrp = WdfRequestWdmGetIrp(Request);
    PIO_SECURITY_CONTEXT pSecurityContext = 
        pIrp->Tail.Overlay.CurrentStackLocation->
            Parameters.Create.SecurityContext;
    
    switch (IoControlCode) {
        case IOCTL_VNVME_ADMIN_PASSTHROUGH:
        case IOCTL_VNVME_RESET_STATISTICS:
            // 需要管理员权限
            if (!SeSinglePrivilegeCheck(SeLoadDriverPrivilege, UserMode)) {
                return STATUS_ACCESS_DENIED;
            }
            break;
            
        default:
            break;
    }
    
    return STATUS_SUCCESS;
}
```

### 输入验证

```c
NTSTATUS ValidatePassthroughCommand(PVNVME_PASSTHROUGH_CMD pCmd)
{
    // 检查结构大小
    if (pCmd->StructureSize < sizeof(VNVME_PASSTHROUGH_CMD)) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 检查数据传输大小
    if (pCmd->DataTransferLength > MAX_TRANSFER_SIZE) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 检查超时
    if (pCmd->TimeoutMs == 0 || pCmd->TimeoutMs > MAX_TIMEOUT_MS) {
        pCmd->TimeoutMs = DEFAULT_TIMEOUT_MS;
    }
    
    // 禁止危险命令 (可选)
    switch (pCmd->Opcode) {
        case 0x80:  // Format NVM
        case 0x81:  // Security Send
        case 0x82:  // Security Receive
        case 0x84:  // Sanitize
            return STATUS_ACCESS_DENIED;
    }
    
    return STATUS_SUCCESS;
}
```

## 错误处理

### IOCTL 错误码映射

| NTSTATUS | Win32 错误 | 说明 |
|----------|-----------|------|
| STATUS_SUCCESS | ERROR_SUCCESS | 成功 |
| STATUS_BUFFER_TOO_SMALL | ERROR_INSUFFICIENT_BUFFER | 缓冲区太小 |
| STATUS_INVALID_PARAMETER | ERROR_INVALID_PARAMETER | 无效参数 |
| STATUS_INVALID_DEVICE_REQUEST | ERROR_INVALID_FUNCTION | 不支持的 IOCTL |
| STATUS_ACCESS_DENIED | ERROR_ACCESS_DENIED | 权限不足 |
| STATUS_DEVICE_NOT_READY | ERROR_NOT_READY | 设备未就绪 |
| STATUS_IO_TIMEOUT | ERROR_SEM_TIMEOUT | 操作超时 |
