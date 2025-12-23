# 开发执行清单

本文档提供 **事无巨细** 的开发执行清单，按照顺序完成可从零构建整个项目。

---

## 📊 实现状态概览 (2025-12-23 更新)

> **注意**: 下方的复选框列表是原始开发规划，保留供参考。实际实现状态请参考此概览。

| Phase | 描述 | 状态 | 完成度 |
|-------|------|------|--------|
| Phase 0 | 前置准备 | ✅ 已完成 | 100% |
| Phase 1 | 项目骨架和 FDO 创建 | ✅ 已完成 | 100% |
| Phase 2 | PDO 创建和 PCIe/BAR0 仿真 | ✅ 已完成 | 100% |
| Phase 3 | 用户态通信和共享内存 | ✅ 已完成 | 100% |
| Phase 4 | NVMe 命令处理 | ✅ 主要已完成 | 95% |
| Phase 5 | 存储后端 | ✅ 主要已完成 | 90% |
| Phase 6 | 测试和优化 | 🔄 进行中 | 10% |

### 主要实现成果

**内核驱动 (vnvme.sys)** - ~5,863 行:
- ✅ DriverEntry 和 PnP/Power 回调
- ✅ 控制设备 (\\.\vnvme) 和 IOCTL 处理
- ✅ 总线枚举和 PDO 创建
- ✅ BAR0 寄存器模拟
- ✅ PCIe 配置空间模拟
- ✅ Doorbell 轮询处理
- ✅ 共享内存管理
- ✅ 队列管理 (Admin SQ/CQ)
- ✅ PRP 列表处理
- ✅ Admin 命令处理 (内核模式)
- ✅ I/O 命令处理 (内核模式)
- ✅ 存储后端 (内存/文件)
- ✅ 用户态命令转发

**用户态服务 (vnvme-server.exe)** - ~1,967 行:
- ✅ 服务入口和主循环
- ✅ 驱动通信
- ✅ 配置文件加载
- ✅ 命令处理引擎 (Admin + I/O)
- ✅ 存储后端 (内存 + 文件)
- ✅ 优雅关闭处理

**命令行工具 (vnvmectl.exe)** - ~512 行:
- ✅ version 命令
- ✅ status 命令
- ✅ list 命令
- ✅ create 命令
- ✅ delete 命令

### 待完成项 (28+ TODO)

参考 [ROADMAP.md](ROADMAP.md) 获取详细的 TODO 清单。

---

## 目录

