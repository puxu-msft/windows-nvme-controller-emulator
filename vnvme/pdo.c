/**
 * @file pdo.c
 * @brief PDO 设备对象处理
 * 
 * 处理虚拟 NVMe 控制器 PDO 的 PnP、Power 和 IO 操作。
 */

#include "vnvme.h"
#include <ntstrsafe.h>

//===========================================================================
// 常量定义
//===========================================================================

// PCIe 设备标识 (模拟 Red Hat VirtIO NVMe)
#define VNVME_PCI_VENDOR_ID         0x1B36  // Red Hat, Inc.
#define VNVME_PCI_DEVICE_ID         0x0010  // NVMe 控制器

// 设备 ID 字符串
#define VNVME_DEVICE_ID             L"PCI\\VEN_1B36&DEV_0010"
#define VNVME_HARDWARE_ID           L"PCI\\VEN_1B36&DEV_0010&REV_01"
#define VNVME_COMPATIBLE_ID         L"PCI\\CC_010802"  // NVMe 类代码
#define VNVME_DEVICE_DESCRIPTION    L"Virtual NVMe Controller"
#define VNVME_DEVICE_LOCATION       L"Virtual Bus"

//===========================================================================
// PDO PnP 回调
//===========================================================================

/**
 * @brief PDO 设备准备硬件
 */
NTSTATUS
VnvmePdoEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PVNVME_PDO_CONTEXT pdoContext;
    NTSTATUS status;
    
    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    
    pdoContext = VnvmeGetPdoContext(Device);
    
    TRACE_INFO("VnvmePdoEvtDevicePrepareHardware: ControllerId=%lu", pdoContext->ControllerId);
    
    // 分配 BAR0 内存
    status = VnvmeAllocateBar0(pdoContext);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeAllocateBar0 failed: 0x%08X", status);
        return status;
    }
    
    // 初始化 BAR0 寄存器
    VnvmeInitializeBar0Registers(pdoContext);
    
    // 分配 PCIe 配置空间
    status = VnvmeAllocatePcieConfig(pdoContext);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeAllocatePcieConfig failed: 0x%08X", status);
        VnvmeFreeBar0(pdoContext);
        return status;
    }
    
    // 初始化 PCIe 配置空间
    VnvmeInitializePcieConfig(pdoContext);
    
    // 初始化轮询定时器
    status = VnvmeInitializePollingTimer(pdoContext);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeInitializePollingTimer failed: 0x%08X", status);
        VnvmeFreePcieConfig(pdoContext);
        VnvmeFreeBar0(pdoContext);
        return status;
    }
    
    TRACE_INFO("PDO PrepareHardware complete: BAR0=%p, PhysAddr=0x%llX",
        pdoContext->Bar0VirtAddr, pdoContext->Bar0PhysAddr.QuadPart);
    
    return STATUS_SUCCESS;
}

/**
 * @brief PDO 设备释放硬件
 */
NTSTATUS
VnvmePdoEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated
    )
{
    PVNVME_PDO_CONTEXT pdoContext;
    
    UNREFERENCED_PARAMETER(ResourcesTranslated);
    
    pdoContext = VnvmeGetPdoContext(Device);
    
    TRACE_INFO("VnvmePdoEvtDeviceReleaseHardware: ControllerId=%lu", pdoContext->ControllerId);
    
    // 停止轮询定时器
    VnvmeStopPollingTimer(pdoContext);
    
    // 释放 PCIe 配置空间
    VnvmeFreePcieConfig(pdoContext);
    
    // 释放 BAR0 内存
    VnvmeFreeBar0(pdoContext);
    
    TRACE_INFO("PDO ReleaseHardware complete");
    
    return STATUS_SUCCESS;
}

//===========================================================================
// PDO Power 回调
//===========================================================================

/**
 * @brief PDO 进入 D0 状态
 */
NTSTATUS
VnvmePdoEvtDeviceD0Entry(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE PreviousState
    )
{
    PVNVME_PDO_CONTEXT pdoContext;
    
    pdoContext = VnvmeGetPdoContext(Device);
    
    TRACE_INFO("VnvmePdoEvtDeviceD0Entry: ControllerId=%lu, Previous state=%d", 
        pdoContext->ControllerId, PreviousState);
    
    // 启动轮询定时器
    VnvmeStartPollingTimer(pdoContext);
    
    return STATUS_SUCCESS;
}

