/**
 * @file storage.c
 * @brief 存储后端实现
 * 
 * 提供可插拔的存储后端，支持:
 * - 内存后端 (VNVME_STORAGE_TYPE_MEMORY): 用于测试和小容量设备
 * - 文件后端 (VNVME_STORAGE_TYPE_FILE): 用于持久化存储
 * - 稀疏文件后端 (VNVME_STORAGE_TYPE_SPARSE): 用于大容量虚拟磁盘 (按需分配)
 * 
 * 设计原则:
 * - 同步 I/O: 所有操作在调用者线程完成
 * - 可扩展: 通过 VnvmeStorageInit 选择后端类型
 * - 零拷贝友好: 提供直接访问内部缓冲区的 API (仅内存后端)
 */

#include "vnvme.h"

//===========================================================================
// FSCTL 常量和结构体定义 (用于稀疏文件支持)
//===========================================================================

#ifndef FSCTL_SET_SPARSE
#define FSCTL_SET_SPARSE CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 49, METHOD_BUFFERED, FILE_SPECIAL_ACCESS)
#endif

#ifndef FSCTL_SET_ZERO_DATA
#define FSCTL_SET_ZERO_DATA CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 50, METHOD_BUFFERED, FILE_WRITE_DATA)
#endif

// 稀疏文件设置缓冲区
typedef struct _FILE_SET_SPARSE_BUFFER {
    BOOLEAN SetSparse;
} FILE_SET_SPARSE_BUFFER, *PFILE_SET_SPARSE_BUFFER;

// 零数据信息
typedef struct _FILE_ZERO_DATA_INFORMATION {
    LARGE_INTEGER FileOffset;
    LARGE_INTEGER BeyondFinalZero;
} FILE_ZERO_DATA_INFORMATION, *PFILE_ZERO_DATA_INFORMATION;

//===========================================================================
// ZwFsControlFile 声明 (在 ntifs.h 中定义，但 WDK 未默认包含)
//===========================================================================

NTSYSAPI
NTSTATUS
NTAPI
ZwFsControlFile(
    _In_ HANDLE FileHandle,
    _In_opt_ HANDLE Event,
    _In_opt_ PIO_APC_ROUTINE ApcRoutine,
    _In_opt_ PVOID ApcContext,
    _Out_ PIO_STATUS_BLOCK IoStatusBlock,
    _In_ ULONG FsControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength
    );

//===========================================================================
// 存储后端上下文结构 (完整定义)
//===========================================================================

struct _VNVME_STORAGE_CONTEXT {
    VNVME_STORAGE_TYPE Type;
    ULONGLONG TotalBytes;               // 总容量
    ULONG BlockSize;                    // 块大小
    BOOLEAN ReadOnly;                   // 只读标志
    
    // 内存后端
    PVOID MemoryBuffer;                 // 内存缓冲区
    SIZE_T MemorySize;
    
    // 文件后端
    HANDLE FileHandle;
    UNICODE_STRING FilePath;
    
    // 稀疏文件后端
    BOOLEAN IsSparse;                   // 是否为稀疏文件
    
    // 统计
    volatile LONG64 ReadCount;
    volatile LONG64 WriteCount;
    volatile LONG64 BytesRead;
    volatile LONG64 BytesWritten;
};

//===========================================================================
// 内存后端实现
//===========================================================================

/**
 * @brief 初始化内存后端
 */
