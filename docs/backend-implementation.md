# 存储后端实现指南

本文档详细说明各存储后端的实现细节，包括初始化、I/O 操作、资源管理等。

## 后端架构概述

### 设计原则

1. **模块化**: 每种后端类型独立实现，通过函数指针表统一接口
2. **可扩展**: 支持运行时注册新的后端类型
3. **低耦合**: 驱动核心不依赖具体后端实现
4. **按需选择**: 根据使用场景选择合适的后端

### 架构图

```
┌─────────────────────────────────────────────────────────────────┐
│                    vnvme.sys (功能驱动)                          │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   后端管理器 (Backend Manager)              │  │
│  │  VNvmeBackendCreate() | VNvmeBackendRead() | ...           │  │
│  └───────────────────────────────────────────────────────────┘  │
│                              │                                   │
│                              ▼                                   │
│  ┌───────────────────────────────────────────────────────────┐  │
│  │                   后端注册表 (Backend Registry)             │  │
│  │  g_BackendRegistry[] - 存储所有可用后端类型                  │  │
│  └───────────────────────────────────────────────────────────┘  │
│        │              │              │              │            │
│        ▼              ▼              ▼              ▼            │
│  ┌─────────┐   ┌─────────┐   ┌─────────┐   ┌─────────────┐      │
│  │ Memory  │   │  File   │   │   VHD   │   │   Remote    │      │
│  │ Backend │   │ Backend │   │ Backend │   │   Backend   │      │
│  │ (内置)  │   │ (内置)  │   │ (可选)  │   │   (预留)    │      │
│  └─────────┘   └─────────┘   └─────────┘   └─────────────┘      │
│       │              │              │              │             │
└───────┼──────────────┼──────────────┼──────────────┼─────────────┘
        ▼              ▼              ▼              ▼
   NonPagedPool    ZwReadFile     virtdisk       Network
                   ZwWriteFile    API/Driver      Stack
```

---

## 后端管理器实现

### vnvme_backend_manager.c

