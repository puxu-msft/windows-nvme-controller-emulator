# vnvme-server 用户态服务

本文档详细描述 vnvme-server.exe 的实现细节，包括架构设计、模块划分、核心算法和配置。

---

## 何时需要 vnvme-server？

> **重要**: vnvme-server.exe 仅在 **用户态命令模式** 下需要。

本项目采用**双模式架构**，通过 `VNVME_DEFAULT_CMD_MODE` 宏切换：

| 模式 | 宏值 | vnvme-server | 使用场景 |
|------|------|--------------|----------|
| **内核命令模式** | `VNVME_CMD_MODE_KERNEL` | ❌ 不需要 | 生产环境、性能优先 |
| **用户态命令模式** | `VNVME_CMD_MODE_USER` | ✅ 必须运行 | 开发调试、功能扩展 |

### 模式对比

```
【内核命令模式】                        【用户态命令模式】
                                        
stornvme.sys                            stornvme.sys
     │                                       │
     ▼                                       ▼
vnvme.sys                               vnvme.sys
┌────────────────┐                      ┌────────────────┐
│ admin_cmd.c    │                      │ user_forward.c │──┐
│ io_cmd.c       │                      │ (转发到 SHM)   │  │
│ storage.c      │                      └────────────────┘  │
│ (内核态后端)    │                                          │
└────────────────┘                      ┌────────────────┐  │
                                        │ vnvme-server   │◀─┘
❌ 无用户态服务                          │  .exe          │
                                        │ ┌────────────┐ │
优点:                                   │ │ command_   │ │
• 最低延迟 (~1μs)                       │ │ processor  │ │
• 无进程依赖                            │ │ backend    │ │
• 系统级稳定性                          │ └────────────┘ │
                                        └────────────────┘
缺点:                                   
• 内核开发复杂                          优点:
• 后端功能受限                          • 灵活性高 (丰富后端)
• 调试困难                              • 调试方便 (用户态)
• 蓝屏风险                              • 崩溃不影响系统
```

### 运行配置

```powershell
# 内核命令模式 - 直接加载驱动即可
pnputil /add-driver vnvme.inf /install

# 用户态命令模式 - 需要同时运行服务
pnputil /add-driver vnvme.inf /install
vnvme-server.exe --config vnvme.conf   # 必须运行！
```

---

## 实现状态

> **✅ 已实现**: 用户态服务核心功能 (v1 - 紧凑版)
> 
> 当前实现 (3 文件, ~2000 行):
> - `main.c` - 程序入口、IOCTL 通信、主循环、心跳、配置
> - `command_processor.c` - NVMe 命令处理器 (Admin + I/O)
> - `backend.c` - 存储后端 (内存 + 文件)
>
> **🔄 模块化重构中** (v2): 基础设施模块已完成，待集成启用。

## 项目结构

### 当前实现 (v1 - 紧凑版, 正在使用)

```
vnvme-server/
├── main.c                  # 入口、配置、驱动通信、主循环 (621行)
├── command_processor.c     # NVMe 命令处理 Admin + I/O (943行)
├── backend.c               # 存储后端 内存 + 文件 (403行)
└── vnvme.conf.example      # 示例配置文件
```

### 模块化版本 (v2 - 已创建, 待启用)

```
vnvme-server/
├── types.h                 # ✅ 基础类型定义 (避免循环依赖)
├── vnvme_server.h          # ✅ 公共头文件、共享类型
├── logger.h                # ✅ 日志接口
├── logger.c                # ✅ 日志系统实现 (~300行)
├── config.h                # ✅ 配置结构定义
├── config.c                # ✅ 配置解析 (~430行)
├── driver_comm.h           # ✅ 驱动通信接口
├── driver_comm.c           # ✅ 驱动通信实现 (~370行)
├── backend.h               # ✅ 后端接口定义
├── backend_common.c        # ✅ 后端分发器 (~200行)
├── backend_memory.c        # ✅ 内存后端 (~210行)
├── backend_file.c          # ✅ 文件后端 (~320行)
├── main_v2.c               # ✅ 模块化入口 (待测试)
│
├── main.c                  # v1 入口 (当前使用)
├── command_processor.c     # v1 命令处理 (两版本共用)
└── backend.c               # v1 后端 (当前使用)
```

### 待完成模块

```
vnvme-server/
├── admin_commands.c        # TODO: 从 command_processor.c 分离
├── io_commands.c           # TODO: 从 command_processor.c 分离
└── command_processor.h     # TODO: 命令处理接口
```

### 模块职责

