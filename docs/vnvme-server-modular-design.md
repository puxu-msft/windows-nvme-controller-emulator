# vnvme-server 模块化架构设计

本文档定义 vnvme-server 的高度模块化设计，包含完整的模块依赖图、接口定义和实现计划。

---

## 设计原则

1. **单一职责**: 每个模块只负责一个功能域
2. **接口隔离**: 通过 .h 文件定义清晰接口，隐藏实现细节
3. **依赖注入**: 通过上下文结构体传递依赖，便于测试
4. **可测试性**: 每个模块可独立编译测试
5. **渐进式实现**: 可从 v1 逐步迁移到 v2

---

## 目标架构 (v2 - 高度模块化)

```
vnvme-server/
│
├── core/                           # 核心基础设施
│   ├── types.h                     # ✅ 基础类型、错误码、宏
│   ├── logger.h / logger.c         # ✅ 日志系统
│   ├── config.h / config.c         # ✅ 配置管理
│   └── utils.h / utils.c           # ⬜ 通用工具函数
│
├── driver/                         # 驱动通信层
│   ├── driver_comm.h / driver_comm.c   # ✅ 设备句柄、IOCTL
│   ├── shm.h / shm.c               # ⬜ 共享内存封装
│   └── heartbeat.h / heartbeat.c   # ⬜ 心跳管理 (从 driver_comm 分离)
│
├── protocol/                       # NVMe 协议处理
│   ├── command_engine.h / command_engine.c   # ✅ 命令引擎 (分发+调度) (562+253行)
│   ├── admin_commands.h / admin_commands.c   # ✅ Admin 命令 (911+301行)
│   ├── io_commands.h / io_commands.c         # ✅ I/O 命令 (632+255行)
│   └── nvme_types.h                # ⬜ NVMe 协议类型 (复用 include/nvme_spec.h)
│
├── backend/                        # 存储后端层
│   ├── backend.h                   # ✅ 后端接口
│   ├── backend_common.c            # ✅ 后端工厂/分发
│   ├── backend_memory.c            # ✅ 内存后端
│   ├── backend_file.c              # ✅ 文件后端
│   ├── backend_sparse.c            # ⬜ 稀疏文件后端 (扩展)
│   └── backend_vhdx.c              # ⬜ VHDX 后端 (扩展)
│
├── namespace/                      # 命名空间管理
│   ├── namespace.h / namespace.c   # ⬜ 命名空间抽象
│   └── ns_manager.h / ns_manager.c # ⬜ 多命名空间管理
│
├── controller/                     # 控制器状态
│   ├── controller.h / controller.c # ⬜ 控制器状态机
│   └── features.h / features.c     # ⬜ Feature 管理
│
├── main.c                          # ✅ 程序入口 (655行)
├── main_v2.c                       # ✅ 模块化入口 (233行, 待启用)
├── vnvme_server.h                  # ✅ 公共头文件
│
└── tests/                          # 单元测试
    ├── test_logger.c
    ├── test_config.c
    ├── test_backend.c
    └── test_commands.c
```

---

## 模块依赖图