```c
//
// 后端管理器 - 管理后端注册和创建
//

#include "vnvme.h"
#include "vnvme_backend.h"

// 全局后端注册表
static VNVME_BACKEND_REGISTRATION g_BackendRegistry[VNVME_BACKEND_MAX] = {
    {
        .Type = VNVME_BACKEND_MEMORY,
        .Name = L"Memory",
        .DefaultCaps = VNVME_BACKEND_CAP_READ | VNVME_BACKEND_CAP_WRITE |
                       VNVME_BACKEND_CAP_FLUSH | VNVME_BACKEND_CAP_TRIM |
                       VNVME_BACKEND_CAP_ASYNC,
        .Operations = &g_MemoryBackendOps
    },
    {
        .Type = VNVME_BACKEND_FILE,
        .Name = L"File",
        .DefaultCaps = VNVME_BACKEND_CAP_READ | VNVME_BACKEND_CAP_WRITE |
                       VNVME_BACKEND_CAP_FLUSH | VNVME_BACKEND_CAP_FUA |
                       VNVME_BACKEND_CAP_PERSISTENT | VNVME_BACKEND_CAP_RESIZE,
        .Operations = &g_FileBackendOps
    },
    {
        .Type = VNVME_BACKEND_VHD,
        .Name = L"VHD",
        .DefaultCaps = 0,  // 动态检测
        .Operations = NULL // 需要单独实现或加载
    },
    {
        .Type = VNVME_BACKEND_REMOTE,
        .Name = L"Remote",
        .DefaultCaps = 0,
        .Operations = NULL // 预留
    },
};

// 注册表锁
static KSPIN_LOCK g_RegistryLock;
static BOOLEAN g_RegistryInitialized = FALSE;

//
// 初始化后端管理器
//
NTSTATUS VNvmeBackendManagerInit(VOID)
{
    if (g_RegistryInitialized) {
        return STATUS_SUCCESS;
    }
    
    KeInitializeSpinLock(&g_RegistryLock);
    g_RegistryInitialized = TRUE;
    
    return STATUS_SUCCESS;
}

//
// 创建后端实例
//
NTSTATUS VNvmeBackendCreate(
    _In_ PVNVME_BACKEND_CONFIG Config,
    _Out_ PVNVME_BACKEND* Backend)
{
    NTSTATUS status;
    KLOCK_QUEUE_HANDLE lockHandle;
    PVNVME_BACKEND_OPS ops = NULL;
    
    if (!Config || !Backend) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *Backend = NULL;
    
    // 验证后端类型
    if (Config->Type >= VNVME_BACKEND_MAX) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 查找后端操作表
    KeAcquireInStackQueuedSpinLock(&g_RegistryLock, &lockHandle);
    ops = g_BackendRegistry[Config->Type].Operations;
    KeReleaseInStackQueuedSpinLock(&lockHandle);
    
    if (!ops || !ops->Initialize) {
        return STATUS_NOT_SUPPORTED;
    }
    
    // 调用具体后端的初始化函数
    status = ops->Initialize(Backend, Config);
    
    return status;
}

//
// 销毁后端实例
//
VOID VNvmeBackendDestroy(
    _In_ PVNVME_BACKEND Backend)
{
    if (!Backend || !Backend->Operations || !Backend->Operations->Close) {
        return;
    }
    
    Backend->Operations->Close(Backend);
}

//
// 注册自定义后端 (供扩展使用)
//
NTSTATUS VNvmeBackendRegister(
    _In_ PVNVME_BACKEND_REGISTRATION Registration)
{
    KLOCK_QUEUE_HANDLE lockHandle;
    
    if (!Registration || Registration->Type >= VNVME_BACKEND_MAX) {
        return STATUS_INVALID_PARAMETER;
    }
    
    KeAcquireInStackQueuedSpinLock(&g_RegistryLock, &lockHandle);
    
    // 不允许覆盖已注册的后端
    if (g_BackendRegistry[Registration->Type].Operations != NULL) {
        KeReleaseInStackQueuedSpinLock(&lockHandle);
        return STATUS_ALREADY_REGISTERED;
    }
    
    g_BackendRegistry[Registration->Type] = *Registration;
    
    KeReleaseInStackQueuedSpinLock(&lockHandle);
    
    return STATUS_SUCCESS;
}
```

---

## 内存后端实现

### 特性

- **持久性**: 非持久（驱动卸载后数据丢失）
- **性能**: 最高（直接内存拷贝）
- **适用场景**: 测试、临时存储、RAM Disk

### vnvme_backend_memory.c

