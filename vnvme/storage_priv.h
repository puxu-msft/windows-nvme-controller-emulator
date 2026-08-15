/**
 * @file storage_priv.h
 * @brief 存储后端内部头文件
 * 
 * 定义存储后端的内部结构和函数，供 storage*.c 文件使用。
 * 外部模块应使用 vnvme.h 中声明的公共 API。
 */

#ifndef _STORAGE_PRIV_H_
#define _STORAGE_PRIV_H_

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
// 内存后端函数 (storage_memory.c)
//===========================================================================

NTSTATUS
StorageMemoryInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG TotalBytes
    );

VOID
StorageMemoryCleanup(
    _Inout_ PVNVME_STORAGE_CONTEXT Context
    );

NTSTATUS
StorageMemoryRead(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
StorageMemoryWrite(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
StorageMemoryWriteZeroes(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length
    );

NTSTATUS
StorageMemoryGetDirect(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_ PVOID* DirectPtr
    );

//===========================================================================
// 文件后端函数 (storage_file.c)
//===========================================================================

NTSTATUS
StorageFileInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONGLONG TotalBytes,
    _In_ BOOLEAN CreateNew
    );

VOID
StorageFileCleanup(
    _Inout_ PVNVME_STORAGE_CONTEXT Context
    );

NTSTATUS
StorageFileRead(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
StorageFileWrite(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
StorageFileFlush(
    _In_ PVNVME_STORAGE_CONTEXT Context
    );

//===========================================================================
// 稀疏文件后端函数 (storage_file.c)
//===========================================================================

NTSTATUS
StorageSparseInit(
    _Inout_ PVNVME_STORAGE_CONTEXT Context,
    _In_ PUNICODE_STRING FilePath,
    _In_ ULONGLONG TotalBytes,
    _In_ BOOLEAN CreateNew
    );

NTSTATUS
StorageSparseRead(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
StorageSparseWrite(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    );

NTSTATUS
StorageSparseWriteZeroes(
    _In_ PVNVME_STORAGE_CONTEXT Context,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length
    );

//===========================================================================
// 常量
//===========================================================================

#define VNVME_MAX_MEMORY_BACKEND_SIZE   (256 * 1024 * 1024ULL)  // 256 MB
#define ZERO_CHUNK_SIZE                 (64 * 1024)              // 64 KB
#define SPARSE_ZERO_CHUNK_SIZE          (64 * 1024)              // 64 KB

#endif /* _STORAGE_PRIV_H_ */
