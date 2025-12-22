# 构建与部署指南

本文档描述 Virtual NVMe StorPort Miniport 驱动的构建、签名和部署流程。

## 开发环境要求

### 必需组件

| 组件 | 版本 | 下载地址 |
|------|------|----------|
| Visual Studio 2022 | 17.0+ | https://visualstudio.microsoft.com/ |
| Windows SDK | 10.0.22621.0+ | Visual Studio 安装器 |
| Windows WDK | 10.0.22621.0+ | https://learn.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk |
| Spectre 缓解库 | 匹配 WDK 版本 | Visual Studio 安装器 |

### Visual Studio 工作负载

安装 Visual Studio 时选择以下工作负载：

- **"使用 C++ 的桌面开发"**
  - MSVC v143 - VS 2022 C++ x64/x86 构建工具
  - MSVC v143 - VS 2022 C++ x64/x86 Spectre 缓解库

- **Windows 驱动程序开发**（安装 WDK 后自动添加）

### 环境验证

```powershell
# 验证 WDK 安装
Get-ItemProperty "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots" |
    Select-Object KitsRoot10

# 验证 SDK 版本
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include" |
    Where-Object { $_.Name -match "^\d+\." } |
    Sort-Object Name -Descending |
    Select-Object -First 1

# 验证驱动开发工具
where.exe inf2cat
where.exe signtool
```

## 项目结构

```
virtual-nvme-driver/
├── src/
│   └── miniport/
│       ├── vnvme.vcxproj           # 驱动项目文件
│       ├── vnvme.vcxproj.filters
│       ├── vnvme_main.c            # 驱动入口
│       ├── vnvme_adapter.c         # 适配器管理
│       ├── vnvme_lun.c             # LUN 管理
│       ├── vnvme_scsi.c            # SCSI 命令处理
│       ├── vnvme_backend.c         # 后端接口
│       ├── vnvme_backend_memory.c  # 内存后端
│       ├── vnvme_backend_file.c    # 文件后端
│       ├── vnvme_ioctl.c           # IOCTL 处理
│       ├── vnvme.h                 # 主头文件
│       └── sources.props           # 源文件属性
├── inf/
│   └── vnvme.inf                   # INF 文件
├── tools/
│   └── vnvmectl/
│       ├── vnvmectl.vcxproj        # 管理工具项目
│       └── vnvmectl.cpp
├── test/
│   └── vnvme_test.vcxproj          # 测试项目
├── vnvme.sln                       # 解决方案文件
└── build/                          # 构建输出
```

## 创建项目

### 1. 创建解决方案

```
Visual Studio → 新建项目 → 空解决方案 → vnvme
```

### 2. 添加 Miniport 驱动项目

```
解决方案 → 添加 → 新建项目 → 
  Empty WDM Driver 或 Kernel Mode Driver (KMDF)
  
注意: StorPort Miniport 使用 WDM 模型，不是 KMDF
```

### 3. 配置项目属性

**常规属性：**
```xml
<PropertyGroup Label="Globals">
  <ProjectGuid>{新 GUID}</ProjectGuid>
  <TargetName>vnvme</TargetName>
  <DriverType>WDM</DriverType>
  <KMDF_VERSION_MAJOR></KMDF_VERSION_MAJOR>  <!-- 留空，不使用 KMDF -->
</PropertyGroup>
```

**链接器设置：**
```xml
<ItemDefinitionGroup>
  <Link>
    <AdditionalDependencies>
      storport.lib;
      ntoskrnl.lib;
      hal.lib;
      wdmsec.lib;
      %(AdditionalDependencies)
    </AdditionalDependencies>
    <EntryPointSymbol>DriverEntry</EntryPointSymbol>
  </Link>
</ItemDefinitionGroup>
```

## 项目文件模板

### vnvme.vcxproj

