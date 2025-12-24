/**
 * @file pcie_config.c
 * @brief PCIe 配置空间模拟
 * 
 * 模拟 NVMe 控制器的 PCIe 配置空间。
 */

#include "vnvme.h"

//===========================================================================
// PCIe 配置空间结构
//===========================================================================

// PCIe 配置空间寄存器偏移
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_REVISION_ID         0x08
#define PCI_CLASS_CODE          0x09
#define PCI_CACHE_LINE_SIZE     0x0C
#define PCI_LATENCY_TIMER       0x0D
#define PCI_HEADER_TYPE         0x0E
#define PCI_BIST                0x0F
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_SUBSYSTEM_VENDOR_ID 0x2C
#define PCI_SUBSYSTEM_ID        0x2E
#define PCI_CAPABILITY_PTR      0x34
#define PCI_INTERRUPT_LINE      0x3C
#define PCI_INTERRUPT_PIN       0x3D

// 虚拟 NVMe 控制器的 VID/DID
#define VNVME_VENDOR_ID         0x1D94  // 虚拟厂商 ID
#define VNVME_DEVICE_ID         0x7001  // 虚拟设备 ID
#define VNVME_SUBSYSTEM_VID     0x1D94
#define VNVME_SUBSYSTEM_ID      0x0001
#define VNVME_REVISION_ID       0x01

// NVMe 设备类代码
#define NVME_CLASS_CODE         0x010802  // Mass Storage Controller, NVM, NVMe

//===========================================================================
// 配置空间分配与初始化
//===========================================================================

/**
 * @brief 分配 PCIe 配置空间
 */
NTSTATUS
VnvmeAllocatePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PVOID configSpace;
    
    TRACE_INFO("VnvmeAllocatePcieConfig: Allocating %u bytes", VNVME_PCIE_CONFIG_SIZE);
    
    configSpace = VNVME_ALLOC_POOL(NonPagedPoolNx, VNVME_PCIE_CONFIG_SIZE);
    if (configSpace == NULL) {
        TRACE_ERROR("VnvmeAllocatePcieConfig: Failed to allocate config space");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(configSpace, VNVME_PCIE_CONFIG_SIZE);
    
    PdoContext->PcieConfig = configSpace;
    PdoContext->PcieConfigSize = VNVME_PCIE_CONFIG_SIZE;
    
    // 初始化配置空间
    VnvmeInitializePcieConfig(PdoContext);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 释放 PCIe 配置空间
 */
VOID
VnvmeFreePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    if (PdoContext->PcieConfig != NULL) {
        TRACE_INFO("VnvmeFreePcieConfig: Freeing config space");
        VNVME_FREE_POOL(PdoContext->PcieConfig);
        PdoContext->PcieConfig = NULL;
        PdoContext->PcieConfigSize = 0;
    }
}

/**
 * @brief 初始化 PCIe 配置空间默认值
 */
