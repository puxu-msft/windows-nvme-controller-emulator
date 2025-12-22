# 命令处理流程

本文档描述 Virtual NVMe StorPort Miniport 驱动的 SCSI 命令处理流程。

## 概述

作为 StorPort Miniport 驱动，我们处理的是 SCSI 命令（通过 SRB 传递），而不是原生 NVMe 命令。Windows 存储栈会将上层请求转换为标准 SCSI 命令。

```
┌─────────────────────────────────────────────────────────────────┐
│                      命令处理流程                                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  StorPort.sys                                                   │
│      │                                                          │
│      │ 调用 HwStartIo(DeviceExtension, Srb)                     │
│      ▼                                                          │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  VNvmeHwStartIo()                                          │ │
│  │      │                                                     │ │
│  │      ├── SRB_FUNCTION_EXECUTE_SCSI ──► VNvmeScsiExecute()  │ │
│  │      ├── SRB_FUNCTION_IO_CONTROL ────► VNvmeIoctlProcess() │ │
│  │      ├── SRB_FUNCTION_RESET_* ───────► VNvmeReset()        │ │
│  │      ├── SRB_FUNCTION_FLUSH ─────────► VNvmeFlush()        │ │
│  │      └── SRB_FUNCTION_PNP ───────────► VNvmePnp()          │ │
│  └────────────────────────────────────────────────────────────┘ │
│      │                                                          │
│      │ StorPortNotification(RequestComplete, ...)               │
│      ▼                                                          │
│  请求完成                                                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## SRB 函数类型

| SRB Function | 说明 | 处理函数 |
|--------------|------|----------|
| `SRB_FUNCTION_EXECUTE_SCSI` | 执行 SCSI 命令 | `VNvmeScsiExecute()` |
| `SRB_FUNCTION_IO_CONTROL` | IOCTL 请求 | `VNvmeIoctlProcess()` |
| `SRB_FUNCTION_RESET_LOGICAL_UNIT` | 重置 LUN | `VNvmeLunReset()` |
| `SRB_FUNCTION_RESET_DEVICE` | 重置设备 | `VNvmeTargetReset()` |
| `SRB_FUNCTION_RESET_BUS` | 重置总线 | `VNvmeBusReset()` |
| `SRB_FUNCTION_FLUSH` | 刷新缓存 | `VNvmeFlush()` |
| `SRB_FUNCTION_SHUTDOWN` | 关机 | `VNvmeShutdown()` |
| `SRB_FUNCTION_PNP` | PnP 请求 | `VNvmePnp()` |
| `SRB_FUNCTION_POWER` | 电源请求 | `VNvmePower()` |
| `SRB_FUNCTION_WMI` | WMI 请求 | `VNvmeWmi()` |

## SCSI 命令处理

### 支持的 SCSI 命令

| 命令 | 操作码 | 必需 | 说明 |
|------|--------|------|------|
| `TEST UNIT READY` | 0x00 | ✓ | 检查设备就绪 |
| `INQUIRY` | 0x12 | ✓ | 设备识别 |
| `READ CAPACITY (10)` | 0x25 | ✓ | 读取容量 |
| `READ CAPACITY (16)` | 0x9E | ✓ | 读取大容量 |
| `READ (6/10/12/16)` | 0x08/28/A8/88 | ✓ | 读取数据 |
| `WRITE (6/10/12/16)` | 0x0A/2A/AA/8A | ✓ | 写入数据 |
| `MODE SENSE (6/10)` | 0x1A/5A | ✓ | 模式查询 |
| `MODE SELECT (6/10)` | 0x15/55 | ○ | 模式设置 |
| `SYNCHRONIZE CACHE (10/16)` | 0x35/91 | ✓ | 刷新缓存 |
| `START STOP UNIT` | 0x1B | ✓ | 启动/停止 |
| `UNMAP` | 0x42 | ○ | TRIM 支持 |
| `REPORT LUNS` | 0xA0 | ○ | 报告 LUN 列表 |
| `REQUEST SENSE` | 0x03 | ○ | 请求感知数据 |

### VNvmeScsiExecute 实现

```c
UCHAR VNvmeScsiExecute(
    _In_ PVNVME_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_LU_EXTENSION lu;
    PCDB cdb = (PCDB)Srb->Cdb;
    UCHAR opCode = cdb->CDB6GENERIC.OperationCode;
    UCHAR srbStatus;
    
    //
    // 查找目标 LUN
    //
    lu = VNvmeLunFind(AdapterExtension, 
        Srb->PathId, Srb->TargetId, Srb->Lun);
    
    //
    // 分发 SCSI 命令
    //
    switch (opCode) {
        
        //
        // 基础命令
        //
        case SCSIOP_TEST_UNIT_READY:
            srbStatus = VNvmeScsiTestUnitReady(lu, Srb);
            break;
            
        case SCSIOP_INQUIRY:
            srbStatus = VNvmeScsiInquiry(lu, Srb);
            break;
            
        //
        // 容量命令
        //
        case SCSIOP_READ_CAPACITY:
            srbStatus = VNvmeScsiReadCapacity(lu, Srb);
            break;
            
        case SCSIOP_READ_CAPACITY16:
            srbStatus = VNvmeScsiReadCapacity16(lu, Srb);
            break;
            
        //
        // 读取命令
        //
        case SCSIOP_READ6:
        case SCSIOP_READ:
        case SCSIOP_READ12:
        case SCSIOP_READ16:
            srbStatus = VNvmeScsiRead(lu, Srb);
            break;
            
        //
        // 写入命令
        //
        case SCSIOP_WRITE6:
        case SCSIOP_WRITE:
        case SCSIOP_WRITE12:
        case SCSIOP_WRITE16:
            srbStatus = VNvmeScsiWrite(lu, Srb);
            break;
            
        //
        // 模式命令
        //
        case SCSIOP_MODE_SENSE:
        case SCSIOP_MODE_SENSE10:
            srbStatus = VNvmeScsiModeSense(lu, Srb);
            break;
            
        //
        // 缓存命令
        //
        case SCSIOP_SYNCHRONIZE_CACHE:
        case SCSIOP_SYNCHRONIZE_CACHE16:
            srbStatus = VNvmeScsiSynchronizeCache(lu, Srb);
            break;
            
        //
        // TRIM 命令
        //
        case SCSIOP_UNMAP:
            srbStatus = VNvmeScsiUnmap(lu, Srb);
            break;
            
        //
        // 其他命令
        //
        case SCSIOP_START_STOP_UNIT:
            srbStatus = VNvmeScsiStartStopUnit(lu, Srb);
            break;
            
        case SCSIOP_REPORT_LUNS:
            srbStatus = VNvmeScsiReportLuns(AdapterExtension, Srb);
            break;
            
        default:
            srbStatus = SRB_STATUS_INVALID_REQUEST;
            break;
    }
    
    return srbStatus;
}
```

## 核心命令实现

### TEST UNIT READY

```c
UCHAR VNvmeScsiTestUnitReady(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    UNREFERENCED_PARAMETER(Srb);
    
    if (!LuExtension) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    switch (LuExtension->State) {
        case VNVME_LUN_STATE_READY:
            return SRB_STATUS_SUCCESS;
            
        case VNVME_LUN_STATE_INITIALIZING:
            VNvmeSetSenseData(Srb, 
                SCSI_SENSE_NOT_READY,
                SCSI_ADSENSE_LUN_NOT_READY,
                SCSI_SENSEQ_BECOMING_READY);
            return SRB_STATUS_ERROR;
            
        case VNVME_LUN_STATE_OFFLINE:
            VNvmeSetSenseData(Srb,
                SCSI_SENSE_NOT_READY,
                SCSI_ADSENSE_LUN_NOT_READY,
                SCSI_SENSEQ_MANUAL_INTERVENTION_REQUIRED);
            return SRB_STATUS_ERROR;
            
        default:
            return SRB_STATUS_NO_DEVICE;
    }
}
```

### INQUIRY

```c
UCHAR VNvmeScsiInquiry(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    PINQUIRYDATA data = Srb->DataBuffer;
    ULONG allocLength;
    
    // 获取分配长度
    if (cdb->CDB6INQUIRY.CommandUniqueBytes[0] == 0) {
        allocLength = cdb->CDB6INQUIRY.AllocationLength;
    } else {
        allocLength = (cdb->CDB6INQUIRY.AllocationLength << 8) |
                      cdb->CDB6INQUIRY.CommandUniqueBytes[0];
    }
    
    // 检查 VPD 请求
    if (cdb->CDB6INQUIRY3.EnableVitalProductData) {
        return VNvmeScsiVpdPage(LuExtension, Srb);
    }
    
    // LUN 不存在检查
    if (!LuExtension) {
        RtlZeroMemory(data, min(allocLength, sizeof(INQUIRYDATA)));
        data->DeviceType = DEVICE_QUALIFIER_NOT_SUPPORTED;
        data->DeviceTypeQualifier = DEVICE_QUALIFIER_NOT_SUPPORTED >> 5;
        Srb->DataTransferLength = sizeof(INQUIRYDATA);
        return SRB_STATUS_SUCCESS;
    }
    
    //
    // 填充标准 INQUIRY 数据
    //
    RtlZeroMemory(data, min(allocLength, sizeof(INQUIRYDATA)));
    
    // 设备类型: 直接访问块设备 (磁盘)
    data->DeviceType = DIRECT_ACCESS_DEVICE;
    data->DeviceTypeQualifier = DEVICE_CONNECTED;
    
    // 不可移除
    data->RemovableMedia = LuExtension->RemovableMedia;
    
    // 版本 (SPC-4)
    data->Versions = 0x06;
    
    // 响应格式 (SPC-2)
    data->ResponseDataFormat = 2;
    
    // 附加长度
    data->AdditionalLength = INQUIRYDATABUFFERSIZE - 5;
    
    // 功能标志
    data->CommandQueue = TRUE;      // 支持命令队列
    
    // 厂商 ID (8 字符, 空格填充)
    RtlCopyMemory(data->VendorId, LuExtension->VendorId, 8);
    
    // 产品 ID (16 字符)
    RtlCopyMemory(data->ProductId, LuExtension->ProductId, 16);
    
    // 产品版本 (4 字符)
    RtlCopyMemory(data->ProductRevisionLevel, "1.0 ", 4);
    
    Srb->DataTransferLength = min(allocLength, sizeof(INQUIRYDATA));
    return SRB_STATUS_SUCCESS;
}
```

### VPD 页处理

```c
UCHAR VNvmeScsiVpdPage(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    UCHAR pageCode = cdb->CDB6INQUIRY3.PageCode;
    PUCHAR data = Srb->DataBuffer;
    
    switch (pageCode) {
        
        case VPD_SUPPORTED_PAGES:
            // 支持的 VPD 页列表
            return VNvmeVpdSupportedPages(LuExtension, Srb);
            
        case VPD_SERIAL_NUMBER:
            // 序列号页 (0x80)
            return VNvmeVpdSerialNumber(LuExtension, Srb);
            
        case VPD_DEVICE_IDENTIFIERS:
            // 设备标识页 (0x83) - MPIO 必需
            return VNvmeVpdDeviceIdentifiers(LuExtension, Srb);
            
        case VPD_BLOCK_LIMITS:
            // 块限制页 (0xB0) - UNMAP 必需
            return VNvmeVpdBlockLimits(LuExtension, Srb);
            
        case VPD_LOGICAL_BLOCK_PROVISIONING:
            // 逻辑块配置页 (0xB2) - 精简配置
            return VNvmeVpdLogicalBlockProvisioning(LuExtension, Srb);
            
        default:
            VNvmeSetSenseData(Srb,
                SCSI_SENSE_ILLEGAL_REQUEST,
                SCSI_ADSENSE_INVALID_CDB, 0);
            return SRB_STATUS_INVALID_REQUEST;
    }
}

