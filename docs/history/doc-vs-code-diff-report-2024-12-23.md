# 文档示例代码 vs 实际代码 差异报告

**日期**: 2024-12-23  
**背景**: 清理 architecture-v2.md 时发现文档示例与实际代码存在差异

---

## 1. VNVME_FDO_CONTEXT 结构

### 文档版本 (已从 architecture-v2.md 删除)

```c
typedef struct _VNVME_FDO_CONTEXT {
    // === 标识 ===
    BOOLEAN                 IsFdo;              // TRUE = FDO, FALSE = PDO
    ULONG                   Signature;          // 'FDOV'
    
    // === WDF 对象 ===
    WDFDEVICE               WdfDevice;
    WDFCHILDLIST            ChildList;
    
    // === 控制设备 ===
    WDFDEVICE               ControlDevice;
    WDFQUEUE                ControlQueue;
    
    // === 用户态通信 ===
    BOOLEAN                 UserModeReady;
    LARGE_INTEGER           LastHeartbeat;
    KEVENT                  CommandReadyEvent;
    HANDLE                  UserEventHandle;
    
    // === 共享内存 ===
    PVOID                   SharedMemoryKernel;
    PVOID                   SharedMemoryUser;
    PHYSICAL_ADDRESS        SharedMemoryPhys;
    SIZE_T                  SharedMemorySize;
    PMDL                    SharedMemoryMdl;
    
    // === 子设备管理 ===
    ULONG                   ControllerCount;
    ULONG                   MaxControllers;
    KSPIN_LOCK              ChildListLock;
} VNVME_FDO_CONTEXT;
```

### 实际代码 (vnvme.h L38-67)

```c
typedef struct _VNVME_FDO_CONTEXT {
    /* WDF 设备对象 */
    WDFDEVICE Device;                           // 文档: WdfDevice
    WDFDEVICE ControlDevice;                    // ✓ 相同
    
    /* 共享内存 */
    PVOID SharedMemoryKernelVa;                 // ✅ 已改为文档风格
    PHYSICAL_ADDRESS SharedMemoryPhys;          // ✅ 已改为文档风格
    SIZE_T SharedMemorySize;                    // ✓ 相同
    PMDL SharedMemoryMdl;                       // ✓ 相同
    PVOID SharedMemoryUserVa;                   // ✓ 相同
    
    /* 子设备管理 */
    LIST_ENTRY ChildDeviceList;                 // 文档: WDFCHILDLIST ChildList
    KSPIN_LOCK ChildDeviceListLock;             // 文档: ChildListLock
    ULONG ChildDeviceCount;                     // 文档: ControllerCount
    ULONG NextControllerId;                     // 文档: MaxControllers
    
    /* 用户态通信 */
    KEVENT CommandEvent;                        // 文档: CommandReadyEvent
    KEVENT UserReadyEvent;                      // ⬆️ 新增
    volatile BOOLEAN UserReady;                 // 文档: UserModeReady
    ULONG UserPid;                              // ⬆️ 新增
    LARGE_INTEGER LastHeartbeat;                // ✓ 相同
    
    /* 统计 */
    volatile LONG64 CommandsProcessed;          // ⬆️ 新增
    volatile LONG64 ErrorCount;                 // ⬆️ 新增
    
} VNVME_FDO_CONTEXT;
```

### 差异分析表

| 功能 | 文档成员 | 实际成员 | 差异类型 | 修复状态 |
|------|----------|----------|----------|----------|
| 设备对象 | `WdfDevice` | `Device` | 🔄 重命名 | ⬜ 待定 |
| 子设备列表 | `WDFCHILDLIST ChildList` | `LIST_ENTRY ChildDeviceList` | ⚠️ 类型不同 | ⬜ 保留现状 |
| 控制器计数 | `ControllerCount` | `ChildDeviceCount` | 🔄 重命名 | ⬜ 待定 |
| 最大控制器 | `MaxControllers` | `NextControllerId` | ⚠️ 语义不同 | ✅ 添加 MaxControllers |
| 共享内存 | `SharedMemoryKernel` | `SharedMemoryKernelVa` | 🔄 重命名 | ✅ 已改进 |
| 共享内存物理 | `SharedMemoryPhys` | `SharedMemoryPhys` | ✓ 相同 | ✅ 已修复 |
| 用户态地址 | `SharedMemoryUser` | `SharedMemoryUserVa` | 🔄 重命名 | ✓ 已有 |
| 命令事件 | `CommandReadyEvent` | `CommandEvent` | 🔄 重命名 | ⬜ 待定 |
| 用户态就绪 | `UserModeReady` | `UserReady` | 🔄 重命名 | ⬜ 待定 |
| 标识 | `IsFdo`, `Signature` | ✓ | ✓ 相同 | ✅ 已添加 |
| 控制队列 | `ControlQueue` | (无) | ❌ 缺失 | ⬜ 不需要 |
| 用户事件句柄 | `UserEventHandle` | (无) | ❌ 缺失 | ⬜ 暂不添加 |
| 统计计数 | (无) | `CommandsProcessed`, `ErrorCount` | ✅ 新增 | ✓ 已有 |
| 用户态PID | (无) | `UserPid` | ✅ 新增 | ✓ 已有 |
| 就绪事件 | (无) | `UserReadyEvent` | ✅ 新增 | ✓ 已有 |

