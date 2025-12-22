# 命令处理流程

## 总体流程

```
  StorPort 请求
       │
       ▼
  ┌─────────────┐
  │ 请求分发器  │
  └──────┬──────┘
         │
    ┌────┴────┐
    ▼         ▼
 Admin      I/O
 命令       命令
    │         │
    ▼         ▼
 处理器    处理器
    │         │
    └────┬────┘
         ▼
  ┌─────────────┐
  │ 存储后端    │
  └─────────────┘
         │
         ▼
    完成响应
```

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

### PRP 列表解析
```c
// 单页情况
if (dataLength <= PAGE_SIZE) {
    address = PRP1;
}
// 双页情况
else if (dataLength <= 2 * PAGE_SIZE) {
    addresses[0] = PRP1;
    addresses[1] = PRP2;
}
// PRP 列表情况
else {
    addresses[0] = PRP1;
    prpList = (PUINT64)PRP2;
    for (i = 1; i < numPages; i++) {
        addresses[i] = prpList[i-1];
    }
}
```

## 错误处理

| 状态码 | 说明 |
|--------|------|
| 0x0000 | 成功 |
| 0x0001 | 无效操作码 |
| 0x0002 | 无效字段 |
| 0x0080 | LBA 超出范围 |
| 0x0081 | 容量超限 |