//
// 设备标识 VPD 页 - 对 MPIO 至关重要
//
UCHAR VNvmeVpdDeviceIdentifiers(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVPD_IDENTIFICATION_PAGE page = Srb->DataBuffer;
    PVPD_IDENTIFICATION_DESCRIPTOR desc;
    ULONG totalLength;
    
    RtlZeroMemory(page, Srb->DataTransferLength);
    
    page->DeviceType = DIRECT_ACCESS_DEVICE;
    page->PageCode = VPD_DEVICE_IDENTIFIERS;
    
    // 添加 NAA 标识符 (MPIO 使用)
    desc = (PVPD_IDENTIFICATION_DESCRIPTOR)page->Descriptors;
    
    desc->CodeSet = VpdCodeSetBinary;
    desc->IdentifierType = VpdIdentifierTypeNAA;
    desc->Association = VpdAssocDevice;
    desc->IdentifierLength = 8;
    
    // NAA 6 格式标识符 (基于 LUN 唯一 ID)
    RtlCopyMemory(desc->Identifier, LuExtension->DeviceIdentifier, 8);
    
    totalLength = sizeof(VPD_IDENTIFICATION_PAGE) +
                  sizeof(VPD_IDENTIFICATION_DESCRIPTOR) + 8;
    
    page->PageLength = totalLength - 4;
    Srb->DataTransferLength = min(Srb->DataTransferLength, totalLength);
    
    return SRB_STATUS_SUCCESS;
}
```

### READ CAPACITY

```c
//
// READ CAPACITY (10) - 最大支持 2TB
//
UCHAR VNvmeScsiReadCapacity(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PREAD_CAPACITY_DATA data = Srb->DataBuffer;
    ULONG lastLba;
    ULONG blockSize;
    
    if (!LuExtension || LuExtension->State != VNVME_LUN_STATE_READY) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    // 最后一个 LBA (0-based)
    if (LuExtension->TotalSectors > 0xFFFFFFFF) {
        // 超过 2TB，返回 0xFFFFFFFF 表示使用 READ CAPACITY 16
        lastLba = 0xFFFFFFFF;
    } else {
        lastLba = (ULONG)(LuExtension->TotalSectors - 1);
    }
    
    blockSize = LuExtension->SectorSize;
    
    // 转换为大端
    REVERSE_BYTES(&data->LogicalBlockAddress, &lastLba);
    REVERSE_BYTES(&data->BytesPerBlock, &blockSize);
    
    Srb->DataTransferLength = sizeof(READ_CAPACITY_DATA);
    return SRB_STATUS_SUCCESS;
}

