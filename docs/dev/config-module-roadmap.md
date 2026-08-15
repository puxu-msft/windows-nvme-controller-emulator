# Config 模块开发路线图

**创建日期**: 2025-12-25  
**状态**: ✅ 全部完成  
**完成日期**: 2025-12-25

---

## 概述

将驱动配置管理从分散的各模块集中到统一的 `config` 模块，提供：
- 集中式注册表访问
- 配置验证和默认值
- 运行时动态配置支持
- 类型安全的配置访问

## 完成状态

| 配置项 | 位置 | 状态 |
|--------|------|------|
| DebugLevel | config.c | ✅ 已迁移 |
| DebugFlags | config.c | ✅ 已迁移 |
| HeartbeatTimeoutMs | config.c | ✅ 已迁移 |
| StorageType/Path/Size | config.c | ✅ 可配置 |
| MaxIOQueues | config.c | ✅ 可配置 |
| AdminQueueDepth | config.c | ✅ 可配置 |
| IOQueueDepth | config.c | ✅ 可配置 |
| DoorbellPollIntervalUs | config.c | ✅ 可配置 |
| BatchSize | config.c | ✅ 可配置 |

---

## Phase 1: 基础配置框架 ✅

### 任务清单
- [x] 创建 `config.h` - 配置结构体和函数声明
- [x] 创建 `config.c` - 注册表读取实现
- [x] 修改 `debug.h` - 宏引用 g_Config 字段
- [x] 修改 `debug.c` - 简化为仅统计初始化
- [x] 修改 `vnvme.c` - DriverEntry 调用 VnvmeConfigInit
- [x] 验证编译

---

## Phase 2: 存储配置 ✅

### 配置项
| 注册表值 | 类型 | 默认值 | 范围 |
|----------|------|--------|------|
| StorageType | DWORD | 1 (Memory) | 0-3 |
| StoragePath | SZ | (空) | 文件路径 |
| StorageSizeGB | DWORD | 1 | 1-1024 |

### 任务清单
- [x] 扩展 VNVME_CONFIG 结构体
- [x] 修改 ctrl_dev.c 集成 VnvmeStorageCreate()
- [x] 验证编译

---

## Phase 3: 队列配置 ✅

### 配置架构
- **编译时常量** (`vnvme_common.h`): 数组大小
- **运行时配置** (`g_Config`): 实际使用限制

### 任务清单
- [x] 修改 admin_cmd.c 使用 CONFIG_MAX_IO_QUEUES
- [x] 修改 queue.c 队列验证使用 CONFIG_MAX_IO_QUEUES
- [x] 验证编译

---

## Phase 4: 性能调优配置 ✅

### 配置项
| 注册表值 | 类型 | 默认值 | 范围 |
|----------|------|--------|------|
| DoorbellPollIntervalUs | DWORD | 100 | 1-10000 |
| BatchSize | DWORD | 32 | 1-256 |

### 任务清单
- [x] 修改 doorbell.c 使用 CONFIG_POLL_INTERVAL_US
- [x] 自适应边界基于配置值动态计算
- [x] 验证编译

---

## Phase 5: IOCTL 动态配置 ✅

### 新增 IOCTL
| IOCTL | 功能码 | 功能 |
|-------|--------|------|
| IOCTL_VNVME_GET_CONFIG | 0x120 | 获取当前配置 |
| IOCTL_VNVME_SET_CONFIG | 0x121 | 设置可动态修改的配置 |

### 可动态修改的配置
- DebugLevel, DebugFlags
- HeartbeatTimeoutMs
- DoorbellPollIntervalUs, BatchSize

### 任务清单
- [x] 在 vnvme_ioctl.h 添加 IOCTL 定义
- [x] 在 ctrl_dev.c 实现 VnvmeHandleGetConfig/SetConfig
- [x] 验证编译

---

## 注册表配置位置

```
HKLM\SYSTEM\CurrentControlSet\Services\vnvme\Parameters\
  ├── DebugLevel              (DWORD)  默认: 4
  ├── DebugFlags              (DWORD)  默认: 0xFFFFFFFF
  ├── HeartbeatTimeoutMs      (DWORD)  默认: 10000
  ├── StorageType             (DWORD)  默认: 1 (Memory)
  ├── StoragePath             (SZ)     默认: (空)
  ├── StorageSizeGB           (DWORD)  默认: 1
  ├── MaxIOQueues             (DWORD)  默认: 16
  ├── AdminQueueDepth         (DWORD)  默认: 64
  ├── IOQueueDepth            (DWORD)  默认: 256
  ├── DoorbellPollIntervalUs  (DWORD)  默认: 100
  └── BatchSize               (DWORD)  默认: 32
```

---

*更新日期: 2025-12-25*
