# 命令处理流程

## 总体流程

```
  disk.sys (SRB 请求)
       │
       ▼
  ┌─────────────┐
  │ SRB 分发器  │  ← 解析 SCSI 命令
  └──────┬──────┘
         │
    ┌────┴────┐
    ▼         ▼
 管理命令   块 I/O
 (IOCTL)    命令
    │         │
    ▼         ▼
 NVMe      后端
 模拟器    读写
    │         │
    └────┬────┘
         ▼
  ┌─────────────┐
  │ 存储后端    │
  └─────────────┘
         │
         ▼
    完成 SRB
```

### SRB 请求处理

由于 Windows 存储栈通过 disk.sys 发送 SCSI Request Block (SRB)，我们需要处理以下常见的 SCSI 命令：

| SCSI 命令 | 操作码 | 对应操作 |
|-----------|--------|----------|
| READ(10/16) | 0x28/0x88 | 读取数据块 |
| WRITE(10/16) | 0x2A/0x8A | 写入数据块 |
| SYNCHRONIZE_CACHE | 0x35 | 刷新缓存 (Flush) |
| READ_CAPACITY | 0x25 | 返回磁盘容量 |
| INQUIRY | 0x12 | 返回设备信息 |
| MODE_SENSE | 0x1A | 返回模式页 |
| TEST_UNIT_READY | 0x00 | 检查设备就绪状态 |

## Admin 命令处理

### Identify Controller (CNS=01)
1. 填充 Controller Identify 结构
2. 返回控制器信息（VID, SSVID, SN, MN, FR 等）
3. 报告支持的特性和能力

### Identify Namespace (CNS=00)
1. 验证 NSID 有效性
2. 填充 Namespace Identify 结构
3. 返回命名空间大小、块大小等信息

### Create I/O CQ
1. 验证队列 ID 未被使用
2. 分配完成队列内存
3. 初始化队列状态
4. 返回成功状态

### Create I/O SQ
1. 验证关联的 CQ 存在
2. 分配提交队列内存
3. 建立 SQ-CQ 关联
4. 返回成功状态

## I/O 命令处理

### Read 命令
```
1. 解析命令参数 (SLBA, NLB)
2. 验证范围有效性
3. 计算数据长度
4. 调用后端读取
5. 复制数据到 PRP 列表
6. 构造完成条目
```

### Write 命令
```
1. 解析命令参数 (SLBA, NLB)
2. 验证范围有效性
3. 从 PRP 列表获取数据
4. 调用后端写入
5. 构造完成条目
```

### Flush 命令
```
1. 调用后端 Flush
2. 确保数据持久化
3. 返回完成状态
```

## PRP 处理

PRP (Physical Region Page) 是 NVMe 用于描述数据缓冲区物理地址的机制。

### PRP 条目格式
```
每个 PRP 条目是 64-bit 物理地址:
- Bits [1:0]: 必须为 0 (4 字节对齐)
- Bits [n-1:2]: 页内偏移 (n = 12 + MPS)
- Bits [63:n]: 页帧号
```

### PRP 列表解析流程
```c
NTSTATUS ProcessPrpList(
    UINT64 Prp1,
    UINT64 Prp2, 
    UINT32 DataLength,
    PVOID* Addresses,
    PUINT32 AddressCount)
{
    UINT32 pageSize = 4096;  // 假设 MPS = 0
    UINT32 offset = Prp1 & (pageSize - 1);
    UINT32 firstPageBytes = pageSize - offset;
    UINT32 index = 0;
    
    // 情况 1: 数据完全在第一页内
    if (DataLength <= firstPageBytes) {
        Addresses[index++] = (PVOID)Prp1;
        *AddressCount = index;
        return STATUS_SUCCESS;
    }
    
    // 第一页
    Addresses[index++] = (PVOID)Prp1;
    UINT32 remaining = DataLength - firstPageBytes;
    
    // 情况 2: 数据跨两页，PRP2 直接是第二页地址
    if (remaining <= pageSize) {
        Addresses[index++] = (PVOID)Prp2;
        *AddressCount = index;
        return STATUS_SUCCESS;
    }
    
    // 情况 3: 数据超过两页，PRP2 指向 PRP 列表
    // PRP 列表是连续的 PRP 条目数组
    PUINT64 prpList = (PUINT64)MapPhysicalAddress(Prp2);
    if (!prpList) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    UINT32 prpIndex = 0;
    while (remaining > 0) {
        // 检查是否跨 PRP 列表页边界
        UINT32 prpListOffset = (Prp2 + prpIndex * sizeof(UINT64)) & (pageSize - 1);
        if (prpListOffset == 0 && prpIndex > 0) {
            // PRP 列表跨页，最后一个条目指向下一个 PRP 列表页
            UINT64 nextPrpListPage = prpList[prpIndex - 1];
            UnmapPhysicalAddress(prpList);
            prpList = (PUINT64)MapPhysicalAddress(nextPrpListPage);
            prpIndex = 0;
        }
        
        Addresses[index++] = (PVOID)prpList[prpIndex++];
        remaining = (remaining > pageSize) ? (remaining - pageSize) : 0;
    }
    
    UnmapPhysicalAddress(prpList);
    *AddressCount = index;
    return STATUS_SUCCESS;
}
```

### PRP 处理注意事项
1. **对齐要求**: PRP 地址必须是 DWORD (4字节) 对齐
2. **偏移限制**: PRP1 可以有页内偏移，后续 PRP 必须页对齐
3. **列表边界**: PRP List 不能跨页边界，最后条目指向下一页
4. **内存映射**: 在内核中需要正确映射物理地址

## 错误处理

| 状态码 | 说明 |
|--------|------|
| 0x0000 | 成功 |
| 0x0001 | 无效操作码 |
| 0x0002 | 无效字段 |
| 0x0080 | LBA 超出范围 |
| 0x0081 | 容量超限 |