| 模块 | 职责 | 依赖 |
|------|------|------|
| main.c | 程序入口、生命周期管理 | 所有模块 |
| config.c | 配置文件解析、命令行解析 | logger |
| driver_comm.c | 驱动连接、共享内存、心跳 | logger |
| command_processor.c | 命令分发、NotifyRing 处理 | admin/io_commands, logger |
| admin_commands.c | Admin 命令实现 | backend, logger |
| io_commands.c | I/O 命令实现 | backend, logger |
| backend.c | 后端抽象、后端选择 | backend_memory/file, logger |
| backend_memory.c | 内存后端实现 | logger |
| backend_file.c | 文件后端实现 | logger |
| logger.c | 日志输出、级别控制 | 无 |

---

## 概述

```
┌────────────────────────────────────────────────────────────────────────┐
│                        vnvme-server.exe                                 │
│                                                                         │
│  ┌───────────┐  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │  Main     │  │ Command      │  │   Admin      │  │    I/O       │  │
│  │  Module   │─▶│ Processor    │─▶│  Commands    │  │   Commands   │  │
│  └───────────┘  └──────────────┘  └──────────────┘  └──────────────┘  │
│        │               │                  │                 │          │
│        ▼               ▼                  ▼                 ▼          │
│  ┌───────────┐  ┌──────────────┐  ┌─────────────────────────────────┐ │
│  │  Config   │  │ Shared Memory│  │          Backend Layer          │ │
│  │  Parser   │  │   + Notify   │  │  ┌────────┐  ┌────────────────┐ │ │
│  └───────────┘  └──────────────┘  │  │ Memory │  │     File       │ │ │
│                                    │  └────────┘  └────────────────┘ │ │
│                                    └─────────────────────────────────┘ │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 项目结构

```
vnvme-server/
├── main.c                  # ✅ 入口、初始化、主循环、心跳、配置加载 (621行)
├── command_processor.c     # ✅ NVMe 命令处理 Admin + I/O (943行)
├── backend.c               # ✅ 后端抽象接口 内存 + 文件 (403行)
├── vnvme.conf.example      # ✅ 示例配置文件
└── build/                  # 构建输出目录
```

### 已实现功能概览

| 模块 | 功能 | 状态 |
|------|------|------|
| main.c | 命令行参数解析 | ✅ |
| main.c | 配置文件加载 (--config) | ✅ |
| main.c | 大小后缀解析 (K/M/G) | ✅ |
| main.c | 驱动连接 | ✅ |
| main.c | 共享内存映射 | ✅ |
| main.c | 心跳发送 | ✅ |
| main.c | 优雅关闭检测 | ✅ |
| command_processor.c | Admin 命令处理 | ✅ |
| command_processor.c | I/O 命令处理 | ✅ |
| command_processor.c | NotifyRing 轮询 | ✅ |
| backend.c | 内存后端 | ✅ |
| backend.c | 文件后端 | ✅ |

---

## 模块详细设计

### 1. 主模块 (main.c)

```c
// main.c - 程序入口和生命周期管理

int main(int argc, char* argv[])
{
    // 1. 解析命令行参数
    VNVME_CONFIG config = {0};
    if (!ParseCommandLine(argc, argv, &config)) {
        return 1;
    }
    
    // 2. 加载配置文件 (如果指定)
    if (config.ConfigFilePath[0]) {
        if (!LoadConfigFile(config.ConfigFilePath, &config)) {
            LogError("Failed to load config file");
            return 1;
        }
    }
    
    // 3. 初始化日志
    InitLogger(&config.LogConfig);
    LogInfo("vnvme-server starting...");
    
    // 4. 打开内核驱动
    HANDLE hDevice = OpenKernelDriver();
    if (hDevice == INVALID_HANDLE_VALUE) {
        LogError("Failed to open kernel driver: %d", GetLastError());
        return 1;
    }
    
    // 5. 映射共享内存
    PVOID pSharedMemory = NULL;
    HANDLE hCommandEvent = NULL;
    if (!MapSharedMemory(hDevice, &pSharedMemory, &hCommandEvent)) {
        LogError("Failed to map shared memory");
        CloseHandle(hDevice);
        return 1;
    }
    
    // 6. 初始化后端
    if (!InitBackend(&config)) {
        LogError("Failed to initialize backend");
        goto cleanup;
    }
    
    // 7. 通知内核我们已就绪
    if (!SendUserReady(hDevice)) {
        LogError("Failed to send USER_READY");
        goto cleanup;
    }
    
    // 8. 启动心跳线程
    HANDLE hHeartbeatThread = StartHeartbeatThread(hDevice);
    
    // 9. 进入命令处理循环
    LogInfo("Entering main command loop");
    CommandLoop(pSharedMemory, hCommandEvent);
    
    // 10. 清理
cleanup:
    StopHeartbeatThread(hHeartbeatThread);
    UnmapSharedMemory(pSharedMemory);
    CloseHandle(hDevice);
    LogInfo("vnvme-server stopped");
    return 0;
}
```

#### 命令行参数

```
用法: vnvme-server.exe [选项]

