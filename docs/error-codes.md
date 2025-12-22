# 错误码参考

本文档提供 Virtual NVMe StorPort Miniport 驱动使用的错误码定义，包括 SCSI 状态码、SRB 状态码和驱动特定错误码。

## 概述

在 StorPort Miniport 架构中，错误通过以下方式报告：

1. **SRB 状态码 (SRB_STATUS_xxx)**：StorPort 层通用状态
2. **SCSI 状态码**：标准 SCSI 协议状态
3. **Sense Data**：详细错误信息
4. **驱动特定错误码**：管理接口返回的错误

---

## SRB 状态码

### SRB_STATUS 定义

SRB 状态码是 StorPort Miniport 返回请求完成状态的主要方式。

```c
// SRB 状态码定义
#define SRB_STATUS_PENDING                  0x00
#define SRB_STATUS_SUCCESS                  0x01
#define SRB_STATUS_ABORTED                  0x02
#define SRB_STATUS_ABORT_FAILED             0x03
#define SRB_STATUS_ERROR                    0x04
#define SRB_STATUS_BUSY                     0x05
#define SRB_STATUS_INVALID_REQUEST          0x06
#define SRB_STATUS_INVALID_PATH_ID          0x07
#define SRB_STATUS_NO_DEVICE                0x08
#define SRB_STATUS_TIMEOUT                  0x09
#define SRB_STATUS_SELECTION_TIMEOUT        0x0A
#define SRB_STATUS_COMMAND_TIMEOUT          0x0B
#define SRB_STATUS_MESSAGE_REJECTED         0x0D
#define SRB_STATUS_BUS_RESET                0x0E
#define SRB_STATUS_PARITY_ERROR             0x0F
#define SRB_STATUS_REQUEST_SENSE_FAILED     0x10
#define SRB_STATUS_NO_HBA                   0x11
#define SRB_STATUS_DATA_OVERRUN             0x12
#define SRB_STATUS_UNEXPECTED_BUS_FREE      0x13
#define SRB_STATUS_PHASE_SEQUENCE_FAILURE   0x14
#define SRB_STATUS_BAD_SRB_BLOCK_LENGTH     0x15
#define SRB_STATUS_REQUEST_FLUSHED          0x16
#define SRB_STATUS_INVALID_LUN              0x20
#define SRB_STATUS_INVALID_TARGET_ID        0x21
#define SRB_STATUS_BAD_FUNCTION             0x22
#define SRB_STATUS_ERROR_RECOVERY           0x23
#define SRB_STATUS_NOT_POWERED              0x24
#define SRB_STATUS_LINK_DOWN                0x25
#define SRB_STATUS_INTERNAL_ERROR           0x30

// 状态修饰符
#define SRB_STATUS_QUEUE_FROZEN             0x40
#define SRB_STATUS_AUTOSENSE_VALID          0x80
```

### 常用状态码说明

| 状态码 | 值 | 说明 | 使用场景 |
|--------|-----|------|----------|
| SRB_STATUS_SUCCESS | 0x01 | 命令成功完成 | 正常完成 |
| SRB_STATUS_ERROR | 0x04 | 通用错误 | 需配合 Sense Data |
| SRB_STATUS_BUSY | 0x05 | 设备忙 | 暂时无法处理 |
| SRB_STATUS_INVALID_REQUEST | 0x06 | 无效请求 | 不支持的命令 |
| SRB_STATUS_NO_DEVICE | 0x08 | 设备不存在 | LUN 未配置 |
| SRB_STATUS_DATA_OVERRUN | 0x12 | 数据溢出 | 缓冲区太小 |
| SRB_STATUS_INVALID_LUN | 0x20 | 无效 LUN | LUN 超出范围 |

### 状态码使用示例

