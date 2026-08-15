/**
 * @file io_cmd.c
 * @brief I/O 命令处理
 * 
 * 实现 NVMe I/O 命令的处理，包括:
 * - Read
 * - Write
 * - Flush
 * - Write Zeroes
 * - Dataset Management (TRIM)
 */

#include "vnvme.h"

//===========================================================================
// 辅助函数
//===========================================================================

// MakeIoStatus 已移至 vnvme_utils.h 作为 NvmeMakeStatus()

/**
 * @brief 发送 I/O 完成
 */
static NTSTATUS
PostIoCompletion(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ UINT16 Cid,
    _In_ UINT8 Sct,
    _In_ UINT8 Sc
    )
{
    NVME_COMPLETION completion = {0};
    UINT8 phase;
    USHORT sqHead = 0;
    
    // 获取当前 CQ 的 phase tag 和 SQ Head
    if (QueueId == 0) {
        phase = (UINT8)PdoContext->AdminCqPhase;
        sqHead = (USHORT)PdoContext->AdminSq.Head;
    } else if (QueueId <= VNVME_MAX_IO_QUEUES && PdoContext->IoCq[QueueId - 1].Created) {
        phase = PdoContext->IoCq[QueueId - 1].PhaseTag ? 1 : 0;
        sqHead = (USHORT)PdoContext->IoSq[QueueId - 1].Head;
    } else {
        return STATUS_INVALID_PARAMETER;
    }
    
    completion.CID = Cid;
    completion.DW0 = 0;
    completion.SQID = QueueId;
    completion.SQHD = sqHead;
    completion.Status = NvmeMakeStatus(Sct, Sc, phase);
    
    return VnvmePostCompletion(PdoContext, QueueId, &completion);
}

//===========================================================================
// Read 命令处理
//===========================================================================

/**
 * @brief 处理 Read 命令
 * 
 * 零复制架构: 从后端存储读取数据，写入 PRP 指定的主机内存。
 * 
 * 数据流:
 * 1. 验证 NSID 和 LBA 范围
 * 2. 从存储后端读取数据到临时缓冲区
 * 3. 解析 PRP 列表，将数据写入主机内存
 */
static NTSTATUS
HandleRead(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    ULONGLONG slba;      // Starting LBA
    USHORT nlb;          // Number of Logical Blocks (0's based)
    ULONG blockSize;
    ULONGLONG totalBytes;
    ULONGLONG byteOffset;
    PVNVME_NAMESPACE ns;
    NTSTATUS status;
    PVOID readBuffer = NULL;
    PVOID directPtr = NULL;
    PHYSICAL_ADDRESS prpPhys;
    PVOID prpVa;
    SIZE_T mapSize;
    ULONGLONG bufferOffset;
    
    // 解析命令参数
    slba = ((ULONGLONG)Command->CDW11 << 32) | Command->CDW10;
    nlb = (USHORT)(Command->CDW12 & 0xFFFF) + 1;
    
    TRACE_INFO("HandleRead: QID=%u, CID=%u, NSID=%u, SLBA=%llu, NLB=%u",
               QueueId, Command->CID, nsid, slba, nlb);
    
    // 验证 NSID (使用公共验证宏)
    if (!VNVME_NSID_VALID(nsid)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = VNVME_BLOCKS_TO_BYTES(nlb, blockSize);
    byteOffset = VNVME_LBA_TO_BYTES(slba, blockSize);
    
    // 验证 LBA 范围 (使用公共验证宏)
    if (!VNVME_LBA_RANGE_VALID(slba, nlb, ns->TotalBlocks)) {
        TRACE_WARN("HandleRead: LBA out of range (SLBA=%llu + NLB=%u > Total=%llu)",
                   slba, nlb, ns->TotalBlocks);
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
    }
    
    // 尝试直接访问 (仅内存后端)
    if (ns->Storage != NULL) {
        status = VnvmeStorageGetDirect(ns->Storage, byteOffset, (ULONG)totalBytes, &directPtr);
        if (NT_SUCCESS(status) && directPtr != NULL) {
            // 直接从内存后端写入 PRP
            goto WriteToHost;
        }
        
        // 分配临时读取缓冲区
        if (totalBytes > 2 * 1024 * 1024) {
            // 限制单次读取大小 (2 MB)
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
        }
        
        readBuffer = VNVME_ALLOC_POOL(NonPagedPoolNx, (SIZE_T)totalBytes);
        if (readBuffer == NULL) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
        }
        
        status = VnvmeStorageRead(ns->Storage, byteOffset, readBuffer, (ULONG)totalBytes);
        if (!NT_SUCCESS(status)) {
            ExFreePoolWithTag(readBuffer, VNVME_POOL_TAG);
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_MEDIA_ERROR, NVME_SC_UNRECOVERED_READ_ERROR);
        }
        
        directPtr = readBuffer;
    } else {
        // 无存储后端: 返回全零
        readBuffer = VNVME_ALLOC_POOL(NonPagedPoolNx, (SIZE_T)totalBytes);
        if (readBuffer == NULL) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
        }
        RtlZeroMemory(readBuffer, (SIZE_T)totalBytes);
        directPtr = readBuffer;
    }

