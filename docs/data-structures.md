# 核心数据结构

本文档定义 Virtual NVMe StorPort Miniport 驱动的核心数据结构。

## 编译器和平台注意事项

```c
// 使用 StorPort 头文件
#include <storport.h>
#include <ntddscsi.h>
#include <srb.h>

// 结构对齐设置
#pragma pack(push, 1)   // NVMe 规范结构使用 1 字节对齐
// ... NVMe 结构定义 ...
#pragma pack(pop)

// 驱动池标签
#define VNVME_POOL_TAG          'vmNV'
#define VNVME_BACKEND_TAG       'eBNV'
#define VNVME_LUN_TAG           'uLNV'

// 限制常量
#define VNVME_MAX_TARGETS       1       // 目标数量
#define VNVME_MAX_LUNS          64      // 每目标最大 LUN 数
#define VNVME_MAX_TRANSFER_SIZE (4 * 1024 * 1024)  // 4MB
```

## StorPort 扩展结构

### 适配器扩展 (DeviceExtension)

```c
//
// 适配器状态枚举
//
typedef enum _VNVME_ADAPTER_STATE {
    VNVME_ADAPTER_STATE_UNINITIALIZED = 0,  // 未初始化
    VNVME_ADAPTER_STATE_INITIALIZING,       // 初始化中
    VNVME_ADAPTER_STATE_RUNNING,            // 运行中
    VNVME_ADAPTER_STATE_STOPPED,            // 已停止
    VNVME_ADAPTER_STATE_RESETTING,          // 重置中
    VNVME_ADAPTER_STATE_ERROR               // 错误状态
} VNVME_ADAPTER_STATE;

//
// 适配器扩展结构 (HW_INITIALIZATION_DATA.DeviceExtensionSize)
//
typedef struct _VNVME_ADAPTER_EXTENSION {
    //
    // 自引用 (便于从回调中获取)
    //
    struct _VNVME_ADAPTER_EXTENSION* Self;
    
    //
    // 适配器状态
    //
    VNVME_ADAPTER_STATE State;
    
    //
    // 适配器配置
    //
    VNVME_ADAPTER_CONFIG Config;
    
    //
    // LUN 管理
    //
    KSPIN_LOCK      LunListLock;        // LUN 列表锁
    LIST_ENTRY      LunList;            // LUN 链表头
    ULONG           LunCount;           // LUN 数量
    
    //
    // 后端管理器
    //
    PVNVME_BACKEND_MANAGER BackendManager;
    
    //
    // 统计信息
    //
    VNVME_ADAPTER_STATS Stats;
    
    //
    // 注册表配置路径
    //
    UNICODE_STRING RegistryPath;
    
#if DBG
    //
    // 调试: 故障注入
    //
    VNVME_FAULT_INJECTION FaultInjection;
#endif

} VNVME_ADAPTER_EXTENSION, *PVNVME_ADAPTER_EXTENSION;
```

### LUN 扩展 (SpecificLuExtension)

```c
//
// LUN 状态枚举
//
typedef enum _VNVME_LUN_STATE {
    VNVME_LUN_STATE_NOT_PRESENT = 0,    // 不存在
    VNVME_LUN_STATE_INITIALIZING,       // 初始化中
    VNVME_LUN_STATE_READY,              // 就绪
    VNVME_LUN_STATE_OFFLINE,            // 离线
    VNVME_LUN_STATE_ERROR               // 错误
} VNVME_LUN_STATE;

//
// LUN 扩展结构 (HW_INITIALIZATION_DATA.SpecificLuExtensionSize)
//
typedef struct _VNVME_LU_EXTENSION {
    //
    // 链表节点 (用于适配器 LunList)
    //
    LIST_ENTRY ListEntry;
    
    //
    // 所属适配器
    //
    PVNVME_ADAPTER_EXTENSION AdapterExtension;
    
    //
    // 地址三元组
    //
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    
    //
    // LUN 状态
    //
    VNVME_LUN_STATE State;
    
    //
    // 磁盘属性
    //
    ULONGLONG TotalSectors;             // 总扇区数
    ULONG SectorSize;                   // 扇区大小 (512/4096)
    ULONGLONG TotalSize;                // 总容量 (字节)
    BOOLEAN ReadOnly;                   // 只读标志
    BOOLEAN RemovableMedia;             // 可移除介质
    
    //
    // 设备标识
    //
    CHAR VendorId[8 + 1];               // 厂商标识 (8字符)
    CHAR ProductId[16 + 1];             // 产品标识 (16字符)
    CHAR SerialNumber[20 + 1];          // 序列号 (20字符)
    UCHAR DeviceIdentifier[16];         // 唯一标识符 (用于 MPIO)
    
    //
    // 存储后端
    //
    PVNVME_BACKEND Backend;
    
    //
    // 同步锁
    //
    KSPIN_LOCK Lock;
    
    //
    // 统计信息
    //
    VNVME_LUN_STATS Stats;
    
} VNVME_LU_EXTENSION, *PVNVME_LU_EXTENSION;
```

