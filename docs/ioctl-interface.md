# IOCTL 管理接口

本文档定义 Virtual NVMe StorPort Miniport 驱动的用户态管理接口。

## 概述

在 StorPort Virtual Miniport 架构中，管理控制有两种实现方式：

1. **SCSI Pass-through**: 利用标准 SCSI 接口发送厂商特定命令
2. **WMI 接口**: 通过 Windows Management Instrumentation 提供管理功能
3. **辅助管理驱动**: 独立的 KMDF 驱动提供 IOCTL 接口（可选）

本驱动采用 **SCSI Pass-through** + **WMI** 的组合方案，无需额外的管理驱动。

---

## 架构设计

### 管理接口位置

```
┌─────────────────────────────────────────────────────────────────┐
│                    用户态管理程序 (vnvme-cli.exe)                │
│                                                                  │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐  │
│  │ CreateFile()    │  │ WMI API         │  │ SetupDi API     │  │
│  │ DeviceIoControl │  │ IWbemServices   │  │ 设备枚举        │  │
│  └────────┬────────┘  └────────┬────────┘  └────────┬────────┘  │
└───────────┼─────────────────────┼─────────────────────┼─────────┘
            │                     │                     │
            │ IOCTL_SCSI_PASS_    │ WMI Queries/        │ PnP 枚举
            │ THROUGH_DIRECT      │ Methods             │
            ▼                     ▼                     ▼
┌─────────────────────────────────────────────────────────────────┐
│                          内核模式                                │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    disk.sys (类驱动)                         ││
│  │         转发 SCSI_PASS_THROUGH 和其他 IOCTL                  ││
│  └──────────────────────────┬──────────────────────────────────┘│
│                             │                                    │
│  ┌──────────────────────────▼──────────────────────────────────┐│
│  │                    storport.sys                              ││
│  │              WMI Provider / SRB 路由                         ││
│  └──────────────────────────┬──────────────────────────────────┘│
│                             │ SRB (SCSI_REQUEST_BLOCK)          │
│  ┌──────────────────────────▼──────────────────────────────────┐│
│  │                  ★ vnvme.sys (Miniport) ★                   ││
│  │                                                              ││
│  │  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐ ││
│  │  │ SCSI 命令处理  │  │ WMI 回调实现   │  │ LUN/Disk 管理  │ ││
│  │  │                │  │                │  │                │ ││
│  │  │ ◆ 标准 SCSI    │  │ ◆ MOF 定义     │  │ ◆ 热插拔       │ ││
│  │  │ ◆ 厂商命令     │  │ ◆ 查询/设置    │  │ ◆ 配置管理     │ ││
│  │  └────────────────┘  └────────────────┘  └────────────────┘ ││
│  └──────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
```

---

## 方法一：SCSI Pass-through

### 厂商特定 CDB 设计

使用 SCSI 厂商特定操作码 (0xC0-0xFF) 实现管理命令：

```c
//
// 厂商特定操作码定义
//
#define VNVME_SCSI_OPCODE_VENDOR_BASE   0xC0

// 管理命令操作码
#define SCSIOP_VNVME_GET_ADAPTER_INFO   0xC0    // 获取适配器信息
#define SCSIOP_VNVME_GET_LUN_INFO       0xC1    // 获取 LUN 信息
#define SCSIOP_VNVME_CREATE_LUN         0xC2    // 创建 LUN
#define SCSIOP_VNVME_DELETE_LUN         0xC3    // 删除 LUN
#define SCSIOP_VNVME_MODIFY_LUN         0xC4    // 修改 LUN 属性
#define SCSIOP_VNVME_GET_STATISTICS     0xC5    // 获取统计信息
#define SCSIOP_VNVME_RESET_STATISTICS   0xC6    // 重置统计信息
#define SCSIOP_VNVME_GET_VERSION        0xCF    // 获取驱动版本
```

### CDB 格式定义

```c
//
// 厂商特定 CDB 格式 (16 字节)
//

// 通用厂商命令 CDB
#pragma pack(push, 1)
typedef struct _VNVME_CDB16 {
    UCHAR OperationCode;        // [0] 0xC0-0xCF
    UCHAR ServiceAction;        // [1] 子命令
    UCHAR LunId;                // [2] 目标 LUN ID
    UCHAR Reserved1;            // [3]
    UCHAR Parameter[8];         // [4-11] 命令参数
    ULONG AllocationLength;     // [12-15] 数据传输长度 (大端)
} VNVME_CDB16, *PVNVME_CDB16;
#pragma pack(pop)

// 创建 LUN 命令 CDB
#pragma pack(push, 1)
typedef struct _VNVME_CDB_CREATE_LUN {
    UCHAR OperationCode;        // [0] 0xC2
    UCHAR BackendType;          // [1] 后端类型
    UCHAR Reserved1[2];         // [2-3]
    ULONG BlockSize;            // [4-7] 块大小 (大端)
    ULONG Reserved2;            // [8-11]
    ULONG AllocationLength;     // [12-15] 数据长度 (大端)
} VNVME_CDB_CREATE_LUN, *PVNVME_CDB_CREATE_LUN;
#pragma pack(pop)
```

