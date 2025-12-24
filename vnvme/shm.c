/**
 * @file shm.c
 * @brief 共享内存管理
 * 
 * 分配和管理内核/用户态共享内存区域。
 */

#include "vnvme.h"

//===========================================================================
// 共享内存分配
//===========================================================================

/**
 * @brief 分配共享内存 (v2 零复制架构)
 * 
 * 共享内存布局:
 *   - Control Block (4KB)
 *   - Notify Ring (4KB)
 *   - Admin SQ (4KB) - 原始 NVME_COMMAND, stornvme 直接写入
 *   - Admin CQ (4KB) - 原始 NVME_COMPLETION, stornvme 直接读取
 *   - I/O Queue Descriptors (4KB)
 *   - I/O Queues (可变)
 *   - Data Buffer (剩余空间)
 */
NTSTATUS
VnvmeAllocateShm(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    PVOID virtAddr;
    PHYSICAL_ADDRESS lowAddress = {0};
    PHYSICAL_ADDRESS highAddress = {.QuadPart = 0xFFFFFFFFFFFFFFFF};
    PHYSICAL_ADDRESS boundaryAddress = {0};
    SIZE_T size = VNVME_SHM_SIZE;
    PVNVME_SHM_CONTROL_BLOCK controlBlock;
    PVNVME_NOTIFY_RING notifyRing;
    
    TRACE_INFO("VnvmeAllocateShm: Allocating %llu bytes (v2 zero-copy)", (ULONGLONG)size);
    
    // 分配连续物理内存
    virtAddr = MmAllocateContiguousMemorySpecifyCache(
        size,
        lowAddress,
        highAddress,
        boundaryAddress,
        MmCached
        );
    
    if (virtAddr == NULL) {
        TRACE_ERROR("VnvmeAllocateShm: MmAllocateContiguousMemorySpecifyCache failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 清零
    RtlZeroMemory(virtAddr, size);
    
    // 保存指针
    FdoContext->ShmKernelVirtAddr = virtAddr;
    FdoContext->ShmPhysAddr = MmGetPhysicalAddress(virtAddr);
    FdoContext->ShmSize = size;
    
    //==========================================================================
    // 初始化控制块 (v2)
    //==========================================================================
    controlBlock = (PVNVME_SHM_CONTROL_BLOCK)virtAddr;
    controlBlock->Magic = VNVME_SHM_MAGIC;
    controlBlock->Version = VNVME_SHM_VERSION;
    controlBlock->TotalSize = (UINT32)size;
    controlBlock->ControlBlockSize = VNVME_CONTROL_BLOCK_SIZE;
    controlBlock->Flags = 0;
    
    //==========================================================================
    // 初始化 Admin 队列描述符
    //==========================================================================
    // Admin SQ 描述符
    controlBlock->AdminSQ.Offset = VNVME_OFFSET_ADMIN_SQ;
    controlBlock->AdminSQ.EntrySize = NVME_SQ_ENTRY_SIZE;
    controlBlock->AdminSQ.Capacity = VNVME_ADMIN_QUEUE_DEPTH;
    controlBlock->AdminSQ.Valid = 0; // 等待 CC.EN=1 时激活
    controlBlock->AdminSQ.Head = 0;
    controlBlock->AdminSQ.Tail = 0;
    controlBlock->AdminSQ.Phase = 0;
    
    // Admin CQ 描述符
    controlBlock->AdminCQ.Offset = VNVME_OFFSET_ADMIN_CQ;
    controlBlock->AdminCQ.EntrySize = NVME_CQ_ENTRY_SIZE;
    controlBlock->AdminCQ.Capacity = VNVME_ADMIN_QUEUE_DEPTH;
    controlBlock->AdminCQ.Valid = 0;
    controlBlock->AdminCQ.Head = 0;
    controlBlock->AdminCQ.Tail = 0;
    controlBlock->AdminCQ.Phase = 1; // CQ 初始相位为 1
    
    //==========================================================================
    // I/O 队列配置
    //==========================================================================
    controlBlock->IoQueueCount = 0;
    controlBlock->MaxIoQueues = VNVME_MAX_IO_QUEUES;
    controlBlock->IoQueueDescriptorOffset = VNVME_OFFSET_IO_QUEUE_DESC;
    
    //==========================================================================
    // 通知环
    //==========================================================================
    controlBlock->NotifyRingOffset = VNVME_OFFSET_NOTIFY_RING;
    controlBlock->NotifyRingSize = sizeof(VNVME_NOTIFY_RING);
    
    notifyRing = (PVNVME_NOTIFY_RING)((PUCHAR)virtAddr + VNVME_OFFSET_NOTIFY_RING);
    notifyRing->Head = 0;
    notifyRing->Tail = 0;
    notifyRing->Size = VNVME_NOTIFY_RING_SIZE;
    
    //==========================================================================
    // 数据缓冲区
    //==========================================================================
    controlBlock->DataBufferOffset = VNVME_OFFSET_DATA_BUFFER;
    controlBlock->DataBufferSize = (UINT32)(size - VNVME_OFFSET_DATA_BUFFER);
    
    //==========================================================================
    // 状态
    //==========================================================================
    controlBlock->KernelReady = 1;
    controlBlock->UserReady = 0;
    controlBlock->ErrorCode = 0;
    controlBlock->ControllerState = VNVME_CTRL_STATE_DISABLED;
    
    // 统计初始化
    controlBlock->CommandsProcessed = 0;
    controlBlock->CompletionsPosted = 0;
    controlBlock->BytesRead = 0;
    controlBlock->BytesWritten = 0;
    
    TRACE_INFO("VnvmeAllocateShm: v2 zero-copy layout initialized");
    TRACE_INFO("  VirtAddr=%p, PhysAddr=0x%llX",
               virtAddr, FdoContext->ShmPhysAddr.QuadPart);
    TRACE_INFO("  Admin SQ: offset=0x%X, capacity=%u",
               controlBlock->AdminSQ.Offset, controlBlock->AdminSQ.Capacity);
    TRACE_INFO("  Admin CQ: offset=0x%X, capacity=%u",
               controlBlock->AdminCQ.Offset, controlBlock->AdminCQ.Capacity);
    TRACE_INFO("  NotifyRing: offset=0x%X, size=%u",
               controlBlock->NotifyRingOffset, controlBlock->NotifyRingSize);
    TRACE_INFO("  DataBuffer: offset=0x%X, size=%u",
               controlBlock->DataBufferOffset, controlBlock->DataBufferSize);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 释放共享内存
 */
VOID
VnvmeFreeShm(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    // 先取消用户态映射
    VnvmeUnmapShmFromUser(FdoContext);
    
    // 释放 MDL
    if (FdoContext->ShmMdl != NULL) {
        IoFreeMdl(FdoContext->ShmMdl);
        FdoContext->ShmMdl = NULL;
    }
    
    // 释放内存
    if (FdoContext->ShmKernelVirtAddr != NULL) {
        TRACE_INFO("VnvmeFreeShm: Freeing shared memory at %p",
                   FdoContext->ShmKernelVirtAddr);
        MmFreeContiguousMemory(FdoContext->ShmKernelVirtAddr);
        FdoContext->ShmKernelVirtAddr = NULL;
        FdoContext->ShmSize = 0;
    }
}

//===========================================================================
// 用户态映射
//===========================================================================

/**
 * @brief 将共享内存映射到用户空间
 */
NTSTATUS
VnvmeMapShmToUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _Out_ PVOID* UserAddress
    )
{
    PMDL mdl;
    PVOID userVirtAddr;
    
    *UserAddress = NULL;
    
    if (FdoContext->ShmKernelVirtAddr == NULL) {
        TRACE_ERROR("VnvmeMapShmToUser: Shared memory not allocated");
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    // 如果已有 MDL，复用它
    if (FdoContext->ShmMdl == NULL) {
        // 创建 MDL
        mdl = IoAllocateMdl(
            FdoContext->ShmKernelVirtAddr,
            (ULONG)FdoContext->ShmSize,
            FALSE,
            FALSE,
            NULL
            );
        
        if (mdl == NULL) {
            TRACE_ERROR("VnvmeMapShmToUser: IoAllocateMdl failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        // 锁定页面
        MmBuildMdlForNonPagedPool(mdl);
        
        FdoContext->ShmMdl = mdl;
    } else {
        mdl = FdoContext->ShmMdl;
    }
    
    // 映射到用户空间
    __try {
        userVirtAddr = MmMapLockedPagesSpecifyCache(
            mdl,
            UserMode,
            MmCached,
            NULL,
            FALSE,
            NormalPagePriority
            );
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        TRACE_ERROR("VnvmeMapShmToUser: MmMapLockedPagesSpecifyCache exception");
        return STATUS_ACCESS_VIOLATION;
    }
    
    if (userVirtAddr == NULL) {
        TRACE_ERROR("VnvmeMapShmToUser: MmMapLockedPagesSpecifyCache failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    FdoContext->ShmUserVirtAddr = userVirtAddr;
    *UserAddress = userVirtAddr;
    
    TRACE_INFO("VnvmeMapShmToUser: Mapped to user VirtAddr=%p", userVirtAddr);
    return STATUS_SUCCESS;
}

/**
 * @brief 取消用户空间映射
 */
VOID
VnvmeUnmapShmFromUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    if (FdoContext->ShmUserVirtAddr != NULL && 
        FdoContext->ShmMdl != NULL) {
        
        TRACE_INFO("VnvmeUnmapShmFromUser: Unmapping user VirtAddr=%p",
                   FdoContext->ShmUserVirtAddr);
        
        MmUnmapLockedPages(
            FdoContext->ShmUserVirtAddr,
            FdoContext->ShmMdl
            );
        
        FdoContext->ShmUserVirtAddr = NULL;
    }
}
