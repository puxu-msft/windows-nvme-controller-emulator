/**
 * @file driver_comm.c
 * @brief 驱动通信模块实现
 */

#define LOG_MODULE "driver"

#include "driver_comm.h"
#include "logger.h"
#include <stdio.h>

//===========================================================================
// 常量
//===========================================================================

#define VNVME_DEVICE_PATH   L"\\\\.\\vnvme"

//===========================================================================
// 心跳线程
//===========================================================================

static DWORD WINAPI HeartbeatThreadProc(LPVOID lpParameter)
{
    PDRIVER_COMM_CONTEXT pCtx = (PDRIVER_COMM_CONTEXT)lpParameter;
    
    LogDebug("Heartbeat thread started, interval=%u ms", pCtx->HeartbeatIntervalMs);
    
    while (pCtx->Running) {
        if (!DriverSendHeartbeat(pCtx)) {
            LogWarn("Failed to send heartbeat");
        }
        
        Sleep(pCtx->HeartbeatIntervalMs);
    }
    
    LogDebug("Heartbeat thread stopped");
    return 0;
}

//===========================================================================
// 连接和断开
//===========================================================================

BOOL DriverConnect(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx) return FALSE;
    
    LogDebug("Connecting to driver: %ls", VNVME_DEVICE_PATH);
    
    pCtx->DeviceHandle = CreateFileW(
        VNVME_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    
    if (pCtx->DeviceHandle == INVALID_HANDLE_VALUE) {
        LogError("Failed to open driver device: error %u", GetLastError());
        return FALSE;
    }
    
    LogInfo("Connected to driver");
    return TRUE;
}

void DriverDisconnect(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx) return;
    
    // 停止心跳
    DriverStopHeartbeat(pCtx);
    
    // 取消映射共享内存
    DriverUnmapSharedMemory(pCtx);
    
    // 关闭设备句柄
    if (pCtx->DeviceHandle && pCtx->DeviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(pCtx->DeviceHandle);
        pCtx->DeviceHandle = NULL;
        LogInfo("Disconnected from driver");
    }
}

//===========================================================================
// 共享内存
//===========================================================================

BOOL DriverMapSharedMemory(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || pCtx->DeviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    LogDebug("Mapping shared memory...");
    
    VNVME_SHM_MAP_REQUEST request = {0};
    VNVME_SHM_MAP_RESPONSE response = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->DeviceHandle,
        IOCTL_VNVME_MAP_SHM,
        &request,
        sizeof(request),
        &response,
        sizeof(response),
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogError("IOCTL_VNVME_MAP_SHM failed: error %u", GetLastError());
        return FALSE;
    }
    
    if (!response.UserAddress || response.Size == 0) {
        LogError("Invalid shared memory response");
        return FALSE;
    }
    
    // 设置共享内存上下文
    pCtx->Shm.BaseAddress = response.UserAddress;
    pCtx->Shm.Size = response.Size;
    
    // 解析共享内存布局
    pCtx->Shm.ControlBlock = (PVNVME_SHM_CONTROL_BLOCK)pCtx->Shm.BaseAddress;
    
    // 验证签名
    if (pCtx->Shm.ControlBlock->Signature != VNVME_SHM_SIGNATURE) {
        LogError("Invalid shared memory signature: 0x%X", 
                 pCtx->Shm.ControlBlock->Signature);
        DriverUnmapSharedMemory(pCtx);
        return FALSE;
    }
    
    // 计算各区域指针
    PUCHAR base = (PUCHAR)pCtx->Shm.BaseAddress;
    
    pCtx->Shm.NotifyRing = (PVNVME_NOTIFY_RING)(base + pCtx->Shm.ControlBlock->NotifyRingOffset);
    pCtx->Shm.CompletionRing = (PVNVME_COMPLETION_NOTIFY_RING)(base + pCtx->Shm.ControlBlock->CompletionRingOffset);
    pCtx->Shm.DataBuffer = base + pCtx->Shm.ControlBlock->DataBufferOffset;
    pCtx->Shm.DataBufferSize = pCtx->Shm.ControlBlock->DataBufferSize;
    
    LogInfo("Shared memory mapped: base=%p, size=%zu", 
            pCtx->Shm.BaseAddress, pCtx->Shm.Size);
    LogDebug("  ControlBlock: %p", pCtx->Shm.ControlBlock);
    LogDebug("  NotifyRing: %p (entries=%u)", 
             pCtx->Shm.NotifyRing, pCtx->Shm.ControlBlock->NotifyRingSize);
    LogDebug("  CompletionRing: %p", pCtx->Shm.CompletionRing);
    LogDebug("  DataBuffer: %p (size=%zu)", 
             pCtx->Shm.DataBuffer, pCtx->Shm.DataBufferSize);
    
    return TRUE;
}

