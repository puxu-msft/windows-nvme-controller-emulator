# 后端存储

本文档详细说明虚拟 NVMe 控制器的存储后端实现。

## 实现状态

### 内核态存储后端

> **✅ 已实现**: 内核驱动存储后端 (`vnvme/storage.c`)
> 
> 实际实现采用简化的同步 API 设计，位于内核驱动中：
> - `VnvmeStorageCreate()` - 创建存储后端 (内存或文件)
> - `VnvmeStorageDestroy()` - 销毁存储后端
> - `VnvmeStorageRead()` / `VnvmeStorageWrite()` - 同步读写
> - `VnvmeStorageFlush()` - 刷新缓存
> - `VnvmeStorageWriteZeroes()` - 零填充
> - `VnvmeStorageGetDirect()` - 零拷贝直接访问 (仅内存后端)
> - `VnvmeStorageGetStats()` - 获取统计信息
>
> **限制**: 
> - 内存后端最大 256 MB
> - 异步 I/O 和 TRIM 尚未实现
> - VHD 后端尚未实现

### 用户态存储后端

> **✅ 已实现**: 用户态存储后端 (`vnvme-server/backend.c`)
> 
> 用于双模式架构的用户态命令处理模式：
> - `BackendCreate()` - 创建存储后端 (内存或文件)
> - `BackendDestroy()` - 销毁存储后端
> - `BackendRead()` / `BackendWrite()` - 同步读写
> - `BackendFlush()` - 刷新缓存
> - `BackendWriteZeroes()` - 零填充
> - `BackendGetSize()` - 获取后端大小
> - `BackendGetStats()` - 获取统计信息
>
> **特点**:
> - 内存后端: 使用 `VirtualAlloc` 分配
> - 文件后端: 使用 `CreateFile/ReadFile/WriteFile`
> - 无大小限制 (取决于系统内存/磁盘空间)

## 概述

存储后端负责实际的数据持久化。我们支持多种后端类型：

```
┌─────────────────────────────────────────────────────────────────────┐
│                         存储后端架构                                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│     ┌─────────────────────────────────────────────────────────┐     │
│     │                NVMe Controller Emulation                 │     │
│     │                    (vnvme_emu.sys)                       │     │
│     └─────────────────────────────────────────────────────────┘     │
│                               │                                      │
│                               ▼                                      │
│     ┌─────────────────────────────────────────────────────────┐     │
│     │              Backend Abstraction Layer                   │     │
│     │                 (VNVME_BACKEND interface)                │     │
│     └─────────────────────────────────────────────────────────┘     │
│                               │                                      │
│         ┌─────────────────────┼─────────────────────┐               │
│         │                     │                     │               │
│         ▼                     ▼                     ▼               │
│   ┌──────────┐          ┌──────────┐          ┌──────────┐          │
│   │ Memory   │          │  File    │          │   VHD    │          │
│   │ Backend  │          │ Backend  │          │ Backend  │          │
│   │ (RAM)    │          │ (.img)   │          │ (.vhdx)  │          │
│   └──────────┘          └──────────┘          └──────────┘          │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

## 后端接口

### 接口定义

```c
//
// 后端操作函数表
//
typedef struct _VNVME_BACKEND_OPERATIONS {
    //
    // 初始化后端
    //
    NTSTATUS (*Initialize)(
        _Inout_ PVNVME_BACKEND Backend,
        _In_ PVNVME_BACKEND_INIT_PARAMS Params);
    
    //
    // 关闭后端
    //
    VOID (*Shutdown)(
        _Inout_ PVNVME_BACKEND Backend);
    
    //
    // 同步读取
    //
    NTSTATUS (*Read)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG Length,
        _Out_writes_bytes_(Length) PVOID Buffer);
    
    //
    // 同步写入
    //
    NTSTATUS (*Write)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG Length,
        _In_reads_bytes_(Length) PVOID Buffer);
    
    //
    // 异步读取
    //
    NTSTATUS (*ReadAsync)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG Length,
        _Out_writes_bytes_(Length) PVOID Buffer,
        _In_ PVNVME_BACKEND_COMPLETION Completion);
    
    //
    // 异步写入
    //
    NTSTATUS (*WriteAsync)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG Length,
        _In_reads_bytes_(Length) PVOID Buffer,
        _In_ PVNVME_BACKEND_COMPLETION Completion);
    
    //
    // 刷新缓存
    //
    NTSTATUS (*Flush)(
        _In_ PVNVME_BACKEND Backend);
    
    //
    // TRIM/Deallocate
    //
    NTSTATUS (*Trim)(
        _In_ PVNVME_BACKEND Backend,
        _In_ ULONG64 Offset,
        _In_ ULONG64 Length);
    
    //
    // 获取后端信息
    //
    NTSTATUS (*GetInfo)(
        _In_ PVNVME_BACKEND Backend,
        _Out_ PVNVME_BACKEND_INFO Info);
    
} VNVME_BACKEND_OPERATIONS, *PVNVME_BACKEND_OPERATIONS;