//
// READ CAPACITY (16) - 支持大于 2TB
//
UCHAR VNvmeScsiReadCapacity16(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PREAD_CAPACITY16_DATA data = Srb->DataBuffer;
    ULONGLONG lastLba;
    ULONG blockSize;
    
    if (!LuExtension || LuExtension->State != VNVME_LUN_STATE_READY) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    lastLba = LuExtension->TotalSectors - 1;
    blockSize = LuExtension->SectorSize;
    
    RtlZeroMemory(data, sizeof(READ_CAPACITY16_DATA));
    
    // 转换为大端
    REVERSE_BYTES_QUAD(&data->LogicalBlockAddress, &lastLba);
    REVERSE_BYTES(&data->BytesPerBlock, &blockSize);
    
    // 报告精简配置能力
    data->LBPME = 1;    // 逻辑块配置管理已启用
    data->LBPRZ = 1;    // 读取归零
    
    Srb->DataTransferLength = sizeof(READ_CAPACITY16_DATA);
    return SRB_STATUS_SUCCESS;
}
```

### READ 命令

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
    UCHAR opCode = cdb->CDB6GENERIC.OperationCode;
    
    if (!LuExtension || LuExtension->State != VNVME_LUN_STATE_READY) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    //
    // 解析 CDB
    //
    switch (opCode) {
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
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ADSENSE_ILLEGAL_BLOCK, 0);
        return SRB_STATUS_ERROR;
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
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_MEDIUM_ERROR,
            SCSI_ADSENSE_UNRECOVERED_ERROR, 0);
        return SRB_STATUS_ERROR;
    }
    
    //
    // 更新统计
    //
    InterlockedIncrement64(&LuExtension->Stats.ReadCommands);
    InterlockedAdd64(&LuExtension->Stats.BytesRead, length);
    
    Srb->DataTransferLength = length;
    return SRB_STATUS_SUCCESS;
}
```