```c
//
// 设置成功状态
//
VOID
VNvmeCompleteSuccess(
    _Inout_ PSCSI_REQUEST_BLOCK pSrb)
{
    pSrb->SrbStatus = SRB_STATUS_SUCCESS;
    pSrb->ScsiStatus = SCSISTAT_GOOD;
}

//
// 设置错误状态 (带 Sense Data)
//
VOID
VNvmeCompleteError(
    _Inout_ PSCSI_REQUEST_BLOCK pSrb,
    _In_ UCHAR SenseKey,
    _In_ UCHAR Asc,
    _In_ UCHAR Ascq)
{
    pSrb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    pSrb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    
    // 填充 Sense Data
    if (pSrb->SenseInfoBuffer && pSrb->SenseInfoBufferLength >= 18) {
        PSENSE_DATA pSense = (PSENSE_DATA)pSrb->SenseInfoBuffer;
        RtlZeroMemory(pSense, pSrb->SenseInfoBufferLength);
        
        pSense->ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
        pSense->SenseKey = SenseKey;
        pSense->AdditionalSenseLength = 10;
        pSense->AdditionalSenseCode = Asc;
        pSense->AdditionalSenseCodeQualifier = Ascq;
        
        pSrb->SenseInfoBufferLength = 18;
    }
}

//
// 设置设备不存在状态
//
VOID
VNvmeCompleteNoDevice(
    _Inout_ PSCSI_REQUEST_BLOCK pSrb)
{
    pSrb->SrbStatus = SRB_STATUS_NO_DEVICE;
    pSrb->ScsiStatus = SCSISTAT_GOOD;
    pSrb->DataTransferLength = 0;
}
```

---

## SCSI 状态码

### SCSI 状态字节

```c
// SCSI 状态定义
#define SCSISTAT_GOOD                   0x00
#define SCSISTAT_CHECK_CONDITION        0x02
#define SCSISTAT_CONDITION_MET          0x04
#define SCSISTAT_BUSY                   0x08
#define SCSISTAT_INTERMEDIATE           0x10
#define SCSISTAT_INTERMEDIATE_COND_MET  0x14
#define SCSISTAT_RESERVATION_CONFLICT   0x18
#define SCSISTAT_COMMAND_TERMINATED     0x22
#define SCSISTAT_QUEUE_FULL             0x28
#define SCSISTAT_ACA_ACTIVE             0x30
#define SCSISTAT_TASK_ABORTED           0x40
```

### 状态码说明

| 状态 | 值 | 说明 |
|------|-----|------|
| GOOD | 0x00 | 命令成功完成 |
| CHECK_CONDITION | 0x02 | 检查条件，查看 Sense Data |
| BUSY | 0x08 | 设备忙，稍后重试 |
| RESERVATION_CONFLICT | 0x18 | 预留冲突 |
| QUEUE_FULL | 0x28 | 队列满 |

---

## Sense Data

### Sense Data 格式

```c
//
// 固定格式 Sense Data (18 字节最小)
//
#pragma pack(push, 1)
typedef struct _SENSE_DATA {
    UCHAR ErrorCode:7;          // 错误码 (0x70 或 0x71)
    UCHAR Valid:1;              // 信息字段有效
    
    UCHAR SegmentNumber;        // 段号 (通常为 0)
    
    UCHAR SenseKey:4;           // 感知键
    UCHAR Reserved1:1;
    UCHAR IncorrectLength:1;    // ILI 位
    UCHAR EndOfMedia:1;         // EOM 位
    UCHAR FileMark:1;           // Filemark 位
    
    UCHAR Information[4];       // 信息字段
    UCHAR AdditionalSenseLength;// 附加感知长度
    UCHAR CommandSpecific[4];   // 命令特定信息
    UCHAR AdditionalSenseCode;  // ASC
    UCHAR AdditionalSenseCodeQualifier; // ASCQ
    UCHAR FieldReplaceableUnitCode; // FRU
    UCHAR SenseKeySpecific[3];  // 感知键特定
    
} SENSE_DATA, *PSENSE_DATA;
#pragma pack(pop)

// 错误码定义
#define SCSI_SENSE_ERRORCODE_FIXED_CURRENT  0x70
#define SCSI_SENSE_ERRORCODE_FIXED_DEFERRED 0x71
#define SCSI_SENSE_ERRORCODE_DESCRIPTOR_CURRENT 0x72
#define SCSI_SENSE_ERRORCODE_DESCRIPTOR_DEFERRED 0x73
```

