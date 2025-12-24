/**
 * @file main_v2.c
 * @brief VNVME 用户态服务入口 (模块化版本 v2)
 * 
 * 此版本使用模块化架构:
 *   - config.c/.h      - 配置管理
 *   - logger.c/.h      - 日志系统
 *   - driver_comm.c/.h - 驱动通信
 *   - backend*.c/.h    - 存储后端
 *   - command_engine.c/.h - 命令引擎
 */

// 模块化头文件 (v2 架构 - 不再使用 vnvme_server.h)
#include "types.h"
#include "config.h"
#include "logger.h"
#include "driver_comm.h"
#include "backend.h"
#include "command_engine.h"

// 版本信息来自 vnvme_common.h
#include "../include/vnvme_common.h"

#include <stdio.h>

//===========================================================================
// 全局变量
//===========================================================================

volatile BOOL g_Running = TRUE;
BOOL g_DebugMode = FALSE;

static SERVER_CONFIG g_Config = {0};
static DRIVER_COMM_CONTEXT g_DriverCtx = {0};
static PBACKEND_CONTEXT g_Backend = NULL;
static PCMD_ENGINE_CONTEXT g_CmdEngine = NULL;

//===========================================================================
// 控制台处理程序
//===========================================================================

static BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType)
{
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            LogInfo("Shutdown requested...");
            g_Running = FALSE;
            return TRUE;
        default:
            return FALSE;
    }
}

//===========================================================================
// 主循环
//===========================================================================

static void MainLoop(void)
{
    UINT64 totalCommands = 0;
    DWORD lastHeartbeat = GetTickCount();
    const DWORD heartbeatInterval = 1000;  // 1秒
    const DWORD waitTimeout = 100;         // 100ms 等待超时 (允许定期心跳和关闭检查)
    
    // 尝试启用事件等待模式
    if (DriverGetCommandEvent(&g_DriverCtx)) {
        LogInfo("Event wait mode enabled (low CPU usage)");
    } else {
        LogInfo("Polling mode active (event not available)");
    }
    
    LogInfo("Entering main loop (Ctrl+C to exit)...");
    
    while (g_Running) {
        // 检查内核是否请求关闭
        if (DriverIsShutdownRequested(&g_DriverCtx)) {
            LogInfo("Kernel requested shutdown");
            g_Running = FALSE;
            break;
        }
        
        // 处理命令
        UINT64 processed = CmdEnginePoll(g_CmdEngine);
        totalCommands += processed;
        
        // 定期发送心跳
        DWORD now = GetTickCount();
        if (now - lastHeartbeat >= heartbeatInterval) {
            DriverSendHeartbeat(&g_DriverCtx);
            lastHeartbeat = now;
            
            if (processed > 0) {
                LogDebug("Processed %llu commands, total %llu", processed, totalCommands);
            }
        }
        
        // 等待新命令 (使用事件等待或轮询)
        // 如果刚处理了命令，继续检查是否有更多命令
        if (processed == 0) {
            DriverWaitForCommand(&g_DriverCtx, waitTimeout);
        }
    }
    
    // 刷新后端存储
    if (g_Backend != NULL) {
        LogInfo("Flushing storage backend...");
        BackendFlush(g_Backend);
    }
    
    LogInfo("Shutdown complete. Total commands processed: %llu", totalCommands);
}

//===========================================================================
// 打印配置
//===========================================================================

static void PrintStartupBanner(void)
{
    printf("\n");
    printf("  VNVME Server v%d.%d.%d\n",
           VNVME_VERSION_MAJOR, VNVME_VERSION_MINOR, VNVME_VERSION_PATCH);
    printf("  =====================================\n");
    printf("\n");
}

//===========================================================================
// 主函数
//===========================================================================

