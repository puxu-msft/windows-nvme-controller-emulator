/**
 * @file logger.h
 * @brief 日志系统接口
 */

#ifndef _VNVME_LOGGER_H_
#define _VNVME_LOGGER_H_

#include "types.h"
#include <stdio.h>

//===========================================================================
// 日志配置
//===========================================================================

typedef struct _LOGGER_CONFIG {
    LOG_LEVEL   level;                  // 日志级别
    BOOL        enableConsole;          // 输出到控制台
    BOOL        enableFile;             // 输出到文件
    BOOL        enableColor;            // 控制台彩色输出
    BOOL        enableTimestamp;        // 显示时间戳
    BOOL        enableModule;           // 显示模块名
    WCHAR       filePath[MAX_PATH];     // 日志文件路径
} LOGGER_CONFIG, *PLOGGER_CONFIG;

//===========================================================================
// 初始化和清理
//===========================================================================

/**
 * 初始化日志系统
 * 
 * @param pConfig 日志配置，NULL 使用默认配置
 * @return TRUE 成功，FALSE 失败
 */
BOOL LoggerInit(const LOGGER_CONFIG* pConfig);

/**
 * 关闭日志系统
 */
void LoggerShutdown(void);

/**
 * 设置日志级别
 * 
 * @param level 新的日志级别
 */
void LoggerSetLevel(LOG_LEVEL level);

/**
 * 获取当前日志级别
 * 
 * @return 当前日志级别
 */
LOG_LEVEL LoggerGetLevel(void);

//===========================================================================
// 日志输出函数
//===========================================================================

/**
 * 输出日志消息
 * 
 * @param level 日志级别
 * @param module 模块名称
 * @param format 格式字符串
 * @param ... 格式参数
 */
void LogMessage(LOG_LEVEL level, const char* module, const char* format, ...);

/**
 * 输出日志消息 (带文件/行号)
 */
void LogMessageEx(LOG_LEVEL level, const char* module, 
                  const char* file, int line,
                  const char* format, ...);

//===========================================================================
// 便捷日志宏
//===========================================================================

#define LOG_MODULE  "main"  // 默认模块名，各模块可重定义

// 基本日志宏
#define LogError(fmt, ...)   LogMessage(LOG_LEVEL_ERROR, LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogWarn(fmt, ...)    LogMessage(LOG_LEVEL_WARNING, LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogInfo(fmt, ...)    LogMessage(LOG_LEVEL_INFO, LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogDebug(fmt, ...)   LogMessage(LOG_LEVEL_DEBUG, LOG_MODULE, fmt, ##__VA_ARGS__)
#define LogVerbose(fmt, ...) LogMessage(LOG_LEVEL_VERBOSE, LOG_MODULE, fmt, ##__VA_ARGS__)

// 带位置的日志宏
#define LogErrorEx(fmt, ...)   LogMessageEx(LOG_LEVEL_ERROR, LOG_MODULE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define LogWarnEx(fmt, ...)    LogMessageEx(LOG_LEVEL_WARNING, LOG_MODULE, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

// 函数跟踪宏
#define LogFuncEnter()       LogMessage(LOG_LEVEL_VERBOSE, LOG_MODULE, ">>> %s", __FUNCTION__)
#define LogFuncExit()        LogMessage(LOG_LEVEL_VERBOSE, LOG_MODULE, "<<< %s", __FUNCTION__)
#define LogFuncExitCode(rc)  LogMessage(LOG_LEVEL_VERBOSE, LOG_MODULE, "<<< %s (rc=%d)", __FUNCTION__, (int)(rc))

// Windows 错误日志
#define LogLastError(msg) \
    do { \
        DWORD _err = GetLastError(); \
        LogMessage(LOG_LEVEL_ERROR, LOG_MODULE, "%s: error %u (0x%X)", msg, _err, _err); \
    } while(0)

//===========================================================================
// 十六进制转储
//===========================================================================

/**
 * 输出十六进制转储
 * 
 * @param level 日志级别
 * @param module 模块名称
 * @param prefix 前缀字符串
 * @param data 数据指针
 * @param size 数据大小
 */
void LogHexDump(LOG_LEVEL level, const char* module,
                const char* prefix, const void* data, size_t size);

#define LogHexDumpDebug(prefix, data, size) \
    LogHexDump(LOG_LEVEL_DEBUG, LOG_MODULE, prefix, data, size)

//===========================================================================
// 别名 (兼容 main_v2.c)
//===========================================================================

#define LogInit(pConfig)    LoggerInit(pConfig)
#define LogShutdown()       LoggerShutdown()

#endif /* _VNVME_LOGGER_H_ */
