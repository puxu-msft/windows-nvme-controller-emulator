/**
 * @file backend_file.c
 * @brief 文件存储后端实现
 */

#define LOG_MODULE "backend"

#include "backend.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

//===========================================================================
// 文件后端上下文
//===========================================================================

typedef struct _FILE_BACKEND_CONTEXT {
    BACKEND_TYPE    Type;
    UINT64          Size;
    UINT32          BlockSize;
    BOOL            ReadOnly;
    HANDLE          FileHandle;     // 文件句柄
    WCHAR           FilePath[MAX_PATH];
    CRITICAL_SECTION Lock;          // 访问锁
} FILE_BACKEND_CONTEXT, *PFILE_BACKEND_CONTEXT;

//===========================================================================
// 文件后端操作 (导出供 backend_common.c 调用)
//===========================================================================

BOOL FileBackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    
    if (!pFileCtx || pFileCtx->FileHandle == INVALID_HANDLE_VALUE || !buffer) {
        return FALSE;
    }
    
    if (offset + size > pFileCtx->Size) {
        LogError("Read beyond end: offset=%llu, size=%u, total=%llu",
                 offset, size, pFileCtx->Size);
        return FALSE;
    }
    
    EnterCriticalSection(&pFileCtx->Lock);
    
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    
    if (!SetFilePointerEx(pFileCtx->FileHandle, li, NULL, FILE_BEGIN)) {
        LeaveCriticalSection(&pFileCtx->Lock);
        LogError("SetFilePointerEx failed: error %u", GetLastError());
        return FALSE;
    }
    
    DWORD bytesRead;
    BOOL result = ReadFile(pFileCtx->FileHandle, buffer, size, &bytesRead, NULL);
    
    LeaveCriticalSection(&pFileCtx->Lock);
    
    if (!result || bytesRead != size) {
        LogError("ReadFile failed: error %u, read=%u, expected=%u",
                 GetLastError(), bytesRead, size);
        return FALSE;
    }
    
    LogVerbose("Read: offset=%llu, size=%u", offset, size);
    return TRUE;
}

BOOL FileBackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    
    if (!pFileCtx || pFileCtx->FileHandle == INVALID_HANDLE_VALUE || !buffer) {
        return FALSE;
    }
    
    if (pFileCtx->ReadOnly) {
        LogError("Write to read-only backend");
        return FALSE;
    }
    
    if (offset + size > pFileCtx->Size) {
        LogError("Write beyond end: offset=%llu, size=%u, total=%llu",
                 offset, size, pFileCtx->Size);
        return FALSE;
    }
    
    EnterCriticalSection(&pFileCtx->Lock);
    
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    
    if (!SetFilePointerEx(pFileCtx->FileHandle, li, NULL, FILE_BEGIN)) {
        LeaveCriticalSection(&pFileCtx->Lock);
        LogError("SetFilePointerEx failed: error %u", GetLastError());
        return FALSE;
    }
    
    DWORD bytesWritten;
    BOOL result = WriteFile(pFileCtx->FileHandle, buffer, size, &bytesWritten, NULL);
    
    LeaveCriticalSection(&pFileCtx->Lock);
    
    if (!result || bytesWritten != size) {
        LogError("WriteFile failed: error %u, written=%u, expected=%u",
                 GetLastError(), bytesWritten, size);
        return FALSE;
    }
    
    LogVerbose("Write: offset=%llu, size=%u", offset, size);
    return TRUE;
}

BOOL FileBackendFlush(PBACKEND_CONTEXT pCtx)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    
    if (!pFileCtx || pFileCtx->FileHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    BOOL result = FlushFileBuffers(pFileCtx->FileHandle);
    if (!result) {
        LogError("FlushFileBuffers failed: error %u", GetLastError());
        return FALSE;
    }
    
    LogVerbose("Flush");
    return TRUE;
}

BOOL FileBackendWriteZeroes(PBACKEND_CONTEXT pCtx, UINT64 offset, UINT64 size)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    
    if (!pFileCtx || pFileCtx->FileHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    if (pFileCtx->ReadOnly) {
        LogError("WriteZeroes to read-only backend");
        return FALSE;
    }
    
    if (offset + size > pFileCtx->Size) {
        LogError("WriteZeroes beyond end: offset=%llu, size=%llu, total=%llu",
                 offset, size, pFileCtx->Size);
        return FALSE;
    }
    
    // 分块写零
    const UINT32 chunkSize = 64 * 1024;  // 64KB
    static BYTE zeroBuffer[64 * 1024] = {0};
    
    EnterCriticalSection(&pFileCtx->Lock);
    
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)offset;
    
    if (!SetFilePointerEx(pFileCtx->FileHandle, li, NULL, FILE_BEGIN)) {
        LeaveCriticalSection(&pFileCtx->Lock);
        LogError("SetFilePointerEx failed: error %u", GetLastError());
        return FALSE;
    }
    
    UINT64 remaining = size;
    while (remaining > 0) {
        DWORD toWrite = (DWORD)(remaining > chunkSize ? chunkSize : remaining);
        DWORD bytesWritten;
        
        if (!WriteFile(pFileCtx->FileHandle, zeroBuffer, toWrite, &bytesWritten, NULL) ||
            bytesWritten != toWrite) {
            LeaveCriticalSection(&pFileCtx->Lock);
            LogError("WriteZeroes failed: error %u", GetLastError());
            return FALSE;
        }
        
        remaining -= toWrite;
    }
    
    LeaveCriticalSection(&pFileCtx->Lock);
    
    LogVerbose("WriteZeroes: offset=%llu, size=%llu", offset, size);
    return TRUE;
}

