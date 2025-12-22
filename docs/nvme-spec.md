# NVMe 规范概述

## NVMe 简介

NVMe (Non-Volatile Memory Express) 是专为闪存和 SSD 设计的高性能存储协议。本驱动实现 NVMe 1.4 规范的核心子集。

## 核心概念

### 控制器 (Controller)
- 管理一个或多个命名空间
- 处理管理命令和 I/O 命令
- 维护控制器状态

### 命名空间 (Namespace)
- 可寻址的逻辑块集合
- 每个命名空间有唯一的 NSID
- 支持不同的块大小和容量

### 队列 (Queues)
- **Admin Queue**: 管理命令提交/完成
- **I/O Queue**: 读写命令提交/完成
- 提交队列 (SQ) 和完成队列 (CQ) 配对

## 实现的命令

### Admin Commands (必需)

| 命令 | Opcode | 说明 |
|------|--------|------|
| Identify | 0x06 | 获取控制器/命名空间信息 |
| Create I/O CQ | 0x05 | 创建 I/O 完成队列 |
| Create I/O SQ | 0x01 | 创建 I/O 提交队列 |
| Delete I/O CQ | 0x04 | 删除 I/O 完成队列 |
| Delete I/O SQ | 0x00 | 删除 I/O 提交队列 |
| Get Features | 0x0A | 获取特性 |
| Set Features | 0x09 | 设置特性 |

### I/O Commands (必需)

| 命令 | Opcode | 说明 |
|------|--------|------|
| Read | 0x02 | 读取数据 |
| Write | 0x01 | 写入数据 |
| Flush | 0x00 | 刷新缓存 |

## 关键数据结构

### Submission Queue Entry (64 bytes)
```
Offset  Size  Field
0x00    4     Command Dword 0 (Opcode, FUSE, PSDT, CID)
0x04    4     NSID
0x08    8     Reserved
0x10    8     MPTR
0x18    8     PRP1
0x20    8     PRP2
0x28    24    Command Specific
```

### Completion Queue Entry (16 bytes)
```
Offset  Size  Field
0x00    4     Command Specific
0x04    4     Reserved
0x08    2     SQ Head Pointer
0x0A    2     SQ Identifier
0x0C    2     Command Identifier
0x0E    2     Status Field
```

## 控制器寄存器

| 偏移 | 寄存器 | 说明 |
|------|--------|------|
| 0x00 | CAP | 控制器能力 |
| 0x08 | VS | 版本 |
| 0x14 | CC | 控制器配置 |
| 0x1C | CSTS | 控制器状态 |
| 0x24 | AQA | Admin 队列属性 |
| 0x28 | ASQ | Admin SQ 基地址 |
| 0x30 | ACQ | Admin CQ 基地址 |
