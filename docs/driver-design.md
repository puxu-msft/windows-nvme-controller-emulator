# 驱动详细设计

本文档描述 Virtual NVMe StorPort Miniport 驱动的详细设计。

## 模块划分

### 核心模块

```
┌─────────────────────────────────────────────────────────────────┐
│                     vnvme.sys - StorPort Virtual Miniport        │
├─────────────────────────────────────────────────────────────────┤
│  ┌───────────────┐  ┌───────────────┐  ┌───────────────────┐    │
│  │  vnvme_main   │  │  vnvme_scsi   │  │  vnvme_backend    │    │
│  │  驱动入口     │  │  SCSI命令处理 │  │  存储后端         │    │
│  └───────┬───────┘  └───────┬───────┘  └─────────┬─────────┘    │
│          │                  │                    │              │
│  ┌───────┴───────┐  ┌───────┴───────┐  ┌─────────┴─────────┐    │
│  │ vnvme_adapter │  │  vnvme_lun    │  │  vnvme_ioctl      │    │
│  │ 适配器管理    │  │  LUN管理      │  │  用户态接口       │    │
│  └───────────────┘  └───────────────┘  └───────────────────┘    │
└─────────────────────────────────────────────────────────────────┘
```

### 1. vnvme_main.c - 驱动入口

```c
// 主要函数
ULONG DriverEntry(
    PVOID DriverObject,
    PVOID RegistryPath);

// StorPort 回调函数
ULONG VNvmeHwFindAdapter(
    PVOID DeviceExtension,
    PVOID HwContext,
    PVOID BusInformation,
    PCHAR ArgumentString,
    PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    PUCHAR Again);

BOOLEAN VNvmeHwInitialize(
    PVOID DeviceExtension);

BOOLEAN VNvmeHwStartIo(
    PVOID DeviceExtension,
    PSCSI_REQUEST_BLOCK Srb);

BOOLEAN VNvmeHwResetBus(
    PVOID DeviceExtension,
    ULONG PathId);

SCSI_ADAPTER_CONTROL_STATUS VNvmeHwAdapterControl(
    PVOID DeviceExtension,
    SCSI_ADAPTER_CONTROL_TYPE ControlType,
    PVOID Parameters);

// 可选回调
BOOLEAN VNvmeHwBuildIo(
    PVOID DeviceExtension,
    PSCSI_REQUEST_BLOCK Srb);

VOID VNvmeHwFreeAdapterResources(
    PVOID DeviceExtension);
```

### 2. vnvme_adapter.c - 适配器管理

```c
// 主要函数
NTSTATUS VNvmeAdapterInitialize(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PPORT_CONFIGURATION_INFORMATION ConfigInfo);

VOID VNvmeAdapterShutdown(
    PVNVME_ADAPTER_EXTENSION AdapterExtension);

NTSTATUS VNvmeAdapterStartIo(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PSCSI_REQUEST_BLOCK Srb);

NTSTATUS VNvmeAdapterReset(
    PVNVME_ADAPTER_EXTENSION AdapterExtension);

// 适配器状态管理
VOID VNvmeAdapterSetState(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    VNVME_ADAPTER_STATE NewState);
```

### 3. vnvme_lun.c - LUN 管理

```c
// 主要函数
NTSTATUS VNvmeLunCreate(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PVNVME_LUN_CONFIG Config,
    PVNVME_LU_EXTENSION* LuExtension);

NTSTATUS VNvmeLunDelete(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    UCHAR PathId,
    UCHAR TargetId,
    UCHAR Lun);

PVNVME_LU_EXTENSION VNvmeLunFind(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    UCHAR PathId,
    UCHAR TargetId,
    UCHAR Lun);

NTSTATUS VNvmeLunResize(
    PVNVME_LU_EXTENSION LuExtension,
    ULONGLONG NewSizeInBytes);

NTSTATUS VNvmeLunSetBackend(
    PVNVME_LU_EXTENSION LuExtension,
    PVNVME_BACKEND_CONFIG BackendConfig);
```

### 4. vnvme_scsi.c - SCSI 命令处理

```c
// 主要函数
UCHAR VNvmeScsiExecute(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PSCSI_REQUEST_BLOCK Srb);

// 各命令处理函数
UCHAR VNvmeScsiInquiry(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiReadCapacity(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiReadCapacity16(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiRead(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiWrite(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiModeSense(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiSynchronizeCache(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

UCHAR VNvmeScsiUnmap(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);

// VPD 页处理
UCHAR VNvmeScsiVpdPages(
    PVNVME_LU_EXTENSION LuExtension,
    PSCSI_REQUEST_BLOCK Srb);
```

