# 构建与部署指南

## 开发环境要求

- Windows 10/11 (64-bit)
- Visual Studio 2022
- Windows Driver Kit (WDK) 10
- Windows SDK 10

## 安装 WDK

1. 下载 WDK: https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk
2. 先安装 Visual Studio 2022
3. 安装 Windows SDK
4. 安装 WDK

## 项目配置

### 创建驱动项目
1. 打开 Visual Studio 2022
2. 新建项目 → "Kernel Mode Driver (KMDF)"
3. 项目名称: VirtualNvme

### 编译配置
```
平台: x64
配置: Debug / Release
目标 OS: Windows 10 (Build 19041+)
驱动模型: KMDF
```

## 构建命令

### 命令行构建
```powershell
# 进入项目目录
cd virtual-nvme-driver

# 使用 MSBuild
msbuild VirtualNvme.sln /p:Configuration=Debug /p:Platform=x64
```

### 输出文件
```
Debug\x64\
├── vnvme.sys        # 驱动文件
├── vnvme.inf        # 安装信息文件
├── vnvme.pdb        # 调试符号
└── vnvme.cat        # 签名目录 (需要签名)
```

## 驱动签名

### 测试签名
```powershell
# 启用测试签名模式
bcdedit /set testsigning on
# 重启计算机
```

### 生产签名
需要从 Microsoft 获取 EV 代码签名证书

## 安装驱动

### 使用设备管理器
1. 打开设备管理器
2. 操作 → 添加过时硬件
3. 从磁盘安装
4. 选择 vnvme.inf

### 使用命令行
```powershell
# 安装驱动
pnputil /add-driver vnvme.inf /install

# 查看状态
sc query vnvme

# 卸载驱动
pnputil /delete-driver vnvme.inf /uninstall
```

## 调试配置

### 启用内核调试
```powershell
bcdedit /debug on
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000
```

### WinDbg 连接
```
File → Kernel Debug → Net
Port: 50000
Key: <生成的密钥>
```

### 常用调试命令
```
!drvobj vnvme        # 查看驱动对象
!devobj <addr>       # 查看设备对象
!analyze -v          # 分析崩溃
dt vnvme!*           # 显示符号
```

## 注册表配置

```
HKLM\SYSTEM\CurrentControlSet\Services\vnvme\Parameters

BackendType     REG_DWORD   0=Memory, 1=File
BackendPath     REG_SZ      文件后端路径
DiskSizeMB      REG_DWORD   虚拟磁盘大小(MB)
BlockSize       REG_DWORD   块大小(512/4096)
```
