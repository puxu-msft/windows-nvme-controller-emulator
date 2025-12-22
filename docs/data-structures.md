# 核心数据结构

## 编译器和平台注意事项

```c
// 所有 NVMe 规范结构必须使用 1 字节对齐
#pragma pack(push, 1)

// NVMe 使用小端字节序 (Little-Endian)
// Windows x64 也是小端，无需额外转换
// 如果需要支持大端平台，使用以下宏:
// #define NVME_TO_LE16(x)  (x)
// #define NVME_TO_LE32(x)  (x)
// #define NVME_TO_LE64(x)  (x)

// 位域在 MSVC 中按从低位到高位的顺序排列
// 这与 NVMe 规范的位定义一致
```

## 控制器上下文

```c
typedef struct _VNVME_CONTROLLER_CONTEXT {
    // 控制器标识
    UINT32 ControllerId;
    
    // 控制器状态
    VNVME_CONTROLLER_STATE State;
    
    // 控制器能力
    NVME_CAP Capabilities;
    
    // 控制器配置
    NVME_CC Configuration;
    
    // 控制器状态寄存器
    NVME_CSTS Status;
    
    // Admin 队列
    PVNVME_QUEUE AdminSQ;
    PVNVME_QUEUE AdminCQ;
    
    // I/O 队列数组
    PVNVME_QUEUE IoSQs[VNVME_MAX_IO_QUEUES];
    PVNVME_QUEUE IoCQs[VNVME_MAX_IO_QUEUES];
    UINT32 IoQueueCount;
    
    // 命名空间列表
    PVNVME_NAMESPACE Namespaces[VNVME_MAX_NAMESPACES];
    UINT32 NamespaceCount;
    
    // 存储后端
    PVNVME_BACKEND Backend;
    
    // 同步锁
    KSPIN_LOCK Lock;
    
} VNVME_CONTROLLER_CONTEXT, *PVNVME_CONTROLLER_CONTEXT;
```

## 队列结构

```c
typedef struct _VNVME_QUEUE {
    UINT16 QueueId;
    UINT16 QueueSize;       // 队列深度
    UINT16 Head;            // 队首指针
    UINT16 Tail;            // 队尾指针
    BOOLEAN IsSubmissionQueue;
    
    // 队列内存
    PVOID QueueBuffer;
    SIZE_T QueueBufferSize;
    
    // 关联队列 (SQ关联CQ)
    struct _VNVME_QUEUE* AssociatedQueue;
    
    // 完成队列特有
    BOOLEAN Phase;          // 阶段位
    
    KSPIN_LOCK Lock;
} VNVME_QUEUE, *PVNVME_QUEUE;
```

## 命名空间结构

```c
typedef struct _VNVME_NAMESPACE {
    UINT32 NamespaceId;     // NSID
    UINT64 BlockCount;      // 块数量
    UINT32 BlockSize;       // 块大小 (通常512或4096)
    UINT64 TotalSize;       // 总容量
    
    // 元数据
    BOOLEAN IsActive;
    UINT8 FlbaSetting;      // 格式化LBA设置
    
    // 后端存储偏移
    UINT64 BackendOffset;
    
} VNVME_NAMESPACE, *PVNVME_NAMESPACE;
```

## 存储后端结构

```c
typedef struct _VNVME_BACKEND {
    VNVME_BACKEND_TYPE Type;    // MEMORY / FILE
    UINT64 TotalSize;
    
    union {
        // 内存后端
        struct {
            PVOID Buffer;
            SIZE_T BufferSize;
        } Memory;
        
        // 文件后端
        struct {
            HANDLE FileHandle;
            UNICODE_STRING FilePath;
        } File;
    };
    
    // 后端操作函数表
    PVNVME_BACKEND_OPS Operations;
    
} VNVME_BACKEND, *PVNVME_BACKEND;
```

## 后端操作函数表

