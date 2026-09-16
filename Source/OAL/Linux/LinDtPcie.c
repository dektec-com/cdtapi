// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver backend for Linux
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Not yet built or run: no Linux machine is available to the project. The buffer layout
// this backend depends on is in LinIoctlBuffer.c and is unit-tested on every platform;
// what remains untested here is the open, close and ioctl system calls themselves.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// CDtapiLite includes
#include "Core/DtlAlloc.h"      // Allocation seam.
#include "DtlDrvAbi.h"          // Driver ABI; pulls in sys/ioctl.h.
#include "LinIoctlBuffer.h"     // Layout of the shared in/out buffer.
#include "OAL/OsBackend.h"      // Backend interface being implemented.
#include "OAL/OsIoctlOutcome.h" // Classifies the return value of ioctl.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct LinDevice
{
    int Fd;
    unsigned long LastError;
} LinDevice;

// Most commands fit in this much, so they need no allocation. A larger one falls back to
// the heap.
#define LIN_STACK_BUFFER_BYTES 1024

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Opens /dev/DtPcie<Index>. The udev rule shipped with the driver creates these nodes
// world-readable and writable, so no privilege is needed.
//
static void* LinOpen(int Index)
{
    char Path[32];
    LinDevice* Dev;
    int Fd;

    snprintf(Path, sizeof(Path), "/dev/DtPcie%d", Index);

    Fd = open(Path, O_RDWR | O_CLOEXEC);
    if (Fd < 0)
        return NULL;

    Dev = (LinDevice*)DtlMalloc(sizeof(LinDevice));
    if (Dev == NULL)
    {
        close(Fd);
        return NULL;
    }

    Dev->Fd = Fd;
    Dev->LastError = 0;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void LinClose(void* State)
{
    LinDevice* Dev = (LinDevice*)State;

    close(Dev->Fd);
    DtlFree(Dev);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinIoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// ioctl takes a single pointer, so input and output share one block laid out by
// LinIoctlPack. A scratch block is always used, rather than the caller's input buffer,
// so that the driver's answer never overwrites memory the caller passed as const.
//
// A command the driver refuses comes back as its DtStatus, negated, in the return value
// of ioctl rather than in errno. OsIoctlClassifyLinux separates the two.
//
static int LinIoCtl(void* State, unsigned long Code, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    LinDevice* Dev = (LinDevice*)State;
    uint8_t Stack[LIN_STACK_BUFFER_BYTES];
    uint8_t* Buf = Stack;
    size_t OutBytes = (Out != NULL && OutSize != NULL) ? *OutSize : 0;
    bool SizeHeader = _IOC_TYPE(Code) == DT_IOCTL_MAGIC_SIZE;
    size_t BufSize = LinIoctlBufferSize(SizeHeader, InSize, OutBytes);
    int Result = OS_IOCTL_COMMUNICATION;
    int Rc;

    if (BufSize > sizeof(Stack))
    {
        Buf = (uint8_t*)DtlMalloc(BufSize);
        if (Buf == NULL)
        {
            Dev->LastError = ENOMEM;
            return OS_IOCTL_NO_RESOURCES;
        }
    }

    if (LinIoctlPack(SizeHeader, In, InSize, OutBytes, Buf, BufSize) != 0)
    {
        Dev->LastError = EINVAL;
        goto Cleanup;
    }

    Rc = ioctl(Dev->Fd, Code, Buf);
    if (Rc != 0)
    {
        Result = OsIoctlClassifyLinux(Rc, DrvStatus);
        Dev->LastError =
            (Result == OS_IOCTL_DRIVER_STATUS) ? *DrvStatus : (unsigned long)errno;
        goto Cleanup;
    }

    if (LinIoctlUnpack(Buf, BufSize, Out, OutBytes) != 0)
    {
        Dev->LastError = EINVAL;
        goto Cleanup;
    }

    // The Linux driver does not report how much it wrote, so *OutSize is left as the
    // caller set it. See the OsDrvIoCtl contract.

    Result = OS_IOCTL_OK;

Cleanup:
    if (Buf != Stack)
        DtlFree(Buf);

    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static unsigned long LinLastError(const void* State)
{
    return ((const LinDevice*)State)->LastError;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const OsBackend* OsPlatformBackend(void)
{
    static const OsBackend Backend = {LinOpen, LinClose, LinIoCtl, LinLastError};
    return &Backend;
}
