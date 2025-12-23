/**
 * @file backend_memory.c
 * @brief 内存存储后端实现
 */

#define LOG_MODULE "backend"

#include "backend.h"
#include "logger.h"
#include <stdlib.h>
#include <string.h>

//===========================================================================
// 内存后端上下文
//===========================================================================

typedef struct _MEMORY_BACKEND_CONTEXT {
    BACKEND_TYPE    Type;
    UINT64          Size;
    UINT32          BlockSize;
    BOOL            ReadOnly;
    PVOID           Data;           // 数据缓冲区
    CRITICAL_SECTION Lock;          // 访问锁
} MEMORY_BACKEND_CONTEXT, *PMEMORY_BACKEND_CONTEXT;

//===========================================================================
// 内存后端操作 (导出供 backend_common.c 调用)
//===========================================================================

BOOL MemoryBackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    
    if (!pMemCtx || !pMemCtx->Data || !buffer) {
        return FALSE;
    }
    
    if (offset + size > pMemCtx->Size) {
        LogError("Read beyond end: offset=%llu, size=%u, total=%llu",
                 offset, size, pMemCtx->Size);
        return FALSE;
    }
    
    EnterCriticalSection(&pMemCtx->Lock);
    memcpy(buffer, (PUCHAR)pMemCtx->Data + offset, size);
    LeaveCriticalSection(&pMemCtx->Lock);
    
    LogVerbose("Read: offset=%llu, size=%u", offset, size);
    return TRUE;
}

BOOL MemoryBackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    
    if (!pMemCtx || !pMemCtx->Data || !buffer) {
        return FALSE;
    }
    
    if (pMemCtx->ReadOnly) {
        LogError("Write to read-only backend");
        return FALSE;
    }
    
    if (offset + size > pMemCtx->Size) {
        LogError("Write beyond end: offset=%llu, size=%u, total=%llu",
                 offset, size, pMemCtx->Size);
        return FALSE;
    }
    
    EnterCriticalSection(&pMemCtx->Lock);
    memcpy((PUCHAR)pMemCtx->Data + offset, buffer, size);
    LeaveCriticalSection(&pMemCtx->Lock);
    
    LogVerbose("Write: offset=%llu, size=%u", offset, size);
    return TRUE;
}

BOOL MemoryBackendFlush(PBACKEND_CONTEXT pCtx)
{
    // 内存后端无需刷新
    UNREFERENCED_PARAMETER(pCtx);
    return TRUE;
}

BOOL MemoryBackendWriteZeroes(PBACKEND_CONTEXT pCtx, UINT64 offset, UINT64 size)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    
    if (!pMemCtx || !pMemCtx->Data) {
        return FALSE;
    }
    
    if (pMemCtx->ReadOnly) {
        LogError("WriteZeroes to read-only backend");
        return FALSE;
    }
    
    if (offset + size > pMemCtx->Size) {
        LogError("WriteZeroes beyond end: offset=%llu, size=%llu, total=%llu",
                 offset, size, pMemCtx->Size);
        return FALSE;
    }
    
    EnterCriticalSection(&pMemCtx->Lock);
    memset((PUCHAR)pMemCtx->Data + offset, 0, (size_t)size);
    LeaveCriticalSection(&pMemCtx->Lock);
    
    LogVerbose("WriteZeroes: offset=%llu, size=%llu", offset, size);
    return TRUE;
}

UINT64 MemoryBackendGetSize(PBACKEND_CONTEXT pCtx)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    return pMemCtx ? pMemCtx->Size : 0;
}

UINT32 MemoryBackendGetBlockSize(PBACKEND_CONTEXT pCtx)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    return pMemCtx ? pMemCtx->BlockSize : 512;
}

BOOL MemoryBackendIsReadOnly(PBACKEND_CONTEXT pCtx)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    return pMemCtx ? pMemCtx->ReadOnly : TRUE;
}

//===========================================================================
// 创建和销毁
//===========================================================================

PBACKEND_CONTEXT BackendMemoryCreate(const BACKEND_CONFIG* pConfig)
{
    if (!pConfig || pConfig->Size == 0) {
        LogError("Invalid memory backend config");
        return NULL;
    }
    
    // 限制内存后端最大 4GB (避免内存耗尽)
    const UINT64 maxSize = 4ULL * 1024 * 1024 * 1024;
    UINT64 actualSize = pConfig->Size;
    
    if (actualSize > maxSize) {
        LogWarn("Memory backend size limited to 4GB (requested %llu)", actualSize);
        actualSize = maxSize;
    }
    
    PMEMORY_BACKEND_CONTEXT pCtx = (PMEMORY_BACKEND_CONTEXT)calloc(1, sizeof(MEMORY_BACKEND_CONTEXT));
    if (!pCtx) {
        LogError("Failed to allocate memory backend context");
        return NULL;
    }
    
    pCtx->Type = BACKEND_TYPE_MEMORY;
    pCtx->Size = actualSize;
    pCtx->BlockSize = pConfig->BlockSize > 0 ? pConfig->BlockSize : 512;
    pCtx->ReadOnly = pConfig->ReadOnly;
    
    // 分配数据缓冲区
    pCtx->Data = VirtualAlloc(
        NULL,
        (SIZE_T)actualSize,
        MEM_RESERVE | MEM_COMMIT,
        PAGE_READWRITE
        );
    
    if (!pCtx->Data) {
        LogError("Failed to allocate %llu bytes for memory backend: error %u",
                 actualSize, GetLastError());
        free(pCtx);
        return NULL;
    }
    
    // 初始化为零
    memset(pCtx->Data, 0, (size_t)actualSize);
    
    InitializeCriticalSection(&pCtx->Lock);
    
    LogInfo("Memory backend created: size=%llu bytes (%.2f MB)",
            actualSize, (double)actualSize / (1024.0 * 1024.0));
    
    return (PBACKEND_CONTEXT)pCtx;
}

void BackendMemoryDestroy(PBACKEND_CONTEXT pCtx)
{
    PMEMORY_BACKEND_CONTEXT pMemCtx = (PMEMORY_BACKEND_CONTEXT)pCtx;
    
    if (!pMemCtx) return;
    
    if (pMemCtx->Data) {
        VirtualFree(pMemCtx->Data, 0, MEM_RELEASE);
        pMemCtx->Data = NULL;
    }
    
    DeleteCriticalSection(&pMemCtx->Lock);
    free(pMemCtx);
    
    LogInfo("Memory backend destroyed");
}

//===========================================================================
// 导出虚函数表 (用于基类调用)
//===========================================================================

// 这些函数通过 backend.c 的分发机制调用
