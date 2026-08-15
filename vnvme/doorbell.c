/**
 * @file doorbell.c
 * @brief Doorbell 轮询处理
 * 
 * 实现轮询定时器来检测 Doorbell 写入。
 * 支持自适应轮询间隔：
 * - 有工作时：减少间隔到最小值 (更频繁轮询)
 * - 无工作时：增加间隔到最大值 (节省 CPU)
 * 
 * 轮询间隔可通过注册表配置:
 * - DoorbellPollIntervalUs: 默认轮询间隔 (微秒)
 */

#include "vnvme.h"

// 自适应轮询边界 (相对于配置的间隔)
#define VNVME_POLL_INTERVAL_DIVISOR     10      // 最小间隔 = 配置值 / 10
#define VNVME_POLL_INTERVAL_MULTIPLIER  100     // 最大间隔 = 配置值 * 100

// 用户态心跳超时默认值已移至 debug.c 的 g_HeartbeatTimeout100ns 全局变量
// 可通过注册表 HKLM\SYSTEM\CurrentControlSet\Services\vnvme\Parameters\HeartbeatTimeoutMs 配置

//===========================================================================
// 轮询定时器管理
//===========================================================================

/**
 * @brief 初始化轮询定时器
 * 
 * 创建定时器用于轮询 Doorbell 和 CC 寄存器变化。
 * 使用自适应轮询间隔，根据负载动态调整。
 */
NTSTATUS
VnvmeInitializePollingTimer(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    WDF_TIMER_CONFIG timerConfig;
    WDF_OBJECT_ATTRIBUTES timerAttributes;
    NTSTATUS status;
    
    TRACE_INFO("VnvmeInitializePollingTimer: Creating polling timer");
    
    // 配置非周期性定时器 (手动重新调度以支持自适应间隔)
    WDF_TIMER_CONFIG_INIT(
        &timerConfig,
        VnvmeEvtPollingTimer
        );
    
    timerConfig.AutomaticSerialization = TRUE;
    
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttributes);
    timerAttributes.ParentObject = PdoContext->Device;
    timerAttributes.ExecutionLevel = WdfExecutionLevelPassive;
    
    status = WdfTimerCreate(
        &timerConfig,
        &timerAttributes,
        &PdoContext->PollingTimer
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeInitializePollingTimer: WdfTimerCreate failed 0x%08X", status);
        return status;
    }
    
    // 使用注册表配置的轮询间隔
    PdoContext->PollingIntervalUs = CONFIG_POLL_INTERVAL_US;
    
    TRACE_INFO("VnvmeInitializePollingTimer: Timer created, initial interval=%lu us (from config)",
               PdoContext->PollingIntervalUs);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 启动轮询定时器
 */
VOID
VnvmeStartPollingTimer(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    if (PdoContext->PollingTimer != NULL) {
        TRACE_INFO("VnvmeStartPollingTimer: Starting timer with interval %lu us",
                   PdoContext->PollingIntervalUs);
        WdfTimerStart(PdoContext->PollingTimer, 
                      WDF_REL_TIMEOUT_IN_US(PdoContext->PollingIntervalUs));
        PdoContext->PollingActive = TRUE;
    }
}

/**
 * @brief 停止轮询定时器
 */
VOID
VnvmeStopPollingTimer(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    if (PdoContext->PollingTimer != NULL && PdoContext->PollingActive) {
        TRACE_INFO("VnvmeStopPollingTimer: Stopping timer");
        WdfTimerStop(PdoContext->PollingTimer, TRUE);
        PdoContext->PollingActive = FALSE;
    }
}

//===========================================================================
// 轮询回调
//===========================================================================

/**
 * @brief 调整轮询间隔
 * 
 * 根据是否有工作动态调整轮询间隔:
 * - 有工作: 减半间隔 (更频繁轮询)
 * - 无工作: 加倍间隔 (节省 CPU)
 * 
 * 边界基于配置的轮询间隔:
 * - 最小间隔 = 配置值 / 10 (但不小于 VNVME_MIN_POLL_INTERVAL_US)
 * - 最大间隔 = 配置值 * 100 (但不超过 VNVME_MAX_POLL_INTERVAL_US)
 */
static VOID
AdjustPollingInterval(
    _In_ PVNVME_PDO_CONTEXT PdoContext,
    _In_ BOOLEAN HadWork
    )
{
    ULONG newInterval = PdoContext->PollingIntervalUs;
    ULONG configInterval = CONFIG_POLL_INTERVAL_US;
    ULONG minInterval = configInterval / VNVME_POLL_INTERVAL_DIVISOR;
    ULONG maxInterval = configInterval * VNVME_POLL_INTERVAL_MULTIPLIER;
    
    // 确保边界在全局限制内
    if (minInterval < VNVME_MIN_POLL_INTERVAL_US) {
        minInterval = VNVME_MIN_POLL_INTERVAL_US;
    }
    if (maxInterval > VNVME_MAX_POLL_INTERVAL_US) {
        maxInterval = VNVME_MAX_POLL_INTERVAL_US;
    }
    
    if (HadWork) {
        // 有工作 - 减少间隔 (更快响应)
        newInterval = newInterval / 2;
        if (newInterval < minInterval) {
            newInterval = minInterval;
        }
    } else {
        // 无工作 - 增加间隔 (节省 CPU)
        newInterval = newInterval + newInterval / 4;  // 增加 25%
        if (newInterval > maxInterval) {
            newInterval = maxInterval;
        }
    }
    
    PdoContext->PollingIntervalUs = newInterval;
}