### 数据传输结构

```c
//
// 适配器信息 (GET_ADAPTER_INFO 返回)
//
#pragma pack(push, 1)
typedef struct _VNVME_ADAPTER_INFO {
    ULONG       StructureSize;          // 结构大小
    ULONG       Version;                // 驱动版本 (MAJOR.MINOR.BUILD)
    ULONG       MaxLuns;                // 最大 LUN 数量
    ULONG       CurrentLuns;            // 当前 LUN 数量
    ULONG       MaxTransferLength;      // 最大传输长度
    ULONG       QueueDepth;             // 队列深度 (250)
    UCHAR       AdapterId[8];           // 适配器唯一 ID
    WCHAR       DriverPath[260];        // 驱动路径
} VNVME_ADAPTER_INFO, *PVNVME_ADAPTER_INFO;
#pragma pack(pop)

//
// LUN 信息 (GET_LUN_INFO 返回)
//
#pragma pack(push, 1)
typedef struct _VNVME_LUN_INFO {
    ULONG       StructureSize;          // 结构大小
    UCHAR       LunId;                  // LUN ID (0-254)
    UCHAR       BackendType;            // 后端类型
    UCHAR       State;                  // LUN 状态
    UCHAR       Flags;                  // 标志位
    ULONGLONG   TotalSizeBytes;         // 总容量
    ULONG       BlockSize;              // 块大小
    ULONGLONG   BlockCount;             // 块数量
    CHAR        SerialNumber[20];       // 序列号
    CHAR        ModelNumber[40];        // 型号
    WCHAR       BackendPath[260];       // 后端路径
} VNVME_LUN_INFO, *PVNVME_LUN_INFO;
#pragma pack(pop)

//
// 创建 LUN 参数 (CREATE_LUN 输入)
//
#pragma pack(push, 1)
typedef struct _VNVME_CREATE_LUN_PARAMS {
    ULONG       StructureSize;          // 结构大小
    UCHAR       RequestedLunId;         // 请求的 LUN ID (0xFF = 自动)
    UCHAR       BackendType;            // 后端类型
    UCHAR       Reserved[2];            // 保留
    ULONGLONG   SizeBytes;              // 磁盘大小
    ULONG       BlockSize;              // 块大小 (512 或 4096)
    ULONG       Flags;                  // 创建标志
    CHAR        SerialNumber[20];       // 自定义序列号 (可选)
    WCHAR       BackendPath[260];       // 后端路径 (文件/VHD)
} VNVME_CREATE_LUN_PARAMS, *PVNVME_CREATE_LUN_PARAMS;
#pragma pack(pop)

//
// 创建 LUN 结果 (CREATE_LUN 输出)
//
#pragma pack(push, 1)
typedef struct _VNVME_CREATE_LUN_RESULT {
    ULONG       StructureSize;          // 结构大小
    UCHAR       AssignedLunId;          // 分配的 LUN ID
    UCHAR       Status;                 // 操作状态
    UCHAR       Reserved[2];            // 保留
    ULONG       ErrorCode;              // 错误码 (如果失败)
} VNVME_CREATE_LUN_RESULT, *PVNVME_CREATE_LUN_RESULT;
#pragma pack(pop)

//
// 后端类型枚举
//
typedef enum _VNVME_BACKEND_TYPE {
    VNVME_BACKEND_MEMORY = 0,           // 内存后端
    VNVME_BACKEND_FILE = 1,             // 文件后端
    VNVME_BACKEND_VHD = 2,              // VHD/VHDX 后端
    VNVME_BACKEND_REMOTE = 3,           // 远程后端
    VNVME_BACKEND_MAX
} VNVME_BACKEND_TYPE;

//
// LUN 状态枚举
//
typedef enum _VNVME_LUN_STATE {
    VNVME_LUN_STATE_OFFLINE = 0,        // 离线
    VNVME_LUN_STATE_ONLINE = 1,         // 在线
    VNVME_LUN_STATE_DEGRADED = 2,       // 降级
    VNVME_LUN_STATE_FAILED = 3          // 故障
} VNVME_LUN_STATE;

//
// 创建标志
//
#define VNVME_CREATE_FLAG_READ_ONLY         0x00000001  // 只读
#define VNVME_CREATE_FLAG_PERSISTENT        0x00000002  // 持久化配置
#define VNVME_CREATE_FLAG_THIN_PROVISION    0x00000004  // 精简配置
#define VNVME_CREATE_FLAG_FORCE             0x00000008  // 强制创建
```

### 用户态使用示例