void DriverUnmapSharedMemory(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->Shm.BaseAddress) return;
    
    if (pCtx->DeviceHandle && pCtx->DeviceHandle != INVALID_HANDLE_VALUE) {
        VNVME_SHM_UNMAP_REQUEST request = {0};
        DWORD bytesReturned;
        
        DeviceIoControl(
            pCtx->DeviceHandle,
            IOCTL_VNVME_UNMAP_SHM,
            &request,
            sizeof(request),
            NULL,
            0,
            &bytesReturned,
            NULL
            );
    }
    
    pCtx->Shm.BaseAddress = NULL;
    pCtx->Shm.Size = 0;
    pCtx->Shm.ControlBlock = NULL;
    pCtx->Shm.NotifyRing = NULL;
    pCtx->Shm.CompletionRing = NULL;
    pCtx->Shm.DataBuffer = NULL;
    pCtx->Shm.DataBufferSize = 0;
    
    LogDebug("Shared memory unmapped");
}

//===========================================================================
// 用户态就绪和心跳
//===========================================================================

BOOL DriverSendUserReady(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || pCtx->DeviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->DeviceHandle,
        IOCTL_VNVME_USER_READY,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogError("IOCTL_VNVME_USER_READY failed: error %u", GetLastError());
        return FALSE;
    }
    
    LogInfo("User ready notification sent");
    return TRUE;
}

BOOL DriverSendHeartbeat(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || pCtx->DeviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->DeviceHandle,
        IOCTL_VNVME_HEARTBEAT,
        NULL,
        0,
        NULL,
        0,
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogVerbose("Heartbeat failed: error %u", GetLastError());
        return FALSE;
    }
    
    LogVerbose("Heartbeat sent");
    return TRUE;
}

BOOL DriverStartHeartbeat(PDRIVER_COMM_CONTEXT pCtx, UINT32 intervalMs)
{
    if (!pCtx) return FALSE;
    
    pCtx->HeartbeatIntervalMs = intervalMs > 0 ? intervalMs : 1000;
    pCtx->Running = TRUE;
    
    pCtx->HeartbeatThread = CreateThread(
        NULL,
        0,
        HeartbeatThreadProc,
        pCtx,
        0,
        NULL
        );
    
    if (!pCtx->HeartbeatThread) {
        LogError("Failed to create heartbeat thread: error %u", GetLastError());
        return FALSE;
    }
    
    LogInfo("Heartbeat thread started");
    return TRUE;
}

void DriverStopHeartbeat(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->HeartbeatThread) return;
    
    pCtx->Running = FALSE;
    
    WaitForSingleObject(pCtx->HeartbeatThread, 5000);
    CloseHandle(pCtx->HeartbeatThread);
    pCtx->HeartbeatThread = NULL;
    
    LogInfo("Heartbeat thread stopped");
}

//===========================================================================
// 关闭检测
//===========================================================================

BOOL DriverIsShutdownRequested(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->Shm.ControlBlock) {
        return FALSE;
    }
    
    return pCtx->Shm.ControlBlock->ShutdownRequested != 0;
}

void DriverNotifyShutdownComplete(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->Shm.ControlBlock) return;
    
    pCtx->Shm.ControlBlock->UserReady = 0;
    MemoryBarrier();
    
    LogInfo("Shutdown complete notification sent");
}

//===========================================================================
// 状态查询
//===========================================================================

BOOL DriverGetVersion(PDRIVER_COMM_CONTEXT pCtx, UINT32* pVersion)
{
    if (!pCtx || !pVersion || pCtx->DeviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    VNVME_VERSION_INFO versionInfo = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->DeviceHandle,
        IOCTL_VNVME_GET_VERSION,
        NULL,
        0,
        &versionInfo,
        sizeof(versionInfo),
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogError("IOCTL_VNVME_GET_VERSION failed: error %u", GetLastError());
        return FALSE;
    }
    
    *pVersion = versionInfo.Version;
    return TRUE;
}

BOOL DriverGetStatus(PDRIVER_COMM_CONTEXT pCtx, PVNVME_DRIVER_STATUS pStatus)
{
    if (!pCtx || !pStatus || pCtx->DeviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->DeviceHandle,
        IOCTL_VNVME_GET_STATUS,
        NULL,
        0,
        pStatus,
        sizeof(VNVME_DRIVER_STATUS),
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogError("IOCTL_VNVME_GET_STATUS failed: error %u", GetLastError());
        return FALSE;
    }
    
    return TRUE;
}
