/**
 * @file main.c
 * @brief VNVME 用户态服务入口
 * 
 * vnvme-server.exe - 用户态 NVMe 命令处理服务
 * 
 * 用法:
 *   vnvme-server.exe [options]
 *   
 * 选项:
 *   -h, --help          显示帮助
 *   -v, --version       显示版本
 *   -d, --debug         启用调试输出
 *   --backend=<type>    存储后端 (memory, file)
 *   --file=<path>       文件后端路径
 *   --size=<size>       内存后端大小 (默认 64MB)
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/vnvme_common.h"
#include "../include/vnvme_ioctl.h"

/*===========================================================================
 * 常量定义
 *===========================================================================*/

// 使用符号链接而非内核设备名
#define VNVME_USER_DEVICE       L"\\\\.\\VNVMEControl"
#define DEFAULT_BACKEND_SIZE    (64 * 1024 * 1024)  // 64 MB
#define POLLING_INTERVAL_MS     1                    // 1 ms 轮询间隔

/*===========================================================================
 * 后端类型
 *===========================================================================*/

typedef enum _BACKEND_TYPE {
    BACKEND_MEMORY,
    BACKEND_FILE
} BACKEND_TYPE;

/*===========================================================================
 * 服务器配置
 *===========================================================================*/

typedef struct _SERVER_CONFIG {
    BOOL DebugMode;
    BACKEND_TYPE BackendType;
    WCHAR FilePath[MAX_PATH];
    SIZE_T BackendSize;
} SERVER_CONFIG, *PSERVER_CONFIG;

/*===========================================================================
 * 全局变量
 *===========================================================================*/

volatile BOOL g_Running = TRUE;
BOOL g_DebugMode = FALSE;                           // 供其他模块使用
static HANDLE g_DriverHandle = INVALID_HANDLE_VALUE;
static SERVER_CONFIG g_Config = {0};
static PVOID g_ShmAddress = NULL;                   // 共享内存地址
static SIZE_T g_ShmSize = 0;                        // 共享内存大小

// 外部函数声明
struct _BACKEND_CONTEXT;
typedef struct _BACKEND_CONTEXT BACKEND_CONTEXT, *PBACKEND_CONTEXT;
PBACKEND_CONTEXT BackendCreate(int type, SIZE_T size, const WCHAR* filePath);
void BackendDestroy(PBACKEND_CONTEXT ctx);
BOOL BackendFlush(PBACKEND_CONTEXT ctx);
BOOL CmdProcessorInit(PVOID shmAddress, PBACKEND_CONTEXT backend);
UINT64 CmdProcessorRun(void);

static PBACKEND_CONTEXT g_Backend = NULL;

/*===========================================================================
 * 控制台处理程序
 *===========================================================================*/

static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            printf("\nShutdown requested...\n");
            g_Running = FALSE;
            return TRUE;
        default:
            return FALSE;
    }
}

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

/**
 * @brief 解析大小字符串 (支持 K/M/G 后缀)
 */
static SIZE_T ParseSizeString(const char* str)
{
    SIZE_T value = 0;
    char suffix = 0;
    int len = (int)strlen(str);
    
    if (len == 0) return 0;
    
    // 检查后缀
    suffix = str[len - 1];
    if (suffix == 'K' || suffix == 'k' || 
        suffix == 'M' || suffix == 'm' || 
        suffix == 'G' || suffix == 'g') {
        char* temp = _strdup(str);
        if (temp) {
            temp[len - 1] = '\0';
            value = (SIZE_T)_atoi64(temp);
            free(temp);
        }
    } else {
        value = (SIZE_T)_atoi64(str);
        suffix = 0;
    }
    
    // 应用后缀乘数
    switch (suffix) {
        case 'K': case 'k': value *= 1024; break;
        case 'M': case 'm': value *= 1024 * 1024; break;
        case 'G': case 'g': value *= 1024ULL * 1024 * 1024; break;
    }
    
    return value;
}

/**
 * @brief 从配置文件加载设置
 */