WriteToHost:
    // 将数据写入 PRP 指定的主机内存 (使用 PRP 列表解析)
    {
        PVNVME_PRP_ENTRY prpEntries = NULL;
        ULONG prpEntryCount = 0;
        ULONG i;
        
        status = VnvmeParsePrpList(Command->PRP1, Command->PRP2, (ULONG)totalBytes,
                                   &prpEntries, &prpEntryCount);
        if (!NT_SUCCESS(status)) {
            if (readBuffer != NULL) {
                ExFreePoolWithTag(readBuffer, VNVME_POOL_TAG);
            }
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
        }
        
        bufferOffset = 0;
        for (i = 0; i < prpEntryCount && bufferOffset < totalBytes; i++) {
            prpPhys.QuadPart = prpEntries[i].PhysicalAddress + prpEntries[i].Offset;
            mapSize = prpEntries[i].Length;
            
            if (mapSize > totalBytes - bufferOffset) {
                mapSize = (SIZE_T)(totalBytes - bufferOffset);
            }
            
            prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READWRITE | PAGE_NOCACHE);
            if (prpVa != NULL) {
                RtlCopyMemory(prpVa, (PUCHAR)directPtr + bufferOffset, mapSize);
                MmUnmapIoSpace(prpVa, mapSize);
                bufferOffset += mapSize;
            }
        }
        
        VnvmeFreePrpEntries(prpEntries);
    }
    
    // 释放临时缓冲区
    if (readBuffer != NULL) {
        ExFreePoolWithTag(readBuffer, VNVME_POOL_TAG);
    }
    
    // 更新统计 (控制器级别)
    InterlockedAdd64(&PdoContext->BytesRead, (LONG64)totalBytes);
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    // 更新统计 (命名空间级别)
    InterlockedAdd64(&ns->ReadBytes, (LONG64)totalBytes);
    InterlockedIncrement64(&ns->ReadCommands);
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// Write 命令处理
//===========================================================================

/**
 * @brief 处理 Write 命令
 * 
 * 数据流:
 * 1. 验证 NSID 和 LBA 范围
 * 2. 解析 PRP 列表，从主机内存读取数据
 * 3. 将数据写入存储后端
 */