### WRITE 命令

```c
UCHAR VNvmeScsiWrite(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    ULONGLONG lba;
    ULONG sectors;
    ULONGLONG offset;
    ULONG length;
    NTSTATUS status;
    BOOLEAN fua = FALSE;
    UCHAR opCode = cdb->CDB6GENERIC.OperationCode;
    
    if (!LuExtension || LuExtension->State != VNVME_LUN_STATE_READY) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    // 只读检查
    if (LuExtension->ReadOnly) {
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_DATA_PROTECT,
            SCSI_ADSENSE_WRITE_PROTECT, 0);
        return SRB_STATUS_ERROR;
    }
    
    //
    // 解析 CDB (与 READ 类似)
    //
    switch (opCode) {
        case SCSIOP_WRITE6:
            lba = ((cdb->CDB6READWRITE.LogicalBlockMsb1 & 0x1F) << 16) |
                  (cdb->CDB6READWRITE.LogicalBlockMsb0 << 8) |
                  cdb->CDB6READWRITE.LogicalBlockLsb;
            sectors = cdb->CDB6READWRITE.TransferBlocks;
            if (sectors == 0) sectors = 256;
            break;
            
        case SCSIOP_WRITE:  // WRITE10
            REVERSE_BYTES(&lba, &cdb->CDB10.LogicalBlockByte0);
            REVERSE_BYTES_SHORT(&sectors, &cdb->CDB10.TransferBlocksMsb);
            fua = (cdb->CDB10.ForceUnitAccess != 0);
            break;
            
        case SCSIOP_WRITE12:
            REVERSE_BYTES(&lba, &cdb->CDB12.LogicalBlock);
            REVERSE_BYTES(&sectors, &cdb->CDB12.TransferLength);
            fua = (cdb->CDB12.ForceUnitAccess != 0);
            break;
            
        case SCSIOP_WRITE16:
            REVERSE_BYTES_QUAD(&lba, &cdb->CDB16.LogicalBlock);
            REVERSE_BYTES(&sectors, &cdb->CDB16.TransferLength);
            fua = (cdb->CDB16.ForceUnitAccess != 0);
            break;
            
        default:
            return SRB_STATUS_INVALID_REQUEST;
    }
    
    // 边界检查
    if (lba + sectors > LuExtension->TotalSectors) {
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ADSENSE_ILLEGAL_BLOCK, 0);
        return SRB_STATUS_ERROR;
    }
    
    offset = lba * LuExtension->SectorSize;
    length = sectors * LuExtension->SectorSize;
    
    //
    // 执行后端写入
    //
    status = VNvmeBackendWrite(
        LuExtension->Backend,
        offset,
        length,
        Srb->DataBuffer);
    
    if (!NT_SUCCESS(status)) {
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_MEDIUM_ERROR,
            SCSI_ADSENSE_WRITE_ERROR, 0);
        InterlockedIncrement64(&LuExtension->Stats.WriteErrors);
        return SRB_STATUS_ERROR;
    }
    
    //
    // FUA: 强制刷新到持久存储
    //
    if (fua) {
        VNvmeBackendFlush(LuExtension->Backend);
    }
    
    //
    // 更新统计
    //
    InterlockedIncrement64(&LuExtension->Stats.WriteCommands);
    InterlockedAdd64(&LuExtension->Stats.BytesWritten, length);
    
    Srb->DataTransferLength = length;
    return SRB_STATUS_SUCCESS;
}
```