static BOOL LoadConfigFile(const char* path, PSERVER_CONFIG config)
{
    FILE* file;
    char line[512];
    char key[64];
    char value[256];
    
    file = fopen(path, "r");
    if (file == NULL) {
        return FALSE;
    }
    
    printf("Loading configuration from: %s\n", path);
    
    while (fgets(line, sizeof(line), file) != NULL) {
        // 跳过空行和注释
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }
        
        // 去除行尾换行
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        
        // 解析 key=value
        if (sscanf(line, "%63[^=]=%255s", key, value) == 2) {
            // 去除 key 的空白
            char* p = key;
            while (*p == ' ' || *p == '\t') p++;
            
            if (strcmp(p, "backend") == 0) {
                if (strcmp(value, "memory") == 0) {
                    config->BackendType = BACKEND_MEMORY;
                } else if (strcmp(value, "file") == 0) {
                    config->BackendType = BACKEND_FILE;
                }
            }
            else if (strcmp(p, "size") == 0) {
                config->BackendSize = ParseSizeString(value);
            }
            else if (strcmp(p, "file") == 0) {
                if (MultiByteToWideChar(CP_UTF8, 0, value, -1, 
                                       config->FilePath, MAX_PATH) == 0) {
                    fprintf(stderr, "Warning: Invalid file path in config\n");
                }
            }
            else if (strcmp(p, "debug") == 0) {
                config->DebugMode = (strcmp(value, "true") == 0 || 
                                    strcmp(value, "1") == 0 ||
                                    strcmp(value, "yes") == 0);
            }
        }
    }
    
    fclose(file);
    return TRUE;
}

static void PrintUsage(const char* progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help          Show this help message\n");
    printf("  -v, --version       Show version information\n");
    printf("  -d, --debug         Enable debug output\n");
    printf("  -c, --config=<file> Load configuration from file\n");
    printf("  --backend=<type>    Storage backend: memory (default), file\n");
    printf("  --file=<path>       File path for file backend\n");
    printf("  --size=<size>       Backend size (e.g., 64M, 1G)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                          # Use memory backend (64MB)\n", progname);
    printf("  %s --config=vnvme.conf      # Load from config file\n", progname);
    printf("  %s --backend=memory --size=128M   # 128MB memory\n", progname);
    printf("  %s --backend=file --file=disk.img # File backend\n", progname);
    printf("\n");
}

static void PrintVersion(void)
{
    printf("vnvme-server v%d.%d.%d\n",
           VNVME_VERSION_MAJOR,
           VNVME_VERSION_MINOR,
           VNVME_VERSION_PATCH);
    printf("Part of the Virtual NVMe Driver Project\n");
    printf("API Version: 0x%08X\n", VNVME_VERSION);
}

static BOOL ParseArgs(int argc, char* argv[], PSERVER_CONFIG config)
{
    // 默认值
    config->DebugMode = FALSE;
    config->BackendType = BACKEND_MEMORY;
    config->BackendSize = DEFAULT_BACKEND_SIZE;
    config->FilePath[0] = L'\0';
    
    // 第一遍: 查找配置文件选项
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--config=", 9) == 0) {
            const char* configPath = argv[i] + 9;
            if (!LoadConfigFile(configPath, config)) {
                fprintf(stderr, "Warning: Cannot load config file '%s'\n", configPath);
            }
        }
        else if (strncmp(argv[i], "-c", 2) == 0) {
            const char* configPath = NULL;
            if (argv[i][2] != '\0') {
                configPath = argv[i] + 2;
            } else if (i + 1 < argc) {
                configPath = argv[++i];
            }
            if (configPath != NULL && !LoadConfigFile(configPath, config)) {
                fprintf(stderr, "Warning: Cannot load config file '%s'\n", configPath);
            }
        }
    }
    
    // 第二遍: 处理命令行选项 (覆盖配置文件)
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            PrintUsage(argv[0]);
            return FALSE;
        }
        else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            PrintVersion();
            return FALSE;
        }
        else if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--debug") == 0) {
            config->DebugMode = TRUE;
            g_DebugMode = TRUE;  // 设置全局调试标志
        }
        else if (strncmp(argv[i], "--config=", 9) == 0 || strncmp(argv[i], "-c", 2) == 0) {
            // 已在第一遍处理
            if (strncmp(argv[i], "-c", 2) == 0 && argv[i][2] == '\0') {
                i++;  // 跳过下一个参数
            }
        }
        else if (strncmp(argv[i], "--backend=", 10) == 0) {
            const char* backend = argv[i] + 10;
            if (strcmp(backend, "memory") == 0) {
                config->BackendType = BACKEND_MEMORY;
            }
            else if (strcmp(backend, "file") == 0) {
                config->BackendType = BACKEND_FILE;
            }
            else {
                fprintf(stderr, "Error: Unknown backend type '%s'\n", backend);
                return FALSE;
            }
        }
        else if (strncmp(argv[i], "--file=", 7) == 0) {
            const char* path = argv[i] + 7;
            if (MultiByteToWideChar(CP_UTF8, 0, path, -1, 
                                   config->FilePath, MAX_PATH) == 0) {
                fprintf(stderr, "Error: Invalid file path '%s'\n", path);
                return FALSE;
            }
        }
        else if (strncmp(argv[i], "--size=", 7) == 0) {
            const char* sizeStr = argv[i] + 7;
            config->BackendSize = ParseSizeString(sizeStr);
            if (config->BackendSize == 0) {
                fprintf(stderr, "Error: Invalid size '%s'\n", sizeStr);
                return FALSE;
            }
        }
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            PrintUsage(argv[0]);
            return FALSE;
        }
    }
    
    // 更新全局调试标志
    if (config->DebugMode) {
        g_DebugMode = TRUE;
    }
    
    // 验证配置
    if (config->BackendType == BACKEND_FILE && config->FilePath[0] == L'\0') {
        fprintf(stderr, "Error: File backend requires --file=<path>\n");
        return FALSE;
    }
    
    return TRUE;
}

