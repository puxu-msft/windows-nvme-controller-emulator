/**
 * @file nvme_spec.h
 * @brief NVMe 规范定义
 * 
 * 本文件定义 NVMe 1.4 规范中的关键数据结构和常量。
 * 参考: NVM Express Base Specification, Revision 1.4
 */

#ifndef _NVME_SPEC_H_
#define _NVME_SPEC_H_

#ifdef _KERNEL_MODE
#include <ntddk.h>
#else
#include <stdint.h>
typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
#endif

#pragma pack(push, 1)

/*===========================================================================
 * NVMe 控制器寄存器 (BAR0)
 *===========================================================================*/

/**
 * @brief 控制器能力寄存器 (CAP) - 偏移 0x00, 64 位
 */
typedef union _NVME_CAP {
    struct {
        UINT64 MQES     : 16;   // [15:0]  Maximum Queue Entries Supported (0's based)
        UINT64 CQR      : 1;    // [16]    Contiguous Queues Required
        UINT64 AMS      : 2;    // [18:17] Arbitration Mechanism Supported
        UINT64 Reserved1: 5;    // [23:19]
        UINT64 TO       : 8;    // [31:24] Timeout (500ms units)
        UINT64 DSTRD    : 4;    // [35:32] Doorbell Stride (2^(2+DSTRD))
        UINT64 NSSRS    : 1;    // [36]    NVM Subsystem Reset Supported
        UINT64 CSS      : 8;    // [44:37] Command Sets Supported
        UINT64 BPS      : 1;    // [45]    Boot Partition Support
        UINT64 Reserved2: 2;    // [47:46]
        UINT64 MPSMIN   : 4;    // [51:48] Memory Page Size Minimum (2^(12+MPSMIN))
        UINT64 MPSMAX   : 4;    // [55:52] Memory Page Size Maximum
        UINT64 PMRS     : 1;    // [56]    Persistent Memory Region Supported
        UINT64 CMBS     : 1;    // [57]    Controller Memory Buffer Supported
        UINT64 Reserved3: 6;    // [63:58]
    };
    UINT64 AsUint64;
} NVME_CAP, *PNVME_CAP;

C_ASSERT(sizeof(NVME_CAP) == 8);

/**
 * @brief 版本寄存器 (VS) - 偏移 0x08, 32 位
 */
typedef union _NVME_VS {
    struct {
        UINT32 TER      : 8;    // [7:0]   Tertiary Version
        UINT32 MNR      : 8;    // [15:8]  Minor Version
        UINT32 MJR      : 16;   // [31:16] Major Version
    };
    UINT32 AsUint32;
} NVME_VS, *PNVME_VS;

C_ASSERT(sizeof(NVME_VS) == 4);

/**
 * @brief 控制器配置寄存器 (CC) - 偏移 0x14, 32 位
 */
typedef union _NVME_CC {
    struct {
        UINT32 EN       : 1;    // [0]     Enable
        UINT32 Reserved1: 3;    // [3:1]
        UINT32 CSS      : 3;    // [6:4]   I/O Command Set Selected
        UINT32 MPS      : 4;    // [10:7]  Memory Page Size (2^(12+MPS))
        UINT32 AMS      : 3;    // [13:11] Arbitration Mechanism Selected
        UINT32 SHN      : 2;    // [15:14] Shutdown Notification
        UINT32 IOSQES   : 4;    // [19:16] I/O SQ Entry Size (2^n)
        UINT32 IOCQES   : 4;    // [23:20] I/O CQ Entry Size (2^n)
        UINT32 Reserved2: 8;    // [31:24]
    };
    UINT32 AsUint32;
} NVME_CC, *PNVME_CC;

C_ASSERT(sizeof(NVME_CC) == 4);

/**
 * @brief 控制器状态寄存器 (CSTS) - 偏移 0x1C, 32 位
 */
