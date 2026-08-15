/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  Test Helpers for VNVME Unit Tests
 */

#pragma once

#include <ddk.h>

/*
 * Control Device Name
 */
#define VNVME_CONTROL_DEVICE_NAME   L"\\Device\\VNvmeControl"
#define VNVME_SYMBOLIC_LINK_NAME    L"\\\\.\\VNvmeControl"

/*
 * Test Helper Macros
 */
#define VNVME_TEST_ASSERT_SUCCESS(status) \
    Assert::IsTrue(NT_SUCCESS(status), L"Expected NT_SUCCESS")

#define VNVME_TEST_ASSERT_FAILURE(status) \
    Assert::IsFalse(NT_SUCCESS(status), L"Expected failure status")

/*
 * Test Context Structure
 */
typedef struct _VNVME_TEST_CONTEXT {
    PDEVICE_OBJECT  ControlDevice;
    PFILE_OBJECT    FileObject;
    KEVENT          Event;
    BOOLEAN         Initialized;
} VNVME_TEST_CONTEXT, *PVNVME_TEST_CONTEXT;

/*
 * Initialize test context
 */
static inline NTSTATUS
VNvmeTestInitContext(
    _Out_ PVNVME_TEST_CONTEXT Context
    )
{
    UNICODE_STRING deviceName;
    NTSTATUS status;

    RtlZeroMemory(Context, sizeof(VNVME_TEST_CONTEXT));
    KeInitializeEvent(&Context->Event, NotificationEvent, FALSE);

    RtlInitUnicodeString(&deviceName, VNVME_CONTROL_DEVICE_NAME);

    status = IoGetDeviceObjectPointer(
        &deviceName,
        FILE_ALL_ACCESS,
        &Context->FileObject,
        &Context->ControlDevice
    );

    if (NT_SUCCESS(status)) {
        Context->Initialized = TRUE;
    }

    return status;
}

/*
 * Cleanup test context
 */
static inline VOID
VNvmeTestCleanupContext(
    _Inout_ PVNVME_TEST_CONTEXT Context
    )
{
    if (Context->FileObject) {
        ObDereferenceObject(Context->FileObject);
        Context->FileObject = NULL;
    }
    Context->ControlDevice = NULL;
    Context->Initialized = FALSE;
}

/*
 * Send IOCTL to driver
 */
static inline NTSTATUS
VNvmeTestSendIoctl(
    _In_ PVNVME_TEST_CONTEXT Context,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PULONG_PTR BytesReturned
    )
{
    IO_STATUS_BLOCK ioStatus;
    PIRP irp;
    NTSTATUS status;

    if (!Context->Initialized) {
        return STATUS_INVALID_DEVICE_STATE;
    }

    KeClearEvent(&Context->Event);

    irp = IoBuildDeviceIoControlRequest(
        IoControlCode,
        Context->ControlDevice,
        InputBuffer,
        InputBufferLength,
        OutputBuffer,
        OutputBufferLength,
        FALSE,
        &Context->Event,
        &ioStatus
    );

    if (!irp) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    status = IoCallDriver(Context->ControlDevice, irp);

    if (status == STATUS_PENDING) {
        KeWaitForSingleObject(
            &Context->Event,
            Executive,
            KernelMode,
            FALSE,
            NULL
        );
        status = ioStatus.Status;
    }

    if (BytesReturned) {
        *BytesReturned = ioStatus.Information;
    }

    return status;
}