```
                    ┌─────────────────────────────────────────────────────┐
                    │                     main.c                           │
                    │  (生命周期管理、信号处理、主循环)                      │
                    └───────────────────────┬─────────────────────────────┘
                                            │
           ┌────────────────────────────────┼────────────────────────────────┐
           │                                │                                │
           ▼                                ▼                                ▼
┌──────────────────┐              ┌──────────────────┐              ┌──────────────────┐
│     config.c     │              │  driver_comm.c   │              │ command_engine.c │
│  (配置解析)       │              │  (驱动通信)      │              │  (命令调度)       │
└────────┬─────────┘              └────────┬─────────┘              └────────┬─────────┘
         │                                 │                                 │
         │                                 ▼                                 │
         │                        ┌──────────────────┐                       │
         │                        │     shm.c        │◀──────────────────────┤
         │                        │  (共享内存封装)   │                       │
         │                        └────────┬─────────┘                       │
         │                                 │                                 │
         │                                 ▼                                 │
         │                        ┌──────────────────┐                       │
         │                        │  heartbeat.c     │                       │
         │                        │  (心跳线程)       │                       │
         │                        └──────────────────┘                       │
         │                                                                   │
         │                                 ┌─────────────────────────────────┤
         │                                 │                                 │
         │                                 ▼                                 ▼
         │                        ┌──────────────────┐              ┌──────────────────┐
         │                        │ admin_commands.c │              │  io_commands.c   │
         │                        │  (Identify,      │              │  (Read, Write,   │
         │                        │   Get/Set Feat)  │              │   Flush, etc.)   │
         │                        └────────┬─────────┘              └────────┬─────────┘
         │                                 │                                 │
         │                                 │                                 │
         │                                 ▼                                 ▼
         │                        ┌──────────────────┐              ┌──────────────────┐
         │                        │  controller.c    │◀─────────────│   namespace.c    │
         │                        │  (控制器状态)     │              │  (NS 管理)        │
         │                        └────────┬─────────┘              └────────┬─────────┘
         │                                 │                                 │
         │                                 │                                 │
         │                                 └─────────────┬───────────────────┘
         │                                               │
         │                                               ▼
         │                                      ┌──────────────────┐
         │                                      │    backend.h     │
         │                                      │  (后端接口)       │
         │                                      └────────┬─────────┘
         │                                               │
         │               ┌───────────────────────────────┼───────────────────────────────┐
         │               │                               │                               │
         │               ▼                               ▼                               ▼
         │      ┌──────────────────┐            ┌──────────────────┐            ┌──────────────────┐
         │      │ backend_memory.c │            │ backend_file.c   │            │ backend_vhdx.c   │
         │      │  (内存后端)       │            │  (文件后端)       │            │  (VHDX 后端)     │
         │      └──────────────────┘            └──────────────────┘            └──────────────────┘
         │
         │
         └────────────────────────────────────────────────────────────────────────────────────────┐
                                                                                                  │
                                                                                                  ▼
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                            logger.c                                                      │
│                                         (日志系统 - 无依赖)                                               │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────┘
                                                  ▲
                                                  │
┌─────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                            types.h                                                       │
│                                      (基础类型 - 无依赖)                                                  │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## 依赖关系矩阵

| 模块 | 依赖 | 被依赖 |
|------|------|--------|
| **types.h** | (无) | 所有模块 |
| **logger.c** | types.h | 所有模块 |
| **config.c** | types.h, logger | main |
| **driver_comm.c** | types.h, logger, vnvme_ioctl.h | main, shm |
| **shm.c** | types.h, logger, driver_comm | command_engine, heartbeat |
| **heartbeat.c** | types.h, logger, shm | main |
| **command_engine.c** | types.h, logger, shm, admin_commands, io_commands | main |
| **admin_commands.c** | types.h, logger, controller, namespace, nvme_spec | command_engine |
| **io_commands.c** | types.h, logger, namespace, backend, nvme_spec | command_engine |
| **controller.c** | types.h, logger, features | admin_commands |
| **namespace.c** | types.h, logger, backend | admin_commands, io_commands |
| **features.c** | types.h, logger | controller |
| **backend.h** | types.h | namespace, backend_* |
| **backend_memory.c** | types.h, logger, backend.h | backend_common |
| **backend_file.c** | types.h, logger, backend.h | backend_common |
| **backend_common.c** | types.h, logger, backend.h, backend_* | namespace |

---

## 关键接口定义

### 1. 命令引擎接口 (command_engine.h)

```c
/**
 * @file command_engine.h
 * @brief NVMe 命令处理引擎接口
 */

#ifndef _COMMAND_ENGINE_H_
#define _COMMAND_ENGINE_H_

#include "types.h"
#include "shm.h"
#include "controller.h"

//===========================================================================
// 命令处理上下文
//===========================================================================

typedef struct _COMMAND_ENGINE_CONFIG {
    UINT32          maxQueueDepth;          // 最大队列深度
    UINT32          maxBatchSize;           // 批处理大小
    BOOL            enableBatching;         // 启用批处理
} COMMAND_ENGINE_CONFIG;

