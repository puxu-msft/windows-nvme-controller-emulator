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
#include "../include/vnvme_ioctl.h"

#define VNVME_DEVICE_PATH L"\\\\.\\VNVMEControl"

/*===========================================================================
 * 辅助函数
 *===========================================================================*/

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

/*===========================================================================
 * 命令实现
 *===========================================================================*/

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
    printf("  Mapped: %s\n", status.SharedMemoryMapped ? "Yes" : "No");
    printf("  Size: %u bytes (%.2f MB)\n", status.SharedMemorySize,
           (double)status.SharedMemorySize / (1024.0 * 1024.0));
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
    printf("  test        - Run test suite\n");
    printf("  help        - Show this help\n");
    printf("\n");
}

/*===========================================================================
 * 主函数
 *===========================================================================*/

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
