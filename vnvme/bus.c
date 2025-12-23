/**
 * @file bus.c
 * @brief 虚拟总线管理 - PDO 创建
 * 
 * 管理虚拟 NVMe 控制器的 PDO 创建和删除。
 */

#include "vnvme.h"
#include <ntstrsafe.h>

//===========================================================================
// 内部函数
//===========================================================================

/**
 * @brief 查找指定 ID 的控制器
 */
static PVNVME_PDO_CONTEXT
VnvmeFindController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId
    )
{
    PLIST_ENTRY entry;
    PVNVME_PDO_CONTEXT pdoContext;
    KIRQL oldIrql;
    
    KeAcquireSpinLock(&FdoContext->ChildDeviceListLock, &oldIrql);
    
    for (entry = FdoContext->ChildDeviceList.Flink;
         entry != &FdoContext->ChildDeviceList;
         entry = entry->Flink) {
        
        pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
        if (pdoContext->ControllerId == ControllerId) {
            KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
            return pdoContext;
        }
    }
    
    KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
    return NULL;
}

//===========================================================================
// 低层实现 - 实际 PDO 创建
//===========================================================================

/**
 * @brief 创建 PDO 设备对象 (低层实现)
 * 
 * 此函数负责实际的 WDF PDO 创建，包括设备 ID、PnP 回调配置等。
 * 由高层 API VnvmeCreateVirtualController() 调用。
 */