### 5. vnvme_backend.c - 存储后端

```c
// 后端接口
typedef struct _VNVME_BACKEND_OPS {
    NTSTATUS (*Initialize)(PVNVME_BACKEND*, PVNVME_BACKEND_CONFIG);
    NTSTATUS (*Read)(PVNVME_BACKEND, ULONGLONG, ULONG, PVOID);
    NTSTATUS (*Write)(PVNVME_BACKEND, ULONGLONG, ULONG, PVOID);
    NTSTATUS (*Flush)(PVNVME_BACKEND);
    NTSTATUS (*Trim)(PVNVME_BACKEND, ULONGLONG, ULONGLONG);
    NTSTATUS (*Resize)(PVNVME_BACKEND, ULONGLONG);
    VOID (*Close)(PVNVME_BACKEND);
} VNVME_BACKEND_OPS;

// 主要函数
NTSTATUS VNvmeBackendCreate(
    PVNVME_BACKEND_CONFIG Config,
    PVNVME_BACKEND* Backend);

VOID VNvmeBackendDestroy(
    PVNVME_BACKEND Backend);

NTSTATUS VNvmeBackendRead(
    PVNVME_BACKEND Backend,
    ULONGLONG Offset,
    ULONG Length,
    PVOID Buffer);

NTSTATUS VNvmeBackendWrite(
    PVNVME_BACKEND Backend,
    ULONGLONG Offset,
    ULONG Length,
    PVOID Buffer);

NTSTATUS VNvmeBackendFlush(
    PVNVME_BACKEND Backend);

NTSTATUS VNvmeBackendTrim(
    PVNVME_BACKEND Backend,
    ULONGLONG Offset,
    ULONGLONG Length);
```

### 6. vnvme_ioctl.c - 用户态接口

```c
// 主要函数
UCHAR VNvmeIoctlProcess(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PSCSI_REQUEST_BLOCK Srb);

NTSTATUS VNvmeIoctlCreateDisk(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PVNVME_CREATE_DISK_INPUT Input,
    PVNVME_CREATE_DISK_OUTPUT Output);

NTSTATUS VNvmeIoctlDeleteDisk(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PVNVME_DELETE_DISK_INPUT Input);

NTSTATUS VNvmeIoctlQueryDisk(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PVNVME_QUERY_DISK_INPUT Input,
    PVNVME_QUERY_DISK_OUTPUT Output);

NTSTATUS VNvmeIoctlResizeDisk(
    PVNVME_ADAPTER_EXTENSION AdapterExtension,
    PVNVME_RESIZE_DISK_INPUT Input);
```

## 文件结构

```
virtual-nvme-driver/
├── docs/                       # 文档目录
├── src/
│   ├── miniport/               # Miniport 驱动源码
│   │   ├── vnvme_main.c        # 驱动入口和 StorPort 回调
│   │   ├── vnvme_main.h
│   │   ├── vnvme_adapter.c     # 适配器管理
│   │   ├── vnvme_adapter.h
│   │   ├── vnvme_lun.c         # LUN 管理
│   │   ├── vnvme_lun.h
│   │   ├── vnvme_scsi.c        # SCSI 命令处理
│   │   ├── vnvme_scsi.h
│   │   ├── vnvme_backend.c     # 存储后端接口
│   │   ├── vnvme_backend.h
│   │   ├── vnvme_backend_memory.c  # 内存后端
│   │   ├── vnvme_backend_file.c    # 文件后端
│   │   ├── vnvme_backend_vhd.c     # VHD 后端
│   │   ├── vnvme_ioctl.c       # IOCTL 处理
│   │   ├── vnvme_ioctl.h
│   │   ├── vnvme_wmi.c         # WMI 支持 (可选)
│   │   ├── vnvme_trace.h       # WPP 跟踪
│   │   └── vnvme_common.h      # 公共定义
│   └── include/
│       ├── vnvme_public.h      # 公共头文件 (用户态/内核态共享)
│       └── scsi_defs.h         # SCSI 定义补充
├── inf/
│   └── vnvme.inf               # 驱动安装信息
├── test/
│   ├── vnvme_test.cpp          # 单元测试
│   └── stress_test.cpp         # 压力测试
└── tools/
    └── vnvmectl/               # 用户态控制工具
        ├── vnvmectl.cpp
        ├── vnvmectl.h
        └── vnvmectl.rc
```

