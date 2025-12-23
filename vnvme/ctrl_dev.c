/**
 * @file ctrl_dev.c
 * @brief 控制设备实现
 * 
 * 创建 \\Device\\VNVMEControl 设备，处理用户态 IOCTL 请求。
 */

#include "vnvme.h"
#include <ntstrsafe.h>

//===========================================================================
// 内部函数声明
//===========================================================================

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

static NTSTATUS
VnvmeHandleCreateController(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleDeleteController(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    );

static NTSTATUS
VnvmeHandleListControllers(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleGetCommandEvent(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleSubmitCompletions(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    );

//===========================================================================
// 控制设备创建/删除
//===========================================================================

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
    
    // 需要管理员权限的 SDDL
    DECLARE_CONST_UNICODE_STRING(sddl, L"D:P(A;;GA;;;SY)(A;;GA;;;BA)");
    
    TRACE_INFO("VnvmeCreateControlDevice: Creating control device");
    
    // 分配设备初始化结构
    deviceInit = WdfControlDeviceInitAllocate(
        WdfDeviceGetDriver(Device),
        &sddl
        );
    
    if (deviceInit == NULL) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfControlDeviceInitAllocate failed");
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    // 设置设备名称
    status = WdfDeviceInitAssignName(deviceInit, &deviceName);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfDeviceInitAssignName failed, status=0x%08X", status);
        WdfDeviceInitFree(deviceInit);
        return status;
    }
    
    // 设置设备类型
    WdfDeviceInitSetDeviceType(deviceInit, FILE_DEVICE_UNKNOWN);
    WdfDeviceInitSetExclusive(deviceInit, FALSE);
    
    // 创建设备
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    
    status = WdfDeviceCreate(&deviceInit, &attributes, &controlDevice);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfDeviceCreate failed, status=0x%08X", status);
        // deviceInit 在失败时已被 WdfDeviceCreate 释放
        return status;
    }
    
    // 创建符号链接
    status = WdfDeviceCreateSymbolicLink(controlDevice, &symbolicLink);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeCreateControlDevice: WdfDeviceCreateSymbolicLink failed, status=0x%08X", status);
        WdfObjectDelete(controlDevice);
        return status;
    }
    
    // 创建默认 I/O 队列
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
    
    // 完成控制设备初始化
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

//===========================================================================
// IOCTL 分发
//===========================================================================

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
        
        case IOCTL_VNVME_CREATE_CONTROLLER:
            status = VnvmeHandleCreateController(Request, InputBufferLength, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_DELETE_CONTROLLER:
            status = VnvmeHandleDeleteController(Request, InputBufferLength);
            break;
        
        case IOCTL_VNVME_LIST_CONTROLLERS:
            status = VnvmeHandleListControllers(Request, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_GET_COMMAND_EVENT:
            status = VnvmeHandleGetCommandEvent(Request, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_SUBMIT_COMPLETIONS:
            status = VnvmeHandleSubmitCompletions(Request, InputBufferLength);
            break;
        
        default:
            TRACE_WARN("VnvmeEvtIoDeviceControl: Unknown IoControlCode=0x%08X", IoControlCode);
            status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    
    WdfRequestCompleteWithInformation(Request, status, bytesReturned);
}

//===========================================================================
// IOCTL 处理函数
//===========================================================================

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
    output->NamespaceCount = 0;  // TODO: 计算命名空间数
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
    
    // 如果已经映射，先取消映射
    if (fdoContext->ShmUserVirtAddr != NULL) {
        VnvmeUnmapShmFromUser(fdoContext);
    }
    
    // 映射到用户空间
    status = VnvmeMapShmToUser(fdoContext, &userAddress);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleMapShm: VnvmeMapShmToUser failed, status=0x%08X", status);
        return status;
    }
    
    output->UserAddress = userAddress;
    output->ActualSize = (UINT32)fdoContext->ShmSize;
    output->Reserved = 0;
    
    // 命令事件句柄
    // 零复制架构: 用户态主要通过轮询 NotifyRing 获取命令
    // 事件句柄用于阻塞等待优化，通过 IOCTL_VNVME_GET_COMMAND_EVENT 获取
    output->CommandEventHandle = NULL;
    
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
    
    // 验证版本兼容性
    if ((input->UserVersion >> 16) != (VNVME_VERSION >> 16)) {
        TRACE_ERROR("VnvmeHandleUserReady: Version mismatch, user=0x%08X, driver=0x%08X",
                    input->UserVersion, VNVME_VERSION);
        return STATUS_REVISION_MISMATCH;
    }
    
    fdoContext->UserPid = input->UserPid;
    fdoContext->UserReady = TRUE;
    KeQuerySystemTime(&fdoContext->LastHeartbeat);
    
    // 通知等待者
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
    
    // 更新心跳时间
    KeQuerySystemTime(&fdoContext->LastHeartbeat);
    
    // 填充响应
    output->KernelTimestamp = fdoContext->LastHeartbeat.QuadPart;
    output->PendingCommands = 0;  // TODO: 计算待处理命令数
    output->Reserved = 0;
    
    *BytesReturned = sizeof(VNVME_HEARTBEAT_OUTPUT);
    return STATUS_SUCCESS;
}

