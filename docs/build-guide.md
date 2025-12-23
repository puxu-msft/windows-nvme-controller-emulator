# 构建指南

本文档说明如何设置开发环境并构建虚拟 NVMe 控制器仿真器。

## 开发环境要求

### 软件要求

| 组件 | 版本 | 说明 |
|------|------|------|
| Windows | 10/11 64-bit | 开发和测试环境 |
| Visual Studio | 2019/2022 | 包含 C++ 桌面开发工作负载 |
| Windows SDK | 10.0.19041.0+ | 用于用户模式组件 |
| WDK | 10.0.19041.0+ | Windows Driver Kit |
| Git | 2.x | 版本控制 |

### 硬件要求

- CPU: x64 处理器
- RAM: 8GB+（推荐 16GB+）
- 存储: 20GB+ 可用空间

## 安装步骤

### 1. 安装 Visual Studio

1. 下载 Visual Studio 2022 Community 或更高版本
2. 运行安装程序，选择以下工作负载：
   - **使用 C++ 的桌面开发**
   - **Windows 应用程序开发**

### 2. 安装 Windows SDK

SDK 通常随 Visual Studio 一起安装，确保版本 ≥ 10.0.19041.0

### 3. 安装 WDK

1. 访问 [Microsoft WDK 下载页面](https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk)
2. 下载与 SDK 版本匹配的 WDK
3. 运行安装程序，使用默认选项
4. 安装完成后，WDK 会自动集成到 Visual Studio

### 4. 配置驱动签名

对于测试，需要启用测试签名模式：

```powershell
# 以管理员身份运行
bcdedit /set testsigning on

# 重启系统
Restart-Computer
```

## 项目结构

> **注意**: v2 架构使用单一 `vnvme.sys` 内核驱动 + `vnvme-server.exe` 用户态服务

### 为什么是单一内核驱动？

虽然逻辑上存在"虚拟总线"和"NVMe 控制器仿真"两个功能，但必须合并到一个驱动中：

| 问题 | 分离架构 | 合并架构 |
|------|----------|----------|
| PDO 的 PnP IRP 谁响应？ | ❌ 无法控制 | ✅ 创建者负责 |
| BAR0 资源报告 | ❌ 需要复杂的 Filter | ✅ 直接处理 |
| 共享内存访问 | ❌ 跨驱动通信 | ✅ 直接访问 |

**关键点**: Windows 驱动模型中，创建 PDO 的驱动必须处理该 PDO 的底层 PnP IRP。分离会导致无法响应 `IRP_MN_QUERY_RESOURCES` 等关键请求。

### 代码文件组织

代码在物理上合并，但在逻辑上清晰分为 **FDO 层（总线功能）** 和 **PDO 层（NVMe 仿真）**：