## StorPort 回调实现

### DriverEntry 实现

```c
ULONG DriverEntry(
    _In_ PVOID DriverObject,
    _In_ PVOID RegistryPath)
{
    HW_INITIALIZATION_DATA hwInitData = {0};
    
    // 设置结构大小
    hwInitData.HwInitializationDataSize = sizeof(HW_INITIALIZATION_DATA);
    
    // 虚拟适配器使用 Internal 接口
    hwInitData.AdapterInterfaceType = Internal;
    
    // 必需的回调函数
    hwInitData.HwFindAdapter = VNvmeHwFindAdapter;
    hwInitData.HwInitialize = VNvmeHwInitialize;
    hwInitData.HwStartIo = VNvmeHwStartIo;
    hwInitData.HwResetBus = VNvmeHwResetBus;
    hwInitData.HwAdapterControl = VNvmeHwAdapterControl;
    
    // 可选优化回调
    hwInitData.HwBuildIo = VNvmeHwBuildIo;
    hwInitData.HwFreeAdapterResources = VNvmeHwFreeAdapterResources;
    
    // 虚拟设备不需要中断
    hwInitData.HwInterrupt = NULL;
    
    // 扩展结构大小
    hwInitData.DeviceExtensionSize = sizeof(VNVME_ADAPTER_EXTENSION);
    hwInitData.SpecificLuExtensionSize = sizeof(VNVME_LU_EXTENSION);
    hwInitData.SrbExtensionSize = sizeof(VNVME_SRB_EXTENSION);
    
    // 功能标志
    hwInitData.AutoRequestSense = TRUE;
    hwInitData.MultipleRequestPerLu = TRUE;
    hwInitData.TaggedQueuing = TRUE;
    hwInitData.ReceiveEvent = TRUE;
    
    // 初始化 StorPort
    return StorPortInitialize(
        DriverObject,
        RegistryPath,
        &hwInitData,
        NULL);
}
```

### HwFindAdapter 实现

```c
ULONG VNvmeHwFindAdapter(
    _In_ PVOID DeviceExtension,
    _In_ PVOID HwContext,
    _In_ PVOID BusInformation,
    _In_z_ PCHAR ArgumentString,
    _Inout_ PPORT_CONFIGURATION_INFORMATION ConfigInfo,
    _Out_ PUCHAR Again)
{
    PVNVME_ADAPTER_EXTENSION adapter = DeviceExtension;
    
    UNREFERENCED_PARAMETER(HwContext);
    UNREFERENCED_PARAMETER(BusInformation);
    UNREFERENCED_PARAMETER(ArgumentString);
    
    *Again = FALSE;
    
    //
    // 关键设置：标记为虚拟设备
    //
    ConfigInfo->VirtualDevice = TRUE;
    
    //
    // 配置适配器能力
    //
    
    // 最大传输大小 (4MB)
    ConfigInfo->MaximumTransferLength = 4 * 1024 * 1024;
    
    // 物理中断数 (虚拟设备不需要)
    ConfigInfo->NumberOfPhysicalBreaks = STORPORT_DEFAULT_PHYSICAL_BREAKS;
    
    // 对齐要求
    ConfigInfo->AlignmentMask = 0;  // 无对齐要求
    
    // 缓存对齐
    ConfigInfo->CachesData = TRUE;
    
    // 总线/目标/LUN 数量
    ConfigInfo->NumberOfBuses = 1;
    ConfigInfo->MaximumNumberOfTargets = VNVME_MAX_TARGETS;
    ConfigInfo->MaximumNumberOfLogicalUnits = VNVME_MAX_LUNS;
    
    // 同步模型 (全双工)
    ConfigInfo->SynchronizationModel = StorSynchronizeFullDuplex;
    
    // DMA 相关 (虚拟设备)
    ConfigInfo->MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS;
    ConfigInfo->NeedPhysicalAddresses = FALSE;
    ConfigInfo->ScatterGather = TRUE;
    ConfigInfo->Dma32BitAddresses = FALSE;
    ConfigInfo->Dma64BitAddresses = SCSI_DMA64_SYSTEM_SUPPORTED;
    
    // 重置目标超时
    ConfigInfo->ResetTargetSupported = TRUE;
    
    //
    // 初始化适配器扩展
    //
    RtlZeroMemory(adapter, sizeof(VNVME_ADAPTER_EXTENSION));
    adapter->AdapterExtension = adapter;
    adapter->State = VNVME_ADAPTER_STATE_INITIALIZING;
    
    KeInitializeSpinLock(&adapter->LunListLock);
    InitializeListHead(&adapter->LunList);
    
    // 读取注册表配置
    VNvmeLoadConfiguration(adapter, ArgumentString);
    
    return SP_RETURN_FOUND;
}
```