```c
typedef struct _VNVME_BACKEND_OPS {
    // 初始化后端
    NTSTATUS (*Initialize)(
        PVNVME_BACKEND Backend,
        PVNVME_BACKEND_CONFIG Config);
    
    // 读取数据
    NTSTATUS (*Read)(
        PVNVME_BACKEND Backend,
        UINT64 Offset,
        UINT32 Length,
        PVOID Buffer);
    
    // 写入数据
    NTSTATUS (*Write)(
        PVNVME_BACKEND Backend,
        UINT64 Offset,
        UINT32 Length,
        PVOID Buffer);
    
    // 刷新到持久存储
    NTSTATUS (*Flush)(
        PVNVME_BACKEND Backend);
    
    // 关闭后端
    VOID (*Shutdown)(
        PVNVME_BACKEND Backend);
    
} VNVME_BACKEND_OPS, *PVNVME_BACKEND_OPS;

// 后端类型枚举
typedef enum _VNVME_BACKEND_TYPE {
    VNVME_BACKEND_MEMORY = 0,   // 内存后端 (非持久)
    VNVME_BACKEND_FILE   = 1,   // 文件后端 (持久)
    VNVME_BACKEND_VHD    = 2,   // VHD 后端 (持久)
} VNVME_BACKEND_TYPE;
```

## NVMe 控制器寄存器结构

```c
// Controller Capabilities (CAP) - 64-bit
typedef union _NVME_CAP {
    struct {
        UINT64 MQES     : 16;   // Maximum Queue Entries Supported (0's based)
        UINT64 CQR      : 1;    // Contiguous Queues Required
        UINT64 AMS      : 2;    // Arbitration Mechanism Supported
        UINT64 Reserved1: 5;
        UINT64 TO       : 8;    // Timeout (in 500ms units)
        UINT64 DSTRD    : 4;    // Doorbell Stride (2^(2+DSTRD) bytes)
        UINT64 NSSRS    : 1;    // NVM Subsystem Reset Supported
        UINT64 CSS      : 8;    // Command Sets Supported
        UINT64 Reserved2: 3;
        UINT64 MPSMIN   : 4;    // Memory Page Size Minimum (2^(12+MPSMIN))
        UINT64 MPSMAX   : 4;    // Memory Page Size Maximum (2^(12+MPSMAX))
        UINT64 Reserved3: 8;
    };
    UINT64 AsUint64;
} NVME_CAP, *PNVME_CAP;

// Controller Configuration (CC) - 32-bit
typedef union _NVME_CC {
    struct {
        UINT32 EN       : 1;    // Enable
        UINT32 Reserved1: 3;
        UINT32 CSS      : 3;    // I/O Command Set Selected
        UINT32 MPS      : 4;    // Memory Page Size (2^(12+MPS))
        UINT32 AMS      : 3;    // Arbitration Mechanism Selected
        UINT32 SHN      : 2;    // Shutdown Notification
        UINT32 IOSQES   : 4;    // I/O Submission Queue Entry Size (2^n)
        UINT32 IOCQES   : 4;    // I/O Completion Queue Entry Size (2^n)
        UINT32 Reserved2: 8;
    };
    UINT32 AsUint32;
} NVME_CC, *PNVME_CC;

// Controller Status (CSTS) - 32-bit
typedef union _NVME_CSTS {
    struct {
        UINT32 RDY      : 1;    // Ready
        UINT32 CFS      : 1;    // Controller Fatal Status
        UINT32 SHST     : 2;    // Shutdown Status
        UINT32 NSSRO    : 1;    // NVM Subsystem Reset Occurred
        UINT32 PP       : 1;    // Processing Paused
        UINT32 Reserved : 26;
    };
    UINT32 AsUint32;
} NVME_CSTS, *PNVME_CSTS;

// Admin Queue Attributes (AQA) - 32-bit
typedef union _NVME_AQA {
    struct {
        UINT32 ASQS     : 12;   // Admin Submission Queue Size (0's based)
        UINT32 Reserved1: 4;
        UINT32 ACQS     : 12;   // Admin Completion Queue Size (0's based)
        UINT32 Reserved2: 4;
    };
    UINT32 AsUint32;
} NVME_AQA, *PNVME_AQA;
```

## Identify Controller 数据结构 (4096 bytes)

