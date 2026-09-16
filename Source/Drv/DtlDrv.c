// #*#*#*#*#*#*#*#*#*#*#*#*#*#*#* DtlDrv.c *#*#*#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDtapiLite - Driver ABI layer - Implementation
//
// SPDX-License-Identifier: BSD-3-Clause

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDtapiLite includes
#include "DtlDrv.h"       // Interface being implemented.
#include "DtlDrvAbi.h"    // Vendored driver structures and IOCTL codes.
#include "DtlDrvStatus.h" // Driver status to result.

// The IOCTL codes come from CTL_CODE on Windows, which the SDK evaluates as int. The
// device type DekTec uses puts the value above INT_MAX, so it is converted once, here,
// rather than at every call.
#define DTL_IOCTL(Code) ((unsigned long)(Code))

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- InitHeader -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Fills the header every command starts with. A device-level command addresses no
// building block, so Uuid is zero and PortIndex is -1, which the driver reads as "the
// device itself".
//
static void InitHeader(DtIoctlInputDataHdr* Hdr, int Cmd)
{
    memset(Hdr, 0, sizeof(*Hdr));
    Hdr->m_Uuid = 0;
    Hdr->m_PortIndex = -1;
    Hdr->m_Cmd = Cmd;
    Hdr->m_CmdEx = DT_IOCTL_CMD_NOP;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Issue -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Issues a command whose answer has a fixed size, and turns the outcome into a result:
// a refused command into the result its DtStatus stands for, and a failure to reach the
// driver into DTAPI_E_COMMUNICATION or DTAPI_E_OUT_OF_RESOURCES.
//
// A driver that answers with fewer bytes than the structure holds is treated as a
// failure: the fields it did not write would otherwise be read as zeroes and trusted.
//
// That check only has teeth on Windows and against the emulator. The Linux driver does
// not report how much it wrote, so there OsDrvIoCtl leaves the size as it was and a
// short answer cannot be detected here.
//
static unsigned int Issue(OsDrv* Drv, unsigned long Code, const void* In, size_t InSize,
                          void* Out, size_t OutSize)
{
    size_t Returned = OutSize;
    uint32_t Status;
    int Outcome = OsDrvIoCtl(Drv, Code, In, InSize, Out, &Returned, &Status);

    if (Outcome != OS_IOCTL_OK)
        return DtlDrvOutcomeToResult(Outcome, Status);

    if (Returned < OutSize)
        return DTAPI_E_DEV_DRIVER;

    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlDrvGetDriverVersion -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
unsigned int DtlDrvGetDriverVersion(OsDrv* Drv, DtlDriverVersion* Version)
{
    DtIoctlGetDriverVersionInput In;
    DtIoctlGetDriverVersionOutput Out;
    unsigned int Result;

    if (Drv == NULL || Version == NULL)
        return DTAPI_E_INVALID_ARG;

    InitHeader(&In, DT_IOCTL_CMD_NOP);
    memset(&Out, 0, sizeof(Out));

    Result = Issue(Drv, DTL_IOCTL(DT_IOCTL_GET_DRIVER_VERSION), &In, sizeof(In), &Out,
                   sizeof(Out));
    if (!DTL_SUCCEEDED(Result))
        return Result;

    Version->Major = Out.m_Major;
    Version->Minor = Out.m_Minor;
    Version->Micro = Out.m_Micro;
    Version->Build = Out.m_Build;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtlDrvGetDeviceInfo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
unsigned int DtlDrvGetDeviceInfo(OsDrv* Drv, DtlDeviceInfo* Info)
{
    DtIoctlGetDevInfoInput In;
    DtIoctlGetDevInfoOutput Out;
    unsigned int Result;

    if (Drv == NULL || Info == NULL)
        return DTAPI_E_INVALID_ARG;

    InitHeader(&In, DT_IOCTL_CMD_NOP);
    memset(&Out, 0, sizeof(Out));

    // GET_DEV_INFO2 first. A driver that predates it refuses the command, and then the
    // original is tried, which carries the same common fields.
    Result =
        Issue(Drv, DTL_IOCTL(DT_IOCTL_GET_DEV_INFO2), &In, sizeof(In), &Out, sizeof(Out));
    if (!DTL_SUCCEEDED(Result))
    {
        memset(&Out, 0, sizeof(Out));
        Result = Issue(Drv, DTL_IOCTL(DT_IOCTL_GET_DEV_INFO), &In, sizeof(In), &Out,
                       sizeof(Out));
        if (!DTL_SUCCEEDED(Result))
            return Result;
    }

    memset(Info, 0, sizeof(*Info));
    Info->TypeNumber = Out.m_TypeNumber;
    Info->SubType = Out.m_SubType;
    Info->Serial = (int64_t)Out.m_Serial;
    Info->HardwareRevision = Out.m_HardwareRevision;
    Info->FirmwareVersion = Out.m_FirmwareVersion;
    Info->FirmwareVariant = Out.m_FirmwareVariant;
    Info->FirmwareStatus = Out.m_FirmwareStatus;
    Info->VendorId = Out.m_VendorId;
    Info->DeviceId = Out.m_DeviceId;
    Info->SubVendorId = Out.m_SubVendorId;
    Info->SubSystemId = Out.m_SubSystemId;
    return DTAPI_OK;
}