typedef struct _COMMAND_ENGINE_CONTEXT {
    PSHM_CONTEXT            shm;            // 共享内存上下文
    PCONTROLLER_CONTEXT     controller;     // 控制器上下文
    COMMAND_ENGINE_CONFIG   config;         // 配置
    
    // 统计
    UINT64                  totalProcessed;
    UINT64                  adminProcessed;
    UINT64                  ioProcessed;
    UINT64                  errors;
} COMMAND_ENGINE_CONTEXT, *PCOMMAND_ENGINE_CONTEXT;

//===========================================================================
// 函数接口
//===========================================================================

/**
 * 初始化命令引擎
 */
BOOL CommandEngineInit(
    PCOMMAND_ENGINE_CONTEXT pCtx,
    PSHM_CONTEXT pShm,
    PCONTROLLER_CONTEXT pController,
    const COMMAND_ENGINE_CONFIG* pConfig
);

/**
 * 处理一批命令 (非阻塞)
 * @return 处理的命令数量
 */
UINT32 CommandEngineProcess(PCOMMAND_ENGINE_CONTEXT pCtx);

/**
 * 关闭命令引擎
 */
void CommandEngineShutdown(PCOMMAND_ENGINE_CONTEXT pCtx);

/**
 * 获取统计信息
 */
void CommandEngineGetStats(
    PCOMMAND_ENGINE_CONTEXT pCtx,
    PUINT64 pTotal,
    PUINT64 pAdmin,
    PUINT64 pIo,
    PUINT64 pErrors
);

#endif /* _COMMAND_ENGINE_H_ */
```

### 2. 控制器接口 (controller.h)

```c
/**
 * @file controller.h
 * @brief NVMe 控制器状态管理
 */

#ifndef _CONTROLLER_H_
#define _CONTROLLER_H_

#include "types.h"
#include "namespace.h"

//===========================================================================
// 控制器状态
//===========================================================================

typedef enum _CONTROLLER_STATE {
    CTRL_STATE_DISABLED = 0,    // CC.EN = 0
    CTRL_STATE_ENABLED  = 1,    // CC.EN = 1, CSTS.RDY = 1
    CTRL_STATE_RESETTING = 2,   // 重置中
    CTRL_STATE_ERROR    = 3,    // 错误状态
} CONTROLLER_STATE;

typedef struct _CONTROLLER_CONFIG {
    char            serialNumber[20];
    char            modelNumber[40];
    char            firmwareRevision[8];
    UINT16          vendorId;
    UINT16          subsystemVendorId;
    UINT32          maxNamespaces;
    UINT32          maxQueueEntries;
} CONTROLLER_CONFIG;

typedef struct _CONTROLLER_CONTEXT {
    CONTROLLER_STATE        state;
    CONTROLLER_CONFIG       config;
    
    // 命名空间列表
    PNS_MANAGER_CONTEXT     nsManager;
    
    // Identify 数据缓存
    NVME_IDENTIFY_CONTROLLER identifyCtrl;
    
    // Feature 值
    PFEATURES_CONTEXT       features;
    
    // 统计
    UINT64                  adminCommands;
    UINT64                  ioCommands;
} CONTROLLER_CONTEXT, *PCONTROLLER_CONTEXT;

//===========================================================================
// 函数接口
//===========================================================================

BOOL ControllerInit(PCONTROLLER_CONTEXT pCtx, const CONTROLLER_CONFIG* pConfig);
void ControllerShutdown(PCONTROLLER_CONTEXT pCtx);

CONTROLLER_STATE ControllerGetState(PCONTROLLER_CONTEXT pCtx);
BOOL ControllerSetState(PCONTROLLER_CONTEXT pCtx, CONTROLLER_STATE newState);

BOOL ControllerGetIdentify(PCONTROLLER_CONTEXT pCtx, PVOID buffer, UINT32 size);

BOOL ControllerAddNamespace(PCONTROLLER_CONTEXT pCtx, UINT32 nsid, PNS_CONTEXT pNs);
BOOL ControllerRemoveNamespace(PCONTROLLER_CONTEXT pCtx, UINT32 nsid);
PNS_CONTEXT ControllerGetNamespace(PCONTROLLER_CONTEXT pCtx, UINT32 nsid);

