/**
 * @file vnvme.c
 * @brief VNVME 内核驱动入口和 FDO 管理
 * 
 * 实现驱动初始化、FDO 创建和生命周期管理。
 */

#include "vnvme.h"

/*===========================================================================
 * 全局变量
 *===========================================================================*/

PVNVME_FDO_CONTEXT g_FdoContext = NULL;

/*===========================================================================
 * 驱动入口
 *===========================================================================*/

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
    
    TRACE_INFO("DriverEntry: VNVME driver loading, version 0x%08X", VNVME_VERSION);
    
    /* 启用 NX 池 - 安全性要求 */
    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);
    
    /* 初始化 WPP 跟踪 */
    WPP_INIT_TRACING(DriverObject, RegistryPath);
    
    /* 配置驱动 */
    WDF_DRIVER_CONFIG_INIT(&config, VnvmeEvtDeviceAdd);
    
    /* 配置驱动属性 */
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.EvtCleanupCallback = VnvmeEvtDriverContextCleanup;
    
    /* 创建 WDF 驱动对象 */
    status = WdfDriverCreate(
        DriverObject,
        RegistryPath,
        &attributes,
        &config,
        WDF_NO_HANDLE
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("DriverEntry: WdfDriverCreate failed, status=0x%08X", status);
        WPP_CLEANUP(DriverObject);
        return status;
    }
    
    TRACE_INFO("DriverEntry: Driver created successfully");
    return STATUS_SUCCESS;
}

/*===========================================================================
 * 设备添加回调
 *===========================================================================*/

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
    
    /* 设置设备类型为总线扩展器 */
    WdfDeviceInitSetDeviceType(DeviceInit, FILE_DEVICE_BUS_EXTENDER);
    
    /* 允许多个句柄打开 */
    WdfDeviceInitSetExclusive(DeviceInit, FALSE);
    
    /* 配置 PnP/Power 回调 */
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = VnvmeEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VnvmeEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VnvmeEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VnvmeEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);
    
    /* 配置设备上下文 */
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&deviceAttributes, VNVME_FDO_CONTEXT);
    
    /* 创建 WDF 设备对象 */
    status = WdfDeviceCreate(&DeviceInit, &deviceAttributes, &device);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeEvtDeviceAdd: WdfDeviceCreate failed, status=0x%08X", status);
        return status;
    }
    
    /* 获取并初始化 FDO 上下文 */
    fdoContext = VnvmeGetFdoContext(device);
    RtlZeroMemory(fdoContext, sizeof(VNVME_FDO_CONTEXT));
    
    fdoContext->Device = device;
    fdoContext->NextControllerId = 1;
    
    /* 初始化子设备链表 */
    InitializeListHead(&fdoContext->ChildDeviceList);
    KeInitializeSpinLock(&fdoContext->ChildDeviceListLock);
    
    /* 初始化事件 */
    KeInitializeEvent(&fdoContext->CommandEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&fdoContext->UserReadyEvent, NotificationEvent, FALSE);
    
    /* 保存全局指针 */
    g_FdoContext = fdoContext;
    
    /* 创建控制设备 */
    status = VnvmeCreateControlDevice(device);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeEvtDeviceAdd: VnvmeCreateControlDevice failed, status=0x%08X", status);
        return status;
    }
    
    TRACE_INFO("VnvmeEvtDeviceAdd: FDO created successfully");
    return STATUS_SUCCESS;
}

/*===========================================================================
 * PnP/Power 回调
 *===========================================================================*/

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
    
    /* 分配共享内存 */
    status = VnvmeAllocateSharedMemory(fdoContext);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeEvtDevicePrepareHardware: VnvmeAllocateSharedMemory failed, status=0x%08X", status);
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
    
    /* 释放共享内存 */
    VnvmeFreeSharedMemory(fdoContext);
    
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
 * @brief 退出 D0 电源状态
 */
NTSTATUS
VnvmeEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    UNREFERENCED_PARAMETER(Device);
    
    TRACE_INFO("VnvmeEvtDeviceD0Exit: Exiting D0 to %d", TargetState);
    return STATUS_SUCCESS;
}

/*===========================================================================
 * 清理回调
 *===========================================================================*/

/**
 * @brief 驱动卸载时清理
 */
VOID
VnvmeEvtDriverContextCleanup(
    _In_ WDFOBJECT DriverObject
    )
{
    TRACE_INFO("VnvmeEvtDriverContextCleanup: Driver unloading");
    
    /* 删除控制设备 */
    if (g_FdoContext != NULL) {
        VnvmeDeleteControlDevice(g_FdoContext);
        g_FdoContext = NULL;
    }
    
    /* 清理 WPP 跟踪 */
    WPP_CLEANUP(WdfDriverWdmGetDriverObject((WDFDRIVER)DriverObject));
    
    TRACE_INFO("VnvmeEvtDriverContextCleanup: Cleanup complete");
}
