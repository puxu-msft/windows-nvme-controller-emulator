# VNVME 第二阶段开发路线图

**创建日期**: 2025-12-25  
**状态**: 进行中  
**负责人**: Windows 驱动开发团队

---

## 1. 概述

### 1.1 已完成工作 (第一阶段)

| 状态 | 优先级 | 项目 | 完成日期 |
|------|--------|------|----------|
| ✅ | P0 | 用户崩溃数据一致性 | 2025-12-25 |
| ✅ | P1 | 全局 FDO 指针线程安全 | 2025-12-25 |
| ✅ | P1 | PRP 物理地址验证 | 2025-12-25 |
| ✅ | P1 | Admin 命令完整实现 | 2025-12-25 |
| ✅ | P2 | 内存屏障优化 | 2025-12-25 |
| ✅ | P2 | IOCTL 输入验证 | 2025-12-25 |
| ✅ | P2 | 重复代码消除 | 2025-12-25 |
| ✅ | P2 | 心跳超时可配置 | 2025-12-25 |

### 1.2 待处理工作

| 优先级 | 项目 | 复杂度 | 预估工时 | 风险等级 |
|--------|------|--------|----------|----------|
| **P1** | MmMapIoSpace 热路径优化 | 🔴 高 | 5 天 | 中 |
| **P2** | 异步存储 I/O | 🔴 高 | 5 天 | 中 |
| **P2** | 共享内存 DACL 配置 | 🟡 中 | 2 天 | 低 |
| **P2** | 命令并行化 | 🔴 高 | 5 天 | 中 |
| **P3** | 单实例保护 | 🟢 低 | 0.5 天 | 低 |
| **P3** | 测试覆盖率提升 | 🟡 中 | 10 天 | 低 |

---

## 2. 任务分析与设计

### 2.1 P1: MmMapIoSpace 热路径优化

#### 问题描述

当前每次 I/O 命令都调用 `MmMapIoSpaceEx` / `MmUnmapIoSpace`：

```c
// io_cmd.c - 每次 I/O 都执行
for (i = 0; i < prpEntryCount; i++) {
    prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READWRITE | PAGE_NOCACHE);
    RtlCopyMemory(prpVa, buffer, mapSize);
    MmUnmapIoSpace(prpVa, mapSize);
}
```

**性能影响**:
- `MmMapIoSpace` 需要修改页表，开销约 1-5 微秒
- 高 IOPS 场景下（10K IOPS），每秒调用 10K+ 次
- 累积开销可达 10-50ms/秒

#### 解决方案选项

| 方案 | 描述 | 优点 | 缺点 | 推荐 |
|------|------|------|------|------|
| A | PRP 映射缓存 | 实现简单 | 缓存命中率不确定 | ⭐ |
| B | MDL + MmMapLockedPages | 标准做法 | 需要修改架构 | ⭐⭐ |
| C | 预映射常用区域 | 性能最优 | 内存占用大 | - |

#### 推荐方案: B - MDL 方式

**设计要点**:

1. 创建 `VnvmeMapPrpBuffer()` 替代直接 MmMapIoSpace
2. 使用 MDL 描述 PRP 物理地址
3. 批量映射减少系统调用

```c
// 新 API 设计
typedef struct _VNVME_MAPPED_BUFFER {
    PMDL Mdl;
    PVOID MappedAddress;
    SIZE_T Length;
} VNVME_MAPPED_BUFFER, *PVNVME_MAPPED_BUFFER;

NTSTATUS VnvmeMapPrpBuffer(
    _In_ ULONGLONG PhysicalAddress,
    _In_ SIZE_T Length,
    _Out_ PVNVME_MAPPED_BUFFER Buffer
);

VOID VnvmeUnmapPrpBuffer(
    _In_ PVNVME_MAPPED_BUFFER Buffer
);
```

#### 实施步骤

1. [ ] 在 prp.c 中实现 MDL 映射辅助函数
2. [ ] 更新 io_cmd.c 使用新 API
3. [ ] 更新 admin_cmd.c 使用新 API
4. [ ] 性能测试对比

#### 决策: 延后

**原因**:
- 当前架构使用用户态处理为主，内核态 I/O 是备用路径
- 用户态处理时 PRP 映射在驱动外完成
- 性能改进主要影响内核备用模式

**行动**: 记录为技术债务，在内核模式使用率提升后优先处理

---

### 2.2 P2: 共享内存 DACL 配置

#### 问题描述

技术审查指出共享内存 DACL 硬编码。

#### 现状分析

检查代码后发现，控制设备（`\\Device\\VNVMEControl`）**已有合理的 SDDL 配置**：

```c
// ctrl_dev.c 第 182 行
DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
```