//
// 后端初始化参数
//
typedef struct _VNVME_BACKEND_INIT_PARAMS {
    VNVME_BACKEND_TYPE Type;
    ULONG64 Capacity;            // 请求的容量 (字节)
    ULONG SectorSize;            // 扇区大小 (512 或 4096)
    BOOLEAN ReadOnly;            // 只读模式
    UNICODE_STRING Path;         // 文件路径 (文件/VHD 后端)
    BOOLEAN CreateIfNotExist;    // 如果不存在则创建
    BOOLEAN SparseFile;          // 使用稀疏文件
} VNVME_BACKEND_INIT_PARAMS, *PVNVME_BACKEND_INIT_PARAMS;

//
// 异步完成回调
//
typedef struct _VNVME_BACKEND_COMPLETION {
    PVOID Context;
    VOID (*Callback)(
        _In_ PVOID Context,
        _In_ NTSTATUS Status,
        _In_ ULONG BytesTransferred);
} VNVME_BACKEND_COMPLETION, *PVNVME_BACKEND_COMPLETION;
```

---

## 内存后端 (RAM Disk)

最简单的后端实现，数据存储在非分页内存中。适用于测试和临时存储。

### 实现

```c
//
// 内存后端私有数据
//
typedef struct _VNVME_MEMORY_BACKEND_DATA {
    PVOID Memory;           // 分配的内存
    ULONG64 Size;           // 总大小
    PMDL Mdl;              // MDL (可选，用于直接 DMA)
    EX_SPIN_LOCK Lock;     // 并发保护
} VNVME_MEMORY_BACKEND_DATA, *PVNVME_MEMORY_BACKEND_DATA;

//
// 初始化内存后端
//
NTSTATUS VnvmeMemoryBackendInit(
    _Inout_ PVNVME_BACKEND Backend,
    _In_ PVNVME_BACKEND_INIT_PARAMS Params)
{
    PVNVME_MEMORY_BACKEND_DATA data;
    NTSTATUS status = STATUS_SUCCESS;
    
    // 分配私有数据结构
    data = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                          sizeof(*data),
                          VNVME_POOL_TAG_BACKEND);
    if (!data) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(data, sizeof(*data));
    
    // 限制最大内存大小 (例如 4GB)
    if (Params->Capacity > 4ULL * 1024 * 1024 * 1024) {
        ExFreePoolWithTag(data, VNVME_POOL_TAG_BACKEND);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 分配存储内存
    // 对于大容量，使用 MmAllocateContiguousMemory 或分段分配
    data->Size = Params->Capacity;
    
    if (data->Size <= 256 * 1024 * 1024) {  // <= 256MB
        data->Memory = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                       (SIZE_T)data->Size,
                                       VNVME_POOL_TAG_BACKEND);
    } else {
        // 大内存使用 MmAllocatePagesForMdlEx
        PHYSICAL_ADDRESS lowAddr, highAddr, skipAddr;
        lowAddr.QuadPart = 0;
        highAddr.QuadPart = MAXULONG64;
        skipAddr.QuadPart = 0;
        
        data->Mdl = MmAllocatePagesForMdlEx(
            lowAddr, highAddr, skipAddr,
            (SIZE_T)data->Size,
            MmCached,
            MM_ALLOCATE_FULLY_REQUIRED);
        
        if (data->Mdl) {
            data->Memory = MmGetSystemAddressForMdlSafe(
                data->Mdl, NormalPagePriority | MdlMappingNoExecute);
        }
    }
    
    if (!data->Memory) {
        if (data->Mdl) {
            MmFreePagesFromMdl(data->Mdl);
            ExFreePool(data->Mdl);
        }
        ExFreePoolWithTag(data, VNVME_POOL_TAG_BACKEND);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 初始化为零
    RtlZeroMemory(data->Memory, (SIZE_T)data->Size);
    
    // 初始化锁
    data->Lock = 0;
    
    // 设置后端结构
    Backend->Type = VnvmeBackendMemory;
    Backend->Capacity = data->Size;
    Backend->SectorSize = Params->SectorSize;
    Backend->ReadOnly = Params->ReadOnly;
    Backend->PrivateData = data;
    
    return STATUS_SUCCESS;
}