```c
#include <windows.h>
#include <ntddscsi.h>
#include <stdio.h>

//
// 发送厂商特定 SCSI 命令
//
BOOL VNvmeSendVendorCommand(
    HANDLE hDevice,
    UCHAR OperationCode,
    UCHAR LunId,
    PVOID DataBuffer,
    ULONG DataBufferLength,
    BOOL DataIn)
{
    UCHAR buffer[sizeof(SCSI_PASS_THROUGH_DIRECT) + 32];
    PSCSI_PASS_THROUGH_DIRECT pSptd = (PSCSI_PASS_THROUGH_DIRECT)buffer;
    DWORD bytesReturned;
    
    ZeroMemory(buffer, sizeof(buffer));
    
    pSptd->Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    pSptd->PathId = 0;
    pSptd->TargetId = 0;
    pSptd->Lun = LunId;
    pSptd->CdbLength = 16;
    pSptd->SenseInfoLength = 32;
    pSptd->DataIn = DataIn ? SCSI_IOCTL_DATA_IN : SCSI_IOCTL_DATA_OUT;
    pSptd->DataTransferLength = DataBufferLength;
    pSptd->TimeOutValue = 30;
    pSptd->DataBuffer = DataBuffer;
    pSptd->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH_DIRECT);
    
    // 构建 CDB
    pSptd->Cdb[0] = OperationCode;
    pSptd->Cdb[2] = LunId;
    // 传输长度 (大端)
    pSptd->Cdb[12] = (DataBufferLength >> 24) & 0xFF;
    pSptd->Cdb[13] = (DataBufferLength >> 16) & 0xFF;
    pSptd->Cdb[14] = (DataBufferLength >> 8) & 0xFF;
    pSptd->Cdb[15] = DataBufferLength & 0xFF;
    
    return DeviceIoControl(
        hDevice,
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        pSptd,
        sizeof(buffer),
        pSptd,
        sizeof(buffer),
        &bytesReturned,
        NULL);
}

//
// 获取适配器信息
//
BOOL VNvmeGetAdapterInfo(HANDLE hDevice, PVNVME_ADAPTER_INFO pInfo)
{
    pInfo->StructureSize = sizeof(VNVME_ADAPTER_INFO);
    
    return VNvmeSendVendorCommand(
        hDevice,
        SCSIOP_VNVME_GET_ADAPTER_INFO,
        0,      // LUN 0 用于适配器级命令
        pInfo,
        sizeof(VNVME_ADAPTER_INFO),
        TRUE    // 读取数据
    );
}

//
// 获取 LUN 信息
//
BOOL VNvmeGetLunInfo(HANDLE hDevice, UCHAR LunId, PVNVME_LUN_INFO pInfo)
{
    pInfo->StructureSize = sizeof(VNVME_LUN_INFO);
    
    return VNvmeSendVendorCommand(
        hDevice,
        SCSIOP_VNVME_GET_LUN_INFO,
        LunId,
        pInfo,
        sizeof(VNVME_LUN_INFO),
        TRUE
    );
}

//
// 创建新 LUN
//
BOOL VNvmeCreateLun(
    HANDLE hDevice,
    PVNVME_CREATE_LUN_PARAMS pParams,
    PVNVME_CREATE_LUN_RESULT pResult)
{
    UCHAR buffer[sizeof(SCSI_PASS_THROUGH_DIRECT) + 32 + 
                 sizeof(VNVME_CREATE_LUN_PARAMS)];
    PSCSI_PASS_THROUGH_DIRECT pSptd = (PSCSI_PASS_THROUGH_DIRECT)buffer;
    PVNVME_CREATE_LUN_PARAMS pData;
    DWORD bytesReturned;
    
    ZeroMemory(buffer, sizeof(buffer));
    
    pSptd->Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    pSptd->CdbLength = 16;
    pSptd->SenseInfoLength = 32;
    pSptd->DataIn = SCSI_IOCTL_DATA_OUT;  // 写入参数
    pSptd->DataTransferLength = sizeof(VNVME_CREATE_LUN_PARAMS);
    pSptd->TimeOutValue = 60;
    pSptd->DataBuffer = pParams;
    pSptd->SenseInfoOffset = sizeof(SCSI_PASS_THROUGH_DIRECT);
    
    // 构建 CDB
    PVNVME_CDB_CREATE_LUN pCdb = (PVNVME_CDB_CREATE_LUN)pSptd->Cdb;
    pCdb->OperationCode = SCSIOP_VNVME_CREATE_LUN;
    pCdb->BackendType = pParams->BackendType;
    // BlockSize 大端存储
    pCdb->BlockSize = _byteswap_ulong(pParams->BlockSize);
    pCdb->AllocationLength = _byteswap_ulong(sizeof(VNVME_CREATE_LUN_PARAMS));
    
    if (!DeviceIoControl(
            hDevice,
            IOCTL_SCSI_PASS_THROUGH_DIRECT,
            pSptd,
            sizeof(buffer),
            pSptd,
            sizeof(buffer),
            &bytesReturned,
            NULL)) {
        return FALSE;
    }
    
    // 读取结果 (通过另一个命令或解析 sense data)
    // 这里简化处理
    pResult->Status = 0;  // 成功
    pResult->AssignedLunId = pParams->RequestedLunId;
    
    return TRUE;
}

//
// 示例：创建 1GB 内存磁盘
//
void ExampleCreateMemoryDisk(void)
{
    HANDLE hDevice = CreateFile(
        L"\\\\.\\PhysicalDrive1",  // 假设是第一个 VNvme 设备
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        0,
        NULL);
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        printf("Failed to open device\n");
        return;
    }
    
    VNVME_CREATE_LUN_PARAMS params = {0};
    VNVME_CREATE_LUN_RESULT result = {0};
    
    params.StructureSize = sizeof(params);
    params.RequestedLunId = 0xFF;  // 自动分配
    params.BackendType = VNVME_BACKEND_MEMORY;
    params.SizeBytes = 1ULL * 1024 * 1024 * 1024;  // 1 GB
    params.BlockSize = 512;
    params.Flags = 0;
    
    if (VNvmeCreateLun(hDevice, &params, &result)) {
        printf("LUN created: ID=%d\n", result.AssignedLunId);
    } else {
        printf("Failed to create LUN: %d\n", GetLastError());
    }
    
    CloseHandle(hDevice);
}
```