### HwInitialize 实现

```c
BOOLEAN VNvmeHwInitialize(
    _In_ PVOID DeviceExtension)
{
    PVNVME_ADAPTER_EXTENSION adapter = DeviceExtension;
    NTSTATUS status;
    
    //
    // 对于虚拟设备，此回调通常为空操作
    // 实际初始化在 HwFindAdapter 或首次 IOCTL 时完成
    //
    
    // 初始化后端管理器
    status = VNvmeBackendManagerInit();
    if (!NT_SUCCESS(status)) {
        StorPortLogError(adapter, NULL, 0, 0, 0,
            SP_INTERNAL_ADAPTER_ERROR, 
            VNVME_ERROR_BACKEND_INIT_FAILED);
        return FALSE;
    }
    
    // 设置适配器状态为就绪
    adapter->State = VNVME_ADAPTER_STATE_RUNNING;
    
    return TRUE;
}
```

### HwStartIo 实现

```c
BOOLEAN VNvmeHwStartIo(
    _In_ PVOID DeviceExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_ADAPTER_EXTENSION adapter = DeviceExtension;
    UCHAR srbStatus;
    
    //
    // 分发 SRB 请求
    //
    switch (Srb->Function) {
        
        case SRB_FUNCTION_EXECUTE_SCSI:
            srbStatus = VNvmeScsiExecute(adapter, Srb);
            break;
        
        case SRB_FUNCTION_IO_CONTROL:
            srbStatus = VNvmeIoctlProcess(adapter, Srb);
            break;
        
        case SRB_FUNCTION_RESET_LOGICAL_UNIT:
            srbStatus = VNvmeLunReset(adapter, Srb);
            break;
        
        case SRB_FUNCTION_RESET_DEVICE:
            srbStatus = VNvmeTargetReset(adapter, Srb);
            break;
        
        case SRB_FUNCTION_FLUSH:
        case SRB_FUNCTION_SHUTDOWN:
            srbStatus = VNvmeFlushAll(adapter, Srb);
            break;
        
        case SRB_FUNCTION_PNP:
            srbStatus = VNvmePnp(adapter, Srb);
            break;
        
        case SRB_FUNCTION_POWER:
            srbStatus = VNvmePower(adapter, Srb);
            break;
        
        case SRB_FUNCTION_WMI:
            srbStatus = VNvmeWmi(adapter, Srb);
            break;
        
        default:
            srbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }
    
    //
    // 完成请求
    //
    if (srbStatus != SRB_STATUS_PENDING) {
        Srb->SrbStatus = srbStatus;
        StorPortNotification(
            RequestComplete,
            adapter,
            Srb);
    }
    
    return TRUE;
}
```

### HwAdapterControl 实现

```c
SCSI_ADAPTER_CONTROL_STATUS VNvmeHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PVNVME_ADAPTER_EXTENSION adapter = DeviceExtension;
    SCSI_ADAPTER_CONTROL_STATUS status = ScsiAdapterControlSuccess;
    
    switch (ControlType) {
        
        case ScsiQuerySupportedControlTypes: {
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST list = Parameters;
            
            // 声明支持的控制类型
            if (list->MaxControlType > ScsiQuerySupportedControlTypes) {
                list->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            }
            if (list->MaxControlType > ScsiStopAdapter) {
                list->SupportedTypeList[ScsiStopAdapter] = TRUE;
            }
            if (list->MaxControlType > ScsiRestartAdapter) {
                list->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            }
            if (list->MaxControlType > ScsiPowerSettingNotification) {
                list->SupportedTypeList[ScsiPowerSettingNotification] = TRUE;
            }
            break;
        }
        
        case ScsiStopAdapter:
            // 停止适配器
            adapter->State = VNVME_ADAPTER_STATE_STOPPED;
            VNvmeAdapterFlushAll(adapter);
            break;
        
        case ScsiRestartAdapter:
            // 重启适配器
            adapter->State = VNVME_ADAPTER_STATE_RUNNING;
            break;
        
        case ScsiPowerSettingNotification: {
            PSTOR_POWER_SETTING_INFO powerInfo = Parameters;
            // 处理电源状态变更
            VNvmeHandlePowerChange(adapter, powerInfo);
            break;
        }
        
        default:
            status = ScsiAdapterControlUnsuccessful;
            break;
    }
    
    return status;
}
```