```c
//
// 内存后端实现
//

#include "vnvme.h"
#include "vnvme_backend.h"

#define MEMORY_BACKEND_TAG 'mBNV'

// 内存后端上下文
typedef struct _MEMORY_BACKEND_CONTEXT {
    VNVME_BACKEND   Base;           // 基础结构 (必须在首位)
    PVOID           Buffer;         // 数据缓冲区
    SIZE_T          BufferSize;     // 缓冲区大小
    PMDL            Mdl;            // 锁定 MDL
    ERESOURCE       Lock;           // 读写锁
} MEMORY_BACKEND_CONTEXT, *PMEMORY_BACKEND_CONTEXT;

//
// 初始化内存后端
//
static NTSTATUS MemoryBackendInitialize(
    _Out_ PVNVME_BACKEND* Backend,
    _In_ PVNVME_BACKEND_CONFIG Config)
{
    PMEMORY_BACKEND_CONTEXT ctx;
    NTSTATUS status = STATUS_SUCCESS;
    
    // 验证参数
    if (Config->Size == 0 || Config->Size > (SIZE_T)16 * 1024 * 1024 * 1024) {
        // 限制最大 16GB (防止系统内存耗尽)
        return STATUS_INVALID_PARAMETER;
    }
    
    // 分配上下文
    ctx = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(MEMORY_BACKEND_CONTEXT),
        MEMORY_BACKEND_TAG);
    
    if (!ctx) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(ctx, sizeof(MEMORY_BACKEND_CONTEXT));
    
    // 初始化读写锁
    status = ExInitializeResourceLite(&ctx->Lock);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(ctx, MEMORY_BACKEND_TAG);
        return status;
    }
    
    // 分配数据缓冲区
    ctx->BufferSize = (SIZE_T)Config->Size;
    ctx->Buffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        ctx->BufferSize,
        MEMORY_BACKEND_TAG);
    
    if (!ctx->Buffer) {
        ExDeleteResourceLite(&ctx->Lock);
        ExFreePoolWithTag(ctx, MEMORY_BACKEND_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 初始化为零 (模拟空磁盘)
    RtlZeroMemory(ctx->Buffer, ctx->BufferSize);
    
    // 填充基础结构
    ctx->Base.Type = VNVME_BACKEND_MEMORY;
    ctx->Base.Capabilities = VNVME_BACKEND_CAP_READ | VNVME_BACKEND_CAP_WRITE |
                             VNVME_BACKEND_CAP_FLUSH | VNVME_BACKEND_CAP_TRIM |
                             VNVME_BACKEND_CAP_ASYNC;
    ctx->Base.TotalSize = Config->Size;
    ctx->Base.BlockSize = Config->BlockSize ? Config->BlockSize : 512;
    ctx->Base.ReadOnly = Config->ReadOnly;
    ctx->Base.Operations = &g_MemoryBackendOps;
    KeInitializeSpinLock(&ctx->Base.Lock);
    
    *Backend = (PVNVME_BACKEND)ctx;
    
    return STATUS_SUCCESS;
}

//
// 获取后端信息
//
static NTSTATUS MemoryBackendGetInfo(
    _In_ PVNVME_BACKEND Backend,
    _Out_ PVNVME_BACKEND_INFO Info)
{
    PMEMORY_BACKEND_CONTEXT ctx = (PMEMORY_BACKEND_CONTEXT)Backend;
    
    RtlZeroMemory(Info, sizeof(*Info));
    
    Info->Type = VNVME_BACKEND_MEMORY;
    Info->Capabilities = ctx->Base.Capabilities;
    Info->TotalSize = ctx->Base.TotalSize;
    Info->UsedSize = ctx->BufferSize;  // 内存后端全部预分配
    Info->BlockSize = ctx->Base.BlockSize;
    Info->OptimalTransferSize = 64 * 1024;  // 64KB
    Info->ReadOnly = ctx->Base.ReadOnly;
    Info->Sparse = FALSE;
    
    RtlStringCbCopyW(Info->Description, sizeof(Info->Description), L"Memory Backend");
    
    return STATUS_SUCCESS;
}

//
// 同步读取
//
static NTSTATUS MemoryBackendRead(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer)
{
    PMEMORY_BACKEND_CONTEXT ctx = (PMEMORY_BACKEND_CONTEXT)Backend;
    
    // 边界检查
    if (Offset + Length > ctx->BufferSize) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 获取共享锁 (允许并发读)
    ExAcquireResourceSharedLite(&ctx->Lock, TRUE);
    
    RtlCopyMemory(Buffer, (PUCHAR)ctx->Buffer + Offset, Length);
    
    ExReleaseResourceLite(&ctx->Lock);
    
    return STATUS_SUCCESS;
}

//
// 同步写入
//
static NTSTATUS MemoryBackendWrite(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer)
{
    PMEMORY_BACKEND_CONTEXT ctx = (PMEMORY_BACKEND_CONTEXT)Backend;
    
    // 只读检查
    if (ctx->Base.ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    // 边界检查
    if (Offset + Length > ctx->BufferSize) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 获取独占锁
    ExAcquireResourceExclusiveLite(&ctx->Lock, TRUE);
    
    RtlCopyMemory((PUCHAR)ctx->Buffer + Offset, Buffer, Length);
    
    ExReleaseResourceLite(&ctx->Lock);
    
    return STATUS_SUCCESS;
}

//
// 刷新 (内存后端为空操作)
//
static NTSTATUS MemoryBackendFlush(
    _In_ PVNVME_BACKEND Backend)
{
    UNREFERENCED_PARAMETER(Backend);
    
    // 内存后端无需刷新
    return STATUS_SUCCESS;
}

//
// TRIM (将指定区域清零)
//
static NTSTATUS MemoryBackendTrim(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length)
{
    PMEMORY_BACKEND_CONTEXT ctx = (PMEMORY_BACKEND_CONTEXT)Backend;
    
    // 边界检查
    if (Offset + Length > ctx->BufferSize) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 获取独占锁
    ExAcquireResourceExclusiveLite(&ctx->Lock, TRUE);
    
    RtlZeroMemory((PUCHAR)ctx->Buffer + Offset, (SIZE_T)Length);
    
    ExReleaseResourceLite(&ctx->Lock);
    
    return STATUS_SUCCESS;
}

//
// 关闭后端
//
static VOID MemoryBackendClose(
    _In_ PVNVME_BACKEND Backend)
{
    PMEMORY_BACKEND_CONTEXT ctx = (PMEMORY_BACKEND_CONTEXT)Backend;
    
    if (ctx->Buffer) {
        ExFreePoolWithTag(ctx->Buffer, MEMORY_BACKEND_TAG);
    }
    
    ExDeleteResourceLite(&ctx->Lock);
    ExFreePoolWithTag(ctx, MEMORY_BACKEND_TAG);
}

// 内存后端操作表
VNVME_BACKEND_OPS g_MemoryBackendOps = {
    .Initialize = MemoryBackendInitialize,
    .GetInfo    = MemoryBackendGetInfo,
    .Read       = MemoryBackendRead,
    .Write      = MemoryBackendWrite,
    .ReadAsync  = NULL,  // 可选实现
    .WriteAsync = NULL,
    .Flush      = MemoryBackendFlush,
    .Trim       = MemoryBackendTrim,
    .Resize     = NULL,  // 内存后端不支持动态调整
    .Close      = MemoryBackendClose,
};
```

