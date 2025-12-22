# IOCTL 接口

本文档详细说明用户模式管理工具与驱动的通信接口。

## 概述

用户模式管理工具 (vnvmectl.exe) 通过 DeviceIoControl 与驱动通信，实现：

- 创建/删除虚拟 NVMe 控制器
- 添加/移除命名空间
- 配置后端存储
- 查询状态和统计信息

```
┌─────────────────────────────────────────────────────────────────────┐
│                       IOCTL 架构                                     │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│    用户模式                                                           │
│    ┌────────────────────────────────────────────────────────┐       │
│    │                   vnvmectl.exe                          │       │
│    │         (命令行管理工具 / GUI 工具)                       │       │
│    └────────────────────────────────────────────────────────┘       │
│                              │                                       │
│                    CreateFile + DeviceIoControl                     │
│                              │                                       │
│                              ▼                                       │
│    内核模式                                                          │
│    ┌────────────────────────────────────────────────────────┐       │
│    │                 vnvme_bus.sys                            │       │
│    │              (控制设备接口)                               │       │
│    │          \\.\VNVMEControl                                │       │
│    └────────────────────────────────────────────────────────┘       │
│                              │                                       │
│                    创建/删除子设备                                   │
│                              │                                       │
│                              ▼                                       │
│    ┌────────────────────────────────────────────────────────┐       │
│    │                 vnvme_emu.sys                            │       │
│    │          (NVMe 控制器仿真)                               │       │
│    └────────────────────────────────────────────────────────┘       │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## 设备接口

### 控制设备

Bus 驱动创建一个控制设备用于接收管理命令：

```c
// 设备符号链接
#define VNVME_CONTROL_DEVICE_NAME    L"\\Device\\VNVMEControl"
#define VNVME_CONTROL_SYMLINK_NAME   L"\\DosDevices\\VNVMEControl"

// 设备 GUID
// {12345678-1234-5678-ABCD-123456789ABC}
DEFINE_GUID(GUID_DEVINTERFACE_VNVME_CONTROL,
    0x12345678, 0x1234, 0x5678,
    0xAB, 0xCD, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC);
```

### 设备创建

```c
NTSTATUS VnvmeCreateControlDevice(
    _In_ WDFDRIVER Driver)
{
    PWDFDEVICE_INIT deviceInit;
    WDFDEVICE controlDevice;
    WDF_OBJECT_ATTRIBUTES attributes;
    UNICODE_STRING ntDeviceName;
    UNICODE_STRING symbolicLink;
    WDF_IO_QUEUE_CONFIG ioQueueConfig;
    WDFQUEUE queue;
    NTSTATUS status;
    
    // 分配设备初始化结构
    deviceInit = WdfControlDeviceInitAllocate(Driver,
                                              &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);
    if (!deviceInit) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 设置设备名称
    RtlInitUnicodeString(&ntDeviceName, VNVME_CONTROL_DEVICE_NAME);
    status = WdfDeviceInitAssignName(deviceInit, &ntDeviceName);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    // 设置设备类型
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, FALSE);
    
    // 创建设备
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VNVME_CONTROL_CONTEXT);
    
    status = WdfDeviceCreate(&deviceInit, &attributes, &controlDevice);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 创建符号链接
    RtlInitUnicodeString(&symbolicLink, VNVME_CONTROL_SYMLINK_NAME);
    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 创建 I/O 队列
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig,
                                           WdfIoQueueDispatchSequential);
    ioQueueConfig.EvtIoDeviceControl = VnvmeControlDeviceIoControl;
    
    status = WdfIoQueueCreate(controlDevice, &ioQueueConfig,
                             WDF_NO_OBJECT_ATTRIBUTES, &queue);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 完成设备初始化
    WdfControlFinishInitializing(controlDevice);
    
    return STATUS_SUCCESS;
}
```

## IOCTL 定义

### IOCTL 代码

```c
#define FILE_DEVICE_VNVME   0x8000