typedef union _NVME_CSTS {
    struct {
        UINT32 RDY      : 1;    // [0]     Ready
        UINT32 CFS      : 1;    // [1]     Controller Fatal Status
        UINT32 SHST     : 2;    // [3:2]   Shutdown Status
        UINT32 NSSRO    : 1;    // [4]     NVM Subsystem Reset Occurred
        UINT32 PP       : 1;    // [5]     Processing Paused
        UINT32 Reserved : 26;   // [31:6]
    };
    UINT32 AsUint32;
} NVME_CSTS, *PNVME_CSTS;

C_ASSERT(sizeof(NVME_CSTS) == 4);

/**
 * @brief Admin Queue 属性寄存器 (AQA) - 偏移 0x24, 32 位
 */
typedef union _NVME_AQA {
    struct {
        UINT32 ASQS     : 12;   // [11:0]  Admin SQ Size (0's based)
        UINT32 Reserved1: 4;    // [15:12]
        UINT32 ACQS     : 12;   // [27:16] Admin CQ Size (0's based)
        UINT32 Reserved2: 4;    // [31:28]
    };
    UINT32 AsUint32;
} NVME_AQA, *PNVME_AQA;

C_ASSERT(sizeof(NVME_AQA) == 4);

/**
 * @brief 控制器寄存器布局
 */
typedef struct _NVME_CONTROLLER_REGISTERS {
    NVME_CAP    CAP;            // 0x00: Controller Capabilities
    NVME_VS     VS;             // 0x08: Version
    UINT32      INTMS;          // 0x0C: Interrupt Mask Set
    UINT32      INTMC;          // 0x10: Interrupt Mask Clear
    NVME_CC     CC;             // 0x14: Controller Configuration
    UINT32      Reserved1;      // 0x18
    NVME_CSTS   CSTS;           // 0x1C: Controller Status
    UINT32      NSSR;           // 0x20: NVM Subsystem Reset
    NVME_AQA    AQA;            // 0x24: Admin Queue Attributes
    UINT64      ASQ;            // 0x28: Admin SQ Base Address
    UINT64      ACQ;            // 0x30: Admin CQ Base Address
    UINT32      CMBLOC;         // 0x38: CMB Location
    UINT32      CMBSZ;          // 0x3C: CMB Size
    UINT32      BPINFO;         // 0x40: Boot Partition Info
    UINT32      BPRSEL;         // 0x44: Boot Partition Read Select
    UINT64      BPMBL;          // 0x48: Boot Partition Memory Buffer Location
    UINT64      CMBMSC;         // 0x50: CMB Memory Space Control
    UINT32      CMBSTS;         // 0x58: CMB Status
    UINT8       Reserved2[0x1000 - 0x5C]; // Padding to 4KB
    // Doorbells start at 0x1000
} NVME_CONTROLLER_REGISTERS, *PNVME_CONTROLLER_REGISTERS;

/**
 * @brief Doorbell 寄存器
 */
typedef struct _NVME_DOORBELL {
    UINT32 SQTail;              // SQ Tail Doorbell
    UINT32 CQHead;              // CQ Head Doorbell
} NVME_DOORBELL, *PNVME_DOORBELL;

/*===========================================================================
 * NVMe 命令格式
 *===========================================================================*/

/**
 * @brief 通用 NVMe 命令 (64 字节)
 */
typedef struct _NVME_COMMAND {
    /* DW0 */
    UINT8   OPC;                // Opcode
    UINT8   FUSE    : 2;        // Fused Operation
    UINT8   Reserved1: 4;
    UINT8   PSDT    : 2;        // PRP or SGL
    UINT16  CID;                // Command Identifier
    
    /* DW1 */
    UINT32  NSID;               // Namespace Identifier
    
    /* DW2-3 */
    UINT32  Reserved2;
    UINT32  Reserved3;
    
    /* DW4-5: Metadata Pointer */
    UINT64  MPTR;
    
    /* DW6-9: Data Pointer (PRP1, PRP2 or SGL) */
    UINT64  PRP1;
    UINT64  PRP2;
    
    /* DW10-15: Command Specific */
    UINT32  CDW10;
    UINT32  CDW11;
    UINT32  CDW12;
    UINT32  CDW13;
    UINT32  CDW14;
    UINT32  CDW15;
} NVME_COMMAND, *PNVME_COMMAND;