### SRB 扩展 (SrbExtension)

```c
//
// SRB 扩展结构 (HW_INITIALIZATION_DATA.SrbExtensionSize)
//
typedef struct _VNVME_SRB_EXTENSION {
    //
    // 关联的 LUN
    //
    PVNVME_LU_EXTENSION LuExtension;
    
    //
    // I/O 上下文
    //
    ULONGLONG StartLba;                 // 起始 LBA
    ULONG SectorCount;                  // 扇区数
    ULONG BytesTransferred;             // 已传输字节数
    
    //
    // 异步 I/O 支持
    //
    PVNVME_BACKEND_IO BackendIo;        // 后端 I/O 上下文
    
    //
    // 时间戳 (性能分析)
    //
    LARGE_INTEGER StartTime;
    
} VNVME_SRB_EXTENSION, *PVNVME_SRB_EXTENSION;
```

## 配置结构

### 适配器配置

```c
//
// 适配器配置 (从注册表加载)
//
typedef struct _VNVME_ADAPTER_CONFIG {
    //
    // 最大 LUN 数量
    //
    ULONG MaxLuns;
    
    //
    // 最大传输大小
    //
    ULONG MaxTransferSize;
    
    //
    // 默认后端类型
    //
    VNVME_BACKEND_TYPE DefaultBackendType;
    
    //
    // 默认后端路径 (文件后端)
    //
    WCHAR DefaultBackendPath[260];
    
    //
    // 启用 UNMAP/TRIM 支持
    //
    BOOLEAN EnableUnmap;
    
    //
    // 启用 FUA (Force Unit Access)
    //
    BOOLEAN EnableFua;
    
    //
    // 启用 Write Cache
    //
    BOOLEAN EnableWriteCache;
    
    //
    // 启用 MPIO 支持
    //
    BOOLEAN EnableMpio;
    
} VNVME_ADAPTER_CONFIG, *PVNVME_ADAPTER_CONFIG;
```

### LUN 创建配置

```c
//
// LUN 创建输入参数
//
typedef struct _VNVME_LUN_CONFIG {
    //
    // 地址 (可选，0 表示自动分配)
    //
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    
    //
    // 磁盘大小
    //
    ULONGLONG SizeInBytes;
    
    //
    // 扇区大小
    //
    ULONG SectorSize;                   // 512 或 4096
    
    //
    // 只读标志
    //
    BOOLEAN ReadOnly;
    
    //
    // 设备标识 (可选)
    //
    CHAR VendorId[8];
    CHAR ProductId[16];
    CHAR SerialNumber[20];
    
    //
    // 后端配置
    //
    VNVME_BACKEND_CONFIG BackendConfig;
    
} VNVME_LUN_CONFIG, *PVNVME_LUN_CONFIG;
```

## 存储后端结构

### 后端类型枚举

```c
typedef enum _VNVME_BACKEND_TYPE {
    VNVME_BACKEND_MEMORY = 0,   // 内存后端 (非持久)
    VNVME_BACKEND_FILE   = 1,   // 文件后端 (持久)
    VNVME_BACKEND_VHD    = 2,   // VHD/VHDX 后端 (持久)
    VNVME_BACKEND_REMOTE = 3,   // 远程后端 (预留)
    VNVME_BACKEND_MAX
} VNVME_BACKEND_TYPE;
```

### 后端能力标志

```c
typedef enum _VNVME_BACKEND_CAPS {
    VNVME_BACKEND_CAP_READ       = 0x00000001,  // 支持读取
    VNVME_BACKEND_CAP_WRITE      = 0x00000002,  // 支持写入
    VNVME_BACKEND_CAP_FLUSH      = 0x00000004,  // 支持刷新
    VNVME_BACKEND_CAP_TRIM       = 0x00000008,  // 支持 TRIM/UNMAP
    VNVME_BACKEND_CAP_FUA        = 0x00000010,  // 支持 Force Unit Access
    VNVME_BACKEND_CAP_RESIZE     = 0x00000020,  // 支持动态调整大小
    VNVME_BACKEND_CAP_SNAPSHOT   = 0x00000040,  // 支持快照
    VNVME_BACKEND_CAP_PERSISTENT = 0x00000080,  // 数据持久化
    VNVME_BACKEND_CAP_ASYNC      = 0x00000100,  // 支持异步 I/O
} VNVME_BACKEND_CAPS;
```

