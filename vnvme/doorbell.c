/**
 * @file doorbell.c
 * @brief Doorbell 轮询处理
 * 
 * 实现轮询定时器来检测 Doorbell 写入。
 */

#include "vnvme.h"

/*===========================================================================
 * 轮询定时器管理
 *===========================================================================*/

/**
 * @brief 初始化轮询定时器
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
    
    /* TODO: Phase 4 - 实现定时器 */
    /*
     * 配置定时器：
     * - 周期：50-100 微秒
     * - 回调：VnvmeEvtPollingTimer
     */
    
    WDF_TIMER_CONFIG_INIT_PERIODIC(
        &timerConfig,
        VnvmeEvtPollingTimer,
        VNVME_POLLING_INTERVAL_MS
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
    
    TRACE_INFO("VnvmeInitializePollingTimer: Timer created, interval=%d ms",
               VNVME_POLLING_INTERVAL_MS);
    
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
        TRACE_INFO("VnvmeStartPollingTimer: Starting timer");
        WdfTimerStart(PdoContext->PollingTimer, WDF_REL_TIMEOUT_IN_MS(VNVME_POLLING_INTERVAL_MS));
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

/*===========================================================================
 * 轮询回调
 *===========================================================================*/

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
    
    device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    pdoContext = VnvmeGetPdoContext(device);
    
    /* 检查 Doorbell 变化 */
    VnvmeProcessDoorbells(pdoContext);
}

/**
 * @brief 处理 Doorbell 变化
 */
VOID
VnvmeProcessDoorbells(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    ULONG queueId;
    ULONG doorbellOffset;
    ULONG sqTail;
    ULONG cqHead;
    PULONG doorbellReg;
    
    if (PdoContext->Bar0 == NULL) {
        return;
    }
    
    /* TODO: Phase 4 - 实现完整的 Doorbell 轮询逻辑 */
    /*
     * 对每个队列：
     * 1. 读取 SQ Tail Doorbell
     * 2. 如果有变化，处理新的命令
     * 3. 读取 CQ Head Doorbell  
     * 4. 如果有变化，更新完成队列状态
     */
    
    /* 处理 Admin 队列 (Queue ID = 0) */
    queueId = 0;
    doorbellOffset = NVME_DOORBELL_OFFSET(queueId, 0); /* DSTRD = 0 */
    
    doorbellReg = (PULONG)((PUCHAR)PdoContext->Bar0 + doorbellOffset);
    sqTail = *doorbellReg & 0xFFFF;
    
    if (sqTail != PdoContext->LastAdminSqTail) {
        TRACE_INFO("VnvmeProcessDoorbells: Admin SQ tail changed %u -> %u",
                   PdoContext->LastAdminSqTail, sqTail);
        PdoContext->LastAdminSqTail = sqTail;
        
        /* TODO: 处理 Admin 命令 */
    }
    
    /* CQ Head Doorbell */
    doorbellReg = (PULONG)((PUCHAR)PdoContext->Bar0 + doorbellOffset + 4);
    cqHead = *doorbellReg & 0xFFFF;
    
    if (cqHead != PdoContext->LastAdminCqHead) {
        TRACE_VERBOSE("VnvmeProcessDoorbells: Admin CQ head changed %u -> %u",
                      PdoContext->LastAdminCqHead, cqHead);
        PdoContext->LastAdminCqHead = cqHead;
    }
    
    /* TODO: Phase 5 - 处理 I/O 队列的 Doorbell */
}