---

## 方法二：WMI 接口

### MOF 文件定义

```mof
// vnvme.mof - WMI 类定义

#pragma namespace("\\\\.\\Root\\WMI")

//
// 适配器信息类
//
[WMI,
 Dynamic,
 Provider("WMIProv"),
 guid("{12345678-1234-1234-1234-123456789ABC}"),
 DisplayName("VNvme Adapter Information"),
 Description("Virtual NVMe Adapter Information")]
class VNvmeAdapterInfo
{
    [key, read]
    string InstanceName;
    
    [read]
    boolean Active;
    
    [WmiDataId(1), read,
     DisplayName("Driver Version"),
     Description("Driver version string")]
    string DriverVersion;
    
    [WmiDataId(2), read,
     DisplayName("Maximum LUNs"),
     Description("Maximum number of LUNs supported")]
    uint32 MaxLuns;
    
    [WmiDataId(3), read,
     DisplayName("Current LUN Count"),
     Description("Number of LUNs currently configured")]
    uint32 CurrentLunCount;
    
    [WmiDataId(4), read,
     DisplayName("Queue Depth"),
     Description("Maximum queue depth")]
    uint32 QueueDepth;
};

//
// LUN 信息类
//
[WMI,
 Dynamic,
 Provider("WMIProv"),
 guid("{12345678-1234-1234-1234-123456789ABD}"),
 DisplayName("VNvme LUN Information"),
 Description("Virtual NVMe LUN Information")]
class VNvmeLunInfo
{
    [key, read]
    string InstanceName;
    
    [read]
    boolean Active;
    
    [WmiDataId(1), read,
     DisplayName("LUN ID")]
    uint8 LunId;
    
    [WmiDataId(2), read,
     DisplayName("Backend Type")]
    uint8 BackendType;
    
    [WmiDataId(3), read,
     DisplayName("State")]
    uint8 State;
    
    [WmiDataId(4), read,
     DisplayName("Total Size (Bytes)")]
    uint64 TotalSizeBytes;
    
    [WmiDataId(5), read,
     DisplayName("Block Size")]
    uint32 BlockSize;
    
    [WmiDataId(6), read,
     DisplayName("Serial Number")]
    string SerialNumber;
};

//
// 统计信息类
//
[WMI,
 Dynamic,
 Provider("WMIProv"),
 guid("{12345678-1234-1234-1234-123456789ABE}"),
 DisplayName("VNvme Statistics"),
 Description("Virtual NVMe Statistics")]
class VNvmeStatistics
{
    [key, read]
    string InstanceName;
    
    [read]
    boolean Active;
    
    [WmiDataId(1), read,
     DisplayName("Read Operations")]
    uint64 ReadOperations;
    
    [WmiDataId(2), read,
     DisplayName("Write Operations")]
    uint64 WriteOperations;
    
    [WmiDataId(3), read,
     DisplayName("Bytes Read")]
    uint64 BytesRead;
    
    [WmiDataId(4), read,
     DisplayName("Bytes Written")]
    uint64 BytesWritten;
    
    [WmiDataId(5), read,
     DisplayName("Average Read Latency (us)")]
    uint64 AvgReadLatencyUs;
    
    [WmiDataId(6), read,
     DisplayName("Average Write Latency (us)")]
    uint64 AvgWriteLatencyUs;
};

//
// 管理方法类
//
[WMI,
 Dynamic,
 Provider("WMIProv"),
 guid("{12345678-1234-1234-1234-123456789ABF}"),
 DisplayName("VNvme Management Methods")]
class VNvmeManagement
{
    [key, read]
    string InstanceName;
    
    [read]
    boolean Active;
    
    // 创建 LUN 方法
    [WmiMethodId(1),
     Implemented,
     DisplayName("Create LUN"),
     Description("Create a new virtual LUN")]
    void CreateLun(
        [in, DisplayName("Backend Type")] uint8 BackendType,
        [in, DisplayName("Size (Bytes)")] uint64 SizeBytes,
        [in, DisplayName("Block Size")] uint32 BlockSize,
        [in, DisplayName("Backend Path")] string BackendPath,
        [out, DisplayName("Assigned LUN ID")] uint8 LunId,
        [out, DisplayName("Status")] uint32 Status
    );
    
    // 删除 LUN 方法
    [WmiMethodId(2),
     Implemented,
     DisplayName("Delete LUN"),
     Description("Delete an existing virtual LUN")]
    void DeleteLun(
        [in, DisplayName("LUN ID")] uint8 LunId,
        [out, DisplayName("Status")] uint32 Status
    );
    
    // 重置统计方法
    [WmiMethodId(3),
     Implemented,
     DisplayName("Reset Statistics"),
     Description("Reset all statistics counters")]
    void ResetStatistics(
        [out, DisplayName("Status")] uint32 Status
    );
};
```

