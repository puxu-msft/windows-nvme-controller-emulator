# 构建与部署指南

## 开发环境要求

- Windows 10/11 (64-bit)
- Visual Studio 2022 (带 C++ 桌面开发工作负载)
- Windows Driver Kit (WDK) 10.0.22621 或更高版本
- Windows SDK 10.0.22621 或更高版本
- Spectre 缓解库 (可选但推荐)

## 安装 WDK

### 1. 安装 Visual Studio 2022
```
1. 下载 Visual Studio 2022 Community/Professional/Enterprise
2. 选择 "使用 C++ 的桌面开发" 工作负载
3. 确保选中 "MSVC v143 - VS 2022 C++ x64/x86 Spectre 缓解库"
```

### 2. 安装 Windows SDK
```
1. 下载 Windows SDK: https://developer.microsoft.com/windows/downloads/windows-sdk/
2. 安装时选择 "Debugging Tools for Windows"
```

### 3. 安装 WDK
```
1. 下载 WDK: https://learn.microsoft.com/windows-hardware/drivers/download-the-wdk
2. 运行安装程序完成安装
3. 安装完成后安装 WDK Visual Studio 扩展
```

### 验证安装
```powershell
# 检查 WDK 是否正确安装
Get-ChildItem "C:\Program Files (x86)\Windows Kits\10\Include\*\km"
```

## 项目配置

### 创建驱动项目
1. 打开 Visual Studio 2022
2. 新建项目 → 搜索 "KMDF" → 选择 "Kernel Mode Driver (KMDF)"
3. 项目名称: VirtualNvme

### vcxproj 关键配置
```xml
<PropertyGroup Label="Configuration">
  <TargetVersion>Windows10</TargetVersion>
  <UseDebugLibraries>true</UseDebugLibraries>
  <PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>
  <ConfigurationType>Driver</ConfigurationType>
  <DriverType>KMDF</DriverType>
  <KMDF_VERSION_MAJOR>1</KMDF_VERSION_MAJOR>
  <KMDF_VERSION_MINOR>33</KMDF_VERSION_MINOR>
</PropertyGroup>

<ItemDefinitionGroup>
  <ClCompile>
    <PreprocessorDefinitions>_AMD64_;POOL_NX_OPTIN=1;%(PreprocessorDefinitions)</PreprocessorDefinitions>
    <WppEnabled>true</WppEnabled>
    <WppRecorderEnabled>true</WppRecorderEnabled>
    <WppScanConfigurationData>trace.h</WppScanConfigurationData>
  </ClCompile>
  <Link>
    <AdditionalDependencies>%(AdditionalDependencies);$(DDK_LIB_PATH)\wdmsec.lib</AdditionalDependencies>
  </Link>
</ItemDefinitionGroup>
```

### 编译配置
```
平台: x64 (必需), ARM64 (可选)
配置: Debug / Release
目标 OS: Windows 10 Version 2004 (Build 19041) 或更高
驱动模型: KMDF 1.33+
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

### 测试签名 (开发环境)

#### 启用测试签名模式
```powershell
# 以管理员身份运行
bcdedit /set testsigning on
# 重启计算机使更改生效
Restart-Computer
```

#### 创建测试证书
```powershell
# 创建自签名证书 (在 VS 开发者命令提示符中)
makecert -r -pe -ss PrivateCertStore -n "CN=VNvme Test Certificate" vnvme_test.cer

# 或使用 PowerShell 创建
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject "CN=VNvme Test Certificate" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -NotAfter (Get-Date).AddYears(5)

# 导出证书
Export-Certificate -Cert $cert -FilePath vnvme_test.cer
```

#### 签名驱动文件
```powershell
# 使用 SignTool 签名
signtool sign /v /s PrivateCertStore /n "VNvme Test Certificate" /t http://timestamp.digicert.com vnvme.sys

