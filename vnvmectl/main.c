/**
 * @file main.c
 * @brief VNVME 命令行工具
 * 
 * 用于与 VNVME 驱动进行交互的命令行工具。
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/vnvme_common.h"
#include "../include/vnvme_ioctl.h"

#define VNVME_DEVICE_PATH L"\\\\.\\VNVMEControl"

//===========================================================================
// 辅助函数
//===========================================================================

static HANDLE OpenVnvmeDevice(void)
{
    HANDLE hDevice;
    
    hDevice = CreateFileW(
        VNVME_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    
    if (hDevice == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Error: Failed to open device %ls (Error: %lu)\n",
                VNVME_DEVICE_PATH, GetLastError());
    }
    
    return hDevice;
}

//===========================================================================
// 命令实现
//===========================================================================

static int CmdVersion(void)
{
    HANDLE hDevice;
    VNVME_GET_VERSION_OUTPUT version = {0};
    DWORD bytesReturned;
    BOOL result;
    
    printf("Getting driver version...\n");
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_VERSION,
        NULL, 0U,
        &version, (DWORD)sizeof(version),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_GET_VERSION failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("VNVME Driver Version: 0x%08X (API: 0x%08X, Build: %u)\n",
           version.DriverVersion, version.ApiVersion, version.BuildNumber);
    
    return 0;
}

static int CmdStatus(void)
{
    HANDLE hDevice;
    VNVME_GET_STATUS_OUTPUT status = {0};
    DWORD bytesReturned;
    BOOL result;
    
    printf("Getting driver status...\n");
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_STATUS,
        NULL, 0U,
        &status, (DWORD)sizeof(status),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_GET_STATUS failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("Driver Status:\n");
    printf("  Driver State: %u (%s)\n", status.DriverStatus,
           status.DriverStatus == 0 ? "Initializing" :
           status.DriverStatus == 1 ? "Ready" :
           status.DriverStatus == 2 ? "Running" :
           status.DriverStatus == 3 ? "Error" :
           status.DriverStatus == 4 ? "Stopping" : "Unknown");
    printf("  User Service State: %u (%s)\n", status.UserServiceStatus,
           status.UserServiceStatus == 0 ? "Not Connected" :
           status.UserServiceStatus == 1 ? "Connected" :
           status.UserServiceStatus == 2 ? "Ready" :
           status.UserServiceStatus == 3 ? "Error" : "Unknown");
    printf("  User Ready: %s\n", status.UserReady ? "Yes" : "No");
    printf("  User PID: %u\n", status.UserPid);
    printf("\n");
    printf("Shared Memory:\n");
    printf("  Mapped: %s\n", status.ShmMapped ? "Yes" : "No");
    printf("  Size: %u bytes (%.2f MB)\n", status.ShmSize,
           (double)status.ShmSize / (1024.0 * 1024.0));
    printf("\n");
    printf("Devices:\n");
    printf("  Controller Count: %u\n", status.ControllerCount);
    printf("  Namespace Count: %u\n", status.NamespaceCount);
    printf("\n");
    printf("Statistics:\n");
    printf("  Commands Processed: %llu\n", (unsigned long long)status.CommandsProcessed);
    printf("  Completions Posted: %llu\n", (unsigned long long)status.CompletionsPosted);
    printf("  Bytes Read: %llu (%.2f MB)\n", (unsigned long long)status.BytesRead,
           (double)status.BytesRead / (1024.0 * 1024.0));
    printf("  Bytes Written: %llu (%.2f MB)\n", (unsigned long long)status.BytesWritten,
           (double)status.BytesWritten / (1024.0 * 1024.0));
    printf("  Errors: %llu\n", (unsigned long long)status.ErrorCount);
    printf("\n");
    printf("Timing:\n");
    printf("  Uptime: %llu ms (%.2f hours)\n", (unsigned long long)status.UptimeMs,
           (double)status.UptimeMs / (1000.0 * 60.0 * 60.0));
    printf("  Last Heartbeat: %llu ms ago\n", (unsigned long long)status.LastHeartbeatMs);
    
    return 0;
}

static int CmdListControllers(void)
{
    HANDLE hDevice;
    VNVME_LIST_CONTROLLERS_OUTPUT list = {0};
    DWORD bytesReturned;
    BOOL result;
    ULONG i;
    
    printf("Listing controllers...\n");
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_LIST_CONTROLLERS,
        NULL, 0U,
        &list, (DWORD)sizeof(list),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_LIST_CONTROLLERS failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("Controllers (%u total):\n", list.ControllerCount);
    for (i = 0; i < list.ControllerCount && i < 16; i++) {
        printf("  [%u] ID=%u, Status=%u, Model=%s, SN=%s\n", 
               i, 
               list.Controllers[i].ControllerId,
               list.Controllers[i].Status,
               list.Controllers[i].ModelNumber,
               list.Controllers[i].SerialNumber);
    }
    
    if (list.ControllerCount == 0) {
        printf("  (No controllers created)\n");
    }
    
    return 0;
}

/**
 * @brief 解析大小字符串 (支持 K/M/G/T 后缀)
 */
