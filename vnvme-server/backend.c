/**
 * @file backend.c
 * @brief 用户态存储后端
 * 
 * 实现存储后端抽象:
 * - 内存后端 (RAM disk)
 * - 文件后端 (持久化存储)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*===========================================================================
 * 类型定义
 *===========================================================================*/

typedef enum _BACKEND_TYPE {
    BACKEND_TYPE_MEMORY = 0,
    BACKEND_TYPE_FILE   = 1
} BACKEND_TYPE;

typedef struct _BACKEND_CONTEXT {
    BACKEND_TYPE Type;
    UINT64 Size;                        // 总大小
    UINT32 BlockSize;                   // 块大小
    BOOL ReadOnly;
    
    // 内存后端
    PVOID MemoryBuffer;
    
    // 文件后端
    HANDLE FileHandle;
    WCHAR FilePath[MAX_PATH];
    
    // 统计
    UINT64 ReadCount;
    UINT64 WriteCount;
    UINT64 BytesRead;
    UINT64 BytesWritten;
} BACKEND_CONTEXT, *PBACKEND_CONTEXT;

// 调试模式 (外部变量)
extern BOOL g_DebugMode;

/*===========================================================================
 * 内存后端
 *===========================================================================*/

static PBACKEND_CONTEXT MemoryBackendCreate(SIZE_T size)
{
    PBACKEND_CONTEXT ctx;
    
    ctx = (PBACKEND_CONTEXT)calloc(1, sizeof(BACKEND_CONTEXT));
    if (ctx == NULL) {
        return NULL;
    }
    
    ctx->MemoryBuffer = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (ctx->MemoryBuffer == NULL) {
        fprintf(stderr, "MemoryBackendCreate: VirtualAlloc failed (error %lu)\n", GetLastError());
        free(ctx);
        return NULL;
    }
    
    // 初始化为零
    ZeroMemory(ctx->MemoryBuffer, size);
    
    ctx->Type = BACKEND_TYPE_MEMORY;
    ctx->Size = size;
    ctx->BlockSize = 512;
    ctx->ReadOnly = FALSE;
    
    if (g_DebugMode) {
        printf("Memory backend created: %llu bytes\n", (UINT64)size);
    }
    
    return ctx;
}

static void MemoryBackendDestroy(PBACKEND_CONTEXT ctx)
{
    if (ctx->MemoryBuffer != NULL) {
        VirtualFree(ctx->MemoryBuffer, 0, MEM_RELEASE);
        ctx->MemoryBuffer = NULL;
    }
}

static BOOL MemoryBackendRead(PBACKEND_CONTEXT ctx, UINT64 offset, PVOID buffer, UINT32 length)
{
    if (offset + length > ctx->Size) {
        return FALSE;
    }
    
    memcpy(buffer, (PUCHAR)ctx->MemoryBuffer + offset, length);
    
    ctx->ReadCount++;
    ctx->BytesRead += length;
    
    return TRUE;
}

static BOOL MemoryBackendWrite(PBACKEND_CONTEXT ctx, UINT64 offset, const PVOID buffer, UINT32 length)
{
    if (ctx->ReadOnly) {
        return FALSE;
    }
    
    if (offset + length > ctx->Size) {
        return FALSE;
    }
    
    memcpy((PUCHAR)ctx->MemoryBuffer + offset, buffer, length);
    
    ctx->WriteCount++;
    ctx->BytesWritten += length;
    
    return TRUE;
}

static BOOL MemoryBackendFlush(PBACKEND_CONTEXT ctx)
{
    // 内存后端不需要刷新
    UNREFERENCED_PARAMETER(ctx);
    return TRUE;
}

static BOOL MemoryBackendWriteZeroes(PBACKEND_CONTEXT ctx, UINT64 offset, UINT64 length)
{
    if (ctx->ReadOnly) {
        return FALSE;
    }
    
    if (offset + length > ctx->Size) {
        return FALSE;
    }
    
    ZeroMemory((PUCHAR)ctx->MemoryBuffer + offset, (SIZE_T)length);
    
    ctx->WriteCount++;
    ctx->BytesWritten += length;
    
    return TRUE;
}

