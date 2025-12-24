/**
 * @file user_forward.c
 * @brief 用户态命令转发
 * 
 * 实现命令从内核到用户态的转发机制:
 * 1. 检测 NVMe 命令队列变化
 * 2. 通过通知环 (Notify Ring) 通知用户态
 * 3. 用户态直接从共享内存读取原始 NVME_COMMAND
 * 4. 用户态处理后将完成写入 CQ
 * 5. 内核触发中断 (如果启用)
 */

#include "vnvme.h"

//===========================================================================
// 辅助函数
//===========================================================================

/**
 * @brief 获取共享内存控制块
 */
static PVNVME_SHM_CONTROL_BLOCK
GetShmControlBlock(void)
{
    if (g_FdoContext == NULL || g_FdoContext->ShmKernelVirtAddr == NULL) {
        return NULL;
    }
    return (PVNVME_SHM_CONTROL_BLOCK)g_FdoContext->ShmKernelVirtAddr;
}

/**
 * @brief 获取通知环
 */
static PVNVME_NOTIFY_RING
GetNotifyRing(void)
{
    PVNVME_SHM_CONTROL_BLOCK shm = GetShmControlBlock();
    if (shm == NULL || shm->NotifyRingOffset == 0) {
        return NULL;
    }
    return (PVNVME_NOTIFY_RING)((PUCHAR)shm + shm->NotifyRingOffset);
}

/**
 * @brief 向通知环添加条目
 * 
 * @param QueueId 队列 ID (0 = Admin)
 * @param Type 通知类型 (0 = SQ 有新命令, 1 = CQ 已更新)
 * @param Index Doorbell 值
 * @return TRUE 成功，FALSE 环已满
 */
static BOOLEAN
NotifyRingPush(
    _In_ USHORT QueueId,
    _In_ USHORT Type,
    _In_ ULONG Index
    )
{
    PVNVME_NOTIFY_RING ring = GetNotifyRing();
    ULONG tail;
    ULONG nextTail;
    
    if (ring == NULL) {
        return FALSE;
    }
    
    tail = ring->Tail;
    nextTail = (tail + 1) % ring->Size;
    
    // 检查是否满
    if (nextTail == ring->Head) {
        TRACE_WARN("NotifyRingPush: Ring full, dropping notification");
        return FALSE;
    }
    
    // 写入条目
    ring->Entries[tail].QueueId = QueueId;
    ring->Entries[tail].Type = Type;
    ring->Entries[tail].Index = Index;
    
    // 内存屏障，确保数据写入后再更新 Tail
    KeMemoryBarrier();
    
    ring->Tail = nextTail;
    
    return TRUE;
}

/**
 * @brief 唤醒用户态服务 (内部使用)
 */
