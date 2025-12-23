# NVMe 命令处理

本文档详细说明 NVMe Admin 命令和 I/O 命令的实现。

> ⚠️ **代码风格说明**
> 
> 本文档为设计规范，代码示例展示 NVMe 命令处理的逻辑和流程。
> 函数名如 `VnvmeReadFromPrp`、`VnvmeSetCompletion` 为**概念命名**，
> 实际实现请参考 [vnvme/vnvme.h](../vnvme/vnvme.h) 中的函数声明：
> 
> | 概念函数 | 实际函数 |
> |----------|----------|
> | `VnvmeReadFromPrp` | `VnvmeParsePrpList` + `VnvmeReadFromHostMemory` |
> | `VnvmeWriteToPrp` | `VnvmeParsePrpList` + `VnvmeWriteToHostMemory` |
> | `VnvmeSetCompletion` | `VnvmePostCompletion` |

## 命令格式

### Submission Queue Entry (SQE)

每个 NVMe 命令占用 64 字节：

```c
typedef struct _NVME_COMMAND {
    // DWord 0: Command Dword 0
    struct {
        ULONG OPC    : 8;    // Opcode
        ULONG FUSE   : 2;    // Fused Operation
        ULONG Rsvd   : 4;
        ULONG PSDT   : 2;    // PRP or SGL for Data Transfer
        ULONG CID    : 16;   // Command Identifier
    } CDW0;
    
    // DWord 1: Namespace Identifier
    ULONG NSID;
    
    // DWord 2-3: Reserved
    ULONG CDW2;
    ULONG CDW3;
    
    // DWord 4-5: Metadata Pointer
    ULONG64 MPTR;
    
    // DWord 6-9: Data Pointer (PRP1/PRP2 or SGL)
    union {
        struct {
            ULONG64 PRP1;
            ULONG64 PRP2;
        } PRP;
        UCHAR SGL[16];
    } DPTR;
    
    // DWord 10-15: Command Specific
    ULONG CDW10;
    ULONG CDW11;
    ULONG CDW12;
    ULONG CDW13;
    ULONG CDW14;
    ULONG CDW15;
    
} NVME_COMMAND, *PNVME_COMMAND;

C_ASSERT(sizeof(NVME_COMMAND) == 64);
```

### Completion Queue Entry (CQE)

每个完成条目占用 16 字节：

```c
typedef struct _NVME_COMPLETION {
    // DWord 0: Command Specific
    ULONG DW0;
    
    // DWord 1: Reserved
    ULONG DW1;
    
    // DWord 2: SQ Head Pointer / SQ Identifier
    struct {
        USHORT SQHD;     // SQ Head Pointer
        USHORT SQID;     // SQ Identifier
    };
    
    // DWord 3: Status / CID
    struct {
        USHORT CID;      // Command Identifier
        USHORT Status;   // Phase (bit 0) + Status (bits 1-15)
    };
    
} NVME_COMPLETION, *PNVME_COMPLETION;

C_ASSERT(sizeof(NVME_COMPLETION) == 16);

// 状态码宏
#define NVME_STATUS_SC(status)     (((status) >> 1) & 0xFF)   // Status Code
#define NVME_STATUS_SCT(status)    (((status) >> 9) & 0x7)    // Status Code Type
#define NVME_STATUS_CRD(status)    (((status) >> 12) & 0x3)   // Command Retry Delay
#define NVME_STATUS_MORE(status)   (((status) >> 14) & 0x1)   // More
#define NVME_STATUS_DNR(status)    (((status) >> 15) & 0x1)   // Do Not Retry

#define NVME_MAKE_STATUS(sct, sc, dnr) \
    ((((dnr) & 1) << 15) | (((sct) & 7) << 9) | (((sc) & 0xFF) << 1))
```

---

## Admin 命令

### Identify (Opcode 0x06)

```c
//
// Identify 命令 CDW10
//
typedef union _NVME_IDENTIFY_CDW10 {
    struct {
        ULONG CNS  : 8;    // Controller or Namespace Structure
        ULONG Rsvd : 8;
        ULONG CNTID: 16;   // Controller Identifier
    };
    ULONG AsUlong;
} NVME_IDENTIFY_CDW10;

// CNS 值
#define NVME_IDENTIFY_CNS_NAMESPACE        0x00  // 识别特定命名空间
#define NVME_IDENTIFY_CNS_CONTROLLER       0x01  // 识别控制器
#define NVME_IDENTIFY_CNS_ACTIVE_NSID_LIST 0x02  // 活动命名空间 ID 列表

//
// 处理 Identify 命令
//
NTSTATUS VnvmeProcessIdentify(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    NVME_IDENTIFY_CDW10 cdw10;
    ULONG64 prp1 = Cmd->DPTR.PRP.PRP1;
    NTSTATUS status = STATUS_SUCCESS;
    PVOID dataBuffer;
    
    cdw10.AsUlong = Cmd->CDW10;
    
    // 分配 4KB 临时缓冲区
    dataBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, 4096, 'DTNV');
    if (!dataBuffer) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, 
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(dataBuffer, 4096);
    
    switch (cdw10.CNS) {
    case NVME_IDENTIFY_CNS_CONTROLLER:
        status = VnvmeBuildIdentifyController(Controller, dataBuffer);
        break;
        
    case NVME_IDENTIFY_CNS_NAMESPACE:
        status = VnvmeBuildIdentifyNamespace(Controller, Cmd->NSID, dataBuffer);
        break;
        
    case NVME_IDENTIFY_CNS_ACTIVE_NSID_LIST:
        status = VnvmeBuildActiveNsidList(Controller, Cmd->NSID, dataBuffer);
        break;
        
    default:
        status = STATUS_NOT_SUPPORTED;
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_FIELD, 1);
        break;
    }
    
    if (NT_SUCCESS(status)) {
        // 将数据写入 PRP1 指向的内存
        status = VnvmeWriteToPrp(Controller, prp1, 0, dataBuffer, 4096);
        if (NT_SUCCESS(status)) {
            VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                              NVME_SC_SUCCESS, 0);
        }
    }
    
    ExFreePoolWithTag(dataBuffer, 'DTNV');
    return status;
}
```

