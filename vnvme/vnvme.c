/**
 * @file vnvme.c
 * @brief VNVME 内核驱动入口和 FDO 管理
 * 
 * 实现驱动初始化、FDO 创建和生命周期管理。
 */

#include "vnvme.h"

//===========================================================================
// 全局变量
//===========================================================================

/**
 * @brief 全局 FDO 上下文指针
 * 
 * 线程安全说明:
 * - 在 VnvmeEvtDeviceAdd 中设置一次，不再修改直到驱动卸载
 * - 在驱动卸载时 (VnvmeEvtDriverContextCleanup) 设为 NULL
 * - WDF 保证在删除控制设备之前，所有 IOCTL 请求都已完成
 * - IOCTL 处理函数应保存本地副本后检查 NULL
 */
PVNVME_FDO_CONTEXT g_FdoContext = NULL;

//===========================================================================
// 驱动入口
//===========================================================================

/**
 * @brief 驱动入口点
 */
NTSTATUS
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    
    // 初始化调试子系统 (最先执行)
    VnvmeDebugInit(RegistryPath);
    
    VNVME_DBG_INFO("DriverEntry: VNVME driver loading, version 0x%08X", VNVME_VERSION);
    VNVME_FUNC_ENTER();
    
    // 启用 NX 池 - 安全性要求
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
    
    // 初始化 WPP 跟踪
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    
    // 配置驱动
    WDF_DRIVER_CONFIG_INIT(&config, VnvmeEvtDeviceAdd);
    
    // 配置驱动属性
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VnvmeEvtDriverContextCleanup;
    
    // 创建 WDF 驱动对象
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
        );
    
    if (!NT_SUCCESS(status)) {
        VNVME_DBG_ERROR("DriverEntry: WdfDriverCreate failed, status=0x%08X", status);
        WPP_CLEANUP(DriverObject);
        VNVME_FUNC_EXIT_NTSTATUS(status);
        return status;
    }
    
    VNVME_DBG_INFO("DriverEntry: Driver created successfully");
    VNVME_FUNC_EXIT_NTSTATUS(STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

//===========================================================================
// 设备添加回调
//===========================================================================

/**
 * @brief PnP 管理器发现设备时调用
 */
NTSTATUS
VnvmeEvtDeviceAdd(
    _In_ WDFDRIVER Driver,
    _Inout_ PWDFDEVICE_INIT DeviceInit
    )
{
    NTSTATUS status;
    WDFDEVICE device;
    WDF_OBJECT_ATTRIBUTES deviceAttributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    PVNVME_FDO_CONTEXT fdoContext;
    
    UNREFERENCED_PARAMETER(Driver);
    
    TRACE_INFO("VnvmeEvtDeviceAdd: Creating FDO");
    
    // 设置设备类型为总线扩展器
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
    
    // 允许多个句柄打开
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);
    
    // 配置 PnP/Power 回调
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = VnvmeEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VnvmeEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VnvmeEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VnvmeEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    
    // 配置设备上下文
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, VNVME_FDO_CONTEXT);
    
    // 创建 WDF 设备对象
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeEvtDeviceAdd: WdfDeviceCreate failed, status=0x%08X", status);
        return status;
    }
    
    // 获取并初始化 FDO 上下文
    fdoContext = VnvmeGetFdoContext(device);
    RtlZeroMemory(fdoContext, sizeof(VNVME_FDO_CONTEXT));
    
    // 设置标识
    fdoContext->IsFdo = TRUE;
    fdoContext->Signature = VNVME_FDO_SIGNATURE;
    
    fdoContext->Device = device;
    fdoContext->NextControllerId = 1;
    fdoContext->MaxControllers = VNVME_MAX_CONTROLLERS;
    
    // 命令处理模式 (默认用户态)
    fdoContext->CommandMode = VNVME_DEFAULT_CMD_MODE;
    
    // 初始化子设备链表
    InitializeListHead(&fdoContext->ChildDeviceList);
    KeInitializeSpinLock(&fdoContext->ChildDeviceListLock);
    
    // 初始化事件
    KeInitializeEvent(&fdoContext->CommandReadyEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&fdoContext->UserReadyEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&fdoContext->ShutdownEvent, NotificationEvent, FALSE);
    
    // 初始化关闭标志
    fdoContext->ShutdownRequested = FALSE;
    
    // 保存全局指针
    g_FdoContext = fdoContext;
    
    // 创建控制设备
    status = VnvmeCreateControlDevice(device);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeEvtDeviceAdd: VnvmeCreateControlDevice failed, status=0x%08X", status);
        return status;
    }
    
    TRACE_INFO("VnvmeEvtDeviceAdd: FDO created successfully");
    return STATUS_SUCCESS;
}