```
virtual-nvme-driver/
├── src/
│   ├── vnvme/                      # ★ 单一内核驱动 (v2 架构)
│   │   │
│   │   │   ══════════════════════════════════════════════════════
│   │   │   驱动入口
│   │   │   ══════════════════════════════════════════════════════
│   │   ├── vnvme.c                 # DriverEntry, IRP 分发路由
│   │   ├── vnvme.h                 # 主头文件, FDO_CONTEXT/PDO_CONTEXT 定义
│   │   │
│   │   │   ══════════════════════════════════════════════════════
│   │   │   FDO 层 - 总线功能 (操作 VNVME_FDO_CONTEXT)
│   │   │   ══════════════════════════════════════════════════════
│   │   ├── fdo.c                   # FDO PnP/Power IRP 处理
│   │   ├── bus.c                   # 子设备枚举、PDO 创建/删除
│   │   ├── ctrl_dev.c              # \\.\VNVMEControl 控制设备 + IOCTL
│   │   ├── shm.c                   # 共享内存分配 (内核侧)
│   │   │
│   │   │   ══════════════════════════════════════════════════════
│   │   │   PDO 层 - NVMe 仿真 (操作 VNVME_PDO_CONTEXT)
│   │   │   ══════════════════════════════════════════════════════
│   │   ├── pdo.c                   # PDO PnP/Power IRP 处理
│   │   ├── pcie_config.c           # PCIe 配置空间仿真
│   │   ├── bar0.c                  # BAR0 内存分配, NVMe 寄存器初始化
│   │   ├── doorbell.c              # Doorbell 轮询引擎
│   │   ├── queue.c                 # Admin/IO 队列管理
│   │   ├── prp.c                   # PRP 解析和数据复制
│   │   │
│   │   │   ══════════════════════════════════════════════════════
│   │   │   通用/支持
│   │   │   ══════════════════════════════════════════════════════
│   │   ├── trace.h                 # WPP 跟踪宏
│   │   └── vnvme.inf               # INF 文件
│   │
│   ├── vnvme-server/               # ★ 用户态服务 (v2 架构)
│   │   ├── main.c                  # 服务入口
│   │   ├── driver_comm.c           # 与内核驱动通信
│   │   ├── command_engine.c        # NVMe 命令处理引擎
│   │   ├── admin_commands.c        # Admin 命令处理
│   │   ├── io_commands.c           # I/O 命令处理
│   │   ├── backend.h               # 后端接口
│   │   ├── backend_memory.c        # 内存后端
│   │   ├── backend_file.c          # 文件后端
│   │   ├── backend_vhd.c           # VHD 后端 (可选)
│   │   ├── config.c                # 配置文件解析
│   │   ├── logging.c               # 日志系统
│   │   └── service.c               # Windows 服务包装
│   │
│   └── vnvmectl/                   # 用户模式管理工具
│       ├── main.c                  # CLI 入口
│       ├── commands.c              # 命令实现
│       └── vnvmelib.c              # 驱动通信库
│
├── include/
│   ├── vnvme_common.h              # 内核/用户态共享定义
│   ├── vnvme_ioctl.h               # IOCTL 接口定义
│   ├── vnvme_shared.h              # 共享内存结构定义
│   └── nvme_spec.h                 # NVMe 规范定义
│
├── tests/
│   ├── unit/                       # 单元测试
│   │   ├── test_queue.c
│   │   ├── test_prp.c
│   │   └── test_backend.c
│   └── functional/                 # 功能测试
│       ├── test_install.ps1
│       ├── test_io.ps1
│       └── test_stress.ps1
│
├── docs/                           # 文档
│
└── scripts/
    ├── build.ps1                   # 构建脚本
    ├── install.ps1                 # 安装脚本
    ├── uninstall.ps1               # 卸载脚本
    └── test.ps1                    # 测试脚本
```

### 驱动层次关系图

```
┌─────────────────────────────────────────────────────────────────────┐
│                         vnvme.sys                                   │
│                                                                     │
│  ┌───────────────────────────────────────────────────────────────┐  │
│  │                    vnvme.c (入口 + 路由)                       │  │
│  │                                                               │  │
│  │    if (ctx->IsFdo)           else (PDO)                       │  │
│  │         │                         │                           │  │
│  │         ▼                         ▼                           │  │
│  │  ┌─────────────┐           ┌─────────────┐                    │  │
│  │  │   FDO 层    │           │   PDO 层    │                    │  │
│  │  │             │ 创建/管理  │             │                    │  │
│  │  │ • fdo.c     │──────────▶│ • pdo.c     │                    │  │
│  │  │ • bus.c     │           │ • pcie.c    │                    │  │
│  │  │ • ctrl_dev │          │ • bar0.c    │                    │  │
│  │  │ • shm.c     │◀─────────▶│ • doorbell  │                    │  │
│  │  │             │ 共享内存   │ • queue.c   │                    │  │
│  │  └─────────────┘           └─────────────┘                    │  │
│  │         │                         │                           │  │
│  └─────────│─────────────────────────│───────────────────────────┘  │
│            │                         │                              │
└────────────│─────────────────────────│──────────────────────────────┘
             │                         │
             ▼                         ▼
    \\.\VNVMEControl            PDO (PCI\VEN_1B36...)
    (用户态通信)                       │
                                       ▼
                                 stornvme.sys
```

## 创建解决方案

### 方法 1: 使用 Visual Studio 创建

