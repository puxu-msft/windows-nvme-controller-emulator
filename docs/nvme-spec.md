# NVMe 规范参考

## 概述

本项目实现的是 **StorPort Virtual Miniport** 驱动，对上层应用呈现为标准 **SCSI 磁盘设备**，而非原生 NVMe 设备。因此，本项目不直接实现 NVMe 协议栈，但借鉴 NVMe 规范的部分概念用于：

1. **设备标识信息**：序列号、型号、固件版本等格式参考 NVMe Identify 规范
2. **性能特性模拟**：队列机制、高 IOPS 等 NVMe 特性概念
3. **SMART/健康信息**：日志格式参考 NVMe SMART 规范
4. **TRIM/UNMAP 支持**：逻辑块释放功能

**重要说明**：
- 本驱动接收的是 **SCSI 命令** (由 disk.sys 生成)，不是 NVMe 命令
- 用户态程序通过标准磁盘 API 访问设备，无需了解 NVMe 细节
- NVMe 规范仅作为设计参考，实际实现遵循 SCSI 规范

---

## 本项目中的 NVMe 概念应用

### 设备标识映射

| NVMe 概念 | SCSI 等效 | 本项目应用 |
|-----------|----------|------------|
| Controller | Adapter | VNVME_ADAPTER_EXTENSION |
| Namespace | LUN | VNVME_LU_EXTENSION |
| Namespace Size | Capacity | READ CAPACITY 响应 |
| Serial Number | VPD 0x80 | INQUIRY 页 0x80 |
| Model Number | INQUIRY data | 标准 INQUIRY 响应 |
| Firmware Rev | INQUIRY data | 标准 INQUIRY 响应 |
| SMART Log | 自定义 | VPD 页或厂商命令 |

### 命令映射

| NVMe 命令 | SCSI 等效命令 | 说明 |
|-----------|--------------|------|
| Read | READ(6/10/12/16) | 数据读取 |
| Write | WRITE(6/10/12/16) | 数据写入 |
| Flush | SYNCHRONIZE CACHE | 缓存同步 |
| Dataset Management (TRIM) | UNMAP | 逻辑块释放 |
| Identify | INQUIRY + VPD | 设备识别 |

---

## NVMe 规范概述 (参考)

### NVMe 简介

NVMe (Non-Volatile Memory Express) 是专为闪存和 SSD 设计的高性能存储协议。

**规范版本**: 本项目参考 NVMe Base Specification Revision 1.4

### 核心概念

#### 控制器 (Controller)
- 管理一个或多个命名空间
- 处理管理命令和 I/O 命令
- 维护控制器状态
- 每个控制器有唯一的 Controller ID (CNTLID)

#### 命名空间 (Namespace)
- 可寻址的逻辑块集合
- 每个命名空间有唯一的 NSID (1 到 0xFFFFFFFE)
- NSID 0xFFFFFFFF 表示广播到所有命名空间
- 支持不同的块大小 (512B, 4KB 等) 和容量

#### 队列 (Queues)
- **Admin Queue**: 管理命令提交/完成 (Queue ID = 0)
- **I/O Queue**: 读写命令提交/完成 (Queue ID ≥ 1)
- 提交队列 (SQ) 和完成队列 (CQ) 配对
- 多个 SQ 可以关联到同一个 CQ

**与本项目的关系**：StorPort 自动管理 I/O 队列，虚拟设备获得 250 队列深度（物理设备为 20）。

---

## 设备标识信息规范

### Identify Controller 数据结构 (参考)

本项目使用以下字段生成 SCSI INQUIRY 响应：

```c
//
// NVMe Identify Controller 数据结构 (部分)
// 仅用于理解数据格式，不直接实现
//
typedef struct _NVME_IDENTIFY_CONTROLLER {
    // 字节 0-1: PCI Vendor ID
    USHORT VID;
    
    // 字节 2-3: PCI Subsystem Vendor ID  
    USHORT SSVID;
    
    // 字节 4-23: Serial Number (20 字节 ASCII)
    CHAR SN[20];
    
    // 字节 24-63: Model Number (40 字节 ASCII)
    CHAR MN[40];
    
    // 字节 64-71: Firmware Revision (8 字节 ASCII)
    CHAR FR[8];
    
    // 字节 72: Recommended Arbitration Burst
    UCHAR RAB;
    
    // 字节 73-75: IEEE OUI Identifier
    UCHAR IEEE[3];
    
    // ... 其他字段省略
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;
```

