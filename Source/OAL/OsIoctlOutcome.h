// #*#*#*#*#*#*#*#*#*#*#*#*#* OsIoctlOutcome.h *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - How a failed IOCTL is classified, per platform
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>

// CDtapiLite includes
#include "OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// Each backend receives a failure in its platform's own form and turns it into one of
// the OS_IOCTL_ outcomes. The decision is kept apart from the backends, and free of any
// platform header, so that both forms are unit-tested on every platform. Neither backend
// can be run without a card.
//
// Both functions set *DrvStatus, which must not be NULL, to the driver's DtStatus for
// OS_IOCTL_DRIVER_STATUS and to zero otherwise.
//

// GetLastError's value for an exhausted system resource, from winerror.h.
#define OS_WIN_ERROR_NO_SYSTEM_RESOURCES 1450UL

// Classifies the GetLastError value of a failed DeviceIoControl. A driver status has the
// customer bit, bit 29, set and is passed on unchanged.
int OsIoctlOutcome_ClassifyWindows(uint32_t Error, uint32_t* DrvStatus);

// Classifies the return value of ioctl. The driver returns a refused command's DtStatus
// negated, and those values lie outside the range the C library turns into errno, so a
// return of -1 is a failure of the call itself and anything else nonzero is a status.
int OsIoctlOutcome_ClassifyLinux(int Rc, uint32_t* DrvStatus);