---

## 文件后端实现

### 特性

- **持久性**: 持久（数据存储在本地文件）
- **性能**: 中等（受文件系统影响）
- **适用场景**: 生产环境、需要数据持久化

### vnvme_backend_file.c

```c
//
// 文件后端实现
//

#include "vnvme.h"
#include "vnvme_backend.h"

#define FILE_BACKEND_TAG 'fBNV'

// 文件后端上下文
typedef struct _FILE_BACKEND_CONTEXT {
    VNVME_BACKEND       Base;           // 基础结构
    HANDLE              FileHandle;     // 文件句柄
    PFILE_OBJECT        FileObject;     // 文件对象
    UNICODE_STRING      FilePath;       // 文件路径
    BOOLEAN             NoBuffering;    // 直接 I/O
    BOOLEAN             WriteThrough;   // 写透模式
    ERESOURCE           Lock;           // 读写锁
} FILE_BACKEND_CONTEXT, *PFILE_BACKEND_CONTEXT;

//
// 初始化文件后端
//
static NTSTATUS FileBackendInitialize(
    _Out_ PVNVME_BACKEND* Backend,
    _In_ PVNVME_BACKEND_CONFIG Config)
{
    PFILE_BACKEND_CONTEXT ctx = NULL;
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER allocSize;
    ULONG createOptions;
    ULONG desiredAccess;
    ULONG createDisposition;
    FILE_STANDARD_INFORMATION fileInfo;
    
    // 验证参数
    if (!Config->File.FilePath.Buffer || Config->File.FilePath.Length == 0) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 分配上下文
    ctx = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(FILE_BACKEND_CONTEXT),
        FILE_BACKEND_TAG);
    
    if (!ctx) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(ctx, sizeof(FILE_BACKEND_CONTEXT));
    
    // 初始化读写锁
    status = ExInitializeResourceLite(&ctx->Lock);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    
    // 复制文件路径
    ctx->FilePath.Length = Config->File.FilePath.Length;
    ctx->FilePath.MaximumLength = Config->File.FilePath.Length + sizeof(WCHAR);
    ctx->FilePath.Buffer = ExAllocatePool2(
        POOL_FLAG_PAGED,
        ctx->FilePath.MaximumLength,
        FILE_BACKEND_TAG);
    
    if (!ctx->FilePath.Buffer) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    
    RtlCopyUnicodeString(&ctx->FilePath, &Config->File.FilePath);
    
    // 设置文件属性
    InitializeObjectAttributes(
        &objAttr,
        &ctx->FilePath,
        OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
        NULL,
        NULL);
    
    // 创建选项
    createOptions = FILE_NON_DIRECTORY_FILE | FILE_RANDOM_ACCESS;
    
    if (Config->File.NoBuffering) {
        createOptions |= FILE_NO_INTERMEDIATE_BUFFERING;
        ctx->NoBuffering = TRUE;
    }
    
    if (Config->File.WriteThrough) {
        createOptions |= FILE_WRITE_THROUGH;
        ctx->WriteThrough = TRUE;
    }
    
    // 访问权限
    desiredAccess = Config->ReadOnly ? 
        GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    
    // 创建或打开
    createDisposition = Config->CreateNew ? FILE_OVERWRITE_IF : FILE_OPEN_IF;
    
    allocSize.QuadPart = Config->Size;
    
    status = ZwCreateFile(
        &ctx->FileHandle,
        desiredAccess,
        &objAttr,
        &ioStatus,
        &allocSize,
        FILE_ATTRIBUTE_NORMAL,
        0,  // 独占访问
        createDisposition,
        createOptions,
        NULL,
        0);
    
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    
    // 获取文件对象 (用于异步 I/O)
    status = ObReferenceObjectByHandle(
        ctx->FileHandle,
        0,
        *IoFileObjectType,
        KernelMode,
        (PVOID*)&ctx->FileObject,
        NULL);
    
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    
    // 如果是新创建的文件，设置大小
    if (Config->CreateNew && Config->Size > 0) {
        FILE_END_OF_FILE_INFORMATION eofInfo;
        eofInfo.EndOfFile.QuadPart = Config->Size;
        
        status = ZwSetInformationFile(
            ctx->FileHandle,
            &ioStatus,
            &eofInfo,
            sizeof(eofInfo),
            FileEndOfFileInformation);
        
        if (!NT_SUCCESS(status)) {
            goto Cleanup;
        }
        
        ctx->Base.TotalSize = Config->Size;
    }
    else {
        // 获取现有文件大小
        status = ZwQueryInformationFile(
            ctx->FileHandle,
            &ioStatus,
            &fileInfo,
            sizeof(fileInfo),
            FileStandardInformation);
        
        if (!NT_SUCCESS(status)) {
            goto Cleanup;
        }
        
        ctx->Base.TotalSize = fileInfo.EndOfFile.QuadPart;
    }
    
    // 如果是稀疏文件，设置稀疏属性
    if (Config->File.SparseFile) {
        FILE_SET_SPARSE_BUFFER sparseBuffer = { TRUE };
        
        status = ZwFsControlFile(
            ctx->FileHandle,
            NULL,
            NULL,
            NULL,
            &ioStatus,
            FSCTL_SET_SPARSE,
            &sparseBuffer,
            sizeof(sparseBuffer),
            NULL,
            0);
        
        // 忽略不支持稀疏文件的文件系统
        if (status == STATUS_INVALID_DEVICE_REQUEST) {
            status = STATUS_SUCCESS;
        }
    }
    
    // 填充基础结构
    ctx->Base.Type = VNVME_BACKEND_FILE;
    ctx->Base.Capabilities = VNVME_BACKEND_CAP_READ | VNVME_BACKEND_CAP_WRITE |
                             VNVME_BACKEND_CAP_FLUSH | VNVME_BACKEND_CAP_FUA |
                             VNVME_BACKEND_CAP_PERSISTENT | VNVME_BACKEND_CAP_RESIZE;
    ctx->Base.BlockSize = Config->BlockSize ? Config->BlockSize : 512;
    ctx->Base.ReadOnly = Config->ReadOnly;
    ctx->Base.Operations = &g_FileBackendOps;
    KeInitializeSpinLock(&ctx->Base.Lock);
    
    *Backend = (PVNVME_BACKEND)ctx;
    
    return STATUS_SUCCESS;
    
Cleanup:
    if (ctx) {
        if (ctx->FileObject) {
            ObDereferenceObject(ctx->FileObject);
        }
        if (ctx->FileHandle) {
            ZwClose(ctx->FileHandle);
        }
        if (ctx->FilePath.Buffer) {
            ExFreePoolWithTag(ctx->FilePath.Buffer, FILE_BACKEND_TAG);
        }
        ExDeleteResourceLite(&ctx->Lock);
        ExFreePoolWithTag(ctx, FILE_BACKEND_TAG);
    }
    return status;
}

//
// 同步读取
//
static NTSTATUS FileBackendRead(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer)
{
    PFILE_BACKEND_CONTEXT ctx = (PFILE_BACKEND_CONTEXT)Backend;
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    
    // 边界检查
    if (Offset + Length > ctx->Base.TotalSize) {
        return STATUS_INVALID_PARAMETER;
    }
    
    byteOffset.QuadPart = Offset;
    
    // 获取共享锁
    ExAcquireResourceSharedLite(&ctx->Lock, TRUE);
    
    status = ZwReadFile(
        ctx->FileHandle,
        NULL,           // Event
        NULL,           // APC routine
        NULL,           // APC context
        &ioStatus,
        Buffer,
        Length,
        &byteOffset,
        NULL);          // Key
    
    ExReleaseResourceLite(&ctx->Lock);
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    if (ioStatus.Information != Length) {
        return STATUS_END_OF_FILE;
    }
    
    return STATUS_SUCCESS;
}

//
// 同步写入
//
static NTSTATUS FileBackendWrite(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer)
{
    PFILE_BACKEND_CONTEXT ctx = (PFILE_BACKEND_CONTEXT)Backend;
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    
    // 只读检查
    if (ctx->Base.ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    // 边界检查
    if (Offset + Length > ctx->Base.TotalSize) {
        return STATUS_INVALID_PARAMETER;
    }
    
    byteOffset.QuadPart = Offset;
    
    // 获取独占锁
    ExAcquireResourceExclusiveLite(&ctx->Lock, TRUE);
    
    status = ZwWriteFile(
        ctx->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        Buffer,
        Length,
        &byteOffset,
        NULL);
    
    ExReleaseResourceLite(&ctx->Lock);
    
    return status;
}

//
// 刷新到磁盘
//
static NTSTATUS FileBackendFlush(
    _In_ PVNVME_BACKEND Backend)
{
    PFILE_BACKEND_CONTEXT ctx = (PFILE_BACKEND_CONTEXT)Backend;
    IO_STATUS_BLOCK ioStatus;
    
    return ZwFlushBuffersFile(ctx->FileHandle, &ioStatus);
}

//
// TRIM (设置文件区域为零 - 使用稀疏文件特性)
//
static NTSTATUS FileBackendTrim(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length)
{
    PFILE_BACKEND_CONTEXT ctx = (PFILE_BACKEND_CONTEXT)Backend;
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    FILE_ZERO_DATA_INFORMATION zeroData;
    
    zeroData.FileOffset.QuadPart = Offset;
    zeroData.BeyondFinalZero.QuadPart = Offset + Length;
    
    status = ZwFsControlFile(
        ctx->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        FSCTL_SET_ZERO_DATA,
        &zeroData,
        sizeof(zeroData),
        NULL,
        0);
    
    return status;
}

//
// 动态调整大小
//
static NTSTATUS FileBackendResize(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONGLONG NewSize)
{
    PFILE_BACKEND_CONTEXT ctx = (PFILE_BACKEND_CONTEXT)Backend;
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    FILE_END_OF_FILE_INFORMATION eofInfo;
    
    // 只读检查
    if (ctx->Base.ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    eofInfo.EndOfFile.QuadPart = NewSize;
    
    ExAcquireResourceExclusiveLite(&ctx->Lock, TRUE);
    
    status = ZwSetInformationFile(
        ctx->FileHandle,
        &ioStatus,
        &eofInfo,
        sizeof(eofInfo),
        FileEndOfFileInformation);
    
    if (NT_SUCCESS(status)) {
        ctx->Base.TotalSize = NewSize;
    }
    
    ExReleaseResourceLite(&ctx->Lock);
    
    return status;
}

//
// 关闭后端
//
static VOID FileBackendClose(
    _In_ PVNVME_BACKEND Backend)
{
    PFILE_BACKEND_CONTEXT ctx = (PFILE_BACKEND_CONTEXT)Backend;
    
    if (ctx->FileObject) {
        ObDereferenceObject(ctx->FileObject);
    }
    
    if (ctx->FileHandle) {
        ZwClose(ctx->FileHandle);
    }
    
    if (ctx->FilePath.Buffer) {
        ExFreePoolWithTag(ctx->FilePath.Buffer, FILE_BACKEND_TAG);
    }
    
    ExDeleteResourceLite(&ctx->Lock);
    ExFreePoolWithTag(ctx, FILE_BACKEND_TAG);
}

// 文件后端操作表
VNVME_BACKEND_OPS g_FileBackendOps = {
    .Initialize = FileBackendInitialize,
    .GetInfo    = NULL,  // TODO: 实现
    .Read       = FileBackendRead,
    .Write      = FileBackendWrite,
    .ReadAsync  = NULL,  // TODO: 使用异步 I/O
    .WriteAsync = NULL,
    .Flush      = FileBackendFlush,
    .Trim       = FileBackendTrim,
    .Resize     = FileBackendResize,
    .Close      = FileBackendClose,
};
```