UINT64 FileBackendGetSize(PBACKEND_CONTEXT pCtx)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    return pFileCtx ? pFileCtx->Size : 0;
}

UINT32 FileBackendGetBlockSize(PBACKEND_CONTEXT pCtx)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    return pFileCtx ? pFileCtx->BlockSize : 512;
}

BOOL FileBackendIsReadOnly(PBACKEND_CONTEXT pCtx)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    return pFileCtx ? pFileCtx->ReadOnly : TRUE;
}

//===========================================================================
// 创建和销毁
//===========================================================================

PBACKEND_CONTEXT BackendFileCreate(const BACKEND_CONFIG* pConfig)
{
    if (!pConfig || pConfig->Size == 0 || pConfig->FilePath[0] == L'\0') {
        LogError("Invalid file backend config");
        return NULL;
    }
    
    PFILE_BACKEND_CONTEXT pCtx = (PFILE_BACKEND_CONTEXT)calloc(1, sizeof(FILE_BACKEND_CONTEXT));
    if (!pCtx) {
        LogError("Failed to allocate file backend context");
        return NULL;
    }
    
    pCtx->Type = BACKEND_TYPE_FILE;
    pCtx->Size = pConfig->Size;
    pCtx->BlockSize = pConfig->BlockSize > 0 ? pConfig->BlockSize : 512;
    pCtx->ReadOnly = pConfig->ReadOnly;
    wcscpy_s(pCtx->FilePath, MAX_PATH, pConfig->FilePath);
    
    // 打开或创建文件
    DWORD access = pConfig->ReadOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
    DWORD creation = pConfig->ReadOnly ? OPEN_EXISTING : OPEN_ALWAYS;
    
    pCtx->FileHandle = CreateFileW(
        pConfig->FilePath,
        access,
        FILE_SHARE_READ,
        NULL,
        creation,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        NULL
        );
    
    if (pCtx->FileHandle == INVALID_HANDLE_VALUE) {
        LogError("Failed to open file: error %u", GetLastError());
        free(pCtx);
        return NULL;
    }
    
    // 检查/设置文件大小
    LARGE_INTEGER fileSize;
    if (!GetFileSizeEx(pCtx->FileHandle, &fileSize)) {
        LogError("Failed to get file size: error %u", GetLastError());
        CloseHandle(pCtx->FileHandle);
        free(pCtx);
        return NULL;
    }
    
    if ((UINT64)fileSize.QuadPart < pConfig->Size && !pConfig->ReadOnly) {
        // 扩展文件到目标大小
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)pConfig->Size;
        
        if (!SetFilePointerEx(pCtx->FileHandle, li, NULL, FILE_BEGIN) ||
            !SetEndOfFile(pCtx->FileHandle)) {
            LogError("Failed to extend file: error %u", GetLastError());
            CloseHandle(pCtx->FileHandle);
            free(pCtx);
            return NULL;
        }
        
        LogInfo("Extended file to %llu bytes", pConfig->Size);
    } else if ((UINT64)fileSize.QuadPart > 0) {
        // 使用现有文件大小
        if ((UINT64)fileSize.QuadPart < pConfig->Size) {
            pCtx->Size = (UINT64)fileSize.QuadPart;
            LogWarn("Using existing file size: %llu bytes", pCtx->Size);
        }
    }
    
    InitializeCriticalSection(&pCtx->Lock);
    
    char filePath[MAX_PATH];
    WideCharToMultiByte(CP_UTF8, 0, pConfig->FilePath, -1, filePath, MAX_PATH, NULL, NULL);
    
    LogInfo("File backend created: %s, size=%llu bytes (%.2f GB)",
            filePath, pCtx->Size, (double)pCtx->Size / (1024.0 * 1024.0 * 1024.0));
    
    return (PBACKEND_CONTEXT)pCtx;
}

void BackendFileDestroy(PBACKEND_CONTEXT pCtx)
{
    PFILE_BACKEND_CONTEXT pFileCtx = (PFILE_BACKEND_CONTEXT)pCtx;
    
    if (!pFileCtx) return;
    
    if (pFileCtx->FileHandle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(pFileCtx->FileHandle);
        CloseHandle(pFileCtx->FileHandle);
        pFileCtx->FileHandle = INVALID_HANDLE_VALUE;
    }
    
    DeleteCriticalSection(&pFileCtx->Lock);
    free(pFileCtx);
    
    LogInfo("File backend destroyed");
}

//===========================================================================
// 导出虚函数表 (用于基类调用)
//===========================================================================

// 这些函数通过 backend.c 的分发机制调用
