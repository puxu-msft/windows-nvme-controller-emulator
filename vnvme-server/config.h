/**
 * @file config.h
 * @brief 配置解析模块接口
 */

#ifndef _VNVME_CONFIG_H_
#define _VNVME_CONFIG_H_

#include "types.h"
#include "logger.h"

//===========================================================================
// 配置结构
//===========================================================================

/**
 * 存储配置
 */
typedef struct _STORAGE_CONFIG {
    BACKEND_TYPE backendType;           // 后端类型
    UINT64      size;                   // 存储大小 (字节)
    UINT32      blockSize;              // 块大小 (默认 512)
    WCHAR       filePath[MAX_PATH];     // 文件后端路径
    BOOL        readOnly;               // 只读模式
    BOOL        directIO;               // 直接 I/O (FILE_FLAG_NO_BUFFERING)
} STORAGE_CONFIG, *PSTORAGE_CONFIG;

/**
 * 控制器配置
 */
typedef struct _CONTROLLER_CONFIG {
    char        modelNumber[40];        // 型号
    char        serialNumber[20];       // 序列号
    UINT16      vendorId;               // 厂商 ID
    UINT16      deviceId;               // 设备 ID
} CONTROLLER_CONFIG, *PCONTROLLER_CONFIG;

/**
 * 主配置结构
 */
typedef struct _SERVER_CONFIG {
    // 日志配置
    struct {
        LOG_LEVEL   level;
        BOOL        enableConsole;
        BOOL        enableFile;
        WCHAR       filePath[MAX_PATH];
    } log;
    
    // 存储配置
    STORAGE_CONFIG      storage;
    
    // 控制器配置
    CONTROLLER_CONFIG   controller;
    
    // 运行时选项
    UINT32              heartbeatIntervalMs;    // 心跳间隔 (默认 1000)
    UINT32              pollIntervalUs;         // 轮询间隔 (默认 100)
    BOOL                daemonMode;             // 后台运行
    
    // 配置文件路径
    WCHAR               configFilePath[MAX_PATH];
} SERVER_CONFIG, *PSERVER_CONFIG;

//===========================================================================
// 函数声明
//===========================================================================

/**
 * 初始化配置为默认值
 * 
 * @param pConfig 配置结构指针
 */
void ConfigSetDefaults(PSERVER_CONFIG pConfig);

/**
 * 解析命令行参数
 * 
 * @param argc 参数个数
 * @param argv 参数数组
 * @param pConfig 输出配置结构
 * @return TRUE 成功，FALSE 失败
 */
BOOL ConfigParseArgs(int argc, char* argv[], PSERVER_CONFIG pConfig);

/**
 * 加载配置文件
 * 
 * @param filePath 配置文件路径
 * @param pConfig 输出配置结构
 * @return TRUE 成功，FALSE 失败
 */
BOOL ConfigLoadFile(const WCHAR* filePath, PSERVER_CONFIG pConfig);

/**
 * 验证配置
 * 
 * @param pConfig 配置结构
 * @return TRUE 有效，FALSE 无效
 */
BOOL ConfigValidate(const SERVER_CONFIG* pConfig);

/**
 * 打印配置摘要
 * 
 * @param pConfig 配置结构
 */
void ConfigPrint(const SERVER_CONFIG* pConfig);

/**
 * 解析大小字符串 (支持 K/M/G/T 后缀)
 * 
 * @param str 大小字符串 (如 "100G", "512M")
 * @return 字节数，失败返回 0
 */
UINT64 ConfigParseSize(const char* str);

/**
 * 打印使用帮助
 */
void ConfigPrintUsage(const char* programName);

#endif /* _VNVME_CONFIG_H_ */
