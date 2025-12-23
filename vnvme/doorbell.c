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
 * 
 * 检测 stornvme 对 Doorbell 寄存器的写入，处理新提交的命令。
 */
VOID
VnvmeProcessDoorbells(
    _In_ PVNVME_PDO_CONTEXT PdoContext
    )
{
    ULONG currentCC;
    ULONG sqTail;
    ULONG cqHead;
    BOOLEAN hadWork = FALSE;
    
    if (PdoContext->Doorbells == NULL || PdoContext->Registers == NULL) {
        return;
    }
    
    /* 1. 检查 CC 寄存器变化 (控制器启用/禁用) */
    currentCC = PdoContext->Registers->CC.AsUint32;
    if (currentCC != PdoContext->CachedCC) {
        TRACE_INFO("VnvmeProcessDoorbells: CC changed 0x%08X -> 0x%08X",
                   PdoContext->CachedCC, currentCC);
        
        /* 检测 CC.EN 位变化 */
        if ((currentCC & 0x1) && !(PdoContext->CachedCC & 0x1)) {
            /* CC.EN: 0 -> 1, 控制器启用请求 */
            TRACE_INFO("VnvmeProcessDoorbells: CC.EN set, enabling controller");
            
            /* TODO: Phase 4 - 读取 AQA/ASQ/ACQ 并设置 Admin 队列 */
            
            /* 设置 CSTS.RDY = 1 表示控制器就绪 */
            PdoContext->Registers->CSTS.AsUint32 = 0x1;
            TRACE_INFO("VnvmeProcessDoorbells: CSTS.RDY set (CSTS=0x%08X)",
                       PdoContext->Registers->CSTS.AsUint32);
        } else if (!(currentCC & 0x1) && (PdoContext->CachedCC & 0x1)) {
            /* CC.EN: 1 -> 0, 控制器禁用请求 */
            TRACE_INFO("VnvmeProcessDoorbells: CC.EN cleared, disabling controller");
            PdoContext->Registers->CSTS.AsUint32 = 0x0;
        }
        
        PdoContext->CachedCC = currentCC;
        hadWork = TRUE;
    }
    
    /* 2. 如果控制器未就绪 (CC.EN=0 或 CSTS.RDY=0)，不处理 Doorbell */
    if (!(PdoContext->Registers->CC.EN && PdoContext->Registers->CSTS.RDY)) {
        return;
    }
    
    /* 3. 处理 Admin 队列 (Queue ID = 0) */
    /* Doorbell 布局: SQ0 Tail (0x1000), CQ0 Head (0x1004), SQ1 Tail (0x1008), ... */
    sqTail = PdoContext->Doorbells[0] & 0xFFFF;  /* Admin SQ Tail */
    
    if (sqTail != PdoContext->LastAdminSqTail) {
        TRACE_INFO("VnvmeProcessDoorbells: Admin SQ tail %u -> %u",
                   PdoContext->LastAdminSqTail, sqTail);
        
        /* TODO: Phase 4 - 调用 VnvmeProcessAdminCommands() */
        
        PdoContext->LastAdminSqTail = sqTail;
        hadWork = TRUE;
    }
    
    /* Admin CQ Head Doorbell */
    cqHead = PdoContext->Doorbells[1] & 0xFFFF;  /* Admin CQ Head */
    
    if (cqHead != PdoContext->LastAdminCqHead) {
        TRACE_VERBOSE("VnvmeProcessDoorbells: Admin CQ head %u -> %u",
                      PdoContext->LastAdminCqHead, cqHead);
        PdoContext->LastAdminCqHead = cqHead;
    }
    
    /* TODO: Phase 5 - 处理 I/O 队列的 Doorbell */
    /*
     * for (i = 0; i < IoQueueCount; i++) {
     *     sqTail = Doorbells[2 + i * 2];      // I/O SQ Tail
     *     cqHead = Doorbells[2 + i * 2 + 1];  // I/O CQ Head
     *     ...
     * }
     */
    
    UNREFERENCED_PARAMETER(hadWork);
    /* TODO: Phase 4 - 自适应轮询间隔
     * if (hadWork) {
     *     PollingIntervalUs = max(PollingIntervalUs / 2, 10);
     * } else {
     *     PollingIntervalUs = min(PollingIntervalUs * 2, 1000);
     * }
     */
}