/**
 * @brief 检查用户态服务心跳
 * 
 * 如果用户态服务超过 10 秒没有发送心跳，认为其已崩溃。
 * 在这种情况下，切换到内核态命令处理模式继续运行。
 */
static VOID
CheckUserModeHeartbeat(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    LARGE_INTEGER currentTime;
    LONGLONG elapsed;
    
    if (FdoContext == NULL || !FdoContext->UserReady || FdoContext->UserCrashed) {
        return;
    }
    
    KeQuerySystemTime(&currentTime);
    elapsed = currentTime.QuadPart - FdoContext->LastHeartbeat.QuadPart;
    
    if (elapsed > g_HeartbeatTimeout100ns) {
        TRACE_ERROR("CheckUserModeHeartbeat: User-mode service timeout! "
                    "Last heartbeat was %lld seconds ago (timeout=%lldms)",
                    elapsed / 10000000LL,
                    g_HeartbeatTimeout100ns / 10000LL);
        
        // 标记用户态已崩溃
        FdoContext->UserCrashed = TRUE;
        FdoContext->UserReady = FALSE;
        
        // =========================================================
        // P0 修复: 在切换模式前，中止所有待处理的用户态命令
        // 这确保 stornvme 不会因为等待永远不会到来的完成而超时
        // =========================================================
        {
            PLIST_ENTRY entry;
            KIRQL oldIrql;
            
            KeAcquireSpinLock(&FdoContext->ChildDeviceListLock, &oldIrql);
            
            for (entry = FdoContext->ChildDeviceList.Flink;
                 entry != &FdoContext->ChildDeviceList;
                 entry = entry->Flink) {
                
                PVNVME_PDO_CONTEXT pdoContext = CONTAINING_RECORD(
                    entry, VNVME_PDO_CONTEXT, ListEntry);
                
                // 中止该控制器的所有待处理命令
                VnvmeAbortPendingUserCommands(pdoContext);
            }
            
            KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
        }
        
        // 切换到内核态命令处理模式
        FdoContext->CommandMode = VNVME_CMD_MODE_KERNEL;
        
        TRACE_WARN("CheckUserModeHeartbeat: Switched to kernel-mode command processing");
    }
}

/**
 * @brief 轮询定时器回调
 */
VOID
VnvmeEvtPollingTimer(
    _In_ WDFTIMER Timer
    )
{
    WDFDEVICE device;
    PVNVME_PDO_CONTEXT pdoContext;
    BOOLEAN hadWork;
    
    device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    pdoContext = VnvmeGetPdoContext(device);
    
    // 检查用户态服务心跳 (仅在用户态模式下)
    if (g_FdoContext != NULL && g_FdoContext->CommandMode == VNVME_CMD_MODE_USER) {
        CheckUserModeHeartbeat(g_FdoContext);
    }
    
    // 检查 Doorbell 变化并获取是否有工作
    hadWork = VnvmeProcessDoorbells(pdoContext);
    
    // 调整轮询间隔
    AdjustPollingInterval(pdoContext, hadWork);
    
    // 重新调度定时器 (非周期性定时器需要手动重新调度)
    if (pdoContext->PollingActive && pdoContext->PollingEnabled) {
        WdfTimerStart(pdoContext->PollingTimer,
                      WDF_REL_TIMEOUT_IN_US(pdoContext->PollingIntervalUs));
    }
}

/**
 * @brief 处理 Doorbell 变化
 * 
 * 检测 stornvme 对 Doorbell 寄存器的写入，处理新提交的命令。
 * 
 * 根据 CommandMode 选择处理方式:
 * - VNVME_CMD_MODE_KERNEL: 直接在内核中调用 admin_cmd.c/io_cmd.c
 * - VNVME_CMD_MODE_USER:   转发命令到共享内存，由用户态处理
 * 
 * @return TRUE 如果处理了任何工作，否则 FALSE
 */