选项:
  --config <path>       配置文件路径
  --backend <type>      存储后端类型 (memory|file)
  --size <size>         磁盘大小 (例: 10GB, 512MB)
  --file <path>         后端文件路径 (file 后端)
  --log-level <level>   日志级别 (error|warn|info|debug|trace)
  --log-file <path>     日志文件路径
  --help                显示帮助
  --version             显示版本
```

---

### 2. 配置管理 (config.c)

```c
// config.h - 配置结构

typedef struct _VNVME_CONFIG {
    // 基本配置
    char ConfigFilePath[MAX_PATH];
    
    // 控制器配置
    VNVME_CONTROLLER_CONFIG Controller;
    
    // 后端配置
    VNVME_BACKEND_TYPE BackendType;
    union {
        VNVME_MEMORY_BACKEND_CONFIG Memory;
        VNVME_FILE_BACKEND_CONFIG File;
    } Backend;
    
    // 日志配置
    VNVME_LOG_CONFIG LogConfig;
    
    // 性能配置
    VNVME_PERF_CONFIG Perf;
} VNVME_CONFIG;

typedef struct _VNVME_CONTROLLER_CONFIG {
    char SerialNumber[20];       // 序列号
    char ModelNumber[40];        // 型号
    char FirmwareRevision[8];    // 固件版本
    UINT32 MaxNamespaces;        // 最大命名空间数
    UINT32 MaxQueuePairs;        // 最大队列对数
    UINT32 MaxQueueDepth;        // 最大队列深度
} VNVME_CONTROLLER_CONFIG;

typedef struct _VNVME_FILE_BACKEND_CONFIG {
    char FilePath[MAX_PATH];     // 存储文件路径
    UINT64 Size;                 // 大小 (字节)
    BOOL Preallocate;            // 是否预分配
    BOOL DirectIO;               // 是否使用直接 I/O
} VNVME_FILE_BACKEND_CONFIG;
```

#### 配置文件格式 (INI)

```ini
; vnvme.conf - vnvme-server 配置文件

[controller]
serial_number = VNVME001
model_number = Virtual NVMe SSD
firmware_revision = 1.0
max_namespaces = 16
max_queue_pairs = 32
max_queue_depth = 1024

[backend]
type = file
file_path = C:\vnvme\disk.img
size = 10737418240    ; 10 GB
preallocate = false
direct_io = true

[namespace.1]
enabled = true
size = 10737418240
block_size = 512

[logging]
level = info
file = C:\vnvme\vnvme-server.log
max_size = 10485760   ; 10 MB
max_files = 5

[performance]
batch_size = 16
completion_coalescing = true
```

---

### 3. 内核通信 (kernel_comm.c)

```c
// kernel_comm.h - 内核通信接口

// 打开控制设备
HANDLE OpenKernelDriver(void)
{
    return CreateFileW(
        L"\\\\.\\VNVMEControl",
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );
}

// 映射共享内存
BOOL MapSharedMemory(
    HANDLE hDevice,
    PVOID* ppSharedMemory,
    HANDLE* phCommandEvent)
{
    VNVME_MAP_SHARED_MEMORY_INPUT input = {0};
    VNVME_MAP_SHARED_MEMORY_OUTPUT output = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_MAP_SHARED_MEMORY,
        &input, sizeof(input),
        &output, sizeof(output),
        &bytesReturned,
        NULL
    );
    
    if (result) {
        *ppSharedMemory = output.UserAddress;
        *phCommandEvent = output.CommandEventHandle;
    }
    
    return result;
}

// 发送心跳
BOOL SendHeartbeat(HANDLE hDevice)
{
    DWORD bytesReturned;
    return DeviceIoControl(
        hDevice,
        IOCTL_VNVME_HEARTBEAT,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL
    );
}