/*===========================================================================
 * 文件后端
 *===========================================================================*/

static PBACKEND_CONTEXT FileBackendCreate(const WCHAR* filePath, SIZE_T size)
{
    PBACKEND_CONTEXT ctx;
    LARGE_INTEGER fileSize;
    DWORD creationDisposition;
    
    ctx = (PBACKEND_CONTEXT)calloc(1, sizeof(BACKEND_CONTEXT));
    if (ctx == NULL) {
        return NULL;
    }
    
    wcscpy_s(ctx->FilePath, MAX_PATH, filePath);
    
    // 尝试打开现有文件，如果不存在则创建
    creationDisposition = OPEN_ALWAYS;
    
    ctx->FileHandle = CreateFileW(
        filePath,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        creationDisposition,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        NULL
        );
    
    if (ctx->FileHandle == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "FileBackendCreate: CreateFile failed (error %lu)\n", GetLastError());
        free(ctx);
        return NULL;
    }
    
    // 获取或设置文件大小
    if (GetFileSizeEx(ctx->FileHandle, &fileSize)) {
        if (fileSize.QuadPart == 0) {
            // 新文件，设置大小
            fileSize.QuadPart = size;
            if (!SetFilePointerEx(ctx->FileHandle, fileSize, NULL, FILE_BEGIN) ||
                !SetEndOfFile(ctx->FileHandle)) {
                fprintf(stderr, "FileBackendCreate: Cannot set file size (error %lu)\n", GetLastError());
                CloseHandle(ctx->FileHandle);
                free(ctx);
                return NULL;
            }
            ctx->Size = size;
        } else {
            // 现有文件，使用其大小
            ctx->Size = fileSize.QuadPart;
        }
    } else {
        ctx->Size = size;
    }
    
    ctx->Type = BACKEND_TYPE_FILE;
    ctx->BlockSize = 512;
    ctx->ReadOnly = FALSE;
    
    if (g_DebugMode) {
        printf("File backend created: %ls (%llu bytes)\n", filePath, (UINT64)ctx->Size);
    }
    
    return ctx;
}

static void FileBackendDestroy(PBACKEND_CONTEXT ctx)
{
    if (ctx->FileHandle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(ctx->FileHandle);
        CloseHandle(ctx->FileHandle);
        ctx->FileHandle = INVALID_HANDLE_VALUE;
    }
}

static BOOL FileBackendRead(PBACKEND_CONTEXT ctx, UINT64 offset, PVOID buffer, UINT32 length)
{
    OVERLAPPED overlapped = {0};
    DWORD bytesRead;
    
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = (DWORD)(offset >> 32);
    
    if (!ReadFile(ctx->FileHandle, buffer, length, &bytesRead, &overlapped)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return FALSE;
        }
    }
    
    if (bytesRead != length) {
        // 读取不完整，填充零
        ZeroMemory((PUCHAR)buffer + bytesRead, length - bytesRead);
    }
    
    ctx->ReadCount++;
    ctx->BytesRead += length;
    
    return TRUE;
}

static BOOL FileBackendWrite(PBACKEND_CONTEXT ctx, UINT64 offset, const PVOID buffer, UINT32 length)
{
    OVERLAPPED overlapped = {0};
    DWORD bytesWritten;
    
    if (ctx->ReadOnly) {
        return FALSE;
    }
    
    overlapped.Offset = (DWORD)(offset & 0xFFFFFFFF);
    overlapped.OffsetHigh = (DWORD)(offset >> 32);
    
    if (!WriteFile(ctx->FileHandle, buffer, length, &bytesWritten, &overlapped)) {
        if (GetLastError() != ERROR_IO_PENDING) {
            return FALSE;
        }
    }
    
    ctx->WriteCount++;
    ctx->BytesWritten += length;
    
    return TRUE;
}

static BOOL FileBackendFlush(PBACKEND_CONTEXT ctx)
{
    return FlushFileBuffers(ctx->FileHandle);
}