# 或使用证书文件
signtool sign /v /f vnvme_test.pfx /p <password> /t http://timestamp.digicert.com vnvme.sys
```

#### 创建 CAT 签名目录
```powershell
# 生成 CAT 文件
inf2cat /driver:.\output /os:10_X64

# 签名 CAT 文件
signtool sign /v /s PrivateCertStore /n "VNvme Test Certificate" /t http://timestamp.digicert.com vnvme.cat
```

### 生产签名 (发布环境)

生产驱动需要以下步骤：

1. **获取 EV 代码签名证书**
   - 从认证的 CA 购买 (DigiCert, Sectigo 等)
   - 需要公司身份验证

2. **注册 Windows 硬件开发人员中心**
   - https://partner.microsoft.com/dashboard/hardware
   - 使用 EV 证书注册

3. **提交驱动进行签名**
   ```
   a. 创建提交包 (.hlkx 或 .cab)
   b. 上传到硬件开发人员中心
   c. 等待 Microsoft 签名
   d. 下载签名后的驱动
   ```

4. **WHQL 认证 (可选但推荐)**
   - 运行 Windows HLK 测试
   - 提交测试结果获取认证签名

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

#### 网络调试 (推荐)
```powershell
# 启用调试
bcdedit /debug on

# 配置网络调试 (自动生成密钥)
bcdedit /dbgsettings net hostip:192.168.1.100 port:50000

# 查看生成的密钥
bcdedit /dbgsettings
# 输出示例:
# key                     1.2.3.4.5.6.7.8.9.10.11.12.13.14.15.16

# 重启生效
Restart-Computer
```

#### 串口调试 (虚拟机)
```powershell
bcdedit /debug on
bcdedit /dbgsettings serial debugport:1 baudrate:115200
```

#### 本地调试 (有限功能)
```powershell
bcdedit /debug on
bcdedit /dbgsettings local
```

### WinDbg 连接

#### 网络调试连接
```
1. 启动 WinDbg (管理员)
2. File → Attach to kernel (Ctrl+K)
3. 选择 "Net" 标签
4. Port: 50000
5. Key: <上一步生成的密钥>
6. 点击 OK
```

#### 常用调试命令
```windbg
# 加载符号
.sympath+ srv*c:\symbols*https://msdl.microsoft.com/download/symbols
.reload

# 驱动相关
!drvobj vnvme 2              # 查看驱动对象详情
!devobj <device_address>     # 查看设备对象
!devstack <device_address>   # 查看设备栈
!devnode 0 1                 # 显示所有设备节点

# 崩溃分析
!analyze -v                  # 详细分析蓝屏
.bugcheck                    # 显示 bugcheck 代码

# 内存和对象
!pool <address>              # 检查池内存
!object <address>            # 查看对象
dt vnvme!VNVME_CONTROLLER_CONTEXT  # 显示结构体

# 断点
bp vnvme!DriverEntry         # 设置断点
bl                           # 列出断点
bc *                         # 清除所有断点

# 跟踪
!wdfkd.wdfdriverinfo vnvme   # WDF 驱动信息
!wdfkd.wdfdevice <handle>    # WDF 设备信息
!wdfkd.wdfqueue <handle>     # WDF 队列信息
```

### 日志和跟踪

#### 启用 WPP 跟踪
```powershell
# 启动跟踪会话
logman create trace vnvme_trace -p {GUID} -o vnvme.etl

# 开始跟踪
logman start vnvme_trace

# 停止跟踪
logman stop vnvme_trace

# 解析跟踪文件
tracefmt vnvme.etl -p . -o vnvme.txt
```

#### DbgPrint 输出查看
```powershell
# 使用 DebugView (Sysinternals)
# 1. 以管理员运行 DebugView
# 2. Capture → Capture Kernel (Ctrl+K)
# 3. 启用 "Enable Verbose Kernel Output"
```

## 注册表配置

```
HKLM\SYSTEM\CurrentControlSet\Services\vnvme\Parameters

