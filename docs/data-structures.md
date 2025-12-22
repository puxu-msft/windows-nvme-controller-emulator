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

## 常量定义

```c
#define VNVME_MAX_IO_QUEUES     64
#define VNVME_MAX_NAMESPACES    16
#define VNVME_MAX_QUEUE_DEPTH   1024
#define VNVME_ADMIN_QUEUE_ID    0
#define VNVME_DEFAULT_BLOCK_SIZE 512
```