### 驱动端 WMI 实现

```c
//
// WMI GUID 定义
//
GUID VNvmeAdapterInfoGuid = {0x12345678, 0x1234, 0x1234,
    {0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC}};

GUID VNvmeLunInfoGuid = {0x12345678, 0x1234, 0x1234,
    {0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBD}};

GUID VNvmeStatisticsGuid = {0x12345678, 0x1234, 0x1234,
    {0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBE}};

GUID VNvmeManagementGuid = {0x12345678, 0x1234, 0x1234,
    {0x12, 0x34, 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBF}};

//
// WMI 数据块列表
//
SCSIWMIGUIDREGINFO VNvmeWmiGuidList[] = {
    { &VNvmeAdapterInfoGuid, 1, 0 },
    { &VNvmeLunInfoGuid, VNVME_MAX_LUNS, 0 },
    { &VNvmeStatisticsGuid, 1, 0 },
    { &VNvmeManagementGuid, 1, WMIREG_FLAG_EXPENSIVE }
};

#define VNVME_WMI_GUID_COUNT (sizeof(VNvmeWmiGuidList) / sizeof(SCSIWMIGUIDREGINFO))

//
// HwAdapterControl - WMI 相关处理
//
SCSI_ADAPTER_CONTROL_STATUS
VNvmeHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    PSCSI_WMI_REQUEST_BLOCK pSrb;
    
    switch (ControlType) {
        case ScsiQuerySupportedControlTypes: {
            PSCSI_SUPPORTED_CONTROL_TYPE_LIST pList = Parameters;
            ULONG i;
            
            for (i = 0; i < pList->MaxControlType; i++) {
                pList->SupportedTypeList[i] = FALSE;
            }
            
            pList->SupportedTypeList[ScsiQuerySupportedControlTypes] = TRUE;
            pList->SupportedTypeList[ScsiStopAdapter] = TRUE;
            pList->SupportedTypeList[ScsiRestartAdapter] = TRUE;
            pList->SupportedTypeList[ScsiSetBootConfig] = TRUE;
            pList->SupportedTypeList[ScsiSetRunningConfig] = TRUE;
            
            return ScsiAdapterControlSuccess;
        }
        
        case ScsiStopAdapter:
            // 停止适配器
            return ScsiAdapterControlSuccess;
            
        case ScsiRestartAdapter:
            // 重启适配器
            return ScsiAdapterControlSuccess;
            
        default:
            return ScsiAdapterControlUnsuccessful;
    }
}

//
// WMI SRB 处理
//
BOOLEAN
VNvmeHandleWmiSrb(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _In_ PSCSI_WMI_REQUEST_BLOCK pSrb)
{
    UCHAR minorFunction = pSrb->WMISubFunction;
    UCHAR status = SRB_STATUS_SUCCESS;
    SCSIWMI_REQUEST_CONTEXT context;
    
    // 初始化 WMI 上下文
    context.UserContext = pAdapter;
    
    switch (minorFunction) {
        case IRP_MN_REGINFO:
        case IRP_MN_REGINFO_EX:
            // 注册 WMI 数据块
            status = ScsiPortWmiDispatchFunction(
                &VNvmeWmiContext,
                minorFunction,
                pAdapter,
                &context,
                pSrb->DataPath,
                pSrb->DataTransferLength,
                pSrb->DataBuffer);
            break;
            
        case IRP_MN_QUERY_ALL_DATA:
        case IRP_MN_QUERY_SINGLE_INSTANCE:
            // 查询数据
            status = VNvmeWmiQueryData(pAdapter, pSrb);
            break;
            
        case IRP_MN_EXECUTE_METHOD:
            // 执行方法
            status = VNvmeWmiExecuteMethod(pAdapter, pSrb);
            break;
            
        default:
            status = SRB_STATUS_INVALID_REQUEST;
            break;
    }
    
    pSrb->SrbStatus = status;
    return TRUE;
}
```