//
// 关闭内存后端
//
VOID VnvmeMemoryBackendShutdown(
    _Inout_ PVNVME_BACKEND Backend)
{
    PVNVME_MEMORY_BACKEND_DATA data = Backend->PrivateData;
    
    if (!data) return;
    
    if (data->Mdl) {
        MmUnmapLockedPages(data->Memory, data->Mdl);
        MmFreePagesFromMdl(data->Mdl);
        ExFreePool(data->Mdl);
    } else if (data->Memory) {
        ExFreePoolWithTag(data->Memory, VNVME_POOL_TAG_BACKEND);
    }
    
    ExFreePoolWithTag(data, VNVME_POOL_TAG_BACKEND);
    Backend->PrivateData = NULL;
}

//
// 内存后端读取
//
NTSTATUS VnvmeMemoryBackendRead(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer)
{
    PVNVME_MEMORY_BACKEND_DATA data = Backend->PrivateData;
    KIRQL oldIrql;
    
    // 边界检查
    if (Offset + Length > data->Size) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 获取锁 (读操作可以用共享锁，这里简化)
    oldIrql = ExAcquireSpinLockExclusive(&data->Lock);
    
    // 复制数据
    RtlCopyMemory(Buffer, (PUCHAR)data->Memory + Offset, Length);
    
    ExReleaseSpinLockExclusive(&data->Lock, oldIrql);
    
    return STATUS_SUCCESS;
}

