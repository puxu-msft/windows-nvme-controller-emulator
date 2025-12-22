# 测试策略

## 测试层次

```
┌─────────────────────────────────┐
│      WHQL 认证测试              │  ← Windows 认证
├─────────────────────────────────┤
│      系统集成测试               │  ← 完整系统验证
├─────────────────────────────────┤
│      功能测试                   │  ← 功能验证
├─────────────────────────────────┤
│      单元测试                   │  ← 模块验证
└─────────────────────────────────┘
```

## 测试环境配置

### 推荐使用虚拟机测试

⚠️ **重要**: 内核驱动开发容易导致蓝屏，强烈建议在虚拟机中进行测试。

#### Hyper-V 虚拟机配置
```powershell
# 创建测试虚拟机
New-VM -Name "VNvmeTest" -MemoryStartupBytes 4GB -Generation 2

# 创建虚拟硬盘
New-VHD -Path "C:\VMs\VNvmeTest.vhdx" -SizeBytes 60GB -Dynamic

# 添加虚拟硬盘
Add-VMHardDiskDrive -VMName "VNvmeTest" -Path "C:\VMs\VNvmeTest.vhdx"

# 启用嵌套虚拟化 (可选)
Set-VMProcessor -VMName "VNvmeTest" -ExposeVirtualizationExtensions $true

# 配置调试
Set-VMComPort -VMName "VNvmeTest" -Number 1 -Path "\\.\pipe\vnvme_debug"
```

#### VMware 配置
```
1. 创建 Windows 10/11 虚拟机
2. 设置 4GB+ 内存, 60GB+ 磁盘
3. 添加串口设备用于调试 (Named Pipe)
4. 启用 "Virtualize Intel VT-x/EPT"
```

### 代码覆盖率

使用 Visual Studio 代码覆盖率工具：

```powershell
# 在构建时启用覆盖率
msbuild VirtualNvme.sln /p:Configuration=Debug /p:EnableCodeCoverage=true

# 运行测试并收集覆盖率
vstest.console.exe VNvmeTests.dll /EnableCodeCoverage

# 覆盖率目标
# - 核心模块 (Controller, Queue, Backend): ≥ 80%
# - 命令处理: ≥ 90%
# - 错误路径: ≥ 70%
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

### WHQL 认证流程

#### 1. 设置 HLK 环境
```
a. 下载 Windows HLK: https://learn.microsoft.com/windows-hardware/test/hlk/
b. 安装 HLK Controller 到测试服务器
c. 安装 HLK Studio 到开发机
d. 安装 HLK Client 到测试虚拟机
```

#### 2. 配置测试目标
```
a. 在 HLK Studio 中创建项目
b. 添加测试机器到计算机池
c. 选择设备类型: Storage > Disk
d. 选择驱动目标
```

#### 3. 运行必需测试
```
必须通过的测试类别:
□ Device Fundamentals Reliability
  - DF - PNP (Disable and Enable)
  - DF - Reboot Restart
  - DF - Sleep and PnP
  
□ Storage Tests
  - Disk Stress
  - Disk Verification
  - SCSI Compliance Test
  
□ Crash Dump Support (如支持)
```

#### 4. 提交认证
```
a. 所有必需测试通过后创建 .hlkx 包
b. 登录 Windows 硬件开发人员中心
c. 提交 .hlkx 包进行签名
d. 下载认证后的驱动包
```

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
# diskspd 示例 - 4K 随机读写
diskspd -b4K -d60 -o32 -t4 -r -w50 V:\testfile.dat

# diskspd 示例 - 顺序读取
diskspd -b1M -d60 -o8 -t1 -s -w0 V:\testfile.dat

# 结果指标
# - IOPS (随机 4K): 目标 > 50,000
# - 吞吐量 (顺序): 目标 > 500 MB/s
# - 延迟: 目标 < 1ms
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
