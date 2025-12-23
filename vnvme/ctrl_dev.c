/**
 * @file ctrl_dev.c
 * @brief 控制设备实现
 * 
 * 创建 \\Device\\VNVMEControl 设备，处理用户态 IOCTL 请求。
 */

#include "vnvme.h"

/*===========================================================================
 * 内部函数声明
 *===========================================================================*/

static NTSTATUS
VnvmeHandleGetVersion(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleGetStatus(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleMapShm(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleUserReady(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    );

static NTSTATUS
VnvmeHandleHeartbeat(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

/*===========================================================================
 * 控制设备创建/删除
 *===========================================================================*/

/**
 * @brief 创建控制设备
 */
NTSTATUS
VnvmeCreateControlDevice(
    _In_ WDFDEVICE Device
    )
{
    NTSTATUS status;
    PWDFDEVICE_INIT deviceInit = NULL;
    WDFDEVICE controlDevice = NULL;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDFQUEUE queue;
    PVNVME_FDO_CONTEXT fdoContext = VnvmeGetFdoContext(Device);
    
    DECLARE_CONST_UNICODE_STRING(deviceName, L"\\Device\\VNVMEControl");
    DECLARE_CONST_UNICODE_STRING(symbolicLink, L"\\DosDevices\\VNVMEControl");
    
    /* 需要管理员权限的 SDDL */
    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    
    TRACE_INFO("VnvmeCreateControlDevice: Creating control device");
    
    /* 分配设备初始化结构 */
    deviceInit = WdfControlDeviceInitAllocate(
        WdfDeviceGetDriver(Device),
        &sddl
        );
    
    if (deviceInit == NULL) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfControlDeviceInitAllocate failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* 设置设备名称 */
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfDeviceInitAssignName failed, status=0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    /* 设置设备类型 */
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, FALSE);
    
    /* 创建设备 */
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    
    status = WdfDeviceCreate(&deviceInit, &attributes, &controlDevice);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfDeviceCreate failed, status=0x%08X", status);
        /* deviceInit 在失败时已被 WdfDeviceCreate 释放 */
        return status;
    }
    
    /* 创建符号链接 */
    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfDeviceCreateSymbolicLink failed, status=0x%08X", status);
        WdfObjectDelete(controlDevice);
        return status;
    }
    
    /* 创建默认 I/O 队列 */
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchSequential);
    queueConfig.EvtIoDeviceControl = VnvmeEvtIoDeviceControl;
    
    status = WdfIoQueueCreate(
        controlDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        &queue
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfIoQueueCreate failed, status=0x%08X", status);
        WdfObjectDelete(controlDevice);
        return status;
    }
    
    /* 完成控制设备初始化 */
    WdfControlFinishInitializing(controlDevice);
    
    fdoContext->ControlDevice = controlDevice;
    
    TRACE_INFO("VnvmeCreateControlDevice: Control device created at %wZ", &symbolicLink);
    return STATUS_SUCCESS;
}

/**
 * @brief 删除控制设备
 */
VOID
VnvmeDeleteControlDevice(
    _In_ PVNVME_FDO_CONTEXT FdoContext
    )
{
    if (FdoContext->ControlDevice != NULL) {
        TRACE_INFO("VnvmeDeleteControlDevice: Deleting control device");
        WdfObjectDelete(FdoContext->ControlDevice);
        FdoContext->ControlDevice = NULL;
    }
}

/*===========================================================================
 * IOCTL 分发
 *===========================================================================*/

/**
 * @brief IOCTL 请求处理
 */
