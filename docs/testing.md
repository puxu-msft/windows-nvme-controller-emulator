# 测试策略

## 测试层次

```
┌─────────────────────────────────┐
│      系统集成测试               │
├─────────────────────────────────┤
│      功能测试                   │
├─────────────────────────────────┤
│      单元测试                   │
└─────────────────────────────────┘
```

## 单元测试

### 测试用例

| 模块 | 测试项 |
|------|--------|
| 控制器 | 初始化、状态转换、寄存器操作 |
| 队列 | 创建、删除、入队、出队 |
| 命名空间 | 创建、销毁、参数验证 |
| 后端 | 读写、刷新、边界条件 |

### 示例测试
```c
// 测试队列创建
VOID TestQueueCreate() {
    VNVME_QUEUE queue;
    NTSTATUS status = QueueCreate(&queue, 1, 64, TRUE);
    ASSERT(NT_SUCCESS(status));
    ASSERT(queue.QueueId == 1);
    ASSERT(queue.QueueSize == 64);
    QueueDestroy(&queue);
}
```

## 功能测试

### Admin 命令测试
- [ ] Identify Controller
- [ ] Identify Namespace
- [ ] Create/Delete I/O Queue
- [ ] Get/Set Features

### I/O 命令测试
- [ ] 顺序读取
- [ ] 顺序写入
- [ ] 随机读取
- [ ] 随机写入
- [ ] 混合读写
- [ ] Flush 操作

### 边界条件测试
- [ ] 最大 LBA 访问
- [ ] 零长度操作
- [ ] 无效参数处理
- [ ] 队列满/空处理

## 系统集成测试

### Windows 认证测试
使用 Windows HLK (Hardware Lab Kit):
- Device Fundamentals Tests
- Storage Tests
- Power Management Tests

### 文件系统测试
```powershell
# 格式化
Format-Volume -DriveLetter V -FileSystem NTFS

# 文件操作
Copy-Item test.dat V:\
Compare-Object (Get-FileHash test.dat) (Get-FileHash V:\test.dat)
```

### 性能测试
使用工具:
- CrystalDiskMark
- fio
- diskspd

```powershell
# diskspd 示例
diskspd -b4K -d60 -o32 -t4 -r -w50 V:\testfile.dat
```

## 压力测试

### 长时间运行
- 连续 I/O 操作 24 小时
- 随机断电恢复测试
- 内存泄漏检测

### 工具
```powershell
# Driver Verifier 启用
verifier /standard /driver vnvme.sys

# 检查结果
verifier /query
```

## 测试报告模板

```
测试名称: 
测试日期: 
测试环境: 
测试结果: PASS / FAIL
详细描述:
错误日志:
```