// 提交完成
BOOL SubmitCompletions(HANDLE hDevice, UINT32 count)
{
    VNVME_SUBMIT_COMPLETIONS_INPUT input = { .CompletionCount = count };
    DWORD bytesReturned;
    
    return DeviceIoControl(
        hDevice,
        IOCTL_VNVME_SUBMIT_COMPLETIONS,
        &input, sizeof(input),
        NULL, 0,
        &bytesReturned,
        NULL
    );
}
```

---

### 4. 命令引擎 (command_engine.c)

> **v2 零复制架构**
> 
> 用户态直接访问原始 NVMe 命令，无需通过中间结构：
> 
> ```c
> #include "nvme_spec.h"          // NVME_COMMAND, NVME_COMPLETION
> #include "vnvme_common.h"       // 控制块、队列描述符
> 
> // 获取 Admin SQ 指针
> PNVME_COMMAND adminSQ = (PNVME_COMMAND)VnvmeGetAdminSQ(pSharedMemory);
> 
> // 直接读取原始 NVMe 命令
> PNVME_COMMAND cmd = &adminSQ[head];
> printf("Opcode: 0x%02X, NSID: %u, CID: %u\n",
>        cmd->Opcode, cmd->NSID, cmd->CID);
> 
> // 获取 Admin CQ 指针并写入完成项
> PNVME_COMPLETION adminCQ = (PNVME_COMPLETION)VnvmeGetAdminCQ(pSharedMemory);
> adminCQ[cq_tail].CID = cmd->CID;
> adminCQ[cq_tail].SQID = 0;       // Admin Queue
> adminCQ[cq_tail].StatusField = NVME_STATUS_SUCCESS;
> adminCQ[cq_tail].PhaseTag = phase;
> ```
> 
> 主要变化：
> - `VNVME_SUBMISSION_RING_ENTRY` (80字节) → `NVME_COMMAND` (64字节)
> - 无命令复制开销，直接操作 NVMe 规范定义的结构
> - 通过 `VNVME_NOTIFY_RING` 获取 Doorbell 变更通知

```c
// command_engine.c - 主命令处理循环 (v2 零复制架构)

typedef struct _COMMAND_ENGINE_CONTEXT {
    PSHARED_MEMORY_CONTROL_BLOCK pControlBlock;
    HANDLE hCommandEvent;
    BOOL Running;
    
    // 统计
    UINT64 CommandsProcessed;
    UINT64 CommandsFailed;
} COMMAND_ENGINE_CONTEXT;

void CommandLoop(PVOID pSharedMemory, HANDLE hCommandEvent)
{
    COMMAND_ENGINE_CONTEXT ctx = {0};
    ctx.pControlBlock = (PSHARED_MEMORY_CONTROL_BLOCK)pSharedMemory;
    ctx.hCommandEvent = hCommandEvent;
    ctx.Running = TRUE;
    
    while (ctx.Running) {
        // 1. 等待命令事件
        DWORD waitResult = WaitForSingleObject(hCommandEvent, 100);
        
        if (waitResult == WAIT_TIMEOUT) {
            // 超时，检查是否应该退出
            continue;
        }
        
        if (waitResult != WAIT_OBJECT_0) {
            LogError("Wait failed: %d", GetLastError());
            break;
        }
        
        // 2. 处理命令批次
        ProcessCommandBatch(&ctx);
        
        // 3. 提交完成
        FlushCompletions(&ctx);
    }
}

void ProcessCommandBatch(PCOMMAND_ENGINE_CONTEXT pCtx)
{
    PVNVME_SUBMISSION_RING pSubRing = GetSubmissionRing(pCtx->pControlBlock);
    
    // 读取头指针
    UINT32 head = pSubRing->Head;
    UINT32 tail = pSubRing->Tail;
    
    while (head != tail) {
        PVNVME_SUBMISSION_RING_ENTRY pEntry = &pSubRing->Entries[head];
        
        // 分发命令
        DispatchCommand(pCtx, pEntry);
        
        // 移动头指针
        head = (head + 1) % VNVME_SUBMISSION_RING_SIZE;
        pCtx->CommandsProcessed++;
    }
    
    // 更新头指针 (原子操作)
    MemoryBarrier();
    pSubRing->Head = head;
}

void DispatchCommand(PCOMMAND_ENGINE_CONTEXT pCtx, PVNVME_SUBMISSION_RING_ENTRY pEntry)
{
    NVME_COMPLETION_ENTRY completion = {0};
    
    switch (pEntry->Type) {
        case CMD_TYPE_ADMIN:
            ProcessAdminCommand(pCtx, pEntry, &completion);
            break;
            
        case CMD_TYPE_IO_READ:
            ProcessIoRead(pCtx, pEntry, &completion);
            break;
            
        case CMD_TYPE_IO_WRITE:
            ProcessIoWrite(pCtx, pEntry, &completion);
            break;
            
        case CMD_TYPE_IO_FLUSH:
            ProcessIoFlush(pCtx, pEntry, &completion);
            break;
            
        case CMD_TYPE_CONTROLLER_ENABLE:
            ProcessControllerEnable(pCtx, pEntry, &completion);
            break;
            
        default:
            LogWarn("Unknown command type: %d", pEntry->Type);
            completion.Status = NVME_STATUS_INVALID_OPCODE;
            break;
    }
    
    // 添加到完成环
    AddCompletion(pCtx, pEntry->CommandId, pEntry->QueueId, &completion);
}
```

---

### 5. Admin 命令处理 (admin_commands.c)

```c
// admin_commands.c - Admin 命令实现