### HwResetBus 实现

```c
BOOLEAN VNvmeHwResetBus(
    _In_ PVOID DeviceExtension,
    _In_ ULONG PathId)
{
    PVNVME_ADAPTER_EXTENSION adapter = DeviceExtension;
    KIRQL oldIrql;
    
    UNREFERENCED_PARAMETER(PathId);
    
    //
    // 重置所有 LUN
    //
    KeAcquireSpinLock(&adapter->LunListLock, &oldIrql);
    
    PLIST_ENTRY entry = adapter->LunList.Flink;
    while (entry != &adapter->LunList) {
        PVNVME_LU_EXTENSION lu = CONTAINING_RECORD(
            entry, VNVME_LU_EXTENSION, ListEntry);
        
        // 重置 LUN 状态
        lu->State = VNVME_LUN_STATE_INITIALIZING;
        
        entry = entry->Flink;
    }
    
    KeReleaseSpinLock(&adapter->LunListLock, oldIrql);
    
    //
    // 通知 StorPort 完成所有未决请求
    //
    StorPortCompleteRequest(
        adapter,
        SP_UNTAGGED,        // PathId
        SP_UNTAGGED,        // TargetId
        SP_UNTAGGED,        // Lun
        SRB_STATUS_BUS_RESET);
    
    return TRUE;
}
```

## SCSI 命令处理

### VNvmeScsiExecute 实现

```c
UCHAR VNvmeScsiExecute(
    _In_ PVNVME_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_LU_EXTENSION lu;
    PCDB cdb = (PCDB)Srb->Cdb;
    UCHAR status;
    
    //
    // 查找目标 LUN
    //
    lu = VNvmeLunFind(
        AdapterExtension,
        Srb->PathId,
        Srb->TargetId,
        Srb->Lun);
    
    if (!lu) {
        // LUN 不存在
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        VNvmeSetSenseData(Srb, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ADSENSE_INVALID_LUN, 0);
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    //
    // 分发 SCSI 命令
    //
    switch (cdb->CDB6GENERIC.OperationCode) {
        
        // 状态查询
        case SCSIOP_TEST_UNIT_READY:
            status = VNvmeScsiTestUnitReady(lu, Srb);
            break;
        
        // 设备识别
        case SCSIOP_INQUIRY:
            status = VNvmeScsiInquiry(lu, Srb);
            break;
        
        // 容量查询
        case SCSIOP_READ_CAPACITY:
            status = VNvmeScsiReadCapacity(lu, Srb);
            break;
        
        case SCSIOP_READ_CAPACITY16:
            status = VNvmeScsiReadCapacity16(lu, Srb);
            break;
        
        // 读取操作
        case SCSIOP_READ6:
        case SCSIOP_READ:
        case SCSIOP_READ12:
        case SCSIOP_READ16:
            status = VNvmeScsiRead(lu, Srb);
            break;
        
        // 写入操作
        case SCSIOP_WRITE6:
        case SCSIOP_WRITE:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE16:
            status = VNvmeScsiWrite(lu, Srb);
            break;
        
        // 模式查询
        case SCSIOP_MODE_SENSE:
        case SCSIOP_MODE_SENSE10:
            status = VNvmeScsiModeSense(lu, Srb);
            break;
        
        // 缓存同步
        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            status = VNvmeScsiSynchronizeCache(lu, Srb);
            break;
        
        // TRIM/UNMAP
        case SCSIOP_UNMAP:
            status = VNvmeScsiUnmap(lu, Srb);
            break;
        
        // 启动/停止
        case SCSIOP_START_STOP_UNIT:
            status = VNvmeScsiStartStopUnit(lu, Srb);
            break;
        
        default:
            // 不支持的命令
            Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
            VNvmeSetSenseData(Srb, SCSI_SENSE_ILLEGAL_REQUEST,
                SCSI_ADSENSE_ILLEGAL_COMMAND, 0);
            status = SRB_STATUS_INVALID_REQUEST;
            break;
    }
    
    return status;
}
```

