// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - DtPcie driver backend for Linux
//
// SPDX-License-Identifier: BSD-3-Clause
//
// The buffer layout this backend depends on is in LinIoctlBuffer.c and is unit-tested on
// every platform; the open, close and ioctl system calls need a card.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// With -std=c11 the C library declares only ISO C. Asked for before any header, this
// also exposes the POSIX and Linux calls the backend makes.
#define _GNU_SOURCE

// Standard includes
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// CDTAPI includes
#include "Core/DtAlloc.h"       // Allocation seam.
#include "DtPcieAbi.h"          // Driver ABI; pulls in sys/ioctl.h.
#include "LinIoctlBuffer.h"     // Layout of the shared in/out buffer.
#include "OAL/OsBackend.h"      // Backend interface being implemented.
#include "OAL/OsIoctlOutcome.h" // Classifies the return value of ioctl.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct LinDevice
{
    int Fd;
    uint32_t LastError;
} LinDevice;

// Most commands fit in this much, so they need no allocation. A larger one falls back to
// the heap.
#define LIN_STACK_BUFFER_BYTES 1024

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Open -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Opens /dev/DtPcie<Index>. The udev rule shipped with the driver creates these nodes
// world-readable and writable, so no privilege is needed.
//
static void* Open(int Index)
{
    char Path[32];
    LinDevice* Dev;
    int Fd;

    snprintf(Path, sizeof(Path), "/dev/DtPcie%d", Index);

    Fd = open(Path, O_RDWR | O_CLOEXEC);
    if (Fd < 0)
        return NULL;

    Dev = (LinDevice*)DtAlloc_Malloc(sizeof(LinDevice));
    if (Dev == NULL)
    {
        close(Fd);
        return NULL;
    }

    Dev->Fd = Fd;
    Dev->LastError = 0;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Close -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void Close(void* State)
{
    LinDevice* Dev = (LinDevice*)State;

    close(Dev->Fd);
    DtAlloc_Free(Dev);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- IoctlThroughBlock -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the input into the block Buf of BufSize bytes, issues the ioctl and unpacks the
// output. Returns one of the OS_IOCTL_ outcomes; a failure sets the device's last error.
//
static int IoctlThroughBlock(LinDevice* Dev, uint32_t Code, bool HasSizeHeader,
                             const void* In, size_t InSize, void* Out, size_t OutCapacity,
                             uint8_t* Buf, size_t BufSize, uint32_t* DrvStatus)
{
    if (LinIoctlBuffer_Pack(HasSizeHeader, In, InSize, OutCapacity, Buf, BufSize) != 0)
    {
        Dev->LastError = (uint32_t)EINVAL;
        return OS_IOCTL_COMMUNICATION;
    }

    int Rc = ioctl(Dev->Fd, (unsigned long)Code, Buf);
    if (Rc != 0)
    {
        int Result = OsIoctlOutcome_ClassifyLinux(Rc, DrvStatus);
        Dev->LastError =
            (Result == OS_IOCTL_DRIVER_STATUS) ? *DrvStatus : (uint32_t)errno;
        return Result;
    }

    if (LinIoctlBuffer_Unpack(Buf, BufSize, Out, OutCapacity) != 0)
    {
        Dev->LastError = (uint32_t)EINVAL;
        return OS_IOCTL_COMMUNICATION;
    }

    // The Linux driver does not report how much it wrote, so Ioctl leaves the caller's
    // *OutSize as it was. See the OsDrv_Ioctl contract.
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Ioctl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// ioctl takes a single pointer, so input and output share one block laid out by
// LinIoctlBuffer_Pack. A scratch block is always used, rather than the caller's input
// buffer, so that the driver's answer never overwrites memory the caller passed as const.
//
// A command the driver refuses comes back as its DtStatus, negated, in the return value
// of ioctl rather than in errno. OsIoctlOutcome_ClassifyLinux separates the two.
//
static int Ioctl(void* State, uint32_t Code, const void* In, size_t InSize, void* Out,
                 size_t* OutSize, uint32_t* DrvStatus)
{
    LinDevice* Dev = (LinDevice*)State;
    size_t OutCapacity = (Out != NULL && OutSize != NULL) ? *OutSize : 0;
    bool HasSizeHeader = _IOC_TYPE(Code) == DT_IOCTL_MAGIC_SIZE;
    size_t BufSize = LinIoctlBuffer_Size(HasSizeHeader, InSize, OutCapacity);

    uint8_t Stack[LIN_STACK_BUFFER_BYTES];
    uint8_t* Buf = Stack;
    if (BufSize > sizeof(Stack))
    {
        Buf = (uint8_t*)DtAlloc_Malloc(BufSize);
        if (Buf == NULL)
        {
            Dev->LastError = (uint32_t)ENOMEM;
            return OS_IOCTL_NO_RESOURCES;
        }
    }

    int Result = IoctlThroughBlock(Dev, Code, HasSizeHeader, In, InSize, Out, OutCapacity,
                                   Buf, BufSize, DrvStatus);
    if (Buf != Stack)
        DtAlloc_Free(Buf);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static uint32_t LastError(const void* State)
{
    return ((const LinDevice*)State)->LastError;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- MapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Maps the memory shared, readable and writable. The driver tells from Offset which of
// its memory is meant.
//
static void* MapMemory(void* State, uint64_t Offset, size_t Size)
{
    LinDevice* Dev = (LinDevice*)State;
    void* Address =
        mmap(NULL, Size, PROT_READ | PROT_WRITE, MAP_SHARED, Dev->Fd, (off_t)Offset);

    if (Address == MAP_FAILED)
    {
        Dev->LastError = (uint32_t)errno;
        return NULL;
    }
    return Address;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- UnmapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void UnmapMemory(void* State, void* Address, size_t Size)
{
    (void)State;
    munmap(Address, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatform_Backend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const OsBackend* OsPlatform_Backend(void)
{
    static const OsBackend Backend = {Open,      Close,     Ioctl,
                                      LastError, MapMemory, UnmapMemory};
    return &Backend;
}