C_ASSERT(sizeof(NVME_COMMAND) == 64);

/**
 * @brief NVMe 完成队列条目 (16 字节)
 */
typedef struct _NVME_COMPLETION {
    UINT32  DW0;                // Command Specific
    UINT32  DW1;                // Reserved
    UINT16  SQHD;               // SQ Head Pointer
    UINT16  SQID;               // SQ Identifier
    UINT16  CID;                // Command Identifier
    UINT16  Status;             // Status Field (P, SC, SCT, M, DNR)
} NVME_COMPLETION, *PNVME_COMPLETION;

C_ASSERT(sizeof(NVME_COMPLETION) == 16);

/* 状态字段位定义 */
#define NVME_STATUS_P(status)       ((status) & 0x0001)         // Phase Tag
#define NVME_STATUS_SC(status)      (((status) >> 1) & 0xFF)    // Status Code
#define NVME_STATUS_SCT(status)     (((status) >> 9) & 0x07)    // Status Code Type
#define NVME_STATUS_M(status)       (((status) >> 14) & 0x01)   // More
#define NVME_STATUS_DNR(status)     (((status) >> 15) & 0x01)   // Do Not Retry

/*===========================================================================
 * Admin 命令 Opcodes
 *===========================================================================*/

#define NVME_ADMIN_DELETE_IO_SQ         0x00
#define NVME_ADMIN_CREATE_IO_SQ         0x01
#define NVME_ADMIN_GET_LOG_PAGE         0x02
#define NVME_ADMIN_DELETE_IO_CQ         0x04
#define NVME_ADMIN_CREATE_IO_CQ         0x05
#define NVME_ADMIN_IDENTIFY             0x06
#define NVME_ADMIN_ABORT                0x08
#define NVME_ADMIN_SET_FEATURES         0x09
#define NVME_ADMIN_GET_FEATURES         0x0A
#define NVME_ADMIN_ASYNC_EVENT_REQUEST  0x0C
#define NVME_ADMIN_NAMESPACE_MANAGEMENT 0x0D
#define NVME_ADMIN_FIRMWARE_COMMIT      0x10
#define NVME_ADMIN_FIRMWARE_DOWNLOAD    0x11
#define NVME_ADMIN_NAMESPACE_ATTACHMENT 0x15
#define NVME_ADMIN_KEEP_ALIVE           0x18
#define NVME_ADMIN_FORMAT_NVM           0x80
#define NVME_ADMIN_SECURITY_SEND        0x81
#define NVME_ADMIN_SECURITY_RECEIVE     0x82

/*===========================================================================
 * I/O 命令 Opcodes (NVM Command Set)
 *===========================================================================*/

#define NVME_IO_FLUSH                   0x00
#define NVME_IO_WRITE                   0x01
#define NVME_IO_READ                    0x02
#define NVME_IO_WRITE_UNCORRECTABLE     0x04
#define NVME_IO_COMPARE                 0x05
#define NVME_IO_WRITE_ZEROES            0x08
#define NVME_IO_DATASET_MANAGEMENT      0x09
#define NVME_IO_VERIFY                  0x0C
#define NVME_IO_RESERVATION_REGISTER    0x0D
#define NVME_IO_RESERVATION_REPORT      0x0E
#define NVME_IO_RESERVATION_ACQUIRE     0x11
#define NVME_IO_RESERVATION_RELEASE     0x15

/*===========================================================================
 * Identify 数据结构
 *===========================================================================*/

/**
 * @brief Identify Controller 数据结构 (4096 字节)
 */
