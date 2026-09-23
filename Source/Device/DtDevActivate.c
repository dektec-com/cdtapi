// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtDevActivate.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - Activating a device at attach
//
// SPDX-License-Identifier: BSD-3-Clause
//
// At attach the part is handed the data the card's own EEPROM holds for it, and reports
// itself ready. Until it does, the firmware does not do its work: the card carries
// nothing, in either direction, whether over SDI, ASI or IP. Every card that has the part
// needs this; a card without it needs none of it.

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"          // The command that carries the data.
#include "DtDevActivate.h"         // Interface being implemented.
#include "DtPcie/DtPcieAbi.h"      // Vendored driver structures and IOCTL codes.
#include "DtPcie/DtPcieCmd.h"      // Properties and the VPD.
#include "DtPcie/DtPcieCmdIssue.h" // Issuing commands.
#include "OAL/OsThread.h"          // Sleeping between polls.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Internals +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// The part that is asked, and the data it takes: 64 words of the EEPROM, which sit
// behind the sections that hold the card's own description.
#define ACTIVATE_PART_UUID "BC_IPSECG#1_UUID"
#define ACTIVATE_NUM_WORDS 64

// How long the part may take to answer, in milliseconds, and the step between polls.
#define ACTIVATE_TIMEOUT_MS 50
#define ACTIVATE_POLL_MS 1

// How often to try for the part, a millisecond apart, before giving up on it.
#define ACTIVATE_ACQUIRE_TRIES 10

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Acquire -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The part answers nothing without exclusive access, so it is taken for the whole pass
// and released again.
//
static DtapiResult Acquire(OsDrv* Drv, DtPartRef Part)
{
    DtapiResult Result = DTAPI_E_IN_USE;

    for (int Try = 0; Try < ACTIVATE_ACQUIRE_TRIES && Result != DTAPI_OK; Try++)
    {
        if (Try > 0)
            OsTime_SleepMs(1);
        Result = DtPcieCmd_ExclAccess(Drv, Part, DT_EXCLUSIVE_ACCESS_CMD_ACQUIRE);
    }
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- FindPart -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The part belongs to the device rather than to a port, and is addressed by the UUID the
// driver gives it. DTAPI_E_NOT_FOUND for a device that has no such part.
//
static DtapiResult FindPart(OsDrv* Drv, DtPartRef* Part)
{
    Part->PortIndex = DT_PROPERTY_DEVICE;
    return DtPcieCmd_GetPropertyInt(Drv, ACTIVATE_PART_UUID, DT_PROPERTY_DEVICE,
                                    &Part->Uuid);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static DtapiResult GetStatus(OsDrv* Drv, DtPartRef Part, bool* Busy, bool* Ready)
{
    DtIoctlIpSecGCmdGetStatusOutput Out;

    memset(&Out, 0, sizeof(Out));
    DtapiResult Result =
        DtPcieCmd_IssuePlain(Drv, DT_IOCTL(DT_IOCTL_IPSECG_CMD), DT_IPSECG_CMD_GET_STATUS,
                             Part, &Out, sizeof(Out));
    if (!DT_SUCCEEDED(Result))
        return Result;

    *Busy = Out.m_IsBusy != 0;
    *Ready = Out.m_IsOk != 0;
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Apply -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Hands the part its data. A count of zero is what a card whose EEPROM holds nothing for
// it gets, and is a command of its own rather than no command at all.
//
static DtapiResult Apply(OsDrv* Drv, DtPartRef Part, const uint32_t* Words, int Count)
{
    const size_t InSize =
        sizeof(DtIoctlIpSecGCmdCheckInput) + (size_t)Count * sizeof(uint32_t);
    DtIoctlIpSecGCmdCheckInput* In = (DtIoctlIpSecGCmdCheckInput*)DtAlloc_Malloc(InSize);

    if (In == NULL)
        return DTAPI_E_OUT_OF_MEM;

    memset(In, 0, InSize);
    DtPcieCmd_InitHeader(&In->m_CmdHdr, DT_IPSECG_CMD_CHECK, Part);
    In->m_NumWords = Count;
    memcpy(In->m_Data, Words, (size_t)Count * sizeof(uint32_t));

    DtapiResult Result =
        DtPcieCmd_Issue(Drv, DT_IOCTL(DT_IOCTL_IPSECG_CMD), In, InSize, NULL, 0);
    DtAlloc_Free(In);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReadData -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The data lies in the EEPROM behind the read-only and read-write sections. A card whose
// EEPROM was never given any holds the same word throughout, which counts as none.
// The words are handed over most significant byte first, whatever order this processor
// keeps them in.
//
static DtapiResult ReadData(OsDrv* Drv, uint32_t* Words, bool* Present)
{
    DtVpdProperties Props;
    uint8_t Bytes[ACTIVATE_NUM_WORDS * sizeof(uint32_t)];

    *Present = false;
    DtapiResult Result = DtPcieCmd_VpdGetProperties(Drv, &Props);
    if (!DT_SUCCEEDED(Result))
        return Result;

    const int Offset = Props.RoSize + Props.RwSize;
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

    *Present = true;
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Activation +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtDevActivate_OnAttach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtDevActivate_OnAttach(OsDrv* Drv)
{
    DtPartRef Part;
    bool Busy = false;
    bool Ready = false;

    if (Drv == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = FindPart(Drv, &Part);
    if (Result == DTAPI_E_NOT_FOUND)
        return DTAPI_OK; // Nothing to activate on this device
    if (Result != DTAPI_OK)
        return Result;

    Result = Acquire(Drv, Part);
    if (Result != DTAPI_OK)
        return Result;

    Result = GetStatus(Drv, Part, &Busy, &Ready);
    if (DT_SUCCEEDED(Result) && !Ready)
    {
        uint32_t Words[ACTIVATE_NUM_WORDS];
        bool Present = false;

        memset(Words, 0, sizeof(Words));
        Result = ReadData(Drv, Words, &Present);
        if (DT_SUCCEEDED(Result))
            Result = Apply(Drv, Part, Words, Present ? ACTIVATE_NUM_WORDS : 0);

        for (int Waited = 0; DT_SUCCEEDED(Result) && Waited < ACTIVATE_TIMEOUT_MS;
             Waited += ACTIVATE_POLL_MS)
        {
            Result = GetStatus(Drv, Part, &Busy, &Ready);
            if (!Busy)
                break;
            OsTime_SleepMs(ACTIVATE_POLL_MS);
        }
    }

    DtPcieCmd_ExclAccess(Drv, Part, DT_EXCLUSIVE_ACCESS_CMD_RELEASE);
    if (!DT_SUCCEEDED(Result))
        return Result;
    return Ready ? DTAPI_OK : DTAPI_E_INVALID;
}
