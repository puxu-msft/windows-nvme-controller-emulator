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

#define VNVME_DEVICE_PATH   VNVME_CONTROL_USER_PATH  // L"\\\\.\\VNVMEControl"

//===========================================================================
// 心跳线程
//===========================================================================

static DWORD WINAPI HeartbeatThreadProc(LPVOID lpParameter)
{
    PDRIVER_COMM_CONTEXT pCtx = (PDRIVER_COMM_CONTEXT)lpParameter;
    
    LogDebug("Heartbeat thread started, interval=%u ms", pCtx->heartbeatIntervalMs);
    
    while (pCtx->running) {
        if (!DriverSendHeartbeat(pCtx)) {
            LogWarn("Failed to send heartbeat");
        }
        
        Sleep(pCtx->heartbeatIntervalMs);
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
    
    pCtx->deviceHandle = CreateFileW(
        VNVME_DEVICE_PATH,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
        );
    
    if (pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
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
    DriverUnmapShm(pCtx);
    
    // 关闭设备句柄
    if (pCtx->deviceHandle && pCtx->deviceHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(pCtx->deviceHandle);
        pCtx->deviceHandle = NULL;
        LogInfo("Disconnected from driver");
    }
}

//===========================================================================
// 共享内存
//===========================================================================

BOOL DriverMapShm(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    LogDebug("Mapping SHM...");
    
    VNVME_MAP_SHM_INPUT request = {0};
    VNVME_MAP_SHM_OUTPUT response = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->deviceHandle,
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
    
    if (!response.UserAddress || response.ActualSize == 0) {
        LogError("Invalid SHM response");
        return FALSE;
    }
    
    // 设置共享内存上下文
    pCtx->shm.userAddress = response.UserAddress;
    pCtx->shm.size = response.ActualSize;
    
    // 解析共享内存布局
    pCtx->shm.controlBlock = (PVNVME_SHM_CONTROL_BLOCK)pCtx->shm.userAddress;
    
    // 验证魔数
    if (pCtx->shm.controlBlock->Magic != VNVME_SHM_MAGIC) {
        LogError("Invalid SHM magic: 0x%X", 
                 pCtx->shm.controlBlock->Magic);
        DriverUnmapShm(pCtx);
        return FALSE;
    }
    
    // 计算各区域指针
    PUCHAR base = (PUCHAR)pCtx->shm.userAddress;
    
    pCtx->shm.notifyRing = (PVNVME_NOTIFY_RING)(base + pCtx->shm.controlBlock->NotifyRingOffset);
    pCtx->shm.dataBuffer = base + pCtx->shm.controlBlock->DataBufferOffset;
    pCtx->shm.dataBufferSize = pCtx->shm.controlBlock->DataBufferSize;
    
    LogInfo("SHM mapped: base=%p, size=%zu", 
            pCtx->shm.userAddress, pCtx->shm.size);
    LogDebug("  ControlBlock: %p", pCtx->shm.controlBlock);
    LogDebug("  NotifyRing: %p (size=%u)", 
             pCtx->shm.notifyRing, pCtx->shm.controlBlock->NotifyRingSize);
    LogDebug("  DataBuffer: %p (size=%zu)", 
             pCtx->shm.dataBuffer, pCtx->shm.dataBufferSize);
    
    return TRUE;
}

void DriverUnmapShm(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->shm.userAddress) return;
    
    // 在测试模式下显式调用 IOCTL 取消映射，确保状态一致性
    // 生产环境中共享内存在进程结束时自动取消映射
    if (pCtx->deviceHandle != INVALID_HANDLE_VALUE) {
        DWORD bytesReturned;
        DeviceIoControl(
            pCtx->deviceHandle,
            IOCTL_VNVME_UNMAP_SHM,
            NULL, 0,
            NULL, 0,
            &bytesReturned,
            NULL
            );
    }
    
    // 清理本地状态
    pCtx->shm.userAddress = NULL;
    pCtx->shm.size = 0;
    pCtx->shm.controlBlock = NULL;
    pCtx->shm.notifyRing = NULL;
    pCtx->shm.dataBuffer = NULL;
    pCtx->shm.dataBufferSize = 0;
    
    LogDebug("SHM unmapped");
}

//===========================================================================
// 用户态就绪和心跳
//===========================================================================

BOOL DriverSendUserReady(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->deviceHandle,
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
    if (!pCtx || pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->deviceHandle,
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
    
    pCtx->heartbeatIntervalMs = intervalMs > 0 ? intervalMs : 1000;
    pCtx->running = TRUE;
    
    pCtx->heartbeatThread = CreateThread(
        NULL,
        0,
        HeartbeatThreadProc,
        pCtx,
        0,
        NULL
        );
    
    if (!pCtx->heartbeatThread) {
        LogError("Failed to create heartbeat thread: error %u", GetLastError());
        return FALSE;
    }
    
    LogInfo("Heartbeat thread started");
    return TRUE;
}

void DriverStopHeartbeat(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->heartbeatThread) return;
    
    pCtx->running = FALSE;
    
    WaitForSingleObject(pCtx->heartbeatThread, 5000);
    CloseHandle(pCtx->heartbeatThread);
    pCtx->heartbeatThread = NULL;
    
    LogInfo("Heartbeat thread stopped");
}

//===========================================================================
// 关闭检测
//===========================================================================

BOOL DriverIsShutdownRequested(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->shm.controlBlock) {
        return FALSE;
    }
    
    return pCtx->shm.controlBlock->ShutdownRequested != 0;
}