---

## 2. VNVME_PDO_CONTEXT 结构

### 文档版本 (已从 architecture-v2.md 删除)

```c
typedef struct _VNVME_PDO_CONTEXT {
    // 标识
    BOOLEAN                 IsFdo;
    ULONG                   Signature;
    ULONG                   ControllerIndex;
    
    // 父 FDO 引用
    PVNVME_FDO_CONTEXT      ParentFdo;
    
    // 设备对象
    PDEVICE_OBJECT          PhysicalDeviceObject;
    PDEVICE_OBJECT          AttachedDevice;
    
    // BAR0 内存
    PVOID                   Bar0VirtAddr;
    PHYSICAL_ADDRESS        Bar0PhysAddr;
    ULONG                   Bar0Size;
    
    // NVMe 寄存器
    volatile PNVME_REGISTERS  Registers;
    volatile PULONG           Doorbells;
    
    // 控制器状态
    VNVME_CONTROLLER_STATE  State;
    ULONG                   CachedCC;
    
    // 队列
    VNVME_QUEUE             AdminSQ, AdminCQ;
    USHORT                  AdminSQTailCached, AdminCQHeadCached;
    VNVME_QUEUE             IoSQ[VNVME_MAX_IO_QUEUES];
    VNVME_QUEUE             IoCQ[VNVME_MAX_IO_QUEUES];
    USHORT                  IoQueueCount, MaxIoQueues;
    
    // 轮询
    WDFTIMER                PollTimer;
    ULONG                   PollIntervalUs;
    BOOLEAN                 PollingActive;
    
    // 命名空间
    VNVME_NAMESPACE         Namespaces[VNVME_MAX_NAMESPACES];
    ULONG                   NamespaceCount;
    
    // 统计
    ULONG64                 CommandsProcessed, BytesRead, BytesWritten;
} VNVME_PDO_CONTEXT;
```

### 差异分析表

| 功能 | 文档 | 实际 | 差异 | 修复状态 |
|------|------|------|------|----------|
| 控制器索引 | `ControllerIndex` | `ControllerId` | 🔄 重命名 | ⬜ 待定 |
| 父设备 | `PVNVME_FDO_CONTEXT ParentFdo` | `WDFDEVICE ParentFdo` | ⚠️ 类型不同 | ⬜ 保留现状 |
| BAR0 大小 | `ULONG` | `SIZE_T` | 🔄 类型升级 | ✓ 已有 |
| 寄存器类型 | `PNVME_REGISTERS` | `PNVME_CONTROLLER_REGISTERS` | 🔄 类型名 | ✓ 已有 |
| Doorbell 指针 | `volatile PULONG Doorbells` | (之前无) | ❌ 缺失 | ✅ 已添加 |
| 控制器状态 | `VNVME_CONTROLLER_STATE State` | `ControllerEnabled + ControllerReady` | ⚠️ 分解 | ⬜ 保留现状 |
| CC 缓存 | `CachedCC` | (之前无) | ❌ 缺失 | ✅ 已添加 |
| 队列类型 | `VNVME_QUEUE` | `VNVME_QUEUE_STATE` | 🔄 类型名 | ✓ 已有 |
| Tail/Head 缓存 | `USHORT AdminSQTailCached` | `ULONG LastAdminSqTail` | 🔄 类型+命名 | ✓ 已有 |
| 定时器 | `PollTimer` | `PollingTimer` | 🔄 重命名 | ⬜ 待定 |
| 轮询间隔 | `PollIntervalUs` | `PollingIntervalUs` | 🔄 重命名 | ⬜ 待定 |
| 最大I/O队列 | `MaxIoQueues` | (无) | ❌ 缺失 | ✅ 待添加 |
| 命名空间 | `Namespaces[]`, `NamespaceCount` | (无) | ❌ 缺失 | ✅ 待添加 |
| I/O 统计 | `BytesRead`, `BytesWritten` | (无) | ❌ 缺失 | ✅ 待添加 |
| 标识 | `IsFdo`, `Signature` | (无) | ❌ 缺失 | ✅ 已添加 |
| PCIe 配置 | (无) | `PcieConfig`, `PcieConfigSize` | ✅ 新增 | ✓ 已有 |
| 链表 | (无) | `ListEntry` | ✅ 新增 | ✓ 已有 |

---

## 3. VnvmeAllocateBar0 函数

### 差异对比

