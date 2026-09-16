// #*#*#*#*#*#*#*#*#*#*#*#*#*#*# SimDtPcie.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - The emulated DtPcie device
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdlib.h>
#include <string.h>

// CDtapiLite includes
#include "Core/DtlAlloc.h" // Allocation seam.
#include "DtlDrvAbi.h"     // The driver ABI the emulator answers in.
#include "OAL/OsBackend.h" // Backend interface being implemented.
#include "SimDtPcie.h"     // What the emulated card reports.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Errors +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
//
// The emulator fails the way the real driver fails on the same platform, so that code
// which inspects OsDrvLastError behaves the same against both.
//

#if defined(WINBUILD)
    #define SIM_ERR_INVALID_ARG ERROR_INVALID_PARAMETER
    #define SIM_ERR_BUFFER_SIZE ERROR_INSUFFICIENT_BUFFER
    #define SIM_ERR_UNSUPPORTED ERROR_NOT_SUPPORTED
#else
    #include <errno.h>
    #define SIM_ERR_INVALID_ARG EINVAL
    #define SIM_ERR_BUFFER_SIZE EINVAL
    #define SIM_ERR_UNSUPPORTED ENOTTY
#endif

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimDevice
{
    unsigned long LastError;
} SimDevice;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimFail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static int SimFail(SimDevice* Dev, unsigned long Error)
{
    Dev->LastError = Error;
    return -1;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckSizes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver refuses a request whose input is shorter than its header, or whose output
// buffer cannot hold the answer. Doing the same here is what makes the emulator a test
// of the wire format rather than a stub that accepts anything.
//
static int CheckSizes(SimDevice* Dev, size_t InSize, size_t InNeeded, const void* Out,
                      const size_t* OutSize, size_t OutNeeded)
{
    if (InSize < InNeeded)
        return SimFail(Dev, SIM_ERR_BUFFER_SIZE);

    if (OutNeeded == 0)
        return 0;

    if (Out == NULL || OutSize == NULL || *OutSize < OutNeeded)
        return SimFail(Dev, SIM_ERR_BUFFER_SIZE);

    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetDriverVersion(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize)
{
    DtIoctlGetDriverVersionOutput* Version;

    if (CheckSizes(Dev, InSize, sizeof(DtIoctlGetDriverVersionInput), Out, OutSize,
                   sizeof(DtIoctlGetDriverVersionOutput)) != 0)
    {
        return -1;
    }

    Version = (DtIoctlGetDriverVersionOutput*)Out;
    Version->m_Major = SIM_DRIVER_MAJOR;
    Version->m_Minor = SIM_DRIVER_MINOR;
    Version->m_Micro = SIM_DRIVER_MICRO;
    Version->m_Build = SIM_DRIVER_BUILD;

    *OutSize = sizeof(DtIoctlGetDriverVersionOutput);
    return 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDevInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers GET_DEV_INFO and GET_DEV_INFO2 alike. The two differ only in how the driver
// fills the PCIe-specific tail, which the emulator leaves zeroed.
//
static int GetDevInfo(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize)
{
    DtIoctlGetDevInfoOutput* Info;

    if (CheckSizes(Dev, InSize, sizeof(DtIoctlGetDevInfoInput), Out, OutSize,
                   sizeof(DtIoctlGetDevInfoOutput)) != 0)
    {
        return -1;
    }

    Info = (DtIoctlGetDevInfoOutput*)Out;
    memset(Info, 0, sizeof(*Info));

    Info->m_TypeNumber = SIM_TYPE_NUMBER;
    Info->m_SubType = 0;
    Info->m_SubDvc = 0;
    Info->m_Serial = SIM_SERIAL;
    Info->m_HardwareRevision = SIM_HARDWARE_REVISION;
    Info->m_FirmwareVersion = SIM_FIRMWARE_VERSION;
    Info->m_FirmwareVariant = SIM_FIRMWARE_VARIANT;
    Info->m_FwPackageVersion = -1;
    Info->m_FirmwareStatus = DT_FWSTATUS_UPTODATE;
    Info->m_VendorId = SIM_VENDOR_ID;
    Info->m_DeviceId = SIM_DEVICE_ID;
    Info->m_SubVendorId = SIM_VENDOR_ID;
    Info->m_SubSystemId = SIM_DEVICE_ID;

    *OutSize = sizeof(DtIoctlGetDevInfoOutput);
    return 0;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Backend +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimOpen -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void* SimOpen(int Index)
{
    SimDevice* Dev;

    if (Index != SIM_DEVICE_INDEX)
        return NULL;

    Dev = (SimDevice*)DtlMalloc(sizeof(SimDevice));
    if (Dev == NULL)
        return NULL;

    Dev->LastError = 0;
    return Dev;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimClose -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static void SimClose(void* State)
{
    DtlFree(State);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimIoCtl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Dispatches on the function code rather than on the whole IOCTL number, exactly as the
// driver does. On Linux the number also encodes the argument size, so matching on it
// would accept only the one structure size this build happened to be compiled with.
//
static int SimIoCtl(void* State, unsigned long Code, const void* In, size_t InSize,
                    void* Out, size_t* OutSize)
{
    SimDevice* Dev = (SimDevice*)State;

    if (In == NULL)
        return SimFail(Dev, SIM_ERR_INVALID_ARG);

    switch (DT_IOCTL_TO_FUNCTION(Code))
    {
    case DT_FUNC_CODE_GET_DRIVER_VERSION:
        return GetDriverVersion(Dev, InSize, Out, OutSize);

    case DT_FUNC_CODE_GET_DEV_INFO:
    case DT_FUNC_CODE_GET_DEV_INFO2:
        return GetDevInfo(Dev, InSize, Out, OutSize);

    default:
        return SimFail(Dev, SIM_ERR_UNSUPPORTED);
    }
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimLastError -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static unsigned long SimLastError(const void* State)
{
    return ((const SimDevice*)State)->LastError;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Selection +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSimBackend -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
const OsBackend* OsSimBackend(void)
{
    static const OsBackend Backend = {SimOpen, SimClose, SimIoCtl, SimLastError};
    return &Backend;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- OsSimIsRequested -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
bool OsSimIsRequested(void)
{
    static int Cached = -1;

    // Any value other than empty or "0" turns the emulator on, so that both
    // CDTAPILITE_SIM=1 and CDTAPILITE_SIM=yes work and CDTAPILITE_SIM=0 does not.
    if (Cached < 0)
    {
        const char* Value = getenv("CDTAPILITE_SIM");
        Cached = (Value != NULL && Value[0] != '\0' && strcmp(Value, "0") != 0) ? 1 : 0;
    }

    return Cached == 1;
}
