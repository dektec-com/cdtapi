// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtPcieCmdVpd.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - DtPcie driver commands: the card's Vital Product Data
//
// SPDX-License-Identifier: BSD-3-Clause
//
// Reading the properties of the VPD and the raw bytes of the EEPROM. The commands go to
// the device, as the EEPROM belongs to the card and not to one of its ports.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <limits.h>
#include <stddef.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"   // The answer of a raw read.
#include "DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcieCmd.h"      // Interface being implemented.
#include "DtPcieCmdIssue.h" // Issuing commands.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Commands +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_VpdGetProps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtPcieCmd_VpdGetProps(OsDrv* Drv, DtVpdProps* Props)
{
    if (Props != NULL)
        memset(Props, 0, sizeof(*Props));
    if (Drv == NULL || Props == NULL)
        return DTAPI_E_INVALID_ARG;

    const DtDrvObject Device = {0, DT_PROPERTY_DEVICE};
    DtIoctlVpdCmdGetPropertiesOutput Out;
    memset(&Out, 0, sizeof(Out));
    DtapiResult Result =
        DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_VPD_CMD),
                                  DT_VPD_CMD_GET_PROPERTIES, Device, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    Props->RoOffset = Out.m_RoOffset;
    Props->RoSize = Out.m_RoSize;
    Props->RwOffset = Out.m_RwOffset;
    Props->RwSize = Out.m_RwSize;
    Props->EepromSize = Out.m_EepromSize;
    Props->MaxItemLength = Out.m_MaxItemLength;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtPcieCmd_VpdRawRead -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The answer ends in a buffer of the bytes read, so it is allocated to the size asked
// for. What the driver read is copied out only on success, and a driver that says it
// read more than was asked for is not believed.
//
DtapiResult DtPcieCmd_VpdRawRead(OsDrv* Drv, uint32_t Offset, uint8_t* Buf, int Count,
                                 int* NumRead)
{
    if (NumRead != NULL)
        *NumRead = 0;
    if (Drv == NULL || Buf == NULL || Count < 1)
        return DTAPI_E_INVALID_ARG;
    if ((size_t)Count > SIZE_MAX - sizeof(DtIoctlVpdCmdRawReadOutput))
        return DTAPI_E_INVALID_ARG;

    const size_t OutSize = sizeof(DtIoctlVpdCmdRawReadOutput) + (size_t)Count;
    DtIoctlVpdCmdRawReadOutput* Out =
        (DtIoctlVpdCmdRawReadOutput*)DtAlloc_Malloc(OutSize);
    if (Out == NULL)
        return DTAPI_E_OUT_OF_MEM;
    memset(Out, 0, OutSize);

    DtIoctlVpdCmdRawReadInput In;
    memset(&In, 0, sizeof(In));
    DtPcieCmd_InitDeviceHeader(&In.m_CmdHdr, DT_VPD_CMD_RAW_READ);
    In.m_StartOffset = Offset;
    In.m_NumToRead = Count;

    DtapiResult Result =
        DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_VPD_CMD), &In, sizeof(In), Out, OutSize);
    if (DT_SUCCEEDED(Result))
    {
        if (Out->m_NumRead < 0 || Out->m_NumRead > Count)
            Result = DTAPI_E_DEV_DRIVER;
        else
        {
            memcpy(Buf, Out->m_Buf, (size_t)Out->m_NumRead);
            if (NumRead != NULL)
                *NumRead = Out->m_NumRead;
        }
    }
    DtAlloc_Free(Out);
    return Result;
}
