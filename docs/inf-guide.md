# INF 文件说明

本文档提供 Virtual NVMe 驱动的 INF 安装文件详细说明。

## INF 文件概述

v2 架构需要两个 INF 文件：

| 文件 | 位置 | 用途 |
|------|------|------|
| [vnvme.inf](../templates/vnvme.inf) | `templates/` | 安装主驱动到 `ROOT\VNVME` |
| [vnvme_child.inf](../templates/vnvme_child.inf) | `templates/` | 使子设备被 stornvme.sys 驱动 |

```
安装流程:
                    vnvme.inf
                        │
                        ▼
                 ROOT\VNVME (FDO)
                        │
            vnvme.sys 创建子设备 PDO
                        │
                        ▼
         PCI\VEN_1B36&DEV_0010... (PDO)
                        │
                  vnvme_child.inf
                        │
                        ▼
                  stornvme.sys 加载
```

---

## vnvme.inf 详解

### Version 节

```ini
[Version]
Signature   = "$WINDOWS NT$"    ; Windows NT 签名
Class       = System            ; 系统设备类 (总线驱动)
ClassGuid   = {4D36E97D-E325-11CE-BFC1-08002BE10318}  ; System 类 GUID
Provider    = %ManufacturerName%
CatalogFile = vnvme.cat         ; 签名目录文件
DriverVer   = 12/23/2025,1.0.0.0
PnpLockDown = 1                 ; 限制从 DriverStore 以外位置运行
```

**重要说明：**
- `Class = System` - vnvme.sys 是总线驱动，不是存储适配器
- `CatalogFile` - 发布版本必须有签名目录

### DestinationDirs 节

```ini
[DestinationDirs]
DefaultDestDir = 13      ; DriverStore (推荐，现代方式)
; 或
; DefaultDestDir = 12    ; System32\Drivers (旧方式)
```

**DriverStore 优势：**
- 支持驱动包隔离
- 支持多版本共存
- 符合 Windows 10+ 驱动最佳实践

### 设备匹配

```ini
[Standard.NT$ARCH$.10.0...19041]
%vnvme.DeviceDesc% = vnvme_Device, ROOT\VNVME
```

**设备 ID 说明：**
- `ROOT\VNVME` - 根枚举虚拟设备
- `10.0...19041` - 最低支持 Windows 10 2004 (Build 19041)
- 虚拟设备使用 `ROOT\` 前缀而非 `PCI\`

### 服务安装

```ini
[vnvme_Service_Inst]
DisplayName    = %vnvme.SVCDESC%
ServiceType    = 1     ; SERVICE_KERNEL_DRIVER
StartType      = 3     ; SERVICE_DEMAND_START (按需启动)
ErrorControl   = 1     ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\vnvme.sys   ; DriverStore 路径
```

**StartType 选项：**
| 值 | 名称 | 说明 |
|----|------|------|
| 0 | BOOT_START | 系统引导时由引导加载程序加载 |
| 1 | SYSTEM_START | 系统初始化时由 I/O 子系统加载 |
| 2 | AUTO_START | 服务控制管理器自动启动 |
| 3 | DEMAND_START | 按需手动启动 (推荐用于虚拟设备) |
| 4 | DISABLED | 禁用 |

### WDF 共同安装程序

```ini
[vnvme_Device.NT.CoInstallers]
AddReg    = vnvme_Device_CoInstaller_AddReg
CopyFiles = vnvme_Device_CoInstaller_CopyFiles

[vnvme_Device_CoInstaller_AddReg]
HKR,,CoInstallers32,0x00010000, "WdfCoInstaller$KMDFCOINSTALLERVERSION$.dll,WdfCoInstaller"
```

vnvme.sys 使用 KMDF 框架，需要相应版本的 WdfCoInstaller DLL。

---

## vnvme_child.inf 详解

### 为什么需要此文件

Windows PnP 管理器为子设备 PDO 查找匹配驱动时：
1. PDO 报告硬件 ID: `PCI\VEN_1B36&DEV_0010&SUBSYS_00011B36&REV_02`
2. Windows 在 INF 文件中搜索匹配此 ID 的驱动
3. `vnvme_child.inf` 告诉 Windows 使用 `stornvme.sys`

### Include/Needs 机制

```ini
[vnvme_child_Device.NT]
Include   = stornvme.inf
Needs     = stornvme_Inst.NT