### Sense Key 定义

| Sense Key | 值 | 名称 | 说明 |
|-----------|-----|------|------|
| NO_SENSE | 0x0 | No Sense | 无错误或已恢复 |
| RECOVERED_ERROR | 0x1 | Recovered Error | 已恢复的错误 |
| NOT_READY | 0x2 | Not Ready | 设备未就绪 |
| MEDIUM_ERROR | 0x3 | Medium Error | 介质错误 |
| HARDWARE_ERROR | 0x4 | Hardware Error | 硬件错误 |
| ILLEGAL_REQUEST | 0x5 | Illegal Request | 非法请求 |
| UNIT_ATTENTION | 0x6 | Unit Attention | 单元注意 |
| DATA_PROTECT | 0x7 | Data Protect | 数据保护 |
| BLANK_CHECK | 0x8 | Blank Check | 空白检查 |
| VENDOR_SPECIFIC | 0x9 | Vendor Specific | 厂商特定 |
| COPY_ABORTED | 0xA | Copy Aborted | 复制中止 |
| ABORTED_COMMAND | 0xB | Aborted Command | 命令中止 |
| EQUAL | 0xC | Equal | 相等 (比较命令) |
| VOLUME_OVERFLOW | 0xD | Volume Overflow | 卷溢出 |
| MISCOMPARE | 0xE | Miscompare | 不匹配 |
| COMPLETED | 0xF | Completed | 完成 |

### 常用 ASC/ASCQ 组合

| Sense Key | ASC | ASCQ | 说明 | 宏定义 |
|-----------|-----|------|------|--------|
| NOT_READY | 0x04 | 0x00 | 原因未报告 | `SCSI_ADSENSE_LUN_NOT_READY` |
| NOT_READY | 0x04 | 0x01 | 正在变为就绪 | |
| NOT_READY | 0x04 | 0x02 | 需要初始化命令 | |
| NOT_READY | 0x04 | 0x03 | 需要手动干预 | |
| MEDIUM_ERROR | 0x11 | 0x00 | 未恢复的读错误 | `SCSI_ADSENSE_UNRECOVER_ERROR` |
| MEDIUM_ERROR | 0x03 | 0x00 | 写错误 | |
| ILLEGAL_REQUEST | 0x20 | 0x00 | 无效命令操作码 | `SCSI_ADSENSE_INVALID_COMMAND` |
| ILLEGAL_REQUEST | 0x24 | 0x00 | CDB 中字段无效 | `SCSI_ADSENSE_INVALID_CDB` |
| ILLEGAL_REQUEST | 0x25 | 0x00 | LUN 不支持 | `SCSI_ADSENSE_INVALID_LUN` |
| ILLEGAL_REQUEST | 0x26 | 0x00 | 参数列表中字段无效 | `SCSI_ADSENSE_INVALID_PARAMETER` |
| ILLEGAL_REQUEST | 0x21 | 0x00 | LBA 超出范围 | `SCSI_ADSENSE_LBA_OUT_OF_RANGE` |
| UNIT_ATTENTION | 0x28 | 0x00 | 介质已更改 | `SCSI_ADSENSE_MEDIUM_CHANGED` |
| UNIT_ATTENTION | 0x29 | 0x00 | 电源重置或总线重置 | `SCSI_ADSENSE_POWER_ON` |
| DATA_PROTECT | 0x27 | 0x00 | 写保护 | `SCSI_ADSENSE_WRITE_PROTECT` |