NTSTATUS
VnvmeCreateControllerPdo(
    _In_ WDFDEVICE ParentDevice,
    _In_ ULONG ControllerId,
    _Out_ WDFDEVICE* PdoDevice
    )
{
    NTSTATUS status;
    WDFDEVICE childDevice = NULL;
    PWDFDEVICE_INIT deviceInit = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    PVNVME_PDO_CONTEXT pdoContext;
    DECLARE_UNICODE_STRING_SIZE(deviceId, 128);
    DECLARE_UNICODE_STRING_SIZE(hardwareIds, 256);
    DECLARE_UNICODE_STRING_SIZE(compatibleIds, 128);
    DECLARE_UNICODE_STRING_SIZE(instanceId, 32);
    DECLARE_UNICODE_STRING_SIZE(deviceText, 64);
    
    // 1. 分配 PDO 初始化结构
    deviceInit = WdfPdoInitAllocate(ParentDevice);
    if (deviceInit == NULL) {
        TRACE_ERROR("WdfPdoInitAllocate failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 2. 设置设备 ID
    // Device ID: PCI\VEN_1B36&DEV_0010
    status = RtlUnicodeStringPrintf(&deviceId, L"PCI\\VEN_1B36&DEV_0010");
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    status = WdfPdoInitAssignDeviceID(deviceInit, &deviceId);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("WdfPdoInitAssignDeviceID failed: 0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    // 3. 设置硬件 ID
    // Hardware ID: PCI\VEN_1B36&DEV_0010&REV_01
    status = RtlUnicodeStringPrintf(&hardwareIds, L"PCI\\VEN_1B36&DEV_0010&REV_01");
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    status = WdfPdoInitAddHardwareID(deviceInit, &hardwareIds);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("WdfPdoInitAddHardwareID failed: 0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    // 4. 设置兼容 ID (NVMe 类代码)
    status = RtlUnicodeStringPrintf(&compatibleIds, L"PCI\\CC_010802");
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    status = WdfPdoInitAddCompatibleID(deviceInit, &compatibleIds);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("WdfPdoInitAddCompatibleID failed: 0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    // 5. 设置实例 ID
    status = RtlUnicodeStringPrintf(&instanceId, L"CTRL%04lu", ControllerId);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    status = WdfPdoInitAssignInstanceID(deviceInit, &instanceId);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("WdfPdoInitAssignInstanceID failed: 0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    // 6. 设置设备描述
    status = RtlUnicodeStringPrintf(&deviceText, L"Virtual NVMe Controller %lu", ControllerId);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    status = WdfPdoInitAddDeviceText(deviceInit, &deviceText, &deviceText, 0x0409);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("WdfPdoInitAddDeviceText failed: 0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    WdfPdoInitSetDefaultLocale(deviceInit, 0x0409);
    
    // 7. 配置 PnP/Power 回调
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDevicePrepareHardware = VnvmePdoEvtDevicePrepareHardware;
    pnpPowerCallbacks.EvtDeviceReleaseHardware = VnvmePdoEvtDeviceReleaseHardware;
    pnpPowerCallbacks.EvtDeviceD0Entry = VnvmePdoEvtDeviceD0Entry;
    pnpPowerCallbacks.EvtDeviceD0Exit = VnvmePdoEvtDeviceD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit, &pnpPowerCallbacks);
    
    // 8. 创建 PDO 设备对象
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, VNVME_PDO_CONTEXT);
    
    status = WdfDeviceCreate(&deviceInit, &attributes, &childDevice);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("WdfDeviceCreate failed: 0x%08X", status);
        // deviceInit 在失败时已被 WdfDeviceCreate 释放
        return status;
    }
    
    // 9. 初始化 PDO 上下文
    pdoContext = VnvmeGetPdoContext(childDevice);
    RtlZeroMemory(pdoContext, sizeof(VNVME_PDO_CONTEXT));
    pdoContext->IsFdo = FALSE;
    pdoContext->Signature = 'PDOV';
    pdoContext->Device = childDevice;
    pdoContext->ParentFdo = ParentDevice;
    pdoContext->ControllerId = ControllerId;
    pdoContext->MaxIoQueues = VNVME_MAX_IO_QUEUES;
    pdoContext->PollingIntervalUs = VNVME_POLLING_INTERVAL_MS * 1000;  // 转换为微秒
    
    TRACE_INFO("VnvmeCreateControllerPdo: PDO created, ControllerId=%lu", ControllerId);
    
    *PdoDevice = childDevice;
    return STATUS_SUCCESS;
}

/**
 * @brief 删除 PDO 设备对象 (低层实现)
 */
NTSTATUS
VnvmeDeleteControllerPdo(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    TRACE_INFO("VnvmeDeleteControllerPdo: Deleting PDO, ControllerId=%lu", PdoContext->ControllerId);
    
    // 删除 PDO 设备对象
    // 注意：WdfObjectDelete 会触发 EvtDeviceReleaseHardware 回调
    WdfObjectDelete(PdoContext->Device);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 高层 API - IOCTL 入口
//===========================================================================

/**
 * @brief 创建一个虚拟 NVMe 控制器 (高层 API)
 * 
 * 此函数由用户态服务通过 IOCTL 调用，请求创建新的虚拟 NVMe 控制器。
 * 负责验证参数、检查重复、管理链表，然后调用低层函数创建 PDO。
 */
NTSTATUS
VnvmeCreateVirtualController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId,
    _Out_opt_ WDFDEVICE* ChildDevice
    )
{
    NTSTATUS status;
    WDFDEVICE childDevice = NULL;
    PVNVME_PDO_CONTEXT pdoContext;
    KIRQL oldIrql;
    
    TRACE_INFO("VnvmeCreateVirtualController: Creating controller ID=%lu", ControllerId);
    
    // 1. 验证：检查是否已存在相同 ID 的控制器
    if (VnvmeFindController(FdoContext, ControllerId) != NULL) {
        TRACE_WARN("Controller ID=%lu already exists", ControllerId);
        return STATUS_OBJECT_NAME_COLLISION;
    }
    
    // 2. 验证：检查是否达到最大控制器数
    if (FdoContext->ChildDeviceCount >= FdoContext->MaxControllers) {
        TRACE_WARN("Maximum controller count reached (%lu)", FdoContext->MaxControllers);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 3. 调用低层函数创建 PDO
    status = VnvmeCreateControllerPdo(FdoContext->Device, ControllerId, &childDevice);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControllerPdo failed: 0x%08X", status);
        return status;
    }
    
    // 4. 添加到 FDO 的子设备链表
    pdoContext = VnvmeGetPdoContext(childDevice);
    
    KeAcquireSpinLock(&FdoContext->ChildDeviceListLock, &oldIrql);
    InsertTailList(&FdoContext->ChildDeviceList, &pdoContext->ListEntry);
    FdoContext->ChildDeviceCount++;
    KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
    
    TRACE_INFO("VnvmeCreateVirtualController: Controller ID=%lu created successfully", ControllerId);
    
    if (ChildDevice != NULL) {
        *ChildDevice = childDevice;
    }
    
    return STATUS_SUCCESS;
}

/**
 * @brief 删除虚拟 NVMe 控制器 (高层 API)
 */
NTSTATUS
VnvmeDeleteVirtualController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId
    )
{
    PVNVME_PDO_CONTEXT pdoContext;
    KIRQL oldIrql;
    
    TRACE_INFO("VnvmeDeleteVirtualController: Deleting controller ID=%lu", ControllerId);
    
    // 1. 查找控制器
    pdoContext = VnvmeFindController(FdoContext, ControllerId);
    if (pdoContext == NULL) {
        TRACE_WARN("Controller ID=%lu not found", ControllerId);
        return STATUS_NOT_FOUND;
    }
    
    // 2. 从链表中移除
    KeAcquireSpinLock(&FdoContext->ChildDeviceListLock, &oldIrql);
    RemoveEntryList(&pdoContext->ListEntry);
    FdoContext->ChildDeviceCount--;
    KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
    
    // 3. 调用低层函数删除 PDO
    VnvmeDeleteControllerPdo(pdoContext);
    
    TRACE_INFO("VnvmeDeleteVirtualController: Controller ID=%lu deleted", ControllerId);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 枚举所有子设备
 */
NTSTATUS
VnvmeEnumerateChildren(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    TRACE_INFO("VnvmeEnumerateChildren: Enumerating %lu children", FdoContext->ChildDeviceCount);
    
    // 对于静态 PDO 管理，不需要额外操作
    // WDF 会自动处理子设备的枚举
    
    return STATUS_SUCCESS;
}