### Identify Controller 数据结构

```c
//
// Identify Controller 数据 (4096 字节)
//
typedef struct _NVME_IDENTIFY_CONTROLLER {
    // Bytes 0-255: Controller Capabilities and Features
    USHORT VID;              // 0x00: PCI Vendor ID
    USHORT SSVID;            // 0x02: PCI Subsystem Vendor ID
    CHAR   SN[20];           // 0x04: Serial Number
    CHAR   MN[40];           // 0x18: Model Number
    CHAR   FR[8];            // 0x40: Firmware Revision
    UCHAR  RAB;              // 0x48: Recommended Arbitration Burst
    UCHAR  IEEE[3];          // 0x49: IEEE OUI Identifier
    UCHAR  CMIC;             // 0x4C: Controller Multi-Path I/O
    UCHAR  MDTS;             // 0x4D: Maximum Data Transfer Size
    USHORT CNTLID;           // 0x4E: Controller ID
    ULONG  VER;              // 0x50: Version
    ULONG  RTD3R;            // 0x54: RTD3 Resume Latency
    ULONG  RTD3E;            // 0x58: RTD3 Entry Latency
    ULONG  OAES;             // 0x5C: Optional Async Events Supported
    ULONG  CTRATT;           // 0x60: Controller Attributes
    USHORT RRLS;             // 0x64: Read Recovery Levels Supported
    UCHAR  Rsvd66[9];        // 0x66
    UCHAR  CNTRLTYPE;        // 0x6F: Controller Type
    UCHAR  FGUID[16];        // 0x70: FRU GUID
    USHORT CRDT1;            // 0x80: Command Retry Delay Time 1
    USHORT CRDT2;            // 0x82
    USHORT CRDT3;            // 0x84
    UCHAR  Rsvd134[122];     // 0x86
    
    // Bytes 256-511: Admin Command Set Attributes
    USHORT OACS;             // 0x100: Optional Admin Command Support
    UCHAR  ACL;              // 0x102: Abort Command Limit
    UCHAR  AERL;             // 0x103: Async Event Request Limit
    UCHAR  FRMW;             // 0x104: Firmware Updates
    UCHAR  LPA;              // 0x105: Log Page Attributes
    UCHAR  ELPE;             // 0x106: Error Log Page Entries
    UCHAR  NPSS;             // 0x107: Number of Power States Support
    UCHAR  AVSCC;            // 0x108: Admin Vendor Specific Command Config
    UCHAR  APSTA;            // 0x109: Autonomous Power State Transition
    USHORT WCTEMP;           // 0x10A: Warning Composite Temp Threshold
    USHORT CCTEMP;           // 0x10C: Critical Composite Temp Threshold
    USHORT MTFA;             // 0x10E: Max Time for Firmware Activation
    ULONG  HMPRE;            // 0x110: Host Memory Buffer Preferred Size
    ULONG  HMMIN;            // 0x114: Host Memory Buffer Minimum Size
    UCHAR  TNVMCAP[16];      // 0x118: Total NVM Capacity
    UCHAR  UNVMCAP[16];      // 0x128: Unallocated NVM Capacity
    ULONG  RPMBS;            // 0x138: Replay Protected Memory Block
    USHORT EDSTT;            // 0x13C: Extended Device Self-test Time
    UCHAR  DSTO;             // 0x13E: Device Self-test Options
    UCHAR  FWUG;             // 0x13F: Firmware Update Granularity
    USHORT KAS;              // 0x140: Keep Alive Support
    USHORT HCTMA;            // 0x142: Host Controlled Thermal Management
    USHORT MNTMT;            // 0x144: Minimum Thermal Management Temp
    USHORT MXTMT;            // 0x146: Maximum Thermal Management Temp
    ULONG  SANICAP;          // 0x148: Sanitize Capabilities
    ULONG  HMMINDS;          // 0x14C: Host Memory Buffer Min Desc Entry Size
    USHORT HMMAXD;           // 0x150: Host Memory Max Descriptors Entries
    USHORT NSETIDMAX;        // 0x152: NVM Set Identifier Maximum
    USHORT ENDGIDMAX;        // 0x154: Endurance Group Identifier Maximum
    UCHAR  ANATT;            // 0x156: ANA Transition Time
    UCHAR  ANACAP;           // 0x157: Asymmetric Namespace Access Capabilities
    ULONG  ANAGRPMAX;        // 0x158: ANA Group Identifier Maximum
    ULONG  NANAGRPID;        // 0x15C: Number of ANA Group Identifiers
    ULONG  PELS;             // 0x160: Persistent Event Log Size
    UCHAR  Rsvd356[156];     // 0x164
    
    // Bytes 512-703: NVM Command Set Attributes
    UCHAR  SQES;             // 0x200: Submission Queue Entry Size
    UCHAR  CQES;             // 0x201: Completion Queue Entry Size
    USHORT MAXCMD;           // 0x202: Maximum Outstanding Commands
    ULONG  NN;               // 0x204: Number of Namespaces
    USHORT ONCS;             // 0x208: Optional NVM Command Support
    USHORT FUSES;            // 0x20A: Fused Operation Support
    UCHAR  FNA;              // 0x20C: Format NVM Attributes
    UCHAR  VWC;              // 0x20D: Volatile Write Cache
    USHORT AWUN;             // 0x20E: Atomic Write Unit Normal
    USHORT AWUPF;            // 0x210: Atomic Write Unit Power Fail
    UCHAR  ICSVSCC;          // 0x212: I/O Command Set Vendor Specific
    UCHAR  NWPC;             // 0x213: Namespace Write Protection Capabilities
    USHORT ACWU;             // 0x214: Atomic Compare & Write Unit
    USHORT OCFS;             // 0x216: Optional Copy Formats Supported
    ULONG  SGLS;             // 0x218: SGL Support
    ULONG  MNAN;             // 0x21C: Maximum Number of Allowed Namespaces
    UCHAR  MAXDNA[16];       // 0x220: Maximum Domain Namespace Attachments
    ULONG  MAXCNA;           // 0x230: Maximum I/O Controller Namespace Attachments
    UCHAR  Rsvd564[204];     // 0x234
    
    // Bytes 768-1023: Reserved
    UCHAR  Rsvd768[256];     // 0x300
    
    // Bytes 1024-2047: NVM Command Set I/O Command Set Specific
    UCHAR  Rsvd1024[1024];   // 0x400
    
    // Bytes 2048-3071: Power State Descriptors
    UCHAR  PSD[32][32];      // 0x800: Power State 0-31 Descriptors
    
    // Bytes 3072-4095: Vendor Specific
    UCHAR  VS[1024];         // 0xC00
    
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;

C_ASSERT(sizeof(NVME_IDENTIFY_CONTROLLER) == 4096);
```