//===========================================================================
// 控制器管理 IOCTL 处理
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_CREATE_CONTROLLER
 */
static NTSTATUS
VnvmeHandleCreateController(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_CREATE_CONTROLLER_INPUT input;
    PVNVME_CREATE_CONTROLLER_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    ULONG controllerId = 0;
    
    *BytesReturned = 0;
    
    TRACE_INFO("VnvmeHandleCreateController: Received request");
    
    if (fdoContext == NULL) {
        TRACE_ERROR("VnvmeHandleCreateController: FDO not ready");
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_CREATE_CONTROLLER_INPUT)) {
        TRACE_ERROR("VnvmeHandleCreateController: Input buffer too small");
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    if (OutputBufferLength < sizeof(VNVME_CREATE_CONTROLLER_OUTPUT)) {
        TRACE_ERROR("VnvmeHandleCreateController: Output buffer too small");
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(VNVME_CREATE_CONTROLLER_INPUT),
        (PVOID*)&input,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleCreateController: WdfRequestRetrieveInputBuffer failed, status=0x%08X", status);
        return status;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_CREATE_CONTROLLER_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleCreateController: WdfRequestRetrieveOutputBuffer failed, status=0x%08X", status);
        return status;
    }
    
    // 分配控制器 ID
    controllerId = InterlockedIncrement((volatile LONG*)&fdoContext->NextControllerId) - 1;
    
    // 创建虚拟控制器 (忽略 Config 中的部分配置，Phase 1 使用默认值)
    UNREFERENCED_PARAMETER(input);  // Config 暂未使用
    
    status = VnvmeCreateVirtualController(fdoContext, controllerId, NULL);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleCreateController: VnvmeCreateVirtualController failed, status=0x%08X", status);
        return status;
    }
    
    // 填充输出
    output->ControllerId = controllerId;
    RtlZeroMemory(output->DeviceInstanceId, sizeof(output->DeviceInstanceId));
    
    // 构造设备实例 ID 字符串
    RtlStringCbPrintfW(output->DeviceInstanceId, sizeof(output->DeviceInstanceId),
        L"VNVME\\CONTROLLER\\%lu", controllerId);
    
    *BytesReturned = sizeof(VNVME_CREATE_CONTROLLER_OUTPUT);
    
    TRACE_INFO("VnvmeHandleCreateController: Created controller %lu", controllerId);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_DELETE_CONTROLLER
 */
static NTSTATUS
VnvmeHandleDeleteController(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVNVME_DELETE_CONTROLLER_INPUT input;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    
    TRACE_INFO("VnvmeHandleDeleteController: Received request");
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_DELETE_CONTROLLER_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(VNVME_DELETE_CONTROLLER_INPUT),
        (PVOID*)&input,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 删除虚拟控制器
    status = VnvmeDeleteVirtualController(fdoContext, input->ControllerId);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleDeleteController: VnvmeDeleteVirtualController failed, status=0x%08X", status);
        return status;
    }
    
    TRACE_INFO("VnvmeHandleDeleteController: Deleted controller %lu", input->ControllerId);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_LIST_CONTROLLERS
 */