### 用户态 WMI 访问示例

```cpp
// C++ WMI 访问示例
#include <windows.h>
#include <wbemidl.h>
#include <comdef.h>
#include <stdio.h>

#pragma comment(lib, "wbemuuid.lib")

class VNvmeWmiClient
{
private:
    IWbemLocator* pLocator;
    IWbemServices* pServices;
    
public:
    VNvmeWmiClient() : pLocator(nullptr), pServices(nullptr) {}
    
    ~VNvmeWmiClient() {
        Disconnect();
    }
    
    HRESULT Connect()
    {
        HRESULT hr;
        
        hr = CoInitializeEx(0, COINIT_MULTITHREADED);
        if (FAILED(hr)) return hr;
        
        hr = CoInitializeSecurity(
            NULL, -1, NULL, NULL,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            NULL, EOAC_NONE, NULL);
        
        hr = CoCreateInstance(
            CLSID_WbemLocator, 0,
            CLSCTX_INPROC_SERVER,
            IID_IWbemLocator,
            (LPVOID*)&pLocator);
        
        if (FAILED(hr)) return hr;
        
        hr = pLocator->ConnectServer(
            _bstr_t(L"ROOT\\WMI"),
            NULL, NULL, 0, NULL, 0, 0,
            &pServices);
        
        return hr;
    }
    
    void Disconnect()
    {
        if (pServices) { pServices->Release(); pServices = nullptr; }
        if (pLocator) { pLocator->Release(); pLocator = nullptr; }
        CoUninitialize();
    }
    
    //
    // 获取适配器信息
    //
    HRESULT GetAdapterInfo()
    {
        IEnumWbemClassObject* pEnumerator = NULL;
        HRESULT hr;
        
        hr = pServices->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT * FROM VNvmeAdapterInfo"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            NULL, &pEnumerator);
        
        if (FAILED(hr)) return hr;
        
        IWbemClassObject* pObj = NULL;
        ULONG returned = 0;
        
        while (pEnumerator->Next(WBEM_INFINITE, 1, &pObj, &returned) == S_OK) {
            VARIANT var;
            
            if (SUCCEEDED(pObj->Get(L"DriverVersion", 0, &var, 0, 0))) {
                wprintf(L"Driver Version: %s\n", var.bstrVal);
                VariantClear(&var);
            }
            
            if (SUCCEEDED(pObj->Get(L"MaxLuns", 0, &var, 0, 0))) {
                wprintf(L"Max LUNs: %u\n", var.ulVal);
                VariantClear(&var);
            }
            
            if (SUCCEEDED(pObj->Get(L"CurrentLunCount", 0, &var, 0, 0))) {
                wprintf(L"Current LUNs: %u\n", var.ulVal);
                VariantClear(&var);
            }
            
            pObj->Release();
        }
        
        pEnumerator->Release();
        return S_OK;
    }
    
    //
    // 创建 LUN (调用 WMI 方法)
    //
    HRESULT CreateLun(
        BYTE backendType,
        ULONGLONG sizeBytes,
        ULONG blockSize,
        LPCWSTR backendPath,
        BYTE* pLunId)
    {
        IWbemClassObject* pClass = NULL;
        IWbemClassObject* pInParams = NULL;
        IWbemClassObject* pOutParams = NULL;
        HRESULT hr;
        
        // 获取类定义
        hr = pServices->GetObject(
            bstr_t("VNvmeManagement"),
            0, NULL, &pClass, NULL);
        
        if (FAILED(hr)) return hr;
        
        // 获取方法输入参数
        hr = pClass->GetMethod(L"CreateLun", 0, &pInParams, NULL);
        if (FAILED(hr)) {
            pClass->Release();
            return hr;
        }
        
        // 设置参数
        VARIANT var;
        
        var.vt = VT_UI1;
        var.bVal = backendType;
        pInParams->Put(L"BackendType", 0, &var, 0);
        
        var.vt = VT_UI8;
        var.ullVal = sizeBytes;
        pInParams->Put(L"SizeBytes", 0, &var, 0);
        
        var.vt = VT_UI4;
        var.ulVal = blockSize;
        pInParams->Put(L"BlockSize", 0, &var, 0);
        
        var.vt = VT_BSTR;
        var.bstrVal = SysAllocString(backendPath);
        pInParams->Put(L"BackendPath", 0, &var, 0);
        SysFreeString(var.bstrVal);
        
        // 执行方法
        hr = pServices->ExecMethod(
            bstr_t("VNvmeManagement.InstanceName='vnvme'"),
            bstr_t("CreateLun"),
            0, NULL, pInParams, &pOutParams, NULL);
        
        if (SUCCEEDED(hr) && pOutParams) {
            // 获取输出参数
            if (SUCCEEDED(pOutParams->Get(L"LunId", 0, &var, 0, 0))) {
                *pLunId = var.bVal;
            }
            pOutParams->Release();
        }
        
        pInParams->Release();
        pClass->Release();
        
        return hr;
    }
};

//
// 使用示例
//
int main()
{
    VNvmeWmiClient client;
    
    HRESULT hr = client.Connect();
    if (FAILED(hr)) {
        printf("Failed to connect to WMI: 0x%08X\n", hr);
        return 1;
    }
    
    printf("=== Adapter Info ===\n");
    client.GetAdapterInfo();
    
    printf("\n=== Create LUN ===\n");
    BYTE lunId;
    hr = client.CreateLun(
        0,                              // Memory backend
        1ULL * 1024 * 1024 * 1024,      // 1 GB
        512,                            // 512 byte blocks
        L"",                            // No path for memory
        &lunId);
    
    if (SUCCEEDED(hr)) {
        printf("Created LUN %d\n", lunId);
    } else {
        printf("Failed to create LUN: 0x%08X\n", hr);
    }
    
    return 0;
}
```