static NTSTATUS
HandleWrite(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    ULONGLONG slba;
    USHORT nlb;
    ULONG blockSize;
    ULONGLONG totalBytes;
    ULONGLONG byteOffset;
    PVNVME_NAMESPACE ns;
    NTSTATUS status;
    PVOID writeBuffer = NULL;
    PVOID directPtr = NULL;
    PHYSICAL_ADDRESS prpPhys;
    PVOID prpVa;
    SIZE_T mapSize;
    ULONGLONG bufferOffset;
    
    slba = ((ULONGLONG)Command->CDW11 << 32) | Command->CDW10;
    nlb = (USHORT)(Command->CDW12 & 0xFFFF) + 1;
    
    TRACE_INFO("HandleWrite: QID=%u, CID=%u, NSID=%u, SLBA=%llu, NLB=%u",
               QueueId, Command->CID, nsid, slba, nlb);
    
    // 验证 NSID (使用公共验证宏)
    if (!VNVME_NSID_VALID(nsid)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = VNVME_BLOCKS_TO_BYTES(nlb, blockSize);
    byteOffset = VNVME_LBA_TO_BYTES(slba, blockSize);
    
    // 验证 LBA 范围 (使用公共验证宏)
    if (!VNVME_LBA_RANGE_VALID(slba, nlb, ns->TotalBlocks)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
    }
    
    // 尝试直接访问 (仅内存后端)
    if (ns->Storage != NULL) {
        status = VnvmeStorageGetDirect(ns->Storage, byteOffset, (ULONG)totalBytes, &directPtr);
        if (!NT_SUCCESS(status) || directPtr == NULL) {
            // 分配临时写入缓冲区
            if (totalBytes > 2 * 1024 * 1024) {
                return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                        NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
            }
            
            writeBuffer = VNVME_ALLOC_POOL(NonPagedPoolNx, (SIZE_T)totalBytes);
            if (writeBuffer == NULL) {
                return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                        NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
            }
            directPtr = writeBuffer;
        }
    } else {
        // 无存储后端: 分配缓冲区以丢弃数据
        if (totalBytes > 2 * 1024 * 1024) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
        }
        
        writeBuffer = VNVME_ALLOC_POOL(NonPagedPoolNx, (SIZE_T)totalBytes);
        if (writeBuffer == NULL) {
            // 即使分配失败，也返回成功 (丢弃数据)
            goto Success;
        }
        directPtr = writeBuffer;
    }
    
    // 从 PRP 指定的主机内存读取数据 (使用 PRP 列表解析)
    {
        PVNVME_PRP_ENTRY prpEntries = NULL;
        ULONG prpEntryCount = 0;
        ULONG i;
        
        status = VnvmeParsePrpList(Command->PRP1, Command->PRP2, (ULONG)totalBytes,
                                   &prpEntries, &prpEntryCount);
        if (!NT_SUCCESS(status)) {
            if (writeBuffer != NULL) {
                ExFreePoolWithTag(writeBuffer, VNVME_POOL_TAG);
            }
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
        }
        
        bufferOffset = 0;
        for (i = 0; i < prpEntryCount && bufferOffset < totalBytes; i++) {
            prpPhys.QuadPart = prpEntries[i].PhysicalAddress + prpEntries[i].Offset;
            mapSize = prpEntries[i].Length;
            
            if (mapSize > totalBytes - bufferOffset) {
                mapSize = (SIZE_T)(totalBytes - bufferOffset);
            }
            
            prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READONLY | PAGE_NOCACHE);
            if (prpVa != NULL) {
                RtlCopyMemory((PUCHAR)directPtr + bufferOffset, prpVa, mapSize);
                MmUnmapIoSpace(prpVa, mapSize);
                bufferOffset += mapSize;
            }
        }
        
        VnvmeFreePrpEntries(prpEntries);
    }
    
    // 写入存储后端 (如果使用临时缓冲区)
    if (ns->Storage != NULL && writeBuffer != NULL) {
        status = VnvmeStorageWrite(ns->Storage, byteOffset, writeBuffer, (ULONG)totalBytes);
        if (!NT_SUCCESS(status)) {
            ExFreePoolWithTag(writeBuffer, VNVME_POOL_TAG);
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_MEDIA_ERROR, NVME_SC_WRITE_FAULT);
        }
    }
    
    // 释放临时缓冲区 (如果有)
    if (writeBuffer != NULL) {
        ExFreePoolWithTag(writeBuffer, VNVME_POOL_TAG);
    }