typedef struct _NVME_IDENTIFY_CONTROLLER_DATA {
    /* 控制器能力和特性 */
    UINT16  VID;                // 0x00: PCI Vendor ID
    UINT16  SSVID;              // 0x02: PCI Subsystem Vendor ID
    CHAR    SN[20];             // 0x04: Serial Number
    CHAR    MN[40];             // 0x18: Model Number
    CHAR    FR[8];              // 0x40: Firmware Revision
    UINT8   RAB;                // 0x48: Recommended Arbitration Burst
    UINT8   IEEE[3];            // 0x49: IEEE OUI Identifier
    UINT8   CMIC;               // 0x4C: Controller Multi-Path I/O and Namespace Sharing
    UINT8   MDTS;               // 0x4D: Maximum Data Transfer Size
    UINT16  CNTLID;             // 0x4E: Controller ID
    UINT32  VER;                // 0x50: Version
    UINT32  RTD3R;              // 0x54: RTD3 Resume Latency
    UINT32  RTD3E;              // 0x58: RTD3 Entry Latency
    UINT32  OAES;               // 0x5C: Optional Asynchronous Events Supported
    UINT32  CTRATT;             // 0x60: Controller Attributes
    UINT16  RRLS;               // 0x64: Read Recovery Levels Supported
    UINT8   Reserved1[9];       // 0x66-0x6E
    UINT8   CNTRLTYPE;          // 0x6F: Controller Type
    UINT8   FGUID[16];          // 0x70: FRU GUID
    UINT16  CRDT1;              // 0x80: Command Retry Delay Time 1
    UINT16  CRDT2;              // 0x82: Command Retry Delay Time 2
    UINT16  CRDT3;              // 0x84: Command Retry Delay Time 3
    UINT8   Reserved2[106];     // 0x86-0xEF
    UINT8   Reserved3[16];      // 0xF0-0xFF: NVMe-MI
    
    /* Admin Command Set Attributes */
    UINT16  OACS;               // 0x100: Optional Admin Command Support
    UINT8   ACL;                // 0x102: Abort Command Limit
    UINT8   AERL;               // 0x103: Async Event Request Limit
    UINT8   FRMW;               // 0x104: Firmware Updates
    UINT8   LPA;                // 0x105: Log Page Attributes
    UINT8   ELPE;               // 0x106: Error Log Page Entries
    UINT8   NPSS;               // 0x107: Number of Power States Support
    UINT8   AVSCC;              // 0x108: Admin Vendor Specific Command Config
    UINT8   APSTA;              // 0x109: Autonomous Power State Transition Attrs
    UINT16  WCTEMP;             // 0x10A: Warning Composite Temperature Threshold
    UINT16  CCTEMP;             // 0x10C: Critical Composite Temperature Threshold
    UINT16  MTFA;               // 0x10E: Maximum Time for Firmware Activation
    UINT32  HMPRE;              // 0x110: Host Memory Buffer Preferred Size
    UINT32  HMMIN;              // 0x114: Host Memory Buffer Minimum Size
    UINT8   TNVMCAP[16];        // 0x118: Total NVM Capacity
    UINT8   UNVMCAP[16];        // 0x128: Unallocated NVM Capacity
    UINT32  RPMBS;              // 0x138: Replay Protected Memory Block Support
    UINT16  EDSTT;              // 0x13C: Extended Device Self-test Time
    UINT8   DSTO;               // 0x13E: Device Self-test Options
    UINT8   FWUG;               // 0x13F: Firmware Update Granularity
    UINT16  KAS;                // 0x140: Keep Alive Support
    UINT16  HCTMA;              // 0x142: Host Controlled Thermal Management Attrs
    UINT16  MNTMT;              // 0x144: Minimum Thermal Management Temperature
    UINT16  MXTMT;              // 0x146: Maximum Thermal Management Temperature
    UINT32  SANICAP;            // 0x148: Sanitize Capabilities
    UINT32  HMMINDS;            // 0x14C: Host Memory Buffer Minimum Descriptor Entry
    UINT16  HMMAXD;             // 0x150: Host Memory Maximum Descriptors Entries
    UINT16  NSETIDMAX;          // 0x152: NVM Set Identifier Maximum
    UINT16  ENDGIDMAX;          // 0x154: Endurance Group Identifier Maximum
    UINT8   ANATT;              // 0x156: ANA Transition Time
    UINT8   ANACAP;             // 0x157: Asymmetric Namespace Access Capabilities
    UINT32  ANAGRPMAX;          // 0x158: ANA Group Identifier Maximum
    UINT32  NANAGRPID;          // 0x15C: Number of ANA Group Identifiers
    UINT32  PELS;               // 0x160: Persistent Event Log Size
    UINT8   Reserved4[156];     // 0x164-0x1FF
    
    /* NVM Command Set Attributes */
    UINT8   SQES;               // 0x200: Submission Queue Entry Size
    UINT8   CQES;               // 0x201: Completion Queue Entry Size
    UINT16  MAXCMD;             // 0x202: Maximum Outstanding Commands
    UINT32  NN;                 // 0x204: Number of Namespaces
    UINT16  ONCS;               // 0x208: Optional NVM Command Support
    UINT16  FUSES;              // 0x20A: Fused Operation Support
    UINT8   FNA;                // 0x20C: Format NVM Attributes
    UINT8   VWC;                // 0x20D: Volatile Write Cache
    UINT16  AWUN;               // 0x20E: Atomic Write Unit Normal
    UINT16  AWUPF;              // 0x210: Atomic Write Unit Power Fail
    UINT8   NVSCC;              // 0x212: NVM Vendor Specific Command Config
    UINT8   NWPC;               // 0x213: Namespace Write Protection Capabilities
    UINT16  ACWU;               // 0x214: Atomic Compare & Write Unit
    UINT16  Reserved5;          // 0x216
    UINT32  SGLS;               // 0x218: SGL Support
    UINT32  MNAN;               // 0x21C: Maximum Number of Allowed Namespaces
    UINT8   Reserved6[224];     // 0x220-0x2FF
    
    UINT8   SUBNQN[256];        // 0x300: NVM Subsystem NVMe Qualified Name
    UINT8   Reserved7[768];     // 0x400-0x6FF
    UINT8   Reserved8[256];     // 0x700-0x7FF: NVMe over Fabrics
    
    /* Power State Descriptors */
    struct {
        UINT16 MP;              // Maximum Power
        UINT8  Reserved1;
        UINT8  MXPS    : 1;     // Max Power Scale
        UINT8  NOPS    : 1;     // Non-Operational State
        UINT8  Reserved2: 6;
        UINT32 ENLAT;           // Entry Latency
        UINT32 EXLAT;           // Exit Latency
        UINT8  RRT     : 5;     // Relative Read Throughput
        UINT8  Reserved3: 3;
        UINT8  RRL     : 5;     // Relative Read Latency
        UINT8  Reserved4: 3;
        UINT8  RWT     : 5;     // Relative Write Throughput
        UINT8  Reserved5: 3;
        UINT8  RWL     : 5;     // Relative Write Latency
        UINT8  Reserved6: 3;
        UINT16 IDLP;            // Idle Power
        UINT8  Reserved7: 6;
        UINT8  IPS     : 2;     // Idle Power Scale
        UINT8  Reserved8;
        UINT16 ACTP;            // Active Power
        UINT8  APW     : 3;     // Active Power Workload
        UINT8  Reserved9: 3;
        UINT8  APS     : 2;     // Active Power Scale
        UINT8  Reserved10[9];
    } PSD[32];                  // 0x800-0xBFF
    
    UINT8 VendorSpecific[1024]; // 0xC00-0xFFF
} NVME_IDENTIFY_CONTROLLER_DATA, *PNVME_IDENTIFY_CONTROLLER_DATA;