VOID
VnvmeEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
    )
{
    NTSTATUS status = STATUS_SUCCESS;
    size_t bytesReturned = 0;
    
    UNREFERENCED_PARAMETER(Queue);
    
    TRACE_VERBOSE("VnvmeEvtIoDeviceControl: IoControlCode=0x%08X", IoControlCode);
    
    switch (IoControlCode) {
        
        case IOCTL_VNVME_GET_VERSION:
            status = VnvmeHandleGetVersion(Request, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_GET_STATUS:
            status = VnvmeHandleGetStatus(Request, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_MAP_SHARED_MEMORY:
            status = VnvmeHandleMapShm(Request, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_USER_READY:
            status = VnvmeHandleUserReady(Request, InputBufferLength);
            break;
        
        case IOCTL_VNVME_HEARTBEAT:
            status = VnvmeHandleHeartbeat(Request, InputBufferLength, OutputBufferLength, &bytesReturned);
            break;
        
        default:
            TRACE_WARN("VnvmeEvtIoDeviceControl: Unknown IoControlCode=0x%08X", IoControlCode);
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

/*===========================================================================
 * IOCTL 处理函数
 *===========================================================================*/

/**
 * @brief 处理 IOCTL_VNVME_GET_VERSION
 */
static NTSTATUS
VnvmeHandleGetVersion(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_GET_VERSION_OUTPUT output;
    
    *BytesReturned = 0;
    
    if (OutputBufferLength < sizeof(VNVME_GET_VERSION_OUTPUT)) {
        TRACE_WARN("VnvmeHandleGetVersion: Buffer too small");
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_GET_VERSION_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleGetVersion: WdfRequestRetrieveOutputBuffer failed, status=0x%08X", status);
        return status;
    }
    
    output->DriverVersion = VNVME_VERSION;
    output->ApiVersion = VNVME_VERSION;
    output->BuildNumber = 1;
    output->Reserved = 0;
    
    *BytesReturned = sizeof(VNVME_GET_VERSION_OUTPUT);
    
    TRACE_INFO("VnvmeHandleGetVersion: Version=0x%08X", output->DriverVersion);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_GET_STATUS
 */
static NTSTATUS
VnvmeHandleGetStatus(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_GET_STATUS_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (OutputBufferLength < sizeof(VNVME_GET_STATUS_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_GET_STATUS_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    output->DriverStatus = VNVME_DRIVER_STATUS_READY;
    output->UserServiceStatus = fdoContext->UserReady ? 
        VNVME_USER_STATUS_READY : VNVME_USER_STATUS_NOT_CONNECTED;
    output->ControllerCount = fdoContext->ChildDeviceCount;
    output->NamespaceCount = 0;  /* TODO: 计算命名空间数 */
    output->ShmMapped = (fdoContext->ShmUserVirtAddr != NULL) ? 1 : 0;
    output->ShmSize = (UINT32)fdoContext->ShmSize;
    output->CommandsProcessed = fdoContext->CommandsProcessed;
    output->ErrorCount = fdoContext->ErrorCount;
    
    *BytesReturned = sizeof(VNVME_GET_STATUS_OUTPUT);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_MAP_SHARED_MEMORY
 */
static NTSTATUS
VnvmeHandleMapShm(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_MAP_SHARED_MEMORY_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    PVOID userAddress = NULL;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (OutputBufferLength < sizeof(VNVME_MAP_SHARED_MEMORY_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_MAP_SHARED_MEMORY_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    /* 如果已经映射，先取消映射 */
    if (fdoContext->ShmUserVirtAddr != NULL) {
        VnvmeUnmapShmFromUser(fdoContext);
    }
    
    /* 映射到用户空间 */
    status = VnvmeMapShmToUser(fdoContext, &userAddress);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleMapShm: VnvmeMapShmToUser failed, status=0x%08X", status);
        return status;
    }
    
    output->UserAddress = userAddress;
    output->ActualSize = (UINT32)fdoContext->ShmSize;
    output->Reserved = 0;
    output->CommandEventHandle = NULL;  /* TODO: 创建用户可见的事件句柄 */
    
    *BytesReturned = sizeof(VNVME_MAP_SHARED_MEMORY_OUTPUT);
    
    TRACE_INFO("VnvmeHandleMapShm: Mapped at %p, size=%llu", 
               userAddress, fdoContext->ShmSize);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_USER_READY
 */
static NTSTATUS
VnvmeHandleUserReady(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVNVME_USER_READY_INPUT input;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_USER_READY_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(VNVME_USER_READY_INPUT),
        (PVOID*)&input,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    /* 验证版本兼容性 */
    if ((input->UserVersion >> 16) != (VNVME_VERSION >> 16)) {
        TRACE_ERROR("VnvmeHandleUserReady: Version mismatch, user=0x%08X, driver=0x%08X",
                    input->UserVersion, VNVME_VERSION);
        return STATUS_REVISION_MISMATCH;
    }
    
    fdoContext->UserPid = input->UserPid;
    fdoContext->UserReady = TRUE;
    KeQuerySystemTime(&fdoContext->LastHeartbeat);
    
    /* 通知等待者 */
    KeSetEvent(&fdoContext->UserReadyEvent, IO_NO_INCREMENT, FALSE);
    
    TRACE_INFO("VnvmeHandleUserReady: User service ready, PID=%u", input->UserPid);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_HEARTBEAT
 */
static NTSTATUS
VnvmeHandleHeartbeat(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_HEARTBEAT_INPUT input;
    PVNVME_HEARTBEAT_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL || !fdoContext->UserReady) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_HEARTBEAT_INPUT) ||
        OutputBufferLength < sizeof(VNVME_HEARTBEAT_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(VNVME_HEARTBEAT_INPUT),
        (PVOID*)&input,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_HEARTBEAT_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    /* 更新心跳时间 */
    KeQuerySystemTime(&fdoContext->LastHeartbeat);
    
    /* 填充响应 */
    output->KernelTimestamp = fdoContext->LastHeartbeat.QuadPart;
    output->PendingCommands = 0;  /* TODO: 计算待处理命令数 */
    output->Reserved = 0;
    
    *BytesReturned = sizeof(VNVME_HEARTBEAT_OUTPUT);
    return STATUS_SUCCESS;
}