//
// 内存后端写入
//
NTSTATUS VnvmeMemoryBackendWrite(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer)
{
    PVNVME_MEMORY_BACKEND_DATA data = Backend->PrivateData;
    KIRQL oldIrql;
    
    if (Backend->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    if (Offset + Length > data->Size) {
        return STATUS_INVALID_PARAMETER;
    }
    
    oldIrql = ExAcquireSpinLockExclusive(&data->Lock);
    
    RtlCopyMemory((PUCHAR)data->Memory + Offset, Buffer, Length);
    
    ExReleaseSpinLockExclusive(&data->Lock, oldIrql);
    
    return STATUS_SUCCESS;
}

//
// 内存后端刷新 (无操作)
//
NTSTATUS VnvmeMemoryBackendFlush(
    _In_ PVNVME_BACKEND Backend)
{
    UNREFERENCED_PARAMETER(Backend);
    return STATUS_SUCCESS;
}

//
// 内存后端 TRIM (将区域清零)
//
NTSTATUS VnvmeMemoryBackendTrim(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    PVNVME_MEMORY_BACKEND_DATA data = Backend->PrivateData;
    KIRQL oldIrql;
    
    if (Offset + Length > data->Size) {
        Length = data->Size - Offset;
    }
    
    oldIrql = ExAcquireSpinLockExclusive(&data->Lock);
    
    RtlZeroMemory((PUCHAR)data->Memory + Offset, (SIZE_T)Length);
    
    ExReleaseSpinLockExclusive(&data->Lock, oldIrql);
    
    return STATUS_SUCCESS;
}

//
// 操作函数表
//
VNVME_BACKEND_OPERATIONS VnvmeMemoryBackendOps = {
    .Initialize = VnvmeMemoryBackendInit,
    .Shutdown   = VnvmeMemoryBackendShutdown,
    .Read       = VnvmeMemoryBackendRead,
    .Write      = VnvmeMemoryBackendWrite,
    .ReadAsync  = NULL,  // 使用同步实现
    .WriteAsync = NULL,
    .Flush      = VnvmeMemoryBackendFlush,
    .Trim       = VnvmeMemoryBackendTrim,
    .GetInfo    = VnvmeMemoryBackendGetInfo
};
```

---

## 文件后端

使用普通文件作为存储，支持稀疏文件和 TRIM。

### 实现

```c
//
// 文件后端私有数据
//
typedef struct _VNVME_FILE_BACKEND_DATA {
    HANDLE FileHandle;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    UNICODE_STRING FilePath;
    ULONG64 FileSize;
    BOOLEAN SparseFile;
    ERESOURCE Lock;
} VNVME_FILE_BACKEND_DATA, *PVNVME_FILE_BACKEND_DATA;

//
// 初始化文件后端
//
NTSTATUS VnvmeFileBackendInit(
    _Inout_ PVNVME_BACKEND Backend,
    _In_ PVNVME_BACKEND_INIT_PARAMS Params)
{
    PVNVME_FILE_BACKEND_DATA data;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    FILE_END_OF_FILE_INFORMATION eofInfo;
    FILE_STANDARD_INFORMATION stdInfo;
    NTSTATUS status;
    ULONG createDisposition;
    ULONG createOptions;
    
    data = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                          sizeof(*data),
                          VNVME_POOL_TAG_BACKEND);
    if (!data) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(data, sizeof(*data));
    ExInitializeResourceLite(&data->Lock);
    
    // 复制文件路径
    data->FilePath.Buffer = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                                           Params->Path.MaximumLength,
                                           VNVME_POOL_TAG_BACKEND);
    if (!data->FilePath.Buffer) {
        ExDeleteResourceLite(&data->Lock);
        ExFreePoolWithTag(data, VNVME_POOL_TAG_BACKEND);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlCopyUnicodeString(&data->FilePath, &Params->Path);
    data->SparseFile = Params->SparseFile;
    
    // 设置创建选项
    createDisposition = Params->CreateIfNotExist ? FILE_OPEN_IF : FILE_OPEN;
    createOptions = FILE_NON_DIRECTORY_FILE | 
                   FILE_SYNCHRONOUS_IO_NONALERT |
                   FILE_NO_INTERMEDIATE_BUFFERING;
    
    InitializeObjectAttributes(&objAttr,
                              &data->FilePath,
                              OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE,
                              NULL, NULL);
    
    // 打开/创建文件
    status = ZwCreateFile(&data->FileHandle,
                         Params->ReadOnly ? 
                             (GENERIC_READ | SYNCHRONIZE) :
                             (GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE),
                         &objAttr,
                         &ioStatus,
                         NULL,
                         FILE_ATTRIBUTE_NORMAL,
                         0,  // 不共享
                         createDisposition,
                         createOptions,
                         NULL, 0);
    
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    
    // 获取文件对象
    status = ObReferenceObjectByHandle(data->FileHandle,
                                       0,
                                       *IoFileObjectType,
                                       KernelMode,
                                       (PVOID*)&data->FileObject,
                                       NULL);
    if (!NT_SUCCESS(status)) {
        goto Cleanup;
    }
    
    data->DeviceObject = IoGetRelatedDeviceObject(data->FileObject);
    
    // 如果是新创建的文件，设置大小
    if (ioStatus.Information == FILE_CREATED) {
        // 设置为稀疏文件
        if (data->SparseFile) {
            FILE_SET_SPARSE_BUFFER sparseBuffer = { TRUE };
            status = ZwFsControlFile(data->FileHandle,
                                    NULL, NULL, NULL,
                                    &ioStatus,
                                    FSCTL_SET_SPARSE,
                                    &sparseBuffer, sizeof(sparseBuffer),
                                    NULL, 0);
            // 忽略不支持稀疏的错误
        }
        
        // 设置文件大小
        eofInfo.EndOfFile.QuadPart = Params->Capacity;
        status = ZwSetInformationFile(data->FileHandle,
                                      &ioStatus,
                                      &eofInfo,
                                      sizeof(eofInfo),
                                      FileEndOfFileInformation);
        if (!NT_SUCCESS(status)) {
            goto Cleanup;
        }
        
        data->FileSize = Params->Capacity;
    } else {
        // 获取现有文件大小
        status = ZwQueryInformationFile(data->FileHandle,
                                        &ioStatus,
                                        &stdInfo,
                                        sizeof(stdInfo),
                                        FileStandardInformation);
        if (!NT_SUCCESS(status)) {
            goto Cleanup;
        }
        
        data->FileSize = stdInfo.EndOfFile.QuadPart;
    }
    
    Backend->Type = VnvmeBackendFile;
    Backend->Capacity = data->FileSize;
    Backend->SectorSize = Params->SectorSize;
    Backend->ReadOnly = Params->ReadOnly;
    Backend->PrivateData = data;
    
    return STATUS_SUCCESS;
    
Cleanup:
    if (data->FileObject) {
        ObDereferenceObject(data->FileObject);
    }
    if (data->FileHandle) {
        ZwClose(data->FileHandle);
    }
    if (data->FilePath.Buffer) {
        ExFreePoolWithTag(data->FilePath.Buffer, VNVME_POOL_TAG_BACKEND);
    }
    ExDeleteResourceLite(&data->Lock);
    ExFreePoolWithTag(data, VNVME_POOL_TAG_BACKEND);
    
    return status;
}