SDDL 含义：
- `D:P` - DACL Protected (不继承父级权限)
- `(A;;GA;;;SY)` - 允许 SYSTEM 账户完全访问
- `(A;;GA;;;BA)` - 允许 Administrators 组完全访问

这意味着：
- ✅ 普通用户无法访问控制设备
- ✅ 只有 SYSTEM 和管理员可以映射共享内存
- ✅ vnvme-server 作为 Windows 服务运行时，以 SYSTEM 身份访问

#### 决策: 无需修改

**原因**:
1. 当前 SDDL 已是安全配置
2. 共享内存通过控制设备 IOCTL 映射，继承控制设备权限
3. 添加可配置 SDDL 增加复杂度，且可能引入安全漏洞

**建议**:
- 如需允许特定服务账户，在服务安装时将其加入 Administrators 组
- 或修改 SDDL 添加特定服务 SID（需重新编译驱动）

---

### 2.3 P2: 异步存储 I/O

#### 问题描述

用户态服务 `backend_file.c` 使用同步 I/O：

```c
// 同步读取 - 阻塞调用线程
ReadFile(FileHandle, buffer, size, &bytesRead, NULL);
```

**影响**:
- 单线程处理，I/O 等待期间无法处理其他命令
- 高延迟磁盘会拖慢整体性能

#### 解决方案

使用 Windows Overlapped I/O + IOCP：

```c
// 异步读取
OVERLAPPED ov = {0};
ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
ReadFile(FileHandle, buffer, size, NULL, &ov);
// ... 处理其他命令 ...
GetOverlappedResult(FileHandle, &ov, &bytesRead, TRUE);
```

#### 决策: 延后

**原因**:
- 需要重构 command_engine.c 的处理流程
- 引入 IOCP 需要较大改动
- 当前单线程架构满足基本需求

**行动**: 作为 v3 版本的主要改进

---

### 2.4 P3: 单实例保护

#### 问题描述

驱动支持单实例但未做显式检查：

```c
// vnvme.c - 直接覆盖
InterlockedExchangePointer((PVOID*)&g_FdoContext, fdoContext);
```

#### 解决方案

添加 Compare-And-Swap 检查：

```c
if (InterlockedCompareExchangePointer((PVOID*)&g_FdoContext, 
                                       fdoContext, NULL) != NULL) {
    TRACE_ERROR("Only one instance supported");
    return STATUS_DEVICE_ALREADY_ATTACHED;
}
```

#### 决策: 实施

**优先级**: P3 - 低  
**预估工时**: 0.5 天  
**风险**: 极低

---

## 3. 开发计划

### 第二阶段计划 (已完成)

| 序号 | 任务 | 预估 | 状态 |
|------|------|------|------|
| 1 | P3: 单实例保护 | 0.5 天 | ✅ 已完成 |
| 2 | P2: 共享内存 DACL 配置 | - | ✅ 已确认无需修改 |
| 3 | 文档更新 | 0.5 天 | ✅ 已完成 |

### 技术债务 (后续处理)

| 任务 | 触发条件 |
|------|----------|
| MmMapIoSpace 优化 | 内核模式使用率 > 30% |
| 异步存储 I/O | 性能测试显示瓶颈 |
| 命令并行化 | 用户反馈需求 |

---

## 4. 验收标准

### 4.1 单实例保护 ✅

- [x] 尝试安装第二个实例返回 `STATUS_DEVICE_ALREADY_ATTACHED`
- [x] 单实例正常工作不受影响
- [x] 驱动卸载后可重新安装

### 4.2 共享内存 DACL 配置 ✅

- [x] 默认配置仅允许 SYSTEM 和 Administrators (已确认)
- [x] 控制设备 SDDL: `D:P(A;;GA;;;SY)(A;;GA;;;BA)`
- [x] 非授权进程无法打开控制设备

---

## 5. 风险评估

| 风险 | 可能性 | 影响 | 缓解措施 |
|------|--------|------|----------|
| DACL 配置错误导致服务无法启动 | 中 | 高 | 提供回退机制 |
| 单实例检查影响热插拔 | 低 | 中 | 测试 PnP 场景 |

---

## 6. 附录

### A. 相关文件

**内核驱动**:
- `vnvme/vnvme.c` - 单实例保护
- `vnvme/shm.c` - DACL 配置

**用户态服务**:
- `vnvme-server/backend_file.c` - 异步 I/O (延后)

### B. 参考资料

- [Security Descriptors](https://docs.microsoft.com/en-us/windows/win32/secauthz/security-descriptors)
- [MmMapIoSpace Documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmmapiospace)

---

*更新日期: 2025-12-25*
