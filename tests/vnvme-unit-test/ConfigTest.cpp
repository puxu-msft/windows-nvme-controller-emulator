/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  Configuration Module Unit Tests
 */

#include "stdafx.h"


namespace VNVMEUnitTest
{
    /*
     * Configuration Module Tests
     * Tests for the VNVME configuration system (config.h/config.c)
     */
    TEST_CLASS(ConfigurationTest)
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
         * Test: Get current configuration
         */
        TEST_METHOD(GetConfiguration)
        {
            VNVME_CONFIG_IOCTL_OUTPUT output = {0};
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_GET_CONFIG,
                NULL, 0,
                &output, sizeof(output),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"GET_CONFIG IOCTL failed");
            Assert::AreEqual(sizeof(output), bytesReturned, L"Unexpected output size");
            
            /* Verify default configuration values */
            Assert::IsTrue(output.Storage.MaxDevices > 0, L"MaxDevices should be > 0");
            Assert::IsTrue(output.Queue.MaxQueueEntries > 0, L"MaxQueueEntries should be > 0");
        }

        /*
         * Test: Get version information
         */
        TEST_METHOD(GetVersion)
        {
            VNVME_VERSION_INFO versionInfo = {0};
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_GET_VERSION,
                NULL, 0,
                &versionInfo, sizeof(versionInfo),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"GET_VERSION IOCTL failed");
            Assert::IsTrue(versionInfo.Major > 0 || versionInfo.Minor > 0, 
                L"Version should be non-zero");
        }

        /*
         * Test: Set configuration with valid parameters
         */
        TEST_METHOD(SetConfigurationValid)
        {
            VNVME_CONFIG_IOCTL_INPUT input = {0};
            ULONG_PTR bytesReturned = 0;

            /* Set a valid queue configuration */
            input.Type = VnvmeConfigTypeQueue;
            input.Queue.MaxQueueEntries = 64;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_SET_CONFIG,
                &input, sizeof(input),
                NULL, 0,
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"SET_CONFIG IOCTL failed");
        }

        /*
         * Test: Set configuration with invalid parameters
         */
        TEST_METHOD(SetConfigurationInvalid)
        {
            VNVME_CONFIG_IOCTL_INPUT input = {0};
            ULONG_PTR bytesReturned = 0;

            /* Set an invalid configuration type */
            input.Type = (VNVME_CONFIG_TYPE)0xFFFF;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_SET_CONFIG,
                &input, sizeof(input),
                NULL, 0,
                &bytesReturned
            );

            Assert::IsFalse(NT_SUCCESS(status), L"Invalid config should fail");
        }

        /*
         * Test: Get driver status
         */
        TEST_METHOD(GetStatus)
        {
            VNVME_STATUS_INFO statusInfo = {0};
            ULONG_PTR bytesReturned = 0;

            NTSTATUS status = VNvmeTestSendIoctl(
                &ctx,
                IOCTL_VNVME_GET_STATUS,
                NULL, 0,
                &statusInfo, sizeof(statusInfo),
                &bytesReturned
            );

            Assert::IsTrue(NT_SUCCESS(status), L"GET_STATUS IOCTL failed");
        }
    };
}
