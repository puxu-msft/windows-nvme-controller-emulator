/**
 * @file config.c
 * @brief 配置解析模块实现
 */

#define LOG_MODULE "config"

#include "config.h"
#include "logger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

//===========================================================================
// 默认值
//===========================================================================

#define DEFAULT_SIZE            (100ULL * 1024 * 1024 * 1024)  // 100GB
#define DEFAULT_HEARTBEAT_MS    1000
#define DEFAULT_POLL_US         100
#define DEFAULT_MODEL           "Virtual NVMe SSD"
#define DEFAULT_SERIAL          "VNVME00000001"
#define DEFAULT_VENDOR_ID       0x1B36
#define DEFAULT_DEVICE_ID       0x0010

//===========================================================================
// 初始化
//===========================================================================

void ConfigSetDefaults(PSERVER_CONFIG pConfig)
{
    if (!pConfig) return;
    
    memset(pConfig, 0, sizeof(SERVER_CONFIG));
    
    // 日志默认值
    pConfig->log.level = LOG_LEVEL_INFO;
    pConfig->log.enableConsole = TRUE;
    pConfig->log.enableFile = FALSE;
    
    // 存储默认值
    pConfig->storage.backendType = BACKEND_TYPE_MEMORY;
    pConfig->storage.size = DEFAULT_SIZE;
    pConfig->storage.blockSize = 512;
    pConfig->storage.readOnly = FALSE;
    
    // 控制器默认值
    strncpy(pConfig->controller.modelNumber, DEFAULT_MODEL, 
            sizeof(pConfig->controller.modelNumber) - 1);
    strncpy(pConfig->controller.serialNumber, DEFAULT_SERIAL,
            sizeof(pConfig->controller.serialNumber) - 1);
    pConfig->controller.vendorId = DEFAULT_VENDOR_ID;
    pConfig->controller.deviceId = DEFAULT_DEVICE_ID;
    
    // 运行时默认值
    pConfig->heartbeatIntervalMs = DEFAULT_HEARTBEAT_MS;
    pConfig->pollIntervalUs = DEFAULT_POLL_US;
    pConfig->daemonMode = FALSE;
}

//===========================================================================
// 大小解析
//===========================================================================

UINT64 ConfigParseSize(const char* str)
{
    if (!str || !*str) return 0;
    
    char* endptr;
    double value = strtod(str, &endptr);
    
    if (value < 0) return 0;
    
    // 处理后缀
    UINT64 multiplier = 1;
    switch (toupper(*endptr)) {
        case 'K':
            multiplier = 1024ULL;
            break;
        case 'M':
            multiplier = 1024ULL * 1024;
            break;
        case 'G':
            multiplier = 1024ULL * 1024 * 1024;
            break;
        case 'T':
            multiplier = 1024ULL * 1024 * 1024 * 1024;
            break;
        case '\0':
        case 'B':
            multiplier = 1;
            break;
        default:
            return 0;  // 未知后缀
    }
    
    return (UINT64)(value * multiplier);
}

//===========================================================================
// 命令行解析
//===========================================================================

void ConfigPrintUsage(const char* programName)
{
    printf("Usage: %s [options]\n\n", programName);
    printf("Options:\n");
    printf("  -c, --config <file>    Load configuration from file\n");
    printf("  -s, --size <size>      Storage size (e.g., 100G, 512M)\n");
    printf("  -b, --backend <type>   Backend type: memory, file\n");
    printf("  -f, --file <path>      Backend file path (for file backend)\n");
    printf("  --direct-io            Enable direct I/O (FILE_FLAG_NO_BUFFERING)\n");
    printf("  -m, --model <name>     Model number string\n");
    printf("  -n, --serial <sn>      Serial number string\n");
    printf("  --log-level <level>    Log level: error, warn, info, debug, verbose\n");
    printf("  --log-file <path>      Log to file\n");
    printf("  --daemon               Run as daemon\n");
    printf("  -h, --help             Show this help\n");
    printf("  -v, --version          Show version\n");
    printf("\nExamples:\n");
    printf("  %s --size 100G --backend memory\n", programName);
    printf("  %s --size 500G --backend file --file C:\\vnvme\\disk.img --direct-io\n", programName);
    printf("  %s --config vnvme.conf\n", programName);
}

static LOG_LEVEL ParseLogLevel(const char* str)
{
    if (!str) return LOG_LEVEL_INFO;
    
    if (_stricmp(str, "error") == 0) return LOG_LEVEL_ERROR;
    if (_stricmp(str, "warn") == 0 || _stricmp(str, "warning") == 0) return LOG_LEVEL_WARNING;
    if (_stricmp(str, "info") == 0) return LOG_LEVEL_INFO;
    if (_stricmp(str, "debug") == 0) return LOG_LEVEL_DEBUG;
    if (_stricmp(str, "verbose") == 0) return LOG_LEVEL_VERBOSE;
    if (_stricmp(str, "none") == 0) return LOG_LEVEL_NONE;
    
    return LOG_LEVEL_INFO;
}