static UINT64 ParseSizeString(const char* str)
{
    UINT64 value = 0;
    char suffix = 0;
    int len = (int)strlen(str);
    
    if (len == 0) return 0;
    
    // 检查后缀
    suffix = str[len - 1];
    if (suffix == 'K' || suffix == 'k' || 
        suffix == 'M' || suffix == 'm' || 
        suffix == 'G' || suffix == 'g' ||
        suffix == 'T' || suffix == 't') {
        char* temp = _strdup(str);
        if (temp) {
            temp[len - 1] = '\0';
            value = (UINT64)_atoi64(temp);
            free(temp);
        }
    } else {
        value = (UINT64)_atoi64(str);
        suffix = 0;
    }
    
    // 应用后缀乘数
    switch (suffix) {
        case 'K': case 'k': value *= 1024ULL; break;
        case 'M': case 'm': value *= 1024ULL * 1024; break;
        case 'G': case 'g': value *= 1024ULL * 1024 * 1024; break;
        case 'T': case 't': value *= 1024ULL * 1024 * 1024 * 1024; break;
    }
    
    return value;
}

static int CmdCreate(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_CREATE_CONTROLLER_INPUT input = {0};
    VNVME_CREATE_CONTROLLER_OUTPUT output = {0};
    DWORD bytesReturned;
    BOOL result;
    int i;
    
    // 默认值
    input.Config.NamespaceSize = 64ULL * 1024 * 1024;  // 64 MB
    input.Config.BlockSize = 512;
    input.Config.StorageType = 1;  // VNVME_STORAGE_TYPE_MEMORY
    input.Config.MaxNamespaces = 1;
    input.Config.MaxQueuePairs = 16;
    input.Config.MaxQueueDepth = 256;
    input.Config.MaxTransferSize = 1024 * 1024;  // 1 MB
    strcpy_s(input.Config.ModelNumber, sizeof(input.Config.ModelNumber), "Virtual NVMe");
    strcpy_s(input.Config.SerialNumber, sizeof(input.Config.SerialNumber), "VNVME00001");
    strcpy_s(input.Config.FirmwareRevision, sizeof(input.Config.FirmwareRevision), "1.0.0");
    
    // 解析参数
    for (i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            input.Config.NamespaceSize = ParseSizeString(argv[i] + 7);
            if (input.Config.NamespaceSize == 0) {
                fprintf(stderr, "Error: Invalid size '%s'\n", argv[i] + 7);
                return 1;
            }
        }
        else if (strncmp(argv[i], "--backend=", 10) == 0) {
            const char* backend = argv[i] + 10;
            if (strcmp(backend, "memory") == 0) {
                input.Config.StorageType = 1;
            }
            else if (strcmp(backend, "file") == 0) {
                input.Config.StorageType = 2;
            }
            else {
                fprintf(stderr, "Error: Unknown backend '%s'\n", backend);
                return 1;
            }
        }
        else if (strncmp(argv[i], "--file=", 7) == 0) {
            if (MultiByteToWideChar(CP_UTF8, 0, argv[i] + 7, -1, 
                                   input.Config.FilePath, 260) == 0) {
                fprintf(stderr, "Error: Invalid file path '%s'\n", argv[i] + 7);
                return 1;
            }
        }
        else if (strncmp(argv[i], "--model=", 8) == 0) {
            strncpy_s(input.Config.ModelNumber, sizeof(input.Config.ModelNumber), 
                     argv[i] + 8, _TRUNCATE);
        }
        else if (strncmp(argv[i], "--serial=", 9) == 0) {
            strncpy_s(input.Config.SerialNumber, sizeof(input.Config.SerialNumber), 
                     argv[i] + 9, _TRUNCATE);
        }
        else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            return 1;
        }
    }
    
    // 验证
    if (input.Config.StorageType == 2 && input.Config.FilePath[0] == L'\0') {
        fprintf(stderr, "Error: File backend requires --file=<path>\n");
        return 1;
    }
    
    printf("Creating controller...\n");
    printf("  Size: %llu bytes (%.2f MB)\n", 
           (unsigned long long)input.Config.NamespaceSize,
           (double)input.Config.NamespaceSize / (1024.0 * 1024.0));
    printf("  Backend: %s\n", input.Config.StorageType == 1 ? "memory" : "file");
    printf("  Model: %s\n", input.Config.ModelNumber);
    printf("  Serial: %s\n", input.Config.SerialNumber);
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_CREATE_CONTROLLER,
        &input, (DWORD)sizeof(input),
        &output, (DWORD)sizeof(output),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_CREATE_CONTROLLER failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("\nController created successfully!\n");
    printf("  Controller ID: %u\n", output.ControllerId);
    
    return 0;
}