### 构建 Identify Controller 数据

```c
NTSTATUS VnvmeBuildIdentifyController(
    _In_ PVNVME_CONTROLLER Controller,
    _Out_ PVOID Buffer)
{
    PNVME_IDENTIFY_CONTROLLER id = (PNVME_IDENTIFY_CONTROLLER)Buffer;
    
    // PCI ID
    id->VID = 0x1B36;       // Red Hat
    id->SSVID = 0x1B36;
    
    // 序列号 (ASCII, 空格填充)
    RtlCopyMemory(id->SN, "VNVME00000000001    ", 20);
    
    // 型号 (ASCII, 空格填充)
    RtlCopyMemory(id->MN, "Virtual NVMe SSD                        ", 40);
    
    // 固件版本
    RtlCopyMemory(id->FR, "1.0.0   ", 8);
    
    // 控制器 ID
    id->CNTLID = 1;
    
    // 版本 (对应 VS 寄存器)
    id->VER = (1 << 16) | (4 << 8) | 0;  // 1.4.0
    
    // 最大数据传输大小 (2^(12+MDTS) 字节, MDTS=5 表示 128KB)
    id->MDTS = 5;
    
    // 控制器类型: I/O Controller
    id->CNTRLTYPE = 1;
    
    // Admin 命令支持
    // Bit 0: Security Send/Receive
    // Bit 1: Format NVM
    // Bit 2: Firmware Commit/Download
    // Bit 3: Namespace Management
    id->OACS = 0;  // 不支持可选命令
    
    // 中止命令限制
    id->ACL = 3;
    
    // 异步事件请求限制
    id->AERL = 3;
    
    // SQ/CQ 条目大小
    id->SQES = (6 << 4) | 6;  // 最小/最大都是 64 字节 (2^6)
    id->CQES = (4 << 4) | 4;  // 最小/最大都是 16 字节 (2^4)
    
    // 命名空间数量
    id->NN = Controller->NamespaceCount;
    
    // NVM 命令支持
    // Bit 0: Compare
    // Bit 1: Write Uncorrectable
    // Bit 2: Dataset Management (TRIM)
    // Bit 3: Write Zeroes
    id->ONCS = 0x0004;  // 支持 Dataset Management
    
    // 挥发性写缓存
    id->VWC = 1;  // 存在挥发性写缓存
    
    return STATUS_SUCCESS;
}
```

### Identify Namespace 数据结构

```c
//
// Identify Namespace 数据 (4096 字节)
//
typedef struct _NVME_IDENTIFY_NAMESPACE {
    ULONG64 NSZE;            // 0x00: Namespace Size (in logical blocks)
    ULONG64 NCAP;            // 0x08: Namespace Capacity
    ULONG64 NUSE;            // 0x10: Namespace Utilization
    UCHAR   NSFEAT;          // 0x18: Namespace Features
    UCHAR   NLBAF;           // 0x19: Number of LBA Formats (0-based)
    UCHAR   FLBAS;           // 0x1A: Formatted LBA Size
    UCHAR   MC;              // 0x1B: Metadata Capabilities
    UCHAR   DPC;             // 0x1C: End-to-end Data Protection Capabilities
    UCHAR   DPS;             // 0x1D: End-to-end Data Protection Settings
    UCHAR   NMIC;            // 0x1E: Namespace Multi-path I/O
    UCHAR   RESCAP;          // 0x1F: Reservation Capabilities
    UCHAR   FPI;             // 0x20: Format Progress Indicator
    UCHAR   DLFEAT;          // 0x21: Deallocate Logical Block Features
    USHORT  NAWUN;           // 0x22: Namespace Atomic Write Unit Normal
    USHORT  NAWUPF;          // 0x24: Namespace Atomic Write Unit Power Fail
    USHORT  NACWU;           // 0x26: Namespace Atomic Compare & Write Unit
    USHORT  NABSN;           // 0x28: Namespace Atomic Boundary Size Normal
    USHORT  NABO;            // 0x2A: Namespace Atomic Boundary Offset
    USHORT  NABSPF;          // 0x2C: Namespace Atomic Boundary Size Power Fail
    USHORT  NOIOB;           // 0x2E: Namespace Optimal I/O Boundary
    UCHAR   NVMCAP[16];      // 0x30: NVM Capacity
    USHORT  NPWG;            // 0x40: Namespace Preferred Write Granularity
    USHORT  NPWA;            // 0x42: Namespace Preferred Write Alignment
    USHORT  NPDG;            // 0x44: Namespace Preferred Deallocate Granularity
    USHORT  NPDA;            // 0x46: Namespace Preferred Deallocate Alignment
    USHORT  NOWS;            // 0x48: Namespace Optimal Write Size
    UCHAR   Rsvd74[18];      // 0x4A
    ULONG   ANAGRPID;        // 0x5C: ANA Group Identifier
    UCHAR   Rsvd96[3];       // 0x60
    UCHAR   NSATTR;          // 0x63: Namespace Attributes
    USHORT  NVMSETID;        // 0x64: NVM Set Identifier
    USHORT  ENDGID;          // 0x66: Endurance Group Identifier
    UCHAR   NGUID[16];       // 0x68: Namespace GUID
    UCHAR   EUI64[8];        // 0x78: IEEE Extended Unique Identifier
    
    // LBA Format 支持 (最多 64 个)
    struct {
        USHORT MS;           // Metadata Size
        UCHAR  LBADS;        // LBA Data Size (2^n bytes)
        UCHAR  RP;           // Relative Performance
    } LBAF[64];              // 0x80
    
    UCHAR   Rsvd384[192];    // 0x180
    UCHAR   VS[3712];        // 0x240: Vendor Specific
    
} NVME_IDENTIFY_NAMESPACE, *PNVME_IDENTIFY_NAMESPACE;

C_ASSERT(sizeof(NVME_IDENTIFY_NAMESPACE) == 4096);
```

