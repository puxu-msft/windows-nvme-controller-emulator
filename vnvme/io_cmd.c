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

/**
 * @brief 构造完成状态
 */
static UINT16 MakeIoStatus(UINT8 sct, UINT8 sc, UINT8 phase)
{
    return (UINT16)(phase | (sc << 1) | (sct << 9));
}

/**
 * @brief 发送 I/O 完成
 */
static NTSTATUS PostIoCompletion(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ UINT16 Cid,
    _In_ UINT8 Sct,
    _In_ UINT8 Sc
    )
{
    NVME_COMPLETION completion = {0};
    UINT8 phase;
    
    // 获取当前 CQ 的 phase tag
    if (QueueId == 0) {
        phase = (UINT8)PdoContext->AdminCqPhase;
    } else if (QueueId <= VNVME_MAX_IO_QUEUES && PdoContext->IoCq[QueueId - 1].Created) {
        phase = PdoContext->IoCq[QueueId - 1].PhaseTag ? 1 : 0;
    } else {
        return STATUS_INVALID_PARAMETER;
    }
    
    completion.CID = Cid;
    completion.DW0 = 0;
    completion.SQID = QueueId;
    completion.SQHD = 0;  // TODO: 正确的 SQ Head
    completion.Status = MakeIoStatus(Sct, Sc, phase);
    
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
static NTSTATUS HandleRead(
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
    ULONGLONG bytesRemaining;
    ULONGLONG bufferOffset;
    
    // 解析命令参数
    slba = ((ULONGLONG)Command->CDW11 << 32) | Command->CDW10;
    nlb = (USHORT)(Command->CDW12 & 0xFFFF) + 1;
    
    TRACE_INFO("HandleRead: QID=%u, CID=%u, NSID=%u, SLBA=%llu, NLB=%u",
               QueueId, Command->CID, nsid, slba, nlb);
    
    // 验证 NSID
    if (nsid == 0 || nsid > VNVME_MAX_NAMESPACES) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[nsid - 1];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = (ULONGLONG)nlb * blockSize;
    byteOffset = slba * blockSize;
    
    // 验证 LBA 范围
    if (slba + nlb > ns->TotalBlocks) {
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
        
        readBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)totalBytes, VNVME_POOL_TAG);
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
        readBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)totalBytes, VNVME_POOL_TAG);
        if (readBuffer == NULL) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INTERNAL_ERROR);
        }
        RtlZeroMemory(readBuffer, (SIZE_T)totalBytes);
        directPtr = readBuffer;
    }

