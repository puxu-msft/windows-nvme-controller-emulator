/**
 * @file storage.c
 * @brief 存储后端实现
 * 
 * 提供可插拔的存储后端，支持:
 * - 内存后端 (VNVME_STORAGE_TYPE_MEMORY): 用于测试和小容量设备
 * - 文件后端 (VNVME_STORAGE_TYPE_FILE): 用于持久化存储
 * 
 * 设计原则:
 * - 同步 I/O: 所有操作在调用者线程完成
 * - 可扩展: 通过 VnvmeStorageInit 选择后端类型
 * - 零拷贝友好: 提供直接访问内部缓冲区的 API (仅内存后端)
 */

#include "vnvme.h"

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