### 构建 Identify Namespace 数据

```c
NTSTATUS VnvmeBuildIdentifyNamespace(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG Nsid,
    _Out_ PVOID Buffer)
{
    PNVME_IDENTIFY_NAMESPACE id = (PNVME_IDENTIFY_NAMESPACE)Buffer;
    PVNVME_NAMESPACE ns;
    
    // 查找命名空间
    ns = VnvmeFindNamespace(Controller, Nsid);
    if (!ns) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 命名空间大小 (以逻辑块为单位)
    id->NSZE = ns->TotalBlocks;
    id->NCAP = ns->TotalBlocks;
    id->NUSE = ns->TotalBlocks;  // 或实际使用量
    
    // 命名空间特性
    id->NSFEAT = 0;
    
    // LBA 格式数量 (0-based, 只有 1 个格式)
    id->NLBAF = 0;
    
    // 使用的 LBA 格式 (格式 0)
    id->FLBAS = 0;
    
    // LBA 格式 0: 512 字节或 4KB
    if (ns->BlockSize == 512) {
        id->LBAF[0].LBADS = 9;   // 2^9 = 512
    } else {
        id->LBAF[0].LBADS = 12;  // 2^12 = 4096
    }
    id->LBAF[0].MS = 0;      // 无元数据
    id->LBAF[0].RP = 0;      // 最佳性能
    
    // 生成唯一 NGUID
    RtlCopyMemory(id->NGUID, &ns->Guid, sizeof(GUID));
    
    return STATUS_SUCCESS;
}
```

---

### Create I/O Completion Queue (Opcode 0x05)

```c
typedef union _NVME_CREATE_CQ_CDW10 {
    struct {
        ULONG QID   : 16;    // Queue Identifier
        ULONG QSIZE : 16;    // Queue Size (0-based)
    };
    ULONG AsUlong;
} NVME_CREATE_CQ_CDW10;

typedef union _NVME_CREATE_CQ_CDW11 {
    struct {
        ULONG PC  : 1;       // Physically Contiguous
        ULONG IEN : 1;       // Interrupts Enabled
        ULONG Rsvd: 14;
        ULONG IV  : 16;      // Interrupt Vector
    };
    ULONG AsUlong;
} NVME_CREATE_CQ_CDW11;

NTSTATUS VnvmeProcessCreateIoCq(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    NVME_CREATE_CQ_CDW10 cdw10;
    NVME_CREATE_CQ_CDW11 cdw11;
    PVNVME_COMPLETION_QUEUE cq;
    
    cdw10.AsUlong = Cmd->CDW10;
    cdw11.AsUlong = Cmd->CDW11;
    
    // 验证参数
    if (cdw10.QID == 0 || cdw10.QID > Controller->MaxIoQueues) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 检查队列是否已存在
    if (VnvmeFindIoCQ(Controller, cdw10.QID)) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    
    // 分配 CQ 结构
    cq = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*cq), 'QCNV');
    if (!cq) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(cq, sizeof(*cq));
    
    cq->QueueId = cdw10.QID;
    cq->Size = cdw10.QSIZE + 1;
    cq->BaseAddr = Cmd->DPTR.PRP.PRP1;
    cq->InterruptEnabled = cdw11.IEN;
    cq->Vector = cdw11.IV;
    cq->Head = 0;
    cq->Tail = 0;
    cq->Phase = 1;
    
    // 映射 CQ 内存
    cq->VirtAddr = VnvmeMapPhysicalMemory(
        cq->BaseAddr, 
        cq->Size * sizeof(NVME_COMPLETION));
    
    if (!cq->VirtAddr) {
        ExFreePoolWithTag(cq, 'QCNV');
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 添加到队列列表
    ExInterlockedInsertTailList(&Controller->IoCqList, 
                                &cq->ListEntry,
                                &Controller->QueueLock);
    
    VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    return STATUS_SUCCESS;
}
```

### Create I/O Submission Queue (Opcode 0x01)

