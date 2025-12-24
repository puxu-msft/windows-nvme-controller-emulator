/**
 * @file bar0.c
 * @brief BAR0 内存模拟
 * 
 * 模拟 NVMe 控制器的 BAR0 寄存器空间。
 */

#include "vnvme.h"

//===========================================================================
// BAR0 分配与初始化
//===========================================================================

/**
 * @brief 分配 BAR0 内存
 * 
 * 使用物理连续内存，因为需要向 stornvme 报告物理地址。
 * 使用 MmNonCached 确保 stornvme 的写入立即可见。
 */
NTSTATUS
VnvmeAllocateBar0(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PHYSICAL_ADDRESS lowAddr = {0};
    PHYSICAL_ADDRESS highAddr;
    PHYSICAL_ADDRESS boundary = {0};
    PVOID bar0;
    
    highAddr.QuadPart = (ULONGLONG)-1;  // 使用所有可用物理地址
    
    TRACE_INFO("VnvmeAllocateBar0: Allocating %u bytes (contiguous, non-cached)", 
               VNVME_BAR0_SIZE);
    
    // 分配物理连续、非缓存内存
    bar0 = MmAllocateContiguousMemorySpecifyCache(
        VNVME_BAR0_SIZE,
        lowAddr,
        highAddr,
        boundary,
        MmNonCached
        );
    
    if (bar0 == NULL) {
        TRACE_ERROR("VnvmeAllocateBar0: MmAllocateContiguousMemorySpecifyCache failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    RtlZeroMemory(bar0, VNVME_BAR0_SIZE);
    
    PdoContext->Bar0VirtAddr = bar0;
    PdoContext->Bar0PhysAddr = MmGetPhysicalAddress(bar0);
    PdoContext->Bar0Size = VNVME_BAR0_SIZE;
    
    // 初始化寄存器默认值
    VnvmeInitializeBar0Registers(PdoContext);
    
    TRACE_INFO("VnvmeAllocateBar0: Allocated at VA=%p, PA=0x%llX", 
               bar0, PdoContext->Bar0PhysAddr.QuadPart);
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
        TRACE_INFO("VnvmeFreeBar0: Freeing BAR0 at VA=%p, PA=0x%llX", 
                   PdoContext->Bar0VirtAddr, 
                   PdoContext->Bar0PhysAddr.QuadPart);
        MmFreeContiguousMemory(PdoContext->Bar0VirtAddr);
        PdoContext->Bar0VirtAddr = NULL;
        PdoContext->Bar0PhysAddr.QuadPart = 0;
        PdoContext->Bar0Size = 0;
        PdoContext->Registers = NULL;
        PdoContext->Doorbells = NULL;
    }
}

/**
 * @brief 初始化 BAR0 寄存器默认值
 * 
 * 设置 NVMe 控制器的静态寄存器值，stornvme 会直接读取这些值。
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
    
    // CAP - Controller Capabilities (64-bit, offset 0x00)
    //
    // Bit fields:
    //   [15:0]  MQES   = 0xFFF (4096 entries max)
    //   [16]    CQR    = 1 (Contiguous Queues Required)
    //   [18:17] AMS    = 0 (Round Robin only)
    //   [23:19] Reserved = 0
    //   [31:24] TO     = 40 (Timeout: 40 * 500ms = 20 seconds)
    //   [35:32] DSTRD  = 0 (Doorbell Stride: 2^(2+0) = 4 bytes)
    //   [36]    NSSRS  = 0 (NVM Subsystem Reset Not Supported)
    //   [44:37] CSS    = 1 (NVM Command Set)
    //   [48]    BPS    = 0
    //   [51:48] MPSMIN = 0 (4KB pages min)
    //   [55:52] MPSMAX = 0 (4KB pages max)
    //   [63:56] Reserved = 0
    //
    // 打包: 0x0028_0000_0001_0FFF
    //       TO=40 在 bits[31:24], CSS=1 在 bit[37]
    regs->CAP.AsUint64 = 0x0028000000010FFF;
    
    // VS - Version (32-bit, offset 0x08)
    // Major = 1, Minor = 4, Tertiary = 0  =>  NVMe 1.4
    regs->VS.AsUint32 = 0x00010400;
    
    // INTMS/INTMC - Interrupt Mask Set/Clear (offset 0x0C/0x10)
    regs->INTMS = 0;
    regs->INTMC = 0;
    
    // CC - Controller Configuration (offset 0x14, disabled)
    regs->CC.AsUint32 = 0;
    
    // CSTS - Controller Status (offset 0x1C, not ready)
    regs->CSTS.AsUint32 = 0;
    
    // AQA - Admin Queue Attributes (offset 0x24)
    regs->AQA.AsUint32 = 0;
    
    // ASQ - Admin Submission Queue Base Address (offset 0x28)
    regs->ASQ = 0;
    
    // ACQ - Admin Completion Queue Base Address (offset 0x30)
    regs->ACQ = 0;
    
    // 设置寄存器指针供其他模块使用
    PdoContext->Registers = regs;
    PdoContext->Doorbells = (PULONG)((PUCHAR)PdoContext->Bar0VirtAddr + 0x1000);
    PdoContext->CachedCC = 0;
    
    TRACE_INFO("VnvmeInitializeBar0Registers: Initialized");
    TRACE_INFO("  CAP=0x%016llX (MQES=%u, TO=%u, CSS=%u)", 
               regs->CAP.AsUint64,
               (ULONG)(regs->CAP.AsUint64 & 0xFFFF),
               (ULONG)((regs->CAP.AsUint64 >> 24) & 0xFF),
               (ULONG)((regs->CAP.AsUint64 >> 37) & 0xFF));
    TRACE_INFO("  VS=0x%08X (NVMe %u.%u)", 
               regs->VS.AsUint32,
               (regs->VS.AsUint32 >> 16) & 0xFFFF,
               (regs->VS.AsUint32 >> 8) & 0xFF);
    TRACE_INFO("  Doorbells at offset 0x1000, VA=%p", PdoContext->Doorbells);
}

//===========================================================================
// BAR0 寄存器访问
//===========================================================================

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
 * 
 * 处理 NVMe 控制器寄存器写入，某些寄存器需要特殊处理。
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
    
    // =========================================================================
    // 处理特殊寄存器写入
    // =========================================================================
    
    switch (Offset) {
    case 0x00:  // CAP (低32位) - 只读
    case 0x04:  // CAP (高32位) - 只读
    case 0x08:  // VS - 只读
    case 0x1C:  // CSTS - 只读 (由硬件更新)
    case 0x38:  // CMBLOC - 只读
    case 0x3C:  // CMBSZ - 只读
        TRACE_VERBOSE("VnvmeWriteBar0Register: Read-only register 0x%X, ignored", Offset);
        return;  // 忽略写入只读寄存器
        
    case 0x0C:  // INTMS (Interrupt Mask Set)
        // 设置中断屏蔽位
        *reg |= Value;
        TRACE_INFO("VnvmeWriteBar0Register: INTMS write 0x%X, new mask=0x%X", Value, *reg);
        return;
        
    case 0x10:  // INTMC (Interrupt Mask Clear)
        // 清除中断屏蔽位
        *(PULONG)((PUCHAR)PdoContext->Bar0VirtAddr + 0x0C) &= ~Value;
        TRACE_INFO("VnvmeWriteBar0Register: INTMC write 0x%X", Value);
        return;
        
    case 0x14:  // CC (Controller Configuration)
        {
            ULONG oldCC = *reg;
            BOOLEAN oldEN = (oldCC & 0x1) != 0;
            BOOLEAN newEN = (Value & 0x1) != 0;
            
            // 记录 CC 变化
            TRACE_INFO("VnvmeWriteBar0Register: CC 0x%08X -> 0x%08X (EN: %u->%u)",
                       oldCC, Value, oldEN, newEN);
            
            // 更新 CC 寄存器
            *reg = Value;
            
            // CC.EN 变化会在 doorbell.c 的 VnvmeProcessDoorbells 中处理
            // 这里只需要记录变化
        }
        return;
        
    case 0x20:  // NSSR (NVM Subsystem Reset)
        if (Value == 0x4E564D65) {  // "NVMe" in little-endian
            TRACE_INFO("VnvmeWriteBar0Register: NVM Subsystem Reset triggered");
            // 重置控制器状态
            if (PdoContext->Registers != NULL) {
                PdoContext->Registers->CSTS.AsUint32 = 0;
                PdoContext->Registers->CC.AsUint32 = 0;
            }
        }
        // NSSR 不保存写入值
        return;
        
    case 0x24:  // AQA (Admin Queue Attributes)
        TRACE_INFO("VnvmeWriteBar0Register: AQA = 0x%08X (ASQS=%u, ACQS=%u)",
                   Value, (Value & 0xFFF) + 1, ((Value >> 16) & 0xFFF) + 1);
        *reg = Value;
        return;
        
    default:
        // 其他寄存器直接写入
        *reg = Value;
        return;
    }
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
