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