```c
typedef union _NVME_CREATE_SQ_CDW10 {
    struct {
        ULONG QID   : 16;    // Queue Identifier
        ULONG QSIZE : 16;    // Queue Size (0-based)
    };
    ULONG AsUlong;
} NVME_CREATE_SQ_CDW10;

typedef union _NVME_CREATE_SQ_CDW11 {
    struct {
        ULONG PC    : 1;     // Physically Contiguous
        ULONG QPRIO : 2;     // Queue Priority
        ULONG Rsvd  : 13;
        ULONG CQID  : 16;    // Completion Queue Identifier
    };
    ULONG AsUlong;
} NVME_CREATE_SQ_CDW11;

NTSTATUS VnvmeProcessCreateIoSq(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    NVME_CREATE_SQ_CDW10 cdw10;
    NVME_CREATE_SQ_CDW11 cdw11;
    PVNVME_SUBMISSION_QUEUE sq;
    PVNVME_COMPLETION_QUEUE cq;
    
    cdw10.AsUlong = Cmd->CDW10;
    cdw11.AsUlong = Cmd->CDW11;
    
    // 验证 SQ ID
    if (cdw10.QID == 0 || cdw10.QID > Controller->MaxIoQueues) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_QUEUE_ID, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 验证关联的 CQ 存在
    cq = VnvmeFindIoCQ(Controller, cdw11.CQID);
    if (!cq) {
        VnvmeSetCompletion(Completion, NVME_SCT_CMD_SPECIFIC,
                          NVME_SC_INVALID_CQ_ID, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 分配 SQ 结构
    sq = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*sq), 'QSNV');
    if (!sq) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(sq, sizeof(*sq));
    
    sq->QueueId = cdw10.QID;
    sq->Size = cdw10.QSIZE + 1;
    sq->BaseAddr = Cmd->DPTR.PRP.PRP1;
    sq->CQ = cq;
    sq->Head = 0;
    sq->Tail = 0;
    
    // 映射 SQ 内存
    sq->VirtAddr = VnvmeMapPhysicalMemory(
        sq->BaseAddr,
        sq->Size * sizeof(NVME_COMMAND));
    
    if (!sq->VirtAddr) {
        ExFreePoolWithTag(sq, 'QSNV');
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 添加到队列列表
    ExInterlockedInsertTailList(&Controller->IoSqList,
                                &sq->ListEntry,
                                &Controller->QueueLock);
    
    VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    return STATUS_SUCCESS;
}
```

---

## I/O 命令

### Read (Opcode 0x02)

```c
typedef union _NVME_READ_CDW12 {
    struct {
        ULONG NLB   : 16;    // Number of Logical Blocks (0-based)
        ULONG Rsvd  : 10;
        ULONG PRINFO: 4;     // Protection Information
        ULONG FUA   : 1;     // Force Unit Access
        ULONG LR    : 1;     // Limited Retry
    };
    ULONG AsUlong;
} NVME_READ_CDW12;

NTSTATUS VnvmeProcessRead(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    PVNVME_NAMESPACE ns;
    ULONG64 slba;          // Starting LBA
    ULONG nlb;             // Number of Logical Blocks
    ULONG64 offset;
    ULONG length;
    PVOID buffer;
    NTSTATUS status;
    
    // 获取命名空间
    ns = VnvmeFindNamespace(Controller, Cmd->NSID);
    if (!ns) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_NAMESPACE, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 解析命令参数
    slba = ((ULONG64)Cmd->CDW11 << 32) | Cmd->CDW10;
    nlb = (Cmd->CDW12 & 0xFFFF) + 1;
    
    offset = slba * ns->BlockSize;
    length = nlb * ns->BlockSize;
    
    // 边界检查
    if (slba + nlb > ns->TotalBlocks) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_LBA_OUT_OF_RANGE, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 分配临时缓冲区
    buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, length, 'RDNV');
    if (!buffer) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 从后端读取数据
    status = Controller->Backend->Operations->Read(
        Controller->Backend,
        ns->BackendOffset + offset,
        length,
        buffer);
    
    if (NT_SUCCESS(status)) {
        // 将数据写入 PRP
        status = VnvmeWriteToPrp(Controller, 
                                Cmd->DPTR.PRP.PRP1,
                                Cmd->DPTR.PRP.PRP2,
                                buffer, length);
    }
    
    ExFreePoolWithTag(buffer, 'RDNV');
    
    if (NT_SUCCESS(status)) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    } else {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
    }
    
    return status;
}
```

### Write (Opcode 0x01)

```c
NTSTATUS VnvmeProcessWrite(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    PVNVME_NAMESPACE ns;
    ULONG64 slba;
    ULONG nlb;
    ULONG64 offset;
    ULONG length;
    PVOID buffer;
    NTSTATUS status;
    
    ns = VnvmeFindNamespace(Controller, Cmd->NSID);
    if (!ns) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_NAMESPACE, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    slba = ((ULONG64)Cmd->CDW11 << 32) | Cmd->CDW10;
    nlb = (Cmd->CDW12 & 0xFFFF) + 1;
    
    offset = slba * ns->BlockSize;
    length = nlb * ns->BlockSize;
    
    if (slba + nlb > ns->TotalBlocks) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_LBA_OUT_OF_RANGE, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 只读检查
    if (ns->ReadOnly) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_WRITE_FAULT, 1);
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, length, 'WRNV');
    if (!buffer) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 从 PRP 读取数据
    status = VnvmeReadFromPrp(Controller,
                              Cmd->DPTR.PRP.PRP1,
                              Cmd->DPTR.PRP.PRP2,
                              buffer, length);
    
    if (NT_SUCCESS(status)) {
        // 写入后端
        status = Controller->Backend->Operations->Write(
            Controller->Backend,
            ns->BackendOffset + offset,
            length,
            buffer);
    }
    
    ExFreePoolWithTag(buffer, 'WRNV');
    
    if (NT_SUCCESS(status)) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    } else {
        VnvmeSetCompletion(Completion, NVME_SCT_MEDIA_ERROR,
                          NVME_SC_WRITE_FAULT, 0);
    }
    
    return status;
}
```

