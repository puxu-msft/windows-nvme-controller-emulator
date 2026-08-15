/**
 * @file storage.c
 * @brief 存储后端通用 API
 * 
 * 提供可插拔的存储后端统一接口，支持:
 * - 内存后端 (VNVME_STORAGE_TYPE_MEMORY): 用于测试和小容量设备
 * - 文件后端 (VNVME_STORAGE_TYPE_FILE): 用于持久化存储
 * - 稀疏文件后端 (VNVME_STORAGE_TYPE_SPARSE): 用于大容量虚拟磁盘 (按需分配)
 * 
 * 具体后端实现位于:
 * - storage_memory.c: 内存后端
 * - storage_file.c: 文件和稀疏文件后端
 */

#include "storage_priv.h"

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
                ULONGLONG currentOffset = Offset;
                ULONG remainingLength = Length;
                
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
                while (remainingLength > 0 && NT_SUCCESS(status)) {
                    ULONG chunkSize = (remainingLength > ZERO_CHUNK_SIZE) ? ZERO_CHUNK_SIZE : remainingLength;
                    status = StorageFileWrite(StorageContext, currentOffset, zeroBuffer, chunkSize);
                    currentOffset += chunkSize;
                    remainingLength -= chunkSize;
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
 * @brief TRIM/Deallocate (释放空间)
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