### 后端配置结构

```c
//
// 后端创建配置
//
typedef struct _VNVME_BACKEND_CONFIG {
    //
    // 后端类型
    //
    VNVME_BACKEND_TYPE Type;
    
    //
    // 大小 (字节)
    //
    ULONGLONG Size;
    
    //
    // 块大小
    //
    ULONG BlockSize;
    
    //
    // 只读
    //
    BOOLEAN ReadOnly;
    
    //
    // 创建新后端 vs 打开现有
    //
    BOOLEAN CreateNew;
    
    //
    // 类型特定配置
    //
    union {
        // 内存后端
        struct {
            BOOLEAN PreAllocate;        // 预分配全部内存
        } Memory;
        
        // 文件后端
        struct {
            WCHAR FilePath[260];        // 文件路径
            BOOLEAN SparseFile;         // 使用稀疏文件
            BOOLEAN NoBuffering;        // 直接 I/O
            BOOLEAN WriteThrough;       // 写透模式
        } File;
        
        // VHD 后端
        struct {
            WCHAR VhdPath[260];         // VHD 路径
            ULONG VhdType;              // Fixed/Dynamic/Differencing
        } Vhd;
    };
    
} VNVME_BACKEND_CONFIG, *PVNVME_BACKEND_CONFIG;
```

### 后端实例结构

```c
//
// 后端实例
//
typedef struct _VNVME_BACKEND {
    //
    // 后端类型
    //
    VNVME_BACKEND_TYPE Type;
    
    //
    // 能力标志
    //
    ULONG Capabilities;
    
    //
    // 大小信息
    //
    ULONGLONG TotalSize;
    ULONG BlockSize;
    BOOLEAN ReadOnly;
    
    //
    // 类型特定数据
    //
    union {
        // 内存后端
        struct {
            PVOID Buffer;
            SIZE_T BufferSize;
        } Memory;
        
        // 文件后端
        struct {
            HANDLE FileHandle;
            PFILE_OBJECT FileObject;
            UNICODE_STRING FilePath;
        } File;
        
        // VHD 后端
        struct {
            HANDLE VhdHandle;
            UNICODE_STRING VhdPath;
        } Vhd;
    };
    
    //
    // 操作函数表
    //
    PVNVME_BACKEND_OPS Operations;
    
    //
    // 同步锁
    //
    ERESOURCE Lock;
    
} VNVME_BACKEND, *PVNVME_BACKEND;
```

### 后端操作函数表

```c
//
// 后端操作接口 (虚函数表)
//
typedef struct _VNVME_BACKEND_OPS {
    //
    // 初始化后端
    //
    NTSTATUS (*Initialize)(
        _Out_ PVNVME_BACKEND* Backend,
        _In_ PVNVME_BACKEND_CONFIG Config);
    
    //
    // 获取后端信息
    //
    NTSTATUS (*GetInfo)(
        _In_ PVNVME_BACKEND Backend,
        _Out_ PVNVME_BACKEND_INFO Info);
    
    //
    // 同步读取
    //
    NTSTATUS (*Read)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONGLONG Offset,
        _In_ ULONG Length,
        _Out_writes_bytes_(Length) PVOID Buffer);
    
    //
    // 同步写入
    //
    NTSTATUS (*Write)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONGLONG Offset,
        _In_ ULONG Length,
        _In_reads_bytes_(Length) PVOID Buffer);
    
    //
    // 刷新缓存
    //
    NTSTATUS (*Flush)(
        _In_ PVNVME_BACKEND Backend);
    
    //
    // TRIM/UNMAP
    //
    NTSTATUS (*Trim)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONGLONG Offset,
        _In_ ULONGLONG Length);
    
    //
    // 动态调整大小
    //
    NTSTATUS (*Resize)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONGLONG NewSize);
    
    //
    // 关闭后端
    //
    VOID (*Close)(
        _In_ PVNVME_BACKEND Backend);
    
} VNVME_BACKEND_OPS, *PVNVME_BACKEND_OPS;
```

### 后端信息结构

```c
typedef struct _VNVME_BACKEND_INFO {
    VNVME_BACKEND_TYPE  Type;
    ULONG               Capabilities;
    ULONGLONG           TotalSize;
    ULONGLONG           UsedSize;
    ULONG               BlockSize;
    ULONG               OptimalTransferSize;
    BOOLEAN             ReadOnly;
    BOOLEAN             Sparse;
    WCHAR               Description[64];
} VNVME_BACKEND_INFO, *PVNVME_BACKEND_INFO;
```

