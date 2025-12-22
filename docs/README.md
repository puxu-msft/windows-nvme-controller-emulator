# Virtual NVMe Driver 项目文档

## 项目概述

Virtual NVMe Driver 是一个纯软件实现的虚拟 NVMe SSD 设备驱动程序，运行于 Windows 操作系统。该驱动程序模拟完整的 NVMe 控制器和命名空间，使操作系统能够像访问真实 NVMe SSD 一样访问虚拟存储空间。

## 项目目标

- 实现符合 NVMe 1.4 规范的虚拟控制器
- 提供标准的 Windows 存储驱动接口
- 支持可配置的存储后端（内存/文件）
- 实现核心 NVMe 管理命令和 I/O 命令
- 提供完整的错误处理和日志记录

## 文档结构

| 文档 | 说明 |
|------|------|
| [architecture.md](architecture.md) | 系统架构设计 |
| [nvme-spec.md](nvme-spec.md) | NVMe 规范概述 |
| [driver-design.md](driver-design.md) | 驱动详细设计 |
| [data-structures.md](data-structures.md) | 核心数据结构 |
| [command-handling.md](command-handling.md) | 命令处理流程 |
| [build-guide.md](build-guide.md) | 构建与部署指南 |
| [testing.md](testing.md) | 测试策略 |

## 技术栈

- **开发语言**: C (WDK)
- **驱动模型**: WDF (Windows Driver Framework) - KMDF
- **目标平台**: Windows 10/11 x64
- **构建工具**: Visual Studio 2022 + WDK 10

## 许可证

MIT License