[vnvme_child_Device.NT.HW]
Include   = stornvme.inf
Needs     = stornvme_Inst.NT.HW

[vnvme_child_Device.NT.Services]
Include   = stornvme.inf
Needs     = stornvme_Inst.NT.Services
```

**工作原理：**
- `Include` - 引用系统内置的 stornvme.inf
- `Needs` - 继承该 INF 中指定节的内容
- 这样我们的虚拟 NVMe 设备使用与真实 NVMe 设备相同的驱动配置

### 硬件 ID 匹配

```ini
[Standard.NT$ARCH$.10.0...19041]
%vnvme_child.DeviceDesc% = vnvme_child_Device, PCI\VEN_1B36&DEV_0010&SUBSYS_00011B36&REV_02
```

**此 ID 必须与 PDO 报告的完全匹配**。在 `pdo.c` 中：

```c
// 硬件 ID 必须匹配 vnvme_child.inf
DECLARE_CONST_UNICODE_STRING(hardwareId, 
    L"PCI\\VEN_1B36&DEV_0010&SUBSYS_00011B36&REV_02");
```

| 字段 | 值 | 说明 |
|------|-----|------|
| VEN | 1B36 | Red Hat (QEMU) 的 Vendor ID |
| DEV | 0010 | NVMe Controller Device ID |
| SUBSYS | 00011B36 | Subsystem ID |
| REV | 02 | Revision |

---

## 设备安装

### 方法 1: 使用 devcon (推荐用于开发)

```powershell
# 安装驱动并创建根设备实例
devcon install vnvme.inf ROOT\VNVME

# 安装子设备 INF (使 Windows 知道如何驱动子设备)
pnputil /add-driver vnvme_child.inf /install
```

### 方法 2: 使用 pnputil (Windows 10+)

```powershell
# 预安装驱动到 DriverStore
pnputil /add-driver vnvme.inf /install
pnputil /add-driver vnvme_child.inf /install

# 手动创建根设备实例 (需要重启或扫描硬件)
# 驱动内部使用 IoReportDetectedDevice 自动创建更好
```

### 安装脚本

```powershell
# install_vnvme.ps1
# 需要管理员权限运行

param(
    [string]$DriverPath = $PSScriptRoot
)

$VnvmeInf = Join-Path $DriverPath "vnvme.inf"
$ChildInf = Join-Path $DriverPath "vnvme_child.inf"

Write-Host "Installing Virtual NVMe driver..." -ForegroundColor Cyan

# 1. 安装子设备 INF (先安装，确保子设备创建时能找到驱动)
Write-Host "  Adding vnvme_child.inf to DriverStore..."
pnputil /add-driver $ChildInf /install

# 2. 安装主驱动并创建根设备
Write-Host "  Installing vnvme.inf and creating root device..."
devcon install $VnvmeInf ROOT\VNVME

# 3. 验证
Write-Host "`nVerifying installation..." -ForegroundColor Cyan
$device = Get-PnpDevice | Where-Object { $_.InstanceId -like "*VNVME*" }
if ($device) {
    Write-Host "  Success! Device found:" -ForegroundColor Green
    $device | Format-Table Status, Class, FriendlyName, InstanceId
} else {
    Write-Host "  Warning: Device not found. Check Event Viewer for errors." -ForegroundColor Yellow
}
```

### 卸载脚本

```powershell
# uninstall_vnvme.ps1

Write-Host "Uninstalling Virtual NVMe driver..." -ForegroundColor Cyan

# 1. 移除设备实例
Write-Host "  Removing device instances..."
devcon remove "ROOT\VNVME"

# 2. 从 DriverStore 删除
Write-Host "  Removing from DriverStore..."
$packages = pnputil /enum-drivers | Select-String -Pattern "vnvme" -Context 0,5

foreach ($match in $packages) {
    $lines = $match.Context.PostContext + $match.Line
    $oemInf = ($lines | Select-String "oem\d+\.inf").Matches.Value
    if ($oemInf) {
        Write-Host "    Deleting $oemInf..."
        pnputil /delete-driver $oemInf /uninstall /force
    }
}

Write-Host "Done." -ForegroundColor Green
```

---

## 驱动签名

### 开发阶段 - 测试签名

#### 1. 启用测试签名模式

```powershell
# 需要管理员权限
bcdedit /set testsigning on
# 重启系统
Restart-Computer
```

#### 2. 创建测试证书

```batch
REM 创建自签名证书
makecert -r -pe -ss PrivateCertStore -n "CN=Virtual NVMe Test" vnvme_test.cer

