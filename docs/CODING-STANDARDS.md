# VNVME 编码规范和开发原则

本文档定义 Virtual NVMe 项目的编码规范、命名约定和开发原则。

---

## 0. 文档与代码的对应关系

> ⚠️ **重要约定**

项目文档中存在两种代码示例：

| 类型 | 说明 | 如何识别 |
|------|------|----------|
| **概念代码** | 说明原理、WDM 概念或设计思路 | 使用 `BUS_FDO_EXTENSION`、`CHILD_PDO_EXTENSION` 等传统命名，或标有"概念示例" |
| **实际代码** | 与 vnvme/*.c 源文件一致的代码 | 使用 `VNVME_FDO_CONTEXT`、`VNVME_PDO_CONTEXT`，函数名与 vnvme.h 声明一致 |

**权威性排序**：
1. `vnvme/*.c` 源文件（最高权威）
2. `vnvme/vnvme.h` 函数声明
3. `architecture-v2.md` 架构设计
4. 其他文档（仅供参考）

**典型对应关系**：

| 概念代码命名 | 实际代码命名 | 说明 |
|--------------|--------------|------|
| `BUS_FDO_EXTENSION` | `VNVME_FDO_CONTEXT` | WDM→WDF 命名 |
| `CHILD_PDO_EXTENSION` | `VNVME_PDO_CONTEXT` | WDM→WDF 命名 |
| `VnvmeInitRegisters` | `VnvmeInitializeBar0Registers` | 完整动词 |
| `VnvmePdoQueryId` | `VnvmePdoQueryDeviceId` | 完整名称 |

---

## 1. 命名规范

### 1.1 结构体命名

所有公共结构体使用 `VNVME_` 前缀，并根据用途添加后缀：

| 后缀 | 用途 | 示例 |
|------|------|------|
| `_ENTRY` | 环/队列中的单个元素 | `VNVME_SUBMISSION_RING_ENTRY`, `VNVME_COMPLETION_RING_ENTRY`, `VNVME_PRP_ENTRY` |
| `_RING` | 环形缓冲区容器 | `VNVME_SUBMISSION_RING`, `VNVME_COMPLETION_RING` |
| `_CONFIG` | 配置参数结构 | `VNVME_CONTROLLER_CONFIG`, `VNVME_NAMESPACE_CONFIG` |
| `_CONTEXT` | WDF 设备上下文 | `VNVME_FDO_CONTEXT`, `VNVME_PDO_CONTEXT` |
| `_STATE` | 状态信息 | `VNVME_QUEUE_STATE` |
| `_INFO` | 只读信息结构 | `VNVME_CONTROLLER_INFO` |
| `_INPUT` | IOCTL 输入参数 | `VNVME_MAP_SHARED_MEMORY_INPUT` |
| `_OUTPUT` | IOCTL 输出结果 | `VNVME_GET_STATUS_OUTPUT` |
| `_CONTROL_BLOCK` | 控制/管理结构 | `VNVME_SHARED_MEMORY_CONTROL_BLOCK` |

### 1.2 NVMe 术语一致性

遵循 NVMe 规范的术语，保持 Submission / Completion 配对：

```
内核 ──[SUBMISSION_RING]──→ 用户态 ──[处理]──→ [COMPLETION_RING] ──→ 内核
```

- ✅ `SUBMISSION_RING` - 提交环（内核向用户态提交命令）
- ✅ `COMPLETION_RING` - 完成环（用户态向内核返回结果）
- ❌ ~~`COMMAND_RING`~~ - 避免使用，语义不够明确

### 1.3 变量命名

| 类型 | 规则 | 示例 |
|------|------|------|
| 指针类型 | `P` 前缀 + 类型名 | `PVNVME_SUBMISSION_RING_ENTRY` |
| 指针变量 | `p` 前缀 + 描述 | `pEntry`, `pContext`, `pBuffer` |
| 函数参数 | 使用与类型一致的名称 | `PVNVME_SUBMISSION_RING_ENTRY pEntry` |
| 环类型变量 | 使用 `Ring` 后缀 | `subRing`, `cplRing` |

### 1.4 函数命名

```c
// 格式: Vnvme<模块><动作>
NTSTATUS VnvmeAllocateSharedMemory(...);    // 模块: 共享内存, 动作: 分配
VOID VnvmeInitializeBar0Registers(...);      // 模块: BAR0, 动作: 初始化
```

### 1.5 分层 API 命名

对于需要分层的功能，采用高层/低层命名：

```c
// 高层 API - 对外暴露，供 IOCTL 调用
// 命名: Vnvme<动作><业务概念>
NTSTATUS VnvmeCreateVirtualController(...);  // 创建虚拟控制器
NTSTATUS VnvmeDeleteVirtualController(...);  // 删除虚拟控制器

// 低层实现 - 内部使用，实际操作
// 命名: Vnvme<动作><技术概念>
NTSTATUS VnvmeCreateControllerPdo(...);      // 实际创建 PDO
NTSTATUS VnvmeDeleteControllerPdo(...);      // 实际删除 PDO
```

调用关系：
```
路径 1: IOCTL 创建控制器 (用户态请求)
─────────────────────────────────────
IOCTL_VNVME_CREATE_CONTROLLER
    ↓
VnvmeHandleCreateController()     ← IOCTL 处理
    ↓
VnvmeCreateVirtualController()    ← 高层: 参数验证、状态管理、列表维护
    ↓
VnvmeCreateControllerPdo()        ← 低层: WDF PDO 创建
    ↓
VnvmeAllocateBar0()               ← 资源分配
VnvmeInitializePcieConfig()


路径 2: 驱动加载时自动创建 (可选)
─────────────────────────────────────
VnvmeEvtDeviceAdd()               ← PnP 回调 (非 IOCTL)
    ↓
VnvmeCreateControllerPdo()        ← 直接调用低层，跳过高层 API
    ↓
VnvmeAllocateBar0()
VnvmeInitializePcieConfig()
```

### 1.6 WDF 回调函数命名

WDF 回调函数使用 `VnvmeEvt<对象><事件>` 格式：

```c
// FDO 回调
VnvmeEvtDeviceAdd                // EVT_WDF_DRIVER_DEVICE_ADD
VnvmeEvtDevicePrepareHardware    // EVT_WDF_DEVICE_PREPARE_HARDWARE
VnvmeEvtDeviceReleaseHardware    // EVT_WDF_DEVICE_RELEASE_HARDWARE
VnvmeEvtDeviceD0Entry            // EVT_WDF_DEVICE_D0_ENTRY
VnvmeEvtDeviceD0Exit             // EVT_WDF_DEVICE_D0_EXIT
VnvmeEvtDriverContextCleanup     // EVT_WDF_OBJECT_CONTEXT_CLEANUP (驱动清理)

// PDO 回调
VnvmePdoEvtDevicePrepareHardware // PDO 准备硬件
VnvmePdoEvtDeviceReleaseHardware // PDO 释放硬件
VnvmePdoEvtDeviceD0Entry         // PDO 进入 D0
VnvmePdoEvtDeviceD0Exit          // PDO 退出 D0

// I/O 队列回调
VnvmeEvtIoDeviceControl          // EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL

// 定时器回调
VnvmeEvtPollingTimer             // EVT_WDF_TIMER
```

### 1.6 IOCTL 处理函数命名

IOCTL 处理函数使用 `VnvmeHandle<操作>` 格式（静态函数）：

```c
static VnvmeHandleGetVersion(...)      // 处理 IOCTL_VNVME_GET_VERSION
static VnvmeHandleGetStatus(...)       // 处理 IOCTL_VNVME_GET_STATUS
static VnvmeHandleMapSharedMemory(...) // 处理 IOCTL_VNVME_MAP_SHARED_MEMORY
static VnvmeHandleUserReady(...)       // 处理 IOCTL_VNVME_USER_READY
static VnvmeHandleHeartbeat(...)       // 处理 IOCTL_VNVME_HEARTBEAT
```

### 1.7 处理逻辑函数命名

处理逻辑函数使用 `VnvmeProcess<对象>` 格式：

```c
VnvmeProcessDoorbells(...)     // 处理 Doorbell 变化
VnvmeProcessAdminCommand(...)  // 处理管理命令
VnvmeProcessIoCommand(...)     // 处理 I/O 命令
VnvmeProcessInterrupts(...)    // 处理中断合并
```

---

## 2. IOCTL 设计原则

### 2.1 状态输出应详尽

状态查询 IOCTL 应返回尽可能多的信息，便于调试和监控：

```c
// ✅ 好的设计 - 提供完整信息
typedef struct _VNVME_GET_STATUS_OUTPUT {
    UINT32 DriverStatus;            // 驱动状态
    UINT32 UserServiceStatus;       // 用户态服务状态
    UINT32 ControllerCount;         // 控制器数量
    UINT32 NamespaceCount;          // 命名空间数量
    UINT32 SharedMemoryMapped;      // 共享内存是否已映射
    UINT32 SharedMemorySize;        // 共享内存大小
    UINT32 UserReady;               // 用户态是否就绪
    UINT32 UserPid;                 // 用户态进程 ID
    UINT64 CommandsProcessed;       // 已处理命令数
    UINT64 CompletionsPosted;       // 已提交完成数
    UINT64 BytesRead;               // 读取字节数
    UINT64 BytesWritten;            // 写入字节数
    UINT64 ErrorCount;              // 错误数
    UINT64 UptimeMs;                // 运行时间
    UINT64 LastHeartbeatMs;         // 上次心跳时间
} VNVME_GET_STATUS_OUTPUT;

// ❌ 不好的设计 - 信息过少
typedef struct _VNVME_GET_STATUS_OUTPUT {
    UINT32 DriverStatus;
    UINT64 CommandsProcessed;
} VNVME_GET_STATUS_OUTPUT;
```

### 2.2 状态码应有人类可读描述

```c
// ✅ 在工具输出中显示状态名称
printf("  Driver State: %u (%s)\n", status.DriverStatus,
       status.DriverStatus == 0 ? "Initializing" :
       status.DriverStatus == 1 ? "Ready" :
       status.DriverStatus == 2 ? "Running" : "Unknown");
```

---

## 3. 内核驱动开发原则

### 3.1 内存对齐

共享内存结构必须使用 `#pragma pack(push, 1)` 确保布局一致：

```c
#pragma pack(push, 1)

typedef struct _VNVME_SUBMISSION_RING_ENTRY {
    UINT32 Type;
    UINT16 CommandId;
    // ...
} VNVME_SUBMISSION_RING_ENTRY;

#pragma pack(pop)

// 使用静态断言验证大小
VNVME_STATIC_ASSERT(sizeof(VNVME_SUBMISSION_RING_ENTRY) == 80, entry_size);
```

### 3.2 避免与系统头文件冲突

```c
// ❌ 不要使用可能冲突的宏名
#define C_ASSERT(x) ...  // 与 winnt.h 冲突

// ✅ 使用项目前缀
#define VNVME_STATIC_ASSERT(expr, msg) \
    typedef char vnvme_static_assert_##msg[(expr) ? 1 : -1]
```

### 3.3 NVMe 寄存器访问

使用联合体的具体成员进行赋值，避免类型转换警告：

```c
// ❌ 直接赋值会产生类型错误
regs->CAP = (NVME_CAP_REGISTER){ .MPSMIN = 0, ... };

// ✅ 使用联合体成员
NVME_CAP_REGISTER cap = {0};
cap.MPSMIN = 0;
cap.MPSMAX = 0;
// ...
regs->CAP.AsUint64 = cap.AsUint64;
```

### 3.4 匿名联合/结构警告处理

NVMe 规范头文件中的匿名联合需要禁用 C4201 警告：

```c
#pragma warning(push)
#pragma warning(disable: 4201)  // nonstandard extension: nameless struct/union

// NVMe 寄存器定义...

#pragma warning(pop)
```

---

## 4. 构建配置原则

### 4.1 开发阶段配置

```xml
<!-- 禁用签名以便快速迭代 -->
<SignMode>Off</SignMode>

<!-- 禁用 INF2CAT 避免签名错误 -->
<EnableInf2Cat>false</EnableInf2Cat>

<!-- 禁用 Spectre 缓解以避免缺少库的错误 -->
<SpectreMitigation>false</SpectreMitigation>
```

### 4.2 INF 文件版本要求

使用 DIRID 13 (DriverStore) 需要 Windows 10 1709+：

```inf
[Manufacturer]
%ManufacturerName%=Standard,NTamd64.10.0...16299
```

---

## 5. 文档与代码同步原则

### 5.1 代码变更必须同步文档

当修改公共接口（结构体、IOCTL、函数签名）时，必须同步更新：

1. **头文件注释** - 结构体字段说明
2. **相关文档** - data-structures.md, ioctl-interface.md 等
3. **示例代码** - 文档中的代码片段
4. **变量命名** - 确保文档示例与实际代码一致

### 5.2 中英术语对照

| 英文 | 中文 | 用于 |
|------|------|------|
| Submission Ring | 提交环 | 内核→用户态命令传递 |
| Completion Ring | 完成环 | 用户态→内核结果返回 |
| Entry | 条目 | 环中的单个元素 |
| Control Block | 控制块 | 共享内存头部管理结构 |

---

## 6. 版本控制原则

### 6.1 .gitignore 配置

```gitignore
# 使用 ** 匹配任意层级
**/build/
*.obj
*.pdb
*.sys
*.exe
```

### 6.2 提交消息格式

```
<类型>: <简短描述>