BOOLEAN
VnvmeProcessDoorbells(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    ULONG currentCC;
    ULONG sqTail;
    ULONG cqHead;
    BOOLEAN hadWork = FALSE;
    VNVME_COMMAND_MODE cmdMode = VNVME_DEFAULT_CMD_MODE;
    
    if (PdoContext->Doorbells == NULL || PdoContext->Registers == NULL) {
        return FALSE;
    }
    
    // 获取命令处理模式
    if (g_FdoContext != NULL) {
        cmdMode = g_FdoContext->CommandMode;
    }
    
    // 1. 检查 CC 寄存器变化 (控制器启用/禁用)
    currentCC = PdoContext->Registers->CC.AsUint32;
    if (currentCC != PdoContext->CachedCC) {
        TRACE_INFO("VnvmeProcessDoorbells: CC changed 0x%08X -> 0x%08X",
                   PdoContext->CachedCC, currentCC);
        
        // 检测 CC.EN 位变化
        if ((currentCC & 0x1) && !(PdoContext->CachedCC & 0x1)) {
            // CC.EN: 0 -> 1, 控制器启用请求
            TRACE_INFO("VnvmeProcessDoorbells: CC.EN set, enabling controller");
            
            // 读取 AQA/ASQ/ACQ 寄存器设置 Admin 队列
            VnvmeInitializeAdminQueues(PdoContext);
            
            // 设置 CSTS.RDY = 1 表示控制器就绪
            PdoContext->Registers->CSTS.AsUint32 = 0x1;
            TRACE_INFO("VnvmeProcessDoorbells: CSTS.RDY set (CSTS=0x%08X)",
                       PdoContext->Registers->CSTS.AsUint32);
        } else if (!(currentCC & 0x1) && (PdoContext->CachedCC & 0x1)) {
            // CC.EN: 1 -> 0, 控制器禁用请求
            TRACE_INFO("VnvmeProcessDoorbells: CC.EN cleared, disabling controller");
            PdoContext->Registers->CSTS.AsUint32 = 0x0;
        }
        
        PdoContext->CachedCC = currentCC;
        hadWork = TRUE;
    }
    
    // 2. 如果控制器未就绪 (CC.EN=0 或 CSTS.RDY=0)，不处理 Doorbell
    if (!(PdoContext->Registers->CC.EN && PdoContext->Registers->CSTS.RDY)) {
        return hadWork;
    }
    
    // 3. 处理 Admin 队列 (Queue ID = 0)
    // Doorbell 布局: SQ0 Tail (0x1000), CQ0 Head (0x1004), SQ1 Tail (0x1008), ...
    sqTail = PdoContext->Doorbells[0] & 0xFFFF;  // Admin SQ Tail
    
    if (sqTail != PdoContext->LastAdminSqTail) {
        TRACE_INFO("VnvmeProcessDoorbells: Admin SQ tail %lu -> %lu",
                   PdoContext->LastAdminSqTail, sqTail);
        
        // 根据模式选择处理方式
        if (cmdMode == VNVME_CMD_MODE_KERNEL) {
            // 内核模式: 直接处理命令
            VnvmeProcessAdminCommands(PdoContext, sqTail);
        } else {
            // 用户态模式: 转发到共享内存
            VnvmeForwardAdminCommandsToUser(PdoContext, sqTail);
        }
        
        PdoContext->LastAdminSqTail = sqTail;
        hadWork = TRUE;
    }
    
    // Admin CQ Head Doorbell
    cqHead = PdoContext->Doorbells[1] & 0xFFFF;  // Admin CQ Head
    
    if (cqHead != PdoContext->LastAdminCqHead) {
        TRACE_VERBOSE("VnvmeProcessDoorbells: Admin CQ head %lu -> %lu",
                      PdoContext->LastAdminCqHead, cqHead);
        PdoContext->LastAdminCqHead = cqHead;
    }
    
    // 4. 处理 I/O 队列的 Doorbell
    for (USHORT qid = 1; qid <= PdoContext->IoQueueCount; qid++) {
        ULONG ioSqTailIdx = qid * 2;      // I/O SQ Tail Doorbell index
        ULONG ioCqHeadIdx = qid * 2 + 1;  // I/O CQ Head Doorbell index
        USHORT queueIndex = VNVME_QUEUE_ID_TO_INDEX(qid);
        
        if (!PdoContext->IoSq[queueIndex].Created) {
            continue;
        }
        
        sqTail = PdoContext->Doorbells[ioSqTailIdx] & 0xFFFF;
        
        if (sqTail != PdoContext->IoSq[queueIndex].Tail) {
            TRACE_VERBOSE("VnvmeProcessDoorbells: I/O SQ[%u] tail %lu -> %lu",
                          qid, PdoContext->IoSq[queueIndex].Tail, sqTail);
            
            // 根据模式选择处理方式
            if (cmdMode == VNVME_CMD_MODE_KERNEL) {
                // 内核模式: 直接处理命令
                VnvmeProcessIoCommands(PdoContext, qid, sqTail);
            } else {
                // 用户态模式: 转发到共享内存
                VnvmeForwardIoCommandsToUser(PdoContext, qid, sqTail);
            }
            
            PdoContext->IoSq[queueIndex].Tail = sqTail;
            hadWork = TRUE;
        }
        
        cqHead = PdoContext->Doorbells[ioCqHeadIdx] & 0xFFFF;
        if (cqHead != PdoContext->IoCq[queueIndex].Head) {
            TRACE_VERBOSE("VnvmeProcessDoorbells: I/O CQ[%u] head %lu -> %lu",
                          qid, PdoContext->IoCq[queueIndex].Head, cqHead);
            PdoContext->IoCq[queueIndex].Head = cqHead;
        }
    }
    
    return hadWork;
}