void ProcessAdminCommand(
    PCOMMAND_ENGINE_CONTEXT pCtx,
    PVNVME_SUBMISSION_RING_ENTRY pEntry,
    PNVME_COMPLETION_ENTRY pCompletion)
{
    switch (pEntry->Opcode) {
        case NVME_ADMIN_IDENTIFY:
            ProcessIdentify(pCtx, pEntry, pCompletion);
            break;
            
        case NVME_ADMIN_CREATE_IO_CQ:
            ProcessCreateIoCq(pCtx, pEntry, pCompletion);
            break;
            
        case NVME_ADMIN_CREATE_IO_SQ:
            ProcessCreateIoSq(pCtx, pEntry, pCompletion);
            break;
            
        case NVME_ADMIN_DELETE_IO_CQ:
            ProcessDeleteIoCq(pCtx, pEntry, pCompletion);
            break;
            
        case NVME_ADMIN_DELETE_IO_SQ:
            ProcessDeleteIoSq(pCtx, pEntry, pCompletion);
            break;
            
        case NVME_ADMIN_SET_FEATURES:
            ProcessSetFeatures(pCtx, pEntry, pCompletion);
            break;
            
        case NVME_ADMIN_GET_FEATURES:
            ProcessGetFeatures(pCtx, pEntry, pCompletion);
            break;
            
        default:
            LogWarn("Unsupported admin opcode: 0x%02x", pEntry->Opcode);
            pCompletion->Status = NVME_STATUS_INVALID_OPCODE;
            break;
    }
}

void ProcessIdentify(
    PCOMMAND_ENGINE_CONTEXT pCtx,
    PVNVME_SUBMISSION_RING_ENTRY pEntry,
    PNVME_COMPLETION_ENTRY pCompletion)
{
    UINT8 cns = pEntry->Admin.CDW10 & 0xFF;
    UINT32 nsid = pEntry->NSID;
    
    // 获取数据缓冲区 (从共享内存)
    PVOID pDataBuffer = GetDataBuffer(pCtx, pEntry->DataBufferOffset);
    
    switch (cns) {
        case 0x00:  // Namespace
            BuildIdentifyNamespace(nsid, pDataBuffer);
            break;
            
        case 0x01:  // Controller
            BuildIdentifyController(pDataBuffer);
            break;
            
        case 0x02:  // Active Namespace List
            BuildActiveNamespaceList(pDataBuffer);
            break;
            
        default:
            pCompletion->Status = NVME_STATUS_INVALID_FIELD;
            return;
    }
    
    pCompletion->Status = NVME_STATUS_SUCCESS;
}

void BuildIdentifyController(PVOID pBuffer)
{
    PNVME_IDENTIFY_CONTROLLER_DATA pIdCtrl = 
        (PNVME_IDENTIFY_CONTROLLER_DATA)pBuffer;
    
    RtlZeroMemory(pIdCtrl, 4096);
    
    // 厂商 ID
    pIdCtrl->VID = 0x1B36;  // Red Hat QEMU
    pIdCtrl->SSVID = 0x1B36;
    
    // 序列号和型号
    CopyPaddedString(pIdCtrl->SN, sizeof(pIdCtrl->SN), 
                     g_Config.Controller.SerialNumber);
    CopyPaddedString(pIdCtrl->MN, sizeof(pIdCtrl->MN),
                     g_Config.Controller.ModelNumber);
    CopyPaddedString(pIdCtrl->FR, sizeof(pIdCtrl->FR),
                     g_Config.Controller.FirmwareRevision);
    
    // 能力
    pIdCtrl->MDTS = 5;       // 最大传输大小 = 2^5 * 4KB = 128KB
    pIdCtrl->CNTLID = 1;     // 控制器 ID
    pIdCtrl->VER = 0x00010400; // NVMe 1.4
    
    // Optional Admin Command Support
    pIdCtrl->OACS = 0;       // 不支持可选 Admin 命令
    
    // 错误日志页数
    pIdCtrl->ELPE = 63;
    
    // 队列数量
    pIdCtrl->SQES = (6 << 4) | 6;  // SQ entry: 64 bytes
    pIdCtrl->CQES = (4 << 4) | 4;  // CQ entry: 16 bytes
    pIdCtrl->NN = g_Config.Controller.MaxNamespaces;
    
    // NVMe over Fabrics
    pIdCtrl->NVMEOF = 0;
    
    // 电源状态
    pIdCtrl->NPSS = 0;       // 1 个电源状态
    pIdCtrl->PSD[0].MP = 25; // 25 瓦
}
```

---

### 6. I/O 命令处理 (io_commands.c)

```c
// io_commands.c - I/O 命令实现