<详细说明>
```

类型包括：
- `feat` - 新功能
- `fix` - 修复
- `refactor` - 重构
- `docs` - 文档
- `build` - 构建配置

---

## 7. 调试信息原则

### 7.1 状态工具应提供完整上下文

`vnvmectl status` 输出应包含：

1. **状态分类** - 驱动状态、用户态状态、设备状态
2. **资源信息** - 共享内存大小、映射状态
3. **统计数据** - 命令数、字节数、错误数
4. **时间信息** - 运行时间、心跳时间

### 7.2 错误输出应有具体上下文

```c
// ✅ 提供具体错误码
fprintf(stderr, "Error: IOCTL failed (Error: %lu)\n", GetLastError());

// ❌ 不提供信息
fprintf(stderr, "Error occurred\n");
```

---

## 8. 代码风格决策

本节记录项目开发过程中的关键风格决策和设计选择。

### 8.1 注释风格

**决策**: 统一使用 `//` 而非 `/* */`

```c
// ✅ 使用 C++ 风格行注释
// 标识
BOOLEAN IsFdo;                      // TRUE = FDO, FALSE = PDO

// ✅ 分隔符也使用行注释
//===========================================================================
// 函数声明 - vnvme.c
//===========================================================================

// ❌ 避免块注释
/* 标识 */
BOOLEAN IsFdo;                      /* TRUE = FDO, FALSE = PDO */
```