//
// 文件后端读取
//
NTSTATUS VnvmeFileBackendRead(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer)
{
    PVNVME_FILE_BACKEND_DATA data = Backend->PrivateData;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    NTSTATUS status;
    
    byteOffset.QuadPart = Offset;
    
    ExAcquireResourceSharedLite(&data->Lock, TRUE);
    
    status = ZwReadFile(data->FileHandle,
                        NULL,
                        NULL, NULL,
                        &ioStatus,
                        Buffer,
                        Length,
                        &byteOffset,
                        NULL);
    
    ExReleaseResourceLite(&data->Lock);
    
    return status;
}

//
// 文件后端写入
//
NTSTATUS VnvmeFileBackendWrite(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer)
{
    PVNVME_FILE_BACKEND_DATA data = Backend->PrivateData;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    NTSTATUS status;
    
    if (Backend->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    byteOffset.QuadPart = Offset;
    
    ExAcquireResourceExclusiveLite(&data->Lock, TRUE);
    
    status = ZwWriteFile(data->FileHandle,
                         NULL,
                         NULL, NULL,
                         &ioStatus,
                         Buffer,
                         Length,
                         &byteOffset,
                         NULL);
    
    ExReleaseResourceLite(&data->Lock);
    
    return status;
}

//
// 文件后端刷新
//
NTSTATUS VnvmeFileBackendFlush(
    _In_ PVNVME_BACKEND Backend)
{
    PVNVME_FILE_BACKEND_DATA data = Backend->PrivateData;
    IO_STATUS_BLOCK ioStatus;
    
    return ZwFlushBuffersFile(data->FileHandle, &ioStatus);
}

//
// 文件后端 TRIM (使用 FSCTL_SET_ZERO_DATA)
//
NTSTATUS VnvmeFileBackendTrim(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    PVNVME_FILE_BACKEND_DATA data = Backend->PrivateData;
    IO_STATUS_BLOCK ioStatus;
    FILE_ZERO_DATA_INFORMATION zeroData;
    NTSTATUS status;
    
    if (!data->SparseFile) {
        // 非稀疏文件不支持高效 TRIM
        return STATUS_SUCCESS;
    }
    
    zeroData.FileOffset.QuadPart = Offset;
    zeroData.BeyondFinalZero.QuadPart = Offset + Length;
    
    ExAcquireResourceExclusiveLite(&data->Lock, TRUE);
    
    status = ZwFsControlFile(data->FileHandle,
                            NULL, NULL, NULL,
                            &ioStatus,
                            FSCTL_SET_ZERO_DATA,
                            &zeroData, sizeof(zeroData),
                            NULL, 0);
    
    ExReleaseResourceLite(&data->Lock);
    
    return status;
}

//
// 操作函数表
//
VNVME_BACKEND_OPERATIONS VnvmeFileBackendOps = {
    .Initialize = VnvmeFileBackendInit,
    .Shutdown   = VnvmeFileBackendShutdown,
    .Read       = VnvmeFileBackendRead,
    .Write      = VnvmeFileBackendWrite,
    .ReadAsync  = NULL,
    .WriteAsync = NULL,
    .Flush      = VnvmeFileBackendFlush,
    .Trim       = VnvmeFileBackendTrim,
    .GetInfo    = VnvmeFileBackendGetInfo
};
```

---

## VHD 后端

使用 Windows VHD/VHDX 格式，支持差分磁盘和动态扩展。

### 实现概要

```c
//
// VHD 后端使用 Windows VHD API
// 需要链接 virtdisk.lib
//

#include <virtdisk.h>

typedef struct _VNVME_VHD_BACKEND_DATA {
    HANDLE VhdHandle;
    VIRTUAL_STORAGE_TYPE StorageType;
    ULONG64 VirtualSize;
    ULONG64 PhysicalSize;
    ULONG BlockSize;
    WCHAR VhdPath[MAX_PATH];
} VNVME_VHD_BACKEND_DATA, *PVNVME_VHD_BACKEND_DATA;

//
// 打开 VHD
//
NTSTATUS VnvmeVhdBackendInit(
    _Inout_ PVNVME_BACKEND Backend,
    _In_ PVNVME_BACKEND_INIT_PARAMS Params)
{
    PVNVME_VHD_BACKEND_DATA data;
    OPEN_VIRTUAL_DISK_PARAMETERS openParams;
    VIRTUAL_DISK_ACCESS_MASK accessMask;
    DWORD result;
    
    data = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                          sizeof(*data),
                          VNVME_POOL_TAG_BACKEND);
    if (!data) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(data, sizeof(*data));
    
    // 设置存储类型
    data->StorageType.DeviceId = VIRTUAL_STORAGE_TYPE_DEVICE_VHDX;
    data->StorageType.VendorId = VIRTUAL_STORAGE_TYPE_VENDOR_MICROSOFT;
    
    // 设置访问掩码
    accessMask = Params->ReadOnly ? 
        VIRTUAL_DISK_ACCESS_READ :
        (VIRTUAL_DISK_ACCESS_READ | VIRTUAL_DISK_ACCESS_WRITE);
    
    // 打开参数
    RtlZeroMemory(&openParams, sizeof(openParams));
    openParams.Version = OPEN_VIRTUAL_DISK_VERSION_2;
    openParams.Version2.GetInfoOnly = FALSE;
    openParams.Version2.ReadOnly = Params->ReadOnly;
    
    // 复制路径
    RtlCopyMemory(data->VhdPath, 
                  Params->Path.Buffer,
                  min(Params->Path.Length, sizeof(data->VhdPath) - 2));
    
    // 打开 VHD
    // 注意：在内核模式下需要使用不同的 API
    // 这里展示用户模式概念，实际内核实现需要调整
    result = OpenVirtualDisk(&data->StorageType,
                            data->VhdPath,
                            accessMask,
                            OPEN_VIRTUAL_DISK_FLAG_NONE,
                            &openParams,
                            &data->VhdHandle);
    
    if (result != ERROR_SUCCESS) {
        ExFreePoolWithTag(data, VNVME_POOL_TAG_BACKEND);
        return STATUS_UNSUCCESSFUL;
    }
    
    // 获取 VHD 信息
    GET_VIRTUAL_DISK_INFO vhdInfo;
    ULONG infoSize = sizeof(vhdInfo);
    
    vhdInfo.Version = GET_VIRTUAL_DISK_INFO_SIZE;
    result = GetVirtualDiskInformation(data->VhdHandle,
                                       &infoSize,
                                       &vhdInfo,
                                       NULL);
    
    if (result == ERROR_SUCCESS) {
        data->VirtualSize = vhdInfo.Size.VirtualSize;
        data->PhysicalSize = vhdInfo.Size.PhysicalSize;
        data->BlockSize = vhdInfo.Size.BlockSize;
    }
    
    Backend->Type = VnvmeBackendVhd;
    Backend->Capacity = data->VirtualSize;
    Backend->SectorSize = Params->SectorSize;
    Backend->ReadOnly = Params->ReadOnly;
    Backend->PrivateData = data;
    
    return STATUS_SUCCESS;
}