### 本项目的 SCSI 设备标识实现

```c
//
// 虚拟设备标识信息
//
typedef struct _VNVME_DEVICE_IDENTITY {
    // 供应商标识 (8 字节，ASCII，空格填充)
    CHAR VendorId[8];           // "VNVME   "
    
    // 产品标识 (16 字节，ASCII，空格填充)
    CHAR ProductId[16];         // "Virtual NVMe   "
    
    // 产品版本 (4 字节，ASCII)
    CHAR ProductRevision[4];    // "1.0 "
    
    // 序列号 (可变长度，最大 20 字节)
    CHAR SerialNumber[20];
    
    // 固件版本 (8 字节)
    CHAR FirmwareRevision[8];
    
    // NAA 标识符 (8 字节，用于 VPD 0x83)
    UCHAR NaaIdentifier[8];
    
} VNVME_DEVICE_IDENTITY, *PVNVME_DEVICE_IDENTITY;

//
// 初始化设备标识
//
VOID
VNvmeInitializeDeviceIdentity(
    _Out_ PVNVME_DEVICE_IDENTITY pIdentity,
    _In_ ULONG LunId,
    _In_opt_ PCSTR CustomSerial)
{
    // 清零
    RtlZeroMemory(pIdentity, sizeof(VNVME_DEVICE_IDENTITY));
    
    // 设置供应商标识 (空格填充)
    RtlCopyMemory(pIdentity->VendorId, "VNVME   ", 8);
    
    // 设置产品标识
    RtlCopyMemory(pIdentity->ProductId, "Virtual NVMe    ", 16);
    
    // 设置版本
    RtlCopyMemory(pIdentity->ProductRevision, "1.0 ", 4);
    
    // 生成或使用自定义序列号
    if (CustomSerial && CustomSerial[0]) {
        RtlCopyMemory(pIdentity->SerialNumber, CustomSerial, 
                      min(strlen(CustomSerial), 20));
    } else {
        // 生成唯一序列号: VNVME-<timestamp>-<LunId>
        LARGE_INTEGER time;
        KeQuerySystemTime(&time);
        RtlStringCbPrintfA(pIdentity->SerialNumber, 20,
                          "VNVME%08X%02X", 
                          (ULONG)time.LowPart, LunId);
    }
    
    // 设置固件版本
    RtlCopyMemory(pIdentity->FirmwareRevision, "1.0.0   ", 8);
    
    // 生成 NAA 标识符 (NAA=6, IEEE Company ID + 唯一值)
    // NAA 格式: 6 + IEEE OUI (24-bit) + Vendor Specific (36-bit)
    pIdentity->NaaIdentifier[0] = 0x60;  // NAA=6
    pIdentity->NaaIdentifier[1] = 0x00;  // IEEE OUI (示例)
    pIdentity->NaaIdentifier[2] = 0x00;
    pIdentity->NaaIdentifier[3] = 0x00;
    // 后 4 字节为唯一值 (使用 LUN ID 和时间戳)
    *(PULONG)&pIdentity->NaaIdentifier[4] = 
        (LunId << 24) | (time.LowPart & 0x00FFFFFF);
}
```

---

## SMART 健康信息 (参考)

### NVMe SMART Log 格式

NVMe 规范定义的 SMART 日志 (Log ID = 0x02, 512 字节)：

```
偏移    大小    字段                          说明
------  ------  ----------------------------  ---------------------
0x00    1       Critical Warning              临界警告标志
                                                [0] 可用备用空间低于阈值
                                                [1] 温度超过阈值
                                                [2] 可靠性降级
                                                [3] 只读模式
                                                [4] 易失性备份失败
0x01    2       Composite Temperature         复合温度 (Kelvin)
0x03    1       Available Spare               可用备用空间 (%)
0x04    1       Available Spare Threshold     可用备用阈值 (%)
0x05    1       Percentage Used               已使用寿命百分比
0x06    26      Reserved                      保留
0x20    16      Data Units Read               读取数据单元 (× 1000 × 512B)
0x30    16      Data Units Written            写入数据单元 (× 1000 × 512B)
0x40    16      Host Read Commands            主机读命令数
0x50    16      Host Write Commands           主机写命令数
0x60    16      Controller Busy Time          控制器忙时间 (分钟)
0x70    16      Power Cycles                  上电周期数
0x80    16      Power On Hours                上电小时数
0x90    16      Unsafe Shutdowns              非安全关机次数
0xA0    16      Media Errors                  介质/数据完整性错误数
0xB0    16      Error Log Entries             错误日志条目数
0xC0    4       Warning Composite Temp Time   警告温度时间
0xC4    4       Critical Composite Temp Time  临界温度时间
0xC8    16      Temperature Sensors           温度传感器 1-8
0xD8    296     Reserved                      保留
```

