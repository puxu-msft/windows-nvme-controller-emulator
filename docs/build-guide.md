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