**注意**: 以下结构的字段偏移必须严格按照 NVMe 规范。Reserved 字段大小已根据规范修正。

```c
#pragma pack(push, 1)

typedef struct _NVME_IDENTIFY_CONTROLLER {
    // ============ Controller Capabilities and Features (Bytes 0-255) ============
    UINT16  VID;                // [0:1]   PCI Vendor ID
    UINT16  SSVID;              // [2:3]   PCI Subsystem Vendor ID
    CHAR    SN[20];             // [4:23]  Serial Number (ASCII, 右填充空格)
    CHAR    MN[40];             // [24:63] Model Number (ASCII, 右填充空格)
    CHAR    FR[8];              // [64:71] Firmware Revision (ASCII)
    UINT8   RAB;                // [72]    Recommended Arbitration Burst
    UINT8   IEEE[3];            // [73:75] IEEE OUI Identifier
    UINT8   CMIC;               // [76]    Controller Multi-Path I/O
    UINT8   MDTS;               // [77]    Maximum Data Transfer Size (2^n * MPS)
    UINT16  CNTLID;             // [78:79] Controller ID
    UINT32  VER;                // [80:83] Version
    UINT32  RTD3R;              // [84:87] RTD3 Resume Latency
    UINT32  RTD3E;              // [88:91] RTD3 Entry Latency
    UINT32  OAES;               // [92:95] Optional Async Events Supported
    UINT32  CTRATT;             // [96:99] Controller Attributes
    UINT8   Reserved1[12];      // [100:111] 保留
    UINT8   FGUID[16];          // [112:127] FRU GUID
    UINT8   Reserved2[128];     // [128:255] 保留
    
    // ============ Admin Command Set Attributes (Bytes 256-511) ============
    UINT16  OACS;               // [256:257] Optional Admin Command Support
    UINT8   ACL;                // [258]    Abort Command Limit
    UINT8   AERL;               // [259]    Async Event Request Limit
    UINT8   FRMW;               // [260]    Firmware Updates
    UINT8   LPA;                // [261]    Log Page Attributes
    UINT8   ELPE;               // [262]    Error Log Page Entries
    UINT8   NPSS;               // [263]    Number of Power States Support
    UINT8   AVSCC;              // [264]    Admin Vendor Specific Command Config
    UINT8   APSTA;              // [265]    Autonomous Power State Transition
    UINT16  WCTEMP;             // [266:267] Warning Composite Temperature Threshold
    UINT16  CCTEMP;             // [268:269] Critical Composite Temperature Threshold
    UINT16  MTFA;               // [270:271] Maximum Time for Firmware Activation
    UINT32  HMPRE;              // [272:275] Host Memory Buffer Preferred Size
    UINT32  HMMIN;              // [276:279] Host Memory Buffer Minimum Size
    UINT8   TNVMCAP[16];        // [280:295] Total NVM Capacity
    UINT8   UNVMCAP[16];        // [296:311] Unallocated NVM Capacity
    UINT32  RPMBS;              // [312:315] Replay Protected Memory Block Support
    UINT16  EDSTT;              // [316:317] Extended Device Self-test Time
    UINT8   DSTO;               // [318]    Device Self-test Options
    UINT8   FWUG;               // [319]    Firmware Update Granularity
    UINT16  KAS;                // [320:321] Keep Alive Support
    UINT16  HCTMA;              // [322:323] Host Controlled Thermal Management
    UINT16  MNTMT;              // [324:325] Minimum Thermal Management Temperature
    UINT16  MXTMT;              // [326:327] Maximum Thermal Management Temperature
    UINT32  SANICAP;            // [328:331] Sanitize Capabilities
    UINT8   Reserved3[180];     // [332:511] 保留
    
    // ============ NVM Command Set Attributes (Bytes 512-767) ============
    UINT8   SQES;               // [512]    Submission Queue Entry Size (min/max)
    UINT8   CQES;               // [513]    Completion Queue Entry Size (min/max)
    UINT16  MAXCMD;             // [514:515] Maximum Outstanding Commands
    UINT32  NN;                 // [516:519] Number of Namespaces
    UINT16  ONCS;               // [520:521] Optional NVM Command Support
    UINT16  FUSES;              // [522:523] Fused Operation Support
    UINT8   FNA;                // [524]    Format NVM Attributes
    UINT8   VWC;                // [525]    Volatile Write Cache
    UINT16  AWUN;               // [526:527] Atomic Write Unit Normal
    UINT16  AWUPF;              // [528:529] Atomic Write Unit Power Fail
    UINT8   NVSCC;              // [530]    NVM Vendor Specific Command Config
    UINT8   Reserved4;          // [531]    保留
    UINT16  ACWU;               // [532:533] Atomic Compare & Write Unit
    UINT8   Reserved5[2];       // [534:535] 保留
    UINT32  SGLS;               // [536:539] SGL Support
    UINT8   Reserved6[228];     // [540:767] 保留
    
    // ============ I/O Command Set Attributes (Bytes 768-2047) ============
    UINT8   Reserved7[1280];    // [768:2047] 保留 (NVMe 1.4+)
    
    // ============ Power State Descriptors (Bytes 2048-3071) ============
    UINT8   PSD[1024];          // [2048:3071] 32 x 32-byte Power State Descriptors
    
    // ============ Vendor Specific (Bytes 3072-4095) ============
    UINT8   VS[1024];           // [3072:4095] Vendor Specific
    
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;

// 静态断言确保结构大小正确
C_ASSERT(sizeof(NVME_IDENTIFY_CONTROLLER) == 4096);

#pragma pack(pop)
```