//
// VHD 读写通过 Attach 后访问物理磁盘路径
// 或使用 raw VHD API
//
```

---

## 后端工厂

### 创建后端

```c
//
// 根据类型创建后端
//
NTSTATUS VnvmeCreateBackend(
    _In_ PVNVME_BACKEND_INIT_PARAMS Params,
    _Out_ PVNVME_BACKEND* Backend)
{
    PVNVME_BACKEND backend;
    PVNVME_BACKEND_OPERATIONS ops;
    NTSTATUS status;
    
    // 分配后端结构
    backend = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                             sizeof(*backend),
                             VNVME_POOL_TAG_BACKEND);
    if (!backend) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(backend, sizeof(*backend));
    
    // 选择操作函数表
    switch (Params->Type) {
    case VnvmeBackendMemory:
        ops = &VnvmeMemoryBackendOps;
        break;
        
    case VnvmeBackendFile:
        ops = &VnvmeFileBackendOps;
        break;
        
    case VnvmeBackendVhd:
        ops = &VnvmeVhdBackendOps;
        break;
        
    default:
        ExFreePoolWithTag(backend, VNVME_POOL_TAG_BACKEND);
        return STATUS_NOT_SUPPORTED;
    }
    
    backend->Operations = ops;
    
    // 初始化
    status = ops->Initialize(backend, Params);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(backend, VNVME_POOL_TAG_BACKEND);
        return status;
    }
    
    *Backend = backend;
    return STATUS_SUCCESS;
}