Success:
    // 更新统计 (控制器级别)
    InterlockedAdd64(&PdoContext->BytesWritten, (LONG64)totalBytes);
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    // 更新统计 (命名空间级别)
    InterlockedAdd64(&ns->WriteBytes, (LONG64)totalBytes);
    InterlockedIncrement64(&ns->WriteCommands);
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// Flush 命令处理
//===========================================================================

/**
 * @brief 处理 Flush 命令
 */
static NTSTATUS
HandleFlush(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    USHORT i;
    
    TRACE_INFO("HandleFlush: QID=%u, CID=%u, NSID=%u", QueueId, Command->CID, nsid);
    
    // NSID = 0xFFFFFFFF 表示刷新所有命名空间
    if (nsid == 0xFFFFFFFF) {
        // 刷新所有活动的命名空间
        for (i = 0; i < VNVME_MAX_NAMESPACES; i++) {
            if (PdoContext->Namespaces[i].Active && 
                PdoContext->Namespaces[i].Storage != NULL) {
                VnvmeStorageFlush(PdoContext->Namespaces[i].Storage);
                InterlockedIncrement64(&PdoContext->Namespaces[i].FlushCommands);
            }
        }
    } else {
        if (!VNVME_NSID_VALID(nsid)) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
        }
        
        if (!PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)].Active) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
        }
        
        // 刷新指定命名空间
        if (PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)].Storage != NULL) {
            VnvmeStorageFlush(PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)].Storage);
        }
        InterlockedIncrement64(&PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)].FlushCommands);
    }
    
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// Write Zeroes 命令处理
//===========================================================================

/**
 * @brief 处理 Write Zeroes 命令
 */