C_ASSERT(sizeof(NVME_IDENTIFY_CONTROLLER_DATA) == 4096);

/**
 * @brief Identify Namespace 数据结构 (4096 字节)
 */
typedef struct _NVME_IDENTIFY_NAMESPACE_DATA {
    UINT64  NSZE;               // 0x00: Namespace Size
    UINT64  NCAP;               // 0x08: Namespace Capacity
    UINT64  NUSE;               // 0x10: Namespace Utilization
    UINT8   NSFEAT;             // 0x18: Namespace Features
    UINT8   NLBAF;              // 0x19: Number of LBA Formats (0's based)
    UINT8   FLBAS;              // 0x1A: Formatted LBA Size
    UINT8   MC;                 // 0x1B: Metadata Capabilities
    UINT8   DPC;                // 0x1C: End-to-end Data Protection Capabilities
    UINT8   DPS;                // 0x1D: End-to-end Data Protection Type Settings
    UINT8   NMIC;               // 0x1E: Namespace Multi-path I/O and Sharing
    UINT8   RESCAP;             // 0x1F: Reservation Capabilities
    UINT8   FPI;                // 0x20: Format Progress Indicator
    UINT8   DLFEAT;             // 0x21: Deallocate Logical Block Features
    UINT16  NAWUN;              // 0x22: Namespace Atomic Write Unit Normal
    UINT16  NAWUPF;             // 0x24: Namespace Atomic Write Unit Power Fail
    UINT16  NACWU;              // 0x26: Namespace Atomic Compare & Write Unit
    UINT16  NABSN;              // 0x28: Namespace Atomic Boundary Size Normal
    UINT16  NABO;               // 0x2A: Namespace Atomic Boundary Offset
    UINT16  NABSPF;             // 0x2C: Namespace Atomic Boundary Size Power Fail
    UINT16  NOIOB;              // 0x2E: Namespace Optimal I/O Boundary
    UINT8   NVMCAP[16];         // 0x30: NVM Capacity
    UINT16  NPWG;               // 0x40: Namespace Preferred Write Granularity
    UINT16  NPWA;               // 0x42: Namespace Preferred Write Alignment
    UINT16  NPDG;               // 0x44: Namespace Preferred Deallocate Granularity
    UINT16  NPDA;               // 0x46: Namespace Preferred Deallocate Alignment
    UINT16  NOWS;               // 0x48: Namespace Optimal Write Size
    UINT8   Reserved1[18];      // 0x4A-0x5B
    UINT32  ANAGRPID;           // 0x5C: ANA Group Identifier
    UINT8   Reserved2[3];       // 0x60-0x62
    UINT8   NSATTR;             // 0x63: Namespace Attributes
    UINT16  NVMSETID;           // 0x64: NVM Set Identifier
    UINT16  ENDGID;             // 0x66: Endurance Group Identifier
    UINT8   NGUID[16];          // 0x68: Namespace GUID
    UINT8   EUI64[8];           // 0x78: IEEE Extended Unique Identifier
    
    /* LBA Format Support */
    struct {
        UINT32 MS       : 16;   // Metadata Size
        UINT32 LBADS    : 8;    // LBA Data Size (2^n)
        UINT32 RP       : 2;    // Relative Performance
        UINT32 Reserved : 6;
    } LBAF[16];                 // 0x80-0xBF
    
    UINT8   Reserved3[192];     // 0xC0-0x17F
    UINT8   VendorSpecific[3712]; // 0x180-0xFFF
} NVME_IDENTIFY_NAMESPACE_DATA, *PNVME_IDENTIFY_NAMESPACE_DATA;

