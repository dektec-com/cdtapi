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
#include "Core/DtlAlloc.h"          // Allocation seam.
#include "DtlDrvAbi.h"              // The driver ABI the emulator answers in.
#include "OAL/OsAbstractionLayer.h" // The OS_IOCTL_ outcomes.
#include "OAL/OsBackend.h"          // Backend interface being implemented.
#include "SimDtPcie.h"              // What the emulated card reports.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

typedef struct SimDevice
{
    unsigned long LastError;
} SimDevice;

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SimFail -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Refuses a command with a DtStatus, which is how the driver refuses one. The emulator
// stands in for the driver, not for the operating system, so it has no other way to
// fail.
//
static int SimFail(SimDevice* Dev, DtStatus Status, uint32_t* DrvStatus)
{
    Dev->LastError = Status;
    *DrvStatus = Status;
    return OS_IOCTL_DRIVER_STATUS;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckSizes -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The driver refuses a request whose input is shorter than the command's input
// structure, or whose output buffer cannot hold the answer, both with
// DT_STATUS_INVALID_PARAMETER and in that order. Doing the same here is what makes the
// emulator a test of the wire format rather than a stub that accepts anything.
//
// Returns OS_IOCTL_OK when the sizes are acceptable.
//
static int CheckSizes(SimDevice* Dev, size_t InSize, size_t InNeeded, const void* Out,
                      const size_t* OutSize, size_t OutNeeded, uint32_t* DrvStatus)
{
    if (InSize < InNeeded)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    if (OutNeeded == 0)
        return OS_IOCTL_OK;

    if (Out == NULL || OutSize == NULL || *OutSize < OutNeeded)
        return SimFail(Dev, DT_STATUS_INVALID_PARAMETER, DrvStatus);

    return OS_IOCTL_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
static int GetDriverVersion(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize,
                            uint32_t* DrvStatus)
{
    DtIoctlGetDriverVersionOutput* Version;
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlGetDriverVersionInput), Out,
                             OutSize, sizeof(DtIoctlGetDriverVersionOutput), DrvStatus);

    if (Outcome != OS_IOCTL_OK)
        return Outcome;

    Version = (DtIoctlGetDriverVersionOutput*)Out;
    Version->m_Major = SIM_DRIVER_MAJOR;
    Version->m_Minor = SIM_DRIVER_MINOR;
    Version->m_Micro = SIM_DRIVER_MICRO;
    Version->m_Build = SIM_DRIVER_BUILD;

    *OutSize = sizeof(DtIoctlGetDriverVersionOutput);
    return OS_IOCTL_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetDevInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Answers GET_DEV_INFO and GET_DEV_INFO2 alike. The two differ only in how the driver
// fills the PCIe-specific tail, which the emulator leaves zeroed.
//
static int GetDevInfo(SimDevice* Dev, size_t InSize, void* Out, size_t* OutSize,
                      uint32_t* DrvStatus)
{
    DtIoctlGetDevInfoOutput* Info;
    int Outcome = CheckSizes(Dev, InSize, sizeof(DtIoctlGetDevInfoInput), Out, OutSize,
                             sizeof(DtIoctlGetDevInfoOutput), DrvStatus);

    if (Outcome != OS_IOCTL_OK)
        return Outcome;

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
    return OS_IOCTL_OK;
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
// A command the emulator does not model is refused with DT_STATUS_NOT_SUPPORTED before
// any size is looked at, as the driver refuses a command it does not know. OsDrvIoCtl
// has already refused a request without input.
//
static int SimIoCtl(void* State, unsigned long Code, const void* In, size_t InSize,
                    void* Out, size_t* OutSize, uint32_t* DrvStatus)
{
    SimDevice* Dev = (SimDevice*)State;

    (void)In;

    switch (DT_IOCTL_TO_FUNCTION(Code))
    {
    case DT_FUNC_CODE_GET_DRIVER_VERSION:
        return GetDriverVersion(Dev, InSize, Out, OutSize, DrvStatus);

    case DT_FUNC_CODE_GET_DEV_INFO:
    case DT_FUNC_CODE_GET_DEV_INFO2:
        return GetDevInfo(Dev, InSize, Out, OutSize, DrvStatus);

    default:
        return SimFail(Dev, DT_STATUS_NOT_SUPPORTED, DrvStatus);
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