static VOID
SignalUserMode(void)
{
    if (g_FdoContext != NULL && g_FdoContext->UserReady) {
        KeSetEvent(&g_FdoContext->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
    }
}

/**
 * @brief 通知用户态有新命令就绪 (公开 API)
 * 
 * 当有新命令到达时调用，会设置 CommandReadyEvent 事件。
 * 用户态可以等待此事件，或使用轮询模式。
 */
VOID
VnvmeNotifyUserMode(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    if (FdoContext == NULL) {
        FdoContext = g_FdoContext;
    }
    
    if (FdoContext != NULL && FdoContext->EventNotificationEnabled) {
        KeSetEvent(&FdoContext->CommandReadyEvent, IO_NO_INCREMENT, FALSE);
    }
}

//===========================================================================
// Admin 命令转发
//===========================================================================

/**
 * @brief 转发 Admin 命令到共享内存供用户态处理
 * 
 * 零拷贝架构: Admin SQ 已经在共享内存中，用户态可以直接访问。
 * 内核只需要通知用户态有新命令到达。
 */
VOID
VnvmeForwardAdminCommandsToUser(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ ULONG NewTail
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm = GetShmControlBlock();
    
    UNREFERENCED_PARAMETER(PdoContext);
    
    if (shm == NULL) {
        TRACE_WARN("VnvmeForwardAdminCommandsToUser: SHM not initialized");
        return;
    }
    
    if (!g_FdoContext->UserReady) {
        TRACE_WARN("VnvmeForwardAdminCommandsToUser: User mode not ready");
        return;
    }
    
    // 更新共享内存中的 Admin SQ Tail
    shm->AdminSQ.Tail = NewTail;
    
    // 通过通知环告知用户态
    if (!NotifyRingPush(0, 0, NewTail)) {
        TRACE_WARN("VnvmeForwardAdminCommandsToUser: Failed to push notification");
    }
    
    // 唤醒用户态
    SignalUserMode();
    
    TRACE_INFO("VnvmeForwardAdminCommandsToUser: Forwarded Admin commands, tail=%lu", NewTail);
}

//===========================================================================
// I/O 命令转发
//===========================================================================

/**
 * @brief 转发 I/O 命令到共享内存供用户态处理
 * 
 * @param QueueId I/O 队列 ID (1-based)
 */
VOID
VnvmeForwardIoCommandsToUser(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ USHORT QueueId,
    _In_ ULONG NewTail
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm = GetShmControlBlock();
    PVNVME_QUEUE_DESCRIPTOR ioSqDescriptors;
    USHORT queueIndex;
    
    UNREFERENCED_PARAMETER(PdoContext);
    
    if (shm == NULL) {
        TRACE_WARN("VnvmeForwardIoCommandsToUser: SHM not initialized");
        return;
    }
    
    if (!g_FdoContext->UserReady) {
        TRACE_WARN("VnvmeForwardIoCommandsToUser: User mode not ready");
        return;
    }
    
    if (QueueId == 0 || QueueId > VNVME_MAX_IO_QUEUES) {
        TRACE_ERROR("VnvmeForwardIoCommandsToUser: Invalid queue ID %u", QueueId);
        return;
    }
    
    queueIndex = QueueId - 1;
    
    // 获取 I/O 队列描述符数组
    if (shm->IoQueueDescriptorOffset == 0) {
        TRACE_WARN("VnvmeForwardIoCommandsToUser: I/O queue descriptors not initialized");
        return;
    }
    
    ioSqDescriptors = (PVNVME_QUEUE_DESCRIPTOR)((PUCHAR)shm + shm->IoQueueDescriptorOffset);
    
    // 更新 I/O SQ Tail (SQ 和 CQ 交替排列)
    ioSqDescriptors[queueIndex * 2].Tail = NewTail;
    
    // 通过通知环告知用户态
    if (!NotifyRingPush(QueueId, 0, NewTail)) {
        TRACE_WARN("VnvmeForwardIoCommandsToUser: Failed to push notification for queue %u", QueueId);
    }
    
    // 唤醒用户态
    SignalUserMode();
    
    TRACE_VERBOSE("VnvmeForwardIoCommandsToUser: Forwarded I/O queue %u, tail=%lu", QueueId, NewTail);
}

//===========================================================================
// 用户态完成处理
//===========================================================================

/**
 * @brief 处理用户态提交的完成结果
 * 
 * 用户态将完成写入 CQ 后，调用 IOCTL 通知内核。
 * 内核需要更新 CQ Doorbell 并可能触发中断。
 */
NTSTATUS
VnvmeProcessUserCompletions(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    PVNVME_SHM_CONTROL_BLOCK shm = GetShmControlBlock();
    PVNVME_QUEUE_DESCRIPTOR ioDescs;
    ULONG adminCqTail;
    USHORT i;
    ULONG ioCqTail;
    BOOLEAN needInterrupt = FALSE;
    
    if (shm == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 检查 Admin CQ 更新
    adminCqTail = shm->AdminCQ.Tail;
    if (adminCqTail != PdoContext->AdminCq.Tail) {
        TRACE_VERBOSE("VnvmeProcessUserCompletions: Admin CQ tail %lu -> %lu",
                      PdoContext->AdminCq.Tail, adminCqTail);
        
        // 计算增量并更新统计
        {
            LONG64 delta;
            if (adminCqTail >= PdoContext->AdminCq.Tail) {
                delta = (LONG64)(adminCqTail - PdoContext->AdminCq.Tail);
            } else {
                // 处理环回
                delta = (LONG64)(shm->AdminCQ.Capacity - PdoContext->AdminCq.Tail + adminCqTail);
            }
            InterlockedAdd64(&PdoContext->AdminCommandsProcessed, delta);
        }
        
        PdoContext->AdminCq.Tail = adminCqTail;
        needInterrupt = TRUE;
    }
    
    // 处理 I/O CQ 更新
    if (shm->IoQueueDescriptorOffset != 0) {
        ioDescs = (PVNVME_QUEUE_DESCRIPTOR)((PUCHAR)shm + shm->IoQueueDescriptorOffset);
        
        for (i = 0; i < PdoContext->IoQueueCount; i++) {
            // CQ 在偶数索引后: [SQ0][CQ0][SQ1][CQ1]...
            PVNVME_QUEUE_DESCRIPTOR cqDesc = &ioDescs[i * 2 + 1];
            
            if (!PdoContext->IoCq[i].Created) {
                continue;
            }
            
            ioCqTail = cqDesc->Tail;
            if (ioCqTail != PdoContext->IoCq[i].Tail) {
                TRACE_VERBOSE("VnvmeProcessUserCompletions: I/O CQ%u tail %lu -> %lu",
                              i + 1, PdoContext->IoCq[i].Tail, ioCqTail);
                
                // 计算增量并更新统计
                {
                    LONG64 delta;
                    if (ioCqTail >= PdoContext->IoCq[i].Tail) {
                        delta = (LONG64)(ioCqTail - PdoContext->IoCq[i].Tail);
                    } else {
                        // 处理环回
                        delta = (LONG64)(cqDesc->Capacity - PdoContext->IoCq[i].Tail + ioCqTail);
                    }
                    InterlockedAdd64(&PdoContext->IoCommandsProcessed, delta);
                }
                
                PdoContext->IoCq[i].Tail = ioCqTail;
                needInterrupt = TRUE;
            }
        }
    }
    
    // 触发 MSI-X 中断 (如果有完成更新)
    // 注意: 在虚拟设备中，我们通过事件通知机制而非真正的硬件中断
    // 如果需要模拟中断，可以在这里触发
    if (needInterrupt) {
        TRACE_VERBOSE("VnvmeProcessUserCompletions: Completions processed, would trigger interrupt");
        // 未来可以添加: 通过 WDF 中断对象触发软件中断
        // 或者通知等待完成的线程
    }
    
    return STATUS_SUCCESS;
}