static NTSTATUS
HandleWriteZeroes(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    ULONGLONG slba;
    USHORT nlb;
    ULONG blockSize;
    ULONGLONG totalBytes;
    ULONGLONG byteOffset;
    PVNVME_NAMESPACE ns;
    NTSTATUS status;
    
    slba = ((ULONGLONG)Command->CDW11 << 32) | Command->CDW10;
    nlb = (USHORT)(Command->CDW12 & 0xFFFF) + 1;
    
    TRACE_INFO("HandleWriteZeroes: QID=%u, CID=%u, NSID=%u, SLBA=%llu, NLB=%u",
               QueueId, Command->CID, nsid, slba, nlb);
    
    if (!VNVME_NSID_VALID(nsid)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = VNVME_BLOCKS_TO_BYTES(nlb, blockSize);
    byteOffset = VNVME_LBA_TO_BYTES(slba, blockSize);
    
    if (!VNVME_LBA_RANGE_VALID(slba, nlb, ns->TotalBlocks)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
    }
    
    // 调用存储后端写零
    if (ns->Storage != NULL) {
        // 分块写零以避免大内存分配
        #define WRITE_ZEROES_CHUNK_SIZE (1 * 1024 * 1024)  // 1 MB
        
        ULONGLONG remaining = totalBytes;
        ULONGLONG offset = byteOffset;
        
        while (remaining > 0) {
            ULONG chunkSize = (remaining > WRITE_ZEROES_CHUNK_SIZE) 
                              ? WRITE_ZEROES_CHUNK_SIZE 
                              : (ULONG)remaining;
            
            status = VnvmeStorageWriteZeroes(ns->Storage, offset, chunkSize);
            if (!NT_SUCCESS(status)) {
                return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                        NVME_SCT_MEDIA_ERROR, NVME_SC_WRITE_FAULT);
            }
            
            offset += chunkSize;
            remaining -= chunkSize;
        }
    }
    
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// Dataset Management (TRIM) 命令处理
//===========================================================================

/**
 * @brief 处理 Dataset Management 命令
 * 
 * 解析范围描述符并对每个范围执行 TRIM (写零) 操作。
 */
static NTSTATUS
HandleDatasetManagement(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    ULONG nr;           // Number of Ranges (0's based)
    BOOLEAN deallocate; // Attribute - Deallocate (TRIM)
    PVNVME_NAMESPACE ns;
    PHYSICAL_ADDRESS prp1Phys;
    PNVME_DSM_RANGE ranges = NULL;
    SIZE_T rangeBufferSize;
    ULONG i;
    NTSTATUS status;
    
    nr = (Command->CDW10 & 0xFF) + 1;
    deallocate = (Command->CDW11 & 0x04) != 0;
    
    TRACE_INFO("HandleDatasetManagement: QID=%u, CID=%u, NSID=%u, NR=%u, Deallocate=%u",
               QueueId, Command->CID, nsid, nr, deallocate);
    
    if (!VNVME_NSID_VALID(nsid)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    // 如果不是 Deallocate 操作，直接返回成功
    if (!deallocate) {
        InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_SUCCESS);
    }
    
    // 限制范围数量 (最多 256 个范围 = 4KB)
    if (nr > 256) {
        nr = 256;
    }
    
    rangeBufferSize = nr * sizeof(NVME_DSM_RANGE);
    
    // 映射范围描述符数据 (从 PRP1)
    prp1Phys.QuadPart = Command->PRP1;
    ranges = (PNVME_DSM_RANGE)MmMapIoSpaceEx(prp1Phys, rangeBufferSize, 
                                              PAGE_READONLY | PAGE_NOCACHE);
    if (ranges == NULL) {
        TRACE_WARN("HandleDatasetManagement: Failed to map range descriptors");
        // 仍然返回成功 - TRIM 是提示性操作
        InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_SUCCESS);
    }
    
    // 处理每个范围
    for (i = 0; i < nr; i++) {
        ULONGLONG slba = ranges[i].StartingLBA;
        ULONG nlb = ranges[i].LengthInLogicalBlocks;
        ULONGLONG byteOffset;
        ULONGLONG byteLength;
        
        // 跳过零长度范围
        if (nlb == 0) {
            continue;
        }
        
        // 验证 LBA 范围
        if (slba + nlb > ns->TotalBlocks) {
            TRACE_WARN("HandleDatasetManagement: Range %u out of bounds", i);
            continue;  // 跳过无效范围，继续处理其他范围
        }
        
        byteOffset = slba * ns->BlockSize;
        byteLength = (ULONGLONG)nlb * ns->BlockSize;
        
        // 对存储后端执行写零操作 (等效于 TRIM)
        if (ns->Storage != NULL) {
            status = VnvmeStorageWriteZeroes(ns->Storage, byteOffset, (ULONG)byteLength);
            if (!NT_SUCCESS(status)) {
                TRACE_WARN("HandleDatasetManagement: WriteZeroes failed for range %u", i);
                // 继续处理其他范围
            }
        }
        
        TRACE_VERBOSE("HandleDatasetManagement: TRIM range %u: LBA=%llu, Blocks=%u",
                      i, slba, nlb);
    }
    
    MmUnmapIoSpace(ranges, rangeBufferSize);
    
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// Compare 命令处理
//===========================================================================

/**
 * @brief 处理 Compare 命令
 * 
 * Compare 命令将主机提供的数据与存储介质上的数据进行比较。
 * 如果数据匹配，返回成功；如果不匹配，返回比较失败状态。
 * 
 * @param PdoContext PDO 上下文
 * @param QueueId I/O 队列 ID
 * @param Command NVMe 命令
 * @return NTSTATUS 状态码
 */
static NTSTATUS
HandleCompare(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    ULONGLONG slba;
    USHORT nlb;
    ULONG blockSize;
    ULONGLONG totalBytes;
    ULONGLONG byteOffset;
    PVNVME_NAMESPACE ns;
    NTSTATUS status;
    PVOID compareBuffer = NULL;     // 主机提供的数据
    PVOID storageBuffer = NULL;     // 存储介质上的数据
    PVNVME_PRP_ENTRY prpEntries = NULL;
    ULONG prpEntryCount = 0;
    ULONG i;
    ULONGLONG bufferOffset;
    BOOLEAN mismatch = FALSE;
    
    // 解析命令参数
    slba = ((ULONGLONG)Command->CDW11 << 32) | Command->CDW10;
    nlb = (USHORT)(Command->CDW12 & 0xFFFF) + 1;
    
    TRACE_INFO("HandleCompare: QID=%u, CID=%u, NSID=%u, SLBA=%llu, NLB=%u",
               QueueId, Command->CID, nsid, slba, nlb);
    
    // 验证 NSID (使用公共验证宏)
    if (!VNVME_NSID_VALID(nsid)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[VNVME_NSID_TO_INDEX(nsid)];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = VNVME_BLOCKS_TO_BYTES(nlb, blockSize);
    byteOffset = VNVME_LBA_TO_BYTES(slba, blockSize);
    
    // 验证 LBA 范围 (使用公共验证宏)
    if (!VNVME_LBA_RANGE_VALID(slba, nlb, ns->TotalBlocks)) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_LBA_OUT_OF_RANGE);
    }
    
    // 限制单次比较大小
    if (totalBytes > 2 * 1024 * 1024) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_FIELD);
    }
    
    // 分配缓冲区 (使用统一宏)
    compareBuffer = VNVME_ALLOC_POOL(NonPagedPoolNx, (SIZE_T)totalBytes);
    if (compareBuffer == NULL) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
    }
    
    storageBuffer = VNVME_ALLOC_POOL(NonPagedPoolNx, (SIZE_T)totalBytes);
    if (storageBuffer == NULL) {
        ExFreePoolWithTag(compareBuffer, VNVME_POOL_TAG);
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
    }
    
    // 解析 PRP 列表
    status = VnvmeParsePrpList(Command->PRP1, Command->PRP2, (ULONG)totalBytes,
                               &prpEntries, &prpEntryCount);
    if (!NT_SUCCESS(status)) {
        ExFreePoolWithTag(compareBuffer, VNVME_POOL_TAG);
        ExFreePoolWithTag(storageBuffer, VNVME_POOL_TAG);
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
    }
    
    // 从 PRP 读取主机数据
    bufferOffset = 0;
    for (i = 0; i < prpEntryCount && bufferOffset < totalBytes; i++) {
        PHYSICAL_ADDRESS prpPhys;
        PVOID prpVa;
        SIZE_T mapSize;
        
        prpPhys.QuadPart = prpEntries[i].PhysicalAddress + prpEntries[i].Offset;
        mapSize = prpEntries[i].Length;
        
        if (mapSize > totalBytes - bufferOffset) {
            mapSize = (SIZE_T)(totalBytes - bufferOffset);
        }
        
        prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READONLY | PAGE_NOCACHE);
        if (prpVa != NULL) {
            RtlCopyMemory((PUCHAR)compareBuffer + bufferOffset, prpVa, mapSize);
            MmUnmapIoSpace(prpVa, mapSize);
            bufferOffset += mapSize;
        }
    }
    
    VnvmeFreePrpEntries(prpEntries);
    
    // 从存储后端读取数据
    if (ns->Storage != NULL) {
        status = VnvmeStorageRead(ns->Storage, byteOffset, storageBuffer, (ULONG)totalBytes);
        if (!NT_SUCCESS(status)) {
            ExFreePoolWithTag(compareBuffer, VNVME_POOL_TAG);
            ExFreePoolWithTag(storageBuffer, VNVME_POOL_TAG);
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_MEDIA_ERROR, NVME_SC_UNRECOVERED_READ_ERROR);
        }
    } else {
        // 无存储后端：假设存储全零
        RtlZeroMemory(storageBuffer, (SIZE_T)totalBytes);
    }
    
    // 比较数据
    if (RtlCompareMemory(compareBuffer, storageBuffer, (SIZE_T)totalBytes) != totalBytes) {
        mismatch = TRUE;
    }
    
    ExFreePoolWithTag(compareBuffer, VNVME_POOL_TAG);
    ExFreePoolWithTag(storageBuffer, VNVME_POOL_TAG);
    
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    if (mismatch) {
        // NVMe 规范定义 Compare Failure 状态码
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_MEDIA_ERROR, NVME_SC_COMPARE_FAILURE);
    }
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// I/O 命令分发
//===========================================================================

