# 快速入门指南

5 分钟上手 VNVME 虚拟 NVMe 控制器。

---

## 前提条件

- Windows 10/11 或 Windows Server 2019+
- 管理员权限
- 测试签名模式已启用

### 启用测试签名

```powershell
# 以管理员身份运行
bcdedit /set testsigning on
shutdown /r /t 0  # 重启生效
```

---

## 步骤 1: 获取二进制文件

从 Release 页面下载或自行编译：

```powershell
# 编译项目
msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64
```

编译产物：
- `vnvme\build\Release\x64\vnvme.sys` - 内核驱动
- `build\Release\x64\vnvme-server.exe` - 用户态服务
- `build\Release\x64\vnvmectl.exe` - 管理工具

---

## 步骤 2: 安装驱动

```powershell
# 复制文件
Copy-Item vnvme.sys C:\Windows\System32\drivers\
Copy-Item vnvme.inf C:\Windows\INF\

# 安装驱动
pnputil /add-driver C:\Windows\INF\vnvme.inf /install
```

验证安装：
```powershell
sc query vnvme
# 应显示 STATE: RUNNING
```

---

## 步骤 3: 启动服务

```powershell
# 创建 100GB 内存虚拟磁盘
vnvme-server.exe --size 100G --backend memory
```

或使用文件后端：
```powershell
# 创建存储目录
New-Item -ItemType Directory -Path C:\vnvme -Force

# 使用文件后端
vnvme-server.exe --size 100G --backend file --file C:\vnvme\disk.img
```

服务启动后会显示：
```
[INFO] VNVME Server v1.0.0
[INFO] Backend: memory (100.00 GB)
[INFO] Connected to driver
[INFO] User service ready
```

---

## 步骤 4: 验证

### 检查设备管理器

1. 打开设备管理器 (`devmgmt.msc`)
2. 展开 "存储控制器"
3. 应该看到 "Virtual NVMe Controller"

### 检查磁盘管理

1. 打开磁盘管理 (`diskmgmt.msc`)
2. 会提示初始化新磁盘
3. 选择 GPT 分区方式
4. 创建新卷并格式化

### 使用命令行工具

```powershell
# 查看驱动状态
vnvmectl.exe status

# 列出控制器
vnvmectl.exe list

# 查看统计信息
vnvmectl.exe stats
```

---

## 步骤 5: 使用虚拟磁盘

现在可以像使用真实磁盘一样使用虚拟 NVMe 磁盘：

```powershell
# 假设虚拟磁盘挂载为 E:
echo "Hello VNVME" > E:\test.txt
cat E:\test.txt
```

---

## 停止和卸载

### 停止服务
```powershell
# 在 vnvme-server 窗口按 Ctrl+C
```

### 卸载驱动
```powershell
pnputil /delete-driver vnvme.inf /uninstall /force
```

### 禁用测试签名（可选）
```powershell
bcdedit /set testsigning off
shutdown /r /t 0
```

---

## 常见问题

### Q: 驱动加载失败？
确保测试签名已启用：
```powershell
bcdedit /enum | findstr testsigning
```

### Q: 磁盘不显示？
检查 vnvme-server 是否正在运行并已连接：
```powershell
vnvmectl.exe status
```

### Q: 性能不佳？
- 使用 `--direct-io` 绕过文件缓存
- 使用内存后端进行性能测试
- 查看 [性能优化指南](operations/performance.md)

---

## 下一步

- 📖 [用户手册](operations/user-manual.md) - 完整功能说明
- 🏗️ [架构设计](architecture/overview.md) - 了解工作原理
- 🔧 [调试指南](development/debugging.md) - 问题排查