int main(int argc, char* argv[])
{
    int result = 0;
    LOGGER_CONFIG logConfig = {0};
    
    // 解析命令行参数
    ConfigSetDefaults(&g_Config);
    if (!ConfigParseArgs(argc, argv, &g_Config)) {
        return 1;
    }
    
    // 初始化日志系统
    logConfig.level = g_Config.log.level;
    logConfig.enableConsole = g_Config.log.enableConsole;
    logConfig.enableFile = g_Config.log.enableFile;
    logConfig.enableTimestamp = TRUE;
    wcscpy_s(logConfig.filePath, MAX_PATH, g_Config.log.filePath);
    
    if (!LogInit(&logConfig)) {
        fprintf(stderr, "Failed to initialize logging\n");
        return 1;
    }
    
    // 设置全局调试标志
    g_DebugMode = (g_Config.log.level >= LOG_LEVEL_DEBUG);
    
    // 打印启动信息
    PrintStartupBanner();
    ConfigPrint(&g_Config);
    
    // 安装控制台处理程序
    if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE)) {
        LogWarn("Cannot set console handler");
    }
    
    // 连接驱动
    LogInfo("Connecting to driver...");
    if (!DriverConnect(&g_DriverCtx)) {
        LogError("Cannot connect to driver. Make sure vnvme.sys is loaded.");
        result = 1;
        goto cleanup_log;
    }
    LogInfo("Driver version: 0x%08X", g_DriverCtx.driverVersion);
    
    // 映射共享内存
    LogInfo("Mapping SHM...");
    if (!DriverMapShm(&g_DriverCtx)) {
        LogError("Cannot map SHM");
        result = 1;
        goto cleanup_driver;
    }
    LogInfo("SHM: address=%p, size=%zu bytes",
            g_DriverCtx.shm.userAddress, g_DriverCtx.shm.size);
    
    // 创建存储后端
    LogInfo("Creating storage backend...");
    BACKEND_CONFIG backendConfig = {0};
    backendConfig.Type = g_Config.storage.backendType;
    backendConfig.Size = g_Config.storage.size;
    backendConfig.BlockSize = g_Config.storage.blockSize;
    backendConfig.ReadOnly = g_Config.storage.readOnly;
    backendConfig.DirectIO = g_Config.storage.directIO;
    wcscpy_s(backendConfig.FilePath, MAX_PATH, g_Config.storage.filePath);
    
    g_Backend = BackendCreate(&backendConfig);
    if (g_Backend == NULL) {
        LogError("Failed to create storage backend");
        result = 1;
        goto cleanup_driver;
    }
    LogInfo("Backend: type=%s, size=%llu bytes",
            BackendGetTypeName(backendConfig.Type),
            BackendGetSize(g_Backend));
    
    // 创建并初始化命令引擎
    LogInfo("Initializing command engine...");
    g_CmdEngine = CmdEngineCreate();
    if (g_CmdEngine == NULL) {
        LogError("Failed to create command engine");
        result = 1;
        goto cleanup_backend;
    }
    
    if (!CmdEngineInitSimple(g_CmdEngine, g_DriverCtx.shm.userAddress, g_Backend)) {
        LogError("Failed to initialize command engine");
        result = 1;
        goto cleanup_cmdengine;
    }
    CmdEngineSetDebugMode(g_CmdEngine, g_DebugMode);
    
    // 通知驱动用户态就绪
    LogInfo("Notifying driver...");
    if (!DriverSendUserReady(&g_DriverCtx)) {
        LogError("Failed to notify driver");
        result = 1;
        goto cleanup_backend;
    }
    
    LogInfo("Service ready.");
    
    // 主循环
    MainLoop();
    
    // 清理
cleanup_cmdengine:
    if (g_CmdEngine != NULL) {
        CmdEngineDestroy(g_CmdEngine);
        g_CmdEngine = NULL;
    }
    
cleanup_backend:
    if (g_Backend != NULL) {
        BackendDestroy(g_Backend);
        g_Backend = NULL;
    }
    
cleanup_driver:
    DriverDisconnect(&g_DriverCtx);
    
cleanup_log:
    LogShutdown();
    
    return result;
}