### MODE SENSE

```c
UCHAR VNvmeScsiModeSense(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PCDB cdb = (PCDB)Srb->Cdb;
    UCHAR pageCode = cdb->MODE_SENSE.PageCode;
    BOOLEAN dbd = cdb->MODE_SENSE.Dbd;  // 禁用块描述符
    PUCHAR buffer = Srb->DataBuffer;
    ULONG offset = 0;
    PMODE_PARAMETER_HEADER header;
    
    RtlZeroMemory(buffer, Srb->DataTransferLength);
    
    //
    // 模式参数头
    //
    header = (PMODE_PARAMETER_HEADER)buffer;
    header->MediumType = 0;
    header->DeviceSpecificParameter = 0;
    if (LuExtension->ReadOnly) {
        header->DeviceSpecificParameter |= MODE_DSP_WRITE_PROTECT;
    }
    
    offset = sizeof(MODE_PARAMETER_HEADER);
    
    //
    // 块描述符 (除非 DBD=1)
    //
    if (!dbd) {
        // 添加块描述符
        header->BlockDescriptorLength = sizeof(MODE_PARAMETER_BLOCK);
        offset += sizeof(MODE_PARAMETER_BLOCK);
    }
    
    //
    // 模式页
    //
    switch (pageCode) {
        case MODE_PAGE_CACHING:
            offset += VNvmeBuildCachingPage(LuExtension, buffer + offset);
            break;
            
        case MODE_PAGE_CONTROL:
            offset += VNvmeBuildControlPage(LuExtension, buffer + offset);
            break;
            
        case MODE_SENSE_RETURN_ALL:
            offset += VNvmeBuildCachingPage(LuExtension, buffer + offset);
            offset += VNvmeBuildControlPage(LuExtension, buffer + offset);
            break;
            
        default:
            VNvmeSetSenseData(Srb,
                SCSI_SENSE_ILLEGAL_REQUEST,
                SCSI_ADSENSE_INVALID_CDB, 0);
            return SRB_STATUS_INVALID_REQUEST;
    }
    
    header->ModeDataLength = (UCHAR)(offset - 1);
    Srb->DataTransferLength = offset;
    
    return SRB_STATUS_SUCCESS;
}
```