void DriverNotifyShutdownComplete(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || !pCtx->shm.controlBlock) return;
    
    pCtx->shm.controlBlock->UserReady = 0;
    MemoryBarrier();
    
    LogInfo("Shutdown complete notification sent");
}

//===========================================================================
// 状态查询
//===========================================================================

BOOL DriverGetVersion(PDRIVER_COMM_CONTEXT pCtx, UINT32* pVersion)
{
    if (!pCtx || !pVersion || pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    VNVME_GET_VERSION_OUTPUT versionInfo = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->deviceHandle,
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
    
    *pVersion = versionInfo.DriverVersion;
    return TRUE;
}

BOOL DriverGetStatus(PDRIVER_COMM_CONTEXT pCtx, PVNVME_GET_STATUS_OUTPUT pStatus)
{
    if (!pCtx || !pStatus || pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->deviceHandle,
        IOCTL_VNVME_GET_STATUS,
        NULL,
        0,
        pStatus,
        sizeof(VNVME_GET_STATUS_OUTPUT),
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogError("IOCTL_VNVME_GET_STATUS failed: error %u", GetLastError());
        return FALSE;
    }
    
    return TRUE;
}

//===========================================================================
// 事件等待机制
//===========================================================================

BOOL DriverGetCommandEvent(PDRIVER_COMM_CONTEXT pCtx)
{
    if (!pCtx || pCtx->deviceHandle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }
    
    // 已经有事件句柄
    if (pCtx->commandEvent != NULL) {
        return TRUE;
    }
    
    VNVME_GET_COMMAND_EVENT_OUTPUT output = {0};
    DWORD bytesReturned;
    
    BOOL result = DeviceIoControl(
        pCtx->deviceHandle,
        IOCTL_VNVME_GET_COMMAND_EVENT,
        NULL,
        0,
        &output,
        sizeof(output),
        &bytesReturned,
        NULL
        );
    
    if (!result) {
        LogWarn("IOCTL_VNVME_GET_COMMAND_EVENT failed: error %u (falling back to polling)", 
                GetLastError());
        pCtx->eventModeEnabled = FALSE;
        return FALSE;
    }
    
    if (output.EventHandle == NULL) {
        LogWarn("Driver returned NULL event handle (falling back to polling)");
        pCtx->eventModeEnabled = FALSE;
        return FALSE;
    }
    
    pCtx->commandEvent = output.EventHandle;
    pCtx->eventModeEnabled = TRUE;
    LogInfo("Event mode enabled, handle=0x%p", pCtx->commandEvent);
    
    return TRUE;
}

BOOL DriverWaitForCommand(PDRIVER_COMM_CONTEXT pCtx, DWORD timeoutMs)
{
    if (!pCtx) {
        return FALSE;
    }
    
    // 未启用事件模式，回退到短暂 Sleep
    if (!pCtx->eventModeEnabled || pCtx->commandEvent == NULL) {
        Sleep(1);
        return TRUE;
    }
    
    DWORD waitResult = WaitForSingleObject(pCtx->commandEvent, timeoutMs);
    
    switch (waitResult) {
        case WAIT_OBJECT_0:
            // 事件已触发，有命令就绪
            return TRUE;
            
        case WAIT_TIMEOUT:
            // 超时，没有新命令
            return FALSE;
            
        case WAIT_FAILED:
            LogWarn("WaitForSingleObject failed: error %u", GetLastError());
            // 禁用事件模式，回退到轮询
            pCtx->eventModeEnabled = FALSE;
            return FALSE;
            
        default:
            return FALSE;
    }
}