```xml
<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" ToolsVersion="Current" 
         xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  
  <PropertyGroup Label="Globals">
    <ProjectGuid>{YOUR-GUID-HERE}</ProjectGuid>
    <RootNamespace>vnvme</RootNamespace>
    <TargetName>vnvme</TargetName>
    <DriverType>WDM</DriverType>
    <Configuration>Debug</Configuration>
    <Platform>x64</Platform>
    <WindowsTargetPlatformVersion>10.0.22621.0</WindowsTargetPlatformVersion>
  </PropertyGroup>
  
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.Default.props" />
  
  <PropertyGroup Label="Configuration" Condition="'$(Configuration)|$(Platform)'=='Debug|x64'">
    <ConfigurationType>Driver</ConfigurationType>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <DriverTargetPlatform>Universal</DriverTargetPlatform>
    <UseDebugLibraries>true</UseDebugLibraries>
  </PropertyGroup>
  
  <PropertyGroup Label="Configuration" Condition="'$(Configuration)|$(Platform)'=='Release|x64'">
    <ConfigurationType>Driver</ConfigurationType>
    <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
    <DriverTargetPlatform>Universal</DriverTargetPlatform>
    <UseDebugLibraries>false</UseDebugLibraries>
  </PropertyGroup>
  
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.props" />
  
  <ItemDefinitionGroup>
    <ClCompile>
      <PreprocessorDefinitions>
        POOL_NX_OPTIN=1;
        %(PreprocessorDefinitions)
      </PreprocessorDefinitions>
      <AdditionalIncludeDirectories>
        $(ProjectDir);
        $(ProjectDir)..\include;
        %(AdditionalIncludeDirectories)
      </AdditionalIncludeDirectories>
      <TreatWarningAsError>true</TreatWarningAsError>
      <WarningLevel>Level4</WarningLevel>
    </ClCompile>
    <Link>
      <AdditionalDependencies>
        storport.lib;
        ntoskrnl.lib;
        hal.lib;
        %(AdditionalDependencies)
      </AdditionalDependencies>
      <EntryPointSymbol>DriverEntry</EntryPointSymbol>
    </Link>
  </ItemDefinitionGroup>
  
  <ItemGroup>
    <ClCompile Include="vnvme_main.c" />
    <ClCompile Include="vnvme_adapter.c" />
    <ClCompile Include="vnvme_lun.c" />
    <ClCompile Include="vnvme_scsi.c" />
    <ClCompile Include="vnvme_backend.c" />
    <ClCompile Include="vnvme_backend_memory.c" />
    <ClCompile Include="vnvme_backend_file.c" />
    <ClCompile Include="vnvme_ioctl.c" />
  </ItemGroup>
  
  <ItemGroup>
    <ClInclude Include="vnvme.h" />
    <ClInclude Include="vnvme_backend.h" />
    <ClInclude Include="vnvme_trace.h" />
  </ItemGroup>
  
  <ItemGroup>
    <Inf Include="..\inf\vnvme.inf" />
  </ItemGroup>
  
  <Import Project="$(VCTargetsPath)\Microsoft.Cpp.targets" />
  
</Project>
```

## 构建步骤

### 命令行构建

```batch
REM 打开开发者命令提示符
"C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

REM 进入项目目录
cd /d Q:\src\virtual-nvme-driver

REM Debug 构建
msbuild vnvme.sln /p:Configuration=Debug /p:Platform=x64

REM Release 构建
msbuild vnvme.sln /p:Configuration=Release /p:Platform=x64

REM 清理构建
msbuild vnvme.sln /t:Clean /p:Configuration=Debug /p:Platform=x64
```

### Visual Studio 构建

1. 打开 `vnvme.sln`
2. 选择配置 (Debug/Release) 和平台 (x64)
3. 生成 → 生成解决方案 (Ctrl+Shift+B)

### 构建输出

```
build/
├── x64/
│   ├── Debug/
│   │   ├── vnvme.sys       # 驱动文件
│   │   ├── vnvme.pdb       # 调试符号
│   │   └── vnvme.inf       # INF 文件副本
│   └── Release/
│       ├── vnvme.sys
│       ├── vnvme.pdb
│       └── vnvme.inf
```

## 驱动签名

### 开发阶段 - 测试签名

#### 1. 启用测试签名模式

```batch
REM 以管理员身份运行
bcdedit /set testsigning on

REM 重启系统
shutdown /r /t 0
```

#### 2. 创建测试证书

```batch
REM 创建自签名证书
makecert -r -pe -ss PrivateCertStore -n "CN=VNvme Test Cert" vnvme_test.cer

REM 导入到受信任的根证书
certutil -addstore Root vnvme_test.cer
certutil -addstore TrustedPublisher vnvme_test.cer
```

#### 3. 签名驱动

```batch
REM 创建 CAT 文件
inf2cat /driver:build\x64\Release /os:10_x64 /verbose

REM 签名 CAT 文件
signtool sign /v /s PrivateCertStore /n "VNvme Test Cert" ^
  /t http://timestamp.digicert.com ^
  build\x64\Release\vnvme.cat

REM 签名 SYS 文件 (可选，CAT 签名即可)
signtool sign /v /s PrivateCertStore /n "VNvme Test Cert" ^
  /t http://timestamp.digicert.com ^
  build\x64\Release\vnvme.sys
```

#### 4. 验证签名

```batch
signtool verify /pa /v build\x64\Release\vnvme.sys
signtool verify /pa /v build\x64\Release\vnvme.cat
```

### 生产阶段 - WHQL 签名

1. **获取 EV 代码签名证书**
2. **注册 Windows 硬件开发人员中心**
3. **运行 HLK 测试**
4. **提交驱动包获取 Microsoft 签名**

## 部署

### 手动安装

