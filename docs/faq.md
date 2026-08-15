# 常见问题 (FAQ)

## 安装和配置

### Q: 为什么需要启用测试签名？

VNVME 驱动使用自签名证书，Windows 默认不加载未签名的内核驱动。启用测试签名模式允许加载测试证书签名的驱动。

```powershell
bcdedit /set testsigning on
# 需要重启生效
```

**生产环境**: 需要使用 EV 代码签名证书对驱动进行正式签名。

---

### Q: 驱动安装后设备管理器显示黄色感叹号？

常见原因：
1. **测试签名未启用** - 运行 `bcdedit /enum | findstr testsigning`
2. **缺少依赖文件** - 确保 vnvme.sys 和 vnvme.inf 都在正确位置
3. **WDF 版本不匹配** - 确保系统安装了正确版本的 WDF

---

### Q: 如何完全卸载驱动？

```powershell
# 停止服务
sc stop vnvme

# 删除驱动
pnputil /delete-driver vnvme.inf /uninstall /force

# 可选：清理文件
Remove-Item C:\Windows\System32\drivers\vnvme.sys -Force
```

---

## 使用问题

### Q: vnvme-server 启动后磁盘不显示？

检查步骤：
1. 确认驱动已加载：`sc query vnvme`
2. 确认服务已连接：查看 vnvme-server 日志是否显示 "Connected to driver"
3. 检查状态：`vnvmectl.exe status`
4. 尝试刷新磁盘管理：操作 → 重新扫描磁盘

---

### Q: 文件后端支持的最大容量是多少？

理论上支持到 NTFS 文件系统的最大文件大小（约 16 EB）。实际限制取决于：
- 可用磁盘空间
- 系统内存（用于共享内存缓冲区）
- 文件系统配置

推荐做法：
- 使用 `--preallocate` 预分配空间避免碎片
- 使用 `--direct-io` 绕过文件缓存提高性能

---

### Q: 内存后端的数据会持久化吗？

**不会**。内存后端的数据仅存在于内存中，vnvme-server 停止后数据会丢失。

如需持久化存储，请使用文件后端：
```powershell
vnvme-server.exe --backend file --file C:\vnvme\disk.img --size 100G
```

---

### Q: 如何提高性能？

1. **使用内存后端**进行性能测试（最快）
2. **启用直接 I/O**：`--direct-io`
3. **预分配文件空间**：`--preallocate`
4. **将文件后端放在 SSD 上**
5. 参考 [性能优化指南](operations/performance.md)

---

## 开发问题

### Q: 如何调试内核驱动？

1. 配置 WinDbg 内核调试（本地或网络）
2. 加载符号：`.reload /f vnvme.sys`
3. 设置断点：`bp vnvme!DriverEntry`

详细步骤请参考 [调试指南](development/debugging.md)。

---

### Q: 如何查看驱动的调试输出？

方法 1：使用 DebugView
1. 以管理员身份运行 DebugView
2. 启用 Capture → Capture Kernel
3. 查找 `[VNVME:` 开头的消息

方法 2：调整调试级别
```powershell
# 设置调试级别 (0-5)
Set-ItemProperty -Path "HKLM:\SYSTEM\CurrentControlSet\Services\vnvme\Parameters" `
    -Name "DebugLevel" -Value 4 -Type DWord
```

---

### Q: 用户态服务崩溃后会发生什么？

驱动会在 10 秒内检测到用户态崩溃（心跳超时），并：
1. 将所有待处理命令标记为错误
2. 自动切换到内核态处理模式（返回 I/O 错误）
3. 等待用户态服务重新连接

重新启动 vnvme-server 后会自动恢复。

---

### Q: 支持哪些 NVMe 命令？

**Admin 命令**:
- Identify Controller/Namespace
- Create/Delete I/O Completion Queue
- Create/Delete I/O Submission Queue
- Get/Set Features
- Abort

**I/O 命令**:
- Read
- Write
- Flush

---

## 兼容性

### Q: 支持哪些 Windows 版本？

| 版本 | 支持状态 |
|------|---------|
| Windows 10 1903+ | ✅ 完全支持 |
| Windows 11 | ✅ 完全支持 |
| Windows Server 2019 | ✅ 完全支持 |
| Windows Server 2022 | ✅ 完全支持 |
| 早期版本 | ❌ 不支持 |

---

### Q: 能在虚拟机中运行吗？

可以，但有限制：
- **VMware/VirtualBox**: 支持，但需要嵌套虚拟化
- **Hyper-V**: 支持，建议关闭动态内存
- **注意**: 虚拟机中的性能会低于物理机

---

### Q: 能与其他虚拟化软件共存吗？

是的，VNVME 作为软件驱动运行，不依赖硬件虚拟化，可以与 Hyper-V、VMware 等共存。

---

## 其他问题

### Q: 这个项目的许可证是什么？

MIT License - 可以自由使用、修改和分发。

### Q: 如何报告 Bug？

请在 GitHub Issues 中提交，包含：
1. 系统版本
2. 复现步骤
3. 错误日志
4. 如有蓝屏，提供 dump 文件

### Q: 如何贡献代码？

1. Fork 项目
2. 阅读 [编码规范](development/coding-standards.md)
3. 提交 Pull Request