---

## VHD 后端 (概念设计)

### 特性

- **持久性**: 持久
- **高级功能**: 动态扩展、差异盘、快照
- **复杂度**: 高（需要处理 VHD/VHDX 格式）

### 实现选项

#### 选项 A: 用户态服务配合

```
┌──────────────────────┐
│   vnvmectl.exe       │   1. 创建/挂载 VHD (使用 virtdisk.dll)
│   (用户态)           │   2. 获取 VHD 内部磁盘路径
│                      │   3. 通过 IOCTL 传递给驱动
└──────────┬───────────┘
           │
           ▼
┌──────────────────────┐
│   vnvme.sys          │   1. 接收 VHD 磁盘设备路径
│   (内核态)           │   2. 直接对该设备进行 I/O
└──────────────────────┘
```

#### 选项 B: 内核直接处理

- 在内核中直接解析 VHD/VHDX 格式
- 复杂度高，但无外部依赖
- 参考 Microsoft 虚拟磁盘服务文档

### VHD 后端结构 (预留)

```c
typedef struct _VHD_BACKEND_CONTEXT {
    VNVME_BACKEND       Base;
    UNICODE_STRING      VhdPath;
    ULONG               VhdType;        // Fixed/Dynamic/Differencing
    HANDLE              VhdHandle;
    // VHD 格式特定字段
    ULONGLONG           VirtualSize;
    ULONGLONG           PhysicalSize;
    ULONG               BlockSize;      // VHD 块大小
    // ... VHD header, BAT 等
} VHD_BACKEND_CONTEXT, *PVHD_BACKEND_CONTEXT;
```

---

## 后端选择建议

| 使用场景 | 推荐后端 | 原因 |
|----------|----------|------|
| 开发/测试 | Memory | 性能最高，便于调试 |
| 生产环境 (简单) | File | 持久化，配置简单 |
| 企业虚拟化 | VHD | 高级功能 (快照、差异盘) |
| 分布式存储 | Remote | 网络存储 (需额外实现) |

## 性能考虑

1. **内存后端**: 直接内存拷贝，无系统调用开销
2. **文件后端**: 
   - 使用 `FILE_NO_INTERMEDIATE_BUFFERING` 减少双重缓冲
   - 使用 `FILE_WRITE_THROUGH` 确保数据安全
   - 考虑使用异步 I/O 提高并发性能
3. **VHD 后端**: 注意 VHD 格式的块对齐，避免读改写