/*===========================================================================
 * 驱动通信函数
 *===========================================================================*/

static BOOL OpenDriver(void)
{
    g_DriverHandle = CreateFileW(
        VNVME_USER_DEVICE,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    
    if (g_DriverHandle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        fprintf(stderr, "Error: Cannot open driver device (error %lu)\n", error);
        fprintf(stderr, "       Make sure vnvme.sys is loaded.\n");
        return FALSE;
    }
    
    if (g_Config.DebugMode) {
        printf("DEBUG: Opened driver device\n");
    }
    
    return TRUE;
}

static void CloseDriver(void)
{
    if (g_DriverHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_DriverHandle);
        g_DriverHandle = INVALID_HANDLE_VALUE;
    }
}

static BOOL GetDriverVersion(PUINT32 version)
{
    VNVME_GET_VERSION_OUTPUT output;
    DWORD bytesReturned;
    
    if (!DeviceIoControl(
            g_DriverHandle,
            IOCTL_VNVME_GET_VERSION,
            NULL, 0,
            &output, sizeof(output),
            &bytesReturned,
            NULL)) {
        return FALSE;
    }
    
    *version = output.DriverVersion;
    return TRUE;
}

static BOOL MapSharedMemory(void)
{
    VNVME_MAP_SHARED_MEMORY_OUTPUT output;
    DWORD bytesReturned;
    
    if (!DeviceIoControl(
            g_DriverHandle,
            IOCTL_VNVME_MAP_SHARED_MEMORY,
            NULL, 0,
            &output, sizeof(output),
            &bytesReturned,
            NULL)) {
        DWORD error = GetLastError();
        fprintf(stderr, "Error: Cannot map shared memory (error %lu)\n", error);
        return FALSE;
    }
    
    g_ShmAddress = output.UserAddress;
    g_ShmSize = output.ActualSize;
    
    if (g_Config.DebugMode) {
        printf("DEBUG: Mapped shared memory at %p, size %zu bytes\n",
               g_ShmAddress, g_ShmSize);
    }
    
    return TRUE;
}

static BOOL NotifyUserReady(void)
{
    VNVME_USER_READY_INPUT input;
    DWORD bytesReturned;
    
    input.UserPid = GetCurrentProcessId();
    input.UserVersion = VNVME_VERSION;
    input.Capabilities = VNVME_USER_CAP_ASYNC | VNVME_USER_CAP_BATCH;
    input.Reserved = 0;
    
    if (!DeviceIoControl(
            g_DriverHandle,
            IOCTL_VNVME_USER_READY,
            &input, sizeof(input),
            NULL, 0,
            &bytesReturned,
            NULL)) {
        DWORD error = GetLastError();
        fprintf(stderr, "Error: USER_READY failed (error %lu)\n", error);
        return FALSE;
    }
    
    if (g_Config.DebugMode) {
        printf("DEBUG: Notified driver that user-mode is ready\n");
    }
    
    return TRUE;
}

static BOOL SendHeartbeat(UINT64 commandsProcessed)
{
    VNVME_HEARTBEAT_INPUT input;
    VNVME_HEARTBEAT_OUTPUT output;
    DWORD bytesReturned;
    
    input.Timestamp = GetTickCount64();
    input.CommandsProcessed = commandsProcessed;
    
    if (!DeviceIoControl(
            g_DriverHandle,
            IOCTL_VNVME_HEARTBEAT,
            &input, sizeof(input),
            &output, sizeof(output),
            &bytesReturned,
            NULL)) {
        return FALSE;
    }
    
    return TRUE;
}

/*===========================================================================
 * 命令处理
 *===========================================================================*/

static UINT64 ProcessCommands(void)
{
    // 调用命令处理器处理所有待处理命令
    return CmdProcessorRun();
}

/**
 * @brief 检查内核是否请求关闭
 */
static BOOL IsShutdownRequested(void)
{
    if (g_ShmAddress != NULL) {
        PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm = 
            (PVNVME_SHARED_MEMORY_CONTROL_BLOCK)g_ShmAddress;
        return shm->ShutdownRequested != 0;
    }
    return FALSE;
}