| 行号 | 文档代码 | 实际代码 | 差异 | 修复状态 |
|------|----------|----------|------|----------|
| 参数 | `PdoCtx` | `PdoContext` | 🔄 参数名 | ✓ 已有 |
| 分配 | `MmAllocateContiguousMemorySpecifyCache()` | `VNVME_ALLOC_POOL()` | ⚠️ 分配方式 | ✅ 已修复 |
| 缓存 | `MmNonCached` | `NonPagedPoolNx` | ⚠️ 内存类型 | ✅ 已修复 |
| 清零 | (隐含) | `RtlZeroMemory()` | ✅ 显式 | ✓ 已有 |
| 物理地址 | `MmGetPhysicalAddress()` 赋值 | (之前无) | ❌ 缺失 | ✅ 已修复 |
| 日志 | (无) | `TRACE_INFO/ERROR` | ✅ 新增 | ✓ 已有 |
| 初始化 | `VnvmeInitRegisters(Ctx)` | `VnvmeInitializeBar0Registers()` | 🔄 函数名 | ✓ 已有 |

---

## 4. VnvmeInitializeBar0Registers 函数

### CAP 寄存器值对比

| 字段 | 文档值 | 原实际值 | 修复后 | 修复状态 |
|------|--------|----------|--------|----------|
| MQES | 4095 | 4095 | 4095 | ✓ |
| CQR | 1 | 1 | 1 | ✓ |
| AMS | 0 | 0 | 0 | ✓ |
| TO | 40 (20s) | 255 (127.5s) | 40 (20s) | ✅ 已修复 |
| DSTRD | 0 | 0 | 0 | ✓ |
| NSSRS | 0 | 1 (注释错误) | 0 | ✅ 已修复 |
| CSS | 1 | 1 | 1 | ✓ |
| MPSMIN | 0 | 0 | 0 | ✓ |
| MPSMAX | 0 | 0 | 0 | ✓ |

### 其他差异

| 项目 | 文档 | 原实际 | 修复后 | 修复状态 |
|------|------|--------|--------|----------|
| Registers 赋值 | 有 | 无 | 有 | ✅ 已修复 |
| Doorbells 赋值 | 有 | 无 | 有 | ✅ 已修复 |
| CachedCC 初始化 | (隐含) | 无 | 有 | ✅ 已修复 |
| 安全检查 | 无 | 有 | 有 | ✓ 保留 |
| 详细注释 | 无 | 简单 | 详细 | ✅ 已改进 |

---

## 修复总结

### 已修复 ✅

1. **bar0.c**:
   - 使用 `MmAllocateContiguousMemorySpecifyCache` (物理连续+非缓存)
   - 添加 `Bar0PhysAddr` 赋值
   - 使用 `MmFreeContiguousMemory` 释放
   - 添加 `Registers` 指针赋值
   - 添加 `Doorbells` 指针赋值
   - 修正 `CAP.TO` 值为 40
   - 添加详细的位域注释

2. **vnvme.h FDO 上下文**:
   - ✅ 添加 `IsFdo` 标识
   - ✅ 添加 `Signature` 标识 ('FDOV')
   - ✅ 添加 `MaxControllers` 成员
   - ✅ 添加签名常量 `VNVME_FDO_SIGNATURE`

3. **vnvme.h PDO 上下文**:
   - ✅ 添加 `IsFdo` 标识
   - ✅ 添加 `Signature` 标识 ('PDOV')
   - ✅ 添加 `Doorbells` 成员
   - ✅ 添加 `CachedCC` 成员
   - ✅ 添加 `MaxIoQueues` 成员
   - ✅ 添加 `NamespaceCount` 成员
   - ✅ 添加 `NamespaceSizes[]` 数组
   - ✅ 添加 `BytesRead` 统计
   - ✅ 添加 `BytesWritten` 统计
   - ✅ 添加签名常量 `VNVME_PDO_SIGNATURE`
   - ✅ 添加 `VNVME_MAX_NAMESPACES` 常量

4. **vnvme.c FDO 初始化**:
   - ✅ 设置 `IsFdo = TRUE`
   - ✅ 设置 `Signature = VNVME_FDO_SIGNATURE`
   - ✅ 设置 `MaxControllers = VNVME_MAX_CONTROLLERS`

5. **doorbell.c**:
   - 实现 CC 寄存器变化检测
   - 自动设置 CSTS.RDY
   - 使用 Doorbells 指针简化访问

### 待修复 ⬜

1. **bus.c PDO 创建** (Phase 2 工作):
   - PDO 创建时需设置 `IsFdo = FALSE`
   - PDO 创建时需设置 `Signature = VNVME_PDO_SIGNATURE`
   - PDO 创建时需设置 `MaxIoQueues`

### 保留现状 (设计决策)

- `LIST_ENTRY` vs `WDFCHILDLIST`: 当前实现使用手动链表
- `ControllerEnabled + ControllerReady` vs `State` 枚举: 布尔更简单
- 成员命名差异: 实际代码更符合 WDF 风格

---

## 附录: CAP 寄存器位域打包

```
CAP = 0x0028_0000_0001_0FFF

Bits [15:0]   MQES   = 0x0FFF = 4095
Bit  [16]     CQR    = 1
Bits [18:17]  AMS    = 0
Bits [23:19]  Reserved = 0
Bits [31:24]  TO     = 0x28 = 40 (20 seconds)
Bits [35:32]  DSTRD  = 0
Bit  [36]     NSSRS  = 0
Bits [44:37]  CSS    = 1
Bits [55:48]  MPSMIN = 0
Bits [59:52]  MPSMAX = 0
```