//===========================================================================
// PnP/Power 回调
//===========================================================================

/**
 * @brief 准备硬件 - 分配资源
 */
NTSTATUS
VnvmeEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    NTSTATUS status;
    PVNVME_FDO_CONTEXT fdoContext = VnvmeGetFdoContext(Device);
    
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    
    TRACE_INFO("VnvmeEvtDevicePrepareHardware: Allocating shared memory");
    
    // 分配共享内存
    status = VnvmeAllocateShm(fdoContext);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeEvtDevicePrepareHardware: VnvmeAllocateShm failed, status=0x%08X", status);
        return status;
    }
    
    TRACE_INFO("VnvmeEvtDevicePrepareHardware: Hardware prepared");
    return STATUS_SUCCESS;
}

/**
 * @brief 释放硬件 - 释放资源
 */
NTSTATUS
VnvmeEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PVNVME_FDO_CONTEXT fdoContext = VnvmeGetFdoContext(Device);
    
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    
    TRACE_INFO("VnvmeEvtDeviceReleaseHardware: Releasing resources");
    
    // 释放共享内存
    VnvmeFreeShm(fdoContext);
    
    TRACE_INFO("VnvmeEvtDeviceReleaseHardware: Resources released");
    return STATUS_SUCCESS;
}

/**
 * @brief 进入 D0 电源状态
 */
NTSTATUS
VnvmeEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    UNREFERENCED_PARAMETER(Device);
    
    TRACE_INFO("VnvmeEvtDeviceD0Entry: Entering D0 from %d", PreviousState);
    return STATUS_SUCCESS;
}

/**
 * @brief 退出 D0 电源状态 - 触发优雅关闭
 */
NTSTATUS
VnvmeEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PVNVME_FDO_CONTEXT fdoContext = VnvmeGetFdoContext(Device);
    LARGE_INTEGER timeout;
    
    TRACE_INFO("VnvmeEvtDeviceD0Exit: Exiting D0 to %d", TargetState);
    
    // 设置关闭标志
    fdoContext->ShutdownRequested = TRUE;
    
    // 触发关闭事件通知用户态
    KeSetEvent(&fdoContext->ShutdownEvent, IO_NO_INCREMENT, FALSE);
    
    // 更新共享内存中的关闭标志
    if (fdoContext->ShmKernelVirtAddr != NULL) {
        PVNVME_SHARED_MEMORY_CONTROL_BLOCK shm = 
            (PVNVME_SHARED_MEMORY_CONTROL_BLOCK)fdoContext->ShmKernelVirtAddr;
        shm->ShutdownRequested = TRUE;
    }
    
    // 等待用户态完成 (最多 5 秒)
    if (fdoContext->UserReady) {
        TRACE_INFO("VnvmeEvtDeviceD0Exit: Waiting for user-mode to shutdown");
        
        timeout.QuadPart = -50000000LL;  // 5 秒 (负数表示相对时间，100ns 单位)
        KeWaitForSingleObject(
            &fdoContext->UserReadyEvent,
            Executive,
            KernelMode,
            FALSE,
            &timeout
            );
        
        TRACE_INFO("VnvmeEvtDeviceD0Exit: User-mode shutdown complete or timed out");
    }
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 清理回调
//===========================================================================

/**
 * @brief 驱动卸载时清理
 */
VOID
VnvmeEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);
    
    TRACE_INFO("VnvmeEvtDriverContextCleanup: Driver unloading");
    
    // 删除控制设备
    if (g_FdoContext != NULL) {
        VnvmeDeleteControlDevice(g_FdoContext);
        g_FdoContext = NULL;
    }
    
    // 清理 WPP 跟踪
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
    
    TRACE_INFO("VnvmeEvtDriverContextCleanup: Cleanup complete");
}