### UNMAP (TRIM)

```c
UCHAR VNvmeScsiUnmap(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PUNMAP_LIST_HEADER header = Srb->DataBuffer;
    PUNMAP_BLOCK_DESCRIPTOR desc;
    ULONG descCount;
    ULONG i;
    ULONGLONG lba, blocks;
    NTSTATUS status;
    
    if (!LuExtension || LuExtension->State != VNVME_LUN_STATE_READY) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    // 检查后端是否支持 TRIM
    if (!(LuExtension->Backend->Capabilities & VNVME_BACKEND_CAP_TRIM)) {
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_ILLEGAL_REQUEST,
            SCSI_ADSENSE_INVALID_CDB, 0);
        return SRB_STATUS_INVALID_REQUEST;
    }
    
    // 解析 UNMAP 列表
    REVERSE_BYTES_SHORT(&descCount, &header->BlockDescrDataLength);
    descCount = descCount / sizeof(UNMAP_BLOCK_DESCRIPTOR);
    
    desc = (PUNMAP_BLOCK_DESCRIPTOR)(header + 1);
    
    for (i = 0; i < descCount; i++) {
        REVERSE_BYTES_QUAD(&lba, &desc[i].StartingLba);
        REVERSE_BYTES(&blocks, &desc[i].LbaCount);
        
        // 边界检查
        if (lba + blocks > LuExtension->TotalSectors) {
            continue;  // 跳过无效范围
        }
        
        // 执行 TRIM
        status = VNvmeBackendTrim(
            LuExtension->Backend,
            lba * LuExtension->SectorSize,
            blocks * LuExtension->SectorSize);
        
        if (!NT_SUCCESS(status)) {
            // TRIM 失败不是致命错误，继续处理
        }
    }
    
    InterlockedIncrement64(&LuExtension->Stats.UnmapCommands);
    
    return SRB_STATUS_SUCCESS;
}
```

### SYNCHRONIZE CACHE

```c
UCHAR VNvmeScsiSynchronizeCache(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    NTSTATUS status;
    
    UNREFERENCED_PARAMETER(Srb);
    
    if (!LuExtension || LuExtension->State != VNVME_LUN_STATE_READY) {
        return SRB_STATUS_SELECTION_TIMEOUT;
    }
    
    // 刷新后端缓存
    status = VNvmeBackendFlush(LuExtension->Backend);
    
    if (!NT_SUCCESS(status)) {
        VNvmeSetSenseData(Srb,
            SCSI_SENSE_MEDIUM_ERROR,
            SCSI_ADSENSE_WRITE_ERROR, 0);
        return SRB_STATUS_ERROR;
    }
    
    InterlockedIncrement64(&LuExtension->Stats.FlushCommands);
    
    return SRB_STATUS_SUCCESS;
}
```

## 请求完成

