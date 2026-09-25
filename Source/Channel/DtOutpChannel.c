// #*#*#*#*#*#*#*#*#*#*#*#*#*# DtOutpChannel.c *#*#*#*#*#*#*#*#*#*#*#*#*#* (C) 2026 DekTec
//
// CDTAPI - The output channel: DtOutpChannel on a DtPcie port
//
// SPDX-License-Identifier: BSD-3-Clause
//
// An output channel checks the arguments, admits one caller at a time, and keeps the side
// that transmits for the port's I/O standard. This file holds the checks and the order
// they are made in, the lock, attaching and detaching. The transmitting is the side's, a
// DtTx behind the functions of DtTxBackend.h: DtSdiTx.c for raw SDI frames, DtAsiTx.c for
// a transport stream over ASI (plan 0011).

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Include files -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.

// Standard includes
#include <stdint.h>
#include <string.h>

// CDTAPI includes
#include "Core/DtAlloc.h"    // Allocation seam.
#include "Core/DtWork.h"     // The pool the channel's work is divided over.
#include "Device/DtDevice.h" // The device and its port capabilities.
#include "DtAsiTx.h"         // The ASI side.
#include "DtIoConfig.h"      // Validating I/O configurations.
#include "DtPcieAbi.h"       // Firmware statuses.
#include "DtSdiTx.h"         // The SDI side.
#include "OAL/OsThread.h"    // The lock, sleeping, the clock.
#include "cdtapi.h"          // Interface being implemented.

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Constants +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=
//
// Internal names for the values the checks below use.
//

#define DT_INSTANT_DETACH 1  // DTAPI_INSTANT_DETACH
#define DT_WAIT_UNTIL_SENT 2 // DTAPI_WAIT_UNTIL_SENT

// How long a detach waits for users of the channel: ten times 10 ms.
#define DT_DETACH_TRIES 10
#define DT_DETACH_PAUSE_MS 10

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= State +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

struct DtOutpChannel
{
    OsMutex* Lock; // Guards everything below
    bool Attached;
    int Detachers; // Detaches waiting for the write to return
    bool Writing;  // A Write or WriteFrame call is between its start and its return

    DtDevice Device; // The channel's own handle to the device
    DtTxPort Port;
    DtTx* Tx; // The side that transmits, while attached

