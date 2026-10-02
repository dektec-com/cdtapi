// #*#*#*#*#*#*#*#*#*#*#*#*#* OsIoctlOutcome.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - How a failed IOCTL is classified, per platform
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDTAPI includes
#include "OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Turns the way a platform reports a failed IOCTL into one of the OS_IOCTL_ outcomes.
// The backends call these functions. They use no platform header, so that the tests
// check the rules of both platforms on every platform; neither backend runs without a
// card.
//
// Both functions set *DrvStatus, which must not be NULL, to the driver's DtStatus for
// OS_IOCTL_DRIVER_STATUS, and to zero otherwise.
//

// The GetLastError value for a lack of system resources, ERROR_NO_SYSTEM_RESOURCES in
// winerror.h.
#define OS_WIN_ERROR_NO_SYSTEM_RESOURCES 1450UL

// Returns the outcome of a failed DeviceIoControl, from its GetLastError value Error:
//   OS_IOCTL_DRIVER_STATUS  Error has the customer bit (bit 29) set; it is the DtStatus
//   OS_IOCTL_NO_RESOURCES   Error is OS_WIN_ERROR_NO_SYSTEM_RESOURCES
//   OS_IOCTL_COMMUNICATION  any other Error
int OsIoctlOutcome_ClassifyWindows(uint32_t Error, uint32_t* DrvStatus);

// Returns the outcome of an ioctl, from its return value Rc:
//   OS_IOCTL_OK             Rc is 0
//   OS_IOCTL_COMMUNICATION  Rc is -1: the call itself failed, and errno says why
//   OS_IOCTL_DRIVER_STATUS  any other Rc, which is the DtStatus negated
// A DtStatus lies outside the range the C library turns into errno, so it never comes
// back as -1.
int OsIoctlOutcome_ClassifyLinux(int Rc, uint32_t* DrvStatus);