static BOOL FileBackendWriteZeroes(PBACKEND_CONTEXT ctx, UINT64 offset, UINT64 length)
{
    BYTE zeroBuffer[4096];
    UINT64 remaining = length;
    UINT64 currentOffset = offset;
    
    if (ctx->ReadOnly) {
        return FALSE;
    }
    
    ZeroMemory(zeroBuffer, sizeof(zeroBuffer));
    
    while (remaining > 0) {
        UINT32 chunkSize = (remaining > sizeof(zeroBuffer)) ? sizeof(zeroBuffer) : (UINT32)remaining;
        
        if (!FileBackendWrite(ctx, currentOffset, zeroBuffer, chunkSize)) {
            return FALSE;
        }
        
        currentOffset += chunkSize;
        remaining -= chunkSize;
    }
    
    return TRUE;
}

/*===========================================================================
 * 公共 API
 *===========================================================================*/

/**
 * @brief 创建存储后端
 */
PBACKEND_CONTEXT BackendCreate(int type, SIZE_T size, const WCHAR* filePath)
{
    if (type == BACKEND_TYPE_FILE) {
        if (filePath == NULL || filePath[0] == L'\0') {
            fprintf(stderr, "BackendCreate: File path required for file backend\n");
            return NULL;
        }
        return FileBackendCreate(filePath, size);
    } else {
        return MemoryBackendCreate(size);
    }
}

/**
 * @brief 销毁存储后端
 */
void BackendDestroy(PBACKEND_CONTEXT ctx)
{
    if (ctx == NULL) {
        return;
    }
    
    if (ctx->Type == BACKEND_TYPE_FILE) {
        FileBackendDestroy(ctx);
    } else {
        MemoryBackendDestroy(ctx);
    }
    
    free(ctx);
}

/**
 * @brief 读取数据
 */
BOOL BackendRead(PBACKEND_CONTEXT ctx, UINT64 offset, void* buffer, UINT32 length)
{
    if (ctx == NULL) {
        return FALSE;
    }
    
    if (ctx->Type == BACKEND_TYPE_FILE) {
        return FileBackendRead(ctx, offset, buffer, length);
    } else {
        return MemoryBackendRead(ctx, offset, buffer, length);
    }
}

/**
 * @brief 写入数据
 */
BOOL BackendWrite(PBACKEND_CONTEXT ctx, UINT64 offset, const void* buffer, UINT32 length)
{
    if (ctx == NULL) {
        return FALSE;
    }
    
    if (ctx->Type == BACKEND_TYPE_FILE) {
        return FileBackendWrite(ctx, offset, (PVOID)buffer, length);
    } else {
        return MemoryBackendWrite(ctx, offset, (PVOID)buffer, length);
    }
}

/**
 * @brief 刷新缓存
 */
BOOL BackendFlush(PBACKEND_CONTEXT ctx)
{
    if (ctx == NULL) {
        return FALSE;
    }
    
    if (ctx->Type == BACKEND_TYPE_FILE) {
        return FileBackendFlush(ctx);
    } else {
        return MemoryBackendFlush(ctx);
    }
}

/**
 * @brief 写零
 */
BOOL BackendWriteZeroes(PBACKEND_CONTEXT ctx, UINT64 offset, UINT64 length)
{
    if (ctx == NULL) {
        return FALSE;
    }
    
    if (ctx->Type == BACKEND_TYPE_FILE) {
        return FileBackendWriteZeroes(ctx, offset, length);
    } else {
        return MemoryBackendWriteZeroes(ctx, offset, length);
    }
}

/**
 * @brief 获取后端大小
 */
UINT64 BackendGetSize(PBACKEND_CONTEXT ctx)
{
    if (ctx == NULL) {
        return 0;
    }
    return ctx->Size;
}

/**
 * @brief 获取统计信息
 */
void BackendGetStats(PBACKEND_CONTEXT ctx, UINT64* readCount, UINT64* writeCount,
                     UINT64* bytesRead, UINT64* bytesWritten)
{
    if (ctx == NULL) {
        return;
    }
    
    if (readCount) *readCount = ctx->ReadCount;
    if (writeCount) *writeCount = ctx->WriteCount;
    if (bytesRead) *bytesRead = ctx->BytesRead;
    if (bytesWritten) *bytesWritten = ctx->BytesWritten;
}