1. 打开 Visual Studio
2. 文件 → 新建 → 项目
3. 选择 "Kernel Mode Driver, Empty (KMDF)"
4. 设置项目名称为 `vnvme`
5. 添加一个控制台应用程序项目 `vnvme-server`
6. 添加一个控制台应用程序项目 `vnvmectl`

### 方法 2: 使用项目文件模板

创建解决方案文件 `vnvme.sln`:

```
Microsoft Visual Studio Solution File, Format Version 12.00
# Visual Studio Version 17
VisualStudioVersion = 17.0.31903.59
MinimumVisualStudioVersion = 10.0.40219.1
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "vnvme", "src\vnvme\vnvme.vcxproj", "{GUID-1}"
EndProject
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "vnvme-server", "src\vnvme-server\vnvme-server.vcxproj", "{GUID-2}"
EndProject
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "vnvmectl", "src\vnvmectl\vnvmectl.vcxproj", "{GUID-3}"
EndProject
Global
    GlobalSection(SolutionConfigurationPlatforms) = preSolution
        Debug|x64 = Debug|x64
        Release|x64 = Release|x64
    EndGlobalSection
EndGlobal
```

## 项目配置

### 驱动项目配置 (vnvme_bus.vcxproj)

关键配置项：

```xml
<PropertyGroup Label="Configuration">
  <TargetVersion>Windows10</TargetVersion>
  <UseDebugLibraries>true</UseDebugLibraries>
  <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
  <ConfigurationType>Driver</ConfigurationType>
  <DriverType>KMDF</DriverType>
  <KMDF_VERSION_MAJOR>1</KMDF_VERSION_MAJOR>
  <KMDF_VERSION_MINOR>31</KMDF_VERSION_MINOR>
</PropertyGroup>

<ItemDefinitionGroup>
  <ClCompile>
    <AdditionalIncludeDirectories>$(ProjectDir)..\..\include;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>
    <PreprocessorDefinitions>_WIN64;_AMD64_;AMD64;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    <WarningLevel>Level4</WarningLevel>
    <TreatWarningAsError>true</TreatWarningAsError>
  </ClCompile>
  <Link>
    <AdditionalDependencies>$(DDK_LIB_PATH)\wdmsec.lib;%(AdditionalDependencies)</AdditionalDependencies>
  </Link>
</ItemDefinitionGroup>
```

### INF 文件配置

确保 INF 文件包含在项目中，并设置正确的属性：

- Build Action: `Inf2Cat`
- Driver Package: 启用
- 签名: 测试签名或正式签名

### INF 文件

> **详细说明**: 请参阅 [INF 文件指南](inf-guide.md)，包含完整的 INF 详解、安装脚本、签名流程和故障排查。

v2 架构需要两个 INF 文件，模板位于 `templates/` 目录：

| 文件 | 用途 | 说明 |
|------|------|------|
| [vnvme.inf](../templates/vnvme.inf) | 主驱动 | 安装到 `ROOT\VNVME`，创建虚拟总线 |
| [vnvme_child.inf](../templates/vnvme_child.inf) | 子设备 | 使 stornvme.sys 加载到虚拟 NVMe 控制器 |

**关键配置**:

| INF 文件 | Class | HardwareID | 说明 |
|----------|-------|------------|------|
| vnvme.inf | System | `ROOT\VNVME` | 根枚举总线设备，KMDF 框架 |
| vnvme_child.inf | SCSIAdapter | `PCI\VEN_1B36&DEV_0010...` | Include/Needs 继承 stornvme.inf |

### 安装根设备

驱动安装后，需要创建根设备实例：

```powershell
# 方法 1: 使用 devcon (推荐用于开发测试)
devcon install vnvme.inf ROOT\VNVME

# 方法 2: 使用 pnputil (Windows 10+)
pnputil /add-driver vnvme.inf /install

# 然后手动添加根设备
pnputil /add-device /instanceid ROOT\VNVME\0001 /deviceid ROOT\VNVME

# 方法 3: 在驱动代码中使用 IoReportRootDevice (自动创建)
# 见 architecture-v2.md 中的说明
```