/**
 * @brief 通知内核用户态关闭完成
 */
static void NotifyShutdownComplete(void)
{
    DWORD bytesReturned;
    
    // 通过心跳 IOCTL 或专用 IOCTL 通知内核
    // 这里使用心跳标记完成
    DeviceIoControl(
        g_DriverHandle,
        IOCTL_VNVME_HEARTBEAT,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL);
}

/*===========================================================================
 * 主循环
 *===========================================================================*/

static void MainLoop(void)
{
    UINT64 totalCommands = 0;
    DWORD lastHeartbeat = GetTickCount();
    
    printf("Entering main loop (Ctrl+C to exit)...\n\n");
    
    while (g_Running) {
        // 检查内核是否请求关闭
        if (IsShutdownRequested()) {
            printf("\nKernel requested shutdown...\n");
            g_Running = FALSE;
            break;
        }
        
        // 处理命令
        UINT64 processed = ProcessCommands();
        totalCommands += processed;
        
        // 定期发送心跳
        DWORD now = GetTickCount();
        if (now - lastHeartbeat >= 1000) {  // 每秒一次
            SendHeartbeat(totalCommands);
            lastHeartbeat = now;
            
            if (g_Config.DebugMode && processed > 0) {
                printf("DEBUG: Processed %llu commands, total %llu\n",
                       processed, totalCommands);
            }
        }
        
        // 短暂休眠避免 CPU 占用过高
        // TODO: 使用事件等待替代轮询
        Sleep(POLLING_INTERVAL_MS);
    }
    
    // 刷新后端存储
    if (g_Backend != NULL) {
        printf("Flushing storage backend...\n");
        BackendFlush(g_Backend);
    }
    
    // 通知内核关闭完成
    NotifyShutdownComplete();
    
    printf("\nShutdown complete. Total commands processed: %llu\n", totalCommands);
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

int main(int argc, char* argv[])
{
    UINT32 driverVersion;
    
    // 解析命令行参数
    if (!ParseArgs(argc, argv, &g_Config)) {
        return 1;
    }
    
    // 打印启动信息
    printf("VNVME Server v%d.%d.%d\n",
           VNVME_VERSION_MAJOR,
           VNVME_VERSION_MINOR,
           VNVME_VERSION_PATCH);
    printf("=====================================\n\n");
    
    // 安装控制台处理程序
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        fprintf(stderr, "Warning: Cannot set console handler\n");
    }
    
    // 打印配置
    printf("Configuration:\n");
    printf("  Backend:    %s\n", 
           g_Config.BackendType == BACKEND_MEMORY ? "memory" : "file");
    if (g_Config.BackendType == BACKEND_FILE) {
        printf("  File:       %ls\n", g_Config.FilePath);
    }
    printf("  Size:       %zu bytes (%.2f MB)\n", 
           g_Config.BackendSize,
           (double)g_Config.BackendSize / (1024.0 * 1024.0));
    printf("  Debug:      %s\n", g_Config.DebugMode ? "enabled" : "disabled");
    printf("\n");
    
    // 连接驱动
    printf("Connecting to driver...\n");
    if (!OpenDriver()) {
        return 1;
    }
    
    // 获取驱动版本
    if (GetDriverVersion(&driverVersion)) {
        printf("  Driver version: 0x%08X\n", driverVersion);
    }
    
    // 映射共享内存
    printf("Mapping shared memory...\n");
    if (!MapSharedMemory()) {
        CloseDriver();
        return 1;
    }
    printf("  Address:    %p\n", g_ShmAddress);
    printf("  Size:       %zu bytes\n", g_ShmSize);
    
    // 创建存储后端
    printf("Creating storage backend...\n");
    g_Backend = BackendCreate(g_Config.BackendType, g_Config.BackendSize, g_Config.FilePath);
    if (g_Backend == NULL) {
        fprintf(stderr, "Failed to create storage backend\n");
        CloseDriver();
        return 1;
    }
    
    // 初始化命令处理器
    printf("Initializing command processor...\n");
    if (!CmdProcessorInit(g_ShmAddress, g_Backend)) {
        fprintf(stderr, "Failed to initialize command processor\n");
        BackendDestroy(g_Backend);
        CloseDriver();
        return 1;
    }
    
    // 通知驱动用户态就绪
    printf("Notifying driver...\n");
    if (!NotifyUserReady()) {
        BackendDestroy(g_Backend);
        CloseDriver();
        return 1;
    }
    
    printf("\nService ready.\n\n");
    
    // 主循环
    MainLoop();
    
    // 清理
    if (g_Backend != NULL) {
        BackendDestroy(g_Backend);
        g_Backend = NULL;
    }
    CloseDriver();
    
    return 0;
}