    // The pool the channel's work is divided over, NULL for none, and the pieces asked
    // for, 0 for as many as the signal calls for. Held from the setting until the next
    // one or the detach, across a change of side, and given to every side attached.
    DtWorkPool* WorkPool;
    int WorkThreads;
};

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Helpers +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- LockAttached -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Takes the lock of an attached channel, which is what admits a call. Returns
// DTAPI_E_NOT_ATTACHED, without the lock, otherwise.
//
static DtapiResult LockAttached(DtOutpChannel* Chan)
{
    OsMutex_Lock(Chan->Lock);
    if (!Chan->Attached)
    {
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- ReleaseAll -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Lets go of the side and the device, ignoring failures.
//
static void ReleaseAll(DtOutpChannel* Chan)
{
    if (Chan->Tx != NULL)
        Chan->Tx->Ops->Release(Chan->Tx);
    Chan->Tx = NULL;
    DtDevice_Release(&Chan->Device);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- GiveWork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Gives the side the channel's pool. A side short of memory for the pieces works in the
// writing thread, which is not a reason to fail an attach, so the result is ignored.
//
static void GiveWork(DtOutpChannel* Chan)
{
    if (Chan->Tx->Ops->SetWorkPool != NULL)
        Chan->Tx->Ops->SetWorkPool(Chan->Tx, Chan->WorkPool, Chan->WorkThreads);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DropWork -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Lets go of the pool when the channel detaches, as it forgets its other settings then.
//
static void DropWork(DtOutpChannel* Chan)
{
    DtWorkPool_Freep(&Chan->WorkPool);
    Chan->WorkThreads = 0;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- SetWorkPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// Holds Pool for the channel and gives it to the side, with the lock taken, attached,
// and no call between its start and its return. The pool is kept when the side is short
// of memory, so that the next standard tries again.
//
static DtapiResult SetWorkPool(DtOutpChannel* Chan, DtWorkPool* Pool, int NumThreads)
{
    DtapiResult Result = DTAPI_OK;
    if (Chan->Tx->Ops->SetWorkPool != NULL)
        Result = Chan->Tx->Ops->SetWorkPool(Chan->Tx, Pool, NumThreads);

    DtWorkPool_Hold(Pool);
    DtWorkPool_Free(Chan->WorkPool);
    Chan->WorkPool = Pool;
    Chan->WorkThreads = NumThreads;
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// Asks a write on another thread to return and waits for it up to Tries pauses of 10 ms,
// or without a limit for -1. Then, with DTAPI_WAIT_UNTIL_SENT and while sending, waits
// until the card has taken what was written; with DTAPI_INSTANT_DETACH forgets it. Stops,
// and releases everything.
//
// A detach that gives up with DTAPI_E_TIMEOUT withdraws its request, so the channel stays
// attached and usable; one that finds the channel detached by another while it waited
// returns DTAPI_E_NOT_ATTACHED.
//
static DtapiResult Detach(DtOutpChannel* Chan, int DetachMode, int Tries)
{
    if (LockAttached(Chan) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;
    if ((DetachMode & DT_INSTANT_DETACH) != 0 && (DetachMode & DT_WAIT_UNTIL_SENT) != 0)
    {
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_INVALID_FLAGS;
    }

    Chan->Detachers++;
    for (int Try = 0; Chan->Attached && Chan->Writing; Try++)
    {
        if (Try == Tries)
        {
            Chan->Detachers--;
            OsMutex_Unlock(Chan->Lock);
            return DTAPI_E_TIMEOUT;
        }
        Chan->Tx->Ops->Wake(Chan->Tx);
        OsMutex_Unlock(Chan->Lock);
        OsTime_SleepMs(DT_DETACH_PAUSE_MS);
        OsMutex_Lock(Chan->Lock);
    }
    if (!Chan->Attached)
    {
        Chan->Detachers--;
        OsMutex_Unlock(Chan->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    DtTx* Tx = Chan->Tx;
    if ((DetachMode & DT_WAIT_UNTIL_SENT) != 0 && Tx->TxControl == DTAPI_TXCTRL_SEND)
        Tx->Ops->WaitUntilSent(Tx);
    if ((DetachMode & DT_INSTANT_DETACH) != 0)
        Tx->Ops->ClearFifo(Tx);
    Tx->Ops->SetTxControl(Tx, DTAPI_TXCTRL_IDLE);

    ReleaseAll(Chan);
    Chan->Attached = false;
    DropWork(Chan);
    Chan->Detachers--;
    OsMutex_Unlock(Chan->Lock);
    return DTAPI_OK;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Lifetime +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Alloc -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtOutpChannel* DtOutpChannel_Alloc(void)
{
    DtOutpChannel* Chan = (DtOutpChannel*)DtAlloc_Malloc(sizeof(DtOutpChannel));

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

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Free -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// An instant detach whose result is ignored. Unlike a detach, it waits for a write on
// another thread to return for as long as it takes, so that it does not use the channel
// after it is gone.
//
void DtOutpChannel_Free(DtOutpChannel* OutpChannel)
{
    if (OutpChannel == NULL)
        return;

    Detach(OutpChannel, DT_INSTANT_DETACH, -1);
    OsMutex_Destroy(OutpChannel->Lock);
    DtAlloc_Free(OutpChannel);
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Freep -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
void DtOutpChannel_Freep(DtOutpChannel** OutpChannel)
{
    if (OutpChannel == NULL)
        return;

    DtOutpChannel_Free(*OutpChannel);
    *OutpChannel = NULL;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- AttachPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// AttachToPort's steps once the channel has its own handle to the device. The caller
// releases everything when they fail.
//
static DtapiResult AttachPort(DtOutpChannel* Chan, int Port, uint64_t Caps)
{
    Chan->Port.Device = &Chan->Device;
    Chan->Port.Port = Port;
    Chan->Port.PortIndex = Port - 1;
    Chan->Port.Caps = Caps;
    Chan->Port.Lock = Chan->Lock;
    Chan->Port.Detachers = &Chan->Detachers;

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
    if (Result == DTAPI_OK && Config.Value != DTAPI_IOCONFIG_OUTPUT)
        Result = DTAPI_E_NO_DT_OUTPUT;
    if (Result != DTAPI_OK)
        return Result;

    Config.Group = DTAPI_IOCONFIG_IOSTD;
    Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &Config);
    if (Result == DTAPI_OK &&
        !DtDevice_PortHasIoStdCaps(&Chan->Device, Port, Config.Value))
        Result = DTAPI_E_NOT_SUPPORTED;
    if (Result == DTAPI_OK && Config.Value == DTAPI_IOCONFIG_ASI)
        Result = DtAsiTx_Attach(&Chan->Port, &Chan->Tx);
    else if (Result == DTAPI_OK)
        Result = DtSdiTx_Attach(&Chan->Port, &Config, &Chan->Tx);
    if (Result != DTAPI_OK)
        return Result;
    GiveWork(Chan);

    // A fail-safe port in fail-safe mode is reported, as a success.
    if ((Caps & DT_CAP_FAILSAFE) != 0)
    {
        DtIoConfig FailSafe = Config;

        FailSafe.Group = DTAPI_IOCONFIG_FAILSAFE;
        Result = DtPcieCmd_GetIoConfig(Chan->Device.Drv, &FailSafe);
        if (Result != DTAPI_OK)
            return Result;
        if (FailSafe.Value == DTAPI_IOCONFIG_TRUE)
            return DTAPI_OK_FAILSAFE;
    }
    return DTAPI_OK;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_AttachToPort -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The channel's checks, the port's, and then the side's attach, in that order. A failure
// after the channel has its own handle lets go of all of it.
//
static DtapiResult Attach(DtOutpChannel* Chan, DtDevice* Device, int Port)
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
    if (!DtDevice_PortHasAnyCap(Device, Port, DT_CAP_OUTPUT | DT_CAP_IP))
        return DTAPI_E_NO_DT_OUTPUT;
    if (DtDevice_PortHasAllCaps(Device, Port, DT_CAP_MATRIX) ||
        (!DtDevice_PortHasAsiCaps(Device, Port) &&
         !DtDevice_PortHasSdiCaps(Device, Port)))
        return DTAPI_E_NOT_SUPPORTED;

    DtapiResult Result =
        DtDevice_AttachIndex(&Chan->Device, Device->Index, true, Device->Info.Serial);
    if (Result != DTAPI_OK)
        return Result;

    Result = AttachPort(Chan, Port, Caps);
    if (Result >= DTAPI_E)
        ReleaseAll(Chan);
    return Result;
}

DtapiResult DtOutpChannel_AttachToPort(DtOutpChannel* OutpChannel, DtDevice* Device,
                                       int Port)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    OsMutex_Lock(OutpChannel->Lock);
    DtapiResult Result;
    if (OutpChannel->Attached)
        Result = DTAPI_E_ATTACHED;
    else
    {
        Result = Attach(OutpChannel, Device, Port);
        OutpChannel->Attached = Result < DTAPI_E;
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Detach -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_Detach(DtOutpChannel* OutpChannel, int DetachMode)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    return Detach(OutpChannel, DetachMode, DT_DETACH_TRIES);
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Control +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetWorkPool -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_SetWorkPool(DtOutpChannel* OutpChannel, DtWorkPool* Pool,
                                      int NumThreads)
{
    if (OutpChannel == NULL || NumThreads < 0)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // The side sizes its working buffers here, so a write between its start and its
    // return would find them changing under it.
    DtapiResult Result = OutpChannel->Writing
                             ? DTAPI_E_IN_USE
                             : SetWorkPool(OutpChannel, Pool, NumThreads);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_ClearFifo -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_ClearFifo(DtOutpChannel* OutpChannel)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->ClearFifo(OutpChannel->Tx);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFifoLoad -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_GetFifoLoad(DtOutpChannel* OutpChannel, int* FifoLoad)
{
    if (OutpChannel == NULL || FifoLoad == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->GetFifoLoad(OutpChannel->Tx, FifoLoad);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_GetFifoSize(DtOutpChannel* OutpChannel, int* FifoSize)
{
    if (OutpChannel == NULL || FifoSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->GetFifoSize(OutpChannel->Tx, FifoSize);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetMaxFifoSize -.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_GetMaxFifoSize(DtOutpChannel* OutpChannel, int* MaxFifoSize)
{
    if (OutpChannel == NULL || MaxFifoSize == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result =
        OutpChannel->Tx->Ops->GetMaxFifoSize(OutpChannel->Tx, MaxFifoSize);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_GetFlags(DtOutpChannel* OutpChannel, int* Status, int* Latched)
{
    if (OutpChannel == NULL || Status == NULL || Latched == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->GetFlags(OutpChannel->Tx, Status, Latched);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// -1 in every output first, the group checked, and then what DtDevice_GetIoConfig
// checks and reads of the channel's port.
//
DtapiResult DtOutpChannel_GetIoConfig(DtOutpChannel* OutpChannel, int Group, int* Value,
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
    if (OutpChannel == NULL || Value == NULL)
        return DTAPI_E_INVALID_ARG;
    Result = DtIoConfig_CheckGroup(Group);
    if (Result != DTAPI_OK)
        return Result;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    Config.Port = OutpChannel->Port.Port;
    Result = DtDevice_GetIoConfig(&OutpChannel->Device, &Config, 1);
    OsMutex_Unlock(OutpChannel->Lock);
    *Value = Config.Value;
    if (SubValue != NULL)
        *SubValue = Config.SubValue;
    if (ParXtra0 != NULL)
        *ParXtra0 = Config.ParXtra[0];
    if (ParXtra1 != NULL)
        *ParXtra1 = Config.ParXtra[1];
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetIoConfig -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The channel's checks, then the side's. A configuration the port lacks a capability for
// is refused by the driver. The transmit mode is kept. A standard that crosses between
// SDI and ASI releases the one side, sets the configuration and attaches the other, with
// its default transmit mode; when that fails the channel is left detached.
//
DtapiResult DtOutpChannel_SetIoConfig(DtOutpChannel* OutpChannel, int Group, int Value,
                                      int SubValue, int64_t ParXtra0, int64_t ParXtra1)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;

    DtapiResult Result = DtIoConfig_IsValid(Group, Value, SubValue);
    if (Result != DTAPI_OK)
        return Result;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    // An input direction is refused, and an output that names another port needs that
    // port in ParXtra0.
    if (Group == DTAPI_IOCONFIG_IODIR && Value == DTAPI_IOCONFIG_INPUT)
        Result = DTAPI_E_INVALID_ARG;
    else if (Group == DTAPI_IOCONFIG_IODIR && Value == DTAPI_IOCONFIG_OUTPUT &&
             (SubValue == DTAPI_IOCONFIG_DBLBUF || SubValue == DTAPI_IOCONFIG_LOOPS2L3 ||
              SubValue == DTAPI_IOCONFIG_LOOPS2TS ||
              SubValue == DTAPI_IOCONFIG_LOOPTHR) &&
             (ParXtra0 < 1 || ParXtra0 > OutpChannel->Device.NumPorts))
    {
        Result = DTAPI_E_INVALID_ARG;
    }
    else if (Group == DTAPI_IOCONFIG_IOSTD &&
             !DtDevice_PortHasIoStdCaps(&OutpChannel->Device, OutpChannel->Port.Port,
                                        Value))
        Result = DTAPI_E_NOT_SUPPORTED;
    else if (OutpChannel->Tx->TxControl != DTAPI_TXCTRL_IDLE)
        Result = DTAPI_E_NOT_IDLE;
    else
    {
        DtIoConfig Config;
        Config.Port = OutpChannel->Port.Port;
        Config.Group = Group;
        Config.Value = Value;
        Config.SubValue = SubValue;
        Config.ParXtra[0] = ParXtra0;
        Config.ParXtra[1] = ParXtra1;
        DtTx* Tx = OutpChannel->Tx;
        const bool IsAsi = Tx->Ops->SetTsRateBps != NULL;
        const bool NewAsi = Value == DTAPI_IOCONFIG_ASI;

        if (Group == DTAPI_IOCONFIG_IOSTD && NewAsi != IsAsi)
        {
            Tx->Ops->Release(Tx);
            OutpChannel->Tx = NULL;
            Result = DtPcieCmd_SetIoConfig(OutpChannel->Device.Drv, &Config);
            if (Result == DTAPI_OK && NewAsi)
                Result = DtAsiTx_Attach(&OutpChannel->Port, &OutpChannel->Tx);
            else if (Result == DTAPI_OK)
                Result = DtSdiTx_Attach(&OutpChannel->Port, &Config, &OutpChannel->Tx);
            if (Result != DTAPI_OK)
            {
                ReleaseAll(OutpChannel);
                OutpChannel->Attached = false;
                DropWork(OutpChannel);
            }
            else
                GiveWork(OutpChannel);
        }
        else
        {
            if (Tx->Ops->BeforeIoConfig != NULL)
                Result = Tx->Ops->BeforeIoConfig(Tx);
            if (Result == DTAPI_OK)
                Result = DtPcieCmd_SetIoConfig(OutpChannel->Device.Drv, &Config);
            Result = Tx->Ops->ApplyIoConfig(Tx, &Config, Result);
        }
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxControl -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_SetTxControl(DtOutpChannel* OutpChannel, int TxControl)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->SetTxControl(OutpChannel->Tx, TxControl);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxMode -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The channel's checks, then the side's: no 192-byte packets; on SDI, the full frame
// added to a mode that names neither the full frame nor active video; and compression and
// network byte order only on a port that has them.
//
DtapiResult DtOutpChannel_SetTxMode(DtOutpChannel* OutpChannel, int TxMode, int StuffMode)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if ((TxMode & DTAPI_TXMODE_TS) != 0 && (TxMode & DTAPI_TXMODE_SDI) != 0)
        return DTAPI_E_INVALID_MODE;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    const uint64_t Caps = OutpChannel->Port.Caps;
    DtapiResult Result = DTAPI_OK;
    if ((TxMode & DTAPI_TXMODE_TS_MASK) == DTAPI_TXMODE_192)
        Result = DTAPI_E_INVALID_MODE;
    if (Result == DTAPI_OK && (TxMode & DTAPI_TXMODE_SDI) != 0)
    {
        if ((TxMode & DTAPI_TXMODE_SDI_MASK) != DTAPI_TXMODE_SDI_FULL &&
            (TxMode & DTAPI_TXMODE_SDI_MASK) != DTAPI_TXMODE_SDI_ACTVID)
        {
            TxMode |= DTAPI_TXMODE_SDI_FULL;
        }
        if (((TxMode & DTAPI_TXMODE_SDI_HUFFMAN) != 0 && (Caps & DT_CAP_HUFFMAN) == 0) ||
            ((TxMode & DTAPI_TXMODE_SDI_10B_NBO) != 0 && (Caps & DT_CAP_SDI10BNBO) == 0))
        {
            Result = DTAPI_E_INVALID_MODE;
        }
    }
    if (Result == DTAPI_OK)
        Result = OutpChannel->Tx->Ops->SetTxMode(OutpChannel->Tx, TxMode, StuffMode);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Writing +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_Write -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
// The write's checks, the four-byte checks always applied: the buffer's address and the
// size must be multiples of 4, and a failing check overrides an idle channel. A null
// buffer with bytes to write is refused when not idle. A write while a Write or
// WriteFrame on another thread has not returned gives DTAPI_E_IN_USE. Then the side takes
// the bytes, waiting for room without the lock.
//
DtapiResult DtOutpChannel_Write(DtOutpChannel* OutpChannel, const void* Buffer,
                                int NumBytesToWrite)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (NumBytesToWrite < 0)
        return DTAPI_E_INVALID_SIZE;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;
    if (OutpChannel->Detachers > 0)
    {
        OsMutex_Unlock(OutpChannel->Lock);
        return DTAPI_E_NOT_ATTACHED;
    }

    DtTx* Tx = OutpChannel->Tx;
    DtapiResult Result = Tx->TxControl == DTAPI_TXCTRL_IDLE ? DTAPI_E_IDLE : DTAPI_OK;
    if ((uintptr_t)Buffer % 4 != 0 || NumBytesToWrite % 4 != 0 ||
        (Result == DTAPI_OK && Buffer == NULL && NumBytesToWrite > 0))
    {
        Result = DTAPI_E_INVALID_BUF;
    }

    if (Result == DTAPI_OK && OutpChannel->Writing)
        Result = DTAPI_E_IN_USE;

    if (Result == DTAPI_OK)
    {
        OutpChannel->Writing = true;
        Result = Tx->Ops->Write(Tx, (const uint8_t*)Buffer, (size_t)NumBytesToWrite);
        OutpChannel->Writing = false;
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_WriteFrame -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
// The argument checks, then the channel's, then the side's.
//
DtapiResult DtOutpChannel_WriteFrame(DtOutpChannel* OutpChannel, const void* Frame,
                                     int FrameSize, int TimeOut)
{
    uint64_t Start = OsTime_MonotonicMs();

    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (TimeOut != -1 && TimeOut <= 0)
        return DTAPI_E_INVALID_TIMEOUT;
    if (FrameSize <= 0 || FrameSize % 4 != 0)
        return DTAPI_E_INVALID_SIZE;
    if (Frame == NULL || (uintptr_t)Frame % 4 != 0)
        return DTAPI_E_INVALID_BUF;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtTx* Tx = OutpChannel->Tx;
    DtapiResult Result = DTAPI_OK;
    if (OutpChannel->Detachers > 0)
        Result = DTAPI_E_NOT_ATTACHED;
    else if (Tx->TxControl == DTAPI_TXCTRL_IDLE)
        Result = DTAPI_E_IDLE;
    else if (OutpChannel->Writing)
        Result = DTAPI_E_IN_USE;
    else if (Tx->Ops->WriteFrame == NULL)
        Result = DTAPI_E_NOT_SDI_MODE;
    else
    {
        uint64_t Deadline = TimeOut == -1 ? DT_TX_NO_DEADLINE : Start + (uint64_t)TimeOut;

        OutpChannel->Writing = true;
        Result = Tx->Ops->WriteFrame(Tx, (const uint8_t*)Frame, FrameSize, Deadline);
        OutpChannel->Writing = false;
    }
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+= Flags and ASI +=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=

// .-.-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_ClearFlags -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_ClearFlags(DtOutpChannel* OutpChannel, int Latched)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->ClearFlags(OutpChannel->Tx, Latched);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_GetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_GetTsRateBps(DtOutpChannel* OutpChannel, int* TsRate)
{
    if (OutpChannel == NULL || TsRate == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtTx* Tx = OutpChannel->Tx;
    DtapiResult Result = Tx->Ops->GetTsRateBps == NULL
                             ? DTAPI_E_NOT_SUPPORTED
                             : Tx->Ops->GetTsRateBps(Tx, TsRate);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTsRateBps -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.-
//
DtapiResult DtOutpChannel_SetTsRateBps(DtOutpChannel* OutpChannel, int TsRate)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtTx* Tx = OutpChannel->Tx;
    DtapiResult Result = Tx->Ops->SetTsRateBps == NULL
                             ? DTAPI_E_NOT_SUPPORTED
                             : Tx->Ops->SetTsRateBps(Tx, TsRate);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}

// .-.-.-.-.-.-.-.-.-.-.-.-.-.- DtOutpChannel_SetTxPolarity -.-.-.-.-.-.-.-.-.-.-.-.-.-.-.
//
DtapiResult DtOutpChannel_SetTxPolarity(DtOutpChannel* OutpChannel, int TxPolarity)
{
    if (OutpChannel == NULL)
        return DTAPI_E_INVALID_ARG;
    if (LockAttached(OutpChannel) != DTAPI_OK)
        return DTAPI_E_NOT_ATTACHED;

    DtapiResult Result = OutpChannel->Tx->Ops->SetTxPolarity(OutpChannel->Tx, TxPolarity);
    OsMutex_Unlock(OutpChannel->Lock);
    return Result;
}
