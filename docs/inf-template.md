# INF 文件模板

本文档提供 Virtual NVMe StorPort Miniport 驱动的 INF 安装文件模板。

## 完整 INF 文件

```ini
;
; vnvme.inf - Virtual NVMe StorPort Miniport Driver
;
; Copyright (c) 2024-2025 Virtual NVMe Project
;

[Version]
Signature   = "$WINDOWS NT$"
Class       = SCSIAdapter
ClassGuid   = {4D36E97B-E325-11CE-BFC1-08002BE10318}
Provider    = %ManufacturerName%
CatalogFile = vnvme.cat
DriverVer   = 12/23/2025,1.0.0.0
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13      ; DriverStore

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
vnvme.sys = 1,,

;
; 制造商信息
;
[Manufacturer]
%ManufacturerName% = Standard,NTamd64.10.0...19041

;
; 设备列表 (Windows 10 2004+)
;
[Standard.NTamd64.10.0...19041]
%VNvme.DeviceDesc% = VNvme_Install, Root\VNvme

;
; 驱动安装节
;
[VNvme_Install]
CopyFiles = VNvme_CopyFiles

[VNvme_CopyFiles]
vnvme.sys

;
; 服务安装
;
[VNvme_Install.Services]
AddService = vnvme, 0x00000002, VNvme_Service

[VNvme_Service]
DisplayName    = %VNvme.ServiceName%
ServiceType    = 1                  ; SERVICE_KERNEL_DRIVER
StartType      = 3                  ; SERVICE_DEMAND_START
ErrorControl   = 1                  ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\vnvme.sys     ; DriverStore path
LoadOrderGroup = SCSI Miniport

;
; 注册表设置
;
[VNvme_Install.HW]
AddReg = VNvme_HW_AddReg

[VNvme_HW_AddReg]
; StorPort Miniport 参数
HKR, "Parameters", "BusType", 0x00010001, 0x0000000A  ; BusTypeRAID
HKR, "Parameters", "IoTimeoutValue", 0x00010001, 30
HKR, "Parameters", "MaxLuns", 0x00010001, 64
HKR, "Parameters", "MaxTransferSize", 0x00010001, 0x400000  ; 4MB

; 虚拟 NVMe 特定参数
HKR, "Parameters\VNvme", "DefaultBackendType", 0x00010001, 1  ; File
HKR, "Parameters\VNvme", "EnableUnmap", 0x00010001, 1
HKR, "Parameters\VNvme", "EnableMpio", 0x00010001, 0

;
; WDF 协同安装器 (可选，StorPort 不需要)
;
; [VNvme_Install.CoInstallers]
; AddReg = VNvme_CoInstaller_AddReg

;
; 字符串定义
;
[Strings]
ManufacturerName   = "Virtual NVMe Project"
DiskName           = "Virtual NVMe Installation Disk"
VNvme.DeviceDesc   = "Virtual NVMe Storage Controller"
VNvme.ServiceName  = "Virtual NVMe Miniport Driver"
```

## INF 文件详解

### Version 节

```ini
[Version]
Signature   = "$WINDOWS NT$"    ; Windows NT 签名
Class       = SCSIAdapter       ; SCSI 适配器类
ClassGuid   = {4D36E97B-E325-11CE-BFC1-08002BE10318}  ; SCSI 类 GUID
Provider    = %ManufacturerName%
CatalogFile = vnvme.cat         ; 驱动目录签名文件
DriverVer   = 12/23/2025,1.0.0.0
PnpLockdown = 1                 ; 限制从 DriverStore 以外位置运行
```

**重要说明：**
- `Class = SCSIAdapter` 是 StorPort Miniport 驱动的必需设置
- `ClassGuid` 必须是 SCSI 适配器类的 GUID
- `CatalogFile` 指向签名目录文件（发布版必需）

### DestinationDirs 节

```ini
[DestinationDirs]
DefaultDestDir = 13      ; DriverStore (推荐)
; 或
; DefaultDestDir = 12    ; System32\Drivers (旧方式)
```

**DriverStore 优势：**
- 支持驱动包隔离
- 支持多版本共存
- 符合现代 Windows 驱动最佳实践

### 设备匹配

```ini
[Standard.NTamd64.10.0...19041]
%VNvme.DeviceDesc% = VNvme_Install, Root\VNvme
```

**设备 ID 说明：**
- `Root\VNvme` - 根枚举设备 ID
- 虚拟设备使用 Root 枚举而非 PCI 等硬件总线
- 格式：`Root\<DeviceID>`

### 服务安装

```ini
[VNvme_Service]
ServiceType    = 1     ; SERVICE_KERNEL_DRIVER
StartType      = 3     ; SERVICE_DEMAND_START
ErrorControl   = 1     ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\vnvme.sys
LoadOrderGroup = SCSI Miniport   ; 关键！
```