BackendType     REG_DWORD   0=Memory, 1=File
BackendPath     REG_SZ      文件后端路径
DiskSizeMB      REG_DWORD   虚拟磁盘大小(MB)
BlockSize       REG_DWORD   块大小(512/4096)
```

## 静态代码分析

### PREfast (代码分析)

PREfast 是 WDK 内置的静态代码分析工具，可检测驱动中的常见错误。

#### 启用 PREfast

**方法 1: Visual Studio 项目属性**
```
项目属性 → Code Analysis → General
→ Enable Code Analysis: Yes
→ Run Code Analysis on Build: Yes
```

**方法 2: MSBuild 命令行**
```powershell
msbuild VirtualNvme.sln /p:Configuration=Debug /p:Platform=x64 /p:EnablePREfast=true
```

**方法 3: 项目文件配置**
```xml
<PropertyGroup>
  <EnablePREfast>true</EnablePREfast>
  <PREfastLog>prefast.xml</PREfastLog>
  <PREfastAdditionalOptions>/analyze:plugin EspXEngine.dll</PREfastAdditionalOptions>
</PropertyGroup>
```

#### 常见 PREfast 警告

| 代码 | 说明 | 修复建议 |
|------|------|----------|
| C6001 | 使用未初始化的内存 | 初始化变量 |
| C6011 | 取消引用 NULL 指针 | 添加 NULL 检查 |
| C6014 | 内存泄漏 | 确保释放分配的内存 |
| C6031 | 忽略返回值 | 检查函数返回值 |
| C6054 | 字符串可能未终止 | 确保 null 终止 |
| C6200 | 缓冲区溢出 (非栈) | 检查缓冲区边界 |
| C6201 | 栈缓冲区溢出 | 检查数组索引 |
| C6385 | 读取无效数据 | 检查读取范围 |
| C6386 | 写入缓冲区溢出 | 检查写入范围 |
| C28615 | 调用 MustCheck 函数 | 检查返回状态 |
| C28719 | 禁止的 API (POOL_TYPE) | 使用 NonPagedPoolNx |

#### SAL 注释

使用 SAL (Source-code Annotation Language) 提高分析精度：

```c
// 参数注释
NTSTATUS ProcessCommand(
    _In_        PDEVICE_CONTEXT     DeviceContext,
    _In_        ULONG               CommandId,
    _Inout_     PVOID               Buffer,
    _In_        SIZE_T              BufferSize,
    _Out_       PSIZE_T             BytesReturned
);

// 返回值注释
_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS AllocateResource(
    _Outptr_result_nullonfailure_ PVOID* Resource
);

// 锁注释
_Acquires_lock_(Lock)
VOID AcquireLock(_Inout_ PKSPIN_LOCK Lock);

_Releases_lock_(Lock)
VOID ReleaseLock(_Inout_ PKSPIN_LOCK Lock);
```

### SDV (Static Driver Verifier)

SDV 执行更深入的规则验证，检测驱动是否正确遵循 Windows 驱动规则。

#### 运行 SDV

```powershell
# 在驱动项目目录中
cd src\driver

# 清理之前的运行
msbuild /t:sdv /p:Configuration=Release /p:Platform=x64 /p:Inputs="/clean"

# 运行 SDV
msbuild /t:sdv /p:Configuration=Release /p:Platform=x64 /p:Inputs="/check:*"

# 查看结果
msbuild /t:sdv /p:Configuration=Release /p:Platform=x64 /p:Inputs="/view"
```

#### 重要 SDV 规则

| 规则 | 说明 |
|------|------|
| IrqlKeDispatchLte | KeXxx 函数的 IRQL 要求 |
| IrqlZwPassive | Zw 函数必须在 PASSIVE_LEVEL |
| SpinLock | 自旋锁正确获取/释放 |
| RequestCompleted | 请求必须被完成 |
| DoubleCompletion | 不能双重完成请求 |
| DeferredRequestCompleted | 延迟请求必须完成 |
| MarkCancOnCancReqLocal | 取消处理正确性 |
| PnpSurpriseRemove | 意外移除处理 |

## CI/CD 集成

### GitHub Actions 配置

创建 `.github/workflows/build.yml`:

```yaml
name: Build Driver