/**
 * @brief PDO 离开 D0 状态
 */
NTSTATUS
VnvmePdoEvtDeviceD0Exit(
    _In_ WDFDEVICE Device,
    _In_ WDF_POWER_DEVICE_STATE TargetState
    )
{
    PVNVME_PDO_CONTEXT pdoContext;
    
    pdoContext = VnvmeGetPdoContext(Device);
    
    TRACE_INFO("VnvmePdoEvtDeviceD0Exit: ControllerId=%lu, Target state=%d",
        pdoContext->ControllerId, TargetState);
    
    // 停止轮询定时器
    VnvmeStopPollingTimer(pdoContext);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// PDO 查询接口
//===========================================================================

/**
 * @brief 查询 PDO 设备 ID
 * 
 * 返回格式:
 * - BusQueryDeviceID: "PCI\\VEN_1B36&DEV_0010"
 * - BusQueryHardwareIDs: "PCI\\VEN_1B36&DEV_0010&REV_01\0PCI\\CC_010802\0\0" (多字符串)
 * - BusQueryCompatibleIDs: "PCI\\CC_010802\0\0" (多字符串)
 * - BusQueryInstanceID: "0" (控制器 ID)
 */
NTSTATUS
VnvmePdoQueryDeviceId(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ BUS_QUERY_ID_TYPE IdType,
    _Out_ PWSTR* DeviceId
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    WCHAR instanceId[16];
    
    *DeviceId = NULL;
    
    TRACE_INFO("VnvmePdoQueryDeviceId: ControllerId=%lu, IdType=%d", 
        PdoContext->ControllerId, IdType);
    
    switch (IdType) {
    case BusQueryDeviceID:
        // 设备 ID: "PCI\\VEN_1B36&DEV_0010"
        *DeviceId = VnvmeAllocateString(VNVME_DEVICE_ID);
        break;
        
    case BusQueryHardwareIDs:
        // 硬件 ID: 多字符串，包含完整 ID 和类代码
        *DeviceId = VnvmeAllocateMultiString(VNVME_HARDWARE_ID, VNVME_COMPATIBLE_ID);
        break;
        
    case BusQueryCompatibleIDs:
        // 兼容 ID: NVMe 类代码
        *DeviceId = VnvmeAllocateMultiString(VNVME_COMPATIBLE_ID, NULL);
        break;
        
    case BusQueryInstanceID:
        // 实例 ID: 控制器索引
        status = RtlStringCbPrintfW(instanceId, sizeof(instanceId), L"%lu", PdoContext->ControllerId);
        if (NT_SUCCESS(status)) {
            *DeviceId = VnvmeAllocateString(instanceId);
        }
        break;
        
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }
    
    if (*DeviceId == NULL && NT_SUCCESS(status)) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    
    TRACE_INFO("VnvmePdoQueryDeviceId: Result=%S, Status=0x%08X", 
        *DeviceId ? *DeviceId : L"(null)", status);
    
    return status;
}

/**
 * @brief 查询 PDO 设备文本
 */
NTSTATUS
VnvmePdoQueryDeviceText(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ DEVICE_TEXT_TYPE TextType,
    _In_ LCID LocaleId,
    _Out_ PWSTR* DeviceText
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    WCHAR description[128];
    
    UNREFERENCED_PARAMETER(LocaleId);
    
    *DeviceText = NULL;
    
    TRACE_INFO("VnvmePdoQueryDeviceText: ControllerId=%lu, TextType=%d", 
        PdoContext->ControllerId, TextType);
    
    switch (TextType) {
    case DeviceTextDescription:
        // 设备描述: "Virtual NVMe Controller #N"
        status = RtlStringCbPrintfW(description, sizeof(description),
            L"%s #%lu", VNVME_DEVICE_DESCRIPTION, PdoContext->ControllerId);
        if (NT_SUCCESS(status)) {
            *DeviceText = VnvmeAllocateString(description);
        }
        break;
        
    case DeviceTextLocationInformation:
        // 位置信息
        *DeviceText = VnvmeAllocateString(VNVME_DEVICE_LOCATION);
        break;
        
    default:
        status = STATUS_NOT_SUPPORTED;
        break;
    }
    
    if (*DeviceText == NULL && NT_SUCCESS(status)) {
        status = STATUS_INSUFFICIENT_RESOURCES;
    }
    
    return status;
}

//===========================================================================
// PDO 设备能力查询
//===========================================================================

/**
 * @brief 查询 PDO 设备能力
 * 
 * 处理 IRP_MN_QUERY_CAPABILITIES，返回设备的 PnP 能力信息。
 */
NTSTATUS
VnvmePdoQueryCapabilities(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Inout_ PDEVICE_CAPABILITIES Capabilities
    )
{
    UNREFERENCED_PARAMETER(PdoContext);
    
    TRACE_INFO("VnvmePdoQueryCapabilities: ControllerId=%lu", PdoContext->ControllerId);
    
    // 版本检查
    if (Capabilities->Version < 1 || 
        Capabilities->Size < sizeof(DEVICE_CAPABILITIES)) {
        return STATUS_UNSUCCESSFUL;
    }
    
    // 设备能力
    Capabilities->DeviceD1 = FALSE;
    Capabilities->DeviceD2 = FALSE;
    Capabilities->LockSupported = FALSE;
    Capabilities->EjectSupported = TRUE;       // 支持弹出
    Capabilities->Removable = TRUE;            // 可移除
    Capabilities->DockDevice = FALSE;
    Capabilities->UniqueID = TRUE;             // 实例 ID 唯一
    Capabilities->SilentInstall = TRUE;        // 静默安装
    Capabilities->RawDeviceOK = FALSE;
    Capabilities->SurpriseRemovalOK = TRUE;    // 支持意外移除
    Capabilities->WakeFromD0 = FALSE;
    Capabilities->WakeFromD1 = FALSE;
    Capabilities->WakeFromD2 = FALSE;
    Capabilities->WakeFromD3 = FALSE;
    Capabilities->HardwareDisabled = FALSE;
    Capabilities->NoDisplayInUI = FALSE;
    
    // 电源状态映射 (系统状态 → 设备状态)
    Capabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    Capabilities->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
    Capabilities->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    Capabilities->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    Capabilities->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    Capabilities->DeviceState[PowerSystemShutdown] = PowerDeviceD3;
    
    // 系统唤醒和设备唤醒能力
    Capabilities->SystemWake = PowerSystemUnspecified;
    Capabilities->DeviceWake = PowerDeviceUnspecified;
    
    // D1/D2 延迟 (不支持这些状态)
    Capabilities->D1Latency = 0;
    Capabilities->D2Latency = 0;
    Capabilities->D3Latency = 0;
    
    // 地址 (PCIe 设备地址)
    Capabilities->Address = PdoContext->ControllerId;
    Capabilities->UINumber = PdoContext->ControllerId;
    
    return STATUS_SUCCESS;
}

//===========================================================================
// PDO 总线信息查询
//===========================================================================

/**
 * @brief 查询 PDO 总线信息
 * 
 * 处理 IRP_MN_QUERY_BUS_INFORMATION，返回 PCIe 总线类型。
 */
NTSTATUS
VnvmePdoQueryBusInformation(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Out_ PPNP_BUS_INFORMATION* BusInformation
    )
{
    PPNP_BUS_INFORMATION busInfo;
    
    TRACE_INFO("VnvmePdoQueryBusInformation: ControllerId=%lu", PdoContext->ControllerId);
    
    // 分配总线信息结构
    busInfo = (PPNP_BUS_INFORMATION)ExAllocatePool2(
        POOL_FLAG_PAGED,
        sizeof(PNP_BUS_INFORMATION),
        VNVME_POOL_TAG
        );
    
    if (busInfo == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 填充 PCIe 总线信息
    busInfo->BusTypeGuid = GUID_BUS_TYPE_PCI;  // PCI/PCIe 总线类型
    busInfo->LegacyBusType = PCIBus;
    busInfo->BusNumber = 0;                     // 虚拟总线号
    
    *BusInformation = busInfo;
    
    return STATUS_SUCCESS;
}

//===========================================================================
// PDO 资源查询
//===========================================================================

/**
 * @brief 查询 PDO 资源需求
 * 
 * 处理 IRP_MN_QUERY_RESOURCE_REQUIREMENTS，报告 BAR0 内存资源需求。
 * 这告诉 PnP 管理器我们需要什么资源。
 */
NTSTATUS
VnvmePdoQueryResourceRequirements(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Out_ PIO_RESOURCE_REQUIREMENTS_LIST* ResourceRequirements
    )
{
    PIO_RESOURCE_REQUIREMENTS_LIST reqList;
    PIO_RESOURCE_DESCRIPTOR resDesc;
    ULONG listSize;
    
    TRACE_INFO("VnvmePdoQueryResourceRequirements: ControllerId=%lu", 
        PdoContext->ControllerId);
    
    // 计算所需大小: 1 个内存资源 (BAR0)
    listSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST);
    
    reqList = (PIO_RESOURCE_REQUIREMENTS_LIST)ExAllocatePool2(
        POOL_FLAG_PAGED,
        listSize,
        VNVME_POOL_TAG
        );
    
    if (reqList == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(reqList, listSize);
    
    // 填充资源需求列表头
    reqList->ListSize = listSize;
    reqList->InterfaceType = PCIBus;
    reqList->BusNumber = 0;
    reqList->SlotNumber = PdoContext->ControllerId;
    reqList->AlternativeLists = 1;
    
    // 资源列表
    reqList->List[0].Version = 1;
    reqList->List[0].Revision = 1;
    reqList->List[0].Count = 1;
    
    // BAR0: 64KB 内存资源
    resDesc = &reqList->List[0].Descriptors[0];
    resDesc->Type = CmResourceTypeMemory;
    resDesc->ShareDisposition = CmResourceShareDeviceExclusive;
    resDesc->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_PREFETCHABLE;
    resDesc->Option = 0;
    resDesc->u.Memory.MinimumAddress.QuadPart = 0;
    resDesc->u.Memory.MaximumAddress.QuadPart = (ULONGLONG)-1;
    resDesc->u.Memory.Length = VNVME_BAR0_SIZE;
    resDesc->u.Memory.Alignment = VNVME_BAR0_SIZE;  // 64KB 对齐
    
    *ResourceRequirements = reqList;
    
    TRACE_INFO("VnvmePdoQueryResourceRequirements: BAR0 size=%u", VNVME_BAR0_SIZE);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 查询 PDO 已分配资源
 * 
 * 处理 IRP_MN_QUERY_RESOURCES，报告我们已分配的 BAR0 物理地址。
 * 由于我们自己管理内存，可以返回我们已分配的地址。
 */
NTSTATUS
VnvmePdoQueryResources(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _Out_ PCM_RESOURCE_LIST* Resources
    )
{
    PCM_RESOURCE_LIST resList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR resDesc;
    ULONG listSize;
    
    TRACE_INFO("VnvmePdoQueryResources: ControllerId=%lu", PdoContext->ControllerId);
    
    // 如果 BAR0 尚未分配，返回空资源列表
    if (PdoContext->Bar0VirtAddr == NULL) {
        *Resources = NULL;
        return STATUS_SUCCESS;
    }
    
    // 计算所需大小
    listSize = sizeof(CM_RESOURCE_LIST);
    
    resList = (PCM_RESOURCE_LIST)ExAllocatePool2(
        POOL_FLAG_PAGED,
        listSize,
        VNVME_POOL_TAG
        );
    
    if (resList == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(resList, listSize);
    
    // 填充资源列表
    resList->Count = 1;
    resList->List[0].InterfaceType = PCIBus;
    resList->List[0].BusNumber = 0;
    resList->List[0].PartialResourceList.Version = 1;
    resList->List[0].PartialResourceList.Revision = 1;
    resList->List[0].PartialResourceList.Count = 1;
    
    // BAR0 内存资源
    resDesc = &resList->List[0].PartialResourceList.PartialDescriptors[0];
    resDesc->Type = CmResourceTypeMemory;
    resDesc->ShareDisposition = CmResourceShareDeviceExclusive;
    resDesc->Flags = CM_RESOURCE_MEMORY_READ_WRITE | CM_RESOURCE_MEMORY_PREFETCHABLE;
    resDesc->u.Memory.Start = PdoContext->Bar0PhysAddr;
    resDesc->u.Memory.Length = (ULONG)PdoContext->Bar0Size;
    
    *Resources = resList;
    
    TRACE_INFO("VnvmePdoQueryResources: BAR0 PhysAddr=0x%llX, Size=%llu",
        PdoContext->Bar0PhysAddr.QuadPart, (ULONGLONG)PdoContext->Bar0Size);
    
    return STATUS_SUCCESS;
}