WriteToHost:
    // 将数据写入 PRP 指定的主机内存
    bytesRemaining = totalBytes;
    bufferOffset = 0;
    
    // 处理 PRP1
    if (bytesRemaining > 0 && Command->PRP1 != 0) {
        prpPhys.QuadPart = Command->PRP1;
        mapSize = (bytesRemaining > PAGE_SIZE) ? PAGE_SIZE : (SIZE_T)bytesRemaining;
        
        prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READWRITE | PAGE_NOCACHE);
        if (prpVa != NULL) {
            RtlCopyMemory(prpVa, (PUCHAR)directPtr + bufferOffset, mapSize);
            MmUnmapIoSpace(prpVa, mapSize);
            bytesRemaining -= mapSize;
            bufferOffset += mapSize;
        }
    }
    
    // 处理 PRP2 (如果数据超过一页)
    if (bytesRemaining > 0 && Command->PRP2 != 0) {
        prpPhys.QuadPart = Command->PRP2;
        mapSize = (bytesRemaining > PAGE_SIZE) ? PAGE_SIZE : (SIZE_T)bytesRemaining;
        
        prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READWRITE | PAGE_NOCACHE);
        if (prpVa != NULL) {
            RtlCopyMemory(prpVa, (PUCHAR)directPtr + bufferOffset, mapSize);
            MmUnmapIoSpace(prpVa, mapSize);
            bytesRemaining -= mapSize;
            bufferOffset += mapSize;
        }
    }
    
    // TODO: 处理 PRP List (超过 2 页的情况)
    
    // 释放临时缓冲区
    if (readBuffer != NULL) {
        ExFreePoolWithTag(readBuffer, VNVME_POOL_TAG);
    }
    
    // 更新统计
    InterlockedAdd64(&PdoContext->BytesRead, (LONG64)totalBytes);
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
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
static NTSTATUS HandleWrite(
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
    ULONGLONG bytesRemaining;
    ULONGLONG bufferOffset;
    
    slba = ((ULONGLONG)Command->CDW11 << 32) | Command->CDW10;
    nlb = (USHORT)(Command->CDW12 & 0xFFFF) + 1;
    
    TRACE_INFO("HandleWrite: QID=%u, CID=%u, NSID=%u, SLBA=%llu, NLB=%u",
               QueueId, Command->CID, nsid, slba, nlb);
    
    // 验证 NSID
    if (nsid == 0 || nsid > VNVME_MAX_NAMESPACES) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[nsid - 1];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = (ULONGLONG)nlb * blockSize;
    byteOffset = slba * blockSize;
    
    // 验证 LBA 范围
    if (slba + nlb > ns->TotalBlocks) {
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
            
            writeBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)totalBytes, VNVME_POOL_TAG);
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
        
        writeBuffer = ExAllocatePool2(POOL_FLAG_NON_PAGED, (SIZE_T)totalBytes, VNVME_POOL_TAG);
        if (writeBuffer == NULL) {
            // 即使分配失败，也返回成功 (丢弃数据)
            goto Success;
        }
        directPtr = writeBuffer;
    }
    
    // 从 PRP 指定的主机内存读取数据
    bytesRemaining = totalBytes;
    bufferOffset = 0;
    
    // 处理 PRP1
    if (bytesRemaining > 0 && Command->PRP1 != 0) {
        prpPhys.QuadPart = Command->PRP1;
        mapSize = (bytesRemaining > PAGE_SIZE) ? PAGE_SIZE : (SIZE_T)bytesRemaining;
        
        prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READONLY | PAGE_NOCACHE);
        if (prpVa != NULL) {
            RtlCopyMemory((PUCHAR)directPtr + bufferOffset, prpVa, mapSize);
            MmUnmapIoSpace(prpVa, mapSize);
            bytesRemaining -= mapSize;
            bufferOffset += mapSize;
        }
    }
    
    // 处理 PRP2 (如果数据超过一页)
    if (bytesRemaining > 0 && Command->PRP2 != 0) {
        prpPhys.QuadPart = Command->PRP2;
        mapSize = (bytesRemaining > PAGE_SIZE) ? PAGE_SIZE : (SIZE_T)bytesRemaining;
        
        prpVa = MmMapIoSpaceEx(prpPhys, mapSize, PAGE_READONLY | PAGE_NOCACHE);
        if (prpVa != NULL) {
            RtlCopyMemory((PUCHAR)directPtr + bufferOffset, prpVa, mapSize);
            MmUnmapIoSpace(prpVa, mapSize);
            bytesRemaining -= mapSize;
            bufferOffset += mapSize;
        }
    }
    
    // TODO: 处理 PRP List (超过 2 页的情况)
    
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
    // 更新统计
    InterlockedAdd64(&PdoContext->BytesWritten, (LONG64)totalBytes);
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
    return PostIoCompletion(PdoContext, QueueId, Command->CID,
                            NVME_SCT_GENERIC, NVME_SC_SUCCESS);
}

//===========================================================================
// Flush 命令处理
//===========================================================================

/**
 * @brief 处理 Flush 命令
 */
static NTSTATUS HandleFlush(
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
            }
        }
    } else {
        if (nsid == 0 || nsid > VNVME_MAX_NAMESPACES) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
        }
        
        if (!PdoContext->Namespaces[nsid - 1].Active) {
            return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                    NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
        }
        
        // 刷新指定命名空间
        if (PdoContext->Namespaces[nsid - 1].Storage != NULL) {
            VnvmeStorageFlush(PdoContext->Namespaces[nsid - 1].Storage);
        }
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
static NTSTATUS HandleWriteZeroes(
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
    
    if (nsid == 0 || nsid > VNVME_MAX_NAMESPACES) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    ns = &PdoContext->Namespaces[nsid - 1];
    
    if (!ns->Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    blockSize = ns->BlockSize;
    totalBytes = (ULONGLONG)nlb * blockSize;
    byteOffset = slba * blockSize;
    
    if (slba + nlb > ns->TotalBlocks) {
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
 */
static NTSTATUS HandleDatasetManagement(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ PNVME_COMMAND Command
    )
{
    ULONG nsid = Command->NSID;
    ULONG nr;           // Number of Ranges (0's based)
    BOOLEAN deallocate; // Attribute - Deallocate (TRIM)
    
    nr = (Command->CDW10 & 0xFF) + 1;
    deallocate = (Command->CDW11 & 0x04) != 0;
    
    TRACE_INFO("HandleDatasetManagement: QID=%u, CID=%u, NSID=%u, NR=%u, Deallocate=%u",
               QueueId, Command->CID, nsid, nr, deallocate);
    
    if (nsid == 0 || nsid > VNVME_MAX_NAMESPACES) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    if (!PdoContext->Namespaces[nsid - 1].Active) {
        return PostIoCompletion(PdoContext, QueueId, Command->CID,
                                NVME_SCT_GENERIC, NVME_SC_INVALID_NAMESPACE);
    }
    
    // TODO Phase 4: 解析范围描述符并处理 TRIM
    
    InterlockedIncrement64(&PdoContext->IoCommandsProcessed);
    
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
            // TODO: 实现 Compare 命令
            TRACE_WARN("VnvmeProcessIoCommand: Compare not implemented");
            status = PostIoCompletion(PdoContext, QueueId, Command->CID,
                                      NVME_SCT_GENERIC, NVME_SC_INVALID_OPCODE);
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