### Sense Data 生成函数

```c
//
// 预定义 Sense Data 结构
//
typedef struct _VNVME_SENSE_INFO {
    UCHAR SenseKey;
    UCHAR Asc;
    UCHAR Ascq;
    const char* Description;
} VNVME_SENSE_INFO, *PVNVME_SENSE_INFO;

// 常用错误预定义
static const VNVME_SENSE_INFO VNvmeSenseTable[] = {
    // 非法请求
    { SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00, "Invalid command operation code" },
    { SCSI_SENSE_ILLEGAL_REQUEST, 0x24, 0x00, "Invalid field in CDB" },
    { SCSI_SENSE_ILLEGAL_REQUEST, 0x25, 0x00, "Logical unit not supported" },
    { SCSI_SENSE_ILLEGAL_REQUEST, 0x26, 0x00, "Invalid field in parameter list" },
    { SCSI_SENSE_ILLEGAL_REQUEST, 0x21, 0x00, "LBA out of range" },
    
    // 未就绪
    { SCSI_SENSE_NOT_READY, 0x04, 0x00, "Logical unit not ready" },
    { SCSI_SENSE_NOT_READY, 0x04, 0x01, "Becoming ready" },
    { SCSI_SENSE_NOT_READY, 0x04, 0x03, "Manual intervention required" },
    
    // 介质错误
    { SCSI_SENSE_MEDIUM_ERROR, 0x11, 0x00, "Unrecovered read error" },
    { SCSI_SENSE_MEDIUM_ERROR, 0x03, 0x00, "Write error" },
    
    // 硬件错误
    { SCSI_SENSE_HARDWARE_ERROR, 0x00, 0x00, "No additional sense information" },
    
    // 数据保护
    { SCSI_SENSE_DATA_PROTECT, 0x27, 0x00, "Write protected" },
    
    // 单元注意
    { SCSI_SENSE_UNIT_ATTENTION, 0x28, 0x00, "Medium may have changed" },
    { SCSI_SENSE_UNIT_ATTENTION, 0x29, 0x00, "Power on or reset" },
};

//
// 构建 Sense Data
//
VOID
VNvmeBuildSenseData(
    _Out_writes_bytes_(SenseBufferLength) PVOID SenseBuffer,
    _In_ ULONG SenseBufferLength,
    _In_ UCHAR SenseKey,
    _In_ UCHAR Asc,
    _In_ UCHAR Ascq,
    _In_opt_ ULONGLONG Information)
{
    PSENSE_DATA pSense = (PSENSE_DATA)SenseBuffer;
    
    if (SenseBufferLength < sizeof(SENSE_DATA)) {
        return;
    }
    
    RtlZeroMemory(pSense, SenseBufferLength);
    
    pSense->ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
    pSense->Valid = (Information != 0) ? 1 : 0;
    pSense->SenseKey = SenseKey;
    pSense->AdditionalSenseLength = 10;
    pSense->AdditionalSenseCode = Asc;
    pSense->AdditionalSenseCodeQualifier = Ascq;
    
    // 填充信息字段 (大端)
    if (pSense->Valid) {
        pSense->Information[0] = (UCHAR)(Information >> 24);
        pSense->Information[1] = (UCHAR)(Information >> 16);
        pSense->Information[2] = (UCHAR)(Information >> 8);
        pSense->Information[3] = (UCHAR)(Information);
    }
}

//
// 辅助函数: 设置 LBA 超范围错误
//
VOID
VNvmeSetLbaOutOfRangeError(
    _Inout_ PSCSI_REQUEST_BLOCK pSrb,
    _In_ ULONGLONG BadLba)
{
    VNvmeBuildSenseData(
        pSrb->SenseInfoBuffer,
        pSrb->SenseInfoBufferLength,
        SCSI_SENSE_ILLEGAL_REQUEST,
        SCSI_ADSENSE_ILLEGAL_BLOCK,
        0x00,
        BadLba);
    
    pSrb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    pSrb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    pSrb->SenseInfoBufferLength = 18;
}

//
// 辅助函数: 设置写保护错误
//
VOID
VNvmeSetWriteProtectError(
    _Inout_ PSCSI_REQUEST_BLOCK pSrb)
{
    VNvmeBuildSenseData(
        pSrb->SenseInfoBuffer,
        pSrb->SenseInfoBufferLength,
        SCSI_SENSE_DATA_PROTECT,
        SCSI_ADSENSE_WRITE_PROTECT,
        0x00,
        0);
    
    pSrb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    pSrb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    pSrb->SenseInfoBufferLength = 18;
}

//
// 辅助函数: 设置无效 CDB 错误
//
VOID
VNvmeSetInvalidCdbError(
    _Inout_ PSCSI_REQUEST_BLOCK pSrb,
    _In_ UCHAR FieldOffset)
{
    PSENSE_DATA pSense = (PSENSE_DATA)pSrb->SenseInfoBuffer;
    
    VNvmeBuildSenseData(
        pSrb->SenseInfoBuffer,
        pSrb->SenseInfoBufferLength,
        SCSI_SENSE_ILLEGAL_REQUEST,
        SCSI_ADSENSE_INVALID_CDB,
        0x00,
        0);
    
    // 设置 Sense Key Specific 字段指示错误位置
    if (pSrb->SenseInfoBufferLength >= 18) {
        pSense->SenseKeySpecific[0] = 0xC0;  // SKSV=1, C/D=1, BPV=0
        pSense->SenseKeySpecific[1] = 0;
        pSense->SenseKeySpecific[2] = FieldOffset;
    }
    
    pSrb->SrbStatus = SRB_STATUS_ERROR | SRB_STATUS_AUTOSENSE_VALID;
    pSrb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
    pSrb->SenseInfoBufferLength = 18;
}
```

