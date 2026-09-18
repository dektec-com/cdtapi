// #*#*#*#*#*#*#*#*#*#*#*#*#* OsIoctlOutcome.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - How a failed IOCTL is classified, per platform - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// CDTAPI includes
#include "OsIoctlOutcome.h" // Interface being implemented.

// The customer bit of a Windows error value, set for errors a driver defines itself.
#define OS_WIN_CUSTOMER_BIT 0x20000000u

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Classification +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.- OsIoctlOutcome_ClassifyWindows -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsIoctlOutcome_ClassifyWindows(uint32_t Error, uint32_t* DrvStatus)
{
    *DrvStatus = 0;

    if ((Error & OS_WIN_CUSTOMER_BIT) != 0)
    {
        *DrvStatus = (uint32_t)Error;
        return OS_IOCTL_DRIVER_STATUS;
    }

    if (Error == OS_WIN_ERROR_NO_SYSTEM_RESOURCES)
        return OS_IOCTL_NO_RESOURCES;

    return OS_IOCTL_COMMUNICATION;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- OsIoctlOutcome_ClassifyLinux -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
int OsIoctlOutcome_ClassifyLinux(int Rc, uint32_t* DrvStatus)
{
    *DrvStatus = 0;

    if (Rc == 0)
        return OS_IOCTL_OK;

    if (Rc == -1)
        return OS_IOCTL_COMMUNICATION;

    // Negated through a 64-bit value, so that INT_MIN does not overflow.
    *DrvStatus = (uint32_t)(-(int64_t)Rc);
    return OS_IOCTL_DRIVER_STATUS;
}
