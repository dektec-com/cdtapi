// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtInpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#*#*# (C) 2026 DekTec
//
// CDTAPI - The input channel: DtInpChannel on a DtPcie port
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An input channel checks the arguments, admits one caller at a time, and keeps the side
// that receives for the port's I/O standard. This file holds the checks and the order
// they are made in, the lock, attaching and detaching, and the waits of a read. The
// receiving is the side's, a DtRx behind the functions of DtRxBackend.h: DtSdiRx.c for
// raw SDI frames, DtAsiRx.c for a transport stream over ASI (plan 0011).

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"    // Allocation seam.
#include "Device/DtDevice.h" // The device and its port capabilities.
#include "DtAsiRx.h"         // The ASI side.
#include "DtIoConfig.h"      // Validating I/O configurations.
#include "DtPcieAbi.h"       // DT_FWSTATUS_ values.
#include "DtSdiRx.h"         // The SDI side.
#include "OAL/OsThread.h"    // The lock, sleeping, the clock.
#include "cdtapi.h"          // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Internal names for the values the checks below use.
//

#define DT_INSTANT_DETACH 1 // DTAPI_INSTANT_DETACH

#define DT_RXMODE_TS 0x10       // DTAPI_RXMODE_TS
#define DT_RXMODE_TS_MASK 0x1F  // DTAPI_RXMODE_TS_MASK
#define DT_RXMODE_STRAW 0x14    // DTAPI_RXMODE_STRAW
#define DT_RXMODE_STL3 0x15     // DTAPI_RXMODE_STL3
#define DT_RXMODE_STL3FULL 0x16 // DTAPI_RXMODE_STL3FULL
#define DT_RXMODE_STTRP 0x19    // DTAPI_RXMODE_STTRP
#define DT_RXMODE_TIMESTAMP32 0x01000000
#define DT_RXMODE_TIMESTAMP64 0x02000000
#define DT_RXMODE_TIMESTAMP_TOD 0x04000000

// How long a detach waits for users of the channel: ten times 10 ms.
#define DT_DETACH_TRIES 10
#define DT_DETACH_PAUSE_MS 10

// How often a read on a channel that is not receiving looks again.
#define DT_IDLE_POLL_MS 10

