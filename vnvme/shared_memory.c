/**
 * @file shared_memory.c
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
VnvmeAllocateSharedMemory(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    PVOID virtualAddress;
    PHYSICAL_ADDRESS lowAddress = {0};
    PHYSICAL_ADDRESS highAddress = {.QuadPart = 0xFFFFFFFFFFFFFFFF};
    PHYSICAL_ADDRESS boundaryAddress = {0};
    SIZE_T size = VNVME_SHARED_MEMORY_SIZE;
    PVNVME_SHARED_MEMORY_CONTROL_BLOCK controlBlock;
    
    TRACE_INFO("VnvmeAllocateSharedMemory: Allocating %llu bytes", (ULONGLONG)size);
    
    /* 分配连续物理内存 */
    virtualAddress = MmAllocateContiguousMemorySpecifyCache(
        size,
        lowAddress,
        highAddress,
        boundaryAddress,
        MmCached
        );
    
    if (virtualAddress == NULL) {
        TRACE_ERROR("VnvmeAllocateSharedMemory: MmAllocateContiguousMemorySpecifyCache failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* 清零 */
    RtlZeroMemory(virtualAddress, size);
    
    /* 保存指针 */
    FdoContext->SharedMemory = virtualAddress;
    FdoContext->SharedMemoryPhysical = MmGetPhysicalAddress(virtualAddress);
    FdoContext->SharedMemorySize = size;
    
    /* 初始化控制块 */
    controlBlock = (PVNVME_SHARED_MEMORY_CONTROL_BLOCK)virtualAddress;
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
    
    TRACE_INFO("VnvmeAllocateSharedMemory: Allocated at VA=%p, PA=0x%llX",
               virtualAddress, FdoContext->SharedMemoryPhysical.QuadPart);
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
VnvmeFreeSharedMemory(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    /* 先取消用户态映射 */
    VnvmeUnmapSharedMemoryFromUser(FdoContext);
    
    /* 释放 MDL */
    if (FdoContext->SharedMemoryMdl != NULL) {
        IoFreeMdl(FdoContext->SharedMemoryMdl);
        FdoContext->SharedMemoryMdl = NULL;
    }
    
    /* 释放内存 */
    if (FdoContext->SharedMemory != NULL) {
        TRACE_INFO("VnvmeFreeSharedMemory: Freeing shared memory at %p",
                   FdoContext->SharedMemory);
        MmFreeContiguousMemory(FdoContext->SharedMemory);
        FdoContext->SharedMemory = NULL;
        FdoContext->SharedMemorySize = 0;
    }
}

/*===========================================================================
 * 用户态映射
 *===========================================================================*/

/**
 * @brief 将共享内存映射到用户空间
 */
NTSTATUS
VnvmeMapSharedMemoryToUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _Out_ PVOID* UserAddress
    )
{
    PMDL mdl;
    PVOID userVa;
    
    *UserAddress = NULL;
    
    if (FdoContext->SharedMemory == NULL) {
        TRACE_ERROR("VnvmeMapSharedMemoryToUser: Shared memory not allocated");
        return STATUS_INVALID_DEVICE_STATE;
    }
    
    /* 如果已有 MDL，复用它 */
    if (FdoContext->SharedMemoryMdl == NULL) {
        /* 创建 MDL */
        mdl = IoAllocateMdl(
            FdoContext->SharedMemory,
            (ULONG)FdoContext->SharedMemorySize,
            FALSE,
            FALSE,
            NULL
            );
        
        if (mdl == NULL) {
            TRACE_ERROR("VnvmeMapSharedMemoryToUser: IoAllocateMdl failed");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        
        /* 锁定页面 */
        MmBuildMdlForNonPagedPool(mdl);
        
        FdoContext->SharedMemoryMdl = mdl;
    } else {
        mdl = FdoContext->SharedMemoryMdl;
    }
    
    /* 映射到用户空间 */
    __try {
        userVa = MmMapLockedPagesSpecifyCache(
            mdl,
            UserMode,
            MmCached,
            NULL,
            FALSE,
            NormalPagePriority
            );
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        TRACE_ERROR("VnvmeMapSharedMemoryToUser: MmMapLockedPagesSpecifyCache exception");
        return STATUS_ACCESS_VIOLATION;
    }
    
    if (userVa == NULL) {
        TRACE_ERROR("VnvmeMapSharedMemoryToUser: MmMapLockedPagesSpecifyCache failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    FdoContext->SharedMemoryUserVa = userVa;
    *UserAddress = userVa;
    
    TRACE_INFO("VnvmeMapSharedMemoryToUser: Mapped to user VA=%p", userVa);
    return STATUS_SUCCESS;
}

/**
 * @brief 取消用户空间映射
 */
VOID
VnvmeUnmapSharedMemoryFromUser(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    if (FdoContext->SharedMemoryUserVa != NULL && 
        FdoContext->SharedMemoryMdl != NULL) {
        
        TRACE_INFO("VnvmeUnmapSharedMemoryFromUser: Unmapping user VA=%p",
                   FdoContext->SharedMemoryUserVa);
        
        MmUnmapLockedPages(
            FdoContext->SharedMemoryUserVa,
            FdoContext->SharedMemoryMdl
            );
        
        FdoContext->SharedMemoryUserVa = NULL;
    }
}