C_ASSERT(sizeof(NVME_IDENTIFY_NAMESPACE_DATA) == 4096);

/*===========================================================================
 * Feature Identifiers
 *===========================================================================*/

#define NVME_FEATURE_ARBITRATION                0x01
#define NVME_FEATURE_POWER_MANAGEMENT           0x02
#define NVME_FEATURE_LBA_RANGE_TYPE             0x03
#define NVME_FEATURE_TEMPERATURE_THRESHOLD      0x04
#define NVME_FEATURE_ERROR_RECOVERY             0x05
#define NVME_FEATURE_VOLATILE_WRITE_CACHE       0x06
#define NVME_FEATURE_NUMBER_OF_QUEUES           0x07
#define NVME_FEATURE_INTERRUPT_COALESCING       0x08
#define NVME_FEATURE_INTERRUPT_VECTOR_CONFIG    0x09
#define NVME_FEATURE_WRITE_ATOMICITY_NORMAL     0x0A
#define NVME_FEATURE_ASYNC_EVENT_CONFIG         0x0B
#define NVME_FEATURE_AUTO_POWER_STATE_TRANSITION 0x0C
#define NVME_FEATURE_HOST_MEMORY_BUFFER         0x0D
#define NVME_FEATURE_TIMESTAMP                  0x0E
#define NVME_FEATURE_KEEP_ALIVE_TIMER           0x0F
#define NVME_FEATURE_HOST_CONTROLLED_TM         0x10
#define NVME_FEATURE_NON_OP_POWER_STATE_CONFIG  0x11
#define NVME_FEATURE_READ_RECOVERY_LEVEL_CONFIG 0x12
#define NVME_FEATURE_PREDICTABLE_LATENCY_MODE   0x13
#define NVME_FEATURE_PREDICTABLE_LATENCY_WINDOW 0x14
#define NVME_FEATURE_LBA_STATUS_INFO_REPORT     0x15
#define NVME_FEATURE_HOST_BEHAVIOR              0x16
#define NVME_FEATURE_SANITIZE_CONFIG            0x17
#define NVME_FEATURE_ENDURANCE_GROUP_EVENT      0x18
#define NVME_FEATURE_SOFTWARE_PROGRESS_MARKER   0x80
#define NVME_FEATURE_HOST_IDENTIFIER            0x81
#define NVME_FEATURE_RESERVATION_NOTIFICATION   0x82
#define NVME_FEATURE_RESERVATION_PERSISTENCE    0x83