### 本项目的统计信息实现

```c
//
// 驱动维护的统计信息
//
typedef struct _VNVME_STATISTICS {
    // I/O 计数器
    volatile LONG64 ReadOperations;
    volatile LONG64 WriteOperations;
    volatile LONG64 BytesRead;
    volatile LONG64 BytesWritten;
    
    // 延迟统计 (微秒)
    volatile LONG64 TotalReadLatencyUs;
    volatile LONG64 TotalWriteLatencyUs;
    volatile LONG64 MaxReadLatencyUs;
    volatile LONG64 MaxWriteLatencyUs;
    
    // 错误计数
    volatile LONG64 ReadErrors;
    volatile LONG64 WriteErrors;
    volatile LONG64 OtherErrors;
    
    // 时间信息
    LARGE_INTEGER StartTime;    // 驱动启动时间
    volatile LONG64 UptimeSeconds;
    
} VNVME_STATISTICS, *PVNVME_STATISTICS;

//
// 更新读取统计
//
FORCEINLINE
VOID
VNvmeUpdateReadStats(
    _Inout_ PVNVME_STATISTICS pStats,
    _In_ ULONG ByteCount,
    _In_ ULONG LatencyUs)
{
    InterlockedIncrement64(&pStats->ReadOperations);
    InterlockedAdd64(&pStats->BytesRead, ByteCount);
    InterlockedAdd64(&pStats->TotalReadLatencyUs, LatencyUs);
    
    // 更新最大延迟 (简化的原子更新)
    LONG64 currentMax = pStats->MaxReadLatencyUs;
    while (LatencyUs > currentMax) {
        LONG64 oldMax = InterlockedCompareExchange64(
            &pStats->MaxReadLatencyUs, LatencyUs, currentMax);
        if (oldMax == currentMax) break;
        currentMax = oldMax;
    }
}

//
// 生成 SMART 风格的健康报告 (用于 WMI 或厂商命令)
//
VOID
VNvmeGetHealthInfo(
    _In_ PVNVME_STATISTICS pStats,
    _Out_ PVNVME_HEALTH_INFO pHealth)
{
    RtlZeroMemory(pHealth, sizeof(VNVME_HEALTH_INFO));
    
    // 虚拟设备总是健康的
    pHealth->CriticalWarning = 0;
    
    // 模拟温度 (室温 + 一点变化)
    pHealth->Temperature = 298 + (pStats->ReadOperations % 10);  // ~25°C
    
    // 虚拟设备没有备用空间概念，总是 100%
    pHealth->AvailableSpare = 100;
    pHealth->AvailableSpareThreshold = 10;
    pHealth->PercentageUsed = 0;  // 虚拟设备不损耗
    
    // 转换 I/O 统计
    // NVMe 单位: 1 Data Unit = 1000 × 512 字节 = 512KB
    pHealth->DataUnitsRead = pStats->BytesRead / (1000 * 512);
    pHealth->DataUnitsWritten = pStats->BytesWritten / (1000 * 512);
    pHealth->HostReadCommands = pStats->ReadOperations;
    pHealth->HostWriteCommands = pStats->WriteOperations;
    
    // 运行时间
    LARGE_INTEGER currentTime;
    KeQuerySystemTime(&currentTime);
    pHealth->PowerOnHours = 
        (currentTime.QuadPart - pStats->StartTime.QuadPart) / 
        (10000000ULL * 3600);  // 100ns 转小时
}
```

---

## 块设备特性

### NVMe 命名空间特性

NVMe 命名空间具有以下关键特性，本项目在 SCSI 层等效实现：

