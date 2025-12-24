# VNVME 发布说明

## 版本历史

---

## v1.0.0 (2024-12-24)

### 🎉 首个稳定版本

这是 VNVME 虚拟 NVMe 驱动程序的首个稳定版本，提供完整的虚拟 NVMe 控制器模拟功能。

### 新功能

#### 核心功能
- ✅ **完整的 NVMe 控制器模拟** - 符合 NVMe 1.4 规范
- ✅ **PCIe 设备模拟** - 包含 BAR0、配置空间、MSI-X 中断
- ✅ **Admin 命令支持** - Identify、Create/Delete Queue、Set/Get Features
- ✅ **I/O 命令支持** - Read、Write、Flush、Write Zeroes、Dataset Management

#### 用户态服务 (vnvme-server)
- ✅ **零拷贝架构** - 共享内存直接访问，无需 IOCTL 传输数据
- ✅ **多后端支持** - 内存后端、文件后端
- ✅ **直接 I/O** - 支持 FILE_FLAG_NO_BUFFERING 绕过系统缓存
- ✅ **事件驱动** - 低 CPU 占用的等待模式
- ✅ **心跳机制** - 用户态崩溃自动检测和恢复

#### 管理工具 (vnvmectl)
- ✅ **控制器管理** - 创建、删除、列出控制器
- ✅ **命名空间管理** - 创建、删除、列出命名空间
- ✅ **状态查询** - 驱动版本、运行状态

#### 可靠性
- ✅ **优雅关闭** - 正确处理服务停止和驱动卸载
- ✅ **心跳超时** - 10秒超时自动切换到内核模式
- ✅ **用户态崩溃检测** - 自动故障转移到内核态处理
- ✅ **资源清理** - 完整的内存和句柄清理
- ✅ **I/O 队列停止** - 使用 WdfIoQueueStop 优雅停止

### 技术规格

| 项目 | 规格 |
|------|------|
| 语言标准 | 驱动 C17, 用户态 C++23 |
| WDK 版本 | 10.0.26100.0 |
| KMDF 版本 | 1.15 |
| 平台 | x64 only |
| 最小 Windows | Windows 10 1903 |

### 已知限制

1. **测试签名** - 当前版本需要启用测试签名模式
2. **单命名空间** - 每个控制器当前仅支持一个命名空间
3. **同步 I/O** - 后端 I/O 目前使用同步模式

### 安装

请参考 [用户手册](user-manual.md) 的安装指南部分。

### 文件列表

```
vnvme/
├── vnvme.sys           # 内核驱动程序
├── vnvme.inf           # 驱动安装信息文件
├── vnvme.cat           # 驱动目录文件 (签名后)
└── vnvme.pdb           # 调试符号

vnvme-server/
├── vnvme-server.exe    # 用户态服务
└── vnvme.conf.example  # 配置文件示例

vnvmectl/
└── vnvmectl.exe        # 管理工具

docs/
├── user-manual.md      # 用户手册
├── api-reference.md    # API 参考
└── debugging-guide.md  # 调试指南
```

---

## 升级说明

### 从开发版升级

1. 停止 vnvme-server 服务
2. 卸载旧版驱动：`pnputil /delete-driver vnvme.inf /uninstall`
3. 安装新版驱动
4. 启动 vnvme-server 服务

### 配置兼容性

v1.0.0 的配置文件格式与之前的开发版本兼容，无需修改。

---

## 贡献者

感谢所有为 VNVME 项目做出贡献的开发者。

---

## 许可证

本项目遵循 MIT 许可证发布。

---

## 反馈

如有问题或建议，请提交 GitHub Issue。
