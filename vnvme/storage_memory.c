/**
 * @file storage_memory.c
 * @brief 内存存储后端实现
 * 
 * 用于测试和小容量设备的内存后端。
 * 数据存储在非分页池中，支持零拷贝直接访问。
 */

#include "storage_priv.h"

//===========================================================================
// 内存后端实现
//===========================================================================

/**
 * @brief 初始化内存后端
 */
NTSTATUS
StorageMemoryInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG TotalBytes
    )
{
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
VOID
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
NTSTATUS
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
NTSTATUS
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
NTSTATUS
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
NTSTATUS
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
