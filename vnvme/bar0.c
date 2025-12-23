/**
 * @file bar0.c
 * @brief BAR0 内存模拟
 * 
 * 模拟 NVMe 控制器的 BAR0 寄存器空间。
 */

#include "vnvme.h"

/*===========================================================================
 * BAR0 分配与初始化
 *===========================================================================*/

/**
 * @brief 分配 BAR0 内存
 */
NTSTATUS
VnvmeAllocateBar0(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PVOID bar0;
    
    TRACE_INFO("VnvmeAllocateBar0: Allocating %u bytes", VNVME_BAR0_SIZE);
    
    bar0 = VNVME_ALLOC_POOL(NonPagedPoolNx, VNVME_BAR0_SIZE);
    if (bar0 == NULL) {
        TRACE_ERROR("VnvmeAllocateBar0: Failed to allocate BAR0");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(bar0, VNVME_BAR0_SIZE);
    
    PdoContext->Bar0VirtAddr = bar0;
    PdoContext->Bar0Size = VNVME_BAR0_SIZE;
    
    /* 初始化寄存器默认值 */
    VnvmeInitializeBar0Registers(PdoContext);
    
    TRACE_INFO("VnvmeAllocateBar0: Allocated at %p", bar0);
    return STATUS_SUCCESS;
}

/**
 * @brief 释放 BAR0 内存
 */
VOID
VnvmeFreeBar0(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    if (PdoContext->Bar0VirtAddr != NULL) {
        TRACE_INFO("VnvmeFreeBar0: Freeing BAR0 at %p", PdoContext->Bar0VirtAddr);
        VNVME_FREE_POOL(PdoContext->Bar0VirtAddr);
        PdoContext->Bar0VirtAddr = NULL;
        PdoContext->Bar0Size = 0;
    }
}

/**
 * @brief 初始化 BAR0 寄存器默认值
 */
VOID
VnvmeInitializeBar0Registers(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PNVME_CONTROLLER_REGISTERS regs;
    
    if (PdoContext->Bar0VirtAddr == NULL) {
        return;
    }
    
    regs = (PNVME_CONTROLLER_REGISTERS)PdoContext->Bar0VirtAddr;
    
    /* CAP - Controller Capabilities */
    /* 
     * MQES = 0xFFF (4096 entries)
     * CQR = 1 (Contiguous Queues Required)
     * AMS = 0 (Round Robin only)
     * TO = 0xFF (Timeout in 500ms units)
     * DSTRD = 0 (Doorbell Stride = 2^(2+0) = 4 bytes)
     * NSSRS = 1 (NVM Subsystem Reset Supported)
     * CSS = 1 (NVM Command Set)
     * MPSMIN = 0 (4KB pages min)
     * MPSMAX = 0 (4KB pages max)
     */
    regs->CAP.AsUint64 = 0x00FF000000010FFF;
    
    /* VS - Version (1.4) */
    regs->VS.AsUint32 = 0x00010400;
    
    /* INTMS/INTMC - Interrupt Mask Set/Clear */
    regs->INTMS = 0;
    regs->INTMC = 0;
    
    /* CC - Controller Configuration (disabled) */
    regs->CC.AsUint32 = 0;
    
    /* CSTS - Controller Status (not ready) */
    regs->CSTS.AsUint32 = 0;
    
    /* AQA - Admin Queue Attributes */
    regs->AQA.AsUint32 = 0;
    
    /* ASQ - Admin Submission Queue Base Address */
    regs->ASQ = 0;
    
    /* ACQ - Admin Completion Queue Base Address */
    regs->ACQ = 0;
    
    TRACE_INFO("VnvmeInitializeBar0Registers: Initialized default values");
    TRACE_INFO("  CAP=0x%016llX, VS=0x%08X", regs->CAP.AsUint64, regs->VS.AsUint32);
}

/*===========================================================================
 * BAR0 寄存器访问
 *===========================================================================*/

/**
 * @brief 读取 BAR0 寄存器
 */
ULONG
VnvmeReadBar0Register(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset
    )
{
    PULONG reg;
    
    if (PdoContext->Bar0VirtAddr == NULL || Offset >= PdoContext->Bar0Size) {
        TRACE_WARN("VnvmeReadBar0Register: Invalid offset 0x%X", Offset);
        return 0xFFFFFFFF;
    }
    
    reg = (PULONG)((PUCHAR)PdoContext->Bar0VirtAddr + Offset);
    return *reg;
}

/**
 * @brief 写入 BAR0 寄存器
 */
VOID
VnvmeWriteBar0Register(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONG Value
    )
{
    PULONG reg;
    
    if (PdoContext->Bar0VirtAddr == NULL || Offset >= PdoContext->Bar0Size) {
        TRACE_WARN("VnvmeWriteBar0Register: Invalid offset 0x%X", Offset);
        return;
    }
    
    reg = (PULONG)((PUCHAR)PdoContext->Bar0VirtAddr + Offset);
    
    TRACE_VERBOSE("VnvmeWriteBar0Register: Offset=0x%X, Value=0x%08X", Offset, Value);
    
    /* TODO: Phase 3 - 处理特殊寄存器写入 */
    /* CC 寄存器写入需要处理使能/禁用逻辑 */
    
    *reg = Value;
}

/**
 * @brief 读取 64 位 BAR0 寄存器
 */
ULONGLONG
VnvmeReadBar0Register64(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset
    )
{
    PULONGLONG reg;
    
    if (PdoContext->Bar0VirtAddr == NULL || (Offset + 8) > PdoContext->Bar0Size) {
        TRACE_WARN("VnvmeReadBar0Register64: Invalid offset 0x%X", Offset);
        return 0xFFFFFFFFFFFFFFFF;
    }
    
    reg = (PULONGLONG)((PUCHAR)PdoContext->Bar0VirtAddr + Offset);
    return *reg;
}

/**
 * @brief 写入 64 位 BAR0 寄存器
 */
VOID
VnvmeWriteBar0Register64(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG Offset,
    _In_ ULONGLONG Value
    )
{
    PULONGLONG reg;
    
    if (PdoContext->Bar0VirtAddr == NULL || (Offset + 8) > PdoContext->Bar0Size) {
        TRACE_WARN("VnvmeWriteBar0Register64: Invalid offset 0x%X", Offset);
        return;
    }
    
    reg = (PULONGLONG)((PUCHAR)PdoContext->Bar0VirtAddr + Offset);
    
    TRACE_VERBOSE("VnvmeWriteBar0Register64: Offset=0x%X, Value=0x%016llX", Offset, Value);
    
    *reg = Value;
}
