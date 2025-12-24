/**
 * @file backend.h
 * @brief 存储后端接口定义
 */

#ifndef _VNVME_BACKEND_H_
#define _VNVME_BACKEND_H_

#include "types.h"

//===========================================================================
// 后端配置
//===========================================================================

typedef struct _BACKEND_CONFIG {
    BACKEND_TYPE    Type;               // 后端类型
    UINT64          Size;               // 存储大小 (字节)
    WCHAR           FilePath[MAX_PATH]; // 文件路径 (文件后端)
    BOOL            ReadOnly;           // 只读模式
    UINT32          BlockSize;          // 块大小 (默认 512)
    BOOL            DirectIO;           // 直接 I/O (FILE_FLAG_NO_BUFFERING)
} BACKEND_CONFIG, *PBACKEND_CONFIG;

//===========================================================================
// 后端上下文 (不透明)
//===========================================================================

typedef struct _BACKEND_CONTEXT BACKEND_CONTEXT, *PBACKEND_CONTEXT;

//===========================================================================
// 后端操作函数
//===========================================================================

/**
 * 创建后端
 * 
 * @param pConfig 后端配置
 * @return 后端上下文，失败返回 NULL
 */
PBACKEND_CONTEXT BackendCreate(const BACKEND_CONFIG* pConfig);

/**
 * 销毁后端
 * 
 * @param pCtx 后端上下文
 */
void BackendDestroy(PBACKEND_CONTEXT pCtx);

/**
 * 读取数据
 * 
 * @param pCtx 后端上下文
 * @param offset 偏移量 (字节)
 * @param buffer 输出缓冲区
 * @param size 读取大小 (字节)
 * @return TRUE 成功，FALSE 失败
 */
BOOL BackendRead(PBACKEND_CONTEXT pCtx, UINT64 offset, void* buffer, UINT32 size);

/**
 * 写入数据
 * 
 * @param pCtx 后端上下文
 * @param offset 偏移量 (字节)
 * @param buffer 输入缓冲区
 * @param size 写入大小 (字节)
 * @return TRUE 成功，FALSE 失败
 */
BOOL BackendWrite(PBACKEND_CONTEXT pCtx, UINT64 offset, const void* buffer, UINT32 size);

/**
 * 刷新数据到持久存储
 * 
 * @param pCtx 后端上下文
 * @return TRUE 成功，FALSE 失败
 */
BOOL BackendFlush(PBACKEND_CONTEXT pCtx);

/**
 * 写入零
 * 
 * @param pCtx 后端上下文
 * @param offset 偏移量 (字节)
 * @param size 大小 (字节)
 * @return TRUE 成功，FALSE 失败
 */
BOOL BackendWriteZeroes(PBACKEND_CONTEXT pCtx, UINT64 offset, UINT64 size);

/**
 * 获取存储大小
 * 
 * @param pCtx 后端上下文
 * @return 存储大小 (字节)
 */
UINT64 BackendGetSize(PBACKEND_CONTEXT pCtx);

/**
 * 获取块大小
 * 
 * @param pCtx 后端上下文
 * @return 块大小 (字节)
 */
UINT32 BackendGetBlockSize(PBACKEND_CONTEXT pCtx);

/**
 * 检查是否只读
 * 
 * @param pCtx 后端上下文
 * @return TRUE 只读，FALSE 可写
 */
BOOL BackendIsReadOnly(PBACKEND_CONTEXT pCtx);

/**
 * 获取后端类型名称
 * 
 * @param type 后端类型
 * @return 类型名称字符串
 */
const char* BackendGetTypeName(BACKEND_TYPE type);

//===========================================================================
// 后端特定创建函数 (内部使用)
//===========================================================================

PBACKEND_CONTEXT BackendMemoryCreate(const BACKEND_CONFIG* pConfig);
PBACKEND_CONTEXT BackendFileCreate(const BACKEND_CONFIG* pConfig);

#endif /* _VNVME_BACKEND_H_ */