## Identify Namespace 数据结构 (4096 bytes)

```c
#pragma pack(push, 1)

typedef struct _NVME_IDENTIFY_NAMESPACE {
    UINT64  NSZE;               // [0:7]   Namespace Size (in logical blocks)
    UINT64  NCAP;               // [8:15]  Namespace Capacity
    UINT64  NUSE;               // [16:23] Namespace Utilization
    UINT8   NSFEAT;             // [24]    Namespace Features
    UINT8   NLBAF;              // [25]    Number of LBA Formats (0's based)
    UINT8   FLBAS;              // [26]    Formatted LBA Size
    UINT8   MC;                 // [27]    Metadata Capabilities
    UINT8   DPC;                // [28]    End-to-end Data Protection Capabilities
    UINT8   DPS;                // [29]    End-to-end Data Protection Settings
    UINT8   NMIC;               // [30]    Namespace Multi-path I/O
    UINT8   RESCAP;             // [31]    Reservation Capabilities
    UINT8   FPI;                // [32]    Format Progress Indicator
    UINT8   DLFEAT;             // [33]    Deallocate Logical Block Features
    UINT16  NAWUN;              // [34:35] Namespace Atomic Write Unit Normal
    UINT16  NAWUPF;             // [36:37] Namespace Atomic Write Unit Power Fail
    UINT16  NACWU;              // [38:39] Namespace Atomic Compare & Write Unit
    UINT16  NABSN;              // [40:41] Namespace Atomic Boundary Size Normal
    UINT16  NABO;               // [42:43] Namespace Atomic Boundary Offset
    UINT16  NABSPF;             // [44:45] Namespace Atomic Boundary Size Power Fail
    UINT16  NOIOB;              // [46:47] Namespace Optimal I/O Boundary
    UINT8   NVMCAP[16];         // [48:63] NVM Capacity
    UINT8   Reserved1[40];      // [64:103] 保留
    UINT8   NGUID[16];          // [104:119] Namespace GUID
    UINT8   EUI64[8];           // [120:127] IEEE Extended Unique Identifier
    
    // LBA Format Support (up to 16 formats)
    struct {
        UINT32 MS       : 16;   // Metadata Size
        UINT32 LBADS    : 8;    // LBA Data Size (2^n bytes)
        UINT32 RP       : 2;    // Relative Performance (00=Best, 01=Better, 10=Good, 11=Degraded)
        UINT32 Reserved : 6;
    } LBAF[16];                 // [128:191] LBA 格式 0-15
    
    UINT8   Reserved2[192];     // [192:383] 保留
    UINT8   VS[3712];           // [384:4095] Vendor Specific
    
} NVME_IDENTIFY_NAMESPACE, *PNVME_IDENTIFY_NAMESPACE;

C_ASSERT(sizeof(NVME_IDENTIFY_NAMESPACE) == 4096);

#pragma pack(pop)
```