VOID
VnvmeInitializePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PUCHAR config;
    
    if (PdoContext->PcieConfig == NULL) {
        return;
    }
    
    config = (PUCHAR)PdoContext->PcieConfig;
    
    // Vendor ID and Device ID
    *(PUSHORT)(config + PCI_VENDOR_ID) = VNVME_VENDOR_ID;
    *(PUSHORT)(config + PCI_DEVICE_ID) = VNVME_DEVICE_ID;
    
    // Command and Status
    *(PUSHORT)(config + PCI_COMMAND) = 0x0000;  // 禁用状态
    *(PUSHORT)(config + PCI_STATUS) = 0x0010;   // Capabilities List
    
    // Revision ID and Class Code
    *(PUCHAR)(config + PCI_REVISION_ID) = VNVME_REVISION_ID;
    config[PCI_CLASS_CODE + 0] = 0x02;  // Programming Interface: NVMe
    config[PCI_CLASS_CODE + 1] = 0x08;  // Sub Class: Non-Volatile Memory
    config[PCI_CLASS_CODE + 2] = 0x01;  // Base Class: Mass Storage
    
    // Header Type
    *(PUCHAR)(config + PCI_HEADER_TYPE) = 0x00;  // Type 0 (Endpoint)
    
    // BAR0 (64-bit, Memory, Non-Prefetchable)
    *(PULONG)(config + PCI_BAR0) = 0x00000004;  // 64-bit, Memory
    *(PULONG)(config + PCI_BAR1) = 0x00000000;  // High 32 bits
    
    // Subsystem Vendor/Device ID
    *(PUSHORT)(config + PCI_SUBSYSTEM_VENDOR_ID) = VNVME_SUBSYSTEM_VID;
    *(PUSHORT)(config + PCI_SUBSYSTEM_ID) = VNVME_SUBSYSTEM_ID;
    
    // Capabilities Pointer
    *(PUCHAR)(config + PCI_CAPABILITY_PTR) = 0x40;  // First capability at 0x40
    
    // Interrupt Line/Pin
    *(PUCHAR)(config + PCI_INTERRUPT_LINE) = 0x00;
    *(PUCHAR)(config + PCI_INTERRUPT_PIN) = 0x01;  // INTA#
    
    // =========================================================================
    // PCIe 能力链表 (Capability List)
    // =========================================================================
    
    // Power Management Capability (0x40)
    // Capability ID = 0x01 (Power Management)
    config[0x40] = 0x01;              // Capability ID: PM
    config[0x41] = 0x50;              // Next Capability Pointer: MSI
    *(PUSHORT)(config + 0x42) = 0x0003;  // PMC: D0, D3hot supported, Version 3
    *(PUSHORT)(config + 0x44) = 0x0000;  // PMCSR: D0 state
    config[0x46] = 0x00;              // PMCSR_BSE
    config[0x47] = 0x00;              // Data
    
    // MSI Capability (0x50)
    // Capability ID = 0x05 (MSI)
    config[0x50] = 0x05;              // Capability ID: MSI
    config[0x51] = 0x60;              // Next Capability Pointer: MSI-X
    *(PUSHORT)(config + 0x52) = 0x0080;  // Message Control: 64-bit capable
    *(PULONG)(config + 0x54) = 0x00000000;  // Message Address (low)
    *(PULONG)(config + 0x58) = 0x00000000;  // Message Address (high)
    *(PUSHORT)(config + 0x5C) = 0x0000;  // Message Data
    
    // MSI-X Capability (0x60)
    // Capability ID = 0x11 (MSI-X)
    config[0x60] = 0x11;              // Capability ID: MSI-X
    config[0x61] = 0x70;              // Next Capability Pointer: PCIe
    *(PUSHORT)(config + 0x62) = 0x001F;  // Message Control: 32 vectors, disabled
    *(PULONG)(config + 0x64) = 0x00002000;  // Table BIR=0, Offset=0x2000
    *(PULONG)(config + 0x68) = 0x00003000;  // PBA BIR=0, Offset=0x3000
    
    // PCI Express Capability (0x70)
    // Capability ID = 0x10 (PCI Express)
    config[0x70] = 0x10;              // Capability ID: PCIe
    config[0x71] = 0x00;              // Next Capability Pointer: None
    *(PUSHORT)(config + 0x72) = 0x0002;  // PCIe Caps: v2, Endpoint
    *(PULONG)(config + 0x74) = 0x00008001;  // Device Caps: 256B MPS, Ext Tag
    *(PUSHORT)(config + 0x78) = 0x0010;  // Device Control: MPS=256
    *(PUSHORT)(config + 0x7A) = 0x0000;  // Device Status
    *(PULONG)(config + 0x7C) = 0x00000001;  // Link Caps: 2.5 GT/s, x1
    *(PUSHORT)(config + 0x80) = 0x0000;  // Link Control
    *(PUSHORT)(config + 0x82) = 0x0011;  // Link Status: 2.5 GT/s, x1
    
    TRACE_INFO("VnvmeInitializePcieConfig: Initialized with PM/MSI/MSI-X/PCIe caps");
    TRACE_INFO("  VID=0x%04X, DID=0x%04X, Class=0x%02X%02X%02X",
               VNVME_VENDOR_ID, VNVME_DEVICE_ID,
               config[PCI_CLASS_CODE + 2], 
               config[PCI_CLASS_CODE + 1], 
               config[PCI_CLASS_CODE + 0]);
}

//===========================================================================
// 配置空间访问
//===========================================================================

/**
 * @brief 读取 PCIe 配置空间
 */