### INQUIRY 命令处理

```c
UCHAR VNvmeScsiInquiry(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PINQUIRYDATA inquiryData = Srb->DataBuffer;
    ULONG dataLength;
    
    // 验证缓冲区大小
    dataLength = min(Srb->DataTransferLength, sizeof(INQUIRYDATA));
    if (dataLength < INQUIRYDATABUFFERSIZE) {
        return SRB_STATUS_DATA_OVERRUN;
    }
    
    RtlZeroMemory(inquiryData, dataLength);
    
    // 检查是否请求 VPD 页
    if (cdb->CDB6INQUIRY3.EnableVitalProductData) {
        return VNvmeScsiVpdPages(LuExtension, Srb);
    }
    
    //
    // 标准 INQUIRY 数据
    //
    
    // 设备类型: 磁盘
    inquiryData->DeviceType = DIRECT_ACCESS_DEVICE;
    inquiryData->DeviceTypeQualifier = DEVICE_CONNECTED;
    
    // 可移除
    inquiryData->RemovableMedia = FALSE;
    
    // 版本 (SPC-4)
    inquiryData->Versions = 0x06;
    
    // 响应格式
    inquiryData->ResponseDataFormat = 2;  // SPC-2
    
    // 附加长度
    inquiryData->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
    
    // 功能标志
    inquiryData->CommandQueue = TRUE;     // 支持命令队列
    inquiryData->Wide16Bit = FALSE;
    inquiryData->Synchronous = FALSE;
    inquiryData->LinkedCommands = FALSE;
    
    // 厂商标识
    RtlCopyMemory(inquiryData->VendorId, "VNVME   ", 8);
    
    // 产品标识
    RtlCopyMemory(inquiryData->ProductId, "Virtual NVMe    ", 16);
    
    // 产品版本
    RtlCopyMemory(inquiryData->ProductRevisionLevel, "1.0 ", 4);
    
    Srb->DataTransferLength = dataLength;
    return SRB_STATUS_SUCCESS;
}
```

### READ/WRITE 命令处理

```c
UCHAR VNvmeScsiRead(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    ULONGLONG lba;
    ULONG sectors;
    ULONGLONG offset;
    ULONG length;
    NTSTATUS status;
    
    //
    // 解析 CDB 获取 LBA 和扇区数
    //
    switch (cdb->CDB6GENERIC.OperationCode) {
        case SCSIOP_READ6:
            lba = ((cdb->CDB6READWRITE.LogicalBlockMsb1 & 0x1F) << 16) |
                  (cdb->CDB6READWRITE.LogicalBlockMsb0 << 8) |
                  cdb->CDB6READWRITE.LogicalBlockLsb;
            sectors = cdb->CDB6READWRITE.TransferBlocks;
            if (sectors == 0) sectors = 256;
            break;
        
        case SCSIOP_READ:  // READ10
            REVERSE_BYTES(&lba, &cdb->CDB10.LogicalBlockByte0);
            REVERSE_BYTES_SHORT(&sectors, &cdb->CDB10.TransferBlocksMsb);
            break;
        
        case SCSIOP_READ12:
            REVERSE_BYTES(&lba, &cdb->CDB12.LogicalBlock);
            REVERSE_BYTES(&sectors, &cdb->CDB12.TransferLength);
            break;
        
        case SCSIOP_READ16:
            REVERSE_BYTES_QUAD(&lba, &cdb->CDB16.LogicalBlock);
            REVERSE_BYTES(&sectors, &cdb->CDB16.TransferLength);
            break;
        
        default:
            return SRB_STATUS_INVALID_REQUEST;
    }
    
    //
    // 边界检查
    //
    if (lba + sectors > LuExtension->TotalSectors) {
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        VNvmeSetSenseData(Srb, SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ADSENSE_ILLEGAL_BLOCK, 0);
        return SRB_STATUS_INVALID_REQUEST;
    }
    
    //
    // 计算偏移和长度
    //
    offset = lba * LuExtension->SectorSize;
    length = sectors * LuExtension->SectorSize;
    
    //
    // 执行后端读取
    //
    status = VNvmeBackendRead(
        LuExtension->Backend,
        offset,
        length,
        Srb->DataBuffer);
    
    if (!NT_SUCCESS(status)) {
        Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
        VNvmeSetSenseData(Srb, SCSI_SENSE_MEDIUM_ERROR,
            SCSI_ADSENSE_UNRECOVERED_ERROR, 0);
        return SRB_STATUS_ERROR;
    }
    
    Srb->DataTransferLength = length;
    return SRB_STATUS_SUCCESS;
}

// WRITE 命令类似，调用 VNvmeBackendWrite()
```