static int CmdDelete(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_DELETE_CONTROLLER_INPUT input = {0};
    DWORD bytesReturned;
    BOOL result;
    
    if (argc < 3) {
        fprintf(stderr, "Usage: vnvmectl delete <controller_id>\n");
        return 1;
    }
    
    input.ControllerId = (ULONG)atoi(argv[2]);
    if (input.ControllerId == 0 && strcmp(argv[2], "0") != 0) {
        fprintf(stderr, "Error: Invalid controller ID '%s'\n", argv[2]);
        return 1;
    }
    
    printf("Deleting controller %u...\n", input.ControllerId);
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_DELETE_CONTROLLER,
        &input, (DWORD)sizeof(input),
        NULL, 0U,
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_DELETE_CONTROLLER failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("Controller %u deleted successfully.\n", input.ControllerId);
    
    return 0;
}

//===========================================================================
// 命名空间命令
//===========================================================================

static int CmdListNamespaces(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_GET_STATS_INPUT input = {0};
    VNVME_GET_STATS_OUTPUT stats = {0};
    DWORD bytesReturned;
    BOOL result;
    ULONG i;
    
    // 解析控制器 ID (可选)
    if (argc >= 3) {
        input.ControllerId = (UINT32)atoi(argv[2]);
    }
    
    printf("Listing namespaces%s...\n", 
           input.ControllerId ? " for controller" : " (all controllers)");
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_STATS,
        &input, (DWORD)sizeof(input),
        &stats, (DWORD)sizeof(stats),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_GET_STATS failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("\nNamespaces (%u total):\n", stats.TotalNamespaceCount);
    printf("  %-6s %-8s %-12s %-10s %-12s %-12s\n", 
           "NSID", "Active", "Blocks", "BlockSize", "Reads", "Writes");
    printf("  ------ -------- ------------ ---------- ------------ ------------\n");
    
    for (i = 0; i < stats.TotalNamespaceCount && i < VNVME_MAX_STATS_NAMESPACES; i++) {
        PVNVME_NAMESPACE_STATS ns = &stats.Namespaces[i];
        printf("  %-6u %-8s %-12llu %-10u %-12llu %-12llu\n",
               ns->NSID,
               ns->Active ? "Yes" : "No",
               (unsigned long long)ns->TotalBlocks,
               ns->BlockSize,
               (unsigned long long)ns->ReadCommands,
               (unsigned long long)ns->WriteCommands);
    }
    
    if (stats.TotalNamespaceCount == 0) {
        printf("  (No namespaces created)\n");
    }
    
    return 0;
}