---

## 驱动特定错误码

### VNVME 错误码定义

```c
//
// VNvme 管理接口错误码
//
typedef enum _VNVME_ERROR_CODE {
    VNVME_SUCCESS = 0,              // 成功
    
    // 参数错误 (1-99)
    VNVME_ERROR_INVALID_PARAMETER = 1,
    VNVME_ERROR_INVALID_SIZE = 2,
    VNVME_ERROR_INVALID_BLOCK_SIZE = 3,
    VNVME_ERROR_INVALID_LUN_ID = 4,
    VNVME_ERROR_INVALID_BACKEND_TYPE = 5,
    VNVME_ERROR_INVALID_PATH = 6,
    
    // 资源错误 (100-199)
    VNVME_ERROR_OUT_OF_MEMORY = 100,
    VNVME_ERROR_MAX_LUNS_REACHED = 101,
    VNVME_ERROR_RESOURCE_BUSY = 102,
    
    // 状态错误 (200-299)
    VNVME_ERROR_LUN_NOT_FOUND = 200,
    VNVME_ERROR_LUN_ALREADY_EXISTS = 201,
    VNVME_ERROR_LUN_OFFLINE = 202,
    VNVME_ERROR_ADAPTER_NOT_READY = 203,
    
    // 后端错误 (300-399)
    VNVME_ERROR_BACKEND_INIT_FAILED = 300,
    VNVME_ERROR_BACKEND_IO_FAILED = 301,
    VNVME_ERROR_BACKEND_NOT_FOUND = 302,
    VNVME_ERROR_FILE_OPEN_FAILED = 303,
    VNVME_ERROR_FILE_CREATE_FAILED = 304,
    VNVME_ERROR_NETWORK_ERROR = 305,
    
    // 权限错误 (400-499)
    VNVME_ERROR_ACCESS_DENIED = 400,
    VNVME_ERROR_READ_ONLY = 401,
    
    // 操作错误 (500-599)
    VNVME_ERROR_NOT_SUPPORTED = 500,
    VNVME_ERROR_OPERATION_TIMEOUT = 501,
    VNVME_ERROR_OPERATION_ABORTED = 502,
    
    // 内部错误 (900-999)
    VNVME_ERROR_INTERNAL = 900,
    VNVME_ERROR_UNKNOWN = 999
    
} VNVME_ERROR_CODE;

//
// 错误码转字符串
//
PCSTR
VNvmeErrorToString(VNVME_ERROR_CODE Error)
{
    switch (Error) {
        case VNVME_SUCCESS: return "Success";
        case VNVME_ERROR_INVALID_PARAMETER: return "Invalid parameter";
        case VNVME_ERROR_INVALID_SIZE: return "Invalid size";
        case VNVME_ERROR_INVALID_BLOCK_SIZE: return "Invalid block size";
        case VNVME_ERROR_INVALID_LUN_ID: return "Invalid LUN ID";
        case VNVME_ERROR_INVALID_BACKEND_TYPE: return "Invalid backend type";
        case VNVME_ERROR_INVALID_PATH: return "Invalid path";
        case VNVME_ERROR_OUT_OF_MEMORY: return "Out of memory";
        case VNVME_ERROR_MAX_LUNS_REACHED: return "Maximum LUNs reached";
        case VNVME_ERROR_RESOURCE_BUSY: return "Resource busy";
        case VNVME_ERROR_LUN_NOT_FOUND: return "LUN not found";
        case VNVME_ERROR_LUN_ALREADY_EXISTS: return "LUN already exists";
        case VNVME_ERROR_LUN_OFFLINE: return "LUN offline";
        case VNVME_ERROR_ADAPTER_NOT_READY: return "Adapter not ready";
        case VNVME_ERROR_BACKEND_INIT_FAILED: return "Backend initialization failed";
        case VNVME_ERROR_BACKEND_IO_FAILED: return "Backend I/O failed";
        case VNVME_ERROR_BACKEND_NOT_FOUND: return "Backend not found";
        case VNVME_ERROR_FILE_OPEN_FAILED: return "File open failed";
        case VNVME_ERROR_FILE_CREATE_FAILED: return "File create failed";
        case VNVME_ERROR_NETWORK_ERROR: return "Network error";
        case VNVME_ERROR_ACCESS_DENIED: return "Access denied";
        case VNVME_ERROR_READ_ONLY: return "Read only";
        case VNVME_ERROR_NOT_SUPPORTED: return "Not supported";
        case VNVME_ERROR_OPERATION_TIMEOUT: return "Operation timeout";
        case VNVME_ERROR_OPERATION_ABORTED: return "Operation aborted";
        case VNVME_ERROR_INTERNAL: return "Internal error";
        default: return "Unknown error";
    }
}
```