## Submission Queue Entry (SQE) - 64 bytes

```c
#pragma pack(push, 1)

typedef struct _NVME_SQE {
    // Command Dword 0
    union {
        struct {
            UINT32 OPC      : 8;    // Opcode
            UINT32 FUSE     : 2;    // Fused Operation (00=Normal)
            UINT32 Reserved : 4;
            UINT32 PSDT     : 2;    // PRP or SGL (00=PRP)
            UINT32 CID      : 16;   // Command Identifier
        };
        UINT32 CDW0;
    };
    
    UINT32 NSID;                // Namespace Identifier
    UINT32 CDW2;                // Command Dword 2
    UINT32 CDW3;                // Command Dword 3
    UINT64 MPTR;                // Metadata Pointer
    UINT64 PRP1;                // PRP Entry 1 / SGL Entry 1
    UINT64 PRP2;                // PRP Entry 2 / SGL Entry 2
    UINT32 CDW10;               // Command Dword 10 (command specific)
    UINT32 CDW11;               // Command Dword 11 (command specific)
    UINT32 CDW12;               // Command Dword 12 (command specific)
    UINT32 CDW13;               // Command Dword 13 (command specific)
    UINT32 CDW14;               // Command Dword 14 (command specific)
    UINT32 CDW15;               // Command Dword 15 (command specific)
    
} NVME_SQE, *PNVME_SQE;

C_ASSERT(sizeof(NVME_SQE) == 64);

#pragma pack(pop)
```

## Completion Queue Entry (CQE) - 16 bytes

```c
#pragma pack(push, 1)

typedef struct _NVME_CQE {
    UINT32 DW0;                 // Command Specific Result
    UINT32 DW1;                 // Reserved
    UINT16 SQHD;                // SQ Head Pointer
    UINT16 SQID;                // SQ Identifier
    UINT16 CID;                 // Command Identifier
    union {
        struct {
            UINT16 P        : 1;    // Phase Tag
            UINT16 SC       : 8;    // Status Code
            UINT16 SCT      : 3;    // Status Code Type
            UINT16 Reserved : 2;
            UINT16 M        : 1;    // More
            UINT16 DNR      : 1;    // Do Not Retry
        };
        UINT16 Status;
    };
    
} NVME_CQE, *PNVME_CQE;

C_ASSERT(sizeof(NVME_CQE) == 16);

#pragma pack(pop)
```

## 常量定义

```c
#define VNVME_MAX_IO_QUEUES         64
#define VNVME_MAX_NAMESPACES        16
#define VNVME_MAX_QUEUE_DEPTH       1024
#define VNVME_ADMIN_QUEUE_ID        0
#define VNVME_DEFAULT_BLOCK_SIZE    512
#define VNVME_4K_BLOCK_SIZE         4096
#define VNVME_SQE_SIZE              64      // Submission Queue Entry size
#define VNVME_CQE_SIZE              16      // Completion Queue Entry size
#define VNVME_IDENTIFY_DATA_SIZE    4096    // Identify command data size

// NVMe 版本号 (1.4.0)
#define VNVME_VERSION_MAJOR         1
#define VNVME_VERSION_MINOR         4
#define VNVME_VERSION_TERTIARY      0
#define VNVME_VERSION               ((VNVME_VERSION_MAJOR << 16) | \
                                     (VNVME_VERSION_MINOR << 8) | \
                                     VNVME_VERSION_TERTIARY)

// 控制器状态枚举
typedef enum _VNVME_CONTROLLER_STATE {
    VNVME_CTRL_STATE_DISABLED = 0,
    VNVME_CTRL_STATE_ENABLING,
    VNVME_CTRL_STATE_ENABLED,
    VNVME_CTRL_STATE_DISABLING,
    VNVME_CTRL_STATE_FATAL_ERROR,
} VNVME_CONTROLLER_STATE;
```