static int CmdCreateNamespace(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_CREATE_NAMESPACE_INPUT input = {0};
    VNVME_CREATE_NAMESPACE_OUTPUT output = {0};
    DWORD bytesReturned;
    BOOL result;
    int i;
    
    // 必须指定控制器 ID
    if (argc < 3) {
        fprintf(stderr, "Usage: %s ns-create <controller_id> [options]\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --size=<size>  Namespace size (e.g., 64M, 1G)\n");
        return 1;
    }
    
    input.ControllerId = (UINT32)atoi(argv[2]);
    input.Config.BlockSize = 512;
    input.Config.TotalBlocks = (64ULL * 1024 * 1024) / 512;  // 默认 64MB
    input.Config.Flags = VNVME_NS_FLAG_ENABLED;
    
    // 解析参数
    for (i = 3; i < argc; i++) {
        if (strncmp(argv[i], "--size=", 7) == 0) {
            UINT64 sizeBytes = ParseSizeString(argv[i] + 7);
            input.Config.TotalBlocks = sizeBytes / input.Config.BlockSize;
        }
    }
    
    printf("Creating namespace on controller %u (blocks: %llu, block size: %u)...\n",
           input.ControllerId, 
           (unsigned long long)input.Config.TotalBlocks,
           input.Config.BlockSize);
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_CREATE_NAMESPACE,
        &input, (DWORD)sizeof(input),
        &output, (DWORD)sizeof(output),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_CREATE_NAMESPACE failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("Namespace created successfully. NSID: %u\n", output.NSID);
    
    return 0;
}

static int CmdDeleteNamespace(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_DELETE_NAMESPACE_INPUT input = {0};
    DWORD bytesReturned;
    BOOL result;
    
    if (argc < 4) {
        fprintf(stderr, "Usage: %s ns-delete <controller_id> <nsid>\n", argv[0]);
        return 1;
    }
    
    input.ControllerId = (UINT32)atoi(argv[2]);
    input.NSID = (UINT32)atoi(argv[3]);
    
    printf("Deleting namespace %u from controller %u...\n", 
           input.NSID, input.ControllerId);
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_DELETE_NAMESPACE,
        &input, (DWORD)sizeof(input),
        NULL, 0,
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_DELETE_NAMESPACE failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("Namespace %u deleted successfully.\n", input.NSID);
    
    return 0;
}

//===========================================================================
// 统计和调试命令
//===========================================================================

static int CmdStats(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_GET_STATS_INPUT input = {0};
    VNVME_GET_STATS_OUTPUT stats = {0};
    DWORD bytesReturned;
    BOOL result;
    ULONG i;
    
    // 解析控制器 ID (可选)
    if (argc >= 3) {
        input.ControllerId = (UINT32)atoi(argv[2]);
    }
    
    printf("Getting statistics%s...\n",
           input.ControllerId ? " for controller" : " (all controllers)");
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_STATS,
        &input, (DWORD)sizeof(input),
        &stats, (DWORD)sizeof(stats),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_GET_STATS failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("\n=== System Statistics ===\n");
    printf("Uptime: %llu ms (%.2f hours)\n", 
           (unsigned long long)stats.Uptime,
           (double)stats.Uptime / (1000.0 * 60.0 * 60.0));
    printf("Controllers: %u\n", stats.ControllerCount);
    printf("Namespaces: %u\n", stats.TotalNamespaceCount);
    printf("Total Commands: %llu\n", (unsigned long long)stats.TotalCommandsProcessed);
    
    printf("\n=== Controller Statistics ===\n");
    for (i = 0; i < stats.ControllerCount && i < VNVME_MAX_STATS_CONTROLLERS; i++) {
        PVNVME_CONTROLLER_STATS ctrl = &stats.Controllers[i];
        printf("\nController %u:\n", ctrl->ControllerId);
        printf("  Namespaces: %u\n", ctrl->NamespaceCount);
        printf("  I/O Queues: %u\n", ctrl->IoQueueCount);
        printf("  Admin Commands: %llu\n", (unsigned long long)ctrl->AdminCommandsProcessed);
        printf("  I/O Commands: %llu\n", (unsigned long long)ctrl->IoCommandsProcessed);
        printf("  Total Read: %llu bytes (%.2f MB)\n", 
               (unsigned long long)ctrl->TotalReadBytes,
               (double)ctrl->TotalReadBytes / (1024.0 * 1024.0));
        printf("  Total Write: %llu bytes (%.2f MB)\n",
               (unsigned long long)ctrl->TotalWriteBytes,
               (double)ctrl->TotalWriteBytes / (1024.0 * 1024.0));
        printf("  Polling Interval: %u us\n", ctrl->PollingIntervalUs);
    }
    
    printf("\n=== Namespace Statistics ===\n");
    for (i = 0; i < stats.TotalNamespaceCount && i < VNVME_MAX_STATS_NAMESPACES; i++) {
        PVNVME_NAMESPACE_STATS ns = &stats.Namespaces[i];
        printf("\nNamespace %u:\n", ns->NSID);
        printf("  Status: %s\n", ns->Active ? "Active" : "Inactive");
        printf("  Size: %llu blocks x %u bytes = %.2f MB\n",
               (unsigned long long)ns->TotalBlocks, ns->BlockSize,
               (double)(ns->TotalBlocks * ns->BlockSize) / (1024.0 * 1024.0));
        printf("  Read Commands: %llu (%llu bytes)\n",
               (unsigned long long)ns->ReadCommands,
               (unsigned long long)ns->ReadBytes);
        printf("  Write Commands: %llu (%llu bytes)\n",
               (unsigned long long)ns->WriteCommands,
               (unsigned long long)ns->WriteBytes);
        printf("  Flush Commands: %llu\n", (unsigned long long)ns->FlushCommands);
    }
    
    return 0;
}