| NVMe 特性 | 说明 | SCSI 等效 | 本项目实现 |
|-----------|------|-----------|------------|
| NSZE | 命名空间大小 (块数) | READ CAPACITY | 支持 |
| NCAP | 命名空间容量 | READ CAPACITY | 支持 |
| FLBAS | 格式化 LBA 大小 | 块大小 | 512/4096 |
| DLFEAT | Deallocate 特性 | UNMAP 支持 | 支持 |
| NGUID | 命名空间 GUID | VPD 0x83 NAA | 支持 |

### 块大小支持

```c
//
// 支持的块大小
//
typedef enum _VNVME_BLOCK_SIZE {
    VNVME_BLOCK_SIZE_512 = 512,
    VNVME_BLOCK_SIZE_4K = 4096
} VNVME_BLOCK_SIZE;

//
// 验证块大小
//
FORCEINLINE
BOOLEAN
VNvmeIsValidBlockSize(ULONG BlockSize)
{
    return (BlockSize == VNVME_BLOCK_SIZE_512 ||
            BlockSize == VNVME_BLOCK_SIZE_4K);
}

//
// READ CAPACITY 响应生成
//
VOID
VNvmeBuildReadCapacity16Response(
    _In_ PVNVME_LU_EXTENSION pLu,
    _Out_ PREAD_CAPACITY16_DATA pData)
{
    RtlZeroMemory(pData, sizeof(READ_CAPACITY16_DATA));
    
    // 最后一个 LBA (大端)
    ULONGLONG lastLba = pLu->BlockCount - 1;
    REVERSE_BYTES_QUAD(&pData->LogicalBlockAddress, &lastLba);
    
    // 块大小 (大端)
    ULONG blockSize = pLu->BlockSize;
    REVERSE_BYTES(&pData->BytesPerBlock, &blockSize);
    
    // 精简配置位
    if (pLu->Flags.ThinProvisioned) {
        pData->LBPME = 1;  // Logical Block Provisioning Management Enabled
        pData->LBPRZ = 1;  // Logical Block Provisioning Read Zeros
    }
}
```

---

## TRIM/UNMAP 支持

### NVMe Dataset Management 命令

NVMe 使用 Dataset Management 命令 (Opcode 0x09) 实现 TRIM 功能。

### 本项目的 SCSI UNMAP 实现

```c
//
// UNMAP 命令处理
//
UCHAR
VNvmeHandleUnmap(
    _In_ PVNVME_ADAPTER_EXTENSION pAdapter,
    _In_ PVNVME_LU_EXTENSION pLu,
    _In_ PSCSI_REQUEST_BLOCK pSrb)
{
    PCDB pCdb = (PCDB)pSrb->Cdb;
    PUNMAP_LIST_HEADER pHeader;
    PUNMAP_BLOCK_DESCRIPTOR pDesc;
    ULONG descCount;
    ULONG i;
    
    // 获取数据缓冲区
    pHeader = (PUNMAP_LIST_HEADER)pSrb->DataBuffer;
    
    // 验证数据长度
    USHORT dataLength = (pCdb->UNMAP.AllocationLength[0] << 8) |
                        pCdb->UNMAP.AllocationLength[1];
    
    if (pSrb->DataTransferLength < sizeof(UNMAP_LIST_HEADER)) {
        return SRB_STATUS_DATA_OVERRUN;
    }
    
    // 解析描述符数量
    USHORT descListLength = (pHeader->BlockDescrDataLength[0] << 8) |
                            pHeader->BlockDescrDataLength[1];
    descCount = descListLength / sizeof(UNMAP_BLOCK_DESCRIPTOR);
    
    // 处理每个描述符
    pDesc = (PUNMAP_BLOCK_DESCRIPTOR)(pHeader + 1);
    
    for (i = 0; i < descCount; i++) {
        ULONGLONG startLba;
        ULONG blockCount;
        
        // 解析 LBA (大端)
        REVERSE_BYTES_QUAD(&startLba, pDesc->StartingLba);
        
        // 解析块数 (大端)
        blockCount = (pDesc->LbaCount[0] << 24) |
                     (pDesc->LbaCount[1] << 16) |
                     (pDesc->LbaCount[2] << 8) |
                     pDesc->LbaCount[3];
        
        // 调用后端释放块
        if (pLu->pBackend && pLu->pBackend->Deallocate) {
            NTSTATUS status = pLu->pBackend->Deallocate(
                pLu->pBackendContext,
                startLba,
                blockCount);
            
            if (!NT_SUCCESS(status)) {
                // 记录错误但继续处理其他描述符
                VNvmeLogError(pAdapter, 
                    VNVME_ERROR_UNMAP_FAILED,
                    (ULONG)startLba, blockCount);
            }
        }
        
        pDesc++;
    }
    
    pSrb->DataTransferLength = 0;
    return SRB_STATUS_SUCCESS;
}
```

