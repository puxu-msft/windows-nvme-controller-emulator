/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  Namespace Management Unit Tests
 */

#include "stdafx.h"


namespace VNVMEUnitTest
{
    /*
     * Namespace Management Tests
     * Tests for NVMe namespace create/delete operations
     */
    TEST_CLASS(NamespaceTest)
    {
        VNVME_TEST_CONTEXT ctx;
        ULONG testControllerId;

        TEST_METHOD_INITIALIZE(TestInit)
        {
            DdkThreadInit();
            testControllerId = 0;

            NTSTATUS status = VNvmeTestInitContext(&ctx);
            Assert::IsTrue(NT_SUCCESS(status), L"Failed to initialize test context");

            /* Create a test controller for namespace tests */
            VNVME_CREATE_CONTROLLER_INPUT input = {0};
            VNVME_CREATE_CONTROLLER_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            input.SerialNumber[0] = 'N';
            input.SerialNumber[1] = 'S';
            input.SerialNumber[2] = 'T';
            input.SerialNumber[3] = 'S';
            input.SerialNumber[4] = 'T';
            input.MaxNamespaces = 16;

            status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_CONTROLLER,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            if (NT_SUCCESS(status)) {
                testControllerId = output.ControllerId;
            }
        }

        TEST_METHOD_CLEANUP(TestCleanup)
        {
            /* Delete test controller */
            if (testControllerId > 0) {
                VNVME_DELETE_CONTROLLER_INPUT deleteInput = {0};
                deleteInput.ControllerId = testControllerId;
                VNvmeTestSendIoctl(&ctx, IOCTL_VNVME_DELETE_CONTROLLER,
                    &deleteInput, sizeof(deleteInput), NULL, 0, NULL);
            }

            VNvmeTestCleanupContext(&ctx);
        }

        /*
         * Test: List namespaces (initially empty)
         */
        TEST_METHOD(ListNamespacesEmpty)
        {
            VNVME_LIST_NAMESPACES_INPUT input = {0};
            VNVME_NAMESPACE_LIST list = {0};
            ULONG_PTR bytesReturned = 0;

            input.ControllerId = testControllerId;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_LIST_NAMESPACES,
                &input, sizeof(input),
                &list, sizeof(list),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"LIST_NAMESPACES should succeed");
            Assert::AreEqual((ULONG)0, list.Count, L"Initial namespace count should be 0");
        }

        /*
         * Test: Create namespace with valid parameters
         */
        TEST_METHOD(CreateNamespaceValid)
        {
            VNVME_CREATE_NAMESPACE_INPUT input = {0};
            VNVME_CREATE_NAMESPACE_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            input.ControllerId = testControllerId;
            input.SizeInBlocks = 1024 * 1024;  /* 1M blocks */
            input.BlockSize = 512;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_NAMESPACE,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"CREATE_NAMESPACE should succeed");
            Assert::IsTrue(output.NamespaceId > 0, L"Namespace ID should be assigned");

            /* Cleanup */
            VNVME_DELETE_NAMESPACE_INPUT deleteInput = {0};
            deleteInput.ControllerId = testControllerId;
            deleteInput.NamespaceId = output.NamespaceId;
            VNvmeTestSendIoctl(&ctx, IOCTL_VNVME_DELETE_NAMESPACE,
                &deleteInput, sizeof(deleteInput), NULL, 0, NULL);
        }

        /*
         * Test: Create namespace with invalid size
         */
        TEST_METHOD(CreateNamespaceInvalidSize)
        {
            VNVME_CREATE_NAMESPACE_INPUT input = {0};
            VNVME_CREATE_NAMESPACE_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            input.ControllerId = testControllerId;
            input.SizeInBlocks = 0;  /* Invalid: zero size */
            input.BlockSize = 512;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_NAMESPACE,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::IsFalse(NT_SUCCESS(status), L"Zero-size namespace should fail");
        }

        /*
         * Test: Create namespace with invalid block size
         */
        TEST_METHOD(CreateNamespaceInvalidBlockSize)
        {
            VNVME_CREATE_NAMESPACE_INPUT input = {0};
            VNVME_CREATE_NAMESPACE_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            input.ControllerId = testControllerId;
            input.SizeInBlocks = 1024;
            input.BlockSize = 123;  /* Invalid: not power of 2 */

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_NAMESPACE,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::IsFalse(NT_SUCCESS(status), L"Invalid block size should fail");
        }

        /*
         * Test: Delete non-existent namespace
         */
        TEST_METHOD(DeleteNonExistentNamespace)
        {
            VNVME_DELETE_NAMESPACE_INPUT input = {0};
            ULONG_PTR bytesReturned = 0;

            input.ControllerId = testControllerId;
            input.NamespaceId = 0xFFFFFFFF;  /* Non-existent */

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_DELETE_NAMESPACE,
                &input, sizeof(input),
                NULL, 0,
                &bytesReturned
            );

            Assert::AreEqual(STATUS_NOT_FOUND, status,
                L"Deleting non-existent namespace should return STATUS_NOT_FOUND");
        }

        /*
         * Test: Create namespace on non-existent controller
         */
        TEST_METHOD(CreateNamespaceInvalidController)
        {
            VNVME_CREATE_NAMESPACE_INPUT input = {0};
            VNVME_CREATE_NAMESPACE_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            input.ControllerId = 0xFFFFFFFF;  /* Non-existent controller */
            input.SizeInBlocks = 1024;
            input.BlockSize = 512;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_CREATE_NAMESPACE,
                &input, sizeof(input),
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::AreEqual(STATUS_NOT_FOUND, status,
                L"Creating namespace on invalid controller should fail");
        }
    };
}
