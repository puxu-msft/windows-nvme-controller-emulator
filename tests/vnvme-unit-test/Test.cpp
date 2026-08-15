/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  VNVME Unit Test Module Initialization
 */

#include "stdafx.h"


namespace VNVMEUnitTest
{
    /*
     * Module-level initialization and cleanup
     * These run once per test module (DLL load/unload)
     */
    TEST_MODULE_INITIALIZE(VNVMEUnitTestInit)
    {
        TEST_MODULE_START(VNVMEUnitTestCleanup);
        
        /* Load the VNVME driver for testing */
        DdkLoadDriver("VNVME");
    }

    TEST_MODULE_CLEANUP(VNVMEUnitTestCleanup)
    {
        TEST_MODULE_END();
        
        /* Unload the VNVME driver */
        DdkUnloadDriver("VNVME");
    }

    /*
     * Basic Driver Load Test
     * Verifies that the driver loads correctly and creates the control device
     */
    TEST_CLASS(VNVMEDriverLoadTest)
    {
        PFILE_OBJECT pFile;
        PDEVICE_OBJECT pDevice;

        TEST_METHOD_INITIALIZE(TestInit)
        {
            DdkThreadInit();
            pFile = NULL;
            pDevice = NULL;
        }

        TEST_METHOD_CLEANUP(TestCleanup)
        {
            if (pFile) {
                ObDereferenceObject(pFile);
            }
        }

        /*
         * Test: Driver creates control device
         */
        TEST_METHOD(DriverCreatesControlDevice)
        {
            UNICODE_STRING deviceName;
            NTSTATUS status;

            RtlInitUnicodeString(&deviceName, L"\\Device\\VNvmeControl");

            status = IoGetDeviceObjectPointer(
                &deviceName,
                FILE_ALL_ACCESS,
                &pFile,
                &pDevice
            );

            Assert::IsTrue(NT_SUCCESS(status), L"Failed to get control device");
            Assert::IsNotNull(pDevice, L"Device object is NULL");
            Assert::IsNotNull(pFile, L"File object is NULL");
        }
    };
}