---

## 电源管理 (参考)

### NVMe 电源状态

NVMe 定义了多个电源状态 (PS0-PS31)，本项目作为虚拟设备采用简化的电源管理：

| 状态 | NVMe | 本项目 |
|------|------|--------|
| 活动 | PS0 | D0 (正常运行) |
| 空闲 | PS1-PS2 | D0 (保持响应) |
| 待机 | PS3+ | 不适用 |
| 关闭 | 不适用 | 停止适配器 |

```c
//
// StorPort 电源管理回调
//
SCSI_ADAPTER_CONTROL_STATUS
VNvmeHwAdapterControl(
    _In_ PVOID DeviceExtension,
    _In_ SCSI_ADAPTER_CONTROL_TYPE ControlType,
    _In_ PVOID Parameters)
{
    PVNVME_ADAPTER_EXTENSION pAdapter = DeviceExtension;
    
    switch (ControlType) {
        case ScsiStopAdapter:
            // 系统关机或休眠前调用
            // 刷新所有后端数据
            for (ULONG i = 0; i < VNVME_MAX_LUNS; i++) {
                if (pAdapter->Luns[i].Flags.Present &&
                    pAdapter->Luns[i].pBackend &&
                    pAdapter->Luns[i].pBackend->Flush) {
                    pAdapter->Luns[i].pBackend->Flush(
                        pAdapter->Luns[i].pBackendContext);
                }
            }
            pAdapter->AdapterState = VNVME_ADAPTER_STATE_STOPPED;
            return ScsiAdapterControlSuccess;
            
        case ScsiRestartAdapter:
            // 从休眠恢复后调用
            // 重新初始化后端连接
            for (ULONG i = 0; i < VNVME_MAX_LUNS; i++) {
                if (pAdapter->Luns[i].Flags.Present) {
                    VNvmeReinitializeBackend(&pAdapter->Luns[i]);
                }
            }
            pAdapter->AdapterState = VNVME_ADAPTER_STATE_RUNNING;
            return ScsiAdapterControlSuccess;
            
        default:
            return ScsiAdapterControlUnsuccessful;
    }
}
```

---

## NVMe 与 SCSI 对比

### 命令结构差异

| 方面 | NVMe | SCSI (StorPort) |
|------|------|-----------------|
| 命令格式 | 64 字节 SQE | 可变 CDB (6-32 字节) |
| 完成格式 | 16 字节 CQE | SRB 状态 + Sense Data |
| 队列管理 | 应用/驱动管理 | StorPort 自动管理 |
| 中断 | MSI-X | StorPort 抽象 |
| 地址空间 | PRP/SGL | MDL/物理地址 |

### 性能特性对比

| 特性 | 原生 NVMe | StorPort Virtual Miniport |
|------|-----------|---------------------------|
| 队列数量 | 最多 65535 | StorPort 管理 |
| 队列深度 | 最多 65535 | 250 (虚拟设备) |
| 延迟 | ~10 μs | ~50-100 μs (额外层) |
| CPU 开销 | 低 | 中等 |
| 驱动复杂度 | 高 | 低 |

### 选择 StorPort 的原因

1. **微软推荐**：虚拟存储设备官方推荐使用 StorPort
2. **稳定性**：StorPort 提供成熟的错误处理和恢复机制
3. **兼容性**：自动支持 MPIO、集群、Hyper-V 等企业功能
4. **简化开发**：无需实现 NVMe 队列管理、中断处理等复杂逻辑
5. **性能足够**：对于虚拟设备，StorPort 性能开销可接受

---

## 参考资料

- [NVM Express Base Specification 1.4](https://nvmexpress.org/specifications/)
- [SCSI Block Commands (SBC-4)](https://www.t10.org/drafts.htm)
- [SCSI Primary Commands (SPC-5)](https://www.t10.org/drafts.htm)
- [StorPort Miniport Drivers](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/storport-miniport-drivers)
- [Virtual Miniport Drivers](https://docs.microsoft.com/en-us/windows-hardware/drivers/storage/overview-of-storage-virtual-miniport-drivers)
