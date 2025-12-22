# NVMe 规范概述

## NVMe 简介

NVMe (Non-Volatile Memory Express) 是专为闪存和 SSD 设计的高性能存储协议。本驱动实现 NVMe 1.4 规范的核心子集。

**规范参考**: [NVM Express Base Specification Revision 1.4](https://nvmexpress.org/specifications/)

## 核心概念

### 控制器 (Controller)
- 管理一个或多个命名空间
- 处理管理命令和 I/O 命令
- 维护控制器状态
- 每个控制器有唯一的 Controller ID (CNTLID)

### 命名空间 (Namespace)
- 可寻址的逻辑块集合
- 每个命名空间有唯一的 NSID (1 到 0xFFFFFFFE)
- NSID 0xFFFFFFFF 表示广播到所有命名空间
- 支持不同的块大小 (512B, 4KB 等) 和容量

### 队列 (Queues)
- **Admin Queue**: 管理命令提交/完成 (Queue ID = 0)
- **I/O Queue**: 读写命令提交/完成 (Queue ID ≥ 1)
- 提交队列 (SQ) 和完成队列 (CQ) 配对
- 多个 SQ 可以关联到同一个 CQ

### 队列机制
```
    Host                          Controller
    ┌──────┐                      ┌──────┐
    │  SQ  │ ──── 命令提交 ────> │      │
    │ Tail │                      │ 处理 │
    └──────┘                      │      │
    ┌──────┐                      │      │
    │  CQ  │ <─── 完成通知 ───── │      │
    │ Head │                      └──────┘
    └──────┘
```

## 实现的命令

### Admin Commands (Opcode 范围: 0x00-0x7F)

| 命令 | Opcode | 说明 | 必需 |
|------|--------|------|------|
| Delete I/O SQ | 0x00 | 删除 I/O 提交队列 | ✓ |
| Create I/O SQ | 0x01 | 创建 I/O 提交队列 | ✓ |
| Get Log Page | 0x02 | 获取日志页 | ✓ |
| Delete I/O CQ | 0x04 | 删除 I/O 完成队列 | ✓ |
| Create I/O CQ | 0x05 | 创建 I/O 完成队列 | ✓ |
| Identify | 0x06 | 获取控制器/命名空间信息 | ✓ |
| Abort | 0x08 | 终止命令 | ✓ |
| Set Features | 0x09 | 设置特性 | ✓ |
| Get Features | 0x0A | 获取特性 | ✓ |
| Async Event Request | 0x0C | 异步事件请求 | ✓ |

### NVM I/O Commands (Opcode 范围: 0x00-0x7F)

| 命令 | Opcode | 说明 | 必需 |
|------|--------|------|------|
| Flush | 0x00 | 刷新缓存到非易失存储 | ✓ |
| Write | 0x01 | 写入数据 | ✓ |
| Read | 0x02 | 读取数据 | ✓ |
| Write Uncorrectable | 0x04 | 标记块为不可纠正 | - |
| Compare | 0x05 | 比较数据 | - |
| Write Zeroes | 0x08 | 写零 | - |
| Dataset Management | 0x09 | 数据集管理 (TRIM) | - |

## 关键数据结构

### Submission Queue Entry (64 bytes)
```
Offset  Size  Field              Description
0x00    4     CDW0               命令双字 0
                                   [7:0]   OPC   - 操作码
                                   [9:8]   FUSE  - 融合操作
                                   [13:10] Reserved
                                   [15:14] PSDT  - PRP/SGL 选择
                                   [31:16] CID   - 命令标识符
0x04    4     NSID               命名空间标识符
0x08    4     CDW2               命令双字 2 (保留)
0x0C    4     CDW3               命令双字 3 (保留)
0x10    8     MPTR               元数据指针
0x18    8     PRP1/SGL1          数据指针 1 (PRP Entry 1 或 SGL Segment)
0x20    8     PRP2/SGL2          数据指针 2 (PRP Entry 2 或 SGL Segment)
0x28    4     CDW10              命令双字 10 (命令特定)
0x2C    4     CDW11              命令双字 11 (命令特定)
0x30    4     CDW12              命令双字 12 (命令特定)
0x34    4     CDW13              命令双字 13 (命令特定)
0x38    4     CDW14              命令双字 14 (命令特定)
0x3C    4     CDW15              命令双字 15 (命令特定)
```

### Completion Queue Entry (16 bytes)
```
Offset  Size  Field              Description
0x00    4     DW0                命令特定结果
0x04    4     DW1                保留
0x08    2     SQHD               提交队列头指针
0x0A    2     SQID               提交队列标识符
0x0C    2     CID                命令标识符 (对应 SQE 中的 CID)
0x0E    2     Status             状态字段
                                   [0]     P    - 阶段位
                                   [8:1]   SC   - 状态码
                                   [11:9]  SCT  - 状态码类型
                                   [13:12] Reserved
                                   [14]    M    - 更多
                                   [15]    DNR  - 不要重试
```

### Read/Write 命令参数
```
CDW10[31:0]  + CDW11[31:0] = SLBA (起始逻辑块地址, 64-bit)
CDW12[15:0]  = NLB  (逻辑块数量, 0's based, 实际数量 = NLB + 1)
CDW12[31:16] = 保留
CDW13        = DSM  (数据集管理)
CDW14        = EILBRT (期望初始逻辑块引用标签)
CDW15        = ELBAT/ELBATM (期望逻辑块应用标签)
```

## 控制器寄存器 (Controller Registers)

所有寄存器基于 BAR0 偏移，本驱动在内存中模拟这些寄存器。

| 偏移 | 大小 | 寄存器 | 说明 |
|------|------|--------|------|
| 0x00 | 8 | CAP | 控制器能力 (Controller Capabilities) |
| 0x08 | 4 | VS | 版本 (Version) |
| 0x0C | 4 | INTMS | 中断掩码设置 (Interrupt Mask Set) |
| 0x10 | 4 | INTMC | 中断掩码清除 (Interrupt Mask Clear) |
| 0x14 | 4 | CC | 控制器配置 (Controller Configuration) |
| 0x18 | 4 | Reserved | 保留 |
| 0x1C | 4 | CSTS | 控制器状态 (Controller Status) |
| 0x20 | 4 | NSSR | NVM 子系统复位 (NVM Subsystem Reset) |
| 0x24 | 4 | AQA | Admin 队列属性 (Admin Queue Attributes) |
| 0x28 | 8 | ASQ | Admin SQ 基地址 (Admin Submission Queue Base Address) |
| 0x30 | 8 | ACQ | Admin CQ 基地址 (Admin Completion Queue Base Address) |
| 0x38 | 4 | CMBLOC | 控制器内存缓冲区位置 |
| 0x3C | 4 | CMBSZ | 控制器内存缓冲区大小 |
| 0x1000+ | 4 | SQnTDBL | 提交队列 n 尾部门铃 |
| 0x1000+ | 4 | CQnHDBL | 完成队列 n 头部门铃 |

### Doorbell 寄存器计算公式

门铃寄存器地址根据队列 ID 计算，公式如下：

```
SQyTDBL (Submission Queue y Tail Doorbell):
    Offset = 0x1000 + (2y × (4 << CAP.DSTRD))

CQyHDBL (Completion Queue y Head Doorbell):
    Offset = 0x1000 + ((2y + 1) × (4 << CAP.DSTRD))
```

其中:
- `y` = 队列 ID (0 = Admin Queue, ≥1 = I/O Queue)
- `CAP.DSTRD` = 门铃跨度 (Doorbell Stride)
- 当 `CAP.DSTRD = 0` 时，跨度为 4 字节

**示例 (DSTRD = 0)**:
| 队列 | 寄存器 | 偏移计算 | 偏移值 |
|------|--------|----------|--------|
| Admin SQ (y=0) | SQ0TDBL | 0x1000 + (0 × 4) | 0x1000 |
| Admin CQ (y=0) | CQ0HDBL | 0x1000 + (1 × 4) | 0x1004 |
| I/O SQ 1 (y=1) | SQ1TDBL | 0x1000 + (2 × 4) | 0x1008 |
| I/O CQ 1 (y=1) | CQ1HDBL | 0x1000 + (3 × 4) | 0x100C |
| I/O SQ 2 (y=2) | SQ2TDBL | 0x1000 + (4 × 4) | 0x1010 |
| I/O CQ 2 (y=2) | CQ2HDBL | 0x1000 + (5 × 4) | 0x1014 |

### CAP 寄存器位定义 (64-bit)
```
Bits    Field    Description
63:56   Reserved 保留
55:52   MPSMAX   最大内存页大小 (2^(12+MPSMAX))
51:48   MPSMIN   最小内存页大小 (2^(12+MPSMIN))
47:45   Reserved 保留
44:37   CSS      命令集支持 (bit 0 = NVM Command Set)
36      NSSRS    NVM 子系统复位支持
35:32   DSTRD    门铃跨度 (2^(2+DSTRD) bytes)
31:24   TO       超时 (单位500ms)
23:19   Reserved 保留
18:17   AMS      仲裁机制支持
16      CQR      连续队列要求
15:0    MQES     最大队列条目数 (0's based, 实际值+1)
```

### CC 寄存器位定义 (32-bit)
```
Bits    Field    Description
31:24   Reserved 保留
23:20   IOCQES   I/O CQ 条目大小 (2^n bytes)
19:16   IOSQES   I/O SQ 条目大小 (2^n bytes)
15:14   SHN      关机通知 (00=无, 01=正常, 10=突然)
13:11   AMS      仲裁机制选择
10:7    MPS      内存页大小 (2^(12+MPS))
6:4     CSS      命令集选择
3:1     Reserved 保留
0       EN       启用 (1=启用控制器)
```

### CSTS 寄存器位定义 (32-bit)
```
Bits    Field    Description
31:6    Reserved 保留
5       PP       处理暂停
4       NSSRO    NVM 子系统复位发生
3:2     SHST     关机状态 (00=正常, 01=进行中, 10=完成)
1       CFS      控制器致命状态
0       RDY      就绪 (控制器准备好处理命令)
```

## Features ID 列表

Features 是控制器/命名空间的可配置属性，通过 Get Features (0x0A) 和 Set Features (0x09) 命令访问。

### Mandatory Features (必须实现)

| Feature ID | 名称 | 说明 | CDW11 参数 |
|------------|------|------|------------|
| 0x01 | Arbitration | 仲裁机制设置 | HPW[31:24], MPW[23:16], LPW[15:8], AB[2:0] |
| 0x02 | Power Management | 电源管理 | PS[4:0] 电源状态 |
| 0x04 | Temperature Threshold | 温度阈值 | TMPTH[15:0], TMPSEL[19:16], THSEL[21:20] |
| 0x05 | Error Recovery | 错误恢复 | TLER[15:0], DULBE[16] |
| 0x06 | Volatile Write Cache | 易失性写缓存 | WCE[0] 写缓存启用 |
| 0x07 | Number of Queues | 队列数量 | NCQR[31:16], NSQR[15:0] |
| 0x08 | Interrupt Coalescing | 中断合并 | TIME[15:8], THR[7:0] |
| 0x09 | Interrupt Vector Config | 中断向量配置 | IV[15:0], CD[16] |
| 0x0A | Write Atomicity Normal | 正常写原子性 | DN[0] |
| 0x0B | Async Event Config | 异步事件配置 | 事件掩码位 |

### Optional Features (可选实现)

| Feature ID | 名称 | 说明 |
|------------|------|------|
| 0x03 | LBA Range Type | LBA 范围类型 (已废弃) |
| 0x0C | Autonomous Power State Transition | 自主电源状态转换 |
| 0x0D | Host Memory Buffer | 主机内存缓冲区 |
| 0x0E | Timestamp | 时间戳 |
| 0x0F | Keep Alive Timer | 保持活动定时器 |
| 0x10 | Host Controlled Thermal Management | 主机控制的热管理 |
| 0x11 | Non-Operational Power State Config | 非操作电源状态配置 |

## Get Log Page 参数

Get Log Page (Opcode 0x02) 命令用于获取控制器维护的各种日志。

### 命令 CDW10-CDW13 参数
```
CDW10:
    [7:0]   LID    - Log Page Identifier
    [14:8]  LSP    - Log Specific Parameter
    [15]    RAE    - Retain Async Event
    [27:16] NUMDL  - Number of Dwords Lower (0's based)
CDW11:
    [15:0]  NUMDU  - Number of Dwords Upper
CDW12:
    [31:0]  LPOL   - Log Page Offset Lower
CDW13:
    [31:0]  LPOU   - Log Page Offset Upper
```

### 必须支持的日志页

| LID | 名称 | 大小 | 说明 |
|-----|------|------|------|
| 0x01 | Error Information | 64 × n | 错误信息日志 (每条目 64 字节) |
| 0x02 | SMART / Health Information | 512 | SMART 健康信息 |
| 0x03 | Firmware Slot Information | 512 | 固件槽位信息 |

### SMART / Health Information 日志 (LID = 0x02)

```
Offset  Size  Field                      Description
0x00    1     Critical Warning           临界警告标志
                                           [0] 可用备用空间低于阈值
                                           [1] 温度超过阈值
                                           [2] 可靠性降级
                                           [3] 只读模式
                                           [4] 易失性备份失败
0x01    2     Composite Temperature      复合温度 (Kelvin)
0x03    1     Available Spare            可用备用空间 (%)
0x04    1     Available Spare Threshold  可用备用阈值 (%)
0x05    1     Percentage Used            已使用寿命百分比
0x06    26    Reserved                   保留
0x20    16    Data Units Read            读取数据单元 (× 1000 × 512B)
0x30    16    Data Units Written         写入数据单元 (× 1000 × 512B)
0x40    16    Host Read Commands         主机读命令数
0x50    16    Host Write Commands        主机写命令数
0x60    16    Controller Busy Time       控制器忙时间 (分钟)
0x70    16    Power Cycles               上电周期数
0x80    16    Power On Hours             上电小时数
0x90    16    Unsafe Shutdowns           非安全关机次数
0xA0    16    Media Errors               介质/数据完整性错误数
0xB0    16    Error Log Entries          错误日志条目数
0xC0    4     Warning Composite Temp Time 警告温度时间
0xC4    4     Critical Composite Temp Time 临界温度时间
0xC8    2×8   Temperature Sensors        温度传感器 1-8
0xD8    296   Reserved                   保留
```

## Read/Write 命令 FUA 位

FUA (Force Unit Access) 位控制数据是否绕过易失性缓存直接写入/读取非易失性介质。

### CDW12 位定义 (Read/Write 命令)

```
Bits    Field    Description
15:0    NLB      Number of Logical Blocks (0's based)
25:16   Reserved 保留
26      DTYPE    Dataset Type (Write only)
29:27   Reserved 保留
30      FUA      Force Unit Access
31      LR       Limited Retry
```

### FUA 位行为

| 命令 | FUA=0 | FUA=1 |
|------|-------|-------|
| Read | 可从易失性缓存返回数据 | 必须从非易失性介质读取 |
| Write | 数据可暂存于易失性缓存 | 数据必须写入非易失性介质后才返回完成 |

### 实现建议

对于虚拟 NVMe 驱动:

```c
typedef struct _NVME_RW_COMMAND {
    // CDW0-CDW9 (标准 SQE 字段)
    UINT32 CDW0;
    UINT32 NSID;
    UINT64 Reserved;
    UINT64 MPTR;
    UINT64 PRP1;
    UINT64 PRP2;
    
    // CDW10-CDW11: SLBA (起始 LBA)
    UINT64 SLBA;
    
    // CDW12
    union {
        struct {
            UINT32 NLB      : 16;   // 逻辑块数量 (0's based)
            UINT32 Reserved1: 10;
            UINT32 DTYPE    : 4;    // 仅 Write
            UINT32 Reserved2: 1;
            UINT32 FUA      : 1;    // Force Unit Access
            UINT32 LR       : 1;    // Limited Retry
        };
        UINT32 CDW12;
    };
    
    UINT32 CDW13;   // DSM (Dataset Management)
    UINT32 CDW14;   // EILBRT
    UINT32 CDW15;   // ELBAT/ELBATM
    
} NVME_RW_COMMAND, *PNVME_RW_COMMAND;

// FUA 处理示例
NTSTATUS ProcessWriteCommand(PNVME_RW_COMMAND pCmd)
{
    // ... 写入数据 ...
    
    if (pCmd->FUA) {
        // FUA=1: 确保数据持久化
        FlushToPersistentStorage();
    }
    // FUA=0: 可以稍后刷新
    
    return STATUS_SUCCESS;
}
```
