/**
 * @file ctrl_dev.c
 * @brief 控制设备实现
 * 
 * 创建 \\Device\\VNVMEControl 设备，处理用户态 IOCTL 请求。
 */

#include "vnvme.h"
#include <ntstrsafe.h>

//===========================================================================
// IOCTL 输入验证常量
//===========================================================================

// 单次提交的最大完成数量 (防止过大值导致统计溢出)
#define VNVME_MAX_COMPLETIONS_PER_SUBMIT    4096

// 支持的块大小
#define VNVME_BLOCK_SIZE_512                512
#define VNVME_BLOCK_SIZE_4096               4096

// 最大调试级别
#define VNVME_MAX_DEBUG_LEVEL               5

// 最大命名空间容量 (16TB)
#define VNVME_MAX_NAMESPACE_CAPACITY        (16ULL * 1024 * 1024 * 1024 * 1024)

//===========================================================================
// ObOpenObjectByPointer 声明 (用于创建用户态可等待事件句柄)
//===========================================================================

NTKERNELAPI
NTSTATUS
ObOpenObjectByPointer(
    _In_ PVOID Object,
    _In_ ULONG HandleAttributes,
    _In_opt_ PACCESS_STATE PassedAccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_opt_ POBJECT_TYPE ObjectType,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PHANDLE Handle
    );

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