- [前置准备](#前置准备)
- [Phase 1: 项目骨架和 FDO 创建](#phase-1-项目骨架和-fdo-创建)
- [Phase 2: PDO 创建和 PCIe/BAR0 仿真](#phase-2-pdo-创建和-pciebar0-仿真)
- [Phase 3: 用户态通信和共享内存](#phase-3-用户态通信和共享内存)
- [Phase 4: NVMe 命令处理](#phase-4-nvme-命令处理)
- [Phase 5: 存储后端](#phase-5-存储后端)
- [Phase 6: 测试和优化](#phase-6-测试和优化)
- [验收检查清单](#验收检查清单)

---

## 前置准备

### 0.1 开发环境安装

- [ ] **0.1.1** 安装 Windows 10/11 专业版或企业版
  - 需要 Hyper-V 支持用于测试虚拟机
  - 建议版本: 22H2 或更高

- [ ] **0.1.2** 安装 Visual Studio 2022
  ```
  下载: https://visualstudio.microsoft.com/
  选择工作负载:
    ✓ 使用 C++ 的桌面开发
    ✓ Windows 10/11 SDK (10.0.22621 或更高)
  ```

- [ ] **0.1.3** 安装 Windows Driver Kit (WDK)
  ```
  下载: https://docs.microsoft.com/en-us/windows-hardware/drivers/download-the-wdk
  版本: WDK for Windows 11, version 22H2
  运行安装程序，默认选项即可
  ```

- [ ] **0.1.4** 安装 WDK Visual Studio 扩展
  ```
  Visual Studio → 扩展 → 管理扩展
  搜索 "Windows Driver Kit" → 安装
  重启 Visual Studio
  ```

- [ ] **0.1.5** 验证安装
  ```powershell
  # 检查 WDK 工具
  Test-Path "C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64\signtool.exe"
  # 应返回 True
  
  # 检查编译器
  & "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  cl.exe
  # 应显示编译器版本
  ```

### 0.2 调试环境配置

- [ ] **0.2.1** 配置测试虚拟机 (推荐)
  ```powershell
  # 创建 Hyper-V 虚拟机用于驱动测试
  New-VM -Name "VNVME-Test" -MemoryStartupBytes 4GB -Generation 2
  Set-VMProcessor -VMName "VNVME-Test" -Count 4
  # 安装 Windows 10/11
  ```

- [ ] **0.2.2** 启用内核调试
  ```powershell
  # 在测试 VM 中运行 (管理员)
  bcdedit /debug on
  bcdedit /dbgsettings serial debugport:1 baudrate:115200
  # 或使用网络调试
  bcdedit /dbgsettings net hostip:<主机IP> port:50000
  ```

- [ ] **0.2.3** 启用测试签名
  ```powershell
  # 在测试 VM 中运行 (管理员)
  bcdedit /set testsigning on
  # 重启
  ```

- [ ] **0.2.4** 安装 WinDbg Preview
  ```
  Microsoft Store → 搜索 "WinDbg Preview" → 安装
  ```

- [ ] **0.2.5** 配置调试连接
  ```
  WinDbg → File → Attach to kernel
  选择对应的调试方式 (Serial/Network)
  ```

### 0.3 项目仓库初始化

- [ ] **0.3.1** 创建项目目录
  ```powershell
  mkdir C:\dev\vnvme
  cd C:\dev\vnvme
  git init
  ```

- [ ] **0.3.2** 创建 .gitignore
  ```
  # 创建 .gitignore 文件，内容:
  build/
  *.sys
  *.pdb
  *.obj
  *.log
  x64/
  ARM64/
  .vs/
  *.user
  ```

- [ ] **0.3.3** 复制文档和模板
  ```powershell
  # 将本项目的 docs/, templates/, include/, scripts/ 复制到项目目录
  ```

---

## Phase 1: 项目骨架和 FDO 创建

> **目标**: 驱动能加载，设备管理器显示设备，控制设备可访问
> **预计时间**: 2 周

### 1.1 创建 Visual Studio 解决方案

- [ ] **1.1.1** 创建解决方案
  ```
  Visual Studio → 文件 → 新建 → 项目
  选择 "Kernel Mode Driver, Empty (KMDF)"
  名称: vnvme
  解决方案名称: vnvme
  位置: C:\dev\vnvme
  ```

- [ ] **1.1.2** 配置内核驱动项目
  ```
  项目属性 → Configuration Properties:
    General:
      Target Platform Version: 10.0 (latest)
      Driver Model: WDF
      KMDF Version Major: 1
      KMDF Version Minor: 33
    
    C/C++ → General:
      Warning Level: Level4 (/W4)
      Treat Warnings As Errors: Yes
    
    C/C++ → Preprocessor:
      添加: POOL_NX_OPTIN=1
    
    Driver Settings → General:
      Target OS Version: Windows 10 or higher
  ```

- [ ] **1.1.3** 创建用户态服务项目
  ```
  解决方案 → 右键 → 添加 → 新建项目
  选择 "Console App"
  名称: vnvme-server
  ```

- [ ] **1.1.4** 创建命令行工具项目
  ```
  解决方案 → 右键 → 添加 → 新建项目
  选择 "Console App"
  名称: vnvmectl
  ```

- [ ] **1.1.5** 添加共享头文件
  ```
  复制以下文件到解决方案根目录的 include/ 文件夹:
    - include/vnvme_common.h
    - include/vnvme_ioctl.h
    - include/nvme_spec.h
  
  所有项目 → 属性 → C/C++ → General → Additional Include Directories:
  添加: $(SolutionDir)include
  ```

### 1.2 实现驱动入口 (vnvme.c)

- [ ] **1.2.1** 创建 vnvme.c
  ```c
  // vnvme/vnvme.c
  #include <ntddk.h>
  #include <wdf.h>
  #include "vnvme_common.h"
  
  // 前向声明
  DRIVER_INITIALIZE DriverEntry;
  EVT_WDF_DRIVER_DEVICE_ADD VnvmeEvtDeviceAdd;
  EVT_WDF_OBJECT_CONTEXT_CLEANUP VnvmeEvtDriverContextCleanup;
  
  // 驱动入口
  NTSTATUS DriverEntry(
      _In_ PDRIVER_OBJECT DriverObject,
      _In_ PUNICODE_STRING RegistryPath)
  {
      NTSTATUS status;
      WDF_DRIVER_CONFIG config;
      WDF_OBJECT_ATTRIBUTES attributes;
      
      // 启用 NX 池
      ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
      
      WDF_DRIVER_CONFIG_INIT(&config, VnvmeEvtDeviceAdd);
      
      WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
      attributes.EvtCleanupCallback = VnvmeEvtDriverContextCleanup;
      
      status = WdfDriverCreate(
          DriverObject,
          RegistryPath,
          &attributes,
          &config,
          WDF_NO_HANDLE
      );
      
      if (!NT_SUCCESS(status)) {
          return status;
      }
      
      return STATUS_SUCCESS;
  }
  ```

- [ ] **1.2.2** 实现 VnvmeEvtDeviceAdd
  ```c
  NTSTATUS VnvmeEvtDeviceAdd(
      _In_ WDFDRIVER Driver,
      _Inout_ PWDFDEVICE_INIT DeviceInit)
  {
      NTSTATUS status;
      WDFDEVICE device;
      WDF_OBJECT_ATTRIBUTES deviceAttributes;
      PVNVME_FDO_CONTEXT fdoContext;
      
      UNREFERENCED_PARAMETER(Driver);
      
      // 设置设备类型
      WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
      
      // 设置独占访问
      WdfDeviceInitSetExclusive(DeviceInit, FALSE);
      
      // 配置设备上下文
      WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, VNVME_FDO_CONTEXT);
      
      // 创建设备
      status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
      if (!NT_SUCCESS(status)) {
          return status;
      }
      
      // 获取上下文
      fdoContext = VnvmeGetFdoContext(device);
      RtlZeroMemory(fdoContext, sizeof(VNVME_FDO_CONTEXT));
      fdoContext->Device = device;
      
      // 创建控制设备
      status = VnvmeCreateControlDevice(device);
      if (!NT_SUCCESS(status)) {
          return status;
      }
      
      return STATUS_SUCCESS;
  }
  ```

- [ ] **1.2.3** 定义 FDO 上下文
  ```c
  // vnvme/vnvme.h
  #ifndef _VNVME_H_
  #define _VNVME_H_
  
  #include <ntddk.h>
  #include <wdf.h>
  #include "vnvme_common.h"
  
  typedef struct _VNVME_FDO_CONTEXT {
      WDFDEVICE Device;
      WDFDEVICE ControlDevice;
      
      // 共享内存
      PVOID SharedMemory;
      PHYSICAL_ADDRESS SharedMemoryPhysical;
      SIZE_T SharedMemorySize;
      PMDL SharedMemoryMdl;
      
      // 子设备管理
      LIST_ENTRY ChildDeviceList;
      KSPIN_LOCK ChildDeviceListLock;
      ULONG ChildDeviceCount;
      
      // 用户态通信
      KEVENT CommandEvent;
      BOOLEAN UserReady;
      ULONG UserPid;
      
      // 统计
      ULONG64 CommandsProcessed;
      ULONG64 ErrorCount;
  } VNVME_FDO_CONTEXT, *PVNVME_FDO_CONTEXT;
  
  WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(VNVME_FDO_CONTEXT, VnvmeGetFdoContext)
  
  // 函数声明
  NTSTATUS VnvmeCreateControlDevice(WDFDEVICE Device);
  
  #endif // _VNVME_H_
  ```

### 1.3 实现控制设备 (ctrl_dev.c)

- [ ] **1.3.1** 创建控制设备
  ```c
  // vnvme/ctrl_dev.c
  #include "vnvme.h"
  
  NTSTATUS VnvmeCreateControlDevice(WDFDEVICE Device)
  {
      NTSTATUS status;
      PWDFDEVICE_INIT deviceInit;
      WDFDEVICE controlDevice;
      WDF_OBJECT_ATTRIBUTES attributes;
      WDF_IO_QUEUE_CONFIG queueConfig;
      WDFQUEUE queue;
      DECLARE_CONST_UNICODE_STRING(deviceName, VNVME_CONTROL_DEVICE);
      DECLARE_CONST_UNICODE_STRING(symbolicLink, VNVME_CONTROL_LINK);
      PVNVME_FDO_CONTEXT fdoContext = VnvmeGetFdoContext(Device);
      
      // 分配设备初始化结构
      deviceInit = WdfControlDeviceInitAllocate(
          WdfDeviceGetDriver(Device),
          &SDDL_DEVOBJ_SYS_ALL_ADM_ALL
      );
      
      if (deviceInit == NULL) {
          return STATUS_INSUFFICIENT_RESOURCES;
      }
      
      // 设置设备名称
      status = WdfDeviceInitAssignName(deviceInit, &deviceName);
      if (!NT_SUCCESS(status)) {
          WdfDeviceInitFree(deviceInit);
          return status;
      }
      
      // 创建设备
      WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
      status = WdfDeviceCreate(&deviceInit, &attributes, &controlDevice);
      if (!NT_SUCCESS(status)) {
          return status;
      }
      
      // 创建符号链接
      status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLink);
      if (!NT_SUCCESS(status)) {
          WdfObjectDelete(controlDevice);
          return status;
      }
      
      // 创建 I/O 队列
      WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
      queueConfig.EvtIoDeviceControl = VnvmeEvtIoDeviceControl;
      
      status = WdfIoQueueCreate(controlDevice, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &queue);
      if (!NT_SUCCESS(status)) {
          WdfObjectDelete(controlDevice);
          return status;
      }
      
      // 完成控制设备初始化
      WdfControlFinishInitializing(controlDevice);
      
      fdoContext->ControlDevice = controlDevice;
      
      return STATUS_SUCCESS;
  }
  ```

- [ ] **1.3.2** 实现 IOCTL 处理
  ```c
  VOID VnvmeEvtIoDeviceControl(
      _In_ WDFQUEUE Queue,
      _In_ WDFREQUEST Request,
      _In_ size_t OutputBufferLength,
      _In_ size_t InputBufferLength,
      _In_ ULONG IoControlCode)
  {
      NTSTATUS status = STATUS_SUCCESS;
      size_t bytesReturned = 0;
      
      UNREFERENCED_PARAMETER(Queue);
      UNREFERENCED_PARAMETER(InputBufferLength);
      
      switch (IoControlCode) {
          case IOCTL_VNVME_GET_VERSION:
              status = VnvmeHandleGetVersion(Request, OutputBufferLength, &bytesReturned);
              break;
              
          case IOCTL_VNVME_GET_STATUS:
              status = VnvmeHandleGetStatus(Request, OutputBufferLength, &bytesReturned);
              break;
              
          case IOCTL_VNVME_MAP_SHARED_MEMORY:
              status = VnvmeHandleMapSharedMemory(Request, OutputBufferLength, &bytesReturned);
              break;
              
          case IOCTL_VNVME_USER_READY:
              status = VnvmeHandleUserReady(Request);
              break;
              
          case IOCTL_VNVME_HEARTBEAT:
              status = VnvmeHandleHeartbeat(Request);
              break;
              
          default:
              status = STATUS_INVALID_DEVICE_REQUEST;
              break;
      }
      
      WdfRequestCompleteWithInformation(Request, status, bytesReturned);
  }
  ```

- [ ] **1.3.3** 实现 GET_VERSION IOCTL
  ```c
  NTSTATUS VnvmeHandleGetVersion(
      _In_ WDFREQUEST Request,
      _In_ size_t OutputBufferLength,
      _Out_ size_t* BytesReturned)
  {
      PVNVME_GET_VERSION_OUTPUT output;
      NTSTATUS status;
      
      if (OutputBufferLength < sizeof(VNVME_GET_VERSION_OUTPUT)) {
          return STATUS_BUFFER_TOO_SMALL;
      }
      
      status = WdfRequestRetrieveOutputBuffer(
          Request,
          sizeof(VNVME_GET_VERSION_OUTPUT),
          (PVOID*)&output,
          NULL
      );
      
      if (!NT_SUCCESS(status)) {
          return status;
      }
      
      output->DriverVersion = VNVME_VERSION;
      output->ApiVersion = VNVME_VERSION;
      output->BuildNumber = 1;
      output->Reserved = 0;
      
      *BytesReturned = sizeof(VNVME_GET_VERSION_OUTPUT);
      return STATUS_SUCCESS;
  }
  ```

### 1.4 创建 INF 文件

- [ ] **1.4.1** 复制 INF 模板
  ```powershell
  Copy-Item templates\vnvme.inf vnvme\vnvme.inf
  ```

- [ ] **1.4.2** 更新项目包含 INF
  ```
  vnvme 项目 → 右键 → 添加 → 现有项 → vnvme.inf
  选择 vnvme.inf → 属性:
    Item Type: Inf
  ```

- [ ] **1.4.3** 配置驱动包
  ```
  vnvme 项目属性 → Inf2Cat → General:
    Run Inf2Cat: Yes
  ```

### 1.5 构建和测试

- [ ] **1.5.1** 构建解决方案
  ```powershell
  # 方法 1: Visual Studio
  Build → Build Solution (Ctrl+Shift+B)
  
  # 方法 2: 命令行
  .\scripts\build.ps1 -Configuration Debug -Platform x64
  ```

- [ ] **1.5.2** 创建测试证书
  ```powershell
  # 在开发机上
  $cert = New-SelfSignedCertificate `
      -Type CodeSigningCert `
      -Subject "CN=VNVMETestCert" `
      -CertStoreLocation "Cert:\CurrentUser\My"
  
  # 导出证书
  Export-Certificate -Cert $cert -FilePath VNVMETestCert.cer
  ```

- [ ] **1.5.3** 复制到测试 VM
  ```powershell
  # 复制以下文件到测试 VM:
  # - vnvme\x64\Debug\vnvme.sys
  # - vnvme\x64\Debug\vnvme.inf
  # - VNVMETestCert.cer
  ```

- [ ] **1.5.4** 在测试 VM 安装证书
  ```powershell
  # 以管理员运行
  Import-Certificate -FilePath VNVMETestCert.cer -CertStoreLocation "Cert:\LocalMachine\TrustedPublisher"
  Import-Certificate -FilePath VNVMETestCert.cer -CertStoreLocation "Cert:\LocalMachine\Root"
  ```

- [ ] **1.5.5** 安装驱动
  ```powershell
  # 使用 devcon (从 WDK 复制)
  devcon install vnvme.inf ROOT\VNVME
  ```

- [ ] **1.5.6** 验证安装
  ```powershell
  # 检查设备
  Get-PnpDevice | Where-Object { $_.InstanceId -like "*VNVME*" }
  
  # 检查控制设备
  Test-Path \\.\VNVMEControl
  ```

### Phase 1 验收

- [ ] **1.6.1** 设备管理器显示 "Virtual NVMe Bus Controller"
- [ ] **1.6.2** 驱动服务状态为 "Running"
- [ ] **1.6.3** 控制设备 `\\.\VNVMEControl` 可访问
- [ ] **1.6.4** 无蓝屏，无 WDF 错误
- [ ] **1.6.5** 驱动可干净卸载

---

## Phase 2: PDO 创建和 PCIe/BAR0 仿真

> **目标**: 创建 PDO，stornvme.sys 能加载
> **预计时间**: 2 周

### 2.1 PDO 创建 (bus.c)

- [ ] **2.1.1** 创建 bus.c
  ```c
  // vnvme/bus.c - 总线管理和 PDO 创建
  ```

- [ ] **2.1.2** 定义 PDO 上下文
  ```c
  typedef struct _VNVME_PDO_CONTEXT {
      WDFDEVICE Device;
      WDFDEVICE ParentFdo;
      ULONG ControllerId;
      
      // BAR0
      PVOID Bar0Virtual;
      PHYSICAL_ADDRESS Bar0Physical;
      SIZE_T Bar0Size;
      PMDL Bar0Mdl;
      
      // NVMe 寄存器
      PNVME_CONTROLLER_REGISTERS Registers;
      
      // 队列
      struct {
          PHYSICAL_ADDRESS SqBase;
          PHYSICAL_ADDRESS CqBase;
          ULONG SqSize;
          ULONG CqSize;
          ULONG SqTail;
          ULONG CqHead;
          BOOLEAN PhaseTag;
      } AdminQueue;
      
      struct {
          // ... I/O 队列
      } IoQueues[64];
      
      // 轮询
      WDFTIMER PollingTimer;
      ULONG PollingIntervalUs;
  } VNVME_PDO_CONTEXT, *PVNVME_PDO_CONTEXT;
  ```

- [ ] **2.1.3** 实现高层 API - CreateVirtualController (供 IOCTL 调用)
  ```c
  // 高层 API - 验证参数，调用低层实现
  NTSTATUS VnvmeCreateVirtualController(
      PVNVME_FDO_CONTEXT FdoContext,
      ULONG ControllerId,
      WDFDEVICE* ChildDevice)
  {
      // 1. 验证 ControllerId 是否已存在
      // 2. 调用低层 VnvmeCreateControllerPdo()
      // 3. 添加到 FdoContext->ChildDeviceList
      // 4. 触发总线重新枚举
  }
  ```

- [ ] **2.1.4** 实现低层实现 - CreateControllerPdo (实际创建 PDO)
  ```c
  // 低层实现 - 实际创建 WDF PDO
  NTSTATUS VnvmeCreateControllerPdo(
      WDFDEVICE ParentDevice,
      ULONG ControllerId,
      WDFDEVICE* PdoDevice)
  {
      // 1. WdfPdoInitAllocate()
      // 2. 设置硬件 ID、兼容 ID
      // 3. WdfDeviceCreate()
      // 4. 初始化 VNVME_PDO_CONTEXT
  }
  ```

- [ ] **2.1.5** 设置硬件 ID
  ```c
  // 硬件 ID 格式:
  // PCI\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02
  DECLARE_UNICODE_STRING_SIZE(hardwareId, 64);
  RtlUnicodeStringPrintf(&hardwareId,
      L"PCI\\VEN_1B36&DEV_0010&SUBSYS_11001AF4&REV_02");
  ```

- [ ] **2.1.5** 设置设备描述
  ```c
  DECLARE_CONST_UNICODE_STRING(deviceDesc, L"Virtual NVMe Controller");
  WdfPdoInitAddDeviceText(DeviceInit, &deviceDesc, NULL, 0x409);
  ```

### 2.2 PDO PnP 处理 (pdo.c)

- [ ] **2.2.1** 创建 pdo.c
  ```c
  // vnvme/pdo.c - PDO PnP/Power 处理
  ```

- [ ] **2.2.2** 实现 IRP 预处理
  ```c
  NTSTATUS VnvmePdoPreprocessIrp(
      WDFDEVICE Device,
      PIRP Irp)
  {
      PIO_STACK_LOCATION irpStack = IoGetCurrentIrpStackLocation(Irp);
      
      if (irpStack->MajorFunction == IRP_MJ_PNP) {
          switch (irpStack->MinorFunction) {
              case IRP_MN_QUERY_RESOURCES:
                  return VnvmePdoQueryResources(Device, Irp);
              case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
                  return VnvmePdoQueryResourceRequirements(Device, Irp);
              case IRP_MN_QUERY_INTERFACE:
                  return VnvmePdoQueryInterface(Device, Irp);
          }
      }
      
      return STATUS_CONTINUE_COMPLETION;
  }
  ```

- [ ] **2.2.3** 实现资源查询
  ```c
  NTSTATUS VnvmePdoQueryResources(WDFDEVICE Device, PIRP Irp)
  {
      // 报告 BAR0 内存资源
      // CmResourceTypeMemory, 64KB, 从 Bar0Physical 开始
  }
  ```

### 2.3 BAR0 仿真 (bar0.c)

- [ ] **2.3.1** 创建 bar0.c
  ```c
  // vnvme/bar0.c - BAR0 内存管理和寄存器初始化
  ```

- [ ] **2.3.2** 分配 BAR0 内存
  ```c
  NTSTATUS VnvmeAllocateBar0(PVNVME_PDO_CONTEXT PdoContext)
  {
      PdoContext->Bar0Size = 64 * 1024;  // 64 KB
      PdoContext->Bar0Virtual = MmAllocateContiguousMemory(
          PdoContext->Bar0Size,
          (PHYSICAL_ADDRESS){.QuadPart = 0xFFFFFFFF}
      );
      
      if (!PdoContext->Bar0Virtual) {
          return STATUS_INSUFFICIENT_RESOURCES;
      }
      
      PdoContext->Bar0Physical = MmGetPhysicalAddress(PdoContext->Bar0Virtual);
      RtlZeroMemory(PdoContext->Bar0Virtual, PdoContext->Bar0Size);
      
      PdoContext->Registers = (PNVME_CONTROLLER_REGISTERS)PdoContext->Bar0Virtual;
      
      VnvmeInitializeBar0Registers(PdoContext);
      return STATUS_SUCCESS;
  }
  ```

- [ ] **2.3.3** 初始化 NVMe 寄存器
  ```c
  VOID VnvmeInitializeBar0Registers(PVNVME_PDO_CONTEXT PdoContext)
  {
      PNVME_CONTROLLER_REGISTERS regs = PdoContext->Registers;
      
      // CAP - Controller Capabilities
      regs->CAP.MQES = 1023;        // 最大队列条目 = 1024
      regs->CAP.CQR = 1;            // 需要连续队列
      regs->CAP.AMS = 0;            // 仅支持 Round Robin
      regs->CAP.TO = 40;            // 超时 = 20秒 (40 * 500ms)
      regs->CAP.DSTRD = 0;          // Doorbell 步长 = 4字节
      regs->CAP.NSSRS = 0;          // 不支持子系统复位
      regs->CAP.CSS = 1;            // 仅 NVM 命令集
      regs->CAP.MPSMIN = 0;         // 最小页面 = 4KB
      regs->CAP.MPSMAX = 0;         // 最大页面 = 4KB
      
      // VS - Version (NVMe 1.4)
      regs->VS.MJR = 1;
      regs->VS.MNR = 4;
      regs->VS.TER = 0;
      
      // CSTS - Controller Status
      regs->CSTS.RDY = 0;           // 未就绪，等待 CC.EN
      regs->CSTS.CFS = 0;           // 无致命错误
      
      return STATUS_SUCCESS;
  }
  ```

### 2.4 PCIe 配置空间 (pcie_config.c)

- [ ] **2.4.1** 创建 pcie_config.c
  ```c
  // vnvme/pcie_config.c - PCIe 配置空间仿真
  ```

- [ ] **2.4.2** 实现总线接口
  ```c
  NTSTATUS VnvmePdoQueryInterface(WDFDEVICE Device, PIRP Irp)
  {
      // 检查是否请求 BUS_INTERFACE_STANDARD
      // 填充 GetBusData/SetBusData 函数指针
  }
  ```

- [ ] **2.4.3** 实现配置空间读取
  ```c
  ULONG VnvmeReadConfig(
      PVOID Context,
      ULONG Offset,
      PVOID Buffer,
      ULONG Length)
  {
      static UINT8 configSpace[256] = {
          0x36, 0x1B,   // VendorID: 0x1B36 (Red Hat)
          0x10, 0x00,   // DeviceID: 0x0010 (virtio-blk legacy)
          // ... 填充完整配置空间
      };
      
      RtlCopyMemory(Buffer, &configSpace[Offset], Length);
      return Length;
  }
  ```

### 2.5 子设备 INF

- [ ] **2.5.1** 复制子设备 INF
  ```powershell
  Copy-Item templates\vnvme_child.inf vnvme\vnvme_child.inf
  ```

- [ ] **2.5.2** 预安装 INF
  ```powershell
  pnputil /add-driver vnvme_child.inf /install
  ```

### Phase 2 验收

- [ ] **2.6.1** PDO 在设备管理器中出现
- [ ] **2.6.2** PDO 硬件 ID 正确: `PCI\VEN_1B36&DEV_0010...`
- [ ] **2.6.3** stornvme.sys 加载到 PDO
- [ ] **2.6.4** BAR0 资源分配成功 (设备属性 → 资源)
- [ ] **2.6.5** 无 Code 10/43 错误

---

## Phase 3: 用户态通信和共享内存

> **目标**: vnvme-server 能连接并接收命令
> **预计时间**: 2 周

### 3.1 共享内存分配 (shm.c)

- [ ] **3.1.1** 创建 shm.c
  ```c
  // vnvme/shm.c - 共享内存管理
  ```

- [ ] **3.1.2** 实现分配函数
  ```c
  NTSTATUS VnvmeAllocateShm(PVNVME_FDO_CONTEXT FdoContext);
  ```

- [ ] **3.1.3** 初始化控制块
  ```c
  NTSTATUS VnvmeInitControlBlock(PVNVME_FDO_CONTEXT FdoContext);
  ```

- [ ] **3.1.4** 实现用户态映射
  ```c
  NTSTATUS VnvmeMapToUserSpace(
      PVNVME_FDO_CONTEXT FdoContext,
      PEPROCESS Process,
      PVOID* UserAddress);
  ```

### 3.2 IOCTL 实现 (ioctl_handlers.c)

- [ ] **3.2.1** 实现 MAP_SHARED_MEMORY
  ```c
  NTSTATUS VnvmeHandleMapSharedMemory(WDFREQUEST Request, ...);
  ```

- [ ] **3.2.2** 实现 USER_READY
  ```c
  NTSTATUS VnvmeHandleUserReady(WDFREQUEST Request, ...);
  ```

- [ ] **3.2.3** 实现 HEARTBEAT
  ```c
  NTSTATUS VnvmeHandleHeartbeat(WDFREQUEST Request, ...);
  ```

- [ ] **3.2.4** 实现 SUBMIT_COMPLETIONS
  ```c
  NTSTATUS VnvmeHandleSubmitCompletions(WDFREQUEST Request, ...);
  ```

### 3.3 vnvme-server 实现

- [ ] **3.3.1** 实现 main.c
  ```c
  // vnvme-server/main.c
  int main(int argc, char* argv[]);
  ```

- [ ] **3.3.2** 实现配置加载
  ```c
  BOOL LoadConfig(const char* path, PVNVME_CONFIG config);
  ```

- [ ] **3.3.3** 实现内核通信
  ```c
  HANDLE OpenKernelDriver(void);
  BOOL MapSharedMemory(HANDLE hDevice, ...);
  ```

- [ ] **3.3.4** 实现命令循环
  ```c
  void CommandLoop(PVOID pSharedMemory, HANDLE hCommandEvent);
  ```

- [ ] **3.3.5** 实现心跳线程
  ```c
  DWORD WINAPI HeartbeatThreadProc(LPVOID lpParam);
  ```

### 3.4 优雅关闭

- [x] **3.4.1** 内核: 添加 `ShutdownEvent` 到 FDO 上下文
- [x] **3.4.2** 内核: 添加 `ShutdownRequested` 标志
- [ ] **3.4.3** 内核: 保存 `ControlQueue` 句柄
- [x] **3.4.4** 内核: `VnvmeEvtDeviceD0Exit` 触发关闭
- [ ] **3.4.5** 内核: 使用 `WdfIoQueueStop()` 等待请求完成
- [x] **3.4.6** 用户态: 检测 `ShutdownRequested` 标志
- [x] **3.4.7** 用户态: 完成待处理命令
- [x] **3.4.8** 用户态: 刷新后端缓存
- [x] **3.4.9** 用户态: 通知内核关闭完成

### 3.5 vnvme-server 配置文件

- [x] **3.5.1** 实现 INI 格式配置文件解析
- [x] **3.5.2** 支持 --config 命令行选项
- [x] **3.5.3** 支持大小后缀 (K/M/G) 解析
- [x] **3.5.4** 创建示例配置文件 vnvme.conf.example

### Phase 3 验收

- [ ] **3.6.1** vnvme-server 启动无错误
- [ ] **3.6.2** 共享内存映射成功
- [ ] **3.6.3** USER_READY 发送成功
- [ ] **3.6.4** 心跳正常 (连续运行 5 分钟)
- [ ] **3.6.5** 优雅关闭正常 (devcon remove 无蓝屏)
- [ ] **3.6.6** 反复加载/卸载 10 次无错误

---

## Phase 4: NVMe 命令处理

> **目标**: stornvme 初始化成功，磁盘出现
> **预计时间**: 3 周
> **状态**: ✅ 命令处理已完成，待测试验证
>
> **双模式架构**: 支持内核态和用户态两种命令处理模式
> - `VNVME_CMD_MODE_KERNEL`: 内核态处理 (低延迟，备用方案)
> - `VNVME_CMD_MODE_USER`: 用户态处理 (默认，更灵活)
> 
> 通过 `vnvme.h` 中的 `VNVME_DEFAULT_CMD_MODE` 切换模式

### 4.1 Doorbell 轮询引擎

- [x] **4.1.1** 创建 doorbell.c
  ```c
  // vnvme/doorbell.c - Doorbell 轮询
  ```

- [x] **4.1.2** 实现轮询定时器
  ```c
  NTSTATUS VnvmeStartPolling(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [x] **4.1.3** 实现轮询回调
  ```c
  VOID VnvmePollTimerCallback(WDFTIMER Timer);
  ```

- [x] **4.1.4** 检测 CC.EN 变化
  ```c
  VOID VnvmeCheckControllerEnable(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [x] **4.1.5** 检测 SQ Tail 变化
  ```c
  VOID VnvmeCheckSqTail(PVNVME_PDO_CONTEXT PdoContext);
  ```

### 4.2 控制器启用流程

- [x] **4.2.1** 处理 CC.EN = 1
  ```c
  VOID VnvmeHandleControllerEnable(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [x] **4.2.2** 配置 Admin 队列
  ```c
  NTSTATUS VnvmeConfigureAdminQueue(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [x] **4.2.3** 设置 CSTS.RDY = 1
  ```c
  VOID VnvmeSetControllerReady(PVNVME_PDO_CONTEXT PdoContext);
  ```

### 4.3 Admin 命令处理 (内核 - admin_cmd.c)

> **架构变更**: Admin 命令在内核驱动中处理，而非用户态服务

- [x] **4.3.1** 创建 admin_cmd.c
  ```c
  // vnvme/admin_cmd.c - Admin 命令处理
  ```

- [x] **4.3.2** 实现 Identify Controller (CNS=1)
  ```c
  static NTSTATUS HandleIdentifyController(...);
  ```

- [x] **4.3.3** 实现 Identify Namespace (CNS=0)
  ```c
  static NTSTATUS HandleIdentifyNamespace(...);
  ```

- [x] **4.3.4** 实现 Create I/O CQ
  ```c
  static NTSTATUS HandleCreateIoCq(...);
  ```

- [x] **4.3.5** 实现 Create I/O SQ
  ```c
  static NTSTATUS HandleCreateIoSq(...);
  ```

- [x] **4.3.6** 实现 Delete I/O CQ/SQ
  ```c
  static NTSTATUS HandleDeleteIoCq(...);
  static NTSTATUS HandleDeleteIoSq(...);
  ```

- [x] **4.3.7** 实现 Set Features / Get Features
  ```c
  static NTSTATUS HandleSetFeatures(...);
  static NTSTATUS HandleGetFeatures(...);
  ```

- [x] **4.3.8** 实现 Abort / Keep Alive
  ```c
  static NTSTATUS HandleAbort(...);
  static NTSTATUS HandleKeepAlive(...);
  ```

### 4.4 I/O 命令处理 (内核 - io_cmd.c)

> **架构变更**: I/O 命令在内核驱动中处理，而非用户态服务

- [x] **4.4.1** 创建 io_cmd.c
  ```c
  // vnvme/io_cmd.c - I/O 命令处理
  ```

- [x] **4.4.2** 实现 Read
  ```c
  static NTSTATUS HandleRead(...);  // 使用 VnvmeStorageRead
  ```

- [x] **4.4.3** 实现 Write
  ```c
  static NTSTATUS HandleWrite(...);  // 使用 VnvmeStorageWrite
  ```

- [x] **4.4.4** 实现 Flush
  ```c
  static NTSTATUS HandleFlush(...);  // 使用 VnvmeStorageFlush
  ```

- [x] **4.4.5** 实现 Write Zeroes
  ```c
  static NTSTATUS HandleWriteZeroes(...);  // 使用 VnvmeStorageWriteZeroes
  ```

- [x] **4.4.6** 实现 Dataset Management
  ```c
  static NTSTATUS HandleDatasetManagement(...);
  ```

### 4.5 PRP 解析

- [x] **4.5.1** 创建 prp.c (内核)
  ```c
  // vnvme/prp.c - PRP 解析和数据复制
  ```

- [x] **4.5.2** 实现 PRP 解析
  ```c
  NTSTATUS VnvmeParsePrpList(...);
  ```

- [x] **4.5.3** 实现数据复制 (单页和双页)
  ```c
  // 通过 MmMapIoSpaceEx 映射 PRP1/PRP2，直接 RtlCopyMemory
  // PRP List (>2 页) 返回 STATUS_NOT_IMPLEMENTED
  ```

### 4.6 完成处理

- [x] **4.6.1** 实现完成提交 (queue.c)
  ```c
  NTSTATUS VnvmePostCompletion(
      PVNVME_PDO_CONTEXT PdoContext,
      ULONG QueueId,
      PNVME_COMPLETION Completion);
  ```

- [x] **4.6.2** 管理 Phase Tag
  ```c
  // 在 VnvmePostCompletion 中自动更新 Phase Tag
  ```

### 4.7 用户态命令转发 (user_forward.c)

> **双模式架构**: 支持内核态和用户态两种命令处理模式

- [x] **4.7.1** 创建 user_forward.c
  ```c
  // vnvme/user_forward.c - 用户态命令转发
  ```

- [x] **4.7.2** 实现 Admin 命令转发
  ```c
  VOID VnvmeForwardAdminCommandsToUser(...);
  ```

- [x] **4.7.3** 实现 I/O 命令转发
  ```c
  VOID VnvmeForwardIoCommandsToUser(...);
  ```

- [x] **4.7.4** 实现通知环操作
  ```c
  static BOOLEAN NotifyRingPush(...);
  static VOID SignalUserMode(...);
  ```

- [x] **4.7.5** 实现用户态完成处理
  ```c
  NTSTATUS VnvmeProcessUserCompletions(...);
  ```

### 4.8 用户态命令处理 (vnvme-server/command_processor.c)

- [x] **4.8.1** 创建 command_processor.c
  ```c
  // vnvme-server/command_processor.c - 用户态命令处理器
  ```

- [x] **4.8.2** 实现初始化
  ```c
  BOOL CmdProcessorInit(PVOID shmAddress, PBACKEND_CONTEXT backend);
  ```

- [x] **4.8.3** 实现主处理循环
  ```c
  UINT64 CmdProcessorRun(void);  // 从 NotifyRing 读取并处理命令
  ```

- [x] **4.8.4** 实现 Admin 命令处理
  ```c
  // Identify, Create/Delete CQ/SQ, Set/Get Features, Abort, KeepAlive
  ```

- [x] **4.8.5** 实现 I/O 命令处理
  ```c
  // Read, Write, Flush, Write Zeroes
  ```

- [x] **4.8.6** 实现完成写入
  ```c
  static void PostAdminCompletion(...);
  static void PostIoCompletion(...);
  ```

### 4.9 用户态存储后端 (vnvme-server/backend.c)

- [x] **4.9.1** 创建 backend.c
  ```c
  // vnvme-server/backend.c - 用户态存储后端
  ```

- [x] **4.9.2** 实现后端创建/销毁
  ```c
  PBACKEND_CONTEXT BackendCreate(int type, SIZE_T size, const WCHAR* filePath);
  void BackendDestroy(PBACKEND_CONTEXT ctx);
  ```

- [x] **4.9.3** 实现后端操作
  ```c
  BOOL BackendRead(...);
  BOOL BackendWrite(...);
  BOOL BackendFlush(...);
  BOOL BackendWriteZeroes(...);
  UINT64 BackendGetSize(...);
  ```

- [x] **4.9.4** 实现内存后端
  ```c
  // 使用 VirtualAlloc
  ```

- [x] **4.9.5** 实现文件后端
  ```c
  // 使用 CreateFile/ReadFile/WriteFile
  ```

### Phase 4 验收

- [x] **4.10.1** Admin 命令处理完成 (12 个命令 - 内核态)
- [x] **4.10.2** I/O 命令处理完成 (5 个命令 - 内核态)
- [x] **4.10.3** 用户态命令转发完成 (user_forward.c)
- [x] **4.10.4** 用户态命令处理完成 (command_processor.c)
- [x] **4.10.5** 用户态存储后端完成 (backend.c)
- [ ] **4.10.6** 控制器状态 CSTS.RDY = 1 (待测试)
- [ ] **4.10.7** stornvme 无错误日志 (待测试)
- [ ] **4.10.8** 磁盘在设备管理器中出现 (待测试)
- [ ] **4.10.9** `nvme list` 显示设备 (待测试)
- [ ] **4.10.10** 可读取磁盘属性 (待测试)

---

## Phase 5: 存储后端

> **目标**: 可格式化和使用虚拟磁盘
> **预计时间**: 2 周
> **状态**: ✅ 存储后端已完成，待测试验证

### 5.1 后端抽象层

> **架构变更**: 存储后端在内核驱动中实现 (storage.c)，而非用户态服务

- [x] **5.1.1** 创建 storage.c
  ```c
  // vnvme/storage.c - 存储后端实现
  ```

- [x] **5.1.2** 定义后端接口
  ```c
  typedef enum _VNVME_STORAGE_TYPE { ... };
  struct _VNVME_STORAGE_CONTEXT { ... };
  ```

- [x] **5.1.3** 实现统一 API
  ```c
  NTSTATUS VnvmeStorageCreate(...);
  VOID VnvmeStorageDestroy(...);
  NTSTATUS VnvmeStorageRead(...);
  NTSTATUS VnvmeStorageWrite(...);
  NTSTATUS VnvmeStorageFlush(...);
  NTSTATUS VnvmeStorageWriteZeroes(...);
  PVOID VnvmeStorageGetDirect(...);  // 零拷贝访问 (仅内存后端)
  ```

### 5.2 内存后端

- [x] **5.2.1** 实现内存后端 (storage.c)
  ```c
  static NTSTATUS StorageMemoryInit(...);  // 最大 256 MB
  static VOID StorageMemoryCleanup(...);
  ```

- [x] **5.2.2** 实现内存后端操作
  ```c
  // VnvmeStorageRead/Write 通过 RtlCopyMemory
  // VnvmeStorageGetDirect 返回直接指针 (零拷贝)
  // VnvmeStorageWriteZeroes 通过 RtlZeroMemory
  ```

### 5.3 文件后端

- [x] **5.3.1** 实现文件后端 (storage.c)
  ```c
  static NTSTATUS StorageFileInit(...);   // ZwCreateFile
  static VOID StorageFileCleanup(...);    // ZwClose
  ```

- [x] **5.3.2** 实现文件后端操作
  ```c
  // VnvmeStorageRead: ZwReadFile
  // VnvmeStorageWrite: ZwWriteFile
  // VnvmeStorageFlush: 更新文件时间戳
  // VnvmeStorageWriteZeroes: 零缓冲区 + ZwWriteFile
  ```

### 5.4 命名空间管理

- [x] **5.4.1** VNVME_NAMESPACE 包含 Storage 字段
  ```c
  // vnvme.h: VNVME_NAMESPACE.Storage = PVNVME_STORAGE_CONTEXT
  ```

- [ ] **5.4.2** 实现命名空间操作
  ```c
  // 待实现: 创建时关联存储后端
  NTSTATUS CreateNamespace(...);
  NTSTATUS DeleteNamespace(...);
  ```

### 5.5 vnvmectl 管理工具

- [x] **5.5.1** 实现 create 命令
  ```c
  int CmdCreate(int argc, char** argv);  // 支持 --size, --backend, --file, --model, --serial
  ```

- [x] **5.5.2** 实现 delete 命令
  ```c
  int CmdDelete(int argc, char** argv);
  ```

- [x] **5.5.3** 实现 list 命令
  ```c
  int CmdList(int argc, char** argv);
  ```

- [x] **5.5.4** 实现 version 命令
  ```c
  int CmdVersion(void);
  ```

- [x] **5.5.5** 实现 status 命令
  ```c
  int CmdStatus(void);
  ```

- [x] **5.5.6** 实现 test 命令
  ```c
  int CmdTest(void);
  ```

### 5.6 I/O 命令与存储后端集成

- [x] **5.6.1** HandleRead 使用存储后端
  ```c
  VnvmeStorageRead(...) 或 VnvmeStorageGetDirect(...)
  ```

- [x] **5.6.2** HandleWrite 使用存储后端
  ```c
  VnvmeStorageWrite(...)
  ```

- [x] **5.6.3** HandleFlush 使用存储后端
  ```c
  VnvmeStorageFlush(...)
  ```

- [x] **5.6.4** HandleWriteZeroes 使用存储后端
  ```c
  VnvmeStorageWriteZeroes(...)  // 1 MB 分块处理
  ```

### Phase 5 验收

- [x] **5.7.1** 存储后端抽象层完成
- [x] **5.7.2** 内存后端完成 (最大 256 MB)
- [x] **5.7.3** 文件后端完成
- [x] **5.7.4** I/O 命令与存储后端集成完成
- [ ] **5.7.5** 可格式化磁盘 (待测试)
- [ ] **5.7.6** 可读写文件 (待测试)
- [ ] **5.7.7** 重启后数据持久化 (待测试)

---

## Phase 6: 测试和优化

> **目标**: 完成功能测试，优化性能
> **预计时间**: 2 周

### 6.1 功能测试

- [ ] **6.1.1** 驱动加载/卸载测试
- [ ] **6.1.2** 热插拔测试
- [ ] **6.1.3** 多磁盘测试
- [ ] **6.1.4** 并发 I/O 测试
- [ ] **6.1.5** 大文件测试 (>4GB)
- [ ] **6.1.6** 随机 I/O 测试

### 6.2 性能测试

- [ ] **6.2.1** CrystalDiskMark 测试
- [ ] **6.2.2** fio 测试
- [ ] **6.2.3** 记录基线性能
- [ ] **6.2.4** 延迟分析 (识别瓶颈)

### 6.3 性能优化

> 详见 [performance-optimization.md](performance-optimization.md)

**6.3.1 自适应轮询 (1 天)**
- [ ] **6.3.1.1** 实现 `VNVME_ADAPTIVE_POLL` 结构
- [ ] **6.3.1.2** 实现 `VnvmeAdjustPollingInterval()` 函数
- [ ] **6.3.1.3** 添加注册表参数配置
- [ ] **6.3.1.4** 测试验证

**6.3.2 批处理优化 (1 天)**
- [ ] **6.3.2.1** 实现 `VnvmeFetchCommandBatch()`
- [ ] **6.3.2.2** 实现 `VnvmePostCompletionBatch()`
- [ ] **6.3.2.3** 用户态批处理实现
- [ ] **6.3.2.4** 测试验证

**6.3.3 事件通知 (2 天)**
- [ ] **6.3.3.1** 实现 `VnvmeCreateUserEventHandle()`
- [ ] **6.3.3.2** 实现混合通知模式
- [ ] **6.3.3.3** 用户态等待实现
- [ ] **6.3.3.4** IOCTL 返回事件句柄
- [ ] **6.3.3.5** 测试验证

**6.3.4 内存访问优化 (1 天)**
- [ ] **6.3.4.1** 缓存行对齐关键结构
- [ ] **6.3.4.2** 优化内存屏障使用
- [ ] **6.3.4.3** 预取优化
- [ ] **6.3.4.4** 测试验证

**6.3.5 后端存储优化 (1 天)**
- [ ] **6.3.5.1** 实现异步 I/O (IOCP)
- [ ] **6.3.5.2** 直接 I/O 支持
- [ ] **6.3.5.3** 可选: 内存映射后端
- [ ] **6.3.5.4** 测试验证

### 6.4 文档和发布

- [ ] **6.4.1** 更新所有文档
- [ ] **6.4.2** 添加代码注释
- [ ] **6.4.3** 创建用户手册
- [ ] **6.4.4** 创建发布包

---

## 验收检查清单

### 功能验收

- [ ] 设备管理器显示 "Virtual NVMe Controller"
- [ ] stornvme.sys 成功加载
- [ ] `nvme list` 显示设备
- [ ] 可格式化磁盘
- [ ] 可读写文件
- [ ] 数据持久化
- [ ] 热插拔工作
- [ ] 多磁盘支持
- [ ] 优雅关闭

### 稳定性验收

- [ ] 连续运行 24 小时无蓝屏
- [ ] 连续运行 24 小时无内存泄漏
- [ ] 压力测试通过 (fio 8 小时)
- [ ] 反复加载/卸载 100 次无错误

### 性能验收

- [ ] 顺序读 > 100 MB/s (内存后端)
- [ ] 顺序写 > 100 MB/s (内存后端)
- [ ] 随机 4K 读 > 1000 IOPS
- [ ] 随机 4K 写 > 1000 IOPS

### 兼容性验收

- [ ] Windows 10 20H1+ 支持
- [ ] Windows 11 支持
- [ ] Windows Server 2019+ 支持
- [ ] x64 和 ARM64 平台

---

## 常见问题

### Q: 驱动安装失败，错误码 0x...?
参考 [troubleshooting.md](troubleshooting.md)

### Q: stornvme 报错 Code 10?
检查:
1. BAR0 资源是否正确分配
2. PCIe 配置空间是否正确填充
3. 使用 WinDbg 查看具体失败原因

### Q: 性能低于预期?
检查:
1. 轮询间隔是否过长
2. 批处理是否启用
3. 后端是否使用直接 I/O

---

**祝开发顺利！** 🚀
