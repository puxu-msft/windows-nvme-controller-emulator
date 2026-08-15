/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  Controller Management Unit Tests
 */

#include "stdafx.h"


namespace VNVMEUnitTest
{
    /*
     * Controller Management Tests
     * Tests for NVMe controller create/delete operations
     */
    TEST_CLASS(ControllerTest)
    {
        VNVME_TEST_CONTEXT ctx;

        TEST_METHOD_INITIALIZE(TestInit)
        {
            DdkThreadInit();
            NTSTATUS status = VNvmeTestInitContext(&ctx);
            Assert::IsTrue(NT_SUCCESS(status), L"Failed to initialize test context");
        }

        TEST_METHOD_CLEANUP(TestCleanup)
        {
            VNvmeTestCleanupContext(&ctx);
        }

        /*
         * Test: List controllers (initially empty)
         */
        TEST_METHOD(ListControllersEmpty)
        {
            VNVME_CONTROLLER_LIST list = {0};
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_LIST_CONTROLLERS,
                NULL, 0,
                &list, sizeof(list),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"LIST_CONTROLLERS should succeed");
            Assert::AreEqual((ULONG)0, list.Count, L"Initial controller count should be 0");
        }

        /*
         * Test: Create controller with valid parameters
         */
        TEST_METHOD(CreateControllerValid)
        {
            VNVME_CREATE_CONTROLLER_INPUT input = {0};
            VNVME_CREATE_CONTROLLER_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            /* Setup valid controller parameters */
            input.SerialNumber[0] = 'V';
            input.SerialNumber[1] = 'N';
            input.SerialNumber[2] = 'V';
            input.SerialNumber[3] = 'M';
            input.SerialNumber[4] = 'E';
            input.SerialNumber[5] = '0';
            input.SerialNumber[6] = '0';
            input.SerialNumber[7] = '1';
            input.MaxNamespaces = 16;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_CONTROLLER,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"CREATE_CONTROLLER should succeed");
            Assert::IsTrue(output.ControllerId > 0, L"Controller ID should be assigned");

            /* Cleanup: Delete the controller */
            VNVME_DELETE_CONTROLLER_INPUT deleteInput = {0};
            deleteInput.ControllerId = output.ControllerId;
            VNvmeTestSendIoctl(&ctx, IOCTL_VNVME_DELETE_CONTROLLER,
                &deleteInput, sizeof(deleteInput), NULL, 0, NULL);
        }

        /*
         * Test: Create controller with invalid parameters
         */
        TEST_METHOD(CreateControllerInvalid)
        {
            VNVME_CREATE_CONTROLLER_INPUT input = {0};
            VNVME_CREATE_CONTROLLER_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            /* Invalid: MaxNamespaces = 0 */
            input.MaxNamespaces = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_CONTROLLER,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::IsFalse(NT_SUCCESS(status), L"Invalid controller params should fail");
        }

        /*
         * Test: Delete non-existent controller
         */
        TEST_METHOD(DeleteNonExistentController)
        {
            VNVME_DELETE_CONTROLLER_INPUT input = {0};
            ULONG_PTR bytesReturned = 0;

            /* Use an ID that doesn't exist */
            input.ControllerId = 0xFFFFFFFF;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_DELETE_CONTROLLER,
                &input, sizeof(input),
                NULL, 0,
                &bytesReturned
            );

            Assert::AreEqual(STATUS_NOT_FOUND, status, 
                L"Deleting non-existent controller should return STATUS_NOT_FOUND");
        }

        /*
         * Test: Create multiple controllers
         */
        TEST_METHOD(CreateMultipleControllers)
        {
            const ULONG NUM_CONTROLLERS = 3;
            ULONG controllerIds[NUM_CONTROLLERS] = {0};
            NTSTATUS status;

            /* Create multiple controllers */
            for (ULONG i = 0; i < NUM_CONTROLLERS; i++) {
                VNVME_CREATE_CONTROLLER_INPUT input = {0};
                VNVME_CREATE_CONTROLLER_OUTPUT output = {0};
                ULONG_PTR bytesReturned = 0;

                input.SerialNumber[0] = 'T';
                input.SerialNumber[1] = 'E';
                input.SerialNumber[2] = 'S';
                input.SerialNumber[3] = 'T';
                input.SerialNumber[4] = (CHAR)('0' + i);
                input.MaxNamespaces = 4;

                status = VNvmeTestSendIoctl(
                    &ctx,
                    IOCTL_VNVME_CREATE_CONTROLLER,
                    &input, sizeof(input),
                    &output, sizeof(output),
                    &bytesReturned
                );

                Assert::IsTrue(NT_SUCCESS(status), L"CREATE_CONTROLLER should succeed");
                controllerIds[i] = output.ControllerId;
            }

            /* Verify list shows all controllers */
            VNVME_CONTROLLER_LIST list = {0};
            ULONG_PTR bytesReturned = 0;

            status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_LIST_CONTROLLERS,
                NULL, 0,
                &list, sizeof(list),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"LIST_CONTROLLERS should succeed");
            Assert::AreEqual(NUM_CONTROLLERS, list.Count, L"Controller count mismatch");

            /* Cleanup: Delete all controllers */
            for (ULONG i = 0; i < NUM_CONTROLLERS; i++) {
                VNVME_DELETE_CONTROLLER_INPUT deleteInput = {0};
                deleteInput.ControllerId = controllerIds[i];
                VNvmeTestSendIoctl(&ctx, IOCTL_VNVME_DELETE_CONTROLLER,
                    &deleteInput, sizeof(deleteInput), NULL, 0, NULL);
            }
        }
    };
}