### Flush (Opcode 0x00)

```c
NTSTATUS VnvmeProcessFlush(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    NTSTATUS status;
    
    // Flush 后端
    if (Controller->Backend && Controller->Backend->Operations->Flush) {
        status = Controller->Backend->Operations->Flush(Controller->Backend);
    } else {
        status = STATUS_SUCCESS;
    }
    
    if (NT_SUCCESS(status)) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    } else {
        VnvmeSetCompletion(Completion, NVME_SCT_MEDIA_ERROR,
                          NVME_SC_INTERNAL_ERROR, 0);
    }
    
    return status;
}
```

---

## Admin 命令 (续)

### Get Features (Opcode 0x0A) / Set Features (Opcode 0x09)

stornvme.sys 初始化时会调用 Set Features 来配置 I/O 队列数量，这是必须支持的命令。

```c
// Feature Identifiers (FID)
#define NVME_FEATURE_ARBITRATION           0x01
#define NVME_FEATURE_POWER_MANAGEMENT      0x02
#define NVME_FEATURE_LBA_RANGE_TYPE        0x03
#define NVME_FEATURE_TEMPERATURE_THRESHOLD 0x04
#define NVME_FEATURE_ERROR_RECOVERY        0x05
#define NVME_FEATURE_VOLATILE_WRITE_CACHE  0x06
#define NVME_FEATURE_NUMBER_OF_QUEUES      0x07   // ★ 关键
#define NVME_FEATURE_INTERRUPT_COALESCING  0x08
#define NVME_FEATURE_INTERRUPT_VECTOR_CONFIG 0x09
#define NVME_FEATURE_WRITE_ATOMICITY       0x0A
#define NVME_FEATURE_ASYNC_EVENT_CONFIG    0x0B

typedef union _NVME_FEATURES_CDW10 {
    struct {
        ULONG FID   : 8;     // Feature Identifier
        ULONG SEL   : 3;     // Select (Get Features only)
        ULONG Rsvd  : 21;
    };
    ULONG AsUlong;
} NVME_FEATURES_CDW10;

//
// Set Features 处理 (Opcode 0x09)
//
NTSTATUS VnvmeProcessSetFeatures(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    NVME_FEATURES_CDW10 cdw10;
    ULONG cdw11 = Cmd->CDW11;
    
    cdw10.AsUlong = Cmd->CDW10;
    
    switch (cdw10.FID) {
    
    case NVME_FEATURE_NUMBER_OF_QUEUES:
        {
            // CDW11: 请求的 I/O 队列数量
            // Bits 15:0  = 请求的 I/O Submission Queue 数量 (0-based)
            // Bits 31:16 = 请求的 I/O Completion Queue 数量 (0-based)
            USHORT requestedSQ = (cdw11 & 0xFFFF);
            USHORT requestedCQ = ((cdw11 >> 16) & 0xFFFF);
            
            // 限制在我们支持的最大值
            USHORT allocatedSQ = min(requestedSQ, Controller->MaxIoQueues - 1);
            USHORT allocatedCQ = min(requestedCQ, Controller->MaxIoQueues - 1);
            
            // 保存分配的队列数
            Controller->AllocatedIoSqCount = allocatedSQ + 1;
            Controller->AllocatedIoCqCount = allocatedCQ + 1;
            
            // 完成条目 DW0 返回实际分配的数量
            Completion->DW0 = ((ULONG)allocatedCQ << 16) | allocatedSQ;
            
            VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        }
        break;
        
    case NVME_FEATURE_VOLATILE_WRITE_CACHE:
        {
            // CDW11 Bit 0 = Volatile Write Cache Enable
            Controller->WriteCache.Enabled = (cdw11 & 1) ? TRUE : FALSE;
            VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        }
        break;
        
    case NVME_FEATURE_ARBITRATION:
    case NVME_FEATURE_POWER_MANAGEMENT:
    case NVME_FEATURE_TEMPERATURE_THRESHOLD:
    case NVME_FEATURE_ERROR_RECOVERY:
    case NVME_FEATURE_INTERRUPT_COALESCING:
    case NVME_FEATURE_INTERRUPT_VECTOR_CONFIG:
    case NVME_FEATURE_WRITE_ATOMICITY:
    case NVME_FEATURE_ASYNC_EVENT_CONFIG:
        // 这些功能我们接受但不做实际处理
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        break;
        
    default:
        // 不支持的功能
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_FIELD, 1);
        return STATUS_NOT_SUPPORTED;
    }
    
    return STATUS_SUCCESS;
}

//
// Get Features 处理 (Opcode 0x0A)
//
NTSTATUS VnvmeProcessGetFeatures(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    NVME_FEATURES_CDW10 cdw10;
    
    cdw10.AsUlong = Cmd->CDW10;
    
    switch (cdw10.FID) {
    
    case NVME_FEATURE_NUMBER_OF_QUEUES:
        {
            // 返回已分配的队列数量
            USHORT sqCount = Controller->AllocatedIoSqCount - 1;  // 0-based
            USHORT cqCount = Controller->AllocatedIoCqCount - 1;
            Completion->DW0 = ((ULONG)cqCount << 16) | sqCount;
            VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        }
        break;
        
    case NVME_FEATURE_VOLATILE_WRITE_CACHE:
        Completion->DW0 = Controller->WriteCache.Enabled ? 1 : 0;
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        break;
        
    case NVME_FEATURE_ARBITRATION:
        Completion->DW0 = 0;  // 默认仲裁
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        break;
        
    case NVME_FEATURE_POWER_MANAGEMENT:
        Completion->DW0 = 0;  // 电源状态 0
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        break;
        
    case NVME_FEATURE_TEMPERATURE_THRESHOLD:
        Completion->DW0 = 0;  // 默认阈值
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        break;
        
    default:
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_FIELD, 1);
        return STATUS_NOT_SUPPORTED;
    }
    
    return STATUS_SUCCESS;
}
```

