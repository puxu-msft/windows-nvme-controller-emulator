/**
 * @file backend_common.c
 * @brief 存储后端通用分发器
 */

#define LOG_MODULE "backend"

#include "backend.h"
#include "logger.h"
#include <stdlib.h>

//===========================================================================
// 外部后端创建函数声明
//===========================================================================

extern PBACKEND_CONTEXT BackendMemoryCreate(const BACKEND_CONFIG* pConfig);
extern void BackendMemoryDestroy(PBACKEND_CONTEXT pCtx);

extern PBACKEND_CONTEXT BackendFileCreate(const BACKEND_CONFIG* pConfig);
extern void BackendFileDestroy(PBACKEND_CONTEXT pCtx);

//===========================================================================
// 后端操作分发
//===========================================================================

// 从各后端文件导出的操作函数
// Memory 后端
extern BOOL MemoryBackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size);
extern BOOL MemoryBackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size);
extern BOOL MemoryBackendFlush(PBACKEND_CONTEXT pCtx);
extern BOOL MemoryBackendWriteZeroes(PBACKEND_CONTEXT pCtx, UINT64 offset, UINT64 size);
extern UINT64 MemoryBackendGetSize(PBACKEND_CONTEXT pCtx);
extern UINT32 MemoryBackendGetBlockSize(PBACKEND_CONTEXT pCtx);
extern BOOL MemoryBackendIsReadOnly(PBACKEND_CONTEXT pCtx);

// File 后端
extern BOOL FileBackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size);
extern BOOL FileBackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size);
extern BOOL FileBackendFlush(PBACKEND_CONTEXT pCtx);
extern BOOL FileBackendWriteZeroes(PBACKEND_CONTEXT pCtx, UINT64 offset, UINT64 size);
extern UINT64 FileBackendGetSize(PBACKEND_CONTEXT pCtx);
extern UINT32 FileBackendGetBlockSize(PBACKEND_CONTEXT pCtx);
extern BOOL FileBackendIsReadOnly(PBACKEND_CONTEXT pCtx);

//===========================================================================
// 公共 API 实现
//===========================================================================

PBACKEND_CONTEXT BackendCreate(const BACKEND_CONFIG* pConfig)
{
    if (!pConfig) {
        LogError("NULL backend config");
        return NULL;
    }
    
    LogInfo("Creating backend: type=%s, size=%llu bytes",
            pConfig->Type == BACKEND_TYPE_MEMORY ? "memory" : "file",
            pConfig->Size);
    
    switch (pConfig->Type) {
        case BACKEND_TYPE_MEMORY:
            return BackendMemoryCreate(pConfig);
            
        case BACKEND_TYPE_FILE:
            return BackendFileCreate(pConfig);
            
        default:
            LogError("Unknown backend type: %d", pConfig->Type);
            return NULL;
    }
}

void BackendDestroy(PBACKEND_CONTEXT pCtx)
{
    if (!pCtx) return;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;  // 第一个字段是类型
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            BackendMemoryDestroy(pCtx);
            break;
            
        case BACKEND_TYPE_FILE:
            BackendFileDestroy(pCtx);
            break;
            
        default:
            LogError("Unknown backend type in destroy: %d", type);
            break;
    }
}

BOOL BackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size)
{
    if (!pCtx) return FALSE;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendRead(pCtx, offset, buffer, size);
            
        case BACKEND_TYPE_FILE:
            return FileBackendRead(pCtx, offset, buffer, size);
            
        default:
            return FALSE;
    }
}

BOOL BackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size)
{
    if (!pCtx) return FALSE;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendWrite(pCtx, offset, buffer, size);
            
        case BACKEND_TYPE_FILE:
            return FileBackendWrite(pCtx, offset, buffer, size);
            
        default:
            return FALSE;
    }
}

BOOL BackendFlush(PBACKEND_CONTEXT pCtx)
{
    if (!pCtx) return FALSE;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendFlush(pCtx);
            
        case BACKEND_TYPE_FILE:
            return FileBackendFlush(pCtx);
            
        default:
            return FALSE;
    }
}

BOOL BackendWriteZeroes(PBACKEND_CONTEXT pCtx, UINT64 offset, UINT64 size)
{
    if (!pCtx) return FALSE;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendWriteZeroes(pCtx, offset, size);
            
        case BACKEND_TYPE_FILE:
            return FileBackendWriteZeroes(pCtx, offset, size);
            
        default:
            return FALSE;
    }
}

UINT64 BackendGetSize(PBACKEND_CONTEXT pCtx)
{
    if (!pCtx) return 0;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendGetSize(pCtx);
            
        case BACKEND_TYPE_FILE:
            return FileBackendGetSize(pCtx);
            
        default:
            return 0;
    }
}

UINT32 BackendGetBlockSize(PBACKEND_CONTEXT pCtx)
{
    if (!pCtx) return 512;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendGetBlockSize(pCtx);
            
        case BACKEND_TYPE_FILE:
            return FileBackendGetBlockSize(pCtx);
            
        default:
            return 512;
    }
}

BOOL BackendIsReadOnly(PBACKEND_CONTEXT pCtx)
{
    if (!pCtx) return TRUE;
    
    BACKEND_TYPE type = *(BACKEND_TYPE*)pCtx;
    
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return MemoryBackendIsReadOnly(pCtx);
            
        case BACKEND_TYPE_FILE:
            return FileBackendIsReadOnly(pCtx);
            
        default:
            return TRUE;
    }
}

const char* BackendGetTypeName(BACKEND_TYPE type)
{
    switch (type) {
        case BACKEND_TYPE_MEMORY:
            return "memory";
        case BACKEND_TYPE_FILE:
            return "file";
        default:
            return "unknown";
    }
}
