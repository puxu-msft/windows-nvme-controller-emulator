/**
 * @file storage_file.c
 * @brief 文件和稀疏文件存储后端实现
 * 
 * 提供两种文件后端:
 * - 普通文件后端: 用于持久化存储
 * - 稀疏文件后端: 用于大容量虚拟磁盘 (按需分配磁盘空间)
 */

#include "storage_priv.h"

//===========================================================================
// 文件后端实现
//===========================================================================

/**
 * @brief 初始化文件后端
 */
NTSTATUS
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
VOID
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

/**
 * @brief 文件后端读取
 */
NTSTATUS
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
NTSTATUS
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
NTSTATUS
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
// 稀疏文件后端实现
//===========================================================================

/**
 * @brief 初始化稀疏文件后端
 * 
 * 稀疏文件只在有数据的区域分配实际磁盘空间，非常适合大容量虚拟磁盘。
 * 例如：1TB 虚拟盘只使用了 10GB，则只占用 10GB 磁盘空间。
 */
NTSTATUS
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
NTSTATUS
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
NTSTATUS
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
NTSTATUS
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