### Admin 命令分发

```c
//
// Admin 命令 Opcode 定义
//
#define NVME_ADMIN_OPC_DELETE_IO_SQ     0x00
#define NVME_ADMIN_OPC_CREATE_IO_SQ     0x01
#define NVME_ADMIN_OPC_GET_LOG_PAGE     0x02
#define NVME_ADMIN_OPC_DELETE_IO_CQ     0x04
#define NVME_ADMIN_OPC_CREATE_IO_CQ     0x05
#define NVME_ADMIN_OPC_IDENTIFY         0x06
#define NVME_ADMIN_OPC_ABORT            0x08
#define NVME_ADMIN_OPC_SET_FEATURES     0x09
#define NVME_ADMIN_OPC_GET_FEATURES     0x0A
#define NVME_ADMIN_OPC_ASYNC_EVENT_REQ  0x0C
#define NVME_ADMIN_OPC_NS_MANAGEMENT    0x0D
#define NVME_ADMIN_OPC_FW_COMMIT        0x10
#define NVME_ADMIN_OPC_FW_DOWNLOAD      0x11

//
// Admin 命令处理入口
//
NTSTATUS VnvmeProcessAdminCommand(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    UCHAR opcode = (UCHAR)(Cmd->CDW0.OPC);
    
    switch (opcode) {
    
    case NVME_ADMIN_OPC_IDENTIFY:
        return VnvmeProcessIdentify(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_CREATE_IO_CQ:
        return VnvmeProcessCreateIoCq(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_CREATE_IO_SQ:
        return VnvmeProcessCreateIoSq(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_DELETE_IO_SQ:
        return VnvmeProcessDeleteIoSq(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_DELETE_IO_CQ:
        return VnvmeProcessDeleteIoCq(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_SET_FEATURES:
        return VnvmeProcessSetFeatures(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_GET_FEATURES:
        return VnvmeProcessGetFeatures(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_GET_LOG_PAGE:
        return VnvmeProcessGetLogPage(Controller, Cmd, Completion);
        
    case NVME_ADMIN_OPC_ABORT:
        // Abort 命令 - 简单返回成功
        Completion->DW0 = 0;  // Abort 未找到命令
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        return STATUS_SUCCESS;
        
    case NVME_ADMIN_OPC_ASYNC_EVENT_REQ:
        // 异步事件 - 排队等待，暂不完成
        return VnvmeQueueAsyncEvent(Controller, Cmd);
        
    default:
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_OPCODE, 1);
        return STATUS_NOT_SUPPORTED;
    }
}
```

---

### Dataset Management (TRIM, Opcode 0x09)

```c
typedef struct _NVME_DSM_RANGE {
    ULONG ContextAttributes;
    ULONG LengthInBlocks;
    ULONG64 StartingLBA;
} NVME_DSM_RANGE, *PNVME_DSM_RANGE;

NTSTATUS VnvmeProcessDatasetManagement(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ PNVME_COMMAND Cmd,
    _Out_ PNVME_COMPLETION Completion)
{
    PVNVME_NAMESPACE ns;
    ULONG rangeCount;
    ULONG attributes;
    PNVME_DSM_RANGE ranges = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    
    ns = VnvmeFindNamespace(Controller, Cmd->NSID);
    if (!ns) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INVALID_NAMESPACE, 1);
        return STATUS_INVALID_PARAMETER;
    }
    
    rangeCount = (Cmd->CDW10 & 0xFF) + 1;
    attributes = Cmd->CDW11;
    
    // 检查是否是 Deallocate (TRIM)
    if (!(attributes & 0x04)) {
        // 不是 deallocate，忽略
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
        return STATUS_SUCCESS;
    }
    
    // 读取范围列表
    ULONG rangesSize = rangeCount * sizeof(NVME_DSM_RANGE);
    ranges = ExAllocatePool2(POOL_FLAG_NON_PAGED, rangesSize, 'DSNV');
    if (!ranges) {
        VnvmeSetCompletion(Completion, NVME_SCT_GENERIC,
                          NVME_SC_INTERNAL_ERROR, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    status = VnvmeReadFromPrp(Controller,
                              Cmd->DPTR.PRP.PRP1,
                              Cmd->DPTR.PRP.PRP2,
                              ranges, rangesSize);
    
    if (NT_SUCCESS(status)) {
        // 处理每个 TRIM 范围
        for (ULONG i = 0; i < rangeCount; i++) {
            if (ranges[i].LengthInBlocks == 0) continue;
            
            ULONG64 offset = ranges[i].StartingLBA * ns->BlockSize;
            ULONG64 length = (ULONG64)ranges[i].LengthInBlocks * ns->BlockSize;
            
            if (Controller->Backend->Operations->Trim) {
                Controller->Backend->Operations->Trim(
                    Controller->Backend,
                    ns->BackendOffset + offset,
                    length);
            }
        }
    }
    
    ExFreePoolWithTag(ranges, 'DSNV');
    
    VnvmeSetCompletion(Completion, NVME_SCT_GENERIC, NVME_SC_SUCCESS, 0);
    return STATUS_SUCCESS;
}
```

---

## PRP 处理