### StorPort 通知

```c
//
// 完成 SRB 请求
//
VOID VNvmeCompleteRequest(
    _In_ PVNVME_ADAPTER_EXTENSION AdapterExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SrbStatus)
{
    Srb->SrbStatus = SrbStatus;
    
    StorPortNotification(
        RequestComplete,
        AdapterExtension,
        Srb);
}

//
// 设置 Sense 数据
//
VOID VNvmeSetSenseData(
    _In_ PSCSI_REQUEST_BLOCK Srb,
    _In_ UCHAR SenseKey,
    _In_ UCHAR AdditionalSenseCode,
    _In_ UCHAR AdditionalSenseCodeQualifier)
{
    PSENSE_DATA senseData;
    
    if (Srb->SenseInfoBufferLength < sizeof(SENSE_DATA)) {
        return;
    }
    
    senseData = Srb->SenseInfoBuffer;
    RtlZeroMemory(senseData, sizeof(SENSE_DATA));
    
    senseData->ErrorCode = SCSI_SENSE_ERRORCODE_FIXED_CURRENT;
    senseData->SenseKey = SenseKey;
    senseData->AdditionalSenseLength = sizeof(SENSE_DATA) - 
        FIELD_OFFSET(SENSE_DATA, AdditionalSenseLength) - 1;
    senseData->AdditionalSenseCode = AdditionalSenseCode;
    senseData->AdditionalSenseCodeQualifier = AdditionalSenseCodeQualifier;
    
    Srb->SrbStatus |= SRB_STATUS_AUTOSENSE_VALID;
    Srb->ScsiStatus = SCSISTAT_CHECK_CONDITION;
}
```

## SCSI 状态码映射

| SRB Status | SCSI Status | 说明 |
|------------|-------------|------|
| `SRB_STATUS_SUCCESS` | `SCSISTAT_GOOD` | 成功 |
| `SRB_STATUS_ERROR` | `SCSISTAT_CHECK_CONDITION` | 错误，查看 Sense |
| `SRB_STATUS_SELECTION_TIMEOUT` | - | 设备不存在 |
| `SRB_STATUS_INVALID_REQUEST` | - | 无效请求 |
| `SRB_STATUS_DATA_OVERRUN` | - | 数据溢出 |
| `SRB_STATUS_BUSY` | `SCSISTAT_BUSY` | 设备忙 |

## 异步 I/O 支持 (可选)

对于大型 I/O 操作，可以使用异步处理避免阻塞 StorPort：

```c
UCHAR VNvmeScsiReadAsync(
    _In_ PVNVME_LU_EXTENSION LuExtension,
    _In_ PSCSI_REQUEST_BLOCK Srb)
{
    PVNVME_SRB_EXTENSION srbExt = Srb->SrbExtension;
    
    // 设置异步上下文
    srbExt->LuExtension = LuExtension;
    srbExt->BackendIo = CreateBackendIo(Srb);
    srbExt->BackendIo->CompletionCallback = VNvmeAsyncIoComplete;
    
    // 启动异步读取
    VNvmeBackendReadAsync(LuExtension->Backend, srbExt->BackendIo);
    
    // 返回 PENDING，稍后通过回调完成
    return SRB_STATUS_PENDING;
}

VOID VNvmeAsyncIoComplete(
    _In_ PVNVME_BACKEND_IO Io,
    _In_ NTSTATUS Status)
{
    PSCSI_REQUEST_BLOCK Srb = Io->Context;
    PVNVME_SRB_EXTENSION srbExt = Srb->SrbExtension;
    
    if (NT_SUCCESS(Status)) {
        Srb->SrbStatus = SRB_STATUS_SUCCESS;
        Srb->DataTransferLength = Io->BytesTransferred;
    } else {
        Srb->SrbStatus = SRB_STATUS_ERROR;
        VNvmeSetSenseData(Srb, SCSI_SENSE_MEDIUM_ERROR, 
            SCSI_ADSENSE_UNRECOVERED_ERROR, 0);
    }
    
    // 通知 StorPort 完成
    StorPortNotification(
        RequestComplete,
        srbExt->LuExtension->AdapterExtension,
        Srb);
}
```