on:
  push:
    branches: [ main, develop ]
  pull_request:
    branches: [ main ]

jobs:
  build:
    runs-on: windows-latest
    
    strategy:
      matrix:
        configuration: [Debug, Release]
        platform: [x64, ARM64]
    
    steps:
    - uses: actions/checkout@v4
    
    - name: Setup MSBuild
      uses: microsoft/setup-msbuild@v1.3
    
    - name: Install WDK
      run: |
        # 下载并安装 WDK
        Invoke-WebRequest -Uri "https://go.microsoft.com/fwlink/?linkid=2196230" -OutFile "wdksetup.exe"
        Start-Process -FilePath "wdksetup.exe" -ArgumentList "/quiet", "/norestart" -Wait
    
    - name: Build
      run: |
        msbuild VirtualNvme.sln `
          /p:Configuration=${{ matrix.configuration }} `
          /p:Platform=${{ matrix.platform }} `
          /p:EnablePREfast=true
    
    - name: Upload Artifacts
      uses: actions/upload-artifact@v4
      with:
        name: driver-${{ matrix.configuration }}-${{ matrix.platform }}
        path: |
          ${{ matrix.configuration }}/${{ matrix.platform }}/*.sys
          ${{ matrix.configuration }}/${{ matrix.platform }}/*.inf
          ${{ matrix.configuration }}/${{ matrix.platform }}/*.pdb

  test:
    needs: build
    runs-on: windows-latest
    
    steps:
    - uses: actions/checkout@v4
    
    - name: Download Artifact
      uses: actions/download-artifact@v4
      with:
        name: driver-Debug-x64
        path: driver
    
    - name: Run Static Analysis
      run: |
        # 运行额外的静态分析检查
        # ...
    
    - name: Package
      run: |
        # 创建发布包
        Compress-Archive -Path driver\* -DestinationPath vnvme-driver.zip
```

### Azure DevOps Pipeline

创建 `azure-pipelines.yml`:

```yaml
trigger:
  branches:
    include:
      - main
      - develop

pool:
  vmImage: 'windows-latest'

variables:
  solution: 'VirtualNvme.sln'
  buildConfiguration: 'Release'

stages:
- stage: Build
  jobs:
  - job: BuildDriver
    strategy:
      matrix:
        x64:
          buildPlatform: 'x64'
        ARM64:
          buildPlatform: 'ARM64'
    
    steps:
    - task: PowerShell@2
      displayName: 'Install WDK'
      inputs:
        targetType: 'inline'
        script: |
          # WDK 安装脚本
          choco install windows-driver-kit -y
    
    - task: VSBuild@1
      displayName: 'Build Driver'
      inputs:
        solution: '$(solution)'
        platform: '$(buildPlatform)'
        configuration: '$(buildConfiguration)'
        msbuildArgs: '/p:EnablePREfast=true'
    
    - task: PublishBuildArtifacts@1
      displayName: 'Publish Artifacts'
      inputs:
        PathtoPublish: '$(buildConfiguration)\$(buildPlatform)'
        ArtifactName: 'driver-$(buildPlatform)'

- stage: Test
  dependsOn: Build
  jobs:
  - job: RunTests
    steps:
    - task: DownloadBuildArtifacts@1
      inputs:
        buildType: 'current'
        downloadType: 'single'
        artifactName: 'driver-x64'
        downloadPath: '$(System.ArtifactsDirectory)'
    
    - task: PowerShell@2
      displayName: 'Verify INF'
      inputs:
        targetType: 'inline'
        script: |
          & "C:\Program Files (x86)\Windows Kits\10\Tools\x64\infverif.exe" `
            /v /w $(System.ArtifactsDirectory)\driver-x64\vnvme.inf