**例外**: 文件头 Doxygen 注释可使用 `/** */`。

### 8.2 Phase 2/3 预留字段

**决策**: 不直接删除未实现字段，以注释形式保留

```c
typedef struct _VNVME_NAMESPACE {
    // 标识
    ULONG NsId;                         // 命名空间 ID (1-based, 0=未使用)
    BOOLEAN Active;                     // 是否激活
    // BOOLEAN ReadOnly;                // TODO Phase 2: 只读标志
    // BOOLEAN ThinProvisioned;         // TODO Phase 2: 精简配置
    
    // TODO Phase 2: 唯一标识
    // GUID Guid;
    // UCHAR Nguid[16];                 // Namespace Globally Unique Identifier
    // UCHAR Eui64[8];                  // IEEE Extended Unique Identifier
    
    // 容量
    ULONG BlockSize;                    // 逻辑块大小 (512 或 4096)
    ULONGLONG TotalBlocks;              // 总逻辑块数
    ULONGLONG TotalBytes;               // 总字节数
    
    // TODO Phase 2: 后端偏移
    // ULONGLONG BackendOffset;         // 在后端存储中的偏移
    
    // TODO Phase 3: 统计
    // volatile LONG64 ReadCommands;    // 读命令计数
} VNVME_NAMESPACE, *PVNVME_NAMESPACE;
```

