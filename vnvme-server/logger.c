/**
 * @file logger.c
 * @brief 日志系统实现
 */

#define LOG_MODULE "logger"

#include "logger.h"
#include <stdarg.h>
#include <time.h>

//===========================================================================
// 全局变量
//===========================================================================

static LOGGER_CONFIG g_LogConfig = {
    .level          = LOG_LEVEL_INFO,
    .enableConsole  = TRUE,
    .enableFile     = FALSE,
    .enableColor    = TRUE,
    .enableTimestamp = TRUE,
    .enableModule   = TRUE,
    .filePath       = {0}
};

static HANDLE g_LogFile = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_LogLock;
static BOOL g_Initialized = FALSE;

//===========================================================================
// 控制台颜色
//===========================================================================

#define COLOR_RESET     7   // 默认白色
#define COLOR_ERROR     12  // 红色
#define COLOR_WARNING   14  // 黄色
#define COLOR_INFO      10  // 绿色
#define COLOR_DEBUG     11  // 青色
#define COLOR_VERBOSE   8   // 灰色

static const char* GetLevelString(LOG_LEVEL level)
{
    switch (level) {
        case LOG_LEVEL_ERROR:   return "ERROR";
        case LOG_LEVEL_WARNING: return "WARN ";
        case LOG_LEVEL_INFO:    return "INFO ";
        case LOG_LEVEL_DEBUG:   return "DEBUG";
        case LOG_LEVEL_VERBOSE: return "VERB ";
        default:                return "?????";
    }
}

static WORD GetLevelColor(LOG_LEVEL level)
{
    switch (level) {
        case LOG_LEVEL_ERROR:   return COLOR_ERROR;
        case LOG_LEVEL_WARNING: return COLOR_WARNING;
        case LOG_LEVEL_INFO:    return COLOR_INFO;
        case LOG_LEVEL_DEBUG:   return COLOR_DEBUG;
        case LOG_LEVEL_VERBOSE: return COLOR_VERBOSE;
        default:                return COLOR_RESET;
    }
}

//===========================================================================
// 初始化和清理
//===========================================================================

BOOL LoggerInit(const LOGGER_CONFIG* pConfig)
{
    if (g_Initialized) {
        return TRUE;
    }
    
    InitializeCriticalSection(&g_LogLock);
    
    if (pConfig) {
        memcpy(&g_LogConfig, pConfig, sizeof(LOGGER_CONFIG));
    }
    
    // 打开日志文件
    if (g_LogConfig.enableFile && g_LogConfig.filePath[0]) {
        g_LogFile = CreateFileW(
            g_LogConfig.filePath,
            GENERIC_WRITE,
            FILE_SHARE_READ,
            NULL,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            NULL
            );
        
        if (g_LogFile == INVALID_HANDLE_VALUE) {
            // 无法打开文件，禁用文件日志
            g_LogConfig.enableFile = FALSE;
        } else {
            // 写入 UTF-8 BOM
            DWORD written;
            const BYTE bom[] = {0xEF, 0xBB, 0xBF};
            WriteFile(g_LogFile, bom, sizeof(bom), &written, NULL);
        }
    }
    
    g_Initialized = TRUE;
    return TRUE;
}

void LoggerShutdown(void)
{
    if (!g_Initialized) {
        return;
    }
    
    EnterCriticalSection(&g_LogLock);
    
    if (g_LogFile != INVALID_HANDLE_VALUE) {
        CloseHandle(g_LogFile);
        g_LogFile = INVALID_HANDLE_VALUE;
    }
    
    LeaveCriticalSection(&g_LogLock);
    DeleteCriticalSection(&g_LogLock);
    
    g_Initialized = FALSE;
}

void LoggerSetLevel(LOG_LEVEL level)
{
    g_LogConfig.level = level;
}

LOG_LEVEL LoggerGetLevel(void)
{
    return g_LogConfig.level;
}

//===========================================================================
// 日志输出
//===========================================================================