/**
 * @brief 处理 I/O 命令
 * 
 * @param PdoContext PDO 上下文
 * @param QueueId I/O 队列 ID (1-based)
 * @param Command 命令指针
 * @return NTSTATUS 状态码
 */
NTSTATUS
VnvmeProcessIoCommand(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    NTSTATUS status;
    
    TRACE_VERBOSE("VnvmeProcessIoCommand: OPC=0x%02X, QID=%u, CID=%u, NSID=%u",
                  Command->OPC, QueueId, Command->CID, Command->NSID);
    
    switch (Command->OPC) {
        case NVME_IO_READ:
            status = HandleRead(PdoContext, QueueId, Command);
            break;
            
        case NVME_IO_WRITE:
            status = HandleWrite(PdoContext, QueueId, Command);
            break;
            
        case NVME_IO_FLUSH:
            status = HandleFlush(PdoContext, QueueId, Command);
            break;
            
        case NVME_IO_WRITE_ZEROES:
            status = HandleWriteZeroes(PdoContext, QueueId, Command);
            break;
            
        case NVME_IO_DATASET_MANAGEMENT:
            status = HandleDatasetManagement(PdoContext, QueueId, Command);
            break;
            
        case NVME_IO_COMPARE:
            status = HandleCompare(PdoContext, QueueId, Command);
            break;
            
        default:
            TRACE_WARN("VnvmeProcessIoCommand: Unknown opcode 0x%02X", Command->OPC);
            status = PostIoCompletion(PdoContext, QueueId, Command->CID,
                                      NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
            break;
    }
    
    return status;
}

//===========================================================================
// 批量命令处理 (内核模式)
//===========================================================================

/**
 * @brief 处理 I/O 队列中所有待处理命令
 * 
 * 从上次处理的 Tail 到新 Tail 之间的所有命令逐个处理。
 * 
 * @param QueueId I/O 队列 ID (1-based)
 */
VOID
VnvmeProcessIoCommands(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ ULONG NewTail
    )
{
    USHORT queueIndex;
    ULONG head;
    ULONG tail;
    ULONG queueSize;
    NTSTATUS status;
    NVME_COMMAND command;
    
    if (QueueId == 0 || QueueId > PdoContext->IoQueueCount) {
        TRACE_ERROR("VnvmeProcessIoCommands: Invalid queue ID %u", QueueId);
        return;
    }
    
    queueIndex = QueueId - 1;
    head = PdoContext->IoSq[queueIndex].Head;
    tail = NewTail;
    queueSize = PdoContext->IoSq[queueIndex].Size;
    
    if (queueSize == 0 || !PdoContext->IoSq[queueIndex].Created) {
        TRACE_WARN("VnvmeProcessIoCommands: Queue %u not initialized", QueueId);
        return;
    }
    
    // 处理从 head 到 tail 的所有命令
    while (head != tail) {
        // 获取命令
        status = VnvmeFetchCommand(PdoContext, QueueId, &command);
        if (!NT_SUCCESS(status)) {
            TRACE_ERROR("VnvmeProcessIoCommands: VnvmeFetchCommand failed 0x%08X", status);
            break;
        }
        
        // 处理命令
        VnvmeProcessIoCommand(PdoContext, QueueId, &command);
        
        // 更新 head
        head = (head + 1) % queueSize;
        PdoContext->IoSq[queueIndex].Head = head;
        
        // 更新统计
        InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    }
}