**原则**:
- 保留原文档中有价值的注释说明
- 使用 `TODO Phase N:` 前缀标记
- 便于后续阶段快速取消注释启用

### 8.3 类型大小选择

**决策**: 优先匹配 NVMe 规范定义的类型大小

| 字段 | NVMe 规范 | 代码类型 | 说明 |
|------|-----------|----------|------|
| Queue ID | 16-bit | `USHORT` | SQID/CQID 在规范中是 16-bit |
| IoQueueCount | - | `USHORT` | 最大 64，16-bit 足够 |
| MaxIoQueues | CAP.MQES (16-bit) | `USHORT` | 与规范一致 |
| NamespaceCount | - | `USHORT` | 最大 16，16-bit 足够 |
| ControllerId | - | `ULONG` | 内部索引，无规范限制 |

### 8.4 常量命名精确性

**决策**: 常量名应准确描述其用途

| 旧名称 | 新名称 | 原因 |
|--------|--------|------|
| `VNVME_MAX_QUEUES` | `VNVME_MAX_IO_QUEUES` | 仅用于 I/O 队列数组，不含 Admin |
| `SharedMemory*` | `Shm*` | 缩短名称，统一前缀 |

### 8.5 LIST_ENTRY vs WDFCHILDLIST

**决策**: 使用 `LIST_ENTRY` 管理子设备列表