**LoadOrderGroup 说明：**
- `SCSI Miniport` 是 StorPort Miniport 驱动的必需加载组
- 确保驱动在正确的时机加载

### 注册表参数

```ini
[VNvme_HW_AddReg]
; BusType 定义
HKR, "Parameters", "BusType", 0x00010001, 0x0000000A
```

**BusType 值：**
| 值 | 名称 | 说明 |
|----|------|------|
| 0x01 | BusTypeSCSI | SCSI 总线 |
| 0x06 | BusTypeFibre | 光纤通道 |
| 0x08 | BusTypeiSCSI | iSCSI |
| 0x09 | BusTypeSAS | SAS |
| 0x0A | BusTypeRAID | RAID (推荐用于虚拟设备) |
| 0x11 | BusTypeNVMe | NVMe |

对于虚拟存储设备，`BusTypeRAID` (0x0A) 是常用选择。

## 设备安装脚本

### 安装驱动

```powershell
# install_vnvme.ps1

# 需要管理员权限运行

$DriverPath = "$PSScriptRoot\vnvme.inf"

# 预安装驱动包到 DriverStore
pnputil /add-driver $DriverPath /install

# 创建根枚举设备实例
$DeviceId = "Root\VNvme"
devcon install $DriverPath $DeviceId

Write-Host "Virtual NVMe driver installed successfully."
```

### 使用 devcon.exe

```batch
@echo off
REM install.cmd

REM 安装驱动并创建设备
devcon.exe install vnvme.inf Root\VNvme

REM 检查安装结果
if %errorlevel% equ 0 (
    echo Driver installed successfully.
) else (
    echo Driver installation failed.
)
```

### 卸载驱动

```powershell
# uninstall_vnvme.ps1

# 移除设备实例
devcon remove "Root\VNvme"

# 从 DriverStore 删除
$DriverStore = Get-ChildItem -Path "C:\Windows\System32\DriverStore\FileRepository" `
    -Filter "vnvme.inf_*" -Directory

foreach ($dir in $DriverStore) {
    $infPath = Join-Path $dir.FullName "vnvme.inf"
    if (Test-Path $infPath) {
        pnputil /delete-driver $infPath /uninstall /force
    }
}

Write-Host "Virtual NVMe driver uninstalled."
```

## 测试签名

开发阶段可以使用测试签名：

### 启用测试签名模式

```batch
bcdedit /set testsigning on
```

### 创建测试证书

```batch
REM 创建自签名证书
makecert -r -pe -ss PrivateCertStore -n "CN=Virtual NVMe Test Cert" vnvme_test.cer

REM 签名 cat 文件
signtool sign /v /s PrivateCertStore /n "Virtual NVMe Test Cert" /t http://timestamp.digicert.com vnvme.cat

REM 签名 sys 文件
signtool sign /v /s PrivateCertStore /n "Virtual NVMe Test Cert" /t http://timestamp.digicert.com vnvme.sys
```

### 使用 Inf2Cat 创建目录

```batch
REM 创建 cat 文件
inf2cat /driver:. /os:10_x64 /verbose
```

## 完整安装包结构

```
vnvme_driver_package/
├── vnvme.inf           # INF 文件
├── vnvme.sys           # 驱动文件
├── vnvme.cat           # 签名目录
├── vnvme.pdb           # 调试符号 (可选)
├── install.cmd         # 安装脚本
├── uninstall.cmd       # 卸载脚本
└── README.txt          # 说明文档
```

## 生产环境签名

生产环境需要 WHQL 或 EV 代码签名：

1. **获取 EV 代码签名证书**
   - 从认证 CA 购买 (DigiCert, GlobalSign 等)

2. **注册 Windows 硬件开发人员中心**
   - https://partner.microsoft.com/dashboard

3. **提交驱动进行 WHQL 测试**
   - 通过 HLK 测试
   - 获取 Microsoft 签名

4. **或使用门户签名**
   - 适用于开发/测试阶段
   - 需要 EV 证书

## 常见问题

### 驱动加载失败

检查事项：
- 是否启用测试签名模式
- INF 文件语法是否正确
- LoadOrderGroup 是否设置为 "SCSI Miniport"
- 驱动文件路径是否正确

### 设备管理器显示黄色感叹号

可能原因：
- 驱动未签名或签名无效
- 驱动初始化失败 (检查 Event Log)
- INF 文件中的设备 ID 不匹配

### 磁盘不显示

检查事项：
- 驱动是否成功响应 INQUIRY 命令
- LUN 是否已创建
- 检查 StorPort 事件日志