BOOL ConfigParseArgs(int argc, char* argv[], PSERVER_CONFIG pConfig)
{
    if (!pConfig) return FALSE;
    
    ConfigSetDefaults(pConfig);
    
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];
        
        // 帮助
        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            ConfigPrintUsage(argv[0]);
            exit(0);
        }
        
        // 版本
        if (strcmp(arg, "-v") == 0 || strcmp(arg, "--version") == 0) {
            printf("vnvme-server version 1.0.0\n");
            exit(0);
        }
        
        // 配置文件
        if ((strcmp(arg, "-c") == 0 || strcmp(arg, "--config") == 0) && i + 1 < argc) {
            MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1, 
                               pConfig->configFilePath, MAX_PATH);
            continue;
        }
        
        // 大小
        if ((strcmp(arg, "-s") == 0 || strcmp(arg, "--size") == 0) && i + 1 < argc) {
            pConfig->storage.size = ConfigParseSize(argv[++i]);
            if (pConfig->storage.size == 0) {
                LogError("Invalid size: %s", argv[i]);
                return FALSE;
            }
            continue;
        }
        
        // 后端类型
        if ((strcmp(arg, "-b") == 0 || strcmp(arg, "--backend") == 0) && i + 1 < argc) {
            const char* type = argv[++i];
            if (_stricmp(type, "memory") == 0 || _stricmp(type, "mem") == 0) {
                pConfig->storage.backendType = BACKEND_TYPE_MEMORY;
            } else if (_stricmp(type, "file") == 0) {
                pConfig->storage.backendType = BACKEND_TYPE_FILE;
            } else {
                LogError("Unknown backend type: %s", type);
                return FALSE;
            }
            continue;
        }
        
        // 文件路径
        if ((strcmp(arg, "-f") == 0 || strcmp(arg, "--file") == 0) && i + 1 < argc) {
            MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1,
                               pConfig->storage.filePath, MAX_PATH);
            continue;
        }
        
        // 直接 I/O
        if (strcmp(arg, "--direct-io") == 0) {
            pConfig->storage.directIO = TRUE;
            continue;
        }
        
        // 型号
        if ((strcmp(arg, "-m") == 0 || strcmp(arg, "--model") == 0) && i + 1 < argc) {
            strncpy(pConfig->controller.modelNumber, argv[++i],
                    sizeof(pConfig->controller.modelNumber) - 1);
            continue;
        }
        
        // 序列号
        if ((strcmp(arg, "-n") == 0 || strcmp(arg, "--serial") == 0) && i + 1 < argc) {
            strncpy(pConfig->controller.serialNumber, argv[++i],
                    sizeof(pConfig->controller.serialNumber) - 1);
            continue;
        }
        
        // 日志级别
        if (strcmp(arg, "--log-level") == 0 && i + 1 < argc) {
            pConfig->log.level = ParseLogLevel(argv[++i]);
            continue;
        }
        
        // 日志文件
        if (strcmp(arg, "--log-file") == 0 && i + 1 < argc) {
            MultiByteToWideChar(CP_UTF8, 0, argv[++i], -1,
                               pConfig->log.filePath, MAX_PATH);
            pConfig->log.enableFile = TRUE;
            continue;
        }
        
        // 守护进程模式
        if (strcmp(arg, "--daemon") == 0) {
            pConfig->daemonMode = TRUE;
            continue;
        }
        
        // 未知参数
        LogError("Unknown option: %s", arg);
        return FALSE;
    }
    
    return TRUE;
}

//===========================================================================
// 配置文件解析
//===========================================================================

static char* TrimString(char* str)
{
    if (!str) return NULL;
    
    // 去掉前导空白
    while (isspace(*str)) str++;
    
    if (*str == '\0') return str;
    
    // 去掉尾部空白
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    *(end + 1) = '\0';
    
    return str;
}

