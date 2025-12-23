/**
 * @file shm.c
 * @brief 共享内存管理
 * 
 * 分配和管理内核/用户态共享内存区域。
 */

#include "vnvme.h"

/*===========================================================================
 * 共享内存分配
 *===========================================================================*/

/**
 * @brief 分配共享内存
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
    SIZE_T size = VNVME_SHARED_MEMORY_SIZE;
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK controlBlock;
    
    TRACE_INFO("VnvmeAllocateShm: Allocating %llu bytes", (ULONGLONG)size);
    
    /* 分配连续物理内存 */
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
    
    /* 清零 */
    RtlZeroMemory(virtAddr, size);
    
    /* 保存指针 */
    FdoContext->ShmKernelVirtAddr = virtAddr;
    FdoContext->ShmPhysAddr = MmGetPhysicalAddress(virtAddr);
    FdoContext->ShmSize = size;
    
    /* 初始化控制块 */
    controlBlock = (PVNVME_SHARED_MEMORY_CONTROL_BLOCK)virtAddr;
    controlBlock->Magic = VNVME_SHARED_MEMORY_MAGIC;
    controlBlock->Version = VNVME_SHARED_MEMORY_VERSION;
    controlBlock->TotalSize = (UINT32)size;
    controlBlock->ControlBlockSize = VNVME_CONTROL_BLOCK_SIZE;
    
    /* 计算环偏移 */
    controlBlock->SubmissionRingOffset = VNVME_CONTROL_BLOCK_SIZE;
    controlBlock->SubmissionRingSize = VNVME_SUBMISSION_RING_SIZE;
    controlBlock->CompletionRingOffset = controlBlock->SubmissionRingOffset + 
        sizeof(VNVME_SUBMISSION_RING);
    controlBlock->CompletionRingSize = VNVME_COMPLETION_RING_SIZE;
    
    /* 计算数据缓冲区偏移 */
    controlBlock->DataBufferOffset = controlBlock->CompletionRingOffset + 
        sizeof(VNVME_COMPLETION_RING);
    controlBlock->DataBufferSize = (UINT32)(size - controlBlock->DataBufferOffset);
    
    /* 初始化状态 */
    controlBlock->KernelReady = 1;
    controlBlock->UserReady = 0;
    controlBlock->ErrorCode = 0;
    
    TRACE_INFO("VnvmeAllocateShm: Allocated at VirtAddr=%p, PhysAddr=0x%llX",
               virtAddr, FdoContext->ShmPhysAddr.QuadPart);
    TRACE_INFO("  SubmissionRing: offset=0x%X, size=%u",
               controlBlock->SubmissionRingOffset, controlBlock->SubmissionRingSize);
    TRACE_INFO("  CompletionRing: offset=0x%X, size=%u",
               controlBlock->CompletionRingOffset, controlBlock->CompletionRingSize);
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
    /* 先取消用户态映射 */
    VnvmeUnmapShmFromUser(FdoContext);
    
    /* 释放 MDL */
    if (FdoContext->ShmMdl != NULL) {
        IoFreeMdl(FdoContext->ShmMdl);
        FdoContext->ShmMdl = NULL;
    }
    
    /* 释放内存 */
    if (FdoContext->ShmKernelVirtAddr != NULL) {
        TRACE_INFO("VnvmeFreeShm: Freeing shared memory at %p",
                   FdoContext->ShmKernelVirtAddr);
        MmFreeContiguousMemory(FdoContext->ShmKernelVirtAddr);
        FdoContext->ShmKernelVirtAddr = NULL;
        FdoContext->ShmSize = 0;
    }
}

/*===========================================================================
 * 用户态映射
 *===========================================================================*/

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
    
    /* 如果已有 MDL，复用它 */
    if (FdoContext->ShmMdl == NULL) {
        /* 创建 MDL */
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
        
        /* 锁定页面 */
        MmBuildMdlForNonPagedPool(mdl);
        
        FdoContext->ShmMdl = mdl;
    } else {
        mdl = FdoContext->ShmMdl;
    }
    
    /* 映射到用户空间 */
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
