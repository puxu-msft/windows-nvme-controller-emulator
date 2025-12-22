# 开发执行清单

本文档提供 **事无巨细** 的开发执行清单，按照顺序完成可从零构建整个项目。

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

### 1.3 实现控制设备 (control_device.c)

- [ ] **1.3.1** 创建控制设备
  ```c
  // vnvme/control_device.c
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

- [ ] **2.1.3** 实现 CreateControllerPdo
  ```c
  NTSTATUS VnvmeCreateControllerPdo(
      WDFDEVICE ParentDevice,
      ULONG ControllerId,
      WDFDEVICE* PdoDevice);
  ```

- [ ] **2.1.4** 设置硬件 ID
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
      
      return VnvmeInitRegisters(PdoContext);
  }
  ```

- [ ] **2.3.3** 初始化 NVMe 寄存器
  ```c
  NTSTATUS VnvmeInitRegisters(PVNVME_PDO_CONTEXT PdoContext)
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

### 3.1 共享内存分配 (shared_memory.c)

- [ ] **3.1.1** 创建 shared_memory.c
  ```c
  // vnvme/shared_memory.c - 共享内存管理
  ```

- [ ] **3.1.2** 实现分配函数
  ```c
  NTSTATUS VnvmeAllocateSharedMemory(PVNVME_FDO_CONTEXT FdoContext);
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

### Phase 3 验收

- [ ] **3.4.1** vnvme-server 启动无错误
- [ ] **3.4.2** 共享内存映射成功
- [ ] **3.4.3** USER_READY 发送成功
- [ ] **3.4.4** 心跳正常 (连续运行 5 分钟)
- [ ] **3.4.5** 优雅关闭正常

---

## Phase 4: NVMe 命令处理

> **目标**: stornvme 初始化成功，磁盘出现
> **预计时间**: 3 周

### 4.1 Doorbell 轮询引擎

- [ ] **4.1.1** 创建 doorbell.c
  ```c
  // vnvme/doorbell.c - Doorbell 轮询
  ```

