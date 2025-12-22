# INF 文件模板

本文档提供 Virtual NVMe 驱动的 INF 文件模板。

## 虚拟总线驱动 INF (vnvmebus.inf)

```ini
;
; vnvmebus.inf - Virtual NVMe Bus Driver
;
; Copyright (c) 2024 Virtual NVMe Project
;

[Version]
Signature   = "$WINDOWS NT$"
Class       = System
ClassGuid   = {4D36E97D-E325-11CE-BFC1-08002BE10318}
Provider    = %ManufacturerName%
CatalogFile = vnvmebus.cat
DriverVer   = 12/23/2024,1.0.0.0
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13  ; Driver Store

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
vnvmebus.sys = 1,,

;*****************************************
; Install Section
;*****************************************

[Manufacturer]
%ManufacturerName% = Standard,NTamd64

[Standard.NTamd64]
%VNvmeBus.DeviceDesc% = VNvmeBus_Device, Root\VNvmeBus

[VNvmeBus_Device.NT]
CopyFiles = Drivers_Dir

[Drivers_Dir]
vnvmebus.sys

;-------------- Service installation
[VNvmeBus_Device.NT.Services]
AddService = vnvmebus,%SPSVCINST_ASSOCSERVICE%,VNvmeBus_Service_Inst

[VNvmeBus_Service_Inst]
DisplayName    = %VNvmeBus.SVCDESC%
ServiceType    = 1               ; SERVICE_KERNEL_DRIVER
StartType      = 3               ; SERVICE_DEMAND_START
ErrorControl   = 1               ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\vnvmebus.sys

;-------------- Registry Settings
[VNvmeBus_Device.NT.HW]
AddReg = VNvmeBus_Device_HW_AddReg

[VNvmeBus_Device_HW_AddReg]
; 安全描述符 - 允许管理员完全控制
HKR,,Security,,"D:P(A;;GA;;;BA)(A;;GA;;;SY)"

;-------------- WDF Settings
[VNvmeBus_Device.NT.Wdf]
KmdfService = vnvmebus, VNvmeBus_wdfsect

[VNvmeBus_wdfsect]
KmdfLibraryVersion = 1.33

;*****************************************
; Strings Section
;*****************************************

[Strings]
SPSVCINST_ASSOCSERVICE = 0x00000002
ManufacturerName       = "Virtual NVMe Project"
DiskName               = "Virtual NVMe Bus Driver Installation Disk"
VNvmeBus.DeviceDesc    = "Virtual NVMe Bus Controller"
VNvmeBus.SVCDESC       = "Virtual NVMe Bus Driver"
```

## 功能驱动 INF (vnvme.inf)

```ini
;
; vnvme.inf - Virtual NVMe Function Driver
;
; Copyright (c) 2024 Virtual NVMe Project
;

[Version]
Signature   = "$WINDOWS NT$"
Class       = DiskDrive
ClassGuid   = {4D36E967-E325-11CE-BFC1-08002BE10318}
Provider    = %ManufacturerName%
CatalogFile = vnvme.cat
DriverVer   = 12/23/2024,1.0.0.0
PnpLockdown = 1

[DestinationDirs]
DefaultDestDir = 13  ; Driver Store

[SourceDisksNames]
1 = %DiskName%,,,""

[SourceDisksFiles]
vnvme.sys = 1,,

;*****************************************
; Install Section
;*****************************************

[Manufacturer]
%ManufacturerName% = Standard,NTamd64

[Standard.NTamd64]
%VNvme.DeviceDesc% = VNvme_Device, VNvmeBus\Disk

[VNvme_Device.NT]
CopyFiles = Drivers_Dir

[Drivers_Dir]
vnvme.sys

;-------------- Service installation
[VNvme_Device.NT.Services]
AddService = vnvme,%SPSVCINST_ASSOCSERVICE%,VNvme_Service_Inst
AddService = disk,,disk_Service_Inst

[VNvme_Service_Inst]
DisplayName    = %VNvme.SVCDESC%
ServiceType    = 1               ; SERVICE_KERNEL_DRIVER
StartType      = 3               ; SERVICE_DEMAND_START
ErrorControl   = 1               ; SERVICE_ERROR_NORMAL
ServiceBinary  = %13%\vnvme.sys

[disk_Service_Inst]
DisplayName    = "Disk Driver"
ServiceType    = 1               ; SERVICE_KERNEL_DRIVER
StartType      = 3               ; SERVICE_DEMAND_START
ErrorControl   = 1               ; SERVICE_ERROR_NORMAL
ServiceBinary  = %12%\disk.sys

;-------------- Parameters
[VNvme_Device.NT.HW]
AddReg = VNvme_Device_AddReg

[VNvme_Device_AddReg]
HKR,,"BackendType",0x00010001,0      ; 0=Memory, 1=File
HKR,,"DiskSizeMB",0x00010001,1024    ; 1GB default
HKR,,"BlockSize",0x00010001,512      ; 512 bytes

;-------------- WDF Settings
[VNvme_Device.NT.Wdf]
KmdfService = vnvme, VNvme_wdfsect

[VNvme_wdfsect]
KmdfLibraryVersion = 1.33

;*****************************************
; Strings Section
;*****************************************

[Strings]
SPSVCINST_ASSOCSERVICE = 0x00000002
ManufacturerName       = "Virtual NVMe Project"
DiskName               = "Virtual NVMe Driver Installation Disk"
VNvme.DeviceDesc       = "Virtual NVMe Disk"
VNvme.SVCDESC          = "Virtual NVMe Driver"
```

## INF 文件关键说明

### Class 和 ClassGuid

| 驱动类型 | Class | ClassGuid |
|----------|-------|-----------|
| 总线驱动 | System | {4D36E97D-E325-11CE-BFC1-08002BE10318} |
| 磁盘驱动 | DiskDrive | {4D36E967-E325-11CE-BFC1-08002BE10318} |
| 存储控制器 | SCSIAdapter | {4D36E97B-E325-11CE-BFC1-08002BE10318} |

### DestinationDirs 值

| 值 | 说明 |
|----|------|
| 10 | Windows 目录 (%SystemRoot%) |
| 11 | System32 目录 |
| 12 | Drivers 目录 (%SystemRoot%\System32\Drivers) |
| 13 | Driver Store (推荐，支持 DCH) |

### 安装和卸载命令

```powershell
# 安装总线驱动
pnputil /add-driver vnvmebus.inf /install

# 安装功能驱动
pnputil /add-driver vnvme.inf /install

# 创建虚拟设备实例 (需要总线驱动支持)
devcon install vnvmebus.inf Root\VNvmeBus

# 卸载驱动
pnputil /delete-driver vnvme.inf /uninstall /force
pnputil /delete-driver vnvmebus.inf /uninstall /force
```

### 签名验证

```powershell
# 验证 INF 语法
infverif /v /w vnvme.inf

# 验证驱动签名
signtool verify /v /pa vnvme.sys
```
