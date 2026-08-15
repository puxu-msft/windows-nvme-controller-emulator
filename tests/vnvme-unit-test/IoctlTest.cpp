/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  IOCTL Interface Unit Tests
 */

#include "stdafx.h"


namespace VNVMEUnitTest
{
    /*
     * IOCTL Interface Tests
     * Tests for the VNVME IOCTL dispatch handling
     */
    TEST_CLASS(IoctlTest)
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
         * Test: Invalid IOCTL code
         */
        TEST_METHOD(InvalidIoctlCode)
        {
            UCHAR buffer[64] = {0};
            ULONG_PTR bytesReturned = 0;

            /* Use an invalid IOCTL code */
            ULONG invalidCode = CTL_CODE(FILE_DEVICE_VNVME, 0xFFF, METHOD_BUFFERED, FILE_ANY_ACCESS);

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                invalidCode,
                NULL, 0,
                buffer, sizeof(buffer),
                &bytesReturned
            );

            Assert::AreEqual(STATUS_INVALID_DEVICE_REQUEST, status, 
                L"Invalid IOCTL should return STATUS_INVALID_DEVICE_REQUEST");
        }

        /*
         * Test: IOCTL with insufficient output buffer
         */
        TEST_METHOD(InsufficientOutputBuffer)
        {
            UCHAR buffer[4] = {0};  /* Too small */
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_GET_VERSION,
                NULL, 0,
                buffer, sizeof(buffer),
                &bytesReturned
            );

            Assert::AreEqual(STATUS_BUFFER_TOO_SMALL, status,
                L"Small buffer should return STATUS_BUFFER_TOO_SMALL");
        }

        /*
         * Test: IOCTL with NULL input where required
         */
        TEST_METHOD(MissingRequiredInput)
        {
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_SET_CONFIG,
                NULL, 0,  /* No input provided */
                NULL, 0,
                &bytesReturned
            );

            Assert::IsFalse(NT_SUCCESS(status), 
                L"Missing required input should fail");
        }

        /*
         * Test: Debug level IOCTL
         */
        TEST_METHOD(SetDebugLevel)
        {
            ULONG debugLevel = 3;
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_SET_DEBUG_LEVEL,
                &debugLevel, sizeof(debugLevel),
                NULL, 0,
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"SET_DEBUG_LEVEL should succeed");
        }

        /*
         * Test: Get statistics
         */
        TEST_METHOD(GetStatistics)
        {
            VNVME_STATS stats = {0};
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_GET_STATS,
                NULL, 0,
                &stats, sizeof(stats),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"GET_STATS should succeed");
        }

        /*
         * Test: Reset statistics
         */
        TEST_METHOD(ResetStatistics)
        {
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_RESET_STATS,
                NULL, 0,
                NULL, 0,
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"RESET_STATS should succeed");
        }
    };
}