---

## NTSTATUS 到 SRB 状态映射

```c
//
// NTSTATUS 到 SRB 状态映射
//
UCHAR
VNvmeNtStatusToSrbStatus(NTSTATUS Status)
{
    switch (Status) {
        case STATUS_SUCCESS:
            return SRB_STATUS_SUCCESS;
            
        case STATUS_INVALID_PARAMETER:
        case STATUS_INVALID_DEVICE_REQUEST:
            return SRB_STATUS_INVALID_REQUEST;
            
        case STATUS_DEVICE_NOT_READY:
        case STATUS_DEVICE_BUSY:
            return SRB_STATUS_BUSY;
            
        case STATUS_NO_SUCH_DEVICE:
        case STATUS_DEVICE_DOES_NOT_EXIST:
            return SRB_STATUS_NO_DEVICE;
            
        case STATUS_IO_TIMEOUT:
            return SRB_STATUS_TIMEOUT;
            
        case STATUS_BUFFER_TOO_SMALL:
        case STATUS_BUFFER_OVERFLOW:
            return SRB_STATUS_DATA_OVERRUN;
            
        case STATUS_CANCELLED:
            return SRB_STATUS_ABORTED;
            
        default:
            return SRB_STATUS_ERROR;
    }
}

//
// NTSTATUS 到 Sense Data 映射
//
VOID
VNvmeNtStatusToSenseData(
    _In_ NTSTATUS Status,
    _Out_ PUCHAR SenseKey,
    _Out_ PUCHAR Asc,
    _Out_ PUCHAR Ascq)
{
    switch (Status) {
        case STATUS_SUCCESS:
            *SenseKey = SCSI_SENSE_NO_SENSE;
            *Asc = 0x00;
            *Ascq = 0x00;
            break;
            
        case STATUS_INVALID_PARAMETER:
            *SenseKey = SCSI_SENSE_ILLEGAL_REQUEST;
            *Asc = SCSI_ADSENSE_INVALID_CDB;
            *Ascq = 0x00;
            break;
            
        case STATUS_DEVICE_NOT_READY:
            *SenseKey = SCSI_SENSE_NOT_READY;
            *Asc = SCSI_ADSENSE_LUN_NOT_READY;
            *Ascq = 0x00;
            break;
            
        case STATUS_MEDIA_WRITE_PROTECTED:
            *SenseKey = SCSI_SENSE_DATA_PROTECT;
            *Asc = SCSI_ADSENSE_WRITE_PROTECT;
            *Ascq = 0x00;
            break;
            
        case STATUS_DISK_FULL:
            *SenseKey = SCSI_SENSE_MEDIUM_ERROR;
            *Asc = 0x03;  // Write fault
            *Ascq = 0x00;
            break;
            
        case STATUS_IO_DEVICE_ERROR:
            *SenseKey = SCSI_SENSE_HARDWARE_ERROR;
            *Asc = 0x00;
            *Ascq = 0x00;
            break;
            
        default:
            *SenseKey = SCSI_SENSE_ABORTED_COMMAND;
            *Asc = 0x00;
            *Ascq = 0x00;
            break;
    }
}
```

