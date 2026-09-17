// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# LinDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - DtPcie driver backend for Linux
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

// CDtapiLite includes
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

    Dev = (LinDevice*)DtMalloc(sizeof(LinDevice));
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
    DtFree(Dev);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinTransfer -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Packs the input into the block Buf of BufSize bytes, issues the ioctl and unpacks the
// output. Returns one of the OS_IOCTL_ outcomes, with the device's last error set.
//
static int LinTransfer(LinDevice* Dev, uint32_t Code, bool SizeHeader, const void* In,
                       size_t InSize, void* Out, size_t OutBytes, uint8_t* Buf,
                       size_t BufSize, uint32_t* DrvStatus)
{
    if (LinIoctlPack(SizeHeader, In, InSize, OutBytes, Buf, BufSize) != 0)
    {
        Dev->LastError = (uint32_t)EINVAL;
        return OS_IOCTL_COMMUNICATION;
    }

    int Rc = ioctl(Dev->Fd, (unsigned long)Code, Buf);
    if (Rc != 0)
    {
        int Result = OsIoctlClassifyLinux(Rc, DrvStatus);
        Dev->LastError =
            (Result == OS_IOCTL_DRIVER_STATUS) ? *DrvStatus : (uint32_t)errno;
        return Result;
    }

    if (LinIoctlUnpack(Buf, BufSize, Out, OutBytes) != 0)
    {
        Dev->LastError = (uint32_t)EINVAL;
        return OS_IOCTL_COMMUNICATION;
    }

    // The Linux driver does not report how much it wrote, so *OutSize is left as the
    // caller set it. See the OsDrvIoCtl contract.
    return OS_IOCTL_OK;
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
static int LinIoCtl(void* State, uint32_t Code, const void* In, size_t InSize, void* Out,
                    size_t* OutSize, uint32_t* DrvStatus)
{
    LinDevice* Dev = (LinDevice*)State;
    size_t OutBytes = (Out != NULL && OutSize != NULL) ? *OutSize : 0;
    bool SizeHeader = _IOC_TYPE(Code) == DT_IOCTL_MAGIC_SIZE;
    size_t BufSize = LinIoctlBufferSize(SizeHeader, InSize, OutBytes);

    uint8_t Stack[LIN_STACK_BUFFER_BYTES];
    uint8_t* Buf = Stack;
    if (BufSize > sizeof(Stack))
    {
        Buf = (uint8_t*)DtMalloc(BufSize);
        if (Buf == NULL)
        {
            Dev->LastError = (uint32_t)ENOMEM;
            return OS_IOCTL_NO_RESOURCES;
        }
    }

    int Result = LinTransfer(Dev, Code, SizeHeader, In, InSize, Out, OutBytes, Buf,
                             BufSize, DrvStatus);
    if (Buf != Stack)
        DtFree(Buf);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static uint32_t LinLastError(const void* State)
{
    return ((const LinDevice*)State)->LastError;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinMapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// As XpDriverLinux::MapMemory: shared, readable and writable, at the offset the driver
// reads as which memory is meant.
//
static void* LinMapMemory(void* State, uint64_t Offset, size_t Size)
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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LinUnmapMemory -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void LinUnmapMemory(void* State, void* Address, size_t Size)
{
    (void)State;
    munmap(Address, Size);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsPlatformBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
const OsBackend* OsPlatformBackend(void)
{
    static const OsBackend Backend = {LinOpen,      LinClose,     LinIoCtl,
                                      LinLastError, LinMapMemory, LinUnmapMemory};
    return &Backend;
}