static NTSTATUS
VnvmeHandleListControllers(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_LIST_CONTROLLERS_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    PLIST_ENTRY entry;
    ULONG index = 0;
    KIRQL oldIrql;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (OutputBufferLength < sizeof(VNVME_LIST_CONTROLLERS_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_LIST_CONTROLLERS_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    RtlZeroMemory(output, sizeof(VNVME_LIST_CONTROLLERS_OUTPUT));
    
    // 遍历控制器列表
    KeAcquireSpinLock(&fdoContext->ChildDeviceListLock, &oldIrql);
    
    for (entry = fdoContext->ChildDeviceList.Flink;
         entry != &fdoContext->ChildDeviceList && index < 16;
         entry = entry->Flink) {
        
        PVNVME_PDO_CONTEXT pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
        PVNVME_CONTROLLER_INFO info = &output->Controllers[index];
        
        info->ControllerId = pdoContext->ControllerId;
        info->Status = 0;  // TODO: 实际状态
        info->NamespaceCount = pdoContext->NamespaceCount;
        info->TotalCapacity = 0;  // TODO: 计算容量
        
        // 默认序列号和型号
        RtlCopyMemory(info->SerialNumber, "VNVME000        ", 16);
        RtlCopyMemory(info->ModelNumber, "Virtual NVMe Controller         ", 32);
        
        index++;
    }
    
    KeReleaseSpinLock(&fdoContext->ChildDeviceListLock, oldIrql);
    
    output->ControllerCount = index;
    *BytesReturned = sizeof(VNVME_LIST_CONTROLLERS_OUTPUT);
    
    TRACE_INFO("VnvmeHandleListControllers: Listed %lu controllers", index);
    return STATUS_SUCCESS;
}

//===========================================================================
// 事件通知 IOCTL 处理
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_GET_COMMAND_EVENT
 * 
 * 返回一个用户态可等待的事件句柄。
 * 当有新命令到达时，内核会设置此事件。
 * 
 * 注意: 零复制架构中，用户态主要通过轮询 NotifyRing 获取通知。
 * 事件机制是可选的优化，用于减少轮询时的 CPU 占用。
 * 
 * TODO Phase 3: 使用 ZwCreateEvent + ObReferenceObjectByHandle 实现
 *               或者使用 IO 完成端口机制
 */
static NTSTATUS
VnvmeHandleGetCommandEvent(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_GET_COMMAND_EVENT_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (OutputBufferLength < sizeof(VNVME_GET_COMMAND_EVENT_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_GET_COMMAND_EVENT_OUTPUT),
        (PVOID*)&output,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // TODO Phase 3: 实现用户态可等待事件
    // 当前返回 NULL，用户态使用轮询模式
    // 零复制架构: 轮询 NotifyRing 也是高效的
    output->EventHandle = NULL;
    *BytesReturned = sizeof(VNVME_GET_COMMAND_EVENT_OUTPUT);
    
    TRACE_INFO("VnvmeHandleGetCommandEvent: Polling mode (event not implemented)");
    return STATUS_SUCCESS;
}

//===========================================================================
// 完成提交 IOCTL 处理
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_SUBMIT_COMPLETIONS
 * 
 * 用户态通知内核已经向 CQ 写入了完成条目。
 * 内核需要:
 * 1. 更新 CQ Doorbell (Head pointer)
 * 2. 触发中断通知 stornvme
 * 
 * 零复制架构:
 * - 用户态直接写入共享内存中的 CQ
 * - 然后调用此 IOCTL 通知内核更新 doorbell 并产生中断
 */
static NTSTATUS
VnvmeHandleSubmitCompletions(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVNVME_SUBMIT_COMPLETIONS_INPUT input;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    ULONG completionCount;
    ULONG controllersNotified = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_SUBMIT_COMPLETIONS_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(
        Request,
        sizeof(VNVME_SUBMIT_COMPLETIONS_INPUT),
        (PVOID*)&input,
        NULL
        );
    
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleSubmitCompletions: WdfRequestRetrieveInputBuffer failed, status=0x%08X", status);
        return status;
    }
    
    completionCount = input->CompletionCount;
    
    if (completionCount == 0) {
        TRACE_WARN("VnvmeHandleSubmitCompletions: CompletionCount is 0");
        return STATUS_SUCCESS;
    }
    
    // 遍历所有控制器，通知有完成条目
    // TODO: 优化 - 可以在输入中指定控制器 ID
    KeAcquireSpinLock(&fdoContext->ChildDeviceListLock, &oldIrql);
    
    for (entry = fdoContext->ChildDeviceList.Flink;
         entry != &fdoContext->ChildDeviceList;
         entry = entry->Flink) {
        
        PVNVME_PDO_CONTEXT pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
        
        // 触发中断通知 stornvme 有完成条目可用
        // 零复制架构: CQ 数据已在共享内存中
        // 只需要通过 MSI-X 或 INTx 机制通知驱动轮询 CQ
        
        if (pdoContext->Registers != NULL) {
            // 增加命令处理计数 (统计)
            InterlockedAdd64(&fdoContext->CommandsProcessed, completionCount);
            
            // TODO Phase 3: 实际触发中断
            // 方法 1: 写 INTMS 寄存器并模拟 INTx
            // 方法 2: 调用 WdfInterruptQueueDpcForIsr (需要 WDFINTERRUPT)
            // 方法 3: 发送软件生成的 MSI-X 消息
            //
            // 当前: 依赖 stornvme 轮询 CQ doorbell
            // stornvme 会检测到 CQ Head != Tail 并处理完成
            
            controllersNotified++;
        }
    }
    
    KeReleaseSpinLock(&fdoContext->ChildDeviceListLock, oldIrql);
    
    TRACE_INFO("VnvmeHandleSubmitCompletions: Submitted %lu completions to %lu controllers",
               completionCount, controllersNotified);
    
    return STATUS_SUCCESS;
}