## 适配器状态机

```
┌─────────────────────────────────────────────────────────────┐
│                    适配器状态机                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  UNINITIALIZED ──[HwFindAdapter]──► INITIALIZING           │
│                                           │                 │
│                                     [HwInitialize]          │
│                                           │                 │
│                                           ▼                 │
│  STOPPED ◄──[ScsiStopAdapter]────── RUNNING                │
│     │                                     │                 │
│     │                              [HwResetBus]             │
│  [ScsiRestartAdapter]                     │                 │
│     │                                     ▼                 │
│     └────────────────────────────► RESETTING ──► RUNNING   │
│                                                             │
│  任意状态 ──[致命错误]──► ERROR                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

```c
typedef enum _VNVME_ADAPTER_STATE {
    VNVME_ADAPTER_STATE_UNINITIALIZED = 0,
    VNVME_ADAPTER_STATE_INITIALIZING,
    VNVME_ADAPTER_STATE_RUNNING,
    VNVME_ADAPTER_STATE_STOPPED,
    VNVME_ADAPTER_STATE_RESETTING,
    VNVME_ADAPTER_STATE_ERROR
} VNVME_ADAPTER_STATE;
```

## 内存管理

### StorPort 内存分配 API

```c
// 使用 StorPort 提供的内存分配
PVOID buffer = StorPortAllocatePool(
    AdapterExtension,
    sizeof(MY_STRUCT),
    VNVME_POOL_TAG,
    &status);

// 释放
StorPortFreePool(AdapterExtension, buffer);

// 用于 DMA 的连续内存 (虚拟设备通常不需要)
STOR_PHYSICAL_ADDRESS physAddr;
PVOID dmaBuffer = StorPortGetPhysicalAddress(
    AdapterExtension,
    Srb,
    virtualAddress,
    &length);
```

### 后端缓冲区管理

```c
// 对于大容量后端，使用分页池
PVOID buffer = ExAllocatePool2(
    POOL_FLAG_PAGED,
    size,
    VNVME_POOL_TAG);

// 对于小型频繁分配，使用 Lookaside List
NPAGED_LOOKASIDE_LIST ioctlPool;
ExInitializeNPagedLookasideList(
    &ioctlPool,
    NULL,
    NULL,
    POOL_NX_ALLOCATION,
    sizeof(VNVME_IOCTL_CONTEXT),
    VNVME_POOL_TAG,
    0);
```

## WPP 跟踪

```c
// vnvme_trace.h

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(VNvmeTraceGuid, \
        (a1b2c3d4,e5f6,7890,ab,cd,ef,12,34,56,78,90), \
        WPP_DEFINE_BIT(VNVME_INIT) \
        WPP_DEFINE_BIT(VNVME_IO) \
        WPP_DEFINE_BIT(VNVME_SCSI) \
        WPP_DEFINE_BIT(VNVME_BACKEND) \
        WPP_DEFINE_BIT(VNVME_ERROR))

// 使用
DoTraceMessage(VNVME_IO, "Read: LBA=%I64u, Sectors=%u", lba, sectors);
DoTraceMessage(VNVME_ERROR, "Backend read failed: Status=%!STATUS!", status);
```

## 测试支持

### 调试版特性

```c
#if DBG

// 故障注入
typedef struct _VNVME_FAULT_INJECTION {
    BOOLEAN InjectReadError;
    BOOLEAN InjectWriteError;
    ULONG FailAfterNIos;
    ULONG IoCount;
} VNVME_FAULT_INJECTION;

// 性能计数器
typedef struct _VNVME_PERF_COUNTERS {
    LONG64 TotalReads;
    LONG64 TotalWrites;
    LONG64 TotalBytesRead;
    LONG64 TotalBytesWritten;
    LONG64 ReadErrors;
    LONG64 WriteErrors;
} VNVME_PERF_COUNTERS;

#endif
```