---

## 方法三：PowerShell 管理脚本

利用 WMI 接口，可以直接使用 PowerShell 管理驱动：

```powershell
# VNvme-Management.ps1 - PowerShell 管理脚本

#
# 获取适配器信息
#
function Get-VNvmeAdapter {
    Get-WmiObject -Namespace "ROOT\WMI" -Class "VNvmeAdapterInfo" |
        Select-Object InstanceName, DriverVersion, MaxLuns, CurrentLunCount, QueueDepth
}

#
# 获取所有 LUN 信息
#
function Get-VNvmeLun {
    param(
        [Parameter(Mandatory=$false)]
        [byte]$LunId
    )
    
    $query = "SELECT * FROM VNvmeLunInfo"
    if ($PSBoundParameters.ContainsKey('LunId')) {
        $query += " WHERE LunId = $LunId"
    }
    
    Get-WmiObject -Namespace "ROOT\WMI" -Query $query |
        Select-Object LunId, BackendType, State, TotalSizeBytes, BlockSize, SerialNumber
}

#
# 获取统计信息
#
function Get-VNvmeStatistics {
    Get-WmiObject -Namespace "ROOT\WMI" -Class "VNvmeStatistics" |
        Select-Object ReadOperations, WriteOperations, BytesRead, BytesWritten,
                      AvgReadLatencyUs, AvgWriteLatencyUs
}

#
# 创建新 LUN
#
function New-VNvmeLun {
    param(
        [Parameter(Mandatory=$true)]
        [ValidateSet('Memory', 'File', 'VHD', 'Remote')]
        [string]$BackendType,
        
        [Parameter(Mandatory=$true)]
        [uint64]$SizeBytes,
        
        [Parameter(Mandatory=$false)]
        [ValidateSet(512, 4096)]
        [uint32]$BlockSize = 512,
        
        [Parameter(Mandatory=$false)]
        [string]$BackendPath = ""
    )
    
    $backendTypeMap = @{
        'Memory' = 0
        'File' = 1
        'VHD' = 2
        'Remote' = 3
    }
    
    $wmi = Get-WmiObject -Namespace "ROOT\WMI" -Class "VNvmeManagement" | 
           Select-Object -First 1
    
    if ($null -eq $wmi) {
        throw "VNvme adapter not found"
    }
    
    $result = $wmi.CreateLun(
        $backendTypeMap[$BackendType],
        $SizeBytes,
        $BlockSize,
        $BackendPath)
    
    if ($result.Status -eq 0) {
        Write-Host "Created LUN $($result.LunId) successfully"
        return $result.LunId
    } else {
        throw "Failed to create LUN: Status = $($result.Status)"
    }
}

#
# 删除 LUN
#
function Remove-VNvmeLun {
    param(
        [Parameter(Mandatory=$true)]
        [byte]$LunId
    )
    
    $wmi = Get-WmiObject -Namespace "ROOT\WMI" -Class "VNvmeManagement" |
           Select-Object -First 1
    
    if ($null -eq $wmi) {
        throw "VNvme adapter not found"
    }
    
    $result = $wmi.DeleteLun($LunId)
    
    if ($result.Status -eq 0) {
        Write-Host "Deleted LUN $LunId successfully"
    } else {
        throw "Failed to delete LUN: Status = $($result.Status)"
    }
}

#
# 重置统计
#
function Reset-VNvmeStatistics {
    $wmi = Get-WmiObject -Namespace "ROOT\WMI" -Class "VNvmeManagement" |
           Select-Object -First 1
    
    if ($null -eq $wmi) {
        throw "VNvme adapter not found"
    }
    
    $result = $wmi.ResetStatistics()
    
    if ($result.Status -eq 0) {
        Write-Host "Statistics reset successfully"
    } else {
        throw "Failed to reset statistics: Status = $($result.Status)"
    }
}

#
# 示例使用
#
<#
# 查看适配器信息
Get-VNvmeAdapter

# 查看所有 LUN
Get-VNvmeLun

# 查看特定 LUN
Get-VNvmeLun -LunId 0

# 创建 1GB 内存磁盘
New-VNvmeLun -BackendType Memory -SizeBytes 1GB

# 创建 10GB 文件磁盘
New-VNvmeLun -BackendType File -SizeBytes 10GB -BackendPath "C:\VDisks\disk1.vhd"

# 查看统计
Get-VNvmeStatistics

# 删除 LUN
Remove-VNvmeLun -LunId 1

# 重置统计
Reset-VNvmeStatistics
#>
```