---

## 错误日志记录

### StorPort 错误日志

```c
//
// 记录错误到 Windows 事件日志
//
VOID
VNvmeLogError(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _In_ ULONG ErrorCode,
    _In_ ULONG UniqueId,
    _In_ ULONG Param1,
    _In_ ULONG Param2)
{
    // 使用 StorPort 日志 API
    StorPortLogError(
        pAdapter,               // DeviceExtension
        NULL,                   // Srb (可选)
        0,                      // PathId
        0,                      // TargetId
        0,                      // Lun
        ErrorCode,              // 错误码
        UniqueId                // 唯一标识
    );
    
    // 也可以使用 ETW 跟踪
    VNvmeTraceError(pAdapter,
        "Error: Code=0x%08X, UniqueId=%u, Param1=%u, Param2=%u",
        ErrorCode, UniqueId, Param1, Param2);
}

//
// 预定义错误码 (用于事件日志)
//
#define VNVME_ERR_BACKEND_INIT_FAILED    0x00010001
#define VNVME_ERR_BACKEND_IO_FAILED      0x00010002
#define VNVME_ERR_MEMORY_ALLOC_FAILED    0x00010003
#define VNVME_ERR_INVALID_REQUEST        0x00010004
#define VNVME_ERR_LUN_CONFIG_FAILED      0x00010005
```

---

## 参考资料

- [SCSI Reference](https://www.t10.org/drafts.htm)
- [StorPort SRB_STATUS Values](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/srb-status-values)
- [SCSI Sense Data](https://en.wikipedia.org/wiki/SCSI_Sense_Data)
- [SCSI Primary Commands (SPC)](https://www.t10.org/cgi-bin/ac.pl?t=f&f=spc5r25.pdf)