## IOCTL 结构

### IOCTL 定义

```c
//
// IOCTL 控制码
//
#define IOCTL_VNVME_BASE                    0x800

#define IOCTL_VNVME_CREATE_DISK             CTL_CODE(IOCTL_SCSI_BASE, \
                                                IOCTL_VNVME_BASE + 0, \
                                                METHOD_BUFFERED, \
                                                FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_VNVME_DELETE_DISK             CTL_CODE(IOCTL_SCSI_BASE, \
                                                IOCTL_VNVME_BASE + 1, \
                                                METHOD_BUFFERED, \
                                                FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_VNVME_QUERY_DISK              CTL_CODE(IOCTL_SCSI_BASE, \
                                                IOCTL_VNVME_BASE + 2, \
                                                METHOD_BUFFERED, \
                                                FILE_READ_ACCESS)

#define IOCTL_VNVME_RESIZE_DISK             CTL_CODE(IOCTL_SCSI_BASE, \
                                                IOCTL_VNVME_BASE + 3, \
                                                METHOD_BUFFERED, \
                                                FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_VNVME_QUERY_ADAPTER           CTL_CODE(IOCTL_SCSI_BASE, \
                                                IOCTL_VNVME_BASE + 4, \
                                                METHOD_BUFFERED, \
                                                FILE_READ_ACCESS)

#define IOCTL_VNVME_SET_BACKEND             CTL_CODE(IOCTL_SCSI_BASE, \
                                                IOCTL_VNVME_BASE + 5, \
                                                METHOD_BUFFERED, \
                                                FILE_READ_ACCESS | FILE_WRITE_ACCESS)
```

### IOCTL 输入输出结构

```c
//
// 创建磁盘
//
typedef struct _VNVME_CREATE_DISK_INPUT {
    VNVME_LUN_CONFIG Config;
} VNVME_CREATE_DISK_INPUT, *PVNVME_CREATE_DISK_INPUT;

typedef struct _VNVME_CREATE_DISK_OUTPUT {
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR Reserved;
    NTSTATUS Status;
} VNVME_CREATE_DISK_OUTPUT, *PVNVME_CREATE_DISK_OUTPUT;

//
// 删除磁盘
//
typedef struct _VNVME_DELETE_DISK_INPUT {
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR Reserved;
} VNVME_DELETE_DISK_INPUT, *PVNVME_DELETE_DISK_INPUT;

//
// 查询磁盘
//
typedef struct _VNVME_QUERY_DISK_INPUT {
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR Reserved;
} VNVME_QUERY_DISK_INPUT, *PVNVME_QUERY_DISK_INPUT;

typedef struct _VNVME_QUERY_DISK_OUTPUT {
    VNVME_LUN_STATE State;
    ULONGLONG TotalSize;
    ULONG SectorSize;
    BOOLEAN ReadOnly;
    VNVME_BACKEND_TYPE BackendType;
    VNVME_LUN_STATS Stats;
} VNVME_QUERY_DISK_OUTPUT, *PVNVME_QUERY_DISK_OUTPUT;

//
// 调整磁盘大小
//
typedef struct _VNVME_RESIZE_DISK_INPUT {
    UCHAR PathId;
    UCHAR TargetId;
    UCHAR Lun;
    UCHAR Reserved;
    ULONGLONG NewSizeInBytes;
} VNVME_RESIZE_DISK_INPUT, *PVNVME_RESIZE_DISK_INPUT;

//
// 查询适配器
//
typedef struct _VNVME_QUERY_ADAPTER_OUTPUT {
    VNVME_ADAPTER_STATE State;
    ULONG LunCount;
    ULONG MaxLuns;
    VNVME_ADAPTER_STATS Stats;
} VNVME_QUERY_ADAPTER_OUTPUT, *PVNVME_QUERY_ADAPTER_OUTPUT;
```

## 统计信息结构

```c
//
// 适配器统计
//
typedef struct _VNVME_ADAPTER_STATS {
    LONG64 TotalIoRequests;
    LONG64 TotalBytesRead;
    LONG64 TotalBytesWritten;
    LONG64 TotalErrors;
    LONG64 TotalResets;
} VNVME_ADAPTER_STATS, *PVNVME_ADAPTER_STATS;

//
// LUN 统计
//
typedef struct _VNVME_LUN_STATS {
    LONG64 ReadCommands;
    LONG64 WriteCommands;
    LONG64 FlushCommands;
    LONG64 UnmapCommands;
    LONG64 BytesRead;
    LONG64 BytesWritten;
    LONG64 ReadErrors;
    LONG64 WriteErrors;
} VNVME_LUN_STATS, *PVNVME_LUN_STATS;
```