static int CmdDebug(int argc, char* argv[])
{
    HANDLE hDevice;
    VNVME_SET_DEBUG_LEVEL_INPUT input = {0};
    DWORD bytesReturned;
    BOOL result;
    
    if (argc < 3) {
        printf("Current debug levels:\n");
        printf("  0 - None (errors only)\n");
        printf("  1 - Error\n");
        printf("  2 - Warning\n");
        printf("  3 - Info\n");
        printf("  4 - Debug\n");
        printf("  5 - Verbose (all)\n");
        printf("\nUsage: %s debug <level> [flags]\n", argv[0]);
        printf("Flags (hex, can combine):\n");
        printf("  0x0001 - Trace IOCTL\n");
        printf("  0x0002 - Trace Commands\n");
        printf("  0x0004 - Trace DMA\n");
        printf("  0x0008 - Trace Queue\n");
        printf("  0xFFFF - Trace All\n");
        return 0;
    }
    
    input.DebugLevel = (UINT32)atoi(argv[2]);
    if (argc >= 4) {
        input.DebugFlags = (UINT32)strtoul(argv[3], NULL, 0);
    }
    
    printf("Setting debug level to %u (flags: 0x%04X)...\n", 
           input.DebugLevel, input.DebugFlags);
    
    hDevice = OpenVnvmeDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        return 1;
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_SET_DEBUG_LEVEL,
        &input, (DWORD)sizeof(input),
        NULL, 0,
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        fprintf(stderr, "Error: IOCTL_VNVME_SET_DEBUG_LEVEL failed (Error: %lu)\n",
                GetLastError());
        return 1;
    }
    
    printf("Debug level set successfully.\n");
    
    return 0;
}

//===========================================================================
// 测试命令
//===========================================================================

static int CmdTest(void)
{
    int result = 0;
    
    printf("=== VNVME Driver Test Suite ===\n\n");
    
    printf("Test 1: Get Version\n");
    printf("--------------------\n");
    if (CmdVersion() != 0) {
        printf("FAILED: Could not get version\n");
        result = 1;
    } else {
        printf("PASSED\n");
    }
    printf("\n");
    
    printf("Test 2: Get Status\n");
    printf("------------------\n");
    if (CmdStatus() != 0) {
        printf("FAILED: Could not get status\n");
        result = 1;
    } else {
        printf("PASSED\n");
    }
    printf("\n");
    
    printf("Test 3: List Controllers\n");
    printf("------------------------\n");
    if (CmdListControllers() != 0) {
        printf("FAILED: Could not list controllers\n");
        result = 1;
    } else {
        printf("PASSED\n");
    }
    printf("\n");
    
    printf("=== Test Summary ===\n");
    if (result == 0) {
        printf("All tests PASSED\n");
    } else {
        printf("Some tests FAILED\n");
    }
    
    return result;
}