static NTSTATUS
VnvmeHandleGetStats(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleSetDebugLevel(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    );

static NTSTATUS
VnvmeHandleUnmapShm(
    _In_ WDFREQUEST Request
    );

static NTSTATUS
VnvmeHandleCreateNamespace(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleDeleteNamespace(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    );

static NTSTATUS
VnvmeHandleListNamespaces(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleGetConfig(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    );

static NTSTATUS
VnvmeHandleSetConfig(
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
    fdoContext->ControlQueue = queue;  // 保存队列用于优雅关闭
    
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
        
        case IOCTL_VNVME_MAP_SHM:
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
        
        case IOCTL_VNVME_GET_STATS:
            status = VnvmeHandleGetStats(Request, InputBufferLength, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_SET_DEBUG_LEVEL:
            status = VnvmeHandleSetDebugLevel(Request, InputBufferLength);
            break;
        
        case IOCTL_VNVME_UNMAP_SHM:
            status = VnvmeHandleUnmapShm(Request);
            break;
        
        case IOCTL_VNVME_CREATE_NAMESPACE:
            status = VnvmeHandleCreateNamespace(Request, InputBufferLength, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_DELETE_NAMESPACE:
            status = VnvmeHandleDeleteNamespace(Request, InputBufferLength);
            break;
        
        case IOCTL_VNVME_LIST_NAMESPACES:
            status = VnvmeHandleListNamespaces(Request, InputBufferLength, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_GET_CONFIG:
            status = VnvmeHandleGetConfig(Request, OutputBufferLength, &bytesReturned);
            break;
        
        case IOCTL_VNVME_SET_CONFIG:
            status = VnvmeHandleSetConfig(Request, InputBufferLength);
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
    
    // 计算活动命名空间数
    {
        PLIST_ENTRY entry;
        PVNVME_PDO_CONTEXT pdoContext;
        ULONG nsCount = 0;
        USHORT i;
        KIRQL oldIrql;
        
        KeAcquireSpinLock(&fdoContext->ChildDeviceListLock, &oldIrql);
        
        for (entry = fdoContext->ChildDeviceList.Flink;
             entry != &fdoContext->ChildDeviceList;
             entry = entry->Flink) {
            pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
            for (i = 0; i < VNVME_MAX_NAMESPACES; i++) {
                if (pdoContext->Namespaces[i].Active) {
                    nsCount++;
                }
            }
        }
        
        KeReleaseSpinLock(&fdoContext->ChildDeviceListLock, oldIrql);
        output->NamespaceCount = nsCount;
    }
    
    output->ShmMapped = (fdoContext->ShmUserVirtAddr != NULL) ? 1 : 0;
    output->ShmSize = (UINT32)fdoContext->ShmSize;
    output->CommandsProcessed = fdoContext->CommandsProcessed;
    output->ErrorCount = fdoContext->ErrorCount;
    
    *BytesReturned = sizeof(VNVME_GET_STATUS_OUTPUT);
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_MAP_SHM
 */
static NTSTATUS
VnvmeHandleMapShm(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_MAP_SHM_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    PVOID userAddress = NULL;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (OutputBufferLength < sizeof(VNVME_MAP_SHM_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(
        Request,
        sizeof(VNVME_MAP_SHM_OUTPUT),
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
    
    *BytesReturned = sizeof(VNVME_MAP_SHM_OUTPUT);
    
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
    
    // 计算待处理命令数 (简化: 通过 SQ Tail - Head 估算)
    {
        PLIST_ENTRY entry;
        PVNVME_PDO_CONTEXT pdoContext;
        ULONG pending = 0;
        USHORT i;
        KIRQL oldIrql;
        
        KeAcquireSpinLock(&fdoContext->ChildDeviceListLock, &oldIrql);
        
        for (entry = fdoContext->ChildDeviceList.Flink;
             entry != &fdoContext->ChildDeviceList;
             entry = entry->Flink) {
            pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
            
            // Admin SQ 待处理
            if (pdoContext->AdminSq.Tail >= pdoContext->AdminSq.Head) {
                pending += pdoContext->AdminSq.Tail - pdoContext->AdminSq.Head;
            }
            
            // I/O SQ 待处理
            for (i = 0; i < pdoContext->IoQueueCount; i++) {
                if (pdoContext->IoSq[i].Created) {
                    if (pdoContext->IoSq[i].Tail >= pdoContext->IoSq[i].Head) {
                        pending += pdoContext->IoSq[i].Tail - pdoContext->IoSq[i].Head;
                    }
                }
            }
        }
        
        KeReleaseSpinLock(&fdoContext->ChildDeviceListLock, oldIrql);
        output->PendingCommands = pending;
    }
    output->KernelTimestamp = fdoContext->LastHeartbeat.QuadPart;
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
        USHORT i;
        ULONGLONG capacity = 0;
        
        // 计算总容量
        for (i = 0; i < VNVME_MAX_NAMESPACES; i++) {
            if (pdoContext->Namespaces[i].Active) {
                capacity += pdoContext->Namespaces[i].TotalBlocks * pdoContext->Namespaces[i].BlockSize;
            }
        }
        
        info->ControllerId = pdoContext->ControllerId;
        info->Status = (pdoContext->CachedCC & 0x1) ? 1 : 0;  // 使用 CC.EN (位 0)
        info->NamespaceCount = pdoContext->NamespaceCount;
        info->TotalCapacity = capacity;
        
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
 * 实现方案:
 * - 使用 ObOpenObjectByPointer 获取内核事件的用户态句柄
 * - 返回可在用户态 WaitForSingleObject 等待的句柄
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
    HANDLE userHandle = NULL;
    
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
    
    // 如果还没有创建用户态句柄，现在创建
    if (fdoContext->UserEventHandle == NULL) {
        // 使用 ObOpenObjectByPointer 获取内核事件的用户态句柄
        status = ObOpenObjectByPointer(
            &fdoContext->CommandReadyEvent,
            0,                          // 非内核句柄
            NULL,                       // 无访问状态
            EVENT_ALL_ACCESS,           // 完全访问
            *ExEventObjectType,
            UserMode,                   // 用户态访问
            &userHandle
        );
        
        if (NT_SUCCESS(status)) {
            fdoContext->UserEventHandle = userHandle;
            fdoContext->EventNotificationEnabled = TRUE;
            TRACE_INFO("VnvmeHandleGetCommandEvent: Created user event handle 0x%p", userHandle);
        } else {
            TRACE_ERROR("VnvmeHandleGetCommandEvent: ObOpenObjectByPointer failed: 0x%08X", status);
            // 回退到轮询模式
            output->EventHandle = NULL;
            *BytesReturned = sizeof(VNVME_GET_COMMAND_EVENT_OUTPUT);
            return STATUS_SUCCESS;
        }
    }
    
    output->EventHandle = fdoContext->UserEventHandle;
    *BytesReturned = sizeof(VNVME_GET_COMMAND_EVENT_OUTPUT);
    
    TRACE_INFO("VnvmeHandleGetCommandEvent: Returning event handle 0x%p", output->EventHandle);
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
    ULONG targetControllerId;
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
    targetControllerId = input->ControllerId;
    
    if (completionCount == 0) {
        TRACE_WARN("VnvmeHandleSubmitCompletions: CompletionCount is 0");
        return STATUS_SUCCESS;
    }
    
    // P2: 验证完成数量上限，防止统计溢出或恶意输入
    if (completionCount > VNVME_MAX_COMPLETIONS_PER_SUBMIT) {
        TRACE_ERROR("VnvmeHandleSubmitCompletions: CompletionCount %lu exceeds max %lu",
                    completionCount, VNVME_MAX_COMPLETIONS_PER_SUBMIT);
        return STATUS_INVALID_PARAMETER;
    }
    
    // 遍历控制器列表
    // ControllerId == 0: 广播到所有控制器
    // ControllerId != 0: 只通知指定控制器
    KeAcquireSpinLock(&fdoContext->ChildDeviceListLock, &oldIrql);
    
    for (entry = fdoContext->ChildDeviceList.Flink;
         entry != &fdoContext->ChildDeviceList;
         entry = entry->Flink) {
        
        PVNVME_PDO_CONTEXT pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
        
        // 如果指定了控制器 ID，只处理匹配的控制器
        if (targetControllerId != 0 && pdoContext->ControllerId != targetControllerId) {
            continue;
        }
        
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
            
            // 如果是指定控制器模式，找到后即可退出
            if (targetControllerId != 0) {
                break;
            }
        }
    }
    
    KeReleaseSpinLock(&fdoContext->ChildDeviceListLock, oldIrql);
    
    TRACE_INFO("VnvmeHandleSubmitCompletions: Submitted %lu completions to %lu controllers (target=%lu)",
               completionCount, controllersNotified, targetControllerId);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 统计和调试 IOCTL 处理
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_GET_STATS
 * 
 * 返回控制器和命名空间的性能统计信息。
 */
static NTSTATUS
VnvmeHandleGetStats(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_GET_STATS_INPUT input = NULL;
    PVNVME_GET_STATS_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    KIRQL oldIrql;
    PLIST_ENTRY entry;
    ULONG ctrlIdx = 0;
    ULONG nsIdx = 0;
    ULONG targetControllerId = 0;
    LARGE_INTEGER currentTime;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (OutputBufferLength < sizeof(VNVME_GET_STATS_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    // 可选的输入参数
    if (InputBufferLength >= sizeof(VNVME_GET_STATS_INPUT)) {
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(VNVME_GET_STATS_INPUT),
                                               (PVOID*)&input, NULL);
        if (NT_SUCCESS(status) && input != NULL) {
            targetControllerId = input->ControllerId;
        }
    }
    
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VNVME_GET_STATS_OUTPUT),
                                            (PVOID*)&output, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    RtlZeroMemory(output, sizeof(VNVME_GET_STATS_OUTPUT));
    
    // 计算运行时间
    KeQuerySystemTime(&currentTime);
    output->Uptime = (UINT64)((currentTime.QuadPart - fdoContext->StartTime.QuadPart) / 10000);
    output->TotalCommandsProcessed = fdoContext->CommandsProcessed;
    
    // 遍历控制器收集统计
    KeAcquireSpinLock(&fdoContext->ChildDeviceListLock, &oldIrql);
    
    for (entry = fdoContext->ChildDeviceList.Flink;
         entry != &fdoContext->ChildDeviceList && ctrlIdx < VNVME_MAX_STATS_CONTROLLERS;
         entry = entry->Flink) {
        
        PVNVME_PDO_CONTEXT pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
        PVNVME_CONTROLLER_STATS ctrlStats;
        USHORT i;
        
        // 如果指定了控制器 ID，只收集匹配的
        if (targetControllerId != 0 && pdoContext->ControllerId != targetControllerId) {
            continue;
        }
        
        ctrlStats = &output->Controllers[ctrlIdx];
        ctrlStats->ControllerId = pdoContext->ControllerId;
        ctrlStats->NamespaceCount = pdoContext->NamespaceCount;
        ctrlStats->AdminCommandsProcessed = pdoContext->AdminCommandsProcessed;
        ctrlStats->IoCommandsProcessed = pdoContext->IoCommandsProcessed;
        ctrlStats->TotalReadBytes = pdoContext->BytesRead;
        ctrlStats->TotalWriteBytes = pdoContext->BytesWritten;
        ctrlStats->IoQueueCount = pdoContext->IoQueueCount;
        ctrlStats->PollingIntervalUs = pdoContext->PollingIntervalUs;
        
        ctrlIdx++;
        
        // 收集命名空间统计
        for (i = 0; i < VNVME_MAX_NAMESPACES && nsIdx < VNVME_MAX_STATS_NAMESPACES; i++) {
            PVNVME_NAMESPACE ns = &pdoContext->Namespaces[i];
            
            if (ns->Active) {
                PVNVME_NAMESPACE_STATS nsStats = &output->Namespaces[nsIdx];
                
                nsStats->NSID = ns->NsId;
                nsStats->Active = TRUE;
                nsStats->TotalBlocks = ns->TotalBlocks;
                nsStats->BlockSize = ns->BlockSize;
                nsStats->ReadCommands = ns->ReadCommands;
                nsStats->WriteCommands = ns->WriteCommands;
                nsStats->FlushCommands = ns->FlushCommands;
                nsStats->ReadBytes = ns->ReadBytes;
                nsStats->WriteBytes = ns->WriteBytes;
                
                nsIdx++;
                output->TotalNamespaceCount++;
            }
        }
    }
    
    KeReleaseSpinLock(&fdoContext->ChildDeviceListLock, oldIrql);
    
    output->ControllerCount = ctrlIdx;
    *BytesReturned = sizeof(VNVME_GET_STATS_OUTPUT);
    
    TRACE_INFO("VnvmeHandleGetStats: Returned %lu controllers, %lu namespaces",
               ctrlIdx, nsIdx);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_SET_DEBUG_LEVEL
 * 
 * 动态调整调试输出级别。
 */
static NTSTATUS
VnvmeHandleSetDebugLevel(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVNVME_SET_DEBUG_LEVEL_INPUT input;
    
    if (InputBufferLength < sizeof(VNVME_SET_DEBUG_LEVEL_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VNVME_SET_DEBUG_LEVEL_INPUT),
                                           (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // P2: 验证调试级别范围
    if (input->DebugLevel > VNVME_MAX_DEBUG_LEVEL) {
        TRACE_WARN("VnvmeHandleSetDebugLevel: DebugLevel %u exceeds max %u, clamping",
                   input->DebugLevel, VNVME_MAX_DEBUG_LEVEL);
        g_DebugLevel = VNVME_MAX_DEBUG_LEVEL;
    } else {
        g_DebugLevel = input->DebugLevel;
    }
    
    g_DebugFlags = input->DebugFlags;
    
    TRACE_INFO("VnvmeHandleSetDebugLevel: Level=%u, Flags=0x%08X",
               g_DebugLevel, input->DebugFlags);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 内部辅助函数
//===========================================================================

/**
 * @brief 根据控制器 ID 查找 PDO 上下文
 */
static PVNVME_PDO_CONTEXT
CtrlDevFindController(
    _In_ PVNVME_FDO_CONTEXT FdoContext,
    _In_ ULONG ControllerId
    )
{
    PLIST_ENTRY entry;
    PVNVME_PDO_CONTEXT pdoContext;
    KIRQL oldIrql;
    
    KeAcquireSpinLock(&FdoContext->ChildDeviceListLock, &oldIrql);
    
    for (entry = FdoContext->ChildDeviceList.Flink;
         entry != &FdoContext->ChildDeviceList;
         entry = entry->Flink) {
        
        pdoContext = CONTAINING_RECORD(entry, VNVME_PDO_CONTEXT, ListEntry);
        if (pdoContext->ControllerId == ControllerId) {
            KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
            return pdoContext;
        }
    }
    
    KeReleaseSpinLock(&FdoContext->ChildDeviceListLock, oldIrql);
    return NULL;
}

//===========================================================================
// 共享内存取消映射
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_UNMAP_SHM
 */
static NTSTATUS
VnvmeHandleUnmapShm(
    _In_ WDFREQUEST Request
    )
{
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    
    UNREFERENCED_PARAMETER(Request);
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    // 共享内存会在进程退出时自动取消映射
    // 这里主要用于测试模式下的显式清理
    
    TRACE_INFO("VnvmeHandleUnmapShm: Request received (no-op in kernel, user should unmap via VirtualFree)");
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 命名空间管理 IOCTL
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_CREATE_NAMESPACE
 */
static NTSTATUS
VnvmeHandleCreateNamespace(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_CREATE_NAMESPACE_INPUT input;
    PVNVME_CREATE_NAMESPACE_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    PVNVME_PDO_CONTEXT pdoContext;
    UINT32 nsid;
    UINT32 i;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_CREATE_NAMESPACE_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    if (OutputBufferLength < sizeof(VNVME_CREATE_NAMESPACE_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VNVME_CREATE_NAMESPACE_INPUT),
                                           (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VNVME_CREATE_NAMESPACE_OUTPUT),
                                            (PVOID*)&output, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 查找控制器
    pdoContext = CtrlDevFindController(fdoContext, input->ControllerId);
    if (pdoContext == NULL) {
        TRACE_ERROR("VnvmeHandleCreateNamespace: Controller %lu not found", input->ControllerId);
        return STATUS_NOT_FOUND;
    }
    
    // P2: 验证块大小 (只允许 512 或 4096)
    {
        UINT32 blockSize = input->Config.BlockSize;
        if (blockSize != 0 && blockSize != VNVME_BLOCK_SIZE_512 && blockSize != VNVME_BLOCK_SIZE_4096) {
            TRACE_ERROR("VnvmeHandleCreateNamespace: Invalid block size %lu (must be 512 or 4096)",
                        blockSize);
            return STATUS_INVALID_PARAMETER;
        }
    }
    
    // P2: 验证总块数 (不能为零，且总容量不能超过上限)
    if (input->Config.TotalBlocks == 0) {
        TRACE_ERROR("VnvmeHandleCreateNamespace: TotalBlocks cannot be 0");
        return STATUS_INVALID_PARAMETER;
    }
    
    {
        UINT64 blockSize = input->Config.BlockSize ? input->Config.BlockSize : VNVME_BLOCK_SIZE_512;
        UINT64 totalCapacity = input->Config.TotalBlocks * blockSize;
        
        // 检查溢出
        if (totalCapacity / blockSize != input->Config.TotalBlocks) {
            TRACE_ERROR("VnvmeHandleCreateNamespace: Capacity overflow");
            return STATUS_INTEGER_OVERFLOW;
        }
        
        if (totalCapacity > VNVME_MAX_NAMESPACE_CAPACITY) {
            TRACE_ERROR("VnvmeHandleCreateNamespace: Total capacity %llu exceeds max %llu",
                        totalCapacity, VNVME_MAX_NAMESPACE_CAPACITY);
            return STATUS_INVALID_PARAMETER;
        }
    }
    
    // 检查命名空间数量上限
    if (pdoContext->NamespaceCount >= VNVME_MAX_NAMESPACES) {
        TRACE_ERROR("VnvmeHandleCreateNamespace: Max namespaces reached");
        return STATUS_QUOTA_EXCEEDED;
    }
    
    // 分配 NSID (1-based)
    nsid = 0;
    for (i = 0; i < VNVME_MAX_NAMESPACES; i++) {
        if (!pdoContext->Namespaces[i].Active) {
            nsid = i + 1;  // NSID 从 1 开始
            break;
        }
    }
    
    if (nsid == 0) {
        return STATUS_QUOTA_EXCEEDED;
    }
    
    // 初始化命名空间
    PVNVME_NAMESPACE ns = &pdoContext->Namespaces[nsid - 1];
    RtlZeroMemory(ns, sizeof(VNVME_NAMESPACE));
    
    ns->NsId = nsid;
    ns->TotalBlocks = input->Config.TotalBlocks;
    ns->BlockSize = input->Config.BlockSize ? input->Config.BlockSize : 512;
    ns->TotalBytes = ns->TotalBlocks * ns->BlockSize;
    ns->ReadOnly = (input->Config.Flags & VNVME_NS_FLAG_READONLY) ? TRUE : FALSE;
    ns->ThinProvisioned = (input->Config.Flags & VNVME_NS_FLAG_SPARSE) ? TRUE : FALSE;
    
    // 创建存储后端
    {
        VNVME_STORAGE_TYPE storageType;
        UNICODE_STRING storagePath;
        PUNICODE_STRING pStoragePath = NULL;
        
        // 根据配置和 namespace 标志选择存储类型
        if (ns->ThinProvisioned) {
            storageType = VNVME_STORAGE_TYPE_SPARSE;
        } else {
            storageType = (VNVME_STORAGE_TYPE)CONFIG_STORAGE_TYPE;
        }
        
        // 如果是文件后端，需要路径
        if (storageType == VNVME_STORAGE_TYPE_FILE || 
            storageType == VNVME_STORAGE_TYPE_SPARSE) {
            if (CONFIG_STORAGE_PATH[0] != L'\0') {
                RtlInitUnicodeString(&storagePath, CONFIG_STORAGE_PATH);
                pStoragePath = &storagePath;
            } else {
                // 没有配置路径，回退到内存后端
                TRACE_WARN("VnvmeHandleCreateNamespace: No storage path configured, using memory backend");
                storageType = VNVME_STORAGE_TYPE_MEMORY;
            }
        }
        
        status = VnvmeStorageCreate(
            &ns->Storage,
            storageType,
            ns->TotalBytes,
            ns->BlockSize,
            pStoragePath
            );
        
        if (!NT_SUCCESS(status)) {
            TRACE_ERROR("VnvmeHandleCreateNamespace: VnvmeStorageCreate failed, status=0x%08X", status);
            return status;
        }
    }
    
    ns->Active = TRUE;
    pdoContext->NamespaceCount++;
    
    // 填充输出
    output->NSID = nsid;
    output->Reserved = 0;
    *BytesReturned = sizeof(VNVME_CREATE_NAMESPACE_OUTPUT);
    
    TRACE_INFO("VnvmeHandleCreateNamespace: Created NSID=%lu on Controller=%lu, Blocks=%llu, BlockSize=%lu, Storage=%p",
               nsid, input->ControllerId, input->Config.TotalBlocks, ns->BlockSize, ns->Storage);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_DELETE_NAMESPACE
 */
static NTSTATUS
VnvmeHandleDeleteNamespace(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVNVME_DELETE_NAMESPACE_INPUT input;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    PVNVME_PDO_CONTEXT pdoContext;
    PVNVME_NAMESPACE ns;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_DELETE_NAMESPACE_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VNVME_DELETE_NAMESPACE_INPUT),
                                           (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 查找控制器
    pdoContext = CtrlDevFindController(fdoContext, input->ControllerId);
    if (pdoContext == NULL) {
        TRACE_ERROR("VnvmeHandleDeleteNamespace: Controller %lu not found", input->ControllerId);
        return STATUS_NOT_FOUND;
    }
    
    // 验证 NSID
    if (input->NSID == 0 || input->NSID > VNVME_MAX_NAMESPACES) {
        TRACE_ERROR("VnvmeHandleDeleteNamespace: Invalid NSID %lu", input->NSID);
        return STATUS_INVALID_PARAMETER;
    }
    
    ns = &pdoContext->Namespaces[input->NSID - 1];
    if (!ns->Active) {
        TRACE_ERROR("VnvmeHandleDeleteNamespace: NSID %lu not active", input->NSID);
        return STATUS_NOT_FOUND;
    }
    
    // 清理存储后端
    if (ns->Storage != NULL) {
        VnvmeStorageDestroy(ns->Storage);
        ns->Storage = NULL;
    }
    
    ns->Active = FALSE;
    pdoContext->NamespaceCount--;
    
    TRACE_INFO("VnvmeHandleDeleteNamespace: Deleted NSID=%lu from Controller=%lu",
               input->NSID, input->ControllerId);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_LIST_NAMESPACES
 */
static NTSTATUS
VnvmeHandleListNamespaces(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_LIST_NAMESPACES_INPUT input;
    PVNVME_LIST_NAMESPACES_OUTPUT output;
    PVNVME_FDO_CONTEXT fdoContext = g_FdoContext;
    PVNVME_PDO_CONTEXT pdoContext;
    UINT32 count = 0;
    UINT32 i;
    
    *BytesReturned = 0;
    
    if (fdoContext == NULL) {
        return STATUS_DEVICE_NOT_READY;
    }
    
    if (InputBufferLength < sizeof(VNVME_LIST_NAMESPACES_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    if (OutputBufferLength < sizeof(VNVME_LIST_NAMESPACES_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VNVME_LIST_NAMESPACES_INPUT),
                                           (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VNVME_LIST_NAMESPACES_OUTPUT),
                                            (PVOID*)&output, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    RtlZeroMemory(output, sizeof(VNVME_LIST_NAMESPACES_OUTPUT));
    
    // 查找控制器
    pdoContext = CtrlDevFindController(fdoContext, input->ControllerId);
    if (pdoContext == NULL) {
        TRACE_ERROR("VnvmeHandleListNamespaces: Controller %lu not found", input->ControllerId);
        return STATUS_NOT_FOUND;
    }
    
    // 枚举命名空间
    for (i = 0; i < VNVME_MAX_NAMESPACES && count < VNVME_MAX_NAMESPACES; i++) {
        PVNVME_NAMESPACE ns = &pdoContext->Namespaces[i];
        if (ns->Active) {
            PVNVME_NAMESPACE_INFO info = &output->Namespaces[count];
            
            info->NSID = ns->NsId;
            info->Flags = 0;
            if (ns->Active) info->Flags |= VNVME_NS_FLAG_ENABLED;
            if (ns->ReadOnly) info->Flags |= VNVME_NS_FLAG_READONLY;
            if (ns->ThinProvisioned) info->Flags |= VNVME_NS_FLAG_SPARSE;
            info->TotalBlocks = ns->TotalBlocks;
            info->BlockSize = ns->BlockSize;
            info->Reserved = 0;
            
            count++;
        }
    }
    
    output->Count = count;
    *BytesReturned = sizeof(VNVME_LIST_NAMESPACES_OUTPUT);
    
    TRACE_INFO("VnvmeHandleListNamespaces: Listed %lu namespaces for Controller=%lu",
               count, input->ControllerId);
    
    return STATUS_SUCCESS;
}

//===========================================================================
// 配置管理 IOCTL
//===========================================================================

/**
 * @brief 处理 IOCTL_VNVME_GET_CONFIG
 * 
 * 获取当前驱动配置。
 */
static NTSTATUS
VnvmeHandleGetConfig(
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _Out_ size_t* BytesReturned
    )
{
    NTSTATUS status;
    PVNVME_GET_CONFIG_OUTPUT output;
    VNVME_CONFIG config;
    
    *BytesReturned = 0;
    
    if (OutputBufferLength < sizeof(VNVME_GET_CONFIG_OUTPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveOutputBuffer(Request, sizeof(VNVME_GET_CONFIG_OUTPUT),
                                            (PVOID*)&output, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    // 获取当前配置
    VnvmeConfigGet(&config);
    
    RtlZeroMemory(output, sizeof(VNVME_GET_CONFIG_OUTPUT));
    
    // 复制到 IOCTL 输出结构
    output->Config.DebugLevel = config.DebugLevel;
    output->Config.DebugFlags = config.DebugFlags;
    output->Config.HeartbeatTimeoutMs = (UINT32)(config.HeartbeatTimeout100ns / 10000);
    
    output->Config.StorageType = (UINT32)config.StorageType;
    output->Config.StorageSizeGB = config.StorageSizeGB;
    RtlCopyMemory(output->Config.StoragePath, config.StoragePath, 
                  sizeof(output->Config.StoragePath));
    
    output->Config.MaxIOQueues = config.MaxIOQueues;
    output->Config.AdminQueueDepth = config.AdminQueueDepth;
    output->Config.IOQueueDepth = config.IOQueueDepth;
    
    output->Config.DoorbellPollIntervalUs = config.DoorbellPollIntervalUs;
    output->Config.BatchSize = config.BatchSize;
    
    output->Config.AllowUserModeAccess = config.AllowUserModeAccess ? 1 : 0;
    output->Config.RequireAdminPrivilege = config.RequireAdminPrivilege ? 1 : 0;
    
    // 指示可动态修改的字段
    output->DynamicFieldMask = VNVME_CONFIG_FIELD_ALL_DYNAMIC;
    
    *BytesReturned = sizeof(VNVME_GET_CONFIG_OUTPUT);
    
    TRACE_INFO("VnvmeHandleGetConfig: Returned config (DebugLevel=%lu, PollInterval=%luus)",
               output->Config.DebugLevel, output->Config.DoorbellPollIntervalUs);
    
    return STATUS_SUCCESS;
}

/**
 * @brief 处理 IOCTL_VNVME_SET_CONFIG
 * 
 * 设置可动态修改的配置字段。
 */
static NTSTATUS
VnvmeHandleSetConfig(
    _In_ WDFREQUEST Request,
    _In_ size_t InputBufferLength
    )
{
    NTSTATUS status;
    PVNVME_SET_CONFIG_INPUT input;
    VNVME_CONFIG newConfig;
    UINT32 fieldMask;
    
    if (InputBufferLength < sizeof(VNVME_SET_CONFIG_INPUT)) {
        return STATUS_BUFFER_TOO_SMALL;
    }
    
    status = WdfRequestRetrieveInputBuffer(Request, sizeof(VNVME_SET_CONFIG_INPUT),
                                           (PVOID*)&input, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    
    fieldMask = input->FieldMask;
    
    // 仅允许修改动态字段
    if (fieldMask & ~VNVME_CONFIG_FIELD_ALL_DYNAMIC) {
        TRACE_WARN("VnvmeHandleSetConfig: Attempt to modify read-only fields (mask=0x%08X)", fieldMask);
        return STATUS_ACCESS_DENIED;
    }
    
    // 获取当前配置作为基础
    VnvmeConfigGet(&newConfig);
    
    // 根据 FieldMask 更新请求的字段
    if (fieldMask & VNVME_CONFIG_FIELD_DEBUG_LEVEL) {
        newConfig.DebugLevel = input->Config.DebugLevel;
    }
    if (fieldMask & VNVME_CONFIG_FIELD_DEBUG_FLAGS) {
        newConfig.DebugFlags = input->Config.DebugFlags;
    }
    if (fieldMask & VNVME_CONFIG_FIELD_HEARTBEAT) {
        newConfig.HeartbeatTimeout100ns = (LONGLONG)input->Config.HeartbeatTimeoutMs * 10000;
    }
    if (fieldMask & VNVME_CONFIG_FIELD_POLL_INTERVAL) {
        newConfig.DoorbellPollIntervalUs = input->Config.DoorbellPollIntervalUs;
    }
    if (fieldMask & VNVME_CONFIG_FIELD_BATCH_SIZE) {
        newConfig.BatchSize = input->Config.BatchSize;
    }
    
    // 应用更新
    status = VnvmeConfigUpdate(&newConfig);
    if (!NT_SUCCESS(status)) {
        TRACE_ERROR("VnvmeHandleSetConfig: VnvmeConfigUpdate failed 0x%08X", status);
        return status;
    }
    
    TRACE_INFO("VnvmeHandleSetConfig: Updated config (mask=0x%08X)", fieldMask);
    
    return STATUS_SUCCESS;
}