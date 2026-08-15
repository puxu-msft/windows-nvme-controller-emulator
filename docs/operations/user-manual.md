# VNVME 用户手册

## 目录

1. [简介](#简介)
2. [系统要求](#系统要求)
3. [安装指南](#安装指南)
4. [快速开始](#快速开始)
5. [配置选项](#配置选项)
6. [命令行工具](#命令行工具)
7. [高级用法](#高级用法)
8. [故障排除](#故障排除)
9. [性能调优](#性能调优)

---

## 简介

VNVME (Virtual NVMe) 是一个 Windows 虚拟 NVMe 控制器驱动程序，可以创建虚拟 NVMe 磁盘设备。它由两个主要组件组成：

- **vnvme.sys** - 内核模式驱动程序，模拟 NVMe 控制器硬件
- **vnvme-server.exe** - 用户模式服务，处理 I/O 请求并提供存储后端

### 主要特性

- ✅ 完整的 NVMe 控制器模拟 (符合 NVMe 1.4 规范)
- ✅ 多种存储后端：内存、文件
- ✅ 零拷贝数据传输 (共享内存架构)
- ✅ 直接 I/O 支持 (绕过系统缓存)
- ✅ 事件驱动模式 (低 CPU 占用)
- ✅ 热插拔支持

---

## 系统要求

### 硬件要求
- x64 CPU (Intel/AMD)
- 最少 4GB 内存 (推荐 8GB+)
- 存储空间取决于虚拟磁盘大小

### 软件要求
- Windows 10 版本 1903 或更高
- Windows 11 (任何版本)
- Windows Server 2019/2022

### 开发/测试模式
- 需要启用测试签名模式 (Test Signing)
- 需要以管理员权限运行

---

## 安装指南

### 步骤 1：启用测试签名 (开发阶段)

```powershell
# 以管理员身份运行
bcdedit /set testsigning on
# 重启系统
shutdown /r /t 0
```

### 步骤 2：安装驱动程序

方法 A：使用设备管理器
1. 打开设备管理器
2. 操作 → 添加过时硬件
3. 选择 "手动从列表安装"
4. 选择 "显示所有设备"
5. 点击 "从磁盘安装"，选择 `vnvme.inf`

方法 B：使用命令行
```powershell
# 复制驱动文件
Copy-Item vnvme.sys C:\Windows\System32\drivers\
Copy-Item vnvme.inf C:\Windows\INF\

# 安装驱动
pnputil /add-driver vnvme.inf /install
```

### 步骤 3：验证安装

```powershell
# 检查驱动状态
sc query vnvme

# 应该显示 RUNNING 状态
```

---

## 快速开始

### 1. 启动用户态服务

```powershell
# 基本用法：创建 100GB 内存后端虚拟磁盘
vnvme-server.exe --size 100G --backend memory

# 使用文件后端
vnvme-server.exe --size 500G --backend file --file C:\vnvme\disk.img
```

### 2. 创建虚拟控制器

```powershell
# 使用命令行工具创建控制器 (128MB 内存后端)
vnvmectl.exe create --size=128M

# 使用文件后端
vnvmectl.exe create --backend=file --file=C:\vnvme\disk.img --size=1G

# 列出所有控制器
vnvmectl.exe list
```

### 3. 在磁盘管理器中查看

1. 打开磁盘管理 (`diskmgmt.msc`)
2. 会看到新的 NVMe 磁盘
3. 初始化磁盘并创建分区

### 4. 停止服务

```powershell
# 按 Ctrl+C 优雅关闭 vnvme-server
# 服务会自动清理资源并通知驱动
```

---

## 配置选项

### 命令行选项

| 选项 | 简写 | 描述 | 默认值 |
|------|------|------|--------|
| `--config <file>` | `-c` | 配置文件路径 | - |
| `--size <size>` | `-s` | 存储大小 (例如 100G, 512M) | 100G |
| `--backend <type>` | `-b` | 后端类型：memory, file | memory |
| `--file <path>` | `-f` | 文件后端路径 | - |
| `--direct-io` | - | 启用直接 I/O (绕过系统缓存) | 禁用 |
| `--preallocate` | - | 预分配文件空间 (避免碎片) | 禁用 |
| `--model <name>` | `-m` | 型号字符串 | Virtual NVMe SSD |
| `--serial <sn>` | `-n` | 序列号 | VNVME00000001 |
| `--log-level <level>` | - | 日志级别：error, warn, info, debug, verbose | info |
| `--log-file <path>` | - | 日志文件路径 | - |
| `--daemon` | - | 后台运行 | 禁用 |

### 配置文件格式

创建 `vnvme.conf` 文件：

```ini
[Storage]
Type = file
Size = 100G
File = C:\vnvme\disk.img
DirectIO = true
ReadOnly = false

[Controller]
Model = Virtual NVMe Controller
Serial = VNVME-001-2024
VendorId = 0x1234
DeviceId = 0x5678

[Log]
Level = info
Console = true
File = C:\vnvme\vnvme.log
```

使用配置文件：
```powershell
vnvme-server.exe --config vnvme.conf
```

---

## 命令行工具

### vnvmectl 命令参考

#### 基础命令

```powershell
# 显示帮助
vnvmectl.exe help

# 获取驱动版本
vnvmectl.exe version

# 获取驱动状态
vnvmectl.exe status

# 运行测试套件
vnvmectl.exe test
```

#### 控制器管理

```powershell
# 列出所有控制器
vnvmectl.exe list

# 创建控制器
vnvmectl.exe create --size=128M
vnvmectl.exe create --backend=file --file=disk.img --size=1G

# 删除控制器
vnvmectl.exe delete <controller_id>
```

#### 命名空间管理

```powershell
# 列出命名空间
vnvmectl.exe ns-list [controller_id]

# 创建命名空间
vnvmectl.exe ns-create <controller_id> [--size=<size>]

# 删除命名空间
vnvmectl.exe ns-delete <controller_id> <nsid>
```

#### 调试与监控

```powershell
# 显示详细统计信息
vnvmectl.exe stats

# 设置调试级别 (0-5) 和标志
vnvmectl.exe debug <level> [flags]
vnvmectl.exe debug 4 0xFFFF    # 启用所有调试跟踪
```

---

## 高级用法

### 直接 I/O 模式

直接 I/O 绕过系统文件缓存，适合需要精确控制缓存的场景：

```powershell
vnvme-server.exe --size 100G --backend file --file C:\vnvme\disk.img --direct-io
```

**注意事项：**
- 需要扇区对齐的读写操作
- 可能在小随机 I/O 场景下性能较低
- 适合顺序大块 I/O 操作

### 事件驱动模式

默认情况下，服务会自动尝试启用事件驱动模式以降低 CPU 占用：

```
[INFO] Event wait mode enabled (low CPU usage)
```

如果日志显示：
```
[INFO] Polling mode active (event not available)
```

则表示回退到了轮询模式。

### 多控制器配置

可以创建多个虚拟 NVMe 控制器：

```powershell
vnvmectl.exe create --id 1
vnvmectl.exe create --id 2
vnvmectl.exe create --id 3
```

每个控制器对应一个独立的 NVMe 设备。

---

## 故障排除

### 驱动未加载

症状：`sc query vnvme` 显示服务不存在

解决方案：
1. 确认测试签名已启用：`bcdedit | findstr testsigning`
2. 重新安装驱动
3. 检查 Windows 事件查看器中的系统日志

### 服务无法连接驱动

症状：`vnvme-server.exe` 报错 "Cannot open driver device"

解决方案：
1. 确认驱动已启动：`sc start vnvme`
2. 以管理员权限运行服务
3. 检查控制设备是否存在：`dir \\.\VnvmeControl`

### 磁盘未在设备管理器中显示

症状：控制器已创建，但无磁盘出现

解决方案：
1. 运行 `vnvmectl.exe status` 检查状态
2. 确认 vnvme-server 正在运行
3. 刷新设备管理器
4. 检查 stornvme 驱动状态

### 蓝屏 (BSOD)

1. 收集 minidump 文件：`C:\Windows\Minidump\`
2. 使用 WinDbg 分析：
   ```
   !analyze -v
   .load vnvme.sys
   ```
3. 参考 [../development/debugging.md](../development/debugging.md) 进行调试

---

## 性能调优

### 内存后端优化

- 适合小容量高性能场景
- 内存占用 = 虚拟磁盘大小
- 推荐用于测试和开发

### 文件后端优化

- 使用 SSD 作为后端文件存储
- 启用 `--direct-io` 避免双重缓存
- 预分配文件空间避免碎片

### 系统级优化

1. **CPU 亲和性**：将 vnvme-server 绑定到特定 CPU 核心
2. **优先级**：提高服务进程优先级
3. **内存锁定**：避免工作集被换出

---

## 常见问题 (FAQ)

**Q: 数据是否持久化？**

A: 取决于后端类型：
- `memory` 后端：服务停止后数据丢失
- `file` 后端：数据持久化到文件

**Q: 最大支持多大的虚拟磁盘？**

A: 理论上支持到 EB 级别，实际受限于：
- 内存后端：物理内存大小
- 文件后端：文件系统限制 (NTFS 约 16TB)

**Q: 可以用于生产环境吗？**

A: 当前版本为开发/测试用途，不建议用于生产环境关键数据。

---

## 获取帮助

- 完整文档：[docs/README.md](README.md)
- 架构说明：[docs/architecture-v2.md](architecture-v2.md)
- 调试指南：[docs/debugging-guide.md](debugging-guide.md)
- 问题报告：请提交 GitHub Issue