---

## 错误码定义

### SCSI 状态码

| 状态 | 值 | 说明 |
|------|-----|------|
| GOOD | 0x00 | 命令成功 |
| CHECK_CONDITION | 0x02 | 检查条件 (查看 Sense Data) |
| BUSY | 0x08 | 设备忙 |
| RESERVATION_CONFLICT | 0x18 | 预留冲突 |

### VNvme 特定错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| VNVME_STATUS_SUCCESS | 0 | 成功 |
| VNVME_STATUS_INVALID_PARAM | 1 | 无效参数 |
| VNVME_STATUS_LUN_NOT_FOUND | 2 | LUN 不存在 |
| VNVME_STATUS_LUN_EXISTS | 3 | LUN 已存在 |
| VNVME_STATUS_NO_RESOURCE | 4 | 资源不足 |
| VNVME_STATUS_BACKEND_ERROR | 5 | 后端错误 |
| VNVME_STATUS_NOT_SUPPORTED | 6 | 不支持的操作 |
| VNVME_STATUS_ACCESS_DENIED | 7 | 访问被拒绝 |

### Sense Data 格式

```c
// 固定格式 Sense Data (18 字节)
typedef struct _VNVME_SENSE_DATA {
    UCHAR ErrorCode;            // 0x70 (当前错误) 或 0x71 (延迟错误)
    UCHAR SegmentNumber;        // 保留
    UCHAR SenseKey;             // 感知键 (0-15)
    UCHAR Information[4];       // 信息字段
    UCHAR AdditionalLength;     // 附加长度 (10)
    UCHAR CmdSpecificInfo[4];   // 命令特定信息
    UCHAR ASC;                  // 附加感知码
    UCHAR ASCQ;                 // 附加感知码限定符
    UCHAR FRU;                  // 字段可更换单元代码
    UCHAR SenseKeySpecific[3];  // 感知键特定
} VNVME_SENSE_DATA, *PVNVME_SENSE_DATA;

// 常用 Sense Key
#define SENSE_NO_SENSE          0x00
#define SENSE_RECOVERED_ERROR   0x01
#define SENSE_NOT_READY         0x02
#define SENSE_MEDIUM_ERROR      0x03
#define SENSE_HARDWARE_ERROR    0x04
#define SENSE_ILLEGAL_REQUEST   0x05
#define SENSE_UNIT_ATTENTION    0x06
#define SENSE_DATA_PROTECT      0x07
#define SENSE_ABORTED_COMMAND   0x0B
```

---

## 安全考虑

### 访问控制

1. **管理操作权限**：创建/删除 LUN 需要管理员权限
2. **WMI 安全**：通过 WMI 安全描述符限制访问
3. **SCSI 命令过滤**：阻止未授权的厂商命令

### 输入验证

```c
UCHAR
VNvmeValidateCreateLunParams(
    _In_ PVNVME_CREATE_LUN_PARAMS pParams)
{
    // 检查结构大小
    if (pParams->StructureSize < sizeof(VNVME_CREATE_LUN_PARAMS)) {
        return VNVME_STATUS_INVALID_PARAM;
    }
    
    // 检查后端类型
    if (pParams->BackendType >= VNVME_BACKEND_MAX) {
        return VNVME_STATUS_INVALID_PARAM;
    }
    
    // 检查磁盘大小 (最小 1MB，最大 64TB)
    if (pParams->SizeBytes < (1024 * 1024) ||
        pParams->SizeBytes > (64ULL * 1024 * 1024 * 1024 * 1024)) {
        return VNVME_STATUS_INVALID_PARAM;
    }
    
    // 检查块大小
    if (pParams->BlockSize != 512 && pParams->BlockSize != 4096) {
        return VNVME_STATUS_INVALID_PARAM;
    }
    
    // 文件后端必须提供路径
    if (pParams->BackendType == VNVME_BACKEND_FILE ||
        pParams->BackendType == VNVME_BACKEND_VHD) {
        if (pParams->BackendPath[0] == L'\0') {
            return VNVME_STATUS_INVALID_PARAM;
        }
    }
    
    return VNVME_STATUS_SUCCESS;
}
```

---

## 参考资料

- [SCSI Pass-Through Interface](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/handling-scsi-pass-through-requests)
- [StorPort WMI Support](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/handling-wmi-srbs-in-storage-miniport-drivers)
- [WMI Data Provider](https://docs.microsoft.com/en-us/windows/win32/wmisdk/wmi-providers)
- [MOF Syntax](https://docs.microsoft.com/en-us/windows/win32/wmisdk/mof-data-types)