#endif /* _CONTROLLER_H_ */
```

### 3. 命名空间接口 (namespace.h)

```c
/**
 * @file namespace.h
 * @brief NVMe 命名空间管理
 */

#ifndef _NAMESPACE_H_
#define _NAMESPACE_H_

#include "types.h"
#include "backend.h"

//===========================================================================
// 命名空间
//===========================================================================

typedef struct _NS_CONFIG {
    UINT32          nsid;               // Namespace ID (1-based)
    UINT64          size;               // 大小 (字节)
    UINT32          blockSize;          // 块大小 (默认 512)
    UINT32          metadataSize;       // 元数据大小 (通常 0)
    BOOL            readOnly;           // 只读
    BACKEND_TYPE    backendType;        // 后端类型
    WCHAR           backendPath[MAX_PATH];  // 后端路径
} NS_CONFIG;

typedef struct _NS_CONTEXT {
    UINT32              nsid;
    NS_CONFIG           config;
    PBACKEND_CONTEXT    backend;        // 存储后端
    
    // Identify Namespace 数据
    NVME_IDENTIFY_NAMESPACE identifyNs;
    
    // 统计
    UINT64              readOps;
    UINT64              writeOps;
    UINT64              bytesRead;
    UINT64              bytesWritten;
} NS_CONTEXT, *PNS_CONTEXT;

//===========================================================================
// 命名空间管理器
//===========================================================================

typedef struct _NS_MANAGER_CONTEXT {
    UINT32          maxNamespaces;
    UINT32          activeCount;
    PNS_CONTEXT*    namespaces;         // 数组 [maxNamespaces]
    CRITICAL_SECTION lock;
} NS_MANAGER_CONTEXT, *PNS_MANAGER_CONTEXT;

//===========================================================================
// 函数接口
//===========================================================================

// 单个命名空间
PNS_CONTEXT NsCreate(const NS_CONFIG* pConfig);
void NsDestroy(PNS_CONTEXT pCtx);
BOOL NsRead(PNS_CONTEXT pCtx, UINT64 slba, UINT32 nlb, PVOID buffer);
BOOL NsWrite(PNS_CONTEXT pCtx, UINT64 slba, UINT32 nlb, const PVOID buffer);
BOOL NsFlush(PNS_CONTEXT pCtx);
BOOL NsWriteZeroes(PNS_CONTEXT pCtx, UINT64 slba, UINT32 nlb);
BOOL NsGetIdentify(PNS_CONTEXT pCtx, PVOID buffer, UINT32 size);

// 命名空间管理器
PNS_MANAGER_CONTEXT NsManagerCreate(UINT32 maxNamespaces);
void NsManagerDestroy(PNS_MANAGER_CONTEXT pCtx);
BOOL NsManagerAdd(PNS_MANAGER_CONTEXT pCtx, PNS_CONTEXT pNs);
BOOL NsManagerRemove(PNS_MANAGER_CONTEXT pCtx, UINT32 nsid);
PNS_CONTEXT NsManagerGet(PNS_MANAGER_CONTEXT pCtx, UINT32 nsid);
UINT32 NsManagerGetActiveList(PNS_MANAGER_CONTEXT pCtx, UINT32* nsidList, UINT32 maxCount);

#endif /* _NAMESPACE_H_ */
```

### 4. Admin 命令接口 (admin_commands.h)

```c
/**
 * @file admin_commands.h
 * @brief NVMe Admin 命令处理
 */

#ifndef _ADMIN_COMMANDS_H_
#define _ADMIN_COMMANDS_H_

#include "types.h"
#include "controller.h"
#include "../include/nvme_spec.h"

//===========================================================================
// Admin 命令处理上下文
//===========================================================================

typedef struct _ADMIN_CMD_CONTEXT {
    PCONTROLLER_CONTEXT     controller;
    PVOID                   dataBuffer;     // 数据传输缓冲区 (共享内存)
    SIZE_T                  dataBufferSize;
} ADMIN_CMD_CONTEXT, *PADMIN_CMD_CONTEXT;