void ProcessIoRead(
    PCOMMAND_ENGINE_CONTEXT pCtx,
    PVNVME_SUBMISSION_RING_ENTRY pEntry,
    PNVME_COMPLETION_ENTRY pCompletion)
{
    UINT32 nsid = pEntry->NSID;
    UINT64 slba = pEntry->IO.StartLBA;
    UINT32 nlb = (pEntry->IO.CDW12 & 0xFFFF) + 1;  // NLB 从 0 开始
    
    // 检查命名空间
    PNAMESPACE pNs = GetNamespace(nsid);
    if (!pNs) {
        pCompletion->Status = NVME_STATUS_INVALID_NAMESPACE;
        return;
    }
    
    // 检查 LBA 范围
    UINT64 endLba = slba + nlb;
    if (endLba > pNs->TotalBlocks) {
        pCompletion->Status = NVME_STATUS_LBA_OUT_OF_RANGE;
        return;
    }
    
    // 计算字节偏移和大小
    UINT64 byteOffset = slba * pNs->BlockSize;
    UINT32 byteLength = nlb * pNs->BlockSize;
    
    // 获取数据缓冲区
    PVOID pDataBuffer = GetDataBuffer(pCtx, pEntry->DataBufferOffset);
    
    // 调用后端读取
    NTSTATUS status = g_Backend.Read(
        pNs->BackendContext,
        byteOffset,
        pDataBuffer,
        byteLength
    );
    
    if (NT_SUCCESS(status)) {
        pCompletion->Status = NVME_STATUS_SUCCESS;
    } else {
        LogError("Backend read failed: 0x%08x", status);
        pCompletion->Status = NVME_STATUS_MEDIA_ERROR;
    }
}

void ProcessIoWrite(
    PCOMMAND_ENGINE_CONTEXT pCtx,
    PVNVME_SUBMISSION_RING_ENTRY pEntry,
    PNVME_COMPLETION_ENTRY pCompletion)
{
    UINT32 nsid = pEntry->NSID;
    UINT64 slba = pEntry->IO.StartLBA;
    UINT32 nlb = (pEntry->IO.CDW12 & 0xFFFF) + 1;
    
    // 检查命名空间
    PNAMESPACE pNs = GetNamespace(nsid);
    if (!pNs) {
        pCompletion->Status = NVME_STATUS_INVALID_NAMESPACE;
        return;
    }
    
    // 检查只读
    if (pNs->ReadOnly) {
        pCompletion->Status = NVME_STATUS_WRITE_FAULT;
        return;
    }
    
    // 检查 LBA 范围
    UINT64 endLba = slba + nlb;
    if (endLba > pNs->TotalBlocks) {
        pCompletion->Status = NVME_STATUS_LBA_OUT_OF_RANGE;
        return;
    }
    
    // 计算字节偏移和大小
    UINT64 byteOffset = slba * pNs->BlockSize;
    UINT32 byteLength = nlb * pNs->BlockSize;
    
    // 获取数据缓冲区
    PVOID pDataBuffer = GetDataBuffer(pCtx, pEntry->DataBufferOffset);
    
    // 调用后端写入
    NTSTATUS status = g_Backend.Write(
        pNs->BackendContext,
        byteOffset,
        pDataBuffer,
        byteLength
    );
    
    if (NT_SUCCESS(status)) {
        pCompletion->Status = NVME_STATUS_SUCCESS;
    } else {
        LogError("Backend write failed: 0x%08x", status);
        pCompletion->Status = NVME_STATUS_MEDIA_ERROR;
    }
}
```

---

### 7. 后端抽象 (backend.c)

```c
// backend.h - 后端接口定义

typedef enum _VNVME_BACKEND_TYPE {
    VNVME_BACKEND_MEMORY,
    VNVME_BACKEND_FILE
} VNVME_BACKEND_TYPE;

typedef struct _VNVME_BACKEND_OPS {
    NTSTATUS (*Init)(PVOID pConfig, PVOID* ppContext);
    NTSTATUS (*Read)(PVOID pContext, UINT64 offset, PVOID buffer, UINT32 length);
    NTSTATUS (*Write)(PVOID pContext, UINT64 offset, PVOID buffer, UINT32 length);
    NTSTATUS (*Flush)(PVOID pContext);
    VOID (*Close)(PVOID pContext);
    
    UINT64 (*GetCapacity)(PVOID pContext);
    UINT32 (*GetBlockSize)(PVOID pContext);
} VNVME_BACKEND_OPS;

// backend.c - 后端管理

