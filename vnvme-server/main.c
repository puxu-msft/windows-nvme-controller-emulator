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

static volatile BOOL g_Running = TRUE;
static HANDLE g_DriverHandle = INVALID_HANDLE_VALUE;
static SERVER_CONFIG g_Config = {0};
static PVOID g_ShmAddress = NULL;                   // 共享内存地址
static SIZE_T g_ShmSize = 0;                        // 共享内存大小

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

static void PrintUsage(const char* progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help          Show this help message\n");
    printf("  -v, --version       Show version information\n");
    printf("  -d, --debug         Enable debug output\n");
    printf("  --backend=<type>    Storage backend: memory (default), file\n");
    printf("  --file=<path>       File path for file backend\n");
    printf("  --size=<bytes>      Backend size in bytes (default: 64MB)\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s                          # Use memory backend (64MB)\n", progname);
    printf("  %s --backend=memory --size=134217728  # 128MB memory\n", progname);
    printf("  %s --backend=file --file=disk.img     # File backend\n", progname);
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
            config->BackendSize = (SIZE_T)_atoi64(sizeStr);
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
    UINT64 processedCount = 0;
    
    // TODO Phase 3: 实现真正的命令处理
    // 1. 读取 NotifyRing 检查是否有新命令
    // 2. 遍历各 SQ 检查新命令
    // 3. 处理 Admin 命令和 I/O 命令
    // 4. 写入 CQ 完成条目
    // 5. 调用 IOCTL_VNVME_SUBMIT_COMPLETIONS
    
    // 当前: 仅检查共享内存有效性
    if (g_ShmAddress != NULL) {
        PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm = VnvmeGetControlBlock(g_ShmAddress);
        
        // 验证 magic
        if (shm->Magic != VNVME_SHARED_MEMORY_MAGIC) {
            if (g_Config.DebugMode) {
                static BOOL warned = FALSE;
                if (!warned) {
                    printf("DEBUG: Waiting for shared memory initialization...\n");
                    warned = TRUE;
                }
            }
        }
    }
    
    return processedCount;
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
    
    // 通知驱动用户态就绪
    printf("Notifying driver...\n");
    if (!NotifyUserReady()) {
        CloseDriver();
        return 1;
    }
    
    printf("\nService ready.\n\n");
    
    // 主循环
    MainLoop();
    
    // 清理
    CloseDriver();
    
    return 0;
}