//===========================================================================
// 命令处理函数
//===========================================================================

/**
 * 处理 Admin 命令
 * @return NVMe 状态码 (NVME_SC_*)
 */
UINT16 AdminCmdProcess(
    PADMIN_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    NVME_COMPLETION* pCpl
);

// 单独命令处理函数 (可单独测试)
UINT16 AdminCmdIdentify(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdGetFeatures(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdSetFeatures(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdGetLogPage(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdAbort(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdCreateIoSq(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdCreateIoCq(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdDeleteIoSq(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 AdminCmdDeleteIoCq(PADMIN_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);

#endif /* _ADMIN_COMMANDS_H_ */
```

### 5. I/O 命令接口 (io_commands.h)

```c
/**
 * @file io_commands.h
 * @brief NVMe I/O 命令处理
 */

#ifndef _IO_COMMANDS_H_
#define _IO_COMMANDS_H_

#include "types.h"
#include "namespace.h"
#include "../include/nvme_spec.h"

//===========================================================================
// I/O 命令处理上下文
//===========================================================================

typedef struct _IO_CMD_CONTEXT {
    PNS_MANAGER_CONTEXT     nsManager;      // 命名空间管理器
    PVOID                   dataBuffer;     // 数据传输缓冲区
    SIZE_T                  dataBufferSize;
} IO_CMD_CONTEXT, *PIO_CMD_CONTEXT;

//===========================================================================
// 命令处理函数
//===========================================================================

/**
 * 处理 I/O 命令
 * @return NVMe 状态码 (NVME_SC_*)
 */
UINT16 IoCmdProcess(
    PIO_CMD_CONTEXT pCtx,
    const NVME_COMMAND* pCmd,
    NVME_COMPLETION* pCpl
);

// 单独命令处理函数 (可单独测试)
UINT16 IoCmdRead(PIO_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 IoCmdWrite(PIO_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 IoCmdFlush(PIO_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 IoCmdWriteZeroes(PIO_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);
UINT16 IoCmdDatasetManagement(PIO_CMD_CONTEXT pCtx, const NVME_COMMAND* pCmd, NVME_COMPLETION* pCpl);

#endif /* _IO_COMMANDS_H_ */
```

---

## 实现计划

### 阶段 1: 基础设施 ✅ 已完成

| 文件 | 状态 | 说明 |
|------|------|------|
| types.h | ✅ | 基础类型、宏、错误码 |
| logger.h / logger.c | ✅ | 日志系统 |
| config.h / config.c | ✅ | 配置解析 |
| backend.h | ✅ | 后端接口 |
| backend_common.c | ✅ | 后端分发器 |
| backend_memory.c | ✅ | 内存后端 |
| backend_file.c | ✅ | 文件后端 |
| driver_comm.h / driver_comm.c | ✅ | 驱动通信 |

### 阶段 2: 命令处理拆分 ⬜ 待实现

| 文件 | 优先级 | 依赖 | 预计行数 |
|------|--------|------|----------|
| admin_commands.h | 高 | types, nvme_spec | ~80 |
| admin_commands.c | 高 | controller, namespace | ~600 |
| io_commands.h | 高 | types, nvme_spec | ~60 |
| io_commands.c | 高 | namespace, backend | ~400 |
| command_engine.h | 高 | shm, admin/io_commands | ~80 |
| command_engine.c | 高 | 上述所有 | ~300 |

### 阶段 3: 控制器和命名空间 ⬜ 待实现

| 文件 | 优先级 | 依赖 | 预计行数 |
|------|--------|------|----------|
| features.h / features.c | 中 | types | ~200 |
| controller.h / controller.c | 中 | features, namespace | ~400 |
| namespace.h / namespace.c | 中 | backend | ~350 |
| ns_manager.h / ns_manager.c | 中 | namespace | ~200 |

### 阶段 4: 扩展后端 ⬜ 未来

| 文件 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| backend_sparse.c | 低 | backend.h | 稀疏文件后端 (节省磁盘) |
| backend_vhdx.c | 低 | backend.h | VHDX 格式后端 |
| backend_iscsi.c | 低 | backend.h | iSCSI 后端 (网络存储) |

### 阶段 5: 高级功能 ⬜ 未来

| 文件 | 优先级 | 依赖 | 说明 |
|------|--------|------|------|
| async_io.c | 低 | io_commands | 异步 I/O 支持 |
| queue_manager.c | 低 | command_engine | 多队列管理 |
| metrics.c | 低 | all | 性能指标收集 |

---

## 迁移策略

### 从 v1 到 v2 的渐进迁移

```
阶段 A: 当前状态 (v1 运行, v2 模块已创建)
├── main.c (v1, 活跃)
├── command_processor.c (v1, 活跃)
├── backend.c (v1, 活跃)
└── [v2 模块被注释]

阶段 B: 后端模块替换
├── main.c (v1)
├── command_processor.c (v1)
└── backend_common.c + backend_*.c (v2, 替换 backend.c)

阶段 C: 命令模块替换
├── main.c (v1)
├── admin_commands.c + io_commands.c (v2, 替换 command_processor.c)
└── backend (v2)

阶段 D: 完全 v2
├── main_v2.c (v2)
├── command_engine.c (v2)
├── admin_commands.c + io_commands.c (v2)
├── controller.c + namespace.c (v2)
└── backend (v2)
```

---

## 编译配置

### vcxproj 模块分组

```xml
<!-- 阶段 1: 基础设施 (已启用) -->
<ItemGroup Label="Core">
  <ClCompile Include="core\logger.c" />
  <ClCompile Include="core\config.c" />
</ItemGroup>

<!-- 阶段 1: 后端 (已启用) -->
<ItemGroup Label="Backend">
  <ClCompile Include="backend\backend_common.c" />
  <ClCompile Include="backend\backend_memory.c" />
  <ClCompile Include="backend\backend_file.c" />
</ItemGroup>

<!-- 阶段 1: 驱动通信 (已启用) -->
<ItemGroup Label="Driver">
  <ClCompile Include="driver\driver_comm.c" />
</ItemGroup>

<!-- 阶段 2: 命令处理 (待实现) -->
<ItemGroup Label="Protocol">
  <!-- <ClCompile Include="protocol\command_engine.c" /> -->
  <!-- <ClCompile Include="protocol\admin_commands.c" /> -->
  <!-- <ClCompile Include="protocol\io_commands.c" /> -->
</ItemGroup>

<!-- 阶段 3: 控制器 (待实现) -->
<ItemGroup Label="Controller">
  <!-- <ClCompile Include="controller\controller.c" /> -->
  <!-- <ClCompile Include="controller\features.c" /> -->
  <!-- <ClCompile Include="namespace\namespace.c" /> -->
</ItemGroup>
```

---

## 测试策略

每个模块都应该可以独立测试：

```c
// tests/test_backend.c
void test_memory_backend_read_write() {
    BACKEND_CONFIG config = {
        .Type = BACKEND_TYPE_MEMORY,
        .Size = 1024 * 1024,  // 1MB
        .BlockSize = 512
    };
    
    PBACKEND_CONTEXT ctx = BackendCreate(&config);
    assert(ctx != NULL);
    
    BYTE writeData[512] = {0xAB};
    BYTE readData[512] = {0};
    
    assert(BackendWrite(ctx, 0, writeData, 512));
    assert(BackendRead(ctx, 0, readData, 512));
    assert(memcmp(writeData, readData, 512) == 0);
    
    BackendDestroy(ctx);
}
```

---

## 下一步行动

1. **完成命令处理拆分** (阶段 2)
   - 从 command_processor.c 提取 Admin 命令到 admin_commands.c
   - 从 command_processor.c 提取 I/O 命令到 io_commands.c
   - 创建 command_engine.c 作为分发层

2. **创建控制器/命名空间抽象** (阶段 3)
   - 实现 controller.c 管理控制器状态
   - 实现 namespace.c 管理单个命名空间
   - 实现 ns_manager.c 管理多命名空间

3. **启用 v2 编译并测试**
   - 更新 vcxproj 启用 v2 模块
   - 验证功能一致性
   - 替换 main.c