// 控制器管理
#define IOCTL_VNVME_CREATE_CONTROLLER \
    CTL_CODE(FILE_DEVICE_VNVME, 0x801, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_DELETE_CONTROLLER \
    CTL_CODE(FILE_DEVICE_VNVME, 0x802, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_QUERY_CONTROLLER \
    CTL_CODE(FILE_DEVICE_VNVME, 0x803, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_LIST_CONTROLLERS \
    CTL_CODE(FILE_DEVICE_VNVME, 0x804, METHOD_BUFFERED, FILE_READ_ACCESS)

// 命名空间管理
#define IOCTL_VNVME_CREATE_NAMESPACE \
    CTL_CODE(FILE_DEVICE_VNVME, 0x810, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_DELETE_NAMESPACE \
    CTL_CODE(FILE_DEVICE_VNVME, 0x811, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_QUERY_NAMESPACE \
    CTL_CODE(FILE_DEVICE_VNVME, 0x812, METHOD_BUFFERED, FILE_READ_ACCESS)

// 后端管理
#define IOCTL_VNVME_ATTACH_BACKEND \
    CTL_CODE(FILE_DEVICE_VNVME, 0x820, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define IOCTL_VNVME_DETACH_BACKEND \
    CTL_CODE(FILE_DEVICE_VNVME, 0x821, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// 统计与诊断
#define IOCTL_VNVME_GET_STATISTICS \
    CTL_CODE(FILE_DEVICE_VNVME, 0x830, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_RESET_STATISTICS \
    CTL_CODE(FILE_DEVICE_VNVME, 0x831, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// 调试
#define IOCTL_VNVME_GET_DEBUG_LOG \
    CTL_CODE(FILE_DEVICE_VNVME, 0x840, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_VNVME_SET_DEBUG_LEVEL \
    CTL_CODE(FILE_DEVICE_VNVME, 0x841, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// ============ v2 架构: 用户态服务通信 ============

// 共享内存映射 (用户态服务调用)
#define IOCTL_VNVME_MAP_SHARED_MEMORY \
    CTL_CODE(FILE_DEVICE_VNVME, 0x850, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)

// 通知内核用户态已就绪
#define IOCTL_VNVME_USER_READY \
    CTL_CODE(FILE_DEVICE_VNVME, 0x851, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// 获取待处理命令事件句柄
#define IOCTL_VNVME_GET_COMMAND_EVENT \
    CTL_CODE(FILE_DEVICE_VNVME, 0x852, METHOD_BUFFERED, FILE_READ_ACCESS)

// 提交完成条目
#define IOCTL_VNVME_SUBMIT_COMPLETIONS \
    CTL_CODE(FILE_DEVICE_VNVME, 0x853, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// 用户态心跳
#define IOCTL_VNVME_HEARTBEAT \
    CTL_CODE(FILE_DEVICE_VNVME, 0x854, METHOD_BUFFERED, FILE_WRITE_ACCESS)

// 获取控制器配置
#define IOCTL_VNVME_GET_CONTROLLER_CONFIG \
    CTL_CODE(FILE_DEVICE_VNVME, 0x855, METHOD_BUFFERED, FILE_READ_ACCESS)
```

---

## v2 用户态服务通信结构

### 共享内存映射

```c
//
// IOCTL_VNVME_MAP_SHARED_MEMORY 输入
//
typedef struct _VNVME_MAP_SHARED_MEMORY_IN {
    ULONG ControllerIndex;          // 控制器索引
} VNVME_MAP_SHARED_MEMORY_IN, *PVNVME_MAP_SHARED_MEMORY_IN;

//
// IOCTL_VNVME_MAP_SHARED_MEMORY 输出
//
typedef struct _VNVME_MAP_SHARED_MEMORY_OUT {
    VNVME_IOCTL_RESULT Result;
    
    PVOID UserAddress;              // 映射到用户空间的地址
    SIZE_T Size;                    // 共享内存总大小
    
    // 区域偏移 (相对于 UserAddress)
    ULONG ControlBlockOffset;       // 控制块偏移
    ULONG CommandRingOffset;        // 命令环偏移
    ULONG CompletionRingOffset;     // 完成环偏移
    ULONG DataBufferOffset;         // 数据缓冲区偏移
    
    // 区域大小
    ULONG CommandRingSize;          // 命令环大小
    ULONG CompletionRingSize;       // 完成环大小
    ULONG DataBufferSize;           // 数据缓冲区大小
    
} VNVME_MAP_SHARED_MEMORY_OUT, *PVNVME_MAP_SHARED_MEMORY_OUT;

//
// IOCTL_VNVME_USER_READY 输入
//
typedef struct _VNVME_USER_READY_IN {
    ULONG ControllerIndex;
    ULONG UserPid;                  // 用户进程 PID (用于安全验证)
} VNVME_USER_READY_IN, *PVNVME_USER_READY_IN;

//
// IOCTL_VNVME_GET_COMMAND_EVENT 输出
//
typedef struct _VNVME_GET_COMMAND_EVENT_OUT {
    VNVME_IOCTL_RESULT Result;
    HANDLE CommandReadyEvent;       // 用户态可等待的事件句柄
    HANDLE ShutdownEvent;           // 关机事件句柄
} VNVME_GET_COMMAND_EVENT_OUT, *PVNVME_GET_COMMAND_EVENT_OUT;

//
// IOCTL_VNVME_SUBMIT_COMPLETIONS 输入
//
typedef struct _VNVME_SUBMIT_COMPLETIONS_IN {
    ULONG ControllerIndex;
    ULONG CompletionCount;          // 完成条目数量
    // 完成条目在共享内存的完成环中，内核直接读取
} VNVME_SUBMIT_COMPLETIONS_IN, *PVNVME_SUBMIT_COMPLETIONS_IN;

//
// IOCTL_VNVME_HEARTBEAT 输入
//
typedef struct _VNVME_HEARTBEAT_IN {
    ULONG ControllerIndex;
    ULONG64 Timestamp;              // 用户态时间戳
    ULONG64 CommandsProcessed;      // 已处理命令数
} VNVME_HEARTBEAT_IN, *PVNVME_HEARTBEAT_IN;
```

### 共享内存控制块结构

```c
//
// 共享内存控制块 (位于共享内存开头)
// 内核和用户态都可以访问
//
typedef struct _VNVME_SHARED_CONTROL_BLOCK {
    // 魔数和版本
    ULONG Magic;                    // 0x454D564E ("VNME")
    ULONG Version;                  // 结构版本
    
    // 状态
    volatile LONG State;            // VNVME_SHARED_STATE_*
    volatile LONG UserConnected;    // 用户态是否已连接
    
    // 心跳
    volatile LONG64 KernelHeartbeat;  // 内核更新
    volatile LONG64 UserHeartbeat;    // 用户态更新
    
    // 命令环 (内核写入，用户读取)
    volatile ULONG CommandRingHead;   // 用户态读取位置
    volatile ULONG CommandRingTail;   // 内核写入位置
    ULONG CommandRingMask;            // Size - 1
    ULONG CommandEntrySize;           // sizeof(VNVME_SHARED_COMMAND)
    
    // 完成环 (用户写入，内核读取)
    volatile ULONG CompletionRingHead; // 内核读取位置
    volatile ULONG CompletionRingTail; // 用户态写入位置
    ULONG CompletionRingMask;
    ULONG CompletionEntrySize;        // sizeof(VNVME_SHARED_COMPLETION)
    
    // 数据缓冲区
    ULONG DataBufferBlockSize;        // 4096
    ULONG DataBufferBlockCount;
    volatile ULONG64 DataBufferBitmap[256]; // 位图 (支持 16K 块 = 64MB)
    
    // 统计
    volatile ULONG64 TotalCommands;
    volatile ULONG64 TotalCompletions;
    volatile ULONG64 TotalBytesRead;
    volatile ULONG64 TotalBytesWritten;
    volatile ULONG64 TotalErrors;
    
    // 填充到 4KB
    UCHAR Reserved[3584];
    
} VNVME_SHARED_CONTROL_BLOCK, *PVNVME_SHARED_CONTROL_BLOCK;

C_ASSERT(sizeof(VNVME_SHARED_CONTROL_BLOCK) == 4096);

// 共享内存状态
#define VNVME_SHARED_STATE_INITIALIZING    0
#define VNVME_SHARED_STATE_WAITING_USER    1
#define VNVME_SHARED_STATE_RUNNING         2
#define VNVME_SHARED_STATE_STOPPING        3
#define VNVME_SHARED_STATE_ERROR           4
```

---

## 数据结构

### 通用结构

```c
//
// 结果头
//
typedef struct _VNVME_IOCTL_RESULT {
    NTSTATUS Status;
    ULONG ErrorCode;
    WCHAR ErrorMessage[128];
} VNVME_IOCTL_RESULT, *PVNVME_IOCTL_RESULT;
```

### 创建控制器

```c
//
// IOCTL_VNVME_CREATE_CONTROLLER 输入
//
typedef struct _VNVME_CREATE_CONTROLLER_IN {
    // 标识
    ULONG ControllerIndex;          // 0 = 自动分配
    WCHAR FriendlyName[64];         // 可选的友好名称
    
    // PCI ID (可选，使用默认值)
    USHORT VendorId;                // 0 = 使用默认
    USHORT DeviceId;
    USHORT SubsystemVendorId;
    USHORT SubsystemDeviceId;
    
    // 容量配置
    ULONG64 TotalCapacityBytes;     // 总容量
    ULONG BlockSize;                // 512 或 4096
    
    // 队列配置
    USHORT MaxQueueEntries;         // 最大队列条目数
    USHORT MaxIoQueues;             // 最大 I/O 队列数
    
    // 后端配置
    VNVME_BACKEND_TYPE BackendType;
    WCHAR BackendPath[260];         // 文件路径 (仅文件/VHD 后端)
    BOOLEAN BackendReadOnly;        // 只读模式
    BOOLEAN BackendCreateIfNotExist;// 如果不存在则创建
    BOOLEAN UseSparseFile;          // 使用稀疏文件
    
    // 特性开关
    BOOLEAN EnableTrim;
    BOOLEAN EnableFlush;
    BOOLEAN EnableWriteCache;
    
    // 自动启动命名空间
    BOOLEAN CreateDefaultNamespace;
    
} VNVME_CREATE_CONTROLLER_IN, *PVNVME_CREATE_CONTROLLER_IN;

//
// IOCTL_VNVME_CREATE_CONTROLLER 输出
//
typedef struct _VNVME_CREATE_CONTROLLER_OUT {
    VNVME_IOCTL_RESULT Result;
    
    ULONG ControllerIndex;          // 分配的控制器索引
    WCHAR InstanceId[64];           // PnP 实例 ID
    WCHAR DevicePath[260];          // 设备路径
    
} VNVME_CREATE_CONTROLLER_OUT, *PVNVME_CREATE_CONTROLLER_OUT;
```

### 删除控制器

```c
//
// IOCTL_VNVME_DELETE_CONTROLLER 输入
//
typedef struct _VNVME_DELETE_CONTROLLER_IN {
    ULONG ControllerIndex;
    BOOLEAN Force;                  // 强制删除，即使有打开的句柄
} VNVME_DELETE_CONTROLLER_IN, *PVNVME_DELETE_CONTROLLER_IN;

//
// IOCTL_VNVME_DELETE_CONTROLLER 输出
//
typedef struct _VNVME_DELETE_CONTROLLER_OUT {
    VNVME_IOCTL_RESULT Result;
} VNVME_DELETE_CONTROLLER_OUT, *PVNVME_DELETE_CONTROLLER_OUT;
```

### 查询控制器

```c
//
// IOCTL_VNVME_QUERY_CONTROLLER 输入
//
typedef struct _VNVME_QUERY_CONTROLLER_IN {
    ULONG ControllerIndex;
} VNVME_QUERY_CONTROLLER_IN, *PVNVME_QUERY_CONTROLLER_IN;

//
// IOCTL_VNVME_QUERY_CONTROLLER 输出
//
typedef struct _VNVME_QUERY_CONTROLLER_OUT {
    VNVME_IOCTL_RESULT Result;
    
    // 基本信息
    ULONG ControllerIndex;
    WCHAR FriendlyName[64];
    WCHAR InstanceId[64];
    
    // 状态
    VNVME_CONTROLLER_STATE State;
    BOOLEAN IsEnabled;
    BOOLEAN IsReady;
    
    // PCI ID
    USHORT VendorId;
    USHORT DeviceId;
    CHAR SerialNumber[20];
    CHAR ModelNumber[40];
    CHAR FirmwareRevision[8];
    
    // 容量
    ULONG64 TotalCapacity;
    ULONG BlockSize;
    
    // 队列信息
    USHORT MaxQueueEntries;
    USHORT MaxIoQueues;
    USHORT ActiveIoSqCount;
    USHORT ActiveIoCqCount;
    
    // 命名空间
    ULONG NamespaceCount;
    
    // 后端
    VNVME_BACKEND_TYPE BackendType;
    WCHAR BackendPath[260];
    BOOLEAN BackendReadOnly;
    
    // NVMe 版本
    ULONG NvmeVersion;
    
} VNVME_QUERY_CONTROLLER_OUT, *PVNVME_QUERY_CONTROLLER_OUT;
```

### 列出控制器

```c
//
// IOCTL_VNVME_LIST_CONTROLLERS 输出
//
typedef struct _VNVME_CONTROLLER_ENTRY {
    ULONG Index;
    WCHAR FriendlyName[64];
    VNVME_CONTROLLER_STATE State;
    ULONG64 TotalCapacity;
    ULONG NamespaceCount;
} VNVME_CONTROLLER_ENTRY, *PVNVME_CONTROLLER_ENTRY;

typedef struct _VNVME_LIST_CONTROLLERS_OUT {
    VNVME_IOCTL_RESULT Result;
    ULONG ControllerCount;
    VNVME_CONTROLLER_ENTRY Controllers[32];
} VNVME_LIST_CONTROLLERS_OUT, *PVNVME_LIST_CONTROLLERS_OUT;
```

### 创建命名空间

```c
//
// IOCTL_VNVME_CREATE_NAMESPACE 输入
//
typedef struct _VNVME_CREATE_NAMESPACE_IN {
    ULONG ControllerIndex;
    ULONG NamespaceId;              // 0 = 自动分配
    ULONG64 SizeInBytes;
    ULONG BlockSize;                // 512 或 4096
    BOOLEAN ReadOnly;
    GUID UniqueId;                  // 可选
} VNVME_CREATE_NAMESPACE_IN, *PVNVME_CREATE_NAMESPACE_IN;

//
// IOCTL_VNVME_CREATE_NAMESPACE 输出
//
typedef struct _VNVME_CREATE_NAMESPACE_OUT {
    VNVME_IOCTL_RESULT Result;
    ULONG NamespaceId;
} VNVME_CREATE_NAMESPACE_OUT, *PVNVME_CREATE_NAMESPACE_OUT;
```

### 统计信息

```c
//
// IOCTL_VNVME_GET_STATISTICS 输入
//
typedef struct _VNVME_GET_STATISTICS_IN {
    ULONG ControllerIndex;
    BOOLEAN IncludeNamespaceStats;
} VNVME_GET_STATISTICS_IN, *PVNVME_GET_STATISTICS_IN;

//
// IOCTL_VNVME_GET_STATISTICS 输出
//
typedef struct _VNVME_STATISTICS {
    // 命令计数
    ULONG64 AdminCommandsReceived;
    ULONG64 AdminCommandsCompleted;
    ULONG64 IoCommandsReceived;
    ULONG64 IoCommandsCompleted;
    
    // 数据传输
    ULONG64 TotalReadBytes;
    ULONG64 TotalWriteBytes;
    ULONG64 TotalReadCommands;
    ULONG64 TotalWriteCommands;
    ULONG64 TotalFlushCommands;
    ULONG64 TotalTrimCommands;
    
    // 错误
    ULONG64 CommandErrors;
    ULONG64 MediaErrors;
    ULONG64 InternalErrors;
    
    // 中断
    ULONG64 InterruptsGenerated;
    ULONG64 InterruptsCoalesced;
    
    // 运行时间
    ULONG64 UptimeSeconds;
    
} VNVME_STATISTICS, *PVNVME_STATISTICS;

typedef struct _VNVME_GET_STATISTICS_OUT {
    VNVME_IOCTL_RESULT Result;
    VNVME_STATISTICS Stats;
} VNVME_GET_STATISTICS_OUT, *PVNVME_GET_STATISTICS_OUT;
```

## IOCTL 处理

### 分发函数

```c
VOID VnvmeControlDeviceIoControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    NTSTATUS status;
    PVOID inputBuffer = NULL;
    PVOID outputBuffer = NULL;
    size_t bytesReturned = 0;
    
    UNREFERENCED_PARAMETER(Queue);
    
    // 获取缓冲区
    if (InputBufferLength > 0) {
        status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength,
                                               &inputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
    }
    
    if (OutputBufferLength > 0) {
        status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength,
                                                &outputBuffer, NULL);
        if (!NT_SUCCESS(status)) {
            WdfRequestComplete(Request, status);
            return;
        }
    }
    
    // 分发到具体处理函数
    switch (IoControlCode) {
    case IOCTL_VNVME_CREATE_CONTROLLER:
        status = VnvmeIoctlCreateController(inputBuffer, InputBufferLength,
                                           outputBuffer, OutputBufferLength,
                                           &bytesReturned);
        break;
        
    case IOCTL_VNVME_DELETE_CONTROLLER:
        status = VnvmeIoctlDeleteController(inputBuffer, InputBufferLength,
                                           outputBuffer, OutputBufferLength,
                                           &bytesReturned);
        break;
        
    case IOCTL_VNVME_QUERY_CONTROLLER:
        status = VnvmeIoctlQueryController(inputBuffer, InputBufferLength,
                                          outputBuffer, OutputBufferLength,
                                          &bytesReturned);
        break;
        
    case IOCTL_VNVME_LIST_CONTROLLERS:
        status = VnvmeIoctlListControllers(outputBuffer, OutputBufferLength,
                                          &bytesReturned);
        break;
        
    case IOCTL_VNVME_CREATE_NAMESPACE:
        status = VnvmeIoctlCreateNamespace(inputBuffer, InputBufferLength,
                                          outputBuffer, OutputBufferLength,
                                          &bytesReturned);
        break;
        
    case IOCTL_VNVME_GET_STATISTICS:
        status = VnvmeIoctlGetStatistics(inputBuffer, InputBufferLength,
                                        outputBuffer, OutputBufferLength,
                                        &bytesReturned);
        break;
        
    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }
    
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}
```

### 创建控制器处理

```c
NTSTATUS VnvmeIoctlCreateController(
    _In_ PVOID InputBuffer,
    _In_ SIZE_T InputBufferLength,
    _Out_ PVOID OutputBuffer,
    _In_ SIZE_T OutputBufferLength,
    _Out_ PSIZE_T BytesReturned)
{
    PVNVME_CREATE_CONTROLLER_IN input = InputBuffer;
    PVNVME_CREATE_CONTROLLER_OUT output = OutputBuffer;
    NTSTATUS status;
    PVNVME_CONTROLLER controller;
    
    *BytesReturned = 0;
    
    // 验证缓冲区大小
    if (InputBufferLength < sizeof(*input) ||
        OutputBufferLength < sizeof(*output)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    RtlZeroMemory(output, sizeof(*output));
    
    // 验证参数
    if (input->TotalCapacityBytes == 0) {
        output->Result.Status = STATUS_INVALID_PARAMETER;
        output->Result.ErrorCode = VNVME_ERROR_INVALID_CAPACITY;
        wcscpy_s(output->Result.ErrorMessage, 128, L"Capacity must be greater than 0");
        *BytesReturned = sizeof(*output);
        return STATUS_INVALID_PARAMETER;
    }
    
    if (input->BlockSize != 512 && input->BlockSize != 4096) {
        input->BlockSize = 512;  // 使用默认值
    }
    
    // 分配控制器索引
    ULONG controllerIndex = input->ControllerIndex;
    if (controllerIndex == 0) {
        controllerIndex = VnvmeAllocateControllerIndex();
    }
    
    // 创建 PDO
    status = VnvmeBusPlugInDevice(controllerIndex, input, &controller);
    
    if (NT_SUCCESS(status)) {
        output->Result.Status = STATUS_SUCCESS;
        output->ControllerIndex = controllerIndex;
        
        // 填充设备路径
        swprintf_s(output->InstanceId, 64, L"VNVME\\CTRL%04d", controllerIndex);
        swprintf_s(output->DevicePath, 260, 
            L"\\\\?\\PCI#VEN_1B36&DEV_0010#VNVME%04d", controllerIndex);
    } else {
        output->Result.Status = status;
        output->Result.ErrorCode = VNVME_ERROR_CREATE_FAILED;
        wcscpy_s(output->Result.ErrorMessage, 128, L"Failed to create controller");
    }
    
    *BytesReturned = sizeof(*output);
    return status;
}
```

## 用户模式库

### 头文件

```c
// vnvmelib.h

#ifndef _VNVME_LIB_H_
#define _VNVME_LIB_H_

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

// 初始化/关闭
BOOL VnvmeLibInit(void);
void VnvmeLibShutdown(void);

// 控制器管理
DWORD VnvmeCreateController(
    _In_ const VNVME_CREATE_CONTROLLER_IN* params,
    _Out_ VNVME_CREATE_CONTROLLER_OUT* result);

DWORD VnvmeDeleteController(
    _In_ ULONG controllerIndex,
    _In_ BOOL force);

DWORD VnvmeQueryController(
    _In_ ULONG controllerIndex,
    _Out_ VNVME_QUERY_CONTROLLER_OUT* info);

DWORD VnvmeListControllers(
    _Out_ VNVME_LIST_CONTROLLERS_OUT* list);

// 命名空间管理
DWORD VnvmeCreateNamespace(
    _In_ ULONG controllerIndex,
    _In_ ULONG64 sizeBytes,
    _In_ ULONG blockSize,
    _Out_ PULONG namespaceId);

DWORD VnvmeDeleteNamespace(
    _In_ ULONG controllerIndex,
    _In_ ULONG namespaceId);

// 统计
DWORD VnvmeGetStatistics(
    _In_ ULONG controllerIndex,
    _Out_ VNVME_STATISTICS* stats);

DWORD VnvmeResetStatistics(
    _In_ ULONG controllerIndex);

#ifdef __cplusplus
}
#endif

#endif // _VNVME_LIB_H_
```

### 实现

```c
// vnvmelib.c

#include "vnvmelib.h"
#include <stdio.h>

static HANDLE g_hDevice = INVALID_HANDLE_VALUE;

BOOL VnvmeLibInit(void)
{
    g_hDevice = CreateFileW(
        L"\\\\.\\VNVMEControl",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    
    return g_hDevice != INVALID_HANDLE_VALUE;
}

void VnvmeLibShutdown(void)
{
    if (g_hDevice != INVALID_HANDLE_VALUE) {
        CloseHandle(g_hDevice);
        g_hDevice = INVALID_HANDLE_VALUE;
    }
}

DWORD VnvmeCreateController(
    _In_ const VNVME_CREATE_CONTROLLER_IN* params,
    _Out_ VNVME_CREATE_CONTROLLER_OUT* result)
{
    DWORD bytesReturned;
    
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        return ERROR_NOT_READY;
    }
    
    if (!DeviceIoControl(
        g_hDevice,
        IOCTL_VNVME_CREATE_CONTROLLER,
        (LPVOID)params, sizeof(*params),
        result, sizeof(*result),
        &bytesReturned,
        NULL)) {
        return GetLastError();
    }
    
    return NT_SUCCESS(result->Result.Status) ? ERROR_SUCCESS : 
           RtlNtStatusToDosError(result->Result.Status);
}

DWORD VnvmeQueryController(
    _In_ ULONG controllerIndex,
    _Out_ VNVME_QUERY_CONTROLLER_OUT* info)
{
    VNVME_QUERY_CONTROLLER_IN input = { controllerIndex };
    DWORD bytesReturned;
    
    if (g_hDevice == INVALID_HANDLE_VALUE) {
        return ERROR_NOT_READY;
    }
    
    if (!DeviceIoControl(
        g_hDevice,
        IOCTL_VNVME_QUERY_CONTROLLER,
        &input, sizeof(input),
        info, sizeof(*info),
        &bytesReturned,
        NULL)) {
        return GetLastError();
    }
    
    return NT_SUCCESS(info->Result.Status) ? ERROR_SUCCESS :
           RtlNtStatusToDosError(info->Result.Status);
}

// ... 其他函数实现
```

## 命令行工具

### vnvmectl 使用示例

```
vnvmectl.exe - Virtual NVMe Controller Management Tool

Usage:
  vnvmectl create [options]     Create a new virtual NVMe controller
  vnvmectl delete <index>       Delete a virtual NVMe controller
  vnvmectl list                 List all virtual NVMe controllers
  vnvmectl info <index>         Show controller information
  vnvmectl stats <index>        Show statistics

Create options:
  --size <size>       Total capacity (e.g., 1G, 500M, 100G)
  --block-size <512|4096>    Logical block size
  --backend <type>    Backend type (memory, file, vhd)
  --path <path>       Backend file path (for file/vhd backend)
  --name <name>       Friendly name

Examples:
  vnvmectl create --size 1G --backend memory
  vnvmectl create --size 100G --backend file --path C:\VMs\disk.img
  vnvmectl delete 1
  vnvmectl list
```

### 实现示例

```c
// vnvmectl main.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "vnvmelib.h"

int cmd_create(int argc, wchar_t* argv[])
{
    VNVME_CREATE_CONTROLLER_IN params = { 0 };
    VNVME_CREATE_CONTROLLER_OUT result = { 0 };
    DWORD error;
    
    // 解析参数
    params.BackendType = VnvmeBackendMemory;
    params.TotalCapacityBytes = 1ULL * 1024 * 1024 * 1024;  // 默认 1GB
    params.BlockSize = 512;
    params.MaxQueueEntries = 1024;
    params.MaxIoQueues = 16;
    params.CreateDefaultNamespace = TRUE;
    
    for (int i = 0; i < argc; i++) {
        if (wcscmp(argv[i], L"--size") == 0 && i + 1 < argc) {
            params.TotalCapacityBytes = ParseSize(argv[++i]);
        } else if (wcscmp(argv[i], L"--backend") == 0 && i + 1 < argc) {
            i++;
            if (wcscmp(argv[i], L"memory") == 0) {
                params.BackendType = VnvmeBackendMemory;
            } else if (wcscmp(argv[i], L"file") == 0) {
                params.BackendType = VnvmeBackendFile;
            }
        } else if (wcscmp(argv[i], L"--path") == 0 && i + 1 < argc) {
            wcscpy_s(params.BackendPath, 260, argv[++i]);
        }
    }
    
    // 创建控制器
    error = VnvmeCreateController(&params, &result);
    
    if (error == ERROR_SUCCESS) {
        wprintf(L"Controller created successfully\n");
        wprintf(L"  Index: %d\n", result.ControllerIndex);
        wprintf(L"  Instance: %s\n", result.InstanceId);
        return 0;
    } else {
        wprintf(L"Failed to create controller: %s\n", result.Result.ErrorMessage);
        return 1;
    }
}

int cmd_list(void)
{
    VNVME_LIST_CONTROLLERS_OUT list;
    DWORD error;
    
    error = VnvmeListControllers(&list);
    
    if (error != ERROR_SUCCESS) {
        wprintf(L"Failed to list controllers\n");
        return 1;
    }
    
    wprintf(L"Index  Name                           State     Capacity\n");
    wprintf(L"-----  -----------------------------  --------  ----------\n");
    
    for (ULONG i = 0; i < list.ControllerCount; i++) {
        wprintf(L"%-5d  %-30s  %-8s  %llu GB\n",
            list.Controllers[i].Index,
            list.Controllers[i].FriendlyName,
            GetStateName(list.Controllers[i].State),
            list.Controllers[i].TotalCapacity / (1024 * 1024 * 1024));
    }
    
    return 0;
}

int wmain(int argc, wchar_t* argv[])
{
    int result = 0;
    
    if (!VnvmeLibInit()) {
        wprintf(L"Failed to connect to VNVME driver\n");
        wprintf(L"Make sure the driver is installed and running\n");
        return 1;
    }
    
    if (argc < 2) {
        PrintUsage();
        result = 1;
    } else if (wcscmp(argv[1], L"create") == 0) {
        result = cmd_create(argc - 2, argv + 2);
    } else if (wcscmp(argv[1], L"list") == 0) {
        result = cmd_list();
    } else if (wcscmp(argv[1], L"delete") == 0 && argc > 2) {
        result = cmd_delete(_wtoi(argv[2]));
    } else if (wcscmp(argv[1], L"info") == 0 && argc > 2) {
        result = cmd_info(_wtoi(argv[2]));
    } else {
        PrintUsage();
        result = 1;
    }
    
    VnvmeLibShutdown();
    return result;
}
```
