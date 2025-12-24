/**
 * @file test_driver_comm.c
 * @brief VNVME 驱动通信测试
 * 
 * 测试驱动基础 IOCTL 通信功能
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../include/vnvme_common.h"
#include "../include/vnvme_ioctl.h"

#define TEST_PASS   0
#define TEST_FAIL   1
#define TEST_SKIP   2

static int g_testsPassed = 0;
static int g_testsFailed = 0;
static int g_testsSkipped = 0;

#define TEST_BEGIN(name) \
    printf("  [TEST] %s ... ", name)

#define TEST_END_PASS() \
    do { printf("PASS\n"); g_testsPassed++; return TEST_PASS; } while(0)

#define TEST_END_FAIL(msg) \
    do { printf("FAIL: %s\n", msg); g_testsFailed++; return TEST_FAIL; } while(0)

#define TEST_END_SKIP(msg) \
    do { printf("SKIP: %s\n", msg); g_testsSkipped++; return TEST_SKIP; } while(0)

//===========================================================================
// 辅助函数
//===========================================================================

static HANDLE OpenDevice(void)
{
    return CreateFileW(
        VNVME_CONTROL_USER_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
}

//===========================================================================
// 测试用例
//===========================================================================

static int TestOpenDevice(void)
{
    HANDLE hDevice;
    
    TEST_BEGIN("Open device");
    
    hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            TEST_END_SKIP("Driver not loaded");
        }
        char msg[64];
        sprintf_s(msg, sizeof(msg), "Error %lu", err);
        TEST_END_FAIL(msg);
    }
    
    CloseHandle(hDevice);
    TEST_END_PASS();
}

static int TestGetVersion(void)
{
    HANDLE hDevice;
    VNVME_GET_VERSION_OUTPUT version = {0};
    DWORD bytesReturned;
    BOOL result;
    
    TEST_BEGIN("Get version");
    
    hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        TEST_END_SKIP("Driver not loaded");
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_VERSION,
        NULL, 0,
        &version, sizeof(version),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        char msg[64];
        sprintf_s(msg, sizeof(msg), "IOCTL failed: %lu", GetLastError());
        TEST_END_FAIL(msg);
    }
    
    if (bytesReturned != sizeof(version)) {
        TEST_END_FAIL("Wrong output size");
    }
    
    if (version.DriverVersion == 0) {
        TEST_END_FAIL("Version is 0");
    }
    
    printf("v%u.%u.%u ", 
           (version.DriverVersion >> 16) & 0xFF,
           (version.DriverVersion >> 8) & 0xFF,
           version.DriverVersion & 0xFF);
    
    TEST_END_PASS();
}

static int TestGetStatus(void)
{
    HANDLE hDevice;
    VNVME_GET_STATUS_OUTPUT status = {0};
    DWORD bytesReturned;
    BOOL result;
    
    TEST_BEGIN("Get status");
    
    hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        TEST_END_SKIP("Driver not loaded");
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_GET_STATUS,
        NULL, 0,
        &status, sizeof(status),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        char msg[64];
        sprintf_s(msg, sizeof(msg), "IOCTL failed: %lu", GetLastError());
        TEST_END_FAIL(msg);
    }
    
    if (bytesReturned != sizeof(status)) {
        TEST_END_FAIL("Wrong output size");
    }
    
    printf("state=%u ", status.DriverStatus);
    
    TEST_END_PASS();
}

static int TestMapShm(void)
{
    HANDLE hDevice;
    VNVME_MAP_SHM_INPUT input = {0};
    VNVME_MAP_SHM_OUTPUT output = {0};
    DWORD bytesReturned;
    BOOL result;
    
    TEST_BEGIN("Map SHM");
    
    hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        TEST_END_SKIP("Driver not loaded");
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_MAP_SHM,
        &input, sizeof(input),
        &output, sizeof(output),
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        DWORD err = GetLastError();
        CloseHandle(hDevice);
        char msg[64];
        sprintf_s(msg, sizeof(msg), "IOCTL failed: %lu", err);
        TEST_END_FAIL(msg);
    }
    
    // 验证返回的地址
    if (output.UserAddress == NULL) {
        CloseHandle(hDevice);
        TEST_END_FAIL("UserAddress is NULL");
    }
    
    if (output.ActualSize == 0) {
        CloseHandle(hDevice);
        TEST_END_FAIL("Size is 0");
    }
    
    // 验证控制块魔数
    PVNVME_SHM_CONTROL_BLOCK shm = (PVNVME_SHM_CONTROL_BLOCK)output.UserAddress;
    if (shm->Magic != VNVME_SHM_MAGIC) {
        CloseHandle(hDevice);
        char msg[64];
        sprintf_s(msg, sizeof(msg), "Bad magic: 0x%08X", shm->Magic);
        TEST_END_FAIL(msg);
    }
    
    printf("addr=%p size=%u ", output.UserAddress, output.ActualSize);
    
    // 取消映射
    DeviceIoControl(
        hDevice,
        IOCTL_VNVME_UNMAP_SHM,
        NULL, 0,
        NULL, 0,
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    TEST_END_PASS();
}

static int TestListControllers(void)
{
    HANDLE hDevice;
    VNVME_LIST_CONTROLLERS_OUTPUT list = {0};
    DWORD bytesReturned;
    BOOL result;
    
    TEST_BEGIN("List controllers");
    
    hDevice = OpenDevice();
    if (hDevice == INVALID_HANDLE_VALUE) {
        TEST_END_SKIP("Driver not loaded");
    }
    
    result = DeviceIoControl(
        hDevice,
        IOCTL_VNVME_LIST_CONTROLLERS,
        NULL, 0,
        &list, sizeof(list),
        &bytesReturned,
        NULL
        );
    
    CloseHandle(hDevice);
    
    if (!result) {
        char msg[64];
        sprintf_s(msg, sizeof(msg), "IOCTL failed: %lu", GetLastError());
        TEST_END_FAIL(msg);
    }
    
    printf("count=%u ", list.ControllerCount);
    
    TEST_END_PASS();
}

//===========================================================================
// 主函数
//===========================================================================

int main(int argc, char* argv[])
{
    (void)argc;
    (void)argv;
    
    printf("=== VNVME Driver Communication Tests ===\n\n");
    
    printf("Driver IOCTL Tests:\n");
    TestOpenDevice();
    TestGetVersion();
    TestGetStatus();
    TestMapShm();
    TestListControllers();
    
    printf("\n=== Summary ===\n");
    printf("Passed:  %d\n", g_testsPassed);
    printf("Failed:  %d\n", g_testsFailed);
    printf("Skipped: %d\n", g_testsSkipped);
    
    return g_testsFailed > 0 ? 1 : 0;
}