static VNVME_BACKEND_OPS g_MemoryBackendOps = {
    .Init = MemoryBackendInit,
    .Read = MemoryBackendRead,
    .Write = MemoryBackendWrite,
    .Flush = MemoryBackendFlush,
    .Close = MemoryBackendClose,
    .GetCapacity = MemoryBackendGetCapacity,
    .GetBlockSize = MemoryBackendGetBlockSize
};

static VNVME_BACKEND_OPS g_FileBackendOps = {
    .Init = FileBackendInit,
    .Read = FileBackendRead,
    .Write = FileBackendWrite,
    .Flush = FileBackendFlush,
    .Close = FileBackendClose,
    .GetCapacity = FileBackendGetCapacity,
    .GetBlockSize = FileBackendGetBlockSize
};

BOOL InitBackend(PVNVME_CONFIG pConfig)
{
    PVNVME_BACKEND_OPS pOps;
    PVOID pBackendConfig;
    
    switch (pConfig->BackendType) {
        case VNVME_BACKEND_MEMORY:
            pOps = &g_MemoryBackendOps;
            pBackendConfig = &pConfig->Backend.Memory;
            break;
            
        case VNVME_BACKEND_FILE:
            pOps = &g_FileBackendOps;
            pBackendConfig = &pConfig->Backend.File;
            break;
            
        default:
            LogError("Unknown backend type: %d", pConfig->BackendType);
            return FALSE;
    }
    
    NTSTATUS status = pOps->Init(pBackendConfig, &g_BackendContext);
    if (!NT_SUCCESS(status)) {
        LogError("Backend init failed: 0x%08x", status);
        return FALSE;
    }
    
    g_Backend = *pOps;
    LogInfo("Backend initialized: type=%d, capacity=%llu",
            pConfig->BackendType,
            g_Backend.GetCapacity(g_BackendContext));
    
    return TRUE;
}
```

---

### 8. 心跳机制 (heartbeat.c)

```c
// heartbeat.c - 心跳和健康监控

#define HEARTBEAT_INTERVAL_MS  1000

typedef struct _HEARTBEAT_CONTEXT {
    HANDLE hDevice;
    HANDLE hThread;
    HANDLE hStopEvent;
    BOOL Running;
    
    UINT64 HeartbeatCount;
    UINT64 FailedCount;
} HEARTBEAT_CONTEXT;

static HEARTBEAT_CONTEXT g_Heartbeat;

DWORD WINAPI HeartbeatThreadProc(LPVOID lpParam)
{
    while (g_Heartbeat.Running) {
        // 等待停止事件或超时
        DWORD waitResult = WaitForSingleObject(
            g_Heartbeat.hStopEvent, 
            HEARTBEAT_INTERVAL_MS
        );
        
        if (waitResult == WAIT_OBJECT_0) {
            // 停止信号
            break;
        }
        
        // 发送心跳
        if (!SendHeartbeat(g_Heartbeat.hDevice)) {
            g_Heartbeat.FailedCount++;
            LogWarn("Heartbeat failed (count=%llu)", g_Heartbeat.FailedCount);
            
            if (g_Heartbeat.FailedCount >= 3) {
                LogError("Too many heartbeat failures, exiting");
                // 触发优雅关闭
                RequestShutdown();
                break;
            }
        } else {
            g_Heartbeat.HeartbeatCount++;
            g_Heartbeat.FailedCount = 0;  // 重置失败计数
        }
    }
    
    return 0;
}

HANDLE StartHeartbeatThread(HANDLE hDevice)
{
    g_Heartbeat.hDevice = hDevice;
    g_Heartbeat.Running = TRUE;
    g_Heartbeat.hStopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    g_Heartbeat.hThread = CreateThread(
        NULL, 0,
        HeartbeatThreadProc,
        NULL, 0, NULL
    );
    
    return g_Heartbeat.hThread;
}

void StopHeartbeatThread(HANDLE hThread)
{
    g_Heartbeat.Running = FALSE;
    SetEvent(g_Heartbeat.hStopEvent);
    WaitForSingleObject(hThread, 5000);
    CloseHandle(g_Heartbeat.hStopEvent);
    CloseHandle(hThread);
}
```

---

## 日志系统

```c
// logger.h - 日志接口

typedef enum _LOG_LEVEL {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN  = 1,
    LOG_LEVEL_INFO  = 2,
    LOG_LEVEL_DEBUG = 3,
    LOG_LEVEL_TRACE = 4
} LOG_LEVEL;

void InitLogger(PVNVME_LOG_CONFIG pConfig);
void LogMessage(LOG_LEVEL level, const char* format, ...);
void CloseLogger(void);