| 方案 | 优点 | 缺点 |
|------|------|------|
| `WDFCHILDLIST` | 框架支持热插拔、自动枚举 | 需要标识描述符、多个回调、复杂 |
| `LIST_ENTRY` ✅ | 简单直接、完全控制 | 需手动维护 |

**原因**:
- vnvme 控制器由 IOCTL 创建/删除，非真实总线枚举
- 控制器数量少 (≤16)，不需要框架的重量级机制
- 驱动完全控制 PDO 生命周期

### 8.6 WDF vs WDM 字段

**决策**: 使用 KMDF，不保留 WDM 风格的底层字段

| WDM 字段 | WDF 替代 | 说明 |
|----------|----------|------|
| `PDEVICE_OBJECT PhysicalDeviceObject` | `WdfDeviceWdmGetPhysicalDevice(Device)` | 按需获取 |
| `PDEVICE_OBJECT AttachedDevice` | 框架自动管理 | Filter DO 附加由 WDF 处理 |
| `BOOLEAN Present` | WDF PnP 状态机 | 框架跟踪设备存在 |
| `BOOLEAN ReportedMissing` | WDF PnP 状态机 | 枚举器回调返回状态 |

---

## 更新历史

| 日期 | 版本 | 说明 |
|------|------|------|
| 2025-12-23 | 1.1 | 添加代码风格决策章节 (注释风格、Phase预留、类型选择等) |
| 2025-12-23 | 1.0 | 初始版本，基于 Phase 1 开发经验 |