//
// 销毁后端
//
VOID VnvmeDestroyBackend(
    _In_ PVNVME_BACKEND Backend)
{
    if (!Backend) return;
    
    if (Backend->Operations && Backend->Operations->Shutdown) {
        Backend->Operations->Shutdown(Backend);
    }
    
    ExFreePoolWithTag(Backend, VNVME_POOL_TAG_BACKEND);
}
```

---

## 异步 I/O 支持

### 异步操作框架

```c
//
// 异步 I/O 请求
//
typedef struct _VNVME_ASYNC_IO_REQUEST {
    LIST_ENTRY ListEntry;
    PVNVME_BACKEND Backend;
    BOOLEAN IsRead;
    ULONG64 Offset;
    ULONG Length;
    PVOID Buffer;
    VNVME_BACKEND_COMPLETION Completion;
    PIO_WORKITEM WorkItem;
} VNVME_ASYNC_IO_REQUEST, *PVNVME_ASYNC_IO_REQUEST;

//
// 异步 I/O 工作项回调
//
VOID VnvmeAsyncIoWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PVNVME_ASYNC_IO_REQUEST request = Context;
    NTSTATUS status;
    
    UNREFERENCED_PARAMETER(DeviceObject);
    
    // 执行同步 I/O
    if (request->IsRead) {
        status = request->Backend->Operations->Read(
            request->Backend,
            request->Offset,
            request->Length,
            request->Buffer);
    } else {
        status = request->Backend->Operations->Write(
            request->Backend,
            request->Offset,
            request->Length,
            request->Buffer);
    }
    
    // 调用完成回调
    if (request->Completion.Callback) {
        request->Completion.Callback(
            request->Completion.Context,
            status,
            NT_SUCCESS(status) ? request->Length : 0);
    }
    
    // 清理
    IoFreeWorkItem(request->WorkItem);
    ExFreePoolWithTag(request, VNVME_POOL_TAG_BACKEND);
}

