// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevActivate.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Activating a device at attach
//
// SPDX-License-Identifier: BSD-3-Clause
//
// At attach the object is handed the data the card's own EEPROM holds for it, and reports
// itself ready. Until it does, the firmware does not do its work: the card carries
// nothing, in either direction, whether over SDI, ASI or IP. Every card that has the
// object needs this; a card without it needs none of it.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"          // Allocation seam.
#include "DtDevActivate.h"         // Interface being implemented.
#include "DtPcie/DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcie/DtPcieCmd.h"      // Properties and the VPD.
#include "DtPcie/DtPcieCmdIssue.h" // Issuing commands.
#include "OAL/OsThread.h"          // Sleeping between polls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The property naming the object that is asked, whose answer is its UUID, and the data
// it takes: 64 words of the EEPROM, which sit behind the sections that hold the card's
// own description.
#define ACTIVATE_UUID_PROPERTY "BC_IPSECG#1_UUID"
#define ACTIVATE_NUM_WORDS 64

// How long the object may take to answer, in milliseconds, and the step between polls.
#define ACTIVATE_TIMEOUT_MS 50
#define ACTIVATE_POLL_MS 1

// How often to try for the object, a millisecond apart, before giving up on it.
#define ACTIVATE_ACQUIRE_TRIES 10

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AcquireExclusiveAccess -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The object answers nothing without exclusive access, so it is taken for the whole pass
// and released again.
//
static DtapiResult AcquireExclusiveAccess(OsDrv* Drv, DtDrvObject Object)
{
    DtapiResult Result = DTAPI_E_IN_USE;

    for (int Try = 0; Try < ACTIVATE_ACQUIRE_TRIES && Result != DTAPI_OK; Try++)
    {
        if (Try > 0)
            OsTime_SleepMs(1);
        Result = DtPcieCmd_ExclAccess(Drv, Object, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindActivationObject -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The object belongs to the device rather than to a port, and is addressed by the UUID
// the driver gives it. DTAPI_E_NOT_FOUND for a device that has no such object.
//
static DtapiResult FindActivationObject(OsDrv* Drv, DtDrvObject* Object)
{
    Object->PortIndex = DT_PROPERTY_DEVICE;
    return DtPcieCmd_GetPropertyInt(Drv, ACTIVATE_UUID_PROPERTY, DT_PROPERTY_DEVICE,
                                    &Object->Uuid);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetObjectStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetObjectStatus(OsDrv* Drv, DtDrvObject Object, bool* Busy,
                                   bool* Ready)
{
    DtIoctlIpSecGCmdGetStatusOutput Out;

    memset(&Out, 0, sizeof(Out));
    DtapiResult Result =
        DtPcieCmd_IssueHeaderOnly(Drv, DT_IOCTL(DT_IOCTL_IPSECG_CMD),
                                  DT_IPSECG_CMD_GET_STATUS, Object, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Busy = Out.m_IsBusy != 0;
    *Ready = Out.m_IsOk != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- HandData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Hands the object its data. A count of zero is what a card whose EEPROM holds nothing
// for it gets, and is a command of its own rather than no command at all.
//
static DtapiResult HandData(OsDrv* Drv, DtDrvObject Object, const uint32_t* Words,
                            int Count)
{
    const size_t InSize =
        sizeof(DtIoctlIpSecGCmdCheckInput) + (size_t)Count * sizeof(uint32_t);
    DtIoctlIpSecGCmdCheckInput* In = (DtIoctlIpSecGCmdCheckInput*)DtAlloc_Malloc(InSize);

    if (In == NULL)
        return DTAPI_E_OUT_OF_MEM;

    memset(In, 0, InSize);
    DtPcieCmd_InitHeader(&In->m_CmdHdr, DT_IPSECG_CMD_CHECK, Object);
    In->m_NumWords = Count;
    memcpy(In->m_Data, Words, (size_t)Count * sizeof(uint32_t));

    DtapiResult Result =
        DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_IPSECG_CMD), In, InSize, NULL, 0);
    DtAlloc_Free(In);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadEepromData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The data lies in the EEPROM behind the read-only and read-write sections, after the one
// that ends last, whatever their order. A card whose EEPROM was never given any holds the
// same word throughout, which counts as none. The EEPROM holds each word most significant
// byte first, and the words are assembled from the bytes, so that their values do not
// depend on this processor's byte order.
//
static DtapiResult ReadEepromData(OsDrv* Drv, uint32_t* Words, bool* HasData)
{
    DtVpdProps Props;
    uint8_t Bytes[ACTIVATE_NUM_WORDS * sizeof(uint32_t)];

    *HasData = false;
    DtapiResult Result = DtPcieCmd_VpdGetProps(Drv, &Props);
    if (!DT_SUCCEEDED(Result))
        return Result;

    const int RoEnd = Props.RoOffset + Props.RoSize;
    const int RwEnd = Props.RwOffset + Props.RwSize;
    const int Offset = RoEnd > RwEnd ? RoEnd : RwEnd;
    if (Offset < 0 || Props.EepromSize < 0 ||
        (int)sizeof(Bytes) > Props.EepromSize - Offset)
        return DTAPI_OK; // No room for any: the card holds none

    int NumRead = 0;
    Result =
        DtPcieCmd_VpdRawRead(Drv, (uint32_t)Offset, Bytes, (int)sizeof(Bytes), &NumRead);
    if (!DT_SUCCEEDED(Result))
        return Result;
    if (NumRead != (int)sizeof(Bytes))
        return DTAPI_OK; // Not all of it: treat as none

    for (int i = 0; i < ACTIVATE_NUM_WORDS; i++)
    {
        const uint8_t* At = &Bytes[(size_t)i * sizeof(uint32_t)];

        Words[i] =
            (uint32_t)At[0] << 24 | (uint32_t)At[1] << 16 | (uint32_t)At[2] << 8 | At[3];
    }

    bool AllSame = true;
    for (int i = 1; i < ACTIVATE_NUM_WORDS && AllSame; i++)
        AllSame = Words[i] == Words[0];
    if (AllSame)
        return DTAPI_OK;

    *HasData = true;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevActivate_OnAttach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtDevActivate_OnAttach(OsDrv* Drv)
{
    DtDrvObject Object;
    bool Busy = false;
    bool Ready = false;

    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = FindActivationObject(Drv, &Object);
    if (Result == DTAPI_E_NOT_FOUND)
        return DTAPI_OK; // Nothing to activate on this device
    if (Result != DTAPI_OK)
        return Result;

    Result = AcquireExclusiveAccess(Drv, Object);
    if (Result != DTAPI_OK)
        return Result;

    Result = GetObjectStatus(Drv, Object, &Busy, &Ready);
    if (DT_SUCCEEDED(Result) && !Ready)
    {
        uint32_t Words[ACTIVATE_NUM_WORDS];
        bool HasData = false;

        memset(Words, 0, sizeof(Words));
        Result = ReadEepromData(Drv, Words, &HasData);
        if (DT_SUCCEEDED(Result))
            Result = HandData(Drv, Object, Words, HasData ? ACTIVATE_NUM_WORDS : 0);

        for (int Waited = 0; DT_SUCCEEDED(Result) && Waited < ACTIVATE_TIMEOUT_MS;
             Waited += ACTIVATE_POLL_MS)
        {
            Result = GetObjectStatus(Drv, Object, &Busy, &Ready);
            if (!Busy)
                break;
            OsTime_SleepMs(ACTIVATE_POLL_MS);
        }
    }

    DtPcieCmd_ExclAccess(Drv, Object, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    if (!DT_SUCCEEDED(Result))
        return Result;
    return Ready ? DTAPI_OK : DTAPI_E_INVALID;
}
