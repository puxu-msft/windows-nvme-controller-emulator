# 核心数据结构

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

```c
typedef struct _NVME_IDENTIFY_CONTROLLER {
    // Controller Capabilities and Features
    UINT16  VID;                // PCI Vendor ID
    UINT16  SSVID;              // PCI Subsystem Vendor ID
    CHAR    SN[20];             // Serial Number (ASCII)
    CHAR    MN[40];             // Model Number (ASCII)
    CHAR    FR[8];              // Firmware Revision (ASCII)
    UINT8   RAB;                // Recommended Arbitration Burst
    UINT8   IEEE[3];            // IEEE OUI Identifier
    UINT8   CMIC;               // Controller Multi-Path I/O
    UINT8   MDTS;               // Maximum Data Transfer Size (2^n * MPS)
    UINT16  CNTLID;             // Controller ID
    UINT32  VER;                // Version
    UINT32  RTD3R;              // RTD3 Resume Latency
    UINT32  RTD3E;              // RTD3 Entry Latency
    UINT32  OAES;               // Optional Async Events Supported
    UINT32  CTRATT;             // Controller Attributes
    UINT8   Reserved1[156];
    
    // Admin Command Set Attributes
    UINT16  OACS;               // Optional Admin Command Support
    UINT8   ACL;                // Abort Command Limit
    UINT8   AERL;               // Async Event Request Limit
    UINT8   FRMW;               // Firmware Updates
    UINT8   LPA;                // Log Page Attributes
    UINT8   ELPE;               // Error Log Page Entries
    UINT8   NPSS;               // Number of Power States Support
    UINT8   AVSCC;              // Admin Vendor Specific Command Config
    UINT8   APSTA;              // Autonomous Power State Transition
    UINT16  WCTEMP;             // Warning Composite Temperature Threshold
    UINT16  CCTEMP;             // Critical Composite Temperature Threshold
    UINT8   Reserved2[242];
    
    // NVM Command Set Attributes
    UINT8   SQES;               // Submission Queue Entry Size
    UINT8   CQES;               // Completion Queue Entry Size
    UINT16  MAXCMD;             // Maximum Outstanding Commands
    UINT32  NN;                 // Number of Namespaces
    UINT16  ONCS;               // Optional NVM Command Support
    UINT16  FUSES;              // Fused Operation Support
    UINT8   FNA;                // Format NVM Attributes
    UINT8   VWC;                // Volatile Write Cache
    UINT16  AWUN;               // Atomic Write Unit Normal
    UINT16  AWUPF;              // Atomic Write Unit Power Fail
    UINT8   Reserved3[174];
    
    // I/O Command Set Attributes
    UINT8   Reserved4[1344];
    
    // Power State Descriptors
    UINT8   PSD[1024];          // Power State Descriptors (32 x 32 bytes)
    
    // Vendor Specific
    UINT8   VS[1024];           // Vendor Specific
    
} NVME_IDENTIFY_CONTROLLER, *PNVME_IDENTIFY_CONTROLLER;
```

## Identify Namespace 数据结构 (4096 bytes)

```c
typedef struct _NVME_IDENTIFY_NAMESPACE {
    UINT64  NSZE;               // Namespace Size (in logical blocks)
    UINT64  NCAP;               // Namespace Capacity
    UINT64  NUSE;               // Namespace Utilization
    UINT8   NSFEAT;             // Namespace Features
    UINT8   NLBAF;              // Number of LBA Formats (0's based)
    UINT8   FLBAS;              // Formatted LBA Size
    UINT8   MC;                 // Metadata Capabilities
    UINT8   DPC;                // End-to-end Data Protection Capabilities
    UINT8   DPS;                // End-to-end Data Protection Settings
    UINT8   NMIC;               // Namespace Multi-path I/O
    UINT8   RESCAP;             // Reservation Capabilities
    UINT8   FPI;                // Format Progress Indicator
    UINT8   Reserved1[3];
    UINT16  NAWUN;              // Namespace Atomic Write Unit Normal
    UINT16  NAWUPF;             // Namespace Atomic Write Unit Power Fail
    UINT8   Reserved2[92];
    UINT8   NGUID[16];          // Namespace GUID
    UINT8   EUI64[8];           // IEEE Extended Unique Identifier
    
    // LBA Format Support (up to 16 formats)
    struct {
        UINT32 MS       : 16;   // Metadata Size
        UINT32 LBADS    : 8;    // LBA Data Size (2^n bytes)
        UINT32 RP       : 2;    // Relative Performance
        UINT32 Reserved : 6;
    } LBAF[16];
    
    UINT8   Reserved3[192];
    UINT8   VS[3712];           // Vendor Specific
    
} NVME_IDENTIFY_NAMESPACE, *PNVME_IDENTIFY_NAMESPACE;
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