- [ ] **4.1.2** 实现轮询定时器
  ```c
  NTSTATUS VnvmeStartPolling(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [ ] **4.1.3** 实现轮询回调
  ```c
  VOID VnvmePollTimerCallback(WDFTIMER Timer);
  ```

- [ ] **4.1.4** 检测 CC.EN 变化
  ```c
  VOID VnvmeCheckControllerEnable(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [ ] **4.1.5** 检测 SQ Tail 变化
  ```c
  VOID VnvmeCheckSqTail(PVNVME_PDO_CONTEXT PdoContext);
  ```

### 4.2 控制器启用流程

- [ ] **4.2.1** 处理 CC.EN = 1
  ```c
  VOID VnvmeHandleControllerEnable(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [ ] **4.2.2** 配置 Admin 队列
  ```c
  NTSTATUS VnvmeConfigureAdminQueue(PVNVME_PDO_CONTEXT PdoContext);
  ```

- [ ] **4.2.3** 设置 CSTS.RDY = 1
  ```c
  VOID VnvmeSetControllerReady(PVNVME_PDO_CONTEXT PdoContext);
  ```

### 4.3 Admin 命令处理 (用户态)

- [ ] **4.3.1** 创建 admin_commands.c
  ```c
  // vnvme-server/admin_commands.c
  ```

- [ ] **4.3.2** 实现 Identify Controller
  ```c
  void ProcessIdentifyController(PRING_COMMAND cmd, PVOID dataBuffer);
  ```

- [ ] **4.3.3** 实现 Identify Namespace
  ```c
  void ProcessIdentifyNamespace(PRING_COMMAND cmd, PVOID dataBuffer);
  ```

- [ ] **4.3.4** 实现 Create I/O CQ
  ```c
  void ProcessCreateIoCq(PRING_COMMAND cmd, PNVME_COMPLETION completion);
  ```

- [ ] **4.3.5** 实现 Create I/O SQ
  ```c
  void ProcessCreateIoSq(PRING_COMMAND cmd, PNVME_COMPLETION completion);
  ```

- [ ] **4.3.6** 实现 Set Features (Number of Queues)
  ```c
  void ProcessSetFeatures(PRING_COMMAND cmd, PNVME_COMPLETION completion);
  ```

### 4.4 I/O 命令处理 (用户态)

- [ ] **4.4.1** 创建 io_commands.c
  ```c
  // vnvme-server/io_commands.c
  ```

- [ ] **4.4.2** 实现 Read
  ```c
  void ProcessIoRead(PRING_COMMAND cmd, PNVME_COMPLETION completion);
  ```

- [ ] **4.4.3** 实现 Write
  ```c
  void ProcessIoWrite(PRING_COMMAND cmd, PNVME_COMPLETION completion);
  ```

- [ ] **4.4.4** 实现 Flush
  ```c
  void ProcessIoFlush(PRING_COMMAND cmd, PNVME_COMPLETION completion);
  ```

### 4.5 PRP 解析

- [ ] **4.5.1** 创建 prp.c (内核)
  ```c
  // vnvme/prp.c - PRP 解析和数据复制
  ```

- [ ] **4.5.2** 实现 PRP 解析
  ```c
  NTSTATUS VnvmeParsePrp(
      UINT64 Prp1, UINT64 Prp2, ULONG Length,
      PPRP_ENTRY* Entries, ULONG* EntryCount);
  ```

- [ ] **4.5.3** 实现数据复制
  ```c
  NTSTATUS VnvmeCopyFromPrp(PPRP_ENTRY Entries, PVOID Dest, ULONG Length);
  NTSTATUS VnvmeCopyToPrp(PVOID Src, PPRP_ENTRY Entries, ULONG Length);
  ```

### 4.6 完成处理

- [ ] **4.6.1** 实现完成提交 (内核)
  ```c
  NTSTATUS VnvmePostCompletion(
      PVNVME_PDO_CONTEXT PdoContext,
      ULONG QueueId,
      PNVME_COMPLETION Completion);
  ```

- [ ] **4.6.2** 管理 Phase Tag
  ```c
  VOID VnvmeUpdatePhaseTag(PVNVME_PDO_CONTEXT PdoContext, ULONG QueueId);
  ```

### Phase 4 验收

- [ ] **4.7.1** 控制器状态 CSTS.RDY = 1
- [ ] **4.7.2** stornvme 无错误日志
- [ ] **4.7.3** 磁盘在设备管理器中出现
- [ ] **4.7.4** `nvme list` 显示设备
- [ ] **4.7.5** 可读取磁盘属性

---

## Phase 5: 存储后端

> **目标**: 可格式化和使用虚拟磁盘
> **预计时间**: 2 周

### 5.1 后端抽象层

- [ ] **5.1.1** 创建 backend.c
  ```c
  // vnvme-server/backend.c - 后端管理
  ```

- [ ] **5.1.2** 定义后端接口
  ```c
  typedef struct _VNVME_BACKEND_OPS { ... } VNVME_BACKEND_OPS;
  ```

### 5.2 内存后端

- [ ] **5.2.1** 创建 backend_memory.c
  ```c
  // vnvme-server/backend_memory.c
  ```

- [ ] **5.2.2** 实现内存后端操作
  ```c
  NTSTATUS MemoryBackendInit(...);
  NTSTATUS MemoryBackendRead(...);
  NTSTATUS MemoryBackendWrite(...);
  ```

### 5.3 文件后端

- [ ] **5.3.1** 创建 backend_file.c
  ```c
  // vnvme-server/backend_file.c
  ```

- [ ] **5.3.2** 实现文件后端操作
  ```c
  NTSTATUS FileBackendInit(...);
  NTSTATUS FileBackendRead(...);
  NTSTATUS FileBackendWrite(...);
  NTSTATUS FileBackendFlush(...);
  ```

### 5.4 命名空间管理

- [ ] **5.4.1** 创建 namespace.c
  ```c
  // vnvme-server/namespace.c
  ```

- [ ] **5.4.2** 实现命名空间操作
  ```c
  NTSTATUS CreateNamespace(...);
  NTSTATUS DeleteNamespace(...);
  ```

### 5.5 vnvmectl 管理工具

- [ ] **5.5.1** 实现 create 命令
  ```c
  int CmdCreate(int argc, char** argv);
  ```

- [ ] **5.5.2** 实现 delete 命令
  ```c
  int CmdDelete(int argc, char** argv);
  ```

- [ ] **5.5.3** 实现 list 命令
  ```c
  int CmdList(int argc, char** argv);
  ```

### Phase 5 验收

- [ ] **5.6.1** 可创建虚拟磁盘
- [ ] **5.6.2** 可格式化磁盘 (NTFS/exFAT)
- [ ] **5.6.3** 可读写文件
- [ ] **5.6.4** 重启后数据持久化 (文件后端)
- [ ] **5.6.5** vnvmectl 工具正常工作

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

### 6.3 性能优化

- [ ] **6.3.1** 优化轮询间隔
- [ ] **6.3.2** 批处理优化
- [ ] **6.3.3** 减少内存复制
- [ ] **6.3.4** 锁优化

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