NTSTATUS
VnvmeReadPcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PVOID Buffer
    )
{
    PUCHAR config;
    
    if (PdoContext->PcieConfig == NULL) {
        RtlZeroMemory(Buffer, Length);
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    if (Offset + Length > PdoContext->PcieConfigSize) {
        TRACE_WARN("VnvmeReadPcieConfig: Out of range, Offset=0x%X, Length=%u",
                   Offset, Length);
        RtlZeroMemory(Buffer, Length);
        return STATUS_INVALID_PARAMETER;
    }
    
    config = (PUCHAR)PdoContext->PcieConfig;
    RtlCopyMemory(Buffer, config + Offset, Length);
    
    TRACE_VERBOSE("VnvmeReadPcieConfig: Offset=0x%X, Length=%u", Offset, Length);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 写入 PCIe 配置空间
 */
NTSTATUS
VnvmeWritePcieConfig(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PVOID Buffer
    )
{
    PUCHAR config;
    USHORT value16;
    
    if (PdoContext->PcieConfig == NULL) {
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    if (Offset + Length > PdoContext->PcieConfigSize) {
        TRACE_WARN("VnvmeWritePcieConfig: Out of range, Offset=0x%X, Length=%u",
                   Offset, Length);
        return STATUS_INVALID_PARAMETER;
    }
    
    config = (PUCHAR)PdoContext->PcieConfig;
    
    TRACE_VERBOSE("VnvmeWritePcieConfig: Offset=0x%X, Length=%u", Offset, Length);
    
    // =========================================================================
    // 处理特殊寄存器写入
    // =========================================================================
    
    // Command 寄存器 (0x04) - 某些位是只读的
    if (Offset == PCI_COMMAND && Length >= 2) {
        value16 = *(PUSHORT)Buffer;
        // 保持 Status 寄存器的只读位
        // 允许: Bus Master Enable (bit 2), Memory Space Enable (bit 1)
        value16 &= 0x0547;  // 屏蔽保留位
        *(PUSHORT)(config + Offset) = value16;
        TRACE_INFO("VnvmeWritePcieConfig: Command register = 0x%04X", value16);
        return STATUS_SUCCESS;
    }
    
    // PM Control/Status (0x44) - 电源状态转换
    if (Offset == 0x44 && Length >= 2) {
        value16 = *(PUSHORT)Buffer;
        UCHAR newState = (UCHAR)(value16 & 0x03);  // PowerState bits
        TRACE_INFO("VnvmeWritePcieConfig: PM state transition to D%u", newState);
        // 只允许 D0 和 D3hot
        if (newState == 0 || newState == 3) {
            *(PUSHORT)(config + Offset) = value16 & 0x8103;  // 保留有效位
        }
        return STATUS_SUCCESS;
    }
    
    // MSI-X Message Control (0x62) - Enable/Disable
    if (Offset == 0x62 && Length >= 2) {
        value16 = *(PUSHORT)Buffer;
        BOOLEAN enabled = (value16 & 0x8000) != 0;
        TRACE_INFO("VnvmeWritePcieConfig: MSI-X %s", enabled ? "enabled" : "disabled");
        // Table Size 是只读的 (低 11 位)
        value16 = (value16 & 0xC000) | (*(PUSHORT)(config + Offset) & 0x07FF);
        *(PUSHORT)(config + Offset) = value16;
        return STATUS_SUCCESS;
    }
    
    // MSI Message Control (0x52) - Enable/Disable
    if (Offset == 0x52 && Length >= 2) {
        value16 = *(PUSHORT)Buffer;
        BOOLEAN enabled = (value16 & 0x0001) != 0;
        TRACE_INFO("VnvmeWritePcieConfig: MSI %s", enabled ? "enabled" : "disabled");
        *(PUSHORT)(config + Offset) = value16;
        return STATUS_SUCCESS;
    }
    
    // 只读寄存器保护
    if (Offset == PCI_VENDOR_ID || Offset == PCI_DEVICE_ID ||
        Offset == PCI_REVISION_ID || Offset == PCI_CLASS_CODE ||
        Offset == PCI_HEADER_TYPE || Offset == PCI_CAPABILITY_PTR) {
        TRACE_WARN("VnvmeWritePcieConfig: Attempt to write read-only register 0x%X", Offset);
        return STATUS_SUCCESS;  // 静默忽略
    }
    
    // 其他寄存器直接写入
    RtlCopyMemory(config + Offset, Buffer, Length);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// BUS_INTERFACE_STANDARD 实现
//===========================================================================

// 接口引用计数 (简化实现，不实际跟踪)
static VOID
VnvmeInterfaceReference(
    _In_ PVOID Context
    )
{
    UNREFERENCED_PARAMETER(Context);
    // 引用计数在 WDFDEVICE 层面管理
}

static VOID
VnvmeInterfaceDereference(
    _In_ PVOID Context
    )
{
    UNREFERENCED_PARAMETER(Context);
    // 引用计数在 WDFDEVICE 层面管理
}

/**
 * @brief 读取配置空间 (BUS_INTERFACE_STANDARD 回调)
 */
static ULONG
VnvmeInterfaceGetBusData(
    _In_ PVOID Context,
    _In_ ULONG DataType,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length
    )
{
    PVNVME_PDO_CONTEXT pdoContext = (PVNVME_PDO_CONTEXT)Context;
    NTSTATUS status;
    
    // 只支持配置空间读取
    if (DataType != PCI_WHICHSPACE_CONFIG) {
        TRACE_WARN("VnvmeInterfaceGetBusData: Unsupported DataType=%lu", DataType);
        return 0;
    }
    
    status = VnvmeReadPcieConfig(pdoContext, Offset, Length, Buffer);
    if (!NT_SUCCESS(status)) {
        return 0;
    }
    
    return Length;
}

/**
 * @brief 写入配置空间 (BUS_INTERFACE_STANDARD 回调)
 */
static ULONG
VnvmeInterfaceSetBusData(
    _In_ PVOID Context,
    _In_ ULONG DataType,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length
    )
{
    PVNVME_PDO_CONTEXT pdoContext = (PVNVME_PDO_CONTEXT)Context;
    NTSTATUS status;
    
    // 只支持配置空间写入
    if (DataType != PCI_WHICHSPACE_CONFIG) {
        TRACE_WARN("VnvmeInterfaceSetBusData: Unsupported DataType=%lu", DataType);
        return 0;
    }
    
    status = VnvmeWritePcieConfig(pdoContext, Offset, Length, Buffer);
    if (!NT_SUCCESS(status)) {
        return 0;
    }
    
    return Length;
}

/**
 * @brief 翻译总线地址 (返回物理地址)
 */
static BOOLEAN
VnvmeInterfaceTranslateBusAddress(
    _In_ PVOID Context,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _In_ ULONG Length,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Length);
    
    // 虚拟设备: 直接返回相同地址
    // AddressSpace: 0 = 内存, 1 = I/O
    if (*AddressSpace == 0) {
        *TranslatedAddress = BusAddress;
        return TRUE;
    }
    
    return FALSE;
}

/**
 * @brief 获取 DMA 适配器 (虚拟设备不需要真正的 DMA)
 */
static PDMA_ADAPTER
VnvmeInterfaceGetDmaAdapter(
    _In_ PVOID Context,
    _In_ PDEVICE_DESCRIPTION DeviceDescription,
    _Out_ PULONG NumberOfMapRegisters
    )
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(DeviceDescription);
    
    // 虚拟设备不需要 DMA 适配器
    *NumberOfMapRegisters = 0;
    return NULL;
}

/**
 * @brief 查询接口 (处理 IRP_MN_QUERY_INTERFACE)
 * 
 * 返回 BUS_INTERFACE_STANDARD，让 stornvme 能够读写 PCIe 配置空间。
 */
NTSTATUS
VnvmePdoQueryInterface(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ LPCGUID InterfaceType,
    _In_ USHORT Size,
    _In_ USHORT Version,
    _In_opt_ PVOID InterfaceSpecificData,
    _Inout_ PINTERFACE Interface
    )
{
    PBUS_INTERFACE_STANDARD busInterface;
    
    UNREFERENCED_PARAMETER(InterfaceSpecificData);
    
    TRACE_INFO("VnvmePdoQueryInterface: ControllerId=%lu", PdoContext->ControllerId);
    
    // 检查是否请求 BUS_INTERFACE_STANDARD
    if (!IsEqualGUID(InterfaceType, &GUID_BUS_INTERFACE_STANDARD)) {
        return STATUS_NOT_SUPPORTED;
    }
    
    if (Version != 1 || Size < sizeof(BUS_INTERFACE_STANDARD)) {
        return STATUS_INVALID_PARAMETER;
    }
    
    busInterface = (PBUS_INTERFACE_STANDARD)Interface;
    
    // 填充接口
    busInterface->Size = sizeof(BUS_INTERFACE_STANDARD);
    busInterface->Version = 1;
    busInterface->Context = PdoContext;
    busInterface->InterfaceReference = VnvmeInterfaceReference;
    busInterface->InterfaceDereference = VnvmeInterfaceDereference;
    busInterface->TranslateBusAddress = VnvmeInterfaceTranslateBusAddress;
    busInterface->GetDmaAdapter = VnvmeInterfaceGetDmaAdapter;
    busInterface->SetBusData = VnvmeInterfaceSetBusData;
    busInterface->GetBusData = VnvmeInterfaceGetBusData;
    
    TRACE_INFO("VnvmePdoQueryInterface: BUS_INTERFACE_STANDARD provided");
    
    return STATUS_SUCCESS;
}
