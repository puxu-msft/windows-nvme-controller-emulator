# VNVME API 参考文档

## 目录

1. [IOCTL 接口](#ioctl-接口)
2. [共享内存结构](#共享内存结构)
3. [NVMe 命令处理](#nvme-命令处理)
4. [用户态服务 API](#用户态服务-api)

---

## IOCTL 接口

所有 IOCTL 通过控制设备 `\\.\VNVMEControl` 进行通信。

> **参考**：完整定义请查看 [include/vnvme_ioctl.h](../../../include/vnvme_ioctl.h)

### 设备类型和索引基址

```c
#define FILE_DEVICE_VNVME       0x8000U
#define VNVME_IOCTL_INDEX_BASE  0x800U
```

### 设备访问

```c
HANDLE hDevice = CreateFile(
    L"\\\\.\\VNVMEControl",
    GENERIC_READ | GENERIC_WRITE,
    0,
    NULL,
    OPEN_EXISTING,
    FILE_ATTRIBUTE_NORMAL,
    NULL
);
```

---

### IOCTL 代码汇总

| IOCTL | 功能码 | 描述 |
|-------|--------|------|
| `IOCTL_VNVME_GET_VERSION` | 0x800 | 获取驱动版本 |
| `IOCTL_VNVME_GET_STATUS` | 0x801 | 获取驱动状态 |
| `IOCTL_VNVME_MAP_SHM` | 0x80A | 映射共享内存 |
| `IOCTL_VNVME_UNMAP_SHM` | 0x80B | 取消映射共享内存 |
| `IOCTL_VNVME_USER_READY` | 0x814 | 通知用户态就绪 |
| `IOCTL_VNVME_USER_SHUTDOWN` | 0x815 | 通知用户态关闭 |
| `IOCTL_VNVME_HEARTBEAT` | 0x816 | 心跳 |
| `IOCTL_VNVME_GET_COMMAND_EVENT` | 0x81E | 获取命令事件 |
| `IOCTL_VNVME_SUBMIT_COMPLETIONS` | 0x81F | 提交完成 |
| `IOCTL_VNVME_CREATE_CONTROLLER` | 0x828 | 创建控制器 |
| `IOCTL_VNVME_DELETE_CONTROLLER` | 0x829 | 删除控制器 |
| `IOCTL_VNVME_LIST_CONTROLLERS` | 0x82A | 列出控制器 |
| `IOCTL_VNVME_CREATE_NAMESPACE` | 0x832 | 创建命名空间 |
| `IOCTL_VNVME_DELETE_NAMESPACE` | 0x833 | 删除命名空间 |
| `IOCTL_VNVME_LIST_NAMESPACES` | 0x834 | 列出命名空间 |
| `IOCTL_VNVME_GET_DEBUG_INFO` | 0x864 | 获取调试信息 |
| `IOCTL_VNVME_SET_DEBUG_LEVEL` | 0x865 | 设置调试级别 |
| `IOCTL_VNVME_GET_STATS` | 0x86E | 获取统计信息 |
| `IOCTL_VNVME_RESET_STATS` | 0x86F | 重置统计 |

---

### IOCTL_VNVME_GET_VERSION

获取驱动版本信息。

```c
#define IOCTL_VNVME_GET_VERSION CTL_CODE(FILE_DEVICE_VNVME, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

**输入**: 无

**输出**:
```c
typedef struct _VNVME_GET_VERSION_OUTPUT {
    UINT32 DriverVersion;     // 驱动版本
    UINT32 ApiVersion;        // API 版本
    UINT32 BuildNumber;       // 构建号
    UINT32 Reserved;
} VNVME_GET_VERSION_OUTPUT;
```

---

### IOCTL_VNVME_GET_STATUS

获取驱动状态信息。

```c
#define IOCTL_VNVME_GET_STATUS CTL_CODE(FILE_DEVICE_VNVME, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)
```

**输入**: 无

**输出**:
```c
typedef struct _VNVME_GET_STATUS_OUTPUT {
    UINT32 DriverStatus;          // 驱动状态 (VNVME_DRIVER_STATUS_*)
    UINT32 UserServiceStatus;     // 用户态服务状态 (VNVME_USER_STATUS_*)
    UINT32 ControllerCount;       // 控制器数量
    UINT32 NamespaceCount;        // 命名空间数量
    UINT32 ShmMapped;             // 共享内存是否已映射
    UINT32 ShmSize;               // 共享内存大小
    UINT32 UserReady;             // 用户态是否就绪
    UINT32 UserPid;               // 用户态进程 ID
    UINT64 CommandsProcessed;     // 已处理命令数
    UINT64 CompletionsPosted;     // 已提交完成数
    UINT64 BytesRead;             // 读取字节数
    UINT64 BytesWritten;          // 写入字节数
    UINT64 ErrorCount;            // 错误数
    UINT64 UptimeMs;              // 运行时间 (毫秒)
    UINT64 LastHeartbeatMs;       // 上次心跳时间 (毫秒)
} VNVME_GET_STATUS_OUTPUT;
```

---

### IOCTL_VNVME_MAP_SHM

映射共享内存到用户空间。

```c
#define IOCTL_VNVME_MAP_SHM CTL_CODE(FILE_DEVICE_VNVME, 0x80A, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)
```

**输入**:
```c
typedef struct _VNVME_MAP_SHM_INPUT {
    UINT32 RequestedSize;     // 请求大小 (0 = 使用默认)
    UINT32 Reserved;
} VNVME_MAP_SHM_INPUT;
```

**输出**:
```c
typedef struct _VNVME_MAP_SHM_OUTPUT {
    PVOID  UserAddress;           // 用户态映射地址
    UINT32 ActualSize;            // 实际大小
    UINT32 Reserved;
    HANDLE CommandEventHandle;    // 命令事件句柄 (可等待)
} VNVME_MAP_SHM_OUTPUT;
```

---

### IOCTL_VNVME_CREATE_CONTROLLER

创建虚拟 NVMe 控制器。

```c
#define IOCTL_VNVME_CREATE_CONTROLLER CTL_CODE(FILE_DEVICE_VNVME, 0x828, METHOD_BUFFERED, FILE_WRITE_DATA)
```

**输入**:
```c
typedef struct _VNVME_CREATE_CONTROLLER_INPUT {
    VNVME_CONTROLLER_CONFIG Config;   // 控制器配置 (参见 vnvme_common.h)
} VNVME_CREATE_CONTROLLER_INPUT;
```

**输出**:
```c
typedef struct _VNVME_CREATE_CONTROLLER_OUTPUT {
    UINT32 ControllerId;              // 分配的控制器 ID
    WCHAR DeviceInstanceId[200];      // 设备实例 ID
} VNVME_CREATE_CONTROLLER_OUTPUT;
```

---

### IOCTL_VNVME_DELETE_CONTROLLER

删除虚拟 NVMe 控制器。

```c
#define IOCTL_VNVME_DELETE_CONTROLLER CTL_CODE(FILE_DEVICE_VNVME, 0x829, METHOD_BUFFERED, FILE_WRITE_DATA)
```

**输入**:
```c
typedef struct _VNVME_DELETE_CONTROLLER_INPUT {
    UINT32 ControllerId;   // 控制器 ID
    UINT32 Force;          // 强制删除 (即使有活动 I/O)
} VNVME_DELETE_CONTROLLER_INPUT;
```

---

### IOCTL_VNVME_USER_READY

通知驱动用户态服务已就绪。

```c
#define IOCTL_VNVME_USER_READY CTL_CODE(FILE_DEVICE_VNVME, 0x814, METHOD_BUFFERED, FILE_WRITE_DATA)
```

**输入**:
```c
typedef struct _VNVME_USER_READY_INPUT {
    UINT32 UserPid;           // 用户态进程 ID
    UINT32 UserVersion;       // 用户态版本
    UINT32 Capabilities;      // 能力标志 (VNVME_USER_CAP_*)
    UINT32 Reserved;
} VNVME_USER_READY_INPUT;

#define VNVME_USER_CAP_ASYNC      0x0001  // 支持异步处理
#define VNVME_USER_CAP_BATCH      0x0002  // 支持批处理
#define VNVME_USER_CAP_DIRECT_IO  0x0004  // 支持直接 I/O
```

**输出**: 无

调用此 IOCTL 后，驱动开始将命令转发到用户态处理。

---

### IOCTL_VNVME_HEARTBEAT

发送心跳保持连接活跃。

```c
#define IOCTL_VNVME_HEARTBEAT CTL_CODE(FILE_DEVICE_VNVME, 0x816, METHOD_BUFFERED, FILE_WRITE_DATA)
```

**输入**:
```c
typedef struct _VNVME_HEARTBEAT_INPUT {
    UINT64 Timestamp;             // 时间戳
    UINT64 CommandsProcessed;     // 已处理命令数
} VNVME_HEARTBEAT_INPUT;
```

**输出**:
```c
typedef struct _VNVME_HEARTBEAT_OUTPUT {
    UINT64 KernelTimestamp;       // 内核时间戳
    UINT32 PendingCommands;       // 待处理命令数
    UINT32 Reserved;
} VNVME_HEARTBEAT_OUTPUT;
```

用户态服务应定期 (每 1 秒) 发送心跳。如果 10 秒内未收到心跳，驱动将认为用户态已崩溃并切换到内核模式处理。

---

### IOCTL_VNVME_GET_COMMAND_EVENT

获取命令就绪事件句柄。

```c
#define IOCTL_VNVME_GET_COMMAND_EVENT CTL_CODE(FILE_DEVICE_VNVME, 0x81E, METHOD_BUFFERED, FILE_READ_DATA)
```

**输入**: 无

**输出**:
```c
typedef struct _VNVME_GET_COMMAND_EVENT_OUTPUT {
    HANDLE EventHandle;  // 用户态可等待的事件句柄
} VNVME_GET_COMMAND_EVENT_OUTPUT;
```

用户态可以使用 `WaitForSingleObject()` 等待此事件，当有新命令到达时事件会被触发。

---

### IOCTL_VNVME_SUBMIT_COMPLETIONS

通知驱动已写入完成条目。

```c
#define IOCTL_VNVME_SUBMIT_COMPLETIONS CTL_CODE(FILE_DEVICE_VNVME, 0x81F, METHOD_BUFFERED, FILE_WRITE_DATA)
```

**输入**:
```c
typedef struct _VNVME_SUBMIT_COMPLETIONS_INPUT {
    UINT32 CompletionCount;     // 完成数量
    UINT32 ControllerId;        // 目标控制器 ID (0 = 广播到所有控制器)
} VNVME_SUBMIT_COMPLETIONS_INPUT;
```

---

### IOCTL_VNVME_GET_STATS

获取性能统计信息。

```c
#define IOCTL_VNVME_GET_STATS CTL_CODE(FILE_DEVICE_VNVME, 0x86E, METHOD_BUFFERED, FILE_READ_DATA)
```

**输入**:
```c
typedef struct _VNVME_GET_STATS_INPUT {
    UINT32 ControllerId;        // 控制器 ID (0 = 所有控制器)
    UINT32 Flags;               // 保留
} VNVME_GET_STATS_INPUT;
```

**输出**:
```c
typedef struct _VNVME_GET_STATS_OUTPUT {
    UINT32 ControllerCount;         // 返回的控制器数
    UINT32 TotalNamespaceCount;     // 总命名空间数
    UINT64 Uptime;                  // 驱动运行时间 (毫秒)
    UINT64 TotalCommandsProcessed;  // 总命令数
    VNVME_CONTROLLER_STATS Controllers[8];
    VNVME_NAMESPACE_STATS Namespaces[16];
} VNVME_GET_STATS_OUTPUT;
```

---

## 共享内存结构

### 内存布局

```
+------------------+ 0x0000
|  Control Block   | 4KB
+------------------+ 0x1000
|   Notify Ring    | 4KB
+------------------+ 0x2000
|    Admin SQ      | 4KB (64 entries × 64 bytes)
+------------------+ 0x3000
|    Admin CQ      | 4KB (64 entries × 16 bytes)
+------------------+ 0x4000
| I/O Queue Descs  | 4KB
+------------------+ 0x5000
|    I/O Queues    | 可变
+------------------+
|   Data Buffer    | 剩余空间
+------------------+ End (默认 64MB)
```

### Control Block

```c
typedef struct _VNVME_SHM_CONTROL_BLOCK {
    // 标识 (0x00)
    UINT32 Magic;               // 魔数 0x454D564E ("NVME")
    UINT32 Version;             // 版本号
    UINT32 TotalSize;           // 总大小
    UINT32 ControlBlockSize;    // 控制块大小
    
    // Admin 队列描述符 (0x10)
    VNVME_QUEUE_DESCRIPTOR AdminSQ;
    VNVME_QUEUE_DESCRIPTOR AdminCQ;
    
    // I/O 队列配置 (0x50)
    UINT32 IoQueueCount;        // 当前 I/O 队列数量
    UINT32 MaxIoQueues;         // 最大 I/O 队列数量
    UINT32 IoQueueDescriptorOffset;
    UINT32 Reserved1;
    
    // 通知环 (0x60)
    UINT32 NotifyRingOffset;
    UINT32 NotifyRingSize;
    
    // 数据缓冲区 (0x68)
    UINT32 DataBufferOffset;
    UINT32 DataBufferSize;
    
    // 状态 (0x70)
    UINT32 Flags;
    UINT32 ControllerState;
    UINT32 KernelReady;         // 内核就绪标志
    UINT32 UserReady;           // 用户态就绪标志
    UINT32 ShutdownRequested;   // 关闭请求
    UINT32 ErrorCode;
    
    // 时间戳
    UINT64 LastHeartbeat;       // 最后心跳时间戳
    
    // 统计 (0x90)
    UINT64 CommandsProcessed;
    UINT64 CompletionsPosted;
    UINT64 BytesRead;
    UINT64 BytesWritten;
} VNVME_SHM_CONTROL_BLOCK;
```

### Queue Descriptor

```c
typedef struct _VNVME_QUEUE_DESCRIPTOR {
    UINT32 Offset;      // 队列在共享内存中的偏移
    UINT32 EntrySize;   // 条目大小 (SQ=64, CQ=16)
    UINT32 Capacity;    // 队列容量 (条目数)
    UINT32 Valid;       // 队列是否有效
    UINT32 Head;        // 头指针
    UINT32 Tail;        // 尾指针
    UINT32 Phase;       // 相位位 (CQ 使用)
    UINT32 Reserved;
} VNVME_QUEUE_DESCRIPTOR;
```

### Notify Ring

用于内核通知用户态有新命令到达：

```c
typedef struct _VNVME_NOTIFY_ENTRY {
    UINT8  Type;        // 0=SQ doorbell, 1=CQ doorbell
    UINT8  Reserved;
    UINT16 QueueId;     // 队列 ID
    UINT32 Index;       // 新的 Tail (SQ) 或 Head (CQ)
} VNVME_NOTIFY_ENTRY;

typedef struct _VNVME_NOTIFY_RING {
    volatile UINT32 Head;    // 用户态读取位置
    volatile UINT32 Tail;    // 内核写入位置
    UINT32 Size;             // 环大小
    UINT32 Reserved;
    VNVME_NOTIFY_ENTRY Entries[VNVME_NOTIFY_RING_SIZE];
} VNVME_NOTIFY_RING;
```

---

## NVMe 命令处理

### 命令流程

```
1. stornvme 写入命令到 Admin/IO SQ
2. stornvme 写入 SQ Doorbell (Tail)
3. 驱动拦截 doorbell 写入
4. 驱动将通知写入 Notify Ring
5. 驱动触发 CommandReadyEvent
6. 用户态通过 WaitForSingleObject 唤醒
7. 用户态读取 Notify Ring
8. 用户态处理 SQ 中的命令
9. 用户态写入完成到 CQ
10. 用户态调用 IOCTL_VNVME_SUBMIT_COMPLETIONS
11. 驱动触发 MSI-X 中断通知 stornvme
```

### 支持的 Admin 命令

| Opcode | 命令 | 描述 |
|--------|------|------|
| 0x00 | Delete I/O SQ | 删除 I/O 提交队列 |
| 0x01 | Create I/O SQ | 创建 I/O 提交队列 |
| 0x04 | Delete I/O CQ | 删除 I/O 完成队列 |
| 0x05 | Create I/O CQ | 创建 I/O 完成队列 |
| 0x06 | Identify | 获取控制器/命名空间信息 |
| 0x09 | Set Features | 设置控制器特性 |
| 0x0A | Get Features | 获取控制器特性 |
| 0x80 | Format NVM | 格式化命名空间 |

### 支持的 I/O 命令

| Opcode | 命令 | 描述 |
|--------|------|------|
| 0x00 | Flush | 刷新数据到持久存储 |
| 0x01 | Write | 写入数据 |
| 0x02 | Read | 读取数据 |
| 0x08 | Write Zeroes | 写入零 |
| 0x09 | Dataset Management | 数据集管理 (TRIM) |

---

## 用户态服务 API

### 驱动通信

```c
// 连接到驱动
BOOL DriverConnect(PDRIVER_COMM_CONTEXT pCtx);

// 断开连接
void DriverDisconnect(PDRIVER_COMM_CONTEXT pCtx);

// 映射共享内存
BOOL DriverMapShm(PDRIVER_COMM_CONTEXT pCtx);

// 取消映射共享内存
void DriverUnmapShm(PDRIVER_COMM_CONTEXT pCtx);

// 发送用户就绪通知
BOOL DriverSendUserReady(PDRIVER_COMM_CONTEXT pCtx);

// 发送心跳
BOOL DriverSendHeartbeat(PDRIVER_COMM_CONTEXT pCtx);

// 获取命令事件
BOOL DriverGetCommandEvent(PDRIVER_COMM_CONTEXT pCtx);

// 等待命令
BOOL DriverWaitForCommand(PDRIVER_COMM_CONTEXT pCtx, DWORD timeoutMs);
```

### 命令引擎

```c
// 创建命令引擎
PCMD_ENGINE_CONTEXT CmdEngineCreate(void);

// 销毁命令引擎
void CmdEngineDestroy(PCMD_ENGINE_CONTEXT pCtx);

// 初始化
BOOL CmdEngineInit(PCMD_ENGINE_CONTEXT pCtx, const CMD_ENGINE_CONFIG* pConfig);

// 轮询处理命令
UINT64 CmdEnginePoll(PCMD_ENGINE_CONTEXT pCtx);
```

### 存储后端

```c
// 创建后端
PBACKEND_CONTEXT BackendCreate(const BACKEND_CONFIG* pConfig);

// 销毁后端
void BackendDestroy(PBACKEND_CONTEXT pCtx);

// 读取数据
BOOL BackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size);

// 写入数据
BOOL BackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size);

// 刷新
BOOL BackendFlush(PBACKEND_CONTEXT pCtx);
```

---

## 错误码

### NTSTATUS 返回码

| 代码 | 含义 |
|------|------|
| STATUS_SUCCESS (0) | 成功 |
| STATUS_INVALID_PARAMETER | 参数无效 |
| STATUS_INSUFFICIENT_RESOURCES | 资源不足 |
| STATUS_DEVICE_NOT_READY | 设备未就绪 |
| STATUS_OBJECT_NAME_COLLISION | 控制器 ID 重复 |
| STATUS_BUFFER_TOO_SMALL | 缓冲区太小 |

### Win32 错误码

| 代码 | GetLastError() | 含义 |
|------|----------------|------|
| 2 | ERROR_FILE_NOT_FOUND | 驱动设备未找到 |
| 5 | ERROR_ACCESS_DENIED | 权限不足 |
| 87 | ERROR_INVALID_PARAMETER | 参数无效 |
| 1450 | ERROR_NO_SYSTEM_RESOURCES | 系统资源不足 |
