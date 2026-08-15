/*-
 *  SPDX-License-Identifier: BSD-3-Clause
 *
 *  Copyright (c) 2025, Virtual NVMe Driver Project. All rights reserved.
 *
 *  stdafx.h : include file for standard system include files,
 *  or project specific include files that are used frequently, but
 *  are changed infrequently
 */

#pragma once

#include <ddk.h>
#include <Windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <memory.h>
#include <errno.h>

#include "targetver.h"
#include "UnitTest.h"

/* VNVME headers */
#include "vnvme_ioctl.h"
#include "vnvme_common.h"

/* Test helpers */
#include "TestHelpers.h"