```batch
REM 复制文件到目标目录
copy build\x64\Release\vnvme.sys C:\Drivers\vnvme\
copy build\x64\Release\vnvme.cat C:\Drivers\vnvme\
copy inf\vnvme.inf C:\Drivers\vnvme\

REM 使用 devcon 安装
devcon install C:\Drivers\vnvme\vnvme.inf Root\VNvme
```

### 使用 pnputil

```batch
REM 添加驱动到 DriverStore
pnputil /add-driver vnvme.inf /install

REM 列出已安装的驱动
pnputil /enum-drivers | findstr vnvme

REM 删除驱动
pnputil /delete-driver oem123.inf /uninstall /force
```

### 安装脚本

```powershell
# install.ps1
param(
    [string]$DriverPath = ".\build\x64\Release"
)

$ErrorActionPreference = "Stop"

# 检查管理员权限
if (-not ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole] "Administrator")) {
    throw "请以管理员身份运行此脚本"
}

# 安装驱动
Write-Host "正在安装 Virtual NVMe 驱动..."

$infPath = Join-Path $DriverPath "vnvme.inf"

# 使用 pnputil 添加驱动
$result = pnputil /add-driver $infPath /install 2>&1
Write-Host $result

# 创建设备实例
Write-Host "正在创建设备实例..."
$devconPath = "$env:WDKContentRoot\Tools\x64\devcon.exe"

if (Test-Path $devconPath) {
    & $devconPath install $infPath "Root\VNvme"
} else {
    Write-Warning "未找到 devcon.exe，请手动创建设备实例"
    Write-Host "设备 ID: Root\VNvme"
}

Write-Host "安装完成！"
```

## 调试

### 设置内核调试

#### 目标机配置

```batch
REM 启用调试
bcdedit /debug on
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000

REM 获取密钥
bcdedit /dbgsettings
```

#### 主机配置

1. 打开 WinDbg Preview
2. File → Attach to kernel
3. 输入目标机 IP 和端口
4. 输入密钥

### 加载符号

```windbg
.symfix
.sympath+ C:\Symbols;srv*C:\LocalSymbols*https://msdl.microsoft.com/download/symbols

.reload /f vnvme.sys
```

### 常用调试命令

```windbg
# 查看驱动信息
lm m vnvme

# 设置断点
bu vnvme!DriverEntry
bu vnvme!VNvmeHwStartIo

# 查看 StorPort 适配器
!storport.adapters

# 查看 LUN 信息
!storport.lun <adapter_address> <path> <target> <lun>

# 查看 SRB
dt storport!_SCSI_REQUEST_BLOCK <address>
```

### WPP 跟踪

```batch
REM 启动跟踪
tracelog -start vnvme_trace -guid vnvme.guid -f vnvme.etl -flags 0xFF

REM 运行测试...

REM 停止跟踪
tracelog -stop vnvme_trace

REM 格式化输出
tracefmt vnvme.etl -o vnvme.txt -p . -nosummary
```

## 测试

### 功能测试

```powershell
# 创建虚拟磁盘
vnvmectl.exe create -size 1GB -backend memory

# 格式化
Get-Disk | Where-Object PartitionStyle -eq 'RAW' |
    Initialize-Disk -PartitionStyle GPT -PassThru |
    New-Partition -AssignDriveLetter -UseMaximumSize |
    Format-Volume -FileSystem NTFS -Confirm:$false

# 写入测试文件
$testFile = "V:\test.bin"
[byte[]]$data = 1..1024 | ForEach-Object { Get-Random -Maximum 256 }
[IO.File]::WriteAllBytes($testFile, $data)

# 验证读取
$readData = [IO.File]::ReadAllBytes($testFile)
Compare-Object $data $readData
```

### 性能测试

```batch
REM 使用 diskspd
diskspd -b4K -d60 -r -w30 -t4 -o32 -Sh V:\testfile.dat
```

### HLK 测试

1. 安装 Windows HLK
2. 创建测试项目
3. 添加目标机器
4. 选择测试: "Storage - Storage Controller Testing"
5. 运行测试并收集结果

## 常见问题

### 编译错误

**问题**: `error LNK2019: unresolved external symbol StorPortInitialize`

**解决**: 确保链接器设置中包含 `storport.lib`

---

**问题**: `warning C4996: 'ExAllocatePoolWithTag' deprecated`

**解决**: 使用 `ExAllocatePool2` 替代，或定义 `POOL_NX_OPTIN=1`

---

### 运行时错误

**问题**: 驱动加载失败，错误码 577

**解决**: 启用测试签名模式并签名驱动

---

**问题**: 设备管理器显示黄色感叹号

**解决**: 
1. 检查 Event Log 中的 StorPort 事件
2. 使用调试器附加检查 `DriverEntry` 返回值
3. 验证 INF 文件设备 ID 匹配