### 验证安装

```powershell
# 检查驱动是否加载
sc query vnvme

# 检查设备管理器中的设备
Get-PnpDevice | Where-Object { $_.FriendlyName -like "*VNVME*" }

# 检查子设备是否被 stornvme 驱动
Get-PnpDevice -Class SCSIAdapter | Where-Object { $_.FriendlyName -like "*NVMe*" }
```

## 构建

### 命令行构建

```powershell
# 设置环境
$env:WDKContentRoot = "C:\Program Files (x86)\Windows Kits\10"

# 构建 Debug 版本
msbuild vnvme.sln /p:Configuration=Debug /p:Platform=x64

# 构建 Release 版本
msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64
```

### Visual Studio 构建

1. 打开 `vnvme.sln`
2. 选择配置 (Debug/Release) 和平台 (x64)
3. 构建 → 生成解决方案 (Ctrl+Shift+B)

### 构建脚本

`scripts/build.ps1`:

```powershell
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [switch]$Clean,
    [switch]$Rebuild
)

$SolutionPath = Join-Path $PSScriptRoot "..\vnvme.sln"

if ($Clean) {
    Write-Host "Cleaning solution..."
    msbuild $SolutionPath /t:Clean /p:Configuration=$Configuration /p:Platform=x64
}

$target = if ($Rebuild) { "Rebuild" } else { "Build" }

Write-Host "Building $Configuration configuration..."
msbuild $SolutionPath /t:$target /p:Configuration=$Configuration /p:Platform=x64

if ($LASTEXITCODE -eq 0) {
    Write-Host "Build succeeded!" -ForegroundColor Green
    
    # 复制输出文件到 output 目录
    $outputDir = Join-Path $PSScriptRoot "..\output\$Configuration"
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    
    Copy-Item "src\vnvme_bus\x64\$Configuration\vnvme_bus\*" $outputDir -Recurse -Force
    Copy-Item "src\vnvme_emu\x64\$Configuration\vnvme_emu\*" $outputDir -Recurse -Force
    Copy-Item "src\vnvmectl\x64\$Configuration\vnvmectl.exe" $outputDir -Force
    
    Write-Host "Output files copied to: $outputDir"
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
```

## 驱动签名

### 创建测试证书

```powershell
# 创建自签名证书
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=VNVME Test Certificate" `
    -CertStoreLocation Cert:\CurrentUser\My `
    -NotAfter (Get-Date).AddYears(5)

# 导出证书
Export-Certificate -Cert $cert -FilePath "vnvme_test.cer"

# 安装到受信任的根证书
Import-Certificate -FilePath "vnvme_test.cer" `
    -CertStoreLocation Cert:\LocalMachine\Root

# 安装到受信任的发布者
Import-Certificate -FilePath "vnvme_test.cer" `
    -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
```

### 签名驱动

```powershell
# 获取证书
$cert = Get-ChildItem Cert:\CurrentUser\My | 
    Where-Object { $_.Subject -like "*VNVME Test*" }

# 签名 sys 文件
SignTool sign /v /s My /n "VNVME Test Certificate" /t http://timestamp.digicert.com `
    output\Release\vnvme_bus.sys

SignTool sign /v /s My /n "VNVME Test Certificate" /t http://timestamp.digicert.com `
    output\Release\vnvme_emu.sys

# 签名 cat 文件
SignTool sign /v /s My /n "VNVME Test Certificate" /t http://timestamp.digicert.com `
    output\Release\vnvme_bus.cat

SignTool sign /v /s My /n "VNVME Test Certificate" /t http://timestamp.digicert.com `
    output\Release\vnvme_emu.cat
```

## 安装

### 手动安装

```powershell
# 以管理员身份运行

# 安装 Bus 驱动
pnputil /add-driver vnvme_bus.inf /install

# 安装 Emu 驱动  
pnputil /add-driver vnvme_emu.inf /install
```

### 安装脚本

`scripts/install.ps1`:

```powershell
param(
    [ValidateSet("install", "uninstall")]
    [string]$Action = "install",
    
    [string]$OutputDir = "output\Release"
)