//
// 通用异步读取实现
//
NTSTATUS VnvmeBackendReadAsync(
    _In_ PVNVME_BACKEND Backend,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ PVNVME_BACKEND_COMPLETION Completion,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PVNVME_ASYNC_IO_REQUEST request;
    
    // 如果后端有自己的异步实现，使用它
    if (Backend->Operations->ReadAsync) {
        return Backend->Operations->ReadAsync(
            Backend, Offset, Length, Buffer, Completion);
    }
    
    // 否则使用工作项模拟异步
    request = ExAllocatePool2(POOL_FLAG_NON_PAGED,
                             sizeof(*request),
                             VNVME_POOL_TAG_BACKEND);
    if (!request) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    request->Backend = Backend;
    request->IsRead = TRUE;
    request->Offset = Offset;
    request->Length = Length;
    request->Buffer = Buffer;
    request->Completion = *Completion;
    
    request->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (!request->WorkItem) {
        ExFreePoolWithTag(request, VNVME_POOL_TAG_BACKEND);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    IoQueueWorkItem(request->WorkItem,
                   VnvmeAsyncIoWorker,
                   DelayedWorkQueue,
                   request);
    
    return STATUS_PENDING;
}
```

---

## 性能优化

### 缓存层

```c
//
// 简单的块缓存
//
typedef struct _VNVME_CACHE_ENTRY {
    LIST_ENTRY LruEntry;
    ULONG64 BlockNumber;
    PVOID Data;
    BOOLEAN Dirty;
    ULONG RefCount;
} VNVME_CACHE_ENTRY, *PVNVME_CACHE_ENTRY;

typedef struct _VNVME_CACHE {
    PVNVME_BACKEND Backend;
    ULONG BlockSize;
    ULONG MaxEntries;
    ULONG CurrentEntries;
    LIST_ENTRY LruList;
    RTL_GENERIC_TABLE BlockTable;
    ERESOURCE Lock;
} VNVME_CACHE, *PVNVME_CACHE;

//
// 带缓存的读取
//
NTSTATUS VnvmeCachedRead(
    _In_ PVNVME_CACHE Cache,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer)
{
    ULONG64 startBlock = Offset / Cache->BlockSize;
    ULONG64 endBlock = (Offset + Length - 1) / Cache->BlockSize;
    NTSTATUS status = STATUS_SUCCESS;
    
    ExAcquireResourceSharedLite(&Cache->Lock, TRUE);
    
    for (ULONG64 block = startBlock; block <= endBlock; block++) {
        PVNVME_CACHE_ENTRY entry = VnvmeCacheLookup(Cache, block);
        
        if (!entry) {
            // 缓存未命中，从后端读取
            ExReleaseResourceLite(&Cache->Lock);
            
            ExAcquireResourceExclusiveLite(&Cache->Lock, TRUE);
            entry = VnvmeCacheAllocateEntry(Cache, block);
            
            if (entry) {
                status = Cache->Backend->Operations->Read(
                    Cache->Backend,
                    block * Cache->BlockSize,
                    Cache->BlockSize,
                    entry->Data);
                
                if (!NT_SUCCESS(status)) {
                    VnvmeCacheFreeEntry(Cache, entry);
                    entry = NULL;
                }
            }
            
            ExConvertExclusiveToSharedLite(&Cache->Lock);
        }
        
        if (entry) {
            // 从缓存复制数据
            ULONG64 blockStart = block * Cache->BlockSize;
            ULONG64 copyStart = max(Offset, blockStart);
            ULONG64 copyEnd = min(Offset + Length, blockStart + Cache->BlockSize);
            ULONG copyLen = (ULONG)(copyEnd - copyStart);
            
            RtlCopyMemory(
                (PUCHAR)Buffer + (copyStart - Offset),
                (PUCHAR)entry->Data + (copyStart - blockStart),
                copyLen);
            
            // 更新 LRU
            RemoveEntryList(&entry->LruEntry);
            InsertHeadList(&Cache->LruList, &entry->LruEntry);
        }
    }
    
    ExReleaseResourceLite(&Cache->Lock);
    
    return status;
}
```