static void PrintUsage(const char* progName)
{
    printf("VNVME Command Line Tool\n");
    printf("Usage: %s <command> [options]\n", progName);
    printf("\n");
    printf("Commands:\n");
    printf("  version     - Show driver version\n");
    printf("  status      - Show driver status\n");
    printf("  list        - List controllers\n");
    printf("  create      - Create a new controller\n");
    printf("  delete      - Delete a controller\n");
    printf("  ns-list     - List namespaces\n");
    printf("  ns-create   - Create a namespace\n");
    printf("  ns-delete   - Delete a namespace\n");
    printf("  stats       - Show detailed statistics\n");
    printf("  debug       - Set debug level\n");
    printf("  test        - Run test suite\n");
    printf("  help        - Show this help\n");
    printf("\n");
    printf("Create Controller Options:\n");
    printf("  --size=<size>       Namespace size (e.g., 64M, 1G, 256M)\n");
    printf("  --backend=<type>    Backend type: memory (default), file\n");
    printf("  --file=<path>       File path (required for file backend)\n");
    printf("  --model=<name>      Controller model name\n");
    printf("  --serial=<sn>       Controller serial number\n");
    printf("\n");
    printf("Namespace Commands:\n");
    printf("  ns-list [controller_id]          List namespaces\n");
    printf("  ns-create <controller_id> [--size=<size>]\n");
    printf("  ns-delete <controller_id> <nsid>\n");
    printf("\n");
    printf("Debug Command:\n");
    printf("  debug <level> [flags]  Set debug level (0-5) and flags\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s create --size=128M                    # 128MB memory backend\n", progName);
    printf("  %s create --backend=file --file=disk.img --size=1G\n", progName);
    printf("  %s delete 1                              # Delete controller 1\n", progName);
    printf("  %s ns-create 1 --size=64M                # Create 64MB namespace\n", progName);
    printf("  %s stats                                 # Show all statistics\n", progName);
    printf("  %s debug 4 0xFFFF                        # Enable all debug traces\n", progName);
    printf("\n");
}

//===========================================================================
// 主函数
//===========================================================================

int main(int argc, char* argv[])
{
    if (argc < 2) {
        PrintUsage(argv[0]);
        return 0;
    }
    
    if (_stricmp(argv[1], "version") == 0) {
        return CmdVersion();
    }
    else if (_stricmp(argv[1], "status") == 0) {
        return CmdStatus();
    }
    else if (_stricmp(argv[1], "list") == 0) {
        return CmdListControllers();
    }
    else if (_stricmp(argv[1], "create") == 0) {
        return CmdCreate(argc, argv);
    }
    else if (_stricmp(argv[1], "delete") == 0) {
        return CmdDelete(argc, argv);
    }
    else if (_stricmp(argv[1], "ns-list") == 0) {
        return CmdListNamespaces(argc, argv);
    }
    else if (_stricmp(argv[1], "ns-create") == 0) {
        return CmdCreateNamespace(argc, argv);
    }
    else if (_stricmp(argv[1], "ns-delete") == 0) {
        return CmdDeleteNamespace(argc, argv);
    }
    else if (_stricmp(argv[1], "stats") == 0) {
        return CmdStats(argc, argv);
    }
    else if (_stricmp(argv[1], "debug") == 0) {
        return CmdDebug(argc, argv);
    }
    else if (_stricmp(argv[1], "test") == 0) {
        return CmdTest();
    }
    else if (_stricmp(argv[1], "help") == 0 || 
             _stricmp(argv[1], "-h") == 0 ||
             _stricmp(argv[1], "--help") == 0) {
        PrintUsage(argv[0]);
        return 0;
    }
    else {
        fprintf(stderr, "Unknown command: %s\n", argv[1]);
        PrintUsage(argv[0]);
        return 1;
    }
}