# 检查管理员权限
$currentPrincipal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Host "This script requires administrator privileges" -ForegroundColor Red
    exit 1
}

$busInf = Join-Path $PSScriptRoot "..\$OutputDir\vnvme_bus.inf"
$emuInf = Join-Path $PSScriptRoot "..\$OutputDir\vnvme_emu.inf"

if ($Action -eq "install") {
    Write-Host "Installing VNVME drivers..."
    
    # 启用测试签名（如果需要）
    $testsigning = bcdedit /enum | Select-String "testsigning\s+Yes"
    if (-not $testsigning) {
        Write-Host "Enabling test signing mode..."
        bcdedit /set testsigning on
        Write-Host "Please reboot and run this script again" -ForegroundColor Yellow
        exit 0
    }
    
    # 安装驱动
    Write-Host "Installing Bus driver..."
    pnputil /add-driver $busInf /install
    
    Write-Host "Installing Emu driver..."
    pnputil /add-driver $emuInf /install
    
    Write-Host "Installation complete!" -ForegroundColor Green
    
} else {
    Write-Host "Uninstalling VNVME drivers..."
    
    # 先删除所有虚拟设备
    # ... 
    
    # 卸载驱动
    pnputil /delete-driver $busInf /uninstall /force
    pnputil /delete-driver $emuInf /uninstall /force
    
    Write-Host "Uninstallation complete!" -ForegroundColor Green
}
```

## 调试

### 配置内核调试

#### 本地调试 (需要 Windows 10 1903+)

```powershell
# 启用本地内核调试
bcdedit /debug on
bcdedit /dbgsettings local
```

#### 网络调试

```powershell
# 在目标机器上
bcdedit /debug on
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000

# 记下显示的 key
```

### 使用 WinDbg

1. 启动 WinDbg (或 WinDbg Preview)
2. 文件 → Kernel Debug
3. 选择连接类型 (Local/Net/Serial)
4. 连接后加载符号：
   ```
   .sympath+ C:\path\to\output\Debug
   .reload /f vnvme_bus.sys
   .reload /f vnvme_emu.sys
   ```

### 常用调试命令

```
# 查看驱动信息
lm m vnvme*

# 设置断点
bp vnvme_bus!DriverEntry
bp vnvme_emu!DriverEntry

# 查看设备对象
!devobj \Device\VNVMEControl

# 查看驱动对象
!drvobj vnvme_bus

# 查看 IRP
!irp <address>
```

### ETW 跟踪

在驱动中添加 WPP 跟踪：

```c
// 在驱动头文件中
#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(VNVME, (12345678,1234,5678,ABCD,123456789ABC), \
        WPP_DEFINE_BIT(TRACE_INIT) \
        WPP_DEFINE_BIT(TRACE_PNP) \
        WPP_DEFINE_BIT(TRACE_IO) \
        WPP_DEFINE_BIT(TRACE_CMD) \
        WPP_DEFINE_BIT(TRACE_MSIX))
```

收集跟踪：

```powershell
# 开始跟踪
logman create trace vnvme_trace -p {12345678-1234-5678-ABCD-123456789ABC} -o vnvme.etl

logman start vnvme_trace

# ... 执行测试 ...

logman stop vnvme_trace

# 查看跟踪
tracefmt vnvme.etl -o vnvme.txt
```

## 持续集成

### GitHub Actions 配置

`.github/workflows/build.yml`:

```yaml
name: Build VNVME

on:
  push:
    branches: [ main ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: windows-latest
    
    steps:
    - uses: actions/checkout@v3
    
    - name: Add MSBuild to PATH
      uses: microsoft/setup-msbuild@v1.1
      
    - name: Install WDK
      run: |
        # 下载并安装 WDK
        # ...
        
    - name: Build Debug
      run: msbuild vnvme.sln /p:Configuration=Debug /p:Platform=x64
      
    - name: Build Release
      run: msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64
      
    - name: Upload artifacts
      uses: actions/upload-artifact@v3
      with:
        name: vnvme-drivers
        path: output/Release/
```