// The most a read with a time-out of 0 takes at a time.
#define DT_READ_BLOCK (1024 * 1024)

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtInpChannel
{
    OsMutex* Lock; // Guards everything below
    bool Attached;
    int Detachers; // Detaches waiting for the read to return
    bool Reading;  // A read is between its start and its return

    DtDevice Device; // The channel's own handle to the device
    DtRxPort Port;
    DtRx* Rx; // The side that receives, while attached
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LockAttached -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the lock of an attached channel, which is what admits a call. Returns
// DTAPI_E_NOT_ATTACHED, without the lock, otherwise.
//
static DtapiResult LockAttached(DtInpChannel* Chan)
{
    OsMutex_Lock(Chan->Lock);
    if (!Chan->Attached)
    {
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseSide -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
static void ReleaseSide(DtInpChannel* Chan)
{
    if (Chan->Rx != NULL)
        Chan->Rx->Ops->Release(Chan->Rx);
    Chan->Rx = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Asks a read on another thread to return, waits for it up to Tries pauses of 10 ms, or
// without a limit for -1, then stops and releases the side and the device.
//
// A detach that gives up with DTAPI_E_TIMEOUT withdraws its request, so the channel stays
// attached and usable. The count of waiting detaches keeps a detach that gives up from
// withdrawing the request of another that still waits, and a detach that finds the
// channel detached by another while it waited returns DTAPI_E_NOT_ATTACHED.
//
static DtapiResult Detach(DtInpChannel* Chan, int DetachMode, int Tries)
{
    if (LockAttached(Chan) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    Chan->Detachers++;
    for (int Try = 0; Chan->Attached && Chan->Reading; Try++)
    {
        if (Try == Tries)
        {
            Chan->Detachers--;
            OsMutex_Unlock(Chan->Lock);
            return DTAPI_E_TIMEOUT;
        }
        OsMutex_Unlock(Chan->Lock);
        OsTime_SleepMs(DT_DETACH_PAUSE_MS);
        OsMutex_Lock(Chan->Lock);
    }
    Chan->Detachers--;

    if (!Chan->Attached)
    {
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    if ((DetachMode & DT_INSTANT_DETACH) != 0)
        Chan->Rx->Ops->ClearFifo(Chan->Rx);
    Chan->Rx->Ops->SetRxControl(Chan->Rx, DTAPI_RXCTRL_IDLE);

    ReleaseSide(Chan);
    DtDevice_Release(&Chan->Device);
    Chan->Attached = false;
    OsMutex_Unlock(Chan->Lock);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtInpChannel* DtInpChannel_Alloc(void)
{
    DtInpChannel* Chan = (DtInpChannel*)DtAlloc_Malloc(sizeof(DtInpChannel));

    if (Chan == NULL)
        return NULL;
    memset(Chan, 0, sizeof(*Chan));

    Chan->Lock = OsMutex_Create();
    if (Chan->Lock == NULL)
    {
        DtAlloc_Free(Chan);
        return NULL;
    }
    return Chan;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// An instant detach whose result is ignored. Unlike a detach, it waits for a read on
// another thread to return for as long as it takes, so that it does not use the channel
// after it is gone.
//
void DtInpChannel_Free(DtInpChannel* InpChannel)
{
    if (InpChannel == NULL)
        return;

    Detach(InpChannel, DT_INSTANT_DETACH, -1);
    OsMutex_Destroy(InpChannel->Lock);
    DtAlloc_Free(InpChannel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
void DtInpChannel_Freep(DtInpChannel** InpChannel)
{
    if (InpChannel == NULL)
        return;

    DtInpChannel_Free(*InpChannel);
    *InpChannel = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- CheckFailSafe -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// A fail-safe port in fail-safe mode is reported, as a success. A failure to read it
// releases the side the channel attached.
//
static DtapiResult CheckFailSafe(DtInpChannel* Chan, const DtIoConfig* Config)
{
    if ((Chan->Port.Caps & DT_CAP_FAILSAFE) == 0)
        return DTAPI_OK;

    DtIoConfig FailSafe = *Config;
    FailSafe.Group = DTAPI_IOCONFIG_FAILSAFE;
    DtapiResult Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &FailSafe);
    if (Result != DTAPI_OK)
    {
        ReleaseSide(Chan);
        return Result;
    }
    return FailSafe.Value == DTAPI_IOCONFIG_TRUE ? DTAPI_OK_FAILSAFE : DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// AttachToPort's steps once the channel has its own handle to the device, which the
// caller releases when they fail.
//
static DtapiResult AttachPort(DtInpChannel* Chan, int Port, uint64_t Caps)
{
    Chan->Port.Device = &Chan->Device;
    Chan->Port.Port = Port;
    Chan->Port.PortIndex = Port - 1;
    Chan->Port.Caps = Caps;

    // The DMA-rate test mode is switched off first.
    DtIoConfig Config;
    memset(&Config, 0, sizeof(Config));
    Config.Port = Port;
    Config.ParXtra[0] = Config.ParXtra[1] = -1;
    if ((Caps & DT_CAP_DMATESTMODE) != 0)
    {
        Config.Group = DTAPI_IOCONFIG_DMATESTMODE;
        Config.Value = DTAPI_IOCONFIG_FALSE;
        Config.SubValue = -1;
        DtapiResult Result = DtPcieCmd_SetIoConfig(Chan->Device.Drv, &Config);
        if (Result != DTAPI_OK)
            return Result;
    }

    Config.Group = DTAPI_IOCONFIG_IODIR;
    DtapiResult Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &Config);
    if (Result == DTAPI_OK && Config.Value != DTAPI_IOCONFIG_INPUT)
        Result = DTAPI_E_NO_DT_INPUT;
    if (Result != DTAPI_OK)
        return Result;

    Config.Group = DTAPI_IOCONFIG_IOSTD;
    Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &Config);
    if (Result != DTAPI_OK)
        return Result;

    if (Config.Value == DTAPI_IOCONFIG_ASI)
        Result = DtAsiRx_Attach(&Chan->Port, &Chan->Rx);
    else
        Result = DtSdiRx_Attach(&Chan->Port, &Config, &Chan->Rx);
    if (Result != DTAPI_OK)
        return Result;
    return CheckFailSafe(Chan, &Config);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_AttachToPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The channel's checks, the port's, and then the side's, in that order.
//
static DtapiResult Attach(DtInpChannel* Chan, DtDevice* Device, int Port)
{
    if (Device == NULL || Device->Drv == NULL)
        return DTAPI_E_DEVICE;
    if (Device->Info.FirmwareStatus == DT_FWSTATUS_OBSOLETE)
        return DTAPI_E_OBSOLETE_FW;
    if (Device->Info.FirmwareStatus == DT_FWSTATUS_TAINTED)
        return DTAPI_E_TAINTED_FW;
    if (Port < 1 || Port > Device->NumPublicPorts)
        return DTAPI_E_NO_SUCH_PORT;

    uint64_t Caps = Device->PortCaps[Port - 1];
    if ((Caps & DT_CAP_INPUT) == 0 && (Caps & DT_CAP_IP) == 0)
        return DTAPI_E_NO_DT_INPUT;
    if ((Caps & DT_CAP_MATRIX) != 0 || (Caps & DT_CAP_ASI) == 0)
        return DTAPI_E_NOT_SUPPORTED;

    DtapiResult Result =
        DtDevice_AttachIndex(&Chan->Device, Device->Index, true, Device->Info.Serial);
    if (Result != DTAPI_OK)
        return Result;

    Result = AttachPort(Chan, Port, Caps);
    if (Result >= DTAPI_E)
        DtDevice_Release(&Chan->Device);
    return Result;
}

DtapiResult DtInpChannel_AttachToPort(DtInpChannel* InpChannel, DtDevice* Device,
                                      int Port)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    OsMutex_Lock(InpChannel->Lock);
    DtapiResult Result;
    if (InpChannel->Attached)
        Result = DTAPI_E_ATTACHED;
    else
    {
        Result = Attach(InpChannel, Device, Port);
        InpChannel->Attached = Result < DTAPI_E;
    }
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Waits up to 100 ms for a read to return, then stops, releases the side and the device.
//
DtapiResult DtInpChannel_Detach(DtInpChannel* InpChannel, int DetachMode)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    return Detach(InpChannel, DetachMode, DT_DETACH_TRIES);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_ClearFifo(DtInpChannel* InpChannel)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->Rx->Ops->ClearFifo(InpChannel->Rx);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.- DtInpChannel_SetConversionThreads -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_SetConversionThreads(DtInpChannel* InpChannel, int Threads)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // The threads are started and stopped here, and the working buffers with them, so a
    // read that is between its start and its return would find them changing under it.
    const DtRxBackend* Ops = InpChannel->Rx->Ops;
    DtapiResult Result = InpChannel->Reading ? DTAPI_E_IN_USE
                         : Ops->SetConversionThreads == NULL
                             ? DTAPI_E_NOT_SUPPORTED
                             : Ops->SetConversionThreads(InpChannel->Rx, Threads);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.- DtInpChannel_SetConversionDispatch -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_SetConversionDispatch(DtInpChannel* InpChannel,
                                               DtDispatchFunc Dispatch, void* User,
                                               int Pieces)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    const DtRxBackend* Ops = InpChannel->Rx->Ops;
    DtapiResult Result =
        InpChannel->Reading ? DTAPI_E_IN_USE
        : Ops->SetConversionDispatch == NULL
            ? DTAPI_E_NOT_SUPPORTED
            : Ops->SetConversionDispatch(InpChannel->Rx, Dispatch, User, Pieces);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_ClearFlags(DtInpChannel* InpChannel, int Latched)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->Rx->Ops->ClearFlags(InpChannel->Rx, Latched);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_DetectIoStd -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Detecting needs an ASI, SDI or SPI capability; the side detects.
//
DtapiResult DtInpChannel_DetectIoStd(DtInpChannel* InpChannel, int* Value, int* SubValue)
{
    const uint64_t Usable = DT_CAP_ASI | DT_CAP_SDI | DT_CAP_HDSDI | DT_CAP_3GSDI |
                            DT_CAP_SPI | DT_CAP_SPISDI;

    if (InpChannel == NULL || Value == NULL || SubValue == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    const DtRxBackend* Ops = InpChannel->Rx->Ops;
    DtapiResult Result;
    if ((InpChannel->Port.Caps & Usable) == 0 || Ops->DetectIoStd == NULL)
        Result = DTAPI_E_NOT_SUPPORTED;
    else
        Result = Ops->DetectIoStd(InpChannel->Rx, Value, SubValue);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_GetFifoLoad(DtInpChannel* InpChannel, int* FifoLoad)
{
    if (InpChannel == NULL || FifoLoad == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->Rx->Ops->GetFifoLoad(InpChannel->Rx, FifoLoad);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_GetMaxFifoSize(DtInpChannel* InpChannel, int* MaxFifoSize)
{
    if (InpChannel == NULL || MaxFifoSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->Rx->Ops->GetMaxFifoSize(InpChannel->Rx, MaxFifoSize);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_GetFlags(DtInpChannel* InpChannel, int* Flags, int* Latched)
{
    if (InpChannel == NULL || Flags == NULL || Latched == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->Rx->Ops->GetFlags(InpChannel->Rx, Flags, Latched);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// -1 in every output first, the group checked, and then what DtDevice_GetIoConfig
// checks and reads of the channel's port.
//
DtapiResult DtInpChannel_GetIoConfig(DtInpChannel* InpChannel, int Group, int* Value,
                                     int* SubValue, int64_t* ParXtra0, int64_t* ParXtra1)
{
    DtIoConfig Config = {0, Group, -1, -1, {-1, -1}};
    DtapiResult Result = DTAPI_OK;

    if (Value != NULL)
        *Value = -1;
    if (SubValue != NULL)
        *SubValue = -1;
    if (ParXtra0 != NULL)
        *ParXtra0 = -1;
    if (ParXtra1 != NULL)
        *ParXtra1 = -1;
    if (InpChannel == NULL || Value == NULL)
        return DTAPI_E_INVALID_ARG;
    Result = DtIoConfig_CheckGroup(Group);
    if (Result != DTAPI_OK)
        return Result;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    Config.Port = InpChannel->Port.Port;
    Result = DtDevice_GetIoConfig(&InpChannel->Device, &Config, 1);
    OsMutex_Unlock(InpChannel->Lock);
    *Value = Config.Value;
    if (SubValue != NULL)
        *SubValue = Config.SubValue;
    if (ParXtra0 != NULL)
        *ParXtra0 = Config.ParXtra[0];
    if (ParXtra1 != NULL)
        *ParXtra1 = Config.ParXtra[1];
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The channel's checks, then the side's. A configuration the port lacks a capability for
// is refused by the driver. A direction is not applied, once the checks of it have passed
// (plan 0007).
//
// A standard that crosses between SDI and ASI releases the one side, sets the
// configuration and attaches the other, with its default receive mode. When that fails
// the channel is left detached.
//
DtapiResult DtInpChannel_SetIoConfig(DtInpChannel* InpChannel, int Group, int Value,
                                     int SubValue, int64_t ParXtra0, int64_t ParXtra1)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = DtIoConfig_IsValid(Group, Value, SubValue);
    if (Result != DTAPI_OK)
        return Result;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    if (Group == DTAPI_IOCONFIG_IODIR && Value == DTAPI_IOCONFIG_OUTPUT)
        Result = DTAPI_E_INVALID_ARG;
    else if (Group == DTAPI_IOCONFIG_IODIR && Value == DTAPI_IOCONFIG_INPUT &&
             SubValue == DTAPI_IOCONFIG_SHAREDANT &&
             (ParXtra0 < 1 || ParXtra0 > InpChannel->Device.NumPorts))
    {
        Result = DTAPI_E_INVALID_ARG;
    }
    else if (Group == DTAPI_IOCONFIG_IODIR)
        Result = DTAPI_E_NOT_SUPPORTED;
    else if (InpChannel->Rx->RxControl != DTAPI_RXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else
    {
        DtIoConfig Config;
        Config.Port = InpChannel->Port.Port;
        Config.Group = Group;
        Config.Value = Value;
        Config.SubValue = SubValue;
        Config.ParXtra[0] = ParXtra0;
        Config.ParXtra[1] = ParXtra1;
        const bool IsAsi = InpChannel->Rx->Ops->Take != NULL;
        const bool NewAsi = Value == DTAPI_IOCONFIG_ASI;

        if (Group == DTAPI_IOCONFIG_IOSTD && NewAsi != IsAsi)
        {
            ReleaseSide(InpChannel);
            Result = DtPcieCmd_SetIoConfig(InpChannel->Device.Drv, &Config);
            if (Result == DTAPI_OK && NewAsi)
                Result = DtAsiRx_Attach(&InpChannel->Port, &InpChannel->Rx);
            else if (Result == DTAPI_OK)
                Result = DtSdiRx_Attach(&InpChannel->Port, &Config, &InpChannel->Rx);
            if (Result != DTAPI_OK)
            {
                DtDevice_Release(&InpChannel->Device);
                InpChannel->Attached = false;
            }
        }
        else
        {
            Result = DtPcieCmd_SetIoConfig(InpChannel->Device.Drv, &Config);
            if (Result == DTAPI_OK)
                Result = InpChannel->Rx->Ops->ApplyIoConfig(InpChannel->Rx, &Config);
        }
    }
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetRxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_SetRxControl(DtInpChannel* InpChannel, int RxControl)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = InpChannel->Rx->Ops->SetRxControl(InpChannel->Rx, RxControl);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_SetRxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The channel's checks, then the side's.
//
DtapiResult DtInpChannel_SetRxMode(DtInpChannel* InpChannel, int RxMode)
{
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    // DTAPI_RXMODE_SDI without a mode bit means the full frame, as in DTAPI.
    if ((RxMode & DTAPI_RXMODE_SDI) != 0 && (RxMode & DTAPI_RXMODE_SDI_MODE_BITS) == 0)
        RxMode |= DTAPI_RXMODE_SDI_FULL;
    if ((RxMode & DT_RXMODE_TS) != 0 && (RxMode & DTAPI_RXMODE_SDI) != 0)
        return DTAPI_E_INVALID_MODE;
    if ((RxMode & DT_RXMODE_TIMESTAMP32) != 0 && (RxMode & DT_RXMODE_TIMESTAMP64) != 0 &&
        (RxMode & DT_RXMODE_TIMESTAMP_TOD) != 0)
    {
        return DTAPI_E_INVALID_MODE;
    }
    if ((RxMode & (DT_RXMODE_TIMESTAMP32 | DT_RXMODE_TIMESTAMP64 |
                   DT_RXMODE_TIMESTAMP_TOD)) != 0 &&
        (RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STRAW)
    {
        return DTAPI_E_INVALID_MODE;
    }
    if ((RxMode & DTAPI_RXMODE_SDI_MASK) == DTAPI_RXMODE_SDI_ACTVID &&
        (RxMode & DTAPI_RXMODE_SDI_HUFFMAN) != DTAPI_RXMODE_SDI_HUFFMAN)
    {
        return DTAPI_E_INVALID_MODE;
    }

    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result;
    uint64_t Caps = InpChannel->Port.Caps;
    if (((RxMode & DT_RXMODE_TS) == DT_RXMODE_TS && (Caps & DT_CAP_TS) == 0) ||
        ((RxMode & DT_RXMODE_TIMESTAMP64) == DT_RXMODE_TIMESTAMP64 &&
         (Caps & DT_CAP_TIMESTAMP64) == 0) ||
        ((RxMode & DTAPI_RXMODE_SDI_HUFFMAN) == DTAPI_RXMODE_SDI_HUFFMAN &&
         (Caps & DT_CAP_HUFFMAN) == 0) ||
        ((RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STL3 && (Caps & DT_CAP_L3MODE) == 0) ||
        ((RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STL3FULL &&
         (Caps & DT_CAP_L3MODE) == 0) ||
        ((RxMode & DT_RXMODE_TS_MASK) == DT_RXMODE_STTRP &&
         (Caps & DT_CAP_TRPMODE) == 0) ||
        ((RxMode & DTAPI_RXMODE_SDI_10B_NBO) == DTAPI_RXMODE_SDI_10B_NBO &&
         (Caps & DT_CAP_SDI10BNBO) == 0))
    {
        Result = DTAPI_E_INVALID_MODE;
    }
    else
        Result = InpChannel->Rx->Ops->SetRxMode(InpChannel->Rx, RxMode);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Reading +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- WaitMore -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// A read's wait, without the lock, for at most Remaining milliseconds or without a limit
// for -1: while not receiving a sleep, and otherwise the side's wait. While this read
// waited, another thread may have changed the side; what the wait saw is then not the
// new side's to deal with. Gives DTAPI_E_CANCELLED while a detach waits.
//
static DtapiResult WaitMore(DtInpChannel* Chan, int64_t Remaining)
{
    DtRxWait Wait;
    Chan->Rx->Ops->PrepareWait(Chan->Rx, &Wait);
    int Ms = Wait.MaxMs;
    if (Remaining >= 0 && Remaining < Ms)
        Ms = (int)Remaining;

    DtapiResult Result = DTAPI_OK;
    if (Chan->Rx->RxControl == DTAPI_RXCTRL_IDLE)
    {
        OsMutex_Unlock(Chan->Lock);
        OsTime_SleepMs(Ms < DT_IDLE_POLL_MS ? Ms : DT_IDLE_POLL_MS);
        OsMutex_Lock(Chan->Lock);
    }
    else
    {
        OsMutex_Unlock(Chan->Lock);
        Result = Wait.Ops->Wait(&Wait, Ms);
        OsMutex_Lock(Chan->Lock);
        if (Result == DTAPI_OK && Chan->Detachers == 0 && Chan->Rx->Ops == Wait.Ops)
            Result = Chan->Rx->Ops->AfterWait(Chan->Rx, &Wait);
    }
    return Chan->Detachers > 0 ? DTAPI_E_CANCELLED : Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ReadFrame2 -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The checks a frame read makes, and DTAPI_E_IN_USE while a read on another thread has
// not returned: two reads would take frames from the ring in no set order. Then, until a
// frame is taken, the time is up or the channel is being detached: take a frame if the
// side holds one, and otherwise wait without the lock, so that the channel is usable
// while this read waits.
//
DtapiResult DtInpChannel_ReadFrame2(DtInpChannel* InpChannel, void* FrameBuffer,
                                    int* FrameSize, int TimeOut, DtTimeOfDay* ArrivalTime)
{
    uint64_t Start = OsTime_MonotonicMs();
    DtTimeOfDay Arrival = {0, 0};

    if (ArrivalTime != NULL)
        *ArrivalTime = Arrival;
    if (InpChannel == NULL || FrameSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (*FrameSize == 0)
        return DTAPI_E_BUF_TOO_SMALL;
    if (TimeOut != -1 && TimeOut <= 0)
        return DTAPI_E_INVALID_TIMEOUT;
    if (*FrameSize < 0 || *FrameSize % 4 != 0)
        return DTAPI_E_INVALID_SIZE;
    if (FrameBuffer == NULL || (uintptr_t)FrameBuffer % 4 != 0)
        return DTAPI_E_INVALID_BUF;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;
    if (InpChannel->Detachers > 0)
    {
        OsMutex_Unlock(InpChannel->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }
    if (InpChannel->Reading)
    {
        OsMutex_Unlock(InpChannel->Lock);
        return DTAPI_E_IN_USE;
    }
    if (InpChannel->Rx->Ops->CheckFrame == NULL)
    {
        OsMutex_Unlock(InpChannel->Lock);
        return DTAPI_E_NOT_SDI_MODE;
    }

    size_t RawSize;
    DtapiResult Result =
        InpChannel->Rx->Ops->CheckFrame(InpChannel->Rx, *FrameSize, &RawSize);

    InpChannel->Reading = true;
    while (Result == DTAPI_OK)
    {
        // While this read waited without the lock, another thread may have stopped the
        // channel, changed its standard or receive mode, and started it again.
        DtRx* Rx = InpChannel->Rx;
        if (Rx->Ops->CheckFrame == NULL)
        {
            Result = DTAPI_E_NOT_SDI_MODE;
            break;
        }
        if (Rx->RxControl != DTAPI_RXCTRL_IDLE)
        {
            bool Taken = false;

            Result = Rx->Ops->CheckFrame(Rx, *FrameSize, &RawSize);
            if (Result == DTAPI_OK)
                Result = Rx->Ops->TakeFrame(Rx, (uint8_t*)FrameBuffer, &Arrival, &Taken);
            if (Result != DTAPI_OK || Taken)
                break;
        }

        uint64_t Elapsed = OsTime_MonotonicMs() - Start;
        if (TimeOut != -1 && Elapsed >= (uint64_t)TimeOut)
        {
            Result = DTAPI_E_TIMEOUT;
            break;
        }
        Result = WaitMore(InpChannel,
                          TimeOut == -1 ? -1 : (int64_t)TimeOut - (int64_t)Elapsed);
    }
    InpChannel->Reading = false;

    *FrameSize = Result == DTAPI_OK ? (int)RawSize : 0;
    if (ArrivalTime != NULL && Result == DTAPI_OK)
        *ArrivalTime = Arrival;
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_ReadFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_ReadFrame(DtInpChannel* InpChannel, void* FrameBuffer,
                                   int* FrameSize, int TimeOut)
{
    return DtInpChannel_ReadFrame2(InpChannel, FrameBuffer, FrameSize, TimeOut, NULL);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= ASI +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_Read -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The read's checks of its parameters, and DTAPI_E_IN_USE while a read on another thread
// has not returned. With a time-out of 0 the read takes 1 MB at a time as each is there;
// with any other it waits for all of it, which may not exceed the FIFO. It waits without
// the lock, so a detach ends it with DTAPI_E_CANCELLED.
//
DtapiResult DtInpChannel_Read(DtInpChannel* InpChannel, void* Buffer, int NumBytesToRead,
                              int TimeOut)
{
    const uint64_t Start = OsTime_MonotonicMs();

    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (NumBytesToRead == 0)
        return DTAPI_OK;
    if (TimeOut < -1)
        return DTAPI_E_INVALID_TIMEOUT;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtRx* Rx = InpChannel->Rx;
    int MaxFifoSize = 0;
    DtapiResult Result = DTAPI_OK;
    if (InpChannel->Detachers > 0)
        Result = DTAPI_E_NOT_ATTACHED;
    else if (InpChannel->Reading)
        Result = DTAPI_E_IN_USE;
    else if (Rx->Ops->Take == NULL)
        Result = DTAPI_E_NOT_SUPPORTED;
    else if (NumBytesToRead < 0 || NumBytesToRead % 4 != 0)
        Result = DTAPI_E_INVALID_SIZE;
    else if (Buffer == NULL || (uintptr_t)Buffer % 4 != 0)
        Result = DTAPI_E_INVALID_BUF;
    else if (TimeOut != 0)
    {
        Result = Rx->Ops->GetMaxFifoSize(Rx, &MaxFifoSize);
        if (Result == DTAPI_OK && NumBytesToRead > MaxFifoSize)
            Result = DTAPI_E_INVALID_SIZE;
    }
    if (Result != DTAPI_OK)
    {
        OsMutex_Unlock(InpChannel->Lock);
        return Result;
    }

    uint8_t* Out = (uint8_t*)Buffer;
    size_t Left = (size_t)NumBytesToRead;
    InpChannel->Reading = true;
    while (Result == DTAPI_OK && Left > 0)
    {
        // While this read waited without the lock, another thread may have stopped the
        // channel, or switched it to SDI.
        Rx = InpChannel->Rx;
        if (Rx->Ops->Take == NULL)
        {
            Result = DTAPI_E_NOT_SUPPORTED;
            break;
        }
        const size_t Block = TimeOut == 0 && Left > DT_READ_BLOCK ? DT_READ_BLOCK : Left;
        size_t Load = 0;
        if (Rx->RxControl == DTAPI_RXCTRL_RCV)
            Result = Rx->Ops->GetLoad(Rx, &Load);
        if (Result == DTAPI_OK && Load >= Block)
        {
            Result = Rx->Ops->Take(Rx, Out, Block);
            Out += Block;
            Left -= Block;
            continue;
        }
        if (Result != DTAPI_OK)
            break;

        const uint64_t Elapsed = OsTime_MonotonicMs() - Start;
        if (TimeOut > 0 && Elapsed >= (uint64_t)TimeOut)
        {
            Result = DTAPI_E_TIMEOUT;
            break;
        }
        Result =
            WaitMore(InpChannel, TimeOut > 0 ? (int64_t)TimeOut - (int64_t)Elapsed : -1);
    }
    InpChannel->Reading = false;
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetStatus -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtInpChannel_GetStatus(DtInpChannel* InpChannel, int* PacketSize, int* NumInv,
                                   int* ClkDet, int* AsiLock, int* RateOk, int* AsiInv)
{
    if (InpChannel == NULL || PacketSize == NULL || NumInv == NULL || ClkDet == NULL ||
        AsiLock == NULL || RateOk == NULL || AsiInv == NULL)
    {
        return DTAPI_E_INVALID_ARG;
    }
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtRx* Rx = InpChannel->Rx;
    DtapiResult Result =
        Rx->Ops->GetStatus == NULL
            ? DTAPI_E_NOT_SUPPORTED
            : Rx->Ops->GetStatus(Rx, PacketSize, NumInv, ClkDet, AsiLock, RateOk, AsiInv);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_GetTsRateBps(DtInpChannel* InpChannel, int* TsRate)
{
    if (InpChannel == NULL || TsRate == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtRx* Rx = InpChannel->Rx;
    DtapiResult Result = Rx->Ops->GetTsRateBps == NULL
                             ? DTAPI_E_NOT_SUPPORTED
                             : Rx->Ops->GetTsRateBps(Rx, TsRate);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_GetViolCount -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtInpChannel_GetViolCount(DtInpChannel* InpChannel, int* ViolCount)
{
    if (InpChannel == NULL || ViolCount == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtRx* Rx = InpChannel->Rx;
    DtapiResult Result = Rx->Ops->GetViolCount == NULL
                             ? DTAPI_E_NOT_SUPPORTED
                             : Rx->Ops->GetViolCount(Rx, ViolCount);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtInpChannel_PolarityControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The value is checked before anything else.
//
DtapiResult DtInpChannel_PolarityControl(DtInpChannel* InpChannel, int Polarity)
{
    if (Polarity != DTAPI_POLARITY_AUTO && Polarity != DTAPI_POLARITY_NORMAL &&
        Polarity != DTAPI_POLARITY_INVERT)
    {
        return DTAPI_E_INVALID_MODE;
    }
    if (InpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(InpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtRx* Rx = InpChannel->Rx;
    DtapiResult Result = Rx->Ops->PolarityControl == NULL
                             ? DTAPI_E_NOT_SUPPORTED
                             : Rx->Ops->PolarityControl(Rx, Polarity);
    OsMutex_Unlock(InpChannel->Lock);
    return Result;
}