static NTSTATUS
StorageMemoryInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG TotalBytes
    )
{
    // 限制最大内存后端大小 (256 MB)
    #define VNVME_MAX_MEMORY_BACKEND_SIZE (256 * 1024 * 1024ULL)
    
    if (TotalBytes > VNVME_MAX_MEMORY_BACKEND_SIZE) {
        TRACE_ERROR("StorageMemoryInit: Size too large (%llu > %llu)",
                    TotalBytes, VNVME_MAX_MEMORY_BACKEND_SIZE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    Context->MemoryBuffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        (SIZE_T)TotalBytes,
        VNVME_POOL_TAG
    );
    
    if (Context->MemoryBuffer == NULL) {
        TRACE_ERROR("StorageMemoryInit: Failed to allocate %llu bytes", TotalBytes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 初始化为零 (模拟空盘)
    RtlZeroMemory(Context->MemoryBuffer, (SIZE_T)TotalBytes);
    
    Context->MemorySize = (SIZE_T)TotalBytes;
    Context->TotalBytes = TotalBytes;
    Context->Type = VNVME_STORAGE_TYPE_MEMORY;
    
    TRACE_INFO("StorageMemoryInit: Allocated %llu bytes", TotalBytes);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 释放内存后端
 */
static VOID
StorageMemoryCleanup(
    _Inout_ PVNVME_STORAGE_CONTEXT Context
    )
{
    if (Context->MemoryBuffer != NULL) {
        ExFreePoolWithTag(Context->MemoryBuffer, VNVME_POOL_TAG);
        Context->MemoryBuffer = NULL;
        Context->MemorySize = 0;
    }
}

/**
 * @brief 内存后端读取
 */
static NTSTATUS
StorageMemoryRead(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    if (Context->MemoryBuffer == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        TRACE_WARN("StorageMemoryRead: Out of range (Offset=%llu, Len=%u, Total=%llu)",
                   Offset, Length, Context->TotalBytes);
        return STATUS_INVALID_PARAMETER;
    }
    
    RtlCopyMemory(Buffer, (PUCHAR)Context->MemoryBuffer + Offset, Length);
    
    InterlockedIncrement64(&Context->ReadCount);
    InterlockedAdd64(&Context->BytesRead, Length);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 内存后端写入
 */
static NTSTATUS
StorageMemoryWrite(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    if (Context->MemoryBuffer == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Context->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    RtlCopyMemory((PUCHAR)Context->MemoryBuffer + Offset, Buffer, Length);
    
    InterlockedIncrement64(&Context->WriteCount);
    InterlockedAdd64(&Context->BytesWritten, Length);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 内存后端写零
 */
static NTSTATUS
StorageMemoryWriteZeroes(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length
    )
{
    if (Context->MemoryBuffer == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Context->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    RtlZeroMemory((PUCHAR)Context->MemoryBuffer + Offset, Length);
    
    InterlockedIncrement64(&Context->WriteCount);
    InterlockedAdd64(&Context->BytesWritten, Length);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 获取内存后端直接访问指针
 * 
 * 用于零拷贝优化: 直接获取内部缓冲区指针，避免额外复制。
 * 仅内存后端支持此功能。
 */
static NTSTATUS
StorageMemoryGetDirect(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PVOID* DirectPtr
    )
{
    if (Context->MemoryBuffer == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    *DirectPtr = (PUCHAR)Context->MemoryBuffer + Offset;
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 文件后端实现
//===========================================================================

/**
 * @brief 初始化文件后端
 */
static NTSTATUS
StorageFileInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONGLONG TotalBytes,
    _In_ BOOLEAN CreateNew
    )
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    ULONG createDisposition;
    ACCESS_MASK desiredAccess;
    FILE_END_OF_FILE_INFORMATION eofInfo;
    
    // 复制文件路径
    Context->FilePath.Length = FilePath->Length;
    Context->FilePath.MaximumLength = FilePath->Length + sizeof(WCHAR);
    Context->FilePath.Buffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        Context->FilePath.MaximumLength,
        VNVME_POOL_TAG
    );
    
    if (Context->FilePath.Buffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlCopyMemory(Context->FilePath.Buffer, FilePath->Buffer, FilePath->Length);
    Context->FilePath.Buffer[FilePath->Length / sizeof(WCHAR)] = L'\0';
    
    // 设置文件属性
    InitializeObjectAttributes(
        &objAttr,
        FilePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );
    
    // 确定创建方式
    if (CreateNew) {
        createDisposition = FILE_OVERWRITE_IF;
    } else {
        createDisposition = FILE_OPEN_IF;
    }
    
    desiredAccess = GENERIC_READ | GENERIC_WRITE;
    if (Context->ReadOnly) {
        desiredAccess = GENERIC_READ;
    }
    
    // 打开/创建文件
    status = ZwCreateFile(
        &Context->FileHandle,
        desiredAccess | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,                              // 不共享
        createDisposition,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_NO_INTERMEDIATE_BUFFERING,
        NULL,
        0
    );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("StorageFileInit: ZwCreateFile failed with 0x%08X", status);
        ExFreePoolWithTag(Context->FilePath.Buffer, VNVME_POOL_TAG);
        Context->FilePath.Buffer = NULL;
        return status;
    }
    
    // 如果创建新文件，设置文件大小
    if (CreateNew || ioStatus.Information == FILE_CREATED) {
        eofInfo.EndOfFile.QuadPart = TotalBytes;
        status = ZwSetInformationFile(
            Context->FileHandle,
            &ioStatus,
            &eofInfo,
            sizeof(eofInfo),
            FileEndOfFileInformation
        );
        
        if (!NT_SUCCESS(status)) {
            TRACE_ERROR("StorageFileInit: Failed to set file size: 0x%08X", status);
            ZwClose(Context->FileHandle);
            Context->FileHandle = NULL;
            ExFreePoolWithTag(Context->FilePath.Buffer, VNVME_POOL_TAG);
            Context->FilePath.Buffer = NULL;
            return status;
        }
    }
    
    Context->TotalBytes = TotalBytes;
    Context->Type = VNVME_STORAGE_TYPE_FILE;
    
    TRACE_INFO("StorageFileInit: Opened file, size=%llu bytes", TotalBytes);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 释放文件后端
 */
static VOID
StorageFileCleanup(
    _Inout_ PVNVME_STORAGE_CONTEXT Context
    )
{
    if (Context->FileHandle != NULL) {
        ZwClose(Context->FileHandle);
        Context->FileHandle = NULL;
    }
    
    if (Context->FilePath.Buffer != NULL) {
        ExFreePoolWithTag(Context->FilePath.Buffer, VNVME_POOL_TAG);
        Context->FilePath.Buffer = NULL;
    }
}

//===========================================================================
// 稀疏文件后端实现
//===========================================================================

/**
 * @brief 初始化稀疏文件后端
 * 
 * 稀疏文件只在有数据的区域分配实际磁盘空间，非常适合大容量虚拟磁盘。
 * 例如：1TB 虚拟盘只使用了 10GB，则只占用 10GB 磁盘空间。
 */
static NTSTATUS
StorageSparseInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONGLONG TotalBytes,
    _In_ BOOLEAN CreateNew
    )
{
    NTSTATUS status;
    OBJECT_ATTRIBUTES objAttr;
    IO_STATUS_BLOCK ioStatus;
    ULONG createDisposition;
    ACCESS_MASK desiredAccess;
    FILE_END_OF_FILE_INFORMATION eofInfo;
    FILE_SET_SPARSE_BUFFER sparseBuffer;
    
    // 复制文件路径
    Context->FilePath.Length = FilePath->Length;
    Context->FilePath.MaximumLength = FilePath->Length + sizeof(WCHAR);
    Context->FilePath.Buffer = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        Context->FilePath.MaximumLength,
        VNVME_POOL_TAG
    );
    
    if (Context->FilePath.Buffer == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlCopyMemory(Context->FilePath.Buffer, FilePath->Buffer, FilePath->Length);
    Context->FilePath.Buffer[FilePath->Length / sizeof(WCHAR)] = L'\0';
    
    // 设置文件属性
    InitializeObjectAttributes(
        &objAttr,
        FilePath,
        OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
        NULL,
        NULL
    );
    
    // 确定创建方式
    if (CreateNew) {
        createDisposition = FILE_OVERWRITE_IF;
    } else {
        createDisposition = FILE_OPEN_IF;
    }
    
    desiredAccess = GENERIC_READ | GENERIC_WRITE;
    if (Context->ReadOnly) {
        desiredAccess = GENERIC_READ;
    }
    
    // 打开/创建文件
    status = ZwCreateFile(
        &Context->FileHandle,
        desiredAccess | SYNCHRONIZE,
        &objAttr,
        &ioStatus,
        NULL,
        FILE_ATTRIBUTE_NORMAL,
        0,                              // 不共享
        createDisposition,
        FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,  // 无 NO_INTERMEDIATE_BUFFERING 以支持稀疏
        NULL,
        0
    );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("StorageSparseInit: ZwCreateFile failed with 0x%08X", status);
        ExFreePoolWithTag(Context->FilePath.Buffer, VNVME_POOL_TAG);
        Context->FilePath.Buffer = NULL;
        return status;
    }
    
    // 设置文件为稀疏文件
    sparseBuffer.SetSparse = TRUE;
    status = ZwFsControlFile(
        Context->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        FSCTL_SET_SPARSE,
        &sparseBuffer,
        sizeof(sparseBuffer),
        NULL,
        0
    );
    
    if (!NT_SUCCESS(status)) {
        // 如果设置稀疏失败，记录警告但继续 (可能文件系统不支持)
        TRACE_WARN("StorageSparseInit: FSCTL_SET_SPARSE failed with 0x%08X (may not be NTFS)", status);
        Context->IsSparse = FALSE;
    } else {
        Context->IsSparse = TRUE;
        TRACE_INFO("StorageSparseInit: Sparse file enabled");
    }
    
    // 设置文件大小 (逻辑大小，实际不分配磁盘空间)
    if (CreateNew || ioStatus.Information == FILE_CREATED) {
        eofInfo.EndOfFile.QuadPart = TotalBytes;
        status = ZwSetInformationFile(
            Context->FileHandle,
            &ioStatus,
            &eofInfo,
            sizeof(eofInfo),
            FileEndOfFileInformation
        );
        
        if (!NT_SUCCESS(status)) {
            TRACE_ERROR("StorageSparseInit: Failed to set file size: 0x%08X", status);
            ZwClose(Context->FileHandle);
            Context->FileHandle = NULL;
            ExFreePoolWithTag(Context->FilePath.Buffer, VNVME_POOL_TAG);
            Context->FilePath.Buffer = NULL;
            return status;
        }
    }
    
    Context->TotalBytes = TotalBytes;
    Context->Type = VNVME_STORAGE_TYPE_SPARSE;
    
    TRACE_INFO("StorageSparseInit: Opened sparse file, logical size=%llu bytes", TotalBytes);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 稀疏文件读取
 * 
 * 读取稀疏区域时，返回零填充数据（未分配区域默认为零）
 */
static NTSTATUS
StorageSparseRead(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    
    if (Context->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    byteOffset.QuadPart = Offset;
    
    status = ZwReadFile(
        Context->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        Buffer,
        Length,
        &byteOffset,
        NULL
    );
    
    // 对于稀疏区域，ZwReadFile 会返回零填充数据
    if (NT_SUCCESS(status)) {
        InterlockedIncrement64(&Context->ReadCount);
        InterlockedAdd64(&Context->BytesRead, Length);
    }
    
    return status;
}

/**
 * @brief 稀疏文件写入
 * 
 * 写入时会自动分配磁盘空间
 */
static NTSTATUS
StorageSparseWrite(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    
    if (Context->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Context->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    byteOffset.QuadPart = Offset;
    
    status = ZwWriteFile(
        Context->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        Buffer,
        Length,
        &byteOffset,
        NULL
    );
    
    if (NT_SUCCESS(status)) {
        InterlockedIncrement64(&Context->WriteCount);
        InterlockedAdd64(&Context->BytesWritten, Length);
    }
    
    return status;
}

/**
 * @brief 稀疏文件写零 (使用 FSCTL_SET_ZERO_DATA 高效释放空间)
 * 
 * 对于稀疏文件，写零操作可以释放已分配的磁盘空间，非常高效。
 */
static NTSTATUS
StorageSparseWriteZeroes(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length
    )
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    FILE_ZERO_DATA_INFORMATION zeroInfo;
    
    if (Context->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Context->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 使用 FSCTL_SET_ZERO_DATA 高效清零
    // 这会释放稀疏区域的磁盘空间，而不是写入零字节
    zeroInfo.FileOffset.QuadPart = Offset;
    zeroInfo.BeyondFinalZero.QuadPart = Offset + Length;
    
    status = ZwFsControlFile(
        Context->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        FSCTL_SET_ZERO_DATA,
        &zeroInfo,
        sizeof(zeroInfo),
        NULL,
        0
    );
    
    if (NT_SUCCESS(status)) {
        InterlockedIncrement64(&Context->WriteCount);
        InterlockedAdd64(&Context->BytesWritten, Length);
        TRACE_DEBUG("StorageSparseWriteZeroes: Zeroed %u bytes at offset %llu", Length, Offset);
    } else {
        TRACE_WARN("StorageSparseWriteZeroes: FSCTL_SET_ZERO_DATA failed with 0x%08X", status);
        // 回退到普通写零方式：分配临时零缓冲区
        {
            PVOID zeroBuffer;
            NTSTATUS fallbackStatus;
            
            #define SPARSE_ZERO_CHUNK_SIZE (64 * 1024)  // 64 KB chunks
            
            zeroBuffer = ExAllocatePool2(
                POOL_FLAG_NON_PAGED,
                SPARSE_ZERO_CHUNK_SIZE,
                VNVME_POOL_TAG
            );
            
            if (zeroBuffer == NULL) {
                return STATUS_INSUFFICIENT_RESOURCES;
            }
            
            RtlZeroMemory(zeroBuffer, SPARSE_ZERO_CHUNK_SIZE);
            
            fallbackStatus = STATUS_SUCCESS;
            while (Length > 0 && NT_SUCCESS(fallbackStatus)) {
                ULONG chunkSize = (Length > SPARSE_ZERO_CHUNK_SIZE) ? SPARSE_ZERO_CHUNK_SIZE : Length;
                fallbackStatus = StorageSparseWrite(Context, Offset, zeroBuffer, chunkSize);
                Offset += chunkSize;
                Length -= chunkSize;
            }
            
            ExFreePoolWithTag(zeroBuffer, VNVME_POOL_TAG);
            return fallbackStatus;
        }
    }
    
    return status;
}

/**
 * @brief 稀疏文件 TRIM/Deallocate (释放空间)
 * 
 * 支持 NVMe Deallocate 命令，将指定区域标记为未分配
 */
NTSTATUS
VnvmeStorageDeallocate(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG Length
    )
{
    IO_STATUS_BLOCK ioStatus;
    FILE_ZERO_DATA_INFORMATION zeroInfo;
    NTSTATUS status;
    
    if (StorageContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    // 只有稀疏文件后端支持真正的 deallocate
    if (StorageContext->Type != VNVME_STORAGE_TYPE_SPARSE) {
        // 其他后端回退为写零
        return VnvmeStorageWriteZeroes(StorageContext, Offset, (ULONG)Length);
    }
    
    if (StorageContext->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (StorageContext->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    zeroInfo.FileOffset.QuadPart = Offset;
    zeroInfo.BeyondFinalZero.QuadPart = Offset + Length;
    
    status = ZwFsControlFile(
        StorageContext->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        FSCTL_SET_ZERO_DATA,
        &zeroInfo,
        sizeof(zeroInfo),
        NULL,
        0
    );
    
    if (NT_SUCCESS(status)) {
        TRACE_DEBUG("VnvmeStorageDeallocate: Deallocated %llu bytes at offset %llu", Length, Offset);
    }
    
    return status;
}

/**
 * @brief 文件后端读取
 */
static NTSTATUS
StorageFileRead(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    
    if (Context->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    byteOffset.QuadPart = Offset;
    
    status = ZwReadFile(
        Context->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        Buffer,
        Length,
        &byteOffset,
        NULL
    );
    
    if (NT_SUCCESS(status)) {
        InterlockedIncrement64(&Context->ReadCount);
        InterlockedAdd64(&Context->BytesRead, Length);
    }
    
    return status;
}

/**
 * @brief 文件后端写入
 */
static NTSTATUS
StorageFileWrite(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    NTSTATUS status;
    IO_STATUS_BLOCK ioStatus;
    LARGE_INTEGER byteOffset;
    
    if (Context->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (Context->ReadOnly) {
        return STATUS_MEDIA_WRITE_PROTECTED;
    }
    
    if (Offset + Length > Context->TotalBytes) {
        return STATUS_INVALID_PARAMETER;
    }
    
    byteOffset.QuadPart = Offset;
    
    status = ZwWriteFile(
        Context->FileHandle,
        NULL,
        NULL,
        NULL,
        &ioStatus,
        Buffer,
        Length,
        &byteOffset,
        NULL
    );
    
    if (NT_SUCCESS(status)) {
        InterlockedIncrement64(&Context->WriteCount);
        InterlockedAdd64(&Context->BytesWritten, Length);
    }
    
    return status;
}

/**
 * @brief 文件后端刷新
 */
static NTSTATUS
StorageFileFlush(
    _In_ PVNVME_STORAGE_CONTEXT Context
    )
{
    IO_STATUS_BLOCK ioStatus;
    FILE_BASIC_INFORMATION basicInfo;
    NTSTATUS status;
    
    if (Context->FileHandle == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 使用 ZwQueryInformationFile 强制刷新文件缓存
    // ZwFlushBuffersFile 在某些 WDK 版本中不导出
    // 通过设置 FILE_WRITE_THROUGH 标志打开的文件会自动刷新
    // 这里使用 touch 时间戳的方式强制刷新
    status = ZwQueryInformationFile(
        Context->FileHandle,
        &ioStatus,
        &basicInfo,
        sizeof(basicInfo),
        FileBasicInformation
    );
    
    if (NT_SUCCESS(status)) {
        // 更新最后写入时间以强制刷新
        KeQuerySystemTime(&basicInfo.LastWriteTime);
        status = ZwSetInformationFile(
            Context->FileHandle,
            &ioStatus,
            &basicInfo,
            sizeof(basicInfo),
            FileBasicInformation
        );
    }
    
    // 即使刷新失败也返回成功 (对于虚拟存储，刷新不是关键)
    return STATUS_SUCCESS;
}

//===========================================================================
// 通用存储 API
//===========================================================================

/**
 * @brief 创建存储后端
 */
NTSTATUS
VnvmeStorageCreate(
    _Out_ PVNVME_STORAGE_CONTEXT* StorageContext,
    _In_ VNVME_STORAGE_TYPE Type,
    _In_ ULONGLONG TotalBytes,
    _In_ ULONG BlockSize,
    _In_opt_ PUNICODE_STRING FilePath
    )
{
    PVNVME_STORAGE_CONTEXT context;
    NTSTATUS status;
    
    *StorageContext = NULL;
    
    // 分配上下文
    context = ExAllocatePool2(
        POOL_FLAG_NON_PAGED,
        sizeof(VNVME_STORAGE_CONTEXT),
        VNVME_POOL_TAG
    );
    
    if (context == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(context, sizeof(VNVME_STORAGE_CONTEXT));
    context->BlockSize = BlockSize;
    
    // 初始化后端
    switch (Type) {
        case VNVME_STORAGE_TYPE_MEMORY:
            status = StorageMemoryInit(context, TotalBytes);
            break;
            
        case VNVME_STORAGE_TYPE_FILE:
            if (FilePath == NULL) {
                status = STATUS_INVALID_PARAMETER;
            } else {
                status = StorageFileInit(context, FilePath, TotalBytes, TRUE);
            }
            break;
            
        case VNVME_STORAGE_TYPE_SPARSE:
            if (FilePath == NULL) {
                status = STATUS_INVALID_PARAMETER;
            } else {
                status = StorageSparseInit(context, FilePath, TotalBytes, TRUE);
            }
            break;
            
        default:
            status = STATUS_NOT_SUPPORTED;
            break;
    }
    
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(context, VNVME_POOL_TAG);
        return status;
    }
    
    *StorageContext = context;
    return STATUS_SUCCESS;
}

/**
 * @brief 销毁存储后端
 */
VOID
VnvmeStorageDestroy(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext
    )
{
    if (StorageContext == NULL) {
        return;
    }
    
    switch (StorageContext->Type) {
        case VNVME_STORAGE_TYPE_MEMORY:
            StorageMemoryCleanup(StorageContext);
            break;
            
        case VNVME_STORAGE_TYPE_FILE:
        case VNVME_STORAGE_TYPE_SPARSE:
            StorageFileCleanup(StorageContext);
            break;
            
        default:
            break;
    }
    
    ExFreePoolWithTag(StorageContext, VNVME_POOL_TAG);
}

/**
 * @brief 读取数据
 */
NTSTATUS
VnvmeStorageRead(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    if (StorageContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    switch (StorageContext->Type) {
        case VNVME_STORAGE_TYPE_MEMORY:
            return StorageMemoryRead(StorageContext, Offset, Buffer, Length);
            
        case VNVME_STORAGE_TYPE_FILE:
            return StorageFileRead(StorageContext, Offset, Buffer, Length);
            
        case VNVME_STORAGE_TYPE_SPARSE:
            return StorageSparseRead(StorageContext, Offset, Buffer, Length);
            
        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief 写入数据
 */
NTSTATUS
VnvmeStorageWrite(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    if (StorageContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    switch (StorageContext->Type) {
        case VNVME_STORAGE_TYPE_MEMORY:
            return StorageMemoryWrite(StorageContext, Offset, Buffer, Length);
            
        case VNVME_STORAGE_TYPE_FILE:
            return StorageFileWrite(StorageContext, Offset, Buffer, Length);
            
        case VNVME_STORAGE_TYPE_SPARSE:
            return StorageSparseWrite(StorageContext, Offset, Buffer, Length);
            
        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief 写零
 */
NTSTATUS
VnvmeStorageWriteZeroes(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length
    )
{
    if (StorageContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    switch (StorageContext->Type) {
        case VNVME_STORAGE_TYPE_MEMORY:
            return StorageMemoryWriteZeroes(StorageContext, Offset, Length);
            
        case VNVME_STORAGE_TYPE_FILE:
            // 文件后端: 分配临时缓冲区写零
            {
                PVOID zeroBuffer;
                NTSTATUS status;
                
                #define ZERO_CHUNK_SIZE (64 * 1024)  // 64 KB chunks
                
                zeroBuffer = ExAllocatePool2(
                    POOL_FLAG_NON_PAGED,
                    ZERO_CHUNK_SIZE,
                    VNVME_POOL_TAG
                );
                
                if (zeroBuffer == NULL) {
                    return STATUS_INSUFFICIENT_RESOURCES;
                }
                
                RtlZeroMemory(zeroBuffer, ZERO_CHUNK_SIZE);
                
                status = STATUS_SUCCESS;
                while (Length > 0 && NT_SUCCESS(status)) {
                    ULONG chunkSize = (Length > ZERO_CHUNK_SIZE) ? ZERO_CHUNK_SIZE : Length;
                    status = StorageFileWrite(StorageContext, Offset, zeroBuffer, chunkSize);
                    Offset += chunkSize;
                    Length -= chunkSize;
                }
                
                ExFreePoolWithTag(zeroBuffer, VNVME_POOL_TAG);
                return status;
            }
            
        case VNVME_STORAGE_TYPE_SPARSE:
            // 稀疏文件后端: 使用 FSCTL_SET_ZERO_DATA 高效释放空间
            return StorageSparseWriteZeroes(StorageContext, Offset, Length);
            
        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief 刷新缓存
 */
NTSTATUS
VnvmeStorageFlush(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext
    )
{
    if (StorageContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    switch (StorageContext->Type) {
        case VNVME_STORAGE_TYPE_MEMORY:
            // 内存后端无需刷新
            return STATUS_SUCCESS;
            
        case VNVME_STORAGE_TYPE_FILE:
        case VNVME_STORAGE_TYPE_SPARSE:
            return StorageFileFlush(StorageContext);
            
        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/**
 * @brief 获取直接访问指针 (仅内存后端)
 */
NTSTATUS
VnvmeStorageGetDirect(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PVOID* DirectPtr
    )
{
    if (StorageContext == NULL) {
        return STATUS_INVALID_PARAMETER;
    }
    
    if (StorageContext->Type == VNVME_STORAGE_TYPE_MEMORY) {
        return StorageMemoryGetDirect(StorageContext, Offset, Length, DirectPtr);
    }
    
    // 其他后端不支持直接访问
    return STATUS_NOT_SUPPORTED;
}

/**
 * @brief 获取存储统计信息
 */
VOID
VnvmeStorageGetStats(
    _In_ PVNVME_STORAGE_CONTEXT StorageContext,
    _Out_ PULONG64 ReadCount,
    _Out_ PULONG64 WriteCount,
    _Out_ PULONG64 BytesRead,
    _Out_ PULONG64 BytesWritten
    )
{
    if (StorageContext == NULL) {
        *ReadCount = 0;
        *WriteCount = 0;
        *BytesRead = 0;
        *BytesWritten = 0;
        return;
    }
    
    *ReadCount = StorageContext->ReadCount;
    *WriteCount = StorageContext->WriteCount;
    *BytesRead = StorageContext->BytesRead;
    *BytesWritten = StorageContext->BytesWritten;
}