BOOL ConfigLoadFile(const WCHAR* filePath, PSERVER_CONFIG pConfig)
{
    if (!filePath || !pConfig) return FALSE;
    
    FILE* fp = _wfopen(filePath, L"r");
    if (!fp) {
        LogError("Cannot open config file");
        return FALSE;
    }
    
    char line[1024];
    char currentSection[64] = "";
    int lineNum = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        lineNum++;
        char* trimmed = TrimString(line);
        
        // 跳过空行和注释
        if (*trimmed == '\0' || *trimmed == ';' || *trimmed == '#') {
            continue;
        }
        
        // 节名
        if (*trimmed == '[') {
            char* end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                strncpy(currentSection, trimmed + 1, sizeof(currentSection) - 1);
            }
            continue;
        }
        
        // 键值对
        char* eq = strchr(trimmed, '=');
        if (!eq) continue;
        
        *eq = '\0';
        char* key = TrimString(trimmed);
        char* value = TrimString(eq + 1);
        
        // 根据节和键处理
        if (_stricmp(currentSection, "storage") == 0 || 
            _stricmp(currentSection, "namespace") == 0) {
            
            if (_stricmp(key, "Size") == 0 || _stricmp(key, "NamespaceSize") == 0) {
                pConfig->storage.size = ConfigParseSize(value);
            } else if (_stricmp(key, "Type") == 0 || _stricmp(key, "BackendType") == 0) {
                if (_stricmp(value, "memory") == 0) {
                    pConfig->storage.backendType = BACKEND_TYPE_MEMORY;
                } else if (_stricmp(value, "file") == 0) {
                    pConfig->storage.backendType = BACKEND_TYPE_FILE;
                }
            } else if (_stricmp(key, "File") == 0 || _stricmp(key, "BackendFile") == 0) {
                MultiByteToWideChar(CP_UTF8, 0, value, -1,
                                   pConfig->storage.filePath, MAX_PATH);
            } else if (_stricmp(key, "ReadOnly") == 0) {
                pConfig->storage.readOnly = (_stricmp(value, "true") == 0 || 
                                             _stricmp(value, "1") == 0);
            } else if (_stricmp(key, "DirectIO") == 0) {
                pConfig->storage.directIO = (_stricmp(value, "true") == 0 || 
                                             _stricmp(value, "1") == 0);
            }
        } else if (_stricmp(currentSection, "controller") == 0) {
            
            if (_stricmp(key, "Model") == 0 || _stricmp(key, "ModelNumber") == 0) {
                strncpy(pConfig->controller.modelNumber, value,
                        sizeof(pConfig->controller.modelNumber) - 1);
            } else if (_stricmp(key, "Serial") == 0 || _stricmp(key, "SerialNumber") == 0) {
                strncpy(pConfig->controller.serialNumber, value,
                        sizeof(pConfig->controller.serialNumber) - 1);
            } else if (_stricmp(key, "VendorId") == 0) {
                pConfig->controller.vendorId = (UINT16)strtoul(value, NULL, 0);
            } else if (_stricmp(key, "DeviceId") == 0) {
                pConfig->controller.deviceId = (UINT16)strtoul(value, NULL, 0);
            }
        } else if (_stricmp(currentSection, "log") == 0 ||
                   _stricmp(currentSection, "logging") == 0) {
            
            if (_stricmp(key, "Level") == 0) {
                pConfig->log.level = ParseLogLevel(value);
            } else if (_stricmp(key, "File") == 0) {
                MultiByteToWideChar(CP_UTF8, 0, value, -1,
                                   pConfig->log.filePath, MAX_PATH);
                pConfig->log.enableFile = TRUE;
            } else if (_stricmp(key, "Console") == 0) {
                pConfig->log.enableConsole = (_stricmp(value, "true") == 0 ||
                                              _stricmp(value, "1") == 0);
            }
        } else if (_stricmp(currentSection, "runtime") == 0 ||
                   _stricmp(currentSection, "server") == 0) {
            
            if (_stricmp(key, "HeartbeatInterval") == 0) {
                pConfig->heartbeatIntervalMs = (UINT32)strtoul(value, NULL, 0);
            } else if (_stricmp(key, "PollInterval") == 0) {
                pConfig->pollIntervalUs = (UINT32)strtoul(value, NULL, 0);
            } else if (_stricmp(key, "Daemon") == 0) {
                pConfig->daemonMode = (_stricmp(value, "true") == 0 ||
                                       _stricmp(value, "1") == 0);
            }
        }
    }
    
    fclose(fp);
    LogInfo("Loaded config from file");
    return TRUE;
}

//===========================================================================
// 验证和打印
//===========================================================================

BOOL ConfigValidate(const SERVER_CONFIG* pConfig)
{
    if (!pConfig) return FALSE;
    
    // 验证存储大小
    if (pConfig->storage.size == 0) {
        LogError("Storage size cannot be zero");
        return FALSE;
    }
    
    // 验证文件后端路径
    if (pConfig->storage.backendType == BACKEND_TYPE_FILE &&
        pConfig->storage.filePath[0] == L'\0') {
        LogError("File backend requires --file path");
        return FALSE;
    }
    
    // 验证序列号长度
    if (strlen(pConfig->controller.serialNumber) == 0) {
        LogError("Serial number cannot be empty");
        return FALSE;
    }
    
    return TRUE;
}

void ConfigPrint(const SERVER_CONFIG* pConfig)
{
    if (!pConfig) return;
    
    const char* backendType = (pConfig->storage.backendType == BACKEND_TYPE_MEMORY) 
                              ? "memory" : "file";
    
    LogInfo("=== Configuration ===");
    LogInfo("Backend: %s", backendType);
    LogInfo("Size: %llu bytes (%.2f GB)", 
            pConfig->storage.size,
            (double)pConfig->storage.size / (1024.0 * 1024.0 * 1024.0));
    
    if (pConfig->storage.backendType == BACKEND_TYPE_FILE) {
        char filePath[MAX_PATH];
        WideCharToMultiByte(CP_UTF8, 0, pConfig->storage.filePath, -1,
                           filePath, MAX_PATH, NULL, NULL);
        LogInfo("File: %s", filePath);
    }
    
    LogInfo("Model: %s", pConfig->controller.modelNumber);
    LogInfo("Serial: %s", pConfig->controller.serialNumber);
    LogInfo("Log Level: %d", pConfig->log.level);
    LogInfo("=====================");
}