### PRP (Physical Region Page) 解析

```c
//
// 从 PRP 读取数据
//
NTSTATUS VnvmeReadFromPrp(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG64 Prp1,
    _In_ ULONG64 Prp2,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    ULONG pageSize = 4096;  // 假设 4KB 页
    ULONG64 offset = Prp1 & (pageSize - 1);
    ULONG firstPageLen = min(pageSize - (ULONG)offset, Length);
    ULONG remaining = Length - firstPageLen;
    PUCHAR dst = (PUCHAR)Buffer;
    NTSTATUS status;
    
    // 读取第一页
    status = VnvmeCopyFromPhysical(Prp1, dst, firstPageLen);
    if (!NT_SUCCESS(status)) return status;
    
    dst += firstPageLen;
    
    if (remaining == 0) {
        return STATUS_SUCCESS;
    }
    
    // 如果剩余数据 <= 1 页，直接使用 PRP2
    if (remaining <= pageSize) {
        return VnvmeCopyFromPhysical(Prp2, dst, remaining);
    }
    
    // 否则 PRP2 指向 PRP List
    ULONG64 prpList[512];  // 最多 4KB / 8 = 512 个条目
    ULONG prpCount = (remaining + pageSize - 1) / pageSize;
    
    status = VnvmeCopyFromPhysical(Prp2, prpList, prpCount * sizeof(ULONG64));
    if (!NT_SUCCESS(status)) return status;
    
    for (ULONG i = 0; i < prpCount && remaining > 0; i++) {
        ULONG copyLen = min(pageSize, remaining);
        status = VnvmeCopyFromPhysical(prpList[i], dst, copyLen);
        if (!NT_SUCCESS(status)) return status;
        
        dst += copyLen;
        remaining -= copyLen;
    }
    
    return STATUS_SUCCESS;
}

//
// 向 PRP 写入数据
//
NTSTATUS VnvmeWriteToPrp(
    _In_ PVNVME_CONTROLLER Controller,
    _In_ ULONG64 Prp1,
    _In_ ULONG64 Prp2,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    // 类似 ReadFromPrp，但方向相反
    // ...
}

//
// 从物理地址复制数据
//
NTSTATUS VnvmeCopyFromPhysical(
    _In_ ULONG64 PhysAddr,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PHYSICAL_ADDRESS physAddr;
    PVOID mapped;
    
    physAddr.QuadPart = PhysAddr;
    
    mapped = MmMapIoSpace(physAddr, Length, MmNonCached);
    if (!mapped) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlCopyMemory(Buffer, mapped, Length);
    
    MmUnmapIoSpace(mapped, Length);
    
    return STATUS_SUCCESS;
}
```

---

## 状态码定义

```c
// Status Code Type (SCT)
#define NVME_SCT_GENERIC        0x0
#define NVME_SCT_CMD_SPECIFIC   0x1
#define NVME_SCT_MEDIA_ERROR    0x2
#define NVME_SCT_PATH_RELATED   0x3
#define NVME_SCT_VENDOR         0x7

// Generic Status Codes
#define NVME_SC_SUCCESS                 0x00
#define NVME_SC_INVALID_OPCODE          0x01
#define NVME_SC_INVALID_FIELD           0x02
#define NVME_SC_CMDID_CONFLICT          0x03
#define NVME_SC_DATA_TRANSFER_ERROR     0x04
#define NVME_SC_POWER_LOSS              0x05
#define NVME_SC_INTERNAL_ERROR          0x06
#define NVME_SC_ABORT_REQ               0x07
#define NVME_SC_ABORT_QUEUE             0x08
#define NVME_SC_FUSED_FAIL              0x09
#define NVME_SC_FUSED_MISSING           0x0A
#define NVME_SC_INVALID_NAMESPACE       0x0B
#define NVME_SC_CMD_SEQ_ERROR           0x0C
#define NVME_SC_LBA_OUT_OF_RANGE        0x80
#define NVME_SC_CAPACITY_EXCEEDED       0x81
#define NVME_SC_NAMESPACE_NOT_READY     0x82

// Command Specific Status Codes
#define NVME_SC_INVALID_CQ_ID           0x00
#define NVME_SC_INVALID_QUEUE_ID        0x01
#define NVME_SC_MAX_QUEUE_SIZE_EXCEEDED 0x02
#define NVME_SC_ABORT_LIMIT_EXCEEDED    0x03
#define NVME_SC_ASYNC_LIMIT_EXCEEDED    0x05
#define NVME_SC_INVALID_FIRMWARE_SLOT   0x06
#define NVME_SC_INVALID_FIRMWARE_IMAGE  0x07
#define NVME_SC_INVALID_INTERRUPT_VECTOR 0x08

// Media and Data Integrity Errors
#define NVME_SC_WRITE_FAULT             0x80
#define NVME_SC_READ_ERROR              0x81
#define NVME_SC_GUARD_CHECK             0x82
#define NVME_SC_APPTAG_CHECK            0x83
#define NVME_SC_REFTAG_CHECK            0x84
#define NVME_SC_COMPARE_FAILURE         0x85
#define NVME_SC_ACCESS_DENIED           0x86

//
// 设置完成条目
//
VOID VnvmeSetCompletion(
    _Out_ PNVME_COMPLETION Completion,
    _In_ UCHAR StatusCodeType,
    _In_ UCHAR StatusCode,
    _In_ BOOLEAN DoNotRetry)
{
    Completion->Status = NVME_MAKE_STATUS(StatusCodeType, StatusCode, DoNotRetry);
}
```

---

## 参考资源

- [NVM Express Base Specification 2.0](https://nvmexpress.org/specifications/)
- [NVM Express Command Set Specification](https://nvmexpress.org/specifications/)