## SCSI 相关结构

### VPD 页结构

```c
//
// 设备标识 VPD 页 (0x83)
//
typedef struct _VNVME_VPD_DEVICE_ID {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR Reserved;
    UCHAR PageLength;
    
    // 标识符描述符列表
    struct {
        UCHAR CodeSet : 4;
        UCHAR Reserved1 : 4;
        UCHAR IdentifierType : 4;
        UCHAR Association : 2;
        UCHAR Reserved2 : 2;
        UCHAR Reserved3;
        UCHAR IdentifierLength;
        UCHAR Identifier[16];           // NAA 或 EUI-64
    } Descriptor;
    
} VNVME_VPD_DEVICE_ID, *PVNVME_VPD_DEVICE_ID;

//
// 块限制 VPD 页 (0xB0)
//
typedef struct _VNVME_VPD_BLOCK_LIMITS {
    UCHAR DeviceType : 5;
    UCHAR DeviceTypeQualifier : 3;
    UCHAR PageCode;
    UCHAR PageLength[2];
    
    UCHAR Reserved[4];
    
    // 最优传输长度
    UCHAR OptimalTransferLengthGranularity[2];
    UCHAR MaximumTransferLength[4];
    UCHAR OptimalTransferLength[4];
    
    // UNMAP 限制
    UCHAR MaximumUnmapLbaCount[4];
    UCHAR MaximumUnmapBlockDescriptorCount[4];
    UCHAR OptimalUnmapGranularity[4];
    UCHAR UnmapGranularityAlignment[4];
    
} VNVME_VPD_BLOCK_LIMITS, *PVNVME_VPD_BLOCK_LIMITS;
```

### 模式页结构

```c
//
// 缓存模式页 (0x08)
//
typedef struct _VNVME_MODE_CACHING_PAGE {
    UCHAR PageCode : 6;
    UCHAR Reserved1 : 1;
    UCHAR PageSavable : 1;
    UCHAR PageLength;
    
    UCHAR ReadDisableCache : 1;
    UCHAR MultiplicationFactor : 1;
    UCHAR WriteCacheEnable : 1;
    UCHAR Reserved2 : 5;
    
    UCHAR WriteRetentionPriority : 4;
    UCHAR ReadRetentionPriority : 4;
    
    UCHAR DisablePrefetchTransferLength[2];
    UCHAR MinimumPrefetch[2];
    UCHAR MaximumPrefetch[2];
    UCHAR MaximumPrefetchCeiling[2];
    
    UCHAR Flags;
    UCHAR NumberOfCacheSegments;
    UCHAR CacheSegmentSize[2];
    
} VNVME_MODE_CACHING_PAGE, *PVNVME_MODE_CACHING_PAGE;
```

## 调试结构

```c
#if DBG

//
// 故障注入配置
//
typedef struct _VNVME_FAULT_INJECTION {
    BOOLEAN InjectReadError;            // 读取时注入错误
    BOOLEAN InjectWriteError;           // 写入时注入错误
    ULONG FailAfterNIos;                // N 次 I/O 后失败
    ULONG CurrentIoCount;               // 当前 I/O 计数
    NTSTATUS ErrorStatus;               // 注入的错误状态
} VNVME_FAULT_INJECTION, *PVNVME_FAULT_INJECTION;

//
// 性能计数器
//
typedef struct _VNVME_PERF_COUNTERS {
    LONG64 TotalIoTime;                 // 总 I/O 时间 (100ns 单位)
    LONG64 TotalIoCount;                // 总 I/O 次数
    LONG64 MaxIoTime;                   // 最大单次 I/O 时间
    LONG64 MinIoTime;                   // 最小单次 I/O 时间
} VNVME_PERF_COUNTERS, *PVNVME_PERF_COUNTERS;

#endif // DBG
```

## 各后端能力对比

| 后端类型 | 持久化 | TRIM | FUA | 异步 | 动态大小 | 快照 |
|----------|--------|------|-----|------|----------|------|
| Memory   | ✗      | ✓    | N/A | ✓    | ✗        | ✗    |
| File     | ✓      | ✓*   | ✓   | ✓    | ✓        | ✗    |
| VHD      | ✓      | ✓    | ✓   | ✓    | ✓        | ✓    |
| Remote   | ✓      | 取决于目标 | ✓ | ✓  | ✗        | ✗    |

*注: 文件后端 TRIM 需要底层文件系统支持稀疏文件 (NTFS/ReFS)