// 便捷宏
#define LogError(fmt, ...) LogMessage(LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LogWarn(fmt, ...)  LogMessage(LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LogInfo(fmt, ...)  LogMessage(LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LogDebug(fmt, ...) LogMessage(LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)
#define LogTrace(fmt, ...) LogMessage(LOG_LEVEL_TRACE, fmt, ##__VA_ARGS__)
```

---

## 错误处理

### 错误码定义

```c
typedef enum _VNVME_ERROR {
    VNVME_SUCCESS = 0,
    
    // 初始化错误 (100-199)
    VNVME_ERROR_DRIVER_NOT_FOUND = 100,
    VNVME_ERROR_DRIVER_VERSION_MISMATCH = 101,
    VNVME_ERROR_SHARED_MEMORY_MAP_FAILED = 102,
    VNVME_ERROR_BACKEND_INIT_FAILED = 103,
    
    // 运行时错误 (200-299)
    VNVME_ERROR_HEARTBEAT_FAILED = 200,
    VNVME_ERROR_COMMAND_TIMEOUT = 201,
    VNVME_ERROR_BACKEND_IO_FAILED = 202,
    
    // 配置错误 (300-399)
    VNVME_ERROR_CONFIG_FILE_NOT_FOUND = 300,
    VNVME_ERROR_CONFIG_PARSE_ERROR = 301,
    VNVME_ERROR_INVALID_CONFIG = 302
} VNVME_ERROR;
```

### 优雅关闭

```c
void HandleShutdown(int signal)
{
    LogInfo("Shutdown signal received (%d)", signal);
    
    // 1. 停止接受新命令
    g_CommandEngine.Running = FALSE;
    
    // 2. 等待当前命令完成 (最多 5 秒)
    DWORD startTime = GetTickCount();
    while (GetPendingCommandCount() > 0) {
        if (GetTickCount() - startTime > 5000) {
            LogWarn("Timeout waiting for commands, forcing shutdown");
            break;
        }
        Sleep(100);
    }
    
    // 3. 刷新后端
    g_Backend.Flush(g_BackendContext);
    
    // 4. 通知内核我们正在关闭
    SendUserShutdown(g_hDevice);
    
    // 5. 清理资源
    // ... 在 main() 中处理
}
```

---

## 性能优化

### 批处理

```c
// 批量处理命令以减少内核通知次数
#define MAX_BATCH_SIZE 16

void ProcessCommandBatch(PCOMMAND_ENGINE_CONTEXT pCtx)
{
    NVME_COMPLETION_ENTRY completions[MAX_BATCH_SIZE];
    UINT32 completionCount = 0;
    
    // 处理一批命令
    while (completionCount < MAX_BATCH_SIZE) {
        PVNVME_SUBMISSION_RING_ENTRY pEntry = GetNextCommand(pCtx);
        if (!pEntry) break;
        
        DispatchCommand(pCtx, pEntry, &completions[completionCount]);
        completionCount++;
    }
    
    // 批量提交完成
    if (completionCount > 0) {
        BatchAddCompletions(pCtx, completions, completionCount);
        SubmitCompletions(g_hDevice, completionCount);
    }
}
```

### 直接 I/O

```c
// 使用直接 I/O 避免双重缓冲
HANDLE OpenWithDirectIO(LPCWSTR path)
{
    return CreateFileW(
        path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL
    );
}
```

---

## 调试

### 日志输出示例

```
[2024-01-15 10:30:15.123] [INFO ] vnvme-server starting...
[2024-01-15 10:30:15.234] [INFO ] Opened kernel driver
[2024-01-15 10:30:15.345] [INFO ] Shared memory mapped at 0x000001A2B3C40000
[2024-01-15 10:30:15.456] [INFO ] Backend initialized: type=file, capacity=10737418240
[2024-01-15 10:30:15.567] [INFO ] Sent USER_READY to kernel
[2024-01-15 10:30:15.678] [INFO ] Entering main command loop
[2024-01-15 10:30:16.789] [DEBUG] Controller enable: CC=0x00460001
[2024-01-15 10:30:16.890] [DEBUG] Admin queue configured: ASQ=0x... ACQ=0x...
[2024-01-15 10:30:16.901] [INFO ] Controller ready (CSTS.RDY=1)
[2024-01-15 10:30:17.012] [DEBUG] Admin command: opcode=0x06 (Identify), CNS=1
[2024-01-15 10:30:17.123] [DEBUG] Admin command: opcode=0x06 (Identify), CNS=0, NSID=1
```

### WinDbg 附加

```
# 附加到 vnvme-server 进程
windbg -p <pid>

# 设置断点
bp vnvme_server!ProcessAdminCommand
bp vnvme_server!ProcessIoRead

# 查看共享内存
dt vnvme_server!SHARED_MEMORY_CONTROL_BLOCK <address>
```