```

### 本地构建脚本

创建 `build.ps1`:

```powershell
#Requires -RunAsAdministrator
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    
    [ValidateSet("x64", "ARM64")]
    [string]$Platform = "x64",
    
    [switch]$Clean,
    [switch]$EnablePREfast,
    [switch]$RunSDV,
    [switch]$Sign,
    [switch]$Package
)

$ErrorActionPreference = "Stop"
$SolutionPath = Join-Path $PSScriptRoot "VirtualNvme.sln"
$OutputPath = Join-Path $PSScriptRoot "$Configuration\$Platform"

Write-Host "=== Virtual NVMe Driver Build ===" -ForegroundColor Cyan
Write-Host "Configuration: $Configuration"
Write-Host "Platform: $Platform"
Write-Host "Output: $OutputPath"
Write-Host ""

# 查找 MSBuild
$MSBuild = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" `
    -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | 
    Select-Object -First 1

if (-not $MSBuild) {
    throw "MSBuild not found"
}

# 清理
if ($Clean) {
    Write-Host "Cleaning..." -ForegroundColor Yellow
    & $MSBuild $SolutionPath /t:Clean /p:Configuration=$Configuration /p:Platform=$Platform
    Remove-Item -Path $OutputPath -Recurse -Force -ErrorAction SilentlyContinue
}

# 构建参数
$BuildArgs = @(
    $SolutionPath,
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/m"  # 并行构建
)

if ($EnablePREfast) {
    $BuildArgs += "/p:EnablePREfast=true"
    Write-Host "PREfast enabled" -ForegroundColor Yellow
}

# 构建
Write-Host "Building..." -ForegroundColor Yellow
& $MSBuild @BuildArgs
if ($LASTEXITCODE -ne 0) {
    throw "Build failed"
}

# SDV 分析
if ($RunSDV) {
    Write-Host "Running Static Driver Verifier..." -ForegroundColor Yellow
    Push-Location (Join-Path $PSScriptRoot "src\driver")
    try {
        & $MSBuild /t:sdv /p:Configuration=$Configuration /p:Platform=$Platform /p:Inputs="/check:*"
    }
    finally {
        Pop-Location
    }
}

# 签名
if ($Sign) {
    Write-Host "Signing driver..." -ForegroundColor Yellow
    $SignTool = "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
    
    & $SignTool sign /v /s PrivateCertStore /n "VNvme Test Certificate" `
        /t http://timestamp.digicert.com "$OutputPath\vnvme.sys"
    
    # 创建 CAT 文件
    & "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\inf2cat.exe" `
        /driver:$OutputPath /os:10_$Platform
    
    & $SignTool sign /v /s PrivateCertStore /n "VNvme Test Certificate" `
        /t http://timestamp.digicert.com "$OutputPath\vnvme.cat"
}

# 打包
if ($Package) {
    Write-Host "Creating package..." -ForegroundColor Yellow
    $PackagePath = Join-Path $PSScriptRoot "vnvme-$Configuration-$Platform.zip"
    
    $FilesToPackage = @(
        "$OutputPath\vnvme.sys",
        "$OutputPath\vnvme.pdb",
        "$OutputPath\vnvme.inf"
    )
    
    if (Test-Path "$OutputPath\vnvme.cat") {
        $FilesToPackage += "$OutputPath\vnvme.cat"
    }
    
    Compress-Archive -Path $FilesToPackage -DestinationPath $PackagePath -Force
    Write-Host "Package created: $PackagePath" -ForegroundColor Green
}

Write-Host ""
Write-Host "=== Build Complete ===" -ForegroundColor Green
Write-Host "Output: $OutputPath"
Get-ChildItem $OutputPath -Filter "*.sys" | ForEach-Object {
    Write-Host "  $($_.Name) - $([math]::Round($_.Length / 1KB, 2)) KB"
}
```

使用示例:
```powershell
# 基本构建
.\build.ps1

# 发布构建 (带签名和打包)
.\build.ps1 -Configuration Release -EnablePREfast -Sign -Package

# 完整分析
.\build.ps1 -Configuration Release -EnablePREfast -RunSDV
```
