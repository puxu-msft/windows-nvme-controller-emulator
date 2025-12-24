/**
 * @file types.h
 * @brief vnvme-server 基础类型定义
 * 
 * 此头文件包含所有模块共享的基础类型，避免循环依赖。
 */

#ifndef _VNVME_TYPES_H_
#define _VNVME_TYPES_H_

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>
#include <stdbool.h>

//===========================================================================
// 版本信息
//===========================================================================

#define VNVME_SERVER_VERSION_MAJOR  1
#define VNVME_SERVER_VERSION_MINOR  0
#define VNVME_SERVER_VERSION_PATCH  0
#define VNVME_SERVER_VERSION_STRING "1.0.0"

//===========================================================================
// 后端类型
//===========================================================================

typedef enum _BACKEND_TYPE {
    BACKEND_TYPE_MEMORY = 0,    // 内存后端
    BACKEND_TYPE_FILE   = 1,    // 文件后端
    BACKEND_TYPE_MAX
} BACKEND_TYPE;

//===========================================================================
// 日志级别
//===========================================================================

typedef enum _LOG_LEVEL {
    LOG_LEVEL_NONE    = 0,
    LOG_LEVEL_ERROR   = 1,
    LOG_LEVEL_WARNING = 2,
    LOG_LEVEL_INFO    = 3,
    LOG_LEVEL_DEBUG   = 4,
    LOG_LEVEL_VERBOSE = 5
} LOG_LEVEL;

//===========================================================================
// 工具宏
//===========================================================================

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr)         (sizeof(arr) / sizeof((arr)[0]))
#endif

#ifndef MIN
#define MIN(a, b)               ((a) < (b) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b)               ((a) > (b) ? (a) : (b))
#endif

#define ALIGN_UP(x, align)      (((x) + (align) - 1) & ~((align) - 1))
#define ALIGN_DOWN(x, align)    ((x) & ~((align) - 1))

// 安全释放
#define SAFE_FREE(ptr)          do { if (ptr) { free(ptr); ptr = NULL; } } while(0)
#define SAFE_CLOSE_HANDLE(h)    do { if (h && h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = NULL; } } while(0)

//===========================================================================
// 错误码
//===========================================================================

#define VNVME_SUCCESS               0
#define VNVME_ERROR_INVALID_PARAM   1
#define VNVME_ERROR_NO_MEMORY       2
#define VNVME_ERROR_DRIVER_OPEN     3
#define VNVME_ERROR_SHM_MAP         4
#define VNVME_ERROR_BACKEND_INIT    5
#define VNVME_ERROR_CONFIG_LOAD     6
#define VNVME_ERROR_IO              7

#endif /* _VNVME_TYPES_H_ */