/*===========================================================================
 * PCIe 配置空间
 *===========================================================================*/

/**
 * @brief PCIe 配置空间头 (256 字节)
 */
typedef struct _PCIE_CONFIG_HEADER {
    UINT16  VendorId;           // 0x00
    UINT16  DeviceId;           // 0x02
    UINT16  Command;            // 0x04
    UINT16  Status;             // 0x06
    UINT8   RevisionId;         // 0x08
    UINT8   ProgIf;             // 0x09: Programming Interface
    UINT8   SubClass;           // 0x0A: Sub-Class Code
    UINT8   ClassCode;          // 0x0B: Class Code
    UINT8   CacheLineSize;      // 0x0C
    UINT8   LatencyTimer;       // 0x0D
    UINT8   HeaderType;         // 0x0E
    UINT8   BIST;               // 0x0F
    UINT32  BAR0;               // 0x10: Base Address 0
    UINT32  BAR1;               // 0x14: Base Address 1
    UINT32  BAR2;               // 0x18: Base Address 2
    UINT32  BAR3;               // 0x1C: Base Address 3
    UINT32  BAR4;               // 0x20: Base Address 4
    UINT32  BAR5;               // 0x24: Base Address 5
    UINT32  CardbusCISPtr;      // 0x28
    UINT16  SubsystemVendorId;  // 0x2C
    UINT16  SubsystemId;        // 0x2E
    UINT32  ExpROMBaseAddr;     // 0x30
    UINT8   CapabilitiesPtr;    // 0x34
    UINT8   Reserved1[3];       // 0x35-0x37
    UINT32  Reserved2;          // 0x38
    UINT8   InterruptLine;      // 0x3C
    UINT8   InterruptPin;       // 0x3D
    UINT8   MinGrant;           // 0x3E
    UINT8   MaxLatency;         // 0x3F
} PCIE_CONFIG_HEADER, *PPCIE_CONFIG_HEADER;

C_ASSERT(sizeof(PCIE_CONFIG_HEADER) == 64);

/* NVMe Class Code */
#define PCIE_CLASS_MASS_STORAGE         0x01
#define PCIE_SUBCLASS_NVM               0x08
#define PCIE_PROGIF_NVME                0x02

#pragma pack(pop)

#endif /* _NVME_SPEC_H_ */