REM 将证书添加到受信任的根证书
certutil -addstore Root vnvme_test.cer
certutil -addstore TrustedPublisher vnvme_test.cer
```

#### 3. 使用 Inf2Cat 创建目录文件

```batch
REM 创建 .cat 文件
inf2cat /driver:C:\path\to\driver /os:10_x64 /verbose
```

#### 4. 签名

```batch
REM 签名 .cat 文件
signtool sign /v /s PrivateCertStore /n "Virtual NVMe Test" ^
    /t http://timestamp.digicert.com vnvme.cat

REM 签名 .sys 文件 (可选，cat 签名通常足够)
signtool sign /v /s PrivateCertStore /n "Virtual NVMe Test" ^
    /t http://timestamp.digicert.com vnvme.sys
```

### 生产阶段 - WHQL/EV 签名

1. **获取 EV 代码签名证书** - DigiCert, GlobalSign, Sectigo 等
2. **注册 Windows 硬件开发人员中心** - https://partner.microsoft.com/dashboard
3. **提交 HLK 测试** - 通过 Windows Hardware Lab Kit 测试
4. **获取 Microsoft 签名** - 或使用门户签名 (attestation signing)

---

## 完整驱动包结构

```
vnvme_driver_package/
├── vnvme.inf              # 主驱动 INF
├── vnvme_child.inf        # 子设备 INF
├── vnvme.sys              # 内核驱动
├── vnvme.cat              # 主驱动签名目录
├── vnvme_child.cat        # 子设备签名目录
├── vnvme.pdb              # 调试符号 (可选)
├── WdfCoInstaller01031.dll  # WDF 共同安装程序
├── install.ps1            # 安装脚本
├── uninstall.ps1          # 卸载脚本
└── README.txt             # 说明文档
```

---

## 常见问题

### 驱动加载失败

**检查事项：**
1. 是否启用测试签名模式 (`bcdedit /set testsigning on`)
2. INF 文件语法是否正确 (`inf2cat` 会报告错误)
3. WdfCoInstaller 版本是否匹配 KMDF 版本
4. 查看 Event Viewer → Windows Logs → System

### 设备管理器显示黄色感叹号

**可能原因：**
- 驱动未签名或签名无效
- DriverEntry 返回失败 (检查 WPP 跟踪或 DbgPrint)
- 依赖的 DLL 缺失

**调试步骤：**
```powershell
# 查看设备状态
Get-PnpDevice | Where-Object { $_.InstanceId -like "*VNVME*" } | 
    Select-Object Status, Problem, FriendlyName

# 查看详细错误
Get-PnpDeviceProperty -InstanceId "ROOT\VNVME\0000" -KeyName DEVPKEY_Device_ProblemCode
```

### 子设备 (NVMe) 不出现

**检查事项：**
1. vnvme.sys 是否成功调用 `IoCreateDevice` 创建 PDO
2. PDO 报告的硬件 ID 是否与 `vnvme_child.inf` 匹配
3. `vnvme_child.inf` 是否已添加到 DriverStore

```powershell
# 检查 DriverStore 中的驱动
pnputil /enum-drivers | Select-String -Pattern "vnvme"
```

### stornvme 不加载到子设备

**检查事项：**
1. PDO 的 `IRP_MN_QUERY_RESOURCE_REQUIREMENTS` 是否正确响应
2. PDO 的 `IRP_MN_QUERY_RESOURCES` 是否报告了 BAR0
3. BAR0 物理地址是否有效
4. PCIe 配置空间是否正确模拟

```powershell
# 查看子设备使用的驱动
Get-PnpDevice -Class SCSIAdapter | 
    Where-Object { $_.FriendlyName -like "*NVMe*" } |
    Select-Object Status, FriendlyName, InstanceId
```

---

## 参考资源

- [INF 文件规范](https://docs.microsoft.com/windows-hardware/drivers/install/overview-of-inf-files)
- [使用 Inf2Cat 创建目录文件](https://docs.microsoft.com/windows-hardware/drivers/devtest/inf2cat)
- [驱动签名指南](https://docs.microsoft.com/windows-hardware/drivers/install/driver-signing)
- [KMDF 共同安装程序](https://docs.microsoft.com/windows-hardware/drivers/wdf/installing-the-framework-s-co-installer)