static void GetTimestamp(char* buffer, size_t size)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d.%03d",
             st.wYear, st.wMonth, st.wDay,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void LogMessage(LOG_LEVEL level, const char* module, const char* format, ...)
{
    if (!g_Initialized) {
        // 未初始化时，简单输出到控制台
        va_list args;
        va_start(args, format);
        vprintf(format, args);
        printf("\n");
        va_end(args);
        return;
    }
    
    if (level > g_LogConfig.level) {
        return;
    }
    
    EnterCriticalSection(&g_LogLock);
    
    char timestamp[32] = "";
    char message[4096];
    char finalLine[4200];
    
    // 格式化用户消息
    va_list args;
    va_start(args, format);
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    // 构建最终行
    if (g_LogConfig.enableTimestamp) {
        GetTimestamp(timestamp, sizeof(timestamp));
    }
    
    if (g_LogConfig.enableModule && module) {
        snprintf(finalLine, sizeof(finalLine), "%s [%s] [%-8s] %s\n",
                 timestamp, GetLevelString(level), module, message);
    } else {
        snprintf(finalLine, sizeof(finalLine), "%s [%s] %s\n",
                 timestamp, GetLevelString(level), message);
    }
    
    // 输出到控制台
    if (g_LogConfig.enableConsole) {
        HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
        
        if (g_LogConfig.enableColor && hConsole != INVALID_HANDLE_VALUE) {
            CONSOLE_SCREEN_BUFFER_INFO csbi;
            WORD originalAttrs = COLOR_RESET;
            
            if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
                originalAttrs = csbi.wAttributes;
            }
            
            SetConsoleTextAttribute(hConsole, GetLevelColor(level));
            printf("%s", finalLine);
            SetConsoleTextAttribute(hConsole, originalAttrs);
        } else {
            printf("%s", finalLine);
        }
    }
    
    // 输出到文件
    if (g_LogConfig.enableFile && g_LogFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(g_LogFile, finalLine, (DWORD)strlen(finalLine), &written, NULL);
        FlushFileBuffers(g_LogFile);
    }
    
    LeaveCriticalSection(&g_LogLock);
}

void LogMessageEx(LOG_LEVEL level, const char* module,
                  const char* file, int line,
                  const char* format, ...)
{
    if (!g_Initialized || level > g_LogConfig.level) {
        return;
    }
    
    // 提取文件名
    const char* filename = file;
    const char* p = file;
    while (*p) {
        if (*p == '\\' || *p == '/') {
            filename = p + 1;
        }
        p++;
    }
    
    char extendedFormat[4096];
    snprintf(extendedFormat, sizeof(extendedFormat), "[%s:%d] %s", filename, line, format);
    
    va_list args;
    va_start(args, format);
    
    char message[4096];
    vsnprintf(message, sizeof(message), format, args);
    va_end(args);
    
    LogMessage(level, module, "[%s:%d] %s", filename, line, message);
}

//===========================================================================
// 十六进制转储
//===========================================================================

void LogHexDump(LOG_LEVEL level, const char* module,
                const char* prefix, const void* data, size_t size)
{
    if (!g_Initialized || level > g_LogConfig.level) {
        return;
    }
    
    const unsigned char* bytes = (const unsigned char*)data;
    char line[80];
    char ascii[17];
    
    LogMessage(level, module, "%s: %zu bytes", prefix, size);
    
    for (size_t i = 0; i < size; i += 16) {
        int pos = 0;
        
        // 偏移
        pos += snprintf(line + pos, sizeof(line) - pos, "  %04zx: ", i);
        
        // 十六进制
        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                pos += snprintf(line + pos, sizeof(line) - pos, "%02x ", bytes[i + j]);
                ascii[j] = (bytes[i + j] >= 32 && bytes[i + j] < 127) ? bytes[i + j] : '.';
            } else {
                pos += snprintf(line + pos, sizeof(line) - pos, "   ");
                ascii[j] = ' ';
            }
            
            if (j == 7) {
                pos += snprintf(line + pos, sizeof(line) - pos, " ");
            }
        }
        
        ascii[16] = '\0';
        snprintf(line + pos, sizeof(line) - pos, " |%s|", ascii);
        
        LogMessage(level, module, "%s", line);
    }
}
